/*
 * config.c -- the drum fan-out map.
 *
 * Each incoming note on DRUMS_IN_CHANNEL is looked up here and re-emitted as
 * (out_channel, out_note) on the drumlogue. Tracks sharing the same integer
 * choke_group are mutually exclusive; -1 means no choke.
 *
 * >>> THE VALUES BELOW ARE A PLACEHOLDER EXAMPLE. <<<
 * Replace them with your own note choices and your drumlogue's real per-track
 * channels. Discover the channels with the Python tool's --probe (once), or by
 * ear, then bake them in here.
 *
 *   in_note, out_channel, out_note, choke_group
 */
#include "config.h"

const drum_entry_t DRUM_MAP[] = {
    /* label          in_note  out_ch  out_note  choke_group */
    /* Bass Drum   */ {  36,      2,      36,       -1 },
    /* Snare       */ {  38,      3,      38,       -1 },
    /* Closed Hat  */ {  42,      4,      42,        1 },
    /* Open Hat    */ {  46,      5,      46,        1 },  /* group 1: CH chokes OH */
    /* Low Tom     */ {  41,      6,      41,       -1 },
    /* Mid Tom     */ {  45,      7,      45,       -1 },
    /* High Tom    */ {  48,      8,      48,       -1 },
    /* Clap        */ {  39,      9,      39,       -1 },
};

const unsigned DRUM_MAP_LEN = sizeof(DRUM_MAP) / sizeof(DRUM_MAP[0]);
