#!/usr/bin/env python3
"""Generate drum_patches.h for the EffeESP32 drumlogue unit from the original
copych ESP32-S3 FM Drum Synth drumkit JSON.

Selection rules:
  - Notes 35..81 are the 47 General-MIDI percussion instruments and are always
    imported, using the GM names.
  - Notes 0..34 and 82..127 are non-GM slots; import them only when they carry
    a *meaningful* (non-empty name) and *unique* parameter set (not a duplicate
    of an already-imported patch), giving them descriptive short names.

Naming: from the GM map, NOT from the kit's `name` field.  This was briefly
changed to use the kit's names and had to be changed back; the evidence that
the GM slot is the real identity and the `name` field is not:

  - Slots 35..50 carry GM's own instrument names verbatim in `name`:
    35 BassDrum, 36 Kick, 37 SideStick, 38 AccSnare, 39 Hand Claps, 41/43/45/
    47/48/50 Tom, 42/44/46 Hi Hat, 49 Crash 1.  A kit that merely "occupied"
    the GM slots would not line up for sixteen consecutive ones.
  - Slots 88..127 repeat one 13-entry sequence (Sub Kick, Noise Clap, Closed
    Hat, Deep Tom, Snare Body, Snare Noise, Metal Stack, Noise Bell, Chime,
    Tight Clap, Tick Click, Glass FX, Rail bell) three times over, each round
    byte-identical in dec/rel/vol.  Those are the editor's *starting patches*.
    `name` records which template a slot was seeded from, not what it became.
  - Where a name and its GM slot disagree, the parameters side with the slot:
    55 "Cymbal" has a 1.5 s decay (GM Splash) and 57 "Cymbal" an 8.0 s decay
    (GM Crash 2); 51 "Closed Hat" rings for 4.4 s (GM Ride Cymbal 1); 53
    "Snare Body" for 6.1 s (GM Ride Bell); 60..66 are all "Bongo" and GM has
    seven hand drums there.  A closed hat does not ring for four seconds.

VOICING overrides (see VOICE_EDITS): a handful of imported slots keep envelope
or algorithm values that are wrong for what the slot is on this instrument.
They are applied *after* selection so the instrument list, its order and the
duplicate filter stay keyed to the untouched source data.
"""
#
# Usage:
#   1. Download the source drumkit JSON (commit 6e47275):
#        curl -L -o tools/Drumkit_default.json \
#          https://raw.githubusercontent.com/copych/ESP32-S3_FM_Drum_Synth/6e47275a04ffe28770613a126c6da97518948d9f/FMDrums/data/drumkits/Drumkit_default.json
#   2. python3 tools/gen_patches.py [path/to/Drumkit_default.json]
#
import json, sys, os, re

here = os.path.dirname(os.path.abspath(__file__))
src = sys.argv[1] if len(sys.argv) > 1 else os.path.join(here, "Drumkit_default.json")
dst = os.path.normpath(os.path.join(here, os.pardir, "drum_patches.h"))

with open(src) as f:
    kit = json.load(f)
patches = kit["patches"]
assert len(patches) == 128, len(patches)

