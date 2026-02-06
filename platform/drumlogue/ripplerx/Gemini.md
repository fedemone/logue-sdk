
# RipplerX Project Context & Optimization Log

## Project Overview

RipplerX is a physically modeled polyphonic synthesizer for ARMv7 (Korg Drumlogue). 
It uses a combination of Modal Synthesis (Resonators with partials) and Waveguide Synthesis (Strings/Tubes) excited by Mallets and Noise.

## Core Technical Constraints

- Platform: ARMv7 (Cortex-A7/A9) with NEON SIMD support.
- Math Strategy: Use bit-trick approximations for exp, pow, and log to save cycles. Avoid std::pow and std::exp.
- Vectorization: Processing is done in batches of 4 samples (float32x4_t) to match NEON register width.

### 1. Global Optimization PrimitivesUse these bit-trick functions defined in float_math.h for all DSP components:
- Fast 2^p Approximation:

>   C++
>
>   u.i = (int32_t)(p * 8388608.0f) + 1065353216; // float-to-int cast trick
- Fast Inverse Square Root: vrsqrteq_f32 (NEON intrinsic).
- Branchless Logic: Use vbslq_f32 (Bitwise Select) for attack/release and conditional logic.

### 2. Component Status & Specs

#### **Filter (Organic Mode)**
- Saturation: Includes a cubic soft-clipper x - (0.1481f * x * x * x) to simulate analog warmth.
- Coefficients: Pre-calculated with fastcosfullf and fastersinfullf.

#### **Envelope (ADSR)**
- Recursive Form: env = b + env * c.
- Optimization: Replaced pow(x, 3.0) with x*x*x in calcCoefs.

#### **Limiter (FairlyChildish)**
- Parallel Processing: Fully vectorized. No scalar transitions in the hot path.
- Attack/Release: Branchless selection using NEON masks.

#### **Comb (Stereoizer)**
- Delay Line: 20ms buffer.
- Implementation: Adds/Subtracts delayed signal with phase inversion to create mono-compatible width.

#### **Voice & Resonator**
- SIMD Handoff: Voices process 4 samples. Resonators iterate through activePartialsCount.
- Frequency Coupling: $O(N^2)$ complexity optimized via Broadcast SIMD. 4 'A' partials are compared against 1 'B' partial (duplicated across lanes) to calculate frequency repulsion.
- Active Partial Counting (APC): To be fully implemented yet. The update() function counts how many partials are below Nyquist and above the minimum decay threshold. The process() loop only iterates through these active indices. Here are some details:

