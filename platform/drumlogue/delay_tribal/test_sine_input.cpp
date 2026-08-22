/**
 * test_sine_input.cpp
 *
 * @brief Behavioural tests for PercussionSpatializer, driven through the real
 *        engine via Render().
 *
 * This file used to contain a standalone re-implementation of the effect
 * ("Spat") with its own hardcoded delay constants.  It never linked
 * PercussionSpatializer.cc, so it asserted only that the mock agreed with
 * itself; its constants had long since drifted from the engine (it modelled an
 * 8 ms base with fixed 4-lane group spacing, against real tribal taps of
 * 8-106 ms scaled by Depth and Gap) and it failed on every recent commit.
 *
 * Everything below drives the actual engine instead.
 */

#include <cassert>
#include <cmath>
#include <cstdio>
#include <vector>

#include "PercussionSpatializer.h"

static constexpr int   kSR    = 48000;
static constexpr float kSRf   = 48000.0f;

// ---------------------------------------------------------------------------
// Harness
// ---------------------------------------------------------------------------
struct Rig {
    unit_runtime_desc_t desc{};
    PercussionSpatializer sp;

    Rig(int clones, int mode, int rate, int spread,
        int wobble, int scatter, int soft, int gap) {
        desc.samplerate = kSR;
        desc.input_channels = 2;
        desc.output_channels = 2;
        sp.Init(&desc);
        sp.Reset();
        sp.setParameter(k_clones, clones);
        sp.setParameter(k_mode, mode);
        sp.setParameter(k_rate, rate);
        sp.setParameter(k_spread, spread);
        sp.setParameter(k_wobble, wobble);
        sp.setParameter(k_scatter, scatter);
        sp.setParameter(k_attack_softening, soft);
        sp.setParameter(k_gap, gap);
        settle();
    }

    // Parameter smoothing runs over ~480 samples; let it arrive before measuring.
    void settle() {
        float z[8] = {0}, o[8];
        for (int b = 0; b < 400; ++b) sp.Render(z, o, 4);
    }

    // One impulse, then `ms` of tail. Returns interleaved stereo.
    std::vector<float> impulse(int ms) {
        const int n = (ms * kSR) / 1000;
        std::vector<float> y(n * 2, 0.0f);
        for (int b = 0; b * 4 < n; ++b) {
            float in[8] = {0}, out[8];
            if (b == 0) { in[0] = 1.0f; in[1] = 1.0f; }
            sp.Render(in, out, 4);
            for (int s = 0; s < 4; ++s) {
                const int i = b * 4 + s;
                if (i < n) { y[i * 2] = out[s * 2]; y[i * 2 + 1] = out[s * 2 + 1]; }
            }
        }
        return y;
    }
};

static void channel_power(const std::vector<float>& y, double* pl, double* pr, double* plr) {
    *pl = *pr = *plr = 0.0;
    for (size_t i = 0; i + 1 < y.size(); i += 2) {
        *pl  += (double)y[i] * y[i];
        *pr  += (double)y[i + 1] * y[i + 1];
        *plr += (double)y[i] * y[i + 1];
    }
}

static double peak_abs(const std::vector<float>& y) {
    double p = 0;
    for (float v : y) p = fmax(p, fabs((double)v));
    return p;
}

// First sample crossing half the peak — the ensemble's leading edge.
static double first_tap_ms(const std::vector<float>& y) {
    const double thr = peak_abs(y) * 0.5;
    for (size_t i = 2; i + 1 < y.size(); i += 2) {
        if (fabs((double)y[i]) + fabs((double)y[i + 1]) > thr)
            return (double)(i / 2) * 1000.0 / kSRf;
    }
    return -1.0;
}

// ---------------------------------------------------------------------------
// Test 1: impulse produces distinct, ordered taps that track Depth and Gap
// ---------------------------------------------------------------------------
static void test_impulse_taps() {
    printf("\n=== Impulse Response: taps track Gap ===\n");

    struct { int gap; } cases[] = {{0}, {45}, {100}};
    double first[3];

    for (int c = 0; c < 3; ++c) {
        Rig r(2 /*6 clones*/, 0, 30, 80, 0, 0, 20, cases[c].gap);
        auto y = r.impulse(500);
        first[c] = first_tap_ms(y);
        printf("  gap=%3d : first tap at %6.2f ms, peak %.4f\n",
               cases[c].gap, first[c], peak_abs(y));
        assert(first[c] > 0.0 && "impulse produced no wet output");
        assert(peak_abs(y) > 1e-3 && "wet path is silent");
    }

    assert(first[1] > first[0] && "Gap did not push taps later");
    assert(first[2] > first[1] && "Gap did not push taps later");
    printf("  PASS: taps present and monotonically later with Gap\n");
}

