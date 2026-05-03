#include "PercussionSpatializer.h"

#include <cmath>
#include <cstdio>

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

PercussionSpatializer::PercussionSpatializer() {
    std::memset(params_, 0, sizeof(params_));
    std::memset(last_params_, 0, sizeof(last_params_));
    clone_set_index_ = CLONE_SET_4;
    clone_count_ = 4;
    rebuild_profile();
}

int8_t PercussionSpatializer::Init(const unit_runtime_desc_t* desc) {
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

void PercussionSpatializer::Teardown() {}

void PercussionSpatializer::Reset() {
    if (!initialized_) return;
    delay_.clear();
    rng_state_ = 0x9E3729B9u;
    prev_mag_ = 0.0f;
    smoothing_remaining_ = 0;
    randomize_hit();
    rebuild_profile();
}

void PercussionSpatializer::set_clone_count_index(int index) {
    clone_set_index_ = fmax(0, fmin(CLONE_SET_CNT - 1, index));
    clone_count_ = kCloneValues[clone_set_index_];
    pending_profile_rebuild_ = true;
}

void PercussionSpatializer::set_mode(spatial_mode_t mode) {
    if (mode < MODE_TRIBAL || mode >= MODE_COUNT) mode = MODE_TRIBAL;
    mode_ = mode;
    pending_profile_rebuild_ = true;
}

void PercussionSpatializer::set_depth(float norm) {
    depth_ = clamp01(norm);
    pending_profile_rebuild_ = true;
}

void PercussionSpatializer::set_rate(float norm) {
    rate_target_ = 0.05f + clamp01(norm) * 9.95f;
    smoothing_remaining_ = kSmoothFrames;
}

void PercussionSpatializer::set_spread(float norm) {
    spread_ = clamp01(norm);
    pending_profile_rebuild_ = true;
}

void PercussionSpatializer::set_mix(float norm) {
    mix_target_ = clamp01(norm);
    smoothing_remaining_ = kSmoothFrames;
}

void PercussionSpatializer::set_wobble(float norm) {
    wobble_target_ = clamp01(norm);
    smoothing_remaining_ = kSmoothFrames;
}

void PercussionSpatializer::set_scatter(float norm) {
    scatter_target_ = clamp01(norm);
    smoothing_remaining_ = kSmoothFrames;
}

void PercussionSpatializer::set_attack_softening(float norm) {
    soft_atk_target_ = clamp01(norm);
    smoothing_remaining_ = kSmoothFrames;
}

void PercussionSpatializer::advance_smoothing() {
    if (smoothing_remaining_ == 0) return;

    auto step = [this](float& cur, float tgt) {
        cur += (tgt - cur) / (float)fmax<uint32_t>(1, smoothing_remaining_);
    };

    step(rate_, rate_target_);
    step(mix_, mix_target_);
    step(wobble_, wobble_target_);
    step(scatter_, scatter_target_);
    step(soft_atk_, soft_atk_target_);

    --smoothing_remaining_;

    if (smoothing_remaining_ == 0) {
        rate_ = rate_target_;
        mix_ = mix_target_;
        wobble_ = wobble_target_;
        scatter_ = scatter_target_;
        soft_atk_ = soft_atk_target_;
    }

    pending_profile_rebuild_ = true;
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
        case MODE_TRIBAL:   return 0.90f;
        case MODE_MILITARY: return 1.12f;
        case MODE_ANGEL:    return 0.78f;
        default:            return 0.96f;
    }
}

static pan_model_t mode_pan_model(spatial_mode_t mode) {
    switch (mode) {
        case MODE_TRIBAL:   return PAN_MODEL_CIRCLE;
        case MODE_MILITARY: return PAN_MODEL_LINE;
        case MODE_ANGEL:    return PAN_MODEL_SCATTER;
        default:            return PAN_MODEL_CIRCLE;
    }
}

