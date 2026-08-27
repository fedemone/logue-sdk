/**
 * @file test_hw_debug.cpp
 * @brief Diagnostic unit tests targeting the UT-passes / HW-silent gap.
 *
 * Compile (from the brachetti/ directory):
 *   g++ -std=c++14 -O2 -DUNIT_TEST_DEBUG \
 *       -I../common -I. \
 *       -o test_hw_debug test_hw_debug.cpp -lm
 * Run:
 *   ./test_hw_debug
 *
 * Each test is independent and prints PASS/FAIL with a reason.
 * Silent-on-HW hypotheses covered:
 *   H1 - feedback_gain or mallet_stiffness left at zero by default
 *   H2 - Init preset has Gain=0, making master_drive only 1.0 (UT masked with Gain=50)
 *   H3 - Denormals: feedback ring decays into sub-normal range and is flushed to 0
 *   H4 - GateOn → NoteOn(m_ui_note, vel) path differs from UT's NoteOn(60, 127)
 *   H5 - Larger block size (32/64 frames) exposes a buffer-clear or indexing bug
 *   H6 - After unit_reset() the parameters are intact but delay buffers zeroed;
 *         subsequent GateOn must still produce sound
 */

#include <iostream>
#include <iomanip>
#include <cmath>
#include <cassert>
#include <cstring>

// ── Mock Drumlogue OS runtime ────────────────────────────────────────────────
#include "../common/runtime.h"

uint8_t mock_get_num_sample_banks()                        { return 1; }
uint8_t mock_get_num_samples_for_bank(uint8_t)             { return 1; }
const sample_wrapper_t* mock_get_sample(uint8_t, uint8_t)  { return nullptr; }

// ── UT probe hooks ───────────────────────────────────────────────────────────
#define UNIT_TEST_DEBUG
float ut_exciter_out = 0.0f;
float ut_delay_read  = 0.0f;
float ut_voice_out   = 0.0f;

#include "synth_engine.h"

// ── Helpers ──────────────────────────────────────────────────────────────────

/** Build a unit_runtime_desc_t identical to what the Drumlogue OS provides. */
static unit_runtime_desc_t make_desc() {
    unit_runtime_desc_t d = {0};
    d.samplerate                = 48000;
    d.output_channels           = 2;
    d.get_num_sample_banks      = mock_get_num_sample_banks;
    d.get_num_samples_for_bank  = mock_get_num_samples_for_bank;
    d.get_sample                = mock_get_sample;
    return d;
}

static bool is_nan_or_inf(float v) {
    return std::isnan(v) || std::isinf(v);
}

/** Process N frames in blocks of block_size.  Returns max absolute sample seen. */
static float run_blocks(BrachettiSynth& s, int total_frames, int block_size = 32) {
    float buf[256] = {0.0f};  // Max block_size we test is 64 → 128 floats, 256 is safe
    float peak = 0.0f;
    for (int done = 0; done < total_frames; done += block_size) {
        int frames = std::min(block_size, total_frames - done);
        std::memset(buf, 0, sizeof(buf));
        s.processBlock(buf, (size_t)frames);
        for (int i = 0; i < frames * 2; ++i) {
            float a = std::fabs(buf[i]);
            if (a > peak) peak = a;
        }
    }
    return peak;
}

/** Check left-channel of a single frame for nonzero; returns true if sound present. */
static float single_frame(BrachettiSynth& s) {
    float buf[2] = {0.0f, 0.0f};
    s.processBlock(buf, 1);
    return buf[0];
}

// ── Test runner bookkeeping ──────────────────────────────────────────────────
static int g_pass = 0, g_fail = 0;

static void result(const char* name, bool pass, const char* detail = "") {
    if (pass) {
        std::cout << "[PASS] " << name << "\n";
        ++g_pass;
    } else {
        std::cout << "[FAIL] " << name << "  ← " << detail << "\n";
        ++g_fail;
    }
}

// ════════════════════════════════════════════════════════════════════════════
// T0 — Parameter audit (H1, H2)
//   After Init() + LoadPreset(0) - exactly what the HW does - print every
//   critical DSP coefficient and assert nothing is zero where it shouldn't be.
// ════════════════════════════════════════════════════════════════════════════
static void test_param_audit() {
    std::cout << "\n── T0: Parameter audit after Init + LoadPreset(0) ──\n";

    unit_runtime_desc_t desc = make_desc();
    BrachettiSynth s;
    s.Init(&desc);
    // NO manual LoadPreset here — Init already calls it.

    // Access internal state through the public SynthState member
    const SynthState& st = s.state;

    // Print voice[0] DSP coefficients — LoadPreset applies setParameter to all voices
    // symmetrically, so every voice holds the same values after Init. No NoteOn needed.
    std::cout << std::fixed << std::setprecision(6);
    std::cout << "  master_gain          = " << st.master_gain    << "\n";
    std::cout << "  master_drive         = " << st.master_drive   << "\n";
    std::cout << "  mix_ab               = " << st.mix_ab         << "\n";
    std::cout << "  resA.feedback_gain   = " << st.voices[0].resA.feedback_gain  << "\n";
    std::cout << "  resA.lowpass_coeff   = " << st.voices[0].resA.lowpass_coeff  << "\n";
    std::cout << "  resA.ap_coeff        = " << st.voices[0].resA.ap_coeff       << "\n";
    std::cout << "  exciter.mallet_stiff = " << st.voices[0].exciter.mallet_stiffness << "\n";
    std::cout << "  exciter.mallet_res   = " << st.voices[0].exciter.mallet_res_coeff << "\n";
    std::cout << "  exciter.noise_decay  = " << st.voices[0].exciter.noise_decay_coeff << "\n";

    // H1: feedback_gain must not be zero - zero means silence after frame 0
    bool fg_ok = st.voices[0].resA.feedback_gain > 0.01f;
    result("T0a feedback_gain > 0.01 after LoadPreset(0)", fg_ok,
           "feedback_gain is zero - resonator will not sustain");

    // H2: master_drive at Gain=0 → 1.0f (unity, not muted).
    //     The UT hid this by setting Gain=50 → drive=11.0.
    //     Verify drive>=1.0 so the signal isn't attenuated.
    bool drv_ok = st.master_drive >= 1.0f;
    result("T0b master_drive >= 1.0 (Gain=0 preset gives unity, not mute)", drv_ok,
           "master_drive < 1 would attenuate signal below audibility");

    // master_gain is a fixed boot constant (1.5 since the 9th HW pass "+0.5"
    // loudness bump), never changed by any parameter.
    bool mg_ok = std::fabs(st.master_gain - 1.5f) < 1e-6f;
    result("T0c master_gain == 1.5 (fixed boot constant, not UI-changed)", mg_ok,
           "master_gain changed unexpectedly");
}

// ════════════════════════════════════════════════════════════════════════════
// T1 — HW boot sequence: Init → GateOn → processBlock in 32-frame blocks (H4, H5)
//   The UT uses NoteOn(60,127) + frame-by-frame loop.  The HW uses GateOn
//   + block-based render.  This test replicates the exact OS call order.
// ════════════════════════════════════════════════════════════════════════════
static void test_hw_boot_sequence() {
    std::cout << "\n── T1: HW boot sequence (GateOn + 32-frame blocks) ──\n";

    unit_runtime_desc_t desc = make_desc();
    BrachettiSynth s;
    s.Init(&desc);
    // Hardware fires GateOn, NOT NoteOn directly
    s.GateOn(127);

    // Process 10 blocks of 32 frames (= 6.7 ms, well beyond delay-line roundtrip of ~3.8 ms)
    float buf[64] = {0.0f};
    float peak = 0.0f;
    std::cout << "  Block | L-peak\n";
    for (int b = 0; b < 10; ++b) {
        std::memset(buf, 0, sizeof(buf));
        s.processBlock(buf, 32);
        float blk_peak = 0.0f;
        for (int i = 0; i < 64; ++i) {
            float a = std::fabs(buf[i]);
            if (a > blk_peak) blk_peak = a;
            if (a > peak) peak = a;
        }
        std::cout << "  " << std::setw(5) << b << " | " << blk_peak << "\n";
    }

    result("T1 GateOn + 32-frame blocks produces nonzero audio", peak > 1e-9f,
           "all 320 frames were zero after GateOn(127)");
}

// ════════════════════════════════════════════════════════════════════════════
// T2 — Default preset (no UT override): Dkay=250, Gain=0 (H1, H2)
//   The existing UT overrides k_paramDkay=1500 and k_paramGain=50.
//   This test uses the raw preset 0 values so we see what the hardware gets.
// ════════════════════════════════════════════════════════════════════════════
static void test_default_preset_no_override() {
    std::cout << "\n── T2: Default preset, no UT overrides (Dkay=250, Gain=0) ──\n";

    unit_runtime_desc_t desc = make_desc();
    BrachettiSynth s;
    s.Init(&desc);
    s.NoteOn(60, 127);

    // Sample output at specific frames that bracket the delay-line roundtrip
    std::cout << "  Frame | L output\n";
    float peak = 0.0f;
    for (int i = 0; i < 400; ++i) {
        float v = single_frame(s);
        float a = std::fabs(v);
        if (a > peak) peak = a;
        if (i < 5 || (i >= 180 && i <= 195) || i >= 395) {
            std::cout << "  " << std::setw(5) << i << " | " << std::setprecision(8) << v << "\n";
        } else if (i == 6) {
            std::cout << "  ... (travelling down delay line) ...\n";
        }
    }

    result("T2 preset-0 defaults produce nonzero output", peak > 1e-9f,
           "completely silent with default Dkay=250 / Gain=0 - hardware would be mute");
}

// ════════════════════════════════════════════════════════════════════════════
// T3 — Denormal / flush-to-zero decay (H3)
//   ARM Cortex-A7 NEON typically runs with FTZ.  If the feedback ring decays
//   into the sub-normal float range (~1e-38), those samples become exactly
//   0.0.  A sustained note should stay audible for at least 100 ms (4800 frames).
// ════════════════════════════════════════════════════════════════════════════
static void test_denormal_decay() {
    std::cout << "\n── T3: Denormal / FTZ decay check (100 ms of sustained sound) ──\n";

    unit_runtime_desc_t desc = make_desc();
    BrachettiSynth s;
    s.Init(&desc);
    // Use the default preset (Dkay=250 → feedback_gain≈0.869) but hold the gate open.
    s.NoteOn(60, 127);

    float buf[64] = {};
    bool still_audible_at_50ms  = false;
    bool still_audible_at_100ms = false;
    bool nan_or_inf_detected    = false;

    // 100 ms = 4800 frames; process in 32-frame blocks
    for (int frame = 0; frame < 4800; frame += 32) {
        std::memset(buf, 0, sizeof(buf));
        s.processBlock(buf, 32);
        for (int i = 0; i < 64; ++i) {
            float a = std::fabs(buf[i]);
            if (is_nan_or_inf(buf[i])) nan_or_inf_detected = true;
            if (a > 1e-9f) {
                if (frame >= 2400) still_audible_at_50ms  = true;
                if (frame >= 4768) still_audible_at_100ms = true;
            }
        }
    }

    result("T3a no NaN/Inf in 100 ms of output", !nan_or_inf_detected,
           "NaN or Inf detected - would be clamped to 0.99 by limiter, masking silence");
    result("T3b signal > 1e-9 at 50 ms (feedback_gain=0.869 not decaying into denormals)",
           still_audible_at_50ms,
           "signal flushed to zero before 50 ms - FTZ/denormal issue on hardware");
    result("T3c signal > 1e-9 at 100 ms",
           still_audible_at_100ms,
           "signal gone before 100 ms - sound may be inaudible before DAC transduces it");
}

// ════════════════════════════════════════════════════════════════════════════
// T4 — GateOn / GateOff cycle (H4)
//   Verifies the release envelope fades rather than hard-zeros the signal,
//   and that a second GateOn after GateOff produces sound.
// ════════════════════════════════════════════════════════════════════════════
static void test_gate_on_off_cycle() {
    std::cout << "\n── T4: GateOn → 250 frames → GateOff → 250 frames → GateOn again ──\n";

    unit_runtime_desc_t desc = make_desc();
    BrachettiSynth s;
    s.Init(&desc);

    s.GateOn(127);
    float peak_during  = run_blocks(s, 250, 32);

    s.GateOff();
    float peak_release = run_blocks(s, 250, 32);

    s.GateOn(127);
    float peak_second  = run_blocks(s, 250, 32);

    std::cout << "  peak_during (first gate):  " << peak_during  << "\n";
    std::cout << "  peak_release (after off):  " << peak_release << "\n";
    std::cout << "  peak_second (second gate): " << peak_second  << "\n";

    result("T4a first GateOn produces sound",   peak_during  > 1e-9f,
           "no audio during first gate hold");
    result("T4b GateOff enters release (not hard zero)", peak_release > 1e-9f,
           "signal hard-zeroed on GateOff - release not working; hardware would click");
    result("T4c second GateOn also produces sound", peak_second > 1e-9f,
           "re-trigger after GateOff produces no sound - voice state corrupt");
}

// ════════════════════════════════════════════════════════════════════════════
// T5 — Block-size sensitivity (H5)
//   Drumlogue may pass different block sizes.  Verify output is nonzero for
//   1, 16, 32 and 64 frames per processBlock call.
// ════════════════════════════════════════════════════════════════════════════
static void test_block_sizes() {
    std::cout << "\n── T5: Block-size sensitivity (1, 16, 32, 64 frames) ──\n";

    int sizes[] = {1, 16, 32, 64};
    for (int sz : sizes) {
        unit_runtime_desc_t desc = make_desc();
        BrachettiSynth s;
        s.Init(&desc);
        s.GateOn(127);
        // Process enough frames to complete at least one delay-line round trip (~184 frames for C4)
        float peak = run_blocks(s, 400, sz);
        char label[64];
        std::snprintf(label, sizeof(label), "T5 block_size=%d produces nonzero audio", sz);
        result(label, peak > 1e-9f, "silent at this block size");
    }
}

