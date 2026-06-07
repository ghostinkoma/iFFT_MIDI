// auto-generated drum_data.h - IMA-ADPCM 4-bit mono drum samples
#pragma once
#include <Arduino.h>

#define DRUM_SAMPLE_RATE 22050

struct DrumSample {
    uint32_t offset;     // byte offset into DRUM_ADPCM_DATA
    uint32_t length;     // number of decoded samples (nibbles)
    int16_t  init_predictor;
    uint8_t  init_step_index;
    uint8_t  used;       // 0 = no sample mapped for this MIDI note
};

extern const uint8_t      DRUM_ADPCM_DATA[];
extern const uint32_t     DRUM_ADPCM_TOTAL_BYTES;
extern const DrumSample   drum_map[128];
