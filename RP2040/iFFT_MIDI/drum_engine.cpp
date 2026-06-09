// ============================================================================
// drum_engine.cpp - IMA-ADPCM 4-bit decoder + linear-interp resampler + mixer.
//
// [BUGFIX v7.1]
// - DRUM_LATENCY_OFFSET: float除算 → 整数定数化
//   旧: (CFG_HOP_SIZE / 0.8) = float演算 → 1280サンプル(58ms遅延)
//   新: (CFG_HOP_SIZE / 2) = 512サンプル(23ms) ← Hann窓中央と正確に揃う
// ============================================================================
#include "drum_engine.h"
#include "voices/drum_data.h"
#include "config.h"
#include <string.h>

static const int32_t DRUM_MIX_GAIN_Q15 = (int32_t)(CFG_GAIN_DRUM * 32768.0f);

// [BUGFIX] float除算を整数定数に変更
// Hann窓の実効中心はFFT_SIZE/2 = 1024サンプル先
// drum_engine_mix はhopサイズ分を1フレームで出力するため、
// ホップ半分のオフセットが最も自然な遅延補正値


static const int8_t  IMA_INDEX[16] = {
    -1,-1,-1,-1, 2, 4, 6, 8, -1,-1,-1,-1, 2, 4, 6, 8
};
static const int16_t IMA_STEP[89] = {
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

struct DrumVoice {
    uint8_t  used;
    uint8_t  midi_note;
    uint32_t age;
    uint32_t base_off;
    uint32_t length;
    uint32_t pos_src;
    int16_t  predictor;
    uint8_t  step_index;
    int16_t  cur, nxt;
    uint32_t phase;
    uint32_t phase_inc;
    int16_t  vel;
    int32_t  delay_remain;
};

static DrumVoice g_v[DRUM_VOICES];
static uint32_t  g_age_counter = 0;

static inline int16_t decode_nibble(uint8_t nib, int16_t& pred, uint8_t& si) {
    int step  = IMA_STEP[si];
    int delta = step >> 3;
    if (nib & 4) delta += step;
    if (nib & 2) delta += step >> 1;
    if (nib & 1) delta += step >> 2;
    int p = pred;
    if (nib & 8) p -= delta; else p += delta;
    if (p >  32767) p =  32767;
    if (p < -32768) p = -32768;
    pred = (int16_t)p;
    int s = (int)si + IMA_INDEX[nib];
    if (s < 0)  s = 0;
    if (s > 88) s = 88;
    si = (uint8_t)s;
    return pred;
}

static inline uint8_t fetch_nibble(uint32_t off, uint32_t i) {
    uint8_t b = DRUM_ADPCM_DATA[off + (i >> 1)];
    return (i & 1) ? ((b >> 4) & 0x0F) : (b & 0x0F);
}

void drum_engine_init() {
    memset(g_v, 0, sizeof(g_v));
    g_age_counter = 0;
}

int drum_engine_active_count() {
    int n = 0;
    for (int i = 0; i < DRUM_VOICES; i++) if (g_v[i].used) n++;
    return n;
}

void drum_engine_note_on(uint8_t midi_note, uint8_t velocity) {
    if (midi_note >= 128) return;
    const DrumSample& d = drum_map[midi_note];
    if (!d.used || d.length == 0) return;

    int slot = -1;

    int      same_count = 0;
    int      oldest_same_slot = -1;
    uint32_t oldest_age = 0xFFFFFFFFu;
    for (int i = 0; i < DRUM_VOICES; i++) {
        if (g_v[i].used && g_v[i].midi_note == midi_note) {
            same_count++;
            if (g_v[i].age < oldest_age) {
                oldest_age = g_v[i].age;
                oldest_same_slot = i;
            }
        }
    }

    if (same_count >= DRUM_POLYPHONY_PER_NOTE) {
        slot = oldest_same_slot;
    }

    if (slot < 0) {
        for (int i = 0; i < DRUM_VOICES; i++) {
            if (!g_v[i].used) { slot = i; break; }
        }
    }

    if (slot < 0) {
        uint32_t best = 0; slot = 0;
        for (int i = 0; i < DRUM_VOICES; i++) {
            uint32_t r = (uint32_t)(((uint64_t)g_v[i].pos_src * 1024) /
                                    (g_v[i].length ? g_v[i].length : 1));
            if (r > best) { best = r; slot = i; }
        }
    }

    DrumVoice& v = g_v[slot];
    v.used         = 1;
    v.midi_note    = midi_note;
    v.age          = ++g_age_counter;
    v.base_off     = d.offset;
    v.length       = d.length;
    v.predictor    = d.init_predictor;
    v.step_index   = d.init_step_index;
    v.pos_src      = 0;
    v.phase        = 0;
    v.phase_inc    = (uint32_t)(((uint64_t)DRUM_SAMPLE_RATE << 16) / (uint32_t)CFG_SAMPLE_RATE);
    v.vel          = velocity;
    // [BUGFIX] 整数定数 DRUM_LATENCY_OFFSET を使用
    v.delay_remain = DRUM_LATENCY_OFFSET;

    v.cur = decode_nibble(fetch_nibble(v.base_off, 0), v.predictor, v.step_index);
    v.pos_src = 1;
    if (v.length >= 2) {
        v.nxt = decode_nibble(fetch_nibble(v.base_off, 1), v.predictor, v.step_index);
        v.pos_src = 2;
    } else {
        v.nxt = v.cur;
    }
}

void drum_engine_mix(int32_t* out, int samples) {
    if (DRUM_MIX_GAIN_Q15 == 0) return;

    for (int vi = 0; vi < DRUM_VOICES; vi++) {
        DrumVoice& v = g_v[vi];
        if (!v.used) continue;
        for (int j = 0; j < samples; j++) {
            if (v.delay_remain > 0) {
                v.delay_remain--;
                continue;
            }

            uint32_t frac = v.phase & 0xFFFFu;
            int32_t  s    = (int32_t)v.cur * (int32_t)(0x10000u - frac)
                          + (int32_t)v.nxt * (int32_t)frac;
            s >>= 16;
            s = (s * (int32_t)v.vel) >> 7;
            s = (int32_t)(((int64_t)s * (int64_t)DRUM_MIX_GAIN_Q15) >> 15);
            out[j] += s;
            v.phase += v.phase_inc;
            while (v.phase >= 0x10000u) {
                v.phase -= 0x10000u;
                v.cur = v.nxt;
                if (v.pos_src >= v.length) { v.used = 0; goto next_voice; }
                v.nxt = decode_nibble(fetch_nibble(v.base_off, v.pos_src),
                                      v.predictor, v.step_index);
                v.pos_src++;
            }
        }
        next_voice: ;
    }
}