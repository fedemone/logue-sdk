#pragma once
// Braquetti — the sequencer core: three lanes of 16 steps, the knobs that
// edit them, the clock that plays them.  No sound, no SDK calls, no clock of
// its own (the caller passes the time), no heap: host-testable, and a unit
// wraps it (PLAN.md has the design and the hardware findings it rests on).
//
//   Knobs (the order header.c will declare):
//     P00-P15  steps 1-16 of the selected lane (pages 1-4, a beat per page),
//              in the layer P19 shows: instrument, probability or accent
//     P16      Lane: which lane page 5 and the steps show (A, B, C)
//     P17      Family of the selected lane
//     P18      Level of the selected lane
//     P19      Layer: what the steps edit (Inst, Prob, Accent), then two
//              actions, clear the lane and clear all lanes
//     P20-P23  page 6: the unit's (its CPU meter); with BRQ_OVERVIEW, four
//              bitmaps of all three lanes, steps 1-4, 5-8, 9-12, 13-16
//
//   Steps, Family and Level are read as turns: the change from the number
//   the panel last knew, applied to what the lane holds.  The panel keeps
//   its own number and re-reads the unit's only after a program or preset
//   load (PLAN.md, finding 3), so its number is never the step's content.
//
//   A program load sets every knob in a row.  A group of sets touching 3+
//   knobs within 5 ms, or any set within 50 ms of a preset load, is a
//   restore: it edits nothing, and an edit made at the start of the group is
//   undone (finding 4).
//
//   Clock: one gate per step; an accented gate (velocity >= 100: the
//   drumlogue sends 127 for an accent, 64 otherwise) is step 1, and so is
//   the first gate after a reset (stop sends one).  Finding 5, 6.  A step
//   plays with its probability (25-100 %), at velocity 127 if accented in
//   the Accent layer, else 64.
//
//   Layer is absolute, like Lane: a view, which a program load may set.  Its
//   two actions run only on a deliberate click: one click right (not a
//   jump), at least kConfirmUs after landing on the value before, and not
//   in a program load.
//
//   Limits (hardware test: two lanes on one family stopped the audio):
//   - One family per lane.  The Family knob browses every family; one that
//     another lane plays shows "!!!" and the lane keeps the family it has.
//   - A Policy may refuse an instrument on a step (too heavy with what the
//     other lanes play: limits.h).  The step knob still points at it, but
//     the step plays nothing and shows "!!!"; the next click is checked
//     again.  A lane's steps are checked again, in order, when its family
//     changes.
//
// Threads: knob calls, gates and render may each come on their own thread
// (finding 7).  Lane contents are atomics; gates read them, knobs write
// them.  gate_on() returns the hits; the unit hands them to the audio thread
// through a HitQueue.
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace brq {

constexpr int kLanes = 3;
constexpr int kSteps = 16;

enum : uint8_t {
    P_STEP1 = 0,
    P_STEP16 = 15,
    P_LANE = 16,
    P_FAMILY = 17,
    P_LEVEL = 18,
    P_LAYER = 19,
    P_OVERVIEW = 20,          // the first of four: steps 1-4 ... 13-16
    kNumParams = 24,
};

// Relative knobs: range 0..kRelMax in the header, starting at kRelInit.
constexpr int32_t  kRelMax          = 4095;
constexpr int32_t  kRelInit         = 2048;
constexpr int32_t  kRelSwing        = 512;     // how far an answer sits from the panel's number
constexpr int32_t  kMaxTurn         = 16;      // a bigger jump is a resync, not a turn
constexpr uint64_t kBurstGapUs      = 5000;
constexpr int      kBurstKnobs      = 3;
constexpr uint64_t kPresetRestoreUs = 50000;
constexpr uint8_t  kAccent          = 100;
constexpr int      kLevelMax        = 100;
constexpr int      kLevelInit       = 80;
constexpr size_t   kTextLen         = 16;      // the panel shows about 8 characters

// The Layer knob's values: three views of the steps, then the actions.
enum : uint8_t { L_INST = 0, L_PROB, L_ACCENT, L_CLEAR, L_CLEAR_LANE, L_CLEAR_ALL, kLayers };
constexpr int      kProbLevels = 4;               // 25, 50, 75, 100 %
constexpr uint8_t  kProbAlways = kProbLevels - 1;
constexpr uint64_t kConfirmUs  = 300000;          // a clear needs a click made after a pause
constexpr uint8_t  kVelNormal  = 64;              // a hit's velocity: the drumlogue's normal trig
constexpr uint8_t  kVelAccent  = 127;             // and its accent (Brachetti's voiced level)

inline bool is_step(uint8_t id) { return id <= P_STEP16; }
inline bool is_relative(uint8_t id) { return is_step(id) || id == P_FAMILY || id == P_LEVEL; }
inline int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }
constexpr int kOverviewKnobs = 4;
inline bool is_overview(uint8_t id) { return id >= P_OVERVIEW && id < P_OVERVIEW + kOverviewKnobs; }

