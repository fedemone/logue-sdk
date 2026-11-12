#!/usr/bin/env python3
"""match_samples.py -- voice Brachetti family presets to match reference samples.

Each preset to voice is a SPEC: every value of its rows in the unit's tables
(knob row, modal bank, voicing) as a starting point, the reference WAV it
should sound like, and the values the search may move with their ranges.  The
search appends candidate presets to a scratch copy of the unit, builds
preset_probe.cpp against it, renders every candidate as the sequencer strikes
it (at the output trim it will ship with), scores each one against its
reference, and walks downhill: every free value one step up and one step down,
the best move kept, the steps halved when nothing improves.

The score compares SHAPES, not levels (a reference is peak-normalised, a unit
is gain-staged by its trim):
  * energy over time: the share of the sound in 0-10, 10-30, 30-100, 100-300,
    300-1000 and 1000-3000 ms, as log ratios;
  * spectral balance over time: the share of six bands (<100 Hz, 100-300,
    300 Hz-1 kHz, 1-3 kHz, 3-8 kHz, >8 kHz) in the 0-30, 30-100 and 100-300 ms
    windows, as log ratios;
  * decay: the time to fall 40 dB from the loudest 5 ms frame;
  * pitch: the strongest partial over 10-300 ms (octave-folded), in octaves.

    python3 match_samples.py specs.py tom            # search, print the rows
    python3 match_samples.py specs.py tom --report   # score the specs as they are

The specs file defines SPECS = {unit: [spec, ...]}; see match_specs.py.
Needs numpy and scipy, g++, and the reference WAVs.
"""
import argparse
import importlib.util
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile

import numpy as np
import scipy.io.wavfile as wavfile

HERE = os.path.dirname(os.path.abspath(__file__))
DRUM = os.path.abspath(os.path.join(HERE, "..", ".."))
SR = 48000
MAX_BATCH = 200          # Preset is a uint8_t enum: keep every build under 256

# ---------------------------------------------------------------------------
# Rows: how a spec becomes C initialisers, per unit layout
# ---------------------------------------------------------------------------
# The columns in slot order: the common pages (Program, Pitch = the root,
# Poly, Velocity, Tone, Cutoff, Resnc, Gain), then the family's.  A column
# no spec sets is 0 — the value of every family knob in a standard slot
# (bar Attack, plate Chirp, world Rattle in VlRs'; noise GFad / Swel / Band /
# VTon), where its mapping is the identity.
MODAL_KNOBS = ["Prg", "Root", "Ply", "Vel", "Ton", "Cut", "Rsnc", "Gain",
               "MlRs", "MlSt", "VlRs", "VlSt", "Ptls", "Mdl", "Dky", "Rel",
               "Mtr", "Hit", "InHm", "TbRd", "NzMx", "NzRs", "NzFl", "NzFq"]
MODAL_KNOB_DEFAULTS = dict(Ply=0, Vel=1, VlRs=0, VlSt=0, Ptls=2, Mdl=3, Dky=150, Mtr=0,
                           Ton=0, Hit=0, Rel=10, InHm=0, Cut=1999, TbRd=10, Gain=0,
                           NzMx=10, NzRs=400, NzFl=0, NzFq=500, Rsnc=71, MlRs=600, MlSt=35)
# BrcNoise: slot 1 is blank (no Pitch), GrnRate / GrnDpth are the mallet
# knobs' slots (MlSt, MlRs).  Its Model value (the noise band default) is
# not a column: add a new preset's to kShippedModel in presets.h by hand.
NOISE_KNOBS = ["Prg", "-", "Ply", "Vel", "Ton", "Cut", "Rsnc", "Gain",
               "MlSt", "MlRs", "GFad", "Swel", "NzMx", "NzRs", "NzFl", "NzFq",
               "Band", "Rel", "VTon", "Pnch", "Root", "Dky"]
