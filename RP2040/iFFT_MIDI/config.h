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
#define FFT_N      2048
#define FFT_LOG2   11
#define CFG_FFT_SIZE        2048U

//palette_synth
//#define PS_SINTAB       1024
//#define PS_SINTAB_BITS  10
#define PS_SINTAB       2048
#define PS_SINTAB_BITS  11
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
#define DRUM_LATENCY_OFFSET (CFG_HOP_SIZE / 2)  // = 512サンプル @ 24kHz ≈ 21ms (Hann窓中央=hop半分に整合)

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

//Display language prams
//   英数(8x16)は全言語で常時リンク。各言語は参照したフォント分だけフラッシュ増。
//   フォントは U8g2_for_Adafruit_GFX 同梱の GNU Unifont (16x16 ゴシック, Unicode)。
#define LANG_ENG  0   // 英数のみ
#define LANG_JPN  1   // 日本語 (かな + JIS漢字) + 英数
//   将来: LANG_KOR / LANG_CHS 等を追加し、text_render.cpp の FONTS_CJK を差し替え
#define DISPLAY_LANG        LANG_JPN

//   第二水準(希少漢字 = japanese3, +約158KB)を含めるか。
//   1: 第二水準まで網羅 (フラッシュ ~83%)
//   0: japanese1+2 のみ (かな+常用+頻出, フラッシュ ~68%, 希少漢字は '?' 表示)
//      → 後々 USB MSC / 44.1kHz でフラッシュが要る時の節約スイッチ
#define JPN_INCLUDE_LEVEL2  1

//   日本語フォントの実体を選択:
//     JPN_FONT_UNIFONT   : U8g2_for_Adafruit_GFX 同梱の GNU Unifont (追加ファイル不要)
//     JPN_FONT_SHINONOME : 東雲ゴシック (Public Domain, font_shnmk16_jp.c / font_shnm8x16_jp.c
//                          をスケッチに同梱)。見栄え重視・単一フォント・英数も東雲で統一。
//   ※ SHINONOME 選択時は JPN_INCLUDE_LEVEL2 は無視 (1フォントで第二水準まで内包)。
#define JPN_FONT_UNIFONT    0
#define JPN_FONT_SHINONOME  1
#define JPN_FONT            JPN_FONT_SHINONOME

//USB mass-storage (PCから直接 MIDI / SongList.txt を編集)
//  1: 内蔵フラッシュFAT を USBメモリとしてPCへ公開 (FatFS + FatFSUSB)
//     ※ 初回起動で内蔵FSは FAT へ自動再フォーマット(既存LittleFSデータは消去)
//        以後はPC上のドライブに /midi フォルダを作りMIDIを置く
//  0: 公開しない (FatFSは使うがUSBドライブにはしない)
#define USE_USB_MSC         1

//USB MIDI 入力 (PCからのMIDIを内蔵シンセで鳴らす外部音源モード)
//  1: 既定USBスタックに USB MIDI I/F を追加 (MSCと共存)。MIDI受信中は外部音源化。
//  0: 無効
//  ※ Tools>USB Stack は「Pico SDK」(既定)のまま。Adafruit TinyUSB に変えると
//     FatFSUSB が使えなくなるので変更しないこと。
#define USE_USB_MIDI        1

//  USB MIDI ポート名 (DAW/デバイスマネージャに出る名前 = iInterface文字列)
//この文字列を変更を反映させるためには、Windowsのデバイスマネージャーより古いデバイス名のアンインストールをおこなったあと、
//RP2040をいったんUSBから抜いてもう一度接続してください。
#define USB_MIDI_NAME       "iFFT Koma Sound"

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

//USB Mass Storage (FatFSUSB) prams
//   1: PC接続時に 1MB FatFS領域を USBメモリとして公開 (MIDI/SongList.txt を直接編集)
//      ※ ストレージは LittleFS では不可。FatFS 必須 (PCはLittleFSを読めない)
//   0: 無効 (従来どおり内部FSのみ)
#define USE_USB_STORAGE     1

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