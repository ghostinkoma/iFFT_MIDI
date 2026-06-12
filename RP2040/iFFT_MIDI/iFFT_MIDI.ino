// ============================================================
// iFFT_Orgel v3 — FFTパレット加算合成
//
// [BUGFIX v7.1]
// 1. draw_notes(): ソートなしプールに対する正しい全スキャン
//    - ev_in_use() チェック追加 → 解放済みスロットのゴーストノーツ完全排除
//    - ソート仮定の break を廃止 → 未来ノーツの見逃し完全排除
//    - g_vis_cursor を廃止 → 4分後のノーツ消失バグ完全修正
// 2. DRUM_LATENCY_OFFSET: float除算 → integer 定数化
// 3. 曲切り替え時: drum_engine_init() でドラムボイスリセット追加
// 4. dispatch: DISPATCH_LEAD_MS を 100ms に拡大 (フレームレート依存取りこぼし抑制)
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
#include "drum_engine.h"
#include "text_render.h"

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

// ノーツ表示
#define VIS_WINDOW_MS 1500u
static uint32_t g_paint_count = 0;

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

        if (ev.is_drum) {
            if (ev.velocity > 0) drum_engine_note_on(ev.note, ev.velocity);
            continue;
        }

        uint16_t vi = voice_idx_from_note(ev.note, ev.program);

        if (ev.velocity > 0) {
            // [BUGFIX] ev.duration_ms==65535 は「終端未確定(ACTIVE)」。
            //   そのまま渡すと end_ms = now + 65535ms = 65秒後の自動リリースになり、
            //   note-off を取りこぼした音が数十秒鳴り続ける。
            //   確定済みの長さだけ信頼し、未確定/異常長は安全側で 30秒に頭打ち。
            //   (正規ノートは明示 note-off が先に来るので、この上限は実質暴走保護)
            uint32_t dur_ms = ev.duration_ms;
            if (dur_ms >= NOTE_DURATION_ACTIVE || dur_ms > 30000u) dur_ms = 30000u;
            ps_note_on(ev.note, ev.velocity, vi, ev.channel, millis() + dur_ms);
        } else {
            ps_note_off(ev.note, ev.channel);
        }
    }
}

