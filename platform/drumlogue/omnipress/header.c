/**
 * @file header.c
 * @brief drumlogue SDK unit header for OmniPress Master Compressor
 */

#include "unit.h"

// Struct layout: min, max, center, init, type, frac, frac_mode, reserved, name

const __unit_header unit_header_t unit_header = {
    .header_size = sizeof(unit_header_t),
    .target      = UNIT_TARGET_PLATFORM | k_unit_module_masterfx,
    .api         = UNIT_API_VERSION,
    .dev_id      = 0x46654465U,   // 'FeDe' - https://github.com/fedemone/logue-sdk
    .unit_id     = 0x31U,
    .version     = 0x00020000U,   // v2.0.0
    .name        = "OmniPress",
    .num_presets = 0,
    .num_params  = 24,

    .params = {
        // Page 1: Core Dynamics
        // ID 0: THRESH  -60.0..0.0 dB  (x0.1, stored -600..0)
        { -600, 0, -600, -200, k_unit_param_type_db, 1, 1, 0, {"THRESH"} },
        // ID 1: SLOPE   1.0:1..10.0:1
        // 0.00 -> 0.33 : Expansion (Slope +3.0 down to 0.0)
        // 0.33 -> 0.66 : Compression (Slope 0.0 down to -1.0 limit)
        // 0.66 -> 1.00 : Negative Compression (Slope -1.0 down to -2.0 reverse)
        // Typed as strings so getParameterStrValue is consulted: this knob means
        // something different in each mode and the raw 0.01..1.00 readout showed
        // none of it. Standard/Multiband now read "Exp 1.5" / "2.0:1" / "Limit" /
        // "Rev 1.5", and Distressor reads its eight ratio steps by name.
        { 1, 100, 10, 40, k_unit_param_type_strings, 2, 1, 0, {"SLOPE"} },
        // ID 2: ATTACK  1..1000 ms  (x0.1 ms, stored 1..1000)
        { 1, 1000, 1, 150, k_unit_param_type_msec, 1, 1, 0, {"ATTACK"} },
        // ID 3: RELEASE 10..2000 ms
        { 10, 2000, 10, 200, k_unit_param_type_msec, 0, 0, 0, {"RELEASE"} },

        // Page 2: Character & Output
        // ID 4: MAKEUP  0.0..24.0 dB  (x0.1, stored 0..240)
        { 0, 240, 0, 0, k_unit_param_type_db, 1, 1, 0, {"MAKEUP"} },
        // ID 5: DRIVE   0-100%
        // Typed as strings, like SLOPE, so getParameterStrValue is consulted:
        // in Distressor mode this knob crosses into its slam region partway
        // along (see DRIVE_SLAM_KNEE) and the readout says so -- "60%" below
        // it, "SLAM 75" above.  Every other mode reads as a plain percentage.
        { 0, 100, 0, 0, k_unit_param_type_strings, 0, 0, 0, {"DRIVE"} },
        // ID 6: MIX  -100 (DRY) .. +100 (WET)
        { -100, 100, 0, 100, k_unit_param_type_drywet, 0, 0, 0, {"MIX"} },
        // ID 7: SC HPF  20..500 Hz
        { 20, 500, 20, 20, k_unit_param_type_hertz, 0, 0, 0, {"SC HPF"} },

        // Page 3: Mode Selection
        // ID 8: COMP MODE  0=Standard, 1=Multiband.  The Distressor engine is
        // still in the source (distressor_mode.h, drive_slam.h, wavefolder.h)
        // and on the bench, but no longer on the panel -- see README.
        { 0, 1, 0, 0, k_unit_param_type_strings, 0, 0, 0, {"COMP MODE"} },    // ID 8 - Compressor mode selection
        // Omnipressor-style limits on how far the function knob may push the VCA.
        // These shipped at -1.0/+1.0 dB, which clamped Standard mode to a 2 dB
        // window and made it read as nearly bypassed however the SLOPE was set.
        // -20 dB leaves room for real compression; +6 dB allows the upward side
        // of the transfer curve without letting quiet passages run away.
        { -300, 0, -30, -200, k_unit_param_type_db, 1, 1, 0, {"ATT LMT"} },   // ID 9
        { 0, 300, 30, 60, k_unit_param_type_db, 1, 1, 0, {"GAIN LMT"} },      // ID 10
        //   Standard: 0=Peak, 1=RMS, 2=Blend (Multiband has its own per-band
        //   peak followers and only reads the +4 bit). The Distressor engine,
        //   bench only, reads 0=Basic, 1=Emph, 2=Link, 3=Emph+Link.
        //   +4 on any of the above listens to the external sidechain input
        //   (SC L/R) instead of the main bus. All 24 parameter slots the SDK
        //   allows are taken, so the sidechain source shares this control.
        { 0, 7, 0, 0, k_unit_param_type_strings, 0, 0, 0, {"DETECT"} },       // ID 11

        // Page 4: Overlord EQ, and Multiband solo/mute
        { 0, 100, 50, 50, k_unit_param_type_percent, 0, 0, 0, {"BASS"} },     // ID 12 - Overlord
        { 0, 100, 50, 50, k_unit_param_type_percent, 0, 0, 0, {"TREBLE"} },   // ID 13 - Overlord
        { 0, 100, 50, 50, k_unit_param_type_percent, 0, 0, 0, {"PRESENCE"} }, // ID 14 - Overlord
        // ID 15: Multiband solo/mute. One band at a time: 0=Off, 1-3 solo
        // Low/Mid/High, 4-6 mute Low/Mid/High. This slot was DstrDist, and it
        // replaces both the MBand selector and MBState.
        { 0, 6, 0, 0, k_unit_param_type_strings, 0, 0, 0, {"SoloMute"} },   // ID 15

        // Page 5: Multiband thresholds, one knob per band, and the low split.
        // Offsets from THRESH, -30.0..+30.0 dB: THRESH moves all three pivots
        // and 0 pivots where Standard would.  Typed as strings so the readout
        // can carry its sign ("+0.0dB", "-6.0dB").
        { -300, 300, 0, 0, k_unit_param_type_strings, 1, 1, 0, {"Lo Thresh"} },  // ID 16
        { -300, 300, 0, 0, k_unit_param_type_strings, 1, 1, 0, {"Mid Thresh"} }, // ID 17
        { -300, 300, 0, 0, k_unit_param_type_strings, 1, 1, 0, {"Hi Thresh"} },  // ID 18
        // ID 19: low/mid split, 62.5 Hz at 0 to 1 kHz at 100, log; 50 = 250 Hz
        { 0, 100, 0, 50, k_unit_param_type_strings, 0, 0, 0, {"Xover Lo"} },    // ID 19

        // Page 6: Multiband ratios, one knob per band, and the high split.
        // In series with SLOPE's curve: 1.0:1 follows SLOPE exactly, 4.0:1
        // compresses that band four times harder than SLOPE alone.
        { 10, 200, 10, 10, k_unit_param_type_strings, 1, 1, 0, {"Lo Ratio"} },  // ID 20 - 1.0..20.0:1
        { 10, 200, 10, 10, k_unit_param_type_strings, 1, 1, 0, {"Mid Ratio"} }, // ID 21
        { 10, 200, 10, 10, k_unit_param_type_strings, 1, 1, 0, {"Hi Ratio"} },  // ID 22
        // ID 23: mid/high split, 1 kHz at 0 to 16 kHz at 100, log; 33 = 2.5 kHz.
        // The two ranges meet only at 1 kHz, so the splits can never cross.
        { 0, 100, 0, 33, k_unit_param_type_strings, 0, 0, 0, {"Xover Hi"} },    // ID 23

    }
};
