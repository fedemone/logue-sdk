/**
 * @file test_levels.cpp
 * @brief Output-level and distortion bench for OmniPress.
 *
 * Unlike test_compressor.cpp, this does NOT re-implement the DSP in scalar
 * form — it includes masterfx.h and drives the real MasterFX::Process() loop.
 * ARM NEON intrinsics are supplied by test_portable/arm_neon.h, a portable
 * stand-in that is semantically identical except that vrecpeq_f32/vrsqrteq_f32
 * are exact instead of 8-bit estimates (every call site in OmniPress follows
 * them with a Newton-Raphson step, so the difference is below -90 dBFS).
 *
 * Compile:
 *   g++ -std=c++14 -O2 -I test_portable -I . -I ../common \
 *       -o test_levels test_levels.cpp -lm
 * Run:
 *   ./test_levels            # everything
 *   ./test_levels gain       # one section: gain|default|matched|mb|drive|kick|release|quirks|slam|recover|panel
 *
 * Sections R (recover) and M (panel) are pass/fail checks, and the exit status
 * is non-zero if any of their rows fail; every other section is a measurement
 * to read.  The Distressor is off the panel but not out of the source: the
 * sections that measure it select it through MasterFX::setEngineMode().
 */

#include <cstdio>
#include <cstring>
#include <cmath>
#include <string>
#include <vector>
#include <ctime>

// Section R corrupts the DSP state directly to prove the watchdog clears it,
// which needs the private members. Opening the class up is contained to this
// translation unit; nothing else here touches them.
#define private public
#include "masterfx.h"
#undef private

/* =========================================================================
 * Host emulation
 * ========================================================================= */

static MasterFX g_fx;          /* static storage => zero-init, like s_fx_instance */

static const int    SR    = 48000;
static const size_t BLOCK = 64;
static const int    NCH   = 4; /* drumlogue master FX input: L R SC-L SC-R */

/* v[] is the panel.  mode and dist are what the panel no longer reaches: the
 * engine (0 = Standard, 1 = Distressor, 2 = Multiband, as CompMode numbers
 * them) and the Distressor's distortion type.  The bench still drives the
 * Distressor through MasterFX::setEngineMode()/setDistressorDistortion() so
 * that its code keeps being measured while it is off the panel. */
struct Params { int32_t v[k_num_params]; int mode; int dist; };

/* The .init column of unit_header in header.c */
static Params headerDefaults() {
    Params p{};
    p.v[k_threhold]            = -200;   /* -20.0 dB */
    p.v[k_slope]               = 40;
    p.v[k_attack]              = 150;    /* 15.0 ms */
    p.v[k_release]             = 200;    /* 200 ms  */
    p.v[k_makeup]              = 0;
    p.v[k_drive]               = 0;
    p.v[k_mix]                 = 100;    /* WET */
    p.v[k_sc_hpf]              = 20;
    p.mode = 0;
    p.v[k_attenuation_limit]   = -200;   /* -20.0 dB */
    p.v[k_gain_limit]          = 60;     /* +6.0 dB */
    p.v[k_detection_mode]      = 0;
    p.v[k_bass]                = 50;
    p.v[k_treble]              = 50;
    p.v[k_presence]            = 50;
    p.v[k_band_solo_mute]      = 0;      /* Off */
    p.v[k_band_low_threshold]  = -200;
    p.v[k_band_mid_threshold]  = -200;
    p.v[k_band_high_threshold] = -200;
    p.v[k_crossover_low]       = 50;     /* 250 Hz */
    p.v[k_band_low_ratio]      = 40;     /* 4.0:1 */
    p.v[k_band_mid_ratio]      = 40;
    p.v[k_band_high_ratio]     = 40;
    p.v[k_crossover_high]      = 33;     /* 2.5 kHz */
    p.mode = 0;
    p.dist = 0;
    return p;
}

/* The three per-band knobs of one kind at once */
static void setBandThresholds(Params& p, int32_t raw) {
    p.v[k_band_low_threshold] = p.v[k_band_mid_threshold] = p.v[k_band_high_threshold] = raw;
}
static void setBandRatios(Params& p, int32_t raw) {
    p.v[k_band_low_ratio] = p.v[k_band_mid_ratio] = p.v[k_band_high_ratio] = raw;
}

/* Select the engine p.mode asks for, the way the unit would see it: the
 * panel's COMP MODE for Standard and Multiband, the engine API for the
 * Distressor. */
static void selectEngine(const Params& p) {
    if (p.mode == COMP_MODE_DISTRESSOR) {
        g_fx.setEngineMode(COMP_MODE_DISTRESSOR);
        g_fx.setDistressorDistortion(p.dist);
    } else {
        g_fx.setParameter(k_compressor_mode, p.mode == COMP_MODE_MULTIBAND ? 1 : 0);
    }
}

/* Engine first and SLOPE last: setParameter(k_slope) branches on the engine. */
static void apply(const Params& p) {
    g_fx.Reset();
    selectEngine(p);
    for (int i = 0; i < k_num_params; ++i)
        if (i != k_compressor_mode && i != k_slope)
            g_fx.setParameter(i, p.v[i]);
    g_fx.setParameter(k_slope, p.v[k_slope]);
}

/* How a host replays a stored preset: strictly by parameter ID.  For the
 * Distressor the engine is selected where COMP MODE (ID 8) sits, and the
 * distortion type where DstrDist (ID 15) used to, so the order the drive
 * stage is armed in is the one a stored program would have used. */
static void applyIdOrder(const Params& p) {
    g_fx.Reset();
    for (int i = 0; i < k_num_params; ++i) {
        if (i == k_compressor_mode && p.mode == COMP_MODE_DISTRESSOR) {
            g_fx.setEngineMode(COMP_MODE_DISTRESSOR);
            continue;
        }
        if (i == k_compressor_mode) {
            g_fx.setParameter(i, p.mode == COMP_MODE_MULTIBAND ? 1 : 0);
            continue;
        }
        if (i == k_band_solo_mute && p.mode == COMP_MODE_DISTRESSOR)
            g_fx.setDistressorDistortion(p.dist);
        g_fx.setParameter(i, p.v[i]);
    }
}

/* SLOPE raw value that selects distressor ratio index 0..7 */
static int32_t distressorSlopeRaw(int idx) {
    for (int32_t v = 1; v <= 100; ++v)
        if ((int)(v * 0.0799f) == idx) return v;
    return 1;
}

/* =========================================================================
 * Measurement
 * ========================================================================= */

struct Result {
    double rms_dbfs;      /* output broadband RMS                            */
    double peak;          /* output peak |x|                                 */
    double gain_fund_db;  /* 20log10(A1_out / A1_in)                         */
    double gain_rms_db;   /* broadband RMS out vs in                         */
    double phase_deg;     /* fundamental phase; +/-180 = polarity inverted   */
    double thd_pct;       /* harmonics 2..24 relative to fundamental         */
    double nonfund_pct;   /* all non-fundamental energy incl. sub-harmonics  */
    double sub_db;        /* f0/2 component relative to fundamental          */
};

/* pan: 0 = mono (L=R), 1 = left only. f0 must divide evenly into the window. */
static Result measure(double amp, double f0, int settle, int meas, int pan = 0) {
    std::vector<float> in(BLOCK * NCH), out(BLOCK * 2);
    const double w = 2.0 * M_PI * f0 / SR;
    long n = 0;

    for (int done = 0; done < settle; done += (int)BLOCK) {
        for (size_t i = 0; i < BLOCK; ++i, ++n) {
            float s = (float)(amp * sin(w * n)), r = pan ? 0.0f : s;
            in[i*NCH+0] = s; in[i*NCH+1] = r; in[i*NCH+2] = s; in[i*NCH+3] = r;
        }
        g_fx.Process(in.data(), out.data(), BLOCK);
    }

    const int NH = 25;
    double re[NH] = {0}, im[NH] = {0}, sre = 0, sim = 0;
    double sumsq = 0, peak = 0;
    long N = 0;

    for (int done = 0; done < meas; done += (int)BLOCK) {
        long n0 = n;
        for (size_t i = 0; i < BLOCK; ++i, ++n) {
            float s = (float)(amp * sin(w * n)), r = pan ? 0.0f : s;
            in[i*NCH+0] = s; in[i*NCH+1] = r; in[i*NCH+2] = s; in[i*NCH+3] = r;
        }
        g_fx.Process(in.data(), out.data(), BLOCK);
        for (size_t i = 0; i < BLOCK; ++i) {
            double x = out[i*2], t = (double)(n0 + (long)i);
            sumsq += x * x;
            if (fabs(x) > peak) peak = fabs(x);
            for (int k = 1; k < NH; ++k) { double a = w*k*t; re[k] += x*cos(a); im[k] += x*sin(a); }
            double a2 = 0.5 * w * t;
            sre += x * cos(a2); sim += x * sin(a2);
            N++;
        }
    }

    if (!N) return Result{};   /* settle-only call: nothing to analyse */

    double A[NH];
    for (int k = 1; k < NH; ++k) A[k] = 2.0 * sqrt(re[k]*re[k] + im[k]*im[k]) / N;
    double Asub = 2.0 * sqrt(sre*sre + sim*sim) / N;

    double harm2 = 0;
    for (int k = 2; k < NH; ++k) harm2 += A[k] * A[k];

    double meansq  = sumsq / N;
    double nonfund = 2.0 * meansq - A[1]*A[1];
    if (nonfund < 0) nonfund = 0;

    Result r{};
    r.rms_dbfs     = 20.0 * log10(sqrt(meansq) + 1e-30);
    r.peak         = peak;
    r.gain_fund_db = 20.0 * log10((A[1] + 1e-30) / amp);
    r.gain_rms_db  = 20.0 * log10((sqrt(meansq) + 1e-30) / (amp / sqrt(2.0)));
    r.phase_deg    = atan2(re[1], im[1]) * 180.0 / M_PI;
    r.thd_pct      = 100.0 * sqrt(harm2) / (A[1] + 1e-30);
    r.nonfund_pct  = 100.0 * sqrt(nonfund) / (A[1] + 1e-30);
    r.sub_db       = 20.0 * log10((Asub + 1e-30) / (A[1] + 1e-30));
    return r;
}

