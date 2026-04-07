#pragma once

#include <arm_neon.h>
#include <float_math.h>
#include <cmath>
#include <cstdlib>
#include <cstring>

#define FDN_CHANNELS 8
#define FDN_BUFFER_SIZE 32768
#define FDN_BUFFER_MASK (FDN_BUFFER_SIZE - 1)
#define PREDELAY_BUFFER_SIZE 16384
#define PREDELAY_MASK (PREDELAY_BUFFER_SIZE - 1)
#define SPARKLE_BUFFER_SIZE 4096
#define NUM_RESONATORS (6)

// Biquad definitions for the COLOR path
typedef struct {
    float b0, b1, b2, a1, a2;
} biquad_coeffs_t;

typedef struct {
    float z1, z2;
} biquad_state_t;

fast_inline float process_biquad(float in, biquad_state_t* state, biquad_coeffs_t* c) {
    float out = in * c->b0 + state->z1;
    state->z1 = in * c->b1 - out * c->a1 + state->z2;
    state->z2 = in * c->b2 - out * c->a2;
    return out;
}

class FDNEngine {
public:
    FDNEngine() : sampleRate(48000.0f) {
        initialized = false;
        Reset();
    }

    // ========================================================================
    // NEW PARALLEL PARAMETERS
    // ========================================================================
    float dark_amt = 50.0f;
    float glow_amt = 50.0f;
    float bright_amt = 50.0f;
    float color_amt = 50.0f;
    float spark_amt = 50.0f;

    float sizeScale = 1.0f;
    float predelayScale = 0.0f;
    float decay = 0.8f;

    // ========================================================================
    // STATE VARIABLES
    // ========================================================================
    // FDN Core
    float fdnMem[FDN_CHANNELS * FDN_BUFFER_SIZE] __attribute__((aligned(16)));
    float baseDelayTimes[FDN_CHANNELS];
    float delayTimes[FDN_CHANNELS];
    float fdnState[FDN_CHANNELS];
    float hadamard[FDN_CHANNELS][FDN_CHANNELS] __attribute__((aligned(16)));
    int writePos = 0;

    // Predelay
    float preDelayBuffer[PREDELAY_BUFFER_SIZE] __attribute__((aligned(16)));
    int preDelayWritePos = 0;

    // Path 1: Glow (Modulated Chamberlin SVF)
    float glow_lfo_phase = 0.0f;
    float glow_lp_l = 0.0f;
    float glow_bp_l = 0.0f;
    float glow_lp_r = 0.0f;
    float glow_bp_r = 0.0f;

    // Path 2: Dark (Mono Sub)
    float dark_prev_sample = 0.0f;
    int zero_cross_count = 0;
    float sub_state = 1.0f;

    // Path 3: Bright (Harmonic Exciter States)
    biquad_state_t  bright_hpf_l;
    biquad_state_t  bright_hpf_r;
    biquad_coeffs_t bright_coeffs;

    // Path 4: Color (6 Visual Spectrum Resonators)
    biquad_state_t  color_filters_l[NUM_RESONATORS] __attribute__((aligned(16)));
    biquad_state_t  color_filters_r[NUM_RESONATORS] __attribute__((aligned(16)));
    biquad_coeffs_t color_coeffs[NUM_RESONATORS]    __attribute__((aligned(16)));

    // Path 5: Sparkle (Stereo Granular S&H)
    float sparkle_buffer_l[SPARKLE_BUFFER_SIZE];
    float sparkle_buffer_r[SPARKLE_BUFFER_SIZE];
    int spark_write = 0;
    float spark_read = 0.0f;
    float spark_speed = 2.0f;
    float spark_pan_l = 0.5f;
    float spark_pan_r = 0.5f;
    int spark_countdown = 0;

    float sampleRate;
    bool initialized;

    // ========================================================================
    // INITIALIZATION & MATH
    // ========================================================================
    void Init(float sr) {
        sampleRate = sr;
        generate_hadamard();

        // Base prime delay times for 8 channels
        const float primes[FDN_CHANNELS] = {1103.0f, 1511.0f, 1999.0f, 2503.0f, 3011.0f, 3511.0f, 3989.0f, 4513.0f};
        for (int i = 0; i < FDN_CHANNELS; i++) {
            baseDelayTimes[i] = primes[i] * (sampleRate / 48000.0f);
        }

        init_color_resonators();
        initialize_brightness_harmonic_exciter();
        Reset();
        initialized = true;
    }

