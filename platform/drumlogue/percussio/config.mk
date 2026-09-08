##############################################################################
# Project Configuration
#

PROJECT := percussio
PROJECT_TYPE := synth

##############################################################################
# Sources
#

# C sources
CSRC = header.c

# C++ sources
CXXSRC = unit.cc

# The engine is header-only (clm.h, banks.h, voice.h, presets.h, synth.h) and
# reached through unit.cc, so there is nothing else to list here.

##############################################################################
# Include Paths
#

UINCDIR  = .

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
