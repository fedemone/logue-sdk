/**
 * @file PercussionSpatializer.cc
 */

#include "PercussionSpatializer.h"
#include <cmath>

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static inline float xorshift_f32(uint32_t& s) {
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    return (float)(s & 0x00FFFFFFu) / (float)0x01000000u;
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

PercussionSpatializer::PercussionSpatializer() {
    memset(params_, 0, sizeof(params_));
    clone_set_index_ = CLONE_SET_4;
    clone_count_     = 4;
}

int8_t PercussionSpatializer::Init(const unit_runtime_desc_t* desc) {
    if (desc->samplerate != 48000) return k_unit_err_samplerate;
    if (desc->input_channels != 2 || desc->output_channels != 2)
        return k_unit_err_geometry;
    sample_rate_  = desc->samplerate;
    initialized_  = true;
    Reset();
    return k_unit_err_none;
}

void PercussionSpatializer::Teardown() {}

void PercussionSpatializer::Reset() {
    if (!initialized_) return;
    delay_.clear();
    rng_state_  = 0x9E3729B9u;
    prev_mag_   = 0.0f;
    smoothing_remaining_ = 0;
    rebuild_profile();
    randomize_hit();
}

// ---------------------------------------------------------------------------
// Parameter setters
// ---------------------------------------------------------------------------

void PercussionSpatializer::set_clone_count_index(int idx) {
    idx = idx < 0 ? 0 : (idx >= CLONE_SET_CNT ? CLONE_SET_CNT - 1 : idx);
    clone_set_index_ = idx;
    clone_count_     = kCloneValues[idx];
    pending_profile_rebuild_ = true;
}

void PercussionSpatializer::set_mode(spatial_mode_t mode) {
    if (mode >= MODE_COUNT) mode = MODE_TRIBAL;
    mode_ = mode;
    pending_profile_rebuild_ = true;
}

// Spatial parameters: immediate rebuild at next block boundary
void PercussionSpatializer::set_depth(float norm)  { depth_  = clamp01(norm); pending_profile_rebuild_ = true; }
void PercussionSpatializer::set_spread(float norm) { spread_ = clamp01(norm); pending_profile_rebuild_ = true; }
void PercussionSpatializer::set_scatter(float norm) {
    scatter_target_ = clamp01(norm);
    smoothing_remaining_ = kSmoothBlocks;
    pending_profile_rebuild_ = true;   // rebuild once now with current scatter, again at end
}

// Dynamic parameters: smoothed, no full rebuild (update_clone_dynamics handles them)
void PercussionSpatializer::set_rate(float norm)            { rate_target_     = 0.05f + clamp01(norm) * 9.95f; smoothing_remaining_ = kSmoothBlocks; }
void PercussionSpatializer::set_mix(float norm)             { mix_target_      = clamp01(norm);  smoothing_remaining_ = kSmoothBlocks; }
void PercussionSpatializer::set_wobble(float norm)          { wobble_target_   = clamp01(norm);  smoothing_remaining_ = kSmoothBlocks; }
void PercussionSpatializer::set_attack_softening(float norm){ soft_atk_target_ = clamp01(norm);  smoothing_remaining_ = kSmoothBlocks; }

// ---------------------------------------------------------------------------
// Smoothing — called once per 4-frame block
// Does NOT trigger rebuild_profile().  update_clone_dynamics() updates gains
// cheaply.  One full rebuild fires at the end of the smoothing window.
// ---------------------------------------------------------------------------
void PercussionSpatializer::advance_smoothing() {
    if (smoothing_remaining_ == 0) return;

    const float inv_rem = 1.0f / (float)smoothing_remaining_;
    rate_     += (rate_target_     - rate_)     * inv_rem;
    mix_      += (mix_target_      - mix_)      * inv_rem;
    wobble_   += (wobble_target_   - wobble_)   * inv_rem;
    scatter_  += (scatter_target_  - scatter_)  * inv_rem;
    soft_atk_ += (soft_atk_target_ - soft_atk_) * inv_rem;

    update_clone_dynamics();  // cheap: no pow/sqrt

    if (--smoothing_remaining_ == 0) {
        rate_     = rate_target_;
        mix_      = mix_target_;
        wobble_   = wobble_target_;
        scatter_  = scatter_target_;
        soft_atk_ = soft_atk_target_;
        pending_profile_rebuild_ = true;   // final rebuild with settled values
    }
}

// ---------------------------------------------------------------------------
// update_clone_dynamics — cheap per-block refresh of net gains + wobble depth
// Only arithmetic: no fasterpowf, no my_sqrt_f.
// ---------------------------------------------------------------------------
void PercussionSpatializer::update_clone_dynamics() {
    const float ms_to_smp = (float)sample_rate_ * 0.001f;
    const float wobble_ms = 0.20f + wobble_ * 2.8f;
    const float inv_cnt1  = 1.0f / (float)(clone_count_ > 1 ? clone_count_ - 1 : 1);

    for (int i = 0; i < clone_count_; ++i) {
        const float t        = (float)i * inv_cnt1;
        const float soft_fac = (i == 0) ? 1.0f : (0.72f + 0.28f * (1.0f - soft_atk_));
        clones_[i].wobble_depth_samples = wobble_ms * (0.20f + 0.30f * t) * ms_to_smp;
        clones_[i].net_gain_l = clones_[i].base_gain * soft_fac * clones_[i].pan_gain_l;
        clones_[i].net_gain_r = clones_[i].base_gain * soft_fac * clones_[i].pan_gain_r;
    }
}

// ---------------------------------------------------------------------------
// Mode helpers
// ---------------------------------------------------------------------------
static float mode_pan_exponent(spatial_mode_t mode) {
    switch (mode) {
        case MODE_MILITARY: return 1.12f;
        case MODE_ANGEL:    return 0.78f;
        default:            return 0.90f;
    }
}

static pan_model_t mode_pan_model(spatial_mode_t mode) {
    switch (mode) {
        case MODE_MILITARY: return PAN_MODEL_LINE;
        case MODE_ANGEL:    return PAN_MODEL_SCATTER;
        default:            return PAN_MODEL_CIRCLE;
    }
}

// ---------------------------------------------------------------------------
// rebuild_profile — full spatial rebuild; called at most twice per parameter
// change (once at first block after change, once at end of smoothing).
// ---------------------------------------------------------------------------
void PercussionSpatializer::rebuild_profile() {
    // Base delay times (ms) per mode
    static const float tribal[MAX_CLONES]   = { 18.f, 24.f, 31.f, 39.f, 48.f, 58.f, 67.f, 77.f, 88.f,100.f };
    // Military: wider spread, more distinct hits for ensemble feel
    static const float military[MAX_CLONES] = { 15.f, 21.f, 28.f, 37.f, 47.f, 58.f, 70.f, 84.f, 99.f,115.f };
    static const float angel[MAX_CLONES]    = { 16.f, 23.f, 31.f, 40.f, 50.f, 61.f, 73.f, 86.f,100.f,115.f };

    const float* base_delay = (mode_ == MODE_MILITARY) ? military :
                              (mode_ == MODE_ANGEL)    ? angel    : tribal;

    // Profile fields consumed by randomize_hit()
    profile_.jitter_ms     = 0.8f + depth_ * 3.2f;
    profile_.scatter_amount = (mode_ == MODE_ANGEL ? 0.30f : 0.10f)
                            + scatter_ * (mode_ == MODE_ANGEL ? 0.70f : 0.40f);
    profile_.pan_exponent  = mode_pan_exponent(mode_);
    profile_.pan_model     = mode_pan_model(mode_);

    // HP/LP base frequencies and per-follower multipliers
    float hp_base, lp_base, hp_follow;
    if (mode_ == MODE_TRIBAL) {
        hp_base  =   60.0f;  lp_base = 3200.0f;  hp_follow = 0.35f;
    } else if (mode_ == MODE_MILITARY) {
        // Lower HP preserves the body/punch of each stroke.
        // Was 900 Hz (killed everything below — followers became inaudible).
        hp_base  =  150.0f;  lp_base = 7000.0f;  hp_follow = 0.30f;
    } else {
        hp_base  =  180.0f;  lp_base = 5200.0f;  hp_follow = 0.35f;
    }

    // Gain rolloff: exponential avoids the linear formula going negative at i>=9
    // Military: 0.92^i — slow rolloff keeps late clones audible (ensemble presence)
    const float gain_step = (mode_ == MODE_MILITARY) ? 0.92f :
                            (mode_ == MODE_ANGEL)    ? 0.85f : 0.88f;

    const float ms_to_smp = (float)sample_rate_ * 0.001f;
    const float wobble_ms = 0.20f + wobble_ * 2.8f;
    const float inv_cnt1  = 1.0f / (float)(clone_count_ > 1 ? clone_count_ - 1 : 1);

    for (int i = 0; i < clone_count_; ++i) {
        const float t        = (float)i * inv_cnt1;
        const float follower = t;

        // Delay and wobble (samples)
        float dms = base_delay[i] * (0.65f + 0.55f * depth_) + depth_ * 22.0f * t;
        clones_[i].delay_samples        = dms * ms_to_smp;
        clones_[i].wobble_depth_samples = wobble_ms * (0.20f + 0.30f * t) * ms_to_smp;

        // Per-clone wobble rate stagger (makes each clone drift independently)
        clones_[i].wobble_rate_mul = 0.70f + 0.30f * t;

        // Pan position
        float base_x = 0.0f;
        switch (profile_.pan_model) {
            case PAN_MODEL_CIRCLE: {
                float arc = t * 3.14159265f - 1.5707963f;
                base_x = fastersinfullf(arc);
                break;
            }
            case PAN_MODEL_LINE:
                base_x = t * 2.0f - 1.0f;
                break;
            case PAN_MODEL_SCATTER: {
                float center = t * 2.0f - 1.0f;
                float jit    = (xorshift_f32(rng_state_) * 2.0f - 1.0f) * profile_.scatter_amount;
                base_x       = clampf(center + jit, -1.0f, 1.0f);
                break;
            }
        }
        float scat_r   = profile_.scatter_amount * (0.15f + 0.85f * t);
        float rand_off = (xorshift_f32(rng_state_) * 2.0f - 1.0f) * scat_r * spread_;
        float px       = clampf(base_x + rand_off, -1.0f, 1.0f);

        float a    = 0.5f * (px + 1.0f);
        float pl   = fasterpowf(1.0f - a, profile_.pan_exponent);
        float pr   = fasterpowf(a,        profile_.pan_exponent);
        float pnrm = my_sqrt_f(pl * pl + pr * pr + 1e-12f);
        float pan_l = pl * pnrm * spread_;
        float pan_r = pr * pnrm * spread_;

        // HP/LP attenuation baked into pan_gain (eliminates per-sample division)
        float hp_hz   = hp_base * (1.0f + follower * hp_follow);
        float lp_hz   = lp_base * (1.0f - follower * (mode_ == MODE_TRIBAL ? 0.28f : 0.18f));
        float hp_attn = 1.0f / (1.0f + hp_hz * 0.0012f);
        float lp_attn = 1.0f - fminf(0.85f, lp_hz * inv_12000);
        clones_[i].pan_gain_l = pan_l * hp_attn * lp_attn;
        clones_[i].pan_gain_r = pan_r * hp_attn * lp_attn;

        // Base gain: exponential rolloff × scatter detachment
        float raw_gain   = fasterpowf(gain_step, (float)i);
        float detachment = fmaxf(0.35f, 1.0f - profile_.scatter_amount * (0.10f + 0.35f * follower));
        clones_[i].base_gain = raw_gain * detachment;

        // net_gain: apply current soft_atk (will be refreshed each block by update_clone_dynamics)
        float soft_fac       = (i == 0) ? 1.0f : (0.72f + 0.28f * (1.0f - soft_atk_));
        clones_[i].net_gain_l = clones_[i].base_gain * soft_fac * clones_[i].pan_gain_l;
        clones_[i].net_gain_r = clones_[i].base_gain * soft_fac * clones_[i].pan_gain_r;
    }

    pending_profile_rebuild_ = false;
}

// ---------------------------------------------------------------------------
// randomize_hit — called on transient detection
// Pre-computes scatter_samples per clone so the render loop is xorshift-free.
// ---------------------------------------------------------------------------
void PercussionSpatializer::randomize_hit() {
    const float ms_to_smp  = (float)sample_rate_ * 0.001f;
    const float inv_cnt1   = 1.0f / (float)(clone_count_ > 1 ? clone_count_ - 1 : 1);
    const float global_jit = (xorshift_f32(rng_state_) * 2.0f - 1.0f) * profile_.jitter_ms;

    for (int i = 0; i < clone_count_; ++i) {
        const float follower  = (float)i * inv_cnt1;
        const float max_sct   = profile_.scatter_amount * 2.4f * (0.25f + 0.75f * follower);
        const float clone_jit = (xorshift_f32(rng_state_) * 2.0f - 1.0f) * max_sct;
        clones_[i].scatter_samples = (global_jit + clone_jit) * ms_to_smp;
        clones_[i].wobble_phase    = xorshift_f32(rng_state_) * (2.0f * 3.14159265f);
    }
}

// ---------------------------------------------------------------------------
// render_block4 — 4 frames, NEON interpolation across time
// ---------------------------------------------------------------------------
void PercussionSpatializer::render_block4(const float* in, float* out) {
    // 1. Push all 4 input samples; delay_.write advances by 4
    for (int s = 0; s < 4; ++s)
        delay_.push(in[s * 2], in[s * 2 + 1]);

    // 2. Transient detection — check peak of 4 frames
    float mag_max = 0.0f;
    for (int s = 0; s < 4; ++s) {
        float m = 0.5f * (fabsf(in[s * 2]) + fabsf(in[s * 2 + 1]));
        if (m > mag_max) mag_max = m;
    }
    const bool transient = (mag_max > prev_mag_ * 1.9f) && (mag_max > 0.002f);
    prev_mag_ = 0.5f * (fabsf(in[6]) + fabsf(in[7]));  // last frame

    // 3. Advance smoothing + rebuild if needed; THEN randomize on transient
    //    (ensures randomize_hit uses the freshly rebuilt profile)
    advance_smoothing();
    if (pending_profile_rebuild_) rebuild_profile();
    if (transient) randomize_hit();

    // 4. Per-clone NEON accumulation
    const float two_pi     = 2.0f * 3.14159265f;
    const float block_rate = rate_ * two_pi / (float)sample_rate_ * 4.0f;

    // Safe offset to keep float read-position positive without changing frac
    const float safe_offset = (float)(delay_line_t::kLen * 8);

    float32x4_t wet_l4 = vdupq_n_f32(0.0f);
    float32x4_t wet_r4 = vdupq_n_f32(0.0f);

    for (int k = 0; k < clone_count_; ++k) {
        clone_t& c = clones_[k];

        // Advance LFO phase (staggered rate per clone for natural ensemble drift)
        c.wobble_phase += block_rate * c.wobble_rate_mul;
        if (c.wobble_phase >= two_pi) c.wobble_phase -= two_pi;

        // Total delay is constant over the 4 samples (wobble is slow)
        const float wob     = fastersinfullf(c.wobble_phase) * c.wobble_depth_samples;
        float total_d = c.delay_samples + wob + c.scatter_samples;
        if (total_d < 2.0f) total_d = 2.0f;

        // Compute the float read-position for sample 0 of the block:
        //   pos_s0 = (delay_.write - 3) - total_d
        // (Each subsequent sample shifts by +1, so frac is constant across the block.)
        // Add safe_offset to guarantee positivity before int cast.
        const float raw_pos  = (float)(delay_.write - 3) - total_d;
        const float safe_pos = raw_pos + safe_offset;
        const int   base_int = (int)safe_pos;          // floor (positive value)
        const float fr       = safe_pos - (float)base_int;
        const int   base     = base_int & delay_line_t::kMask;

        // 5 scalar reads (wrap handled by bitmask)
        alignas(16) float sl[5], sr[5];
        delay_.read5(base, sl, sr);

        // NEON linear interpolation across 4 time samples:
        //   out[s] = sl[s] + fr*(sl[s+1]-sl[s])
        float32x4_t vl0   = vld1q_f32(sl);
        float32x4_t vl1   = vld1q_f32(sl + 1);
        float32x4_t vr0   = vld1q_f32(sr);
        float32x4_t vr1   = vld1q_f32(sr + 1);
        float32x4_t vfrac = vdupq_n_f32(fr);

        float32x4_t cl4 = vmlaq_f32(vl0, vsubq_f32(vl1, vl0), vfrac);
        float32x4_t cr4 = vmlaq_f32(vr0, vsubq_f32(vr1, vr0), vfrac);

        wet_l4 = vmlaq_n_f32(wet_l4, cl4, c.net_gain_l);
        wet_r4 = vmlaq_n_f32(wet_r4, cr4, c.net_gain_r);
    }

    // 5. Dry-wet mix and interleaved store
    alignas(16) float dry_l[4], dry_r[4];
    for (int s = 0; s < 4; ++s) { dry_l[s] = in[s * 2]; dry_r[s] = in[s * 2 + 1]; }

    float32x4_t in_l4 = vld1q_f32(dry_l);
    float32x4_t in_r4 = vld1q_f32(dry_r);
    float32x4_t vmix  = vdupq_n_f32(mix_);
    float32x4_t vdry  = vdupq_n_f32(1.0f - mix_);

    float32x4_t mix_l = vmlaq_f32(vmulq_f32(in_l4, vdry), wet_l4, vmix);
    float32x4_t mix_r = vmlaq_f32(vmulq_f32(in_r4, vdry), wet_r4, vmix);

    alignas(16) float ol[4], or_[4];
    vst1q_f32(ol,  mix_l);
    vst1q_f32(or_, mix_r);
    for (int s = 0; s < 4; ++s) {
        out[s * 2]     = ol[s];
        out[s * 2 + 1] = or_[s];
    }
}

// ---------------------------------------------------------------------------
// render_scalar_frame — scalar fallback for the tail (0-3 frames)
// ---------------------------------------------------------------------------
void PercussionSpatializer::render_scalar_frame(const float* in, float* out) {
    delay_.push(in[0], in[1]);

    const float mag = 0.5f * (fabsf(in[0]) + fabsf(in[1]));
    if ((mag > prev_mag_ * 1.9f) && (mag > 0.002f)) randomize_hit();
    prev_mag_ = mag;

    const float two_pi     = 2.0f * 3.14159265f;
    const float frame_rate = rate_ * two_pi / (float)sample_rate_;
    const float safe_offset = (float)(delay_line_t::kLen * 8);

    float wet_l = 0.0f, wet_r = 0.0f;

    for (int k = 0; k < clone_count_; ++k) {
        clone_t& c = clones_[k];

        c.wobble_phase += frame_rate * c.wobble_rate_mul;
        if (c.wobble_phase >= two_pi) c.wobble_phase -= two_pi;

        float total_d = c.delay_samples
                      + fastersinfullf(c.wobble_phase) * c.wobble_depth_samples
                      + c.scatter_samples;
        if (total_d < 1.0f) total_d = 1.0f;

        const float raw_pos  = (float)(delay_.write - 1) - total_d;
        const float safe_pos = raw_pos + safe_offset;
        const int   base_int = (int)safe_pos;
        const float fr       = safe_pos - (float)base_int;
        const int   i0       = base_int & delay_line_t::kMask;
        const int   i1       = (i0 + 1) & delay_line_t::kMask;

        wet_l += (delay_.l[i0] + (delay_.l[i1] - delay_.l[i0]) * fr) * c.net_gain_l;
        wet_r += (delay_.r[i0] + (delay_.r[i1] - delay_.r[i0]) * fr) * c.net_gain_r;
    }

    out[0] = in[0] * (1.0f - mix_) + wet_l * mix_;
    out[1] = in[1] * (1.0f - mix_) + wet_r * mix_;
}

// ---------------------------------------------------------------------------
// Parameter accessors
// ---------------------------------------------------------------------------

void PercussionSpatializer::setParameter(uint8_t index, int32_t value) {
    if (index >= k_total) return;
    params_[index] = (int8_t)value;
    const float norm = clamp01((float)value * 0.01f);

    switch (index) {
        case k_clones:
            set_clone_count_index(value < 0 ? 0 : (value >= CLONE_SET_CNT ? CLONE_SET_CNT-1 : value));
            break;
        case k_mode:
            set_mode((spatial_mode_t)(value < 0 ? 0 : (value >= (int)MODE_COUNT ? (int)MODE_COUNT-1 : value)));
            break;
        case k_depth:            set_depth(norm);            break;
        case k_rate:             set_rate(norm);             break;
        case k_spread:           set_spread(norm);           break;
        case k_mix:              set_mix(norm);              break;
        case k_wobble:           set_wobble(norm);           break;
        case k_scatter:          set_scatter(norm);          break;
        case k_attack_softening: set_attack_softening(norm); break;
        default: break;
    }
}

int32_t PercussionSpatializer::getParameterValue(uint8_t index) const {
    if (index >= k_total) return 0;
    return params_[index];
}

const char* PercussionSpatializer::getParameterStrValue(uint8_t index, int32_t value) const {
    static const char* mode_names[]  = { "Tribale", "Militare", "Angeli" };
    static const char* clone_names[] = { "2cloni",  "4cloni",   "6cloni", "8cloni", "10cloni" };
    switch (index) {
        case k_mode:   if (value >= 0 && value < MODE_COUNT)    return mode_names[value];  break;
        case k_clones: if (value >= 0 && value < CLONE_SET_CNT) return clone_names[value]; break;
        default: break;
    }
    return nullptr;
}

const uint8_t* PercussionSpatializer::getParameterBmpValue(uint8_t, int32_t) const {
    return nullptr;
}

// ---------------------------------------------------------------------------
// Render — block4 path + scalar tail
// ---------------------------------------------------------------------------
void PercussionSpatializer::Render(const float* in, float* out, size_t frames) {
    if (!initialized_) {
        memset(out, 0, frames * 2 * sizeof(float));
        return;
    }

    size_t i = 0;
    for (; i + 3 < frames; i += 4)
        render_block4(in + i * 2, out + i * 2);
    for (; i < frames; ++i)
        render_scalar_frame(in + i * 2, out + i * 2);
}
