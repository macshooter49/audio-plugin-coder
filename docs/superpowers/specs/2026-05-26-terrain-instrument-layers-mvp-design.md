# Terrain Instrument — Layers MVP (Mark 2, Phase 1)

**Spec date:** 2026-05-26
**Branch:** `feature/terrain-instrument`
**Author:** Brainstormed with the user 2026-05-26 evening session
**Status:** Approved, ready for implementation planning

## Mental model

Four complete Mark 1.5 samplers running in parallel, summed into one shared FX chain. The existing Mark 1.5 sampler **already is** Layer A — Phase 1 is about extracting it into a reusable `LayerState` struct and instantiating three more copies. No new DSP gets written; existing audio code paths carry forward verbatim, only ownership changes.

```
┌─────────────┐ ┌─────────────┐ ┌─────────────┐ ┌─────────────┐
│  LAYER A    │ │  LAYER B    │ │  LAYER C    │ │  LAYER D    │
│  sampler    │ │  sampler    │ │  sampler    │ │  sampler    │
│  + chops    │ │  + chops    │ │  + chops    │ │  + chops    │
│  + ROOT     │ │  + ROOT     │ │  + ROOT     │ │  + ROOT     │
│  + voices   │ │  + voices   │ │  + voices   │ │  + voices   │
└──────┬──────┘ └──────┬──────┘ └──────┬──────┘ └──────┬──────┘
       │  vol/mute/    │  vol/mute/    │  vol/mute/    │  vol/mute/
       │  solo gain    │  solo gain    │  solo gain    │  solo gain
       └──────────┬────┴───────┬───────┴───────┬───────┘
                  │            │               │
                  ▼            ▼               ▼
              Sum stereo (layer-mixer bus in processBlock)
                              │
                              ▼
        SHARED FX chain (GRAIN → TAPE → SPACE → EQ → DLY → MOD)
                              │
                              ▼
                         Master out
```

At any moment ONE layer is the **editing target** (`editingLayer` atomic, 0–3). All slicer-zone UI (waveform, chops, ROOT, PITCH/SLICE/1-SHOT/LOOP, SLICES, lab cards, scan controls, IN/OUT markers, etc.) reads/writes through the editing layer. Clicking an A/B/C/D pad swaps the editing target and the visible slicer state instantly hot-swaps to that layer.

MIDI in Phase 1 is broadcast to every layer that has a sample (the LAYER ALL play mode). Each layer independently dispatches the MIDI through its own slice context (so layer A in SLICE mode + layer B in PITCH mode coexist without coupling). ROUND ROBIN / RANDOM / SOLO play modes are explicitly Phase 3 work.

## Per-layer state — `LayerState` struct

Every per-sampler field currently on `TerrainInstrumentAudioProcessor` moves into a `LayerState` struct. The processor then owns `std::array<LayerState, 4> layers;`.

```cpp
struct LayerState {
    // Audio source
    tw::SampleBuffer    sampleBuffer;          // own audio data (independent buffer)
    tw::TerrainSynth    synth;                 // own 32-voice pool

    // Slice state (matches current Mark 1.5 fields exactly)
    std::shared_ptr<tw::SliceList> currentSlices;
    tw::Slice                       pitchModeSlice;   // virtual slice for PITCH mode
    std::atomic<int>                activeSliceIndex { 0 };
    std::array<std::atomic<float>, kMaxGlowSlots> sliceGlowLevel {};

    // Per-layer mode / config (APVTS-mirrored)
    std::atomic<int>    rootMidiNote { 60 };   // C4 default
    std::atomic<int>    sliceMode    { 0 };    // PITCH / SLICE / CHROM / RANDOM / LAYER
    std::atomic<int>    playMode     { 0 };    // 1-SHOT / LOOP
    std::atomic<int>    sliceCount   { 4 };
    std::atomic<float>  chopFadeMs   { 5.0f };

    // Mixer (NEW for Mark 2)
    std::atomic<float>  volume       { 1.0f }; // 0..2
    std::atomic<bool>   mute         { false };
    std::atomic<bool>   solo         { false };
    // pan deferred to Phase 2 (Mixer UI)

    // Identity & helpers
    int                 layerIndex { 0 };       // 0..3 = A/B/C/D
    juce::String        sourceFileName;         // for preset save
    bool hasSample() const noexcept { return sampleBuffer.getNumSamples() > 0; }
};
```

The processor adds:
- `std::array<LayerState, 4> layers;`
- `std::atomic<int> editingLayer { 0 };` — which layer the UI is currently targeting

