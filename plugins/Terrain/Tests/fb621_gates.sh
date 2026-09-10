#!/usr/bin/env bash
# ══════════════════════════════════════════════════════════════════════════════════════════════
#  fb621 — EVERYTHING A USER MAKES TRAVELS. The asset envelope (Source/PresetAssets.h) driven
#  against real libFLAC: round trip · peaks above 1.0 · the size win · wavetable frames · garbage
#  refused · factory references · awkward shapes · determinism · and the one that matters —
#  the asset written into a .terrain comes back out of the file whole.
#
#  Two controls, each must go RED:
#    AS_MUT=noscale   the envelope's `scale` field is rewritten to 1.0 after encoding → bar [2]
#    AS_MUT=lenient   bar [5] flips: garbage must be ACCEPTED → a correct decoder fails the run
#
#    bash Tests/fb621_gates.sh          # from plugins/Terrain
# ══════════════════════════════════════════════════════════════════════════════════════════════
set -u
OUT="${TI_GATE_OUT:-$(mktemp -d /tmp/fb621.XXXXXX)}"; REPO="$(cd ../.. && pwd)"; J="$REPO/_tools/JUCE/modules"; mkdir -p "$OUT"; rc_all=0
run () {  # label  mutation-env  command...
  local name="$1" mut="$2"; shift 2
  "$@"            > "$OUT/$name.normal.txt"  2>&1; local a=$?
  env "$mut" "$@" > "$OUT/$name.mutated.txt" 2>&1; local b=$?
  local v="OK"; [ $a -ne 0 ] && v="RED — the gate itself is failing"; [ $b -eq 0 ] && v="BROKEN CONTROL — the mutation did NOT go red"
  printf '  %-22s normal=%d  mutated(%s)=%d   %s\n' "$name" "$a" "$mut" "$b" "$v"
  if [ $a -ne 0 ] || [ $b -eq 0 ]; then rc_all=1; fi; return 0
}
echo "══ fb621 GATE ══"
# juce_core + juce_audio_basics + juce_audio_formats (libFLAC lives inside the module) — no GUI, no AU.
c++ -std=c++17 -O2 -DNDEBUG=1 -DJUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1 -DJUCE_STANDALONE_APPLICATION=1 \
    -DJUCE_MODULE_AVAILABLE_juce_core=1 -DJUCE_MODULE_AVAILABLE_juce_audio_basics=1 \
    -DJUCE_MODULE_AVAILABLE_juce_audio_formats=1 \
    -DJUCE_USE_FLAC=1 -DJUCE_USE_OGGVORBIS=0 -DJUCE_USE_MP3AUDIOFORMAT=0 -DJUCE_USE_LAME_AUDIO_FORMAT=0 \
    -DJUCE_USE_WINDOWS_MEDIA_FORMAT=0 \
    -I "$J" -I Source \
    Tests/preset_assets_cert.cpp Tests/juce_compdate_stub.cpp \
    "$J/juce_core/juce_core.mm" "$J/juce_audio_basics/juce_audio_basics.mm" "$J/juce_audio_formats/juce_audio_formats.mm" \
    -framework Foundation -framework CoreFoundation -framework Cocoa -framework IOKit -framework Security \
    -framework Accelerate -framework AudioToolbox -framework CoreAudio -framework CoreMIDI -framework QuartzCore \
    -o "$OUT/preset_assets_cert" 2> "$OUT/compile.txt" || { echo "  preset_assets_cert: COMPILE FAIL — $OUT/compile.txt"; exit 1; }
run assets:noscale  AS_MUT=noscale  "$OUT/preset_assets_cert"
run assets:lenient  AS_MUT=lenient  "$OUT/preset_assets_cert"

# ── THE SEAM: the REAL encoder writes the payloads, the INSTALLED AU consumes them ─────────────
PAY="$OUT/payload"
"$OUT/preset_assets_cert" --emit "$PAY" > "$OUT/emit.txt" 2>&1
if [ $? -ne 0 ]; then echo "  emit: NO FACTORY REFERENCE (the shipped wavetable library was not found beside the installed AU)"; fi
sed -n '1,8p' "$OUT/emit.txt" | sed 's/^/    /'
AUBIN="$HOME/Library/Audio/Plug-Ins/Components/Terrain.component/Contents/MacOS/Terrain"
[ -f "$AUBIN" ] || { echo "  the AU is not installed — build and install first"; exit 1; }
printf '  installed AU: %s\n' "$(date -r "$AUBIN" '+%b %d %H:%M')"
c++ -std=c++17 -O2 -I Tests -I Tests/shim Tests/preset_assets_au.cpp \
    -framework Accelerate -framework AudioToolbox -framework CoreFoundation -framework CoreAudio \
    -o "$OUT/preset_assets_au" 2>> "$OUT/compile.txt" || { echo "  preset_assets_au: COMPILE FAIL — $OUT/compile.txt"; exit 1; }
run artist:corrupt  AA_MUT=corrupt  "$OUT/preset_assets_au" "$PAY"
run artist:inherit  AA_MUT=inherit  "$OUT/preset_assets_au" "$PAY"
echo; echo "  full output: $OUT"; exit $rc_all
