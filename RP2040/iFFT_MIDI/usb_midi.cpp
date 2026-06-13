// ============================================================================
// usb_midi.cpp — USB MIDI 入力 → 内蔵シンセ
// ============================================================================
#include "usb_midi.h"

#if USE_USB_MIDI
#include <tusb-midi.h>                // ビルドで USB MIDI ドライバをリンク(マーカー)
#include <class/midi/midi_device.h>   // tud_midi_stream_read() 等の宣言
#include <device/usbd.h>              // TUD_MIDI_DESCRIPTOR 等
#include <USB.h>                      // arduino-pico 既定スタックの記述子登録
#include <string.h>                   // memcpy
#include "palette_synth.h"
#include "drum_engine.h"
#include "voices/voice_table.h"  // voice_idx_from_note()

static uint8_t  s_prog[16]  = {0};   // チャンネル毎プログラム
static uint8_t  s_status    = 0;     // ランニングステータス
static uint8_t  s_data[2];
static int      s_dcount    = 0;
static int      s_need      = 0;
static uint32_t s_last_rx   = 0;
static int      s_epIn = 0, s_epOut = 0, s_itf = 0;
static uint8_t  s_strID = 0;          // MIDIポート名(iInterface)の文字列インデックス

// MIDI は AudioControl + MIDIStreaming の 2 インターフェース構成。
// simpleInterface() は先頭の番号(dst[2])しか直さず2つ目が壊れる → 記述子が破損し
// 後続のMSCまで巻き添えになる。CDC(SerialUSB)同様、割り当てられた itf 番号で
// 記述子を作り直す専用コールバックを使う。
static void midi_desc_cb(int itf, uint8_t *dst, int len, void *param) {
    (void)param;
    uint8_t desc[TUD_MIDI_DESC_LEN] = {
        TUD_MIDI_DESCRIPTOR((uint8_t)itf, s_strID, s_epOut, s_epIn, 64)
    };
    memcpy(dst, desc, len);
}

void usb_midi_begin() {
    for (int i = 0; i < 16; i++) s_prog[i] = 0;

    // USB記述子に MIDI(2 I/F) を登録 (FatFSUSB の MSC 登録と同じ流儀)。
    USB.disconnect();
    s_strID = USB.registerString(USB_MIDI_NAME);   // ポート名を登録
    s_epIn  = USB.registerEndpointIn();
    s_epOut = USB.registerEndpointOut();
    s_itf = USB.registerInterface(2, midi_desc_cb, nullptr,
                                  TUD_MIDI_DESC_LEN, 5, 0);
    USB.connect();
}

bool usb_midi_is_active() {
    // 直近 1.5 秒以内に受信があれば「演奏中」とみなす
    return s_last_rx && (millis() - s_last_rx) < 1500;
}

void usb_midi_all_off() { ps_all_off(); }

// 1メッセージを発音経路へ (dispatch_events と同じ振り分け)
static void handle_msg(uint8_t status, uint8_t d1, uint8_t d2, uint32_t now) {
    uint8_t type = status & 0xF0;
    uint8_t ch   = status & 0x0F;
    switch (type) {
        case 0x90:                                   // Note On
            if (d2 > 0) {
                if (ch == 9) drum_engine_note_on(d1, d2);
                else {
                    uint16_t vi = voice_idx_from_note(d1, s_prog[ch]);
                    // ライブは終端未知 → 長め上限(=暴走保護)。実際は明示Note Offで解放。
                    ps_note_on(d1, d2, vi, ch, now + 30000u);
                }
                break;
            }
            // velocity 0 は Note Off 扱い → fall through
        case 0x80:                                   // Note Off
            if (ch != 9) ps_note_off(d1, ch);
            break;
        case 0xB0:                                   // Control Change
            if (d1 == 120 || d1 == 123) ps_all_off();// All Sound Off / All Notes Off
            break;
        case 0xC0:                                   // Program Change
            s_prog[ch] = d1;
            break;
        default: break;                              // Pitch Bend/Aftertouch等は無視
    }
}

void usb_midi_poll(uint32_t now) {
    uint8_t buf[64];
    uint32_t n;
    while ((n = tud_midi_stream_read(buf, sizeof(buf))) > 0) {
        s_last_rx = now ? now : 1;
        for (uint32_t i = 0; i < n; i++) {
            uint8_t b = buf[i];
            if (b & 0x80) {                          // ステータスバイト
                if (b >= 0xF8) continue;             // リアルタイム(クロック等)は無視
                if (b >= 0xF0) { s_status = 0; s_dcount = 0; continue; } // システムコモン無視
                s_status = b;
                s_dcount = 0;
                uint8_t t = b & 0xF0;
                s_need = (t == 0xC0 || t == 0xD0) ? 1 : 2;
            } else {                                 // データバイト
                if (!s_status) continue;
                s_data[s_dcount++] = b;
                if (s_dcount >= s_need) {
                    handle_msg(s_status, s_data[0], (s_need >= 2) ? s_data[1] : 0, now);
                    s_dcount = 0;                    // ランニングステータス継続
                }
            }
        }
    }
}

#else  // USE_USB_MIDI == 0
void usb_midi_begin() {}
bool usb_midi_is_active() { return false; }
void usb_midi_poll(uint32_t) {}
void usb_midi_all_off() {}
#endif