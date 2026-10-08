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

# Optimisation level.  The SDK Makefile reads OPTIM (default -Os), and this
# unit stays at -Os: measured on the ARM build, -Os, -O2 and -O3 all run
# 18.3k instructions per 64-frame render, so the larger code buys nothing.
# Objects depend on config.mk, so setting OPTIM here rebuilds them.

##############################################################################
# Libraries
#

ULIBS  = -lm
ULIBS += -lc

##############################################################################
# Macros
#

UDEFS = -DARM_NEON_OPTIMIZATION