# Standard knobs whose slot a unit gave to its Punch knob: the column ships
# Punch's 0 (off), and the spec's value goes to the unit's per-preset table
# of what the displaced knob shipped with (the unit reads it from there).
DISPLACED = {"tom": ("Rel", "kShippedRel"), "plate": ("Rel", "kShippedRel"),
             "world": ("Rel", "kShippedRel"), "snare": ("Ptls", "kShippedPartls")}


def shipped_table(text, unit, values, at=None):
    """Append values to (or, with at, overwrite entry at of) the unit's table of
    a displaced knob's shipped values (DISPLACED)."""
    if unit not in DISPLACED:
        return text
    col, table = DISPLACED[unit]
    m = re.search(r"(static const int8_t %s\[kNumPresets\] = \{)([^}]*)(\};)" % table, text)
    vals = [x.strip() for x in m.group(2).split(",") if x.strip()]
    new = ["%d" % int(round(v[col])) for v in values]
    if at is None:
        vals += new
    else:
        vals[at] = new[0]
    return text[:m.start(2)] + ", ".join(vals) + text[m.end(2):]
NOISE_KNOB_DEFAULTS = dict(Ply=0, Vel=1, MlRs=500, MlSt=40, VlRs=0, VlSt=0, Mdl=5, Ton=0,
                           Hit=50, Rel=18, Cut=1999, Gain=3, NzMx=95, NzRs=900, NzFl=1,
                           NzFq=500, Rsnc=71, Root=84, Dky=20)
# Integer knobs (the table is int16_t) and their ranges.
KNOB_RANGES = dict(MlRs=(0, 1000), MlSt=(0, 50), Ptls=(0, 4), Dky=(0, 200), Mtr=(-10, 30),
                   Hit=(-98, 98), Rel=(0, 20), InHm=(-100, 100), TbRd=(0, 20), Gain=(0, 24),
                   NzMx=(0, 100), NzRs=(0, 1000), NzFl=(0, 2), NzFq=(2, 2000), VlSt=(-100, 100))

MODAL_FIELDS = ["r2", "r3", "r4", "t1", "t2", "t3", "t4", "mix", "e1", "e2", "e3", "e4",
                "count", "r5", "r6", "e5", "e6"]
