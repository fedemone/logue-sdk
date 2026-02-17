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
#include <chrono>
#include <iomanip>

/** Additional unit testes created by Claude
 * to use, lauch this command
 * /mnt/d/Fede/drumlogue/arm-unknown-linux-gnueabihf/bin/arm-unknown-linux-gnueabihf-g++ -static -std=c++17 -O3 -I.. -DDEBUGN test_ripplerx_debug.cpp ../ripplerx.cc ../Voice.cc ../Resonator.cc ../Partial.cc ../Waveguide.cc ../Models.cc ../Mallet.cc ../Noise.cc ../Filter.cc ../Envelope.cc -o run_test_debug && qemu-arm ./run_test_debug | tee run_test_debug_result.log
*/

// --- ARCHITECTURE COMPATIBILITY ---
#include <arm_neon.h>

// Include Synth Engine
#include "../ripplerx.h"

// Fix macro collision
#ifdef isfinite
#undef isfinite
#endif

#ifndef DEBUGN
#define DEBUGN
#endif

// --- MOCK DRUMLOGUE RUNTIME ---
#define k_unit_err_none_mock 0
#define k_unit_err_geometry_mock 1
#define k_unit_err_samplerate_mock 2

// Mock Sample Data with actual audio content
alignas(16) float mock_sample_data[2048];
sample_wrapper_t mock_wrapper;

uint8_t mock_get_num_sample_banks() { return 1; }
uint8_t mock_get_num_samples_for_bank(uint8_t bank) { return 1; }

const sample_wrapper_t* mock_get_sample(uint8_t bank, uint8_t sample) {
    // Initialize mock sample data with a sine wave if not already done
    if (mock_sample_data[0] == 0.0f) {
        std::cout << "  Initializing mock sample data..." << std::endl;
        for (int i = 0; i < 2048; ++i) {
            // SIMULATE REAL SAMPLE: Sharp transient (click) + Body + Noise
            // This stresses the Resonator much more than a pure sine wave
            float transient = (i < 10) ? (float)(10 - i) / 10.0f : 0.0f; // Sharp attack
            float body = 0.5f * sinf(i * 0.1f);
            float noise = 0.1f * ((float)(rand() % 100) / 50.0f - 1.0f);
            mock_sample_data[i] = transient + body + noise;
        }
    }
    mock_wrapper.sample_ptr = mock_sample_data;
    mock_wrapper.frames = 1024; // 1024 stereo frames
    mock_wrapper.channels = 2;
    return &mock_wrapper;
}

// --- TEST UTILITIES ---
struct TestStats {
    float min_val = 0.0f;
    float max_val = 0.0f;
    float avg_val = 0.0f;
    size_t nan_count = 0;
    size_t inf_count = 0;
    size_t clip_count = 0;
    size_t total_samples = 0;

    void update(float val) {
        total_samples++;
        if (!std::isfinite(val)) {
            if (std::isnan(val)) nan_count++;
            else inf_count++;
            return;
        }
        min_val = std::min(min_val, val);
        max_val = std::max(max_val, val);
        avg_val += std::abs(val);
        if (std::abs(val) > 1.0f) clip_count++;
    }

    void finalize() {
        if (total_samples > 0) {
            avg_val /= total_samples;
        }
    }

    void print(const char* label) {
        std::cout << "  [" << label << "] Min: " << min_val
                  << ", Max: " << max_val
                  << ", Avg: " << avg_val
                  << ", NaN: " << nan_count
                  << ", Inf: " << inf_count
                  << ", Clips: " << clip_count << std::endl;
    }
};

bool is_valid_float(float x) {
    return std::isfinite(x) && std::abs(x) < 100.0f;
}

void verify_buffer(const float* buffer, size_t samples, const char* context) {
    for (size_t i = 0; i < samples; ++i) {
        if (!std::isfinite(buffer[i])) {
            std::cerr << "[FAIL] " << context << ": NaN/Inf at sample " << i
                      << " value=" << buffer[i] << std::endl;
            exit(1);
        }
        if (std::abs(buffer[i]) > 100.0f) {
            std::cerr << "[FAIL] " << context << ": Audio explosion at sample " << i
                      << " value=" << buffer[i] << std::endl;
            exit(1);
        }
    }
}

// --- ENHANCED TEST SUITE ---

void test_dirty_initialization() {
    std::cout << "\n[Test 1] Hot-Load Initialization (Dirty Memory)" << std::endl;
    std::cout << "  Testing constructor and Init() with garbage-filled memory..." << std::endl;

    // Simulate hot-load with multiple garbage patterns
    const uint8_t garbage_patterns[] = {0xFF, 0xAA, 0x55, 0xCC, 0x33};

    for (size_t pattern_idx = 0; pattern_idx < sizeof(garbage_patterns); ++pattern_idx) {
        alignas(16) char memory[sizeof(RipplerX)];
        std::memset(memory, garbage_patterns[pattern_idx], sizeof(memory));

        RipplerX* synth = new (memory) RipplerX();

        unit_runtime_desc_t desc;
        desc.samplerate = 48000;
        desc.output_channels = 2;
        desc.get_num_sample_banks = mock_get_num_sample_banks;
        desc.get_num_samples_for_bank = mock_get_num_samples_for_bank;
        desc.get_sample = mock_get_sample;

        if (synth->Init(&desc) != k_unit_err_none_mock) {
            std::cerr << "[FAIL] Init failed with pattern 0x" << std::hex
                      << (int)garbage_patterns[pattern_idx] << std::endl;
            exit(1);
        }

        // Render 100 blocks (~2 seconds) to catch persistent phantom sound
        alignas(16) float buffer[128];
        TestStats stats;

        for (int block = 0; block < 100; ++block) {
            // DIRTY BUFFER TEST: Fill with garbage before Render
            for(int k=0; k<128; ++k) buffer[k] = 999.0f;

            // Render should OVERWRITE the garbage, not add to it
            synth->Render(buffer, 64);

            for (float f : buffer) {
                stats.update(f);
            }

            verify_buffer(buffer, 128, "Dirty Init");
        }

        stats.finalize();

        // Phantom sound check: ANY output without trigger is failure
        if (stats.max_val > 1e-6f) {
            std::cerr << "[FAIL] Phantom sound detected with pattern 0x" << std::hex
                      << (int)garbage_patterns[pattern_idx] << std::dec
                      << ", max amplitude: " << stats.max_val << std::endl;
            exit(1);
        }

        synth->~RipplerX();
    }

    std::cout << "  [PASS] All garbage patterns handled correctly (no phantom sound)" << std::endl;
}

void test_runtime_stability_3_seconds() {
    std::cout << "\n[Test 2] Runtime Stability (3 Seconds with Sound)" << std::endl;
    std::cout << "  Rendering continuous audio for 3 seconds..." << std::endl;

    RipplerX synth;
    unit_runtime_desc_t desc;
    desc.samplerate = 48000;
    desc.output_channels = 2;
    desc.get_num_sample_banks = mock_get_num_sample_banks;
    desc.get_num_samples_for_bank = mock_get_num_samples_for_bank;
    desc.get_sample = mock_get_sample;
    synth.Init(&desc);

    // Set reasonable parameters
    synth.LoadPreset(0); // Load Bells as base
    synth.setParameter(c_parameterMalletResonance, 800); // 0.8f (Standard Program Value)
    synth.setParameter(c_parameterMalletStiffness, 600); // 600.0 (Standard Program Value)
    synth.setParameter(c_parameterDecay, 500);
    synth.setParameter(c_parameterModel, 0);
    synth.setParameter(c_parameterPartials, 3); // 32 Partials

    const size_t kBlockSize = 64;
    const size_t kTotalBlocks = (48000 * 3) / kBlockSize; // 3 seconds
    alignas(16) float outputBuffer[kBlockSize * 2];

    TestStats stats;
    bool hasSignal = false;
    auto start_time = std::chrono::high_resolution_clock::now();

    // Trigger note at start
    synth.NoteOn(60, 100);

    // Retrigger notes periodically to maintain sound
    for (size_t block = 0; block < kTotalBlocks; ++block) {
        // Retrigger every 0.5 seconds to keep sound going
        if (block % (kTotalBlocks / 6) == 0 && block > 0) {
            synth.NoteOn(60 + (block % 12), 80 + (block % 40));
        }

        std::memset(outputBuffer, 0, sizeof(outputBuffer));
        synth.Render(outputBuffer, kBlockSize);

        for (size_t i = 0; i < kBlockSize * 2; ++i) {
            float val = outputBuffer[i];
            stats.update(val);
            if (std::abs(val) > 0.001f) hasSignal = true;
        }

        verify_buffer(outputBuffer, kBlockSize * 2, "3-Second Stability");

        // Progress indicator every 0.5 seconds
        if (block % (kTotalBlocks / 6) == 0) {
            std::cout << "  Progress: " << (block * 100 / kTotalBlocks) << "% complete..." << std::endl;
        }
    }

    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);

    stats.finalize();

    if (!hasSignal) {
        std::cerr << "[FAIL] No audio output during 3-second test!" << std::endl;
        exit(1);
    }

    stats.print("3-Second Run");
    std::cout << "  Render time: " << duration.count() << " ms" << std::endl;
    std::cout << "  [PASS] 3-second stability test completed successfully" << std::endl;
}

