#include "palette_synth.h"
#include "voices/voice_table.h"
#include "drum_engine.h"     // <-- 追加: ADPCMドラムをOLA前に加算するため
#include <math.h>
#include <string.h>

// ================================================================
// iFFT パレット合成エンジン (V3 - 全機能統合版)
// ・グローバル正規化済み倍音データを使用
// ・カテゴリ別ゲイン (config.h で調整可)
// ・パレットリミッター (シンバル等のピーク対策)
// ・打楽器リングアウト (note-off無視で自然減衰)
// ・線形ベロシティ (シンプルで予測可能)
// ・ADPCM打楽器をソフトクリップ前に time-domain mix
// ================================================================

static int16_t sin_tab[PS_SINTAB];
static int16_t hann[FFT_N];
static int32_t pal_re[2][PS_HALF];
static int32_t pal_im[2][PS_HALF];

volatile uint32_t ps_fill_seq = 0;
volatile uint32_t ps_cons_seq = 0;
volatile uint8_t  ps_active   = 0;

struct Note {
    uint8_t  used;
    uint16_t voice;
    uint8_t  midi;
    uint8_t  channel;   // MIDIチャンネル (別パート識別 → 同音切れ防止)
    uint8_t  vel_q;
    uint8_t  st;
    uint8_t  sustained;
    uint8_t  is_drum;
    float    env;
    float    env_step;
    float    decay;
    float    fbase;
    uint32_t phi;
    uint32_t dphi;
    uint32_t end_ms;
};
static Note notes[CFG_MAX_POLYPHONY];

static int32_t work_re[FFT_N];
static int32_t work_im[FFT_N];
static int32_t ola_tail[FFT_N/2];

#define HOP_MS         ((float)CFG_HOP_SIZE * 1000.0f / (float)CFG_SAMPLE_RATE)
#define DAMPER_TAU_MS  80.0f
#define FREQ_TO_BIN    ((float)FFT_N / (float)CFG_SAMPLE_RATE)

// カテゴリ別ゲイン (config.h CFG_GAIN_* から初期化, int32でQF15)
static int32_t cat_gain_q15[17];

static uint32_t s_prng = 0xC0FFEE42u;
static inline uint32_t prng_next() {
    s_prng = s_prng * 1664525u + 1013904223u; return s_prng;
}

// CFG_GAIN_* (float) を Q15 に変換 (クランプなし)
static inline int32_t gain_to_q15(float g) {
    return (int32_t)(g * 32767.0f);
}

static void init_cat_gains() {
    cat_gain_q15[0]  = gain_to_q15(CFG_GAIN_PIANO);
    cat_gain_q15[1]  = gain_to_q15(CFG_GAIN_CHROMATIC);
    cat_gain_q15[2]  = gain_to_q15(CFG_GAIN_ORGAN);
    cat_gain_q15[3]  = gain_to_q15(CFG_GAIN_GUITAR);
    cat_gain_q15[4]  = gain_to_q15(CFG_GAIN_BASS);
    cat_gain_q15[5]  = gain_to_q15(CFG_GAIN_STRINGS);
    cat_gain_q15[6]  = gain_to_q15(CFG_GAIN_ENSEMBLE);
    cat_gain_q15[7]  = gain_to_q15(CFG_GAIN_BRASS);
    cat_gain_q15[8]  = gain_to_q15(CFG_GAIN_REED);
    cat_gain_q15[9]  = gain_to_q15(CFG_GAIN_PIPE);
    cat_gain_q15[10] = gain_to_q15(CFG_GAIN_SYNTH_LEAD);
    cat_gain_q15[11] = gain_to_q15(CFG_GAIN_SYNTH_PAD);
    cat_gain_q15[12] = gain_to_q15(CFG_GAIN_SYNTH_FX);
    cat_gain_q15[13] = gain_to_q15(CFG_GAIN_ETHNIC);
    cat_gain_q15[14] = gain_to_q15(CFG_GAIN_PERC);
    cat_gain_q15[15] = gain_to_q15(CFG_GAIN_SFX);
    cat_gain_q15[16] = gain_to_q15(CFG_GAIN_DRUM);
}

