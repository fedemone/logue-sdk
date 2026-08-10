/**
 * note_audit.cpp — render every preset AT ITS OWN SHIPPED NOTE and report what
 * the Note parameter is actually doing on that engine.
 *
 * Build (from the brachetti directory):
 *   g++ -std=c++17 -O2 -I. -I.. -I../../common -I../common -DRUNTIME_COMMON_H_ \
 *       note_audit.cpp -o note_audit
 * Run:
 *   ./note_audit [wav_output_dir]     # analysis to stdout, WAVs optional
 *
 * Why a dedicated tool: render_presets.cpp passes its OWN hard-coded note per
 * preset, which is the note the calibration was scored against — it can (and
 * in one case did) drift from the Note column the preset actually ships, and
 * the shipped column is what the drumlogue plays.  This tool drives GateOn(),
 * the exact path the internal sequencer uses, so the note under test is always
 * m_ui_note = the preset's own Note parameter.
 *
 * Printed per preset:
 *   note / nominal Hz   the shipped Note column and its equal-tempered pitch
 *   engine              the engine that note is feeding
 *   anchor              the engine-internal reference the note is measured
 *                       against, where one exists (cymbal ref_note, kernel
 *                       root_note).  A mismatch means the shipped preset does
 *                       NOT play its calibrated spectrum.
 *   f0 / centroid       measured from the render (see note_audit.py)
 */

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <string>
#include <sys/stat.h>

#include "../common/runtime.h"
uint8_t mock_get_num_sample_banks() { return 1; }
uint8_t mock_get_num_samples_for_bank(uint8_t) { return 1; }
const sample_wrapper_t* mock_get_sample(uint8_t, uint8_t) { return nullptr; }

float ut_exciter_out = 0.0f;
float ut_delay_read  = 0.0f;
float ut_voice_out   = 0.0f;
#include "synth_engine.h"

static const int kSR = 48000;

static bool write_wav(const char* path, const float* x, int n) {
    FILE* f = fopen(path, "wb");
    if (!f) return false;
    const int nb = n * 2;
    auto w32 = [&](uint32_t v) { fwrite(&v, 4, 1, f); };
    auto w16 = [&](uint16_t v) { fwrite(&v, 2, 1, f); };
    fwrite("RIFF", 1, 4, f); w32(36 + nb); fwrite("WAVE", 1, 4, f);
    fwrite("fmt ", 1, 4, f); w32(16); w16(1); w16(1);
    w32(kSR); w32(kSR * 2); w16(2); w16(16);
    fwrite("data", 1, 4, f); w32((uint32_t)nb);
    for (int i = 0; i < n; ++i) {
        float v = x[i];
        if (v >  1.0f) v =  1.0f;
        if (v < -1.0f) v = -1.0f;
        int16_t s = (int16_t)(v * 32767.0f);
        fwrite(&s, 2, 1, f);
    }
    fclose(f);
    return true;
}

static const char* engine_name(BrachettiSynth::EngineType e) {
    switch (e) {
        case BrachettiSynth::ENGINE_KS:       return "KS";
        case BrachettiSynth::ENGINE_BAR:      return "BAR";
        case BrachettiSynth::ENGINE_MEMBRANE: return "MEMBRANE";
        case BrachettiSynth::ENGINE_SNARE:    return "SNARE";
        case BrachettiSynth::ENGINE_PLATE:    return "PLATE";
        case BrachettiSynth::ENGINE_NOISE:    return "NOISE";
        case BrachettiSynth::ENGINE_CYMBAL:   return "CYMBAL";
        default:                              return "REMOVED";
    }
}

