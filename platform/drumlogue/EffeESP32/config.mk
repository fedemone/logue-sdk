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

##############################################################################
# Optimisation level
#
# The SDK Makefile reads OPTIM (default -Os).  Measured on the ARM build under
# qemu, in instructions per 64-frame render (a note every 100 ms): -Os 25.7k,
# -O2 23.5k, -O3 22.6k.  -O3 runs 12% fewer than -Os, with output matching -Os
# to -142 dB.
OPTIM = -O3
