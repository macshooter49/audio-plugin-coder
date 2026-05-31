# Per-chop FX-independence routing — Design Spec (option 1: shared chain)

**Plugin:** Terrain Instrument (Mark 2, post-Mix-page)
**Branch:** `feature/terrain-instrument`
**Date:** 2026-05-30
**Status:** Approved, ready for implementation plan
**Scope flag:** v1 keeps shared global FX params (per-chop FX params is a future feature)

## Background

This is the second design pass on per-chop FX-independence routing. A prior
spec (committed as `b8e19fd` then reverted) proposed a 4-bucket pool of
private FX-chain instances so simultaneous indy chops with different masks
could be processed in isolation. That implementation shipped, hit subtle
behavioral bugs (silent dry pass-through on empty mask, unwanted master-FX
bleed paths, click-on-release), and was reverted at user request in favor
of a simpler architecture with explicit tradeoffs.

This spec covers the simpler architecture: **one shared indy FX chain**
that processes the existing `indyCaptureBus` with a per-block bypass mask
computed by OR-ing every currently-playing indy voice's chip selection.

Chip UI, `Slice` data model, and JSON persistence are already shipped (no
JS / WebView changes needed). What's missing — and what this spec
defines — is the audio-thread routing that turns those flags into per-FX
behavior on the indy bus.

## §1 — Architecture (one shared indy chain)

### Restore `indyCaptureBus` singleton

The pre-our-work baseline has a single `juce::AudioBuffer<float>
indyCaptureBus` on the processor. Voices marked `fxIndependent=true`
write into it via the existing `setIndyTargetBuffer` setter on
`SamplerVoice`. This entire scaffold already exists at `6fa483c` — we
keep it as-is.

### New: one private indy FX chain instance

Add `tw::IndyFxChain indyChain` to the processor. The chain bundles its
own copies of the global FX modules:

- `GrainEngine` L/R
- `TapeProcessor` L/R
- `SpaceReverb`
- `MoogDelay`
- `TerrainChorus`
- `ParametricEQ` L/R

Plus per-block `ParamTargets` (snapshot of the same APVTS values the
global chain consumes) and a current `uint8_t mask`. Allocated once in
`prepareToPlay`.

### Per-block flow

1. Clear `indyCaptureBus` at the top of `processBlock` (unchanged).
2. Layers render into per-layer scratch buffers. Indy voices write to
   `indyCaptureBus` instead of their layer scratch (unchanged routing —
   the existing `setIndyTargetBuffer` + `indyTargetBuffer` path).
3. Layers sum into master with vol/pan/mute/solo (unchanged).
4. **NEW:** scan all voices, OR their indy masks into `activeIndyMask`
   (see §2).
5. **NEW:** clear `indySumBuffer`; call
   `indyChain.setMask(activeIndyMask)` →
   `indyChain.setParamTargets(snapshotFxParamTargets())` →
   `indyChain.processInto(indyCaptureBus, indySumBuffer, numSamples)`.
   This fills `indySumBuffer` with the post-chain indy signal.
6. Global chain runs per-sample on master (unchanged FX cascade).
   **Inside the same per-sample loop**, immediately after the existing
   `leftChannel[i] = outL;` / `rightChannel[i] = outR;` lines and
   BEFORE the soft-clipper (the exact spot the old indy add-back
   lived):
   ```cpp
   leftChannel[i]  += indySumBuffer.getSample (0, i) * outputGain;
   rightChannel[i] += indySumBuffer.getSample (1, i) * outputGain;
   ```
   Master volume + DAC soft-clipper apply to indy output exactly as
   they did pre-our-work.
7. **REMOVE** the existing post-loop indy add-back (the `leftChannel[i]
   += indyL[i] * outputGain` block that previously read from
   `indyCaptureBus` directly). The new mix-in from `indySumBuffer`
   replaces it. `indyCaptureBus` is consumed by the indy chain in step
   5 and is NOT read directly inside the per-sample loop anymore.

### What stays the same as baseline

- `indyCaptureBus` singleton + its allocation / clear / `setIndyTargetBuffer`
  wiring.
- Voices' `setIndyTargetBuffer` path is unchanged. The voice still asks
  "am I independent? if so, write to indyTargetBuffer; if not, write to
  passedBuffer." We don't touch this.
- WET stem capture still works (master ring captures the final
  post-everything signal, including indy sum).

### What's new

- `tw::IndyFxChain` struct (one instance on processor).
- `tw::fxmask::packMask(...)` and `tw::fxmask::isEmpty(...)` pure
  helpers (re-introduced from the reverted spec; trivial header-only
  functions).
