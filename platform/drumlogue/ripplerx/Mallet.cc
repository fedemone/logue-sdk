#include "Mallet.h"

void Mallet::trigger(float32_t srate, float32_t freq)
{
    // 1. Setup Stiffness Filter (Bandpass)
    // Limits the noise to a specific frequency band "knock"
    filter.bp(srate, freq, 0.707f);
    filter.clear(); // Don't use clear(), use reset logic from Filter class

    // 2. Setup Duration
    // 200ms duration for the noise burst
    elapsed = (int)(srate * 0.2f);

    // 3. Setup Envelope
    // Initial Amplitude = 2.0 (boosted for impact)
    float32_t start_amp = 2.0f;

    // Decay coefficient for 100ms
    // coef = exp(-1 / (time * srate))
    // We pre-calculate the per-sample decay
    float32_t decay_val = e_expff(-1.0f / (0.1f * srate));

    // NEON State Setup
    // v_amp_state needs to decay across the 4 samples of the first vector.
    // [Amp, Amp*d, Amp*d^2, Amp*d^3]
    float32_t d2 = decay_val * decay_val;
    float32_t d3 = d2 * decay_val;
    float32_t amps[4] = { start_amp, start_amp * decay_val, start_amp * d2, start_amp * d3 };
    v_amp_state = vld1q_f32(amps);

    // The decay coefficient for the *next vector* (4 samples later) is d^4
    float32_t d4 = d3 * decay_val;
    v_decay_coef = vdupq_n_f32(d4);
}

void Mallet::clear()
{
    elapsed = 0;
    v_amp_state = vdupq_n_f32(0.0f);

    // Reset Filter
    filter.clear();
}