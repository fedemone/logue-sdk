/**
 * @file fm_presets.cc
 * @brief Factory preset data for FM Percussion Synth
 *
 * Presets are rebuilt around the fixed 4-engine model.
 * The old voice-allocation and resonant fields are intentionally removed.
 */

#include "fm_presets.h"
#include <stddef.h>

const fm_preset_t FM_PRESETS[NUM_OF_PRESETS] = {
    // 0: TightKick
    {
        "TightKick",
        100, 20, 10, 40,
        85, 75, 10, 20,
        20, 15, 20, 25,
        0, 10, LFO_TARGET_PITCH, 10,
        0, 0, LFO_TARGET_NONE, 0,
        12, 35, 15, 20},

    // 1: HeavyKick
    {
        "HeavyKick",
        100, 10, 5, 35,
        70, 95, 10, 30,
        15, 10, 15, 30,
        0, 15, LFO_TARGET_PITCH, 20,
        0, 0, LFO_TARGET_NONE, 0,
        18, 25, 45, 15},

    // 2: ClickKick
    {
        "ClickKick",
        100, 15, 10, 50,
        90, 55, 5, 15,
        25, 10, 35, 20,
        1, 18, LFO_TARGET_INDEX, 20,
        1, 0, LFO_TARGET_NONE, 0,
        8, 55, 10, 35},

    // 3: CrackSnare
    {
        "CrackSnare",
        35, 100, 20, 20,
        20, 30, 90, 75,
        10, 10, 20, 20,
        2, 35, LFO_TARGET_INDEX, 50,
        0, 10, LFO_TARGET_NOISE_MIX, 15,
        20, 55, 20, 20},

    // 4: BodySnare
    {
        "BodySnare",
        25, 100, 10, 15,
        25, 20, 65, 95,
        15, 10, 10, 10,
        0, 20, LFO_TARGET_PITCH, 15,
        0, 0, LFO_TARGET_NONE, 0,
        25, 30, 55, 10},

    // 5: GhostSnare
    {
        "GhostSnare",
        20, 55, 10, 15,
        15, 20, 60, 45,
        5, 10, 5, 5,
        0, 8, LFO_TARGET_ENV, 20,
        0, 0, LFO_TARGET_NONE, 0,
        16, 25, 10, 5},

    // 6: RimSnare
    {
        "RimSnare",
        10, 80, 20, 20,
        15, 10, 85, 45,
        10, 10, 20, 10,
        1, 28, LFO_TARGET_PITCH, -35,
        0, 15, LFO_TARGET_INDEX, 10,
        30, 45, 15, 20},

    // 7: MetalClang
    {
        "MetalClang",
        10, 15, 100, 20,
        10, 10, 20, 15,
        95, 65, 10, 10,
        0, 35, LFO_TARGET_INDEX, 40,
        0, 0, LFO_TARGET_NONE, 0,
        8, 80, 35, 45},

    // 8: MetalWash
    {
        "MetalWash",
        15, 20, 100, 25,
        10, 15, 25, 20,
        80, 90, 20, 25,
        0, 20, LFO_TARGET_ENV, 40,
        0, 0, LFO_TARGET_NONE, 0,
        70, 55, 40, 30},

    // 9: GongHit
    {
        "GongHit",
        0, 10, 100, 0,
        0, 0, 0, 0,
        100, 95, 0, 0,
        3, 10, LFO_TARGET_PITCH, -20,
        0, 0, LFO_TARGET_NONE, 0,
        90, 25, 45, 20},  // env_shape was 128 + 90

    // 10: BellRing
    {
        "BellRing",
        0, 0, 100, 0,
        0, 0, 0, 0,
        100, 85, 0, 0,
        4, 18, LFO_TARGET_INDEX, 35,
        0, 0, LFO_TARGET_NONE, 0,
        110, 35, 55, 30  // env_shape was 128 + 110
    },

    // 11: PercBlock
    {
        "PercBlock",
        30, 25, 10, 100,
        20, 20, 10, 20,
        10, 15, 95, 65,
        0, 20, LFO_TARGET_PITCH, 15,
        0, 0, LFO_TARGET_NONE, 0,
        14, 55, 20, 20},

    // 12: PercTom
    {
        "PercTom",
        35, 20, 5, 100,
        15, 60, 10, 20,
        10, 10, 75, 90,
        0, 12, LFO_TARGET_ENV, 20,
        1, 0, LFO_TARGET_NONE, 0,
        22, 25, 50, 15},

    // 13: PercWood
    {
        "PercWood",
        20, 15, 10, 100,
        35, 45, 10, 20,
        10, 10, 85, 40,
        1, 15, LFO_TARGET_INDEX, 10,
        0, 0, LFO_TARGET_NONE, 0,
        18, 40, 35, 10},

    // 14: DryHit
    {
        "DryHit",
        60, 60, 60, 60,
        60, 40, 55, 35,
        50, 35, 55, 35,
        0, 0, LFO_TARGET_NONE, 0,
        0, 0, LFO_TARGET_NONE, 0,
        4, 20, 20, 5},

    // 15: DriveKit
    {
        "DriveKit",
        100, 20, 20, 20,
        85, 65, 10, 15,
        15, 20, 20, 25,
        1, 22, LFO_TARGET_INDEX, 20,
        0, 0, LFO_TARGET_NONE, 0,
        10, 45, 25, 80},

    // 16: DarkPulse
    {
        "DarkPulse",
        80, 50, 35, 50,
        55, 45, 35, 45,
        35, 40, 35, 45,
        0, 8, LFO_TARGET_ENV, 10,
        0, 0, LFO_TARGET_NONE, 0,
        16, 20, 30, 20},

    // 17: BrightPulse
    {
        "BrightPulse",
        75, 45, 65, 45,
        70, 40, 60, 35,
        70, 55, 50, 35,
        1, 25, LFO_TARGET_INDEX, 25,
        0, 0, LFO_TARGET_NONE, 0,
        8, 55, 25, 15},

    // 18: Industrial
    {
        "Industrial",
        35, 60, 90, 35,
        30, 25, 65, 45,
        85, 70, 55, 40,
        0, 32, LFO_TARGET_METAL_GATE, 55,
        0, 20, LFO_TARGET_INDEX, 20,
        70, 65, 55, 75},  // env_shape was 128 + 70

    // 19: Shaker
    {
        "Shaker",
        15, 20, 95, 80,
        10, 10, 20, 20,
        75, 80, 85, 70,
        0, 55, LFO_TARGET_NOISE_MIX, 70,
        0, 30, LFO_TARGET_ENV, 20,
        12, 40, 45, 25},

    // 20: EuclidKit
    {
        "EuclidKit",
        100, 100, 100, 100,
        60, 55, 55, 55,
        50, 50, 50, 50,
        2, 20, LFO_TARGET_PITCH, 20,
        6, 15, LFO_TARGET_INDEX, 10,
        18, 45, 35, 20},

    // 21: WidePerc
    {
        "WidePerc",
        40, 35, 35, 100,
        25, 55, 20, 55,
        20, 40, 70, 80,
        1, 15, LFO_TARGET_LFO2_PHASE, 35,
        4, 20, LFO_TARGET_PITCH, 25,
        20, 35, 45, 15},

    // 22: LowBody
    {
        "LowBody",
        90, 30, 20, 60,
        65, 90, 20, 75,
        20, 25, 25, 65,
        0, 12, LFO_TARGET_PITCH, 10,
        0, 0, LFO_TARGET_NONE, 0,
        14, 20, 70, 10},

    // 23: HardDrive
    {
        "HardDrive",
        100, 50, 60, 50,
        90, 70, 70, 55,
        65, 55, 65, 55,
        1, 30, LFO_TARGET_INDEX, 30,
        0, 0, LFO_TARGET_NONE, 0,
        8, 60, 45, 95},

    // 24: SoftHit
    {
        "SoftHit",
        85, 85, 70, 85,
        30, 50, 30, 45,
        25, 35, 25, 45,
        0, 10, LFO_TARGET_ENV, 15,
        0, 0, LFO_TARGET_NONE, 0,
        4, 20, 35, 10},

    // 25: Experimental
    {
        "ExpTrack",
        60, 50, 60, 50,
        55, 55, 55, 55,
        55, 55, 55, 55,
        4, 40, LFO_TARGET_LFO2_PHASE, 50,
        8, 30, LFO_TARGET_METAL_GATE, 40,
        40, 50, 50, 50}};  // env_shape was 128 + 40

