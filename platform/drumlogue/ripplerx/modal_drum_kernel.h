#pragma once
/**
 * modal_drum_kernel.h — dense coupled-resonator drum kernel (Timpani / Taiko).
 *
 * Port of the standalone ResonatorDrumSynth that produced the approved
 * references 105_timp_wedge.wav / 110_taiko_wedge.wav (PROGRESS2.md):
 * one velocity-scaled half-sine excitation impulse rings a bank of up to 280
 * two-pole resonators (the drum's measured modes + a dense jittered membrane
 * fill), a recorded broadband residual transient supplies the stick "knock",
 * a raised-cosine attack bloom gives the few-ms swell to peak, and a
 * sweeping-cutoff noise wedge fills the taiko's broadband decay.  The output
 * stage is a transparent peak limiter (unity below 0.85) — NOT the master
 * soft-clip — so the strike keeps its crest ("wham").
 *
 * TWO KETTLES (HW feedback round 3): the kernel holds kVoices=2 independent
 * drums.  A hit on the note a voice is already tuned to retriggers that voice
 * in place — its resonator states are NOT zeroed, so energy accumulates like
 * a real drum roll.  A hit on a DIFFERENT note takes a free/oldest voice and
 * retunes it synchronously (sin/cos only — the decay poles are pitch-
 * invariant, so no expf in the note path), leaving the other kettle's ring
 * untouched.  This is what makes note changes sound like a second drum
 * instead of bending (distorting) the ringing tail.
 *
 * RT-safe: no heap, fixed structure-of-arrays state (16-byte aligned), NEON
 * inner loop with a bit-equivalent scalar fallback.  Coefficient work that
 * moves decay poles (Dkay / Rel / Mterl / Inharm / Partls) is amortized:
 * SetMods() marks the shared base tables dirty and Process() rebuilds a
 * bounded number of modes per block, so knob turns never blow the deadline.
 */

#include <stdint.h>
#include <cmath>
#include <cstring>

#include "modal_drum_data.h"

#if defined(__ARM_NEON) || defined(__ARM_NEON__)
#include <arm_neon.h>
#define MODAL_DRUM_NEON 1
#endif

class ModalDrumKernel {
public:
    static const int kMaxModes     = 280;  // timp_wedge table (multiple of 4)
    static const int kVoices       = 2;    // two kettles
    static const int kRebuildPerBlock = 48; // modes re-based per Process() call

    // Live sound-design modifiers, all anchored so that zero deltas reproduce
    // the recipe (= the approved render) exactly.  Pitch is NOT a mod: the
    // note travels with each Trigger() and retunes one voice synchronously.
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

    void Init(float sample_rate) {
        m_sr = (sample_rate > 8000.0f) ? sample_rate : 48000.0f;
        m_recipe = nullptr;
        m_active = false;
        for (int v = 0; v < kVoices; ++v) ClearVoice(m_vc[v]);
    }

    // Bind a recipe (preset load).  ref_drive is the master_drive the shipped
    // preset row produces — the kernel master path divides it back out so the
    // shipped Gain value plays the approved render transparently.
    void Configure(const ModalDrumRecipe* recipe, float ref_drive) {
        m_recipe = recipe;
        m_ref_drive = (ref_drive > 0.05f) ? ref_drive : 1.0f;
        m_mods = Mods();
        RebuildBase(0, kMaxModes);
        m_rebuild_pos = m_num_modes_padded;
        for (int v = 0; v < kVoices; ++v) {
            ClearVoice(m_vc[v]);
            m_vc[v].tuned = false;
        }
        m_trigger_serial = 0;
        m_active = true;
    }

    void Deactivate() { m_active = false; }
    bool IsActive() const { return m_active && m_recipe; }
    float RefDrive() const { return m_ref_drive; }
    float RootNote() const { return m_recipe ? m_recipe->root_note : 60.0f; }
    void Flush() {  // hard stop (engine Reset): kill both kettles' rings
        for (int v = 0; v < kVoices; ++v) ClearVoice(m_vc[v]);
    }

