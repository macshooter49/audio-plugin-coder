#!/bin/bash
# ── FIXES.md §0 — run the cert against six deliberately broken engines. ──
# Each -D deletes ONE mechanism a gate claims to protect. Every one of them must turn the
# cert RED, and MUTATION.md records which gate fired and with what numbers.
set -u
TI="$(cd "$(dirname "$0")/../../.." && pwd)"
D="$TI/Design/fx4/eq"
OUT="${1:-/tmp/eqmut}"
mkdir -p "$OUT"
build () { clang++ -O2 -std=c++17 ${2:-} -I "$TI/Tests/shim" -I "$TI/Source" -I "$D" \
             "$D/eq_cert.cpp" -o "$OUT/$1" || { echo "BUILD FAILED $1"; exit 1; }; }
run   () { "$OUT/$1" > "$OUT/$1.log" 2>&1; echo "$1  exit=$? $(grep -o 'RESULT: .*' "$OUT/$1.log")"; }
build baseline ""
run   baseline
for M in EQ_MUT_NO_PIVOT EQ_MUT_NO_RINGCAP EQ_MUT_NO_SMOOTH EQ_MUT_NO_DIP EQ_MUT_NO_CEILING EQ_MUT_NO_DENORM; do
    build "$M" "-D$M"
    run   "$M"
done
