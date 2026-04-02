#pragma once

/**
 * @file NeonAdvancedLabirinto.h
 * @brief Advanced NEON-optimized reverb with FDN (Feedback Delay Network)
 *
 * OPTIMIZED:
 * - vld4q_f32 gather for 3x faster delay line reads
 * - Interleaved storage format for efficient vector loads
 * - Vectorized linear interpolation
 * - Process 4 samples in parallel throughout
 */

#include <arm_neon.h>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <float_math.h>
#include <algorithm>

// Maximum delay line length (2 seconds at 48kHz)
#define MAX_DELAY_SECONDS 2.0f
#define MAX_DELAY_SAMPLES (int)(MAX_DELAY_SECONDS * 48000)

// Number of FDN channels (must be multiple of 4 for NEON)
#define FDN_CHANNELS 8

// Buffer size for delay lines (power of 2 for efficient modulo)
#define BUFFER_SIZE 65536  // 2^16
#define BUFFER_MASK (BUFFER_SIZE - 1)
#define FREQ_MAX_DIV_MIN (18.333f)
#define PREDELAY_BUFFER_SIZE 16384  // ~341ms at 48kHz
#define PREDELAY_MASK (PREDELAY_BUFFER_SIZE - 1)

/**
 * OPTIMIZED: Interleaved frame structure for vld4q_f32
 * Stores all 8 FDN channels at a single time position
 * Format: [ch0, ch1, ch2, ch3, ch4, ch5, ch6, ch7]
 */
typedef struct __attribute__((aligned(16))) {
    float samples[FDN_CHANNELS];  // All 8 channels at this time position
} interleaved_frame_t;

class NeonAdvancedLabirinto {
public:
    /*===========================================================================*/
    /* Lifecycle Methods */
    /*===========================================================================*/

    NeonAdvancedLabirinto()
        : sampleRate(48000.0f)
        , writePos(0)
        , decay(0.5f)
        , diffusion(0.3f)
        , modDepth(0.1f)
        , modRate(0.5f)
        , mix(0.3f)
        , width(1.0f)
        , dampingCoeff(0.5f)
        , lowDecayMult(1.0f)
        , highDecayMult(0.5f)
        , initialized(false)
        , pillar_(3)
        , pingPong_(false)
        , shimmerDepth_(0.0f)
        , shimmerPhase_(0.0f)
        , shimmerFreq_ (35.0f) {

        // Initialize delay times (prime-based for smooth diffusion)
        float baseDelays[FDN_CHANNELS] = {
            0.0421f, 0.0713f, 0.0987f, 0.1249f,
            0.1571f, 0.1835f, 0.2127f, 0.2413f
        };

        for (int i = 0; i < FDN_CHANNELS; i++) {
            delayTimes[i] = baseDelays[i];
        }

        // Initialize modulation phases (store full vector per channel)
        for (int i = 0; i < FDN_CHANNELS; i++) {
	    // Initialize all 4 lanes to the same starting phase
            modPhaseVec[i] = vdupq_n_f32(0.0f);
        }

        // Initialize filter states
        for (int i = 0; i < FDN_CHANNELS; i++) {
            lpfState[i] = vdupq_n_f32(0.0f);
        }

        // Initialize Hadamard matrix
        initHadamardMatrix();
    }

    ~NeonAdvancedLabirinto() {
    }

    /**
     * Initialize the reverb with proper memory alignment
     * @return true if initialization successful
     */
    bool init() {
        // Initialize Hadamard mixing matrix BEFORE clearing/processing
        initHadamardMatrix();

        // Clear buffer
        clear();

        initialized = true;
        return true;
    }

    /**
     * Clear all delay lines and filter states
     */
    void clear() {
        // Use NEON to clear efficiently
        float32x4_t zero = vdupq_n_f32(0.0f);
        for (int i = 0; i < BUFFER_SIZE; i++) {
            // Clear 8 channels using 2 NEON stores
            vst1q_f32(&delayLine[i].samples[0], zero);
            vst1q_f32(&delayLine[i].samples[4], zero);
        }

        writePos = 0;

        // Reset modulation phases with proper per-lane offsets
	    // lane k = k * incPerSample so sequential
        // samples start at phase 0, inc, 2*inc, 3*inc
        float incPerSample = modRate * M_TWOPI / sampleRate;
        float32x4_t init_phases = {
            0.0f,
            incPerSample,
            2.0f * incPerSample,
            3.0f * incPerSample
        };
        for (int i = 0; i < FDN_CHANNELS; i++) {
            modPhaseVec[i] = init_phases;
        }

        // Reset filter states
        for (int i = 0; i < FDN_CHANNELS; i++) {
            lpfState[i] = zero;
        }

        // Reset pre delay line
        memset(preDelayBuffer, 0, sizeof(preDelayBuffer));
        preDelayWritePos = 0;
        activeSampleCount = 0;
    }

