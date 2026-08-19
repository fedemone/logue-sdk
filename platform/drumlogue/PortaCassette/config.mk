##############################################################################
# Project Configuration
#

PROJECT := portacassette
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

# NOTE: the SDK Makefile never reads UCFLAGS, and the arch/FPU flags it does
# use (-march=armv7-a -mfpu=neon-vfpv4 -mfloat-abi=hard) are already correct for
# the drumlogue.  Setting -mfloat-abi=softfp here looked like it was overriding
# them but was silently ignored; had it applied it would have produced an ABI
# mismatch at the unit boundary.  Extra defines belong in UDEFS below.

##############################################################################
# Libraries
#

ULIBS  = -lm
ULIBS += -lc

##############################################################################
# Macros
#

UDEFS = -DARM_NEON_OPTIMIZATION