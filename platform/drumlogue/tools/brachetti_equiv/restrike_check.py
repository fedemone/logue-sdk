#!/usr/bin/env python3
"""restrike_check.py -- does striking a voice that still rings click?

    ./restrike_check.py lippa [pizzico ...]        (unit names: brachetti_<name>)

For every preset of each unit and gaps of 30, 60, 150, 300 and 600 ms, two
strikes on ONE voice (Poly 1: the slot a fast pattern reuses once Poly is
reached), through the unit's own path and master stage (strike_roll.cpp).
The second strike's attack is read as the peak of the output above 6 kHz
over its first 10 ms, and compared with what a clean restrike would bring:
the same second strike on the same slot once the first has died (2.5 s
later: the same exciter noise, which follows the slot's seed) plus the first
strike's own ring at that moment (it alone, same window).  A click is far
above the sum; a clean restrike is at or below it (two attacks rarely peak
together).  Prints restrike / (strike + ring) per preset and gap; exits 1 if
any exceeds 1.3.  The limiter is in the path, as on the drumlogue.

This is how BrcLippa's MmFloor click was found (2.0-2.9x at 30-150 ms:
mymembrane~'s attack boost acting on the ring) and checked fixed (BrcLippa README,
"Restrikes and CPU on mymembrane~").
"""
import os
import re
import subprocess
import sys
import tempfile

import numpy as np
import scipy.signal as sg

HERE = os.path.dirname(os.path.abspath(__file__))
DRUM = os.path.abspath(os.path.join(HERE, "..", ".."))
SR = 48000
GAPS = (30, 60, 150, 300, 600)
FIRST_MS = 150.0     # silence before the first strike: its attack is then whole
LIMIT = 1.3
B, A = sg.butter(4, 6000 / (SR / 2), "high")


def build(unit, work):
    udir = os.path.join(DRUM, "brachetti_split", "brachetti_" + unit)
    exe = os.path.join(work, "roll_" + unit)
    subprocess.run(["g++", "-std=c++17", "-O2", "-w", "-I" + udir, "-I" + DRUM, "-I" + os.path.join(DRUM, "common"),
                    "-DRUNTIME_COMMON_H_", os.path.join(HERE, "strike_roll.cpp"), "-o", exe], check=True)
    txt = open(os.path.join(udir, "presets.h")).read()
    body = txt[txt.index("kPresetNames[] = {"):]
    body = body[:body.index("};")]
    names = re.findall(r'"([^"]+)"', body)
    return exe, names


def attack(exe, preset, gap, hits, poly, t, work):
    out = os.path.join(work, "x.f32")
    subprocess.run([exe, str(preset), str(gap), str(hits), "110", str(poly), "%.3f" % (t + 0.02), out, "60",
                    str(FIRST_MS)], check=True)
    x = np.fromfile(out, dtype=np.float32).astype(np.float64)
    h = sg.lfilter(B, A, x)
    i = int(t * SR)
    return float(np.abs(h[i - 48:i + int(0.01 * SR)]).max())


def main():
    units = sys.argv[1:] or ["lippa"]
    work = tempfile.mkdtemp(prefix="restrike_")
    worst = 0.0
    for unit in units:
        exe, names = build(unit, work)
        print("=== brachetti_%s: restrike / (the strike on a silent voice + the ring), above 6 kHz, by gap (ms)" % unit)
        print("%-9s" % "" + "".join("%7d" % g for g in GAPS))
        for p, nm in enumerate(names):
            t0 = FIRST_MS / 1000.0
            later = t0 + 2.5
            alone = attack(exe, p, 2500, 2, 1, later, work)     # the second strike, ring gone
            row = []
            for g in GAPS:
                t = t0 + g / 1000.0
                ring = attack(exe, p, g, 1, 1, t, work)           # the first strike's ring alone
                same = attack(exe, p, g, 2, 1, t, work)
                row.append(same / max(alone + ring, 1e-9))
            worst = max(worst, max(row))
            print("%-9s" % nm + "".join("%7.2f" % r for r in row))
    print("worst %.2f (limit %.2f)" % (worst, LIMIT))
    sys.exit(0 if worst <= LIMIT else 1)


if __name__ == "__main__":
    main()
