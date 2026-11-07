#pragma once
/**
 * modal_drum_kernel.h — dense coupled-resonator drum kernel (Timpani / Taiko).
 *
 * Port of the standalone ResonatorDrumSynth that produced the approved
 * references 105_timp_wedge.wav / 110_taiko_wedge.wav: one velocity-scaled
 * half-sine excitation impulse rings a bank of up to 280 two-pole resonators
 * (the drum's measured modes + a dense jittered membrane fill), a recorded
 * broadband residual transient supplies the stick "knock", a raised-cosine
 * attack bloom gives the few-ms swell to peak, and a sweeping-cutoff noise
 * wedge fills the taiko's broadband decay.  The output stage is a
 * transparent peak limiter (unity below 0.85) — NOT a soft clip — so the
 * strike keeps its crest ("wham").
 *
 * TWO KETTLES: a hit on the note a kettle is already tuned to retriggers that
 * kettle in place — its resonator states are NOT zeroed, so energy
 * accumulates like a real drum roll.  A hit on a DIFFERENT note takes a
 * free / oldest kettle and retunes it synchronously (sin/cos only — the decay
 * poles are pitch-invariant, so no expf in the note path), leaving the other
 * kettle's ring untouched.  That is what makes a note change sound like a
 * second drum instead of bending (distorting) the ringing tail.
 *
 * RT-safe: no heap, fixed structure-of-arrays state (16-byte aligned), NEON
 * inner loop with a bit-equivalent scalar fallback.  Coefficient work that
 * moves decay poles (Dkay / Rel / Mterl / Inharm / Partls) is amortised:
 * SetMods() marks the shared base tables dirty and Process() rebuilds a
 * bounded number of modes per block, so knob turns never blow the deadline.
 */

#include <stdint.h>
#include <cmath>
#include <cstring>

#include "../brachetti_common/dsp_math.h"
#include "modal_drum_data.h"

#if defined(__ARM_NEON) || defined(__ARM_NEON__)
#include <arm_neon.h>
#define MODAL_DRUM_NEON 1
#endif

class ModalDrumKernel {
public:
    static const int kMaxModes        = 280;  // timp_wedge table (multiple of 4)
    static const int kVoices          = 2;    // two kettles
    static const int kRebuildPerBlock = 48;   // modes re-based per Process() call

    // Live sound-design modifiers, all anchored so that zero deltas reproduce
    // the recipe (= the approved render) exactly.  Pitch is NOT a mod: the
    // note travels with each Trigger() and retunes one kettle synchronously.
    struct Mods {
        float decay_mult     = 1.0f; // Dkay/Rel: T60 multiplier (>1 = longer)
        float hf_decay_tilt  = 0.0f; // Mterl: +bright material rings HF longer
        float stretch        = 0.0f; // Inharm: spreads upper modes
        float density        = 1.0f; // Partls: fraction of the fill retained
        float exc_sharp      = 1.0f; // MlltStif: impulse-length divisor
        float knock_mult     = 1.0f; // MlltRes/HitPos/VlMllRes: residual gain
        float vel_sharp      = 0.6f; // VlMllStf: velocity→sharpness amount
        float vel_knock_exp  = 1.5f; // VlMllRes: knock velocity exponent
        float noise_mult     = 1.0f; // NzMix: noise-wedge level multiplier
        float noise_add      = 0.0f; // NzMix above anchor when recipe level is 0
        float noise_bright   = 1.0f; // NzFltFrq: wedge start-cutoff multiplier
    };

    // The noise wedge's start-cutoff coefficient is bounded to this range;
    // the NzFltFrq mapping lands its knob ends exactly on it.
    static constexpr float kNoiseCoefMin = 0.02f;
    static constexpr float kNoiseCoefMax = 0.90f;

    // Bind a recipe (preset load).  ref_drive is the master drive the shipped
    // preset row produces — the master stage divides it back out so the
    // shipped Gain value plays the approved render transparently.
    void Configure(const ModalDrumRecipe* recipe, float ref_drive) {
        m_recipe = recipe;
        m_ref_drive = (ref_drive > 0.05f) ? ref_drive : 1.0f;
        m_mods = Mods();
        m_sched_ready = false;
        RebuildBase(0, kMaxModes);
        m_rebuild_pos = m_num_modes_padded;
        FinishSchedule();
        for (int v = 0; v < kVoices; ++v) {
            ClearVoice(m_vc[v]);
            m_vc[v].tuned = false;
        }
        m_trigger_serial = 0;
        m_active = true;
    }

    bool  IsActive() const    { return m_active && m_recipe; }
    float RefDrive() const    { return m_ref_drive; }
    float RootNote() const    { return m_recipe ? m_recipe->root_note : 60.0f; }
    float NoiseCoefHi() const { return m_recipe ? m_recipe->noise_coef_hi : 0.5f; }

    // Hard stop (the unit's Reset): kill both kettles' rings.
    void Flush() {
        for (int v = 0; v < kVoices; ++v) ClearVoice(m_vc[v]);
    }

