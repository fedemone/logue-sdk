#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdbool>
#include <cstring>
#include <cmath>
#include <type_traits>

#include <arm_neon.h>

#include "unit.h"
#include "constants.h"

class Filter {
public:
  enum Type : uint8_t {
    None = 0,
    LP12 = 1,
    HP12 = 2,
    BP6 = 3,
    LP6 = 4,
    HP6 = 5,
    AP1 = 6,
  };
};

class BiQuadFilter : public Filter {
public:
  BiQuadFilter() {
    // Clear state.
    memset(m_z1, 0, 2 * sizeof(float));
    memset(m_z2, 0, 2 * sizeof(float));
  }
  ~BiQuadFilter() {}

  void init(Type type, float cutoff, float q) {
    // cutoff = fmin(cutoff, c_sampleRate / 2.0); // Nyquist limit.
    switch(type) {
    case None:
      m_b0 = 1.0f;
      m_b1 = m_b2 = m_a1 = m_a2 = 0.0f;
      break;
    case LP12:
      {
      float w0 = get_w0(cutoff);
      float b0 = q * w0 * w0;
      float a0_recip = 1.0f / (b0 + w0 + q);
      m_b0 = m_b2 = b0 * a0_recip;
      m_b1 = 2.0f * m_b0;
      m_a1 = 2.0f * (b0 - q) * a0_recip;
      m_a2 = (b0 - w0 + q) * a0_recip;
      break;
      }
    case HP12:
      {
      float w0 = get_w0(cutoff);
      float b0 = q * w0 * w0;
      float a0_recip = 1.0f / (b0 + w0 + q);
      m_b0 = m_b2 = q * a0_recip;
      m_b1 = -2.0f * m_b0;
      m_a1 = 2.0f * (b0 - q) * a0_recip;
      m_a2 = (b0 - w0 + q) * a0_recip;
      break;
      }
    case BP6:
      {
      float w0 = get_w0(cutoff);
      float b0 = q * w0 * w0;
      float a0_recip = 1.0f / (b0 + w0 + q);
      m_b0 = w0 * a0_recip;
      m_b1 = 0.0f;
      m_b2 = -m_b0;
      m_a1 = 2.0f * (b0 - q) * a0_recip;
      m_a2 = (b0 - w0 + q) * a0_recip;
      break;
      }
    case LP6:
      {
      float w0 = get_w0(cutoff);
      m_b0 = m_b1 = w0 / (w0 + 1.0f);
      m_a1 = (w0 - 1.0f) / (w0 + 1.0f);
      m_b2 = m_a2 = 0.0f;
      (void) q;
      break;
      }
    case HP6:
      {
      float w0 = get_w0(cutoff);
      m_b0 = 1.0f / (w0 + 1.0f);
      m_b1 = -m_b0;
      m_a1 = (w0 - 1.0f) / (w0 + 1.0f);
      m_b2 = m_a2 = 0.0f;
      (void) q;
      break;
      }
    case AP1:
      {
      float w0 = get_w0(cutoff);
      m_b0 = m_a1 = (w0 - 1.0f)/(w0 + 1.0f);
      m_b1 = 1.0f;
      m_b2 = m_a2 = 0.0f;
      (void) q;
      break;
      }
    }
  }

  void setCoefficients(float b0, float b1, float b2, float a1, float a2) {
    m_b0 = b0;
    m_b1 = b1;
    m_b2 = b2;
    m_a1 = a1;
    m_a2 = a2;
  }


  float32x2_t process(float32x2_t in) {
    float32x2_t z1 = vld1_f32(m_z1);
    float32x2_t z2 = vld1_f32(m_z2);
    float32x2_t result = vmla_n_f32(z1, in, m_b0);
    z1 = vmla_n_f32(z2, in, m_b1);
    z2 = vmul_n_f32(in, m_b2);
    vst1_f32(m_z1, vmls_n_f32(z1, result, m_a1));
    vst1_f32(m_z2, vmls_n_f32(z2, result, m_a2));
    return result;
  }

private:
  inline float get_w0(float cutoff) {
    return tan((M_PI * cutoff) / c_sampleRate);
  }
  
public:
  // Coefficients
  float m_b0, m_b1, m_b2;
  float m_a1, m_a2;

private:
  // State
  float m_z1[2];
  float m_z2[2];

};

