# iFFT MIDI

A GM-compatible MIDI playback system running on the RP2040.
It performs iFFT-based sound synthesis using harmonic data generated from a SoundFont (GeneralUser GS).

---

## Overview

This project operates with the following specifications:

- General MIDI (GM128) compatible playback engine
- Instrument analysis toolchain for SoundFont (GeneralUser GS)
- Harmonic extraction (using the Goertzel algorithm)
- Re-synthesized sound source via iFFT
- IMA-ADPCM playback engine for drums
- Runs on RP2040 standalone (no external RAM required)
- 12-bit sound output resolution
- Development environment: Arduino IDE 2.3.x

---

## Build Instructions

- Clone this repository or download the zip file, then build and upload using Arduino IDE 2.3.x.
- Please configure the following two settings during the build:
    - Select "Raspberry Pi Pico" from the board manager (install it first if you haven't already).
    - Under **Tools > Flash Size**, select "Sketch 1MB FS 1M".
- The process is exactly the same as uploading other sample sketches to an RP2040.
- The project already includes sound sources sampled from GeneralUser GS.
- If you wish to customize the sound sources, please use the toolchain in `/tool` to extract them.
    - (Extraction from SoundFonts other than GeneralUser GS has not been tested.)

---

## MIDI File Upload
- When connected to a Raspberry Pi Pico or compatible device, it will be recognized as a USB flash drive.
- The disk size is 1MB for the Raspberry Pi Pico.
- Please check the drive and format it as FAT.
- Create a folder named `midi` in the root of the drive.
- Placing MIDI files under this `midi` folder will make them available for playback.
- If you want to change this folder name, you can specify it via `CFG_MIDI_DIR` in `config.h`.
- When connected to a PC, the device is recognized as storage.
- **Caution!** If you want to use it for automatic playback, please use a power-only cable, not a communication cable.
- By placing `SongList.txt` in the root of the drive, you can display the song title/author on the OLED at the start of the song.
- To change this filename, modify the value of `CFG_SONGLIST_FILE` in `config.h`.

---

## Key Features

### ■ Sound Source Configuration
- Supports 128 voices (polyphonic playback)
- Pianos are converted to individual voices in 88-key units
- Other GM instruments are sampled at octave/semitone strides
- Automatic judgment of Sustain / Decay
- Maximum simultaneous voice count: 128 voices + 42 percussion voices
- Sound source sampling utilizes GeneralUser GS:
    - https://github.com/mrbumpy409/GeneralUser-GS

### ■ Audio Processing
- FFT window size: 2048
- Harmonic extraction: Frequency analysis via the Goertzel algorithm
- Loudness normalization via RMS normalization
- Time-domain waveform reconstruction via iFFT
- iFFT performed at 26Ksps

### ■ Drum Processing
- 4-bit IMA-ADPCM compression
- Supports standard GM drum kit (bank 128)
- Streaming playback method
- Maximum simultaneous voice count: 42

---

## Sound Data Generation
This process is for customizing the SoundFont. It is generated using the separate tool `gm_extract`.

**Processing Flow:**

1. Load SF2 (GeneralUser GS)
2. Render each program/note
3. Exclude attack portion and analyze steady-state component
4. Extract harmonic components
5. RMS normalization
6. Convert to C++ header

**Output:**

- `voice_table.cpp`
- `voice_table.h`
- `voice_harm_data.h`
- `drum_data.cpp`

---

## Test Bench Files
- 1812Overture.mid
- Bond.mid
- dq6-theme.mid
- GraxyExpress999.mid
- la-campanella-Franz-liszt-paganini.mid
- Umi no Mieru Machi.mid
- xi - FREEDOM DiVE↓.mid
- All can be played without any audio dropouts.

---

## Hardware Requirements

- RP2040 microcontroller
- PWM output or simple DAC
- BTL output
- Speaker (small size is fine)
- External components: Coupling capacitor (essential)
    - **Never omit the coupling capacitor.** Failure to include it may damage the microcontroller's GPIO.
- SSD1306 OLED display
- Pin assignments can be changed in `config.h`.

*No external RAM required.*

---

## Japanese Language Support

- By listing song titles, authors, and MIDI filenames in `SongList.txt`, you can display them on the OLED at the start of the song.
- Kanji JIS Level 1 and 2, and symbols are already included in the project.
- Utilizes the Shinonome font (16-dot).
- There is a limit on the number of displayable characters for visibility.
- Please refer to `SongList.txt` for detailed description methods and samples.

---

## Simple GM-Compatible External MIDI Sound Source Function

- When connected to a PC, it can be used as an external MIDI sound source.
- Due to the limitations of the RP2040, this is a simplified implementation, so please consider the sound quality and latency as a bonus feature.

---

## Limitations

- Not a full reproduction of SF2 modulation (simplified rendering)
- Sound timbre is dependent on GeneralUser GS
- Drums are simplified waveform playback based on ADPCM
- Tested with several speakers, but some speakers may not provide sufficient sound pressure.
- If necessary, it is recommended to connect to an external amplifier. When connecting to an external amplifier:
    - Reduce the value of `#define CFG_MASTER_GAIN` or add an attenuator resistor.
- In any case, a coupling capacitor is mandatory.

## Notes
- This program is available for commercial use, but no responsibility is taken.
- If you extract from a SoundFont, please follow the license regulations of each SF.

---

## Acknowledgments
- GeneralUser GS creator: Christian Collins
    - https://github.com/mrbumpy409/GeneralUser-GS
- Shinonome font creator: code4fukui
    - https://github.com/code4fukui/shinonome-font