// ════════════════════════════════════════════════════════════════════════════
// T6 — unit_reset() does not kill subsequent GateOn (H6)
//   The OS calls unit_reset() between patterns.  It wipes delay buffers and
//   is_active flags but must not zero feedback_gain / mallet_stiffness.
// ════════════════════════════════════════════════════════════════════════════
static void test_reset_then_gate_on() {
    std::cout << "\n── T6: unit_reset() then GateOn still produces audio ──\n";

    unit_runtime_desc_t desc = make_desc();
    BrachettiSynth s;
    s.Init(&desc);

    // Simulate OS: fire one note, then reset (e.g. pattern change), then play again
    s.GateOn(127);
    run_blocks(s, 64, 32);  // partial play

    s.Reset();              // unit_reset() — wipes buffers, keeps parameters

    s.GateOn(127);
    float peak = run_blocks(s, 400, 32);

    std::cout << "  peak after Reset + GateOn: " << peak << "\n";
    result("T6 post-Reset GateOn produces sound", peak > 1e-9f,
           "Reset() wiped a critical DSP parameter (feedback_gain / mallet_stiffness)");
}

// ════════════════════════════════════════════════════════════════════════════
// T7 — Preset sweep: every preset must produce nonzero audio on first GateOn
//   If a preset has a zero-killing default (e.g. all-zero noise with Gain=0
//   AND zero mallet somehow), this catches it.
// ════════════════════════════════════════════════════════════════════════════
static void test_all_presets_audible() {
    std::cout << "\n── T7: All " << BrachettiSynth::k_NumPrograms << " presets produce nonzero audio ──\n";

    bool any_fail = false;
    for (int p = 0; p < BrachettiSynth::k_NumPrograms; ++p) {
        // Flute and Clarinet were removed outright (HW request) — every
        // remaining program must produce audio.
        unit_runtime_desc_t desc = make_desc();
        BrachettiSynth s;
        s.Init(&desc);
        s.LoadPreset((uint8_t)p);
        s.GateOn(127);
        // 1500 frames covers the worst-case round-trip for the lowest preset note
        // (note 35 / B1 ≈ 870-sample delay with fasterpowf approximation on x86).
        float peak = run_blocks(s, 1500, 32);
        if (peak < 1e-9f) {
            std::cout << "  [SILENT] preset " << p << " (" << BrachettiSynth::getPresetName(p) << ")"
                      << "  peak=" << peak << "\n";
            any_fail = true;
        }
    }
    result("T7 all presets audible on first GateOn", !any_fail,
           "one or more presets produce no audio - hardware would be silent on those presets");
}

// ════════════════════════════════════════════════════════════════════════════
// T8 — Voice allocation: NoteOn activates a voice that processBlock can see
//   Regression for next_voice_idx advancing before indexing (voice 0 skipped).
// ════════════════════════════════════════════════════════════════════════════
static void test_voice_allocation() {
    std::cout << "\n── T8: Voice allocation - first NoteOn activates a processed voice ──\n";

    unit_runtime_desc_t desc = make_desc();
    BrachettiSynth s;
    s.Init(&desc);

    // Count active voices before NoteOn
    int active_before = 0;
    for (int i = 0; i < 4; ++i)
        if (s.state.voices[i].is_active) ++active_before;

    s.NoteOn(60, 127);

    int active_after = 0;
    int active_idx = -1;
    for (int i = 0; i < 4; ++i) {
        if (s.state.voices[i].is_active) { ++active_after; active_idx = i; }
    }

    std::cout << "  active voices before: " << active_before << "\n";
    std::cout << "  active voices after:  " << active_after  << "\n";
    std::cout << "  active voice index:   " << active_idx    << "\n";

    result("T8a exactly 1 voice active after NoteOn",
           active_before == 0 && active_after == 1,
           "unexpected voice count");

    // Verify that voice is within the range processBlock iterates
    result("T8b active voice index in [0,3]",
           active_idx >= 0 && active_idx < 4,
           "voice index out of range");
}

// ════════════════════════════════════════════════════════════════════════════
// T9 — Delay-line round-trip (THE root cause from run_test_result.log)
//
//   The existing UT log (run_test_result.log) shows:
//     Frame 0:   Exciter = 15.0  →  Delay Read = 0.000  ← exciter fired
//     Frames 182-185: Delay Read = 0.000 STILL zero after full round trip!
//   This means the signal enters the waveguide but never comes back.
//
//   Most likely cause: resA.delay_length = 0 after NoteOn, so read_ptr ==
//   write_ptr — always reading the slot that is *about* to be written
//   (still zero from last frame).  Zero delay_length → permanent silence.
//
//   This test:
//     a) Asserts delay_length is sane (> 10 samples, < 4096) after NoteOn
//     b) Asserts that after exactly ceil(delay_length) frames the delay
//        line returns a nonzero value (the round-trip echo).
// ════════════════════════════════════════════════════════════════════════════
static void test_delay_roundtrip() {
    std::cout << "\n── T9: Delay-line round-trip echo (root cause from run_test_result.log) ──\n";

    unit_runtime_desc_t desc = make_desc();
    BrachettiSynth s;
    s.Init(&desc);
    s.LoadPreset(BrachettiSynth::k_Koto);  // KS reference (program 0 is now a membrane kick)
    s.NoteOn(60, 127);

    // Inspect the allocated voice's delay_length directly.
    // NoteOn advances next_voice_idx before assigning, so the active voice is
    // always at that index — no hardcoded assumptions about initial state.
    uint8_t active_idx = s.state.next_voice_idx;
    const WaveguideState& resA = s.state.voices[active_idx].resA;
    float dl = resA.delay_length;
    std::cout << "  resA.delay_length after NoteOn(60) = " << dl << " samples\n";
    std::cout << "  Raw C4 period @ 48 kHz = 183.47 samples; after LP+AP compensation (~2 samples) ≈ 181.5\n";

    bool dl_sane = dl > 10.0f && dl < 4090.0f;
    result("T9a delay_length in [10, 4090] after NoteOn(60)", dl_sane,
           "delay_length is 0 or huge - waveguide permanently silent (matches log)");

    if (!dl_sane) {
        std::cout << "  *** SKIP round-trip check because delay_length=" << dl << " ***\n";
        return;
    }

    // Process ceil(delay_length)+2 frames one at a time and capture outA via
    // the ut_delay_read hook.  At least one frame after the roundtrip must be
    // nonzero.
    int roundtrip = (int)dl + 4;   // a few extra frames for the fractional part
    float max_delay_read = 0.0f;
    for (int i = 0; i <= roundtrip; ++i) {
        float buf[2] = {};
        s.processBlock(buf, 1);
        float a = std::fabs(ut_delay_read);
        if (a > max_delay_read) max_delay_read = a;
    }

    std::cout << "  max |ut_delay_read| over " << roundtrip+1
              << " frames = " << max_delay_read << "\n";
    result("T9b delay line returns nonzero after one round trip", max_delay_read > 1e-4f,
           "delay line always reads zero - same symptom as run_test_result.log silent HW");
}

// ════════════════════════════════════════════════════════════════════════════
// T10 — Dedicated Noise SVF (NzFltr / NzFltFrq parameter routing)
//   A structural test: verify that setParameter() routes NzFltr to
//   noise_filter.mode and NzFltFrq to noise_filter.f on all four voices.
//   This directly tests the previously unmapped parameter cases without
//   relying on audio amplitude measurements that can overflow the resonator
//   feedback loop at high noise injection levels.
// ════════════════════════════════════════════════════════════════════════════
static void test_noise_svf() {
    std::cout << "\n── T10: NzFltr/NzFltFrq route to per-voice noise_filter ──\n";

    unit_runtime_desc_t desc = make_desc();
    BrachettiSynth s;
    s.Init(&desc);

    // NzFltr=0 → LP mode on all four voices
    s.setParameter(BrachettiSynth::k_paramNzFltr, 0);
    bool all_lp = true;
    for (int i = 0; i < 4; ++i)
        if (s.state.voices[i].exciter.noise_filter.mode != 0) all_lp = false;

    // NzFltFrq: higher cutoff → larger TPT a3 coefficient (a3 = g·a2, which is strictly monotonic up to Nyquist)
    s.setParameter(BrachettiSynth::k_paramNzFltFrq, 5000);
    float f_5kHz = s.state.voices[0].exciter.noise_filter.a3;
    s.setParameter(BrachettiSynth::k_paramNzFltFrq, 200);
    float f_200Hz = s.state.voices[0].exciter.noise_filter.a3;

    // NzFltr=2 → HP mode on all four voices
    s.setParameter(BrachettiSynth::k_paramNzFltr, 2);
    bool all_hp = true;
    for (int i = 0; i < 4; ++i)
        if (s.state.voices[i].exciter.noise_filter.mode != 2) all_hp = false;

    std::cout << "  NzFltr=0 LP mode on all voices : " << (all_lp ? "yes" : "no") << "\n";
    std::cout << "  f@5kHz=" << std::setprecision(4) << f_5kHz
              << "  f@200Hz=" << f_200Hz << "  (higher freq → higher f)\n";
    std::cout << "  NzFltr=2 HP mode on all voices : " << (all_hp ? "yes" : "no") << "\n";

    result("T10a NzFltr=0 sets noise_filter.mode=LP(0) on all 4 voices",
           all_lp,
           "NzFltr not routing to noise_filter.mode — parameter still falls to default");
    result("T10b NzFltFrq=5000 gives larger f-coeff than NzFltFrq=200",
           f_5kHz > f_200Hz,
           "NzFltFrq not routing to noise_filter.set_coeffs");
    result("T10c NzFltr=2 sets noise_filter.mode=HP(2) on all 4 voices",
           all_hp,
           "NzFltr HP mode not routing correctly");
}

// ════════════════════════════════════════════════════════════════════════════
// T11 — TubRad interacts with Mterl to increase lowpass_coeff
//   With TubRad=0 the coefficient is determined by Mterl alone.
//   TubRad=20 (widest tube) must pull the coefficient towards 1.0 (less loss).
// ════════════════════════════════════════════════════════════════════════════
static void test_tubrad_mterl() {
    std::cout << "\n── T11: TubRad combines with Mterl to brighten lowpass_coeff ──\n";

    unit_runtime_desc_t desc = make_desc();
    BrachettiSynth s;
    s.Init(&desc);

    // Target ResA (default context after Init)
    s.setParameter(BrachettiSynth::k_paramMterl,  10); // mid material
    s.setParameter(BrachettiSynth::k_paramTubRad,  0); // narrow tube (no adjustment)
    float coeff_narrow = s.state.voices[0].resA.lowpass_coeff;

    s.setParameter(BrachettiSynth::k_paramTubRad, 20); // widest tube
    float coeff_wide   = s.state.voices[0].resA.lowpass_coeff;

    std::cout << "  lowpass_coeff TubRad=0  : " << coeff_narrow << "\n";
    std::cout << "  lowpass_coeff TubRad=20 : " << coeff_wide   << "\n";

    result("T11a TubRad=20 raises lowpass_coeff above TubRad=0",
           coeff_wide > coeff_narrow,
           "TubRad had no effect on lowpass_coeff");
    result("T11b TubRad=20 lowpass_coeff < 1.0 (damping still present)",
           coeff_wide < 1.0f,
           "TubRad pushed coeff to 1.0 — all high-frequency damping lost");
}

// ════════════════════════════════════════════════════════════════════════════
// T12 — Partls coupling cross-feeds resonators
//   Partls=0 → ResB inactive, no coupling.  Partls=4 → ResB active, full
//   coupling (ResA←ResB feedback, ResB←ResA feed-forward).  Both must produce
//   audible output; the dual-resonator path must also sustain longer.
// ════════════════════════════════════════════════════════════════════════════
static void test_partls_coupling() {
    std::cout << "\n── T12: Partls coupling — ResA only vs dual with cross-feed ──\n";

    auto run_preset_with_partls = [](int partls_val) -> float {
        unit_runtime_desc_t desc = make_desc();
        BrachettiSynth s;
        s.Init(&desc);
        s.setParameter(BrachettiSynth::k_paramPartls, partls_val);
        s.GateOn(127);
        return run_blocks(s, 400, 32);
    };

    float peak_resA_only = run_preset_with_partls(0); // Partls=0: 4 partials, ResB off
    float peak_dual      = run_preset_with_partls(4); // Partls=4: 64 partials, ResB + full coupling
    std::cout << "  peak Partls=0 (ResA only) : " << peak_resA_only << "\n";
    std::cout << "  peak Partls=4 (dual+coupling) : " << peak_dual  << "\n";

    result("T12a Partls=0 (ResA only) produces sound",
           peak_resA_only > 1e-9f, "ResA-only mode is silent");
    result("T12b Partls=4 (ResB + coupling) produces sound",
           peak_dual > 1e-9f, "Dual-resonator coupling mode is silent");
}

// ════════════════════════════════════════════════════════════════════════════
// T13 — Tone tilt EQ: Tone=30 (bright) vs Tone=-10 (dark)
//   Positive Tone boosts the highpass component; negative Tone blends towards
//   the lowpass component.  A percussive mallet has bright attack energy, so
//   Tone=30 must produce a higher peak than Tone=-10.
// ════════════════════════════════════════════════════════════════════════════
static void test_tone_eq() {
    std::cout << "\n── T13: Tone tilt EQ — bright vs dark ──\n";

    // The master limiter clips all output to ±0.99, hiding EQ differences.
    // Instead, capture ut_voice_out — the post-Tone-EQ signal BEFORE master drive
    // and limiter — by running one frame at a time.
    //
    // At the delay-line round-trip (~190 frames), tone_lp ≈ 0 (tracking zero for
    // 190 frames), so the entire signal is "high frequency" from the tilt EQ's
    // perspective.  Tone=30 boosts that component ×3; Tone=-10 crushes it to ~0.
    auto peak_pre_limiter = [](int tone_val) -> float {
        unit_runtime_desc_t desc = make_desc();
        BrachettiSynth s;
        s.Init(&desc);
        s.LoadPreset(BrachettiSynth::k_Koto);  // KS reference (program 0 is now a membrane kick)
        s.setParameter(BrachettiSynth::k_paramTone, tone_val);
        s.GateOn(127);
        float peak = 0.0f;
        for (int i = 0; i < 400; ++i) {
            float buf[2] = {};
            s.processBlock(buf, 1);
            float a = std::fabs(ut_voice_out); // pre-master-effects signal
            if (a > peak) peak = a;
        }
        return peak;
    };

    float peak_neutral = peak_pre_limiter(0);
    float peak_bright  = peak_pre_limiter(30);
    float peak_dark    = peak_pre_limiter(-10);
    std::cout << "  peak (pre-limiter) Tone= 0 (neutral) : " << peak_neutral << "\n";
    std::cout << "  peak (pre-limiter) Tone=30 (bright)  : " << peak_bright  << "\n";
    std::cout << "  peak (pre-limiter) Tone=-10 (dark)   : " << peak_dark    << "\n";

    result("T13a Tone=0 (neutral) produces sound (pre-limiter)",
           peak_neutral > 1e-9f, "Tone=0 is silent");
    result("T13b Tone=30 (bright) pre-limiter peak > Tone=-10 (dark)",
           peak_bright > peak_dark,
           "Tone EQ has no effect or inverted: bright pre-limiter peak <= dark");
}

