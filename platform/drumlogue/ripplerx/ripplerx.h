#include <cstdint>
#ifdef __cplusplus
extern "C" {
#endif
#ifdef __cplusplus
}
#endif
#include "constants.h"
#pragma once
/*
*  File: RipplerX.h
*
*  Synth Class derived from drumlogue unit
*
*  2021-2025 (c) Korg
*
*/

#include <stdio.h>
#include <cstdio>
#include <math.h>
#include <stddef.h>
#include "../common/runtime.h"
#include <arm_neon.h>
#include "constants.h"

#include "Voice.h"
#include "Limiter.h"
#include "Comb.h"
#include "Models.h"

#include "runtime.h"


#ifndef fmin
#define fmin(a,b) ((a)<(b)?(a):(b))
#endif
#ifndef fmax
#define fmax(a,b) ((a)>(b)?(a):(b))
#endif
#ifndef isfinite
#define isfinite(x) ((x)==(x) && (x)!=(1.0/0.0) && (x)!=-(1.0/0.0))
#endif

/**
 * RipplerX Synth Engine
 * Optimized polyphonic resonator synthesizer with ARM NEON vectorization
 * Manages voice allocation, parameter control, and real-time audio rendering
 */
class RipplerX
{
    public:
    /*===========================================================================*/
    /* Public Data Structures/Types. */
    /*===========================================================================*/

    /**
     * Cached parameter structure for batch voice updates
     * Reduces array indexing overhead during parameter propagation
     */
    struct CachedParameters {
        // Resonator A parameters
        bool  resA_on;
        int   resA_model;
        float resA_partials;
        float resA_decay;
        float resA_damp;
        float resA_tone;
        float resA_hit;
        float resA_rel;
        float resA_inharm;
        float resA_cut;
        float resA_radius;
        float resA_vel_decay;
        float resA_vel_hit;
        float resA_vel_inharm;

        // Resonator B parameters
        bool  resB_on;
        int   resB_model;
        float resB_partials;
        float resB_decay;
        float resB_damp;
        float resB_tone;
        float resB_hit;
        float resB_rel;
        float resB_inharm;
        float resB_cut;
        float resB_radius;
        float resB_vel_decay;
        float resB_vel_hit;
        float resB_vel_inharm;

        // Noise parameters
        int   noise_mode;
        float noise_freq;
        float noise_q;
        float noise_att;
        float noise_dec;
        float noise_sus;
        float noise_rel;
        float noise_vel_freq;
        float noise_vel_q;

        // Pitch parameters
        float pitch_coarseA;
        float pitch_coarseB;
        float pitch_fineA;
        float pitch_fineB;

        // Coupling parameters
        bool  couple;
        float ab_split;
    };

    /*===========================================================================*/
    /* Lifecycle Methods. */
    /*===========================================================================*/

    RipplerX() = default;
    ~RipplerX() = default;

    inline int8_t Init(const unit_runtime_desc_t * desc) {
        // Validate runtime configuration
        if (desc->samplerate != c_sampleRate)
            return k_unit_err_samplerate + 10; // 10 = samplerate fail
        if (desc->output_channels != 1 && desc->output_channels != 2) {
            // 20 = geometry fail, plus output_channels for debug
            return k_unit_err_geometry + 20 + (desc->output_channels & 0x7F);
        }
        if (!desc->get_num_sample_banks) return k_unit_err_samplebank + 30; // 30 = missing get_num_sample_banks
        if (!desc->get_num_samples_for_bank) return k_unit_err_samplebank + 31; // 31 = missing get_num_samples_for_bank
        if (!desc->get_sample) return k_unit_err_samplebank + 32; // 32 = missing get_sample

        // Initialize synth resources (static allocation - no heap)
        // Models are now static; no per-voice setup needed

        // Initialize sample management
        m_sampleBank = 0;
        m_sampleNumber = 1;
        m_sampleStart = 0;  // NOTE: not user editable
        m_sampleEnd = 1000;

        // Cache runtime sample access functions
        m_get_num_sample_banks_ptr = desc->get_num_sample_banks;
        m_get_num_samples_for_bank_ptr = desc->get_num_samples_for_bank;
        m_get_sample = desc->get_sample;

        // Load default program
        LoadPreset(Program::Initial);    // this does prepareToPlay
        Reset();

        // Initialize effects
        comb.init(getSampleRate());
        limiter.init(getSampleRate());

        // === Debug: Log parameter initialization ===
        for(int i = 0; i < c_parameterTotal; i++) {
            loggedValues[i] = 6.0f; // marker for changed values
        }
        // === End of debug init ===

        return k_unit_err_none;
    }

    // Unused
    inline void loadDefaultProgramParameters() {
        // Mallet parameters (normalized 0.0-1.0)
        parameters[mallet_mix] = 0.5f;
        parameters[mallet_res] = 0.8f;  // 8/10 normalized
        parameters[mallet_stiff] = 600;

        // Resonator A parameters
        parameters[a_on] = 1;
        parameters[a_model] = ModelNames::Squared;
        parameters[a_partials] = c_partials[3];
        parameters[a_decay] = 50;  // Increased from 10 for longer sustain
        parameters[a_damp] = 0;     // corresponds to Material
        parameters[a_tone] = 0;
        parameters[a_hit] = 0.26f;
        parameters[a_rel] = 1.0f;
        parameters[a_inharm] = 0.0001f;
        parameters[a_ratio] = 1.0f;
        parameters[a_cut] = 20.0f;
        parameters[a_radius] = 0.5f;
        parameters[a_coarse] = 0.0f;
        parameters[a_fine] = 0.0f;
        parameters[b_on] = 0;
        parameters[b_model] = 0;
        parameters[b_partials] = c_partials[3];
        parameters[b_decay] = 1.0f;
        parameters[b_damp] = 0.0f;   // corresponds to Material
        parameters[b_tone] = 0.0f;
        parameters[b_hit] = 0.26f;
        parameters[b_rel] = 1.0f;
        parameters[b_inharm] = 0.0001f;
        parameters[b_ratio] = 1.0f;
        parameters[b_cut] = 20.0f;
        parameters[b_radius] = 0.5f;
        parameters[b_coarse] = 0.0f;
        parameters[b_fine] = 0.0f;
        parameters[noise_mix] = 0.2f;
        parameters[noise_res] = 0.0f;
        parameters[noise_filter_mode] = 2;
        parameters[noise_filter_freq] = 20;
        parameters[noise_filter_q] = 0.707f;
        parameters[noise_att] = 1.0f;
        parameters[noise_dec] = 500.0f;
        parameters[noise_sus] = 0.0f;
        parameters[noise_rel] = 500.0f;
        parameters[vel_mallet_mix] = 0.0f;
        parameters[vel_mallet_res] = 0.0f;
        parameters[vel_mallet_stiff] = 0.0f;
        parameters[vel_noise_mix] = 0.0f;
        parameters[vel_noise_res] = 0.0f;
        parameters[vel_a_decay] = 0.0f;
        parameters[vel_a_hit] = 0.0f;
        parameters[vel_a_inharm] = 0.0f;
        parameters[vel_b_decay] = 0.0f;
        parameters[vel_b_hit] = 0.0f;
        parameters[vel_b_inharm] = 0.0f;
        parameters[couple] = 0;
        parameters[ab_mix] = 0.5f;
        parameters[ab_split] = 0.01f;
        parameters[gain] = 0.8f;
    }

