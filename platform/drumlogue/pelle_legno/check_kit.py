#!/usr/bin/env python3
"""check_kit.py -- check a folder of WAV files against the drumlogue's USER
sample rules before copying it onto the unit.

The rules come from the drumlogue Owner's Manual, "Loading USER SAMPLE":

  * the filename is `XXX_sample_name.wav`, XXX = three digits 001..128 (the
    slot), and sample_name uses only letters, digits and
    -_!?#$%&'()*+,:;<=>@  -- "Any files not following the rules above will be
    deleted from the drumlogue";
  * 32-bit float / 48 kHz is the native format; 16/24/32-bit PCM and other
    rates are accepted but converted at start-up (slower boot);
  * at most four seconds of a sample is played (Specifications, note *2);
  * 32 MB of storage is shared by samples, programs, kits and units.

Plus what makes a percussion sample behave in a sequencer: it starts at rest
(no click at the step), ends at rest (no click at the cut), has no DC, and its
peak is not clipped.

Usage:  python3 check_kit.py [Samples] [--existing 001 002 ...]
        --existing  slots already taken on the unit: reports collisions
Exit status 0 = every file passes, 1 = at least one error.
"""
import argparse
import os
import re
import struct
import sys

import numpy as np

NAME_RE = re.compile(r"^(\d{3})_([A-Za-z0-9\-_!?#$%&'()*+,:;<=>@]+)\.wav$")
STORAGE_BYTES = 32 * 1024 * 1024


def read_wav(path):
    """Minimal RIFF reader: returns (format_tag, channels, rate, bits, frames, data)."""
    with open(path, "rb") as f:
        raw = f.read()
    if raw[:4] != b"RIFF" or raw[8:12] != b"WAVE":
        raise ValueError("not a RIFF/WAVE file")
    pos, fmt, data = 12, None, None
    while pos + 8 <= len(raw):
        cid, size = raw[pos:pos + 4], struct.unpack("<I", raw[pos + 4:pos + 8])[0]
        body = raw[pos + 8:pos + 8 + size]
        if cid == b"fmt ":
            fmt = struct.unpack("<HHIIHH", body[:16])
        elif cid == b"data":
            data = body
        pos += 8 + size + (size & 1)
    if fmt is None or data is None:
        raise ValueError("missing fmt or data chunk")
    tag, ch, rate, _brate, _align, bits = fmt
    if tag == 0xFFFE:                      # WAVE_FORMAT_EXTENSIBLE: real tag in the GUID
        tag = 3 if bits == 32 and b"\x03\x00\x00\x00\x00\x00\x10\x00" in raw[:200] else 1
    if tag == 3 and bits == 32:
        x = np.frombuffer(data, "<f4").astype(np.float64)
    elif tag == 1 and bits == 16:
        x = np.frombuffer(data, "<i2") / 32768.0
    elif tag == 1 and bits == 24:
        b = np.frombuffer(data, np.uint8).reshape(-1, 3)
        v = (b[:, 0].astype(np.int32) | (b[:, 1].astype(np.int32) << 8) | (b[:, 2].astype(np.int32) << 16))
        x = np.where(v >= 1 << 23, v - (1 << 24), v) / float(1 << 23)
    elif tag == 1 and bits == 32:
        x = np.frombuffer(data, "<i4") / float(1 << 31)
    else:
        raise ValueError(f"unsupported encoding: tag {tag}, {bits} bit")
    frames = len(x) // ch
    return tag, ch, rate, bits, frames, x[: frames * ch].reshape(frames, ch)


def check(folder, existing=()):
    files = sorted(f for f in os.listdir(folder) if f.lower().endswith(".wav"))
    errors, warnings, slots, total = 0, 0, {}, 0
    print(f"{'file':26s} {'fmt':10s} {'len':>6s} {'peak':>7s}  notes")
    for fn in files:
        notes, bad = [], False
        m = NAME_RE.match(fn)
        if not m:
            notes.append("NAME violates XXX_name.wav / allowed characters (the unit deletes it)")
            bad = True
        else:
            slot = int(m.group(1))
            if not 1 <= slot <= 128:
                notes.append("slot outside 001..128")
                bad = True
            if slot in slots:
                notes.append(f"slot {slot:03d} already used by {slots[slot]}")
                bad = True
            if f"{slot:03d}" in existing:
                notes.append(f"slot {slot:03d} is already taken on the unit")
                bad = True
            slots[slot] = fn
        path = os.path.join(folder, fn)
        total += os.path.getsize(path)
        try:
            tag, ch, rate, bits, frames, x = read_wav(path)
        except ValueError as e:
            print(f"{fn:26s} ERROR {e}")
            errors += 1
            continue
        enc = f"{'f' if tag == 3 else 'i'}{bits}/{rate // 1000}k{'' if ch == 1 else ' st'}"
        dur = frames / rate
        if not np.all(np.isfinite(x)):
            notes.append("NaN/inf samples")
            bad = True
        peak = float(np.max(np.abs(x))) if frames else 0.0
        if tag != 3 or bits != 32 or rate != 48000:
            notes.append("not 32-bit float / 48 kHz: the unit converts it at boot")
            warnings += 1
        if dur > 4.0:
            notes.append("longer than the 4 s the unit can play")
            warnings += 1
        if peak > 1.0:
            notes.append("peak above full scale")
            bad = True
        if frames:
            if np.max(np.abs(x[0])) > 0.01 * max(peak, 1e-9):
                notes.append("does not start at rest (click on the step)")
                warnings += 1
            if np.max(np.abs(x[-1])) > 0.01 * max(peak, 1e-9):
                notes.append("does not end at rest (click at the cut)")
                warnings += 1
            if abs(float(np.mean(x))) > 0.002:
                notes.append(f"DC offset {np.mean(x):+.4f}")
                warnings += 1
        errors += bad
        pk = f"{20 * np.log10(peak):6.1f}" if peak > 0 else "  -inf"
        print(f"{fn:26s} {enc:10s} {dur:5.2f}s {pk}  {'; '.join(notes) if notes else 'ok'}")
    print(f"\n{len(files)} files, {total / 1e6:.2f} MB "
          f"({100 * total / STORAGE_BYTES:.0f} % of the drumlogue's 32 MB shared storage), "
          f"{errors} error(s), {warnings} warning(s)")
    return errors


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("folder", nargs="?", default=os.path.join(os.path.dirname(os.path.abspath(__file__)), "Samples"))
    ap.add_argument("--existing", nargs="*", default=[], help="slots already used on the unit, e.g. 001 002")
    a = ap.parse_args()
    return 1 if check(a.folder, set(a.existing)) else 0


if __name__ == "__main__":
    sys.exit(main())
