// seq_probe host test: drives the unit's callbacks the way the drumlogue
// would — a real-time render thread, panel calls from another, a program
// load, knob turns from a host that keeps its own number and from one that
// takes the unit's answer, gates (also from the audio thread), stop, a
// polling panel — and checks every display and every Result.
//
// And the file rule: files are touched only inside the first unit_init.
// The folder is snapshotted when unit_init returns and must be unchanged at
// the end; a second unit_init (a "late" one) must touch nothing.
//
//   ./run_host_test.sh          host build, address + undefined sanitizers
//   ARM=1 ./run_host_test.sh    ARM build under qemu-arm
#include <sys/stat.h>
#include <dirent.h>

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <thread>

#include "unit.h"

#ifndef PROBE_DIR
#error "build with -DPROBE_DIR=\"...\""
#endif

static int s_fail = 0;
#define CHECK(cond, ...) do { if (!(cond)) { ++s_fail; std::printf("FAIL %s:%d  ", __FILE__, __LINE__); \
    std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)

enum { P_CLOCK = 2, P_FOLLOW_S = 3, P_OWNED = 4, P_OWNED_S = 5, P_BURST = 7, P_GATE = 9,
       P_EVENT = 10, P_FILES = 11, P_STEP1 = 12, P_STEP2 = 13, P_STEP4 = 15, P_LANE = 16,
       P_RESULT = 20 };

static std::string str(uint8_t id, int32_t v = 0) {
    const char* s = unit_get_param_str_value(id, v);
    return s ? s : "(null)";
}
static std::string result(int i) { return str(P_RESULT, i); }
static bool starts(const std::string& s, const char* p) { return s.rfind(p, 0) == 0; }

static void sleep_ms(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }

// The folder as name -> size:mtime, to prove nothing changed in it.
static std::map<std::string, std::string> snapshot(const std::string& dir) {
    std::map<std::string, std::string> m;
    if (DIR* d = opendir(dir.c_str())) {
        while (const dirent* e = readdir(d)) {
            struct stat st;
            const std::string p = dir + "/" + e->d_name;
            if (e->d_name[0] == '.' || stat(p.c_str(), &st) != 0) continue;
            m[e->d_name] = std::to_string(st.st_size) + ":" + std::to_string(st.st_mtim.tv_sec) + "." +
                           std::to_string(st.st_mtim.tv_nsec);
        }
        closedir(d);
    }
    return m;
}

static std::atomic<bool> s_run{true};
static std::atomic<bool> s_gate_from_audio{false};
static std::atomic<bool> s_bad_sample{false};
static std::atomic<float> s_peak{0.0f};

static void render_thread() {
    float out[64 * 2];
    while (s_run.load()) {
        if (s_gate_from_audio.exchange(false)) unit_gate_on(64);   // the sequencer on the audio thread
        unit_render(nullptr, out, 64);
        for (float x : out) {
            if (!std::isfinite(x) || std::fabs(x) > 1.0f) s_bad_sample = true;
            if (std::fabs(x) > s_peak.load()) s_peak = std::fabs(x);
        }
        std::this_thread::sleep_for(std::chrono::microseconds(1333));   // 64 frames at 48 kHz
    }
}

