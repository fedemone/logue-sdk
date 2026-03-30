#pragma once
#include <cstdint>
#include "float_math.h"
#include "unit.h"

// Bring in our Session 1 components
#include "wavetables.h"
#include "oscillator.h"
#include "lfo.h"
#include "filter.h"

constexpr float Q_Limit = 0.707f;
constexpr float Mid_Note_Freq = 69.0f;

class alignas(16) ScrutaAstri {
public:
     enum ParamIndex {
        k_paramProgram = 0, k_paramNote, k_paramOsc1Wave, k_paramOsc2Wave,
        k_paramO2Detune, k_paramO2SubOct, k_paramOsc2Mix, k_paramMastrVol,
        k_paramF1Cutoff, k_paramF1Reso, k_paramF2Cutoff, k_paramF2Reso,
        k_paramL1Wave, k_paramL1Rate, k_paramL1Depth, k_paramL2Wave,
        k_paramL2Rate, k_paramL2Depth, k_paramL3Wave, k_paramL3Rate,
        k_paramL3Depth, k_paramSampRed, k_paramBitRed, k_paramCMOSDist,
        k_paramLast // this one is just a marker
    };

    // Constant Borders
    static constexpr float CLEAN_MOOG_BORDER = 33.0f;
    static constexpr float MOOG_SHERMAN_BORDER = 66.0f;
    static constexpr uint32_t SAMPLE_RATE = 48000;
    static constexpr float SAMPLE_RATE_F = (float)SAMPLE_RATE;

    inline int8_t Init(const unit_runtime_desc_t * desc) {
        if (desc->samplerate != SAMPLE_RATE) return k_unit_err_samplerate;
        Reset();
        return k_unit_err_none;
    }

    inline void Reset() {
        // Zero out memory without destroying default struct values
        osc1.phase = 0.0f;
        osc2.phase = 0.0f;
        lfo1.phase = 0.0f;
        lfo2.phase = 0.0f;
        lfo3.phase = 0.0f;

        filter1.mode = mode_low; // Lowpass
        filter2.mode = mode_low; // Lowpass

        m_srr_counter = 0.0f;
        m_srr_hold_val = 0.0f;
    }

