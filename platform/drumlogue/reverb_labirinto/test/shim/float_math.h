#pragma once
// Host stand-in for the SDK's float_math.h. The unit only pulls six symbols
// from it; here they are exact libm versions (the on-target fast approximations
// differ only in accuracy, not in structure).
#include <cmath>
#include <arm_neon.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#ifndef M_TWOPI
#define M_TWOPI (2.0f * 3.14159265358979323846f)
#endif

static inline float e_expff(float x)            { return expf(x); }
static inline float fasterpowf(float a, float b){ return powf(a, b); }
static inline float fastercosfullf(float x)     { return cosf(x); }
static inline float fastersinfullf(float x)     { return sinf(x); }
static inline float fasterSqrt_15bits(float x)  { return sqrtf(x); }

static inline float32x4_t sin_ps(float32x4_t x) {
    float32x4_t r;
    for (int i = 0; i < 4; i++) r.v[i] = sinf(x.v[i]);
    return r;
}
