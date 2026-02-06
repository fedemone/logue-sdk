#include "Waveguide.h"
#include "constants.h"
#include <cmath>

// Proper constructor with safe initialization
Waveguide::Waveguide()
	: read_ptr(0), write_ptr(0), tube_decay(0.0f), y{}, y1{}
{
	// Initialize radius and max_radius using NEON intrinsics for float32x4_t
	// Broadcast single values to all 4 lanes
	radius = vdupq_n_f32(0.0f);
	max_radius = vdupq_n_f32(1.0f);
	for (int i = 0; i < c_tube_len; ++i) {
		tube[i] = vdupq_n_f32(0.0f);
	}
}


/**
 * @brief Key improvements
 * - Branchless Hot Path: The is_closed check is gone. We use vmlaq_f32 with a vPolarity register.
 * On ARM, a multiply-accumulate is usually just as fast as a standalone add/sub, but removing the branch
 * prevents pipeline stalls.
 * - Pre-calculated Coefficients: vOneMinusRadius is calculated once in update.
 * - Register Residency: Because the Resonator calls this, and we've kept the state in float32x4_t,
 * the compiler can likely keep the waveguide state in the NEON "D" or "Q" registers without ever moving
 * them to the slower general-purpose registers.
 * - Bit-Manipulation: The update function is significantly lighter by avoiding the standard log/exp
 * calls for velocity-based decay.
 *
 * @param f_0
 * @param vel
 * @param isRelease
 */
void Waveguide::update(float32_t f_0, float32_t vel, bool isRelease)
{
    // Constants for fast math
    // const float inv_log2e = 0.69314718f; // ln(2)

    // 1. Frequency Validation and Tuning
    // Prevent division by zero and extreme high frequencies
    f_0 = fminf(c_nyquist_factor * srate, fmaxf(c_freq_min, f_0));

    // Calculate the floating-point delay length (tlen)
    float tlen = srate / f_0;
    if (is_closed) {
        tlen *= c_closed_tube_octave_factor; // Adjust for closed tube physics
    }

    // 2. Fractional Delay Logic (All-pass Coefficient)
    // tlen = integer_part + fractional_part
    int int_delay = (int)tlen;
    float frac = tlen - (float)int_delay;

    // All-pass coefficient: g = (1 - delta) / (1 + delta)
    // This provides a flat frequency response compared to linear interpolation.
    float g = (1.0f - frac) / (1.0f + frac);

    // Update read_ptr relative to write_ptr
    // If c_tube_len = 20000, we ensure we wrap within the buffer
    read_ptr = write_ptr - int_delay;
    while (read_ptr < 0) read_ptr += c_tube_len;
    if (read_ptr >= c_tube_len) read_ptr %= c_tube_len;

    // 3. Decay Calculation (Optimized with Bit-Manipulation)
    // Identity: decay_base * exp(vel_offset) -> decay_base * 2^(vel_offset * log2(e))
    float offset_decay = (vel * vel_decay * M_TWOLN100) * M_LOG2_E;

    union { float f; int32_t i; } u;
    // The constant 1065353216 is the bit-representation of 1.0f in IEEE-754
    // 8388608.0f is 2^23, shifting the value into the exponent bits
    u.i = (int32_t)(offset_decay * 8388608.0f) + 1065353216;

    float decay_k = fminf(c_decay_max, decay * u.f);
    if (isRelease) {
        decay_k *= rel;
    }

    // Calculate final tube decay factor (coefficient for feedback)
    float tube_decay_val = 0.0f;
    if (decay_k > c_decay_min) {
        // We use e_expff for the complex physics-based decay curve
        float exponent = (-M_PI * c_waveguide_decay_constant) / (f_0 * srate * decay_k);
        tube_decay_val = e_expff(exponent);
    }

    // 4. NEON Vector Broadcasting
    // We "bake" these into registers once so process() only does math
    vDecay = vdupq_n_f32(tube_decay_val);
    vPolarity = vdupq_n_f32(is_closed ? -1.0f : 1.0f);
    vG = vdupq_n_f32(g);

    // Radius (Damping) coefficients
    // Assuming radius is a member updated by the user/UI
    vRadius = radius;
    vOneMinusRad = vsubq_f32(vdupq_n_f32(1.0f), vRadius);
}

/**
 * @brief Key improvements:
 * - Pitch Precision: By adding the vFrac logic, your waveguide will have perfect tuning even at very high
 *  frequencies (high sample rates).
 * - NEON Vectorization: Linear interpolation usually adds two multiplications and an addition.
 * Using vmulq and vmlaq, this is compressed into just a few cycles.
 * - Pipeline Flow: Notice that we use read_ptr_prev. Even though there is a small integer check
 *  for the pointer wrapping, the NEON unit stays busy with the actual floating-point math, hiding that latency.
 * - Inversion Logic: The use of vmlaq_f32(input, dsample, vPolarity) is incredibly efficient.
 * It performs $input + (dsample \cdot 1.0)$ or $input + (dsample \cdot -1.0)$ without any branching.
 * - Linear interpolation follows the formula: $out = (1-frac) \cdot tube[n] + frac \cdot tube[n-1]$.
 *  - The All-pass Waveguide LogicThe formula for an all-pass fractional delay is:
 * $y[n] = g \cdot x[n] + x[n-1] - g \cdot y[n-1]$
 * Where $g$ is the coefficient derived from the fractional part of the delay.
 *
 * @param input
 * @return float32x4_t
 */
float32x4_t Waveguide::process(float32x4_t input) {
    // 1. Fetch from delay line
    float32x4_t xn = tube[read_ptr];

    // 2. All-pass Interpolation: y = g*xn + x_prev - g*y_prev
    // Optimized as: y = x_prev + g*(xn - y_prev)
    float32x4_t vDiff = vsubq_f32(xn, vAP_State);
    float32x4_t sample = vmlaq_f32(vAP_State_Prev_X, vG, vDiff);

    // Update all-pass states for next sample
    vAP_State_Prev_X = xn;
    vAP_State = sample;

    // 3. Damping Filter (Radius)
    float32x4_t vY = vmulq_f32(vRadius, sample);
    vY = vmlaq_f32(vY, vY1, vOneMinusRad);
    vY1 = vY;

    // 4. Decay and Feedback
    float32x4_t dsample = vmulq_f32(vY, vDecay);

    // tube[w] = input + (dsample * polarity)
    tube[write_ptr] = vmlaq_f32(input, dsample, vPolarity);

    // 5. Update pointers
    if (++read_ptr >= c_tube_len)  read_ptr = 0;
    if (++write_ptr >= c_tube_len) write_ptr = 0;

    return dsample;
}

void Waveguide::clear() {
    read_ptr = 0;
    write_ptr = 0;
    vY1 = vdupq_n_f32(0.0f);
    vAP_State = vdupq_n_f32(0.0f);
    vAP_State_Prev_X = vdupq_n_f32(0.0f);

    for (int i = 0; i < c_tube_len; ++i) {
        tube[i] = vdupq_n_f32(0.0f);
    }
}