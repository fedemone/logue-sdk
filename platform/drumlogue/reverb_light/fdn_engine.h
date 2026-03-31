#pragma once

/**
 * @file fdn_engine.h
 * @brief Feedback Delay Network (FDN) Reverb Engine
 *
 * Features:
 * - 8-channel FDN with Hadamard matrix
 * - NEON-optimized for ARMv7 (processes 4 samples at a time)
 * - Modulated delay lines for diffusion
 * - Stereo input/output
 *
 * OPTIMIZED:
 * - Vectorized processStereo for 4x performance
 * - NEON-accelerated delay line access
 * - Block processing (4 samples per call)
 */

#include <arm_neon.h>
#include <cstdint>
#include <cmath>
#include <cstring>
#include <malloc.h>
#include <algorithm>

// Buffer size - must be power of 2 for efficient modulo
#define FDN_BUFFER_SIZE 32768  // 2^15
#define FDN_BUFFER_MASK (FDN_BUFFER_SIZE - 1)
#define FDN_CHANNELS 8
// pre-delay buffers
#define PREDELAY_BUFFER_SIZE 16384  // ~341ms at 48kHz
#define PREDELAY_MASK (PREDELAY_BUFFER_SIZE - 1)

class FDNEngine {
public:
    /*===========================================================================*/
    /* Lifecycle Methods */
    /*===========================================================================*/

    FDNEngine()
        : sampleRate(48000.0f)
        , writePos(0)
        , decay(0.5f)
        , modulation(0.05f)
        , glow(0.3f)
        , colorCoeff(0.5f)
        , brightness(0.5f)
        , sizeScale(1.0f)
        , colorLpfL(0.0f)
        , colorLpfR(0.0f)
        , initialized(false)
        , fdnMem(nullptr) {

        // Initialize base delay times (prime-based, in seconds)
        static const float kBaseDelays[FDN_CHANNELS] = {
            0.0421f, 0.0713f, 0.0987f, 0.1249f,
            0.1571f, 0.1835f, 0.2127f, 0.2413f
        };

        for (int i = 0; i < FDN_CHANNELS; i++) {
            baseDelayTimes[i] = kBaseDelays[i];
            delayTimes[i] = kBaseDelays[i];
        }

        // Initialize modulation phases
        for (int i = 0; i < FDN_CHANNELS; i++) {
            modPhases[i] = 0.0f;
        }

        // Initialize state
        memset(fdnState, 0, sizeof(fdnState));

        // Build Hadamard matrix
        buildHadamard();
    }

    ~FDNEngine() {}

    /**
     * Initialize the FDN engine
     * @param sr Sample rate
     * @return true if initialization successful, false if out of memory
     */
    bool init(float sr) {
        sampleRate = sr;
        if (initialized) return true;

        memset(fdnMem, 0, sizeof(fdnMem));
        memset(preDelayBuffer, 0, sizeof(preDelayBuffer));

        preDelayWritePos = 0;
        preDelayOffsetSamples = 0;
        activeSampleCount = 0;

        initialized = true;
        return true;
    }

    /*===========================================================================*/
    /* Parameter Setters */
    /*===========================================================================*/
    void setPreDelay(float ms) {
        float clampedMs = std::max(0.0f, std::min(340.0f, ms));
        preDelayOffsetSamples = (int)(clampedMs * sampleRate / 1000.0f);
    }

    void setDecay(float d) {
        decay = std::max(0.0f, std::min(0.99f, d));
    }

    void setModulation(float m) {
        modulation = std::max(0.0f, std::min(1.0f, m));
    }

    void setDelayTime(int channel, float timeSeconds) {
        if (channel >= 0 && channel < FDN_CHANNELS) {
            delayTimes[channel] = std::max(0.01f, std::min(2.0f, timeSeconds));
        }
    }

    /** Wet/dry mix  0.0 = fully dry, 1.0 = fully wet (GLOW parameter) */
    void setGlow(float g) {
        glow = std::max(0.0f, std::min(1.0f, g));
    }

    /**
     * Tone color: LPF coefficient (COLR parameter).
     * 0.0 = open/bright, 1.0 = very dark/filtered.
     */
    void setColor(float c) {
        colorCoeff = std::max(0.0f, std::min(0.95f, c));
    }

