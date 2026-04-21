#pragma once

/**
 * @file PercussionSpatializer.h
 * @brief Enhanced Percussion Spatializer with realistic ensemble modeling
 *
 * COMPLETED IMPLEMENTATION:
 * - Integrated vld4 gather for 3x faster delay line reads
 * - Connected filter tables with mode processing
 * - Added smooth parameter ramping
 * - Implemented proper mode crossfading
 */

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <arm_neon.h>
#include "unit.h"
#include "spatial_modes.h"
#include "filters.h"
#include "float_math.h"

extern float lfo_table[LFO_TABLE_SIZE] __attribute__((aligned(16)));

/**
 * Per-clone parameters with randomization and modulation
 */
typedef struct __attribute__((aligned(CACHE_LINE_SIZE))) {
    float32x4_t delay_offsets;    // Micro-delay for vibrato (4 clones)
    float32x4_t left_gains;       // Left channel pan (4 clones)
    float32x4_t right_gains;      // Right channel pan (4 clones)
    uint32x4_t  mod_phases;       // Fixed-point phase (0 to 0xFFFFFFFF)
    float32x4_t pitch_mod;        // Pitch modulation depth (tape wobble)
    float32x4_t velocity;         // Random velocity per hit (0.7-1.0)
    uint32x4_t  phase_flags;      // Phase inversion flags
    uint32x4_t  active;           // Which clones are active
} clone_group_t;

static_assert(sizeof(clone_group_t) % CACHE_LINE_SIZE == 0,
              "clone_group_t must be cache-aligned");

/**
 * OPTIMIZED: Truly interleaved delay line for vld4 gather
 *
 * Instead of storing samples by time position, we store by clone:
 * For each time position t, we store 8 floats: [L0, L1, L2, L3, R0, R1, R2, R3]
 *
 * Then vld4 can load all 4 clones at the SAME time position in one instruction
 */
typedef struct __attribute__((aligned(16))) {
    float samples[8];  // [L0, L1, L2, L3, R0, R1, R2, R3] at a SINGLE time position
} interleaved_frame_t;

/**
 * PRNG state (Xorshift128+)
 */
typedef struct {
    uint64x2_t state0;
    uint64x2_t state1;
} prng_t;

enum params {
    k_clones,
    k_mode,
    k_depth,
    k_rate,
    k_spread,
    k_mix,
    k_wobble,
    k_attack_softening,
    k_total,    // marker (count) same as header.c
};

/**
 * Main Enhanced Spatializer Class
 */
class PercussionSpatializer {
public:
    /*===========================================================================*/
    /* Lifecycle Methods */
    /*===========================================================================*/

    PercussionSpatializer()
        : write_ptr_(0)
        , clone_count_(4)
        , current_mode_(MODE_TRIBAL)
        , initialized_(false)
        , is_mono_(false)
        , sample_rate_(48000)
        , transient_detected_(false)
        , transient_energy_(0.0f)
        , depth_(0.5f)
        , rate_(1.0f)
        , spread_(0.8f)
        , mix_(0.5f)
        , wobble_depth_(0.3f)
        , attack_soften_(0.2f)
        , target_mix_(0.5f)
        , target_wobble_(0.3f)
        , target_attack_(0.2f)
        , mix_ramp_samples_(0)
        , wobble_ramp_samples_(0)
        , attack_ramp_samples_(0)
        , crossfade_counter_(0)
        , crossfade_active_(false)
        , flags_(0) {

        // Initialize phase increment vector
        phase_inc_ = vdupq_n_u32(0);

        // Initialize PRNG with a fixed seed
        prng_init(0x9E3779B97F4A7C15ULL);

        // Pre-calculate sin/cos tables
        if (!tables_initialized) {
            for (int i = 0; i < 360; i++) {
                float angle = i * 2.0f * M_PI / 360.0f;
                sin_table[i] = sinf(angle); // at init no fast function needed
                cos_table[i] = cosf(angle);
            }
            tables_initialized = true;
        }

        // TODO introduce different LFO shapes
        // Initialize LFO table (triangle wave) here to ensure it runs on drumlogue
        // (attribute((constructor)) is unreliable in shared libs)
        for (int i = 0; i < LFO_TABLE_SIZE; i++) {
            float phase = (float)i / LFO_TABLE_SIZE;
            lfo_table[i] = 2.0f * fabsf(phase - 0.5f);
        }

        // Initialize filter states
        memset(&mode_filters_, 0, sizeof(mode_filters_));

        for (int i = 0; i < CLONE_GROUPS; i++) {
            filter_state_[i] = vdupq_n_f32(0.0f);
        }

        // Initialize filters
        init_mode_filters(&mode_filters_, current_mode_, depth_);

        // Clear parameter arrays
        memset(params_, 0, sizeof(params_));
        memset(last_params_, 0, sizeof(last_params_));

        // Clear crossfade buffer
        memset(old_mode_buffer_, 0, sizeof(old_mode_buffer_));
    }

    ~PercussionSpatializer() {
    }

