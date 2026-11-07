#pragma once
// Brachetti common library — the modal bank and the knobs that shape it.
//
// A strike rings up to six modes, each a two-pole oscillator
// y[n] = k·y[n-1] − y[n-2] with its own exponential decay.  A unit's preset
// table gives each preset a calibrated ModalPresetConfig; on every strike the
// knobs reshape it RELATIVE to the knob values the preset shipped with
// (REFERENCE ANCHORS), so at the shipped values every mapping is a no-op and
// the calibrated sound plays bit-identically:
//
//   shape_modal()   Model, Partls, Inharm, Mterl, TubRad → ratios, count,
//                   T60s, mode amplitudes        (before the bank is struck)
//   ModalBank::init the strike: tuned to the note, scaled by velocity
//   strike_modal()  Dkay + Rel → ring length, MlltStif → brightness tilt,
//                   MlltRes → presence, HitPos → which modes the strike feeds
#include <cstddef>
#include <cstdint>
#include <cstdio>
#if defined(__ARM_NEON) || defined(__ARM_NEON__)
#include <arm_neon.h>
#endif
#include "dsp_math.h"
#include "knobs.h"
#include "models.h"

namespace brc {

constexpr int kMaxModes = 6;

// One preset's calibrated bank.  Modes 5 and 6 are optional (mode_count 5-6);
// a ratio of 0 for them means "derive from mode 4".
struct ModalPresetConfig {
    float ratio2; float ratio3; float ratio4;
    float t60_1_ms; float t60_2_ms; float t60_3_ms; float t60_4_ms;
    float mix;
    float env1; float env2; float env3; float env4;
    uint8_t mode_count;
    float ratio5; float ratio6;
    float env5; float env6;
};

// Model → mode-ratio template (modes 2..6 relative to the fundamental).  Used
// when the Model knob leaves the preset's shipped model, and as the fallback
// when Partls raises the count beyond what a preset's row defines.
// Sources: Fletcher & Rossing, "The Physics of Musical Instruments", 2nd ed.
//   String/OpenTube: harmonic series.       ClosedTube: odd harmonics.
//   Beam: free-free bar (3.011²:5²:7²… /3.011²).  Membrane: Bessel J zeros.
//   SquarePlate: free square plate (Chladni).     Plate: thick circular plate
//   (Chaigne & Doutaut).  Drumhead: air-loaded kettledrum quasi-harmonic
//   principal modes.  MarimbaBar: deep-arch tuned bar 1:4:10.
static const float kModelRatios[k_NumModels][5] = {
    /* k_String      */ {2.000f, 3.000f, 4.000f, 5.000f, 6.000f},
    /* k_Beam        */ {2.756f, 5.404f, 8.933f, 13.345f, 18.638f},
    /* k_SquarePlate */ {1.520f, 1.940f, 2.710f, 3.210f, 3.730f},
    /* k_Membrane    */ {1.594f, 2.136f, 2.296f, 2.653f, 2.918f},
    /* k_Plate       */ {2.920f, 6.370f, 11.750f, 14.000f, 19.000f},
    /* k_Drumhead    */ {1.500f, 1.983f, 2.444f, 2.896f, 3.341f},
    /* k_MarimbaBar  */ {3.984f, 10.668f, 18.000f, 25.000f, 32.000f},
    /* k_OpenTube    */ {2.000f, 3.000f, 4.000f, 5.000f, 6.000f},
    /* k_ClosedTube  */ {3.000f, 5.000f, 7.000f, 9.000f, 11.000f},
};

// The bank one strike plays: a preset's config after the knobs reshaped it.
struct ModalShape {
    float   ratio[kMaxModes];   // frequency / fundamental (ratio[0] = 1)
    float   t60_ms[4];          // modes 1-4; 5 and 6 derive theirs from mode 4
    float   env[kMaxModes];     // initial amplitudes
    float   mix;
    uint8_t count;

