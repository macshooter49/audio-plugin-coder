#!/usr/bin/env bash
#  tp58 — builds and runs bpmlock_cert.cpp against the SHIPPED warp engines.
#  Run from plugins/Terrain.
set -e
W="$(cd "$(dirname "$0")/../../.." && pwd)"
J="$W/_tools/JUCE/modules"
SS="$W/_tools/signalsmith-stretch/include"
SL="$(ls -d "$W"/build*/_deps/signalsmith-linear-src/include 2>/dev/null | head -1)"
clang++ -std=c++17 -O2 -ObjC++ -DJUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1 \
  -DJUCE_STANDALONE_APPLICATION=1 -DNDEBUG=1 \
  -I "$J" -I Source -I "$SS" -I "$SL" \
  Tests/bpmlock_cert.cpp "$J/juce_core/juce_core.mm" "$J/juce_audio_basics/juce_audio_basics.mm" \
  -o /tmp/bpmcert \
  -framework CoreFoundation -framework Accelerate -framework IOKit -framework Cocoa -framework Security
exec /tmp/bpmcert
