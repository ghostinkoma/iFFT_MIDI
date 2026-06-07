#pragma once
// ================================================================
// palette_synth.h — FFTパレット加算合成エンジン (iFFT版)
//
// Core0: paint_palette()  各倍音を FFT bin に配置 (位相追跡)
// Core1: render_block()   パレットリミッター→iFFT→Hann窓→OLA→PCM
// ================================================================
#include <Arduino.h>
#include "config.h"
#include "ifft_q15.h"

#define PS_SINTAB       1024
#define PS_SINTAB_BITS  10
#define PS_HALF         (FFT_N/2 + 1)

void ps_init();
void ps_note_on (uint8_t midi, uint8_t vel, uint16_t voice_idx, uint8_t channel, uint32_t end_ms);
void ps_note_off(uint8_t midi, uint8_t channel);
void ps_all_off();
int  ps_paint_palette(uint32_t now_ms);
void ps_render_block(uint32_t* out, uint32_t pwm_wrap, uint32_t pwm_mid);

extern volatile uint32_t ps_fill_seq;
extern volatile uint32_t ps_cons_seq;
extern volatile uint8_t  ps_active;