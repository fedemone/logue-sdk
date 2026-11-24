#!/usr/bin/env bash
# cpu_cycles.sh <famA> <progA> <famB> <progB> <famC> <progC> [gap] [frames] [settle]
#
# Estimated Cortex-A7 cycles per rendered frame of Braquetti playing: each
# lane on its family and instrument on all 16 steps (family -1: lane empty),
# a gate every <gap> frames (default 3000: 16ths at 240 BPM).  The method and
# the numbers are tools/brachetti_equiv/unit_cycles.sh's (the unit built with
# the SDK's flags, traced under qemu-arm, each instruction weighted as there,
# a run without the measured part subtracted), so they compare with
# Brachetti's: BrcKernel's Timpani struck twice (~2,080) runs on the
# drumlogue; Brachetti's Snare AcSnre x4 is ~1,850; the audio stopped at
# about twice one heavy instrument.  A model, not the device's clock.
#
#   ./cpu_cycles.sh 0 0 3 0 7 2          # Kick2, AcSnre, HHat-C on every step
#   ./cpu_cycles.sh 2 1,1,1,1,0,0,2,0,0,0,0,0,0,0,0,0 -1 0 -1 0 6000 24000 0
#                                        # a pattern (step codes), steps 1-4 measured
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
arm-linux-gnueabihf-objdump -d --no-show-raw-insn "$w/bench" |
    awk -F'\t' '/^ *[0-9a-f]+:\t/ { a = $1; sub(/:.*/, "", a); gsub(/ /, "", a); print a, $2 }' > "$w/map"
trace() {
    qemu-arm -one-insn-per-tb -d exec,nochain -D /dev/stdout "$w/bench" "$@" |
        awk '/^Trace/ { split($0, f, "["); split(f[2], g, "/"); n[g[2]]++ } END { for (k in n) print k, n[k] }'
}
args=("$1" "$2" "$3" "$4" "$5" "$6" "$gap" "$frames" "$settle")
trace render "${args[@]}" > "$w/b.pcs"
trace setup  "${args[@]}" > "$w/s.pcs"
python3 - "$w/map" "$w/b.pcs" "$w/s.pcs" "$frames" "braquetti A $1/$2 B $3/$4 C $5/$6, a gate every $gap frames" <<'PY'
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
