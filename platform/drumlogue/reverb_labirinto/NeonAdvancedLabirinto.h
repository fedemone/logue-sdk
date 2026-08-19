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

// Number of FDN channels (must be multiple of 4 for NEON)
#define FDN_CHANNELS 8

// Delay line ring buffer; a power of two so wrapping is a mask.
// It costs BUFFER_SIZE * FDN_CHANNELS * 4 bytes, so the size is worth being
// honest about: 2^16 spent 2 MB holding a delay nothing could reach. 2^15 is
// 0.68 s at 48 kHz, against a longest reachable delay of one ping-pong bank at
// PINGPONG_MAX_MS * PINGPONG_MAX_SPREAD = 0.57 s. See the static assert below.
#define BUFFER_SIZE 32768  // 2^15
#define BUFFER_MASK (BUFFER_SIZE - 1)
#define FREQ_MAX_DIV_MIN (18.333f)
#define PREDELAY_BUFFER_SIZE 16384  // ~341ms at 48kHz
#define PREDELAY_MASK (PREDELAY_BUFFER_SIZE - 1)
#define NEON_LANES  (4)

// Mid-band RT60 range that TIME maps onto (seconds, exponential).
#define RT60_MIN_S   (0.25f)
#define RT60_MAX_S   (10.0f)
#define RT60_RATIO   (RT60_MAX_S / RT60_MIN_S)

// Ping-pong (PILL=1) bounce time range in milliseconds, set by BNCE. The
// bounce period is the delay-line length of one bank, so this directly sets how
// fast the tail jumps between speakers.
#define PINGPONG_MIN_MS   (60.0f)
#define PINGPONG_MAX_MS   (500.0f)
// Longest multiplier in the per-bank spread table in updateDelayTimes(), i.e.
// the longest delay line the engine can ever ask for is
// PINGPONG_MAX_MS * PINGPONG_MAX_SPREAD.
#define PINGPONG_MAX_SPREAD (1.147f)

// Limiter knee. Below this level the transfer is exactly y = x; above it the
// excess is soft-clipped, saturating at kLimitThreshold + (2/3)*(1-threshold).
// A knee matters here: the previous x/(1+|x|) limiter had no linear region at
// all, so it shortened the reverb tail in proportion to how loud it was.
#define kLimitThreshold (0.8f)
#define kLimitCeiling   (kLimitThreshold + 0.6666667f * (1.0f - kLimitThreshold))

// Delay-time slew coefficient, applied once per 4-sample block (~40 ms glide).
// Retuning the ping-pong bounce time therefore bends pitch like tape instead of
// clicking, matching the pre-delay behaviour.
#define DELAY_SLEW_COEFF (0.002f)

// The read pointer trails the write pointer by the delay length plus the
// modulation depth (at most 18 samples, from the esotico shimmer path). If that
// ever exceeded the ring buffer the read would wrap past the write head and
// replay stale audio, so pin the relationship here rather than trusting it.
static_assert(PINGPONG_MAX_MS * 0.001f * PINGPONG_MAX_SPREAD * 48000.0f + 64.0f
                  < (float)BUFFER_SIZE,
              "delay ring buffer is too small for the longest ping-pong bounce");

/**
 * OPTIMIZED: Interleaved frame structure for vld4q_f32
 * Stores all 8 FDN channels at a single time position
 * Format: [ch0, ch1, ch2, ch3, ch4, ch5, ch6, ch7]
 */
typedef struct __attribute__((aligned(16))) {
    float samples[FDN_CHANNELS];  // All 8 channels at this time position
} interleaved_frame_t;

typedef enum {
    k_foresta,
    k_tempio,
    k_labirinto,
    k_esotico,
    k_stellare,
    k_preset_number,
} preset_numer_t;


enum parameterState {
  k_paramProgram = 0,
  k_time, k_low, k_high, k_damp,
  k_wide, k_diffusion, k_pill, k_shimmer_freq,
  k_pre_delay, k_vibr, k_bounce,
  k_total
};

static const char *k_preset_names[k_preset_number] =
    {"foresta", "tempio", "labirinto", "esotico", "stellare"};
// ============================================================================
// Factory Presets
// ============================================================================
//    {PRESET,  TIME, LOW, HIGH, DAMP, WIDE, DFSN, PILL, SHMR, PDLY, VIBR, BNCE}
static const int32_t k_presets[k_preset_number][k_total] = {
    // 0: foresta - mellow, sparse, "wood" (warm lows, short, moderate decay)
    {k_foresta,   50,  60,   40,  220,   90,   50,    3,    0,    40,  15,  180},
    // 1: tempio  - sombre, "stone" (heavy lows, long, dark, 6-ch)
    {k_tempio,    70,  80,   25,  150,  150,   80,    2,   10,   10,  10,  180},
    // 2: labirinto - glassy tail bouncing between the speakers every BNCE ms
    {k_labirinto, 65,  50,   55,  510,  150,   50,    1,    0,   10,  30,  190},
    // 3: esotico - microtonal echoes on non-Western scale
    {k_esotico,   58,  30,   80,  800,  80,    50,    4,    45,    0,  30,  180},
    // 4: stellare - long, subtle, "spacey" shimmer (8-ch + shimmer)
    {k_stellare,  90,  70,   80,  520,  190,   8,    4,   35,   20,   5,  180},
};

// ============================================================================
// NEON helper: 4x4 float matrix transpose.
// Swaps the [channel][time] <-> [time][channel] layout for a group of 4
// channels. Used to run the per-channel IIR filters (sequential in time,
// independent across channels) 4-wide instead of one channel at a time.
// The operation is its own inverse, so the same routine transposes both ways.
// ============================================================================
static inline void neon_transpose4(const float32x4_t* in, float32x4_t* out) {
    float32x4x2_t a = vtrnq_f32(in[0], in[1]);
    float32x4x2_t b = vtrnq_f32(in[2], in[3]);
    out[0] = vcombine_f32(vget_low_f32(a.val[0]),  vget_low_f32(b.val[0]));
    out[1] = vcombine_f32(vget_low_f32(a.val[1]),  vget_low_f32(b.val[1]));
    out[2] = vcombine_f32(vget_high_f32(a.val[0]), vget_high_f32(b.val[0]));
    out[3] = vcombine_f32(vget_high_f32(a.val[1]), vget_high_f32(b.val[1]));
}

