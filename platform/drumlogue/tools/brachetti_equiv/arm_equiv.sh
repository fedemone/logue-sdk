#!/usr/bin/env bash
# arm_equiv.sh <family-unit-dir> [gen_scenarios.py options...]
#
# The NEON half of run_equiv.sh.  The host build never compiles the
# `__ARM_NEON` paths (the modal bank, the cymbal bank, the Timpani/Taiko
# kernel), so this cross-builds equiv_driver.cpp for the Cortex-A7 with the
# unit's own flags — -Os -ffast-math, NEON-VFPv4 — for both the original and
# the family unit, runs the same script under qemu-arm and compares the
# rendered samples.  Under -ffast-math the compiler may fuse and reorder float
# operations differently around different code, so the two builds are not
# expected to be bit-identical; what is checked is that they differ only at
# float-rounding level.  Needs: apt-get install g++-arm-linux-gnueabihf qemu-user
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
drum="$(cd "$here/../.." && pwd)"
family="$(cd "$1" && pwd)"; shift
name="$(basename "$family")"
work="${EQUIV_WORK:-$(mktemp -d)}"
mkdir -p "$work"
ARCH="-march=armv7-a -mtune=cortex-a7 -marm -mfloat-abi=hard -mfpu=neon-vfpv4"
OPT="-Os -ffast-math -fsigned-char -fno-stack-protector -fstrict-aliasing -fno-math-errno -fomit-frame-pointer"
build() {
    arm-linux-gnueabihf-g++ -static -std=gnu++17 $ARCH $OPT -w -I"$1" -I"$1/.." \
        -I"$drum/common" -DRUNTIME_COMMON_H_ "$here/equiv_driver.cpp" -o "$2"
}
build "$drum/brachetti" "$work/arm_orig"
build "$family" "$work/arm_$name"
qemu-arm "$work/arm_$name" --list > "$work/$name.arm.list"
extra=()
case "$name" in
    brachetti_kick)  extra=(--avoid-reselect Kick) ;;
    brachetti_noise) extra=(--avoid-reselect Shaker) ;;
esac
python3 "$here/gen_scenarios.py" "$work/$name.arm.list" "${extra[@]}" "$@" > "$work/$name.arm.script"
qemu-arm "$work/arm_orig"  "$work/$name.arm.script" "$work/orig.f32"   > "$work/orig.arm.out"
qemu-arm "$work/arm_$name" "$work/$name.arm.script" "$work/family.f32" > "$work/family.arm.out"
printf '%s (ARM, -ffast-math): ' "$name"
python3 "$here/compare_dumps.py" "$work/$name.arm.script" "$work/orig.arm.out" "$work/orig.f32" "$work/family.arm.out" "$work/family.f32"
# The dumps run to hundreds of MB; keep them only when asked.
[ -n "${KEEP_DUMPS:-}" ] || rm -f "$work/orig.f32" "$work/family.f32"