    // Mods that move the base tables (decay poles, stretch, density) restart
    // the amortised rebuild; trigger-time mods take effect on the next hit.
    void SetMods(const Mods& m) {
        const bool rebuild = (m.decay_mult    != m_mods.decay_mult)    ||
                             (m.hf_decay_tilt != m_mods.hf_decay_tilt) ||
                             (m.stretch       != m_mods.stretch)       ||
                             (m.density       != m_mods.density);
        // noise_bright is not a rebuild trigger: it is one multiply and two
        // clamps, applied here so a NzFltFrq turn reaches the noise wedge
        // without waiting for some other mod to force a rebuild.
        const bool bright = (m.noise_bright != m_mods.noise_bright);
        m_mods = m;
        if (rebuild && m_recipe) { m_rebuild_pos = 0; m_sched_ready = false; }
        if (bright && m_recipe) ApplyNoiseBright();
    }

    // Strike.  `note` selects / retunes a kettle; `ratio` is the transpose
    // (2^((note - root)/12)); velocity in [0, 1].
    void Trigger(uint8_t note, float ratio, float velocity) {
        if (!IsActive()) return;
        // Ceiling 2.0, not 1.0: the Velocity knob can ask for a strike harder
        // than MIDI 127 (bounded to 1.30 there), and clamping it back to 1.0
        // would make its "wham" direction dead.  This only guards against
        // garbage: exc_gain, knock and noise scale linearly past 1.0 and the
        // pitch glide is still pole-clamped in Process().
        const float v = (velocity < 0.0f) ? 0.0f : (velocity > 2.0f ? 2.0f : velocity);
        Voice& vc = TakeKettle(note, ratio);
        vc.serial = ++m_trigger_serial;
        HandOverBank(vc);
        ArmStrike(vc, *m_recipe, v);
    }

    // Render `frames` mono samples (transparent-limited, at the approved
    // absolute level).  Returns false when both kettles are fully silent.
    bool Process(float* __restrict mono, int frames) {
        if (!IsActive()) return false;
        const ModalDrumRecipe& r = *m_recipe;
        ContinueRebuild();

        bool any_audible = false;
        for (int i = 0; i < frames; ++i) mono[i] = 0.0f;
        const BlockRates k = {
            expf(-r.noise_decay / m_sr),
            expf(-r.noise_sweep_hz / m_sr),
            r.noise_level * m_mods.noise_mult + m_mods.noise_add,
            expf(-kGlideDecay / m_sr),
        };

        for (int vi = 0; vi < kVoices; ++vi) {
            Voice& vc = m_vc[vi];
            if (!vc.tuned || vc.is_silent) continue;
            any_audible = true;
            RetireModes(vc);
            vc.age += (uint32_t)frames;
            GlideRetune(vc);
            const float block_peak = RenderKettle(vc, r, k, mono, frames);
            GateSilence(vc, r, k.noise_level, block_peak);
        }
        if (!any_audible) return false;

        // The transparent peak limiter on the SUM of the kettles: unity below
        // kLimThr, a tanh knee above — preserves the strike's crest ("wham").
        for (int s = 0; s < frames; ++s)
            mono[s] = TanhKnee(mono[s], kLimThr, 1.0f - kLimThr, 1.0f / (1.0f - kLimThr)) * r.out_gain;
        return true;
    }

    // A peak limiter that leaves |x| <= thr alone and bends the excess onto
    // thr + span · tanh((|x| - thr) · inv_span), keeping the sign.
    static float TanhKnee(float x, float thr, float span, float inv_span) {
        float a = fabsf(x);
        if (a > thr) {
            a = thr + span * fastertanhf((a - thr) * inv_span);
            x = (x < 0.0f) ? -a : a;
        }
        return x;
    }

private:
    static constexpr float kPi = 3.14159265358979f;
    static constexpr float kLimThr = 0.85f;
    static constexpr float kExcLenMin = 2.0f;    // samples (velocity-sharpened)
    static constexpr float kExcLenMax = 10.0f;
    static constexpr float kGlideAmount = 0.10f; // strike pitch-glide depth
    static constexpr float kGlideDecay  = 45.0f; // 1/s
    static constexpr float kThetaMax = 0.45f * 2.0f * kPi;   // keep poles < 0.45·sr