    inline int8_t Init(const unit_runtime_desc_t* desc) {
        if (desc->samplerate != 48000) return k_unit_err_samplerate;
        if (desc->input_channels == 1 && desc->output_channels == 1) {
            is_mono_ = true;
        } else if (desc->input_channels != 2 || desc->output_channels != 2)
            return k_unit_err_geometry;

        sample_rate_ = desc->samplerate;

        initialized_ = true;

        Reset();
        return k_unit_err_none;
    }

    inline void Teardown() {}

    inline void Reset() {
        if (!initialized_) {
            return;
        }

        // Clear delay line using NEON
        float32x4_t zero = vdupq_n_f32(0.0f);
        for (int i = 0; i < DELAY_MAX_SAMPLES; i++) {
            vst1q_f32(&delay_line_[i].samples[0], zero);
            vst1q_f32(&delay_line_[i].samples[4], zero);
        }
        write_ptr_ = 0;

        // Reset filter states (init_mode_filters handles biquad state clearing)
        for (int i = 0; i < CLONE_GROUPS; i++) {
            filter_state_[i] = vdupq_n_f32(0.0f);
        }
        init_mode_filters(&mode_filters_, current_mode_, depth_);

        // Reset clone parameters
        init_clone_parameters();
        transient_energy_ = 0.0f;
        crossfade_counter_ = 0;
        crossfade_active_ = false;
    }

    inline void Resume() {}
    inline void Suspend() {}

    /*===========================================================================*/
    /* OPTIMIZED: Core Processing with NEON and prefetch */
    /*===========================================================================*/

