#!/usr/bin/env bash
# unit_cycles.sh <unit-dir> <family-preset|-1> <program> <notes> [frames] [program2]
#
# Estimated Cortex-A7 cycles per rendered frame of a whole UNIT (Brachetti or
# a family unit, through its drumlogue callbacks: RingOver, knob steps and
# all), for one instrument struck as <notes> stacked notes.  With program2 it
# measures a ring-over: Program changed while those notes ring, and <notes>
# more strikes of the new instrument.  The unit is cross-built with the SDK's
# flags and traced under qemu-arm; each instruction is weighted as in
# arm_cycles.sh (VDIV/VSQRT.F32 14 cycles, ...), and a run without the
# measured render is subtracted.  A model, not the device's clock — compare
# units and cases with each other.  For scale: BrcKernel's Timpani struck
# twice (~2080) is known to run on the drumlogue; the original unit stopped the
# audio interface at about twice a single heavy voice's level.
#
#   ./unit_cycles.sh ../../brachetti 3 0 4            # Brachetti, Snare, AcSnre x4
#   ./unit_cycles.sh ../../brachetti_split/brachetti_snare -1 0 4 4800 1   # ring-over
# Needs: apt-get install g++-arm-linux-gnueabihf qemu-user
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
drum="$(cd "$here/../.." && pwd)"
unit="$(cd "$1" && pwd)"; fam="$2"; prog="$3"; notes="$4"; frames="${5:-4800}"; prog2="${6:-}"
w="$(mktemp -d)"; trap 'rm -rf "$w"' EXIT
ARCH="-march=armv7-a -mtune=cortex-a7 -marm -mfloat-abi=hard -mfpu=neon-vfpv4"
OPT="-Os -pipe -ffast-math -ffp-contract=off -fsigned-char -fno-stack-protector -fstrict-aliasing \
     -falign-functions=16 -fno-math-errno -fomit-frame-pointer -finline-limit=2000000000 \
     --param max-inline-insns-single=2000000000 -ffunction-sections -fdata-sections"
objs=()
for c in "$unit"/*.c; do
    o="$w/$(basename "$c" .c).o"
    arm-linux-gnueabihf-gcc -c $ARCH $OPT -std=c11 -w -I"$unit" -I"$drum/common" "$c" -o "$o"
    objs+=("$o")
done
arm-linux-gnueabihf-g++ -c $ARCH $OPT -std=gnu++14 -fno-threadsafe-statics -fno-exceptions -fno-rtti -w \
    -I"$unit" -I"$drum/common" "$unit/unit.cc" -o "$w/unit.o"
arm-linux-gnueabihf-g++ -c $ARCH -O2 -std=gnu++14 -w -I"$drum/common" "$here/unit_bench.cpp" -o "$w/bench.o"
arm-linux-gnueabihf-g++ -static $ARCH -Wl,--gc-sections "$w/bench.o" "$w/unit.o" "${objs[@]}" -lm -o "$w/bench"
arm-linux-gnueabihf-objdump -d --no-show-raw-insn "$w/bench" |
    awk -F'\t' '/^ *[0-9a-f]+:\t/ { a = $1; sub(/:.*/, "", a); gsub(/ /, "", a); print a, $2 }' > "$w/map"
trace() {
    qemu-arm -one-insn-per-tb -d exec,nochain -D /dev/stdout "$w/bench" "$@" |
        awk '/^Trace/ { split($0, f, "["); split(f[2], g, "/"); n[g[2]]++ } END { for (k in n) print k, n[k] }'
}
trace render "$fam" "$prog" "$notes" "$frames" $prog2 > "$w/b.pcs"
trace setup  "$fam" "$prog" "$notes" "$frames" $prog2 > "$w/s.pcs"
python3 - "$w/map" "$w/b.pcs" "$w/s.pcs" "$frames" "$(basename "$unit") preset $fam program $prog x$notes${prog2:+ -> program $prog2 x$notes}" <<'PY'
import sys
mnem = {}
for line in open(sys.argv[1]):
    p = line.split(None, 1)
    if len(p) == 2: mnem[int(p[0], 16)] = p[1].split()[0]
load = lambda f: {int(a, 16): int(n) for a, n in (l.split() for l in open(f))}
b, s = load(sys.argv[2]), load(sys.argv[3])
def w(m):
    if m.startswith(("vdiv.f32", "vsqrt.f32")): return 14
    if m.startswith(("vdiv.f64", "vsqrt.f64")): return 28
    if m.startswith(("sdiv", "udiv")): return 6
    return 1
ins = cyc = 0
for a, n in b.items():
    n -= s.get(a, 0)
    if n <= 0: continue
    m = mnem.get(a, "?"); ins += n; cyc += n * w(m)
fr = int(sys.argv[4]) // 64 * 64
print("%s: %.0f instructions, ~%.0f A7 cycles per rendered frame" % (sys.argv[5], ins / fr, cyc / fr))
PY
