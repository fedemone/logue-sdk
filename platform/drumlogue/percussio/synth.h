#pragma once
/**
 * @file synth.h
 * @brief Percussio -- percussion synthesis after Stephen Dill's CCRMA 220a project.
 *
 * A translation of https://ccrma.stanford.edu/~sdill/220A-project/drums.html
 * (Stephen Dill, Music 220a, CCRMA, 1998) into a drumlogue user synth: the five
 * synthesis techniques the project compares -- subtractive, additive, FM,
 * Karplus-Strong and granular -- as five selectable engines, with each of the
 * project's published sounds as a preset carrying its original parameters.
 *
 * clm.h holds the CLM generators, banks.h the measured spectra, voice.h the
 * engines, presets.h the parameter map and the recipes.  Deviations from the
 * page are marked at the point they occur and collected in README.md.
 */

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "banks.h"
#include "clm.h"
#include "float_math.h"
#include "output_stage.h"
#include "presets.h"
#include "unit.h"
#include "voice.h"

namespace pcs {

/** Four voices: enough for a bell or a 2 s Karplus-Strong tail to ring under
 *  the next hit, which is the whole point of the long-decay presets. */
static constexpr uint32_t kNumVoices = 4;

/**
 * Output stage: trim in dB, then the shared soft knee from
 * common/output_stage.h.  Both were measured with
 * platform/drumlogue/tools/level_meter; README.md carries the full table.
 *
 * The knee starts at 0.95 rather than the shared 0.70 default.  Percussio is
 * mostly sustained tonal material -- bells, chimes, a marimba, plucked strings
 * -- and a memoryless waveshaper on a sustained tone is audible in a way it is
 * not on a drum transient.  Measured against a linear reference, moving the
 * threshold up is a strict improvement at every trim: at +6 dB it takes the
 * mean waveshaping residual over the tonal presets from -38.7 dB to -48.6 dB
 * while also reading 0.2 LU louder, because fewer presets touch the knee at
 * all.  The sharper corner does put more of the residual above 8 kHz, but only
 * from -61 dB to -55 dB relative to the signal, which is inaudible.
 *
 * Both are overridable so another setting can be measured without editing this
 * file:
 *
 *   EXTRA_FLAGS="-DPERCUSSIO_TRIM_DB=9.0f -DPERCUSSIO_KNEE_THR=0.85f" \
 *       ./run.sh ../../percussio
 */
#ifndef PERCUSSIO_TRIM_DB
#define PERCUSSIO_TRIM_DB 6.0f
#endif
#ifndef PERCUSSIO_KNEE_THR
#define PERCUSSIO_KNEE_THR 0.95f
#endif
static constexpr float kOutputTrimDb = PERCUSSIO_TRIM_DB;
static constexpr float kOutputKneeThr = PERCUSSIO_KNEE_THR;

class Percussio {
 public:
  /*=========================================================================*/
  /* Lifecycle                                                               */
  /*=========================================================================*/

  Percussio(void) : out_(kOutputTrimDb, kOutputKneeThr) {}
  ~Percussio(void) {}

  inline int8_t Init(const unit_runtime_desc_t* desc) {
    if (!desc) return k_unit_err_undef;
    if (desc->samplerate != 48000) return k_unit_err_samplerate;
    if (desc->output_channels != 2) return k_unit_err_geometry;

    rng_.seed(0x9E3779B9u);
    for (uint32_t i = 0; i < kNumVoices; ++i) voices_[i].init(0x1234567u + i * 2654435761u);

    renderGrainSources();

    LoadPreset(0);
    return k_unit_err_none;
  }

  inline void Teardown() {}

  inline void Reset() {
    for (uint32_t i = 0; i < kNumVoices; ++i) voices_[i].clear();
  }

  inline void Resume() {}

  inline void Suspend() { Reset(); }

  /*=========================================================================*/
  /* Render                                                                  */
  /*=========================================================================*/