// ================================================================
// OLED: ノーツ降下
//
// [BUGFIX] プールはソートされていない。
// 旧コードの問題:
//   1. ev_in_use() チェックなし → 解放済みスロットのゴーストノーツ描画
//   2. diff > VIS_WINDOW_MS での break → 未来のノーツが後続スロットにあっても見逃す
//   3. g_vis_cursor のスキップ条件が解放済みスロットの古いstart_msに反応して
//      有効ノーツをスキップする → 約4分後にノーツが全消失
//
// 修正: プール全体を毎フレームスキャン (4096スロット × 条件分岐のみ = ~12μs/frame)
// ================================================================
static void draw_notes() {
    const int PLAY_Y   = CFG_PHYS_H - 1;
    const int X_OFFSET = (CFG_PHYS_W - CFG_PIANO_KEYS) / 2;
    const int32_t ppm  = ((int32_t)PLAY_Y << 16) / (int32_t)VIS_WINDOW_MS;

    display.clearDisplay();
    uint32_t cur = g_player.playback_ms;
    int ne = g_player.ev_count();  // = POOL_SIZE = 4096

    // [BUGFIX] g_vis_cursor廃止: プール全体をスキャンする
    // - ev_in_use() で解放済みスロットを確実にスキップ
    // - break を廃止: ソートされていないプールではbreakは誤り
    // - 過去ノーツ(end_ms < cur - 200ms)のみ描画スキップ (高速化)
    for (int i = 0; i < ne; i++) {
        // [BUGFIX #1] 解放済みスロットは必ずスキップ
        if (!g_player.ev_in_use(i)) continue;

        uint32_t t   = g_player.ev_time(i);
        uint16_t dur = g_player.ev_dur(i);

        // [BUGFIX #2] ドラム・持続音(active)を正しく処理
        // dur==65535(ACTIVE)は end_ms未確定: t以降を描画候補とする
        // dur==0 は解放済みの可能性があるが ev_in_use()チェック済み
        uint32_t end_ms;
        if (dur == 65535u) {
            end_ms = cur + VIS_WINDOW_MS + 1000u;  // 確定前は未来とみなす
        } else {
            end_ms = t + (uint32_t)dur;
        }

        // 完全に過去のノーツはスキップ (視覚的にPLAY_Yより下に出る)
        if (end_ms + 200 < cur) continue;

        // [BUGFIX #3] 窓より遠い未来はスキップ (break廃止→continue)
        int32_t diff = (int32_t)(t - cur);
        if (diff > (int32_t)VIS_WINDOW_MS) continue;  // ← breakからcontinueへ変更

        uint8_t note = g_player.ev_note(i);
        if (note < CFG_MIDI_NOTE_A0 || note > CFG_MIDI_NOTE_C8) continue;
        if (g_player.ev_is_drum(i)) continue;  // ドラムはノーツ表示しない

        int x = X_OFFSET + (note - CFG_MIDI_NOTE_A0);
        if (x < 0 || x >= CFG_PHYS_W) continue;

        // ノート下端のy: 発音時刻にPLAY_Yへ到達
        int yb = PLAY_Y - (int)(((int64_t)diff * ppm) >> 16);
        // バーの高さ = 長さ (activeノーツは最小高さ2)
        int bh;
        if (dur == 65535u) {
            bh = 2;
        } else {
            bh = (int)(((int32_t)dur * ppm) >> 16);
            if (bh < 2) bh = 2;
        }
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
    text_set_color(SSD1306_WHITE);
    text_draw(0, 0, "-- SELECT --");                  // 行0 (英数8x16)

    int total = g_songlist.count();
    // 16px行は画面に3行入る (16/32/48)。選択を中央寄りに。
    int start = g_song_idx - 1; if (start < 0) start = 0;
    if (start > total - 3) start = (total > 3) ? total - 3 : 0;
    int shown = (total - start < 3) ? (total - start) : 3;

    for (int i = 0; i < shown; i++) {
        int idx = start + i;
        int y   = 16 + i * 16;
        int x   = text_draw(0, y, (idx == g_song_idx) ? ">" : " ");
        x       = text_draw(x, y, " ");
        text_draw_clip(x, y, g_songlist.get(idx).title, CFG_PHYS_W - x);
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

    // [BUGFIX] g_vis_cursor廃止 (draw_notes()で全スキャンに変更したため不要)
    // g_vis_cursor = 0;  ← 削除

    ps_all_off();

    // [BUGFIX] 曲切り替え時にドラムエンジンをリセット
    // 前の曲のドラムボイスが鳴り続けるバグを防ぐ
    drum_engine_init();

    // 曲開始時の情報表示 (4行/各16px)
    //   1-2行目: タイトル (songlistの空白を保持。"\n"指定で強制改行/無ければ幅折返し, 2行でクリップ)
    //   3行目  : Author の値 (128px = 全角8/半角16 でクリップ)
    //   4行目  : MIDI ファイル名 (basename, 128px でクリップ)
    display.clearDisplay();
    text_draw_wrap(0, 0, info.title, CFG_PHYS_W, 16, 2);
    text_draw_clip(0, 32, info.author, CFG_PHYS_W);
    {
        const char* fn = strrchr(info.midi_path, '/');
        fn = fn ? fn + 1 : info.midi_path;
        text_draw_clip(0, 48, fn, CFG_PHYS_W);
    }
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
    DBG_PRINTLN("=== iFFT_Orgel v3 [BUGFIX v7.1] ===");

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

    // UTF-8 テキスト描画(日本語等)を既存 Adafruit バッファ上で有効化
    text_init(display);

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

    ps_init();
    drum_engine_init();

    setup_pwm_dma();

    if (CFG_AUTO_START) start_song(0);
    else { g_in_menu=true; draw_menu(); }
    DBG_PRINTLN("[setup] done");
}

void loop() {
    uint32_t now = millis();

    if (g_showing_info) {
        if (now - g_info_start >= SONG_INTERVAL_TIME) {
            g_showing_info = false;
            g_player.play();
            DBG_PRINTLN("[play] start");
        }
        handle_buttons(now);
        if (ps_fill_seq - ps_cons_seq < 2) {
            ps_paint_palette(now);
            g_paint_count++;
        }
        return;
    }

    g_player.tick(now);
    dispatch_events();
    handle_buttons(now);

    if (ps_fill_seq - ps_cons_seq < 2) {
        ps_paint_palette(now);
        g_paint_count++;
    }

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

    if (now - g_last_oled_ms >= 33) {
        g_last_oled_ms = now;
        if      (g_in_menu)       draw_menu();
        else if (!g_showing_info) draw_notes();
    }

    static uint32_t last_dbg=0;
    if (now-last_dbg >= 5000) {
        last_dbg=now;
        DBG_PRINTF("[%lus] act=%d drum=%d pos=%lums fill=%lu cons=%lu paint=%lu pool=%d\n",
            (unsigned long)(now/1000),(int)ps_active, drum_engine_active_count(),
            (unsigned long)g_player.playback_ms,
            (unsigned long)ps_fill_seq,(unsigned long)ps_cons_seq,
            (unsigned long)g_paint_count, g_player.pool_in_use());
    }
}

// ================================================================
// Core1: iFFT合成
// ================================================================
void setup1() {
    while (ps_fill_seq == 0) delay(1);
}

void loop1() {
    if (need_fill) {
        need_fill = false;
        if (ps_fill_seq > ps_cons_seq) {
            ps_render_block(dma_buf[buf_fill], pwm_wrap, pwm_mid);
        }
    }
}
