#!/usr/bin/env bash
#  Chop stretch/warp click harness — builds Source/ChopStretch_test.cpp twice: against the BASE commit's
#  sources (default 661c141, override with BASE=<rev>) and against the working tree, renders every
#  scenario through both, and prints the per-mode click counts before -> after + the LTAS character check.
#  Run from plugins/Terrain.   FILTER=<id substring> narrows the scenario set.
set -e
BASE="${BASE:-661c141}"
OUT="${OUT:-${TMPDIR:-/tmp}/chopstretch}"
mkdir -p "$OUT"
W="$(cd "$(dirname "$0")/../../.." && pwd)"
pick() { for d in "$@"; do if [ -e "$d" ]; then echo "$d"; return; fi; done; }
J="$(pick "$W/_tools/JUCE/modules/juce_core" "$W/../../../.worktrees/terrain-instrument/_tools/JUCE/modules/juce_core" "$HOME/Developer/VST-Plugins/audio-plugin-coder/.worktrees/terrain-instrument/_tools/JUCE/modules/juce_core")"
J="$(dirname "$J")"
SS="$(pick "$W/_tools/signalsmith-stretch/include/signalsmith-stretch" "$HOME/Developer/VST-Plugins/audio-plugin-coder/.worktrees/terrain-instrument/_tools/signalsmith-stretch/include/signalsmith-stretch")"
SS="$(dirname "$SS")"
SL="$(ls -d "$W"/build*/_deps/signalsmith-linear-src/include "$HOME"/Developer/VST-Plugins/audio-plugin-coder/.worktrees/terrain-instrument/build*/_deps/signalsmith-linear-src/include 2>/dev/null | head -1)"

build() {  # $1 = source dir (the harness is compiled FROM it so its quoted includes resolve there), $2 = binary
  [ "$1" = "Source" ] || cp Source/ChopStretch_test.cpp "$1/ChopStretch_test.cpp"
  clang++ -std=c++17 -O2 -ObjC++ -DJUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1 -DJUCE_STANDALONE_APPLICATION=1 -DNDEBUG=1 \
    -w -I Tests/shim_ap -I "$J" -I "$1" -I "$SS" -I "$SL" \
    "$1/ChopStretch_test.cpp" "$J/juce_core/juce_core.mm" "$J/juce_audio_basics/juce_audio_basics.mm" \
    -framework CoreFoundation -framework Accelerate -framework IOKit -framework Cocoa -framework Security -o "$2"
}

#  BASE_SRC=<dir> uses an already-extracted copy of the base Source/ tree instead of git archive.
if [ -z "$BASE_SRC" ]; then
  rm -rf "$OUT/base" && mkdir -p "$OUT/base"
  TREE="$(git rev-parse "$BASE:plugins/Terrain/Source")"
  # from the repo root: run inside a subdirectory, git archive filters the tree by that subdirectory
  git -C "$(git rev-parse --show-toplevel)" archive --format=tar "$TREE" | tar -x -C "$OUT/base"
  [ -f "$OUT/base/SamplerVoice.h" ] || { echo "base extraction failed"; exit 1; }
  BASE_SRC="$OUT/base"
fi
build "$BASE_SRC" "$OUT/cs_before"
build "Source"           "$OUT/cs_after"
"$OUT/cs_after" selftest
"$OUT/cs_before" run "$OUT/before.tsv" $FILTER
"$OUT/cs_after"  run "$OUT/after.tsv"  $FILTER
"$OUT/cs_after"  compare "$OUT/before.tsv" "$OUT/after.tsv"
