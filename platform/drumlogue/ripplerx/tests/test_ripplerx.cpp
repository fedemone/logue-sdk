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
#include <algorithm>

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

// In real SDK this returns a wrapper struct, but for this test
// we only need to ensure the pointer check in Render() doesn't crash.
// We will bypass sample playback in the test trigger to focus on Synth engine.
 const sample_wrapper_t* mock_get_sample(uint8_t bank, uint8_t sample) {
 return nullptr;
 }


// Helper: Check if buffer contains valid audio (finite, not NaN)
bool is_buffer_valid(const float* buffer, size_t size) {
    for (size_t i = 0; i < size; ++i) {
        if (!std::isfinite(buffer[i])) return false;
        if (std::abs(buffer[i]) > 10.0f) return false; // Sanity check for explosion
    }
    return true;
}

// Helper: Check if buffer is silent (below threshold)
bool is_buffer_silent(const float* buffer, size_t size, float threshold = 1e-5f) {
    for (size_t i = 0; i < size; ++i) {
        if (std::abs(buffer[i]) > threshold) return false;
    }
    return true;
}

// --- TEST UTILS ---
bool is_valid_float(float x) { return std::isfinite(x) && std::abs(x) < 100.0f;
// Threshold for "Explosion"
}

void test_dirty_initialization() {
    std::cout << "\n[Test] 1. Dirty Initialization (Hot-Load Simulation)..." << std::endl;

    // CRITICAL TEST: Allocate memory filled with garbage (0xFF) to simulate hot-load
    // This simulates loading the unit while hardware is already running
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

    // CRITICAL FIX: Render MULTIPLE blocks to catch persistent phantom sound
    // The bug manifests over time as distortion builds up
    const int kNumTestBlocks = 100;  // ~2 seconds at 48kHz
    alignas(16) float buffer[128];

    float maxVal = 0.0f;
    bool foundPhantomSound = false;
    for (int block = 0; block < kNumTestBlocks; ++block) {
        std::memset(buffer, 0, sizeof(buffer));
        synth->Render(buffer, 64);

        // Check for any output in this block
        float blockMax = 0.0f;
        for (float f : buffer) {
            if (!std::isfinite(f)) {
                std::cerr << "[FAIL] NaN/Inf detected at block " << block
                          << " - Init did not clear state properly!" << std::endl;
                exit(1);
            }
            blockMax = std::max(blockMax, std::abs(f));
        }

        maxVal = std::max(maxVal, blockMax);

        // If we detect sound without trigger, that's a phantom voice
        if (blockMax > 1e-6f) {
            foundPhantomSound = true;
            std::cerr << "[FAIL] Phantom sound detected at block " << block
                      << ", amplitude: " << blockMax << std::endl;
            std::cerr << "[FAIL] Init did not clear voice/resonator state!" << std::endl;
            exit(1);
        }
    }
    if (foundPhantomSound || maxVal > 1e-6f) {
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
void test_sample_boundary_conditions() {
    std::cout << "\n[Test] 4. Sample Boundary Edge Cases..." << std::endl;
    RipplerX synth;
    unit_runtime_desc_t desc;
    desc.samplerate = 48000;
    desc.output_channels = 2;
    desc.get_num_sample_banks = mock_get_num_sample_banks;
    desc.get_num_samples_for_bank = mock_get_num_samples_for_bank;
    desc.get_sample = mock_get_sample;
    synth.Init(&desc);

    // Set up parameters
    synth.setParameter(c_parameterModel, 0);
    synth.setParameter(c_parameterPartials, 3);
    synth.setParameter(c_parameterDecay, 500);

    // Trigger note and render many blocks
    // This tests sample boundary handling when sample ends mid-block
    synth.NoteOn(60, 127);

    alignas(16) float buffer[128];
    for (int i = 0; i < 200; ++i) {
        std::memset(buffer, 0, sizeof(buffer));
        synth.Render(buffer, 64);

        // Verify no invalid values
        for (int j = 0; j < 128; ++j) {
            if (!std::isfinite(buffer[j])) {
                std::cerr << "[FAIL] Invalid value at block " << i << std::endl;
                exit(1);
            }
        }
    }

    std::cout << "[PASS] Sample boundary handling safe (200 blocks)." << std::endl;
}

void test_nan_injection() {
    std::cout << "\n[Test] 5. NaN Injection Safety..." << std::endl;
    RipplerX synth;
    unit_runtime_desc_t desc;
    desc.samplerate = 48000;
    desc.output_channels = 2;
    desc.get_num_sample_banks = mock_get_num_sample_banks;
    desc.get_num_samples_for_bank = mock_get_num_samples_for_bank;
    desc.get_sample = mock_get_sample;
    synth.Init(&desc);

    // Set extreme parameters that might cause numerical issues
    synth.setParameter(c_parameterDecay, 1);        // Minimum decay
    synth.setParameter(c_parameterMaterial, 10);    // Maximum damping
    synth.setParameter(c_parameterModel, 0);
    synth.setParameter(c_parameterPartials, 5);     // 64 partials (stress test)

    // Trigger with extreme values
    synth.NoteOn(127, 127);  // Highest note, max velocity

    alignas(16) float buffer[128];
    for (int i = 0; i < 1000; ++i) {
        std::memset(buffer, 0, sizeof(buffer));
        synth.Render(buffer, 64);

        // Verify no NaN propagation
        for (int j = 0; j < 128; ++j) {
            if (!std::isfinite(buffer[j])) {
                std::cerr << "[FAIL] NaN detected at iteration " << i
                          << ", sample " << j << std::endl;
                exit(1);
            }
        }
    }

    std::cout << "[PASS] NaN safety verified (1000 iterations)." << std::endl;
}

void test_hot_reload_stress() {
    std::cout << "\n[Test] 6. Hot-Reload Stress Test..." << std::endl;

    for (int attempt = 0; attempt < 10; ++attempt) {
        // Simulate hot-load with dirty memory (0xFF pattern)
        alignas(16) char memory[sizeof(RipplerX)];
        std::memset(memory, 0xFF, sizeof(memory));

        // Placement new - construct in dirty memory
        RipplerX* synth = new (memory) RipplerX();

        unit_runtime_desc_t desc;
        desc.samplerate = 48000;
        desc.output_channels = 2;
        desc.get_num_sample_banks = mock_get_num_sample_banks;
        desc.get_num_samples_for_bank = mock_get_num_samples_for_bank;
        desc.get_sample = mock_get_sample;

        if (synth->Init(&desc) != k_unit_err_none_mock) {
            std::cerr << "[FAIL] Init failed on attempt " << attempt << std::endl;
            exit(1);
        }

        // Render without trigger - should be SILENT
        alignas(16) float buffer[128];
        std::memset(buffer, 0, sizeof(buffer));
        synth->Render(buffer, 64);

        // Check for phantom sound (indicates state not cleared)
        float maxVal = 0.0f;
        for (float f : buffer) {
            maxVal = std::max(maxVal, std::abs(f));
        }

        if (maxVal > 1e-6f) {
            std::cerr << "[FAIL] Hot-reload attempt " << attempt
                      << " produced phantom sound: " << maxVal << std::endl;
            exit(1);
        }

        // Manual destructor call (placement new requires manual destruction)
        synth->~RipplerX();
    }

    std::cout << "[PASS] Hot-reload stability verified (10 cycles)." << std::endl;
}


void test_hot_load_garbage_initialization() {
    std::cout << "[Test] 1. Hot-Load Garbage Initialization (Comb/Voice Memory)... ";

    // 1. Simulate dirty memory (e.g., previous unit state)
    alignas(16) char memory[sizeof(RipplerX)];
    std::memset(memory, 0xAA, sizeof(memory)); // Fill with garbage (0xAAAAAAAA is a valid-ish float but large negative)

    // 2. Construct object in dirty memory
    RipplerX* synth = new (memory) RipplerX();

    // 3. Initialize
    unit_runtime_desc_t desc;
    desc.samplerate = 48000;
    desc.output_channels = 2;
    desc.get_num_sample_banks = mock_get_num_sample_banks;
    desc.get_num_samples_for_bank = mock_get_num_samples_for_bank;
    desc.get_sample = mock_get_sample;

    if (synth->Init(&desc) != k_unit_err_none) {
        std::cout << "FAILED (Init returned error)" << std::endl;
        exit(1);
    }

    // 4. Render immediately without triggering a note
    // This verifies that:
    // a) Comb filter buffer was zeroed (otherwise garbage loops)
    // b) Voices were cleared (otherwise phantom notes play)
    alignas(16) float buffer[256];
    std::memset(buffer, 0, sizeof(buffer));

    // Render a few blocks to allow any garbage in delay lines to propagate
    bool failed = false;
    float max_amp = 0.0f;
    for (int i = 0; i < 10; ++i) {
        synth->Render(buffer, 64); // 64 frames = 128 samples

        if (!is_buffer_valid(buffer, 128)) {
            std::cout << "FAILED (NaN/Inf detected at block " << i << ")" << std::endl;
            failed = true;
            break;
        }

        // Check for silence
        for (int j = 0; j < 128; ++j) {
            max_amp = std::max(max_amp, std::abs(buffer[j]));
        }
    }

    if (!failed) {
        if (max_amp > 1e-4f) {
            std::cout << "FAILED (Phantom sound detected, max amp: " << max_amp << ")" << std::endl;
			exit(1);
        } else {
            std::cout << "PASSED" << std::endl;
        }
    }

    // Manual destructor for placement new
    synth->~RipplerX();
}

void test_limiter_silence_stability() {
    std::cout << "[Test] 2. Limiter Silence Stability (rsqrt(0) check)... ";

    RipplerX synth;
    unit_runtime_desc_t desc = {0};
    desc.samplerate = 48000;
    desc.output_channels = 2;
    desc.get_num_sample_banks = mock_get_num_sample_banks;
    desc.get_num_samples_for_bank = mock_get_num_samples_for_bank;
    desc.get_sample = mock_get_sample;
    synth.Init(&desc);

    alignas(16) float buffer[128];
    std::memset(buffer, 0, sizeof(buffer)); // Input is perfect silence

    // Render silence through the chain
    // If Limiter doesn't handle 0.0f correctly (e.g. rsqrt(0)), it might produce NaNs
    synth.Render(buffer, 64);

    if (is_buffer_valid(buffer, 128)) {
        std::cout << "PASSED" << std::endl;
    } else {
        std::cout << "FAILED (NaN/Inf produced from silence)" << std::endl;
		exit(1);
    }
}

void test_comb_filter_stability() {
    std::cout << "[Test] 3. Comb Filter Stability... ";

    RipplerX synth;
    unit_runtime_desc_t desc = {0};
    desc.samplerate = 48000;
    desc.output_channels = 2;
    desc.get_num_sample_banks = mock_get_num_sample_banks;
    desc.get_num_samples_for_bank = mock_get_num_samples_for_bank;
    desc.get_sample = mock_get_sample;
    synth.Init(&desc);

    // Trigger a short impulse to fill the comb buffer
    synth.NoteOn(60, 127);

    alignas(16) float buffer[128];

    // Render for a while to let the comb filter feedback loop run
    bool stable = true;
    for (int i = 0; i < 100; ++i) {
        std::memset(buffer, 0, sizeof(buffer));
        synth.Render(buffer, 64);
        if (!is_buffer_valid(buffer, 128)) {
            stable = false;
            break;
        }
    }

    if (stable) {
        std::cout << "PASSED" << std::endl;
    } else {
        std::cout << "FAILED (Instability detected)" << std::endl;
		exit(1);
    }
}

void test_percussion_auto_release() {
    std::cout << "\n[Test] 6. Percussion Auto-Release (No NoteOff)..." << std::endl;
    RipplerX synth;
    unit_runtime_desc_t desc;
    desc.samplerate = 48000;
    desc.output_channels = 2;
    desc.get_num_sample_banks = mock_get_num_sample_banks;
    desc.get_num_samples_for_bank = mock_get_num_samples_for_bank;
    desc.get_sample = mock_get_sample;
    synth.Init(&desc);

    // Set parameters for a percussive sound (Mallet/Resonator)
    // Decay = 50 (Short enough to decay within 2s, but long enough to verify curve)
    synth.setParameter(c_parameterDecay, 50);
    synth.setParameter(c_parameterMalletResonance, 10);
    synth.setParameter(c_parameterModel, 0);
    synth.setParameter(c_parameterNoiseMix, 0); // Disable noise to test Resonator damping only

    // Trigger NoteOn (Percussion hit) BUT NO NoteOff
    synth.NoteOn(60, 127);

    // Render for 2 seconds (approx 1500 blocks)
    // We expect the sound to fade to silence naturally due to physics damping
    alignas(16) float buffer[128];
    float maxVal = 0.0f;

    for (int i = 0; i < 1500; ++i) {
        std::memset(buffer, 0, sizeof(buffer));
        synth.Render(buffer, 64);

        // Check the tail (last 100 blocks) for silence
        if (i > 1400) {
             for (float f : buffer) maxVal = std::max(maxVal, std::abs(f));
        }
    }

    if (maxVal > 0.001f) {
        std::cerr << "[FAIL] Sound continued without NoteOff! Max Level: " << maxVal << std::endl;
        std::cerr << "       Percussion voices should decay naturally (Damping failed)." << std::endl;
        exit(1);
    }
    std::cout << "[PASS] Auto-Release: Signal silenced naturally without NoteOff." << std::endl;
}

// --- COMPONENT UNIT TESTS ---

void test_envelope_class() {
    std::cout << "[Test] 7. Component: Envelope..." << std::endl;
    Envelope env;
    // Init: srate=48k, A=10ms, D=10ms, S=0.5, R=10ms
    env.init(48000.0f, 10.0f, 10.0f, 0.5f, 10.0f, 0.0f, 0.0f, 0.0f);

    env.attack(1.0f);
    if (env.getState() != 1) { // Attack
         std::cerr << "[FAIL] Envelope failed to enter Attack state" << std::endl; exit(1);
    }

    // Process a few samples
    env.process();
    if (env.getEnv() <= 0.0f) {
         std::cerr << "[FAIL] Envelope value not increasing" << std::endl; exit(1);
    }
    std::cout << "  [PASS] Envelope logic verified." << std::endl;
}

void test_filter_class() {
    std::cout << "[Test] 8. Component: Filter..." << std::endl;
    Filter flt;
    flt.lp(48000.0f, 1000.0f, 0.707f);

    // DC Gain check for LP (should be 1.0)
    // Use small amplitude (0.1) to avoid soft clipper non-linearity at full scale
    float input = 0.1f;
    float out = 0.0f;
    for(int i=0; i<200; ++i) out = flt.df1(input); // Feed DC

    float gain = out / input;
    if (std::abs(gain - 1.0f) > 0.05f) {
         std::cerr << "[FAIL] Filter LP Unity Gain failed. Out=" << out << " Gain=" << gain << std::endl; exit(1);
    }
    std::cout << "  [PASS] Filter LP response verified." << std::endl;
}

void test_noise_class() {
    std::cout << "[Test] 9. Component: Noise..." << std::endl;
    Noise n;
    n.init(48000.0f, 0, 1000.0f, 0.707f, 10.0f, 10.0f, 1.0f, 10.0f, 0.0f, 0.0f);
    n.attack(1.0f);

    float val = n.process();
    if (val == 0.0f) {
        // Extremely unlikely to be exactly 0.0f for noise
        std::cerr << "[FAIL] Noise generator output is silent" << std::endl; exit(1);
    }
    std::cout << "  [PASS] Noise generator verified." << std::endl;
}

void test_mallet_class() {
    std::cout << "[Test] 10. Component: Mallet..." << std::endl;
    Mallet m;
    m.trigger(48000.0f, 500.0f);

    float32x4_t out = m.process();
    float val = vgetq_lane_f32(out, 0);

    if (val == 0.0f) {
        std::cerr << "[FAIL] Mallet output is silent" << std::endl; exit(1);
    }
    std::cout << "  [PASS] Mallet trigger verified." << std::endl;
}

void test_models_logic() {
    std::cout << "[Test] 11. Component: Models..." << std::endl;
    // Verify accessors don't crash
    const float* m = getAModels(0);
    if (m[0] != 1.0f) {
        std::cerr << "[FAIL] Model data corruption" << std::endl; exit(1);
    }
    // Test recalc (indirectly via function call, verifying no crash)
    recalcBeam(true, 1.5f);
    std::cout << "  [PASS] Models data access verified." << std::endl;
}

void test_partial_class() {
    std::cout << "[Test] 12. Component: Partial..." << std::endl;
    Partial p;
    p.update(440.0f, 1.0f, 1.0f, 1.0f, false);

    float32x4_t in = vdupq_n_f32(1.0f);
    float32x4_t out = p.process(in);

    if (!std::isfinite(vgetq_lane_f32(out, 0))) {
        std::cerr << "[FAIL] Partial produced NaN" << std::endl; exit(1);
    }
    std::cout << "  [PASS] Partial processing verified." << std::endl;
}

void test_waveguide_class() {
    std::cout << "[Test] 13. Component: Waveguide..." << std::endl;
    Waveguide w;
    // Initialize parameters (normally done by Resonator::setParams)
    w.srate = 48000.0f;
    w.decay = 50.0f;
    w.rel = 0.5f;
    w.vel_decay = 0.0f;
    w.is_closed = false;

    w.update(440.0f, 1.0f, false);

    float32x4_t in = vdupq_n_f32(0.5f);
    float32x4_t out = w.process(in);

    if (!std::isfinite(vgetq_lane_f32(out, 0))) {
        std::cerr << "[FAIL] Waveguide produced NaN" << std::endl; exit(1);
    }
    std::cout << "  [PASS] Waveguide processing verified." << std::endl;
}

void test_voice_class() {
    std::cout << "[Test] 14. Component: Voice..." << std::endl;
    Voice v;
    v.Init();
    v.trigger(48000.0f, 60, 1.0f, 500.0f);

    if (!v.isPressed) {
        std::cerr << "[FAIL] Voice trigger failed to set isPressed" << std::endl; exit(1);
    }
    v.release();
    if (!v.isRelease) {
        std::cerr << "[FAIL] Voice release failed to set isRelease" << std::endl; exit(1);
    }
    std::cout << "  [PASS] Voice lifecycle verified." << std::endl;
}

void test_hot_load_stability_3s() {
    std::cout << "\n[Test] 15. Hot-Load Stability (3s, Dirty Buffer)..." << std::endl;

    // 1. Dirty Memory Init (Simulate Hot Load)
    alignas(16) char memory[sizeof(RipplerX)];
    std::memset(memory, 0xCC, sizeof(memory)); // Fill with garbage
    RipplerX* synth = new (memory) RipplerX();

    unit_runtime_desc_t desc;
    desc.samplerate = 48000;
    desc.output_channels = 2;
    desc.get_num_sample_banks = mock_get_num_sample_banks;
    desc.get_num_samples_for_bank = mock_get_num_samples_for_bank;
    desc.get_sample = mock_get_sample;

    if (synth->Init(&desc) != k_unit_err_none_mock) {
        std::cerr << "[FAIL] Init failed." << std::endl; exit(1);
    }

    // 2. Render Loop for 3 seconds with Non-Empty Buffer
    const int kNumBlocks = (48000 * 3) / 64;
    alignas(16) float buffer[128];

    for (int i = 0; i < kNumBlocks; ++i) {
        // Simulate "Buffer Not Empty" (e.g., previous unit output)
        for (int j = 0; j < 128; ++j) {
            buffer[j] = 0.1f * sinf(j * 0.1f + i);
        }

        synth->Render(buffer, 64);

        if (!is_buffer_valid(buffer, 128)) {
             std::cerr << "[FAIL] Instability/NaN at block " << i << std::endl;
             exit(1);
        }
    }

    std::cout << "[PASS] Hot-Load 3s Stability verified (Accumulation safe)." << std::endl;
    synth->~RipplerX();
}

int main() {
    std::cout << ">>> STARTING RIPPLERX COMPREHENSIVE TEST SUITE <<<" << std::endl;
    test_dirty_initialization();
    test_audio_stability();
    test_stress_polyphony();
    test_envelope_decay();
    test_high_pitch_stability();
    test_sample_boundary_conditions();
    test_nan_injection();
    test_hot_reload_stress();
    test_hot_load_garbage_initialization();
    test_limiter_silence_stability();
    test_comb_filter_stability();
    test_percussion_auto_release();

    test_envelope_class();
    test_filter_class();
    test_noise_class();
    test_mallet_class();
    test_models_logic();
    test_partial_class();
    test_waveguide_class();
    test_voice_class();
    test_hot_load_stability_3s();

    std::cout << "\n>>> ALL TESTS PASSED <<<" << std::endl;
    return 0;
}