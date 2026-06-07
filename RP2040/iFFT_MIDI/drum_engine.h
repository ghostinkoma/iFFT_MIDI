// ============================================================================
// drum_engine.h - ADPCM 4-bit drum playback engine
//
// 32 ボイス: 大編成オーケストラ (1812等) や 32分音符ハイハット連打を想定
//   RAM 増分: 約 +830 byte (16→32)
//   CPU 増分: ほぼゼロ (空きボイスは即 skip)
// ============================================================================
#pragma once
#include <Arduino.h>

#ifndef DRUM_VOICES
#define DRUM_VOICES 32
#endif

// 同一ノートあたりの最大同時発音数
//   1 : 完全choke (連打タイミング最優先、余韻なし)
//   2 : 規定 (自然な余韻 + 連打時最古をchoke)
//   3 : クラッシュ系を重ねたい時 (32分連打の高速ハイハット余韻保持にも有効)
#ifndef DRUM_POLYPHONY_PER_NOTE
#define DRUM_POLYPHONY_PER_NOTE 3
#endif

void drum_engine_init();
void drum_engine_note_on(uint8_t midi_note, uint8_t velocity);
void drum_engine_mix(int32_t* out, int samples);
int  drum_engine_active_count();