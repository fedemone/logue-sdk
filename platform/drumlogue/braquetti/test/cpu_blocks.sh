#!/usr/bin/env bash
# cpu_blocks.sh <famA> <progA> <famB> <progB> <famC> <progC> [gap] [frames] [settle]
#
# The heaviest audio blocks of Braquetti playing a pattern: the arguments are
# cpu_cycles.sh's (a <prog> may be 16 comma-separated step codes).  Where
# cpu_cycles.sh gives the average cost per frame, this gives each 64-frame
# block's, because the drumlogue must finish every block in time: an
# average can hide one block that does not.  Instructions per frame of each
# block, counted under qemu-arm between successive entries of unit_render
# (the block's render, and the gate callbacks just before it); deterministic,
# unweighted (cpu_cycles.sh's cycle weights move the totals by a few %).
#
#   ./cpu_blocks.sh 2 1,1,1,1,0,0,2,0,0,0,0,0,0,0,0,0 7 0,0,1,1,0,3,3,0,2,0,0,5,0,0,0,0 \
#                   9 0,1,0,1,0,0,0,0,0,1,1,0,1,0,1,1 6000 96000 0
# Needs: apt-get install g++-arm-linux-gnueabihf qemu-user
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
unit="$(cd "$here/.." && pwd)"
drum="$(cd "$unit/.." && pwd)"
gap="${7:-3000}"; frames="${8:-12000}"; settle="${9:-4}"
w="$(mktemp -d)"; trap 'rm -rf "$w"' EXIT
ARCH="-march=armv7-a -mtune=cortex-a7 -marm -mfloat-abi=hard -mfpu=neon-vfpv4"
OPT="-Os -pipe -ffast-math -ffp-contract=off -fsigned-char -fno-stack-protector -fstrict-aliasing \
     -falign-functions=16 -fno-math-errno -fomit-frame-pointer -finline-limit=2000000000 \
     --param max-inline-insns-single=2000000000 -ffunction-sections -fdata-sections"
for c in header family_headers; do
    arm-linux-gnueabihf-gcc -c $ARCH $OPT -std=c11 -w -I"$drum/common" "$unit/$c.c" -o "$w/$c.o"
done
arm-linux-gnueabihf-g++ -c $ARCH $OPT -std=gnu++14 -fno-threadsafe-statics -fno-exceptions -fno-rtti -w \
    -I"$drum/common" "$unit/unit.cc" -o "$w/unit.o"
arm-linux-gnueabihf-g++ -c $ARCH -O2 -std=gnu++14 -w -I"$drum/common" "$here/cpu_bench.cpp" -o "$w/bench.o"
arm-linux-gnueabihf-g++ -static $ARCH -Wl,--gc-sections "$w/bench.o" "$w/unit.o" "$w/header.o" \
    "$w/family_headers.o" -lm -o "$w/bench"
entry="$(arm-linux-gnueabihf-nm "$w/bench" | awk '$3 == "unit_render" { printf "%016s", $1 }' | tr ' ' 0)"
qemu-arm -one-insn-per-tb -d exec,nochain -D /dev/stdout "$w/bench" render "$1" "$2" "$3" "$4" "$5" "$6" \
        "$gap" "$frames" "$settle" |
    awk -v entry="$entry" '/^Trace/ { split($0, f, "["); split(f[2], g, "/");
            if (g[2] == entry) { if (b > 0) print b - 1, n; ++b; n = 0 } ++n }' > "$w/blocks"
python3 - "$w/blocks" "$gap" "$settle" "$frames" "$1/$2 $3/$4 $5/$6" <<'PY'
import sys
rows = [tuple(map(int, l.split())) for l in open(sys.argv[1])]
gap, settle, frames = int(sys.argv[2]), int(sys.argv[3]), int(sys.argv[4])
first = settle * gap // 64            # the measured part starts after the settling
blocks = [(i, n / 64.0) for i, n in rows if i >= first]
if not blocks: sys.exit("no blocks")
mean = sum(c for _, c in blocks) / len(blocks)
step = lambda i: (i * 64 // gap) % 16 + 1 if gap > 0 else 1
top = sorted(blocks, key=lambda b: -b[1])[:6]
print("braquetti %s: %d blocks, mean %.0f, heaviest %.0f instructions per frame (x%.1f the mean)" %
      (sys.argv[5], len(blocks), mean, top[0][1], top[0][1] / mean))
print("  heaviest blocks: " + ", ".join("step %d %.0f" % (step(i), c) for i, c in top))
per = {}
for i, c in blocks: per[step(i)] = max(per.get(step(i), 0), c)
print("  heaviest block per step: " + " ".join("%d:%.0f" % (k, per[k]) for k in sorted(per)))
PY
