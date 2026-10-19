#!/usr/bin/env bash
#
# Build probe.cc against PortaCassette.h for the drumlogue's Cortex-A7 and run
# measure.py under qemu-arm.
#
#   ./run.sh                    # every measurement
#   ./run.sh onset alias        # just these
#   SRC=/tmp/old ./run.sh calib # measure another copy of the unit's sources
#
# Requires an armhf cross toolchain, qemu-user and numpy:
#   sudo apt-get install g++-arm-linux-gnueabihf qemu-user python3-numpy
#
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SRC="$(cd "${SRC:-$HERE/..}" && pwd)"
COMMON="$(cd "$HERE/../../common" && pwd)"

CXX=${CXX:-arm-linux-gnueabihf-g++}
QEMU=${QEMU:-$(command -v qemu-arm || command -v qemu-arm-static || echo qemu-arm)}
command -v "$CXX" >/dev/null || { echo "missing $CXX (apt-get install g++-arm-linux-gnueabihf)" >&2; exit 1; }
command -v "$QEMU" >/dev/null || { echo "missing qemu-arm (apt-get install qemu-user)" >&2; exit 1; }

# Same architecture flags the drumlogue Makefile uses.
FLAGS="-O2 -march=armv7-a -mtune=cortex-a7 -marm -mfloat-abi=hard -mfpu=neon-vfpv4 -std=gnu++14
       -fno-math-errno -Wno-psabi -D__ARM_NEON__ -DARM_NEON_OPTIMIZATION -I$SRC -I$COMMON"

# A private build directory per run, so concurrent runs cannot overwrite each
# other's probes.
mkdir -p "$HERE/build"
BUILD="$(mktemp -d "$HERE/build/run.XXXXXX")"
trap 'rm -rf "$BUILD"' EXIT
"$CXX" $FLAGS -static -o "$BUILD/probe" "$HERE/probe.cc" -lm
if ! "$CXX" $FLAGS -static -o "$BUILD/probe_nc" "$HERE/probe.cc" -lm \
        -DPC_CEILING_KNEE_OVERRIDE=1000.0f 2>/dev/null; then
  echo "warning: $SRC predates PC_CEILING_KNEE_OVERRIDE; tone tests include the output ceiling" >&2
  cp "$BUILD/probe" "$BUILD/probe_nc"
fi

export QEMU PROBE="$BUILD/probe" PROBE_NC="$BUILD/probe_nc"
python3 "$HERE/measure.py" "$@"
