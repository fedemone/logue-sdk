#pragma once
// Brachetti common library — the exciter: what a strike puts into a voice.
//
// Two sources, both shaped per strike:
//  * the MALLET: a unit impulse through two cascaded one-pole low-passes
//    (MlltStif = attack sharpness, MlltRes = body brightness);
//  * the NOISE BURST: filtered white noise (NzFltr / NzFltFrq) split into a
//    slow "body" band and a fast "click" band, each with its own envelope
//    (NzRes shapes them, Rel releases them), mixed by noise_band_mix and
//    scaled by NzMix.  Units mix the burst in PARALLEL with their resonators
//    (noise_out_sample), so the broadband texture is not pitch-filtered away.
// NzMix crossfades the two: 0 = mallet only, 100 = noise only.
//
// master_env holds every voice at 1.0 while it plays (the ring length is the
// voice's own decay); only AllNoteOff releases it, at the rate set_release_time
// derives from the Dkay knob.
#include <cstddef>
#include <cstdint>
#include "dsp_math.h"
#include "envelope.h"
#include "filter.h"
#include "knobs.h"
#include "models.h"
#include "noise.h"

namespace brc {

// Mixed into a voice's noise seed on every strike, so repeated hits do not
// replay the same noise.  The value is the original brachetti's, kept so the
// noise sequences — and so every render — stay exactly as they were.
constexpr uint32_t kNoiseSeedStir = 0x26A9DBE5u;

// What the drumlogue shows for NzFltr.
inline const char* noise_filter_name(int32_t v) {
    static const char* const names[] = {"LP", "BP", "HP"};
    return (v >= 0 && v < 3) ? names[v] : nullptr;
}

// One sample of the noise burst, split into its parts so a unit can reshape a
// part before the bands are summed (the plate's hi-hat filter, the snare's
// wires).
struct NoiseBurst {
    float raw;        // white noise before the NzFltr filter
    float filtered;   // after it
    float high;       // filtered noise minus its low-passed copy: the click band
    float env_low;    // body-band envelope
    float env_high;   // click-band envelope
    float low_part;   // enveloped, mix-weighted body band
    float high_part;  // enveloped, mix-weighted click band
};

struct Exciter {
    size_t current_frame = 0;   // samples since the strike

    FastNoise    noise_gen;
    FastEnvelope noise_env;     // body band
    FastEnvelope noise_env_hi;  // click band (short, high)
    FastEnvelope master_env;    // holds the voice; AllNoteOff releases it

    float noise_decay_coeff = 0.0f;   // NzMix 0..1
    float noise_out_sample  = 0.0f;   // this sample's burst, read by the voice
    float noise_lp_state    = 0.0f;   // body-band low-pass memory
    float noise_band_mix    = 0.5f;   // 0 = all body .. 1 = all click
    float noise_hi_lp_state = 0.0f;   // click-band split memory
    float noise_hi_lp_coeff = 0.30f;  // body/click split corner (per preset)
    float mallet_lp         = 0.0f;
    float mallet_lp2        = 0.0f;
    float mallet_stiffness  = 0.5f;   // MlltStif, shaped per strike
    float mallet_res_coeff  = 0.5f;   // MlltRes

    FastSVF noise_filter;             // NzFltr / NzFltFrq

    // ── Knobs (UI thread) ───────────────────────────────────────────────────
    void set_mallet_stiff(int32_t v) { mallet_stiffness = knob::mallet_stiff(v); }
    // A second one-pole after the stiffness pole: low = darker, softer body.
    void set_mallet_res(int32_t v)   { mallet_res_coeff = 0.01f + (knob::mallet_res(v) * 0.99f); }
    void set_noise_mix(int32_t v)    { noise_decay_coeff = knob::noise_mix(v); }

    // Rel: the noise envelopes' release (0 = fast snap, 20 = slow tail); the
    // click band always releases faster than the body.
    void set_release(int32_t v) {
        const float rel_rate = 0.00005f + ((1.0f - knob::release(v)) * 0.01f);
        noise_env.release_rate    = rel_rate;
        noise_env_hi.release_rate = fminf(0.99f, rel_rate * 2.5f);
    }