// ============================================================================
// Main Class
// ============================================================================

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
        , width(1.0f)
        , dampingCoeff(0.5f)
        , lowDecayMult(1.0f)
        , highDecayMult(0.5f)
        , initialized(false)
        , pillar_(3)
        , pingPong_(false)
        , shimmerDepth_(0.0f)
        , shimmerPhase_(0.0f)
        , shimmerFreq_ (35.0f)
        , preDelayWritePos(0)
        , currentPreDelaySamples(0.0f)
        , targetPreDelaySamples(0.0f)
        , currentPreset(-1)
        , lfoSpeed(1.0f)
        , randomLfoValue(0.0f)
        , randomLfoCounter(0)
        , randomLfoPeriodBlocks(48000 / NEON_LANES)   // 1 Hz at 48 kHz
        , smoothedLfoValue(0.0f)
        , filterMode(kFilterWood)
        , noiseSeed(132465798U)
        , noiseColour(2.0f)
        , noiseGain(0.0f)
        , noiseEnvelope(0.0f)
        , bounceTimeMs(180.0f)
        , unifiedDecay(0.5f)
        , lowBandGain(0.5f)
        , highBandGain(0.5f)
        , meanDelay(0.1415f)
        , inputDrive(0.4f)
        , filterUpdateCounter(0)
        , baseFc(1000.0f)
        , outputMakeup(1.0f) {

        updateDelayTimes();
        for (int i = 0; i < FDN_CHANNELS; i++)
            delayTimes[i] = targetDelayTimes[i];
        updateDecayGains();

        // Initialize modulation phases (store full vector per channel)
        for (int i = 0; i < FDN_CHANNELS; i++) {
	    // Initialize all 4 lanes to the same starting phase
            modPhaseVec[i] = vdupq_n_f32(0.0f);
            filterState1[i] = 0.0f;
            filterState2[i] = 0.0f;
        }

        // Initialize filter states
        memset(lpfState, 0, sizeof(float) * FDN_CHANNELS);
        memset(metalState, 0, sizeof(float) * FDN_CHANNELS);
        memset(crystalAPState, 0, sizeof(float) * FDN_CHANNELS);
        updateFilterCoeffs();

    }

    ~NeonAdvancedLabirinto() {
    }

    /**
     * Initialize the reverb with proper memory alignment
     * @return true if initialization successful
     */
    bool init() {

        // Clear buffer
        clear();

        // Initialize Cochrane Microtonal Shimmer (18-EDO Scale)
        initMicrotonalShimmer();

        // set state variables
        initialized = true;
        return true;
    }

    /**
     * Clear all delay lines and filter states
     */
    void clear() {
        // All-zero bits is 0.0f in IEEE 754, so one memset beats 65536 vector
        // stores here — and this runs on unit_reset()/unit_suspend(), not only
        // at startup.
        memset(delayLine, 0, sizeof(delayLine));

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

        memset(metalState, 0, sizeof(float) * FDN_CHANNELS);
        memset(crystalAPState, 0, sizeof(float) * FDN_CHANNELS);
        // Reset filter states
        memset(lpfState, 0, sizeof(float) * FDN_CHANNELS);
        for (int i = 0; i < FDN_CHANNELS; i++) {
            filterState1[i] = 0.0f;
            filterState2[i] = 0.0f;
        }

        // Reset pre delay line
        memset(preDelayBuffer, 0, sizeof(preDelayBuffer));
        memset(channelPhase_, 0, sizeof(channelPhase_));
        memset(swirlRate_, 0, sizeof(swirlRate_));
        memset(microtonalRate_, 0, sizeof(microtonalRate_));
        preDelayWritePos = 0;
        activeSampleCount = 0;
        // Snap the slewed delay lengths to their targets so a reset does not
        // glide in from wherever the previous patch left them.
        for (int i = 0; i < FDN_CHANNELS; i++)
            delayTimes[i] = targetDelayTimes[i];
        // Reset LFO
        randomLfoCounter = 0;
        randomLfoValue = 0.0f;
        filterUpdateCounter = 0;

        // Reset noise
        for (int i = 0; i < FDN_CHANNELS; i++) {
            noiseStates[i] = 0.0f;
            noiseStates2[i] = 0.0f;
        }
        noiseEnvelope = 0.0f;
    }

    /*===========================================================================*/
    /* Delay network geometry */
    /*===========================================================================*/

    /**
     * Pick the delay-line lengths for the current routing mode.
     *
     * Normal modes use the prime-ish spread that gives a dense, smooth tail.
     *
     * Ping-pong (PILL=1) needs the two banks to hand energy back and forth on a
     * regular beat, so both banks are clustered around the same bounce time with
     * only a small spread for diffusion. Bank A (channels 0-3, panned left) and
     * bank B (channels 4-7, panned right) are interleaved around that centre so
     * each hop takes about one bounce time in either direction.
     */
    void updateDelayTimes() {
        if (pillar_ == 1) {
            const float t = bounceTimeMs * 0.001f;
            // +-11% spread inside each bank: enough diffusion to avoid a flutter
            // echo, tight enough that the bank as a whole arrives together.
            static const float spread[FDN_CHANNELS] = {
                0.890f, 0.963f, 1.037f, 1.110f,             // bank A (left)
                0.926f, 1.000f, 1.074f, PINGPONG_MAX_SPREAD  // bank B (right)
            };
            for (int i = 0; i < FDN_CHANNELS; i++)
                targetDelayTimes[i] = t * spread[i];
        } else {
            static const float baseDelays[FDN_CHANNELS] = {
                0.0421f, 0.0713f, 0.0987f, 0.1249f,
                0.1571f, 0.1835f, 0.2127f, 0.2413f
            };
            for (int i = 0; i < FDN_CHANNELS; i++)
                targetDelayTimes[i] = baseDelays[i];
        }

        float sum = 0.0f;
        for (int i = 0; i < FDN_CHANNELS; i++) sum += targetDelayTimes[i];
        meanDelay = sum / (float)FDN_CHANNELS;
        updateDecayGains();
    }

    /**
     * Derive the per-pass feedback gains from the requested RT60.
     *
     * TIME sets a real mid-band RT60 (RT60_MIN_S..RT60_MAX_S). A signal makes
     * 1/meanDelay trips round the network per second, so the gain that decays it
     * by 60 dB in rt60 seconds is 10^(-3 * meanDelay / rt60).
     *
     * LOW and HIGH then stretch or shorten that RT60 per band (1.0 = neutral at
     * their centre position), and the band split happens on the DAMP crossover
     * inside the feedback loop. Previously both simply scaled the loop gain by
     * sqrt(low*high) <= 1, so mid settings always shortened the tail.
     */
    void updateDecayGains() {
        const float rt60 = RT60_MIN_S * fasterpowf(RT60_RATIO, decay);
        const float gMid = fasterpowf(10.0f, -3.0f * meanDelay / rt60);

        // Everything else in the loop that is not unity: cross-feedback raises
        // the eigenvalue to ~(1+crossGain), and some colour stages peak above 1
        // at their resonance. Divide the requested gain by that so the *total*
        // round trip is gMid and the RT60 actually comes out where TIME asked.
        // (Subtracting the cross-feedback instead, as before, left a per-pass
        // gain of 0.88*1.10 = 0.97 at long TIME settings — a 30 s tail from a
        // control that claims 10 s, and only a hair away from self-oscillation.)
        const float loopExtra = (1.0f + effectiveCrossGain()) * colourPeakGain();
        // 0.985 rather than 1.0: the dark modes get extra loss for free from
        // their lowpass, but crystal and noise have a genuinely unity-gain loop,
        // so without a margin here a long TIME plus a busy pattern piles up
        // until the saturator is the only thing holding it.
        const float ceiling   = 0.985f / loopExtra;

        // g^(1/mult): mult > 1 lengthens that band's RT60, mult < 1 shortens it.
        const float g = gMid / loopExtra;
        lowBandGain  = fminf(ceiling, fasterpowf(g, 1.0f / lowDecayMult));
        highBandGain = fminf(ceiling, fasterpowf(g, 1.0f / highDecayMult));
        unifiedDecay = fminf(0.995f, gMid);   // total round-trip gain, for drive

        // How hard to drive the network. A long tail recirculates more, so some
        // compensation is needed or TIME doubles as a volume control; but full
        // (1-g) normalisation is too much — it made a short reverb 5x louder at
        // the onset than a long one. (1-g^2)^0.4 splits the difference: the
        // onset drops a little as the tail lengthens, total energy rises a
        // little, which is how a bigger room actually behaves.
        const float gg = fminf(0.999f, unifiedDecay);
        inputDrive = fmaxf(0.14f, 0.55f * fasterpowf(1.0f - gg * gg, 0.4f));
    }

    /**
     * Bank-to-bank bleed for ping-pong, and plain cross-feedback for every other
     * routing mode.
     *
     * The bleed is what stops the quiet side going silent between bounces, but
     * it accumulates once per trip round the network — so at a fast bounce time
     * the tail makes twice as many trips per second and the alternation washes
     * out twice as fast. Scaling the bleed with the bounce time keeps the amount
     * of leakage *per second* roughly constant, so BNCE stays a crisp ping-pong
     * across its whole range instead of only at the slow end.
     */
    float effectiveCrossGain() const {
        if (pillar_ != 1) return crossGain[pillar_];
        return fmaxf(0.04f, fminf(0.16f, crossGain[1] * bounceTimeMs * (1.0f / 190.0f)));
    }

    /**
     * Worst-case gain the per-mode colour stage adds inside the feedback loop.
     * Used to keep the loop bounded whatever TIME asks for.
     */
    float colourPeakGain() const {
        switch (filterMode) {
            // Crystal adds 0.45 * a Q=1.2 bandpass on top of a unity dry path.
            // The bandpass peaks at alpha/(1+alpha) ~ 0.19 at the top of the
            // crystal fc range (plus LFO), so the sum peaks near 1.09. Keeping
            // that peak modest matters: the whole loop is scaled down by it, so
            // a showier resonance would cost esotico its decay time everywhere
            // except at the resonant frequency.
            case kFilterCrystal: return 1.09f;
            case kFilterNoise:   return 1.05f;  // noise injection headroom
            default:             return 1.0f;   // metal comb is normalised
        }
    }

    /*===========================================================================*/
    /* Parameter Setters/Getters */
    /*===========================================================================*/
    inline int getPreset() {
        return currentPreset;
    }

    inline void loadPreset(int32_t value) {
        if (currentPreset != value) {
            setFilterType(value);
            for (uint8_t i = 0; i < k_total; i++) {
                setParameter(i, k_presets[value][i]);
            }
            // DAMP is applied before DFSN in the loop above, so the noise gain
            // it derives from `diffusion` was computed from the previous
            // preset's value. Re-apply it now that everything else is in place.
            setDamping((float)params_[k_damp] * 10.0f);
        }
    }

    inline int32_t getParameterValue(uint8_t index) {
        if (index >= k_total) return -1;    // invalid value
        return params_[index];
    }

    inline void setParameter(uint8_t index, int32_t value) {
        if (index >= k_total) return;
        params_[index] = value;   // store into local DB

        switch (index) {
        case k_paramProgram:
          if (currentPreset != value) loadPreset(value);
            break;
        case k_time: // TIME  1..100 → decay 0.01..0.99
            setDecay(0.01f + (value - 1) / 99.0f * 0.98f);
            break;
        case k_low: // LOW  1..100 → low-freq decay multiplier
            setLowDecay((float)value);
            break;
        case k_high: // HIGH  1..100 → high-freq decay multiplier
            setHighDecay((float)value);
            break;
        case k_damp: // DAMP  20..1000 (×10 → 200..10000 Hz)
            setDamping((float)value * 10.0f);
            break;
        case k_wide: // WIDE  0..200 → stereo width 0.0..2.0
            setWidth(value * 0.01f);
            break;
        case k_diffusion: // DFSN  0..100 → diffusion 0.0..1.0
            setDiffusion(value * 0.01f);
            break;
        case k_pill: // PILL  0..4  - pillar routing mode
            setPillar(value);
            break;
        case k_shimmer_freq: // SHMR  0..100  - shimmer frequency
            setShimmerFreq(value);
            break;
        case k_pre_delay: // PDLY 0..200 ms
            setPreDelay((float)value);
            break;
        case k_vibr:
            // value 1..30 → 0.1..3.0 Hz
            setLfoSpeed(value * 0.1f);
            updateModRate();
            break;
        case k_bounce: // BNCE 60..500 ms - ping-pong bounce time
            setBounceTime(value);
            break;
        default:
        break;
        }
    }
    void setDecay(float d) {
        decay = fmaxf(0.0f, fminf(0.99f, d));
        updateDecayGains();
    }
    void setDiffusion(float d) {
        diffusion = fmaxf(0.0f, fminf(1.0f, d));
        if (filterMode == kFilterNoise) {
            // Map to 0=Brown, 1=Pink, 2=White, 3=Blue, 4=Violet, 5=Grey
            noiseColour = diffusion * 5.999f;
        }
        updateModDepth();
    }

    /**
     * Recompute everything derived from PILL and DFSN: the delay-modulation
     * depth, the shimmer re-injection gain, and the modulation rate that scales
     * with the depth.
     *
     * Both inputs feed all three, so both setters have to run the whole chain.
     * setPillar() used to read modDepth to derive shimmerDepth_ one line
     * *before* recomputing modDepth, so the shimmer gain came from the mode you
     * had just left; and setDiffusion() touched neither shimmerDepth_ nor
     * modRate, so DFSN did not move the shimmer at all and the modulation rate
     * kept a stale value until VIBR or PILL was next touched.
     */
    void updateModDepth() {
        // Ping-pong keeps the depth low: heavy delay-time wobble smears the
        // bounce it exists to make audible.
        const float pillarDepth = (pillar_ == 0) ? 0.6f :
                                  (pillar_ == 1) ? 0.15f :
                                  (pillar_ == 2) ? 0.2f : 0.1f;
        setModDepth(pillarDepth * diffusion);
        shimmerDepth_ = (pillar_ == 4) ? 0.4f * modDepth : 0.0f;
        updateModRate();
    }

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
        shimmerPhase_ = 0.0f;
        updateModDepth();     // depth, shimmer gain and mod rate all follow PILL
        updateDelayTimes();   // ping-pong uses its own bank geometry
    }
    void setModDepth(float d) { modDepth = fmaxf(0.0f, fminf(1.0f, d)); }
    void setModRate(float r) { modRate = fmaxf(0.1f, fminf(10.0f, r)); }
    // Recomputes modRate from scratch using current lfoSpeed and modDepth.
    // Uses a fixed base rate of 1.0 Hz so the result is always deterministic
    // regardless of how many times this is called.
    void updateModRate() { modRate = fmaxf(0.1f, fminf(10.0f, lfoSpeed * modDepth)); }
    // WIDE. Applied to the mid/side split at the output of both the vector and
    // the scalar path. Note it has no effect at PILL=0, which folds to mono.
    void setWidth(float w) { width = fmaxf(0.0f, fminf(2.0f, w)); }
    // 3 Hz to 8 Hz: Creates Cochrane's "microtonal beating" — a nervous, spicy, disconcerting chorusing.
    // 20 Hz to 55 Hz: Creates the "low pitching" cascade — thick, dark, metallic undertones that dive deeper as the reverb decays.
    void setShimmerFreq(float value) {
        // Normalize UI value to 0.0 -> 1.0
        float norm = fmaxf(0.0f, fminf(1.0f, (float)value * 0.01f));

        // Exponential mapping: min * (max/min)^norm
        // 3.0f * (55.0 / 3.0)^norm
        shimmerFreq_ = 3.0f * fasterpowf(FREQ_MAX_DIV_MIN, norm);
    }
    float getShimmerFreq() { return shimmerFreq_; }

    /**
     * Ping-pong bounce time in milliseconds (PILL=1). Sets how long the tail
     * spends on one side before crossing to the other; the value is the
     * delay-line length of a bank, so it is a real time, not an abstract index.
     * Ignored by the other routing modes, which have no banks to swap.
     */
    void setBounceTime(int32_t ms) {
        bounceTimeMs = fmaxf(PINGPONG_MIN_MS, fminf(PINGPONG_MAX_MS, (float)ms));
        if (pillar_ == 1) updateDelayTimes();
    }
    void setPreDelay(float ms) {
        // Clamp against the buffer rather than a magic 340 ms, keeping a block
        // of margin so the read pointer can never overtake the write head.
        const float maxSamples = (float)(PREDELAY_BUFFER_SIZE - 4 * NEON_LANES);
        targetPreDelaySamples =
            fmaxf(0.0f, fminf(maxSamples, ms * sampleRate * 0.001f));
    }
    void setDamping(float freqHz) {
        freqHz = fmaxf(200.0f, fminf(10000.0f, freqHz));
	    // omega = 2π * fc / fs;  coeff ≈ 1 - omega  (first-order approx)
        float omega = 2.0f * (float)M_PI * freqHz / sampleRate;
        dampingCoeff = e_expff(-omega);
        updateFilterCoeffs();   // recalc wood/stone/metal filters
        updateBaseFc();
        if (filterMode == kFilterNoise) {
            // Scale factor: steady-state noise level = noiseGain per channel.
            // Was 0.05 which made stellare's coloured noise ~0.005 = inaudible.
            // 0.35 makes it a usable wash; recirculation + soft-sat keep it bounded.
            noiseGain = diffusion * dampingCoeff * 0.35f;
        }
    }

    /**
     * Low-frequency RT60 multiplier, relative to the mid-band RT60 set by TIME.
     * 1..100 → 0.51..1.50, i.e. neutral (1.0) at the centre of the range so a
     * mid setting neither lengthens nor shortens the tail.
     */
    void setLowDecay(float value) {
        lowDecayMult = 0.5f + value * 0.01f;
        updateDecayGains();
    }

    /**
     * High-frequency RT60 multiplier, relative to the mid-band RT60. Same
     * neutral-at-centre mapping, slightly wider so a bright tail can outlast the
     * mids: 1..100 → 0.41..1.60. Higher value = brighter, longer treble tail.
     */
    void setHighDecay(float value) {
        highDecayMult = 0.4f + value * 0.012f;
        updateDecayGains();
    }

    /**
     * @brief Set the Lfo Speed object for modulation 0.1Hz -> 3Hz
     *
     * @param speedHz
     */
    void setLfoSpeed(float speedHz) {
        lfoSpeed = fmaxf(0.1f, fminf(3.0f, speedHz));
        // The counter is ticked once per NEON block, not once per sample, so the
        // period has to be in blocks. It was in samples, which made VIBR run four
        // times slower than the panel says: the advertised 0.1-3.0 Hz was really
        // delivering 0.025-0.75 Hz. (The other per-block smoothers in this file —
        // noiseEnvelope, DELAY_SLEW_COEFF — already account for the block rate.)
        randomLfoPeriodBlocks = (int)(sampleRate / lfoSpeed / (float)NEON_LANES);
        if (randomLfoPeriodBlocks < 1) randomLfoPeriodBlocks = 1;
    }

    void setFilterType(int preset) {
        currentPreset = preset;   // Fix: currentPreset must track the active preset
        switch (preset) {
            case k_foresta:    filterMode = kFilterWood;    break;
            case k_tempio:     filterMode = kFilterStone;   break;
            case k_labirinto:  filterMode = kFilterMetal;   break;  // glassy high-Q bandpass
            case k_esotico:    filterMode = kFilterCrystal; break;  // bright allpass-like shimmer
            case k_stellare:   filterMode = kFilterNoise;   break;
            default:           filterMode = kFilterWood;    break;
        }
        // Per-mode wet makeup. The dark LPF modes (wood/stone) and the noise mode
        // produce far less output than the resonant metal/crystal modes, so they
        // are leveled up here. Crystal (esotico) already sits at a good level via
        // its additive blend, so it is left near unity. Starting points for HW
        // calibration.
        switch (filterMode) {
            case kFilterWood:    outputMakeup = 2.7f; break;  // foresta
            case kFilterStone:   outputMakeup = 2.5f; break;  // tempio (darkest)
            case kFilterMetal:   outputMakeup = 2.4f; break;  // labirinto (tamed)
            case kFilterCrystal: outputMakeup = 3.0f; break;  // esotico (good as-is)
            case kFilterNoise:   outputMakeup = 2.6f; break;  // stellare
            default:             outputMakeup = 2.0f; break;
        }
        updateBaseFc();
        updateDecayGains();   // in-loop colour headroom is mode dependent
    }

    void updateBaseFc() {
        // compute base cutoff frequency from preset and dampingCoeff
        float fc;
        switch (filterMode) {
            case kFilterWood:
                fc = 1000.0f + dampingCoeff * 7000.0f;   // 1000..8000 Hz
                break;
            case kFilterStone:
                fc = 80.0f + dampingCoeff * 4920.0f;    // 80..5000 Hz
                break;
            case kFilterMetal:
                // 300..1300 Hz: covers kick body and snare fundamentals so
                // drums with energy in this range trigger the metallic resonance.
                fc = 300.0f + dampingCoeff * 1000.0f;
                break;
            case kFilterCrystal:
                // 1000..3500 Hz: snare crack / hi-hat body. Stays below DAMP LPF
                // even at moderate DAMP settings so the crystal character survives.
                fc = 1000.0f + dampingCoeff * 2500.0f;
                break;
            case kFilterNoise:
                fc = 5000.0f + dampingCoeff * 10000.0f;
                break;
            default:
                fc = 1000.0f;
                break;
        }
        baseFc = fc;
        updateFilterCoeffs();   // initial coefficients
    }

    /*===========================================================================*/
    /* Advanced features */
    /*===========================================================================*/

    void updateFilterCoeffsAt(float fc) {
        float w0 = 2.0f * M_PI * fc / sampleRate;
        float cos_w0 = fastercosfullf(w0);
        float sin_w0 = fastersinfullf(w0);

        float alpha, b0, b1, b2, a0, a1, a2;

        // peaking filter (band‑pass with gain)
        // For simplicity, we'll use a standard biquad peaking:
        // H(s) = (s^2 + s*(ω0/Q) + ω0^2) / (s^2 + s*(ω0/Q) + ω0^2)
        // Actually we want a peaking EQ, but a simple 2‑pole band‑pass works well.
        // We'll implement a classic band‑pass with gain at centre frequency.
        // Coefficients from RBJ cookbook.
        // standard Direct Form Biquad calculates its output using this mathematical formula:
        // y[n] = (b0 * x[n]) + (b1 * x[n-1]) + (b2 * x[n-2]) - (a1 * y[n-1]) - (a2 * y[n-2])
        /** The Micro-Boost "Compounding" Peaking EQ (Keep Inside Loop)
         * Instead of a bandpass filter that aggressively cuts the background,
         * you can use a standard RBJ Peaking EQ, but configured with a microscopic
         * boost (e.g., $+0.13\text{ dB}$) and then normalized so its peak gain is
         *  exactly $1.0$.
         * Because the FDN loop recirculates the signal dozens of times,
         * you don't need a heavy filter. A tiny difference compounds over time.
         * If the baseline gain is set to 0.985 (retaining 98.5% of tail energy per pass)
         * and the peak is pinned at 1.0, after 50 reflections the resonant frequency
         *  remains at $1.0^{50} = 1.0$, while the background tail drops to $0.985^{50}
         * \approx 0.46$. This creates a massive, clear contrast between the metallic
         * ring and the tail, without choking the reverb or self-oscillating. */
        if (filterMode == kFilterMetal) {
            // Pinned unity peak gain with maximum tail preservation.
            // Baseline frequencies lose only 1.5% energy per pass, allowing a huge tail.
            float baselineGain = 0.985f;
            float A = 1.0f / baselineGain; // Micro-boost amount (~0.13 dB)
            float Q = 4.0f;                // High selectivity for glassy ringing
            alpha = sin_w0 / (2.0f * Q);

            b0 = 1.0f + alpha * A;
            b1 = -2.0f * cos_w0;
            b2 = 1.0f - alpha * A;
            a0 = 1.0f + alpha / A;
            a1 = -2.0f * cos_w0;
            a2 = 1.0f - alpha / A;

            float invA0 = 1.0f / a0;
            // Normalize the feed-forward coefficients by (1 / A)
            // This scales the peak gain down from A to exactly 1.0
            float norm = baselineGain * invA0;

            biquadA0 = b0 * norm;
            biquadA1 = b1 * norm;
            biquadA2 = b2 * norm;
            biquadB1 = a1 * invA0;
            biquadB2 = a2 * invA0;
        }
        else if (filterMode == kFilterCrystal) {
            // CRYSTAL: Medium-Q Bandpass at higher frequencies — used by esotico.
            // Q=3 gives a broader peak than METAL, suitable for microtonal shimmer.
            // The higher fc range (2-7 kHz) keeps the tail bright and airy without
            // the harsh metallic ring, blending with the microtonal modulation path.
            float Q = 1.2f;
            alpha = sin_w0 / (2.0f * Q);

            b0 = alpha;
            b1 = 0.0f;
            b2 = -alpha;
            a0 = 1.0f + alpha;
            a1 = -2.0f * cos_w0;
            a2 = 1.0f - alpha;
        }
        else if (filterMode == kFilterWood) {
            // WOOD: Warm, highly damped Lowpass
            float Q = 0.5f;
            alpha = sin_w0 / (2.0f * Q);

            b0 = (1.0f - cos_w0) / 2.0f;
            b1 = 1.0f - cos_w0;
            b2 = (1.0f - cos_w0) / 2.0f;
            a0 = 1.0f + alpha;
            a1 = -2.0f * cos_w0;
            a2 = 1.0f - alpha;
        }
        else {
            // STONE / DEFAULT: Heavier Butterworth Lowpass
            float Q = 0.707f;
            alpha = sin_w0 / (2.0f * Q);

            b0 = (1.0f - cos_w0) / 2.0f;
            b1 = 1.0f - cos_w0;
            b2 = (1.0f - cos_w0) / 2.0f;
            a0 = 1.0f + alpha;
            a1 = -2.0f * cos_w0;
            a2 = 1.0f - alpha;
        }

        // ==========================================================
        // UNIFIED NORMALIZATION (Safely prepares coeffs for the DSP loop)
        // ==========================================================
        float invA0 = 1.0f / a0;

        // Feed-forward coefficients
        biquadA0 = b0 * invA0;
        biquadA1 = b1 * invA0;
        biquadA2 = b2 * invA0;

        // Feedback coefficients - DO NOT NEGATE HERE!
        // The DSP loop explicitly uses subtraction: `- (out * biquadB1)`
        biquadB1 = a1 * invA0;
        biquadB2 = a2 * invA0;
    }

    void updateFilterCoeffs() { updateFilterCoeffsAt(baseFc); }

    void updateRandomLfo() {
        if (--randomLfoCounter <= 0) {
            randomLfoCounter = randomLfoPeriodBlocks;
            randomLfoValue = randomFloat() * 2.0f - 1.0f;
        }
        // One-pole smoothing to prevent filter pops when the random value steps.
        // 0.001 per block at 48 kHz is a ~83 ms glide, comfortably shorter than
        // the 333 ms period at the fastest VIBR setting.
        smoothedLfoValue += 0.001f * (randomLfoValue - smoothedLfoValue);
    }

    // basic cyclic pseudo-random values
    float randomFloat() {
        noiseSeed ^= noiseSeed << 13;
        noiseSeed ^= noiseSeed >> 17;
        noiseSeed ^= noiseSeed << 5;
        return (float)noiseSeed / (float)0xFFFFFFFFU;
    }

    void addColouredNoise(float32x4_t* signals, int numChannels, float gainScale = 0.5f) {
        if (filterMode != kFilterNoise || noiseGain < 0.001f) return;

        for (int ch = 0; ch < numChannels; ch++) {
            float n_out[4];
            for (int i = 0; i < 4; i++) {
                float n = randomFloat() * 2.0f - 1.0f; // Raw white noise

                if (noiseColour < 0.5f) {
                    // 0: Brown (Heavy LPF)
                    noiseStates[ch] = 0.95f * noiseStates[ch] + 0.05f * n;
                    n_out[i] = noiseStates[ch];
                } else if (noiseColour < 1.5f) {
                    // 1: Pink (Mild LPF Approximation)
                    noiseStates[ch] = 0.5f * noiseStates[ch] + 0.5f * n;
                    n_out[i] = noiseStates[ch] * 1.5f; // Gain makeup
                } else if (noiseColour < 2.5f) {
                    // 2: White (Flat)
                    n_out[i] = n;
                } else if (noiseColour < 3.5f) {
                    // 3: Blue (Mild HPF - difference of mild LPF)
                    float diff = n - noiseStates[ch];
                    noiseStates[ch] = 0.5f * noiseStates[ch] + 0.5f * n;
                    n_out[i] = diff * 0.8f;
                } else if (noiseColour < 4.5f) {
                    // 4: Violet (Heavy HPF - pure derivative)
                    float diff = n - noiseStates[ch];
                    noiseStates[ch] = n;
                    n_out[i] = diff * 0.5f;
                } else {
                    // 5: Grey (Notch filter around 2kHz using z^-2)
                    // y[n] = x[n] + x[n-2] creates a comb notch
                    n_out[i] = (n + noiseStates2[ch]) * 0.778f;
                    noiseStates2[ch] = noiseStates[ch];
                    noiseStates[ch] = n;
                }
            }
            // Inject the colored noise into the FDN channel
            float32x4_t noise = vld1q_f32(n_out);
            signals[ch] = vmlaq_n_f32(signals[ch], noise, noiseGain * gainScale);
        }
    }

    void applyCrossFeedback(float32x4_t* signals) {
        float gain = effectiveCrossGain();
        if (gain < 0.001f) return;

        float32x4_t t0, t1;
        switch (pillar_) {
            case 0: { // 2 channels — simultaneous update
                t0 = signals[0]; t1 = signals[1];
                signals[0] = vaddq_f32(t0, vmulq_n_f32(t1, gain));
                signals[1] = vaddq_f32(t1, vmulq_n_f32(t0, gain));
                break;
            }
            case 1: { // ping-pong — bleed each bank into its opposite number
                // Couples across the L/R banks (0<->4, 1<->5, ...) rather than
                // inside one bank. This is the only path that puts a little of
                // the left tail on the right, so it softens a hard bounce into a
                // musical one instead of leaving the far side silent.
                for (int i = 0; i < 4; i++) {
                    t0 = signals[i]; t1 = signals[i+4];
                    signals[i]   = vaddq_f32(t0, vmulq_n_f32(t1, gain));
                    signals[i+4] = vaddq_f32(t1, vmulq_n_f32(t0, gain));
                }
                break;
            }
            case 2: { // 6 channels — 2 independent 3-way rings
                for (int i = 0; i < 6; i += 3) {
                    float32x4_t t2 = signals[i+2];
                    t0 = signals[i]; t1 = signals[i+1];
                    signals[i]   = vaddq_f32(t0, vmulq_n_f32(t1, gain));
                    signals[i+1] = vaddq_f32(t1, vmulq_n_f32(t2, gain));
                    signals[i+2] = vaddq_f32(t2, vmulq_n_f32(t0, gain));
                }
                break;
            }
            default: { // 8 channels — 2 independent 4-way rings
                for (int i = 0; i < 8; i += 4) {
                    float32x4_t t2 = signals[i+2], t3 = signals[i+3];
                    t0 = signals[i]; t1 = signals[i+1];
                    signals[i]   = vaddq_f32(t0, vmulq_n_f32(t2, gain));
                    signals[i+2] = vaddq_f32(t2, vmulq_n_f32(t0, gain));
                    signals[i+1] = vaddq_f32(t1, vmulq_n_f32(t3, gain));
                    signals[i+3] = vaddq_f32(t3, vmulq_n_f32(t1, gain));
                }
                break;
            }
        }
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

        // enable Flush-to-Zero (FTZ) and Default-NaN (DN) mode in the
        // ARM Floating Point Status and Control Register (FPSCR)
        #if defined(__arm__) || defined(__aarch64__)
            uint32_t fpscr;
            __asm__ volatile("vmrs %0, fpscr" : "=r"(fpscr));
            fpscr |= (1 << 24) | (1 << 22); // Set FZ (bit 24) and DN (bit 22)
            __asm__ volatile("vmsr fpscr, %0" : : "r"(fpscr));
        #endif

	    // Safety check
        if (!initialized) {
            memcpy(outL, inL, numSamples * sizeof(float));
            memcpy(outR, inR, numSamples * sizeof(float));
            return;
        }

        // The host normally hands us frames_per_buffer, but the SDK allows
        // smaller counts, so anything from 1 frame up has to work.
        int samplesProcessed = 0;
        while (samplesProcessed < numSamples) {
            const int remaining = numSamples - samplesProcessed;
            if (remaining >= NEON_LANES) {
                process4Samples(inL + samplesProcessed, inR + samplesProcessed,
                                outL + samplesProcessed, outR + samplesProcessed);
                samplesProcessed += NEON_LANES;
            } else {
                processPartial(inL + samplesProcessed, inR + samplesProcessed,
                               outL + samplesProcessed, outR + samplesProcessed,
                               remaining);
                samplesProcessed += remaining;
            }
        }
    }