// One lane to strike: its family, the program (0-based) and the velocity.
struct Hit {
    uint8_t lane, family, program, velocity;
};

// Gates in, audio out: one producer, one consumer, no locks.
class HitQueue {
public:
    bool push(const Hit& h) {
        const uint32_t w = m_w.load(std::memory_order_relaxed);
        if (w - m_r.load(std::memory_order_acquire) >= kSize) return false;
        m_hits[w & (kSize - 1)] = h;
        m_w.store(w + 1, std::memory_order_release);
        return true;
    }
    // The next hit without taking it (consumer side).
    bool peek(Hit& h) const {
        const uint32_t r = m_r.load(std::memory_order_relaxed);
        if (r == m_w.load(std::memory_order_acquire)) return false;
        h = m_hits[r & (kSize - 1)];
        return true;
    }
    bool pop(Hit& h) {
        const uint32_t r = m_r.load(std::memory_order_relaxed);
        if (r == m_w.load(std::memory_order_acquire)) return false;
        h = m_hits[r & (kSize - 1)];
        m_r.store(r + 1, std::memory_order_release);
        return true;
    }

private:
    static constexpr uint32_t kSize = 16;
    Hit m_hits[kSize];
    std::atomic<uint32_t> m_w;
    std::atomic<uint32_t> m_r;
};

// 16x16 1-bpp, the drumlogue format: pixel x of row y is bit (x & 7) of byte
// y * 2 + (x >> 3), 1 = lit.
struct Bitmap {
    uint8_t px[32];
    void clear() { std::memset(px, 0, sizeof(px)); }
    void set(int x, int y) {
        if (x >= 0 && x < 16 && y >= 0 && y < 16) px[y * 2 + (x >> 3)] |= uint8_t(1u << (x & 7));
    }
    bool get(int x, int y) const { return (px[y * 2 + (x >> 3)] >> (x & 7)) & 1u; }
    void fill(int x0, int y0, int x1, int y1) {
        for (int y = y0; y <= y1; ++y)
            for (int x = x0; x <= x1; ++x) set(x, y);
    }
};

// The text a refused step or an unavailable family shows.
static const char kBlocked[] = "!!!";

// The Layer knob's texts.  At namespace scope: a static inside a template's
// function would be an STB_GNU_UNIQUE symbol, which a unit must not have.
static const char* const kLayerNames[kLayers] = {"Inst", "Prob", "Accent", "Clear?", "ClrLane", "ClrAll"};

// A Policy decides whether a lane may play an instrument (code: program + 1)
// on a step, given what the lanes hold now:
//   template <class Seq> static bool allows(const Seq&, int lane, int step, int family, int code);
// This one allows everything (the core's own tests).
struct NoLimits {
    template <class Seq>
    static bool allows(const Seq&, int, int, int, int) { return true; }
};

