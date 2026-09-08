#!/usr/bin/env bash
# ══════════════════════════════════════════════════════════════════════════════════════════════
#  fb602_gates.sh — every fb602 gate, each run TWICE: normal, then with its mutation control.
#
#    bash Tests/fb602_gates.sh          # from plugins/Terrain
#
#  A green bar that cannot go red is not a gate, so this runner refuses to call a gate OK unless
#  the mutated run of that same gate FAILED. It prints the pair for every one.
#
#  ⚠️ The AU-driving gates measure the AU INSTALLED in ~/Library/Audio/Plug-Ins/Components, NOT the
#     build tree. Rebuild and reinstall before trusting them; the header line prints its date.
#  ⚠️ preset_path_cert and delete_dispatch_cert do NOT build on the standard cert line:
#     Tests/shim/juce_core/juce_core.h is a six-line stub with no juce::File, so they link the REAL
#     juce_core module. That line is below, with the flags spelled out rather than held in a shell
#     variable — an unquoted $VAR of flags is ONE argument in zsh (no word splitting), which
#     silently drops every -D and makes juce_core fail with "No global header file was included!".
# ══════════════════════════════════════════════════════════════════════════════════════════════
set -u
OUT="${TI_GATE_OUT:-/private/tmp/claude-501/-Users-macshooter/941a8123-ffc6-4f73-84a3-70aee55ea3c3/scratchpad/fb602/gates}"
REPO="$(cd ../.. && pwd)"
mkdir -p "$OUT"
rc_all=0

run () {  # label  mutation-env  command...
  local name="$1" mut="$2"; shift 2
  "$@"                  > "$OUT/$name.normal.txt"  2>&1; local a=$?
  env "$mut" "$@"       > "$OUT/$name.mutated.txt" 2>&1; local b=$?
  local verdict="OK"
  [ $a -ne 0 ] && verdict="RED — the gate itself is failing"
  [ $b -eq 0 ] && verdict="BROKEN CONTROL — the mutation did NOT go red"
  printf '  %-20s normal=%d  mutated(%-18s)=%d   %s\n' "$name" "$a" "$mut" "$b" "$verdict"
  if [ $a -ne 0 ] || [ $b -eq 0 ]; then rc_all=1; fi
  return 0
}

# fb605 — NAME THE BUNDLE THE GATES ACTUALLY OPEN, NOT A BUNDLE NAMED AFTER THE PRODUCT.
# The AU certs below find the plugin by its CODES (aumu/Tern/Wvcr), never by bundle name, and the
# rename made those two different things: on this machine "Terrain.component" was a stale FX build
# (aufx/Trrn) while the synth was still installed as "Terrain Instrument.component". A header that
# dates the wrong bundle is worse than no header — it invites you to trust a stale binary.
echo "══ fb602 GATES ══  installed AU carrying aumu/Tern/Wvcr:"
python3 - <<'PY'
import os, glob, plistlib, datetime
comp = os.path.expanduser('~/Library/Audio/Plug-Ins/Components')
hits = []
for p in sorted(glob.glob(os.path.join(comp, '*.component'))):
    ip = os.path.join(p, 'Contents', 'Info.plist')
    try:
        with open(ip, 'rb') as f: pl = plistlib.load(f)
        ac = (pl.get('AudioComponents') or [{}])[0]
    except Exception: continue
    if (ac.get('type'), ac.get('subtype'), ac.get('manufacturer')) == ('aumu', 'Tern', 'Wvcr'):
        exe = os.path.join(p, 'Contents', 'MacOS', pl.get('CFBundleExecutable', ''))
        t = datetime.datetime.fromtimestamp(os.path.getmtime(exe if os.path.exists(exe) else p))
        hits.append((os.path.basename(p), t.strftime('%b %d %H:%M')))
if not hits:
    print('     NONE INSTALLED — every AU cert below will fail to open the plugin.')
for n, t in hits:
    print(f'     {n}   binary {t}')
