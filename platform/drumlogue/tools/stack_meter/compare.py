#!/usr/bin/env python3
"""Gain-compensated distortion between a processed render and a linear one.

Fits the best scalar gain per 512-frame window (10.7 ms: long enough that the
fit cannot absorb audio-rate distortion, short enough to follow a limiter), then
reports the residual the gain cannot explain.  That separates "the output stage
turned it down", which is not heard as distortion, from "the output stage bent
the waveform", which is the harshness.

Windows whose reference energy is more than 60 dB under the loudest window are
skipped: they are the decayed tail, where a per-window gain fit is meaningless.
"""
import array, math, sys

def load(p):
    a = array.array('f')
    with open(p, 'rb') as f:
        a.frombytes(f.read())
    return a

proc, ref, scale = load(sys.argv[1]), load(sys.argv[2]), float(sys.argv[3])
# The limiter has a look-ahead delay; align the processed render to the
# reference before fitting, or the fit sees a phase error as distortion.
delay = int(sys.argv[4]) * 2 if len(sys.argv) > 4 else 0
if delay:
    proc = proc[delay:]
n = min(len(proc), len(ref))
win = int(sys.argv[5]) * 2 if len(sys.argv) > 5 else 1024
starts = range(0, n - win + 1, win)
energy = [sum(ref[i] * ref[i] for i in range(w, w + win)) for w in starts]
gate = max(energy) * 1e-6 if energy else 0.0

err = sig = 0.0
gains = []
for w, e in zip(starts, energy):
    if e <= gate:
        continue
    num = den = 0.0
    for i in range(w, w + win):
        r = ref[i] * scale
        num += proc[i] * r
        den += r * r
    a = num / den
    gains.append(a)
    for i in range(w, w + win):
        r = a * ref[i] * scale
        d = proc[i] - r
        err += d * d
        sig += r * r
if not gains or sig <= 0:
    print("  (no usable window)")
else:
    print(f"  distortion {10 * math.log10(err / sig):7.2f} dB    "
          f"gain {20 * math.log10(max(max(gains), 1e-9)):6.2f} .. "
          f"{20 * math.log10(max(min(gains), 1e-9)):6.2f} dB")
