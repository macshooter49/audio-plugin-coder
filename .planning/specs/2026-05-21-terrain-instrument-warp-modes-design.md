# SPEC: Terrain Instrument — Per-Chop Warp Modes

**Date:** 2026-05-21
**Branch:** `feature/terrain-instrument`
**Checkpoint:** `pre-warp-modes` tag → `b2c50f5`
**Status:** Awaiting user review before Phase 1
**Research foundations:**
- Technical: `.planning/research/2026-05-20-warp-modes-research.md`
- Aesthetic: `.planning/research/2026-05-21-warp-aesthetics-research.md`

---

## TL;DR

Add Ableton-style per-chop time-stretch warp modes to the Terrain Instrument slicer, with a gesture-driven UX: **shift + drag on a chop boundary handle = stretch — chops ARE the warp markers**. Three modes ship in v1.0: **BEATS** (transient-aware granular, custom DSP), **TONES** (Signalsmith Stretch, melodic), **TEXTURE** (Signalsmith + jitter, glitch/ambient). Per-chop mode picker (one letter on chop corner) + global BPM sync footer strip (manual entry + tap tempo). Default state per chop is `NONE` — current resample behavior preserved unmodified. Existing sessions behave identically until a chop is opt'd in. Ships in 3 phases with pluginval level-5 between each. **Caricature-forward — artifacts are features, not bugs.**

---

## Scope

### In scope (v1.0)

- 3 warp modes per chop: BEATS, TONES, TEXTURE (plus default NONE = current resample)
- Per-chop state: `warpMode` (enum 0-3) + `stretchRatio` (float, 0.25–4.0, default 1.0)
- **Shift + click + hold + drag on chop boundary handle** = stretch ratio adjustment
- Mode picker: one letter (B/T/X) in chop corner, click cycles, right-click submenu
- BPM sync footer strip: source BPM field, TAP button, global SYNC toggle
- Signalsmith Stretch as MIT header-only dependency, vendored at `_tools/signalsmith-stretch/`
- Per-voice stretcher instances on `SamplerVoice` (lazy-init, RT-safe)
- Apple Accelerate FFT backend on arm64 (`SIGNALSMITH_USE_ACCELERATE`)
- Voice steal policy: warped-voice cap = 8; oldest warped voice stolen when exceeded
- Backdrop-blur on every new menu surface; soft fades; no purple-on-purple hardness
- pluginval level-5 SUCCESS gate before every phase commit

### Out of scope (explicit non-goals)

- Complex / Complex Pro modes (duplicative with TONES + formant config)
- Per-mode parameter banks (transient envelope, loop direction, grain-size sub-knobs)
- Auto-BPM detection (aubio LGPL = packaging headache; manual + tap is the model)
- Warp markers separate from chops (chops ARE the markers)
- Per-chop BPM sync override (global toggle only)
- Modulation of warp mode (mode is a discrete choice)
- Mod targets on `stretchRatio` (defer to v1.1)
- **Touching Terrain Effects** (FX plugin is its own product — locked per memory)

---

## DSP architecture

### Mode-to-engine mapping

| Mode | Enum | Engine | At stretch=1.0 | Character emerges when... |
|---|---|---|---|---|
| `NONE` | 0 | Existing resample | identical to current playback | n/a — stretch ignored entirely |
| `BEATS` | 1 | Custom transient-detected granular | transparent passthrough | stretch < 0.8 or > 1.2 → choppy stutter |
| `TONES` | 2 | Signalsmith Stretch (default config) | transparent | stretch < 0.5 or > 2.0 → spectral smear, formant artifacts |
| `TEXTURE` | 3 | Signalsmith Stretch + grain-position jitter | mild jitter audible | any off-unity ratio → glitchy random texture |

### Per-voice architecture

- Each `SamplerVoice` owns one `WarpProcessor` instance, heap-allocated on first non-NONE assignment (avoids CPU cost for unwarped voices, of which there will be many).
- `WarpProcessor` selects engine at `prepareForNoteOn()` based on `VoiceConfig::warpMode`. Engine swaps reset+seed cleanly — no audible artifact on mode change between triggers.
- Engines live at:
  - `Source/Warp/BeatsEngine.h` — header-only, transient detection via spectral flux on 256-sample windows, cached at sample load. Stretches by looping sub-slices (transient → next transient) with 8ms equal-power crossfade. Loop direction alternates (forward/backward) when stretch >1.5× to dodge the robotic perfect-loop tell.
  - `Source/Warp/SignalsmithEngine.h` — wraps `signalsmith::stretch::SignalsmithStretch<float>`. TONES uses `setFormantFactor(1.0)` (formants preserved). TEXTURE skips formant correction and applies a `Source/Warp/TextureJitter.h` wrapper that perturbs grain positions by a configurable amount.
