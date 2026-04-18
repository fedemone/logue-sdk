/**
 * render_presets.cpp — Render every preset to a WAV file for acoustic analysis.
 *
 * Build (from the ripplerx directory):
 *   g++ -std=c++17 -O2 -I. -Itest_stubs -I.. -I../../common -I../common \
 *       -DRUNTIME_COMMON_H_ render_presets.cpp -o render_presets
 * Run:
 *   ./render_presets [output_dir]   # default: rendered/
 *
 * Each WAV is 48000 Hz mono float32, named <idx>_<PresetName>.wav.
 * The analyze_samples.py engine can then compare rendered output against
 * reference samples for a complete closed-loop validation.
 */

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <string>
#include <sys/stat.h>

// ── Drumlogue OS mock (same as test_dsp.cpp) ─────────────────────────────────
#include "../common/runtime.h"
uint8_t mock_get_num_sample_banks() { return 1; }
uint8_t mock_get_num_samples_for_bank(uint8_t) { return 1; }
const sample_wrapper_t* mock_get_sample(uint8_t, uint8_t) { return nullptr; }

// ── Bring in the synth ────────────────────────────────────────────────────────
float ut_exciter_out = 0.0f;
float ut_delay_read  = 0.0f;
float ut_voice_out   = 0.0f;
#include "synth_engine.h"

// ── Minimal WAV writer (no external libraries) ────────────────────────────────
static bool write_wav(const char* path, const float* samples, int n_samples, int sr) {
    FILE* f = fopen(path, "wb");
    if (!f) return false;

    // Convert float → 16-bit PCM (clamp to [-1, 1])
    const int n_bytes = n_samples * 2;
    auto* pcm = new int16_t[n_samples];
    for (int i = 0; i < n_samples; ++i) {
        float v = samples[i];
        if (v >  1.0f) v =  1.0f;
        if (v < -1.0f) v = -1.0f;
        pcm[i] = (int16_t)(v * 32767.0f);
    }

    // RIFF header
    auto w32 = [&](uint32_t v) { fwrite(&v, 4, 1, f); };
    auto w16 = [&](uint16_t v) { fwrite(&v, 2, 1, f); };
    fwrite("RIFF", 1, 4, f);
    w32(36 + n_bytes);          // ChunkSize
    fwrite("WAVE", 1, 4, f);
    fwrite("fmt ", 1, 4, f);
    w32(16);                    // Subchunk1Size (PCM)
    w16(1);                     // AudioFormat = PCM
    w16(1);                     // NumChannels = 1 (mono)
    w32((uint32_t)sr);          // SampleRate
    w32((uint32_t)(sr * 2));    // ByteRate
    w16(2);                     // BlockAlign
    w16(16);                    // BitsPerSample
    fwrite("data", 1, 4, f);
    w32((uint32_t)n_bytes);
    fwrite(pcm, 2, n_samples, f);
    fclose(f);
    delete[] pcm;
    return true;
}

// ── Render one preset ─────────────────────────────────────────────────────────
static void render_preset(int preset_idx, uint8_t note, float duration_s,
                          const char* out_path, const char* preset_name) {
    RipplerXWaveguide synth;
    unit_runtime_desc_t desc = {};
    desc.samplerate = 48000;
    desc.output_channels = 2;
    desc.get_num_sample_banks        = mock_get_num_sample_banks;
    desc.get_num_samples_for_bank    = mock_get_num_samples_for_bank;
    desc.get_sample                  = mock_get_sample;
    synth.Init(&desc);
    synth.LoadPreset(preset_idx);

    const int sr = 48000;
    const int total_frames = (int)(duration_s * sr);
    const int block = 128;

    // Collect mono output
    auto* mono = new float[total_frames]();
    auto* stereo = new float[block * 2]();

    // Strike at velocity 100 (≈ 79%), hold for 50 ms, then release
    synth.NoteOn(note, 100);

    int frame = 0;
    bool released = false;

    while (frame < total_frames) {
        int todo = block;
        if (frame + todo > total_frames) todo = total_frames - frame;

        memset(stereo, 0, todo * 2 * sizeof(float));
        synth.processBlock(stereo, (size_t)todo);

        for (int i = 0; i < todo; ++i)
            mono[frame + i] = stereo[i * 2];   // left channel → mono

        frame += todo;

        // Release at 50 ms
        if (!released && frame >= sr / 20) {
            synth.NoteOff(note);
            released = true;
        }
    }

    if (write_wav(out_path, mono, total_frames, sr)) {
        printf("  [%2d] %-12s  note=%-3d  %.1fs  → %s\n",
               preset_idx, preset_name, (int)note, duration_s, out_path);
    } else {
        fprintf(stderr, "  ERROR: could not write %s\n", out_path);
    }

    delete[] mono;
    delete[] stereo;
}