    /**
     * Brightness: high-frequency blend (BRIG parameter).
     * 0.0 = no HF content, 1.0 = full HF (bypasses LPF).
     * Room size: scales all delay times (SIZE parameter).
     */
    void setBrightness(float b) {
        brightness = std::max(0.0f, std::min(1.0f, b));
    }

    /**
     * 0.0 = tiny room, 1.0 = normal, 2.0 = large hall.
     */
    void setSize(float s) {
        sizeScale = std::max(0.1f, std::min(2.0f, s));
        for (int i = 0; i < FDN_CHANNELS; i++) {
            delayTimes[i] = std::min(baseDelayTimes[i] * sizeScale,
                                     (float)(FDN_BUFFER_SIZE - 1) / sampleRate);
        }
    }

    /*===========================================================================*/
    /* NEON Utilities */
    /*===========================================================================*/

    /*===========================================================================*/
    /* Core Processing - Scalar (Single Sample) */
    /*===========================================================================*/

    /**
     * Process one sample through FDN (scalar fallback)
     */
    float processScalar(float input) {
        float delayOut[FDN_CHANNELS];

        for (int ch = 0; ch < FDN_CHANNELS; ch++) {
            float delaySamples = delayTimes[ch] * sampleRate;
            float mod = sinf(modPhases[ch] * 2.0f * (float)M_PI) * modulation * 3.0f;
            float readPos = (float)writePos - (delaySamples + mod);

            while (readPos < 0) readPos += FDN_BUFFER_SIZE;
            while (readPos >= FDN_BUFFER_SIZE) readPos -= FDN_BUFFER_SIZE;

            int index1 = (int)readPos;
            int index2 = (index1 + 1) & FDN_BUFFER_MASK;
            float frac = readPos - index1;

            float s1 = fdnMem[ch * FDN_BUFFER_SIZE + index1];
            float s2 = fdnMem[ch * FDN_BUFFER_SIZE + index2];

            delayOut[ch] = s1 + frac * (s2 - s1);

            modPhases[ch] += modulation * 2.0f / sampleRate;
            if (modPhases[ch] > 1.0f) modPhases[ch] -= 1.0f;
        }

        float mixed[FDN_CHANNELS];
        for (int i = 0; i < FDN_CHANNELS; i++) {
            float sum = 0.0f;
            for (int j = 0; j < FDN_CHANNELS; j++) {
                sum += hadamard[i][j] * delayOut[j];
            }

            // COLOR: Polynomial Soft-Clipping Saturation inside the loop
            float wet = sum * decay;
            float sat = wet - (wet * wet * wet) * 0.15f;
            // HARD CLAMP to prevent FDN blowup
            mixed[i] = std::max(-1.5f, std::min(1.5f, sat));
        }

        mixed[0] += input * (1.0f - decay);

        float output = 0.0f;
        for (int ch = 0; ch < FDN_CHANNELS; ch++) {
            fdnMem[ch * FDN_BUFFER_SIZE + writePos] = mixed[ch];
            fdnState[ch] = mixed[ch];
            output += mixed[ch];
        }

        writePos = (writePos + 1) & FDN_BUFFER_MASK;
        return output / FDN_CHANNELS;
    }

    /*===========================================================================*/
    /* NEON Vectorized Processing (4 Samples) */
    /*===========================================================================*/