  fast_inline void Render(float* out, size_t frames) {
    float* __restrict p = out;
    const float* e = p + (frames << 1);

    for (; p != e; p += 2) {
      float s = 0.0f;
      for (uint32_t v = 0; v < kNumVoices; ++v) s += voices_[v].process();
      p[0] = s;
      p[1] = s;
    }
    out_.processStereo(out, frames);
  }

  /*=========================================================================*/
  /* Parameters                                                              */
  /*=========================================================================*/

  inline void setParameter(uint8_t index, int32_t value) {
    if (index >= k_num_params) return;
    params_[index] = (int16_t)value;
  }

  inline int32_t getParameterValue(uint8_t index) const {
    return (index < k_num_params) ? params_[index] : 0;
  }

  inline const char* getParameterStrValue(uint8_t index, int32_t value) const {
    switch (index) {
      case k_method:
        if (value >= 0 && value < (int32_t)kNumMethods) return kMethodName[value];
        break;
      case k_model: {
        const uint8_t m = methodIndex();
        if (value >= 0 && value < (int32_t)kNumModels[m]) return kModelName[m][value];
        // Out of range for the current method: show the method's last model
        // rather than a blank, so the knob never reads as empty.
        return kModelName[m][kNumModels[m] - 1];
      }
      case k_curve:
        if (value >= 0 && value < kNumCurves) return kCurveName[value];
        break;
      case k_bank:
        if (value >= 0 && value < (int32_t)banks::kNumBanks) return banks::kBanks[value].name;
        break;
      default:
        break;
    }
    return nullptr;
  }

  inline const uint8_t* getParameterBmpValue(uint8_t index, int32_t value) const {
    (void)index;
    (void)value;
    return nullptr;
  }

  /*=========================================================================*/
  /* Note handling                                                           */
  /*=========================================================================*/

  inline void NoteOn(uint8_t note, uint8_t velocity) {
    last_note_ = note;
    trigger(note, velocity);
  }

  // The engines are one-shot: a CLM instrument runs for its duration and stops,
  // so there is nothing for a note-off to release.  This also makes the unit
  // immune to the drumlogue firing gate_on and gate_off in the same scheduler
  // tick, which strands gated envelopes before they open.
  inline void NoteOff(uint8_t note) { (void)note; }

  inline void GateOn(uint8_t velocity) { trigger(last_note_, velocity); }

  inline void GateOff() {}

  inline void AllNoteOff() { Reset(); }

  inline void PitchBend(uint16_t bend) {
    // 14-bit, centred at 0x2000; +-2 semitones.
    bend_ = exp2f(((float)bend - 8192.0f) * (1.0f / 8192.0f) * (2.0f / 12.0f));
  }

  inline void ChannelPressure(uint8_t pressure) { (void)pressure; }
  inline void Aftertouch(uint8_t note, uint8_t aftertouch) {
    (void)note;
    (void)aftertouch;
  }

  /*=========================================================================*/
  /* Presets                                                                 */
  /*=========================================================================*/

  inline void LoadPreset(uint8_t idx) {
    if (idx >= kNumPresets) return;
    preset_ = idx;
    for (uint8_t i = 0; i < k_num_params; ++i) params_[i] = kPresets[idx].p[i];
  }

  inline uint8_t getPresetIndex() const { return preset_; }

  static inline const char* getPresetName(uint8_t idx) {
    return (idx < kNumPresets) ? kPresets[idx].name : nullptr;
  }

 private:
  /*=========================================================================*/
  /* Internals                                                               */
  /*=========================================================================*/

  uint8_t methodIndex() const {
    const int16_t m = params_[k_method];
    return (uint8_t)((m < 0) ? 0 : ((m >= (int16_t)kNumMethods) ? (kNumMethods - 1) : m));
  }