VOICING_FIELDS = {
    "tom": ["modal_mix", "onset_ms", "chirp_hz", "band_mix", "split", "pitch_env",
            "pitch_decay", "pitch_amount", "boom_inc", "boom_env", "boom_decay", "boom_mix",
            "boom_attack_env", "boom_attack_inc", "slap_gain", "slap_decay", "slap_a_lo",
            "slap_a_hi", "boom_hz", "glide", "glide_hold_ms", "glide_ms"],
    "bar": ["modal_mix", "onset_ms", "chirp_hz", "band_mix", "split"],
    "plate": ["modal_mix", "onset_ms", "chirp_hz", "band_mix", "split", "noise_gain",
              "shimmer_mix", "shimmer_decay", "click_decay", "hat", "hat_mode", "hat_hz",
              "hat_q"],
    "snare": ["modal_mix", "onset_ms", "band_mix", "split", "pitch_env", "pitch_decay",
              "pitch_amount", "boom_inc", "boom_env", "boom_decay", "boom_mix",
              "boom_attack_env", "boom_attack_inc", "wire_mix", "wire_onset_env",
              "wire_onset_attack", "wire_a1", "wire_a2", "band_b_hz", "band_b_r",
              "band_c_hz", "band_c_r"],
    "noise": ["band_mix", "split", "gate_hz", "gate_depth", "gate_decay", "attack_rate"],
    "world": ["modal_mix", "onset_ms", "band_mix", "split", "exc_gain",
              "fr_level", "fr_hz", "fr_rise", "fr_rise_ms", "fr_fall", "fr_fall_hold_ms",
              "fr_fall_ms", "fr_hold_ms", "fr_t60_ms", "fr_formant1_hz", "fr_formant2_hz", "fr_noise",
              "wire_level", "wire_hz", "wire_t60_ms", "wire_damp", "wire_burst_ms", "wire_direct",
              "gourd_hz", "gourd_up_hz", "gourd_up_ms", "gourd_down_hz", "gourd_down_hold_ms",
              "gourd_down_ms", "gourd_q",
              "buzz_level", "buzz_thresh", "buzz_hz", "buzz_q",
              "jg_level", "jg_lo_hz", "jg_hi_hz", "jg_t60_ms", "jg_bounce_lo_ms", "jg_bounce_hi_ms",
              "jg_spread_ms", "jg_clash_ms",
              "pt_level", "pt_rate", "pt_attack_ms", "pt_hold_ms", "pt_t60_ms", "pt_click_ms",
              "pt_f1_hz", "pt_q1", "pt_f2_hz", "pt_q2", "pt_mix2", "pt_direct"],
}
VOICING_DEFAULTS = dict(modal_mix=0.2, onset_ms=0.0, chirp_hz=0.0, band_mix=0.0, split=0.0,
                        pitch_env=0.0, pitch_decay=0.0, pitch_amount=0.0, boom_inc=0.0,
                        boom_env=0.0, boom_decay=0.0, boom_mix=0.0, boom_attack_env=0.0,
                        boom_attack_inc=0.0, slap_gain=0.0, slap_decay=0.0, slap_a_lo=0.0,
                        slap_a_hi=0.0, boom_hz=0.0, glide=0.0, glide_hold_ms=0.0, glide_ms=0.0, noise_gain=5.0,
                        shimmer_mix=0.0, shimmer_decay=1.0, click_decay=0.0, hat=False,
                        hat_mode=0.0, hat_hz=0.0, hat_q=0.0, wire_mix=0.0, wire_onset_env=1.0,
                        wire_onset_attack=0.001, wire_a1=1.76, wire_a2=0.918, band_b_hz=0.0,
                        band_b_r=0.0, band_c_hz=0.0, band_c_r=0.0, gate_hz=0.0,
                        gate_depth=0.0, gate_decay=1.0, attack_rate=0.0, exc_gain=1.0)


def full(spec):
    """The spec's values with every unset field at its default."""
    v = dict(VOICING_DEFAULTS)
    v.update(MODAL_KNOB_DEFAULTS if spec["unit"] != "noise" else NOISE_KNOB_DEFAULTS)
    v.update(dict(r2=0, r3=0, r4=0, t1=0, t2=0, t3=0, t4=0, mix=0.2, e1=0, e2=0, e3=0,
                  e4=0, count=0, r5=0, r6=0, e5=0, e6=0))
    v.update(spec["values"])
    # The tracked boom's load-time increment follows its base frequency.
    if v.get("boom_hz", 0) > 0 and not v.get("boom_inc"):
        v["boom_inc"] = 2 * np.pi * v["boom_hz"] / SR
    if "mix" not in spec["values"] and "modal_mix" in v:
        v["mix"] = v["modal_mix"]
    return v


def cfloat(x):
    if isinstance(x, bool):
        return "true" if x else "false"
    t = "%.9g" % float(x)
    if not any(c in t for c in ".en"):
        t += ".0"
    return t + "f"


def knob_row(v, idx, unit):
    cols = NOISE_KNOBS if unit == "noise" else MODAL_KNOBS
    vals = []
    for c in cols:
        x = idx if c == "Prg" else int(round(v.get(c, 0)))
        if unit in DISPLACED and c == DISPLACED[unit][0]:
            x = 0   # the slot is Punch's (off); the value goes to shipped_table
        if c in KNOB_RANGES:
            lo, hi = KNOB_RANGES[c]
            x = max(lo, min(hi, x))
        vals.append("%4d" % x)
    return "    {" + ",".join(vals) + "},"


def modal_row(v):
    vals = []
    for f in MODAL_FIELDS:
        vals.append(str(int(v[f])) if f == "count" else cfloat(v[f]))
    return "    {" + ", ".join(vals) + "},"


