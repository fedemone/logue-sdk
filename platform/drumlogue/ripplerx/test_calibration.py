#!/usr/bin/env python3
"""
test_calibration.py — Compare current preset parameters against reference WAV analysis.

Usage:
    python3 test_calibration.py           # run all checks
    python3 test_calibration.py --verbose # show per-parameter detail

Exit code: 0 = all presets within tolerance, 1 = at least one fails.

Tolerance bands (±):
    Dkay  ±20   (~15% of full range 0-200)
    Mterl ±8    (one octave brightness shift)
    InHm  ±40   (broader: measurement noisier at high freq)
"""

import re, sys, json
from pathlib import Path
import numpy as np

# ── import the analysis engine ────────────────────────────────────────────────
sys.path.insert(0, str(Path(__file__).parent))
from analyze_samples import analyze, spectral_centroid_slope, hz_to_midi

SAMPLES_DIR   = Path(__file__).parent / "samples"
SYNTH_ENGINE  = Path(__file__).parent / "synth_engine.h"
VERBOSE       = "--verbose" in sys.argv

# ── tolerances ───────────────────────────────────────────────────────────────
TOL_DKAY  = 20
TOL_MTERL = 8
TOL_INHARM = 40

# ── map sample file → (preset_name, preset_row_idx, preset_note_col1) ────────
# preset_note_col1 is what the synth's Note param is set to (for T60 calc)
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
}

# ── parse preset table from synth_engine.h ───────────────────────────────────
# Each preset row looks like:
#   { n, note, bank, smp, mlr, mls, vlr, vls, ptls, mdl, dky, mtr, ton, hit, rel, inh, lwc, tbr, gn, nzm, nzr, nzf, nzq, rsc},
def parse_presets(path):
    text = path.read_text()
    # Find everything between "static const int32_t presets" and the closing "};"
    m = re.search(r'static const int32_t presets\[.*?\]\s*=\s*\{(.*?)\};',
                  text, re.DOTALL)
    if not m:
        raise ValueError("Could not find preset table in synth_engine.h")
    block = m.group(1)
    rows = {}
    for line in block.splitlines():
        # Strip comments, find "{...}"
        code = re.sub(r'//.*', '', line).strip()
        m2 = re.search(r'\{([^}]+)\}', code)
        if not m2:
            continue
        nums = [int(x.strip()) for x in m2.group(1).split(',') if x.strip()]
        if len(nums) < 24:
            continue
        # cols: 0=prg,1=note,2=bnk,3=smp,4=mlr,5=mls,6=vlr,7=vls,
        #       8=ptls,9=mdl,10=dky,11=mtr,12=ton,13=hit,14=rel,15=inh,
        #       16=lwc,17=tbr,18=gn,19=nzm,20=nzr,21=nzf,22=nzq,23=rsc
        idx = nums[0]
        rows[idx] = {
            "note":  nums[1],
            "dkay":  nums[10],
            "mterl": nums[11],
            "inharm":nums[15],
            "nzmix": nums[19],
            "mllt_stif": nums[5],
        }
    return rows

# ── convert T60 + note → Dkay  (same as analyze_samples.py) ─────────────────
def t60_to_dkay(t60_s, note_midi, sr=48000):
    f0   = 440.0 * 2**((note_midi - 69) / 12.0)
    N    = sr / f0
    log10g = 3.0 * N / (sr * t60_s)
    g    = 10.0 ** (-log10g)
    g    = np.clip(g, 0.85, 0.999)
    dkay = (g - 0.85) / 0.149 * 200.0
    return int(np.clip(round(dkay), 0, 200))

def centroid_to_mterl(centroid_hz, f0_hz):
    ratio = centroid_hz / max(f0_hz, 1.0)
    coeff = np.clip(ratio / 40.0, 0.01, 0.99)
    mterl_norm = (coeff - 0.01) / 0.99
    return int(np.clip(round(mterl_norm * 40.0 - 10.0), -10, 30))

def inharm_b_to_inharm(B):
    return int(np.clip(round(B * 2000), 0, 1999))

