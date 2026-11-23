##############################################################################
# brachetti_split — builds the twelve single-family Brachetti units.
#
# Not a unit itself: it runs each family unit's own Makefile.  The docker
# build script treats this folder as one project, so
#
#     build drumlogue/brachetti_split
#
# builds and installs all twelve (BrcKick … BrcCordiera), and `build --drumlogue`
# reaches them through here.  One family on its own:
#
#     build --drumlogue drumlogue/brachetti_split/brachetti_kick
#
# (--drumlogue because the script finds a unit's platform from its parent
# folder's name, which is brachetti_split here), or `make` in its folder.
# make INSTALLDIR=... install is passed down to every unit.
##############################################################################

UNITS := brachetti_kick brachetti_tom brachetti_kernel brachetti_snare \
         brachetti_bar brachetti_plate brachetti_cymbal brachetti_noise \
         brachetti_world brachetti_lippa brachetti_pizzico brachetti_cordiera

.PHONY: all install clean $(UNITS)

all: $(UNITS)

$(UNITS):
	$(MAKE) -C $@

install: all
	@for u in $(UNITS); do $(MAKE) -C $$u install || exit 1; done

clean:
	@for u in $(UNITS); do $(MAKE) -C $$u clean || exit 1; done
