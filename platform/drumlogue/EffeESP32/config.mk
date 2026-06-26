##############################################################################
# Project Configuration
#

PROJECT := effeesp32
PROJECT_TYPE := synth

##############################################################################
# Sources
#

# C sources
CSRC = header.c

# C++ sources (the FM engine is header-only: fm_voice6.h, fm_operator.h,
# svf_filter.h, adsr.h, drum_patches.h are all included from synth.h)
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
