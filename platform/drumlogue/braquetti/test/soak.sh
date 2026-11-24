#!/usr/bin/env bash
# soak.sh <famA> <codesA> <famB> <codesB> <famC> <codesC> <gap> <seconds>
#
# Braquetti playing a pattern for minutes of audio (soak.cc says what it
# reports), built as the drumlogue builds it: on the host with -ffast-math,
# or with ARM=1 for ARM with the SDK's flags, run under qemu-arm.  The
# second hardware test's pattern 2, three minutes:
#
#   ./soak.sh 4 1,1,1,1,0,0,2,0,0,0,0,0,0,0,0,0 11 0,0,0,0,1,0,2,4,5,1,1,3,0,0,0,0 \
#             3 1,0,1,3,3,2,1,7,4,2,8,2,3,2,3,2 6000 180
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
unit="$(cd "$here/.." && pwd)"
drum="$(cd "$unit/.." && pwd)"
w="$(mktemp -d)"; trap 'rm -rf "$w"' EXIT
if [ -n "${ARM:-}" ]; then
    ARCH="-march=armv7-a -mtune=cortex-a7 -marm -mfloat-abi=hard -mfpu=neon-vfpv4"
    OPT="-Os -pipe -ffast-math -ffp-contract=off -fsigned-char -fno-stack-protector -fstrict-aliasing \
         -falign-functions=16 -fno-math-errno -fomit-frame-pointer -finline-limit=2000000000 \
         --param max-inline-insns-single=2000000000 -ffunction-sections -fdata-sections"
    cc="arm-linux-gnueabihf-gcc $ARCH"; cxx="arm-linux-gnueabihf-g++ $ARCH"; link="-static"; run=(qemu-arm)
    cxxu="-fno-threadsafe-statics -fno-exceptions -fno-rtti"
else
    OPT="-O2 -ffast-math -ffp-contract=off"; cc=gcc; cxx=g++; link=""; run=(); cxxu=""
fi
for c in header family_headers; do $cc -c $OPT -std=c11 -w -I"$drum/common" "$unit/$c.c" -o "$w/$c.o"; done
$cxx -c $OPT -std=gnu++14 $cxxu -w -I"$drum/common" -DBRAQUETTI_HOST_TEST "$unit/unit.cc" -o "$w/unit.o"
$cxx -c -O2 -std=gnu++14 -w -I"$drum/common" "$here/soak.cc" -o "$w/soak.o"
$cxx $link "$w/soak.o" "$w/unit.o" "$w/header.o" "$w/family_headers.o" -lm -o "$w/soak"
"${run[@]}" "$w/soak" "$@"
