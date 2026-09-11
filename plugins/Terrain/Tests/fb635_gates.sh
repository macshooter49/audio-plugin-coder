#!/usr/bin/env bash
# ══════════════════════════════════════════════════════════════════════════════════════════════
#  fb635 — WHAT A PRESET LOAD TURNS ON, THE PAGE SHOWS ON · CARRIES COUNT WHAT IS ON · THE UI ROUND.
#
#  Max (2026-09-11): B/D forget their one-shot pictures · noise and filters look off but play on (and the
#  shape defaulted) · the LFO Path comes back as a triangle · "carrying 3 flow cards" with the cards off ·
#  Chaos and Rossler need a scroll · the Custom emblem · the inspector will not scroll · take away Ctrl+S.
#
#  Every gate runs NORMAL (must pass) and with each MUTATION (must go RED):
#    page    fb635_page_gate   scroll · emblem · wheel · cmds
#    osc     fb635_osc_gate    noheal · noro                     (the B/D one-shot picture)
#    lfo     fb635_lfo_gate    custom-only · hosttoast           (the Path, the host-restore door)
#    state   fb635_state_gate  nohook                            (noise · filter routing on a load)
#    park    lfo_park          LFO_PARK_MUTATE=2                  (the LFO playhead, on an ASSIGNED LFO — fb567)
#    face    fb635_face_gate   nokick · noro                     (whatever was hidden at load paints when shown)
#    restale fb635_restale_gate blend · mirror                   (A→B shows B: blend · phase · curve · no write-back [load+host] · motion · cards)
#    fav     fb635_fav_gate    rawread · nokey · nopull          (the heart survives a new instance, a rename, another instance)
#    rule    preset_carries_cert  ungated · noheal · flowblob · offcounts
#    au      preset_carries_au    ungated · flowblob             (the INSTALLED AU's <preset carries>)
#  plus WIRING tripwires: the load generation, the editor timer's announcement, the carries asserts.
#
#    bash Tests/fb635_gates.sh          # from plugins/Terrain, after building + installing
# ══════════════════════════════════════════════════════════════════════════════════════════════
set -u
OUT="${TI_GATE_OUT:-$(mktemp -d /tmp/fb635.XXXXXX)}"; REPO="$(cd ../.. && pwd)"; J="$REPO/_tools/JUCE/modules"; mkdir -p "$OUT"; rc_all=0
export NODE_PATH="$PWD/Tests/node_modules"
run () {  # label  mutation-env  command...
  local name="$1" mut="$2"; shift 2
  "$@"            > "$OUT/$name.normal.txt"  2>&1; local a=$?
  env "$mut" "$@" > "$OUT/$name.mutated.txt" 2>&1; local b=$?
  local v="OK"; [ $a -ne 0 ] && v="RED — the gate itself is failing"; [ $b -eq 0 ] && v="BROKEN CONTROL — the mutation did NOT go red"
  [ $b -ge 2 ] && v="BROKEN CONTROL — exit $b (a mutation anchor not found, or a crash): the control tested nothing"
  printf '  %-24s normal=%d  mutated(%s)=%d   %s\n' "$name" "$a" "$mut" "$b" "$v"
  if [ $a -ne 0 ] || [ $b -eq 0 ] || [ $b -ge 2 ]; then rc_all=1; fi; return 0
}
echo "══ fb635 GATE ══"
node Tests/ui_syntax.js Source/ui/public/index.html > "$OUT/ui_syntax.txt" 2>&1 || { echo "  ui_syntax: RED — $OUT/ui_syntax.txt"; rc_all=1; }
for m in scroll emblem wheel cmds;  do run "page:$m"  "PG_MUT=$m" node Tests/fb635_page_gate.js;  done
for m in noheal noro;               do run "osc:$m"   "OG_MUT=$m" node Tests/fb635_osc_gate.js;   done
for m in custom-only hosttoast;     do run "lfo:$m"   "LG_MUT=$m" node Tests/fb635_lfo_gate.js;   done
run "state:nohook" "SG_MUT=nohook" node Tests/fb635_state_gate.js
# fb635 — the LFO PLAYHEAD gate (fb567) was in no runner and went stale at fb628 (an unrouted LFO rests); it now plays
# an ASSIGNED LFO and lives here, with its own control (the idle class never applied → bars 3/4 and friends red)
run "lfo_park:idle" "LFO_PARK_MUTATE=2" node Tests/lfo_park.js
for m in nokick noro;               do run "face:$m"  "FG_MUT=$m" node Tests/fb635_face_gate.js;  done
# the sweep (Max's "double check everything else"): preset A→B shows B, on six of his presets (fixtures, audio stripped)
for m in blend mirror;              do run "restale:$m" "RG_MUT=$m" node Tests/fb635_restale_gate.js; done
# Max: "my heart is gone" in a new instance — the favourites file read back, re-read, and carried through a rename
for m in rawread nokey nopull;      do run "fav:$m"   "FV_MUT=$m" node Tests/fb635_fav_gate.js;   done

