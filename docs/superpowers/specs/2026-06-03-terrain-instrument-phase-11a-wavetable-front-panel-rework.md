# Phase 11a — Wavetable Engine Front-Panel Rework (Foundation)

> **Status:** SPEC drafted 2026-06-03 after Serum 2 + Vital + Pigments deep-dive research and v14 mockup approval.
> **Parent phase chain:** Phase 8b (unison-in-voice) + Phase 10a (frequency-domain wavetables) + Phase 8b polish-3 (voice cap) all shipped → this is the next major phase.
> **Mockup:** `plugins/TerrainInstrument/Design/v14-wt-engine-mockup.html` (Variant A approved by user)
> **References used in design:** `docs/research/vital-deep-dive-2026-06-02.md` (Phase 10 era) + new in-session Serum 2 + Vital + Pigments subagent reports (will be archived to `docs/research/2026-06-03-serum2-vital-pigments-wavetable-research.md` after spec ships)
> **Branch:** `feature/terrain-instrument` · **Starting HEAD:** `64acce8`

## TL;DR

Reorganize OSC A and OSC B's wavetable engine front panel from "5 tuning knobs" to **5 wavetable-engine-specific knobs**, with the tuning knobs + 4 type selectors moving to a back panel toggled via the `+` button. **Foundation only** — three new transformation axes (SPECTRAL, FOLD, FRAME SPREAD) get their UI + param representation in 11a, but only FRAME SPREAD does any audible DSP this phase. SPECTRAL and FOLD become placeholder params with the wiring in place; their actual DSP arrives in Phase 11c (SPECTRAL) and Phase 11d (FOLD) after per-mode research rounds.

## Goal

Establish the architectural foundation for a Vital + Serum 2 + Pigments synthesis of wavetable controls on Terrain. After Phase 11a, the UI looks like the v14 mockup, the params exist in APVTS, presets save/load the new state, and FRAME SPREAD audibly works. Modes (WARP, SPECTRAL, FOLD types/shapes) accumulate in later sub-phases.

## Success criteria

1. **UI** — Open the SYN page. OSC A + OSC B each show 5 prominent knobs labeled WT POS / WARP / SPECTRAL / FOLD / SPREAD. Press `+` on either; the visualizer area swaps to a 4-pill selector row (WARP / SPECTRAL / FOLD / INTERP) with edge-to-edge alignment to the engine pills above; the knob row swaps to OCT / SEM / CENT / PAN / LEVEL.

2. **Front knobs do something** — WT POS scrubs the frame (same as legacy FRAME), WARP applies the current warp mode (same as legacy SYN_OSC_A_WARP_AMOUNT), **SPREAD distributes each unison sine across the wavetable position by SPREAD × 1.0 frames** (audibly thickens timbre at UNISON > 1), SPECTRAL and FOLD turn but don't audibly do anything yet (placeholder; non-bypass-style "no change" state).

3. **Back selectors do something** — WARP type changes between BEND / SYNC / FORMANT / NONE (the existing 4). SPECTRAL type, FOLD shape, INTERP mode are placeholder selectors with at least one option each.

4. **Preset compatibility** — V1 presets (current saved Phase 8b/10a presets) load without crash. Old SYN_OSC_A_WT_FRAME param still exists under that name; the UI just labels it "WT POS." New params (SPECTRAL_TYPE/AMT, FOLD_SHAPE/AMT, FRAME_SPREAD, INTERP_MODE) default to zero/None so preset audio is identical to pre-11a.

5. **Build green, install green, DAW load green.**

6. **UI alignment rule honored** — selector pills line up edge-to-edge with engine pills. Tuning row underneath aligns identically to the front knob row above. Every label sits flush per `feedback-ui-spacing-alignment-hard-rule`. cmd+Q DAW between rebuilds.

---

## Architecture decisions

### Decision 1: Five front knobs, three of them are NEW transformation amounts

| Knob | Backing param (existing or new) | DSP this phase | DSP next phase |
|---|---|---|---|
| **WT POS** | `SYN_OSC_A_WT_FRAME` (existing — label change only) | Frame scrub (already wired) | — |
| **WARP** | `SYN_OSC_A_WARP_AMOUNT` (existing) | Applies current SYN_OSC_A_WARP_MODE (BEND/SYNC/FORMANT) | New modes added in 11b |
| **SPECTRAL** | `SYN_OSC_A_SPECTRAL_AMT` (NEW, float 0..1) | placeholder — no audible effect | Audible in 11c (per-mode DSP) |
| **FOLD** | `SYN_OSC_A_FOLD_AMT` (NEW, float 0..1) | placeholder — no audible effect | Audible in 11d (per-shape DSP) |
| **SPREAD** | `SYN_OSC_A_FRAME_SPREAD` (NEW, float 0..1) | **real DSP** — per-sine `uFramePos_[u]` distributes across the wavetable | refinements in 11c/d if needed |

