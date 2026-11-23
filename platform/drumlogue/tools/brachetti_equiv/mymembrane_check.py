#!/usr/bin/env python3
"""mymembrane_check.py -- BrcLippa's port of mymembrane~ against Pd itself.

Renders Miguel Moreno's mymembrane~ (pd-mkmr, BSD-3-Clause) with Pure Data,
headless, at the settings of its help patch and its demo's four instruments
(velocities 127 and 90), renders BrcLippa's presets 3-7 (MmSnare, MmKick,
MmSnr2, MmTom, MmFloor: the same settings) through the unit's own strike path
(preset_probe match: the sequencer gate at the root, before the limiter), and
compares them: waveform correlation over the first 200 ms, the 40 dB decay,
and the periodicity (match_samples.periodicity).

    git clone https://github.com/MikeMorenoDSP/pd-mkmr /tmp/pd-mkmr
    apt-get install puredata-core
    python3 mymembrane_check.py /tmp/pd-mkmr/instruments

Passes (exit 0) when every correlation is at least 0.93 and the decays and
periodicities agree (10 %, 0.03).  Pd renders in real time (-nosound), about
3.5 s per setting.
"""
import os
import shutil
import subprocess
import sys
import tempfile

import numpy as np
import scipy.io.wavfile as wavfile

HERE = os.path.dirname(os.path.abspath(__file__))
DRUM = os.path.abspath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, HERE)
import match_samples as M  # noqa: E402

# name: (freq, feedback, damping, apgain, apfreq, noise), from mymembrane~-help.pd
# and mymembrane~-demo.pd; the BrcLippa preset index that plays it.
SETTINGS = [
    ("help", (277.6, 0.7908, 0.4409, 0.6378, 169.2, 0.1018), 3, "MmSnare"),
    ("demo1", (109.7, 1.0, 0.07874, 1.0, 500.0, 0.01786), 4, "MmKick"),
    ("demo2", (245.3, 0.8677, 0.4016, 0.3071, 174.5, 0.1266), 5, "MmSnr2"),
    ("demo3", (314.2, 0.9214, 0.2362, -0.5591, 257.0, 0.01156), 6, "MmTom"),
    ("demo4", (134.1, 0.9838, 0.748, 0.3701, 309.4, 0.001), 7, "MmFloor"),
]
STRIKE_DELAY = 0.012   # the patch below triggers at 10 ms; mymembrane~ strikes 2 ms later


def pd_patch(wav, vel, f, fb, dmp, apg, apf, nse):
    """A patch that strikes mymembrane~ once and writes its outlet to wav.
    mymembrane~'s inlets: 0 velocity/trigger, 1 frequency, 2 feedback,
    3 damping, 4 all-pass gain, 5 all-pass frequency, 6 noise, 7 clear."""
    return "\n".join([
        "#N canvas 0 0 600 400 10;",
        "#X obj 10 10 loadbang;",                          # 0
        "#X obj 10 30 t b b b;",                           # 1
        "#X msg 200 60 open -bytes 4 %s \\, start;" % wav,  # 2
        "#X obj 200 200 writesf~ 1;",                      # 3
        "#X obj 10 200 mymembrane~;",                      # 4
        "#X msg 60 60 %g;" % f, "#X msg 60 80 %g;" % fb, "#X msg 60 100 %g;" % dmp,   # 5 6 7
        "#X msg 60 120 %g;" % apg, "#X msg 60 140 %g;" % apf, "#X msg 60 160 %g;" % nse,  # 8 9 10
        "#X msg 60 180 1;",                                # 11: clear on
        "#X obj 10 100 del 10;",                           # 12
        "#X msg 10 120 %d;" % vel,                         # 13
        "#X obj 300 30 del 3000;",                         # 14
        "#X msg 300 60 stop;",                             # 15
        "#X msg 300 90 \\; pd quit;",                      # 16
        "#X obj 300 120 del 600;",                         # 17
        "#X msg 120 30 \\; pd dsp 1;",                     # 18
        "#X connect 0 0 1 0;", "#X connect 1 2 18 0;",
        "#X connect 1 2 2 0;", "#X connect 2 0 3 0;",
        "#X connect 1 1 5 0;", "#X connect 1 1 6 0;", "#X connect 1 1 7 0;", "#X connect 1 1 8 0;",
        "#X connect 1 1 9 0;", "#X connect 1 1 10 0;", "#X connect 1 1 11 0;",
        "#X connect 5 0 4 1;", "#X connect 6 0 4 2;", "#X connect 7 0 4 3;", "#X connect 8 0 4 4;",
        "#X connect 9 0 4 5;", "#X connect 10 0 4 6;", "#X connect 11 0 4 7;",
        "#X connect 1 0 12 0;", "#X connect 12 0 13 0;", "#X connect 13 0 4 0;",
        "#X connect 4 0 3 0;",
        "#X connect 0 0 14 0;", "#X connect 14 0 15 0;", "#X connect 15 0 3 0;",
        "#X connect 14 0 17 0;", "#X connect 17 0 16 0;",
    ]) + "\n"


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    inst = os.path.abspath(sys.argv[1])
    work = tempfile.mkdtemp(prefix="mmcheck_")
    ok = True
    try:
        unit = os.path.join(DRUM, "brachetti_split", "brachetti_lippa")
        probe = os.path.join(work, "probe")
        subprocess.run(["g++", "-std=c++17", "-O2", "-w", "-I" + unit, "-I" + DRUM,
                        "-I" + os.path.join(DRUM, "common"), "-DRUNTIME_COMMON_H_",
                        os.path.join(HERE, "preset_probe.cpp"), "-o", probe], check=True)
        for vel in (127, 90):
            out = os.path.join(work, "unit%d" % vel)
            os.makedirs(out)
            subprocess.run([probe, "match", "3", "7", str(vel), "2.9", out], check=True, capture_output=True)
            for name, (f, fb, dmp, apg, apf, nse), idx, preset in SETTINGS:
                wav = os.path.join(work, "%s_%d.wav" % (name, vel))
                pf = os.path.join(work, "render.pd")
                open(pf, "w").write(pd_patch(wav, vel, f, fb, dmp, apg, apf, nse))
                subprocess.run(["pd", "-nogui", "-nosound", "-r", "48000", "-path", inst, "-open", pf],
                               capture_output=True, timeout=60)
                _, r = wavfile.read(wav)
                r = r.astype(np.float64)[int(STRIKE_DELAY * M.SR):]
                _, x = wavfile.read(os.path.join(out, "%d.wav" % idx))
                x = x.astype(np.float64)
                n = int(0.2 * M.SR)
                a, b = x[:n], r[:n]
                corr = float(np.dot(a, b) / np.sqrt(np.dot(a, a) * np.dot(b, b)))
                m = min(len(x), len(r))
                tx, tr = M.t40(x[:m]) * 1000, M.t40(r[:m]) * 1000
                px, pr = M.periodicity(x), M.periodicity(r)
                good = corr >= 0.93 and abs(tx - tr) <= 0.1 * tr + 5 and abs(px - pr) <= 0.03
                ok &= good
                print("%-8s v%-3d corr %.3f  t40 %4.0f / %4.0f ms  periodicity %.2f / %.2f  %s"
                      % (preset, vel, corr, tx, tr, px, pr, "ok" if good else "DIFFERENT"))
    finally:
        shutil.rmtree(work, ignore_errors=True)
    print("mymembrane~ port: " + ("matches Pd" if ok else "DIFFERS from Pd"))
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
