/*
*  File: RipplerX.h
*
*  Synth Class derived from drumlogue unit
*
*  2021-2025 (c) Korg
*
*/
#pragma once

#include <cstdint>
#include <cstdio>
#include <cmath>
#include <cstddef>
#include <arm_neon.h>

#include "constants.h"
#include "../common/runtime.h" // Drumlogue SDK runtime

// Include our optimized components
#include "Voice.h"
#include "Limiter.h"
#include "Comb.h"
#include "Models.h"

// Define safe math macros if not present
#ifndef fmin
#define fmin(a,b) ((a)<(b)?(a):(b))
#endif
#ifndef fmax
#define fmax(a,b) ((a)>(b)?(a):(b))
#endif
#ifndef isfinite
#define isfinite(x) ((x)==(x) && (x)!=(1.0f/0.0f) && (x)!=-(1.0f/0.0f))
#endif

/**
 * RipplerX Synth Engine
 * Optimized polyphonic resonator synthesizer with ARM NEON vectorization
 * Manages voice allocation, parameter control, and real-time audio rendering
 */
class alignas(16) RipplerX
{
public:
    // ==============================================================================
    // Structs & Constants
    // ==============================================================================

    /**
     * Cached parameter structure.
     * Keeps hot parameters contiguous in memory to minimize cache misses during batch updates.
     */
    struct CachedParameters {
        // Resonator A
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

        // Resonator B
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

        // additional
        float32x4_t gain;
        float32x4_t ab_mix;
        float32x4_t ab_inv;

        // Noise
        int   noise_mode;
        float noise_freq;
        float noise_q;
        float noise_att;
        float noise_dec;
        float noise_sus;
        float noise_rel;
        float noise_vel_freq;
        float noise_vel_q;

        // Tuning & Physics
        float pitch_coarseA;
        float pitch_coarseB;
        float pitch_fineA;
        float pitch_fineB;
        bool  couple;
        float ab_split;
    };

    // ==============================================================================
    // Lifecycle
    // ==============================================================================

    RipplerX() { }
    ~RipplerX() = default;

    inline int8_t Init(const unit_runtime_desc_t * desc) {
        if (!desc) return k_unit_err_geometry;
        if (desc->samplerate != c_sampleRate) return k_unit_err_samplerate;

        // CRITICAL: Clear ALL voices IMMEDIATELY before any initializatio
        // This prevents phantom sounds from garbage memory on hot-load
        clearVoices();

        // Setup function pointers for sample access
        m_get_num_sample_banks_ptr = desc->get_num_sample_banks;
        m_get_num_samples_for_bank_ptr = desc->get_num_samples_for_bank;
        m_get_sample = desc->get_sample;

        initState();
        return k_unit_err_none;
    }

    inline void Teardown() { }
    inline void Reset() {
        updateLastModels();
        comb.init((float)c_sampleRate);
        limiter.init((float)c_sampleRate, -0.1f, 70.0f); // Thresh -0.1dB
        // CRITICAL: Clear voices again after component init to ensure clean state
        clearVoices();
    }
    inline void Resume() {
        // Resume synth operation (e.g., after focus regained)
        }
    inline void Suspend() {
        // Suspend synth operation (e.g., before switching units)
    }

