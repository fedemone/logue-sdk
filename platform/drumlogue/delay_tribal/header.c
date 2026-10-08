/**
 * @file header.c
 * @brief drumlogue SDK unit header for Enhanced Percussion Spatializer
 */

#include "unit.h"

// Struct layout: min, max, center, init, type, frac, frac_mode, reserved, name

/*
 * Build stamp: which optimisation level and compiler built this unit, so a
 * shipped .drmlgunit can be checked without its build log:
 *
 *     strings delay_tribal.drmlgunit | grep "build:"
 *
 * config.mk sets OPTIM.  Objects depend on config.mk (see the object rules
 * in the Makefile), but a tree built before that relinks its old
 * size-optimised objects unless it is cleaned once.  GCC defines
 * __OPTIMIZE_SIZE__ only at -Os, and this file is compiled with the same
 * flags as the rest of the unit; `used` keeps the string through LTO and
 * strip.
 */
#if defined(__OPTIMIZE_SIZE__)
#define UNIT_BUILD_OPT "-Os (size)"
#elif defined(__OPTIMIZE__)
#define UNIT_BUILD_OPT "-O2/-O3 (speed)"
#else
#define UNIT_BUILD_OPT "-O0 (debug)"
#endif
__attribute__((used)) static const char unit_build_stamp[] =
    "build: " UNIT_BUILD_OPT ", gcc " __VERSION__;

const __unit_header unit_header_t unit_header = {
    .header_size = sizeof(unit_header_t),
    .target      = UNIT_TARGET_PLATFORM | k_unit_module_delfx,
    .api         = UNIT_API_VERSION,
    .dev_id      = 0x46654465U,   // 'FeDe' - https://github.com/fedemone/logue-sdk
    .unit_id     = 0x30U,
    .version     = 0x00030000U,   // v3.0.0 - new design
    .name        = "Tribale",
    .num_presets = 0,
    .num_params  = 10,  // 0..9 — Gap (id 9) is a real parameter and must be exposed

    .params = {
        // Depth and Mix were removed: both are fixed at 100% internally.  Depth
        // now always uses the widest arrival spread, and the unit is fully wet
        // (the first clone is the leading stroke), so neither knob had a
        // setting worth offering.

        // Page 1
        // ID 0: Clones  0=2, 1=4, 2=6, 3=8, 4=10
        { 0, 4, 0, 0, k_unit_param_type_strings, 0, 0, 0, {"Clones"} },
        // ID 1: Mode  0=Tribal, 1=Military, 2=Angel
        { 0, 2, 0, 0, k_unit_param_type_strings, 0, 0, 0, {"Mode"} },
        // ID 2: Rate  x0.1 precision (0.0..10.0 Hz)
        { 0, 100, 0, 30, k_unit_param_type_none, 1, 0, 0, {"Rate"} },
        // ID 3: Spread  0-100%
        { 0, 100, 0, 80, k_unit_param_type_percent, 0, 0, 0, {"Spread"} },

        // Page 2
        // ID 4: Wobble (Pitch Wobble Depth)  0-100%
        { 0, 100, 0, 30, k_unit_param_type_percent, 0, 0, 0, {"Wobble"} },
        // ID 5: Scatter (ensemble looseness)  0-100%
        { 0, 100, 0, 45, k_unit_param_type_percent, 0, 0, 0, {"Scatter"} },
        // ID 6: SoftAtk (Attack Softening)  0-100%
        { 0, 100, 0, 20, k_unit_param_type_percent, 0, 0, 0, {"SoftAtk"} },
        // ID 7: Gap (distance between hits)  0-100%
        { 0, 100, 0, 45, k_unit_param_type_percent, 0, 0, 0, {"Gap"} },

        // Pages 3-6: blank padding to fill 24 slots
        { 0, 0, 0, 0, k_unit_param_type_none, 0, 0, 0, {""} },
        { 0, 0, 0, 0, k_unit_param_type_none, 0, 0, 0, {""} },
        { 0, 0, 0, 0, k_unit_param_type_none, 0, 0, 0, {""} },
        { 0, 0, 0, 0, k_unit_param_type_none, 0, 0, 0, {""} },
        { 0, 0, 0, 0, k_unit_param_type_none, 0, 0, 0, {""} },
        { 0, 0, 0, 0, k_unit_param_type_none, 0, 0, 0, {""} },
        { 0, 0, 0, 0, k_unit_param_type_none, 0, 0, 0, {""} },
        { 0, 0, 0, 0, k_unit_param_type_none, 0, 0, 0, {""} },
        { 0, 0, 0, 0, k_unit_param_type_none, 0, 0, 0, {""} },
        { 0, 0, 0, 0, k_unit_param_type_none, 0, 0, 0, {""} },
        { 0, 0, 0, 0, k_unit_param_type_none, 0, 0, 0, {""} },
        { 0, 0, 0, 0, k_unit_param_type_none, 0, 0, 0, {""} },
        { 0, 0, 0, 0, k_unit_param_type_none, 0, 0, 0, {""} },
        { 0, 0, 0, 0, k_unit_param_type_none, 0, 0, 0, {""} },
        { 0, 0, 0, 0, k_unit_param_type_none, 0, 0, 0, {""} },
        { 0, 0, 0, 0, k_unit_param_type_none, 0, 0, 0, {""} },
    }
};
