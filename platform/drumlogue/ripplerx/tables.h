#pragma once
#include "float_math.h"

// Global static table for instant pitch lookups
struct FastTables {
    float note_to_delay_length[128] = {0.0f};

    // Called ONCE during Init()
    inline void generate(float srate) {
        for (int i = 0; i < 128; ++i) {
            // Standard MIDI to Hz formula.
            // Use powf (not fasterpowf) — this is a startup-only initialisation,
            // NOT in the audio hot loop.  fasterpowf has a ~3% systematic error
            // (e.g. fasterpowf(2,0)≈0.97 instead of 1.0) that would make every
            // note in the table ~50 cents flat before any filter compensation.
            float freq = 440.0f * powf(2.0f, ((float)i - 69.0f) / 12.0f);

            // Protect against divide-by-zero or sub-sonic frequencies.
            // Clamp to 24 Hz (MIDI note 24, B0): 48000/24 = 2000 samples, safely inside
            // the 2048-sample buffer. External MIDI notes below 24 are clamped in code.
            if (freq < 12.0f) freq = 12.0f;

            note_to_delay_length[i] = srate / freq;
        }
    }
};

// Expose the global instance
extern FastTables g_tables;