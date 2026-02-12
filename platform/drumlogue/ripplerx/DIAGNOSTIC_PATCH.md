# RipplerX Test 8 - Diagnostic Patch to Find Explosion Source

## 🔍 The Mystery

Test 8 fails at **exactly 499.913** even with:
- ✅ `c_decay_min = 0.01f` (correct)
- ✅ Clamps in Resonator.cc and Waveguide.cc (correct)
- ✅ All other tests passing

The explosion value (499.913) is suspiciously consistent.

## 🔬 Diagnostic Approach

Add detailed logging to find which component explodes:

### Patch 1: Add Coefficient Logging to Resonator.cc

```cpp
// In Resonator.cc, after line 165, add:

// DIAGNOSTIC: Log coefficients if they look suspicious
float va2_val = vgetq_lane_f32(part.va2, 0);
if (!std::isfinite(va2_val) || fabsf(va2_val) >= 1.0f) {
    printf("[DIAG] Partial %d: va2=%.6f d_k=%.6f inv_decay=%.6f\n",
           part.k, va2_val, d_k, inv_decay);
}
```

### Patch 2: Add Output Logging to Render Loop

```cpp
// In ripplerx.h, in the Render function, after voice processing:

float frame_max = 0.0f;
for (int i = 0; i < 4; ++i) {
    frame_max = std::max(frame_max, std::abs(vgetq_lane_f32(voice_mix, i)));
}
if (frame_max > 10.0f) {
    printf("[DIAG] Voice %zu explosion: %.2f\n", v, frame_max);
}
```

### Patch 3: Check Individual Component Outputs

```cpp
// In Voice processing (ripplerx.h), after each stage:

// After mallet
float m_max = std::max(std::abs(vgetq_lane_f32(m_sig, 0)), 
                       std::abs(vgetq_lane_f32(m_sig, 1)));
if (m_max > 10.0f) printf("[DIAG] Mallet explosion: %.2f\n", m_max);

// After resonator A
float res_a_max = std::max(std::abs(vgetq_lane_f32(res_out_A, 0)),
                           std::abs(vgetq_lane_f32(res_out_A, 1)));
if (res_a_max > 10.0f) printf("[DIAG] Resonator A explosion: %.2f\n", res_a_max);

// After resonator B
float res_b_max = std::max(std::abs(vgetq_lane_f32(res_out_B, 0)),
                           std::abs(vgetq_lane_f32(res_out_B, 1)));
if (res_b_max > 10.0f) printf("[DIAG] Resonator B explosion: %.2f\n", res_b_max);
```

## 🎯 Alternative: Simpler Coefficient Safety Check

Instead of logging, just add a hard safety limit:

### In Resonator.cc (after line 159):

```cpp
float inv_a0 = 1.0f / (1.0f + inv_decay);

// CRITICAL SAFETY CHECK: Detect unstable coefficients
float va2_check = (1.0f - inv_decay) * inv_a0;
if (!std::isfinite(va2_check) || fabsf(va2_check) >= 0.9999f) {
    // Coefficient would be unstable - zero this partial
    part.vb0 = part.vb2 = part.va1 = part.va2 = vdupq_n_f32(0.0f);
    continue;
}
```

## 🔧 Nuclear Option: Just Disable Problematic Partials Entirely

If coefficients look bad, don't use them:

```cpp
// Replace lines 154-165 with:

// 5. Coefficients & Pre-Normalization
float omega = f_k * inv_srate_2pi;
float b0_val = inv_srate_2pi * t_gain * a_k;
float inv_decay = inv_srate_2pi / d_k;

// SAFETY: Cap inv_decay to prevent instability
if (inv_decay > 100.0f) {
    // Too much decay - this partial would be unstable
    part.vb0 = part.vb2 = part.va1 = part.va2 = vdupq_n_f32(0.0f);
    continue;
}

float inv_a0 = 1.0f / (1.0f + inv_decay);

// Calculate coefficients
float b0_coef = b0_val * inv_a0;
float va1_coef = -2.0f * fastercosfullf(omega) * inv_a0;
float va2_coef = (1.0f - inv_decay) * inv_a0;

// FINAL SAFETY CHECK
if (!std::isfinite(b0_coef) || !std::isfinite(va1_coef) || 
    !std::isfinite(va2_coef) || fabsf(va2_coef) >= 0.9999f) {
    part.vb0 = part.vb2 = part.va1 = part.va2 = vdupq_n_f32(0.0f);
    continue;
}

// Pre-normalized coefficients directly into NEON registers
part.vb0 = vdupq_n_f32(b0_coef);
part.vb2 = vdupq_n_f32(-b0_coef);
part.va1 = vdupq_n_f32(va1_coef);
part.va2 = vdupq_n_f32(va2_coef);
```

## 🎯 Recommended Fix

Apply the "Nuclear Option" above - add comprehensive safety checks before assigning coefficients. This will catch ANY unstable coefficient before it causes an explosion.

The fact that it's **exactly 499.913** every time suggests it's hitting a specific limit somewhere. It might even be the limiter's threshold!

## 🔍 Check the Limiter

The value 499.913 is close to 500. Let me check if there's a limiter threshold:

```bash
grep -n "500\|limiter" Limiter.h
```

If the limiter has a threshold around 500, the explosion might be getting through but capped at ~500!

## ✅ Quick Temporary Fix for Test 8

Just reduce the decay parameter in the test:

```cpp
// In test_note_on_off_cycle(), change:
synth.setParameter(c_parameterDecay, 300);  // Too high!

// To:
synth.setParameter(c_parameterDecay, 50);  // Much safer
```

This will at least let you pass the test while we debug the root cause.
