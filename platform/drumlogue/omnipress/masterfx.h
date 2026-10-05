#pragma once
/*
 * File: masterfx.h
 *
 * OmniPress Master Compressor Effect
 * Inspired by Eventide Omnipressor & Empirical Labs Distressor
 *
 *  * Features:
 * - Negative ratios (expansion)
 * - Reverse compression mode
 * - Peak/RMS detection blend
 * - Wavefolder/overdrive stage
 * - Sidechain HPF with external input
 * FIXED:
 * - Proper array sizes matching header.c (12 parameters)
 * - Bounds checking on all array accesses
 * - Safe string lookup with validation
 */

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <arm_neon.h>

#include "unit.h"
#include "constants.h"
#include "filters.h"
#include "wavefolder.h"
#include "drive_slam.h"
#include "operation_overlord.h"
#include "distressor_mode.h"
#include "multiband.h"

enum parameters
{
    k_threhold,
    k_slope,
    k_attack,                      // also every Multiband band's attack
    k_release,                     // also every Multiband band's release
    k_makeup,
    k_drive,
    k_mix,
    k_sc_hpf,
    k_compressor_mode,             // panel: 0=Standard, 1=Multiband
    k_attenuation_limit,
    k_gain_limit,
    k_detection_mode,              // envelope detector type: 0=Peak, 1=RMS, 2=Blend
    k_bass,
    k_treble,
    k_presence,
    k_band_solo_mute,              // was DstrDist: Off / solo one band / mute one band
    k_band_low_threshold,
    k_band_mid_threshold,
    k_band_high_threshold,
    k_crossover_low,               // low/mid split, 62.5 Hz..1 kHz
    k_band_low_ratio,
    k_band_mid_ratio,
    k_band_high_ratio,
    k_crossover_high,              // mid/high split, 1 kHz..16 kHz
    k_num_params,
};

class MasterFX {
public:
    /*===========================================================================*/
    /* Lifecycle Methods */
    /*===========================================================================*/

    MasterFX(void) : samplerate_(48000.0f) {
        // Initialize all DSP modules
        sidechain_hpf_init(&sc_hpf_, 80.0f, samplerate_);
        wavefolder_init(&wavefolder_);
        distressor_init(&distressor_, samplerate_);
        multiband_init(&multiband_, samplerate_);
        overlord_init(&overlord_, samplerate_);
        slam_init(&slam_, samplerate_);

        // Initialize smoothing
        envelope_detector_init(&envelope_, samplerate_);

        // Clear parameter array
        for (int i = 0; i < k_num_params; i++) {
            raw_params_[i] = 0;
        }
    }

    virtual ~MasterFX(void) {}

    inline int8_t Init(const unit_runtime_desc_t* desc) {
        if (desc->samplerate != 48000)
            return k_unit_err_samplerate;

        // Note: input_channels may be 2 if sidechain feature removed
        has_sidechain_ = (desc->input_channels == 4);

        if (desc->output_channels != 2)
            return k_unit_err_geometry;

        samplerate_ = desc->samplerate;
        Reset();
        return k_unit_err_none;
    }

    inline void Teardown() {}

    inline void Reset() {
        // Clear DSP state FIRST, then apply defaults.  This used to run the
        // other way round, so multiband_init() wiped the per-band threshold,
        // ratio, attack, release and makeup that the setParameter calls above
        // it had just written, and every band came back on its own hardcoded
        // defaults instead of the header's.
        sc_hpf_hz_ = SC_HPF_DEFAULT;
        gain_history_ = vdupq_n_f32(0.0f);
        sidechain_hpf_init(&sc_hpf_, sc_hpf_hz_, samplerate_);
        wavefolder_init(&wavefolder_);
        multiband_init(&multiband_, samplerate_);
        distressor_reset(&distressor_, samplerate_);
        overlord_init(&overlord_, samplerate_);
        slam_init(&slam_, samplerate_);
        envelope_detector_init(&envelope_, samplerate_);
        use_external_sc_ = 0;

        // Set default parameters
        setParameter(k_threhold, THRESH_DEFAULT);   // Thresh: -10.0 dB
        setParameter(k_slope, SLOPE_DEFAULT);       // Slope: 0.4 (4.0:1)
        setParameter(k_attack, ATTACK_DEFAULT);     // Attack: 15.0 ms
        setParameter(k_release, RELEASE_DEFAULT);   // Release: 200 ms
        setParameter(k_makeup, MAKEUP_DEFAULT);     // Makeup: 0 dB
        setParameter(k_drive, DRIVE_DEFAULT);       // Drive: 0%
        setParameter(k_mix, MIX_DEFAULT);           // Mix: 100% wet
        setParameter(k_sc_hpf, SC_HPF_DEFAULT);     // SC HPF: 20 Hz
        setParameter(k_compressor_mode, COMP_MODE_STANDARD);         // COMP MODE: Standard
        setParameter(k_attenuation_limit, ATTENUATION_DEFAULT);      // Defaults to hardware -30.0 dB
        setParameter(k_gain_limit, GAIN_DEFAULT);                    // Defaults to hardware +30.0 dB
        setParameter(k_bass, 50);                                    // BASS: flat (matches header.c init)
        setParameter(k_treble, 50);                                  // TREBLE: flat (matches header.c init)
        setParameter(k_presence, 50);                                // PRESENCE: centre (matches header.c init)
        // The Distressor engine is off the panel; Reset() leaves it on its own
        // default (DstrDist = Off) for the bench and for whoever revives it.
        setDistressorDistortion(DIST_MODE_CLEAN);
        setParameter(k_band_solo_mute, SOLO_MUTE_OFF);               // every band plays
        setParameter(k_band_low_threshold,  BAND_THRESH_DEFAULT);    // -20.0 dB
        setParameter(k_band_mid_threshold,  BAND_THRESH_DEFAULT);
        setParameter(k_band_high_threshold, BAND_THRESH_DEFAULT);
        setParameter(k_crossover_low,  XOVER_LOW_KNOB_DEFAULT);      // 250 Hz
        setParameter(k_band_low_ratio,  BAND_RATIO_DEFAULT);         // 4.0:1
        setParameter(k_band_mid_ratio,  BAND_RATIO_DEFAULT);
        setParameter(k_band_high_ratio, BAND_RATIO_DEFAULT);
        setParameter(k_crossover_high, XOVER_HIGH_KNOB_DEFAULT);     // 2.5 kHz
        setParameter(k_detection_mode, DETECT_MODE_PEAK);            // Detection: Peak

        // Derived coefficients that depend on more than one parameter, so they
        // have to be refreshed once the whole set is in place.
        update_opto_coeff(&distressor_, release_coeff_);
        // Both detectors are plain level followers in every mode -- the gain
        // smoothers carry ATTACK and RELEASE (see DETECTOR_ATTACK_MS) -- which
        // also keeps the three modes' detectors timed alike.
        envelope_set_level_follower(&envelope_);
        envelope_set_level_follower(&distressor_.distressor_env);
    }

