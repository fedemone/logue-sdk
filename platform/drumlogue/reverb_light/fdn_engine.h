#pragma once

#include <arm_neon.h>
#include <float_math.h>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>

#define FDN_CHANNELS 8
#define FDN_BUFFER_SIZE 32768
#define FDN_BUFFER_MASK (FDN_BUFFER_SIZE - 1)
// Each channel's line is FDN_BUFFER_SIZE samples plus a 16-float (one cache
// line) tail.  Slot FDN_BUFFER_SIZE mirrors slot 0, so the interpolating read
// can always load its two neighbours as one pair.  The odd stride also keeps
// the eight write heads, which all sit at the same index, out of a single L1
// cache set: at a power-of-two stride every one of them maps to the same set
// of the Cortex-A7's 4-way cache and they evict each other on every sample.
#define FDN_LINE_STRIDE (FDN_BUFFER_SIZE + 16)
#define PREDELAY_BUFFER_SIZE 16384
#define PREDELAY_MASK (PREDELAY_BUFFER_SIZE - 1)
#define PREDELAY_MAX_SAMPLES (16000.0f)   // PDLY 100% (~333 ms)
#define SPARKLE_BUFFER_SIZE 4096
#define NUM_RESONATORS (6)
#define SAMPLE_RATE (48000.0f)
#define NEON_LANES  (4)
#ifndef fast_inline
#define fast_inline inline __attribute__((always_inline, optimize("Ofast")))
#endif

// Largest round-trip gain the FDN loop may have at any frequency.  0.985 is
// what DCAY 100% with BASS 0% has always given; see updateFeedback().
#define FDN_MAX_LOOP_GAIN (0.985f)

// SIZE and PDLY glide like NeonLabirinto's delay times: a ~40 ms one-pole
// toward the target, its rate capped so a read head never moves faster than
// half a sample per sample against its write head (at most an octave down, a
// fifth up, never backwards), and the rate itself eased in and out over a few
// blocks so the bend has no kinks.  Before, a knob step jumped every read
// head at once, and the jump was a click.
#define GLIDE_COEFF    (0.0005f)   // per sample
#define GLIDE_MAX_RATE (0.5f)      // samples of delay change per sample
#define GLIDE_EASE     (0.3f)      // per block

static_assert(2.0f * 4513.0f + 2.0f < (float)FDN_BUFFER_SIZE,
              "FDN line too short for the longest prime at SIZE 100%");
static_assert(PREDELAY_MAX_SAMPLES + 2.0f < (float)PREDELAY_BUFFER_SIZE,
              "pre-delay line too short for PDLY 100%");

/**
 * Flush-to-zero and default-NaN for the length of one render, and the
 * caller's mode back afterwards -- the same scope NeonLabirinto uses.
 *
 * This used to set bit 22 for "default NaN" and never restore anything.  On
 * ARMv7 DN is bit 25; bit 22 is half of RMode and selects round towards plus
 * infinity.  Measured on the ARM build under QEMU, FPSCR went from 0x20000010
 * to 0x61400010 across one render: from then on every scalar float operation
 * on the drumlogue's shared audio thread -- the synths', the master FX's, the
 * firmware's mixer -- rounded up instead of to nearest.
 */
struct AudioFpuScope {
#if defined(__arm__) && defined(__ARM_FP)
    static constexpr uint32_t kFZ = 1u << 24;
    static constexpr uint32_t kDN = 1u << 25;
    uint32_t prev;
    AudioFpuScope() {
        __asm__ volatile("vmrs %0, fpscr" : "=r"(prev) : : "memory");
        const uint32_t want = prev | kFZ | kDN;
        if (want != prev) __asm__ volatile("vmsr fpscr, %0" : : "r"(want) : "memory");
    }
    ~AudioFpuScope() {
        uint32_t now;
        __asm__ volatile("vmrs %0, fpscr" : "=r"(now) : : "memory");
        const uint32_t restored = (now & ~(kFZ | kDN)) | (prev & (kFZ | kDN));
        if (restored != now) __asm__ volatile("vmsr fpscr, %0" : : "r"(restored) : "memory");
    }
#endif
};

// One gliding parameter (see GLIDE_*).  plan() is called once per block and
// returns the per-sample increment for that block; the caller adds it per
// sample and stores the end value back in cur.
struct Glide {
    float cur = 0.0f;
    float step = 0.0f;
    float plan(float target, float max_step) {
        float want = GLIDE_COEFF * (target - cur);
        want = fmaxf(-max_step, fminf(max_step, want));
        step += GLIDE_EASE * (want - step);
        return step;
    }
    void snap(float v) { cur = v; step = 0.0f; }
};
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

enum k_parameters {
    k_paramProgram, k_dark, k_bright, k_glow,
    k_color, k_spark, k_size, k_pdly,
    k_decay, k_bass, k_color_shift,
    k_rate, k_irid, k_wdth,
    k_total
};

typedef enum {
    k_stanzaNeon,
    k_vicoBuio,
    k_strobo,
    k_bruciato,
    k_preset_number,
} preset_numer_t;

// ============================================================================
// Presets
// ============================================================================
static const char* k_preset_names[k_preset_number] = {
    "StanzaNeon", // 0: Tight, bright, standard drum room
    "VicoBuio",   // 1: Long decay, heavy LPF, spooky
    "Strobo",     // 2: High pre-delay, short decay, heavily modulated
    "Bruciato"    // 3: Massive size, max decay, floating
};

// Values:
//  { NAME, DARK, BRIG, GLOW, COLR, SPRK, SIZE, PDLY, DCAY, BASS, CLRQ, RATE, IRID, WDTH }
static const int32_t k_presets[k_preset_number][k_total] = {
    { k_stanzaNeon, 20, 50, 10,  0,  5, 30,  5,  65,  30,  0, 20,  0, 70 },  // StanzaNeon: medium decay, wide
    { k_vicoBuio,   50, 20,  0, 10,  0, 60, 15,  85,  10,  0, 10, 15, 50 },  // VicoBuio:   long dark, iridiscence
    { k_strobo,     20, 10, 20, 20, 20, 20, 80,  45,  50,  0, 40,  0,100 },  // Strobo:     short, fast LFO, wide
    { k_bruciato,   70,  0, 40, 10,  0, 90, 10,  95,  20,  0, 15, 30, 60 }   // Bruciato:   massive, iridiscence
};

