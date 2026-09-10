#pragma once
/**
 * @file voice.h
 * @brief One Percussio voice: the five instrument families from the source page.
 *
 *   kSubtract -- drum-subtract.ins : subtract-op / subtract-pp / subtract-oz
 *   kAdditive -- drum-add.ins      : add-partials / add-freqs / add-noisy-freqs / add-noise
 *   kFm       -- drum-fm.ins       : fm (Chowning, JAES September 1973)
 *   kKarplus  -- drum-ks.ins       : drum-ks (Karplus & Strong, CMJ September 1983)
 *   kGranular -- drum-grani.ins    : grani, over the page's own bell and cymbal
 *
 * A voice is one-shot: it runs for `dur` samples and then frees itself.  That is
 * what the CLM instruments do -- every one of them takes a duration and stops --
 * and it also sidesteps the drumlogue's habit of firing gate_on and gate_off in
 * the same scheduler tick, which can strand a gated envelope before it opens.
 */

#include <cstdint>
#include <cstring>

#include "banks.h"
#include "clm.h"
#include "attributes.h"
#include "float_math.h"

namespace pcs {

static constexpr float kSampleRate = 48000.0f;
static constexpr float kInvSampleRate = 1.0f / kSampleRate;

/** Karplus-Strong wavetable ceiling.  See kKsRateScale in presets.h. */
static constexpr uint32_t kKsMaxLen = 12288;

/** Concurrent grains per voice.  Density 100 with a 500 ms grain would want 50;
 *  the excess simply does not spawn, which thins the texture rather than
 *  stealing an already-sounding grain. */
static constexpr uint32_t kMaxGrains = 8;

/** Grain source length, pre-rendered once in Synth::Init(). */
static constexpr uint32_t kGrainSrcLen = 48000;

/** The rate every randh on the page is held at: 0.49 of the sample rate. */
static constexpr float kRandhRate = 0.49f;

/**
 * CLM's default sample rate in 1998, which is the rate every number on the page
 * was chosen at.
 *
 * A filter coefficient is a normalised-frequency quantity: b1, a1 and r say
 * where a pole or zero sits relative to the sample rate, not relative to the
 * ear.  Ported verbatim to 48 kHz they land 48000/22050 = 2.18x higher, which
 * makes every two-pole 2.18x too broad -- NsyBass2 comes out at Q 0.65 where
 * the page has Q 1.42, so a noisy bass is just noise -- and pushes the one-pole
 * and one-zero emphasis, which sits just under the page's 11 kHz Nyquist, out
 * past 24 kHz where nobody can hear it.  That is what collapses the eleven
 * op/oz sounds into one: measured, their spectral centroids span 2.13x at the
 * page's rate and only 1.58x at 48 kHz.
 *
 * The wavetable lengths already carry this correction (see kKsRateScale).  The
 * subtractive chain gets it by running where it was designed to run, at 22050,
 * and interpolating up; add-noise's resonators sit at measured absolute
 * frequencies instead, so there the rate is translated into the coefficient.
 */
static constexpr float kClmRate = 22050.0f;
static constexpr float kInvClmRate = 1.0f / kClmRate;
static constexpr float kClmRateRatio = kClmRate / kSampleRate;

/** How often the pitch envelope moves coefficients, in samples.  1.33 ms is
 *  well inside the ear's pitch-integration window and keeps the cost of
 *  retuning a 32-partial bank down to something a drumlogue can afford. */
static constexpr uint32_t kPunchHop = 64;

enum Method : uint8_t {
  kSubtract = 0,
  kAdditive,
  kFm,
  kKarplus,
  kGranular,
  kNumMethods
};

enum SubtractModel : uint8_t { kOnePole = 0, kTwoPole, kOneZero, kTwoPoleQ, kNumSubtractModels };
enum AdditiveModel : uint8_t {
  kPure = 0, kNoiseFloor, kNoisyFreqs, kTunedNoise, kTunedNoiseQ, kNumAdditiveModels
};
enum FmModel : uint8_t { kModDecayTail = 0, kModDecayZero, kModRise, kNumFmModels };
enum KsModel : uint8_t { kKsRandom = 0, kKsSine, kKsConst, kNumKsModels };
enum GrainModel : uint8_t { kGrainBell = 0, kGrainBellRev, kGrainCym, kGrainCymRev, kNumGrainModels };

/** One hit's worth of resolved settings, computed off the parameter values. */
struct VoiceParams {
  uint8_t method, model, bank;
  uint32_t dur;       // samples
  float amp;          // level * velocity
  float atk, hold;    // envelope breakpoints, fraction of dur
  float env_base;     // CLM env :base
  float freq_hz;      // engine reference frequency at note 60
  float note_ratio;   // 2^((note - 60 + tune) / 12)
  float punch;        // pitch-envelope depth, semitones at the attack
  float tone_fc;      // tone-control corner in Hz; 0 when the engine spends Coef
  bool tone_hp;       // true high-passes, false low-passes
  // subtractive
  float coef;         // one-pole b1 / one-zero a1
  float radius;       // two-pole / ppolar r
  float noise_rate;   // randh rate, normalised to fs
  // additive
  float part_frac;    // fraction of the bank's partials used
  float noise_amp;
  // fm
  float fm_ratio, index1, index2, mod_break;
  // karplus-strong
  float blend;
  uint32_t ks_len;
  // granular
  float density;      // grains per second
  uint32_t grain_len; // samples
};

class Voice {
 public:
  /*-------------------------------------------------------------------------*/
  /* Lifecycle                                                               */
  /*-------------------------------------------------------------------------*/

