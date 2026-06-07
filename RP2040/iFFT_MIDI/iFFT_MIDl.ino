// ============================================================
// iFFT_Orgel v3 — FFTパレット加算合成
//
// Core0: MIDI解析 + FFTパレット描画 + OLED + 選曲
// Core1: パレット→iFFT→Hann→OLA→PCM
// DMA  : PCM→差動PWM (GP2/GP3, CPU負荷ゼロ)
//
// 音色は倍音テーブルから周波数ドメインへ「描く」
// 位相追跡によりピッチ連続、1回のiFFTで全音同時合成
//
// ADPCM打楽器エンジン: ドラム (channel 10/is_drum) は別経路の
// IMA-ADPCM デコーダ → リサンプラ → time-domain mix で再生
// ============================================================

#include "config.h"
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <LittleFS.h>
#include <hardware/pwm.h>
#include <hardware/dma.h>
#include <hardware/clocks.h>
#include <hardware/irq.h>
#include <hardware/sync.h>

#include "voices/voice_table.h"
#include "midi_player.h"
#include "song_list.h"
#include "palette_synth.h"
#include "drum_engine.h"     // <-- 追加: ADPCM打楽器エンジン

// ================================================================
// グローバル
// ================================================================
Adafruit_SSD1306 display(CFG_PHYS_W, CFG_PHYS_H, &Wire, -1);
MidiPlayer       g_player;
SongList         g_songlist;

static uint32_t pwm_wrap, pwm_mid;
static uint     pwm_slice_num, dma_chan;
static uint32_t dma_buf[2][CFG_HOP_SIZE] __attribute__((aligned(4)));
volatile uint8_t  buf_dma   = 0;
volatile uint8_t  buf_fill  = 1;
volatile bool     need_fill = true;
volatile uint32_t dma_count = 0;

static int       g_song_idx     = 0;
static bool      g_in_menu      = false;
static bool      g_showing_info = false;
static uint32_t  g_info_start   = 0;
static uint32_t  g_last_btn_ms  = 0;
static uint32_t  g_last_oled_ms = 0;

// ノーツ表示 (MIDIイベントを先読みして降下表示)
#define VIS_WINDOW_MS 1500u
static int      g_vis_cursor = 0;
static uint32_t g_paint_count = 0;  // パレット描画回数 (診断用)   // events配列への先頭カーソル

// ================================================================
// DMA 割り込み
// ================================================================
static void __isr dma_irq_handler() {
    if (!dma_channel_get_irq0_status(dma_chan)) return;
    dma_channel_acknowledge_irq0(dma_chan);
    uint8_t next = buf_fill; buf_dma = next; buf_fill = 1 - next;
    need_fill = true;
    dma_channel_set_read_addr(dma_chan, dma_buf[buf_dma], false);
    dma_channel_set_trans_count(dma_chan, CFG_HOP_SIZE, true);
    dma_count++;
}