/* Mean (DC) and peak of the output for a steady sine input. */
static void measureDC(double amp, double f0, int settle, int meas,
                      double* mean_out, double* peak_out) {
    std::vector<float> in(BLOCK * NCH), out(BLOCK * 2);
    const double w = 2.0 * M_PI * f0 / SR;
    long n = 0;
    double sum = 0, peak = 0;
    long N = 0;

    for (int phase = 0; phase < 2; ++phase) {
        const int frames = phase ? meas : settle;
        for (int done = 0; done < frames; done += (int)BLOCK) {
            for (size_t i = 0; i < BLOCK; ++i, ++n) {
                float v = (float)(amp * sin(w * n));
                in[i*NCH+0] = v; in[i*NCH+1] = v;
                in[i*NCH+2] = v; in[i*NCH+3] = v;
            }
            g_fx.Process(in.data(), out.data(), BLOCK);
            if (phase) {
                for (size_t i = 0; i < BLOCK; ++i) {
                    double x = out[i*2];
                    sum += x;
                    if (fabs(x) > peak) peak = fabs(x);
                    N++;
                }
            }
        }
    }
    *mean_out = N ? sum / N : 0.0;
    *peak_out = peak;
}

/* Main bus and sidechain input at independent levels; returns the gain applied
 * to the main bus at the fundamental. */
static double measureKeyed(double amp, double key_amp, double f0,
                           int settle, int meas) {
    std::vector<float> in(BLOCK * NCH), out(BLOCK * 2);
    const double w = 2.0 * M_PI * f0 / SR;
    long n = 0;
    double re = 0, im = 0;
    long N = 0;

    for (int phase = 0; phase < 2; ++phase) {
        const int frames = phase ? meas : settle;
        for (int done = 0; done < frames; done += (int)BLOCK) {
            long n0 = n;
            for (size_t i = 0; i < BLOCK; ++i, ++n) {
                float m = (float)(amp * sin(w * n));
                float k = (float)(key_amp * sin(w * n));
                in[i*NCH+0] = m; in[i*NCH+1] = m;
                in[i*NCH+2] = k; in[i*NCH+3] = k;
            }
            g_fx.Process(in.data(), out.data(), BLOCK);
            if (phase) {
                for (size_t i = 0; i < BLOCK; ++i) {
                    double x = out[i*2], t = (double)(n0 + (long)i);
                    re += x * cos(w * t); im += x * sin(w * t); N++;
                }
            }
        }
    }
    if (!N) return 0.0;
    double A = 2.0 * sqrt(re*re + im*im) / N;
    return 20.0 * log10((A + 1e-30) / amp);
}

static const char* DIST_NAME[9] = {
    "Off", "Dist2", "Dist3", "Both", "Soft", "Hard", "Trg", "Sine", "SubOct"
};
static const char* MODE_NAME[3] = { "Standard", "Distressor", "Multiband" };

static const double F0     = 1000.0;
static const int    SETTLE = 48000;   /* 1 s     */
static const int    MEAS   = 24000;   /* 0.5 s   */
static const int    QSETTLE= 24000;   /* short settle for the sweeps */
static const int    QMEAS  = 9600;

static void hdr(const char* t) {
    printf("\n===========================================================================\n");
    printf("%s\n", t);
    printf("===========================================================================\n");
}

/* =========================================================================
 * Sections
 * ========================================================================= */

/* A. Static insertion gain with every gain computer forced to 0 dB GR. */
static void section_gain() {
    hdr("A. STATIC INSERTION GAIN (0 dB gain reduction, DRIVE 0, MAKEUP 0, MIX WET)\n"
        "   Expected: 0.00 dB for all three modes.");
    printf("%-12s %-9s %10s %10s %10s %8s\n",
           "mode", "in dBFS", "gain(1k)", "gain RMS", "out dBFS", "peak");
    for (double amp : {0.01, 0.1, 0.5}) {
        for (int mode = 0; mode < 3; ++mode) {
            Params p = headerDefaults();
            p.mode = mode;
            p.v[k_attenuation_limit] = 0;
            p.v[k_gain_limit]        = 0;
            if (mode == 1) p.v[k_slope] = distressorSlopeRaw(0);
            if (mode == 2) {
                setBandThresholds(p, 0);
                setBandRatios(p, 10);
            }
            apply(p);
            Result r = measure(amp, F0, SETTLE, MEAS);
            printf("%-12s %-9.1f %+10.2f %+10.2f %+10.2f %8.3f\n",
                   MODE_NAME[mode], 20*log10(amp), r.gain_fund_db, r.gain_rms_db,
                   r.rms_dbfs, r.peak);
        }
        printf("\n");
    }
}

/* B. Factory defaults exactly as header.c ships them. */
static void section_default() {
    hdr("B. FACTORY DEFAULTS (THRESH -20, SLOPE 40, ATT LMT -20 dB, GAIN LMT +6 dB)");
    printf("%-12s %-9s %10s %10s %10s %8s\n",
           "mode", "in dBFS", "gain(1k)", "gain RMS", "out dBFS", "THD%");
    for (double amp : {0.01, 0.1, 0.5}) {
        for (int mode = 0; mode < 3; ++mode) {
            Params p = headerDefaults();
            p.mode = mode;
            apply(p);
            Result r = measure(amp, F0, SETTLE, MEAS);
            printf("%-12s %-9.1f %+10.2f %+10.2f %+10.2f %8.3f\n",
                   MODE_NAME[mode], 20*log10(amp), r.gain_fund_db, r.gain_rms_db,
                   r.rms_dbfs, r.thd_pct);
        }
        printf("\n");
    }
}

/* C. Same knob positions in all three modes. */
static void section_matched() {
    hdr("C. MATCHED KNOBS: threshold -30 dB, ~4:1, limits +/-30 dB, in -6 dBFS");
    printf("%-12s %-12s %10s %10s %10s\n",
           "mode", "ratio knob", "gain(1k)", "gain RMS", "out dBFS");

    Params p = headerDefaults();
    p.v[k_threhold] = -300; p.v[k_attenuation_limit] = -300; p.v[k_gain_limit] = 300;
    p.mode = 0; p.v[k_slope] = 58;
    apply(p);
    Result r = measure(0.5, F0, SETTLE, MEAS);
    printf("%-12s %-12s %+10.2f %+10.2f %+10.2f\n",
           MODE_NAME[0], "SLOPE 58", r.gain_fund_db, r.gain_rms_db, r.rms_dbfs);

    p = headerDefaults();
    p.v[k_threhold] = -300; p.mode = 1;
    p.v[k_slope] = distressorSlopeRaw(3);
    apply(p);
    r = measure(0.5, F0, SETTLE, MEAS);
    printf("%-12s %-12s %+10.2f %+10.2f %+10.2f\n",
           MODE_NAME[1], "4:1", r.gain_fund_db, r.gain_rms_db, r.rms_dbfs);

    p = headerDefaults();
    p.mode = 2;
    setBandThresholds(p, -300);
    setBandRatios(p, 40);
    apply(p);
    r = measure(0.5, F0, SETTLE, MEAS);
    printf("%-12s %-12s %+10.2f %+10.2f %+10.2f\n",
           MODE_NAME[2], "4.0:1", r.gain_fund_db, r.gain_rms_db, r.rms_dbfs);
}

