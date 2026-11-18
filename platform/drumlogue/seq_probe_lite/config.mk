##############################################################################
# Project Configuration
#
# SeqProbeLite: SeqProbe (../seq_probe, see its README.md) without bitmap
# knobs and without file access, to tell whether either keeps a unit off the
# drumlogue's list.  Its own unit id, so both install together.  header.c and
# unit.cc here set PROBE_LITE and include ../seq_probe's.

PROJECT := seq_probe_lite
PROJECT_TYPE := synth

##############################################################################
# Sources
#

# C sources
CSRC = header.c

# C++ sources
CXXSRC = unit.cc

# List ASM source files here
ASMSRC = 

ASMXSRC = 

##############################################################################
# Include Paths
#

UINCDIR  = 

##############################################################################
# Library Paths
#

ULIBDIR = 

##############################################################################
# Libraries
#

ULIBS  = -lm
ULIBS += -lc

##############################################################################
# Macros
#

UDEFS = 