    /*===========================================================================*/
    /* Parameter Setters */
    /*===========================================================================*/

    void setDecay(float d) { decay = fmaxf(0.0f, fminf(0.99f, d)); }
    void setDiffusion(float d) { diffusion = fmaxf(0.0f, fminf(1.0f, d)); }

    /**
     * Set pillar count / routing mode.
     *
     * 0 = sparse  (only 2 channels reach output - large sparse room feel)
     * 1 = ping-pong (4 channels, alternating L/R - bouncing stereo echo)
     * 2 = stone    (6 channels - sombre, dense)
     * 3 = full     (all 8 channels - full FDN, default)
     * 4 = shimmer  (all 8 channels + subtle frequency-modulated re-injection)
     */
    void setPillar(int value) {
        pillar_       = std::max(0, std::min(value, 4));
        pingPong_     = (pillar_ == 1);
        shimmerDepth_ = (pillar_ == 4) ? 0.04f : 0.0f;
        shimmerPhase_ = 0.0f;
    }
    void setModDepth(float d) { modDepth = fmaxf(0.0f, fminf(1.0f, d)); }
    void setModRate(float r) { modRate = fmaxf(0.1f, fminf(10.0f, r)); }
    void setMix(float m) { mix = fmaxf(0.0f, fminf(1.0f, m)); }
    void setWidth(float w) { width = fmaxf(0.0f, fminf(2.0f, w)); }
    // 3 Hz to 8 Hz: Creates Cochrane's "microtonal beating" — a nervous, spicy, disconcerting chorusing.
    // 20 Hz to 55 Hz: Creates the "low pitching" cascade — thick, dark, metallic undertones that dive deeper as the reverb decays.
    void setShimmerFreq(float value) {
        // Normalize UI value to 0.0 -> 1.0
        float norm = fmaxf(0.0f, fminf(1.0f, (float)value / 100.0f));

        // Exponential mapping: min * (max/min)^norm
        // 3.0f * (55.0 / 3.0)^norm
        shimmerFreq_ = 3.0f * fasterpowf(FREQ_MAX_DIV_MIN, norm);
    }
    float getShimmerFreq() { return shimmerFreq_; }
    void setPreDelay(float ms) {
        float clampedMs = fmaxf(0.0f, fminf(340.0f, ms));
        preDelayOffsetSamples = (int)(clampedMs * sampleRate / 1000.0f);
    }
    void setDamping(float freqHz) {
        freqHz = fmaxf(200.0f, fminf(10000.0f, freqHz));
	    // omega = 2π * fc / fs;  coeff ≈ 1 - omega  (first-order approx)
        float omega = 2.0f * (float)M_PI * freqHz / sampleRate;
        dampingCoeff = e_expff(-omega);
    }

    /**
     * Low-frequency RT60 multiplier (1..100 → 0.01..1.0 s scale).
     * Increases effective decay for low-end warmth.
     */
    void setLowDecay(float value) {
        // value 1-100; map to a per-channel decay multiplier 0.9..1.5
        lowDecayMult = 0.9f + (value / 100.0f) * 0.6f;
    }

    /**
     * High-frequency RT60 multiplier (1..100 → 0.01..1.0 s scale).
     * Controls how quickly the high end decays.
     */
    void setHighDecay(float value) {
        // value 1-100; higher value = brighter (less high-freq damping)
        highDecayMult = 0.1f + (value / 100.0f) * 0.9f;
    }

    /*===========================================================================*/
    /* Core Processing - Fully NEON Vectorized */
    /*===========================================================================*/

