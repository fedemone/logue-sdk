/**
 * @file fm_presets.cc
 * @brief Factory preset data for FM Percussion Synth
 */

#include "fm_presets.h"

// ---------------------------------------------------------
// Preset Name Dictionary
// ---------------------------------------------------------
const char preset_names[NUM_OF_PRESETS][NAME_LENGTH] =
{
    // Iconic Drum Machines
    "TR-808 Kit",
    "TR-909 Kit",
    "DX7 FM Kit",
    "CR-78 Kit",

    // Modern / Genre
    "Techno 1",
    "Indstrl 1",
    "AmbientKit",
    "Synthwave",

    // Engine Showcases
    "Deep Kick",
    "Rattle Snr",
    "Crash Ride",
    "Gong Hit",
    "FM Tom",
    "Conga Roll",
    "Noise Swp",
    "Laser Zap",

    // Resonant Showcases
    "Reso Kick",
    "Reso Tom",
    "Reso Snare",
    "Reso Metal",

    // Euclidean / Advanced
    "Euclid Tom",
    "WholTone",
    "Diminish",
    "MetalGate",
    "Polyrhythm",
    "RandomKit"
};

// ---------------------------------------------------------
// Preset Data Array
// ---------------------------------------------------------
// Struct Layout reminder:
// Page 1: kick_sweep, kick_decay, master_decay, mix_level
// Page 2: snare_noise, snare_body
// Page 3: metal_inharm, metal_bright, perc_ratio, perc_var
// Page 4: lfo1_shape, lfo1_rate, lfo1_target, lfo1_depth
// Page 5: lfo2_shape (Eucl), lfo2_rate, lfo2_target, lfo2_depth
// Page 6: env_shape (bit7=Gong), res_mode, res_cutoff, res_res, res_drive, voice_engines[4]