  void init(uint32_t seed) {
    rng_.seed(seed);
    noise_.init(rng_.next(), 0.49f);
    active_ = false;
    age_ = 0;
    dc_x1_ = 0.0f;
    dc_y1_ = 0.0f;
    std::memset(ks_buf_, 0, sizeof(ks_buf_));
  }

  void clear() {
    active_ = false;
    dc_x1_ = 0.0f;
    dc_y1_ = 0.0f;
    op_.clear();
    oz_.clear();
    tp_.clear();
    tone_.clear();
    for (uint32_t i = 0; i < n_part_; ++i) res_[i].clear();
    n_part_ = 0;
    n_grains_ = 0;
  }

  bool active() const { return active_; }
  uint32_t age() const { return age_; }

  /*-------------------------------------------------------------------------*/
  /* Trigger                                                                 */
  /*-------------------------------------------------------------------------*/

  void trigger(const VoiceParams& p, const int16_t* grain_bell, const int16_t* grain_cym) {
    p_ = p;
    pos_ = 0;
    age_ = 0;
    active_ = true;
    dc_x1_ = 0.0f;
    dc_y1_ = 0.0f;
    grain_src_ = (p.model == kGrainCym || p.model == kGrainCymRev) ? grain_cym : grain_bell;

    // The last 2 ms are faded out.  CLM simply stops writing samples at the end
    // of the duration; several of the page's envelopes are still at full
    // amplitude there ((0 1 100 1) on drum-ks, (0 1 90 1 100 0) on the cymbals),
    // and on hardware that truncation is an audible click.
    const uint32_t fade = 96;
    fade_start_ = (p_.dur > fade * 2) ? (p_.dur - fade) : (p_.dur >> 1);
    fade_gain_ = 1.0f / (float)(p_.dur - fade_start_);

    buildAmpEnv();

    // The pitch envelope.  Depth is in semitones and it falls exponentially
    // with a time constant of ModDcy of the duration -- the same knob that
    // times the FM modulator envelope, because it is the same idea: how fast
    // the modulation gets out of the way.
    const_q_ = (p_.method == kSubtract && p_.model == kTwoPoleQ) ||
               (p_.method == kAdditive && p_.model == kTunedNoiseQ);
    punch_semis_ = (p_.punch > 0.0f) ? p_.punch : 0.0f;
    punch_env_ = 1.0f;
    punch_ratio_ = 1.0f;
    punch_next_ = kPunchHop;
    if (punch_semis_ > 0.0f) {
      const float tau = clipminmaxf(1.0f, p_.mod_break * (float)p_.dur, 1.0e7f);
      punch_dec_ = expf(-(float)kPunchHop / tau);
      punch_ratio_ = exp2f(punch_semis_ * (1.0f / 12.0f));
    }

    tone_.clear();
    tone_on_ = (p_.tone_fc > 0.0f);
    if (tone_on_) tone_.set(p_.tone_fc, kSampleRate, p_.tone_hp);

    switch (p_.method) {
      case kSubtract: triggerSubtract(); break;
      case kAdditive: triggerAdditive(); break;
      case kFm: triggerFm(); break;
      case kKarplus: triggerKarplus(); break;
      default: triggerGranular(); break;
    }
  }

