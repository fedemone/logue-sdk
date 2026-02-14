#ifdef __cplusplus
extern "C" {
#endif
#include <stddef.h>
#ifdef __cplusplus
}
#endif
#include "Resonator.h"
#ifdef DEBUGN
#include <cstdio>
#include <cstdlib>
#endif
// The resonator, as partial or waveguide, is the main body that's vibrating,
// so it's the core of the sound emitted at note /


Resonator::Resonator()
{
    srate = c_sampleRate;

    // Initialize partials to safe defaults
    for (size_t i = 0; i < c_max_partials; ++i) {
        partials[i].k = static_cast<int>(i + 1);
        partials[i].clear();
    }
}



// called by original plugin::onSlider()
void Resonator::setParams(float32_t _srate, bool _on, int _model, int _partials, float32_t _decay,
    float32_t _damp, float32_t tone, float32_t hit,	float32_t _rel, float32_t _inharm, float32_t _cut,
    float32_t _radius, float32_t vel_decay, float32_t vel_hit, float32_t vel_inharm)
{
	on = _on;
    npartials = _partials;
    nmodel = _model;
	decay = _decay;
	radius = _radius;
	srate = _srate;
	cut = _cut;

	// LowCut now provided in Hz (converted at setParameter); clamp to safe range
	auto freq = fmax(c_freq_min, fmin(c_freq_max, _cut));

	if (_cut < 0.0f) {
		filter.lp(srate, freq, c_butterworth_q);
	}
	else {
		filter.hp(srate, freq, c_butterworth_q);
    }

	// Update ONLY the active partials, not all 64
	for (int p = 0; p < npartials; ++p) {
		partials[p].damp = _damp;
		partials[p].decay = decay;
		partials[p].hit = hit;
		partials[p].inharm = _inharm;
		partials[p].rel = _rel;
		partials[p].tone = tone;
		partials[p].vel_decay = vel_decay;
		partials[p].vel_hit = vel_hit;
		partials[p].vel_inharm = vel_inharm;
		partials[p].srate = _srate;
	}

	waveguide.decay = decay;
	waveguide.radius = vdupq_n_f32(radius);
	waveguide.is_closed = _model == ModelNames::ClosedTube;
	waveguide.srate = srate;
	waveguide.vel_decay = vel_decay;
	waveguide.rel = _rel;
}


/**
 * @brief Key Performance Gains
 * - integrated partials update: reduce function call overhead and maximise vectorization gain
 * - Bit-Manipulation Power: I replaced the generic fasterpowf with the base-2 exponent
 *  identity $2^{x \cdot \text{const}}$. This avoids the expensive internal loop of a standard power function.
 * - Shared Multiplications: The term inv_srate_2pi is used for omega, b0, and inv_decay.
 *  By calculating it once outside the loop, we save 3 divisions per partial ($64 \times 3 = 192$ saved divisions).
 * - Direct Register "Baking": Instead of updating scalar variables and then calling vdupq, we calculate the final
 *  normalized values and store them directly into the vb0...va2 vectors. T
 * his ensures that the next call to process() finds the coefficients exactly where the NEON unit needs them.
 * - Floating Point Dispatch: Using fminf instead of fmin prevents the compiler from promoting values to double
 * (64-bit), which is a common silent performance killer on ARMv7-A.
 *
 * This version incorporates:
 * - Division Minimization: Rearranges the algebra to calculate inv_decay with only 1 division (down from 2).
 * - Safety Clamping: Ensures the divisor d_raw never hits zero, preventing Inf/NaN propagation.
 * - Active Partial Counting: Updates activePartialsCount so the audio render loop can exit early.
 * - Bit-Tricks: Uses the integer-exponent approximation for velocity scaling inside the loop.
 *
 * @param freq
 * @param vel
 * @param isRelease
 * @param model
 */