def voicing_row(v, unit):
    # A voicing field no spec sets is 0 (the layer it belongs to is off).
    return "    {" + ", ".join(cfloat(v.get(f, 0.0)) for f in VOICING_FIELDS[unit]) + "},"


def insert_table(text, marker, rows):
    """Append rows to the initialiser of the table named by marker."""
    a = text.index(marker)
    b = text.index("\n};", a)
    body = text[a:b].rstrip()
    last = body.rsplit("\n", 1)[-1]
    code = last.split("//")[0].rstrip()
    if not code.endswith(",") and not code.endswith("{"):
        pos = a + len(body.rsplit("\n", 1)[0]) + 1 + len(code)
        text = text[:pos] + "," + text[pos:]
        b += 1
    return text[:b] + "\n" + "\n".join(rows) + text[b:]


def preset_count(text):
    return sum(1 for line in text[text.index("enum Preset"):text.index("    kNumPresets")].splitlines()
               if line.strip().startswith("k_"))


def write_presets(unit, specs, vals, trims, source):
    """Append finished presets to the REAL unit: presets.h's tables (with a
    comment naming the reference) and header.c's Program range."""
    d = os.path.join(DRUM, UNIT_DIRS[unit])
    p = os.path.join(d, "presets.h")
    text = open(p).read()
    n0 = preset_count(text)
    names = [s["name"] for s in specs]
    enum_rows = "".join("    %-19s // %-2d %s (voiced after %s)\n"
                        % (s["enum"] + ",", n0 + i, s["note"], os.path.basename(s["ref"]))
                        for i, s in enumerate(specs))
    k = text.index("    kNumPresets")
    text = text[:k] + enum_rows + text[k:]
    text = insert_table(text, "kPresetNames[] = {",
                        ['    %-10s // added: voiced after the Pelle e Legno sample' % ('"%s",' % n) for n in names])
    text = insert_table(text, "kPresetKnobs[kNumPresets]",
                        [knob_row(v, n0 + i, unit) + "   // %d: %s" % (n0 + i, s["name"])
                         for i, (s, v) in enumerate(zip(specs, vals))])
    if unit != "noise":
        text = insert_table(text, "kModalConfigs[kNumPresets]",
                            ["    /* %s: %s */" % (s["enum"], s["note"]) + "\n" + modal_row(v)
                             for s, v in zip(specs, vals)])
    text = insert_table(text, "kVoicing[kNumPresets]",
                        ["    /* %-14s */" % s["enum"] + voicing_row(v, unit)[4:] for s, v in zip(specs, vals)])
    text = insert_table(text, "kOutTrim[kNumPresets]",
                        ["    /* %-2d %-7s */ %.5ff," % (n0 + i, s["name"], t) for i, (s, t) in enumerate(zip(specs, trims))])
    text = shipped_table(text, unit, vals)
    open(p, "w").write(text)
    h = os.path.join(d, "header.c")
    ht = open(h).read()
    m = re.search(r'\{0, (\d+), 0, 0, k_unit_param_type_strings, 0, 0, 0, \{"Program"\}\}', ht)
    ht = ht[:m.start(1)] + str(n0 + len(specs) - 1) + ht[m.end(1):]
    open(h, "w").write(ht)
    print("wrote %d presets (%d..%d) into %s" % (len(specs), n0, n0 + len(specs) - 1, d), file=sys.stderr)


