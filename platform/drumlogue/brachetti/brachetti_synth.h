#pragma once
// Brachetti — all twelve Brachetti families in one drumlogue unit.
//
// The unit's 12 presets are the 12 families (the order of kFamilies below).
// Loading a preset switches the engine; the Program knob then browses that
// family's instruments, exactly as Program does on the single-family unit.
// Every family engine is the unchanged code of its unit in ../brachetti_split
// (families.h); this file only routes the drumlogue's calls to the selected
// one and translates the knobs.
//
// The knobs.  The drumlogue reads the 24 knob names and ranges once, from
// header.c, so they cannot change with the family.  Pages 1-2 (Program, Pitch,
// Poly, Velocity, Tone, Cutoff, Resnc, Gain) are the same on every family
// unit already and pass straight through.  Pages 3-6 carry each family's
// dedicated knobs: kFamilies[f].slot says which of the family's knobs sits
// in each of Brachetti's slots (most families keep their own layout).  Where
// a family's knob has the same range as Brachetti's slot, its value passes
// through unchanged; where it does not (a 0-4 Partls in a -100..100 slot, a
// 0-20 Rel, a 0-100 Blend in the 0-1000 NzRes slot), the value is spread
// linearly over the slot's range — every value of the family's knob still
// has its own knob position, and its preset values come back exactly.
// A slot where every family's knob has the same range and type is declared
// with that type, so the drumlogue formats and steps it as on the family
// unit; the others are "strings" knobs, which show the family knob's own
// value (as the family unit would show it), or "---" where the family has no
// knob.  The drumlogue hands a strings knob's value over as an UNSIGNED
// 16-bit number (-1 arrives as 65535), so no strings slot has a negative
// range: the mixed slots run 0-200.  Cutoff and NzFltFrq step 0.1 kHz, as
// on the family units (knob_steps.h).  README.md has the per-family knob table.
//
// Ringing on.  Each family engine is a brc::RingOver pair (ring_over.h): a
// Program change while an instrument rings lets it ring out under its own
// settings while the new one plays.  A family switch does the same at the
// family level: the family left behind keeps rendering until it falls
// silent, so its last strokes ring on under the new family.
//
// Thread safety.  A preset load prepares the new family completely (Init, or
// a ring-over Program change if it is still ringing; the panel's common
// knobs) before it publishes it with one atomic store; the audio thread picks
// the store up at its next block.  A family that is neither selected nor
// ringing is never rendered, so Init never runs on an engine the audio thread
// is using.
#include <atomic>
#include "families.h"

