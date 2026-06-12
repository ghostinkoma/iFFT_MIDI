# iFFT MIDI

A GM-compatible MIDI playback system operating on the RP2040.  
It performs iFFT-based sound synthesis using overtone (harmonic) data generated from a SoundFont (GeneralUser GS).

---

## Overview

This project consists of the following components:

- General MIDI (GM128) compatible playback engine
- Timbre analysis toolchain from SoundFont (GeneralUser GS)
- Harmonic extraction (Goertzel algorithm)
- Sound synthesis re-constructed by iFFT
- IMA-ADPCM playback engine for drums
- Standalone operation on RP2040 (No external RAM required)
- 12-bit sound output resolution
- Development environment: Arduino IDE 2.3.x
  
---

## How to Build

1. Clone this repository or download the ZIP file, then build and flash it using Arduino IDE 2.3.x.
2. Please configure the following two settings during the build:
   - Select **Raspberry Pi Pico** from the Board Manager. (If you haven't installed the board support yet, please install it before selecting).
   - Go to **Tools** -> **Flash Size** and select **Sketch 1MB FS 1M**.
3. The flashing process is identical to writing any other sample sketch to the RP2040.
4. The project already includes sound sources sampled from GeneralUser GS.
5. If you prefer to use custom sounds, please extract them using the toolchain located in the `/tool` directory.
   - *Note: Extraction using sound sources other than GeneralUser GS has not been tested.*
  
---

## Uploading MIDI Files

- This project uses LittleFS. If you haven't installed the uploader plugin yet, please install it from the following repository:
  https://github.com/earlephilhower/arduino-littlefs-upload
- Press `Ctrl + Shift + P` and select the LittleFS upload option.
- Files located in the `/data` directory of the project will be uploaded to the target device.
- Please store your MIDI files in `/data/midi`.
- Ensure that the entire `/data` folder does not exceed **1MB**.
  
---

## Key Features

### ■ Sound Source Configuration
- Supports up to 128 voices (Polyphonic playback)
- Pianos are separated into individual voices per 88 keys.
- Other GM timbres are sampled with octave/semitone strides.
- Automatic detection of Sustain / Decay.
- Maximum polyphony: 128 tones + 42 percussion voices.
- The default sound source is sampled from **GeneralUser GS** by Christian Collins:
  https://github.com/mrbumpy409/GeneralUser-GS

### ■ Audio Processing
- FFT Window Size: 2048
- Harmonic Extraction: Frequency analysis via the Goertzel algorithm
- Loudness equalization via RMS normalization
- Time-domain waveform reconstruction via iFFT
- iFFT processing runs at 26 Ksps.
  
### ■ Drum Processing
- IMA-ADPCM 4-bit compression
- Standard GM Drum Kit support (Bank 128)
- Streaming playback method
- Maximum percussion polyphony: Up to 42 voices

---

## Sound Data Generation

Sound data is generated separately using the `gm_extract` tool.

**Processing Flow:**
1. Load SF2 (GeneralUser GS).
2. Render each program and note.
3. Exclude the attack portion and analyze the steady-state components.
4. Extract harmonic (overtone) components.
5. Apply RMS normalization.
6. Convert to C++ header files.

**Outputs:**
- `voice_table.cpp`
- `voice_table.h`
- `voice_harm_data.h`
- `drum_data.cpp`

---

## Test Bench Files

- `1812Overture.mid`
- `Bond.mid`
- `dq6-theme.mid`
- `GraxyExpress999.mid`
- `la-campanella-Franz-liszt-paganini.mid`
- `Umi no Mieru Machi.mid`
- `xi - FREEDOM DiVE↓.mid`

*All files can be played back smoothly without any audio dropouts.*

---

## Hardware Requirements

- RP2040 microcontroller
- PWM output or a simple DAC
- BTL output
- Speaker (Small speakers are acceptable)
- Minimal external components (such as a coupling capacitor)
  - **CRITICAL:** Never omit the coupling capacitor. Failure to include it may destroy the microcontroller's GPIO pins.
- SSD1306 OLED Display
- Pin assignments can be modified in `config.h`.

*\*No external RAM required.*

---

## Japanese Text Support

- By listing the song title, artist, and MIDI filename in `SongList.txt`, you can display the song information on the OLED at the beginning of playback.
- JIS Level 1 & 2 Kanji and symbols are fully integrated into the project.
- It utilizes the **Shinonome Font** (16-dot bitmap font).
- To prioritize readability, there is a limit on the number of characters that can be displayed.
- Please refer to `SongList.txt` for detailed notation methods and examples.

---

## Limitations

- It does not perfectly replicate SF2 modulations (uses simplified rendering).
- Timbres are dependent on GeneralUser GS.
- Drums rely on simplified waveform playback based on ADPCM.
- Tested on several speakers, but some models may not yield sufficient sound pressure.
  - In such cases, connecting to an external amplifier is recommended. 
  - When connecting to an external amplifier, please lower the value of `#define CFG_MASTER_GAIN` or insert an attenuator resistor.
  - **A coupling capacitor is strictly required in all configurations.**

## Disclaimer & Notes

- This software is available for commercial use. However, the author assumes **absolutely no liability or responsibility** for any damages arising from its use.
- If you extract sounds from a SoundFont, you must comply with the license regulations of that specific SoundFont.

---

## Acknowledgments

- **Christian Collins** (Author of GeneralUser GS)
  https://github.com/mrbumpy409/GeneralUser-GS
- **code4fukui** (Author of Shinonome Font)
  https://github.com/code4fukui/shinonome-font
- **Earle F. Philhower III** (Author of arduino-littlefs-upload)
  https://github.com/earlephilhower/arduino-littlefs-upload
