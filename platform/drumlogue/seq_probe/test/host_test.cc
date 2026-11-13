// seq_probe host test: drives the unit's callbacks the way the drumlogue
// would — a real-time render thread, panel calls from another, a program
// load, knob turns from a host that keeps its own number and from one that
// takes the unit's answer, gates, stop — and checks the displays and the log.
//
//   ./run_host_test.sh            host build, worker thread
//   NO_THREAD=1 ./run_host_test.sh   the path where no thread can be created
//   ARM=1 ./run_host_test.sh      ARM build under qemu-arm
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>

#include "unit.h"

#ifndef PROBE_DIR
#error "build with -DPROBE_DIR=\"...\""
#endif

static int s_fail = 0;
#define CHECK(cond, ...) do { if (!(cond)) { ++s_fail; std::printf("FAIL %s:%d  ", __FILE__, __LINE__); \
    std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)

static std::string str(uint8_t id, int32_t v = 0) {
    const char* s = unit_get_param_str_value(id, v);
    return s ? s : "(null)";
}

static void sleep_ms(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }

static std::atomic<bool> s_run{true};
static std::atomic<bool> s_bad_sample{false};
static std::atomic<float> s_peak{0.0f};

static void render_thread() {
    float out[64 * 2];
    while (s_run.load()) {
        unit_render(nullptr, out, 64);
        for (float x : out) {
            if (!std::isfinite(x) || std::fabs(x) > 1.0f) s_bad_sample = true;
            if (std::fabs(x) > s_peak.load()) s_peak = std::fabs(x);
        }
        std::this_thread::sleep_for(std::chrono::microseconds(1333));   // 64 frames at 48 kHz
    }
}

static std::string read_file(const char* path) {
    std::string s;
    if (FILE* f = std::fopen(path, "rb")) {
        char buf[4096];
        size_t n;
        while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) s.append(buf, n);
        std::fclose(f);
    }
    return s;
}

static bool has(const std::string& log, const char* what) { return log.find(what) != std::string::npos; }

