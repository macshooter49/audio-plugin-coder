# Sample Engine — Status & C++ Handoff (for Opus)

**Project:** Terrain Instrument (Waves Crate)
**Engine:** Oscillator engine #2 of 5 — Wavetable · **Sample** · Granular · Spectral · FM
**Date:** 2026-06-25
**Branch:** `feature/terrain-instrument`
**UI state:** Front panel **complete & installed** (VST3 + AU). 100% UI-only — **no C++ touched.** Audio is your handoff.
**Files of record:** UI = `Source/ui/public/index.html`. Design = `.ideas/sample-engine-v1-design.md`. This doc = the build status + the exact C++ contract.

---

## 0. The big idea (why this matters beyond Sample)

The Sample engine is a **per-oscillator front-end over one shared audio buffer** — Serum 2's model. Once the shared `SampleBuffer` + load path exist, **Granular and Spectral are alternate readers of the same buffer** (same Start/End, Loop, Scan vocabulary, reinterpreted per domain). So finishing Sample's DSP unlocks ~70% of Granular and Spectral. Build the buffer/loop/scan spine once, reuse three times.

Max's framing: *"because of what we're able to do, we're able to use samples for our granular and spectral engine, just like Serum 2."* Front already has a **grain** flavour; **Granular** here = true scan/density/length granular. Spectral = the hard one (FFT resynthesis + transient-detect timestretch).

---

## 1. WHAT'S BUILT (UI — all live in-DAW, all 4 oscillators A/B/C/D)

Pick **Sample** in an oscillator's header engine dropdown (`SYN_OSC_x_ENGINE` = index **1**) → the wavetable front swaps to the Sample view. Mechanism: the engine `paint()` toggles `.engine-sample` on the `.device.osc`; CSS shows `.sample-view`, hides `.front-only`. Knob row stays pixel-aligned with every other engine (both header rows are `position:absolute`, so nothing shifts the knobs).

**Visualizer (clean box — only waveform + lines + fades + playhead live inside):**
- Full-bleed waveform (synthetic placeholder until you feed the real buffer — see §3).
- **Outer Start/End** = white separator lines (no markers). Drag to set the playable region; outer area dims (trim shade).
- **Inner Loop brace LS/LE** = purple separator lines, hard-clamped inside Start/End (can never read out of bounds). Drag the **band** between them to slide the whole loop.
- **Fades = thin LFO-style ramp LINES** (SVG `.samp-env`, non-scaling stroke), not shadows. **Activated by SHIFT+drag** a region edge: shift+drag white Start/End → fade-in/out ramp; shift+drag purple LS/LE → loop crossfade ramp. Normal drag still moves the edge. Default fades = 0.
- **Playhead** = a thin white scan line that animates per loop mode (reverses in Reverse, bounces in Ping-Pong). **No dot** (removed per Max — line only).
- **Right-click** the waveform → glass menu: Add/Remove loop, Reverse, Trim to loop, Normalize, Fade in/out, Reset region. (Reverse/Normalize redraw live; the rest set state.)
- **Drop affordance:** drag a file over the box → it highlights + shows "＋ Drop a sample." (Real load is your handoff.)
- **⤢** glyph top-right → chop editor (stub; next increment).

**Header controls (top, as native `<select>` dropdowns — no click-cycle):**
- **Loop mode:** One-Shot · Forward · Reverse · Ping-Pong · Tailed. (One-Shot auto-removes the loop.)
- **Snap:** Off · Zero · Transient.
- The wavetable shape selector ("Sine") is **hidden in Sample mode** (no wave shapes here). A/S/N/+ emblems stay.

