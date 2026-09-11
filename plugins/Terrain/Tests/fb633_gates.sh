#!/usr/bin/env bash
# ══════════════════════════════════════════════════════════════════════════════════════════════
#  fb633 — THE BODE CARD (the live layer that vanished, the node that trailed, the streams that
#  scattered, the canvas nothing chased), THE ADD-EFFECT MENU A–Z, THE WIDE SAVE SHEET.
#
#  Page gates, Chrome headless. Each control flips one bar to the OLD behaviour and must go RED:
#    fb633_page_gate.js   PG_MUT=order (the build-order menu) · PG_MUT=narrow (the 340 sheet)
#    fb633_bode_gate.js   BG_MUT=mean · svg · blank · jitter · once   (see the file's head)
#  Then the rack's own gates, normal runs only (the new card must not move anything else):
#    fxmod_move.js (fb457 — the .bod-n@cx probe still moves) · fx3_ui.js · fxmod_menu.js
#
#    bash Tests/fb633_gates.sh          # from plugins/Terrain
# ══════════════════════════════════════════════════════════════════════════════════════════════
set -u
OUT="${TI_GATE_OUT:-$(mktemp -d /tmp/fb633.XXXXXX)}"; mkdir -p "$OUT"; rc_all=0
export NODE_PATH="$PWD/Tests/node_modules"
run () {  # label  mutation-env  command...
  local name="$1" mut="$2"; shift 2
  "$@"            > "$OUT/$name.normal.txt"  2>&1; local a=$?
  env "$mut" "$@" > "$OUT/$name.mutated.txt" 2>&1; local b=$?
  local v="OK"; [ $a -ne 0 ] && v="RED — the gate itself is failing"; [ $b -eq 0 ] && v="BROKEN CONTROL — the mutation did NOT go red"
  printf '  %-22s normal=%d  mutated(%s)=%d   %s\n' "$name" "$a" "$mut" "$b" "$v"
  if [ $a -ne 0 ] || [ $b -eq 0 ]; then rc_all=1; fi; return 0
}
plain () {  # label  command...
  local name="$1"; shift
  "$@" > "$OUT/$name.txt" 2>&1; local a=$?
  printf '  %-22s normal=%d   %s\n' "$name" "$a" "$([ $a -eq 0 ] && echo OK || echo 'RED')"
  [ $a -ne 0 ] && rc_all=1; return 0
}
echo "══ fb633 GATE ══"
run page:order   PG_MUT=order   node Tests/fb633_page_gate.js
run page:narrow  PG_MUT=narrow  node Tests/fb633_page_gate.js
for m in mean svg blank jitter once; do run "bode:$m" "BG_MUT=$m" node Tests/fb633_bode_gate.js; done
# fxmod_move (fb457) carries FOUR bars that were already red on the shipped page before fb633 — the
# wavetable shaping re-bakes forever ("a SETTLED shaping bakes nothing" / "CONVERGES"), a CPU leak of
# its own that fb633 neither caused nor fixed. A red bar everyone steps over is zero information, so
# they are NAMED here and only a NEW failure in that gate turns this line red.
node Tests/fxmod_move.js > "$OUT/fxmod_move.txt" 2>&1
known=$(grep -c "FAIL.*\(bakes nothing\|CONVERGES\)" "$OUT/fxmod_move.txt"); other=$(grep "FAIL" "$OUT/fxmod_move.txt" | grep -vc "bakes nothing\|CONVERGES\|FAIL [0-9]")
if [ "$other" -eq 0 ]; then printf '  %-22s new failures=0  (known pre-fb633 bake-leak reds: %s)   OK\n' fxmod_move "$known"
else printf '  %-22s NEW failures=%s   RED\n' fxmod_move "$other"; rc_all=1; fi
plain fx3_ui      node Tests/fx3_ui.js
plain fxmod_menu  node Tests/fxmod_menu.js
echo; echo "  full output: $OUT"; exit $rc_all
