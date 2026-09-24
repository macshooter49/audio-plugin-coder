#!/usr/bin/env bash
#  tp101 — builds and runs chop_baseline_cert.cpp against the SHIPPED chop engine headers.
#  Run from plugins/Terrain.  JUCE / signalsmith come from the repo's _tools (or TERRAIN_TOOLS).
set -e
W="$(cd "$(dirname "$0")/../../.." && pwd)"
T="${TERRAIN_TOOLS:-$W/_tools}"
J="$T/JUCE/modules"
SS="$T/signalsmith-stretch/include"
SL="$(ls -d "$W"/build*/_deps/signalsmith-linear-src/include "$T"/../build*/_deps/signalsmith-linear-src/include 2>/dev/null | head -1)"
# SamplerVoice.h asks for juce_audio_processors only for juce::SynthesiserVoice, which lives in
# juce_audio_basics — a one-line shim keeps the cert off the GUI modules.
SHIM="$(mktemp -d)"; mkdir -p "$SHIM/juce_audio_processors"
echo '#include <juce_audio_basics/juce_audio_basics.h>' > "$SHIM/juce_audio_processors/juce_audio_processors.h"
clang++ -std=c++17 -O2 -ObjC++ -DJUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1 \
  -DJUCE_STANDALONE_APPLICATION=1 -DNDEBUG=1 -DJUCE_MODULE_AVAILABLE_juce_audio_basics=1 \
  -I "$SHIM" -I "$J" -I Source -I "$SS" -I "$SL" \
  Tests/chop_baseline_cert.cpp Tests/juce_compdate_stub.cpp "$J/juce_core/juce_core.mm" "$J/juce_audio_basics/juce_audio_basics.mm" \
  -o /tmp/chop_baseline_cert \
  -framework CoreFoundation -framework Accelerate -framework IOKit -framework Cocoa -framework Security
exec /tmp/chop_baseline_cert
