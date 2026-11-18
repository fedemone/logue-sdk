/**
 * @file unit.cc
 * @brief SeqProbe — measures the drumlogue's side of the unit API.
 *
 * A test unit for the planned parallel sequencer.  It answers, on the
 * device, what the SDK documentation does not say:
 *
 *   - when the panel asks for a bitmap or a string (on a timer? only for the
 *     knob being turned? when a page is entered?);
 *   - whether the panel takes the number unit_get_param_value() returns;
 *   - what a program load sends (the "restore burst");
 *   - what the sequencer sends: gates, per-step velocity, stop, tempo;
 *   - which callbacks run on the audio thread;
 *   - when unit_init runs, and whether files can be read and written then.
 *
 * The answers are on the panel: pages 1-3 live, page 6 a list (Result).
 *
 * Files: the drumlogue allows file handling only before the sequencer and
 * the synths start.  So files are touched in one place, run_at_boot(),
 * called from unit_init() only — and it does nothing unless unit_init runs
 * at boot (the first init, soon after the system started).
 *
 * Pages 4-5 are a prototype of the sequencer's lane view: each knob shows
 * one step of the lane chosen on page 5, and a turn edits that step from
 * what it holds.
 *
 * Built with -DPROBE_LITE (../seq_probe_lite) it is SeqProbeLite: the same
 * probe without what no other unit here uses — no bitmap knobs (Blink,
 * Follow and Grid show text) and no file access at all.  Installed next to
 * SeqProbe, one boot tells whether either of those keeps a unit off the
 * drumlogue's list.
 */

#include <atomic>
#include <cerrno>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <dirent.h>
#include <fcntl.h>
#include <sched.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

#include "unit.h"
#include "probe_draw.h"

// Optional: a weak undefined symbol resolves to null instead of failing the
// load, so the unit loads on a C library without it.
#pragma weak clock_gettime

#ifndef PROBE_DIR
// The drumlogue's user filesystem (the drive the PC sees in USB mode), as
// rgrvmusic/borg names it; borg keeps its own logs in Programs/.
#define PROBE_DIR "/var/lib/drumlogued/userfs/Programs"
#endif
#ifndef PROBE_BOOT_WINDOW_S
// unit_init this soon after the system started counts as boot time.
#define PROBE_BOOT_WINDOW_S 60
#endif