**Front 5 knobs** (same positions as wavetable; Title-case SF Pro):
`Scan · Stretch · Formant · Spray · X-Fade`. Interactive **now** via a UI-only fallback (so they feel real); the generic `.knob[data-syn]` handler **takes over automatically the moment your params exist** (fallback self-disables when `getSliderState(id)` is non-null).
- **Scan** and **Formant** render **BIPOLAR** (fill from 12 o'clock; center = 0; left = −, right = +). Scan must scrub/play **backwards (−)** and **forwards (+)**.

---

## 2. C++ HANDOFF — params to add (APVTS)

Add these and wire the **4-point WebSliderRelay** for each (relay member in `PluginEditor.h` → `.withOptionsFrom(relay)` in ctor → `WebSliderParameterAttachment` against the APVTS param → JS already reads it). Miss a link and the JS write silently no-ops. UI knob IDs and the JS `KNOB_DEFAULTS_NORMALISED` already exist for these.

| Param ID (×4: A/B/C/D) | Type | Range | Default | Behavior |
|---|---|---|---|---|
| `SYN_OSC_x_SAMPLE_SCAN` | float | **−1 … +1 (bipolar)** | **0** | Playback **rate + direction** across the buffer. 0 = frozen, + = forward, − = reverse, extremes = tape-stop. Fully modulatable. (Reused later by Granular scan.) |
| `SYN_OSC_x_SAMPLE_STRETCH` | float | 0 … 1 | 0 | **Time-stretch, pitch held** (use our Tones/Beats/Texture engines). 0 = off. Algorithm selectable later (right-click). |
| `SYN_OSC_x_SAMPLE_FORMANT` | float | **−1 … +1 (bipolar)** | **0** | Formant shift ± independent of pitch (≈ ±12 st UI). |
| `SYN_OSC_x_SAMPLE_SPRAY` | float | 0 … 1 | 0 | Per-note **random start scatter** (fire on note-on via NoteOn-random source). Our beat-Serum differentiator. |
| `SYN_OSC_x_SAMPLE_XFADE` | float | 0 … 1 | 0.12 | Loop crossfade length + equal-power curve, auto-scaled in-bounds. |

**Plus region/loop params** (UI state today is JS-local; promote to APVTS so it saves/automates/modulates):
| Param | Type | Notes |
|---|---|---|
| `SYN_OSC_x_SAMPLE_START` / `_END` | float 0..1 | playable region (drag handles). |
| `SYN_OSC_x_SAMPLE_LOOP_START` / `_LOOP_END` | float 0..1 | loop brace; clamp inside Start/End. |
| `SYN_OSC_x_SAMPLE_LOOP_MODE` | choice | One-Shot / Forward / Reverse / Ping-Pong / Tailed. (`SAMPLE_LOOP_MODE` already exists globally — decide per-osc vs shared.) |
| `SYN_OSC_x_SAMPLE_SNAP` | choice | Off / Zero-cross / Transient (handle snapping). |
| `SYN_OSC_x_SAMPLE_FADE_IN` / `_FADE_OUT` | float 0..1 | edge fades (shift-drag). |

**DSP foundation already on disk:** `SamplerVoice.h` (1408 lines), `SampleBuffer.h`, `SampleLoader.h`, `Slice.h`, and params `SLICE_MODE / SLICE_SUB_MODE (CHOP/CHROMATIC/RANDOM) / SAMPLE_LOOP_MODE / CHOP_FADE_MS`. The Sample engine should render off this shared buffer, not a new sampler.

**Also wire:** drag-and-drop file load into the shared buffer (UI fires standard dragover/drop on `.samp-disp`; expose a native function or hook so the dropped file path reaches C++), filename root-note auto-map (C3 default, MIDI 69 = A3), and feed the real waveform back to the UI for drawing (a `window.updateSampleWave(osc, Float32Array)`-style hook, mirroring the wavetable `updateOscCycle` pattern). Every new param should also be a **mod-matrix destination** and the engine output must run the existing per-voice **filter + FLOW** chain (no special-casing).

---

## 3. WHAT'S LEFT in the Sample engine (roadmap)

1. **C++ params + DSP render** (§2) — the big one. Scan/stretch/formant/spray/xfade DSP, loop modes (incl. Tailed release tail + Ping-Pong), edge fades, equal-power loop xfade.
2. **Real waveform feed** — replace the synthetic placeholder with the loaded buffer (UI hook ready to receive it).
3. **Drag-drop load + playback mapping** — actually load the dropped file; map across the keyboard from the root note.
4. **Per-knob & per-control right-click menus** (Max: *"all these buttons need right-click"*) — Scan → reverse / keytrack / tempo-fit; Stretch → Tones/Beats/Texture algorithm; Formant/Spray/X-Fade → curve/range; plus right-click on the dropdowns. Needs the param semantics from §2 first.
5. **Selectable KEY (root note) dropdown** — Max wants the key selectable (deferred from this pass).
6. **Length cap** — one-shots up to ~5 min; decide max + show a length readout (the old `A min · 2.00s` meta was removed; re-add a minimal length indicator if wanted).
7. **The ⤢ chop card (junior chop engine)** — Lux Cache / TE skin: Auto/Grid/Manual slicing, transient-detect, chop/dupe/reverse/trim/normalize, **key-playable slices** via `SLICE_SUB_MODE` (CHOP/CHROMATIC/RANDOM), BPM grid. Glyph present; click is a stub.
8. **Mod-matrix dests + filter/FLOW routing** for the new params.
9. **LS/LE numeric readouts** — removed for space; could return in a hover/drag readout.

---

## 4. RESEARCH for Opus (suggested deep-dives)

- **Pitch-independent time-stretch** for STRETCH (WSOLA / phase-vocoder / our Tones-Beats-Texture engines) — quality vs CPU.
- **Formant shifting** independent of pitch (cepstral / LPC / spectral envelope warping).
- **Transient detection** for Snap=Transient and the chop card's Auto slicing.
- **Loop crossfade** equal-power math + auto-scale-in-bounds (Serum 2.0.17 behavior).
- **Granular** (next engine): scan/density/length/window, ≥256 simultaneous grains, per-grain randomization (Serum's #1 gap) — reuses this buffer + scan.
- **Spectral** (hardest): FFT resynthesis + transient-detection timestretch, drawable spectral filter — reuses this buffer.
- Serum 2 / Vital / Pigments sampler comparison is already in `~/terrain-drop/compass_artifact_…markdown.md`.

---

## 5. Build / verify facts (carried)

- Live build tree = **`build-release/`** (not `build/` — stale). Unix Makefiles gen, arm64.
- index.html changed → **bust the WebUI BinaryData cache** (rm `juce_binarydata_TerrainInstrument_WebUI` + `CMakeFiles/TerrainInstrument_WebUI.dir` + `libTerrainInstrument_WebUI.a`) → reconfigure → build, then `strings`-verify the embed on the compiled binary.
- Build **both** VST3 + AU; install to `~/Library/Audio/Plug-Ins/{VST3,Components}/` (manual cp — builds don't auto-install).
- All inline `<script>` blocks pass `node --check`; the whole sample module is wrapped in try/catch so a UI error can't break the rest of the panel.
