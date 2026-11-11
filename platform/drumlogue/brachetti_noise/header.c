/**
 * @file header.c
 * @brief drumlogue SDK unit header — Brachetti Noise
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
    .unit_id = 0x42724E7AU,                                // 'BrNz' (Brachetti Noise)
    .version = 0x00010000U,
    .name = "BrcNoise",
    // The presets, exposed to the drumlogue's preset menu as well as through
    // the Program parameter.  Only the preset path puts a preset's knob values
    // on the panel: the host owns the values the panel draws and re-reads
    // them after unit_load_preset(), never because another parameter (such
    // as Program) moved them.  Program stays for what only a parameter has:
    // it is stored with the pattern and can be sequenced and motion-recorded.
    // Both call the same LoadPreset(), and unit_get_preset_index() reports
    // the preset loaded either way.  MUST equal the preset count (presets.h),
    // and Program's max below must be one less; equiv_driver checks both.
    .num_presets = 3,
    .num_params = 18,
    .params = {
        // Format: min, max, center, default, type, frac_digits, frac_type, <reserved>, name
        //
        // Defaults are preset 0's (Clap) shipped values, so the unit boots
        // sounding like the preset it shows.  The knobs the host owns keep
        // their own — Poly, Velocity, VlMllRes, Tone, Cutoff, Resnc —
        // because LoadPreset never writes them;
        // `kHostKnobs` in presets.h holds the same values.

        // Page 1
        // Program: the preset (this family's presets only)
        {0, 2, 0, 0, k_unit_param_type_strings, 0, 0, 0, {"Program"}},
        // Poly: global voice cap 1-4
        {1, 4, 1, 4, k_unit_param_type_strings, 0, 0, 0, {"Poly"}},
        // Velocity: velocity bias: -100 ghost .. 0 as played .. +100 wham
        {-100, 100, 0, 0, k_unit_param_type_none, 0, 0, 0, {"Velocity"}},
        {0, 1000, 500, 500, k_unit_param_type_none, 1, 1, 0, {"MlltRes"}},

        // Page 2
        // MlltStif: stored /100, displayed x100 (0-5000)
        {0, 50, 0, 27, k_unit_param_type_strings, 0, 0, 0, {"MlltStif"}},
        {-100, 100, 0, 0, k_unit_param_type_none, 0, 0, 0, {"VlMllRes"}},
        {-100, 100, 0, 0, k_unit_param_type_none, 0, 0, 0, {"VlMllStf"}},
        // Model: physical-model template
        {0, 8, 0, 5, k_unit_param_type_strings, 0, 0, 0, {"Model"}},

        // Page 3
        {-10, 30, 0, 0, k_unit_param_type_none, 1, 0, 0, {"Tone"}},
        // HitPos: bipolar
        {-98, 98, 0, 50, k_unit_param_type_none, 2, 0, 0, {"HitPos"}},
        {0, 20, 0, 19, k_unit_param_type_none, 1, 0, 0, {"Rel"}},
        // Cutoff: master low-pass, stored /10 (10-19990 Hz), 1999 = open
        {1, 1999, 500, 1999, k_unit_param_type_strings, 0, 0, 0, {"Cutoff"}},

        // Page 4
        // Gain: master drive
        {0, 100, 0, 5, k_unit_param_type_none, 0, 0, 0, {"Gain"}},
        {0, 100, 50, 95, k_unit_param_type_percent, 0, 0, 0, {"NzMix"}},
        {0, 1000, 300, 950, k_unit_param_type_percent, 1, 1, 0, {"NzRes"}},
        {0, 2, 0, 1, k_unit_param_type_strings, 0, 0, 0, {"NzFltr"}},

        // Page 5
        // NzFltFrq: stored /10 (20-20000 Hz)
        {2, 2000, 1200, 200, k_unit_param_type_strings, 0, 0, 0, {"NzFltFrq"}},
        // Resnc: master low-pass Q, stored /10 (displayed 710-4000)
        {71, 400, 0, 71, k_unit_param_type_strings, 0, 0, 0, {"Resnc"}}
    }
};