// No constructor and no member initialisers: a global Sequencer is
// zero-initialised and needs no code at load.  Call init() from unit_init.
template <class Catalog, class Policy = NoLimits>
class Sequencer {
    static_assert(Catalog::kMaxPrograms <= 254, "a step stores program + 1 in a byte");
    static_assert(Catalog::kFamilies <= 255, "a family fits a byte");

public:
    void init() {
        for (int l = 0; l < kLanes; ++l) {
            m_family[l].store(uint8_t(Catalog::default_family(l)), std::memory_order_relaxed);
            m_family_cursor[l] = uint8_t(Catalog::default_family(l));
            m_level[l].store(kLevelInit, std::memory_order_relaxed);
            for (int s = 0; s < kSteps; ++s) {
                m_steps[l][s].store(0, std::memory_order_relaxed);
                m_prob[l][s].store(kProbAlways, std::memory_order_relaxed);
                m_accent[l][s].store(0, std::memory_order_relaxed);
                m_cursor[l][s] = 0;
            }
        }
        m_layer = L_INST;
        m_layer_us = 0;
        m_rng = 0x9E3779B9u;
        for (int i = 0; i < kNumParams; ++i) {
            m_last_set[i] = is_relative(uint8_t(i)) ? kRelInit : 0;
            m_last_get[i] = 0;
            m_asked[i] = false;
        }
        m_lane = 0;
        m_set_last_us = m_preset_us = m_group_start_us = 0;
        m_group_knobs = 0;
        m_group_restore = false;
        m_snapshot_taken = false;
        m_restart.store(true, std::memory_order_relaxed);
        m_pos = 0;
    }

    // ── Knobs ───────────────────────────────────────────────────────────────
    void set_param(uint8_t id, int32_t value, uint64_t now_us) {
        if (id >= kNumParams) return;
        const bool restore = in_restore(id, now_us);
        const int32_t last = m_last_set[id];
        const bool asked = m_asked[id];
        m_last_set[id] = value;
        m_asked[id] = false;
        if (id == P_LANE) {
            m_lane = uint8_t(clampi(value, 0, kLanes - 1));
            m_family_cursor[m_lane] = uint8_t(family(m_lane));   // back on the family it plays
            return;
        }
        if (id == P_LAYER) {
            const int prev = m_layer;
            m_layer = uint8_t(clampi(value, 0, kLayers - 1));
            const bool deliberate = !restore && m_layer == prev + 1 && now_us - m_layer_us >= kConfirmUs;
            m_layer_us = now_us;
            if (deliberate && (m_layer == L_CLEAR_LANE || m_layer == L_CLEAR_ALL)) {
                if (!m_snapshot_taken) { take_snapshot(); m_snapshot_taken = true; }   // a load undoes it
                for (int l = 0; l < kLanes; ++l)
                    if (m_layer == L_CLEAR_ALL || l == m_lane) clear_lane(l);
            }
            return;
        }
        if (!is_relative(id) || restore) return;
        // If the panel asked since its last set, it may count from the
        // unit's answer; the reference closer to the new value is the one.
        int32_t ref = last;
        if (asked && std::abs(value - m_last_get[id]) < std::abs(value - last)) ref = m_last_get[id];
        const int32_t d = value - ref;
        if (d == 0 || d > kMaxTurn || d < -kMaxTurn) return;
        if (!m_snapshot_taken) { take_snapshot(); m_snapshot_taken = true; }
        turn(id, d);
    }

    int32_t get_param(uint8_t id) {
        if (id >= kNumParams) return 0;
        int32_t v = m_last_set[id];
        if (id == P_LANE) v = m_lane;
        else if (id == P_LAYER) v = m_layer;
        else if (is_relative(id)) v = (v < kRelInit) ? v + kRelSwing : v - kRelSwing;
        m_last_get[id] = v;
        m_asked[id] = true;
        return v;
    }

    void preset_loaded(uint64_t now_us) { m_preset_us = now_us; }