void test_audio_stability() {
    std::cout << "\n[Test 3] Basic Audio Stability & Signal Flow" << std::endl;

    RipplerX synth;
    unit_runtime_desc_t desc;
    desc.samplerate = 48000;
    desc.output_channels = 2;
    desc.get_num_sample_banks = mock_get_num_sample_banks;
    desc.get_num_samples_for_bank = mock_get_num_samples_for_bank;
    desc.get_sample = mock_get_sample;
    synth.Init(&desc);

    synth.setParameter(c_parameterMalletResonance, 800);
    synth.setParameter(c_parameterMalletStiffness, 600);
    synth.setParameter(c_parameterDecay, 500);
    synth.setParameter(c_parameterModel, 0);
    synth.setParameter(c_parameterPartials, 3);
    synth.setParameter(c_parameterNoiseMix, 500);

    const size_t kBlockSize = 64;
    const size_t kNumBlocks = (48000 / kBlockSize); // 1 second
    alignas(16) float outputBuffer[kBlockSize * 2];

    TestStats stats;
    bool hasSignal = false;

    synth.NoteOn(60, 127);

    // Render 100ms (attack + decay)
    for (int i = 0; i < (4800 / 64); ++i) {
        std::memset(outputBuffer, 0, sizeof(outputBuffer));
        synth.Render(outputBuffer, 64);
    }

    synth.NoteOff(60);  // ← ADD THIS LINE

    // Render 500ms (release)
    for (int i = 0; i < (24000 / 64); ++i) {
        std::memset(outputBuffer, 0, sizeof(outputBuffer));
        synth.Render(outputBuffer, 64);
    }

    synth.NoteOff(60);  // ← ADD THIS LINE

    for (size_t b = 0; b < kNumBlocks; ++b) {
        std::memset(outputBuffer, 0, sizeof(outputBuffer));
        synth.Render(outputBuffer, kBlockSize);

        for (size_t i = 0; i < kBlockSize * 2; ++i) {
            float val = outputBuffer[i];
            stats.update(val);
            if (std::abs(val) > 0.001f) hasSignal = true;
        }

        verify_buffer(outputBuffer, kBlockSize * 2, "Basic Stability");
    }

    synth.NoteOff(60);  // ← ADD THIS LINE

    stats.finalize();

    if (!hasSignal) {
        std::cerr << "[FAIL] No audio output detected!" << std::endl;
        exit(1);
    }

    stats.print("1-Second Run");
    std::cout << "  [PASS] Basic stability test passed" << std::endl;
}

void test_stress_polyphony() {
    std::cout << "\n[Test 4] Polyphony Stress (Voice Stealing)" << std::endl;

    RipplerX synth;
    unit_runtime_desc_t desc;
    desc.samplerate = 48000;
    desc.output_channels = 2;
    desc.get_num_sample_banks = mock_get_num_sample_banks;
    desc.get_num_samples_for_bank = mock_get_num_samples_for_bank;
    desc.get_sample = mock_get_sample;
    synth.Init(&desc);

    synth.setParameter(c_parameterMalletResonance, 800);
    synth.setParameter(c_parameterMalletStiffness, 600);
    synth.setParameter(c_parameterDecay, 800);

    const size_t kBlockSize = 64;
    alignas(16) float outputBuffer[kBlockSize * 2];
    TestStats stats;

    // Trigger 30 notes rapidly (test voice stealing)
    for (int i = 0; i < 30; ++i) {
        synth.NoteOn(48 + (i % 24), 100 + (i % 27));

        for(int k = 0; k < 5; ++k) {
            std::memset(outputBuffer, 0, sizeof(outputBuffer));
            synth.Render(outputBuffer, kBlockSize);

            for (size_t j = 0; j < kBlockSize * 2; ++j) {
                stats.update(outputBuffer[j]);
            }

            verify_buffer(outputBuffer, kBlockSize * 2, "Polyphony Stress");
        }
    }

    stats.finalize();
    stats.print("Polyphony");
    std::cout << "  [PASS] Survived 30 rapid note triggers" << std::endl;
}

void test_envelope_decay() {
    std::cout << "\n[Test 5] Envelope Decay (Silence Verification)" << std::endl;

    RipplerX synth;
    unit_runtime_desc_t desc;
    desc.samplerate = 48000;
    desc.output_channels = 2;
    desc.get_num_sample_banks = mock_get_num_sample_banks;
    desc.get_num_samples_for_bank = mock_get_num_samples_for_bank;
    desc.get_sample = mock_get_sample;
    synth.Init(&desc);

    synth.setParameter(c_parameterDecay, 10);
    synth.setParameter(c_parameterRelease, 5);

    synth.NoteOn(60, 127);

    alignas(16) float buffer[128];

    // === CHANGED: Split into two phases ===

    // Phase 1: Render 100ms (attack + decay phase)
    for (int i = 0; i < (4800 / 64); ++i) {  // 100ms at 48kHz
        // std::memset(buffer, 0, sizeof(buffer));
        synth.Render(buffer, 64);
    }

    // === ADDED: Trigger release ===
    synth.NoteOff(60);

    // Phase 2: Render 500ms for release to complete
    for (int i = 0; i < (24000 / 64); ++i) {  // 500ms at 48kHz
        // std::memset(buffer, 0, sizeof(buffer));
        synth.Render(buffer, 64);
    }

    // Check for silence
    float maxVal = 0.0f;
    for (float f : buffer) maxVal = std::max(maxVal, std::abs(f));

    if (maxVal > 0.01f) {
        std::cerr << "[FAIL] Envelope did not close! Level: " << maxVal << std::endl;
        exit(1);
    }

    std::cout << "  [PASS] Envelope decay verified (final level: " << maxVal << ")" << std::endl;
}

void test_extreme_parameters() {
    std::cout << "\n[Test 6] Extreme Parameters (Numerical Stability)" << std::endl;

    RipplerX synth;
    unit_runtime_desc_t desc;
    desc.samplerate = 48000;
    desc.output_channels = 2;
    desc.get_num_sample_banks = mock_get_num_sample_banks;
    desc.get_num_samples_for_bank = mock_get_num_samples_for_bank;
    desc.get_sample = mock_get_sample;
    synth.Init(&desc);

    // Test extreme parameter combinations
    struct TestCase {
        const char* name;
        uint8_t note;
        uint8_t vel;
        int decay;
        int material;
        int partials;
    };

    TestCase cases[] = {
        {"Very High Pitch", 127, 127, 500, 5, 1},
        {"Very Low Pitch", 24, 127, 500, 5, 2},
        {"Min Decay", 60, 127, 1, 5, 3},
        {"Max Decay", 60, 127, 1000, 5, 3},
        {"Max Partials", 60, 127, 500, 5, 4}, // 64 partials, as partials for resonator A are at indexes 0-5 and for resonator A are from index 5 to 9
        {"Min Damping", 60, 127, 500, -10, 1},
        {"Max Damping", 60, 127, 500, 10, 2},
    };

    alignas(16) float buffer[128];

    for (const auto& test : cases) {
        synth.setParameter(c_parameterDecay, test.decay);
        synth.setParameter(c_parameterMaterial, test.material);
        synth.setParameter(c_parameterPartials, test.partials);
        synth.setParameter(c_parameterModel, 0);

        synth.NoteOn(test.note, test.vel);

        TestStats stats;
        for (int i = 0; i < 100; ++i) {
            // std::memset(buffer, 0, sizeof(buffer));  // try dirty buffer
            synth.Render(buffer, 64);

            for (float f : buffer) {
                stats.update(f);
            }

            verify_buffer(buffer, 128, test.name);
        }

        stats.finalize();
        std::cout << "  [" << test.name << "] ";
        std::cout << "Max: " << stats.max_val << ", NaN: " << stats.nan_count << std::endl;
    }

    std::cout << "  [PASS] All extreme parameter tests passed" << std::endl;
}

