// ============================================================================
// gm_extract  --  GM SoundFont -> iFFT_Orgel voice_table generator (C++ / CMake)
//
//   GeneralUser-GS.sf2  (or any GM .sf2)
//     -> render every required note via TinySoundFont (vendored tsf.h, no deps)
//     -> auto-detect sustained vs. decay from the RMS envelope
//     -> extract harmonic amplitudes at h*f0 via windowed Goertzel (exact freq)
//     -> RMS / equal-loudness normalize each voice
//        (fixes "trumpet too quiet" + "piano clips": every voice ends up the
//         same perceived loudness, so per-category runtime gains sit at ~1.0)
//     -> emit voices/voice_table.h, voices/voice_harm_data.h, voice_table.cpp
//
// Sample-density policy (data-size reduction):
//   * Piano family (GM prog 0-7) : per-NOTE (stride 1) over A0..C8  -> dense
//   * Decay-type instruments     : a few per octave (stride 6)      -> medium
//   * Sustained instruments      : ~1 per octave   (stride 12)      -> sparse
//   Percussion is NOT generated here (handled by the separate ADPCM engine);
//   a single silent placeholder voice is emitted so firmware keeps compiling.
//
// Pitch is taken from the MIDI note at runtime, so a per-octave sample is
// pitch-shifted with zero artifacts; only the harmonic ratios are reused.
//
// NOTE: TinySoundFont does not implement SF2 modulators. GeneralUser GS uses
// them; timbres render close but not identically to a fully compliant synth.
// This matches the prior Python build exactly. For maximum fidelity a future
// "render via FluidSynth WAVs" input mode can be added.
// ============================================================================

#include <cstdio>
#include <cstdint>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>
#include <algorithm>
#include <filesystem>
#include <array>
#include <cstdlib>

#define TSF_IMPLEMENTATION
#include "third_party/tsf.h"

namespace fs = std::filesystem;

// ---- analysis / format constants -------------------------------------------
static const double PI            = 3.14159265358979323846;
static const int    SR            = 22050;     // == firmware CFG_SAMPLE_RATE
static const double NYQUIST       = SR / 2.0;
static const int    HARM_MAX      = 64;        // == firmware VOICE_HARM_MAX
static const int    ANALYSIS_WIN  = 8192;      // harmonic-analysis window
static const double ATTACK_SKIP_S = 0.06;      // skip initial transient
static const double HOLD_S        = 1.6;
static const double REL_S         = 0.5;
static const double SIGNAL_RMS_TARGET = 4200.0;// equal-loudness target (int16)
static const int    HARM_CLIP     = 20000;     // clamp one harmonic bin

static const int PIANO_LO = 21, PIANO_HI = 108;   // A0..C8 per-note
static const int NOTE_LO  = 24, NOTE_HI  = 96;    // C1..C7 for other programs

static const char* CAT_NAMES[17] = {
    "Piano","Chroma","Organ","Guitar","Bass","Strings","Ensmbl","Brass",
    "Reed","Pipe","SynLd","SynPd","SynFX","Ethnic","Perc","SFX","Drum"};

// ---- IMA-ADPCM 4-bit tables (standard, used by encoder and firmware decoder) ----
static const int8_t ima_index_table[16] = {
    -1,-1,-1,-1, 2, 4, 6, 8, -1,-1,-1,-1, 2, 4, 6, 8
};
static const int16_t ima_step_table[89] = {
        7,    8,    9,   10,   11,   12,   13,   14,   16,   17,
       19,   21,   23,   25,   28,   31,   34,   37,   41,   45,
       50,   55,   60,   66,   73,   80,   88,   97,  107,  118,
      130,  143,  157,  173,  190,  209,  230,  253,  279,  307,
      337,  371,  408,  449,  494,  544,  598,  658,  724,  796,
      876,  963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066,
     2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358,
     5894, 6484, 7132, 7845, 8630, 9493,10442,11487,12635,13899,
    15289,16818,18500,20350,22385,24623,27086,29794,32767
};

