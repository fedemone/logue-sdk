#pragma once
// Brachetti common library — what a knob's raw value means.
//
//  * knob::*        raw header.c value → the number the DSP uses
//  * Anchors        the shipped knob values (REFERENCE ANCHORS), and
//    off_anchor / off_unity, whether a knob has moved off them
//  * HostKnob       the knobs the host owns: is_host_knob, seed_host_knobs
//  * VelocityBias   the Velocity knob (ghost … as played … wham)
//  * SequencerNote  the note the sequencer plays: preset root + Pitch knob
//  * name_at, knob_text, format_*  the strings the drumlogue shows for a knob
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include "dsp_math.h"

namespace brc {

// Raw knob value → DSP value.  Every unit reads its knobs through these, both
// live and when it captures a preset's shipped values as REFERENCE ANCHORS,
// so the two always agree.
namespace knob {
inline float unit_range(int32_t v, float scale) { return clampf((float)v * scale, 0.0f, 1.0f); }

inline float mallet_res(int32_t v)   { return unit_range(v, 0.001f); }          // MlltRes  0-1000
inline float mallet_stiff(int32_t v) { return clampf((float)v * 0.02f, 0.01f, 1.0f); }  // MlltStif 0-50 (÷100)
inline float decay(int32_t v)        { return unit_range(v, 0.005f); }          // Dkay     0-200 (÷10)
inline float release(int32_t v)      { return unit_range(v, 0.05f); }           // Rel      0-20
inline float noise_mix(int32_t v)    { return unit_range(v, 0.01f); }           // NzMix    0-100
inline float noise_res(int32_t v)    { return unit_range(v, 0.001f); }          // NzRes    0-1000
inline float material(int32_t v)     { return (clampf((float)v, -10.0f, 30.0f) + 10.0f) * 0.025f; }  // Mterl -10..30 → 0..1
inline float inharm(int32_t v)       { return clampf((float)v * 0.01f, -1.0f, 1.0f); }   // Inharm  -100..100
inline float hit_pos(int32_t v)      { return clampf((float)v * 0.01f, -1.0f, 1.0f); }   // HitPos  -98..98
inline float tube_radius(int32_t v)  { return clampf((float)v, 0.0f, 20.0f) * 0.05f; }  // TubRad 0-20 → 0..1
inline float percent(int32_t v)      { return (float)v * 0.01f; }               // VlMllRes / VlMllStf, ±1
inline float noise_freq(int32_t v)   { return clampf((float)v, 2.0f, 2000.0f); } // NzFltFrq 2-2000, in stored units (÷10 Hz)
inline float gain_drive(int32_t v)   { return 1.0f + (fmaxf(0.0f, (float)v * 0.01f) * 20.0f); }  // Gain 0-100 → drive ×1..×21
inline int32_t clamp_int(int32_t v, int32_t lo, int32_t hi) { return (v < lo) ? lo : ((v > hi) ? hi : v); }
inline uint8_t poly(int32_t v)       { return (uint8_t)clamp_int(v, 1, 4); }    // Poly 1-4
}  // namespace knob

// ── Reference anchors ───────────────────────────────────────────────────────
// The knob values the current preset shipped with, captured by LoadPreset
// through the same knob:: functions the mappings read.  Every knob mapping is
// a function of the DELTA from its anchor, built so that a delta of 0 leaves
// the calibrated sound exactly as voiced: a preset plays as shipped at its
// own knob positions and only knob MOVEMENT alters it.  A unit fills the
// fields its mappings use.  (The defaults only matter before the first
// preset loads.)
struct Anchors {
    float   decay        = 0.975f;   // Dkay
    float   release      = 0.9f;     // Rel
    float   mallet_stiff = 0.26f;    // MlltStif
    float   mallet_res   = 0.5f;     // MlltRes
    uint8_t model        = 0;        // Model
    int32_t partls       = 0;        // Partls
    float   inharm       = 0.0f;     // Inharm
    float   material     = 0.5f;     // Mterl
    float   hit_pos      = 0.26f;    // HitPos
    float   tube_radius  = 0.25f;    // TubRad
    float   vel_stiff    = 0.0f;     // VlMllStf
    float   vel_res      = 0.0f;     // VlMllRes
};

// Has a knob moved off its anchor?  `delta` is knob − anchor: exactly 0 at
// the shipped value, where a mapping must leave the calibrated sound alone.
inline bool off_anchor(float delta) { return delta < -0.001f || delta > 0.001f; }

// The same test for an anchored FACTOR, which is exactly 1 at the anchor.
inline bool off_unity(float factor) { return factor < 0.999f || factor > 1.001f; }

// ── The knobs the host owns ─────────────────────────────────────────────────
// Performance and master controls (Pitch, Poly, Velocity, VlMllRes, Tone,
// Cutoff, Resnc — whichever the unit has) are not part of a preset: a Program
// change leaves them where they are, and Init() puts them at their header.c
// defaults.  A unit lists them once, with those defaults, and both rules read
// the same table:
//
//   static const brc::HostKnob kHostKnobs[] = {{k_paramPoly, 4}, ...};
//   LoadPreset:  if (p != k_paramProgram && !brc::is_host_knob(kHostKnobs, p)) load p
//   Init:        brc::seed_host_knobs(*this, kHostKnobs);
//
// The defaults MUST track header.c's `.init` fields (C cannot assert across
// the two files); the preset table's static_asserts pin those columns.
struct HostKnob {
    uint8_t param;   // the knob's slot
    int16_t init;    // its header.c default
};

template <size_t N>
inline bool is_host_knob(const HostKnob (&knobs)[N], uint8_t param) {
    for (size_t i = 0; i < N; ++i)
        if (knobs[i].param == param) return true;
    return false;
}

// Every host-owned knob to its default, in table order, through the unit's
// own setParameter (so whatever a knob drives is set up too).
template <class Synth, size_t N>
inline void seed_host_knobs(Synth& synth, const HostKnob (&knobs)[N]) {
    for (size_t i = 0; i < N; ++i) synth.setParameter(knobs[i].param, knobs[i].init);
}

// MIDI velocity 0-127 → 0..1.
inline float midi_velocity(uint8_t v) { return (float)v * 0.007874015f; }

// ── The Velocity knob ───────────────────────────────────────────────────────
// Biases every strike ONCE, at NoteOn, before anything reads the velocity.
// 0 returns the incoming velocity EXACTLY (knob_exp2(0) is exactly 1.0), so
// the default knob keeps every preset and every render byte-identical.
//   knob < 0 → ghost notes: down to x0.19 (velocity 127 lands at ~24)
//   knob > 0 → wham:        up to x5.28, clamped at kWhamMax, so any stroke
//                           above ~velocity 25 saturates to a full-force hit.
struct VelocityBias {
    // How far a full "wham" may push a strike PAST the calibrated maximum.
    // Every preset is voiced at velocity 1.0, so without an over-range the top
    // of the knob would do nothing whenever the sequencer already sends 127.
    // 1.30 is a hit ~2.3 dB hotter; the shaping downstream clamps at its
    // calibrated maxima, so the over-range reads as energy, not a new timbre.
    static constexpr float kWhamMax = 1.30f;
    // Floor: a ghosted strike must stay a strike — below this the magnitude
    // squelch can retire the voice before it speaks.
    static constexpr float kGhostMin = 0.02f;