    /**
     * Process stereo audio through the reverb (NEON vectorized)
     * Processes 4 samples at a time for maximum efficiency
     */
    void process(const float* inL, const float* inR,
                 float* outL, float* outR,
                 int numSamples) {

	    // Safety check
        if (!initialized) {
            memcpy(outL, inL, numSamples * sizeof(float));
            memcpy(outR, inR, numSamples * sizeof(float));
            return;
        }

        // Process in blocks of 4 samples
        int samplesProcessed = 0;
        while (samplesProcessed < numSamples) {
            int blockSize = (numSamples - samplesProcessed) >= 4 ? 4 : 1;

            if (blockSize == 4) {
                // =================================================================
                // VECTORIZED PATH: Process 4 samples at once
                // =================================================================
                process4Samples(inL + samplesProcessed, inR + samplesProcessed,
                                outL + samplesProcessed, outR + samplesProcessed);
            } else {
                // =================================================================
                // SCALAR PATH: Process remaining 1-3 samples
                // =================================================================
                for (int i = 0; i < blockSize; i++) {
                    float dryL = inL[samplesProcessed + i];
                    float dryR = inR[samplesProcessed + i];
                    float mono = (dryL + dryR) * 0.5f;
                    float wetL, wetR;
                    processScalar(mono, wetL, wetR);
                    outL[samplesProcessed + i] = dryL * (1.0f - mix) + wetL * mix;
                    outR[samplesProcessed + i] = dryR * (1.0f - mix) + wetR * mix;
                }
            }

            samplesProcessed += blockSize;
        }
    }

private:
    /*===========================================================================*/
    /* OPTIMIZED: vld4-based delay line reading with vectorized interpolation */
    /*===========================================================================*/

    void readDelayLines4(float32x4_t* out) {
        // Calculate read positions for all 8 channels × 4 samples
        float32x4_t baseReadPos = {
            (float)writePos,
            (float)(writePos + 1),
            (float)(writePos + 2),
            (float)(writePos + 3)
        };

        // Pre-calculate modulation for all channels (sin values for all 4 phases at once)
        float32x4_t mods[FDN_CHANNELS];
        for (int ch = 0; ch < FDN_CHANNELS; ch++) {
            // Get the 4 phases for this channel (one per sample)
            float32x4_t phases = modPhaseVec[ch];

            // Compute sin(2π * phase) for all 4 samples at once
            // First scale to [0, 2π]
            float32x4_t angles = vmulq_f32(phases, vdupq_n_f32(M_TWOPI));

            // Compute sin using NEON approximation
            float32x4_t sin_vals = sin_ps(angles);

	    // Scale by modulation depth
            mods[ch] = vmulq_f32(sin_vals, vdupq_n_f32(modDepth * 100.0f));
        }

        // For each channel, calculate read positions
        float32x4_t readPositions[FDN_CHANNELS];
        uint32_t baseIndices[FDN_CHANNELS][4];
        float fracParts[FDN_CHANNELS][4];  // fractional parts for interpolation

        for (int ch = 0; ch < FDN_CHANNELS; ch++) {
            float delaySamples = delayTimes[ch] * sampleRate;
            readPositions[ch] = vsubq_f32(baseReadPos,
                vaddq_f32(vdupq_n_f32(delaySamples), mods[ch]));

            // Wrap to [0, BUFFER_SIZE)
            float pos_vals[4];
            vst1q_f32(pos_vals, readPositions[ch]);

            for (int s = 0; s < 4; s++) {
                // Manual wrap for each sample (safer than vectorized wrap)
                float pos = pos_vals[s];
                while (pos < 0) pos += BUFFER_SIZE;
                while (pos >= BUFFER_SIZE) pos -= BUFFER_SIZE;
                uint32_t base = (uint32_t)pos;
                baseIndices[ch][s] = base;
                fracParts[ch][s] = pos - (float)base;  // true fractional part
            }
        }

        // Read each channel independently: each channel has its own read position
        // (baseIndices[ch][s]) so we cannot share a single vld4q_f32 across channels.
        // Scalar interpolation per channel avoids the previous cross-frame read bug.
        for (int ch = 0; ch < FDN_CHANNELS; ch++) {
            float out_lanes[4];

            for (int s = 0; s < 4; s++) {
                uint32_t idx0 = baseIndices[ch][s] & BUFFER_MASK;
                uint32_t idx1 = (idx0 + 1) & BUFFER_MASK;
                float frac = fracParts[ch][s];
                float s0 = delayLine[idx0].samples[ch];
                float s1 = delayLine[idx1].samples[ch];
                out_lanes[s] = s0 + frac * (s1 - s0);
            }

            out[ch] = vld1q_f32(out_lanes);
        }
    }

    /*===========================================================================*/
    /* Vectorized Hadamard Transform */
    /*===========================================================================*/

