##############################################################################
# Project Configuration
#

PROJECT := scruta_astri
PROJECT_TYPE := synth

##############################################################################
# Sources
#

# C sources
CSRC = header.c

# C++ sources
CXXSRC = unit.cc

# List all your FM percussion synth source files
# Note: These are all headers, so no need to list them in sources
# They will be included via synth.h

##############################################################################
# Include Paths
#

UINCDIR  = .

##############################################################################
# Compiler Flags
#

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
# qemu, in instructions per 64-frame render (a note every 100 ms): -Os 55.5k,
# -O2 53.1k, -O3 52.0k.  -O3 runs 6% fewer than -Os, with output matching -Os
# to -124 dB.
OPTIM = -O3