namespace brachetti {

constexpr uint8_t kNumSlots    = 24;
constexpr uint8_t kNone        = 0xFF;   // a Brachetti slot the family does not use
constexpr uint8_t kNumFamilies = 12;
constexpr size_t  kChunk       = 64;     // frames per render of a family ringing out (stack buffer)

// One family: its engine, its header.c, where its knobs sit, and the calls.
struct Family {
    const char*          name;           // the preset name
    void*                engine;
    const unit_header_t* hdr;            // the family unit's own header.c
    uint8_t              slot[kNumSlots];  // Brachetti slot -> the family's knob (kNone: unused)
    int                  num_programs;
    int8_t      (*init)(void*, const unit_runtime_desc_t*);
    void        (*teardown)(void*);
    void        (*reset)(void*);
    void        (*resume)(void*);
    void        (*suspend)(void*);
    void        (*render)(void*, float*, size_t);
    void        (*set)(void*, uint8_t, int32_t);
    int32_t     (*get)(const void*, uint8_t);
    const char* (*text)(const void*, uint8_t, int32_t);
    void        (*note_on)(void*, uint8_t, uint8_t);
    void        (*note_off)(void*, uint8_t);
    void        (*gate_on)(void*, uint8_t);
    void        (*gate_off)(void*);
    void        (*all_note_off)(void*);
    void        (*pitch_bend)(void*, uint16_t);
    bool        (*sounding)(const void*);
    void        (*set_header)(void*, const unit_header_t*);
};

// The calls, for any family class (internal linkage: no symbol of these is
// shared with another loaded unit).
template <class S> static int8_t      f_init(void* e, const unit_runtime_desc_t* d) { return static_cast<S*>(e)->Init(d); }
template <class S> static void        f_teardown(void* e)                  { static_cast<S*>(e)->Teardown(); }
template <class S> static void        f_reset(void* e)                     { static_cast<S*>(e)->Reset(); }
template <class S> static void        f_resume(void* e)                    { static_cast<S*>(e)->Resume(); }
template <class S> static void        f_suspend(void* e)                   { static_cast<S*>(e)->Suspend(); }
template <class S> static void        f_render(void* e, float* o, size_t n) { static_cast<S*>(e)->processBlock(o, n); }
template <class S> static void        f_set(void* e, uint8_t i, int32_t v) { static_cast<S*>(e)->setParameter(i, v); }
template <class S> static int32_t     f_get(const void* e, uint8_t i)      { return static_cast<const S*>(e)->getParameterValue(i); }
template <class S> static const char* f_text(const void* e, uint8_t i, int32_t v) { return static_cast<const S*>(e)->getParameterStrValue(i, v); }
template <class S> static void        f_note_on(void* e, uint8_t n, uint8_t v) { static_cast<S*>(e)->NoteOn(n, v); }
template <class S> static void        f_note_off(void* e, uint8_t n)       { static_cast<S*>(e)->NoteOff(n); }
template <class S> static void        f_gate_on(void* e, uint8_t v)        { static_cast<S*>(e)->GateOn(v); }
template <class S> static void        f_gate_off(void* e)                  { static_cast<S*>(e)->GateOff(); }
template <class S> static void        f_all_note_off(void* e)              { static_cast<S*>(e)->AllNoteOff(); }
template <class S> static void        f_pitch_bend(void* e, uint16_t b)    { static_cast<S*>(e)->PitchBend(b); }
template <class S> static bool        f_sounding(const void* e)            { return static_cast<const S*>(e)->Sounding(); }
template <class S> static void        f_set_header(void* e, const unit_header_t* h) { static_cast<S*>(e)->SetHeader(h); }

#define BRACHETTI_CALLS(S)                                                     \
    S::k_NumPrograms, &f_init<S>, &f_teardown<S>, &f_reset<S>, &f_resume<S>,   \
    &f_suspend<S>, &f_render<S>, &f_set<S>, &f_get<S>, &f_text<S>,             \
    &f_note_on<S>, &f_note_off<S>, &f_gate_on<S>, &f_gate_off<S>,              \
    &f_all_note_off<S>, &f_pitch_bend<S>, &f_sounding<S>, &f_set_header<S>

// The engines: every family is resident, each as a ring-over pair (two copies:
// a Program change lets the ringing one ring out), so a switch never
// constructs anything.
static brc::RingOver<fam_kick::BrachettiSynth>     s_kick;
static brc::RingOver<fam_tom::BrachettiSynth>      s_tom;
static brc::RingOver<fam_kernel::BrachettiSynth>   s_kernel;
static brc::RingOver<fam_snare::BrachettiSynth>    s_snare;
static brc::RingOver<fam_bar::BrachettiSynth>      s_bar;
static brc::RingOver<fam_plate::BrachettiSynth>    s_plate;
static brc::RingOver<fam_cymbal::BrachettiSynth>   s_cymbal;
static brc::RingOver<fam_noise::BrachettiSynth>    s_noise;
static brc::RingOver<fam_world::BrachettiSynth>    s_world;
static brc::RingOver<fam_lippa::BrachettiSynth>    s_lippa;
static brc::RingOver<fam_pizzico::BrachettiSynth>  s_pizzico;
static brc::RingOver<fam_cordiera::BrachettiSynth> s_cordiera;

// Where each family's knobs sit in Brachetti's 24 slots.  Pages 1-2 are the
// common knobs, the same slot on every family.  Pages 3-6 are Brachetti's
// standard dedicated layout (header.c): MlltRes MlltStif VlMllRes VlMllStf /
// Partls Model Dkay Rel / Mterl HitPos Inharm TubRad / NzMix NzRes NzFltr
// NzFltFrq.  The nine families built on that layout keep it (their own
// relabelled or re-purposed knobs stay in their slot); the cymbal, the kernel
// and the noise family, which have fewer knobs, are placed so that each knob
// lands in the slot of its own kind.
#define BRC_COMMON_SLOTS 0, 1, 2, 3, 4, 5, 6, 7
#define N kNone
static const Family kFamilies[kNumFamilies] = {
    {"Kick",     &s_kick,     &brc_hdr_kick,
     {BRC_COMMON_SLOTS, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23},
     BRACHETTI_CALLS(brc::RingOver<fam_kick::BrachettiSynth>)},
    {"Tom",      &s_tom,      &brc_hdr_tom,
     {BRC_COMMON_SLOTS, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23},
     BRACHETTI_CALLS(brc::RingOver<fam_tom::BrachettiSynth>)},
    // Kernel: Fill (its 12) in Partls; Punch (19) in Model's slot; Dkay Rel
    // Mterl HitPos Inharm TubRad (13-18) one slot on, where they belong;
    // NzMix (20) and NzFltFrq (21) in their own slots.
    {"Kernel",   &s_kernel,   &brc_hdr_kernel,
     {BRC_COMMON_SLOTS, 8, 9, 10, 11, 12, 19, 13, 14, 15, 16, 17, 18, 20, N, N, 21},
     BRACHETTI_CALLS(brc::RingOver<fam_kernel::BrachettiSynth>)},
    {"Snare",    &s_snare,    &brc_hdr_snare,
     {BRC_COMMON_SLOTS, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23},
     BRACHETTI_CALLS(brc::RingOver<fam_snare::BrachettiSynth>)},
    {"Bar",      &s_bar,      &brc_hdr_bar,
     {BRC_COMMON_SLOTS, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23},
     BRACHETTI_CALLS(brc::RingOver<fam_bar::BrachettiSynth>)},
    {"Plate",    &s_plate,    &brc_hdr_plate,
     {BRC_COMMON_SLOTS, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23},
     BRACHETTI_CALLS(brc::RingOver<fam_plate::BrachettiSynth>)},
    // Cymbal: as the kernel, without the noise page.
    {"Cymbal",   &s_cymbal,   &brc_hdr_cymbal,
     {BRC_COMMON_SLOTS, 8, 9, 10, 11, 12, 19, 13, 14, 15, 16, 17, 18, N, N, N, N},
     BRACHETTI_CALLS(brc::RingOver<fam_cymbal::BrachettiSynth>)},
    // Noise: GrnDpth (9) and GrnRate (8) swap into the MlltRes / MlltStif
    // kinds; GrnFade Swell (10, 11) stay; Band (16) and VelTone (18) in
    // Partls' and Model's slots; Punch (19) in Rel's, where five other
    // families have it; Rel (17) in TubRad's (the other 0-20 knob); the noise
    // page (12-15) on the noise page.
    {"Noise",    &s_noise,    &brc_hdr_noise,
     {BRC_COMMON_SLOTS, 9, 8, 10, 11, 16, 18, N, 19, N, N, N, 17, 12, 13, 14, 15},
     BRACHETTI_CALLS(brc::RingOver<fam_noise::BrachettiSynth>)},
    {"World",    &s_world,    &brc_hdr_world,
     {BRC_COMMON_SLOTS, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23},
     BRACHETTI_CALLS(brc::RingOver<fam_world::BrachettiSynth>)},
    {"Lippa",    &s_lippa,    &brc_hdr_lippa,
     {BRC_COMMON_SLOTS, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23},
     BRACHETTI_CALLS(brc::RingOver<fam_lippa::BrachettiSynth>)},
    {"Pizzico",  &s_pizzico,  &brc_hdr_pizzico,
     {BRC_COMMON_SLOTS, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23},
     BRACHETTI_CALLS(brc::RingOver<fam_pizzico::BrachettiSynth>)},
    {"Cordiera", &s_cordiera, &brc_hdr_cordiera,
     {BRC_COMMON_SLOTS, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23},
     BRACHETTI_CALLS(brc::RingOver<fam_cordiera::BrachettiSynth>)},
};
#undef N
#undef BRC_COMMON_SLOTS
#undef BRACHETTI_CALLS

// The display buffers of Brachetti's own strings (one per slot, as in
// brc::knob_text: the OS may keep one on screen while it asks for another).
// Namespace-scope static: private to this unit (see brc::name_at).
static char s_text[kNumSlots][brc::kKnobTextSize];

// ── Knob value translation ──────────────────────────────────────────────────
// a·num/den rounded to nearest, for a >= 0, num >= 0, den > 0.
inline int32_t mul_div_round(int32_t a, int32_t num, int32_t den) {
    return (int32_t)(((int64_t)a * num + den / 2) / den);
}

// A family knob's value -> Brachetti's slot value.  Identity where the ranges
// match; otherwise the family's range spread over the slot's.  Every slot is
// at least as wide as any knob in it, so slot_to_family(family_to_slot(v)) == v.
inline int32_t family_to_slot(const unit_param_t& s, const unit_param_t& f, int32_t v) {
    if (s.min == f.min && s.max == f.max) return v;
    const int32_t fspan = f.max - f.min, sspan = s.max - s.min;
    if (fspan <= 0) return s.min;
    v = brc::knob::clamp_int(v, f.min, f.max);
    return s.min + mul_div_round(v - f.min, sspan, fspan);
}

inline int32_t slot_to_family(const unit_param_t& s, const unit_param_t& f, int32_t v) {
    if (s.min == f.min && s.max == f.max) return brc::knob::clamp_int(v, s.min, s.max);
    const int32_t fspan = f.max - f.min, sspan = s.max - s.min;
    if (sspan <= 0) return f.min;
    v = brc::knob::clamp_int(v, s.min, s.max);
    return f.min + mul_div_round(v - s.min, fspan, sspan);
}

// Slot `id`'s value -> the family knob's, and back.  (Cutoff and NzFltFrq
// step 0.1 kHz: the family's own header says so and each family's RingOver
// translates them, exactly as on the family unit — knob_steps.h.)
inline int32_t to_family(uint8_t id, const unit_param_t& f, int32_t v) {
    return slot_to_family(unit_header.params[id], f, v);
}
inline int32_t to_slot(uint8_t id, const unit_param_t& f, int32_t v) {
    return family_to_slot(unit_header.params[id], f, v);
}

// A numeric knob as the drumlogue would show it: `frac` fractional bits
// (fixed mode) or decimals (decimal mode), and a % for percent knobs.
inline const char* format_number(char* buf, size_t size, const unit_param_t& p, int32_t v) {
    int32_t scaled = v, digits = p.frac;
    if (p.frac_mode == k_unit_param_frac_mode_fixed && p.frac > 0) {
        // v / 2^frac has exactly `frac` decimals: v · 5^frac / 10^frac.
        for (int i = 0; i < p.frac; ++i) scaled *= 5;
    }
    int32_t pow10 = 1;
    for (int i = 0; i < digits; ++i) pow10 *= 10;
    const char* sign = (scaled < 0) ? "-" : "";
    const int32_t mag = (scaled < 0) ? -scaled : scaled;
    const char* pct = (p.type == k_unit_param_type_percent) ? "%" : "";
    if (digits > 0) snprintf(buf, size, "%s%d.%0*d%s", sign, (int)(mag / pow10), (int)digits, (int)(mag % pow10), pct);
    else            snprintf(buf, size, "%s%d%s", sign, (int)mag, pct);
    return buf;
}

// ==============================================================================
class Synth {
public:
    int8_t Init(const unit_runtime_desc_t* desc) {
        m_desc = *desc;
        for (const Family& f : kFamilies) {
            f.set_header(f.engine, f.hdr);   // the family unit's knob steps, as on the unit
            const int8_t err = f.init(f.engine, desc);
            if (err != k_unit_err_none) return err;
        }
        for (uint8_t i = 0; i < kNumSlots; ++i) m_echo[i] = unit_header.params[i].init;
        m_family.store(0);
        m_gate_family = 0;
        return k_unit_err_none;
    }