const fm_preset_t FM_PRESETS[NUM_OF_PRESETS] = {
    // =====================================================================
    // 1. ICONIC DRUM MACHINES
    // =====================================================================

    // Preset 0: "TR-808 Kit"
    // Deep, booming kick with a slow decay. Snare has high noise sizzle.
    // Metal is a pure cymbal. Perc is a low-ratio tom.
    {
        "TR-808 Kit",
        60, 90, 80, 80,          // Kick: Deep sweep, long decay
        85, 30,                  // Snare: High noise (rattle), low body
        40, 60, 20, 10,          // Metal: Clean cymbal. Perc: Low ratio tom (1.2)
        0, 0, LFO_TARGET_NONE, 0,
        0, 0, LFO_TARGET_NONE, 0,
        0, 0, 100, 0, 0,         // env_shape 0 = Cymbal character
        {ENGINE_KICK, ENGINE_SNARE, ENGINE_METAL, ENGINE_PERC}
    },

    // Preset 1: "TR-909 Kit"
    // Punchy, tight kick. Snare has aggressive body and moderate noise.
    // Metal is slightly inharmonic (crash). Perc is a tighter, higher tom.
    {
        "TR-909 Kit",
        90, 40, 50, 85,          // Kick: Aggressive sweep, short punchy decay
        40, 80,                  // Snare: Lower noise, punchy body
        60, 80, 35, 20,          // Metal: Inharmonic crash. Perc: Mid tom
        0, 0, LFO_TARGET_NONE, 0,
        0, 0, LFO_TARGET_NONE, 0,
        0, 0, 100, 0, 0,         // env_shape 0 = Cymbal character
        {ENGINE_KICK, ENGINE_SNARE, ENGINE_METAL, ENGINE_PERC}
    },

    // Preset 2: "DX7 FM Kit"
    // Pure frequency modulation. Kick has no sweep, just pure sine sub.
    // Metal is set to GONG character (EnvShape bit 7). Perc is bell-like.
    {
        "DX7 FM Kit",
        0, 70, 70, 80,           // Kick: No sweep (pure FM sub)
        10, 95,                  // Snare: Almost no noise, pure FM body
        80, 90, 70, 80,          // Metal: Highly inharmonic. Perc: Bell ratio
        0, 0, LFO_TARGET_NONE, 0,
        0, 0, LFO_TARGET_NONE, 0,
        128, 0, 100, 0, 0,       // env_shape 128 = GONG character!
        {ENGINE_KICK, ENGINE_SNARE, ENGINE_METAL, ENGINE_PERC}
    },

    // Preset 3: "CR-78 Kit"
    // Very short, blippy drum machine. High noise, high pitch.
    {
        "CR-78 Kit",
        30, 20, 20, 70,          // Kick: Tiny blip
        90, 10,                  // Snare: Mostly noise burst
        20, 40, 80, 0,           // Metal: Simple closed hat. Perc: Woodblock (high ratio, no var)
        0, 0, LFO_TARGET_NONE, 0,
        0, 0, LFO_TARGET_NONE, 0,
        0, 0, 100, 0, 0,
        {ENGINE_KICK, ENGINE_SNARE, ENGINE_METAL, ENGINE_PERC}
    },

    // =====================================================================
    // 2. MODERN & GENRE
    // =====================================================================

    // Preset 4: "Techno 1"
    // Huge distorted kick, rattling snare.
    {
        "Techno 1",
        100, 60, 60, 90,         // Kick: Massive transient
        60, 60,                  // Snare: Balanced
        80, 100, 15, 30,         // Metal: Bright. Perc: Low rumble tom
        1, 10, LFO_TARGET_PITCH, -10, // LFO: Slight pitch drop
        0, 0, LFO_TARGET_NONE, 0,
        0, 0, 100, 0, 0,
        {ENGINE_KICK, ENGINE_SNARE, ENGINE_METAL, ENGINE_PERC}
    },

    // Preset 5: "Indstrl 1"
    // Industrial. Heavy modulation, gong metal, high variation percs.
    {
        "Indstrl 1",
        80, 50, 40, 95,          // Kick: Hard punch
        100, 80,                 // Snare: White noise blast
        100, 100, 50, 100,       // Metal: Chaotic bright gong. Perc: High variance fm
        5, 80, LFO_TARGET_INDEX, 50, // LFO: Fast S&H on FM Index!
        0, 0, LFO_TARGET_NONE, 0,
        128, 0, 100, 0, 0,       // env_shape 128 = GONG character
        {ENGINE_KICK, ENGINE_SNARE, ENGINE_METAL, ENGINE_PERC}
    },

    // ... [Other presets follow the exact same struct initialization format] ...

    // =====================================================================
    // 3. EUCLIDEAN & ADVANCED SHOWCASES
    // =====================================================================

    // Preset 24: "WholTone"
    // Demonstrating the EuclTun (Euclidean Tuning) feature on the Perc engine.
    // LFO2 shape is used as the tuning mode (e.g. 4 = Whole Tone scale spread).
    {
        "WholTone",
        40, 30, 70, 80,
        30, 40,
        35, 55, 70, 45,          // Perc is highly tuned
        1, 18, LFO_TARGET_PITCH, -10,
        4, 0,  LFO_TARGET_NONE, 0, // EuclTun = 4 (Whole Tone spread across 4 voices)
        0, 0, 100, 0, 0,
        {ENGINE_KICK, ENGINE_SNARE, ENGINE_METAL, ENGINE_PERC}
    },

    // Preset 25: "MetalGate"
    // Demonstrating the MetalGate LFO target (chops the cymbal tail).
    {
        "MetalGate",
        60, 50, 60, 80,
        50, 50,
        50, 80, 30, 20,          // Metal is bright
        3, 60, LFO_TARGET_METAL_GATE, 100, // LFO1: Square wave gating the Metal engine!
        0, 0, LFO_TARGET_NONE, 0,
        0, 0, 100, 0, 0,
        {ENGINE_KICK, ENGINE_SNARE, ENGINE_METAL, ENGINE_PERC}
    }
};