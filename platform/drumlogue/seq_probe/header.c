/**
 * @file header.c
 * @brief drumlogue SDK unit header — SeqProbe (a test unit, makes no music)
 *
 * Copyright (c) 2020-2022 KORG Inc. All rights reserved.
 *
 */

#include "unit.h"

#ifdef PROBE_LITE
// SeqProbeLite (../seq_probe_lite): no bitmap knobs, no file access.  Its
// own id, so it installs next to SeqProbe.
#define PROBE_UNIT_ID   0x5371506CU                    // 'SqPl'
#define PROBE_NAME      "SeqProbeLite"
#define PROBE_BITMAPS   k_unit_param_type_strings
#else
#define PROBE_UNIT_ID   0x53715062U                    // 'SqPb'
#define PROBE_NAME      "SeqProbe"
#define PROBE_BITMAPS   k_unit_param_type_bitmaps
#endif

// The parameter order must match the P_* indices in unit.cc.
const __unit_header unit_header_t unit_header = {
    .header_size = sizeof(unit_header_t),
    .target = UNIT_TARGET_PLATFORM | k_unit_module_synth,
    .api = UNIT_API_VERSION,
    .dev_id = 0x46654465U,                                 // 'FeDe'
    .unit_id = PROBE_UNIT_ID,
    .version = 0x00010000U,
    .name = PROBE_NAME,
    // Two presets, to see what the drumlogue re-reads after one is loaded.
    .num_presets = 2,
    .num_params = 24,
    .params = {
        // Format: min, max, center, default, type, frac_digits, frac_type, <reserved>, name

        // Page 1 — does the panel redraw on its own?
        {0, 15, 0, 0, PROBE_BITMAPS, 0, 0, 0, {"Blink"}},
        {0, 15, 0, 0, PROBE_BITMAPS, 0, 0, 0, {"Follow"}},
        {0, 15, 0, 0, k_unit_param_type_strings, 0, 0, 0, {"Clock"}},
        {0, 15, 0, 0, k_unit_param_type_strings, 0, 0, 0, {"FollowS"}},

        // Page 2 — whose number is it, and what does a program load send?
        // Owned/OwnedS start at 50; the unit answers 70 (Shadow x 10) when asked.
        {0, 100, 0, 50, k_unit_param_type_none, 0, 0, 0, {"Owned"}},
        {0, 100, 0, 50, k_unit_param_type_strings, 0, 0, 0, {"OwnedS"}},
        {0, 10, 0, 7, k_unit_param_type_none, 0, 0, 0, {"Shadow"}},
        {0, 15, 0, 0, k_unit_param_type_strings, 0, 0, 0, {"Burst"}},

        // Page 3 — what the sequencer sends, and the boot-time file test
        {0, 15, 0, 0, PROBE_BITMAPS, 0, 0, 0, {"Grid"}},
        {0, 15, 0, 0, k_unit_param_type_strings, 0, 0, 0, {"Gate"}},
        {0, 15, 0, 0, k_unit_param_type_strings, 0, 0, 0, {"Event"}},
        // Files: the boot-time file test in a word (the knob does nothing)
        {0, 1, 0, 0, k_unit_param_type_strings, 0, 0, 0, {"Files"}},

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

        // Page 6 — every measurement, one per knob position (README.md,
        // "The Result list"); kNumResults in unit.cc
        {0, 15, 0, 0, k_unit_param_type_strings, 0, 0, 0, {"Result"}},
        {0, 0, 0, 0, k_unit_param_type_none, 0, 0, 0, {""}},
        {0, 0, 0, 0, k_unit_param_type_none, 0, 0, 0, {""}},
        {0, 0, 0, 0, k_unit_param_type_none, 0, 0, 0, {""}}
    }
};
