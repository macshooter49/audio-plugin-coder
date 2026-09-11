#!/usr/bin/env bash
# ══════════════════════════════════════════════════════════════════════════════════════════════
#  fb631 — the preset-change SPIKE, the LFO that kept moving, the vocab field that deleted itself.
#    spike:lenient   SP_MUT=lenient  the spike cert must EXPECT a spike → a fixed plugin fails it
#    lfo:route       (no control of its own — bars E/F went red on the shipped code; see the file)
#    lfo:card        the popped card stays green
#    vocab:blurkill  VS_MUT=blurkill re-installs destroy-on-blur → the field dies → RED
#    bash Tests/fb631_gates.sh          # from plugins/Terrain, with the AU installed
# ══════════════════════════════════════════════════════════════════════════════════════════════
set -u
OUT="${TI_GATE_OUT:-$(mktemp -d /tmp/fb631.XXXXXX)}"; mkdir -p "$OUT"; rc_all=0
run () { local name="$1" mut="$2"; shift 2
  "$@" > "$OUT/$name.normal.txt" 2>&1; local a=$?
  local b=1; if [ -n "$mut" ]; then env "$mut" "$@" > "$OUT/$name.mutated.txt" 2>&1; b=$?; fi
  local v="OK"; [ $a -ne 0 ] && v="RED — the gate itself is failing"; [ -n "$mut" ] && [ $b -eq 0 ] && v="BROKEN CONTROL — the mutation did NOT go red"
  printf '  %-16s normal=%d  mutated(%s)=%d   %s\n' "$name" "$a" "${mut:-none}" "$b" "$v"
  if [ $a -ne 0 ] || { [ -n "$mut" ] && [ $b -eq 0 ]; }; then rc_all=1; fi; return 0; }
echo "══ fb631 GATE ══"
AUBIN="$HOME/Library/Audio/Plug-Ins/Components/Terrain.component/Contents/MacOS/Terrain"
[ -f "$AUBIN" ] || { echo "  the AU is not installed — build and install first"; exit 1; }
c++ -std=c++17 -O2 -I Tests -I Tests/shim Tests/preset_load_spike_au.cpp -framework Accelerate -framework AudioToolbox -framework CoreFoundation -framework CoreAudio -o "$OUT/spike" 2> "$OUT/compile.txt" || { echo "  spike: COMPILE FAIL — $OUT/compile.txt"; exit 1; }
PRESETS=$(ls "$HOME/Library/WavesCrate/TerrainInstrument/Banks/User/"*.terrain 2>/dev/null | head -8)
[ -n "$PRESETS" ] || { echo "  no user presets to load — the spike cert needs real .terrain files"; exit 1; }
run spike:lenient  SP_MUT=lenient  "$OUT/spike" $PRESETS
export NODE_PATH="$PWD/Tests/node_modules"
run lfo:route      ""              node Tests/lfo_route_gate.js
run lfo:card       ""              node Tests/lfo_route_card_gate.js
run vocab:blurkill VS_MUT=blurkill node Tests/vocab_steal_gate.js
echo; echo "  full output: $OUT"; exit $rc_all