    // Mods that move the base tables (decay poles, stretch, density) restart
    // the amortized rebuild; trigger-time mods take effect on the next hit.
    void SetMods(const Mods& m) {
        bool rebuild = (m.decay_mult != m_mods.decay_mult) ||
                       (m.hf_decay_tilt != m_mods.hf_decay_tilt) ||
                       (m.stretch    != m_mods.stretch)    ||
                       (m.density    != m_mods.density);
        m_mods = m;
        if (rebuild && m_recipe) m_rebuild_pos = 0;
    }

    // Strike.  `note` selects/retunes a kettle; `ratio` is the engine-computed
    // transpose (2^((note-root)/12)); velocity ∈ [0,1].
    void Trigger(uint8_t note, float ratio, float velocity) {
        if (!IsActive()) return;
        const ModalDrumRecipe& r = *m_recipe;
        float v = (velocity < 0.0f) ? 0.0f : (velocity > 1.0f ? 1.0f : velocity);

        // ── kettle selection ────────────────────────────────────────────
        // Same note → the same physical drum: retrigger in place (roll).
        // New note → prefer a silent kettle, else steal the least-recently
        // struck one; retune it synchronously and clear its old ring.
        Voice* vc = nullptr;
        for (int i = 0; i < kVoices; ++i)
            if (m_vc[i].tuned && m_vc[i].note == note) { vc = &m_vc[i]; break; }
        if (!vc) {
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
        }
        vc->serial = ++m_trigger_serial;

        // Excitation: energy scales with velocity, and the impulse SHARPENS
        // with velocity (velocity changes timbre, not just level).
        vc->exc_gain = v * r.exc_gain;
        float t = m_mods.vel_sharp * v;
        float len = kExcLenMax - t * (kExcLenMax - kExcLenMin);
        len /= (m_mods.exc_sharp > 0.1f) ? m_mods.exc_sharp : 0.1f;
        vc->exc_len = (int)(len + 0.5f);
        if (vc->exc_len < 1)  vc->exc_len = 1;
        if (vc->exc_len > 40) vc->exc_len = 40;
        vc->exc_pos = 0;

        // Broadband knock: velocity curve shaped by vel_knock_exp (a LOWER
        // exponent keeps the hit prominent at soft velocities).
        vc->tr_pos = 0;
        vc->tr_gain = powf(v, m_mods.vel_knock_exp) * r.knock_gain * m_mods.knock_mult;

        // Noise wedge: reset envelope + cutoff to bright, sweeps down per hit.
        vc->noise_env  = v;
        vc->noise_coef = m_noise_coef_hi_eff;

        // Strike pitch glide (head tension): starts sharp, settles.
        vc->glide_state = v * kGlideAmount;
        vc->glide_idle = false;

        // Arm the raised-cosine attack bloom (membrane-only swell to peak).
        vc->atk_len = (int)(r.bloom_ms * 0.001f * m_sr + 0.5f);
        if (vc->atk_len < 1) vc->atk_len = 1;
        vc->atk_pos = 0;

        vc->silent_blocks = 0;
        vc->is_silent = false;
    }

