/**
 * brush_probe.cpp — characterise BrshSnr across velocity (host-only).
 *
 * HW: "sound is too much explosion like, too much chaotic, rather than soft
 * hit with resonance.  Even with negative velocity hit is too hard (it's just
 * lowering the volume not softening)."  Two separate claims, and they need two
 * separate measurements:
 *
 *   "explosion / chaotic, no resonance" → is there ANY pitched content, and
 *      how much of the energy is in the first 25 ms?  A brush stroke is a
 *      drawn-out swish over a quietly ringing head; an explosion is a
 *      front-loaded broadband burst with nothing behind it.  Spectral FLATNESS
 *      separates the two: ~1.0 is white noise, low is tonal.
 *
 *   "velocity only lowers volume" → does the timbre move with velocity, or
 *      only the level?  Measured as centroid and attack-share at three
 *      velocities after normalising each render to the same peak.  If the
 *      normalised curves are identical, the knob is a volume control.
 *
 * AcSnare is printed alongside as the in-family control.
 *
 * Usage:
 *   g++ -std=c++17 -O2 -I. -I.. -I../../common -I../common -DRUNTIME_COMMON_H_ \
 *       brush_probe.cpp -o /tmp/brush_probe && /tmp/brush_probe
 */

#include <cstdio>
#include <cstring>
#include <cmath>
#include <cstdint>
#include <vector>
#include <complex>
#include <algorithm>

#include "../common/runtime.h"
uint8_t mock_get_num_sample_banks() { return 1; }
uint8_t mock_get_num_samples_for_bank(uint8_t) { return 1; }
const sample_wrapper_t* mock_get_sample(uint8_t, uint8_t) { return nullptr; }

#include "synth_engine.h"

static const int kSR = 48000;

// Renders through GateOn(), the sequencer's own path, so the note under test
// is always the preset's OWN shipped Note — the pitch the drumlogue plays.
// Passing a note here instead would measure a preset that does not ship.
static std::vector<float> render(int preset, int /*unused*/, int vel, float dur) {
    static BrachettiSynth s;
    unit_runtime_desc_t d = {};
    d.samplerate = kSR; d.output_channels = 2;
    d.get_num_sample_banks = mock_get_num_sample_banks;
    d.get_num_samples_for_bank = mock_get_num_samples_for_bank;
    d.get_sample = mock_get_sample;
    s.Init(&d);
    s.LoadPreset((uint8_t)preset);
    for (int i = 0; i < NUM_VOICES; ++i)
        s.state.voices[i].exciter.noise_gen.seed = 2463534242UL;
    const int total = (int)(dur * kSR), block = 128;
    std::vector<float> mono((size_t)total, 0.0f);
    static float st[256];
    s.GateOn((uint8_t)vel);
    int f = 0; bool rel = false;
    while (f < total) {
        int todo = (total - f < block) ? (total - f) : block;
        memset(st, 0, sizeof(st));
        s.processBlock(st, (size_t)todo);
        for (int i = 0; i < todo; ++i) mono[f + i] = st[i * 2];
        f += todo;
        if (!rel && f >= kSR / 20) { s.GateOff(); rel = true; }
    }
    return mono;
}

// Power spectrum of a window, via a plain DFT on a decimated band grid.
static void spectrum(const std::vector<float>& x, int off, int len,
                     std::vector<double>& mag, int nbins, double fmax) {
    mag.assign((size_t)nbins, 0.0);
    if (off + len > (int)x.size()) len = (int)x.size() - off;
    if (len <= 0) return;
    for (int b = 0; b < nbins; ++b) {
        const double f = fmax * (b + 0.5) / nbins;
        const double w = 2.0 * M_PI * f / kSR;
        double re = 0.0, im = 0.0;
        for (int i = 0; i < len; ++i) {
            const double h = 0.5 - 0.5 * cos(2.0 * M_PI * i / len);   // Hann
            re += x[off + i] * h * cos(w * i);
            im -= x[off + i] * h * sin(w * i);
        }
        mag[b] = (re * re + im * im) / (double)len;
    }
}

struct Metrics { double peak, rms, atk25, atk50, centroid, flatness, t40, tone_hz, tone_db; };