# ── THE RULE: Source/PresetCarries.h::of() + the heal (fb632's compile line) ─────────────────────
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
for m in ungated noheal flowblob offcounts; do run "rule:$m" "CR_MUT=$m" "$OUT/preset_carries_cert"; done

# ── THE WIRING ───────────────────────────────────────────────────────────────────────────────────
nGen=$(grep -c "stateLoadGen_.fetch_add (1" Source/PluginProcessor.cpp)
nTimer=$(grep -c "stateLoadGen_.load (std::memory_order_acquire) != announcedLoadGen_" Source/PluginEditor.cpp)
nHost=$(grep -c "afterPatchLoad (true)" Source/PluginEditor.cpp)
nOf=$(grep -c "tw::carries::of (" Source/PluginProcessor.cpp); nHeal=$(grep -c "tw::carries::healCatalogue (" Source/PluginProcessor.cpp)
nAssert=$(grep -c "tw::carries::kLfoPath\|tw::carries::kEngSample" Source/PluginProcessor.cpp)
if [ "$nGen" -ge 1 ] && [ "$nTimer" -ge 1 ] && [ "$nHost" -ge 1 ] && [ "$nOf" -ge 2 ] && [ "$nHeal" -ge 1 ] && [ "$nAssert" -ge 2 ]; then
  printf '  %-24s gen=%d timer=%d host=%d of()=%d heal=%d asserts=%d   OK\n' wiring "$nGen" "$nTimer" "$nHost" "$nOf" "$nHeal" "$nAssert"
else printf '  %-24s gen=%d timer=%d host=%d of()=%d heal=%d asserts=%d   RED\n' wiring "$nGen" "$nTimer" "$nHost" "$nOf" "$nHeal" "$nAssert"; rc_all=1; fi

# ── THE SEAM: the INSTALLED AU writes the counts into its <preset> child ─────────────────────────
PAY="$OUT/payload"; "$OUT/preset_carries_cert" --emit "$PAY" > "$OUT/emit.txt" 2>&1
AUBIN="$HOME/Library/Audio/Plug-Ins/Components/Terrain.component/Contents/MacOS/Terrain"
[ -f "$AUBIN" ] || { echo "  the AU is not installed — build and install first"; exit 1; }
printf '  installed AU: %s\n' "$(date -r "$AUBIN" '+%b %d %H:%M')"
c++ -std=c++17 -O2 -I Tests -I Tests/shim Tests/preset_carries_au.cpp \
    -framework Accelerate -framework AudioToolbox -framework CoreFoundation -framework CoreAudio \
    -o "$OUT/preset_carries_au" 2>> "$OUT/compile.txt" || { echo "  preset_carries_au: COMPILE FAIL — $OUT/compile.txt"; exit 1; }
for m in ungated flowblob; do run "au:$m" "CA_MUT=$m" "$OUT/preset_carries_au" "$PAY"; done
echo; echo "  full output: $OUT"; exit $rc_all