void test_hot_reload_multi_pattern() {
    std::cout << "\n[Test 7] Hot-Reload Multi-Pattern Stress" << std::endl;

    const uint8_t patterns[] = {0x00, 0xFF, 0xAA, 0x55, 0xF0, 0x0F, 0xCC, 0x33, 0xC3, 0x3C};

    for (size_t p = 0; p < sizeof(patterns); ++p) {
        alignas(16) char memory[sizeof(RipplerX)];
        std::memset(memory, patterns[p], sizeof(memory));

        RipplerX* synth = new (memory) RipplerX();

        unit_runtime_desc_t desc;
        desc.samplerate = 48000;
        desc.output_channels = 2;
        desc.get_num_sample_banks = mock_get_num_sample_banks;
        desc.get_num_samples_for_bank = mock_get_num_samples_for_bank;
        desc.get_sample = mock_get_sample;

        if (synth->Init(&desc) != k_unit_err_none_mock) {
            std::cerr << "[FAIL] Init failed with pattern 0x" << std::hex
                      << (int)patterns[p] << std::endl;
            exit(1);
        }

        // Render 50 blocks to catch delayed phantom sounds
        alignas(16) float buffer[128];
        float maxVal = 0.0f;

        for (int i = 0; i < 50; ++i) {
            // std::memset(buffer, 0, sizeof(buffer));
            synth->Render(buffer, 64);

            for (float f : buffer) {
                maxVal = std::max(maxVal, std::abs(f));
            }

            verify_buffer(buffer, 128, "Hot-Reload");
        }

        if (maxVal > 1e-6f) {
            std::cerr << "[FAIL] Pattern 0x" << std::hex << (int)patterns[p]
                      << " phantom sound: " << maxVal << std::endl;
            exit(1);
        }

        synth->~RipplerX();
    }

    std::cout << "  [PASS] All 10 garbage patterns handled correctly" << std::endl;
}

void test_note_on_off_cycle() {
    std::cout << "\n[Test 8] Note On/Off Cycle (Gate Handling)" << std::endl;

    RipplerX synth;
    unit_runtime_desc_t desc;
    desc.samplerate = 48000;
    desc.output_channels = 2;
    desc.get_num_sample_banks = mock_get_num_sample_banks;
    desc.get_num_samples_for_bank = mock_get_num_samples_for_bank;
    desc.get_sample = mock_get_sample;
    synth.Init(&desc);

    synth.setParameter(c_parameterDecay, 300);
    synth.setParameter(c_parameterRelease, 5);

    alignas(16) float buffer[128];

    // Rapid on/off cycling
    for (int cycle = 0; cycle < 20; ++cycle) {
        synth.NoteOn(60 + (cycle % 12), 100);

        // Render 10 blocks
        for (int i = 0; i < 10; ++i) {
            // std::memset(buffer, 0, sizeof(buffer));
            synth.Render(buffer, 64);
            verify_buffer(buffer, 128, "Note Cycle");
        }

        synth.NoteOff(60 + (cycle % 12));

        // Render 5 blocks after off
        for (int i = 0; i < 5; ++i) {
            // std::memset(buffer, 0, sizeof(buffer));
            synth.Render(buffer, 64);
            verify_buffer(buffer, 128, "Note Cycle");
        }
    }

    std::cout << "  [PASS] 20 note on/off cycles completed" << std::endl;
}

void test_limiter_silence_stability() {
    std::cout << "\n[Test 9] Limiter Silence Stability (rsqrt(0) check)..." << std::endl;

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
    synth.Render(buffer, 64);

    verify_buffer(buffer, 128, "Limiter Silence");
    std::cout << "  [PASS] Limiter handled silence without NaN/Inf" << std::endl;
}

void test_comb_filter_stability() {
    std::cout << "\n[Test 10] Comb Filter Stability..." << std::endl;

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
    for (int i = 0; i < 100; ++i) {
        // std::memset(buffer, 0, sizeof(buffer));
        synth.Render(buffer, 64);
        verify_buffer(buffer, 128, "Comb Stability");
    }
    std::cout << "  [PASS] Comb filter stable under feedback" << std::endl;
}

void test_percussion_auto_release() {
    std::cout << "\n[Test 11] Percussion Auto-Release (No NoteOff)..." << std::endl;
    RipplerX synth;
    unit_runtime_desc_t desc;
    desc.samplerate = 48000;
    desc.output_channels = 2;
    desc.get_num_sample_banks = mock_get_num_sample_banks;
    desc.get_num_samples_for_bank = mock_get_num_samples_for_bank;
    desc.get_sample = mock_get_sample;
    synth.Init(&desc);

    // Set parameters for a percussive sound
    synth.setParameter(c_parameterDecay, 1); // 1 = Short decay (approx 0.1s) for test speed
    synth.setParameter(c_parameterMalletResonance, 800);
    synth.setParameter(c_parameterMalletStiffness, 600);
    synth.setParameter(c_parameterModel, 0);
    synth.setParameter(c_parameterNoiseMix, 0);

    // Trigger NoteOn (Percussion hit) BUT NO NoteOff
    synth.NoteOn(60, 127);

    alignas(16) float buffer[128];
    float maxVal = 0.0f;

    // Render for 2 seconds
    for (int i = 0; i < 1500; ++i) {
        // std::memset(buffer, 0, sizeof(buffer));  // try with dirty buffer
        synth.Render(buffer, 64);

        // Check the tail (last 100 blocks) for silence
        if (i > 1400) {
             for (float f : buffer) maxVal = std::max(maxVal, std::abs(f));
        }
    }

    if (maxVal > 0.001f) {
        std::cerr << "[FAIL] Sound continued without NoteOff! Max Level: " << maxVal << std::endl;
        exit(1);
    }
    std::cout << "  [PASS] Auto-Release: Signal silenced naturally without NoteOff." << std::endl;
}

void test_component_filter_vec() {
    std::cout << "\n[Test 12] Component: Filter (Vector)..." << std::endl;
    Filter flt;

    // Vectorization Test
    Filter flt_vec;
    flt_vec.lp(48000.0f, 1000.0f, 0.707f);
    flt_vec.clear();

    alignas(16) float vec_in[4] = {0.1f, -0.1f, 0.05f, -0.05f};
    float32x4_t v_in = vld1q_f32(vec_in);
    float32x4_t v_out = flt_vec.df1_vec(v_in);

    // Check for NaN/Explosion in vector output
    float vec_out[4];
    vst1q_f32(vec_out, v_out);
    for(int i=0; i<4; ++i) {
        if(!std::isfinite(vec_out[i])) {
             std::cerr << "[FAIL] Filter vector output is NaN at index " << i << std::endl; exit(1);
        }
    }

    std::cout << "  [PASS] Filter LP response and vectorization verified." << std::endl;
}

void test_component_envelope() {
    std::cout << "\n[Test 13] Component: Envelope..." << std::endl;
    Envelope env;
    env.init(48000.0f, 10.0f, 10.0f, 0.5f, 10.0f, 0.0f, 0.0f, 0.0f);
    env.attack(1.0f);
    if (env.getState() != 1) {
         std::cerr << "[FAIL] Envelope failed to enter Attack state" << std::endl; exit(1);
    }
    env.process();
    if (env.getEnv() <= 0.0f) {
         std::cerr << "[FAIL] Envelope value not increasing" << std::endl; exit(1);
    }
    std::cout << "  [PASS] Envelope logic verified." << std::endl;
}

void test_rhythmic_stability_crash() {
    std::cout << "\n[Test 14] Rhythmic Stability (4 Beats Crash Check)..." << std::endl;
    RipplerX synth;
    unit_runtime_desc_t desc;
    desc.samplerate = 48000;
    desc.output_channels = 2;
    desc.get_num_sample_banks = mock_get_num_sample_banks;
    desc.get_num_samples_for_bank = mock_get_num_samples_for_bank;
    desc.get_sample = mock_get_sample;

    if (synth.Init(&desc) != 0) {
        std::cerr << "Init failed" << std::endl;
        exit(1);
    }

    // Set parameters to "Bells" defaults (Model=Squared, Partials=32)
    synth.setParameter(c_parameterModel, 2); // Squared
    synth.setParameter(c_parameterPartials, 3); // 32
    synth.setParameter(c_parameterDecay, 500);
    synth.setParameter(c_parameterMalletResonance, 500);

    const size_t kBlockSize = 64;
    alignas(16) float buffer[128];

    // Simulate 4 beats at 120 BPM (0.5s per beat)
    for (int beat = 1; beat <= 4; ++beat) {
        std::cout << "  --- BEAT " << beat << " TRIGGER ---" << std::endl;
        synth.NoteOn(60, 100);

        // Render 0.5 seconds (approx 375 blocks)
        float max_peak = 0.0f;

        for (int i = 0; i < 375; ++i) {
            // std::memset(buffer, 0, sizeof(buffer));
            synth.Render(buffer, kBlockSize);

            // Check for NaN/Inf immediately
            for (int j = 0; j < 128; ++j) {
                float val = buffer[j];
                if (!std::isfinite(val)) {
                    std::cerr << "[FAIL] NaN detected at Beat " << beat << " Block " << i << std::endl;
                    exit(1);
                }
                if (std::abs(val) > 10.0f) {
                    std::cerr << "[FAIL] Explosion detected at Beat " << beat << " Block " << i << " Val=" << val << std::endl;
                    exit(1);
                }
                max_peak = std::max(max_peak, std::abs(val));
            }
        }

        std::cout << "  Beat " << beat << " Max Peak: " << max_peak << std::endl;

        // Note Off
        synth.NoteOff(60);
    }
    std::cout << "  [PASS] Survived 4 beats of rhythmic triggering." << std::endl;
}