    inline void Resume() {}
    inline void Suspend() {}

    /*===========================================================================*/
    /* DSP Process Loop - NEON Optimized with Safe Bounds */
    /*===========================================================================*/

    fast_inline void Process(const float* in, float* out, size_t frames) {
        const float* __restrict in_p = in;
        float* __restrict out_p = out;
        size_t frames_remaining = frames;

        // Pre-calculate mix and makeup balance
        float32x4_t wet_gain = vdupq_n_f32(mix_);
        float32x4_t dry_gain = vdupq_n_f32(1.0f - mix_);
        float32x4_t makeup_lin = vdupq_n_f32(makeup_lin_scalar);
        float32x4_t combined_wet_gain = vmulq_f32(wet_gain, makeup_lin);
        float combined_wet_gain_s = mix_ * makeup_lin_scalar;
        float dry_gain_s = 1.0f - mix_;

        // Every lane of the wet path that came out non-finite this call, OR-ed
        // together and tested once at the end -- see the watchdog below.
        uint32x4_t nonfinite = vdupq_n_u32(0);

        // =================================================================
        // Process complete blocks of 4 samples
        // =================================================================
        while (frames_remaining >= 4) {
            // Load 4 stereo frames — 2-channel or 4-channel (sidechain) layout
            float32x4_t main_l, main_r, sc_l, sc_r;
            if (has_sidechain_) {
                // 4-channel: [L0,R0,SL0,SR0, L1,R1,SL1,SR1, ...] = 16 floats
                float32x4x4_t interleaved = vld4q_f32(in_p);
                main_l = sanitize_input(interleaved.val[0]);
                main_r = sanitize_input(interleaved.val[1]);
                sc_l   = sanitize_input(interleaved.val[2]);
                sc_r   = sanitize_input(interleaved.val[3]);
                in_p += 16;
            } else {
                // 2-channel: [L0,R0, L1,R1, L2,R2, L3,R3] = 8 floats
                float32x4x2_t stereo = vld2q_f32(in_p);
                main_l = sanitize_input(stereo.val[0]);
                main_r = sanitize_input(stereo.val[1]);
                sc_l   = main_l;
                sc_r   = main_r;
                in_p += 8;
            }

            // Save dry signal for mixing
            float32x4_t dry_l = main_l;
            float32x4_t dry_r = main_r;

            // Process 4 samples
            float32x4x2_t processed = process_block(main_l, main_r, sc_l, sc_r);
            nonfinite = vorrq_u32(nonfinite,
                                  vorrq_u32(nonfinite_mask_q(processed.val[0]),
                                            nonfinite_mask_q(processed.val[1])));

            // Mix stage: Apply makeup gain to processed (wet) signal only
            float32x4x2_t mixed;
            mixed.val[0] = vaddq_f32(vmulq_f32(dry_l, dry_gain),
                                     vmulq_f32(processed.val[0], combined_wet_gain));
            mixed.val[1] = vaddq_f32(vmulq_f32(dry_r, dry_gain),
                                     vmulq_f32(processed.val[1], combined_wet_gain));

            // Output limiter: hard clip to [-1, 1] — transparent below clipping level.
            {
                const float32x4_t one  = vdupq_n_f32( 1.0f);
                const float32x4_t mone = vdupq_n_f32(-1.0f);
                mixed.val[0] = vmaxq_f32(mone, vminq_f32(one, mixed.val[0]));
                mixed.val[1] = vmaxq_f32(mone, vminq_f32(one, mixed.val[1]));
            }

            // Store results
            vst2q_f32(out_p, mixed);

            out_p += 8;
            frames_remaining -= 4;
        }

        // =================================================================
        // Process remaining samples (0-3) individually
        // =================================================================
        while (frames_remaining > 0) {
            float main_l, main_r, sc_l, sc_r;
            if (has_sidechain_) {
                main_l = sanitize_input_s(in_p[0]); main_r = sanitize_input_s(in_p[1]);
                sc_l   = sanitize_input_s(in_p[2]); sc_r   = sanitize_input_s(in_p[3]);
                in_p += 4;
            } else {
                main_l = sanitize_input_s(in_p[0]); main_r = sanitize_input_s(in_p[1]);
                sc_l   = main_l;  sc_r   = main_r;
                in_p += 2;
            }

            float dry_l = main_l;
            float dry_r = main_r;

            float32x4x2_t processed = process_block(
                vdupq_n_f32(main_l), vdupq_n_f32(main_r),
                vdupq_n_f32(sc_l),   vdupq_n_f32(sc_r));
            nonfinite = vorrq_u32(nonfinite,
                                  vorrq_u32(nonfinite_mask_q(processed.val[0]),
                                            nonfinite_mask_q(processed.val[1])));

            float out_l_s = dry_l * dry_gain_s + vgetq_lane_f32(processed.val[0], 0) * combined_wet_gain_s;
            float out_r_s = dry_r * dry_gain_s + vgetq_lane_f32(processed.val[1], 0) * combined_wet_gain_s;

            // Output limiter (scalar path)
            out_p[0] = fmaxf(-1.0f, fminf(1.0f, out_l_s));
            out_p[1] = fmaxf(-1.0f, fminf(1.0f, out_r_s));

            out_p += 2;
            frames_remaining--;
        }

        // =================================================================
        // Watchdog
        // =================================================================
        // Every stage here is an IIR -- detector, gain smoother, tone stack,
        // crossovers, DC blockers, bias trackers -- and a NaN that gets into
        // one need never wash out.  Whether it does is up to the optimizer:
        // under -ffast-math each comparison it meets may or may not discard
        // it.  Measured on the shipped ARM build, one +Inf sample on the bus
        // left Multiband at -300 dBFS for good, while Standard and Distressor
        // let a few bad samples out and recovered; a host build of the same
        // source latched in every mode.  This is the master FX, the last thing
        // before the outputs, so a latched OmniPress is a silent drumlogue,
        // and luck is not a design.
        // The input guard above stops anything upstream from starting that;
        // this catches anything that starts in here.
        //
        // The detector state is asked directly as well as the output, because
        // a NaN envelope does not reach the output as a NaN: neon_log2q_f32
        // reads it as ~770 dB, the gain computer turns that into -650 dB, and
        // the unit goes quiet with every sample perfectly finite.
        const uint32x2_t nf2 = vorr_u32(vget_low_u32(nonfinite), vget_high_u32(nonfinite));
        if ((vget_lane_u32(nf2, 0) | vget_lane_u32(nf2, 1)) || state_is_nonfinite()) {
            clear_dsp_state();
            memset(out, 0, frames * 2 * sizeof(float));
            ++guard_trips_;
        }
    }

