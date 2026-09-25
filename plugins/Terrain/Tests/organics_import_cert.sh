#!/bin/bash
# tp108 — build + run Tests/organics_import_cert.cpp against the built SharedCode (the organics_integration.sh recipe):
# the user SoundFont import on the shipping processor — play, state save → reload by id, the int alone, Remove.
#   Tests/organics_import_cert.sh        (build Terrain_VST3 / Terrain_AU first). Exit 0 = PASS.
set -e
HERE=$(cd "$(dirname "$0")" && pwd); ROOT=$(cd "$HERE/../../.." && pwd); B=${TERRAIN_BUILD:-$ROOT/build}/plugins/Terrain
F=$B/CMakeFiles/Terrain.dir/flags.make; OUT=${TMPDIR:-/tmp}/tp108_orgimp/Fake.bundle
[ -f "$B/Terrain_artefacts/Release/libTerrain_SharedCode.a" ] || { echo "build Terrain first"; exit 2; }
mkdir -p "$OUT/Contents/MacOS" "$OUT/Contents/Resources"; ln -sfn "$HERE/../Resources/Wavetables" "$OUT/Contents/Resources/Wavetables"
DEF=$(grep '^CXX_DEFINES' "$F" | sed 's/^CXX_DEFINES = //'); INC=$(grep '^CXX_INCLUDES' "$F" | sed 's/^CXX_INCLUDES = //')
FW="-framework Accelerate -framework AudioToolbox -framework Cocoa -framework CoreAudio -framework CoreAudioKit -framework CoreMIDI -framework DiscRecording -framework Foundation -framework IOKit -framework QuartzCore -framework Security -framework WebKit -weak_framework Metal -weak_framework MetalKit"
eval clang++ -std=gnu++20 -O2 -arch arm64 -fvisibility=hidden -fvisibility-inlines-hidden -w $DEF $INC -c "$HERE/organics_import_cert.cpp" -o "$OUT/../t.o"
eval clang++ -arch arm64 -flto "$OUT/../t.o" "$B/Terrain_artefacts/Release/libTerrain_SharedCode.a" "$B/libTerrain_WebUI.a" $FW -o "$OUT/Contents/MacOS/organics_import_cert"
LIB=$(mktemp -d "${TMPDIR:-/tmp}/orgimpcert.XXXXXX"); trap 'rm -rf "$LIB"' EXIT
cp "$HERE/fixtures/organics/index.json" "$HERE/fixtures/organics/ids.json" "$LIB/"; cp -R "$HERE/fixtures/organics/test.sine" "$LIB/"
"$OUT/Contents/MacOS/organics_import_cert" "$LIB" "$HERE/fixtures/organics/sfz-src/fixture.sfz" 2>&1 | grep -v '^Terrain:'
exit ${PIPESTATUS[0]}
