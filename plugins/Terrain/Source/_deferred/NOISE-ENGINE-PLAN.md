# NOISE ENGINE — design + build plan (Terrain Instrument)

Turn the dead CROSS module (center of the OSC row) into a real **Noise engine** — a center
column that mirrors the oscillators, with its own glass selector, a white transparent visualizer,
and Serum-2-style controls. Requested by Max 2026-07-15.

## Research (work vault → Serum 2 User Guide.pdf, p.128-132; + Pigments/Vital on hand)
Serum 2's noise osc = a **stereo sample player + algorithmic color modes**:
- **Colors (algorithmic):** White, Pink (-3 dB/oct), Brown (-6 dB/oct), Geiger (random clicks).
- **Samples:** factory noise .wav loops — *these are the "Tape Hiss / Vinyl / Space Wind" names in
  Max's screenshot; they are LICENSED content → we do NOT bundle them.* We synthesize equivalents.
- **Header:** name + power button (green when on); click name → menu (colors category + sample
  categories + Load Sample + Embed in Preset); **`‹ ›` arrows** step presets.
- **Controls:** Level, Pitch (+ Fine), Pan; color modes add **Stereo** (0=mono→100=decorrelated) and
  **Filter** (HP/LP); plus **Start** (sample-start %), **Rand** (randomize start per note),
  **loop vs one-shot**.
- **Routing:** NOISE is a routable source into Filter 1/2 (= our A/B/C/D/Sub/**N**).

**Verdict on a back panel: YES.** Front stays clean (type + viz + Level/Pitch/Pan); the extra Serum
controls (Filter, Stereo/Width, Start, Rand, Loop) live on a back panel like the osc/filter backs.

## Layout — center column, mirrors the oscillators (Max's spec)
Three vertical zones aligned to the osc columns:
1. **Header** (aligns with the osc headers, on the scope centerline): `Noise` + power + a **glass
   selector** showing the current type + **`‹ ›` arrows** — the SAME glass dropdown as the wavetable
   selector (categorized + Imports section at top + Import/Load). Purple accent menu.
2. **Visualizer** (aligns with the scope flatline, center): a **WHITE transparent** noise viz;
   **each type has its own look** (dense static, LF blobs, tape streaks, vinyl crackle dots, drifting
   space particles). Canvas-2D, offscreen-cached where static, animated.
3. **Knobs** (brought DOWN to align with the osc knob row): **Level / Pitch / Pan**.

## Noise types — ALL synthesized (no licensed samples) + imports
- **Colors:** White, Pink, Brown, Geiger (Serum's four), + Blue/Violet if wanted.
- **Character (algorithmic):** Tape Hiss (HP white + wow/flutter), Tape Hum (50/60 Hz + harmonics +
  hiss), Tape Air, Tape Crackle (sparse impulses + hiss); Clean Vinyl (LF rumble + sparse crackle +
  surface), Dirty Vinyl (more); Space Open (wide airy), Space Helium (resonant/pitched), Space Wind
  (slow-modulated bandpass). Each a procedural generator = filtered noise + impulse trains + slow mod.
- **Imports:** load your own audio as a looped noise source — reuse the wavetable import infra
  (folder scan + drag-drop + embed-in-preset).

## DSP
- Per-voice noise source, shaped by the amp env, routed through Filter 1/2 via the **N** routing.
  "Always plays when the module is On" (enable = the power button).
- Each type = a procedural generator (cheap: filtered white + impulse/mod) OR a looped import.
- Params (front): `NOISE_ON`, `NOISE_TYPE`, `NOISE_LEVEL`, `NOISE_PITCH`, `NOISE_PAN`.
  (back): `NOISE_FILTER`, `NOISE_STEREO`, `NOISE_START`, `NOISE_RAND`, `NOISE_LOOP`.
  Each needs the full 4-point WebSliderRelay binding (CLAUDE.md gotcha).
- CPU-friendly (hard rule): generators are O(1)/sample; imports reuse the sample-player path.

## Build phases (each ships + Max reacts)
- **P1 — Skeleton + core sound:** replace CROSS with the noise column (header + selector menu +
  Level/Pitch/Pan + placeholder viz). DSP: White/Pink/Brown/Geiger + on/off + routing + params/relays.
- **P2 — Visualizer:** white transparent, per-type visuals.
- **P3 — Character types:** the tape/vinyl/space generators, "for real."
- **P4 — Back panel:** Filter/Stereo/Start/Rand/Loop.
- **P5 — Imports:** drag-drop + folder + embed-in-preset (reuse wavetable import).

## Open questions for Max
- Back panel: confirmed yes? (Serum has the controls for it.)
- Type set: colors + tape/vinyl/space to start — any must-haves (e.g. "space noise" specifically)?
- Does noise get its own amp/pitch env, or ride the main voice env? (Serum rides voice env → default.)