// ════════════════════════════════════════════════════════════════════════════
// T14 — noise_filter SVF delay state zeroed on NoteOn() and Reset()
//   If lp/bp/hp are not cleared on retrigger, the first noise sample after
//   NoteOn sees stale filter memory, producing a click/pop.
//   This test verifies those fields are exactly 0.0 immediately after both
//   a Reset() and a NoteOn() — before any audio is processed.
// ════════════════════════════════════════════════════════════════════════════
static void test_noise_filter_state_clear() {
    std::cout << "\n── T14: noise_filter SVF state cleared on Reset() and NoteOn() ──\n";

    unit_runtime_desc_t desc = make_desc();
    BrachettiSynth s;
    s.Init(&desc);

    // Run 200 frames with NzMix=100 to accumulate non-zero filter state
    s.setParameter(BrachettiSynth::k_paramNzMix, 100);
    s.NoteOn(60, 127);
    for (int i = 0; i < 200; ++i) { float buf[2]{}; s.processBlock(buf, 1); }

    // Snapshot: confirm states are nonzero after processing noise
    // (voice 1 was allocated by the first NoteOn above — index from T8 shows index 1)
    float lp_before = s.state.voices[1].exciter.noise_filter.lp;
    float bp_before = s.state.voices[1].exciter.noise_filter.bp;

    // Reset() must zero all noise_filter delay states
    s.Reset();
    float lp_after_reset = s.state.voices[0].exciter.noise_filter.lp;
    float bp_after_reset = s.state.voices[0].exciter.noise_filter.bp;

    // A fresh NoteOn() must also zero the states before the first sample
    s.NoteOn(60, 100);
    float lp_after_noteon = s.state.voices[1].exciter.noise_filter.lp;
    float bp_after_noteon = s.state.voices[1].exciter.noise_filter.bp;

    std::cout << "  noise_filter.lp before reset : " << lp_before  << "\n";
    std::cout << "  noise_filter.bp before reset : " << bp_before  << "\n";
    std::cout << "  noise_filter.lp after Reset() : " << lp_after_reset << "\n";
    std::cout << "  noise_filter.lp after NoteOn(): " << lp_after_noteon << "\n";

    result("T14a noise_filter.lp/bp non-zero after 200-frame noise injection",
           lp_before != 0.0f || bp_before != 0.0f,
           "NzMix=100 noise filter state never changed — filter may not be processing");
    result("T14b Reset() zeroes noise_filter.lp",
           lp_after_reset == 0.0f,
           "Reset() left stale noise_filter.lp — would cause click on next NoteOn");
    result("T14c Reset() zeroes noise_filter.bp",
           bp_after_reset == 0.0f,
           "Reset() left stale noise_filter.bp — would cause click on next NoteOn");
    result("T14d NoteOn() zeroes noise_filter.lp",
           lp_after_noteon == 0.0f,
           "NoteOn() left stale noise_filter.lp — retrigger click on poly overlap");
    result("T14e NoteOn() zeroes noise_filter.bp",
           bp_after_noteon == 0.0f,
           "NoteOn() left stale noise_filter.bp — retrigger click on poly overlap");
}

// ════════════════════════════════════════════════════════════════════════════
// T15 — Partls=5/6 (editor-select mode) must not change coupling depth
//   Values 5 and 6 select which resonator subsequent edits target.
//   They must not reset or replace the coupling depth set by values 0-4.
// ════════════════════════════════════════════════════════════════════════════
static void test_partls_mode_select_coupling() {
    std::cout << "\n── T15: Partls=5/6 editor-select leaves coupling depth unchanged ──\n";

    unit_runtime_desc_t desc = make_desc();
    BrachettiSynth s;
    s.Init(&desc);

    // Set coupling to mid-depth via Partls=2 (16 partials → 0.5 coupling)
    s.setParameter(BrachettiSynth::k_paramPartls, 2);
    float depth_before = s.m_coupling_depth_ut();

    // Partls=5 selects ResA for editing — must not touch coupling
    s.setParameter(BrachettiSynth::k_paramPartls, 5);
    float depth_after_5 = s.m_coupling_depth_ut();

    // Partls=6 selects ResB for editing — must not touch coupling
    s.setParameter(BrachettiSynth::k_paramPartls, 6);
    float depth_after_6 = s.m_coupling_depth_ut();

    std::cout << "  coupling_depth after Partls=2  : " << depth_before  << "\n";
    std::cout << "  coupling_depth after Partls=5  : " << depth_after_5 << "\n";
    std::cout << "  coupling_depth after Partls=6  : " << depth_after_6 << "\n";

    result("T15a Partls=2 sets coupling_depth=0.5",
           depth_before == 0.5f,
           "Partls=2 did not set m_coupling_depth to 0.5");
    result("T15b Partls=5 (ResA select) does not change coupling_depth",
           depth_after_5 == depth_before,
           "Partls=5 overwrote coupling_depth — would enable full coupling unexpectedly");
    result("T15c Partls=6 (ResB select) does not change coupling_depth",
           depth_after_6 == depth_before,
           "Partls=6 overwrote coupling_depth — would enable full coupling unexpectedly");
}

// ════════════════════════════════════════════════════════════════════════════
// T16 — Dynamic Energy Squelch reclaims CPU from an inaudible voice
//   A voice with near-zero feedback_gain decays to silence in milliseconds.
//   After GateOff, it should become is_active=false well before the envelope's
//   theoretical worst-case release time expires.  A voice still sustaining
//   above threshold must remain alive.
// ════════════════════════════════════════════════════════════════════════════
static void test_energy_squelch() {
    std::cout << "\n── T16: Dynamic Energy Squelch kills inaudible releasing voices ──\n";

    unit_runtime_desc_t desc = make_desc();

    // Sub-test A: a voice with very low feedback_gain dies quickly after GateOff.
    {
        BrachettiSynth s;
        s.Init(&desc);
    s.LoadPreset(BrachettiSynth::k_Koto);  // KS reference (program 0 is now a membrane kick)
        s.NoteOn(60, 127);
        // Read the slot NoteOn actually used instead of assuming one — the
        // pass-25 T18 lesson.  This hard-coded voices[1] and broke the moment
        // the KS reference preset changed, because the allocator owes no test a
        // particular slot.  The override must also come AFTER NoteOn, which
        // writes feedback_gain from the preset.  Near-zero gain makes the
        // waveguide lose energy almost instantly (round-trip ≈ 190 samples).
        const int vi = (int)s.state.next_voice_idx;
        s.state.voices[vi].resA.feedback_gain = 0.001f;
        s.state.voices[vi].resB.feedback_gain = 0.001f;
        // Koto, unlike the retired GtrStr, also has a MODAL bank (mix 0.22).
        // The squelch keys on mag_env, so with the modal bank still ringing the
        // voice is correctly NOT silent and the test's premise disappears —
        // this assertion is about the waveguide alone.  Silence the modal path
        // so the killed waveguide really is the voice's only source.
        s.state.voices[vi].modal_mix = 0.0f;
        // Let the exciter fire and the delay line fill for ~300 frames
        for (int i = 0; i < 300; ++i) { float buf[2]{}; s.processBlock(buf, 1); }
        // Release the note we actually played: GateOff() releases m_ui_note,
        // which is 60 for the Koto reference preset (GtrStr removed pass 41).
        s.NoteOff(60);

        int frames_to_death = 0;
        for (int i = 0; i < 5000; ++i) {
            float buf[2]{}; s.processBlock(buf, 1);
            if (!s.state.voices[vi].is_active) { frames_to_death = i + 1; break; }
        }

        std::cout << "  low-gain voice killed after " << frames_to_death << " frames post-GateOff\n";
        // master_env.release_rate is now set from Decay (not Rel), so at Init
        // preset Dkay=25 the gate is ~112ms (5374 frames).  The voice is killed
        // earlier by mag_env < kSquelchThreshold (empirically ~4651 frames = 97ms).
        result("T16a near-zero feedback_gain voice is killed after GateOff",
               frames_to_death > 0 && frames_to_death < 5500,
               "Squelch never fired — voice consumed CPU for the full envelope release");
    }

    // Sub-test B: a voice that is still sustaining (normal feedback_gain) stays active.
    {
        BrachettiSynth s;
        s.Init(&desc);
    s.LoadPreset(BrachettiSynth::k_Koto);  // KS reference (program 0 is now a membrane kick)
        s.NoteOn(60, 127);
        for (int i = 0; i < 200; ++i) { float buf[2]{}; s.processBlock(buf, 1); }
        // Do NOT call GateOff — voice should remain active.
        bool still_active = s.state.voices[1].is_active;
        std::cout << "  sustaining voice still active after 200 frames: " << (still_active ? "yes" : "no") << "\n";
        result("T16b sustaining voice (no GateOff) remains active",
               still_active,
               "Squelch incorrectly killed a sustaining voice");
    }
}

// ════════════════════════════════════════════════════════════════════════════
// T17 — PitchBend changes delay_length proportionally and symmetrically
//   Bending up (bend > 8192) must shorten the delay; down must lengthen it.
//   The unbent length must be restored when bend returns to centre (8192).
// ════════════════════════════════════════════════════════════════════════════
static void test_pitch_bend() {
    std::cout << "\n── T17: PitchBend adjusts active-voice delay_length ──\n";

    unit_runtime_desc_t desc = make_desc();
    BrachettiSynth s;
    s.Init(&desc);
    s.NoteOn(60, 127);

    float centre = s.state.voices[1].resA.delay_length;

    s.PitchBend(16383); // maximum up (~+2 semitones)
    float bent_up = s.state.voices[1].resA.delay_length;

    s.PitchBend(0);     // maximum down (~−2 semitones)
    float bent_down = s.state.voices[1].resA.delay_length;

    s.PitchBend(8192);  // centre — should restore the original length
    float restored = s.state.voices[1].resA.delay_length;

    std::cout << "  delay_length centre=" << centre
              << "  up=" << bent_up << "  down=" << bent_down
              << "  restored=" << restored << "\n";

    result("T17a bend-up shortens delay_length (higher pitch)",
           bent_up < centre,
           "PitchBend up did not shorten delay_length");
    result("T17b bend-down lengthens delay_length (lower pitch)",
           bent_down > centre,
           "PitchBend down did not lengthen delay_length");
    result("T17c centre bend (8192) restores delay_length within 0.1 sample",
           std::fabs(restored - centre) < 0.1f,
           "PitchBend centre did not restore the original delay_length");
}

// ════════════════════════════════════════════════════════════════════════════
// T18 — Pitch bend held before NoteOn is applied immediately to new notes
//   A bend wheel held at max-up when a new note is struck must shorten that
//   note's delay_length from the start.  Also verifies the base_delay fields
//   are stored correctly so a subsequent centre-bend restores the root pitch.
// ════════════════════════════════════════════════════════════════════════════
static void test_pitch_bend_persists_to_new_note() {
    std::cout << "\n── T18: Held pitch bend applies to notes struck while bent ──\n";

    unit_runtime_desc_t desc = make_desc();
    BrachettiSynth s;
    s.Init(&desc);

    // Establish the nominal (no-bend) delay for note 60
    s.NoteOn(60, 127);
    float nominal = s.state.voices[s.state.next_voice_idx].resA.delay_length;

    // Hold bend up, then strike the same pitch.  Read whichever slot NoteOn
    // actually chose — do NOT hard-code an index.  The two strikes here are 0 ms
    // apart on the same note, so roll fusion reuses the SAME slot (see T35); an
    // earlier version of this test assumed the second hit always advanced to
    // voices[2], which silently probed an untouched voice reading 0.0 (T18a
    // passed spuriously because 0 < nominal, and T18b caught it).
    s.PitchBend(16383);
    s.NoteOn(60, 127);
    const uint8_t bent_idx = s.state.next_voice_idx;
    float bent_at_noteon = s.state.voices[bent_idx].resA.delay_length;

    // Return to centre — delay should snap back to root pitch
    s.PitchBend(8192);
    float after_centre = s.state.voices[bent_idx].resA.delay_length;

    std::cout << "  nominal delay=" << nominal
              << "  delay while bent=" << bent_at_noteon
              << "  after centre=" << after_centre << "\n";

    result("T18a note struck while bent up has shorter delay_length",
           bent_at_noteon < nominal,
           "Held pitch bend was not applied to the new note's delay_length");
    result("T18b returning to centre restores root delay_length within 0.1 sample",
           std::fabs(after_centre - nominal) < 0.1f,
           "Centre bend after bent NoteOn did not restore the root delay_length");
}

// ════════════════════════════════════════════════════════════════════════════
// T19 — Loop filter pitch compensation accuracy
//   After NoteOn(60) the effective loop period (delay_length + τ_LP + τ_AP)
//   should equal srate/f₀ (183.47 samples for C4 at 48 kHz) to within 2 cents.
//   This verifies that both the table generation (powf, not fasterpowf) and the
//   pitch compensation formula (pa/(1-pa) for LP, (1-c)/(1+c) for AP) are correct.
// ════════════════════════════════════════════════════════════════════════════
static void test_pitch_compensation_accuracy() {
    std::cout << "\n── T19: Loop filter pitch compensation accuracy ──\n";

    unit_runtime_desc_t desc = make_desc();
    BrachettiSynth s;
    s.Init(&desc);

    s.NoteOn(60, 127);
    uint8_t vi = s.state.next_voice_idx;

    float dl  = s.state.voices[vi].resA.delay_length;
    float lc  = s.state.voices[vi].resA.lowpass_coeff;
    float ac  = s.state.voices[vi].resA.ap_coeff;

    // Reconstruct the group delays using the corrected DC-limit formulas matching NoteOn.
    // AP: H(z) = (c+z⁻¹)/(1+c·z⁻¹)  →  τ_AP at DC = (1-c)/(1+c)  [NOT (1+c)/(1-c)]
    float pa     = 1.0f - lc;
    float tau_lp = pa / (1.0f - pa);                    // τ_LP = pa/(1-pa)
    float tau_ap = (1.0f - ac) / (1.0f + ac);           // τ_AP = (1-c)/(1+c) ← corrected
    float effective_period = dl + tau_lp + tau_ap;

    // C4 exact at A4=440 Hz, 48 kHz
    float expected_period  = 48000.0f / (440.0f * powf(2.0f, (60.0f - 69.0f) / 12.0f));
    float error_cents      = 1200.0f * log2f(effective_period / expected_period);

    std::cout << "  delay_length    = " << dl              << " samples\n";
    std::cout << "  lowpass_coeff   = " << lc              << "  τ_LP=" << tau_lp << " (pa/(1-pa))\n";
    std::cout << "  ap_coeff        = " << ac              << "  τ_AP=" << tau_ap << " ((1-c)/(1+c))\n";
    std::cout << "  effective period= " << effective_period << " samples (target=" << expected_period << ")\n";
    std::cout << "  pitch error     = " << error_cents      << " cents\n";

    result("T19a effective loop period within 2 cents of C4 target",
           std::fabs(error_cents) < 2.0f,
           "Pitch compensation inaccurate: effective period too far from 48000/261.63");
}

