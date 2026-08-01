#pragma once
// Minimal scalar stand-in for <arm_neon.h> so the real drumlogue DSP header can
// be compiled and measured on an x86 host. Semantics match the ARMv7 NEON
// intrinsics used by NeonAdvancedLabirinto.h.
#include <cstdint>
#include <cmath>

struct float32x4_t { float v[4]; };
struct float32x2_t { float v[2]; };
struct uint32x4_t  { uint32_t v[4]; };
struct int32x4_t   { int32_t  v[4]; };
struct float32x4x2_t { float32x4_t val[2]; };
struct float32x4x4_t { float32x4_t val[4]; };

static inline float32x4_t vld1q_f32(const float* p) { float32x4_t r; for (int i=0;i<4;i++) r.v[i]=p[i]; return r; }
static inline void vst1q_f32(float* p, float32x4_t a) { for (int i=0;i<4;i++) p[i]=a.v[i]; }
static inline float32x4_t vdupq_n_f32(float x) { float32x4_t r; for (int i=0;i<4;i++) r.v[i]=x; return r; }
static inline float32x4_t vaddq_f32(float32x4_t a, float32x4_t b) { float32x4_t r; for (int i=0;i<4;i++) r.v[i]=a.v[i]+b.v[i]; return r; }
static inline float32x4_t vsubq_f32(float32x4_t a, float32x4_t b) { float32x4_t r; for (int i=0;i<4;i++) r.v[i]=a.v[i]-b.v[i]; return r; }
static inline float32x4_t vmulq_f32(float32x4_t a, float32x4_t b) { float32x4_t r; for (int i=0;i<4;i++) r.v[i]=a.v[i]*b.v[i]; return r; }
static inline float32x4_t vmulq_n_f32(float32x4_t a, float n) { float32x4_t r; for (int i=0;i<4;i++) r.v[i]=a.v[i]*n; return r; }
static inline float32x4_t vmlaq_f32(float32x4_t a, float32x4_t b, float32x4_t c) { float32x4_t r; for (int i=0;i<4;i++) r.v[i]=a.v[i]+b.v[i]*c.v[i]; return r; }
static inline float32x4_t vmlaq_n_f32(float32x4_t a, float32x4_t b, float n) { float32x4_t r; for (int i=0;i<4;i++) r.v[i]=a.v[i]+b.v[i]*n; return r; }
static inline float32x4_t vmlsq_n_f32(float32x4_t a, float32x4_t b, float n) { float32x4_t r; for (int i=0;i<4;i++) r.v[i]=a.v[i]-b.v[i]*n; return r; }
static inline float32x4_t vabsq_f32(float32x4_t a) { float32x4_t r; for (int i=0;i<4;i++) r.v[i]=std::fabs(a.v[i]); return r; }
static inline float32x4_t vmaxq_f32(float32x4_t a, float32x4_t b) { float32x4_t r; for (int i=0;i<4;i++) r.v[i]=a.v[i]>b.v[i]?a.v[i]:b.v[i]; return r; }
static inline float32x4_t vminq_f32(float32x4_t a, float32x4_t b) { float32x4_t r; for (int i=0;i<4;i++) r.v[i]=a.v[i]<b.v[i]?a.v[i]:b.v[i]; return r; }
static inline float32x4_t vnegq_f32(float32x4_t a) { float32x4_t r; for (int i=0;i<4;i++) r.v[i]=-a.v[i]; return r; }
static inline float32x4_t vextq_f32(float32x4_t a, float32x4_t b, int n) {
    float32x4_t r; for (int i=0;i<4;i++) { int j=i+n; r.v[i] = (j<4)? a.v[j] : b.v[j-4]; } return r;
}
static inline float vgetq_lane_f32(float32x4_t a, int n) { return a.v[n]; }
static inline float32x4_t vsetq_lane_f32(float x, float32x4_t a, int n) { a.v[n]=x; return a; }
static inline uint32x4_t vcltq_f32(float32x4_t a, float32x4_t b) { uint32x4_t r; for (int i=0;i<4;i++) r.v[i]= (a.v[i]<b.v[i])?0xFFFFFFFFu:0u; return r; }
static inline uint32x4_t vcgtq_f32(float32x4_t a, float32x4_t b) { uint32x4_t r; for (int i=0;i<4;i++) r.v[i]= (a.v[i]>b.v[i])?0xFFFFFFFFu:0u; return r; }
static inline float32x4_t vbslq_f32(uint32x4_t m, float32x4_t a, float32x4_t b) { float32x4_t r; for (int i=0;i<4;i++) r.v[i]= m.v[i]?a.v[i]:b.v[i]; return r; }
static inline int32x4_t vcvtq_s32_f32(float32x4_t a) { int32x4_t r; for (int i=0;i<4;i++) r.v[i]=(int32_t)a.v[i]; return r; }
static inline float32x4_t vcvtq_f32_s32(int32x4_t a) { float32x4_t r; for (int i=0;i<4;i++) r.v[i]=(float)a.v[i]; return r; }
static inline float32x4_t vrecpeq_f32(float32x4_t a) { float32x4_t r; for (int i=0;i<4;i++) r.v[i]=1.0f/a.v[i]; return r; }
static inline float32x4_t vrecpsq_f32(float32x4_t a, float32x4_t b) { float32x4_t r; for (int i=0;i<4;i++) r.v[i]=2.0f-a.v[i]*b.v[i]; return r; }
static inline float32x4_t vrsqrteq_f32(float32x4_t a) { float32x4_t r; for (int i=0;i<4;i++) r.v[i]=1.0f/std::sqrt(a.v[i]); return r; }
static inline float32x4_t vrsqrtsq_f32(float32x4_t a, float32x4_t b) { float32x4_t r; for (int i=0;i<4;i++) r.v[i]=(3.0f-a.v[i]*b.v[i])*0.5f; return r; }
static inline float32x4x2_t vtrnq_f32(float32x4_t a, float32x4_t b) {
    float32x4x2_t r;
    r.val[0].v[0]=a.v[0]; r.val[0].v[1]=b.v[0]; r.val[0].v[2]=a.v[2]; r.val[0].v[3]=b.v[2];
    r.val[1].v[0]=a.v[1]; r.val[1].v[1]=b.v[1]; r.val[1].v[2]=a.v[3]; r.val[1].v[3]=b.v[3];
    return r;
}
static inline float32x2_t vget_low_f32(float32x4_t a)  { float32x2_t r; r.v[0]=a.v[0]; r.v[1]=a.v[1]; return r; }
static inline float32x2_t vget_high_f32(float32x4_t a) { float32x2_t r; r.v[0]=a.v[2]; r.v[1]=a.v[3]; return r; }
static inline float32x4_t vcombine_f32(float32x2_t a, float32x2_t b) { float32x4_t r; r.v[0]=a.v[0]; r.v[1]=a.v[1]; r.v[2]=b.v[0]; r.v[3]=b.v[1]; return r; }
static inline float32x4x4_t vld4q_f32(const float* p) {
    float32x4x4_t r; for (int i=0;i<4;i++) for (int j=0;j<4;j++) r.val[i].v[j]=p[j*4+i]; return r;
}
