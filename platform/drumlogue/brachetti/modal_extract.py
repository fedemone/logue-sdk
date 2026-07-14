#!/usr/bin/env python3
"""modal_extract.py — extract physical modal parameters from a reference sample.

Implements the analysis half of the DAFx2020 "Advanced Fourier Decomposition for
Realistic Drum Synthesis" method (Werner et al.): high-resolution spectral peak
tracking to recover each resonant mode's frequency, plus a per-mode Short-Time
Fourier energy-decay fit to recover its T60.  The output (mode ratios + T60 in ms)
maps DIRECTLY onto a row of `modal_preset_configs[]` in synth_engine.h
(ratio2..ratio6, t60_1..t60_4, env1..env6), so a preset can be tuned from the
measured physics of its reference sample instead of by ear.

Usage:
    python3 modal_extract.py samples/Orchestral-Timpani-C.wav
    python3 modal_extract.py samples/Taiko-Hit.wav --nmodes 10 --fmax 4000

Notes:
  * "ratio" is freq / fundamental — paste these into ratio2..ratio6.
  * "T60ms" is the -60 dB time of that mode's energy envelope — paste into
    t60_1..t60_4 (modes 5/6 inherit 0.85x/0.70x of t60_4 in the engine).
  * Quiet, closely-spaced upper modes (small ratio gaps + high amp) are the
    usual source of audible beating / "roughness"; keep their env low.
"""
import argparse
import numpy as np
import scipy.io.wavfile as wav
from scipy.signal import find_peaks


def load(path):
    sr, data = wav.read(path)
    if data.ndim > 1:
        data = data.mean(axis=1)
    data = data.astype(np.float64)
    data /= (np.max(np.abs(data)) + 1e-12)
    return sr, data


def analyze(path, fmin=40.0, fmax=4000.0, nmodes=10):
    sr, x = load(path)
    n = len(x)
    print(f"\n=== {path}  ({n / sr * 1000:.0f} ms, sr={sr}) ===")
    # High-resolution global spectrum from the first 0.5 s (steady mode picture).
    seg = x[:min(n, int(0.5 * sr))]
    nfft = 1 << 16
    w = np.hanning(len(seg))
    sp = np.abs(np.fft.rfft(seg * w, nfft))
    fr = np.fft.rfftfreq(nfft, 1 / sr)
    band = (fr >= fmin) & (fr <= fmax)
    spb = sp * band
    min_dist = int(25 * nfft / sr)  # >= 25 Hz between peaks
    pk, _ = find_peaks(spb, distance=min_dist, prominence=spb.max() * 0.02)
    pk = pk[np.argsort(spb[pk])[::-1][:nmodes]]
    pk = np.sort(pk)

    # Per-mode T60 via STFT bin-energy decay fit.
    fs, hop = 4096, 512
    nfr = (n - fs) // hop
    res = []
    for pb in pk:
        f0 = fr[pb]
        amps, ts = [], []
        for i in range(nfr):
            s = i * hop
            fwin = x[s:s + fs] * np.hanning(fs)
            ff = np.abs(np.fft.rfft(fwin))
            fb = np.fft.rfftfreq(fs, 1 / sr)
            bi = np.argmin(np.abs(fb - f0))
            a = np.sqrt(np.sum(ff[max(0, bi - 1):bi + 2] ** 2))  # small neighbourhood
            amps.append(max(a, 1e-9))
            ts.append(s / sr)
        amps, ts = np.array(amps), np.array(ts)
        peaki = np.argmax(amps)
        la, tt = np.log(amps[peaki:]), ts[peaki:]
        if len(tt) > 3:
            slope = np.polyfit(tt, la, 1)[0]
            t60 = (np.log(1e-3) / slope) if slope < 0 else 9.9
        else:
            t60 = 9.9
        res.append((f0, amps.max(), max(0.0, t60)))

    f1 = res[0][0]
    amax = max(r[1] for r in res)
    print(f"fundamental ~ {f1:.1f} Hz")
    print(f"{'ratio':>7} {'freq':>8} {'amp':>6} {'T60ms':>7}")
    for f0, a, t60 in res:
        print(f"{f0 / f1:7.3f} {f0:8.1f} {a / amax:6.2f} {t60 * 1000:7.0f}")
    return res


if __name__ == "__main__":
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("wav", nargs="+", help="reference WAV file(s)")
    ap.add_argument("--fmin", type=float, default=40.0)
    ap.add_argument("--fmax", type=float, default=4000.0)
    ap.add_argument("--nmodes", type=int, default=10)
    args = ap.parse_args()
    for p in args.wav:
        analyze(p, args.fmin, args.fmax, args.nmodes)
