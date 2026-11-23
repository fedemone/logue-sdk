/**
 * @file family_headers.c
 * @brief The twelve family units' header tables, as Brachetti compiles them
 *
 * Brachetti's family table (catalog.h) points at each family unit's header
 * (brc_hdr_<family>); this defines them.  A file of its own rather than a
 * path to ../brachetti in config.mk: the SDK Makefile puts each source's
 * folder on make's VPATH, and make would then find Brachetti's build/ and
 * objects there and never build its own (seq_probe_lite has the same file
 * for the same reason).
 */
#include "../brachetti/family_headers.c"
