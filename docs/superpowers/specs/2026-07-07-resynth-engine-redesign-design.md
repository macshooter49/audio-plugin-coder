# Resynth — Engine Redesign (Geode → "Resynth", sample-only) — Design Spec

**Date:** 2026-07-07 · **Branch:** `feature/terrain-instrument` · **Engine slot:** `Engine::SPEC = 3`
**Status:** DESIGN APPROVED (Max, 2026-07-07 — name/knobs/scope/rules signed off via decision questions). Build bible.
**One-liner:** Rename Geode → **Resynth**. A **sample-only** spectral resynthesis oscillator: drop a one-shot / noise / any audio → it re-synthesizes the sample and lets you **degrade, shape, filter, and distort** it into lo-fi "lossy data" textures and synth waveforms — **never detuning** the sample. Serum-2-spectral energy + Chase Bliss *Lost + Found* "Spectral Modulator / Gen Lite" character.

> Supersedes the additive-fidelity direction of `2026-07-05-geode-resynthesis-engine-design.md`. Max A/B'd geo6 and chose to **lean into the lossy/spectral character** (SIEVE = "Goodhertz Losser / old-computer-data") rather than rebuild a faithful phase-vocoder core. This spec is the pivot.

---

## 0. Why (Max's verdict, verbatim intent)

geo6 (play-through + Bedrock) shipped; Max tried it on one-shots: *"It actually does sound cool now. It sounds like a data remover… low quality data being removed but it still sounds good. This is the kind of stuff I was looking for… it's more spectral… data resynthesis."* The hero is the **SIEVE** "lossy data compression" sound. Decision: **keep and grow that**, fix/replace the knobs that don't earn their place, and reframe the whole engine around **samples**.

Reference: **Chase Bliss Lost + Found** manual (read 2026-07-07). Relevant modes:
- **3B Spectral Modulator** — *"a synthesized replica of your dry signal… crystal waterfalls, quivering digital distortion, a sparkling substitute for a low-pass filter."* RESONANCE = *"soft clipping and emphasis to individual frequency bands → jagged, focused character."* → our **DRIVE**.
- **6B Gen Lite** (Generation Loss) — degraded-tape lo-fi → our **CRUSH**.
- Design philosophy: every effect = **3 knobs + 1 hold function.** Dead simple; every knob does something real.

---

## 1. Naming & rules (LOCKED)

- **Display name:** **Resynth** (chosen over "Resynthesizer"/"ReSynth" to fit the A/B/C/D engine tab). Applies to: engine tab label, engine-select menu, tooltips, right-click header, waveform panel title.
- **Internal identifiers UNCHANGED** — `Engine::SPEC`, `GeodeEngine`, `geodeEngA_…`, and **all `SYN_OSC_x_GEODE_*` param-ID strings stay** so saved presets/DAW sessions do not break. Only user-facing *strings* change. (APVTS param IDs are persisted state — renaming them = silent preset breakage.)
- **Name-reuse rule REVERSED for this engine** (Max, 2026-07-07): the "new control names only" hard rule ([[feedback-terrain-new-button-names-only]]) does **not** apply here — Resynth **reuses** normal names (Scan, Stretch, Cut, Shape, Drive, Crush, Tilt, Start). Simplicity over novelty.
- **🔒 HARD DSP RULE — NOTHING DETUNES.** Every control must preserve the pitch of the dropped sample. Only the MIDI note and the existing right-click ±semitone control may change pitch. Any stage that touches `wr_.ratio[]` in a pitch-shifting way is a bug. Formant, Shape, Drive, Crush, Sieve, Cut, Tilt all operate on **amplitude/spectrum only**.

---

## 2. Scope change: SAMPLE-ONLY

