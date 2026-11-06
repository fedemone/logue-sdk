#!/usr/bin/env python3
"""gen_scenarios.py — write an equivalence script for one Brachetti family unit.

    ./driver_clone --list > unit.txt
    python3 gen_scenarios.py unit.txt [--seed N] [--fuzz N] > script.txt

The script only uses the presets and parameters the FAMILY unit exposes, so the
original `brachetti` can play it too (it has every one of them, under the same
names): a family unit is equivalent to the original when both print identical
hashes for every render.  Parameters a family unit removed are, by
construction, never touched here — the original keeps them at each preset's
shipped value, which is exactly what the family unit hard-wires.

Groups (each scenario starts from a fresh, freshly booted synth):
  ship    every preset, sequencer gate at four velocities
  held    a held gate, then the release
  midi    MIDI notes around the root, note-offs, a chord
  knob    every parameter at its ends, centre, default and random points,
          set before the strike, and a second time WHILE the voice rings
  stack   repeated strikes at roll / flam / slow spacings, same and new notes
  chain   Program changes over a ringing voice
  life    Reset / Suspend / Resume / AllNoteOff / pitch bend mid-ring
  fuzz    random sessions: every parameter, notes, gates, bends, resets
"""
import argparse
import random
import sys

SR = 48000


def ms(t):
    return max(64, int(SR * t / 1000.0))


