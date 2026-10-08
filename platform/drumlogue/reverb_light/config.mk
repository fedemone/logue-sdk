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
# unit always shipped at -Os.  At -O3 the ARM build runs about a quarter fewer
# instructions per render (11.2k -> 8.2k at the default settings, before the
# LFO and delay-read changes took it to 6.2k).
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