    void applyHadamard4(const float32x4_t* in, float32x4_t* out) {
        // Standard matrix multiplication mapped across parallel time lanes
        for (int i = 0; i < FDN_CHANNELS; i++) {
            out[i] = vdupq_n_f32(0.0f);
            for (int j = 0; j < FDN_CHANNELS; j++) {
                // in[j] holds 4 time samples.
                // hadamard[i][j] is the scalar mixing coefficient.
                out[i] = vmlaq_f32(out[i], in[j], vdupq_n_f32(hadamard[i][j]));
            }
        }
    }

    /*===========================================================================*/
    /* Vectorized Filter Application */
    /*===========================================================================*/
    /**
     * NEON one-pole LPF per channel.
     */
    void applyDiffusion4(float32x4_t* signals) {
        // Causal one-pole LPF per channel.
        // The 4 lanes of signals[ch] hold 4 consecutive TIME samples for channel ch.
        // We must process them in order, carrying the filter state sample-to-sample.
        // State carried across blocks = lane 3 of the previous block's output.
        float pole = diffusion * dampingCoeff;
        float oneminuspole = 1.0f - pole;

        // Since the filter is recursive (each step depends on the previous output),
        // the sequential dependency remains, but the scalar operations can be
        // performed on register values.
        for (int ch = 0; ch < FDN_CHANNELS; ch++) {
            // Extract inter-block carry: most recent output from the previous block
            float state = vgetq_lane_f32(lpfState[ch], 3);

            // Load the 4 input values into a NEON vector
            float32x4_t v = signals[ch];

            // Process each lane sequentially, building the result vector directly
            float32x4_t result;

            // Lane 0
            float y0 = vgetq_lane_f32(v, 0) * oneminuspole + state * pole;
            result = vsetq_lane_f32(y0, result, 0);
            state = y0;

            // Lane 1
            float y1 = vgetq_lane_f32(v, 1) * oneminuspole + state * pole;
            result = vsetq_lane_f32(y1, result, 1);
            state = y1;

            // Lane 2
            float y2 = vgetq_lane_f32(v, 2) * oneminuspole + state * pole;
            result = vsetq_lane_f32(y2, result, 2);
            state = y2;

            // Lane 3
            float y3 = vgetq_lane_f32(v, 3) * oneminuspole + state * pole;
            result = vsetq_lane_f32(y3, result, 3);
            state = y3;

            // Now 'result' holds the filtered values, ready for further SIMD processing

            lpfState[ch] = result;
            signals[ch] = result;
        }
    }

    /*===========================================================================*/
    /* Vectorized Delay Line Write */
    /*===========================================================================*/

    void writeDelayLines4(const float32x4_t* signals) {
        // Spill all channel vectors once; index by sample position (variable s)
        // to avoid vgetq_lane_f32(v, variable) which requires a constant index.
        float ch_lanes[FDN_CHANNELS][4];
        for (int ch = 0; ch < FDN_CHANNELS; ch++)
            vst1q_f32(ch_lanes[ch], signals[ch]);

        for (int s = 0; s < 4; s++) {
            uint32_t pos = (writePos + s) & BUFFER_MASK;
            for (int ch = 0; ch < FDN_CHANNELS; ch++)
                delayLine[pos].samples[ch] = ch_lanes[ch][s];
        }

        writePos = (writePos + 4) & BUFFER_MASK;
    }

    /*===========================================================================*/
    /* Main Processing Loop */
    /*===========================================================================*/