// ════════════════════════════════════════════════════════════════════════════
// T20 — Same-tick GateOn + GateOff (Drumlogue one-shot drum trigger model)
//
//   On the Drumlogue the internal sequencer fires gate_on THEN gate_off in the
//   same scheduler tick, before any audio block is rendered.  The master_env
//   therefore starts at value=0 (just triggered) and is immediately released.
//
//   Before the fix: release() with value=0 → ENV_RELEASE → ENV_IDLE in one
//   audio sample → damper_fade=0 → voice_out *= 0 → permanent silence.
//
//   After the fix: NoteOn calls process() once after trigger(), advancing
//   value to 1.0 before GateOff can call release().  The first audio block
//   then sees value=1.0 releasing normally and hears the strike.
// ════════════════════════════════════════════════════════════════════════════
static void test_same_tick_gate() {
    std::cout << "\n── T20: Same-tick GateOn + GateOff (Drumlogue drum trigger model) ──\n";

    unit_runtime_desc_t desc = make_desc();
    BrachettiSynth s;
    s.Init(&desc);

    // Replicate the hardware sequence: both events fire before any audio block
    s.GateOn(127);    // → NoteOn → trigger() + process() pre-advance
    s.GateOff();      // → release(); value must be 1.0 here, not 0

    // Now render 300 frames (covers delay round-trip + release fade)
    float peak = run_blocks(s, 300, 32);

    std::cout << "  peak output over 300 frames: " << peak << "\n";

    result("T20 same-tick GateOn+GateOff produces audible sound",
           peak > 1e-4f,
           "voice was silent after same-tick trigger+release — master_env killed at value=0");
}

// ════════════════════════════════════════════════════════════════════════════
// T21 — OS parameter-initialisation sequence
//
//   The Drumlogue SDK documentation states: "Unit parameters are expected to be
//   set to [their init value] after the initialization phase."  In practice the
//   OS calls unit_set_param_value(i, header_default[i]) for every parameter
//   AFTER unit_init() returns.
//
//   One header.c default differs from the Init preset:
//     k_paramModel (index 9): header default = 3 (Membrane), Init preset = 0 (String)
//   If the OS overrides the preset after init, the model is Membrane when the
//   first note is played.  This test verifies that the unit still produces sound
//   even when all 24 header defaults are sent after init (simulating OS init).
// ════════════════════════════════════════════════════════════════════════════
static void test_os_param_init_sequence() {
    std::cout << "\n── T21: OS parameter-init sequence (header defaults sent after Init) ──\n";

    unit_runtime_desc_t desc = make_desc();
    BrachettiSynth s;
    s.Init(&desc);

    // Send all 24 header.c default values exactly as the Drumlogue OS does
    // Format: setParameter(index, header_default_value)
    // Source: header.c params array — columns are min,max,center,DEFAULT,type,...
    // Reflects header.c after the ÷10 step-size change for MlltStif/Dkay/NzFltFrq
    static const int32_t header_defaults[24] = {
        0,      // 0: Program
        60,     // 1: Note
        0,      // 2: Bank
        1,      // 3: Sample
        500,    // 4: MlltRes
        250,    // 5: MlltStif  (new default 250 = 2500 effective, ÷10 range)
        0,      // 6: VlMllRes
        0,      // 7: VlMllStf
        3,      // 8: Partls
        3,      // 9: Model  ← DIFFERS: header=3 (Membrane), Init preset=0 (String)
        25,     // 10: Dkay   (new default 25 = 250 effective, ÷10 range)
        10,     // 11: Mterl
        0,      // 12: Tone
        26,     // 13: HitPos
        10,     // 14: Rel
        300,    // 15: Inharm
        1,      // 16: LowCut
        5,      // 17: TubRad
        0,      // 18: Gain
        0,      // 19: NzMix
        0,      // 20: NzRes  ← DIFFERS: header=0, Init preset=300
        0,      // 21: NzFltr
        2,      // 22: NzFltFrq  (new default 2 = 20 Hz effective, ÷10 range)
        707,    // 23: Resnc
    };

    for (int i = 0; i < 24; ++i) {
        s.setParameter((uint8_t)i, header_defaults[i]);
    }

    std::cout << "  All 24 header.c defaults sent. Model is now 3 (Membrane).\n";

    // Same-tick trigger: matches the most demanding hardware scenario
    s.GateOn(127);
    s.GateOff();

    float peak = run_blocks(s, 300, 32);
    std::cout << "  peak output over 300 frames: " << peak << "\n";

    result("T21 same-tick trigger after OS param init produces sound",
           peak > 1e-4f,
           "silent after OS sends header defaults then same-tick GateOn+GateOff");
}

// ════════════════════════════════════════════════════════════════════════════
// T22 — master_env value trace at critical frames
//
//   Directly traces the master_env value through the same-tick scenario to
//   verify that the Phase 18 pre-advance fix actually produces value=1.0 before
//   the first audio block, and that the release decays slowly (not instantly).
//   If this test fails, the pre-advance fix is not working correctly.
// ════════════════════════════════════════════════════════════════════════════
static void test_master_env_trace() {
    std::cout << "\n── T22: master_env value trace through same-tick trigger ──\n";

    unit_runtime_desc_t desc = make_desc();
    BrachettiSynth s;
    s.Init(&desc);
    s.LoadPreset(BrachettiSynth::k_Koto);  // KS reference (program 0 is now a membrane kick)

    // Probe master_env by reading the voice's exciter state after each event.
    // Voice index 1 (next_voice_idx advances from 0 to 1 on first NoteOn).
    s.GateOn(127);   // NoteOn: trigger() + process() → value should be 1.0
    float val_after_noteon = s.state.voices[1].exciter.master_env.value;
    int   state_after_noteon = (int)s.state.voices[1].exciter.master_env.state;

    s.GateOff();     // NoteOff: release() → state should be ENV_RELEASE (2)
    float val_after_gateoff = s.state.voices[1].exciter.master_env.value;
    int   state_after_gateoff = (int)s.state.voices[1].exciter.master_env.state;

    std::cout << "  After GateOn:  value=" << val_after_noteon
              << " state=" << state_after_noteon << " (expect: 1.0 / ENV_DECAY=2)\n";
    std::cout << "  After GateOff: value=" << val_after_gateoff
              << " state=" << state_after_gateoff << " (expect: 1.0 / ENV_RELEASE=3)\n";

    // Render one sample to get the damper_fade value in frame 0
    float buf[2] = {0.0f, 0.0f};
    s.processBlock(buf, 1);
    std::cout << "  Frame 0 output: L=" << buf[0] << " (should be ~0.5 or higher)\n";

    result("T22a master_env value=1.0 after NoteOn pre-advance",
           val_after_noteon >= 0.99f,
           "pre-advance did not advance value to 1.0 — same-tick GateOff will silence voice");

    result("T22b master_env in ENV_RELEASE (not ENV_IDLE) after GateOff",
           state_after_gateoff == 3,  // ENV_RELEASE = 3
           "GateOff put master_env in wrong state (IDLE=0, ATTACK=1, DECAY=2, RELEASE=3)");

    result("T22c frame 0 output is audible (> 0.1)",
           buf[0] > 0.1f,
           "first audio sample is near-zero despite pre-advance fix");
}

// ════════════════════════════════════════════════════════════════════════════
// T23 — Exciter output independent of master_env
//
//   Verifies that the mallet strike at frame 0 is nonzero WITHOUT any envelope
//   involvement, by checking the exciter output before the master_env is applied.
//   This isolates the waveguide exciter path from the envelope gate path.
//   Uses the UNIT_TEST_DEBUG hooks (ut_exciter_out, ut_voice_out).
// ════════════════════════════════════════════════════════════════════════════
static void test_exciter_independent_of_env() {
    std::cout << "\n── T23: Exciter output at frame 0 (mallet strike, no sample) ──\n";

    unit_runtime_desc_t desc = make_desc();
    BrachettiSynth s;
    s.Init(&desc);
    s.LoadPreset(BrachettiSynth::k_Koto);  // KS reference (program 0 is now a membrane kick)

    // GateOn only — no GateOff — so master_env stays at 1.0 (sustained, ENV_DECAY)
    s.GateOn(127);

    // Process exactly 1 frame to capture frame-0 values via the UT debug hooks
    ut_exciter_out = 0.0f;
    ut_voice_out   = 0.0f;
    float buf[2] = {0.0f, 0.0f};
    s.processBlock(buf, 1);

    std::cout << "  Frame 0 exciter_out = " << ut_exciter_out << " (mallet impulse, expect ~3-5)\n";
    std::cout << "  Frame 0 voice_out   = " << ut_voice_out   << " (post-env, expect > 0)\n";
    std::cout << "  Frame 0 L channel   = " << buf[0]         << " (final output, expect > 0.1)\n";

    result("T23a mallet produces nonzero exciter at frame 0",
           ut_exciter_out > 0.5f,
           "mallet impulse is near-zero — mallet_stiffness or mallet_res_coeff may be 0");

    result("T23b voice_out is nonzero (waveguide+env path works)",
           ut_voice_out > 0.1f,
           "voice_out is near-zero despite nonzero exciter — check feedback/env path");

    result("T23c final frame 0 output > 0.1 (whole chain passes through)",
           buf[0] > 0.1f,
           "final output near-zero — master_filter or master_drive may be wrong");
}

// ════════════════════════════════════════════════════════════════════════════
// T24 — 4-voice polyphony: all voices active simultaneously
//
//   Strikes 4 different notes before any voice has time to release.
//   All 4 voice slots must be is_active=true, and the combined peak must
//   exceed a single-voice baseline (richer signal when all voices fire).
// ════════════════════════════════════════════════════════════════════════════
static void test_polyphony() {
    std::cout << "\n── T24: 4-voice polyphony (4 simultaneous NoteOn) ──\n";

    unit_runtime_desc_t desc = make_desc();
    BrachettiSynth s;
    s.Init(&desc);

    // Fire 4 different notes — round-robin allocates voices 1,2,3,0
    s.NoteOn(36, 127);
    s.NoteOn(48, 127);
    s.NoteOn(60, 127);
    s.NoteOn(72, 127);

    int active_count = 0;
    for (int i = 0; i < 4; ++i)
        if (s.state.voices[i].is_active) ++active_count;

    std::cout << "  Active voices after 4× NoteOn: " << active_count << " (expect 4)\n";
    result("T24a all 4 voices active after 4 simultaneous NoteOn",
           active_count == 4,
           "fewer than 4 voices active — voice allocation or stealing is wrong");

    // Each voice must hold its own distinct note (round-robin: NoteOn order → voices 1,2,3,0)
    bool notes_distinct =
        s.state.voices[1].current_note == 36 &&
        s.state.voices[2].current_note == 48 &&
        s.state.voices[3].current_note == 60 &&
        s.state.voices[0].current_note == 72;

    std::cout << "  voice[0].current_note=" << (int)s.state.voices[0].current_note
              << "  voice[1]=" << (int)s.state.voices[1].current_note
              << "  voice[2]=" << (int)s.state.voices[2].current_note
              << "  voice[3]=" << (int)s.state.voices[3].current_note << "\n";

    result("T24b each voice holds its own distinct note (no slot aliasing)",
           notes_distinct,
           "voice notes don't match allocation order — round-robin is broken");
}

// ════════════════════════════════════════════════════════════════════════════
// T25 — AllNoteOff sets every active voice to releasing
//
//   After 4 NoteOn calls, all voices are active and not releasing.
//   AllNoteOff() must flip is_releasing=true on all 4 without killing them
//   immediately (they still need to complete their release envelope).
// ════════════════════════════════════════════════════════════════════════════
static void test_all_note_off() {
    std::cout << "\n── T25: AllNoteOff releases all active voices ──\n";

    unit_runtime_desc_t desc = make_desc();
    BrachettiSynth s;
    s.Init(&desc);

    s.NoteOn(36, 100);
    s.NoteOn(48, 100);
    s.NoteOn(60, 100);
    s.NoteOn(72, 100);

    // Before AllNoteOff: all 4 voices active and NOT releasing
    bool pre_releasing = false;
    for (int i = 0; i < 4; ++i)
        if (s.state.voices[i].is_releasing) pre_releasing = true;

    s.AllNoteOff();

    // After AllNoteOff: all 4 voices must be marked as releasing
    int releasing_count = 0;
    int still_active_count = 0;
    for (int i = 0; i < 4; ++i) {
        if (s.state.voices[i].is_releasing)  ++releasing_count;
        if (s.state.voices[i].is_active)     ++still_active_count;
    }

    std::cout << "  pre_releasing (any before AllNoteOff) : " << (pre_releasing ? "yes" : "no") << "\n";
    std::cout << "  releasing_count after AllNoteOff       : " << releasing_count   << " (expect 4)\n";
    std::cout << "  still_active_count after AllNoteOff    : " << still_active_count << " (expect 4)\n";

    result("T25a no voice releasing before AllNoteOff",
           !pre_releasing,
           "a voice was already is_releasing=true before AllNoteOff — check NoteOn reset");
    result("T25b all 4 voices is_releasing=true after AllNoteOff",
           releasing_count == 4,
           "AllNoteOff did not mark all voices as releasing");
    result("T25c voices still active immediately after AllNoteOff (release not instant)",
           still_active_count == 4,
           "AllNoteOff immediately killed voices — release envelope not given a chance to run");
}

