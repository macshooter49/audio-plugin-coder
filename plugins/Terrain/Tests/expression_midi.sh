#!/bin/bash
# tp103 — build + run the Settings expression/MIDI harnesses against the built SharedCode (LTO archive), inside a fake
# bundle whose Contents/Resources/Wavetables points at the repo's library (wtFactoryRoot resolves as in the plugin).
#   Tests/expression_midi.sh cert                  — the measured cert (MPE, poly AT, channel, A4, voice ceiling)
#   Tests/expression_midi.sh null <out.f32>        — render the fixed MPE-off sequence (compare two builds with cmp)
# Build Terrain_VST3 / Terrain_AU first. Exit 0 = PASS.
set -e
MODE=${1:-cert}
HERE=$(cd "$(dirname "$0")" && pwd); ROOT=$(cd "$HERE/../../.." && pwd); B=${TERRAIN_BUILD:-$ROOT/build}/plugins/Terrain
F=$B/CMakeFiles/Terrain.dir/flags.make; OUT=${TMPDIR:-/tmp}/tp103_expr_$MODE/Fake.bundle
[ -f "$B/Terrain_artefacts/Release/libTerrain_SharedCode.a" ] || { echo "build Terrain first"; exit 2; }
mkdir -p "$OUT/Contents/MacOS" "$OUT/Contents/Resources"; ln -sfn "$HERE/../Resources/Wavetables" "$OUT/Contents/Resources/Wavetables"
DEF=$(grep '^CXX_DEFINES' "$F" | sed 's/^CXX_DEFINES = //'); INC=$(grep '^CXX_INCLUDES' "$F" | sed 's/^CXX_INCLUDES = //')
FW="-framework Accelerate -framework AudioToolbox -framework Cocoa -framework CoreAudio -framework CoreAudioKit -framework CoreMIDI -framework DiscRecording -framework Foundation -framework IOKit -framework QuartzCore -framework Security -framework WebKit -weak_framework Metal -weak_framework MetalKit"
if [ "$MODE" = null ]; then SRC=expression_null_render; else SRC=expression_midi_cert; fi
eval clang++ -std=gnu++20 -O2 -arch arm64 -fvisibility=hidden -fvisibility-inlines-hidden -w $DEF $INC -c "$HERE/$SRC.cpp" -o "$OUT/../t.o"
eval clang++ -arch arm64 -flto "$OUT/../t.o" "$B/Terrain_artefacts/Release/libTerrain_SharedCode.a" "$B/libTerrain_WebUI.a" $FW -o "$OUT/Contents/MacOS/$SRC"
shift || true
"$OUT/Contents/MacOS/$SRC" "$@" 2>&1 | grep -v '^Terrain:'
exit ${PIPESTATUS[0]}
