#!/usr/bin/env bash
#
# Build a drumlogue user synth unit against the polyphony meter and run it under
# qemu-arm, so what a unit does with several voices at once can be measured on
# the host instead of discovered on hardware.
#
#   ./run.sh ../../EffeESP32 poly 14 8            # 1..8 stacked voices, instr 14
#   ./run.sh ../../EffeESP32 roll 14 6 250        # 6 hits on one note, 250 ms apart
#   ./run.sh ../../EffeESP32 dump 14 4 0 127 2 /tmp/a.f32
#
# Argument order after the project directory is:
#   <mode> <instrument> <n> <gap-ms> <velocity> <seconds> [dump-path]
#
# SEL_PARAM picks the parameter that selects the sound (default 0, "Instr" on
# EffeESP32 and EffeMD); -1 leaves every parameter at its header default.
#
# EXTRA_FLAGS is appended to both compiles, for trying a constant without
# editing the source:
#
#   EXTRA_FLAGS=-DLIMIT_CEILING_OVERRIDE=0.8f ./run.sh ../../EffeESP32 poly 14 8
#
# To get the absolute distortion of an output stage rather than how it changes
# with voice count, render the same thing twice -- once normally, once with the
# master gain turned down far enough that the stage is linear -- and compare:
#
#   ./run.sh ../../EffeESP32 dump 14 4 0 127 2 /tmp/proc.f32
#   EXTRA_FLAGS=-DMASTER_GAIN_OVERRIDE=0.01f \
#     ./run.sh ../../EffeESP32 dump 14 4 0 127 2 /tmp/lin.f32
#   ./compare.py /tmp/proc.f32 /tmp/lin.f32 251 32
#
# (251 = 2.51/0.01, the gain the reference was rendered at; 32 = the look-ahead
# delay in frames that the processed build adds, 0 if it has none.)
#
# Requires an armhf cross toolchain and qemu-user:
#   sudo apt-get install g++-arm-linux-gnueabihf qemu-user
#
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
COMMON="$(cd "$HERE/../../common" && pwd)"
SOURCES="$HERE/../level_meter/sources.mk"

if [ $# -lt 1 ]; then
  sed -n '3,30p' "$0" | sed 's/^# \{0,1\}//'
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

CSRC=$(make -s -f "$SOURCES" PROJECT="$PROJECT" print-csrc)
CXXSRC=$(make -s -f "$SOURCES" PROJECT="$PROJECT" print-cxxsrc)
UDEFS=$(make -s -f "$SOURCES" PROJECT="$PROJECT" print-udefs)

BUILD="$HERE/build/$NAME"
mkdir -p "$BUILD"

OBJS=()
for f in $CSRC; do
  o="$BUILD/$(basename "${f%.c}").c.o"
  "$CC" $FLAGS $UDEFS -c -o "$o" "$PROJECT/$f"
  OBJS+=("$o")
done

"$CXX" $FLAGS $UDEFS -static -o "$BUILD/stack" "$HERE/measure.cpp" \
  $(for f in $CXXSRC; do echo "$PROJECT/$f"; done) "${OBJS[@]}" -lm

exec "$QEMU" "$BUILD/stack" "$@"