int main() {
    const std::string dir = PROBE_DIR;
    // A file from "the previous boot".
    std::remove((dir + "/seqprobe-save.tmp").c_str());
    if (FILE* f = std::fopen((dir + "/seqprobe-save.txt").c_str(), "wb")) {
        std::fputs("seq_probe save test, uptime 9 s\n", f);
        std::fclose(f);
    }

    unit_runtime_desc_t desc;
    std::memset(&desc, 0, sizeof(desc));
    desc.target = unit_header.target;
    desc.api = UNIT_API_VERSION;
    desc.samplerate = 48000;
    desc.frames_per_buffer = 64;
    desc.input_channels = 2;
    desc.output_channels = 2;
    CHECK(unit_init(&desc) == k_unit_err_none, "unit_init");
    const auto after_init = snapshot(dir);

    // ── The boot-time file test, before anything runs ───────────────────────
    CHECK(str(P_FILES) == "rw ok", "Files: %s", str(P_FILES).c_str());
    CHECK(starts(result(0), "b+") && result(0).find(" #1") != std::string::npos, "init: %s", result(0).c_str());
    CHECK(result(1) == "rd ok 1", "read at boot: %s", result(1).c_str());
    CHECK(result(2) == "wr ok", "write at boot: %s", result(2).c_str());
    CHECK(result(3) == "prev ok", "the previous boot's file: %s", result(3).c_str());
    CHECK(starts(result(4), "io "), "file time: %s", result(4).c_str());
    CHECK(result(5) == "aud ?", "no audio thread yet: %s", result(5).c_str());
    CHECK(unit_header.params[P_RESULT].max == 11 && result(12) == "-", "Result has 12 positions");

    std::thread audio(render_thread);

    // ── Page 1: the panel draws it, a knob turns ────────────────────────────
    CHECK(str(P_FOLLOW_S) == "k1=00", "FollowS before a turn: %s", str(P_FOLLOW_S).c_str());
    unit_set_param_value(0, 5);
    CHECK(str(P_FOLLOW_S) == "k1=05", "FollowS follows knob 1: %s", str(P_FOLLOW_S).c_str());
    {
        uint8_t blink[32], follow[32];
        std::memcpy(blink, unit_get_param_bmp_value(0, 5), 32);
        std::memcpy(follow, unit_get_param_bmp_value(1, 0), 32);
        CHECK(std::memcmp(blink + 24, follow + 24, 8) == 0, "Follow's bar (rows 12-15) is Blink's");
        CHECK(blink[24] == 0x3F && blink[25] == 0x00, "bar of value 5 lights pixels 0-5: %02x %02x",
              blink[24], blink[25]);
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
    CHECK(starts(str(P_BURST), "24/"), "Burst shows the restore: %s", str(P_BURST).c_str());
    CHECK(unit_get_param_value(P_OWNED) == 90, "preset B's answer survives the restore: %d",
          (int)unit_get_param_value(P_OWNED));
    CHECK(str(P_STEP1) == "Kick2", "the restore did not edit step 1: %s", str(P_STEP1).c_str());
    CHECK(str(P_OWNED_S, 50) == "h50 u90", "OwnedS: %s", str(P_OWNED_S, 50).c_str());

    // ── Step knobs: a host that keeps its own number ────────────────────────
    sleep_ms(80);   // past the 50 ms after a preset load that counts as restore
    unit_set_param_value(P_STEP1, 501);
    CHECK(str(P_STEP1) == "808Sub", "one turn right from Kick2: %s", str(P_STEP1).c_str());

    // …one that takes the unit's answer (asked on a page draw)…
    sleep_ms(20);
    const int32_t answer = unit_get_param_value(P_STEP1);
    CHECK(answer == 301, "step knob answers 200 away from the host's number: %d", (int)answer);
    unit_set_param_value(P_STEP1, answer + 1);
    CHECK(str(P_STEP1) == "Kick", "counted from the unit's answer: %s", str(P_STEP1).c_str());
    sleep_ms(10);
    unit_set_param_value(P_STEP1, answer + 2);
    CHECK(str(P_STEP1) == "Kick", "clamped at the last program: %s", str(P_STEP1).c_str());
    // …and one that was asked but kept its own number.
    sleep_ms(10);
    unit_get_param_value(P_STEP1);
    unit_set_param_value(P_STEP1, answer + 1);
    CHECK(str(P_STEP1) == "808Sub", "counted from its own number: %s", str(P_STEP1).c_str());
    CHECK(result(10) == "unit1 own1", "adoption: %s", result(10).c_str());
    sleep_ms(10);
    unit_set_param_value(P_STEP1, 900);
    CHECK(str(P_STEP1) == "808Sub", "a big jump is a resync, not an edit: %s", str(P_STEP1).c_str());

    // ── The lane view: page 5 picks the lane page 4 shows ───────────────────
    sleep_ms(10);
    unit_set_param_value(P_LANE, 1);
    CHECK(str(P_LANE, 1) == "B Snare", "Lane: %s", str(P_LANE, 1).c_str());
    CHECK(str(P_STEP1) == "---", "page 4 now shows lane B's step 1: %s", str(P_STEP1).c_str());
    CHECK(str(P_STEP4) == "---", "lane B step 4: %s", str(P_STEP4).c_str());

    // An edit that turns out to be the first set of a restore is undone.
    sleep_ms(20);
    unit_set_param_value(P_STEP2, 501);
    CHECK(str(P_STEP2) == "AcSnre", "edit lane B step 2: %s", str(P_STEP2).c_str());
    unit_set_param_value(0, 0);
    unit_set_param_value(1, 0);
    CHECK(str(P_STEP2) == "---", "undone once the burst shows: %s", str(P_STEP2).c_str());

    // ── The sequencer: gates, stop, a MIDI note, tempo ──────────────────────
    CHECK(str(P_GATE) == "wait", "Gate before any gate: %s", str(P_GATE).c_str());
    unit_gate_on(127);
    CHECK(str(P_GATE) == "01 v127", "first gate: %s", str(P_GATE).c_str());
    sleep_ms(30);
    unit_gate_off();
    unit_gate_on(100);
    CHECK(str(P_GATE) == "02 v100", "second gate: %s", str(P_GATE).c_str());
    sleep_ms(100);
    unit_all_note_off();
    unit_reset();
    CHECK(str(P_EVENT) == "Aoff>Rset", "what stop sent: %s", str(P_EVENT).c_str());
    unit_gate_on(90);
    CHECK(str(P_GATE) == "01 v90", "the first gate after a stop is step 1: %s", str(P_GATE).c_str());
    unit_note_on(60, 100);
    CHECK(str(P_EVENT) == "Rset>N60", "a MIDI note: %s", str(P_EVENT).c_str());
    unit_set_tempo(120u << 16);
    CHECK(result(11) == "bpm 120.0", "tempo: %s", result(11).c_str());
    s_gate_from_audio = true;
    sleep_ms(100);
    CHECK(result(5) == "aud G", "gate_on seen on the audio thread: %s", result(5).c_str());
    CHECK(result(6) == "OTHER 0", "the audio thread's scheduling: %s", result(6).c_str());
    CHECK(s_peak.load() > 0.05f, "the gates made sound (peak %.3f)", s_peak.load());

    // ── A panel that polls: Clock asked 40 times a second for 2.5 s ─────────
    for (int i = 0; i < 100; ++i) { str(P_CLOCK); sleep_ms(25); }
    // An audio second is a little longer than a real one here (sleep
    // overhead, sanitizers, qemu), so the rate runs a little high.
    const int str_hz = std::atoi(result(7).c_str() + 4);
    CHECK(str_hz >= 25 && str_hz <= 80, "str rate: %s", result(7).c_str());
    // Only the Blink loop above asked for bitmaps: at most ~10 a second.
    const int bmp_hz = std::atoi(result(8).c_str() + 4);
    CHECK(bmp_hz <= 15, "bmp rate: %s", result(8).c_str());
    CHECK(result(9) == "knobs 1", "one knob polled at a time: %s", result(9).c_str());
    CHECK(std::atof(str(P_CLOCK).c_str() + 1) >= 2.0, "Clock counts seconds: %s", str(P_CLOCK).c_str());

    s_run = false;
    audio.join();
    CHECK(!s_bad_sample.load(), "every sample finite and within ±1");
    unit_teardown();
    CHECK(snapshot(dir) == after_init, "no file changed after unit_init");

    // ── A second, late unit_init touches no file ────────────────────────────
    CHECK(unit_init(&desc) == k_unit_err_none, "second unit_init");
    CHECK(str(P_FILES) == "late", "Files on a late init: %s", str(P_FILES).c_str());
    CHECK(result(1) == "late" && result(2) == "late", "nothing read or written late");
    CHECK(result(0).find(" #2") != std::string::npos, "init count: %s", result(0).c_str());
    unit_teardown();
    CHECK(snapshot(dir) == after_init, "a late init touched no file");

    if (s_fail) {
        std::printf("%d check(s) FAILED\n", s_fail);
        return 1;
    }
    std::printf("all checks passed\n");
    return 0;
}
