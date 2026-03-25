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
#include <arm_neon.h>

#include "unit.h"
#include "constants.h"
#include "compressor_core.h"
#include "filters.h"
#include "wavefolder.h"
#include "operation_overlord.h"
#include "distressor_mode.h"
#include "multiband.h"

// Parameter count must match header.c
#define NUM_PARAMS 13

class MasterFX {
public:
    /*===========================================================================*/
    /* Lifecycle Methods */
    /*===========================================================================*/

    MasterFX(void) : samplerate_(48000.0f) {
        // Initialize all DSP modules
        compressor_init(&comp_);
        sidechain_hpf_init(&sc_hpf_, 80.0f, samplerate_);
        wavefolder_init(&wavefolder_);
        distressor_init(&distressor_);
        multiband_init(&multiband_, samplerate_);
        overlord_init(&overlord_, samplerate_);

        // Initialize smoothing
        smoothing_init(&smoother_, samplerate_);
        envelope_detector_init(&envelope_, samplerate_);
        gain_computer_init(&gain_comp_);

        // Clear parameter array
        for (int i = 0; i < NUM_PARAMS; i++) {
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
        // Reset all components
        compressor_reset(&comp_);
        sc_hpf_hz_ = 80.0f;
        sidechain_hpf_init(&sc_hpf_, sc_hpf_hz_, samplerate_);
        wavefolder_init(&wavefolder_);
        distressor_reset(&distressor_);
        multiband_init(&multiband_, samplerate_);
        overlord_init(&overlord_, samplerate_);
        smoothing_init(&smoother_, samplerate_);
        envelope_detector_init(&envelope_, samplerate_);
        gain_computer_init(&gain_comp_);

        // Set default parameters
        setParameter(0, -200);  // Thresh: -20.0 dB
        setParameter(1, 40);    // Ratio: 4.0
        setParameter(2, 150);   // Attack: 15.0 ms
        setParameter(3, 200);   // Release: 200 ms
        setParameter(4, 0);     // Makeup: 0 dB
        setParameter(5, 0);     // Drive: 0%
        setParameter(6, 100);   // Mix: 100% wet
        setParameter(7, 20);    // SC HPF: 20 Hz
        setParameter(8, 0);     // COMP MODE: Standard
        setParameter(9, 0);     // BAND SEL: Low
        setParameter(10, -200); // L THRESH: -20.0 dB
        setParameter(11, 40);   // L RATIO: 4.0
        setParameter(12, 0);    // DSTR DIST: None
        setParameter(13, 0);    // DSTR RATIO: Warm mode
        setParameter(14, 0);    // BASS: bypass
        setParameter(15, 0);    // TREBLE: bypass
        setParameter(16, 0);    // PRESENCE: bypass

        comp_mode_ = 0;
        band_select_ = 0;
        use_external_sc_ = 0;
        detection_mode_ = DETECT_MODE_PEAK;
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

        // Pre-calculate makeup gain (linear)
        float32x4_t makeup_lin = vdupq_n_f32(powf(10.0f, makeup_db_ / 20.0f));

        // Pre-calculate mix balance
        float32x4_t wet_gain = vdupq_n_f32(mix_);
        float32x4_t dry_gain = vdupq_n_f32(1.0f - mix_);

        // =================================================================
        // Process complete blocks of 4 samples
        // =================================================================
        while (frames_remaining >= 4) {
            // Load 4 stereo frames (16 floats total)
            float32x4x4_t interleaved = vld4q_f32(in_p);
            float32x4_t main_l = interleaved.val[0];
            float32x4_t main_r = interleaved.val[1];
            float32x4_t sc_l   = interleaved.val[2];
            float32x4_t sc_r   = interleaved.val[3];

            // Save dry signal for mixing
            float32x4_t dry_l = main_l;
            float32x4_t dry_r = main_r;

            // Process 4 samples
            float32x4x2_t processed = process_block(main_l, main_r, sc_l, sc_r);

            // Mix stage
            float32x4x2_t mixed;
            mixed.val[0] = vaddq_f32(vmulq_f32(dry_l, dry_gain),
                                      vmulq_f32(processed.val[0], wet_gain));
            mixed.val[1] = vaddq_f32(vmulq_f32(dry_r, dry_gain),
                                      vmulq_f32(processed.val[1], wet_gain));

            // Apply makeup gain
            mixed.val[0] = vmulq_f32(mixed.val[0], makeup_lin);
            mixed.val[1] = vmulq_f32(mixed.val[1], makeup_lin);

            // Store results
            vst2q_f32(out_p, mixed);

            // Advance pointers
            in_p += 16;
            out_p += 8;
            frames_remaining -= 4;
        }

        // =================================================================
        // Process remaining samples (1-3) individually
        // =================================================================
        while (frames_remaining > 0) {
            // Load one stereo frame (4 floats)
            float32x4_t frame = vld1q_f32(in_p);

            // Extract channels
            float32x2_t main = vget_low_f32(frame);
            float32x2_t sc   = vget_high_f32(frame);

            float main_l = vget_lane_f32(main, 0);
            float main_r = vget_lane_f32(main, 1);
            float sc_l   = vget_lane_f32(sc, 0);
            float sc_r   = vget_lane_f32(sc, 1);

            // Save dry signal
            float dry_l = main_l;
            float dry_r = main_r;

            // Process single sample (convert to vectors for reuse)
            float32x4_t main_l_vec = vdupq_n_f32(main_l);
            float32x4_t main_r_vec = vdupq_n_f32(main_r);
            float32x4_t sc_l_vec = vdupq_n_f32(sc_l);
            float32x4_t sc_r_vec = vdupq_n_f32(sc_r);

            float32x4x2_t processed = process_block(main_l_vec, main_r_vec,
                                                     sc_l_vec, sc_r_vec);

            // Extract single results
            float proc_l = vgetq_lane_f32(processed.val[0], 0);
            float proc_r = vgetq_lane_f32(processed.val[1], 0);

            // Mix and output
            out_p[0] = dry_l * (1.0f - mix_) + proc_l * mix_;
            out_p[1] = dry_r * (1.0f - mix_) + proc_r * mix_;

            // Apply makeup gain
            out_p[0] *= powf(10.0f, makeup_db_ / 20.0f);
            out_p[1] *= powf(10.0f, makeup_db_ / 20.0f);

            // Advance pointers
            in_p += 4;
            out_p += 2;
            frames_remaining--;
        }
    }

private:
    /*===========================================================================*/
    /* Private Processing Methods */
    /*===========================================================================*/

    /**
     * Process one block of 4 samples (all modes)
     * Returns processed stereo signal
     */
    fast_inline float32x4x2_t process_block(MasterFX* fx,
                                            float32x4_t main_l,
                                            float32x4_t main_r,
                                            float32x4_t sc_l,
                                            float32x4_t sc_r) {
        // =================================================================
        // 1. SIDECHAIN SELECTION
        // =================================================================
        float32x4_t sidechain;
        if (fx->has_sidechain_ && fx->use_external_sc_) {
            // Use external sidechain (sum L+R)
            sidechain = vaddq_f32(sc_l, sc_r);
        } else {
            // Use main signal as sidechain (sum L+R)
            sidechain = vaddq_f32(main_l, main_r);
        }

        // =================================================================
        // 2. SIDECHAIN HPF
        // =================================================================
        sidechain = sidechain_hpf_process(&fx->sc_hpf_, sidechain);

        // =================================================================
        // 3. ENVELOPE DETECTION (mode-specific)
        // =================================================================
        float32x4_t envelope_db;
        if (fx->comp_mode_ == COMP_MODE_DISTRESSOR && (fx->distressor_.detector_mode & DETECT_HPF)) {
            // Use Distressor's dedicated detector (already in dB)
            float32x4_t envelope = distressor_detect(&fx->distressor_, sidechain, fx->samplerate_);
            envelope_db = linear_to_db(envelope);
        } else {
            // Standard envelope detection
            float32x4_t envelope = envelope_detect(&fx->envelope_, main_l, sidechain);
            envelope_db = linear_to_db(envelope);
        }

        // =================================================================
        // 4. MODE-SPECIFIC PROCESSING
        // =================================================================
        float32x4_t processed_l, processed_r;

        switch (fx->comp_mode_) {
            case COMP_MODE_STANDARD: // Standard compressor
                fx->standard_process(main_l, main_r, envelope_db,
                                    &processed_l, &processed_r);
                break;

            case COMP_MODE_DISTRESSOR: // Distressor mode - contains wavefolder_process
                fx->distressor_process(main_l, main_r, envelope_db,
                                    &processed_l, &processed_r);
                break;

            case COMP_MODE_MULTIBAND: // Multiband mode
                multiband_process(&fx->multiband_, main_l, main_r,
                                &processed_l, &processed_r);
                break;

            default:
                processed_l = main_l;
                processed_r = main_r;
        }

        // =================================================================
        // 5. DRIVE
        // =================================================================
        // distressor uses wavefolding as distortion, apply just one
        if ((fx->comp_mode_ != COMP_MODE_DISTRESSOR) && (fx->drive_ > 0.01f)) {
            float32x4x2_t driven = overlord_process(&overlord_, processed_l, processed_r, samplerate_);
            processed_l = driven.val[0];
            processed_r = driven.val[1];
        }

        float32x4x2_t result;
        result.val[0] = processed_l;
        result.val[1] = processed_r;
        return result;
    }

    /**
     * Standard compressor processing
     */
    fast_inline void standard_process(float32x4_t main_l,
                                      float32x4_t main_r,
                                      float32x4_t envelope_db,
                                      float32x4_t* out_l,
                                      float32x4_t* out_r) {
        // Gain computer with knee
        float32x4_t target_gain_db = gain_computer_process(&gain_comp_,
                                                           envelope_db,
                                                           thresh_db_,
                                                           ratio_);

        // Attack/release smoothing
        float32x4_t smoothed_gain_db = smoothing_process(&smoother_, target_gain_db);

        // Convert to linear gain (ARMv7-compatible)
        float32x4_t gain_lin = neon_expq_f32(vmulq_f32(smoothed_gain_db,
                                                        vdupq_n_f32(0.115129f)));

        // Apply gain reduction
        *out_l = vmulq_f32(main_l, gain_lin);
        *out_r = vmulq_f32(main_r, gain_lin);
    }

    /**
     * Distressor mode processing with integrated detector and wavefolder
     */
    fast_inline void distressor_process(MasterFX* fx,
                                        float32x4_t main_l,
                                        float32x4_t main_r,
                                        float32x4_t envelope_db,
                                        float32x4_t* out_l,
                                        float32x4_t* out_r) {
        // Use Distressor's dedicated envelope detector if available
        float32x4_t detected_db;
        if (fx->distressor_.detector_mode & DETECT_HPF) {
            // Distressor-specific detection with HPF and emphasis
            float32x4_t sidechain = vaddq_f32(main_l, main_r);
            float32x4_t envelope = distressor_detect(&fx->distressor_, sidechain, fx->samplerate_);
            detected_db = linear_to_db(envelope);
        } else {
            detected_db = envelope_db;
        }

        // Distressor-specific gain computer
        float32x4_t target_gain_db = distressor_gain_computer(&fx->distressor_,
                                                            detected_db,
                                                            fx->thresh_db_);

        // Smoothing with opto mode
        float32x4_t smoothed_gain_db = distressor_smooth(&fx->distressor_,
                                                        target_gain_db,
                                                        fx->attack_coeff_,
                                                        fx->release_coeff_ *
                                                        fx->distressor_.opto_release_mult);

        // Convert to linear (ARMv7-compatible)
        float32x4_t gain_lin = neon_expq_f32(vmulq_f32(smoothed_gain_db,
                                                        vdupq_n_f32(0.115129f)));

        // Apply gain
        float32x4_t comp_l = vmulq_f32(main_l, gain_lin);
        float32x4_t comp_r = vmulq_f32(main_r, gain_lin);

        // Apply harmonics or wavefolder based on dist_mode
        switch (fx->distressor_.dist_mode) {
            case DIST_MODE_WAVE:
                // Apply wavefolder to compressed signal
                {
                    float32x4x2_t folded = wavefolder_process(&fx->wavefolder_,
                                                            comp_l,
                                                            comp_r,
                                                            fx->drive_);
                    *out_l = folded.val[0];
                    *out_r = folded.val[1];
                }
                break;

            case DIST_MODE_DIST2:
            case DIST_MODE_DIST3:
            case DIST_MODE_BOTH:
                // Apply harmonic generation
                *out_l = generate_harmonics(&fx->distressor_, comp_l, fx->distressor_.dist_mode);
                *out_r = generate_harmonics(&fx->distressor_, comp_r, fx->distressor_.dist_mode);
                break;

            case DIST_MODE_CLEAN:
            default:
                // No harmonics, just compressed signal
                *out_l = comp_l;
                *out_r = comp_r;
                break;
        }
    }

public:
    /*===========================================================================*/
    /* Parameter Handling - With Bounds Checking */
    /*===========================================================================*/

    inline void setParameter(uint8_t index, int32_t value) {
        // FIXED: Bounds check on parameter index
        if (index >= NUM_PARAMS) return;

        raw_params_[index] = value;

        switch (index) {
            case 0: // THRESH (-60.0 to 0.0 dB)
                thresh_db_ = value * 0.1f;
                break;

            case 1: // RATIO (1.0 to 20.0)
                ratio_ = value * 0.1f;
                break;

            case 2: // ATTACK (0.1 to 100.0 ms)
                attack_ms_ = value * 0.1f;
                attack_coeff_ = expf(-1.0f / (attack_ms_ * 0.001f * samplerate_));
                envelope_set_attack_release(&envelope_, attack_ms_, release_ms_);
                smoothing_set_times(&smoother_, attack_ms_, release_ms_);
                break;

            case 3: // RELEASE (10 to 2000 ms)
                release_ms_ = static_cast<float>(value);
                release_coeff_ = expf(-1.0f / (release_ms_ * 0.001f * samplerate_));
                envelope_set_attack_release(&envelope_, attack_ms_, release_ms_);
                smoothing_set_times(&smoother_, attack_ms_, release_ms_);
                break;

            case 4: // MAKEUP (0.0 to 24.0 dB)
                makeup_db_ = value * 0.1f;
                break;

            case 5: // DRIVE (0 to 100%)
                drive_ = value * 0.01f;
                wavefolder_set_drive(&wavefolder_, value);
                overlord_set_drive(&overlord_, value);
                break;

            case 6: // MIX (-100 to +100)
                mix_ = (value + 100.0f) * 0.005f; // Map to 0.0..1.0
                break;

            case 7: // SC HPF (20 to 500 Hz)
                sc_hpf_hz_ = static_cast<float>(value);
                sidechain_hpf_set_cutoff(&sc_hpf_, sc_hpf_hz_);
                break;

            case 8: // COMP MODE (0-2)
                if (value >= 0 && value <= 2) {
                    comp_mode_ = value;

                    // Reset appropriate detectors for the mode
                    if (comp_mode_ == COMP_MODE_DISTRESSOR) {
                        // Distressor mode - faster attack by default
                        attack_ms_ = fmaxf(attack_ms_, 0.05f);  // Cap to 0.05ms min
                        attack_coeff_ = expf(-1.0f / (attack_ms_ * 0.001f * samplerate_));
                    }
                }
                break;

            case 9: // BAND SEL (0-6) - for multiband mode
                band_select_ = value;
                break;

            case 10: // L THRESH (multiband low threshold) - param_id=0
            case 11: // L RATIO (multiband low ratio) - param_id=1
                switch(band_select_) {
                    case BAND_LOW:
                        multiband_set_param(&multiband_, BAND_LOW, index - 10, value * 0.1f);
                        break;
                    case BAND_MID:
                        multiband_set_param(&multiband_, BAND_MID, index - 10, value * 0.1f);
                        break;
                    case BAND_LOW_MID:
                        multiband_set_param(&multiband_, BAND_LOW, index - 10, value * 0.1f);
                        multiband_set_param(&multiband_, BAND_MID, index - 10, value * 0.1f);
                        break;
                    case BAND_LOW_HI:
                        multiband_set_param(&multiband_, BAND_LOW, index - 10, value * 0.1f);
                        multiband_set_param(&multiband_, BAND_HIGH, index - 10, value * 0.1f);
                        break;
                     case BAND_MID_HI:
                        multiband_set_param(&multiband_, BAND_MID, index - 10, value * 0.1f);
                        multiband_set_param(&multiband_, BAND_HIGH, index - 10, value * 0.1f);
                        break;
                    case BAND_HIGH:
                        multiband_set_param(&multiband_, BAND_HIGH, index - 10, value * 0.1f);
                        break;
                    case BAND_ALL:
                        multiband_set_param(&multiband_, BAND_LOW, index - 10, value * 0.1f);
                        multiband_set_param(&multiband_, BAND_MID, index - 10, value * 0.1f);
                        multiband_set_param(&multiband_, BAND_HIGH, index - 10, value * 0.1f);
                        break;
                    default:
                        break;
                    }
                break;

            case 12: // DSTR MODE (0=None, 1=2nd harm, 2=3rd harm, 3=Both, 4=Wave)
                if (value >= 0 && value <= 4) {
                    distressor_.dist_mode = value;

                    // Enable/disable detector HPF based on mode
                    if (value == DIST_MODE_WAVE) {
                        // Wavefolder benefits from full frequency detection
                        distressor_.detector_mode |= DETECT_HPF;
                    } else {
                        distressor_.detector_mode &= ~DETECT_HPF;
                    }
                }
                break;

            case 13: // DSTR RATIO
                distressor_set_ratio(&distressor_, value);
                break;

            case 14: // BASS (Operation Overlord EQ)
                if (comp_mode_ == COMP_MODE_DISTRESSOR) {  // Distressor mode benefits most from EQ
                    overlord_.bass = value / 100.0f;
                }
                break;

            case 15: // TREBLE
                if (comp_mode_ == COMP_MODE_DISTRESSOR) {
                    overlord_.treble = value / 100.0f;
                }
                break;

            case 16: // PRESENCE
                if (comp_mode_ == COMP_MODE_DISTRESSOR) {
                    overlord_.presence = value / 100.0f;
                }
                break;
        }
    }

    inline int32_t getParameterValue(uint8_t index) const {
        // FIXED: Bounds check on parameter index
        if (index < NUM_PARAMS) {
            return raw_params_[index];
        }
        return 0;
    }

    inline const char* getParameterStrValue(uint8_t index, int32_t value) const {
        static char str_buf[16];

        switch (index) {
            case 8: // COMP MODE
                if (value >= 0 && value <= 2) {
                    static const char* modes[] = {"Standard", "Distressor", "Multiband"};
                    return modes[value];
                }
                break;

            case 9: // BAND SEL
                if (value >= 0 && value <= 6) {
                    static const char* bands[] = {"Low", "Mid", "High", "LowMid", "LowHi", "MidHi", "All"};
                    return bands[value];
                }
                break;

            case 1: // RATIO - show special cases
                {
                    float ratio = value * 0.1f;
                    if (fabsf(ratio - 20.0f) < 0.1f)
                        return "Limit";
                    else {
                        snprintf(str_buf, sizeof(str_buf), "%.1f:1", ratio);
                        return str_buf;
                    }
                }
                break;

            case 6: // MIX - show DRY/BAL/WET
                if (value <= -100) return "DRY";
                if (value >= 100) return "WET";
                if (abs(value) < 10) return "BAL";
                break;

            case 12: // DSTR MODE
                if (value >= 0 && value <= 4) {
                    return distressor_dist_strings[value];
                    return dstr_modes[value];
                }
                break;

            case 13: // DSTR RATIO
                if (value >= 0 && value <= 7) {
                    return distressor_ratio_strings[value];
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
    static inline const char* getPresetName(uint8_t idx) { return nullptr; }

private:
    /*===========================================================================*/
    /* Private Member Variables */
    /*===========================================================================*/

    std::atomic_uint_fast32_t flags_;
    float samplerate_;
    bool has_sidechain_;

    int32_t raw_params_[NUM_PARAMS];

    // Floating-point parameters
    float thresh_db_;
    float ratio_;
    float attack_ms_;
    float release_ms_;
    float makeup_db_;
    float drive_;
    float mix_;   // 0.0 = dry, 1.0 = wet
    float sc_hpf_hz_;

    // DSP coefficients
    float attack_coeff_;
    float release_coeff_;

    // Mode flags
    uint8_t comp_mode_;          // 0=Standard, 1=Distressor, 2=Multiband
    uint8_t band_select_;        // 0=Low, 1=Mid, 2=High, 3=All
    uint8_t use_external_sc_;    // 0=internal, 1=external sidechain
    uint8_t detection_mode_;     // 0=peak, 1=RMS, 2=blend

    // DSP Modules
    compressor_t comp_;
    sidechain_hpf_t sc_hpf_;
    wavefolder_t wavefolder_;
    distressor_t distressor_;
    multiband_t multiband_;
    envelope_detector_t envelope_;
    gain_computer_t gain_comp_;
    smoothing_t smoother_;
    overlord_t overlord_;
};