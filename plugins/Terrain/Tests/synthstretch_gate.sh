#!/bin/bash
# SYNTH Sample-osc stretch/warp click harness (Tests/synthstretch_cert.cpp) — the synth twin of chopstretch_gate.sh,
# linked against the built SharedCode (the settings_perf_cert.sh recipe), so it measures the shipping processBlock.
#
#   bash Tests/synthstretch_gate.sh build <binary>      build the harness against the CURRENT build/ tree
#   bash Tests/synthstretch_gate.sh build-base <binary> build it against BASE_LIB + BASE_SRC (below)
#   <binary> cpu                                        CPU per warped voice (8-note chord, processBlock timing)
#   bash Tests/synthstretch_gate.sh [FILTER]            build + selftest + run the working tree, compare against BEFORE:
#        BEFORE=<tsv>                   a before.tsv already rendered, or
#        BEFORE_BIN=<bin>               a harness binary linked against the base commit (rendered here), or
#        BASE_LIB=<dir> BASE_SRC=<dir>  the base commit's libTerrain_SharedCode.a + libTerrain_WebUI.a (copied out of
#                                       its build) and its source tree (git archive <rev> plugins/Terrain | tar -x -C <dir>):
#                                       the before binary is compiled against THOSE headers (the voice's layout changes)
#   Build Terrain_VST3 first. TERRAIN_BUILD overrides the build dir. OUT=<dir> for the tsv files.
set -e
HERE=$(cd "$(dirname "$0")" && pwd); ROOT=$(cd "$HERE/../../.." && pwd); B=${TERRAIN_BUILD:-$ROOT/build}/plugins/Terrain
F=$B/CMakeFiles/Terrain.dir/flags.make
OUT=${OUT:-${TMPDIR:-/tmp}/synthstretch}; mkdir -p "$OUT"
[ -f "$B/Terrain_artefacts/Release/libTerrain_SharedCode.a" ] || { echo "build Terrain first (no libTerrain_SharedCode.a)"; exit 2; }
FW="-framework Accelerate -framework AudioToolbox -framework Cocoa -framework CoreAudio -framework CoreAudioKit -framework CoreMIDI -framework DiscRecording -framework Foundation -framework IOKit -framework QuartzCore -framework Security -framework WebKit -weak_framework Metal -weak_framework MetalKit"
DEF=$(grep '^CXX_DEFINES' "$F" | sed 's/^CXX_DEFINES = //'); INC=$(grep '^CXX_INCLUDES' "$F" | sed 's/^CXX_INCLUDES = //')
build() {   # $1 = binary, $2 = source tree root (holds plugins/Terrain), $3 = SharedCode lib, $4 = WebUI lib
  local inc="${INC//$ROOT\/plugins\/Terrain/$2/plugins/Terrain}"
  eval clang++ -std=gnu++20 -O2 -arch arm64 -fvisibility=hidden -fvisibility-inlines-hidden -w $DEF $inc -c "$HERE/synthstretch_cert.cpp" -o "$1.o"
  eval clang++ -arch arm64 -flto "$1.o" "$3" "$4" $FW -o "$1"
}
if [ "$1" = "build" ]; then build "$2" "$ROOT" "$B/Terrain_artefacts/Release/libTerrain_SharedCode.a" "$B/libTerrain_WebUI.a"; exit 0; fi
if [ "$1" = "build-base" ]; then build "$2" "$BASE_SRC" "$BASE_LIB/libTerrain_SharedCode.a" "$BASE_LIB/libTerrain_WebUI.a"; exit 0; fi
FILTER="$1"
build "$OUT/ss_after" "$ROOT" "$B/Terrain_artefacts/Release/libTerrain_SharedCode.a" "$B/libTerrain_WebUI.a"
"$OUT/ss_after" selftest 2>&1 | grep -v '^Terrain:'
if [ -z "$BEFORE" ] && [ -z "$BEFORE_BIN" ] && [ -n "$BASE_LIB" ] && [ -n "$BASE_SRC" ]; then
  build "$OUT/ss_before" "$BASE_SRC" "$BASE_LIB/libTerrain_SharedCode.a" "$BASE_LIB/libTerrain_WebUI.a"; BEFORE_BIN="$OUT/ss_before"
fi
if [ -n "$BEFORE_BIN" ]; then "$BEFORE_BIN" run "$OUT/before.tsv" $FILTER 2>&1 | grep -v '^Terrain:'; BEFORE="$OUT/before.tsv"; fi
"$OUT/ss_after" run "$OUT/after.tsv" $FILTER 2>&1 | grep -v '^Terrain:'
if [ -n "$BEFORE" ]; then "$OUT/ss_after" compare "$BEFORE" "$OUT/after.tsv" 2>&1 | grep -v '^Terrain:'; fi
exit 0
