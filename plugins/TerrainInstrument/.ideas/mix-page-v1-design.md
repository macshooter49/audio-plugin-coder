# Mix Page v1 — Design Spec

**Project:** Terrain Instrument
**Phase:** Mark 2 Phase 2 (Mix page — first Phase 2 deliverable)
**Date:** 2026-05-27
**Status:** Design locked (user-approved sections 1-4). Implementation planning + localhost mockup next.
**Branch baseline:** `feature/terrain-instrument` @ tag `mark-2-phase-1-layers-polish` (`4964f9f`)

---

## 1. Goal

Make the 4 sampler layers (A/B/C/D, shipped in Phase 1) **mixable, creatively triggerable, and individually exportable** without leaving the plugin. This is the first user-facing deliverable behind the inert MIX pill in the header, and the first stop before the Sequencer (Phase 3) and Synth (Phase 4+).

**Driving user need (verbatim from the brainstorm):** *"People want as much independence as possible, and that's what we're giving them. … operate on every chop independently with full studio-grade effects."*

The Mix page makes the **per-layer independence story** real on the levels/routing axis the same way the polish round made it real on the visual/sliceMode axis.

## 2. Scope

### In scope (v1)

1. **4 channel strips** (A/B/C/D) — fader, pan, mute, solo, peak meter, pitch-jitter knob
2. **5 trigger modes** — LAYER (default) / ROUND-ROBIN / RANDOM (weighted) / SOLO (multi-select) / VELOCITY (zone split)
3. **Stem capture** — 10-min rolling per layer + master, drag-to-DAW per layer or all 4 at once, DRY/MIX source toggle
4. **Per-layer pitch jitter** — small knob per strip, 0–100 cents random per voice (kills phasing on stacks)

### Out of scope (v2 backlog — capture in memory)

- Choke groups (A↔C, B↔D pairwise)
- Snapshot scenes (8 mixer-state slots + XY morph pad between two)
- Beat-locked auto-mute step patterns per layer
- Freeze-and-resample (bounce a layer in place)
- Per-layer FX routing (deferred to Phase 4 anyway, but blocks "true post-FX" stems)
- Per-layer sliceSubMode, sliceCount, ADSR, chopFadeMs (still APVTS-broadcast in Phase 1)
- Global ADSR / global lab card / multi-select chops (user flagged for later)

## 3. Layout & footprint

- **Activation:** clicking the MIX pill in the header opens the Mix page in the bottom panel area (the zone currently used by FX/EQ/DLY/MOD). Same mutually-exclusive swap pattern as the existing panel pills.
- **Footprint:** bottom panel only. Top half of the window (waveform, A/B/C/D layer pads, mode pills, sequencer transport chrome) is **untouched**. The very bottom OUTPUT / MIX-knob / CAPTURE strip stays put underneath.
- **Internal split:** LEFT half = 4 vertical channel strips. RIGHT half = trigger-mode area (top) + stem capture area (bottom).

## 4. Channel strip anatomy (LEFT half)

Each of the 4 strips is ~70-80px wide. Top to bottom:

1. **Header pill** — letter "A"/"B"/"C"/"D" + per-layer `.playing` indicator (already wired from Phase 1 Task 11 via `getLayerVoiceActivity`)
2. **Pan knob** — small, ±100 L/R; double-click resets to center
3. **Vertical fader** — main volume control (FL Studio mixer vibe); range 0–200% (linear gain 0.0–2.0, already supported by `layer.volume` atomic)
4. **Meter** — peak LED bar alongside the fader, fed from a new per-layer post-volume peak atomic
5. **Pitch-jitter knob** — tiny, below the fader; 0–100 cents random offset applied per-voice at trigger time
6. **M / S buttons** — strip mute and strip solo (mixer-level, persistent)

### Two SOLO concepts kept orthogonal

| Concept | Lives | Behavior |
| --- | --- | --- |
| **Strip SOLO** (M/S button in strip) | Channel strip | Persistent mixer mute-everything-else; affects summing of `layer.volume` × output |
| **Trigger-mode SOLO** (checkboxes in right panel) | Trigger contextual area | Determines which layers are **eligible to fire** on each incoming MIDI note |

