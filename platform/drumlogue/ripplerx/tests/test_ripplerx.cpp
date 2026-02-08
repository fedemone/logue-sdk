#include <iostream>
#include <vector>
#include <cmath>
#include <cstring>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cmath>
#include <cstddef>
#include <new>

// new unit test for proper debugging.
// launch this command for WSL build and run
// mnt/d/Fede/drumlogue/arm-unknown-linux-gnueabihf/bin/arm-unknown-linux-gnueabihf-g++ -static -std=c++17 -O3 -I.. -I /mnt/d/Fede/drumlogue/arm-unknown-linux-gnueabihf/arm-unknown-linux-gnueabihf test_ripplerx.cpp ../ripplerx.cc ../Voice.cc ../Resonator.cc ../Partial.cc ../Waveguide.cc ../Models.cc ../Mallet.cc ../Noise.cc ../Filter.cc ../Envelope.cc -o run_test
// qemu-arm ./run_test

// --- ARCHITECTURE COMPATIBILITY ---
// Compiling for ARM, so use arm_neon.h directly
#include <arm_neon.h>

// Include Synth Engine (pulls in runtime.h and types)
#include "../ripplerx.h"

// Fix macro collision from RipplerX.h/constants.h
#ifdef isfinite
#undef isfinite
#endif

// --- MOCK DRUMLOGUE RUNTIME ---

// Mock the SDK constants
#define k_unit_err_none_mock 0
#define k_unit_err_geometry_mock 1
#define k_unit_err_samplerate_mock 2
// Mock Sample Data
float mock_sample_data[1024];
 uint8_t mock_get_num_sample_banks() { return 1;
 }
 uint8_t mock_get_num_samples_for_bank(uint8_t bank)
 {
 return 1;
 }
 const sample_wrapper_t* mock_get_sample(uint8_t bank, uint8_t sample) {
// In real SDK this returns a wrapper struct, but for this test
// we only need to ensure the pointer check in Render() doesn't crash.
// We will bypass sample playback in the test trigger to focus on Synth engine.
return nullptr;
 }
// --- TEST UTILS ---
bool is_valid_float(float x) { return std::isfinite(x) && std::abs(x) < 100.0f;
// Threshold for "Explosion"
}

void test_dirty_initialization() {
    std::cout << "\n[Test] 1. Dirty Initialization (Hot-Load Simulation)..." << std::endl;

    // Allocate memory filled with garbage (0xFF) to simulate uninitialized state
    alignas(16) char memory[sizeof(RipplerX)];
    std::memset(memory, 0xFF, sizeof(memory));

    // Use placement new to construct object in dirty memory
    RipplerX* synth = new (memory) RipplerX();

    unit_runtime_desc_t desc;
    desc.samplerate = 48000;
    desc.output_channels = 2;
    desc.get_num_sample_banks = mock_get_num_sample_banks;
    desc.get_num_samples_for_bank = mock_get_num_samples_for_bank;
    desc.get_sample = mock_get_sample;

    // Init should clear all garbage
    if (synth->Init(&desc) != k_unit_err_none_mock) {
        std::cerr << "[FAIL] Init failed." << std::endl;
        exit(1);
    }

    // Render one block WITHOUT triggering a note.
    // If garbage data persists (e.g. active=true, gate=true), we might get sound or crash.
    alignas(16) float buffer[128];
    std::memset(buffer, 0, sizeof(buffer));
    synth->Render(buffer, 64);

    float maxVal = 0.0f;
    for (float f : buffer) maxVal = std::max(maxVal, std::abs(f));

    if (maxVal > 1e-6f) {
        std::cerr << "[FAIL] Output detected without trigger! Init did not clear state. Max: " << maxVal << std::endl;
        exit(1);
    }

    std::cout << "[PASS] Dirty Init: Silence maintained (State correctly cleared)." << std::endl;
    synth->~RipplerX(); // Manual destructor call for placement new
}

void test_audio_stability() {
    std::cout << "\n[Test] 2. Audio Stability & Signal Flow..." << std::endl;
    RipplerX synth;
    unit_runtime_desc_t desc;
    desc.samplerate = 48000;
    desc.output_channels = 2;
    desc.get_num_sample_banks = mock_get_num_sample_banks;
    desc.get_num_samples_for_bank = mock_get_num_samples_for_bank;
    desc.get_sample = mock_get_sample;
    synth.Init(&desc);

    // Force parameters
    synth.setParameter(c_parameterMalletResonance, 10);
    synth.setParameter(c_parameterDecay, 500);
    synth.setParameter(c_parameterModel, 0);
    synth.setParameter(c_parameterPartials, 3); // 32 Partials
    synth.setParameter(c_parameterNoiseMix, 1000);
    synth.setParameter(c_parameterNoiseFilterFreq, 10000);

    const size_t kBlockSize = 64;
    const size_t c_kNumBlocks = (48000 / kBlockSize); // 1 second
    alignas(16) float outputBuffer[kBlockSize * 2];

    bool hasSignal = false;
    float maxAmplitude = 0.0f;

    // Trigger Note
    synth.NoteOn(60, 127);

    for (size_t b = 0; b < c_kNumBlocks; ++b) {
        std::memset(outputBuffer, 0, sizeof(outputBuffer));
        synth.Render(outputBuffer, kBlockSize);

        for (size_t i = 0; i < kBlockSize * 2; ++i) {
            float val = outputBuffer[i];
            if (!std::isfinite(val)) {
                std::cerr << "[FAIL] NaN/Inf detected at Block " << b << std::endl;
                exit(1);
            }
            if (std::abs(val) > 10.0f) {
                std::cerr << "[FAIL] Audio Explosion (>10.0f) at Block " << b << std::endl;
                exit(1);
            }
            if (std::abs(val) > 0.001f) hasSignal = true;
            maxAmplitude = std::max(maxAmplitude, std::abs(val));
        }
    }

    if (!hasSignal) {
        std::cerr << "[FAIL] No Audio Output." << std::endl;
        exit(1);
    }
    std::cout << "[PASS] Stability: Max Amp " << maxAmplitude << ", No NaNs." << std::endl;
}