  /*-------------------------------------------------------------------------*/
  /* Render                                                                  */
  /*-------------------------------------------------------------------------*/

  fast_inline float process() {
    if (!active_) return 0.0f;
    if (pos_ >= p_.dur) {
      active_ = false;
      return 0.0f;
    }

    // The pitch envelope moves filter and oscillator coefficients, which are
    // far too dear to recompute per sample, so it steps every kPunchHop.  A
    // preset with no punch never enters here and pays nothing.
    if (punch_semis_ > 0.0f && pos_ >= punch_next_) {
      punch_env_ *= punch_dec_;
      punch_ratio_ = exp2f(punch_semis_ * punch_env_ * (1.0f / 12.0f));
      retune();
      punch_next_ = pos_ + kPunchHop;
    }

    float s;
    switch (p_.method) {
      case kSubtract: s = renderSubtract(); break;
      case kAdditive: s = renderAdditive(); break;
      case kFm: s = renderFm(); break;
      case kKarplus: s = renderKarplus(); break;
      default: s = renderGranular(); break;
    }

    if (tone_on_) s = tone_.process(s);

    s *= amp_env_.process(pos_) * p_.amp;

    if (pos_ >= fade_start_) s *= 1.0f - (float)(pos_ - fade_start_) * fade_gain_;

    // ~10 Hz DC blocker.  The measured additive tables carry the analysing
    // transform's DC bin as a partial (0.24 Hz at amplitude 3.34 on the
    // 262144-point bell), and the noise engines are asymmetric per hit.
    const float y = s - dc_x1_ + 0.998691f * dc_y1_;
    dc_x1_ = s;
    dc_y1_ = y;

    ++pos_;
    ++age_;
    return y;
  }

 private:
  /*-------------------------------------------------------------------------*/
  /* Envelope                                                                */
  /*-------------------------------------------------------------------------*/

  /**
   * Every amplitude envelope on the page is one of
   *
   *     (0 S)  (A 1)  (H M)  (100 0)
   *
   * with S the start level, A the attack breakpoint, H the point the plateau
   * ends and M the level there.  For the noise, additive and Karplus-Strong
   * instruments S is 0 when there is an attack segment and 1 otherwise, and
   * M is 1 (a plateau).  That covers (0 1 100 0), (0 0 5 1 100 0),
   * (0 0 30 1 50 1 100 0), (0 1 10 1 100 0), (0 1 90 1 100 0) and (0 1 100 1)
   * exactly.
   *
   * The FM carrier envelope is the other family: every fm call on the page ends
   * "50 .2 100 0", so there M is 0.2 and H is the knob.  Its S is 0.8 when there
   * is an attack segment, which is what seven of the page's eight carrier
   * envelopes use.
   */
  void buildAmpEnv() {
    const bool fm = (p_.method == kFm);
    const float mid = fm ? 0.2f : 1.0f;
    float a = clip01f(p_.atk);
    float h = clip01f(p_.hold);
    if (h < a) h = a;
    const float s = (a > 0.0f) ? (fm ? 0.8f : 0.0f) : 1.0f;

    const float xs[4] = {0.0f, a, h, 1.0f};
    const float ys[4] = {s, 1.0f, mid, 0.0f};
    amp_env_.setPoints(xs, ys, 4, p_.env_base);
    amp_env_.trigger(p_.dur);
  }

  /*-------------------------------------------------------------------------*/
  /* Pitch envelope and the constant-Q resonators                            */
  /*-------------------------------------------------------------------------*/

  bool twoPole() const { return p_.model == kTwoPole || p_.model == kTwoPoleQ; }
  bool tunedNoise() const { return p_.model == kTunedNoise || p_.model == kTunedNoiseQ; }

  /** Where the note is right now: the played transposition times the punch. */
  fast_inline float pitch() const { return p_.note_ratio * punch_ratio_; }

