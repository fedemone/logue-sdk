#pragma once
// Brachetti common library — the master stage every voice runs into.
//
//   voices ──(mono, left lane only)──> Cutoff/Resnc low-pass ──> Gain × preset
//   trim ──> transparent peak limiter ──> both output channels
//
// plus the Tone knob's tilt EQ, which runs per voice (tone_tilt below) so it
// colours each strike before the voices are summed.
//
// The voice bus is MONO: every voice is single-channel, and render() filters
// the LEFT sample and writes the result to both channels, so voice loops add
// into main_out[i * 2] only (a stereo store would cost a load-add-store per
// sample per voice in the hottest loop of a unit).
//
// MasterControls is the knob half on its own, for a unit that runs its own
// stage (brachetti_kernel).
#include <cstddef>
#include <cstdint>
#include "dsp_math.h"
#include "filter.h"
#include "knobs.h"

namespace brc {

// ── Tone knob: tilt EQ ──────────────────────────────────────────────────────
// A one-pole low-pass splits the signal; Tone < 0 crossfades toward the
// low-pass (darker), Tone > 0 adds the high-pass part (brighter).  Every
// preset ships Tone 0, where callers skip this entirely: the low-pass then
// re-converges within ~10 samples once the knob moves — inaudible under the
// knob's own change.
constexpr float kToneLpMix   = 0.3f;
constexpr float kToneCutDiv  = 0.1f;          // 1 / 10
constexpr float kToneBoostDiv = 0.13333334f;  // 1 / 7.5

inline float tone_tilt(float x, float& lp, float tone) {
    lp = (x * kToneLpMix) + (lp * (1.0f - kToneLpMix));
    if (tone < 0.0f) return x + (lp - x) * (-tone * kToneCutDiv);
    return x + (x - lp) * (tone * kToneBoostDiv);
}

// ── The master knobs: Tone, Cutoff / Resnc, Gain ────────────────────────────
struct MasterControls {
    FastSVF filter;            // Cutoff / Resnc low-pass
    float   tone  = 0.0f;      // Tone knob, -10..30
    float   drive = 1.0f;      // Gain knob

    void set_tone(int32_t v) { tone = clampf((float)v, -10.0f, 30.0f); }
    void set_gain(int32_t v) { drive = 1.0f + (fmaxf(0.0f, (float)v * 0.01f) * 20.0f); }

    // Cutoff is stored ÷10 (1-1999 → 10-19990 Hz; set_coeffs caps it at
    // 0.47·fs).  Resnc is stored ÷10 (71-400 → Q 0.707 .. ~4.0).
    void set_filter(int32_t cutoff, int32_t resnc) {
        const float cutoff_hz = (float)knob::clamp_int(cutoff, 1, 1999) * 10.0f;
        const float q = fmaxf(0.707f, 0.707f + ((float)resnc - 71.0f) * 0.01f);
        filter.set_coeffs(cutoff_hz, q, kSampleRate);
    }

    // The filter's memory only: its coefficients are the knobs' and stay.
    void clear_filter() {
        filter.mode = 0;      // low-pass: the Cutoff knob opens it as it rises
        filter.lp = 0.0f;
        filter.bp = 0.0f;
        filter.hp = 0.0f;
    }
};

struct MasterStage : MasterControls {
    // Voice → bus gain: as loud as the bus can take before the limiter's soft
    // knee starts to narrow velocity sensitivity (a ghosted hit must stay well
    // under a neutral one).
    static constexpr float kBusGain = 2.3f;

    // ── Transparent peak limiter ────────────────────────────────────────────
    // Unity below kLimThr, a soft knee above it that is asymptotic to kLimCeil.
    // It is a gain ENVELOPE, not a waveshaper: a curve applied to every sample
    // changes the gain within a cycle and manufactures harmonics on low tones.
    // The peak is tracked by an instant-attack, slow-release follower and ONE
    // gain is applied per cycle: lim_env only jumps up and decays down, so it
    // is >= |x| at every sample and out = x · limit(env)/env stays below
    // kLimCeil BY CONSTRUCTION (the clamp after it is pure safety), while the
    // waveform inside a cycle is preserved.  The threshold only trades
    // loudness against how much gain riding happens.
    static constexpr float kLimThr  = 0.75f;
    static constexpr float kLimCeil = 0.99f;
    static constexpr float kLimSpan = kLimCeil - kLimThr;
    // Release 20 ms: keeps the strike's crest with the fewest added harmonics
    // on long low tones; longer releases only get quieter, and two hits
    // 150 ms apart show no ducking.  1-exp(-1/tau) ~= 1/tau at 960 samples.
    static constexpr float kLimRelMs   = 20.0f;
    static constexpr float kLimRelCoef = 1.0f / (kLimRelMs * 0.001f * 48000.0f);

