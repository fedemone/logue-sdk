##############################################################################
# Project Configuration
#
# Braquetti: a three-lane step sequencer playing Brachetti's family engines
# (PLAN.md).  Built like Brachetti (../brachetti/config.mk), whose engines it
# compiles in.

PROJECT := braquetti
PROJECT_TYPE := synth

##############################################################################
# Sources
#

# C sources
CSRC = header.c family_headers.c

# C++ sources
CXXSRC = unit.cc

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
# No -lrt: clock_gettime is a weak reference (unit.cc).

ULIBS  = -lm
ULIBS += -lc

##############################################################################
# Macros
#

# As Brachetti: no fused multiply-add, so the engines render as voiced.
# Add -DBRQ_OVERVIEW for the overview on page 6 instead of the CPU meter.
UDEFS = -ffp-contract=off

USE_LINK_GC := yes
USE_LTO := no