  /**
   * `Reso` is a pole radius, which fixes the resonator's bandwidth in hertz --
   * that is what subtract-pp and add-noise are passed and it is what the page
   * means.  The Q models hold the bandwidth proportional to the centre
   * frequency instead, so the resonator keeps its Q wherever the note puts it.
   *
   * A two-pole's bandwidth is BW = -(fs/pi) ln r, so BW proportional to f means
   * ln r proportional to f, i.e. r = r0^(f/f0).  Anchored on the preset's own
   * pitch, which makes it the identity at note 60 with no punch: a Q model and
   * its plain counterpart are the same filter there, and differ only once the
   * sound is moved.
   */
  float radiusNow() const {
    if (p_.radius <= 0.0f || p_.radius >= 1.0f) return p_.radius;
    float e = const_q_ ? clipminmaxf(1.0f / 64.0f, pitch(), 64.0f) : 1.0f;
    // add-noise's ppolar resonators sit at frequencies measured off a recording,
    // so they cannot be moved to the page's sample rate the way the subtractive
    // chain is; the rate goes into the coefficient instead.  r^(22050/48000)
    // realises the page's bandwidth in hertz, and hence its Q, exactly.
    if (p_.method == kAdditive) e *= kClmRateRatio;
    return (e == 1.0f) ? p_.radius : powf(p_.radius, e);
  }

  void setPartial(uint32_t i, float f, float r, bool noisy) {
    const float w = M_TWOPI * clipminmaxf(0.01f, f, 0.45f * kSampleRate) * kInvSampleRate;
    if (noisy)
      res_[i].set(w, r);
    else
      osc_[i].setFreq(w);
    if (p_.model == kNoisyFreqs)
      part_n_[i].setRate(clipminmaxf(0.001f, f * kInvSampleRate, 0.5f));
  }

  /** Move whatever the current engine tunes to the pitch envelope's new value. */
  void retune() {
    switch (p_.method) {
      case kSubtract:
        if (twoPole()) {
          const float f = clipminmaxf(1.0f, p_.freq_hz * pitch(), 0.45f * kClmRate);
          tp_.set(M_TWOPI * f * kInvClmRate, radiusNow());
        } else {
          // One-pole and one-zero have no frequency of their own, so the punch
          // moves the same thing the note moves: the rate randh is held at.
          noise_.setRate(clipminmaxf(0.001f, p_.noise_rate * pitch(), 0.5f));
        }
        break;
      case kAdditive: {
        const float r = radiusNow();
        const bool noisy = tunedNoise();
        for (uint32_t i = 0; i < n_part_; ++i)
          setPartial(i, part_f_[i] * punch_ratio_, r, noisy);
        break;
      }
      case kFm:
        inc_c_ = inc_c_base_ * punch_ratio_;
        inc_m_ = inc_m_base_ * punch_ratio_;
        break;
      default:
        // A Karplus-Strong wavetable is an integer number of samples and the
        // grain reader steps the source by one, so neither bends without
        // resampling.  Both ignore the punch rather than click on it.
        break;
    }
  }

  /*-------------------------------------------------------------------------*/
  /* Subtractive -- drum-subtract.ins                                        */
  /*-------------------------------------------------------------------------*/

  void triggerSubtract() {
    op_.clear();
    oz_.clear();
    tp_.clear();
    op_.set(1.0f, p_.coef);
    oz_.set(1.0f, p_.coef);

    sub_ph_ = 1.0f;  // force a first subchain sample on the first output sample
    sub_prev_ = 0.0f;
    sub_cur_ = 0.0f;

    if (twoPole()) {
      // The note transposes the resonance; the noise rate stays put.
      const float f = clipminmaxf(1.0f, p_.freq_hz * pitch(), 0.45f * kClmRate);
      tp_.set(M_TWOPI * f * kInvClmRate, radiusNow());
      noise_.setRate(clipminmaxf(0.001f, p_.noise_rate, 0.5f));
    } else {
      // One-pole and one-zero have no frequency of their own -- the page's
      // op/oz sounds are unpitched by construction.  The note therefore moves
      // the one thing that does set their spectrum: the rate randh is held at.
      // Slower holds mean a coarser, darker noise.
      noise_.setRate(clipminmaxf(0.001f, p_.noise_rate * pitch(), 0.5f));
    }
  }

  /** One sample of randh -> filter, at the rate the page's coefficients mean. */
  fast_inline float subStep() {
    const float n = noise_.process();
    switch (p_.model) {
      case kOnePole: return op_.process(n);
      case kTwoPole:
      case kTwoPoleQ: return tp_.process(n);
      default: return oz_.process(n);
    }
  }