/* E. Multiband summing loss and its frequency dependence. */
static void section_mb() {
    hdr("E. MULTIBAND SUMMING LOSS");
    printf("  makeup needed for unity (all bands, no GR):\n");
    printf("  %-10s %10s %10s\n", "MAKEUP dB", "gain(1k)", "out dBFS");
    for (int mk : {0, 30, 60, 69, 75, 90}) {
        Params p = headerDefaults();
        p.mode = 2;
        setBandThresholds(p, 0);
        setBandRatios(p, 10);
        p.v[k_makeup] = mk;
        apply(p);
        Result r = measure(0.1, F0, SETTLE, MEAS);
        printf("  %-10.1f %+10.2f %+10.2f\n", mk * 0.1, r.gain_fund_db, r.rms_dbfs);
    }
    printf("\n  crossover-tree flatness (no GR, makeup 0):\n");
    printf("  %-10s %10s %10s\n", "tone Hz", "gain(1k)", "phase deg");
    for (double f : {100.0, 250.0, 600.0, 1000.0, 2500.0, 6000.0}) {
        Params p = headerDefaults();
        p.mode = 2;
        setBandThresholds(p, 0);
        setBandRatios(p, 10);
        apply(p);
        Result r = measure(0.1, f, SETTLE, MEAS);
        printf("  %-10.0f %+10.2f %+10.1f\n", f, r.gain_fund_db, r.phase_deg);
    }
}

/* D. Level and THD versus DRIVE for every distortion type. */
static void section_drive() {
    for (double amp : {0.1, 0.5}) {
        char title[256];
        snprintf(title, sizeof(title),
                 "D. LEVEL AND THD vs DRIVE (Distressor, ratio 1:1 so the compressor is\n"
                 "   out of the picture; input %.0f dBFS 1 kHz).  DstrDist 0 (Off) is the\n"
                 "   Overlord tube fall-through, not a bypass — compare with D2.",
                 20*log10(amp));
        hdr(title);
        for (int dist = 0; dist <= 8; ++dist) {
            printf("\n  DstrDist %d (%s)\n", dist, DIST_NAME[dist]);
            printf("  %6s %10s %10s %8s %8s %9s %7s\n",
                   "DRIVE", "gain(1k)", "out dBFS", "THD%", "nonfnd%", "sub f/2", "peak");
            for (int drive : {0,1,2,3,5,8,10,15,20,30,50,70,100}) {
                Params p = headerDefaults();
                p.mode = 1;
                p.v[k_slope]           = distressorSlopeRaw(0);
                p.v[k_threhold]        = 0;
                p.dist = dist;
                p.v[k_drive]           = drive;
                apply(p);
                Result r = measure(amp, F0, SETTLE, MEAS);
                printf("  %6d %+10.2f %+10.2f %8.2f %8.2f %9.1f %7.3f\n",
                       drive, r.gain_fund_db, r.rms_dbfs, r.thd_pct,
                       r.nonfund_pct, r.sub_db, r.peak);
            }
        }
    }

    hdr("D2. OVERLORD TUBE DRIVE, measured in Standard mode.  The DstrDist 0\n"
        "    (Off) block above must match this row for row: with no shaper\n"
        "    selected the Distressor falls through to the same broadband tube,\n"
        "    so DRIVE is never a dead knob.  Multiband is NOT this stage — it\n"
        "    saturates inside each band instead.");
    for (double amp : {0.1, 0.5}) {
        printf("\n  input %.0f dBFS\n", 20*log10(amp));
        printf("  %6s %10s %10s %8s %7s\n", "DRIVE", "gain(1k)", "out dBFS", "THD%", "peak");
        for (int drive : {0,1,2,3,5,8,10,15,20,30,50,70,100}) {
            Params p = headerDefaults();
            p.mode = 0;
            p.v[k_attenuation_limit] = 0;
            p.v[k_gain_limit]        = 0;
            p.v[k_drive]             = drive;
            apply(p);
            Result r = measure(amp, F0, SETTLE, MEAS);
            printf("  %6d %+10.2f %+10.2f %8.2f %7.3f\n",
                   drive, r.gain_fund_db, r.rms_dbfs, r.thd_pct, r.peak);
        }
    }
}

/* F. First DRIVE value at which each distortion type becomes measurable. */
static void section_kick() {
    for (double amp : {0.1, 0.5}) {
        char title[200];
        snprintf(title, sizeof(title),
                 "F. DISTORTION ONSET vs DRIVE (input %.0f dBFS 1 kHz, Distressor 1:1)",
                 20*log10(amp));
        hdr(title);
        printf("%-8s %9s %8s | %8s %8s %8s %8s | %10s\n",
               "type","gain@0","THD@0","THD 1%","THD 5%","THD 10%","clip","gain@100");
        for (int dist = 0; dist <= 8; ++dist) {
            int d1=-1,d5=-1,d10=-1,dc=-1; double g0=0,t0=0,g100=0;
            for (int drv = 0; drv <= 100; ++drv) {
                Params p = headerDefaults();
                p.mode = 1;
                p.v[k_slope]           = distressorSlopeRaw(0);
                p.v[k_threhold]        = 0;
                p.dist = dist;
                p.v[k_drive]           = drv;
                apply(p);
                Result m = measure(amp, F0, QSETTLE, QMEAS);
                if (drv == 0)   { g0 = m.gain_fund_db; t0 = m.thd_pct; }
                if (drv == 100) { g100 = m.gain_fund_db; }
                if (d1  < 0 && m.thd_pct >=  1.0) d1  = drv;
                if (d5  < 0 && m.thd_pct >=  5.0) d5  = drv;
                if (d10 < 0 && m.thd_pct >= 10.0) d10 = drv;
                if (dc  < 0 && m.peak    >= 0.999) dc = drv;
            }
            char b1[8],b5[8],b10[8],bc[8];
            snprintf(b1,8,  d1 <0?"none":"%d", d1);
            snprintf(b5,8,  d5 <0?"none":"%d", d5);
            snprintf(b10,8, d10<0?"none":"%d", d10);
            snprintf(bc,8,  dc <0?"none":"%d", dc);
            printf("%-8s %+9.2f %8.2f | %8s %8s %8s %8s | %+10.2f\n",
                   DIST_NAME[dist], g0, t0, b1, b5, b10, bc, g100);
        }
    }

    hdr("F2. OVERLORD TUBE DRIVE ONSET (Standard mode, no GR)");
    printf("%-9s %9s %8s | %8s %8s %8s | %10s\n",
           "in dBFS","gain@0","THD@0","THD 1%","THD 5%","THD 10%","gain@100");
    for (double amp : {0.05, 0.1, 0.5}) {
        int d1=-1,d5=-1,d10=-1; double g0=0,t0=0,g100=0;
        for (int drv = 0; drv <= 100; ++drv) {
            Params p = headerDefaults();
            p.mode = 0;
            p.v[k_attenuation_limit] = 0;
            p.v[k_gain_limit]        = 0;
            p.v[k_drive]             = drv;
            apply(p);
            Result m = measure(amp, F0, QSETTLE, QMEAS);
            if (drv == 0)   { g0 = m.gain_fund_db; t0 = m.thd_pct; }
            if (drv == 100) { g100 = m.gain_fund_db; }
            if (d1  < 0 && m.thd_pct >=  1.0) d1  = drv;
            if (d5  < 0 && m.thd_pct >=  5.0) d5  = drv;
            if (d10 < 0 && m.thd_pct >= 10.0) d10 = drv;
        }
        char b1[8],b5[8],b10[8];
        snprintf(b1,8, d1 <0?"none":"%d", d1);
        snprintf(b5,8, d5 <0?"none":"%d", d5);
        snprintf(b10,8,d10<0?"none":"%d", d10);
        printf("%-9.0f %+9.2f %8.2f | %8s %8s %8s | %+10.2f\n",
               20*log10(amp), g0, t0, b1, b5, b10, g100);
    }
}

/* H. Transient behaviour. Steady tones cannot see a detector that fails to
 *    release, which is how a latching peak detector survived section A-G. */
