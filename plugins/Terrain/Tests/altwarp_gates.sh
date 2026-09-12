#!/usr/bin/env bash
# ══════════════════════════════════════════════════════════════════════════════════════════════
#  fb636 ALT WARP (39-46) — THE RUNNER. Serum 2's missing Alt Warp modes: the laws, the plumbing, the plugin.
#
#    bash Tests/altwarp_gates.sh                          # from plugins/Terrain (macOS; ~1 min)
#    bash Tests/altwarp_gates.sh <Terrain.component>      # + the real-plugin bars on THAT build (~1 min more)
#
#  A green bar that cannot go red is not a gate: every control below is run, and the runner refuses to call
#  the gate OK unless the mutant FAILED (exit 1; exit 2 = its anchor was not found and it tested nothing) AND
#  the bar it names is among the red ones.
#    altwarp_gate.py      the plumbing: names both sides, filed, 16 Flip-history sites, the pair read at 8 sites +
#                         the waterfall, Flip's FM DC gate, the curve card, the mip pick.
#                         controls: names family flipidx pair fmdc curve mip
#    altwarp_cert.cpp     the laws, on the SHIPPED statics (sliced verbatim by extract_altwarp.py) and the shipping
#                         Wavetable bake: dry points, the glide stall, Serum's measured numbers, shape laws,
#                         continuity, 10 M fuzz, aliasing at the extremes, parity, DC, modes 0-38 bit-identical.
#                         controls: copysign exp2 pmsign nodead oedead asymdir asymskew fliplaw noblep fwdonly
#                                   oeweights case rate dc (sliced mutants)
#    altwarp_au.cpp       the real plugin (only with a bundle argument). No control can redden it without a
#                         mutated plugin build; its bars carry their own in-run controls (49 %, 50 %, Skew).
#  Source/ is never written: every mutant is a generated copy under $OUT or a string in memory.
# ══════════════════════════════════════════════════════════════════════════════════════════════
set -u
OUT="${TI_GATE_OUT:-$(mktemp -d /tmp/altwarp.XXXXXX)}"; mkdir -p "$OUT"; rc_all=0
run () {  # run <label> <expected-exit> <bar that must be red, or -> <cmd...>
  local label="$1" want="$2" bar="$3"; shift 3
  local f="$OUT/${label//[^A-Za-z0-9_.-]/_}.txt"
  env "$@" > "$f" 2>&1; local rc=$?
  local v="OK" reds
  reds=$(grep -oE 'FAIL  \[[0-9A-Za-z]+\]' "$f" | sed 's/FAIL  //' | tr '\n' ' ')
  if [ "$rc" -ne "$want" ]; then
    [ "$want" -eq 1 ] && v="BROKEN CONTROL — the mutation did NOT go red (exit $rc)" || v="RED — the gate itself is failing"
    rc_all=1
  elif [ "$want" -eq 1 ] && [ "$bar" != "-" ] && ! grep -qF "FAIL  $bar" "$f"; then
    v="BROKEN CONTROL — red, but not at $bar (red: ${reds:-none})"; rc_all=1
  elif [ "$want" -eq 1 ]; then
    v="OK — red at: $reds"
  fi
  printf '  %-20s exit=%d want=%d   %s\n' "$label" "$rc" "$want" "$v"
  [ "$rc" -ne "$want" ] && grep -E 'FAIL|MUTATION' "$f" | head -3 | sed 's/^/        /'
  return 0
}
echo "══ fb636 ALT WARP 39-46 ══"
echo "  ── the plumbing ──"
run gate               0 - python3 Tests/altwarp_gate.py
for mb in names:[1] family:[2] flipidx:[3] pair:[4] fmdc:[5] curve:[6] mip:[7]; do
  run "gate:${mb%%:*}"  1 "${mb#*:}" AWG_MUTATE="${mb%%:*}" python3 Tests/altwarp_gate.py
done

echo; echo "  ── the laws: slice the shipping statics, build the cert healthy and as fourteen mutants ──"
MUTS="copysign:[4a] exp2:[3a] pmsign:[3b] nodead:[2] oedead:[2] asymdir:[3c] asymskew:[3c] fliplaw:[3d] noblep:[7a] fwdonly:[7c] oeweights:[3e] case:[11] rate:[3f] dc:[9]"
python3 Tests/extract_altwarp.py Source/SynthVoice.h "$OUT/aw.h" > "$OUT/slice.txt" 2>&1 \
  || { echo "  slice FAILED:"; cat "$OUT/slice.txt"; exit 1; }
CXX=(clang++ -O2 -std=c++17 -I Tests/shim -I Source)
build () {  # build <name> <slice header>
  "${CXX[@]}" -include "$2" Tests/altwarp_cert.cpp -o "$OUT/$1" -framework Accelerate > "$OUT/$1.build.txt" 2>&1 \
    || { echo "  $1: COMPILE FAIL"; grep -m3 error "$OUT/$1.build.txt" | sed 's/^/        /'; rc_all=1; }
}
build cert "$OUT/aw.h"
for mb in $MUTS; do
  m="${mb%%:*}"
  python3 Tests/extract_altwarp.py Source/SynthVoice.h "$OUT/aw_$m.h" --mutate "$m" >> "$OUT/slice.txt" 2>&1 \
    || { echo "  slice ($m) FAILED — the control cannot fire:"; tail -1 "$OUT/slice.txt"; rc_all=1; continue; }
  build "cert_$m" "$OUT/aw_$m.h"
done
run cert               0 - "$OUT/cert"
for mb in $MUTS; do
  run "cert:${mb%%:*}"  1 "${mb#*:}" "$OUT/cert_${mb%%:*}"
done

if [ $# -ge 1 ]; then
  echo; echo "  ── the real plugin: $1 ──"
  c++ -std=c++17 -O2 -I Tests Tests/altwarp_au.cpp -framework AudioToolbox -framework CoreFoundation -o "$OUT/awau" > "$OUT/awau.build.txt" 2>&1 \
    || { echo "  altwarp_au: COMPILE FAIL"; rc_all=1; }
  run au               0 - TERRAIN_AU_BUNDLE="$1" "$OUT/awau"
  grep -E '^  (PASS|FAIL)' "$OUT/au.txt" | sed 's/^/    /'
fi
echo; echo "  full output: $OUT"
exit $rc_all