    // What the panel shows: the lane's content, not the knob's number.
    const char* param_text(uint8_t id, int32_t value) {
        if (id >= kNumParams) return "";
        char* s = m_text[id];
        const int lane = m_lane;
        const int f = family(lane);
        if (is_step(id)) {
            const int s = id - P_STEP1;
            const int code = step(lane, s);
            if (!m_cursor[lane][s]) return "---";
            if (!code) return kBlocked;                        // refused by the Policy
            switch (view()) {
            case L_PROB:
                snprintf(m_text[id], kTextLen, "%d%%", 100 * (prob(lane, s) + 1) / kProbLevels);
                return m_text[id];
            case L_ACCENT: return accent(lane, s) ? "Accent" : "Normal";
            default:       return Catalog::program_name(f, code - 1);
            }
        }
        switch (id) {
        case P_LANE:
            snprintf(s, kTextLen, "Lane %c", char('A' + clampi(value, 0, kLanes - 1)));
            return s;
        case P_FAMILY:   // the family browsed: "!!!" when another lane plays it
            return m_family_cursor[lane] == f ? Catalog::family_name(f) : kBlocked;
        case P_LEVEL:
            snprintf(s, kTextLen, "%d%%", level(lane));
            return s;
        case P_LAYER: {
            const int v = clampi(value, 0, kLayers - 1);
            if (v == L_CLEAR_LANE && lane_empty(lane)) return "Cleared";
            if (v == L_CLEAR_ALL && lane_empty(0) && lane_empty(1) && lane_empty(2)) return "AllClear";
            return kLayerNames[v];
        }
        default: return "-";
        }
    }

    // Overview (P20-P23, page 6): each knob shows four steps of all three
    // lanes, one cell of 3x3 pixels per step (lane rows at y 0, 5, 10): a
    // step that plays is filled, one refused ("!!!") an outline, an empty
    // one a dot — a dash on the selected lane, which marks it.
    const uint8_t* param_bitmap(uint8_t id) {
        if (!is_overview(id)) return nullptr;
        Bitmap& b = m_overview[id - P_OVERVIEW];
        b.clear();
        const int first = (id - P_OVERVIEW) * 4;
        for (int l = 0; l < kLanes; ++l) {
            const int y = l * 5;
            for (int k = 0; k < 4; ++k) {
                const int s = first + k, x = k * 4;
                if (step(l, s)) {
                    b.fill(x, y, x + 2, y + 2);
                } else if (blocked(l, s)) {
                    b.fill(x, y, x + 2, y + 2);
                    b.px[(y + 1) * 2 + ((x + 1) >> 3)] &= uint8_t(~(1u << ((x + 1) & 7)));   // hollow
                } else if (l == m_lane) {
                    b.fill(x, y + 1, x + 2, y + 1);
                } else {
                    b.set(x + 1, y + 1);
                }
            }
        }
        return b.px;
    }

    // ── Clock ───────────────────────────────────────────────────────────────
    // One gate: advance (or restart), write the lanes to strike into hits[],
    // return how many (0-3).
    // A hit's velocity is the step's (kVelAccent or kVelNormal); the gate's
    // own only marks the bar.
    int gate_on(uint8_t velocity, Hit* hits) {
        const bool restart = m_restart.exchange(false, std::memory_order_relaxed);
        m_pos = (velocity >= kAccent || restart) ? 0 : uint8_t((m_pos + 1) % kSteps);
        int n = 0;
        for (int l = 0; l < kLanes; ++l) {
            // Family first, then the step clamped to it: a family change on
            // the knob thread may not have clamped the steps yet.
            const int f = family(l);
            const int code = clampi(step(l, m_pos), 0, Catalog::programs(f));
            if (!code) continue;
            const int p = prob(l, m_pos);
            if (p < kProbAlways && int(next_random() % kProbLevels) > p) continue;   // plays (p+1) times in 4
            hits[n++] = {uint8_t(l), uint8_t(f), uint8_t(code - 1), accent(l, m_pos) ? kVelAccent : kVelNormal};
        }
        return n;
    }

    void reset() { m_restart.store(true, std::memory_order_relaxed); }   // the next gate is step 1

    // ── State ───────────────────────────────────────────────────────────────
    int step(int lane, int s) const { return m_steps[lane][s].load(std::memory_order_relaxed); }
    // What the step knob points at: the step's code, or the one refused ("!!!").
    int step_cursor(int lane, int s) const { return m_cursor[lane][s]; }
    bool blocked(int lane, int s) const { return m_cursor[lane][s] && !step(lane, s); }
    int family(int lane) const { return m_family[lane].load(std::memory_order_relaxed); }
    int level(int lane) const { return m_level[lane].load(std::memory_order_relaxed); }
    int prob(int lane, int s) const { return m_prob[lane][s].load(std::memory_order_relaxed); }     // 0-3: 25-100 %
    bool accent(int lane, int s) const { return m_accent[lane][s].load(std::memory_order_relaxed) != 0; }
    int layer() const { return m_layer; }