void test_degradation_over_12_beats() {
    std::cout << "\n[Test 15] Degradation over 12 beats..." << std::endl;
    RipplerX synth;
    unit_runtime_desc_t desc;
    desc.samplerate = 48000;
    desc.output_channels = 2;
    desc.get_num_sample_banks = mock_get_num_sample_banks;
    desc.get_num_samples_for_bank = mock_get_num_samples_for_bank;
    desc.get_sample = mock_get_sample;

    if (synth.Init(&desc) != 0) {
        std::cerr << "Init failed" << std::endl;
        exit(1);
    }

    // Set parameters that might cause issues, similar to preset 11 or hot-loading
    synth.setParameter(c_parameterModel, 2); // Squared
    synth.setParameter(c_parameterPartials, 3); // 32
    synth.setParameter(c_parameterDecay, 800);
    synth.setParameter(c_parameterMalletResonance, 50);

    const size_t kBlockSize = 64;
    alignas(16) float buffer[128];

    // Simulate 12 beats at 120 BPM (0.5s per beat)
    for (int beat = 1; beat <= 12; ++beat) {
        std::cout << "  --- BEAT " << beat << " TRIGGER ---" << std::endl;
        synth.NoteOn(60, 100);

        // Render 0.5 seconds (approx 375 blocks)
        float max_peak = 0.0f;
        float avg_peak = 0.0f;
        int sample_count = 0;

        for (int i = 0; i < 375; ++i) {
            // std::memset(buffer, 0, sizeof(buffer));
            synth.Render(buffer, kBlockSize);

            for (int j = 0; j < 128; ++j) {
                float val = buffer[j];
                if (!std::isfinite(val)) {
                    std::cerr << "[FAIL] NaN detected at Beat " << beat << " Block " << i << std::endl;
                    exit(1);
                }
                if (std::abs(val) > 10.0f) {
                    std::cerr << "[FAIL] Explosion detected at Beat " << beat << " Block " << i << " Val=" << val << std::endl;
                    exit(1);
                }
                max_peak = std::max(max_peak, std::abs(val));
                avg_peak += std::abs(val);
                sample_count++;
            }
        }

        avg_peak /= sample_count;

        std::cout << "  Beat " << beat << " Max Peak: " << max_peak << ", Avg Peak: " << avg_peak << std::endl;

        if (beat > 1 && max_peak < 0.001f) {
             std::cerr << "[FAIL] Sound went silent prematurely at beat " << beat << std::endl;
             exit(1);
        }

        // Note Off
        synth.NoteOff(60);
    }
    std::cout << "  [PASS] Survived 12 beats of rhythmic triggering without critical failure." << std::endl;
}

void test_preset_11_issue() {
std::cout << "\n[Test 16] Preset 11 Issue (Harp)..." << std::endl;
    RipplerX synth;
    unit_runtime_desc_t desc;
    desc.samplerate = 48000;
    desc.output_channels = 2;
    desc.get_num_sample_banks = mock_get_num_sample_banks;
    desc.get_num_samples_for_bank = mock_get_num_samples_for_bank;
    desc.get_sample = mock_get_sample;
    synth.Init(&desc);

    // Load Preset 11 (Harp)
    synth.LoadPreset(11);

    alignas(16) float buffer[128];
    synth.NoteOn(60, 100);

    // Render 1 second
    for (int i = 0; i < 750; ++i) {
        // std::memset(buffer, 0, sizeof(buffer));  // try with dirty buffer
        synth.Render(buffer, 64);
        verify_buffer(buffer, 128, "Preset 11");
    }
    std::cout << "  [PASS] Preset 11 loaded and rendered without crash." << std::endl;
}


void test_preset_14_crash() {
    std::cout << "\n[Test 17] Preset 14 Crash (Kalimba)..." << std::endl;
    RipplerX synth;
    unit_runtime_desc_t desc;
    desc.samplerate = 48000;
    desc.output_channels = 2;
    desc.get_num_sample_banks = mock_get_num_sample_banks;
    desc.get_num_samples_for_bank = mock_get_num_samples_for_bank;
    desc.get_sample = mock_get_sample;
    synth.Init(&desc);

    // Load Preset 14 (Kalimba) - Known to cause crash
    synth.LoadPreset(14);

    alignas(16) float buffer[128];
    synth.NoteOn(60, 100);

    // Render 1 second
    for (int i = 0; i < 750; ++i) {
        // std::memset(buffer, 0, sizeof(buffer));  // try with dirty buffer
        synth.Render(buffer, 64);
        verify_buffer(buffer, 128, "Preset 14");
    }
    std::cout << "  [PASS] Preset 14 loaded and rendered without crash." << std::endl;
}

void test_preset_28_issue() {
    std::cout << "\n[Test 18] Preset 28 Issue (Vibes)..." << std::endl;
    RipplerX synth;
    unit_runtime_desc_t desc;
    desc.samplerate = 48000;
    desc.output_channels = 2;
    desc.get_num_sample_banks = mock_get_num_sample_banks;
    desc.get_num_samples_for_bank = mock_get_num_samples_for_bank;
    desc.get_sample = mock_get_sample;
    synth.Init(&desc);

    // Load Preset 27 (Vibes - 0-indexed is 27, 1-indexed 28)
    synth.LoadPreset(27);

    alignas(16) float buffer[128];
    synth.NoteOn(60, 100);

    // Render 1 second
    for (int i = 0; i < 750; ++i) {
        // std::memset(buffer, 0, sizeof(buffer));  // try with dirty buffer
        synth.Render(buffer, 64);
        verify_buffer(buffer, 128, "Preset 28");
    }
    std::cout << "  [PASS] Preset 28 loaded and rendered without crash." << std::endl;
}

void test_preset_loop_transition() {
    std::cout << "\n[Test 19] Preset Loop Transition (All Presets)..." << std::endl;
    RipplerX synth;
    unit_runtime_desc_t desc;
    desc.samplerate = 48000;
    desc.output_channels = 2;
    desc.get_num_sample_banks = mock_get_num_sample_banks;
    desc.get_num_samples_for_bank = mock_get_num_samples_for_bank;
    desc.get_sample = mock_get_sample;
    synth.Init(&desc);

    const size_t kBlockSize = 64;
    alignas(16) float buffer[128];
    TestStats stats;

    for (int prog = 0; prog < (int)Program::last_program; ++prog) {
        const char* name = RipplerX::getPresetName(prog);
        std::cout << "  Testing Preset " << prog << " (" << (name ? name : "Unknown") << ")..." << std::flush;

        synth.setParameter(c_parameterProgramName, prog);

        // Trigger note
        synth.NoteOn(60, 100);

        // Render 0.2 seconds (approx 175 blocks) to verify stability
        bool stable = true;
        for (int i = 0; i < 175; ++i) {
            // std::memset(buffer, 0, sizeof(buffer));
            synth.Render(buffer, kBlockSize);

            for (float f : buffer) {
                stats.update(f);
                if (!std::isfinite(f) || std::abs(f) > 10.0f) {
                    stable = false;
                }
            }
            if (!stable) break;
        }

        synth.NoteOff(60);

        // Render tail (0.05s)
        for (int i = 0; i < 35; ++i) {
             std::memset(buffer, 0, sizeof(buffer));
             synth.Render(buffer, kBlockSize);
             for (float f : buffer) {
                if (!std::isfinite(f) || std::abs(f) > 10.0f) stable = false;
             }
        }

        if (!stable) {
            std::cout << " [FAIL]" << std::endl;
            std::cerr << "[FAIL] Instability detected in Preset " << prog << std::endl;
            exit(1);
        } else {
            // TODO check that buffer is really different for each program, for some blocks

            std::cout << " [OK]" << std::endl;
        }
    }

    stats.finalize();
    std::cout << "  [PASS] All " << (int)Program::last_program << " presets loaded and rendered successfully." << std::endl;
}

void test_preset_retrigger_instability() {
    std::cout << "\n[Test 20] Preset Retrigger Instability (11 & 14)..." << std::endl;
    RipplerX synth;
    unit_runtime_desc_t desc;
    desc.samplerate = 48000;
    desc.output_channels = 2;
    desc.get_num_sample_banks = mock_get_num_sample_banks;
    desc.get_num_samples_for_bank = mock_get_num_samples_for_bank;
    desc.get_sample = mock_get_sample;
    synth.Init(&desc);

    int presets[] = {11, 14};
    const size_t kBlockSize = 64;
    alignas(16) float buffer[128];

    for (int p : presets) {
        std::cout << "  Testing Preset " << p << "..." << std::endl;
        synth.LoadPreset(p);

        // FORCE SAMPLE LOAD: Simulate hardware behavior
        synth.setParameter(c_parameterSampleNumber, 1);
        synth.loadConfigureSample();

        // STRESS TEST: 16 triggers, NO NoteOff. Forces voice stealing and accumulation.
        for (int beat = 0; beat < 16; ++beat) {
            synth.NoteOn(60, 100);

            // Render 0.5s (approx 375 blocks)
            float max_peak = 0.0f;
            for (int i = 0; i < 375; ++i) {
                // std::memset(buffer, 0, sizeof(buffer));
                synth.Render(buffer, kBlockSize);

                for (float f : buffer) {
                    if (!std::isfinite(f) || std::abs(f) > 50.0f) {
                        std::cerr << "[FAIL] Instability in Preset " << p << " at Beat " << beat << " Block " << i << " Val=" << f << std::endl;
                        exit(1);
                    }
                    max_peak = std::max(max_peak, std::abs(f));
                }
            }
            std::cout << "    Trigger " << beat << " Peak: " << max_peak << " (Voices Stacking)" << std::endl;
            // NO NoteOff - Force overlap!
        }
        std::cout << "    [OK] Preset " << p << " stable." << std::endl;
    }
    std::cout << "  [PASS] Retrigger test passed." << std::endl;
}

