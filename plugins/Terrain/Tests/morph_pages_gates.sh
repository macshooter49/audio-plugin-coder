#!/usr/bin/env bash
# ══════════════════════════════════════════════════════════════════════════════════════════════
#  fb636 F4 — A SPECTRAL-MORPH BUFFER NOTHING CAN REACH IS GIVEN BACK (the morph half of M2), AND
#  NOTHING READS ONE AFTER IT HAS GONE.
#
#    bash Tests/morph_pages_gates.sh               # from plugins/Terrain (macOS; ~1 min)
#    MPG_SECS=60 bash Tests/morph_pages_gates.sh   # the long stress (the design's 60 s)
#
#  A green bar that cannot go red is not a gate: every control below is run, and the runner refuses to
#  call the gate OK unless the mutant FAILED (exit 1; exit 2 = an anchor was not found and the control
#  tested nothing) AND the bar it names is among the red ones.
#    morph_pages_gate.py    the source half: the stamps, one publish door, the fence's audio half seq_cst,
#                           the free's conditions and what it may write, the claim, the census, two free doors.
#                           controls: stampeach direct acquire nograce cooldown noclaim early census extrafree
#    morph_pages_cert.cpp   M1-M5 the buffers (built, retired, held, freed while on, freed when off, the same
#                           floats after a free, no rebuild state written) and L1-L2 the SHIPPING lifetime
#                           (sliced verbatim by extract_morph_life.py) with the audio read on another thread.
#                           controls: nofence nohold cooldown stampeach (sliced mutants)
#  Source/ is never written: every mutant is a generated copy under $OUT or a string in memory.
# ══════════════════════════════════════════════════════════════════════════════════════════════
set -u
OUT="${TI_GATE_OUT:-$(mktemp -d /tmp/morphpages.XXXXXX)}"; mkdir -p "$OUT"; rc_all=0
SECS="${MPG_SECS:-6}"
run () {  # run <label> <expected-exit> <bar that must be red, or -> <cmd...>
  local label="$1" want="$2" bar="$3"; shift 3
  local f="$OUT/${label//[^A-Za-z0-9_.-]/_}.txt"
  env "$@" > "$f" 2>&1; local rc=$?
  local v="OK" reds
  reds=$(grep -oE 'FAIL  (\[[0-9]+\]|[A-Z][0-9])' "$f" | sed 's/FAIL  //' | tr '\n' ' ')
  if [ "$rc" -ne "$want" ]; then
    [ "$want" -eq 1 ] && v="BROKEN CONTROL — the mutation did NOT go red (exit $rc)" || v="RED — the gate itself is failing"
    rc_all=1
  elif [ "$want" -eq 1 ] && [ "$bar" != "-" ] && ! grep -qF "FAIL  $bar" "$f"; then
    v="BROKEN CONTROL — red, but not at $bar (red: ${reds:-none})"; rc_all=1
  elif [ "$want" -eq 1 ]; then
    v="OK — red at: $reds"
  fi
  printf '  %-22s exit=%d want=%d   %s\n' "$label" "$rc" "$want" "$v"
  [ "$rc" -ne "$want" ] && grep -E 'FAIL|MUTATION' "$f" | head -3 | sed 's/^/        /'
  return 0
}
echo "══ fb636 F4 — MORPH BUFFERS GIVEN BACK ══"
echo "  ── the source ──"
run gate                 0 - python3 Tests/morph_pages_gate.py
for mb in stampeach:[2] direct:[2] acquire:[3] nograce:[4] cooldown:[4] noclaim:[5] early:[1] census:[6] extrafree:[7]; do
  run "gate:${mb%%:*}"   1 "${mb#*:}" MPG_SRC_MUT="${mb%%:*}" python3 Tests/morph_pages_gate.py
done

echo; echo "  ── the cert: slice the shipping lifetime, build it healthy and as four mutants ──"
python3 Tests/extract_morph_life.py Source/PluginProcessor.h Source/PluginProcessor.cpp "$OUT/ml.h" > "$OUT/slice.txt" 2>&1 \
  || { echo "  slice FAILED:"; cat "$OUT/slice.txt"; exit 1; }
for m in nofence nohold cooldown stampeach; do
  python3 Tests/extract_morph_life.py Source/PluginProcessor.h Source/PluginProcessor.cpp "$OUT/ml_$m.h" --mutate "$m" >> "$OUT/slice.txt" 2>&1 \
    || { echo "  slice ($m) FAILED:"; tail -2 "$OUT/slice.txt"; exit 1; }
done
CXX=(clang++ -O2 -std=c++17 -pthread -I Tests/shim -I Source)
build () {  # build <name> <extra flags...>
  local name="$1"; shift
  "${CXX[@]}" "$@" Tests/morph_pages_cert.cpp -o "$OUT/$name" -framework Accelerate > "$OUT/$name.build.txt" 2>&1 \
    || { echo "  $name: COMPILE FAIL"; grep -m3 error "$OUT/$name.build.txt" | sed 's/^/        /'; rc_all=1; }
}
# ⚠️ the header goes in by -D, never -I: a quoted #include searches the including file's own directory
#    first, so an -I<mutdir> build silently compiles the healthy copy (the fb606 trap).
build cert -DMORPH_LIFE_HEADER="\"$OUT/ml.h\""
for m in nofence nohold cooldown stampeach; do build "cert_$m" -DMORPH_LIFE_HEADER="\"$OUT/ml_$m.h\""; done

echo; echo "  ── the buffers and the lifetime (stress ${SECS}s) ──"
run cert                 0 - "$OUT/cert" "$SECS"
for mb in nofence:L1 nohold:M2 cooldown:M5 stampeach:M3; do
  run "cert:${mb%%:*}"   1 "${mb#*:}" "$OUT/cert_${mb%%:*}" "$SECS"
done
echo; echo "  full output: $OUT"
exit $rc_all