def rewrite_presets(unit, specs, vals, trims):
    """Replace the rows of presets already in the REAL unit (found by the
    enum, name and index comments write_presets leaves), so a preset can be
    revoiced in place."""
    d = os.path.join(DRUM, UNIT_DIRS[unit])
    p = os.path.join(d, "presets.h")
    text = open(p).read()
    enum_block = text[text.index("enum Preset"):text.index("    kNumPresets")]
    enums = [l.strip().split()[0].rstrip(",").split("=")[0].strip()
             for l in enum_block.splitlines() if l.strip().startswith("k_")]

    def sub1(pattern, repl):
        nonlocal text
        new, n = re.subn(pattern, lambda m: repl(m), text, count=1, flags=re.M)
        if n != 1:
            raise SystemExit("rewrite: no row matches %r" % pattern)
        text = new

    for s, v, t in zip(specs, vals, trims):
        i = enums.index(s["enum"])
        sub1(r"^    \{.*\},(   // %d: %s)$" % (i, re.escape(s["name"])),
             lambda m: knob_row(v, i, unit) + m.group(1))
        if unit != "noise":
            sub1(r"^(    /\* %s: .*\*/\n)    \{.*\},?$" % re.escape(s["enum"]),
                 lambda m: m.group(1) + modal_row(v))
        sub1(r"^(    /\* %s\s*\*/)\{.*\},?$" % re.escape(s["enum"]),
             lambda m: m.group(1) + voicing_row(v, unit)[4:])
        sub1(r"^(    /\* %-2d %-7s \*/ )[0-9.]+f,?$" % (i, re.escape(s["name"])),
             lambda m: m.group(1) + "%.5ff," % t)
        text = shipped_table(text, unit, [v], at=i)
    open(p, "w").write(text)
    print("rewrote %d presets in %s" % (len(specs), d), file=sys.stderr)


def patch_presets(text, unit, cands, first_idx):
    """Append candidate presets to a unit's presets.h text."""
    enum_rows = "".join("    k_Cand%d,\n" % (first_idx + i) for i in range(len(cands)))
    k = text.index("    kNumPresets")
    text = text[:k] + enum_rows + text[k:]
    text = insert_table(text, "kPresetNames[] = {",
                        ['    "C%d",' % (first_idx + i) for i in range(len(cands))])
    text = insert_table(text, "kPresetKnobs[kNumPresets]",
                        [knob_row(c, first_idx + i, unit) for i, c in enumerate(cands)])
    if unit != "noise":
        text = insert_table(text, "kModalConfigs[kNumPresets]", [modal_row(c) for c in cands])
    text = insert_table(text, "kVoicing[kNumPresets]", [voicing_row(c, unit) for c in cands])
    text = shipped_table(text, unit, cands)
    text = insert_table(text, "kOutTrim[kNumPresets]", ["    1.0f," for _ in cands])
    return text


# ---------------------------------------------------------------------------
# Build, render
# ---------------------------------------------------------------------------
UNIT_DIRS = dict(tom="brachetti_tom", bar="brachetti_bar", plate="brachetti_plate",
                 snare="brachetti_snare", noise="brachetti_noise", kick="brachetti_kick",
                 world="brachetti_world")


def build(unit_dir, out):
    subprocess.run(["g++", "-std=c++17", "-O1", "-I" + unit_dir, "-I" + os.path.dirname(unit_dir),
                    "-I" + os.path.join(DRUM, "common"), "-DRUNTIME_COMMON_H_", "-w",
                    os.path.join(HERE, "preset_probe.cpp"), "-o", out], check=True)
    return out


def render_candidates(unit, cands, vel, secs):
    """Render candidate value-dicts; returns [(audio, trim), ...]."""
    src = os.path.join(DRUM, UNIT_DIRS[unit])
    results = []
    for b0 in range(0, len(cands), MAX_BATCH):
        batch = cands[b0:b0 + MAX_BATCH]
        tmp = tempfile.mkdtemp(prefix="brcmatch_")
        try:
            # A sibling of the real brachetti_common / common, so the unit's
            # relative includes resolve.
            work = os.path.join(tmp, "drum")
            os.makedirs(work)
            for d in ("brachetti_common", "common"):
                os.symlink(os.path.join(DRUM, d), os.path.join(work, d))
            unit_dir = os.path.join(work, UNIT_DIRS[unit])
            shutil.copytree(src, unit_dir)
            p = os.path.join(unit_dir, "presets.h")
            text = open(p).read()
            n0 = preset_count(text)
            open(p, "w").write(patch_presets(text, unit, batch, n0))
            probe = build(unit_dir, os.path.join(tmp, "probe"))
            outd = os.path.join(tmp, "out")
            os.makedirs(outd)
            r = subprocess.run([probe, "match", str(n0), str(n0 + len(batch) - 1), str(vel),
                                str(secs), outd], capture_output=True, text=True, check=True)
            trims = {int(l.split()[0]): float(l.split()[1]) for l in r.stdout.split("\n") if l.strip()}
            for i in range(len(batch)):
                _, x = wavfile.read(os.path.join(outd, "%d.wav" % (n0 + i)))
                results.append((x.astype(np.float64), trims[n0 + i]))
        finally:
            shutil.rmtree(tmp, ignore_errors=True)
    return results


