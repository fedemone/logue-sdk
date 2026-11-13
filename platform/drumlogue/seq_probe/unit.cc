/**
 * @file unit.cc
 * @brief SeqProbe — measures the drumlogue's side of the unit API.
 *
 * A test unit for the planned parallel sequencer.  It answers, on the
 * device, what the SDK documentation does not say:
 *
 *   - when the panel asks for a bitmap or string (on a timer? only for the
 *     knob being turned? when a page is entered?);
 *   - whether the panel adopts the number get_param_value() returns;
 *   - what a program load sends (the "restore burst");
 *   - what the sequencer sends: gates, per-step velocity, stop;
 *   - which callback runs on which thread;
 *   - whether a unit may start a thread and write files in userfs.
 *
 * Everything is logged to userfs/Programs/seqprobe.log (README.md says how
 * to read it); pages 1-3 show the same answers on the panel.  Pages 4-5 are
 * a prototype of the sequencer's lane view: each knob shows one step of the
 * lane chosen on page 5, and a turn edits that step from what it holds.
 *
 * Files and threads are not part of the SDK.  The unit only uses them where
 * the C library offers them (weak symbols) and keeps working without.
 */

#include <atomic>
#include <cerrno>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <dirent.h>
#include <fcntl.h>
#include <pthread.h>
#include <sched.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

#include "unit.h"
#include "probe_draw.h"
#include "probe_log.h"

// Optional symbols: a weak undefined symbol resolves to null instead of
// failing the load, so the unit loads on a C library without them.  Every
// use checks for null first.
#pragma weak pthread_create
#pragma weak pthread_join
#pragma weak clock_gettime

#ifndef PROBE_DIR
// The drumlogue's user filesystem (the drive the PC sees in USB mode), as
// rgrvmusic/borg writes it; borg keeps its own logs in Programs/.
#define PROBE_DIR "/var/lib/drumlogued/userfs/Programs"
#endif

using probe::Ev;
using probe::Rec;

