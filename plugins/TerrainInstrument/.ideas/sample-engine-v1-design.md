# Sample Engine v1 — Design Spec

**Project:** Terrain Instrument (Waves Crate)
**Scope:** Oscillator engine #2 of 5 (Wavetable · **Sample** · Granular · Spectral · FM)
**Date:** 2026-06-25
**Status:** Design locked (user-approved: front-5 knobs + chop-card scope). Mockup + implementation plan next.
**Branch baseline:** `feature/terrain-instrument` @ `a137201`
**References:** Serum 2 / Vital / Pigments research (`~/terrain-drop/compass_artifact_…markdown.md`); Opus mockup (`~/terrain-drop/terrain-samp-mockup-v2.html`); Lux Cache *Sample Studio* screenshots (slice/design/layer aesthetic).

---

## 1. Goal

Give each of the 4 oscillators a **Sample engine** — a "junior" version of the big-brother front sample/chop sampler, living inside the synth oscillator slot. It must:

- Load any WAV/FLAC by drag-and-drop, with an **interactive waveform** (start/end + loop brace, loop modes, crossfade, live playhead).
- Carry its own **performance identity** via 5 front knobs that are *not* a stripped Serum clone.
- Include a **junior chop engine** (the ⤢ extension card): chop / dupe / transient-detect / reverse / trim / normalize, with **key-playable slices**.
- Be a **full citizen of the world** — every control is a mod-matrix destination, and engine output flows through the same per-voice filter + FLOW chain as wavetable.

**Driving user need (verbatim):** *"We're basically making a junior version of our front sample chop panel… it's a whole new brother to the world… it has to be wired to the filters, the modes, the LFOs… It has to work with everything else."*

**Design north star:** Apple / Teenage-Engineering restraint — icons mixed with numbers ("140 BPM · grid 1/8"), Lux Cache's color discipline and use of space. Not busy. Not blocky.

## 2. Scope

### In scope (v1) — FRONT PANEL ONLY
1. **Engine-conditional front render.** When `SYN_OSC_x_ENGINE == 1` (Sample), the front view shows the Sample layout instead of the wavetable layout. (Today the front is *always* wavetable — this swap does not exist yet and is the core mechanism.)
2. **Interactive sample visualizer** — drag-and-drop load; outer Start/End brace; inner Loop brace (LS/LE) hard-clamped inside Start/End; live playhead; filename root-note auto-map (C3 default).
3. **Loop controls** — `LOOP ▾` mode dropdown, LS/LE numeric readouts, `SNAP ▾`, root/length readout, in a **floating bottom overlay on the waveform** (LFO `mv-ov` pattern), not a separate row.
4. **Front 5 knobs** — SCAN · STRETCH · FORMANT · SPRAY · XFADE (same 5 positions as wavetable; only labels + bound params change).
5. **Waveform right-click menu** — Reverse · Trim to selection · Normalize · Fade in/out · Set root from pitch · Reset.
6. **⤢ chop extension card** — junior chop engine (see §6), key-playable slices + region-carve editing.
7. **Mod + signal wiring** — all front params are mod destinations; output runs the existing filter/FLOW chain.

### Out of scope (v1)
- **Back panel** stays exactly the wavetable back (OCT/SEMI/CENT/LEVEL/PAN, blend pills). **UNISON/DETUNE live on the back**, not the front.
- Granular / Spectral engines (separate future engines; SCAN is designed to be reused by granular).
- Multisample / SFZ key-zone mapping (big-brother feature; not the junior engine).
- Serum-style dual serial WARP slots — does not earn front space here.

## 3. The visualizer (waveform) — LFO overlay pattern, "you're inside it"

**Layout principle (user-mandated):** copy the LFO module exactly — a **full-bleed interactive waveform** with controls in **floating transparent overlay rows on top of it**, never a separate control row that shrinks the waveform. Same `#mod-engine .mv-ov` pattern (top + bottom overlay rows, `text-shadow` for legibility, expand ⤢ floating top-right). Proper, symmetric spacing identical to the LFO. The waveform stays maximal so the user feels "already inside it."

Display itself: transparent, thin ~1.3px line, faint purple glow/fill, faint grid (matches LFO/filter). No block.

- **Top overlay row** — engine/preset label area + expand ⤢ (top-right), floating, never eating waveform height.
- **Bottom overlay row** — the loop controls (see §4), floating over the bottom edge of the waveform, LFO-strip style.
- **Outer Start/End handles** (top tabs) — the playable region of the file.
- **Inner Loop brace LS/LE** (bottom tabs, purple) — the loop region, **hard-clamped to stay inside Start/End** so playback can never read out of bounds (the "never gets out of order" requirement). LS/LE are modulatable for scrub/glitch.
- **Playhead** — a thin white line + dot riding the loop while a note sounds (LFO-follower aesthetic).
- **Direct manipulation ("inside it"):** drag handles to move them; **drag the loop band itself** to slide the whole loop; hover highlights; the waveform is the instrument surface, not a picture. Drag-and-drop a WAV/FLAC anywhere on it to load. Root note auto-detected from filename suffix (e.g. `…F2.wav` → F2; MIDI 69 = A3; default C3).
- **Caps everywhere:** every handle drag is bounds-checked; LS ≥ Start, LE ≤ End, LS < LE with a minimum gap.

## 4. The loop controls (floating bottom overlay — LFO-style, NOT a separate strip)

Lives as the bottom overlay row *on* the waveform (§3), spaced like the LFO's bottom row:

