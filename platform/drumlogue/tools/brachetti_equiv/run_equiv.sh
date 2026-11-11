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
# The work directory is removed after an IDENTICAL run unless KEEP_WORK=1
# (or EQUIV_WORK names it).
#
# The original is read from ../../brachetti, or from BRACHETTI_ORIG when it is
# elsewhere (it is no longer on main: see the message below).  Presets a family
# unit added after the split have no counterpart there; they are left out of
# the script and named on stderr, and every other preset is still compared.
#
# Host build, no -ffast-math, so both sides evaluate the same float expressions
# in the same order; any difference at all in the output is a real one.
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
drum="$(cd "$here/../.." && pwd)"
family="$(cd "$1" && pwd)"; shift
orig="${BRACHETTI_ORIG:-$drum/brachetti}"
if [ ! -f "$orig/synth_engine.h" ]; then
    cat >&2 <<EOF
The original brachetti unit is not at $orig.
It is no longer on main; it lives on the original_brachetti branch:
  git worktree add /tmp/brachetti_orig origin/original_brachetti
  BRACHETTI_ORIG=/tmp/brachetti_orig/platform/drumlogue/brachetti $0 $family
EOF
    exit 2
fi
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
"$work/drv_orig" --list > "$work/orig.list"
# Known, deliberate deviations (README.md here, "Known differences"): each is
# reachable only by re-selecting the preset that is already selected while one
# of its voices still rings, so the script simply never does that for them.
extra=()
case "$name" in
    brachetti_kick)  extra=(--avoid-reselect Kick) ;;
    brachetti_noise) extra=(--avoid-reselect Shaker) ;;
esac
python3 "$here/gen_scenarios.py" "$work/$name.list" --compare-with "$work/orig.list" \
    "${extra[@]}" "$@" > "$work/$name.script"

"$work/drv_orig"  "$work/$name.script" > "$work/$name.orig.out"
"$work/drv_$name" "$work/$name.script" > "$work/$name.family.out"

renders=$(wc -l < "$work/$name.orig.out")
scen=$(grep -c '^scenario' "$work/$name.script")
silent=$(awk '$4+0 == 0' "$work/$name.orig.out" | wc -l)
if cmp -s "$work/$name.orig.out" "$work/$name.family.out"; then
    echo "$name: IDENTICAL — $scen scenarios, $renders renders ($silent silent spans)"
    # Keep the binaries, script and hashes only when asked (or when the caller
    # chose the directory); a difference always keeps them.
    if [ -z "${EQUIV_WORK:-}" ] && [ -z "${KEEP_WORK:-}" ]; then rm -rf "$work"; fi
else
    echo "$name: DIFFERENT — first differing renders:"
    diff "$work/$name.orig.out" "$work/$name.family.out" | head -20
    echo "work dir $work"
    exit 1
fi
