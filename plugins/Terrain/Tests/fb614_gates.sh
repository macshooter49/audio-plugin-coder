#!/usr/bin/env bash
# ══════════════════════════════════════════════════════════════════════════════════════════════
#  fb612 · fb613 · fb614 — the factory library, the loudness stall, and the distortion curve.
#
#    bash Tests/fb614_gates.sh          # from plugins/Terrain
#
#  A green bar that cannot go red is not a gate, so every control is run and the runner refuses to
#  call a gate OK unless its mutant FAILED.
#
#    wt_factory_gate.py   fb612 — 120 tables ship in the bundle as "Terra - Name" 24-bit FLAC, ten
#                         categories, no duplicate names, every file 128x2048, and the bank is found
#                         from the PLUGIN binary rather than the host app.
#    hero_glow_gate.js    fb613 — the UI must not get slower when the audio gets louder. Drives the
#                         real renderTerrain at four levels with the canvas instrumented. Bar [1]
#                         insists the cap is actually REACHED when loud: a bound nothing ever hits
#                         proves nothing.
#    dst_curve_gate.js    fb614 — one pushed frame paints the curve (the idle case), and a
#                         unity-in-the-linear-region curve lies ON the reference to 0.09 px.
# ══════════════════════════════════════════════════════════════════════════════════════════════
set -u
OUT="${TI_GATE_OUT:-/private/tmp/claude-501/-Users-macshooter/941a8123-ffc6-4f73-84a3-70aee55ea3c3/scratchpad/fb614}"
mkdir -p "$OUT"; rc_all=0

run () {  # run <label> <expected-exit> <cmd...>
  local label="$1" want="$2"; shift 2
  local f="$OUT/${label//[^A-Za-z0-9_.-]/_}.txt"
  env "$@" > "$f" 2>&1; local rc=$?
  local v="OK"
  if [ "$rc" -ne "$want" ]; then
    [ "$want" -eq 1 ] && v="BROKEN CONTROL — the mutation did NOT go red" || v="RED — the gate itself is failing"
    rc_all=1; fi
  printf '  %-22s exit=%d want=%d  %-16s %s\n' "$label" "$rc" "$want" \
         "$(grep -oE '[0-9]+ pass, [0-9]+ fail' "$f" | tail -1)" "$v"
  [ "$rc" -ne "$want" ] && grep '✗' "$f" | head -2 | sed 's/^/        /'
  return 0
}

echo "══ fb612 · fb613 · fb614 GATES ══"
echo
echo "  ── fb612: the factory wavetable library ──"
run factory              0 python3 Tests/wt_factory_gate.py
for m in allcaps nodash dupe short hostapp; do run "factory:$m" 1 WTFAC_MUT="$m" python3 Tests/wt_factory_gate.py; done

echo
echo "  ── fb613: loud audio must not cost more to draw ──"
run glow                 0 node Tests/hero_glow_gate.js
run glow:nosprite        1 GLOW_MUT=nosprite node Tests/hero_glow_gate.js

echo
echo "  ── fb614: the distortion curve draws, on the right axis ──"
run curve                0 node Tests/dst_curve_gate.js
for m in nospan staticunity inquiet; do run "curve:$m" 1 DSTC_MUT="$m" node Tests/dst_curve_gate.js; done

echo
echo "  full output: $OUT"
exit $rc_all