    inline void Teardown() {
        // Nothing to deallocate - using static allocation
    }

    inline void Reset() {
        clearVoices();
        updateLastModels();
    }

    inline void Resume() {
        // Resume synth operation (e.g., after focus regained)
    }

    inline void Suspend() {
        // Suspend synth operation (e.g., before switching units)
    }

    /*===========================================================================*/
    /* Optimized Real-Time Rendering */
    /*===========================================================================*/

    /**
     * Production-ready, high-performance audio rendering with ARM NEON vectorization.
     * This method incorporates fixes from previous debug versions and is optimized for real-time use.
     * It processes voices in parallel, handling stereo/mono samples, and applies effects.
     */
    inline void Render(float * __restrict outBuffer, size_t frames)
    {
        // Load parameters once per render call
        const bool a_on = (bool)parameters[ProgramParameters::a_on];
        const bool b_on = (bool)parameters[ProgramParameters::b_on];
        const float32_t mallet_mix = parameters[ProgramParameters::mallet_mix];
        const float32_t mallet_res = parameters[ProgramParameters::mallet_res];
        const float32_t vel_mallet_mix = parameters[ProgramParameters::vel_mallet_mix];
        const float32_t vel_mallet_res = parameters[ProgramParameters::vel_mallet_res];
        const float32_t noise_mix = parameters[ProgramParameters::noise_mix];
        const float32_t noise_res = parameters[ProgramParameters::noise_res];
        const float32_t vel_noise_mix = parameters[ProgramParameters::vel_noise_mix];
        const float32_t vel_noise_res = parameters[ProgramParameters::vel_noise_res];
        const float32_t ab_mix = parameters[ProgramParameters::ab_mix];
        const float32_t gain = parameters[ProgramParameters::gain];
        const bool couple = (bool)parameters[ProgramParameters::couple];
        const bool serial = couple; // Alias for clarity

        // Precompute NEON constants (stay in registers across loop)
        const float32x4_t v_zero = vdupq_n_f32(0.0f);
        const float32x4_t v_ab_mix = vdupq_n_f32(ab_mix);
        const float32x4_t v_one_minus_ab_mix = vdupq_n_f32(1.0f - ab_mix);

        // Main rendering loop: each iteration processes 2 stereo frames (4 floats) using NEON.
        for (size_t frame = 0; frame < frames/2; frame++) {
            float32x4_t dirOut = vdupq_n_f32(0.0f);
            float32x4_t resAOut = vdupq_n_f32(0.0f);
            float32x4_t resBOut = vdupq_n_f32(0.0f);
            float32x4_t audioIn = vdupq_n_f32(0.0f);

            // Robust sample loading with bounds checking.
            // This check prevents crashes if a sample is invalid or finishes playing.
            if (m_samplePointer != nullptr && m_sampleEnd > 0 && m_sampleIndex < m_sampleEnd) {
                if (m_sampleChannels == 2) {
                    // Stereo: load 4 samples (2 stereo frames).
                    if (m_sampleIndex + 4 <= m_sampleEnd) {
                        audioIn = vld1q_f32(&m_samplePointer[m_sampleIndex]);
                        m_sampleIndex += 4;
                    } else {
                        // Not enough samples left for a full vector; zero out and mark as finished.
                        audioIn = vdupq_n_f32(0.0f);
                        m_sampleIndex = m_sampleEnd; // Prevent re-entry
                    }
                } else {
                    // Mono: load 2 mono samples and duplicate to create 2 stereo frames ([M1,M1,M2,M2]).
                    if (m_sampleIndex + 2 <= m_sampleEnd) {
                        float32_t m1 = m_samplePointer[m_sampleIndex];
                        float32_t m2 = m_samplePointer[m_sampleIndex + 1];
                        float32x2_t s1 = vdup_n_f32(m1);
                        float32x2_t s2 = vdup_n_f32(m2);
                        audioIn = vcombine_f32(s1, s2);
                        m_sampleIndex += 2;
                    } else if (m_sampleIndex + 1 <= m_sampleEnd) {
                        // Only 1 mono sample left: fill first stereo frame, zero the second.
                        float32_t m1 = m_samplePointer[m_sampleIndex];
                        float32x2_t s1 = vdup_n_f32(m1);
                        float32x2_t s2 = vdup_n_f32(0.0f);
                        audioIn = vcombine_f32(s1, s2);
                        m_sampleIndex = m_sampleEnd;
                    } else {
                        audioIn = vdupq_n_f32(0.0f);
                    }
                }
                audioIn = vmulq_n_f32(audioIn, gain);
            }

            // Process all active voices
            for (size_t i = 0; i < c_numVoices; ++i) {
                Voice& voice = voices[i];
                if (!voice.m_initialized) continue;

                float32x4_t resOut = vdupq_n_f32(0.0f);

                float32_t msample = voice.mallet.process();

                if (msample != 0.0f) {
                    float32_t mallet_mix_vel = fmax(0.0f, fmin(1.0f,
                        mallet_mix + vel_mallet_mix * voice.vel));
                    float32_t mallet_res_vel = fmax(0.0f, fmin(1.0f,
                        mallet_res + vel_mallet_res * voice.vel));

                    // Accumulate direct and resonator paths (uses FMA instructions).
                    dirOut = vmlaq_n_f32(dirOut, vdupq_n_f32(msample), mallet_mix_vel);
                    resOut = vmlaq_n_f32(resOut, vdupq_n_f32(msample), mallet_res_vel);
                }

                // Add input sample only if voice is pressed
                if (voice.isPressed) {
                    resOut = vaddq_f32(resOut, audioIn);
                }

                float32_t nsample = voice.noise.process();

                if (nsample != 0.0f) {
                    float32_t noise_mix_vel = fmax(0.0f, fmin(1.0f,
                        noise_mix + vel_noise_mix * voice.vel));
                    float32_t noise_res_vel = fmax(0.0f, fmin(1.0f,
                        noise_res + vel_noise_res * voice.vel));

                    dirOut = vmlaq_n_f32(dirOut, vdupq_n_f32(nsample), noise_mix_vel);
                    resOut = vmlaq_n_f32(resOut, vdupq_n_f32(nsample), noise_res_vel);
                }

                float32x4_t out_from_a = v_zero; // Store Resonator A's output for serial coupling.

                if (a_on) {
                    float32x4_t out = voice.resA.process(resOut);
                    if (voice.resA.getCut() > c_res_cutoff)
                        out = voice.resA.applyFilter(out);
                    resAOut = vaddq_f32(resAOut, out);
                    out_from_a = out;
                }

                if (b_on) {
                    // Serial coupling: resB input is either resA output or direct resonator input
                    float32x4_t resB_input = (a_on && couple) ? out_from_a : resOut;
                    float32x4_t out = voice.resB.process(resB_input);
                    if (voice.resB.getCut() > c_res_cutoff)
                        out = voice.resB.applyFilter(out);
                    resBOut = vaddq_f32(resBOut, out);
                }

                // Increment frame counter for voice stealing logic. This is a critical fix.
                voice.m_framesSinceNoteOn += 2; // Processed 2 frames in this iteration.
            }

            // Mix resonator outputs based on serial/parallel coupling mode.
            float32x4_t resOut;
            if (a_on && b_on) {
                if (serial) {
                    resOut = resBOut;
                } else {
                    // Parallel mode: blend resA and resB based on ab_mix parameter
                    // resOut = resB * ab_mix + resA * (1 - ab_mix)
                    float32x4_t resB_scaled = vmulq_f32(resBOut, v_ab_mix);
                    float32x4_t resA_scaled = vmulq_f32(resAOut, v_one_minus_ab_mix);
                    resOut = vaddq_f32(resB_scaled, resA_scaled);
                }
            } else {
                // Either A or B (or neither): simple sum
                resOut = vaddq_f32(resAOut, resBOut);
            }

            // Apply final gain and effects chain.
            float32x4_t totalOut = vmlaq_n_f32(dirOut, resOut, gain);
            float32x4_t split = comb.process(totalOut);
            float32x4_t channels = limiter.process(split);

            // Accumulate into output buffer
            float32x4_t old = vld1q_f32(outBuffer);
            channels = vaddq_f32(old, channels);
            vst1q_f32(outBuffer, channels);
            outBuffer += 4;
        }

        // Lightweight safety check for NaN/Inf values in the output buffer.
        // This prevents audio hardware from crashing due to invalid floating point values.
        static int nan_clamp_count = 0;
        float* checkBuf = outBuffer - (frames * 2); // Point back to the start of the buffer for this render call.
        for (size_t i = 0; i < frames * 2; ++i) {
            float v = checkBuf[i];
            if (!isfinite(v)) {
                checkBuf[i] = 0.0f;
                nan_clamp_count++;
            }
        }

        // === Debug logic with improved safety ===
        bool errorDetected = false;

        // --- Define error conditions ---
        const bool isGainInvalid = !isfinite(gain) || gain <= 0.0f;
        const bool isSampleInvalid = m_samplePointer == nullptr;

        if (isGainInvalid || isSampleInvalid) {
            errorDetected = true;
        }

        // --- If any error was detected, switch to debug program and log the state ---
        if (errorDetected) {
            // First, load the Debug program preset to have a clean slate
            setCurrentProgram(Program::Debug);

            // Now, overwrite specific debug parameters with live values
            if (isGainInvalid) {
                 setParameter(c_parameterMalletStiffness, 0); // Use a fixed marker for bad gain
            }

            if (isSampleInvalid) {
                setParameter(c_parameterResonatorNote, 50); // D3 as marker
                setParameter(c_parameterSampleBank, m_sampleBank);
                setParameter(c_parameterSampleNumber, m_sampleNumber - 1);
            }

            // Log other general state for context
            setParameter(c_parameterModel, nan_clamp_count); // Log NaN count in Model slot
            setParameter(c_parameterDecay, getParameterValue(c_parameterNoiseMix)); // Log Noise Mix in Decay slot
        }
        // === End of debug logic ===
    }