    void Teardown() { for (const Family& f : kFamilies) f.teardown(f.engine); }
    void Reset()    { for (const Family& f : kFamilies) f.reset(f.engine); }
    void Resume()   { for (const Family& f : kFamilies) f.resume(f.engine); }
    void Suspend()  { for (const Family& f : kFamilies) f.suspend(f.engine); }

    // ── Presets: the families ───────────────────────────────────────────────
    static const char* PresetName(uint8_t idx) {
        return (idx < kNumFamilies) ? kFamilies[idx].name : "";
    }
    uint8_t PresetIndex() const { return m_family.load(); }

    // Switch to family `idx` on its first instrument, the panel's
    // performance and master knobs carried over; the family left behind rings
    // out.  Re-selecting the family already playing goes back to its first
    // instrument (a Program change: what rings, rings on).
    void LoadPreset(uint8_t idx) {
        if (idx >= kNumFamilies) return;
        if (idx == m_family.load()) { active().set(active().engine, 0, 0); return; }
        const Family& f = kFamilies[idx];
        if (f.sounding(f.engine))
            f.set(f.engine, 0, 0);       // still ringing from before: ring over onto instrument 0
        else
            f.init(f.engine, &m_desc);   // fresh: knobs at defaults, voices silent, instrument 0
        for (uint8_t p = brc::CommonLayout::k_paramPitch; p <= brc::CommonLayout::k_paramResnc; ++p)
            if (!blank(f, p)) f.set(f.engine, p, to_family(p, f.hdr->params[p], m_echo[p]));
        for (uint8_t i = brc::CommonLayout::k_firstDedicated; i < kNumSlots; ++i)
            m_echo[i] = unit_header.params[i].init;   // the unused slots rest at their default
        m_family.store(idx, std::memory_order_release);
    }