// The engine-internal note the preset was calibrated at, where the engine has
// one.  Kept in one place so the audit cannot drift from the DSP:
//   CYMBAL — ref_note in the NoteOn config switch (pitch_ratio pivots on it)
//   kernel — ModalDrumRecipe::root_note (the Trigger ratio pivots on it)
// Everything else tunes mode 1 straight from the note, so there is no anchor
// to disagree with: the note IS the fundamental.
static int engine_anchor(int preset) {
    switch (preset) {
        case BrachettiSynth::k_Cymbal:    return 65;
        case BrachettiSynth::k_Ride:      return 69;
        case BrachettiSynth::k_RideBell:  return 60;
        case BrachettiSynth::k_Gong:      return 50;
        case BrachettiSynth::k_HiHatOpen: return 79;
        case BrachettiSynth::k_Splash:    return 76;
        case BrachettiSynth::k_Timpani:   return (int)kTimpaniRecipe.root_note;
        case BrachettiSynth::k_Taiko:     return (int)kTaikoRecipe.root_note;
        default:          return -1;
    }
}

int main(int argc, char** argv) {
    const char* dir = (argc > 1) ? argv[1] : nullptr;
    if (dir) mkdir(dir, 0755);

    printf("idx,name,note,nominal_hz,engine,anchor,anchor_delta_semis,wav,wav_up12\n");

    // Render `preset` at `note` through the sequencer path and return the mono
    // buffer.  transpose != 0 moves the Note parameter, which is how the second
    // render (+12 semitones) tests whether the preset TRACKS its note at all.
    auto render = [&](int preset, int transpose, int n) -> float* {
        BrachettiSynth s;
        unit_runtime_desc_t d = {};
        d.samplerate = kSR;
        d.output_channels = 2;
        d.get_num_sample_banks     = mock_get_num_sample_banks;
        d.get_num_samples_for_bank = mock_get_num_samples_for_bank;
        d.get_sample               = mock_get_sample;
        s.Init(&d);
        s.LoadPreset((uint8_t)preset);
        for (int i = 0; i < NUM_VOICES; ++i)
            s.state.voices[i].exciter.noise_gen.seed = 2463534242UL;
        if (transpose) {
            const int nn = s.getParameterValue(BrachettiSynth::k_paramNote) + transpose;
            s.setParameter(BrachettiSynth::k_paramNote, nn);
        }
        float* mono = new float[n]();
        float buf[128];
        s.GateOn(110);          // same-tick gate, the drumlogue's own path
        s.GateOff();
        for (int done = 0; done < n; done += 64) {
            std::memset(buf, 0, sizeof(buf));
            s.processBlock(buf, 64);
            for (int i = 0; i < 64 && done + i < n; ++i) mono[done + i] = buf[i * 2];
        }
        return mono;
    };

    for (int p = 0; p < 40; ++p) {
        BrachettiSynth s;
        unit_runtime_desc_t d = {};
        d.samplerate = kSR;
        d.output_channels = 2;
        d.get_num_sample_banks     = mock_get_num_sample_banks;
        d.get_num_samples_for_bank = mock_get_num_samples_for_bank;
        d.get_sample               = mock_get_sample;
        s.Init(&d);
        s.LoadPreset((uint8_t)p);

        // The shipped Note column, read back through the parameter store —
        // exactly the value the sequencer will play through GateOn().
        const int note = s.getParameterValue(BrachettiSynth::k_paramNote);
        const float hz = 440.0f * powf(2.0f, ((float)note - 69.0f) / 12.0f);
        const BrachettiSynth::EngineType eng = s.kPresetEngine[p];
        const int anchor = engine_anchor(p);

        const int n = (int)(3.0f * kSR);
        char path[512] = "", path12[512] = "";
        if (dir) {
            float* a = render(p, 0, n);
            float* b = render(p, 12, n);
            snprintf(path,   sizeof(path),   "%s/%02d_%s.wav", dir, p,
                     BrachettiSynth::getPresetName((uint8_t)p));
            snprintf(path12, sizeof(path12), "%s/%02d_%s_up12.wav", dir, p,
                     BrachettiSynth::getPresetName((uint8_t)p));
            write_wav(path,   a, n);
            write_wav(path12, b, n);
            delete[] a;
            delete[] b;
        }

        printf("%d,%s,%d,%.2f,%s,", p, BrachettiSynth::getPresetName((uint8_t)p),
               note, hz, engine_name(eng));
        if (anchor >= 0) printf("%d,%d,", anchor, note - anchor);
        else             printf(",,");
        printf("%s,%s\n", path, path12);
    }
    return 0;
}