# GM names (notes 35..81) -> compact display names (<= 9 chars for the OLED).
GM = {
    35: ("ABassDr",  "Acoustic Bass Drum"),
    36: ("Kick",     "Bass Drum 1"),
    37: ("SideStk",  "Side Stick"),
    38: ("Snare",    "Acoustic Snare"),
    39: ("Clap",     "Hand Clap"),
    40: ("ElSnare",  "Electric Snare"),
    41: ("LFlrTom",  "Low Floor Tom"),
    42: ("ClHat",    "Closed Hi-Hat"),
    43: ("HFlrTom",  "High Floor Tom"),
    44: ("PedHat",   "Pedal Hi-Hat"),
    45: ("LowTom",   "Low Tom"),
    46: ("OpHat",    "Open Hi-Hat"),
    47: ("LMidTom",  "Low-Mid Tom"),
    48: ("HMidTom",  "Hi-Mid Tom"),
    49: ("Crash1",   "Crash Cymbal 1"),
    50: ("HighTom",  "High Tom"),
    51: ("Ride1",    "Ride Cymbal 1"),
    52: ("ChinaCy",  "Chinese Cymbal"),
    53: ("RideBel",  "Ride Bell"),
    54: ("Tambrn",   "Tambourine"),
    55: ("Splash",   "Splash Cymbal"),
    56: ("Cowbell",  "Cowbell"),
    57: ("Crash2",   "Crash Cymbal 2"),
    58: ("Vibrslp",  "Vibraslap"),
    59: ("Ride2",    "Ride Cymbal 2"),
    60: ("HiBongo",  "Hi Bongo"),
    61: ("LoBongo",  "Low Bongo"),
    62: ("MHConga",  "Mute Hi Conga"),
    63: ("OHConga",  "Open Hi Conga"),
    64: ("LoConga",  "Low Conga"),
    65: ("HiTimbl",  "High Timbale"),
    66: ("LoTimbl",  "Low Timbale"),
    67: ("HiAgogo",  "High Agogo"),
    68: ("LoAgogo",  "Low Agogo"),
    69: ("Cabasa",   "Cabasa"),
    70: ("Maracas",  "Maracas"),
    71: ("SWhistl",  "Short Whistle"),
    72: ("LWhistl",  "Long Whistle"),
    73: ("SGuiro",   "Short Guiro"),
    74: ("LGuiro",   "Long Guiro"),
    75: ("Claves",   "Claves"),
    76: ("HiWdBlk",  "Hi Wood Block"),
    77: ("LoWdBlk",  "Low Wood Block"),
    78: ("MCuica",   "Mute Cuica"),
    79: ("OCuica",   "Open Cuica"),
    80: ("MTrngl",   "Mute Triangle"),
    81: ("OTrngl",   "Open Triangle"),
}

# Compact names for the extra (non-GM) named slots.
EXTRA = {
    "Sub Kick":   "SubKick",
    "Noise Clap": "NzClap",
    "Closed Hat": "ClHat2",
    "Deep Tom":   "DeepTom",
    "Snare Body": "SnBody",
    "Snare Noise":"SnNoise",
    "Metal Stack":"MtlStk",
    "Twirl":      "Twirl",
    "Glass Bell": "GlasBel",
    "HighQ":      "HighQ",
    "SnareSlap":  "SnSlap",
    "Noise Bell": "NzBell",
    "Chime":      "Chime",
    "Tight Clap": "TgtClap",
    "Tick Click": "TickClk",
    "Glass FX":   "GlasFX",
    "Rail bell":  "RailBel",
}

# Voicing overrides, keyed by source slot: {slot: {field: value}}.
#
# The imported kit was authored for a machine with no polyphony ceiling and no
# 16-step grid in front of it.  Sixteen of its slots ring for 2.8-8.0 s, which
# on a drumlogue part means every step of a pattern is still sounding when the
# next one lands: the part reads as one continuous wash rather than as hits,
# and the voice pool spends itself on tails nobody hears.  These are the slots
# where that was audible on hardware.
#
# `rel` is the length control.  The sequencer gates a step off within a few ms
# of the hit, so the envelope leaves DECAY for RELEASE almost immediately and
# `rel` shapes the whole audible tail (see Adsr::end / noteOff).  Measured on
# untouched instruments: Cabasa (dec 600, rel 300) tails at 0.302 s, HiWdBlk
# (120/50) at 0.058 s -- the tail follows `rel` and ignores `dec`.
#
# `dec` is not dead, though, and the two are not interchangeable.  Decay runs
# for the length of the gate, so it sets the level release *starts from*: a
# 50 ms decay has already taken the envelope well down by the time the gate
# ends, and the tail that follows is both shorter-sounding and quieter than the
# same `rel` behind a 1.3 s decay.  Short decay + long release is a soft swell;
# long decay + short release is a clean, full-level hit that stops.  So `dec`
# is set only where the body of the hit is what needs changing, and left at the
# kit's value otherwise.
#
# `alg` re-voices the operator routing where a changed envelope exposed a
# structure that only worked as a long ring.
VOICE_EDITS = {
    # --- cymbals: a crash that is a crash and not a drone -------------------
    49:  {"dec": 0.6,  "rel": 0.33},                # Crash1   (was 6.000/6.000)
    51:  {"dec": 0.6,  "rel": 0.6},                 # Ride1    (was 4.398/4.408)
    55:  {"dec": 0.6,  "rel": 0.6},                 # Splash   (was 1.500/1.500)
    57:  {"dec": 0.6,  "rel": 0.6},                 # Crash2   (was 8.000/8.000)
    59:  {"dec": 0.6,  "rel": 0.6},                 # Ride2    (was 3.665/3.725)
    52:  {"dec": 0.05, "rel": 0.6},                 # ChinaCy  (was 3.128/3.128)
    53:  {"dec": 0.05, "rel": 0.6,  "alg": 17},     # RideBel  (was 6.113/6.156)
    # --- release only: the body of the hit is already right -----------------
    46:  {"rel": 0.18},                             # OpHat    (was 1.332, dec 1.325 kept)
    29:  {"rel": 0.33, "alg": 13},                  # MtlStk   (was 0.600, dec 0.600 kept)
    # --- short, percussive slots the kit left ringing -----------------------
    58:  {"dec": 0.05, "rel": 0.18},                # Vibrslp  (was 2.798/2.798)
    62:  {"dec": 0.05, "rel": 0.33},                # MHConga  (was 0.070/0.070)
    67:  {"dec": 0.05, "rel": 0.05, "alg": 9},      # HiAgogo  (was 0.350/0.466)
    81:  {"dec": 0.05, "rel": 0.05},                # OTrngl   (was 6.270/6.229)
    87:  {"dec": 0.05, "rel": 0.05, "alg": 1},      # RailBel  (was 6.300/6.100)
    100: {"dec": 0.05, "rel": 0.05, "alg": 0},      # RailBe2  (was 6.300/6.100)
}

