#!/usr/bin/env bash
# ══════════════════════════════════════════════════════════════════════════════════════════════
#  fb617 — THE SERIALISER IS A FIXED POINT. The gate every .terrain stands behind.
#
#    bash Tests/fb617_gates.sh          # from plugins/Terrain
#
#  preset_roundtrip_cert.cpp drives the INSTALLED AU (aumu/Tern/Wvcr): save → load → save must be
#  byte-identical, an empty slice list must not echo a root slicesJson, and a pre-fix session that
#  carries two <layers> children must load the NEWEST and save exactly one. Run twice: normal, then
#  with RT_MUT=stale, which expects the old behaviour and must go RED — a green bar that cannot go
#  red is not a gate.
# ══════════════════════════════════════════════════════════════════════════════════════════════
set -u
OUT="${TI_GATE_OUT:-$(mktemp -d /tmp/fb617.XXXXXX)}"; mkdir -p "$OUT"; rc_all=0
run () {  # label  mutation-env  command...
  local name="$1" mut="$2"; shift 2
  "$@"            > "$OUT/$name.normal.txt"  2>&1; local a=$?
  env "$mut" "$@" > "$OUT/$name.mutated.txt" 2>&1; local b=$?
  local v="OK"; [ $a -ne 0 ] && v="RED — the gate itself is failing"; [ $b -eq 0 ] && v="BROKEN CONTROL — the mutation did NOT go red"
  printf '  %-22s normal=%d  mutated(%s)=%d   %s\n' "$name" "$a" "$mut" "$b" "$v"
  if [ $a -ne 0 ] || [ $b -eq 0 ]; then rc_all=1; fi; return 0
}
echo "══ fb617 GATE ══  installed AU carrying aumu/Tern/Wvcr:"
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
if not hits: print('     NONE INSTALLED — the cert cannot open the plugin.')
if len(hits) > 1: print('     ⚠️  MORE THAN ONE bundle claims aumu/Tern/Wvcr — delete the stale one before trusting a green bar.')
PY
echo
c++ -std=c++17 -O2 -I Tests -I Tests/shim -I Source Tests/preset_roundtrip_cert.cpp \
    -framework Accelerate -framework AudioToolbox -framework CoreFoundation -o "$OUT/preset_roundtrip_cert" \
    || { echo "  preset_roundtrip_cert: COMPILE FAIL"; exit 1; }
run preset_roundtrip RT_MUT=stale "$OUT/preset_roundtrip_cert"
echo; echo "  full output: $OUT"; exit $rc_all
