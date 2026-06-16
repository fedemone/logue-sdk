#!/usr/bin/env python3
"""Generate drum_patches.h for the EffeESP32 drumlogue unit from the original
copych ESP32-S3 FM Drum Synth drumkit JSON.

Selection rules (per task spec):
  - Notes 35..81 are the 47 General-MIDI percussion instruments and are always
    imported, using the GM names from GmDrums.h.
  - Notes 0..34 and 82..127 are non-GM slots; import them only when they carry
    a *meaningful* (non-empty name) and *unique* parameter set (not a duplicate
    of an already-imported patch), giving them descriptive short names.
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
    lines.append(f"  /* {full} */")
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
print(f"Total instruments: {len(selected)} (47 GM + {len(selected)-47} extras)")
print("Extras:", [s for s,_,_,_ in selected[47:]])
