/**
 * @file unit.cc
 * @brief SeqProbeLite — SeqProbe without bitmap knobs or file access (../seq_probe)
 *
 * Its own file rather than a path to ../seq_probe in config.mk: see header.c.
 */
#define PROBE_LITE 1
#include "../seq_probe/unit.cc"