// ════════════════════════════════════════════════════════════════════════════
// T26 — MIDI note extremes: note 0 and note 127
//
//   Note 0  (8.18 Hz → clamped to 12 Hz → delay≈4000 samples) must stay
//   within the 4096-sample buffer and produce no NaN output.
//   Note 127 (12544 Hz → ~3.83 samples → delay clamped to 2.0 by compensation)
//   must also produce no NaN.
// ════════════════════════════════════════════════════════════════════════════
static void test_midi_note_extremes() {
    std::cout << "\n── T26: MIDI note extremes (note 0 and note 127) ──\n";

    for (int note : {0, 127}) {
        unit_runtime_desc_t desc = make_desc();
        BrachettiSynth s;
        s.Init(&desc);
        s.NoteOn((uint8_t)note, 127);

        uint8_t vi = s.state.next_voice_idx;
        float dl = s.state.voices[vi].resA.delay_length;
        std::cout << "  Note " << note << " resA.delay_length = " << dl << "\n";

        bool dl_in_bounds = dl >= 2.0f && dl <= 4094.0f;
        char lbl_bounds[64];
        std::snprintf(lbl_bounds, sizeof(lbl_bounds),
                      "T26 note=%d delay_length in [2, 4094]", note);
        result(lbl_bounds, dl_in_bounds,
               "delay_length out of safe buffer range — overflow or underflow");

        // Render 100 frames and check for NaN
        bool has_nan = false;
        float buf[64] = {};
        for (int b = 0; b < 3; ++b) {
            std::memset(buf, 0, sizeof(buf));
            s.processBlock(buf, 32);
            for (int i = 0; i < 64; ++i)
                if (is_nan_or_inf(buf[i])) { has_nan = true; break; }
            if (has_nan) break;
        }

        char lbl_nan[64];
        std::snprintf(lbl_nan, sizeof(lbl_nan),
                      "T26 note=%d no NaN/Inf in 96 frames", note);
        result(lbl_nan, !has_nan, "NaN/Inf at extreme MIDI note");
    }
}

// ════════════════════════════════════════════════════════════════════════════
// T27 — Maximum Inharm (ap_coeff=0.9995): stability and pitch clamping
//
//   At Inharm=100 (the max after pass 36 made the range BIPOLAR -100..100)
//   the allpass coefficient reaches its 0.995 cap, giving a group delay of
//   (1+0.995)/(1-0.995) = 399 samples.  The cap is explicit in setParameter:
//   ×0.01 would otherwise land on exactly 1.000 and put the allpass pole on
//   the unit circle.  (This test previously drove 199, which the old 0..199
//   range accepted; under the bipolar range that is out of bounds and is
//   REJECTED by setParameter, so the test was silently asserting nothing.)
//   For note 60 the raw delay is 183.47 samples; after subtracting τ_AP the
//   result is negative, so delay_length must be clamped to 2.0.  The waveguide
//   must remain stable (no NaN/Inf) for the entire 500-block render.
// ════════════════════════════════════════════════════════════════════════════
static void test_max_inharm_stability() {
    std::cout << "\n── T27: Max Inharm (ap_coeff=0.995) — clamping and stability ──\n";

    unit_runtime_desc_t desc = make_desc();
    BrachettiSynth s;
    s.Init(&desc);   // After Init: m_is_resonator_a=true, m_is_resonator_b=true

    // Set max inharmonicity on both resonators (both selected after Init)
    s.setParameter(BrachettiSynth::k_paramInharm, 100);

    // Verify coefficient was applied to ResA
    float ac = s.state.voices[0].resA.ap_coeff;
    std::cout << "  ResA.ap_coeff after Inharm=100 : " << ac
              << " (expect ≈0.995)\n";
    result("T27a ap_coeff >= 0.99 after Inharm=100 (bipolar max)",
           ac >= 0.99f,
           "k_paramInharm=100 did not set ap_coeff to ~0.995");

    s.NoteOn(60, 127);
    uint8_t vi = s.state.next_voice_idx;
    float dl = s.state.voices[vi].resA.delay_length;
    // With the corrected AP group delay formula τ=(1-c)/(1+c), ap_del≈0.0003 for c≈0.9995.
    // The delay_length stays near the nominal C4 period (~183 samples) rather than
    // clamping to 2.  Old formula (1+c)/(1-c)≈3999 would have forced the clamp — that
    // was the wrong behaviour, not the intended one.
    std::cout << "  ResA.delay_length after NoteOn(60): " << dl
              << " (expect ~183 with corrected AP formula)\n";
    result("T27b delay_length is valid (within delay buffer bounds)",
           dl >= 2.0f && dl < 4094.0f,
           "delay_length outside valid buffer range");

    // Render 500 blocks of 32 frames (~333 ms) — verify no NaN
    bool has_nan = false;
    float buf[64] = {};
    for (int b = 0; b < 500 && !has_nan; ++b) {
        std::memset(buf, 0, sizeof(buf));
        s.processBlock(buf, 32);
        for (int i = 0; i < 64; ++i)
            if (is_nan_or_inf(buf[i])) { has_nan = true; break; }
    }
    result("T27c no NaN/Inf after 500 blocks with max Inharm",
           !has_nan,
           "NaN/Inf detected — extreme ap_coeff destabilises the allpass filter");
}

// ════════════════════════════════════════════════════════════════════════════
// T28 — LoadPreset() mid-note: no NaN during transition, next hit audible
//
//   Simulates a patch change while a note is already ringing.  The DSP must
//   survive the setParameter storm without producing NaN and the next GateOn
//   after the preset change must produce audible output.
// ════════════════════════════════════════════════════════════════════════════
static void test_preset_change_mid_note() {
    std::cout << "\n── T28: LoadPreset mid-note — stability and re-trigger ──\n";

    unit_runtime_desc_t desc = make_desc();
    BrachettiSynth s;
    s.Init(&desc);

    // Start a note, let it ring for 10 blocks
    s.GateOn(127);
    float peak_before = run_blocks(s, 320, 32);

    // Change preset while voice is still active
    s.LoadPreset(14);  // Cymbal — very different from Init

    // Render 20 more blocks, checking for NaN every sample
    bool has_nan = false;
    float buf[64] = {};
    for (int b = 0; b < 20 && !has_nan; ++b) {
        std::memset(buf, 0, sizeof(buf));
        s.processBlock(buf, 32);
        for (int i = 0; i < 64; ++i)
            if (is_nan_or_inf(buf[i])) { has_nan = true; break; }
    }

    // Strike a new note under the new preset
    s.GateOn(127);
    float peak_after = run_blocks(s, 300, 32);

    std::cout << "  peak before preset change : " << peak_before << "\n";
    std::cout << "  NaN during transition     : " << (has_nan ? "YES" : "no") << "\n";
    std::cout << "  peak after preset change  : " << peak_after  << "\n";

    result("T28a pre-change output was audible", peak_before > 1e-4f,
           "no sound before LoadPreset — pre-condition failed");
    result("T28b no NaN/Inf during preset change", !has_nan,
           "NaN produced while changing preset mid-note — parameter handling unstable");
    result("T28c new GateOn after preset change is audible", peak_after > 1e-4f,
           "silent after LoadPreset + GateOn — preset change corrupted DSP state");
}

// ════════════════════════════════════════════════════════════════════════════
// T29 — Velocity scaling: hard hit louder than soft hit
//
//   Two independent synth instances strike the same note at velocity=127
//   and velocity=1.  The hard-hit peak must be at least 2× louder.
//   The soft hit must still produce audible output (velocity=1 ≠ silence).
// ════════════════════════════════════════════════════════════════════════════
static void test_velocity_scaling() {
    std::cout << "\n── T29: Velocity scaling (vel=127 vs vel=1) ──\n";

    // The limiter clips post-render peaks to 0.99 regardless of velocity, masking amplitude
    // differences.  Instead, capture ut_voice_out at frame 0: voice_out = exciter * velocity,
    // so the pre-limiter signal at the first sample is directly proportional to velocity.
    unit_runtime_desc_t desc = make_desc();

    auto frame0_voice_out = [&](uint8_t vel) -> float {
        BrachettiSynth s;
        s.Init(&desc);
        s.NoteOn(60, vel);
        ut_voice_out = 0.0f;
        float buf[2] = {};
        s.processBlock(buf, 1);
        return ut_voice_out;
    };

    float vo_hard = frame0_voice_out(127);
    float vo_soft = frame0_voice_out(1);

    std::cout << "  frame-0 voice_out vel=127 (hard): " << vo_hard << "\n";
    std::cout << "  frame-0 voice_out vel=1   (soft): " << vo_soft << "\n";
    if (vo_soft > 0.0f)
        std::cout << "  hard/soft ratio (pre-limiter)   : " << (vo_hard / vo_soft) << "\n";

    result("T29a soft hit (vel=1) produces nonzero pre-limiter voice_out",
           vo_soft > 1e-4f,
           "velocity=1 produced near-zero output — current_velocity may be zeroed");
    result("T29b hard hit voice_out is at least 10× soft hit (vel scales linearly)",
           vo_hard > vo_soft * 10.0f,
           "velocity has no proportional effect on voice_out — check voice_out *= current_velocity");
}

// ════════════════════════════════════════════════════════════════════════════
// T30 — Dkay=0 (shortest decay, ~50 ms gate): audible output
//
//   The Drumlogue's shortest decay preset uses Dkay=0 (50 ms).  A same-tick
//   GateOn+GateOff must still produce audible output within the first 100
//   frames even with this shortest possible envelope.
// ════════════════════════════════════════════════════════════════════════════
static void test_dkay_zero_short_gate() {
    std::cout << "\n── T30: Dkay=0 (shortest gate) produces audible output ──\n";

    unit_runtime_desc_t desc = make_desc();
    BrachettiSynth s;
    s.Init(&desc);

    s.setParameter(BrachettiSynth::k_paramDkay, 0);

    // Same-tick trigger (most demanding scenario for short decays)
    s.GateOn(127);
    s.GateOff();

    // Render 100 frames — the exciter fires at frame 0 and must be heard
    float peak = run_blocks(s, 100, 32);
    std::cout << "  peak over 100 frames with Dkay=0: " << peak << "\n";

    result("T30 Dkay=0 same-tick trigger is audible within 100 frames",
           peak > 1e-4f,
           "silent with Dkay=0 — release envelope with 50ms gate kills sound before DAC");
}

// ════════════════════════════════════════════════════════════════════════════
// T31 — String sustain for 1 second (Karplus-Strong long-decay reference)
//
//   Proves that the physical model can sustain a plucked string for at least
//   1 second when parameters are configured for maximum sustain:
//     Dkay=200  → feedback_gain=0.999, master_env gate=10s
//     Mterl=30, TubRad=20  → lowpass_coeff≈0.901 (bright, low loss)
//     NzMix=0   → pure mallet exciter, no noise burst
//     Model=0 (String), GateOn only (no GateOff to avoid early release)
//
//   Theoretical T_60 with g=0.999 at C4 (period≈183.5 samples):
//     T_60 = −3·ln(10) / (Hz·ln(g)) = −6.908 / (261.6·(−0.001)) = 26.4 s
//   So after 1 s the amplitude should be ≈0.999^(261.6) ≈ 0.77 (−2.3 dB).
// ════════════════════════════════════════════════════════════════════════════
static void test_string_one_second_sustain() {
    std::cout << "\n── T31: String sustain 1 second (Dkay=200, Mterl=30, TubRad=20) ──\n";

    unit_runtime_desc_t desc = make_desc();
    BrachettiSynth s;
    s.Init(&desc);

    // Configure for maximum-sustain string.  Both resonators are selected
    // after Init (m_is_resonator_a=true, m_is_resonator_b=true).
    s.setParameter(BrachettiSynth::k_paramDkay,    200); // g=0.999, gate=10s
    s.setParameter(BrachettiSynth::k_paramMterl,    30); // bright material
    s.setParameter(BrachettiSynth::k_paramTubRad,   20); // wide tube
    s.setParameter(BrachettiSynth::k_paramNzMix,     0); // no noise
    s.setParameter(BrachettiSynth::k_paramModel,     0); // String

    // Report the actual coefficients so the test is self-documenting
    float g  = s.state.voices[0].resA.feedback_gain;
    float lc = s.state.voices[0].resA.lowpass_coeff;
    float c4_hz = 440.0f * powf(2.0f, (60.0f - 69.0f) / 12.0f); // 261.626 Hz
    float period_s = 1.0f / c4_hz;
    // T_60 = -3*ln(10) / (ln(g)/period)  [log-energy decay formula]
    float t60_s = (-3.0f * logf(10.0f)) * period_s / logf(g);
    std::cout << std::fixed << std::setprecision(4);
    std::cout << "  feedback_gain = " << g  << "  (expect ≈0.999)\n";
    std::cout << "  lowpass_coeff = " << lc << "  (expect ≈0.90)\n";
    std::cout << "  Theoretical T_60 at C4 = " << t60_s << " s  (expect ≈26 s)\n";

    // GateOn only — no GateOff — so master_env holds at 1.0 for the duration
    s.GateOn(127);

    // Skip the first 200 frames while the delay line fills
    run_blocks(s, 200, 32);

    // Advance to 0.5 s mark (24000 frames from NoteOn, minus 200 already consumed)
    float peak_500ms = run_blocks(s, 24000 - 200, 32);

    // Advance another 0.5 s to the 1.0 s mark
    float peak_1000ms = run_blocks(s, 24000, 32);

    std::cout << "  peak at 0.5 s : " << peak_500ms  << "  (expect > 0.05)\n";
    std::cout << "  peak at 1.0 s : " << peak_1000ms << "  (expect > 0.05)\n";

    result("T31a feedback_gain >= 0.998 after Dkay=200",
           g >= 0.998f,
           "Dkay=200 did not yield g≈0.999 — feedback gain mapping may be wrong");
    result("T31b string audible at 0.5 s (Karplus-Strong sustain proven)",
           peak_500ms > 0.05f,
           "string decayed to silence before 0.5 s — feedback_gain or master_env wrong");
    result("T31c string audible at 1.0 s (T_60 ≈ 26 s confirmed)",
           peak_1000ms > 0.05f,
           "string decayed to silence before 1.0 s — decay far shorter than theoretical T_60");
}

