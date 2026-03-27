#include <iostream>
#include <iomanip>
#include <cmath>
#include <vector>
#include <cassert>

// ==============================================================================
// instructions fro Windows/WSL (NOTE do not remove these lines!!)
// /mnt/d/Fede/drumlogue/arm-unknown-linux-gnueabihf/bin/arm-unknown-linux-gnueabihf-g++ -static -std=c++17 -O3 -I.. -I. -I../../common -I../common -DRUNTIME_COMMON_H_ test_dsp.cpp -o run_test && qemu-arm ./run_test | tee run_test_result.log
// ==============================================================================


// 1. Mock the Drumlogue OS structures
#include "../common/runtime.h"

uint8_t mock_get_num_sample_banks() { return 1; }
uint8_t mock_get_num_samples_for_bank(uint8_t bank) { (void)bank; return 1; }
const sample_wrapper_t* mock_get_sample(uint8_t bank, uint8_t index) { return nullptr; }

// 2. Define UT flags BEFORE including engine
#define UNIT_TEST_DEBUG
float ut_exciter_out = 0.0f;
float ut_delay_read = 0.0f;
float ut_voice_out = 0.0f;

#include "synth_engine.h"

void run_active_test() {
    std::cout << "--- STARTING ACTIVE DSP UNIT TEST 1 ---\n";

    RipplerXWaveguide synth;
    unit_runtime_desc_t desc = {0};
    desc.samplerate = 48000;
    desc.output_channels = 2;
    synth.Init(&desc);

    synth.LoadPreset(10);

    std::cout << "Triggering NoteOn(60, 127)...\n";
    synth.NoteOn(60, 127);

    uint8_t active_idx = synth.state.next_voice_idx;

    // [UT-ASSERT 1]: Verify Delay Length Generation
    float delay_len = synth.state.voices[active_idx].resA.delay_length;
    if (delay_len <= 12.0f || delay_len > 4096.0f) {
        std::cerr << "\n[FATAL ERROR] Delay length is invalid: " << delay_len
                  << "\nReason: Korg SDK fasterpowf() may be failing on your PC compiler.\n";
        exit(1);
    }

    float frame_buffer[2] = {0.0f};
    bool exciter_fired = false;
    bool signal_emerged = false;

    std::cout << "Simulating 200 frames of audio...\n";

    for (int i = 0; i <= 200; ++i) {
        // SIMULATE DRUMLOGUE SEQUENCER: Short 1ms drum trigger!
        if (i == 48) {
            std::cout << "Frame 48: Sequencer sends GateOff()...\n";
            synth.GateOff();
        }

        synth.processBlock(frame_buffer, 1);

        if (i == 0 && ut_exciter_out > 1.0f) exciter_fired = true;
        if (i > 150 && std::abs(frame_buffer[0]) > 0.0001f) signal_emerged = true;

        // [UT-ASSERT 2]: Check for premature voice death (The Squelch Bug)
        if (i > 48 && i < 180 && !synth.state.voices[active_idx].is_active) {
            std::cerr << "\n[FATAL ERROR] Voice was prematurely killed at frame " << i
                      << " before sound emerged!\nReason: The Squelch logic detected silence while the wave was still travelling silently down the delay line.\n";
            exit(1);
        }

        // [UT-ASSERT 3]: Check for memory corruption
        if (std::isnan(frame_buffer[0]) || std::isinf(frame_buffer[0])) {
            std::cerr << "\n[FATAL ERROR] NaN / Infinity detected at output!\n";
            exit(1);
        }
    }

    if (!exciter_fired) {
        std::cerr << "\n[FATAL ERROR] Exciter never injected energy.\n";
        exit(1);
    }

    if (!signal_emerged) {
        std::cerr << "\n[FATAL ERROR] Sound never emerged from the master output.\n";
        exit(1);
    }

    std::cout << "\n--- 1st DIAGNOSTIC COMPLETE ---\n";
}