  /**
   * The whole subtractive chain runs at 22050 Hz and is linearly interpolated
   * up to 48 kHz, which is what makes b1, a1, r and the randh rate mean at the
   * output what they mean on the page.  It costs 0.46 filter evaluations per
   * output sample rather than one, so it is also cheaper than getting it wrong.
   */
  fast_inline float renderSubtract() {
    sub_ph_ += kClmRateRatio;
    while (sub_ph_ >= 1.0f) {
      sub_ph_ -= 1.0f;
      sub_prev_ = sub_cur_;
      sub_cur_ = subStep();
    }
    return sub_prev_ + (sub_cur_ - sub_prev_) * sub_ph_;
  }

  /*-------------------------------------------------------------------------*/
  /* Additive -- drum-add.ins                                                */
  /*-------------------------------------------------------------------------*/

  void triggerAdditive() {
    const banks::Bank& b = banks::kBanks[(p_.bank < banks::kNumBanks) ? p_.bank : 0];

    uint32_t want = (uint32_t)(b.n * clip01f(p_.part_frac) + 0.5f);
    if (want < 1) want = 1;
    if (want > b.n) want = b.n;

    const float nyquist = 0.45f * kSampleRate;
    const float f0 = (b.kind == banks::kRatio) ? (p_.freq_hz * p_.note_ratio) : p_.note_ratio;
    const bool noisy = tunedNoise();
    const float r = radiusNow();

    n_part_ = 0;
    float sum = 0.0f;
    for (uint32_t i = 0; i < want && n_part_ < banks::kMaxPartials; ++i) {
      // Which partials play is decided at the settled pitch, so a partial does
      // not appear and disappear as the pitch envelope falls through Nyquist.
      const float f = b.f[i] * f0;
      if (f >= nyquist || f <= 0.0f) continue;
      const float a = b.a[i];
      part_a_[n_part_] = a;
      part_f_[n_part_] = f;
      const float w = M_TWOPI * clipminmaxf(0.01f, f * punch_ratio_, nyquist) * kInvSampleRate;
      if (noisy) {
        res_[n_part_].clear();
        res_[n_part_].set(w, r);
      } else {
        osc_[n_part_].set(w);
      }
      if (p_.model == kNoisyFreqs) part_n_[n_part_].init(rng_.next(), f * kInvSampleRate);
      // The sub-20 Hz entries are the analysing transform's DC bin, not a
      // partial; they play, but they must not set the bank's level.
      if (f >= 20.0f) sum += a;
      ++n_part_;
    }
    part_norm_ = (sum > 1.0e-6f) ? (1.0f / sum) : 1.0f;
    noise_.setRate(clipminmaxf(0.001f, p_.noise_rate, 0.5f));
  }

  fast_inline float renderAdditive() {
    float acc = 0.0f;
    switch (p_.model) {
      case kTunedNoiseQ:
      case kTunedNoise: {
        // add-noise: ppolar resonators driven by randh instead of oscillators.
        const float n = noise_.process();
        for (uint32_t i = 0; i < n_part_; ++i) acc += part_a_[i] * res_[i].process(n);
        break;
      }
      case kNoisyFreqs:
        // add-noisy-freqs: one randh per partial, at that partial's frequency.
        for (uint32_t i = 0; i < n_part_; ++i)
          acc += part_a_[i] * (osc_[i].process() + p_.noise_amp * part_n_[i].process());
        break;
      case kNoiseFloor:
        for (uint32_t i = 0; i < n_part_; ++i) acc += part_a_[i] * osc_[i].process();
        acc += p_.noise_amp * noise_.process();
        break;
      default:
        for (uint32_t i = 0; i < n_part_; ++i) acc += part_a_[i] * osc_[i].process();
        break;
    }
    return acc * part_norm_;
  }

  /*-------------------------------------------------------------------------*/
  /* FM -- drum-fm.ins                                                       */
  /*-------------------------------------------------------------------------*/

