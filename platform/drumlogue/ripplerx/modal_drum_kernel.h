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
 * RT-safe: no heap, fixed structure-of-arrays state (16-byte aligned), NEON
 * inner loop with a bit-equivalent scalar fallback.  Mono, single instance:
 * a drum membrane is one physical object — retriggering pumps more energy
 * into the SAME resonators (states are not zeroed), like a real drum roll.
 *
 * Coefficient rebuilds (note / Inharm / Partls / Mterl / Dkay / Rel changes)
 * are amortized: SetDirty() marks the bank and Process() rebuilds a bounded
 * number of modes per block, so knob turns never blow the audio deadline.
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
    static const int kMaxTransient = 3400; // 70 ms @ 48 kHz
    static const int kRebuildPerBlock = 48; // modes re-tuned per Process() call

    // Live sound-design modifiers, all anchored so that "1.0 / 0.0 deltas"
    // reproduce the recipe (= the approved render) exactly.
    struct Mods {
        float transpose      = 1.0f; // Note: frequency ratio vs recipe root
        float decay_mult     = 1.0f; // Dkay/Rel: T60 multiplier (>1 = longer)
        float hf_decay_tilt  = 0.0f; // Mterl: +bright material rings HF longer
        float stretch        = 0.0f; // Inharm: spreads upper modes
        float density        = 1.0f; // Partls: fraction of the fill retained
        float exc_sharp      = 1.0f; // MlltStif: impulse-length divisor
        float knock_mult     = 1.0f; // MlltRes (× HitPos tilt): residual gain
        float vel_sharp      = 0.6f; // VlMllStf: velocity→sharpness amount
        float vel_knock_exp  = 1.5f; // VlMllRes: knock velocity exponent
        float noise_mult     = 1.0f; // NzMix: noise-wedge level multiplier
        float noise_add      = 0.0f; // NzMix above anchor when recipe level is 0
        float noise_bright   = 1.0f; // NzFltFrq: wedge start-cutoff multiplier
    };

    void Init(float sample_rate) {
        m_sr = (sample_rate > 8000.0f) ? sample_rate : 48000.0f;
        m_recipe = nullptr;
        ClearRing();
        m_active = false;
    }

    // Bind a recipe (preset load).  ref_drive is the master_drive the shipped
    // preset row produces — the kernel master path divides it back out so the
    // shipped Gain value plays the approved render transparently.
    void Configure(const ModalDrumRecipe* recipe, float ref_drive) {
        m_recipe = recipe;
        m_ref_drive = (ref_drive > 0.05f) ? ref_drive : 1.0f;
        m_mods = Mods();
        ClearRing();
        RebuildAll();
        m_active = true;
    }

    void Deactivate() { m_active = false; }
    bool IsActive() const { return m_active && m_recipe; }
    float RefDrive() const { return m_ref_drive; }
    float RootNote() const { return m_recipe ? m_recipe->root_note : 60.0f; }
    void Flush() { ClearRing(); }  // hard stop (engine Reset): kill the ring

    // Mods changes that move mode frequencies/decays require a rebuild; it is
    // amortized inside Process().  Trigger-time mods take effect on next hit.
    void SetMods(const Mods& m) {
        bool rebuild = (m.transpose  != m_mods.transpose)  ||
                       (m.decay_mult != m_mods.decay_mult) ||
                       (m.hf_decay_tilt != m_mods.hf_decay_tilt) ||
                       (m.stretch    != m_mods.stretch)    ||
                       (m.density    != m_mods.density);
        m_mods = m;
        if (rebuild && m_recipe) m_rebuild_pos = 0;  // restart amortized rebuild
    }

    void Trigger(float velocity) {
        if (!IsActive()) return;
        const ModalDrumRecipe& r = *m_recipe;
        float v = (velocity < 0.0f) ? 0.0f : (velocity > 1.0f ? 1.0f : velocity);

        // Excitation: energy scales with velocity, and the impulse SHARPENS
        // with velocity (velocity changes timbre, not just level).
        m_exc_gain = v * r.exc_gain;
        float t = m_mods.vel_sharp * v;
        float len = kExcLenMax - t * (kExcLenMax - kExcLenMin);
        len /= (m_mods.exc_sharp > 0.1f) ? m_mods.exc_sharp : 0.1f;
        m_exc_len = (int)(len + 0.5f);
        if (m_exc_len < 1) m_exc_len = 1;
        if (m_exc_len > 40) m_exc_len = 40;
        m_exc_pos = 0;

        // Broadband knock scales super-linearly (punch grows faster than level).
        m_tr_pos = 0;
        float vexp = powf(v, m_mods.vel_knock_exp);
        m_tr_gain = vexp * r.knock_gain * m_mods.knock_mult;

        // Noise wedge: reset envelope + cutoff to bright, sweeps down per hit.
        m_noise_env  = v;
        m_noise_coef = m_noise_coef_hi_eff;

        // Strike pitch glide (head tension): starts sharp, settles.
        m_glide_state = v * kGlideAmount;
        m_glide_idle = false;

        // Arm the raised-cosine attack bloom (membrane-only swell to peak).
        m_atk_len = (int)(r.bloom_ms * 0.001f * m_sr + 0.5f);
        if (m_atk_len < 1) m_atk_len = 1;
        m_atk_pos = 0;

        m_silent_blocks = 0;
        m_is_silent = false;
    }

    // Render `frames` mono samples of the drum (transparent-limited, at the
    // approved absolute level).  Returns false if the kernel is fully silent
    // (caller may skip its master stage).
    bool Process(float* __restrict mono, int frames) {
        if (!IsActive()) return false;
        const ModalDrumRecipe& r = *m_recipe;

        // Amortized coefficient rebuild: bounded work per block.
        if (m_rebuild_pos < m_num_modes_padded) {
            int end = m_rebuild_pos + kRebuildPerBlock;
            if (end > m_num_modes_padded) end = m_num_modes_padded;
            RebuildRange(m_rebuild_pos, end);
            m_rebuild_pos = end;
        }

        if (m_is_silent) {
            for (int i = 0; i < frames; ++i) mono[i] = 0.0f;
            return false;
        }

        // Per-block pitch-glide retune: trig stays out of the inner loop.  Once
        // the glide has decayed, snap the bank to exact tuning and stop paying
        // for the per-block cos pass.
        if (!m_glide_idle) {
            float glide_mult = 1.0f + m_glide_state;
            for (int m = 0; m < m_num_modes_padded; ++m)
                m_a1[m] = 2.0f * m_r[m] * cosf(m_theta[m] * glide_mult);
            if (m_glide_state < 1e-5f) m_glide_idle = true;  // final pass was ~exact
        }
        float glide_decay = expf(-kGlideDecay / m_sr);
        float noise_decay_fac = expf(-r.noise_decay / m_sr);
        float noise_sweep_fac = expf(-r.noise_sweep_hz / m_sr);
        float noise_level = r.noise_level * m_mods.noise_mult + m_mods.noise_add;

        float block_peak = 0.0f;
        for (int s = 0; s < frames; ++s) {
            // --- excitation impulse (short half-sine burst) ---
            float e = 0.0f;
            if (m_exc_pos < m_exc_len) {
                e = sinf(kPi * (float)m_exc_pos / (float)m_exc_len) * m_exc_gain;
                m_exc_pos++;
            }

            // --- resonator bank: y0 = a1*y1 - a2*y2 + g*e, 4 modes per NEON
            //     iteration; padding lanes have all-zero coeffs (contribute 0).
            float body = 0.0f;
#if defined(MODAL_DRUM_NEON)
            {
                const float32x4_t ve = vdupq_n_f32(e);
                float32x4_t vbody = vdupq_n_f32(0.0f);
                for (int m = 0; m < m_num_modes_padded; m += 4) {
                    float32x4_t a1 = vld1q_f32(&m_a1[m]);
                    float32x4_t a2 = vld1q_f32(&m_a2[m]);
                    float32x4_t y1 = vld1q_f32(&m_y1[m]);
                    float32x4_t y2 = vld1q_f32(&m_y2[m]);
                    float32x4_t g  = vld1q_f32(&m_gain[m]);
                    float32x4_t y0 = vmulq_f32(g, ve);
                    y0 = vmlaq_f32(y0, a1, y1);
                    y0 = vmlsq_f32(y0, a2, y2);
                    vst1q_f32(&m_y2[m], y1);
                    vst1q_f32(&m_y1[m], y0);
                    vbody = vaddq_f32(vbody, y0);
                }
                float32x2_t s2 = vadd_f32(vget_low_f32(vbody), vget_high_f32(vbody));
                s2 = vpadd_f32(s2, s2);
                body = vget_lane_f32(s2, 0);
            }
#else
            for (int m = 0; m < m_num_modes_padded; ++m) {
                float y0 = m_a1[m] * m_y1[m] - m_a2[m] * m_y2[m] + m_gain[m] * e;
                m_y2[m] = m_y1[m];
                m_y1[m] = y0;
                body += y0;
            }
#endif
            float sample = body;

            // --- noise wedge: broadband membrane response, cutoff sweeps down
            //     (4 cascaded 1-poles = -24 dB/oct; bright at the hit, darkens).
            if (noise_level > 0.0f && m_noise_env > 1e-5f) {
                m_rng ^= m_rng << 13; m_rng ^= m_rng >> 17; m_rng ^= m_rng << 5;
                float wn = (float)(int32_t)m_rng * (1.0f / 2147483647.0f);
                m_nz_lp1 += m_noise_coef * (wn      - m_nz_lp1);
                m_nz_lp2 += m_noise_coef * (m_nz_lp1 - m_nz_lp2);
                m_nz_lp3 += m_noise_coef * (m_nz_lp2 - m_nz_lp3);
                m_nz_lp4 += m_noise_coef * (m_nz_lp3 - m_nz_lp4);
                sample += m_nz_lp4 * m_noise_env * noise_level;
                m_noise_env *= noise_decay_fac;
                m_noise_coef = m_noise_coef_lo_eff +
                               (m_noise_coef - m_noise_coef_lo_eff) * noise_sweep_fac;
            }

            // --- attack bloom: the MEMBRANE swells to peak over a few ms; the
            //     stick click (transient below) stays sharp and is added after.
            if (m_atk_pos < m_atk_len) {
                float ph = (float)m_atk_pos / (float)m_atk_len;
                float b = 0.5f - 0.5f * cosf(kPi * ph);
                sample *= r.bloom_floor + (1.0f - r.bloom_floor) * b;
                m_atk_pos++;
            }

            // --- broadband residual "knock" (recorded, exponential-tapered) ---
            if (m_tr_pos < r.transient_len) {
                sample += r.transient[m_tr_pos] * m_tr_gain;
                m_tr_pos++;
            }

            // --- transparent peak limiter: unity below 0.85, tanh knee above —
            //     preserves the strike's crest (the "wham") unlike a soft-clip.
            float a = fabsf(sample);
            if (a > kLimThr) {
                float over = (a - kLimThr) * (1.0f / (1.0f - kLimThr));
                a = kLimThr + (1.0f - kLimThr) * tanhf(over);
                sample = (sample < 0.0f) ? -a : a;
            }

            sample *= r.out_gain;
            mono[s] = sample;
            if (a > block_peak) block_peak = a;

            m_glide_state *= glide_decay;
        }

        // Silence gate: once the strike machinery is done and the bank has
        // decayed below audibility for a stretch, stop burning the NEON loop.
        bool machinery_done = (m_exc_pos >= m_exc_len) &&
                              (m_tr_pos >= r.transient_len) &&
                              (m_noise_env <= 1e-5f || noise_level <= 0.0f);
        if (machinery_done && block_peak < 3e-5f) {
            if (++m_silent_blocks >= 8) m_is_silent = true;
        } else {
            m_silent_blocks = 0;
        }
        return !m_is_silent;
    }