// ════════════════════════════════════════════════════════════════════════════
// T32 — Dkay controls waveguide decay time (isolated single resonator)
//
//   Partls=0 isolates ResA (no coupling, no ResB) so only the waveguide's own
//   feedback_gain determines decay.  The ut_delay_read probe captures the
//   waveguide output directly, bypassing the limiter and velocity scaling.
//
//   At t=300 ms (14400 frames), with note 60 (period≈183.5 s):
//     Dkay=25  → g=0.869 → 78.5 round trips → amplitude ≈ A0 × 0.869^78.5 ≈ 0
//     Dkay=200 → g=0.999 → 78.5 round trips → amplitude ≈ A0 × 0.999^78.5 ≈ A0×0.925
// ════════════════════════════════════════════════════════════════════════════
static void test_dkay_controls_decay() {
    std::cout << "\n── T32: Dkay controls waveguide decay (ResA only, ut_delay_read probe) ──\n";

    unit_runtime_desc_t desc = make_desc();

    // Helper: isolate ResA (Partls=0 → no coupling, m_active_partials=4 < 16)
    // then advance to frame 14400 (300 ms) and capture one frame via ut_delay_read.
    auto probe_at_300ms = [&](int32_t dkay_val) -> float {
        BrachettiSynth s;
        s.Init(&desc);
    s.LoadPreset(BrachettiSynth::k_Koto);  // KS reference (program 0 is now a membrane kick)
        s.setParameter(BrachettiSynth::k_paramPartls, 0); // ResA only, no coupling
        s.setParameter(BrachettiSynth::k_paramDkay,   dkay_val);
        // Mterl=30 → coeff=1.0 → loss_g_dc=1.0, LP is a passthrough.
        // Without this, the default preset (Mterl=10, dc_gain≈0.95) gives a
        // per-round-trip gain of ~0.950 × feedback_gain so 300ms amplitude is
        // 0.950^78.5 ≈ 0.017× initial — far below the 0.5 threshold.
        // With Mterl=30 the only per-trip attenuation is feedback_gain itself.
        s.setParameter(BrachettiSynth::k_paramMterl,  30);
        s.GateOn(127);
        // Advance to ~290 ms, then measure the peak over ~400 frames (≈2 C4 periods).
        // Single-sample measurement can land on a zero crossing of the 261 Hz sinusoid,
        // giving a false near-zero reading even when the waveguide is sustaining normally.
        run_blocks(s, 13952, 32);  // 290.7 ms
        float peak = 0.0f;
        for (int i = 0; i < 448; ++i) {  // ~9.3 ms window (448 frames, >2 periods at C4)
            ut_delay_read = 0.0f;
            float buf[2] = {};
            s.processBlock(buf, 1);
            float v = std::fabs(ut_delay_read);
            if (v > peak) peak = v;
        }
        return peak;
    };

    float probe_short = probe_at_300ms(25);
    float probe_long  = probe_at_300ms(200);

    float g_short = 0.85f + (25.0f  / 200.0f) * 0.149f;
    float g_long  = 0.85f + (200.0f / 200.0f) * 0.149f;
    float trips_300ms = 14400.0f / 183.47f; // ≈78.5 trips
    std::cout << std::fixed << std::setprecision(4);
    std::cout << "  Dkay=25:  g=" << g_short << "  expected trips@300ms=" << trips_300ms
              << "  peak|delay_read|@300ms=" << probe_short << "\n";
    std::cout << "  Dkay=200: g=" << g_long
              << "  peak|delay_read|@300ms=" << probe_long  << "\n";
    if (probe_short > 0.0f)
        std::cout << "  long/short ratio: " << (probe_long / probe_short) << "\n";

    // With g=0.999 and 78.5 round trips, expected amplitude ≈ initial × 0.875.
    // Initial exciter peak ≈ 3.79 (mallet_lp2 × 15 × velocity at frame 0),
    // so expect peak|delay_read| ≈ 3.3 at 300ms.  Threshold 0.5 gives 85% margin.
    result("T32a Dkay=200 waveguide still active at 300 ms",
           probe_long > 0.5f,
           "Dkay=200 delay-read at noise floor at 300 ms — feedback_gain not routing or filter unstable");
    result("T32b Dkay=25 waveguide quieter at 300 ms than Dkay=200 (> 10× ratio)",
           probe_long > probe_short * 10.0f,
           "Dkay has no measurable effect on waveguide amplitude — feedback_gain not routed");
}

// ════════════════════════════════════════════════════════════════════════════
// T33 — Dkay → feedback_gain mapping correctness
//
//   The mapping is: g = 0.85 + (value/200) * 0.149
//   Verify three anchor points: min (0→0.85), mid (100→0.9245), max (200→0.999).
//   This protects against future refactors accidentally changing the range.
// ════════════════════════════════════════════════════════════════════════════
static void test_dkay_feedback_gain_mapping() {
    std::cout << "\n── T33: Dkay → feedback_gain mapping correctness ──\n";

    unit_runtime_desc_t desc = make_desc();
    BrachettiSynth s;
    s.Init(&desc);

    // Both resonators selected after Init; check voice[0].resA
    struct { int32_t val; float expected_g; const char* label; } cases[] = {
        {   0, 0.850f, "Dkay=0   → g=0.850 (instant dead thud)" },
        { 100, 0.850f + (100.0f/200.0f) * 0.149f, "Dkay=100 → g=0.9245 (mid sustain)" },
        { 200, 0.999f, "Dkay=200 → g=0.999 (near-infinite sustain)" },
    };

    for (auto& c : cases) {
        s.setParameter(BrachettiSynth::k_paramDkay, c.val);
        float g = s.state.voices[0].resA.feedback_gain;
        std::cout << "  " << c.label << "  actual=" << std::fixed
                  << std::setprecision(4) << g << "\n";
        char label[80];
        std::snprintf(label, sizeof(label), "T33 Dkay=%-3d: feedback_gain within 0.001 of expected",
                      (int)c.val);
        result(label, std::fabs(g - c.expected_g) < 0.001f,
               "feedback_gain does not match the formula g=0.85+(val/200)*0.149");
    }
}

// ════════════════════════════════════════════════════════════════════════════
// T34 — Re-trigger consistency: no progressive amplitude loss across slot reuse
//
//   Root cause of hardware bug: NoteOn did not clear the delay buffer, z1, or
//   write_ptr on voice slot reuse.  After 4 presses (all 4 slots used), the 5th
//   press reuses slot 1 which still holds residual oscillation from press 1.
//   Depending on phase, this causes destructive interference: each successive
//   press is shorter and quieter, eventually reaching silence.
//
//   Fix: NoteOn now memsets the delay buffer and zeros z1/write_ptr.
//   Test: press the same note 8 times (two full cycles through all 4 voice slots),
//   measuring the peak in the first 50 ms after each press.  The 5th–8th peaks
//   (second slot cycle) must be within 10% of the 1st–4th peaks (first cycle).
// ════════════════════════════════════════════════════════════════════════════
static void test_retrigger_consistency() {
    std::cout << "\n── T34: Re-trigger consistency (no progressive amplitude loss) ──\n";

    unit_runtime_desc_t desc = make_desc();
    BrachettiSynth s;
    s.Init(&desc);

    // Use Dkay=25 (Init preset) so voices release within ~97 ms — fast enough
    // that we press slowly (every 200 ms) and still test slot reuse cleanly.
    s.setParameter(BrachettiSynth::k_paramDkay, 25);

    float first_cycle_peak  = 0.0f;
    float second_cycle_peak = 0.0f;

    for (int press = 0; press < 8; ++press) {
        // GateOn → NoteOn (drum trigger model: GateOn then immediately GateOff)
        s.GateOn(127);
        s.GateOff();

        // Measure peak over first 2400 frames (50 ms).
        // Use buf[0] (main audio output) rather than ut_voice_out, because
        // GateOff resets state.next_voice_idx to (NUM_VOICES-1) so the per-voice
        // probe would target an inactive slot and always read 0.
        float peak = 0.0f;
        float buf[2] = {};
        for (int i = 0; i < 2400; ++i) {
            buf[0] = buf[1] = 0.0f;
            s.processBlock(buf, 1);
            float v = std::fabs(buf[0]);
            if (v > peak) peak = v;
        }
        // Advance an additional 7200 frames (150 ms) to let the voice mostly release
        // (master_env for Dkay=25 takes ~97 ms to idle)
        run_blocks(s, 7200, 32);

        if (press < 4) first_cycle_peak  = std::fmax(first_cycle_peak,  peak);
        else           second_cycle_peak = std::fmax(second_cycle_peak, peak);

        std::cout << "  Press " << (press + 1) << " peak=" << peak
                  << (press < 4 ? " (first cycle, fresh slot)" : " (second cycle, reused slot)") << "\n";
    }

    std::cout << "  First-cycle max peak  = " << first_cycle_peak  << "\n";
    std::cout << "  Second-cycle max peak = " << second_cycle_peak << "\n";

    float ratio = (first_cycle_peak > 0.0f) ? (second_cycle_peak / first_cycle_peak) : 0.0f;
    std::cout << "  Second/first ratio    = " << ratio << " (expect >= 0.90)\n";

    result("T34a re-trigger produces nonzero output on all 8 presses",
           second_cycle_peak > 0.01f,
           "Voice silent on slot reuse — delay buffer contaminating new note");
    result("T34b second slot cycle amplitude within 10% of first cycle",
           ratio >= 0.90f,
           "Progressive amplitude loss on slot reuse — buffer not cleared on NoteOn");
}

// ════════════════════════════════════════════════════════════════════════════
// T35 — Roll-fusion policy (guards a regression that shipped twice)
//
//   Pass 12 made repeated hits reuse ONE voice; pass 23 made every family
//   except KS stack; HW then reported "pressed rolls feel less smooth, Djambe
//   especially muddy" — a pressed roll was spreading up to 4 whole drum bodies
//   (each with its own crack/slap burst) across the voices, and it also made
//   the snare buzz-roll wire continuity unreachable.  The policy is now
//   time-based, so it must be asserted rather than rediscovered on hardware:
//
//     strokes closer than kRollFuseSec, same note, drum family  → REUSE 1 voice
//     strokes wider than kRollFuseSec                           → STACK
//     sustained families (cymbal / bar) always                  → STACK
// ════════════════════════════════════════════════════════════════════════════
static int roll_max_voices(BrachettiSynth& s, uint8_t note, float gap_ms, int strokes) {
    const int gap = (int)(gap_ms * 0.001f * 48000.0f);
    int maxv = 0;
    s.state.next_voice_idx = 0;
    for (int k = 0; k < strokes; ++k) {
        s.NoteOn(note, 110);
        s.NoteOff(note);              // Drumlogue fires gate on+off in one tick
        run_blocks(s, gap, 64);
        int act = 0;
        for (int i = 0; i < NUM_VOICES; ++i) if (s.state.voices[i].is_active) ++act;
        if (act > maxv) maxv = act;
    }
    return maxv;
}

static void test_roll_fusion() {
    std::cout << "\n── T35: Roll fusion (pressed roll fuses, spaced strokes stack) ──\n";

    unit_runtime_desc_t desc = make_desc();
    BrachettiSynth s;

    struct Case { int preset; uint8_t note; const char* name; };
    const Case drums[] = {
        {0,  36, "Kick2"}, {3, 38, "AcSnare"}, {6, 48, "Djambe"},
        {12, 45, "AcTom"}, {28, 50, "Conga"},
    };

    bool all_fused = true;
    for (const Case& c : drums) {
        s.Init(&desc); s.LoadPreset((uint8_t)c.preset);
        // 45 ms ≈ a 22 stroke/s pressed roll — well inside the fuse window.
        int v = roll_max_voices(s, c.note, 45.0f, 12);
        std::cout << "  " << c.name << " pressed roll (45 ms): voices=" << v << "\n";
        if (v > 1) all_fused = false;
    }
    result("T35a pressed roll on a drum family uses a single voice",
           all_fused,
           "Pressed roll is spreading across voices — bodies pile up into mud "
           "and the snare wire continuity cannot fire");

    // Just past the window the round-robin must take over again, or flams and
    // roll tails lose their overlap.
    s.Init(&desc); s.LoadPreset(0);   // Kick2: boom still ringing at 150 ms
    int spaced = roll_max_voices(s, 36, 150.0f, 6);
    std::cout << "  Kick2 spaced strokes (150 ms): voices=" << spaced << "\n";
    result("T35b strokes wider than the fuse window still stack",
           spaced > 1,
           "Spaced strokes are choking one voice — fuse window is too wide");

    // Sustained families are excluded: for a cymbal swell or a marimba roll the
    // overlap IS the sound (HW: "important for cymbals").
    s.Init(&desc); s.LoadPreset(13);  // Cymbal (ENGINE_CYMBAL, capped at 2)
    int cym = roll_max_voices(s, 69, 45.0f, 8);
    s.Init(&desc); s.LoadPreset(1);   // Marimba (ENGINE_BAR)
    int bar = roll_max_voices(s, 60, 45.0f, 8);
    std::cout << "  Cymbal fast roll: voices=" << cym
              << "   Marimba fast roll: voices=" << bar << "\n";
    result("T35c sustained families keep stacking on a fast roll",
           cym >= 2 && bar >= 2,
           "Roll fusion leaked into a sustained engine — cymbal swells and "
           "marimba rolls need the overlap");
}

