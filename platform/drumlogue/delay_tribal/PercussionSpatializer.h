#pragma once
/**
 * @file PercussionSpatializer.h
 * @brief Percussion micro-ensemble effect for Korg Drumlogue.
 *
 * This version explicitly separates the three spatial concepts:
 * - clone placement
 * - pan law
 * - stereo scatter
 *
 * The goal is to make the effect feel like multiple players rather than
 * a chorus or a smeared delay cloud.
 */

#include <arm_neon.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

#include "unit.h"
#include "spatial_modes.h"

#ifndef fast_inline
#define fast_inline inline __attribute__((always_inline))
#endif

extern float lfo_table[LFO_TABLE_SIZE];

struct delay_line_t {
    std::vector<float> l;
    std::vector<float> r;
    size_t size = 0;
    size_t write = 0;

    void init(size_t n) {
        size = n;
        l.assign(n, 0.0f);
        r.assign(n, 0.0f);
        write = 0;
    }

    void clear() {
        std::fill(l.begin(), l.end(), 0.0f);
        std::fill(r.begin(), r.end(), 0.0f);
        write = 0;
    }

    fast_inline void push(float in_l, float in_r) {
        l[write] = in_l;
        r[write] = in_r;
        ++write;
        if (write >= size) write = 0;
    }

    fast_inline void read_delay(float delay_samples, float& out_l, float& out_r) const {
        if (size == 0) {
            out_l = out_r = 0.0f;
            return;
        }

        float pos = (float)write - delay_samples;
        while (pos < 0.0f) pos += (float)size;
        while (pos >= (float)size) pos -= (float)size;

        const size_t i0 = (size_t)pos;
        const size_t i1 = (i0 + 1) % size;
        const float frac = pos - (float)i0;

        out_l = l[i0] + (l[i1] - l[i0]) * frac;
        out_r = r[i0] + (r[i1] - r[i0]) * frac;
    }
};

typedef struct {
    float delay_ms;
    float gain;
    float pan_x;
    float pan_l;
    float pan_r;
    float hp_coeff;
    float lp_coeff;
    float wobble_phase;
    float wobble_depth_ms;
    float jitter_ms;
    bool active;
} clone_t;

enum params {
    k_clones = 0,              // 2..6 effective clones
    k_mode,                    // Tribal / Military / Angel
    k_depth,                   // spread between arrivals
    k_rate,                    // wobble rate
    k_spread,                  // stereo spread
    k_mix,                     // wet/dry
    k_wobble,                  // detune / timing wobble depth
    k_attack_softening,        // soften followers
    k_total,
};

class PercussionSpatializer {
public:
    PercussionSpatializer();
    ~PercussionSpatializer() = default;

    inline int8_t Init(const unit_runtime_desc_t* desc);
    inline void Teardown();
    inline void Reset();
    inline void Resume() {}
    inline void Suspend() {}

    inline void setParameter(uint8_t index, int32_t value);
    inline int32_t getParameterValue(uint8_t index) const;
    inline const char* getParameterStrValue(uint8_t index, int32_t value) const;
    inline const uint8_t* getParameterBmpValue(uint8_t index, int32_t value) const;

    inline void Render(const float* in, float* out, size_t frames);

private:
    static constexpr int kMaxClones = CLONE_MAX;
    static constexpr int kCrossfadeSamples = 128;

    void rebuild_profile();
    void randomize_hit();
    void set_clone_count(int value);
    void set_mode(spatial_mode_t mode);
    void set_depth(float norm);
    void set_rate(float norm);
    void set_spread(float norm);
    void set_mix(float norm);
    void set_wobble(float norm);
    void set_attack_softening(float norm);

    float process_one(float in_l, float in_r, float& out_l, float& out_r);

    static fast_inline float clamp01(float x) { return x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x); }
    static fast_inline float eqpow_l(float x) { return fastercosfullf(x * 1.57079632679f); }
    static fast_inline float eqpow_r(float x) { return fastersinfullf(x * 1.57079632679f); }

private:
    delay_line_t delay_;
    size_t sample_rate_ = 48000;
    bool initialized_ = false;
    bool is_mono_ = false;

    spatial_mode_t mode_ = MODE_TRIBAL;
    int clone_count_ = CLONE_DEFAULT;

    int8_t params_[k_total] = {};
    int8_t last_params_[k_total] = {};

    float depth_ = 0.50f;
    float rate_ = 1.00f;
    float spread_ = 0.80f;
    float mix_ = 0.35f;
    float wobble_ = 0.25f;
    float soft_atk_ = 0.20f;

    clone_t clones_[kMaxClones]{};
    spatial_profile_t profile_{};

    float state_l_ = 0.0f;
    float state_r_ = 0.0f;

    uint32_t rng_state_ = 0x9E3779B9u;
    float hit_jitter_ms_ = 0.0f;
};
