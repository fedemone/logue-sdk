#!/usr/bin/env python3
"""knob_matrix.py — fold knob_matrix.cpp's scores into the README matrix.

knob_matrix.cpp scores every preset x knob x point as the largest change, in
dB, of any unmasked third-octave band in the attack, body or tail window
(see its header).  Per preset a knob is

    live    >= 3 dB at some point of its travel
    subtle  0.5-3 dB
    inert   under 0.5 dB at every point (bit-identical included)

and per family (the README matrix's columns, membership from its footnotes):

    ●  live on most of the family (a strict majority of its members)
    ○  inert on every member
    ◐  anything between — subtle everywhere, or live on some members only

This is the evidence for a cell, not the whole of it: one velocity, one note,
one strike per preset.  The README's footnoted cells (Poly, the cymbal Partls
density, Resnc's need for a lowered Cutoff, the kick's 808Sub-only dive) carry
knowledge a single strike cannot, and the code is still the explanation.

Usage:  python3 knob_matrix.py <csv from knob_matrix>   [--members]
"""
import csv
import sys
from collections import defaultdict

FAMILIES = [
    ("Kick",   [0, 2, 20]),
    ("Tom",    [6, 12, 23, 27, 28, 33, 39]),
    ("Kernel", [5, 7]),
    ("Snare",  [3, 8, 37, 38]),
    ("Bar",    [1, 4, 10, 11, 15, 16, 17, 24, 30, 34]),
    ("Plate",  [18, 19, 29, 35]),
    ("Cymbal", [13, 14, 26, 31, 32, 36]),
    ("Noise",  [21, 22, 25]),
    ("String", [9]),
]
KNOBS = ["Velocity", "MlltRes", "MlltStif", "VlMllRes", "VlMllStf", "Partls", "Model",
         "Dkay", "Mterl", "Tone", "HitPos", "Rel", "Inharm", "TubRad", "Resnc"]


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    best = defaultdict(float)
    names = {}
    with open(sys.argv[1]) as f:
        for r in csv.DictReader(f):
            key = (int(r["preset"]), r["knob"])
            best[key] = max(best[key], float(r["band_db"]))
            names[int(r["preset"])] = r["name"]
    covered = {p for _, ps in FAMILIES for p in ps}
    missing = sorted(set(names) - covered)
    if missing:
        sys.exit("presets in no family: %s" % missing)

    def state(p, k):
        v = best[(p, k)]
        return "live" if v >= 3.0 else ("subtle" if v >= 0.5 else "inert")

    print("%-9s" % "knob" + "".join("%-8s" % f for f, _ in FAMILIES))
    lines = []
    for k in KNOBS:
        row = "%-9s" % k
        for fam, ps in FAMILIES:
            st = [state(p, k) for p in ps]
            live = st.count("live")
            if live * 2 > len(ps):
                g = "●"
            elif all(s == "inert" for s in st):
                g = "○"
            else:
                g = "◐"
            row += "%-8s" % g
            lines.append("%-6s %-9s %s" % (fam, k, "  ".join(
                "%s %.1f" % (names[p], best[(p, k)]) for p in ps)))
        print(row)
    if "--members" in sys.argv:
        print("\nper member: largest band change in dB over the knob's travel")
        print("\n".join(lines))


if __name__ == "__main__":
    main()