static const int    DRUM_NOTE_LO   = 27;   // GM drum kit lowest note
static const int    DRUM_NOTE_HI   = 87;   // GM drum kit highest note
static const double DRUM_RENDER_S  = 1.0;  // render up to 1.0s of drum tail
static const double DRUM_RELEASE_S = 0.2;  // a little post-noteoff tail

static int gm_category(int prog) { return std::min(prog / 8, 15); }
static double midi_to_hz(int note) { return 440.0 * std::pow(2.0, (note - 69) / 12.0); }

// ---- drum stream model (used by emit + main loop) -------------------------
struct DrumEntry {
    uint32_t offset = 0;
    uint32_t length = 0;          // decoded sample count
    int16_t  init_predictor = 0;
    uint8_t  init_step_index = 0;
    uint8_t  used = 0;
};

struct AdpcmBlock {
    std::vector<uint8_t> bytes;   // packed nibbles (low first, then high)
    uint32_t num_samples = 0;
    int16_t  init_predictor = 0;
    uint8_t  init_step_index = 0;
};

// ---- IMA-ADPCM 4-bit encoder ----------------------------------------------
static AdpcmBlock ima_adpcm_encode(const std::vector<int16_t>& pcm) {
    AdpcmBlock r;
    r.num_samples = (uint32_t)pcm.size();
    r.bytes.assign((pcm.size() + 1) / 2, 0);
    int predictor = 0;
    int step_index = 0;
    r.init_predictor  = 0;
    r.init_step_index = 0;
    for (size_t i = 0; i < pcm.size(); ++i) {
        int diff = (int)pcm[i] - predictor;
        int nibble = 0;
        if (diff < 0) { nibble = 8; diff = -diff; }
        int step  = ima_step_table[step_index];
        int delta = step >> 3;
        if (diff >= step) { nibble |= 4; diff -= step; delta += step; }
        step >>= 1;
        if (diff >= step) { nibble |= 2; diff -= step; delta += step; }
        step >>= 1;
        if (diff >= step) { nibble |= 1; delta += step; }
        predictor += (nibble & 8) ? -delta : delta;
        if (predictor >  32767) predictor =  32767;
        if (predictor < -32768) predictor = -32768;
        step_index += ima_index_table[nibble];
        if (step_index <  0) step_index = 0;
        if (step_index > 88) step_index = 88;
        if (i & 1) r.bytes[i/2] |= (uint8_t)((nibble & 0x0F) << 4);
        else       r.bytes[i/2]  = (uint8_t)( nibble & 0x0F);
    }
    return r;
}

// trim leading/trailing near-silence, peak-normalize, return int16 PCM
static std::vector<int16_t> trim_norm_pcm(const std::vector<float>& x, int sr) {
    double peak = 0;
    for (float v : x) peak = std::max(peak, (double)std::fabs(v));
    if (peak < 1e-4) return {};
    double thresh = peak * 0.01;
    size_t start = 0;
    while (start < x.size() && std::fabs(x[start]) <= thresh) ++start;
    int win = std::max(1, sr / 40);   // 25ms
    size_t end = x.size();
    while (end > start + (size_t)win) {
        double s = 0;
        for (size_t k = end - win; k < end; ++k) s += std::fabs(x[k]);
        if (s / win > thresh) break;
        end -= (size_t)win;
    }
    if (end <= start) return {};
    double k = 0.85 / peak * 32767.0;
    std::vector<int16_t> out;
    out.reserve(end - start);
    for (size_t i = start; i < end; ++i) {
        long v = std::lround(x[i] * k);
        if (v >  32767) v =  32767;
        if (v < -32768) v = -32768;
        out.push_back((int16_t)v);
    }
    return out;
}