// ============================================================================
// Main Class
// ============================================================================
class FDNEngine {
public:
    FDNEngine() : sampleRate(SAMPLE_RATE) {
        initialized = false;
        Reset();
    }

    // ========================================================================
    // NEW PARALLEL PARAMETERS
    // ========================================================================
    float dark_amt = 10.0f;
    float glow_amt = 10.0f;
    float bright_amt = 10.0f;
    float color_amt = 10.0f;
    float spark_amt = 10.0f;

    float sizeScale = 1.0f;
    float predelayScale = 0.0f;
    float decay = 0.8f;         // Feedback gain: 0.1 (short) .. 0.98 (infinite)
    float hpf_coeff = 0.95f;    // Per-channel one-pole HPF coefficient: 0.85..0.99
    float fb_gain = 0.8f;       // decay, bounded so the loop stays stable (updateFeedback)

    // ========================================================================
    // STATE VARIABLES
    // ========================================================================
    // FDN Core
    float fdnMem[FDN_CHANNELS * FDN_LINE_STRIDE] __attribute__((aligned(16)));
    float baseDelayTimes[FDN_CHANNELS] __attribute__((aligned(16)));
    float delayTimes[FDN_CHANNELS];
    float fdnState[FDN_CHANNELS];
    float hadamard[FDN_CHANNELS][FDN_CHANNELS] __attribute__((aligned(16)));
    float fdn_norm = 0.35355339f;   // 1/sqrt(FDN_CHANNELS); set in generate_hadamard()
    int writePos = 0;

    // internal parameters
    int32_t params_[k_total]  __attribute__((aligned(16)));
    int32_t current_preset_ = 0;

    // Per-channel one-pole HPF states (applied to each delay output to cut bass buildup)
    float hpf_x_prev[FDN_CHANNELS] __attribute__((aligned(16)));   // previous input sample
    float hpf_y_prev[FDN_CHANNELS] __attribute__((aligned(16)));   // previous output sample

    // Predelay
    float preDelayBuffer[PREDELAY_BUFFER_SIZE] __attribute__((aligned(16)));
    int preDelayWritePos = 0;

    // Path 1: Glow (Modulated Chamberlin SVF)
    float glow_lfo_phase = 0.0f;
    float glow_lp_l = 0.0f;
    float glow_bp_l = 0.0f;
    float glow_lp_r = 0.0f;
    float glow_bp_r = 0.0f;
    float glow_hpf_state_l = 0.0f;
    float glow_hpf_state_r = 0.0f;
    float glow_lpf_state_l = 0.0f;
    float glow_lpf_state_r = 0.0f;

    // Path 2: Dark (Organic Granular Sub-Octave)
    float dark_buffer[4096];
    int dark_write = 0;
    float dark_phase = 0.0f;
    float dark_lpf_state = 0.0f;

    // Path 3: Bright (Harmonic Exciter States)
    biquad_state_t  bright_hpf_l;
    biquad_state_t  bright_hpf_r;
    biquad_coeffs_t bright_coeffs;

    // Path 4: Color (6 Visual Spectrum Resonators)
    biquad_coeffs_t color_coeffs[NUM_RESONATORS]    __attribute__((aligned(16)));
    // SoA coefficients/state for the NEON color path, padded to 8 lanes
    // (NUM_RESONATORS real + zero padding) so the 6 parallel bandpass biquads
    // run two-at-a-time on NEON. Zero-coeff padding lanes contribute nothing.
    alignas(16) float col_b0[8];
    alignas(16) float col_b1[8];
    alignas(16) float col_b2[8];
    alignas(16) float col_a1[8];
    alignas(16) float col_a2[8];
    alignas(16) float col_z1l[8];
    alignas(16) float col_z2l[8];
    alignas(16) float col_z1r[8];
    alignas(16) float col_z2r[8];    // Applying __attribute__((aligned(16))) at the end of a comma-separated declaration list only aligns
                                     // the very last variable leaving the other unaligned.

    // Path 5: Sparkle (Stereo Granular S&H)
    float sparkle_buffer_l[SPARKLE_BUFFER_SIZE];
    float sparkle_buffer_r[SPARKLE_BUFFER_SIZE];
    int spark_write = 0;
    float spark_read = 0.0f;
    float spark_speed = 2.0f;
    float spark_pan_l = 0.5f;
    float spark_pan_r = 0.5f;
    int spark_countdown = 0;
    int spark_duration = 0;   // total grain length for envelope computation
    float spark_inv_duration = 0.0f;

    float sampleRate;
    float glowLfoRate = 0.0f;
    // The glow LFO is a sine in cycles (glow_lfo_phase in [0,1)), evaluated
    // once per block and interpolated linearly inside it: at 4 Hz and 64
    // frames the error is 1.4e-4, and it saves three sine approximations per
    // sample (a fifth of this unit's render).
    float lfo_sin_ = 0.0f;      // sin(2*pi*phase) at the start of the next block
    float lfo_cos_ = 1.0f;      // cos(2*pi*phase), the right channel's 90 degree offset
    Glide size_glide_;          // sizeScale as the delay lines see it
    Glide pdly_glide_;          // pre-delay in samples
    float size_max_step_ = 0.0f;
    bool  glides_primed_ = false;   // false until the first block after Reset()
    float glow_rate_hz = 0.4f;
    float irid_amt = 0.0f;
    float width_amt = 1.0f;
    bool initialized;

    // Path 6: iridiscence (Granular Octave-Up)
    float iridiscensce_buffer[4096];
    int   iridiscensce_write = 0;
    float irid_phase = 0.0f;
    float irid_lpf_l = 0.0f;
    float irid_lpf_r = 0.0f;

    // ========================================================================
    // INITIALIZATION & MATH
    // ========================================================================
    bool init(float sr) {
        sampleRate = sr;
        generate_hadamard();

        // Base prime delay times for 8 channels
        const float primes[FDN_CHANNELS] = {1103.0f, 1511.0f, 1999.0f, 2503.0f, 3011.0f, 3511.0f, 3989.0f, 4513.0f};
        for (int i = 0; i < FDN_CHANNELS; i++) {
            baseDelayTimes[i] = primes[i] * (sampleRate / SAMPLE_RATE);
        }
        // SIZE scales every line; the longest one moves fastest, so it sets
        // the cap on how fast sizeScale may glide.
        size_max_step_ = GLIDE_MAX_RATE / baseDelayTimes[FDN_CHANNELS - 1];

        // LFO rate: controlled by RATE parameter (default 0.4 Hz).
        // Depth scales with glow_amt so GLOW=0 → no filter modulation.
        glowLfoRate = glow_rate_hz / sampleRate;

        update_color_resonators(1.0f);
        initialize_brightness_harmonic_exciter();
        Reset();
        initialized = true;
        return true;
    }