    static ModalShape from(const ModalPresetConfig& c, float mix) {
        ModalShape s;
        s.ratio[0] = 1.0f;  s.ratio[1] = c.ratio2; s.ratio[2] = c.ratio3;
        s.ratio[3] = c.ratio4; s.ratio[4] = c.ratio5; s.ratio[5] = c.ratio6;
        s.t60_ms[0] = c.t60_1_ms; s.t60_ms[1] = c.t60_2_ms;
        s.t60_ms[2] = c.t60_3_ms; s.t60_ms[3] = c.t60_4_ms;
        s.env[0] = c.env1; s.env[1] = c.env2; s.env[2] = c.env3;
        s.env[3] = c.env4; s.env[4] = c.env5; s.env[5] = c.env6;
        s.mix = mix;
        s.count = c.mode_count;
        return s;
    }
};

// The knob values (raw, as header.c stores them) the modal mappings read.
struct ModalKnobs {
    int32_t model, partls, inharm, material, tube_radius;
    int32_t decay, release, mallet_stiff, mallet_res, hit_pos;
};

// Is an anchored factor far enough from 1 to apply?  (At the anchor it is
// exactly 1, and the calibrated values must pass through untouched.)
inline bool moved(float factor) { return factor < 0.999f || factor > 1.001f; }

// ── Before the strike: Model, Partls, Inharm, Mterl, TubRad ─────────────────
// Returns the TubRad body-size factor it applied (1.0 when it did not), for
// units whose body also feeds another layer.
inline float shape_modal(ModalShape& s, const ModalKnobs& k, const Anchors& a, uint8_t model) {
    const uint8_t model_now = (model < k_NumModels) ? model : (uint8_t)k_String;
    float* r = s.ratio;
    float* t = s.t60_ms;
    float* e = s.env;

    // Model: moving the knob off the shipped model swaps the calibrated
    // ratios for the selected physical model's template.
    if (model_now != a.model) {
        const float* tr = kModelRatios[model_now];
        r[1] = tr[0]; r[2] = tr[1]; r[3] = tr[2]; r[4] = tr[3]; r[5] = tr[4];
    }
    // Partls: the mode count AND the overtone richness, offset around the
    // shipped count (added modes are high and quiet, so the count alone would
    // be inaudible).  Each step is a clear brightness change.
    if (k.partls >= 0 && k.partls <= 4 && k.partls != a.partls) {
        const int d = k.partls - a.partls;
        const int c = (int)s.count + d;
        s.count = (uint8_t)((c < 2) ? 2 : ((c > 6) ? 6 : c));
        const float rich = clampf(knob_exp2(1.0f * (float)d), 0.15f, 6.0f);
        e[1] *= clampf(rich, 0.5f, 2.0f);
        e[2] *= rich; e[3] *= rich; e[4] *= rich; e[5] *= rich;
    }
    // Modes a raised count needs but the calibrated row does not define
    // (they would otherwise sit at 0 Hz with 0 gain).
    if (s.count > 2) {
        const float* tr = kModelRatios[model_now];
        if (r[2] <= 0.0f) r[2] = tr[1];
        if (r[3] <= 0.0f) r[3] = tr[2];
        if (r[4] <= 0.0f) r[4] = tr[3];
        if (r[5] <= 0.0f) r[5] = tr[4];
        if (t[1] <= 0.0f) t[1] = t[0] * 0.65f;
        if (t[2] <= 0.0f) t[2] = t[1] * 0.65f;
        if (t[3] <= 0.0f) t[3] = t[2] * 0.65f;
        if (e[2] <= 0.0f) e[2] = e[1] * 0.60f;
        if (e[3] <= 0.0f) e[3] = e[2] * 0.60f;
        if (e[4] <= 0.0f) e[4] = e[3] * 0.60f;
        if (e[5] <= 0.0f) e[5] = e[4] * 0.60f;
    }
    // Inharm: overtone spread around the fundamental, ratio' = 1 + (ratio−1)·
    // spread.  Exponential (0.30 … 3.36 over the bipolar range): a linear
    // spread reaches its floor partway down and collapses every partial onto
    // the fundamental.
    float spread = knob_exp2(1.75f * (knob::inharm(k.inharm) - a.inharm));
    if (moved(spread)) {
        spread = clampf(spread, 0.05f, 5.0f);
        for (int i = 1; i < kMaxModes; ++i)
            if (r[i] > 0.0f) r[i] = 1.0f + (r[i] - 1.0f) * spread;
    }
    // Mterl: material damping of the upper modes — metal sustains its
    // overtones, wood damps them.  Mode 1 keeps its T60 (overall length is
    // Dkay's job), and the upper modes' initial energy tilts too (gently, √):
    // damping only the tail is inaudible on fundamental-dominated drums.
    float mat = knob_exp2(3.4f * (knob::material(k.material) - a.material));
    if (moved(mat)) {
        mat = clampf(mat, 0.08f, 9.0f);
        t[1] *= mat;
        t[2] *= mat;
        t[3] *= mat;   // modes 5/6 derive their decay from mode 4
        const float matenv = clampf(sqrtf(mat), 0.25f, 3.2f);
        e[1] *= clampf(matenv, 0.5f, 2.2f);
        e[2] *= matenv; e[3] *= matenv; e[4] *= matenv; e[5] *= matenv;
    }
    // TubRad: body size — a wider body rings ALL its modes longer (a mode-1
    // change alone is inaudible on fundamental-dominated instruments).
    float tub = knob_exp2(1.9f * (knob::tube_radius(k.tube_radius) - a.tube_radius));
    if (moved(tub)) {
        tub = clampf(tub, 0.25f, 3.5f);
        t[0] *= tub; t[1] *= tub; t[2] *= tub; t[3] *= tub;
        return tub;
    }
    return 1.0f;
}

struct ModalBank {
    bool     active     = false;
    uint8_t  count      = 0;      // modes ringing (2-6)
    float    mix        = 0.0f;
    uint32_t norm_count = 0;      // drift-control clock
    float    k[kMaxModes]     = {};
    float    y1[kMaxModes]    = {};
    float    y2[kMaxModes]    = {};
    float    env[kMaxModes]   = {};
    float    decay[kMaxModes] = {0.9990f, 0.9985f, 0.9980f, 0.9975f, 0.9970f, 0.9965f};

