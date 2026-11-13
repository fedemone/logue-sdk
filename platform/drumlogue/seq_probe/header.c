/**
 * @file header.c
 * @brief drumlogue SDK unit header — SeqProbe (a test unit, makes no music)
 *
 * Copyright (c) 2020-2022 KORG Inc. All rights reserved.
 *
 */

#include "unit.h"

// The parameter order must match the P_* indices in unit.cc.
const __unit_header unit_header_t unit_header = {
    .header_size = sizeof(unit_header_t),
    .target = UNIT_TARGET_PLATFORM | k_unit_module_synth,
    .api = UNIT_API_VERSION,
    .dev_id = 0x46654465U,                                 // 'FeDe'
    .unit_id = 0x53715062U,                                // 'SqPb' (SeqProbe)
    .version = 0x00010000U,
    .name = "SeqProbe",
    // Two presets, to see what the drumlogue re-reads after one is loaded.
    .num_presets = 2,
    .num_params = 24,
    .params = {
        // Format: min, max, center, default, type, frac_digits, frac_type, <reserved>, name

        // Page 1 — does the panel redraw on its own?
        {0, 15, 0, 0, k_unit_param_type_bitmaps, 0, 0, 0, {"Blink"}},
        {0, 15, 0, 0, k_unit_param_type_bitmaps, 0, 0, 0, {"Follow"}},
        {0, 15, 0, 0, k_unit_param_type_strings, 0, 0, 0, {"Clock"}},
        {0, 15, 0, 0, k_unit_param_type_strings, 0, 0, 0, {"FollowS"}},

        // Page 2 — whose number is it, and what does a program load send?
        // Owned/OwnedS start at 50; the unit answers 70 (Shadow x 10) when asked.
        {0, 100, 0, 50, k_unit_param_type_none, 0, 0, 0, {"Owned"}},
        {0, 100, 0, 50, k_unit_param_type_strings, 0, 0, 0, {"OwnedS"}},
        {0, 10, 0, 7, k_unit_param_type_none, 0, 0, 0, {"Shadow"}},
        {0, 15, 0, 0, k_unit_param_type_strings, 0, 0, 0, {"Burst"}},

        // Page 3 — what the sequencer sends, and the files
        {0, 15, 0, 0, k_unit_param_type_bitmaps, 0, 0, 0, {"Grid"}},
        {0, 15, 0, 0, k_unit_param_type_strings, 0, 0, 0, {"Gate"}},
        {0, 15, 0, 0, k_unit_param_type_strings, 0, 0, 0, {"Event"}},
        {0, 1, 0, 1, k_unit_param_type_strings, 0, 0, 0, {"Files"}},

        // Page 4 — prototype: steps 1-4 of the lane chosen on page 5.  Read
        // as relative turns, so the stored number does not matter.
        {0, 1000, 0, 500, k_unit_param_type_strings, 0, 0, 0, {"Step 1"}},
        {0, 1000, 0, 500, k_unit_param_type_strings, 0, 0, 0, {"Step 2"}},
        {0, 1000, 0, 500, k_unit_param_type_strings, 0, 0, 0, {"Step 3"}},
        {0, 1000, 0, 500, k_unit_param_type_strings, 0, 0, 0, {"Step 4"}},

        // Page 5 — the lane page 4 shows
        {0, 2, 0, 0, k_unit_param_type_strings, 0, 0, 0, {"Lane"}},
        {0, 0, 0, 0, k_unit_param_type_none, 0, 0, 0, {""}},
        {0, 0, 0, 0, k_unit_param_type_none, 0, 0, 0, {""}},
        {0, 0, 0, 0, k_unit_param_type_none, 0, 0, 0, {""}},

        // Page 6 — unused
        {0, 0, 0, 0, k_unit_param_type_none, 0, 0, 0, {""}},
        {0, 0, 0, 0, k_unit_param_type_none, 0, 0, 0, {""}},
        {0, 0, 0, 0, k_unit_param_type_none, 0, 0, 0, {""}},
        {0, 0, 0, 0, k_unit_param_type_none, 0, 0, 0, {""}}
    }
};