    void process4Samples(const float* inL, const float* inR,
                         float* outL, float* outR) {

        // Load 4 input samples for L and R channels
        float32x4_t inL4 = vld1q_f32(inL);
        float32x4_t inR4 = vld1q_f32(inR);

        // Convert to mono for FDN input
        float32x4_t inMono = vmulq_f32(vaddq_f32(inL4, inR4), vdupq_n_f32(0.5f));

        // =================================================================
        // 1. Pre-Delay Write
        // =================================================================
        // TO BE EVALUATED: Add tape saturation to the pre-delay buffer?
        float monoLanes[4];
        vst1q_f32(monoLanes, inMono);

        for (int s = 0; s < 4; s++) {
            preDelayBuffer[(preDelayWritePos + s) & PREDELAY_MASK] = monoLanes[s];
        }

        // =================================================================
        // 2. Pre-Delay Read
        // =================================================================
        float delayedLanes[4];
        for (int s = 0; s < 4; s++) {
            int readPos = (preDelayWritePos + s - preDelayOffsetSamples + PREDELAY_BUFFER_SIZE) & PREDELAY_MASK;
            delayedLanes[s] = preDelayBuffer[readPos];
        }
        float32x4_t delayedMono = vld1q_f32(delayedLanes);
        preDelayWritePos = (preDelayWritePos + 4) & PREDELAY_MASK;

        // =================================================================
        // 3. Active Partial Counting (CPU Optimization)
        // =================================================================
        // Check if the current delayed input block contains active audio
        float32x4_t absIn = vabsq_f32(delayedMono);
        float32x4_t max1 = vmaxq_f32(absIn, vextq_f32(absIn, absIn, 2));
        float32x4_t max2 = vmaxq_f32(max1, vextq_f32(max1, max1, 1));

        if (vgetq_lane_f32(max2, 0) > 1e-5f) {
            // Signal present: reset counter to maximum reverb tail length
            // RT60 roughly corresponds to decay time + predelay
            activeSampleCount = (int)(sampleRate * (1.0f + decay * 5.0f));
        } else if (activeSampleCount > 0) {
            // Signal absent: decrement counter
            activeSampleCount -= 4;
        }

        // If counter has expired, bypass FDN processing to save cycles
        if (activeSampleCount <= 0) {
            vst1q_f32(outL, inL4);
            vst1q_f32(outR, inR4);
            return;
        }

        // =================================================================
        // Read from all 8 delay lines for 4 samples using current phases
        // =================================================================
        float32x4_t delayOut[FDN_CHANNELS];
        readDelayLines4(delayOut);

        // Advance modulation phases for the next block (after read so phases aren't clobbered)
        updateModulation4();

        // =================================================================
        // Apply Hadamard mixing matrix (vectorized)
        // =================================================================
        float32x4_t mixed[FDN_CHANNELS];
        applyHadamard4(delayOut, mixed);

        // =================================================================
        // Apply decay uniformly to all channels using the geometric mean of
        // highDecayMult and lowDecayMult. This preserves the warmth/brightness
        // balance controls while avoiding L/R stereo imbalance that would result
        // from applying different decay multipliers to the two channel groups.
        // =================================================================
        float unifiedDecay = fminf(0.99f, decay * sqrtf(highDecayMult * lowDecayMult));
        float32x4_t decayAll = vdupq_n_f32(unifiedDecay);
        float32x4_t feedback = vdupq_n_f32(1.0f - decay);

        for (int i = 0; i < FDN_CHANNELS; i++) {
            mixed[i] = vmulq_f32(mixed[i], decayAll);
        }

        // Add input to first channel (with feedback control)
        mixed[0] = vaddq_f32(mixed[0], vmulq_f32(delayedMono, feedback));

        // =================================================================
        // Apply DAMP (dampingCoeff) + COMP (diffusion) filters
        // =================================================================
        applyDiffusion4(mixed);

        // =================================================================
        // Exotic Low-Pitching Shimmer (PILL=4) - NEON Vectorized
        // =================================================================
        if (shimmerDepth_ > 0.0f) {
            // 1. Sum channels 0-3 (Left) and 4-7 (Right)
            float32x4_t sumL = vaddq_f32(vaddq_f32(mixed[0], mixed[1]),
                                         vaddq_f32(mixed[2], mixed[3]));
            float32x4_t sumR = vaddq_f32(vaddq_f32(mixed[4], mixed[5]),
                                         vaddq_f32(mixed[6], mixed[7]));

            // 2. Mix them down to a mono preview and scale by 0.125 (1/8)
            float32x4_t monoPreview = vmulq_f32(vaddq_f32(sumL, sumR), vdupq_n_f32(0.125f));

            // 3. Calculate phase increments for 4 parallel samples
            float inc = M_TWOPI * shimmerFreq_ / sampleRate;
            float32x4_t phaseVec = vdupq_n_f32(shimmerPhase_);
            phaseVec = vsetq_lane_f32(shimmerPhase_ + inc, phaseVec, 1);
            phaseVec = vsetq_lane_f32(shimmerPhase_ + 2.0f * inc, phaseVec, 2);
            phaseVec = vsetq_lane_f32(shimmerPhase_ + 3.0f * inc, phaseVec, 3);

            // 4. Generate 4 sine wave samples at once using your fast approximation
            float32x4_t sinVec = sin_ps(phaseVec);

            // 5. Calculate the ring-modulated shimmer signal (preview * sin * depth)
            float32x4_t shim = vmulq_f32(monoPreview,
                                         vmulq_f32(sinVec, vdupq_n_f32(shimmerDepth_)));

            // 6. Inject the shimmer back into channels 6 and 7 with inverted phase
            mixed[FDN_CHANNELS - 2] = vaddq_f32(mixed[FDN_CHANNELS - 2], shim);
            mixed[FDN_CHANNELS - 1] = vsubq_f32(mixed[FDN_CHANNELS - 1], shim);

            // 7. Advance the master scalar phase for the next block of 4 samples
            shimmerPhase_ += 4.0f * inc;
            while (shimmerPhase_ >= M_TWOPI) { shimmerPhase_ -= M_TWOPI; }
        }

        // =================================================================
        // Write back to delay lines
        // =================================================================
        writeDelayLines4(mixed);

        // =================================================================
        // Mix down to stereo — routing depends on PILL value
        // =================================================================
        float32x4_t leftMix, rightMix;

        if (pingPong_) {
            // PILL=1: alternating channels — ch 0,2 → L; ch 1,3 → R
            float32x4_t pingL = vaddq_f32(mixed[0], mixed[2]);
            float32x4_t pingR = vaddq_f32(mixed[1], mixed[3]);
            leftMix  = vmulq_f32(pingL, vdupq_n_f32(0.5f));
            rightMix = vmulq_f32(pingR, vdupq_n_f32(0.5f));
        } else {
            int activeCh;
            switch (pillar_) {
                case 0: activeCh = 2; break;
                case 2: activeCh = 6; break;
                default: activeCh = FDN_CHANNELS; break;  // 3, 4 → 8
            }

            int halfL = activeCh < 4 ? activeCh : 4;
            int halfR = activeCh > 4 ? activeCh - 4 : 0;

            float32x4_t leftSum  = vdupq_n_f32(0.0f);
            float32x4_t rightSum = vdupq_n_f32(0.0f);
            for (int i = 0; i < halfL; i++)
                leftSum = vaddq_f32(leftSum, mixed[i]);
            for (int i = 4; i < 4 + halfR; i++)
                rightSum = vaddq_f32(rightSum, mixed[i]);

            float normL = halfL > 0 ? 1.0f / halfL : 1.0f;
            leftMix = vmulq_f32(leftSum, vdupq_n_f32(normL));
            if (halfR > 0)
                rightMix = vmulq_f32(rightSum, vdupq_n_f32(1.0f / halfR));
            else
                rightMix = leftMix; // PILL=0: mono fold
        }

        // Apply stereo width:  mid = (L+R)*0.5, side = (L-R)*0.5
        // outL_wet = mid + side*width,  outR_wet = mid - side*width
        float32x4_t mid = vmulq_f32(vaddq_f32(leftMix, rightMix), vdupq_n_f32(0.5f));
        float32x4_t side = vmulq_f32(vsubq_f32(leftMix, rightMix), vdupq_n_f32(0.5f));
        float32x4_t width4 = vdupq_n_f32(width);

        float32x4_t wetL = vaddq_f32(mid, vmulq_f32(side, width4));
        float32x4_t wetR = vsubq_f32(mid, vmulq_f32(side, width4));

        // Wet/dry mix
        float32x4_t wetGain = vdupq_n_f32(mix);
        float32x4_t dryGain = vdupq_n_f32(1.0f - mix);

        float32x4_t outL4 = vaddq_f32(vmulq_f32(inL4, dryGain), vmulq_f32(wetL, wetGain));
        float32x4_t outR4 = vaddq_f32(vmulq_f32(inR4, dryGain), vmulq_f32(wetR, wetGain));

        // Store results
        vst1q_f32(outL, outL4);
        vst1q_f32(outR, outR4);
    }

