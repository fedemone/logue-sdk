/**
 * @file header.c
 * @brief drumlogue SDK unit header — Brachetti Bar
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
    .unit_id = 0x42724272U,                                // 'BrBr' (Brachetti Bar)
    .version = 0x00010000U,
    .name = "BrcBar",
    // Zero on purpose: the Program parameter
    // is the preset control (stored with the pattern, automatable), and a
    // non-zero count would draw a second, competing preset UI.
    .num_presets = 0,
    .num_params = 24,
    .params = {

        // Format: min, max, center, default, type, frac_digits, frac_type, <reserved>, name

        // Page 1 — common: performance
        // Program: the preset (this family's presets only)
        {0, 18, 0, 0, k_unit_param_type_strings, 0, 0, 0, {"Program"}},
        // Pitch: semitones from the preset's root note (sequencer path)
        {-24, 24, 0, 0, k_unit_param_type_semi, 0, 0, 0, {"Pitch"}},
        // Poly: global voice cap 1-4
        {1, 4, 1, 4, k_unit_param_type_strings, 0, 0, 0, {"Poly"}},
        // Velocity: velocity bias: -100 ghost .. 0 as played .. +100 wham
        {-100, 100, 0, 0, k_unit_param_type_none, 0, 0, 0, {"Velocity"}},

        // Page 2 — common: master
        {-10, 30, 0, 0, k_unit_param_type_none, 1, 0, 0, {"Tone"}},
        // Cutoff: master low-pass, stored /10 (10-19990 Hz), 1999 = open
        {1, 1999, 500, 1999, k_unit_param_type_strings, 0, 0, 0, {"Cutoff"}},
        // Resnc: master low-pass Q, stored /10 (displayed 710-4000)
        {71, 400, 0, 71, k_unit_param_type_strings, 0, 0, 0, {"Resnc"}},
        // Gain: master drive
        {0, 100, 0, 20, k_unit_param_type_none, 0, 0, 0, {"Gain"}},

        // Page 3 — dedicated
        {0, 1000, 500, 800, k_unit_param_type_none, 1, 1, 0, {"MlltRes"}},
        // MlltStif: stored /100, displayed x100 (0-5000)
        {0, 50, 0, 13, k_unit_param_type_strings, 0, 0, 0, {"MlltStif"}},
        // Punch (a bar knob, in VlMllRes' slot): the transient.  + the hit kept
        // and the ring dropped after it, a sharper onset; − a slower swell (up
        // to ~60 ms more: bowed)
        {-100, 100, 0, 0, k_unit_param_type_none, 0, 0, 0, {"Punch"}},
        {-100, 100, 0, 0, k_unit_param_type_none, 0, 0, 0, {"VlMllStf"}},

        // Page 4 — dedicated
        // Partls: mode-count offset around the preset's own count; shows the count
        {0, 4, 0, 0, k_unit_param_type_strings, 0, 0, 0, {"Partls"}},
        // Model: physical-model template
        {0, 8, 0, 6, k_unit_param_type_strings, 0, 0, 0, {"Model"}},
        // Dkay: stored /10, displayed x10 (0-2000)
        {0, 200, 25, 194, k_unit_param_type_strings, 0, 0, 0, {"Dkay"}},
        {0, 20, 0, 5, k_unit_param_type_none, 1, 0, 0, {"Rel"}},

        // Page 5 — dedicated
        {-10, 30, 0, -7, k_unit_param_type_none, 1, 1, 0, {"Mterl"}},
        // HitPos: bipolar
        {-98, 98, 0, 0, k_unit_param_type_none, 2, 0, 0, {"HitPos"}},
        // Inharm: bipolar
        {-100, 100, 0, 2, k_unit_param_type_none, 0, 0, 0, {"Inharm"}},
        {0, 20, 0, 7, k_unit_param_type_none, 1, 0, 0, {"TubRad"}},

        // Page 6 — dedicated
        {0, 100, 50, 0, k_unit_param_type_percent, 0, 0, 0, {"NzMix"}},
        {0, 1000, 300, 300, k_unit_param_type_percent, 1, 1, 0, {"NzRes"}},
        {0, 2, 0, 0, k_unit_param_type_strings, 0, 0, 0, {"NzFltr"}},
        // NzFltFrq: stored /10 (20-20000 Hz)
        {2, 2000, 1200, 1200, k_unit_param_type_strings, 0, 0, 0, {"NzFltFrq"}}
    }
};