static void section_release() {
    hdr("H1. RELEASE ON TRANSIENT MATERIAL\n"
        "    200 ms hit at -3 dBFS, then a -30 dBFS tail. Gain applied to the\n"
        "    tail, in 100 ms windows. The rows must differ with RELEASE.");

    struct Case { const char* name; int mode; int slope; int release; int detect; };
    const Case cases[] = {
        { "Standard Peak  REL=10",   0, 58,   10, 0 },
        { "Standard Peak  REL=200",  0, 58,  200, 0 },
        { "Standard Peak  REL=2000", 0, 58, 2000, 0 },
        { "Standard RMS   REL=200",  0, 58,  200, 1 },
        { "Standard Blend REL=200",  0, 58,  200, 2 },
        { "Distressor 4:1 REL=10",   1, 38,   10, 0 },
        { "Distressor 4:1 REL=2000", 1, 38, 2000, 0 },
        { "Distressor Opto REL=200", 1, 63,  200, 0 },
        { "Multiband      REL=200",  2, 40,  200, 0 },
        { "Multiband      REL=2000", 2, 40, 2000, 0 },
    };

    printf("  %-24s %s\n", "config", " 100ms   200    300    400    500    600    700    800");
    for (const Case& c : cases) {
        Params p = headerDefaults();
        p.mode = c.mode;
        p.v[k_slope]             = c.slope;
        p.v[k_release]           = c.release;
        p.v[k_detection_mode]    = c.detect;
        p.v[k_attenuation_limit] = -300;
        p.v[k_gain_limit]        = 300;
        if (c.mode == 2) {
            setBandThresholds(p, -200);
        }
        apply(p);
        measure(0.708, F0, 9600, 0);          /* the hit, no analysis */
        printf("  %-24s", c.name);
        for (int k = 0; k < 8; ++k)
            printf(" %6.1f", measure(0.0316, F0, 0, 4800).gain_fund_db);
        printf("\n");
    }

    hdr("H2. EXTERNAL SIDECHAIN (DETECT + 4)\n"
        "    Main -20 dBFS, key -3 dBFS, both 1 kHz. Internal detection must\n"
        "    ignore the key; external must duck. Multiband ducks only the band\n"
        "    the key occupies, so it ducks less than the broadband modes.");
    printf("  %-12s %14s %14s\n", "mode", "internal", "external");
    for (int mode = 0; mode < 3; ++mode) {
        Params p = headerDefaults();
        p.mode = mode;
        p.v[k_slope]             = (mode == 1) ? distressorSlopeRaw(3) : 58;
        p.v[k_attenuation_limit] = -300;
        p.v[k_gain_limit]        = 300;
        if (mode == 2) {
            setBandThresholds(p, -200);
        }
        p.v[k_detection_mode] = 0;
        apply(p);
        double internal = measureKeyed(0.1, 0.708, F0, SETTLE, MEAS);
        p.v[k_detection_mode] = 4;
        apply(p);
        double external = measureKeyed(0.1, 0.708, F0, SETTLE, MEAS);
        printf("  %-12s %+14.2f %+14.2f\n", MODE_NAME[mode], internal, external);
    }
}

