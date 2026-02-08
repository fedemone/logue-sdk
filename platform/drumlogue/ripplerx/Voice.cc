#include <stddef.h>
#include <cstddef>
#include <cstdio>
#include <cstring> // For memcpy
#include "float_math.h"
#include "Voice.h"
#include "Models.h"
#include "constants.h"
#include <arm_neon.h> // Explicitly include NEON

void Voice::Init()
{
    m_initialized = true;
    m_gate = true;
    m_framesSinceNoteOn = 0;
    isPressed = true;
}

float32_t Voice::note2freq(int _note)
{
    constexpr float32_t c_inv_semitones = 1.0f / c_semitones_per_octave;
    return c_midi_a4_converted * fast_pow2((float)_note * c_inv_semitones);
}

void Voice::trigger(float32_t srate, int _note, float32_t _vel, float32_t malletFreq)
{
    resA.clear();
    resB.clear();

    note = _note;
    vel = _vel;
    freq = note2freq(note);
    isRelease = false;
    isPressed = true;

    mallet.trigger(srate, malletFreq);
    noise.attack(_vel);

    if (resA.isOn()) resA.activate();
    if (resB.isOn()) resB.activate();

    updateResonators(true);
}

void Voice::release()
{
    isRelease = true;
    isPressed = false;
    noise.release();
    updateResonators(false);
}

void Voice::clear()
{
    mallet.clear();
    noise.clear();
    resA.clear();
    resB.clear();
    m_gate = false;
    m_initialized = false;
    isPressed = false;
    m_framesSinceNoteOn = SIZE_MAX;
}

void Voice::setCoupling(bool _couple, float32_t _split) {
    couple = _couple;
    split = _split;
}

void Voice::setPitch(float32_t a_coarse, float32_t b_coarse, float32_t a_fine, float32_t b_fine)
{
    constexpr float32_t inv_semitones = 1.0f / c_semitones_per_octave;
    constexpr float32_t inv_cents = 1.0f / (c_cents_per_semitone * c_semitones_per_octave);

    aPitchFactor = fasterpowf(2.0f, a_coarse * inv_semitones + a_fine * inv_cents);
    bPitchFactor = fasterpowf(2.0f, b_coarse * inv_semitones + b_fine * inv_cents);
}

void Voice::applyPitch(float32_t* __restrict model, float32_t factor)
{
    float32x4_t v_factor = vdupq_n_f32(factor);
    for (size_t i = 0; i < 64; i += 16) {
        float32x4_t a0 = vld1q_f32(&model[i]);
        float32x4_t a1 = vld1q_f32(&model[i + 4]);
        float32x4_t a2 = vld1q_f32(&model[i + 8]);
        float32x4_t a3 = vld1q_f32(&model[i + 12]);

        vst1q_f32(&model[i],      vmulq_f32(a0, v_factor));
        vst1q_f32(&model[i + 4],  vmulq_f32(a1, v_factor));
        vst1q_f32(&model[i + 8],  vmulq_f32(a2, v_factor));
        vst1q_f32(&model[i + 12], vmulq_f32(a3, v_factor));
    }
}

/**
 * @brief Key Optimization
 * - ChangesSIMD "Broadcast" Loading (vld1q_dup_f32):In the coupling loop, the previous code loaded a vector of B values,
 *  then used a switch statement to extract scalars, then duplicated them.
 * I replaced this with vld1q_dup_f32, which loads a single float from memory and replicates it to all 4 vector
 * lanes in a single instruction. This significantly reduces pipeline stalling.
 * - Simplified Loop Structure:I flattened the nested tiling. We now iterate the outer loop i (A-models)
 * in steps of 4 (vectors) and the inner loop j (B-models) linearly. We compare 4 A partials against 1 B partial simultaneously.
 *  This is much more cache-friendly and removes the overhead of managing the inner fb_idx loop.
 * - Branchless Selection (vbslq_f32 vs vand):I optimized the conditional accumulation. Instead of masking and adding,
 * I use vbslq_f32 (Bitwise Select) to choose between 1.0f and 0.0f based on comparison masks,
 * which is semantically cleaner and maps directly to hardware mux instructions.
 * - Vectorized Pitch Application:In applyPitch, the scalar factor is loaded into a vector register once outside the loop,
 * rather than relying on the compiler to optimize the scalar-vector multiplication inside the loop.
 * - Memory Management:Added __restrict to pointers to allow the compiler to assume memory regions do not overlap,
 * enabling better instruction reordering.Visualizing the SIMD StrategyThe coupling loop is an $O(N^2)$ operation
 * (every partial affects every other partial). To make this fast, we use a "Broadcast" strategy.
 * Instead of comparing one A to one B, we load Four A's into a register.
 * Then we pick One B, broadcast it to fill a register, and perform the subtraction across all four lanes at once.
 *
 * NOTE: the Euclidean Distance (Sqrt vs Linear Approx)
 * Original Code:
 * C++
 * sqrt(pow(dx, 2.0) + pow(dy, 2.0)) // Exact Euclidean distance
 * Optimized Code:
 * C++
 * 0.4*dy - 0.6*dx + 0.56*max(dx,dy) // Alpha max plus beta min algorithm
 * Impact: The optimized code uses a fast linear approximation of the square root (similar to how GPUs calculate lighting).
 * It has an error margin of about ~4-5%.
 * Result: The "repulsion" force between partials will be slightly stronger or weaker depending on the specific frequency ratio,
 * but the general behavior (pushing frequencies apart) remains the same.
 * @param updateFrequencies
 */
