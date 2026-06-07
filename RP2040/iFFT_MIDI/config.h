#pragma once
// ================================================================
// config.h — iFFT_Orgel V3 設定
//   ・ストリーミングMIDI対応版 (RAM 128KB -> 約30KB に削減)
//   ・ADPCM ドラム 32ボイス
//   ・チャイコフスキー1812 等の大編成・長尺曲を想定
// ================================================================
#include <Arduino.h>

// ================================================================
// 基本
// ================================================================
#define CFG_SAMPLE_RATE     22050U
#define CFG_MAX_POLYPHONY   64

// クロック (225MHzに上げてあるなら 225000)
#define CFG_SYS_CLK_KHZ     225000

// ================================================================
// FFT/合成
// ================================================================
#define CFG_FFT_SIZE        2048U
#define CFG_SPEC_HALF       (CFG_FFT_SIZE/2 + 1)
#define CFG_HOP_SIZE        (CFG_FFT_SIZE/2)

// ================================================================
// 音量
// ================================================================
#define CFG_MASTER_GAIN     9000

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
#define CFG_GAIN_SFX         0.21f
#define CFG_GAIN_DRUM        0.21f

// ================================================================
// ハードウェア
// ================================================================
#define CFG_AUDIO_PIN_P     2
#define CFG_AUDIO_PIN_N     3

#define CFG_I2C_SDA         4
#define CFG_I2C_SCL         5
#define CFG_I2C_FREQ        1000000UL
#define CFG_PHYS_W          128
#define CFG_PHYS_H          64
#define CFG_OLED_ADDR       0x3C

#define CFG_BTN_PLAY        14
#define CFG_BTN_NEXT        15

// ================================================================
// 動作モード
// ================================================================
#define CFG_AUTO_START      true
#define CFG_REPEAT_MODE     true
#define SONG_INTERVAL_TIME  2000U

// ================================================================
// MIDI (ストリーミング版用設定)
// ================================================================
#define CFG_MIDI_DIR        "/midi"
#define CFG_MAX_SONGS       128       // RAM が空いたので 62 -> 128 に増設可能
#define CFG_SONGLIST_FILE   "/SongList.txt"

// ストリーミング再生のチューニング (midi_player.cpp 内で #ifndef ガード)
//   ・前後8秒分のイベントをリングに保持
//   ・長い持続音(8秒以上)は note-on 即時発行のプレースホルダで動作
#define MIDI_LOOKAHEAD_MS   8000U   // 再生位置+Nms 先のイベントまで decode
#define MIDI_KEEP_PAST_MS   8000U   // 再生位置-Nms 前のイベントを保持 (可視化用)

// 旧 CFG_MAX_EVENTS は廃止 (ストリーミング化により上限ナシ)
// #define CFG_MAX_EVENTS  ... // 削除

//音階マッピング
#define CFG_MIDI_NOTE_A0    21
#define CFG_MIDI_NOTE_C8    108

// ================================================================
// ノーツ
// ================================================================
#define CFG_PIANO_KEYS      72

// ================================================================
// デバッグ
// ================================================================
#define DBG_ENABLE  1
#if DBG_ENABLE
  #define DBG_PRINTF(...)   Serial1.printf(__VA_ARGS__)
  #define DBG_PRINTLN(x)    Serial1.println(x)
#else
  #define DBG_PRINTF(...)
  #define DBG_PRINTLN(x)
#endif