/**
 * test_ripplerx_render.cpp
 *
 * Minimal single-preset renderer for ARM/qemu runs.
 *
 * Intended flow:
 *   1) Build on WSL with arm toolchain.
 *   2) Run via qemu-arm from batch_tune_runner.py using --render-cmd.
 */

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "../common/runtime.h"
uint8_t mock_get_num_sample_banks() { return 1; }
uint8_t mock_get_num_samples_for_bank(uint8_t) { return 1; }
const sample_wrapper_t* mock_get_sample(uint8_t, uint8_t) { return nullptr; }

float ut_exciter_out = 0.0f;
float ut_delay_read  = 0.0f;
float ut_voice_out   = 0.0f;
#include "synth_engine.h"

static bool write_wav(const char* path, const float* samples, int n_samples, int sr) {
    FILE* f = fopen(path, "wb");
    if (!f) return false;

    const int n_bytes = n_samples * 2;
    auto* pcm = new int16_t[n_samples];
    for (int i = 0; i < n_samples; ++i) {
        float v = samples[i];
        if (v >  1.0f) v =  1.0f;
        if (v < -1.0f) v = -1.0f;
        pcm[i] = (int16_t)(v * 32767.0f);
    }

    auto w32 = [&](uint32_t v) { fwrite(&v, 4, 1, f); };
    auto w16 = [&](uint16_t v) { fwrite(&v, 2, 1, f); };
    fwrite("RIFF", 1, 4, f);
    w32(36 + n_bytes);
    fwrite("WAVE", 1, 4, f);
    fwrite("fmt ", 1, 4, f);
    w32(16);
    w16(1);
    w16(1);
    w32((uint32_t)sr);
    w32((uint32_t)(sr * 2));
    w16(2);
    w16(16);
    fwrite("data", 1, 4, f);
    w32((uint32_t)n_bytes);
    fwrite(pcm, 2, n_samples, f);
    fclose(f);

    delete[] pcm;
    return true;
}

static int argi(char** b, char** e, const char* key, int def) {
    for (char** it = b; it != e; ++it) {
        if (strcmp(*it, key) == 0 && (it + 1) != e) return atoi(*(it + 1));
    }
    return def;
}

static float argf(char** b, char** e, const char* key, float def) {
    for (char** it = b; it != e; ++it) {
        if (strcmp(*it, key) == 0 && (it + 1) != e) return (float)atof(*(it + 1));
    }
    return def;
}

static const char* args(char** b, char** e, const char* key, const char* def) {
    for (char** it = b; it != e; ++it) {
        if (strcmp(*it, key) == 0 && (it + 1) != e) return *(it + 1);
    }
    return def;
}

int main(int argc, char** argv) {
    char** b = argv + 1;
    char** e = argv + argc;

    const int preset_idx = argi(b, e, "--preset", 12);
    const int note       = argi(b, e, "--note", 60);
    const float dur_s    = argf(b, e, "--duration", 3.0f);
    const char* out_path = args(b, e, "--out", "rendered_single.wav");
    const char* name     = args(b, e, "--name", "preset");

    RipplerXWaveguide synth;
    unit_runtime_desc_t desc = {};
    desc.samplerate = 48000;
    desc.output_channels = 2;
    desc.get_num_sample_banks = mock_get_num_sample_banks;
    desc.get_num_samples_for_bank = mock_get_num_samples_for_bank;
    desc.get_sample = mock_get_sample;

    if (synth.Init(&desc) != k_unit_err_none) {
        fprintf(stderr, "Init failed\n");
        return 2;
    }

    synth.LoadPreset((uint8_t)preset_idx);

    const int sr = 48000;
    const int total_frames = (int)(dur_s * sr);
    const int block = 128;

    auto* mono = new float[total_frames]();
    auto* stereo = new float[block * 2]();

    synth.NoteOn((uint8_t)note, 100);

    int frame = 0;
    bool released = false;
    while (frame < total_frames) {
        int todo = block;
        if (frame + todo > total_frames) todo = total_frames - frame;

        memset(stereo, 0, todo * 2 * sizeof(float));
        synth.processBlock(stereo, (size_t)todo);

        for (int i = 0; i < todo; ++i)
            mono[frame + i] = stereo[i * 2];

        frame += todo;

        if (!released && frame >= sr / 20) {
            synth.NoteOff((uint8_t)note);
            released = true;
        }
    }

    const bool ok = write_wav(out_path, mono, total_frames, sr);
    delete[] mono;
    delete[] stereo;

    if (!ok) {
        fprintf(stderr, "failed to write %s\n", out_path);
        return 3;
    }

    printf("rendered preset=%d name=%s note=%d dur=%.2fs -> %s\n",
           preset_idx, name, note, dur_s, out_path);
    return 0;
}
