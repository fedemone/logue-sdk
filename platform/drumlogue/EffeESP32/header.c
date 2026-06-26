/**
 * @file header.c
 * @brief EffeESP32 — 6-operator FM drum synth (drumlogue unit header / GUI).
 *
 * 24 fixed parameters.  Index 0 selects one of 59 instrument patches; the
 * remaining knobs override the cached patch (see synth.h / drum_patches.h).
 * Ported from copych/ESP32-S3_FM_Drum_Synth (MIT).
 */

#include "unit.h"

const __unit_header unit_header_t unit_header = {
    .header_size = sizeof(unit_header_t),
    .target = UNIT_TARGET_PLATFORM | k_unit_module_synth,
    .api = UNIT_API_VERSION,
    .dev_id = 0x46654465U,   // 'FeDe' — https://github.com/fedemone/logue-sdk
    .unit_id = 0x34U,        // unique among FeDe units (EffeMD = 0x33)
    .version = 0x00010000U,  // v1.0.0
    .name = "EffeESP32",
    .num_presets = 1,
    .num_params = 24,
    .params = {
        // {min, max, center, init, type, frac, frac_mode, reserved, {name}}

        // Page 1 — instrument & voice
        {0, 58, 0, 1, k_unit_param_type_strings, 0, 0, 0, {"Instr"}},
        {-24, 24, 0, 0, k_unit_param_type_semi,    0, 0, 0, {"Pitch"}},
        {0, 200, 0, 100, k_unit_param_type_percent, 0, 0, 0, {"Level"}},
        {-100, 100, 0, 0, k_unit_param_type_pan,    0, 0, 0, {"Pan"}},

        // Page 2 — algorithm & envelope
        {0, 17, 0, 0, k_unit_param_type_strings, 0, 0, 0, {"Algo"}},
        {0, 2000, 0, 1, k_unit_param_type_msec, 0, 0, 0, {"Attack"}},
        {0, 2000, 0, 0, k_unit_param_type_msec, 0, 0, 0, {"Hold"}},
        {0, 2000, 0, 200, k_unit_param_type_msec, 0, 0, 0, {"Decay"}},

        // Page 3 — envelope tail & velocity
        {0, 100, 0, 0, k_unit_param_type_percent, 0, 0, 0, {"Sustain"}},
        {0, 2000, 0, 100, k_unit_param_type_msec, 0, 0, 0, {"Release"}},
        {0, 100, 0, 50, k_unit_param_type_percent, 0, 0, 0, {"VeloMod"}},
        // Filter + carrier waveform selector (string table, -4..5):
        //   0  = filter off (patch waveform)   1 = filter on (patch waveform)
        //  -1..-4 = filter off + Sin/Tri/Sqr/Saw carrier
        //   2..5  = filter on  + Sin/Tri/Sqr/Saw carrier
        {-4, 5, 0, 0, k_unit_param_type_strings, 0, 0, 0, {"Filter"}},

        // Page 4 — SVF morph filter
        {20, 20000, 0, 16000, k_unit_param_type_hertz, 0, 0, 0, {"FltFrq"}},
        {0, 100, 0, 0, k_unit_param_type_percent, 0, 0, 0, {"FltRes"}},
        {0, 100, 0, 33, k_unit_param_type_percent, 0, 0, 0, {"FltMrp"}},
        {-50, 50, 0, 0, k_unit_param_type_none, 0, 0, 0, {"Detune"}},

        // Page 5 — operator levels 1..4
        {0, 100, 0, 80, k_unit_param_type_percent, 0, 0, 0, {"Op1"}},
        {0, 100, 0, 80, k_unit_param_type_percent, 0, 0, 0, {"Op2"}},
        {0, 100, 0, 80, k_unit_param_type_percent, 0, 0, 0, {"Op3"}},
        {0, 100, 0, 80, k_unit_param_type_percent, 0, 0, 0, {"Op4"}},

        // Page 6 — operator levels 5..6 + trigger note
        {0, 100, 0, 80, k_unit_param_type_percent, 0, 0, 0, {"Op5"}},
        {0, 100, 0, 80, k_unit_param_type_percent, 0, 0, 0, {"Op6"}},
        // Note: MIDI note this instrument answers to / triggers on gate.
        //       Reloaded to the instrument's canonical GM note on selection.
        {0, 127, 0, 36, k_unit_param_type_midi_note, 0, 0, 0, {"Note"}},
        // Global operator-feedback macro (scales every op's feedback, 0..200%).
        {0, 200, 0, 100, k_unit_param_type_percent, 0, 0, 0, {"Feedbk"}},
    }
};