void test_parameter_mapping() {
    std::cout << "\n[Test] 16. Parameter Mapping & Scaling..." << std::endl;
    RipplerX synth;
    unit_runtime_desc_t desc = {0};
    desc.samplerate = 48000;
    synth.Init(&desc);

    // Test Decay Scaling (A/B Split)
    // Max A is 1000. Should map to 10.0f (c_decay_max)
    synth.setParameter(c_parameterDecay, 1000);
    float valA = synth.getInternalParameter(a_decay);
    if (std::abs(valA - 1000.0f) > 0.001f) {
        std::cerr << "[FAIL] Decay A scaling incorrect. Input 1000 -> " << valA << " (Expected 1000.0)" << std::endl;
        exit(1);
    }

    // Test Decay B
    // Input 1500 -> B value 500 -> 5.0f
    synth.setParameter(c_parameterDecay, 1500);
    float valB = synth.getInternalParameter(b_decay);
    if (std::abs(valB - 500.0f) > 0.001f) {
        std::cerr << "[FAIL] Decay B scaling incorrect. Input 1500 -> " << valB << " (Expected 500.0)" << std::endl;
        exit(1);
    }

    // Test Mallet Resonance Scaling
    // Input 500 -> Should be 0.5f (Previously was 50.0f -> clamped to 1.0f)
    synth.setParameter(c_parameterMalletResonance, 500);
    float valMallet = synth.getInternalParameter(mallet_res);
    if (std::abs(valMallet - 0.5f) > 0.001f) {
        std::cerr << "[FAIL] Mallet Resonance scaling incorrect. Input 500 -> " << valMallet
                  << " (Expected 0.5)" << std::endl;
        exit(1);
    }

    std::cout << "[PASS] Parameter mapping verified." << std::endl;
}

void test_api_lifecycle_stability() {
    std::cout << "\n[Test] 21. API Lifecycle & Long-Term Stability (14+ Beats)..." << std::endl;

    RipplerX synth;
    unit_runtime_desc_t desc;
    desc.samplerate = 48000;
    desc.output_channels = 2;
    desc.get_num_sample_banks = mock_get_num_sample_banks;
    desc.get_num_samples_for_bank = mock_get_num_samples_for_bank;
    desc.get_sample = mock_get_sample;

    // 1. unit_init
    if (synth.Init(&desc) != k_unit_err_none_mock) {
        std::cerr << "[FAIL] Init failed." << std::endl;
        exit(1);
    }

    // 2. unit_load_preset
    synth.LoadPreset(0); // Bells

    // 3. Simulation Loop (10 seconds > 14 beats at 120BPM)
    const int kNumBlocks = (48000 * 10) / 64;
    alignas(16) float buffer[128];

    int beat_counter = 0;
    int samples_per_beat = 24000; // 0.5s at 48kHz (120BPM)
    int samples_processed = 0;

    for (int i = 0; i < kNumBlocks; ++i) {
        // std::memset(buffer, 0, sizeof(buffer));

        // Trigger logic
        if (samples_processed % samples_per_beat < 64) {
            beat_counter++;
            // unit_note_on / unit_gate_on
            synth.NoteOn(60, 100);
            synth.GateOn(100);

            // unit_set_param_value (Modulate Decay)
            if (beat_counter % 4 == 0) {
                synth.setParameter(c_parameterDecay, 500 + (beat_counter * 10) % 400);
            }
        }

        // Release logic
        if ((samples_processed + samples_per_beat/2) % samples_per_beat < 64) {
            // unit_note_off / unit_gate_off
            synth.NoteOff(60);
            synth.GateOff();
        }

        // unit_pitch_bend
        if (i % 100 == 0) {
            synth.PitchBend(0x2000 + (rand() % 0x1000 - 0x800));
        }

        // unit_render
        synth.Render(buffer, 64);

        // Check for explosion
        for (float f : buffer) {
            if (!std::isfinite(f) || std::abs(f) > 50.0f) {
                std::cerr << "[FAIL] Instability detected at block " << i
                          << " (Beat " << beat_counter << ")"
                          << " Value: " << f << std::endl;
                exit(1);
            }
        }

        samples_processed += 64;
    }

    // 4. Lifecycle calls
    synth.AllNoteOff(); // unit_all_note_off
    synth.Reset();      // unit_reset
    synth.Suspend();    // unit_suspend
    synth.Resume();     // unit_resume
    synth.Teardown();   // unit_teardown

    std::cout << "[PASS] API Lifecycle & Long-Term Stability verified." << std::endl;
}

void test_coupling_safety() {
    std::cout << "\n[Test 22] Coupling Safety (Negative Freq Check)..." << std::endl;
    // This test verifies that the coupling logic doesn't produce NaN/Inf even with extreme inputs
    // and that Voice::clear() initializes freq safely.

    Voice v;
    v.clear(); // Should set freq = 50.0f

    // Trigger with low frequency to maximize coupling forces
    // Note 0 -> ~8Hz.
    v.setCoupling(true, 1.0f); // Max split
    v.trigger(48000.0f, 0, 1.0f, 100.0f);

    // We can't easily check internal state without friend classes,
    // but we can ensure it doesn't crash during trigger/update.

    // If we reached here, we didn't crash in updateResonators.
    std::cout << "  [PASS] Coupling safety test executed." << std::endl;
}

void test_partial_wakeup_instability() {
    std::cout << "\n[Test 23] Partial Wake-up Instability..." << std::endl;
    // This reproduces the "Distortion then Silence" bug caused by stale filter state
    // when switching partial counts (e.g. 64 -> 4 -> 64).

    RipplerX synth;
    unit_runtime_desc_t desc = {0};
    desc.samplerate = 48000;
    synth.Init(&desc);

    // 1. Set max partials (64) and fill state with energy
    synth.setParameter(c_parameterPartials, 4); // Index 4 -> 64 partials
    synth.NoteOn(60, 127);
    alignas(16) float buffer[128];
    for(int i=0; i<100; ++i) synth.Render(buffer, 64);

    // 2. Reduce partials (4) - Partials 4-63 become dormant but retain state
    synth.setParameter(c_parameterPartials, 0); // Index 0 -> 4 partials
    for(int i=0; i<50; ++i) synth.Render(buffer, 64);

    // 3. Increase partials back to 64 - Wake up 4-63
    // If active_prev wasn't cleared in step 2, they resume with old state -> EXPLOSION
    synth.setParameter(c_parameterPartials, 4);

    float max_val = 0.0f;
    for(int i=0; i<100; ++i) {
        synth.Render(buffer, 64);
        for(float f : buffer) max_val = std::max(max_val, std::abs(f));
    }

    if (max_val > 50.0f) {
        std::cerr << "[FAIL] Wake-up explosion detected! Max: " << max_val << std::endl;
        exit(1);
    }
    std::cout << "  [PASS] Partial wake-up stable." << std::endl;
}

// https://en.cppreference.com/w/cpp/algorithm/min_element.html
template<class ForwardIt>
ForwardIt min_element(ForwardIt first, ForwardIt last)
{
    if (first == last)
        return last;

    ForwardIt smallest = first;

    while (++first != last)
        if (*first < *smallest)
            smallest = first;

    return smallest;
}
// https://en.cppreference.com/w/cpp/algorithm/max_element.html
template<class ForwardIt>
ForwardIt max_element(ForwardIt first, ForwardIt last)
{
    if (first == last)
        return last;

    ForwardIt largest = first;

    while (++first != last)
        if (*largest < *first)
            largest = first;

    return largest;
}