    /*===========================================================================*/
    /* Parameter Management */
    /*===========================================================================*/

    /**
     * Set individual parameter with selective voice/model update
     * Uses change flags to minimize unnecessary computation
     */
    inline void setParameter(uint8_t index, int32_t value) {
        bool noiseChanged = false;
        bool pitchChanged = false;
        bool resonatorChangedA = false;
        bool resonatorChangedB = false;
        bool couplingChanged = false;

        if (index >= c_parameterTotal)
            return;

        // === Expanded debug: Check for NaN, out-of-bounds, or invalid model/partials indices after parameter changes
        auto check_for_error_and_debug = [this]() {
            bool invalid = false;
            // Check for NaN in all parameters
            for (int i = 0; i < ProgramParameters::last_param; ++i) {
                float v = parameters[i];
                if (!(v == v)) invalid = true; // NaN
            }
            // Check for out-of-bounds or invalid indices for model/partials - TODO remove later, just for debug
            if (a_b_partials >= c_partialElements * 2)
                invalid = true;
            if (parameters[a_model] < 0 || parameters[a_model] >= c_modelElements * 2)
                invalid = true;
            if (parameters[b_model] < 0 || parameters[b_model] >= c_modelElements * 2)
                invalid = true;
            // Optionally: check for other invalid states (e.g., extreme values)
            if (invalid) {
                setCurrentProgram(Program::Debug);
                // Optionally log or export all parameter values for diagnosis
            }
            // === End of debug checks ===
        };

        switch(index) {
            case c_parameterProgramName:
                if (value < (int32_t)last_program) {
                    setCurrentProgram(value);
                }
                break;

            // case c_parameterGain:
            //     // Precompute gain in dB -> linear conversion
            //     parameters[gain] = fasterpowf(10.0, value / 20.0);
            //     break;

            case c_parameterResonatorNote: {
                // Raise minimum frequency to ensure all models are audible
                // This can happen due to: invalid preset, UI bug, parameter mapping error, or manual edits
                const int32_t min_audible_note = 36; // MIDI C2, typically audible for all models
                if (value < min_audible_note) {
                    m_note = min_audible_note;
                } else {
                    m_note = value;
                }
                break;
            }

            case c_parameterSampleBank:
                if ((size_t)value < c_sampleBankElements)
                    m_sampleBank = value;
                break;

            case c_parameterSampleNumber:
                // Defensive check for out-of-bounds value from host - debug, remove after fix
                if (value <= 0 || value > 128) {
                    setCurrentProgram(Program::Debug);
                    // Log the invalid value to another parameter for inspection
                    parameters[mallet_stiff] = value;
                    // Use a magic number to signify this specific error source
                    a_b_tone = -99;
                    parameters[a_tone] = -9.9f;
                }
                m_sampleNumber = value;
                break;

            case c_parameterMalletResonance:
                // Normalize from 0-10 range to 0.0-1.0
                parameters[mallet_res] = value / 10.0f;
                break;

            case c_parameterMalletStiffness:
                parameters[mallet_stiff] = value;
                break;

            case c_parameterVelocityMalletResonance:
                // Normalize from 0-1000 range to 0.0-1.0
                parameters[vel_mallet_res] = value / 1000.0f;
                break;

            case c_parameterVelocityMalletStifness:
                // Normalize from 0-1000 range to 0.0-1.0
                parameters[vel_mallet_stiff] = value / 1000.0f;
                break;

            case c_parameterModel: {
                // Removed defensive. In case of error should swith to debug program and save the values for inspection. No error should never happen here.
                a_b_model = value;
                if (a_b_model < c_modelElements) {
                    parameters[a_model] = a_b_model;
                    resonatorChangedA = true;
                } else {
                    parameters[b_model] = a_b_model - c_modelElements;
                    resonatorChangedB = true;
                }
                clearVoices();
                break;
            }

            case c_parameterPartials: {
                // Removed defensive. In case of error should swith to debug program and save the values for inspection. No error should never happen here.
                a_b_partials = value;
                if (a_b_partials < c_partialElements) {
                    parameters[a_partials] = c_partials[a_b_partials];
                    resonatorChangedA = true;
                } else {
                    parameters[b_partials] = c_partials[a_b_partials - c_partialElements];
                    resonatorChangedB = true;
                }
                clearVoices();
                break;
            }

            case c_parameterDecay: {
                a_b_decay = value;
                const int32_t maxA = 1000; // original A max
                if (a_b_decay <= maxA) {
                    parameters[a_decay] = a_b_decay;
                    resonatorChangedA = true;
                } else if (a_b_decay <= (maxA * 2)) {
                    parameters[b_decay] = a_b_decay - maxA;
                    resonatorChangedB = true;
                }
                clearVoices();
                break;
            }

            case c_parameterMaterial: {
                const int32_t minA = -10;
                const int32_t maxA = 10;
                const int32_t span = maxA - minA; // 20
                a_b_damp = value;
                if (a_b_damp <= maxA) {
                    parameters[a_damp] = a_b_damp;
                    resonatorChangedA = true;
                } else if (a_b_damp <= (maxA + span)) {
                    parameters[b_damp] = a_b_damp - span;
                    resonatorChangedB = true;
                }
                break;
            }

            case c_parameterTone: {
                const int32_t minA = -10;
                const int32_t maxA = 10;
                const int32_t span = maxA - minA; // 20
                a_b_tone = value;
                if (a_b_tone <= maxA) {
                    parameters[a_tone] = a_b_tone;
                    resonatorChangedA = true;
                } else if (a_b_tone <= (maxA + span)) {
                    parameters[b_tone] = a_b_tone - span;
                    resonatorChangedB = true;
                }
                break;
            }

            case c_parameterHitPosition: {
                const int32_t minA = 2;
                const int32_t maxA = 50;
                const int32_t span = maxA - minA; // 48
                a_b_hit = value;
                if (a_b_hit <= maxA) {
                    parameters[a_hit] = a_b_hit;
                    resonatorChangedA = true;
                } else if (a_b_hit <= (maxA + span)) {
                    parameters[b_hit] = a_b_hit - span;
                    resonatorChangedB = true;
                }
                break;
            }

            case c_parameterRelease: {
                const int32_t maxA = 10;
                const int32_t span = 10;
                a_b_rel = value;
                if (a_b_rel <= maxA) {
                    parameters[a_rel] = a_b_rel;
                    resonatorChangedA = true;
                } else if (a_b_rel <= (maxA + span)) {
                    parameters[b_rel] = a_b_rel - span;
                    resonatorChangedB = true;
                }
                break;
            }

            case c_parameterInharmonic: {
                const int32_t minA = 1;
                const int32_t maxA = 10000;
                const int32_t span = maxA - minA; // 9999
                a_b_inharm = value;
                if (a_b_inharm <= maxA) {
                    parameters[a_inharm] = a_b_inharm;
                    resonatorChangedA = true;
                } else if (a_b_inharm <= (maxA + span)) {
                    parameters[b_inharm] = a_b_inharm - span;
                    resonatorChangedB = true;
                }
                break;
            }

            case c_parameterFilterCutoff: {
                // Value is scaled to fit int16 in header (Hz/2).
                // Convert back to Hz and apply A/B mapping on original range.
                const int32_t minAHz = 20;
                const int32_t maxAHz = 20000;
                const int32_t spanHz = maxAHz - minAHz; // 19980
                a_b_filter = value * 2; // restore original Hz
                if (a_b_filter <= maxAHz) {
                    parameters[a_cut] = a_b_filter;
                    resonatorChangedA = true;
                } else if (a_b_filter <= (maxAHz + spanHz)) {
                    parameters[b_cut] = a_b_filter - spanHz;
                    resonatorChangedB = true;
                }
                break;
            }

            case c_parameterTubeRadius: {
                const int32_t maxA = 10;
                const int32_t span = 10;
                a_b_radius = value;
                if (a_b_radius <= maxA) {
                    parameters[a_radius] = a_b_radius;
                    resonatorChangedA = true;
                } else if (a_b_radius <= (maxA + span)) {
                    parameters[b_radius] = a_b_radius - span;
                    resonatorChangedB = true;
                }
                break;
            }

            case c_parameterCoarsePitch: {
                // JUCE design: a_coarse/b_coarse are semitone offsets, not MIDI notes or frequencies
                // Clamp to musically reasonable range (-48 to +48)
                const int32_t minSemitone = -48;
                const int32_t maxSemitone = 48;
                a_b_coarse = value;
                if (a_b_coarse <= maxSemitone) {
                    parameters[a_coarse] = fmax(fmin(a_b_coarse, maxSemitone), minSemitone);
                    pitchChanged = true;
                } else if (a_b_coarse <= (maxSemitone * 2)) {
                    parameters[b_coarse] = fmax(fmin(a_b_coarse - maxSemitone, maxSemitone), minSemitone);
                    pitchChanged = true;
                }
                break;
            }

            case c_parameterNoiseMix:
                // Normalize from 0-1000 percent range to 0.0-1.0
                parameters[noise_mix] = value / 1000.0f;
                noiseChanged = true;
                break;

            case c_parameterNoiseResonance:
                // Normalize from 0-1000 percent range to 0.0-1.0
                parameters[noise_res] = value / 1000.0f;
                noiseChanged = true;
                break;

            case c_parameterNoiseFilterMode:
                if ((size_t)value < c_noiseFilterModeElements) {
                    parameters[noise_filter_mode] = value;
                } else {
                    loggedValues[6] = value;    // debug log
                    // It seems the UI can send invalid values. Default to a safe value.
                    parameters[noise_filter_mode] = 0; // LP
                }
                noiseChanged = true;
                break;

            case c_parameterNoiseFilterFreq:
                parameters[noise_filter_freq] = value;
                noiseChanged = true;
                break;

            case c_parameterNoiseFilterQ:
                parameters[noise_filter_q] = value;
                noiseChanged = true;
                break;

            default:
                break;
        }

        // Update voices with minimal overhead
        prepareToPlay(noiseChanged, pitchChanged, resonatorChangedA, resonatorChangedB, couplingChanged);
        // no effect if model did not change
        updateLastModels();

        // Defensive: After parameter change, check for error and move to debug if needed
        check_for_error_and_debug();
    }