- All engines: reset + seek at note-on (mandatory — prevents voice bleed across triggers).
- RT-safe: zero allocations in `processBlock`. All buffers pre-sized in `prepareToPlay`.
- Polyphony cap: 8 warped voices max simultaneously. Unwarped voices retain current 32-voice limit. Voice steal favors oldest warped voice.

### Signalsmith integration

- Vendored as git submodule at `_tools/signalsmith-stretch/` (MIT, header-only)
- Zero compilation tax — pure header include
- `SIGNALSMITH_USE_ACCELERATE` defined on arm64 → Apple Accelerate FFT backend (~2× speedup measured by Signalsmith team)
- Reset + seek at each note-on (critical RT-safety pattern documented by Signalsmith)

---

## Per-chop state additions (Slice.h)

```cpp
enum class WarpMode : uint8_t {
    None    = 0,
    Beats   = 1,
    Tones   = 2,
    Texture = 3
};

struct Slice {
    // ... existing fields (start, end, reverse, pitch) ...
    WarpMode warpMode     = WarpMode::None;
    float    stretchRatio = 1.0f;  // clamped 0.25 .. 4.0
};
```

- Both new fields serialize through the existing `setSlicesJson` round-trip path (no new persistence work — already routed end-to-end).
- Preset save/load picks them up automatically.
- Stretch ratio clamped at the gesture boundary (`std::clamp(ratio, 0.25f, 4.0f)`).
- Old presets without these fields default to `WarpMode::None` / `1.0f` — zero behavioral change for existing user data.

---

## Gesture map (final, corrections applied)

| Gesture | Behavior | Status |
|---|---|---|
| Drag chop body vertically (no shift) | Pitch ±12 semitones | Existing |
| Scroll wheel on chop body | Pitch ±1 semitone per tick | Existing (just shipped) |
| Drag chop boundary handle horizontally (no shift) | Move boundary in source space | Existing |
| **Shift + click + hold + drag on chop boundary handle** | **Stretch ratio adjustment** | **NEW (headline)** |
| Click on mode letter in chop corner | Cycle warp mode (NONE → B → T → X → NONE) | NEW |
| Right-click chop → "Warp" submenu | Pick mode explicitly | NEW |
| Shift + scroll wheel | Undefined (intentionally not bound) | n/a |

### Shift+drag stretch direction

Consistent with the existing boundary-drag intuition: **outward grows, inward shrinks**.

- Drag LEFT boundary leftward (with shift) → stretch ratio increases
- Drag RIGHT boundary rightward (with shift) → stretch ratio increases
- Drag LEFT boundary rightward (with shift) → stretch ratio decreases
- Drag RIGHT boundary leftward (with shift) → stretch ratio decreases

User doesn't have to learn new direction semantics — same direction as the non-shift boundary drag, just operating on `stretchRatio` instead of source bounds.

### Shift+drag visual feedback

- Cursor changes to `↔` resize cursor when shift is held over a boundary handle.
- During drag: cursor moves; the boundary handle stays visually pinned at its source position. A floating tooltip follows the cursor showing the current ratio (e.g., `1.50×`, `0.75×`), styled identically to the existing pitch readout that floats during vertical-drag.
- On release: tooltip fades out (200ms ease); a small low-contrast `1.50×` label appears on the chop body above the pitch meter row.
- The stretch label is visible **only when ratio ≠ 1.0** — no clutter on unstretched chops.

---

## UI surface

### Persistent additions (visible at all times)

1. **Mode letter** in chop corner — B / T / X. Small monospace, ~10px, positioned bottom-right of chop body (or top-right if it collides with REV — confirm at impl). NONE shows nothing. Letter color matches the existing pitch-meter accent.
2. **Stretch ratio label** on chop body — `1.50×` style, monospace, low-contrast. Visible only when `stretchRatio ≠ 1.0`. Sits above the pitch meter row.
3. **BPM sync footer strip** — single horizontal row at the bottom of the slicer panel, sitting in the existing footer:
   - `SOURCE [120.00]` editable BPM field
   - `TAP` button (tap 4+ times → derive BPM from median inter-tap interval)
   - `SYNC` toggle pill (orange when on, matching existing footer pill styling)

### Contextual additions (visible on hover / interaction)

- **Floating ratio tooltip** during shift+drag — same render path as the existing pitch tooltip
- **Right-click "Warp" submenu** — appears via the existing slice context menu; new submenu inherits its backdrop-blur and fade-in pattern

### UI cleanliness rules (LOCKED — non-negotiable)

