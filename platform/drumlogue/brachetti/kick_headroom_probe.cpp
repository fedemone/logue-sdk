/**
 * kick_headroom_probe.cpp — is the kick driving the master limiter? (host-only)
 *
 * Two HW reports from pass 29 look like one cause:
 *   "VlMllRes/VlMllStf are still too subtle ... 'thump' is not really increased"
 *   "Boom for kicks was declared perfect, but with long decays it sounds a bit
 *    brittle or distorted"
 *
 * If the kick already sits above the master limiter's knee for most of its
 * length, then (a) anything the thump layer adds is compressed straight back
 * out, so the knob measures ~1.0x, and (b) the boom — a near-sine — gets its
 * crest flattened into harmonics, which is what "brittle / distorted" sounds
 * like, and worst on LONG decays because the sustained part sits in the knee
 * longest.
 *
 * Reported per preset:
 *   >knee   % of samples above kMasterLimThr (the limiter is working)
 *   GR      mean gain reduction applied by the limiter, in dB
 *   H2..H5  harmonic distortion of the boom fundamental (Goertzel), in dB
 *   crest   peak / RMS over the first 250 ms (a clean sine is 1.41)
 *
 * Build: g++ -std=c++17 -O2 -I. -I.. -I../common -I../../common \
 *          -DRUNTIME_COMMON_H_ kick_headroom_probe.cpp -o /tmp/kh && /tmp/kh
 */
#include <cstdio>
#include <cstring>
#include <cmath>
#include <vector>
#include "../common/runtime.h"
uint8_t mock_get_num_sample_banks() { return 1; }
uint8_t mock_get_num_samples_for_bank(uint8_t) { return 1; }
const sample_wrapper_t* mock_get_sample(uint8_t, uint8_t) { return nullptr; }
float ut_exciter_out = 0, ut_delay_read = 0, ut_voice_out = 0;
#include "synth_engine.h"

static unit_runtime_desc_t D;
static BrachettiSynth s;
static const float SR = 48000.0f;

static std::vector<float> render(int preset, uint8_t note, float dur,
                                 int knob = -1, int32_t val = 0,
                                 int knob2 = -1, int32_t val2 = 0) {
    s.Init(&D);
    s.LoadPreset((uint8_t)preset);
    if (knob  >= 0) s.setParameter((uint8_t)knob,  val);
    if (knob2 >= 0) s.setParameter((uint8_t)knob2, val2);
    for (int i = 0; i < NUM_VOICES; ++i)
        s.state.voices[i].exciter.noise_gen.seed = 2463534242UL;
    s.GateOn(110);
    s.GateOff();
    std::vector<float> out;
    float blk[256];
    for (int f = 0; f < (int)(dur * SR); f += 128) {
        memset(blk, 0, sizeof(blk));
        s.processBlock(blk, 128);
        for (int i = 0; i < 128; ++i) out.push_back(blk[i * 2]);
    }
    return out;
}

// Goertzel magnitude at f over the whole buffer.
static double goertzel(const std::vector<float>& x, double f) {
    const double w = 2.0 * M_PI * f / SR, c = 2.0 * cos(w);
    double s1 = 0, s2 = 0;
    for (float v : x) { const double s0 = v + c * s1 - s2; s2 = s1; s1 = s0; }
    return sqrt(s1 * s1 + s2 * s2 - c * s1 * s2) / (double)x.size();
}

struct K { const char* name; int idx; uint8_t note; double f0; };

static void report(const char* label, const K& k, int knob, int32_t val,
                   int knob2, int32_t val2) {
    std::vector<float> x = render(k.idx, k.note, 1.5f, knob, val, knob2, val2);
    // limiter statistics, recomputed from the same constants the engine uses
    size_t above = 0; double gr_acc = 0; size_t gr_n = 0;
    for (float v : x) {
        const double a = fabs(v);
        if (a > kMasterLimThr) {
            ++above;
            // invert the knee to recover what went in:  y = thr + span*o/(o+span)
            const double y = a - kMasterLimThr;
            const double span = kMasterLimSpan;
            if (y < span) {
                const double o = y * span / (span - y);   // pre-knee excess
                const double pre = kMasterLimThr + o;
                if (pre > 1e-9) { gr_acc += 20.0 * log10(a / pre); ++gr_n; }
            }
        }
    }
    const size_t n250 = (size_t)(0.250 * SR);
    double pk = 0, acc = 0;
    for (size_t i = 0; i < n250 && i < x.size(); ++i) {
        pk = fmax(pk, fabs(x[i])); acc += (double)x[i] * x[i];
    }
    const double r = sqrt(acc / (double)n250);
    const double h1 = goertzel(x, k.f0);
    double h[4];
    for (int i = 0; i < 4; ++i) h[i] = goertzel(x, k.f0 * (i + 2));
    auto dbr = [&](double v) { return h1 > 1e-12 ? 20.0 * log10(v / h1) : 0.0; };
    printf("  %-22s %6.1f%% %8.2f dB   %6.1f %6.1f %6.1f %6.1f   %5.2f\n",
           label, 100.0 * (double)above / (double)x.size(),
           gr_n ? gr_acc / (double)gr_n : 0.0,
           dbr(h[0]), dbr(h[1]), dbr(h[2]), dbr(h[3]),
           r > 1e-9 ? pk / r : 0.0);
}

int main() {
    D.samplerate = 48000; D.output_channels = 2;
    D.get_num_sample_banks = mock_get_num_sample_banks;
    D.get_num_samples_for_bank = mock_get_num_samples_for_bank;
    D.get_sample = mock_get_sample;

    static const K ks[] = {
        { "Kick2",    0, 36, 65.4 }, { "808Sub", 2, 36, 65.4 },
        { "KickDrum", 20, 36, 65.4 },
    };
    printf("master limiter knee = %.2f, ceiling = %.2f\n\n",
           kMasterLimThr, kMasterLimCeil);
    for (const K& k : ks) {
        printf("%s\n", k.name);
        printf("  %-22s %7s %11s   %6s %6s %6s %6s   %5s\n",
               "", ">knee", "mean GR", "H2", "H3", "H4", "H5", "crest");
        report("shipped",              k, -1,  0, -1, 0);
        report("Dkay max (long boom)", k, 10, 200, -1, 0);
        report("Dkay+Rel max",         k, 10, 200, 14, 20);
        report("VlMllRes +100",        k,  6, 100, -1, 0);
        report("VlMllRes +100, Dk max",k,  6, 100, 10, 200);
        printf("\n");
    }
    return 0;
}