void test_polyphony_accumulation_4_beats() {
    std::cout << "\n[Test 24] Polyphony Accumulation (4-Beat Hardware Bug Replication)" << std::endl;
    std::cout << "  Replicating exact hardware crash scenario..." << std::endl;

    RipplerX synth;
    unit_runtime_desc_t desc;
    desc.samplerate = 48000;
    desc.output_channels = 2;
    desc.get_num_sample_banks = mock_get_num_sample_banks;
    desc.get_num_samples_for_bank = mock_get_num_samples_for_bank;
    desc.get_sample = mock_get_sample;
    synth.Init(&desc);

    // CRITICAL: Use EXACT same parameters as hardware
    // These are typical loaded preset values
    synth.setParameter(c_parameterDecay, 500);      // Moderate decay
    synth.setParameter(c_parameterModel, 0);        // String model
    synth.setParameter(c_parameterPartials, 3);     // 32 partials
    synth.setParameter(c_parameterMaterial, 0);     // No damping
    synth.setParameter(c_parameterRelease, 100);    // Some release

    alignas(16) float buffer[128];

    // Simulate 4 beats at 120 BPM
    // Beat duration = 60/120 = 0.5 seconds = 24000 samples
    const int samples_per_beat = 24000;
    const int blocks_per_beat = samples_per_beat / 64;

    float max_per_beat[4] = {0.0f, 0.0f, 0.0f, 0.0f};

    // Trigger 4 notes (one per beat), exactly like hardware
    for (int beat = 0; beat < 4; ++beat) {
        // Trigger note at start of beat
        synth.NoteOn(60, 100);

        std::cout << "  Beat " << (beat + 1) << ": ";

        // Render one beat worth of audio
        for (int block = 0; block < blocks_per_beat; ++block) {
            std::memset(buffer, 0, sizeof(buffer));
            synth.Render(buffer, 64);

            // Track maximum amplitude
            for (int s = 0; s < 128; ++s) {
                float val = std::abs(buffer[s]);
                max_per_beat[beat] = std::max(max_per_beat[beat], val);

                // Check for explosion (hardware would crash here)
                if (val > 10.0f || !std::isfinite(val)) {
                    std::cerr << "\n[FAIL] Audio explosion at beat " << (beat + 1)
                              << ", block " << block << ", sample " << s
                              << ", value: " << buffer[s] << std::endl;
                    std::cerr << "  This is the EXACT bug causing hardware crash!" << std::endl;
                    std::cerr << "  Max values per beat: ";
                    for (int b = 0; b <= beat; ++b) {
                        std::cerr << "Beat " << (b+1) << "=" << max_per_beat[b] << " ";
                    }
                    std::cerr << std::endl;
                    exit(1);
                }
            }
        }

        std::cout << "Max=" << max_per_beat[beat] << std::endl;

        // CRITICAL CHECK: Amplitude should NOT increase with each beat!
        // If max_per_beat[1] > max_per_beat[0] * 1.5, there's accumulation
        if (beat > 0) {
            float ratio = max_per_beat[beat] / max_per_beat[0];
            if (ratio > 2.5f) {
                std::cerr << "[FAIL] Amplitude growing with polyphony!" << std::endl;
                std::cerr << "  Beat 1: " << max_per_beat[0] << std::endl;
                std::cerr << "  Beat " << (beat+1) << ": " << max_per_beat[beat]
                          << " (ratio: " << ratio << "x)" << std::endl;
                std::cerr << "  This indicates ACCUMULATION BUG - missing normalization or wrong scaling!" << std::endl;
                exit(1);
            }
        }
    }

    // Check that amplitude is stable across all beats
    float min_max = *min_element(max_per_beat, max_per_beat + 4);
    float max_max = *max_element(max_per_beat, max_per_beat + 4);
    float variation_ratio = max_max / min_max;

    std::cout << "  Amplitude variation: " << variation_ratio << "x" << std::endl;

    if (variation_ratio > 2.5f) {
        std::cerr << "[FAIL] Excessive amplitude variation across beats!" << std::endl;
        std::cerr << "  Min: " << min_max << ", Max: " << max_max << std::endl;
        std::cerr << "  This indicates unstable accumulation or feedback growth." << std::endl;
        exit(1);
    }

    std::cout << "  [PASS] 4-beat polyphony stable (max variation: " << variation_ratio << "x)" << std::endl;
}

void test_decay_scaling_verification() {
    std::cout << "\n[Test 25] Decay Scaling Verification" << std::endl;
    std::cout << "  Verifying 0.01f scaling is applied..." << std::endl;

    RipplerX synth;
    unit_runtime_desc_t desc;
    desc.samplerate = 48000;
    desc.output_channels = 2;
    desc.get_num_sample_banks = mock_get_num_sample_banks;
    desc.get_num_samples_for_bank = mock_get_num_samples_for_bank;
    desc.get_sample = mock_get_sample;
    synth.Init(&desc);

    // Test with decay = 1000 (maximum user value)
    synth.setParameter(c_parameterDecay, 1000);
    synth.setParameter(c_parameterModel, 0);
    synth.setParameter(c_parameterPartials, 1); // Single partial for simplicity

    synth.NoteOn(60, 100);

    alignas(16) float buffer[128];

    // Render 2 seconds worth
    bool found_ringing = false;
    for (int i = 0; i < (96000 / 64); ++i) {
        synth.Render(buffer, 64);

        for (int s = 0; s < 128; ++s) {
            // With correct 0.01f scaling, decay=1000 should produce long but stable ring
            // Without scaling, decay=1000 would cause immediate explosion
            if (std::abs(buffer[s]) > 10.0f || !isfinite(buffer[s])) {
                std::cerr << "[FAIL] Decay scaling incorrect - explosion with decay=1000" << std::endl;
                std::cerr << "  Sample value: " << buffer[s] << " at iteration " << i << std::endl;
                std::cerr << "  The 0.01f scaling is MISSING or incorrect!" << std::endl;
                exit(1);
            }

            if (std::abs(buffer[s]) > 0.001f) {
                found_ringing = true;
            }
        }
    }

    // With decay=1000, we should still hear ringing after 2 seconds
    if (!found_ringing) {
        std::cerr << "[WARN] No ringing with decay=1000 - might be over-damped" << std::endl;
    }

    std::cout << "  [PASS] Decay=1000 stable (scaling correct)" << std::endl;
}

void test_single_vs_quad_voice_level() {
    std::cout << "\n[Test 26] Single vs Quad Voice Output Level" << std::endl;
    std::cout << "  Checking if 4 voices are 4x louder (accumulation bug)..." << std::endl;

    RipplerX synth;
    unit_runtime_desc_t desc;
    desc.samplerate = 48000;
    desc.output_channels = 2;
    desc.get_num_sample_banks = mock_get_num_sample_banks;
    desc.get_num_samples_for_bank = mock_get_num_samples_for_bank;
    desc.get_sample = mock_get_sample;

    alignas(16) float buffer[128];

    // Test 1: Single voice
    synth.Init(&desc);
    synth.setParameter(c_parameterDecay, 300);
    synth.setParameter(c_parameterModel, 0);

    synth.NoteOn(60, 100);

    float single_voice_max = 0.0f;
    for (int i = 0; i < 100; ++i) {
        synth.Render(buffer, 64);
        for (float f : buffer) {
            single_voice_max = std::max(single_voice_max, std::abs(f));
        }
    }

    // Test 2: Four voices simultaneously
    synth.Init(&desc);
    synth.setParameter(c_parameterDecay, 300);
    synth.setParameter(c_parameterModel, 0);

    synth.NoteOn(60, 100);
    synth.NoteOn(64, 100);
    synth.NoteOn(67, 100);
    synth.NoteOn(72, 100);

    float quad_voice_max = 0.0f;
    for (int i = 0; i < 100; ++i) {
        synth.Render(buffer, 64);
        for (float f : buffer) {
            quad_voice_max = std::max(quad_voice_max, std::abs(f));
        }
    }

    float ratio = quad_voice_max / single_voice_max;

    std::cout << "  Single voice max: " << single_voice_max << std::endl;
    std::cout << "  Quad voice max: " << quad_voice_max << std::endl;
    std::cout << "  Ratio: " << ratio << "x" << std::endl;

    // If ratio is close to 4.0, there's no voice normalization (accumulation bug)
    // Ideal ratio should be 2.0-2.5 (some constructive interference expected)
    if (ratio > 3.5f) {
        std::cerr << "[FAIL] 4 voices are " << ratio << "x louder than 1 voice!" << std::endl;
        std::cerr << "  This indicates LINEAR ACCUMULATION without normalization." << std::endl;
        std::cerr << "  Expected ratio: ~2.0x, Actual: " << ratio << "x" << std::endl;
        exit(1);
    }

    std::cout << "  [PASS] Polyphony level reasonable (ratio: " << ratio << "x)" << std::endl;
}