def param_key(p):
    """Hashable signature of the sound-defining parameters (ignores name)."""
    ops = tuple((round(o["ratio"],4), round(o["detune"],4), round(o["fb"],4),
                 round(o["vol"],4), o["wave"]) for o in p["ops"])
    return (p["alg"], round(p["freq"],4), round(p["vol"],4), round(p["pan"],4),
            round(p["atk"],5), round(p["hold"],5), round(p["dec"],5),
            round(p["sus"],4), round(p["rel"],5), p["flt"],
            round(p["filterFreq"],2), round(p["filterReso"],4),
            round(p["filterMorph"],4), ops)

selected = []      # list of (short_name, full_name, patch_dict, midi_note)
seen = set()

# 1) GM range 35..81
for n in range(35, 82):
    p = patches[n]
    short, full = GM[n]
    selected.append((short, full, p, n))
    seen.add(param_key(p))

# 2) extras 0..34 and 82..127, only meaningful + unique
for n in list(range(0, 35)) + list(range(82, 128)):
    p = patches[n]
    name = p["name"].strip()
    if not name:
        continue
    k = param_key(p)
    if k in seen:
        continue
    seen.add(k)
    short = EXTRA.get(name, re.sub(r"[^A-Za-z0-9]", "", name)[:7] or "Perc")
    # keep display names unique
    base, used = short, {s for s, _, _, _ in selected}
    suffix = 2
    while short in used:
        short = (base[:6] + str(suffix))
        suffix += 1
    selected.append((short, name, p, n))

assert len({s for s, _, _, _ in selected}) == len(selected), "duplicate panel label"

# 3) apply the voicing overrides.  After selection, so that the instrument list
#    and the duplicate filter above stay keyed to the untouched source data --
#    an edit that made two slots identical must not silently drop one of them.
#    Patch dicts are shared with `patches`, so copy before writing.
edited = {}
for i, (short, full, p, n) in enumerate(selected):
    if n not in VOICE_EDITS:
        continue
    p = dict(p)
    for field, value in VOICE_EDITS[n].items():
        p[field] = value
    selected[i] = (short, full, p, n)
    edited[short] = n
missing = sorted(set(VOICE_EDITS) - set(edited.values()))
assert not missing, f"VOICE_EDITS names slots that are not imported: {missing}"

WF = {0:"WF_SINE",1:"WF_COSINE",2:"WF_TRIANGLE",3:"WF_SQUARE",4:"WF_SAW",
      # original has 10 waveforms; the negative variants fold onto base shapes.
      5:"WF_SINE",6:"WF_COSINE",7:"WF_TRIANGLE",8:"WF_SQUARE",9:"WF_SAW"}