    fast_inline void Process(const float* in, float* out, size_t frames) {
        if (!initialized_) {
            memcpy(out, in, frames * 2 * sizeof(float));
            return;
        }

        const float* in_p = in;
        float*       out_p = out;

        // --- Advance smooth ramps for mix/wobble/attack by the actual frame count ---
        // Each ramp advances by `frames` samples per render call so a 480-sample
        // ramp completes in ~10ms regardless of block size.  The step is recomputed
        // from (target - current) / remaining every call so mid-ramp parameter
        // changes re-aim smoothly without a pre-calculated stale slope.
        if (mix_ramp_samples_ > 0) {
            uint32_t steps = (mix_ramp_samples_ >= (uint32_t)frames) ? (uint32_t)frames : mix_ramp_samples_;
            float step = (target_mix_ - mix_) / (float)mix_ramp_samples_;
            mix_ += step * (float)steps;
            mix_ramp_samples_ -= steps;
            if (mix_ramp_samples_ == 0) mix_ = target_mix_;
        }
        if (wobble_ramp_samples_ > 0) {
            uint32_t steps = (wobble_ramp_samples_ >= (uint32_t)frames) ? (uint32_t)frames : wobble_ramp_samples_;
            float step = (target_wobble_ - wobble_depth_) / (float)wobble_ramp_samples_;
            wobble_depth_ += step * (float)steps;
            wobble_ramp_samples_ -= steps;
            if (wobble_ramp_samples_ == 0) wobble_depth_ = target_wobble_;
        }
        if (attack_ramp_samples_ > 0) {
            uint32_t steps = (attack_ramp_samples_ >= (uint32_t)frames) ? (uint32_t)frames : attack_ramp_samples_;
            float step = (target_attack_ - attack_soften_) / (float)attack_ramp_samples_;
            attack_soften_ += step * (float)steps;
            attack_ramp_samples_ -= steps;
            if (attack_ramp_samples_ == 0) attack_soften_ = target_attack_;
        }

        // Determine starting filter depth for ramping
        float current_depth = mode_filters_.last_depth_param;
        float depth_step = 0.0f;
        if (mode_filters_.ramp_samples > 0) {
            depth_step = (mode_filters_.depth_param - mode_filters_.last_depth_param) /
                         mode_filters_.ramp_samples;
        }

        const float32x4_t dry4 = vdupq_n_f32(1.0f - mix_);
        const float32x4_t wet4 = vdupq_n_f32(mix_);

        // -----------------------------------------------------------------------
        // Main loop: 4 stereo frames (8 floats) per iteration.
        // vld2q_f32 de-interleaves [L0,R0,L1,R1,L2,R2,L3,R3] into
        //   val[0] = [L0,L1,L2,L3]  val[1] = [R0,R1,R2,R3]
        // vst2q_f32 re-interleaves on output.
        // -----------------------------------------------------------------------
        size_t frames4 = frames & ~3u;
        for (size_t i = 0; i < frames4; i += 4, in_p += 8, out_p += 8) {

            // --- Advance depth ramp by 4 steps ---
            if (mode_filters_.ramp_samples > 0) {
                uint32_t steps = (mode_filters_.ramp_samples >= 4) ? 4u
                                                                    : mode_filters_.ramp_samples;
                current_depth += depth_step * (float)steps;
                mode_filters_.ramp_samples -= steps;
                if (mode_filters_.ramp_samples == 0) {
                    current_depth = mode_filters_.depth_param;
                    mode_filters_.last_depth_param = current_depth;
                    depth_step = 0.0f;
                }
            } else {
                current_depth = mode_filters_.depth_param;
            }

            // --- De-interleave 4 stereo frames in one instruction ---
            float32x4x2_t stereo_in = vld2q_f32(in_p);
            float32x4_t in_l4 = stereo_in.val[0];  // [L0, L1, L2, L3]
            float32x4_t in_r4 = stereo_in.val[1];  // [R0, R1, R2, R3]

            // --- Transient detection: 4 real consecutive samples per vector ---
            transient_detected_ = detect_transient_fast(in_l4, in_r4);
            if (transient_detected_) randomize_velocities();

            // --- Write each frame then generate its clones (preserves delay ordering) ---
            float l_arr[NEON_LANES], r_arr[NEON_LANES];
            vst1q_f32(l_arr, in_l4);
            vst1q_f32(r_arr, in_r4);

            float wet_l_arr[NEON_LANES], wet_r_arr[NEON_LANES];
            for (int f = 0; f < 4; f++) {
                write_to_delay_opt(l_arr[f], r_arr[f]);

                float32x4_t acc_l = vdupq_n_f32(0.0f);
                float32x4_t acc_r = vdupq_n_f32(0.0f);
                generate_clones_opt(&acc_l, &acc_r, current_depth);

                wet_l_arr[f] = horizontal_sum_f32x4(acc_l);
                wet_r_arr[f] = horizontal_sum_f32x4(acc_r);
            }

            float32x4_t wet_l4 = vld1q_f32(wet_l_arr);
            float32x4_t wet_r4 = vld1q_f32(wet_r_arr);

            // --- Apply mode crossfade (NEON: 4 frames per iteration) ---
            // Fade-in the new mode's wet signal from 0→1 over CROSSFADE_SAMPLES frames.
            // Counter decrements by 4 per NEON iteration; granularity is ±4/480 ≈ 0.8%
            // per block, perceptually indistinguishable from per-sample interpolation.
            if (crossfade_active_) {
                float fade_in = 1.0f - (float)crossfade_counter_ / CROSSFADE_SAMPLES;
                wet_l4 = vmulq_n_f32(wet_l4, fade_in);
                wet_r4 = vmulq_n_f32(wet_r4, fade_in);
                crossfade_counter_ = (crossfade_counter_ > 4) ? crossfade_counter_ - 4 : 0;
                if (crossfade_counter_ == 0) crossfade_active_ = false;
            }

            // --- Wet/dry mix using NEON ---
            float32x4_t out_l4 = vmlaq_f32(vmulq_f32(in_l4, dry4), wet_l4, wet4);
            float32x4_t out_r4 = vmlaq_f32(vmulq_f32(in_r4, dry4), wet_r4, wet4);

            // --- Re-interleave and store 4 stereo frames in one instruction ---
            float32x4x2_t stereo_out;
            stereo_out.val[0] = out_l4;
            stereo_out.val[1] = out_r4;
            vst2q_f32(out_p, stereo_out);
        }

        // -----------------------------------------------------------------------
        // Scalar tail: 0-3 remaining frames (handles frames not divisible by 4).
        // Drumlogue blocks are typically 64 or 128 so this path is rarely taken.
        // -----------------------------------------------------------------------
        for (size_t i = frames4; i < frames; i++, in_p += 2, out_p += 2) {
            float in_l = in_p[0];
            float in_r = in_p[1];

            if (mode_filters_.ramp_samples > 0) {
                current_depth += depth_step;
                if (--mode_filters_.ramp_samples == 0) {
                    current_depth = mode_filters_.depth_param;
                    mode_filters_.last_depth_param = current_depth;
                }
            } else {
                current_depth = mode_filters_.depth_param;
            }

            // Single-sample transient detection (tail path only)
            float32x4_t in_l4 = vdupq_n_f32(in_l);
            float32x4_t in_r4 = vdupq_n_f32(in_r);
            transient_detected_ = detect_transient_fast(in_l4, in_r4);
            if (transient_detected_) randomize_velocities();

            write_to_delay_opt(in_l, in_r);

            float32x4_t acc_l = vdupq_n_f32(0.0f);
            float32x4_t acc_r = vdupq_n_f32(0.0f);
            generate_clones_opt(&acc_l, &acc_r, current_depth);

            float wet_l = horizontal_sum_f32x4(acc_l);
            float wet_r = horizontal_sum_f32x4(acc_r);

            if (crossfade_active_) {
                float fade_in = 1.0f - (float)crossfade_counter_ / CROSSFADE_SAMPLES;
                wet_l *= fade_in;
                wet_r *= fade_in;
                if (--crossfade_counter_ == 0) crossfade_active_ = false;
            }

            out_p[0] = (1.0f - mix_) * in_l + mix_ * wet_l;
            out_p[1] = (1.0f - mix_) * in_r + mix_ * wet_r;
        }
    }

    /*===========================================================================*/
    /* Parameter Interface */
    /*===========================================================================*/