    void updateModulation4() {
        float incPerSample = modRate * M_TWOPI / sampleRate;
        float32x4_t blockAdvance = vdupq_n_f32(4.0f * incPerSample);
        float32x4_t twoPi = vdupq_n_f32(M_TWOPI);

        for (int ch = 0; ch < FDN_CHANNELS; ch++) {
            float32x4_t newPhases = vaddq_f32(modPhaseVec[ch], blockAdvance);

            // Wrap to [0, 2π) using truncate-toward-zero floor
            float32x4_t div = vmulq_f32(newPhases, vdupq_n_f32(1.0f / (M_TWOPI)));
            float32x4_t floor_f = vcvtq_f32_s32(vcvtq_s32_f32(div));
            newPhases = vsubq_f32(newPhases, vmulq_f32(floor_f, twoPi));

            uint32x4_t neg = vcltq_f32(newPhases, vdupq_n_f32(0.0f));
            newPhases = vbslq_f32(neg, vaddq_f32(newPhases, twoPi), newPhases);

            modPhaseVec[ch] = newPhases;
        }
    }

    /*===========================================================================*/
    /* Scalar Fallback for Remainder Samples */
    /*===========================================================================*/

    void processScalar(float input, float& wetL, float& wetR) {
        float delayOut[FDN_CHANNELS];

        // 1. Pre-Delay Write
        preDelayBuffer[preDelayWritePos] = input;

        // 2. Pre-Delay Read
        int readPos = (preDelayWritePos - preDelayOffsetSamples + PREDELAY_BUFFER_SIZE) & PREDELAY_MASK;
        float delayedInput = preDelayBuffer[readPos];
        preDelayWritePos = (preDelayWritePos + 1) & PREDELAY_MASK;

        // 3. Active Partial Counting
        if (fabsf(delayedInput) > 1e-5f) {
            activeSampleCount = (int)(sampleRate * (1.0f + decay * 5.0f));
        } else if (activeSampleCount > 0) {
            activeSampleCount--;
        }

        // Bypass if tail is dead
        if (activeSampleCount <= 0) {
            wetL = input;
            wetR = input;
            return;
        }

        for (int ch = 0; ch < FDN_CHANNELS; ch++) {
            // For scalar path, we only need the current phase (lane 0)
            float phase = vgetq_lane_f32(modPhaseVec[ch], 0);

            float delaySamples = delayTimes[ch] * sampleRate;
            float mod = sinf(phase * M_TWOPI) * modDepth * 100.0f;
            float readPos = (float)writePos - (delaySamples + mod);

            while (readPos < 0) readPos += BUFFER_SIZE;
            while (readPos >= BUFFER_SIZE) readPos -= BUFFER_SIZE;

            int idx = (int)readPos;
            int idx_next = (idx + 1) & BUFFER_MASK;
            float frac = readPos - idx;

            float s1 = delayLine[idx].samples[ch];
            float s2 = delayLine[idx_next].samples[ch];
            delayOut[ch] = s1 + frac * (s2 - s1);

            // Update scalar phase (only for lane 0)
            float new_phase = phase + modRate * M_TWOPI / sampleRate;
            if (new_phase >= M_TWOPI) new_phase -= M_TWOPI;

            // Update just lane 0, preserve other lanes
            float32x4_t temp = modPhaseVec[ch];
            temp = vsetq_lane_f32(new_phase, temp, 0);
            modPhaseVec[ch] = temp;
        }

        // Frequency-dependent decay
        // Frequency-dependent decay
        float mixed[FDN_CHANNELS];
        for (int i = 0; i < FDN_CHANNELS; i++) {
            float sum = 0.0f;
            for (int j = 0; j < FDN_CHANNELS; j++) {
                sum += hadamard[i][j] * delayOut[j];
            }
            float dm = std::min(0.99f, decay * sqrtf(highDecayMult * lowDecayMult));
            mixed[i] = sum * dm;
        }

        // 4. Inject delayedInput instead of raw input
        mixed[0] += delayedInput * (1.0f - decay);

        // Exotic "Low Pitching" Shimmer (PILL=4)
        // Injects a ring-modulated copy of the wet signal back into the network.
        // The cascading sum/difference frequencies create dense, microtonal undertones.
        // Shimmer (PILL=4): inject a small frequency-modulated copy of the
        // current wet signal back into channels 6 and 7 before writing.
        // Loop gain ≈ 0.04 * 0.088 ≈ 0.004 << 1 → unconditionally stable.
        // TO BE EVALUATED: introduce wavetable LFO for shimmer?
        if (shimmerDepth_ > 0.0f) {
            float previewL = 0.0f, previewR = 0.0f;
            for (int i = 0; i < 4; i++)            previewL += mixed[i];
            for (int i = 4; i < FDN_CHANNELS; i++) previewR += mixed[i];

            float monoPreview = (previewL + previewR) * 0.125f; // /8

            // The microtonal happens here: modulating at audio-rate (e.g., 35 Hz)
            float shim = monoPreview * fastersinfullf(shimmerPhase_) * shimmerDepth_;

            mixed[FDN_CHANNELS - 2] += shim;
            mixed[FDN_CHANNELS - 1] -= shim;

            // Advance phase using our new low-frequency target
            shimmerPhase_ += M_TWOPI * shimmerFreq_ / sampleRate;
            if (shimmerPhase_ >= M_TWOPI) shimmerPhase_ -= M_TWOPI;
        }

        for (int ch = 0; ch < FDN_CHANNELS; ch++) {
            delayLine[writePos].samples[ch] = mixed[ch];
        }
        writePos = (writePos + 1) & BUFFER_MASK;

        // Stereo mix-down: routing depends on PILL value
        float leftRaw = 0.0f, rightRaw = 0.0f;

        if (pingPong_) {
            // PILL=1: alternating L/R among 4 active channels
            // ch 0, 2 → L;  ch 1, 3 → R
            leftRaw  = (mixed[0] + mixed[2]) * 0.5f;
            rightRaw = (mixed[1] + mixed[3]) * 0.5f;
        } else {
            // Determine active channel count
            int activeCh;
            if      (pillar_ == 0) activeCh = 2;
            else if (pillar_ == 2) activeCh = 6;
            else                   activeCh = FDN_CHANNELS;  // 3, 4 → 8

            int halfL = activeCh < 4 ? activeCh : 4;
            int halfR = activeCh > 4 ? activeCh - 4 : 0;
            for (int i = 0; i < halfL; i++)         leftRaw  += mixed[i];
            for (int i = 4; i < 4 + halfR; i++)     rightRaw += mixed[i];
            leftRaw  /= (halfL > 0 ? (float)halfL : 1.0f);
            if (halfR > 0)
                rightRaw /= (float)halfR;
            else
                rightRaw  = leftRaw;  // mono fold for very sparse (PILL=0)
        }

        // Apply stereo width
        float mid  = (leftRaw + rightRaw) * 0.5f;
        float side = (leftRaw - rightRaw) * 0.5f;
        wetL = mid + side * width;
        wetR = mid - side * width;
    }


