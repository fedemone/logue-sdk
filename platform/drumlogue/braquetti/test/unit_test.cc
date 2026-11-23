// Braquetti, the whole unit through its drumlogue callbacks.
//
// This file includes catalog.h too, which gives it a second, independent set
// of Brachetti's family engines (they are file-scope statics): the
// reference.  A lane must play an instrument exactly as that engine plays it
// when struck by a gate, scaled by the lane's level.
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#include "unit.h"
#include "../catalog.h"
#include "../seq_core.h"

int braquetti_test_live_voices();   // unit.cc, BRAQUETTI_HOST_TEST
int braquetti_test_load();
int braquetti_test_budget();

using namespace brq;

static int s_fail = 0;
#define CHECK(cond, ...) do { if (!(cond)) { ++s_fail; std::printf("FAIL %s:%d  ", __FILE__, __LINE__); \
    std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)

static unit_runtime_desc_t s_desc;
static int32_t s_panel[kNumParams];

static void sleep_ms(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }

// A knob turn as the panel makes it: its own number plus clicks.  Knobs are
// turned 10 ms apart, so no three of them make a burst (seq_core.h).
static void turn(uint8_t id, int clicks) {
    sleep_ms(10);
    while (clicks) {
        const int c = clicks > 16 ? 16 : clicks < -16 ? -16 : clicks;
        s_panel[id] += c;
        unit_set_param_value(id, s_panel[id]);
        clicks -= c;
        if (clicks) sleep_ms(10);
    }
}
static void set_abs(uint8_t id, int32_t v) {
    sleep_ms(10);
    s_panel[id] = v;
    unit_set_param_value(id, v);
}
static std::string text(uint8_t id) {
    const char* s = unit_get_param_str_value(id, s_panel[id]);
    return s ? s : "(null)";
}

static std::vector<float> render(int blocks) {
    std::vector<float> v(size_t(blocks) * 128);
    for (int b = 0; b < blocks; ++b) unit_render(nullptr, v.data() + size_t(b) * 128, 64);
    return v;
}

// The reference: one family engine of this file's own pool, struck as
// Brachetti's gate strikes it, with the same history as the unit's engine:
// initialised once, reset before each strike (as unit_reset does).  A reset
// does not rewind an engine's voice rotation or a voice's noise, so only an
// engine with the same history renders the same samples.
static bool s_ref_ready[brachetti::kNumFamilies];
static std::vector<float> reference(int family, int program, uint8_t velocity, int blocks) {
    const brachetti::Family& f = brachetti::kFamilies[family];
    if (!s_ref_ready[family]) {
        f.set_header(f.engine, f.hdr);
        f.init(f.engine, &s_desc);
        s_ref_ready[family] = true;
    }
    f.reset(f.engine);
    f.set(f.engine, f.slot[brc::CommonLayout::k_paramProgram], program);
    f.gate_on(f.engine, velocity);
    std::vector<float> v(size_t(blocks) * 128);
    for (int b = 0; b < blocks; ++b) f.render(f.engine, v.data() + size_t(b) * 128, 64);
    return v;
}

// out must be gain x ref wherever that is below the limiter's knee, and
// within +-1 everywhere.  On the host: sample for sample.  Built for ARM with
// -ffast-math (BRQ_TOLERANCE_DB), two source files may round a float
// differently, so the render's difference must stay that far below its
// signal, as tools/brachetti_equiv/arm_brachetti_check.sh judges Brachetti.
static void check_scaled(const std::vector<float>& out, const std::vector<float>& ref, float gain,
                         const char* what) {
    int diff = 0, compared = 0;
    float peak = 0.0f;
    double err2 = 0.0, sig2 = 0.0;
    for (size_t k = 0; k < out.size(); ++k) {
        const float want = gain * ref[k];
        if (std::fabs(ref[k]) > peak) peak = std::fabs(ref[k]);
        if (!(std::fabs(out[k]) <= 1.0f)) ++diff;
        if (std::fabs(want) > 0.9f) continue;
        ++compared;
        err2 += double(out[k] - want) * double(out[k] - want);
        sig2 += double(want) * double(want);
#ifndef BRQ_TOLERANCE_DB
        if (out[k] != want) ++diff;
#endif
    }
    CHECK(peak > 0.05f, "%s: the reference sounds (peak %.3f)", what, peak);
#ifdef BRQ_TOLERANCE_DB
    const double db = err2 > 0.0 ? 10.0 * std::log10(err2 / sig2) : -400.0;
    CHECK(diff == 0 && db <= BRQ_TOLERANCE_DB, "%s: difference %.1f dB below the signal (limit %d), %d out of range",
          what, -db, -(BRQ_TOLERANCE_DB), diff);
#else
    (void)err2; (void)sig2;
    CHECK(diff == 0, "%s: %d of %d samples differ from %.2f x the engine", what, diff, compared, gain);
#endif
    if (diff && std::getenv("BRQ_DEBUG"))
        for (size_t k = 0, shown = 0; k < out.size() && shown < 6; ++k)
            if (std::fabs(gain * ref[k]) <= 0.9f && out[k] != gain * ref[k]) {
                std::printf("  sample %zu (frame %zu): out %.6f, %.2f x ref %.6f, ratio out/ref %.4f\n", k, k / 2,
                            out[k], gain, gain * ref[k], ref[k] != 0.0f ? out[k] / ref[k] : 0.0f);
                ++shown;
            }
}

