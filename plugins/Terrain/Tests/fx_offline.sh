#!/bin/bash
# tpfx — build + run Tests/fx_offline.cpp against the built TerrainFX SharedCode (the organics_null.sh recipe, FX flags).
#   Tests/fx_offline.sh        (build TerrainFX_VST3 or TerrainFX_AU first; TERRAIN_BUILD overrides the build dir)
set -e
HERE=$(cd "$(dirname "$0")" && pwd); ROOT=$(cd "$HERE/../../.." && pwd); B=${TERRAIN_BUILD:-$ROOT/build}/plugins/Terrain
F=$B/CMakeFiles/TerrainFX.dir/flags.make; OUT=${TMPDIR:-/tmp}/tpfx_offline/Fake.bundle
LIB="$B/TerrainFX_artefacts/Release/libTerrain FX_SharedCode.a"   # JUCE names it after PRODUCT_NAME, space included
[ -f "$LIB" ] || { echo "build TerrainFX first"; exit 2; }
mkdir -p "$OUT/Contents/MacOS" "$OUT/Contents/Resources"; ln -sf "$LIB" "$OUT/../libfx.a"
DEF=$(grep '^CXX_DEFINES' "$F" | sed 's/^CXX_DEFINES = //'); INC=$(grep '^CXX_INCLUDES' "$F" | sed 's/^CXX_INCLUDES = //')
FW="-framework Accelerate -framework AudioToolbox -framework Cocoa -framework CoreAudio -framework CoreAudioKit -framework CoreMIDI -framework DiscRecording -framework Foundation -framework IOKit -framework QuartzCore -framework Security -framework WebKit -weak_framework Metal -weak_framework MetalKit"
eval clang++ -std=gnu++20 -O2 -arch arm64 -fvisibility=hidden -fvisibility-inlines-hidden -w $DEF $INC -c "$HERE/fx_offline.cpp" -o "$OUT/../t.o"
eval clang++ -arch arm64 -flto "$OUT/../t.o" "$OUT/../libfx.a" "$B/libTerrain_WebUI.a" $FW -o "$OUT/Contents/MacOS/fx_offline"
"$OUT/Contents/MacOS/fx_offline" "$@" 2>&1 | grep -v '^Terrain:'
exit ${PIPESTATUS[0]}
