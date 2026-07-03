#include "realistic_cymbals.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

namespace {

const uint32_t kSampleRate = 48000u;
const uint16_t kChannels = 1u;
const uint16_t kBitsPerSample = 16u;

void writeU16LE(FILE *file, uint16_t value) {
  fputc((int)(value & 0xffu), file);
  fputc((int)((value >> 8) & 0xffu), file);
}

void writeU32LE(FILE *file, uint32_t value) {
  fputc((int)(value & 0xffu), file);
  fputc((int)((value >> 8) & 0xffu), file);
  fputc((int)((value >> 16) & 0xffu), file);
  fputc((int)((value >> 24) & 0xffu), file);
}

void writeWavHeader(FILE *file, uint32_t sampleFrames) {
  const uint16_t blockAlign = (uint16_t)(kChannels * (kBitsPerSample / 8u));
  const uint32_t byteRate = kSampleRate * blockAlign;
  const uint32_t dataBytes = sampleFrames * blockAlign;

  fwrite("RIFF", 1u, 4u, file);
  writeU32LE(file, 36u + dataBytes);
  fwrite("WAVE", 1u, 4u, file);
  fwrite("fmt ", 1u, 4u, file);
  writeU32LE(file, 16u);
  writeU16LE(file, 1u); // PCM
  writeU16LE(file, kChannels);
  writeU32LE(file, kSampleRate);
  writeU32LE(file, byteRate);
  writeU16LE(file, blockAlign);
  writeU16LE(file, kBitsPerSample);
  fwrite("data", 1u, 4u, file);
  writeU32LE(file, dataBytes);
}

int16_t floatToPcm16(float sample) {
  if (sample > 1.0f) {
    sample = 1.0f;
  } else if (sample < -1.0f) {
    sample = -1.0f;
  }

  return (int16_t)lrintf(sample * 32767.0f);
}

// Cheap behaviour metrics computed while streaming the render:
//  - peak / rms: loudness.
//  - tailRms: rms of the final third — "does the tail still ring?".
//  - t60Sec: first time the 10 ms rms envelope drops 60 dB below its peak.
//  - brightness: first-difference energy over signal energy in the first
//    300 ms — a spectral-tilt proxy (higher = more high-frequency content).
struct Stats {
  float peak;
  float rms;
  float tailRms;
  float t60Sec;
  float brightness;
};

class StatsAccum {
public:
  explicit StatsAccum(uint32_t totalFrames)
      : total_(totalFrames), i_(0u), peak_(0.0f), sumSq_(0.0), tailSumSq_(0.0),
        tailCount_(0u), diffSumSq_(0.0), earlySumSq_(0.0), prev_(0.0f),
        blockSumSq_(0.0), blockN_(0u), envPeakDb_(-160.0f), t60_(-1.0f) {}

  void push(float x) {
    const float a = fabsf(x);
    if (a > peak_) {
      peak_ = a;
    }
    sumSq_ += (double)x * (double)x;
    if (i_ >= total_ - total_ / 3u) {
      tailSumSq_ += (double)x * (double)x;
      ++tailCount_;
    }
    if (i_ < (uint32_t)(0.3f * kSampleRate)) {
      const float d = x - prev_;
      diffSumSq_ += (double)d * (double)d;
      earlySumSq_ += (double)x * (double)x;
      prev_ = x;
    }
    blockSumSq_ += (double)x * (double)x;
    if (++blockN_ >= kSampleRate / 100u) { // 10 ms blocks
      const double blockRms = sqrt(blockSumSq_ / (double)blockN_) + 1e-12;
      const float db = (float)(20.0 * log10(blockRms));
      if (db > envPeakDb_) {
        envPeakDb_ = db;
      } else if (t60_ < 0.0f && db < envPeakDb_ - 60.0f) {
        t60_ = (float)i_ / (float)kSampleRate;
      }
      blockSumSq_ = 0.0;
      blockN_ = 0u;
    }
    ++i_;
  }

