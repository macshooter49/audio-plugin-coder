#!/usr/bin/env bash
# ══════════════════════════════════════════════════════════════════════════════════════════════
#  fb636 (UI-1) — THE HERO'S LOOPS ARE KILLED, AND THE EDITOR STOPS SPENDING FOR NOTHING.
#
#    bash Tests/fb636_gates.sh          # from plugins/Terrain
#
#  A green bar that cannot go red is not a gate: every mutation below is run and the runner refuses
#  to call the gate OK unless the mutant FAILED (exit 1 — exit 2 means an anchor was not found and
#  the control tested nothing).
#
#    fb636_hero_gate.js   the front page is still while the synth plays, never blank, painted by a hand;
#                         XY auto-play keeps its audio clock; the undo glyph is change-gated; the Mac
#                         idle polls rest; a popped card's drag receiver is need-paced; the hero
#                         sampler's polls exist only while the front shows; the C++ frame is lighter.
#    and the neighbours that hold the laws this change leans on:
#      canvas_alive_gate  fb577  no canvas blank at rest (bar 3 wipes the hero) — and its controls
#      tape_alive_gate    fb576  the front tape machine is drawn, heals, costs nothing idle
#      idle_gesture_gate  fb591  a hand paints while it lasts, and stops
#      lane_clock_gate    fb581  a quiet lane is rest
#      hero_glow_gate     fb613  renderTerrain stays bounded (it still draws the one still picture)
#      flowmod_gesture    fb524  a quick drop into a popped card lands (the need-paced receiver)
#      lfo_park · all_menus · flowmod_underline · ui_syntax
# ══════════════════════════════════════════════════════════════════════════════════════════════
set -u
OUT="${TI_GATE_OUT:-$(mktemp -d /tmp/fb636.XXXXXX)}"; mkdir -p "$OUT"; rc_all=0
export NODE_PATH="$PWD/Tests/node_modules"

run () {  # run <label> <expected-exit> <cmd...>
  local label="$1" want="$2"; shift 2
  local f="$OUT/${label//[^A-Za-z0-9_.-]/_}.txt"
  env "$@" > "$f" 2>&1; local rc=$?
  local v="OK"
  if [ "$rc" -ne "$want" ]; then
    [ "$want" -eq 1 ] && v="BROKEN CONTROL — the mutation did NOT go red (exit $rc)" || v="RED — the gate itself is failing"
    rc_all=1; fi
  printf '  %-26s exit=%d want=%d   %s\n' "$label" "$rc" "$want" "$v"
  [ "$rc" -ne "$want" ] && grep -E '✗|FAIL' "$f" | head -3 | sed 's/^/        /'
  return 0
}

echo "══ fb636 (UI-1) GATES ══"
run ui_syntax              0 node Tests/ui_syntax.js Source/ui/public/index.html
echo; echo "  ── the still front, the rest flag, the need-paced polls, the lighter frame ──"
run hero                   0 node Tests/fb636_hero_gate.js
for m in animate resize theme xy undo rest popped card overlay glowrest scan dots cpp; do
  run "hero:$m"            1 HG_MUT="$m" node Tests/fb636_hero_gate.js
done
echo; echo "  ── the neighbours ──"
run canvas_alive           0 node Tests/canvas_alive_gate.js
for m in 1 2 3; do run "canvas_alive:$m" 1 CANVAS_ALIVE_MUTATE="$m" node Tests/canvas_alive_gate.js; done
run tape_alive             0 node Tests/tape_alive_gate.js
run idle_gesture           0 node Tests/idle_gesture_gate.js
run lane_clock             0 node Tests/lane_clock_gate.js
run hero_glow              0 node Tests/hero_glow_gate.js
run hero_glow:nosprite     1 GLOW_MUT=nosprite node Tests/hero_glow_gate.js
run flowmod_gesture        0 node Tests/flowmod_gesture.js
run flowmod_underline      0 node Tests/flowmod_underline.js
run lfo_park               0 node Tests/lfo_park.js
run all_menus              0 node Tests/all_menus.js

echo; echo "  full output: $OUT"
exit $rc_all