    // Blocks the chain keeps running after the last voice died (~320 ms at 64
    // frames), so the filter tail rings out and there is no step on re-entry.
    // After that a silent unit skips the master stage entirely: the drumlogue
    // calls every unit's render continuously.
    static constexpr uint16_t kIdleFlushBlocks = 240;

    float    out_trim      = 1.0f;    // the current preset's output trim
    // Drive and trim held back while the previous preset's voices fade out
    // (-1 = nothing held); see hold_while_fading().
    float    pending_drive = -1.0f;
    float    pending_trim  = -1.0f;
    float    lim_env       = 0.0f;    // limiter peak follower, 0 = no reduction
    uint16_t idle_blocks   = 0;

    void set_gain(int32_t v) {
        MasterControls::set_gain(v);
        pending_drive = -1.0f;   // an explicit Gain turn wins over a held one
    }

    // ── Preset changes ──────────────────────────────────────────────────────
    // A new preset brings its own Gain and output trim, but the previous
    // preset's voices are still fading out through this stage; the incoming
    // values would step their tails by up to 22 dB.  So while any voice fades,
    // the outgoing values stay and the incoming ones wait in pending_*.
    void hold_while_fading(float prev_drive, float prev_trim, bool fading) {
        if (!fading) return;
        if (drive != prev_drive)  { pending_drive = drive;    drive    = prev_drive; }
        if (out_trim != prev_trim) { pending_trim = out_trim; out_trim = prev_trim; }
    }

    // Once the last fading voice has retired, the held values take over.
    // (Called every block: the voices are only scanned while something is held.)
    template <class Voice, int N>
    void release_held(const Voice (&voices)[N]) {
        if (pending_drive < 0.0f && pending_trim < 0.0f) return;
        for (int i = 0; i < N; ++i)
            if (voices[i].is_active && voices[i].fade_mul < 1.0f) return;
        if (pending_drive >= 0.0f) { drive = pending_drive; pending_drive = -1.0f; }
        if (pending_trim  >= 0.0f) { out_trim = pending_trim; pending_trim = -1.0f; }
    }

    // Runtime state only: the knob-derived settings stay.  Values held behind
    // a fade belong to the preset already loaded, and the voices they waited
    // for are gone, so they take over now.
    void reset() {
        if (pending_drive >= 0.0f) drive    = pending_drive;
        if (pending_trim  >= 0.0f) out_trim = pending_trim;
        pending_drive = -1.0f;
        pending_trim  = -1.0f;
        lim_env     = 0.0f;   // a high envelope would duck the next strike
        idle_blocks = 0;
        clear_filter();
    }

    // ── Rendering ───────────────────────────────────────────────────────────
    static void clear(float* out, size_t frames) {
        for (size_t i = 0; i < frames * 2; ++i) out[i] = 0.0f;
    }

    // Idle guard: false once the bus has been silent for kIdleFlushBlocks.
    bool should_run(bool any_voice_rendered) {
        if (any_voice_rendered) { idle_blocks = kIdleFlushBlocks; return true; }
        if (idle_blocks > 0)    { --idle_blocks; return true; }
        return false;
    }

    float limit(float x) {
        const float ax = fabsf(x);
        lim_env = (ax > lim_env) ? ax : lim_env + (ax - lim_env) * kLimRelCoef;
        const float a = lim_env;
        if (a > kLimThr) {
            // y = thr + span · over / (over + span), over = a - thr: unity
            // slope at the knee, monotone, strictly below the ceiling for every
            // finite input.  Rejected alternatives: x/(1+|x|) compresses from
            // zero up (-3.5 dB at 0.5); a clamped tanh knee pins thousands of
            // samples dead flat, where this curve pins none.
            const float over = a - kLimThr;
            const float y = kLimThr + kLimSpan * over / (over + kLimSpan);
            x *= y / a;   // one gain for the whole cycle
        }
        return fmaxf(-kLimCeil, fminf(kLimCeil, x));
    }

    // The mono bus in out[i * 2] → filter → drive × trim → limiter → stereo.
    void render(float* out, size_t frames) {
        const float stage_gain = drive * out_trim;   // constant within a block
        for (size_t i = 0; i < frames; ++i) {
            float x = filter.process(out[i * 2]);
            x *= stage_gain;
            x = limit(x);
            out[i * 2]     = x;
            out[i * 2 + 1] = x;
        }
    }
};

}  // namespace brc