static bool silent(const std::vector<float>& v) {
    for (float x : v) if (x != 0.0f) return false;
    return true;
}

int main() {
    std::memset(&s_desc, 0, sizeof(s_desc));
    s_desc.target = unit_header.target;
    s_desc.api = UNIT_API_VERSION;
    s_desc.samplerate = 48000;
    s_desc.frames_per_buffer = 64;
    s_desc.input_channels = 2;
    s_desc.output_channels = 2;
    CHECK(unit_init(&s_desc) == k_unit_err_none, "unit_init");
    for (int i = 0; i < kNumParams; ++i) s_panel[i] = unit_header.params[i].init;

    // ── The panel ───────────────────────────────────────────────────────────
    CHECK(std::strcmp(unit_header.name, "Braquetti") == 0, "name");
    CHECK(text(P_LANE) == "Lane A" && text(P_FAMILY) == "Kick" && text(P_LEVEL) == "80%" &&
          text(P_STEP1) == "---", "defaults: %s %s %s %s", text(P_LANE).c_str(), text(P_FAMILY).c_str(),
          text(P_LEVEL).c_str(), text(P_STEP1).c_str());
    for (int i = 0; i < kNumParams; ++i) {
        const unit_param_t& p = unit_header.params[i];
        CHECK(p.init >= p.min && p.init <= p.max, "P%02d default inside its range", i);
        CHECK((p.type == k_unit_param_type_bitmaps) == (i == P_OVERVIEW), "P%02d bitmap only for Overview", i);
    }
    CHECK(unit_get_param_bmp_value(P_OVERVIEW, 0) != nullptr, "Overview draws");

    // ── Empty lanes: a gate plays nothing ───────────────────────────────────
    unit_gate_on(64);
    CHECK(silent(render(20)), "empty lanes are silent");

    // ── One lane, one instrument: exactly the engine, at the lane's level ───
    turn(P_STEP1, +1);   // lane A step 1: Kick2
    CHECK(text(P_STEP1) == "Kick2", "step 1: %s", text(P_STEP1).c_str());
    unit_reset();
    unit_gate_on(64);
    check_scaled(render(60), reference(0, 0, 64, 60), 0.8f, "Kick2 at 80%");

    // An accent (step 1's bar mark) strikes as a normal trig.
    unit_reset();
    unit_gate_on(127);
    check_scaled(render(60), reference(0, 0, 64, 60), 0.8f, "Kick2 on an accent");

    // The lane's level.
    turn(P_LEVEL, -30);
    CHECK(text(P_LEVEL) == "50%", "level: %s", text(P_LEVEL).c_str());
    unit_reset();
    unit_gate_on(64);
    check_scaled(render(60), reference(0, 0, 64, 60), 0.5f, "Kick2 at 50%");
    turn(P_LEVEL, +30);

    // Reset (sent on stop) silences at once.
    unit_reset();
    unit_gate_on(64);
    render(10);
    unit_reset();
    CHECK(silent(render(4)), "silent right after a reset");

    // ── Two lanes on one family: both sound (its ring-over pair) ────────────
    set_abs(P_LANE, 1);
    turn(P_FAMILY, -3);   // lane B: Snare -> Kick
    CHECK(text(P_FAMILY) == "Kick", "lane B family: %s", text(P_FAMILY).c_str());
    turn(P_STEP1, +2);    // lane B step 1: 808Sub
    CHECK(text(P_STEP1) == "808Sub", "lane B step 1: %s", text(P_STEP1).c_str());
    unit_reset();
    unit_gate_on(64);
    render(4);
    CHECK(braquetti_test_live_voices() == 2, "Kick2 and 808Sub both sound: %d voices",
          braquetti_test_live_voices());
    turn(P_STEP1, -2);    // lane B step 1 off again
    turn(P_FAMILY, +3);   // back to Snare

    // ── A busy pattern: every step on every lane ────────────────────────────
    // Lane A Kick2, lane B AcSnre, lane C HHat-C, on all 16 steps.
    for (int lane = 0; lane < kLanes; ++lane) {
        set_abs(P_LANE, lane);
        const int clicks = lane == 2 ? 3 : 1;
        for (int s = 0; s < kSteps; ++s)
            if (!(lane == 0 && s == 0)) turn(uint8_t(P_STEP1 + s), clicks);
    }
    set_abs(P_LANE, 2);
    CHECK(text(P_STEP16) == "HHat-C", "lane C step 16: %s", text(P_STEP16).c_str());
    // Plays 48 gates; the load (each family's voices at its cost) must stay
    // within the budget right after every strike.
    auto busy = [](const char* what, int min_voices) {
        unit_reset();
        int over = 0, most_load = 0, most = 0;
        bool bounded = true, heard = false;
        for (int g = 0; g < 48; ++g) {
            unit_gate_on(g % 16 == 0 ? 127 : 64);
            std::vector<float> out = render(1);       // the strikes happen in this block
            const int load = braquetti_test_load(), live = braquetti_test_live_voices();
            if (load > braquetti_test_budget()) ++over;
            if (load > most_load) most_load = load;
            if (live > most) most = live;
            const std::vector<float> rest = render(2);
            out.insert(out.end(), rest.begin(), rest.end());
            for (float x : out) {
                if (!(std::fabs(x) <= 1.0f)) bounded = false;
                if (x != 0.0f) heard = true;
            }
            unit_gate_off();
        }
        CHECK(over == 0, "%s: the load stays within the budget %d (most %d)", what, braquetti_test_budget(),
              most_load);
        CHECK(most >= min_voices, "%s: %d+ voices sound together (most %d)", what, min_voices, most);
        CHECK(heard && bounded, "%s: it sounds, every sample finite and within +-1", what);
    };
    busy("Kick2, AcSnre, HHat-C", 3);

    // The heaviest families on every step: Timpani, Cymbal and a Lippa drum.
    // The family changes keep the steps on.
    set_abs(P_LANE, 0);
    turn(P_FAMILY, +2);   // Kick -> Kernel: Timpni
    set_abs(P_LANE, 1);
    turn(P_FAMILY, +3);   // Snare -> Cymbal: Cymbal
    set_abs(P_LANE, 2);
    turn(P_FAMILY, +2);   // Noise -> Lippa: its third, Bendir
    CHECK(text(P_FAMILY) == "Lippa" && text(P_STEP1) == "Bendir", "lane C: %s %s", text(P_FAMILY).c_str(),
          text(P_STEP1).c_str());
    busy("Timpni, Cymbal, Bendir", 1);

    // ── Gates, knobs and audio on three threads ─────────────────────────────
    std::atomic<bool> run{true};
    std::atomic<bool> bad{false};
    std::thread audio([&] {
        float buf[128];
        while (run.load()) {
            unit_render(nullptr, buf, 64);
            for (float x : buf) if (!(std::fabs(x) <= 1.0f)) bad = true;
            std::this_thread::sleep_for(std::chrono::microseconds(300));
        }
    });
    std::thread gates([&] {
        int g = 0;
        while (run.load()) {
            unit_gate_on(g++ % 16 == 0 ? 127 : 64);
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            unit_gate_off();
        }
    });
    for (int i = 0; i < 60; ++i) {
        set_abs(P_LANE, i % 3);
        turn(uint8_t(P_STEP1 + i % 16), (i % 5) - 2);
        if (i % 7 == 0) turn(P_FAMILY, (i % 2) ? 1 : -1);
        if (i % 11 == 0) unit_all_note_off();
    }
    run = false;
    gates.join();
    audio.join();
    CHECK(!bad.load(), "threads: every sample finite and within +-1");

    unit_teardown();
    if (s_fail) { std::printf("%d check(s) FAILED\n", s_fail); return 1; }
    std::printf("unit: all checks passed\n");
    return 0;
}
