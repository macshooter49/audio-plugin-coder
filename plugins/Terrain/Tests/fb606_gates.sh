#!/usr/bin/env bash
# ══════════════════════════════════════════════════════════════════════════════════════════════
#  fb606_gates.sh — the two fb606 gates, each run TWICE: normal, then with its mutation control.
#
#    bash Tests/fb606_gates.sh          # from plugins/Terrain
#
#  A green bar that cannot go red is not a gate, so this runner refuses to call a gate OK unless
#  the mutated run of that same gate FAILED. It prints the pair for both.
#
#  WHAT THEY GATE
#    wt_folder_scan_cert   Max: "I should be able to open the MASTER FOLDER and see all of the
#                          tables in the SUB FOLDERS too." Compiles the SHIPPING scan — sliced out
#                          of Source/PluginProcessor.cpp by extract_imports_scan.py, not copied —
#                          against a real temp tree: depth 0/1/2, an EMPTY folder, and a folder
#                          whose name starts with a literal 0x7F (his PLUTO 2 pack really is).
#    wt_folder_menu_gate   Max: "delete + locate folder is behind the menu AKA impossible to get
#                          to lol." Opens the real browser in headless Chrome, right-clicks a
#                          folder and HIT-TESTS the menu with elementFromPoint. A z-index
#                          comparison is not sufficient and the gate prints why.
#
#  THE SEAM. The cert's `--emit` writes the two payloads the WebView really receives, and the JS
#  gate feeds THOSE to the shipping JS. A C++ shape the JS reader does not accept is green on both
#  sides and broken in the plugin; nothing but this hand-off can see it.
# ══════════════════════════════════════════════════════════════════════════════════════════════
set -u
OUT="${TI_GATE_OUT:-/private/tmp/claude-501/-Users-macshooter/941a8123-ffc6-4f73-84a3-70aee55ea3c3/scratchpad/wtfolders}"
REPO="$(cd ../.. && pwd)"
mkdir -p "$OUT/mut" "$OUT/payload"
rc_all=0

JUCE_FLAGS=(-std=c++17 -O1 -DNDEBUG=1 -DJUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1
            -DJUCE_STANDALONE_APPLICATION=1 -DJUCE_MODULE_AVAILABLE_juce_core=1)
JUCE_SRC=("$REPO/_tools/JUCE/modules/juce_core/juce_core.mm" Tests/juce_compdate_stub.cpp)
JUCE_LIBS=(-framework Foundation -framework CoreFoundation -framework Cocoa -framework IOKit -framework Security)

echo "══ fb606 GATES ══"

# ── 1. slice the shipping scan, twice: as it ships, and with its recursion turned off ─────────
python3 Tests/extract_imports_scan.py Source/PluginProcessor.cpp Tests/ti_imports_scan_extracted.h || rc_all=1
python3 Tests/extract_imports_scan.py Source/PluginProcessor.cpp "$OUT/mut/ti_imports_scan_extracted.h" --mutate || rc_all=1
echo

# ⚠️ THE MUTANT NEEDS -DTI_SLICE_HEADER, NOT -I. A quoted #include searches the including file's
#    OWN directory first, before every -I, so an -I<mutdir> build silently compiles the healthy
#    Tests/ copy and the control reports a gate that cannot go red. (Measured: it passed 11/0.)
c++ "${JUCE_FLAGS[@]}" -I"$REPO/_tools/JUCE/modules" -I Tests \
    Tests/wt_folder_scan_cert.cpp "${JUCE_SRC[@]}" "${JUCE_LIBS[@]}" -o "$OUT/wt_folder_scan_cert" \
    || { echo "  wt_folder_scan_cert: COMPILE FAIL"; rc_all=1; }
c++ "${JUCE_FLAGS[@]}" -DTI_SLICE_HEADER="\"$OUT/mut/ti_imports_scan_extracted.h\"" \
    -I"$REPO/_tools/JUCE/modules" -I Tests \
    Tests/wt_folder_scan_cert.cpp "${JUCE_SRC[@]}" "${JUCE_LIBS[@]}" -o "$OUT/wt_folder_scan_cert_MUT" \
    || { echo "  wt_folder_scan_cert (mutant): COMPILE FAIL"; rc_all=1; }

# ── 2. the payloads the JS gate reads — the REAL C++ output, never a fixture ──────────────────
"$OUT/wt_folder_scan_cert" --emit "$OUT/payload" > "$OUT/emit.txt" 2>&1 \
  || { echo "  --emit FAILED (see $OUT/emit.txt)"; rc_all=1; }

pair () {   # label  normal-cmd…  ::  mutant-cmd…
  local name="$1"; shift
  local -a norm=() mutt=(); local seen=0
  for a in "$@"; do if [ "$a" = "::" ]; then seen=1; continue; fi
    if [ $seen -eq 0 ]; then norm+=("$a"); else mutt+=("$a"); fi; done
  "${norm[@]}" > "$OUT/$name.normal.txt"  2>&1; local a=$?
  "${mutt[@]}" > "$OUT/$name.mutated.txt" 2>&1; local b=$?
  local verdict="OK"
  [ $a -ne 0 ] && verdict="RED — the gate itself is failing"
  [ $b -eq 0 ] && verdict="BROKEN CONTROL — the mutation did NOT go red"
  printf '  %-22s normal=%d  mutated=%d   %s\n' "$name" "$a" "$b" "$verdict"
  printf '      %s\n' "$(grep -E '[0-9]+ pass' "$OUT/$name.normal.txt"  | tail -1)"
  printf '      %s   (mutated)\n' "$(grep -E '[0-9]+ pass' "$OUT/$name.mutated.txt" | tail -1)"
  if [ $a -ne 0 ] || [ $b -eq 0 ]; then rc_all=1; fi
  return 0
}

echo
pair folder_scan_recursion   "$OUT/wt_folder_scan_cert" :: "$OUT/wt_folder_scan_cert_MUT"
pair folder_scan_flat        "$OUT/wt_folder_scan_cert" :: env TI_RECURSE_MUT=flat "$OUT/wt_folder_scan_cert"
pair folder_menu_synmenu     node Tests/wt_folder_menu_gate.js :: env TPBMENU_MUTATE=synmenu node Tests/wt_folder_menu_gate.js
pair folder_menu_noclamp     node Tests/wt_folder_menu_gate.js :: env TPBMENU_MUTATE=noclamp node Tests/wt_folder_menu_gate.js

echo
echo "  full output: $OUT"
exit $rc_all
