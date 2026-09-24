#!/usr/bin/env bash
# ══════════════════════════════════════════════════════════════════════════════════════════════
#  THE ORGANICS ENGINE — runtime DSP gates (Agent B, tp104). Builds ONLY the standalone test binary
#  (juce_core + juce_audio_basics + juce_audio_formats + juce_events, no plugin, no GUI) in Release
#  (-O3, the CPU bar is a Release number), then runs it against Tests/fixtures/organics.
#
#    bash Tests/organics_engine_test.sh            # from plugins/Terrain (or anywhere)
#    ORG_REGEN=1 bash Tests/organics_engine_test.sh   # rewrite the generated fixtures first
#    ORG_REAL=0 bash Tests/organics_engine_test.sh    # skip the real-data bars (Agent A's compiled library)
#
#  Exit 0 only when every bar prints PASS. Module objects are cached in $ORG_OUT (default
#  <repo>/build/organics_engine_test); the three organics sources recompile every run.
# ══════════════════════════════════════════════════════════════════════════════════════════════
set -u
cd "$(dirname "$0")/.." || exit 2                       # plugins/Terrain
REPO="$(cd ../.. && pwd)"
J="${JUCE_MODULES:-$REPO/_tools/JUCE/modules}"
[ -f "$J/juce_core/juce_core.h" ] || J="$HOME/Developer/VST-Plugins/audio-plugin-coder/.worktrees/terrain-instrument/_tools/JUCE/modules"
[ -f "$J/juce_core/juce_core.h" ] || { echo "JUCE modules not found (set JUCE_MODULES)"; exit 2; }
OUT="${ORG_OUT:-$REPO/build/organics_engine_test}"; mkdir -p "$OUT"
CXX="${CXX:-c++}"
DEFS="-DNDEBUG=1 -DJUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1 -DJUCE_STANDALONE_APPLICATION=1 -DJUCE_MODAL_LOOPS_PERMITTED=1 \
 -DJUCE_MODULE_AVAILABLE_juce_core=1 -DJUCE_MODULE_AVAILABLE_juce_audio_basics=1 -DJUCE_MODULE_AVAILABLE_juce_audio_formats=1 \
 -DJUCE_MODULE_AVAILABLE_juce_events=1 -DJUCE_USE_FLAC=1 -DJUCE_USE_OGGVORBIS=0 -DJUCE_USE_MP3AUDIOFORMAT=0 \
 -DJUCE_USE_LAME_AUDIO_FORMAT=0 -DJUCE_USE_WINDOWS_MEDIA_FORMAT=0"
FLAGS="-std=c++17 -O3 $DEFS -I $J -I Source -I Source/organics"
FW="-framework Foundation -framework CoreFoundation -framework Cocoa -framework IOKit -framework Security -framework Accelerate -framework AudioToolbox -framework CoreAudio -framework CoreMIDI"

echo "══ ORGANICS ENGINE TEST — build ══"
for m in juce_core juce_audio_basics juce_audio_formats juce_events; do
  if [ ! -f "$OUT/$m.o" ]; then
    $CXX $FLAGS -x objective-c++ -c "$J/$m/$m.mm" -o "$OUT/$m.o" 2> "$OUT/$m.log" || { echo "  COMPILE FAIL $m — first error:"; grep -m1 'error' "$OUT/$m.log"; exit 1; }
  fi
done
for s in Source/organics/OrganicsLibrary.cpp Source/organics/OrganicEngine.cpp Source/organics/OrganicEngine_test.cpp Tests/juce_compdate_stub.cpp; do
  o="$OUT/$(basename "${s%.cpp}").o"
  $CXX $FLAGS -Wall -Wextra -Wno-unused-parameter -c "$s" -o "$o" 2> "$o.log" || { echo "  COMPILE FAIL $s — first error:"; grep -m1 'error' "$o.log"; exit 1; }
  if grep -q 'warning' "$o.log"; then echo "  warnings in $s:"; grep 'warning' "$o.log" | head -5; fi
done
$CXX "$OUT"/*.o $FW -o "$OUT/organics_engine_test" 2> "$OUT/link.log" || { echo "  LINK FAIL — first error:"; grep -m1 -i 'error\|undefined' "$OUT/link.log"; exit 1; }

# tp105 NO-SILENCE sweep: every installed instrument × artic × key (range ± ORG_SWEEP_MARGIN, default 12) × vel {20,64,100,127}
#   × 8 presses must sound (−60 dBFS within 30 ms + Human timing + the region's authored onset).
#   bash Tests/organics_engine_test.sh --sweep [root]   (root default: $TERRAIN_ORGANICS_DIR, else ~/Library/WavesCrate/TerrainInstrument/Organics)
if [ "${1:-}" = "--sweep" ]; then
  SR="${2:-${TERRAIN_ORGANICS_DIR:-$HOME/Library/WavesCrate/TerrainInstrument/Organics}}"
  [ -d "$SR" ] || SR="$HOME/Library/WavesCrate/Terrain/Organics"
  exec "$OUT/organics_engine_test" --sweep "$SR" "${ORG_SWEEP_MARGIN:-12}"
fi
FIX="Tests/fixtures/organics"
if [ "${ORG_REGEN:-0}" = "1" ]; then "$OUT/organics_engine_test" --gen "$FIX" || exit 1; fi
# Agent A's compiled library (outside git): the real-data bars run when it is present (ORG_REAL=0 skips them)
REAL="${ORG_REAL_DIR:-$HOME/Developer/VST-Plugins/organics-library/compiled}"
if [ "${ORG_REAL:-1}" = "1" ] && [ -f "$REAL/salamander.grand.v3/map.json" ]; then
  "$OUT/organics_engine_test" "$FIX" "$REAL"
else
  "$OUT/organics_engine_test" "$FIX"
fi
