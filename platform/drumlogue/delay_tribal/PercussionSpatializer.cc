#include "PercussionSpatializer.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#if defined(__ARM_NEON) || defined(__ARM_NEON__)
#include <arm_neon.h>
#endif

float lfo_table[LFO_TABLE_SIZE];

__attribute__((constructor))
static void init_lfo_table() {
    for (int i = 0; i < LFO_TABLE_SIZE; ++i) {
        float p = (float)i / (float)LFO_TABLE_SIZE;
        lfo_table[i] = 2.0f * std::fabs(p - 0.5f);
    }
}

static inline float xorshift_f32(uint32_t& s) {
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    return (float)(s & 0x00FFFFFFu) / (float)0x01000000u;
}

#if defined(__ARM_NEON) || defined(__ARM_NEON__)
static inline float32x4_t clamp01q(float32x4_t x) {
    return vminq_f32(vmaxq_f32(x, vdupq_n_f32(0.0f)), vdupq_n_f32(1.0f));
}

static inline float32x4_t fast_rsqrtq(float32x4_t x) {
    float32x4_t y = vrsqrteq_f32(x);
    y = vmulq_f32(vrsqrtsq_f32(vmulq_f32(x, y), y), y);
    y = vmulq_f32(vrsqrtsq_f32(vmulq_f32(x, y), y), y);
    return y;
}

static inline float32x4_t fast_sqrtq(float32x4_t x) {
    return vmulq_f32(x, fast_rsqrtq(x));
}
#endif

PercussionSpatializer::PercussionSpatializer() {
    std::memset(params_, 0, sizeof(params_));
    std::memset(last_params_, 0, sizeof(last_params_));
    rebuild_profile();
}

inline int8_t PercussionSpatializer::Init(const unit_runtime_desc_t* desc) {
    if (desc->samplerate != 48000) return k_unit_err_samplerate;
    if (desc->input_channels != 2 || desc->output_channels != 2) {
        if (desc->input_channels == 1 && desc->output_channels == 1) {
            is_mono_ = true;
        } else {
            return k_unit_err_geometry;
        }
    }
    sample_rate_ = desc->samplerate;
    initialized_ = true;
    delay_.init((size_t)(sample_rate_ * 0.250f));
    Reset();
    return k_unit_err_none;
}

inline void PercussionSpatializer::Teardown() {}

inline void PercussionSpatializer::Reset() {
    if (!initialized_) return;
    delay_.clear();
    state_l_ = state_r_ = 0.0f;
    rng_state_ = 0x9E3729B9u;
    randomize_hit();
    rebuild_profile();
}

void PercussionSpatializer::set_clone_count(int value) {
    clone_count_ = fmax(CLONE_MIN, fmin(CLONE_MAX, value));
    rebuild_profile();
}

void PercussionSpatializer::set_mode(spatial_mode_t mode) {
    if (mode < MODE_TRIBAL || mode >= MODE_COUNT) mode = MODE_TRIBAL;
    mode_ = mode;
    rebuild_profile();
}

void PercussionSpatializer::set_depth(float norm) {
    depth_ = clamp01(norm);
    rebuild_profile();
}

void PercussionSpatializer::set_rate(float norm) {
    rate_ = 0.05f + clamp01(norm) * 9.95f;
}

void PercussionSpatializer::set_spread(float norm) {
    spread_ = clamp01(norm);
    rebuild_profile();
}

void PercussionSpatializer::set_mix(float norm) {
    mix_ = clamp01(norm);
}

void PercussionSpatializer::set_wobble(float norm) {
    wobble_ = clamp01(norm);
    rebuild_profile();
}

void PercussionSpatializer::set_attack_softening(float norm) {
    soft_atk_ = clamp01(norm);
    rebuild_profile();
}

void PercussionSpatializer::randomize_hit() {
    const float j = (xorshift_f32(rng_state_) * 2.0f - 1.0f);
    hit_jitter_ms_ = j * profile_.jitter_ms;

    for (int i = 0; i < clone_count_; ++i) {
        clones_[i].wobble_phase = xorshift_f32(rng_state_) * 2.0f * 3.1415926535f;
    }
}

static float mode_pan_exponent(spatial_mode_t mode) {
    switch (mode) {
        case MODE_TRIBAL:   return 0.92f;
        case MODE_MILITARY: return 1.12f;
        case MODE_ANGEL:    return 0.78f;
        default:            return 0.95f;
    }
}