    // Every step checked again against the Policy, from what its knob points
    // at, lane A first and step 1 first: after a change of what the Policy
    // allows (the unit's Limit knob).  Refused steps come back if they fit.
    void recheck_all() {
        for (int l = 0; l < kLanes; ++l)
            for (int s = 0; s < kSteps; ++s) m_steps[l][s].store(0, std::memory_order_relaxed);
        for (int l = 0; l < kLanes; ++l)
            for (int s = 0; s < kSteps; ++s) check_step(l, s);
    }
    bool lane_empty(int lane) const {
        for (int s = 0; s < kSteps; ++s) if (m_cursor[lane][s]) return false;
        return true;
    }
    int selected_lane() const { return m_lane; }
    int position() const { return m_pos; }

private:
    // What the step knobs edit: the layer, or the instruments while Layer
    // is on an action.
    int view() const { return m_layer <= L_ACCENT ? int(m_layer) : int(L_INST); }

    void turn(uint8_t id, int d) {
        const int lane = m_lane;
        if (is_step(id)) {
            const int s = id - P_STEP1;
            if (view() == L_PROB) {                 // a step without an instrument has no probability
                if (m_cursor[lane][s])
                    m_prob[lane][s].store(uint8_t(clampi(prob(lane, s) + d, 0, kProbAlways)), std::memory_order_relaxed);
            } else if (view() == L_ACCENT) {        // right: accent, left: normal
                if (m_cursor[lane][s]) m_accent[lane][s].store(d > 0 ? 1 : 0, std::memory_order_relaxed);
            } else {
                m_cursor[lane][s] = uint8_t(clampi(m_cursor[lane][s] + d, 0, Catalog::programs(family(lane))));
                check_step(lane, s);
            }
        } else if (id == P_FAMILY) {
            const int f = clampi(m_family_cursor[lane] + d, 0, Catalog::kFamilies - 1);
            m_family_cursor[lane] = uint8_t(f);
            if (!family_taken(lane, f) && f != family(lane)) set_family(lane, f);
        } else if (id == P_LEVEL) {
            m_level[lane].store(uint8_t(clampi(level(lane) + d, 0, kLevelMax)), std::memory_order_relaxed);
        }
    }

    // The step plays what its knob points at, if the Policy allows it.
    void check_step(int lane, int s) {
        const int code = m_cursor[lane][s];
        const bool ok = !code || Policy::allows(*this, lane, s, family(lane), code);
        m_steps[lane][s].store(uint8_t(ok ? code : 0), std::memory_order_relaxed);
    }

    // Every step of the lane off, at 100 % and not accented; its family and
    // level stay.
    void clear_lane(int lane) {
        for (int s = 0; s < kSteps; ++s) {
            m_steps[lane][s].store(0, std::memory_order_relaxed);
            m_prob[lane][s].store(kProbAlways, std::memory_order_relaxed);
            m_accent[lane][s].store(0, std::memory_order_relaxed);
            m_cursor[lane][s] = 0;
        }
    }

    // xorshift32, gate thread only.
    uint32_t next_random() {
        m_rng ^= m_rng << 13;
        m_rng ^= m_rng >> 17;
        m_rng ^= m_rng << 5;
        return m_rng;
    }

    bool family_taken(int lane, int f) const {
        for (int l = 0; l < kLanes; ++l)
            if (l != lane && family(l) == f) return true;
        return false;
    }

    // A new family keeps the steps' positions: a step that is on stays on,
    // on the same program number if the family has it, else its last; each
    // is checked again against the Policy.  The steps are switched off while
    // the family changes, so a gate meanwhile plays nothing rather than a
    // program of the wrong family.
    void set_family(int lane, int f) {
        for (int s = 0; s < kSteps; ++s) m_steps[lane][s].store(0, std::memory_order_relaxed);
        m_family[lane].store(uint8_t(f), std::memory_order_relaxed);
        const int n = Catalog::programs(f);
        for (int s = 0; s < kSteps; ++s) {
            if (m_cursor[lane][s] > n) m_cursor[lane][s] = uint8_t(n);
            check_step(lane, s);
        }
    }