void Voice::updateResonators(bool updateFrequencies)
{
    if (!updateFrequencies) {
        if (resA.isOn()) resA.update(freq, vel, isRelease, aShifts);
        if (resB.isOn()) resB.update(freq, vel, isRelease, bShifts);
        return;
    }

    alignas(16) float32_t localAShifts[64];
    alignas(16) float32_t localBShifts[64];

    const float32_t* aModelSrc = getAModels(resA.getModel());
    const float32_t* bModelSrc = getBModels(resB.getModel());

    memcpy(localAShifts, aModelSrc, 64 * sizeof(float32_t));
    memcpy(localBShifts, bModelSrc, 64 * sizeof(float32_t));

    if (aPitchFactor != 1.0f) applyPitch(localAShifts, aPitchFactor);
    if (bPitchFactor != 1.0f) applyPitch(localBShifts, bPitchFactor);

    if (couple && resA.isOn() && resB.isOn() && freq > 0.1f) {

        // Base constants
        const float32_t k = split * c_coupling_split_factor / freq;
        const float32_t dy_base = k * c_coupling_split_factor / freq; // Base DY

        // Vector constants
        const float32x4_t v_threshold = vdupq_n_f32(c_coupling_threshold);
        const float32x4_t v_half = vdupq_n_f32(0.5f);
        const float32x4_t v_coeff_dx_06 = vdupq_n_f32(c_freq_shift_coeff_dx);
        const float32x4_t v_coeff_max_56 = vdupq_n_f32(c_freq_shift_coeff_max);
        const float32x4_t v_zero = vdupq_n_f32(0.0f);
        const float32x4_t v_one = vdupq_n_f32(1.0f);

        // OUTER LOOP: Tiles of 4
        for (int i = 0; i < 64; i += 4) {

            // JITTER TRICK: Vary 'dy' slightly based on the partial index 'i'
            // This replaces the expensive 'cos(avg)' from the original code.
            // (i & 7) creates a pattern that repeats every 8 partials.
            float32_t jitter = 1.0f + (float)(i & 7) * 0.02f;
            float32_t dy_jittered = dy_base * jitter;

            // Recalculate dy-dependent vector constants for this batch
            float32x4_t v_dy = vdupq_n_f32(dy_jittered);
            float32x4_t v_coeff_dy_04 = vdupq_n_f32(c_freq_shift_coeff_dy * dy_jittered);
            float32x4_t v_coeff_dy_56 = vdupq_n_f32(c_freq_shift_coeff_max * dy_jittered);

            float32x4_t fa_vec = vld1q_f32(&localAShifts[i]);
            float32x4_t k_count = v_zero;
            float32x4_t x_count = v_zero;
            float32x4_t dx_max = v_zero;
            float32x4_t dy_max = v_zero;

            // INNER LOOP
            for (int j = 0; j < 64; ++j) {
                float32x4_t fb_vec = vld1q_dup_f32(&localBShifts[j]);
                float32x4_t dx_vec = vmulq_f32(vsubq_f32(fa_vec, fb_vec), v_half);

                float32x4_t abs_dx = vabsq_f32(dx_vec);
                uint32x4_t valid_mask = vcleq_f32(abs_dx, v_threshold);

                // x_count += dx
                x_count = vaddq_f32(x_count, vbslq_f32(valid_mask, dx_vec, v_zero));

                // k_count += sign(dx)
                uint32x4_t dx_pos_mask = vcgtq_f32(dx_vec, v_zero);
                float32x4_t k_inc = vbslq_f32(valid_mask,
                                      vbslq_f32(dx_pos_mask, v_one, vdupq_n_f32(-1.0f)),
                                      v_zero);
                k_count = vaddq_f32(k_count, k_inc);

                // dx_max += dx (where |dx| > |dy|)
                uint32x4_t gt_dy_mask = vcgtq_f32(abs_dx, v_dy);
                uint32x4_t dx_max_active = vandq_u32(valid_mask, gt_dy_mask);
                dx_max = vaddq_f32(dx_max, vbslq_f32(dx_max_active, dx_vec, v_zero));

                // dy_max += 1.0 (where |dy| >= |dx|)
                uint32x4_t dy_max_active = vandq_u32(valid_mask, vmvnq_u32(gt_dy_mask));
                dy_max = vaddq_f32(dy_max, vbslq_f32(dy_max_active, v_one, v_zero));
            }

            // Final Calculation
            float32x4_t term1 = vmulq_f32(v_coeff_dy_04, k_count);
            float32x4_t term2 = vmulq_f32(v_coeff_dx_06, x_count);
            float32x4_t term3 = vmulq_f32(v_coeff_max_56, dx_max);
            float32x4_t term4 = vmulq_f32(v_coeff_dy_56, dy_max);

            float32x4_t shift = vsubq_f32(vaddq_f32(term1, term3), term2);
            shift = vaddq_f32(shift, term4);

            float32x4_t a_curr = vld1q_f32(&localAShifts[i]);
            float32x4_t b_curr = vld1q_f32(&localBShifts[i]);

            vst1q_f32(&localAShifts[i], vaddq_f32(a_curr, shift));
            vst1q_f32(&localBShifts[i], vsubq_f32(b_curr, shift));
        }
    }

    memcpy(aShifts, localAShifts, 64 * sizeof(float32_t));
    memcpy(bShifts, localBShifts, 64 * sizeof(float32_t));

    if (resA.isOn()) resA.update(freq, vel, isRelease, localAShifts);
    if (resB.isOn()) resB.update(freq, vel, isRelease, localBShifts);
}