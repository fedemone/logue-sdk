#!/usr/bin/env bash
# ring_budget_check.sh — the CPU budget across a ring-over, on every family
# unit and on Brachetti's family switch (ring_budget_check.cpp says what is
# checked).  Exit 0 = within budget everywhere.
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
drum="$(cd "$here/../.." && pwd)"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
rc=0
for u in kick tom kernel snare bar plate cymbal noise world lippa pizzico cordiera; do
    dir="$drum/brachetti_split/brachetti_$u"
    g++ -std=c++17 -O2 -w -I"$dir" -I"$dir/.." -I"$drum/common" -DRUNTIME_COMMON_H_ \
        "$here/ring_budget_check.cpp" -o "$work/rb_$u"
    "$work/rb_$u" || rc=1
done
b="$drum/brachetti"
gcc -std=c11 -O2 -w -I"$drum/common" -c "$b/header.c" -o "$work/h.o"
gcc -std=c11 -O2 -w -I"$drum/common" -c "$b/family_headers.c" -o "$work/fh.o"
g++ -std=c++17 -O2 -w -I"$b" -I"$drum/common" -DBRACHETTI_ALL -c "$here/ring_budget_check.cpp" -o "$work/rb.o"
g++ "$work/rb.o" "$work/h.o" "$work/fh.o" -o "$work/rb_brachetti"
"$work/rb_brachetti" || rc=1
exit $rc