    inline void setParameter(uint8_t index, int32_t value) {
        m_params[index] = value;

        switch(index) {
            case k_paramProgram: {
                // Re-sync LFOs on Program change to act as a Drone Reset
                lfo1.phase = 0.0f;
                lfo2.phase = 0.0f;
                lfo3.phase = 0.0f;

                // Decode ranges:
                // 0-23: Normal
                // 24-47: Osc 1 Reversed
                // 48-71: Osc 2 Reversed
                // 72-95: Both Reversed
                int preset = value % 96;

                m_osc1_dir = (preset >= 24 && preset < 48) || (preset >= 72) ? -1.0f : 1.0f;
                m_osc2_dir = (preset >= 48) ? -1.0f : 1.0f;

                // Force frequency target updates
                updateOscillators();
                break;
            }
            case k_paramNote:
                m_base_hz = 440.0f * fasterpow2f(((float)value - Mid_Note_Freq) / 12.0f);
                updateOscillators();
                break;
            case k_paramO2Detune:
            case k_paramO2SubOct:
                updateOscillators();
                break;
            // -- LFO Rates: Exponential mapping from 0.01Hz to 1000Hz
            // A value of 0 = 0.01Hz (100 seconds per cycle for ADSR sweeps)
            // A value of 50 = 3.16Hz (Standard LFO)
            // A value of 100 = 1000Hz (Audio rate FM/AM)
            case k_paramL1Rate: {
                float hz = 0.01f * fasterpowf(100000.0f, (float)value / 100.0f);
                lfo1.set_rate(hz, SAMPLE_RATE_F);
                break;
            }
            case k_paramL2Rate: {
                float hz = 0.01f * fasterpowf(100000.0f, (float)value / 100.0f);
                lfo2.set_rate(hz, SAMPLE_RATE_F);
                break;
            }
            case k_paramL3Rate: {
                float hz = 0.01f * fasterpowf(100000.0f, (float)value / 100.0f);
                lfo3.set_rate(hz, SAMPLE_RATE_F);
                break;
            }

            // -- LFO Waves (Updated UI maximum to 5 in header.c)
            case k_paramL1Wave: lfo1.wave_type = value % 6; break;
            case k_paramL2Wave: lfo2.wave_type = value % 6; break;
            case k_paramL3Wave: lfo3.wave_type = value % 6; break;

            // -- LFO Depths (0.0 to 1.0)
            case k_paramL1Depth: m_lfo1_depth = (float)value / 100.0f; break;
            case k_paramL2Depth: m_lfo2_depth = (float)value / 100.0f; break;
            case k_paramL3Depth: m_lfo3_depth = (float)value / 100.0f; break;

            // -- FX
            case k_paramSampRed: m_srr_rate = 1.0f - ((float)value / 105.0f); break;
            case k_paramBitRed:  m_brr_steps = fasterpow2f(16.0f - (float)value); break;

            // FIX: The Sherman Destruction Boost
            case k_paramCMOSDist: {
                if (value <= CLEAN_MOOG_BORDER) {
                    m_cmos_gain = 1.0f + ((float)value / (float)CLEAN_MOOG_BORDER);
                    filter1.drive = 0.0f;
                    filter2.drive = 0.0f;
                    m_sherman_asym_base = 0.0f; // Update base
                    m_sherman_makeup = 1.0f;
                } else if (value <= MOOG_SHERMAN_BORDER) {
                    m_cmos_gain = 2.0f;
                    float moog_drive = ((float)(value - CLEAN_MOOG_BORDER) / (float)(MOOG_SHERMAN_BORDER - CLEAN_MOOG_BORDER)) * 2.0f;
                    filter1.drive = moog_drive;
                    filter2.drive = moog_drive;
                    m_sherman_asym_base = 0.0f; // Update base
                    m_sherman_makeup = 1.0f;
                } else {
                    m_cmos_gain = 2.0f;
                    filter1.drive = 2.0f;
                    filter2.drive = 2.0f;
                    // Update base with the parameter calculation
                    m_sherman_asym_base = ((float)(value - MOOG_SHERMAN_BORDER) / (100.0f - MOOG_SHERMAN_BORDER)) * 2.0f;
                    m_sherman_makeup = 1.0f + m_sherman_asym_base * 0.5f;
                }
                break;
            }

            // -- Mixer
            case k_paramOsc2Mix: m_osc2_mix = (float)value / 100.0f; break;
            // FIX: 300% Volume Headroom!
            case k_paramMastrVol: m_master_vol = ((float)value / 100.0f) * 3.0f; break;
            // FIX: Cutoff 10x Trick
            case k_paramF1Cutoff: m_f1_base_hz = (float)value * 10.0f; break;
            case k_paramF2Cutoff: m_f2_base_hz = (float)value * 10.0f; break;
            // -- Filters Base (Resonance 0-100 -> Q 0.707 to 5.0)
            case k_paramF1Reso:   m_f1_q = Q_Limit + ((float)value / 25.0f); break;
            case k_paramF2Reso:   m_f2_q = Q_Limit + ((float)value / 25.0f); break;
        }
    }

    inline void NoteOn(uint8_t note, uint8_t velocity) {
        // Calculate Base Frequency from MIDI Note
        m_base_hz = 440.0f * fasterpow2f(((float)note - Mid_Note_Freq) / 12.0f);
        // Push immediate frequency updates to oscillators
        updateOscillators();
        // Open the drone VCA
        m_drone_target = 1.0f;
    }

