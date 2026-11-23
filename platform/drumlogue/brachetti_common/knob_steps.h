#pragma once
// Brachetti common library — knobs the panel steps more coarsely than the
// engine stores them.
//
// Cutoff and NzFltFrq hold a frequency /10 in the engine (Cutoff 1-1999,
// NzFltFrq 2-2000: 10 Hz per step), but the display shows 0.1 kHz, so one
// click of the encoder often changed nothing visible and a sweep took a
// thousand clicks.  The units' header.c declares them 1-200 (0.1 kHz per
// click), and the unit translates between that and the engine's value:
//
//     engine = header × factor + offset        (Cutoff: offset -1, so its
//                                               open position 1999 is 200)
//
// The engine, its preset tables and its REFERENCE ANCHORS keep the 10 Hz
// values: a preset plays exactly as voiced, and shows (and comes back from a
// saved project as) the nearest step, at most 50 Hz away.  A knob is stepped
// only when its header says so (the name AND the 1-200 range), so a header
// with the engine's own range (the original's) passes through untouched.
//
// brc::RingOver applies it for every unit (Brachetti per family, from the
// family unit's header); tools that hand header values to an engine directly
// translate them with header_to_engine.
//
// Provides: KnobStep, knob_step, header_to_engine, engine_to_header.
#include <cstdint>
#include <cstring>
#include "unit.h"

namespace brc {

struct KnobStep {
    const char* name;
    int16_t     header_max;   // the stepped header range is 1..header_max
    int16_t     factor;
    int16_t     offset;
    int16_t     engine_min;   // the engine's own range
    int16_t     engine_max;
};

static const KnobStep kKnobSteps[] = {
    {"Cutoff",   200, 10, -1, 1, 1999},
    {"NzFltFrq", 200, 10,  0, 2, 2000},
};

// The step a header knob has, or nullptr.
inline const KnobStep* knob_step(const unit_param_t& p) {
    for (const KnobStep& s : kKnobSteps)
        if (p.min == 1 && p.max == s.header_max && strcmp(p.name, s.name) == 0) return &s;
    return nullptr;
}

inline int32_t step_to_engine(const KnobStep& s, int32_t v) {
    const int32_t h = (v < 1) ? 1 : ((v > s.header_max) ? s.header_max : v);
    const int32_t e = h * s.factor + s.offset;
    return (e < s.engine_min) ? s.engine_min : ((e > s.engine_max) ? s.engine_max : e);
}

inline int32_t step_to_header(const KnobStep& s, int32_t e) {
    const int32_t h = (e - s.offset + s.factor / 2) / s.factor;   // the nearest step
    return (h < 1) ? 1 : ((h > s.header_max) ? s.header_max : h);
}

inline int32_t header_to_engine(const unit_param_t& p, int32_t v) {
    const KnobStep* s = knob_step(p);
    return s ? step_to_engine(*s, v) : v;
}

inline int32_t engine_to_header(const unit_param_t& p, int32_t e) {
    const KnobStep* s = knob_step(p);
    return s ? step_to_header(*s, e) : e;
}

}  // namespace brc