All existing per-sampler-equivalent fields on the processor (`sampleBuffer`, `synth`, `currentSlices`, `pitchModeSlice`, `activeSliceIndex`, `sliceGlowLevel`, `chopFadeMsAtomic`, the active `rootMidiNote`, `sliceMode`, `playMode`, etc.) are DELETED from the processor and accessed via `layers[editingLayer]` from the UI bridge, or via the appropriate layer index from the audio thread.

The `LayerState` per-chop fields (`tw::Slice` struct, with all its existing fields: `startSample`, `endSample`, `pitchOffsetSemis`, `reverse`, `warpMode`, `stretchRatio`, `scanEnabled`, `scanRate`, `scanWindow`, `attackMs`, `decayMs`, `sustainLevel`, `releaseMs`, `volume`, plus `fxIndependent` and per-FX flags) are unchanged — they ride along inside each layer's `currentSlices` list.

## Voice pool

Per the user's "duplicate 4 times" directive: each `LayerState` has its own `tw::TerrainSynth` with the existing 32-voice pool. **Total: 128 voices across the plugin.** Idle voices cost nothing; the headroom is free until played. JUCE handles 128 voices without breaking a sweat.

Each `TerrainSynth` constructs identically to today (same `SamplerSound`, same `SamplerVoice` setup), just pointed at the layer's own `sampleBuffer` instead of the shared one. The construction sequence currently in `TerrainInstrumentAudioProcessor` (lines 15–22 of `PluginProcessor.cpp` — `synth.addSound(...)` + N `synth.addVoice(...)` calls) repeats inside `LayerState`'s constructor.

## UI editing target — `editingLayer` switch

The A/B/C/D pads already exist visually (committed at `a0d05e9`). In Phase 1 they become functional:

- **Click an A/B/C/D pad** → `editingLayer` atomic flips to that index (0/1/2/3)
- **Pad visuals update**: clicked pad gets `.active` (purple gradient), others lose `.active`. The `.placeholder` class is removed from all of them — they're real layers now, not Mark 2 placeholders.
- **Slicer UI hot-swaps** to show the new layer's state. The existing JS poll loops (`pollSliceGlow`, `pollScanViz`, `getActiveSliceIndex`, native fn reads) already read atomics — we just route them through `layers[editingLayer]` on the C++ side. Wave canvas redraws, lab cards re-source from new state, ROOT pill updates, PITCH/SLICE mode toggles to whatever this layer was last in.

New native function:
- `setEditingLayer(int idx)` — flips the atomic, triggers a UI refresh poll on the JS side.

Every existing native fn that touches per-sampler state gets a thin shim that reads `editingLayer` first, then dispatches into `layers[editingLayer]`. List of affected native fns (audit during planning):
- `getSlices` / `replaceSlices`
- `setSliceReverse` / `setSliceADSR` / `setSliceVolume` / `setSlicePitch` / `setSliceWarp*` / `setSliceScan*`
- `auditionSlice`
- `getActiveSliceIndex` / `setActiveSliceIndex`
- `getSliceGlowLevels`
- `isAnyVoicePlaying` (also extended to be per-layer — see "Pad lit-up" below)
- `setPitchSliceBounds`
- `loadSampleFromBase64` / `loadSampleFromPath` — load into `layers[editingLayer]`
- `setRootMidiNote`, `setSliceMode`, `setPlayMode`, `setSliceCount`, `setChopFadeMs`

**Loading a sample respects `editingLayer`** — drag-drop or click-to-load lands the bytes in the currently-edited layer. Workflow: click B pad → drag a sample → it loads into layer B.

**Pad lit-up indicator** becomes per-layer:
- Extend `isAnyVoicePlaying` → `getLayerVoiceActivity()` returning a 4-element bool array
- JS poll inside `pollSliceGlow` toggles `.playing` class on each pad independently
- All 4 pads can light up simultaneously in LAYER ALL mode when a key triggers samples on multiple layers

## Mixer state (vol / mute / solo per layer)

Lives per `LayerState`. Mixer math runs once per audio block in `processBlock` after all 4 synths render into per-layer temp buffers:

```cpp
// Inside processBlock — pseudocode
const bool anySolo = std::any_of(layers.begin(), layers.end(),
                                  [](auto& l){ return l.solo.load(); });

masterBuf.clear();
for (int li = 0; li < 4; ++li) {
    auto& layer = layers[li];
    if (! layer.hasSample())  continue;
    if (layer.mute.load())    continue;
    if (anySolo && ! layer.solo.load()) continue;
    layer.synth.renderNextBlock(layerBuf[li], midiBuf, 0, numSamples);
    const float g = layer.volume.load();
    masterBuf.addFrom(0, 0, layerBuf[li], 0, 0, numSamples, g);
    masterBuf.addFrom(1, 0, layerBuf[li], 1, 0, numSamples, g);
}
// masterBuf now feeds the shared FX chain
```