    inline void updateOscillators() {
        // Apply direction to Target Osc 1
        m_osc1_target_hz = m_base_hz * m_osc1_dir;

        // Calculate Target for Osc 2
        float detune_hz = (((float)m_params[k_paramO2Detune] / 50.0f) - 1.0f) * 5.0f;
        float osc2_hz = m_base_hz + detune_hz;

        switch (m_params[k_paramO2SubOct]) {
            case 1: osc2_hz *= 0.5f; break;   // -1 Octave
            case 2: osc2_hz *= 0.25f; break;  // -2 Octaves
            case 3: osc2_hz *= 2.0f; break;   // +1 Octave
            default: break;                   // 0 = Unison
        }

        // Apply direction to Target Osc 2
        m_osc2_target_hz = osc2_hz * m_osc2_dir;

        // Immediately set oscillator frequencies so sound starts on the first sample.
        // Zero-crossing updates in the audio loop handle live pitch changes without clicks.
        osc1.set_frequency(m_osc1_target_hz, SAMPLE_RATE_F);
        osc2.set_frequency(m_osc2_target_hz, SAMPLE_RATE_F);
    }

    // The Drumlogue sends this when the sequencer stops!
    inline void AllNoteOff() {
        m_drone_target = 0.0f; // Smoothly fade out the drone
    }

    // Hard stop
    inline void Suspend() {
        m_drone_target = 0.0f;
        m_drone_amp = 0.0f;
    }