- `snapshotFxParamTargets()` helper on the processor (same as in the
  reverted spec, just feeds one chain instead of four).
- `juce::AudioBuffer<float> indySumBuffer` on the processor — scratch
  the chain writes into; mixed into master inside the per-sample loop.
- Per-block mask aggregation pass (see §2).

## §2 — Active mask resolution (per-block union)

At the top of `processBlock`, after layers have rendered but before the
indy chain is invoked, scan all voices across all 4 layers and OR their
indy masks together:

```cpp
uint8_t activeIndyMask = 0;
for (auto& layer : layers)
{
    auto& synth = layer.synth;
    for (int v = 0; v < synth.getNumVoices(); ++v)
    {
        if (auto* sv = dynamic_cast<SamplerVoice*> (synth.getVoice (v)))
            if (sv->isPlaying() && sv->isFxIndependent())
                activeIndyMask |= sv->packFxMask();
    }
}
```

`activeIndyMask` is the OR of every currently-playing indy voice's mask.

### Semantics

- One indy chop with `GRAIN` lit → mask = 0b0000001 → chain runs grain
  only on the indy bus.
- Two overlapping indy chops with `GRAIN` and `SPACE` → mask = 0b0001001
  → chain runs grain + space on the indy bus. Both chops hear both FX
  (chop A's grain output passes through space; chop B's space tail
  inherits grain processing of whatever's in the bus).
- All indy voices released → mask = 0 → chain idles (per-sample loop
  becomes pure pass-through; `outL[i] += L[i]` only).
- Mask changes mid-block-to-block as voices come and go; FX modules
  smooth across the change because their state is persistent and their
  bypass flags are evaluated per block.

### Bleed (the explicit tradeoff)

Two overlapping indy chops with disjoint masks share one chain. Each
gets the FX it asked for AND every other FX any concurrent indy chop
wanted. There is no isolation. The user explicitly accepted this in
exchange for the architectural simplicity.

The invariant the union rule preserves: **a chop never has fewer FX
applied than its chips request.** A chop may have additional FX from
overlapping chops; it will never be missing one.

### Voice-side additions (small)

`SamplerVoice` gains two tiny accessors (read `activeConfig`):

```cpp
bool    isFxIndependent() const noexcept { return activeConfig.fxIndependent; }
uint8_t packFxMask()      const noexcept
{
    return tw::fxmask::packMask (
        activeConfig.fxGrain,
        activeConfig.fxTapeMachine,
        activeConfig.fxSpace,
        activeConfig.fxDelay,
        activeConfig.fxEq,
        activeConfig.fxJune);
}
```

No `currentBucketIndex`, no `bucketBuses`, no `bucketResolver`, no
`bucketActivityNotifier`, no pool tracking. The voice exposes its mask;
the processor reads it.

`isPlaying()` is already a JUCE `SynthesiserVoice` method
(`getCurrentlyPlayingNote() >= 0`).

### Cost

128 voices max (4 layers × 32). Per-block dynamic_cast + 3 field reads
per voice. Trivial.

## §3 — Per-module behavior + scope boundaries

### Chip semantics

- **GRAIN** (bit 0), **SPACE** (bit 3), **DELAY** (bit 4), **EQ** (bit
  5), **JUNE** (bit 6) — pure bool. Mask bit set → `process()` is
  called for that module. Mask bit clear → module is skipped; signal
  passes through unchanged to the next module in the chain.
- **TAPE** (bits 1-2) — 4-state. `(mask >> 1) & 0b11 == 0` → bypassed.
  Otherwise → `tapeProcessor.setMachine(machine - 1)` (mask encoding 1
  / 2 / 3 = Studio / Cassette / Wire; the underlying machine index in
  `TapeProcessor::setMachine` is 0 / 1 / 2). Also call
  `setWireModes(spaceNoise, tubeSat)` if tape is on, mirroring the
  global chain.

### Chain order

Match the global chain exactly:

```
indy bus → [grain?] → [delay?] → [space?] → [tape?] → [june?] → [eq?] → indySumBuffer
```

Verified against `PluginProcessor.cpp` lines 1656 (grain), 1737 (delay),
1743 (space), 1754/1768 (tape), 1795 (chorus/june), 1874 (EQ).

### Shared APVTS params

`snapshotFxParamTargets()` reads every modulated FX param from APVTS at
the top of each block into a `ParamTargets` struct. The chain consumes
that snapshot. Turning a knob on the global FX panel updates both the
global chain (per-sample, via its existing SmoothedValue advance) AND
the indy chain (per-block, via the next snapshot). Chips are pure
bypass gates; knobs stay on the global FX panels.

### Not routed per-chop (by design — no chips for them in the UI)

- **Tape Loop** (transport, not an FX — stays on the global path
  entirely; indy bus bypasses it).
- **Tape LINK** (the "all three machines in series" toggle — only the
  global chain runs in linked mode; the indy chain uses single-machine
  mode per the chop's `fxTapeMachine`).
- **Output gain / master mix** (global only).

### WET stem capture

`masterFxBuffer` already captures the final post-everything master
(global chain + indy sum + soft-clipper). The existing energy-ratio
attribution in `exportStemToFile` continues to work without change —
indy output reaches `masterFxBuffer` because the indy mix-in happens
inside the per-sample loop before the master ring write.

## Touch points

**New files:**
- `plugins/TerrainInstrument/Source/FxMask.h` — tiny pure header with
  `tw::fxmask::packMask` and `tw::fxmask::isEmpty`. ~30 lines.
- `plugins/TerrainInstrument/Source/IndyFxChain.h` — wraps the 7 FX
  module instances + `ParamTargets` + `processInto`. ~200 lines.
- (Test files for both, JUCE UnitTest pattern.)

**Modified files:**
- `plugins/TerrainInstrument/Source/PluginProcessor.h` — add
  `tw::IndyFxChain indyChain`, `juce::AudioBuffer<float> indySumBuffer`,
  `tw::IndyFxChain::ParamTargets snapshotFxParamTargets() const noexcept`.
- `plugins/TerrainInstrument/Source/PluginProcessor.cpp` —
  `prepareToPlay` calls `indyChain.prepare(sr, blockSize)` + allocates
  `indySumBuffer`. `processBlock` does the mask aggregation pass,
  calls `indyChain.processInto(indyCaptureBus, indySumBuffer,
  numSamples)` BEFORE the per-sample master loop, and replaces the
  existing in-loop indy add-back (`leftChannel[i] += indyL[i] *
  outputGain` that reads from `indyCaptureBus` directly) with the
  equivalent read from `indySumBuffer` (post-chain). The
  `indyCaptureBus` read-pointer setup (`const float* indyL =
  indyCaptureBus.getReadPointer(0);` etc.) goes away — only the indy
  chain reads `indyCaptureBus` now.
- `plugins/TerrainInstrument/Source/SamplerVoice.h` — add
  `isFxIndependent()` + `packFxMask()` const accessors. (The existing
  `setIndyTargetBuffer` / `indyTargetBuffer` path is preserved
  unchanged.)
- `plugins/TerrainInstrument/CMakeLists.txt` — add the two new test
  files to `target_sources`.

**No JS / WebView / Slice schema changes.** No preset migration. The
existing `indyCaptureBus` allocation + `setIndyTargetBuffer` calls in
`prepareToPlay` are kept exactly as they are at baseline `6fa483c`.

## Out of scope (deferred features)

- **Per-chop FX parameter overrides** (separate reverb size per chop,
  etc.) — Slice schema expansion + per-chop FX sliders in the UI.
  v1 keeps shared global params only.
- **Per-chop isolation** (no bleed between overlapping different-mask
  chops). That's what option 3 / the bucket-pool approach offered;
  explicitly traded away here.
- **TAPE LINK per chop** — only the global chain supports linked mode.
- **Per-FX dry/wet mix per chop** — chips are on/off only.
- **Tape Loop / Output gain per chop** — global only.

## Success criteria

1. INDEPENDENT chop with all chips OFF → indy chain runs as pure
   pass-through (mask = 0). Chop is **audibly dry** through master gain
   + soft-clipper, identical to baseline `6fa483c` INDEPENDENT behavior.
2. INDEPENDENT chop with all chips ON (TAPE = Studio) → indy chain
   processes through all 6 FX modules. Should sound like the global
   chain (shared params, separate state). A/B-compare by flipping
   INDEPENDENT off.
3. INDEPENDENT chop with only GRAIN on → chop is granulated, no
   reverb / delay / tape / eq / june audible on it.
4. INDEPENDENT chop with only SPACE on → chop is dry-with-reverb, no
   other FX.
5. Two simultaneous INDEPENDENT chops with different masks (e.g., chop
   A = GRAIN only, chop B = SPACE only) → **both chops audibly receive
   GRAIN AND SPACE**. This is the explicit bleed tradeoff. The
   alternative (option 3 isolation) was rejected.
6. Tweaking global SPACE panel updates the indy chain's reverb in real
   time. Shared params confirmed.
7. Stems WET export remains coherent.
8. No clicks on note-on or note-off when INDEPENDENT.
9. Master volume knob still affects INDEPENDENT chops (verified by the
   indy mix-in placement inside the per-sample loop, before soft-clip).
