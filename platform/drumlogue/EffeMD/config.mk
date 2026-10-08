##############################################################################
# Project Configuration
#

PROJECT := effemd
PROJECT_TYPE := synth

##############################################################################
# Sources
#

# C sources
CSRC = header.c

# C++ sources (the SDK Makefile only builds .cc files)
CXXSRC = unit.cc \
		 FmClapModel.cc \
		 FmCowbellModel.cc \
		 FmCymbalModel.cc \
		 FmKickModel.cc \
		 FmRimshotModel.cc \
		 FmSnareModel.cc \
		 FmTomModel.cc \
		 TRXBassDrum.cc \
		 TRXClaves.cc \
		 TRXHiHat.cc \
		 TRXSnareDrum.cc \
		 FmWhistleModel.cc \
		 TRXGongModel.cc

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
# qemu, in instructions per 64-frame render (a note every 100 ms): -Os 15.1k,
# -O2 12.9k, -O3 14.9k.  -O2 runs 14% fewer than -Os, with output identical to
# -Os.  -O2 is the fastest here; -O3 inlines and unrolls more and lands back
# near -Os.
OPTIM = -O2
