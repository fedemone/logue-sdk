/**
 * @file header.c
 * @brief drumlogue SDK unit header — Brachetti (all twelve families)
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
    .unit_id = 0x4272416CU,                                // 'BrAl' (Brachetti, all families)
    .version = 0x00010000U,
    .name = "Brachetti",
    // The presets are the twelve families (brachetti_synth.h, kFamilies):
    // Kick Tom Kernel Snare Bar Plate Cymbal Noise World Lippa Pizzico
    // Cordiera.  Loading one switches the engine and puts that family's own
    // values on the panel; Program then picks the instrument in it.
    .num_presets = 12,
    .num_params = 24,
    .params = {

        // Format: min, max, center, default, type, frac_digits, frac_type, <reserved>, name
        //
        // Pages 1-2 are the common knobs of every family unit, with their
        // ranges (Cutoff steps 0.1 kHz here).  On pages 3-6 a slot where
        // every family's knob has the same range and type has that type, so
        // the drumlogue shows and steps it as on the family unit; the other
        // slots are "strings" knobs, formatted by the family's own knob
        // (README.md has the table).  The drumlogue passes a strings knob's
        // value unsigned (16 bits), so no strings slot goes negative: the
        // mixed slots run 0-200.  A slot is at least as wide as every family
        // knob placed in it.  The defaults are what the first preset (Kick,
        // on Kick2) loads.

        // Page 1 — common: performance
        // Program: the instrument, within the selected family (Tom has 32,
        // the most; beyond a family's last, its last)
        {0, 31, 0, 0, k_unit_param_type_strings, 0, 0, 0, {"Program"}},
        // Pitch: semitones from the instrument's root note (no Pitch on Noise)
        {-24, 24, 0, 0, k_unit_param_type_semi, 0, 0, 0, {"Pitch"}},
        // Poly: voice cap 1-4 (no Poly on Kernel)
        {1, 4, 1, 4, k_unit_param_type_strings, 0, 0, 0, {"Poly"}},
        // Velocity: -100 ghost .. 0 as played .. +100 wham
        {-100, 100, 0, 0, k_unit_param_type_none, 0, 0, 0, {"Velocity"}},

        // Page 2 — common: master
        {-10, 30, 0, 0, k_unit_param_type_none, 1, 0, 0, {"Tone"}},
        // Cutoff: master low-pass in 0.1 kHz steps (the family knob, stored /10,
        // is slot x 10 - 1), 200 = open
        {1, 200, 50, 200, k_unit_param_type_strings, 0, 0, 0, {"Cutoff"}},
        // Resnc: master low-pass Q, stored /10 (displayed 710-4000)
        {71, 400, 0, 71, k_unit_param_type_strings, 0, 0, 0, {"Resnc"}},
        // Gain: master drive (loads with the instrument)
        {0, 100, 0, 4, k_unit_param_type_none, 0, 0, 0, {"Gain"}},

        // Page 3 — dedicated: the strike
        {0, 1000, 500, 360, k_unit_param_type_none, 1, 1, 0, {"MlltRes"}},
        {0, 50, 0, 30, k_unit_param_type_strings, 0, 0, 0, {"MlltStif"}},
        {-100, 100, 0, 0, k_unit_param_type_none, 0, 0, 0, {"VlMllRes"}},
        {-100, 100, 0, 40, k_unit_param_type_none, 0, 0, 0, {"VlMllStf"}},

        // Page 4 — dedicated: the body and its length
        {0, 200, 100, 100, k_unit_param_type_strings, 0, 0, 0, {"Partls"}},
        {0, 200, 100, 100, k_unit_param_type_strings, 0, 0, 0, {"Model"}},
        {0, 200, 25, 200, k_unit_param_type_strings, 0, 0, 0, {"Dkay"}},
        {0, 200, 100, 180, k_unit_param_type_strings, 0, 0, 0, {"Rel"}},

        // Page 5 — dedicated: the body
        {-10, 30, 0, 10, k_unit_param_type_none, 1, 1, 0, {"Mterl"}},
        {-98, 98, 0, 36, k_unit_param_type_none, 2, 0, 0, {"HitPos"}},
        {-100, 100, 0, 1, k_unit_param_type_none, 0, 0, 0, {"Inharm"}},
        {0, 20, 0, 0, k_unit_param_type_none, 1, 0, 0, {"TubRad"}},

        // Page 6 — dedicated: the noise burst
        {0, 100, 50, 1, k_unit_param_type_percent, 0, 0, 0, {"NzMix"}},
        {0, 1000, 300, 420, k_unit_param_type_strings, 0, 0, 0, {"NzRes"}},
        {0, 200, 100, 0, k_unit_param_type_strings, 0, 0, 0, {"NzFltr"}},
        {1, 200, 120, 38, k_unit_param_type_strings, 0, 0, 0, {"NzFltFrq"}}
    }
};