namespace {

#ifdef PROBE_LITE
constexpr bool kLite = true;
#else
constexpr bool kLite = false;
#endif

// ============================================================================
// Parameters (header.c holds the same order)
// ============================================================================
enum : uint8_t {
    P_BLINK, P_FOLLOW, P_CLOCK, P_FOLLOW_S,     // page 1
    P_OWNED, P_OWNED_S, P_SHADOW, P_BURST,      // page 2
    P_GRID, P_GATE, P_EVENT, P_FILES,           // page 3
    P_STEP1, P_STEP2, P_STEP3, P_STEP4,         // page 4
    P_LANE,                                     // page 5
    P_RESULT = 20,                              // page 6
    kNumParams = 24,
};

constexpr uint64_t kBurstGapUs      = 5000;     // sets closer than this are one group
constexpr int      kBurstIds        = 3;        // a group touching 3+ knobs is a restore
constexpr uint64_t kPresetRestoreUs = 50000;    // sets this soon after a preset load, too
constexpr int32_t  kStepCenter      = 500;
constexpr int32_t  kStepSwing       = 200;      // how far get_param_value moves a step knob
constexpr int32_t  kMaxStepDelta    = 16;       // a bigger jump is a resync, not a turn
constexpr uint64_t kRestartGapUs    = 1500000;  // no gate for this long: next gate is step 1
constexpr uint64_t kBootWindowUs    = uint64_t(PROBE_BOOT_WINDOW_S) * 1000000u;
constexpr uint32_t kPollMinCalls    = 3;        // calls to one knob in a second = polling
constexpr size_t   kStrLen          = 24;

// ============================================================================
// Clock and threads
// ============================================================================
uint32_t s_sr = 48000;
std::atomic<uint32_t> s_frames{0};   // audio frames rendered since unit_init

// Time since the system started (CLOCK_MONOTONIC); 0 when unavailable.
uint64_t uptime_us() {
    if (&clock_gettime == nullptr) return 0;
    timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return 0;
    return uint64_t(ts.tv_sec) * 1000000u + uint64_t(ts.tv_nsec) / 1000u;
}

uint64_t now_us() {
    const uint64_t t = uptime_us();
    return t ? t : uint64_t(s_frames.load(std::memory_order_relaxed)) * 1000000u / s_sr;
}

uint32_t thread_id() { return static_cast<uint32_t>(syscall(SYS_gettid)); }

// Which callbacks ran on the audio thread (the one unit_render runs on).
enum Cb { kCbGateOn, kCbGateOff, kCbNoteOn, kCbSet, kCbGet, kCbStr, kCbBmp,
          kCbPreset, kCbAllOff, kCbReset, kCbTempo, kNumCb };
const char kCbLetter[kNumCb + 1] = "GgNSVTBPARM";   // README.md, "The Result list"
std::atomic<uint32_t> s_render_tid{0};
std::atomic<uint32_t> s_on_audio{0};                // bit per Cb
int s_render_policy = -1;
int s_render_prio = 0;

void mark(Cb cb) {
    const uint32_t rt = s_render_tid.load(std::memory_order_relaxed);
    if (rt && thread_id() == rt) s_on_audio.fetch_or(1u << cb, std::memory_order_relaxed);
}

// How often the panel asks for each knob's text and picture.  Counted per
// second of audio; the session's highest rates are kept, from seconds in
// which no knob turned (a turning knob is asked on every click).
std::atomic<uint32_t> s_calls_str[kNumParams];
std::atomic<uint32_t> s_calls_bmp[kNumParams];
std::atomic<bool>     s_set_this_second{false};
uint32_t s_max_str_hz = 0, s_max_bmp_hz = 0, s_max_polled = 0;
uint32_t s_second_frames = 0;

void poll_second() {
    uint32_t str_hz = 0, bmp_hz = 0, polled = 0;
    for (int id = 0; id < kNumParams; ++id) {
        const uint32_t s = s_calls_str[id].exchange(0, std::memory_order_relaxed);
        const uint32_t b = s_calls_bmp[id].exchange(0, std::memory_order_relaxed);
        if (s > str_hz) str_hz = s;
        if (b > bmp_hz) bmp_hz = b;
        if (s + b >= kPollMinCalls) ++polled;
    }
    if (s_set_this_second.exchange(false, std::memory_order_relaxed)) return;
    if (str_hz > s_max_str_hz) s_max_str_hz = str_hz;
    if (bmp_hz > s_max_bmp_hz) s_max_bmp_hz = bmp_hz;
    if (polled > s_max_polled) s_max_polled = polled;
}

// ============================================================================
// unit_init, and the only file access
// ============================================================================
uint32_t s_init_count = 0;        // survives a re-init while the unit stays loaded
uint64_t s_init_uptime_us = 0;

namespace files {

const char kDir[]      = PROBE_DIR;
const char kSaveTmp[]  = PROBE_DIR "/seqprobe-save.tmp";
const char kSavePath[] = PROBE_DIR "/seqprobe-save.txt";
const char kStamp[]    = "seq_probe save test";

enum Run { kNotRun, kLate, kNoClock, kDone };
Run      s_run = kNotRun;
int      s_read_err = 0;   // errno of opendir, 0 = read
int      s_entries = 0;    // entries in the folder
int      s_prev = 0;       // 0 = no file from an earlier boot, 1 = it reads back, 2 = it does not
int      s_write = 0;      // 0 = written and read back, errno, or -1 = reads back different
uint32_t s_us = 0;         // how long it all took

bool reads_stamp(const char* path) {
    const int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return false;
    char b[32];
    const ssize_t n = read(fd, b, sizeof(b));
    close(fd);
    return n >= ssize_t(sizeof(kStamp) - 1) && std::memcmp(b, kStamp, sizeof(kStamp) - 1) == 0;
}

// Read the folder and the file an earlier boot wrote, then write a new one
// the way a song save would: a temporary file, flushed to storage, renamed
// over the real one (a power cut keeps the old file), read back.
//
// Only at boot: the first unit_init, soon after the system started.  Any
// later init (the unit selected while the drumlogue runs) touches nothing.
void run_at_boot(uint64_t uptime) {
    s_read_err = s_entries = s_prev = s_write = 0;
    s_us = 0;
    if (!uptime) { s_run = kNoClock; return; }
    if (s_init_count > 1 || uptime > kBootWindowUs) { s_run = kLate; return; }

    if (DIR* d = opendir(kDir)) {
        while (const dirent* e = readdir(d))
            if (std::strcmp(e->d_name, ".") && std::strcmp(e->d_name, "..")) ++s_entries;
        closedir(d);
    } else {
        s_read_err = errno;
    }
    if (access(kSavePath, F_OK) == 0) s_prev = reads_stamp(kSavePath) ? 1 : 2;

    char text[64];
    const int n = snprintf(text, sizeof(text), "%s, uptime %u s\n", kStamp, unsigned(uptime / 1000000u));
    const int fd = open(kSaveTmp, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (fd < 0) {
        s_write = errno;
    } else {
        int err = 0;
        if (write(fd, text, size_t(n)) != n) err = errno ? errno : EIO;
        else if (fsync(fd) != 0) err = errno ? errno : EIO;
        close(fd);
        if (err) s_write = err;
        else if (rename(kSaveTmp, kSavePath) != 0) s_write = errno;
        else s_write = reads_stamp(kSavePath) ? 0 : -1;
    }

    const uint64_t end = uptime_us();
    s_us = uint32_t(end > uptime ? end - uptime : 0);
    s_run = kDone;
}

}  // namespace files

// ============================================================================
// State
// ============================================================================
int32_t s_val[kNumParams];        // the number the host last set
int32_t s_last_get[kNumParams];   // the number the unit last answered
bool    s_get_since_set[kNumParams];
int32_t s_shadow = 70;            // what Owned / OwnedS answer when asked
uint8_t s_preset = 0;
uint8_t s_lane = 0;
uint32_t s_tempo = 0;

// Step turns made after the panel asked for the value: whether it counted
// from the unit's answer or from its own number.
uint32_t s_adopt_unit = 0, s_adopt_own = 0;

// The sequencer side
uint64_t s_last_gate_us = 0;
bool     s_restart = true;
uint8_t  s_step = 0;
uint8_t  s_last_vel = 0;
uint32_t s_gates = 0;

// The Event display: the last two non-knob, non-gate events
char s_note_name[8] = "";
const char* s_event_prev = "-";
const char* s_event_last = "-";

void note_event(const char* name) {
    s_event_prev = s_event_last;
    s_event_last = name;
}

// The restore guard
uint64_t s_set_last_us = 0, s_group_start_us = 0, s_preset_us = 0;
uint32_t s_group_ids = 0;     // bit per knob touched in the current group
uint16_t s_group_n = 0;
bool     s_group_restore = false;
uint16_t s_last_burst_n = 0;
uint32_t s_last_burst_span_us = 0;
struct PendingEdit { uint8_t lane, step, prev; };
PendingEdit s_pending[4];
uint8_t s_npending = 0;

// ============================================================================
// The lane-view prototype: 3 lanes x 16 steps, each step a program or empty
// ============================================================================
struct LaneDef { const char* name; uint8_t count; const char* const* progs; };
const char* const kKickProgs[]  = {"Kick2", "808Sub", "Kick"};
const char* const kSnareProgs[] = {"AcSnre", "MrchSnr", "BrshSnr", "RimShot"};
const char* const kBarProgs[]   = {"Marmba", "TblrBel", "Vibrph", "Wodblk"};
const LaneDef kLanes[3] = {
    {"A Kick", 3, kKickProgs}, {"B Snare", 4, kSnareProgs}, {"C Bar", 4, kBarProgs},
};
// 0 = empty, n = program n-1.  A demo pattern so every view has something.
uint8_t s_steps[3][16] = {
    {1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0},
    {0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0},
    {0, 0, 2, 0, 0, 0, 2, 0, 0, 0, 2, 0, 0, 0, 2, 0},
};

// ============================================================================
// Sound: a tick on every gate, a blip per lane step, a tone per MIDI note
// ============================================================================
// Every member initialised, so the array is constant-initialised: nothing
// runs when the drumlogue loads the unit.
struct VoiceReq {
    std::atomic<bool> fire{false};
    float freq = 0.0f, amp = 0.0f, decay_s = 0.0f;
    bool noise = false;
};

struct Voice {
    float re, im, cr, ci, amp, mul;
    bool noise, on;
};

enum { kVoiceLaneA, kVoiceLaneB, kVoiceLaneC, kVoiceTick, kVoiceNote, kNumVoices };
VoiceReq s_req[kNumVoices];
Voice    s_voice[kNumVoices];             // touched by the audio thread only
std::atomic<bool> s_kill{false};          // "stop every voice", for the audio thread
uint32_t s_noise = 0x12345678u;

void request(int v, float freq, float amp, float decay_s, bool noise = false) {
    VoiceReq& r = s_req[v];
    r.freq = freq; r.amp = amp; r.decay_s = decay_s; r.noise = noise;
    r.fire.store(true, std::memory_order_release);
}

void start_requested() {
    if (s_kill.exchange(false, std::memory_order_acquire))
        for (int i = 0; i < kNumVoices; ++i) s_voice[i].on = false;
    for (int i = 0; i < kNumVoices; ++i) {
        VoiceReq& r = s_req[i];
        if (!r.fire.exchange(false, std::memory_order_acquire)) continue;
        Voice& v = s_voice[i];
        const float w = 6.2831853f * r.freq / float(s_sr);
        v.re = 1.0f; v.im = 0.0f; v.cr = cosf(w); v.ci = sinf(w);
        v.amp = r.amp;
        v.mul = expf(-1.0f / (r.decay_s * float(s_sr)));
        v.noise = r.noise;
        v.on = true;
    }
}

void silence() {
    for (int i = 0; i < kNumVoices; ++i) s_req[i].fire.store(false, std::memory_order_relaxed);
    s_kill.store(true, std::memory_order_release);
}

void play_step(uint8_t vel) {
    request(kVoiceTick, 2000.0f, vel >= 120 ? 0.12f : 0.05f, 0.004f);
    const uint8_t a = s_steps[0][s_step], b = s_steps[1][s_step], c = s_steps[2][s_step];
    if (a) request(kVoiceLaneA, 50.0f + 20.0f * float(a - 1), 0.5f, 0.18f);
    if (b) request(kVoiceLaneB, 0.0f, 0.22f, 0.05f + 0.02f * float(b - 1), true);
    if (c) request(kVoiceLaneC, 440.0f * (1.0f + 0.25f * float(c - 1)), 0.22f, 0.35f);
}

// ============================================================================
// Displays
// ============================================================================
char       s_str[kNumParams][kStrLen];
probe::Bmp s_bmp[kNumParams];

bool blink_phase() { return (s_frames.load(std::memory_order_relaxed) / (s_sr / 2)) & 1u; }

// Blink / Follow: a square that blinks with the unit's clock, Blink's knob
// value as a number, and the same value as a bar along the bottom.
void draw_knob_view(probe::Bmp& b, int32_t v, bool lit) {
    if (lit) b.fill(0, 0, 4, 4);
    b.number2(15, 0, v);
    b.fill(0, 12, int(v < 0 ? 0 : v > 15 ? 15 : v), 15);
}

// Grid: 3 lanes x 16 steps, the cursor (Grid's knob, the lane from page 5)
// as blinking ticks above and below its cell, the playhead on row 13, beats
// on row 15.
void draw_grid(probe::Bmp& b, int32_t cursor, bool lit) {
    for (int lane = 0; lane < 3; ++lane) {
        const int y = 1 + lane * 4;
        for (int s = 0; s < 16; ++s)
            if (s_steps[lane][s]) b.fill(s, y, s, y + 1);
    }
    if (lit && cursor >= 0 && cursor < 16) {
        const int y = 1 + s_lane * 4;
        b.set(cursor, y - 1);
        b.set(cursor, y + 2);
    }
    if (s_gates && !s_restart) b.set(s_step, 13);
    for (int s = 0; s < 16; s += 4) b.set(s, 15);
}

const char* step_name(uint8_t lane, uint8_t step) {
    const uint8_t p = s_steps[lane][step];
    return p == 0 ? "---" : kLanes[lane].progs[p - 1];
}

// Files (page 3): the boot-time file test in a word.
const char* files_text(char* s) {
    switch (files::s_run) {
    case files::kNotRun:  return kLite ? "lite" : "-";
    case files::kLate:    return "late";
    case files::kNoClock: return "noclk";
    default: break;
    }
    if (files::s_read_err) snprintf(s, kStrLen, "rd e%d", files::s_read_err);
    else if (files::s_write > 0) snprintf(s, kStrLen, "wr e%d", files::s_write);
    else if (files::s_write < 0) return "wr diff";
    else return "rw ok";
    return s;
}

// Result (page 6): one measurement per position of the knob.  README.md,
// "The Result list", says what each means.
constexpr int kNumResults = 12;

const char* result_text(int32_t i, char* s) {
    if (i < 0 || i >= kNumResults) return "-";
    switch (i) {
    case 0:   // when unit_init ran (seconds after the system started), how many times
        if (s_init_uptime_us)
            snprintf(s, kStrLen, "b+%us #%u", unsigned(s_init_uptime_us / 1000000u), unsigned(s_init_count));
        else
            snprintf(s, kStrLen, "b+? #%u", unsigned(s_init_count));
        return s;
    case 1:   // reading at boot
        if (files::s_run != files::kDone) return files_text(s);
        if (files::s_read_err) snprintf(s, kStrLen, "rd e%d", files::s_read_err);
        else snprintf(s, kStrLen, "rd ok %d", files::s_entries);
        return s;
    case 2:   // writing at boot
        if (files::s_run != files::kDone) return files_text(s);
        if (files::s_write > 0) snprintf(s, kStrLen, "wr e%d", files::s_write);
        else return files::s_write < 0 ? "wr diff" : "wr ok";
        return s;
    case 3:   // the file the previous boot wrote
        if (files::s_run != files::kDone) return files_text(s);
        return files::s_prev == 1 ? "prev ok" : files::s_prev == 2 ? "prev bad" : "prev none";
    case 4:   // how long the boot-time file work took
        snprintf(s, kStrLen, "io %u.%ums", unsigned(files::s_us / 1000u), unsigned(files::s_us / 100u % 10u));
        return s;
    case 5: { // callbacks seen on the audio thread
        if (!s_render_tid.load()) return "aud ?";
        const uint32_t bits = s_on_audio.load();
        if (!bits) return "aud none";
        int n = snprintf(s, kStrLen, "aud ");
        for (int k = 0; k < kNumCb && n < int(kStrLen) - 1; ++k)
            if (bits & (1u << k)) s[n++] = kCbLetter[k];
        s[n] = '\0';
        return s;
    }
    case 6:   // the audio thread's scheduling
        if (s_render_policy < 0) return "rt ?";
        snprintf(s, kStrLen, "%s %d",
                 s_render_policy == SCHED_FIFO ? "FIFO" : s_render_policy == SCHED_RR ? "RR"
                 : s_render_policy == SCHED_OTHER ? "OTHER" : "?", s_render_prio);
        return s;
    // (No '/': the panel shows only the characters the SDK README lists.)
    case 7: snprintf(s, kStrLen, "str %uHz", unsigned(s_max_str_hz)); return s;
    case 8: snprintf(s, kStrLen, "bmp %uHz", unsigned(s_max_bmp_hz)); return s;
    case 9: snprintf(s, kStrLen, "knobs %u", unsigned(s_max_polled)); return s;
    case 10:
        snprintf(s, kStrLen, "unit%u own%u", unsigned(s_adopt_unit), unsigned(s_adopt_own));
        return s;
    case 11:
        if (!s_tempo) return "bpm -";
        snprintf(s, kStrLen, "bpm %u.%u", unsigned(s_tempo >> 16), unsigned((s_tempo & 0xFFFFu) * 10u / 65536u));
        return s;
    default:
        return "-";
    }
}

// ============================================================================
// Knob handling
// ============================================================================
// A person turns one knob at a time; restoring a program sets them all in a
// row.  Returns true when this set belongs to such a restore — then it only
// tells us the stored number, and edits made a moment ago in the same group
// are undone.
bool in_restore(uint8_t id, uint64_t t) {
    if (t - s_set_last_us > kBurstGapUs) {
        s_group_start_us = t; s_group_n = 0; s_group_ids = 0;
        s_group_restore = false; s_npending = 0;
    }
    s_set_last_us = t;
    ++s_group_n;
    s_group_ids |= 1u << id;
    int ids = 0;
    for (uint32_t m = s_group_ids; m; m &= m - 1) ++ids;
    const bool after_preset = (t - s_preset_us) < kPresetRestoreUs;
    if (!s_group_restore && (ids >= kBurstIds || after_preset)) {
        s_group_restore = true;
        for (uint8_t i = 0; i < s_npending; ++i)
            s_steps[s_pending[i].lane][s_pending[i].step] = s_pending[i].prev;
        s_npending = 0;
    }
    if (s_group_restore) {
        s_last_burst_n = s_group_n;
        s_last_burst_span_us = uint32_t(t - s_group_start_us);
    }
    return s_group_restore;
}

// A step knob is read as a turn: the change from the number the host last
// knew, applied to what the step holds.  If the panel asked for the value
// since its last set, the host may have taken the unit's answer as its
// number; the reference closer to the new value is the one it used.
void step_turn(uint8_t id, int32_t v, int32_t last_set, bool restore) {
    const bool asked = s_get_since_set[id];
    s_get_since_set[id] = false;
    if (restore) return;
    int32_t ref = last_set;
    if (asked) {
        if (std::abs(v - s_last_get[id]) < std::abs(v - last_set)) {
            ref = s_last_get[id];
            ++s_adopt_unit;
        } else {
            ++s_adopt_own;
        }
    }
    const int32_t d = v - ref;
    if (d == 0 || d > kMaxStepDelta || d < -kMaxStepDelta) return;   // a big jump is a resync
    const uint8_t lane = s_lane, step = uint8_t(id - P_STEP1);
    const int old = s_steps[lane][step];
    int nv = old + d;
    if (nv < 0) nv = 0;
    if (nv > kLanes[lane].count) nv = kLanes[lane].count;
    s_steps[lane][step] = uint8_t(nv);
    if (s_npending < 4) s_pending[s_npending++] = {lane, step, uint8_t(old)};
}

void reset_state() {
    for (uint8_t i = 0; i < kNumParams; ++i) {
        s_val[i] = unit_header.params[i].init;
        s_last_get[i] = 0;
        s_get_since_set[i] = false;
        s_calls_str[i].store(0);
        s_calls_bmp[i].store(0);
    }
    s_max_str_hz = s_max_bmp_hz = s_max_polled = 0;
    s_second_frames = 0;
    s_render_tid.store(0);
    s_on_audio.store(0);
    s_render_policy = -1;
    s_shadow = s_val[P_SHADOW] * 10;
    s_lane = 0;
    s_preset = 0;
    s_tempo = 0;
    s_adopt_unit = s_adopt_own = 0;
    s_restart = true;
    s_gates = 0;
    s_set_last_us = s_preset_us = 0;
    s_last_burst_n = 0;
    s_event_prev = s_event_last = "-";
}

}  // namespace

// ============================================================================
// SDK callbacks
// ============================================================================
__unit_callback int8_t unit_init(const unit_runtime_desc_t* desc) {
    if (!desc) return k_unit_err_undef;
    if (desc->target != unit_header.target) return k_unit_err_target;
    if (!UNIT_API_IS_COMPAT(desc->api)) return k_unit_err_api_version;
    if (desc->output_channels != 2) return k_unit_err_geometry;

    ++s_init_count;
    s_init_uptime_us = uptime_us();
    s_sr = desc->samplerate ? desc->samplerate : 48000;
    s_frames.store(0);
    reset_state();
    silence();
    if (!kLite) files::run_at_boot(s_init_uptime_us);   // the only file access, and only at boot
    return k_unit_err_none;
}

__unit_callback void unit_teardown() { silence(); }

__unit_callback void unit_reset() {
    mark(kCbReset);
    note_event("Rset");
    s_restart = true;
    silence();
}

__unit_callback void unit_resume() { note_event("Resm"); }

__unit_callback void unit_suspend() {
    note_event("Susp");
    silence();
}

__unit_callback void unit_render(const float* in, float* out, uint32_t frames) {
    (void)in;
    if (!s_render_tid.load(std::memory_order_relaxed)) {
        sched_param sp;
        std::memset(&sp, 0, sizeof(sp));
        s_render_policy = sched_getscheduler(0);
        sched_getparam(0, &sp);
        s_render_prio = sp.sched_priority;
        s_render_tid.store(thread_id(), std::memory_order_relaxed);
    }
    start_requested();
    for (uint32_t i = 0; i < frames; ++i) {
        float s = 0.0f;
        for (int k = 0; k < kNumVoices; ++k) {
            Voice& v = s_voice[k];
            if (!v.on) continue;
            float x;
            if (v.noise) {
                s_noise ^= s_noise << 13; s_noise ^= s_noise >> 17; s_noise ^= s_noise << 5;
                x = float(int32_t(s_noise)) * (1.0f / 2147483648.0f);
            } else {
                const float re = v.re * v.cr - v.im * v.ci;
                v.im = v.im * v.cr + v.re * v.ci;
                v.re = re;
                x = v.im;
            }
            s += v.amp * x;
            v.amp *= v.mul;
            if (v.amp < 1e-4f) v.on = false;
        }
        if (s > 1.0f) s = 1.0f;
        if (s < -1.0f) s = -1.0f;
        out[i * 2] = s;
        out[i * 2 + 1] = s;
    }
    s_frames.fetch_add(frames, std::memory_order_relaxed);
    s_second_frames += frames;
    if (s_second_frames >= s_sr) {
        s_second_frames -= s_sr;
        poll_second();
    }
}

__unit_callback void unit_set_param_value(uint8_t id, int32_t value) {
    mark(kCbSet);
    if (id >= kNumParams) return;
    s_set_this_second.store(true, std::memory_order_relaxed);
    const bool restore = in_restore(id, now_us());
    const int32_t last_set = s_val[id];
    s_val[id] = value;
    switch (id) {
    case P_SHADOW:
        if (!restore) s_shadow = value * 10;   // a restore must not overwrite the preset test
        break;
    case P_LANE:
        s_lane = uint8_t(value < 0 ? 0 : value > 2 ? 2 : value);
        break;
    case P_STEP1: case P_STEP2: case P_STEP3: case P_STEP4:
        step_turn(id, value, last_set, restore);
        break;
    default:
        break;
    }
    if (id < P_STEP1 || id > P_STEP4) s_get_since_set[id] = false;
}

__unit_callback int32_t unit_get_param_value(uint8_t id) {
    mark(kCbGet);
    if (id >= kNumParams) return 0;
    int32_t v;
    switch (id) {
    case P_OWNED: case P_OWNED_S:
        v = s_shadow;   // not what the host set: does the panel take it?
        break;
    case P_STEP1: case P_STEP2: case P_STEP3: case P_STEP4:
        // Far from the host's own number, so the next turn tells which of
        // the two it counted from.
        v = s_val[id] < kStepCenter ? s_val[id] + kStepSwing : s_val[id] - kStepSwing;
        break;
    default:
        v = s_val[id];
        break;
    }
    s_last_get[id] = v;
    s_get_since_set[id] = true;
    return v;
}

__unit_callback const char* unit_get_param_str_value(uint8_t id, int32_t value) {
    mark(kCbStr);
    if (id >= kNumParams) return "";
    s_calls_str[id].fetch_add(1, std::memory_order_relaxed);
    char* s = s_str[id];
    switch (id) {
    // SeqProbeLite shows the bitmap knobs as text: the value, and a mark
    // that blinks with the unit's clock (Follow in the other phase); Grid
    // shows the cursor step and the playhead step.
    case P_BLINK: case P_FOLLOW:
        if (!kLite) return "-";
        snprintf(s, kStrLen, "%02d %s", int(s_val[P_BLINK]), blink_phase() == (id == P_BLINK) ? "*" : "-");
        return s;
    case P_GRID:
        if (!kLite) return "-";
        if (s_gates && !s_restart)
            snprintf(s, kStrLen, "c%02d p%02d", int(s_val[P_GRID]) + 1, int(s_step) + 1);
        else
            snprintf(s, kStrLen, "c%02d", int(s_val[P_GRID]) + 1);
        return s;
    case P_CLOCK:
        snprintf(s, kStrLen, "t%u", unsigned(s_frames.load() / s_sr));
        return s;
    case P_FOLLOW_S:
        snprintf(s, kStrLen, "k1=%02d", int(s_val[P_BLINK]));
        return s;
    case P_OWNED_S:
        snprintf(s, kStrLen, "h%d u%d", int(value), int(s_shadow));
        return s;
    case P_BURST:
        if (!s_last_burst_n) return "none";
        snprintf(s, kStrLen, "%u in %ums", unsigned(s_last_burst_n), unsigned(s_last_burst_span_us / 1000u));
        return s;
    case P_GATE:
        if (!s_gates) return "wait";
        snprintf(s, kStrLen, "%02u v%u", unsigned(s_step + 1), unsigned(s_last_vel));
        return s;
    case P_EVENT:
        snprintf(s, kStrLen, "%s>%s", s_event_prev, s_event_last);
        return s;
    case P_FILES:
        return files_text(s);
    case P_STEP1: case P_STEP2: case P_STEP3: case P_STEP4:
        return step_name(s_lane, uint8_t(id - P_STEP1));   // the step, not the knob's number
    case P_LANE:
        return kLanes[value < 0 ? 0 : value > 2 ? 2 : value].name;
    case P_RESULT:
        return result_text(value, s);
    default:
        return "-";
    }
}

__unit_callback const uint8_t* unit_get_param_bmp_value(uint8_t id, int32_t value) {
    mark(kCbBmp);
    if (id >= kNumParams) return nullptr;
    s_calls_bmp[id].fetch_add(1, std::memory_order_relaxed);
    (void)value;
    probe::Bmp& b = s_bmp[id];
    b.clear();
    const bool lit = blink_phase();
    switch (id) {
    case P_BLINK:  draw_knob_view(b, s_val[P_BLINK], lit); break;
    case P_FOLLOW: draw_knob_view(b, s_val[P_BLINK], !lit); break;   // knob 1's value, other phase
    case P_GRID:   draw_grid(b, s_val[P_GRID], lit); break;
    default: break;
    }
    return b.px;
}

__unit_callback void unit_set_tempo(uint32_t tempo) {
    mark(kCbTempo);
    s_tempo = tempo;
}

__unit_callback void unit_note_on(uint8_t note, uint8_t velocity) {
    mark(kCbNoteOn);
    snprintf(s_note_name, sizeof(s_note_name), "N%u", unsigned(note));
    note_event(s_note_name);
    request(kVoiceNote, 440.0f * powf(2.0f, (float(note) - 69.0f) / 12.0f),
            0.3f * float(velocity) / 127.0f, 0.3f);
}

__unit_callback void unit_note_off(uint8_t note) { (void)note; }

__unit_callback void unit_gate_on(uint8_t velocity) {
    mark(kCbGateOn);
    const uint64_t t = now_us();
    if (s_restart || t - s_last_gate_us > kRestartGapUs) s_step = 0;
    else s_step = uint8_t((s_step + 1) & 15);
    s_restart = false;
    s_last_gate_us = t;
    s_last_vel = velocity;
    ++s_gates;
    play_step(velocity);
}

__unit_callback void unit_gate_off() { mark(kCbGateOff); }

__unit_callback void unit_all_note_off() {
    mark(kCbAllOff);
    note_event("Aoff");
    s_restart = true;
    silence();
}

__unit_callback void unit_pitch_bend(uint16_t bend) { (void)bend; }
__unit_callback void unit_channel_pressure(uint8_t pressure) { (void)pressure; }
__unit_callback void unit_aftertouch(uint8_t note, uint8_t aftertouch) { (void)note; (void)aftertouch; }

__unit_callback void unit_load_preset(uint8_t idx) {
    mark(kCbPreset);
    s_preset_us = now_us();
    if (idx < 2) {
        s_preset = idx;
        s_shadow = idx == 0 ? 20 : 90;   // Owned should show this if the panel re-reads
        note_event(idx == 0 ? "PreA" : "PreB");
    }
}

__unit_callback uint8_t unit_get_preset_index() { return s_preset; }

__unit_callback const char* unit_get_preset_name(uint8_t idx) {
    return idx == 0 ? "ProbeA" : idx == 1 ? "ProbeB" : nullptr;
}