    // ── Mode retirement ────────────────────────────────────────────────────
    // A mode whose envelope has decayed below kRetireEps is still stepped
    // every sample, for as long as the kettle lives.  On Timpani that is most
    // of the bank most of the time: the 224-mode "fill" above the measured
    // skeleton has T60 620-1670 ms against the skeleton's ~1.9 s, so by 2 s
    // after a strike only ~70 of 280 modes carry anything at all.  Stepping
    // the other 210 is pure cost — and a note change runs TWO banks at once,
    // which is what puts this preset over the device's CPU budget (measured:
    // 2 kettles = 110 µs/block, against ~50 µs/block known to run on the
    // drumlogue).
    //
    // kRetireEps is ABSOLUTE, against a kettle peak of ~0.95 pre-out_gain, so
    // -100 dB per mode.  kPeakPerAmp bounds a mode's post-strike envelope from
    // its table amp: gain[m] carries a sin(theta) that cancels the resonator's
    // own 1/sin(theta), so the impulse peak is ~amp (see DeriveVoiceRange), and
    // the driving burst is at most 2*40/pi ~ 26 sample-equivalents at up to
    // twice nominal velocity (the Velocity knob's wham ceiling).  Deliberately
    // an OVER-estimate: too large only delays a retirement, it cannot cause an
    // early one.
    static constexpr float kRetireEps    = 1.0e-5f;
    static constexpr float kPeakPerAmp   = 26.0f * 2.0f;
    static constexpr int   kRetireNever  = 1 << 28;

    // ── Aggregate cost bound: only ONE kettle runs a full bank ─────────────
    // Natural retirement above handles a tail, but not the PEAK, and the peak
    // is what the hardware reports: a strike resets a kettle's bound to the
    // full bank, so a note change (which is always simultaneous with a strike)
    // puts two full banks in play at once: 110 µs/block, against ~50 µs/block
    // known to run on the drumlogue and ~96 µs known to overload it, for ~7 s
    // (heard as sporadic clicks for the next 8-10 seconds).
    //
    // So when a new note takes the second kettle, the OLDER kettle keeps its
    // measured skeleton (the low modes that carry the pitch — the same n/5
    // split SelectedModes already calls "always sounds") and its dense upper
    // FILL is damped away.  Steady two-note cost falls from 2 x 280 to 280+56.
    //
    // It is damped rather than dropped because a pole radius scaled down is
    // continuous — no step to click on — and the fade is masked anyway: the
    // event that triggers it IS a full-velocity strike on the other kettle.
    // 6.908/280 = a 25 ms T60 on the fill, cut loose 40 ms later when it is
    // ~115 dB down.
    static constexpr float kFillDampRate  = 280.0f;   // 1/s, added to the fill
    static constexpr float kFillCutSec    = 0.040f;

    struct Voice {
        alignas(16) float a1  [kMaxModes];
        alignas(16) float a2  [kMaxModes];
        alignas(16) float gain[kMaxModes];
        alignas(16) float y1  [kMaxModes];
        alignas(16) float y2  [kMaxModes];
        uint8_t  note = 0;
        bool     tuned = false;       // has a valid tuning (coeffs derived)
        float    ratio = 1.0f;        // transpose ratio for this kettle
        uint32_t serial = 0;          // last-trigger order (steal the oldest)
        // strike state
        float exc_gain = 0.0f;
        int   exc_len = 0, exc_pos = 0;
        int   tr_pos = 1 << 30;
        float tr_gain = 0.0f;
        float noise_env = 0.0f, noise_coef = 0.5f;
        float nz_lp1 = 0.0f, nz_lp2 = 0.0f, nz_lp3 = 0.0f, nz_lp4 = 0.0f;
        uint32_t rng = 2463534242u;
        float glide_state = 0.0f;
        bool  glide_idle = true;
        int   atk_len = 0, atk_pos = 0;
        int   silent_blocks = 0;
        bool  is_silent = true;
        // Mode retirement (see m_grp_retire): how many modes this kettle still
        // steps, and how long since its last strike.  Always a multiple of 4 so
        // the NEON arm keeps its alignment.
        int      live_hi = 0;
        uint32_t age = 0;
        // Fill damping (see kFillDampRate): 1.0 = this kettle owns the full
        // bank; < 1.0 = a newer kettle took over and this one's upper fill is
        // being damped out, to be cut loose at `fill_cut_at`.
        float    fill_damp = 1.0f;
        uint32_t fill_cut_at = 0xFFFFFFFFu;
    };

    // Per-block rates Process() hands to every kettle.
    struct BlockRates {
        float noise_decay;   // noise-wedge envelope, per sample
        float noise_sweep;   // noise-wedge cutoff sweep, per sample
        float noise_level;   // noise-wedge level after NzMix
        float glide_decay;   // strike pitch glide, per sample
    };

    void ClearVoice(Voice& vc) {
        memset(vc.y1, 0, sizeof(vc.y1));
        memset(vc.y2, 0, sizeof(vc.y2));
        vc.exc_gain = 0.0f; vc.exc_len = vc.exc_pos = 0;
        vc.tr_pos = 1 << 30; vc.tr_gain = 0.0f;
        vc.noise_env = 0.0f;
        vc.nz_lp1 = vc.nz_lp2 = vc.nz_lp3 = vc.nz_lp4 = 0.0f;
        vc.rng = 2463534242u;
        vc.glide_state = 0.0f; vc.glide_idle = true;
        vc.atk_len = vc.atk_pos = 0;
        vc.silent_blocks = 0;
        vc.is_silent = true;
        vc.live_hi = 0;
        vc.age = 0;
        vc.fill_damp = 1.0f;
        vc.fill_cut_at = 0xFFFFFFFFu;
    }