- **Remove the wavetable/analog source door (Door B).** Resynth accepts a **dropped sample only** ("why resynthesize a synth?"). Serum-2-spectral model.
- No-sample state: show the **drop zone** (like the Sample/Granular engine's empty state) instead of the current default "harmonic saw" store. (Confirm behavior in impl: either silent-until-dropped + drop prompt, or a tiny neutral default — prefer drop prompt for clarity.)
- Keep the off-thread analyzer → `GeodeFrameStore` handoff exactly as-is (real-time-safe double-buffer). Only the *source* narrows to samples.

---

## 3. Knob map (LOCKED — 6 + 5, aligned, no shifted rows)

**Page 1 — Play & Character (thin-white row):**

| Knob | From | Behavior | Menu / notes |
|---|---|---|---|
| **SCAN** | Creep | Play/scan rate. 0 = hold, center = natural speed, up = faster. Keeps the play-through read-head advance. | — |
| **STRETCH** | Fossil | Time-stretch / slow / freeze-to-pad. The smeared, slowed, data-artifact character. | (stretch feel; freeze at max) |
| **SIEVE** | *keep* | **The hero.** Lossy "octave-dropping low-quality data" degradation. **Do not alter its qualities.** | — |
| **CUT** | Cut | Spectral filter — **amplified/stronger** sweep (audible by ~20%). | **Right-click glass menu: LP / HP** (label shows LP or HP). Removes lows or highs from the data. |
| **SHAPE** | *new* (Haze slot) | Morph the resynthesized partial **amplitude profile** toward a target waveform. 0 = the sample's own spectrum; up = it becomes that synth waveform *built from* the sample. | **Right-click glass menu: Sine / Square / Saw.** |
| **DRIVE** | *new* (Fracture slot) | Spectral distortion — soft-clip + per-band emphasis (Lost+Found Spectral Modulator). Jagged, "quivering digital" synth grit. | **No detune.** |

**Page 2 — Sculpt & Fidelity (thin-white row):**

| Knob | From | Behavior | Notes |
|---|---|---|---|
| **QUALITY** | *keep* | Partial budget / fidelity. Low = thin/lossy, high = full. Pairs with SIEVE across the degradation range. | 16→96 partials |
| **FORMANT** | *fix* | **True pitch-preserving** formant shift (moves the spectral envelope over a fixed ratio grid; **never** shifts partial frequencies). Tamed so it is not harsh. | + **FKEEP** toggle (preserve on/off) |
| **TILT** | *keep* | Spectral tilt, dark ↔ bright. | bipolar |
| **CRUSH** | *new* | Bitcrush (sample-rate + bit reduction) + generation-loss degrade (Gen Lite). Deepens "old data" lo-fi. Stacks with SIEVE. | post-synth on L/R |
| **START** | Position | Sample **start point**, drawn on the waveform (no separate slider). | on-waveform handle |

**Removed entirely:** Geode name · wavetable source · **Bedrock** (tune via right-click ±semitones; also un-shoves the 7th-knob layout) · **Haze** (did nothing) · **Fracture** (detuned badly) · **Distill** (cut for a cleaner set) · the little **Position slider / position follower** UI.

> Layout note: 6 (page 1) + 5 (page 2). Max's "all equal" complaint was about Bedrock's 7th knob shoving row 2 — removing it restores clean alignment. Final row balance to be confirmed on the headless UI mockup (UI-spacing hard rule; may center the 5 or promote one knob).

---

## 4. Per-knob DSP (all amplitude-domain unless noted — NO pitch change)

- **SCAN** — existing play-through: `pos01_` advances at `creepRate` referenced to `store_->naturalSec` (GeodeEngine.h render read-head). Rename param display only.
- **STRETCH** — existing FOSSIL freeze becomes a **time-stretch**: at low values slow the read-head advance (frames interpolate → smeared/slowed); at max, freeze into an infinite pad. (Investigate a light hop-resynthesis stretch feel; the frozen-frame smear is already musical.)
- **SIEVE** — unchanged. (Spectral gate below a rising threshold — the lossy hero.)
- **CUT** — strengthen the spectral low-pass so it bites by ~20% of travel; add an **HP mode** (mirror: gate partials *below* a rising ratio). Mode chosen by right-click LP/HP; knob label reflects mode.
- **SHAPE** — for `amount a` and target harmonic weights `W(n)` (saw = 1/n all n; square = 1/n odd n only; sine = n==1 only), set `wr_.amp[j] = lerp(wr_.amp[j], wr_.amp[j] * W(nearestHarmonic(wr_.ratio[j])), a)` — i.e. re-weight partials toward the target series **without moving `wr_.ratio[j]`**. Harmonic index derived from partial ratio vs fundamental. (Exact derivation from `dsp-hooks` map.)
- **DRIVE** — per-band soft-clip + emphasis on `wr_.amp[]` pre-synth (recommended over post-synth waveshaping to stay spectral & pitch-safe; final placement from `dsp-hooks` map). Adds harmonics/grit, focuses bands.
- **CRUSH** — post-synth on the L/R block: sample-rate decimation + bit-depth quantize + optional gen-loss (noise + wow/flutter-lite). Cheap, stacks with SIEVE.
- **FORMANT (fix)** — **root cause & fix from `dsp-hooks` map.** Must re-map `wr_.amp[]` along a **fixed** ratio grid (envelope shift), not scale/shift `wr_.ratio[]`. Add gain tame + smoothing to kill harshness.

---

## 5. UI plan — the sample UI + live "data-removal" viz

Throw out the current Geode waterfall UI. Reuse the **Sample/Granular engine UI** (exact anchors from `sample-ui-reuse` + `current-resynth-ui` maps):
- **Drag-in sample zone.**
- **Real sample waveform** display (reuse the sample-engine waveform canvas + its C++→JS push).
- **Loop modes at the TOP** (One-Shot / Forward / Reverse / Ping-Pong), like the sample engine. (Wire to `GEODE_LOOP`.)
- **Phase** control if the sample UI carries one.
- **White MIDI followers** — one white playhead line per held voice, **polyphonic** (multiple notes → multiple lines), reused from the multi-sample-follower. Driven by each voice's `pos01_`.
- **Remove** the little position slider + single position follower.

**Live degradation visualizer (the big ask):** the waveform/spectral display must **show the data being taken away**, reacting to every knob in real time (4K/60). Reuse the always-alive **display engine** already built for the waterfall (`computeDisplayEnvelope` / `window.__geodeSpectrum`, EQ-push pattern, zero audio-thread cost): repoint it to render the **resynthesized+degraded** representation (SIEVE thinning, CUT filtering, SHAPE squaring, CRUSH blocking, DRIVE jaggedness) overlaid on / morphing the sample waveform. Moving a knob visibly removes/reshapes data. *"Whatever it sounds like, it looks like."* (Every-curve-must-move hard rule.)

---

## 6. Removed / obsolete

Bedrock param (keep the ID reserved/neutralized so preset load doesn't error — hide from UI, ignore in DSP), wavetable door + its analyzer branch, Haze/Fracture/Distill DSP + UI, position slider element, current Geode knob-row layout, waterfall-only viz framing.

---

## 7. Build / ship checklist (both formats, every time)

- [ ] index.html changed → **bust the WebUI BinaryData tree + cmake configure** before build (CLAUDE.md §2B), verify embed via `strings` on the compiled binary.
- [ ] New/changed params → **4-point WebSlider bind** each (relay + `.withOptionsFrom` + `WebSliderParameterAttachment` + JS read) or silent no-op.
- [ ] Build **VST3 AND AU**; verify both with `strings` + mtime; bump `TERRAIN_BUILD` (geo6 → **rs1**).
- [ ] pluginval clean, zero NaN; md5 VST3/AU parity where expected.
- [ ] Headless-render the UI mockup and READ the PNG before wiring; pass UI-spacing (align visible glyphs).
- [ ] Install to `~/Library/Audio/Plug-Ins/{VST3,Components}`; remind Max to **reload the DAW** (stale in-memory build trap).

## 8. Risks / open

- **SHAPE harmonic mapping** on inharmonic samples (partials not integer-ratio) — define `nearestHarmonic` sensibly so it degrades gracefully (never detunes).
- **FORMANT fix** must be verified by measurement (pitch unchanged across the sweep) — offline test like prior geo work.
- **CRUSH + SIEVE** stack could get too noisy/harsh — gain-stage and cap.
- **Viz cost** — keep the degradation render on the display-engine tick (message thread), never the audio thread; wd9 self-heal armor.
- **Loop-mode + START + SCAN + STRETCH** interaction — define precedence (START sets read-head origin; SCAN advances; STRETCH scales advance; loop mode wraps).
- Final page-1/page-2 balance + the no-sample default state — confirm on mockup with Max.

## 9. Decisions locked (2026-07-07)

Name = **Resynth** · new knobs = **SHAPE + DRIVE + CRUSH** · keep **TILT + START** · cut **DISTILL** (+ Haze/Fracture/Bedrock/wavetable/position-slider) · sample-only · **no detune** · reuse normal names.