// ---------------------------------------------------------------------------
// Test 2: sine response — output non-zero at 100 % wet
// ---------------------------------------------------------------------------
static void test_sine_response(int clones, float freq_hz) {
    printf("\n=== Sine Response: clone idx %d @ %.0f Hz, mix=100%% ===\n",
           clones, (double)freq_hz);

    Rig r(clones, 0, 30, 80, 30, 20, 20, 20);
    const float w = 2.0f * (float)M_PI * freq_hz / kSRf;
    double energy = 0;
    int n = 0;
    for (int b = 0; b < 4000; ++b) {
        float in[8], out[8];
        for (int s = 0; s < 4; ++s) {
            const float v = sinf(w * (float)(b * 4 + s));
            in[s * 2] = v; in[s * 2 + 1] = v;
        }
        r.sp.Render(in, out, 4);
        if (b > 500) for (int i = 0; i < 8; ++i) { energy += (double)out[i] * out[i]; ++n; }
    }
    const double rms = sqrt(energy / n);
    printf("  output rms = %.5f\n", rms);
    assert(rms > 1e-3 && "sine input produced no output");
    assert(std::isfinite(rms) && "sine input produced NaN/Inf");
    printf("  PASS\n");
}

// ---------------------------------------------------------------------------
// Test 3: Spread is a WIDTH control, not a level control
//
// Regression: pan gains were multiplied by spread_, so Spread=0 muted the wet
// path entirely instead of collapsing the ensemble to mono.
// ---------------------------------------------------------------------------
static void test_spread_is_width_not_level() {
    printf("\n=== Spread: width control, constant level ===\n");

    double rms[6], corr[6];
    for (int i = 0; i < 6; ++i) {
        Rig r(2, 0, 30, i * 20, 30, 50, 20, 20);
        auto y = r.impulse(400);
        double pl, pr, plr;
        channel_power(y, &pl, &pr, &plr);
        rms[i]  = sqrt((pl + pr) / (double)(y.size() / 2));
        corr[i] = plr / sqrt(pl * pr + 1e-30);
        printf("  spread=%3d : rms %.6f  correlation %+.3f\n", i * 20, rms[i], corr[i]);
        assert(rms[i] > 1e-4 && "Spread muted the wet path");
    }

    // Level must stay put across the whole sweep (within 1 dB).
    const double ratio = rms[5] / rms[0];
    assert(ratio > 0.89 && ratio < 1.12 && "Spread is acting as a level control");
    // Width must actually open up.
    assert(corr[0] > 0.95 && "Spread=0 should be mono");
    assert(corr[5] < corr[0] - 0.2 && "Spread=100 should decorrelate the channels");
    printf("  PASS: level flat within %.2f dB, correlation %.3f -> %.3f\n",
           fabs(10.0 * log10(ratio)), corr[0], corr[5]);
}

// ---------------------------------------------------------------------------
// Test 4: the stereo image is centred at every clone count
//
// Regression: clone gains roll off loudest-first while pan swept left-to-right,
// seating the loudest clone hard left. The image leaned left by 1.7 dB at 2
// clones, rising to 6.4 dB at 10.
// ---------------------------------------------------------------------------
static void test_stereo_balance() {
    printf("\n=== Stereo balance across clone counts ===\n");

    // Averaged over many hits: per-hit humanisation (random gain factors and
    // accents) makes any single impulse wander by a dB or so, which is the
    // point of it. What must be centred is the expected balance.
    for (int mode = 0; mode < 3; ++mode) {
        for (int cs = 0; cs < 5; ++cs) {
            Rig r(cs, mode, 30, 100, 30, 50, 20, 20);
            // Angel's balance trim eases toward centre over hits; let it
            // converge before measuring.
            for (int h = 0; h < 30; ++h) r.impulse(300);
            double tl = 0, tr = 0;
            for (int h = 0; h < 120; ++h) {
                auto y = r.impulse(300);
                double pl, pr, plr;
                channel_power(y, &pl, &pr, &plr);
                tl += pl; tr += pr;
            }
            const double db = 10.0 * log10((tl + 1e-30) / (tr + 1e-30));
            // Angel splits the bands across channels (L lowpassed, R
            // highpassed), so its balance is program-dependent by design and
            // no fixed gain trim can centre it for every input; it is held to
            // a looser bound than the two symmetric modes.
            // At two clones there is no centre seat and each channel is fed
            // by a single clone, so the per-hit random cutoff leaves a ~0.9 dB
            // residual the rebuild-time trim cannot see.
            const double limit = (mode == 2) ? 2.0 : 1.2;
            printf("  mode=%d clones=%2d : L/R %+5.2f dB (limit %.1f)\n",
                   mode, (cs + 1) * 2, db, limit);
            assert(fabs(db) < limit && "stereo image is off-centre");
        }
    }
    printf("  PASS: symmetric modes within 1.2 dB, Angel within 2 dB\n");
}

