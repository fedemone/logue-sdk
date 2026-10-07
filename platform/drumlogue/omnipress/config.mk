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
# ARM build runs 35-65% fewer instructions per render (Standard 4.3k -> 2.8k,
# Multiband with DRIVE 10.0k -> 3.7k) and its output matches -Os to -64 dB
# or better.  On a drumlogue whose audio thread a polyphonic synth already
# fills, that headroom is the point.
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