void ps_init() {
    fft_q15_init();
    for (int i=0; i<PS_SINTAB; i++)
        sin_tab[i] = (int16_t)lround(sin(2.0*M_PI*i/PS_SINTAB)*32767.0);
    for (int n=0; n<FFT_N; n++)
        hann[n] = (int16_t)lround((0.5-0.5*cos(2.0*M_PI*n/FFT_N))*32767.0);

    init_cat_gains();
    memset(notes, 0, sizeof(notes));
    memset(ola_tail, 0, sizeof(ola_tail));
    memset(pal_re, 0, sizeof(pal_re));
    memset(pal_im, 0, sizeof(pal_im));

    DBG_PRINTF("[ps] iFFT engine, %d voices, master=%d\n",
        VOICE_COUNT, (int)CFG_MASTER_GAIN);
    // カテゴリゲインの実効値を表示 (設定が反映されているか確認用)
    static const char* cat_name[17] = {
        "Piano","Chroma","Organ","Guitar","Bass","Strings","Ensmbl","Brass",
        "Reed","Pipe","SynLd","SynPd","SynFX","Ethnic","Perc","SFX","Drum"};
    for (int c=0; c<17; c++)
        DBG_PRINTF("  gain[%s]=%d/32767 (%.2fx)\n",
            cat_name[c], cat_gain_q15[c], cat_gain_q15[c]/32767.0f);
    for (int v=0; v<VOICE_COUNT && v<6; v++)
        DBG_PRINTF("  [%d] %s base=%.1fHz nh=%d tau=%.0fms\n",
            v, voice_table[v].name, voice_table[v].base_hz,
            voice_table[v].nh, voice_table[v].decay_tau_ms);
    if (VOICE_COUNT > 6) DBG_PRINTF("  ... (%d more)\n", VOICE_COUNT - 6);
}

void ps_note_on(uint8_t midi, uint8_t vel, uint16_t voice_idx, uint8_t channel, uint32_t end_ms) {
    if (midi < CFG_MIDI_NOTE_A0 || midi > CFG_MIDI_NOTE_C8) return;
    if (voice_idx >= (uint16_t)VOICE_COUNT) voice_idx = 0;

    int slot = -1;
    bool retrigger = false;
    // 同音階・同音色・同チャンネルのときだけ再トリガー
    // (別音色 or 別パートは別スロット → 同音切れ防止)
    for (int i=0; i<CFG_MAX_POLYPHONY; i++)
        if (notes[i].used && notes[i].midi==midi &&
            notes[i].voice==voice_idx && notes[i].channel==channel)
            { slot=i; retrigger=true; break; }
    if (slot<0)
        for (int i=0; i<CFG_MAX_POLYPHONY; i++)
            if (!notes[i].used) { slot=i; break; }
    if (slot<0) {
        int mi=0; float me=notes[0].env;
        for (int i=1; i<CFG_MAX_POLYPHONY; i++)
            if (notes[i].env<me) { me=notes[i].env; mi=i; }
        slot=mi;
    }

    Note& n = notes[slot];
    n.used=1; n.voice=voice_idx; n.midi=midi; n.channel=channel; n.vel_q=vel; n.st=1;
    n.end_ms=end_ms;
    n.sustained = voice_table[voice_idx].sustained;
    n.is_drum   = (voice_table[voice_idx].category == 16) ? 1 : 0;

    n.fbase = 440.0f * powf(2.0f, (midi-69)/12.0f);
    double cph = (double)n.fbase * (double)CFG_HOP_SIZE / (double)CFG_SAMPLE_RATE;
    double frac = cph - floor(cph);
    n.dphi = (uint32_t)(frac * 4294967296.0);

    if (!retrigger) {
        n.phi = prng_next();
        n.env = 0.0f;
    }

    n.env_step = HOP_MS / 10.0f;
    float tau = voice_table[voice_idx].decay_tau_ms;
    if (tau < 50.0f)   tau = 50.0f;
    if (tau > 5000.0f) tau = 5000.0f;
    n.decay = expf(-HOP_MS / tau);
}

void ps_note_off(uint8_t midi, uint8_t channel) {
    for (int i=0; i<CFG_MAX_POLYPHONY; i++)
        if (notes[i].used && notes[i].midi==midi &&
            notes[i].channel==channel && notes[i].st==2)
            notes[i].st=3;
}

void ps_all_off() { memset(notes, 0, sizeof(notes)); ps_active=0; }