The process loop inside Resonator is now the heaviest part of your engine. Since Partial::process is inlined, the compiler will likely unroll the for loop.
If you find that 64 partials are too heavy for your specific CPU, you can implement a "Dynamic Partials" optimization:
Inside Resonator::update, count how many partials actually produce sound (aren't muted by the range check).
Store that count in activePartials.
Change the process loop to only run up to activePartials.
This prevents the CPU from doing math on partials that are above the Nyquist frequency or have zero decay.

Updated Resonator Implementation for Active Counting
 * 1. Update the Header (Resonator.h)
Add a member variable to track the number of audible partials.

> C++
>
>   private:
>       int activePartialsCount = 0; // Number of partials that passed the range check
 * 2. Update the Logic (Resonator.cpp)
We modify update() to sort or simply count the "live" partials. Since the partials are typically ordered by frequency (k=1, 2, 3...), we can often just find the first partial that hits the Nyquist limit and stop there.

> C++
>
>   void Resonator::update(float32_t freq, float32_t vel, bool isRelease, float32_t model[c_max_partials])
>   {
>       if (nmodel >= OpenTube) {
>           waveguide.update(model[0] * freq, vel, isRelease);
>           return;
>       }
>   
>       const float log2e = 1.44269504f;
>       const float inv_srate_2pi = M_TWOPI / srate;
>       const float log_vel = vel * M_TWOLN100;
>       const float ratio_max = model[c_max_partials - 1];
>       const float f_nyq = c_nyquist_factor * srate;
>   
>       activePartialsCount = 0;
>   
>       for (int p = 0; p < npartials; ++p) {
>           Partial& part = partials[p];
>           int idx = part.k - 1;
>           if (idx < 0 || idx >= c_max_partials) continue;
>   
>           float32_t ratio = model[idx];
>   
>           // 1. Inharmonicity
>           float exp_inharm_part = (part.vel_inharm * log_vel) * log2e;
>           union { float f; int32_t i; } u_inharm;
>           u_inharm.i = (int32_t)(exp_inharm_part * 8388608.0f) + 1065353216;
>           
>           float inharm_k = fminf(1.0f, part.inharm * u_inharm.f) - 0.0001f;
>           float r_m_1 = ratio - 1.0f;
>           inharm_k = fasterSqrt(1.0f + inharm_k * (r_m_1 * r_m_1));
>           
>           float f_k = freq * ratio * inharm_k;
>   
>           // 2. Decay
>           float exp_decay_part = (part.vel_decay * log_vel) * log2e;
>           union { float f; int32_t i; } u_decay;
>           u_decay.i = (int32_t)(exp_decay_part * 8388608.0f) + 1065353216;
>           
>           float d_k = fminf(100.0f, part.decay * u_decay.f);
>           if (isRelease) d_k *= rel;
>   
>           // Range Check: If this partial is invalid, we mute it and stop counting
>           // (Assuming partials are roughly ordered by frequency)
>           if (f_k >= f_nyq || f_k < c_freq_min || d_k < c_decay_min) {
>               part.vb0 = part.vb2 = part.va1 = part.va2 = vdupq_n_f32(0.0f);
>               continue; 
>           }
>   
>           // 3. Damping and Tone
>           float f_max = fminf(c_freq_max, freq * ratio_max * inharm_k);
>           float d_base = (part.damp <= 0 ? freq : f_max) / f_k;
>           float d_mod = e_expff(fasterlogf(d_base) * (part.damp * 2.0f));
>           d_k /= d_mod;
>   
>           float t_base = (part.tone <= 0 ? f_k / freq : f_k / f_max);
>           float t_gain = e_expff(fasterlogf(t_base) * (part.tone * 2.0f));
>   
>           // 4. Hit modulation
>           float h_mod = fminf(0.5f, part.hit + vel * part.vel_hit * 0.5f);
>           float a_k = 35.0f * fabsf(fastersinfullf(M_PI * (float)part.k * h_mod));
>   
>           // 5. Coefficients
>           float omega = f_k * inv_srate_2pi;
>           float b0_val = inv_srate_2pi * t_gain * a_k;
>           float inv_decay = inv_srate_2pi / d_k;
>           float inv_a0 = 1.0f / (1.0f + inv_decay);
>   
>           part.vb0 = vdupq_n_f32(b0_val * inv_a0);
>           part.vb2 = vdupq_n_f32(-b0_val * inv_a0);
>           part.va1 = vdupq_n_f32(-2.0f * fastercosfullf(omega) * inv_a0);
>           part.va2 = vdupq_n_f32((1.0f - inv_decay) * inv_a0);
>   
>           // This partial is audible!
>           activePartialsCount++;
>       }
>   }
 * 3. Update the Hot Path (process)
Now, the loop only runs up to the number of audible partials, potentially saving dozens of vector operations per frame.

> C++
>
>   inline float32x4_t process(float32x4_t input) {
>       if (nmodel >= OpenTube) {
>           return waveguide.process(input);
>       }
>   
>       float32x4_t output = vdupq_n_f32(0.0f);
>       // Dynamic loop bound
>       for (int i = 0; i < activePartialsCount; ++i) {
>           output = vaddq_f32(output, partials[i].process(input));
>       }
>       return output;
>   }

This optimization is particularly effective for high-pitched notes where most partials are above the Nyquist limit.


### 3. Key Data Structures

**Partial (Optimized for NEON)**

> C++
>
>   struct Partial {
>       float32x4_t vb0, vb2, va1, va2; // Filter coefficients
>       float32x4_t vy1, vy2, vx1, vx2; // Filter state
>       float32_t k, inharm, decay, damp, tone, hit; // Parameters
>       float32_t vel_decay, vel_hit, vel_inharm; // Per-partial modulation
>   };

### 4. Pending Tasks / Next Steps
-[ ] Build and test on real HW: the pre optimized verison had a crash in audio interface triggered by Resonator, suspect was infinite/div by 0. Build should be stable and working.
-[ ] Test Bench: Verify Resonator::update logic with isRelease scaling for d_k.
-[ ] Benchmark: Compare CPU usage of 64 partials with and without APC (Active Partial Counting).
-[ ] Validation: Ensure rel and srate are correctly initialized in Resonator::setParams.