/* G. Mechanisms behind the numbers above. */
static void section_quirks() {
    hdr("G1. MAKEUP GAIN ACCURACY, measured end to end through Process()\n"
        "    (the old fasterpowf path is shown for comparison)");
    printf("   %-9s %12s %14s %12s\n", "MAKEUP", "measured", "e_expff", "fasterpowf");
    for (int raw : {0, 30, 60, 120, 180, 240}) {
        Params p = headerDefaults();
        p.mode = 0;
        p.v[k_attenuation_limit] = 0;
        p.v[k_gain_limit]        = 0;
        p.v[k_makeup]            = raw;
        apply(p);
        float db = raw * 0.1f;
        printf("   %-9.1f %+12.3f %+14.3f %+12.3f\n", db,
               measure(0.01, F0, SETTLE, MEAS).gain_fund_db,
               20*log10(e_expff(db * INV_DB_COEFF)),
               20*log10(fasterpowf(10.0f, db * 0.05f)));
    }

    hdr("G1b. ENVELOPE-TO-dB ACCURACY (neon_log2q_f32 feeds every threshold\n"
        "     comparison; the error here lands directly on the threshold)");
    {
        double w_new = 0, w_old = 0, w_2nd = 0;
        for (double v = 1e-4; v < 1.0; v *= 1.0007) {
            float f = (float)v;
            float got[4];
            vst1q_f32(got, linear_to_db(vdupq_n_f32(f)));
            unsigned u; memcpy(&u, &f, 4);
            double ex = (double)((u >> 23) & 0xFF) - 127.0;
            double m  = (double)(u & 0x7FFFFF) / 8388608.0;
            double want = 20.0 * log10(v);
            w_new = fmax(w_new, fabs(got[0] - want));
            w_old = fmax(w_old, fabs((ex + m) * 6.0206 - want));
            w_2nd = fmax(w_2nd, fabs((ex + m*1.442695 - m*m*0.442695) * 6.0206 - want));
        }
        printf("   linear_to_db now (shared cubic) : %.4f dB worst case\n", w_new);
        printf("   previous mantissa interpolation : %.4f dB\n", w_old);
        printf("   previous multiband 2nd order    : %.4f dB\n", w_2nd);
    }

    hdr("G2. DETECTOR CALIBRATION: the feed is the mono average 0.5*(L+R), so a\n"
        "    mono source reads its true level.  Hard-panned content sits 6 dB\n"
        "    lower in a mono-sum detector by construction, which is why the two\n"
        "    columns still differ by 6 dB * slope.\n"
        "    threshold -30 dB, ~4:1, limits +/-30 dB, in -6 dBFS");
    for (int mode = 0; mode < 3; ++mode) {
        Params p = headerDefaults();
        p.mode = mode;
        p.v[k_threhold]          = -300;
        p.v[k_attenuation_limit] = -300;
        p.v[k_gain_limit]        = 300;
        p.v[k_slope]             = (mode == 1) ? distressorSlopeRaw(3) : 58;
        if (mode == 2) {
            setBandThresholds(p, -300);
        }
        apply(p); double mono = measure(0.5, F0, SETTLE, MEAS, 0).gain_fund_db;
        apply(p); double left = measure(0.5, F0, SETTLE, MEAS, 1).gain_fund_db;
        printf("   %-11s mono(L=R) %+7.2f dB   left-only %+7.2f dB   delta %+5.2f dB\n",
               MODE_NAME[mode], mono, left, mono - left);
    }

    hdr("G3. DRIVE IS LIVE FROM ITS FIRST STEP.  The old gate was 'drive_ > 0.01f',\n"
        "    so DRIVE=1 was identical to 0 and DRIVE=2 arrived with a ~2 dB step.\n"
        "    The drive stages are level-matched now, so live shows as THD and the\n"
        "    level must not step at all.");
    for (int mode : {0, 2}) {
        for (int drv : {0, 1, 2}) {
            Params p = headerDefaults();
            p.mode = mode;
            p.v[k_drive]             = drv;
            p.v[k_attenuation_limit] = 0;
            p.v[k_gain_limit]        = 0;
            if (mode == 2) {
                setBandThresholds(p, 0);
                setBandRatios(p, 10);
            }
            apply(p);
            const Result r = measure(0.1, F0, SETTLE, MEAS);
            printf("   %-10s DRIVE %3d -> %+7.2f dB  THD %6.3f%%\n",
                   MODE_NAME[mode], drv, r.gain_fund_db, r.thd_pct);
        }
    }

    hdr("G4. WAVEFOLDER GAIN LAW, HANDED BACK (Hard clip, in -60 dBFS so the\n"
        "    shaper stays linear).  The drive gain g = 1+19d -- times\n"
        "    drive_slam_gain() past DRIVE_SLAM_KNEE -- is what pushes the signal\n"
        "    into the shaper, and the level matching behind it hands all of it\n"
        "    back, so 'measured' must read 0 dB however large g gets.  History:\n"
        "    g was applied twice (+52 dB), then over-corrected with a 1/sqrt(g)\n"
        "    makeup, then left uncompensated (+26 dB across the knob, and the\n"
        "    whole of the theory column at -60 dBFS).  A matcher whose gain floor\n"
        "    sat above -g left this table +13.6 dB hot at DRIVE 100.");
    printf("   %6s %12s %14s\n", "DRIVE", "measured dB", "g dB");
    for (int drv : {0, 5, 10, 25, 50, 75, 100}) {
        Params p = headerDefaults();
        p.mode = 1;
        p.v[k_slope]           = distressorSlopeRaw(0);
        p.v[k_threhold]        = 0;
        p.dist = DRIVE_MODE_HARD_CLIP;
        p.v[k_drive]           = drv;
        apply(p);
        double m  = measure(0.001, F0, SETTLE, MEAS).gain_fund_db;
        const float slam = drive_slam_amount(drv * 0.01f);
        const slam_voicing_t& v = drive_slam_voicing[SLAM_FAMILY_SAT];
        double th = 20.0 * log10((1.0 + 19.0 * (drv * 0.01))
                                 * drive_slam_gain(slam, v.gain_max));
        printf("   %6d %+12.2f %+14.2f%s\n", drv, m, th,
               (fabs(m) > 0.5) ? "   <-- NOT HANDED BACK" : "");
    }

    hdr("G4b. SATURATED CEILING PER TYPE (in -6 dBFS, ratio 1:1).  Every shaper\n"
        "     saturates at +/-1, so any makeup scales the ceiling as well as the\n"
        "     small-signal gain.  Under the old 1/sqrt(g) law the five wavefolder\n"
        "     modes peaked at 0.224 at DRIVE=100 while the harmonic saturators,\n"
        "     which carry no makeup, reached 1.000 -- driving harder made those\n"
        "     five quieter.  The peaks must now stay in family across the row,\n"
        "     and with every drive stage level-matched none of them comes near\n"
        "     1.000, so the output limiter is never the thing doing the\n"
        "     distorting.");
    printf("   %-8s", "DRIVE");
    for (int t = 0; t < 9; ++t) printf(" %7s", DIST_NAME[t]);
    printf("\n");
    for (int drv : {0, 25, 50, 100}) {
        printf("   %-8d", drv);
        for (int t = 0; t < 9; ++t) {
            Params p = headerDefaults();
            p.mode = 1;
            p.v[k_slope]           = distressorSlopeRaw(0);
            p.v[k_threhold]        = 0;
            p.dist = t;
            p.v[k_drive]           = drv;
            apply(p);
            printf(" %7.3f", measure(0.5, F0, SETTLE, MEAS).peak);
        }
        printf("\n");
    }

    hdr("G5. PARAMETER ORDER: a host replaying IDs 0..23 sets SLOPE (1) before\n"
        "    COMP MODE (8), so the distressor ratio never leaves its 4:1 default");
    static const char* rn[8] = {"1:1","2:1","3:1","4:1","6:1","Opto","20:1","NUKE"};
    for (int idx : {0, 3, 7}) {
        Params p = headerDefaults();
        p.mode = 1;
        p.v[k_slope]           = distressorSlopeRaw(idx);
        p.v[k_threhold]        = -300;
        apply(p);        double a = measure(0.5, F0, SETTLE, MEAS).gain_fund_db;
        applyIdOrder(p); double b = measure(0.5, F0, SETTLE, MEAS).gain_fund_db;
        printf("   ratio %-5s (SLOPE %3d): mode-first %+7.2f dB   ID-order %+7.2f dB\n",
               rn[idx], p.v[k_slope], a, b);
    }

    hdr("G6. WET-PATH POLARITY PER DISTORTION TYPE (DRIVE 0, in -20 dBFS)\n"
        "    phase +/-180 means the wet path is inverted and cancels the dry path");
    printf("   %-8s %10s %11s %14s\n", "type", "gain dB", "phase deg", "MIX=BAL gain");
    for (int dist = 0; dist <= 8; ++dist) {
        Params p = headerDefaults();
        p.mode = 1;
        p.v[k_slope]           = distressorSlopeRaw(0);
        p.v[k_threhold]        = 0;
        p.dist = dist;
        apply(p);
        Result w = measure(0.1, F0, SETTLE, MEAS);
        p.v[k_mix] = 0;
        apply(p);
        Result b = measure(0.1, F0, SETTLE, MEAS);
        printf("   %-8s %+10.2f %11.1f %+14.2f\n",
               DIST_NAME[dist], w.gain_fund_db, w.phase_deg, b.gain_fund_db);
    }

    hdr("G7. MIX LAW (MAKEUP is applied to the wet path only)");
    for (int mix : {-100, -50, 0, 50, 100}) {
        Params p = headerDefaults();
        p.mode = 0;
        p.v[k_mix]               = mix;
        p.v[k_attenuation_limit] = 0;
        p.v[k_gain_limit]        = 0;
        apply(p);
        printf("   MIX %+5d -> %+7.2f dB\n", mix, measure(0.1, F0, SETTLE, MEAS).gain_fund_db);
    }

    hdr("G8. DRIVE REACHES A STAGE IN EVERY MODE.  DstrDist = None used to\n"
        "    bypass the Distressor's shaper while the broadband tube was gated\n"
        "    off for any non-Standard mode, so the factory default combination\n"
        "    (Distressor + DstrDist None) left DRIVE doing nothing at all.\n"
        "    Distressor/None must track Standard exactly up to DRIVE_SLAM_KNEE\n"
        "    (they are the same tube), and DRIVE=0 must still be clean in both.\n"
        "    Above the knee they separate on purpose: the slam region belongs to\n"
        "    the Distressor, so the last row is the one place these two columns\n"
        "    are meant to disagree -- more THD on the Distressor side, at a\n"
        "    comparable level.");
    printf("   %6s | %-22s | %-22s\n", "DRIVE", "Standard", "Distressor, None");
    printf("   %6s | %10s %11s | %10s %11s\n", "", "gain dB", "THD%", "gain dB", "THD%");
    for (int drv : {0, 1, 5, 20, 50, 60, 100}) {
        Params p = headerDefaults();
        p.v[k_attenuation_limit] = 0;
        p.v[k_gain_limit]        = 0;
        p.v[k_drive]             = drv;
        p.dist = DIST_MODE_CLEAN;

        p.mode = 0;
        apply(p);
        Result s = measure(0.1, F0, SETTLE, MEAS);

        p.mode = 1;
        p.v[k_slope]           = distressorSlopeRaw(0);   /* 1:1, no gain reduction */
        p.v[k_threhold]        = 0;
        apply(p);
        Result d = measure(0.1, F0, SETTLE, MEAS);

        printf("   %6d | %+10.2f %11.2f | %+10.2f %11.2f%s\n",
               drv, s.gain_fund_db, s.thd_pct, d.gain_fund_db, d.thd_pct,
               (drv * 0.01f > DRIVE_SLAM_KNEE) ? "   <-- slam region" : "");
    }

    hdr("G9. COST OF SILENCE.  A decaying IIR fed silence parks its delay line\n"
        "    in the subnormal range and stays there, and subnormal arithmetic is\n"
        "    one to two orders of magnitude slower wherever flush-to-zero is off.\n"
        "    Multiband holds 64 crossover states, so it used to cost 44x more to\n"
        "    process silence than signal -- enough to overrun the audio thread\n"
        "    between drum hits. Every ratio here must stay near 1.0x.");
    printf("   %-30s %11s %11s %9s\n", "mode", "signal", "silence", "ratio");
    for (int mode = 0; mode <= 2; ++mode) {
        for (int drv : {0, 100}) {
            Params p = headerDefaults();
            p.mode = mode;
            p.v[k_drive]           = drv;
            p.v[k_bass]            = 100;   /* keeps the Overlord EQ in circuit */
            apply(p);

            std::vector<float> in(64 * 4, 0.0f), out(64 * 2);
            double t[2];
            for (int phase = 0; phase < 2; ++phase) {
                for (size_t i = 0; i < 64; ++i) {
                    float s = phase ? 0.0f : 0.4f * sinf((float)i * 0.13f);
                    in[i*4+0]=s; in[i*4+1]=s; in[i*4+2]=s; in[i*4+3]=s;
                }
                /* warm the state into the regime being measured */
                for (int k = 0; k < 4000; ++k) g_fx.Process(in.data(), out.data(), 64);
                clock_t c0 = clock();
                for (int k = 0; k < 20000; ++k) g_fx.Process(in.data(), out.data(), 64);
                t[phase] = (double)(clock() - c0) / CLOCKS_PER_SEC;
            }
            char lbl[64];
            snprintf(lbl, sizeof lbl, "%s, DRIVE %d", MODE_NAME[mode], drv);
            printf("   %-30s %9.3f s %9.3f s %8.2fx%s\n",
                   lbl, t[0], t[1], t[1] / t[0],
                   (t[1] / t[0] > 3.0) ? "  <-- DENORMAL STALL" : "");
        }
    }
}