    // Render `frames` mono samples (transparent-limited, at the approved
    // absolute level).  Returns false when both kettles are fully silent.
    bool Process(float* __restrict mono, int frames) {
        if (!IsActive()) return false;
        const ModalDrumRecipe& r = *m_recipe;

        // Amortized base rebuild + per-voice re-derivation, bounded per block.
        if (m_rebuild_pos < m_num_modes_padded) {
            int end = m_rebuild_pos + kRebuildPerBlock;
            if (end > m_num_modes_padded) end = m_num_modes_padded;
            RebuildBase(m_rebuild_pos, end);
            for (int v = 0; v < kVoices; ++v)
                if (m_vc[v].tuned) DeriveVoiceRange(m_vc[v], m_rebuild_pos, end);
            m_rebuild_pos = end;
        }

        bool any_audible = false;
        for (int i = 0; i < frames; ++i) mono[i] = 0.0f;

        float noise_decay_fac = expf(-r.noise_decay / m_sr);
        float noise_sweep_fac = expf(-r.noise_sweep_hz / m_sr);
        float noise_level = r.noise_level * m_mods.noise_mult + m_mods.noise_add;
        float glide_decay = expf(-kGlideDecay / m_sr);

        for (int vi = 0; vi < kVoices; ++vi) {
            Voice& vc = m_vc[vi];
            if (!vc.tuned || vc.is_silent) continue;
            any_audible = true;

            // Per-block pitch-glide retune of THIS kettle (trig out of the
            // inner loop).  Once settled, snap to exact tuning and stop.
            if (!vc.glide_idle) {
                float gm = vc.ratio * (1.0f + vc.glide_state);
                const float th_max = 0.45f * 2.0f * kPi;   // keep poles < 0.45·sr
                for (int m = 0; m < m_num_modes_padded; ++m) {
                    float th = m_base_theta[m] * gm;
                    vc.a1[m] = (th < th_max) ? 2.0f * m_base_r[m] * cosf(th) : 0.0f;
                }
                if (vc.glide_state < 1e-5f) vc.glide_idle = true;
            }

            float block_peak = 0.0f;
            for (int s = 0; s < frames; ++s) {
                // --- excitation impulse (short half-sine burst) ---
                float e = 0.0f;
                if (vc.exc_pos < vc.exc_len) {
                    e = sinf(kPi * (float)vc.exc_pos / (float)vc.exc_len) * vc.exc_gain;
                    vc.exc_pos++;
                }

                // --- resonator bank: y0 = a1*y1 - a2*y2 + g*e, 4 modes per
                //     NEON iteration; padding lanes contribute exactly 0.
                float body = 0.0f;
#if defined(MODAL_DRUM_NEON)
                {
                    const float32x4_t ve = vdupq_n_f32(e);
                    float32x4_t vbody = vdupq_n_f32(0.0f);
                    for (int m = 0; m < m_num_modes_padded; m += 4) {
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
                    float32x2_t s2 = vadd_f32(vget_low_f32(vbody), vget_high_f32(vbody));
                    s2 = vpadd_f32(s2, s2);
                    body = vget_lane_f32(s2, 0);
                }
#else
                for (int m = 0; m < m_num_modes_padded; ++m) {
                    float y0 = vc.a1[m] * vc.y1[m] - vc.a2[m] * vc.y2[m] + vc.gain[m] * e;
                    vc.y2[m] = vc.y1[m];
                    vc.y1[m] = y0;
                    body += y0;
                }
#endif
                float sample = body;

                // --- noise wedge: broadband membrane response per kettle,
                //     cutoff sweeps down (4 cascaded 1-poles = -24 dB/oct).
                if (noise_level > 0.0f && vc.noise_env > 1e-5f) {
                    vc.rng ^= vc.rng << 13; vc.rng ^= vc.rng >> 17; vc.rng ^= vc.rng << 5;
                    float wn = (float)(int32_t)vc.rng * (1.0f / 2147483647.0f);
                    vc.nz_lp1 += vc.noise_coef * (wn       - vc.nz_lp1);
                    vc.nz_lp2 += vc.noise_coef * (vc.nz_lp1 - vc.nz_lp2);
                    vc.nz_lp3 += vc.noise_coef * (vc.nz_lp2 - vc.nz_lp3);
                    vc.nz_lp4 += vc.noise_coef * (vc.nz_lp3 - vc.nz_lp4);
                    sample += vc.nz_lp4 * vc.noise_env * noise_level;
                    vc.noise_env *= noise_decay_fac;
                    vc.noise_coef = m_noise_coef_lo_eff +
                                    (vc.noise_coef - m_noise_coef_lo_eff) * noise_sweep_fac;
                }

                // --- attack bloom: the MEMBRANE swells to peak over a few ms;
                //     the stick click (transient below) stays sharp.
                if (vc.atk_pos < vc.atk_len) {
                    float ph = (float)vc.atk_pos / (float)vc.atk_len;
                    float b = 0.5f - 0.5f * cosf(kPi * ph);
                    sample *= r.bloom_floor + (1.0f - r.bloom_floor) * b;
                    vc.atk_pos++;
                }

                // --- broadband residual "knock" (recorded, exp-tapered) ---
                if (vc.tr_pos < r.transient_len) {
                    sample += r.transient[vc.tr_pos] * vc.tr_gain;
                    vc.tr_pos++;
                }

                mono[s] += sample;
                float a = fabsf(sample);
                if (a > block_peak) block_peak = a;

                vc.glide_state *= glide_decay;
            }

            // Per-kettle silence gate: strike machinery done + inaudible ring
            // for a stretch → stop burning this voice's NEON loop.
            bool machinery_done = (vc.exc_pos >= vc.exc_len) &&
                                  (vc.tr_pos >= r.transient_len) &&
                                  (vc.noise_env <= 1e-5f || noise_level <= 0.0f);
            if (machinery_done && block_peak < 3e-5f) {
                if (++vc.silent_blocks >= 8) vc.is_silent = true;
            } else {
                vc.silent_blocks = 0;
            }
        }

        if (!any_audible) return false;

        // --- transparent peak limiter on the SUM of the kettles: unity below
        //     0.85, tanh knee above — preserves the strike's crest ("wham").
        for (int s = 0; s < frames; ++s) {
            float x = mono[s];
            float a = fabsf(x);
            if (a > kLimThr) {
                float over = (a - kLimThr) * (1.0f / (1.0f - kLimThr));
                a = kLimThr + (1.0f - kLimThr) * tanhf(over);
                x = (x < 0.0f) ? -a : a;
            }
            mono[s] = x * r.out_gain;
        }
        return true;
    }

private:
    static constexpr float kPi = 3.14159265358979f;
    static constexpr float kLimThr = 0.85f;
    static constexpr float kExcLenMin = 2.0f;   // samples (velocity-sharpened)
    static constexpr float kExcLenMax = 10.0f;
    static constexpr float kGlideAmount = 0.10f; // strike pitch-glide depth
    static constexpr float kGlideDecay  = 45.0f; // 1/s

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
    }

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
        }
        // Noise-wedge cutoffs (cheap; refresh alongside any rebuild).
        m_noise_coef_hi_eff = r.noise_coef_hi * m_mods.noise_bright;
        if (m_noise_coef_hi_eff > 0.90f) m_noise_coef_hi_eff = 0.90f;
        if (m_noise_coef_hi_eff < 0.02f) m_noise_coef_hi_eff = 0.02f;
        m_noise_coef_lo_eff = r.noise_coef_lo;
    }

    // Derive one kettle's coefficients from the base tables at its transpose
    // ratio.  sin/cos only (~30 µs for 280 modes on the A7) — cheap enough to
    // run synchronously inside NoteOn, so a new note strikes in tune at once.
    void DeriveVoiceRange(Voice& vc, int begin, int end) {
        const float th_max = 0.45f * 2.0f * kPi;   // keep poles < 0.45·sr
        for (int m = begin; m < end; ++m) {
            float th = m_base_theta[m] * vc.ratio;
            float rr = m_base_r[m];
            if (th <= 0.0f || th >= th_max) {   // padding or off the top
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

    float m_sr = 48000.0f;
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

    float m_noise_coef_hi_eff = 0.5f, m_noise_coef_lo_eff = 0.008f;

    Voice m_vc[kVoices];
};