For Phase 1, **vol/mute/solo state is editable via APVTS or new native fns** but the visible Mixer UI is deferred to Phase 2. Defaults: vol=1.0, mute=false, solo=false. Pan deferred to Phase 2 (Mixer UI is the natural home for the pan knob).

A per-block scratch buffer per layer (`std::array<juce::AudioBuffer<float>, 4> layerScratch;`) gets allocated in `prepareToPlay` to avoid audio-thread allocation.

## MIDI dispatch — LAYER ALL only (Phase 1)

For Phase 1, **every MIDI event broadcasts to every layer with a sample**. Simplest possible dispatch:

```cpp
// Inside processBlock, before the per-layer render loop above
for (int li = 0; li < 4; ++li) {
    if (! layers[li].hasSample()) continue;
    // Each layer's synth processes the SAME midiBuf reference
}
```

`midiBuf` is shared — every active layer sees every note. Each layer's own `TerrainSynth` does its own slice dispatch using ITS `SliceContext` (built from its own `sliceMode` / `playMode` / `sliceCount`). So Layer A in SLICE mode + Layer B in PITCH mode + Layer C in CHROM mode all coexist independently.

ROUND ROBIN / RANDOM / SOLO play modes (the mixer-level dispatch options the user named) are **Phase 3 work**. Phase 1 = LAYER ALL only.

## Preset format + V1 migration + empty-state

**V2 preset format** (per-layer state serialized in an array):

```jsonc
preset.json {
  "version": 2,
  "editingLayer": 0,
  "layers": [
    {
      "sample": "...wav path or embedded base64...",
      "sourceFileName": "...",
      "chops": [ /* tw::Slice array */ ],
      "pitchModeSlice": { /* tw::Slice fields */ },
      "activeSliceIndex": 0,
      "rootMidi": 60,
      "sliceMode": 1,
      "playMode": 0,
      "sliceCount": 4,
      "chopFadeMs": 5.0,
      "volume": 1.0,
      "mute": false,
      "solo": false
    },
    { /* layer B — empty if no sample */ },
    { /* layer C — empty if no sample */ },
    { /* layer D — empty if no sample */ }
  ],
  // Shared global state below (unchanged from V1 schema)
  "fx": { "grain": { /*...*/ }, "tape": { /*...*/ }, "space": { /*...*/ }, "eq": { /*...*/ }, "delay": { /*...*/ } },
  "mod": { /* unchanged from V1 */ }
}
```

**V1 → V2 migration** on preset load:
- Detect `version` missing or `< 2` → V1 preset.
- V1's single-sample state → **`layers[0]` (layer A)** verbatim. All existing fields (chops, ROOT, sliceMode, playMode, sliceCount, chopFadeMs, etc.) move into `layers[0]`. No data loss, no surprise behavior.
- `layers[1..3]` default to empty (no sample, default slicer state, vol=1.0, mute=false, solo=false).
- `editingLayer` set to 0 so the user sees layer A on open (matches Mark 1.5 behavior).
- This is one-way: once saved as V2, the preset can't downgrade to V1. User's existing V1 presets remain readable; new saves are V2-only.
- Per-preset migration runs lazily on `setStateInformation` — no batch upgrade pass.

**Empty-state prompt per layer:**

When `editingLayer` changes OR a sample loads:
- A new native fn `getLayerHasSample(int layerIdx)` returns the layer's `hasSample()` (or piggyback this onto existing poll fns)
- JS reads the editing layer's sample status and toggles `#hero.empty-state` accordingly:
  - `!hasSample(editingLayer)` → `#hero.classList.add('empty-state')` → "DRAG SAMPLE OR CLICK TO LOAD" reappears
  - `hasSample(editingLayer)` → remove the class, waveform renders
- The existing click-to-load + drag-drop handler already targets the current sampler — no chain rewiring needed beyond making the loader write into `layers[editingLayer].sampleBuffer`

**Cold start workflow:** Open plugin with no preset → all 4 layers empty → all 4 pads show the prompt when clicked. Load a sample into A → only A shows waveform; B/C/D still show the prompt until populated.

## Phase 1 scope — IN vs OUT

### IN (this spec, this phase)