    // ── Knobs ───────────────────────────────────────────────────────────────
    void SetParameter(uint8_t id, int32_t value) {
        if (id >= kNumSlots) return;
        m_echo[id] = value;
        const Family& f = active();
        const uint8_t fs = f.slot[id];
        if (fs == kNone || blank(f, fs)) return;
        if (id == brc::CommonLayout::k_paramProgram) {   // this family's instruments only
            f.set(f.engine, fs, brc::knob::clamp_int(value, 0, f.num_programs - 1));
            return;
        }
        f.set(f.engine, fs, to_family(id, f.hdr->params[fs], value));
    }

    int32_t GetParameter(uint8_t id) const {
        if (id >= kNumSlots) return 0;
        const Family& f = active();
        const uint8_t fs = f.slot[id];
        if (fs == kNone || blank(f, fs)) return m_echo[id];
        if (id == brc::CommonLayout::k_paramProgram) return f.get(f.engine, fs);   // not scaled: an index
        return to_slot(id, f.hdr->params[fs], f.get(f.engine, fs));
    }

    const char* GetParameterText(uint8_t id, int32_t value) const {
        if (id >= kNumSlots) return "";
        const Family& f = active();
        const uint8_t fs = f.slot[id];
        if (fs == kNone || blank(f, fs)) return "---";
        if (id == brc::CommonLayout::k_paramProgram)
            return f.text(f.engine, fs, brc::knob::clamp_int(value, 0, f.num_programs - 1));
        const unit_param_t& fp = f.hdr->params[fs];
        const int32_t v = to_family(id, fp, value);
        if (fp.type == k_unit_param_type_strings) return f.text(f.engine, fs, v);
        return format_number(s_text[id], brc::kKnobTextSize, fp, v);
    }

