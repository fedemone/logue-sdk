#!/usr/bin/env bash
# arm_cpu.sh <unit-dir> [frames]
#
# Render cost of a Brachetti unit on the Cortex-A7 instruction set, as a
# number that can be compared before and after a change.  Cross-builds
# equiv_driver.cpp with the unit's own flags (-Os -ffast-math, NEON-VFPv4),
# runs a fixed bench under qemu-arm and counts every ARM instruction executed
# (qemu's exec trace, one instruction per block).  The same script is run a
# second time with its render commands removed, so start-up, Init and preset
# loading cancel out and what is left is the cost of rendering.
#
# The bench: every preset of the unit, struck as a four-note stack (the voice
# cap), then [frames] frames rendered (default 1200 = 25 ms, the densest part
# of a strike), then the same again with no voice left (the idle path).
#
# Prints instructions per rendered frame.  qemu counts instructions, not
# cycles, so this is a regression check, not a CPU-load measurement: compare
# a unit with itself, never two units with each other.
# Needs: apt-get install g++-arm-linux-gnueabihf qemu-user
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
drum="$(cd "$here/../.." && pwd)"
unit="$(cd "$1" && pwd)"
frames="${2:-1200}"
name="$(basename "$unit")"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
ARCH="-march=armv7-a -mtune=cortex-a7 -marm -mfloat-abi=hard -mfpu=neon-vfpv4"
# The SDK Makefile's optimisation and inlining flags (as arm_size.sh): with
# -Os alone GCC keeps small helpers called from several places out of line,
# which a unit built by the SDK never does.
OPT="-Os -pipe -ffast-math -fsigned-char -fno-stack-protector -fstrict-aliasing -falign-functions=16 \
     -fno-math-errno -fomit-frame-pointer -finline-limit=2000000000 \
     --param max-inline-insns-single=2000000000"
arm-linux-gnueabihf-g++ -static -std=gnu++17 $ARCH $OPT -w -I"$unit" -I"$unit/.." \
    -I"$drum/common" -DRUNTIME_COMMON_H_ "$here/equiv_driver.cpp" -o "$work/drv"
qemu-arm "$work/drv" --list > "$work/list"
python3 - "$work/list" "$frames" > "$work/bench.script" <<'EOF'
import sys
presets = [l.split(None, 2)[2].strip() for l in open(sys.argv[1]) if l.startswith("preset ")]
frames = int(sys.argv[2])
for p in presets:
    print("scenario bench")
    print("preset %s" % p)
    for off, vel in ((0, 100), (3, 90), (7, 80), (12, 110)):
        print("note_rel %d %d" % (off, vel))
    print("render_only %d" % frames)
    print("all_off")
    print("reset")
    print("render_only %d" % frames)
EOF
grep -v '^render_only' "$work/bench.script" > "$work/setup.script"
count() {
    qemu-arm -one-insn-per-tb -d exec,nochain -D /dev/stdout "$work/drv" "$1" |
        grep -c '^Trace'
}
total=$(count "$work/bench.script")
setup=$(count "$work/setup.script")
renders=$(grep -c '^render_only' "$work/bench.script")
rendered=$(( renders * ( (frames + 63) / 64 * 64 ) ))
presets=$(( renders / 2 ))
awk -v n="$name" -v t="$total" -v s="$setup" -v f="$rendered" -v p="$presets" \
    'BEGIN { printf "%s: %.1f ARM instructions per rendered frame (%d presets, %d frames)\n", n, (t - s) / f, p, f }'
