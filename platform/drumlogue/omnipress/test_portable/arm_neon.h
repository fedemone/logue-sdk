#pragma once
/*
 * Minimal portable stand-in for <arm_neon.h>, sufficient to compile and run the
 * OmniPress DSP headers on x86-64 for offline measurement.
 *
 * Semantics match ARMv7 NEON except for vrecpeq_f32 / vrsqrteq_f32, which are
 * 8-bit estimates on real hardware.  Here they are computed exactly; every call
 * site in OmniPress follows the estimate with a Newton-Raphson step, which on
 * hardware lands at ~16-bit accuracy, so the difference is below -90 dBFS and
 * irrelevant for level/THD measurement.
 */

#include <stdint.h>
#include <string.h>
#include <math.h>

typedef float        float32x4_t __attribute__((vector_size(16)));
typedef float        float32x2_t __attribute__((vector_size(8)));
typedef uint32_t     uint32x4_t  __attribute__((vector_size(16)));
typedef int32_t      int32x4_t   __attribute__((vector_size(16)));

typedef struct { float32x4_t val[2]; } float32x4x2_t;
typedef struct { float32x4_t val[3]; } float32x4x3_t;
typedef struct { float32x4_t val[4]; } float32x4x4_t;

#define NSHIM static inline __attribute__((always_inline))

/* ---- reinterpret ---- */
NSHIM uint32x4_t  vreinterpretq_u32_f32(float32x4_t a){ uint32x4_t r; memcpy(&r,&a,16); return r; }
NSHIM float32x4_t vreinterpretq_f32_u32(uint32x4_t a) { float32x4_t r; memcpy(&r,&a,16); return r; }
NSHIM int32x4_t   vreinterpretq_s32_f32(float32x4_t a){ int32x4_t r;  memcpy(&r,&a,16); return r; }
NSHIM float32x4_t vreinterpretq_f32_s32(int32x4_t a)  { float32x4_t r; memcpy(&r,&a,16); return r; }
NSHIM uint32x4_t  vreinterpretq_u32_s32(int32x4_t a)  { uint32x4_t r; memcpy(&r,&a,16); return r; }
NSHIM int32x4_t   vreinterpretq_s32_u32(uint32x4_t a) { int32x4_t r;  memcpy(&r,&a,16); return r; }

/* ---- set / dup ---- */
NSHIM float32x4_t vdupq_n_f32(float v){ return (float32x4_t){v,v,v,v}; }
NSHIM uint32x4_t  vdupq_n_u32(uint32_t v){ return (uint32x4_t){v,v,v,v}; }
NSHIM int32x4_t   vdupq_n_s32(int32_t v){ return (int32x4_t){v,v,v,v}; }

/* ---- load / store ---- */
NSHIM float32x4_t vld1q_f32(const float* p){ float32x4_t r; memcpy(&r,p,16); return r; }
NSHIM void        vst1q_f32(float* p, float32x4_t v){ memcpy(p,&v,16); }
NSHIM float32x4_t vld1q_dup_f32(const float* p){ return vdupq_n_f32(*p); }

NSHIM float32x4x2_t vld2q_f32(const float* p){
    float32x4x2_t r;
    for (int i = 0; i < 4; ++i) { r.val[0][i] = p[2*i]; r.val[1][i] = p[2*i+1]; }
    return r;
}
NSHIM float32x4x4_t vld4q_f32(const float* p){
    float32x4x4_t r;
    for (int i = 0; i < 4; ++i)
        for (int c = 0; c < 4; ++c) r.val[c][i] = p[4*i+c];
    return r;
}
NSHIM void vst2q_f32(float* p, float32x4x2_t v){
    for (int i = 0; i < 4; ++i) { p[2*i] = v.val[0][i]; p[2*i+1] = v.val[1][i]; }
}

/* ---- lane access ---- */
#define vgetq_lane_f32(v, i) ((v)[(i)])
#define vget_lane_f32(v, i)  ((v)[(i)])
NSHIM float32x2_t vget_low_f32(float32x4_t a){ return (float32x2_t){a[0], a[1]}; }
NSHIM float32x2_t vget_high_f32(float32x4_t a){ return (float32x2_t){a[2], a[3]}; }
NSHIM float32x2_t vadd_f32(float32x2_t a, float32x2_t b){ return a + b; }

/* ---- float arithmetic ---- */
NSHIM float32x4_t vaddq_f32(float32x4_t a, float32x4_t b){ return a + b; }
NSHIM float32x4_t vsubq_f32(float32x4_t a, float32x4_t b){ return a - b; }
NSHIM float32x4_t vmulq_f32(float32x4_t a, float32x4_t b){ return a * b; }
NSHIM float32x4_t vmulq_n_f32(float32x4_t a, float s){ return a * vdupq_n_f32(s); }
NSHIM float32x4_t vmlaq_f32(float32x4_t a, float32x4_t b, float32x4_t c){ return a + b * c; }
NSHIM float32x4_t vnegq_f32(float32x4_t a){ return -a; }
NSHIM float32x4_t vabsq_f32(float32x4_t a){
    float32x4_t r; for (int i=0;i<4;++i) r[i] = fabsf(a[i]); return r;
}
NSHIM float32x4_t vmaxq_f32(float32x4_t a, float32x4_t b){
    float32x4_t r; for (int i=0;i<4;++i) r[i] = a[i] > b[i] ? a[i] : b[i]; return r;
}
NSHIM float32x4_t vminq_f32(float32x4_t a, float32x4_t b){
    float32x4_t r; for (int i=0;i<4;++i) r[i] = a[i] < b[i] ? a[i] : b[i]; return r;
}