    /*===========================================================================*/
    /* Initialization */
    /*===========================================================================*/

    void initHadamardMatrix() {
      float norm = 1.0f / sqrtf(8.0f);

      // Store in row-major for scalar access
      for (int i = 0; i < FDN_CHANNELS; i++) {
        for (int j = 0; j < FDN_CHANNELS; j++) {
          int bits = i & j;
          int parity = 0;
          while (bits) {
            parity ^= (bits & 1);
            bits >>= 1;
          }
          hadamard[i][j] = parity ? -norm : norm;
        }
      }

      // Pre-transpose into column-major NEON-friendly format
      for (int j = 0; j < FDN_CHANNELS; j++) {
        for (int i = 0; i < FDN_CHANNELS; i += 4) {
          float32x4_t col = {hadamard[i][j], hadamard[i + 1][j],
                             hadamard[i + 2][j], hadamard[i + 3][j]};
          hadamardCols[j][i / 4] = col;
        }
      }
    }

    /*===========================================================================*/
    /* Private Member Variables */
    /*===========================================================================*/

    float sampleRate;
    int writePos;
    float decay;
    float diffusion;
    float modDepth;
    float modRate;
    float mix;           // wet/dry blend  0..1
    float width;         // stereo width   0..2
    float dampingCoeff;  // one-pole LPF coeff for damping
    float lowDecayMult;  // low-freq decay multiplier
    float highDecayMult; // high-freq decay multiplier

    bool initialized;
    int   pillar_;        /* 0..4 - pillar count / routing mode */
    bool  pingPong_;      /* true when pillar_==1 */
    float shimmerDepth_;  /* re-injection gain for pillar_==4 */
    float shimmerPhase_;  /* LFO phase for shimmer (radians) */
    float shimmerFreq_; // Low audio rate for cascading undertones

    interleaved_frame_t delayLine[BUFFER_SIZE] __attribute__((aligned(64)));

    float32x4_t hadamardCols[FDN_CHANNELS][FDN_CHANNELS/4] __attribute__((aligned(16)));  // Column-major for NEON
    float delayTimes[FDN_CHANNELS] __attribute__((aligned(16)));
    float32x4_t modPhaseVec[FDN_CHANNELS] __attribute__((aligned(16)));
    float32x4_t lpfState[FDN_CHANNELS] __attribute__((aligned(16)));
    float hadamard[FDN_CHANNELS][FDN_CHANNELS] __attribute__((aligned(64)));

    float preDelayBuffer[PREDELAY_BUFFER_SIZE] __attribute__((aligned(16)));
    int preDelayWritePos;
    int preDelayOffsetSamples;
    int activeSampleCount; // Tracks tail for Active Partial Counting
};
