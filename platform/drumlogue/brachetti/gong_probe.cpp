/**
 * gong_probe.cpp — what a REPEATED strike does to an ENGINE_CYMBAL preset.
 *
 * HW, twice: "multiple gong hits do not stack correctly and the sound is
 * muddy."  Every earlier pass on this answered it by counting voice indices
 * and CPU (`cym_cpu_probe`, T36) and never listened to the passage, so the
 * two things the report is actually about — does a strike ADD to what is
 * already sounding, and does the spectrum survive it — were never measured.
 * This probe measures those.
 *
 *   Ledger    per strike: which slot it took, whether that was a free slot or
 *             a re-strike, the bank it was given, and the aggregate cost.
 *   Stacking  per strike: the peak in the 30 ms after it over the level just
 *             before it.  A strike you can hear over the ring is > 1; the
 *             pre-fix build measured 1.5-2.3 while ping-ponging between two
 *             slots, and re-exciting one plate measures 14-18.
 *   Mud       whole-passage band split and spectral centroid.  Mud is not a
 *             mix problem here: it is sub-mode energy the bank synthesises
 *             from a DC-carrying drive, and it PILES UP across strikes, so it
 *             only shows on a passage.  Watch the <100 Hz column and the
 *             centroid against the single-strike row.
 *
 * Build: g++ -std=c++17 -O2 -I. -I.. -I../common -I../../common \
 *            -DRUNTIME_COMMON_H_ gong_probe.cpp -o /tmp/gong
 * Run:   /tmp/gong [wav_out_dir]      # WAVs only written if a dir is given
 */
#include <cstdio>
#include <cstring>
#include <cmath>
#include <vector>
#include <algorithm>
#include "../common/runtime.h"
uint8_t mock_get_num_sample_banks() { return 1; }
uint8_t mock_get_num_samples_for_bank(uint8_t) { return 1; }
const sample_wrapper_t* mock_get_sample(uint8_t, uint8_t) { return nullptr; }
float ut_exciter_out = 0.0f, ut_delay_read = 0.0f, ut_voice_out = 0.0f;
#include "synth_engine.h"

static const float SR = 48000.0f;
static unit_runtime_desc_t D;

static void mkdesc() {
    memset(&D, 0, sizeof(D));
    D.target = k_unit_target_drumlogue_synth;
    D.api = k_unit_api_2_0_0;
    D.samplerate = 48000;
    D.frames_per_buffer = 64;
    D.input_channels = 2;
    D.output_channels = 2;
    D.get_num_sample_banks = mock_get_num_sample_banks;
    D.get_num_samples_for_bank = mock_get_num_samples_for_bank;
    D.get_sample = mock_get_sample;
}

static void render(BrachettiSynth& s, int frames, std::vector<float>& out) {
    float buf[128];
    for (int done = 0; done < frames; done += 64) {
        memset(buf, 0, sizeof(buf));
        s.processBlock(buf, 64);
        for (int i = 0; i < 64; ++i) out.push_back(buf[i * 2]);
    }
}

static void write_wav(const char* path, const std::vector<float>& x) {
    FILE* f = fopen(path, "wb");
    if (!f) return;
    const int n = (int)x.size(), nb = n * 2;
    auto w32 = [&](uint32_t v) { fwrite(&v, 4, 1, f); };
    auto w16 = [&](uint16_t v) { fwrite(&v, 2, 1, f); };
    fwrite("RIFF", 1, 4, f); w32(36 + nb); fwrite("WAVE", 1, 4, f);
    fwrite("fmt ", 1, 4, f); w32(16); w16(1); w16(1);
    w32(48000); w32(96000); w16(2); w16(16);
    fwrite("data", 1, 4, f); w32(nb);
    for (int i = 0; i < n; ++i) {
        float v = x[i];
        if (v >  1.0f) v =  1.0f;
        if (v < -1.0f) v = -1.0f;
        const int16_t q = (int16_t)(v * 32767.0f);
        fwrite(&q, 2, 1, f);
    }
    fclose(f);
}

static double rms(const float* x, int n) {
    if (n <= 0) return 0.0;
    double a = 0.0;
    for (int i = 0; i < n; ++i) a += (double)x[i] * x[i];
    return sqrt(a / n);
}
static double peak(const float* x, int n) {
    double p = 0.0;
    for (int i = 0; i < n; ++i) { const double a = fabs((double)x[i]); if (a > p) p = a; }
    return p;
}

