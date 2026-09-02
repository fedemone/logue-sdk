#pragma once
/**
 * @file synth.h
 * @brief LevelRef -- calibrated reference signal generator for the drumlogue.
 *
 * Why this exists
 * ---------------
 * "The synth track is too quiet" is not measurable from inside a unit.  The
 * unit knows what it puts on the bus (platform/drumlogue/tools/level_meter
 * measures exactly that, on the host), but it cannot know what the drumlogue
 * does with it afterwards, or how loud the drum tracks it is competing with
 * are.  Those two unknowns are why raising a unit's gain is guesswork.
 *
 * LevelRef removes them.  It emits a signal of a KNOWN loudness, so the synth
 * track can be compared against a drum track with one number instead of an
 * impression:
 *
 *   1. Load LevelRef on the synth track.  Signal = PinkNz, Mode = Drone,
 *      TgtLUFS = -20.  It sounds immediately, no note required.
 *   2. Put a drum pattern on another track.  Set BOTH track faders to the
 *      same position.
 *   3. Turn TgtLUFS until the noise and the drums sound equally loud.
 *
 * The value it lands on is the drum bus's loudness in the same units the
 * meter reports for the synth units, and the difference between it and a
 * unit's measured LUFS is exactly how much that unit is short.  If it lands
 * near a unit's measured value and the balance is still wrong, the deficit is
 * not in the unit and no amount of gain in the unit will fix it.
 *
 * Calibration
 * -----------
 * Each signal's amplitude is derived from the requested LUFS through a fixed
 * per-signal offset, kSignalLufsAtUnity: the loudness that signal measures at
 * amplitude 1.0.  Those constants were measured with tools/level_meter (gated
 * BS.1770), not derived on paper, so what the screen says is what a meter
 * reads back.  Re-measure and update them if a generator changes.
 *
 * The output is mono, written identically to L and R, which is what BS.1770
 * sums -- the same geometry every other unit here is measured in.
 */

#include <cstdint>
#include <cstddef>
#include <cmath>
#include <cstring>

#include "unit.h"

/**
 * Loudness in LUFS that each signal measures at amplitude 1.0, mono into both
 * channels, and its crest factor in dB.  MEASURED with tools/level_meter
 * (gated BS.1770) against this exact generator -- not derived on paper.  The
 * first table is what makes the TgtLUFS knob mean what it says; the second
 * lets PeakdB warn about a clipping setting before it happens.  Re-measure
 * both if a generator changes.
 */
static constexpr float kSignalLufsAtUnity[5] = {
    -5.68f,   // PinkNz   (Kellet economy pink, normalized -- see Generate)
    +0.01f,   // Sine1k   (K-weighting is about +0.7 dB at 1 kHz)
    -1.83f,   // Sine100  (and rolls off below it)
    +1.38f,   // WhitNz   (uniform, not Gaussian -- crest is exactly sqrt(3))
     0.0f     // Silence  (unused)
};

/** Peak level each generator reaches at amplitude 1.0, in dBFS. */
static constexpr float kSignalPeakAtUnityDb[5] = {
    +3.43f,   // PinkNz   (the pink filter overshoots its white input)
     0.0f,    // Sine1k
     0.0f,    // Sine100
     0.0f,    // WhitNz
     0.0f     // Silence
};

class LevelRef {
 public:
  enum SignalId : int32_t {
    k_sig_pink = 0,
    k_sig_sine1k,
    k_sig_sine100,
    k_sig_white,
    k_sig_silence,
    k_sig_count
  };

  enum ParamIndex : uint8_t {
    k_param_signal = 0,
    k_param_target,
    k_param_mode,
    k_param_peak,
    k_param_count = 24
  };

  LevelRef() = default;
  ~LevelRef() = default;

