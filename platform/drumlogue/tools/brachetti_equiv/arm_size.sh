#!/usr/bin/env bash
# arm_size.sh <unit-dir> [<unit-dir> ...]
#
# Cross-compiles a drumlogue synth unit for the Cortex-A7 the way the SDK
# Makefile does (same -march/-mfpu/-O flags) and prints the section sizes.
# This is the in-session version of the check in brachetti/CLAUDE.md
# ("Host Build / Test Commands"); it exists so every Brachetti family unit can
# be measured with one command.  Needs: apt-get install g++-arm-linux-gnueabihf
#
# gcc 13 rejects the Makefile's -finline-limit=9999999999 (> INT_MAX);
# 2000000000 behaves the same.  This is not the vendor gcc 6.5, so .text moves
# a little between toolchains; .rodata/.data/.bss are exact.
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
common="$(cd "$here/../../common" && pwd)"
ARCH="-march=armv7-a -mtune=cortex-a7 -marm -mfloat-abi=hard -mfpu=neon-vfpv4"
OPT="-Os -pipe -ffast-math -fsigned-char -fno-stack-protector -fstrict-aliasing \
     -falign-functions=16 -fno-math-errno -fomit-frame-pointer"
CXXO="-std=gnu++14 -fno-threadsafe-statics -fno-exceptions -fno-rtti \
      -finline-limit=2000000000 --param max-inline-insns-single=2000000000"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT
for unit in "$@"; do
    dir="$(cd "$unit" && pwd)"
    name="$(basename "$dir")"
    arm-linux-gnueabihf-g++ -c $ARCH $OPT $CXXO -DRENDER_STAGE=4 -fPIC \
        -ffunction-sections -fdata-sections -w -I"$dir" -I"$common" \
        "$dir/unit.cc" -o "$tmp/unit.o"
    arm-linux-gnueabihf-gcc -c $ARCH $OPT -std=c11 -DRENDER_STAGE=4 -fPIC \
        -ffunction-sections -fdata-sections -w -I"$dir" -I"$common" \
        "$dir/header.c" -o "$tmp/header.o"
    arm-linux-gnueabihf-g++ $ARCH $OPT -shared -Wl,--gc-sections \
        "$tmp/unit.o" "$tmp/header.o" -lm -o "$tmp/$name.elf"
    printf '%-20s' "$name"
    arm-linux-gnueabihf-size -A "$tmp/$name.elf" |
        awk '$1==".text"||$1==".rodata"||$1==".data"||$1==".data.rel.ro"||$1==".bss"{printf " %s=%s", $1, $2} END{print ""}'
done