// ---- drum-mode renderer (separate from melodic Renderer; runs at drum_sr) --
struct DrumRenderer {
    tsf* g = nullptr;
    int  sr;
    DrumRenderer(const std::string& sf2_path, int sample_rate) : sr(sample_rate) {
        g = tsf_load_filename(sf2_path.c_str());
        if (!g) { fprintf(stderr,"ERROR: cannot load SF2 (drum): %s\n", sf2_path.c_str()); exit(1); }
        tsf_set_output(g, TSF_MONO, sr, 0.0f);
    }
    ~DrumRenderer() { if (g) tsf_close(g); }
    std::vector<float> render(int note, int vel = 110) {
        tsf_reset(g);
        // bank 128 preset 0 (Standard Drum Kit). The 4th arg = midiDrums flag.
        tsf_channel_set_presetnumber(g, 0, 0, 1);
        int nh = (int)(sr * DRUM_RENDER_S);
        int nr = (int)(sr * DRUM_RELEASE_S);
        std::vector<float> out(nh + nr, 0.0f);
        tsf_channel_note_on(g, 0, note, vel / 127.0f);
        tsf_render_float(g, out.data(), nh, 0);
        tsf_channel_note_off(g, 0, note);
        tsf_render_float(g, out.data() + nh, nr, 0);
        return out;
    }
};

// Release time constant (ms) used after note-off for SUSTAINED voices.
// (Decay voices use the firmware's DAMPER_TAU_MS instead, so this is irrelevant.)
// Values picked for musical naturalness: brass/organ cut fast, pads/strings tail longer.
static double release_tau_for_category(int cat) {
    switch (cat) {
        case 5:  case 6:  return 300.0;  // Strings, Ensemble
        case 8:  case 9:  return 180.0;  // Reed, Pipe (woodwinds)
        case 11:          return 600.0;  // Synth Pad
        case 12:          return 400.0;  // Synth FX
        default:          return 200.0;  // Organ, Brass, Lead, Ethnic, others
    }
}

// ---------------------------------------------------------------------------
struct VoiceMetaRow {
    std::string name; double base_hz; double tau; int nh; int sustained; int cat; uint32_t off;
};

struct Builder {
    std::vector<int16_t>     harm;
    std::vector<VoiceMetaRow> meta;
    int add(const std::string& name, int note, int nh, bool sustained, int cat,
            const std::vector<int16_t>& block, double tau) {
        uint32_t off = (uint32_t)harm.size();
        harm.insert(harm.end(), block.begin(), block.end());   // length nh+1
        meta.push_back({name, midi_to_hz(note), tau, nh, sustained ? 1 : 0, cat, off});
        return (int)meta.size() - 1;
    }
};

// ---- renderer (TinySoundFont) ----------------------------------------------
struct Renderer {
    tsf* g = nullptr;
    explicit Renderer(const std::string& path) {
        g = tsf_load_filename(path.c_str());
        if (!g) { fprintf(stderr, "ERROR: cannot load SF2: %s\n", path.c_str()); exit(1); }
        tsf_set_output(g, TSF_MONO, SR, 0.0f);   // 0 dB; we normalize ourselves
    }
    ~Renderer() { if (g) tsf_close(g); }

    bool has_preset(int prog) const { return tsf_get_presetindex(g, 0, prog) >= 0; }

    std::vector<float> render(int prog, int note, int vel = 110) {
        tsf_reset(g);
        tsf_channel_set_presetnumber(g, 0, prog, 0);
        int nh = (int)(SR * HOLD_S), nr = (int)(SR * REL_S);
        std::vector<float> out(nh + nr, 0.0f);
        tsf_channel_note_on(g, 0, note, vel / 127.0f);
        tsf_render_float(g, out.data(), nh, 0);
        tsf_channel_note_off(g, 0, note);
        tsf_render_float(g, out.data() + nh, nr, 0);
        return out;
    }
};

// ---- envelope / detection --------------------------------------------------
static std::vector<double> env_rms(const std::vector<float>& x, int win = 512) {
    int n = (int)x.size() / win;
    std::vector<double> e(std::max(n, 1), 0.0);
    for (int i = 0; i < n; ++i) {
        double s = 0; for (int k = 0; k < win; ++k) { double v = x[i*win+k]; s += v*v; }
        e[i] = std::sqrt(s / win + 1e-12);
    }
    return e;
}

