#pragma once
// ================================================================
// config.h — iFFT_Orgel V3 (v7 Note Pool Architecture)
// ================================================================
#include <Arduino.h>

//System prams
#define CFG_SYS_CLK_KHZ     225000
#define CFG_SAMPLE_RATE     24000U
#define CFG_MAX_POLYPHONY   128
#define CFG_MAX_SONGS       128

//ifft_q15
#define FFT_N      4096
#define FFT_LOG2   12
#define CFG_FFT_SIZE        4096U

//palette_synth
//#define PS_SINTAB       1024
//#define PS_SINTAB_BITS  10
#define PS_SINTAB       4096
#define PS_SINTAB_BITS  12
#define PS_HALF         (FFT_N/2 + 1)

//drum_engine
#define DRUM_VOICES 48
// 同一ノートあたりの最大同時発音数
//   1 : 完全choke (連打タイミング最優先、余韻なし)
//   2 : 規定 (自然な余韻 + 連打時最古をchoke)
//   3 : クラッシュ系を重ねたい時 (DRUM_VOICES分連打の高速ハイハット余韻保持にも有効)
// Maximum polyphony per single note
//   1 : Complete choke (Prioritizes rapid trigger timing, no tail/reverb)
//   2 : Default (Natural tail/reverb + chokes the oldest note during rapid triggers)
//   3 : For layering crash cymbals (Also effective for preserving tails during DRUM_VOICES-note rapid hi-hat rolls)
#define DRUM_CHOKE    1
#define DRUM_DEFAILT  2
#define DRUM_DURINF   3
#define DRUM_POLYPHONY_PER_NOTE DRUM_DURINF

#define CFG_SPEC_HALF       (CFG_FFT_SIZE/2 + 1)
#define CFG_HOP_SIZE        (CFG_FFT_SIZE/2)
#define DRUM_LATENCY_OFFSET (CFG_HOP_SIZE / 2)  // パーカッション系発火タイミング= 512サンプル @ 24Khzz ≈ 20ms

//Sound prams
#define CFG_MASTER_GAIN      9000U

#define CFG_GAIN_PIANO       1.2f
#define CFG_GAIN_CHROMATIC   1.1f
#define CFG_GAIN_ORGAN       1.1f
#define CFG_GAIN_GUITAR      1.1f
#define CFG_GAIN_BASS        1.2f
#define CFG_GAIN_STRINGS     1.1f
#define CFG_GAIN_ENSEMBLE    1.1f
#define CFG_GAIN_BRASS       1.2f
#define CFG_GAIN_REED        0.8f
#define CFG_GAIN_PIPE        1.0f
#define CFG_GAIN_SYNTH_LEAD  1.1f
#define CFG_GAIN_SYNTH_PAD   1.1f
#define CFG_GAIN_SYNTH_FX    1.1f
#define CFG_GAIN_ETHNIC      1.1f
#define CFG_GAIN_PERC        1.1f
#define CFG_GAIN_SFX         0.7f
#define CFG_GAIN_DRUM        0.21f

//Pin asine prams
#define CFG_AUDIO_PIN_P     2
#define CFG_AUDIO_PIN_N     3

#define CFG_I2C_SDA         4
#define CFG_I2C_SCL         5

//I2C prams
#define CFG_I2C_FREQ        1000000UL
#define CFG_PHYS_W          128
#define CFG_PHYS_H          64
#define CFG_OLED_ADDR       0x3C

//Control button pin asine prams
#define CFG_BTN_PLAY        14
#define CFG_BTN_NEXT        15

//Play mode prams
#define CFG_AUTO_START      true
#define CFG_REPEAT_MODE     true
#define SONG_INTERVAL_TIME  2000U

//Drectry prams
#define CFG_MIDI_DIR        "/midi"
#define CFG_SONGLIST_FILE   "/SongList.txt"

//midi_player
#define MIDI_LOOKAHEAD_MS   10000U  //先読み秒数
#define MIDI_KEEP_PAST_MS   10U     //発火してからノーツが消えるまでの時間

#define CFG_MIDI_NOTE_A0    21
#define CFG_MIDI_NOTE_C8    108
#define CFG_PIANO_KEYS      72

#define DBG_ENABLE  1
#if DBG_ENABLE
  #define DBG_PRINTF(...)   Serial1.printf(__VA_ARGS__)
  #define DBG_PRINTLN(x)    Serial1.println(x)
#else
  #define DBG_PRINTF(...)
  #define DBG_PRINTLN(x)
#endif