    void init_color_resonators() {
        // EXACT VISUAL SPECTRUM FREQUENCIES (in Hz)
        const float freqs[NUM_RESONATORS] = {4100.0f, 5000.0f, 5200.0f, 5800.0f, 6600.0f, 7200.0f};
        const float Q = 8.0f; // High Q for distinct ringing resonance

        for (int i = 0; i < NUM_RESONATORS; i++) {
            // Constant Peak Gain Bandpass Biquad Math
            float w0 = 2.0f * M_PI * freqs[i] / sampleRate;
            float alpha = sinf(w0) / (2.0f * Q);

            float a0 = 1.0f + alpha;
            color_coeffs[i].b0 = alpha / a0;
            color_coeffs[i].b1 = 0.0f;
            color_coeffs[i].b2 = -alpha / a0;
            color_coeffs[i].a1 = -2.0f * cosf(w0) / a0;
            color_coeffs[i].a2 = (1.0f - alpha) / a0;
        }
    }

    // 5kHz Butterworth HPF
    void initialize_brightness_harmonic_exciter() {
        float w0_bright = 2.0f * M_PI * 5000.0f / sampleRate;
        float alpha_bright = sinf(w0_bright) / (2.0f * 0.707f);
        float a0_bright = 1.0f + alpha_bright;

        bright_coeffs.b0 = ((1.0f + cosf(w0_bright)) / 2.0f) / a0_bright;
        bright_coeffs.b1 = -(1.0f + cosf(w0_bright)) / a0_bright;
        bright_coeffs.b2 = ((1.0f + cosf(w0_bright)) / 2.0f) / a0_bright;
        bright_coeffs.a1 = (-2.0f * cosf(w0_bright)) / a0_bright;
        bright_coeffs.a2 = (1.0f - alpha_bright) / a0_bright;
    }

    void generate_hadamard() {
        float norm = 1.0f / sqrtf(FDN_CHANNELS);
        for (int i = 0; i < FDN_CHANNELS; i++) {
            for (int j = 0; j < FDN_CHANNELS; j++) {
                int parity = 0;
                int bits = i & j;
                while (bits) {
                    parity ^= (bits & 1);
                    bits >>= 1;
                }
                hadamard[i][j] = parity ? -norm : norm;
            }
        }
    }

    void Reset() {
        memset(fdnMem, 0, sizeof(fdnMem));
        memset(fdnState, 0, sizeof(fdnState));
        memset(preDelayBuffer, 0, sizeof(preDelayBuffer));
        memset(color_filters_l, 0, sizeof(color_filters_l));
        memset(color_filters_r, 0, sizeof(color_filters_r));
        memset(sparkle_buffer_l, 0, sizeof(sparkle_buffer_l));
        memset(sparkle_buffer_r, 0, sizeof(sparkle_buffer_r));
        writePos = 0;
        preDelayWritePos = 0;
        dark_prev_sample = 0.0f;
        memset(&bright_hpf_l, 0, sizeof(biquad_state_t));
        memset(&bright_hpf_r, 0, sizeof(biquad_state_t));
        glow_lfo_phase = 0.0f;
        glow_lp_l = 0.0f;
        glow_bp_l = 0.0f;
        glow_lp_r = 0.0f;
        glow_bp_r = 0.0f;
    }

    //==============
    // Setters
    //==============
    void setDarkness(int32_t val) {
        dark_amt = val;
    }
    void setBrightness(int32_t val) {
        bright_amt = val;
    }
    void setGlow(int32_t val) {
        glow_amt = val;
    }
    void setColor(int32_t val) {
        color_amt = val;
    }
    void setSpark(int32_t val) {
        spark_amt = val;
    }
    void setSize(int32_t val) {
        sizeScale = fmaxf(0.1f, val * 2.0f);
    }
    void setPreDelay(int32_t val) {
        predelayScale = val;
    }