    /** Times the watchdog has had to clear the DSP state since load. */
    inline uint32_t getGuardTrips() const { return guard_trips_; }

private:
    /*===========================================================================*/
    /* Private Processing Methods */
    /*===========================================================================*/

    /**
     * Input guard: a non-finite sample becomes silence, and anything louder
     * than INPUT_CEILING is clamped to it.
     *
     * OmniPress listens to the whole bus -- every part, both send returns and
     * the drumlogue's own engine -- so it is the one unit that sees every
     * other unit's mistakes, and as the master it is the one whose own mistake
     * silences everything.  A momentary bad block upstream is a click if it
     * passes through and a dead instrument if it latches in here, so it is
     * stopped at the door.  The test is on the exponent bits (see
     * nonfinite_mask_q) because this unit is built with -ffast-math.
     */
    fast_inline float32x4_t sanitize_input(float32x4_t x) {
        x = vbslq_f32(nonfinite_mask_q(x), vdupq_n_f32(0.0f), x);
        return vmaxq_f32(vdupq_n_f32(-INPUT_CEILING),
                         vminq_f32(vdupq_n_f32(INPUT_CEILING), x));
    }

    static inline float sanitize_input_s(float x) {
        if (is_nonfinite(x)) return 0.0f;
        return fmaxf(-INPUT_CEILING, fminf(INPUT_CEILING, x));
    }

    /**
     * The recursive state whose corruption does not show up in the output as
     * a NaN, only as silence or as a gain frozen where it was -- see the
     * watchdog at the end of Process().  The drive stage's level followers
     * belong here: a NaN follower fails the floor test, so the level gain
     * just stops updating, and nothing downstream ever sees a bad sample.
     */
    inline bool state_is_nonfinite() const {
        return is_nonfinite(envelope_.env_state) ||
               is_nonfinite(envelope_.rms_accum) ||
               is_nonfinite(distressor_.distressor_env.env_state) ||
               is_nonfinite(distressor_.level_in) ||
               is_nonfinite(distressor_.level_out) ||
               is_nonfinite(sc_hpf_.z1) || is_nonfinite(sc_hpf_.z2) ||
               is_nonfinite(slam_.env);
    }

    /**
     * Return every audio-rate history to silence and keep every setting.
     *
     * Reset() is no use for this: it puts the parameters back to their header
     * defaults, and the watchdog must not cost the user their patch -- least
     * of all the per-band Multiband values, which raw_params_ cannot restore
     * because it only remembers the last band selection written.
     */
    inline void clear_dsp_state() {
        gain_history_ = vdupq_n_f32(0.0f);
        sidechain_hpf_clear(&sc_hpf_);
        envelope_detector_clear(&envelope_);
        wavefolder_clear_state(&wavefolder_);
        distressor_clear_state(&distressor_);
        multiband_clear_state(&multiband_);
        overlord_clear_state(&overlord_);
        slam_clear_state(&slam_);
    }

    /** Write one per-band setting (multiband_set_param id) to all three bands. */
    fast_inline void set_all_bands(int p_id, float val) {
        for (int b = BAND_LOW; b <= BAND_HIGH; ++b)
            multiband_set_param(&multiband_, b, p_id, val);
    }

