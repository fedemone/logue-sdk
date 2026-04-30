#!/usr/bin/env python3
"""
test_audio_render.py — Compare rendered preset audio against reference samples.

This closes the loop that test_calibration.py cannot: it actually runs the DSP
(via render_presets binary), analyzes the rendered output with the same librosa
pipeline used for reference samples, and flags discrepancies in T60, pitch, and
spectral centroid.

Usage:
    # 1. Build and run the render tool first:
    #    g++ -std=c++17 -O2 -I. -Itest_stubs -I.. -I../../common -I../common \\
    #        -DRUNTIME_COMMON_H_ render_presets.cpp -o render_presets
    #    ./render_presets rendered/
    # 2. Run this script:
    python3 test_audio_render.py           # uses ./rendered/
    python3 test_audio_render.py --verbose # show all metrics

Tolerances (render vs. reference sample):
    T60:       factor 2.0  (rendered within 0.5× to 2.0× of reference)
    F0:        50 cents     (half a semitone)
    Centroid:  factor 2.5  (spectral brightness order-of-magnitude check)
    NzFrac:    absolute 0.4 (noise fraction: 0=pure tone, 1=pure noise)
"""

import re, sys, subprocess, os
from pathlib import Path
import numpy as np

sys.path.insert(0, str(Path(__file__).parent))
from analyze_samples import analyze, hz_to_midi

RENDERED_DIR = Path(sys.argv[1]) if len(sys.argv) > 1 and not sys.argv[1].startswith('--') else Path("rendered")
SAMPLES_DIR  = Path(__file__).parent / "samples"
VERBOSE      = "--verbose" in sys.argv

# ── Tolerance parameters ──────────────────────────────────────────────────────
T60_FACTOR      = 2.0   # rendered T60 must be within [ref/2, ref*2]
F0_CENTS        = 50    # rendered pitch within ±50 cents
CENTROID_FACTOR = 2.5   # rendered centroid within [ref/2.5, ref*2.5]
NZFRAC_ABS      = 0.40  # absolute difference in noise fraction

# ── Same SAMPLE_TO_PRESET mapping as test_calibration.py ─────────────────────
# (preset_name, preset_idx, synth_note)
SAMPLE_TO_PRESET = {
    "marimba-hit-c4_C_minor.wav":             ("Marimba",   1,  72),
    "kalimba-e_E.wav":                         ("Kalimba",  16,  65),
    "Koto-B5.wav":                             ("Koto",     10,  60),
    "Koto-Pluck-C-Major.wav":                  ("Koto",     10,  60),
    "Chinese-Gong.wav":                        ("TamTam",    9,  41),
    "Gong-long-G#.wav":                        ("Gong",     15,  50),
    "Djambe-A3.wav":                           ("Djambe",    6,  48),
    "Djambe-B3.wav":                           ("Djambe",    6,  48),
    "Taiko-Hit.wav":                           ("Taiko",     7,  41),
    "Orchestral-Timpani-C.wav":                ("Timpani",   5,  40),
    "Triangle-Bell-C#.wav":                    ("Triangle", 20,  84),
    "Triangle-Bell_F5.wav":                    ("Triangle", 20,  84),
    "Marching-Snare-Drum-A#-minor.wav":        ("MrchSnr",   8,  65),
    "Woodblock.wav":                           ("Wodblk",   12,  48),
    "Flute-D3.wav":                            ("Flute",    24,  72),
    "Clarinet-C-minor.wav":                    ("Clarinet", 25,  72),
    "Tabla-Drum-Hit-D4_.wav":                  ("Djambe",    6,  48),
    "percussion-one-shot-tabla-3_C_major.wav": ("Djambe",    6,  48),
    "cymbal-Crash16Inch.wav":                  ("Cymbal",   14,  60),
    "cymbal-Ride18Inch.wav":                   ("Ride",     35,  57),
    "cymbal-RideBell20InchSabian.wav":         ("RidBel",  36,  60),
    "glass-bowl-e-flat-tibetan-singing-bowl-struck-38746.wav": ("GlsBwl", 27, 76),
    "steel-pan-Nova Drum Real C 432.wav":      ("StelPan",  17,  60),
    "steel-pan-PERCY-C4-SHort.wav":            ("StelPan",  17,  60),
    "steel-pan-yudin C3.wav":                  ("StelPan",  17,  60),
    "tubular-bells.wav":                       ("TblrBel",   4,  72),
    "tubular-bells-phased.wav":                ("TblrBel",   4,  72),
    "tubular-bells-71571.wav":                 ("TblrBel",   4,  72),
    "high-church-clock-fx_100bpm.wav":         ("TblrBel",   4,  72),
    "HatClosedLive3.wav":                      ("HHat-C",   29,  79),
    "TightClosedHat.wav":                      ("HHat-C",   29,  79),
    "OpenHatBig.wav":                          ("HHat-O",   30,  79),
    "Bongo_Conga2.wav":                        ("Bongo",    37,  57),
    "GlassBottle.wav":                         ("GlsBotl",  38,  88),
    "one-tic-clock.wav":                       ("Tick",     39,  49),
    "ordinary-old-clock-ticking-sound-recording_120bpm-mechanical-strike.wav":
                                               ("Tick",     39,  49),
}