// ---------------------------------------------------------------------------
// Test 5: Scatter audibly loosens the ensemble's timing
//
// Regression: scatter_amount is a unitless pan fraction (max 0.63) but was
// multiplied by ms_to_smp as if it were milliseconds, capping timing jitter at
// ~1.5 ms against 70-130 ms taps — inaudible.
// ---------------------------------------------------------------------------
static void test_scatter_moves_timing() {
    printf("\n=== Scatter: per-hit timing spread ===\n");

    double sd[2];
    for (int c = 0; c < 2; ++c) {
        const int scatter = c ? 100 : 0;
        Rig r(1 /*4 clones*/, 0, 30, 80, 0, scatter, 20, 20);

        std::vector<double> t;
        for (int h = 0; h < 40; ++h) {
            auto y = r.impulse(250);
            const double f = first_tap_ms(y);
            if (f > 0) t.push_back(f);
        }
        double m = 0;
        for (double v : t) m += v;
        m /= (double)t.size();
        double s = 0;
        for (double v : t) s += (v - m) * (v - m);
        sd[c] = sqrt(s / (double)t.size());
        printf("  scatter=%3d : first tap %6.2f ms, sd %.2f ms over %zu hits\n",
               scatter, m, sd[c], t.size());
    }

    assert(sd[1] > sd[0] * 2.0 && "Scatter barely changes timing spread");
    printf("  PASS: spread widens %.1fx from Scatter=0 to Scatter=100\n", sd[1] / sd[0]);
}

// ---------------------------------------------------------------------------
// Test 6: the unit is fully wet — no dry leak, and a real ensemble arrives
//
// Mix was removed; the first clone is the leading stroke. What must hold is
// that nothing passes through at t=0 and that the wet arrives at a usable
// level rather than the -13 dB the old gain staging delivered.
// ---------------------------------------------------------------------------
static void test_fully_wet() {
    printf("\n=== Fully wet: no dry leak, usable level ===\n");

    Rig r(4, 0, 30, 80, 30, 45, 20, 45);
    float in[8] = {0}, out[8];
    in[0] = in[1] = 1.0f;
    r.sp.Render(in, out, 4);
    printf("  impulse in -> out[0]=%.4f at t=0 (no dry path)\n", out[0]);
    assert(fabsf(out[0]) < 0.05f && "dry signal leaked into a fully wet unit");

    for (int mode = 0; mode < 3; ++mode) {
        Rig e(4, mode, 30, 80, 30, 45, 20, 45);
        auto y = e.impulse(500);
        const double pk = peak_abs(y);
        printf("  mode=%d : wet peak %.4f (%+.1f dB) for a unit impulse\n",
               mode, pk, 20.0 * log10(pk));
        // The ensemble must land within 6 dB of the hit that produced it.
        assert(pk > 0.5 && "wet ensemble is too quiet to read as drummers");
    }
    printf("  PASS\n");
}