    // ========================================================================
    // BAREBONES FDN STEP (Replaces old bloated FDN logic)
    // ========================================================================
    void step_core_fdn(float in_l, float in_r, float* out_l, float* out_r) {
        float fdnOut[FDN_CHANNELS];

        // 1. Read from Delay Lines
        for (int ch = 0; ch < FDN_CHANNELS; ch++) {
            float dt = baseDelayTimes[ch] * sizeScale;
            float readPos = (float)writePos - dt;
            if (readPos < 0.0f) readPos += FDN_BUFFER_SIZE;

            int idx1 = (int)readPos;
            int idx2 = (idx1 + 1) & FDN_BUFFER_MASK;
            float frac = readPos - idx1;

            float val1 = fdnMem[ch * FDN_BUFFER_SIZE + idx1];
            float val2 = fdnMem[ch * FDN_BUFFER_SIZE + idx2];
            fdnOut[ch] = val1 + frac * (val2 - val1);
        }

        // 2. Mixdown to Stereo Output
        *out_l = fdnOut[0] + fdnOut[1] + fdnOut[2] + fdnOut[3];
        *out_r = fdnOut[4] + fdnOut[5] + fdnOut[6] + fdnOut[7];

        // 3. Hadamard Mixing & Feedback Writing
        for (int i = 0; i < FDN_CHANNELS; i++) {
            float sum = 0.0f;
            for (int j = 0; j < FDN_CHANNELS; j++) {
                sum += fdnOut[j] * hadamard[i][j];
            }

            // Inject Input: Left to channels 0-3, Right to 4-7
            float input_inject = (i < 4) ? in_l : in_r;

            // Pure delay network - no old LPFs, no old swirl, just decay and input
            fdnMem[i * FDN_BUFFER_SIZE + writePos] = input_inject + (sum * decay);
        }

        writePos = (writePos + 1) & FDN_BUFFER_MASK;
    }