/* S. The slam region: what DRIVE past DRIVE_SLAM_KNEE actually buys. */
static void section_slam() {
    hdr("S1. THE SLAM REGION (Distressor, ratio 1:1, in -20 dBFS 1 kHz -- a\n"
        "    realistic drum-bus level).  Below DRIVE 60 nothing here is armed\n"
        "    and these rows must match the old linear drive law exactly.  Above\n"
        "    it the pre-gain goes geometric and a program-dependent bias shifts\n"
        "    the duty cycle, so THD keeps climbing after the shaper has already\n"
        "    saturated -- which is the whole point, since a bounded shaper fed\n"
        "    more of the same gain simply stops changing.\n"
        "    Watch three things: THD roughly triples from 60 to 100, the output\n"
        "    level stays inside a couple of dB (the knob is buying character,\n"
        "    not level), and peak stays off 1.000 (the output limiter is not\n"
        "    doing the distorting).");
    for (int dist = 0; dist <= 8; ++dist) {
        printf("\n  DstrDist %d (%s)\n", dist, DIST_NAME[dist]);
        printf("  %6s %10s %10s %8s %7s\n",
               "DRIVE", "gain(1k)", "out dBFS", "THD%", "peak");
        for (int drive : {40, 50, 60, 70, 80, 90, 100}) {
            Params p = headerDefaults();
            p.mode = 1;
            p.v[k_slope]           = distressorSlopeRaw(0);
            p.v[k_threhold]        = 0;
            p.dist = dist;
            p.v[k_drive]           = drive;
            apply(p);
            Result r = measure(0.1, F0, SETTLE, MEAS);
            printf("  %6d %+10.2f %+10.2f %8.2f %7.3f%s\n",
                   drive, r.gain_fund_db, r.rms_dbfs, r.thd_pct, r.peak,
                   (drive == 60) ? "   <-- knee" : "");
        }
    }

    hdr("S2. THE SLAM IS DISTRESSOR-ONLY.  Standard and Multiband share the same\n"
        "    DRIVE knob but not the slam region, so their DRIVE 60 -> 100 rows\n"
        "    must be byte-identical to what they measured before it existed.\n"
        "    in -20 dBFS 1 kHz, limits wide open.");
    for (int mode : {0, 2}) {
        printf("\n  %s\n", MODE_NAME[mode]);
        printf("  %6s %10s %10s %8s\n", "DRIVE", "gain(1k)", "out dBFS", "THD%");
        for (int drive : {60, 80, 100}) {
            Params p = headerDefaults();
            p.mode = mode;
            p.v[k_attenuation_limit] = 0;
            p.v[k_gain_limit]        = 0;
            p.v[k_drive]             = drive;
            if (mode == 2) {
                setBandThresholds(p, 0);
                setBandRatios(p, 10);
            }
            apply(p);
            Result r = measure(0.1, F0, SETTLE, MEAS);
            printf("  %6d %+10.2f %+10.2f %8.2f\n",
                   drive, r.gain_fund_db, r.rms_dbfs, r.thd_pct);
        }
    }

    hdr("S2b. PARAMETER ORDER.  The slam depends on three parameters at once --\n"
        "     DRIVE (ID 5) for how far in, COMP MODE (ID 8) for whether there is\n"
        "     a slam region at all, and DstrDist (ID 15) for which voicing -- so\n"
        "     a host replaying IDs 0..23 in order arms it from a state where the\n"
        "     other two are still whatever Reset() left behind.  Both columns\n"
        "     must agree.  in -20 dBFS, DRIVE 100.");
    printf("   %-8s %12s %12s %12s %12s\n",
           "DstrDist", "mode-first", "THD%", "ID-order", "THD%");
    for (int dist = 0; dist <= 8; ++dist) {
        Params p = headerDefaults();
        p.mode = 1;
        p.v[k_slope]           = distressorSlopeRaw(0);
        p.v[k_threhold]        = 0;
        p.dist = dist;
        p.v[k_drive]           = 100;
        apply(p);        Result a = measure(0.1, F0, SETTLE, MEAS);
        applyIdOrder(p); Result b = measure(0.1, F0, SETTLE, MEAS);
        printf("   %-8s %+12.2f %12.2f %+12.2f %12.2f%s\n",
               DIST_NAME[dist], a.rms_dbfs, a.thd_pct, b.rms_dbfs, b.thd_pct,
               (fabs(a.rms_dbfs - b.rms_dbfs) > 0.05) ? "   <-- MISMATCH" : "");
    }

    hdr("S3. NO DC LEFT ON THE BUS.  Dist2 and Both are asymmetric and the slam\n"
        "    biases every shaper, so the stage behind them has to take the offset\n"
        "    back out -- a master FX that parks DC on the output steals headroom\n"
        "    from everything after it.  Mean/peak of the output over a whole\n"
        "    number of cycles, across the knob: this used to look only at DRIVE\n"
        "    100, where the slam's blocker happened to run, and Dist2 sat at\n"
        "    +20% of peak at DRIVE 50 unseen.");
    printf("  %-9s", "DstrDist");
    for (int drive : {0, 10, 30, 50, 60, 80, 100}) printf(" %8d", drive);
    printf("\n");
    for (int dist = 0; dist <= 8; ++dist) {
        printf("  %-9s", DIST_NAME[dist]);
        bool dc = false;
        for (int drive : {0, 10, 30, 50, 60, 80, 100}) {
            Params p = headerDefaults();
            p.mode = 1;
            p.v[k_slope]           = distressorSlopeRaw(0);
            p.v[k_threhold]        = 0;
            p.dist = dist;
            p.v[k_drive]           = drive;
            apply(p);
            double mean, peak;
            measureDC(0.1, F0, SETTLE, MEAS, &mean, &peak);
            const double r = (peak > 0) ? mean / peak : 0.0;
            printf(" %+7.2f%%", 100.0 * r);
            if (fabs(r) > 0.02) dc = true;
        }
        printf("%s\n", dc ? "   <-- DC ON THE BUS" : "");
    }
}


/* R. Recovery from a bad sample.
 *
 * OmniPress is the master: everything the drumlogue plays passes through it,
 * so a state it cannot get out of is a silent instrument.  Before the input
 * guard and watchdog, one NaN on the bus latched this host build for good in
 * every mode; on the shipped ARM build, where -ffast-math decided differently,
 * one +Inf left Multiband at -300 dBFS for good and one finite 1e20 sample
 * silenced Distressor for 9 s.  Each row
 * here plays a -10 dBFS 220 Hz tone through the reported
 * settings (THRESH -11.4, ATTACK 3.2 ms, RELEASE 224 ms), spoils it once, and
 * compares the output 3 s later against an identical run that was left alone.
 */
static int g_recover_failures = 0;

static bool nonfinite(float x) {         /* bit test: robust under -ffast-math */
    uint32_t u; memcpy(&u, &x, sizeof u);
    return (u & 0x7F800000u) == 0x7F800000u;
}

enum Spoil { SPOIL_NONE, SPOIL_INPUT, SPOIL_ENVELOPE, SPOIL_DISTRESSOR_ENV,
             SPOIL_TUBE_DC, SPOIL_CROSSOVER, SPOIL_SLAM_DC, SPOIL_DRIVE_DC,
             SPOIL_DRIVE_LEVEL, SPOIL_TUBE_LEVEL };

struct RecoverResult { double rms_db; long bad; uint32_t trips; };

static RecoverResult runSpoiled(const Params& p, Spoil spoil, float value) {
    apply(p);
    const uint32_t trips0 = g_fx.getGuardTrips();
    std::vector<float> in(BLOCK * NCH), out(BLOCK * 2);
    const double w = 2.0 * M_PI * 220.0 / SR;
    const long spoil_at = SR / 2, from = spoil_at + 3 * SR, to = from + SR / 2;
    long n = 0, bad = 0, N = 0;
    double ss = 0;
    while (n < to) {
        const long n0 = n;
        for (size_t i = 0; i < BLOCK; ++i, ++n) {
            float v = (float)(0.316 * sin(w * n));
            if (spoil == SPOIL_INPUT && n == spoil_at) v = value;
            for (int c = 0; c < NCH; ++c) in[i*NCH + c] = v;
        }
        if (n0 <= spoil_at && spoil_at < n) {
            switch (spoil) {
                case SPOIL_ENVELOPE:       g_fx.envelope_.env_state = value; break;
                case SPOIL_DISTRESSOR_ENV: g_fx.distressor_.distressor_env.env_state = value; break;
                case SPOIL_TUBE_DC:        g_fx.overlord_.dc_l.y_prev = value; break;
                case SPOIL_CROSSOVER:      g_fx.multiband_.xover_low_mid.l_lpf_z1 = value; break;
                case SPOIL_SLAM_DC:        g_fx.slam_.dc_l.y_prev = value; break;
                case SPOIL_DRIVE_DC:       g_fx.distressor_.out_dc_l.y_prev = value; break;
                case SPOIL_DRIVE_LEVEL:    g_fx.distressor_.level.out = value; break;
                case SPOIL_TUBE_LEVEL:     g_fx.overlord_.level.out = value; break;
                default: break;
            }
        }
        g_fx.Process(in.data(), out.data(), BLOCK);
        for (size_t i = 0; i < BLOCK * 2; ++i) {
            if (nonfinite(out[i])) { ++bad; continue; }
            if (n0 >= from) { ss += (double)out[i] * out[i]; ++N; }
        }
    }
    RecoverResult r;
    r.rms_db = 10.0 * log10(ss / (N ? N : 1) + 1e-30);
    r.bad = bad;
    r.trips = g_fx.getGuardTrips() - trips0;
    return r;
}