void PercussionSpatializer::rebuild_profile() {
    static const float tribal[CLONE_MAX]   = { 18.f, 24.f, 31.f, 39.f, 48.f, 58.f };
    static const float military[CLONE_MAX] = { 12.f, 16.f, 21.f, 27.f, 34.f, 42.f };
    static const float angel[CLONE_MAX]    = { 16.f, 23.f, 31.f, 40.f, 50.f, 61.f };

    const float* base = tribal;
    if (mode_ == MODE_MILITARY) base = military;
    else if (mode_ == MODE_ANGEL) base = angel;

    profile_.spread = spread_;
    profile_.wobble_ms = 0.20f + wobble_ * 2.8f;
    profile_.jitter_ms = 0.8f + depth_ * 3.2f;
    profile_.attack_soften = soft_atk_;
    profile_.pan_exponent = mode_pan_exponent(mode_);
    profile_.pan_model = (mode_ == MODE_TRIBAL) ? PAN_MODEL_CIRCLE
                        : (mode_ == MODE_MILITARY) ? PAN_MODEL_LINE
                                                   : PAN_MODEL_SCATTER;
    profile_.scatter_amount = (mode_ == MODE_ANGEL) ? 0.42f : (mode_ == MODE_TRIBAL ? 0.15f : 0.08f);

    if (mode_ == MODE_TRIBAL) {
        profile_.hp_hz = 60.0f;
        profile_.lp_hz = 3200.0f;
    } else if (mode_ == MODE_MILITARY) {
        profile_.hp_hz = 900.0f;
        profile_.lp_hz = 9000.0f;
    } else {
        profile_.hp_hz = 180.0f;
        profile_.lp_hz = 5200.0f;
    }

    for (int i = 0; i < clone_count_; ++i) {
        const float t = (clone_count_ <= 1) ? 0.0f : (float)i / (float)(clone_count_ - 1);

        clones_[i].delay_ms = base[i] + (depth_ * 26.0f * t);
        clones_[i].gain = 1.0f - 0.12f * (float)i;
        clones_[i].gain *= (i == 0) ? 1.0f : (0.72f + 0.28f * (1.0f - soft_atk_));
        clones_[i].wobble_depth_ms = profile_.wobble_ms * (0.25f + 0.25f * t);
        clones_[i].jitter_ms = profile_.jitter_ms * (0.20f + 0.15f * t);
        clones_[i].active = true;

        float base_x = 0.0f;
        switch (profile_.pan_model) {
            case PAN_MODEL_CIRCLE: {
                float arc = (t * 2.0f - 1.0f) * 1.57079632679f;
                base_x = fastersinfullf(arc);
                break;
            }
            case PAN_MODEL_LINE: {
                base_x = t * 2.0f - 1.0f;
                break;
            }
            case PAN_MODEL_SCATTER: {
                float centered = t * 2.0f - 1.0f;
                float jitter = (xorshift_f32(rng_state_) * 2.0f - 1.0f) * profile_.scatter_amount;
                base_x = clampf(centered + jitter, -1.0f, 1.0f);
                break;
            }
        }

        float scatter = profile_.scatter_amount * (0.15f + 0.85f * t);
        float random_off = (xorshift_f32(rng_state_) * 2.0f - 1.0f) * scatter * spread_;
        clones_[i].pan_x = clampf(base_x + random_off, -1.0f, 1.0f);

#if defined(__ARM_NEON) || defined(__ARM_NEON__)
        float32x4_t x = vdupq_n_f32(clones_[i].pan_x);
        float32x4_t a = vmulq_n_f32(vaddq_f32(x, vdupq_n_f32(1.0f)), 0.5f);
        a = clamp01q(a);

        float32x4_t l = vsubq_f32(vdupq_n_f32(1.0f), a);
        float32x4_t r = a;

        if (profile_.pan_exponent > 1.0f) {
            l = vmulq_f32(l, fast_rsqrtq(vaddq_f32(l, vdupq_n_f32(1e-6f))));
            r = vmulq_f32(r, fast_rsqrtq(vaddq_f32(r, vdupq_n_f32(1e-6f))));
        } else {
            l = fast_sqrtq(l);
            r = fast_sqrtq(r);
        }

        float ll[4], rr[4];
        vst1q_f32(ll, l);
        vst1q_f32(rr, r);
        float norm = my_sqrt_f(ll[0] * ll[0] + rr[0] * rr[0] + 1e-12f);
        clones_[i].pan_l = ll[0] * norm * spread_;
        clones_[i].pan_r = rr[0] * norm * spread_;
#else
        float x = clones_[i].pan_x;
        float a = 0.5f * (x + 1.0f);
        float exponent = profile_.pan_exponent;
        float l = fasterpowf(1.0f - a, exponent);
        float r = fasterpowf(a, exponent);
        float norm = my_sqrt_f(l*l + r*r + 1e-12f);
        clones_[i].pan_l = l * norm * spread_;
        clones_[i].pan_r = r * norm * spread_;
#endif

        const float follower = (float)i / (float)fmax(1, clone_count_ - 1);
        float hp = profile_.hp_hz;
        float lp = profile_.lp_hz;
        hp *= (1.0f + follower * (mode_ == MODE_MILITARY ? 0.55f : 0.35f));
        lp *= (1.0f - follower * (mode_ == MODE_TRIBAL ? 0.28f : 0.18f));

        clones_[i].hp_coeff = hp;
        clones_[i].lp_coeff = lp;
    }
}

