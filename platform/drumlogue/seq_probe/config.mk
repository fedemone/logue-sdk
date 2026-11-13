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
# No -lpthread on purpose: pthread_create / pthread_join / clock_gettime are
# weak references (unit.cc), so the unit loads even where they are missing.

ULIBS  = -lm
ULIBS += -lc

##############################################################################
# Macros
#

UDEFS = 