private:
    static constexpr float kPi = 3.14159265358979f;
    static constexpr float kLimThr = 0.85f;
    static constexpr float kExcLenMin = 2.0f;   // samples (velocity-sharpened)
    static constexpr float kExcLenMax = 10.0f;
    static constexpr float kGlideAmount = 0.10f; // strike pitch-glide depth
    static constexpr float kGlideDecay  = 45.0f; // 1/s

    void ClearRing() {
        memset(m_y1, 0, sizeof(m_y1));
        memset(m_y2, 0, sizeof(m_y2));
        m_exc_pos = m_exc_len = 0;
        m_tr_pos = kMaxTransient; m_tr_gain = 0.0f;
        m_noise_env = 0.0f;
        m_nz_lp1 = m_nz_lp2 = m_nz_lp3 = m_nz_lp4 = 0.0f;
        m_glide_state = 0.0f; m_glide_idle = true;
        m_atk_pos = m_atk_len = 0;
        m_rng = 2463534242u;
        m_silent_blocks = 0;
        m_is_silent = true;
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

    void RebuildAll() {
        m_num_modes = SelectedModes();
        m_num_modes_padded = (m_num_modes + 3) & ~3;
        if (m_num_modes_padded > kMaxModes) m_num_modes_padded = kMaxModes;
        RebuildRange(0, m_num_modes_padded);
        m_rebuild_pos = m_num_modes_padded;
    }

    void RebuildRange(int begin, int end) {
        // A density change can shrink/grow the padded count; recompute bounds
        // when the amortized rebuild starts from 0.  Lanes dropped by a shrink
        // stop being stepped by the render loop, so their y states would
        // freeze — zero them now or they re-enter as a stale ghost when the
        // density comes back up.
        if (begin == 0) {
            int old_padded = m_num_modes_padded;
            m_num_modes = SelectedModes();
            m_num_modes_padded = (m_num_modes + 3) & ~3;
            if (m_num_modes_padded > kMaxModes) m_num_modes_padded = kMaxModes;
            if (end > m_num_modes_padded) end = m_num_modes_padded;
            for (int m = m_num_modes_padded; m < old_padded; ++m) {
                m_y1[m] = 0.0f;
                m_y2[m] = 0.0f;
            }
        }
        const ModalDrumRecipe& r = *m_recipe;
        const float f0 = r.modes[0].freq * m_mods.transpose;
        const float nyq_lim = 0.45f * m_sr;
        for (int m = begin; m < end; ++m) {
            if (m >= m_num_modes) {  // padding lanes: contribute exactly 0
                m_theta[m] = m_r[m] = m_a1[m] = m_a2[m] = m_gain[m] = 0.0f;
                m_y1[m] = m_y2[m] = 0.0f;
                continue;
            }
            float freq  = r.modes[m].freq * m_mods.transpose;
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

            if (freq >= nyq_lim) {  // transposed off the top: drop the mode
                m_theta[m] = m_r[m] = m_a1[m] = m_a2[m] = m_gain[m] = 0.0f;
                continue;
            }
            float theta = 2.0f * kPi * freq / m_sr;
            float rr = expf(-decay / m_sr);
            if (rr > 0.99999f) rr = 0.99999f;
            m_theta[m] = theta;
            m_r[m]     = rr;
            m_a1[m]    = 2.0f * rr * cosf(theta);
            m_a2[m]    = rr * rr;
            m_gain[m]  = amp * sinf(theta);  // impulse-response peak ≈ amp
        }
        // Noise-wedge cutoffs (cheap; refresh alongside any rebuild).
        m_noise_coef_hi_eff = r.noise_coef_hi * m_mods.noise_bright;
        if (m_noise_coef_hi_eff > 0.90f)  m_noise_coef_hi_eff = 0.90f;
        if (m_noise_coef_hi_eff < 0.02f)  m_noise_coef_hi_eff = 0.02f;
        m_noise_coef_lo_eff = r.noise_coef_lo;
    }

    float m_sr = 48000.0f;
    const ModalDrumRecipe* m_recipe = nullptr;
    bool  m_active = false;
    float m_ref_drive = 1.0f;
    Mods  m_mods;

    int m_num_modes = 0;
    int m_num_modes_padded = 0;
    int m_rebuild_pos = 0;

    alignas(16) float m_theta[kMaxModes];
    alignas(16) float m_r    [kMaxModes];
    alignas(16) float m_a1   [kMaxModes];
    alignas(16) float m_a2   [kMaxModes];
    alignas(16) float m_gain [kMaxModes];
    alignas(16) float m_y1   [kMaxModes];
    alignas(16) float m_y2   [kMaxModes];

    // strike state
    float m_exc_gain = 0.0f;
    int   m_exc_len = 0, m_exc_pos = 0;
    int   m_tr_pos = kMaxTransient;
    float m_tr_gain = 0.0f;
    float m_noise_env = 0.0f, m_noise_coef = 0.5f;
    float m_noise_coef_hi_eff = 0.5f, m_noise_coef_lo_eff = 0.008f;
    float m_nz_lp1 = 0.0f, m_nz_lp2 = 0.0f, m_nz_lp3 = 0.0f, m_nz_lp4 = 0.0f;
    uint32_t m_rng = 2463534242u;
    float m_glide_state = 0.0f;
    bool  m_glide_idle = true;
    int   m_atk_len = 0, m_atk_pos = 0;

    // silence gate
    int  m_silent_blocks = 0;
    bool m_is_silent = true;
};
