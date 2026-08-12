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
 *   ./test_levels gain       # one section: gain|default|matched|mb|drive|kick|quirks
 */

#include <cstdio>
#include <cstring>
#include <cmath>
#include <string>
#include <vector>

#include "masterfx.h"

/* =========================================================================
 * Host emulation
 * ========================================================================= */

static MasterFX g_fx;          /* static storage => zero-init, like s_fx_instance */

static const int    SR    = 48000;
static const size_t BLOCK = 64;
static const int    NCH   = 4; /* drumlogue master FX input: L R SC-L SC-R */

struct Params { int32_t v[k_num_params]; };

/* The .init column of unit_header in header.c */
static Params headerDefaults() {
    Params p{};
    p.v[k_threhold]                   = -200;   /* -20.0 dB */
    p.v[k_slope]                      = 40;
    p.v[k_attack]                     = 150;    /* 15.0 ms */
    p.v[k_release]                    = 200;    /* 200 ms  */
    p.v[k_makeup]                     = 0;
    p.v[k_drive]                      = 0;
    p.v[k_mix]                        = 100;    /* WET */
    p.v[k_sc_hpf]                     = 20;
    p.v[k_compressor_mode]            = 0;
    p.v[k_attenuation_limit]          = -10;    /* -1.0 dB */
    p.v[k_gain_limit]                 = 10;     /* +1.0 dB */
    p.v[k_detection_mode]             = 0;
    p.v[k_bass]                       = 50;
    p.v[k_treble]                     = 50;
    p.v[k_presence]                   = 50;
    p.v[k_distressor_distortion_type] = 0;
    p.v[k_multiband_band_selection]   = 0;
    p.v[k_multiband_band_threshold]   = -200;
    p.v[k_multiband_band_ratio]       = 40;
    p.v[k_multiband_band_attack]      = 150;
    p.v[k_multiband_band_release]     = 200;
    p.v[k_multiband_band_makeup]      = 0;
    p.v[k_multiband_band_mute]        = 0;
    p.v[k_multiband_band_solo]        = 0;
    return p;
}

/* COMP MODE first and SLOPE last: setParameter(k_slope) branches on comp_mode_,
 * and the per-band values are routed by the current band selection. */
static void apply(const Params& p) {
    g_fx.Reset();
    g_fx.setParameter(k_compressor_mode, p.v[k_compressor_mode]);
    g_fx.setParameter(k_multiband_band_selection, p.v[k_multiband_band_selection]);
    for (int i = 0; i < k_num_params; ++i)
        if (i != k_compressor_mode && i != k_slope && i != k_multiband_band_selection)
            g_fx.setParameter(i, p.v[i]);
    g_fx.setParameter(k_slope, p.v[k_slope]);
}

