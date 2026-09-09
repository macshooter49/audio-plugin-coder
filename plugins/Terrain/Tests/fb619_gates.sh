#!/usr/bin/env bash
# ══════════════════════════════════════════════════════════════════════════════════════════════
#  fb619 — PRESET BANKS. The file layer (Source/PresetBank.h) driven standalone against real
#  juce_core on a throwaway root: write · scan · meta rewrite (byte-clean) · move · favourites ·
#  export/import a .terrainpack · rename · delete · no write escapes the user root · zip-slip
#  dropped · caps REPORT. Two controls, each must go RED: BK_MUT=nocap expects a silent
#  truncation; BK_MUT=escape expects a write outside the user root to succeed.
#
#    bash Tests/fb619_gates.sh          # from plugins/Terrain
# ══════════════════════════════════════════════════════════════════════════════════════════════
set -u
OUT="${TI_GATE_OUT:-$(mktemp -d /tmp/fb619.XXXXXX)}"; REPO="$(cd ../.. && pwd)"; mkdir -p "$OUT"; rc_all=0
run () {  # label  mutation-env  command...
  local name="$1" mut="$2"; shift 2
  "$@"            > "$OUT/$name.normal.txt"  2>&1; local a=$?
  env "$mut" "$@" > "$OUT/$name.mutated.txt" 2>&1; local b=$?
  local v="OK"; [ $a -ne 0 ] && v="RED — the gate itself is failing"; [ $b -eq 0 ] && v="BROKEN CONTROL — the mutation did NOT go red"
  printf '  %-22s normal=%d  mutated(%s)=%d   %s\n' "$name" "$a" "$mut" "$b" "$v"
  if [ $a -ne 0 ] || [ $b -eq 0 ]; then rc_all=1; fi; return 0
}
echo "══ fb619 GATE ══"
c++ -std=c++17 -O2 -DNDEBUG=1 -DJUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1 \
    -DJUCE_STANDALONE_APPLICATION=1 -DJUCE_MODULE_AVAILABLE_juce_core=1 \
    -I"$REPO/_tools/JUCE/modules" -I Source \
    Tests/preset_bank_cert.cpp Tests/juce_compdate_stub.cpp "$REPO/_tools/JUCE/modules/juce_core/juce_core.mm" \
    -framework Foundation -framework CoreFoundation -framework Cocoa -framework IOKit -framework Security \
    -o "$OUT/preset_bank_cert" || { echo "  preset_bank_cert: COMPILE FAIL"; exit 1; }
run bank:nocap   BK_MUT=nocap  "$OUT/preset_bank_cert"
run bank:escape  BK_MUT=escape "$OUT/preset_bank_cert"
echo; echo "  full output: $OUT"; exit $rc_all