    /**
     * Re-arm the drive stage.
     *
     * DRIVE feeds a different shaper in each mode and the slam region belongs
     * to the Distressor only, so the cached pre-gains depend on both the DRIVE
     * knob and COMP MODE.  A host replaying a stored program walks the
     * parameter IDs in order and therefore sets DRIVE (ID 5) before COMP MODE
     * (ID 8), so both handlers call this rather than either computing the
     * gains itself.
     *
     * Standard and Multiband pass slam = 0, which leaves every law in this
     * chain exactly where it was before the slam region existed.
     */
    fast_inline void refresh_drive_stage() {
        const float slam = (comp_mode_ == COMP_MODE_DISTRESSOR)
                             ? drive_slam_amount(drive_)
                             : 0.0f;
        const float drive_percent = drive_ * 100.0f;

        // Which stage DRIVE is actually reaching decides how much bias and trim
        // the slam applies -- see drive_slam_voicing in constants.h.
        slam_set(&slam_, slam, drive_slam_family(distressor_.dist_mode));
        wavefolder_set_drive(&wavefolder_, drive_percent, slam);
        distressor_set_drive(&distressor_, drive_percent, slam);
        overlord_set_drive(&overlord_, drive_percent, slam);

        // Multiband saturates inside each band with its own triode voicing
        // (softer on the lows, brighter on the highs) rather than driving one
        // broadband stage. The field existed and was read by multiband_process
        // but nothing ever wrote it.
        for (int b = BAND_LOW; b <= BAND_HIGH; ++b)
            multiband_set_param(&multiband_, b, 7, drive_);
    }

    /**
     * Process one block of 4 samples (all modes)
     * Returns processed stereo signal
     */
    fast_inline float32x4x2_t process_block(float32x4_t main_l,
                                            float32x4_t main_r,
                                            float32x4_t sc_l,
                                            float32x4_t sc_r) {
        // =================================================================
        // 1. SIDECHAIN SELECTION
        // =================================================================
        // Average, not sum: summing made the detector see +6 dB on mono-correlated
        // material, so the labelled threshold was 6 dB optimistic and drifted with
        // stereo width.  Multiband detects per band with max(|L|,|R|), so summing
        // here also gave the three modes different effective thresholds.
        float32x4_t sidechain;
        if (has_sidechain_ && use_external_sc_) {
            sidechain = vmulq_n_f32(vaddq_f32(sc_l, sc_r), 0.5f);
        } else {
            sidechain = vmulq_n_f32(vaddq_f32(main_l, main_r), 0.5f);
        }

        // =================================================================
        // 2. SIDECHAIN HPF
        // =================================================================
        sidechain = sidechain_hpf_process(&sc_hpf_, sidechain);

        // =================================================================
        // 3 & 4. ENVELOPE DETECTION & MODE-SPECIFIC PROCESSING
        // =================================================================
        float32x4_t processed_l, processed_r;

        switch (comp_mode_) {
            case COMP_MODE_STANDARD: {
                float32x4_t env = envelope_detect(&envelope_, sidechain);
                float32x4_t env_db = linear_to_db(env);
                standard_process(main_l, main_r, env_db, &processed_l, &processed_r);
                break;
            }
            case COMP_MODE_DISTRESSOR: {
                float32x4_t env;
                if (distressor_.detector_mode & DETECT_LINK) {
                    float32x4_t raw_l = (has_sidechain_ && use_external_sc_) ? sc_l : main_l;
                    float32x4_t raw_r = (has_sidechain_ && use_external_sc_) ? sc_r : main_r;
                    env = distressor_detect_stereo(&distressor_, raw_l, raw_r, samplerate_);
                } else {
                    env = distressor_detect(&distressor_, sidechain, samplerate_);
                }
                float32x4_t env_db = linear_to_db(env);
                distressor_process(main_l, main_r, env_db, &processed_l, &processed_r);
                break;
            }
            case COMP_MODE_MULTIBAND:
                // Detectors are inside multiband_process.
                // Redundant envelope detection is now skipped to save CPU.
                multiband_process(&multiband_, main_l, main_r,
                                  sidechain, (has_sidechain_ && use_external_sc_) != 0,
                                  &processed_l, &processed_r);
                break;

            default:
                processed_l = main_l;
                processed_r = main_r;
        }

        // =================================================================
        // 5. DRIVE / EQ
        // =================================================================
        // The old gate was 'drive_ > 0.01f', i.e. DRIVE=1 (drive_ = 0.01) was
        // byte-identical to DRIVE=0 — a dead first click — and the Overlord EQ
        // never ran at all in Standard/Multiband unless DRIVE reached 2.
        // Routing DRIVE=0 through the EQ-only path fixes both: the EQ is always
        // in circuit and the drive knob is live from its first step.
        //
        // Which stage DRIVE reaches depends on the mode:
        //   Standard    - the broadband tube always.
        //   Distressor  - the DstrDist shaper, which is where DRIVE belongs...
        //                 except at DstrDist = None, where that shaper is bypassed
        //                 and DRIVE would otherwise be a dead knob (the factory
        //                 default combination).  Fall through to the tube instead,
        //                 so the mode has a broadband drive at every DstrDist.
        //   Multiband   - never: each band has its own triode, fed from DRIVE in
        //                 setParameter, and stacking a broadband stage on top
        //                 would double-saturate.
        const bool tube_drive =
            drive_ > 0.0f &&
            (comp_mode_ == COMP_MODE_STANDARD ||
             (comp_mode_ == COMP_MODE_DISTRESSOR &&
              distressor_.dist_mode == DIST_MODE_CLEAN));

        if (!tube_drive) {
            float32x4x2_t eq_out = overlord_apply_eq(&overlord_, processed_l, processed_r, samplerate_);
            processed_l = eq_out.val[0];
            processed_r = eq_out.val[1];
        } else {
            float32x4x2_t driven = overlord_process(&overlord_, processed_l, processed_r, samplerate_);
            processed_l = driven.val[0];
            processed_r = driven.val[1];
        }

        // =================================================================
        // 6. SLAM OUTPUT STAGE
        // =================================================================
        // Closes the bias slam_bias() opened at the head of the drive chain:
        // biased clipping leaves DC behind, and the trim keeps the region on
        // the right side of the output limiter so what changes above DRIVE 60
        // is the character and not just the level.  Disarmed outside the
        // Distressor's slam region, where it costs one predictable branch.
        // The DstrDist shapers have their own DC blocker and level matching
        // (distressor_drive_output), so this is the tube path's alone.
        if (tube_drive)
            slam_output(&slam_, &processed_l, &processed_r);

        float32x4x2_t result;
        result.val[0] = processed_l;
        result.val[1] = processed_r;
        return result;
    }