    /**
     * TODO: Added for robust code, but should not be needed. Remove after debugging.
     * The static mapping is robust, but UI/engine desynchronization, type mismatches,
     * or out-of-range values can still cause silence or display errors.
     * Adding strict clamping, validation, and
     * fallback logic in both the UI and engine will eliminate these issues.
     */
    static inline int nearest_partials_index(int value) {
    int best = 0;
    int min_diff = abs(value - c_partials[0]);
    for (uint32_t i = 1; i < c_partialElements; ++i) {
        int diff = abs(value - c_partials[i]);
        if (diff < min_diff) {
            min_diff = diff;
            best = i;
        }
    }
    return best;
    }
    // === end of debug function ===

    /**
     * Batch parameter propagation with cached structs
     * Reduces array lookups by ~60% compared to per-voice indexing
     */
    inline void prepareToPlay(bool noiseChanged = true, bool pitchChanged = true,
        bool resonatorChangedA = true, bool resonatorChangedB = true,
        bool couplingChanged = true) {

        bool frequencyModelChanged = false;

        // Recalculate models if needed
        if (last_a_model != (int32_t)parameters[a_model]) {
            if (parameters[a_ratio] > 0.0f) {
                if (parameters[a_model] == ModelNames::Beam)
                    recalcBeam(true, parameters[a_ratio]);
                else if (parameters[a_model] == ModelNames::Membrane)
                    recalcMembrane(true, parameters[a_ratio]);
                else if (parameters[a_model] == ModelNames::Plate)
                    recalcPlate(true, parameters[a_ratio]);
            }
            last_a_model = (int32_t)parameters[a_model];
            frequencyModelChanged = true;
        }

        if (last_b_model != (int32_t)parameters[b_model]) {
            if (parameters[b_ratio] > 0.0f) {
                if (parameters[b_model] == ModelNames::Beam)
                    recalcBeam(false, parameters[b_ratio]);
                else if (parameters[b_model] == ModelNames::Membrane)
                    recalcMembrane(false, parameters[b_ratio]);
                else if (parameters[b_model] == ModelNames::Plate)
                    recalcPlate(false, parameters[b_ratio]);
            }
            last_b_model = (int32_t)parameters[b_model];
            frequencyModelChanged = true;
        }

        // Frequencies need update if model, pitch, or coupling changed
        bool updateFreqs = frequencyModelChanged || pitchChanged || couplingChanged;

        auto srate = getSampleRate();

        // ===== OPTIMIZATION: Cache all parameters in struct =====
        // Eliminates repeated array lookups in batch voice update loop
        CachedParameters cached;

        cached.resA_on = (bool)parameters[a_on];
        cached.resA_model = (int)parameters[a_model];
        cached.resA_partials = parameters[a_partials];
        cached.resA_decay = parameters[a_decay];
        cached.resA_damp = parameters[a_damp];
        cached.resA_tone = parameters[a_tone];
        cached.resA_hit = parameters[a_hit];
        cached.resA_rel = parameters[a_rel];
        cached.resA_inharm = parameters[a_inharm];
        cached.resA_cut = parameters[a_cut];
        cached.resA_radius = parameters[a_radius];
        cached.resA_vel_decay = parameters[vel_a_decay];
        cached.resA_vel_hit = parameters[vel_a_hit];
        cached.resA_vel_inharm = parameters[vel_a_inharm];

        cached.resB_on = (bool)parameters[b_on];
        cached.resB_model = (int)parameters[b_model];
        cached.resB_partials = parameters[b_partials];
        cached.resB_decay = parameters[b_decay];
        cached.resB_damp = parameters[b_damp];
        cached.resB_tone = parameters[b_tone];
        cached.resB_hit = parameters[b_hit];
        cached.resB_rel = parameters[b_rel];
        cached.resB_inharm = parameters[b_inharm];
        cached.resB_cut = parameters[b_cut];
        cached.resB_radius = parameters[b_radius];
        cached.resB_vel_decay = parameters[vel_b_decay];
        cached.resB_vel_hit = parameters[vel_b_hit];
        cached.resB_vel_inharm = parameters[vel_b_inharm];

        cached.noise_mode = (int)parameters[noise_filter_mode];
        cached.noise_freq = parameters[noise_filter_freq];
        cached.noise_q = parameters[noise_filter_q];
        cached.noise_att = parameters[noise_att];
        cached.noise_dec = parameters[noise_dec];
        cached.noise_sus = parameters[noise_sus];
        cached.noise_rel = parameters[noise_rel];
        cached.noise_vel_freq = parameters[vel_noise_freq];
        cached.noise_vel_q = parameters[vel_noise_q];

        cached.pitch_coarseA = parameters[a_coarse];
        cached.pitch_coarseB = parameters[b_coarse];
        cached.pitch_fineA = parameters[a_fine];
        cached.pitch_fineB = parameters[b_fine];

        cached.couple = (bool)parameters[couple];
        cached.ab_split = parameters[ab_split];

        // Batch update all voices
        for (size_t i = 0; i < c_numVoices; ++i) {
            Voice& voice = voices[i];

            if (noiseChanged) {
                voice.noise.init(srate, cached.noise_mode, cached.noise_freq,
                               cached.noise_q, cached.noise_att, cached.noise_dec,
                               cached.noise_sus, cached.noise_rel,
                               cached.noise_vel_freq, cached.noise_vel_q);
            }

            if (pitchChanged) {
                voice.setPitch(cached.pitch_coarseA, cached.pitch_coarseB,
                             cached.pitch_fineA, cached.pitch_fineB);
            }

            if (resonatorChangedA) {
                voice.resA.setParams(srate, cached.resA_on, cached.resA_model,
                                   cached.resA_partials, cached.resA_decay,
                                   cached.resA_damp, cached.resA_tone, cached.resA_hit,
                                   cached.resA_rel, cached.resA_inharm, cached.resA_cut,
                                   cached.resA_radius, cached.resA_vel_decay,
                                   cached.resA_vel_hit, cached.resA_vel_inharm);
            }

            if (resonatorChangedB) {
                voice.resB.setParams(srate, cached.resB_on, cached.resB_model,
                                   cached.resB_partials, cached.resB_decay,
                                   cached.resB_damp, cached.resB_tone, cached.resB_hit,
                                   cached.resB_rel, cached.resB_inharm, cached.resB_cut,
                                   cached.resB_radius, cached.resB_vel_decay,
                                   cached.resB_vel_hit, cached.resB_vel_inharm);
            }

            if (couplingChanged) {
                voice.setCoupling(cached.couple, cached.ab_split);
            }

            // Update resonators to apply changes to active voices.
            // The bool argument 'updateFrequencies' controls the expensive coupling calculation,
            // which is only needed if pitch, coupling, or models have changed.
            if (pitchChanged || couplingChanged || frequencyModelChanged || resonatorChangedA || resonatorChangedB) {
                voice.updateResonators(updateFreqs);
            }
        }
    }

