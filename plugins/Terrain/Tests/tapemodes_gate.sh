#!/usr/bin/env bash
#  tp60 — builds and runs tapemodes_cert.cpp against the SHIPPED TapeFxEngine. Run from plugins/Terrain.
set -e
W="$(cd "$(dirname "$0")/../../.." && pwd)"
J="$W/_tools/JUCE/modules"
clang++ -std=c++17 -O2 -ObjC++ -DJUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1 \
  -DJUCE_STANDALONE_APPLICATION=1 -DNDEBUG=1 -I "$J" -I Source \
  Tests/tapemodes_cert.cpp "$J/juce_core/juce_core.mm" "$J/juce_audio_basics/juce_audio_basics.mm" \
  -o /tmp/tapecert \
  -framework CoreFoundation -framework Accelerate -framework IOKit -framework Cocoa -framework Security
exec /tmp/tapecert
