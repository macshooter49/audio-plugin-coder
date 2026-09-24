#!/bin/bash
# tp104 — build + run Tests/organics_null.cpp against the built SharedCode (an LTO archive), the expression_midi.sh recipe.
#   Tests/organics_null.sh write <dir> [bankDir]    — on the build BEFORE the Organics integration
#   Tests/organics_null.sh check <dir> [bankDir]    — on the build AFTER it; exit 1 on any differing sample
# Build Terrain_VST3 / Terrain_AU first. TERRAIN_BUILD overrides the build dir.
set -e
HERE=$(cd "$(dirname "$0")" && pwd); ROOT=$(cd "$HERE/../../.." && pwd); B=${TERRAIN_BUILD:-$ROOT/build}/plugins/Terrain
F=$B/CMakeFiles/Terrain.dir/flags.make; OUT=${TMPDIR:-/tmp}/tp104_orgnull/Fake.bundle
[ -f "$B/Terrain_artefacts/Release/libTerrain_SharedCode.a" ] || { echo "build Terrain first"; exit 2; }
mkdir -p "$OUT/Contents/MacOS" "$OUT/Contents/Resources"; ln -sfn "$HERE/../Resources/Wavetables" "$OUT/Contents/Resources/Wavetables"
DEF=$(grep '^CXX_DEFINES' "$F" | sed 's/^CXX_DEFINES = //'); INC=$(grep '^CXX_INCLUDES' "$F" | sed 's/^CXX_INCLUDES = //')
FW="-framework Accelerate -framework AudioToolbox -framework Cocoa -framework CoreAudio -framework CoreAudioKit -framework CoreMIDI -framework DiscRecording -framework Foundation -framework IOKit -framework QuartzCore -framework Security -framework WebKit -weak_framework Metal -weak_framework MetalKit"
eval clang++ -std=gnu++20 -O2 -arch arm64 -fvisibility=hidden -fvisibility-inlines-hidden -w $DEF $INC -c "$HERE/organics_null.cpp" -o "$OUT/../t.o"
eval clang++ -arch arm64 -flto "$OUT/../t.o" "$B/Terrain_artefacts/Release/libTerrain_SharedCode.a" "$B/libTerrain_WebUI.a" $FW -o "$OUT/Contents/MacOS/organics_null"
"$OUT/Contents/MacOS/organics_null" "$@" 2>&1 | grep -v '^Terrain:'
exit ${PIPESTATUS[0]}
