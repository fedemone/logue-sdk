#!/usr/bin/env python3
"""note_audit.py — measure what each preset's shipped Note actually sounds like.

Companion to note_audit.cpp:

    g++ -std=c++17 -O2 -I. -I.. -I../../common -I../common -DRUNTIME_COMMON_H_ \
        note_audit.cpp -o note_audit
    ./note_audit /tmp/na > /tmp/na.csv
    python3 note_audit.py /tmp/na.csv

For every preset it prints the shipped Note, its equal-tempered frequency, and
three measurements taken from the render of THAT note:

  peak_hz     strongest spectral peak over the whole hit (the partial a
              listener locks onto for a pitched instrument)
  body_hz     strongest peak below 2 kHz (the drum/body pitch, which on a
              bright percussion sound is not the overall peak)
  centroid    spectral centroid, i.e. "brightness"
  +12         how far the strongest peak ACTUALLY moves when the Note
              parameter is raised an octave.  ~+12.0 = the preset tracks its
              note; ~0.0 = the Note knob is inert on this preset and the
              displayed pitch is decoration.
  +12c        the same octave shift measured on the spectral CENTROID.  Read
              this one for the cymbal family: peak-picking a dense inharmonic
              wash latches onto a different anchor after a transpose and
              reports nonsense (Ride "+57"), while the centroid moves with the
              whole spectrum.

`peak/nominal` in semitones is the number to read: a pitched instrument whose
note is doing its job lands near 0, ±12 (an octave — the note names the same
pitch class), or on a documented inharmonic partial.  A large, non-octave
offset means the Note column does not name the pitch the preset produces, so
the display lies and transposing from the sequencer starts from the wrong
place.
"""

import csv
import math
import sys

import numpy as np
from scipy.io import wavfile


def load(path):
    sr, x = wavfile.read(path)
    x = x.astype(np.float64)
    if x.ndim > 1:
        x = x[:, 0]
    if np.max(np.abs(x)) > 0:
        x = x / np.max(np.abs(x))
    return sr, x


def spectrum(sr, x):
    # Hann-windowed magnitude spectrum of the first 1.5 s (the whole strike +
    # early tail; long tails add nothing to a pitch estimate).
    n = min(len(x), int(1.5 * sr))
    seg = x[:n] * np.hanning(n)
    mag = np.abs(np.fft.rfft(seg))
    freqs = np.fft.rfftfreq(n, 1.0 / sr)
    return freqs, mag


def measure(path):
    sr, x = load(path)
    freqs, mag = spectrum(sr, x)
    band = freqs > 25.0                       # ignore DC/rumble
    peak_hz = freqs[band][int(np.argmax(mag[band]))]
    low = band & (freqs < 2000.0)
    body_hz = freqs[low][int(np.argmax(mag[low]))] if mag[low].size else 0.0
    centroid = float(np.sum(freqs * mag) / np.sum(mag)) if np.sum(mag) > 0 else 0.0
    return peak_hz, body_hz, centroid


def semis(a, b):
    if a <= 0 or b <= 0:
        return float("nan")
    return 12.0 * math.log2(a / b)


def main():
    csv_path = sys.argv[1] if len(sys.argv) > 1 else "note_audit.csv"
    rows = list(csv.DictReader(open(csv_path)))

    hdr = (f"{'idx':>3} {'name':<9} {'note':>4} {'nominal':>8} {'engine':<9} "
           f"{'peak_hz':>8} {'body_hz':>8} {'centroid':>8} {'peak/nom':>9} "
           f"{'body/nom':>9} {'+12':>6} {'+12c':>6}")
    print(hdr)
    print("-" * len(hdr))
    for r in rows:
        if not r["wav"]:
            continue
        nominal = float(r["nominal_hz"])
        peak_hz, body_hz, centroid = measure(r["wav"])
        track = float("nan")
        track_c = float("nan")
        if r.get("wav_up12"):
            up_peak, up_body, up_centroid = measure(r["wav_up12"])
            track_c = semis(up_centroid, centroid)
            # Track whichever band carries the preset's pitch: on a bright
            # percussion sound the overall peak is noise, the body is the drum.
            track = semis(up_body, body_hz) if abs(semis(up_body, body_hz)) > abs(
                semis(up_peak, peak_hz)) else semis(up_peak, peak_hz)
        print(f"{r['idx']:>3} {r['name']:<9} {r['note']:>4} {nominal:>8.1f} "
              f"{r['engine']:<9} {peak_hz:>8.1f} {body_hz:>8.1f} {centroid:>8.0f} "
              f"{semis(peak_hz, nominal):>+9.1f} {semis(body_hz, nominal):>+9.1f} "
              f"{track:>+6.1f} {track_c:>+6.1f}")


if __name__ == "__main__":
    main()