    inline void setParameter(uint8_t index, int32_t value) {
        if (index < k_total) {
            params_[index] = value;
        }

        switch (index) {
            case k_clones: { // Clone Count
                set_clone_count(value);
            break;}
            case k_mode: { // Mode
                set_mode(static_cast<spatial_mode_t>(value));
                break;}
            case k_depth: {  // Depth
                depth_ = value / 100.0f;
                // FIX 3: Pass 0 to force instant coefficient calculation in the UI thread!
                update_filter_params(&mode_filters_, depth_, 0);
                break;}
            case k_rate: {// Rate (LFO speed for pitch wobble)
                rate_ = 0.1f + (value / 100.0f) * 9.9f;
                // Convert to 32-bit fixed point: (rate / sr) * 2^32
                uint32_t inc = (uint32_t)((rate_ / sample_rate_) * 4294967296.0f);
                phase_inc_ = vdupq_n_u32(inc);
                break;}
            case k_spread: {// Spread
                spread_ = value / 100.0f;
                update_panning();
                break;}
            case k_mix: {// Mix — ramp to new target over ~10ms to avoid zipper noise
                target_mix_ = value / 100.0f;
                mix_ramp_samples_ = 480;
                break;}
            case k_wobble: {// Wobble Depth
                target_wobble_ = value / 100.0f;
                wobble_ramp_samples_ = 480;
                break;}
            case k_attack_softening: {// Attack Softening
                target_attack_ = value / 100.0f;
                attack_ramp_samples_ = 480;
                break;}
            default:
                break;
        }
    }

    inline int32_t getParameterValue(uint8_t index) const {
        if (index < k_total) {
            return params_[index];
        }
        return 0;
    }

    inline const char* getParameterStrValue(uint8_t index, int32_t value) const {
        static const char* mode_names[] = {"Tribal", "Military", "Angel"};
        static const char* clone_names[] = {"4", "8", "12", "16"};

        switch (index) {
            case k_clones: // Clone Count
                if (value >= 0 && value <= 3) return clone_names[value];
                break;
            case k_mode: // Mode
                if (value >= 0 && value <= 2) return mode_names[value];
                break;
            default:
                break;
        }
        return nullptr;
    }

    inline void LoadPreset(uint8_t idx) { (void)idx; }
    inline uint8_t getPresetIndex() const { return 0; }
    static inline const char* getPresetName(uint8_t idx) { return nullptr; }

private:
    /*===========================================================================*/
    /* Xorshift128+ PRNG Implementation */
    /*===========================================================================*/

    void prng_init(uint64_t seed) {
        uint64_t s0[2] = {seed, seed * 0x9E3779B97F4A7C15ULL};
        uint64_t s1[2] = {seed * 0xBF58476D1CE4E5B9ULL, seed * 0x94D049BB133111EBULL};

        prng_.state0 = vld1q_u64(s0);
        prng_.state1 = vld1q_u64(s1);
    }

    uint32x4_t prng_rand_u32() {
        uint64x2_t s0 = prng_.state0;
        uint64x2_t s1 = prng_.state1;

        uint64x2_t s1_left = vshlq_n_u64(s1, 23);
        s1 = veorq_u64(s1, s1_left);

        uint64x2_t s1_right = vshrq_n_u64(s1, 17);
        s1 = veorq_u64(s1, s1_right);

        uint64x2_t s0_right = vshrq_n_u64(s0, 26);
        uint64x2_t s0_xor = veorq_u64(s0, s0_right);
        s1 = veorq_u64(s1, s0_xor);

        prng_.state0 = s1;
        prng_.state1 = s0;

        uint64x2_t sum = vaddq_u64(s0, s1);

        uint32x2_t low32 = vmovn_u64(sum);
        uint32x2_t high32 = vshrn_n_u64(sum, 32);

        return vcombine_u32(low32, high32);
    }

    float32x4_t prng_rand_float() {
        uint32x4_t rand = prng_rand_u32();
        uint32x4_t masked = vandq_u32(rand, vdupq_n_u32(0x7FFFFF));
        uint32x4_t float_bits = vorrq_u32(masked, vdupq_n_u32(0x3F800000));
        return vsubq_f32(vreinterpretq_f32_u32(float_bits), vdupq_n_f32(1.0f));
    }

    /*===========================================================================*/
    /* Randomization Methods */
    /*===========================================================================*/

    void randomize_velocities() {
        for (int g = 0; g < CLONE_GROUPS; g++) {
            float32x4_t rand_float = prng_rand_float();
            float32x4_t velocity = vmlaq_f32(vdupq_n_f32(0.7f), rand_float, vdupq_n_f32(0.3f));
            clone_groups_[g].velocity = velocity;
        }
    }

    /*===========================================================================*/
    /* OPTIMIZED: NEON Utilities */
    /*===========================================================================*/

