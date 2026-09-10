#!/usr/bin/env bash
# ══════════════════════════════════════════════════════════════════════════════════════════════
#  fb620 — THE PRESET SURFACES. The page runs headless (puppeteer) with the fb618/fb619 natives
#  stubbed over an in-memory catalogue and the bars click the real furniture: header name, quick
#  menu, Browse all, rows, +, the sheets. Two controls, each must go RED:
#    TP_MUT=zorder   #syn-panel raised over the glass  → the hit-test bar [3]
#    TP_MUT=nopush   the courier never pushes onPatchLoaded → the load bars [6] [7]
#    TP_MUT=lanes    the pre-fb628 world (overlay reaches over, lane loses clearance) → [19] [20]
#    TP_MUT=header   the pre-fb629 world (the header paints its own bar, the mark shrinks) → [21] [22]
#    TP_MUT=knobval  ring text blown to 14px, fraction hidden, strip call swallowed → [23] [24] [25]
#  Plus every inline <script> must still parse (ui_syntax.js).
#
#    bash Tests/fb620_gates.sh          # from plugins/Terrain
# ══════════════════════════════════════════════════════════════════════════════════════════════
set -u
OUT="${TI_GATE_OUT:-$(mktemp -d /tmp/fb620.XXXXXX)}"; mkdir -p "$OUT"; rc_all=0
run () {  # label  mutation-env  command...
  local name="$1" mut="$2"; shift 2
  "$@"            > "$OUT/$name.normal.txt"  2>&1; local a=$?
  env "$mut" "$@" > "$OUT/$name.mutated.txt" 2>&1; local b=$?
  local v="OK"; [ $a -ne 0 ] && v="RED — the gate itself is failing"; [ $b -eq 0 ] && v="BROKEN CONTROL — the mutation did NOT go red"
  printf '  %-22s normal=%d  mutated(%s)=%d   %s\n' "$name" "$a" "$mut" "$b" "$v"
  if [ $a -ne 0 ] || [ $b -eq 0 ]; then rc_all=1; fi; return 0
}
echo "══ fb620 GATE ══"
node Tests/ui_syntax.js Source/ui/public/index.html > "$OUT/ui_syntax.txt" 2>&1; s=$?
printf '  %-22s %s\n' "ui_syntax" "$([ $s -eq 0 ] && echo OK || echo 'RED — an inline <script> does not parse')"; [ $s -ne 0 ] && rc_all=1
run surfaces:zorder  TP_MUT=zorder  node Tests/preset_surfaces_gate.js
run surfaces:nopush  TP_MUT=nopush  node Tests/preset_surfaces_gate.js
run surfaces:lanes   TP_MUT=lanes   node Tests/preset_surfaces_gate.js
run surfaces:header  TP_MUT=header  node Tests/preset_surfaces_gate.js
run surfaces:knobval TP_MUT=knobval node Tests/preset_surfaces_gate.js
echo; echo "  full output: $OUT"; exit $rc_all