    // ── The strike ──────────────────────────────────────────────────────────
    // Same note → the same physical drum: retrigger in place (a roll).  New
    // note → a silent kettle if there is one, else the least recently struck;
    // it is retuned synchronously and its old ring cleared.
    Voice& TakeKettle(uint8_t note, float ratio) {
        for (int i = 0; i < kVoices; ++i)
            if (m_vc[i].tuned && m_vc[i].note == note) return m_vc[i];
        Voice* vc = nullptr;
        for (int i = 0; i < kVoices; ++i)
            if (!m_vc[i].tuned || m_vc[i].is_silent) { vc = &m_vc[i]; break; }
        if (!vc) {
            vc = &m_vc[0];
            for (int i = 1; i < kVoices; ++i)
                if (m_vc[i].serial < vc->serial) vc = &m_vc[i];
        }
        ClearVoice(*vc);
        vc->note = note;
        vc->ratio = ratio;
        vc->tuned = true;
        RetuneVoice(*vc);
        return *vc;
    }

    // The struck kettle owns the full bank from here; any OTHER kettle still
    // ringing gives its dense fill up, so the two banks together cost about
    // one and a bit, not two (see kFillDampRate).
    void HandOverBank(Voice& vc) {
        if (vc.fill_damp != 1.0f) {
            vc.fill_damp = 1.0f;
            vc.fill_cut_at = 0xFFFFFFFFu;
            DeriveVoiceRange(vc, m_skeleton_padded, m_num_modes_padded);
        }
        const float fill_damp = expf(-kFillDampRate / m_sr);
        for (int i = 0; i < kVoices; ++i) {
            Voice& ov = m_vc[i];
            if (&ov == &vc || !ov.tuned || ov.is_silent) continue;
            if (ov.fill_damp == 1.0f) {
                ov.fill_damp = fill_damp;
                ov.fill_cut_at = ov.age + (uint32_t)(kFillCutSec * m_sr);
                // Scale the poles in place rather than re-deriving the range:
                // a1 = 2·r·cos(th) and a2 = r², so damping r by d is exactly
                // a1 *= d, a2 *= d² — no trig, which matters because this runs
                // in NoteOn ALONGSIDE the new kettle's full RetuneVoice.
                // DeriveVoiceRange and the glide fold the same factor in, so a
                // rebuild landing mid-fade stays consistent with this.
                const float d2 = fill_damp * fill_damp;
                for (int m = m_skeleton_padded; m < m_num_modes_padded; ++m) {
                    ov.a1[m] *= fill_damp;
                    ov.a2[m] *= d2;
                }
            }
        }
    }

    void ArmStrike(Voice& vc, const ModalDrumRecipe& r, float v) {
        // Excitation: energy scales with velocity, and the impulse SHARPENS
        // with velocity (velocity changes timbre, not just level).
        vc.exc_gain = v * r.exc_gain;
        const float t = m_mods.vel_sharp * v;
        float len = kExcLenMax - t * (kExcLenMax - kExcLenMin);
        len /= (m_mods.exc_sharp > 0.1f) ? m_mods.exc_sharp : 0.1f;
        vc.exc_len = (int)(len + 0.5f);
        if (vc.exc_len < 1)  vc.exc_len = 1;
        if (vc.exc_len > 40) vc.exc_len = 40;
        vc.exc_pos = 0;

        // Broadband knock: velocity curve shaped by vel_knock_exp (a LOWER
        // exponent keeps the hit prominent at soft velocities).
        vc.tr_pos = 0;
        vc.tr_gain = powf(v, m_mods.vel_knock_exp) * r.knock_gain * m_mods.knock_mult;

        // Noise wedge: envelope and cutoff back to bright, swept down per hit.
        vc.noise_env  = v;
        vc.noise_coef = m_noise_coef_hi_eff;

        // Strike pitch glide (head tension): starts sharp, settles.
        vc.glide_state = v * kGlideAmount;
        vc.glide_idle = false;

        // The raised-cosine attack bloom (membrane-only swell to peak).
        vc.atk_len = (int)(r.bloom_ms * 0.001f * m_sr + 0.5f);
        if (vc.atk_len < 1) vc.atk_len = 1;
        vc.atk_pos = 0;

        vc.silent_blocks = 0;
        vc.is_silent = false;
        // A strike re-excites the whole bank, so every retired group comes back
        // (their y are zero, cleared on retirement, so they restart cleanly).
        vc.live_hi = m_num_modes_padded;
        vc.age = 0;
    }