void test_stress_polyphony() {
    std::cout << "\n[Test] 3. Stress Test (Polyphony & Stealing)..." << std::endl;
    RipplerX synth;
    unit_runtime_desc_t desc;
    desc.samplerate = 48000;
    desc.output_channels = 2;
    desc.get_num_sample_banks = mock_get_num_sample_banks;
    desc.get_num_samples_for_bank = mock_get_num_samples_for_bank;
    desc.get_sample = mock_get_sample;
    synth.Init(&desc);

    synth.setParameter(c_parameterMalletResonance, 10);
    synth.setParameter(c_parameterDecay, 800); // Long decay to force overlap

    const size_t kBlockSize = 64;
    alignas(16) float outputBuffer[kBlockSize * 2];

    // Trigger 20 notes rapidly (more than 8 voices)
    for (int i = 0; i < 20; ++i) {
        synth.NoteOn(60 + i, 127);

        // Render small chunk
        for(int k=0; k<5; ++k) {
            std::memset(outputBuffer, 0, sizeof(outputBuffer));
            synth.Render(outputBuffer, kBlockSize);

            // Check for crash
            if (!std::isfinite(outputBuffer[0])) {
                std::cerr << "[FAIL] NaN during stress test at note " << i << std::endl;
                exit(1);
            }
        }
    }
    std::cout << "[PASS] Stress Test: Survived rapid triggering." << std::endl;
}

void test_envelope_decay() {
    std::cout << "\n[Test] 4. Envelope Decay (Infinite Sustain Check)..." << std::endl;
    RipplerX synth;
    unit_runtime_desc_t desc;
    desc.samplerate = 48000;
    desc.output_channels = 2;
    desc.get_num_sample_banks = mock_get_num_sample_banks;
    desc.get_num_samples_for_bank = mock_get_num_samples_for_bank;
    desc.get_sample = mock_get_sample;
    synth.Init(&desc);

    // Set very short decay
    synth.setParameter(c_parameterDecay, 10); // Very short decay
    synth.setParameter(c_parameterRelease, 10);
    synth.setParameter(c_parameterMalletResonance, 0); // No resonance to ring out

    // Trigger
    synth.NoteOn(60, 127);

    // Render 0.5 seconds (should be more than enough to silence)
    alignas(16) float buffer[128];
    for (int i = 0; i < (48000 / 64) / 2; ++i) {
        std::memset(buffer, 0, sizeof(buffer));
        synth.Render(buffer, 64);
    }

    // Check last buffer for silence
    float maxVal = 0.0f;
    for (float f : buffer) maxVal = std::max(maxVal, std::abs(f));

    if (maxVal > 0.01f) {
        std::cerr << "[FAIL] Envelope did not close! Sound is continuous. Level: " << maxVal << std::endl;
        exit(1);
    }
    std::cout << "[PASS] Decay: Signal correctly silenced after release." << std::endl;
}

void test_high_pitch_stability() {
    std::cout << "\n[Test] 5. High Pitch Stability (Filter Explosion Check)..." << std::endl;
    RipplerX synth;
    unit_runtime_desc_t desc;
    desc.samplerate = 48000;
    desc.output_channels = 2;
    desc.get_num_sample_banks = mock_get_num_sample_banks;
    desc.get_num_samples_for_bank = mock_get_num_samples_for_bank;
    desc.get_sample = mock_get_sample;
    synth.Init(&desc);

    // High pitch, high resonance (stress test for IIR filters)
    synth.NoteOn(100, 127); // Very high note

    alignas(16) float buffer[128];
    bool crashed = false;

    for (int i = 0; i < 100; ++i) {
        synth.Render(buffer, 64);
        // If Limiter detects NaN, it outputs 0.0f.
        // We can't easily distinguish "silence" from "crash muted by limiter" here
        // without internal access, but we can check for NaNs that slipped through.
        if (!std::isfinite(buffer[0])) crashed = true;
    }

    if (crashed) {
        std::cerr << "[FAIL] High pitch caused NaN/Inf!" << std::endl;
        exit(1);
    }
    std::cout << "[PASS] High Pitch: No numeric instability detected." << std::endl;
}

int main() {
    std::cout << ">>> STARTING RIPPLERX COMPREHENSIVE TEST SUITE <<<" << std::endl;
    test_dirty_initialization();
    test_audio_stability();
    test_stress_polyphony();
    test_envelope_decay();
    test_high_pitch_stability();
    std::cout << "\n>>> ALL TESTS PASSED <<<" << std::endl;
    return 0;
}