inline void PercussionSpatializer::setParameter(uint8_t index, int32_t value) {
    if (index >= k_total) return;
    params_[index] = (int8_t)value;

    const float norm = clamp01((float)value / 100.0f);

    switch (index) {
        case k_clones:
            set_clone_count(CLONE_MIN + fmax(0, fmin(CLONE_MAX - CLONE_MIN, value)));
            break;
        case k_mode:
            set_mode((spatial_mode_t)fmax(0, fmin((int)MODE_COUNT - 1, value)));
            break;
        case k_depth:
            set_depth(norm);
            break;
        case k_rate:
            set_rate(norm);
            break;
        case k_spread:
            set_spread(norm);
            break;
        case k_mix:
            set_mix(norm);
            break;
        case k_wobble:
            set_wobble(norm);
            break;
        case k_attack_softening:
            set_attack_softening(norm);
            break;
        default:
            break;
    }
}

inline int32_t PercussionSpatializer::getParameterValue(uint8_t index) const {
    if (index >= k_total) return 0;
    return params_[index];
}

inline const char* PercussionSpatializer::getParameterStrValue(uint8_t index, int32_t value) const {
    static const char* mode_names[] = { "Tribal", "Military", "Angel" };
    switch (index) {
        case k_mode:
            if (value >= 0 && value < MODE_COUNT) return mode_names[value];
            break;
        default:
            break;
    }
    return nullptr;
}

inline const uint8_t* PercussionSpatializer::getParameterBmpValue(uint8_t, int32_t) const {
    return nullptr;
}

