#!/usr/bin/env bash
# ══════════════════════════════════════════════════════════════════════════════════════════════
#  fb636 F1 — A FREED WAVETABLE REALLY LEAVES THE FOOTPRINT (page-backed tables), AND NOTHING READS
#  ONE AFTER IT HAS LEFT (the import lifetime: the grace fence + the ImportRead pins).
#
#    bash Tests/wt_pages_gates.sh          # from plugins/Terrain (macOS; ~1 min)
#    WTP_SECS=60 bash Tests/wt_pages_gates.sh   # the long stress (the design's 60 s)
#
#  A green bar that cannot go red is not a gate: every control below is run and the runner refuses
#  to call the gate OK unless the mutant FAILED (exit 1; exit 2 = an anchor was not found and the
#  control tested nothing).
#    wt_pages_gate.py      the source half: TableStore on mipData_/twinData_, the allocator's two doors,
#                          the four off-audio samplers pinned, the census of raw live loads, the claim /
#                          free / pin shapes.       controls: nostore windows unpin display noclaim nofree
#    wt_pages_cert.cpp     P1-P3 the page door (Tag-240 region, the footprint really drops, no ratchet),
#                          P4 the content hash, L1-L3 the SHIPPING lifetime (sliced verbatim by
#                          extract_import_life.py) under a four-thread stress.
#                          controls: nopin freepin nograce (sliced mutants) · malloc (a Wavetable.h whose
#                          page door never opens — it must fail P1-P3 AND print the SAME P4 hash)
#  Source/ is never written: every mutant is a generated copy under $OUT.
# ══════════════════════════════════════════════════════════════════════════════════════════════
set -u
OUT="${TI_GATE_OUT:-$(mktemp -d /tmp/wtpages.XXXXXX)}"; mkdir -p "$OUT/malloc"; rc_all=0
SECS="${WTP_SECS:-8}"
run () {  # run <label> <expected-exit> <cmd...>
  local label="$1" want="$2"; shift 2
  local f="$OUT/${label//[^A-Za-z0-9_.-]/_}.txt"
  env "$@" > "$f" 2>&1; local rc=$?
  local v="OK"
  if [ "$rc" -ne "$want" ]; then
    [ "$want" -eq 1 ] && v="BROKEN CONTROL — the mutation did NOT go red (exit $rc)" || v="RED — the gate itself is failing"
    rc_all=1; fi
  printf '  %-22s exit=%d want=%d   %s\n' "$label" "$rc" "$want" "$v"
  [ "$rc" -ne "$want" ] && grep -E 'FAIL' "$f" | head -3 | sed 's/^/        /'
  return 0
}
echo "══ fb636 F1 — WAVETABLE PAGES ══"
echo "  ── the source ──"
run gate                 0 python3 Tests/wt_pages_gate.py
for m in nostore windows unpin display noclaim nofree; do
  run "gate:$m"          1 WTP_SRC_MUT="$m" python3 Tests/wt_pages_gate.py
done

echo; echo "  ── the cert: slice the shipping lifetime, build it healthy and as four mutants ──"
python3 Tests/extract_import_life.py Source/PluginProcessor.h Source/PluginProcessor.cpp "$OUT/il.h" > "$OUT/slice.txt" 2>&1 \
  || { echo "  slice FAILED:"; cat "$OUT/slice.txt"; exit 1; }
for m in nopin freepin nograce; do
  python3 Tests/extract_import_life.py Source/PluginProcessor.h Source/PluginProcessor.cpp "$OUT/il_$m.h" --mutate "$m" >> "$OUT/slice.txt" 2>&1 \
    || { echo "  slice ($m) FAILED:"; tail -2 "$OUT/slice.txt"; exit 1; }
done
python3 - "$OUT/malloc/Wavetable.h" <<'PY' || { echo "  malloc mutant FAILED"; exit 1; }
import sys
s = open ("Source/Wavetable.h", encoding = "utf-8").read()
old = "static constexpr std::size_t kMapMinBytes = 256 * 1024;"
if s.count (old) != 1: sys.exit ("anchor found %d times" % s.count (old))
open (sys.argv[1], "w", encoding = "utf-8").write (s.replace (old, "static constexpr std::size_t kMapMinBytes = (std::size_t) -1;"))
PY
CXX=(clang++ -O2 -std=c++17 -pthread -I Tests/shim -I Source)
build () {  # build <name> <extra flags...>
  local name="$1"; shift
  "${CXX[@]}" "$@" Tests/wt_pages_cert.cpp -o "$OUT/$name" -framework Accelerate > "$OUT/$name.build.txt" 2>&1 \
    || { echo "  $name: COMPILE FAIL"; grep -m3 error "$OUT/$name.build.txt" | sed 's/^/        /'; rc_all=1; }
}
# ⚠️ the headers go in by -D, never -I: a quoted #include searches the including file's own directory
#    first, so an -I<mutdir> build silently compiles the healthy copy (the fb606 trap).
build cert           -DIMPORT_LIFE_HEADER="\"$OUT/il.h\""
for m in nopin freepin nograce; do build "cert_$m" -DIMPORT_LIFE_HEADER="\"$OUT/il_$m.h\""; done
build cert_malloc    -DIMPORT_LIFE_HEADER="\"$OUT/il.h\"" -DWT_PAGES_HEADER="\"$OUT/malloc/Wavetable.h\""

echo; echo "  ── the page door and the lifetime (stress ${SECS}s) ──"
run cert                 0 "$OUT/cert" "$SECS"
for m in nopin freepin nograce; do
  run "cert:$m"          1 "$OUT/cert_$m" "$SECS"
done
run cert:malloc          1 "$OUT/cert_malloc" 1
h1=$(grep -m1 'P4  hash' "$OUT/cert.txt" 2>/dev/null | sed 's/.*hash //')
h2=$(grep -m1 'P4  hash' "$OUT/cert_malloc.txt" 2>/dev/null | sed 's/.*hash //')
if [ -n "$h1" ] && [ "$h1" = "$h2" ]; then
  printf '  %-22s %s\n' "P4 same floats" "OK — page-backed and malloc'd tables hash the same: $h1"
else
  printf '  %-22s %s\n' "P4 same floats" "RED — page-backed [$h1] vs malloc [$h2]"; rc_all=1
fi
echo; echo "  full output: $OUT"
exit $rc_all
