#pragma once
/*
*  File: RipplerX.h
*
*  Synth Class derived from drumlogue unit
*
*  2021-2025 (c) Korg
*
*/

#include <cstddef>
#include <cstdint>
#include <array>
#include <cstdio>
#include <math.h>
#include "../common/runtime.h"
#include <arm_neon.h>
#include "constants.h"
#include "Voice.h"
#include "Limiter.h"
#include "Comb.h"
#include "Models.h"

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
            return k_unit_err_samplerate;
        if (desc->output_channels != 2)
            return k_unit_err_geometry;

        if (!desc->get_num_sample_banks || !desc->get_num_samples_for_bank || !desc->get_sample)
            return k_unit_err_undef;

        // Initialize synth resources (static allocation - no heap)
        // Models is now a member object, no allocation needed
        for (size_t i = 0; i < c_numVoices; ++i) {
            voices[i].setModels(&models);
        }

        // Initialize sample management
        m_sampleBank = 0;
        m_sampleNumber = 1;
        m_sampleStart = 0;
        m_sampleEnd = 1000;

        // Cache runtime sample access functions
        m_get_num_sample_banks_ptr = desc->get_num_sample_banks;
        m_get_num_samples_for_bank_ptr = desc->get_num_samples_for_bank;
        m_get_sample = desc->get_sample;

        // Load default program
        LoadPreset(0);

        // Initialize effects
        comb.init(getSampleRate());
        limiter.init(getSampleRate());

        // Initialize voice states
        Reset();
        prepareToPlay();

        return k_unit_err_none;
    }

    inline void loadDefaultProgramParameters() {
        parameters[mallet_mix] = 0.0f;
        parameters[mallet_res] = 8;
        parameters[mallet_stiff] = 600;
        parameters[a_on] = 1;
        parameters[a_model] = ModelNames::Squared;
        parameters[a_partials] = c_partials[3];
        parameters[a_decay] = 10;
        parameters[a_damp] = 0;
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
        parameters[b_damp] = 0.0f;
        parameters[b_tone] = 0.0f;
        parameters[b_hit] = 0.26f;
        parameters[b_rel] = 1.0f;
        parameters[b_inharm] = 0.0001f;
        parameters[b_ratio] = 1.0f;
        parameters[b_cut] = 20.0f;
        parameters[b_radius] = 0.5f;
        parameters[b_coarse] = 0.0f;
        parameters[b_fine] = 0.0f;
        parameters[noise_mix] = 0.0f;
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
        parameters[gain] = 0;
    }

    inline void Teardown() {
        // Nothing to deallocate - using static allocation
    }

    inline void Reset() {
        clearVoices();
        resetLastModels();
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
     * High-performance audio rendering with ARM NEON vectorization
     * Processes voices in parallel, with scalar-to-NEON optimization
     */
    inline void Render(float * __restrict outBuffer, size_t frames)
    {
        // Load parameters once per render call (reduces memory pressure)
        const bool a_on = (bool)getParameterValue(Parameters::a_on);
        const bool b_on = (bool)getParameterValue(Parameters::b_on);
        const float32_t mallet_mix = (float32_t)getParameterValue(Parameters::mallet_mix);
        const float32_t mallet_res = (float32_t)getParameterValue(Parameters::mallet_res);
        const float32_t vel_mallet_mix = (float32_t)getParameterValue(Parameters::vel_mallet_mix);
        const float32_t vel_mallet_res = (float32_t)getParameterValue(Parameters::vel_mallet_res);
        const float32_t noise_mix = (float32_t)getParameterValue(Parameters::noise_mix);
        const float32_t noise_res = (float32_t)getParameterValue(Parameters::noise_res);
        const float32_t vel_noise_mix = (float32_t)getParameterValue(Parameters::vel_noise_mix);
        const float32_t vel_noise_res = (float32_t)getParameterValue(Parameters::vel_noise_res);
        const bool serial = (bool)getParameterValue(Parameters::couple);
        const float32_t ab_mix = (float32_t)getParameterValue(Parameters::ab_mix);
        const float32_t gain = (float32_t)getParameterValue(Parameters::gain);
        const bool couple = (bool)getParameterValue(Parameters::couple);

        // Precompute NEON constants (stay in registers across loop)
        const float32x4_t v_zero = vdupq_n_f32(0.0f);
        const float32x4_t v_ab_mix = vdupq_n_f32(ab_mix);
        const float32x4_t v_one_minus_ab_mix = vdupq_n_f32(1.0f - ab_mix);

        // Frame-by-frame rendering: each iteration processes 1 stereo frame (2 floats)
        // Note: using float32x4_t requires processing 2 frames at once, hence frames/2 iterations
        for (size_t frame = 0; frame < frames/2; frame++) {
            float32x4_t dirOut = vdupq_n_f32(0.0f);
            float32x4_t resAOut = vdupq_n_f32(0.0f);
            float32x4_t resBOut = vdupq_n_f32(0.0f);
            float32x4_t audioIn = vdupq_n_f32(0.0f);

            // Load stereo sample input with strict bounds checking
            // m_sampleIndex and m_sampleEnd count float32 samples (interleaved), not frames
            if (m_samplePointer != nullptr && m_sampleIndex < m_sampleEnd) {
                if (m_sampleChannels == 2) {
                    // Stereo: need 4 samples (2 L/R pairs = 2 stereo frames).
                    // Check we won't read past m_sampleEnd.
                    if (m_sampleIndex + 4 <= m_sampleEnd) {
                        audioIn = vld1q_f32(&m_samplePointer[m_sampleIndex]);
                        m_sampleIndex += 4;
                    } else {
                        // Not enough samples left; zero
                        audioIn = vdupq_n_f32(0.0f);
                        m_sampleIndex = m_sampleEnd; // Prevent re-entry
                    }
                } else {
                    // Mono: load 2 mono samples and duplicate each to stereo
                    // Result: [M1, M1, M2, M2] for 2 stereo output frames
                    if (m_sampleIndex + 2 <= m_sampleEnd) {
                        float32_t m1 = m_samplePointer[m_sampleIndex];
                        float32_t m2 = m_samplePointer[m_sampleIndex + 1];
                        float32x2_t s1 = vdup_n_f32(m1);
                        float32x2_t s2 = vdup_n_f32(m2);
                        audioIn = vcombine_f32(s1, s2);
                        m_sampleIndex += 2;
                    } else if (m_sampleIndex + 1 <= m_sampleEnd) {
                        // Only 1 sample left: fill first frame, zero second
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
                float32x4_t resOut = vdupq_n_f32(0.0f);

                // ===== OPTIMIZATION 1: Scalar mallet processing =====
                float32_t msample = voice.m_initialized ? voice.mallet.process() : 0.0f;

                if (msample != 0.0f) {
                    float32_t mallet_mix_vel = fmax(0.0f, fmin(1.0f,
                        mallet_mix + vel_mallet_mix * voice.vel));
                    float32_t mallet_res_vel = fmax(0.0f, fmin(1.0f,
                        mallet_res + vel_mallet_res * voice.vel));

                    // Broadcast and accumulate (FMA instruction)
                    dirOut = vmlaq_n_f32(dirOut, vdupq_n_f32(msample), mallet_mix_vel);
                    resOut = vmlaq_n_f32(resOut, vdupq_n_f32(msample), mallet_res_vel);
                }

                // Add input sample only if voice is pressed
                if (voice.isPressed) {
                    resOut = vaddq_f32(resOut, audioIn);
                }

                // ===== OPTIMIZATION 2: Scalar noise processing =====
                float32_t nsample = voice.noise.process();

                if (nsample != 0.0f) {
                    float32_t noise_mix_vel = fmax(0.0f, fmin(1.0f,
                        noise_mix + vel_noise_mix * voice.vel));
                    float32_t noise_res_vel = fmax(0.0f, fmin(1.0f,
                        noise_res + vel_noise_res * voice.vel));

                    dirOut = vmlaq_n_f32(dirOut, vdupq_n_f32(nsample), noise_mix_vel);
                    resOut = vmlaq_n_f32(resOut, vdupq_n_f32(nsample), noise_res_vel);
                }

                // ===== OPTIMIZATION 3: Resonator processing with optional filtering =====
                float32x4_t out_from_a = v_zero;

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

                // Increment frame counter: we process 2 frames per iteration
                voice.m_framesSinceNoteOn += 2;
            }

            // ===== OPTIMIZATION 4: Mix resonator outputs =====
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

            // ===== OPTIMIZATION 5: Effects and gain with FMA =====
            float32x4_t totalOut = vmlaq_n_f32(dirOut, resOut, gain);
            float32x4_t split = comb.process(totalOut);
            float32x4_t channels = limiter.process(split);

            // Accumulate into output buffer
            float32x4_t old = vld1q_f32(outBuffer);
            channels = vaddq_f32(old, channels);
            vst1q_f32(outBuffer, channels);
            outBuffer += 4;
        }
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

            case c_parameterGain:
                // Precompute gain in dB -> linear conversion
                parameters[gain] = fasterpowf(10.0, value / 20.0);
                break;

            case c_parameterSampleBank:
                if ((size_t)value < c_sampleBankElements)
                    m_sampleBank = value;
                break;

            case c_parameterSampleNumber:
                m_sampleNumber = value;
                break;

            case c_parameterMalletResonance:
                parameters[mallet_res] = value;
                break;

            case c_parameterMalletStifness:
                parameters[mallet_stiff] = value;
                break;

            case c_parameterVelocityMalletResonance:
                parameters[vel_mallet_res] = value;
                break;

            case c_parameterVelocityMalletStifness:
                parameters[vel_mallet_stiff] = value;
                break;

            case c_parameterModel: {
                if ((size_t)value < c_modelElements) {
                    parameters[a_model] = value;
                    resonatorChangedA = true;
                } else if ((size_t)value < (size_t)(2 * c_modelElements)) {
                    parameters[b_model] = value - (int32_t)c_modelElements;
                    resonatorChangedB = true;
                }
                break;
            }

            case c_parameterPartials: {
                if ((size_t)value < c_partialElements) {
                    parameters[a_partials] = c_partials[value];
                    resonatorChangedA = true;
                } else if ((size_t)value < (size_t)(2 * c_partialElements)) {
                    parameters[b_partials] = c_partials[value - (int32_t)c_partialElements];
                    resonatorChangedB = true;
                }
                break;
            }

            case c_parameterDecay: {
                const int32_t maxA = 1000; // original A max
                if (value <= maxA) {
                    parameters[a_decay] = value;
                    resonatorChangedA = true;
                } else if (value <= (maxA * 2)) {
                    parameters[b_decay] = value - maxA;
                    resonatorChangedB = true;
                }
                break;
            }

            case c_parameterMaterial: {
                const int32_t minA = -10;
                const int32_t maxA = 10;
                const int32_t span = maxA - minA; // 20
                if (value <= maxA) {
                    parameters[a_damp] = value;
                    resonatorChangedA = true;
                } else if (value <= (maxA + span)) {
                    parameters[b_damp] = value - span;
                    resonatorChangedB = true;
                }
                break;
            }

            case c_parameterTone: {
                const int32_t minA = -10;
                const int32_t maxA = 10;
                const int32_t span = maxA - minA; // 20
                if (value <= maxA) {
                    parameters[a_tone] = value;
                    resonatorChangedA = true;
                } else if (value <= (maxA + span)) {
                    parameters[b_tone] = value - span;
                    resonatorChangedB = true;
                }
                break;
            }

            case c_parameterHitPosition: {
                const int32_t minA = 2;
                const int32_t maxA = 50;
                const int32_t span = maxA - minA; // 48
                if (value <= maxA) {
                    parameters[a_hit] = value;
                    resonatorChangedA = true;
                } else if (value <= (maxA + span)) {
                    parameters[b_hit] = value - span;
                    resonatorChangedB = true;
                }
                break;
            }

            case c_parameterRelease: {
                const int32_t maxA = 10;
                const int32_t span = 10;
                if (value <= maxA) {
                    parameters[a_rel] = value;
                    resonatorChangedA = true;
                } else if (value <= (maxA + span)) {
                    parameters[b_rel] = value - span;
                    resonatorChangedB = true;
                }
                break;
            }

            case c_parameterInharmonic: {
                const int32_t minA = 1;
                const int32_t maxA = 10000;
                const int32_t span = maxA - minA; // 9999
                if (value <= maxA) {
                    parameters[a_inharm] = value;
                    resonatorChangedA = true;
                } else if (value <= (maxA + span)) {
                    parameters[b_inharm] = value - span;
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
                const int32_t hz = value * 2; // restore original Hz
                if (hz <= maxAHz) {
                    parameters[a_cut] = hz;
                    resonatorChangedA = true;
                } else if (hz <= (maxAHz + spanHz)) {
                    parameters[b_cut] = hz - spanHz;
                    resonatorChangedB = true;
                }
                break;
            }

            case c_parameterTubeRadius: {
                const int32_t maxA = 10;
                const int32_t span = 10;
                if (value <= maxA) {
                    parameters[a_radius] = value;
                    resonatorChangedA = true;
                } else if (value <= (maxA + span)) {
                    parameters[b_radius] = value - span;
                    resonatorChangedB = true;
                }
                break;
            }

            case c_parameterCoarsePitch: {
                const int32_t minA = -480;
                const int32_t maxA = 480;
                const int32_t span = maxA - minA; // 960
                if (value <= maxA) {
                    parameters[a_coarse] = value;
                    pitchChanged = true;
                } else if (value <= (maxA + span)) {
                    parameters[b_coarse] = value - span;
                    pitchChanged = true;
                }
                break;
            }

            case c_parameterNoiseMix:
                parameters[noise_mix] = value;
                noiseChanged = true;
                break;

            case c_parameterNoiseResonance:
                parameters[noise_res] = value;
                noiseChanged = true;
                break;

            case c_parameterNoiseFilterMode:
                if ((size_t)value < c_noiseFilterModeElements) {
                    parameters[noise_filter_mode] = value;
                    noiseChanged = true;
                }
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
        Reset();
    }

    /**
     * Batch parameter propagation with cached structs
     * Reduces array lookups by ~60% compared to per-voice indexing
     */
    inline void prepareToPlay(bool noiseChanged = true, bool pitchChanged = true,
        bool resonatorChangedA = true, bool resonatorChangedB = true,
        bool couplingChanged = true) {

        // Recalculate models if needed
        if (last_a_model != (int32_t)parameters[a_model]) {
            if (parameters[a_ratio] > 0.0f) {
                if (parameters[a_model] == ModelNames::Beam)
                    models.recalcBeam(true, parameters[a_ratio]);
                else if (parameters[a_model] == ModelNames::Membrane)
                    models.recalcMembrane(true, parameters[a_ratio]);
                else if (parameters[a_model] == ModelNames::Plate)
                    models.recalcPlate(true, parameters[a_ratio]);
            }
        }

        if (last_b_model != (int32_t)parameters[b_model]) {
            if (parameters[b_ratio] > 0.0f) {
                if (parameters[b_model] == ModelNames::Beam)
                    models.recalcBeam(false, parameters[b_ratio]);
                else if (parameters[b_model] == ModelNames::Membrane)
                    models.recalcMembrane(false, parameters[b_ratio]);
                else if (parameters[b_model] == ModelNames::Plate)
                    models.recalcPlate(false, parameters[b_ratio]);
            }
        }

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

            if (resonatorChangedA || resonatorChangedB) {
                voice.updateResonators();
            }
        }
    }

    inline int32_t getParameterValue(uint8_t index) const {
        if (index >= c_parameterTotal)
            return 0;

        switch(index) {
            case c_parameterProgramName:             return m_currentProgram;
            case c_parameterGain:                    return parameters[gain];
            case c_parameterSampleBank:              return m_sampleBank;
            case c_parameterSampleNumber:            return m_sampleNumber;
            case c_parameterMalletResonance:         return parameters[mallet_res];
            case c_parameterMalletStifness:          return parameters[mallet_stiff];
            case c_parameterVelocityMalletResonance: return parameters[vel_mallet_res];
            case c_parameterVelocityMalletStifness:  return parameters[vel_mallet_stiff];
            case c_parameterModel:                   return parameters[a_model];
            case c_parameterPartials:                return parameters[a_partials];
            case c_parameterDecay:                   return parameters[a_decay];
            case c_parameterMaterial:                return parameters[a_damp];
            case c_parameterTone:                    return parameters[a_tone];
            case c_parameterHitPosition:             return parameters[a_hit];
            case c_parameterRelease:                 return parameters[a_rel];
            case c_parameterInharmonic:              return parameters[a_inharm];
            case c_parameterFilterCutoff:            return (int32_t)(parameters[a_cut] / 2); // return scaled value (Hz/2)
            case c_parameterTubeRadius:              return parameters[a_radius];
            case c_parameterCoarsePitch:             return parameters[a_coarse];
            case c_parameterNoiseMix:                return parameters[noise_mix];
            case c_parameterNoiseResonance:          return parameters[noise_res];
            case c_parameterNoiseFilterMode:         return parameters[noise_filter_mode];
            case c_parameterNoiseFilterFreq:         return parameters[noise_filter_freq];
            case c_parameterNoiseFilterQ:            return parameters[noise_filter_q];
            default:                                return 0;
        }
    }

    inline const char *getParameterStrValue(uint8_t index, int32_t value) const {
        // Optional visual cue to distinguish A vs B for extended ranges
        static constexpr bool k_showABMarkers = true;
        static constexpr bool k_showABMarkersNumeric = true;
        auto fmt_num = [](char* buf, size_t n, const char* prefix, int32_t scaled, int decimals, const char* suffix) {
            if (decimals <= 0) {
                std::snprintf(buf, n, "%s%d%s", prefix, scaled, (suffix ? suffix : ""));
            } else if (decimals == 1) {
                std::snprintf(buf, n, "%s%.1f%s", prefix, (double)scaled / 10.0, (suffix ? suffix : ""));
            } else if (decimals == 2) {
                std::snprintf(buf, n, "%s%.2f%s", prefix, (double)scaled / 100.0, (suffix ? suffix : ""));
            } else if (decimals == 3) {
                std::snprintf(buf, n, "%s%.3f%s", prefix, (double)scaled / 1000.0, (suffix ? suffix : ""));
            } else {
                std::snprintf(buf, n, "%s%.4f%s", prefix, (double)scaled / 10000.0, (suffix ? suffix : ""));
            }
        };
        switch (index) {
            case c_parameterSampleBank:
                if (value >= 0 && (size_t)value < c_sampleBankElements)
                    return c_sampleBankName[value];
                break;
            case c_parameterProgramName:
                if (value >= 0 && value < (int32_t)last_program)
                    return c_programName[value];
                break;
            case c_parameterModel:
                if (value >= 0) {
                    if ((size_t)value < c_modelElements) {
                        if (!k_showABMarkers) return c_modelName[value];
                        static char s_modelBuf[32];
                        std::snprintf(s_modelBuf, sizeof(s_modelBuf), "A:%s", c_modelName[value]);
                        return s_modelBuf;
                    }
                    if ((size_t)value < (size_t)(2 * c_modelElements)) {
                        int base = value - (int32_t)c_modelElements;
                        if (!k_showABMarkers) return c_modelName[base];
                        static char s_modelBuf[32];
                        std::snprintf(s_modelBuf, sizeof(s_modelBuf), "B:%s", c_modelName[base]);
                        return s_modelBuf;
                    }
                }
                break;
            case c_parameterPartials:
                if (value >= 0) {
                    if ((size_t)value < c_partialElements) {
                        if (!k_showABMarkers) return c_partialsName[value];
                        static char s_partialsBuf[32];
                        std::snprintf(s_partialsBuf, sizeof(s_partialsBuf), "A:%s", c_partialsName[value]);
                        return s_partialsBuf;
                    }
                    if ((size_t)value < (size_t)(2 * c_partialElements)) {
                        int base = value - (int32_t)c_partialElements;
                        if (!k_showABMarkers) return c_partialsName[base];
                        static char s_partialsBuf[32];
                        std::snprintf(s_partialsBuf, sizeof(s_partialsBuf), "B:%s", c_partialsName[base]);
                        return s_partialsBuf;
                    }
                }
                break;
            case c_parameterNoiseFilterMode:
                if (value >= 0 && (size_t)value < c_noiseFilterModeElements)
                    return c_noiseFilterModeName[value];
                break;
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

    inline void NoteOn(uint8_t note, uint8_t velocity) {
        auto srate = getSampleRate();

        // Allocate voice using round-robin
        nvoice = nextVoiceNumber();
        Voice & voice = voices[nvoice];

        m_note = note;
        voice.note = note;

        // Load and configure sample
        // IMPORTANT: Copy values from sampleWrapper immediately—do NOT store the pointer.
        // sampleWrapper is a temporary provided by runtime and may be freed/reused.
        const sample_wrapper_t* sampleWrapper = GetSample(m_sampleBank, m_sampleNumber - 1);
        bool sampleValid = false;

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
            m_sampleEnd = (totalSamples * m_sampleEnd) / 1000;
            sampleValid = true;
        }

        // Initialize voice state
        voice.m_initialized = sampleValid;
        voice.m_gate = true;
        voice.m_framesSinceNoteOn = 0;

        // Calculate mallet frequency with velocity sensitivity
        auto mallet_stiff = (float32_t)getParameterValue(Parameters::mallet_stiff);
        auto vel_mallet_stiff = (float32_t)getParameterValue(Parameters::vel_mallet_stiff);
        auto malletFreq = fmin(5000.0, e_expf(fasterlogf(mallet_stiff) +
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
        Reset();
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
        parameters[a_fine] = (bend - 0x2000) * 100 / 0x2000;
        prepareToPlay(false, true, false, false, false);
    }

    inline void ChannelPressure(uint8_t pressure) { (void)pressure; }
    inline void Aftertouch(uint8_t note, uint8_t aftertouch) { (void)note; (void)aftertouch; }

    inline void LoadPreset(uint8_t idx) {
        setCurrentProgram(idx);
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
        if (index >= 0 && index < (int)last_program) {
            m_currentProgram = index;
            parameters = programs[index];
        }
    }

    inline void resetLastModels() {
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
    Models models;
    Comb comb{};
    Limiter limiter{};
    int nvoice = 0;

    // Parameters (instance member - not static)
    std::array<float32_t, last_param> parameters{};

    // State variables
    uint8_t m_currentProgram = 0;
    uint8_t m_note = 60;
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
    uint16_t m_sampleStart = 0;
    size_t m_sampleIndex = 0;
    size_t m_sampleEnd = 1000;

    // Runtime function pointers
    unit_runtime_get_num_sample_banks_ptr m_get_num_sample_banks_ptr = nullptr;
    unit_runtime_get_num_samples_for_bank_ptr m_get_num_samples_for_bank_ptr = nullptr;
    unit_runtime_get_sample_ptr m_get_sample = nullptr;
};

/*===========================================================================*/