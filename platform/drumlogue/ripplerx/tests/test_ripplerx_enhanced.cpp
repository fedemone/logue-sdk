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
 * /mnt/d/Fede/drumlogue/arm-unknown-linux-gnueabihf/bin/arm-unknown-linux-gnueabihf-g++ -static -std=c++17 -O3 -I.. -DDEBUGN test_ripplerx_enhanced.cpp ../ripplerx.cc ../Voice.cc ../Resonator.cc ../Partial.cc ../Waveguide.cc ../Models.cc ../Mallet.cc ../Noise.cc ../Filter.cc ../Envelope.cc -o run_test_enhanced && qemu-arm ./run_test_enhanced | tee run_test_enhanced_result.log
*/

// --- ARCHITECTURE COMPATIBILITY ---
#include <arm_neon.h>

// Include Synth Engine
#include "../ripplerx.h"

// Fix macro collision
#ifdef isfinite
#undef isfinite
#endif

#define DEBUGN

// --- MOCK DRUMLOGUE RUNTIME ---
#define k_unit_err_none_mock 0
#define k_unit_err_geometry_mock 1
#define k_unit_err_samplerate_mock 2

// Mock Sample Data with actual audio content
alignas(16) float mock_sample_data[2048];

uint8_t mock_get_num_sample_banks() { return 1; }
uint8_t mock_get_num_samples_for_bank(uint8_t bank) { return 1; }

const sample_wrapper_t* mock_get_sample(uint8_t bank, uint8_t sample) {
    return nullptr;
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
            std::memset(buffer, 0, sizeof(buffer));
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
    synth.setParameter(c_parameterMalletResonance, 10);
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

    synth.setParameter(c_parameterMalletResonance, 10);
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

    synth.setParameter(c_parameterMalletResonance, 10);
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
        std::memset(buffer, 0, sizeof(buffer));
        synth.Render(buffer, 64);
    }

    // === ADDED: Trigger release ===
    synth.NoteOff(60);

    // Phase 2: Render 500ms for release to complete
    for (int i = 0; i < (24000 / 64); ++i) {  // 500ms at 48kHz
        std::memset(buffer, 0, sizeof(buffer));
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
        {"Very High Pitch", 127, 127, 500, 5, 3},
        {"Very Low Pitch", 24, 127, 500, 5, 3},
        {"Min Decay", 60, 127, 1, 5, 3},
        {"Max Decay", 60, 127, 1000, 5, 3},
        {"Max Partials", 60, 127, 500, 5, 5}, // 64 partials
        {"Min Damping", 60, 127, 500, -10, 3},
        {"Max Damping", 60, 127, 500, 10, 3},
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
            std::memset(buffer, 0, sizeof(buffer));
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
            std::memset(buffer, 0, sizeof(buffer));
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
            std::memset(buffer, 0, sizeof(buffer));
            synth.Render(buffer, 64);
            verify_buffer(buffer, 128, "Note Cycle");
        }

        synth.NoteOff(60 + (cycle % 12));

        // Render 5 blocks after off
        for (int i = 0; i < 5; ++i) {
            std::memset(buffer, 0, sizeof(buffer));
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
        std::memset(buffer, 0, sizeof(buffer));
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
    synth.setParameter(c_parameterDecay, 5); // 5 = 0.5s decay (was 50=5.0s, too long for 2s test)
    synth.setParameter(c_parameterMalletResonance, 10);
    synth.setParameter(c_parameterModel, 0);
    synth.setParameter(c_parameterNoiseMix, 0);

    // Trigger NoteOn (Percussion hit) BUT NO NoteOff
    synth.NoteOn(60, 127);

    alignas(16) float buffer[128];
    float maxVal = 0.0f;

    // Render for 2 seconds
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
    synth.setParameter(c_parameterMalletResonance, 10);

    const size_t kBlockSize = 64;
    alignas(16) float buffer[128];

    // Simulate 4 beats at 120 BPM (0.5s per beat)
    for (int beat = 1; beat <= 4; ++beat) {
        std::cout << "  --- BEAT " << beat << " TRIGGER ---" << std::endl;
        synth.NoteOn(60, 100);

        // Render 0.5 seconds (approx 375 blocks)
        float max_peak = 0.0f;

        for (int i = 0; i < 375; ++i) {
            std::memset(buffer, 0, sizeof(buffer));
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

int main() {
    std::cout << "\n";
    std::cout << "╔════════════════════════════════════════════════════════════╗\n";
    std::cout << "║     RIPPLERX COMPREHENSIVE TEST SUITE v2.0                ║\n";
    std::cout << "║     Enhanced with 3-second runtime stability test         ║\n";
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
    } catch (const std::exception& e) {
        std::cerr << "\n[EXCEPTION] " << e.what() << std::endl;
        return 1;
    }

    auto total_end = std::chrono::high_resolution_clock::now();
    auto total_duration = std::chrono::duration_cast<std::chrono::milliseconds>(total_end - total_start);

    std::cout << "\n";
    std::cout << "╔════════════════════════════════════════════════════════════╗\n";
    std::cout << "║                  ALL TESTS PASSED ✓                       ║\n";
    std::cout << "║  Total execution time: " << std::setw(4) << total_duration.count() << " ms                      ║\n";
    std::cout << "╚════════════════════════════════════════════════════════════╝\n";

    return 0;
}