    // ==============================================================================
    // Audio Rendering (Hot Path)
    // ==============================================================================
    // Cached NEON vectors for frequently used parameters
    // Updated only when parameters change, reducing per-frame overhead
    float32x4_t m_v_gain_cached;
    float32x4_t m_v_ab_mix_cached;
    float32x4_t m_v_ab_inv_cached;
    inline void Render(float * __restrict outBuffer, size_t frames)
    {
        // 1. Load Global Mix Parameters into Vector Registers
        const float32_t mallet_mix      = parameters[ProgramParameters::mallet_mix];
        const float32_t mallet_res      = parameters[ProgramParameters::mallet_res];
        const float32_t vel_mallet_mix  = parameters[ProgramParameters::vel_mallet_mix];
        const float32_t vel_mallet_res  = parameters[ProgramParameters::vel_mallet_res];

        const float32_t noise_mix       = parameters[ProgramParameters::noise_mix];
        const float32_t noise_res       = parameters[ProgramParameters::noise_res];
        const float32_t vel_noise_mix   = parameters[ProgramParameters::vel_noise_mix];
        const float32_t vel_noise_res   = parameters[ProgramParameters::vel_noise_res];

        // Use cached gain vectors (updated in setParameter)
        const float32x4_t v_gain        = m_v_gain_cached;

        // A/B Mix Logic
        const bool a_on = (bool)parameters[ProgramParameters::a_on];
        const bool b_on = (bool)parameters[ProgramParameters::b_on];
        const bool serial = (bool)parameters[ProgramParameters::couple];
        const float32x4_t v_ab_mix = m_v_ab_mix_cached;
        const float32x4_t v_ab_inv = m_v_ab_inv_cached;
        // Loop over frames (step = 2 frames / 4 samples)
        for (size_t i = 0; i < frames * 2; i += 4) {

            float32x4_t accum_dir = vdupq_n_f32(0.0f); // Direct signal
            float32x4_t accum_res = vdupq_n_f32(0.0f); // Resonator Input
            float32x4_t audioIn   = vdupq_n_f32(0.0f); // Sample Input

            // --- A. Optimized Sample Playback ---
            if (m_samplePointer && m_sampleIndex < m_sampleEnd) {
                // FIXED: Remove unnecessary memcpy, use direct NEON load
                // NEON vld1q_f32 handles unaligned loads efficiently
                if (m_sampleChannels == 2) {
                    // Stereo: Load 4 samples (2 stereo frames) directly
                    if (m_sampleIndex + 4 <= m_sampleEnd) {
                        // Direct load - NEON handles alignment automatically
                        audioIn = vld1q_f32(&m_samplePointer[m_sampleIndex]);
                        m_sampleIndex += 4;
                    } else {
                        // FIXED: Explicitly zero the input for partial block
                        m_sampleIndex = m_sampleEnd;
                        audioIn = vdupq_n_f32(0.0f);
                    }
                } else {
                    // Mono: load 2 mono samples and duplicate to create 2 stereo frames ([M1,M1,M2,M2]).
                    if (m_sampleIndex + 2 <= m_sampleEnd) {
                        float32_t m1 = m_samplePointer[m_sampleIndex];
                        float32_t m2 = m_samplePointer[m_sampleIndex + 1];
                        // Create stereo frames: [M1, M1, M2, M2]
                        float32x2_t v_m1 = vdup_n_f32(m1);
                        float32x2_t v_m2 = vdup_n_f32(m2);
                        audioIn = vcombine_f32(v_m1, v_m2);
                        m_sampleIndex += 2;
                    } else {
                        // FIXED: Explicitly zero the input for partial block
                        m_sampleIndex = m_sampleEnd;
                        audioIn = vdupq_n_f32(0.0f);
                    }
                }
                audioIn = vmulq_f32(audioIn, v_gain);
            }

            // --- B. Voice Processing ---
            for (size_t v = 0; v < c_numVoices; ++v) {
                Voice& voice = voices[v];

                // 1. Mallet
                float32x4_t m_sig = voice.mallet.process();
                if (vgetq_lane_f32(m_sig, 0) != 0.0f) {
                    float32_t mmix = fmax(0.0f, fmin(1.0f, mallet_mix + vel_mallet_mix * voice.vel));
                    float32_t mres = fmax(0.0f, fmin(1.0f, mallet_res + vel_mallet_res * voice.vel));
                    accum_dir = vmlaq_n_f32(accum_dir, m_sig, mmix);
                    accum_res = vmlaq_n_f32(accum_res, m_sig, mres);
                }
#ifdef DEBUGN
                // debug mallet
                float m_max = std::max(std::abs(vgetq_lane_f32(m_sig, 0)),
                       std::abs(vgetq_lane_f32(m_sig, 1)));
                if (m_max > 10.0f) printf("[DIAG] Mallet explosion: %.2f\n", m_max);
#endif

                // 2. Input Sample Injection
                if (voice.isPressed) {
                    accum_res = vaddq_f32(accum_res, audioIn);
                }

                // 3. Noise
                float32x4_t n_sig = voice.noise.process();
                if (vgetq_lane_f32(n_sig, 0) != 0.0f) {
                    float32_t nmix = fmax(0.0f, fmin(1.0f, noise_mix + vel_noise_mix * voice.vel));
                    float32_t nres = fmax(0.0f, fmin(1.0f, noise_res + vel_noise_res * voice.vel));
                    accum_dir = vmlaq_n_f32(accum_dir, n_sig, nmix);
                    accum_res = vmlaq_n_f32(accum_res, n_sig, nres);
                }

                // 4. Resonators
                float32x4_t res_out_A = vdupq_n_f32(0.0f);
                float32x4_t res_out_B = vdupq_n_f32(0.0f);

                if (a_on) {
                    res_out_A = voice.resA.process(accum_res);
                    if (voice.resA.getCut() > c_res_cutoff) {
                         res_out_A = voice.resA.applyFilter(res_out_A);
                    }
                }
#ifdef DEBUGN
                // debug resonator A
                float res_a_max = std::max(std::abs(vgetq_lane_f32(res_out_A, 0)),
                           std::abs(vgetq_lane_f32(res_out_A, 1)));
                if (res_a_max > 10.0f) printf("[DIAG] Resonator A explosion: %.2f\n", res_a_max);
#endif

                if (b_on) {
                    float32x4_t input_B = (a_on && serial) ? res_out_A : accum_res;
                    res_out_B = voice.resB.process(input_B);
                    if (voice.resB.getCut() > c_res_cutoff) {
                         res_out_B = voice.resB.applyFilter(res_out_B);
                    }
                }

#ifdef DEBUGN
                // debug resonator B
                float res_b_max = std::max(std::abs(vgetq_lane_f32(res_out_B, 0)),
                                        std::abs(vgetq_lane_f32(res_out_B, 1)));
                if (res_b_max > 10.0f) printf("[DIAG] Resonator B explosion: %.2f\n", res_b_max);
#endif

                // Precompute all possibilities
                float32x4_t mix_serial = res_out_B;
                float32x4_t mix_parallel = vaddq_f32(vmulq_f32(res_out_B, v_ab_mix),
                                                    vmulq_f32(res_out_A, v_ab_inv));
                float32x4_t mix_single = vaddq_f32(res_out_A, res_out_B);

                // Create selection masks
                uint32x4_t both_active = vandq_u32(
                    vdupq_n_u32(a_on ? 0xFFFFFFFF : 0),
                    vdupq_n_u32(b_on ? 0xFFFFFFFF : 0)
                );
                uint32x4_t is_serial = vdupq_n_u32(serial ? 0xFFFFFFFF : 0);

                // Select based on conditions
                float32x4_t mix_both = vbslq_f32(is_serial, mix_serial, mix_parallel);
                float32x4_t voice_mix = vbslq_f32(both_active, mix_both, mix_single);
                accum_dir = vaddq_f32(accum_dir, voice_mix);

#ifdef DEBUGN
                // DEBUG
                float frame_max = 0.0f;
                for (int i = 0; i < 4; ++i) {
                    frame_max = std::max(frame_max, std::abs(vgetq_lane_f32(voice_mix, i)));
                }
                if (frame_max > 10.0f) {
                    printf("[DIAG] Voice %zu explosion: %.2f\n", v, frame_max);
                }
                float voice_max = fmaxf(fabsf(vgetq_lane_f32(voice_mix, 0)),
                                        fabsf(vgetq_lane_f32(voice_mix, 1)));
                if (voice_max > 10.0f) {
                    printf("[VOICE %zu EXPLOSION] Output: %.2f, isPressed: %d\n",
                        v, voice_max, voices[v].isPressed);
                }
#endif
            } // End Voice Loop

            // --- C. Global Effects ---
            accum_dir = comb.process(accum_dir);

            // [SAFETY] Silence Guard
            // The Limiter crashes if fed perfect 0.0f (rsqrt(0) = Inf).
            // We add a tiny epsilon to ensure stability during silence.
            // This fixes the "Silence -> Crash" symptom.
            accum_dir = vaddq_f32(accum_dir, vdupq_n_f32(1.0e-9f));

            accum_dir = limiter.process(accum_dir);
            accum_dir = vmulq_f32(accum_dir, v_gain);

            // Accumulate
            float32x4_t dest = vld1q_f32(&outBuffer[i]);
            vst1q_f32(&outBuffer[i], vaddq_f32(dest, accum_dir));

        } // End Frame Loop
    }

