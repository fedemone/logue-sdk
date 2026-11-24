/**
 * @file unit.cc
 * @brief drumlogue SDK unit interface — Braquetti
 *
 * A three-lane step sequencer playing Brachetti's instruments (PLAN.md has
 * the design and the hardware findings it rests on):
 *
 *   - seq_core.h: the lanes, the knobs that edit them, the clock;
 *   - catalog.h:  Brachetti's family table, i.e. its twelve family engines
 *     (one ring-over pair each), one family per lane;
 *   - limits.h:   what each instrument costs, the CPU budget, the step
 *     limits that refuse a step too heavy for it.
 *
 * Every Multi-part gate is one step.  Its hits (lane, family, instrument)
 * go through a lock-free queue to the audio thread, which at the start of a
 * block sets each family's Program and strikes it, exactly as Brachetti's
 * gate does; every engine call happens there, whatever thread the
 * drumlogue's callbacks come on.  Each family still sounding is rendered and
 * summed, at the level of the lane that struck it last, then a soft
 * limiter.
 *
 * The CPU (limits.h): every live voice counts at its instrument's measured
 * cost, and before a strike that would go over the budget, older voices
 * fade (~10 ms) — never a hit of the same step: the step limits keep what
 * one step strikes within the budget, so fading older voices always makes
 * room.  And a strike is the costliest moment of a voice (a Timpani strike
 * block costs about twice its next ones, test/cpu_blocks.sh), so a block
 * carries at most one strike: the other lanes' hits of the same step follow
 * in the next blocks, 1.3 ms apart.
 */

#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <time.h>

#include "unit.h"
#include "catalog.h"
#include "limits.h"
#include "seq_core.h"

// Optional: a weak undefined symbol resolves to null instead of failing the
// load; without it the clock falls back to the audio frame count.
#pragma weak clock_gettime