/* How a host replays a stored preset: strictly by parameter ID */
static void applyIdOrder(const Params& p) {
    g_fx.Reset();
    for (int i = 0; i < k_num_params; ++i) g_fx.setParameter(i, p.v[i]);
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
            p.v[k_compressor_mode]   = mode;
            p.v[k_attenuation_limit] = 0;
            p.v[k_gain_limit]        = 0;
            if (mode == 1) p.v[k_slope] = distressorSlopeRaw(0);
            if (mode == 2) {
                p.v[k_multiband_band_selection] = BAND_ALL;
                p.v[k_multiband_band_threshold] = 0;
                p.v[k_multiband_band_ratio]     = 10;
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
    hdr("B. FACTORY DEFAULTS (THRESH -20, SLOPE 40, ATT LMT -1 dB, GAIN LMT +1 dB)");
    printf("%-12s %-9s %10s %10s %10s %8s\n",
           "mode", "in dBFS", "gain(1k)", "gain RMS", "out dBFS", "THD%");
    for (double amp : {0.01, 0.1, 0.5}) {
        for (int mode = 0; mode < 3; ++mode) {
            Params p = headerDefaults();
            p.v[k_compressor_mode] = mode;
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
    p.v[k_compressor_mode] = 0; p.v[k_slope] = 58;
    apply(p);
    Result r = measure(0.5, F0, SETTLE, MEAS);
    printf("%-12s %-12s %+10.2f %+10.2f %+10.2f\n",
           MODE_NAME[0], "SLOPE 58", r.gain_fund_db, r.gain_rms_db, r.rms_dbfs);

    p = headerDefaults();
    p.v[k_threhold] = -300; p.v[k_compressor_mode] = 1;
    p.v[k_slope] = distressorSlopeRaw(3);
    apply(p);
    r = measure(0.5, F0, SETTLE, MEAS);
    printf("%-12s %-12s %+10.2f %+10.2f %+10.2f\n",
           MODE_NAME[1], "4:1", r.gain_fund_db, r.gain_rms_db, r.rms_dbfs);

    p = headerDefaults();
    p.v[k_compressor_mode] = 2;
    p.v[k_multiband_band_selection] = BAND_ALL;
    p.v[k_multiband_band_threshold] = -300;
    p.v[k_multiband_band_ratio]     = 40;
    apply(p);
    r = measure(0.5, F0, SETTLE, MEAS);
    printf("%-12s %-12s %+10.2f %+10.2f %+10.2f\n",
           MODE_NAME[2], "4.0:1", r.gain_fund_db, r.gain_rms_db, r.rms_dbfs);
}

/* E. Multiband summing loss and its frequency dependence. */
static void section_mb() {
    hdr("E. MULTIBAND SUMMING LOSS");
    printf("  makeup needed for unity (all bands, no GR):\n");
    printf("  %-10s %10s %10s\n", "MBMkup dB", "gain(1k)", "out dBFS");
    for (int mk : {0, 30, 60, 69, 75, 90}) {
        Params p = headerDefaults();
        p.v[k_compressor_mode] = 2;
        p.v[k_multiband_band_selection] = BAND_ALL;
        p.v[k_multiband_band_threshold] = 0;
        p.v[k_multiband_band_ratio]     = 10;
        p.v[k_multiband_band_makeup]    = mk;
        apply(p);
        Result r = measure(0.1, F0, SETTLE, MEAS);
        printf("  %-10.1f %+10.2f %+10.2f\n", mk * 0.1, r.gain_fund_db, r.rms_dbfs);
    }
    printf("\n  crossover-tree flatness (no GR, makeup 0):\n");
    printf("  %-10s %10s %10s\n", "tone Hz", "gain(1k)", "phase deg");
    for (double f : {100.0, 250.0, 600.0, 1000.0, 2500.0, 6000.0}) {
        Params p = headerDefaults();
        p.v[k_compressor_mode] = 2;
        p.v[k_multiband_band_selection] = BAND_ALL;
        p.v[k_multiband_band_threshold] = 0;
        p.v[k_multiband_band_ratio]     = 10;
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
                 "   out of the picture; input %.0f dBFS 1 kHz)", 20*log10(amp));
        hdr(title);
        for (int dist = 0; dist <= 8; ++dist) {
            printf("\n  DstrDist %d (%s)\n", dist, DIST_NAME[dist]);
            printf("  %6s %10s %10s %8s %8s %9s %7s\n",
                   "DRIVE", "gain(1k)", "out dBFS", "THD%", "nonfnd%", "sub f/2", "peak");
            for (int drive : {0,1,2,3,5,8,10,15,20,30,50,70,100}) {
                Params p = headerDefaults();
                p.v[k_compressor_mode] = 1;
                p.v[k_slope]           = distressorSlopeRaw(0);
                p.v[k_threhold]        = 0;
                p.v[k_distressor_distortion_type] = dist;
                p.v[k_drive]           = drive;
                apply(p);
                Result r = measure(amp, F0, SETTLE, MEAS);
                printf("  %6d %+10.2f %+10.2f %8.2f %8.2f %9.1f %7.3f\n",
                       drive, r.gain_fund_db, r.rms_dbfs, r.thd_pct,
                       r.nonfund_pct, r.sub_db, r.peak);
            }
        }
    }

    hdr("D2. OVERLORD TUBE DRIVE (the DRIVE knob in Standard and Multiband mode)");
    for (double amp : {0.1, 0.5}) {
        printf("\n  input %.0f dBFS\n", 20*log10(amp));
        printf("  %6s %10s %10s %8s %7s\n", "DRIVE", "gain(1k)", "out dBFS", "THD%", "peak");
        for (int drive : {0,1,2,3,5,8,10,15,20,30,50,70,100}) {
            Params p = headerDefaults();
            p.v[k_compressor_mode]   = 0;
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
                p.v[k_compressor_mode] = 1;
                p.v[k_slope]           = distressorSlopeRaw(0);
                p.v[k_threhold]        = 0;
                p.v[k_distressor_distortion_type] = dist;
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
            p.v[k_compressor_mode]   = 0;
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

/* G. Mechanisms behind the numbers above. */
static void section_quirks() {
    hdr("G1. UNITY ACCURACY OF fasterpowf() USED FOR MAKEUP GAIN");
    printf("   fasterpowf(10, 0)     = %.6f -> %+0.3f dB (ideal 0)\n",
           fasterpowf(10.0f,0.0f), 20*log10(fasterpowf(10.0f,0.0f)));
    printf("   fasterpowf(10, 6/20)  = %.6f -> %+0.3f dB (ideal +6)\n",
           fasterpowf(10.0f,0.3f), 20*log10(fasterpowf(10.0f,0.3f)));
    printf("   fasterpowf(10, 12/20) = %.6f -> %+0.3f dB (ideal +12)\n",
           fasterpowf(10.0f,0.6f), 20*log10(fasterpowf(10.0f,0.6f)));
    printf("   fasterpowf(10, 24/20) = %.6f -> %+0.3f dB (ideal +24)\n",
           fasterpowf(10.0f,1.2f), 20*log10(fasterpowf(10.0f,1.2f)));

    hdr("G2. DETECTOR CALIBRATION: the feed is the mono average 0.5*(L+R), so a\n"
        "    mono source reads its true level.  Hard-panned content sits 6 dB\n"
        "    lower in a mono-sum detector by construction, which is why the two\n"
        "    columns still differ by 6 dB * slope.\n"
        "    threshold -30 dB, ~4:1, limits +/-30 dB, in -6 dBFS");
    for (int mode = 0; mode < 3; ++mode) {
        Params p = headerDefaults();
        p.v[k_compressor_mode]   = mode;
        p.v[k_threhold]          = -300;
        p.v[k_attenuation_limit] = -300;
        p.v[k_gain_limit]        = 300;
        p.v[k_slope]             = (mode == 1) ? distressorSlopeRaw(3) : 58;
        if (mode == 2) {
            p.v[k_multiband_band_selection] = BAND_ALL;
            p.v[k_multiband_band_threshold] = -300;
        }
        apply(p); double mono = measure(0.5, F0, SETTLE, MEAS, 0).gain_fund_db;
        apply(p); double left = measure(0.5, F0, SETTLE, MEAS, 1).gain_fund_db;
        printf("   %-11s mono(L=R) %+7.2f dB   left-only %+7.2f dB   delta %+5.2f dB\n",
               MODE_NAME[mode], mono, left, mono - left);
    }

    hdr("G3. DRIVE IS LIVE FROM ITS FIRST STEP.  The old gate was 'drive_ > 0.01f',\n"
        "    so DRIVE=1 was identical to 0 and DRIVE=2 arrived with a ~2 dB step.");
    for (int mode : {0, 2}) {
        for (int drv : {0, 1, 2}) {
            Params p = headerDefaults();
            p.v[k_compressor_mode]   = mode;
            p.v[k_drive]             = drv;
            p.v[k_attenuation_limit] = 0;
            p.v[k_gain_limit]        = 0;
            if (mode == 2) {
                p.v[k_multiband_band_selection] = BAND_ALL;
                p.v[k_multiband_band_threshold] = 0;
                p.v[k_multiband_band_ratio]     = 10;
            }
            apply(p);
            printf("   %-10s DRIVE %3d -> %+7.2f dB\n",
                   MODE_NAME[mode], drv, measure(0.1, F0, SETTLE, MEAS).gain_fund_db);
        }
    }

    hdr("G4. WAVEFOLDER GAIN LAW (Hard clip, in -60 dBFS so the shaper stays\n"
        "    linear).  Pre-gain is g = 1+19d and makeup is 1/sqrt(g), so the net\n"
        "    small-signal gain is sqrt(g): +13 dB across the knob, not +52 dB.");
    printf("   %6s %12s %14s %8s\n", "DRIVE", "measured dB", "sqrt(g) dB", "delta");
    for (int drv : {0, 5, 10, 25, 50, 75, 100}) {
        Params p = headerDefaults();
        p.v[k_compressor_mode] = 1;
        p.v[k_slope]           = distressorSlopeRaw(0);
        p.v[k_threhold]        = 0;
        p.v[k_distressor_distortion_type] = DRIVE_MODE_HARD_CLIP;
        p.v[k_drive]           = drv;
        apply(p);
        double m  = measure(0.001, F0, SETTLE, MEAS).gain_fund_db;
        double th = 10.0 * log10(1.0 + 19.0 * (drv * 0.01));
        printf("   %6d %+12.2f %+14.2f %+8.2f\n", drv, m, th, m - th);
    }

    hdr("G5. PARAMETER ORDER: a host replaying IDs 0..23 sets SLOPE (1) before\n"
        "    COMP MODE (8), so the distressor ratio never leaves its 4:1 default");
    static const char* rn[8] = {"1:1","2:1","3:1","4:1","6:1","Opto","20:1","NUKE"};
    for (int idx : {0, 3, 7}) {
        Params p = headerDefaults();
        p.v[k_compressor_mode] = 1;
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
        p.v[k_compressor_mode] = 1;
        p.v[k_slope]           = distressorSlopeRaw(0);
        p.v[k_threhold]        = 0;
        p.v[k_distressor_distortion_type] = dist;
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
        p.v[k_compressor_mode]   = 0;
        p.v[k_mix]               = mix;
        p.v[k_attenuation_limit] = 0;
        p.v[k_gain_limit]        = 0;
        apply(p);
        printf("   MIX %+5d -> %+7.2f dB\n", mix, measure(0.1, F0, SETTLE, MEAS).gain_fund_db);
    }
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
    if (s == "all" || s == "quirks")  section_quirks();
    printf("\n");
    return 0;
}