  Stats finish() const {
    Stats s;
    s.peak = peak_;
    s.rms = (float)sqrt(sumSq_ / (double)(i_ ? i_ : 1u));
    s.tailRms = (float)sqrt(tailSumSq_ / (double)(tailCount_ ? tailCount_ : 1u));
    s.t60Sec = t60_;
    s.brightness = (float)(diffSumSq_ / (earlySumSq_ + 1e-12));
    return s;
  }

private:
  uint32_t total_, i_;
  float peak_;
  double sumSq_, tailSumSq_;
  uint32_t tailCount_;
  double diffSumSq_, earlySumSq_;
  float prev_;
  double blockSumSq_;
  uint32_t blockN_;
  float envPeakDb_, t60_;
};

void printStats(const char *path, const Stats &s) {
  printf("%-34s peak %.3f rms %.4f tailRms %.5f t60 %s bright %.4f\n",
         path, s.peak, s.rms, s.tailRms,
         s.t60Sec >= 0.0f ? "" : "n/a", s.brightness);
  if (s.t60Sec >= 0.0f) {
    printf("%-34s   (t60 = %.2f s)\n", "", s.t60Sec);
  }
}

// Renders one strike. durationFitSec > 0 fits the synth envelopes to that
// one-shot length; 0 lets the natural velocity-scaled decay run free (the WAV
// is still renderSeconds long).
int renderStrike(const char *path,
                 realistic_cymbals::Preset preset,
                 float renderSeconds,
                 float durationFitSec,
                 float velocity,
                 float muffle,
                 float comb,
                 float phaseMod,
                 uint32_t seed,
                 Stats *statsOut) {
  FILE *file = fopen(path, "wb");
  if (!file) {
    fprintf(stderr, "Could not open %s for writing\n", path);
    return 1;
  }

  const uint32_t frames = (uint32_t)(renderSeconds * (float)kSampleRate + 0.5f);
  writeWavHeader(file, frames);

  realistic_cymbals::CymbalSynth cymbal((float)kSampleRate);
  realistic_cymbals::RenderParams params;
  params.preset = preset;
  params.velocity = velocity;
  params.muffle = muffle;
  params.comb = comb;
  params.phaseMod = phaseMod;
  params.durationSec = durationFitSec;
  cymbal.noteOn(params, seed);

  StatsAccum acc(frames);
  for (uint32_t i = 0u; i < frames; ++i) {
    const float x = cymbal.process();
    acc.push(x);
    writeU16LE(file, (uint16_t)floatToPcm16(x));
  }

  if (fclose(file) != 0) {
    fprintf(stderr, "Could not close %s cleanly\n", path);
    return 1;
  }

  const Stats s = acc.finish();
  printStats(path, s);
  if (statsOut) {
    *statsOut = s;
  }
  return 0;
}

struct KitHit {
  float timeSec;
  realistic_cymbals::Preset preset;
  float velocity;
  float muffle;
  float comb;
  float phaseMod;
};

// Renders a sequence of overlapping strikes through the polyphonic kit and
// reports the maximum simultaneous voice count observed.
int renderKit(const char *path,
              const KitHit *hits,
              uint32_t hitCount,
              float renderSeconds,
              uint32_t seed,
              Stats *statsOut,
              uint16_t *maxVoicesOut) {
  FILE *file = fopen(path, "wb");
  if (!file) {
    fprintf(stderr, "Could not open %s for writing\n", path);
    return 1;
  }

  const uint32_t frames = (uint32_t)(renderSeconds * (float)kSampleRate + 0.5f);
  writeWavHeader(file, frames);

  realistic_cymbals::CymbalKit kit((float)kSampleRate);
  StatsAccum acc(frames);
  uint32_t nextHit = 0u;
  uint16_t maxVoices = 0u;

  for (uint32_t i = 0u; i < frames; ++i) {
    const float t = (float)i / (float)kSampleRate;
    while (nextHit < hitCount && t >= hits[nextHit].timeSec) {
      realistic_cymbals::RenderParams params;
      params.preset = hits[nextHit].preset;
      params.velocity = hits[nextHit].velocity;
      params.muffle = hits[nextHit].muffle;
      params.comb = hits[nextHit].comb;
      params.phaseMod = hits[nextHit].phaseMod;
      params.durationSec = 0.0f; // natural decay: tails must overlap
      kit.noteOn(params, seed + nextHit * 0x1234567u);
      ++nextHit;
    }
    const uint16_t nv = kit.activeVoices();
    if (nv > maxVoices) {
      maxVoices = nv;
    }
    const float x = kit.process();
    acc.push(x);
    writeU16LE(file, (uint16_t)floatToPcm16(x));
  }

  if (fclose(file) != 0) {
    fprintf(stderr, "Could not close %s cleanly\n", path);
    return 1;
  }

  const Stats s = acc.finish();
  printStats(path, s);
  printf("%-34s   (max simultaneous voices: %u)\n", "", maxVoices);
  if (statsOut) {
    *statsOut = s;
  }
  if (maxVoicesOut) {
    *maxVoicesOut = maxVoices;
  }
  return 0;
}

int gFailures = 0;

void check(bool ok, const char *what) {
  printf("[%s] %s\n", ok ? "PASS" : "FAIL", what);
  if (!ok) {
    ++gFailures;
  }
}

} // namespace

