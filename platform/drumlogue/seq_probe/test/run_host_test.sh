#!/usr/bin/env bash
# Builds the probe with the host test and runs it.
#
#   ./run_host_test.sh               host g++, address + undefined sanitizers
#   NO_THREAD=1 ./run_host_test.sh   as if the firmware refused a thread
#   ARM=1 ./run_host_test.sh         arm-linux-gnueabihf-g++ with the SDK's
#                                    -march/-mfpu flags, run under qemu-arm
#
# Not a device build: the .drmlgunit comes from the SDK Docker image
# (`build drumlogue/seq_probe`), whose toolchain matches the drumlogue's C
# library.
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
unit="$here/.."
common="$unit/../common"
out="${OUT:-$(mktemp -d)}"
mkdir -p "$out/userfs"

defs=(-DPROBE_DIR="\"$out/userfs\"" -DPROBE_STATS_SECONDS=1)
[ -n "${NO_THREAD:-}" ] && defs+=(-DPROBE_FORCE_NO_THREAD)

if [ -n "${ARM:-}" ]; then
    cxx=arm-linux-gnueabihf-g++; cc=arm-linux-gnueabihf-gcc
    arch=(-march=armv7-a -mtune=cortex-a7 -marm -mfloat-abi=hard -mfpu=neon-vfpv4)
    opt=(-Os -ffast-math -fno-math-errno)
    cxxopt=(-fno-threadsafe-statics)
    run=(qemu-arm -L /usr/arm-linux-gnueabihf)
else
    cxx=g++; cc=gcc
    arch=()
    opt=(-O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined)
    cxxopt=()
    run=()
fi

warn=(-W -Wall -Wextra -Wno-ignored-qualifiers -Werror)
"$cc"  "${arch[@]}" "${opt[@]}" -std=c11 -fPIC -I"$common" -c "$unit/header.c" -o "$out/header.o"
"$cc"  "${arch[@]}" "${opt[@]}" -std=c11 -fPIC -I"$common" -c "$common/_unit_base.c" -o "$out/base.o"
"$cxx" "${arch[@]}" "${opt[@]}" "${cxxopt[@]}" -std=gnu++14 "${warn[@]}" -fPIC -I"$common" "${defs[@]}" \
    -c "$unit/unit.cc" -o "$out/unit.o"
"$cxx" "${arch[@]}" "${opt[@]}" "${cxxopt[@]}" -std=gnu++14 "${warn[@]}" -I"$common" "${defs[@]}" \
    "$here/host_test.cc" "$out/unit.o" "$out/header.o" "$out/base.o" -lm -lpthread -o "$out/host_test"

"${run[@]}" "$out/host_test"
echo "log: $out/userfs/seqprobe.log"