void run_active_test2() {
    std::cout << "--- STARTING DIAGNOSTIC DSP UNIT TEST 2 ---\n";

    RipplerXWaveguide synth;
    unit_runtime_desc_t desc = {0};
    desc.samplerate = 48000;
    desc.output_channels = 2;
    synth.Init(&desc);

    synth.LoadPreset(0);
    synth.setParameter(RipplerXWaveguide::k_paramDkay, 1500);
    synth.setParameter(RipplerXWaveguide::k_paramNzMix, 0);

    synth.NoteOn(60, 127);
    uint8_t active_idx = synth.state.next_voice_idx;
    VoiceState& v = synth.state.voices[active_idx];

    // DIAGNOSTIC X-RAY: Run exactly 1 frame of audio
    float frame_buffer[2] = {0.0f};
    synth.processBlock(frame_buffer, 1);

    std::cout << "\n[X-RAY 1: PITCH & TUNING]\n";
    std::cout << "Note 60 Delay Length: " << v.resA.delay_length << " (Should be ~183.47)\n";
    std::cout << "Read Pointer Offset : " << (float)v.resA.write_ptr - v.resA.delay_length << "\n";

    std::cout << "\n[X-RAY 2: EXCITERS & UI BINDING]\n";
    std::cout << "Mallet Stiffness  : " << v.exciter.mallet_stiffness << "\n";
    std::cout << "Mallet Res Coeff  : " << v.exciter.mallet_res_coeff << " (If 0.0, UI matrix is broken!)\n";
    std::cout << "Exciter Output    : " << ut_exciter_out << " (Should be ~15.0)\n";

    std::cout << "\n[X-RAY 3: WAVEGUIDE MEMORY]\n";
    std::cout << "Phase Multiplier  : " << v.resA.phase_mult << " (Should be 1.0 or -1.0)\n";
    std::cout << "Buffer[0] Memory  : " << v.resA.buffer[0] << " (If 0.0, the waveguide multiplied the Mallet by zero!)\n";

    std::cout << "\n--- 2nd DIAGNOSTIC COMPLETE ---\n";
    return 0;
}

static void test_nan_explosion_and_dc_offset() {
    // 1. Setup the test environment
    unit_runtime_desc_t desc = {48000, 2, nullptr, nullptr, nullptr};
    RipplerXWaveguide s;
    s.Init(&desc);

    // We want to test a sharp transient that might trigger SVF or envelope blowups
    s.setParameter(RipplerXWaveguide::k_paramProgram, 3); // Load Ac Snare
    s.GateOn(127); // Hardest possible velocity strike

    const size_t frames = 32;
    float out_buffer[frames * 2];

    bool has_nan = false;
    int consecutive_dc_samples = 0;
    bool has_permanent_dc = false;

    // 2. Render 100 audio blocks (~68 ms, plenty of time for a NaN to propagate)
    for (int block = 0; block < 100; ++block) {
        s.processBlock(out_buffer, frames);

        for (size_t i = 0; i < frames * 2; ++i) {
            float sample = out_buffer[i];

            // Check 1: Did the math explode?
            if (std::isnan(sample) || std::isinf(sample)) {
                has_nan = true;
                break;
            }

            // Check 2: Did the limiter catch a NaN and peg the output to 0.99f permanently?
            // We check if it stays exactly at the limiter threshold for consecutive samples.
            if (fabsf(sample) >= 0.9899f) {
                consecutive_dc_samples++;
                if (consecutive_dc_samples > 50) { // 50 samples of pure DC is a brickwalled signal
                    has_permanent_dc = true;
                    break;
                }
            } else {
                consecutive_dc_samples = 0; // Reset if the wave actually oscillates
            }
        }

        if (has_nan || has_permanent_dc) break;
    }

    // 3. Report results using your existing test runner format
    result("T21a No NaNs generated during heavy transient strike",
           !has_nan,
           "Engine produced a NaN/Inf value!");

    result("T21b Output did not flatline into a DC offset (Silence Bug)",
           !has_permanent_dc,
           "Output pegged to limiter max (0.99) permanently. Math explosion masked by limiter.");
}

