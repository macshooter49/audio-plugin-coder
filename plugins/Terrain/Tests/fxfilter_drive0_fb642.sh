#!/bin/bash
# fb642 — build + run fxfilter_drive0_fb642.cpp against the built SharedCode (LTO archive), inside a fake bundle whose
# Contents/Resources/Wavetables points at the repo's library, so wtFactoryRoot() resolves exactly as it does in the plugin.
set -e
HERE=$(cd "$(dirname "$0")" && pwd); ROOT=$(cd "$HERE/../../.." && pwd); B=$ROOT/build/plugins/Terrain
F=$B/CMakeFiles/Terrain.dir/flags.make; OUT=${TMPDIR:-/tmp}/fb642_fxf/Fake.bundle
[ -f "$B/Terrain_artefacts/Release/libTerrain_SharedCode.a" ] || { echo "build Terrain first"; exit 2; }
mkdir -p "$OUT/Contents/MacOS" "$OUT/Contents/Resources"; ln -sfn "$HERE/../Resources/Wavetables" "$OUT/Contents/Resources/Wavetables"
DEF=$(grep '^CXX_DEFINES' "$F" | sed 's/^CXX_DEFINES = //'); INC=$(grep '^CXX_INCLUDES' "$F" | sed 's/^CXX_INCLUDES = //')
FW="-framework Accelerate -framework AudioToolbox -framework Cocoa -framework CoreAudio -framework CoreAudioKit -framework CoreMIDI -framework DiscRecording -framework Foundation -framework IOKit -framework QuartzCore -framework Security -framework WebKit -weak_framework Metal -weak_framework MetalKit"
python3 - "$HERE/../Source/ui/public/index.html" "$OUT/../fxf_names_fb642.h" <<'PY2'
import sys,re
s=open(sys.argv[1],encoding='utf-8').read(); i=re.search(r'var\s+FLT_ENGINES\s*=', s).start(); m=re.search(r'\[(.*?)\]\s*;', s[i:], re.S)
names=[a or b for a,b in re.findall(r"'([^']*)'|\"([^\"]*)\"", m.group(1))]
open(sys.argv[2],'w').write('static const char* kNames[] = {' + ','.join('"%s"' % n.replace('"','') for n in names) + '};\n')
PY2
eval clang++ -std=gnu++20 -O2 -arch arm64 -fvisibility=hidden -fvisibility-inlines-hidden -w $DEF $INC -I"$OUT/.." -c "$HERE/fxfilter_drive0_fb642.cpp" -o "$OUT/../t.o"
eval clang++ -arch arm64 -flto "$OUT/../t.o" "$B/Terrain_artefacts/Release/libTerrain_SharedCode.a" "$B/libTerrain_WebUI.a" $FW -o "$OUT/Contents/MacOS/fxfilter_drive0_fb642"
"$OUT/Contents/MacOS/fxfilter_drive0_fb642" 2>&1 | grep -v '^Terrain:'
exit ${PIPESTATUS[0]}