static Metrics measure(const std::vector<float>& x) {
    Metrics m{};
    double tot = 0.0, e25 = 0.0, e50 = 0.0;
    for (size_t i = 0; i < x.size(); ++i) {
        const double e = (double)x[i] * x[i];
        tot += e;
        if ((int)i < kSR / 40) e25 += e;
        if ((int)i < kSR / 20) e50 += e;
        if (fabs(x[i]) > m.peak) m.peak = fabs(x[i]);
    }
    m.rms = sqrt(tot / x.size());
    m.atk25 = (tot > 0) ? e25 / tot : 0.0;
    m.atk50 = (tot > 0) ? e50 / tot : 0.0;

    // t40: time to fall 40 dB below the peak of a 10 ms envelope
    const int win = kSR / 100;
    double pk = 0.0;
    std::vector<double> env;
    for (size_t i = 0; i + win < x.size(); i += (size_t)win) {
        double s = 0.0;
        for (int j = 0; j < win; ++j) s += (double)x[i + j] * x[i + j];
        s = sqrt(s / win);
        env.push_back(s);
        if (s > pk) pk = s;
    }
    m.t40 = 0.0;
    for (size_t i = 0; i < env.size(); ++i)
        if (env[i] >= pk * 0.01) m.t40 = (double)(i + 1) * win / kSR;

    // Spectrum of the sustained part (50-350 ms): centroid and flatness.
    std::vector<double> mag;
    spectrum(x, kSR / 20, kSR * 3 / 10, mag, 96, 12000.0);
    double num = 0.0, den = 0.0, lsum = 0.0, asum = 0.0;
    for (int b = 0; b < 96; ++b) {
        const double f = 12000.0 * (b + 0.5) / 96;
        num += f * mag[b]; den += mag[b];
        lsum += log(mag[b] + 1e-300); asum += mag[b];
    }
    m.centroid = (den > 0) ? num / den : 0.0;
    m.flatness = (asum > 0) ? exp(lsum / 96) / (asum / 96) : 0.0;

    // Is there an audible RESONANCE?  Strongest narrow peak in the snare-head
    // band (100-600 Hz) of the sustained window, in dB over the median of that
    // band.  A pure noise wash reads ~0 dB; a ringing head reads well above.
    // Window 30-300 ms and search the SNARE-HEAD register (120-320 Hz).  A
    // wider search just finds the 2.2 kHz wire band's skirt, which is not the
    // resonance anyone means by "soft hit with resonance".
    std::vector<double> lo;
    spectrum(x, kSR * 3 / 100, kSR * 27 / 100, lo, 140, 700.0);
    std::vector<double> srt(lo.begin() + 12, lo.end());
    std::sort(srt.begin(), srt.end());
    const double med = srt[srt.size() / 2];
    double best = 0.0; m.tone_hz = 0.0;
    for (int b = 24; b <= 64; ++b)                       // 120-322 Hz
        if (lo[b] > best) { best = lo[b]; m.tone_hz = 700.0 * (b + 0.5) / 140; }
    m.tone_db = (med > 0) ? 10.0 * log10(best / med) : 0.0;
    return m;
}

int main(int argc, char** argv) {
    struct Case { int preset; int note; const char* name; };
    const Case cases[] = { { 37, 0, "BrshSnr" }, { 3, 0, "AcSnare" } };
    const int vels[] = { 127, 90, 64, 30 };

    printf("brush probe — BrshSnr voicing across velocity (AcSnare as control)\n");
    printf("flatness: 1.0 = white noise, low = tonal.  atk25 = share of total "
           "energy in the first 25 ms.\n");
    printf("the LAST two columns are peak-NORMALISED, so a knob that only "
           "changes level leaves them flat.\n\n");
    printf("%-9s %4s %8s %9s %7s %7s %9s %8s   %s\n",
           "preset", "vel", "peak", "rms", "atk25", "t40 s", "centroid", "flatness",
           "head resonance");
    for (const auto& c : cases) {
        for (int v : vels) {
            std::vector<float> x = render(c.preset, c.note, v, 2.0f);
            Metrics m = measure(x);
            printf("%-9s %4d %8.4f %9.5f %7.3f %7.3f %9.0f %8.3f   %5.1f dB @ %.0f Hz\n",
                   c.name, v, m.peak, m.rms, m.atk25, m.t40, m.centroid, m.flatness,
                   m.tone_db, m.tone_hz);
        }
        printf("\n");
    }
    (void)argc; (void)argv;
    return 0;
}