    /**
     * Process 4 samples in parallel using NEON
     */
    void process4Samples(const float* inL, const float* inR,
                         float* outL, float* outR) {

        float32x4_t inL4 = vld1q_f32(inL);
        float32x4_t inR4 = vld1q_f32(inR);

        // Convert to mono
        float32x4_t inMono = vmulq_f32(vaddq_f32(inL4, inR4), vdupq_n_f32(0.5f));

        // =================================================================
        // THE HPF (Personality Injector) - Processed manually for 4 lanes
        // =================================================================
        float monoLanes[4];
        vst1q_f32(monoLanes, inMono);
        for(int s = 0; s < 4; s++) {
            float filtered = monoLanes[s] - hpfStateL;
            hpfStateL = monoLanes[s] - hpfCoeff * filtered;
            monoLanes[s] = filtered; // Overwrite with high-passed signal
        }
        inMono = vld1q_f32(monoLanes); // Reload filtered signal into NEON vector

        // =================================================================
        // 1. Pre-Delay Write & Read (monoLanes already holds HPF output above)
        // =================================================================
        float delayedLanes[4];
        for (int s = 0; s < 4; s++) {
            preDelayBuffer[(preDelayWritePos + s) & PREDELAY_MASK] = monoLanes[s];
            int readPos = (preDelayWritePos + s - preDelayOffsetSamples + PREDELAY_BUFFER_SIZE) & PREDELAY_MASK;
            delayedLanes[s] = preDelayBuffer[readPos];
        }

        float32x4_t delayedMono = vld1q_f32(delayedLanes);
        preDelayWritePos = (preDelayWritePos + 4) & PREDELAY_MASK;

        // =================================================================
        // 2. Active Partial Counting (APC)
        // =================================================================
        float32x4_t absIn = vabsq_f32(delayedMono);
        float32x4_t max1 = vmaxq_f32(absIn, vextq_f32(absIn, absIn, 2));
        float32x4_t max2 = vmaxq_f32(max1, vextq_f32(max1, max1, 1));

        if (vgetq_lane_f32(max2, 0) > 1e-5f) {
            // Wake up. Tail length tied to decay parameter.
            activeSampleCount = (int)(sampleRate * (1.0f + decay * 5.0f));
        } else if (activeSampleCount > 0) {
            activeSampleCount -= 4;
        }

        // Bypass everything if asleep
        if (activeSampleCount <= 0) {
            vst1q_f32(outL, inL4);
            vst1q_f32(outR, inR4);
            return;
        }

        // =================================================================
        // Read from all 8 delay lines for 4 samples
        // =================================================================
        float delayOut[FDN_CHANNELS][4];

        for (int ch = 0; ch < FDN_CHANNELS; ch++) {
            float delaySamples = delayTimes[ch] * sampleRate;

            // Calculate read positions for 4 samples
            float readPos[4];
            for (int s = 0; s < 4; s++) {
                // Max ±3 samples depth to avoid Doppler pitch shift exceeding decay rate
                float mod = sinf(modPhases[ch] * 2.0f * M_PI) * modulation * 3.0f;
                float pos = (float)(writePos + s) - (delaySamples + mod);
                while (pos < 0) pos += FDN_BUFFER_SIZE;
                while (pos >= FDN_BUFFER_SIZE) pos -= FDN_BUFFER_SIZE;
                readPos[s] = pos;

                // Update modulation phase (once per 4 samples, max 2 Hz LFO rate)
                if (s == 3) {
                    modPhases[ch] += modulation * 2.0f / sampleRate * 4.0f;
                    if (modPhases[ch] > 1.0f) modPhases[ch] -= 1.0f;
                }
            }

            // Linear interpolation for 4 samples
            for (int s = 0; s < 4; s++) {
                int idx1 = (int)readPos[s];
                int idx2 = (idx1 + 1) & FDN_BUFFER_MASK;
                float frac = readPos[s] - idx1;

                float s1 = fdnMem[ch * FDN_BUFFER_SIZE + idx1];
                float s2 = fdnMem[ch * FDN_BUFFER_SIZE + idx2];
                delayOut[ch][s] = s1 + frac * (s2 - s1);
            }
        }

        // =================================================================
        // Apply Hadamard mixing (Optimized: 4 time-samples in parallel)
        // =================================================================
        float mixed[FDN_CHANNELS][4];

        // Load 4 time-samples for each channel
        float32x4_t v_delayOut[FDN_CHANNELS];
        for (int ch = 0; ch < FDN_CHANNELS; ch++) {
            v_delayOut[ch] = vld1q_f32(delayOut[ch]);
        }

        float32x4_t v_mixed[FDN_CHANNELS];
        float32x4_t v_decay = vdupq_n_f32(decay);

        for (int i = 0; i < FDN_CHANNELS; i++) {
            v_mixed[i] = vdupq_n_f32(0.0f);
            for (int j = 0; j < FDN_CHANNELS; j++) {
                v_mixed[i] = vmlaq_f32(v_mixed[i], v_delayOut[j], vdupq_n_f32(hadamard[i][j]));
            }
            v_mixed[i] = vmulq_f32(v_mixed[i], v_decay);

            // COLOR: NEON Polynomial Soft-Clipping Saturation
            float32x4_t x2 = vmulq_f32(v_mixed[i], v_mixed[i]);
            float32x4_t x3 = vmulq_f32(x2, v_mixed[i]);
            float32x4_t sat = vsubq_f32(v_mixed[i], vmulq_f32(x3, vdupq_n_f32(0.15f)));

            // HARD CLAMP NEON vectors to prevent FDN blowup
            v_mixed[i] = vmaxq_f32(vminq_f32(sat, vdupq_n_f32(1.5f)), vdupq_n_f32(-1.5f));
        }

        // Add delayed mono input to the first channel
        float32x4_t v_feedback = vdupq_n_f32(1.0f - decay);
        v_mixed[0] = vaddq_f32(v_mixed[0], vmulq_f32(delayedMono, v_feedback));

        // Spill back to the mixed array
        for (int ch = 0; ch < FDN_CHANNELS; ch++) {
            vst1q_f32(mixed[ch], v_mixed[ch]);
        }

        // =================================================================
        // Write back to delay lines and update state
        // =================================================================
        for (int ch = 0; ch < FDN_CHANNELS; ch++) {
            float* delayLine = &fdnMem[ch * FDN_BUFFER_SIZE];

            // Write 4 consecutive samples
            for (int s = 0; s < 4; s++) {
                delayLine[(writePos + s) & FDN_BUFFER_MASK] = mixed[ch][s];
            }

            // Update fdnState (use last sample for next block)
            fdnState[ch] = mixed[ch][3];
        }

        writePos = (writePos + 4) & FDN_BUFFER_MASK;

        // =================================================================
        // Stereo downmix (channels 0-3 to left, 4-7 to right)
        // =================================================================
        // Spill stereo input vectors for variable-index access (same reason as inMono above)
        float inLArr[4], inRArr[4];
        vst1q_f32(inLArr, inL4);
        vst1q_f32(inRArr, inR4);

        for (int s = 0; s < 4; s++) {
            float leftOut = 0.0f, rightOut = 0.0f;
            for (int ch = 0; ch < 4; ch++) {
                leftOut += mixed[ch][s];
                rightOut += mixed[ch + 4][s];
            }
            leftOut *= 0.25f;
            rightOut *= 0.25f;

            // Apply tone: LPF (color) blended with dry-wet mix
            // LPF: y = colorCoeff*y_prev + (1-colorCoeff)*x
            colorLpfL = colorCoeff * colorLpfL + (1.0f - colorCoeff) * leftOut;
            colorLpfR = colorCoeff * colorLpfR + (1.0f - colorCoeff) * rightOut;

            // Brightness: blend between lpf output and unfiltered
            float wetL = colorLpfL + brightness * (leftOut  - colorLpfL);
            float wetR = colorLpfR + brightness * (rightOut - colorLpfR);

            float inLVal = inLArr[s];
            float inRVal = inRArr[s];

            outL[s] = inLVal * (1.0f - glow) + wetL * glow;
            outR[s] = inRVal * (1.0f - glow) + wetR * glow;
        }
    }

