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

# LTO must be DISABLED.  With LTO enabled the compiler can fold constant data
# into immediate loads inside .text, which can push the section past the
# drumlogue firmware's per-unit code-size limit.
USE_LTO := no

# CRITICAL — .rodata size budget:
# The Drumlogue firmware checks the ELF ".text segment" size when loading a
# unit.  The 'size' command and that firmware check BOTH count .rodata as part
# of "text" (text = .text + .rodata + .init + .fini).  The per-unit limit is
# approximately 30 KB.
#
# The preset tables (modal_preset_configs, model_param_presets) total ~7 KB.
# Declaring them 'static constexpr' or 'static const' causes GCC to place them
# in .rodata, pushing the reported text size to ~30 KB and triggering the
# firmware's "Error: units 3" load failure.
#
# Fix: the arrays are declared plain 'static' (no const/constexpr) in
# synth_engine.h so GCC places them in .data.  .data is NOT counted toward
# the firmware's code-size check.  Do NOT add const/constexpr back.