    inline int32_t getParameterValue(uint8_t index) const {
        if (index >= c_parameterTotal)
            return 0;

        switch(index) {
            case c_parameterProgramName:             return m_currentProgram;
            // case c_parameterGain:                    return parameters[gain];
            case c_parameterResonatorNote:           return m_note;
            case c_parameterSampleBank:              return m_sampleBank;
            case c_parameterSampleNumber:            return m_sampleNumber;
            case c_parameterMalletResonance:         return (int32_t)(parameters[mallet_res] * 10.0f);  // denormalize 0.0-1.0 to 0-10
            case c_parameterMalletStiffness:          return parameters[mallet_stiff];
            case c_parameterVelocityMalletResonance: return (int32_t)(parameters[vel_mallet_res] * 1000.0f);  // denormalize
            case c_parameterVelocityMalletStifness:  return (int32_t)(parameters[vel_mallet_stiff] * 1000.0f);  // denormalize
            case c_parameterModel:                   return a_b_model;
            case c_parameterPartials:                return a_b_partials;
            case c_parameterDecay:                   return a_b_decay;
            case c_parameterMaterial:                return a_b_damp;
            case c_parameterTone:                    return a_b_tone;
            case c_parameterHitPosition:             return a_b_hit;
            case c_parameterRelease:                 return a_b_rel;
            case c_parameterInharmonic:              return a_b_inharm;
            case c_parameterFilterCutoff:            return (int32_t)(a_b_filter / 2); // return scaled value (Hz/2) to fit int16 in header.
            case c_parameterTubeRadius:              return a_b_radius;
            case c_parameterCoarsePitch:             return a_b_coarse;
            case c_parameterNoiseMix:                return (int32_t)(parameters[noise_mix] * 1000.0f);  // denormalize
            case c_parameterNoiseResonance:          return (int32_t)(parameters[noise_res] * 1000.0f);  // denormalize
            case c_parameterNoiseFilterMode:         return parameters[noise_filter_mode];
            case c_parameterNoiseFilterFreq:         return parameters[noise_filter_freq];
            case c_parameterNoiseFilterQ:            return parameters[noise_filter_q];
            default:                                return 0;
        }
    }

