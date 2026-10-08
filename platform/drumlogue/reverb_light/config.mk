# --- Project Configuration for Light FDN Reverb ---

# 1. Project Name (The filename of your .drmlgunit)
PROJECT := luce_al_neon

# 2. Project Type
PROJECT_TYPE := revfx

# 3. C Source Files (Include the metadata header)
CSRC = header.c

# 4. C++ Source Files (The drumlogue SDK bridge)
# Note: unit.cc is the entry point, it includes FDNLightReverb.h
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
# unit always shipped at -Os -- which, measured, is the right level for it.
# Instructions per 64-frame render on the ARM build at the default settings:
# -Os 38.5k, -O2 45.4k, -O3 44.2k (output identical to -146 dB).  So OPTIM is
# left at the SDK default; objects still depend on config.mk, so setting it
# here rebuilds them.

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