- **Every new menu surface gets backdrop-blur.** `backdrop-filter: blur(8px)` minimum on any new overlay, matching the existing slice context menu and chop drawer pattern.
- **No hard purple-on-purple overlap.** Every new element either uses the established `--bg-surface` CSS variable or sits on a blurred-out backdrop. The eye-strain rule.
- **Soft fade entrances/exits** — 200ms `ease-in-out` on opacity for every new overlay element.
- **Single-letter density check** — the mode letter must not collide with the REV tag, pitch meter, slice number, glow alpha overlay, or MPC mode indicator. If any collision detected during impl, the letter gets a small rounded semi-transparent pill background.
- **Zero new colors.** All new UI elements use existing Lunar Haze theme variables (`--bg-surface`, `--accent-purple`, `--text-primary`, etc.). No new hex codes introduced.

---

## BPM sync behavior

- **`SOURCE BPM` field**: editable, accepts 60–200 BPM, default 120, snap to 0.01 BPM.
- **`TAP` button**: collects tap timestamps in a rolling 4-element buffer; on each tap, derives BPM from median inter-tap interval; updates the SOURCE field live.
- **`SYNC` toggle (global):**
  - **ON**: chops with `warpMode != NONE` automatically scale their effective stretch ratio to play at host BPM. Effective ratio = `(hostBPM / sourceBPM) × chop.stretchRatio`. The stored/displayed `stretchRatio` is treated as a multiplier ON TOP of the BPM-sync ratio, so user manual stretch and host-sync compose cleanly.
  - **OFF**: `stretchRatio` used directly (manual stretch only).
  - Chops with `warpMode == NONE` are completely unaffected by the toggle — legacy behavior preserved no matter what.
- No per-chop sync override in v1.0 (defer to v1.1 if user feedback demands it).

---

## Phase plan & ship gates

### Phase 1 (~5 days) — Foundation + TONES mode

1. Vendor Signalsmith Stretch as submodule at `_tools/signalsmith-stretch/`
2. CMakeLists integration with `SIGNALSMITH_USE_ACCELERATE` on arm64
3. `Source/Warp/WarpProcessor.h` — per-voice RT-safe wrapper
4. `Source/Warp/SignalsmithEngine.h` — Signalsmith config wrapper
5. TONES mode end-to-end: UI letter cycle, shift+drag gesture handler, voice integration
6. `Slice.h` state additions + JSON round-trip wiring
7. **pluginval level-5 SUCCESS → commit**

**Phase 1 ship test:** Load a vocal sample, slice to 4 chops, set chop 2 to TONES, shift+drag right boundary to stretch=2.0 → chop 2 plays at 2× duration with formant character; chops 1/3/4 unchanged.

### Phase 2 (~5 days) — Character modes

1. `Source/Warp/BeatsEngine.h` — transient detection (cached at sample load), sub-slice crossfade loops with alternating direction
2. `Source/Warp/TextureJitter.h` — Signalsmith + grain-position jitter wrapper
3. Mode cycle expands to all 3 (NONE / B / T / X)
4. **pluginval level-5 SUCCESS → commit**

**Phase 2 ship test:** Load an Amen-break loop, slice to 8 chops, set all to BEATS, stretch all to 2.0 → cut-up jungle character (Photek target). Load wordless vocal pad, single chop, TEXTURE at stretch=0.5 → Burial-vocal-pad target.

### Phase 3 (~2 days) — Workflow polish

1. BPM sync footer strip UI (editable BPM field, TAP button, SYNC toggle)
2. TAP tempo logic (4-tap median)
3. SYNC toggle behavior (host-BPM-aware effective ratio in voice render)
4. **pluginval level-5 SUCCESS → commit**

### Every phase commit must end with

1. Clean build, no new warnings
2. pluginval level-5 SUCCESS on installed VST3
3. Manual smoke test in DAW (Logic or REAPER)
4. Atomic commit with descriptive message
5. Editor reload + plugin re-add in DAW (new native fns added → re-add required)

### Rollback policy

- Any phase failing ship gate → `git reset --hard pre-warp-modes`, replan, retry
- Phases ship independently; Phase 1 standing alone is acceptable if scope tightens
- **Never amend or move the `pre-warp-modes` tag.** It stays at b2c50f5 as the canonical rollback target.

---

## Sonic targets (from research-2 — DSP tuning pass/fail criteria)

These are the concrete user experiences each mode must produce. Each phase manual-tested against the relevant targets before commit.

