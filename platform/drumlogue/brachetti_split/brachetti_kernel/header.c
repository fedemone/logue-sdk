/**
 * @file header.c
 * @brief drumlogue SDK unit header — Brachetti Kernel
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
    .unit_id = 0x42724B6EU,                                // 'BrKn' (Brachetti Kernel)
    .version = 0x00010000U,
    .name = "BrcKernel",
    // Zero on purpose: the Program parameter
    // is the preset control (stored with the pattern, automatable), and a
    // non-zero count would draw a second, competing preset UI.
    .num_presets = 0,
    .num_params = 22,
    .params = {

        // Format: min, max, center, default, type, frac_digits, frac_type, <reserved>, name

        // Page 1 — common: performance
        // Program: the preset (this family's presets only)
        {0, 1, 0, 0, k_unit_param_type_strings, 0, 0, 0, {"Program"}},
        // Pitch: semitones from the preset's root note (sequencer path)
        {-24, 24, 0, 0, k_unit_param_type_semi, 0, 0, 0, {"Pitch"}},
        {0, 0, 0, 0, k_unit_param_type_none, 0, 0, 0, {""}},   // blank: this unit has no Poly (the kernel always has its two kettles)
        // Velocity: velocity bias: -100 ghost .. 0 as played .. +100 wham
        {-100, 100, 0, 0, k_unit_param_type_none, 0, 0, 0, {"Velocity"}},

        // Page 2 — common: master
        {-10, 30, 0, 0, k_unit_param_type_none, 1, 0, 0, {"Tone"}},
        // Cutoff: master low-pass, stored /10 (10-19990 Hz), 1999 = open
        {1, 1999, 500, 1999, k_unit_param_type_strings, 0, 0, 0, {"Cutoff"}},
        // Resnc: master low-pass Q, stored /10 (displayed 710-4000)
        {71, 400, 0, 71, k_unit_param_type_strings, 0, 0, 0, {"Resnc"}},
        // Gain: master drive
        {0, 100, 0, 4, k_unit_param_type_none, 0, 0, 0, {"Gain"}},

        // Page 3 — dedicated
        // Knock (was MlltRes): the knock ("wham") level
        {0, 1000, 500, 360, k_unit_param_type_none, 1, 1, 0, {"Knock"}},
        // MlltStif: stored /100, displayed x100 (0-5000)
        {0, 50, 0, 30, k_unit_param_type_strings, 0, 0, 0, {"MlltStif"}},
        {-100, 100, 0, 0, k_unit_param_type_none, 0, 0, 0, {"VlMllRes"}},
        {-100, 100, 0, 40, k_unit_param_type_none, 0, 0, 0, {"VlMllStf"}},

        // Page 4 — dedicated
        // Fill (was Partls): density of the membrane fill; shows the share kept
        {0, 4, 0, 2, k_unit_param_type_strings, 0, 0, 0, {"Fill"}},
        // Dkay: stored /10, displayed x10 (0-2000)
        {0, 200, 25, 150, k_unit_param_type_strings, 0, 0, 0, {"Dkay"}},
        {0, 20, 0, 18, k_unit_param_type_none, 1, 0, 0, {"Rel"}},
        {-10, 30, 0, 10, k_unit_param_type_none, 1, 1, 0, {"Mterl"}},

        // Page 5 — dedicated
        // HitPos: bipolar
        {-98, 98, 0, 36, k_unit_param_type_none, 2, 0, 0, {"HitPos"}},
        // Inharm: bipolar
        {-100, 100, 0, 1, k_unit_param_type_none, 0, 0, 0, {"Inharm"}},
        {0, 20, 0, 0, k_unit_param_type_none, 1, 0, 0, {"TubRad"}},
        // Punch: the transient shaper.  + the attack passes and the body drops
        // after it (up to −15 dB), − a softer, rounder attack.  0 = off
        {-100, 100, 0, 0, k_unit_param_type_none, 0, 0, 0, {"Punch"}},

        // Page 6 — dedicated
        {0, 100, 50, 1, k_unit_param_type_percent, 0, 0, 0, {"NzMix"}},
        // NzFltFrq: stored /10 (20-20000 Hz)
        {2, 2000, 1200, 380, k_unit_param_type_strings, 0, 0, 0, {"NzFltFrq"}}
    }
};
