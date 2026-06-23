#pragma once
#include <cstdint>
#include "wavetables.h"

class WavetableOsc {
public:
    float phase = 0.0f;
    float phase_inc = 0.0f;
    // Pointer to the active waveform array
    const float* current_table = nullptr;
    float table_length = 256.0f;

    // Call this from NoteOn or when the drone pitch is modulated
    inline void set_frequency(float hz, float sample_rate) {
        phase_inc = hz / sample_rate;
    }

    inline float interpolate(float p) {
        // Assuming standard 256 samples per cycle for AKWF wavetables
        float f_idx = p * table_length;
        int idx = (int)f_idx;
        float frac = f_idx - (float)idx;

        int next_idx = (idx + 1) % (int)table_length;

        float val1 = current_table[idx];
        float val2 = current_table[next_idx];

        return val1 + frac * (val2 - val1);
    }

    inline float process() {
        if (!current_table) return 0.0f;

        float out = interpolate(phase);

        // Advance or decrement phase based on phase_inc sign
        phase += phase_inc;

        // Bidirectional wrapping logic
        if (phase >= 1.0f) {
            phase -= 1.0f;
        } else if (phase < 0.0f) {
            phase += 1.0f;
        }

        return out;
    }
};