# ---------------------------------------------------------------------------
# Features and score
# ---------------------------------------------------------------------------
WINDOWS = [(0, .01), (.01, .03), (.03, .1), (.1, .3), (.3, 1.0), (1.0, 3.0)]
BAND_WINDOWS = [(0, .03), (.03, .1), (.1, .3)]
BANDS = [(0, 100), (100, 300), (300, 1000), (1000, 3000), (3000, 8000), (8000, 24000)]


def load(path):
    sr, x = wavfile.read(path)
    x = x.astype(np.float64)
    if x.ndim > 1:
        x = x.mean(axis=1)
    if x.dtype.kind == "i":
        x /= 32768.0
    return x


def t40(x):
    h = int(0.005 * SR)
    n = len(x) // h
    if n < 2:
        return 0.005
    e = np.sqrt(np.mean(x[:n * h].reshape(n, h) ** 2, axis=1) + 1e-20)
    db = 20 * np.log10(e / e.max())
    i0 = int(np.argmax(db))
    below = np.nonzero(db[i0:] < -40)[0]
    return (below[0] if len(below) else n - i0) * 0.005


TRACK_WINDOWS = [(.01, .06), (.06, .12), (.12, .2), (.2, .35)]


def pitch_track(x, lo, hi):
    """The strongest partial inside [lo, hi] Hz in successive windows: the
    shape of a glide."""
    out = []
    for a, b in TRACK_WINDOWS:
        s = x[int(a * SR):int(b * SR)]
        S = np.abs(np.fft.rfft(s * np.hanning(len(s)), 1 << 16))
        f = np.fft.rfftfreq(1 << 16, 1 / SR)
        m = (f >= lo) & (f <= hi)
        out.append(f[m][np.argmax(S[m])])
    return np.array(out)


def features(x, track=None):
    x = x - np.mean(x)
    tot = np.sum(x ** 2) + 1e-20
    ew = np.array([np.sum(x[int(a * SR):int(b * SR)] ** 2) / tot for a, b in WINDOWS])
    bw = []
    for a, b in BAND_WINDOWS:
        s = x[int(a * SR):int(b * SR)]
        if len(s) < 64:
            bw.append(np.zeros(len(BANDS)))
            continue
        S = np.abs(np.fft.rfft(s, 1 << 15)) ** 2
        f = np.fft.rfftfreq(1 << 15, 1 / SR)
        bw.append(np.array([S[(f >= lo) & (f < hi)].sum() for lo, hi in BANDS]) / (S.sum() + 1e-20))
    s = x[int(.01 * SR):int(.3 * SR)]
    S = np.abs(np.fft.rfft(s * np.hanning(len(s)), 1 << 17))
    f = np.fft.rfftfreq(1 << 17, 1 / SR)
    m = (f > 40) & (f < 6000)
    pk = f[m][np.argmax(S[m])]
    return dict(ew=ew, bw=np.array(bw), t40=t40(x), pk=pk,
                track=pitch_track(x, *track) if track else None)