float PercussionSpatializer::process_one(float in_l, float in_r, float& out_l, float& out_r) {
    delay_.push(in_l, in_r);

    const float leader_l = in_l;
    const float leader_r = in_r;

#if defined(__ARM_NEON) || defined(__ARM_NEON__)
    float wet_l = 0.0f;
    float wet_r = 0.0f;
    const float inv_12000 = 1.0f / 12000.0f;

    for (int base = 0; base < clone_count_; base += 4) {
        const int n = fmin(4, clone_count_ - base);

        float delay_ms_arr[4] = {0,0,0,0};
        float gain_arr[4] = {0,0,0,0};
        float panl_arr[4] = {0,0,0,0};
        float panr_arr[4] = {0,0,0,0};
        float hp_arr[4] = {0,0,0,0};
        float lp_arr[4] = {0,0,0,0};
        float wob_arr[4] = {0,0,0,0};

        for (int i = 0; i < n; ++i) {
            const clone_t& c = clones_[base + i];
            delay_ms_arr[i] = c.delay_ms;
            gain_arr[i] = c.gain;
            panl_arr[i] = c.pan_l;
            panr_arr[i] = c.pan_r;
            hp_arr[i] = c.hp_coeff;
            lp_arr[i] = c.lp_coeff;
            wob_arr[i] = c.wobble_depth_ms;
        }

        float tap_l[4] = {0,0,0,0};
        float tap_r[4] = {0,0,0,0};

        for (int i = 0; i < n; ++i) {
            const clone_t& c = clones_[base + i];
            float dms = delay_ms_arr[i] + hit_jitter_ms_;
            if (base + i > 0) {
                float phase = c.wobble_phase + (rate_ * 0.0015f);
                float wobble = fastersinfullf(phase) * wob_arr[i];
                dms += wobble;
            }

            float delay_samples = dms * (float)sample_rate_ * 0.001f;
            delay_.read_delay(delay_samples, tap_l[i], tap_r[i]);
        }

        float32x4_t tl = vld1q_f32(tap_l);
        float32x4_t tr = vld1q_f32(tap_r);
        float32x4_t g  = vld1q_f32(gain_arr);
        float32x4_t pl = vld1q_f32(panl_arr);
        float32x4_t pr = vld1q_f32(panr_arr);
        float32x4_t hp = vld1q_f32(hp_arr);
        float32x4_t lp = vld1q_f32(lp_arr);

        const float follower_base = (base == 0) ? 0.0f : (float)base / (float)fmax(1, clone_count_ - 1);
        float32x4_t follower = vdupq_n_f32(follower_base);
        float32x4_t soft = vsubq_f32(vdupq_n_f32(1.0f),
                                     vmulq_f32(vdupq_n_f32(profile_.attack_soften),
                                               vaddq_f32(vdupq_n_f32(0.35f),
                                                         vmulq_n_f32(follower, 0.65f))));
        soft = vmaxq_f32(soft, vdupq_n_f32(0.08f));

        float32x4_t hp_mix = vrecpeq_f32(vaddq_f32(vdupq_n_f32(1.0f), vmulq_n_f32(hp, 0.0012f)));
        hp_mix = vmulq_f32(vrecpsq_f32(vaddq_f32(vdupq_n_f32(1.0f), vmulq_n_f32(hp, 0.0012f)), hp_mix), hp_mix);
        float32x4_t lp_mix = vsubq_f32(vdupq_n_f32(1.0f),
                                       vminq_f32(vdupq_n_f32(0.85f),
                                                 vmulq_n_f32(lp, vdupq_n_f32(inv_12000))));

        float32x4_t wg = vmulq_f32(g, soft);
        wg = vmulq_f32(wg, hp_mix);
        wg = vmulq_f32(wg, lp_mix);

        float32x4_t wl = vmulq_f32(tl, vmulq_f32(wg, pl));
        float32x4_t wr = vmulq_f32(tr, vmulq_f32(wg, pr));

        float tmp_l[4], tmp_r[4];
        vst1q_f32(tmp_l, wl);
        vst1q_f32(tmp_r, wr);
        wet_l += tmp_l[0] + tmp_l[1] + tmp_l[2] + tmp_l[3];
        wet_r += tmp_r[0] + tmp_r[1] + tmp_r[2] + tmp_r[3];
    }

    out_l = leader_l * (1.0f - mix_) + wet_l * mix_;
    out_r = leader_r * (1.0f - mix_) + wet_r * mix_;
    return 0.5f * (std::fabs(out_l) + std::fabs(out_r));
#else
    float wet_l = 0.0f;
    float wet_r = 0.0f;

    for (int i = 0; i < clone_count_; ++i) {
        const clone_t& c = clones_[i];

        float dms = c.delay_ms + hit_jitter_ms_;
        if (i > 0) {
            float phase = c.wobble_phase + (rate_ * 0.0015f);
            float wobble = fastersinfullf(phase) * c.wobble_depth_ms;
            dms += wobble;
        }

        float delay_samples = dms * (float)sample_rate_ * 0.001f;
        float dl = 0.0f, dr = 0.0f;
        delay_.read_delay(delay_samples, dl, dr);

        const float follower = (float)i / (float)fmax(1, clone_count_ - 1);
        float soft = 1.0f - profile_.attack_soften * (0.35f + 0.65f * follower);
        soft = fmax(0.08f, soft);

        float hp = c.hp_coeff;
        float lp = c.lp_coeff;

        float hp_mix = 1.0f / (1.0f + hp * 0.0012f);
        float lp_mix = 1.0f - fmin(0.85f, lp / 12000.0f);

        float follower_gain = c.gain * soft * hp_mix * lp_mix;
        wet_l += dl * follower_gain * c.pan_l;
        wet_r += dr * follower_gain * c.pan_r;
    }

    out_l = leader_l * (1.0f - mix_) + wet_l * mix_;
    out_r = leader_r * (1.0f - mix_) + wet_r * mix_;
    return 0.5f * (std::fabs(out_l) + std::fabs(out_r));
#endif
}

inline void PercussionSpatializer::Render(const float* in, float* out, size_t frames) {
    if (!initialized_) {
        std::memset(out, 0, frames * 2 * sizeof(float));
        return;
    }

    for (size_t i = 0; i < frames; ++i) {
        float in_l = in[i * 2 + 0];
        float in_r = in[i * 2 + 1];
        float out_l = 0.0f;
        float out_r = 0.0f;

        static float prev_mag = 0.0f;
        float mag = 0.5f * (std::fabs(in_l) + std::fabs(in_r));
        bool transient = (mag > prev_mag * 1.9f) && (mag > 0.002f);
        prev_mag = mag;

        if (transient) {
            randomize_hit();
        }

        process_one(in_l, in_r, out_l, out_r);

        out[i * 2 + 0] = out_l;
        out[i * 2 + 1] = out_r;
    }
}