    // NzRes: attack and decay of both noise bands.  The click band attacks
    // faster and decays faster than the body.
    void set_noise_res(int32_t v) {
        const float norm = knob::noise_res(v);
        noise_env.attack_rate    = 0.9f - (norm * 0.8f);
        noise_env.decay_rate     = 0.0001f + ((1.0f - norm) * 0.005f);
        noise_env_hi.attack_rate = fminf(0.99f, (0.95f - (norm * 0.3f)));
        noise_env_hi.decay_rate  = 0.003f + ((1.0f - norm) * 0.015f);
    }

    void set_noise_filter_mode(int32_t v) { noise_filter.mode = (int)clampf((float)v, 0.0f, 2.0f); }

    // NzFltFrq is stored ÷10 (2-2000 → 20 Hz-20 kHz).  It colours the noise
    // only; the body/click split corner is the preset's noise_hi_lp_coeff.
    void set_noise_filter_freq(int32_t v) {
        const float freq = clampf((float)v * 10.0f, 20.0f, 20000.0f);
        noise_filter.set_coeffs(freq, 0.707f, kSampleRate);
    }

    // AllNoteOff fades a voice with master_env: Dkay 0..200 → 50 ms .. 10 s.
    void set_release_time(int32_t dkay) {
        const float t_s = fasterpow2f(7.643856f * knob::decay(dkay));   // log2(200)
        master_env.release_rate = M_THREELN10 * 20 * kInvSampleRate / t_s;
    }

    // What a fresh object starts with for the state the NzFltr / NzFltFrq
    // knobs own once a preset is loaded (LP, fully open).
    void init_noise_filter() {
        noise_filter.mode = 0;
        noise_filter.set_coeffs(12000.0f, 0.707f, kSampleRate);
    }

    void clear_noise_filter() {
        noise_filter.lp = 0.0f;
        noise_filter.bp = 0.0f;
        noise_filter.hp = 0.0f;
    }

    // Noise band balance for a strike: the Model knob's default, unless the
    // preset has its own (0 in its table = no opinion).  Clamped here, where it
    // is written: the per-sample code relies on it staying inside [0, 1].
    void set_band_mix(uint8_t model, float preset_mix) {
        noise_band_mix = model_noise_band_mix(model);
        if (preset_mix > 0.001f) noise_band_mix = fminf(1.0f, preset_mix);
    }

    // ── Strike (NoteOn) ─────────────────────────────────────────────────────
    // Velocity and strike position shape the exciter before it fires.  The
    // strike position comes from HitPos and the velocity: radius 0 = dead
    // centre, 1 = the rim.  FLOORED AT 0: hit_x is a distance from the centre,
    // consumed through a magnitude, so a negative HitPos would fold onto its
    // positive mirror.
    //  * VlMllStf: a harder hit is a stiffer, brighter mallet; the rim adds up
    //    to 50 %.  Knob travel is scaled into the REMAINING headroom on its
    //    side (no clamp needed, monotonic, full travel reaches the bound at
    //    full velocity), anchored at the shipped VlMllStf (vel_stiff_ref): d
    //    is 0 for every shipped preset and t0 is the calibrated stiffness.
    //  * VlMllRes: a harder hit is a faster noise attack; the rim adds up to
    //    10 %.  The click band stays snappier than the body.
    void shape_strike(float velocity, int32_t hit_pos, int32_t stiff_knob,
                      int32_t vel_stiff_knob, float vel_stiff_ref,
                      int32_t noise_res_knob, int32_t vel_res_knob) {
        const float hit_x = fmaxf(0.0f, (float)hit_pos * 0.01f);
        const float hit_y = (1.0f - velocity) * hit_x * 0.5f;
        const float radius = fminf(1.0f, sqrtsum2acc(hit_x, hit_y));

        const float base_stiff = knob::mallet_stiff(stiff_knob);
        const float stif_mod   = knob::percent(vel_stiff_knob);
        const float t0 = clampf(base_stiff + vel_stiff_ref * velocity + radius * 0.5f, 0.01f, 1.0f);
        const float raw = stif_mod - vel_stiff_ref;
        const float d   = ((raw >= 0.0f) ? (raw / fmaxf(1e-6f, 1.0f - vel_stiff_ref))
                                         : (raw / fmaxf(1e-6f, 1.0f + vel_stiff_ref)))
                          * velocity;
        mallet_stiffness = (d >= 0.0f) ? (t0 + d * (1.0f - t0))
                                       : (t0 + d * (t0 - 0.01f));

        const float base_attack = 0.9f - (knob::noise_res(noise_res_knob) * 0.8f);
        const float res_mod     = knob::percent(vel_res_knob);
        noise_env.attack_rate = clampf(base_attack + (res_mod * velocity * 1.0f) + radius * 0.1f,
                                       0.01f, 0.99f);
        noise_env_hi.attack_rate = clampf(noise_env.attack_rate * 1.25f, 0.05f, 0.99f);
    }