void load_fm_preset(uint8_t idx, int8_t *params) {
    if (idx >= NUM_OF_PRESETS || params == NULL) {
        return;
    }

    const fm_preset_t *p = &FM_PRESETS[idx];

    params[PARAM_KPROB] = p->prob_kick;
    params[PARAM_SPROB] = p->prob_snare;
    params[PARAM_MPROB] = p->prob_metal;
    params[PARAM_PPROB] = p->prob_perc;

    params[PARAM_KICK_ATK] = p->kick_attack;
    params[PARAM_KICK_BODY] = p->kick_body;
    params[PARAM_SNARE_ATK] = p->snare_attack;
    params[PARAM_SNARE_BODY] = p->snare_body;

    params[PARAM_METAL_ATK] = p->metal_attack;
    params[PARAM_METAL_BODY] = p->metal_body;
    params[PARAM_PERC_ATK] = p->perc_attack;
    params[PARAM_PERC_BODY] = p->perc_body;

    params[PARAM_LFO1_SHAPE] = p->lfo1_shape;
    params[PARAM_LFO1_RATE] = p->lfo1_rate;
    params[PARAM_LFO1_TARGET] = p->lfo1_target;
    params[PARAM_LFO1_DEPTH] = p->lfo1_depth;

    params[PARAM_EUCL_TUN] = p->eucl_tun;
    params[PARAM_LFO2_RATE] = p->lfo2_rate;
    params[PARAM_LFO2_TARGET] = p->lfo2_target;
    params[PARAM_LFO2_DEPTH] = p->lfo2_depth;

    params[PARAM_ENV_SHAPE] = p->env_shape;
    params[PARAM_HIT_SHAPE] = p->hit_shape;
    params[PARAM_BODY_TILT] = p->body_tilt;
    params[PARAM_DRIVE] = p->drive;
}
