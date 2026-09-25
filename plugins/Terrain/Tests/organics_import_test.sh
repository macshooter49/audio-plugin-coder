#!/usr/bin/env bash
# ══════════════════════════════════════════════════════════════════════════════════════════════
#  tp108 — THE USER SOUNDFONT IMPORT: gates for Source/organics/OrganicsImport.cpp.
#
#    bash Tests/organics_import_test.sh           # from plugins/Terrain (or anywhere)
#
#  1  Tests/organics_import_fixtures.py writes the fixtures into a temp dir with a UNICODE name: the SFZ round-trip
#     fixture (Tests/fixtures/organics/sfz-src: sines, 2 velocity layers with an authored loop, a keyswitch, a
#     sequential round robin, a release region, #define/#include/default_path/note names), a generated SF2 (the
#     compiler test's two zones), a 3-preset SF2 with a stereo pair, an SF3 (Ogg Vorbis samples) and the bad files;
#     then compiles the SFZ and the SF2 with the OFFLINE compiler (Tools/organics/torgc.py) for the reference maps.
#  2  the C++ test binary (juce_core/audio_basics/audio_formats/events + OrganicsImport/Library/Engine) imports them
#     into a temp library root and checks the result, the pitch through OrganicEngine, reload by id, the job API,
#     Remove, and every error path.
#  3  Tests/organics_import_compare.py compares the C++ map.json with torgc's field by field (tolerances printed).
#  Exit 0 only when every bar passes.
# ══════════════════════════════════════════════════════════════════════════════════════════════
set -u
cd "$(dirname "$0")/.." || exit 2                       # plugins/Terrain
REPO="$(cd ../.. && pwd)"
J="${JUCE_MODULES:-$REPO/_tools/JUCE/modules}"
[ -f "$J/juce_core/juce_core.h" ] || J="$HOME/Developer/VST-Plugins/audio-plugin-coder/.worktrees/terrain-instrument/_tools/JUCE/modules"
[ -f "$J/juce_core/juce_core.h" ] || { echo "JUCE modules not found (set JUCE_MODULES)"; exit 2; }
OUT="${ORG_OUT:-$REPO/build/organics_import_test}"; mkdir -p "$OUT"
CXX="${CXX:-c++}"
DEFS="-DNDEBUG=1 -DJUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1 -DJUCE_STANDALONE_APPLICATION=1 -DJUCE_MODAL_LOOPS_PERMITTED=1 \
 -DJUCE_MODULE_AVAILABLE_juce_core=1 -DJUCE_MODULE_AVAILABLE_juce_audio_basics=1 -DJUCE_MODULE_AVAILABLE_juce_audio_formats=1 \
 -DJUCE_MODULE_AVAILABLE_juce_events=1 -DJUCE_USE_FLAC=1 -DJUCE_USE_OGGVORBIS=1 -DJUCE_USE_MP3AUDIOFORMAT=0 \
 -DJUCE_USE_LAME_AUDIO_FORMAT=0 -DJUCE_USE_WINDOWS_MEDIA_FORMAT=0"
FLAGS="-std=c++17 -O2 $DEFS -I $J -I Source -I Source/organics"
FW="-framework Foundation -framework CoreFoundation -framework Cocoa -framework IOKit -framework Security -framework Accelerate -framework AudioToolbox -framework CoreAudio -framework CoreMIDI"

echo "══ ORGANICS IMPORT TEST — build ══"
for m in juce_core juce_audio_basics juce_audio_formats juce_events; do
  if [ ! -f "$OUT/$m.o" ]; then
    $CXX $FLAGS -x objective-c++ -c "$J/$m/$m.mm" -o "$OUT/$m.o" 2> "$OUT/$m.log" || { echo "  COMPILE FAIL $m — first error:"; grep -m1 'error' "$OUT/$m.log"; exit 1; }
  fi
done
for s in Source/organics/OrganicsImport.cpp Source/organics/OrganicsLibrary.cpp Source/organics/OrganicEngine.cpp Tests/organics_import_test.cpp Tests/juce_compdate_stub.cpp; do
  o="$OUT/$(basename "${s%.cpp}").o"
  $CXX $FLAGS -Wall -Wextra -Wno-unused-parameter -c "$s" -o "$o" 2> "$o.log" || { echo "  COMPILE FAIL $s — first error:"; grep -m1 'error' "$o.log"; exit 1; }
  if grep -q 'warning' "$o.log"; then echo "  warnings in $s:"; grep 'warning' "$o.log" | head -5; fi
done
$CXX "$OUT"/*.o $FW -o "$OUT/organics_import_test" 2> "$OUT/link.log" || { echo "  LINK FAIL — first error:"; grep -m1 -i 'error\|undefined' "$OUT/link.log"; exit 1; }
[ "${1:-}" = "--build-only" ] && exit 0

TMP="$(mktemp -d "${TMPDIR:-/tmp}/orgimport.XXXXXX")"
trap 'rm -rf "$TMP"' EXIT
echo "══ fixtures + the offline compiler's reference maps ══"
python3 Tests/organics_import_fixtures.py "$TMP" || { echo "  FIXTURES FAIL"; exit 1; }
echo "══ the C++ import ══"
"$OUT/organics_import_test" "$TMP"; rc1=$?
echo "══ map.json vs torgc.py ══"
python3 Tests/organics_import_compare.py "$TMP"; rc2=$?
if [ $rc1 -eq 0 ] && [ $rc2 -eq 0 ]; then echo "ORGANICS IMPORT: ALL PASS"; exit 0; fi
echo "ORGANICS IMPORT: FAIL (import $rc1, compare $rc2)"; exit 1