int main() {
    const std::string dir = PROBE_DIR;
    std::remove((dir + "/seqprobe.log").c_str());
    std::remove((dir + "/seqprobe-save.txt").c_str());

    unit_runtime_desc_t desc;
    std::memset(&desc, 0, sizeof(desc));
    desc.target = unit_header.target;
    desc.api = UNIT_API_VERSION;
    desc.samplerate = 48000;
    desc.frames_per_buffer = 64;
    desc.input_channels = 2;
    desc.output_channels = 2;
    CHECK(unit_init(&desc) == k_unit_err_none, "unit_init");

    std::thread audio(render_thread);

    // ── Page 1: the panel draws it, a knob turns ────────────────────────────
    CHECK(str(3) == "k1=00", "FollowS before a turn: %s", str(3).c_str());
    unit_set_param_value(0, 5);
    CHECK(str(3) == "k1=05", "FollowS follows knob 1: %s", str(3).c_str());
    {
        const uint8_t* a = unit_get_param_bmp_value(0, 5);
        uint8_t blink[32], follow[32];
        std::memcpy(blink, a, 32);
        std::memcpy(follow, unit_get_param_bmp_value(1, 0), 32);
        CHECK(std::memcmp(blink + 24, follow + 24, 8) == 0, "Follow's bar (rows 12-15) is Blink's");
        CHECK(blink[24] == 0x3F && blink[25] == 0x00, "bar of value 5 lights pixels 0-5: %02x %02x",
              blink[24], blink[25]);
        // The blinking square (rows 0-4, x 0-4) flips within a second.
        const bool lit0 = blink[0] & 1;
        bool flipped = false;
        for (int i = 0; i < 12 && !flipped; ++i) {
            sleep_ms(100);
            flipped = (unit_get_param_bmp_value(0, 5)[0] & 1) != lit0;
        }
        CHECK(flipped, "Blink's square flips with the unit's clock");
    }

    // ── A program load: preset, then every knob ─────────────────────────────
    sleep_ms(20);
    unit_load_preset(1);
    for (uint8_t i = 0; i < 24; ++i) unit_set_param_value(i, unit_header.params[i].init);
    CHECK(str(7).rfind("24/", 0) == 0, "Burst shows the restore: %s", str(7).c_str());
    CHECK(unit_get_param_value(4) == 90, "preset B's shadow survives the restore: %d",
          (int)unit_get_param_value(4));
    CHECK(str(12) == "Kick2", "the restore did not edit step 1: %s", str(12).c_str());
    CHECK(str(5, 50) == "h50 u90", "OwnedS shows host and unit values: %s", str(5, 50).c_str());

    // ── Step knobs: a host that keeps its own number ────────────────────────
    sleep_ms(80);   // past the 50 ms after a preset load that counts as restore
    unit_set_param_value(12, 501);
    CHECK(str(12) == "808Sub", "one turn right from Kick2: %s", str(12).c_str());

    // …and one that takes the unit's answer (asked on a page draw).
    sleep_ms(20);
    const int32_t answer = unit_get_param_value(12);
    CHECK(answer == 301, "step knob answers 200 away from the host's number: %d", (int)answer);
    unit_set_param_value(12, answer + 1);
    CHECK(str(12) == "Kick", "counted from the unit's answer: %s", str(12).c_str());
    sleep_ms(10);
    unit_set_param_value(12, answer + 2);
    CHECK(str(12) == "Kick", "clamped at the last program: %s", str(12).c_str());
    sleep_ms(10);
    unit_set_param_value(12, answer + 1);
    CHECK(str(12) == "808Sub", "and back down: %s", str(12).c_str());
    sleep_ms(10);
    unit_set_param_value(12, 900);
    CHECK(str(12) == "808Sub", "a big jump is a resync, not an edit: %s", str(12).c_str());

    // ── The lane view: page 5 picks the lane page 4 shows ───────────────────
    sleep_ms(10);
    unit_set_param_value(16, 1);
    CHECK(str(16, 1) == "B Snare", "Lane: %s", str(16, 1).c_str());
    CHECK(str(12) == "---", "page 4 now shows lane B's step 1: %s", str(12).c_str());
    CHECK(str(15) == "---", "lane B step 4: %s", str(15).c_str());

    // An edit that turns out to be the first set of a restore is undone.
    sleep_ms(20);
    unit_set_param_value(13, 501);
    CHECK(str(13) == "AcSnre", "edit lane B step 2: %s", str(13).c_str());
    unit_set_param_value(0, 0);
    unit_set_param_value(1, 0);
    CHECK(str(13) == "---", "reverted once the burst shows: %s", str(13).c_str());

    // ── The sequencer: gates, stop, a MIDI note ─────────────────────────────
    CHECK(str(9) == "wait", "Gate before any gate: %s", str(9).c_str());
    unit_gate_on(127);
    CHECK(str(9) == "01 v127", "first gate: %s", str(9).c_str());
    sleep_ms(30);
    unit_gate_off();
    unit_gate_on(100);
    CHECK(str(9) == "02 v100", "second gate: %s", str(9).c_str());
    sleep_ms(100);
    unit_all_note_off();
    unit_reset();
    CHECK(str(10) == "Aoff>Rset", "what stop sent: %s", str(10).c_str());
    unit_gate_on(90);
    CHECK(str(9) == "01 v90", "the first gate after a stop is step 1: %s", str(9).c_str());
    unit_note_on(60, 100);
    CHECK(str(10) == "Rset>N60", "a MIDI note: %s", str(10).c_str());
    unit_set_tempo(120u << 16);
    sleep_ms(200);
    CHECK(s_peak.load() > 0.05f, "the gates made sound (peak %.3f)", s_peak.load());

    // ── A panel that polls: Clock asked 40 times a second for 2 s ───────────
    for (int i = 0; i < 80; ++i) { str(2); sleep_ms(25); }

    // ── Files ───────────────────────────────────────────────────────────────
    sleep_ms(300);
#ifdef PROBE_FORCE_NO_THREAD
    CHECK(str(11, 1) == "ok -", "Files without a thread: %s", str(11, 1).c_str());
#else
    CHECK(str(11, 1) == "ok T", "Files with the worker: %s", str(11, 1).c_str());
#endif
    CHECK(std::atof(str(2).c_str() + 1) >= 1.0, "Clock counts seconds: %s", str(2).c_str());

    s_run = false;
    audio.join();
    CHECK(!s_bad_sample.load(), "every sample finite and within ±1");
    unit_teardown();

    const std::string log = read_file((dir + "/seqprobe.log").c_str());
    CHECK(!log.empty(), "the log was written");
    const char* expected[] = {
        "=== seq_probe 1.0 session",
        "init samplerate=48000 frames_per_buffer=64 in=2 out=2",
        "THREAD set runs here",
        "THREAD render runs here",
        "FILE save: write + fsync + rename + read back: OK",
        "FILE dir: f seqprobe.log",
        "FILE fs:",
        "load_preset 1",
        "RESTORE detected at set 3 of a group (3 knobs",
        ", right after load_preset",
        "set P23 = 0  (restore)",
        "edit P12 lane A step 1: 1 -> 2 (turn +1, counted from the host's last set)",
        "edit P12 lane A step 1: 2 -> 3 (turn +1, counted from the unit's get answer)",
        "resync P12: set 900",
        "edit P13 lane B step 2: 1 -> 0 UNDONE (that turn was the start of a restore)",
        "gate_on vel=127 -> step 1",
        "gate_on vel=100 -> step 2",
        "all_note_off",
        "restart: next gate is step 1 (stop event)",
        "note_on note=60 vel=100",
        "tempo 120.000 bpm",
        "bmp P00 v=5  (first call)",
        "str P03 v=0  (first call)",
        "poll P02 per second: str=",
        " blocks in 1 s, 64 frames each, longest gap between blocks ",
        "=== teardown ===",
#ifdef PROBE_FORCE_NO_THREAD
        "worker: no pthread_create in this C library; flushing from callbacks",
#else
        "worker thread started",
#endif
    };
    for (const char* e : expected) CHECK(has(log, e), "log has \"%s\"", e);
    CHECK(read_file((dir + "/seqprobe-save.txt").c_str()).rfind("seq_probe save test", 0) == 0,
          "the saved file reads back");

    if (s_fail) {
        std::printf("\n--- log ---\n%s\n", log.c_str());
        std::printf("%d check(s) FAILED\n", s_fail);
        return 1;
    }
    std::printf("all checks passed (%zu bytes of log)\n", log.size());
    return 0;
}
