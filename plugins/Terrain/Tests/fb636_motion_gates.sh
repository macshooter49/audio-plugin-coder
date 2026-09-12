#!/usr/bin/env bash
# ══════════════════════════════════════════════════════════════════════════════════════════════
#  fb636 — THE MOTION LAW: EVERY ANIMATION WINDS UP, WINDS DOWN, AND ONLY MOVES WHILE MIDI SOUNDS.
#
#  Max: "animation loops stop by also giving us an animation loop ending... a wind up and a wind down
#  and then it stops. That's the Serum 2 method... I only want things to be moving officially when
#  MIDI's coming in... but we want to keep all the paint there."
#
#  Every gate runs NORMAL (must pass) and, where it has one, with each MUTATION (must go RED):
#    motion   motion_law_gate   noend · notail · nowindup · litonly · anyband · norest · cardrest · cardhome ·
#                               brfreeze · wallclock · wfloop · rewind
#             (fb636 h3 — bar 4 excuses a step back only when it is the landing fold, a whole number of periods;
#              'rewind' starts each ending 1.3 s back and must now go red)
#    edge     wind_edge_gate    rejump · unseen · cardslow · notes · lastfx
#             (fb636 h3 — the review's cases: a note mid-ending never jumps the pose; an ending nobody watched (a card closed,
#              the synth page hidden) is not resumed with no MIDI; a slow card gets home in ≤ 2.5 s; the cards' readouts
#              come home)
#             (fb636 h1 — Max 02:34: all four tiles move with the MIDI lit or not, and every tile and FLOW card rests on
#              ONE designed home pose; 'unlit' became 'litonly', the reversed rule, which must now go red)
#    home     home_pose_gate    drv · sweep · magtau · topo · noise · churn · dly · lfobr · lfoic · brsnap
#             (fb636 h2 — Max 02:34 "same for everything else": the filter emblems and curve, the topo field, the noise cloud,
#              the waterfall churn line, the delay pulse, the LFO curve breath and the CSS breathers each rest on ONE home —
#              the boot picture, byte for byte — after a note stopped at three different points)
#    rest     idle_gesture_gate                      (a hand still paints at rest; rest is still zero — fb591)
#    canvas   canvas_alive_gate  CANVAS_ALIVE_MUTATE=1 (nothing blank when the clock stops — fb577)
#    lane     lane_clock_gate    LANE_CLOCK_MUTATE=1   (a quiet lane is rest, never a self-clock — fb581)
#    park     lfo_park           LFO_PARK_MUTATE=2     (the LFO head: the model the motion clock copies — fb567;
#                                                     fb636 h2: bar 10 — the curve finishes its breath, then rests on .9)
#    stall    geode_follower_gate GEODE_FOLLOWER_MUTATE=4 (re-based at fb636: a stall costs ONE bounded wind-down, then parks;
#                                                          fb636 h1: it waits for that wind-down — a home can be ~4.7 s away)
#    comets   flowmod_underline · fxmod_underline      (re-based at fb636: the LFO feed carries its note)
#
#    bash Tests/fb636_motion_gates.sh      # from plugins/Terrain (page gates only: no build, no install)
# ══════════════════════════════════════════════════════════════════════════════════════════════
set -u
OUT="${TI_GATE_OUT:-$(mktemp -d /tmp/fb636m.XXXXXX)}"; mkdir -p "$OUT"; rc_all=0
export NODE_PATH="$PWD/Tests/node_modules"
run () {  # label  mutation-env  command...
  local name="$1" mut="$2"; shift 2
  "$@"            > "$OUT/$name.normal.txt"  2>&1; local a=$?
  env "$mut" "$@" > "$OUT/$name.mutated.txt" 2>&1; local b=$?
  local v="OK"; [ $a -ne 0 ] && v="RED — the gate itself is failing"; [ $b -eq 0 ] && v="BROKEN CONTROL — the mutation did NOT go red"
  [ $b -ge 2 ] && v="BROKEN CONTROL — exit $b (a mutation anchor not found, or a crash): the control tested nothing"
  printf '  %-24s normal=%d  mutated(%s)=%d   %s\n' "$name" "$a" "$mut" "$b" "$v"
  if [ $a -ne 0 ] || [ $b -eq 0 ] || [ $b -ge 2 ]; then rc_all=1; fi; return 0
}
plain () {  # label command...   (a gate with no mutation of its own: it must simply pass)
  local name="$1"; shift
  "$@" > "$OUT/$name.normal.txt" 2>&1; local a=$?
  printf '  %-24s normal=%d   %s\n' "$name" "$a" "$([ $a -eq 0 ] && echo OK || echo 'RED — the gate itself is failing')"
  [ $a -ne 0 ] && rc_all=1; return 0
}
echo "══ fb636 MOTION GATE ══"
node Tests/ui_syntax.js Source/ui/public/index.html > "$OUT/ui_syntax.txt" 2>&1 || { echo "  ui_syntax: RED — $OUT/ui_syntax.txt"; rc_all=1; }
for m in noend notail nowindup litonly anyband norest cardrest cardhome brfreeze wallclock wfloop rewind; do run "motion:$m" "MOTION_MUT=$m" node Tests/motion_law_gate.js; done
for m in rejump unseen cardslow notes lastfx; do run "edge:$m" "EDGE_MUT=$m" node Tests/wind_edge_gate.js; done
for m in drv sweep magtau topo noise churn dly lfobr lfoic brsnap; do run "home:$m" "HOME_MUT=$m" node Tests/home_pose_gate.js; done
plain "rest:idle_gesture"  node Tests/idle_gesture_gate.js
run   "canvas:nowake"      "CANVAS_ALIVE_MUTATE=1" node Tests/canvas_alive_gate.js
run   "lane:quiet"         "LANE_CLOCK_MUTATE=1"   node Tests/lane_clock_gate.js
run   "park:idle"          "LFO_PARK_MUTATE=2"     node Tests/lfo_park.js
run   "stall:geode"        "GEODE_FOLLOWER_MUTATE=4" node Tests/geode_follower_gate.js
plain "comets:flowmod"     node Tests/flowmod_underline.js
plain "comets:fxmod"       node Tests/fxmod_underline.js
echo "  logs: $OUT"
[ $rc_all -eq 0 ] && echo "  ✅ fb636 motion gate GREEN (every control red)" || echo "  ❌ fb636 motion gate RED"
exit $rc_all