    // A person turns one knob at a time; a program load sets them all.
    bool in_restore(uint8_t id, uint64_t t) {
        if (t - m_set_last_us > kBurstGapUs) {
            m_group_start_us = t;
            m_group_knobs = 0;
            m_group_restore = false;
            m_snapshot_taken = false;
        }
        m_set_last_us = t;
        m_group_knobs |= 1u << id;
        int knobs = 0;
        for (uint32_t m = m_group_knobs; m; m &= m - 1) ++knobs;
        const bool after_preset = t - m_preset_us < kPresetRestoreUs;
        if (!m_group_restore && (knobs >= kBurstKnobs || after_preset)) {
            m_group_restore = true;
            if (m_snapshot_taken) restore_snapshot();   // that first "turn" was the restore
        }
        return m_group_restore;
    }

    void take_snapshot() {
        for (int l = 0; l < kLanes; ++l) {
            m_snap_family[l] = uint8_t(family(l));
            m_snap_family_cursor[l] = m_family_cursor[l];
            m_snap_level[l] = uint8_t(level(l));
            for (int s = 0; s < kSteps; ++s) {
                m_snap_steps[l][s] = uint8_t(step(l, s));
                m_snap_cursor[l][s] = m_cursor[l][s];
                m_snap_prob[l][s] = uint8_t(prob(l, s));
                m_snap_accent[l][s] = uint8_t(accent(l, s));
            }
        }
    }

    void restore_snapshot() {
        for (int l = 0; l < kLanes; ++l) {
            m_family[l].store(m_snap_family[l], std::memory_order_relaxed);
            m_family_cursor[l] = m_snap_family_cursor[l];
            m_level[l].store(m_snap_level[l], std::memory_order_relaxed);
            for (int s = 0; s < kSteps; ++s) {
                m_steps[l][s].store(m_snap_steps[l][s], std::memory_order_relaxed);
                m_cursor[l][s] = m_snap_cursor[l][s];
                m_prob[l][s].store(m_snap_prob[l][s], std::memory_order_relaxed);
                m_accent[l][s].store(m_snap_accent[l][s], std::memory_order_relaxed);
            }
        }
    }

    // The lanes (written by knob calls, read by gates and displays)
    std::atomic<uint8_t> m_steps[kLanes][kSteps];   // what plays: program + 1, 0 = off
    std::atomic<uint8_t> m_prob[kLanes][kSteps];    // 0-3: plays 1-4 times in 4
    std::atomic<uint8_t> m_accent[kLanes][kSteps];  // 1: strikes at kVelAccent
    std::atomic<uint8_t> m_family[kLanes];
    std::atomic<uint8_t> m_level[kLanes];
    uint8_t m_lane;
    uint8_t m_layer;                                 // P19
    uint64_t m_layer_us;                             // when Layer last moved

    // What the knobs point at (knob thread only): a step's chosen code, even
    // when the Policy refused it (then m_steps holds 0: "!!!"); the family
    // browsed, even when another lane plays it ("!!!").
    uint8_t m_cursor[kLanes][kSteps];
    uint8_t m_family_cursor[kLanes];

    // Knob reading
    int32_t m_last_set[kNumParams];
    int32_t m_last_get[kNumParams];
    bool    m_asked[kNumParams];

    // The restore guard
    uint64_t m_set_last_us, m_preset_us, m_group_start_us;
    uint32_t m_group_knobs;
    bool     m_group_restore, m_snapshot_taken;
    uint8_t  m_snap_steps[kLanes][kSteps];
    uint8_t  m_snap_cursor[kLanes][kSteps];
    uint8_t  m_snap_prob[kLanes][kSteps];
    uint8_t  m_snap_accent[kLanes][kSteps];
    uint8_t  m_snap_family[kLanes];
    uint8_t  m_snap_family_cursor[kLanes];
    uint8_t  m_snap_level[kLanes];

    // The clock (gate thread; reset may come from the audio thread)
    std::atomic<bool> m_restart;
    uint8_t m_pos;
    uint32_t m_rng;

    // Displays
    char   m_text[kNumParams][kTextLen];
    Bitmap m_overview[kOverviewKnobs];
};

}  // namespace brq