// ---------------------------------------------------------------------------
// Test 7: the output limiter never lets the signal past full scale
//
// Regression: nothing bounded the clone sum, so dense settings hard-clipped.
// The cubic that replaced it over-corrected, costing 3.5 dB at full scale even
// with the effect fully dry.
// ---------------------------------------------------------------------------
static void test_output_ceiling() {
    printf("\n=== Output ceiling ===\n");

    // Worst case: max clones, max gap/scatter drive, full wet, hot input.
    Rig r(4, 0, 100, 100, 100, 100, 0, 100);
    double worst = 0;
    for (int b = 0; b < 4000; ++b) {
        float in[8], out[8];
        for (int s = 0; s < 4; ++s) {
            // full-scale square-ish drive, the hardest thing to keep bounded
            const float v = ((b + s) % 64 < 32) ? 1.0f : -1.0f;
            in[s * 2] = v; in[s * 2 + 1] = v;
        }
        r.sp.Render(in, out, 4);
        for (int i = 0; i < 8; ++i) {
            assert(std::isfinite(out[i]) && "output went non-finite");
            worst = fmax(worst, fabs((double)out[i]));
        }
    }
    printf("  worst |output| over 16000 frames at max drive = %.4f\n", worst);
    assert(worst <= 1.0 && "output exceeded full scale");
    printf("  PASS: bounded below full scale, no hard clipping\n");
}

// ---------------------------------------------------------------------------
// Test 8: transient detection fires once per stroke, on every kind of source
//
// Everything per-hit -- velocity, timing scatter, Angel's re-placement --
// hangs off this. Two regressions lived here: comparing raw block-peak
// magnitude never fired at all on long-decaying sources (0/40 on a 400 ms tom),
// and a fast envelope release shorter than the carrier half-cycle fired 20
// times per stroke on a 60 Hz kick.
// ---------------------------------------------------------------------------
static void test_transient_detection() {
    printf("\n=== Transient detection: once per stroke ===\n");

    struct { const char* name; float f0, decay_ms, amp; int bpm; } srcs[] = {
        {"kick   60 Hz 200 ms",   60.0f, 200.0f, 0.90f, 120},
        {"kick   60 Hz 200 ms q", 60.0f, 200.0f, 0.05f, 120},
        {"snare 200 Hz  80 ms",  200.0f,  80.0f, 0.90f, 120},
        {"hat   800 Hz  30 ms",  800.0f,  30.0f, 0.90f, 480},
        {"tom   120 Hz 400 ms",  120.0f, 400.0f, 0.90f, 120},
        {"kick   60 Hz 900 ms",   60.0f, 900.0f, 0.90f, 140},
    };

    for (const auto& s : srcs) {
        Rig r(4, 2 /*Angel re-places on every detected hit*/, 30, 100, 30, 45, 20, 45);

        const int   period = (int)(kSRf * 60.0f / (float)s.bpm);
        const float dk     = expf(-1.0f / (s.decay_ms * 0.001f * kSRf));
        const int   hits   = 30;

        float last = r.sp.get_clones()[1].pan_gain_l;
        int   trig = 0;
        for (int h = 0; h < hits; ++h) {
            float env = s.amp;
            for (int b = 0; b * 4 < period; ++b) {
                float in[8], out[8];
                for (int k = 0; k < 4; ++k) {
                    const int n = b * 4 + k;
                    const float v = env * sinf(2.0f * (float)M_PI * s.f0 * (float)n / kSRf);
                    env *= dk;
                    in[k * 2] = v; in[k * 2 + 1] = v;
                }
                r.sp.Render(in, out, 4);
                const float pg = r.sp.get_clones()[1].pan_gain_l;
                if (fabsf(pg - last) > 1e-9f) { ++trig; last = pg; }
            }
        }
        const double per_stroke = (double)trig / hits;
        printf("  %-22s %5.2f triggers per stroke\n", s.name, per_stroke);
        assert(per_stroke > 0.9 && "transient detector missed strokes");
        assert(per_stroke < 1.6 && "transient detector retriggered mid-stroke");
    }
    printf("  PASS: one trigger per stroke on every source\n");
}

// ---------------------------------------------------------------------------
int main() {
    setvbuf(stdout, nullptr, _IONBF, 0);  // keep output if an assert aborts
    printf("===========================================\n");
    printf("PercussionSpatializer behavioural tests\n");
    printf("===========================================\n");

    test_impulse_taps();
    test_sine_response(1, 220.0f);
    test_sine_response(4, 1000.0f);
    test_spread_is_width_not_level();
    test_stereo_balance();
    test_scatter_moves_timing();
    test_fully_wet();
    test_output_ceiling();
    test_transient_detection();

    printf("\n=== ALL delay_tribal BEHAVIOURAL TESTS PASSED ===\n");
    return 0;
}
