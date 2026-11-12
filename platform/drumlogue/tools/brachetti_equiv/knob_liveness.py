#!/usr/bin/env python3
"""knob_liveness.py — how much each knob of a Brachetti unit changes each preset.

    ./knob_liveness.py tom noise ...      # unit names (brachetti_<name>)

For every preset, each knob is set to its min and to its max (header.c) and
the render compared with the preset as shipped (knob_liveness.cpp: a strike
at velocity 110, then a held MIDI note at 70).  The score is the mean
absolute change, in dB, of the energy in 8 octave bands (150 Hz .. 24 kHz)
over 7 time windows, floored 70 dB under the loudest — the larger of the two
ends.  Printed as a table: '.' under 0.3 dB (nothing to hear), ':x' under
1.5 dB (subtle), else the number.  The master knobs (Tone, Cutoff, Resnc,
Gain) are left out: they are live on everything.

It measures a SINGLE knob from the shipped state, so a knob that acts on a
part a preset has switched off (NzFltr with no noise) reads '.' until that
part is turned up; and it averages over 43 ms frames, so amplitude
modulation (BrcNoise's grains) reads smaller than it sounds.  It is how the
knob reviews in the units' READMEs were measured.  Host build, needs numpy.
"""
import os
import re
import subprocess
import sys
import tempfile

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
DRUM = os.path.abspath(os.path.join(HERE, "..", ".."))
MASTER = {"Program", "Tone", "Cutoff", "Resnc", "Gain"}
SECONDS = 4.0
SEGMENTS = [(0, .05), (.05, .2), (.2, .6), (.6, 1.8), (2, 2.15), (2.15, 2.6), (2.6, 4)]
EDGES = [0, 150, 300, 600, 1200, 2400, 4800, 9600, 24000]


def knobs(unit):
    """(slot, min, max, name) of every named knob in header.c."""
    txt = open(os.path.join(DRUM, "brachetti_" + unit, "header.c")).read()
    out = []
    rx = r'\{(-?\d+),\s*(-?\d+),\s*(-?\d+),\s*(-?\d+),\s*k_unit_param_type_\w+,[^{]*\{"(\w*)"\}\}'
    for slot, m in enumerate(re.finditer(rx, txt)):
        if m.group(5) and m.group(5) not in MASTER:
            out.append((slot, int(m.group(1)), int(m.group(2)), m.group(5)))
    return out


def features(x):
    n, hop = 2048, 1024
    frames = np.lib.stride_tricks.sliding_window_view(x, n)[::hop] * np.hanning(n)
    spec = np.abs(np.fft.rfft(frames, axis=1)) ** 2
    f = np.fft.rfftfreq(n, 1 / 48000)
    bands = np.stack([spec[:, (f >= a) & (f < b)].sum(1) for a, b in zip(EDGES[:-1], EDGES[1:])], 1)
    t = (np.arange(len(frames)) * hop + n / 2) / 48000
    e = np.stack([bands[(t >= a) & (t < b)].sum(0) + 1e-12 for a, b in SEGMENTS])
    return 10 * np.log10(e)


def change(a, b):
    floor = a.max() - 70
    return float(np.mean(np.abs(np.maximum(a, floor) - np.maximum(b, floor))))


def cell(x):
    return "." if x < 0.3 else (":%.1f" % x if x < 1.5 else "%.1f" % x)


def main():
    for unit in sys.argv[1:] or ["tom"]:
        udir = os.path.join(DRUM, "brachetti_" + unit)
        ks = knobs(unit)
        with tempfile.TemporaryDirectory() as work:
            exe = os.path.join(work, "live")
            subprocess.check_call(["g++", "-std=c++17", "-O2", "-w", "-I" + udir, "-I" + DRUM,
                                   "-I" + os.path.join(DRUM, "common"), "-DRUNTIME_COMMON_H_",
                                   os.path.join(HERE, "knob_liveness.cpp"), "-o", exe])
            listing = "".join("%d %d %d %s\n" % k for k in ks)
            n = int(subprocess.run([exe, str(SECONDS), work], input=listing, text=True,
                                   capture_output=True, cwd=udir, check=True).stdout)
            names = re.findall(r'"([^"]+)"', open(os.path.join(udir, "presets.h")).read()
                               .split("kPresetNames")[1].split(";")[0])
            print("=== brachetti_%s   (mean |dB| change at the knob's ends; '.' < 0.3, ':' < 1.5)" % unit)
            print("%-8s " % "" + " ".join("%7s" % k[3][:7] for k in ks))
            for p in range(n):
                base = features(np.fromfile(os.path.join(work, "p%d_base.raw" % p), np.float32))
                row = []
                for slot, _, _, _ in ks:
                    row.append(max(change(base, features(np.fromfile(
                        os.path.join(work, "p%d_k%d_%s.raw" % (p, slot, end)), np.float32)))
                        for end in ("lo", "hi")))
                print("%-8s " % names[p][:8] + " ".join("%7s" % cell(x) for x in row))
        sys.stdout.flush()


if __name__ == "__main__":
    main()