    /**
     * Standard compressor processing
     * - Zero Zipper Noise: The gain applied to the VCA (gain_history_)
     * is smoothed by a continuous single-pole IIR filter on every single sample block.
     * Even if the input audio hovers aggressively right on the threshold pivot point,
     * the filter prevents instantaneous steps.
     * - Consistent Ballistics: Because the smoothing happens after the function_slope_
     * multiplier, an attack setting of $20\text{ ms}$ means the gain will take exactly
     * $20\text{ ms}$ to reach its target, regardless of whether you are doing a mild
     * 2:1 compression or an extreme -2.0 reversal.
     * - Smart Directional Switching: By checking fabsf(targets[i]) > fabsf(state),
     * the smoother correctly understands that going from $0\text{ dB}$ to $+12\text{ dB}$
     * (Expansion) and going from $0\text{ dB}$ to $-12\text{ dB}$ (Compression)
     * are both Attack phases because the processor is actively responding to a transient spike.
     */
    fast_inline void standard_process(float32x4_t main_l, float32x4_t main_r,
                                  float32x4_t envelope_db, // Assumed instantaneous peak dB here
                                  float32x4_t* out_l, float32x4_t* out_r) {

        // 1. Calculate raw displacement from the pivot
        float32x4_t excess_db = vsubq_f32(envelope_db, vdupq_n_f32(thresh_db_));

        // 2. Multiply by function slope to find target gain
        float32x4_t target_gain_db = vmulq_f32(excess_db, vdupq_n_f32(function_slope_));

        // 3. Clamp safely to your UI limits
        target_gain_db = vmaxq_f32(target_gain_db, vdupq_n_f32(atten_limit_db_));
        target_gain_db = vminq_f32(target_gain_db, vdupq_n_f32(gain_limit_db_));

        // 4. BI-DIRECTIONAL SMOOTHING (Unrolled for IIR Vector correctness)
        float targets[4], smoothed[4];
        vst1q_f32(targets, target_gain_db);

        // Fetch the scalar history from the last lane of the previous block
        float state = vgetq_lane_f32(gain_history_, 3);

        for (int i = 0; i < 4; ++i) {
            // Evaluate ballistics based on whether audio is demanding MORE or LESS gain modification
            // If the target is moving further away from 0dB unity, we are in the "Attack" phase of the effect.
            bool is_attack = fabsf(targets[i]) > fabsf(state);
            float coeff = is_attack ? attack_coeff_ : release_coeff_;

            // Single pole IIR filter.  The coefficient is the fraction of the
            // distance *kept* each sample, so it multiplies (state - target);
            // this read state += coeff * (target - state), which with coeff
            // near 1 jumped 97-99% of the way every sample and made ATTACK
            // and RELEASE do nothing here.
            state = targets[i] + coeff * (state - targets[i]);
            smoothed[i] = state;
        }
        // Store history back to the class member vector
        gain_history_ = flush_denormal_q(vld1q_f32(smoothed));

        // 5. Convert smoothly changing dB back to linear gain
        float32x4_t gain_lin = neon_expq_f32(vmulq_f32(gain_history_, vdupq_n_f32(INV_DB_COEFF)));

        // 6. Apply to VCA
        *out_l = vmulq_f32(main_l, gain_lin);
        *out_r = vmulq_f32(main_r, gain_lin);
    }

    /**
     * Distressor mode processing with integrated detector and wavefolder
     */
    fast_inline void distressor_process(float32x4_t main_l,
                                        float32x4_t main_r,
                                        float32x4_t envelope_db,
                                        float32x4_t* out_l,
                                        float32x4_t* out_r) {
        // Detection is fully handled in process_block() before this call;
        // envelope_db already reflects HPF, EMPH, and LINK flags.
        float32x4_t target_gain_db = distressor_gain_computer(&distressor_,
                                                              envelope_db,
                                                              thresh_db_);

        float32x4_t smoothed_gain_db = distressor_smooth(&distressor_,
                                                         target_gain_db,
                                                         attack_coeff_,
                                                         distressor_.opto_coeff);

        float32x4_t gain_lin = neon_expq_f32(vmulq_f32(smoothed_gain_db, vdupq_n_f32(INV_DB_COEFF)));

        float32x4_t comp_l = vmulq_f32(main_l, gain_lin);
        float32x4_t comp_r = vmulq_f32(main_r, gain_lin);
        // What enters the drive stage, for its level matching below.
        const float32x4_t drive_in_l = comp_l;
        const float32x4_t drive_in_r = comp_r;

        // Slam bias, ahead of whichever shaper DstrDist selects -- including
        // None, where the signal falls through to the Overlord tube and picks
        // it up there.  Disarmed below DRIVE_SLAM_KNEE.  slam_output() in
        // process_block() removes the DC it leaves behind.
        slam_bias(&slam_, &comp_l, &comp_r);

        // Apply saturation to the COMPRESSED signal (not raw input).

        switch (distressor_.dist_mode) {
            case DRIVE_MODE_SOFT_CLIP:
            case DRIVE_MODE_HARD_CLIP:
            case DRIVE_MODE_TRIANGLE:
            case DRIVE_MODE_SINE:
            case DRIVE_MODE_SUBOCTAVE: {
                // Drive and makeup are already cached by setParameter(k_drive);
                // recomputing them here ran a reciprocal square root on every
                // 4-sample block for no reason.
                // Wavefolder operates post-compression for dynamics control
                float32x4x2_t folded = wavefolder_process(&wavefolder_, comp_l, comp_r);
                *out_l = folded.val[0];
                *out_r = folded.val[1];
                break;
            }
            case DIST_MODE_DIST2:
            case DIST_MODE_DIST3:
            case DIST_MODE_BOTH: {
                // Pre-gain cached by distressor_set_drive(): 1x (DRIVE=0) to
                // 40x (DRIVE=100) below the slam knee, then geometric on top
                // of that up to 960x, which is what carries these saturators
                // from their knee into the square-wave regime.
                // makeup = 1.0 keeps output level comparable to the input so
                // harmonic character is always audible. The output hard-clip limiter
                // prevents clipping. Do NOT divide by sat_drive (old formula made
                // the distorted output quieter than dry, masking the effect).
                const float32x4_t drv = distressor_.sat_drive;
                *out_l = generate_harmonics(&distressor_,
                                            vmulq_f32(comp_l, drv),
                                            distressor_.dist_mode);
                *out_r = generate_harmonics(&distressor_,
                                            vmulq_f32(comp_r, drv),
                                            distressor_.dist_mode);
                break;
            }
            case DIST_MODE_CLEAN:
            default:
                // The Overlord tube takes it from here (process_block), with
                // its own DC blocker and the slam's trim behind it.
                *out_l = comp_l;
                *out_r = comp_r;
                return;
        }

        // Every shaper above: DC out, level back to what went in.
        distressor_drive_output(&distressor_, drive_in_l, drive_in_r, out_l, out_r);
    }

public:
    /*===========================================================================*/
    /* Parameter Handling - With Bounds Checking */
    /*===========================================================================*/