// Band energies + centroid, straight from a radix-2 FFT so the tool has no
// Python/NumPy dependency.  Band edges: 100 / 300 / 1k / 3k / 8k Hz.
static void bands(const std::vector<float>& x, double* out6, double* centroid) {
    int N = 1;
    while (N * 2 <= (int)x.size() && N < (1 << 20)) N *= 2;
    std::vector<double> re(N, 0.0), im(N, 0.0);
    for (int i = 0; i < N; ++i)
        re[i] = x[i] * (0.5 - 0.5 * cos(2.0 * M_PI * i / (N - 1)));   // Hann
    for (int i = 1, j = 0; i < N; ++i) {
        int bit = N >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) { std::swap(re[i], re[j]); std::swap(im[i], im[j]); }
    }
    for (int len = 2; len <= N; len <<= 1) {
        const double ang = -2.0 * M_PI / len, wr = cos(ang), wi = sin(ang);
        for (int i = 0; i < N; i += len) {
            double cr = 1.0, ci = 0.0;
            for (int k = 0; k < len / 2; ++k) {
                const double ur = re[i + k], ui = im[i + k];
                const double vr = re[i + k + len / 2] * cr - im[i + k + len / 2] * ci;
                const double vi = re[i + k + len / 2] * ci + im[i + k + len / 2] * cr;
                re[i + k] = ur + vr;              im[i + k] = ui + vi;
                re[i + k + len / 2] = ur - vr;    im[i + k + len / 2] = ui - vi;
                const double ncr = cr * wr - ci * wi;
                ci = cr * wi + ci * wr; cr = ncr;
            }
        }
    }
    static const double edge[7] = { 0.0, 100.0, 300.0, 1000.0, 3000.0, 8000.0, 24000.0 };
    double e[6] = {0}, tot = 0.0, wsum = 0.0;
    for (int k = 1; k < N / 2; ++k) {
        const double f = k * (double)SR / N, p = re[k] * re[k] + im[k] * im[k];
        tot += p; wsum += p * f;
        for (int b = 0; b < 6; ++b) if (f >= edge[b] && f < edge[b + 1]) { e[b] += p; break; }
    }
    for (int b = 0; b < 6; ++b) out6[b] = (tot > 0.0) ? 100.0 * e[b] / tot : 0.0;
    *centroid = (tot > 0.0) ? wsum / tot : 0.0;
}

static int cym_active(BrachettiSynth& s) {
    int n = 0;
    for (int i = 0; i < NUM_VOICES; ++i)
        if (s.state.voices[i].is_active && s.state.voices[i].cymbal.active) ++n;
    return n;
}
static int cym_cost(BrachettiSynth& s) {
    int c = 0;
    for (int i = 0; i < NUM_VOICES; ++i)
        if (s.state.voices[i].is_active && s.state.voices[i].cymbal.active)
            c += BrachettiSynth::kCymVoiceFixedLanes + (int)s.state.voices[i].cymbal.resCount;
    return c;
}

static void passage(int preset, uint8_t note, const char* name,
                    int hits, float gap_ms, bool ledger, const char* dir) {
    BrachettiSynth s;
    s.Init(&D);
    s.LoadPreset((uint8_t)preset);
    const int gap = (int)(gap_ms * 0.001f * SR) & ~63;
    std::vector<float> audio;
    std::vector<double> onset;

    if (ledger)
        printf("  hit  slot  kind      bank  voices  cost/%d\n",
               BrachettiSynth::kCymCostBudget);
    for (int k = 0; k < hits; ++k) {
        const int pre_n = (int)(0.010f * SR);
        const double before = (audio.size() >= (size_t)pre_n)
            ? rms(audio.data() + audio.size() - pre_n, pre_n) : 0.0;
        const int active_before = cym_active(s);
        s.NoteOn(note, 110);
        s.NoteOff(note);
        const int slot = (int)s.state.next_voice_idx;
        if (ledger)
            printf("  %2d   %2d   %s   %3d     %d      %d\n", k + 1, slot,
                   (cym_active(s) > active_before) ? "new     " : "re-strike",
                   (int)s.state.voices[slot].cymbal.resCount, cym_active(s), cym_cost(s));
        const size_t mark = audio.size();
        render(s, gap, audio);
        const int on_n = (int)std::min((size_t)(0.030f * SR), audio.size() - mark);
        const double after = peak(audio.data() + mark, on_n);
        if (k > 0) onset.push_back(before > 1e-6 ? after / before : 0.0);
    }
    render(s, (int)(2.0f * SR), audio);

    if (!onset.empty()) {
        printf("  strike over ring:");
        for (double v : onset) printf(" %.1f", v);
        printf("\n");
    }
    double e[6], cen;
    bands(audio, e, &cen);
    const double r = rms(audio.data(), (int)audio.size());
    const double p = peak(audio.data(), (int)audio.size());
    printf("  rms %.4f  peak %.3f  crest %5.2f  centroid %6.0f Hz   "
           "<100 %.1f | 100-300 %.1f | 300-1k %.1f | 1-3k %.1f | 3-8k %.1f | >8k %.1f\n",
           r, p, (r > 0.0) ? p / r : 0.0, cen, e[0], e[1], e[2], e[3], e[4], e[5]);
    if (dir) {
        char path[512];
        snprintf(path, sizeof(path), "%s/%s_%dx%.0f.wav", dir, name, hits, gap_ms);
        write_wav(path, audio);
    }
}

int main(int argc, char** argv) {
    mkdesc();
    const char* dir = (argc > 1) ? argv[1] : nullptr;

    printf("=== Gong (preset 14) ===\n");
    printf("\n-- one strike --\n");
    passage(14, 50, "gong", 1, 3000.0f, true, dir);
    for (float gap : {150.0f, 300.0f, 600.0f}) {
        printf("\n-- 8 strikes, %.0f ms apart --\n", gap);
        passage(14, 50, "gong", 8, gap, true, dir);
    }
    printf("\n=== the rest of the ENGINE_CYMBAL family, 8 strikes @300 ms ===\n");
    struct C { int p; uint8_t n; const char* name; };
    for (const C& c : { C{13, 65, "cymbal"}, C{26, 79, "hhat-o"},
                        C{31, 69, "ride"}, C{32, 60, "ridbel"}, C{36, 76, "splash"} }) {
        printf("\n-- %s --\n", c.name);
        passage(c.p, c.n, c.name, 1, 3000.0f, false, dir);
        passage(c.p, c.n, c.name, 8, 300.0f, false, dir);
    }
    return 0;
}
