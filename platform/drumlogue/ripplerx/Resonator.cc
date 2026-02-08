#ifdef __cplusplus
extern "C" {
#endif
#include <stddef.h>
#ifdef __cplusplus
}
#endif
#include "Resonator.h"
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
 * @param freq
 * @param vel
 * @param isRelease
 * @param model
 */
void Resonator::update(float32_t freq, float32_t vel, bool isRelease, float32_t model[c_max_partials])
{
    if (nmodel >= OpenTube) {
        waveguide.update(model[0] * freq, vel, isRelease);
        return;
    }

    // --- Pre-calculate Shared Constants for all Partials ---
    const float inv_srate_2pi = M_TWOPI / srate;
    const float log_vel = vel * M_TWOLN100;

    // Shared ratio_max for the entire model
    const float ratio_max = model[c_max_partials - 1];
    const float f_nyq = c_nyquist_factor * srate;

    for (int p = 0; p < npartials; ++p) {
        Partial& part = partials[p];
        int idx = part.k - 1;

        if (idx < 0 || (uint32_t)idx >= c_max_partials) continue;

        float32_t ratio = model[idx];

        // 1. Inharmonicity (using the bit-trick for 2^x)
        // Accessing vel_inharm from the specific partial
        float exp_inharm_part = (part.vel_inharm * log_vel) * M_LOG2_E;
        union { float f; int32_t i; } u_inharm;
        u_inharm.i = (int32_t)(exp_inharm_part * 8388608.0f) + 1065353216;

        float inharm_k = fminf(1.0f, part.inharm * u_inharm.f) - 0.0001f;
        float r_m_1 = ratio - 1.0f;
        inharm_k = fasterSqrt(1.0f + inharm_k * (r_m_1 * r_m_1));

        float f_k = freq * ratio * inharm_k;

        // 2. Decay
        // Accessing vel_decay from the specific partial
        float exp_decay_part = (part.vel_decay * log_vel) * M_LOG2_E;
        union { float f; int32_t i; } u_decay;
        u_decay.i = (int32_t)(exp_decay_part * 8388608.0f) + 1065353216;

        float d_k = fminf(100.0f, part.decay * u_decay.f);
        if (isRelease) d_k *= part.rel;

        // Range Check & Early Exit
        if (f_k >= f_nyq || f_k < c_freq_min || d_k < c_decay_min) {
            part.vb0 = part.vb2 = part.va1 = part.va2 = vdupq_n_f32(0.0f);
            continue;
        }

        // 3. Damping and Tone
        float f_max = fminf(c_freq_max, freq * ratio_max * inharm_k);
        float d_base = (part.damp <= 0 ? freq : f_max) / f_k;
        float d_mod = e_expff(fasterlogf(d_base) * (part.damp * 2.0f));
        d_k /= d_mod;

        float t_base = (part.tone <= 0 ? f_k / freq : f_k / f_max);
        float t_gain = e_expff(fasterlogf(t_base) * (part.tone * 2.0f));

        // 4. Hit modulation
        float h_mod = fminf(0.5f, part.hit + vel * part.vel_hit * 0.5f);
        float a_k = 35.0f * fabsf(fastersinfullf(M_PI * (float)part.k * h_mod));

        // 5. Coefficients & Pre-Normalization
        float omega = f_k * inv_srate_2pi;
        float b0_val = inv_srate_2pi * t_gain * a_k;
        float inv_decay = inv_srate_2pi / d_k;

        float inv_a0 = 1.0f / (1.0f + inv_decay);

        // Pre-normalized coefficients directly into NEON registers
        part.vb0 = vdupq_n_f32(b0_val * inv_a0);
        part.vb2 = vdupq_n_f32(-b0_val * inv_a0);
        part.va1 = vdupq_n_f32(-2.0f * fastercosfullf(omega) * inv_a0);
        part.va2 = vdupq_n_f32((1.0f - inv_decay) * inv_a0);
    }
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

            // Load state (Lower 2 lanes hold [L_prev, R_prev])
            float32x2_t x1_prev = vget_low_f32(part.vx1);
            float32x2_t x2_prev = vget_low_f32(part.vx2);
            float32x2_t y1_prev = vget_low_f32(part.vy1);
            float32x2_t y2_prev = vget_low_f32(part.vy2);

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

            // --- Frame 1 (L1, R1) ---
            // x[n]=in1, x[n-2]=x1_prev, y[n-1]=out0, y[n-2]=y1_prev
            float32x2_t term1_1 = vmul_f32(in1, b0);
            float32x2_t term2_1 = vmul_f32(x1_prev, b2);
            float32x2_t term3_1 = vmul_f32(out0, a1);
            float32x2_t term4_1 = vmul_f32(y1_prev, a2);

            float32x2_t out1 = vsub_f32(vadd_f32(term1_1, term2_1), vadd_f32(term3_1, term4_1));

            // --- Update State ---
            part.vx1 = vcombine_f32(in1, in1);
            part.vx2 = vcombine_f32(in0, in0);
            part.vy1 = vcombine_f32(out1, out1);
            part.vy2 = vcombine_f32(out0, out0);

            // Accumulate partial result into resonator output
            float32x4_t p_out = vcombine_f32(out0, out1);
            out = vaddq_f32(out, p_out);
        }
        // --- END OF INTEGRATED PARTIAL MANAGER LOGIC ---
    } else {
        out = waveguide.process(input);
    }

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
    active = false;
	for (size_t i = 0; i < c_max_partials; ++i) {
		partials[i].clear();
	}
	waveguide.clear();
	filter.clear(0.0f);
	silence = 0;  // Reset silence counter
}

float32x4_t Resonator::applyFilter(float32x4_t input)
{
	// Extract individual lanes, apply scalar filter, and repack
	// This is necessary because Filter::df1() operates on scalar samples
	float32_t lane0 = vgetq_lane_f32(input, 0);
	float32_t lane1 = vgetq_lane_f32(input, 1);
	float32_t lane2 = vgetq_lane_f32(input, 2);
	float32_t lane3 = vgetq_lane_f32(input, 3);

	lane0 = filter.df1(lane0);
	lane1 = filter.df1(lane1);
	lane2 = filter.df1(lane2);
	lane3 = filter.df1(lane3);

	// Use vsetq_lane to reconstruct the vector properly
	float32x4_t result = vdupq_n_f32(0.0f);
	result = vsetq_lane_f32(lane0, result, 0);
	result = vsetq_lane_f32(lane1, result, 1);
	result = vsetq_lane_f32(lane2, result, 2);
	result = vsetq_lane_f32(lane3, result, 3);

	return result;
}
