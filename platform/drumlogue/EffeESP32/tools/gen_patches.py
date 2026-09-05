#!/usr/bin/env python3
"""Generate drum_patches.h for the EffeESP32 drumlogue unit from the original
copych ESP32-S3 FM Drum Synth drumkit JSON.

Selection rules:
  - Notes 35..81 are the 47 slots a General-MIDI percussion map addresses, and
    are always imported.
  - Notes 0..34 and 82..127 are the remaining slots; import them only when they
    carry a *meaningful* (non-empty name) and *unique* parameter set (not a
    duplicate of an already-imported patch).

Naming: from the kit's own `name` field, NOT from the GM map.  Drumkit_default
is not a General MIDI kit -- it just occupies those slots -- and labelling it
with GM names named a different instrument than the slot actually holds in 27
of the 47 cases.  Slot 51, "Ride Cymbal 1" under the GM map, is the kit's
"Closed Hat" with a 4.4 s decay; 52 is a "Deep Tom", 53 a "Snare Body", 56 a
"Noise Bell", 70 a "Chime".  The GM slot number is kept in the C comment for
traceability.

The kit reuses names heavily (seven slots are "Bongo", six "Tom"), so a repeated
name is numbered in slot order -- Tom1..Tom6 ascend in pitch, as the GM map's
tom slots do.  Numbering runs over the whole selection, so a name appearing both
in and outside the GM range is numbered once across both.
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

# Kit name -> panel label.  Two forms: the first is used when the name occurs
# once in the selection, the second is the base for a numeric suffix when it
# occurs more than once (kept to 6 chars so "base + digit" stays within the 7
# the OLED shows).  Anything not listed falls back to stripped-and-truncated.
KIT_LABEL = {
    "BassDrum":    ("BassDrm", "BassDr"),
    "Kick":        ("Kick",    "Kick"),
    "SideStick":   ("SideStk", "SidStk"),
    "AccSnare":    ("AccSnar", "AcSnar"),
    "Hand Claps":  ("HndClap", "HndClp"),
    "Snare Body":  ("SnBody",  "SnBody"),
    "Snare Noise": ("SnNoise", "SnNois"),
    "SnareSlap":   ("SnrSlap", "SnSlap"),
    "Tom":         ("Tom",     "Tom"),
    "Deep Tom":    ("DeepTom", "DpTom"),
    "Bongo":       ("Bongo",   "Bongo"),
    "Hi Hat":      ("HiHat",   "HiHat"),
    "Closed Hat":  ("ClosHat", "ClHat"),
    "Crash 1":     ("Crash1",  "Crash"),
    "Cymbal":      ("Cymbal",  "Cymbal"),
    "Noise Bell":  ("NoisBel", "NzBell"),
    "Glass Bell":  ("GlasBel", "GlasBl"),
    "Glass FX":    ("GlassFX", "GlasFX"),
    "Metal Stack": ("MtlStak", "MtlStk"),
    "Rail bell":   ("RailBel", "RailBl"),
    "Chime":       ("Chime",   "Chime"),
    "Tight Clap":  ("TgtClap", "TgtClp"),
    "Noise Clap":  ("NoisClp", "NzClap"),
    "Tick Click":  ("TickClk", "TickCl"),
    "Sub Kick":    ("SubKick", "SubKck"),
    "Whistle":     ("Whistle", "Whistl"),
    "Guiro":       ("Guiro",   "Guiro"),
    "Twirl":       ("Twirl",   "Twirl"),
    "HighQ":       ("HighQ",   "HighQ"),
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

# 1) pick the patches, in slot order, before naming any of them: a repeated kit
#    name has to be numbered over the whole selection, not per range.
picked = []        # list of (patch_dict, midi_note, gm_label or None)
seen = set()

for n in range(35, 82):                       # slots a GM percussion map addresses
    picked.append((patches[n], n, GM[n][1]))
    seen.add(param_key(patches[n]))

for n in list(range(0, 35)) + list(range(82, 128)):
    p = patches[n]
    if not p["name"].strip():
        continue
    k = param_key(p)
    if k in seen:
        continue
    seen.add(k)
    picked.append((p, n, None))

# 2) label from the kit's own name, numbering repeats in slot order.
counts = {}
for p, _, _ in picked:
    counts[p["name"].strip()] = counts.get(p["name"].strip(), 0) + 1

selected = []      # list of (short_name, full_name, patch_dict, midi_note)
used_idx = {}
for p, n, gm_label in picked:
    kit = p["name"].strip()
    uniq, base = KIT_LABEL.get(kit, (None, None))
    if uniq is None:
        uniq = re.sub(r"[^A-Za-z0-9]", "", kit)[:7] or "Perc"
        base = uniq[:6]
    if counts[kit] == 1:
        short = uniq
    else:
        used_idx[kit] = used_idx.get(kit, 0) + 1
        short = f"{base}{used_idx[kit]}"
    assert len(short) <= 7, (short, kit)
    # Provenance in the generated comment: the kit's name, the source slot, and
    # the GM instrument that slot would be under a General MIDI map -- which is
    # usually something else entirely.
    full = f"{kit} (slot {n}" + (f", GM {gm_label}" if gm_label else "") + ")"
    selected.append((short, full, p, n))

assert len({s for s, _, _, _ in selected}) == len(selected), "duplicate panel label"

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
print(f"Total instruments: {len(selected)} "
      f"({sum(1 for _,_,_,n in selected if 35 <= n <= 81)} from slots 35-81 + "
      f"{sum(1 for _,_,_,n in selected if n < 35 or n > 81)} from the rest)")
print("Labels:", ", ".join(s for s, _, _, _ in selected))
