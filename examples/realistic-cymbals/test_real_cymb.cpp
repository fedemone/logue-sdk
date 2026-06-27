#include "realistic_cymbals.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>

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

int renderPreset(const char *path,
                 realistic_cymbals::Preset preset,
                 float seconds,
                 float velocity,
                 float muffle,
                 float comb,
                 float phaseMod,
                 uint32_t seed) {
  FILE *file = fopen(path, "wb");
  if (!file) {
    fprintf(stderr, "Could not open %s for writing\n", path);
    return 1;
  }

  const uint32_t frames = (uint32_t)(seconds * (float)kSampleRate);
  writeWavHeader(file, frames);

  realistic_cymbals::CymbalSynth cymbal((float)kSampleRate);
  realistic_cymbals::RenderParams params;
  params.preset = preset;
  params.velocity = velocity;
  params.muffle = muffle;
  params.comb = comb;
  params.phaseMod = phaseMod;
  cymbal.noteOn(params, seed);

  uint32_t nonZeroSamples = 0u;
  int peak = 0;
  double sumSquares = 0.0;

  for (uint32_t i = 0u; i < frames; ++i) {
    const int16_t pcm = floatToPcm16(cymbal.process());
    const int magnitude = (pcm < 0) ? -(int)pcm : (int)pcm;
    if (magnitude > peak) {
      peak = magnitude;
    }
    if (pcm != 0) {
      ++nonZeroSamples;
    }
    sumSquares += (double)pcm * (double)pcm;
    writeU16LE(file, (uint16_t)pcm);
  }

  if (fclose(file) != 0) {
    fprintf(stderr, "Could not close %s cleanly\n", path);
    return 1;
  }

  printf("Wrote %s (%u frames, peak %d, rms %.1f, non-zero %u)\n",
         path,
         frames,
         peak,
         sqrt(sumSquares / (double)frames),
         nonZeroSamples);
  return 0;
}

} // namespace

int main() {
  struct RenderJob {
    const char *path;
    realistic_cymbals::Preset preset;
    float seconds;
    float velocity;
    float muffle;
    float comb;
    float phaseMod;
    uint32_t seed;
  };

  const RenderJob jobs[] = {
    { "real_cymb_crash.wav", realistic_cymbals::PRESET_CRASH, 8.0f, 0.95f, 0.00f, 0.18f, 0.12f, 0x43524153u },
    { "real_cymb_ride.wav", realistic_cymbals::PRESET_RIDE, 7.0f, 0.75f, 0.10f, 0.12f, 0.08f, 0x52494445u },
    { "real_cymb_splash.wav", realistic_cymbals::PRESET_SPLASH, 3.0f, 0.90f, 0.00f, 0.10f, 0.06f, 0x53504c41u },
    { "real_cymb_gong.wav", realistic_cymbals::PRESET_GONG, 16.0f, 0.85f, 0.00f, 0.25f, 0.18f, 0x474f4e47u }
  };

  int result = 0;
  for (uint32_t i = 0u; i < (uint32_t)(sizeof(jobs) / sizeof(jobs[0])); ++i) {
    result |= renderPreset(jobs[i].path,
                           jobs[i].preset,
                           jobs[i].seconds,
                           jobs[i].velocity,
                           jobs[i].muffle,
                           jobs[i].comb,
                           jobs[i].phaseMod,
                           jobs[i].seed);
  }

  return result;
}