  void triggerFm() {
    ph_c_.clear();
    ph_m_.clear();
    const float fc = clipminmaxf(1.0f, p_.freq_hz * p_.note_ratio, 0.45f * kSampleRate);
    inc_c_base_ = fc * kInvSampleRate;
    inc_m_base_ = fc * p_.fm_ratio * kInvSampleRate;
    inc_c_ = inc_c_base_ * punch_ratio_;
    inc_m_ = inc_m_base_ * punch_ratio_;

    // The modulator envelope: (0 1 MD .2 100 0), (0 1 MD 0 100 0) or
    // (0 0 MD 1 100 0), which are the three shapes the page's fm calls use.
    const float brk = clipminmaxf(0.01f, p_.mod_break, 0.99f);
    const bool rise = (p_.model == kModRise);
    const float xs[3] = {0.0f, brk, 1.0f};
    const float ys[3] = {rise ? 0.0f : 1.0f,
                         rise ? 1.0f : ((p_.model == kModDecayTail) ? 0.2f : 0.0f), 0.0f};
    mod_env_.setPoints(xs, ys, 3, p_.env_base);
    mod_env_.trigger(p_.dur);
  }

  fast_inline float renderFm() {
    // Chowning: the carrier's phase increment is displaced by the modulator,
    // scaled by an index that runs between index1 and index2 under the
    // modulator envelope.  Peak deviation is therefore I * f_mod, in Hz.
    const float idx = p_.index1 + (p_.index2 - p_.index1) * mod_env_.process(pos_);
    const float m = clm::Phasor::sine(ph_m_.advance(inc_m_));
    return clm::Phasor::sine(ph_c_.advance(inc_c_ + idx * inc_m_ * m));
  }

  /*-------------------------------------------------------------------------*/
  /* Karplus-Strong -- drum-ks.ins                                           */
  /*-------------------------------------------------------------------------*/

  void triggerKarplus() {
    uint32_t p = (uint32_t)(p_.ks_len / clipminmaxf(0.03f, p_.note_ratio, 32.0f) + 0.5f);
    if (p < 2) p = 2;
    if (p > kKsMaxLen) p = kKsMaxLen;
    ks_p_ = p;
    ks_i_ = 0;
    ks_last_ = 0.0f;

    switch (p_.model) {
      case kKsSine:
        for (uint32_t i = 0; i < p; ++i)
          ks_buf_[i] = clm::Phasor::sine((float)i / (float)p);
        break;
      case kKsConst:
        for (uint32_t i = 0; i < p; ++i) ks_buf_[i] = 1.0f;
        break;
      default:
        for (uint32_t i = 0; i < p; ++i) ks_buf_[i] = rng_.nextBi();
        break;
    }
    // b is compared against a raw uniform draw, so scale it into the RNG's
    // range once rather than converting the draw to a float every sample.
    // The scale is 2^32 - 256 rather than 2^32 because the latter does not fit
    // in the uint32 the cast lands in; b = 1 then inverts all but 256 draws in
    // 4 billion, which is the same sound.
    ks_thresh_ = (uint32_t)(clip01f(p_.blend) * 4294967040.0f);
  }

  fast_inline float renderKarplus() {
    //     X(t) = +1/2 [X(t-p) + X(t-p-1)]   with probability b
    //            -1/2 [X(t-p) + X(t-p-1)]   with probability 1-b
    //
    // DEVIATION (sign): the page prints the recurrence with the PLUS branch
    // taken with probability b, but its prose and every one of its presets read
    // the other way -- "b near 0 simply averages the samples, and produces
    // string-like sounds", with b = 0 named Plucked String, b = 1 named Cymbal
    // and Metallic Plink, b = 1/2 the snare.  Plain averaging is the plucked
    // string and a fixed inversion is the hollow metallic one, so b here is the
    // probability of INVERTING, which is what makes the page's preset values
    // produce the sounds the page names.
    const float d = ks_buf_[ks_i_];
    float y = 0.5f * (d + ks_last_);
    if (rng_.next() < ks_thresh_) y = -y;
    if (si_fabsf(y) < clm::kDenormal) y = 0.0f;  // see clm::kDenormal
    ks_buf_[ks_i_] = y;
    ks_last_ = d;
    if (++ks_i_ >= ks_p_) ks_i_ = 0;
    return y;
  }

  /*-------------------------------------------------------------------------*/
  /* Granular -- drum-grani.ins                                              */
  /*-------------------------------------------------------------------------*/

