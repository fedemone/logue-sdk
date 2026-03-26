#include <iostream>
#include <iomanip>
#include <cmath>

// Include real Drumlogue OS types before the engine
#include "unit.h"
#include "synth.h"

int main() {
    std::cout << "--- STARTING SCRUTAASTRI DRONE TEST ---\n";

    ScrutaAstri synth;
    unit_runtime_desc_t desc = {};
    desc.samplerate = 48000;
    desc.output_channels = 2;
    synth.Init(&desc);

    // Initial parameters
    synth.setParameter(ScrutaAstri::k_paramProgram, 0);
    synth.setParameter(ScrutaAstri::k_paramOsc1Wave, 0);
    synth.setParameter(ScrutaAstri::k_paramOsc2Wave, 1);
    synth.setParameter(ScrutaAstri::k_paramOsc2Mix, 50);

    // Test LFO 3 VCA Tremolo with EXP_DECAY
    synth.setParameter(ScrutaAstri::k_paramL3Wave, 5);
    synth.setParameter(ScrutaAstri::k_paramL3Rate, 1);
    synth.setParameter(ScrutaAstri::k_paramL3Depth, 100);

    // Crank the distortion to test bounds
    synth.setParameter(ScrutaAstri::k_paramCMOSDist, 100);
    synth.setParameter(ScrutaAstri::k_paramMastrVol, 50);

    std::cout << "Triggering Drone at Note 36...\n";
    synth.NoteOn(36, 127);

    float frame_buffer[2] = {0.0f};

    std::cout << std::fixed << std::setprecision(6);
    std::cout << "Frame | Master Out L | Master Out R\n";
    std::cout << "-----------------------------------\n";

    // Run 32 frames of audio to capture multiple APC (Active Partial Counting) cycles
    for (int i = 0; i < 32; ++i) {
        synth.processBlock(frame_buffer, 1);

        std::cout << std::setw(5) << i << " | "
                  << std::setw(12) << frame_buffer[0] << " | "
                  << std::setw(12) << frame_buffer[1] << "\n";
    }

    std::cout << "\n--- ARCHITECTURE & STABILITY CHECKS ---\n";

    // 1. Check for NaN/Inf in output
    bool nan_detected = false;
    float check_buf[2] = {0.0f};
    for (int i = 0; i < 4800; ++i) {
        synth.processBlock(check_buf, 1);
        if (std::isnan(check_buf[0]) || std::isinf(check_buf[0])) {
            std::cout << "FAIL: NaN/Inf detected at frame " << i + 32 << "\n";
            nan_detected = true;
            break;
        }
    }
    if (!nan_detected) std::cout << "PASS: No NaN/Inf in 4832 frames\n";

    // 2. Output Bounding Check (Soft Clipper Enforcement)
    synth.setParameter(ScrutaAstri::k_paramCMOSDist, 100);
    synth.setParameter(ScrutaAstri::k_paramMastrVol, 100); // 300% headroom via synth.h math
    bool clip_detected = false;
    for (int i = 0; i < 4800; ++i) {
        synth.processBlock(check_buf, 1);
        if (std::fabs(check_buf[0]) > 1.0f) {
            std::cout << "FAIL: Output exceeds [-1,1] at frame " << i << ": " << check_buf[0] << "\n";
            clip_detected = true;
            break;
        }
    }
    if (!clip_detected) std::cout << "PASS: Output stays within [-1, 1] under extreme drive\n";

    // 3. Extended Preset Range Verification (0-95 bounds)
    std::cout << "\n--- PRESET RANGE MODULATION TEST ---\n";
    bool mod_target_stable = true;

    // Cycle through the boundary indices (24, 48, 72, 95)
    int test_presets[] = {24, 48, 72, 95};
    for (int p : test_presets) {
        synth.setParameter(ScrutaAstri::k_paramProgram, p);
        synth.processBlock(check_buf, 1);
        if (std::isnan(check_buf[0]) || std::isinf(check_buf[0])) {
            std::cout << "FAIL: Preset " << p << " caused modulation matrix crash.\n";
            mod_target_stable = false;
        }
    }
    if (mod_target_stable) std::cout << "PASS: Preset ranges 0-95 process safely.\n";

    // 4. High-Freq Filter Modulation Stability Check
    std::cout << "\n--- FILTER STABILITY TEST (High LFO Depth) ---\n";
    ScrutaAstri synth2;
    synth2.Init(&desc);
    synth2.setParameter(ScrutaAstri::k_paramF1Cutoff, 15000);
    synth2.setParameter(ScrutaAstri::k_paramL1Wave, 2);   // LFO_SAW_UP
    synth2.setParameter(ScrutaAstri::k_paramL1Rate, 50);  // mid-rate
    synth2.setParameter(ScrutaAstri::k_paramL1Depth, 100);
    synth2.NoteOn(60, 127);
    bool filter_exploded = false;
    for (int i = 0; i < 4800; ++i) {
        synth2.processBlock(check_buf, 1);
        if (std::isnan(check_buf[0]) || std::isinf(check_buf[0]) || std::fabs(check_buf[0]) > 100.0f) {
            std::cout << "FAIL: Filter exploded at frame " << i << ": " << check_buf[0] << "\n";
            filter_exploded = true;
            break;
        }
    }
    if (!filter_exploded) std::cout << "PASS: Filter stable with full LFO depth\n";

    // 5. Extreme LFO Filter Modulation Stability Check
    std::cout << "\n--- FREQUENCY MATH STABILITY TEST ---\n";
    MorphingFilter test_filter;

    // Simulate extreme LFO modulating cutoff deeply into negative values
    float extreme_negative_hz = -5000.0f;

    // Call set_coeffs. If clamping is commented out inside filter.h, 'g' becomes negative.
    test_filter.set_coeffs(extreme_negative_hz, 1.0f, 48000.0f);

    // Process a dummy audio sample
    float test_out = test_filter.process(0.5f, 0.0f);

    if (std::isnan(test_out) || std::isinf(test_out) || std::fabs(test_out) > 10.0f) {
        std::cout << "FAIL: Filter exploded due to negative frequency math.\n";
        std::cout << "      (This proves the cutoff variable 'hz' MUST be clamped in set_coeffs).\n";
    } else {
        std::cout << "PASS: Frequency bounds clamped safely. No NaN generated.\n";
    }

    // 6. Polivoks testing
    std::cout << "\n--- POLIVOKS FILTER INSTABILITY TEST ---\n";
    PolivoksFilter poli_test;

    bool poli_exploded = false;
    float max_amplitude = 0.0f;

    // Simulate extreme parameters
    float test_q = 0.05f; // In many SVF implementations, approaching 0 equals maximum resonance
    poli_test.drive = 10.0f; // Extreme overdrive pushing the integrators

    for (int i = 0; i < 48000; ++i) {
        // Audio-rate modulation simulation: sweeping cutoff from -5000Hz to 25000Hz
        // This tests both the negative frequency clamp and the Nyquist clamp
        float mod_hz = 10000.0f + (15000.0f * std::sin(i * 0.1f));

        poli_test.set_coeffs(mod_hz, test_q, 48000.0f);

        // Feed a harsh square wave / alternating DC impulse to provoke ringing
        float input_sig = (i % 100 < 50) ? 1.0f : -1.0f;

        float poli_out = poli_test.process(input_sig);

        // Track the maximum amplitude to see if it breaches standard bounds
        if (std::fabs(poli_out) > max_amplitude) {
            max_amplitude = std::fabs(poli_out);
        }

        if (std::isnan(poli_out) || std::isinf(poli_out)) {
            std::cout << "FAIL: Polivoks filter exploded (NaN/Inf) at frame " << i << "\n";
            std::cout << "      Frequency was: " << mod_hz << " Hz\n";
            poli_exploded = true;
            break;
        }
    }

    if (!poli_exploded) {
        std::cout << "PASS: Polivoks remained mathematically stable.\n";
        std::cout << "      Maximum internal amplitude reached: " << max_amplitude << "\n";
        if (max_amplitude > 10.0f) {
            std::cout << "      WARNING: Amplitude is unusually high. Ensure fast_tanh is applied to integrators.\n";
        }
    }

    std::cout << "\n--- TEST COMPLETE ---\n";
    return 0;
}