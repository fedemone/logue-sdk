#!/bin/bash

# ============================================================================
# Phase 5 Benchmarks - Percussion Spatializer
# ============================================================================

set -e

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

BUILD_DIR="./build"
mkdir -p "$BUILD_DIR"

print_header() {
    echo -e "\n${BLUE}========================================${NC}"
    echo -e "${BLUE}$1${NC}"
    echo -e "${BLUE}========================================${NC}"
}

# The benchmark reads the ARM cycle counter and uses NEON intrinsics, so it
# only builds for the drumlogue's armv7-a target — not for the build host.
CXX=${CXX:-arm-linux-gnueabihf-g++}

if ! command -v "$CXX" >/dev/null 2>&1; then
    echo -e "${YELLOW}$CXX not found — set CXX to an armv7-a C++ cross compiler${NC}"
    exit 1
fi

"$CXX" -std=gnu++17 -O3 -march=armv7-a -mtune=cortex-a7 -marm \
    -mfloat-abi=hard -mfpu=neon-vfpv4 \
    -I. -I../common \
    -o "$BUILD_DIR/bench_delay" benchmark_delay.cpp PercussionSpatializer.cc header.c

print_header "Built $BUILD_DIR/bench_delay"
echo -e "${YELLOW}Run it on the target (the cycle counter is unavailable on the host).${NC}"

if [ "$(uname -m)" = "armv7l" ]; then
    print_header "Running Delay Benchmarks"
    "$BUILD_DIR/bench_delay"
fi