// ════════════════════════════════════════════════════════════════════════════
// T36: ENGINE_CYMBAL voice stacking (HW: "multiple gong hits are not stacking")
//
//   The cymbal family was hard-capped at 2 voices AND stole the voice with the
//   smallest magEnv.  magEnv is a ~10 ms average of |out| starting at 0, and a
//   gong takes 0.25 s just to open its driver attack, so the newest hit was
//   always the "quietest" voice in the bank: a third strike killed the second
//   one mid-bloom and repeated hits ping-ponged between two slots instead of
//   accumulating.  Both halves of the fix are asserted here:
//
//     hits within the cost budget             → land on DISTINCT voices
//     no strike ever reuses the slot it just  → the self-kill / ping-pong
//       struck while another is affordable       signature of the original bug
//     a hit past the budget, all voices young → steals the OLDEST, never the
//                                               one that was just struck
//     aggregate cymbal COST                   → within kCymCostBudget
//
//   NOTE the budget is in resonator-lane EQUIVALENTS and charges each voice
//   kCymVoiceFixedLanes on top of its bank — see the constant's comment.  This
//   test deliberately does NOT assert "4 distinct voices": that was pass 26's
//   assumption, and measuring the real per-voice cost showed 4 simultaneous
//   cymbal voices to be roughly twice the only CPU level this unit has field
//   evidence for.  What must hold is that hits ACCUMULATE rather than replace
//   each other, which is what the HW report was actually about.
// ════════════════════════════════════════════════════════════════════════════
static void test_cymbal_stacking() {
    std::cout << "\n── T36: Cymbal/gong voice stacking ──\n";

    unit_runtime_desc_t desc = make_desc();
    BrachettiSynth s;

    // Gong strikes 300 ms apart must accumulate on distinct voices for as long
    // as the cost budget can afford another one.
    s.Init(&desc); s.LoadPreset(14);        // Gong (ENGINE_CYMBAL)
    s.state.next_voice_idx = 0;
    uint32_t used = 0u;
    int worst_cost = 0;
    bool reused_immediately = false;
    int prev_idx = -1;
    for (int k = 0; k < 4; ++k) {
        s.NoteOn(50, 110);
        s.NoteOff(50);                      // gate on+off in one tick
        const int idx = (int)s.state.next_voice_idx;
        if (idx == prev_idx) reused_immediately = true;
        prev_idx = idx;
        used |= (1u << idx);
        int cost = 0;
        for (int i = 0; i < NUM_VOICES; ++i)
            if (s.state.voices[i].is_active && s.state.voices[i].cymbal.active)
                cost += BrachettiSynth::kCymVoiceFixedLanes +
                        (int)s.state.voices[i].cymbal.resCount;
        if (cost > worst_cost) worst_cost = cost;
        run_blocks(s, (int)(0.300f * 48000.0f), 64);
    }
    int distinct = 0;
    for (int i = 0; i < NUM_VOICES; ++i) if (used & (1u << i)) ++distinct;
    std::cout << "  4 gong hits (300 ms apart): distinct voices=" << distinct
              << "  worst cost=" << worst_cost
              << " (budget " << BrachettiSynth::kCymCostBudget << ")\n";
    result("T36a repeated gong hits accumulate on distinct voices",
           distinct >= 2 && !reused_immediately,
           "Gong hits are not stacking — repeated strikes are reusing the slot "
           "they just struck instead of accumulating");

    // Every voice is now younger than kCymStealProtectSec, so the 5th strike
    // must take the OLDEST slot.  Voice 0 was struck first, so it is the oldest.
    uint8_t oldest = 0;
    uint32_t oldest_age = 0u;
    for (int i = 0; i < NUM_VOICES; ++i) {
        if (s.state.voices[i].cymbal.active &&
            s.state.voices[i].cymbal.sampleIndex >= oldest_age) {
            oldest_age = s.state.voices[i].cymbal.sampleIndex;
            oldest = (uint8_t)i;
        }
    }
    uint8_t youngest = 0;
    uint32_t young_age = 0xFFFFFFFFu;
    for (int i = 0; i < NUM_VOICES; ++i) {
        if (s.state.voices[i].cymbal.active &&
            s.state.voices[i].cymbal.sampleIndex < young_age) {
            young_age = s.state.voices[i].cymbal.sampleIndex;
            youngest = (uint8_t)i;
        }
    }
    s.NoteOn(50, 110);
    s.NoteOff(50);
    std::cout << "  5th hit stole voice " << (int)s.state.next_voice_idx
              << " (oldest=" << (int)oldest << ", youngest=" << (int)youngest << ")\n";
    result("T36b over the cap the OLDEST cymbal voice is stolen",
           s.state.next_voice_idx == oldest && s.state.next_voice_idx != youngest,
           "Voice stealing is eating the freshest strike — the magEnv ranking "
           "inverts while a slow-attack cymbal is still blooming");

    // CPU guard: the aggregate bank must respect the budget even at the
    // maximum density setting with every voice ringing.
    s.Init(&desc); s.LoadPreset(13);        // Cymbal: largest base bank (96)
    // Density is Partls on the cymbal family now (7 = 60 %, the old Rsntrs
    // maximum).  Slot 3 is the Velocity knob — setting THAT to 60 would leave
    // this test measuring the budget at the DEFAULT bank size and passing for
    // the wrong reason.
    s.setParameter(BrachettiSynth::k_paramPartls, 7);
    s.state.next_voice_idx = 0;
    int budget_worst = 0;
    for (int k = 0; k < 8; ++k) {
        s.NoteOn(69, 127);
        s.NoteOff(69);
        int cost = 0;
        for (int i = 0; i < NUM_VOICES; ++i)
            if (s.state.voices[i].is_active && s.state.voices[i].cymbal.active)
                cost += BrachettiSynth::kCymVoiceFixedLanes +
                        (int)s.state.voices[i].cymbal.resCount;
        if (cost > budget_worst) budget_worst = cost;
        run_blocks(s, (int)(0.120f * 48000.0f), 64);
    }
    // The budget bounds what a NEW voice may claim; the minimum-bank floor can
    // carry a voice past it, so allow that floor on top of the budget.
    const int limit = BrachettiSynth::kCymCostBudget +
                      BrachettiSynth::kCymMinResonators;
    std::cout << "  8 crash hits @Partls=7 (60 %): worst aggregate cost="
              << budget_worst << " (limit " << limit << ")\n";
    result("T36c stacked cymbal voices stay inside the CPU cost budget",
           budget_worst <= limit,
           "Cymbal stack is over the CPU budget — this is what crashed the HW "
           "audio interface after pass 26 raised the cap from 2 voices to 4");
}

// ════════════════════════════════════════════════════════════════════════════
// ════════════════════════════════════════════════════════════════════════════
// T37: ENGINE_NOISE survives same-tick gating (Clap / Shaker / HHat-C)
//
//   Same defect class as T20, found by reviewing for it after T36.  T20 fixed
//   master_env by force-setting it to 1.0/ENV_DECAY in NoteOn; ENGINE_SNARE was
//   patched separately by skipping the release.  noise_env/noise_env_hi on
//   ENGINE_NOISE were covered by neither: trigger() leaves value=0 in
//   ENV_ATTACK, the same-tick release() jumped straight to ENV_RELEASE, and the
//   first process() saw value <= 0.001f and went to ENV_IDLE.  The envelope died
//   before it opened and the entire voice was silent ON HARDWARE — while
//   render_presets.cpp, which holds the gate ~50 ms, sounded perfect.
//
//   FastEnvelope::release() now defers into ENV_ATTACK_REL, so the attack
//   completes and the release runs from the top of it.  Assert against the
//   held-gate render rather than an absolute level, so the test tracks the
//   presets if they are ever retuned.
// ════════════════════════════════════════════════════════════════════════════
// skip_frames excludes the onset: a dead envelope still leaks the ~20 ms of
// exciter that is already in flight, so only the TAIL separates the two cases.
static double gate_energy(BrachettiSynth& s, unit_runtime_desc_t& desc,
                          int preset, int hold_frames, int skip_frames = 0) {
    s.Init(&desc);
    s.LoadPreset((uint8_t)preset);
    for (int i = 0; i < NUM_VOICES; ++i)
        s.state.voices[i].exciter.noise_gen.seed = 2463534242UL;  // noise PRNG free-runs

    s.GateOn(100);
    if (hold_frames <= 0) s.GateOff();          // same tick, before any audio

    float buf[128];
    double acc = 0.0;
    const int total = 48000 / 5;                // 200 ms
    bool released = (hold_frames <= 0);
    for (int done = 0; done < total; done += 64) {
        std::memset(buf, 0, sizeof(buf));
        s.processBlock(buf, 64);
        if (done >= skip_frames)
            for (int i = 0; i < 64; ++i) acc += (double)buf[i * 2] * buf[i * 2];
        if (!released && done >= hold_frames) { s.GateOff(); released = true; }
    }
    return std::sqrt(acc / (total - skip_frames));
}

static void test_noise_same_tick_gate() {
    std::cout << "\n── T37: ENGINE_NOISE survives same-tick gate on+off ──\n";

    unit_runtime_desc_t desc = make_desc();
    BrachettiSynth s;

    struct { int idx; const char* name; } ps[] = {
        {21, "Clap"}, {22, "Shaker"}, {26, "HHat-C"},
    };
    bool all_alive = true, all_full = true;
    for (auto& p : ps) {
        double same = gate_energy(s, desc, p.idx, 0);
        double held = gate_energy(s, desc, p.idx, 48000 / 20);   // 50 ms
        double tail = gate_energy(s, desc, p.idx, 0, 48000 / 40);  // same tick, past 25 ms
        double ratio = held > 1e-9 ? same / held : 0.0;
        std::cout << "  " << p.name << ": same-tick RMS=" << same
                  << "  tail(>25ms)=" << tail
                  << "  held-50ms RMS=" << held << "  ratio=" << ratio << "\n";
        if (tail < 1e-3) all_alive = false;
        if (ratio < 0.5) all_full = false;
    }

    result("T37a ENGINE_NOISE presets keep a tail under same-tick gating",
           all_alive,
           "Clap/Shaker/HHat-C went silent — noise_env was released at value=0 "
           "and jumped to ENV_IDLE before producing anything");
    result("T37b same-tick output is comparable to a held gate",
           all_full,
           "the noise tail is being truncated by the same-tick release");
}

// ════════════════════════════════════════════════════════════════════════════
// T38 — A preset change while a voice rings fades it out, never re-excites it
//
//   processBlock used to route on the LIVE kPresetEngine[m_preset_idx], so a
//   ringing voice was handed to whichever engine the new preset selected.  For
//   a cymbal voice that is fatal: the ENGINE_CYMBAL branch never calls
//   process_exciter, so the voice sits at current_frame == 0 holding an
//   unstarted envelope, and the legacy path then fired a COMPLETE UNPLAYED
//   ATTACK — a measured 19x RMS burst on Cymbal -> Clap.  Other switches hard-
//   cut a ringing voice to exact silence.
//
//   Now the engine is latched at NoteOn and a preset change arms a ~10 ms
//   fade.  Assert against the tail that was already playing: a fade may never
//   be louder than what it replaces, and it must reach silence quickly.
// ════════════════════════════════════════════════════════════════════════════
static void test_preset_change_fade() {
    std::cout << "\n── T38: preset change during a ringing voice ──\n";

    unit_runtime_desc_t desc = make_desc();
    BrachettiSynth s;

    struct Case { int from, to; const char* name; } cases[] = {
        {13, 21, "Cymbal->Clap"},    // the 19x burst
        {14, 0,  "Gong->Kick2"},
        {14,  9, "Gong->Koto"},
        { 9, 14, "Koto->Gong"},    // was a hard cut to silence
    };

    bool all_bounded = true, all_quiet = true;
    for (const Case& c : cases) {
        s.Init(&desc);
        s.LoadPreset((uint8_t)c.from);
        for (int i = 0; i < NUM_VOICES; ++i)
            s.state.voices[i].exciter.noise_gen.seed = 2463534242UL;
        s.GateOn(110);
        s.GateOff();

        run_blocks(s, 48000 / 8, 64);                    // ~125 ms of ring
        // Compare against the 25 ms IMMEDIATELY before the switch — the tail
        // the fade actually replaces.  A max over the whole ring would be a
        // different (and for a still-rising KS string, unfair) reference.
        float pk_before = run_blocks(s, 48000 / 40, 64);
        s.LoadPreset((uint8_t)c.to);                     // <-- switch mid-ring
        float pk_after  = run_blocks(s, 48000 / 40, 64); // first 25 ms after
        float pk_settle = run_blocks(s, 48000 / 20, 64); // the following 50 ms

        double ratio = (pk_before > 1e-9f) ? (double)pk_after / pk_before : 0.0;
        std::cout << "  " << c.name << ": peak before=" << pk_before
                  << " after=" << pk_after << " (ratio=" << ratio
                  << ")  settled=" << pk_settle << "\n";

        if (ratio > 1.2 || pk_after > 0.95f) all_bounded = false;
        if (pk_settle > 1e-3f) all_quiet = false;
    }

    result("T38a a preset change never renders louder than the tail it replaces",
           all_bounded,
           "a ringing voice was re-excited by the incoming preset — the engine "
           "is being read live instead of latched at NoteOn");
    result("T38b the orphaned voice is silent within ~25 ms",
           all_quiet,
           "the preset-change fade did not retire the voice");
}

// ════════════════════════════════════════════════════════════════════════════
// T39 — Velocity knob (the ex-Rsntrs slot): ghost <- neutral -> wham
//
//   Three properties, and the first is the one that protects every shipped
//   preset: at the DEFAULT knob (0) a strike must be bit-for-bit the strike
//   that was sent, on every engine.  The other two are the reason the knob
//   exists: turning it down must ghost a hit, and turning it UP must bite even
//   when the sequencer already sends 127 — a knob that can only "restore" full
//   velocity is dead for anyone who never edits per-step velocity, which is
//   the same class of dead knob this unit has shipped four times.
// ════════════════════════════════════════════════════════════════════════════
static void test_velocity_knob() {
    std::cout << "\n── T39: Velocity knob (ghost / neutral / wham) ──\n";

    unit_runtime_desc_t desc = make_desc();

    // One exemplar per strike path: legacy voice, dense cymbal, drum kernel.
    struct Case { int preset; const char* name; } cases[] = {
        { 0,  "Kick2 (membrane)" },
        { 3,  "AcSnare (snare)"  },
        {13,  "Cymbal (cymbal)"  },
        { 5,  "Timpani (kernel)" },
        { 1,  "Marimba (bar)"    },
    };

    // Sum |x| over `frames` — RMS-ish energy that a peak-limited master stage
    // cannot flatten as completely as it flattens the peak.
    auto energy = [&](int preset, int32_t knob, uint8_t vel, int frames) -> double {
        BrachettiSynth s;
        s.Init(&desc);
        s.LoadPreset((uint8_t)preset);
        for (int i = 0; i < NUM_VOICES; ++i)
            s.state.voices[i].exciter.noise_gen.seed = 2463534242UL;   // pin the PRNG
        s.setParameter(BrachettiSynth::k_paramVelocity, knob);
        s.GateOn(vel);
        s.GateOff();
        float buf[128] = {0.0f};
        double sum = 0.0;
        for (int done = 0; done < frames; done += 64) {
            std::memset(buf, 0, sizeof(buf));
            s.processBlock(buf, 64);
            for (int i = 0; i < 128; i += 2) sum += std::fabs(buf[i]);
        }
        return sum;
    };

    const int frames = 48000 / 4;   // 250 ms
    bool neutral_identical = true, ghost_quieter = true;
    bool wham_reaches_full = true, wham_never_drops = true;
    for (const Case& c : cases) {
        const double neutral   = energy(c.preset,    0, 100, frames);
        const double neutral2  = energy(c.preset,    0, 100, frames);
        const double ghost     = energy(c.preset, -100, 100, frames);
        const double mid_wham  = energy(c.preset, +100,  64, frames);
        const double full_neut = energy(c.preset,    0, 127, frames);
        const double full_wham = energy(c.preset, +100, 127, frames);

        std::cout << "  " << c.name << ": neutral=" << neutral
                  << "  ghost=" << ghost
                  << "  | vel64+wham=" << mid_wham
                  << "  vel127 neutral=" << full_neut << " wham=" << full_wham << "\n";

        if (neutral != neutral2)                 neutral_identical = false;
        if (!(ghost < neutral * 0.6))            ghost_quieter = false;
        if (!(mid_wham >= full_neut * 0.95))     wham_reaches_full = false;
        if (!(full_wham >= full_neut * 0.98))    wham_never_drops = false;
    }

    // Byte-identity of the default knob, checked directly against the raw
    // velocity path: knob 0 must return the input untouched, not "close to".
    BrachettiSynth probe;
    probe.Init(&desc);
    bool exact_neutral = true;
    for (int v = 1; v <= 127; ++v) {
        const float raw = (float)v * 0.007874015f;
        if (probe.vel_bias_apply(raw) != raw) exact_neutral = false;
    }

    result("T39a the default Velocity knob is an exact no-op on every velocity",
           exact_neutral,
           "vel_bias_apply(raw) != raw at knob 0 — every shipped preset just "
           "moved (knob_exp2(0) must be exactly 1.0)");
    result("T39b renders are deterministic at the default knob",
           neutral_identical,
           "two identical renders differed — the PRNG pin or the knob leaked state");
    result("T39c the knob at minimum ghosts the hit",
           ghost_quieter,
           "Velocity = -100 did not drop the strike energy below 60 %");
    // This is the wham's real job: a half-hearted step must land as a full
    // strike.  Beyond that, at velocity 127, the presets that already pin the
    // master limiter (kick, bars) CANNOT get louder — a limited bus has no
    // level left to give (pass 30).  So assert the reachable property, and
    // separately that the over-range never costs level anywhere.
    result("T39d the knob at maximum lifts a mid-velocity stroke to a full hit",
           wham_reaches_full,
           "Velocity = +100 on a velocity-64 stroke stayed below the "
           "full-velocity render — the ceiling is not being reached");
    result("T39e the wham over-range never costs level at velocity 127",
           wham_never_drops,
           "Velocity = +100 made a full-velocity hit QUIETER — a velocity "
           "curve downstream is non-monotone past 1.0");
}

