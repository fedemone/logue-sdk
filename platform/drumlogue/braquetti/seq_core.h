#pragma once
// Braquetti — the sequencer core: three lanes of 16 steps, the knobs that
// edit them, the clock that plays them.  No sound, no SDK calls, no clock of
// its own (the caller passes the time), no heap: host-testable, and a unit
// wraps it (PLAN.md has the design and the hardware findings it rests on).
//
//   Knobs (the order header.c will declare):
//     P00-P15  steps 1-16 of the selected lane (pages 1-4, a beat per page)
//     P16      Lane: which lane page 5 and the steps show (A, B, C)
//     P17      Family of the selected lane
//     P18      Level of the selected lane
//     P19      spare
//     P20      Overview: a bitmap of all three lanes (page 6)
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
//   the first gate after a reset (stop sends one).  Finding 5, 6.
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
    P_SPARE = 19,
    P_OVERVIEW = 20,
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

inline bool is_step(uint8_t id) { return id <= P_STEP16; }
inline bool is_relative(uint8_t id) { return is_step(id) || id == P_FAMILY || id == P_LEVEL; }
inline int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

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
};

// No constructor and no member initialisers: a global Sequencer is
// zero-initialised and needs no code at load.  Call init() from unit_init.
template <class Catalog>
class Sequencer {
    static_assert(Catalog::kMaxPrograms <= 254, "a step stores program + 1 in a byte");
    static_assert(Catalog::kFamilies <= 255, "a family fits a byte");

public:
    void init() {
        for (int l = 0; l < kLanes; ++l) {
            m_family[l].store(uint8_t(Catalog::default_family(l)), std::memory_order_relaxed);
            m_level[l].store(kLevelInit, std::memory_order_relaxed);
            for (int s = 0; s < kSteps; ++s) m_steps[l][s].store(0, std::memory_order_relaxed);
        }
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
        if (id == P_LANE) { m_lane = uint8_t(clampi(value, 0, kLanes - 1)); return; }
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
            const int code = step(lane, id - P_STEP1);
            return code ? Catalog::program_name(f, code - 1) : "---";
        }
        switch (id) {
        case P_LANE:
            snprintf(s, kTextLen, "Lane %c", char('A' + clampi(value, 0, kLanes - 1)));
            return s;
        case P_FAMILY: return Catalog::family_name(f);
        case P_LEVEL:
            snprintf(s, kTextLen, "%d%%", level(lane));
            return s;
        default: return "-";
        }
    }

    // Overview (P20): each lane a 2-pixel row (y 1, 6, 11), a step per
    // column; dots under the selected lane.
    const uint8_t* param_bitmap(uint8_t id) {
        if (id != P_OVERVIEW) return nullptr;
        m_overview.clear();
        for (int l = 0; l < kLanes; ++l) {
            const int y = 1 + l * 5;
            for (int s = 0; s < kSteps; ++s)
                if (step(l, s)) { m_overview.set(s, y); m_overview.set(s, y + 1); }
            if (l == m_lane)
                for (int s = 0; s < kSteps; s += 2) m_overview.set(s, y + 3);
        }
        return m_overview.px;
    }

    // ── Clock ───────────────────────────────────────────────────────────────
    // One gate: advance (or restart), write the lanes to strike into hits[],
    // return how many (0-3).
    int gate_on(uint8_t velocity, Hit* hits) {
        const bool restart = m_restart.exchange(false, std::memory_order_relaxed);
        m_pos = (velocity >= kAccent || restart) ? 0 : uint8_t((m_pos + 1) % kSteps);
        int n = 0;
        for (int l = 0; l < kLanes; ++l) {
            // Family first, then the step clamped to it: a family change on
            // the knob thread may not have clamped the steps yet.
            const int f = family(l);
            const int code = clampi(step(l, m_pos), 0, Catalog::programs(f));
            if (code) hits[n++] = {uint8_t(l), uint8_t(f), uint8_t(code - 1), velocity};
        }
        return n;
    }

    void reset() { m_restart.store(true, std::memory_order_relaxed); }   // the next gate is step 1

    // ── State ───────────────────────────────────────────────────────────────
    int step(int lane, int s) const { return m_steps[lane][s].load(std::memory_order_relaxed); }
    int family(int lane) const { return m_family[lane].load(std::memory_order_relaxed); }
    int level(int lane) const { return m_level[lane].load(std::memory_order_relaxed); }
    int selected_lane() const { return m_lane; }
    int position() const { return m_pos; }

private:
    void turn(uint8_t id, int d) {
        const int lane = m_lane;
        if (is_step(id)) {
            const int s = id - P_STEP1;
            const int code = clampi(step(lane, s) + d, 0, Catalog::programs(family(lane)));
            m_steps[lane][s].store(uint8_t(code), std::memory_order_relaxed);
        } else if (id == P_FAMILY) {
            set_family(lane, clampi(family(lane) + d, 0, Catalog::kFamilies - 1));
        } else if (id == P_LEVEL) {
            m_level[lane].store(uint8_t(clampi(level(lane) + d, 0, kLevelMax)), std::memory_order_relaxed);
        }
    }

    // A new family keeps the steps' positions: a step that is on stays on,
    // on the same program number if the family has it, else its last.
    void set_family(int lane, int f) {
        m_family[lane].store(uint8_t(f), std::memory_order_relaxed);
        const int n = Catalog::programs(f);
        for (int s = 0; s < kSteps; ++s) {
            const int code = step(lane, s);
            if (code > n) m_steps[lane][s].store(uint8_t(n), std::memory_order_relaxed);
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
            m_snap_level[l] = uint8_t(level(l));
            for (int s = 0; s < kSteps; ++s) m_snap_steps[l][s] = uint8_t(step(l, s));
        }
    }

    void restore_snapshot() {
        for (int l = 0; l < kLanes; ++l) {
            m_family[l].store(m_snap_family[l], std::memory_order_relaxed);
            m_level[l].store(m_snap_level[l], std::memory_order_relaxed);
            for (int s = 0; s < kSteps; ++s) m_steps[l][s].store(m_snap_steps[l][s], std::memory_order_relaxed);
        }
    }

    // The lanes (written by knob calls, read by gates and displays)
    std::atomic<uint8_t> m_steps[kLanes][kSteps];   // program + 1, 0 = off
    std::atomic<uint8_t> m_family[kLanes];
    std::atomic<uint8_t> m_level[kLanes];
    uint8_t m_lane;

    // Knob reading
    int32_t m_last_set[kNumParams];
    int32_t m_last_get[kNumParams];
    bool    m_asked[kNumParams];

    // The restore guard
    uint64_t m_set_last_us, m_preset_us, m_group_start_us;
    uint32_t m_group_knobs;
    bool     m_group_restore, m_snapshot_taken;
    uint8_t  m_snap_steps[kLanes][kSteps];
    uint8_t  m_snap_family[kLanes];
    uint8_t  m_snap_level[kLanes];

    // The clock (gate thread; reset may come from the audio thread)
    std::atomic<bool> m_restart;
    uint8_t m_pos;

    // Displays
    char   m_text[kNumParams][kTextLen];
    Bitmap m_overview;
};

}  // namespace brq
