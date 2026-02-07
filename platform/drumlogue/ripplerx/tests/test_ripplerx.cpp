#include <iostream>
#include <vector>
#include <cmath>
#include <cstring>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cmath>
#include <cstddef>

// new unit test for proper debugging.
// unis this command for WSL build and run
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
int main()
 { std::cout << ">>> STARTING RIPPLERX DIAGNOSTIC TEST <<<" << std::endl;

// 1. Setup Environment
RipplerX synth;
 unit_runtime_desc_t desc;
 desc.samplerate = 48000;
 desc.output_channels = 2;
 desc.get_num_sample_banks = mock_get_num_sample_banks;
 desc.get_num_samples_for_bank = mock_get_num_samples_for_bank;
 desc.get_sample = mock_get_sample;

// 2. Initialize
std::cout << "[Test] Initializing Synth..." << std::endl;
 int8_t err = synth.Init(&desc);
 if (err != k_unit_err_none_mock) { std::cerr << "[FAIL] Init failed with error code: " << (int)err << std::endl;
 return 1;
 }

 // Force parameters to ensure signal flow
 synth.setParameter(c_parameterMalletResonance, 10); // 1.0 (Max resonance)
 synth.setParameter(c_parameterDecay, 500);          // 50.0 (Long decay)
 synth.setParameter(c_parameterModel, 0);            // String Model
 synth.setParameter(c_parameterNoiseMix, 1000);      // 1.0 (Max Mix)
 synth.setParameter(c_parameterNoiseFilterFreq, 10000); // Open the filter

// 3. Trigger a Note (High Velocity to maximize energy)
std::cout << "[Test] Triggering NoteOn (60, Vel 127)..." << std::endl;
 synth.NoteOn(60, 127);

// 4. Render Loop (Simulate ~1 second of audio)
const size_t kBlockSize = 64;

// Standard block
const size_t c_kNumBlocks = (48000 / kBlockSize);

// Aligned buffer for NEON
alignas(16) float outputBuffer[kBlockSize * 2];
 bool hasSignal = false;
 bool hasExploded = false;
 float maxAmplitude = 0.0f;
 for (size_t b = 0;
 b < c_kNumBlocks;
 ++b) {
// Clear buffer (simulating SDK behavior which might accumulate)
std::memset(outputBuffer, 0, sizeof(outputBuffer));

// Render
synth.Render(outputBuffer, kBlockSize);

// Analyze Output
for (size_t i = 0;
 i < kBlockSize * 2;
 ++i) { float val = outputBuffer[i];

// Check for NaN/Inf (The Crash Cause)
if (!std::isfinite(val)) { std::cerr << "[FAIL] NaN/Inf detected at Block " << b << " Sample " << i << std::endl;
 hasExploded = true;
 break;
 }
// Check for Magnitude Explosion (Filter instability)
if (std::abs(val) > 10.0f) {
// Audio shouldn't exceed +/- 1.0f significantly
std::cerr << "[FAIL] Audio Explosion detected (> 10.0f) at Block " << b << " Value: " << val << std::endl;
 hasExploded = true;
 break;
 }
// Check for Signal presence
if (std::abs(val) > 0.001f) {
    if (!hasSignal) std::cout << "[Test] Signal detected at Block " << b << " Sample " << i << " Val: " << val << std::endl;
    hasSignal = true;
 } if (std::abs(val) > maxAmplitude) maxAmplitude = std::abs(val);
 } if (hasExploded) break;
 }
// 5. Final Report
std::cout << "--- REPORT ---" << std::endl;
 std::cout << "Max Amplitude: " << maxAmplitude << std::endl;
 if (hasExploded) { std::cout << "RESULT: FAILED (Numeric Instability Detected)" << std::endl;
 std::cout << "Diagnosis: This confirms the Resonator/Filter feedback loop is broken." << std::endl;
 std::cout << "Action: Apply the 'Serial Loop' fix to Partial::process and Waveguide::process." << std::endl;
 return 1;
 } else if (!hasSignal) { std::cout << "RESULT: FAILED (No Audio Output)" << std::endl;
 return 1;
 } else { std::cout << "RESULT: PASS (Stable Output Generated)" << std::endl;
 return 0;
 } }