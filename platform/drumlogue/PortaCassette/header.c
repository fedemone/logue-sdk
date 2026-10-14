/**
 * @file header.c
 * @brief drumlogue SDK unit header for PortaCassette Emulator
 */

#include "unit.h"

// Struct layout: min, max, center, init, type, frac, frac_mode, reserved, name
/*
 * Build stamp: which optimisation level and compiler built this unit, so a
 * shipped .drmlgunit can be checked without its build log:
 *
 *     strings portacassette.drmlgunit | grep "build:"
 *
 * config.mk leaves OPTIM at the SDK default, -Os: -O2 and -O3 measured no
 * faster for this unit (see config.mk).  Objects depend on config.mk (see the
 * object rules in the Makefile), so changing it rebuilds them.  GCC defines
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
    .target      = UNIT_TARGET_PLATFORM | k_unit_module_masterfx, // Master bus effect
    .api         = UNIT_API_VERSION,
    .dev_id      = 0x46654465U,   // 'FeDe'
    .unit_id     = 0x506F7274U,   // 'Port'
    .version     = 0x00020100U,   // v2.1.0
    .name        = "PortaK7",     // 8 chars max for OLED
    .num_presets = 0,             // Can add presets later
    .num_params  = 16,            // 4 full pages

    .params = {
        // ==========================================
        // PAGE 1: Tape Mechanics & Gain Staging
        // ==========================================
        // ID 0: Tape Age (Wow/Flutter & high cut) (0 to 100%)
        { 0, 100, 0, 10, k_unit_param_type_percent, 0, 0, 0, {"Age"} },

        // ID 1: Mix (0 to 100%)
        { 0, 100, 50, 100, k_unit_param_type_percent, 0, 0, 0, {"Mix"} },

        // ID 2: Trim (0 to 100% = 0 to +30 dB) - drives the channel strip's
        // op-amps, which clip about 20 dB above nominal; a half-power fader
        // make-up follows.  Before the EQ, as on the console.
        { 0, 100, 0, 20, k_unit_param_type_percent, 0, 0, 0, {"Trim"} },

        // ID 3: Tape Drive (0 to 100%) - Pushes signal hard into the magnetic tape
        { 0, 100, 0, 40, k_unit_param_type_percent, 0, 0, 0, {"Drive"} },

        // ==========================================
        // PAGE 2: Machine Character & EQ Low band
        // ==========================================
        // ID 4: dbx Mode (0 = Active, 1 = Encode Only (Bypass Decode), 2 = Off,
        //               3 = Decode Only (Bypass Encode))
        { 0, 3, 0, 0, k_unit_param_type_strings, 0, 0, 0, {"dbx NR"} },

        // ID 5: Model (0 = 244, 1 = 424, 2 = 488, 3 = Vinyl, 4 = 424 at NORMAL
        //              speed) - homage to old Tascam Portastudio
        { 0, 4, 0, 0, k_unit_param_type_strings, 0, 0, 0, {"Model"} },

        // ID 6: Low Frequency (2 = 20Hz, 40 = 400Hz -> x10 mapping)
        { 2, 40, 10, 8, k_unit_param_type_strings, 0, 0, 0, {"LowHz"} },

        // ID 7: Low Gain (-12dB to +12dB mapped as 0 to 240, 120 is 0dB)
        { 0, 240, 120, 120, k_unit_param_type_strings, 0, 0, 0, {"LowGain"} },

        // ==========================================
        // PAGE 3: Parametric EQ (Mid / High)
        // ==========================================
        // ID 8: Mid Frequency (20 = 200Hz, 500 = 5000Hz -> x10 mapping)
        { 20, 500, 100, 100, k_unit_param_type_strings, 0, 0, 0, {"MidHz"} },

        // ID 9: Mid Gain (-12dB to +12dB mapped as 0 to 240, 120 is 0dB)
        { 0, 240, 120, 120, k_unit_param_type_strings, 0, 0, 0, {"MidGain"} },

        // ID 10: High Frequency (20 = 2000Hz, 150 = 15000Hz -> x100 mapping)
        { 20, 150, 80, 100, k_unit_param_type_strings, 0, 0, 0, {"HiHz"} },

        // ID 11: High Gain (-12dB to +12dB mapped as 0 to 240, 120 is 0dB)
        { 0, 240, 120, 120, k_unit_param_type_strings, 0, 0, 0, {"HiGain"} },


        // ==========================================
        // PAGE 4: Noise & dbx ballistics
        // ==========================================
        // ID 12: Crosstalk (mapped to 0.0f to 0.1f).  1 gives each machine's
        // published separation at 1 kHz.
        { 0, 10, 0, 1, k_unit_param_type_none, 0, 0, 0, {"X-talk"} },
        // ID 13: Tape Bias Hiss (0 to 100%)
        { 0, 100, 0, 15, k_unit_param_type_percent, 0, 0, 0, {"Hiss"} },
        // ID 14: dbx detector attack, 0.1 to 100.0 ms (x0.1 ms, stored 1..1000)
        { 1, 1000, 30, 30, k_unit_param_type_msec, 1, 1, 0, {"Attack"} },
        // ID 15: dbx detector release, 10 to 2000 ms
        { 10, 2000, 150, 150, k_unit_param_type_msec, 0, 0, 0, {"Release"} },

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