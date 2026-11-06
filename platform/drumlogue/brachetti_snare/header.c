/**
 * @file header.c
 * @brief drumlogue SDK unit header — Brachetti Snare
 *
 * Copyright (c) 2020-2022 KORG Inc. All rights reserved.
 *
 */

#include "unit.h"

const __unit_header unit_header_t unit_header = {
    .header_size = sizeof(unit_header_t),
    .target = UNIT_TARGET_PLATFORM | k_unit_module_synth,
    .api = UNIT_API_VERSION,
    .dev_id = 0x46654465U,                                 // 'FeDe'
    .unit_id = 0x4272536EU,                                // 'BrSn' (Brachetti Snare)
    .version = 0x00010000U,
    .name = "BrcSnare",
    // Zero on purpose, as in the original brachetti: the Program parameter
    // is the preset control (stored with the pattern, automatable), and a
    // non-zero count would draw a second, competing preset UI.
    .num_presets = 0,
    .num_params = 24,
    .params = {
        // Format: min, max, center, default, type, frac_digits, frac_type, <reserved>, name
        //
        // Defaults are preset 0's (AcSnre) shipped values, so the unit boots
        // sounding like the preset it shows.  The knobs the host owns keep
        // their own — Pitch, Poly, Velocity, VlMllRes, Tone, Cutoff, Resnc —
        // because LoadPreset never writes them (pass 48);
        // seed_performance_params() in synth_engine.h holds the same values.
        // Ranges, steps and display are the original brachetti's; see its
        // header.c for the history behind each one.

        // Page 1
        // Program: the preset (this family's presets only)
        {0, 3, 0, 0, k_unit_param_type_strings, 0, 0, 0, {"Program"}},
        // Pitch: semitones from the preset's root note (sequencer path)
        {-24, 24, 0, 0, k_unit_param_type_semi, 0, 0, 0, {"Pitch"}},
        // Poly: global voice cap 1-4
        {1, 4, 1, 4, k_unit_param_type_strings, 0, 0, 0, {"Poly"}},
        // Velocity: velocity bias: -100 ghost .. 0 as played .. +100 wham
        {-100, 100, 0, 0, k_unit_param_type_none, 0, 0, 0, {"Velocity"}},

        // Page 2
        {0, 1000, 500, 120, k_unit_param_type_none, 1, 1, 0, {"MlltRes"}},
        // MlltStif: stored /100, displayed x100 (0-5000)
        {0, 50, 0, 28, k_unit_param_type_strings, 0, 0, 0, {"MlltStif"}},
        {-100, 100, 0, 0, k_unit_param_type_none, 0, 0, 0, {"VlMllRes"}},
        {-100, 100, 0, 0, k_unit_param_type_none, 0, 0, 0, {"VlMllStf"}},

        // Page 3
        // Partls: mode-count offset around the preset's own count; shows the count
        {0, 4, 0, 2, k_unit_param_type_strings, 0, 0, 0, {"Partls"}},
        // Model: physical-model template
        {0, 8, 0, 5, k_unit_param_type_strings, 0, 0, 0, {"Model"}},
        // Dkay: stored /10, displayed x10 (0-2000)
        {0, 200, 25, 168, k_unit_param_type_strings, 0, 0, 0, {"Dkay"}},
        {-10, 30, 0, -7, k_unit_param_type_none, 1, 1, 0, {"Mterl"}},

        // Page 4
        {-10, 30, 0, 0, k_unit_param_type_none, 1, 0, 0, {"Tone"}},
        // HitPos: bipolar since pass 39
        {-98, 98, 0, 46, k_unit_param_type_none, 2, 0, 0, {"HitPos"}},
        {0, 20, 0, 9, k_unit_param_type_none, 1, 0, 0, {"Rel"}},
        // Inharm: bipolar since pass 36
        {-100, 100, 0, 0, k_unit_param_type_none, 0, 0, 0, {"Inharm"}},

        // Page 5
        // Cutoff: master low-pass, stored /10 (10-19990 Hz), 1999 = open
        {1, 1999, 500, 1999, k_unit_param_type_strings, 0, 0, 0, {"Cutoff"}},
        {0, 20, 0, 8, k_unit_param_type_none, 1, 0, 0, {"TubRad"}},
        // Gain: master drive
        {0, 100, 0, 7, k_unit_param_type_none, 0, 0, 0, {"Gain"}},
        {0, 100, 50, 52, k_unit_param_type_percent, 0, 0, 0, {"NzMix"}},

        // Page 6
        {0, 1000, 300, 950, k_unit_param_type_percent, 1, 1, 0, {"NzRes"}},
        {0, 2, 0, 2, k_unit_param_type_strings, 0, 0, 0, {"NzFltr"}},
        // NzFltFrq: stored /10 (20-20000 Hz)
        {2, 2000, 1200, 300, k_unit_param_type_strings, 0, 0, 0, {"NzFltFrq"}},
        // Resnc: master low-pass Q, stored /10 (displayed 710-4000)
        {71, 400, 0, 71, k_unit_param_type_strings, 0, 0, 0, {"Resnc"}}
    }
};
