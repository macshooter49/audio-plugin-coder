#!/usr/bin/env bash
# ══════════════════════════════════════════════════════════════════════════════════════════════
#  fb618 — A PRESET LOADS CLEAN. The two-bar gate from Design/PRESET-SYSTEM-v1.md §5.
#
#    bash Tests/fb618_gates.sh          # from plugins/Terrain
#
#  preset_null_cert.cpp drives the INSTALLED AU through kAudioUnitProperty_ClassInfo — the door a
#  DAW uses. [1] the <preset> child survives, once, first, with carries. [2] a fresh instance
#  loads the chunk and saves it back byte-identical. [3] a chunk WITHOUT a property leaves nothing
#  of the previous patch behind (cards, macro names). [4] the first note on two fresh instances
#  nulls below -100 dBFS. Two controls, each must go RED: PN_MUT=inherit expects the old patch's
#  cards to survive; PN_MUT=drift compares against a second note on the SAME instance.
#  fb617's round-trip cert runs too: the remove-first save block must not disturb the fixed point.
# ══════════════════════════════════════════════════════════════════════════════════════════════
set -u
OUT="${TI_GATE_OUT:-$(mktemp -d /tmp/fb618.XXXXXX)}"; mkdir -p "$OUT"; rc_all=0
run () {  # label  mutation-env  command...
  local name="$1" mut="$2"; shift 2
  "$@"            > "$OUT/$name.normal.txt"  2>&1; local a=$?
  env "$mut" "$@" > "$OUT/$name.mutated.txt" 2>&1; local b=$?
  local v="OK"; [ $a -ne 0 ] && v="RED — the gate itself is failing"; [ $b -eq 0 ] && v="BROKEN CONTROL — the mutation did NOT go red"
  printf '  %-22s normal=%d  mutated(%s)=%d   %s\n' "$name" "$a" "$mut" "$b" "$v"
  if [ $a -ne 0 ] || [ $b -eq 0 ]; then rc_all=1; fi; return 0
}
echo "══ fb618 GATE ══  installed AU carrying aumu/Tern/Wvcr:"
python3 - <<'PY'
import os, glob, plistlib, datetime
hits = []
for root in (os.path.expanduser('~/Library/Audio/Plug-Ins/Components'), '/Library/Audio/Plug-Ins/Components'):
    for p in sorted(glob.glob(os.path.join(root, '*.component'))):
        try:
            with open(os.path.join(p, 'Contents', 'Info.plist'), 'rb') as f: pl = plistlib.load(f)
            ac = (pl.get('AudioComponents') or [{}])[0]
        except Exception: continue
        if (ac.get('type'), ac.get('subtype'), ac.get('manufacturer')) == ('aumu', 'Tern', 'Wvcr'):
            exe = os.path.join(p, 'Contents', 'MacOS', pl.get('CFBundleExecutable', ''))
            t = datetime.datetime.fromtimestamp(os.path.getmtime(exe if os.path.exists(exe) else p))
            hits.append((p, t.strftime('%b %d %H:%M')))
for n, t in hits: print(f'     {n}   binary {t}')
if not hits: print('     NONE INSTALLED — the certs cannot open the plugin.')
if len(hits) > 1: print('     ⚠️  MORE THAN ONE bundle claims aumu/Tern/Wvcr — delete the stale one before trusting a green bar.')
PY
echo
for c in preset_null_cert preset_roundtrip_cert; do
  c++ -std=c++17 -O2 -I Tests -I Tests/shim -I Source "Tests/$c.cpp" \
      -framework Accelerate -framework AudioToolbox -framework CoreFoundation -o "$OUT/$c" \
      || { echo "  $c: COMPILE FAIL"; rc_all=1; }
done
run null:inherit   PN_MUT=inherit "$OUT/preset_null_cert"
run null:drift     PN_MUT=drift   "$OUT/preset_null_cert"
run roundtrip      RT_MUT=stale   "$OUT/preset_roundtrip_cert"
echo; echo "  full output: $OUT"; exit $rc_all