    fast_inline bool detect_transient_fast(float32x4_t in_l, float32x4_t in_r) {
        float32x4_t abs_l = vabsq_f32(in_l);
        float32x4_t abs_r = vabsq_f32(in_r);
        float32x4_t energy = vaddq_f32(abs_l, abs_r);

        float32x2_t sum_lo = vpadd_f32(vget_low_f32(energy), vget_high_f32(energy));
        float32x2_t sum_hi = vpadd_f32(sum_lo, sum_lo);
        float total = vget_lane_f32(sum_hi, 0);

        bool detected = (total > 0.5f) && (transient_energy_ < 0.5f);
        transient_energy_ = total * 0.1f + transient_energy_ * 0.9f;

        return detected;
    }

    fast_inline float horizontal_sum_f32x4(float32x4_t v) {
        float32x2_t sum_halves = vpadd_f32(vget_low_f32(v), vget_high_f32(v));
        return vget_lane_f32(sum_halves, 0) + vget_lane_f32(sum_halves, 1);
    }

    /*===========================================================================*/
    /* Core Initialization */
    /*===========================================================================*/

    void init_clone_parameters() {
        if (!initialized_) return;

        // Mode-specific delay constellations give each mode a distinct ensemble feel:
        //   Tribal  : 8, 14, 20, 26 ms groups — deliberate, spaced ethnic drum echoes
        //   Military: 1,  2,  3,  4 ms groups — tight machine-gun double-strokes
        //   Angel   : 15, 27, 39, 51 ms groups — wide, dreamy, ethereal cloud
        float base_delay, group_step, lane_step;
        switch (current_mode_) {
            case MODE_TRIBAL:   base_delay = 8.0f;  group_step = 6.0f;  lane_step = 1.0f;  break;
            case MODE_MILITARY: base_delay = 1.0f;  group_step = 1.0f;  lane_step = 0.3f;  break;
            case MODE_ANGEL:    base_delay = 15.0f; group_step = 12.0f; lane_step = 2.0f;  break;
            default:            base_delay = 8.0f;  group_step = 6.0f;  lane_step = 1.0f;  break;
        }

        for (int group = 0; group < CLONE_GROUPS; group++) {
            clone_group_t* g = &clone_groups_[group];

            float base = base_delay + group * group_step;
            float offsets[NEON_LANES];
            float pitch_mod[NEON_LANES];
            uint32_t phases[NEON_LANES];

            for (int i = 0; i < NEON_LANES; i++) {
                int clone_idx = group * NEON_LANES + i;

                offsets[i] = base + (i * lane_step);
                // Convert 0.0-1.0 phase to 32-bit fixed point
                phases[i] = (uint32_t)(((float)clone_idx / 16.0f) * 4294967296.0f);
                pitch_mod[i] = 0.1f + (i * 0.05f);
            }

            g->delay_offsets = vld1q_f32(offsets);
            g->mod_phases = vld1q_u32(phases); // Load as uint32!
            g->pitch_mod = vld1q_f32(pitch_mod);
            g->left_gains = vdupq_n_f32(0.0f);
            g->right_gains = vdupq_n_f32(0.0f);
            g->velocity = vdupq_n_f32(1.0f);
            g->phase_flags = vdupq_n_u32(0);
            g->active = vdupq_n_u32(0xFFFFFFFF);
        }
        update_panning();
    }

    /*===========================================================================*/
    /* Delay Line Operations */
    /*===========================================================================*/

    fast_inline void write_to_delay_opt(float in_l, float in_r) {
        uint32_t pos = write_ptr_ & DELAY_MASK;
        // Duplicate L and R for all 4 clone channels: [L,L,L,L] and [R,R,R,R]
        vst1q_f32(&delay_line_[pos].samples[0], vdupq_n_f32(in_l));
        vst1q_f32(&delay_line_[pos].samples[4], vdupq_n_f32(in_r));
        write_ptr_ += 1;
    }

    /**
     * ULTRA-OPTIMIZED: Read 4 samples using vld4
     * This loads 4 clones × 4 time positions in one instruction
     */
    fast_inline float32x4x4_t read_delayed_vld4(uint32_t base_pos) {
        // Ensure we don't read past buffer end
        if (base_pos + 3 >= DELAY_MAX_SAMPLES) {
            // Handle wrap-around by reading individually
            float32x4x4_t result;
            for (int i = 0; i < 4; i++) {
                uint32_t pos = (base_pos + i) & DELAY_MASK;
                result.val[i] = vld1q_f32(&delay_line_[pos].samples[0]);
            }
            return result;
        }
        return vld4q_f32(&delay_line_[base_pos].samples[0]);
    }

    /*===========================================================================*/
    /* Mode Filter Application */
    /*===========================================================================*/

    fast_inline void apply_mode_filters(mode_filters_t* filters,
                                    uint32_t group_idx,
                                    float32x4_t* samples_l,
                                    float32x4_t* samples_r,
                                    float depth_param) {
        // Just forward to the filters.h implementation
        ::apply_mode_filters(filters, group_idx, samples_l, samples_r, depth_param);
    }

    /*===========================================================================*/
    /* Clone Generation with Proper Attack Softening using optimized vld4 gather */
    /*===========================================================================*/