    inline void processBlock(float* __restrict main_out, size_t frames) {

        // preset will target different modulation
        int mod_target = m_params[k_paramProgram] % 24;

        // Grab Wavetables from UI
        osc1.current_table = SCRUTAASTRI_WAVETABLES[m_params[k_paramOsc1Wave]];
        osc2.current_table = SCRUTAASTRI_WAVETABLES[m_params[k_paramOsc2Wave]];

        for (size_t i = 0; i < frames; ++i) {

            // 1. HARDWARE DRONE GATE (Smooth ~2ms AR Envelope)
            m_drone_amp += (m_drone_target - m_drone_amp) * 0.01f;

            // 2. CPU SQUELCH: If the hardware is paused and faded out, output silence!
            if (m_drone_amp < 0.0001f && m_drone_target == 0.0f) {
                main_out[i * 2] = 0.0f;
                main_out[i * 2 + 1] = 0.0f;
                continue; // Skip all DSP calculations for this frame!
            }

            // 3. CORE MODULATION SIGNALS
            float l1_val = lfo1.process() * m_lfo1_depth;
            float l2_val = lfo2.process() * m_lfo2_depth;
            float l3_val = lfo3.process() * m_lfo3_depth;

            // 3.1 ZERO-CROSSING FREQUENCY UPDATES
            // Store previous phase states
            float pre_phase1 = osc1.phase;
            float pre_phase2 = osc2.phase;

            // Zero-crossing frequency updates happen after oscillator processing (see below)

            // Apply smooth APC pitch modulation continuously
            if (m_pitch_mod_multiplier != 1.0f) {
                osc1.set_frequency(m_osc1_target_hz * m_pitch_mod_multiplier, SAMPLE_RATE_F);
                osc2.set_frequency(m_osc2_target_hz * m_pitch_mod_multiplier, SAMPLE_RATE_F);
            }

            // 3.2 ACTIVE PARTIAL COUNTING (APC) BLOCK
            // Evaluate complex modulation targets only every 4 samples to save CPU cycles
            if (++m_apc_counter >= APC_FACTOR) {
                m_apc_counter = 0;

                // Reset multipliers/offsets
                m_f1_mod_multiplier = 1.0f;
                m_f2_mod_multiplier = 1.0f;
                m_cmos_mod_multiplier = 1.0f;
                m_srr_mod_offset = 0.0f;
                m_mix_mod_offset = 0.0f;
                m_pitch_mod_multiplier = 1.0f;

                switch (mod_target) {
                    case k_paramNote:
                        // Modulate global pitch by +/- 12 semitones using LFO 1
                        m_pitch_mod_multiplier = fasterpow2f(l1_val);
                        break;
                    case k_paramF1Cutoff:
                        m_f1_mod_multiplier = fasterpow2f(l2_val * 4.0f);
                        break;
                    case k_paramF2Cutoff:
                        m_f2_mod_multiplier = fasterpow2f(l1_val * 4.0f);
                        break;
                    case k_paramCMOSDist:
                        m_cmos_mod_multiplier = 1.0f + (l3_val * 0.5f);
                        break;
                    case k_paramSampRed:
                        m_srr_mod_offset = l1_val * 0.5f; // Modulate Sample Rate Reduction
                        break;
                    case k_paramOsc2Mix:
                        m_mix_mod_offset = l2_val; // Crossfade modulation
                        break;
                    case k_paramO2Detune:
                        // Apply FM via LFO1 to the Target Hz evaluated in the zero-crossing block
                        m_osc2_target_hz *= fasterpow2f(l1_val * 2.0f);
                        break;
                    case k_paramL1Wave: filter1.mode = (filter_mode)(m_params[k_paramL1Wave] % mode_last);
                        break;
                    case k_paramL2Wave: filter2.mode = (filter_mode)(m_params[k_paramL2Wave] % mode_last);
                        break;
                    case k_paramOsc1Wave: {
                            int base_wave1 = m_params[k_paramOsc1Wave];
                            // LFO1 sweeps the wavetable index back and forth
                            int wave1_offset = (int)(l1_val * 20.0f);
                            int final_wave1 = ((base_wave1 + wave1_offset) % NUM_WAVETABLES + NUM_WAVETABLES) % NUM_WAVETABLES;

                            osc1.current_table = SCRUTAASTRI_WAVETABLES[final_wave1];
                        }
                        break;
                    case k_paramOsc2Wave: {
                            int base_wave2 = m_params[k_paramOsc2Wave];
                            // LFO2 sweeps the wavetable index back and forth
                            int wave1_offset = (int)(l2_val * 20.0f);
                            int final_wave2 = ((base_wave2 + wave1_offset) % NUM_WAVETABLES + NUM_WAVETABLES) % NUM_WAVETABLES;

                            osc2.current_table = SCRUTAASTRI_WAVETABLES[final_wave2];
                        }
                        break;
                    default:
                        break;
                }
            }


            // 4. OSCILLATOR MIX
            // Advance oscillators
            float sig1 = osc1.process() * 0.5f;

            // Apply APC mixed modulation offset, constrained 0.0 to 1.0
            float dynamic_mix = fmaxf(0.0f, fminf(1.0f, m_osc2_mix + m_mix_mod_offset));
            float sig2 = osc2.process() * dynamic_mix;
            float mixed_sig = sig1 + sig2;

            // Bidirectional phase wrap detection
            bool osc1_wrapped = (m_osc1_dir > 0.0f) ? (osc1.phase < pre_phase1) : (osc1.phase > pre_phase1);
            if (osc1_wrapped) {
                osc1.set_frequency(m_osc1_target_hz, SAMPLE_RATE_F);
            }

            bool osc2_wrapped = (m_osc2_dir > 0.0f) ? (osc2.phase < pre_phase2) : (osc2.phase > pre_phase2);
            if (osc2_wrapped) {
                osc2.set_frequency(m_osc2_target_hz, SAMPLE_RATE_F);
            }

            // 5. FILTER 1
            float f1_mod_hz = m_f1_base_hz * fasterpow2f(l1_val * 3.0f);
            f1_mod_hz *= m_f1_mod_multiplier;

            filter1.set_coeffs(f1_mod_hz, m_f1_q, SAMPLE_RATE_F);

            // Calculate dynamic asymmetry using the preserved base value
            float dynamic_asym = m_sherman_asym_base + (sig1 * m_asym_mod_depth * 2.0f);

            // Inject the clamped modulated value into the filter state
            filter1.sherman_asym = fmaxf(0.0f, fminf(4.0f, dynamic_asym));

            mixed_sig = filter1.process(mixed_sig, l3_val);

            // 6. THE CRUSH SANDWICH
            // Apply APC offset to SRR rate, clamping to avoid reversed counters
            float dynamic_srr = fmaxf(0.001f, fminf(1.0f, m_srr_rate + m_srr_mod_offset));
            m_srr_counter += dynamic_srr;

            if (m_srr_counter >= 1.0f) {
                m_srr_hold_val = mixed_sig;
                m_srr_counter -= 1.0f;
            }
            mixed_sig = m_srr_hold_val;

            if (m_brr_steps < 65536.0f) {
                mixed_sig = roundf(mixed_sig * m_brr_steps) / m_brr_steps;
            }

            // 7. FILTER 2 - Polivoks
            float f2_mod_hz = m_f2_base_hz * fasterpow2f(l2_val * 3.0f);
            f2_mod_hz *= m_f2_mod_multiplier; // Apply APC multiplier

            filter2.set_coeffs(f2_mod_hz, m_f2_q, SAMPLE_RATE_F);
            filter2.drive = filter1.drive;
            mixed_sig = filter2.process(mixed_sig);
            mixed_sig *= m_sherman_makeup;

            // 8. MASTER VCA & DISTORTION
            if (mod_target == k_paramMastrVol || mod_target == k_paramProgram) {
                mixed_sig *= (1.0f + l3_val);
            }

            float dynamic_cmos = m_cmos_gain * m_cmos_mod_multiplier; // Apply APC multiplier

            // Optional: small DC bias to avoid zero‑crossing stutter
            const float dc_bias = 0.005f;  // adjust to taste (0.001-0.01 works)
            float distorted = mixed_sig * dynamic_cmos + dc_bias;

            // Apply smooth cubic saturation (fast_tanh)
            distorted = fast_tanh( fast_tanh( distorted * 1.2f ) ) * 0.9f;  // defined in filter.h

            // Remove the DC bias (optional - could leave a tiny offset)
            // distorted -= dc_bias;

            // Apply master volume (no additional clipper needed)
            float scaled = distorted * m_master_vol;
            float master_out = scaled;   // fast_tanh already bounds output to ±1

            master_out *= m_drone_amp;

            main_out[i * 2]     = master_out;
            main_out[i * 2 + 1] = master_out;
        }
    }

private:
    int32_t m_params[24] = {0};