void PercussionSpatializer::rebuild_profile() {
    static const float tribal[MAX_CLONES]   = { 18.f, 24.f, 31.f, 39.f, 48.f, 58.f, 67.f, 77.f, 88.f, 100.f };
    static const float military[MAX_CLONES] = { 12.f, 16.f, 21.f, 27.f, 34.f, 42.f, 51.f, 61.f, 72.f, 84.f };
    static const float angel[MAX_CLONES]    = { 16.f, 23.f, 31.f, 40.f, 50.f, 61.f, 73.f, 86.f, 100.f, 115.f };

    const float* base = tribal;
    if (mode_ == MODE_MILITARY) base = military;
    else if (mode_ == MODE_ANGEL) base = angel;

    profile_.spread = spread_;
    profile_.wobble_ms = 0.20f + wobble_ * 2.8f;
    profile_.jitter_ms = 0.8f + depth_ * 3.2f;
    profile_.attack_soften = soft_atk_;
    profile_.pan_exponent = mode_pan_exponent(mode_);
    profile_.pan_model = mode_pan_model(mode_);
    profile_.scatter_amount = (mode_ == MODE_ANGEL ? 0.30f : 0.10f) + scatter_ * (mode_ == MODE_ANGEL ? 0.70f : 0.40f);

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

        clones_[i].delay_ms = base[i] * (0.65f + 0.55f * depth_) + (depth_ * 22.0f * t);
        clones_[i].gain = 1.0f - 0.12f * (float)i;
        clones_[i].gain *= (i == 0) ? 1.0f : (0.72f + 0.28f * (1.0f - soft_atk_));
        clones_[i].wobble_depth_ms = profile_.wobble_ms * (0.20f + 0.30f * t);
        clones_[i].jitter_ms = profile_.jitter_ms * (0.15f + 0.25f * t);
        clones_[i].active = true;

        float base_x = 0.0f;
        switch (profile_.pan_model) {
            case PAN_MODEL_CIRCLE: {
                float arc = t * M_PI - 1.57079632679f;  // -pi/2, +pi/2
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

        float x = clones_[i].pan_x;
        float a = 0.5f * (x + 1.0f);
        float exponent = profile_.pan_exponent;
        float l = fasterpowf(1.0f - a, exponent);
        float r = fasterpowf(a, exponent);
        float norm = my_sqrt_f(l*l + r*r + 1e-12f);
        clones_[i].pan_l = l * norm * spread_;
        clones_[i].pan_r = r * norm * spread_;

        const float follower = (float)i / (float)fmax(1, clone_count_ - 1);
        float hp = profile_.hp_hz;
        float lp = profile_.lp_hz;
        hp *= (1.0f + follower * (mode_ == MODE_MILITARY ? 0.55f : 0.35f));
        lp *= (1.0f - follower * (mode_ == MODE_TRIBAL ? 0.28f : 0.18f));

        clones_[i].hp_hz = hp;
        clones_[i].lp_hz = lp;
    }

    pending_profile_rebuild_ = false;
}

void PercussionSpatializer::setParameter(uint8_t index, int32_t value) {
    if (index >= k_total) return;
    params_[index] = (int8_t)value;

    const float norm = clamp01((float)value * 0.01f);

    switch (index) {
        case k_clones:
            set_clone_count_index(fmax(0, fmin(CLONE_SET_CNT - 1, value)));
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
        case k_scatter:
            set_scatter(norm);
            break;
        case k_attack_softening:
            set_attack_softening(norm);
            break;
        default:
            break;
    }
}

int32_t PercussionSpatializer::getParameterValue(uint8_t index) const {
    if (index >= k_total) return 0;
    return params_[index];
}

const char* PercussionSpatializer::getParameterStrValue(uint8_t index, int32_t value) const {
    static const char* mode_names[] = { "Tribale", "Militare", "Angeli" };
    static const char* clone_names[] = { "2cloni", "4cloni", "6cloni", "8cloni", "10cloni" };

    switch (index) {
        case k_mode:
            if (value >= 0 && value < MODE_COUNT) return mode_names[value];
            break;
        case k_clones:
            if (value >= 0 && value < CLONE_SET_CNT) return clone_names[value];
            break;
        default:
            break;
    }
    return nullptr;
}

const uint8_t* PercussionSpatializer::getParameterBmpValue(uint8_t, int32_t) const {
    return nullptr;
}

static fast_inline void mix_clone_batch4(const clone_t* clones,
                                         int base,
                                         const delay_line_t& delay,
                                         float sample_rate,
                                         float hit_jitter_ms,
                                         float rate,
                                         float scatter_amount,
                                         uint32_t& rng_state,
                                         float& wet_l,
                                         float& wet_r) {
    alignas(16) float dl[NEON_LANES], dr[NEON_LANES], gain[NEON_LANES], pan_l[NEON_LANES], pan_r[NEON_LANES], hp_mix[NEON_LANES], lp_mix[NEON_LANES];

    for (int lane = 0; lane < NEON_LANES; ++lane) {
        const clone_t& c = clones[base + lane];
        float dms = c.delay_ms + hit_jitter_ms;
        if (base + lane > 0) {
            dms += fastersinfullf(c.wobble_phase + rate * 0.0015f) * c.wobble_depth_ms;
        }

        float scatter_jit = (xorshift_f32(rng_state) * 2.0f - 1.0f) * (scatter_amount * 2.4f) * (0.25f + (float)(base + lane) * 0.08333333333f);  // approx 0.75f / 9.0f
        dms += scatter_jit;

        delay.read_delay(dms * sample_rate * 0.001f, dl[lane], dr[lane]);
        gain[lane] = c.gain;
        pan_l[lane] = c.pan_l;
        pan_r[lane] = c.pan_r;
        hp_mix[lane] = 1.0f / (1.0f + c.hp_hz * 0.0012f);
        lp_mix[lane] = 1.0f - fmin(0.85f, c.lp_hz * inv_12000);
    }

    float32x4_t vdl = vld1q_f32(dl);
    float32x4_t vdr = vld1q_f32(dr);
    float32x4_t vgain = vld1q_f32(gain);
    float32x4_t vpl = vld1q_f32(pan_l);
    float32x4_t vpr = vld1q_f32(pan_r);
    float32x4_t vhp = vld1q_f32(hp_mix);
    float32x4_t vlp = vld1q_f32(lp_mix);

    float32x4_t vl = vmulq_f32(vmulq_f32(vdl, vgain), vmulq_f32(vpl, vmulq_f32(vhp, vlp)));
    float32x4_t vr = vmulq_f32(vmulq_f32(vdr, vgain), vmulq_f32(vpr, vmulq_f32(vhp, vlp)));

    wet_l += horizontal_sum4(vl);
    wet_r += horizontal_sum4(vr);
}

static fast_inline void mix_clone_batch2(const clone_t* clones,
                                         int base,
                                         const delay_line_t& delay,
                                         float sample_rate,
                                         float hit_jitter_ms,
                                         float rate,
                                         float scatter_amount,
                                         uint32_t& rng_state,
                                         float& wet_l,
                                         float& wet_r) {
    alignas(16) float dl[HALF_LANES], dr[HALF_LANES], gain[HALF_LANES], pan_l[HALF_LANES], pan_r[HALF_LANES], hp_mix[HALF_LANES], lp_mix[HALF_LANES];

    for (int lane = 0; lane < HALF_LANES; ++lane) {
        const clone_t& c = clones[base + lane];
        float dms = c.delay_ms + hit_jitter_ms;
        if (base + lane > 0) {
            dms += fastersinfullf(c.wobble_phase + rate * 0.0015f) * c.wobble_depth_ms;
        }

        float scatter_jit = (xorshift_f32(rng_state) * 2.0f - 1.0f) * (scatter_amount * 2.4f) * (0.25f + (float)(base + lane) * 0.08333333333f);  // approx 0.75f / 9.0f
        dms += scatter_jit;

        delay.read_delay(dms * sample_rate * 0.001f, dl[lane], dr[lane]);
        gain[lane] = c.gain;
        pan_l[lane] = c.pan_l;
        pan_r[lane] = c.pan_r;
        hp_mix[lane] = 1.0f / (1.0f + c.hp_hz * 0.0012f);
        lp_mix[lane] = 1.0f - fmin(0.85f, c.lp_hz * inv_12000);
    }

    float32x2_t vdl = vld1_f32(dl);
    float32x2_t vdr = vld1_f32(dr);
    float32x2_t vgain = vld1_f32(gain);
    float32x2_t vpl = vld1_f32(pan_l);
    float32x2_t vpr = vld1_f32(pan_r);
    float32x2_t vhp = vld1_f32(hp_mix);
    float32x2_t vlp = vld1_f32(lp_mix);

    float32x2_t vl = vmul_f32(vmul_f32(vdl, vgain), vmul_f32(vpl, vmul_f32(vhp, vlp)));
    float32x2_t vr = vmul_f32(vmul_f32(vdr, vgain), vmul_f32(vpr, vmul_f32(vhp, vlp)));

    wet_l += horizontal_sum2(vl);
    wet_r += horizontal_sum2(vr);
}

float PercussionSpatializer::process_frame(float in_l, float in_r, float& out_l, float& out_r) {
    advance_smoothing();
    if (pending_profile_rebuild_) rebuild_profile();

    delay_.push(in_l, in_r);

    float wet_l = 0.0f;
    float wet_r = 0.0f;
    int i = 0;

    for (; i + 3 < clone_count_; i += 4) {
        mix_clone_batch4(clones_, i, delay_, (float)sample_rate_, hit_jitter_ms_, rate_, profile_.scatter_amount, rng_state_, wet_l, wet_r);
    }
    for (; i + 1 < clone_count_; i += 2) {
        mix_clone_batch2(clones_, i, delay_, (float)sample_rate_, hit_jitter_ms_, rate_, profile_.scatter_amount, rng_state_, wet_l, wet_r);
    }
    for (; i < clone_count_; ++i) {
        const clone_t& c = clones_[i];
        float dms = c.delay_ms + hit_jitter_ms_;
        if (i > 0) {
            dms += fastersinfullf(c.wobble_phase + rate_ * 0.0015f) * c.wobble_depth_ms;
        }
        float scatter_jit = (xorshift_f32(rng_state_) * 2.0f - 1.0f) * (profile_.scatter_amount * 2.4f) * (0.25f + (float)i * 0.08333333333f);  // approx 0.75f / 9.0f
        dms += scatter_jit;

        float dl = 0.0f, dr = 0.0f;
        delay_.read_delay(dms * (float)sample_rate_ * 0.001f, dl, dr);

        const float follower = (float)i / (float)fmax(1, clone_count_ - 1);
        float soft = 1.0f - soft_atk_ * (0.35f + 0.65f * follower);
        soft = fmax(0.08f, soft);
        float hp_mix = 1.0f / (1.0f + c.hp_hz * 0.0012f);
        float lp_mix = 1.0f - fmin(0.85f, c.lp_hz * inv_12000);
        float detachment = fmax(0.35f, 1.0f - scatter_ * (0.10f + 0.35f * follower));
        float follower_gain = c.gain * soft * detachment;
        wet_l += dl * follower_gain * hp_mix * lp_mix * c.pan_l;
        wet_r += dr * follower_gain * hp_mix * lp_mix * c.pan_r;
    }

    out_l = in_l * (1.0f - mix_) + wet_l * mix_;
    out_r = in_r * (1.0f - mix_) + wet_r * mix_;
    return 0.5f * (std::fabs(out_l) + std::fabs(out_r));
}

void PercussionSpatializer::Render(const float* in, float* out, size_t frames) {
    if (!initialized_) {
        std::memset(out, 0, frames * 2 * sizeof(float));
        return;
    }

    for (size_t i = 0; i < frames; ++i) {
        float out_l = 0.0f, out_r = 0.0f;
        float in_l = in[i * 2 + 0];
        float in_r = in[i * 2 + 1];

        float mag = 0.5f * (std::fabs(in_l) + std::fabs(in_r));
        bool transient = (mag > prev_mag_ * 1.9f) && (mag > 0.002f);
        prev_mag_ = mag;

        if (transient) randomize_hit();

        process_frame(in_l, in_r, out_l, out_r);
        out[i * 2 + 0] = out_l;
        out[i * 2 + 1] = out_r;
    }
}
