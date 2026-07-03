// Inspired by Dan Stowell's SuperCollider cymbal synthesis tutorial:
// https://mcld.co.uk/cymbalsynthesis/

#include "realistic_cymbals.h"

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

float semitoneRatio(float semitones) {
  return powf(2.0f, semitones / 12.0f);
}


} // namespace

// Explicit modal frequency tunings for each instrument. These are intentionally
// inharmonic clusters rather than harmonic series; the random resonator bank
// below repeats these anchors with small per-voice detuning to keep the
// SuperCollider-style dense modal cloud while making each preset's tuning
// visible and editable.
static const float kCrashFrequenciesHz[] = {
  343.f, 421.f, 512.f, 731.f, 973.f, 1289.f, 1627.f, 2143.f,
  2879.f, 3659.f, 4721.f, 6121.f, 7841.f, 10061.f, 13109.f, 16987.f
};

static const float kRideFrequenciesHz[] = {
  481.f, 603.f, 759.f, 1013.f, 1349.f, 1811.f, 2399.f, 3191.f,
  4211.f, 5603.f, 7421.f, 9829.f, 12113.f, 14591.f, 16879.f, 18193.f
};

// A small (8-12") splash still has a pitched modal body: its lowest plate
// modes sit around 1-3 kHz, with the sizzle above. Starting the anchors at
// ~1.1 kHz (instead of 3.3 kHz) and spreading them inharmonically gives the
// strike a metallic "pssh" identity; anchoring everything above 3 kHz reads
// as band-passed noise.
static const float kSplashFrequenciesHz[] = {
  1123.f, 1409.f, 1801.f, 2251.f, 2749.f, 3407.f, 4211.f, 5233.f,
  6473.f, 7963.f, 9787.f, 12043.f, 14813.f, 17041.f
};

static const float kGongFrequenciesHz[] = {
  162.f, 219.f, 287.f, 361.f, 452.f, 579.f, 733.f, 911.f,
  1139.f, 1451.f, 1847.f, 2381.f, 3097.f, 4181.f, 5741.f, 7919.f
};

static const PresetConfig kPresets[PRESET_COUNT] = {
  // min max freqs count jitter ampA ampD sustain ampR lowA decay hiA hiD thwack stick noise res shimmer direct comb pm modes
  { 300.f, 20000.f, kCrashFrequenciesHz, (uint16_t)(sizeof(kCrashFrequenciesHz) / sizeof(kCrashFrequenciesHz[0])), 1.8f, 0.0015f, 0.18f, 0.35f, 0.28f, 0.018f, 0.44f, 0.040f, 0.30f, 0.002f, 0.50f, 0.045f, 0.74f, 0.30f, 0.020f, 0.10f, 0.10f, 112 },
  { 450.f, 18000.f, kRideFrequenciesHz, (uint16_t)(sizeof(kRideFrequenciesHz) / sizeof(kRideFrequenciesHz[0])), 1.2f, 0.0012f, 0.24f, 0.42f, 0.32f, 0.015f, 0.55f, 0.035f, 0.34f, 0.0015f, 0.40f, 0.040f, 0.66f, 0.34f, 0.016f, 0.07f, 0.08f, 104 },
  { 1000.f, 19000.f, kSplashFrequenciesHz, (uint16_t)(sizeof(kSplashFrequenciesHz) / sizeof(kSplashFrequenciesHz[0])), 0.55f, 0.0010f, 0.30f, 0.50f, 0.30f, 0.006f, 0.70f, 0.018f, 0.25f, 0.0012f, 0.54f, 0.038f, 2.60f, 0.16f, 0.0015f, 0.04f, 0.06f, 72 },
  { 150.f, 14000.f, kGongFrequenciesHz, (uint16_t)(sizeof(kGongFrequenciesHz) / sizeof(kGongFrequenciesHz[0])), 2.4f, 0.0040f, 0.80f, 0.62f, 1.20f, 0.25f, 1.70f, 0.50f, 1.20f, 0.020f, 0.22f, 0.035f, 0.82f, 0.22f, 0.010f, 0.14f, 0.15f, 96 }
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
  // durationSec > 0 fits envelopes to a fixed one-shot render length. When it
  // is not supplied (live/drum-machine use) do NOT cap the tail to decaySec:
  // use a duration long enough that the velocity-scaled natural decay rules,
  // so hard strikes actually ring longer.
  durationSec_ = (p.durationSec > 0.01f) ? p.durationSec : 30.0f;
  // Harder strikes are louder: perceptual-ish curve, soft hits stay audible.
  velocityGain_ = 0.25f + 0.75f * velocity_ * sqrtf(velocity_);
  // Strike envelope for the nonlinear (phase-mod) shimmer: the mode-coupling
  // chaos of a real cymbal scales with vibration energy, so it must be
  // strongest right after the strike and die with the high-frequency energy.
  // Base time constant follows the preset's high-band lifetime; harder
  // strikes sustain the chaotic regime longer.
  strikeDecaySec_ = fmaxf(0.04f, cfg_->highDecaySec * lerpf(0.5f, 1.6f, velocity_));
  // Deterministic per-strike LFO phases (seeded, not free-running) so every
  // strike gets the same attack shimmer character for a given seed.
  for (int i = 0; i < 3; ++i) {
    pmPhase_[i] = frand(rng_);
  }
  initialiseResonators(*cfg_, velocity_, muffle_, durationSec_, rng_);
}

