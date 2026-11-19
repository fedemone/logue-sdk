/**
 * @file family_headers.c
 * @brief The twelve family units' header.c tables, compiled into Brachetti
 *
 * Brachetti shows each family's dedicated knobs through its own 24 slots, and
 * formats and scales them by what the family unit declares for that knob
 * (range, type, fraction digits, name).  Rather than copy those numbers, each
 * family's own header.c is compiled here once more under another name
 * (brc_hdr_<family>), as a plain const table: no `.unit_header` section, so
 * the only unit header in this binary is Brachetti's (header.c).  A change to
 * a family's header.c reaches Brachetti with the next build.
 */

#include "unit.h"

#undef __unit_header
#define __unit_header   /* a plain const table here, not this unit's header */

#define unit_header brc_hdr_kick
#include "../brachetti_split/brachetti_kick/header.c"
#undef unit_header
#define unit_header brc_hdr_tom
#include "../brachetti_split/brachetti_tom/header.c"
#undef unit_header
#define unit_header brc_hdr_kernel
#include "../brachetti_split/brachetti_kernel/header.c"
#undef unit_header
#define unit_header brc_hdr_snare
#include "../brachetti_split/brachetti_snare/header.c"
#undef unit_header
#define unit_header brc_hdr_bar
#include "../brachetti_split/brachetti_bar/header.c"
#undef unit_header
#define unit_header brc_hdr_plate
#include "../brachetti_split/brachetti_plate/header.c"
#undef unit_header
#define unit_header brc_hdr_cymbal
#include "../brachetti_split/brachetti_cymbal/header.c"
#undef unit_header
#define unit_header brc_hdr_noise
#include "../brachetti_split/brachetti_noise/header.c"
#undef unit_header
#define unit_header brc_hdr_world
#include "../brachetti_split/brachetti_world/header.c"
#undef unit_header
#define unit_header brc_hdr_lippa
#include "../brachetti_split/brachetti_lippa/header.c"
#undef unit_header
#define unit_header brc_hdr_pizzico
#include "../brachetti_split/brachetti_pizzico/header.c"
#undef unit_header
#define unit_header brc_hdr_cordiera
#include "../brachetti_split/brachetti_cordiera/header.c"
#undef unit_header
