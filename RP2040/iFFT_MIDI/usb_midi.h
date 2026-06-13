#pragma once
// ============================================================================
// usb_midi.h — USB MIDI 入力 (PCからのMIDIを内蔵シンセで鳴らす外部音源モード)
//
//   ・arduino-pico 既定USBスタックに <tusb-midi.h> でMIDI I/Fを追加。
//     FatFSUSB(MSC) と同じスタック上で共存する(スタック切替不要)。
//   ・受信した生MIDIを解析し、ファイル再生と同じ発音経路へ流す:
//       ch9 → drum_engine_note_on / 他 → voice_idx_from_note + ps_note_on/off
//   ・直近に受信があれば「MIDIモード」とみなす(時間ベース)。
// ============================================================================
#include <Arduino.h>
#include "config.h"

void usb_midi_begin();
bool usb_midi_is_active();        // 直近にMIDI受信があったか(モード判定)
void usb_midi_poll(uint32_t now); // 受信MIDIを解析して即発音
void usb_midi_all_off();          // パニック(全消音)