void CymbalSynth::initialiseResonators(const PresetConfig &cfg,
                                        float vel,
                                        float muff,
                                        float durationSec,
                                        uint32_t seed) {
  resonatorCount_ = (cfg.resonators > (uint16_t)kMaxResonators)
                        ? (uint16_t)kMaxResonators
                        : cfg.resonators;

  // SuperCollider Ringz defaults to a one-second 60 dB decay. The tutorial
  // gets most of the frequency-dependent decay from the filtered driver, not
  // from per-mode Q changes, so keep the resonator decay mostly uniform.
  const float requestedRingDecay = lerpf(0.55f, 1.15f, vel) * lerpf(1.0f, 0.18f, muff);
  const float ringDecay = fminf(requestedRingDecay, fmaxf(0.08f, durationSec * 0.42f));
  const float r = expf(-kRingz60Db / (ringDecay * sampleRate_));

  for (uint16_t i = 0u; i < resonatorCount_; ++i) {
    const float anchor = cfg.frequencyHz[i % cfg.frequencyCount];
    const float detune = (frand(seed) * 2.0f - 1.0f) * cfg.frequencyJitterSemitones;
    const float octave = (float)(i / cfg.frequencyCount);
    const float f = clampf(anchor * semitoneRatio(detune) * powf(2.0f, octave * 0.17f),
                            cfg.minHz,
                            cfg.maxHz);
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

  const float t = sampleIndex_ * invSampleRate_;
  const float decay = fminf(cfg_->decaySec, durationSec_ * 0.55f) *
                      lerpf(0.45f, 1.15f, velocity_) *
                      lerpf(1.0f, 0.08f, muffle_);
  const float lowEnv = expEnv(cfg_->lowAttackSec, decay);
  const float highEnv = expEnv(cfg_->highAttackSec,
                              fminf(cfg_->highDecaySec, durationSec_ * 0.40f) * lerpf(0.5f, 1.2f, velocity_));
  const float shimmerEnv = highEnv * cfg_->shimmerLevel * velocity_ * (1.0f - muffle_);

  const float maxCutoff = lerpf(6000.0f, 20000.0f, velocity_) * lerpf(1.0f, 0.18f, muffle_);
  const float lowCutoff = 10.0f + maxCutoff * lowEnv;
  const float highCutoff = 10001.0f - 10000.0f * highEnv;

  // Use the noise as an excitation signal for the resonator bank rather than as
  // an audible layer.  Pink low-frequency energy helps the random driver blend
  // into the ringing modes, while the high driver is tightly envelope-shaped.
  // Harder strikes excite the high plate modes disproportionately, so the
  // drive gets whiter (brighter) with velocity.
  const float whiteBlend = lerpf(0.08f, 0.45f, velocity_ * velocity_);
  const float noise = pink() * (1.0f - whiteBlend) + white() * whiteBlend;
  const float loDriver = onePoleLow(noise * cfg_->noiseLevel, lowCutoff, lpState_) * lowEnv;
  const float hiDriver = onePoleHigh(noise * cfg_->noiseLevel, highCutoff, hpLowState_) *
                         (0.18f * shimmerEnv);

  const float stickAttack = 0.001f;
  const float thwackEnv = (t < cfg_->thwackSec)
                              ? (t < stickAttack
                                     ? (t / stickAttack)
                                     : expf(-(t - stickAttack) /
                                            (cfg_->thwackSec - stickAttack + 0.000001f)))
                              : 0.0f;
  const float thwack = thwackEnv * cfg_->stickLevel * velocity_;

  // Strike envelope: near-instant attack, exponential decay whose time
  // constant was set from velocity + the preset's high-band lifetime at
  // noteOn. This — not the slow driver envelope — gates the phase modulation,
  // so the nonlinear shimmer blooms at the strike and settles into clean
  // linear ringing as the real instrument does.
  const float strikeEnv = velocity_ * expEnv(0.0008f, strikeDecaySec_);

  float pm = 0.0f;
  for (int i = 0; i < 3; ++i) {
    // The chaotic regime right after the strike also runs *faster*: scale the
    // LFO rates by the strike envelope so early shimmer is denser, then the
    // wobble slows as energy drains.
    const float rate = (i == 0 ? 37.0f : (i == 1 ? 71.0f : 113.0f)) *
                       lerpf(0.7f, 1.6f, velocity_) * (1.0f + 0.6f * strikeEnv);
    pmPhase_[i] += rate * invSampleRate_;
    if (pmPhase_[i] >= 1.0f) {
      pmPhase_[i] -= 1.0f;
    }
    pm += sinf(2.0f * kPi * pmPhase_[i]);
  }

  const float pmDepth = cfg_->phaseModDepth * phaseModAmount_ * strikeEnv;
  const float pmGain = 1.0f + pmDepth * pm * 0.22f;
  const float pmExciter = pmDepth * 0.035f * pm;
  const float driver = loDriver + hiDriver + thwack + pmExciter;
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
  float out = res + loDriver * cfg_->directNoiseLevel + thwack * 0.07f;

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

  // Harder strikes are louder.
  out *= velocityGain_;

  // Click-free end when the caller supplied a fixed one-shot duration (in
  // live mode durationSec_ is effectively unbounded and this never engages).
  const float release = fmaxf(0.001f, fminf(cfg_->ampReleaseSec, durationSec_ * 0.45f));
  const float releaseStart = durationSec_ - release;
  if (t >= releaseStart) {
    out *= fmaxf(0.0f, 1.0f - (t - releaseStart) / release);
  }

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

CymbalKit::CymbalKit(float sampleRate) : noteCounter_(0u) {
  setSampleRate(sampleRate);
  for (int v = 0; v < kVoices; ++v) {
    voiceAge_[v] = 0u;
  }
}

void CymbalKit::setSampleRate(float sampleRate) {
  for (int v = 0; v < kVoices; ++v) {
    voices_[v].setSampleRate(sampleRate);
  }
}

void CymbalKit::noteOn(const RenderParams &params, uint32_t seed) {
  // Prefer a silent voice; otherwise steal the least recently started one so
  // the freshest tails keep ringing.
  int chosen = -1;
  for (int v = 0; v < kVoices; ++v) {
    if (!voices_[v].isActive()) {
      chosen = v;
      break;
    }
  }
  if (chosen < 0) {
    uint32_t oldest = voiceAge_[0];
    chosen = 0;
    for (int v = 1; v < kVoices; ++v) {
      if (voiceAge_[v] < oldest) {
        oldest = voiceAge_[v];
        chosen = v;
      }
    }
  }
  voiceAge_[chosen] = ++noteCounter_;
  // Decorrelate simultaneous voices even when the caller reuses one seed.
  voices_[chosen].noteOn(params, seed + 0x9e3779b9u * (uint32_t)chosen);
}

float CymbalKit::process() {
  float sum = 0.0f;
  for (int v = 0; v < kVoices; ++v) {
    if (voices_[v].isActive()) {
      sum += voices_[v].process();
    }
  }
  // Soft headroom: single voice passes ~unchanged, stacked strikes are
  // limited gently instead of hard-clipping.
  return sum / (1.0f + 0.35f * fabsf(sum));
}

void CymbalKit::process(float *out, uint32_t frames) {
  for (uint32_t i = 0u; i < frames; ++i) {
    out[i] = process();
  }
}

bool CymbalKit::isActive() const {
  for (int v = 0; v < kVoices; ++v) {
    if (voices_[v].isActive()) {
      return true;
    }
  }
  return false;
}

uint16_t CymbalKit::activeVoices() const {
  uint16_t n = 0u;
  for (int v = 0; v < kVoices; ++v) {
    if (voices_[v].isActive()) {
      ++n;
    }
  }
  return n;
}

} // namespace realistic_cymbals