/* ---- reciprocal / rsqrt (exact; see header comment) ---- */
NSHIM float32x4_t vrecpeq_f32(float32x4_t a){
    float32x4_t r; for (int i=0;i<4;++i) r[i] = 1.0f / a[i]; return r;
}
NSHIM float32x4_t vrecpsq_f32(float32x4_t a, float32x4_t b){
    return vdupq_n_f32(2.0f) - a * b;
}
NSHIM float32x4_t vrsqrteq_f32(float32x4_t a){
    float32x4_t r; for (int i=0;i<4;++i) r[i] = 1.0f / sqrtf(a[i]); return r;
}
NSHIM float32x4_t vrsqrtsq_f32(float32x4_t a, float32x4_t b){
    return (vdupq_n_f32(3.0f) - a * b) * vdupq_n_f32(0.5f);
}

/* ---- integer arithmetic ---- */
NSHIM int32x4_t  vaddq_s32(int32x4_t a, int32x4_t b){ return a + b; }
NSHIM int32x4_t  vsubq_s32(int32x4_t a, int32x4_t b){ return a - b; }
NSHIM uint32x4_t vaddq_u32(uint32x4_t a, uint32x4_t b){ return a + b; }
NSHIM uint32x4_t vsubq_u32(uint32x4_t a, uint32x4_t b){ return a - b; }

/* ---- bitwise ---- */
NSHIM uint32x4_t vandq_u32(uint32x4_t a, uint32x4_t b){ return a & b; }
NSHIM uint32x4_t vorrq_u32(uint32x4_t a, uint32x4_t b){ return a | b; }
NSHIM uint32x4_t veorq_u32(uint32x4_t a, uint32x4_t b){ return a ^ b; }
NSHIM int32x4_t  vandq_s32(int32x4_t a, int32x4_t b){ return a & b; }
NSHIM int32x4_t  vorrq_s32(int32x4_t a, int32x4_t b){ return a | b; }

#define vshlq_n_s32(a, n) ((a) << (n))
#define vshrq_n_s32(a, n) ((a) >> (n))
#define vshrq_n_u32(a, n) ((a) >> (n))

/* ---- compare (result: all-ones / all-zeros lanes) ---- */
NSHIM uint32x4_t vcgtq_f32(float32x4_t a, float32x4_t b){
    uint32x4_t r; for (int i=0;i<4;++i) r[i] = a[i] >  b[i] ? 0xFFFFFFFFu : 0u; return r;
}
NSHIM uint32x4_t vcltq_f32(float32x4_t a, float32x4_t b){
    uint32x4_t r; for (int i=0;i<4;++i) r[i] = a[i] <  b[i] ? 0xFFFFFFFFu : 0u; return r;
}
NSHIM uint32x4_t vcleq_f32(float32x4_t a, float32x4_t b){
    uint32x4_t r; for (int i=0;i<4;++i) r[i] = a[i] <= b[i] ? 0xFFFFFFFFu : 0u; return r;
}
NSHIM uint32x4_t vcgeq_f32(float32x4_t a, float32x4_t b){
    uint32x4_t r; for (int i=0;i<4;++i) r[i] = a[i] >= b[i] ? 0xFFFFFFFFu : 0u; return r;
}
NSHIM uint32x4_t vcgtq_u32(uint32x4_t a, uint32x4_t b){
    uint32x4_t r; for (int i=0;i<4;++i) r[i] = a[i] >  b[i] ? 0xFFFFFFFFu : 0u; return r;
}
NSHIM uint32x4_t vtstq_u32(uint32x4_t a, uint32x4_t b){
    uint32x4_t r; for (int i=0;i<4;++i) r[i] = (a[i] & b[i]) ? 0xFFFFFFFFu : 0u; return r;
}

/* ---- bit select ---- */
NSHIM uint32x4_t vbslq_u32(uint32x4_t m, uint32x4_t a, uint32x4_t b){
    return (m & a) | (~m & b);
}
NSHIM float32x4_t vbslq_f32(uint32x4_t m, float32x4_t a, float32x4_t b){
    return vreinterpretq_f32_u32(vbslq_u32(m, vreinterpretq_u32_f32(a),
                                              vreinterpretq_u32_f32(b)));
}

/* ---- conversions ---- */
NSHIM int32x4_t   vcvtq_s32_f32(float32x4_t a){
    int32x4_t r; for (int i=0;i<4;++i) r[i] = (int32_t)a[i]; return r;   /* truncate toward zero */
}
NSHIM uint32x4_t  vcvtq_u32_f32(float32x4_t a){
    uint32x4_t r; for (int i=0;i<4;++i) r[i] = (uint32_t)a[i]; return r;
}
NSHIM float32x4_t vcvtq_f32_s32(int32x4_t a){
    float32x4_t r; for (int i=0;i<4;++i) r[i] = (float)a[i]; return r;
}
NSHIM float32x4_t vcvtq_f32_u32(uint32x4_t a){
    float32x4_t r; for (int i=0;i<4;++i) r[i] = (float)a[i]; return r;
}

#undef NSHIM