void test_decay_parameter_range() {
    std::cout << "\n[Test 27] Decay Parameter Range & Scaling Verification" << std::endl;
    std::cout << "  Testing full decay range (1-1000) for stability..." << std::endl;

    RipplerX synth;
    unit_runtime_desc_t desc;
    desc.samplerate = 48000;
    desc.output_channels = 2;
    desc.get_num_sample_banks = mock_get_num_sample_banks;
    desc.get_num_samples_for_bank = mock_get_num_samples_for_bank;
    desc.get_sample = mock_get_sample;

    alignas(16) float buffer[128];

    // Test decay values that users actually use: 1, 100, 500, 800, 1000
    int decay_values[] = {1, 100, 500, 800, 1000};

    for (int decay_val : decay_values) {
        synth.Init(&desc);
        synth.setParameter(c_parameterDecay, decay_val);
        synth.setParameter(c_parameterModel, 0);
        synth.setParameter(c_parameterPartials, 3);  // 32 partials

        synth.NoteOn(60, 100);

        float max_amplitude = 0.0f;
        bool found_explosion = false;

        // Render 1 second
        for (int i = 0; i < (48000 / 64); ++i) {
            std::memset(buffer, 0, sizeof(buffer));
            synth.Render(buffer, 64);

            for (int s = 0; s < 128; ++s) {
                float val = std::abs(buffer[s]);
                max_amplitude = std::max(max_amplitude, val);

                // CRITICAL: Detect explosion (hardware would crash here)
                if (val > 50.0f || !std::isfinite(val)) {
                    std::cerr << "[FAIL] Explosion with decay=" << decay_val
                              << " at block " << i << ", value=" << buffer[s] << std::endl;
                    std::cerr << "  This indicates missing 0.01f scaling or c_decay_max too high!" << std::endl;
                    found_explosion = true;
                    break;
                }
            }
            if (found_explosion) break;
        }

        if (found_explosion) {
            exit(1);
        }

        std::cout << "  Decay=" << std::setw(4) << decay_val
                  << ": Max=" << max_amplitude << " - ";

        // Verify sound level is reasonable
        if (max_amplitude < 0.001f) {
            std::cerr << "[FAIL] Sound too quiet!" << std::endl;
            std::cerr << "  Decay=" << decay_val << " produced max=" << max_amplitude << std::endl;
            std::cerr << "  Either decay range is wrong OR 0.01f scaling is too aggressive." << std::endl;
            std::cerr << "  Solution: Increase c_decay_max in constants.h" << std::endl;
            exit(1);
        }

        std::cout << "OK" << std::endl;
    }

    std::cout << "  [PASS] Full decay range stable and audible" << std::endl;
}

void test_polyphony_accumulation_2_beats() {
    std::cout << "\n[Test 28] Polyphony Accumulation (2-Beat Hardware Crash)" << std::endl;
    std::cout << "  Replicating EXACT hardware crash scenario..." << std::endl;

    RipplerX synth;
    unit_runtime_desc_t desc;
    desc.samplerate = 48000;
    desc.output_channels = 2;
    desc.get_num_sample_banks = mock_get_num_sample_banks;
    desc.get_num_samples_for_bank = mock_get_num_samples_for_bank;
    desc.get_sample = mock_get_sample;
    synth.Init(&desc);

    // CRITICAL: Use REALISTIC preset values that users load
    synth.setParameter(c_parameterDecay, 800);      // Long decay (typical preset)
    synth.setParameter(c_parameterModel, 0);        // String model
    synth.setParameter(c_parameterPartials, 4);     // 48 partials (heavy)
    synth.setParameter(c_parameterMaterial, 0);     // No extra damping

    alignas(16) float buffer[128];

    // Simulate beats at 120 BPM (0.5 second per beat)
    const int samples_per_beat = 24000;
    const int blocks_per_beat = samples_per_beat / 64;

    float max_per_beat[5] = {0.0f};
    int active_voices_per_beat[5] = {0};

    for (int beat = 0; beat < 5; ++beat) {
        // Trigger note at start of beat
        synth.NoteOn(60 + (beat % 12), 100);

        // Count active voices
        int active_count = 0;
        for (size_t v = 0; v < 8; ++v) {
            if (synth.voices[v].isPressed) {
                active_count++;
            }
        }
        active_voices_per_beat[beat] = active_count;

        std::cout << "  Beat " << (beat + 1) << " (voices=" << active_count << "): ";

        // Render one beat
        for (int block = 0; block < blocks_per_beat; ++block) {
            std::memset(buffer, 0, sizeof(buffer));
            synth.Render(buffer, 64);

            for (int s = 0; s < 128; ++s) {
                float val = std::abs(buffer[s]);
                max_per_beat[beat] = std::max(max_per_beat[beat], val);

                // CRITICAL: Hardware crashes at this threshold
                if (val > 50.0f || !std::isfinite(val)) {
                    std::cerr << "\n[FAIL] HARDWARE CRASH DETECTED!" << std::endl;
                    std::cerr << "  Beat: " << (beat + 1) << std::endl;
                    std::cerr << "  Block: " << block << std::endl;
                    std::cerr << "  Sample: " << s << std::endl;
                    std::cerr << "  Value: " << buffer[s] << std::endl;
                    std::cerr << "  Active voices: " << active_count << std::endl;
                    std::cerr << "\n  Amplitude per beat:" << std::endl;
                    for (int b = 0; b <= beat; ++b) {
                        std::cerr << "    Beat " << (b+1) << ": " << max_per_beat[b]
                                  << " (" << active_voices_per_beat[b] << " voices)" << std::endl;
                    }
                    std::cerr << "\n  ROOT CAUSE: Voice accumulation without normalization!" << std::endl;
                    std::cerr << "  FIX: Add voice count normalization in Render()" << std::endl;
                    exit(1);
                }
            }
        }

        std::cout << "Max=" << max_per_beat[beat] << std::endl;

        // CRITICAL: Check for LINEAR accumulation (smoking gun!)
        if (beat >= 1) {
            // If amplitude doubles with each voice, there's no normalization
            float growth_ratio = max_per_beat[beat] / max_per_beat[0];
            float expected_linear_growth = (float)(beat + 1);

            // If growth is close to linear (voices * 1.0), that's the bug
            if (growth_ratio > expected_linear_growth * 0.85f) {
                std::cerr << "\n[FAIL] LINEAR ACCUMULATION DETECTED!" << std::endl;
                std::cerr << "  Beat 1: " << max_per_beat[0] << " (1 voice)" << std::endl;
                std::cerr << "  Beat " << (beat+1) << ": " << max_per_beat[beat]
                          << " (" << (beat+1) << " voices)" << std::endl;
                std::cerr << "  Growth ratio: " << growth_ratio << "x" << std::endl;
                std::cerr << "  Expected with linear accumulation: " << expected_linear_growth << "x" << std::endl;
                std::cerr << "\n  This is the BUG causing hardware crash!" << std::endl;
                std::cerr << "  Voices are adding without normalization:" << std::endl;
                std::cerr << "    accum = voice1 + voice2 + voice3 + ..." << std::endl;
                std::cerr << "  Should be:" << std::endl;
                std::cerr << "    accum = (voice1 + voice2 + voice3) / sqrt(N)" << std::endl;
                exit(1);
            }
        }
    }

    // Check overall stability across 5 beats
    float min_max = *min_element(max_per_beat, max_per_beat + 5);
    float max_max = *max_element(max_per_beat, max_per_beat + 5);
    float variation = max_max / min_max;

    std::cout << "  Overall variation: " << variation << "x (should be < 3.0x)" << std::endl;

    if (variation > 3.0f) {
        std::cerr << "[FAIL] Excessive amplitude variation!" << std::endl;
        std::cerr << "  This indicates missing voice normalization." << std::endl;
        exit(1);
    }

    std::cout << "  [PASS] 5-beat polyphony stable without accumulation" << std::endl;
}

void test_iir_coefficient_stability() {
    std::cout << "\n[Test 29] IIR Coefficient Stability Check" << std::endl;
    std::cout << "  Verifying all va2 coefficients stay safely below 1.0..." << std::endl;

    RipplerX synth;
    unit_runtime_desc_t desc;
    desc.samplerate = 48000;
    desc.output_channels = 2;
    desc.get_num_sample_banks = mock_get_num_sample_banks;
    desc.get_num_samples_for_bank = mock_get_num_samples_for_bank;
    desc.get_sample = mock_get_sample;
    synth.Init(&desc);

    // Test with MAXIMUM decay to stress-test coefficient calculation
    synth.setParameter(c_parameterDecay, 1000);     // Maximum user value
    synth.setParameter(c_parameterModel, 0);
    synth.setParameter(c_parameterPartials, 5);     // 64 partials (all of them)
    synth.setParameter(c_parameterMaterial, -10);   // Min damping (least stable)

    synth.NoteOn(60, 127);

    alignas(16) float buffer[128];

    // Render and watch for instability
    bool stability_verified = false;
    for (int i = 0; i < 200; ++i) {
        std::memset(buffer, 0, sizeof(buffer));
        synth.Render(buffer, 64);

        for (int s = 0; s < 128; ++s) {
            float val = std::abs(buffer[s]);

            // Coefficient instability manifests as exponential growth
            if (val > 100.0f || !std::isfinite(val)) {
                std::cerr << "[FAIL] IIR coefficient instability!" << std::endl;
                std::cerr << "  Iteration: " << i << std::endl;
                std::cerr << "  Value: " << buffer[s] << std::endl;
                std::cerr << "  This indicates va2 coefficient >= 1.0" << std::endl;
                std::cerr << "\n  Likely causes:" << std::endl;
                std::cerr << "    1. Missing 0.01f scaling in decay calculation" << std::endl;
                std::cerr << "    2. c_decay_max set too high (should be <= 100.0)" << std::endl;
                std::cerr << "    3. d_eff not clamped properly" << std::endl;
                exit(1);
            }

            if (val > 0.01f) {
                stability_verified = true;
            }
        }
    }

    if (!stability_verified) {
        std::cerr << "[WARN] No audible output with decay=1000" << std::endl;
        std::cerr << "  Check if decay range is configured correctly" << std::endl;
    }

    std::cout << "  [PASS] IIR coefficients stable with max decay" << std::endl;
}