def cents_diff(f1, f2):
    """Pitch difference in cents between two frequencies."""
    if f1 is None or f2 is None or f1 <= 0 or f2 <= 0:
        return None
    return abs(1200 * np.log2(f1 / f2))

def t60_ratio(t_rendered, t_ref):
    """Factor by which rendered T60 differs from reference (1.0 = identical)."""
    if t_rendered is None or t_ref is None or t_ref <= 0:
        return None
    r = t_rendered / t_ref
    return r   # >1 means rendered decays slower, <1 means faster

def main():
    # ── Step 1: analyze all reference samples (group by preset) ──────────────
    ref_by_preset = {}   # preset_idx → {t60:[...], f0:[...], centroid:[...], nzfrac:[...]}
    wavs = sorted(SAMPLES_DIR.glob("*.wav"))
    print(f"Analysing {len(wavs)} reference samples…")
    for wav in wavs:
        if wav.name not in SAMPLE_TO_PRESET:
            continue
        name, idx, _note = SAMPLE_TO_PRESET[wav.name]
        r = analyze(wav)
        if r["t60_s"] is None:
            continue
        if idx not in ref_by_preset:
            ref_by_preset[idx] = {"name": name, "t60": [], "f0": [], "centroid": [], "nzfrac": []}
        ref_by_preset[idx]["t60"].append(r["t60_s"])
        if r["f0_hz"]: ref_by_preset[idx]["f0"].append(r["f0_hz"])
        ref_by_preset[idx]["centroid"].append(r["centroid_hz"])
        nzf = r.get("noise_fraction", r.get("nzmix", 0)) / 100.0 if r.get("nzmix") else r.get("noise_fraction", 0)
        ref_by_preset[idx]["nzfrac"].append(float(nzf))

    # ── Step 2: analyze rendered WAVs ─────────────────────────────────────────
    rendered_wavs = sorted(RENDERED_DIR.glob("*.wav"))
    print(f"Analysing {len(rendered_wavs)} rendered presets…\n")

    ren_by_preset = {}   # preset_idx → {t60, f0, centroid, nzfrac}
    for wav in rendered_wavs:
        # filename: "14_Cymbal.wav" → idx=14
        parts = wav.stem.split("_", 1)
        if len(parts) < 2:
            continue
        try:
            idx = int(parts[0])
        except ValueError:
            continue
        r = analyze(wav)
        if r["t60_s"] is None:
            continue
        nzf = r.get("noise_fraction", 0)
        ren_by_preset[idx] = {
            "name":     parts[1],
            "t60":      r["t60_s"],
            "f0":       r["f0_hz"],
            "centroid": r["centroid_hz"],
            "nzfrac":   float(nzf),
        }

    # ── Step 3: compare ───────────────────────────────────────────────────────
    total_checks = 0
    passed = 0
    failures = []

    fmt_h = f"{'Preset':<12} {'Metric':<12} {'Reference':>12} {'Rendered':>12} {'Ratio/Diff':>12}  Status"
    print(fmt_h)
    print("─" * 70)

    for idx in sorted(ref_by_preset.keys()):
        if idx not in ren_by_preset:
            continue
        ref = ref_by_preset[idx]
        ren = ren_by_preset[idx]
        name = ref["name"]

        ref_t60      = float(np.mean(ref["t60"]))      if ref["t60"]      else None
        ref_f0       = float(np.mean(ref["f0"]))       if ref["f0"]       else None
        ref_centroid = float(np.mean(ref["centroid"])) if ref["centroid"] else None
        ref_nzfrac   = float(np.mean(ref["nzfrac"]))   if ref["nzfrac"]   else 0.0

        ren_t60      = ren["t60"]
        ren_f0       = ren["f0"]
        ren_centroid = ren["centroid"]
        ren_nzfrac   = ren["nzfrac"]

        checks = []

        # T60 ratio
        if ref_t60 and ren_t60:
            ratio = ren_t60 / ref_t60
            ok = (1.0 / T60_FACTOR) <= ratio <= T60_FACTOR
            checks.append(("T60(s)", f"{ref_t60:.3f}", f"{ren_t60:.3f}", f"×{ratio:.2f}", ok))

        # F0 pitch
        if ref_f0 and ren_f0:
            c = cents_diff(ren_f0, ref_f0)
            ok = c <= F0_CENTS
            checks.append(("F0(Hz)", f"{ref_f0:.1f}", f"{ren_f0:.1f}", f"{c:+.0f}¢", ok))

        # Spectral centroid
        if ref_centroid and ren_centroid:
            r2 = ren_centroid / ref_centroid
            ok = (1.0 / CENTROID_FACTOR) <= r2 <= CENTROID_FACTOR
            checks.append(("Centroid", f"{ref_centroid:.0f}Hz", f"{ren_centroid:.0f}Hz", f"×{r2:.2f}", ok))

        # Noise fraction
        diff_nz = abs(ren_nzfrac - ref_nzfrac)
        ok_nz = diff_nz <= NZFRAC_ABS
        checks.append(("NoiseFrac", f"{ref_nzfrac:.2f}", f"{ren_nzfrac:.2f}", f"{diff_nz:+.2f}", ok_nz))

        for metric, ref_val, ren_val, diff_str, ok in checks:
            total_checks += 1
            if ok:
                passed += 1
                status = "PASS"
            else:
                status = "FAIL"
                failures.append((name, metric, ref_val, ren_val, diff_str))
            if VERBOSE or not ok:
                print(f"{name:<12} {metric:<12} {ref_val:>12} {ren_val:>12} {diff_str:>12}  {status}")

        if VERBOSE:
            print()

    # ── Summary ───────────────────────────────────────────────────────────────
    score = 100.0 * passed / total_checks if total_checks else 0
    print(f"\n{'═'*70}")
    print(f"AUDIO RENDER SCORE: {passed}/{total_checks} checks passed ({score:.0f}%)")
    print(f"  Presets with reference samples checked: {len(ref_by_preset & ren_by_preset.keys())}")
    print()

    if failures:
        print("PARAMETERS OUTSIDE TOLERANCE:")
        for name, metric, ref_val, ren_val, diff in failures:
            print(f"  [{name}] {metric}: ref={ref_val}, rendered={ren_val} (diff={diff})")
    else:
        print("All rendered presets match their reference samples within tolerance.")

    return 0 if not failures else 1

if __name__ == "__main__":
    sys.exit(main())