    // ── Notes ───────────────────────────────────────────────────────────────
    // A strike goes to the selected family; a note-off, all-notes-off and pitch
    // bend to every family (one may still ring from before a switch); a gate's
    // release to the family the gate struck.
    void NoteOn(uint8_t note, uint8_t vel) { active().note_on(active().engine, note, vel); }
    void NoteOff(uint8_t note)    { for (const Family& f : kFamilies) f.note_off(f.engine, note); }
    void GateOn(uint8_t vel) {
        if (vel == 0) { GateOff(); return; }
        m_gate_family = m_family.load();
        active().gate_on(active().engine, vel);
    }
    void GateOff()                { kFamilies[m_gate_family].gate_off(kFamilies[m_gate_family].engine); }
    void AllNoteOff()             { for (const Family& f : kFamilies) f.all_note_off(f.engine); }
    void PitchBend(uint16_t bend) { for (const Family& f : kFamilies) f.pitch_bend(f.engine, bend); }

    // ── Audio ───────────────────────────────────────────────────────────────
    // The selected family, plus every other family still ringing.
    void Render(float* out, size_t frames) {
        const uint8_t cur = m_family.load(std::memory_order_acquire);
        const Family& f = kFamilies[cur];
        f.render(f.engine, out, frames);
        for (uint8_t i = 0; i < kNumFamilies; ++i) {
            const Family& r = kFamilies[i];
            if (i == cur || !r.sounding(r.engine)) continue;
            float tmp[kChunk * 2];
            for (size_t done = 0; done < frames; done += kChunk) {
                const size_t n = (frames - done < kChunk) ? frames - done : kChunk;
                r.render(r.engine, tmp, n);
                for (size_t k = 0; k < 2 * n; ++k) out[2 * done + k] += tmp[k];
            }
        }
    }

private:
    const Family& active() const { return kFamilies[m_family.load(std::memory_order_acquire)]; }

    // A knob the family unit declares as a blank slot (no Poly on the kernel,
    // no Pitch on the noise family).
    static bool blank(const Family& f, uint8_t fs) { return f.hdr->params[fs].name[0] == '\0'; }

    unit_runtime_desc_t  m_desc{};
    std::atomic<uint8_t> m_family{0};      // the selected family (the preset)
    int32_t              m_echo[kNumSlots] = {0};   // what the host last set, per slot
    uint8_t              m_gate_family = 0;   // the family the last gate struck
};

}  // namespace brachetti
