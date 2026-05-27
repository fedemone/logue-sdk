##############################################################################
# Project Configuration
#

PROJECT := ripplerx
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
# RENDER_STAGE: Incremental debug build — uncomment ONE line to isolate silence:
#   Stage 1 — Raw exciter only (mallet impulse / PCM sample, no waveguide)
#   Stage 2 — + Waveguide resonators
#   Stage 3 — + master_env fade + voice squelch
#   Stage 4 — + Tone EQ + master filter + overdrive  (default when unset)
UDEFS += -DRENDER_STAGE=4

# Linker GC: strips unreachable code/data.  Keep enabled for code-budget reasons.
USE_LINK_GC := yes

# LTO must be DISABLED.  With LTO on, the C++14 'static constexpr' arrays
# (modal_preset_configs, model_param_presets) have internal linkage and the
# LTO pass constant-folds all array accesses into immediate loads, moving the
# data from .rodata into .text.  That doubles .text (16 KB → 30 KB) and can
# push the section past the drumlogue firmware's per-unit limit.  With LTO off
# the arrays remain as real .rodata objects and .text stays at a normal size.
USE_LTO := no