int main() {
  int result = 0;

  // --- One-shot preset demos (fixed-duration fit, as before) ---------------
  printf("== preset demos ==\n");
  result |= renderStrike("real_cymb_crash.wav", realistic_cymbals::PRESET_CRASH,
                         3.0f, 3.0f, 0.95f, 0.00f, 0.18f, 0.12f, 0x43524153u, 0);
  result |= renderStrike("real_cymb_ride.wav", realistic_cymbals::PRESET_RIDE,
                         3.0f, 3.0f, 0.75f, 0.10f, 0.12f, 0.08f, 0x52494445u, 0);
  result |= renderStrike("real_cymb_splash.wav", realistic_cymbals::PRESET_SPLASH,
                         1.5f, 1.5f, 0.90f, 0.00f, 0.10f, 0.06f, 0x53504c41u, 0);
  result |= renderStrike("real_cymb_gong.wav", realistic_cymbals::PRESET_GONG,
                         5.0f, 5.0f, 0.85f, 0.00f, 0.25f, 0.18f, 0x474f4e47u, 0);

  // --- Velocity behaviour: harder crash => louder, brighter, longer --------
  printf("\n== velocity sweep (crash, natural decay) ==\n");
  Stats soft, med, hard;
  result |= renderStrike("real_cymb_crash_v30.wav", realistic_cymbals::PRESET_CRASH,
                         4.0f, 0.0f, 0.30f, 0.0f, 0.18f, 0.12f, 0x43524153u, &soft);
  result |= renderStrike("real_cymb_crash_v60.wav", realistic_cymbals::PRESET_CRASH,
                         4.0f, 0.0f, 0.60f, 0.0f, 0.18f, 0.12f, 0x43524153u, &med);
  result |= renderStrike("real_cymb_crash_v95.wav", realistic_cymbals::PRESET_CRASH,
                         4.0f, 0.0f, 0.95f, 0.0f, 0.18f, 0.12f, 0x43524153u, &hard);

  check(hard.peak > med.peak && med.peak > soft.peak,
        "harder strike is louder (peak v95 > v60 > v30)");
  check(hard.brightness > soft.brightness,
        "harder strike is brighter (spectral tilt v95 > v30)");
  check(hard.t60Sec < 0.0f || soft.t60Sec < 0.0f || hard.t60Sec > soft.t60Sec,
        "harder strike rings longer (t60 v95 > v30)");
  check(hard.tailRms > soft.tailRms,
        "harder strike has more tail energy (tailRms v95 > v30)");

  // --- Muffle behaviour ------------------------------------------------------
  printf("\n== muffle (crash, natural decay) ==\n");
  Stats open, choked;
  result |= renderStrike("real_cymb_crash_open.wav", realistic_cymbals::PRESET_CRASH,
                         3.0f, 0.0f, 0.85f, 0.0f, 0.18f, 0.12f, 0x43524153u, &open);
  result |= renderStrike("real_cymb_crash_choke.wav", realistic_cymbals::PRESET_CRASH,
                         3.0f, 0.0f, 0.85f, 0.9f, 0.18f, 0.12f, 0x43524153u, &choked);
  check(choked.tailRms < open.tailRms * 0.25f,
        "muffled strike is choked (tailRms < 25% of open)");

  // --- Polyphony: stacked hits must overlap, not cut ------------------------
  printf("\n== polyphony: crash roll + mixed stack ==\n");
  const KitHit roll[] = {
    { 0.00f, realistic_cymbals::PRESET_CRASH, 0.95f, 0.0f, 0.18f, 0.12f },
    { 0.35f, realistic_cymbals::PRESET_CRASH, 0.70f, 0.0f, 0.18f, 0.12f },
    { 0.70f, realistic_cymbals::PRESET_CRASH, 0.85f, 0.0f, 0.18f, 0.12f },
    { 1.05f, realistic_cymbals::PRESET_CRASH, 0.60f, 0.0f, 0.18f, 0.12f },
    { 1.40f, realistic_cymbals::PRESET_CRASH, 0.90f, 0.0f, 0.18f, 0.12f },
    { 1.75f, realistic_cymbals::PRESET_CRASH, 0.95f, 0.0f, 0.18f, 0.12f },
  };
  Stats rollStats;
  uint16_t rollVoices = 0u;
  result |= renderKit("real_cymb_roll.wav", roll,
                      (uint32_t)(sizeof(roll) / sizeof(roll[0])), 5.0f,
                      0x524f4c4cu, &rollStats, &rollVoices);
  check(rollVoices >= 3u, "crash roll stacks voices (>= 3 simultaneous)");
  check(rollStats.peak <= 1.0f, "stacked output stays within +/-1.0");

  const KitHit stack[] = {
    { 0.00f, realistic_cymbals::PRESET_GONG,   0.85f, 0.0f, 0.25f, 0.18f },
    { 0.80f, realistic_cymbals::PRESET_CRASH,  0.95f, 0.0f, 0.18f, 0.12f },
    { 1.60f, realistic_cymbals::PRESET_RIDE,   0.75f, 0.0f, 0.12f, 0.08f },
    { 2.10f, realistic_cymbals::PRESET_SPLASH, 0.90f, 0.0f, 0.10f, 0.06f },
  };
  Stats stackStats;
  uint16_t stackVoices = 0u;
  result |= renderKit("real_cymb_stack.wav", stack,
                      (uint32_t)(sizeof(stack) / sizeof(stack[0])), 6.0f,
                      0x5354434bu, &stackStats, &stackVoices);
  check(stackVoices >= 3u, "mixed stack keeps earlier tails alive (>= 3 voices)");

  printf("\n%d check(s) failed\n", gFailures);
  return result | (gFailures ? 2 : 0);
}
