// Copyright (C) 2025 tilr
// Comb stereoizer
#pragma once
#include "float_math.h"
#include "constants.h"
// Removed <vector> to avoid potential static initialization issues


class Comb
{
public:
  Comb() : pos(0), buf_size(0) {}
  ~Comb() = default;

  void init(float32_t srate)
  {
    pos = 0;
    buf_size = (int)(20 * srate / 1000);
    if (buf_size > kMaxBufSize) buf_size = kMaxBufSize;

    // Clear buffer
    float32x2_t zero = vdup_n_f32(0.0f);
    for(int i=0; i<buf_size; ++i) {
      buf[i] = zero;
    }

    // Precompute stereoizer constant: [+0.165, -0.165]
    const float stereo_vals[2] = {0.165f, -0.165f};
    stereoizer = vld1_f32(stereo_vals);
  }

  // comb filter gets in input a sum of stereo samples, stores them in a circular buffer,
  // and outputs a stereoized version of the input by adding and subtracting a delayed version
  // this version uses NEON intrinsics, and uses either a two stereo samples, or
  // four mono one. They will treated in same way, doing a sum and a difference.
    float32x4_t process(float32x4_t input) {
        // Sum the high and low stereo pairs
        float32x2_t input_sum = vadd_f32(vget_high_f32(input), vget_low_f32(input));

        // Read delayed value from current position (old data)
        float32x2_t delayed = buf[pos];

        // Store new sum in circular buffer
        buf[pos] = input_sum;

        // Increment to next position (faster than modulo)
        if (++pos >= buf_size) pos = 0;

        // Apply stereoizer effect: [delayed * 0.165, delayed * -0.165]
        float32x2_t stereo_effect = vmul_f32(delayed, stereoizer);

        // Broadcast stereo effect to both channels and add to input
        return vaddq_f32(input, vcombine_f32(stereo_effect, stereo_effect));
    }

private:
    int pos = 0;
    int buf_size = 0;
    float32x2_t stereoizer;
    float32x2_t buf[kMaxBufSize]{};
};