    void reset() {
        active = false;
        count = 0;
        mix = 0.0f;
        norm_count = 0;
        static const float kIdleDecay[kMaxModes] = {0.9990f, 0.9985f, 0.9980f, 0.9975f, 0.9970f, 0.9965f};
        for (int i = 0; i < kMaxModes; ++i) {
            k[i] = 0.0f; y1[i] = 0.0f; y2[i] = 0.0f; env[i] = 0.0f;
            decay[i] = kIdleDecay[i];
        }
    }

    bool mode_on(int i) const { return i < 2 || count > i; }

    // Strike the bank: tuned to the note, amplitudes scaled by velocity.
    // Exact math, NOT the faster* approximations: fastercosfullf's ~1e-3 error
    // shifts low mode frequencies (an 80-170 Hz 1 : 1.504 : 1.742 spread
    // collapses toward 1 : 1.41 : 1.61 and the modes beat), and fasterexpf is
    // badly wrong for the tiny arguments of long T60s.  This runs once per
    // NoteOn, so accuracy wins.
    void init(uint8_t note, float velocity, const ModalShape& s) {
        const float* r = s.ratio;
        float base_f = 440.0f * exp2f(((float)note - 69.0f) * 0.08333333333f);   // 1/12
        if (base_f < 20.0f) base_f = 20.0f;
        const float nyq = 0.45f * kSampleRate;
        float f[kMaxModes];
        f[0] = fminf(base_f, nyq);
        f[1] = fminf(base_f * r[1], nyq);
        f[2] = fminf(base_f * r[2], nyq);
        f[3] = fminf(base_f * r[3], nyq);
        // Modes 5/6: explicit ratios when given, else derived from mode 4.
        f[4] = fminf(base_f * (r[4] > 0.0f ? r[4] : r[3] * 1.31f), nyq);
        f[5] = fminf(base_f * (r[5] > 0.0f ? r[5] : r[3] * 1.62f), nyq);
        active = true;
        count  = s.count;
        // Seed each oscillator at full amplitude (a cosine pair y2 = cos w,
        // y1 = 1), so it starts at its peak on frame 0 instead of building up
        // from sin(w) over ~1 ms — the right initial energy for a struck body.
        for (int i = 0; i < kMaxModes; ++i) {
            const float w = (M_TWOPI * f[i]) * kInvSampleRate;
            y2[i] = mode_on(i) ? cosf(w) : 0.0f;
            y1[i] = mode_on(i) ? 1.0f : 0.0f;
        }
        for (int i = 0; i < kMaxModes; ++i) k[i] = 2.0f * y2[i];
        norm_count = 0;
        for (int i = 0; i < kMaxModes; ++i)
            env[i] = mode_on(i) ? (s.env[i] * velocity) : 0.0f;
        // Per-sample decay for a T60: exp(ln 0.001 / (T60 · fs)).  Modes 5 and
        // 6 decay faster than mode 4 (T60 × 0.85 and × 0.70), computed as
        // decay4^(1/r) instead of two more expf.
        float t60_s[4];
        for (int i = 0; i < 4; ++i) t60_s[i] = 0.001f * s.t60_ms[i];
        decay[0] = (t60_s[0] > 0.0f) ? expf(kLn0001 / (t60_s[0] * kSampleRate)) : 0.99905f;
        decay[1] = (t60_s[1] > 0.0f) ? expf(kLn0001 / (t60_s[1] * kSampleRate)) : 0.99810f;
        decay[2] = (count > 2 && t60_s[2] > 0.0f) ? expf(kLn0001 / (t60_s[2] * kSampleRate)) : 0.99810f;
        decay[3] = (count > 3 && t60_s[3] > 0.0f) ? expf(kLn0001 / (t60_s[3] * kSampleRate)) : 0.99810f;
        decay[4] = (count > 4 && t60_s[3] > 0.0f) ? powf(decay[3], 1.0f / 0.85f) : 0.99810f;
        decay[5] = (count > 5 && t60_s[3] > 0.0f) ? powf(decay[3], 1.0f / 0.70f) : 0.99810f;
        mix = s.mix;
    }