    float bias = 0.0f;   // -1 (ghost) .. 0 (as played) .. +1 (wham)

    void set(int32_t knob_value) { bias = clampf((float)knob_value * 0.01f, -1.0f, 1.0f); }

    float apply(float vel_norm) const {
        if (bias == 0.0f) return vel_norm;
        const float scaled = vel_norm * knob_exp2(2.4f * bias);
        const float ceil_v = (bias > 0.0f) ? (1.0f + (kWhamMax - 1.0f) * bias) : 1.0f;
        return clampf(scaled, kGhostMin, ceil_v);
    }
};

// ── The note the sequencer plays ────────────────────────────────────────────
// The drumlogue's sequencer (GateOn) plays the preset's ROOT note plus the
// Pitch knob; external MIDI plays the note it carries.  Kept derived, so
// neither input can go stale against it: LoadPreset moves the root,
// setParameter moves the offset, and both re-derive the note.
struct SequencerNote {
    static constexpr int32_t kPitchRange = 24;   // must equal header.c's Pitch min/max

    uint8_t root      = 36;   // the preset's root (its preset-table column)
    int8_t  pitch     = 0;    // Pitch knob, ±kPitchRange
    uint8_t note      = 36;   // root + pitch: what GateOn plays
    uint8_t gate_note = 36;   // what the last GateOn struck, for GateOff

