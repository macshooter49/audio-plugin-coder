#!/bin/bash
# fb642 — build + run formant_latency_fb642.cpp against the built SharedCode (LTO archive), inside a fake bundle whose
# Contents/Resources/Wavetables points at the repo's library, so wtFactoryRoot() resolves exactly as it does in the plugin.
set -e
HERE=$(cd "$(dirname "$0")" && pwd); ROOT=$(cd "$HERE/../../.." && pwd); B=$ROOT/build/plugins/Terrain
F=$B/CMakeFiles/Terrain.dir/flags.make; OUT=${TMPDIR:-/tmp}/fb642_frm/Fake.bundle
[ -f "$B/Terrain_artefacts/Release/libTerrain_SharedCode.a" ] || { echo "build Terrain first"; exit 2; }
mkdir -p "$OUT/Contents/MacOS" "$OUT/Contents/Resources"; ln -sfn "$HERE/../Resources/Wavetables" "$OUT/Contents/Resources/Wavetables"
DEF=$(grep '^CXX_DEFINES' "$F" | sed 's/^CXX_DEFINES = //'); INC=$(grep '^CXX_INCLUDES' "$F" | sed 's/^CXX_INCLUDES = //')
FW="-framework Accelerate -framework AudioToolbox -framework Cocoa -framework CoreAudio -framework CoreAudioKit -framework CoreMIDI -framework DiscRecording -framework Foundation -framework IOKit -framework QuartzCore -framework Security -framework WebKit -weak_framework Metal -weak_framework MetalKit"
eval clang++ -std=gnu++20 -O2 -arch arm64 -fvisibility=hidden -fvisibility-inlines-hidden -w $DEF $INC -c "$HERE/formant_latency_fb642.cpp" -o "$OUT/../t.o"
eval clang++ -arch arm64 -flto "$OUT/../t.o" "$B/Terrain_artefacts/Release/libTerrain_SharedCode.a" "$B/libTerrain_WebUI.a" $FW -o "$OUT/Contents/MacOS/formant_latency_fb642"
"$OUT/Contents/MacOS/formant_latency_fb642" 2>&1 | grep -v '^Terrain:'
exit ${PIPESTATUS[0]}