static void section_recover() {
    struct Cfg { const char* name; int mode; int dist; int drive; int mix; };
    const Cfg cfgs[] = {
        { "Standard   DRIVE 0  ",   0, 0,  0, 100 },
        { "Standard   DRIVE 67 ",   0, 0, 67, 100 },
        { "Dstr Off   DRIVE 67 ",   1, 0, 67, 100 },
        { "Dstr Dist2 DRIVE 0  ",   1, 1,  0, 100 },
        { "Dstr Dist2 DRIVE 67 ",   1, 1, 67, 100 },
        { "Dstr Dist2 DRIVE 67 BAL", 1, 1, 67, 0 },
        { "Multiband  DRIVE 0  ",   2, 0,  0, 100 },
        { "Multiband  DRIVE 67 ",   2, 0, 67, 100 },
    };
    auto params = [](const Cfg& c) {
        Params p = headerDefaults();
        p.v[k_threhold] = -114;  p.v[k_attack] = 32;  p.v[k_release] = 224;
        p.mode = c.mode;
        p.v[k_slope] = (c.mode == 1) ? distressorSlopeRaw(4) : 40;   /* 6:1 */
        p.dist = c.dist;
        p.v[k_drive] = c.drive;
        p.v[k_mix] = c.mix;
        return p;
    };
    auto row = [](const char* what, const RecoverResult& ref, const RecoverResult& r,
                  bool expect_trip) {
        const bool level_ok = fabs(r.rms_db - ref.rms_db) < 0.5;
        const bool ok = level_ok && r.bad == 0 && (!expect_trip || r.trips > 0);
        printf("   %-10s %9.2f %9.2f %8ld %6u   %s\n", what, ref.rms_db, r.rms_db,
               r.bad, r.trips, ok ? "ok" : "<-- FAIL");
        if (!ok) ++g_recover_failures;
    };

    hdr("R1. ONE BAD SAMPLE ON THE BUS.  Out 3 s later vs. an untouched run;\n"
        "    must match within 0.5 dB with no non-finite sample anywhere.");
    const float pokes[]       = { NAN, INFINITY, -INFINITY, 1e30f, 1e20f };
    const char* poke_names[]  = { "NaN", "+Inf", "-Inf", "1e30", "1e20" };
    for (const Cfg& c : cfgs) {
        printf("\n  %s\n   %-10s %9s %9s %8s %6s\n", c.name, "spoil", "ref dB", "out dB", "nonfin", "trips");
        const RecoverResult ref = runSpoiled(params(c), SPOIL_NONE, 0.0f);
        for (int k = 0; k < 5; ++k)
            row(poke_names[k], ref, runSpoiled(params(c), SPOIL_INPUT, pokes[k]), false);
    }

    hdr("R2. A NaN THAT STARTS INSIDE.  The input guard cannot see these, so the\n"
        "    watchdog has to: state is poisoned directly, the output must stay\n"
        "    finite, the watchdog must trip, and the level must come back.");
    struct Inside { const char* name; int cfg; Spoil spoil; };
    const Inside inside[] = {
        { "envelope",   0, SPOIL_ENVELOPE },        /* finite-but-silent case */
        { "dstr env",   4, SPOIL_DISTRESSOR_ENV },  /* likewise */
        { "tube DC",    1, SPOIL_TUBE_DC },
        { "tube lvl",   1, SPOIL_TUBE_LEVEL },
        { "slam DC",    2, SPOIL_SLAM_DC },         /* the tube path's */
        { "drive DC",   4, SPOIL_DRIVE_DC },        /* the shapers' */
        { "drive lvl",  4, SPOIL_DRIVE_LEVEL },
        { "crossover",  6, SPOIL_CROSSOVER },
    };
    printf("   %-10s %9s %9s %8s %6s\n", "state", "ref dB", "out dB", "nonfin", "trips");
    for (const Inside& t : inside) {
        const Cfg& c = cfgs[t.cfg];
        const RecoverResult ref = runSpoiled(params(c), SPOIL_NONE, 0.0f);
        /* The block that carries the NaN is zeroed whole, but none of it may
         * leave as a non-finite sample. */
        row(t.name, ref, runSpoiled(params(c), t.spoil, NAN), true);
    }
    printf("\n  %s\n", g_recover_failures ? "RECOVERY: FAILED" : "RECOVERY: all rows ok");
}


/* M. The Multiband panel: one knob per band, two crossovers, SoloMute.
 *
 * Pass/fail, like R.  Every check reads the unit through its panel the way the
 * drumlogue does -- setParameter() with the header's raw values -- and
 * measures what comes out, so a knob wired to the wrong band, a crossover that
 * moves the other split, or a SoloMute value that silences the wrong band
 * fails here -- and so does a DRIVE that moves the level (M6). */
static int g_panel_failures = 0;

static void panelCheck(bool ok, const char* what, const char* detail) {
    printf("   %-52s %s %s\n", what, ok ? "ok      " : "<-- FAIL", detail);
    if (!ok) ++g_panel_failures;
}

static Params multibandFlat() {
    Params p = headerDefaults();
    p.mode = COMP_MODE_MULTIBAND;
    setBandThresholds(p, 0);       /* no gain reduction anywhere */
    setBandRatios(p, 10);
    return p;
}