    /*===========================================================================*/
    /* Main Processing Entry Point - Vectorized */
    /*===========================================================================*/

    /**
     * Process stereo audio through FDN (vectorized)
     * Handles both full blocks of 4 and remainder samples
     */
    void processStereo(const float* inL, const float* inR,
                       float* outL, float* outR,
                       int numSamples) {

        // Bypass if not initialized
        if (!initialized) {
            memcpy(outL, inL, numSamples * sizeof(float));
            memcpy(outR, inR, numSamples * sizeof(float));
            return;
        }

        int samplesProcessed = 0;

        // =================================================================
        // Process in blocks of 4 (vectorized path)
        // =================================================================
        while (samplesProcessed + 4 <= numSamples) {
            process4Samples(inL + samplesProcessed,
                            inR + samplesProcessed,
                            outL + samplesProcessed,
                            outR + samplesProcessed);
            samplesProcessed += 4;
        }

        // =================================================================
        // Process remaining 1-3 samples (scalar fallback)
        // =================================================================
        for (int i = samplesProcessed; i < numSamples; i++) {
            // Simple one-pole DC blocker / HPF
            float currentL = (inL[i] + inR[i]) * 0.5f;
            float filtered = currentL - hpfStateL;
            hpfStateL = currentL - hpfCoeff * filtered;

            // Pre-Delay
            preDelayBuffer[preDelayWritePos] = filtered;
            int readPos = (preDelayWritePos - preDelayOffsetSamples + PREDELAY_BUFFER_SIZE) & PREDELAY_MASK;
            float delayedInput = preDelayBuffer[readPos];
            preDelayWritePos = (preDelayWritePos + 1) & PREDELAY_MASK;

            // APC Check
            if (std::abs(delayedInput) > 1e-5f) {
                activeSampleCount = (int)(sampleRate * (1.0f + decay * 5.0f));
            } else if (activeSampleCount > 0) {
                activeSampleCount--;
            }

            // Bypass if asleep
            if (activeSampleCount <= 0) {
                outL[i] = inL[i];
                outR[i] = inR[i];
                continue;
            }

            // Pass delayed input to your existing processScalar method
            processScalar(delayedInput);  // populates fdnState for stereo spread

            // Simple stereo spread (channels 0-3 to left, 4-7 to right)
            float leftOut = 0.0f, rightOut = 0.0f;
            for (int ch = 0; ch < 4; ch++) {
                leftOut += fdnState[ch];
                rightOut += fdnState[ch + 4];
            }
            leftOut *= 0.25f;
            rightOut *= 0.25f;

            // Apply tone (color LPF + brightness blend)
            colorLpfL = colorCoeff * colorLpfL + (1.0f - colorCoeff) * leftOut;
            colorLpfR = colorCoeff * colorLpfR + (1.0f - colorCoeff) * rightOut;
            float wetL = colorLpfL + brightness * (leftOut  - colorLpfL);
            float wetR = colorLpfR + brightness * (rightOut - colorLpfR);

            outL[i] = inL[i] * (1.0f - glow) + wetL * glow;
            outR[i] = inR[i] * (1.0f - glow) + wetR * glow;
        }
    }

private:
    /*===========================================================================*/
    /* Private Methods */
    /*===========================================================================*/

