#!/bin/bash
# tp114 — build + run Tests/organics_limiter_cert.cpp against the built SharedCode (an LTO archive), the INSTALLED Organics
# library (~/Library/WavesCrate/TerrainInstrument/Organics, or $TERRAIN_ORGANICS_DIR). The organics_integration.sh recipe.
#   Tests/organics_limiter.sh gain  <id> <key> <vel>        engine → plugin-output offset (limiter off / on)
#   Tests/organics_limiter.sh keys  <id> <vels> [artic]     every key × RR take at the plugin output, limiter on (exit 1 = an over)
#   Tests/organics_limiter.sh wav   <id> <key> <vel> <dir> [artic]   before / after WAV pair + GR stats
#   Tests/organics_limiter.sh layer <id> <key> <vel>        two Organics oscs (each limited, the sum is not) · Organics + WT
# Build Terrain first (cmake --build build --target Terrain). TERRAIN_BUILD overrides the build dir.
set -e
HERE=$(cd "$(dirname "$0")" && pwd); ROOT=$(cd "$HERE/../../.." && pwd); B=${TERRAIN_BUILD:-$ROOT/build}/plugins/Terrain
F=$B/CMakeFiles/Terrain.dir/flags.make; OUT=${TMPDIR:-/tmp}/tp114_orglim/Fake.bundle
[ -f "$B/Terrain_artefacts/Release/libTerrain_SharedCode.a" ] || { echo "build Terrain first"; exit 2; }
mkdir -p "$OUT/Contents/MacOS" "$OUT/Contents/Resources"; ln -sfn "$HERE/../Resources/Wavetables" "$OUT/Contents/Resources/Wavetables"
if [ ! -x "$OUT/Contents/MacOS/organics_limiter_cert" ] || [ "$HERE/organics_limiter_cert.cpp" -nt "$OUT/Contents/MacOS/organics_limiter_cert" ] \
   || [ "$B/Terrain_artefacts/Release/libTerrain_SharedCode.a" -nt "$OUT/Contents/MacOS/organics_limiter_cert" ]; then
  DEF=$(grep '^CXX_DEFINES' "$F" | sed 's/^CXX_DEFINES = //'); INC=$(grep '^CXX_INCLUDES' "$F" | sed 's/^CXX_INCLUDES = //')
  FW="-framework Accelerate -framework AudioToolbox -framework Cocoa -framework CoreAudio -framework CoreAudioKit -framework CoreMIDI -framework DiscRecording -framework Foundation -framework IOKit -framework QuartzCore -framework Security -framework WebKit -weak_framework Metal -weak_framework MetalKit"
  eval clang++ -std=gnu++20 -O2 -arch arm64 -fvisibility=hidden -fvisibility-inlines-hidden -w $DEF $INC -I "$HERE/../Source" -c "$HERE/organics_limiter_cert.cpp" -o "$OUT/../t.o"
  eval clang++ -arch arm64 -flto "$OUT/../t.o" "$B/Terrain_artefacts/Release/libTerrain_SharedCode.a" "$B/libTerrain_WebUI.a" $FW -o "$OUT/Contents/MacOS/organics_limiter_cert"
fi
"$OUT/Contents/MacOS/organics_limiter_cert" "$@" 2>&1 | grep -v '^Terrain:'
exit ${PIPESTATUS[0]}