    // ── Per block ───────────────────────────────────────────────────────────
    // The amortised base rebuild and per-kettle re-derivation, bounded per block.
    void ContinueRebuild() {
        if (m_rebuild_pos >= m_num_modes_padded) return;
        int end = m_rebuild_pos + kRebuildPerBlock;
        if (end > m_num_modes_padded) end = m_num_modes_padded;
        RebuildBase(m_rebuild_pos, end);
        for (int v = 0; v < kVoices; ++v)
            if (m_vc[v].tuned) DeriveVoiceRange(m_vc[v], m_rebuild_pos, end);
        m_rebuild_pos = end;
        // The per-group figures RebuildBase just wrote are per-group MAXIMA;
        // turning them into the suffix maxima the walk needs can only be done
        // once the whole table is current.  Until then no kettle retires
        // anything (m_sched_ready), which is the safe direction.
        if (m_rebuild_pos >= m_num_modes_padded) FinishSchedule();
    }

    // Retire mode groups whose envelopes have decayed past kRetireEps.  The
    // schedule is a suffix maximum, so the bound only ever walks DOWN and each
    // group is visited once per strike.  Retired lanes are zeroed rather than
    // frozen: stale state there would re-enter as a ghost on the next strike,
    // the same trap RebuildBase guards on a density shrink.
    void RetireModes(Voice& vc) {
        if (vc.live_hi > m_num_modes_padded) vc.live_hi = m_num_modes_padded;
        // A damped fill is walked out IN STEP with its damping rather than
        // dropped in one go at the end: dropping it at the end would leave
        // both banks at full width for the whole 40 ms window — the very peak
        // this mechanism exists to remove — and the ramp is free of steps
        // because the groups released first are both the quietest in the table
        // and the furthest into their fade.
        if (vc.fill_damp != 1.0f && vc.live_hi > m_skeleton_padded) {
            const uint32_t span = (uint32_t)(kFillCutSec * m_sr);
            const uint32_t left = (vc.fill_cut_at > vc.age) ? (vc.fill_cut_at - vc.age) : 0u;
            int target = m_skeleton_padded +
                         (int)(((int64_t)(m_num_modes_padded - m_skeleton_padded)
                                * (int64_t)left) / (int64_t)span);
            target &= ~3;
            if (target < m_skeleton_padded) target = m_skeleton_padded;
            if (vc.live_hi > target) {
                memset(&vc.y1[target], 0, (size_t)(vc.live_hi - target) * sizeof(float));
                memset(&vc.y2[target], 0, (size_t)(vc.live_hi - target) * sizeof(float));
                vc.live_hi = target;
            }
        }
        if (m_sched_ready) {
            while (vc.live_hi > 0 && m_grp_retire[(vc.live_hi >> 2) - 1] <= (int)vc.age) {
                vc.live_hi -= 4;
                memset(&vc.y1[vc.live_hi], 0, 4 * sizeof(float));
                memset(&vc.y2[vc.live_hi], 0, 4 * sizeof(float));
            }
        }
    }

    // The strike's pitch glide, retuned once per block (trig out of the inner
    // loop).  Once settled, snap to exact tuning and stop.
    void GlideRetune(Voice& vc) {
        if (vc.glide_idle) return;
        const float gm = vc.ratio * (1.0f + vc.glide_state);
        for (int m = 0; m < m_num_modes_padded; ++m) {
            const float th = m_base_theta[m] * gm;
            const float rr = (m >= m_skeleton_padded) ? (m_base_r[m] * vc.fill_damp) : m_base_r[m];
            vc.a1[m] = (th < kThetaMax) ? 2.0f * rr * cosf(th) : 0.0f;
        }
        if (vc.glide_state < 1e-5f) vc.glide_idle = true;
    }

    // One kettle for one block, added into mono[]; returns its peak.
    float RenderKettle(Voice& vc, const ModalDrumRecipe& r, const BlockRates& k,
                       float* __restrict mono, int frames) {
        float block_peak = 0.0f;
        for (int s = 0; s < frames; ++s) {
            float sample = RingBank(vc, Excitation(vc));
            if (k.noise_level > 0.0f && vc.noise_env > 1e-5f) sample += NoiseWedge(vc, k);
            // The attack bloom: the MEMBRANE swells to peak over a few ms; the
            // stick click (the knock below) stays sharp.
            if (vc.atk_pos < vc.atk_len) sample *= Bloom(vc, r);
            if (vc.tr_pos < r.transient_len) sample += Knock(vc, r);
            mono[s] += sample;
            const float a = fabsf(sample);
            if (a > block_peak) block_peak = a;
            vc.glide_state *= k.glide_decay;
        }
        return block_peak;
    }

    // Per-kettle silence gate: the strike machinery done and the ring
    // inaudible for a stretch → stop running this kettle's bank.
    static void GateSilence(Voice& vc, const ModalDrumRecipe& r, float noise_level, float block_peak) {
        const bool machinery_done = (vc.exc_pos >= vc.exc_len) &&
                                    (vc.tr_pos >= r.transient_len) &&
                                    (vc.noise_env <= 1e-5f || noise_level <= 0.0f);
        if (machinery_done && block_peak < 3e-5f) {
            if (++vc.silent_blocks >= 8) vc.is_silent = true;
        } else {
            vc.silent_blocks = 0;
        }
    }