def f(x):
    s = f"{float(x):.6g}"
    if not any(c in s for c in ".eEnN"):  # ensure a valid float literal
        s += ".0"
    return s + "f"

lines = []
lines.append("#pragma once")
lines.append("")
lines.append("/**")
lines.append(" * @file drum_patches.h")
lines.append(" * @brief Instrument patch table — auto-generated, DO NOT EDIT BY HAND.")
lines.append(" *")
lines.append(" * Source data: copych/ESP32-S3_FM_Drum_Synth, FMDrums/data/drumkits/")
lines.append(" *              Drumkit_default.json (commit 6e47275).  MIT License.")
lines.append(" * Generator:   tools/gen_patches.py")
lines.append(" *")
lines.append(" * Layout mirrors the original FmDrumPatch (FmPatch.h): a flat struct of")
lines.append(" * fixed parameters.  Selecting an instrument copies one of these structs")
lines.append(" * into the synth working cache; the UI then edits the cached copy.")
lines.append(" *")
lines.append(" * Entries marked `[voiced]` carry a decay/release (and sometimes algorithm)")
lines.append(" * override from the generator's VOICE_EDITS table rather than the source")
lines.append(" * kit's value; the comment gives the original.")
lines.append(" */")
lines.append("")
lines.append('#include "fm_voice6.h"')
lines.append("")
lines.append(f"#define DRUM_INST_COUNT {len(selected)}")
lines.append("")
lines.append("static const fm_drum_patch_t g_drum_patches[DRUM_INST_COUNT] = {")
for short, full, p, note in selected:
    ops = []
    for o in p["ops"]:
        ops.append(f"{{ {f(o['ratio'])}, {f(o['detune'])}, {f(o['fb'])}, "
                   f"{f(o['vol'])}, {WF[int(o['wave'])]} }}")
    ops_str = ",\n      ".join(ops)
    note_txt = ""
    if note in VOICE_EDITS:
        orig = patches[note]
        was = [f"{k} {orig[k]:g}" if k != "alg" else f"alg {orig[k]}"
               for k in VOICE_EDITS[note]]
        note_txt = "  [voiced] was " + ", ".join(was)
    lines.append(f"  /* {full}{note_txt} */")
    lines.append("  {")
    lines.append(f"    {int(p['alg'])}, {f(p['freq'])}, {f(p['vol'])}, {f(p['pan'])},")
    lines.append(f"    {f(p['atk'])}, {f(p['hold'])}, {f(p['dec'])}, {f(p['sus'])}, {f(p['rel'])},")
    lines.append(f"    {f(p['veloMod'])}, {int(p['flt'])}, {f(p['filterFreq'])}, "
                 f"{f(p['filterReso'])}, {f(p['filterMorph'])},")
    lines.append(f"    {{ {ops_str} }}")
    lines.append("  },")
lines.append("};")
lines.append("")
lines.append("static const char* const g_drum_inst_names[DRUM_INST_COUNT] = {")
row = "  "
for short, full, p, note in selected:
    row += f'"{short}", '
    if len(row) > 76:
        lines.append(row.rstrip()); row = "  "
if row.strip():
    lines.append(row.rstrip())
lines.append("};")
lines.append("")

# Canonical trigger note for each instrument: the source MIDI note in the
# original drumkit (GM note for 35..81, original slot index for the extras).
lines.append("static const uint8_t g_drum_inst_notes[DRUM_INST_COUNT] = {")
row = "  "
for short, full, p, note in selected:
    row += f"{note}, "
    if len(row) > 76:
        lines.append(row.rstrip()); row = "  "
if row.strip():
    lines.append(row.rstrip())
lines.append("};")
lines.append("")

with open(dst, "w") as out:
    out.write("\n".join(lines) + "\n")

print(f"Wrote {dst}")
print(f"Total instruments: {len(selected)} "
      f"({sum(1 for _,_,_,n in selected if 35 <= n <= 81)} from slots 35-81 + "
      f"{sum(1 for _,_,_,n in selected if n < 35 or n > 81)} from the rest)")
print("Labels:", ", ".join(s for s, _, _, _ in selected))
print("Voiced:", ", ".join(f"{s}(slot {n})" for s, n in sorted(edited.items(), key=lambda kv: kv[1])))
