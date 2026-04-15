/**
 * @file fm_presets.c
 * @brief Factory preset data for FM Percussion Synth
 *
 * Provides the definition of FM_PRESETS[] using C99 designated initializers
 * with string literals for .name (char array members).
 */

#include "fm_presets.h"

const char preset_names[NUM_OF_PRESETS][NAME_LENGTH] =
{
    "DepTrbl",
    "MtlStrm",
    "ChrdPerc",
    "PhseDnc",
    "BplrBss",
    "SnreRll",
    "AbntMtl",
    "Polyrtm",
    "ResKick",
    "ResTom",
    "ResSnr",
    "ResMtl",
    "SlwEnv",
    "WahDrum",
    "NoisSwp",
    "FMBuzz",
    "GhstSnr",
    "RimPtch",
    "TomWah",
    "Shaker",
    // Gong character (EnvShape bit 7 set)
    "GongHit",
    "TmplBell",
    "MetlGong",
    // Euclidean tuning + MetalGate showcase presets
    "DimKit",
    "WholPrc",
    "HiHatSw"
};

const fm_preset_t FM_PRESETS[NUM_OF_PRESETS] = {
    // Preset 0: "Deep Tribal" (original)
    {
        "DepTrbl", 100, 80, 60, 70,
        80, 60, 30, 70,
        40, 50, 50, 40,
        0, 20, LFO_TARGET_PITCH, 30,
        0, 5, LFO_TARGET_INDEX, 20,   // EuclTun=0 (Off)
        20, 0,  // env_shape, voice_index
        RESONANT_MODE_BANDPASS, 2, 50, 30,
        {ENGINE_KICK, ENGINE_SNARE, ENGINE_METAL, ENGINE_PERC}
    },

    // Preset 1: "Metal Storm" (original)
    {
        "MtlStrm", 60, 80, 100, 50,
        40, 30, 60, 40,
        80, 90, 30, 60,
        6, 60, LFO_TARGET_PITCH, 20,
        0, 80, LFO_TARGET_LFO1_PHASE, 50,  // EuclTun=0 (Off)
        5, 0,
        RESONANT_MODE_PEAK, 90, 70, 60,
        {ENGINE_KICK, ENGINE_SNARE, ENGINE_METAL, ENGINE_PERC}
    },

    // Preset 2: "Chordal Perc" (original)
    {
        "ChrdPerc", 70, 70, 70, 70,
        50, 50, 40, 50,
        30, 40, 60, 50,
        2, 30, LFO_TARGET_PITCH, 40,
        0, 15, LFO_TARGET_PITCH, -30,  // EuclTun=0 (Off)
        40, 0,
        RESONANT_MODE_LOWPASS, 50, 30, 20,
        {ENGINE_KICK, ENGINE_SNARE, ENGINE_METAL, ENGINE_PERC}
    },

    // Preset 3: "Phase Dance" (original)
    {
        "PhseDnc", 80, 60, 80, 60,
        30, 40, 50, 60,
        50, 60, 40, 30,
        3, 45, LFO_TARGET_LFO2_PHASE, 70,
        0, 30, LFO_TARGET_LFO1_PHASE, 70,  // EuclTun=0 (Off)
        30, 0,
        RESONANT_MODE_HIGHPASS, 25, 40, 40,
        {ENGINE_KICK, ENGINE_SNARE, ENGINE_METAL, ENGINE_PERC}
    },

    // Preset 4: "Bipolar Bass" (original)
    {
        "BplrBss", 100, 40, 30, 50,
        90, 80, 20, 30,
        20, 30, 30, 20,
        1, 10, LFO_TARGET_PITCH, -50,
        0, 25, LFO_TARGET_NONE, 0,
        10, 0,
        RESONANT_MODE_LOWPASS, 10, 20, 10,
        {ENGINE_KICK, ENGINE_SNARE, ENGINE_METAL, ENGINE_PERC}
    },

    // Preset 5: "Snare Roll" (original)
    {
        "SnreRll", 30, 100, 40, 30,
        20, 30, 80, 60,
        30, 40, 40, 30,
        1, 90, LFO_TARGET_INDEX, 60,
        0, 45, LFO_TARGET_PITCH, 20,  // EuclTun=0 (Off)
        15, 0,
        RESONANT_MODE_PEAK, 2, 60, 70,
        {ENGINE_KICK, ENGINE_SNARE, ENGINE_METAL, ENGINE_PERC}
    },

    // Preset 6: "Ambient Metals" (original)
    {
        "AbntMtl", 40, 50, 100, 60,
        30, 70, 40, 50,
        70, 80, 50, 60,
        0, 15, LFO_TARGET_ENV, 40,
        0, 40, LFO_TARGET_PITCH, 20,  // EuclTun=0 (Off)
        90, 0,
        RESONANT_MODE_NOTCH, 70, 80, 50,
        {ENGINE_KICK, ENGINE_SNARE, ENGINE_METAL, ENGINE_PERC}
    },

    // Preset 7: "Polyrhythm" (original)
    {
        "Polyrtm", 90, 70, 50, 80,
        50, 50, 50, 50,
        50, 50, 50, 50,
        4, 35, LFO_TARGET_PITCH, 30,
        0, 55, LFO_TARGET_PITCH, 30,  // EuclTun=0 (Off)
        25, 0,
        RESONANT_MODE_BANDPASS, 50, 50, 30,
        {ENGINE_KICK, ENGINE_SNARE, ENGINE_METAL, ENGINE_PERC}
    },

    // ===== NEW RESONANT PRESETS =====

    // Preset 8: "ResoKick" - Resonant kick drum
    {
        "ResoKick", 100, 0, 0, 0,
        70, 60, 0, 0,
        0, 0, 0, 0,
        0, 10, LFO_TARGET_PITCH, 20,
        0, 5, LFO_TARGET_NONE, 0,
        25, 8,
        RESONANT_MODE_LOWPASS, 20, 40, 15,
        {ENGINE_RESONANT, ENGINE_SNARE, ENGINE_KICK, ENGINE_PERC}
    },

    // Preset 9: "ResoTom" - Resonant tom
    {
        "ResoTom", 0, 0, 0, 100,
        50, 50, 0, 0,
        0, 0, 50, 50,
        1, 15, LFO_TARGET_PITCH, 30,
        0, 8, LFO_TARGET_INDEX, 20,  // EuclTun=0 (Off)
        35, 1,
        RESONANT_MODE_BANDPASS, 20, 60, 25,
        {ENGINE_KICK, ENGINE_SNARE, ENGINE_METAL, ENGINE_RESONANT}
    },

    // Preset 10: "ResoSnare" - Resonant snare
    {
        "ResoSnare", 0, 100, 0, 0,
        0, 0, 50, 50,
        0, 0, 0, 0,
        2, 20, LFO_TARGET_PITCH, 40,
        0, 10, LFO_TARGET_INDEX, 30,  // EuclTun=0 (Off)
        20, 3,
        RESONANT_MODE_HIGHPASS, 20, 50, 45,
        {ENGINE_KICK, ENGINE_RESONANT, ENGINE_METAL, ENGINE_PERC}
    },

    // Preset 11: "ResoMetal" - Resonant metal/cymbal
    {
        "ResoMetal", 0, 0, 100, 0,
        0, 0, 0, 0,
        70, 80, 0, 0,
        8, 30, LFO_TARGET_PITCH, 50,
        0, 40, LFO_TARGET_INDEX, 40,  // EuclTun=0 (Off)
        10, 2,
        RESONANT_MODE_PEAK, 20, 80, 80,
        {ENGINE_KICK, ENGINE_SNARE, ENGINE_RESONANT, ENGINE_PERC}
    },

    // ===== NEW PRESETS (exploiting phase sync, NOISE_MIX, RES_MORPH) =====

    // Preset 12: "SlwEnv" - Slow ramp LFO (0.5 Hz) on ENV as a secondary
    // envelope shaper: the kick swells in, the snare gets a slow fade-in.
    // Because LFO phase resets on each trigger, this is one-shot per hit.
    {
        "SlwEnv", 100, 80, 0, 0,
        70, 60, 20, 40,
        0, 0, 0, 0,
        1, 5, LFO_TARGET_ENV, 60,      // LFO1: 0.5 Hz ramp → ENV swell
        0, 0, LFO_TARGET_NONE, 0,
        30, 0,
        RESONANT_MODE_LOWPASS, 20, 30, 15,
        {ENGINE_KICK, ENGINE_SNARE, ENGINE_METAL, ENGINE_PERC}
    },

    // Preset 13: "WahDrum" - LFO→RES_MORPH creates auto-wah filter sweep.
    // Resonant is on voice 2 (metal slot).  Medium rate (4 Hz) triangle
    // gives a fast wah on every hit; synced to trigger so it starts fresh.
    {
        "WahDrum", 80, 60, 0, 70,
        60, 50, 30, 40,
        0, 0, 60, 50,
        0, 35, LFO_TARGET_RES_MORPH, 80,  // LFO1: triangle 4 Hz → filter morph
        0, 10, LFO_TARGET_PITCH, 20,       // LFO2: slow pitch drift; EuclTun=0
        25, 2,
        RESONANT_MODE_BANDPASS, 50, 70, 40,
        {ENGINE_KICK, ENGINE_SNARE, ENGINE_RESONANT, ENGINE_PERC}
    },

    // Preset 14: "NoisSwp" - LFO→NOISE_MIX sweeps snare from pure tone to
    // full noise and back over 2 seconds (0.5 Hz).  Interesting on slow
    // patterns: each snare hit starts with crack, fades to hiss.
    {
        "NoisSwp", 40, 100, 0, 50,
        30, 50, 50, 60,
        50, 60, 0, 0,
        1, 5, LFO_TARGET_NOISE_MIX, 90,  // LFO1: slow ramp → noise blend
        0, 20, LFO_TARGET_PITCH, -20,     // LFO2: slight negative pitch wobble
        20, 0,
        RESONANT_MODE_HIGHPASS, 30, 50, 45,
        {ENGINE_KICK, ENGINE_SNARE, ENGINE_METAL, ENGINE_PERC}
    },

    // Preset 15: "FMBuzz" - Both LFOs near audio rate targeting INDEX.
    // LFO1 at ~15 Hz and LFO2 at ~20 Hz create amplitude/FM beating.
    // The difference frequency (5 Hz) causes a slow tremolo on top.
    {
        "FMBuzz", 90, 60, 80, 60,
        50, 40, 40, 60,
        70, 80, 50, 50,
        0, 82, LFO_TARGET_INDEX, 70,   // LFO1: ~15 Hz triangle → FM index buzz
        0, 100, LFO_TARGET_INDEX, 50,  // LFO2: ~20 Hz triangle → beating
        15, 0,
        RESONANT_MODE_PEAK, 60, 60, 50,
        {ENGINE_KICK, ENGINE_SNARE, ENGINE_METAL, ENGINE_PERC}
    },

    // Preset 16: "GhstSnr" - Ghost snare: low probability (30%), gentle
    // RES_MORPH sweep on the resonant filter gives each ghost hit a slightly
    // different tonal colour.  Voice alloc K-S-M-R.
    {
        "GhstSnr", 70, 30, 50, 40,
        40, 40, 70, 50,
        40, 50, 0, 0,
        0, 18, LFO_TARGET_RES_MORPH, 60,  // LFO1: 2 Hz triangle → filter colour
        0, 8,  LFO_TARGET_PITCH, -30,      // LFO2: slow negative drift; EuclTun=0
        35, 1,
        RESONANT_MODE_BANDPASS, 40, 65, 35,
        {ENGINE_KICK, ENGINE_SNARE, ENGINE_METAL, ENGINE_RESONANT}
    },

    // Preset 17: "RimPtch" - Ramp LFO on pitch creates a falling tone edge
    // (rim-shot / stick click character) on metal and perc voices.
    // Short env (tight), medium ramp rate (3 Hz → 333 ms period, plenty
    // for a single hit since LFO resets on trigger).
    {
        "RimPtch", 50, 60, 80, 70,
        20, 30, 40, 50,
        60, 70, 50, 40,
        1, 25, LFO_TARGET_PITCH, -70,  // LFO1: ramp, negative → falling pitch
        0, 0,  LFO_TARGET_NONE, 0,
        10, 0,
        RESONANT_MODE_HIGHPASS, 70, 40, 60,
        {ENGINE_KICK, ENGINE_SNARE, ENGINE_METAL, ENGINE_PERC}
    },

    // Preset 18: "TomWah" - Resonant tom with RES_MORPH modulated by a slow
    // triangle (1 Hz).  Each tom hit sweeps the filter from low to high and
    // back, giving a pitch/wah character.  Voice alloc K-S-M-R.
    {
        "TomWah", 0, 0, 0, 100,
        50, 60, 0, 0,
        0, 0, 60, 60,
        0, 10, LFO_TARGET_RES_MORPH, 100, // LFO1: 1 Hz triangle → full morph sweep
        0, 5,  LFO_TARGET_PITCH, 20,       // LFO2: subtle pitch rise; EuclTun=0
        40, 1,
        RESONANT_MODE_LOWPASS, 30, 75, 30,
        {ENGINE_KICK, ENGINE_SNARE, ENGINE_METAL, ENGINE_RESONANT}
    },

    // Preset 19: "Shaker" - High density rattling: all four voices active,
    // low probabilities for irregular hitting, high metal brightness, fast
    // near-audio LFO on NOISE_MIX for textured shimmer (metal and snare).
    {
        "Shaker", 60, 50, 90, 70,
        20, 40, 80, 70,
        80, 90, 60, 70,
        0, 75, LFO_TARGET_NOISE_MIX, 80,  // LFO1: ~10 Hz triangle → noise shimmer
        0, 50, LFO_TARGET_PITCH, 15,       // LFO2: slow pitch arp; EuclTun=0
        8, 0,
        RESONANT_MODE_HIGHPASS, 80, 50, 70,
        {ENGINE_KICK, ENGINE_SNARE, ENGINE_METAL, ENGINE_PERC}
    },

    // ===== CHARACTER 1 (GONG) PRESETS — EnvShape = 128+index =====
    // EnvShape bit 7 = 1 selects gong ratios (1.0, 2.756, 3.752, 5.404)
    // which produce widely-spaced inharmonic partials vs. the DX7 cymbal cluster.

    // Preset 20: "GongHit" - Single gong strike with long resonant tail.
    // Metal engine only (all voices routed to metal), high inharmonicity for
    // rich gong spectrum, slow LFO pitch sag (gong pitch drops after strike).
    {
        "GongHit", 0, 0, 100, 0,
        0, 0, 0, 0,
        70, 60, 0, 0,
        1, 8,  LFO_TARGET_PITCH, -40,  // LFO1: slow ramp → pitch sag after hit
        0, 0,  LFO_TARGET_NONE, 0,
        128 + 110, 0,                  // EnvShape: char=1(Gong) + env=110 (long decay)
        RESONANT_MODE_PEAK, 0, 0, 0,
        {ENGINE_METAL, ENGINE_METAL, ENGINE_METAL, ENGINE_METAL}
    },

    // Preset 21: "TmplBell" - Temple bell: low inharmonicity (partials less
    // spread) gives a clearer pitch centre; LFO index sweep brightens the
    // attack transient then softens.  Long decay envelope.
    {
        "TmplBell", 0, 0, 100, 0,
        0, 0, 0, 0,
        20, 80, 0, 0,
        0, 12, LFO_TARGET_INDEX, 60,   // LFO1: slow ramp → brightness decay
        0, 0,  LFO_TARGET_NONE, 0,
        128 + 100, 0,                  // EnvShape: char=1(Gong) + env=100 (long)
        RESONANT_MODE_LOWPASS, 0, 0, 0,
        {ENGINE_METAL, ENGINE_METAL, ENGINE_METAL, ENGINE_METAL}
    },

    // Preset 22: "MetlGong" - Hybrid: kick gives low body, metal uses gong
    // character.  LFO NOISE_MIX on both snare and metal creates evolving
    // texture.  Medium decay, mid-range voices active.
    {
        "MetlGong", 60, 30, 80, 0,
        50, 40, 20, 30,
        60, 70, 0, 0,
        0, 40, LFO_TARGET_NOISE_MIX, 70, // LFO1: triangle → noise texture
        0, 6,  LFO_TARGET_PITCH, -20,     // LFO2: slow pitch drop; EuclTun=0
        128 + 60, 0,                      // EnvShape: char=1(Gong) + env=60 (medium)
        RESONANT_MODE_BANDPASS, 0, 0, 0,
        {ENGINE_KICK, ENGINE_SNARE, ENGINE_METAL, ENGINE_PERC}
    },

    // ===== EUCLIDEAN TUNING SHOWCASE PRESETS =====

    // Preset 23: "DimKit" — EuclTun=Dim7 (E(4,12)=[0,3,6,9]).
    // All 4 voices active with dim7 chord spread: Kick at root, Snare +3st,
    // Metal +6st, Perc +9st.  Every trigger fires a diminished 7th chord of
    // percussion.  Slow ramp LFO→Pitch adds a falling pitch tail to each hit.
    {
        "DimKit", 100, 90, 80, 100,
        60, 50, 25, 55,
        50, 65, 55, 35,
        1, 12, LFO_TARGET_PITCH, -30,  // LFO1: slow ramp → falling pitch tail
        6, 0,  LFO_TARGET_NONE, 0,     // EuclTun=6 (Dim7 [0,3,6,9])
        30, 0,
        RESONANT_MODE_LOWPASS, 30, 40, 20,
        {ENGINE_KICK, ENGINE_SNARE, ENGINE_METAL, ENGINE_PERC}
    },

    // Preset 24: "WholPrc" — EuclTun=Whole (E(4,8)=[0,2,4,6]).
    // Perc-forward whole-tone tuning: all 4 voices spread across a whole-tone
    // scale (0,2,4,6 semitones from the incoming note).  Low kick/snare so the
    // tuned perc and metal voices dominate.  Short env for staccato character.
    {
        "WholPrc", 40, 30, 70, 100,
        30, 40, 15, 35,
        35, 55, 70, 45,
        1, 18, LFO_TARGET_PITCH, -20,  // LFO1: ramp → slight pitch drop after hit
        4, 0,  LFO_TARGET_NONE, 0,     // EuclTun=4 (Whole [0,2,4,6])
        22, 0,
        RESONANT_MODE_LOWPASS, 20, 35, 15,
        {ENGINE_KICK, ENGINE_SNARE, ENGINE_METAL, ENGINE_PERC}
    },

    // Preset 25: "HiHatSw" — MetalGate open/closed hi-hat.
    // Metal engine routed to voice 2 (slot 2).  LFO1 uses Ramp shape targeting
    // METAL_GATE: phase resets on trigger → one-shot gate that closes from 1→0
    // over the LFO period.  Rate=30 (~3 Hz period=330ms) → medium-length open hat.
    // Increase rate for closed hat, decrease for longer open shimmer.
    {
        "HiHatSw", 70, 50, 100, 0,
        25, 45, 40, 45,
        55, 90, 0, 0,
        1, 30, LFO_TARGET_METAL_GATE, 90,  // LFO1: Ramp → open→closed gate
        0, 0,  LFO_TARGET_NONE, 0,         // EuclTun=0 (Off)
        10, 0,
        RESONANT_MODE_HIGHPASS, 20, 50, 40,
        {ENGINE_KICK, ENGINE_SNARE, ENGINE_METAL, ENGINE_PERC}
    }
};
