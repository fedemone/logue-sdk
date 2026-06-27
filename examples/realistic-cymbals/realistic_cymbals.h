#ifndef REALISTIC_CYMBALS_H
#define REALISTIC_CYMBALS_H

#include <stdint.h>

// C++11, real-time safe cymbal voice: no heap allocation, no STL containers.
namespace realistic_cymbals {

enum Preset { PRESET_CRASH = 0, PRESET_RIDE, PRESET_SPLASH, PRESET_GONG, PRESET_COUNT };

struct PresetConfig {
  float minHz, maxHz;
  float lowAttackSec, decaySec, highAttackSec, highDecaySec;
  float thwackSec, stickLevel, noiseLevel, resonatorLevel, shimmerLevel;
  float directNoiseLevel, combLevel, phaseModDepth;
  uint16_t resonators;
};

struct RenderParams {
  Preset preset;
  float velocity; // 0..1: harder = brighter, louder, longer, more stick.
  float muffle;   // 0..1: higher = hand-damped shorter, darker cymbal.
  float comb;     // 0..1: optional metallic short-delay enhancement.
  float phaseMod; // 0..1: optional nonlinear shimmer/chaos enhancement.
};

class CymbalSynth {
public:
  enum { kMaxResonators = 128, kCombTaps = 4, kCombMaxDelay = 2048 };

  explicit CymbalSynth(float sampleRate = 48000.0f);
  void setSampleRate(float sampleRate);
  void noteOn(const RenderParams &params, uint32_t seed = 0x12345678u);
  float process();
  void process(float *out, uint32_t frames);
  bool isActive() const { return active_; }
  static const PresetConfig &presetConfig(Preset preset);

private:
  struct Resonator { float b0, a1, a2, y1, y2, baseHz, gain; };
  float white();
  float pink();
  float expEnv(float attackSec, float decaySec) const;
  float onePoleLow(float input, float cutoffHz, float &state) const;
  float onePoleHigh(float input, float cutoffHz, float &state) const;
  void initialiseResonators(const PresetConfig &cfg, float velocity, float muffle, uint32_t seed);

  float sampleRate_, invSampleRate_;
  uint32_t rng_;
  uint32_t sampleIndex_;
  float velocity_, muffle_, combAmount_, phaseModAmount_;
  const PresetConfig *cfg_;
  bool active_;

  Resonator resonators_[kMaxResonators];
  uint16_t resonatorCount_;
  float lpState_, hpLowState_, dcState_, pinkState_[7];
  float comb_[kCombTaps][kCombMaxDelay];
  uint16_t combWrite_, combDelay_[kCombTaps];
  float pmPhase_[3];
};

} // namespace realistic_cymbals

#endif