  /** Resolve the knobs into one hit's settings. */
  void resolveParams(VoiceParams& p, uint8_t note, uint8_t velocity) {
    const uint8_t method = methodIndex();
    const uint8_t nmodels = kNumModels[method];
    int16_t model = params_[k_model];
    if (model < 0) model = 0;
    if (model >= (int16_t)nmodels) model = (int16_t)(nmodels - 1);

    p.method = method;
    p.model = (uint8_t)model;
    p.bank = (uint8_t)clm::clampi(0, params_[k_bank], banks::kNumBanks - 1);

    p.dur = (uint32_t)clm::clampi(1, params_[k_decay], 20000) * 48u;
    p.atk = params_[k_attack] * 0.001f;
    p.hold = params_[k_hold] * 0.001f;
    p.env_base = kCurveBase[clm::clampi(0, params_[k_curve], kNumCurves - 1)];

    p.coef = params_[k_coef] * 0.01f;
    p.radius = params_[k_reso] * 0.0001f;
    p.freq_hz = (float)params_[k_freq];

    // The page passes 0.49 to every randh it makes, and no preset ever wanted
    // anything else, so the rate is fixed here and the knob it used to have is
    // the pitch envelope instead.
    p.noise_rate = kRandhRate;

    // Punch is in semitones at the attack: full scale is two octaves.
    p.punch = params_[k_punch] * 0.024f;

    // Coef is the page's filter coefficient on the two subtractive models that
    // take one, and a tone control on everything else -- the engines that read
    // it and the engines that ignore it are disjoint, so one knob covers both.
    //
    // In the tone role the number is read as a corner rather than as a pole,
    // so that turning it further from zero always filters more: negative low-
    // passes from 18 kHz down to 60 Hz, positive high-passes from 20 Hz up to
    // 8 kHz, both logarithmically, both flat at zero.
    const bool coef_is_the_filter =
        (method == kSubtract) && (model == kOnePole || model == kOneZero);
    const int16_t cf = params_[k_coef];
    p.tone_hp = (cf > 0);
    if (coef_is_the_filter || cf == 0) {
      p.tone_fc = 0.0f;
    } else {
      const float u = (float)((cf < 0) ? -cf : cf) * (1.0f / 99.0f);
      p.tone_fc = p.tone_hp ? (20.0f * powf(400.0f, u)) : (18000.0f * powf(1.0f / 300.0f, u));
    }

    p.fm_ratio = params_[k_ratio] * 0.001f;
    p.index1 = params_[k_index1] * 0.01f;
    p.index2 = params_[k_index2] * 0.01f;
    p.mod_break = params_[k_moddcy] * 0.01f;

    p.part_frac = params_[k_partial] * 0.01f;
    p.noise_amp = params_[k_nseamp] * 0.001f;

    p.blend = params_[k_blend] * 0.0001f;
    p.ks_len = (uint32_t)clm::clampi(2, params_[k_length], (int32_t)kKsMaxLen);
    p.density = (float)params_[k_density];
    p.grain_len = (uint32_t)clm::clampi(1, params_[k_grain], 2000) * 48u;

    float semis = (float)note - 60.0f + (float)params_[k_tune];
    float amp = params_[k_level] * 0.001f * (velocity * (1.0f / 127.0f));

    // Rand: per-hit spread of pitch and level.  The page does this by hand in
    // its multi-note examples -- the 100-pack of firecrackers draws a new
    // centre frequency and amplitude for each one, the wind chimes a new
    // carrier -- and on a sequencer it is what keeps a repeated step from
    // sounding like a loop of one sample.
    const float rnd = params_[k_rand] * 0.01f;
    if (rnd > 0.0f) {
      semis += rng_.nextBi() * rnd * 6.0f;
      amp *= 1.0f - rnd * 0.5f * rng_.next01();
    }

    p.note_ratio = exp2f(semis * (1.0f / 12.0f)) * bend_;
    p.amp = amp;
  }