    inline void setParameter(uint8_t index, int32_t value) {
        if (index >= k_num_params) return;

        raw_params_[index] = value;

        switch (index) {
            /*===========================================================================*/
            /* General Parameters */
            /*===========================================================================*/
            case k_threhold: // THRESH (-60.0 to 0.0 dB)
                thresh_db_ = value * 0.1f;
                break;

            case k_slope: // RATIO: map 0 to 100 into 0.0 to 1.0 representing the physical knob turn
                {
                    if (comp_mode_ == COMP_MODE_DISTRESSOR) {
                        int knob = value * 0.0799f;  // Map 0-100 to 0-7 for the distressor's 8 ratio steps
                        distressor_set_ratio(&distressor_, knob);  // this updates opto_release_mult
                        update_opto_coeff(&distressor_, release_coeff_);
                        break;
                    }
                    // Map your raw value to a normalized float position (0.0 to 1.0)
                    // Adjust the math below depending on your actual minimum/maximum raw values
                    float knob = value * 0.01f;

                    // Map the normalized knob position across the 3 hardware regions:
                    if (knob < 0.333f) {
                        // 0.0 to 0.333: Expansion/Gating region
                        // Slopes from +3.0 (Max expansion) down to 0.0 (Linear pass-through)
                        function_slope_ = 3.0f * (1.0f - (knob * 3));
                    }
                    else if (knob < 0.666f) {
                        // 0.333 to 0.666: Compression region
                        // Slopes from 0.0 down to -1.0 (Infinite limiting)
                        function_slope_ = -1.0f * ((knob - 0.333f) * 3);
                    }
                    else {
                        // 0.666 to 1.0: Dynamic Reversal region
                        // Slopes from -1.0 down to -2.0 (Extreme reverse sucking envelope)
                        function_slope_ = -1.0f - ((knob - 0.666f) * 3);
                    }
                }
                break;

            case k_attack: // ATTACK (0.1 to 100.0 ms), every mode and every band
                attack_ms_ = value * 0.1f;
                attack_coeff_ = ballistics_coeff(attack_ms_, samplerate_);
                set_all_bands(3, attack_ms_);
                break;

            case k_release: // RELEASE (10 to 2000 ms), every mode and every band
                release_ms_ = static_cast<float>(value);
                release_coeff_ = ballistics_coeff(release_ms_, samplerate_);
                update_opto_coeff(&distressor_, release_coeff_);
                set_all_bands(4, release_ms_);
                break;

            case k_makeup: // MAKEUP (0.0 to 24.0 dB)
                makeup_db_ = value * 0.1f;
                // fasterpowf(10, 0) returns 0.9713, which put a 0.25 dB insertion
                // loss on every mode even at MAKEUP=0, and turned 24.0 dB into
                // 23.69 dB.  e_expff holds 0.033 dB over the whole range.
                makeup_lin_scalar = e_expff(makeup_db_ * INV_DB_COEFF);
                break;
            case k_attenuation_limit: // ATTEN LIMIT (-30.0 to 0.0 dB)
                atten_limit_db_ = value * 0.1f;
                break;
            case k_gain_limit: // GAIN LIMIT (0.0 to 30.0 dB)
                gain_limit_db_ = value * 0.1f;
                break;

            case k_drive: // DRIVE (0 to 100%)
                drive_ = value * 0.01f;
                refresh_drive_stage();
                break;

            case k_mix: // MIX (-100 to +100)
                mix_ = (value + 100.0f) * 0.005f; // Map to 0.0..1.0
                break;

            case k_sc_hpf: // SC HPF (20 to 500 Hz)
                sc_hpf_hz_ = static_cast<float>(value);
                sidechain_hpf_set_cutoff(&sc_hpf_, sc_hpf_hz_);
                break;

            /*===========================================================================*/
            /* Multiband Parameters: one knob per band, no selector */
            /*===========================================================================*/
            // These replaced a band selector (MBand) in front of shared
            // Thr/Ratio/Atk/Rel/Makeup/State knobs, whose readout could show
            // one band at a time.  ATTACK and RELEASE (page 1) now set every
            // band, and per-band makeup is gone in favour of MAKEUP.
            case k_band_low_threshold:
            case k_band_mid_threshold:
            case k_band_high_threshold:
                multiband_set_param(&multiband_, index - k_band_low_threshold, 0, value * 0.1f);
                break;
            case k_band_low_ratio:
            case k_band_mid_ratio:
            case k_band_high_ratio:
                multiband_set_param(&multiband_, index - k_band_low_ratio, 1, value * 0.1f);
                break;
            case k_crossover_low:
                multiband_set_crossover_low(&multiband_, static_cast<float>(value));
                break;
            case k_crossover_high:
                multiband_set_crossover_high(&multiband_, static_cast<float>(value));
                break;
            case k_band_solo_mute:
                multiband_set_solo_mute(&multiband_,
                                        (value >= 0 && value < SOLO_MUTE_TOTAL) ? value : SOLO_MUTE_OFF);
                break;

            case k_compressor_mode: // COMP MODE: 0=Standard, 1=Multiband
                // Anything else is Multiband too: a program saved when this
                // read 0=Standard, 1=Distressor, 2=Multiband comes back on a
                // mode that exists rather than on the shelved one.
                setEngineMode(value == 0 ? COMP_MODE_STANDARD : COMP_MODE_MULTIBAND);
                break;

            /*===========================================================================*/
            /* Operation Overlord Distortion Emulation Parameters */
            /*===========================================================================*/
            case k_bass: // BASS (Operation Overlord EQ)
                overlord_.bass = value * 0.01f;
                break;

            case k_treble: // TREBLE
                overlord_.treble = value * 0.01f;
                break;

            case k_presence: // PRESENCE
                overlord_.presence = value * 0.01f;
                break;

            case k_detection_mode:
                // Bit 2 selects the sidechain source. The unit already uses all
                // 24 parameters the SDK allows (UNIT_MAX_PARAM_COUNT), so the
                // external input rides on the spare bit of the detector control
                // rather than taking a slot of its own — it belongs with the
                // other detector settings anyway. Values 0-3 keep their old
                // meaning; 4-7 are the same detector fed from SC L/R.
                use_external_sc_ = (value & 4) ? 1 : 0;
                if (comp_mode_ == COMP_MODE_DISTRESSOR) {
                    // Distressor detector flags bitmask: 0=Basic, 1=Emph, 2=Link, 3=Emph+Link
                    // DETECT owns the whole detector now, DstrDist no longer
                    // reaches in to set DETECT_HPF behind its back.
                    distressor_.detector_mode = DETECT_NONE;
                    if (value & 1) distressor_.detector_mode |= DETECT_BAND_EMPH;
                    if (value & 2) distressor_.detector_mode |= DETECT_LINK;
                } else {
                    // Standard / multiband: Peak=0, RMS=1, Blend=2
                    const int32_t det = value & 3;
                    if (det <= 2) {
                        detection_mode_ = det;
                        envelope_.mode = detection_mode_;
                    }
                }
                break;
        }
    }

