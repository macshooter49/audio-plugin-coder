#!/usr/bin/env bash
# ══════════════════════════════════════════════════════════════════════════════════════════════
#  fb610_gates.sh — WHY A NEW WAVETABLE TOOK SECONDS TO APPEAR, and the proof it no longer does.
#
#    bash Tests/fb610_gates.sh [path/to/a/128-frame/factory.wav]      # from plugins/Terrain
#
#  Max: "the tables take a FEW SECONDS. Serum's take NONE. WE NEED IT TO BE INSTANT."
#
#  THE FIRST THING THIS RUNS IS THE MEASUREMENT THAT SAVED THE DAY. Every instinct said "the bake
#  is too slow — 61 mip levels, 128 frames, 7,936 transforms." wt_bake_bench compiles the SHIPPING
#  Wavetable.h against a REAL factory file and reports 32 ms. Had nobody measured, the whole run
#  would have gone into optimising a transform that was never the problem.
#
#  THE ACTUAL BUG was a race with no retry: loadWavetableByPath queues the build on a one-worker
#  pool and returns IMMEDIATELY, the JS fetches the picture while the build is still running and
#  gets the OLD table, and then the staleness check compares a signature made of warp/fold/
#  spectral/FM/harm — thirteen fields describing what is DONE to a table, and not one saying WHICH
#  table. Signatures matched, so the stale picture was declared fresh and never asked again. It
#  corrected only when some unrelated value drifted: "a few seconds", never the same length twice.
#
#    wt_stamp_gate.py   the two signature lists agree in length, in ORDER, on both payload
#                       branches, AND carry a field derived from the table itself. ⚠️ Its bar [4]
#                       is the only one that reds on the pre-fb610 code — [1]-[3] were all GREEN
#                       while the bug was shipping, because consistency was never the problem.
#    wt_stale_gate.js   drives the SHIPPING maybeRebake: a payload identical in every way EXCEPT
#                       which table it is must re-bake at once.
# ══════════════════════════════════════════════════════════════════════════════════════════════
set -u
WAV="${1:-$HOME/Library/WavesCrate/TerrainInstrument/Wavetables/Factory/Chaos/TERRA CHIRIKOV.wav}"
OUT="${TI_GATE_OUT:-/private/tmp/claude-501/-Users-macshooter/941a8123-ffc6-4f73-84a3-70aee55ea3c3/scratchpad/wtload/gates}"
JUCE="$(cd ../.. && pwd)/_tools/JUCE/modules"
mkdir -p "$OUT"; rc_all=0

echo "══ fb610 GATES ══"
echo
echo "  ── the measurement: is the BAKE the problem? ──"
if [ -f "$WAV" ]; then
  c++ -std=gnu++20 -O3 -DNDEBUG -arch arm64 -DJUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1 \
      -DJUCE_STANDALONE_APPLICATION=1 -DJUCE_MODULE_AVAILABLE_juce_core=1 \
      -I Source -I "$JUCE" -o "$OUT/wt_bake_bench" Tests/wt_bake_bench.cpp \
      "$JUCE/juce_core/juce_core.mm" Tests/juce_compdate_stub.cpp \
      -framework Accelerate -framework Foundation -framework CoreFoundation \
      -framework IOKit -framework Security -framework Cocoa 2>"$OUT/bench.build.log" \
    && "$OUT/wt_bake_bench" "$WAV" | sed 's/^/  /' \
    || { echo "  (bench did not build — see $OUT/bench.build.log)"; rc_all=1; }
else
  echo "  (skipped — no factory wav at $WAV)"
fi

run () {  # run <label> <expected-exit> <env> <cmd...>
  local label="$1" want="$2"; shift 2
  local f="$OUT/${label}.txt"
  env "$@" > "$f" 2>&1; local rc=$?
  local v="OK"
  if [ "$rc" -ne "$want" ]; then
    [ "$want" -eq 1 ] && v="BROKEN CONTROL — the mutation did NOT go red" || v="RED — the gate itself is failing"
    rc_all=1; fi
  printf '  %-18s exit=%d want=%d  %-16s %s\n' "$label" "$rc" "$want" \
         "$(grep -oE '[0-9]+ pass, [0-9]+ fail' "$f" | tail -1)" "$v"
  [ "$rc" -ne "$want" ] && grep '✗' "$f" | head -2 | sed 's/^/        /'
  return 0
}

echo
echo "  ── the signature: does it describe the same thing on both sides? ──"
run stamp            0 python3 Tests/wt_stamp_gate.py
for m in drop reorder oneside; do run "stamp:$m" 1 WTSTAMP_MUT="$m" python3 Tests/wt_stamp_gate.py; done

echo
echo "  ── the behaviour: does a newly picked table actually redraw? ──"
run stale            0 node Tests/wt_stale_gate.js
for m in nostamp notick; do run "stale:$m" 1 STALE_MUT="$m" node Tests/wt_stale_gate.js; done

echo
echo "  ── fb611: the file may not be on this disk, and the read must not block the UI ──"
run locality         0 python3 Tests/wt_locality_gate.py
for m in inline nopool nodataless; do run "locality:$m" 1 WTLOC_MUT="$m" python3 Tests/wt_locality_gate.py; done
run "stale:noclear"  1 STALE_MUT=noclear node Tests/wt_stale_gate.js

echo
echo "  full output: $OUT"
exit $rc_all