  inline int8_t Init(const unit_runtime_desc_t* desc) {
    if (!desc) return k_unit_err_undef;
    if (desc->samplerate != 48000) return k_unit_err_samplerate;
    if (desc->output_channels != 2) return k_unit_err_geometry;
    Reset();
    return k_unit_err_none;
  }

  inline void Teardown() {}

  inline void Reset() {
    rng_ = 0x4C766C52u;
    pink_b0_ = pink_b1_ = pink_b2_ = 0.0f;
    phase_ = 0.0f;
    gated_on_ = false;
    UpdateAmplitude();
  }

  inline void Resume() { Reset(); }
  inline void Suspend() {}

  inline void Render(float* out, size_t frames) {
    const bool sounding = (signal_ != k_sig_silence) && (mode_ == 0 || gated_on_);
    if (!sounding) {
      memset(out, 0, frames * 2 * sizeof(float));
      return;
    }
    for (size_t i = 0; i < frames; ++i) {
      float s = amp_ * Generate();
      // Reference signals must stay linear, so there is no soft clip here;
      // this only stops a nonsensical setting reaching the DAC past full
      // scale.  The PeakdB read-out says when a setting is close.
      if (s > 0.999f) s = 0.999f;
      else if (s < -0.999f) s = -0.999f;
      out[i * 2] = s;
      out[i * 2 + 1] = s;
    }
  }

  inline void NoteOn(uint8_t note, uint8_t velocity) {
    (void)note;
    // Deliberately velocity-independent: a reference whose level depended on
    // how the sequencer was programmed would not be a reference.
    if (velocity == 0) { gated_on_ = false; return; }
    gated_on_ = true;
  }

  inline void NoteOff(uint8_t note) { (void)note; gated_on_ = false; }
  inline void GateOn(uint8_t velocity) { gated_on_ = (velocity != 0); }
  inline void GateOff() { gated_on_ = false; }
  inline void AllNoteOff() { gated_on_ = false; }
  inline void PitchBend(uint16_t bend) { (void)bend; }
  inline void ChannelPressure(uint8_t pressure) { (void)pressure; }
  inline void Aftertouch(uint8_t note, uint8_t aftertouch) { (void)note; (void)aftertouch; }

  inline void setParameter(uint8_t index, int32_t value) {
    switch (index) {
      case k_param_signal:
        signal_ = (value < 0) ? 0 : ((value >= k_sig_count) ? k_sig_count - 1 : value);
        ClampTargetToPeakSafe();
        UpdateAmplitude();
        break;
      case k_param_target:
        target_lufs_ = (value < -40) ? -40 : ((value > 0) ? 0 : value);
        ClampTargetToPeakSafe();
        UpdateAmplitude();
        break;
      case k_param_mode:
        mode_ = (value != 0) ? 1 : 0;
        break;
      default:
        break;   // k_param_peak is a read-out, not a control
    }
  }

  inline int32_t getParameterValue(uint8_t index) const {
    switch (index) {
      case k_param_signal: return signal_;
      case k_param_target: return target_lufs_;
      case k_param_mode:   return mode_;
      case k_param_peak:   return peak_dbfs_;
      default:             return 0;
    }
  }

  inline const char* getParameterStrValue(uint8_t index, int32_t value) const {
    static const char* const kSignalNames[k_sig_count] = {
        "PinkNz", "Sine1k", "Sine100", "WhitNz", "Silence"};
    static const char* const kModeNames[2] = {"Drone", "Gated"};
    switch (index) {
      case k_param_signal:
        if (value >= 0 && value < k_sig_count) return kSignalNames[value];
        break;
      case k_param_mode:
        if (value >= 0 && value < 2) return kModeNames[value];
        break;
      default:
        break;
    }
    return nullptr;
  }

  inline const uint8_t* getParameterBmpValue(uint8_t index, int32_t value) const {
    (void)index; (void)value;
    return nullptr;
  }