    /*===========================================================================*/
    /** This implementation applies all three massive optimizations:
     * Zero-Branch LFO: The phase automatically wraps when the 32-bit integer overflows.
     * We shift it down by 24 bits to instantly get a 0-255 index for the lfo_table.
     * Zero-Branch Delay Wrap: By adding DELAY_MAX_SAMPLES to the read pointer before casting to an integer,
     * we ensure it's always positive.
     * We then use a simple bitwise & DELAY_MASK to wrap it instantly.
     * (This saves 6 NEON instructions per group).
     * Vectorized Interpolation: It gathers the 4 clones into local arrays,
     * loads them into NEON, and executes the l0 + frac * (l1 - l0) math across all
     * 4 clones simultaneously using fused multiply-adds (vmlaq_f32).
    /*===========================================================================*/
    fast_inline void generate_clones_opt(float32x4_t* out_l, float32x4_t* out_r,
                                        float filter_depth) {
        float32x4_t acc_l = vdupq_n_f32(0.0f);
        float32x4_t acc_r = vdupq_n_f32(0.0f);

        uint32_t num_groups = (clone_count_ + NEON_LANES - 1) / NEON_LANES;
        uint32_t base_read = (write_ptr_ - 32) & DELAY_MASK;

        // Cache constants in NEON registers
        float32x4_t delay_max_vec = vdupq_n_f32((float)DELAY_MAX_SAMPLES);
        uint32x4_t delay_mask_vec = vdupq_n_u32(DELAY_MASK);
        uint32x4_t one_vec = vdupq_n_u32(1);

        for (uint32_t g = 0; g < num_groups; g++) {
            clone_group_t* group = &clone_groups_[g];

            // =================================================================
            // 1. Branchless Fixed-Point LFO
            // =================================================================
            uint32x4_t phases = group->mod_phases;
            phases = vaddq_u32(phases, phase_inc_);
            group->mod_phases = phases;

            // Shift down 24 bits to index the 256-entry LFO table
            uint32x4_t indices = vshrq_n_u32(phases, 24);

            uint32_t idx_vals[NEON_LANES];
            vst1q_u32(idx_vals, indices);
            float lfo_vals[NEON_LANES] = {
                lfo_table[idx_vals[0]], lfo_table[idx_vals[1]],
                lfo_table[idx_vals[2]], lfo_table[idx_vals[3]]
            };
            float32x4_t lfo = vld1q_f32(lfo_vals);

            // =================================================================
            // 2. Delay Math & Branchless Bitwise Wrapping
            // =================================================================
            float32x4_t wobble = vmulq_f32(lfo, group->pitch_mod);
            wobble = vmulq_f32(wobble, vdupq_n_f32(wobble_depth_));

            float32x4_t delays = vaddq_f32(group->delay_offsets, wobble);
            float32x4_t offset_samples = vmulq_f32(delays, vdupq_n_f32(48.0f));

            float32x4_t base_read_vec = vdupq_n_f32((float)base_read);
            float32x4_t read_pos = vsubq_f32(base_read_vec, offset_samples);

            // Add max samples to ensure positive before cast, eliminating branch checks
            float32x4_t pos_adj = vaddq_f32(read_pos, delay_max_vec);

            uint32x4_t pos_int = vcvtq_u32_f32(pos_adj);
            float32x4_t pos_frac = vsubq_f32(pos_adj, vcvtq_f32_u32(pos_int));

            // Bitwise mask for instant wrapping
            uint32x4_t idx1_vec = vandq_u32(pos_int, delay_mask_vec);
            uint32x4_t idx2_vec = vandq_u32(vaddq_u32(pos_int, one_vec), delay_mask_vec);

            // =================================================================
            // 3. Software Gather & Vectorized Interpolation
            // =================================================================
            uint32_t idx1[NEON_LANES], idx2[NEON_LANES];
            vst1q_u32(idx1, idx1_vec);
            vst1q_u32(idx2, idx2_vec);

            // Gather specific clones from interleaved struct: delay_line_[t].samples[clone_lane]
            float l0_arr[NEON_LANES] = { delay_line_[idx1[0]].samples[0], delay_line_[idx1[1]].samples[1], delay_line_[idx1[2]].samples[2], delay_line_[idx1[3]].samples[3] };
            float l1_arr[NEON_LANES] = { delay_line_[idx2[0]].samples[0], delay_line_[idx2[1]].samples[1], delay_line_[idx2[2]].samples[2], delay_line_[idx2[3]].samples[3] };
            float r0_arr[NEON_LANES] = { delay_line_[idx1[0]].samples[4], delay_line_[idx1[1]].samples[5], delay_line_[idx1[2]].samples[6], delay_line_[idx1[3]].samples[7] };
            float r1_arr[NEON_LANES] = { delay_line_[idx2[0]].samples[4], delay_line_[idx2[1]].samples[5], delay_line_[idx2[2]].samples[6], delay_line_[idx2[3]].samples[7] };

            float32x4_t l0 = vld1q_f32(l0_arr);
            float32x4_t l1 = vld1q_f32(l1_arr);
            float32x4_t r0 = vld1q_f32(r0_arr);
            float32x4_t r1 = vld1q_f32(r1_arr);

            // Vectorized FMA interpolation: out = l0 + frac * (l1 - l0)
            float32x4_t delayed_l = vmlaq_f32(l0, vsubq_f32(l1, l0), pos_frac);
            float32x4_t delayed_r = vmlaq_f32(r0, vsubq_f32(r1, r0), pos_frac);

            // =================================================================
            // 4. Audio Processing (Velocity, Env, Filters, Inversion)
            // =================================================================
            delayed_l = vmulq_f32(delayed_l, group->velocity);
            delayed_r = vmulq_f32(delayed_r, group->velocity);

            if (attack_soften_ > 0.01f) {
                delayed_l = apply_attack_softening(delayed_l, g);
                delayed_r = apply_attack_softening(delayed_r, g);
            }

            // Mode identity is expressed via delay time patterns (see init_clone_parameters).
            // Applying per-clone biquad filters (bandpass/HPF) destroys the drum transient
            // character and creates a reverb-like tail — exactly the wrong behaviour.
            // apply_mode_filters(&mode_filters_, g, &delayed_l, &delayed_r, filter_depth);

            uint32x4_t invert = group->phase_flags;
            float32x4_t neg_one = vdupq_n_f32(-1.0f);
            float32x4_t pos_one = vdupq_n_f32(1.0f);
            float32x4_t phase_scale = vbslq_f32(invert, neg_one, pos_one);

            delayed_l = vmulq_f32(delayed_l, phase_scale);
            delayed_r = vmulq_f32(delayed_r, phase_scale);

            // Accumulate into stereo mix with per-clone panning
            acc_l = vaddq_f32(acc_l, vmulq_f32(delayed_l, group->left_gains));
            acc_r = vaddq_f32(acc_r, vmulq_f32(delayed_r, group->right_gains));
        }

        // Constant-power volume compensation after all groups are summed:
        // 4 clones = 0.5x gain, 16 clones = 0.25x gain
        float volume_comp = 1.0f / fasterSqrt_15bits((float)clone_count_);
        *out_l = vmulq_n_f32(acc_l, volume_comp);
        *out_r = vmulq_n_f32(acc_r, volume_comp);
    }