namespace {

using brachetti::Family;
using brachetti::kFamilies;
using brachetti::kNumFamilies;

constexpr size_t   kChunk          = 64;      // frames per family render (stack buffer)

constexpr float    kKnee           = 0.9f;    // the soft limiter starts here

brq::Sequencer<brq::BrachettiCatalog, brq::CpuLimits> s_seq;
brq::HitQueue s_hits;

uint32_t s_sr = 48000;
std::atomic<uint32_t> s_frames;               // frames rendered: the clock without clock_gettime

uint64_t now_ns() {
    if (&clock_gettime == nullptr) return 0;
    timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return 0;
    return uint64_t(ts.tv_sec) * 1000000000u + uint64_t(ts.tv_nsec);
}

uint64_t now_us() {
    if (&clock_gettime != nullptr) {
        timespec ts;
        if (clock_gettime(CLOCK_MONOTONIC, &ts) == 0)
            return uint64_t(ts.tv_sec) * 1000000u + uint64_t(ts.tv_nsec) / 1000u;
    }
    return uint64_t(s_frames.load(std::memory_order_relaxed)) * 1000000u / s_sr;
}

// Requests for the audio thread
std::atomic<bool> s_reset_req;
std::atomic<bool> s_all_off_req;

// The families the last gate struck, for its gate-off, and the families
// released, for the audio thread.  Gate-on and gate-off may come on two
// threads (PLAN.md, finding 7), so a release does not go through the hit
// queue (one producer): it is a mask, applied once the queue's strikes are
// done.
std::atomic<uint32_t> s_gate_families;
std::atomic<uint32_t> s_released;

// The CPU meter (page 6): every block timed with the clock.  Per mille of
// the block's period, over the last second; the peak and the late blocks
// since the last Stop.  And the renders that went wrong: a family whose
// output is not a finite number is reset and silenced for that block, so
// it never reaches the drumlogue's mix (counted since the unit loaded).
constexpr uint32_t kMeterBlocks = 750;        // a second of 64-frame blocks
std::atomic<uint64_t> s_last_entry_ns;        // written by the audio thread
uint64_t s_win_busy_ns, s_win_family_ns[kNumFamilies];
uint32_t s_win_blocks;
std::atomic<uint32_t> s_cpu_pm;               // the last second's mean
std::atomic<uint32_t> s_peak_pm;              // the heaviest block since Stop
std::atomic<uint32_t> s_late;                 // blocks called over half a period late, since Stop
std::atomic<uint32_t> s_family_pm[kNumFamilies];
std::atomic<uint32_t> s_bad;                  // family renders reset for a NaN or an infinity

// Page 6's knobs: the meter, and the Limit (per cent of the CPU budget).
constexpr uint8_t P_CPU = brq::P_OVERVIEW, P_LATE = P_CPU + 1, P_LANE_CPU = P_CPU + 2, P_LIMIT = P_CPU + 3;

// A NaN or an infinity, by its bits: -ffast-math may fold std::isfinite away.
inline bool not_finite(float x) {
    uint32_t u;
    std::memcpy(&u, &x, sizeof(u));
    return (u & 0x7F800000u) == 0x7F800000u;
}

// Audio thread: whether this block has struck already.
bool s_struck_this_block;

// Per family (audio thread): its output level now and where it goes; the
// instrument it was struck with last and the one before (step codes:
// program + 1), whose ring-over copy may still ring; the blocks since its
// last strike.
float   s_gain[kNumFamilies];
float   s_gain_target[kNumFamilies];
uint8_t s_code[kNumFamilies];
uint8_t s_prev_code[kNumFamilies];
uint8_t s_age[kNumFamilies];
constexpr uint8_t kAgeMax = 255;
// Per family (audio thread): struck since it last fell silent, so rendered.
bool    s_active[kNumFamilies];

int live(int i) { return kFamilies[i].live_voices(kFamilies[i].engine); }

int total_live_voices() {
    int n = 0;
    for (int i = 0; i < kNumFamilies; ++i) n += live(i);
    return n;
}

// A voice of family i counts at its instrument's cost, or at the costlier
// of the two while the previous instrument may still ring: one voice per
// lane fades it within ~10 ms of the strike (8 blocks).
constexpr uint8_t kFadeBlocks = 8;
int voice_cost(int i) {
    const int a = brq::cost(i, s_code[i]);
    if (s_age[i] >= kFadeBlocks) return a;
    const int b = brq::cost(i, s_prev_code[i]);
    return a > b ? a : b;
}

// The voices sounding, at their instruments' cost.
int load() {
    int n = 0;
    for (int i = 0; i < kNumFamilies; ++i) n += live(i) * voice_cost(i);
    return n;
}

// A family struck in the last kLanes blocks holds a hit of this step: its
// newest voice.  Its other voices are older.
int older_voices(int i) {
    const int n = live(i);
    return (s_age[i] < brq::kLanes && n > 0) ? n - 1 : n;
}

// Before a strike: until it fits the budget, the family whose older voices
// cost the most fades its oldest (the ~10 ms preset-change fade, which no
// longer counts as a live voice).  A hit of this step is never faded: the
// step limits keep the step within the budget, so if only those sound the
// strike goes ahead.  Within a family, its ring-over keeps two instruments
// inside its own Poly.
void make_room(int family, int code) {
    const int need = brq::cost(family, code);
    for (int guard = 0; guard < 24 && load() + need > brq::budget(); ++guard) {
        int victim = -1, most = 0;
        for (int i = 0; i < kNumFamilies; ++i) {
            const int c = older_voices(i) * voice_cost(i);
            if (c > most) { most = c; victim = i; }
        }
        if (victim < 0) break;
        kFamilies[victim].fade_oldest(kFamilies[victim].engine);
    }
}

// One hit, on the audio thread: Program, then the gate, as Brachetti's
// Program knob and gate do.
void strike(const brq::Hit& h) {
    const Family& f = kFamilies[h.family];
    const float target = float(s_seq.level(h.lane)) / float(brq::kLevelMax);
    const uint8_t code = uint8_t(h.program + 1);
    if (!f.sounding(f.engine) && live(h.family) == 0) {
        s_gain[h.family] = target;            // nothing to glide from
        s_prev_code[h.family] = 0;            // nor anything ringing on
    }
    s_gain_target[h.family] = target;
    make_room(h.family, code);
    if (code != s_code[h.family]) {
        s_prev_code[h.family] = s_code[h.family];
        s_code[h.family] = code;
    }
    s_age[h.family] = 0;
    s_active[h.family] = true;
    f.set(f.engine, f.slot[brc::CommonLayout::k_paramProgram], h.program);
    f.gate_on(f.engine, h.velocity);
}

inline float soft_limit(float x) {
    const float a = std::fabs(x);
    if (a <= kKnee) return x;
    const float y = kKnee + (1.0f - kKnee) * std::tanh((a - kKnee) / (1.0f - kKnee));
    return x < 0.0f ? -y : y;
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
    for (const Family& f : kFamilies) {
        f.set_header(f.engine, f.hdr);   // the family unit's knob steps, as Brachetti does
        const int8_t err = f.init(f.engine, desc);
        if (err != k_unit_err_none) return err;
        // One voice per lane: a lane's new hit fades its previous one, as
        // Brachetti's own Poly 1 does (the Kernel has no Poly: one kettle).
        const uint8_t poly = f.slot[brc::CommonLayout::k_paramPoly];
        if (poly != brachetti::kNone) f.set(f.engine, poly, 1);
    }
    for (int i = 0; i < kNumFamilies; ++i) {
        s_gain[i] = s_gain_target[i] = 1.0f;
        s_code[i] = s_prev_code[i] = 0;
        s_age[i] = kAgeMax;
        s_active[i] = false;
    }
    s_seq.init();
    brq::Hit h;
    while (s_hits.pop(h)) {}
    s_gate_families.store(0);
    s_released.store(0);
    s_last_entry_ns.store(0);
    s_win_busy_ns = 0;
    s_win_blocks = 0;
    for (int i = 0; i < kNumFamilies; ++i) { s_win_family_ns[i] = 0; s_family_pm[i].store(0); }
    s_cpu_pm.store(0);
    s_peak_pm.store(0);
    s_late.store(0);
    s_bad.store(0);
    brq::s_limit_pct.store(brq::kLimitMax);
    s_reset_req.store(false);
    s_all_off_req.store(false);
    s_frames.store(0);
    return k_unit_err_none;
}

__unit_callback void unit_teardown() { for (const Family& f : kFamilies) f.teardown(f.engine); }
__unit_callback void unit_resume()   { for (const Family& f : kFamilies) f.resume(f.engine); }
__unit_callback void unit_suspend()  { for (const Family& f : kFamilies) f.suspend(f.engine); }

// Stop sends a reset: the next gate is step 1, and the engines are flushed.
__unit_callback void unit_reset() {
    s_seq.reset();
    s_reset_req.store(true, std::memory_order_release);
}

__unit_callback void unit_render(const float* in, float* out, uint32_t frames) {
    (void)in;
    const uint64_t t0 = now_ns();
    const uint64_t period_ns = uint64_t(frames) * 1000000000u / s_sr;
    const uint64_t last = s_last_entry_ns.load(std::memory_order_relaxed);
    if (t0 && last && t0 - last > period_ns + period_ns / 2) s_late.fetch_add(1, std::memory_order_relaxed);
    s_last_entry_ns.store(t0, std::memory_order_relaxed);
    // A reset flushes the engines, then the queue plays: a gate after the
    // reset (Play right after Stop) must not be lost with it.
    const bool reset = s_reset_req.exchange(false, std::memory_order_acquire);
    if (reset) {
        for (const Family& f : kFamilies) f.reset(f.engine);
        s_peak_pm.store(0, std::memory_order_relaxed);
        s_late.store(0, std::memory_order_relaxed);
    }
    if (s_all_off_req.exchange(false, std::memory_order_acquire))
        for (const Family& f : kFamilies) f.all_note_off(f.engine);
    for (int i = 0; i < kNumFamilies; ++i)
        if (s_age[i] < kAgeMax) ++s_age[i];
    // Releases as they come; at most one strike per block (the rest wait,
    // in order, for the next blocks).
    brq::Hit h;
    while (!s_struck_this_block && s_hits.pop(h)) {
        strike(h);
        s_struck_this_block = true;
    }
    s_struck_this_block = false;
    // A gate's release, once its strikes are done.
    if (!s_hits.peek(h)) {
        const uint32_t released = s_released.exchange(0, std::memory_order_acquire);
        for (int i = 0; i < kNumFamilies; ++i)
            if (released & (1u << i)) kFamilies[i].gate_off(kFamilies[i].engine);
    }

    std::memset(out, 0, sizeof(float) * 2 * frames);
    float tmp[kChunk * 2];
    for (int i = 0; i < kNumFamilies; ++i) {
        const Family& f = kFamilies[i];
        // A family struck renders while it sounds or holds a live voice.
        // "Sounding" is measured on the output (brachetti_common/ring_over.h),
        // and an instrument may start silent (the Rainstk's first 9 ms):
        // skipped then, it would never start.
        if (!s_active[i]) continue;
        if (!f.sounding(f.engine) && live(i) == 0) { s_active[i] = false; continue; }
        const uint64_t f0 = t0 ? now_ns() : 0;
        for (uint32_t done = 0; done < frames; done += kChunk) {
            const uint32_t n = (frames - done < kChunk) ? frames - done : uint32_t(kChunk);
            f.render(f.engine, tmp, n);
            bool bad = false;
            for (uint32_t k = 0; k < 2 * n; ++k) bad |= not_finite(tmp[k]);
            if (bad) {                        // silenced, and the family starts afresh
                std::memset(tmp, 0, sizeof(float) * 2 * n);
                f.reset(f.engine);
                s_bad.fetch_add(1, std::memory_order_relaxed);
            }
            // A level change glides over one chunk.
            const float g0 = s_gain[i], g1 = s_gain_target[i];
            if (g0 == g1) {
                for (uint32_t k = 0; k < 2 * n; ++k) out[2 * done + k] += g0 * tmp[k];
            } else {
                const float step = (g1 - g0) / float(n);
                for (uint32_t k = 0; k < n; ++k) {
                    const float g = g0 + step * float(k + 1);
                    out[2 * (done + k)]     += g * tmp[2 * k];
                    out[2 * (done + k) + 1] += g * tmp[2 * k + 1];
                }
                s_gain[i] = g1;
            }
        }
        if (f0) s_win_family_ns[i] += now_ns() - f0;
    }
    for (uint32_t k = 0; k < 2 * frames; ++k) out[k] = soft_limit(out[k]);
    s_frames.fetch_add(frames, std::memory_order_relaxed);

    // The meter.
    if (t0 && period_ns) {
        const uint64_t busy = now_ns() - t0;
        const uint32_t pm = uint32_t(busy * 1000u / period_ns);
        if (!reset && pm > s_peak_pm.load(std::memory_order_relaxed))   // a Stop's flush is not playing
            s_peak_pm.store(pm, std::memory_order_relaxed);
        s_win_busy_ns += busy;
        if (++s_win_blocks >= kMeterBlocks) {
            const uint64_t span = uint64_t(s_win_blocks) * period_ns;
            s_cpu_pm.store(uint32_t(s_win_busy_ns * 1000u / span), std::memory_order_relaxed);
            for (int i = 0; i < kNumFamilies; ++i) {
                s_family_pm[i].store(uint32_t(s_win_family_ns[i] * 1000u / span), std::memory_order_relaxed);
                s_win_family_ns[i] = 0;
            }
            s_win_busy_ns = 0;
            s_win_blocks = 0;
        }
    }
}

// ── The sequencer ───────────────────────────────────────────────────────────
// Each hit strikes at its step's velocity: 127 accented in the Accent
// layer, else 64.  The gate's own velocity only marks the bar (an accent on
// the Multi part's step 1).
__unit_callback void unit_gate_on(uint8_t velocity) {
    brq::Hit hits[brq::kLanes];
    const int n = s_seq.gate_on(velocity, hits);
    uint32_t families = 0;
    for (int i = 0; i < n; ++i) {
        s_hits.push(hits[i]);
        families |= 1u << hits[i].family;
    }
    s_gate_families.store(families, std::memory_order_relaxed);
}

// The gate's release goes to the families it struck, as on Brachetti.
__unit_callback void unit_gate_off() {
    s_released.fetch_or(s_gate_families.exchange(0, std::memory_order_relaxed), std::memory_order_release);
}

__unit_callback void unit_all_note_off() { s_all_off_req.store(true, std::memory_order_release); }

// MIDI notes, pitch bend, pressure: not used (the lanes play the gates).
__unit_callback void unit_note_on(uint8_t note, uint8_t velocity) { (void)note; (void)velocity; }
__unit_callback void unit_note_off(uint8_t note) { (void)note; }
__unit_callback void unit_pitch_bend(uint16_t bend) { (void)bend; }
__unit_callback void unit_channel_pressure(uint8_t pressure) { (void)pressure; }
__unit_callback void unit_aftertouch(uint8_t note, uint8_t aftertouch) { (void)note; (void)aftertouch; }
__unit_callback void unit_set_tempo(uint32_t tempo) { (void)tempo; }

// ── The knobs ───────────────────────────────────────────────────────────────
__unit_callback void unit_set_param_value(uint8_t id, int32_t value) {
#ifndef BRQ_OVERVIEW
    if (id == P_LIMIT) {   // absolute, like Lane: 5-10, tens of per cent; the steps checked again
        const int pct = brq::clampi(int(value) * 10, brq::kLimitMin, brq::kLimitMax);
        if (pct != brq::s_limit_pct.load(std::memory_order_relaxed)) {
            brq::s_limit_pct.store(pct, std::memory_order_relaxed);
            s_seq.recheck_all();
        }
    }
#endif
    s_seq.set_param(id, value, now_us());
}
__unit_callback int32_t unit_get_param_value(uint8_t id) { return s_seq.get_param(id); }
#ifndef BRQ_OVERVIEW
// Page 6: the CPU meter, in per cent of an audio block's period, and the
// Limit.  The panel reads a knob's text when it turns or the page is
// entered, so a turn reads the meter again.
static const char* meter_text(uint8_t id, int32_t value) {
    static char text[brq::kOverviewKnobs][16];
    char* t = text[id - P_CPU];
    if (id == P_LIMIT) {
        snprintf(t, 16, "%d%%", brq::clampi(int(value) * 10, brq::kLimitMin, brq::kLimitMax));
        return t;
    }
    const uint64_t now = now_ns();
    if (!now) return "noclk";
    if (now - s_last_entry_ns.load(std::memory_order_relaxed) > 200000000u) return "stalled";   // no render for 0.2 s
    const unsigned pct = unsigned(s_cpu_pm.load() + 5) / 10, peak = unsigned(s_peak_pm.load() + 5) / 10;
    switch (id) {
    case P_CPU:  snprintf(t, 16, "%u/%u%%", pct > 999 ? 999 : pct, peak > 999 ? 999 : peak); break;
    case P_LATE: snprintf(t, 16, "L%u N%u", unsigned(s_late.load()) % 1000, unsigned(s_bad.load()) % 1000); break;
    default: {
        const int lane = s_seq.selected_lane();
        snprintf(t, 16, "%c %u%%", char('A' + lane), unsigned(s_family_pm[s_seq.family(lane)].load() + 5) / 10);
    }
    }
    return t;
}
#endif

__unit_callback const char* unit_get_param_str_value(uint8_t id, int32_t value) {
#ifndef BRQ_OVERVIEW
    if (brq::is_overview(id)) return meter_text(id, value);
#endif
    return s_seq.param_text(id, value);
}
__unit_callback const uint8_t* unit_get_param_bmp_value(uint8_t id, int32_t value) {
    (void)value;
    return s_seq.param_bitmap(id);
}

// No presets of its own: a program load still marks the restore (seq_core.h).
__unit_callback void unit_load_preset(uint8_t idx) { (void)idx; s_seq.preset_loaded(now_us()); }
__unit_callback uint8_t unit_get_preset_index() { return 0; }
__unit_callback const char* unit_get_preset_name(uint8_t idx) { (void)idx; return nullptr; }

#ifdef BRAQUETTI_HOST_TEST
// For test/unit_test.cc: the voices sounding across the unit's families,
// their cost, the budget.
int braquetti_test_live_voices() { return total_live_voices(); }
int braquetti_test_load() { return load(); }
int braquetti_test_budget() { return brq::budget(); }
int braquetti_test_bad_renders() { return int(s_bad.load()); }
#endif
