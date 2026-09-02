#!/usr/bin/env python3
"""
Harmonic cost of an output-stage change, from the WAVs run.sh writes.

`run.sh <unit> <note> <vel> <presets> <wavdir>` renders one WAV per preset.
Render a baseline and a candidate into two directories and compare them here:
loudness says what a change bought, this says what it cost.

    ./run.sh ../../brachetti 60 127 3 /tmp/base
    EXTRA_FLAGS=-DSOME_GAIN=2.0f ./run.sh ../../brachetti 60 127 3 /tmp/cand
    ./harmonics.py /tmp/base /tmp/cand

For each preset it locates the fundamental in the sustained body (the first
40 ms are skipped so the strike transient does not dominate) and reports H2-H5
relative to it, plus the total energy above 250 Hz.  A memoryless waveshaper
on a low-frequency decay shows up here as a large rise in the odd harmonics
and in the >250 Hz total -- that is the "brittle / distorted long decay"
failure mode, and it is why loudness alone is not enough to judge a master
stage by.

Requires numpy.
"""
import os
import sys
import wave

try:
    import numpy as np
except ImportError:
    sys.exit("harmonics.py needs numpy: pip install numpy")

SKIP_S = 0.04      # step over the strike transient
WINDOW_S = 0.50    # analyse this much of the body
NFFT = 1 << 17
# Fundamental search band.  Override with e.g. HARM_F0_BAND=25:300 when a
# preset's fundamental falls outside it -- on a kick this has to stay low
# enough to find the boom rather than a body partial.
F0_LO, F0_HI = (float(v) for v in os.environ.get("HARM_F0_BAND", "25:300").split(":"))


def load_mono(path):
    with wave.open(path, "rb") as w:
        if w.getsampwidth() != 2:
            raise ValueError(f"{path}: expected 16-bit PCM")
        fs = w.getframerate()
        ch = w.getnchannels()
        raw = w.readframes(w.getnframes())
    a = np.frombuffer(raw, dtype="<i2").astype(np.float64) / 32768.0
    return a.reshape(-1, ch)[:, 0], fs


def analyse(x, fs, f0_hint=None):
    seg = x[int(SKIP_S * fs):int((SKIP_S + WINDOW_S) * fs)]
    if len(seg) < 1024 or not np.any(seg):
        return None
    sp = np.abs(np.fft.rfft(seg * np.hanning(len(seg)), NFFT))
    fr = np.fft.rfftfreq(NFFT, 1.0 / fs)

    def peak_near(f, tol=0.06):
        m = (fr > f * (1 - tol)) & (fr < f * (1 + tol))
        return sp[m].max() if m.any() else 1e-15

    # Pin the candidate to the baseline's fundamental: a changed output stage
    # can add enough low-frequency energy to move a naive argmax onto a
    # different partial, which would compare two different things.
    if f0_hint is not None:
        f0 = f0_hint
    else:
        band = (fr > F0_LO) & (fr < F0_HI)
        f0 = fr[band][np.argmax(sp[band])]

    h1 = peak_near(f0)
    harm = [20 * np.log10(peak_near(f0 * k) / h1) for k in (2, 3, 4, 5)]
    hi = fr > 250.0
    total_hi = 10 * np.log10((sp[hi] ** 2).sum() / (h1 ** 2))
    return f0, harm, total_hi


def main(argv):
    if len(argv) != 3:
        sys.exit(__doc__)
    base_dir, cand_dir = argv[1], argv[2]
    names = sorted(f for f in os.listdir(base_dir) if f.endswith(".wav"))
    if not names:
        sys.exit(f"no WAVs in {base_dir}")

    print(f"{'preset':22s} {'f0 Hz':>7s} "
          f"{'H2':>7s} {'H3':>7s} {'H4':>7s} {'H5':>7s} {'>250Hz':>8s}")
    for name in names:
        cand_path = os.path.join(cand_dir, name)
        if not os.path.exists(cand_path):
            continue
        bx, fs = load_mono(os.path.join(base_dir, name))
        cx, _ = load_mono(cand_path)
        b = analyse(bx, fs)
        if b is None:
            continue
        c = analyse(cx, fs, f0_hint=b[0])
        label = os.path.splitext(name)[0]
        print(f"{label:22s} {b[0]:7.1f} "
              + " ".join(f"{v:7.1f}" for v in b[1]) + f" {b[2]:8.1f}   baseline")
        print(f"{'':22s} {'':7s} "
              + " ".join(f"{v:7.1f}" for v in c[1]) + f" {c[2]:8.1f}   candidate")
        print(f"{'':22s} {'delta':>7s} "
              + " ".join(f"{c[1][i] - b[1][i]:+7.1f}" for i in range(4))
              + f" {c[2] - b[2]:+8.1f}")
        print()


if __name__ == "__main__":
    main(sys.argv)
