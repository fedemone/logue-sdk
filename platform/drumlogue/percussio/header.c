/**
 *  @file header.c
 *  @brief drumlogue SDK unit header -- Percussio
 *
 *  Copyright (c) 2020-2022 KORG Inc. All rights reserved.
 *
 */

#include "unit.h"  // Note: Include common definitions for all units

// ---- Unit header definition  --------------------------------------------------------------------

const __unit_header unit_header_t unit_header = {
    .header_size = sizeof(unit_header_t),
    .target = UNIT_TARGET_PLATFORM | k_unit_module_synth,
    .api = UNIT_API_VERSION,
    .dev_id = 0x46654465U,   // 'FeDe' - https://github.com/fedemone/logue-sdk
    .unit_id = 0x50657263U,  // 'Perc' - unique among FeDe units
    .version = 0x00010000U,  // v1.0.0 -- version 0 is rejected at load on drumlogue
    .name = "Percussio",
    .num_presets = 55,
    .num_params = 24,
    .params = {
        // Format: min, max, center, default, type, frac_digits, frac_mode, <reserved>, name
        //
        // ── TWO RULES, BOTH LEARNED ON HARDWARE (see brachetti/CLAUDE.md) ──
        //
        // 1. THE DEFAULTS BELOW MUST EQUAL PRESET 0.  Init() calls LoadPreset(0),
        //    but the OS then writes its own parameter set on top by calling
        //    unit_set_param_value() for all 24 params -- and with no stored
        //    state, that set is these `default` fields.  If they disagree with
        //    preset 0's row the unit boots showing "BrshSnr1" and sounding like
        //    something else.  Preset 0 is BrshSnr1 in presets.h; change one and
        //    change the other.
        //
        // 2. THE RANGES MUST COVER EVERY VALUE ANY PRESET STORES.  The OS
        //    parameter store cannot represent an out-of-range value, so a preset
        //    that ships one loads as something the display then contradicts.
        //
        // test_dsp.cpp checks both against this header, so neither can drift.
        //
        // Fractional parameters use decimal mode (frac_mode 1): the stored value
        // is the displayed one times 10^frac.  Level/Attack/Hold/NseAmp are
        // tenths of a percent, Reso/Blend hundredths, Ratio thousandths,
        // Index1/Index2 hundredths.

        // Page 1 -- voice
        // Synthesis technique: subtractive, additive, FM, Karplus-Strong, granular.
        {0, 4, 0, 0, k_unit_param_type_strings, 0, 0, 0, {"Method"}},
        // Variant within the technique; the strings change with Method.
        {0, 3, 0, 0, k_unit_param_type_strings, 0, 0, 0, {"Model"}},
        // CLM's duration argument.  10 s covers Chowning's bell.
        {5, 10000, 0, 200, k_unit_param_type_msec, 0, 0, 0, {"Decay"}},
        {0, 1000, 0, 400, k_unit_param_type_percent, 1, 1, 0, {"Level"}},

        // Page 2 -- amplitude envelope: (0 S) (Attack 1) (Hold M) (100 0)
        {0, 1000, 0, 50, k_unit_param_type_percent, 1, 1, 0, {"Attack"}},
        {0, 1000, 0, 50, k_unit_param_type_percent, 1, 1, 0, {"Hold"}},
        // CLM env :base.  Above 1 holds then drops; below 1 drops then tails.
        {0, 10, 0, 5, k_unit_param_type_strings, 0, 0, 0, {"Curve"}},
        {-24, 24, 0, 0, k_unit_param_type_semi, 0, 0, 0, {"Tune"}},

        // Page 3 -- noise source and resonator
        // one-pole b1 / one-zero a1, in hundredths.  Sign selects the response.
        {-99, 99, 0, 90, k_unit_param_type_none, 0, 0, 0, {"Coef"}},
        // Pole radius r, for two-pole (subtract-pp) and ppolar (add-noise).
        {0, 9999, 0, 9900, k_unit_param_type_percent, 2, 1, 0, {"Reso"}},
        // Reference frequency at note 60: two-pole centre, FM carrier, and the
        // fundamental of the ratio-based additive banks.
        {20, 8000, 0, 400, k_unit_param_type_hertz, 0, 0, 0, {"Freq"}},
        // randh rate as a percentage of the sample rate; the page uses 49%.
        {1, 50, 0, 49, k_unit_param_type_percent, 0, 0, 0, {"NseRate"}},

        // Page 4 -- FM (Chowning, JAES 1973)
        {0, 16000, 0, 1400, k_unit_param_type_none, 3, 1, 0, {"Ratio"}},
        {0, 1000, 0, 0, k_unit_param_type_none, 2, 1, 0, {"Index1"}},
        {0, 1000, 0, 100, k_unit_param_type_none, 2, 1, 0, {"Index2"}},
        // Modulator envelope breakpoint, as a percentage of the duration.
        {1, 99, 0, 50, k_unit_param_type_percent, 0, 0, 0, {"ModDcy"}},

        // Page 5 -- additive
        {0, 10, 0, 0, k_unit_param_type_strings, 0, 0, 0, {"Bank"}},
        // Fraction of the bank's partials used, low to high.
        {5, 100, 0, 100, k_unit_param_type_percent, 0, 0, 0, {"Partial"}},
        {0, 1000, 0, 0, k_unit_param_type_percent, 1, 1, 0, {"NseAmp"}},
        // Per-hit spread of pitch and level.
        {0, 100, 0, 0, k_unit_param_type_percent, 0, 0, 0, {"Rand"}},

        // Page 6 -- Karplus-Strong and granular
        // Blend factor b: probability of inverting the two-point average.
        {0, 10000, 0, 5000, k_unit_param_type_percent, 2, 1, 0, {"Blend"}},
        // Wavetable length p, in samples at note 60.
        {20, 12288, 0, 1741, k_unit_param_type_none, 0, 0, 0, {"Length"}},
        {1, 100, 0, 20, k_unit_param_type_none, 0, 0, 0, {"Densty"}},
        {1, 500, 0, 100, k_unit_param_type_msec, 0, 0, 0, {"Grain"}}}};
