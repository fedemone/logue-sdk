# Helper for run.sh: pulls CSRC / CXXSRC / UDEFS out of a unit's config.mk so
# the meter compiles exactly the translation units the SDK Makefile would.
include $(PROJECT)/config.mk

print-csrc:
	@echo $(CSRC)

print-cxxsrc:
	@echo $(CXXSRC)

print-udefs:
	@echo $(UDEFS)
