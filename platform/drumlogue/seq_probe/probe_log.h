#pragma once
// seq_probe — the event log.
//
// Every callback the drumlogue makes pushes a fixed-size record into a
// lock-free ring (any thread, the audio thread included: no locks, no
// allocation, no I/O).  The records are formatted to text and written to the
// log file elsewhere, by a worker thread (unit.cc), so the audio callback
// never waits on storage.
//
// Multi-producer, single-consumer: a producer reserves a slot with fetch_add,
// fills it, then publishes it by storing its sequence number; the consumer
// takes slots in order while the next one is published.  A producer that
// finds the ring nearly full drops its record and counts it instead.
#include <atomic>
#include <cstdint>

namespace probe {

enum Ev : uint8_t {
    kEvInit, kEvTeardown, kEvReset, kEvResume, kEvSuspend,
    kEvRenderStats, kEvPollStats,
    kEvSetParam, kEvGetParam, kEvStr, kEvBmp,
    kEvTempo, kEvNoteOn, kEvNoteOff, kEvGateOn, kEvGateOff, kEvAllNoteOff,
    kEvPitchBend, kEvPressure, kEvAftertouch,
    kEvLoadPreset, kEvGetPresetIndex, kEvGetPresetName,
    kEvThreadInfo, kEvWorker, kEvEdit, kEvResync, kEvBurst, kEvRestart,
    kEvCount
};

// Short names, also used for the THREAD lines ("which callback ran where").
static const char* const kEvName[kEvCount] = {
    "init", "teardown", "reset", "resume", "suspend",
    "render", "poll",
    "set", "get", "str", "bmp",
    "tempo", "note_on", "note_off", "gate_on", "gate_off", "all_note_off",
    "pitch_bend", "pressure", "aftertouch",
    "load_preset", "get_preset_index", "get_preset_name",
    "thread", "worker", "edit", "resync", "burst", "restart",
};

struct Rec {
    uint64_t t_us;    // CLOCK_MONOTONIC, microseconds
    uint32_t frame;   // audio frames rendered so far
    uint32_t tid;     // kernel thread id of the caller
    int32_t  b;
    int32_t  c;
    uint8_t  ev;
    uint8_t  a;
    uint16_t d;
    std::atomic<uint32_t> seq;   // index + 1 once published, 0 when free
};
static_assert(sizeof(Rec) == 32, "a record is 32 bytes");

// 64 KB: drained every 100 ms, it holds about 20 s of the busiest panel.
// (The original Brachetti's 108 KB .bss is recorded loading on hardware.)
constexpr uint32_t kRingSize = 2048;
constexpr uint32_t kRingMask = kRingSize - 1;
constexpr uint32_t kRingSlack = 64;    // room for producers racing the check
static_assert((kRingSize & kRingMask) == 0, "power of two");

class Ring {
public:
    void push(Ev ev, uint8_t a, int32_t b, int32_t c, uint16_t d,
              uint64_t t_us, uint32_t frame, uint32_t tid) {
        const uint32_t w = m_write.load(std::memory_order_relaxed);
        if (w - m_read.load(std::memory_order_acquire) >= kRingSize - kRingSlack) {
            m_dropped.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        const uint32_t i = m_write.fetch_add(1, std::memory_order_relaxed);
        Rec& r = m_rec[i & kRingMask];
        r.t_us = t_us; r.frame = frame; r.tid = tid;
        r.ev = ev; r.a = a; r.b = b; r.c = c; r.d = d;
        r.seq.store(i + 1, std::memory_order_release);
    }

    // Hands each published record, in order, to f(const Rec&); returns how
    // many.  One consumer at a time (the caller guarantees it).
    template <class F>
    uint32_t drain(F&& f) {
        uint32_t n = 0;
        uint32_t rd = m_read.load(std::memory_order_relaxed);
        for (;;) {
            Rec& r = m_rec[rd & kRingMask];
            if (r.seq.load(std::memory_order_acquire) != rd + 1) break;
            f(static_cast<const Rec&>(r));
            r.seq.store(0, std::memory_order_relaxed);
            ++rd; ++n;
            m_read.store(rd, std::memory_order_release);
        }
        return n;
    }

    uint32_t take_dropped() { return m_dropped.exchange(0, std::memory_order_relaxed); }

private:
    Rec m_rec[kRingSize];
    std::atomic<uint32_t> m_write{0};
    std::atomic<uint32_t> m_read{0};
    std::atomic<uint32_t> m_dropped{0};
};

}  // namespace probe