    // ── Per sample ──────────────────────────────────────────────────────────
    // The excitation impulse: a short half-sine burst.
    static float Excitation(Voice& vc) {
        float e = 0.0f;
        if (vc.exc_pos < vc.exc_len) {
            e = sinf(kPi * (float)vc.exc_pos / (float)vc.exc_len) * vc.exc_gain;
            vc.exc_pos++;
        }
        return e;
    }

    // The resonator bank, y0 = a1·y1 - a2·y2 + g·e, 4 modes per NEON
    // iteration; padding lanes contribute exactly 0.
    //
    // Two arms, split on whether the strike burst is still running.  exc_len
    // is at most 40 samples, so for >99 % of a ring `e` is exactly 0.0 and
    // `gain[m] * e` contributes nothing — yet the driven arm still loads the
    // whole gain array (280 floats on Timpani) and issues a multiply-add per
    // mode, every sample.  Dropping it in the tail is bit-exact (x + g·0 == x)
    // and removes a fifth of the loop's memory traffic; it matters because
    // this bank is the unit's dominant per-sample cost and a note change runs
    // TWO of them.
    static float RingBank(Voice& vc, float e) {
        float body = 0.0f;
        const int nm = vc.live_hi;
#if defined(MODAL_DRUM_NEON)
        float32x4_t vbody = vdupq_n_f32(0.0f);
        if (e != 0.0f) {
            const float32x4_t ve = vdupq_n_f32(e);
            for (int m = 0; m < nm; m += 4) {
                float32x4_t a1 = vld1q_f32(&vc.a1[m]);
                float32x4_t a2 = vld1q_f32(&vc.a2[m]);
                float32x4_t y1 = vld1q_f32(&vc.y1[m]);
                float32x4_t y2 = vld1q_f32(&vc.y2[m]);
                float32x4_t g  = vld1q_f32(&vc.gain[m]);
                float32x4_t y0 = vmulq_f32(g, ve);
                y0 = vmlaq_f32(y0, a1, y1);
                y0 = vmlsq_f32(y0, a2, y2);
                vst1q_f32(&vc.y2[m], y1);
                vst1q_f32(&vc.y1[m], y0);
                vbody = vaddq_f32(vbody, y0);
            }
        } else {
            for (int m = 0; m < nm; m += 4) {
                float32x4_t a1 = vld1q_f32(&vc.a1[m]);
                float32x4_t a2 = vld1q_f32(&vc.a2[m]);
                float32x4_t y1 = vld1q_f32(&vc.y1[m]);
                float32x4_t y2 = vld1q_f32(&vc.y2[m]);
                float32x4_t y0 = vmulq_f32(a1, y1);
                y0 = vmlsq_f32(y0, a2, y2);
                vst1q_f32(&vc.y2[m], y1);
                vst1q_f32(&vc.y1[m], y0);
                vbody = vaddq_f32(vbody, y0);
            }
        }
        float32x2_t s2 = vadd_f32(vget_low_f32(vbody), vget_high_f32(vbody));
        s2 = vpadd_f32(s2, s2);
        body = vget_lane_f32(s2, 0);
#else
        if (e != 0.0f) {
            for (int m = 0; m < nm; ++m) {
                float y0 = vc.a1[m] * vc.y1[m] - vc.a2[m] * vc.y2[m] + vc.gain[m] * e;
                vc.y2[m] = vc.y1[m];
                vc.y1[m] = y0;
                body += y0;
            }
        } else {
            for (int m = 0; m < nm; ++m) {
                float y0 = vc.a1[m] * vc.y1[m] - vc.a2[m] * vc.y2[m];
                vc.y2[m] = vc.y1[m];
                vc.y1[m] = y0;
                body += y0;
            }
        }
#endif
        return body;
    }

    // The noise wedge: broadband membrane response per kettle, its cutoff
    // sweeping down (4 cascaded one-poles = -24 dB/oct).
    float NoiseWedge(Voice& vc, const BlockRates& k) const {
        vc.rng ^= vc.rng << 13; vc.rng ^= vc.rng >> 17; vc.rng ^= vc.rng << 5;
        const float wn = (float)(int32_t)vc.rng * (1.0f / 2147483647.0f);
        vc.nz_lp1 += vc.noise_coef * (wn        - vc.nz_lp1);
        vc.nz_lp2 += vc.noise_coef * (vc.nz_lp1 - vc.nz_lp2);
        vc.nz_lp3 += vc.noise_coef * (vc.nz_lp2 - vc.nz_lp3);
        vc.nz_lp4 += vc.noise_coef * (vc.nz_lp3 - vc.nz_lp4);
        const float out = vc.nz_lp4 * vc.noise_env * k.noise_level;
        vc.noise_env *= k.noise_decay;
        vc.noise_coef = m_noise_coef_lo_eff + (vc.noise_coef - m_noise_coef_lo_eff) * k.noise_sweep;
        return out;
    }