if len(hits) > 1:
    print('     ⚠️  MORE THAN ONE bundle claims aumu/Tern/Wvcr. The host picks one of them and')
    print('         you do not get to say which. Delete the stale one before trusting a green bar.')
PY
echo

# ── the AU-driving certs: the standard cert line + AudioToolbox/CoreFoundation ────────────────
for c in headless_restore_au state_holes_au noise_restore_au; do
  c++ -std=c++17 -O2 -I Tests -I Tests/shim -I Source "Tests/$c.cpp" \
      -framework Accelerate -framework AudioToolbox -framework CoreFoundation -o "$OUT/$c" \
      || { echo "  $c: COMPILE FAIL"; rc_all=1; }
done

# ── the two editor certs: REAL juce_core, and the helpers sliced out of the shipping source ───
python3 Tests/extract_helpers.py Source/PluginEditor.cpp "$OUT/ti_helpers_extracted.h" > /dev/null || rc_all=1
for c in preset_path_cert delete_dispatch_cert; do
  c++ -std=c++17 -O2 -DNDEBUG=1 -DJUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1 \
      -DJUCE_STANDALONE_APPLICATION=1 -DJUCE_MODULE_AVAILABLE_juce_core=1 \
      -I"$REPO/_tools/JUCE/modules" -I"$OUT" \
      "Tests/$c.cpp" Tests/juce_compdate_stub.cpp "$REPO/_tools/JUCE/modules/juce_core/juce_core.mm" \
      -framework Foundation -framework CoreFoundation -framework Cocoa -framework IOKit -framework Security \
      -o "$OUT/$c" || { echo "  $c: COMPILE FAIL"; rc_all=1; }
done

run headless_restore  TI_RESTORE_MUT=1     "$OUT/headless_restore_au"
run state_holes       TI_HOLES_MUT=1       "$OUT/state_holes_au"
run noise_restore     TI_NOISE_MUT=1       "$OUT/noise_restore_au"
run preset_path       TI_CERT_MUTATE=slug  "$OUT/preset_path_cert"
run preset_path_miss  TI_CERT_MUTATE=miss  "$OUT/preset_path_cert"
run delete_dispatch   TI_CERT_MUTATE=collide "$OUT/delete_dispatch_cert"
run native_dupes      TI_CERT_MUTATE=1     python3 Tests/cert_native_dupes.py
run user_data         TI_DATA_MUT=1        python3 Tests/user_data_guard.py

# ── the red path that needs no mutation at all: the SHIPPED collision, in the SHIPPED file ────
echo
echo "  cert_native_dupes.py against 5a2ba1c (pre-fb602) — both sections must find the real bug:"
# fb605 — plugins/TerrainInstrument is where this file LIVED AT 5a2ba1c. A git path is a fact
# about a commit, not a name: rewriting it to plugins/Terrain makes `git show` return nothing and
# the PRE/POST diff below silently compares against an empty file.
git -C "$REPO" show 5a2ba1c:plugins/TerrainInstrument/Source/PluginEditor.cpp > "$OUT/PluginEditor.PRE.cpp" 2>/dev/null
python3 Tests/cert_native_dupes.py "$OUT/PluginEditor.PRE.cpp" > "$OUT/native_dupes.pre.txt" 2>&1
if grep -q 'DUPLICATE "deletePreset"' "$OUT/native_dupes.pre.txt" && grep -q 'NEW    "deletePreset"' "$OUT/native_dupes.pre.txt"; then
  echo "    OK   A: $(grep 'DUPLICATE "deletePreset"' "$OUT/native_dupes.pre.txt" | sed 's/^ *//')"
  echo "    OK   B: $(grep 'NEW    "deletePreset"'   "$OUT/native_dupes.pre.txt" | sed 's/^ *//')"
else
  echo "    BROKEN — the gate no longer finds the collision in 5a2ba1c"; rc_all=1
fi

echo
echo "  full output: $OUT"
exit $rc_all