static double estimate_tau_ms(const std::vector<double>& env, int win = 512) {
    if (env.size() < 4) return 1000.0;
    int pk = (int)(std::max_element(env.begin(), env.end()) - env.begin());
    int m = (int)env.size() - pk;
    if (m < 4 || env[pk] <= 0) return 1000.0;
    // least-squares slope of log(env) vs time(s)
    double sx=0, sy=0, sxx=0, sxy=0; int N=0;
    double floor_v = env[pk] * 1e-3;
    for (int i = 0; i < m; ++i) {
        double t = i * (double)win / SR;
        double y = std::log(std::max(env[pk+i], floor_v));
        sx += t; sy += y; sxx += t*t; sxy += t*y; ++N;
    }
    double denom = N*sxx - sx*sx;
    if (std::fabs(denom) < 1e-12) return 5000.0;
    double slope = (N*sxy - sx*sy) / denom;
    if (slope >= -1e-4) return 5000.0;
    double tau = -1.0/slope * 1000.0;
    return std::min(std::max(tau, 50.0), 5000.0);
}

// ---- harmonic extraction (windowed Goertzel at exact h*f0) -----------------
struct Analysis { std::vector<double> amps; int nh; bool sustained; double tau; double sig_rms; };

static double goertzel_mag(const std::vector<double>& wx, double freq) {
    double w0 = 2.0 * PI * freq / SR;
    double coeff = 2.0 * std::cos(w0);
    double s_prev = 0, s_prev2 = 0;
    for (double v : wx) { double s = v + coeff*s_prev - s_prev2; s_prev2 = s_prev; s_prev = s; }
    double power = s_prev2*s_prev2 + s_prev*s_prev - coeff*s_prev*s_prev2;
    return std::sqrt(std::max(power, 0.0));
}

static Analysis analyze(const std::vector<float>& x, double f0, const std::vector<double>& hann) {
    Analysis a;
    auto e = env_rms(x);
    double peak = *std::max_element(e.begin(), e.end()) + 1e-12;
    double sus_ratio = e[(int)(e.size()*0.75)] / peak;
    a.sustained = sus_ratio > 0.40;
    // For sustained voices, decay_tau_ms is the firmware's RELEASE time constant
    // (palette_synth.cpp st=3: env *= n.decay). 5000ms = ~5sec tail, pile-up city.
    // For decay voices, decay_tau_ms is the natural hold-decay (estimated below).
    // Sustained release defaults are category-tuned in the main loop after this call.
    a.tau = a.sustained ? 200.0 : estimate_tau_ms(e);

    int start = (int)(SR * ATTACK_SKIP_S);
    std::vector<double> wx(ANALYSIS_WIN, 0.0);
    for (int n = 0; n < ANALYSIS_WIN; ++n) {
        int idx = start + n;
        double v = (idx < (int)x.size()) ? x[idx] : 0.0;
        wx[n] = v * hann[n];
    }
    int nh = std::min(HARM_MAX, (int)(NYQUIST / f0));
    nh = std::max(nh, 1);
    a.amps.assign(nh + 1, 0.0);
    for (int h = 1; h <= nh; ++h) {
        double fb = h * f0;
        if (fb >= NYQUIST) { nh = h - 1; a.amps.resize(nh + 1); break; }
        a.amps[h] = goertzel_mag(wx, fb);
    }
    a.nh = nh;
    double ss = 0; for (int h = 1; h <= nh; ++h) ss += a.amps[h]*a.amps[h];
    a.sig_rms = std::sqrt(0.5 * ss) + 1e-9;
    return a;
}

static std::vector<int16_t> normalize_int16(const std::vector<double>& amps, double sig_rms) {
    double k = SIGNAL_RMS_TARGET / sig_rms;
    std::vector<int16_t> out(amps.size());
    for (size_t i = 0; i < amps.size(); ++i) {
        long v = std::lround(amps[i] * k);
        v = std::max((long)-HARM_CLIP, std::min((long)HARM_CLIP, v));
        out[i] = (int16_t)v;
    }
    out[0] = 0;
    return out;
}

