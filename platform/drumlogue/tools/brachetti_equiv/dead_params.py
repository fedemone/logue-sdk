#!/usr/bin/env python3
"""dead_params.py — which parameters does each Brachetti family never hear?

    python3 dead_params.py <equiv_driver binary> [--points 9] [--families kick,tom]

Built against the ORIGINAL unit this is what decided which knobs each family
unit drops; built against a family unit it re-checks that unit's own knobs.  For every family, every member preset and every
parameter, it renders the same short session with the parameter at `points`
values spread over its whole range, under several CONTEXTS — the shipped
settings, and settings where the knobs that gate other knobs are moved (noise
opened up, the master filter closed, Partls/Model/Dkay moved, Velocity at both
ends, and a few random knob states).  A parameter is DEAD for a family when, in
every context and on every member, every value renders bit-identically.

Each session renders three spans so a difference can be located:
  strike  the sequencer gate (same-tick on/off), 0.6 s
  held    a MIDI note held 0.2 s, then released, 0.4 s
  panic   a strike cut by AllNoteOff, 0.3 s

This is what decided which knobs a family unit drops.  A knob that only moves
the `panic` span is reported separately: it changes nothing a player hears
except how fast AllNoteOff fades a voice.
"""
import argparse
import collections
import random
import subprocess
import sys
import tempfile

FAMILIES = {
    "kick":   ["Kick2", "808Sub", "Kick"],
    "tom":    ["Djambe", "Ac Tom", "DeepBs", "Conga", "Handpn", "Bongo", "RackTom"],
    "kernel": ["Timpni", "Taiko"],
    "snare":  ["AcSnre", "MrchSnr", "BrshSnr", "RimShot"],
    "bar":    ["Marmba", "TblrBel", "Vibrph", "Wodblk", "Kalimba", "StelPan",
               "Claves", "GlsBwl", "SltDrm", "GlsBotl"],
    "plate":  ["Cowbel", "Trngle", "BelTre", "Tick"],
    "cymbal": ["Cymbal", "Gong", "HHat-O", "Ride", "RidBel", "Splash"],
    "noise":  ["Clap", "Shaker", "HHat-C"],
}

CONTEXTS = [
    ("shipped", []),
    ("noisy",   [("NzMix", 100), ("NzRes", 1000), ("NzFltr", 2), ("NzFltFrq", 300)]),
    ("dark",    [("Cutoff", 300), ("Resnc", 300), ("Tone", -10)]),
    ("rich",    [("Partls", 4), ("Model", 4), ("Dkay", 120), ("Inharm", 60)]),
    ("thin",    [("Partls", 0), ("Model", 7), ("Dkay", 30), ("Mterl", 30)]),
    ("ghost",   [("Velocity", -100), ("MlltRes", 1000), ("HitPos", 90)]),
    ("wham",    [("Velocity", 100), ("VlMllRes", 100), ("VlMllStf", -100)]),
]


def parse_list(text):
    params, presets = {}, set()
    for line in text.splitlines():
        w = line.split()
        if w and w[0] == "param":
            params[w[2]] = (int(w[3]), int(w[4]), int(w[5]))
        elif w and w[0] == "preset":
            presets.add(line.split(None, 2)[2].strip())
    return params, presets


def session(w):
    w("gate_on 110")
    w("gate_off")
    w("render 28800")
    w("note_rel 0 100")
    w("render 9600")
    w("note_off_rel 0")
    w("render 19200")
    w("gate_on 127")
    w("render 4800")
    w("all_off")
    w("render 14400")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("driver")
    ap.add_argument("--points", type=int, default=9)
    ap.add_argument("--random-contexts", type=int, default=3)
    ap.add_argument("--families", default=",".join(FAMILIES))
    a = ap.parse_args()
    # Works on the original or on any family unit: presets and knobs the unit
    # does not have are skipped (a family unit only knows its own).
    params, have_presets = parse_list(subprocess.run([a.driver, "--list"], capture_output=True,
                                                     text=True, check=True).stdout)
    rnd = random.Random(99)
    knobs = [k for k in params if k not in ("Program",)]
    contexts = [(n, [(k, v) for k, v in c if k in params]) for n, c in CONTEXTS]
    for i in range(a.random_contexts):
        ctx = []
        for k in rnd.sample(knobs, 8):
            lo, hi, _ = params[k]
            if k == "Partls":
                hi = min(hi, 4)      # in the original 5-7 select a string resonator, not a count
            ctx.append((k, rnd.randint(lo, hi)))
        contexts.append(("rand%d" % i, ctx))

    lines = []
    w = lines.append
    fams = [fm for fm in a.families.split(",")
            if any(p in have_presets for p in FAMILIES[fm])]
    for fam in fams:
        for preset in [p for p in FAMILIES[fam] if p in have_presets]:
            for cname, ctx in contexts:
                for k in knobs:
                    lo, hi, _ = params[k]
                    if k == "Partls" and fam != "cymbal":
                        hi = min(hi, 4)
                    if k == "Program":
                        continue
                    n = a.points
                    vals = sorted({lo + (hi - lo) * i // (n - 1) for i in range(n)})
                    for v in vals:
                        w("scenario %s|%s|%s|%s|%d" % (fam, preset.replace(" ", "_"),
                                                       cname, k, v))
                        w("preset %s" % preset)
                        for ck, cv in ctx:
                            if ck != k:
                                w("param %s %d" % (ck, cv))
                        w("param %s %d" % (k, v))
                        session(w)
    script = "\n".join(lines) + "\n"
    with tempfile.NamedTemporaryFile("w", suffix=".script", delete=False) as f:
        f.write(script)
        path = f.name
    out = subprocess.run([a.driver, path], capture_output=True, text=True)
    if out.returncode != 0:
        sys.stderr.write(out.stderr)
        sys.exit(1)

    # (fam, knob) -> set of spans that ever differed
    groups = collections.defaultdict(set)    # (fam,preset,ctx,knob,span) -> hashes
    for line in out.stdout.splitlines():
        scen, span, h, _ = line.split()
        fam, preset, ctx, knob, _v = scen.split("|")
        groups[(fam, preset, ctx, knob, int(span))].add(h)
    live = collections.defaultdict(set)
    where = collections.defaultdict(set)
    for (fam, preset, ctx, knob, span), hs in groups.items():
        if len(hs) > 1:
            live[(fam, knob)].add(span)
            where[(fam, knob)].add("%s/%s" % (preset, ctx))
    span_name = {0: "strike", 1: "held", 2: "release", 3: "panic", 4: "panic"}
    for fam in fams:
        dead = [k for k in knobs if not live[(fam, k)]]
        panic_only = [k for k in knobs
                      if live[(fam, k)] and live[(fam, k)] <= {3, 4}]
        print("%-7s dead: %s" % (fam, ", ".join(dead) or "-"))
        if panic_only:
            print("%-7s only moves the AllNoteOff fade: %s" % ("", ", ".join(panic_only)))
            for k in panic_only:
                print("%-9s %s: %s" % ("", k, " ".join(sorted(where[(fam, k)]))[:200]))


if __name__ == "__main__":
    main()
