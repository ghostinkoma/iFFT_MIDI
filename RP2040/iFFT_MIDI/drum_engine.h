// ============================================================================
// drum_engine.h - ADPCM 4-bit drum playback engine
//
// 32 ボイス: 大編成オーケストラ (1812等) や 32分音符ハイハット連打を想定
//   RAM 増分: 約 +830 byte (16→32)
//   CPU 増分: ほぼゼロ (空きボイスは即 skip)
// ============================================================================
#pragma once
#include <Arduino.h>
#include "config.h"

void drum_engine_init();
void drum_engine_note_on(uint8_t midi_note, uint8_t velocity);
void drum_engine_mix(int32_t* out, int samples);
int  drum_engine_active_count();