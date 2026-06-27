#include "realistic_cymbals.h"
// Inspired by Dan Stowell's SuperCollider cymbal synthesis tutorial:
// https://mcld.co.uk/cymbalsynthesis/
#include <math.h>

namespace realistic_cymbals {
namespace {

const float kPi = 3.14159265358979323846f;
const float kRingz60Db = 6.90775527898213705205f; // -log(0.001)

float clampf(float x, float lo, float hi) {
  return x < lo ? lo : (x > hi ? hi : x);
}

float lerpf(float a, float b, float t) {
  return a + (b - a) * t;
}

float frand(uint32_t &s) {
  s = s * 1664525u + 1013904223u;
  return ((s >> 8) * (1.0f / 16777216.0f));
}

float exprand(uint32_t &s, float lo, float hi) {
  return lo * powf(hi / lo, frand(s));
}

} // namespace

static const PresetConfig kPresets[PRESET_COUNT] = {
  // min max  lowA decay hiA hiD thwack stick noise res shimmer direct comb pm modes
  { 300.f, 20000.f, 0.50f, 5.5f, 1.0f, 3.0f, 0.002f, 0.55f, 0.10f, 0.55f, 0.25f, 0.18f, 0.12f, 0.08f, 100 },
  { 450.f, 18000.f, 0.20f, 4.0f, 0.7f, 2.6f, 0.0015f, 0.45f, 0.09f, 0.48f, 0.32f, 0.10f, 0.08f, 0.06f, 96 },
  { 3500.f, 20000.f, 0.08f, 1.1f, 0.25f, 0.8f, 0.0012f, 0.65f, 0.10f, 0.50f, 0.22f, 0.16f, 0.06f, 0.05f, 72 },
  { 150.f, 14000.f, 2.50f, 18.0f, 3.0f, 8.0f, 0.020f, 0.25f, 0.08f, 0.65f, 0.18f, 0.08f, 0.18f, 0.12f, 80 }
};

CymbalSynth::CymbalSynth(float sr) {
  setSampleRate(sr);
  cfg_ = &kPresets[0];
  active_ = false;
  rng_ = 1u;
  sampleIndex_ = 0u;
  resonatorCount_ = 0u;
  lpState_ = hpLowState_ = dcState_ = 0.0f;
  combWrite_ = 0u;
  for (int i = 0; i < 7; ++i) {
    pinkState_[i] = 0.0f;
  }
  for (int tap = 0; tap < kCombTaps; ++tap) {
    combDelay_[tap] = 101u;
    for (int i = 0; i < kCombMaxDelay; ++i) {
      comb_[tap][i] = 0.0f;
    }
  }
  for (int i = 0; i < 3; ++i) {
    pmPhase_[i] = 0.0f;
  }
}

void CymbalSynth::setSampleRate(float sr) {
  sampleRate_ = sr > 1000.0f ? sr : 48000.0f;
  invSampleRate_ = 1.0f / sampleRate_;
}

const PresetConfig &CymbalSynth::presetConfig(Preset p) {
  return kPresets[(p >= 0 && p < PRESET_COUNT) ? p : PRESET_CRASH];
}

void CymbalSynth::noteOn(const RenderParams &p, uint32_t seed) {
  cfg_ = &presetConfig(p.preset);
  velocity_ = clampf(p.velocity, 0.01f, 1.0f);
  muffle_ = clampf(p.muffle, 0.0f, 1.0f);
  combAmount_ = clampf(p.comb, 0.0f, 1.0f);
  phaseModAmount_ = clampf(p.phaseMod, 0.0f, 1.0f);
  rng_ = seed ? seed : 0x12345678u;
  sampleIndex_ = 0u;
  active_ = true;
  lpState_ = hpLowState_ = dcState_ = 0.0f;
  for (int i = 0; i < 7; ++i) {
    pinkState_[i] = 0.0f;
  }
  for (int tap = 0; tap < kCombTaps; ++tap) {
    for (int i = 0; i < kCombMaxDelay; ++i) {
      comb_[tap][i] = 0.0f;
    }
  }
  combWrite_ = 0u;
  initialiseResonators(*cfg_, velocity_, muffle_, rng_);
}

void CymbalSynth::initialiseResonators(const PresetConfig &cfg,
                                       float vel,
                                       float muff,
                                       uint32_t seed) {
  resonatorCount_ = (cfg.resonators > (uint16_t)kMaxResonators)
                        ? (uint16_t)kMaxResonators
                        : cfg.resonators;

  // SuperCollider Ringz defaults to a one-second 60 dB decay. The tutorial
  // gets most of the frequency-dependent decay from the filtered driver, not
  // from per-mode Q changes, so keep the resonator decay mostly uniform.
  const float ringDecay = lerpf(0.55f, 1.15f, vel) * lerpf(1.0f, 0.18f, muff);
  const float r = expf(-kRingz60Db / (ringDecay * sampleRate_));

  for (uint16_t i = 0u; i < resonatorCount_; ++i) {
    const float f = exprand(seed, cfg.minHz, cfg.maxHz);
    const float w = 2.0f * kPi * f * invSampleRate_;
    resonators_[i].b0 = sqrtf(1.0f - r * r);
    resonators_[i].a1 = 2.0f * r * cosf(w);
    resonators_[i].a2 = -(r * r);
    resonators_[i].y1 = 0.0f;
    resonators_[i].y2 = 0.0f;
    resonators_[i].baseHz = f;
    resonators_[i].gain = 0.85f + 0.30f * frand(seed);
  }

  const float delays[kCombTaps] = { 0.0037f, 0.0051f, 0.0079f, 0.0113f };
  for (int tap = 0; tap < kCombTaps; ++tap) {
    combDelay_[tap] = (uint16_t)clampf(delays[tap] * sampleRate_,
                                      1.0f,
                                      (float)(kCombMaxDelay - 1));
  }
}

float CymbalSynth::white() {
  return frand(rng_) * 2.0f - 1.0f;
}

float CymbalSynth::pink() {
  const float w = white();
  pinkState_[0] = 0.99886f * pinkState_[0] + w * 0.0555179f;
  pinkState_[1] = 0.99332f * pinkState_[1] + w * 0.0750759f;
  pinkState_[2] = 0.96900f * pinkState_[2] + w * 0.1538520f;
  pinkState_[3] = 0.86650f * pinkState_[3] + w * 0.3104856f;
  pinkState_[4] = 0.55000f * pinkState_[4] + w * 0.5329522f;
  pinkState_[5] = -0.7616f * pinkState_[5] - w * 0.0168980f;
  const float p = pinkState_[0] + pinkState_[1] + pinkState_[2] +
                  pinkState_[3] + pinkState_[4] + pinkState_[5] +
                  pinkState_[6] + w * 0.5362f;
  pinkState_[6] = w * 0.115926f;
  return p * 0.11f;
}

float CymbalSynth::expEnv(float a, float d) const {
  const float t = sampleIndex_ * invSampleRate_;
  const float atk = a > 0.00001f ? (1.0f - expf(-t / a)) : 1.0f;
  const float dec = expf(-t / (d > 0.00001f ? d : 0.00001f));
  return atk * dec;
}

float CymbalSynth::onePoleLow(float x, float c, float &s) const {
  c = clampf(c, 10.0f, sampleRate_ * 0.45f);
  const float a = 1.0f - expf(-2.0f * kPi * c * invSampleRate_);
  s += a * (x - s);
  return s;
}

float CymbalSynth::onePoleHigh(float x, float c, float &s) const {
  const float l = onePoleLow(x, c, s);
  return x - l;
}

float CymbalSynth::process() {
  if (!active_) {
    return 0.0f;
  }

  const float decay = cfg_->decaySec * lerpf(0.45f, 1.25f, velocity_) *
                      lerpf(1.0f, 0.08f, muffle_);
  const float lowEnv = expEnv(cfg_->lowAttackSec, decay);
  const float highEnv = expEnv(cfg_->highAttackSec,
                               cfg_->highDecaySec * lerpf(0.5f, 1.2f, velocity_));
  const float shimmerEnv = highEnv * cfg_->shimmerLevel * velocity_ * (1.0f - muffle_);

  const float maxCutoff = lerpf(6000.0f, 20000.0f, velocity_) * lerpf(1.0f, 0.18f, muffle_);
  const float lowCutoff = 10.0f + maxCutoff * lowEnv;
  const float highCutoff = 10001.0f - 10000.0f * highEnv;

  const float loDriver = onePoleLow(white() * cfg_->noiseLevel, lowCutoff, lpState_);
  const float hiDriver = onePoleHigh(white() * cfg_->noiseLevel, highCutoff, hpLowState_) *
                         (0.25f * shimmerEnv);

  const float t = sampleIndex_ * invSampleRate_;
  const float stickAttack = 0.001f;
  const float thwackEnv = (t < cfg_->thwackSec)
                              ? (t < stickAttack
                                     ? (t / stickAttack)
                                     : expf(-(t - stickAttack) /
                                            (cfg_->thwackSec - stickAttack + 0.000001f)))
                              : 0.0f;
  const float thwack = thwackEnv * cfg_->stickLevel * velocity_;

  float pm = 0.0f;
  for (int i = 0; i < 3; ++i) {
    const float rate = (i == 0 ? 37.0f : (i == 1 ? 71.0f : 113.0f)) *
                       lerpf(0.7f, 1.6f, velocity_);
    pmPhase_[i] += rate * invSampleRate_;
    if (pmPhase_[i] >= 1.0f) {
      pmPhase_[i] -= 1.0f;
    }
    pm += sinf(2.0f * kPi * pmPhase_[i]);
  }

  const float driver = loDriver + hiDriver + thwack;
  const float pmGain = 1.0f + cfg_->phaseModDepth * phaseModAmount_ * pm * 0.20f;
  float res = 0.0f;
  for (uint16_t i = 0u; i < resonatorCount_; ++i) {
    Resonator &r = resonators_[i];
    const float y = r.b0 * driver * pmGain + r.a1 * r.y1 + r.a2 * r.y2;
    r.y2 = r.y1;
    r.y1 = y;
    res += y * r.gain;
  }
  res = (res / (float)resonatorCount_) * cfg_->resonatorLevel;

  // Tutorial mix: resonators plus a small amount of the low-pass driver and
  // stick. Keep direct noise far below the resonator bank to avoid white-noise
  // dominated renders.
  float out = res + loDriver * cfg_->directNoiseLevel + thwack * 0.10f;

  float combOut = 0.0f;
  for (int tap = 0; tap < kCombTaps; ++tap) {
    const uint16_t readIndex = (uint16_t)((combWrite_ + kCombMaxDelay - combDelay_[tap]) &
                                         (kCombMaxDelay - 1));
    combOut += comb_[tap][readIndex];
    comb_[tap][combWrite_] = out + comb_[tap][readIndex] * 0.25f;
  }
  combWrite_ = (uint16_t)((combWrite_ + 1u) & (kCombMaxDelay - 1));
  out += combOut * (0.12f * cfg_->combLevel * combAmount_);

  dcState_ += 0.001f * (out - dcState_);
  out = (out - dcState_) * 0.9f;

  if (++sampleIndex_ > (uint32_t)(decay * sampleRate_ * 8.0f)) {
    active_ = false;
  }
  return clampf(out, -1.0f, 1.0f);
}

void CymbalSynth::process(float *out, uint32_t frames) {
  for (uint32_t i = 0u; i < frames; ++i) {
    out[i] = process();
  }
}

} // namespace realistic_cymbals
