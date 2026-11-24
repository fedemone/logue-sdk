/**
 * @file header.c
 * @brief drumlogue SDK unit header — Braquetti
 *
 * Copyright (c) 2020-2022 KORG Inc. All rights reserved.
 *
 */

#include "unit.h"

// The order must match seq_core.h's P_* indices.  Steps, Family and Level
// are read as turns (seq_core.h): their range is only room to turn in, and
// what the panel shows is the lane's content.
#define BRQ_STEP(n) {0, 4095, 0, 2048, k_unit_param_type_strings, 0, 0, 0, {"Step " #n}}
#define BRQ_BLANK   {0, 0, 0, 0, k_unit_param_type_none, 0, 0, 0, {""}}

const __unit_header unit_header_t unit_header = {
    .header_size = sizeof(unit_header_t),
    .target = UNIT_TARGET_PLATFORM | k_unit_module_synth,
    .api = UNIT_API_VERSION,
    .dev_id = 0x46654465U,                                 // 'FeDe'
    .unit_id = 0x42725174U,                                // 'BrQt' (Braquetti)
    .version = 0x00010000U,
    .name = "Braquetti",
    .num_presets = 0,
    .num_params = 24,
    .params = {
        // Format: min, max, center, default, type, frac_digits, frac_type, <reserved>, name

        // Pages 1-4: the 16 steps of the selected lane, a beat per page
        BRQ_STEP(1),  BRQ_STEP(2),  BRQ_STEP(3),  BRQ_STEP(4),
        BRQ_STEP(5),  BRQ_STEP(6),  BRQ_STEP(7),  BRQ_STEP(8),
        BRQ_STEP(9),  BRQ_STEP(10), BRQ_STEP(11), BRQ_STEP(12),
        BRQ_STEP(13), BRQ_STEP(14), BRQ_STEP(15), BRQ_STEP(16),

        // Page 5: the selected lane, its family, its level
        {0, 2, 0, 0, k_unit_param_type_strings, 0, 0, 0, {"Lane"}},
        {0, 4095, 0, 2048, k_unit_param_type_strings, 0, 0, 0, {"Family"}},
        {0, 4095, 0, 2048, k_unit_param_type_strings, 0, 0, 0, {"Level"}},
        BRQ_BLANK,

        // Page 6: the three lanes at a glance, four steps per knob
        {0, 1, 0, 0, k_unit_param_type_bitmaps, 0, 0, 0, {"1-4"}},
        {0, 1, 0, 0, k_unit_param_type_bitmaps, 0, 0, 0, {"5-8"}},
        {0, 1, 0, 0, k_unit_param_type_bitmaps, 0, 0, 0, {"9-12"}},
        {0, 1, 0, 0, k_unit_param_type_bitmaps, 0, 0, 0, {"13-16"}}
    }
};