def parse_list(path):
    presets, params = [], []
    with open(path) as f:
        for line in f:
            w = line.split()
            if not w:
                continue
            if w[0] == "preset":
                presets.append(line.split(None, 2)[2].strip())  # "Ac Tom"
            elif w[0] == "param":
                params.append((w[2], int(w[3]), int(w[4]), int(w[5])))
    return presets, params


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("listing")
    ap.add_argument("--seed", type=int, default=1234)
    ap.add_argument("--fuzz", type=int, default=120)
    ap.add_argument("--knob-points", type=int, default=3,
                    help="random points per knob, on top of the fixed ones")
    ap.add_argument("--avoid-reselect", action="append", default=[],
                    help="never select this preset while it is already selected "
                         "(a documented deviation, see run_equiv.sh)")
    a = ap.parse_args()
    rnd = random.Random(a.seed)
    presets, params = parse_list(a.listing)
    knobs = [p for p in params if p[0] != "Program"]
    out = []
    cur = [None]

    def w(line):
        if line.startswith("preset "):
            p = line[7:]
            if p == cur[0] and p in a.avoid_reselect:
                return
            cur[0] = p
        elif line.startswith("scenario "):
            cur[0] = None
        out.append(line)

    def scen(name, preset):
        w("scenario %s" % name.replace(" ", "_"))
        w("preset %s" % preset)

    # ── ship ───────────────────────────────────────────────────────────────
    for p in presets:
        for vel in (127, 100, 64, 25):
            scen("ship_%s_v%d" % (p, vel), p)
            w("gate_on %d" % vel)
            w("gate_off")
            for _ in range(3):
                w("render %d" % ms(500))

    # ── held ───────────────────────────────────────────────────────────────
    for p in presets:
        scen("held_%s" % p, p)
        w("gate_on 110")
        w("render %d" % ms(300))
        w("gate_off")
        w("render %d" % ms(900))

    # ── midi ───────────────────────────────────────────────────────────────
    for p in presets:
        scen("midi_%s" % p, p)
        for off, vel in ((0, 100), (7, 90), (-12, 120), (12, 70), (-5, 45)):
            w("note_rel %d %d" % (off, vel))
            w("render %d" % ms(90))
            w("note_off_rel %d" % off)
        w("render %d" % ms(400))
        w("note_rel 0 127")
        w("note_rel 4 127")
        w("note_rel 7 127")
        w("render %d" % ms(600))

    # ── knob ───────────────────────────────────────────────────────────────
    for p in presets:
        for name, lo, hi, init in knobs:
            pts = {lo, hi, (lo + hi) // 2, init, (lo + init) // 2, (hi + init) // 2}
            for _ in range(a.knob_points):
                pts.add(rnd.randint(lo, hi))
            for v in sorted(pts):
                scen("knob_%s_%s_%d" % (p, name, v), p)
                w("param %s %d" % (name, v))
                w("gate_on 110")
                w("gate_off")
                w("render %d" % ms(700))
            # live edit while ringing, end to end
            scen("live_%s_%s" % (p, name), p)
            w("gate_on 120")
            w("gate_off")
            w("render %d" % ms(60))
            w("param %s %d" % (name, hi))
            w("render %d" % ms(200))
            w("param %s %d" % (name, lo))
            w("render %d" % ms(200))
            w("gate_on 90")
            w("gate_off")
            w("render %d" % ms(400))

    # ── stack ──────────────────────────────────────────────────────────────
    have_pitch = any(k[0] == "Pitch" for k in knobs)
    have_poly = any(k[0] == "Poly" for k in knobs)
    for p in presets:
        for gap in (25, 60, 120, 300):
            for poly in ((1, 2, 4) if have_poly else (None,)):
                scen("stack_%s_%d_%s" % (p, gap, poly), p)
                if poly is not None:
                    w("param Poly %d" % poly)
                for i in range(8):
                    if have_pitch and i % 3 == 2:
                        w("param Pitch %d" % rnd.choice((-7, -2, 3, 5, 12)))
                    w("gate_on %d" % rnd.choice((127, 96, 70, 40)))
                    w("gate_off")
                    w("render %d" % ms(gap))
                w("render %d" % ms(800))

    # ── chain ──────────────────────────────────────────────────────────────
    if len(presets) > 1:
        for i, p in enumerate(presets):
            q = presets[(i + 1) % len(presets)]
            r = presets[(i + 3) % len(presets)]
            scen("chain_%s_%s" % (p, q), p)
            w("gate_on 127")
            w("gate_off")
            w("render %d" % ms(150))
            w("preset %s" % q)
            w("render %d" % ms(200))
            w("gate_on 100")
            w("gate_off")
            w("render %d" % ms(30))
            w("preset %s" % r)
            w("render %d" % ms(5))
            w("preset %s" % p)
            w("gate_on 80")
            w("gate_off")
            w("render %d" % ms(600))

    # ── life ───────────────────────────────────────────────────────────────
    for p in presets:
        scen("life_reset_%s" % p, p)
        w("gate_on 127")
        w("gate_off")
        w("render %d" % ms(40))
        w("reset")
        w("render %d" % ms(40))
        w("gate_on 127")
        w("gate_off")
        w("render %d" % ms(500))
        scen("life_suspend_%s" % p, p)
        w("gate_on 127")
        w("gate_off")
        w("render %d" % ms(80))
        w("suspend")
        w("resume")
        w("render %d" % ms(40))
        w("gate_on 100")
        w("gate_off")
        w("render %d" % ms(500))
        scen("life_alloff_%s" % p, p)
        w("gate_on 127")
        w("render %d" % ms(100))
        w("all_off")
        w("render %d" % ms(400))
        scen("life_bend_%s" % p, p)
        w("note_rel 0 110")
        w("render %d" % ms(50))
        w("bend 16383")
        w("render %d" % ms(100))
        w("bend 0")
        w("render %d" % ms(100))
        w("bend 8192")
        w("note_rel 2 100")
        w("render %d" % ms(400))
        if len(presets) > 1:
            q = presets[(presets.index(p) + 1) % len(presets)]
            scen("life_fadereset_%s" % p, p)
            w("gate_on 127")
            w("gate_off")
            w("render %d" % ms(100))
            w("preset %s" % q)
            w("render %d" % ms(2))
            w("reset")
            w("gate_on 127")
            w("gate_off")
            w("render %d" % ms(400))

    # ── fuzz ───────────────────────────────────────────────────────────────
    for n in range(a.fuzz):
        p = rnd.choice(presets)
        scen("fuzz_%d" % n, p)
        for _ in range(rnd.randint(20, 60)):
            r = rnd.random()
            if r < 0.30:
                name, lo, hi, init = rnd.choice(knobs)
                v = rnd.choice((lo, hi, init, rnd.randint(lo, hi), rnd.randint(lo, hi)))
                w("param %s %d" % (name, v))
            elif r < 0.36 and len(presets) > 1:
                w("preset %s" % rnd.choice(presets))
            elif r < 0.56:
                w("gate_on %d" % rnd.randint(1, 127))
                if rnd.random() < 0.8:
                    w("gate_off")
            elif r < 0.62:
                w("gate_off")
            elif r < 0.72:
                w("note_rel %d %d" % (rnd.randint(-14, 14), rnd.randint(1, 127)))
            elif r < 0.76:
                w("note_off_rel %d" % rnd.randint(-14, 14))
            elif r < 0.78:
                w("note_on %d %d" % (rnd.randint(20, 110), rnd.randint(1, 127)))
            elif r < 0.80:
                w("bend %d" % rnd.choice((0, 4096, 8192, 12000, 16383)))
            elif r < 0.81:
                w("all_off")
            elif r < 0.815:
                w("reset")
            elif r < 0.82:
                w("suspend")
                w("resume")
            else:
                w("render %d" % rnd.choice((64, 128, 640, 2400, 4800, 9600)))
        w("render %d" % ms(600))

    sys.stdout.write("\n".join(out) + "\n")


if __name__ == "__main__":
    main()
