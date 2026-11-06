#!/usr/bin/env bash
# run_equiv.sh <family-unit-dir> [gen_scenarios.py options...]
#
# Proves a Brachetti family unit renders bit-identically to the original
# `brachetti` on every preset it kept.  Builds equiv_driver.cpp against both
# units, writes a script from the FAMILY unit's own preset/parameter listing
# (so it only uses what the family exposes, by name), plays it through both and
# diffs the per-render hashes.  Exit 0 = identical on every render.
#
#   ./run_equiv.sh ../../brachetti_kick
#   ./run_equiv.sh ../../brachetti_kick --fuzz 400 --seed 7
#
# Host build, no -ffast-math, so both sides evaluate the same float expressions
# in the same order; any difference at all in the output is a real one.
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
drum="$(cd "$here/../.." && pwd)"
family="$(cd "$1" && pwd)"; shift
orig="$drum/brachetti"
work="${EQUIV_WORK:-$(mktemp -d)}"
mkdir -p "$work"
name="$(basename "$family")"

build() {  # <unit-dir> <out>
    g++ -std=c++17 -O2 -I"$1" -I"$1/.." -I"$drum/common" -DRUNTIME_COMMON_H_ \
        -w "$here/equiv_driver.cpp" -o "$2"
}
build "$orig" "$work/drv_orig"
build "$family" "$work/drv_$name"

"$work/drv_$name" --list > "$work/$name.list"
# Known, deliberate deviations (README.md here, "Known differences"): each is
# reachable only by re-selecting the preset that is already selected while one
# of its voices still rings, so the script simply never does that for them.
extra=()
case "$name" in
    brachetti_kick)  extra=(--avoid-reselect Kick) ;;
    brachetti_noise) extra=(--avoid-reselect Shaker) ;;
esac
python3 "$here/gen_scenarios.py" "$work/$name.list" "${extra[@]}" "$@" > "$work/$name.script"

"$work/drv_orig"  "$work/$name.script" > "$work/$name.orig.out"
"$work/drv_$name" "$work/$name.script" > "$work/$name.family.out"

renders=$(wc -l < "$work/$name.orig.out")
scen=$(grep -c '^scenario' "$work/$name.script")
silent=$(awk '$4+0 == 0' "$work/$name.orig.out" | wc -l)
if cmp -s "$work/$name.orig.out" "$work/$name.family.out"; then
    echo "$name: IDENTICAL — $scen scenarios, $renders renders ($silent silent spans), work dir $work"
else
    echo "$name: DIFFERENT — first differing renders:"
    diff "$work/$name.orig.out" "$work/$name.family.out" | head -20
    echo "work dir $work"
    exit 1
fi
