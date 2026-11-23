#!/usr/bin/env bash
# arm_cycles.sh <unit-dir> <preset-name> <notes> [frames]
#
# An ESTIMATE of a unit's Cortex-A7 cycles per rendered frame for one
# preset, struck as <notes> stacked notes (all at once, velocities 110, 105,
# ...), over [frames] frames (default 19200 = 400 ms: past the strike, into
# the ring).  Cross-builds equiv_driver.cpp as arm_cpu.sh does (the SDK's
# -Os -ffast-math, NEON-VFPv4), traces every executed instruction under
# qemu-arm and weights each by its kind: VDIV/VSQRT.F32 14 cycles (.F64 28),
# SDIV/UDIV 6, everything else 1.  The same script without its render is
# subtracted, so start-up and the strikes' set-up cancel out.
#
# arm_cpu.sh counts instructions, and a division counts as one there; on the
# A7 one costs about fourteen, so a voice with a division or two per sample
# (mymembrane~'s tanh) is far heavier than its instruction count says.  This
# is still a model (no pipeline, no cache): compare presets and units with
# each other, not with the device's clock.  For scale, on this measure:
# BrcCymbal's Gong ×2 ~1810, BrcKernel's Timpni ×2 ~1900, BrcLippa's MmFloor
# ×4 ~1510 (2240 before the CPU cuts in its README).
# Needs: apt-get install g++-arm-linux-gnueabihf qemu-user
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
drum="$(cd "$here/../.." && pwd)"
unit="$(cd "$1" && pwd)"; preset="$2"; notes="$3"; frames="${4:-19200}"
work="$(mktemp -d)"; trap 'rm -rf "$work"' EXIT
ARCH="-march=armv7-a -mtune=cortex-a7 -marm -mfloat-abi=hard -mfpu=neon-vfpv4"
OPT="-Os -pipe -ffast-math -ffp-contract=off -fsigned-char -fno-stack-protector -fstrict-aliasing -falign-functions=16 \
     -fno-math-errno -fomit-frame-pointer -finline-limit=2000000000 \
     --param max-inline-insns-single=2000000000"
arm-linux-gnueabihf-g++ -static -std=gnu++17 $ARCH $OPT -w -I"$unit" -I"$unit/.." \
    -I"$drum/common" -DRUNTIME_COMMON_H_ "$here/equiv_driver.cpp" -o "$work/drv"
poly=""
grep -q '^param [0-9]* Poly ' <(qemu-arm "$work/drv" --list) && poly="param Poly 4"   # the voice cap, not the default 2
{ echo "scenario bench"; [ -n "$poly" ] && echo "$poly"; echo "preset $preset"
  for k in $(seq 0 $((notes - 1))); do echo "note_rel $(( (k * 5) % 13 )) $((110 - k * 5))"; done
  echo "render_only $frames"; } > "$work/bench.script"
grep -v '^render_only' "$work/bench.script" > "$work/setup.script"
arm-linux-gnueabihf-objdump -d --no-show-raw-insn "$work/drv" |
    awk -F'\t' '/^ *[0-9a-f]+:\t/ { a = $1; sub(/:.*/, "", a); gsub(/ /, "", a); print a, $2 }' > "$work/map"
trace() {
    qemu-arm -one-insn-per-tb -d exec,nochain -D /dev/stdout "$work/drv" "$1" |
        awk '/^Trace/ { split($0, f, "["); split(f[2], g, "/"); n[g[2]]++ } END { for (k in n) print k, n[k] }'
}
trace "$work/bench.script" > "$work/bench.pcs"
trace "$work/setup.script" > "$work/setup.pcs"
python3 - "$work/map" "$work/bench.pcs" "$work/setup.pcs" "$frames" "$(basename "$unit") $preset x$notes" <<'PY'
import sys
mnem = {}
for line in open(sys.argv[1]):
    p = line.split(None, 1)
    if len(p) == 2: mnem[int(p[0], 16)] = p[1].split()[0]
def load(f):
    return {int(a, 16): int(n) for a, n in (l.split() for l in open(f))}
bench, setup = load(sys.argv[2]), load(sys.argv[3])
def weight(m):
    if m.startswith(("vdiv.f32", "vsqrt.f32")): return 14
    if m.startswith(("vdiv.f64", "vsqrt.f64")): return 28
    if m.startswith(("sdiv", "udiv")): return 6
    return 1
ins = cyc = divs = 0
for a, n in bench.items():
    n -= setup.get(a, 0)
    if n <= 0: continue
    m = mnem.get(a, "?")
    ins += n; cyc += n * weight(m)
    if m.startswith(("vdiv", "vsqrt")): divs += n
frames = max(64, int(sys.argv[4]) // 64 * 64)
print("%s: %.0f instructions, ~%.0f A7 cycles, %.1f divisions/square roots per rendered frame"
      % (sys.argv[5], ins / frames, cyc / frames, divs / frames))
PY
