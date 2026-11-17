##############################################################################
# Project Configuration
#

PROJECT := seq_probe
PROJECT_TYPE := synth

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

UINCDIR  = 

##############################################################################
# Library Paths
#

ULIBDIR = 

##############################################################################
# Libraries
#
# No -lrt: clock_gettime is a weak reference (unit.cc), so the unit loads even
# on a C library that keeps it elsewhere.

ULIBS  = -lm
ULIBS += -lc

##############################################################################
# Macros
#

UDEFS = 
