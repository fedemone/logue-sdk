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
    "ResMtl"
};

const fm_preset_t FM_PRESETS[NUM_OF_PRESETS] = {
    // Preset 0: "Deep Tribal" (original)
    {
        "DepTrbl", 100, 80, 60, 70,
        80, 60, 30, 70,
        40, 50, 50, 40,
        0, 20, LFO_TARGET_PITCH, 30,
        1, 5, LFO_TARGET_INDEX, 20,
        20, 0,
        RESONANT_MODE_BANDPASS, 2, 50, 30,
        {ENGINE_KICK, ENGINE_SNARE, ENGINE_METAL, ENGINE_PERC}
    },

    // Preset 1: "Metal Storm" (original)
    {
        "MtlStrm", 60, 80, 100, 50,
        40, 30, 60, 40,
        80, 90, 30, 60,
        6, 60, LFO_TARGET_PITCH, 20,
        4, 80, LFO_TARGET_LFO1_PHASE, 50,
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
        2, 15, LFO_TARGET_PITCH, -30,
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
        5, 30, LFO_TARGET_LFO1_PHASE, 70,
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
        1, 45, LFO_TARGET_PITCH, 20,
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
        8, 40, LFO_TARGET_PITCH, 20,
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
        7, 55, LFO_TARGET_PITCH, 30,
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
        1, 8, LFO_TARGET_INDEX, 20,
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
        2, 10, LFO_TARGET_INDEX, 30,
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
        6, 40, LFO_TARGET_INDEX, 40,
        10, 2,
        RESONANT_MODE_PEAK, 20, 80, 80,
        {ENGINE_KICK, ENGINE_SNARE, ENGINE_RESONANT, ENGINE_PERC}
    }
};
