#pragma once
// Brachetti common library — the COMMON knobs: the first two pages, the same
// slots, names and meaning on all nine Brachetti units.
//
//   page 1  Program · Pitch · Poly · Velocity     performance
//   page 2  Tone · Cutoff · Resnc · Gain          master stage
//   page 3+ the family's own (DEDICATED) knobs
//
// A unit without one of them keeps its slot blank in header.c (BrcNoise has
// no Pitch, BrcKernel no Poly), so every common knob sits in the same place
// on every unit and the family's knobs always start on page 3.
//
// Provides: CommonLayout (the eight slots), CommonKnobs (their state and
// setter: the note, Poly, Velocity and the master controls), kCommonHostKnobs
// (the common knobs the host owns, with their header.c defaults),
// common_knob_text (their display strings).
#include <cstddef>
#include <cstdint>
#include "knobs.h"

namespace brc {

struct CommonLayout {
    enum CommonParam : uint8_t {
        k_paramProgram = 0,  // 0  page 1: performance
        k_paramPitch,        // 1
        k_paramPoly,         // 2
        k_paramVelocity,     // 3
        k_paramTone,         // 4  page 2: master
        k_paramCutoff,       // 5
        k_paramResnc,        // 6
        k_paramGain,         // 7
        k_firstDedicated     // 8  page 3 on: the family's knobs
    };
};

// The common knobs the host owns: a Program change leaves them where they
// are, and Init() seeds them with these header.c defaults (HostKnob, in
// knobs.h).  Gain is common but loads with the preset (it is part of a
// preset's gain staging).  A unit's own host-knob table starts with these.
#define BRC_COMMON_HOST_KNOBS                                   \
    { brc::CommonLayout::k_paramPitch,    0    },               \
    { brc::CommonLayout::k_paramPoly,     4    },               \
    { brc::CommonLayout::k_paramVelocity, 0    },               \
    { brc::CommonLayout::k_paramTone,     0    },               \
    { brc::CommonLayout::k_paramCutoff,   1999 },               \
    { brc::CommonLayout::k_paramResnc,    71   }
static const HostKnob kCommonHostKnobs[] = { BRC_COMMON_HOST_KNOBS };

// The state the common knobs drive, apart from the master stage (which the
// unit owns: MasterStage, or MasterControls on the kernel).
struct CommonKnobs {
    SequencerNote note;       // Pitch: the preset's root + the knob
    VelocityBias  velocity;   // Velocity
    uint8_t       poly = 4;   // Poly: voice cap 1-4

    // A common knob (Pitch … Gain; Program is the unit's: it loads a preset)
    // into its state.  Returns the value the host should read back.
    template <class Master>
    int32_t set(uint8_t index, int32_t value, const int32_t* params, Master& master) {
        switch (index) {
        case CommonLayout::k_paramPitch:    return note.set_pitch(value);
        case CommonLayout::k_paramPoly:     poly = knob::poly(value); break;
        case CommonLayout::k_paramVelocity: velocity.set(value); break;
        case CommonLayout::k_paramTone:     master.set_tone(value); break;
        case CommonLayout::k_paramCutoff:
        case CommonLayout::k_paramResnc:
            master.set_filter(params[CommonLayout::k_paramCutoff], params[CommonLayout::k_paramResnc]);
            break;
        case CommonLayout::k_paramGain:     master.set_gain(value); break;
        default: break;
        }
        return value;
    }
};

inline bool is_common_knob(uint8_t index) { return index < CommonLayout::k_firstDedicated; }

// The display string of a common knob, or nullptr where the drumlogue shows
// the number itself (Program's names are the unit's).
inline const char* common_knob_text(uint8_t index, int32_t value) {
    switch (index) {
    case CommonLayout::k_paramPoly:   return format_int(knob_text(index), kKnobTextSize, knob::poly(value));
    case CommonLayout::k_paramCutoff: return format_hz(knob_text(index), kKnobTextSize, value);
    case CommonLayout::k_paramResnc:  return format_int(knob_text(index), kKnobTextSize, (int)(value * 10));
    default: return nullptr;
    }
}

}  // namespace brc
