##############################################################################
# Project Configuration
#

PROJECT := brachetti_plate
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

ULIBS  = -lm
ULIBS += -lc

##############################################################################
# Macros
#

#UDEFS =

# Linker GC: strips unreachable code/data.  Keep enabled for code-budget reasons.
USE_LINK_GC := yes

# LTO must be DISABLED.  With LTO enabled the compiler can fold constant data
# into immediate loads inside .text, which can push the section past the
# drumlogue firmware's per-unit code-size limit.
USE_LTO := no

# Size: measure, do not assume (tools/brachetti_equiv/arm_size.sh).  The
# preset tables follow the original brachetti's .data pattern — see its
# CLAUDE.md, ".rodata / .data Constraint".