    inline const char *getParameterStrValue(uint8_t index, int32_t value) const {
        // Optional visual cue to distinguish A vs B for extended ranges
        static constexpr bool k_showABMarkersNumeric = true;
        auto fmt_num = [](char* buf, size_t n, const char* prefix, int32_t scaled, int decimals, const char* suffix) {
            if (decimals <= 0) {
                snprintf(buf, n, "%s%d%s", prefix, scaled, (suffix ? suffix : ""));
            } else if (decimals == 1) {
                snprintf(buf, n, "%s%.1f%s", prefix, (double)scaled / 10.0, (suffix ? suffix : ""));
            } else if (decimals == 2) {
                snprintf(buf, n, "%s%.2f%s", prefix, (double)scaled / 100.0, (suffix ? suffix : ""));
            } else if (decimals == 3) {
                snprintf(buf, n, "%s%.3f%s", prefix, (double)scaled / 1000.0, (suffix ? suffix : ""));
            } else {
                snprintf(buf, n, "%s%.4f%s", prefix, (double)scaled / 10000.0, (suffix ? suffix : ""));
            }
        };
        switch (index) {
            case c_parameterSampleBank:
                if ((size_t)value < c_sampleBankElements)
                    return c_sampleBankName[value];
                break;
            case c_parameterProgramName:
                if (value < (int32_t)last_program)
                    return c_programName[value];
                break;
                case c_parameterModel:
                    if (value >= c_modelElements * 2) return "INVALID"; // debug purposes, remove after fix
                    return c_modelName[value];
                case c_parameterPartials:
                    if (value >= c_partialElements * 2) return "INVALID"; // debug purposes, remove after fix
                    return c_partialsName[value];
            case c_parameterNoiseFilterMode:
                if ((size_t)value < c_noiseFilterModeElements) {
                    return c_noiseFilterModeName[value];
                }
                return "---"; // Invalid value

            case c_parameterDecay: {
                if (!k_showABMarkersNumeric) break;
                static char s_numBuf[32];
                const int32_t maxA = 1000; // 0..1000 (0.1 resolution)
                const int32_t span = 1000;
                if (value <= maxA) { fmt_num(s_numBuf, sizeof(s_numBuf), "A:", value, 1, ""); return s_numBuf; }
                if (value <= maxA + span) { fmt_num(s_numBuf, sizeof(s_numBuf), "B:", value - span, 1, ""); return s_numBuf; }
                break;
            }
            case c_parameterMaterial: {
                if (!k_showABMarkersNumeric) break;
                static char s_numBuf[32];
                const int32_t minA = -10, maxA = 10, span = maxA - minA; // 20
                if (value <= maxA) { fmt_num(s_numBuf, sizeof(s_numBuf), "A:", value, 1, ""); return s_numBuf; }
                if (value <= maxA + span) { fmt_num(s_numBuf, sizeof(s_numBuf), "B:", value - span, 1, ""); return s_numBuf; }
                break;
            }
            case c_parameterTone: {
                if (!k_showABMarkersNumeric) break;
                static char s_numBuf[32];
                const int32_t minA = -10, maxA = 10, span = maxA - minA; // 20
                if (value <= maxA) { fmt_num(s_numBuf, sizeof(s_numBuf), "A:", value, 1, ""); return s_numBuf; }
                if (value <= maxA + span) { fmt_num(s_numBuf, sizeof(s_numBuf), "B:", value - span, 1, ""); return s_numBuf; }
                break;
            }
            case c_parameterHitPosition: {
                if (!k_showABMarkersNumeric) break;
                static char s_numBuf[32];
                const int32_t minA = 2, maxA = 50, span = maxA - minA; // 48
                if (value <= maxA) { fmt_num(s_numBuf, sizeof(s_numBuf), "A:", value, 2, ""); return s_numBuf; }
                if (value <= maxA + span) { fmt_num(s_numBuf, sizeof(s_numBuf), "B:", value - span, 2, ""); return s_numBuf; }
                break;
            }
            case c_parameterRelease: {
                if (!k_showABMarkersNumeric) break;
                static char s_numBuf[32];
                const int32_t maxA = 10, span = 10;
                if (value <= maxA) { fmt_num(s_numBuf, sizeof(s_numBuf), "A:", value, 1, ""); return s_numBuf; }
                if (value <= maxA + span) { fmt_num(s_numBuf, sizeof(s_numBuf), "B:", value - span, 1, ""); return s_numBuf; }
                break;
            }
            case c_parameterInharmonic: {
                if (!k_showABMarkersNumeric) break;
                static char s_numBuf[32];
                const int32_t minA = 1, maxA = 10000, span = maxA - minA; // 9999
                if (value <= maxA) { fmt_num(s_numBuf, sizeof(s_numBuf), "A:", value, 4, ""); return s_numBuf; }
                if (value <= maxA + span) { fmt_num(s_numBuf, sizeof(s_numBuf), "B:", value - span, 4, ""); return s_numBuf; }
                break;
            }
            case c_parameterFilterCutoff: {
                if (!k_showABMarkersNumeric) break;
                static char s_numBuf[32];
                // 'value' is scaled (Hz/2) from header; convert for display
                const int32_t minAHz = 20, maxAHz = 20000, spanHz = maxAHz - minAHz; // 19980
                const int32_t hz = value * 2;
                if (hz <= maxAHz) { fmt_num(s_numBuf, sizeof(s_numBuf), "A:", hz, 0, " Hz"); return s_numBuf; }
                if (hz <= maxAHz + spanHz) { fmt_num(s_numBuf, sizeof(s_numBuf), "B:", hz - spanHz, 0, " Hz"); return s_numBuf; }
                break;
            }
            case c_parameterTubeRadius: {
                if (!k_showABMarkersNumeric) break;
                static char s_numBuf[32];
                const int32_t maxA = 10, span = 10;
                if (value <= maxA) { fmt_num(s_numBuf, sizeof(s_numBuf), "A:", value, 1, ""); return s_numBuf; }
                if (value <= maxA + span) { fmt_num(s_numBuf, sizeof(s_numBuf), "B:", value - span, 1, ""); return s_numBuf; }
                break;
            }
            case c_parameterCoarsePitch: {
                if (!k_showABMarkersNumeric) break;
                static char s_numBuf[32];
                const int32_t minA = -480, maxA = 480, span = maxA - minA; // 960
                if (value <= maxA) { fmt_num(s_numBuf, sizeof(s_numBuf), "A:", value, 0, ""); return s_numBuf; }
                if (value <= maxA + span) { fmt_num(s_numBuf, sizeof(s_numBuf), "B:", value - span, 0, ""); return s_numBuf; }
                break;
            }
            default:
                break;
        }
        return nullptr;
    }

