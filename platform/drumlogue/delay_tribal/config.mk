##############################################################################
# Configuration for Makefile
#

PROJECT := delay_tribal
PROJECT_TYPE := delfx

##############################################################################
# Sources
#

# C sources
CSRC = header.c

# C++ sources
CXXSRC = unit.cc PercussionSpatializer.cc

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
# qemu, in instructions per 64-frame render (kick + tone input): -Os 25.4k,
# -O2 23.9k, -O3 22.7k.  -O3 runs 11% fewer than -Os, with output matching -Os
# to -143 dB.
OPTIM = -O3
