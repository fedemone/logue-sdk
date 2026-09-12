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

The kit's own parameter values are imported verbatim -- they are the authored
settings and are assumed correct.  Where the port sounded wrong, the fault was
in the engine, not the data: waveforms 5..9 were folded onto 0..4, operator
feedback was clamped at 7 when the kit goes to 10, pan was linear instead of
equal-power, and a negative effective operator frequency was clamped to zero.
Those are fixed in fm_operator.h / fm_voice6.h; see README "Import fidelity".

VOICING overrides (see VOICE_EDITS) are the one deliberate departure, and they
are *not* corrections of bad data: the kit's long cymbal tails are right for
the original's pad-triggered playing, and too long for a step sequencer driving
a part.  They are applied *after* selection so the instrument list, its order
and the duplicate filter stay keyed to the untouched source data.
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
# NOTE ON OPERATOR NUMBERING: the "opvol" / "opwave" keys are 0-based indices
# into ops[], matching fm_voice6.h's algorithm graphs.  Everything a human
# reads -- these comments, the generated [voiced] comments, and the panel's
# Op1..Op6 knobs -- is 1-based.  So {4: ...} edits the operator called op5.
VOICE_EDITS = {
    # --- cymbals: a crash that is a crash and not a drone -------------------
    49:  {"dec": 0.6,  "rel": 0.33},                # Crash1   (was 6.000/6.000)
    51:  {"dec": 0.6,  "rel": 0.6},                 # Ride1    (was 4.398/4.408)
    55:  {"dec": 0.6,  "rel": 0.6},                 # Splash   (was 1.500/1.500)
    57:  {"dec": 0.6,  "rel": 0.6},                 # Crash2   (was 8.000/8.000)
    59:  {"dec": 0.6,  "rel": 0.6},                 # Ride2    (was 3.665/3.725)
    52:  {"dec": 0.05, "rel": 0.6},                 # ChinaCy  (was 3.128/3.128)
    # RideBel: algorithm 17 drives its carrier at a modulation index near 92 and
    # turns the bell into broadband hiss -- measured spectral centroid 14.2 kHz,
    # flatness 0.67, -33.5 LUFS, which is the "outputs no sound" report.  The
    # kit's own algorithm 10 uses ops 4/5 (volume 0.03) as near-silent
    # modulators and rings ops 0/1/2 as carriers: 4.3 kHz, flatness 0.015.  Back
    # to the kit's, with the level the kit's 6 s ring no longer pays for.
    53:  {"dec": 0.6,  "rel": 0.6,  "vol": 1.0},    # RideBel  (was 6.113/6.156, alg 17 tried)
    # --- release only: the body of the hit is already right -----------------
    46:  {"rel": 0.18},                             # OpHat    (was 1.332, dec 1.325 kept)
    29:  {"rel": 0.33, "alg": 13},                  # MtlStk   (was 0.600, dec 0.600 kept)
    # --- short, percussive slots the kit left ringing -----------------------
    58:  {"dec": 0.05, "rel": 0.18},                # Vibrslp  (was 2.798/2.798)
    # MHConga: the 50 ms decay that makes it read as muted also cost it 5 LU
    # against the rest of the hand-drum family (-26.9 against -21.1..-23.6), far
    # enough to disappear under them.  Level only, length untouched: patch
    # volume to the 2.0 the Level knob tops out at, and the carrier op1 from the
    # kit's 0.8 to 1.0, which fm_operator clamps at.  That is the whole +3.9 dB
    # available without lengthening it -- peak -5.8 -> -1.9 dBFS, still clear of
    # LIMIT_CEILING, tail unchanged at 0.271 s.
    62:  {"dec": 0.05, "rel": 0.33, "vol": 2.0, "opvol": {0: 1.0}},  # MHConga (was 0.070/0.070, vol 1.6)
    67:  {"dec": 0.05, "rel": 0.05, "alg": 9},      # HiAgogo  (was 0.350/0.466)
    81:  {"dec": 0.05, "rel": 0.05},                # OTrngl   (was 6.270/6.229)
    87:  {"dec": 0.05, "rel": 0.05, "alg": 1},      # RailBel  (was 6.300/6.100)
    100: {"dec": 0.05, "rel": 0.05, "alg": 0},      # RailBe2  (was 6.300/6.100)

    # --- unvoiced template slots: giving the carrier something to say -------
    # Slots 60..66 and 86 all carry the editor's default operator set, in which
    # op6 has ratio 0 AND detune 0.  Its phase increment is therefore zero and
    # its feedback loop is seeded at last_out = 0, so sin(fb * 0) = 0 forever:
    # the operator emits exactly nothing.  Algorithm 2 uses op6 as its only
    # modulator, so the carrier is never modulated and the slot is a bare sine
    # at its base frequency -- measured flatness 0.000, centroid == fundamental.
    # A bare sine with a 5 ms attack is the "zapping, feel not correct" report.
    # Algorithm 1 instead rings ops 1, 5 and 6 as carriers, so the silent op6
    # costs nothing and op5 (ratio 2.5) supplies the inharmonic partial a hand
    # drum needs.  MHConga is the control: same algorithm 2, but its op6 has
    # ratio 0.98, so it modulates, and it was not reported.
    #
    # The kit then left 60, 63, 65 and 66 byte-identical -- four differently
    # named instruments at 211 Hz with the same operators.  They are spread
    # here into the ladder GM implies, using the three levers this operator set
    # offers: base frequency, op5's volume (the 2.5x partial, which runs the
    # timbre from a bare fundamental at 0% to 50% off-fundamental at 80%), and
    # release.  Patch volume compensates the level op5 carries, so the family
    # stays within about 1.5 LU of itself.  Bongos sit highest and tightest,
    # congas lower, warmer and longer, timbales highest-but-metal: brightest
    # partial and the longest ring, because the shell is metal rather than
    # wood.  MHConga keeps the kit's 211 Hz, which lands it just above the open
    # conga -- muting a drum does raise its pitch, so that is left alone.
    60:  {"alg": 1, "freq": 250.0, "opvol": {4: 0.65}, "rel": 0.15, "vol": 1.58},  # HiBongo
    # LoBongo was seeded from the Rail bell template, so every operator but the
    # carrier is a square.  Ringing one as the partial put its centroid at
    # 4.8 kHz against 168..494 Hz for the rest of the family; op5 goes to a sine
    # so it contributes a partial rather than a harmonic stack.
    61:  {"alg": 1, "freq": 180.0, "opvol": {4: 0.55}, "opwave": {4: 0},
          "rel": 0.22, "vol": 1.55},                                     # LoBongo
    63:  {"alg": 1, "freq": 190.0, "opvol": {4: 0.35}, "rel": 0.35, "vol": 1.99},  # OHConga
    64:  {"alg": 1, "opvol": {4: 0.25}, "rel": 0.40, "vol": 2.00},                 # LoConga (kit 127 Hz)
    65:  {"alg": 1, "freq": 290.0, "rel": 0.70},                                   # HiTimbl (kit op5 0.8)
    66:  {"alg": 1, "freq": 225.0, "opvol": {4: 0.72}, "rel": 0.62, "vol": 1.49},  # LoTimbl
    # GlasFX keeps the kit's algorithm 11, whose modulators op2/op4/op6 sit at
    # the kit's operator volume 0.8 -- fm_level 5.77 times MOD_RANGE 16, a
    # modulation index near 92 and so 0.83 spectral flatness.  0.25 brings the
    # index to about 4: flatness 0.07, still glassy, no longer white.
    86:  {"opvol": {1: 0.25, 3: 0.25, 5: 0.25}},    # GlasFX   (ops were 0.8)
    # LoWdBlk is the "Closed Hat" template at 3200 Hz on algorithm 2, whose op6
    # (ratio 2, square, volume 0.8) is a live modulator at index 92: 98.8% of
    # its energy is off the fundamental.  Same lever as GlasFX -- op6 at 0.30
    # brings the index to ~5 and the off-fundamental share to 30%.  NOT an
    # algorithm change: every other algorithm here either rings op6 as a raw
    # 6.4 kHz square (66%) or, like 12 and 13, references none of the live
    # operators at all and emits a mathematically pure sine -- which is the
    # defect this block exists to fix, not a fix for it.
    77:  {"opvol": {5: 0.30}},                      # LoWdBlk  (was op6 vol 0.8)
    # Cabasa is a shaker, so broadband is right, but its energy sits at 11.8 kHz
    # -- hiss rather than beads.  Its noise comes from op3 waveshaping a
    # feedback chain, so modulator volume does not reach it; a low-pass does.
    69:  {"flt": 1, "filterFreq": 6000.0, "vol": 0.63},
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
        if field in ("opvol", "opwave"):      # {op index: volume / wave id}
            key = "vol" if field == "opvol" else "wave"
            p["ops"] = [dict(o) for o in p["ops"]]
            for op_i, op_v in value.items():
                p["ops"][op_i][key] = op_v
        else:
            p[field] = value
    selected[i] = (short, full, p, n)
    edited[short] = n
missing = sorted(set(VOICE_EDITS) - set(edited.values()))
assert not missing, f"VOICE_EDITS names slots that are not imported: {missing}"

# All ten upstream shapes.  These used to fold 5..9 onto 0..4, which silently
# dropped the sign inversion on 19 of the 59 imported instruments.
WF = {0:"WF_SINE",     1:"WF_COSINE",     2:"WF_TRIANGLE",
      3:"WF_SQUARE",   4:"WF_SAW",        5:"WF_NEG_SINE",
      6:"WF_NEG_COSINE", 7:"WF_NEG_TRIANGLE", 8:"WF_NEG_SQUARE",
      9:"WF_NEG_SAW"}

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
lines.append(" * Entries marked `[voiced]` carry an override from the generator's")
lines.append(" * VOICE_EDITS table rather than the source kit's value -- base frequency,")
lines.append(" * envelope times, algorithm, patch volume, filter settings, or an")
lines.append(" * operator's volume or waveform.  The comment gives the original for")
lines.append(" * each field that was overridden; operators are numbered op1..op6, as")
lines.append(" * on the panel.")
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
        was = []
        for k in VOICE_EDITS[note]:
            if k in ("opvol", "opwave"):
                sub = "vol" if k == "opvol" else "wave"
                was += [f"op{i+1} {sub} {orig['ops'][i][sub]:g}"
                        for i in VOICE_EDITS[note][k]]
            else:
                was.append(f"{k} {orig[k]:g}")
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
