/**
 * preset_probe.cpp — render and gain-stage the presets of ONE Brachetti family
 * unit on the host.
 *
 * Built once per unit directory, like equiv_driver.cpp:
 *
 *   g++ -std=c++17 -O2 -I<unit-dir> -I<unit-dir>/.. -I<drumlogue>/common \
 *       -DRUNTIME_COMMON_H_ preset_probe.cpp -o probe
 *
 *   probe list
 *       index, name and root note of every preset
 *   probe render <first> <last> <velocity> <seconds> <out-dir>
 *       strike each preset as the sequencer does (GateOn and GateOff in the
 *       same tick) on a freshly booted unit and write <out-dir>/<idx>.wav:
 *       48 kHz, 32-bit float, mono, through the whole master stage
 *   probe match <first> <last> <velocity> <seconds> <out-dir>
 *       the VOICE of each preset: as render, but before the master limiter
 *       (filter, drive and trim only), with each preset first given the trim
 *       calib would give it (body target 1.5); prints "<idx> <trim>" per
 *       preset.  What match_samples.py compares with a reference: the limiter
 *       is the family's gain staging, the same on every preset, not voicing
 *   probe calib [target]
 *       recalibrate the output trims (kOutTrim): the method of the original's
 *       calib_probe.cpp — a held gate at velocity 127 on the preset's root,
 *       the master limiter bypassed, the peak over [10 ms, 1 s] put on the
 *       body target (1.5 ships), clamped to <= 1.  It measures through the
 *       trim already in the table and composes it, so a calibrated tree
 *       reprints its own table.
 *
 * Host tool only.  It reaches the units' private members (the master stage,
 * the voice set) to bypass the limiter, so it opens their access first.
 */
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "../common/runtime.h"
uint8_t mock_get_num_sample_banks() { return 0; }
uint8_t mock_get_num_samples_for_bank(uint8_t) { return 0; }
const sample_wrapper_t* mock_get_sample(uint8_t, uint8_t) { return nullptr; }

#define private public
#define protected public
#include "synth_engine.h"
#undef private
#undef protected

static const int kSR = 48000;
static BrachettiSynth g_synth;   // static storage: the units are built to be constant-initialised

static void boot(BrachettiSynth& s) {
    s = BrachettiSynth();
    unit_runtime_desc_t d = {};
    d.samplerate = kSR;
    d.output_channels = 2;
    d.get_num_sample_banks = mock_get_num_sample_banks;
    d.get_num_samples_for_bank = mock_get_num_samples_for_bank;
    d.get_sample = mock_get_sample;
    s.Init(&d);
}

// The unit's processBlock with the limiter left out: the voices onto the bus,
// then the master filter and drive × trim, exactly as MasterStage::render
// does before it limits.
static void render_unlimited(BrachettiSynth& s, float* out, size_t frames) {
    brc::MasterStage::clear(out, frames);
    const float tone = s.m_master.tone;
    s.m_master.release_held(s.m_voices);
    for (auto& v : s.m_voices)
        if (v.is_active) s.render_voice(v, out, frames, tone);
    const float g = s.m_master.drive * s.m_master.out_trim;
    for (size_t i = 0; i < frames; ++i) out[i * 2] = s.m_master.filter.process(out[i * 2]) * g;
}

static void write_wav(const std::string& path, const std::vector<float>& x) {
    FILE* f = fopen(path.c_str(), "wb");
    if (!f) { perror(path.c_str()); exit(1); }
    const uint32_t data = (uint32_t)(x.size() * 4);
    const uint32_t riff = 36 + data, fmt_size = 16, rate = kSR, brate = kSR * 4;
    const uint16_t tag = 3, ch = 1, align = 4, bits = 32;
    fwrite("RIFF", 1, 4, f); fwrite(&riff, 4, 1, f); fwrite("WAVE", 1, 4, f);
    fwrite("fmt ", 1, 4, f); fwrite(&fmt_size, 4, 1, f); fwrite(&tag, 2, 1, f);
    fwrite(&ch, 2, 1, f); fwrite(&rate, 4, 1, f); fwrite(&brate, 4, 1, f);
    fwrite(&align, 2, 1, f); fwrite(&bits, 2, 1, f);
    fwrite("data", 1, 4, f); fwrite(&data, 4, 1, f);
    fwrite(x.data(), 4, x.size(), f);
    fclose(f);
}

