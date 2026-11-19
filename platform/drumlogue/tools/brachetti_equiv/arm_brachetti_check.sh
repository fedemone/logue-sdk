#!/usr/bin/env bash
# arm_brachetti_check.sh [tolerance]  — brachetti_check on the Cortex-A7.
#
# The host check (brachetti_check.sh) never compiles the NEON paths.  This
# cross-builds Brachetti and the twelve family units with the SDK Makefile's
# flags (-Os -ffast-math, NEON-VFPv4) and runs the same check under qemu-arm.
# Under -ffast-math the compiler may still reorder float operations
# differently in a different translation unit, so the samples are compared
# within a tolerance (default 1e-5) and the largest difference is printed;
# the knob read-backs and display strings must still match exactly.
# -ffp-contract=off is part of the units' flags (their config.mk): with GCC's
# default contraction the families' a*b+c fuse differently inside Brachetti
# and the renders differ at -30..-50 dB from the first sample.
# qemu is slow: each dedicated knob is tried at its two ends only
# (BRACHETTI_KNOB_POINTS=2; set it to 5 for the host run's full sweep).
# Needs: apt-get install g++-arm-linux-gnueabihf qemu-user
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
drum="$(cd "$here/../.." && pwd)"
tol="${1:-1e-5}"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
ARCH="-march=armv7-a -mtune=cortex-a7 -marm -mfloat-abi=hard -mfpu=neon-vfpv4"
OPT="-Os -pipe -ffast-math -ffp-contract=off -fsigned-char -fno-stack-protector -fstrict-aliasing \
     -falign-functions=16 -fno-math-errno -fomit-frame-pointer -fPIC -w -I$drum/common"
CXXO="-std=gnu++14 -fno-threadsafe-statics -fno-exceptions -fno-rtti \
      -finline-limit=2000000000 --param max-inline-insns-single=2000000000"
CC=arm-linux-gnueabihf-gcc
CXX=arm-linux-gnueabihf-g++

b="$drum/brachetti"
$CC $ARCH $OPT -std=c11 -c "$b/header.c" -o "$work/b.h.o"
$CC $ARCH $OPT -std=c11 -c "$b/family_headers.c" -o "$work/b.fh.o"
$CXX $ARCH $OPT $CXXO -I"$b" -c "$b/unit.cc" -o "$work/b.u.o"
$CXX $ARCH -shared "$work/b.u.o" "$work/b.h.o" "$work/b.fh.o" -lm -o "$work/brachetti.so"
libs=()
for u in kick tom kernel snare bar plate cymbal noise world lippa pizzico cordiera; do
    dir="$drum/brachetti_split/brachetti_$u"
    $CC $ARCH $OPT -std=c11 -c "$dir/header.c" -o "$work/$u.h.o"
    $CXX $ARCH $OPT $CXXO -I"$dir" -c "$dir/unit.cc" -o "$work/$u.u.o"
    $CXX $ARCH -shared "$work/$u.u.o" "$work/$u.h.o" -lm -o "$work/brachetti_$u.so"
    libs+=("$work/brachetti_$u.so")
done
$CXX $ARCH -O2 -std=gnu++14 -w -I"$drum/common" "$here/brachetti_check.cpp" -o "$work/brachetti_check" -ldl
BRACHETTI_KNOB_POINTS="${BRACHETTI_KNOB_POINTS:-2}" BRACHETTI_TOL="$tol" qemu-arm -L /usr/arm-linux-gnueabihf "$work/brachetti_check" "$work/brachetti.so" "${libs[@]}"
