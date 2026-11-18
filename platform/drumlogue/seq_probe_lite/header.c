/**
 * @file header.c
 * @brief SeqProbeLite — SeqProbe's header without bitmap knobs (../seq_probe)
 *
 * Its own file rather than a path to ../seq_probe in config.mk: the SDK
 * Makefile adds a source's folder to make's VPATH, and make then finds
 * seq_probe's build/ and objects there and never creates its own.
 */
#define PROBE_LITE 1
#include "../seq_probe/header.c"
