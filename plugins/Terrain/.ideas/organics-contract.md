# Organics engine — frozen contract (tp104, 2026-09-24)

The lead froze these before the four-agent fork. **Design = `.ideas/organics-engine-design.md` (authoritative for
behaviour). This file = the seams between agents.** Change a seam only by messaging the lead.

Max's decisions (2026-09-24): the design doc's knob set — **page 1 `Dynamics · Tone · Body · Attack · Human`, page 2
`Release · Noise · Sustain · Velocity · Image`, unison page = "Ensemble"**. Engine label **"Organics"** (menu name),
enum ORGANIC. It must look clear, even and exactly like the other engines, with the white thin line-art instrument
visuals from `Design/organics/` (mockup approved in spirit — reuse `organics-instruments.js` drawings/families).

## 1. Files and owners (disjoint)

| Agent | Owns |
|---|---|
| A · compiler & content | `Tools/organics/**`, `Tests/organics_compile_test.py`, `Resources/Organics/index-seed.json` (small), the COMPILED LIBRARY OUTSIDE GIT at `~/Developer/VST-Plugins/organics-library/compiled/` |
| B · runtime DSP | `Source/organics/OrganicsLibrary.{h,cpp}`, `Source/organics/OrganicEngine.{h,cpp}`, `Source/organics/OrganicEngine_test.cpp`, `Tests/organics_engine_test.sh`, extra fixtures under `Tests/fixtures/organics/` (new folders only) |
| C · integration C++ | `ParameterIDs.hpp`, `OscBankIds.h` (regenerated), `SynthVoice.h`, `PluginProcessor.{h,cpp}`, `PluginEditor.{h,cpp}`, `SynthModConfig.h`, `PresetCarries.h`, `CMakeLists.txt`, `Source/organics/OrganicsStub.cpp`, literal fixes in `Tests/*.cpp`, new AU harnesses |
| D · UI | `Source/ui/public/index.html` only, `Tests/_org_*.js`, literal fixes in `Tests/*.js` |
| lead | `Source/organics/OrganicsApi.h` (frozen), `Tests/fixtures/organics/test.sine/` + `index.json` + `ids.json` (frozen) |

## 2. C++ API
`Source/organics/OrganicsApi.h` — read it. Pimpl: B implements `OrganicEngine::Impl` and the library in `.cpp` files.
**CMake (C):** compile `Source/organics/OrganicEngine.cpp` + `OrganicsLibrary.cpp` when they exist, else
`Source/organics/OrganicsStub.cpp` (silent no-op bodies) — `if(EXISTS …)` in CMake so the merge needs no edit.
The lead deletes the stub after B merges.

## 3. Library on disk (.torg v1, design §4.1)
- Root: `<terrainDataDir>/Organics` = `~/Library/Application Support/WavesCrate/Terrain/Organics` (Mac),
  `%APPDATA%\WavesCrate\Terrain\Organics` (Win). Override: env `TERRAIN_ORGANICS_DIR` (tests point it at
  `Tests/fixtures/organics`).
- `index.json` (array: id, name, family, category, tags, sizeMB, licence, credit), `ids.json` (append-only
  `{id: int}`; 0 = none; never reuse a number), one folder per id: `map.json`, `samples/NNNN.flac`, `source/`,
  optional `preview.flac` (middle C, vel 90, ≤ 3 s).
- **map.json adds one field to the design doc's schema: `"samples": ["0001.flac", …]`** — `region.smp` indexes it.
- Categories (browser order): Keys · Organs · Strings · Plucked · Winds · Brass · Mallets & Bells · Choir & Voice · Percussion.
- `family` = a visualiser family id = a `FAM.<id>` key in `Design/organics/organics-instruments.js`: grand, upright,
  rhodes, clav, harpsi, organ, tonewheel, violin, cello, bass, guitar, harp, koto, flute, clarinet, sax, trumpet, horn,
  choir, glock, marimba, kalimba, musicbox. New families (e.g. vibraphone → marimba, wurlitzer → rhodes) map onto
  these; unknown → the category's default (Keys→grand, Organs→organ, Strings→violin, Plucked→guitar, Winds→flute,
  Brass→trumpet, Mallets & Bells→glock, Choir & Voice→choir, Percussion→marimba).
- The compiled library is NOT committed to git (size; GitHub limits). A writes to
  `~/Developer/VST-Plugins/organics-library/compiled/` and provides `Tools/organics/install.sh` that copies/links it
  into the root above.

## 4. Parameters (C declares; D reads/writes by ID)
Per osc X in A..H: `SYN_OSC_X_ORG_INST` (int 0..4095, default 0), `SYN_OSC_X_ORG_ARTIC` (int 0..7), then floats 0..1:
`_ORG_DYNAMICS` .5 · `_ORG_TONE` .5 · `_ORG_BODY` .5 · `_ORG_ATTACK` .5 · `_ORG_HUMAN` .25 · `_ORG_RELEASE` .5 ·
`_ORG_NOISE` .5 · `_ORG_SUSTAIN` 0 · `_ORG_VELOCITY` .75 · `_ORG_IMAGE` .667.
APVTS → `OrganicParams`: bipolar knobs `2v−1`; image `1.5v`; others as-is. Engine choice grows to 12 entries
`"WT","SAMP","GRAN","SPEC","FM","HARM","MODAL","ORGANIC","R8","R9","R10","R11"` (UI shows "Organics", never R8–R11).

## 5. Mod destinations
`ModDest::OrganicBase = 5272`, dest = 5272 + osc(0..7)·10 + knob, knob order Dynamics Tone Body Attack Human
Release Noise Sustain Velocity Image. `NumDests` = 5352. E–H are explicit (not +OSCBANK2_BASE).

## 6. Natives (C implements; D calls; D mocks them as `window.__orgMock` until C merges)
- `organicsIndex()` → JSON string of index.json (plus `"installed": true|false` per entry).
- `organicsSetInstrument(osc 0..7, id)` → JSON `{ok, id, name, family, category, artics:[…], hasNoise, hasRelease, status}`;
  also sets `ORG_INST` to `idToIndex(id)`. status ∈ `"ok" | "loading" | "missing"`.
- `organicsGetState(osc)` → same JSON for the osc's current instrument (the page reads back on open — state persists law).
- `organicsPreview(id)` → plays `preview.flac` through the existing browser-preview path (no synth voice). `organicsPreview("")` stops.
- Event `organicViz` (emitted ≤ 15 Hz, ONLY while notes sound or within 300 ms after): `{osc, notes:[{n, lvl}], pedal}`.

## 7. State
`<ORGANICS><OSC slot="0..7" id="…" rev="1"/></ORGANICS>` in the plugin state. The string id wins on load; unknown id →
family default else silent + the page shows "Instrument not installed: <name>". Never a crash.

## 8. Laws that apply to everyone (CLAUDE.md)
No clicks (5 ms fades, never cuts) · bit-identical + zero allocation when no osc uses Organics · no locks/allocation on the
audio thread · the centerline + fixed-position laws for every pixel · dropdowns not click-to-cycle · state persists and
the page reads it back · recycle existing UI code (engine view, knob rings, the two-pane browser, `.pmenu`) · measure,
don't assume · "a filtered build log hides a failed build" · Windows must compile (no big stack arrays, no raw string
literal > 16 KB).