    /*===========================================================================*/
    /* Engine API not on the panel                                               */
    /*===========================================================================*/
    // The drumlogue reaches the unit only through setParameter(), so nothing
    // below is reachable from the instrument.  It is how the bench -- and
    // anyone reviving the Distressor -- selects an engine mode the panel no
    // longer offers.

    /**
     * Select the compressor engine: COMP_MODE_STANDARD, COMP_MODE_DISTRESSOR
     * or COMP_MODE_MULTIBAND.  The panel's COMP MODE maps onto the first and
     * last of these.
     */
    inline void setEngineMode(uint8_t mode) {
        if (mode >= COMP_MODE_TOTAL) return;
        comp_mode_ = mode;
        if (comp_mode_ == COMP_MODE_DISTRESSOR) {
            // Distressor expects at least 0.05ms attack
            attack_ms_ = fmaxf(attack_ms_, 0.05f);
            attack_coeff_ = ballistics_coeff(attack_ms_, samplerate_);
        }
        // SLOPE and DETECT mean something different in each mode, so re-read
        // them here.  A host replaying a stored program walks the parameter
        // IDs in order and therefore sets SLOPE (ID 1) before COMP MODE
        // (ID 8): without this the distressor ratio stayed at its 4:1 init no
        // matter where the knob was.
        setParameter(k_slope, raw_params_[k_slope]);
        setParameter(k_detection_mode, raw_params_[k_detection_mode]);
        // DRIVE (ID 5) is replayed before this for the same reason, and only
        // the Distressor has a slam region, so the drive stage has to be
        // re-armed once the mode is known.
        refresh_drive_stage();
    }

    /**
     * The Distressor's distortion type -- what DstrDist (ID 15) selected
     * before that slot became SoloMute: 0=Off (the Overlord tube), 1=Dist2,
     * 2=Dist3, 3=Both, 4=Soft, 5=Hard, 6=Trg, 7=Sine, 8=SubOct.
     */
    inline void setDistressorDistortion(int32_t value) {
        if (value < 0 || value >= DIST_MODE_TOTAL) return;
        distressor_.dist_mode = value;
        if (value > DIST_MODE_BOTH)
            wavefolder_set_drive_type(&wavefolder_, value);

        // The detector's 100 Hz HPF used to switch in here, on the wavefolder
        // modes only. That made the distortion selector change the
        // compression: on a kick-heavy bus the detector lost most of its
        // energy, gain reduction backed off, and selecting Soft or Hard jumped
        // the output 4 dB even at DRIVE=0. Detector shaping belongs to DETECT
        // (which offers Emph for exactly this), not to the distortion type.

        // The slam's bias budget depends on which shaper family DRIVE is
        // reaching, so selecting one re-arms the stage.
        refresh_drive_stage();
    }

    inline uint8_t getEngineMode() const { return comp_mode_; }

    inline int32_t getParameterValue(uint8_t index) const {
        // FIXED: Bounds check on parameter index
        if (index < k_num_params) {
            return raw_params_[index];
        }
        return 0;
    }

