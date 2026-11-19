#!/usr/bin/env bash
# brachetti_check.sh — Brachetti (all twelve families in one unit) against
# the twelve family units of brachetti_split: builds all thirteen as host
# shared libraries and runs brachetti_check.cpp on them (what it checks is at
# the top of that file).  Exit 0 = every family plays bit-identically through
# Brachetti, and every knob reads back as the family's own.
#
#   ./brachetti_check.sh
#
# Host build without -ffast-math, so any difference at all is a real one.
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
drum="$(cd "$here/../.." && pwd)"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
CXXF="-std=gnu++14 -O2 -fPIC -w -I$drum/common"
CF="-std=c11 -O2 -fPIC -w -I$drum/common"

b="$drum/brachetti"
gcc $CF -c "$b/header.c" -o "$work/b.h.o"
gcc $CF -c "$b/family_headers.c" -o "$work/b.fh.o"
g++ $CXXF -I"$b" -c "$b/unit.cc" -o "$work/b.u.o"
g++ -shared "$work/b.u.o" "$work/b.h.o" "$work/b.fh.o" -o "$work/brachetti.so"

libs=()
for u in kick tom kernel snare bar plate cymbal noise world lippa pizzico cordiera; do
    dir="$drum/brachetti_split/brachetti_$u"
    gcc $CF -c "$dir/header.c" -o "$work/$u.h.o"
    g++ $CXXF -I"$dir" -c "$dir/unit.cc" -o "$work/$u.u.o"
    g++ -shared "$work/$u.u.o" "$work/$u.h.o" -o "$work/brachetti_$u.so"
    libs+=("$work/brachetti_$u.so")
done
g++ -std=gnu++14 -O2 -w -I"$drum/common" "$here/brachetti_check.cpp" -o "$work/brachetti_check" -ldl
"$work/brachetti_check" "$work/brachetti.so" "${libs[@]}"
