#!/bin/bash
# tp104 — build + run Tests/organics_integration_cert.cpp against the built SharedCode (an LTO archive), with the
# Organics library pointed at the frozen fixture (Tests/fixtures/organics). The expression_midi.sh recipe.
#   Tests/organics_integration.sh            (build Terrain_VST3 / Terrain_AU first). Exit 0 = PASS.
# With the stub linked (the runtime not merged yet) the sound bars FAIL by construction — that is the point of them.
set -e
HERE=$(cd "$(dirname "$0")" && pwd); ROOT=$(cd "$HERE/../../.." && pwd); B=${TERRAIN_BUILD:-$ROOT/build}/plugins/Terrain
F=$B/CMakeFiles/Terrain.dir/flags.make; OUT=${TMPDIR:-/tmp}/tp104_orgint/Fake.bundle
[ -f "$B/Terrain_artefacts/Release/libTerrain_SharedCode.a" ] || { echo "build Terrain first"; exit 2; }
mkdir -p "$OUT/Contents/MacOS" "$OUT/Contents/Resources"; ln -sfn "$HERE/../Resources/Wavetables" "$OUT/Contents/Resources/Wavetables"
DEF=$(grep '^CXX_DEFINES' "$F" | sed 's/^CXX_DEFINES = //'); INC=$(grep '^CXX_INCLUDES' "$F" | sed 's/^CXX_INCLUDES = //')
FW="-framework Accelerate -framework AudioToolbox -framework Cocoa -framework CoreAudio -framework CoreAudioKit -framework CoreMIDI -framework DiscRecording -framework Foundation -framework IOKit -framework QuartzCore -framework Security -framework WebKit -weak_framework Metal -weak_framework MetalKit"
eval clang++ -std=gnu++20 -O2 -arch arm64 -fvisibility=hidden -fvisibility-inlines-hidden -w $DEF $INC -c "$HERE/organics_integration_cert.cpp" -o "$OUT/../t.o"
eval clang++ -arch arm64 -flto "$OUT/../t.o" "$B/Terrain_artefacts/Release/libTerrain_SharedCode.a" "$B/libTerrain_WebUI.a" $FW -Wl,-U,__ZN2tw14organics_debug17lastRenderReadersEv -Wl,-U,__ZN2tw14organics_debug15lastLiveReadersEv -o "$OUT/Contents/MacOS/organics_integration_cert"   # the runtime hooks are weak (absent with the stub)
export TERRAIN_ORGANICS_DIR="${TERRAIN_ORGANICS_DIR:-$HERE/fixtures/organics}"
"$OUT/Contents/MacOS/organics_integration_cert" "$@" 2>&1 | grep -v '^Terrain:'
exit ${PIPESTATUS[0]}
