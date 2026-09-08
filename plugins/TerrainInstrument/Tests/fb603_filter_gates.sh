#!/usr/bin/env bash
# ══════════════════════════════════════════════════════════════════════════════════════════════
#  fb603_filter_gates.sh — the three fb603 filter gates, each run with its mutation control.
#
#      bash Tests/fb603_filter_gates.sh              # from plugins/TerrainInstrument, ~4 min
#      bash Tests/fb603_filter_gates.sh --quick      # the 12-type spine, 29 s (measured)
#      TI_GATE_OUT=<dir> bash Tests/fb603_filter_gates.sh
#
#  THE THREE GATES
#    Tests/flt_gate.cpp            the filter section's acceptance test — 11 bars over all 94
#                                  types in 18 s, sharing Tests/flt_measure.h with the report.
#    Tests/flt_cardinality_gate.py the roster moves as ONE number across all nine places it is
#                                  written down (this is what unblocks the growth to 118). <1 s.
#    Tests/flt_curve_diff.js       index.html's drawn curve vs the DSP that actually runs. <1 s
#                                  against Tests/flt_curves.csv, which Tests/fltmeas.cpp writes.
#  Supporting: Tests/flt_measure.h (the one measurement both C++ tools share),
#              Tests/fltmeas.cpp (the human-readable report + the csv),
#              Tests/flt_gate_control.py (the mutation-control bookkeeping).
#
#  A GREEN BAR THAT CANNOT GO RED IS NOT A GATE, so nothing here is reported on its normal run
#  alone. Every gate is run twice and the mutated run must FAIL:
#    · flt_gate       — one mutation per bar. The injected type is chosen FROM THE NORMAL RUN'S
#                       OWN OFFENDERS: line, so it is always a type that currently PASSES that
#                       bar. A hardcoded victim that later started failing on its own would turn
#                       the control into a tautology without anyone noticing.
#    · cardinality    — 13 mutations, each breaking one roster site in a throwaway copy of the
#                       tree; the gate must go red AND name the site.
#    · curve diff     — 3 mutations; each must catch every type that was clean before it.
#
#  ⚠️ flt_gate is EXPECTED TO BE RED until the fb603 fix pass lands — it is the acceptance test
#     FOR that fix pass. What this runner certifies is that each bar is LIVE, i.e. that it moves
#     when the thing it watches moves. Read the bar list, not just the exit code.
# ══════════════════════════════════════════════════════════════════════════════════════════════
set -u
export LC_ALL=C          # the gate prints box-drawing UTF-8; sed on macOS needs a byte locale
OUT="${TI_GATE_OUT:-/private/tmp/claude-501/-Users-macshooter/941a8123-ffc6-4f73-84a3-70aee55ea3c3/scratchpad/fb603/gates}"
QUICK=""
SPINE=""
# mirrors SPINE[] in flt_gate.cpp — the 12 types --quick measures. A victim outside the
# measured set would make every control read BROKEN for a reason that is not the detector.
[ "${1:-}" = "--quick" ] && { QUICK="--quick"; SPINE="0 3 5 9 10 23 43 48 75 83 85 91"; }
mkdir -p "$OUT"
rc_all=0

echo "══ fb603 FILTER GATES ══  $(date '+%Y-%m-%d %H:%M')  ${QUICK:-full 94 types}"
echo "   Source/TerrainFilters.h  $(stat -f '%Sm' -t '%Y-%m-%d %H:%M' Source/TerrainFilters.h 2>/dev/null)"
echo

# ── build ─────────────────────────────────────────────────────────────────────────────────────
for c in fltmeas flt_gate; do
  c++ -std=c++17 -O2 -I Tests -I Tests/shim -I Source "Tests/$c.cpp" \
      -framework Accelerate -o "$OUT/$c" || { echo "  $c: COMPILE FAIL"; rc_all=1; }
done
[ -x "$OUT/flt_gate" ] || exit 1