    // The raised-cosine attack bloom's gain for this sample.
    static float Bloom(Voice& vc, const ModalDrumRecipe& r) {
        const float ph = (float)vc.atk_pos / (float)vc.atk_len;
        const float b = 0.5f - 0.5f * cosf(kPi * ph);
        vc.atk_pos++;
        return r.bloom_floor + (1.0f - r.bloom_floor) * b;
    }

    // The broadband residual "knock" (recorded, exp-tapered).
    static float Knock(Voice& vc, const ModalDrumRecipe& r) {
        return r.transient[vc.tr_pos++] * vc.tr_gain;
    }

    // ── The shared base tables ──────────────────────────────────────────────
    // Mode selection under the Partls density mod: the low/measured skeleton
    // (first fifth of the table, min 8) always sounds; the fill/upper modes
    // scale with density — 1.0 = the full approved wedge.
    int SelectedModes() const {
        if (!m_recipe) return 0;
        int n = m_recipe->num_modes;
        int n0 = n / 5; if (n0 < 8) n0 = 8; if (n0 > n) n0 = n;
        float d = m_mods.density;
        if (d < 0.0f) d = 0.0f;
        if (d > 1.0f) d = 1.0f;
        int count = n0 + (int)((float)(n - n0) * d + 0.5f);
        if (count > kMaxModes) count = kMaxModes;
        return count;
    }

    // Shared base tables: theta at ratio 1 (Inharm stretch baked — the
    // stretch is ratio-invariant), decay pole radius (Dkay/Rel/Mterl baked —
    // pitch-invariant by design so the NOTE path never needs expf), and amp.
    void RebuildBase(int begin, int end) {
        // A density change can shrink/grow the padded count; recompute bounds
        // when the rebuild starts from 0.  Lanes dropped by a shrink stop
        // being stepped, so zero every voice's y there or they re-enter as a
        // stale ghost when the density comes back up.
        if (begin == 0) {
            int old_padded = m_num_modes_padded;
            m_num_modes = SelectedModes();
            m_num_modes_padded = (m_num_modes + 3) & ~3;
            if (m_num_modes_padded > kMaxModes) m_num_modes_padded = kMaxModes;
            // The measured skeleton: the same n/5 (min 8) split SelectedModes
            // uses for "always sounds", padded to a NEON group.  Everything
            // above it is the dense fill, which is what fill damping removes.
            {
                int n0 = m_recipe->num_modes / 5;
                if (n0 < 8) n0 = 8;
                if (n0 > m_num_modes) n0 = m_num_modes;
                m_skeleton_padded = (n0 + 3) & ~3;
                if (m_skeleton_padded > m_num_modes_padded)
                    m_skeleton_padded = m_num_modes_padded;
            }
            for (int m = m_num_modes_padded; m < old_padded; ++m)
                for (int v = 0; v < kVoices; ++v) {
                    m_vc[v].y1[m] = 0.0f;
                    m_vc[v].y2[m] = 0.0f;
                }
        }
        if (end > kMaxModes) end = kMaxModes;
        const ModalDrumRecipe& r = *m_recipe;
        const float f0 = r.modes[0].freq;
        for (int m = begin; m < end; ++m) {
            if (m >= m_num_modes) {   // padding lanes: contribute exactly 0
                m_base_theta[m] = 0.0f;
                m_base_r[m] = 0.0f;
                m_base_amp[m] = 0.0f;
                const int gp = m >> 2;
                if ((m & 3) == 0) m_grp_retire[gp] = 0;
                continue;
            }
            float freq  = r.modes[m].freq;
            float amp   = r.modes[m].amp;
            float decay = r.modes[m].decay * r.decay_scale;

            // Inharm stretch: spread modes away from the fundamental.
            if (m_mods.stretch != 0.0f && freq > f0) {
                freq *= exp2f(m_mods.stretch * log2f(freq / f0) * 0.25f);
            }
            // Mterl tilt: brighter material rings its HF longer (tilt > 0
            // divides HF decay), darker chokes it.
            if (m_mods.hf_decay_tilt != 0.0f && freq > 700.0f) {
                float oct = log2f(freq * (1.0f / 700.0f));
                decay *= exp2f(-m_mods.hf_decay_tilt * oct);
            }
            // Dkay/Rel: T60 multiplier = decay-rate divisor.
            decay /= (m_mods.decay_mult > 0.05f) ? m_mods.decay_mult : 0.05f;

            float rr = expf(-decay / m_sr);
            if (rr > 0.99999f) rr = 0.99999f;
            m_base_theta[m] = 2.0f * kPi * freq / m_sr;
            m_base_r[m]     = rr;
            m_base_amp[m]   = amp;

            // ── Mode retirement schedule (see the m_grp_retire comment) ─────
            // Sample count after which this mode's envelope is below kRetireEps
            // however hard it was struck: P·r^n < eps  →  n = ln(eps/P)/ln(r).
            // Neither r nor amp depends on the kettle's transpose ratio (that
            // is the point of the base tables), so one schedule serves both.
            const float peak = amp * kPeakPerAmp * m_recipe->exc_gain;
            int n_die;
            if (peak <= kRetireEps) {
                n_die = 0;                       // never audible in the first place
            } else if (rr <= 0.0f || rr >= 1.0f) {
                n_die = kRetireNever;
            } else {
                float n = logf(kRetireEps / peak) / logf(rr);
                n_die = (n >= (float)kRetireNever) ? kRetireNever : (int)(n + 1.0f);
            }
            const int g = m >> 2;
            if ((m & 3) == 0 || n_die > m_grp_retire[g]) m_grp_retire[g] = n_die;
        }
        // Noise-wedge cutoffs (cheap; refresh alongside any rebuild).
        ApplyNoiseBright();
        m_noise_coef_lo_eff = r.noise_coef_lo;
    }