1. **Photek-style jungle chop** — 120 BPM Amen-break loop, 8 chops, all BEATS at stretch=2.0 → cut-up jungle, not muddy phase smear
2. **Burial vocal pad** — wordless vocal sample, single chop, TONES at stretch=0.5 → stretched-and-haunted, not chipmunked
3. **Basinski decay sketch** — sustained pad/drone, single chop, TEXTURE at stretch=4.0 → decaying ambient, not phase-vocoder soup (DECAY mode would deliver this better — see Optional 4th mode)
4. **Cloud rap pitched ad-lib** — vocal one-shot, TONES at pitch +5 stretch=1.0 → clean pitched ad-lib with formant character, not chipmunked
5. **Vaporwave slowed sample** — pop loop, BEATS at stretch=0.7 → DJ-Screw / Eccojams feel without grit-cliché
6. **Footwork chop into granular feed** — drum chop, BEATS at stretch=1.5, fed into existing GrainEngine → choppy granular hybrid (THE "no other granular has this" target)
7. **Tape-treated stretched ambient** — pad, TEXTURE at stretch=2.0, into Cassette tape FX → "neither obviously digital nor obviously analog"

---

## Failure modes to avoid (from research-2)

- **Audiobook narrator cleanliness** — sterile too-clean stretching with no character. Worst sin. Engines error toward character.
- **Muddy phase-vocoder smear** — Signalsmith defaults can sound this way at extremes. Tune grain size + window settings.
- **Cliché 1996-jungle preset sound** — over-perfect BEATS stutter. Direction-alternating crossfade dodges the robotic-loop tell.
- **Grain-boundary clicks at low ratios** — Signalsmith produces these without correct seeding at note-on. Reset+seek is mandatory.
- **Transient doubling** — BEATS engine gates adjacent transients within ≥20ms spacing window.
- **Formant-broken vocals** — TONES enables formant preservation by default (`setFormantFactor(1.0)`).
- **Stereo collapse** — Signalsmith processes channels independently; verify stereo width preserved in Phase 1 smoke test.
- **Plugin demo overpolish** — resist the urge to clamp everything; let users dial into ugly territory if they want.

---

## Optional 4th mode for user decision: `DECAY` (the Basinski door)

**Not in v1.0 approved scope** — flagged for explicit user decision before Phase 4 planning.

### Why it's flagged

Research-2 strongly recommends a 4th mode with **no Ableton analog**: a sustained-material-degrades-over-time mode that physically references *Disintegration Loops* — the user's stated sonic north star per memory `terrain-instrument/16-vision-roadmap-2026-05-20.md`.

### Behavior sketch

Long-stretched material progressively loses harmonic content, gains tape-decay character, accumulates dropouts and skips. The longer the playhead runs in a triggered note, the more degraded the audio becomes. One parameter: `DECAY` (0-1, degradation rate per second of playback).

### Why it matters

Research-2 calls this "the move Ableton can't make" — Live's warp modes don't model time-evolving degradation. Combined with chops + grain feedback + Terrain's existing tape FX chain, this would be a Terrain-specific signature pairing naturally with the stated Basinski reference. The user's memory line "this granular has the function no other granular has" maps cleanly onto this mode.

### Cost

~3–4 extra dev days. Fits as Phase 4 after v1.0 ships.

### Decision

**User: add to v1.0 / defer to v1.1 / scrap?**

---

## Open questions / risks

1. **Mode letter collision** — chop body currently displays: pitch number, REV tag, pitch meter, slice number, glow alpha, MPC mode indicator. Adding a B/T/X letter requires layout audit during Phase 1. Fallback: pill background.

2. **CPU at 8 warped voices** — Signalsmith claims real-time on modern arm64. Profile early in Phase 1. If 8 voices is too many, drop limit to 4 and document. Acceptance threshold: <15% CPU usage in a 16-voice session with 8 warped voices on M1 baseline.

3. **JSON round-trip migration** — old presets default to `NONE` / `1.0` for new fields. Test against existing user presets before commit.

4. **DAW reload required after install** — new native functions (`setSliceWarpMode`, `setSliceStretchRatio`, `setBpmSync`, etc.) mean users must remove + re-add the plugin in their DAW post-install. Document in release notes.

5. **Boundary-handle hit area** — current boundary handles may be too narrow for reliable shift+click+drag at the user's typical zoom level. May need to widen hit area (visual unchanged) during Phase 1.

6. **The DECAY mode question** — see Optional section above. User decision needed before Phase 4 planning.

---

## User sign-off needed before Phase 1 starts

| Item | Status |
|---|---|
| 3-mode scope (BEATS / TONES / TEXTURE) | ✅ Confirmed earlier turn |
| Gesture map (shift + drag boundary = stretch) | ✅ Confirmed earlier turn |
| UI cleanliness rules (blur, soft fades, no purple-on-purple) | ✅ Confirmed earlier turn |
| Signalsmith Stretch as MIT header-only dep | ✅ Confirmed earlier turn |
| BPM sync = global toggle (not per-chop) | ✅ Confirmed earlier turn |
| **Optional 4th mode (DECAY) — add / defer / scrap** | ⏳ **Pending decision** |
| **Spec doc itself — quick scan for anything wrong** | ⏳ **Pending review** |

---

*End of spec.*
