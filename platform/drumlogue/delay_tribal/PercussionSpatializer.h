#pragma once
/**
 * PercussionSpatializer.h
 *
 * Percussion micro-ensemble effect for Korg Drumlogue.
 *
 * CPU optimisations (vs. original):
 *  - Power-of-2 delay buffer (8192 samples) → bitmask wrap, no while loops.
 *  - All hp/lp filter attenuations and pan values baked into net_gain at
 *    rebuild time; zero filter arithmetic in the render loop.
 *  - Scatter jitter pre-computed per-hit in randomize_hit(); no xorshift
 *    or division inside the render loop.
 *  - Wobble phase advanced once per 4-frame block (not per sample).
 *  - Block processing: render_block4() processes 4 frames with NEON linear
 *    interpolation across time (5 scalar reads → vld1q + vmlaq per clone).
 *  - Smoothing (advance_smoothing) never triggers rebuild_profile().
 *    Gains/wobble depths update cheaply via update_clone_dynamics().
 *    One full rebuild fires at the very end of each smoothing window.
 */

#include <arm_neon.h>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "constants.h"
#include "unit.h"
#include "spatial_modes.h"
#include "float_math.h"

#ifndef fast_inline
#define fast_inline inline __attribute__((always_inline))
#endif

// ---------------------------------------------------------------------------
// Circular delay line — power-of-2 size, bitmask addressing
// ---------------------------------------------------------------------------
struct delay_line_t {
    static constexpr int kLen  = 8192;   // 2^13 ≈ 170 ms @ 48 kHz
    static constexpr int kMask = kLen - 1;

    float l[kLen];
    float r[kLen];
    int   write = 0;

    void clear() { memset(l, 0, sizeof(l)); memset(r, 0, sizeof(r)); write = 0; }

    fast_inline void push(float in_l, float in_r) {
        l[write] = in_l;
        r[write] = in_r;
        write = (write + 1) & kMask;
    }

    // Read 5 consecutive (wrapped) samples starting at base.
    // render_block4 interpolates 4 outputs from this window.
    fast_inline void read5(int base, float* sl, float* sr) const {
        for (int s = 0; s < 5; ++s) {
            const int idx = (base + s) & kMask;
            sl[s] = l[idx];
            sr[s] = r[idx];
        }
    }
};

// ---------------------------------------------------------------------------
// Clone descriptor — all values pre-computed, nothing expensive at render time
// ---------------------------------------------------------------------------
typedef struct {
    float delay_samples;        // base delay (samples)
    float wobble_depth_samples; // LFO modulation amplitude (samples)
    float scatter_samples;      // per-hit timing offset (samples)
    float pan_gain_l;           // pan_l × hp_attn × lp_attn  (constant per rebuild)
    float pan_gain_r;           // pan_r × hp_attn × lp_attn
    float base_gain;            // clone gain × detachment factor (constant per rebuild)
    float net_gain_l;           // base_gain × soft_factor × pan_gain_l (updated per block)
    float net_gain_r;           // base_gain × soft_factor × pan_gain_r
    float wobble_phase;         // LFO phase [0..2π), advanced per block
    float wobble_rate_mul;      // 0.70..1.00 — staggers each clone's LFO so they drift apart
} clone_t;

// ---------------------------------------------------------------------------
// Parameter IDs (must match header.c)
// ---------------------------------------------------------------------------
enum params {
    k_clones = 0,
    k_mode,
    k_depth,
    k_rate,
    k_spread,
    k_mix,
    k_wobble,
    k_scatter,
    k_attack_softening,
    k_total,
};

// ---------------------------------------------------------------------------
// Main class
// ---------------------------------------------------------------------------
class PercussionSpatializer {
public:
    PercussionSpatializer();
    ~PercussionSpatializer() = default;

    int8_t Init(const unit_runtime_desc_t* desc);
    void   Teardown();
    void   Reset();
    void   Resume()  {}
    void   Suspend() {}

    void         setParameter(uint8_t index, int32_t value);
    int32_t      getParameterValue(uint8_t index) const;
    const char*  getParameterStrValue(uint8_t index, int32_t value) const;
    const uint8_t* getParameterBmpValue(uint8_t index, int32_t value) const;

    void Render(const float* in, float* out, size_t frames);

private:
    static constexpr int      kMaxClones    = MAX_CLONES;
    // 120 blocks × 4 frames = 480 samples ≈ 10 ms @ 48 kHz
    static constexpr uint32_t kSmoothBlocks = 120;

    // Full spatial rebuild (pan geometry, filter baking, delay in samples).
    // Expensive: calls fasterpowf × N + my_sqrt_f × N.  Called rarely.
    void rebuild_profile();

    // Per-transient: randomise scatter_samples and wobble phases.
    // Replaces per-sample xorshift in the old render loop.
    void randomize_hit();

    // Per-block cheap update: recomputes net_gain and wobble_depth only.
    // No pow/sqrt — just arithmetic.  Called every block during smoothing.
    void update_clone_dynamics();

    void set_clone_count_index(int idx);
    void set_mode(spatial_mode_t mode);
    void set_depth(float norm);
    void set_rate(float norm);
    void set_spread(float norm);
    void set_mix(float norm);
    void set_wobble(float norm);
    void set_scatter(float norm);
    void set_attack_softening(float norm);

    void advance_smoothing();

    void render_block4(const float* in, float* out);      // 4 frames, NEON
    void render_scalar_frame(const float* in, float* out); // 1 frame, scalar fallback

    static fast_inline float clamp01(float x) { return x < 0.f ? 0.f : (x > 1.f ? 1.f : x); }

    // ---- state ----
    delay_line_t   delay_;
    uint32_t       sample_rate_ = 48000;
    bool           initialized_ = false;

    spatial_mode_t mode_           = MODE_TRIBAL;
    int            clone_set_index_ = CLONE_SET_4;
    int            clone_count_     = 4;

    int8_t params_[k_total] = {};

    float depth_  = 0.50f;
    float spread_ = 0.80f;

    float rate_     = 1.00f;  float rate_target_     = 1.00f;
    float mix_      = 0.35f;  float mix_target_       = 0.35f;
    float wobble_   = 0.25f;  float wobble_target_    = 0.25f;
    float scatter_  = 0.20f;  float scatter_target_   = 0.20f;
    float soft_atk_ = 0.20f;  float soft_atk_target_  = 0.20f;

    uint32_t smoothing_remaining_ = 0;

    clone_t          clones_[kMaxClones]{};
    spatial_profile_t profile_{};

    uint32_t rng_state_              = 0x9E3779B9u;
    bool     pending_profile_rebuild_ = true;
    float    prev_mag_                = 0.0f;
};

// ---- NEON horizontal-sum helpers (used by mix helpers if needed) -----------
static fast_inline float horizontal_sum4(float32x4_t v) {
#if defined(__aarch64__)
    return vaddvq_f32(v);
#else
    float32x2_t s = vpadd_f32(vget_low_f32(v), vget_high_f32(v));
    s = vpadd_f32(s, s);
    return vget_lane_f32(s, 0);
#endif
}