Plus B mirrors of all 5 params.

### Decision 2: Back panel has 4 type selectors + 5 tuning knobs

Back panel layout (after pressing `+`):

```
┌─ Selector pills row (replaces visualizer) ──────────────┐
│  [ WARP / Bend +  ] [ SPECTRAL / None ] [ FOLD / Linear ] [ INTERP / Linear ]
└──────────────────────────────────────────────────────────┘
┌─ Knob row (replaces front 5) ───────────────────────────┐
│   OCT      SEM     CENT      PAN     LEVEL
└──────────────────────────────────────────────────────────┘
```

Selector params (all NEW except WARP MODE which already exists):

| Selector | Backing param | Choices in Phase 11a | Notes |
|---|---|---|---|
| **WARP TYPE** | `SYN_OSC_A_WARP_MODE` (existing) | NONE / BEND / SYNC / FORMANT (existing 4) | More added in 11b |
| **SPECTRAL TYPE** | `SYN_OSC_A_SPECTRAL_TYPE` (NEW int) | NONE only (1 option) | More added in 11c |
| **FOLD SHAPE** | `SYN_OSC_A_FOLD_SHAPE` (NEW int) | LINEAR only (1 option) | More added in 11d |
| **INTERP MODE** | `SYN_OSC_A_INTERP_MODE` (NEW int) | LINEAR only (1 option) | SPECTRAL added with 11c |

Plus B mirrors.

### Decision 3: PHASE init + RAND amount via right-click on WT POS

