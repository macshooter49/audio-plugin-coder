#!/usr/bin/env bash
# ══════════════════════════════════════════════════════════════════════════════════════════════
#  fb632 — CARRIES ARE COUNTED WHERE THEY ARE PLAYED. Max: "Michael Myers has no one shots in it but
#  the carry says it has one shots. The only time a carry should even be activated is if there's a
#  Sampler, Resynth, or a Granular engine going on."
#
#  Three layers, each with a control that must go RED:
#    the rule       Source/PresetCarries.h::of() on synthetic trees + the heal on a throwaway root
#                     CR_MUT=ungated   bar [1] expects the Michael Myers shape to count ONE
#                     CR_MUT=noheal    bar [7] expects the healed row to still say ONE
#    the wiring     the processor takes the shared function on BOTH paths (the file and the save
#                   sheet) and runs the heal on every catalogue — grep tripwires, no runner needed
#    the seam       the INSTALLED AU, through kAudioUnitProperty_ClassInfo
#                     CA_MUT=ungated   bar [1] flips the same way
#
#    bash Tests/fb632_gates.sh          # from plugins/Terrain
# ══════════════════════════════════════════════════════════════════════════════════════════════
set -u
OUT="${TI_GATE_OUT:-$(mktemp -d /tmp/fb632.XXXXXX)}"; REPO="$(cd ../.. && pwd)"; J="$REPO/_tools/JUCE/modules"; mkdir -p "$OUT"; rc_all=0
run () {  # label  mutation-env  command...
  local name="$1" mut="$2"; shift 2
  "$@"            > "$OUT/$name.normal.txt"  2>&1; local a=$?
  env "$mut" "$@" > "$OUT/$name.mutated.txt" 2>&1; local b=$?
  local v="OK"; [ $a -ne 0 ] && v="RED — the gate itself is failing"; [ $b -eq 0 ] && v="BROKEN CONTROL — the mutation did NOT go red"
  printf '  %-22s normal=%d  mutated(%s)=%d   %s\n' "$name" "$a" "$mut" "$b" "$v"
  if [ $a -ne 0 ] || [ $b -eq 0 ]; then rc_all=1; fi; return 0
}
echo "══ fb632 GATE ══"
# the fb621 line + juce_events/juce_data_structures (ValueTree) — no GUI, no AU.
c++ -std=c++17 -O2 -DNDEBUG=1 -DJUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1 -DJUCE_STANDALONE_APPLICATION=1 \
    -DJUCE_MODULE_AVAILABLE_juce_core=1 -DJUCE_MODULE_AVAILABLE_juce_audio_basics=1 \
    -DJUCE_MODULE_AVAILABLE_juce_audio_formats=1 -DJUCE_MODULE_AVAILABLE_juce_events=1 -DJUCE_MODULE_AVAILABLE_juce_data_structures=1 \
    -DJUCE_USE_FLAC=1 -DJUCE_USE_OGGVORBIS=0 -DJUCE_USE_MP3AUDIOFORMAT=0 -DJUCE_USE_LAME_AUDIO_FORMAT=0 \
    -DJUCE_USE_WINDOWS_MEDIA_FORMAT=0 \
    -I "$J" -I Source \
    Tests/preset_carries_cert.cpp Tests/juce_compdate_stub.cpp \
    "$J/juce_core/juce_core.mm" "$J/juce_audio_basics/juce_audio_basics.mm" "$J/juce_audio_formats/juce_audio_formats.mm" "$J/juce_events/juce_events.mm" "$J/juce_data_structures/juce_data_structures.mm" \
    -framework Foundation -framework CoreFoundation -framework Cocoa -framework IOKit -framework Security \
    -framework Accelerate -framework AudioToolbox -framework CoreAudio -framework CoreMIDI -framework QuartzCore \
    -o "$OUT/preset_carries_cert" 2> "$OUT/compile.txt" || { echo "  preset_carries_cert: COMPILE FAIL — $OUT/compile.txt"; exit 1; }
run rule:ungated  CR_MUT=ungated  "$OUT/preset_carries_cert"
run rule:noheal   CR_MUT=noheal   "$OUT/preset_carries_cert"

# ── THE WIRING: one function, both paths, and the heal on every catalogue ───────────────────────
nOf=$(grep -c "tw::carries::of (" Source/PluginProcessor.cpp); nHeal=$(grep -c "tw::carries::healCatalogue (" Source/PluginProcessor.cpp)
nAssert=$(grep -c "static_assert (.*tw::carries::kEngSample" Source/PluginProcessor.cpp)
if [ "$nOf" -ge 2 ] && [ "$nHeal" -ge 1 ] && [ "$nAssert" -ge 1 ]; then printf '  %-22s of()=%d heal=%d static_assert=%d   OK\n' wiring "$nOf" "$nHeal" "$nAssert"
else printf '  %-22s of()=%d heal=%d static_assert=%d   RED — the processor is not on the shared function\n' wiring "$nOf" "$nHeal" "$nAssert"; rc_all=1; fi

# ── THE SEAM: the real encoder writes the one-shot, the INSTALLED AU counts it ─────────────────
PAY="$OUT/payload"; "$OUT/preset_carries_cert" --emit "$PAY" > "$OUT/emit.txt" 2>&1
AUBIN="$HOME/Library/Audio/Plug-Ins/Components/Terrain.component/Contents/MacOS/Terrain"
[ -f "$AUBIN" ] || { echo "  the AU is not installed — build and install first"; exit 1; }
printf '  installed AU: %s\n' "$(date -r "$AUBIN" '+%b %d %H:%M')"
c++ -std=c++17 -O2 -I Tests -I Tests/shim Tests/preset_carries_au.cpp \
    -framework Accelerate -framework AudioToolbox -framework CoreFoundation -framework CoreAudio \
    -o "$OUT/preset_carries_au" 2>> "$OUT/compile.txt" || { echo "  preset_carries_au: COMPILE FAIL — $OUT/compile.txt"; exit 1; }
run au:ungated  CA_MUT=ungated  "$OUT/preset_carries_au" "$PAY"
echo; echo "  full output: $OUT"; exit $rc_all