// ---- code emission ---------------------------------------------------------
static void emit(const std::string& out, const Builder& VB,
                 const std::vector<int>& piano_note_to_voice,
                 const std::vector<int>& gm_program_to_voice,
                 const std::vector<int>& gm_drum_to_voice,
                 const std::vector<std::array<int,5>>& multi,
                 int drum_rate,
                 const std::vector<uint8_t>& drum_bytes,
                 const std::vector<DrumEntry>& drum_map);

// ---------------------------------------------------------------------------
int main(int argc, char** argv) {
    std::string sf2, out = "generated";
    std::vector<int> prog_filter;
    int decay_stride = 6, sustain_stride = 12;
    int drum_rate = 8000;             // ADPCM source rate (8000 default; try 12000/16000 later)
    bool skip_drums = false;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if      (a == "--out" && i+1 < argc) out = argv[++i];
        else if (a == "--decay-stride" && i+1 < argc) decay_stride = atoi(argv[++i]);
        else if (a == "--sustain-stride" && i+1 < argc) sustain_stride = atoi(argv[++i]);
        else if (a == "--drum-rate" && i+1 < argc) drum_rate = atoi(argv[++i]);
        else if (a == "--no-drums") skip_drums = true;
        else if (a == "--programs" && i+1 < argc) {
            std::string s = argv[++i], tok;
            for (char c : s) { if (c==',') { if(!tok.empty()){prog_filter.push_back(atoi(tok.c_str()));tok.clear();} } else tok+=c; }
            if (!tok.empty()) prog_filter.push_back(atoi(tok.c_str()));
        }
        else if (a[0] != '-') sf2 = a;
    }
    if (sf2.empty()) { fprintf(stderr, "usage: gm_extract <file.sf2> [--out DIR] [--programs 0,16,..] [--decay-stride N] [--sustain-stride N]\n"); return 1; }

    auto in_filter = [&](int p){ return prog_filter.empty() || std::find(prog_filter.begin(),prog_filter.end(),p)!=prog_filter.end(); };

    fs::create_directories(fs::path(out) / "voices");
    Renderer R(sf2);
    Builder VB;

    std::vector<double> hann(ANALYSIS_WIN);
    for (int n = 0; n < ANALYSIS_WIN; ++n) hann[n] = 0.5 - 0.5*std::cos(2.0*PI*n/ANALYSIS_WIN);

    // 0) silent placeholder (drums point here until ADPCM engine lands)
    VB.add("silent", 60, 1, false, 16, std::vector<int16_t>{0,0}, 50.0);

    // 1) piano family : per-note set shared by GM prog 0-7
    std::vector<int> piano_note_to_voice;
    printf("[piano] rendering A0..C8 per-note ...\n");
    for (int note = PIANO_LO; note <= PIANO_HI; ++note) {
        auto x = R.render(0, note);
        auto an = analyze(x, midi_to_hz(note), hann);
        auto hi = normalize_int16(an.amps, an.sig_rms);
        char nm[24]; snprintf(nm, sizeof(nm), "piano_%03d", note);
        int vi = VB.add(nm, note, an.nh, an.sustained, 0, hi, an.tau);
        piano_note_to_voice.push_back(vi);
    }
    printf("[piano] %d voices\n", (int)piano_note_to_voice.size());

    // 2) every other GM program (8..127)
    std::vector<int> gm_program_to_voice(128, piano_note_to_voice[60 - PIANO_LO]);
    std::vector<std::array<int,5>> multi;   // {prog, lo, hi, stride, base_voice}
    for (int prog = 8; prog < 128; ++prog) {
        if (!in_filter(prog)) continue;
        if (!R.has_preset(prog)) { gm_program_to_voice[prog] = 0; continue; }
        int cat = gm_category(prog);
        auto xp = R.render(prog, 60);
        auto ap = analyze(xp, midi_to_hz(60), hann);
        int stride = ap.sustained ? sustain_stride : decay_stride;
        int base = (int)VB.meta.size();
        int count = 0;
        for (int n = NOTE_LO; n <= NOTE_HI; n += stride) {
            auto x = R.render(prog, n);
            auto an = analyze(x, midi_to_hz(n), hann);
            if (an.sustained) an.tau = release_tau_for_category(cat);
            auto hi = normalize_int16(an.amps, an.sig_rms);
            char nm[24]; snprintf(nm, sizeof(nm), "p%03d_%03d", prog, n);
            VB.add(nm, n, an.nh, an.sustained, cat, hi, an.tau);
            ++count;
        }
        multi.push_back({prog, NOTE_LO, NOTE_HI, stride, base});
        gm_program_to_voice[prog] = base;
        printf("[prog %3d] %-7s sustained=%d stride=%2d -> %d samples (base %d)\n",
               prog, CAT_NAMES[cat], (int)ap.sustained, stride, count, base);
    }

    // 3) drums -> silent placeholder for the iFFT voice path (real audio via ADPCM)
    std::vector<int> gm_drum_to_voice(128, 0);

    // 4) Drums: render at drum_rate, IMA-ADPCM 4-bit encode
    std::vector<uint8_t> drum_bytes;
    std::vector<DrumEntry> drum_map(128);
    int drum_count = 0;
    if (!skip_drums) {
        printf("\n[drums] rendering bank128 preset0 (Standard Kit) at %d Hz, IMA-ADPCM 4-bit ...\n", drum_rate);
        DrumRenderer DR(sf2, drum_rate);
        for (int note = DRUM_NOTE_LO; note <= DRUM_NOTE_HI; ++note) {
            auto wav = DR.render(note);
            auto pcm = trim_norm_pcm(wav, drum_rate);
            if (pcm.empty()) continue;
            auto enc = ima_adpcm_encode(pcm);
            drum_map[note].offset          = (uint32_t)drum_bytes.size();
            drum_map[note].length          = enc.num_samples;
            drum_map[note].init_predictor  = enc.init_predictor;
            drum_map[note].init_step_index = enc.init_step_index;
            drum_map[note].used            = 1;
            drum_bytes.insert(drum_bytes.end(), enc.bytes.begin(), enc.bytes.end());
            ++drum_count;
            printf("  note %3d: %5u smp (%4.0fms) -> %4zu B\n",
                   note, enc.num_samples,
                   (double)enc.num_samples*1000.0/drum_rate, enc.bytes.size());
        }
        printf("[drums] %d notes, %.1f KB ADPCM total\n",
               drum_count, drum_bytes.size()/1024.0);
    }

    emit(out, VB, piano_note_to_voice, gm_program_to_voice, gm_drum_to_voice, multi,
         drum_rate, drum_bytes, drum_map);
    size_t total = VB.harm.size();
    printf("\nDONE: %d voices, %zu int16 (%.1f KB harmonic)",
           (int)VB.meta.size(), total, total*2/1024.0);
    if (!skip_drums)
        printf(" + %d drums (%.1f KB ADPCM @ %d Hz)",
               drum_count, drum_bytes.size()/1024.0, drum_rate);
    printf("\n");
    return 0;
}