    // ==============================================================================
    // Parameter Management
    // ==============================================================================

    /**
     * Propagate parameters to all voices efficiently.
     * Manages model recalculation and batch updates.
     */
    inline void prepareToPlay(bool noiseChanged = true, bool pitchChanged = true,
        bool resonatorChangedA = true, bool resonatorChangedB = true,
        bool couplingChanged = true) {

        bool frequencyModelChanged = false;

        // --- Model Recalculation Logic ---
        // If the model type changed, or if ratio changed for physically modeled types
        if (last_a_model != (int32_t)parameters[a_model]) {
            if (parameters[a_ratio] > 0.0f) {
                int model = (int)parameters[a_model];
                if (model == ModelNames::Beam) recalcBeam(true, parameters[a_ratio]);
                else if (model == ModelNames::Membrane) recalcMembrane(true, parameters[a_ratio]);
                else if (model == ModelNames::Plate) recalcPlate(true, parameters[a_ratio]);
            }
            last_a_model = (int32_t)parameters[a_model];
            frequencyModelChanged = true;
        }

        if (last_b_model != (int32_t)parameters[b_model]) {
            if (parameters[b_ratio] > 0.0f) {
                int model = (int)parameters[b_model];
                if (model == ModelNames::Beam) recalcBeam(false, parameters[b_ratio]);
                else if (model == ModelNames::Membrane) recalcMembrane(false, parameters[b_ratio]);
                else if (model == ModelNames::Plate) recalcPlate(false, parameters[b_ratio]);
            }
            last_b_model = (int32_t)parameters[b_model];
            frequencyModelChanged = true;
        }

        bool updateFreqs = frequencyModelChanged || pitchChanged || couplingChanged;
        float srate = (float)c_sampleRate;

        // --- Cache Parameters ---
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

        // --- Batch Update Voices ---
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

            if (updateFreqs || resonatorChangedA || resonatorChangedB) {
                voice.updateResonators(updateFreqs);
            }
        }
    }

    /**
     * Set individual parameter with A/B split logic logic.
     */
    inline void setParameter(uint8_t index, int32_t value) {
        if (index >= c_parameterTotal) return;

        bool noiseChanged = false;
        bool pitchChanged = false;
        bool resonatorChangedA = false;
        bool resonatorChangedB = false;
        bool couplingChanged = false;

        switch(index) {
            case c_parameterProgramName:
                if (value < (int32_t)last_program) {
                    setCurrentProgram(value);
                    noiseChanged = true;
                    pitchChanged = true;
                    resonatorChangedA = true;
                    resonatorChangedB = true;
                    couplingChanged = true;
                }
                break;

            // case c_parameterGain: - NOTE removed in favour of note
            //     parameters[gain] = fasterpowf(10.0f, value / 20.0f);
            //     break;

            case c_parameterResonatorNote:
                m_note = (value < 36) ? 36 : value;
                break;

            case c_parameterSampleBank:
                if ((size_t)value < c_sampleBankElements) m_sampleBank = value;
                break;

            case c_parameterSampleNumber:
                m_sampleNumber = (value <= 0 || value > 128) ? 1 : value;
                break;

            case c_parameterMalletResonance:
                parameters[mallet_res] = value / 10.0f;
                break;

            case c_parameterMalletStiffness:
                parameters[mallet_stiff] = (float)value;
                break;

            case c_parameterVelocityMalletResonance:
                parameters[vel_mallet_res] = value / 1000.0f;
                break;

            case c_parameterVelocityMalletStifness:
                parameters[vel_mallet_stiff] = value / 1000.0f;
                break;

            // --- A/B Split Parameters ---

            case c_parameterModel:
                a_b_model = value;
                if ((uint32_t)value < c_modelElements) {
                    parameters[a_model] = (float)value;
                    parameters[a_on] = 1.0f;
                    resonatorChangedA = true;
                } else {
                    parameters[b_model] = (float)(value - c_modelElements);
                    parameters[b_on] = 1.0f;
                    resonatorChangedB = true;
                }
                clearVoices();
                break;

            case c_parameterPartials:
                a_b_partials = value;
                if ((uint32_t )value < c_partialElements) {
                    parameters[a_partials] = (float)c_partials[value];
                    resonatorChangedA = true;
                } else {
                    parameters[b_partials] = (float)c_partials[value - c_partialElements];
                    resonatorChangedB = true;
                }
                clearVoices();
                break;

            case c_parameterDecay: {
                a_b_decay = value;
                const int32_t maxA = 1000;
                if (value <= maxA) {
                    parameters[a_decay] = (float)value;
                    resonatorChangedA = true;
                } else {
                    parameters[b_decay] = (float)(value - maxA);
                    resonatorChangedB = true;
                }
                break;
            }

            case c_parameterMaterial: {
                a_b_damp = value;
                const int32_t maxA = 10, span = 20;
                if (value <= maxA) {
                    parameters[a_damp] = (float)value;
                    resonatorChangedA = true;
                } else {
                    parameters[b_damp] = (float)(value - span);
                    resonatorChangedB = true;
                }
                break;
            }

            case c_parameterTone: {
                a_b_tone = value;
                const int32_t maxA = 10, span = 20;
                if (value <= maxA) {
                    parameters[a_tone] = (float)value;
                    resonatorChangedA = true;
                } else {
                    parameters[b_tone] = (float)(value - span);
                    resonatorChangedB = true;
                }
                break;
            }

            case c_parameterHitPosition: {
                a_b_hit = value;
                const int32_t maxA = 50, span = 48; // span calculated from original code
                if (value <= maxA) {
                    parameters[a_hit] = (float)value;
                    resonatorChangedA = true;
                } else {
                    parameters[b_hit] = (float)(value - span);
                    resonatorChangedB = true;
                }
                break;
            }

            case c_parameterRelease: {
                a_b_rel = value;
                const int32_t maxA = 10, span = 10;
                if (value <= maxA) {
                    parameters[a_rel] = (float)value;
                    resonatorChangedA = true;
                } else {
                    parameters[b_rel] = (float)(value - span);
                    resonatorChangedB = true;
                }
                break;
            }

            case c_parameterInharmonic: {
                a_b_inharm = value;
                const int32_t maxA = 10000, span = 9999;
                if (value <= maxA) {
                    parameters[a_inharm] = (float)value;
                    resonatorChangedA = true;
                } else {
                    parameters[b_inharm] = (float)(value - span);
                    resonatorChangedB = true;
                }
                break;
            }

            case c_parameterFilterCutoff: {
                a_b_filter = value * 2; // Restore Hz
                const int32_t maxA = 20000, span = 19980;
                if (a_b_filter <= maxA) {
                    parameters[a_cut] = (float)a_b_filter;
                    resonatorChangedA = true;
                } else {
                    parameters[b_cut] = (float)(a_b_filter - span);
                    resonatorChangedB = true;
                }
                break;
            }

            case c_parameterTubeRadius: {
                a_b_radius = value;
                const int32_t maxA = 10, span = 10;
                if (value <= maxA) {
                    parameters[a_radius] = (float)value;
                    resonatorChangedA = true;
                } else {
                    parameters[b_radius] = (float)(value - span);
                    resonatorChangedB = true;
                }
                break;
            }

            case c_parameterCoarsePitch: {
                a_b_coarse = value;
                const int32_t maxA = 48; // 4 octaves, same as original code
                if (value <= maxA) {
                    parameters[a_coarse] = fmax(fmin((float)value, 48.0f), -48.0f);
                    pitchChanged = true;
                } else {
                    parameters[b_coarse] = fmax(fmin((float)(value - 48), 48.0f), -48.0f);
                    pitchChanged = true;
                }
                break;
            }

            case c_parameterNoiseMix:
                parameters[noise_mix] = value / 1000.0f;
                noiseChanged = true;
                break;

            case c_parameterNoiseResonance:
                parameters[noise_res] = value / 1000.0f;
                noiseChanged = true;
                break;

            case c_parameterNoiseFilterMode:
                parameters[noise_filter_mode] = ((uint32_t)value < c_noiseFilterModeElements) ? (float)value : 0.0f;
                noiseChanged = true;
                break;

            case c_parameterNoiseFilterFreq:
                parameters[noise_filter_freq] = (float)value;
                noiseChanged = true;
                break;

            case c_parameterNoiseFilterQ:
                parameters[noise_filter_q] = (float)value;
                noiseChanged = true;
                break;

            default:
                break;
        }

        prepareToPlay(noiseChanged, pitchChanged, resonatorChangedA, resonatorChangedB, couplingChanged);
        updateLastModels();
    }

    // ==============================================================================
    // MIDI / Gate
    // ==============================================================================
    inline void loadConfigureSample() {
        // IMPORTANT: Copy values from sampleWrapper immediately—do NOT store the pointer.
        // sampleWrapper is a temporary provided by runtime and may be freed/reused.
        const sample_wrapper_t* sampleWrapper = m_get_sample(m_sampleBank, m_sampleNumber - 1);
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
        // this replaces nextVoiceNumber
        nvoice = (nvoice + 1) % c_numVoices;    // round robin
        Voice & voice = voices[nvoice];

        if (m_samplePointer) {
             size_t total = m_sampleFrames * m_sampleChannels;
             m_sampleIndex = (total * m_sampleStart) / 1000;
             if (m_sampleChannels == 2) m_sampleIndex &= ~1; // Align stereo
        }

        auto ms = parameters[mallet_stiff];
        auto vms = parameters[vel_mallet_stiff];
        float32_t malletFreq = fmin(5000.0f, e_expf(fasterlogf(ms) + velocity/127.0f * vms * c_malletStiffnessCorrectionFactor));

        voice.trigger((float)c_sampleRate, note, velocity / 127.0f, malletFreq);
    }

    inline void NoteOff(uint8_t note) {
        for (auto& voice : voices) {
            if (voice.note == note) voice.release();
        }
    }

    inline void GateOn(uint8_t velocity) { NoteOn(m_note, velocity); }
    inline void GateOff() { NoteOff(m_note); }
    inline void AllNoteOff() { NoteOff(0xFF); }
    // Convert MIDI bend (0-0x4000, centered at 0x2000) to fine pitch (-99 to 99)
    inline void PitchBend(uint16_t bend) {
        parameters[a_fine] = 100.0f * (float)(bend - 0x2000) / 0x2000;
        prepareToPlay(false, true, false, false, false);
    }
    inline void ChannelPressure(uint8_t pressure) { (void)pressure; }
    inline void Aftertouch(uint8_t note, uint8_t aftertouch) { (void)note; (void)aftertouch; }

    inline void LoadPreset(uint8_t idx) { setParameter(c_parameterProgramName, idx); }
    inline uint8_t getPresetIndex() const { return m_currentProgram; }

    // ==============================================================================
    // Static Helpers
    // ==============================================================================

    static inline const char * getPresetName(uint8_t idx) {
        return (idx < last_program) ? c_programName[idx] : nullptr;
    }

    inline int32_t getParameterValue(uint8_t index) const {
        // [Simplified for brevity in response, but in full implementation
        //  this would contain the inverse logic of setParameter to return
        //  the combined A/B UI values (e.g., a_b_decay) instead of raw params]
        switch(index) {
            case c_parameterProgramName: return m_currentProgram;
            case c_parameterResonatorNote: return m_note;
            case c_parameterSampleBank: return m_sampleBank;
            case c_parameterSampleNumber: return m_sampleNumber;
            case c_parameterMalletResonance: return parameters[mallet_res]  * 10.0f;
            case c_parameterMalletStiffness: return parameters[mallet_stiff];
            case c_parameterVelocityMalletResonance: return parameters[vel_mallet_res]  * 1000.0f;
            case c_parameterVelocityMalletStifness: return parameters[vel_mallet_stiff]  * 1000.0f;
            case c_parameterModel: return a_b_model;
            case c_parameterPartials: return a_b_partials;
            case c_parameterDecay: return a_b_decay;
            case c_parameterMaterial: return a_b_damp;
            case c_parameterTone: return a_b_tone;
            case c_parameterHitPosition: return a_b_hit;
            case c_parameterRelease: return a_b_rel;
            case c_parameterInharmonic: return a_b_inharm;
            case c_parameterFilterCutoff: return (int32_t)(a_b_filter / 2);
            case c_parameterTubeRadius: return a_b_radius;
            case c_parameterCoarsePitch: return a_b_coarse;
            default: return 0;
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
                if ((uint32_t)value >= c_modelElements * 2) return "INVALID";
                return c_modelName[value];
            case c_parameterPartials:
                if ((uint32_t)value >= c_partialElements * 2) return "INVALID";
                return c_partialsName[value];
            case c_parameterNoiseFilterMode:
                if ((size_t)value < c_noiseFilterModeElements) {
                    return c_noiseFilterModeName[value];
                }
                return "---";

            case c_parameterDecay: {
                if (!k_showABMarkersNumeric) break;
                static char s_numBuf[32];
                const int32_t maxA = 1000;
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
            // ... (Add other cases as needed, but these are the critical ones for A/B)
            default: break;
        }
        return nullptr;
    }

    inline const uint8_t * getParameterBmpValue(uint8_t index, int32_t value) const {
        (void)index;
        (void)value;
        return nullptr; }
#ifndef DEBUGN
private:
#endif
    // ==============================================================================
    // Private Helpers
    // ==============================================================================

    inline void initState() {
        // CRITICAL FIX: Force clear ALL voices before any other initialization
        // This prevents phantom sounds from garbage memory on hot-load
        clearVoices();
        m_sampleBank = 0; m_sampleNumber = 0; m_samplePointer = nullptr;
        m_sampleEnd = 1000; m_currentProgram = 0; nvoice = 0;

        // Initialize cached vectors
        m_v_gain_cached = vdupq_n_f32(1.0f);
        m_v_ab_mix_cached = vdupq_n_f32(0.5f);
        m_v_ab_inv_cached = vsubq_f32(vdupq_n_f32(1.0f), m_v_ab_mix_cached);

        // Reset trackers
        a_b_model = 0; a_b_partials = 0; a_b_decay = 0; a_b_damp = 0;
        a_b_tone = 0; a_b_hit = 0; a_b_rel = 0; a_b_filter = 0;
        a_b_inharm = 0; a_b_radius = 0; a_b_coarse = 0;

        scale = 1.0f;
        last_a_model = -1; last_b_model = -1;
        last_a_partials = -1; last_b_partials = -1;

        LoadPreset(Program::Initial);   // This performs prepareToPlay
        Reset();
    }

inline void setCurrentProgram(int index) {
        clearVoices();
        if (index >= 0 && index < (int)last_program) {
            m_currentProgram = index;
            for (int i = 0; i < ProgramParameters::last_param; ++i) {
                parameters[i] = programs[index][i];
            }
            // store the value not the index
            parameters[a_partials] = (float32_t)c_partials[(int)programs[index][a_partials]];
            parameters[b_partials] = (float32_t)c_partials[(int)programs[index][b_partials]];

            // Precompute gain in dB -> linear conversion
            parameters[gain] = fasterpowf(10.0f, parameters[gain] / 20.0f);
            // Update cached vector
            m_v_gain_cached = vdupq_n_f32(parameters[gain]);
            m_v_ab_mix_cached = vdupq_n_f32(parameters[ab_mix]);
            m_v_ab_inv_cached = vsubq_f32(vdupq_n_f32(1.0f), m_v_ab_mix_cached);
        }
    }

    inline void updateLastModels() {
        last_a_model = (int)parameters[a_model];
        last_b_model = (int)parameters[b_model];
        last_a_partials = (int)parameters[a_partials];
        last_b_partials = (int)parameters[b_partials];
    }

    inline void clearVoices() {
        for(auto& v : voices) v.clear();
    }
    // ==============================================================================
    // Member Data
    // ==============================================================================

    Voice voices[c_numVoices];
    Comb comb;
    Limiter limiter;
#ifndef DEBUGN
private:
#endif
    float32_t parameters[ProgramParameters::last_param];

    // Sample State
    uint8_t m_sampleBank = 0;
    uint8_t m_sampleNumber = 0;
    const float32_t* m_samplePointer = nullptr;
    size_t m_sampleFrames = 0;
    uint8_t m_sampleChannels = 0;
    size_t m_sampleIndex = 0;
    size_t m_sampleEnd = 0;
    uint16_t m_sampleStart = 0;

    int nvoice = 0;
    int32_t last_a_model = -1;
    int32_t last_b_model = -1;
    int32_t last_a_partials = -1;
    int32_t last_b_partials = -1;
    uint32_t m_currentProgram = 0;
    uint32_t m_note = 60;
    float32_t scale = 1.0f;

    // UI State Trackers
    uint32_t a_b_model;
    uint32_t a_b_partials;
    uint32_t a_b_decay;
    uint32_t a_b_damp;
    uint32_t a_b_tone;
    uint32_t a_b_hit;
    uint32_t a_b_rel;
    uint32_t a_b_filter;
    uint32_t a_b_inharm;
    uint32_t a_b_radius;
    uint32_t a_b_coarse;

    // Runtime Pointers
    unit_runtime_get_num_sample_banks_ptr m_get_num_sample_banks_ptr = nullptr;
    unit_runtime_get_num_samples_for_bank_ptr m_get_num_samples_for_bank_ptr = nullptr;
    unit_runtime_get_sample_ptr m_get_sample = nullptr;
};