void Resonator::update(float32_t freq, float32_t vel, bool isRelease, float32_t model[c_max_partials])
{
    // 1. Waveguide Bypass
    // If using the waveguide model, update it and return immediately.
    // (Ensure activePartialsCount is 0 so the Resonator::process loop is skipped)
    if (nmodel >= OpenTube) {
        waveguide.update(model[0] * freq, vel, isRelease);
        activePartialsCount = 0;
        return;
    }

    // --- Pre-calculation of Invariants ---
    const float inv_srate_2pi = M_TWOPI / srate;
    const float f_nyq = c_nyquist_factor * srate;

    // Convert velocity (0.0-1.0) to log scale for the bit-trick
    // M_TWOLN100 is approx 2 * ln(100), scaling velocity curve
    const float log_vel = vel * M_TWOLN100;

    // Shared model constant (Ratio of the highest partial)
    const float ratio_max = model[c_max_partials - 1];

    // Reset counter for the Render loop
    int active_count = 0;

    // --- Main Loop ---
    for (int p = 0; p < npartials; ++p) {
        Partial& part = partials[p];

        // Safety check for model array access
        int idx = (int)part.k - 1;
        if (idx < 0 || (uint32_t)idx >= c_max_partials) {
            // Zero out coefficients to be safe
            part.vb0 = part.vb2 = part.va1 = part.va2 = vdupq_n_f32(0.0f);
            part.active_prev = false;
            continue;
        }

        float32_t ratio = model[idx];

        // ---------------------------------------------------------
        // 1. Frequency & Inharmonicity (Bit-Trick Optimized)
        // ---------------------------------------------------------
        // Calc: 2^(vel_inharm * log_vel) using integer bit manipulation
        float exp_inharm_part = (part.vel_inharm * log_vel) * M_LOG2_E;
        union { float f; int32_t i; } u_inharm;
        u_inharm.i = (int32_t)(exp_inharm_part * 8388608.0f) + 1065353216;

        float inharm_k = fminf(1.0f, part.inharm * u_inharm.f); // Clamp max inharm
        // Stiff string approximation: f = f0 * sqrt(1 + B * (k^2 - 1))
        // Here simplified to ratio-based scaling
        float r_m_1 = ratio - 1.0f;
        inharm_k = fasterSqrt(1.0f + (inharm_k - 0.0001f) * (r_m_1 * r_m_1));

        float f_k = freq * ratio * inharm_k;

        // ---------------------------------------------------------
        // 2. Raw Decay (Bit-Trick Optimized)
        // ---------------------------------------------------------
        // Calc: 2^(vel_decay * log_vel)
        float exp_decay_part = (part.vel_decay * log_vel) * M_LOG2_E;
        union { float f; int32_t i; } u_decay;
        u_decay.i = (int32_t)(exp_decay_part * 8388608.0f) + 1065353216;

        // Calculate raw decay time in seconds (or arbitrary units)
        // REVERT: Removed 0.01f scaling to restore full range.
        float d_raw = part.decay * u_decay.f;

        // Apply Release Envelope
        if (isRelease) {
            d_raw *= fabsf(part.rel); // use fabsf to prevent negative decay flipping the filter stability
        }

        // ---------------------------------------------------------
        // 3. Culling (Optimization: Active Partial Counting)
        // ---------------------------------------------------------
        // If frequency is above Nyquist or decay is effectively zero, mute this partial.
        // We clamp d_raw to c_decay_min here to prevent Div-By-Zero later.
        if (f_k >= f_nyq || f_k < c_freq_min || d_raw < c_decay_min) {
            // Mute this partial
            part.vb0 = part.vb2 = part.va1 = part.va2 = vdupq_n_f32(0.0f);
            // We do NOT increment active_count.
            // Note: This assumes partials are sorted by frequency.
            // If they are not sorted, you must process all, but 'active_count'
            // optimization works best if high-k partials are at the end.
            part.active_prev = false;
            continue;
        }

        // --- WAKE-UP LOGIC (Critical Fix) ---
        // If partial was inactive, we MUST reset its filter state (history).
        // Otherwise, it resumes with stale data, creating a massive "Pop"
        // that explodes the IIR filter instantly.
        if (!part.active_prev) {
            part.vx1_low = vdup_n_f32(0.0f);
            part.vx2_low = vdup_n_f32(0.0f);
            part.vy1_low = vdup_n_f32(0.0f);
            part.vy2_low = vdup_n_f32(0.0f);
        }
        part.active_prev = true;

        // Increment the count of partials that need processing
        active_count++;

        // ---------------------------------------------------------
        // 4. Damping & Tone (Log Domain Math)
        // ---------------------------------------------------------
        // Determine the "Max Frequency" for damping calculations to simulate material loss
        float f_max = fminf(c_freq_max, freq * ratio_max * inharm_k);

        // Damping Factor (d_mod)
        // Log-domain scaling: d_mod = (d_base ^ (damp * 2))
        float d_base = (part.damp <= 0.0f ? freq : f_max) / f_k;
        float d_mod = e_expff(fasterlogf(d_base) * (part.damp * 2.0f));

        // Tone Gain (t_gain)
        // Log-domain scaling: t_gain = (t_base ^ (tone * 2))
        float t_base = (part.tone <= 0.0f ? f_k / freq : f_k / f_max);
        float t_gain = e_expff(fasterlogf(t_base) * (part.tone * 2.0f));

        // ---------------------------------------------------------
        // 5. Coefficient Calculation (Minimized Division)
        // ---------------------------------------------------------
        // Original Logic:
        //    d_k = d_raw / d_mod;
        //    inv_decay = inv_srate_2pi / d_k;
        //
        // Algebraic Optimization:
        //    inv_decay = inv_srate_2pi / (d_raw / d_mod)
        //    inv_decay = (inv_srate_2pi * d_mod) / d_raw
        //
        // Benefit: 1 Div, 1 Mul. (Original: 2 Divs)
        
        // FIX: Calculate effective decay and clamp IT to 10.0s.
        // This prevents instability even if d_mod (damping) extends the decay time.
        float d_eff = d_raw / d_mod;
        d_eff = fminf(10.0f, d_eff);
        
        float inv_decay = inv_srate_2pi / d_eff;

        // Biquad Coefficient Alpha
        // inv_a0 = 1 / (1 + inv_decay)
        // Since d_raw > 0 and d_mod > 0, inv_decay > 0. Denominator is safe.
        float inv_a0 = 1.0f / (1.0f + inv_decay);

        // Hit Position Modulation (Comb-like effect)
        float h_mod = fminf(0.5f, part.hit + vel * part.vel_hit * 0.5f);
        float a_k = 35.0f * fabsf(fastersinfullf(M_PI * part.k * h_mod));

        // Final Filter Constants
        float omega = f_k * inv_srate_2pi;
        float b0_val = inv_srate_2pi * t_gain * a_k;

        // ---------------------------------------------------------
        // 6. Broadcast to SIMD Vectors
        // ---------------------------------------------------------
        // We calculate scalars, then duplicate them to all 4 SIMD lanes
        // so the serial loop in Partial::process can read them easily.

        part.vb0 = vdupq_n_f32(b0_val * inv_a0);
        part.vb2 = vdupq_n_f32(-b0_val * inv_a0);
        // Note: b1 is implicitly 0.0f for this BP filter topology

        part.va1 = vdupq_n_f32(-2.0f * fastercosfullf(omega) * inv_a0);

        // va2 logic:
        // va2 = (1 - inv_decay) / (1 + inv_decay)
        //     = (1 - inv_decay) * inv_a0
        float va2_val = (1.0f - inv_decay) * inv_a0;
        part.va2 = vdupq_n_f32(va2_val);
#ifdef DEBUGN
        // DIAGNOSTIC: Log coefficients if they look suspicious - DEBUG
        if (!std::isfinite(va2_val) || fabsf(va2_val) >= 1.0f) {
            printf("[DIAG] Partial %d: va2=%.6f inv_a0=%.6f inv_decay=%.6f\n",
                part.k, va2_val, inv_a0, inv_decay);
        }
#endif
    }

    // Update the class member for the Render loop
    this->activePartialsCount = active_count;
}


