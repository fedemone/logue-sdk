// Copyright 2025 tilr

// Each pressed key triggers a voice, I set max 8 polyphony voices in Drumlogue
// Voices hold A and B resonators, a mallet and noise generator
// they also calculate split frequencies for coupled resonators
// and tune the resonators modals by providing the models

#pragma once

#include <stddef.h>
#include <stdint.h>
#include "float_math.h" // Needed for float32_t
#include "Mallet.h"
#include "Noise.h"
#include "Resonator.h"
#include <atomic>

class alignas(16) Voice
{
public:
    Voice() {}
    ~Voice() {}

    // Core Audio Methods
    float32_t note2freq(int _note);

    // Trigger now matches the optimized .cpp signature
    void trigger(float32_t srate, int _note, float32_t _vel, float32_t malletFreq);

    // Release no longer takes a timestamp in the optimized version
    void release();

    void clear();

    // Tuning & Parameters
    void setPitch(float32_t a_coarse, float32_t b_coarse, float32_t a_fine, float32_t b_fine);

    // Applies pitch factor to a model array using NEON
    void applyPitch(float32_t* model, float32_t factor);

    /** * Main update function for physics models.
     * Handles frequency coupling (Jitter + SIMD) and updates Resonator states.
     * @param updateFrequencies: If false, skips expensive coupling/pitch math.
     */
    void updateResonators(bool updateFrequencies = true);

    void setCoupling(bool _couple, float32_t _split);

    // [NEW] Safe clear method to be called by the audio thread
    inline void checkAndClear() {
        if (m_needs_clear.exchange(false, std::memory_order_acquire)) {
            resA.clear();
            resB.clear();
            mallet.clear();
            noise.clear();
        }
    }
    
    // --- Public Members (accessed by Voice Manager) ---
    int       note = 0;        // MIDI note number
    float32_t freq = 0.0f;     // Frequency in Hz
    float32_t vel = 0.0f;      // MIDI velocity 0.0 .. 1.0
    bool      isRelease = false;
    bool      isPressed = false; // used for audioIn
    bool      couple = false;
    float32_t split = 0.0f;

    // Pitch factors calculated from setPitch
    float32_t aPitchFactor = 1.0f;
    float32_t bPitchFactor = 1.0f;

    // Components
    Mallet    mallet{};
    Noise     noise{};
    Resonator resA{};
    Resonator resB{};

private:
    // Persistent buffers for frequency shifts
    // These store the state of the coupled frequencies between updates
    alignas(16) float32_t aShifts[64] = {0};
    alignas(16) float32_t bShifts[64] = {0};
    std::atomic<bool> m_needs_clear{false};
};