To stay at 5 front knobs without sacrificing PHASE, expose it as a right-click context menu on the WT POS knob. Two values: `SYN_OSC_A_PHASE_INIT` (NEW float 0..1) and `SYN_OSC_A_PHASE_RAND` (NEW float 0..1, default 1.0 to preserve Phase 8b T3's hash-based randomization). Phase 11a wires the params + the right-click handler but the menu UI can be deferred to 11b polish if it crowds the timeline.

### Decision 4: SPREAD is per-OSC, not global

Each oscillator has its own `SYN_OSC_X_FRAME_SPREAD` so OSC A can sit tight while OSC B fans wide. Uses Phase 8b's `uFramePos_[u]` array (NEW — added in this phase parallel to existing `uPhaseA_[u]`).

### Decision 5: Rename FRAME → WT POS in UI, NOT in param IDs

The param `SYN_OSC_A_WT_FRAME` stays — V1 presets load. Only the UI label changes. DAW automation still finds it under its original APVTS path.

### Decision 6: Each SPECTRAL/FOLD mode added in 11c/d gets its own research-before-implement round

Per `feedback-defer-to-manuals-and-research.md` implementation-phase corollary, every mode (Vocode, Smear, Sine Fold, etc.) gets a subagent research dispatch BEFORE implementation. We don't ship "Vocode" until we've read Vital's `spectral_morph.h` Vocode block, the Serum 2 manual sections on spectral interpolation, and any relevant Google sources on the underlying math. Phase 11c isn't 1 phase — it's a series of `11c-vocode`, `11c-harmonic-stretch`, etc. mini-phases each with their own research round.

---

## Files modified in Phase 11a

| File | Change |
|---|---|
| `plugins/TerrainInstrument/Source/ParameterIDs.hpp` | + 12 new param ID constants (6 per OSC × 2 OSCs): SPECTRAL_TYPE/AMT, FOLD_SHAPE/AMT, FRAME_SPREAD, INTERP_MODE. Plus PHASE_INIT/RAND |
| `plugins/TerrainInstrument/Source/PluginProcessor.cpp` | `createParameterLayout` — add the new APVTS entries. Broadcast block — push new values per-block into voices (SPREAD only — others are read directly by render path in later phases) |
| `plugins/TerrainInstrument/Source/SynthVoice.h` | Add `std::array<float, kMaxUnison> uFramePos_` per OSC (A + B). `setFrameSpread(float)` setter. `startNote` populates `uFramePos_[u]` based on frame position + spread. Render-path WT engine reads `uFramePos_[u]` instead of voice-global `framePos_` (or with offset from it). |
| `plugins/TerrainInstrument/Source/PluginEditor.{h,cpp}` | Add 6 new WebSliderRelays per OSC (12 total) for the new params. `withOptionsFrom` + `WebSliderParameterAttachment` per Phase 8b pattern. |
| `plugins/TerrainInstrument/Source/ui/public/index.html` | Replace OSC A + OSC B knob row content: 5 new front knobs. Add back-view HTML: selector pills row + tuning knob row. Toggle logic in `+` handler. Edge-to-edge alignment per v14 mockup. |
| `docs/specs/v1-syn-spec.md` | Update phase plan: Phase 11a ships, 11b/c/d/e queued. |

**No** changes to Wavetable.h, WavetableBank.h, PluginProcessor.h (UnisonSynth class), or test files in this phase.

---

## Per-section design (knob-by-knob)

### Section A — WT POS (rename of FRAME)

**What changes:** The knob currently labeled FRAME on the back panel moves to the front. Label changes to "WT POS". Underlying APVTS param is unchanged (`SYN_OSC_A_WT_FRAME`, float 0..1). The C++ render-path already reads this — no DSP change.

**Right-click menu (deferred to 11b polish if timeline tight):** PHASE INIT (0..1, default 0) and PHASE RAND amount (0..1, default 1.0 — keeps Phase 8b T3 hash randomization on by default; user can dial back to 0 for "all sines start phase=0" laser-zap behavior).

### Section B — WARP (front amount knob; back type selector)

**What changes:** The knob currently labeled WARP AMT on the back panel moves to the front. Label becomes just "WARP". Underlying APVTS param is unchanged (`SYN_OSC_A_WARP_AMOUNT`, float 0..1).

**Back selector** (`SYN_OSC_A_WARP_MODE`, int): existing values NONE / BEND / SYNC / FORMANT. Selector pill shows current value (e.g., "Bend +"). Clicking opens dropdown. Phase 11b expands this menu dramatically (Pigments Phase Transform shapes + Serum FM/RM/PD sources).

### Section C — SPECTRAL (NEW, placeholder)

**Param adds:**
- `SYN_OSC_A_SPECTRAL_TYPE` — int choice, single option "NONE" in 11a
- `SYN_OSC_A_SPECTRAL_AMT` — float 0..1, default 0

**DSP in 11a:** None. The render path doesn't read these. Knob turns but does nothing audibly.

**Why ship the param now:** Lets us reserve the param ID, get preset compat in place from day one, and avoids breaking preset compatibility when 11c arrives. Same pattern as Phase 8a shipping SYN_VOICES as "display only" before Phase 8b polish-3 enforced it.

### Section D — FOLD (NEW, placeholder)

Same shape as SPECTRAL:
- `SYN_OSC_A_FOLD_SHAPE` — int choice, single option "LINEAR" in 11a
- `SYN_OSC_A_FOLD_AMT` — float 0..1, default 0
- DSP in 11a: None.
- DSP in 11d: 3 fold shapes (LINEAR / SINE / TRIANGLE per Pigments) implemented after per-shape research.

### Section E — SPREAD (NEW, REAL DSP this phase)

**Param add:** `SYN_OSC_A_FRAME_SPREAD` — float 0..1, default 0.

**DSP:** Per-sine frame offset using Phase 8b's per-sine arrays. New per-voice arrays:

```cpp
std::array<float, kMaxUnison> uFramePosA_{};
std::array<float, kMaxUnison> uFramePosB_{};
```

Populated in `setFrameSpread(float spread)`:

```cpp
void setFrameSpread (float spreadA01, float spreadB01) noexcept
{
    // Distribute the active sines across the wavetable position.
    // Sine u in [0, activeUnison_) gets offset u_norm × spread × 1.0 around centerFrame.
    // Wraps to [0, 1] if it goes off either end.
    for (int u = 0; u < activeUnison_; ++u)
    {
        if (activeUnison_ <= 1)
        {
            uFramePosA_[(size_t) u] = 0.0f;
            uFramePosB_[(size_t) u] = 0.0f;
            continue;
        }
        const float u_norm = ((float) u / (float) (activeUnison_ - 1)) * 2.0f - 1.0f;
        uFramePosA_[(size_t) u] = u_norm * spreadA01 * 1.0f;  // max ±1.0 of frame range
        uFramePosB_[(size_t) u] = u_norm * spreadB01 * 1.0f;
    }
    for (int u = activeUnison_; u < kMaxUnison; ++u)
    {
        uFramePosA_[(size_t) u] = 0.0f;
        uFramePosB_[(size_t) u] = 0.0f;
    }
}
```

In the render loop, each sine reads its OWN frame position as `framePos_ + uFramePosA_[u]` (clamped to [0,1]). The voice-global `framePos_` is the centre value from `SYN_OSC_A_WT_FRAME` (the WT POS knob).

**Sonic result:** At SPREAD=0, all unison sines read from the same frame — identical to pre-11a behavior. At SPREAD=1 with UNISON=8, the 8 sines read from 8 different positions spread across ±1.0 of the wavetable position. **Each sine plays a different waveform shape on top of detuning.** Vital's `frame_spread` idea, now ours.

### Section F — INTERP MODE (NEW, placeholder selector)

**Param:** `SYN_OSC_A_INTERP_MODE` — int choice, single option "LINEAR" in 11a.
**Future:** "SPECTRAL" mode (Phase 11c+) re-synthesizes between-frame positions using FrameSpec harmonic blending instead of time-domain linear crossfade. Per Serum 2's wavetable-editor interpolation type, exposed as runtime here.

---

## What Phase 11a explicitly DOES NOT include

- **New WARP modes** — the menu in 11a still has NONE/BEND/SYNC/FORMANT. Expansion = Phase 11b after research on Pigments Phase Transform + Serum 2 alt warp.
- **Audible SPECTRAL DSP** — Phase 11c, after research per mode.
- **Audible FOLD DSP** — Phase 11d, after research per shape.
- **SPECTRAL INTERP mode DSP** — Phase 11c (couples with SPECTRAL MORPH).
- **Per-engine front panels for FM/NOISE/SAMP/GRAN/SPEC** — Phase 11e.
- **Cross-parameter blend feature** ("right-click any knob to blend with another") — Phase 11f or later. Future Terrain uniqueness, not foundation work.
- **Visual upgrades** — the wave display stays a static SVG in 11a. Real-time wavetable visualization (Serum 2 / Vital style 3D viz) = Phase 11g or later.

---

## Risks + mitigations

| Risk | Likelihood | Impact | Mitigation |
|---|---|---|---|
| New params break V1 preset compat | Low | Medium | Defaults all = 0/NONE. APVTS still loads V1 silently with the new params at defaults. Verify with V1 preset load test. |
| SPREAD audibly clicks at high values | Medium | Medium | Per-sine frame position uses Phase 10a's bilinear interpolation in `lookup(int mipLevel, float framePos, float phase)`. Already smooth. Mip-level still picked from `uPhaseIncA_[0]` so detune doesn't interact with spread weirdly. |
| Front-back swap toggle breaks DAW automation on the new params | Low | Low | APVTS handles persistence regardless of which knob is currently visible. Verified by Phase 4's existing + button swap. |
| Spacing/alignment regression at certain knob/selector counts | Medium | Medium | User has hard rule (UI spacing). Verify each step in DAW (cmd+Q) per the lockedrule. v14 mockup is the visual contract. |
| Right-click context menu for PHASE init/RAND is harder than expected | Medium | Low | Deferable to 11b polish — keep params functional via APVTS-only access for Phase 11a. |
| `uFramePosA_[u]` initialization in setFrameSpread races with render thread | Low | High | setFrameSpread is called from per-block broadcast (audio thread). No lock needed. Same pattern as setUnison from Phase 8b T2. Verify with juce::ScopedLock if any drift detected. |

---

## Future phases enabled by this foundation

| Phase | What it adds | Research scope |
|---|---|---|
| **11b** | Expanded WARP menu (Pigments Phase Transform shapes + Serum FM/RM/PD sources). PHASE init/RAND right-click menu (if deferred from 11a). | Pigments manual pp.96-97 (Phase Transform 7 shapes), Serum 2 manual WARP sections, source code for any new modes. |
| **11c** | SPECTRAL MORPH DSP. One mode at a time: Vocode, Harmonic Stretch, Inharmonic, Smear, Phase Disperse, LP, HP. Each gets its own research round + implementation. | Vital `spectral_morph.h` per mode, Serum 2 spectral interpolation, Google for DSP math. |
| **11d** | WAVEFOLDING DSP. 3 shapes (LINEAR/SINE/TRIANGLE per Pigments). | Pigments manual pp.98-99, Buchla-style and Pigments-style folder DSP research. |
| **11e** | Per-engine front panels for FM/NOISE/SAMP/GRAN/SPEC. Each engine type gets its own 5 engine-specific knobs. | Per-engine research per Serum 2 + Pigments multi-engine sections. |
| **11f** | Knob-blends-with-knob feature ("right-click → Blend with…"). | Internal architectural design — no external manual research. |
| **11g** | Real-time wavetable visualization (Serum 2 / Vital style 3D viz). | UI/visualization research; canvas-based JS rendering. |

---

## Ready for plan-writing

After user review of this spec, invoke `superpowers:writing-plans` to break Phase 11a into bite-sized implementer tasks. Estimated plan size: ~12-15 tasks (param adds, voice state, UI rework, back-view toggle, per-section testing, build verification, tag).