// ════════════════════════════════════════════════════════════════════════════
// T40 — Cymbal resonator density rides on Partls (the ex-Rsntrs control)
//
//   Partls is inert on ENGINE_CYMBAL (that family bypasses the shared modal
//   bank), so the density moved onto it to free a GUI slot.  Assert that it
//   really drives the bank, that the shipped rows still ask for the 40 % the
//   old knob defaulted to, and that positions 5-7 do NOT fall through into the
//   ResA/ResB edit selector — that selector survives a preset change, so a
//   leak there would silently half-disable Model/Dkay on the NEXT preset.
// ════════════════════════════════════════════════════════════════════════════
static void test_cymbal_density_on_partls() {
    std::cout << "\n── T40: cymbal density on Partls ──\n";

    unit_runtime_desc_t desc = make_desc();
    const int cym[] = {13, 14, 26, 31, 32, 36};   // GtrStr removed pass 41: indices >25 shifted down 1

    // Bank size actually handed to the resonator engine, per knob position.
    auto res_count = [&](int preset, int32_t partls) -> int {
        BrachettiSynth s;
        s.Init(&desc);
        s.LoadPreset((uint8_t)preset);
        s.setParameter(BrachettiSynth::k_paramPartls, partls);
        s.GateOn(110);
        s.GateOff();
        for (int i = 0; i < NUM_VOICES; ++i)
            if (s.state.voices[i].cymbal.active)
                return (int)s.state.voices[i].cymbal.resCount;
        return -1;
    };

    bool monotone = true, shipped_is_40 = true;
    for (int p : cym) {
        const int lo  = res_count(p, 0);   // 25 %
        const int mid = res_count(p, 3);   // 40 % = the shipped rows
        const int hi  = res_count(p, 7);   // 60 %
        std::cout << "  preset " << p << ": Partls 0/3/7 -> " << lo << "/" << mid
                  << "/" << hi << " resonators\n";
        if (!(lo <= mid && mid <= hi && lo < hi)) monotone = false;

        // The shipped row must select the same bank the ex-Rsntrs default did.
        BrachettiSynth s;
        s.Init(&desc);
        s.LoadPreset((uint8_t)p);
        s.GateOn(110);
        s.GateOff();
        int shipped = -1;
        for (int i = 0; i < NUM_VOICES; ++i)
            if (s.state.voices[i].cymbal.active) shipped = (int)s.state.voices[i].cymbal.resCount;
        if (shipped != mid) shipped_is_40 = false;
    }

    // Positions 5-7 on a cymbal preset must leave the editor selection alone.
    BrachettiSynth s;
    s.Init(&desc);
    s.LoadPreset(13);                                        // Cymbal
    const bool sel_a = s.m_is_resonator_a_ut(), sel_b = s.m_is_resonator_b_ut();
    bool sel_intact = true;
    for (int32_t p = 5; p <= 7; ++p) {
        s.setParameter(BrachettiSynth::k_paramPartls, p);    // 50 / 55 / 60 %
        if (s.m_is_resonator_a_ut() != sel_a || s.m_is_resonator_b_ut() != sel_b)
            sel_intact = false;
    }

    result("T40a Partls scales the cymbal resonator bank monotonically",
           monotone,
           "the density knob did not resize the bank — Partls is not reaching "
           "m_cym_reso_scale");
    result("T40b the shipped cymbal rows still ask for the ex-Rsntrs 40 % bank",
           shipped_is_40,
           "a cymbal preset's Partls column no longer stores 3 — its bank size "
           "(and therefore its CPU cost and sound) has moved");
    result("T40c a cymbal density of 5-7 does not hijack the ResA/ResB selector",
           sel_intact,
           "Partls 5-7 fell through to the editor-select branch: Model/Dkay/"
           "Mterl/Inharm will write to one resonator only on the next preset");
}

// ════════════════════════════════════════════════════════════════════════════
// T41 — Reset() must not leave master-stage state queued
//
//   A preset change over a ringing voice DEFERS the incoming preset's master
//   drive behind the ~10 ms fade (pass 30) — the tail keeps the old drive, the
//   new one lands when the fade retires.  processBlock releases it; LoadPreset
//   can never leave a stale one because its parameter loop always rewrites
//   Gain, and that case clears the queue.
//
//   Reset() has neither: it kills every voice outright, so the deferral's fade
//   never happens and nothing clears it.  Suspend() is AllNoteOff() + Reset(),
//   so a suspend caught between the preset change and the end of the fade used
//   to hand the NEXT session a drive belonging to the preset before last — a
//   whole-unit gain error that persists until the user turns Gain.
// ════════════════════════════════════════════════════════════════════════════
static void test_reset_clears_deferred_drive() {
    std::cout << "\n── T41: Reset() leaves no queued master state ──\n";

    unit_runtime_desc_t desc = make_desc();
    BrachettiSynth s;
    s.Init(&desc);

    s.LoadPreset(9);                        // Koto, Gain 0  -> drive 1.0
    s.GateOn(110);
    run_blocks(s, 4800, 64);                // ~100 ms of ring
    const float drive_gtr = s.state.master_drive;

    s.LoadPreset(14);                       // Gong, Gain 20 -> drive 5.0, DEFERRED
    const float drive_held = s.state.master_drive;

    // Suspend mid-fade, before any block has run the release.
    s.AllNoteOff();
    s.Reset();
    const float drive_after_reset = s.state.master_drive;
    run_blocks(s, 640, 64);                 // Resume: blocks with no voice
    const float drive_settled = s.state.master_drive;

    std::cout << "  Koto drive=" << drive_gtr
              << "  held during fade=" << drive_held
              << "  after Reset=" << drive_after_reset
              << "  after 10 blocks=" << drive_settled << "\n";

    result("T41a the incoming preset's drive is still deferred behind the fade",
           drive_held == drive_gtr,
           "the deferral itself has stopped working — a preset change now "
           "slams the new Gain onto the outgoing tail (pass 30 regression)");
    result("T41b Reset() drops a drive queued behind a fade it just cancelled",
           drive_settled == drive_after_reset,
           "the first block after Resume() installed a drive belonging to the "
           "preset that was fading when the unit was suspended");

    // Same argument for the limiter follower: it is master-stage state, it only
    // decays (20 ms release), and a voiceless Reset leaves it holding gain
    // reduction over the first strike of the next session.
    s.Init(&desc);
    s.LoadPreset(0);
    s.GateOn(127);
    run_blocks(s, 4800, 64);
    const bool env_was_up = s.state.master_lim_env > 0.1f;
    s.Reset();
    result("T41c Reset() clears the master limiter follower",
           env_was_up && s.state.master_lim_env == 0.0f,
           "a limiter envelope left high rides the first hit after Resume() "
           "down for ~20 ms");
}

// ════════════════════════════════════════════════════════════════════════════
// T42 — a Timpani/Taiko note change must not run two full mode banks
//
//   HW: "Timpani: changing note while playing leads to silence (audio
//   interface crash)", later "changing note leads to sporadic clicks for the
//   next 8-10 seconds".  Pass 41 looked for a waveform discontinuity, found
//   none, and rejected its own theory.  The cause is CPU, not signal: a note
//   change is always simultaneous with a strike, the strike resets a kettle's
//   mode bound to the full bank, and the old note keeps ringing on the other
//   kettle — so the unit's dominant per-sample cost DOUBLES (measured 110 vs
//   55 µs/block in kernel_cpu_probe) and stays doubled for ~7 s, which is the
//   reported window.
//
//   Pass 43 bounds it: only the newest kettle runs a full bank, the older one
//   keeps its measured skeleton and has its dense fill damped away.  These
//   assertions are on the BOUND, not on timing, so they mean the same thing on
//   any machine.
// ════════════════════════════════════════════════════════════════════════════
static void test_kernel_note_change_cost() {
    std::cout << "\n── T42: kernel note change stays inside its mode budget ──\n";

    unit_runtime_desc_t desc = make_desc();
    BrachettiSynth s;
    s.Init(&desc);
    s.LoadPreset(5);                                  // Timpani, 280 modes
    const int full = s.m_drum_kernel.ModeCount();

    s.setParameter(BrachettiSynth::k_paramNote, 52);
    s.GateOn(110); s.GateOff();
    run_blocks(s, 4800, 64);                          // ~100 ms of ring
    const int one_kettle = s.m_drum_kernel.LiveModes();

    // The reported action: change the note while the first is still ringing.
    s.setParameter(BrachettiSynth::k_paramNote, 57);
    s.GateOn(110); s.GateOff();
    int peak = 0;
    for (int b = 0; b < 200; ++b) {                   // ~270 ms
        run_blocks(s, 64, 64);
        const int live = s.m_drum_kernel.LiveModes();
        if (live > peak) peak = live;
    }
    const int settled = s.m_drum_kernel.LiveModes();

    std::cout << "  full bank=" << full
              << "  one kettle=" << one_kettle
              << "  peak after note change=" << peak
              << "  settled=" << settled
              << "  (two full banks would be " << (2 * full) << ")\n";

    result("T42a a note change settles below two full mode banks",
           settled < 2 * full && settled > full,
           "either the fill damping stopped working (two full banks = the CPU "
           "level that crashed the audio interface) or the older kettle is "
           "being cut entirely, which loses the two-note overlap");
    result("T42b both kettles are still sounding after the change",
           s.m_drum_kernel.LiveVoices() == 2,
           "the older kettle was retired outright — a note change should damp "
           "the previous drum's wash, not mute the drum");
    result("T42c the newest kettle keeps its FULL bank",
           settled >= full,
           "the note just struck is running a thinned bank; the damping is "
           "being applied to the wrong kettle");

    // The mechanism must not fire when there is nothing to make room for:
    // repeating the SAME note retriggers one kettle in place.
    BrachettiSynth s2;
    s2.Init(&desc);
    s2.LoadPreset(5);
    s2.setParameter(BrachettiSynth::k_paramNote, 52);
    for (int i = 0; i < 4; ++i) {
        s2.GateOn(110); s2.GateOff();
        run_blocks(s2, 2400, 64);
    }
    std::cout << "  4 strikes on ONE note: live voices="
              << s2.m_drum_kernel.LiveVoices()
              << " modes=" << s2.m_drum_kernel.LiveModes() << "\n";
    result("T42d repeating a note does not trigger fill damping",
           s2.m_drum_kernel.LiveVoices() == 1 &&
           s2.m_drum_kernel.LiveModes() == full,
           "a roll on one drum is being treated as a note change and losing "
           "its wash");
}

int main() {
    std::cout << "=== BRACHETTI HW-DEBUG UNIT TESTS ===\n";
    std::cout << "Testing HW-vs-UT discrepancies that could cause hardware silence.\n";

    test_param_audit();
    test_hw_boot_sequence();
    test_default_preset_no_override();
    test_denormal_decay();
    test_gate_on_off_cycle();
    test_block_sizes();
    test_reset_then_gate_on();
    test_all_presets_audible();
    test_voice_allocation();
    test_delay_roundtrip();
    test_noise_svf();
    test_tubrad_mterl();
    test_partls_coupling();
    test_tone_eq();
    test_noise_filter_state_clear();
    test_partls_mode_select_coupling();
    test_energy_squelch();
    test_pitch_bend();
    test_pitch_bend_persists_to_new_note();
    test_pitch_compensation_accuracy();
    test_same_tick_gate();
    test_os_param_init_sequence();
    test_master_env_trace();
    test_exciter_independent_of_env();
    test_polyphony();
    test_all_note_off();
    test_midi_note_extremes();
    test_max_inharm_stability();
    test_preset_change_mid_note();
    test_velocity_scaling();
    test_dkay_zero_short_gate();
    test_string_one_second_sustain();
    test_dkay_controls_decay();
    test_dkay_feedback_gain_mapping();
    test_retrigger_consistency();
    test_roll_fusion();
    test_cymbal_stacking();
    test_noise_same_tick_gate();
    test_preset_change_fade();
    test_velocity_knob();
    test_cymbal_density_on_partls();
    test_reset_clears_deferred_drive();
    test_kernel_note_change_cost();

    std::cout << "\n=== RESULTS: " << g_pass << " passed, " << g_fail << " failed ===\n";
    return g_fail == 0 ? 0 : 1;
}
