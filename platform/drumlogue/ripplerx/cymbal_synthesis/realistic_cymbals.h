#ifndef REALISTIC_CYMBALS_H
#define REALISTIC_CYMBALS_H

#include <stdint.h>
#include <math.h>

// C++11, real-time safe cymbal voice: no heap allocation, no STL containers.
namespace realistic_cymbals {

enum Preset { PRESET_CRASH = 0, PRESET_RIDE, PRESET_SPLASH, PRESET_GONG, PRESET_COUNT };

struct PresetConfig {
  float minHz, maxHz;
  const float *frequencyHz;
  uint16_t frequencyCount;
  float frequencyJitterSemitones;
  float ampAttackSec, ampDecaySec, ampSustainLevel, ampReleaseSec;
  float lowAttackSec, decaySec, highAttackSec, highDecaySec;
  float thwackSec, stickLevel, noiseLevel, resonatorLevel, shimmerLevel;
  float directNoiseLevel, combLevel, phaseModDepth;
  uint16_t resonators;
};

struct RenderParams {
  RenderParams()
      : preset(PRESET_CRASH), velocity(0.8f), muffle(0.0f), comb(0.0f),
        phaseMod(0.0f), durationSec(0.0f) {}

  Preset preset;
  float velocity; // 0..1: harder = brighter, louder, longer, more stick.
  float muffle;   // 0..1: higher = hand-damped shorter, darker cymbal.
  float comb;     // 0..1: optional metallic short-delay enhancement.
  float phaseMod; // 0..1: optional nonlinear shimmer/chaos enhancement.
  float durationSec; // >0: fit envelopes to this one-shot render duration.
};

class CymbalSynth {
public:
  enum { kMaxResonators = 128, kCombTaps = 4, kCombMaxDelay = 2048 };
  static_assert(kMaxResonators % 4 == 0, "kMaxResonators must be a multiple of 4 for NEON loop safety");
  explicit CymbalSynth(float sampleRate = 48000.0f);
  void setSampleRate(float sampleRate);
  void noteOn(const RenderParams &params, uint32_t seed = 0x12345678u);
  float process();
  void process(float *out, uint32_t frames);
  bool isActive() const { return active_; }
  static const PresetConfig &presetConfig(Preset preset);

private:
  float white();
  float pink();
  float onePoleLow(float input, float cutoffHz, float &state) const;
  float onePoleHigh(float input, float cutoffHz, float &state) const;
  void  initialiseResonators(const PresetConfig &cfg, float velocity, float muffle, float durationSec, uint32_t seed);

  float sampleRate_, invSampleRate_;
  uint32_t rng_;
  uint32_t sampleIndex_;
  float velocity_, muffle_, combAmount_, phaseModAmount_;
  const PresetConfig *cfg_;
  bool active_;

  // Per-note constants, folded once in noteOn() so process() stays branch-light
  // and free of libm calls in the steady state.
  float velocityGain_;      // output level: harder strikes are louder
  float resonatorNorm_;     // resonatorLevel / sqrt(count): incoherent-sum norm
  float noiseGain_;         // cfg noiseLevel
  float whiteBlend_;        // velocity-dependent drive whiteness
  float shimmerScale_;      // shimmerLevel * velocity * (1 - muffle)
  float maxCutoff_;         // velocity/muffle-scaled driver cutoff ceiling
  float pmDepthBase_;       // phaseModDepth * phaseMod knob
  float pmRateBase_;        // velocity scaling of the PM LFO rates
  float thwackGain_;        // stickLevel * velocity
  uint32_t thwackSamples_;  // stick burst length in samples
  float thwackTauInv_;      // 1 / (thwackSec - attack)
  uint32_t releaseStartSample_; // fixed-duration release fade start
  float releaseInvSamples_;     // 1 / release length (per-sample fade slope)
  uint32_t endSample_;          // voice auto-off point

  // Recursive one-pole envelope states + per-sample multipliers. Each
  // exponential env is value = (1 - atkState) * decState with both states
  // decaying geometrically; multipliers use exact expf at noteOn time.
  float lowAtk_, lowAtkMul_, lowDec_, lowDecMul_;
  float hiAtk_, hiAtkMul_, hiDec_, hiDecMul_;
  float strikeAtk_, strikeAtkMul_, strikeDec_, strikeDecMul_;

  // Resonator bank in structure-of-arrays layout: the per-sample recursion
  // y = b0*x + a1*y1 + a2*y2 is independent per resonator, so contiguous
  // coefficient/state arrays map directly onto 4-wide NEON lanes (and help
  // host auto-vectorisation). Count is padded to a multiple of 4 with
  // all-zero lanes so the SIMD loop needs no scalar tail.
  uint16_t resonatorCount_;
  float resB0_[kMaxResonators];
  float resA1_[kMaxResonators];
  float resA2_[kMaxResonators];
  float resY1_[kMaxResonators];
  float resY2_[kMaxResonators];
  float resGain_[kMaxResonators];

  float lpState_, hpLowState_, dcState_, pinkState_[7];
  float comb_[kCombTaps][kCombMaxDelay];
  uint16_t combWrite_, combDelay_[kCombTaps];
  float pmPhase_[3];
};

// Polyphonic wrapper: fixed pool of voices allocated round-robin so repeated
// strikes stack instead of choking the previous tail (cymbal rolls, crash
// over ride tail, etc.). Free (inactive) voices are preferred; otherwise the
// oldest voice is stolen.
class CymbalKit {
public:
  enum { kVoices = 4 };

  explicit CymbalKit(float sampleRate = 48000.0f);
  void setSampleRate(float sampleRate);
  void noteOn(const RenderParams &params, uint32_t seed = 0x12345678u);
  float process();
  void process(float *out, uint32_t frames);
  bool isActive() const;
  uint16_t activeVoices() const;

private:
  CymbalSynth voices_[kVoices];
  uint32_t voiceAge_[kVoices];
  uint32_t noteCounter_;
};

} // namespace realistic_cymbals

#endif