    // 5kHz Butterworth HPF
    void initialize_brightness_harmonic_exciter() {
        float w0_bright = 2.0f * M_PI * 5000.0f / sampleRate;
        float alpha_bright = sinf(w0_bright) / (2.0f * 0.707f); // at init no fast function
        float a0_bright = 1.0f + alpha_bright;

        bright_coeffs.b0 = ((1.0f + cosf(w0_bright)) / 2.0f) / a0_bright;
        bright_coeffs.b1 = -(1.0f + cosf(w0_bright)) / a0_bright;
        bright_coeffs.b2 = ((1.0f + cosf(w0_bright)) / 2.0f) / a0_bright;
        bright_coeffs.a1 = (-2.0f * cosf(w0_bright)) / a0_bright;
        bright_coeffs.a2 = (1.0f - alpha_bright) / a0_bright;
    }

    // TODO - possibly use LFO for this?
    // shift_factor from the UI: e.g., 0.5f to 2.0f (-1 octave to +1 octave)
    void update_color_resonators(float shift_factor) {
        // EXACT VISUAL SPECTRUM FREQUENCIES (in Hz)
        const float base_freqs[NUM_RESONATORS] = {4100.0f, 5000.0f, 5200.0f, 5800.0f, 6600.0f, 7200.0f};

        // Try possibly a value of Q: 8.0f to makes them "ring" like physical modal resonators.
        // In case 4.0f is too wide/damped to sound like a distinct pitch.
        const float Q = 6.0f + shift_factor * 2.0f;

        for (int i = 0; i < NUM_RESONATORS; i++) {
            // Apply the UI shift
            float hz = base_freqs[i] * shift_factor;

            // Safety Clamp: Prevent high frequencies from crashing the filter near Nyquist
            if (hz > sampleRate * 0.45f) hz = sampleRate * 0.45f;
            // Constant Peak Gain Bandpass Biquad Math
            float w0 = 2.0f * M_PI * hz / sampleRate;
            float alpha = fastersinfullf(w0) / (2.0f * Q);

            float a0 = 1.0f + alpha;
            color_coeffs[i].b0 = alpha / a0;
            color_coeffs[i].b1 = 0.0f;
            color_coeffs[i].b2 = -alpha / a0;
            color_coeffs[i].a1 = -2.0f * fastercosfullf(w0) / a0;
            color_coeffs[i].a2 = (1.0f - alpha) / a0;
        }

        // Mirror into the SoA layout used by the vectorized color path. Padding
        // lanes (>= NUM_RESONATORS) get zero coefficients so they stay silent.
        for (int i = 0; i < 8; i++) {
            if (i < NUM_RESONATORS) {
                col_b0[i] = color_coeffs[i].b0; col_b1[i] = color_coeffs[i].b1;
                col_b2[i] = color_coeffs[i].b2; col_a1[i] = color_coeffs[i].a1;
                col_a2[i] = color_coeffs[i].a2;
            } else {
                col_b0[i] = col_b1[i] = col_b2[i] = col_a1[i] = col_a2[i] = 0.0f;
            }
        }
    }