    void set_root(uint8_t r) { root = r; update(); }

    // Bounded as the SDK asks; returns the stored value so the host reads
    // back what plays.
    int32_t set_pitch(int32_t v) {
        const int32_t p = knob::clamp_int(v, -kPitchRange, kPitchRange);
        pitch = (int8_t)p;
        update();
        return p;
    }

    // GateOff must release the note GateOn STRUCK, not whatever root + Pitch
    // is by then: a motion-sequenced Pitch or a Program change can move it
    // between a held gate's on and off.
    uint8_t strike() { gate_note = note; return note; }

    void update() {
        const int n = (int)root + (int)pitch;
        note = (uint8_t)((n < 0) ? 0 : ((n > 127) ? 127 : n));
    }
};

// ── Knob display strings ────────────────────────────────────────────────────
// A name from a fixed table, or `fallback` when idx is outside it (preset
// names, Model names, filter types).
//
// THE TABLE MUST BE A NAMESPACE-SCOPE `static` ARRAY, never a static local of
// an inline function or a template.  GCC gives such locals the STB_GNU_UNIQUE
// binding, and the dynamic loader keeps ONE copy of a unique symbol for every
// library that defines it — across separately loaded drumlogue units too.
// Each family unit's preset-name table was once `preset_name()::names`, so
// every unit showed the names of whichever unit loaded first (BrcKernel's
// Program read "Marmba", "TblrBel").  The same applies to any mutable buffer
// (knob_text below).  arm_size.sh fails a unit that has a unique symbol.
template <size_t N>
inline const char* name_at(const char* const (&names)[N], int32_t idx, const char* fallback) {
    return (idx >= 0 && idx < (int32_t)N) ? names[idx] : fallback;
}

// The buffer a knob's display string is built in: one per knob slot, because
// the OS may keep one knob's string on screen while it asks for another.  The
// storage is a static local of a `static` (internal-linkage) function, so
// each unit has its own (see name_at for why that matters).
constexpr size_t  kKnobTextSize = 12;
constexpr uint8_t kMaxKnobs     = 24;   // the drumlogue's parameter slots
static inline char* knob_text(uint8_t slot) {
    static char text[kMaxKnobs][kKnobTextSize];
    return text[(slot < kMaxKnobs) ? slot : 0];
}

// The format_* helpers write into the caller's buffer (knob_text).
inline const char* format_int(char* buf, size_t size, int value) {
    snprintf(buf, size, "%d", value);
    return buf;
}

// "40%", "Rs40%": a percentage with an optional prefix.
inline const char* format_percent(char* buf, size_t size, int value, const char* prefix = "") {
    snprintf(buf, size, "%s%d%%", prefix, value);
    return buf;
}

// "800Hz", "1.2kHz" — header.c stores frequencies ÷10.  Bounded to the
// widest frequency knob (NzFltFrq, 2-2000), so the string always fits ten
// bytes ("20.0kHz").
inline const char* format_hz(char* buf, size_t size, int32_t stored) {
    const int32_t hz = knob::clamp_int(stored, 0, 2000) * 10;
    if (hz >= 1000) snprintf(buf, size, "%d.%dkHz", hz / 1000, (hz % 1000) / 100);
    else            snprintf(buf, size, "%dHz", hz);
    return buf;
}

}  // namespace brc