    // Back to the start of a strike (part of a voice's reset).
    void restart() {
        current_frame     = 0;   // the mallet fires on frame 0
        mallet_lp         = 0.0f;
        mallet_lp2        = 0.0f;
        noise_lp_state    = 0.0f;
        noise_band_mix    = 0.5f;
        noise_hi_lp_state = 0.0f;
    }

    // Fire: fresh noise, a click-free filter, both noise envelopes from zero,
    // and the master envelope holding at 1.0 (assigned directly, not through
    // trigger(): ARM -ffast-math may leave an FMA a hair under the 0.99 attack
    // threshold).
    void fire() {
        noise_gen.seed ^= kNoiseSeedStir;
        clear_noise_filter();
        noise_env.trigger();
        noise_env_hi.trigger();
        master_env.sustain_level = 1.0f;
        master_env.value         = 1.0f;
        master_env.state         = ENV_DECAY;
    }

    // NoteOff releases the noise; the voice rings on under its own decay.
    void release_noise() {
        noise_env.release();
        noise_env_hi.release();
    }

    // ── Per sample ──────────────────────────────────────────────────────────
    // Computes the burst's parts; false (and nothing consumed from the noise
    // generator) once both envelopes are closed.  noise_out_sample is reset
    // here and set by the caller from noise_sum().
    bool noise_burst(NoiseBurst& b) {
        noise_out_sample = 0.0f;
        b.env_low  = noise_env.process();
        b.env_high = noise_env_hi.process();
        if (b.env_low > 0.001f || b.env_high > 0.001f) {
            b.raw      = noise_gen.process();
            b.filtered = noise_filter.process(b.raw);
            // Both bands come from the FILTERED noise, so NzFltr / NzFltFrq
            // govern the whole burst; noise_hi_lp_coeff only routes body vs
            // click.
            noise_lp_state += 0.15f * (b.filtered - noise_lp_state);
            const float low = noise_lp_state;
            noise_hi_lp_state += noise_hi_lp_coeff * (b.filtered - noise_hi_lp_state);
            b.high = b.filtered - noise_hi_lp_state;
            // noise_band_mix is clamped where it is written, not per sample.
            const float mix = noise_band_mix;
            b.low_part  = low * (1.0f - mix) * b.env_low;
            b.high_part = b.high * mix * 1.35f * b.env_high;
            return true;
        }
        return false;
    }

    float noise_sum(const NoiseBurst& b) const {
        return (b.low_part + b.high_part) * noise_decay_coeff;
    }

    // The mallet strike, then the sample clock.  The two poles stop updating
    // once the second has decayed (1e-6 is far above the denormal range and
    // inaudible), which saves the CPU for the rest of the voice's life.
    float mallet() {
        float out = 0.0f;
        if (current_frame == 0 || mallet_lp2 > 1e-6f) {
            const float impulse = (current_frame == 0) ? 1.0f : 0.0f;
            mallet_lp  = (impulse * mallet_stiffness) + (mallet_lp * (1.0f - mallet_stiffness));
            mallet_lp2 = (mallet_lp * mallet_res_coeff) + (mallet_lp2 * (1.0f - mallet_res_coeff));
            out += mallet_lp2 * 15.0f * (1.0f - noise_decay_coeff);
        }
        current_frame++;
        return out;
    }

    // The whole exciter for units that use the burst as it is.
    float process() {
        NoiseBurst b;
        if (noise_burst(b)) noise_out_sample = noise_sum(b);
        return mallet();
    }

    bool finished() const {
        return mallet_lp2 < 1e-6f && noise_env.state == ENV_IDLE && noise_env_hi.state == ENV_IDLE;
    }
};

}  // namespace brc