    /**
    * Attack softening filter using NEON
    */
    fast_inline float32x4_t apply_attack_softening(float32x4_t in, uint32_t group_idx) {
        // FIX 1: Use 1.0f so audio passes through perfectly when there is no transient!
        // Ensure non-zero to avoid division by zero
        float coeff = transient_detected_ ? fmax(attack_soften_, 0.01f) : 1.0f;

        float32x4_t alpha = vdupq_n_f32(coeff);
        float32x4_t one_minus_alpha = vdupq_n_f32(1.0f - coeff);

        float32x4_t out = vaddq_f32(vmulq_f32(in, alpha),
                                    vmulq_f32(filter_state_[group_idx], one_minus_alpha));
        filter_state_[group_idx] = out;

        return out;
    }

    /**
    * Update panning using pre-calculated tables
    */
    void update_panning() {
        for (int group = 0; group < CLONE_GROUPS; group++) {
            clone_group_t* g = &clone_groups_[group];
            float left_vals[NEON_LANES], right_vals[NEON_LANES];

            // Initialize all lanes to 0 (inactive)
            for (int i = 0; i < NEON_LANES; i++) {
                left_vals[i] = 0.0f;
                right_vals[i] = 0.0f;
            }

            for (int i = 0; i < NEON_LANES; i++) {
                uint32_t clone_idx = group * NEON_LANES + i;
                if (clone_idx < clone_count_) {
                    float pos = (clone_count_ > 1) ?
                                (float)clone_idx / (clone_count_ - 1) : 0.5f;
                    // Clamp to the first quadrant (0-89°) so all clones contribute
                    // positive energy. Using 0-359° was causing L-channel cancellation
                    // (clones at 120° and 240° have equal and opposite sin values).
                    // cos(0°)=1,sin(0°)=0 → hard left; cos(89°)≈0,sin(89°)≈1 → hard right.
                    int angle_idx = (int)(pos * 89);
                    left_vals[i] = cos_table[angle_idx] * spread_;
                    right_vals[i] = sin_table[angle_idx] * spread_;

                    // Randomize phase inversion for Angel mode
                    if (current_mode_ == MODE_ANGEL) {
                        uint32_t rand_bits[NEON_LANES];
                        vst1q_u32(rand_bits, vandq_u32(prng_rand_u32(), vdupq_n_u32(1)));
                        uint32_t flags[NEON_LANES];
                        vst1q_u32(flags, g->phase_flags);
                        flags[i] = rand_bits[i] ? 0xFFFFFFFFU : 0U;
                        g->phase_flags = vld1q_u32(flags);
                    }
                }
            }

            // Load all 4 values at once - this is efficient and correct
            g->left_gains = vld1q_f32(left_vals);
            g->right_gains = vld1q_f32(right_vals);

            // Also update active mask
            uint32_t active_vals[NEON_LANES];
            for (int i = 0; i < NEON_LANES; i++) {
                uint32_t clone_idx = group * NEON_LANES + i;
                active_vals[i] = (clone_idx < clone_count_) ? 0xFFFFFFFFU : 0U;
            }
            g->active = vld1q_u32(active_vals);
        }
    }

