##############################################################################
# Project Configuration
#

PROJECT := omnipress
PROJECT_TYPE := masterfx

##############################################################################
# Sources
#

CSRC = header.c
CXXSRC = unit.cc

##############################################################################
# Include Paths
#

UINCDIR  = .

##############################################################################
# Compiler Flags
#

# Optimisation level.  The SDK Makefile reads OPTIM (default -Os) and never
# reads UCFLAGS, which is where -O3 used to be asked for -- so this unit
# always shipped at -Os.  (That line also said -mfloat-abi=softfp; the
# drumlogue is hard-float, and the Makefile sets the ABI itself.)  At -O3 the
# ARM build runs 3-9% fewer instructions per render than at -Os (Standard
# 7.8k -> 7.1k, Multiband 19.3k -> 17.7k; -O2 is within a few percent) and
# its output matches -Os to -64 dB or better.  Small, but on a drumlogue
# whose audio thread a polyphonic synth already fills, it is headroom.
OPTIM = -O3

##############################################################################
# Libraries
#

ULIBS  = -lm
ULIBS += -lc

##############################################################################
# Macros
#

UDEFS = -DARM_NEON_OPTIMIZATION