    // Ring length × scale (scale < 1 = shorter), as decay^(1/scale).
    void scale_ring(float scale) {
        if (!moved(scale)) return;
        const float exp_scale = 1.0f / scale;
        for (int i = 0; i < kMaxModes; ++i) decay[i] = powf(decay[i], exp_scale);
    }

    // One sample of the bank, already weighted mode by mode (mode 2 at 0.6,
    // then 0.45, 0.28, 0.18, 0.12).  Switches itself off once every ringing
    // mode is below kSilence.
    float process() {
#if defined(__ARM_NEON) || defined(__ARM_NEON__)
        float32x2_t k12  = {k[0], k[1]};
        float32x2_t y112 = {y1[0], y1[1]};
        float32x2_t y212 = {y2[0], y2[1]};
        float32x2_t yn12 = vsub_f32(vmul_f32(k12, y112), y212);
        y2[0] = y1[0];
        y2[1] = y1[1];
        y1[0] = vget_lane_f32(yn12, 0);
        y1[1] = vget_lane_f32(yn12, 1);
        if (count > 2) {
            float32x2_t k34  = {k[2], k[3]};
            float32x2_t y134 = {y1[2], y1[3]};
            float32x2_t y234 = {y2[2], y2[3]};
            float32x2_t yn34 = vsub_f32(vmul_f32(k34, y134), y234);
            y2[2] = y1[2];
            y2[3] = y1[3];
            y1[2] = vget_lane_f32(yn34, 0);
            y1[3] = vget_lane_f32(yn34, 1);
        }
#else
        step(0); step(1);
        if (count > 2) { step(2); step(3); }
#endif
        if (count > 4) { step(4); step(5); }

        // Drift control: every 128 samples, renormalise any oscillator whose
        // amplitude has crept above 1.2 (float rounding over long tails).
        if ((norm_count++ & 127u) == 0u) {
            renormalise(0); renormalise(1);
            if (count > 2) {
                renormalise(2); renormalise(3);
                if (count > 4) { renormalise(4); renormalise(5); }
            }
        }

        const float m1 = y1[0] * env[0];
        const float m2 = y1[1] * env[1];
        float m3 = 0.0f, m4 = 0.0f, m5 = 0.0f, m6 = 0.0f;
        env[0] *= decay[0];
        env[1] *= decay[1];
        if (count > 2) {
            m3 = y1[2] * env[2];
            m4 = y1[3] * env[3];
            env[2] *= decay[2];
            env[3] *= decay[3];
            if (count > 4) {
                m5 = y1[4] * env[4];
                m6 = y1[5] * env[5];
                env[4] *= decay[4];
                env[5] *= decay[5];
            }
        }
        const float sum = m1 + (0.6f * m2) + (0.45f * m3) + (0.28f * m4) + (0.18f * m5) + (0.12f * m6);

        if (env[0] < kSilence && env[1] < kSilence &&
            (count <= 2 || (env[2] < kSilence && env[3] < kSilence &&
                            (count <= 4 || (env[4] < kSilence && env[5] < kSilence))))) {
            active = false;
            count = 0;
        }
        return sum;
    }

private:
    void step(int i) {
        const float yn = (k[i] * y1[i]) - y2[i];
        y2[i] = y1[i];
        y1[i] = yn;
    }
    void renormalise(int i) {
        const float a = fmaxf(fabsf(y1[i]), fabsf(y2[i]));
        if (a > 1.2f) {
            const float s = 1.0f / a;
            y1[i] *= s;
            y2[i] *= s;
        }
    }
};

// What the Partls knob shows: the mode count its position gives the current
// preset (the knob offsets the preset's own count, clamped to 2..6).
inline const char* format_mode_count(char* buf, size_t size, int shipped,
                                     int32_t knob_value, int32_t anchor) {
    if (shipped == 0) return "-";   // a preset without a modal bank
    int c = shipped + knob::clamp_int(knob_value, 0, 4) - anchor;
    c = (c < 2) ? 2 : ((c > 6) ? 6 : c);
    snprintf(buf, size, "%d md", c);
    return buf;
}

// ── At the strike: Dkay, Rel, MlltStif, MlltRes, HitPos ─────────────────────
// mallet_stiffness is the exciter's, already shaped by velocity and strike
// position (Exciter::shape_strike).
inline void strike_modal(ModalBank& b, const ModalKnobs& k, const Anchors& a,
                         float mallet_stiffness) {
    // Dkay (coarse, ~±4.5 octaves of ring) and Rel (finer, 3.2) trim the
    // calibrated T60s around the shipped values.
    b.scale_ring(knob_exp2(4.5f * (knob::decay(k.decay) - a.decay)
                           + 3.2f * (knob::release(k.release) - a.release)));

    // Mallet stiffness → brightness tilt of the upper modes (mode 1 never
    // tilts).  The velocity / rim part of the stiffness is part of the
    // calibrated sound and keeps ×1.4; only the knob's travel from the
    // shipped value is widened (×3.4) — exactly 0 at the shipped value.
    const float stiff_knob = knob::mallet_stiff(k.mallet_stiff);
    const float tilt = (mallet_stiffness - stiff_knob) * 1.4f
                     + (stiff_knob - a.mallet_stiff) * 3.4f;
    if (tilt < -0.001f || tilt > 0.001f) {
        for (int i = 1; i < kMaxModes; ++i)
            b.env[i] *= clampf(1.0f + tilt * (float)i, 0.05f, 6.0f);
    }

    // MlltRes → modal presence: the bank's level, a full-range timbre control.
    b.mix *= clampf(knob_exp2(3.4f * (knob::mallet_res(k.mallet_res) - a.mallet_res)), 0.05f, 9.0f);

    // HitPos → strike position: toward the rim feeds the upper modes and
    // starves the fundamental, toward the centre the opposite (bipolar: it
    // follows the knob below centre too).  Known limit: these are linear into
    // a clamp, so the far negative corner saturates on presets that ship
    // HitPos high; reshaping them means re-deriving the coefficients by ear.
    const float hit_off = knob::hit_pos(k.hit_pos) - a.hit_pos;
    if (hit_off < -0.001f || hit_off > 0.001f) {
        static const float kFeed[kMaxModes] = {1.55f, 1.35f, 2.40f, 3.60f, 4.80f, 6.00f};
        b.env[0] *= clampf(1.0f - hit_off * kFeed[0], 0.08f, 2.4f);
        for (int i = 1; i < kMaxModes; ++i)
            b.env[i] *= clampf(1.0f + hit_off * kFeed[i], 0.05f, 9.0f);
    }
}

}  // namespace brc
