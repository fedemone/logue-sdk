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
    // Zero on purpose: the Program parameter
    // is the preset control (stored with the pattern, automatable), and a
    // non-zero count would draw a second, competing preset UI.
    .num_presets = 0,
    .num_params = 19,
    .params = {

        // Format: min, max, center, default, type, frac_digits, frac_type, <reserved>, name

        // Page 1 — common: performance
        // Program: the preset (this family's presets only)
        {0, 7, 0, 0, k_unit_param_type_strings, 0, 0, 0, {"Program"}},
        {0, 0, 0, 0, k_unit_param_type_none, 0, 0, 0, {""}},   // blank: this unit has no Pitch (nothing in a noise voice follows the note)
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
        {0, 100, 0, 5, k_unit_param_type_none, 0, 0, 0, {"Gain"}},

        // Page 3 — dedicated
        // GrnRate (was MlltStif): the grain / burst rate, ±2 octaves.
        // Stored /100, displayed x100 (0-5000)
        {0, 50, 0, 27, k_unit_param_type_strings, 0, 0, 0, {"GrnRate"}},
        // GrnDpth (was MlltRes): the grains' contrast; above the shipped value it adds
        // grains to the presets that ship none
        {0, 1000, 500, 500, k_unit_param_type_none, 1, 1, 0, {"GrnDpth"}},
        // GrnFade (new, in HitPos' slot): how fast the grains melt into a
        // plain hiss: − slower (×1/8), + faster (it makes the shakers fade too)
        {-100, 100, 0, 0, k_unit_param_type_none, 0, 0, 0, {"GrnFade"}},
        // Swell (new, in VlMllStf's slot): the noise's rise, − snappier, + slower (×1/32 .. ×32)
        {-100, 100, 0, 0, k_unit_param_type_none, 0, 0, 0, {"Swell"}},

        // Page 4 — dedicated
        {0, 100, 50, 95, k_unit_param_type_percent, 0, 0, 0, {"NzMix"}},
        {0, 1000, 300, 950, k_unit_param_type_percent, 1, 1, 0, {"NzRes"}},
        {0, 2, 0, 1, k_unit_param_type_strings, 0, 0, 0, {"NzFltr"}},
        // NzFltFrq: stored /10 (20-20000 Hz)
        {2, 2000, 1200, 200, k_unit_param_type_strings, 0, 0, 0, {"NzFltFrq"}},

        // Page 5 — dedicated
        // Band (new, in Model's slot): the noise's balance, − the soft body
        // band, + the bright click band
        {-100, 100, 0, 0, k_unit_param_type_none, 0, 0, 0, {"Band"}},
        {0, 20, 0, 19, k_unit_param_type_none, 1, 0, 0, {"Rel"}},
        // VelTone (new, in VlMllRes' slot): velocity → brightness: + a harder
        // stroke opens the noise filter (a soft one closes it), − the reverse
        {-100, 100, 0, 0, k_unit_param_type_none, 0, 0, 0, {"VelTone"}}
    }
};