int ps_paint_palette(uint32_t now_ms) {
    uint32_t buf = ps_fill_seq & 1;
    int32_t* pre = pal_re[buf];
    int32_t* pim = pal_im[buf];
    memset(pre, 0, PS_HALF * sizeof(int32_t));
    memset(pim, 0, PS_HALF * sizeof(int32_t));

    uint8_t alive = 0;
    float damper_decay = expf(-HOP_MS / DAMPER_TAU_MS);

    for (int i=0; i<CFG_MAX_POLYPHONY; i++) {
        Note& n = notes[i];
        if (!n.used) continue;

        // 楽器物理に基づくエンベロープ
        switch (n.st) {
            case 1: // attack
                n.env += n.env_step;
                if (n.env >= 1.0f) { n.env = 1.0f; n.st = 2; }
                break;
            case 2: // 鍵を押している間
                if (!n.sustained) n.env *= n.decay;
                if (now_ms >= n.end_ms) {
                    if (!n.is_drum) n.st = 3;  // 打楽器はリングアウト
                }
                break;
            case 3: // 離鍵後
                if (n.sustained) n.env *= n.decay;
                else             n.env *= damper_decay;
                break;
        }
        if (n.env < 0.003f) { n.used=0; continue; }
        alive++;

        // 振幅 = env × vel × master × category_gain
        int32_t env_q = (int32_t)(n.env * 32767.0f);
        int32_t velm  = (int32_t)n.vel_q * 257;
        int32_t amp0 = (env_q * velm) >> 15;
        amp0 = (amp0 * CFG_MASTER_GAIN) >> 15;
        uint8_t cat = voice_table[n.voice].category;
        if (cat > 16) cat = 16;
        amp0 = (int32_t)(((int64_t)amp0 * (int64_t)cat_gain_q15[cat]) >> 15);

        // 倍音をパレットbinに配置 (位相追跡で正確なピッチ)
        const int16_t* harm = voice_harm_ptr(n.voice);
        uint8_t nh = voice_table[n.voice].nh;
        float fbase = n.fbase;

        n.phi += n.dphi;  // 位相をhop分進める

        for (int h=1; h<=nh; h++) {
            int16_t ha = harm[h];
            if (!ha) continue;
            float fh = fbase * h;
            int bin = (int)(fh * FREQ_TO_BIN + 0.5f);
            if (bin < 1 || bin >= PS_HALF) continue;

            int32_t val = (amp0 * (int32_t)ha) >> 15;

            uint32_t angle = n.phi * (uint32_t)h;
            int idx = (int)(angle >> (32 - PS_SINTAB_BITS)) & (PS_SINTAB - 1);
            int32_t cos_v = sin_tab[(idx + PS_SINTAB/4) & (PS_SINTAB - 1)];
            int32_t sin_v = sin_tab[idx];

            pre[bin] += (int32_t)(((int64_t)val * cos_v) >> 15);
            pim[bin] += (int32_t)(((int64_t)val * sin_v) >> 15);
        }
    }

    pim[0] = 0; pim[PS_HALF-1] = 0;
    ps_active = alive;
    __dmb();
    ps_fill_seq++;
    return alive;
}

void ps_render_block(uint32_t* out, uint32_t pwm_wrap, uint32_t pwm_mid) {
    uint32_t buf = ps_cons_seq & 1;
    int32_t* pre = pal_re[buf];
    int32_t* pim = pal_im[buf];

    for (int k=0; k<PS_HALF; k++) { work_re[k]=pre[k]; work_im[k]=pim[k]; }

    // ※パレットリミッター撤廃:
    //   以前は周波数ピークを18000に正規化していたが、これがカテゴリゲインを
    //   完全に打ち消していた (ゲインを上げてもピークが押し戻され音量不変)。
    //   ピーク保護は下流の時間領域ソフトクリッパーに一本化する。

    // 共役対称 (リミット済み work から)
    for (int k=1; k<FFT_N/2; k++) {
        work_re[FFT_N-k] =  work_re[k];
        work_im[FFT_N-k] = -work_im[k];
    }
    ifft_q15(work_re, work_im);

    // ★ADPCM打楽器をこのhop分展開 (iFFT結果と同じ時間軸に合算)
    //   - drum_engine_mix は加算ミックスなので memset で0クリア必須
    //   - static で BSS に配置 (CFG_HOP_SIZE * 4B、stack を圧迫しない)
    static int32_t drum_buf[CFG_HOP_SIZE];
    memset(drum_buf, 0, sizeof(drum_buf));
    drum_engine_mix(drum_buf, CFG_HOP_SIZE);

    // Hann窓 + 50% OLA + ドラム加算 → ソフトクリップ → PWM出力
    for (int n=0; n<(int)CFG_HOP_SIZE; n++) {
        int32_t wv = (int32_t)(((int64_t)work_re[n] * (int64_t)hann[n]) >> 15);
        int32_t v = wv + ola_tail[n] + drum_buf[n];   // ← ADPCMドラムを加算

        const int32_t SC = 24000;
        if      (v >  SC) v =  SC + ((v - SC) >> 3);
        else if (v < -SC) v = -SC + ((v + SC) >> 3);
        if (v >  32767) v =  32767;
        if (v < -32767) v = -32767;

        int32_t da = (int32_t)(((int64_t)v * (int64_t)pwm_mid) >> 15) + (int32_t)pwm_mid;
        if (da < 0) da = 0;
        if (da > (int32_t)pwm_wrap) da = (int32_t)pwm_wrap;
        uint32_t db = pwm_wrap - (uint32_t)da;
        out[n] = (db << 16) | (uint32_t)da;
    }

    // OLA tail を次フレーム用に保存
    for (int n=0; n<(int)CFG_HOP_SIZE; n++) {
        ola_tail[n] = (int32_t)(((int64_t)work_re[CFG_HOP_SIZE+n] *
                                  (int64_t)hann[CFG_HOP_SIZE+n]) >> 15);
    }

    ps_cons_seq++;
}
