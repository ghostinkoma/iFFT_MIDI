#pragma once
// ================================================================
// config.h — iFFT_Orgel V3 (v7 Note Pool Architecture)
// ================================================================
#include <Arduino.h>

#define CFG_SAMPLE_RATE     22050U
#define CFG_MAX_POLYPHONY   128
#define CFG_SYS_CLK_KHZ     225000

#define CFG_FFT_SIZE        2048U
#define CFG_SPEC_HALF       (CFG_FFT_SIZE/2 + 1)
#define CFG_HOP_SIZE        (CFG_FFT_SIZE/2)

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

#define CFG_AUTO_START      true
#define CFG_REPEAT_MODE     true
#define SONG_INTERVAL_TIME  2000U

#define CFG_MIDI_DIR        "/midi"
#define CFG_MAX_SONGS       128
#define CFG_SONGLIST_FILE   "/SongList.txt"

// ★ v7: lookahead はそのまま、KEEP_PAST は「解放までの margin」(短くて OK)
//   ・解放条件は per-note (両発火済+end_ms 過去) なので margin は最小限で十分
#define MIDI_LOOKAHEAD_MS   20000U
#define MIDI_KEEP_PAST_MS   10U     // 各 Note の release margin (短くて良い)

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