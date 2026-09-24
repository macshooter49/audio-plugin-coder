#!/bin/bash
# Build + run preset_tail_cert.cpp against the built Terrain SharedCode (an LTO bitcode archive).
# Build Terrain_VST3 / Terrain_AU first. Exit 0 = PASS.
#   sh Tests/preset_tail_cert.sh             check (needs the first-note references written by the unfixed build)
#   sh Tests/preset_tail_cert.sh write-ref   write the first-note references (run this on the UNFIXED build)
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../../.." && pwd)
B=$ROOT/build/plugins/Terrain
F=$B/CMakeFiles/Terrain.dir/flags.make
OUT=${TMPDIR:-/tmp}/terrain_preset_tail
MODE=${1:-check}
[ -f "$B/Terrain_artefacts/Release/libTerrain_SharedCode.a" ] || { echo "build Terrain first (no libTerrain_SharedCode.a)"; exit 2; }
DEF=$(grep '^CXX_DEFINES' "$F" | sed 's/^CXX_DEFINES = //')
INC=$(grep '^CXX_INCLUDES' "$F" | sed 's/^CXX_INCLUDES = //')
# the fixed build has requestTailFlush(); the unfixed one has no dice flush at all
if grep -q 'requestTailFlush' "$ROOT/plugins/Terrain/Source/PluginProcessor.h"; then DEF="$DEF -DTL_HAVE_REQ"; fi
FW="-framework Accelerate -framework AudioToolbox -framework Cocoa -framework CoreAudio -framework CoreAudioKit -framework CoreMIDI -framework DiscRecording -framework Foundation -framework IOKit -framework QuartzCore -framework Security -framework WebKit -weak_framework Metal -weak_framework MetalKit"
eval clang++ -std=gnu++20 -O2 -arch arm64 -fvisibility=hidden -fvisibility-inlines-hidden -w $DEF $INC -c "$HERE/preset_tail_cert.cpp" -o "$OUT.o"
eval clang++ -arch arm64 -flto "$OUT.o" "$B/Terrain_artefacts/Release/libTerrain_SharedCode.a" "$B/libTerrain_WebUI.a" $FW -o "$OUT"
"$OUT" "$MODE" "${TMPDIR:-/tmp}"
