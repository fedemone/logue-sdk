#!/usr/bin/env python3
"""compare_dumps.py — numeric comparison of two equiv_driver dumps.

    python3 compare_dumps.py script a.out a.f32 b.out b.f32 [--worst N]

For builds that cannot be bit-identical (two -ffast-math ARM builds: the
compiler is free to fuse and reorder float operations differently around
different code), report the largest absolute sample difference and the
difference energy relative to the signal — overall, and for the worst
renders by name, so a difference can be traced to the scenario that makes it.
"""
import argparse
import sys

import numpy as np


def render_lengths(script):
    out = []
    for line in open(script):
        w = line.split("#")[0].split()
        if len(w) == 2 and w[0] == "render":
            out.append((int(w[1]) + 63) // 64 * 64)
    return out


def names(listing):
    return ["%s#%s" % tuple(line.split()[:2]) for line in open(listing)]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("script")
    ap.add_argument("a_out")
    ap.add_argument("a_f32")
    ap.add_argument("b_out")
    ap.add_argument("b_f32")
    ap.add_argument("--worst", type=int, default=0)
    a_ = ap.parse_args()
    a = np.fromfile(a_.a_f32, dtype=np.float32).astype(np.float64)
    b = np.fromfile(a_.b_f32, dtype=np.float32).astype(np.float64)
    na, nb = names(a_.a_out), names(a_.b_out)
    lens = render_lengths(a_.script)
    if na != nb or a.size != b.size or sum(lens) * 2 != a.size:
        sys.exit("the two runs did not render the same spans")
    d = a - b
    sig = np.sqrt(np.mean(a * a))
    rel = np.sqrt(np.mean(d * d)) / sig if sig > 0 else 0.0
    rows, pos = [], 0
    for name, n in zip(na, lens):
        sa, sd = a[pos:pos + 2 * n], d[pos:pos + 2 * n]
        pos += 2 * n
        e = np.sqrt(np.mean(sa * sa))
        if e > 1e-4:
            rows.append((20 * np.log10(np.sqrt(np.mean(sd * sd)) / e + 1e-30),
                         np.abs(sd).max(), name))
    rows.sort(reverse=True)
    worst = rows[0][0] if rows else -999.0
    print("samples %d  max|diff| %.3g  diff/signal %.1f dB overall, %.1f dB worst render"
          % (a.size, np.abs(d).max(), 20 * np.log10(rel + 1e-30), worst))
    for db, mx, name in rows[:a_.worst]:
        print("  %7.1f dB  max %.3g  %s" % (db, mx, name))


if __name__ == "__main__":
    main()
