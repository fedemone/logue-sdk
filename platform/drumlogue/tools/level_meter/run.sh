#!/usr/bin/env bash
#
# Build a drumlogue user synth unit against the level meter and run it under
# qemu-arm, so unit levels can be measured on the host instead of discovered on
# hardware.
#
#   ./run.sh ../../EffeESP32                    # every preset
#   ./run.sh ../../brachetti 60 127 8           # note, velocity, first 8 presets
#   ./run.sh ../../EffeESP32 60 127 -1 - 0 59   # sweep param 0 in 59 steps
#   ./run.sh ../../EffeMD 60 127 -1 ./wav       # also write one WAV per preset
#
# EXTRA_FLAGS is appended to both compiles, for trying a constant without
# editing the source:
#
#   EXTRA_FLAGS=-DMASTER_GAIN_OVERRIDE=3.16f ./run.sh ../../EffeESP32
#
# Requires an armhf cross toolchain and qemu-user:
#   sudo apt-get install g++-arm-linux-gnueabihf qemu-user
#
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
COMMON="$(cd "$HERE/../../common" && pwd)"

if [ $# -lt 1 ]; then
  sed -n '3,16p' "$0" | sed 's/^# \{0,1\}//'
  exit 1
fi

PROJECT="$(cd "$1" && pwd)"; shift
NAME="$(basename "$PROJECT")"

CXX=${CXX:-arm-linux-gnueabihf-g++}
CC=${CC:-arm-linux-gnueabihf-gcc}
QEMU=${QEMU:-qemu-arm}

for tool in "$CXX" "$CC" "$QEMU"; do
  command -v "$tool" >/dev/null || { echo "missing $tool (apt-get install g++-arm-linux-gnueabihf qemu-user)" >&2; exit 1; }
done

# Same architecture flags the drumlogue Makefile uses, minus -ffast-math: the
# unit must be built the way it ships, but the meter's own arithmetic has to
# keep working NaN/Inf checks.
ARCH_FLAGS="-march=armv7-a -mtune=cortex-a7 -marm -mfloat-abi=hard -mfpu=neon-vfpv4"
FLAGS="-O2 $ARCH_FLAGS -fno-math-errno -Wno-psabi -D__ARM_NEON__ -I$COMMON -I$PROJECT ${EXTRA_FLAGS:-}"

CSRC=$(make -s -f "$HERE/sources.mk" PROJECT="$PROJECT" print-csrc)
CXXSRC=$(make -s -f "$HERE/sources.mk" PROJECT="$PROJECT" print-cxxsrc)
UDEFS=$(make -s -f "$HERE/sources.mk" PROJECT="$PROJECT" print-udefs)

BUILD="$HERE/build/$NAME"
mkdir -p "$BUILD"

OBJS=()
for f in $CSRC; do
  o="$BUILD/$(basename "${f%.c}").c.o"
  "$CC" $FLAGS $UDEFS -c -o "$o" "$PROJECT/$f"
  OBJS+=("$o")
done

"$CXX" $FLAGS $UDEFS -static -o "$BUILD/meter" "$HERE/measure.cpp" \
  $(for f in $CXXSRC; do echo "$PROJECT/$f"; done) "${OBJS[@]}" -lm

exec "$QEMU" "$BUILD/meter" "$@"
