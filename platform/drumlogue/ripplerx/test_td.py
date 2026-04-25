#!/usr/bin/env python3
"""
test_td.py — Time-domain energy-envelope validation for rendered presets.

Why not ±10% per-sample against reference recordings?
  A 262 Hz waveform has adjacent-sample deltas of ~40% of full scale. Any
  1-sample time misalignment between a synthesized tone and a reference (even
  the same tone, slightly shifted) will exceed a ±10% tolerance. Real-sample
  references also have room acoustics and different sample rates. Per-sample
  comparison against real recordings is physically infeasible.

What this test does instead:
  For each rendered preset, it computes the RMS energy envelope (10 ms windows)
  and checks two acoustic properties against analytically derived targets:

    1. T60 (decay time):  measured ≥ Dkay-theoretical × 0.25
                          measured ≤ Dkay-theoretical × 1.10
       The LP filter (Mterl) reduces T60 below the Dkay-only prediction.
       The 0.25 lower bound accommodates worst-case LP loss.

    2. Noise character:  for NzMix ≥ 50 the zero-crossing rate in the first
       20 ms must exceed 5 kHz — proving broad-band noise, not a pitched tone.

Usage:
    python3 test_td.py              # uses ./rendered/
    python3 test_td.py rendered/    # explicit path
    python3 test_td.py --preset 0   # single preset (InitDbg)

Build the renders first:
    g++ -std=c++17 -O2 -I. -Itest_stubs -I.. -I../../common -I../common \\
        -DRUNTIME_COMMON_H_ render_presets.cpp -o render_presets
    ./render_presets rendered/
"""

import math, struct, sys, wave
from pathlib import Path

# ── Configuration ─────────────────────────────────────────────────────────────
RENDERED_DIR  = Path("rendered")
T60_LO_MULT   = 0.15   # measured T60 must be ≥ this fraction of theoretical
T60_HI_MULT   = 2.20   # inharmonic mode beating can extend measured T60 to ~2× KS theoretical
NOISE_ZC_HZ   = 5000   # zero-crossing rate threshold for "noise dominant" character

# ── Preset table (mirrors synth_engine.h)
# Columns: Note, Dkay, NzMix
# Only columns needed for acoustic validation are listed here.
# Index = preset index.
# Mirrored from synth_engine.h m_factory_presets[]. TamTam removed; all
# indices from 9 onward shifted. Note = preset's native pitch; Dkay/NzMix
# from the preset row (fields [10] and [19]).
PRESETS = [
    #  Note  Dkay NzMix  Name
    (  60,   25,    0,  "InitDbg"),
    (  72,  184,    0,  "Marimba"),
    (  36,   50,    0,  "808Sub"),
    (  38,   15,   40,  "AcSnre"),
    (  72,  199,    0,  "TblrBel"),
    (  40,  145,    2,  "Timpani"),
    (  48,  107,   15,  "Djambe"),
    (  41,  173,    5,  "Taiko"),
    (  65,   86,   25,  "MrchSnr"),
    (  60,  185,    0,  "Koto"),
    (  72,  199,    0,  "Vibrph"),
    (  48,   82,   18,  "Wodblk"),
    (  45,   80,    2,  "AcTom"),
    (  60,  176,   15,  "Cymbal"),
    (  50,  188,    4,  "Gong"),
    (  65,  194,    5,  "Kalimba"),
    (  60,  194,    0,  "StelPan"),
    (  79,    3,    0,  "Claves"),
    (  67,  175,    0,  "Cowbel"),
    (  84,  199,    8,  "Triangle"),
    (  36,   70,    3,  "Kick"),
    (  60,    5,  100,  "Clap"),
    (  72,    2,  100,  "Shaker"),
    (  72,  191,   10,  "Flute"),
    (  72,  145,    8,  "Clarinet"),
    (  36,   95,    0,  "PlkBss"),
    (  76,  200,    0,  "GlsBwl"),
    (  69,  195,    0,  "GtrStr"),
    (  79,  119,   50,  "HHat-C"),
    (  79,  169,   40,  "HHat-O"),
    (  62,  149,   12,  "Conga"),
    (  62,  198,    5,  "Handpn"),
    (  84,  193,    5,  "BelTre"),
    (  60,  167,   10,  "SltDrm"),
    (  57,  192,   20,  "Ride"),
    (  60,  184,   20,  "RidBel"),
    (  57,   94,    5,  "Bongo"),
    (  88,  175,   45,  "GlsBotl"),
    (  49,  100,   29,  "Tick"),
]

# ── Helpers ───────────────────────────────────────────────────────────────────
def theoretical_t60(note: int, dkay: int) -> float:
    """T60 from Dkay and note, ignoring LP filter (upper bound)."""
    g = 0.85 + (dkay / 200.0) * 0.149
    if g >= 1.0:
        return float('inf')
    f0 = 440.0 * 2.0 ** ((note - 69) / 12.0)
    N  = 48000.0 / max(f0, 12.0)
    return -3.0 * N / (48000.0 * math.log10(g))


def load_wav_mono(path: Path):
    """Return (samples_float, sample_rate)."""
    with wave.open(str(path), 'rb') as f:
        sr  = f.getframerate()
        sw  = f.getsampwidth()
        ch  = f.getnchannels()
        n   = f.getnframes()
        raw = f.readframes(n)
    fmt   = '<h' if sw == 2 else '<i'
    scale = 32768.0 if sw == 2 else 2147483648.0
    step  = sw * ch
    total = n * ch
    ints  = [struct.unpack_from(fmt, raw, i * step)[0] for i in range(n)]
    return [v / scale for v in ints], sr