  void trigger(uint8_t note, uint8_t velocity) {
    if (velocity == 0) return;

    // Free voice, else the one that has been running longest.
    uint32_t pick = 0;
    uint32_t oldest = 0;
    bool found = false;
    for (uint32_t i = 0; i < kNumVoices; ++i) {
      if (!voices_[i].active()) {
        pick = i;
        found = true;
        break;
      }
      if (voices_[i].age() >= oldest) {
        oldest = voices_[i].age();
        pick = i;
      }
    }
    (void)found;

    VoiceParams p;
    resolveParams(p, note, velocity);
    voices_[pick].trigger(p, grain_bell_, grain_cym_);
  }

  /**
   * Pre-render the two grain sources, once, at load.
   *
   * grani works on a sound file; this unit ships none, so the sources are
   * Percussio's own additive renders of the two recordings the page granulates
   * -- the 65536-point tubular bell and the 262144-point Turkish cymbal, both
   * of which are already in banks.h because add-freqs uses them.
   *
   * Rendered twice: once to find the peak, once to store scaled int16.  The
   * banks are pure oscillators with no randomness, so the two passes agree
   * exactly, and this avoids a 192 KB float scratch buffer.
   */
  void renderGrainSources() {
    renderBank(banks::kBankBell65, grain_bell_);
    renderBank(banks::kBankCym26, grain_cym_);
  }

  void renderBank(uint8_t bank_id, int16_t* dst) {
    const banks::Bank& b = banks::kBanks[bank_id];

    // The page's own render of both: (add-freqs 0 <dur> .1 <bank>
    // :amp-env '(0 1 10 1 100 0)) -- full until 10%, then a linear decay.
    const float xs[4] = {0.0f, 0.0f, 0.1f, 1.0f};
    const float ys[4] = {1.0f, 1.0f, 1.0f, 0.0f};

    float peak = 0.0f;
    for (int pass = 0; pass < 2; ++pass) {
      clm::Oscil osc[banks::kMaxPartials];
      float amp[banks::kMaxPartials];
      uint32_t n = 0;
      float sum = 0.0f;
      for (uint8_t i = 0; i < b.n && n < banks::kMaxPartials; ++i) {
        const float f = b.f[i];
        if (f <= 0.0f || f >= 0.45f * kSampleRate) continue;
        osc[n].set(M_TWOPI * f * kInvSampleRate);
        amp[n] = b.a[i];
        if (f >= 20.0f) sum += b.a[i];
        ++n;
      }
      const float norm = (sum > 1.0e-6f) ? (1.0f / sum) : 1.0f;

      clm::Env env;
      env.setPoints(xs, ys, 4, 1.0f);
      env.trigger(kGrainSrcLen);

      float dc_x1 = 0.0f, dc_y1 = 0.0f;
      const float scale = (pass == 0) ? 1.0f : (0.9f * 32767.0f / (peak > 1.0e-9f ? peak : 1.0f));

      for (uint32_t s = 0; s < kGrainSrcLen; ++s) {
        float acc = 0.0f;
        for (uint32_t i = 0; i < n; ++i) acc += amp[i] * osc[i].process();
        acc *= norm * env.process(s);
        const float y = acc - dc_x1 + 0.998691f * dc_y1;
        dc_x1 = acc;
        dc_y1 = y;
        if (pass == 0) {
          const float a = si_fabsf(y);
          if (a > peak) peak = a;
        } else {
          const float v = y * scale;
          dst[s] = (int16_t)clipminmaxf(-32767.0f, v, 32767.0f);
        }
      }
    }
  }

  /*=========================================================================*/
  /* State                                                                   */
  /*=========================================================================*/

  Voice voices_[kNumVoices];
  dl::OutputStage out_;
  clm::Rng rng_;

  int16_t params_[k_num_params] = {0};
  uint8_t preset_ = 0;
  uint8_t last_note_ = 60;
  float bend_ = 1.0f;

  int16_t grain_bell_[kGrainSrcLen] = {0};
  int16_t grain_cym_[kGrainSrcLen] = {0};
};

static_assert(k_num_params == 24, "header.c declares 24 parameters");

}  // namespace pcs

/** The name unit.cc instantiates. */
typedef pcs::Percussio Synth;