    inline const char* getParameterStrValue(uint8_t index, int32_t value) const {
        static char str_buf[16];

        switch (index) {
            case k_compressor_mode: // COMP MODE (panel values)
                if (value >= 0 && value <= 1) {
                    static const char *modes[] = {"Stndrd", "Mltibnd"};
                    return modes[value];
                }
                break;

            case k_band_solo_mute:
                if (value >= 0 && value < SOLO_MUTE_TOTAL) {
                    static const char* sm[] = {"Off", "Lo-Solo", "Mi-Solo", "Hi-Solo",
                                               "Lo-Mute", "Mi-Mute", "Hi-Mute"};
                    return sm[value];
                }
                break;

            case k_band_low_ratio:
            case k_band_mid_ratio:
            case k_band_high_ratio:
                snprintf(str_buf, sizeof(str_buf), "%.1f:1", value * 0.1f);
                return str_buf;

            case k_crossover_low:
                snprintf(str_buf, sizeof(str_buf), "%dHz",
                         (int)(multiband_xover_knob_hz(XOVER_LOW_HZ_MIN, (float)value) + 0.5f));
                return str_buf;

            case k_crossover_high:
                snprintf(str_buf, sizeof(str_buf), "%.1fkHz",
                         multiband_xover_knob_hz(XOVER_HIGH_HZ_MIN, (float)value) * 0.001f);
                return str_buf;

            case k_slope: // SLOPE (1.0 to 20.0) - show special cases
                {
                    if (comp_mode_ == COMP_MODE_DISTRESSOR) {
                        // Distressor has 8 fixed ratio steps: 1:1, 2:1, 3:1, 4:1, 6:1, 8:1, 12:1, 20:1
                        int knob = value * 0.0799f; // Map 0-100 to 0-7 and never goes to 8 which is invalid
                        if (knob >= 0 && knob < DIST_RATIO_TOTAL) {
                            return distressor_ratio_strings[knob];
                        }
                        break;
                    }
                    // Display the true Omnipressor state based on the internal slope
                    if (function_slope_ > 0.05f) {
                        snprintf(str_buf, sizeof(str_buf), "Exp %.1f", 1.0f + function_slope_);
                    } else if (function_slope_ >= -0.95f && function_slope_ <= 0.05f) {
                        float ratio_val = 1.0f / (1.0f + function_slope_);
                        snprintf(str_buf, sizeof(str_buf), "%.1f:1", ratio_val);
                    } else if (function_slope_ < -0.95f && function_slope_ >= -1.05f) {
                        return "Limit";
                    } else {
                        // Negative ratio conversion
                        float rev_val = 1.0f / (fabsf(function_slope_) - 1.0f);
                        snprintf(str_buf, sizeof(str_buf), "Rev %.1f", rev_val);
                    }
                    return str_buf;
                }
                break;

            case k_drive: // DRIVE - flag the Distressor's slam region
                // Everywhere else this is a plain percentage, but in Distressor
                // mode the knob crosses into a different job at DRIVE_SLAM_KNEE:
                // past it the shapers are driven geometrically and biased, and
                // what the knob buys stops being warmth and starts being
                // destruction.  A knob whose behaviour changes partway along
                // should say where, the same way SLOPE names its regions.
                if (comp_mode_ == COMP_MODE_DISTRESSOR &&
                    drive_slam_amount(value * 0.01f) > 0.0f) {
                    snprintf(str_buf, sizeof(str_buf), "SLAM %d", (int)value);
                } else {
                    snprintf(str_buf, sizeof(str_buf), "%d%%", (int)value);
                }
                return str_buf;

            case k_mix: // MIX - show DRY/BAL/WET
                if (value <= -100) return "DRY";
                if (value >= 100) return "WET";
                if (abs(value) < 10) return "BAL";
                break;

            case k_detection_mode:
                // 4-7 are the same detectors listening to the external sidechain
                if (comp_mode_ == COMP_MODE_DISTRESSOR) {
                    static const char* dst_det[] = {"Basic", "Emph", "Link", "Emp+Lnk",
                                                    "BasicSC", "EmphSC", "LinkSC", "EmLnkSC"};
                    if (value >= 0 && value <= 7) return dst_det[value];
                } else {
                    static const char* std_det[] = {"Peak", "RMS", "Blend", "-",
                                                    "PeakSC", "RMS SC", "BlndSC", "-"};
                    if (value >= 0 && value <= 7) return std_det[value];
                }
                break;
        }
        return nullptr;
    }

    inline const uint8_t* getParameterBmpValue(uint8_t index, int32_t value) const {
        (void)index;
        (void)value;
        return nullptr;
    }

    inline void LoadPreset(uint8_t idx) { (void)idx; }
    inline uint8_t getPresetIndex() const { return 0; }
    static inline const char* getPresetName(uint8_t idx) { (void)idx; return nullptr; }

private:
    /*===========================================================================*/
    /* Private Member Variables */
    /*===========================================================================*/

    std::atomic_uint_fast32_t flags_;
    float samplerate_;
    bool has_sidechain_;

    int32_t raw_params_[k_num_params]  __attribute__((aligned(16)));

    // Floating-point parameters
    float thresh_db_;
    float function_slope_;
    float attack_ms_;
    float release_ms_;
    float makeup_db_;
    float makeup_lin_scalar;
    float atten_limit_db_;
    float gain_limit_db_;
    float drive_;
    float mix_;   // 0.0 = dry, 1.0 = wet
    float sc_hpf_hz_;
    float32x4_t gain_history_;

    // DSP coefficients
    float attack_coeff_;
    float release_coeff_;

    // Mode flags
    uint8_t comp_mode_;          // 0=Standard, 1=Distressor, 2=Multiband
    uint8_t use_external_sc_;    // 0=internal, 1=external sidechain
    uint8_t detection_mode_;     // 0=peak, 1=RMS, 2=blend

    // DSP Modules
    sidechain_hpf_t sc_hpf_;
    wavefolder_t wavefolder_;
    distressor_t distressor_;
    multiband_t multiband_;
    envelope_detector_t envelope_;
    overlord_t overlord_;
    slam_t slam_;

    uint32_t guard_trips_ = 0;   // watchdog clears since load
};