def score(fa, fb, w):
    d_e = np.mean(np.abs(np.log10(fa["ew"] + 1e-3) - np.log10(fb["ew"] + 1e-3)))
    d_b = np.mean(np.abs(np.log10(fa["bw"] + 1e-3) - np.log10(fb["bw"] + 1e-3)))
    d_t = abs(np.log(max(fa["t40"], .005) / max(fb["t40"], .005)))
    oct_ = np.log2(fa["pk"] / fb["pk"])
    d_p = min(abs(oct_), abs(oct_ - round(oct_)) + 0.25)   # octave confusions cost little
    d_g = 0.0
    if fa.get("track") is not None and fb.get("track") is not None:
        d_g = np.mean(np.abs(np.log2(fa["track"] / fb["track"]))) * 12.0   # semitones
    return (w.get("e", 1.0) * d_e + w.get("b", 1.0) * d_b + w.get("t", 0.6) * d_t
            + w.get("p", 1.0) * d_p + w.get("g", 0.15) * d_g)


def describe(fa, fb):
    tr = ""
    if fa.get("track") is not None:
        tr = "  glide " + "/".join("%.0f" % f for f in fa["track"]) + " vs " + "/".join("%.0f" % f for f in fb["track"])
    return ("t40 %4.0f/%4.0f ms  peak %5.0f/%5.0f Hz  e30 %3.0f/%3.0f%%  1-3k@0-30 %3.0f/%3.0f%%"
            % (fa["t40"] * 1000, fb["t40"] * 1000, fa["pk"], fb["pk"],
               100 * fa["ew"][:2].sum(), 100 * fb["ew"][:2].sum(),
               100 * fa["bw"][0][3], 100 * fb["bw"][0][3])) + tr


# ---------------------------------------------------------------------------
# Search
# ---------------------------------------------------------------------------
def moves(spec, vals, steps):
    out = []
    for name, (lo, hi, kind) in spec.get("free", {}).items():
        st = steps[name]
        for sgn in (-1, 1):
            nv = dict(vals)
            if kind == "log":
                nv[name] = float(np.clip(vals[name] * (2.0 ** (sgn * st)), lo, hi))
            elif kind == "tau":   # a per-sample decay, moved by its time constant (ms in lo/hi)
                tau = -1.0 / (np.log(vals[name]) * SR) * 1000.0
                tau = float(np.clip(tau * (2.0 ** (sgn * st)), lo, hi))
                nv[name] = float(np.exp(-1.0 / (tau * 1e-3 * SR)))
            else:
                nv[name] = float(np.clip(vals[name] + sgn * st, lo, hi))
            if name in KNOB_RANGES or kind == "int":
                nv[name] = int(round(nv[name]))
            if nv[name] != vals[name]:
                out.append((name, sgn, nv))
    return out


def initial_steps(spec):
    st = {}
    for name, (lo, hi, kind) in spec.get("free", {}).items():
        st[name] = 0.5 if kind in ("log", "tau") else max(1.0 if kind == "int" or name in KNOB_RANGES else 0.0,
                                                   (hi - lo) / 8.0)
    return st


def start_values(spec, saved):
    v = full(spec)
    v.update(saved.get(spec["name"], {}))
    return v


