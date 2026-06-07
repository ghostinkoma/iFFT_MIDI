// auto-generated voice_table.h
#pragma once
#include <Arduino.h>
#include "voice_harm_data.h"

#define VOICE_HARM_MAX 64

struct VoiceMeta {
    const char* name;
    float       base_hz;
    float       decay_tau_ms;
    uint8_t     nh;
    uint8_t     sustained;   // 1=hold(organ) 0=decay(piano)
    uint8_t     category;    // GM category 0..16
    float       peak;
    uint32_t    harm_off;
};

// stride = semitones between consecutive samples (1=per-note, 12=per-octave)
struct MultiSample { uint8_t program; uint8_t note_lo; uint8_t note_hi; uint8_t stride; uint16_t base_voice; };

extern const VoiceMeta voice_table[];
extern const int        VOICE_COUNT;
extern const uint16_t   piano_note_to_voice[88];
extern const uint16_t   gm_program_to_voice[128];
extern const uint16_t   gm_drum_to_voice[128];
extern const MultiSample multi_samples[];
extern const int        MULTI_SAMPLE_COUNT;

static inline const int16_t* voice_harm_ptr(uint16_t vi) {
    return &VOICE_HARM_DATA[voice_table[vi].harm_off];
}
static inline uint16_t voice_idx_from_drum(uint8_t note) { return gm_drum_to_voice[note & 0x7F]; }
static inline uint16_t voice_idx_from_note(uint8_t midi_note, uint8_t prog) {
    if (prog < 8 && midi_note >= 21 && midi_note <= 108)
        return piano_note_to_voice[midi_note - 21];
    for (int i=0;i<MULTI_SAMPLE_COUNT;i++) {
        const MultiSample& m = multi_samples[i];
        if (m.program==prog) {
            uint8_t n = midi_note;
            if (n < m.note_lo) n = m.note_lo;
            if (n > m.note_hi) n = m.note_hi;
            uint16_t idx = (uint16_t)(n - m.note_lo) / (m.stride ? m.stride : 1);
            return (uint16_t)(m.base_voice + idx);
        }
    }
    return gm_program_to_voice[prog];
}