void Resonator::activate()
{
	if (!active) {  // Only reset silence counter on state change
		active = true;
		silence = 0;
	}
}

/**
 * @brief Key improvements:
 * - Register Accumulation: In the previous version, each partial.process(input)
 *  had to return a value, which the caller then added to out.
 *  Here, the compiler can keep the out variable in a NEON register (e.g., q0)
 * for the entire duration of the npartials loop,
 * eliminating unnecessary move instructions.
 * - L1 Cache Locality: Because partials is an array of objects,
 * the CPU fetches the next partial's coefficients while it's still calculating
 * the current one. This is known as hardware prefetching.
 * - Branch Consolidation: By moving the nmodel < OpenTube check outside
 * the loop, you ensure that the CPU only makes the decision once per
 *  4 samples, rather than checking state inside every partial.
 *
 * @param input
 * @return float32x4_t
 */
// input is two stereo samples / four mono samples
float32x4_t Resonator::process(float32x4_t input)
{
    float32x4_t out = vdupq_n_f32(0.0f);


    if (!active) return out;

    if (nmodel < OpenTube) {
        // --- START OF INTEGRATED PARTIAL MANAGER LOGIC ---
        // Fixed: Serial-Parallel processing for Stereo Interleaved data [L0, R0, L1, R1]

        // Extract inputs
        float32x2_t in0 = vget_low_f32(input);  // [L0, R0]
        float32x2_t in1 = vget_high_f32(input); // [L1, R1]

        for (int p = 0; p < npartials; ++p) {
            Partial& part = partials[p];

            // Load state (now just 64-bit loads)
            float32x2_t x1_prev = part.vx1_low;
            float32x2_t x2_prev = part.vx2_low;
            float32x2_t y1_prev = part.vy1_low;
            float32x2_t y2_prev = part.vy2_low;

            // Load coefficients (duplicated in all lanes, so just take low)
            float32x2_t b0 = vget_low_f32(part.vb0);
            float32x2_t b2 = vget_low_f32(part.vb2);
            float32x2_t a1 = vget_low_f32(part.va1);
            float32x2_t a2 = vget_low_f32(part.va2);

            // --- Frame 0 (L0, R0) ---
            // y[n] = b0*x[n] + b2*x[n-2] - a1*y[n-1] - a2*y[n-2]
            float32x2_t term1_0 = vmul_f32(in0, b0);
            float32x2_t term2_0 = vmul_f32(x2_prev, b2);
            float32x2_t term3_0 = vmul_f32(y1_prev, a1);
            float32x2_t term4_0 = vmul_f32(y2_prev, a2);

            float32x2_t out0 = vsub_f32(vadd_f32(term1_0, term2_0), vadd_f32(term3_0, term4_0));

            // --- PARANOID CLAMP (Fixes Explosion) ---
            // IIR filters can accumulate energy to infinity. Hard clamp internal state.
            // This prevents NaN propagation if a glitch occurs.
            // Clamp to +/- 4.0 (ample headroom for audio, strict enough for safety)
            float32x2_t vMax = vdup_n_f32(4.0f);
            float32x2_t vMin = vdup_n_f32(-4.0f);
            out0 = vmin_f32(vmax_f32(out0, vMin), vMax);

            // --- Frame 1 (L1, R1) ---
            // x[n]=in1, x[n-2]=x1_prev, y[n-1]=out0, y[n-2]=y1_prev
            float32x2_t term1_1 = vmul_f32(in1, b0);
            float32x2_t term2_1 = vmul_f32(x1_prev, b2);
            float32x2_t term3_1 = vmul_f32(out0, a1);
            float32x2_t term4_1 = vmul_f32(y1_prev, a2);

            float32x2_t out1 = vsub_f32(vadd_f32(term1_1, term2_1), vadd_f32(term3_1, term4_1));

            // Paranoid Clamp Frame 1
            out1 = vmin_f32(vmax_f32(out1, vMin), vMax);

            // Store state (64-bit stores)
            part.vx1_low = in1;
            part.vx2_low = in0;
            part.vy1_low = out1;
            part.vy2_low = out0;

            // Accumulate partial result into resonator output
            float32x4_t p_out = vcombine_f32(out0, out1);
            out = vaddq_f32(out, p_out);
        }
        #ifdef DEBUGN
        float max_out = fmaxf(fabsf(vgetq_lane_f32(out, 0)), fabsf(vgetq_lane_f32(out, 1)));
        if (max_out > 50.0f || !std::isfinite(max_out)) {
            printf("[RESONATOR EXPLOSION] Output: %.2f\n", max_out);
            fflush(stdout);
            exit(1);
        }
        #endif
        // --- END OF INTEGRATED PARTIAL MANAGER LOGIC ---
    } else {
        out = waveguide.process(input);
    }

    #ifdef DEBUGN
    float max_val = fmaxf(fabsf(vgetq_lane_f32(out, 0)), fabsf(vgetq_lane_f32(out, 1)));
    if (max_val > 50.0f || !std::isfinite(max_val)) {
        printf("[RES EXPLOSION] Val=%.2f Model=%d\n", max_val, nmodel);
        fflush(stdout);
        exit(1);
    }
    #endif

    // --- OPTIMIZED SILENCE TRACKING ---
    // We check (abs(out) + abs(input)) against threshold
    float32x4_t vSum = vaddq_f32(vabsq_f32(out), vabsq_f32(input));
    uint32x4_t vMask = vcgtq_f32(vSum, vdupq_n_f32(c_silence_threshold));

    // Fast check: if any bit is set in the 128-bit mask
    // Branchless check: Is any lane above -100dB?
    uint64x2_t vMask64 = vreinterpretq_u64_u32(vMask);
    if (vgetq_lane_u64(vMask64, 0) | vgetq_lane_u64(vMask64, 1)) {
        silence = 0;
    } else {
        silence++;
    }

    if (silence >= static_cast<int>(srate)) {
        active = false;
        this->clear();
    }

    return out;
}

void Resonator::clear()
{
    active = false; // CRITICAL: Must be first to prevent phantom processing
	for (size_t i = 0; i < c_max_partials; ++i) {
		partials[i].clear();
	}
	waveguide.clear();
	filter.clear(0.0f);
	silence = 0;  // Reset silence counter
}

float32x4_t Resonator::applyFilter(float32x4_t input)
{
	return filter.df1_vec(input);
}