`LOOP ▾  ·  LS 18  ·  LE 62  ·  SNAP ▾  ·  Amin · 2.00s`

- **Loop modes** (`SAMPLE_LOOP_MODE`, extended): **One-Shot · Forward · Reverse · Ping-Pong · Tailed.** One-shot plays through ignoring loop points; Tailed plays a release tail on note-off rather than cutting.
- **SNAP:** Off / Zero-cross / Transient — where handles land when dragged (Serum's snap-loop detection).
- LS/LE readouts are draggable numerics (mirror the handles).
- All overlay text carries `text-shadow` for legibility over the waveform, exactly like the LFO controls.

## 5. Front 5 knobs (LOCKED)

Same 5 positions as wavetable (WT Pos / Warp / Spectral / Fold / Blur → relabeled). Each bipolar/curve detail confirmable in mockup. New params, all `SYN_OSC_x_SAMPLE_*`, all mod-matrix destinations.

| # | Knob | What it does | Right-click |
|---|------|--------------|-------------|
| 1 | **SCAN** | Playback **rate + direction**, bipolar (center = frozen, → forward, ← reverse, ends = tape-stop). Reused by granular later. | reverse · keytrack · tempo-fit |
| 2 | **STRETCH** | **Time-stretch, pitch held** (our Tones/Beats/Texture engines). The beat-Serum headline — stretch is a knob in plain Sample mode, not buried in Spectral. | algorithm: Tones / Beats / Texture |
| 3 | **FORMANT** | Formant shift ± independent of pitch. Vocal/character; pairs with STRETCH for full pitch/time/timbre decouple. | bipolar reset |
| 4 | **SPRAY** | Per-note **random start scatter** (fires on note-on via NoteOn-random source). Our differentiator — Serum's #1 requested gap is per-voice randomization. | range / mode |
| 5 | **XFADE** | Loop crossfade length + **equal-power curve**, auto-scaled in-bounds. | curve: equal-power / linear |

**Identity:** motion / time / timbre / per-note life / loop-quality. Distinct from wavetable, distinct from Serum.

## 6. The ⤢ chop extension card (junior chop engine)

Lux Cache / Apple-TE skin: icons + numbers, restrained color, generous space. Opens body-level (sibling of the LFO/ARP extension cards). Slice markers across a large waveform.

- **Toolbar:** `MODE ▾ · ✂ CHOP · ⧉ DUPE · ⇄ REVERSE · ⊺ TRIM · ⟂ XFADE <n%> · ⌖ SNAP ZERO`.
- **Slice modes:** **Auto** (transient-detect) · **Grid** (tempo, nudgeable — "140 BPM · grid 1/8") · **Manual** (alt-click to add).
- **Key-playable slices** (uses existing `SLICE_SUB_MODE`): **CHOP** (key → slice) · **CHROMATIC** (active slice pitched by key) · **RANDOM** (no-repeat random slice pitched by key). This makes the engine a breakbeat/slice-sampler, not just a trimmer.
- **Flow:** open a one-shot → chop → dupe a chop → transient-find → reverse/trim/normalize → make it yours → play across the keyboard.
- Anti-click via existing `CHOP_FADE_MS`.

## 7. Wiring (full citizen of the world)

- **Mod matrix:** SCAN, STRETCH, FORMANT, SPRAY, XFADE, LS, LE, Start, End are all mod destinations — reachable by the 10 LFOs, 5 envelopes, and FLOW-DRIFT lanes, same drag-to-modulate as every other knob.
- **Signal path:** Sample engine output is summed into the voice exactly like the wavetable osc, then through the **existing per-voice filter (1/2) + FLOW (Arp/Chop/Glitch/Drift) chain** — no special-casing.
- **SPRAY** taps the NoteOn-random modulation source.
- **DSP foundation already on disk:** `SamplerVoice.h`, `SampleBuffer.h`, `SampleLoader.h`, `Slice.h`, and params `SLICE_MODE / SLICE_SUB_MODE / SAMPLE_LOOP_MODE / CHOP_FADE_MS`. The engine is a per-osc front-end on this shared buffer (Serum's one-buffer model), not a from-scratch sampler.

## 8. Architecture notes (constraints for the plan)

- **Engine-conditional front:** cleanest is a separate Sample front block (visualizer + strip + 5 sample knobs) shown/hidden by engine state, toggled like the existing front↔back swap — because the 5 sample knobs bind to entirely different params than the wavetable knobs. Final mechanism is the plan's call.
- **New params:** Opus adds `SYN_OSC_{A..D}_SAMPLE_{SCAN,STRETCH,FORMANT,SPRAY,XFADE}` + loop-mode/LS/LE/start/end/snap params, each with the full **4-point WebSliderRelay binding** (relay member → `.withOptionsFrom` → `WebSliderParameterAttachment` → JS read) or it silently no-ops.
- **`SYN_OSC_x_ENGINE`** is a 6-choice param written as `idx/5`; **Sample = index 1**.
- **Cross-AI:** C++/DSP + new params = Opus (architect). Front-panel HTML/JS + mockup = this side, per recent "go all in" precedent. Mockup gets sign-off before wiring (CLAUDE.md §5).

## 9. Build/verify reminders (carried)

- index.html change → **bust the WebUI BinaryData cache** + reconfigure before building, then `strings`-verify embed.
- Build **both** VST3 **and** AU.
- New params: pre-flight `grep -c <Param> PluginEditor.*` before claiming done.
