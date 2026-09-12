#!/usr/bin/env bash
# ══════════════════════════════════════════════════════════════════════════════════════════════
#  fb636 ALT WARP · THE PAGE — THE RUNNER.  bash Tests/altwarp_ui_gates.sh   (from plugins/Terrain; page only,
#  no build, no install, Source is never written)
#
#  altwarp_ui_gate.js normal (must pass), then each of its 13 controls: a control passes only if it exits 1 AND
#  the bar it names is among the RED BARS it prints (exit 2 = an anchor was not found: the control tested nothing).
#  Then the neighbours the regroup leans on: ui_syntax, warp_menu (the fb373 round trip over every live mode on
#  all 8 slots) and altwarp_gate.py (its [2] reads the families from source).
# ══════════════════════════════════════════════════════════════════════════════════════════════
set -u
OUT="${TI_GATE_OUT:-$(mktemp -d /tmp/awui.XXXXXX)}"; mkdir -p "$OUT"; rc_all=0
export NODE_PATH="$PWD/Tests/node_modules"
export AWU_TMP="$OUT"
echo "══ fb636 ALT WARP · THE PAGE ══"
node Tests/altwarp_ui_gate.js > "$OUT/normal.txt" 2>&1; a=$?
printf '  %-12s exit=%d   %s\n' normal "$a" "$([ $a -eq 0 ] && echo OK || echo 'RED — the gate itself is failing')"
[ $a -ne 0 ] && { rc_all=1; grep -E 'FAIL' "$OUT/normal.txt" | head -5 | sed 's/^/        /'; }
for pair in order:1 filing:1 nojump:2a anyamt:2b repick:2d onload:2e readout:3a sign:3s ring:3r norepaint:3r pill:3p oscq:3o extvar:4; do
  m="${pair%%:*}"; want="${pair##*:}"
  AWU_MUTATE="$m" node Tests/altwarp_ui_gate.js > "$OUT/$m.txt" 2>&1; b=$?
  bars="$(grep -o 'RED BARS: .*' "$OUT/$m.txt" | sed 's/RED BARS: //')"
  v="OK"
  if [ $b -ge 2 ]; then v="BROKEN CONTROL — exit $b (an anchor not found, or a crash)"; rc_all=1
  elif [ $b -eq 0 ]; then v="BROKEN CONTROL — it did NOT go red"; rc_all=1
  elif ! echo " $bars " | grep -q " $want "; then v="BROKEN CONTROL — red on [$bars], not on its bar [$want]"; rc_all=1; fi
  printf '  %-12s exit=%d   red [%s]  want [%s]   %s\n' "$m" "$b" "$bars" "$want" "$v"
done
echo "  ── the neighbours ──"
node Tests/ui_syntax.js Source/ui/public/index.html > "$OUT/ui_syntax.txt" 2>&1; a=$?
printf '  %-12s exit=%d   %s\n' ui_syntax "$a" "$([ $a -eq 0 ] && echo OK || echo RED)"; [ $a -ne 0 ] && rc_all=1
node Tests/warp_menu.js > "$OUT/warp_menu.txt" 2>&1; a=$?
printf '  %-12s exit=%d   %s   %s\n' warp_menu "$a" "$([ $a -eq 0 ] && echo OK || echo RED)" "$(grep -o 'PASS [0-9]*   FAIL [0-9]*' "$OUT/warp_menu.txt")"; [ $a -ne 0 ] && rc_all=1
python3 Tests/altwarp_gate.py > "$OUT/altwarp_gate.txt" 2>&1; a=$?
printf '  %-12s exit=%d   %s\n' altwarp_gate "$a" "$([ $a -eq 0 ] && echo OK || echo RED)"; [ $a -ne 0 ] && rc_all=1
echo "  logs: $OUT"
[ $rc_all -eq 0 ] && echo "  ✅ ALT WARP page gate GREEN (every control red on its bar)" || echo "  ❌ ALT WARP page gate RED"
exit $rc_all
