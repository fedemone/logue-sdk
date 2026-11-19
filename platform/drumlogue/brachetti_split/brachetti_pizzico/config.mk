##############################################################################
# Project Configuration
#

PROJECT := brachetti_pizzico
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

# No fused multiply-add contraction (-ffp-contract=off; GCC's default is to
# fuse a*b+c into one VFMA wherever its inlining happens to put them
# together).  Some Brachetti coefficients are sensitive to that rounding, so
# with contraction the same family code sounds measurably different in this
# unit and in Brachetti, which compiles all twelve families in one unit; off,
# they agree to 1e-6 and stay close to the host renders the presets were
# voiced against.  Cost: +0.1-0.3 % instructions (arm_cpu.sh).  Passed here
# because UDEFS reaches both the C and the C++ command lines.
UDEFS = -ffp-contract=off

# Linker GC: strips unreachable code/data.  Keep enabled for code-budget reasons.
USE_LINK_GC := yes

# LTO must be DISABLED.  With LTO enabled the compiler can fold constant data
# into immediate loads inside .text, which can push the section past the
# drumlogue firmware's per-unit code-size limit.
USE_LTO := no

# Size: measure, do not assume (tools/brachetti_equiv/arm_size.sh).  The
# loop and all-pass delay lines live in .bss (synth_engine.h).