These are independent. Strip-mute can be on while trigger mode is LAYER (still fires that layer's voices, just mutes its contribution to the sum). Trigger SOLO can deselect a layer while its strip-solo button is on (no notes fire to it). Both useful, both stay.

## 5. Trigger-mode pills + contextual area (RIGHT half, top)

### Pill row

`LAYER · ROUND-ROBIN · RANDOM · SOLO · VELOCITY`

- Same visual treatment as the slicer's PITCH/SLICE pills (purple accent on active)
- Mutually exclusive — exactly one active
- **Global** setting (not per-layer) — it's a "how do the 4 layers interact" choice

### Contextual content (swaps per active mode)

#### LAYER mode (default)
- Status line: "All populated layers fire together"
- 4 small letter indicators (A/B/C/D), each dimmed if the layer is empty, lit if loaded
- No config controls — clean look

#### ROUND-ROBIN mode
- Status line: "Cycling A → B → C → D" with the **next** letter highlighted
- **RESET TO A** button (force next note = A)
- **SYNC TO BAR** toggle: when on, position resets at each bar boundary
- Empty layers are automatically skipped in the cycle

#### RANDOM mode
- 4 horizontal probability sliders A/B/C/D (0-100% each), color-coded by letter
- **Semantics: exactly one layer per note**, picked via weighted random over the weight distribution
  - 25/25/25/25 → uniformly random one-per-note
  - 100/0/0/0 → always A
  - 50/50/0/0 → coin flip A or B
- Default weights: 25/25/25/25
- **RESET TO UNIFORM** button

#### SOLO mode
- 4 large checkboxes A/B/C/D
- Multi-select (any combo of 1-4 layers can be checked)
- Checked layers fire together (like LAYER mode but only the checked subset)
- Default: 1 checked (the layer that's currently being edited via the pad)
- **0 checked = nothing plays** (predictable, user-controlled silence). No silence-proofing.

#### VELOCITY mode
- Horizontal velocity-range bar 0-127
- 4 colored zones (A→D low→high) with 3 draggable boundary handles
- Each zone fires its assigned layer when MIDI note velocity lands in that range
- Default zones: 0-31 (A), 32-63 (B), 64-95 (C), 96-127 (D)
- Zones stay contiguous and non-overlapping (dragging a boundary just resizes neighbors)
- **RESET ZONES** button to restore even quarters

## 6. Stem capture area (RIGHT half, bottom)

### Per-layer drag handles
- 4 drag-source buttons labeled A/B/C/D
- Each is a drag origin for that layer's 10-min rolling stem buffer
- Drag-out → WAV file for just that layer
- Visual: subtle drag-handle affordance, hover cursor change

### DRAG ALL 4 button
- Single drag origin that produces 4 simultaneous WAV files (one per layer)
- Convenience over dragging 4 times — same buffer source as individual handles

### Stem source toggle: DRY / MIX
- **DRY** = layer's pure synth output (sample + chops + warp + pitch-jitter — no volume/pan applied)
- **MIX** = layer output WITH channel-strip volume + pan, still PRE shared FX chain
- Labeled **DRY / MIX** (not PRE/POST-FX) because the shared FX chain runs on the summed output and per-layer FX doesn't exist until Phase 4. When Phase 4 lands, this toggle gets a third option (POST-PER-LAYER-FX).

### Capture timing
- Synchronized with the main CAPTURE bar (the existing OUTPUT-strip count). All 5 buffers (4 stems + 1 master) tick at the same moment, so a drag at time T returns the 10-min window ending at T.

## 7. Engineering architecture

### New per-layer atomics on `tw::LayerState`

```cpp
std::atomic<float> pan { 0.0f };              // -1.0 (L) .. +1.0 (R)
std::atomic<float> pitchJitterCents { 0.0f }; // 0..100 cents random
std::atomic<float> probabilityWeight { 0.25f }; // 0..1, normalized weight for RANDOM mode
std::atomic<bool>  soloSelected { true };     // for trigger-mode SOLO checkboxes
std::atomic<int>   velocityZoneMin { 0 };     // 0..127 inclusive
std::atomic<int>   velocityZoneMax { 31 };    // 0..127 inclusive
std::atomic<float> peakLevelL { 0.0f };       // post-volume peak for meter
std::atomic<float> peakLevelR { 0.0f };
```

### New processor-level state

```cpp
std::atomic<int> triggerMode { 0 };  // 0=LAYER 1=RR 2=RANDOM 3=SOLO 4=VELOCITY
std::atomic<int> roundRobinPos { 0 }; // current cursor for RR (0..3)
std::atomic<bool> rrSyncToBar { false };
juce::String stemSourceMode { "DRY" }; // "DRY" | "MIX"

// 4 rolling stem buffers, shared with the editor for drag-out
std::array<StemBuffer, 4> stemBuffers; // see implementation note below
```

### MIDI dispatch changes (processBlock)

Today: every MIDI note triggers every populated layer (effectively LAYER mode hardcoded).

After: MIDI handler routes each incoming note through a "which layers fire?" filter based on `triggerMode`:

| Mode | Filter behavior |
| --- | --- |
| LAYER | All populated layers receive the note (current behavior) |
| RR | One layer receives — the layer at `roundRobinPos`, advance to next populated layer (skip empties) |
| RANDOM | One layer receives — weighted random pick over `probabilityWeight` array (renormalized to skip empties) |
| SOLO | Layers with `soloSelected == true` receive (subset of LAYER) |
| VELOCITY | One layer receives — the layer whose `[velocityZoneMin..Max]` contains the note's velocity |

The filter is applied at MIDI dispatch time per note, BEFORE `layer.synth.renderNextBlock`. Selected layers' synths receive the MIDI message normally; un-selected layers' synths don't get it.

### Pitch jitter

When a voice starts on a layer with `pitchJitterCents > 0`, the voice picks a random cents offset in `[-jitter, +jitter]` and applies it to the voice's playback rate for its entire lifetime. Implementation: pass jitter atomic to `SamplerVoice` constructor, sample at `startNote()`.

### Stem buffers

- 4 × `StemBuffer` instances, one per layer, each a rolling 10-min stereo `float32` ring (or a shorter default + int16 storage — see RAM constraint below).
- Written from `processBlock` after the per-layer `layerScratch[li]` is computed:
  - If `stemSourceMode == "DRY"`: write the raw `layerScratch[li]` (before volume/pan)
  - If `stemSourceMode == "MIX"`: write `layerScratch[li] × layer.volume × pan-coeffs`
- Read on drag-start via a native fn (returns a temp WAV file path that JUCE drag-and-drop can use).

**RAM constraint:** 10 min × 4 stems × 2 ch × float32 × 48 kHz ≈ 920 MB. Mitigation options to pick at implementation time:
1. Default to **5-min** rolling (halves to ~460 MB)
2. Use **int16** storage internally (halves again to ~230 MB; convert to float on write)
3. Allocate lazily — only allocate a stem buffer when its drag handle is first touched (preserves 0 MB until user opts in)

My lean: option 3 + option 1 (5-min default, lazy alloc) — fits the "weird feature, don't worry about CPU" user spirit while keeping idle memory low.

### New native fns (UI ↔ engine)

```
setTriggerMode(int mode)                // 0..4
getTriggerMode() -> int

setLayerPan(int layerIdx, float panNormalized)
getLayerPan(int layerIdx) -> float

setLayerPitchJitter(int layerIdx, float cents)
getLayerPitchJitter(int layerIdx) -> float

setLayerProbabilityWeight(int layerIdx, float weight01)
getLayerProbabilityWeight(int layerIdx) -> float

setLayerSoloSelected(int layerIdx, bool selected)
getLayerSoloSelected(int layerIdx) -> bool

setLayerVelocityZone(int layerIdx, int minVel, int maxVel)
getAllVelocityZones() -> [{layer, min, max}, ...]  // returns all 4 in one call

setStemSourceMode(string "DRY" | "MIX")
getStemSourceMode() -> string

setRrSyncToBar(bool)
getRrSyncToBar() -> bool
resetRoundRobin()  // force next note = A

getLayerPeakLevels() -> [{l, r}, {l, r}, {l, r}, {l, r}]  // for meters; polled at ~30Hz

beginStemDrag(int layerIdx) -> string  // returns temp file path; -1 = all 4
beginAllStemsDrag() -> [path, path, path, path]
```

### JS state additions

Add to the IIFE state (PluginEditor.cpp:3611+ region):

```js
state.mixPage = {
  open: false,                  // tracks whether MIX pill is the active panel
  triggerMode: 0,
  rrSyncToBar: false,
  stemSource: 'DRY',
  perLayer: [
    // 4 entries; persisted in state.layerStates[] mirrors for switch-aware fields
    // (pan, pitchJitter, soloSelected, velocityZone, probabilityWeight)
    { pan: 0, pitchJitter: 0, soloSelected: false, velocityZone: [0, 31], probabilityWeight: 0.25 },
    { pan: 0, pitchJitter: 0, soloSelected: false, velocityZone: [32, 63], probabilityWeight: 0.25 },
    { pan: 0, pitchJitter: 0, soloSelected: false, velocityZone: [64, 95], probabilityWeight: 0.25 },
    { pan: 0, pitchJitter: 0, soloSelected: false, velocityZone: [96, 127], probabilityWeight: 0.25 }
  ],
  peakLevels: [{l: 0, r: 0}, {l: 0, r: 0}, {l: 0, r: 0}, {l: 0, r: 0}]
};
```

Pan / pitchJitter / soloSelected / velocityZone / probabilityWeight should fold into the existing per-layer snapshot/restore in `snapshotCurrentLayer()` and `restoreLayerSnapshot()` so they survive pad-switch + editor close/reopen. Trigger mode + RR sync + stem source are GLOBAL (not per-layer), live as plain `state.mixPage.*` fields.

### Preset persistence

V2 preset save (per-layer ValueTree) gains:
- `pan`, `pitchJitter`, `soloSelected`, `velocityZoneMin`, `velocityZoneMax`, `probabilityWeight` per layer node
- `triggerMode`, `rrSyncToBar`, `stemSourceMode` at the root (next to `editingLayer`)

V1 backward-compat: missing properties default to factory values. No special seed needed (V1 didn't have any of this).

### MIX pill activation wiring

The MIX pill in the header (shipped 2026-05-26 in `terrain-instrument-mark-2-header-syn-mix-pills`) is currently inert. Wire its click handler to:
- Toggle `state.mixPage.open`
- Show/hide the Mix page DOM in the bottom panel
- Coordinate with the existing FX/EQ/DLY/MOD pills so only one panel is visible at a time (their existing mutual-exclusion logic)

## 8. Open questions deferred to implementation

1. Stem buffer storage: int16 vs float32 + default 5-min vs 10-min — final pick during impl
2. Pitch jitter applied at note-on only, or also continuously modulated per cycle? (Lean: note-on only — simpler, more stable.)
3. Strip-mute interaction with trigger-mode SOLO: if a layer is strip-muted AND solo-checkbox-selected, should it FIRE silently (voice triggered, contributes 0 to sum) or NOT FIRE at all (no voice triggered)? (Lean: fire silently — strip-mute is downstream of trigger, voice should still go through MIDI dispatch.)
4. Meter ballistics: peak-hold + decay rate. Industry standard is ~300ms hold + 12 dB/sec decay.

## 9. Verification checklist (post-implementation)

- [ ] MIX pill opens/closes the Mix panel; mutually exclusive with FX/EQ/DLY/MOD
- [ ] Each strip's fader changes only its layer's contribution to the master sum
- [ ] Pan knob pans only its layer; ±100 fully L/R
- [ ] Strip mute silences only its layer
- [ ] Strip solo silences all other layers' contributions to the sum
- [ ] LAYER mode: all populated layers fire on every note
- [ ] ROUND-ROBIN: cycles through populated layers, skips empty
- [ ] RANDOM with uniform weights: distribution looks uniform over many notes
- [ ] RANDOM with weights set to 100/0/0/0: only A fires
- [ ] SOLO with A+D checked: only A and D fire; B and C silent
- [ ] SOLO with 0 checked: nothing plays
- [ ] VELOCITY zones: low velocity hits A only, high velocity hits D only, middle zones hit B/C
- [ ] Per-layer pitch jitter: stacked layers sound thicker (no destructive phasing)
- [ ] Stem drag handles produce correct per-layer audio (DRY mode)
- [ ] Stem drag handles produce volume+pan-applied audio (MIX mode)
- [ ] DRAG ALL 4 produces 4 simultaneous files
- [ ] Capture timing matches main CAPTURE bar
- [ ] V2 preset save/load round-trips all new state correctly
- [ ] Editor close+reopen preserves all Mix page state
- [ ] Pluginval level 5 PASS

## 10. Implementation phases (rough)

Phase A — engine + persistence (no UI yet):
- Add per-layer atomics + trigger-mode atomic
- Implement MIDI dispatch filter
- V2 preset save/load extension
- Test via existing slicer UI controls (volume/mute/solo already wired)

Phase B — strips UI:
- Wire up MIX pill activation
- Render 4 channel strips with fader/pan/M/S/meter/pitch-jitter
- Native fns + state.* hooks for the new fields

Phase C — trigger mode area:
- 5-pill row + contextual swap area
- Each mode's controls (sliders/checkboxes/velocity bar)

Phase D — stem capture:
- 4 rolling stem buffers + lazy allocation
- Drag handles + DRAG ALL 4 wiring (JUCE startDragging)
- DRY/MIX toggle

Phase E — polish + verify (the checklist above).

A natural break for user verification between each phase.

---

**Related memories:**
- [[terrain-instrument-mark-2-phase-1-layers-mvp-shipped]]
- [[terrain-instrument-mark-2-phase-1-layers-polish-shipped]]
- [[terrain-instrument-mark-2-header-syn-mix-pills]] (MIX pill DOM + existing inert-click handler)
- [[terrain-instrument-mark-2-vision]] (Phase pillar arc)