# ── run test ──────────────────────────────────────────────────────────────────
def main():
    presets = parse_presets(SYNTH_ENGINE)
    wavs    = sorted(SAMPLES_DIR.glob("*.wav"))

    # Accumulate targets per preset (may have multiple samples → average)
    targets = {}   # preset_idx → {dkay: [...], mterl: [...], inharm: [...]}

    print(f"\nAnalysing {len(wavs)} reference samples…")
    for wav in wavs:
        fname = wav.name
        if fname not in SAMPLE_TO_PRESET:
            continue
        preset_name, preset_idx, synth_note = SAMPLE_TO_PRESET[fname]

        r     = analyze(wav)
        slope = spectral_centroid_slope(wav)

        if r["t60_s"] is None or r["f0_hz"] is None:
            continue

        dkay_target   = t60_to_dkay(r["t60_s"], synth_note)
        mterl_target  = centroid_to_mterl(r["centroid_hz"], r["f0_hz"] or 440)
        # Slope correction: falling centroid → darker → push Mterl down
        if slope < -200 and mterl_target > 0:
            mterl_target = max(mterl_target - 10, -10)
        inharm_target = inharm_b_to_inharm(r["B_inharm"])

        if preset_idx not in targets:
            targets[preset_idx] = {"name": preset_name,
                                   "dkay": [], "mterl": [], "inharm": []}
        targets[preset_idx]["dkay"].append(dkay_target)
        targets[preset_idx]["mterl"].append(mterl_target)
        targets[preset_idx]["inharm"].append(inharm_target)

    # Evaluate each preset
    total_checks = 0
    passed       = 0
    failures     = []

    print(f"\n{'Preset':<12} {'Param':<8} {'Target':>8} {'Actual':>8} {'Diff':>8}  Status")
    print("─" * 60)

    preset_scores = {}

    for idx in sorted(targets.keys()):
        t = targets[idx]
        if idx not in presets:
            continue

        p    = presets[idx]
        name = t["name"]

        dkay_t   = int(round(np.mean(t["dkay"])))
        mterl_t  = int(round(np.mean(t["mterl"])))
        inharm_t = int(round(np.mean(t["inharm"])))

        checks = [
            ("Dkay",  dkay_t,   p["dkay"],  TOL_DKAY),
            ("Mterl", mterl_t,  p["mterl"], TOL_MTERL),
            ("InHm",  inharm_t, p["inharm"],TOL_INHARM),
        ]

        preset_pass = True
        preset_score = 0.0

        for param, target, actual, tol in checks:
            diff   = actual - target
            ok     = abs(diff) <= tol
            status = "PASS" if ok else "FAIL"
            total_checks += 1
            if ok:
                passed += 1
            else:
                preset_pass = False
                failures.append((name, param, target, actual, diff))

            # Score: 0=perfect, 1=just outside tolerance, >1=way off
            preset_score += abs(diff) / tol

            if VERBOSE or not ok:
                print(f"{name:<12} {param:<8} {target:>8} {actual:>8} {diff:>+8}  {status}")

        preset_scores[name] = preset_score / len(checks)
        if VERBOSE:
            print()

    # Summary
    score_pct = 100.0 * passed / total_checks if total_checks else 0
    print(f"\n{'═'*60}")
    print(f"CALIBRATION SCORE: {passed}/{total_checks} checks passed ({score_pct:.0f}%)")
    print()

    if failures:
        print("PARAMETERS OUTSIDE TOLERANCE:")
        for name, param, target, actual, diff in failures:
            print(f"  [{name}] {param}: preset={actual}, target={target} (off by {diff:+d})")
    else:
        print("All preset parameters are within calibration tolerance.")

    print()
    print("Per-preset deviation (lower=better, 1.0=just at tolerance boundary):")
    for name, score in sorted(preset_scores.items(), key=lambda x: -x[1]):
        bar = "█" * int(score * 10)
        print(f"  {name:<12} {score:.2f}  {bar}")

    return 0 if not failures else 1

if __name__ == "__main__":
    sys.exit(main())