    void buildHadamard() {
        float norm = 1.0f / sqrtf(8.0f);

        for (int i = 0; i < 8; i++) {
            for (int j = 0; j < 8; j++) {
                int bits = i & j;
                int parity = 0;
                while (bits) {
                    parity ^= (bits & 1);
                    bits >>= 1;
                }
                hadamard[i][j] = parity ? -norm : norm;
            }
        }
    }

    /*===========================================================================*/
    /* Private Member Variables */
    /*===========================================================================*/

    float sampleRate;
    int writePos;
    float decay;
    float modulation;
    float glow;          // wet/dry mix  0..1
    float colorCoeff;    // tone LPF coefficient  0..0.95
    float brightness;    // HF blend  0..1
    float sizeScale;     // room size scale  0.1..2.0
    float colorLpfL;     // LPF state for left channel output
    float colorLpfR;     // LPF state for right channel output

    // High Pass filter
    float hpfStateL = 0.0f;
    // float hpfStateR = 0.0f;
    float hpfCoeff = 0.85f; // Adjust between 0.0 (off) and 0.99 (heavy low cut)

    bool initialized;
    float fdnMem[FDN_CHANNELS * FDN_BUFFER_SIZE] __attribute__((aligned(16)));
    float baseDelayTimes[FDN_CHANNELS]; // unscaled delay times
    float delayTimes[FDN_CHANNELS];
    float modPhases[FDN_CHANNELS];
    float fdnState[FDN_CHANNELS];
    float hadamard[FDN_CHANNELS][FDN_CHANNELS] __attribute__((aligned(16)));

    float preDelayBuffer[PREDELAY_BUFFER_SIZE] __attribute__((aligned(16)));
    int preDelayWritePos;
    int preDelayOffsetSamples;
    int activeSampleCount;
};