// ── Entry point ───────────────────────────────────────────────────────────────
int main(int argc, char** argv) {
    const char* out_dir = (argc > 1) ? argv[1] : "rendered";

    // Create output directory
    mkdir(out_dir, 0755);

    // ── Preset list (idx, note, duration_s, name) ─────────────────────────────
    // Note and duration chosen to match how each instrument is referenced in
    // SAMPLE_TO_PRESET in test_calibration.py.  Decay-heavy presets (Gong,
    // TamTam, Triangle) get longer renders so T60 can be measured reliably.
    struct PresetEntry { int idx; uint8_t note; float dur; const char* name; };
    const PresetEntry presets[] = {
        { 0,  60, 3.0f, "InitDbg"  },
        { 1,  72, 3.0f, "Marimba"  },
        { 2,  36, 2.0f, "808Sub"   },
        { 3,  38, 1.5f, "AcSnre"   },
        { 4,  72,16.0f, "TblrBel"  },
        { 5,  40, 3.0f, "Timpani"  },
        { 6,  48, 2.0f, "Djambe"   },
        { 7,  41, 4.0f, "Taiko"    },
        { 8,  65, 1.5f, "MrchSnr"  },
        { 9,  41, 6.0f, "TamTam"   },
        {10,  60, 6.0f, "Koto"     },
        {11,  72, 6.0f, "Vibrph"   },
        {12,  48, 3.0f, "Wodblk"   },
        {13,  45, 2.0f, "AcTom"    },
        {14,  60, 4.0f, "Cymbal"   },
        {15,  50, 6.0f, "Gong"     },
        {16,  65, 6.0f, "Kalimba"  },
        {17,  60, 8.0f, "StelPan"  },
        {18,  79, 1.0f, "Claves"   },
        {19,  67, 3.0f, "Cowbel"   },
        {20,  84, 8.0f, "Triangle" },
        {21,  36, 2.0f, "Kick"     },
        {22,  60, 1.5f, "Clap"     },
        {23,  72, 2.0f, "Shaker"   },
        {24,  72, 3.0f, "Flute"    },
        {25,  72, 3.0f, "Clarinet" },
        {26,  36, 3.0f, "PlkBss"   },
        {27,  76,20.0f, "GlsBwl"   },
        {28,  69, 4.0f, "GtrStr"   },
        {29,  79, 1.5f, "HHat-C"   },
        {30,  79, 3.0f, "HHat-O"   },
        {31,  62, 2.0f, "Conga"    },
        {32,  62, 6.0f, "Handpn"   },
        {33,  84, 3.0f, "BelTre"   },
        {34,  60, 3.0f, "SltDrm"   },
        {35,  57, 6.0f, "Ride"     },
        {36,  60, 4.0f, "RidBel"   },
        {37,  57, 2.0f, "Bongo"    },
        {38,  88, 2.0f, "GlsBotl"  },
        {39,  49, 1.0f, "Tick"     },
    };
    const int n = (int)(sizeof(presets) / sizeof(presets[0]));

    printf("Rendering %d presets to %s/\n\n", n, out_dir);

    for (int i = 0; i < n; ++i) {
        const auto& p = presets[i];
        char path[512];
        snprintf(path, sizeof(path), "%s/%02d_%s.wav", out_dir, p.idx, p.name);
        render_preset(p.idx, p.note, p.dur, path, p.name);
    }

    printf("\nDone. Run: python3 test_audio_render.py %s/\n", out_dir);
    return 0;
}
