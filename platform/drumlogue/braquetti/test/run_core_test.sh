#!/usr/bin/env bash
# Builds and runs the sequencer core's host test.
#
#   ./run_core_test.sh          host g++, address + undefined sanitizers
#   ARM=1 ./run_core_test.sh    arm-linux-gnueabihf-g++ with the SDK's
#                               -march/-mfpu flags, run under qemu-arm
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
out="${OUT:-$(mktemp -d)}"
warn=(-std=gnu++14 -W -Wall -Wextra -Werror)
if [ -n "${ARM:-}" ]; then
    arm-linux-gnueabihf-g++ -march=armv7-a -mtune=cortex-a7 -marm -mfloat-abi=hard -mfpu=neon-vfpv4 \
        -Os -ffast-math -fno-threadsafe-statics "${warn[@]}" "$here/core_test.cc" -lpthread -o "$out/core_test"
    qemu-arm -L /usr/arm-linux-gnueabihf "$out/core_test"
else
    g++ -O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined "${warn[@]}" \
        "$here/core_test.cc" -lpthread -o "$out/core_test"
    "$out/core_test"
fi