    // Core Engine Instances
    WavetableOsc osc1;
    WavetableOsc osc2;
    MorphingFilter filter1;
    PolivoksFilter filter2;
    FastLFO lfo1;
    FastLFO lfo2;
    FastLFO lfo3;


    // Hardware Gate Trackers
    float m_drone_target = 0.0f;
    float m_drone_amp = 0.0f;

    float m_sherman_makeup = 1.0f;

    // Derived State Variables
    float m_base_hz = 65.4f; // Default to Note 36 (C2)
    float m_lfo1_depth = 0.0f;
    float m_lfo2_depth = 0.0f;
    float m_lfo3_depth = 0.0f;

    float m_osc2_mix = 0.5f;
    float m_master_vol = 0.5f;
    float m_asym_mod_depth = 0.0f;

    float m_f1_base_hz = 10000.0f;
    float m_f1_q = Q_Limit;
    float m_f2_base_hz = 10000.0f;
    float m_f2_q = Q_Limit;

    // Filter Asymmetry Base State
    float m_sherman_asym_base = 0.0f;

    // FX State
    float m_srr_counter = 0.0f;
    float m_srr_rate = 1.0f;
    float m_srr_hold_val = 0.0f;
    float m_brr_steps = 65536.0f;
    float m_cmos_gain = 1.0f;

    // Zero-Crossing Target Trackers
    float m_osc1_target_hz = 65.4f;
    float m_osc2_target_hz = 65.4f;

    // Active Partial Counting (APC) Variables
    uint8_t m_apc_counter = 0;
    static constexpr uint8_t APC_FACTOR = 4; // Calculate mod targets every 4th sample

    // Held Modulation State
    float m_f1_mod_multiplier = 1.0f;
    float m_f2_mod_multiplier = 1.0f;
    float m_cmos_mod_multiplier = 1.0f;
    float m_srr_mod_offset = 0.0f;
    float m_mix_mod_offset = 0.0f;
    float m_pitch_mod_multiplier = 1.0f;

    // Playback Direction Multipliers
    float m_osc1_dir = 1.0f;
    float m_osc2_dir = 1.0f;
};