    void ApplyNoiseBright() {
        m_noise_coef_hi_eff = m_recipe->noise_coef_hi * m_mods.noise_bright;
        if (m_noise_coef_hi_eff > kNoiseCoefMax) m_noise_coef_hi_eff = kNoiseCoefMax;
        if (m_noise_coef_hi_eff < kNoiseCoefMin) m_noise_coef_hi_eff = kNoiseCoefMin;
    }

    // Derive one kettle's coefficients from the base tables at its transpose
    // ratio.  sin/cos only (~30 µs for 280 modes on the A7) — cheap enough to
    // run synchronously inside NoteOn, so a new note strikes in tune at once.
    void DeriveVoiceRange(Voice& vc, int begin, int end) {
        for (int m = begin; m < end; ++m) {
            float th = m_base_theta[m] * vc.ratio;
            // Fill damping is folded in HERE, not applied as a one-shot to the
            // coefficient arrays, so it survives an amortised rebuild landing
            // mid-fade (which would otherwise restore the undamped poles).
            float rr = (m >= m_skeleton_padded) ? (m_base_r[m] * vc.fill_damp)
                                                : m_base_r[m];
            if (th <= 0.0f || th >= kThetaMax) {   // padding or off the top
                vc.a1[m] = vc.a2[m] = vc.gain[m] = 0.0f;
                vc.y1[m] = vc.y2[m] = 0.0f;
                continue;
            }
            vc.a1[m]   = 2.0f * rr * cosf(th);
            vc.a2[m]   = rr * rr;
            vc.gain[m] = m_base_amp[m] * sinf(th);  // impulse peak ≈ amp
        }
    }

    void RetuneVoice(Voice& vc) { DeriveVoiceRange(vc, 0, m_num_modes_padded); }

    // Turn RebuildBase's per-group maxima into the suffix maxima the per-block
    // walk needs, and arm it.  Only valid once the whole table is current, so
    // it is called from the two places that complete a rebuild: Configure (all
    // at once) and ContinueRebuild (the amortised path reaching the end).
    void FinishSchedule() {
        int run = 0;
        for (int g = (m_num_modes_padded >> 2) - 1; g >= 0; --g) {
            if (m_grp_retire[g] > run) run = m_grp_retire[g];
            m_grp_retire[g] = run;
        }
        m_sched_ready = true;
    }

    // The sample rate, deliberately a variable and not a constant: under the
    // unit's -ffast-math a division by a CONSTANT becomes a multiplication by
    // its reciprocal, which moves every mode's pole by a rounding step — and a
    // rounding step on a 280-mode bank ringing for seconds is a measurable
    // phase drift (-80 dB against the original on ARM, instead of -150).
    float m_sr = brc::kSampleRate;
    const ModalDrumRecipe* m_recipe = nullptr;
    bool  m_active = false;
    float m_ref_drive = 1.0f;
    Mods  m_mods;

    int m_num_modes = 0;
    int m_num_modes_padded = 0;
    int m_rebuild_pos = 0;
    uint32_t m_trigger_serial = 0;

    alignas(16) float m_base_theta[kMaxModes];
    alignas(16) float m_base_r    [kMaxModes];
    alignas(16) float m_base_amp  [kMaxModes];

    // Retirement schedule, in NEON groups of 4 modes.  m_grp_retire[g] is the
    // age (in samples since a strike) past which EVERY mode from group g
    // upward is below kRetireEps — a suffix maximum, so it is non-increasing
    // in g and a kettle can walk its bound down monotonically.  Shared by both
    // kettles: r and amp are pitch-invariant by design, only the age differs.
    int m_grp_retire[kMaxModes / 4];
    bool m_sched_ready = false;
    int m_skeleton_padded = kMaxModes;

    float m_noise_coef_hi_eff = 0.5f, m_noise_coef_lo_eff = 0.008f;

    Voice m_vc[kVoices];
};