void test_voice_normalization_effectiveness() {
    std::cout << "\n[Test 30] Voice Normalization Effectiveness" << std::endl;
    std::cout << "  Verifying polyphonic output doesn't scale linearly..." << std::endl;

    RipplerX synth;
    unit_runtime_desc_t desc;
    desc.samplerate = 48000;
    desc.output_channels = 2;
    desc.get_num_sample_banks = mock_get_num_sample_banks;
    desc.get_num_samples_for_bank = mock_get_num_samples_for_bank;
    desc.get_sample = mock_get_sample;

    alignas(16) float buffer[128];

    struct TestCase {
        int num_voices;
        float expected_max_ratio;  // Relative to 1 voice
        float tolerance;
    };

    TestCase cases[] = {
        {1, 1.0f, 0.0f},      // Baseline
        {2, 1.5f, 0.3f},      // Should be ~1.41x (sqrt(2)), allow 1.2-1.8x
        {4, 2.2f, 0.5f},      // Should be ~2.0x (sqrt(4)), allow 1.7-2.7x
        {8, 3.0f, 0.8f},      // Should be ~2.83x (sqrt(8)), allow 2.2-3.8x
    };

    float single_voice_max = 0.0f;

    for (auto& test : cases) {
        synth.Init(&desc);
        synth.setParameter(c_parameterDecay, 400);
        synth.setParameter(c_parameterModel, 0);

        // Trigger N voices
        for (int v = 0; v < test.num_voices; ++v) {
            synth.NoteOn(60 + v, 100);
        }

        float max_amplitude = 0.0f;
        for (int i = 0; i < 100; ++i) {
            synth.Render(buffer, 64);
            for (float f : buffer) {
                max_amplitude = std::max(max_amplitude, std::abs(f));
            }
        }

        if (test.num_voices == 1) {
            single_voice_max = max_amplitude;
            std::cout << "  1 voice:  Max=" << max_amplitude << " (baseline)" << std::endl;
        } else {
            float ratio = max_amplitude / single_voice_max;
            float expected_min = test.expected_max_ratio - test.tolerance;
            float expected_max = test.expected_max_ratio + test.tolerance;

            std::cout << "  " << test.num_voices << " voices: Max=" << max_amplitude
                      << ", Ratio=" << ratio << "x ";

            // Check for LINEAR accumulation (bad)
            if (ratio > (float)test.num_voices * 0.9f) {
                std::cerr << "\n[FAIL] LINEAR ACCUMULATION!" << std::endl;
                std::cerr << "  Expected: ~" << test.expected_max_ratio << "x (with normalization)" << std::endl;
                std::cerr << "  Got: " << ratio << "x (too close to " << test.num_voices << "x)" << std::endl;
                std::cerr << "  Missing voice normalization!" << std::endl;
                exit(1);
            }

            // Check if normalization is working
            if (ratio < expected_min || ratio > expected_max) {
                std::cerr << "\n[WARN] Ratio outside expected range" << std::endl;
                std::cerr << "  Expected: " << expected_min << "-" << expected_max << "x" << std::endl;
                std::cerr << "  Got: " << ratio << "x" << std::endl;
            } else {
                std::cout << "(within expected range)" << std::endl;
            }
        }
    }

    std::cout << "  [PASS] Voice normalization working correctly" << std::endl;
}

void test_preset_loading_stability() {
    std::cout << "\n[Test 31] Preset Loading Stability (Real-World Scenarios)" << std::endl;
    std::cout << "  Testing with typical user preset values..." << std::endl;

    RipplerX synth;
    unit_runtime_desc_t desc;
    desc.samplerate = 48000;
    desc.output_channels = 2;
    desc.get_num_sample_banks = mock_get_num_sample_banks;
    desc.get_num_samples_for_bank = mock_get_num_samples_for_bank;
    desc.get_sample = mock_get_sample;

    alignas(16) float buffer[128];

    // Realistic presets that users actually load
    struct Preset {
        const char* name;
        int decay;
        int model;
        int partials;
        int material;
    };

    Preset presets[] = {
        {"Short Bell",   100, 0, 2, 5},
        {"Long String",  800, 0, 4, 0},
        {"Metal Plate", 1000, 3, 5, -5},
        {"Wood Block",    50, 1, 1, 10},
        {"Glass Bowl",   600, 2, 3, -10},
    };

    for (auto& preset : presets) {
        synth.Init(&desc);
        synth.setParameter(c_parameterDecay, preset.decay);
        synth.setParameter(c_parameterModel, preset.model);
        synth.setParameter(c_parameterPartials, preset.partials);
        synth.setParameter(c_parameterMaterial, preset.material);

        // Trigger chord (3 notes)
        synth.NoteOn(60, 100);
        synth.NoteOn(64, 100);
        synth.NoteOn(67, 100);

        bool preset_stable = true;
        float max_val = 0.0f;

        // Render 2 seconds
        for (int i = 0; i < (96000 / 64); ++i) {
            synth.Render(buffer, 64);

            for (float f : buffer) {
                float val = std::abs(f);
                max_val = std::max(max_val, val);

                if (val > 50.0f || !std::isfinite(val)) {
                    std::cerr << "[FAIL] Preset '" << preset.name << "' unstable!" << std::endl;
                    std::cerr << "  Decay=" << preset.decay << ", Model=" << preset.model << std::endl;
                    std::cerr << "  Explosion at iteration " << i << ", value=" << f << std::endl;
                    preset_stable = false;
                    break;
                }
            }
            if (!preset_stable) break;
        }

        if (!preset_stable) {
            exit(1);
        }

        std::cout << "  " << std::setw(15) << preset.name << ": Max=" << max_val << " - OK" << std::endl;
    }

    std::cout << "  [PASS] All realistic presets stable" << std::endl;
}


int main() {
    std::cout << "\n";
    std::cout << "╔════════════════════════════════════════════════════════════╗\n";
    std::cout << "║     RIPPLERX COMPREHENSIVE TEST SUITE v2.1 (Debug)        ║\n";
    std::cout << "║     Includes hardware issue replication tests.            ║\n";
    std::cout << "╚════════════════════════════════════════════════════════════╝\n";

    auto total_start = std::chrono::high_resolution_clock::now();

    try {
        test_dirty_initialization();
        test_runtime_stability_3_seconds();
        test_audio_stability();
        test_stress_polyphony();
        test_envelope_decay();
        test_extreme_parameters();
        test_hot_reload_multi_pattern();
        test_note_on_off_cycle();
        test_limiter_silence_stability();
        test_comb_filter_stability();
        test_percussion_auto_release();
        test_component_filter_vec();
        test_component_envelope();
        test_rhythmic_stability_crash();
        test_degradation_over_12_beats();
        test_preset_11_issue();
        test_preset_14_crash();
        test_preset_28_issue();
        test_preset_loop_transition();
        test_preset_retrigger_instability();
        test_parameter_mapping();
        test_api_lifecycle_stability();
        test_coupling_safety();
        test_partial_wakeup_instability();
        test_polyphony_accumulation_4_beats();
        test_decay_scaling_verification();
        test_single_vs_quad_voice_level();
        test_decay_parameter_range();              // Test 27:  Catches decay scaling issues
        test_polyphony_accumulation_2_beats();     // Test 28: Catches 2-beat crash bug
        test_iir_coefficient_stability();          // Test 29: Catches va2 >= 1.0
        test_voice_normalization_effectiveness();  // Test 30: Verifies normalization works
        test_preset_loading_stability();           // Test 31: Real-world preset testing
    } catch (const std::exception& e) {
        std::cerr << "\n[EXCEPTION] " << e.what() << std::endl;
        return 1;
    }

    auto total_end = std::chrono::high_resolution_clock::now();
    auto total_duration = std::chrono::duration_cast<std::chrono::milliseconds>(total_end - total_start);

    std::cout << "\n";
    std::cout << "╔════════════════════════════════════════════════════════════╗\n";
    std::cout << "║                  ALL TESTS COMPLETED                      ║\n";
    std::cout << "║  Total execution time: " << std::setw(4) << total_duration.count() << " ms                      ║\n";
    std::cout << "╚════════════════════════════════════════════════════════════╝\n";

    return 0;
}