# ── 1. THE ACCEPTANCE GATE, and one mutation per bar ─────────────────────────────────────────
echo "── [1/3] flt_gate — the filter section's acceptance test ─────────────────────────────"
"$OUT/flt_gate" $QUICK > "$OUT/flt_gate.normal.txt" 2>&1
gate_rc=$?
grep -E '^  (PASS|FAIL)  ' "$OUT/flt_gate.normal.txt" | sed 's/^/   /'
echo "   -> rc=$gate_rc   full output: $OUT/flt_gate.normal.txt"
echo
echo "   MUTATION CONTROLS — each injects that bar's own defect into a type that currently PASSES it."
echo "   A row is OK only if the injected type appears as a NEW offender for that bar."
for m in nan leak stress loop silent mono thd dup osc open; do
  # The victim must be CLEAN for this bar in the normal run — neither an existing offender nor a
  # type the bar skips by design. Tests/flt_gate_control.py reads both machine lines and says so.
  victim=$(python3 Tests/flt_gate_control.py pick "$m" "$OUT/flt_gate.normal.txt" "$SPINE")
  if [ "$victim" = "-1" ]; then
    printf '     %-7s NO CLEAN VICTIM LEFT — every type already fails or is skipped on this bar\n' "$m"
    rc_all=1
    continue
  fi
  TI_FLT_MUT="$m" TI_FLT_INJ="$victim" "$OUT/flt_gate" $QUICK > "$OUT/flt_gate.mut.$m.txt" 2>&1
  verdict=$(python3 Tests/flt_gate_control.py check "$m" "$OUT/flt_gate.normal.txt" \
                    "$OUT/flt_gate.mut.$m.txt" "$victim")
  name=$(grep -m1 "TI_FLT_MUT=$m)" "$OUT/flt_gate.mut.$m.txt" | sed 's/.*target: //; s/ (TI_FLT.*//')
  case "$verdict" in
    OK) printf '     %-7s injected into %-20s (idx %-2s)  clean -> OFFENDER   OK\n' "$m" "${name:-?}" "$victim" ;;
    *)  printf '     %-7s injected into %-20s (idx %-2s)  BROKEN CONTROL — %s\n' "$m" "${name:-?}" "$victim" "${verdict#BROKEN }"
        rc_all=1 ;;
  esac
done
echo

# ── 2. THE CARDINALITY GUARD ──────────────────────────────────────────────────────────────────
echo "── [2/3] flt_cardinality_gate.py — the roster moves as ONE number ───────────────────"
python3 Tests/flt_cardinality_gate.py > "$OUT/cardinality.normal.txt" 2>&1
crc=$?
grep -E '^  (PASS|FAIL)  ' "$OUT/cardinality.normal.txt" | sed 's/^/   /'
echo "   -> rc=$crc  (0 = the roster is consistent today)"
[ $crc -ne 0 ] && rc_all=1
TI_CARD_MUT=all python3 Tests/flt_cardinality_gate.py > "$OUT/cardinality.mutations.txt" 2>&1
mrc=$?
grep -E '^   [a-zA-Z_]+ +rc=' "$OUT/cardinality.mutations.txt" | sed 's/^/  /' | cut -c1-118
tail -1 "$OUT/cardinality.mutations.txt" | sed 's/^/   /'
[ $mrc -ne 0 ] && rc_all=1
echo

# ── 3. THE CURVE-vs-DSP DIFF ──────────────────────────────────────────────────────────────────
echo "── [3/3] flt_curve_diff.js — the drawn curve vs the filter that runs ────────────────"
if [ ! -f Tests/flt_curves.csv ] || [ Source/TerrainFilters.h -nt Tests/flt_curves.csv ]; then
  echo "   Tests/flt_curves.csv is older than Source/TerrainFilters.h — regenerating (~40 s)"
  "$OUT/fltmeas" --csv Tests/flt_curves.csv > "$OUT/fltmeas.txt" 2>/dev/null
fi
node Tests/flt_curve_diff.js > "$OUT/curve_diff.normal.txt" 2>&1
sed -n '/DETECTOR STATUS/,/RES drawn dead/p'   "$OUT/curve_diff.normal.txt" | sed 's/^/  /'
sed -n '/WHERE THE ERROR LIVES/,/honest at every knob/p' "$OUT/curve_diff.normal.txt" | sed 's/^/  /'
TI_CURVE_MUT=all node Tests/flt_curve_diff.js > "$OUT/curve_diff.mutations.txt" 2>&1
mrc=$?
grep -E 'model_flat|model_shift|csv_shift|the diff is' "$OUT/curve_diff.mutations.txt" | sed 's/^/  /'
[ $mrc -ne 0 ] && rc_all=1
echo

echo "══ SUMMARY ══"
echo "   acceptance gate  : rc=$gate_rc  $( [ $gate_rc -eq 0 ] && echo 'ALL BARS GREEN' || echo 'RED — see the FAILED BARS above; this is the fix pass'\''s target' )"
echo "   cardinality      : rc=$crc  $( [ $crc -eq 0 ] && echo 'consistent — the append to 118 is gated' || echo 'INCONSISTENT' )"
echo "   every mutation control fired: $( [ $rc_all -eq 0 ] && echo 'yes' || echo 'NO — a control is broken, fix it before trusting a green bar' )"
echo "   full output: $OUT"
exit $rc_all