private:
    /*===========================================================================*/
    /* OPTIMIZED: vld4-based delay line reading with vectorized interpolation */
    /*===========================================================================*/
    void readDelayLines4(float32x4_t* out) {
        // Calculate base read positions for all 4 samples
        float32x4_t baseReadPos = {
            (float)writePos,
            (float)(writePos + 1),
            (float)(writePos + 2),
            (float)(writePos + 3)
        };
        float32x4_t mods[FDN_CHANNELS];
        // DELAY READ POINTER CALCULATIONS
        float32x4_t readPositions[FDN_CHANNELS];
        uint32_t baseIndices[FDN_CHANNELS][NEON_LANES];
        float fracParts[FDN_CHANNELS][NEON_LANES];

        // ====================================================================
        // DYNAMIC NEON MODULATION (SWIRL & COCHRANE SHIMMER)
        // ====================================================================

        for (int ch = 0; ch < FDN_CHANNELS; ch++) {

            // 1. Select Rate and Depth based on current Mode.
            //    Rates are in normalized phase per sample: swirlRate_ is 0.2-0.9 Hz
            //    and microtonalRate_ is the 18-EDO set, both already divided by
            //    the sample rate. The default used to be a literal 0.5 — half a
            //    cycle per sample, i.e. an LFO running at Nyquist, which is not a
            //    swirl at all but a +-1 sample alternation on the read pointer.
            //    Every preset except esotico/stellare got that instead of chorus.
            float current_rate  = swirlRate_[ch] * (1.0f + modRate);
            float current_depth = modDepth * 4.7f;

            // use shimmer only on the last two presets, not available for all
            if (currentPreset == k_esotico) {
                // Cochrane 18-EDO Shimmer Mode
                current_rate = microtonalRate_[ch];
                current_depth = shimmerDepth_ * 45.0f; // Deep, slow microtonal stretch
            }
            // Noise mode used to overwrite the depth here with
            // `noiseColour * 5.999f`, on the stated grounds that "modulation
            // controls noise colour instead of delay time" — but current_depth
            // *is* the delay modulation depth in samples, and noiseColour is
            // already DFSN * 5.999 (setDiffusion), so the expression squared it
            // and handed stellare up to 36 samples of undocumented delay wobble.
            // Noise colour is set where it belongs; the swirl here is the same
            // one every other mode gets.
            if (filterMode == kFilterCrystal) {
                current_depth = modDepth * 10.0f;
            }

            // 2. Generate the 4 sequential phases for this NEON block
            float base = channelPhase_[ch];
            float32x4_t phase_offsets = { 0.0f, current_rate, current_rate * 2.0f, current_rate * 3.0f };
            float32x4_t phases = vaddq_f32(vdupq_n_f32(base), phase_offsets);

            // 3. Advance the stored scalar phase for the next audio block
            float next_phase = base + (current_rate * 4.0f);
            if (next_phase > 1.0f) next_phase -= 1.0f; // Wrap 0.0 to 1.0
            channelPhase_[ch] = next_phase;

            // 4. Convert normalized phase [0, 1] to Radians [0, 2π]
            float32x4_t angles = vmulq_f32(phases, vdupq_n_f32(M_PI * 2.0f));

            // 5. Compute NEON sine approximation
            float32x4_t sin_vals = sin_ps(angles);

            // 6. Scale by modulation depth (in samples)
            mods[ch] = vmulq_f32(sin_vals, vdupq_n_f32(current_depth));
        // }

        // ====================================================================
        // DELAY READ POINTER CALCULATIONS
        // ====================================================================

        // for (int ch = 0; ch < FDN_CHANNELS; ch++) {
            float delaySamples = delayTimes[ch] * sampleRate;

            // Subtract delay length AND our new dynamic modulation
            readPositions[ch] = vsubq_f32(baseReadPos,
                vaddq_f32(vdupq_n_f32(delaySamples), mods[ch]));

            // Wrap to [0, BUFFER_SIZE)
            float pos_vals[NEON_LANES];
            vst1q_f32(pos_vals, readPositions[ch]);
            float out_lanes[NEON_LANES];
            for (int s = 0; s < NEON_LANES; s++) {
                // Bias positive before the cast to avoid C++ truncation-toward-
                // zero on negatives. One BUFFER_SIZE is enough and no more than
                // enough: the static assert at the top of this file guarantees
                // delay + modulation < BUFFER_SIZE. The bias used to be four
                // times larger, which cost precision for nothing — a float has
                // 24 mantissa bits, so at 131072 the fractional part of the read
                // position quantises to 1/32 of a sample. At BUFFER_SIZE the
                // interpolation gets 1/128.
                float pos = pos_vals[s];
                float safe_pos = pos + (float)BUFFER_SIZE;

                int32_t idx = (int32_t)safe_pos;
                uint32_t base = idx & BUFFER_MASK;
                // Store the wrapped index so we can read from it!
                baseIndices[ch][s] = base;
                fracParts[ch][s] = safe_pos - (float)idx;
            // }
        // }

        // Read each channel independently: each channel has its own read position
        // (baseIndices[ch][s]) so we cannot share a single vld4q_f32 across channels.
        // Scalar interpolation per channel avoids the previous cross-frame read bug.
        // for (int ch = 0; ch < FDN_CHANNELS; ch++) {

            // for (int s = 0; s < NEON_LANES; s++) {
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

    inline void applyHadamard4(const float32x4_t* in, float32x4_t* out) {
        // Pass 1: Mix pairs 4 channels apart (Reads directly from 'in')
        float32x4_t t0 = vaddq_f32(in[0], in[4]); float32x4_t t4 = vsubq_f32(in[0], in[4]);
        float32x4_t t1 = vaddq_f32(in[1], in[5]); float32x4_t t5 = vsubq_f32(in[1], in[5]);
        float32x4_t t2 = vaddq_f32(in[2], in[6]); float32x4_t t6 = vsubq_f32(in[2], in[6]);
        float32x4_t t3 = vaddq_f32(in[3], in[7]); float32x4_t t7 = vsubq_f32(in[3], in[7]);

        // Pass 2: Mix pairs 2 channels apart
        float32x4_t u0 = vaddq_f32(t0, t2); float32x4_t u2 = vsubq_f32(t0, t2);
        float32x4_t u1 = vaddq_f32(t1, t3); float32x4_t u3 = vsubq_f32(t1, t3);
        float32x4_t u4 = vaddq_f32(t4, t6); float32x4_t u6 = vsubq_f32(t4, t6);
        float32x4_t u5 = vaddq_f32(t5, t7); float32x4_t u7 = vsubq_f32(t5, t7);

        // Pass 3: Mix adjacent channels & scale (Writes directly to 'out')
        // NOTE: ). In an FDN reverb, the feedback matrix must be unitary (gain of 1.0) to ensure stability.
        // With a gain of 1.414, the reverb loop will become unstable and explode whenever the unifiedDecay
        // (loop gain) exceeds ~0.707.
        float32x4_t scale = vdupq_n_f32(0.35355339f); // 1.0 / sqrt(8)
        out[0] = vmulq_f32(vaddq_f32(u0, u1), scale); out[1] = vmulq_f32(vsubq_f32(u0, u1), scale);
        out[2] = vmulq_f32(vaddq_f32(u2, u3), scale); out[3] = vmulq_f32(vsubq_f32(u2, u3), scale);
        out[4] = vmulq_f32(vaddq_f32(u4, u5), scale); out[5] = vmulq_f32(vsubq_f32(u4, u5), scale);
        out[6] = vmulq_f32(vaddq_f32(u6, u7), scale); out[7] = vmulq_f32(vsubq_f32(u6, u7), scale);
    }

    /*===========================================================================*/
    /* Ping-pong feedback matrix (PILL=1)                                        */
    /*                                                                           */
    /* The 8-point Hadamard mixes every channel into every other one on every    */
    /* pass, which is exactly what a diffuse reverb wants — and exactly why the  */
    /* old ping-pong was inaudible: after one pass both halves of the network    */
    /* carry the same signal, so no amount of clever output panning can make the */
    /* tail alternate sides. Panning was all the old code did, with a random     */
    /* channel->side map reshuffled every 100 ms, which reads as stereo jitter.  */
    /*                                                                           */
    /* Here the alternation is built into the topology instead. Channels 0-3 are */
    /* the left bank and 4-7 the right bank. Each bank is mixed with its own     */
    /* orthonormal 4-point Hadamard and then written into the *opposite* bank's  */
    /* delay lines, so energy physically crosses the stereo field once per       */
    /* bounce time and comes back one bounce later:                              */
    /*                                                                           */
    /*     write[A] = H4 * read[B]        write[B] = H4 * read[A]                */
    /*                                                                           */
    /* The block matrix [[0,H4],[H4,0]] is orthogonal for orthonormal H4, so the */
    /* network stays lossless and the stability analysis is unchanged.           */
    /*===========================================================================*/
    inline void applyPingPongMatrix(const float32x4_t* in, float32x4_t* out) {
        const float32x4_t scale = vdupq_n_f32(0.5f);   // 1/sqrt(4)

        for (int bank = 0; bank < 2; bank++) {
            const float32x4_t* s = &in[bank * 4];

            // 4-point fast Walsh-Hadamard transform of this bank.
            float32x4_t t0 = vaddq_f32(s[0], s[2]), t2 = vsubq_f32(s[0], s[2]);
            float32x4_t t1 = vaddq_f32(s[1], s[3]), t3 = vsubq_f32(s[1], s[3]);

            // ...written into the other bank's delay lines.
            float32x4_t* d = &out[(1 - bank) * 4];
            d[0] = vmulq_f32(vaddq_f32(t0, t1), scale);
            d[1] = vmulq_f32(vsubq_f32(t0, t1), scale);
            d[2] = vmulq_f32(vaddq_f32(t2, t3), scale);
            d[3] = vmulq_f32(vsubq_f32(t2, t3), scale);
        }
    }

    /*===========================================================================*/
    /* NEON channel-parallel IIR filter chain                                    */
    /*                                                                           */
    /* Processes one group of 4 FDN channels (c0..c0+3). The signals arrive in   */
    /* [channel][time] layout (mixed[ch] = 4 consecutive time samples). Each IIR */
    /* filter has a loop-carried dependency along TIME but the channels are       */
    /* independent, so we transpose to [time][channel] and evaluate all 4         */
    /* channels of each time step in a single NEON op.                            */
    /*                                                                           */
    /* Stages, in order: frequency-dependent decay (always), resonant biquad (all */
    /* modes but noise), metal comb (metal), crystal allpass (crystal). The       */
    /* resonant filter creates the spectral emphasis, the comb turns it metallic, */
    /* then the allpass diffuses it.                                              */
    /*===========================================================================*/
    inline void applyChannelFilters4Group(float32x4_t* mixed, int c0,
                                          bool doBiquad,
                                          bool doMetal, bool doCrystal) {
        // [channel][time] -> [time][channel]
        float32x4_t T[NEON_LANES];
        neon_transpose4(&mixed[c0], T);

        // ---- Frequency-dependent decay (always on) ----
        // The 1-pole splits each channel at the DAMP crossover into a low band
        // (the filter state) and a high band (what the filter rejected), then
        // applies the per-band feedback gain that realises the LOW / HIGH RT60.
        // This replaces both the old flat damping stage and the separate global
        // `mixed[i] *= unifiedDecay` multiply: one pass, and DAMP/LOW/HIGH now
        // mean what the parameter guide says they mean.
        {
            const float alpha = 1.0f - dampingCoeff;
            float32x4_t st = vld1q_f32(&lpfState[c0]);
            for (int t = 0; t < NEON_LANES; t++) {
                float32x4_t d = vsubq_f32(T[t], st);
                st = vmlaq_n_f32(st, d, alpha);
                float32x4_t high = vsubq_f32(T[t], st);
                T[t] = vmlaq_n_f32(vmulq_n_f32(st, lowBandGain), high, highBandGain);
            }
            vst1q_f32(&lpfState[c0], st);
        }

        // ---- Resonant biquad (Direct Form II Transposed), all modes but noise ----
        if (doBiquad) {
            float32x4_t fs1 = vld1q_f32(&filterState1[c0]);
            float32x4_t fs2 = vld1q_f32(&filterState2[c0]);
            for (int t = 0; t < NEON_LANES; t++) {
                float32x4_t in  = T[t];
                float32x4_t out = vmlaq_n_f32(fs1, in, biquadA0);         // in*A0 + fs1
                fs1 = vmlaq_n_f32(fs2, in,  biquadA1);                    // fs2 + in*A1
                fs1 = vmlsq_n_f32(fs1, out, biquadB1);                    //   - out*B1
                fs2 = vmulq_n_f32(in,  biquadA2);                         // in*A2
                fs2 = vmlsq_n_f32(fs2, out, biquadB2);                    //   - out*B2
                // CRYSTAL: the bandpass on its own has a peak gain of only
                // ~0.1, so the old convex blend (0.7*in + 0.3*bp) passed just
                // 0.73 of the signal per trip round the loop — a 27% loss that
                // no TIME setting could recover. Add the resonance on top of a
                // unity dry path instead: flat off-resonance, a small bump at fc.
                // METAL used to blend 0.82*dry + 0.18*biquad here, but only on
                // the one block in eight where the coefficients had just been
                // re-derived — the same flag did double duty as "recompute" and
                // "blend". That switched the signal path on and off at
                // 48000/32 = 1500 Hz, a buzz on the preset this whole unit is
                // named after. The blend also fought the filter's own design:
                // the peaking EQ is deliberately pinned to a unity peak over a
                // 0.985 baseline so the resonance compounds over many passes,
                // and diluting it to 18% flattens that contrast to nothing.
                // So the biquad output now stands on its own, every block.
                if (filterMode == kFilterCrystal)
                    out = vmlaq_n_f32(in, out, 0.45f);
                T[t] = out;
            }
            vst1q_f32(&filterState1[c0], fs1);
            vst1q_f32(&filterState2[c0], fs2);
        }

        // ---- Metal comb (metal mode only): x = (x + z^-1 * 0.18) / 1.18 ----
        // Normalised to unity peak gain. Unnormalised it peaked at 1.18 inside
        // the feedback loop, which is why metal mode used to need a blanket
        // `unifiedDecay *= 0.85` — that trim capped labirinto's RT60 well below
        // every other preset's. The comb colour is identical; only the level is.
        if (doMetal) {
            const float kNorm = 1.0f / 1.18f;
            float32x4_t st = vld1q_f32(&metalState[c0]);
            for (int t = 0; t < NEON_LANES; t++) {
                float32x4_t d = st;
                st = T[t];
                T[t] = vmulq_n_f32(vmlaq_n_f32(T[t], d, 0.18f), kNorm);
            }
            vst1q_f32(&metalState[c0], st);
        }

        // ---- Crystal allpass (crystal mode only): y=-g*x+s; s=x+g*y ----
        if (doCrystal) {
            const float g = 0.35f;
            float32x4_t st = vld1q_f32(&crystalAPState[c0]);
            for (int t = 0; t < NEON_LANES; t++) {
                float32x4_t in = T[t];
                float32x4_t y  = vmlsq_n_f32(st, in, g);                  // st - g*in
                st = vmlaq_n_f32(in, y, g);                               // in + g*y
                T[t] = y;
            }
            vst1q_f32(&crystalAPState[c0], st);
        }

        // [time][channel] -> [channel][time]
        neon_transpose4(T, &mixed[c0]);
    }

    inline void applyExoticShimmer(float32x4_t* mixed) {
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
            // Those multipliers (1.0f, 2.0f, 3.0f) are not artistic DSP parameters. They are strict structural math representing the linear flow of time.
            // Instead of moving smoothly through the delay buffer, your 4 audio samples will be randomly grabbed from completely different, disconnected points in the pitch-shifter's memory.
            // possible alternative
            // Makes the shimmer warble slightly like light through a prism
            // float lfo_wobble = sinf(some_lfo_phase) * 0.05f;
            // float inc = 2.0f + lfo_wobble;
            float32x4_t phaseVec = {
                shimmerPhase_,
                shimmerPhase_ + inc,
                shimmerPhase_ + 2.0f * inc,
                shimmerPhase_ + 3.0f * inc
            };

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
    }

    /**
     * Hard-knee soft limiter. Exactly y = x for |x| <= kLimitThreshold; above
     * the knee the excess is shaped by u - u^3/3, which leaves the slope at 1
     * where it joins and flattens out at kLimitCeiling. Never expands, so it
     * cannot destabilise the feedback loop, and unlike a knee-less curve it
     * does not quietly attenuate the reverb tail at normal levels.
     */
    inline void softClipN(float32x4_t* signals, int n) {
        const float32x4_t thr  = vdupq_n_f32(kLimitThreshold);
        const float32x4_t zero = vdupq_n_f32(0.0f);
        const float32x4_t one  = vdupq_n_f32(1.0f);
        const float range = 1.0f - kLimitThreshold;
        const float invRange = 1.0f / range;

        for (int i = 0; i < n; i++) {
            float32x4_t x    = signals[i];
            float32x4_t a    = vabsq_f32(x);
            float32x4_t over = vmaxq_f32(vsubq_f32(a, thr), zero);
            // u - u^3/3 on the normalised excess: slope 1 at the knee, flat at
            // u = 1 where it reaches 2/3. Clamp u at 1, not at the sqrt(3) root
            // of the cubic — past u = 1 the curve turns over and comes back
            // down, which would fold loud peaks back toward the threshold.
            float32x4_t u    = vminq_f32(vmulq_n_f32(over, invRange), one);
            float32x4_t comp = vmulq_n_f32(
                vmlsq_n_f32(u, vmulq_f32(vmulq_f32(u, u), u), 1.0f / 3.0f), range);
            float32x4_t mag  = vaddq_f32(vminq_f32(a, thr), comp);
            // restore sign without a select: mag * (x / |x|) is undefined at 0,
            // so scale the original instead, guarding the divide.
            float32x4_t safeA = vmaxq_f32(a, vdupq_n_f32(1e-20f));
            float32x4_t rcp   = vrecpeq_f32(safeA);
            rcp = vmulq_f32(vrecpsq_f32(safeA, rcp), rcp);
            rcp = vmulq_f32(vrecpsq_f32(safeA, rcp), rcp);
            signals[i] = vmulq_f32(x, vmulq_f32(mag, rcp));
        }
    }
    inline void applySoftSaturation(float32x4_t* signals) { softClipN(signals, FDN_CHANNELS); }
    inline void softClipPair(float32x4_t* signals)        { softClipN(signals, 2); }


    /**
     * Glide the live delay lengths toward their targets, once per block. A step
     * change would jump the read pointer and click; this bends pitch instead,
     * the same trick the pre-delay uses.
     */
    inline void slewDelayTimes() {
        for (int i = 0; i < FDN_CHANNELS; i++)
            delayTimes[i] += DELAY_SLEW_COEFF * (targetDelayTimes[i] - delayTimes[i]);
    }

    /*===========================================================================*/
    /* Vectorized Delay Line Write */
    /*===========================================================================*/
    inline void writeDelayLines4(const float32x4_t* signals) {
        // signals are [channel][time]; the delay line stores [time].samples[channel].
        // Transpose to [time][channel] so each time slice (8 channels) is written
        // with two aligned vector stores instead of 32 scalar scatter-stores.
        float32x4_t tLo[NEON_LANES], tHi[NEON_LANES];
        neon_transpose4(&signals[0], tLo);   // tLo[s] = channels 0..3 at time s
        neon_transpose4(&signals[4], tHi);   // tHi[s] = channels 4..7 at time s

        for (int s = 0; s < NEON_LANES; s++) {
            uint32_t pos = (writePos + s) & BUFFER_MASK;
            vst1q_f32(&delayLine[pos].samples[0], tLo[s]);
            vst1q_f32(&delayLine[pos].samples[4], tHi[s]);
        }
        writePos = (writePos + NEON_LANES) & BUFFER_MASK;
    }

    /*===========================================================================*/
    /* Main Processing Loop */
    /*===========================================================================*/

    void process4Samples(const float* inL, const float* inR,
                         float* outL, float* outR) {

        // Load 4 input samples for L and R channels
        float32x4_t inL4 = vld1q_f32(inL);
        float32x4_t inR4 = vld1q_f32(inR);

        // APC WAKE-UP: check raw input BEFORE the bypass guard.
        // activeSampleCount starts at 0 after clear(); the code that resets it
        // from the delayed signal is after the pre-delay write, which is itself
        // gated by this guard — so without a raw-input check here the reverb
        // would never activate from bypass.
        bool signal_active_4 = false;
        {
            float32x4_t absL = vabsq_f32(inL4);
            float32x4_t absR = vabsq_f32(inR4);
            float32x4_t absIn = vmaxq_f32(absL, absR);
            float32x4_t mx1 = vmaxq_f32(absIn, vextq_f32(absIn, absIn, 2));
            float mx = vgetq_lane_f32(vmaxq_f32(mx1, vextq_f32(mx1, mx1, 1)), 0);
            signal_active_4 = (mx > 1e-4f);
            if (mx > 1e-5f)
                activeSampleCount = (int)(sampleRate * (1.0f + decay * 5.0f));
        }
        // Noise envelope: fast attack (~83ms), slow release for smooth stellare onset/offset.
        if (signal_active_4)
            noiseEnvelope += 0.001f * (1.0f - noiseEnvelope);
        else
            noiseEnvelope -= 0.0005f * noiseEnvelope;

        if (activeSampleCount <= 0) {
            float32x4_t zero = vdupq_n_f32(0.0f);
            vst1q_f32(outL, zero);
            vst1q_f32(outR, zero);
            return;
        }

        // Convert to mono for FDN input
        float32x4_t inMono = vmulq_f32(vaddq_f32(inL4, inR4), vdupq_n_f32(0.5f));

        // =================================================================
        // 1 & 2. PRE-DELAY (Smoothed, Interpolated, Write-Before-Read)
        // =================================================================
        float monoLanes[NEON_LANES];
        vst1q_f32(monoLanes, inMono); // Unpack the NEON vector
        float delayedLanes[NEON_LANES];

        for (int s = 0; s < NEON_LANES; s++) {
            // 1. WRITE FIRST: Guarantees 0.0ms delay instantly reads this exact sample
            preDelayBuffer[preDelayWritePos] = monoLanes[s];

            // 2. Slew Limiter (1-Pole Low-Pass per sample)
            currentPreDelaySamples += 0.001f * (targetPreDelaySamples - currentPreDelaySamples);

            // 3. Fractional Read Position
            float pd_read_pos = (float)preDelayWritePos - currentPreDelaySamples;

            // Branchless wrap
            // One buffer of bias is enough (setPreDelay clamps the target below
            // PREDELAY_BUFFER_SIZE) and keeps the fractional part precise.
            float safe_pd_pos = pd_read_pos + (float)PREDELAY_BUFFER_SIZE;
            int32_t pd_idx1 = (int32_t)safe_pd_pos;
            int32_t pd_idx2 = pd_idx1 + 1;

            // Mask to buffer size
            uint32_t idx1 = pd_idx1 & PREDELAY_MASK;
            uint32_t idx2 = pd_idx2 & PREDELAY_MASK;
            float pd_frac = safe_pd_pos - (float)pd_idx1;

            // 4. Linear Interpolation Read
            float val1 = preDelayBuffer[idx1];
            float val2 = preDelayBuffer[idx2];
            delayedLanes[s] = val1 + pd_frac * (val2 - val1);

            // 5. Advance write head for the NEXT sample inside the block
            preDelayWritePos = (preDelayWritePos + 1) & PREDELAY_MASK;
        }

        // Repack into the NEON vector for the rest of the FDN engine
        float32x4_t delayedMono = vld1q_f32(delayedLanes);

        // =================================================================
        // 3. Read from all 8 delay lines for 4 samples using current phases
        // =================================================================
        float32x4_t delayOut[FDN_CHANNELS];
        readDelayLines4(delayOut);


        // =================================================================
        // 4. Active Partial Counting (CPU Optimization)
        // =================================================================
        float32x4_t energy = vdupq_n_f32(0.0f);

        for (int ch = 0; ch < FDN_CHANNELS; ch++) {
            energy = vmlaq_f32(
                energy,
                delayOut[ch],
                delayOut[ch]
            );
        }
        // accross vector sum is not availabe in NEON v7
        float e[4];
        vst1q_f32(e, energy);
        float totalEnergy =
            e[0] + e[1] + e[2] + e[3];

        if (totalEnergy > 1e-8f) { // Lower threshold for smoother tail
            // Signal present: reset counter to maximum reverb tail length
            // RT60 roughly corresponds to decay time + predelay
            activeSampleCount = (int)(sampleRate * (2.0f + decay * 8.0f)); // Longer tail count
        } else if (activeSampleCount > 0) {
            // Signal absent: decrement counter
            activeSampleCount -= NEON_LANES;
        }

        // If counter has expired, bypass FDN processing to save cycles
        if (activeSampleCount <= 0) {
            float32x4_t zero = vdupq_n_f32(0.0f);
            vst1q_f32(outL, zero);
            vst1q_f32(outR, zero);
            // Zero the positions being skipped so old reverb data doesn't
            // replay when the FDN reactivates after silence.
            for (int s = 0; s < NEON_LANES; s++) {
                uint32_t pos = (writePos + s) & BUFFER_MASK;
                vst1q_f32(&delayLine[pos].samples[0], zero);
                vst1q_f32(&delayLine[pos].samples[4], zero);
            }
            writePos = (writePos + NEON_LANES) & BUFFER_MASK;
            return;
        }

        // =================================================================
        // 5. Advance modulation phases for the next block (after read so phases aren't clobbered)
        // =================================================================
        updateModulation4();
        updateRandomLfo();
        slewDelayTimes();

        float modAmount = diffusion * modDepth;   // diffusion is DFSN (0..1)
        float fcMod = smoothedLfoValue * modAmount * 500.0f;  // ± up to 500 Hz

        // =================================================================
        // 6. Input drive
        // =================================================================
        // The per-pass feedback gains (lowBandGain / highBandGain) come from the
        // requested RT60 and are applied inside the filter chain, not here.
        // inputDrive is recomputed with them, see updateDecayGains().
        float32x4_t feedback = vdupq_n_f32(inputDrive);

        // =================================================================
        // Apply the feedback mixing matrix (vectorized)
        // =================================================================
        // PILL=1 swaps the two 4-channel banks on every pass so the tail
        // physically alternates between left and right; every other mode uses
        // the fully-diffusing 8-point Hadamard.
        float32x4_t mixed[FDN_CHANNELS];
        if (pingPong_) applyPingPongMatrix(delayOut, mixed);
        else           applyHadamard4(delayOut, mixed);

        // Per-channel IIR filter chain — frequency-dependent decay (always),
        // resonant biquad (all modes but noise), metal comb (metal), crystal
        // allpass (crystal) — all run channel-parallel on NEON via a transpose.
        //
        // Screech Fix: filter coefficients are only re-derived every 8 blocks.
        bool recalcCoeffs;
        if (--filterUpdateCounter <= 0) { filterUpdateCounter = 8; recalcCoeffs = true; }
        else                            { recalcCoeffs = false; }
        const bool doBiquad  = (filterMode != kFilterNoise);
        if (recalcCoeffs && doBiquad) {
            float fc = fmaxf(20.0f, fminf(sampleRate * 0.45f, baseFc + fcMod));
            updateFilterCoeffsAt(fc);
        }
        const bool doMetal   = (filterMode == kFilterMetal);
        const bool doCrystal = (filterMode == kFilterCrystal);
        applyChannelFilters4Group(mixed, 0, doBiquad, doMetal, doCrystal);
        applyChannelFilters4Group(mixed, 4, doBiquad, doMetal, doCrystal);

        // 3. Inject Environmental Noise (Brown, Pink, White, Blue, Violet, Grey)
        // noiseEnvelope smoothly fades noise in/out with signal activity (prevents
        // abrupt onset/offset of stellare noise). gainScale bounds steady-state level.
        addColouredNoise(mixed, FDN_CHANNELS, (1.0f - unifiedDecay) * noiseEnvelope);

        // 4. Apply cross‑channel feedback
        applyCrossFeedback(mixed);

        // 5. Add input.
        float32x4_t inputVec = vmulq_f32(delayedMono, feedback);
        if (pingPong_) {
            // Ping-pong: excite the left bank only, so the very first echo is
            // hard left and the tail starts bouncing from a known side. Feeding
            // both banks would start the tail centred and wash the effect out.
            mixed[0] = vaddq_f32(mixed[0], inputVec);
            mixed[2] = vaddq_f32(mixed[2], vmulq_n_f32(inputVec, 0.7f));
        } else {
            // An FDN this diffuse needs excitation into multiple channels
            mixed[0] = vaddq_f32(mixed[0], inputVec);
            mixed[2] = vaddq_f32(mixed[2], vmulq_n_f32(inputVec, 0.7f));    // was 0.5f
            mixed[5] = vaddq_f32(mixed[5], vmulq_n_f32(inputVec, -0.5f));   // was -0.3f
            mixed[7] = vaddq_f32(mixed[7], vmulq_n_f32(inputVec, 0.35f));   // was 0.2f
        }

        // 6. Soft saturation, only where the network is actually running hot.
        //
        // The previous limiter was x/(1+|x|), whose gain is 1/(1+|x|) at *every*
        // level: a tail sitting at 0.3 lost 2.3 dB on every trip round the loop,
        // roughly -16 dB/s of extra decay on top of the feedback gain, and loud
        // hits decayed faster than quiet ones. That is most of why the reverb
        // sounded weak and short.
        //
        // softClipN is exactly y = x below kLimitThreshold, so the tail decays
        // at the rate TIME asked for and nothing else; above the knee it
        // compresses smoothly and never expands (|dy/dx| <= 1), so the FDN
        // stays bounded without the hard-clip DC lockup that hung stellare.
        applySoftSaturation(mixed);

        // =================================================================
        // Exotic Low-Pitching Shimmer (PILL=4) - NEON Vectorized
        // =================================================================
        applyExoticShimmer(mixed);

        // =================================================================
        // Write back to delay lines
        // =================================================================
        writeDelayLines4(mixed);

        // =================================================================
        // Mix down to stereo — routing depends on PILL value
        // =================================================================
        // Channels 0-3 feed the left output and 4-7 the right, for every mode.
        // In ping-pong that split is what makes the bank swap audible; the
        // alternation lives in the feedback matrix, not in the panning.
        float32x4_t leftMix, rightMix;
        {
            int activeCh;
            switch (pillar_) {
                case 0: activeCh = 2; break;
                case 2: activeCh = 6; break;
                default: activeCh = FDN_CHANNELS; break;  // 1, 3, 4 → 8
            }

            int halfL = activeCh < 4 ? activeCh : 4;
            int halfR = activeCh > 4 ? activeCh - 4 : 0;

            float32x4_t leftSum  = vdupq_n_f32(0.0f);
            float32x4_t rightSum = vdupq_n_f32(0.0f);
            for (int i = 0; i < halfL; i++)
                leftSum = vaddq_f32(leftSum, mixed[i]);
            for (int i = 4; i < 4 + halfR; i++)
                rightSum = vaddq_f32(rightSum, mixed[i]);

            // Fixed 0.5 normalization (vs 1/halfL=0.25 for 8ch) gives 6dB more output,
            // making the reverb tail immediately audible. Soft saturation above keeps
            // individual channels < 1 so summing 4 channels × 0.5 stays below 2.0.
            leftMix = vmulq_f32(leftSum, vdupq_n_f32(0.5f));
            if (halfR > 0)
                rightMix = vmulq_f32(rightSum, vdupq_n_f32(0.5f));
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

        // Per-mode output makeup (levels dark modes up to the resonant ones).
        float32x4_t mk = vdupq_n_f32(outputMakeup);
        wetL = vmulq_f32(wetL, mk);
        wetR = vmulq_f32(wetR, mk);

        // Output ceiling. This is a send effect: whatever the user does to TIME
        // and DAMP, the unit must not be able to throw several times full scale
        // at the mix bus. Same curve as the in-loop saturator, so it is
        // transparent at working levels and only bites on the way to the wall.
        {
            float32x4_t s[2] = { wetL, wetR };
            softClipPair(s);
            wetL = s[0]; wetR = s[1];
        }

        // Store wet signal directly (hardware handles dry+wet blend)
        vst1q_f32(outL, wetL);
        vst1q_f32(outR, wetR);
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
    /* Remainder frames                                                          */
    /*===========================================================================*/

    /**
     * Render 1-3 leftover frames.
     *
     * The DSP only exists in the 4-wide form, so the remainder is zero-padded up
     * to a full block and the surplus outputs are discarded. The write heads are
     * then wound back to advance by exactly the frames the host asked for, so
     * the padding cannot make the reverb run fast: the delay-line frames written
     * past the end are simply overwritten by the next call. Only the filter and
     * LFO states see the extra samples of silence, which is a sub-microsecond
     * error once per render call and does not accumulate.
     *
     * This replaces a hand-written scalar twin of the whole engine. It had
     * drifted a long way from the vector path -- no colour biquad, no metal
     * comb, no crystal allpass, no cross-feedback, no noise injection, no delay
     * slew, input into one channel instead of four, and 21x the modulation
     * depth -- while writing into the same delay lines as the vector path.
     */
    void processPartial(const float* inL, const float* inR,
                        float* outL, float* outR, int n) {
        float padL[NEON_LANES] = {0.0f, 0.0f, 0.0f, 0.0f};
        float padR[NEON_LANES] = {0.0f, 0.0f, 0.0f, 0.0f};
        float tmpL[NEON_LANES], tmpR[NEON_LANES];
        for (int i = 0; i < n; i++) { padL[i] = inL[i]; padR[i] = inR[i]; }

        const int wp0  = writePos;
        const int pdp0 = preDelayWritePos;

        process4Samples(padL, padR, tmpL, tmpR);

        writePos = (wp0 + n) & BUFFER_MASK;
        // The APC bypass path returns before touching the pre-delay line, so
        // only wind that head back if it actually moved.
        if (preDelayWritePos != pdp0)
            preDelayWritePos = (pdp0 + n) & PREDELAY_MASK;

        for (int i = 0; i < n; i++) { outL[i] = tmpL[i]; outR[i] = tmpR[i]; }
    }

    /*===========================================================================*/
    /* Initialization */
    /*===========================================================================*/
    /** use the 18-EDO Equal Division of the Octave) math formula ($2^{n/18}$)
     * to perfectly space the LFO frequencies into a microtonal scale.
     * By applying 8 independent, slow Doppler pitch-shifts to the delay lines—tuned
     * exactly to microtonal intervals—the 8 echoes will physically rub against each
     * other inside the Hadamard matrix. This creates the dense,
     * acoustic "beating" and complex harmonic interference that Cochrane
     * defines as true microtonal shimmer.*/
    void initMicrotonalShimmer()
    {
        // Set up standard swirl (e.g., random slow rates between 0.2Hz and 0.8Hz)
        // Set up Cochrane 18-EDO rates (as discussed previously)
        for(int i=0; i<FDN_CHANNELS; i++) {
            channelPhase_[i] = (float)i / (float)FDN_CHANNELS; // Stagger phases 0.0 to 1.0

            // Example Swirl Rates (prime-ish relationships)
            float s_hz = 0.2f + (0.1f * i);
            swirlRate_[i] = s_hz / sampleRate;

            // 18-EDO Cochrane Rates (Base 0.5Hz)
            float steps_18[8] = {0.0f, 1.0f, 3.0f, 4.0f, 6.0f, 7.0f, 9.0f, 10.0f};
            microtonalRate_[i] = (0.5f * fasterpowf(2.0f, steps_18[i] / 18.0f)) / sampleRate; // at Init use accurate function
        }
    }

    /*===========================================================================*/
    /* Private Member Variables */
    /*===========================================================================*/

    // ============================================================================
    // Parameter State (mirrors header.c defaults)
    // ============================================================================
    // ID 0:  PRESET 0..4               default 0 (foresta)
    // ID 1:  TIME   1..100             default 50   mid-band RT60
    // ID 2:  LOW    1..100             default 50   low-band RT60 multiplier
    // ID 3:  HIGH   1..100             default 70   high-band RT60 multiplier
    // ID 4:  DAMP   20..1000           default 250  (×10 in code → 2500 Hz)
    // ID 5:  WIDE   0..200 %           default 100
    // ID 6:  DFSN   0..100 %           default 100
    // ID 7:  PILL   0..4               default 3
    // ID 8:  SHMR   0..100             default 35   shimmer frequency (PILL=4)
    // ID 9:  PDLY   0..200             default 0 (ms)
    // ID 10: VIBR   1..30              default 10   (×0.1 → 0.1..3.0 Hz)
    // ID 11: BNCE   60..500            default 180  ping-pong bounce time (ms)
    int32_t params_[k_total]  __attribute__((aligned(16)));
    float sampleRate;
    int writePos;
    float decay;
    float diffusion;
    float modDepth;
    float modRate;
    float width;         // stereo width   0..2
    float dampingCoeff;  // one-pole LPF coeff for damping
    float lowDecayMult;  // low-freq decay multiplier
    float highDecayMult; // high-freq decay multiplier

    bool  initialized;
    int   pillar_;        /* 0..4 - pillar count / routing mode */
    bool  pingPong_;      /* true when pillar_==1 */
    float shimmerDepth_;  /* re-injection gain for pillar_==4 */
    float shimmerPhase_;  /* LFO phase for shimmer (radians) */
    float shimmerFreq_; // Low audio rate for cascading undertones

    interleaved_frame_t delayLine[BUFFER_SIZE] __attribute__((aligned(64)));

    float delayTimes[FDN_CHANNELS] __attribute__((aligned(16)));        // slewed, live
    float targetDelayTimes[FDN_CHANNELS] __attribute__((aligned(16)));  // requested
    float32x4_t modPhaseVec[FDN_CHANNELS] __attribute__((aligned(16)));
    // new filter states
    float metalState[FDN_CHANNELS] __attribute__((aligned(16)));
    float crystalAPState[FDN_CHANNELS] __attribute__((aligned(16)));
    // FIX: IIR filter states must be strictly scalar! 1 history sample per channel.
    float lpfState[FDN_CHANNELS] __attribute__((aligned(16)));
    // Unified Phase Tracking for Delay Modulation
    float channelPhase_[FDN_CHANNELS] __attribute__((aligned(16)));
    float swirlRate_[FDN_CHANNELS] __attribute__((aligned(16)));
    float microtonalRate_[FDN_CHANNELS] __attribute__((aligned(16)));
    float preDelayBuffer[PREDELAY_BUFFER_SIZE] __attribute__((aligned(16)));
    int preDelayWritePos;
    // Pre-delay Slew Limiter States
    float currentPreDelaySamples;
    float targetPreDelaySamples;
    int activeSampleCount; // Tracks tail for Active Partial Counting

    // ---------- New character features ----------
    int currentPreset;                 // 0..3
    float lfoSpeed;                    // Hz (0.1..3.0)
    float randomLfoValue;              // current random output (-1..1)
    int randomLfoCounter;              // samples until next random update
    int randomLfoPeriodBlocks;         // = sampleRate / lfoSpeed / NEON_LANES
    float smoothedLfoValue;

    // Filter types (per channel, but we can use shared coeffs)
    // kFilterCrystal: bright allpass-like character for esotico (medium-Q bandpass
    // centred higher than metal, giving a shimmering/glassy microtonal colour)
    enum FilterMode { kFilterWood, kFilterStone, kFilterMetal, kFilterCrystal, kFilterNoise };
    FilterMode filterMode;
    float biquadA0, biquadA1, biquadA2, biquadB1, biquadB2; // shared coeffs
    float filterState1[FDN_CHANNELS] __attribute__((aligned(16)));  // z^-1
    float filterState2[FDN_CHANNELS] __attribute__((aligned(16)));  // z^-2

    // Noise generation
    uint32_t noiseSeed;
    float noiseColour;                 // 0=brown .. 4=violet
    float noiseGain;                   // from DAMP
    float noiseEnvelope;               // 0..1 smooth fade-in/out of stellare noise
    float noiseStates[FDN_CHANNELS];   // z^-1 for noise filtering
    float noiseStates2[FDN_CHANNELS];  // z^-2 for Grey noise notch

    // Cross-feedback gains per pillar
    static const float crossGain[5];   // indexed by pillar_

    float bounceTimeMs;   // ping-pong bounce period (PILL=1), from SHMR
    float unifiedDecay;   // mid-band per-pass feedback gain, from TIME
    float lowBandGain;    // per-pass gain below the DAMP crossover  (LOW)
    float highBandGain;   // per-pass gain above the DAMP crossover  (HIGH)
    float meanDelay;      // mean delay-line length (s), sets passes/second
    float inputDrive;     // injection gain into the network, tracks the decay
    int filterUpdateCounter;

    float baseFc;                   // base cutoff frequency (Hz) for current preset/damping
    float outputMakeup;             // per-mode wet level trim (levels dark vs resonant modes)
};

// Cross-feedback gain per routing mode. Index 1 (ping-pong) is the bleed between
// the left and right banks — it keeps the quiet side from going fully silent
// between bounces — quoted at the 190 ms reference bounce time and scaled from
// there by effectiveCrossGain(). Raising it softens the bounce, lowering it
// hardens it.
const float NeonAdvancedLabirinto::crossGain[5] = {0.15f, 0.10f, 0.07f, 0.04f, 0.04f};
