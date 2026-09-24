#!/bin/bash
# tp103 — build + run standalone_devices.cpp against the built Terrain SharedCode (an LTO bitcode archive).
# Build Terrain_VST3 / Terrain_AU first. Exit 0 = PASS.
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../../.." && pwd)
B=$ROOT/build/plugins/Terrain
F=$B/CMakeFiles/Terrain.dir/flags.make
OUT=${TMPDIR:-/tmp}/terrain_standalone_devices
[ -f "$B/Terrain_artefacts/Release/libTerrain_SharedCode.a" ] || { echo "build Terrain first (no libTerrain_SharedCode.a)"; exit 2; }
DEF=$(grep '^CXX_DEFINES' "$F" | sed 's/^CXX_DEFINES = //')
INC=$(grep '^CXX_INCLUDES' "$F" | sed 's/^CXX_INCLUDES = //')
FW="-framework Accelerate -framework AudioToolbox -framework Cocoa -framework CoreAudio -framework CoreAudioKit -framework CoreMIDI -framework DiscRecording -framework Foundation -framework IOKit -framework QuartzCore -framework Security -framework WebKit -weak_framework Metal -weak_framework MetalKit"
eval clang++ -std=gnu++20 -O2 -arch arm64 -fvisibility=hidden -fvisibility-inlines-hidden -w $DEF $INC -c "$HERE/standalone_devices.cpp" -o "$OUT.o"
eval clang++ -arch arm64 -flto "$OUT.o" "$B/Terrain_artefacts/Release/libTerrain_SharedCode.a" "$B/libTerrain_WebUI.a" $FW -o "$OUT"
"$OUT"