static void setup_pwm_dma() {
    uint32_t sys_hz = clock_get_hz(clk_sys);
    pwm_wrap = sys_hz / CFG_SAMPLE_RATE - 1u;
    pwm_mid  = pwm_wrap / 2u;
    DBG_PRINTF("[pwm] sys=%lu wrap=%lu\n",
               (unsigned long)sys_hz, (unsigned long)pwm_wrap);

    gpio_set_function(CFG_AUDIO_PIN_P, GPIO_FUNC_PWM);
    gpio_set_function(CFG_AUDIO_PIN_N, GPIO_FUNC_PWM);
    gpio_set_drive_strength(CFG_AUDIO_PIN_P, GPIO_DRIVE_STRENGTH_12MA);
    gpio_set_drive_strength(CFG_AUDIO_PIN_N, GPIO_DRIVE_STRENGTH_12MA);

    pwm_slice_num = pwm_gpio_to_slice_num(CFG_AUDIO_PIN_P);
    pwm_config cfg = pwm_get_default_config();
    pwm_config_set_wrap(&cfg, pwm_wrap);
    pwm_config_set_clkdiv_int(&cfg, 1);
    pwm_init(pwm_slice_num, &cfg, false);
    pwm_set_chan_level(pwm_slice_num, PWM_CHAN_A, pwm_mid);
    pwm_set_chan_level(pwm_slice_num, PWM_CHAN_B, pwm_wrap - pwm_mid);

    for (uint i = 0; i < CFG_HOP_SIZE; i++)
        dma_buf[0][i] = dma_buf[1][i] = ((pwm_wrap-pwm_mid)<<16)|pwm_mid;

    dma_chan = dma_claim_unused_channel(true);
    dma_channel_config dc = dma_channel_get_default_config(dma_chan);
    channel_config_set_transfer_data_size(&dc, DMA_SIZE_32);
    channel_config_set_read_increment(&dc, true);
    channel_config_set_write_increment(&dc, false);
    channel_config_set_dreq(&dc, pwm_get_dreq(pwm_slice_num));
    dma_channel_configure(dma_chan, &dc,
        &pwm_hw->slice[pwm_slice_num].cc,
        dma_buf[0], CFG_HOP_SIZE, false);
    dma_channel_set_irq0_enabled(dma_chan, true);
    irq_set_exclusive_handler(DMA_IRQ_0, dma_irq_handler);
    irq_set_enabled(DMA_IRQ_0, true);
    pwm_set_enabled(pwm_slice_num, true);
    dma_channel_start(dma_chan);
}

// ================================================================
// MIDIイベント処理 (Core0)
// ================================================================
static void dispatch_events() {
    while (g_player.q_read != g_player.q_write) {
        NoteEvent ev;
        memcpy(&ev,(const void*)&g_player.queue[g_player.q_read],sizeof(NoteEvent));
        __dmb();
        g_player.q_read = (g_player.q_read+1) % MidiPlayer::QUEUE_SIZE;

        // ----- ADPCMドラム経路 -----
        // ドラム (ch10 / is_drum=1) は iFFT 加算合成 ではなく
        // ADPCMサンプル再生エンジン (drum_engine) で鳴らす。
        // 自然減衰でリングアウトするので duration_ms は使わない。
        if (ev.is_drum) {
            if (ev.velocity > 0) drum_engine_note_on(ev.note, ev.velocity);
            continue;
        }

        // ----- iFFT 加算合成経路 (旋律系) -----
        uint16_t vi = voice_idx_from_note(ev.note, ev.program);  // ピアノは鍵盤ごとに音色選択

        if (ev.velocity > 0)
            // MIDI記載の音長に忠実 (減衰型は自然減衰、持続型は離鍵で停止)
            ps_note_on(ev.note, ev.velocity, vi, ev.channel, millis() + (uint32_t)ev.duration_ms);
        else
            ps_note_off(ev.note, ev.channel);
    }
}

// ================================================================
// OLED: ノーツ降下
// ================================================================
static void draw_notes() {
    // 鍵盤ラインを画面下に置き、これから鳴る音程を上から降らせる
    const int PLAY_Y   = CFG_PHYS_H - 1;                 // 判定ライン(最下部)
    const int X_OFFSET = (CFG_PHYS_W - CFG_PIANO_KEYS) / 2;
    const int32_t ppm  = ((int32_t)PLAY_Y << 16) / (int32_t)VIS_WINDOW_MS;

    display.clearDisplay();
    uint32_t cur = g_player.playback_ms;
    int ne = g_player.ev_count();

    // 既に通り過ぎたイベントをスキップ
    while (g_vis_cursor < ne &&
           g_player.ev_time(g_vis_cursor) + g_player.ev_dur(g_vis_cursor) < cur)
        g_vis_cursor++;

    // cur から VIS_WINDOW_MS 先までのノートを描画
    for (int i = g_vis_cursor; i < ne; i++) {
        uint32_t t  = g_player.ev_time(i);
        int32_t diff = (int32_t)(t - cur);
        if (diff > (int32_t)VIS_WINDOW_MS) break;   // これ以降は未来すぎる

        uint8_t note = g_player.ev_note(i);
        if (note < CFG_MIDI_NOTE_A0 || note > CFG_MIDI_NOTE_C8) continue;
        int x = X_OFFSET + (note - CFG_MIDI_NOTE_A0);
        if (x < 0 || x >= CFG_PHYS_W) continue;

        // ノート下端のy: 発音時刻にPLAY_Yへ到達
        int yb = PLAY_Y - (int)(((int64_t)diff * ppm) >> 16);
        // バーの高さ = 長さ
        int bh = (int)(((int32_t)g_player.ev_dur(i) * ppm) >> 16);
        if (bh < 2) bh = 2;
        int yt = yb - bh;
        // 画面内にクリップ
        if (yb < 0 || yt > PLAY_Y) continue;
        if (yt < 0)      yt = 0;
        if (yb > PLAY_Y) yb = PLAY_Y;
        if (yb - yt < 1) continue;
        display.drawFastVLine(x, yt, yb - yt, SSD1306_WHITE);
    }

    display.drawFastHLine(0, PLAY_Y, CFG_PHYS_W, SSD1306_WHITE);
    display.display();
}