def search(unit, specs, gens, vel, secs, saved):
    targets = [features(load(s["ref"]), s.get("track")) for s in specs]
    state = [dict(vals=start_values(s, saved), steps=initial_steps(s), best=None) for s in specs]
    first = render_candidates(unit, [st["vals"] for st in state], vel, secs)
    for st, (x, trim), ft, s in zip(state, first, targets, specs):
        st["best"] = score(features(x, s.get("track")), ft, s.get("weights", {}))
        st["trim"] = trim
    for g in range(gens):
        cands, owner = [], []
        for k, (st, s) in enumerate(zip(state, specs)):
            for name, sgn, nv in moves(s, st["vals"], st["steps"]):
                cands.append(nv)
                owner.append((k, name, sgn))
        if not cands:
            break
        res = render_candidates(unit, cands, vel, secs)
        improved = [[] for _ in specs]
        for (k, name, sgn), nv, (x, trim) in zip(owner, cands, res):
            sc = score(features(x, specs[k].get("track")), targets[k], specs[k].get("weights", {}))
            if sc < state[k]["best"] - 1e-4:
                improved[k].append((sc, name, nv, trim))
        changed = 0
        for k, st in enumerate(state):
            if improved[k]:
                sc, name, nv, trim = min(improved[k], key=lambda t: t[0])
                st["vals"], st["best"], st["trim"] = nv, sc, trim
                changed += 1
            else:
                for n in st["steps"]:
                    st["steps"][n] *= 0.5
        print("gen %2d: %3d candidates, %2d of %d presets improved, mean score %.3f"
              % (g + 1, len(cands), changed, len(specs), np.mean([s["best"] for s in state])),
              file=sys.stderr)
    return state, targets


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("specs")
    ap.add_argument("unit")
    ap.add_argument("--only", nargs="*")
    ap.add_argument("--gens", type=int, default=12)
    ap.add_argument("--vel", type=int, default=110)
    ap.add_argument("--secs", type=float, default=3.0)
    ap.add_argument("--report", action="store_true", help="score the specs as written, no search")
    ap.add_argument("--write", action="store_true",
                    help="append the presets, at their --state values, to the real unit")
    ap.add_argument("--rewrite", action="store_true",
                    help="replace, at their --state values, presets already in the real unit")
    ap.add_argument("--state", help="JSON of each preset's values: a search starts from it "
                                    "(over the spec) and saves its result to it")
    a = ap.parse_args()
    sp = importlib.util.spec_from_file_location("specs", a.specs)
    mod = importlib.util.module_from_spec(sp)
    sp.loader.exec_module(mod)
    specs = [s for s in mod.SPECS[a.unit] if not a.only or s["name"] in a.only]
    for s in specs:
        s["unit"] = a.unit
    saved = {}
    if a.state and os.path.exists(a.state):
        saved = json.load(open(a.state))
    if a.write or a.rewrite:
        vals = [start_values(s, saved) for s in specs]
        res = render_candidates(a.unit, vals, a.vel, a.secs) if a.write else None
        if a.write:
            write_presets(a.unit, specs, vals, [t for _, t in res], a.specs)
        else:
            # Candidates are appended after the unit's presets, so the trims
            # are measured on copies of the new values, not on the old rows.
            res = render_candidates(a.unit, vals, a.vel, a.secs)
            rewrite_presets(a.unit, specs, vals, [t for _, t in res])
        return
    if a.report:
        res = render_candidates(a.unit, [start_values(s, saved) for s in specs], a.vel, a.secs)
        for s, (x, trim) in zip(specs, res):
            fa, fb = features(x, s.get("track")), features(load(s["ref"]), s.get("track"))
            print("%-8s score %.3f trim %.4f  %s" % (s["name"], score(fa, fb, s.get("weights", {})),
                                                     trim, describe(fa, fb)))
        return
    state, targets = search(a.unit, specs, a.gens, a.vel, a.secs, saved)
    if a.state:
        for s, st in zip(specs, state):
            saved[s["name"]] = {k: v for k, v in st["vals"].items() if k in s.get("free", {})}
        json.dump(saved, open(a.state, "w"), indent=1, sort_keys=True)
    res = render_candidates(a.unit, [st["vals"] for st in state], a.vel, a.secs)
    for s, st, ft, (x, trim) in zip(specs, state, targets, res):
        fa = features(x, s.get("track"))
        print("// %-8s score %.3f  %s" % (s["name"], st["best"], describe(fa, ft)))
        print("//   free: " + ", ".join("%s=%.6g" % (k, st["vals"][k]) for k in s.get("free", {})))
        print(knob_row(st["vals"], 0, a.unit))
        if a.unit != "noise":
            print(modal_row(st["vals"]))
        print(voicing_row(st["vals"], a.unit))
        print("    %.5ff," % trim)


if __name__ == "__main__":
    main()
