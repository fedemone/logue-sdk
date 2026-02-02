/**
 *  @file header.c
 *  @brief drumlogue SDK unit header
 *
 *  Copyright (c) 2020-2022 KORG Inc. All rights reserved.
 *
 */

#include "unit.h"  // Note: Include common definitions for all units

// ---- Unit header definition  --------------------------------------------------------------------

const __unit_header unit_header_t unit_header = {
    .header_size = sizeof(unit_header_t),                  // leave as is, size of this header
    .target = UNIT_TARGET_PLATFORM | k_unit_module_synth,  // target platform and module for this unit
    .api = UNIT_API_VERSION,                               // logue sdk API version against which unit was built
    .dev_id = 0x46747a6cU,                                 // developer identifier
    .unit_id = 0x5265736fU,                                // Id for this unit, should be unique within the scope of a given dev_id
    .version = 0x00010100U,                                // This unit's version: major.minor.patch (major<<16 minor<<8 patch).
    .name = "Resonator",                                   // Name for this unit, will be displayed on device
    .num_presets = 0,                                      // Number of internal presets this unit has
    .num_params = 20,                                      // Number of parameters for this unit, max 24
    .params = {
        // Format: min, max, center, default, type, fractional, frac. type, <reserved>, name

        // See common/runtime.h for type enum and unit_param_t structure

        // Page 1: Note and velocity
        // Resonator note for Gate mode
        {1, 126, 1, 60, k_unit_param_type_midi_note, 0, 0, 0, {"Note"}},
        // Pitch bend range
        {0, 12, 0, 2, k_unit_param_type_none, 0, 0, 0, {"Bend"}},
        // Velocity -> excitation level
        {-100, 100, 0, 0, k_unit_param_type_none, 0, 0, 0, {"Vel Lvl"}},
        // Velocity -> excitation filter
        {-100, 100, 0, 0, k_unit_param_type_none, 0, 0, 0, {"Vel Filt"}},

        // Page 2: Sample
        // Sample bank
        {0, 6, 0, 0, k_unit_param_type_strings, 0, 0, 0, {"Bank"}},
        // Sample number
        {1, 128, 1, 1, k_unit_param_type_none, 0, 0, 0, {"Sample"}},
        // Excitation sample start
        {0, (1000), 0, (0), k_unit_param_type_percent, 1, 1, 0, {"Start"}},
        // Excitation sample end
        {0, (1000), 0, (1000), k_unit_param_type_percent, 1, 1, 0, {"End"}},
        
        // Page 3: Excitation
        // Excitation level (dB)
        {-200, 60, 0, 0, k_unit_param_type_db, 1, 1, 0, {"Level"}},
        // Excitation filter type (None, LP12, HP12, BP6, LP6, HP6)
        {0, 5, 0, 0, k_unit_param_type_strings, 0, 0, 0, {"Filter"}},
        // Excitation filter cutoff (/100)
        {1, 200, 1, 50, k_unit_param_type_khertz, 1, 1, 0, {"Cutoff"}},
        // Excitation filter resonance (Q) (/10)
        {1, 200, 0, 10, k_unit_param_type_none, 1, 1, 0, {"Reso Q"}},

        // Page 4: Resonator in Decay phase
        // Resonator feedback (-100.0 .. +100.0)
        {-1000, 1000, 0, 990, k_unit_param_type_none, 1, 1, 0, {"Dcy Fdbk"}},
        // Resonator damping amount
        {0, 100, 0, 20, k_unit_param_type_none, 0, 0, 0, {"Dcy Damp"}},
        // Resonator dispersion
        {0, 4, 0, 0, k_unit_param_type_none, 0, 0, 0, {"Dispersn"}},
        // Key tracking
        {0, 100, 0, 100, k_unit_param_type_none, 0, 0, 0, {"Keytrack"}},

        // Page 5: Resonator in Release phase
        // Resonator feedback (-100.0 .. +100.0)
        {-1000, 1000, 0, 950, k_unit_param_type_none, 1, 1, 0, {"Rls Fdbk"}},
        // Resonator damping amount
        {0, 100, 0, 80, k_unit_param_type_none, 0, 0, 0, {"Rls Damp"}},
        {0, 0, 0, 0, k_unit_param_type_none, 0, 0, 0, {""}},
        {0, 0, 0, 0, k_unit_param_type_none, 0, 0, 0, {""}},

        // Page 6
        {0, 0, 0, 0, k_unit_param_type_none, 0, 0, 0, {""}},
        {0, 0, 0, 0, k_unit_param_type_none, 0, 0, 0, {""}},
        {0, 0, 0, 0, k_unit_param_type_none, 0, 0, 0, {""}},
        {0, 0, 0, 0, k_unit_param_type_none, 0, 0, 0, {""}}}};
