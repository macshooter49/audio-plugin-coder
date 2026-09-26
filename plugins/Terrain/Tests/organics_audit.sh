#!/usr/bin/env bash
# ══════════════════════════════════════════════════════════════════════════════════════════════
#  THE ORGANICS LIBRARY AUDIT (tp106 final overpass). Builds Tests/organics_audit (+ _knobs) against the runtime
#  (Source/organics) with the module objects organics_engine_test.sh caches, then runs one mode over the INSTALLED
#  library (default ~/Library/WavesCrate/TerrainInstrument/Organics, else …/Terrain/Organics, or $TERRAIN_ORGANICS_DIR).
#
#    Tests/organics_audit.sh lib   [idFilter]    whole-library sweep (≈ 20 min for 74 instruments)   exit 0 = PASS
#    Tests/organics_audit.sh loops [idFilter]    every sustain loop: pump ≤ 4 dB (Tremolo/Vibrato 6 dB; recipes with "loopMotionWhy" exempt), no seam click
#    Tests/organics_audit.sh knobs               every knob 0→100 % (perceptual metrics) + live turns
#    Tests/organics_audit.sh tone [base]         Tone: pure tones ≥ ×1.8, rich ones within ±15 % of today, C7 >16 kHz, whole library ≥ ×1.3
#    Tests/organics_audit.sh null <file> [check] the Organics render null (write before a CPU-only change, check after)
#    Tests/organics_audit.sh calib [idFilter]    the calibration point through the engine (Tools/organics/engine_calibrate.py)
#    Tests/organics_audit.sh peaks [idFilter] [vels]  tp108: every key's v127 peak, every RR take (Tools/organics/peaktrim.py;
#                                                ORG_PEAK_NOISE=0.5 = the Noise knob's default); last line PEAKSUMMARY
#    Tests/organics_audit.sh pitchdump <dir> <id> [vels]  the renders Tests/organics_pitch_check.py / retune.py measure
#    Tests/organics_audit.sh lim [idFilter]      tp114: the safety limiter's cost in loudness (calibration point v100 / v127, raw vs limited)
#    Tests/organics_audit.sh limnote <id> <artic> <key> <vel> [wavdir]  tp114: one note raw vs limited (GR, level, residual, bands, WAVs)
#  tp114: `peaks` prints PEAK (the library, limiter bypassed — peaktrim.py) AND LPEAK (the engine output, limited, + 4× ISP);
#  PEAKSUMMARY's verdict is the limited output against organics::kLimiterCeilingDb.
#  ORG_AUDIT_TSV=<file> (lib) writes one row per rendered note.
# ══════════════════════════════════════════════════════════════════════════════════════════════
set -u
cd "$(dirname "$0")/.." || exit 2                       # plugins/Terrain
REPO="$(cd ../.. && pwd)"
J="${JUCE_MODULES:-$REPO/_tools/JUCE/modules}"
[ -f "$J/juce_core/juce_core.h" ] || J="$HOME/Developer/VST-Plugins/audio-plugin-coder/.worktrees/terrain-instrument/_tools/JUCE/modules"
[ -f "$J/juce_core/juce_core.h" ] || { echo "JUCE modules not found (set JUCE_MODULES)"; exit 2; }
OUT="${ORG_OUT:-$REPO/build/organics_engine_test}"; mkdir -p "$OUT/audit"
CXX="${CXX:-c++}"
DEFS="-DNDEBUG=1 -DJUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1 -DJUCE_STANDALONE_APPLICATION=1 -DJUCE_MODAL_LOOPS_PERMITTED=1 \
 -DJUCE_MODULE_AVAILABLE_juce_core=1 -DJUCE_MODULE_AVAILABLE_juce_audio_basics=1 -DJUCE_MODULE_AVAILABLE_juce_audio_formats=1 \
 -DJUCE_MODULE_AVAILABLE_juce_events=1 -DJUCE_USE_FLAC=1 -DJUCE_USE_OGGVORBIS=0 -DJUCE_USE_MP3AUDIOFORMAT=0 \
 -DJUCE_USE_LAME_AUDIO_FORMAT=0 -DJUCE_USE_WINDOWS_MEDIA_FORMAT=0"
FLAGS="-std=c++17 -O3 $DEFS -I $J -I Source -I Source/organics"
FW="-framework Foundation -framework CoreFoundation -framework Cocoa -framework IOKit -framework Security -framework Accelerate -framework AudioToolbox -framework CoreAudio -framework CoreMIDI"

for m in juce_core juce_audio_basics juce_audio_formats juce_events; do
  if [ ! -f "$OUT/$m.o" ]; then
    $CXX $FLAGS -x objective-c++ -c "$J/$m/$m.mm" -o "$OUT/$m.o" 2> "$OUT/$m.log" || { echo "COMPILE FAIL $m"; grep -m1 'error' "$OUT/$m.log"; exit 1; }
  fi
done
for s in Source/organics/OrganicsLibrary.cpp Source/organics/OrganicEngine.cpp Tests/organics_audit.cpp Tests/organics_audit_knobs.cpp Tests/organics_audit_tone.cpp Tests/juce_compdate_stub.cpp; do
  o="$OUT/audit/$(basename "${s%.cpp}").o"
  $CXX $FLAGS -DTERRAIN_TOOLS_ORGANICS="\"$PWD/Tools/organics\"" -c "$s" -o "$o" 2> "$o.log" || { echo "COMPILE FAIL $s — first error:"; grep -m1 'error' "$o.log"; exit 1; }
done
$CXX "$OUT"/juce_core.o "$OUT"/juce_audio_basics.o "$OUT"/juce_audio_formats.o "$OUT"/juce_events.o "$OUT"/audit/*.o $FW -o "$OUT/organics_audit.new" 2> "$OUT/audit/link.log" || { echo "LINK FAIL"; grep -m1 -i 'error\|undefined' "$OUT/audit/link.log"; exit 1; }
mv -f "$OUT/organics_audit.new" "$OUT/organics_audit"      # a rename: a running audit keeps its own binary

ROOT="${TERRAIN_ORGANICS_DIR:-$HOME/Library/WavesCrate/TerrainInstrument/Organics}"
[ -d "$ROOT" ] || ROOT="$HOME/Library/WavesCrate/Terrain/Organics"
MODE="${1:-lib}"; shift || true
case "$MODE" in
  lib)   exec "$OUT/organics_audit" --lib   "$ROOT" "${1:-}" ;;
  loops) exec "$OUT/organics_audit" --loops "$ROOT" "${1:-}" ;;
  knobs) exec "$OUT/organics_audit" --knobs "$ROOT" ;;
  tone)  exec "$OUT/organics_audit" --tone  "$ROOT" "${1:-}" ;;
  calib) exec "$OUT/organics_audit" --calib "$ROOT" "${1:-}" ;;
  peaks) exec "$OUT/organics_audit" --peaks "$ROOT" "${1:-}" "${2:-}" ;;
  pitchdump) exec "$OUT/organics_audit" --pitchdump "$ROOT" "$@" ;;
  note)  exec "$OUT/organics_audit" --note  "$ROOT" "$@" ;;
  null)  exec "$OUT/organics_audit" --null  "$ROOT" "$1" "${2:-}" ;;
  lim)   exec "$OUT/organics_audit" --lim   "$ROOT" "${1:-}" ;;
  limnote) exec "$OUT/organics_audit" --limnote "$ROOT" "$@" ;;
  *) echo "usage: $0 lib|loops|knobs|tone|calib|peaks|pitchdump|note|null|lim|limnote"; exit 2 ;;
esac