- `LayerState` struct holding all per-sampler state listed above
- 4 instances → `processor.layers[4]`
- `editingLayer` atomic + `setEditingLayer` native fn + UI swap on pad click
- Per-layer voice pool (32 voices each, 128 total)
- Per-layer mixer state (vol/mute/solo) wired through processBlock summing
- MIDI broadcasts to all layers with samples (LAYER ALL mode)
- Empty-state prompt per-layer
- V1 → V2 preset migration
- All existing native fns shimmed to dispatch via `layers[editingLayer]`
- A/B/C/D pads: click = select edit target; B/C/D lose `.placeholder` class once layers are real (no longer "coming in Mark 2")
- `.playing` indicator per pad (extend `isAnyVoicePlaying` to per-layer)

### OUT (deferred to later phases)

- **Mixer page UI** (Phase 2 — the MIX pill opens a 4-strip console)
- **Pan per layer** (Phase 2 — naturally belongs with the Mixer UI)
- **ROUND ROBIN / RANDOM / SOLO play modes** (Phase 3 — mixer-level dispatch)
- **Per-layer FX routing** (Phase 4 — the Independent UI lights up; per-layer FX chains)
- **Per-layer mod matrix** (Phase 5 — mod targets scope to layer)
- **Sequencer driving layers** (Phase 6+)
- **MPE per-layer** (Phase 7+)

## Risks and constraints

1. **CPU at 128 voices**: 4 × 32 voices is significant headroom. Real-world load is bounded by *active* voices. With idle voices costing ~zero (JUCE only renders `isActive` voices), the plugin only consumes CPU proportional to actual polyphony. Risk is low but should be benchmarked at the end of Phase 1.
2. **Memory at 4× sample buffers**: A user loading 60s @ 48kHz stereo float into all 4 layers = ~46 MB × 4 = ~184 MB. Acceptable for a modern plugin; comparable to Kontakt instruments.
3. **Preset V1 → V2 surface area**: every V1 field needs a clear destination in V2 layer[0]. Missing a field = silent data loss. Migration code requires explicit-field testing.
4. **Native fn audit**: every native fn that touches per-sampler state must be shimmed. Missing one = stale UI / wrong layer edited. Implementation plan must enumerate every affected fn.
5. **Allocation discipline**: per-layer scratch buffers must allocate in `prepareToPlay`, not in `processBlock`. Standard JUCE discipline.
6. **WebSliderRelay registration**: any new per-layer APVTS params (vol/mute/solo) exposed to the WebView need 4-place registration per the existing project gotcha (see memory: `terrain-websliderrelay-gotcha`).

## Testing approach

- **Smoke**: load V1 preset → confirm layer A shows V1 content; B/C/D show empty-state prompt
- **Layer switch**: click each pad → confirm slicer state instantly swaps; ROOT, slices, mode, lab cards all reflect the right layer
- **Empty-state**: click empty B/C/D pad → "DRAG SAMPLE OR CLICK TO LOAD" appears; drop sample → it loads into that layer specifically
- **LAYER ALL playback**: load different samples into A/B/C/D → press any key → all 4 fire simultaneously
- **Mute/solo**: APVTS-driven mute on layer B silences only B; solo on layer C silences A/B/D (anySolo logic)
- **Pad lit-up**: per-layer `.playing` class fires correctly when only specific layers have voices active
- **Preset save+reload**: save V2 preset → reload → all 4 layers' samples, chops, modes, mixer state restored
- **CPU benchmark**: profile against Mark 1.5 baseline with 1, 2, 4 active layers
- **pluginval level 5**: must continue to pass after refactor

## Related memory

- `terrain-instrument-mark-2-vision.md` — the 8-pillar arc
- `terrain-instrument-mark-2-frame-pivot.md` — no-resize strategy (still holds)
- `terrain-instrument-mark-2-slicer-zone-chrome.md` — A/B/C/D pad chrome already shipped
- `terrain-instrument-mark-1.5-checkpoint.md` — the proven Mark 1.5 sampler this duplicates
- `terrain-instrument-fx-independence-mark-1.md` — Independent UI tease; lights up in Phase 4
- `terrain-websliderrelay-gotcha.md` — APVTS↔WebView registration discipline

## Open items deferred to implementation plan

- Exact list of native fns requiring per-layer shimming (full audit at planning time)
- APVTS schema diff (which params get layer-indexed `_a`/`_b`/`_c`/`_d` suffixes vs which stay global)
- Decision on whether per-layer params get APVTS entries OR live as raw atomics on `LayerState` (impacts host parameter automation surface)
- Per-layer scratch buffer allocation sizing in `prepareToPlay`
- Test fixture/preset to demonstrate cross-layer LAYER ALL playback during dev