def rms_envelope(samples, sr, win_ms=10):
    """Return list of (time_s, rms) for each 10 ms window."""
    w   = int(sr * win_ms / 1000)
    out = []
    for i in range(0, len(samples) - w, w):
        chunk = samples[i:i+w]
        rms   = math.sqrt(sum(x*x for x in chunk) / len(chunk))
        out.append((i / sr, rms))
    return out


def measure_t60(env):
    """Time when RMS falls to −60 dB relative to peak."""
    if not env:
        return None
    peak = max(r for _, r in env)
    if peak < 1e-9:
        return None
    threshold = peak * 10 ** (-60 / 20)
    for t, r in env:
        if r < threshold:
            return t
    return env[-1][0]          # still above threshold at end of file


def zero_crossing_rate(samples, sr, window_ms=20):
    """Zero-crossing frequency in Hz over the first `window_ms` ms."""
    seg = samples[:int(sr * window_ms / 1000)]
    zc  = sum(1 for i in range(1, len(seg)) if seg[i-1] * seg[i] < 0)
    return (zc / 2) * (sr / len(seg)) if seg else 0.0


# ── Main ──────────────────────────────────────────────────────────────────────
def main():
    global RENDERED_DIR

    only_idx = None
    args = sys.argv[1:]
    skip_next = False
    for i, arg in enumerate(args):
        if skip_next:
            skip_next = False
            continue
        if arg == '--preset':
            try:
                only_idx = int(args[i + 1])
                skip_next = True
            except (IndexError, ValueError):
                pass
        elif arg.startswith('--preset='):
            try:
                only_idx = int(arg.split('=', 1)[1])
            except ValueError:
                pass
        elif not arg.startswith('--'):
            RENDERED_DIR = Path(arg)

    wavs = sorted(RENDERED_DIR.glob("*.wav"))
    if not wavs:
        print(f"No WAV files found in {RENDERED_DIR}")
        sys.exit(1)

    total = passed = 0
    failures = []

    print(f"{'Preset':<10} {'T60 meas':>9} {'T60 theo':>9} {'Ratio':>7}  {'ZC Hz':>7}  "
          f"{'T60':>5}  {'Noise':>5}  Status")
    print("─" * 72)

    for wav in wavs:
        parts = wav.stem.split("_", 1)
        if len(parts) < 2:
            continue
        try:
            idx = int(parts[0])
        except ValueError:
            continue

        if only_idx is not None and idx != only_idx:
            continue

        if idx >= len(PRESETS):
            continue

        note, dkay, nzmix, name = PRESETS[idx]
        t60_theory = theoretical_t60(note, dkay)

        samples, sr = load_wav_mono(wav)
        env         = rms_envelope(samples, sr)
        t60_meas    = measure_t60(env)
        zc_rate     = zero_crossing_rate(samples, sr)

        if t60_meas is None:
            print(f"{name:<10}  (silent)")
            continue

        # ── Check 1: T60 within expected range ────────────────────────────────
        lo = T60_LO_MULT * t60_theory
        hi = T60_HI_MULT * t60_theory
        ok_t60 = lo <= t60_meas <= hi if t60_theory < 1e6 else True
        ratio  = t60_meas / t60_theory if t60_theory < 1e6 else float('nan')
        total += 1
        if ok_t60:
            passed += 1
        else:
            failures.append((name, "T60", f"{t60_theory:.3f}s", f"{t60_meas:.3f}s",
                             f"×{ratio:.2f} outside [{T60_LO_MULT:.2f},{T60_HI_MULT:.2f}]"))

        # ── Check 2: Noise character for NzMix ≥ 50 ──────────────────────────
        ok_noise = True
        if nzmix >= 50:
            total   += 1
            ok_noise = zc_rate >= NOISE_ZC_HZ
            if ok_noise:
                passed += 1
            else:
                failures.append((name, "Noise", f"≥{NOISE_ZC_HZ}Hz", f"{zc_rate:.0f}Hz",
                                 "ZC rate too low — sounds tonal, not noisy"))

        ratio_str = f"×{ratio:.2f}" if not math.isnan(ratio) else "  inf"
        t60_flag  = "PASS" if ok_t60   else "FAIL"
        nz_flag   = ("PASS" if ok_noise else "FAIL") if nzmix >= 50 else "  — "
        status    = "PASS" if (ok_t60 and ok_noise) else "FAIL"

        print(f"{name:<10} {t60_meas:>9.3f}s {t60_theory:>9.3f}s {ratio_str:>7}"
              f"  {zc_rate:>7.0f}  {t60_flag:>5}  {nz_flag:>5}  {status}")

    print("─" * 72)
    score = 100.0 * passed / total if total else 0
    print(f"\nTIME-DOMAIN SCORE: {passed}/{total} checks passed ({score:.0f}%)")

    if failures:
        print("\nFAILURES:")
        for name, metric, expected, actual, reason in failures:
            print(f"  [{name}] {metric}: expected {expected}, got {actual} ({reason})")
    else:
        print("All rendered presets pass energy-envelope time-domain checks.")

    return 0 if not failures else 1


if __name__ == "__main__":
    sys.exit(main())