    inline const uint8_t * getParameterBmpValue(uint8_t index, int32_t value) const {
        (void)index;
        (void)value;
        // Bitmap support: Not yet implemented
        return nullptr;
    }

    /*===========================================================================*/
    /* MIDI and Gate Control */
    /*===========================================================================*/
    inline void loadConfigureSample() {
        // IMPORTANT: Copy values from sampleWrapper immediately—do NOT store the pointer.
        // sampleWrapper is a temporary provided by runtime and may be freed/reused.
        const sample_wrapper_t* sampleWrapper = GetSample(m_sampleBank, m_sampleNumber - 1);
        // bool sampleValid = false;

        if (sampleWrapper) {
            // Copy critical metadata from temporary sampleWrapper
            m_sampleChannels = sampleWrapper->channels;
            m_sampleFrames = sampleWrapper->frames;
            m_samplePointer = sampleWrapper->sample_ptr;

            // Calculate byte-based indices assuming sample_ptr is interleaved float array
            // m_sampleStart and m_sampleEnd are in thousandths (0-1000)
            // m_sampleFrames is in frames; total samples = frames * channels
            size_t totalSamples = m_sampleFrames * m_sampleChannels;
            m_sampleIndex = (totalSamples * m_sampleStart) / 1000;

            // Fix: m_sampleEnd is an index limit. Do not use the member variable as input ratio,
            // as it gets overwritten with the absolute count, causing exponential growth on subsequent triggers.
            m_sampleEnd = totalSamples;
            // not used. To invalid a sample: m_sampleEnd = 0
            // sampleValid = true;
        }
    }