    void generate_hadamard() {
        float norm = 1.0f / sqrtf(FDN_CHANNELS);    // as init is not time strict, let's keep the original function
        fdn_norm = norm;                            // reused by the fast WHT in step_core_fdn()
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

    // Public reset — called by unit_reset()
    void reset() { Reset(); }

    void Reset() {
        memset(fdnMem, 0, sizeof(fdnMem));
        memset(fdnState, 0, sizeof(fdnState));
        memset(preDelayBuffer, 0, sizeof(preDelayBuffer));
        memset(col_z1l, 0, sizeof(col_z1l)); memset(col_z2l, 0, sizeof(col_z2l));
        memset(col_z1r, 0, sizeof(col_z1r)); memset(col_z2r, 0, sizeof(col_z2r));
        memset(sparkle_buffer_l, 0, sizeof(sparkle_buffer_l));
        memset(sparkle_buffer_r, 0, sizeof(sparkle_buffer_r));
        writePos = 0;
        preDelayWritePos = 0;
        memset(dark_buffer, 0, sizeof(dark_buffer));
        dark_write = 0;
        dark_phase = 0.0f;
        dark_lpf_state = 0.0f;
        memset(&bright_hpf_l, 0, sizeof(biquad_state_t));
        memset(&bright_hpf_r, 0, sizeof(biquad_state_t));
        glow_lfo_phase = 0.0f;
        lfo_sin_ = 0.0f;
        lfo_cos_ = 1.0f;
        glides_primed_ = false;
        glow_lp_l = 0.0f;
        glow_bp_l = 0.0f;
        glow_lp_r = 0.0f;
        glow_bp_r = 0.0f;
        glow_hpf_state_l = 0.0f;
        glow_hpf_state_r = 0.0f;
        glow_lpf_state_l = 0.0f;
        glow_lpf_state_r = 0.0f;
        memset(hpf_x_prev, 0, sizeof(hpf_x_prev));
        memset(hpf_y_prev, 0, sizeof(hpf_y_prev));
        memset(iridiscensce_buffer, 0, sizeof(iridiscensce_buffer));
        iridiscensce_write = 0;
        irid_phase = 0.0f;
        irid_lpf_l = 0.0f;
        irid_lpf_r = 0.0f;
    }

    /*===========================================================================*/
    /* Parameter Interface */
    /*===========================================================================*/

    inline void loadPreset(uint8_t index) {
        if (index >= k_preset_number) return;
        if (current_preset_ != index){
            current_preset_ = index;
            for (uint8_t i = 0; i < k_total; i++) {
                setParameter(i, k_presets[index][i]);
            }
        }
    }

    inline int32_t getPreset() {
        return current_preset_;
    }

    inline int32_t getParameterValue(uint8_t index) {
        if (index >= k_total) return -1;    // invalid value
        return  params_[index];
    }

    inline void setParameter(uint8_t index, int32_t value) {
        if (index >= k_total) return;
        params_[index] = value;   // store into local DB

        const float norm = value * 0.01f;  // 0..100 → 0.0..1.0

        switch (index) {
        case k_paramProgram: // NAME  preset selector — load preset when the
                             // user scrolls

          loadPreset(value);
          break;
        case k_dark: // DARK  decay suboctaves  0-100% → decay 0.0..0.99
            setDarkness(norm);
            break;
        case k_bright: // BRIG  brightness  0-100% → 0.0..1.0
            setBrightness(norm);
            break;
        case k_glow: // GLOW  modulation  0-100% → 0.0..1.0
            setGlow(norm);
            break;
        case k_color: // COLR  tone color (spectrum resonance)  0-100% → coeff 0.0..0.95
            setColor(norm);
            break;
        case k_spark: // SPRK  sparkle S&H pops  0-100% → 0.0..1.0
            setSpark(norm);
            break;
        case k_size: // SIZE  room size  0-100% → scale 0.1..2.0
            setSize(norm);
            break;
        case k_pdly: // PDLY pre delay
            setPreDelay(norm);
            break;
        case k_decay: // DCAY  FDN feedback gain  0-100% → 0.1..0.98
            setDecay(norm);
            break;
        case k_bass: // BASS  per-channel HPF in FDN loop  0-100% → coeff 0.99..0.85
            setHpfCoeff(norm);
            break;
        case k_color_shift: // CLRQ shift of the colour frequency (-100..100 → -1..+1 octave)
            // Base-2 exponential mapping: 2^norm
            // val = -1.0  -> 0.5x multiplier (-1 Octave)
            // val =  0.0  -> 1.0x multiplier (No shift)
            // val = +1.0  -> 2.0x multiplier (+1 Octave)
            update_color_resonators(fasterpow2f(norm));
            break;
        case k_rate: // RATE  glow LFO speed  0-100% → 0.05..4.0 Hz (exponential)
            setRate(norm);
            break;
        case k_irid: // IRID  iridiscence amount  0-100% → 0.0..1.0
            setIridiscence(norm);
            break;
        case k_wdth: // WDTH  stereo width  0-100% → 0.0..2.0
            setWidth(norm);
            break;
        default:
        break;
        }
    }

    //==============
    // Setters  (all take normalised float 0.0-1.0)
    //==============
    void setDarkness(float val) {
        dark_amt = val;
    }
    void setBrightness(float val) {
        bright_amt = val;
    }
    void setGlow(float val) {
        glow_amt = val;
    }
    void setColor(float val) {
        color_amt = val;
    }
    void setSpark(float val) {
        spark_amt = val;
    }
    // val is normalised 0.0-1.0; mapped to sizeScale 0.1-2.0
    void setSize(float val) {
        sizeScale = 0.1f + val * 1.9f;
    }
    // val is normalised 0.0-1.0; mapped to up to ~330ms pre-delay
    void setPreDelay(float val) {
        predelayScale = val;
    }
    // val normalised 0.0-1.0; maps to feedback gain 0.1..0.98 (short to near-infinite)
    void setDecay(float val) {
        decay = 0.1f + val * 0.88f;
        updateFeedback();
    }
    // val normalised 0.0-1.0; maps HPF cutoff from minimal (0.99) to moderate (0.85).
    // The one-pole HPF formula is: y[n] = x[n] - x[n-1] + coeff * y[n-1]
    // coeff = 0.99 → fc ≈ 76 Hz (just DC blocking)
    // coeff = 0.85 → fc ≈ 1146 Hz (removes bass buildup in dense reverb tails)
    // Higher knob value = more bass removed from the reverb tail.
    void setHpfCoeff(float val) {
        hpf_coeff = 0.99f - (val * 0.14f);
        updateFeedback();
    }
    // The loop's round trip is fb_gain times the in-loop HPF, and that HPF
    // (y = x - x1 + c*y1) is not unity at the top: its gain rises to 2/(1+c)
    // at Nyquist, 1.08 at BASS 100%.  DCAY alone therefore did not bound the
    // loop, and from DCAY ~94% with BASS 100% (or ~98% with BASS 50%) the high
    // end of the tail grew instead of decaying: measured on the ARM build, a
    // tail left in silence at DCAY 100 / BASS 100 bottomed out near -67 dB,
    // then rose about 0.4 dB/s, through 0 dBFS at three minutes, on its way
    // to Inf.  Bounding fb_gain so the top of the HPF stays at
    // FDN_MAX_LOOP_GAIN changes nothing below that region -- every preset and
    // all of BASS 0% keep their exact decay -- and at its edge holds DCAY at
    // the longest stable tail, the same as DCAY 100% at BASS 0%.
    void updateFeedback() {
        fb_gain = fminf(decay, FDN_MAX_LOOP_GAIN * 0.5f * (1.0f + hpf_coeff));
    }
    // 0.0..1.0 → 0.05..4.0 Hz via 2^(val*6)*0.05 (exponential for musical feel)
    void setRate(float val) {
        glow_rate_hz = 0.05f * fasterpow2f(val * 6.0f);
        glowLfoRate = glow_rate_hz / sampleRate;
    }
    void setIridiscence(float val) {
        irid_amt = val;
    }
    // 0.0..1.0 → 0.0..2.0 (0=mono, 1=unity, 2=extra wide)
    void setWidth(float val) {
        width_amt = val * 2.0f;
    }

    // ========================================================================
    // BAREBONES FDN STEP (Replaces old bloated FDN logic)
    // ========================================================================
    // Four channels' fractional reads (channels ch0..ch0+3), positions in
    // NEON lanes.  The pair each lane interpolates between is one 2-float load
    // (slot FDN_BUFFER_SIZE mirrors slot 0, see FDN_LINE_STRIDE), and the
    // pairs are de-interleaved with one vuzp.  Same arithmetic as the scalar
    // loop it replaces: val1 + frac * (val2 - val1).
    fast_inline float32x4_t read_lines4(int ch0, float32x4_t wp, float size) const {
        const float32x4_t bufsz = vdupq_n_f32((float)FDN_BUFFER_SIZE);
        float32x4_t pos = vmlsq_n_f32(wp, vld1q_f32(&baseDelayTimes[ch0]), size);
        pos = vaddq_f32(pos, vreinterpretq_f32_u32(vandq_u32(
                  vcltq_f32(pos, vdupq_n_f32(0.0f)), vreinterpretq_u32_f32(bufsz))));
        const int32x4_t ipos = vcvtq_s32_f32(pos);          // pos >= 0: truncation is floor
        const float32x4_t frac = vsubq_f32(pos, vcvtq_f32_s32(ipos));
        // pos can round up to exactly FDN_BUFFER_SIZE; that slot is slot 0.
        alignas(16) int32_t idx[NEON_LANES];
        vst1q_s32(idx, vandq_s32(ipos, vdupq_n_s32(FDN_BUFFER_MASK)));
        const float* line = &fdnMem[ch0 * FDN_LINE_STRIDE];
        const float32x2_t p0 = vld1_f32(line + idx[0]);
        const float32x2_t p1 = vld1_f32(line + FDN_LINE_STRIDE + idx[1]);
        const float32x2_t p2 = vld1_f32(line + 2 * FDN_LINE_STRIDE + idx[2]);
        const float32x2_t p3 = vld1_f32(line + 3 * FDN_LINE_STRIDE + idx[3]);
        const float32x4x2_t v = vuzpq_f32(vcombine_f32(p0, p1), vcombine_f32(p2, p3));
        return vmlaq_f32(v.val[0], frac, vsubq_f32(v.val[1], v.val[0]));
    }

    void step_core_fdn(float in_l, float in_r, float size, float* out_l, float* out_r) {
        // 1. Read from the delay lines, four channels per vector.
        const float32x4_t wp = vdupq_n_f32((float)writePos);
        float32x4_t f_lo = read_lines4(0, wp, size);
        float32x4_t f_hi = read_lines4(NEON_LANES, wp, size);

        // 2. Per-channel one-pole HPF to kill DC and bass buildup, vectorized
        //    across the 8 channels (4 per NEON vector). Identical per-channel
        //    recurrence:  y[n] = x[n] - x[n-1] + hpf_coeff * y[n-1].
        {
            float32x4_t xprev_lo = vld1q_f32(&hpf_x_prev[0]);
            float32x4_t xprev_hi = vld1q_f32(&hpf_x_prev[NEON_LANES]);
            float32x4_t yprev_lo = vld1q_f32(&hpf_y_prev[0]);
            float32x4_t yprev_hi = vld1q_f32(&hpf_y_prev[NEON_LANES]);
            float32x4_t y_lo = vmlaq_n_f32(vsubq_f32(f_lo, xprev_lo), yprev_lo, hpf_coeff);
            float32x4_t y_hi = vmlaq_n_f32(vsubq_f32(f_hi, xprev_hi), yprev_hi, hpf_coeff);
            vst1q_f32(&hpf_x_prev[0], f_lo);  vst1q_f32(&hpf_x_prev[NEON_LANES], f_hi);
            vst1q_f32(&hpf_y_prev[0], y_lo);  vst1q_f32(&hpf_y_prev[NEON_LANES], y_hi);
            f_lo = y_lo;  f_hi = y_hi;
        }

        // 3. Stereo mixdown (channels 0-3 → L, 4-7 → R); same summation order.
        float32x2_t sum_lo = vadd_f32(vget_low_f32(f_lo), vget_high_f32(f_lo));
        *out_l = vget_lane_f32(vpadd_f32(sum_lo, sum_lo), 0);
        float32x2_t sum_hi = vadd_f32(vget_low_f32(f_hi), vget_high_f32(f_hi));
        *out_r = vget_lane_f32(vpadd_f32(sum_hi, sum_hi), 0);

        // 4. Hadamard feedback mixing via a Fast Walsh-Hadamard Transform.
        //    hadamard[i][j] = ±1/sqrt(8) by popcount(i&j) parity, so the previous
        //    O(N^2) matrix multiply equals (1/sqrt(8)) * WHT(f). The natural-
        //    order WHT is a 3-stage NEON butterfly of pure add/sub (no multiplies).
        float32x4_t a_lo = vaddq_f32(f_lo, f_hi);            // stage 1 (stride 4)
        float32x4_t a_hi = vsubq_f32(f_lo, f_hi);
        float32x4_t b_lo = vcombine_f32(                     // stage 2 (stride 2)
            vadd_f32(vget_low_f32(a_lo), vget_high_f32(a_lo)),
            vsub_f32(vget_low_f32(a_lo), vget_high_f32(a_lo)));
        float32x4_t b_hi = vcombine_f32(
            vadd_f32(vget_low_f32(a_hi), vget_high_f32(a_hi)),
            vsub_f32(vget_low_f32(a_hi), vget_high_f32(a_hi)));
        float32x4_t r_lo = vrev64q_f32(b_lo);                // stage 3 (stride 1)
        float32x4_t r_hi = vrev64q_f32(b_hi);
        float32x4_t wht_lo = vtrnq_f32(vaddq_f32(b_lo, r_lo), vsubq_f32(b_lo, r_lo)).val[0];
        float32x4_t wht_hi = vtrnq_f32(vaddq_f32(b_hi, r_hi), vsubq_f32(b_hi, r_hi)).val[0];

        // 5. Inject input (L→ch 0-3, R→ch 4-7) with the matrix normalisation and
        //    decay folded into a single scale: in + WHT * (norm*decay).
        const float nd = fdn_norm * fb_gain;
        float32x4_t res_lo = vmlaq_n_f32(vdupq_n_f32(in_l), wht_lo, nd);
        float32x4_t res_hi = vmlaq_n_f32(vdupq_n_f32(in_r), wht_hi, nd);
        float lo[NEON_LANES];
        vst1q_f32(lo, res_lo);
        float hi[NEON_LANES];
        vst1q_f32(hi, res_hi);

        // 6. Scatter feedback into the channel-major delay lines, keeping the
        //    mirror of slot 0 at the end of each line current.
        for (int k = 0; k < NEON_LANES; k++) {
            fdnMem[k * FDN_LINE_STRIDE + writePos]                = lo[k];
            fdnMem[(k + NEON_LANES) * FDN_LINE_STRIDE + writePos] = hi[k];
        }
        if (writePos == 0) {
            for (int ch = 0; ch < FDN_CHANNELS; ch++)
                fdnMem[ch * FDN_LINE_STRIDE + FDN_BUFFER_SIZE] = fdnMem[ch * FDN_LINE_STRIDE];
        }

        writePos = (writePos + 1) & FDN_BUFFER_MASK;
    }

    // ========================================================================
    // PARALLEL AUDIO BLOCK PROCESSOR
    // ========================================================================
    void processBlock(const float* in, float* out, int num_samples) {
        if (!initialized) return;
        const int frames = num_samples >> 1;
        if (frames <= 0) return;
        const AudioFpuScope fpu;    // FZ + DN for this render, put back after
        const float inv_frames = 1.0f / (float)frames;

        // Glow LFO: advance the phase by the whole block and interpolate sin
        // and cos linearly from their values at its start to those at its end.
        // The phase is in cycles, so it is scaled to radians here.  (It used
        // to go to fastersinfullf() as radians directly: the "sine" was
        // sin(0..1 rad), a ramp from 0 to 0.84 that snapped back each cycle,
        // so GLOW swept one-sided with a jump instead of the documented
        // +/- swing, and the right channel was offset by 0.25 rad, not 90
        // degrees.)  As before, GLOW 0% stops the LFO where it is.
        float lfo_s = lfo_sin_, lfo_c = lfo_cos_;
        if (glow_amt > 0.0f) {
            glow_lfo_phase += glowLfoRate * (float)frames;
            glow_lfo_phase -= (float)(int)glow_lfo_phase;
            lfo_sin_ = fastersinfullf(glow_lfo_phase * (float)M_TWOPI);
            lfo_cos_ = fastercosfullf(glow_lfo_phase * (float)M_TWOPI);
        }
        const float lfo_ds = (lfo_sin_ - lfo_s) * inv_frames;
        const float lfo_dc = (lfo_cos_ - lfo_c) * inv_frames;

        // SIZE and PDLY glide (see GLIDE_*).  The first block after a reset
        // starts on the targets: the host sets every parameter between
        // unit_init() and the first render, and that is not a knob move.
        if (!glides_primed_) {
            size_glide_.snap(sizeScale);
            pdly_glide_.snap(predelayScale * PREDELAY_MAX_SAMPLES);
            glides_primed_ = true;
        }
        float size = size_glide_.cur;
        const float size_step = size_glide_.plan(sizeScale, size_max_step_);
        float pdly = pdly_glide_.cur;
        const float pdly_step = pdly_glide_.plan(predelayScale * PREDELAY_MAX_SAMPLES, GLIDE_MAX_RATE);

        float path_sum = dark_amt + glow_amt + bright_amt + color_amt + spark_amt + irid_amt;
        float path_norm = path_sum > 0.0f ? (1.0f / fmaxf(1.0f, path_sum)) : 0.0f;

        for (int i = 0; i < num_samples; i += 2) {
            float in_l = in[i];
            float in_r = in[i+1];

            lfo_s += lfo_ds;
            lfo_c += lfo_dc;
            size += size_step;
            pdly += pdly_step;

            // PREDELAY (fractional, so a gliding PDLY reads smoothly)
            float mono_in = (in_l + in_r) * 0.5f;
            preDelayBuffer[preDelayWritePos] = mono_in;
            float pd_pos = (float)preDelayWritePos - pdly;
            if (pd_pos < 0.0f) pd_pos += (float)PREDELAY_BUFFER_SIZE;
            const int pd_i = (int)pd_pos;
            const float pd_f = pd_pos - (float)pd_i;
            const float pd_a = preDelayBuffer[pd_i & PREDELAY_MASK];
            const float pd_b = preDelayBuffer[(pd_i + 1) & PREDELAY_MASK];
            float pd_sig = pd_a + pd_f * (pd_b - pd_a);
            preDelayWritePos = (preDelayWritePos + 1) & PREDELAY_MASK;

            // 1. CORE FDN (Pure acoustic delays)
            float rev_l, rev_r;
            step_core_fdn(pd_sig, pd_sig, size, &rev_l, &rev_r);

            // Now 5 parallel paths to be summed up at the end


            // ==========================================
            // PATH 1: GLOW (Stereo Swirling SVF)
            // ==========================================
            float glow_l = 0.0f;
            float glow_r = 0.0f;
            if (glow_amt > 0.0f) {
                // Left Channel: Modulate coefficient directly.
                // f_coeff = 0.15 (~1150Hz base) +/- (0.10 * glow_amt) depth
                // 2. Shift the sweep up slightly:
                // Base 0.18 +/- 0.08 keeps the sweep entirely out of the muddy bass frequencies
                float lfo_val_l = lfo_s;
                float f_coeff_l = 0.18f + (0.08f * glow_amt * lfo_val_l);
                float q_coeff   = 0.8f; // Mild resonance to accentuate the sweep

                glow_lp_l += f_coeff_l * glow_bp_l;
                glow_bp_l += f_coeff_l * (rev_l - glow_lp_l - q_coeff * glow_bp_l);
                glow_l = glow_lp_l; // Take the Low-Pass output for warmth

                // Right Channel: 90-degree phase offset for stereo widening
                float lfo_val_r = lfo_c;
                float f_coeff_r = 0.15f + (0.10f * glow_amt * lfo_val_r);

                glow_lp_r += f_coeff_r * glow_bp_r;
                glow_bp_r += f_coeff_r * (rev_r - glow_lp_r - q_coeff * glow_bp_r);
                glow_r = glow_lp_r;
            }
            // ==========================================
            // PATH 2: DARK (Organic Pitch-Shifted Mono Sub-Bass)
            // ==========================================
            float dark_sig = 0.0f;
            float rev_mono = (rev_l + rev_r) * 0.5f;
            if (dark_amt > 0.0f) {
                // 1. Write the mono reverb tail to the dark buffer
                dark_buffer[dark_write] = rev_mono;
                dark_write = (dark_write + 1) & 4095;

                // 2. Advance the pitch shift phase (0.5 = exactly 1 octave down)
                dark_phase += 0.5f;
                if (dark_phase >= 2048.0f) dark_phase -= 2048.0f; // 42ms grain window

                // 3. Read Head 1 (Calculates interpolated audio)
                float r1 = (float)dark_write - dark_phase;
                if (r1 < 0.0f) r1 += 4096.0f;
                int i1 = (int)r1;
                float f1 = r1 - i1;
                float out1 = dark_buffer[i1 & 4095] + f1 * (dark_buffer[(i1 + 1) & 4095] - dark_buffer[i1 & 4095]);

                // 4. Read Head 2 (Offset by exactly half the window to hide the looping click)
                // Replace fmodf (expensive libc call) with conditional subtraction.
                // dark_phase ∈ [0, 2048), so dark_phase + 1024 ∈ [1024, 3072) → at most one wrap.
                float dp2 = dark_phase + 1024.0f;
                if (dp2 >= 2048.0f) dp2 -= 2048.0f;
                float r2 = (float)dark_write - dp2;
                if (r2 < 0.0f) r2 += 4096.0f;
                int i2 = (int)r2;
                float f2 = r2 - i2;
                float out2 = dark_buffer[i2 & 4095] + f2 * (dark_buffer[(i2 + 1) & 4095] - dark_buffer[i2 & 4095]);

                // 5. Crossfade Envelope (Triangle wave tracking the phase)
                // Head 1 is at maximum volume halfway through its window, and muted when it snaps back.
                float fade1 = 1.0f - fabsf((dark_phase - 1024.0f) / 1024.0f);
                float fade2 = 1.0f - fade1;

                float pitched_down = (out1 * fade1) + (out2 * fade2);

                // 6. Smooth Low-Pass Filter (Removes any granular artifacts and leaves pure, deep sub)
                // A coefficient of 0.05f creates a heavy low-pass around ~300Hz
                dark_lpf_state += 0.05f * (pitched_down - dark_lpf_state);

                // 7. Final output with makeup gain to compensate for the heavy filtering
                dark_sig = dark_lpf_state * 2.5f;
            }
            // ==========================================
            // PATH 3: BRIGHT (Harmonic Exciter Air)
            // ==========================================
            float bright_l = 0.0f;
            float bright_r = 0.0f;
            if (bright_amt > 0.0f) {
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
                bright_l = drive_l * (1.0f - (drive_l * drive_l * 0.33333f));
                bright_r = drive_r * (1.0f - (drive_r * drive_r * 0.33333f));
            }
            // ==========================================
            // PATH 4: COLOR (Stereo Visual Spectrum Resonators)
            // ==========================================
            // Drive from the FDN reverb output so the resonators colour the
            // reverb tail rather than the dry signal.  Driving from in_l/in_r
            // produced no audible effect for low-mid drum content because those
            // signals carry negligible energy in the 4-7 kHz resonator band.
            // The reverb tail has broad-band content and reliably excites the
            // high-Q resonators, creating a metallic / spring-like colouring.
            float color_l = 0.0f;
            float color_r = 0.0f;
            if (color_amt > 0.0f) {
                // 6 parallel bandpass biquads per side, evaluated NEON_LANES-at-a-time on
                // NEON (8 padded lanes = two vector groups). Same Direct-Form
                // recurrence as process_biquad(); padding lanes stay zero.
                const float32x4_t inl = vdupq_n_f32(rev_l);
                const float32x4_t inr = vdupq_n_f32(rev_r);
                float32x4_t sumL = vdupq_n_f32(0.0f);
                float32x4_t sumR = vdupq_n_f32(0.0f);
                for (int g = 0; g < 8; g += NEON_LANES) {
                    float32x4_t b0 = vld1q_f32(&col_b0[g]);
                    float32x4_t b1 = vld1q_f32(&col_b1[g]);
                    float32x4_t b2 = vld1q_f32(&col_b2[g]);
                    float32x4_t a1 = vld1q_f32(&col_a1[g]);
                    float32x4_t a2 = vld1q_f32(&col_a2[g]);
                    // Left
                    float32x4_t z1 = vld1q_f32(&col_z1l[g]);
                    float32x4_t z2 = vld1q_f32(&col_z2l[g]);
                    float32x4_t out = vmlaq_f32(z1, inl, b0);          // in*b0 + z1
                    float32x4_t nz1 = vmlsq_f32(vmlaq_f32(z2, inl, b1), out, a1); // in*b1 - out*a1 + z2
                    float32x4_t nz2 = vmlsq_f32(vmulq_f32(inl, b2), out, a2);     // in*b2 - out*a2
                    vst1q_f32(&col_z1l[g], nz1);
                    vst1q_f32(&col_z2l[g], nz2);
                    sumL = vaddq_f32(sumL, out);
                    // Right (same coefficients)
                    z1 = vld1q_f32(&col_z1r[g]);
                    z2 = vld1q_f32(&col_z2r[g]);
                    out = vmlaq_f32(z1, inr, b0);
                    nz1 = vmlsq_f32(vmlaq_f32(z2, inr, b1), out, a1);
                    nz2 = vmlsq_f32(vmulq_f32(inr, b2), out, a2);
                    vst1q_f32(&col_z1r[g], nz1);
                    vst1q_f32(&col_z2r[g], nz2);
                    sumR = vaddq_f32(sumR, out);
                }
                // Horizontal sum of the 6 (+2 zero) resonator outputs per side.
                float32x2_t sl = vadd_f32(vget_low_f32(sumL), vget_high_f32(sumL));
                float32x2_t sr = vadd_f32(vget_low_f32(sumR), vget_high_f32(sumR));
                color_l = vget_lane_f32(vpadd_f32(sl, sl), 0);
                color_r = vget_lane_f32(vpadd_f32(sr, sr), 0);
                // Scale down since we are summing 6 high-Q resonant peaks. - NOTE commented out for the moment, to try louder effect
                // color_l *= 0.50f;
                // color_r *= 0.50f;
            }
            // ==========================================
            // PATH 5: SPARKLE (Stereo Pitched-up S&H Pops)
            // ==========================================
            float spark_l = 0.0f;
            float spark_r = 0.0f;
            if (spark_amt > 0.0f) {
                sparkle_buffer_l[spark_write] = rev_l;
                sparkle_buffer_r[spark_write] = rev_r;
                spark_write = (spark_write + 1) & (SPARKLE_BUFFER_SIZE - 1);

                if (spark_countdown > 0) {
                    int r_idx = (int)spark_read & (SPARKLE_BUFFER_SIZE - 1);
                    // Parabolic envelope: rises and falls over the grain duration.
                    // env = 4 * pos * (1 - pos)  where pos runs 0→1 over the grain.
                    float pos = 1.0f - (float)spark_countdown * spark_inv_duration;
                    float env = 4.0f * pos * (1.0f - pos);
                    spark_l = sparkle_buffer_l[r_idx] * spark_pan_l * env;
                    spark_r = sparkle_buffer_r[r_idx] * spark_pan_r * env;
                    spark_read += spark_speed;
                    spark_countdown--;
                } else {
                    // Xorshift inline PRNG
                    static uint32_t seed = 2463574242UL;
                    seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5;
                    float rand_val = (float)seed / 4294967295.0f;

                    if (rand_val < (0.0001f + (spark_amt * 0.005f))) {
                        // Halved grain duration: 5-15 ms (was 10-30 ms)
                        spark_duration = 250 + (int)(seed % 500);
                        spark_countdown = spark_duration;
                        spark_inv_duration = 1.0f / (float)spark_duration;
                        // More varied pitch ratios: +5, +7, +12, +19, +24 semitones
                        static const float kSpeeds[5] = {1.41f, 1.68f, 2.0f, 2.83f, 4.0f};
                        spark_speed = kSpeeds[seed % 5];
                        // The read head gains (speed - 1) samples per sample
                        // on the write head, so start it far enough back to
                        // still be behind it when the grain ends.  It used to
                        // start only `duration` back, and at +19 and +24
                        // semitones it overtook the write head a third of the
                        // way in, near the envelope's peak, and jumped to
                        // audio 85 ms old: a click in two grains out of five.
                        spark_read = (float)spark_write
                                   - ((float)spark_duration * (spark_speed - 1.0f) + 2.0f);
                        if(spark_read < 0.0f) spark_read += (float)SPARKLE_BUFFER_SIZE;

                        spark_pan_l = (float)(seed % 100) * 0.01f;
                        spark_pan_r = 1.0f - spark_pan_l;
                    }
                }
            }
            // ==========================================
            // PATH 6: IRIDESCENZA (Formerly iridiscence)
            // ==========================================
            // A swirling, stereo-panning, saturated optical halo.
            float irid_l = 0.0f;
            float irid_r = 0.0f;
            if (irid_amt > 0.0f) {
                // 1. Refraction: Speed wobbles microscopically around .9x
                // Driven by the glow LFO.  Same mean and depth as before the
                // LFO fix (0.9 + 0.015 * sin(0..0.5 rad) spanned 0.9..0.9072,
                // mean 0.90367), now as a smooth sine instead of a ramp that
                // snapped back once per cycle.
                float irid_speed = 0.90367f + lfo_s * 0.0036f;
                irid_phase += irid_speed;
                if (irid_phase >= 2048.0f) irid_phase -= 2048.0f;
                float irid_mono = (rev_l + rev_r) * 0.5f;

                iridiscensce_buffer[iridiscensce_write] = irid_mono;
                iridiscensce_write = (iridiscensce_write + 1) & 4095;
                float rs1 = (float)iridiscensce_write - irid_phase;
                if (rs1 < 0.0f) rs1 += 4096.0f;
                int is1 = (int)rs1;
                float fs1 = rs1 - is1;
                float so1 = iridiscensce_buffer[is1 & 4095] + fs1 * (iridiscensce_buffer[(is1+1) & 4095] - iridiscensce_buffer[is1 & 4095]);

                float phase_b = irid_phase + 1024.0f;
                if (phase_b >= 2048.0f) phase_b -= 2048.0f;
                float rs2 = (float)iridiscensce_write - phase_b;
                if (rs2 < 0.0f) rs2 += 4096.0f;
                int is2 = (int)rs2;
                float fs2 = rs2 - is2;
                float so2 = iridiscensce_buffer[is2 & 4095] + fs2 * (iridiscensce_buffer[(is2+1) & 4095] - iridiscensce_buffer[is2 & 4095]);

                // 2. The "Holo-Fade"
                float fade = 1.0f - fabsf((irid_phase - 1024.0f) / 1024.0f);

                // 3. Chromatic Aberration (Stereo Splitting)
                // Instead of summing to mono, Head 1 favors Left and Head 2 favors Right!
                // As they fade in and out, the iridiscence swirls across the stereo image.
                float irid_raw_l = (so1 * fade) + (so2 * (1.0f - fade) * 0.29f);
                float irid_raw_r = (so2 * fade) + (so1 * (1.0f - fade) * 0.31f);

                // 4. Luminescence (Soft Saturation)
                // Pushing it into a fast_tanh creates high-frequency density ("glow")
                // Asymmetric drive for harmonic stereo widening
                irid_raw_l = fast_tanh(irid_raw_l * 1.359f);
                irid_raw_r = fast_tanh(irid_raw_r * 1.703f);

                // 5. Taming the harshness (Stereo LPF)
                // Asymmetric filtering: Right side is brighter and fizzier
                irid_lpf_l += 0.1409f * (irid_raw_l - irid_lpf_l);
                irid_lpf_r += 0.1603f * (irid_raw_r - irid_lpf_r);

                irid_l = irid_lpf_l;
                irid_r = irid_lpf_r;
            }
            // ==========================================
            // FINAL PARALLEL MIXDOWN
            // ==========================================
            // FDN reverb (rev_l/rev_r) is always the base — SIZE/DECAY/BASS remain
            // audible even when all path modifiers are at zero.
            float path_l = (dark_sig * dark_amt) + (glow_l * glow_amt) + (bright_l * bright_amt) +
                           (color_l * color_amt) + (spark_l * spark_amt) + (irid_l * irid_amt);
            float path_r = (dark_sig * dark_amt) + (glow_r * glow_amt) + (bright_r * bright_amt) +
                           (color_r * color_amt) + (spark_r * spark_amt) + (irid_r * irid_amt);

            float mix_l = rev_l + path_l * path_norm;
            float mix_r = rev_r + path_r * path_norm;

            // Mid-side stereo width on wet signal
            float mid  = (mix_l + mix_r) * 0.5f;
            float side = (mix_l - mix_r) * 0.5f * width_amt;

            out[i]   = mid + side;
            out[i+1] = mid - side;
        }
        size_glide_.cur = size;
        pdly_glide_.cur = pdly;
    }
};
