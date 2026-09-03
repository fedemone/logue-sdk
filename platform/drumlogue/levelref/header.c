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
        // Which reference signal to generate.  Pink noise is the one to judge
        // by ear -- its spectrum resembles the material being compared against
        // -- but it is also the most peak-limited: see TgtLUFS.
        {0, 4, 0, 0, k_unit_param_type_strings, 0, 0, 0, {"Signal"}},
        // Target loudness, as an index: 0 is -40 LUFS, 40 is 0 LUFS.  Declared
        // as a strings parameter so the unit formats the display itself, and so
        // shows the loudness it is DELIVERING rather than the one requested --
        // with "MAX" when the signal's ceiling has been reached.  A read-out
        // parameter cannot do that job: unit_param_t has no read-only flag, so
        // every parameter is a turnable knob whose displayed value is the one
        // the drumlogue sent, which makes a read-out indistinguishable from a
        // control that does nothing.  This unit had one, and it cost a hardware
        // session: the knob moved, the number moved, the level did not.
        {0, 40, 20, 20, k_unit_param_type_strings, 0, 0, 0, {"TgtLUFS"}},
        // Drone: sounds continuously from load, no note needed -- the mode to
        // use when comparing against a looping pattern.
        // Gated: follows note on/off, for checking the track's note path.
        {0, 1, 0, 0, k_unit_param_type_strings, 0, 0, 0, {"Mode"}},
        {0, 0, 0, 0, k_unit_param_type_none, 0, 0, 0, {""}},

        // Page 2
        {0, 0, 0, 0, k_unit_param_type_none, 0, 0, 0, {""}},
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
