##############################################################################
# Configuration for Makefile
#

PROJECT := neon_labirinto_reverb
PROJECT_TYPE := revfx

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

UINCDIR =
# --- Optimisation level ---
# The SDK Makefile reads OPTIM (default -Os).  -O3 used to be asked for here
# through UDEFS, together with -mfpu/-mfloat-abi/-ffast-math (which the
# Makefile already sets), but "UDEFS =" under Macros below reset it, so the
# unit always shipped at -Os.  At -O3 the ARM build runs about half the
# instructions per render (labirinto 16.4k -> 7.4k) and its output matches -Os
# to -79 dB or better.
OPTIM = -O3

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