static void draw_menu() {
    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE); display.setTextSize(1);
    display.setCursor(0,0); display.println("-- SELECT --");
    int total=g_songlist.count();
    int start=max(0,g_song_idx-2), end=min(total,start+5);
    for (int i=start;i<end;i++){
        display.setCursor(0,12+(i-start)*10);
        display.print(i==g_song_idx?"> ":"  ");
        // SongList.txt の日本語等を文字化けさせずに表示するため
        // 非ASCII(>=0x80)バイトは '_' に置換 (SSD1306フォントはASCIIのみ)
        char b[18]; strncpy(b,g_songlist.get(i).title,17); b[17]='\0';
        for (int k=0; b[k]; k++) if ((unsigned char)b[k] >= 0x80) b[k] = '_';
        display.print(b);
    }
    display.display();
}

// ================================================================
// 曲開始
// ================================================================
static void start_song(int idx) {
    if (!g_songlist.count()) return;
    g_song_idx = idx % g_songlist.count();
    const SongInfo& info = g_songlist.get(g_song_idx);
    if (!g_player.load_path(info.midi_path)) return;

    g_vis_cursor = 0;
    ps_all_off();
    // パレット同期リセット
    // (fill/cons seqはそのまま回し続けるので明示リセット不要)

    // 曲開始時の情報表示 (show_on_oled の代わりに非ASCII対策付きで自前描画)
    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);
    display.setTextSize(1);
    display.setCursor(0,0); display.print("Playing: #"); display.println(g_song_idx + 1);
    char tb[22]; strncpy(tb, info.title, 21); tb[21]='\0';
    for (int k=0; tb[k]; k++) if ((unsigned char)tb[k] >= 0x80) tb[k] = '_';
    display.setCursor(0,16); display.println(tb);
    display.display();
    g_showing_info = true;
    g_info_start   = millis();
    DBG_PRINTF("[start] %s\n", info.title);
}

// ================================================================
// ボタン
// ================================================================
static void handle_buttons(uint32_t now) {
    if (now - g_last_btn_ms < 200) return;
    bool next=!digitalRead(CFG_BTN_NEXT), play=!digitalRead(CFG_BTN_PLAY);
    if (!next && !play) return;
    g_last_btn_ms = now;
    int total = g_songlist.count(); if (!total) return;

    if (g_in_menu) {
        if (next){ g_song_idx=(g_song_idx+1)%total; draw_menu(); }
        if (play){ g_in_menu=false; g_player.stop(); start_song(g_song_idx); }
    } else {
        if (next){ g_player.stop(); start_song((g_song_idx+1)%total); }
        if (play){
            if (g_player.is_playing()){ g_player.stop(); g_in_menu=true; draw_menu(); }
            else                       { start_song(g_song_idx); }
        }
    }
}

