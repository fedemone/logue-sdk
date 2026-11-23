#!/usr/bin/env bash
# Builds and runs Braquetti's host tests:
#   core_test     the sequencer core, on a small catalog of its own (fast)
#   catalog_test  the catalog read from Brachetti's family table, with
#                 Brachetti's engines compiled in
#   unit_test     the whole unit through its drumlogue callbacks, against
#                 Brachetti's engines as the reference
#
#   ./run_tests.sh          host g++, address + undefined sanitizers
#   ARM=1 ./run_tests.sh    arm-linux-gnueabihf-g++ with the SDK's
#                           -march/-mfpu flags, run under qemu-arm
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
drum="$(cd "$here/../.." && pwd)"
out="${OUT:-$(mktemp -d)}"
warn=(-std=gnu++14 -W -Wall -Wextra -Werror)
if [ -n "${ARM:-}" ]; then
    cxx=arm-linux-gnueabihf-g++; cc=arm-linux-gnueabihf-gcc
    flags=(-march=armv7-a -mtune=cortex-a7 -marm -mfloat-abi=hard -mfpu=neon-vfpv4 -Os -ffast-math -ffp-contract=off)
    cxxflags=(-fno-threadsafe-statics)
    run=(qemu-arm -L /usr/arm-linux-gnueabihf)
    unit_test_defs=(-DBRQ_TOLERANCE_DB=-90)   # -ffast-math: see check_scaled in unit_test.cc
else
    cxx=g++; cc=gcc
    flags=(-O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined)
    cxxflags=()
    run=()
    unit_test_defs=()
fi

"$cxx" "${flags[@]}" "${cxxflags[@]}" "${warn[@]}" "$here/core_test.cc" -lpthread -o "$out/core_test"
"${run[@]}" "$out/core_test"

# Brachetti's engines are not ours to warn about (tools/brachetti_equiv
# builds them with -w too): only a warning located in Braquetti's own files
# fails the build.
"$cc" "${flags[@]}" -std=c11 -w -I"$drum/common" -c "$drum/brachetti/family_headers.c" -o "$out/family_headers.o"
if "$cxx" "${flags[@]}" "${cxxflags[@]}" -std=gnu++14 -fsyntax-only -W -Wall -Wextra -I"$drum/common" \
       "$here/catalog_test.cc" 2>&1 | grep -E '(catalog_test\.cc|seq_core\.h|braquetti/catalog\.h):[0-9]+:[0-9]+: (warning|error)'; then
    echo "warnings in Braquetti's files"; exit 1
fi
"$cxx" "${flags[@]}" "${cxxflags[@]}" -std=gnu++14 -w -I"$drum/common" "$here/catalog_test.cc" \
    "$out/family_headers.o" -o "$out/catalog_test"
"${run[@]}" "$out/catalog_test"

# The unit: unit.cc (built as the drumlogue would, plus a voice-count hook),
# header.c, the family headers, and the test with its own reference engines.
if "$cxx" "${flags[@]}" "${cxxflags[@]}" -std=gnu++14 -fsyntax-only -W -Wall -Wextra -I"$drum/common" \
       -DBRAQUETTI_HOST_TEST "$here/../unit.cc" "$here/unit_test.cc" 2>&1 |
       grep -E '(unit\.cc|unit_test\.cc|seq_core\.h|braquetti/catalog\.h):[0-9]+:[0-9]+: (warning|error)'; then
    echo "warnings in Braquetti's files"; exit 1
fi
"$cc" "${flags[@]}" -std=c11 -W -Wall -Wextra -Werror -I"$drum/common" -c "$here/../header.c" -o "$out/header.o"
"$cxx" "${flags[@]}" "${cxxflags[@]}" -std=gnu++14 -w -I"$drum/common" -DBRAQUETTI_HOST_TEST \
    -c "$here/../unit.cc" -o "$out/unit.o"
"$cxx" "${flags[@]}" "${cxxflags[@]}" "${unit_test_defs[@]}" -std=gnu++14 -w -I"$drum/common" "$here/unit_test.cc" \
    "$out/unit.o" "$out/header.o" "$out/family_headers.o" -lpthread -o "$out/unit_test"
"${run[@]}" "$out/unit_test"