static void section_panel() {
    char buf[96];
    hdr("M. MULTIBAND PANEL (pass/fail).  -20 dBFS tones, one per band:\n"
        "    100 Hz (Low), 1 kHz (Mid), 8 kHz (High) at the default splits.");

    const double tone[3] = { 100.0, 1000.0, 8000.0 };
    const char* band[3] = { "Low", "Mid", "High" };

    /* M1. SoloMute: exactly the band it names is soloed or muted. */
    printf("\n  M1. SoloMute\n");
    for (int sel = 0; sel < SOLO_MUTE_TOTAL; ++sel) {
        double g[3];
        for (int b = 0; b < 3; ++b) {
            Params p = multibandFlat();
            p.v[k_band_solo_mute] = sel;
            apply(p);
            g[b] = measure(0.1, tone[b], SETTLE / 2, MEAS / 2).gain_fund_db;
        }
        bool ok = true;
        for (int b = 0; b < 3; ++b) {
            const bool plays = (sel == SOLO_MUTE_OFF) ||
                               (sel >= SOLO_LOW && sel <= SOLO_HIGH && b == sel - SOLO_LOW) ||
                               (sel >= MUTE_LOW && sel <= MUTE_HIGH && b != sel - MUTE_LOW);
            ok = ok && (plays ? fabs(g[b]) < 1.0 : g[b] < -20.0);
        }
        snprintf(buf, sizeof(buf), "(Low %+6.1f  Mid %+6.1f  High %+6.1f dB)", g[0], g[1], g[2]);
        char what[64];
        snprintf(what, sizeof(what), "%-8s plays exactly the bands it should",
                 g_fx.getParameterStrValue(k_band_solo_mute, sel));
        panelCheck(ok, what, buf);
    }

    /* M2. Each threshold and ratio knob reaches its own band and no other. */
    printf("\n  M2. One knob per band\n");
    for (int k = 0; k < 3; ++k) {
        double g[3];
        for (int b = 0; b < 3; ++b) {
            Params p = multibandFlat();
            p.v[k_band_low_threshold + k] = -400;   /* -40 dB */
            p.v[k_band_low_ratio + k]     = 200;    /* 20:1 */
            apply(p);
            g[b] = measure(0.1, tone[b], SETTLE / 2, MEAS / 2).gain_fund_db;
        }
        bool ok = true;
        for (int b = 0; b < 3; ++b) ok = ok && ((b == k) ? g[b] < -10.0 : fabs(g[b]) < 1.0);
        snprintf(buf, sizeof(buf), "(Low %+6.1f  Mid %+6.1f  High %+6.1f dB)", g[0], g[1], g[2]);
        char what[64];
        snprintf(what, sizeof(what), "%s Thresh/Ratio compress the %s band only", band[k], band[k]);
        panelCheck(ok, what, buf);
    }

    /* M3. The splits move independently.  A 500 Hz tone is Mid at the
     * default 250 Hz low split and Low once Xover Lo passes it; a 4 kHz tone
     * is High at the default 2.5 kHz high split and Mid once Xover Hi passes
     * it.  Soloing the band it should be in tells which side it landed on. */
    printf("\n  M3. Crossovers\n");
    struct XCase { const char* what; int knob_id; int knob; int solo; double f; bool in; };
    const XCase xc[] = {
        { "500 Hz is Mid at Xover Lo 50 (250Hz)",   k_crossover_low,  50, SOLO_MID,  500.0, true  },
        { "500 Hz is Low at Xover Lo 100 (1000Hz)", k_crossover_low, 100, SOLO_LOW,  500.0, true  },
        { "4 kHz is High at Xover Hi 33 (2.5kHz)",  k_crossover_high, 33, SOLO_HIGH, 4000.0, true },
        { "4 kHz is Mid at Xover Hi 100 (16kHz)",   k_crossover_high,100, SOLO_MID,  4000.0, true },
        { "Xover Hi leaves the low split alone",    k_crossover_high,100, SOLO_LOW,  100.0, true  },
        { "Xover Lo leaves the high split alone",   k_crossover_low,   0, SOLO_HIGH, 8000.0, true },
    };
    for (const XCase& c : xc) {
        Params p = multibandFlat();
        p.v[c.knob_id] = c.knob;
        p.v[k_band_solo_mute] = c.solo;
        apply(p);
        const double g = measure(0.1, c.f, SETTLE / 2, MEAS / 2).gain_fund_db;
        snprintf(buf, sizeof(buf), "(%+6.1f dB)", g);
        panelCheck(c.in ? g > -3.5 : g < -20.0, c.what, buf);
    }

    /* M4. A program stored under the old COMP MODE numbering (0=Standard,
     * 1=Distressor, 2=Multiband) comes back on a mode that is on the panel. */
    printf("\n  M4. Old programs and the shelved engine\n");
    for (int v : {0, 1, 2}) {
        g_fx.Reset();
        g_fx.setParameter(k_compressor_mode, v);
        const int want = v == 0 ? COMP_MODE_STANDARD : COMP_MODE_MULTIBAND;
        snprintf(buf, sizeof(buf), "(engine %d)", g_fx.getEngineMode());
        char what[64];
        snprintf(what, sizeof(what), "stored COMP MODE %d selects %s", v,
                 want == COMP_MODE_STANDARD ? "Standard" : "Multiband");
        panelCheck(g_fx.getEngineMode() == want, what, buf);
    }
    {
        /* A program saved under the old layout, replayed by ID: the values
         * that were MBand, MBThr, MBRtio, MBAtk, MBReles, MBMkup, MBState and
         * XOVER land on the new IDs 16-23.  Whatever they mean now, the two
         * splits must stay in their own ranges and in order. */
        g_fx.Reset();
        const int32_t old_ids_16_23[8] = { 6, -200, 40, 150, 200, 240, 2, 50 };
        g_fx.setParameter(k_compressor_mode, 2);
        for (int i = 0; i < 8; ++i) g_fx.setParameter(16 + i, old_ids_16_23[i]);
        const float lo = g_fx.multiband_.xover_low_freq, hi = g_fx.multiband_.xover_high_freq;
        snprintf(buf, sizeof(buf), "(low %.0f Hz, high %.0f Hz)", lo, hi);
        panelCheck(lo >= 62.0f && lo <= 1000.5f && hi >= 999.5f && hi <= 16001.0f && lo <= hi,
                   "an old program's IDs 16-23 cannot cross the splits", buf);
    }
    {
        /* No ID sequence a host can send reaches the Distressor. */
        bool reached = false;
        for (int v = -2; v <= 8 && !reached; ++v) {
            g_fx.Reset();
            g_fx.setParameter(k_compressor_mode, v);
            for (int sm = 0; sm < SOLO_MUTE_TOTAL; ++sm) g_fx.setParameter(k_band_solo_mute, sm);
            reached = g_fx.getEngineMode() == COMP_MODE_DISTRESSOR;
        }
        panelCheck(!reached, "the Distressor is unreachable from the panel", "");
    }

    /* M5. ATTACK and RELEASE (page 1) now drive every band. */
    printf("\n  M5. Page-1 ballistics reach Multiband\n");
    {
        double tail[2];
        const int rels[2] = { 50, 2000 };
        for (int k = 0; k < 2; ++k) {
            Params p = headerDefaults();
            p.mode = COMP_MODE_MULTIBAND;
            setBandThresholds(p, -200);
            p.v[k_release] = rels[k];
            apply(p);
            measure(0.708, F0, 9600, 0);                  /* a 200 ms hit */
            tail[k] = measure(0.0316, F0, 4800, 4800).gain_fund_db;  /* 100-200 ms after */
        }
        snprintf(buf, sizeof(buf), "(RELEASE 50: %+.1f dB, 2000: %+.1f dB)", tail[0], tail[1]);
        panelCheck(tail[1] < tail[0] - 6.0, "RELEASE 2000 holds the gain reduction, 50 lets go", buf);
    }

    /* M6. DRIVE changes the character, not the level.  Every drive stage is
     * level-matched (DRIVE_LEVEL_* in constants.h): unmatched, Standard's
     * tube took a -20 dBFS bus up 18.7 dB by DRIVE 100 and Dist2 ended in a
     * full-scale square.  Quiet (still linear in the shaper), working and hot
     * levels: within 1 dB of DRIVE 0, with THD climbing at the working ones. */
    printf("\n  M6. DRIVE changes character, not level\n");
    struct DCase { const char* what; int mode; int dist; };
    const DCase dc[] = {
        { "Standard (Overlord tube)",      COMP_MODE_STANDARD,   0 },
        { "Distressor Dist2 (bench only)", COMP_MODE_DISTRESSOR, DIST_MODE_DIST2 },
    };
    for (const DCase& c : dc) {
        for (double amp : {0.001, 0.1, 0.5}) {
            double worst = 0.0, thd0 = 0.0, thd100 = 0.0;
            for (int drv : {0, 30, 67, 100}) {
                Params p = headerDefaults();
                p.mode = c.mode;
                p.dist = c.dist;
                p.v[k_threhold] = 0;                       /* no gain reduction */
                p.v[k_attenuation_limit] = 0;
                p.v[k_gain_limit] = 0;
                if (c.mode == COMP_MODE_DISTRESSOR) p.v[k_slope] = distressorSlopeRaw(0);
                p.v[k_drive] = drv;
                apply(p);
                const Result r = measure(amp, F0, SETTLE / 2, MEAS / 2);
                if (fabs(r.gain_rms_db) > fabs(worst)) worst = r.gain_rms_db;
                if (drv == 0)   thd0 = r.thd_pct;
                if (drv == 100) thd100 = r.thd_pct;
            }
            const bool live = (amp < 0.01) || (thd100 > thd0 + 10.0);
            snprintf(buf, sizeof(buf), "(worst %+5.2f dB, THD %.1f%% -> %.1f%%)", worst, thd0, thd100);
            char what[80];
            snprintf(what, sizeof(what), "%s, in %.0f dBFS", c.what, 20.0 * log10(amp));
            panelCheck(fabs(worst) < 1.0 && live, what, buf);
        }
    }

    /* M7. What the panel shows. */
    printf("\n  M7. Readouts\n");
    /* One call per printf: the unit hands back a single static buffer, which
     * the SDK allows -- the host never holds on to the pointer. */
    struct Readout { const char* name; int id; int v[3]; int n; };
    const Readout ro[] = {
        { "COMP MODE",  k_compressor_mode, {0, 1, 0},     2 },
        { "SoloMute",   k_band_solo_mute,  {0, 1, 4},     3 },
        { "Xover Lo",   k_crossover_low,   {0, 50, 100},  3 },
        { "Xover Hi",   k_crossover_high,  {0, 33, 100},  3 },
        { "Mid Ratio",  k_band_mid_ratio,  {10, 40, 200}, 3 },
    };
    for (const Readout& r : ro) {
        printf("     %-10s", r.name);
        for (int k = 0; k < r.n; ++k)
            printf("  %4d -> %-9s", r.v[k], g_fx.getParameterStrValue(r.id, r.v[k]));
        printf("\n");
    }
    printf("\n  %s\n", g_panel_failures ? "PANEL: FAILED" : "PANEL: all checks ok");
}

/* ========================================================================= */

int main(int argc, char** argv) {
    unit_runtime_desc_t desc{};
    desc.samplerate      = 48000;
    desc.input_channels  = 4;
    desc.output_channels = 2;
    int8_t err = g_fx.Init(&desc);
    printf("MasterFX::Init -> %d (0 = ok)\n", err);
    if (err != k_unit_err_none) return 1;

    std::string s = (argc > 1) ? argv[1] : "all";
    if (s == "all" || s == "gain")    section_gain();
    if (s == "all" || s == "default") section_default();
    if (s == "all" || s == "matched") section_matched();
    if (s == "all" || s == "mb")      section_mb();
    if (s == "all" || s == "drive")   section_drive();
    if (s == "all" || s == "kick")    section_kick();
    if (s == "all" || s == "release") section_release();
    if (s == "all" || s == "quirks")  section_quirks();
    if (s == "all" || s == "slam")    section_slam();
    if (s == "all" || s == "recover") section_recover();
    if (s == "all" || s == "panel")   section_panel();
    printf("\n");
    return (g_recover_failures || g_panel_failures) ? 1 : 0;
}