    inline void NoteOn(uint8_t note, uint8_t velocity) {
        auto srate = getSampleRate();

        // Allocate voice using round-robin
        nvoice = nextVoiceNumber();
        Voice & voice = voices[nvoice];

        m_note = note;
        voice.note = note;

        loadConfigureSample();

        // Initialize voice state (always initialize - synth can work without samples)
        voice.Init();

        // Calculate mallet frequency with velocity sensitivity
        auto mallet_stiff = parameters[ProgramParameters::mallet_stiff];
        auto vel_mallet_stiff = parameters[ProgramParameters::vel_mallet_stiff];
        auto malletFreq = fmin(5000.0, e_expf(fasterlogf(ProgramParameters::mallet_stiff) +
            velocity * vel_mallet_stiff * c_malletStiffnessCorrectionFactor));

        voice.trigger(srate, note, velocity / 127.0f, malletFreq);
    }

    inline void NoteOff(uint8_t note) {
        for (auto& voice : voices) {
            voice.m_gate = false;
            if (voice.note == note || note == 0xFF) {
                voice.release();
            }
        }
        // Don't call Reset() here - let voices ring out during release
    }

    inline void GateOn(uint8_t velocity) {
        NoteOn(m_note, velocity);
    }

    inline void GateOff() {
        NoteOff(m_note);
    }

    inline void AllNoteOff() {
        NoteOff(0xFF);
    }

    inline void PitchBend(uint16_t bend) {
        // Convert MIDI bend (0-0x4000, centered at 0x2000) to fine pitch (-99 to 99)
        parameters[a_fine] = 100.0f * (float)(bend - 0x2000) / 0x2000;        // pitchBendRange = 100
        prepareToPlay(false, true, false, false, false);
    }

    inline void ChannelPressure(uint8_t pressure) { (void)pressure; }
    inline void Aftertouch(uint8_t note, uint8_t aftertouch) { (void)note; (void)aftertouch; }

    inline void LoadPreset(uint8_t idx) {
        setParameter(c_parameterProgramName, idx);
    }

    inline uint8_t getPresetIndex() const {
        return m_currentProgram;
    }

    /*===========================================================================*/
    /* Static Interface */
    /*===========================================================================*/

    static inline const char * getPresetName(uint8_t idx) {
        if (idx < last_program)
            return c_programName[idx];
        return nullptr;
    }

    private:

    /*===========================================================================*/
    /* Private Methods */
    /*===========================================================================*/

    inline const sample_wrapper_t* GetSample(size_t bank, size_t number) {
        if (bank >= m_get_num_sample_banks_ptr()) return nullptr;
        if (number >= m_get_num_samples_for_bank_ptr(bank)) return nullptr;
        return m_get_sample(bank, number);
    }

    inline size_t getSampleRate() const {
        return c_sampleRate;
    }

    size_t nextVoiceNumber();

    inline void setCurrentProgram(int index) {
        clearVoices();
        if (index >= 0 && index < (int)last_program) {
            m_currentProgram = index;
            for (int i = 0; i < ProgramParameters::last_param; ++i) {
                parameters[i] = programs[index][i];
            }
            parameters[a_partials] = (float32_t)c_partials[(int)programs[index][a_partials]];
            parameters[b_partials] = (float32_t)c_partials[(int)programs[index][b_partials]];
        }
        // Precompute gain in dB -> linear conversion
        parameters[gain] = fasterpowf(10.0, parameters[gain] / 20.0);
    }

    inline void updateLastModels() {
        last_a_model = (int32_t)parameters[a_model];
        last_a_partials = (int32_t)parameters[a_partials];
        last_b_model = (int32_t)parameters[b_model];
        last_b_partials = (int32_t)parameters[b_partials];
    }

    inline void clearVoices() {
        for (auto& voice : voices) {
            voice.clear();
        }
    }

    /*===========================================================================*/
    /* Member Variables */
    /*===========================================================================*/

    // Audio processing state (static allocation)
    Voice voices[c_numVoices];

    Comb comb{};
    Limiter limiter{};
    int nvoice = 0;

private:
    // ProgramParameters (instance member - not static)
    float32_t parameters[ProgramParameters::last_param] = {};

    // debug
    float loggedValues[c_parameterTotal];  // marker for debug

    // State variables
    uint32_t a_b_model    = 0;  // Combined A/B model for UI
    uint32_t a_b_partials = 0;  // Combined A/B partials index for UI (need c_partials conversion for actual value)
    uint32_t a_b_decay    = 0;  // Combined A/B decay for UI
    uint32_t a_b_damp     = 0;  // Combined A/B material for UI
    uint32_t a_b_tone     = 0;  // Combined A/B tone for UI
    uint32_t a_b_hit      = 0;  // Combined A/B hit for UI
    uint32_t a_b_rel      = 0;  // Combined A/B release for UI
    uint32_t a_b_filter   = 0;  // Combined A/B filter cutoff for UI
    uint32_t a_b_inharm   = 0;  // Combined A/B inharmonics for UI
    uint32_t a_b_radius   = 0;  // Combined A/B radius for UI
    uint32_t a_b_coarse   = 0;  // Combined A/B coarse for UI
    uint32_t m_currentProgram = 0;
    uint32_t m_note = 60;
    float32_t scale = 1.0f;
    int32_t last_a_model = -1;
    int32_t last_b_model = -1;
    int32_t last_a_partials = -1;
    int32_t last_b_partials = -1;

    // Sample management
    uint8_t m_sampleBank = 0;
    uint8_t m_sampleNumber = 1;
    uint8_t m_sampleChannels = 0;
    size_t m_sampleFrames = 0;
    const float32_t * m_samplePointer = nullptr;
    uint16_t m_sampleStart = 0;  // NOTE: not user editable
    size_t m_sampleIndex = 0;
    size_t m_sampleEnd = 1000;

    // Runtime function pointers
    unit_runtime_get_num_sample_banks_ptr m_get_num_sample_banks_ptr = nullptr;
    unit_runtime_get_num_samples_for_bank_ptr m_get_num_samples_for_bank_ptr = nullptr;
    unit_runtime_get_sample_ptr m_get_sample = nullptr;
};

/*===========================================================================*/