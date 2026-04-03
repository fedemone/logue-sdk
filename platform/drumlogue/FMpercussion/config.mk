##############################################################################
# Project Configuration
#

PROJECT := fm_perc_synth
PROJECT_TYPE := synth

##############################################################################
# Sources
# List all your FM percussion synth source files
# Note: If they are all headers, there's no need to list them in sources
# They will be included via synth.h
#

# C sources
CSRC = header.c

# C++ sources
CXXSRC = unit.cc fm_presets.cc


##############################################################################
# Include Paths
#

UINCDIR  = .

##############################################################################
# Compiler Flags
#

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