// Make sure to declare the extern debug variables if not already at the top of your test file
extern float ut_exciter_out;
extern float ut_delay_read;
extern float ut_voice_out;

static void test_denormal_stalls() {
    unit_runtime_desc_t desc = {48000, 2, nullptr, nullptr, nullptr};
    RipplerXWaveguide s;
    s.Init(&desc);

    // Load a preset with a long decay, strike it, and release it
    s.setParameter(RipplerXWaveguide::k_paramProgram, 4); // Tubular Bell has long decay
    s.GateOn(127);

    const size_t frames = 32;
    float out_buffer[frames * 2];

    // Render enough audio to let the note release and decay into microscopic territory
    for (int i = 0; i < 50; ++i) {
        s.processBlock(out_buffer, frames);
    }
    s.GateOff();

    bool hit_subnormal = false;

    // Render 20,000 more samples (the tail end of the release)
    for (int block = 0; block < 625; ++block) {
        s.processBlock(out_buffer, frames);

        // If the FPU drops into subnormal processing, ARM Cortex CPUs without FTZ
        // enabled will experience massive pipeline stalls, destroying your 20us budget.
        if (std::fpclassify(ut_voice_out) == FP_SUBNORMAL) {
            hit_subnormal = true;
            break;
        }
    }

    result("T22 Denormal/Subnormal Stall Prevention",
           !hit_subnormal,
           "ut_voice_out decayed into FP_SUBNORMAL range! Add a +1e-15f DC offset or FTZ flag.");
}

static void test_stereo_phase_alignment() {
    unit_runtime_desc_t desc = {48000, 2, nullptr, nullptr, nullptr};
    RipplerXWaveguide s;
    s.Init(&desc);

    s.setParameter(RipplerXWaveguide::k_paramProgram, 3); // Ac Snare (wide frequency spread)
    s.GateOn(127);

    const size_t frames = 32;
    float out_buffer[frames * 2];

    bool phase_mismatch = false;

    // Render a few blocks to get the SVF and overdrive heavily saturated
    for (int block = 0; block < 10; ++block) {
        s.processBlock(out_buffer, frames);

        for (size_t i = 0; i < frames; ++i) {
            float left_channel = out_buffer[i * 2];
            float right_channel = out_buffer[i * 2 + 1];

            // Because your current master_filter is mono, both channels must be
            // bit-for-bit identical. If they deviate, you have a phase cancellation bug.
            if (left_channel != right_channel) {
                phase_mismatch = true;
                break;
            }
        }
        if (phase_mismatch) break;
    }

    result("T23 Stereo Phase Alignment",
           !phase_mismatch,
           "Left and Right channels deviated! Check the master FX routing loop.");
}

static void test_delay_memory_leak() {
    unit_runtime_desc_t desc = {48000, 2, nullptr, nullptr, nullptr};
    RipplerXWaveguide s;
    s.Init(&desc);

    // 1. Blast the delay lines with a loud, sustained note
    s.setParameter(RipplerXWaveguide::k_paramProgram, 2); // 808 Sub
    s.GateOn(127);

    const size_t frames = 32;
    float out_buffer[frames * 2];

    for (int block = 0; block < 100; ++block) {
        s.processBlock(out_buffer, frames);
    }

    // 2. Hard reset the engine (simulating a sudden patch change or OS interrupt)
    s.Reset();

    // 3. Render a new block of audio WITHOUT triggering a new note
    s.processBlock(out_buffer, frames);

    bool memory_leak_detected = false;

    // The delay read tap must be exactly 0.0f. If it isn't, stale audio from
    // the previous note survived the Reset() and will cause a pop on the next NoteOn.
    if (fabsf(ut_delay_read) > 0.0f) {
        memory_leak_detected = true;
    }

    result("T24 Delay Line Memory Leak on Reset",
           !memory_leak_detected,
           "ut_delay_read returned non-zero audio immediately after Reset()!");
}

int main() {
    run_active_test();
    run_active_test2();
    test_nan_explosion_and_dc_offset();
    test_denormal_stalls();
    test_stereo_phase_alignment();
    test_delay_memory_leak();
    return 0;
}