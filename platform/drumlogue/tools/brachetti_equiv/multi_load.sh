#!/usr/bin/env bash
# multi_load.sh [<unit-dir> ...]   (default: the eight family units)
#
# Do the units stay independent when the drumlogue loads several at once?
# Builds each unit as a host shared library, then prints every display string
# (every "strings" parameter at every value, for every preset) twice: with
# each unit loaded ALONE, and with ALL of them loaded into one process, each
# with RTLD_LOCAL as a plugin host loads them.  Any difference is state one
# unit shares with another — typically a static local of an inline function
# or a template, which GCC makes STB_GNU_UNIQUE and the dynamic loader keeps
# ONE copy of across libraries.  That is how every family unit once showed
# the first-loaded unit's Program names (BrcKernel read "Marmba", "TblrBel").
# arm_size.sh finds the same symbols statically; this shows the effect.
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
drum="$(cd "$here/../.." && pwd)"
if [ $# -eq 0 ]; then
    set -- $(for u in kick tom kernel snare bar plate cymbal noise; do echo "$drum/brachetti_$u"; done)
fi
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
libs=()
for unit in "$@"; do
    dir="$(cd "$unit" && pwd)"
    name="$(basename "$dir")"
    gcc -c -fPIC -O1 -w -I"$dir" -I"$drum/common" "$dir/header.c" -o "$work/$name.h.o"
    g++ -c -fPIC -O1 -std=gnu++14 -w -I"$dir" -I"$drum/common" "$dir/unit.cc" -o "$work/$name.u.o"
    g++ -shared "$work/$name.u.o" "$work/$name.h.o" -o "$work/$name.so"
    libs+=("$work/$name.so")
done
g++ -O1 -std=gnu++14 -w -I"$drum/common" "$here/multi_load.cpp" -o "$work/multi_load" -ldl
for lib in "${libs[@]}"; do "$work/multi_load" "$lib"; done > "$work/alone.txt"
"$work/multi_load" "${libs[@]}" > "$work/together.txt"
lines=$(wc -l < "$work/alone.txt")
if cmp -s "$work/alone.txt" "$work/together.txt"; then
    echo "independent: ${#libs[@]} units loaded together show the same $lines display strings as alone"
else
    echo "SHARED STATE: loaded together, these strings differ from the unit loaded alone (alone < > together):"
    diff "$work/alone.txt" "$work/together.txt" | head -20
    exit 1
fi
