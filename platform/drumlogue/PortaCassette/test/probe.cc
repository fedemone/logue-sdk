/**
 * @file probe.cc
 * @brief Host probe for PortaCassette: renders test tones through the DSP.
 *
 * Built for the drumlogue's Cortex-A7 and run under qemu-arm by run.sh, so the
 * NEON paths measured are the ones that ship.  measure.py drives it.
 *
 *   probe out.raw [key=value ...]
 *
 * Unit parameters take their raw header.c values:
 *   model dbx drive trim age mix hiss xt attack release
 * Defaults are the header's, except mix=100, hiss=0 and xt=0, so a tone
 * measurement sees the tape and nothing else.
 *
 * Signal:
 *   f1 a1 f2 a2  two sines: frequency in Hz (0 = off), sine PEAK level in dBFS
 *   start        seconds of silence before the tones (default 0)
 *   hit tau      tone 1 becomes a train of hits, one every `hit` seconds, each
 *                decaying with time constant `tau` (a kick, with f1 ~ 55 Hz)
 *   secs         render length in seconds (default 3)
 *   lonly        1 = drive the left input only (for crosstalk)
 *   outch        output channel written: 0 = left (default), 1 = right
 *   nowf         1 = wow & flutter off, for clean spectra
 *   stride       input channels per frame, 2 or 4 (default 2)
 *
 * Writes the chosen output channel as raw float32.
 */
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <arm_neon.h>

// The probe pokes one private member (the wow & flutter depth); everything
// else goes through the unit's own parameter path.
#define private public
#include "PortaCassette.h"
#undef private

static PortaCassette s_pc;

int main(int argc, char** argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: probe out.raw [key=value ...]\n");
        return 1;
    }
    int32_t prm[PortaCassette::k_num_params];
    for (int i = 0; i < PortaCassette::k_num_params; ++i) prm[i] = PortaCassette::kParamInit[i];
    prm[PortaCassette::k_param_mix] = 100;
    prm[PortaCassette::k_hiss]      = 0;
    prm[PortaCassette::k_crosstalk] = 0;

    double f1 = 997.0, a1 = -20.0, f2 = 0.0, a2 = -200.0, start = 0.0, secs = 3.0;
    double hit = 0.0, tau = 0.08;
    int lonly = 0, outch = 0, nowf = 0, stride = 2;

    struct { const char* key; int id; } const kParams[] = {
        { "model", PortaCassette::k_param_model }, { "dbx",     PortaCassette::k_param_dbx },
        { "drive", PortaCassette::k_param_drive }, { "trim",    PortaCassette::k_param_preamp },
        { "age",   PortaCassette::k_param_age },   { "mix",     PortaCassette::k_param_mix },
        { "hiss",  PortaCassette::k_hiss },        { "xt",      PortaCassette::k_crosstalk },
        { "attack", PortaCassette::k_attack },     { "release", PortaCassette::k_release },
    };
    for (int a = 2; a < argc; ++a) {
        const char* eq = strchr(argv[a], '=');
        if (!eq) { fprintf(stderr, "bad argument %s\n", argv[a]); return 1; }
        const size_t n = (size_t)(eq - argv[a]);
        const char* v = eq + 1;
        bool known = false;
        for (const auto& p : kParams)
            if (strlen(p.key) == n && !strncmp(argv[a], p.key, n)) { prm[p.id] = atoi(v); known = true; }
#define PROBE_KEY(NAME, VAR, CONV) \
        if (!known && n == strlen(NAME) && !strncmp(argv[a], NAME, n)) { VAR = CONV(v); known = true; }
        PROBE_KEY("f1", f1, atof)  PROBE_KEY("a1", a1, atof)
        PROBE_KEY("f2", f2, atof)  PROBE_KEY("a2", a2, atof)
        PROBE_KEY("start", start, atof)  PROBE_KEY("secs", secs, atof)
        PROBE_KEY("hit", hit, atof)      PROBE_KEY("tau", tau, atof)
        PROBE_KEY("lonly", lonly, atoi)  PROBE_KEY("outch", outch, atoi)
        PROBE_KEY("nowf", nowf, atoi)    PROBE_KEY("stride", stride, atoi)
#undef PROBE_KEY
        if (!known) { fprintf(stderr, "unknown key %s\n", argv[a]); return 1; }
    }

    unit_runtime_desc_t d;
    memset(&d, 0, sizeof(d));
    d.samplerate = 48000;
    d.frames_per_buffer = 64;
    d.input_channels = (uint8_t)stride;
    d.output_channels = 2;
    if (s_pc.Init(&d) != k_unit_err_none) { fprintf(stderr, "init failed\n"); return 1; }
    for (int i = 0; i < PortaCassette::k_num_params; ++i) s_pc.SetParameter((uint8_t)i, prm[i]);
    s_pc.Reset();   // lands the smoothed parameters on their targets

    const uint32_t N = 64;
    std::vector<float> in(N * stride, 0.0f), out(N * 2);
    s_pc.ProcessBlock(in.data(), out.data(), N);   // applies the coefficients
    if (nowf) s_pc.wf_scale_ = 0.0f;               // a fixed 1-sample delay

    FILE* fp = fopen(argv[1], "wb");
    if (!fp) { perror(argv[1]); return 1; }
    const double g1 = pow(10.0, a1 / 20.0), g2 = pow(10.0, a2 / 20.0);
    const double w1 = 2.0 * M_PI * f1 / 48000.0, w2 = 2.0 * M_PI * f2 / 48000.0;
    const uint64_t total = (uint64_t)(secs * 48000.0), n_start = (uint64_t)(start * 48000.0);
    for (uint64_t n0 = 0; n0 < total; n0 += N) {
        for (uint32_t i = 0; i < N; ++i) {
            const uint64_t n = n0 + i;
            double x = 0.0;
            if (n >= n_start) {
                const double t = (double)(n - n_start);
                if (f1 > 0.0) {
                    if (hit > 0.0) {
                        const double th = fmod(t / 48000.0, hit);   // time since the hit
                        x += g1 * exp(-th / tau) * sin(2.0 * M_PI * f1 * th);
                    } else {
                        x += g1 * sin(w1 * t);
                    }
                }
                if (f2 > 0.0) x += g2 * sin(w2 * t);
            }
            in[i * stride + 0] = (float)x;
            in[i * stride + 1] = lonly ? 0.0f : (float)x;
            if (stride == 4) in[i * stride + 2] = in[i * stride + 3] = 0.0f;
        }
        s_pc.ProcessBlock(in.data(), out.data(), N);
        for (uint32_t i = 0; i < N; ++i) fwrite(&out[i * 2 + (outch ? 1 : 0)], sizeof(float), 1, fp);
    }
    fclose(fp);
    return 0;
}
