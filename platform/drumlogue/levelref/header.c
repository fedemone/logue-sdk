/**
 * @file header.c
 * @brief LevelRef -- drumlogue SDK unit header
 *
 * A calibrated reference signal generator.  The Target parameter is the
 * loudness the unit actually produces, in LUFS, so the number on the screen
 * is the number a meter reads back.  See synth.h for the calibration.
 */

#include "unit.h"

const __unit_header unit_header_t unit_header = {
    .header_size = sizeof(unit_header_t),
    .target = UNIT_TARGET_PLATFORM | k_unit_module_synth,
    .api = UNIT_API_VERSION,
    .dev_id = 0x46654465U,   // 'FeDe' - https://github.com/fedemone/logue-sdk
    .unit_id = 0x4C766C52U,  // 'LvlR' - unique among FeDe units
    .version = 0x00010000U,  // v1.0.0 -- version=0 causes load rejection on drumlogue
    .name = "LevelRef",
    .num_presets = 0,
    .num_params = 24,
    .params = {
        // Format: min, max, center, default, type, fractional, frac. type, <reserved>, name

        // Page 1
        // Which reference signal to generate.  Pink noise is the default: it is
        // the only one of these whose spectrum resembles the material it is
        // being compared against, so it is the one to judge by ear.
        {0, 4, 0, 0, k_unit_param_type_strings, 0, 0, 0, {"Signal"}},
        // Target loudness in LUFS.  Calibrated per signal (see synth.h), so
        // -20 here really does measure -20 LUFS on the bus.  Not every signal
        // reaches every value: see ActLUFS.
        {-40, 0, -20, -20, k_unit_param_type_none, 0, 0, 0, {"TgtLUFS"}},
        // The loudness actually being delivered, in LUFS.  Not a control.  It
        // equals TgtLUFS until the request passes what the signal can reach
        // without clipping, and then stops -- PinkNz stops at -10, Sine100 at
        // -2, Sine1k and WhitNz go the whole way to 0.  Read it, not TgtLUFS,
        // when taking a measurement: it shares page 1 with TgtLUFS so the two
        // can be compared without paging, and a ceiling is seen rather than
        // mistaken for a level.
        {-40, 0, -20, -20, k_unit_param_type_none, 0, 0, 0, {"ActLUFS"}},
        // Drone: sounds continuously from load, no note needed -- the mode to
        // use when comparing against a looping pattern.
        // Gated: follows note on/off, for checking the track's note path.
        {0, 1, 0, 0, k_unit_param_type_strings, 0, 0, 0, {"Mode"}},

        // Page 2
        // Read-back of the peak level the current setting produces, in dBFS
        // (negative).  Not a control.  With the target capped to what the
        // signal can deliver this can no longer warn of a clip, so it is
        // headroom information rather than a warning, and sits off page 1.
        {-99, 0, 0, 0, k_unit_param_type_none, 0, 0, 0, {"PeakdB"}},
        {0, 0, 0, 0, k_unit_param_type_none, 0, 0, 0, {""}},
        {0, 0, 0, 0, k_unit_param_type_none, 0, 0, 0, {""}},
        {0, 0, 0, 0, k_unit_param_type_none, 0, 0, 0, {""}},

        // Page 3
        {0, 0, 0, 0, k_unit_param_type_none, 0, 0, 0, {""}},
        {0, 0, 0, 0, k_unit_param_type_none, 0, 0, 0, {""}},
        {0, 0, 0, 0, k_unit_param_type_none, 0, 0, 0, {""}},
        {0, 0, 0, 0, k_unit_param_type_none, 0, 0, 0, {""}},

        // Page 4
        {0, 0, 0, 0, k_unit_param_type_none, 0, 0, 0, {""}},
        {0, 0, 0, 0, k_unit_param_type_none, 0, 0, 0, {""}},
        {0, 0, 0, 0, k_unit_param_type_none, 0, 0, 0, {""}},
        {0, 0, 0, 0, k_unit_param_type_none, 0, 0, 0, {""}},

        // Page 5
        {0, 0, 0, 0, k_unit_param_type_none, 0, 0, 0, {""}},
        {0, 0, 0, 0, k_unit_param_type_none, 0, 0, 0, {""}},
        {0, 0, 0, 0, k_unit_param_type_none, 0, 0, 0, {""}},
        {0, 0, 0, 0, k_unit_param_type_none, 0, 0, 0, {""}},

        // Page 6
        {0, 0, 0, 0, k_unit_param_type_none, 0, 0, 0, {""}},
        {0, 0, 0, 0, k_unit_param_type_none, 0, 0, 0, {""}},
        {0, 0, 0, 0, k_unit_param_type_none, 0, 0, 0, {""}},
        {0, 0, 0, 0, k_unit_param_type_none, 0, 0, 0, {""}}}};