  void triggerGranular() {
    n_grains_ = 0;
    grain_next_ = 0;
    grain_read_ = (p_.model == kGrainBellRev || p_.model == kGrainCymRev)
                      ? (float)(kGrainSrcLen - 1)
                      : 0.0f;
    grain_rev_ = (p_.model == kGrainBellRev || p_.model == kGrainCymRev);
    grain_period_ = (uint32_t)(kSampleRate / clipminmaxf(1.0f, p_.density, 100.0f) + 0.5f);
    if (grain_period_ < 16) grain_period_ = 16;
  }

  fast_inline float renderGranular() {
    if (!grain_src_) return 0.0f;
    if (pos_ >= grain_next_ && n_grains_ < kMaxGrains) {
      Grain& g = grains_[n_grains_++];
      g.start = grain_read_;
      g.pos = 0;
      g.len = p_.grain_len;
      grain_next_ = pos_ + grain_period_;
    }

    // The source is read through at unit speed, so successive grains walk the
    // bell or cymbal the way grani walks a sound file.
    grain_read_ += grain_rev_ ? -1.0f : 1.0f;
    if (grain_read_ >= (float)kGrainSrcLen) grain_read_ -= (float)kGrainSrcLen;
    if (grain_read_ < 0.0f) grain_read_ += (float)kGrainSrcLen;

    float acc = 0.0f;
    uint32_t w = 0;
    for (uint32_t i = 0; i < n_grains_; ++i) {
      Grain& g = grains_[i];
      if (g.pos >= g.len) continue;
      float idx = g.start + (grain_rev_ ? -(float)g.pos : (float)g.pos);
      if (idx >= (float)kGrainSrcLen) idx -= (float)kGrainSrcLen;
      if (idx < 0.0f) idx += (float)kGrainSrcLen;
      // Grain envelope '(0 1 100 0): full at onset, linear ramp to zero.  That
      // is the shape three of the page's four grani calls use, and the onset
      // step is exactly what gives "Fixed Broken Cymbal" its grain.
      const float env = 1.0f - (float)g.pos / (float)g.len;
      acc += grain_src_[(uint32_t)idx] * (1.0f / 32768.0f) * env;
      ++g.pos;
      if (g.pos < g.len) grains_[w++] = g;
    }
    n_grains_ = w;
    return acc;
  }

  /*-------------------------------------------------------------------------*/
  /* State                                                                   */
  /*-------------------------------------------------------------------------*/

  struct Grain {
    float start;
    uint32_t pos, len;
  };

  VoiceParams p_;
  clm::Env amp_env_, mod_env_;
  clm::Rng rng_;
  clm::Randh noise_;

  uint32_t pos_ = 0, age_ = 0;
  bool active_ = false;
  uint32_t fade_start_ = 0;
  float fade_gain_ = 0.0f;
  float dc_x1_ = 0.0f, dc_y1_ = 0.0f;

  clm::OnePole op_;
  clm::OneZero oz_;
  clm::TwoPole tp_;
  clm::Tone tone_;
  bool tone_on_ = false;

  float sub_ph_ = 1.0f, sub_prev_ = 0.0f, sub_cur_ = 0.0f;
  bool const_q_ = false;
  float punch_semis_ = 0.0f, punch_env_ = 1.0f, punch_dec_ = 0.0f, punch_ratio_ = 1.0f;
  uint32_t punch_next_ = 0;

  uint32_t n_part_ = 0;
  float part_norm_ = 1.0f;
  float part_a_[banks::kMaxPartials];
  float part_f_[banks::kMaxPartials];
  clm::Oscil osc_[banks::kMaxPartials];
  clm::TwoPole res_[banks::kMaxPartials];
  clm::Randh part_n_[banks::kMaxPartials];

  clm::Phasor ph_c_, ph_m_;
  float inc_c_ = 0.0f, inc_m_ = 0.0f;
  float inc_c_base_ = 0.0f, inc_m_base_ = 0.0f;

  float ks_buf_[kKsMaxLen];
  uint32_t ks_p_ = 2, ks_i_ = 0, ks_thresh_ = 0;
  float ks_last_ = 0.0f;

  const int16_t* grain_src_ = nullptr;
  Grain grains_[kMaxGrains];
  uint32_t n_grains_ = 0, grain_next_ = 0, grain_period_ = 2400;
  float grain_read_ = 0.0f;
  bool grain_rev_ = false;
};

}  // namespace pcs
