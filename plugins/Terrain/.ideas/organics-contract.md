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
- Root: `<terrainDataDir>/Organics` = `~/Library/WavesCrate/Terrain/Organics` (Mac; JUCE's userApplicationDataDirectory is ~/Library; the legacy `TerrainInstrument` folder wins when only it exists — Max's Mac),
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

---

## tp105 AMENDMENT (2026-09-24, Max's review of the first build) — supersedes the above where they differ

**Front panel.** Page 1 = `Dynamics · Tone · Body · Vibrato · Human`. Page 2 = `Release · Noise · Sustain · Velocity · Image`.
Attack leaves the panel (the amp envelope owns attack). `SYN_OSC_X_ORG_ATTACK` stays declared (saved sessions), hidden,
default 0.5 = Natural, not a mod dest.

**New params (all A–H, appended LAST in the layout, like the first batch):**
`_ORG_VIBRATO` float 0..1 default 0 · `_ORG_VIBRATE` float 0..1 default 0.417 (→ 3..9 Hz, 5.5 Hz) ·
`_ORG_VIBDELAY` float 0..1 default 0.175 (→ 0..2 s, 0.35 s) · `_ORG_VCURVE` int 0..2 default 1 (Soft/Linear/Hard) ·
`_ORG_TUNING` int 0..1 default 1 (As recorded / Equal).

**Mod dests:** the block is unchanged (5272 + osc·10 + knob) but **knob 3 is now Vibrato** (was Attack).
Order: Dynamics 0 · Tone 1 · Body 2 · **Vibrato 3** · Human 4 · Release 5 · Noise 6 · Sustain 7 · Velocity 8 · Image 9.

**Release = a real release.** Note-off decay time = max(amp-envelope release, Release-knob time); knob taper 20 ms (0) …
authored (0.5) … **≥ 10 s at 1.0**; the amp envelope's release ALWAYS lengthens it (piano included). Release-trigger
sample level still follows the knob. The voice passes `ampAttack` / `ampRelease` in `OrganicParams`.

**Noise = real mechanical noise.** map.json regions of `kind:"noise"` gain `"trig": "on" | "off"` (missing = "off").
"on" noises (hammer/key-down thump, breath onset, pick/fret, bow scrape) start with the note, "off" noises (damper,
key-up, pedal release) at note-off. Noise knob: 0 = silent, 0.5 = authored level, 1 = +12 dB. Instruments whose
recordings have no noise get noise regions from a shared, licence-cleared noise library, mapped by family.

**Tuning.** Regions gain `"tfix"` (cents, measured by the compiler = the sample's deviation from equal temperament at
its root). `tuning = 1` (Equal) applies it; 0 plays as recorded.

**Back panel (the osc's "+" / flip side) for Organics** replaces the Sample engine's warp modes there:
Vibrato Rate · Vibrato Delay · Velocity Curve (Soft/Linear/Hard) · Tuning (As recorded/Equal) · Articulation (the
same list as the header pill).

**Visuals.** No dark display fill/outline — transparent over the panel like the wavetable view. Compact families draw
TWO different-looking variants side by side (left + right), spaced to fill the display like a wavetable does; long
families (flute, cello, bass, guitar, piano, organ, harp…) stay single. No sax "sound" arcs. Header centerline law:
OSC · name · articulation · ‹ instrument › · A · + all on ONE line through the ink centres (±0.5 px), in the REAL WebView.
Right-click menu on the Organics osc = the same menu every engine has (mute / solo / level / …).

**Owners (tp105):** V = visuals & UI (`index.html`, `Tests/_org_*.js`, real-WebView harnesses) · E = engine + integration
C++ (`Source/organics/*`, `SynthVoice.h`, `PluginProcessor.*`, `PluginEditor.*`, `ParameterIDs.hpp`, `OscBankIds.h`,
`SynthModConfig.h`, `CMakeLists.txt`, C++ tests) · L = library (`Tools/organics/**`, `Resources/Organics/**`, the compiled
library outside git, `Tests/organics_compile_test.py`, new fixtures folders).

---

## tp106 AMENDMENT (2026-09-24, the final overpass) — measured, then changed

**Engine.** Release at 1.0 = max(12 s, **2 ×** the authored release) ≤ 30 s (was max(12 s, authored): the upper half of the
knob was dead on every instrument authored at ≥ 12 s — the glockenspiel's 15 s). Tone's tilt pivot = max(700 Hz, the lead
note's f0) (a fixed 700 Hz pivot sat under every partial above ~F5, so Tone only changed the level there). A reader whose
estimated level falls under −90 dBFS retires with the 5 ms fade (the old hard stop stepped the output by up to
−83 dBFS). **Voice:** an engine switch under a sounding note fades the osc out through its own 4 ms gate, switches at
silence and fades back in (it switched on the spot: a 48 dB HP-residual click, on every engine).

**Library (Tools/organics).** Every sustain loop is POLISHED at compile time (`analyse.polish_loop`): a repeating swell is
flattened with a 200 ms gain curve (not on recipes with `"loopFlatten": false` — the organs and the accordion), the
cleanest crossfade length (10–200 ms) is chosen by an HP-residual seam metric and BAKED into the audio with an
energy-preserving law for the measured correlation, 8 pad frames after `le` repeat the frames from `ls`, and **the map's
`xf` is 0** (the runtime wraps le → ls with no crossfade of its own). `tfix` is per take on performed instruments
(Strings, Winds, Brass, Choir & Voice) and per note on struck / plucked ones; an authored whole-semitone transpose that
compensates the sample's own offset counts as a tuning correction. A file whose mean is over 2 % of its RMS gets a causal
8 Hz high-pass (DC). Loudness is closed THROUGH THE ENGINE: `Tools/organics/engine_calibrate.py` measures the calibration
point with the runtime (`Tests/organics_audit.sh calib`) and moves every region of the instrument by one offset (−24 LUFS
± 0.05; the v127 peak of that key ≤ −1 dBFS). New recipe keys: `loopFlatten`, `loopMotionWhy` (the loop gate reports that
instrument's pump instead of failing it), `pitchCheckWhy` (the pitch check does not judge it).
