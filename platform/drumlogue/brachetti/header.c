/**
 * @file header.c
 * @brief drumlogue SDK unit header
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
    .unit_id = 0x5265736fU,                                // 'Reso'
    .version = 0x00010000U,
    .name = "Brachetti",
    .num_presets = 40,
    .num_params = 24,
    .params = {
        // Format: min, max, center, default, type, frac_digits, frac_type, <reserved>, name
        //
        // ── DEFAULTS MUST EQUAL PRESET 0 (Kick2) ───────────────────────────
        // HW (pass 29): "preset loaded as default is not Kick2".  Init() calls
        // LoadPreset(0), but the OS then restores its own parameter set on top
        // by calling unit_set_param_value() for all 24 params — and with no
        // stored state that means these `.default` fields.  15 of the 21
        // sound-shaping defaults disagreed with Kick2's preset row (Note 60 vs
        // 36 = two octaves up, Dkay 25 vs 200, NzRes 0 vs 420, ...), so the
        // unit booted showing "Kick2" while sounding like something else.
        // Every default below is now Kick2's shipped value; only Poly and
        // Rsntrs keep their own, because LoadPreset deliberately skips them
        // (they are global performance settings, not per-preset sound design).
        // If preset 0's row changes, change these with it.
        //
        // ── RANGES MUST COVER WHAT THE PRESETS STORE ───────────────────────
        // Related HW report: "a preset loaded is not showing actual parameter
        // values".  31 stored preset values sat OUTSIDE the range declared
        // here, so the OS parameter store could not represent what LoadPreset
        // had written and the display disagreed with the sound.  HitPos (25
        // presets store 0, min was 2) and Dkay (HHat-O stores 210, max was
        // 200) are widened below; the out-of-range TubRad values were clamped
        // into [0,20] in the preset table instead, which is provably
        // sound-neutral because every TubRad consumer already clamps there.

        // Page 1: Program and sample selection
        {0, 39, 0, 0, k_unit_param_type_strings, 0, 0, 0, {"Program"}},
        {24, 126, 1, 36, k_unit_param_type_midi_note, 0, 0, 0, {"Note"}},
        // Ex Bank/Sample (PCM layering removed): performance controls.
        // Poly = GLOBAL voice cap 1-4 (was cymbal-only 1-2 and always displayed
        // "2").  Cymbals are bounded on top of this knob by kCymCostBudget, a
        // ceiling on the aggregate cymbal CPU cost that charges each voice its
        // large FIXED per-sample cost as well as its resonator bank; in
        // practice that affords 2 simultaneous cymbal voices, so Poly above 2
        // adds voices to every other family but not to the cymbals.  The
        // display (type_strings) shows the EFFECTIVE polyphony for the current
        // preset, e.g. "4(2)" on the 2-kettle Timpani/Taiko kernel, "1" on a
        // string.
        {1, 4, 1, 4, k_unit_param_type_strings, 0, 0, 0, {"Poly"}},
        {25, 60, 1, 40, k_unit_param_type_percent, 0, 0, 0, {"Rsntrs"}},

        // Page 2: Mallet
        {0, 1000, 500, 360, k_unit_param_type_none, 1, 1, 0, {"MlltRes"}},
        // Stored ÷100: range 0–50 represents 0–5000, i.e. a step of 100 on the
        // encoder (was ÷10 over 10–500 = step 10).  HW: "effect is too subtle,
        // step should be 100 not 10, intermediate values make no difference" —
        // 490 encoder positions spanned a mapping whose whole travel is
        // value×0.02 into a [0.01,1] stiffness, so adjacent steps were 0.2 %
        // apart.  getParameterStrValue shows the real ×100 value.
        {0, 50, 0, 30, k_unit_param_type_strings, 0, 0, 0, {"MlltStif"}},
        {-100, 100, 0, 0, k_unit_param_type_none, 0, 0, 0, {"VlMllRes"}},
        {-100, 100, 0, 40, k_unit_param_type_none, 0, 0, 0, {"VlMllStf"}},

        // Page 3: Resonator I
        // 0-4 are the partials, value 5 means that next params dedicated to resonator are addressing
        // resonator A+B, value 6 is for resonator A only and 7 for resonator B only
        {0, 7, 0, 2, k_unit_param_type_strings, 0, 0, 0, {"Partls"}},
        // [0..8] String , Beam ,  Square , Membrn , Plate , Drumhd , Marmb , OpnTub , ClsTub
        {0, 8, 0, 3, k_unit_param_type_strings, 0, 0, 0, {"Model"}},
        // Stored ÷10: range 0–200 represents 0–2000. Step of 10 on the encoder.
        // getParameterStrValue shows the real ×10 value (0–2000).
        {0, 210, 25, 200, k_unit_param_type_strings, 0, 0, 0, {"Dkay"}},
        // [−10..30]
        {-10, 30, 0, 10, k_unit_param_type_none, 1, 1, 0, {"Mterl"}},

        // Page 4: Resonator II
        {-10, 30, 0, 0, k_unit_param_type_none, 1, 0, 0, {"Tone"}},
        {0, 98, 0, 36, k_unit_param_type_none, 2, 0, 0, {"HitPos"}},
        {0, 20, 0, 18, k_unit_param_type_none, 1, 0, 0, {"Rel"}},
        // Stored ÷10: range 0–199 represents 0–1990. Step of 10 on the encoder
        // (was step 1 over 0–1999 — far too fine to dial).  getParameterStrValue
        // shows the real ×10 value; code uses value×0.005 to normalise 0–1.
        {0, 199, 30, 1, k_unit_param_type_strings, 0, 0, 0, {"Inharm"}},

        // Page 5: Resonator III
        // Master LOWPASS cutoff: high = open, low = dark.  Stored ÷10
        // (effective 10–19990 Hz, capped at 16 kHz internally).  Default = open.
        // type_strings so getParameterStrValue can display the real Hz/kHz value.
        {1, 1999, 500, 1999, k_unit_param_type_strings, 0, 0, 0, {"Cutoff"}},
        {0, 20, 0, 0, k_unit_param_type_none, 1, 0, 0, {"TubRad"}},
        // Range 0-100, no fraction
        {0, 100, 0, 4, k_unit_param_type_none, 0, 0, 0, {"Gain"}},  // <--- Overdrive
        {0, 100, 50, 1, k_unit_param_type_percent, 0, 0, 0, {"NzMix"}},

        // Page 6: Noise II
        {0, 1000, 300, 420, k_unit_param_type_percent, 1, 1, 0, {"NzRes"}},
        {0, 2, 0, 0, k_unit_param_type_strings, 0, 0, 0, {"NzFltr"}},
        // Stored ÷10: range 2–2000 represents 20–20000 Hz. Step of 10 Hz on the encoder.
        // getParameterStrValue shows the real ×10 Hz/kHz value (same logic as LowCut).
        {2, 2000, 1200, 380, k_unit_param_type_strings, 0, 0, 0, {"NzFltFrq"}},
        // Stored ÷10: range 71–400 represents 710–4000. Step of 10 on the
        // encoder (was step 1 over 707–4000 — far too fine to dial).
        // getParameterStrValue shows the real ×10 value (710–4000); code uses
        // value×0.01 as the filter Q (0.71–4.00).
        {71, 400, 0, 71, k_unit_param_type_strings, 0, 0, 0, {"Resnc"}}
    }
};