  // ---- Preset interface: LevelRef has no presets, but the runtime's entry
  // points are unconditional, so they still have to resolve. ----------------
  inline void LoadPreset(uint8_t idx) { (void)idx; }
  inline uint8_t getPresetIndex() const { return 0; }
  static inline const char* getPresetName(uint8_t idx) { (void)idx; return nullptr; }

 private:
  /**
   * A reference that distorts is worse than no reference, so the target is
   * capped at the loudest value this signal can reach with its peak still
   * under full scale.  It is a property of the signal's crest factor, not of
   * the scaling: pink noise runs 12.1 dB peak-to-RMS, so it cannot be taken
   * past about -9 LUFS however it is normalized.  getParameterValue() returns
   * the capped value, so the knob stops where the signal does.
   */
  inline void ClampTargetToPeakSafe() {
    if (signal_ == k_sig_silence) return;
    const int32_t max_target = (int32_t)floorf(kSignalLufsAtUnity[signal_]
                                               - kSignalPeakAtUnityDb[signal_]);
    if (target_lufs_ > max_target) target_lufs_ = max_target;
  }

  inline void UpdateAmplitude() {
    if (signal_ == k_sig_silence) { amp_ = 0.0f; peak_dbfs_ = -99; return; }
    // amp such that the generator's output measures target_lufs_.
    const float delta_db = (float)target_lufs_ - kSignalLufsAtUnity[signal_];
    amp_ = powf(10.0f, delta_db * (1.0f / 20.0f));
    // The generator is scaled by (target - LUFS at unity), so its peak moves
    // by the same amount:  peak = target - LUFS_at_unity + peak_at_unity.
    const float peak_db = delta_db + kSignalPeakAtUnityDb[signal_];
    int32_t p = (int32_t)lrintf(peak_db);
    peak_dbfs_ = (p > 0) ? 0 : ((p < -99) ? -99 : p);
  }

  inline float White() {
    // xorshift32 -- deterministic, real-time safe, no libc state.
    uint32_t x = rng_;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    rng_ = x;
    return (float)x * (2.0f / 4294967295.0f) - 1.0f;
  }

  inline float Generate() {
    switch (signal_) {
      case k_sig_pink: {
        // Paul Kellet's economy pink filter (-3 dB/octave to within ~0.5 dB
        // from 20 Hz to 20 kHz).  The 0.216 normalizes it back to roughly the
        // white input's RMS; the exact value does not matter because
        // kSignalLufsAtUnity absorbs it, but keeping it near unity keeps amp_
        // in a sane range.
        const float w = White();
        pink_b0_ = 0.99765f * pink_b0_ + w * 0.0990460f;
        pink_b1_ = 0.96300f * pink_b1_ + w * 0.2965164f;
        pink_b2_ = 0.57000f * pink_b2_ + w * 1.0526913f;
        return (pink_b0_ + pink_b1_ + pink_b2_ + w * 0.1848f) * 0.216f;
      }
      case k_sig_sine1k:
      case k_sig_sine100: {
        const float hz = (signal_ == k_sig_sine1k) ? 1000.0f : 100.0f;
        phase_ += hz * (1.0f / 48000.0f);
        if (phase_ >= 1.0f) phase_ -= 1.0f;
        // A reference tone is worth an exact sine: this unit generates one
        // signal and nothing else, so libm's cost here is irrelevant.
        return sinf(phase_ * 6.2831853f);
      }
      case k_sig_white:
        return White();
      default:
        return 0.0f;
    }
  }

  int32_t signal_ = k_sig_pink;
  int32_t target_lufs_ = -20;
  int32_t mode_ = 0;
  int32_t peak_dbfs_ = 0;

  float amp_ = 0.0f;
  float phase_ = 0.0f;
  float pink_b0_ = 0.0f, pink_b1_ = 0.0f, pink_b2_ = 0.0f;
  uint32_t rng_ = 0x4C766C52u;
  bool gated_on_ = false;
};

typedef LevelRef Synth;
