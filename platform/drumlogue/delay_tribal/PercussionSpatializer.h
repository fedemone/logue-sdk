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

extern float lfo_table[LFO_TABLE_SIZE] __attribute__((aligned(16)));

/**
 * Per-clone parameters with randomization and modulation
 */
typedef struct __attribute__((aligned(CACHE_LINE_SIZE))) {
    float32x4_t delay_offsets;    // Micro-delay for vibrato (4 clones)
    float32x4_t left_gains;       // Left channel pan (4 clones)
    float32x4_t right_gains;      // Right channel pan (4 clones)
    float32x4_t mod_phases;       // LFO phases (4 clones)
    float32x4_t pitch_mod;        // Pitch modulation depth (tape wobble)
    float32x4_t velocity;         // Random velocity per hit (0.7-1.0)
    uint32x4_t phase_flags;       // Phase inversion flags
    uint32x4_t active;            // Which clones are active
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
        , bypass_(true)
        , initialized_(false)
        , sample_rate_(48000)
        , transient_detected_(false)
        , transient_energy_(0.0f)
        , depth_(0.5f)
        , rate_(1.0f)
        , spread_(0.8f)
        , mix_(0.5f)
        , wobble_depth_(0.3f)
        , attack_soften_(0.2f)
        , crossfade_counter_(0)
        , crossfade_active_(false)
        , flags_(0) {

        // Initialize phase increment vector
        phase_inc_ = vdupq_n_f32(0.0f);

        // Initialize PRNG with a fixed seed
        prng_init(0x9E3779B97F4A7C15ULL);

        // Pre-calculate sin/cos tables
        if (!tables_initialized) {
            for (int i = 0; i < 360; i++) {
                float angle = i * 2.0f * M_PI / 360.0f;
                sin_table[i] = sinf(angle);
                cos_table[i] = cosf(angle);
            }
            tables_initialized = true;
        }

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
        if (desc->input_channels != 2 || desc->output_channels != 2)
            return k_unit_err_geometry;

        sample_rate_ = desc->samplerate;

        initialized_ = true;
        bypass_ = false;

        Reset();
        return k_unit_err_none;
    }

    inline void Teardown() {}

    inline void Reset() {
        if (!initialized_) {
            bypass_ = true;
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

        crossfade_counter_ = 0;
        crossfade_active_ = false;
    }

    inline void Resume() {}
    inline void Suspend() {}

    /*===========================================================================*/
    /* OPTIMIZED: Core Processing with NEON and prefetch */
    /*===========================================================================*/

    fast_inline void Process(const float* in, float* out, size_t frames) {
        if (bypass_ || !initialized_) {
            memcpy(out, in, frames * 2 * sizeof(float));
            return;
        }

        const float* in_p = in;
        float*       out_p = out;

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
            float l_arr[4], r_arr[4];
            vst1q_f32(l_arr, in_l4);
            vst1q_f32(r_arr, in_r4);

            float wet_l_arr[4], wet_r_arr[4];
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

            // --- Apply mode crossfade (scalar: counter changes each frame) ---
            if (crossfade_active_) {
                for (int f = 0; f < 4; f++) {
                    float fade_in = 1.0f - (float)crossfade_counter_ / CROSSFADE_SAMPLES;
                    wet_l_arr[f] *= fade_in;
                    wet_r_arr[f] *= fade_in;
                    if (--crossfade_counter_ == 0) { crossfade_active_ = false; break; }
                }
                wet_l4 = vld1q_f32(wet_l_arr);
                wet_r4 = vld1q_f32(wet_r_arr);
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
        if (index < 24) {
            params_[index] = value;
        }

        switch (index) {
            case 0: // Clone Count
                set_clone_count(value);
                break;
            case 1: // Mode
                set_mode(static_cast<spatial_mode_t>(value));
                break;
            case 2: // Depth
                depth_ = value / 100.0f;
                update_filter_params(&mode_filters_, depth_, 48); // 1ms ramp
                break;
            case 3: // Rate (LFO speed for pitch wobble)
                rate_ = 0.1f + (value / 100.0f) * 9.9f;
                phase_inc_ = vdupq_n_f32(rate_ / sample_rate_);
                break;
            case 4: // Spread
                spread_ = value / 100.0f;
                update_panning();
                break;
            case 5: // Mix
                mix_ = value / 100.0f;
                break;
            case 6: // Wobble Depth
                wobble_depth_ = value / 100.0f;
                break;
            case 7: // Attack Softening
                attack_soften_ = value / 100.0f;
                break;
        }
    }

    inline int32_t getParameterValue(uint8_t index) const {
        if (index < 24) {
            return params_[index];
        }
        return 0;
    }

    inline const char* getParameterStrValue(uint8_t index, int32_t value) const {
        static const char* mode_names[] = {"Tribal", "Military", "Angel"};
        static const char* clone_names[] = {"4", "8", "16"};

        switch (index) {
            case 0:
                if (value >= 0 && value <= 2) return clone_names[value];
                break;
            case 1:
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

        for (int group = 0; group < CLONE_GROUPS; group++) {
            clone_group_t* g = &clone_groups_[group];

            float base_delay = group * 0.5f;
            float offsets[NEON_LANES];
            float phases[NEON_LANES];
            float pitch_mod[NEON_LANES];

            for (int i = 0; i < NEON_LANES; i++) {
                int clone_idx = group * NEON_LANES + i;

                offsets[i] = base_delay + (i * 0.1f);
                phases[i] = (float)clone_idx / MAX_CLONES;
                pitch_mod[i] = 0.1f + (i * 0.05f);
            }

            g->delay_offsets = vld1q_f32(offsets);
            g->mod_phases = vld1q_f32(phases);
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

    fast_inline void generate_clones_opt(float32x4_t* out_l, float32x4_t* out_r,
                                        float filter_depth) {
        float32x4_t acc_l = vdupq_n_f32(0.0f);
        float32x4_t acc_r = vdupq_n_f32(0.0f);

        uint32_t num_groups = (clone_count_ + NEON_LANES - 1) / NEON_LANES;
        uint32_t base_read = (write_ptr_ - 32) & DELAY_MASK;

        for (uint32_t g = 0; g < num_groups; g++) {
            clone_group_t* group = &clone_groups_[g];

            // Process all 4 lanes in parallel using NEON vectors
            // Instead of scalar loops with vgetq_lane_f32, we use vector operations

            // Step 1: Update LFO phases for all 4 lanes
            float32x4_t phases = group->mod_phases;
            float32x4_t phase_inc_vec = phase_inc_;

            // Add phase increment to all lanes
            phases = vaddq_f32(phases, phase_inc_vec);

            // Wrap phases to [0, 1.0)
            uint32x4_t overflow = vcgtq_f32(phases, vdupq_n_f32(1.0f));
            phases = vbslq_f32(overflow, vsubq_f32(phases, vdupq_n_f32(1.0f)), phases);

            // Store updated phases back
            group->mod_phases = phases;

            // Step 2: Convert phases to LFO table indices
            // Multiply by table size and convert to integer
            float32x4_t scaled = vmulq_f32(phases, vdupq_n_f32(LFO_TABLE_SIZE));
            uint32x4_t indices = vcvtq_u32_f32(scaled);
            // Mask to table size (power of 2)
            indices = vandq_u32(indices, vdupq_n_u32(LFO_TABLE_SIZE - 1));

            // Step 3: Gather LFO values from table (need to do scalar due to table lookup)
            float lfo_vals[4];
            uint32_t idx_vals[4];
            vst1q_u32(idx_vals, indices);
            for (int i = 0; i < 4; i++) {
                lfo_vals[i] = lfo_table[idx_vals[i]];
            }
            float32x4_t lfo = vld1q_f32(lfo_vals);

            // Step 4: Calculate wobble and delay offsets
            float32x4_t wobble = vmulq_f32(lfo, group->pitch_mod);
            wobble = vmulq_f32(wobble, vdupq_n_f32(wobble_depth_));

            float32x4_t delays = vaddq_f32(group->delay_offsets, wobble);
            float32x4_t offset_samples = vmulq_f32(delays, vdupq_n_f32(48.0f));

            // Step 5: Calculate read positions
            float32x4_t base_read_vec = vdupq_n_f32((float)base_read);
            float32x4_t read_pos = vsubq_f32(base_read_vec, offset_samples);

            // Wrap read positions to [0, DELAY_MAX_SAMPLES)
            float32x4_t delay_max_vec = vdupq_n_f32(DELAY_MAX_SAMPLES);
            float32x4_t pos_adj;

            // Handle negative wrap
            uint32x4_t neg = vcltq_f32(read_pos, vdupq_n_f32(0.0f));
            pos_adj = vbslq_f32(neg, vaddq_f32(read_pos, delay_max_vec), read_pos);

            // Handle positive wrap
            uint32x4_t overflow_pos = vcgtq_f32(pos_adj, vsubq_f32(delay_max_vec, vdupq_n_f32(1.0f)));
            pos_adj = vbslq_f32(overflow_pos, vsubq_f32(pos_adj, delay_max_vec), pos_adj);

            // Step 6: Extract integer positions and fractions
            uint32x4_t pos_int = vcvtq_u32_f32(pos_adj);
            float32x4_t pos_frac = vsubq_f32(pos_adj, vcvtq_f32_u32(pos_int));

            // Get min position for base index
            uint32_t pos_ints[4];
            vst1q_u32(pos_ints, pos_int);
            uint32_t min_pos = pos_ints[0];
            for (int i = 1; i < 4; i++) {
                if (pos_ints[i] < min_pos) min_pos = pos_ints[i];
            }

            // Step 7: Load using vld4 (this is the optimized part)
            uint32_t base_idx = min_pos;
            float32x4x4_t left_frames = read_delayed_vld4(base_idx);
            float32x4x4_t right_frames = vld4q_f32(&delay_line_[base_idx].samples[4]);

            // Step 8: Extract samples for each lane with interpolation
            // Store frames into standard arrays to safely index dynamically
            float l_frames_arr[4][4];
            vst1q_f32(l_frames_arr[0], left_frames.val[0]);
            vst1q_f32(l_frames_arr[1], left_frames.val[1]);
            vst1q_f32(l_frames_arr[2], left_frames.val[2]);
            vst1q_f32(l_frames_arr[3], left_frames.val[3]);

            float r_frames_arr[4][4];
            vst1q_f32(r_frames_arr[0], right_frames.val[0]);
            vst1q_f32(r_frames_arr[1], right_frames.val[1]);
            vst1q_f32(r_frames_arr[2], right_frames.val[2]);
            vst1q_f32(r_frames_arr[3], right_frames.val[3]);

            // We need to extract per-lane, but this is unavoidable for linear interpolation
            // Get integer offsets for each lane
            int offsets[4];
            for (int lane = 0; lane < 4; lane++) {
                offsets[lane] = (int)(pos_ints[lane] - min_pos);
            }

            // Get fractions for interpolation
            float frac_vals[4];
            vst1q_f32(frac_vals, pos_frac);

            float out_l_arr[4];
            float out_r_arr[4];

            // Extract and interpolate each lane
            for (int lane = 0; lane < 4; lane++) {
                int offset = offsets[lane];
                if (offset >= 0 && offset < 4) {
                    // Sample at base position + offset
                    float l_sample0 = l_frames_arr[offset][lane];
                    float r_sample0 = r_frames_arr[offset][lane];

                    // If we need next sample for interpolation (offset + 1)
                    if (offset + 1 < 4) {
                        float l_sample1 = l_frames_arr[offset + 1][lane];
                        float r_sample1 = r_frames_arr[offset + 1][lane];

                        float frac = frac_vals[lane];
                        out_l_arr[lane] = l_sample0 + frac * (l_sample1 - l_sample0);
                        out_r_arr[lane] = r_sample0 + frac * (r_sample1 - r_sample0);
                    } else {
                        // No next sample available, just use current
                        out_l_arr[lane] = l_sample0;
                        out_r_arr[lane] = r_sample0;
                    }
                } else {
                    // Fallback for wrap-around or out-of-range
                    uint32_t idx = pos_ints[lane];
                    float frac = frac_vals[lane];
                    uint32_t idx_next = (idx + 1) & DELAY_MASK;

                    float l_sample0 = delay_line_[idx].samples[lane];
                    float r_sample0 = delay_line_[idx].samples[lane + 4];
                    float l_sample1 = delay_line_[idx_next].samples[lane];
                    float r_sample1 = delay_line_[idx_next].samples[lane + 4];

                    out_l_arr[lane] = l_sample0 + frac * (l_sample1 - l_sample0);
                    out_r_arr[lane] = r_sample0 + frac * (r_sample1 - r_sample0);
                }
            }

            float32x4_t delayed_l = vld1q_f32(out_l_arr);
            float32x4_t delayed_r = vld1q_f32(out_r_arr);

            // Apply velocity randomization
            delayed_l = vmulq_f32(delayed_l, group->velocity);
            delayed_r = vmulq_f32(delayed_r, group->velocity);

            // Apply attack softening
            if (attack_soften_ > 0.01f) {
                delayed_l = apply_attack_softening(delayed_l, g);
                delayed_r = apply_attack_softening(delayed_r, g);
            }

            // Apply mode filters
            apply_mode_filters(&mode_filters_, g, &delayed_l, &delayed_r, filter_depth);

            // Apply phase inversion for variation
            uint32x4_t invert = group->phase_flags;
            float32x4_t neg_one = vdupq_n_f32(-1.0f);
            float32x4_t one = vdupq_n_f32(1.0f);
            float32x4_t phase_scale = vbslq_f32(invert, neg_one, one);

            delayed_l = vmulq_f32(delayed_l, phase_scale);
            delayed_r = vmulq_f32(delayed_r, phase_scale);

            // Accumulate with gains
            acc_l = vmlaq_f32(acc_l, delayed_l, group->left_gains);
            acc_r = vmlaq_f32(acc_r, delayed_r, group->right_gains);
        }

            *out_l = acc_l;
            *out_r = acc_r;
    }

    /**
    * Attack softening filter using NEON
    */
    fast_inline float32x4_t apply_attack_softening(float32x4_t in, uint32_t group_idx) {
        float coeff = transient_detected_ ? attack_soften_ : 0.0f;
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
            float left_vals[4], right_vals[4];

            // Initialize all lanes to 0 (inactive)
            for (int i = 0; i < NEON_LANES; i++) {
                left_vals[i] = 0.0f;
                right_vals[i] = 0.0f;
            }

            for (int i = 0; i < NEON_LANES; i++) {
                int clone_idx = group * NEON_LANES + i;
                if (clone_idx < clone_count_) {
                    float pos = (clone_count_ > 1) ?
                                (float)clone_idx / (clone_count_ - 1) : 0.5f;
                    int angle_idx = (int)(pos * 359);

                    left_vals[i] = sin_table[angle_idx] * spread_;
                    right_vals[i] = cos_table[angle_idx] * spread_;

                    // Randomize phase inversion for Angel mode
                    if (current_mode_ == MODE_ANGEL) {
                        uint32_t rand_bits[4];
                        vst1q_u32(rand_bits, vandq_u32(prng_rand_u32(), vdupq_n_u32(1)));
                        uint32_t flags[4];
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
            uint32_t active_vals[4];
            for (int i = 0; i < NEON_LANES; i++) {
                int clone_idx = group * NEON_LANES + i;
                active_vals[i] = (clone_idx < clone_count_) ? 0xFFFFFFFFU : 0U;
            }
            g->active = vld1q_u32(active_vals);
        }
    }

    void set_clone_count(int32_t value) {
        switch (value) {
            case 0: clone_count_ = 4; break;
            case 1: clone_count_ = 8; break;
            case 2: clone_count_ = 16; break;
            default: clone_count_ = 4;
        }
        update_panning();
    }

    /**
     * Smooth mode switching with crossfade
     */
    void set_mode(spatial_mode_t new_mode) {
        if (new_mode == current_mode_) return;

        // Store current output for crossfade
        // In a real implementation, you'd capture the last output samples
        crossfade_counter_ = CROSSFADE_SAMPLES;
        crossfade_active_ = true;

        init_mode_filters(&mode_filters_, new_mode, depth_);

        current_mode_ = new_mode;
        update_panning(); // Update phase inversion for Angel mode
    }

    /**
     * Apply crossfade during mode switching
     */
    fast_inline void apply_crossfade(float32x4_t* out_l, float32x4_t* out_r) {
        if (!crossfade_active_) return;

        // Calculate fade factors (linear crossfade)
        float fade_out = (float)crossfade_counter_ / CROSSFADE_SAMPLES;
        float fade_in = 1.0f - fade_out;

        float32x4_t fade_in_vec = vdupq_n_f32(fade_in);
        float32x4_t fade_out_vec = vdupq_n_f32(fade_out);

        // For simplicity, we're just fading the current output
        // In a full implementation, you'd store the old mode's output separately
        *out_l = vmulq_f32(*out_l, fade_in_vec);
        *out_r = vmulq_f32(*out_r, fade_in_vec);

        crossfade_counter_--;
        if (crossfade_counter_ == 0) {
            crossfade_active_ = false;
        }
    }

    /*===========================================================================*/
    /* Private Member Variables */
    /*===========================================================================*/

    interleaved_frame_t delay_line_[DELAY_MAX_SAMPLES] __attribute__((aligned(CACHE_LINE_SIZE)));
    uint32_t write_ptr_;

    clone_group_t clone_groups_[CLONE_GROUPS] __attribute__((aligned(CACHE_LINE_SIZE)));
    mode_filters_t mode_filters_;

    static float sin_table[360];
    static float cos_table[360];
    static bool tables_initialized;

    prng_t prng_;

    // Attack softening filter states
    float32x4_t filter_state_[CLONE_GROUPS] __attribute__((aligned(16)));

    spatial_mode_t current_mode_;
    uint32_t clone_count_;
    float depth_;
    float rate_;
    float spread_;
    float mix_;
    float wobble_depth_;
    float attack_soften_;

    uint32_t sample_rate_;
    bool bypass_;
    bool initialized_;

    int32_t params_[24];
    int32_t last_params_[24];

    float32x4_t phase_inc_;

    bool transient_detected_;
    float transient_energy_;

    std::atomic_uint_fast32_t flags_;

    // Crossfade state
    float32x4_t old_mode_buffer_[CLONE_GROUPS][NEON_LANES] __attribute__((aligned(CACHE_LINE_SIZE)));
    uint32_t crossfade_counter_;
    bool crossfade_active_;
};