    // ========================================================================
    // PARALLEL AUDIO BLOCK PROCESSOR
    // ========================================================================
    void processBlock(float* out, int num_samples) {
        if (!initialized) return;

        int preDelaySamps = (int)(predelayScale * 16000.0f); // Max ~330ms

        float total_wet = (dark_amt + glow_amt + bright_amt + color_amt + spark_amt) / 5.0f;
        float dry_mix = 1.0f - fminf(1.0f, total_wet);
        float wet_normalize = total_wet > 0.0f ? (1.0f / fmaxf(1.0f, total_wet)) : 0.0f;

        for (int i = 0; i < num_samples; i += 2) {
            float in_l = out[i];
            float in_r = out[i+1];

            // PREDELAY
            float mono_in = (in_l + in_r) * 0.5f;
            preDelayBuffer[preDelayWritePos] = mono_in;
            int pd_read = (preDelayWritePos - preDelaySamps + PREDELAY_BUFFER_SIZE) & PREDELAY_MASK;
            float pd_sig = preDelayBuffer[pd_read];
            preDelayWritePos = (preDelayWritePos + 1) & PREDELAY_MASK;

            // 1. CORE FDN (Pure acoustic delays)
            float rev_l, rev_r;
            step_core_fdn(pd_sig, pd_sig, &rev_l, &rev_r);

            // Now 5 parallel paths to be summed up at the end
{
            // ==========================================
            // PATH 1: GLOW (Stereo Swirling SVF)
            // ==========================================
            // LFO rate scales with the glow amount knob (e.g., 0.2Hz to ~1.5Hz)
            // phase offset
            float lfo_rate = (0.2f + (glow_amt * 1.3f)) / 48000.0f;
            glow_lfo_phase += lfo_rate;
            if (glow_lfo_phase > 1.0f) glow_lfo_phase -= 1.0f;

            // Left Channel: Modulate coefficient directly.
            // f_coeff = 0.15 (~1000Hz base) +/- 0.10 (~800Hz sweep)
            float lfo_val_l = fastersinfullf(glow_lfo_phase);
            float f_coeff_l = 0.15f + (0.10f * lfo_val_l);
            float q_coeff = 0.4f; // Mild resonance to accentuate the sweep

            glow_lp_l += f_coeff_l * glow_bp_l;
            glow_bp_l += f_coeff_l * (rev_l - glow_lp_l - q_coeff * glow_bp_l);
            float glow_l = glow_lp_l; // Take the Low-Pass output for warmth

            // Right Channel: 90-degree phase offset for stereo widening
            float phase_r = glow_lfo_phase + 0.25f;
            if (phase_r > 1.0f) phase_r -= 1.0f;

            float lfo_val_r = fastersinfullf(phase_r);
            float f_coeff_r = 0.15f + (0.10f * lfo_val_r);

            glow_lp_r += f_coeff_r * glow_bp_r;
            glow_bp_r += f_coeff_r * (rev_r - glow_lp_r - q_coeff * glow_bp_r);
            float glow_r = glow_lp_r;
}
{
            // PATH 2: DARK (Mono -2 Octave Sub)
            float rev_mono = (rev_l + rev_r) * 0.5f;
            if ((rev_mono > 0.0f && dark_prev_sample <= 0.0f) ||
                (rev_mono < 0.0f && dark_prev_sample >= 0.0f)) {
                zero_cross_count++;
                if (zero_cross_count >= 4) {
                    sub_state = -sub_state;
                    zero_cross_count = 0;
                }
            }
            dark_prev_sample = rev_mono;
            float dark_sig = sub_state * fabsf(rev_mono) * 1.5f;
}
{
            // ==========================================
            // PATH 3: BRIGHT (Harmonic Exciter Air)
            // ==========================================
            // 1. Isolate the extreme highs using the 2nd-order Butterworth HPF
            float hp_l = process_biquad(rev_l, &bright_hpf_l, &bright_coeffs);
            float hp_r = process_biquad(rev_r, &bright_hpf_r, &bright_coeffs);

            // 2. Drive the isolated highs to prepare for saturation
            float drive_l = hp_l * 4.0f;
            float drive_r = hp_r * 4.0f;

            // Clamp to prevent polynomial foldback explosion
            drive_l = fmaxf(-1.0f, fminf(1.0f, drive_l));
            drive_r = fmaxf(-1.0f, fminf(1.0f, drive_r));

            // 3. Polynomial soft-clipping (x - x^3/3)
            // This squashes the peaks, synthesizing beautiful 2nd and 3rd order "sizzle"
            float bright_l = drive_l * (1.0f - (drive_l * drive_l * 0.33333f));
            float bright_r = drive_r * (1.0f - (drive_r * drive_r * 0.33333f));
}
{
            // PATH 4: COLOR (Stereo Visual Spectrum Resonators)
            float color_l = 0.0f;
            float color_r = 0.0f;
            for(int f=0; f<NUM_RESONATORS; f++) {
                color_l += process_biquad(rev_l, &color_filters_l[f], &color_coeffs[f]);
                color_r += process_biquad(rev_r, &color_filters_r[f], &color_coeffs[f]);
            }
            // Scale down since we are summing 6 high-Q resonant peaks
            color_l *= 0.15f;
            color_r *= 0.15f;
}
{
            // PATH 5: SPARKLE (Stereo Pitched-up S&H Pops)
            sparkle_buffer_l[spark_write] = rev_l;
            sparkle_buffer_r[spark_write] = rev_r;
            spark_write = (spark_write + 1) & (SPARKLE_BUFFER_SIZE - 1);

            float spark_l = 0.0f;
            float spark_r = 0.0f;

            if (spark_countdown > 0) {
                int r_idx = (int)spark_read & (SPARKLE_BUFFER_SIZE - 1);
                spark_l = sparkle_buffer_l[r_idx] * spark_pan_l;
                spark_r = sparkle_buffer_r[r_idx] * spark_pan_r;
                spark_read += spark_speed;
                spark_countdown--;
            } else {
                // Xorshift inline PRNG
                static uint32_t seed = 2463534242UL;
                seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5;
                float rand_val = (float)seed / 4294967295.0f;

                if (rand_val < (0.0001f + (spark_amt * 0.005f))) {
                    spark_countdown = 500 + (seed % 1000); // 10-30ms grain
                    spark_read = spark_write - spark_countdown;
                    if(spark_read < 0) spark_read += SPARKLE_BUFFER_SIZE;
                    spark_speed = (seed % 2 == 0) ? 2.0f : 4.0f; // +12 or +24 semitones

                    spark_pan_l = (float)(seed % 100) / 100.0f;
                    spark_pan_r = 1.0f - spark_pan_l;
                }
            }
}
            // ==========================================
            // FINAL PARALLEL MIXDOWN
            // ==========================================
            float mix_l = (dark_sig * dark_amt) + (glow_l * glow_amt) + (bright_l * bright_amt) +
                          (color_l * color_amt) + (spark_l * spark_amt);

            float mix_r = (dark_sig * dark_amt) + (glow_r * glow_amt) + (bright_r * bright_amt) +
                          (color_r * color_amt) + (spark_r * spark_amt);

            mix_l *= wet_normalize;
            mix_r *= wet_normalize;

            out[i]   = (in_l * dry_mix) + mix_l;
            out[i+1] = (in_r * dry_mix) + mix_r;
        }
    }
};