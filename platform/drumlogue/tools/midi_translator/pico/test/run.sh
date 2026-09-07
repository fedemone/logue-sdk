#!/usr/bin/env bash
# Build and run the host unit tests for the portable core (no Pico needed).
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
root="$(dirname "$here")"
cc -std=c11 -Wall -Wextra -O2 \
   -o "$here/test_core" \
   "$here/test_core.c" "$root/translator.c" "$root/midi_parse.c" "$root/config.c" \
   -I"$root"
"$here/test_core"