// ================================================================
// setup (Core0)
// ================================================================
void setup() {
    Serial1.setTX(0); Serial1.setRX(1);
    Serial1.begin(115200); delay(200);
    DBG_PRINTLN("=== iFFT_Orgel v3 ===");

    set_sys_clock_khz(CFG_SYS_CLK_KHZ, false);

    Wire.setSDA(CFG_I2C_SDA); Wire.setSCL(CFG_I2C_SCL);
    Wire.setClock(1000000); Wire.begin();
    if (!display.begin(SSD1306_SWITCHCAPVCC, CFG_OLED_ADDR)) {
        DBG_PRINTLN("[oled] FAIL");
    } else {
        display.clearDisplay(); display.setTextColor(SSD1306_WHITE);
        display.setCursor(15,24); display.println("iFFT Orgel v3");
        display.display();
    }

    pinMode(CFG_BTN_NEXT, INPUT_PULLUP);
    pinMode(CFG_BTN_PLAY, INPUT_PULLUP);

    if (!LittleFS.begin()) {
        DBG_PRINTLN("[fs] FAIL");
        display.clearDisplay(); display.setCursor(0,20);
        display.println("LittleFS FAIL"); display.display();
        while(1) delay(1000);
    }

    int n = g_songlist.load(CFG_MIDI_DIR);
    DBG_PRINTF("[songs] %d\n", n);
    if (!n) {
        display.clearDisplay(); display.setCursor(0,16);
        display.println("No MIDI files"); display.display();
        while(1) delay(1000);
    }

    // 合成エンジン初期化 (ツイドル/窓/倍音テーブル)
    ps_init();
    drum_engine_init();    // <-- 追加: ADPCMドラムボイステーブル初期化

    setup_pwm_dma();

    if (CFG_AUTO_START) start_song(0);
    else { g_in_menu=true; draw_menu(); }
    DBG_PRINTLN("[setup] done");
}

void loop() {
    uint32_t now = millis();

    // タイトル表示中
    if (g_showing_info) {
        if (now - g_info_start >= SONG_INTERVAL_TIME) {
            g_showing_info = false;
            g_player.play();
            DBG_PRINTLN("[play] start");
        }
        handle_buttons(now);
        // 無音パレットを供給し続ける (アンダーラン防止)
        if (ps_fill_seq - ps_cons_seq < 2) {
            ps_paint_palette(now);
            g_paint_count++;
        }
        return;
    }

    g_player.tick(now);
    dispatch_events();
    handle_buttons(now);

    // パレット描画 (Core1が消費した分を補充、1hop先まで常時更新)
    if (ps_fill_seq - ps_cons_seq < 2) {
        ps_paint_palette(now);
        g_paint_count++;
    }

    // 曲終了: 最後の音の余韻(リリース)が消えるまで待ってから次曲へ
    if (g_player.is_finished() && ps_active == 0) {
        int next = (g_song_idx+1) % g_songlist.count();
        if (!CFG_REPEAT_MODE && next==0) {
            display.clearDisplay(); display.setCursor(10,24);
            display.println("Finished."); display.display();
            g_player.stop();
            return;
        }
        start_song(next);
    }

    // OLED 30fps
    if (now - g_last_oled_ms >= 33) {
        g_last_oled_ms = now;
        if      (g_in_menu)       draw_menu();
        else if (!g_showing_info) draw_notes();
    }

    // 診断
    static uint32_t last_dbg=0;
    if (now-last_dbg >= 5000) {
        last_dbg=now;
        DBG_PRINTF("[%lus] act=%d drum=%d pos=%lums fill=%lu cons=%lu paint=%lu\n",
            (unsigned long)(now/1000),(int)ps_active, drum_engine_active_count(),
            (unsigned long)g_player.playback_ms,
            (unsigned long)ps_fill_seq,(unsigned long)ps_cons_seq,
            (unsigned long)g_paint_count);
    }
}

// ================================================================
// Core1: iFFT合成
// ================================================================
void setup1() {
    // Core0のsetup完了を待つ (ps_init/pwm初期化後)
    while (ps_fill_seq == 0) delay(1);
}

void loop1() {
    if (need_fill) {
        need_fill = false;
        // パレットが用意できていれば消費 (Core0 が painted_seq を進めた後)
        if (ps_fill_seq > ps_cons_seq) {
            ps_render_block(dma_buf[buf_fill], pwm_wrap, pwm_mid);
        }
    }
}