namespace {

// ============================================================================
// Parameters (header.c holds the same order)
// ============================================================================
enum : uint8_t {
    P_BLINK, P_FOLLOW, P_CLOCK, P_FOLLOW_S,     // page 1
    P_OWNED, P_OWNED_S, P_SHADOW, P_BURST,      // page 2
    P_GRID, P_GATE, P_EVENT, P_FILES,           // page 3
    P_STEP1, P_STEP2, P_STEP3, P_STEP4,         // page 4
    P_LANE,                                     // page 5
    kNumParams = 24,
};

constexpr uint64_t kBurstGapUs     = 5000;      // sets closer than this are one group
constexpr int      kBurstIds       = 3;         // a group touching 3+ knobs is a restore
constexpr uint64_t kPresetRestoreUs = 50000;    // sets this soon after a preset load, too
constexpr int32_t  kStepCenter     = 500;
constexpr int32_t  kStepSwing      = 200;       // how far get_param_value moves a step knob
constexpr int32_t  kMaxStepDelta   = 16;        // a bigger jump is a resync, not a turn
constexpr uint64_t kRestartGapUs   = 1500000;   // no gate for this long: next gate is step 1
constexpr uint64_t kPollQuietUs    = 500000;    // log a polled call after this much silence
constexpr long     kLogCap         = 2L * 1024 * 1024;
#ifndef PROBE_STATS_SECONDS
#define PROBE_STATS_SECONDS 10                  // render cadence and call rates, this often
#endif
constexpr uint32_t kStatsSeconds   = PROBE_STATS_SECONDS;

// ============================================================================
// Clock, thread id, log
// ============================================================================
probe::Ring s_ring;
std::atomic<uint32_t> s_frames{0};   // audio frames rendered since unit_init
uint32_t s_sr = 48000;
uint64_t s_t0_us = 0;

uint64_t now_us() {
    if (&clock_gettime != nullptr) {
        timespec ts;
        if (clock_gettime(CLOCK_MONOTONIC, &ts) == 0)
            return uint64_t(ts.tv_sec) * 1000000u + uint64_t(ts.tv_nsec) / 1000u;
    }
    return uint64_t(s_frames.load(std::memory_order_relaxed)) * 1000000u / s_sr;
}

uint32_t thread_id() { return static_cast<uint32_t>(syscall(SYS_gettid)); }

void log_ev(Ev ev, uint8_t a = 0, int32_t b = 0, int32_t c = 0, uint16_t d = 0) {
    s_ring.push(ev, a, b, c, d, now_us(), s_frames.load(std::memory_order_relaxed), thread_id());
}

// The first call of each callback records its thread and scheduling class.
std::atomic<bool> s_seen[probe::kEvCount];

void thread_info_once(Ev ev) {
    if (s_seen[ev].exchange(true, std::memory_order_relaxed)) return;
    sched_param sp;
    std::memset(&sp, 0, sizeof(sp));
    const int policy = sched_getscheduler(0);
    sched_getparam(0, &sp);
    log_ev(probe::kEvThreadInfo, ev, policy, sp.sched_priority);
}

// Calls the panel may make many times a second.  Each is counted (a "poll"
// line reports the rate when it changes) and logged on its own when its
// value changes or after a quiet spell — which is how a page being entered
// shows up.
enum PollKind { kPollStr, kPollBmp, kPollGet, kPollPresetName, kPollPresetIndex, kNumPollKinds };
uint64_t s_poll_last_us[kNumPollKinds][kNumParams];
int32_t  s_poll_last_v[kNumPollKinds][kNumParams];
std::atomic<uint32_t> s_poll_count[3][kNumParams];   // str, bmp, get
uint32_t s_poll_logged[3][kNumParams];               // the rates last reported

void log_polled(PollKind kind, Ev ev, uint8_t id, int32_t v) {
    if (id >= kNumParams) return;
    if (kind < 3) s_poll_count[kind][id].fetch_add(1, std::memory_order_relaxed);
    const uint64_t t = now_us();
    const bool first = s_poll_last_us[kind][id] == 0;
    const uint64_t gap = t - s_poll_last_us[kind][id];
    if (first || gap >= kPollQuietUs || v != s_poll_last_v[kind][id]) {
        const uint64_t gap_ms = gap / 1000u;
        log_ev(ev, id, v, first ? -1 : gap_ms > 0x7FFFFFFF ? 0x7FFFFFFF : int32_t(gap_ms));
    }
    s_poll_last_us[kind][id] = t;
    s_poll_last_v[kind][id] = v;
}

// ============================================================================
// State
// ============================================================================
int32_t s_val[kNumParams];        // the number the host last set
int32_t s_last_get[kNumParams];   // the number the unit last answered
bool    s_get_since_set[kNumParams];
int32_t s_shadow = 70;            // what Owned / OwnedS answer when asked
uint8_t s_preset = 0;
uint8_t s_lane = 0;

// The sequencer side
uint64_t s_last_gate_us = 0;
bool     s_restart = true;
uint8_t  s_step = 0;
uint8_t  s_last_vel = 0;
uint32_t s_gates = 0;
uint32_t s_tempo_last = 0;

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
uint8_t  s_group_first = 0;
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
// Files: the log, a save test, a directory listing — one writer at a time
// ============================================================================
namespace files {

const char kDir[]      = PROBE_DIR;
const char kLogPath[]  = PROBE_DIR "/seqprobe.log";
const char kSaveTmp[]  = PROBE_DIR "/seqprobe-save.tmp";
const char kSavePath[] = PROBE_DIR "/seqprobe-save.txt";

enum State { kWaiting = 0, kOk = 1, kFull = 2, kOff = 3 };   // < 0: -errno
std::atomic<int>  s_state{kWaiting};
std::atomic<bool> s_on{true};
std::atomic<bool> s_busy{false};
std::atomic<bool> s_thread_ok{false};

int      s_fd = -1;
long     s_size = 0;
bool     s_tests_done = false;
uint64_t s_last_sync_us = 0;
char     s_buf[8192];
size_t   s_n = 0;

void close_fd() {
    if (s_fd >= 0) { fsync(s_fd); close(s_fd); }
    s_fd = -1;
}

void flush_buf() {
    const char* p = s_buf;
    size_t n = s_n;
    s_n = 0;
    while (s_fd >= 0 && n > 0) {
        const ssize_t w = write(s_fd, p, n);
        if (w < 0) {
            if (errno == EINTR) continue;
            s_state = -errno;
            close_fd();
            return;
        }
        p += w; n -= size_t(w); s_size += long(w);
    }
}

void line(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
void line(const char* fmt, ...) {
    if (s_fd < 0) return;
    if (s_size + long(s_n) > kLogCap) {
        flush_buf();
        s_state = kFull;
        close_fd();
        return;
    }
    if (sizeof(s_buf) - s_n < 512) flush_buf();   // lines are far shorter
    va_list ap;
    va_start(ap, fmt);
    const int k = vsnprintf(s_buf + s_n, sizeof(s_buf) - s_n, fmt, ap);
    va_end(ap);
    if (k > 0) s_n += (size_t(k) < sizeof(s_buf) - s_n) ? size_t(k) : sizeof(s_buf) - s_n - 1;
}

const char* policy_name(int p) {
    switch (p) {
    case SCHED_OTHER: return "OTHER";
    case SCHED_FIFO:  return "FIFO";
    case SCHED_RR:    return "RR";
    default:          return "?";
    }
}

void format(const Rec& r) {
    const double t = double(int64_t(r.t_us - s_t0_us)) / 1e6;
    char head[64];
    snprintf(head, sizeof(head), "%10.3f f=%-9u tid=%-5u ", t, r.frame, r.tid);
    const char* name = r.ev < probe::kEvCount ? probe::kEvName[r.ev] : "?";
    switch (r.ev) {
    case probe::kEvInit:
        line("%sinit samplerate=%d frames_per_buffer=%u in=%u out=%u api=0x%08x\n", head, r.b,
             unsigned(r.d), unsigned(r.a >> 4), unsigned(r.a & 15), unsigned(r.c));
        break;
    case probe::kEvRenderStats:
        line("%srender %d blocks in %u s, %u frames each, longest gap between blocks %d us\n", head,
             r.b, unsigned(kStatsSeconds), unsigned(r.d), r.c);
        break;
    case probe::kEvPollStats:
        line("%spoll P%02u per second: str=%d bmp=%d get=%u\n", head, r.a, r.b, r.c, unsigned(r.d));
        break;
    case probe::kEvSetParam:
        line("%sset P%02u = %d%s\n", head, r.a, r.b, r.c ? "  (restore)" : "");
        break;
    case probe::kEvGetParam:
    case probe::kEvStr:
    case probe::kEvBmp:
        if (r.c < 0) line("%s%s P%02u v=%d  (first call)\n", head, name, r.a, r.b);
        else line("%s%s P%02u v=%d  (+%d ms since its last call)\n", head, name, r.a, r.b, r.c);
        break;
    case probe::kEvGetPresetName:
    case probe::kEvGetPresetIndex:
        if (r.c < 0) line("%s%s %d  (first call)\n", head, name, r.b);
        else line("%s%s %d  (+%d ms)\n", head, name, r.b, r.c);
        break;
    case probe::kEvTempo:
        line("%stempo %u.%03u bpm (raw 0x%08x)\n", head, unsigned(uint32_t(r.b) >> 16),
             unsigned((uint32_t(r.b) & 0xFFFFu) * 1000u / 65536u), unsigned(r.b));
        break;
    case probe::kEvNoteOn:     line("%snote_on note=%u vel=%d\n", head, r.a, r.b); break;
    case probe::kEvNoteOff:    line("%snote_off note=%u\n", head, r.a); break;
    case probe::kEvGateOn:     line("%sgate_on vel=%d -> step %d\n", head, r.b, r.c); break;
    case probe::kEvPitchBend:  line("%spitch_bend %d\n", head, r.b); break;
    case probe::kEvPressure:   line("%spressure %d\n", head, r.b); break;
    case probe::kEvAftertouch: line("%saftertouch note=%u %d\n", head, r.a, r.b); break;
    case probe::kEvLoadPreset: line("%sload_preset %u\n", head, r.a); break;
    case probe::kEvThreadInfo:
        line("%sTHREAD %s runs here: policy=%s priority=%d\n", head,
             r.a < probe::kEvCount ? probe::kEvName[r.a] : "?", policy_name(r.b), r.c);
        break;
    case probe::kEvWorker:
        if (r.a) line("%sworker thread started\n", head);
        else if (r.b < 0) line("%sworker: no pthread_create in this C library; flushing from callbacks\n", head);
        else line("%sworker: pthread_create failed (%d); flushing from callbacks\n", head, r.b);
        break;
    case probe::kEvEdit: {
        const uint32_t p = uint32_t(r.b);
        const char lane = char('A' + (p >> 24));
        const unsigned step = ((p >> 16) & 0xFF) + 1, from = (p >> 8) & 0xFF, to = p & 0xFF;
        if (r.d == 2)
            line("%sedit P%02u lane %c step %u: %u -> %u UNDONE (that turn was the start of a restore)\n",
                 head, r.a, lane, step, from, to);
        else
            line("%sedit P%02u lane %c step %u: %u -> %u (turn %+d, counted from the %s)\n", head, r.a,
                 lane, step, from, to, r.c, r.d ? "unit's get answer" : "host's last set");
        break;
    }
    case probe::kEvResync:
        line("%sresync P%02u: set %d, last set %d, last answer %u (jump too big for a turn)\n",
             head, r.a, r.b, r.c, unsigned(r.d));
        break;
    case probe::kEvBurst:
        line("%sRESTORE detected at set %d of a group (%u knob%s in %d us, first P%02u)%s\n", head,
             r.b, unsigned(r.d & 0xFF), (r.d & 0xFF) == 1 ? "" : "s", r.c, r.a,
             (r.d >> 8) ? ", right after load_preset" : "");
        break;
    case probe::kEvRestart:
        line("%srestart: next gate is step 1 (%s)\n", head, r.a ? "gap" : "stop event");
        break;
    default:
        line("%s%s\n", head, name);
        break;
    }
}

// What a song save will need: write a temporary file, flush it to storage,
// rename it over the real one (so a power cut keeps the old file), read it
// back.  Then what the folder holds, and how much room is left.
void file_tests() {
    char text[128];
    const int n = snprintf(text, sizeof(text), "seq_probe save test, pid %d\n", int(getpid()));
    int fd = open(kSaveTmp, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (fd < 0) {
        line("FILE save: open %s failed: errno %d (%s)\n", kSaveTmp, errno, strerror(errno));
    } else {
        const bool wrote = write(fd, text, size_t(n)) == n;
        const bool synced = fsync(fd) == 0;
        close(fd);
        if (!wrote || !synced) {
            line("FILE save: write %s, fsync %s\n", wrote ? "ok" : "FAILED", synced ? "ok" : "FAILED");
        } else if (rename(kSaveTmp, kSavePath) != 0) {
            line("FILE save: rename failed: errno %d (%s)\n", errno, strerror(errno));
        } else {
            char back[128];
            ssize_t got = -1;
            fd = open(kSavePath, O_RDONLY | O_CLOEXEC);
            if (fd >= 0) { got = read(fd, back, sizeof(back)); close(fd); }
            const bool same = got == n && std::memcmp(back, text, size_t(n)) == 0;
            line("FILE save: write + fsync + rename + read back: %s\n", same ? "OK" : "MISMATCH");
        }
    }

    DIR* d = opendir(kDir);
    if (!d) {
        line("FILE dir: opendir %s failed: errno %d (%s)\n", kDir, errno, strerror(errno));
    } else {
        int count = 0;
        while (const dirent* e = readdir(d)) {
            if (!std::strcmp(e->d_name, ".") || !std::strcmp(e->d_name, "..")) continue;
            if (count < 40) line("FILE dir: %c %s\n", e->d_type == DT_DIR ? 'd' : 'f', e->d_name);
            ++count;
        }
        closedir(d);
        line("FILE dir: %d entries in %s\n", count, kDir);
    }

    struct statvfs fs;
    if (statvfs(kDir, &fs) == 0) {
        const unsigned long long kb = 1024ull;
        line("FILE fs: %llu KB free of %llu KB, read-only %s\n",
             (unsigned long long)fs.f_bavail * fs.f_frsize / kb,
             (unsigned long long)fs.f_blocks * fs.f_frsize / kb,
             (fs.f_flag & ST_RDONLY) ? "yes" : "no");
    }
}

void open_log() {
    s_fd = open(kLogPath, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0644);
    if (s_fd < 0) { s_state = -errno; return; }
    struct stat st;
    s_size = (fstat(s_fd, &st) == 0) ? long(st.st_size) : 0;
    if (s_size >= kLogCap) { s_state = kFull; close_fd(); return; }
    s_state = kOk;
    line("\n=== seq_probe %u.%u session: pid %d, files written by the %s ===\n",
         unsigned(unit_header.version >> 16), unsigned((unit_header.version >> 8) & 0xFF),
         int(getpid()), s_thread_ok ? "worker thread" : "callbacks (no thread)");
    line("    seconds since unit_init | f= audio frames rendered | tid= thread | what happened\n");
}

// Drain the ring to the log.  Whoever gets s_busy does it; others skip.
void flush_once() {
    if (s_busy.exchange(true, std::memory_order_acquire)) return;
    if (!s_on.load(std::memory_order_relaxed)) {
        if (s_fd >= 0) { line("logging switched off (Files)\n"); flush_buf(); close_fd(); }
        s_ring.drain([](const Rec&) {});
        s_ring.take_dropped();
        s_state = kOff;
    } else {
        if (s_fd < 0 && s_state.load() == kWaiting) open_log();
        if (s_fd >= 0) {
            if (!s_tests_done) { file_tests(); s_tests_done = true; }
            s_ring.drain(format);
            if (const uint32_t dropped = s_ring.take_dropped())
                line("dropped %u records: the ring was full\n", dropped);
            flush_buf();
            const uint64_t t = now_us();
            if (s_fd >= 0 && t - s_last_sync_us > 2000000u) { fsync(s_fd); s_last_sync_us = t; }
        } else {
            s_ring.drain([](const Rec&) {});   // nowhere to write: keep the ring free
        }
    }
    s_busy.store(false, std::memory_order_release);
}

void close_log() {
    while (s_busy.exchange(true, std::memory_order_acquire)) {}
    if (s_fd >= 0) { line("=== teardown ===\n"); flush_buf(); close_fd(); }
    s_busy.store(false, std::memory_order_release);
}

// The worker: drains the ring every 100 ms, never on the audio thread.
pthread_t s_thread;
std::atomic<bool> s_thread_run{false};

void* worker_main(void*) {
    while (s_thread_run.load(std::memory_order_acquire)) {
        flush_once();
        timespec ts = {0, 100 * 1000 * 1000};
        nanosleep(&ts, nullptr);
    }
    flush_once();
    return nullptr;
}

void worker_start() {
    if (s_thread_ok.load()) return;
#ifdef PROBE_FORCE_NO_THREAD   // host test of the no-thread path
    const bool have_threads = false;
#else
    const bool have_threads = &pthread_create != nullptr;
#endif
    if (!have_threads) { log_ev(probe::kEvWorker, 0, -1); return; }
    s_thread_run.store(true);
    const int err = pthread_create(&s_thread, nullptr, worker_main, nullptr);
    if (err == 0) s_thread_ok.store(true);
    else s_thread_run.store(false);
    log_ev(probe::kEvWorker, err == 0 ? 1 : 0, err);
}

void worker_stop() {
    if (!s_thread_ok.load()) { flush_once(); return; }
    s_thread_run.store(false, std::memory_order_release);
    if (&pthread_join != nullptr) pthread_join(s_thread, nullptr);
    s_thread_ok.store(false);
}

// Without a worker, the callbacks that are not the audio thread flush.
uint32_t s_render_tid = 0;
uint64_t s_last_inline_us = 0;

void maybe_flush_inline() {
    if (s_thread_ok.load(std::memory_order_relaxed)) return;
    const uint64_t t = now_us();
    if (t - s_last_inline_us < 250000u) return;
    if (thread_id() == s_render_tid) return;
    s_last_inline_us = t;
    flush_once();
}

}  // namespace files

// ============================================================================
// Sound: a tick on every gate, a blip per lane step, a tone per MIDI note
// ============================================================================
struct VoiceReq {
    std::atomic<bool> fire{false};
    float freq, amp, decay_s;
    bool noise;
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
constexpr size_t kStrLen = 24;
char      s_str[kNumParams][kStrLen];
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

const char* files_text(char* s) {
    if (!files::s_on.load()) return "off";
    const int st = files::s_state.load();
    switch (st) {
    case files::kWaiting: return "wait";
    case files::kOk:      return files::s_thread_ok.load() ? "ok T" : "ok -";
    case files::kFull:    return "full";
    case files::kOff:     return "off";
    default:              snprintf(s, kStrLen, "e%d", -st); return s;
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
        s_group_restore = false; s_npending = 0; s_group_first = id;
    }
    s_set_last_us = t;
    ++s_group_n;
    s_group_ids |= 1u << id;
    const bool after_preset = (t - s_preset_us) < kPresetRestoreUs;
    int ids = 0;
    for (uint32_t m = s_group_ids; m; m &= m - 1) ++ids;
    if (!s_group_restore && (ids >= kBurstIds || after_preset)) {
        s_group_restore = true;
        for (uint8_t i = 0; i < s_npending; ++i) {
            const PendingEdit& e = s_pending[i];
            const uint8_t now = s_steps[e.lane][e.step];
            s_steps[e.lane][e.step] = e.prev;
            log_ev(probe::kEvEdit, uint8_t(P_STEP1 + e.step),
                   int32_t((uint32_t(e.lane) << 24) | (uint32_t(e.step) << 16) | (uint32_t(now) << 8) | e.prev),
                   0, 2);
        }
        s_npending = 0;
        log_ev(probe::kEvBurst, s_group_first, s_group_n, int32_t(t - s_group_start_us),
               uint16_t(ids | (after_preset ? 0x100 : 0)));
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
    uint16_t refkind = 0;
    if (asked && std::abs(v - s_last_get[id]) < std::abs(v - last_set)) {
        ref = s_last_get[id];
        refkind = 1;
    }
    const int32_t d = v - ref;
    if (d == 0) return;
    if (d > kMaxStepDelta || d < -kMaxStepDelta) {
        log_ev(probe::kEvResync, id, v, last_set, uint16_t(s_last_get[id]));
        return;
    }
    const uint8_t lane = s_lane, step = uint8_t(id - P_STEP1);
    const int old = s_steps[lane][step];
    int nv = old + d;
    if (nv < 0) nv = 0;
    if (nv > kLanes[lane].count) nv = kLanes[lane].count;
    s_steps[lane][step] = uint8_t(nv);
    if (s_npending < 4) s_pending[s_npending++] = {lane, step, uint8_t(old)};
    log_ev(probe::kEvEdit, id,
           int32_t((uint32_t(lane) << 24) | (uint32_t(step) << 16) | (uint32_t(old) << 8) | uint32_t(nv)),
           d, refkind);
}

void reset_state() {
    for (uint8_t i = 0; i < kNumParams; ++i) {
        s_val[i] = unit_header.params[i].init;
        s_last_get[i] = 0;
        s_get_since_set[i] = false;
    }
    for (int k = 0; k < kNumPollKinds; ++k)
        for (int i = 0; i < kNumParams; ++i) { s_poll_last_us[k][i] = 0; s_poll_last_v[k][i] = 0; }
    for (int k = 0; k < 3; ++k)
        for (int i = 0; i < kNumParams; ++i) { s_poll_count[k][i].store(0); s_poll_logged[k][i] = 0; }
    for (int e = 0; e < probe::kEvCount; ++e) s_seen[e].store(false);
    s_shadow = s_val[P_SHADOW] * 10;
    s_lane = 0;
    s_preset = 0;
    s_restart = true;
    s_gates = 0;
    s_set_last_us = s_preset_us = 0;
    s_last_burst_n = 0;
    s_event_prev = s_event_last = "-";
}

// Render-side bookkeeping, every kStatsSeconds of audio: render cadence, and
// the panel's call rates (per second) for each knob whose rate changed.
uint32_t s_blocks = 0, s_stats_frames = 0, s_max_gap_us = 0;
uint64_t s_last_render_us = 0;

void render_stats(uint32_t frames) {
    const uint64_t t = now_us();
    if (s_last_render_us) {
        const uint64_t gap = t - s_last_render_us;
        if (gap > s_max_gap_us) s_max_gap_us = uint32_t(gap);
    }
    s_last_render_us = t;
    ++s_blocks;
    s_stats_frames += frames;
    if (s_stats_frames < kStatsSeconds * s_sr) return;
    log_ev(probe::kEvRenderStats, 0, int32_t(s_blocks), int32_t(s_max_gap_us), uint16_t(frames));
    for (uint8_t id = 0; id < kNumParams; ++id) {
        uint32_t rate[3];
        bool changed = false;
        for (int k = 0; k < 3; ++k) {
            const uint32_t n = s_poll_count[k][id].exchange(0, std::memory_order_relaxed);
            rate[k] = (n + kStatsSeconds / 2) / kStatsSeconds;
            const uint32_t was = s_poll_logged[k][id];
            if ((rate[k] == 0) != (was == 0) || rate[k] > was + 2 || rate[k] + 2 < was) changed = true;
        }
        if (!changed) continue;
        for (int k = 0; k < 3; ++k) s_poll_logged[k][id] = rate[k];
        log_ev(probe::kEvPollStats, id, int32_t(rate[0]), int32_t(rate[1]), uint16_t(rate[2]));
    }
    s_blocks = 0; s_stats_frames = 0; s_max_gap_us = 0;
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

    s_sr = desc->samplerate ? desc->samplerate : 48000;
    s_frames.store(0);
    s_t0_us = now_us();
    reset_state();
    silence();
    thread_info_once(probe::kEvInit);
    log_ev(probe::kEvInit, uint8_t((desc->input_channels << 4) | (desc->output_channels & 15)),
           int32_t(desc->samplerate), int32_t(desc->api), desc->frames_per_buffer);
    files::s_state.store(files::kWaiting);
    files::s_tests_done = false;
    files::worker_start();
    return k_unit_err_none;
}

__unit_callback void unit_teardown() {
    thread_info_once(probe::kEvTeardown);
    log_ev(probe::kEvTeardown);
    files::worker_stop();   // the worker flushes once more on its way out
    files::close_log();
}

__unit_callback void unit_reset() {
    thread_info_once(probe::kEvReset);
    log_ev(probe::kEvReset);
    note_event("Rset");
    s_restart = true;
    silence();
}

__unit_callback void unit_resume() {
    thread_info_once(probe::kEvResume);
    log_ev(probe::kEvResume);
    note_event("Resm");
}

__unit_callback void unit_suspend() {
    thread_info_once(probe::kEvSuspend);
    log_ev(probe::kEvSuspend);
    note_event("Susp");
    silence();
}

__unit_callback void unit_render(const float* in, float* out, uint32_t frames) {
    (void)in;
    if (files::s_render_tid == 0) {
        files::s_render_tid = thread_id();
        thread_info_once(probe::kEvRenderStats);
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
    render_stats(frames);
}

__unit_callback void unit_set_param_value(uint8_t id, int32_t value) {
    thread_info_once(probe::kEvSetParam);
    if (id >= kNumParams) return;
    const uint64_t t = now_us();
    const bool restore = in_restore(id, t);
    log_ev(probe::kEvSetParam, id, value, restore ? 1 : 0);
    const int32_t last_set = s_val[id];
    s_val[id] = value;
    switch (id) {
    case P_SHADOW:
        if (!restore) s_shadow = value * 10;   // a restore must not overwrite the preset test
        break;
    case P_FILES:
        files::s_on.store(value != 0);
        if (value && files::s_state.load() != files::kOk) files::s_state.store(files::kWaiting);
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
    files::maybe_flush_inline();
}

__unit_callback int32_t unit_get_param_value(uint8_t id) {
    thread_info_once(probe::kEvGetParam);
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
    log_polled(kPollGet, probe::kEvGetParam, id, v);
    files::maybe_flush_inline();
    return v;
}

__unit_callback const char* unit_get_param_str_value(uint8_t id, int32_t value) {
    thread_info_once(probe::kEvStr);
    if (id >= kNumParams) return "";
    log_polled(kPollStr, probe::kEvStr, id, value);
    files::maybe_flush_inline();
    char* s = s_str[id];
    switch (id) {
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
        snprintf(s, kStrLen, "%u/%ums", unsigned(s_last_burst_n), unsigned(s_last_burst_span_us / 1000u));
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
    default:
        return "-";
    }
}

__unit_callback const uint8_t* unit_get_param_bmp_value(uint8_t id, int32_t value) {
    thread_info_once(probe::kEvBmp);
    if (id >= kNumParams) return nullptr;
    log_polled(kPollBmp, probe::kEvBmp, id, value);
    files::maybe_flush_inline();
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
    thread_info_once(probe::kEvTempo);
    // Externally synced, this can come often: only changes are logged.
    if (tempo != s_tempo_last) {
        s_tempo_last = tempo;
        log_ev(probe::kEvTempo, 0, int32_t(tempo));
    }
}

__unit_callback void unit_note_on(uint8_t note, uint8_t velocity) {
    thread_info_once(probe::kEvNoteOn);
    log_ev(probe::kEvNoteOn, note, velocity);
    snprintf(s_note_name, sizeof(s_note_name), "N%u", unsigned(note));
    note_event(s_note_name);
    request(kVoiceNote, 440.0f * powf(2.0f, (float(note) - 69.0f) / 12.0f),
            0.3f * float(velocity) / 127.0f, 0.3f);
}

__unit_callback void unit_note_off(uint8_t note) {
    thread_info_once(probe::kEvNoteOff);
    log_ev(probe::kEvNoteOff, note);
}

__unit_callback void unit_gate_on(uint8_t velocity) {
    thread_info_once(probe::kEvGateOn);
    const uint64_t t = now_us();
    if (s_restart || t - s_last_gate_us > kRestartGapUs) {
        if (!s_restart) log_ev(probe::kEvRestart, 1);
        s_step = 0;
    } else {
        s_step = uint8_t((s_step + 1) & 15);
    }
    s_restart = false;
    s_last_gate_us = t;
    s_last_vel = velocity;
    ++s_gates;
    log_ev(probe::kEvGateOn, 0, velocity, s_step + 1);
    play_step(velocity);
}

__unit_callback void unit_gate_off() {
    thread_info_once(probe::kEvGateOff);
    log_ev(probe::kEvGateOff);
}

__unit_callback void unit_all_note_off() {
    thread_info_once(probe::kEvAllNoteOff);
    log_ev(probe::kEvAllNoteOff);
    note_event("Aoff");
    if (!s_restart) log_ev(probe::kEvRestart, 0);
    s_restart = true;
    silence();
}

__unit_callback void unit_pitch_bend(uint16_t bend) {
    thread_info_once(probe::kEvPitchBend);
    log_ev(probe::kEvPitchBend, 0, bend);
}

__unit_callback void unit_channel_pressure(uint8_t pressure) {
    thread_info_once(probe::kEvPressure);
    log_ev(probe::kEvPressure, 0, pressure);
}

__unit_callback void unit_aftertouch(uint8_t note, uint8_t aftertouch) {
    thread_info_once(probe::kEvAftertouch);
    log_ev(probe::kEvAftertouch, note, aftertouch);
}

__unit_callback void unit_load_preset(uint8_t idx) {
    thread_info_once(probe::kEvLoadPreset);
    s_preset_us = now_us();
    log_ev(probe::kEvLoadPreset, idx);
    if (idx < 2) {
        s_preset = idx;
        s_shadow = idx == 0 ? 20 : 90;   // Owned should show this if the panel re-reads
        note_event(idx == 0 ? "PreA" : "PreB");
    }
}

__unit_callback uint8_t unit_get_preset_index() {
    thread_info_once(probe::kEvGetPresetIndex);
    log_polled(kPollPresetIndex, probe::kEvGetPresetIndex, 0, s_preset);
    return s_preset;
}

__unit_callback const char* unit_get_preset_name(uint8_t idx) {
    thread_info_once(probe::kEvGetPresetName);
    log_polled(kPollPresetName, probe::kEvGetPresetName, idx < kNumParams ? idx : 0, idx);
    return idx == 0 ? "ProbeA" : idx == 1 ? "ProbeB" : nullptr;
}