// ---------------------------------------------------------------------------
static void emit(const std::string& out, const Builder& VB,
                 const std::vector<int>& piano_note_to_voice,
                 const std::vector<int>& gm_program_to_voice,
                 const std::vector<int>& gm_drum_to_voice,
                 const std::vector<std::array<int,5>>& multi,
                 int drum_rate,
                 const std::vector<uint8_t>& drum_bytes,
                 const std::vector<DrumEntry>& drum_map)
{
    // voices/voice_harm_data.h
    {
        FILE* f = fopen((fs::path(out)/"voices"/"voice_harm_data.h").string().c_str(), "w");
        fprintf(f,
            "// auto-generated: consolidated harmonic data (extern decl only)\n"
            "#pragma once\n#include <Arduino.h>\n\n"
            "extern const int16_t VOICE_HARM_DATA[];\n"
            "extern const uint32_t VOICE_HARM_TOTAL;\n");
        fclose(f);
    }
    // voices/voice_table.h  (MultiSample gains a 'stride' field)
    {
        FILE* f = fopen((fs::path(out)/"voices"/"voice_table.h").string().c_str(), "w");
        fprintf(f, "%s",
"// auto-generated voice_table.h\n"
"#pragma once\n"
"#include <Arduino.h>\n"
"#include \"voice_harm_data.h\"\n\n"
"#define VOICE_HARM_MAX 64\n\n"
"struct VoiceMeta {\n"
"    const char* name;\n"
"    float       base_hz;\n"
"    float       decay_tau_ms;\n"
"    uint8_t     nh;\n"
"    uint8_t     sustained;   // 1=hold(organ) 0=decay(piano)\n"
"    uint8_t     category;    // GM category 0..16\n"
"    float       peak;\n"
"    uint32_t    harm_off;\n"
"};\n\n"
"// stride = semitones between consecutive samples (1=per-note, 12=per-octave)\n"
"struct MultiSample { uint8_t program; uint8_t note_lo; uint8_t note_hi; uint8_t stride; uint16_t base_voice; };\n\n"
"extern const VoiceMeta voice_table[];\n"
"extern const int        VOICE_COUNT;\n"
"extern const uint16_t   piano_note_to_voice[88];\n"
"extern const uint16_t   gm_program_to_voice[128];\n"
"extern const uint16_t   gm_drum_to_voice[128];\n"
"extern const MultiSample multi_samples[];\n"
"extern const int        MULTI_SAMPLE_COUNT;\n\n"
"static inline const int16_t* voice_harm_ptr(uint16_t vi) {\n"
"    return &VOICE_HARM_DATA[voice_table[vi].harm_off];\n"
"}\n"
"static inline uint16_t voice_idx_from_drum(uint8_t note) { return gm_drum_to_voice[note & 0x7F]; }\n"
"static inline uint16_t voice_idx_from_note(uint8_t midi_note, uint8_t prog) {\n"
"    if (prog < 8 && midi_note >= 21 && midi_note <= 108)\n"
"        return piano_note_to_voice[midi_note - 21];\n"
"    for (int i=0;i<MULTI_SAMPLE_COUNT;i++) {\n"
"        const MultiSample& m = multi_samples[i];\n"
"        if (m.program==prog) {\n"
"            uint8_t n = midi_note;\n"
"            if (n < m.note_lo) n = m.note_lo;\n"
"            if (n > m.note_hi) n = m.note_hi;\n"
"            uint16_t idx = (uint16_t)(n - m.note_lo) / (m.stride ? m.stride : 1);\n"
"            return (uint16_t)(m.base_voice + idx);\n"
"        }\n"
"    }\n"
"    return gm_program_to_voice[prog];\n"
"}\n");
        fclose(f);
    }
    // voice_table.cpp
    {
        FILE* f = fopen((fs::path(out)/"voice_table.cpp").string().c_str(), "w");
        fprintf(f, "// auto-generated voice_table.cpp\n#include \"voices/voice_table.h\"\n\n");
        fprintf(f, "const int16_t VOICE_HARM_DATA[] = {\n");
        for (const auto& m : VB.meta) {
            fprintf(f, "  ");
            for (int k = 0; k <= m.nh; ++k) fprintf(f, "%d,", VB.harm[m.off + k]);
            fprintf(f, " // [%u] %s nh=%d\n", m.off, m.name.c_str(), m.nh);
        }
        fprintf(f, "};\n");
        fprintf(f, "const uint32_t VOICE_HARM_TOTAL = %zu;\n\n", VB.harm.size());

        fprintf(f, "const VoiceMeta voice_table[] = {\n");
        for (const auto& m : VB.meta)
            fprintf(f, "    {\"%s\", %.2ff, %.2ff, %d, %d, %d, 0.950f, %uu},\n",
                    m.name.c_str(), m.base_hz, m.tau, m.nh, m.sustained, m.cat, m.off);
        fprintf(f, "};\n");
        fprintf(f, "const int VOICE_COUNT = sizeof(voice_table)/sizeof(voice_table[0]);\n\n");

        fprintf(f, "const uint16_t piano_note_to_voice[88] = {\n  ");
        for (size_t i=0;i<piano_note_to_voice.size();++i) fprintf(f, "%d,", piano_note_to_voice[i]);
        fprintf(f, "\n};\n\n");

        fprintf(f, "const uint16_t gm_program_to_voice[128] = {\n  ");
        for (int v : gm_program_to_voice) fprintf(f, "%d,", v);
        fprintf(f, "\n};\n\n");

        fprintf(f, "const uint16_t gm_drum_to_voice[128] = {\n  ");
        for (int v : gm_drum_to_voice) fprintf(f, "%d,", v);
        fprintf(f, "\n};\n\n");

        fprintf(f, "const MultiSample multi_samples[] = {\n");
        for (const auto& m : multi)
            fprintf(f, "    {%d, %d, %d, %d, %d},\n", m[0], m[1], m[2], m[3], m[4]);
        fprintf(f, "};\n");
        fprintf(f, "const int MULTI_SAMPLE_COUNT = %d;\n", (int)multi.size());
        fclose(f);
    }

    // -------- drum_data.h (header) ------------------------------------------
    {
        FILE* f = fopen((fs::path(out)/"voices"/"drum_data.h").string().c_str(), "w");
        fprintf(f, "%s",
"// auto-generated drum_data.h - IMA-ADPCM 4-bit mono drum samples\n"
"#pragma once\n#include <Arduino.h>\n\n");
        fprintf(f, "#define DRUM_SAMPLE_RATE %d\n\n", drum_rate);
        fprintf(f, "%s",
"struct DrumSample {\n"
"    uint32_t offset;     // byte offset into DRUM_ADPCM_DATA\n"
"    uint32_t length;     // number of decoded samples (nibbles)\n"
"    int16_t  init_predictor;\n"
"    uint8_t  init_step_index;\n"
"    uint8_t  used;       // 0 = no sample mapped for this MIDI note\n"
"};\n\n"
"extern const uint8_t      DRUM_ADPCM_DATA[];\n"
"extern const uint32_t     DRUM_ADPCM_TOTAL_BYTES;\n"
"extern const DrumSample   drum_map[128];\n");
        fclose(f);
    }
    // -------- drum_data.cpp (data) ------------------------------------------
    {
        FILE* f = fopen((fs::path(out)/"drum_data.cpp").string().c_str(), "w");
        fprintf(f, "// auto-generated drum_data.cpp\n#include \"voices/drum_data.h\"\n\n");
        fprintf(f, "const uint8_t DRUM_ADPCM_DATA[%zu] = {", drum_bytes.empty() ? 1 : drum_bytes.size());
        if (drum_bytes.empty()) {
            fprintf(f, "0x00");   // valid 1-byte placeholder when --no-drums
        } else {
            for (size_t i = 0; i < drum_bytes.size(); ++i) {
                if ((i & 15) == 0) fprintf(f, "\n  ");
                fprintf(f, "0x%02X,", drum_bytes[i]);
            }
        }
        fprintf(f, "\n};\n");
        fprintf(f, "const uint32_t DRUM_ADPCM_TOTAL_BYTES = %zu;\n\n", drum_bytes.size());
        fprintf(f, "const DrumSample drum_map[128] = {\n");
        for (int i = 0; i < 128; ++i) {
            const auto& m = drum_map[i];
            fprintf(f, "  {%uu, %uu, %d, %u, %u},%s\n",
                    m.offset, m.length, m.init_predictor, m.init_step_index, m.used,
                    m.used ? "" : "   // (unused)");
        }
        fprintf(f, "};\n");
        fclose(f);
    }
}
