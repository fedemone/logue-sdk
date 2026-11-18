/**
 * @file header.c
 * @brief drumlogue SDK unit header — Brachetti Lippa
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
    .unit_id = 0x42724C70U,                                // 'BrLp' (Brachetti Lippa)
    .version = 0x00010000U,
    .name = "BrcLippa",
    // Zero on purpose: the Program parameter
    // is the preset control (stored with the pattern, automatable), and a
    // non-zero count would draw a second, competing preset UI.
    .num_presets = 0,
    .num_params = 24,
    .params = {

        // Format: min, max, center, default, type, frac_digits, frac_type, <reserved>, name

        // Page 1 — common: performance
        // Program: the preset (this family's presets only)
        {0, 12, 0, 0, k_unit_param_type_strings, 0, 0, 0, {"Program"}},
        // Pitch: semitones from the preset's root note (sequencer path)
        {-24, 24, 0, 0, k_unit_param_type_semi, 0, 0, 0, {"Pitch"}},
        // Poly: global voice cap 1-4
        {1, 4, 1, 4, k_unit_param_type_strings, 0, 0, 0, {"Poly"}},
        // Velocity: velocity bias: -100 ghost .. 0 as played .. +100 wham
        {-100, 100, 0, 0, k_unit_param_type_none, 0, 0, 0, {"Velocity"}},

        // Page 2 — common: master
        {-10, 30, 0, 0, k_unit_param_type_none, 1, 0, 0, {"Tone"}},
        // Cutoff: master low-pass, stored /10 (10-19990 Hz), 1999 = open
        {1, 1999, 500, 1999, k_unit_param_type_strings, 0, 0, 0, {"Cutoff"}},
        // Resnc: master low-pass Q, stored /10 (displayed 710-4000)
        {71, 400, 0, 71, k_unit_param_type_strings, 0, 0, 0, {"Resnc"}},
        // Gain: master drive
        {0, 100, 0, 0, k_unit_param_type_none, 0, 0, 0, {"Gain"}},

        // Page 3 — dedicated
        {0, 1000, 500, 800, k_unit_param_type_none, 1, 1, 0, {"MlltRes"}},
        // MlltStif: stored /100, displayed x100 (0-5000)
        {0, 50, 0, 30, k_unit_param_type_strings, 0, 0, 0, {"MlltStif"}},
        // Bend (BrcLippa's own, in VlMllRes' slot): the glide's depth — how far
        // above the note a hard strike starts.  − less (× ¼), + more (× 4, and
        // a glide added where a preset has none)
        {-100, 100, 0, 0, k_unit_param_type_none, 0, 0, 0, {"Bend"}},
        {-100, 100, 0, 0, k_unit_param_type_none, 0, 0, 0, {"VlMllStf"}},

        // Page 4 — dedicated
        // BendTm (BrcLippa's own, in Partls' slot): the glide's time, × ¼ .. × 4
        {-100, 100, 0, 0, k_unit_param_type_none, 0, 0, 0, {"BendTm"}},
        // Snares (BrcLippa's own, in Model's slot): the snares on the head's
        // motion.  − quieter (× ¼), + louder (× 4, and added where a preset
        // has none)
        {-100, 100, 0, 0, k_unit_param_type_none, 0, 0, 0, {"Snares"}},
        // Dkay: stored /10, displayed x10 (0-2000)
        {0, 200, 25, 160, k_unit_param_type_strings, 0, 0, 0, {"Dkay"}},
        // Punch (the family's own, in Rel's slot): the transient shaper.  + the
        // attack passes and the body drops after it (up to −15 dB), − a softer,
        // rounder attack.  0 = off
        {-100, 100, 0, 0, k_unit_param_type_none, 0, 0, 0, {"Punch"}},

        // Page 5 — dedicated
        {-10, 30, 0, 0, k_unit_param_type_none, 1, 1, 0, {"Mterl"}},
        // HitPos: bipolar; on the loop, off the centre towards the rim
        {-98, 98, 0, 0, k_unit_param_type_none, 2, 0, 0, {"HitPos"}},
        // Inharm: bipolar; on the loop, the all-pass gain (the inharmonicity)
        {-100, 100, 0, 0, k_unit_param_type_none, 0, 0, 0, {"Inharm"}},
        // ApDelay (TubRad's role): the all-pass delay against the period
        {0, 20, 0, 10, k_unit_param_type_none, 1, 0, 0, {"ApDelay"}},

        // Page 6 — dedicated
        // NzMix: the noise burst's, and the noise in the loop's excitation
        {0, 100, 50, 10, k_unit_param_type_percent, 0, 0, 0, {"NzMix"}},
        // Blend (BrcLippa's own, in NzRes' slot): Karplus & Strong's drum —
        // each trip round the loop flips sign at random, up to half the time
        {0, 100, 0, 0, k_unit_param_type_percent, 0, 0, 0, {"Blend"}},
        // SnrTone (BrcLippa's own, in NzFltr's slot): the snares' band, ± 1 octave
        {-100, 100, 0, 0, k_unit_param_type_none, 0, 0, 0, {"SnrTone"}},
        // NzFltFrq: stored /10 (20-20000 Hz); the noise burst's filter, and
        // the corner of the loop excitation's noise
        {2, 2000, 1200, 800, k_unit_param_type_strings, 0, 0, 0, {"NzFltFrq"}}
    }
};
