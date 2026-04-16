#pragma once
/**
 * @file spatial_modes.h
 * @brief Spatial mode definitions for the reverb_sussurro effect.
 *
 * Modes selected via parameter ID 1: 0=Sussurro, 1=Ricordo, 2=Ninfa.
 */

typedef enum {
    MODE_SUSSURRO   = 0,
    MODE_RICORDO = 1,
    MODE_NINFA    = 2,
    MODE_COUNT    = 3,  // marker only
} spatial_mode_t;