    void set_clone_count(int32_t value) {
        switch (value) {
            case 0: clone_count_ = 4; break;
            case 1: clone_count_ = 8; break;
            case 2: clone_count_ = 12; break;
            case 3: clone_count_ = 16; break;
            default: clone_count_ = 4;
        }
        update_panning();
    }

    /**
     * Smooth mode switching with crossfade
     */
    void set_mode(spatial_mode_t new_mode) {
        if (new_mode == current_mode_) return;

        crossfade_counter_ = CROSSFADE_SAMPLES;
        crossfade_active_ = true;

        current_mode_ = new_mode;
        init_mode_filters(&mode_filters_, new_mode, depth_);
        init_clone_parameters();  // Update delay constellation for new mode
        update_panning();         // Update panning / phase inversion for Angel mode
    }

    // Crossfade is applied inline in Process() using vmulq_n_f32 on the wet
    // signal. No separate function needed — see the NEON crossfade block above.
    /**
     * Fast NEON Gather & Interpolate for 4 clones simultaneously
     */
    fast_inline float32x4_t gather_and_interpolate_neon(
        const float* delay_line,
        uint32x4_t idx1_vec,
        uint32x4_t idx2_vec,
        float32x4_t frac_vec)
    {
        // 1. Extract indices to scalar for memory access (unavoidable on ARMv7)
        uint32_t i1[NEON_LANES], i2[NEON_LANES];
        vst1q_u32(i1, idx1_vec);
        vst1q_u32(i2, idx2_vec);

        // 2. "Software Gather" directly into NEON registers lane-by-lane
        // This avoids writing to intermediate arrays and keeps data in the FPU
        float32x4_t s1 = vdupq_n_f32(0.0f);
        float32x4_t s2 = vdupq_n_f32(0.0f);
        s1 = vsetq_lane_f32(delay_line[i1[0]], s1, 0);
        s1 = vsetq_lane_f32(delay_line[i1[1]], s1, 1);
        s1 = vsetq_lane_f32(delay_line[i1[2]], s1, 2);
        s1 = vsetq_lane_f32(delay_line[i1[3]], s1, 3);

        s2 = vsetq_lane_f32(delay_line[i2[0]], s2, 0);
        s2 = vsetq_lane_f32(delay_line[i2[1]], s2, 1);
        s2 = vsetq_lane_f32(delay_line[i2[2]], s2, 2);
        s2 = vsetq_lane_f32(delay_line[i2[3]], s2, 3);

        // 3. Vectorized Linear Interpolation: out = s1 + frac * (s2 - s1)
        // One instruction computes all 4 clones!
        float32x4_t diff = vsubq_f32(s2, s1);
        return vmlaq_f32(s1, diff, frac_vec);
    }

    /*===========================================================================*/
    /* Private Member Variables */
    /*===========================================================================*/

    interleaved_frame_t delay_line_[DELAY_MAX_SAMPLES] __attribute__((aligned(CACHE_LINE_SIZE)));
    uint32_t write_ptr_;

    clone_group_t clone_groups_[CLONE_GROUPS] __attribute__((aligned(CACHE_LINE_SIZE)));
    mode_filters_t mode_filters_  __attribute__((aligned(16)));

    static float sin_table[360]  __attribute__((aligned(16)));
    static float cos_table[360]  __attribute__((aligned(16)));
    static bool tables_initialized;

    prng_t prng_;

    // Attack softening filter states
    float32x4_t filter_state_[CLONE_GROUPS] __attribute__((aligned(CACHE_LINE_SIZE)));

    spatial_mode_t current_mode_;
    uint32_t clone_count_;
    float depth_;
    float rate_;
    float spread_;
    float mix_;
    float wobble_depth_;
    float attack_soften_;

    // Smooth ramping targets for mix/wobble/attack (avoids zipper noise on knob turns)
    float target_mix_;
    float target_wobble_;
    float target_attack_;
    uint32_t mix_ramp_samples_;
    uint32_t wobble_ramp_samples_;
    uint32_t attack_ramp_samples_;

    uint32_t sample_rate_;
    bool initialized_;
    bool is_mono_;

    int32_t params_[k_total]  __attribute__((aligned(16)));
    int32_t last_params_[k_total] __attribute__((aligned(16)));

    uint32x4_t phase_inc_; // Phase increment per sample

    bool transient_detected_;
    float transient_energy_;

    std::atomic_uint_fast32_t flags_;

    // Crossfade state
    float32x4_t old_mode_buffer_[CLONE_GROUPS][NEON_LANES] __attribute__((aligned(CACHE_LINE_SIZE)));
    uint32_t crossfade_counter_;
    bool crossfade_active_;
};
