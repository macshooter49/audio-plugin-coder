#!/bin/bash
# fb640 — build + run browser_freeze_sim.cpp: the real processor + editor + WKWebView page, linked against the built
# SharedCode (LTO archive) inside a fake bundle whose Contents/Resources/Wavetables points at the repo's library.
# Opens a window for ~20 s. Frameworks are read off the Standalone's own link line, so nothing is guessed.
set -e
HERE=$(cd "$(dirname "$0")" && pwd); ROOT=$(cd "$HERE/../../.." && pwd); B=$ROOT/build/plugins/Terrain
F=$B/CMakeFiles/Terrain.dir/flags.make; OUT=${TMPDIR:-/tmp}/fb640_sim/Fake.app
[ -f "$B/Terrain_artefacts/Release/libTerrain_SharedCode.a" ] || { echo "build Terrain first"; exit 2; }
mkdir -p "$OUT/Contents/MacOS" "$OUT/Contents/Resources"; ln -sfn "$HERE/../Resources/Wavetables" "$OUT/Contents/Resources/Wavetables"
cat > "$OUT/Contents/Info.plist" <<'PL'
<?xml version="1.0" encoding="UTF-8"?><!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict><key>CFBundleIdentifier</key><string>com.wavescrate.fb640sim</string><key>CFBundleExecutable</key><string>browser_freeze_sim</string>
<key>CFBundlePackageType</key><string>APPL</string><key>NSHighResolutionCapable</key><true/></dict></plist>
PL
DEF=$(grep '^CXX_DEFINES' "$F" | sed 's/^CXX_DEFINES = //'); INC=$(grep '^CXX_INCLUDES' "$F" | sed 's/^CXX_INCLUDES = //')
FW=$(grep -oE '(-weak_framework|-framework) [A-Za-z]+' "$B/CMakeFiles/Terrain_Standalone.dir/link.txt" | sort -u | tr '\n' ' ')
eval clang++ -std=gnu++20 -O2 -arch arm64 -fvisibility=hidden -fvisibility-inlines-hidden -w $DEF $INC -c "$HERE/browser_freeze_sim.cpp" -o "$OUT/../t.o"
eval clang++ -arch arm64 -flto "$OUT/../t.o" "$B/Terrain_artefacts/Release/libTerrain_SharedCode.a" "$B/libTerrain_WebUI.a" $FW -o "$OUT/Contents/MacOS/browser_freeze_sim"
"$OUT/Contents/MacOS/browser_freeze_sim" 2>&1 | grep -v '^Terrain:'
exit ${PIPESTATUS[0]}