int main(int argc, char** argv) {
    const std::string cmd = (argc > 1) ? argv[1] : "list";
    const int n = BrachettiSynth::k_NumPrograms;
    float buf[64 * 2];

    if (cmd == "list") {
        for (int i = 0; i < n; ++i) {
            boot(g_synth);
            g_synth.LoadPreset((uint8_t)i);
            printf("%d %s %d\n", i, BrachettiSynth::getPresetName((uint8_t)i), g_synth.PresetRootNote());
        }
        return 0;
    }

    if (cmd == "render" && argc >= 7) {
        const int first = atoi(argv[2]), last = atoi(argv[3]);
        const int vel = atoi(argv[4]);
        const int total = (int)(atof(argv[5]) * kSR);
        const std::string dir = argv[6];
        for (int i = first; i <= last && i < n; ++i) {
            boot(g_synth);
            g_synth.LoadPreset((uint8_t)i);
            std::vector<float> x(total, 0.0f);
            g_synth.GateOn((uint8_t)vel);
            g_synth.GateOff();
            for (int f = 0; f < total; f += 64) {
                const int todo = (total - f < 64) ? total - f : 64;
                g_synth.processBlock(buf, (size_t)todo);
                for (int j = 0; j < todo; ++j) x[f + j] = buf[j * 2];
            }
            write_wav(dir + "/" + std::to_string(i) + ".wav", x);
        }
        return 0;
    }

    if (cmd == "match" && argc >= 7) {
        // The voice before the limiter, at the trim calib would give it
        // (computed here, on a held gate at velocity 127).
        const int first = atoi(argv[2]), last = atoi(argv[3]);
        const int vel = atoi(argv[4]);
        const int total = (int)(atof(argv[5]) * kSR);
        const std::string dir = argv[6];
        const int ctotal = 3 * kSR, lo = kSR / 100, hi = kSR;
        for (int i = first; i <= last && i < n; ++i) {
            boot(g_synth);
            g_synth.LoadPreset((uint8_t)i);
            g_synth.m_master.out_trim = 1.0f;
            g_synth.GateOn(127);
            float body = 0.0f;
            for (int f = 0; f < ctotal; f += 64) {
                render_unlimited(g_synth, buf, 64);
                for (int j = 0; j < 64; ++j)
                    if (f + j >= lo && f + j < hi && fabsf(buf[j * 2]) > body) body = fabsf(buf[j * 2]);
            }
            const float trim = (body > 1e-6f) ? fminf(1.0f, 1.5f / body) : 1.0f;
            boot(g_synth);
            g_synth.LoadPreset((uint8_t)i);
            g_synth.m_master.out_trim = trim;
            std::vector<float> x(total, 0.0f);
            g_synth.GateOn((uint8_t)vel);
            g_synth.GateOff();
            for (int f = 0; f + 64 <= total; f += 64) {
                render_unlimited(g_synth, buf, 64);
                for (int j = 0; j < 64; ++j) x[f + j] = buf[j * 2];
            }
            write_wav(dir + "/" + std::to_string(i) + ".wav", x);
            printf("%d %.5f\n", i, trim);
        }
        return 0;
    }

    if (cmd == "calib") {
        const float target = (argc > 2) ? (float)atof(argv[2]) : 1.5f;
        const int total = 6 * kSR, lo = kSR / 100, hi = kSR;
        printf("// body target %.2f, measured at velocity 127 on each preset's own root\n", target);
        printf("%-4s %-9s %9s %9s %9s %9s\n", "idx", "name", "body", "strike", "trim was", "trim");
        for (int i = 0; i < n; ++i) {
            boot(g_synth);
            g_synth.LoadPreset((uint8_t)i);
            g_synth.GateOn(127);   // held, as the original's calib_probe
            float body = 0.0f, strike = 0.0f;
            for (int f = 0; f < total; f += 64) {
                render_unlimited(g_synth, buf, 64);
                for (int j = 0; j < 64; ++j) {
                    const float a = fabsf(buf[j * 2]);
                    if (f + j < lo) { if (a > strike) strike = a; }
                    else if (f + j < hi && a > body) body = a;
                }
            }
            const float was = g_synth.m_master.out_trim;
            const float trim = (body > 1e-6f) ? fminf(1.0f, was * target / body) : was;
            printf("%-4d %-9s %9.4f %9.4f %9.5f %9.5f\n", i, BrachettiSynth::getPresetName((uint8_t)i),
                   body, strike, was, trim);
        }
        return 0;
    }

    fprintf(stderr, "usage: probe list | render|match <first> <last> <vel> <seconds> <dir> | calib [target]\n");
    return 2;
}
