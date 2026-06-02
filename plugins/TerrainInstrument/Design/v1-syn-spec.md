# Terrain Instrument · SYNTH Page 1 · Design Spec (v1)

**Status:** LOCKED 2026-06-01 after 12 mockup iterations.
**Locked mockup:** `Design/v1-syn-page-mockup.html` (open in Safari/Chrome to visually reference)
**Memory:** `terrain-instrument-synth-page-1-mockup-locked` (full session decision history)
**Branch:** `feature/terrain-instrument`, base commit `82d349b`

> Read this spec end-to-end before writing any code. Every decision is intentional and was reached through iterative review with the user. Do not rename, recolor, or restructure without checking with the user first.

---

## Window dimensions (fixed)

- Plugin window: **820 × 640**
- Header: **44px**
- Tab nav: **22px** (page selector: `1 · 2`)
- Page body: **574px tall**, **820px wide**
  - Padding: 12 top, 12 bottom, 14 left, 14 right
  - Inner usable: 792 × 550

## Tab nav (`1 · 2`)

- Tab `1` (this page) = SYNTH
- Tab `2` = MORE (Phase 2 page — Analog Character, SUB+NOISE, line LFOs, spectral, multi-filter routing, synth FX rack, arp+chord memory, MPE, preset morph — see Page 2 section at end)
- Right side shows current page name in small caps (e.g., `SYNTH`)
- Style: thin font, active gets thin purple underline + white text

## 4-row grid layout (top to bottom)

| Row | Height | Content |
|-----|--------|---------|
| 1 | 152 | OSC · A  /  CROSS  /  OSC · B |
| 2 | 148 | FILTER  /  FLOW |
| 3 | 96 | ENVELOPES  /  MODULATION |
| 4 | 138 | RIBBON  /  VOICE META |

Gaps: 12px between rows + columns.

---

## Row 1 — OSC · A · CROSS · OSC · B

Grid columns: `1fr 104px 1fr` with 12px gaps.

### OSC · A (icon engine pills)

Top to bottom inside the device:
1. Top-left label: `OSC · A`
2. Top-right corner: current engine + frame counter (e.g., `WAVETABLE  12 / 256`)
3. **Engine pills row** — 6 pills, **ICONS ONLY (no text)**:
   - `ic-wt` — 3 stacked sine waves
   - `ic-samp` — bracketed waveform
   - `ic-gran` — 9-dot grain cluster
   - `ic-spec` — 5 FFT bars
   - `ic-fm` — nested circles
   - `ic-noise` — jagged static line
4. **Wavetable display** — bright white path on dark, corner brackets. **Right-click opens preset browser** (see browsers section).
5. **5-knob row**: `OCT · SEMI · CENT · LEVEL · PAN`

### CROSS (104px column between OSCs)

Grid rows: `22px 28px 1fr` with 7px gaps.

1. **A → B direction switcher**:
   - Two equal pills (`[ A ][ B ]`) with arrow between
   - Click flips direction (A→B vs A←B)
   - Active pill: solid purple bg + white text
   - Default: A is active (A→B for FM-style routing)
2. **SYNC pill** — full-width 28px tall toggle button. Active: solid purple + WHITE.
3. **3 symmetric knobs** (24px each, equal spacing): `FM · RING · MIX`

### OSC · B (text engine pills — the legend)

Same as OSC A, but engine pills are **text labels** (WT · SAMP · GRAN · SPEC · FM · NOISE) instead of icons. Same pill height (34px) so wavetable displays line up.

This is intentional: OSC B's text labels teach the user what each icon in OSC A means.

---

## Row 2 — FILTER (318px) · FLOW (rest)

### FILTER device

Top-left label: `FILTER`.

Top-right: **1/2 toggle** — two equal 22×18px pills `[1][2]`. Click to swap which filter is being edited. Active: solid purple + white.

Below the label:

1. **Filter type dropdown** — full width pill, shows current filter (e.g., `LADDER LP · 24`) + chevron
2. **Filter response curve display** — bright white path
3. **4-knob row**: `CUT · RES · DRV · ENV`

**Filter 1 and Filter 2 are independent.** Each holds its own type + params. Their routing (parallel/serial) is a Page-2 control.

**Filter type list (right-click or click dropdown to open):**

```
LADDER          STATE-VARIABLE      COMB & RESO         FORMANT           SPECIAL
LADDER LP · 24  SVF LP              COMB +              FORMANT · A       REVERB FILTER  ← Pigments
LADDER LP · 12  SVF HP              COMB −              FORMANT · E       PHASER 4P
LADDER HP · 24  SVF BP              COMB SHIMMER        FORMANT · I       PHASER 8P
DIODE LP        SVF NOTCH           KARPLUS-STRONG      FORMANT MORPH     RING MOD
ACID 303        OB-X SVF                                                  BODE SHIFTER
                                                                          BIT-CRUSH
                                                                          WAVESHAPER
                                                                          GRAIN MASK
```

### FLOW device (the differentiator)

> **Naming rule:** Do NOT use "voice leading" anywhere in marketing or UI copy. User explicitly avoided Auren/Null's terminology.

Top-left label: `FLOW`. Corner tag: `TERRAIN` in purple-400.

Grid columns: `1fr 188px` (modes left, vis right).

**Mode pills** (2×2 grid, big icons, no inline text):
- `ANCHOR` — voices bind to nearest neighbor (tethered nodes icon)
- `TRAIL` — voices follow play order (4-dot comet, fading left to right)
- `CASCADE` — voices descend stepwise (staircase icon)
- `DUST` — voices scatter randomly (irregular varied-size dots)

Active mode: solid purple bg + WHITE icon.

**Trajectory visualization (right column)**:
- Top: small SVG showing chord A (left, 3 white dots) and chord B (right, 3 white dots) with **purple curves** connecting voices — the actual voice paths
- Bottom: thin row of 5 mini-knobs (22px each):
  - `TIME` — glide time
  - `SHAPE` — curve shape
  - `SCATTER` — randomization amount
  - `TRAJ` — **per-voice trajectory amount** (Terrain innovation beyond Auren's 4 fixed modes)
  - `MORPH` — **morph between modes live** (Terrain innovation — riding this knob crossfades between FLOW modes during playback)

---

## Row 3 — ENVELOPES (318px) · MODULATION (rest)

### ENVELOPES (interactive)

1. Top-left label: `ENVELOPES`
2. **4 env tabs** in a row: `AMP · FLT · PITCH · MOD`
   - Active tab: WHITE text, thin purple underline
3. **Interactive ADSR canvas** — fills the rest of the device.
   - Bright WHITE curve, gentle fill
   - **No dots, no labels on the curve** (user feedback: producers know ADSR; dots clutter)
   - **Drag any line segment to adjust** — same gesture as Terrain's slicer lab-card ADSR
   - 3 conceptual handles: attack peak, decay-to-sustain corner, release end — invisible but click-targetable

All 4 envelopes (AMP/FLT/PITCH/MOD) share the same canvas — tab to switch.

### MODULATION

1. Top-left label: `MODULATION`
2. Top-right: `5 / 32 ROUTES` (live count)
3. Grid columns: `156px 1fr` (LFO stack left, patchbay right).

**LFO stack** (3 LFOs vertically stacked, each 22px tall):
- Tag `L1` / `L2` / `L3` in bright white
- Shape thumbnail (bright white SVG: sine, triangle, square sequence, etc.)
- **Rate dropdown** — clickable, shows current rate (e.g., `1 / 4`) + chevron
- Rate options: `1/64 · 1/32 · 1/16 · 1/8T · 1/8 · 1/4T · 1/4 · 1/4D · 1/2 · 1 bar · 2 bar · 4 bar · 8 bar · FREE Hz`
- Right-click LFO → shape picker (sine/tri/sqr/sawUp/sawDown/S&H/smoothRandom + draw-your-own line LFO — see Page 2)

**Patchbay** (modulation matrix display):
- 5 visible routes (more in 32-slot list)
- Each row: `[purple-bg L1 tag] → OSC A · WAVE   [bipolar amount bar]   [+32%]`
- Hint line at bottom: `DRAG SOURCE → KNOB · ADD ROUTE`
- Source tags (L1/L2/L3/ENV) are solid purple-on-white pills — clickable
- Drag any source tag onto any knob anywhere on the page to create a new route

---

## Row 4 — RIBBON (rest) · VOICE META (110px)

### RIBBON (CS-80 expression)

Top-left label: `RIBBON`. Top-right tag: `CS-80 EXPRESSION`.

**The strip itself** (44px tall):
- Octave ticks at C2/C3/C4/C5/C6, labels in small light gray
- Center neutral dashed line at vertical middle
- **Active voice positions** drawn as solid WHITE dots along the strip
- **Last-touch indicator**: slightly larger ring outline at the last touched position

**Below the strip — control row**:
- `RIBBON MODE dropdown` (purple-filled pill with white text + chevron) — 180px wide
- 4 mini-knobs (26px each): `RANGE · GLIDE · DRIFT · RETURN`

**Ribbon mode options (the dropdown):**
```
PITCH
  PITCH BEND
  PITCH DRIFT       ← analog (default)
  PITCH SHIFT       ← snap-to-semi
  PITCH GLIDE       ← smooth
TONE
  FILTER CUTOFF
  OSC A↔B MIX
MODULATION
  LFO 1 · RATE
  FLOW · TIME
  CHORD INVERSION
  USER · ASSIGN…
```

All 4 knobs are mod targets (any LFO/ENV can drive `RIBBON · DRIFT`, etc.).

### VOICE META (110px column on right)

- `VOICES  8` — polyphony cap
- `UNISON  3` — voice spread count
- `SPREAD 42` — stereo spread amount

**This column is the visual reference for "correct text contrast" on the whole page.** Every other label was brightened to match.

Each row: `[icon] LABEL  [VALUE]` — icon 11px, label small caps muted, value 13px bright white.

---

## Right-click engine browsers (each engine has one)

Right-click any OSC display → opens a categorized preset browser overlay for that engine.

### WAVETABLE (25+ presets)

```
ANALOG          DIGITAL        VOCAL              METALLIC          EXPERIMENTAL       USER
Prophet Saw     PPG Wave       Choir · A→O        Bowed Metal       Dustbowl           Import WAV…
Jupiter PWM     DX7 EP         Whisper            Glass Harmonics   Static Evolve      From Sample…
Moog Sqr        D-50 Bell      Vowel Morph        Railroad          Spectral Drift     From Image (PNG)…
OB-X Saw        M1 Piano                                            Serum HD           Random Gen
CS-80 Brass
Juno Str
```

### SAMPLE (Pigments-class — built-in library + USER + Settings)

```
FACTORY · DRUMS       FACTORY · VOCAL      FACTORY · INSTRUMENTS    FACTORY · LOOPS    FACTORY · TEXTURE
808 Kick              Chop · AH            Rhodes MK1               Amen Break         Sub Drone
Rim 909               Chop · YEAH          Upright Pno              Funk Loop          Vinyl Crackle
Foley Clap            Breath               Pizz Str

USER                  SETTINGS                          ACTIONS
Import WAV…           Start / End                       → MAKE WT  (resynth sample to wavetable)
From Chop (slicer)    Loop Points
Multi-Sample…         Velocity Zones
```

### GRANULAR

```
CLOUDS          RHYTHMIC          EVOLVING          EXPERIMENTAL       SOURCE
Dense Cloud     Pulse Stream      Slow Bloom        Reverse Grains     From OSC A
Scattered       Buzz Trig         Rain Fall         Pitch Shards       From Sample
Frozen Shard    Stutter Gate      Wind Drift        Spray Poly         From Audio In
Swarm
```

### SPECTRAL

```
FREEZE & BLUR      MORPH             SHIFT                   RESYNTHESIS
Spectral Freeze    Vocal → Bell      Formant Shift           From Sample
Bin Blur           Pad → Choir       Pitch Scatter           From Image
Fog                A↔B Morph         Harmonic Retune         From Chop
```

### FM

```
CLASSIC FM       PERCUSSIVE      METALLIC          EVOLVING       ALGORITHM
DX7 E.Piano      FM Marimba      FM Gamelan        FM Drone       2-op stack
FM Bell          FM Tine         FM Steel          FM Sweep       3-op feedback
FM Brass         FM Perc         FM Tubular                       4-op complex
FM Bass
```

### NOISE

```
COLOR            VINTAGE              FOLEY              SHAPED
White            Vinyl Crackle        Rain               Resonant Noise
Pink             Tape Hiss            Wind               Formant Noise
Brown            Record Pops          Crowd Murmur       Metal Noise
Blue             Cassette Wow
Violet
Green (airy)
```

---

## Visual identity — LOCKED tokens

Match `plugins/TerrainInstrument/Source/ui/public/index.html` `:root` block. The mockup overrides slightly brightened values for v11/v12 — adopt these for the synth page:

```css
:root[data-theme="dark"] {
  --bg-main: #1A1A2E;
  --bg-hero: #12121F;
  --bg-surface: #232340;
  --bg-card: #2A2A48;
  --bg-card-2: #252340;
  --purple-600: #9B6DFF;
  --purple-500: #8B5CF6;
  --purple-400: #B794FF;
  --border: rgba(58,58,88,0.55);
  --border-strong: rgba(140,130,180,0.45);
  --knob-track: rgba(120,118,160,0.35);
  --text-primary: #ECE8F2;     /* THE bright */
  --text-secondary: #C5BFD2;
  --text-muted: #908599;
  --text-dim: #6E6580;
}
```

(Light mode inverts via existing Terrain pattern — purple-on-white.)

### Rules (non-negotiable)

1. **Active = solid purple-500 BG + WHITE (#FFFFFF) text/icon.** Never purple-on-purple low contrast.
2. **Waveforms / envelope curves / filter response / LFO shapes are bright white** (`#ECE8F2`). The reference is the VOICES/UNISON/SPREAD number style.
3. **Knob labels: white** (`text-primary`), never muted gray.
4. **Font:** `"SF Pro Display", "SF Pro Text"`, weights **200–300 only**, never 600/700, never `font-weight: bold`.
5. **NO `box-shadow` glow effects.** All "active" feedback is via solid bg + contrast.
6. **No decorative emblems** before device labels (OSC · A / FILTER / FLOW / etc.). Text labels only.
7. **Generous spacing** — gaps 12px+ between major elements. No two elements touch edges. No cramming.
8. **No in-display overlay text** in OSC wavetable displays. Frame counter goes in the corner label area, not on top of the wave.

---

## Parameter namespace (`SYN_*`) — extending `ParameterIDs.hpp`

Follow existing convention: `ALL_CAPS_UNDERSCORE`. Group by feature.

```cpp
// Synth — Oscillators
constexpr char SYN_OSC_A_ENGINE[]   = "SYN_OSC_A_ENGINE";    // enum 0..5 (WT/SAMP/GRAN/SPEC/FM/NOISE)
constexpr char SYN_OSC_A_OCT[]      = "SYN_OSC_A_OCT";
constexpr char SYN_OSC_A_SEMI[]     = "SYN_OSC_A_SEMI";
constexpr char SYN_OSC_A_CENT[]     = "SYN_OSC_A_CENT";
constexpr char SYN_OSC_A_LEVEL[]    = "SYN_OSC_A_LEVEL";
constexpr char SYN_OSC_A_PAN[]      = "SYN_OSC_A_PAN";
constexpr char SYN_OSC_A_WT_FRAME[] = "SYN_OSC_A_WT_FRAME";
constexpr char SYN_OSC_A_WT_PRESET[] = "SYN_OSC_A_WT_PRESET"; // index into wavetable browser list
// (mirror for SYN_OSC_B_*)

// Cross-OSC routing
constexpr char SYN_CROSS_DIR[]   = "SYN_CROSS_DIR";   // 0=A→B, 1=A←B
constexpr char SYN_CROSS_SYNC[]  = "SYN_CROSS_SYNC";  // bool
constexpr char SYN_CROSS_FM[]    = "SYN_CROSS_FM";
constexpr char SYN_CROSS_RING[]  = "SYN_CROSS_RING";
constexpr char SYN_CROSS_MIX[]   = "SYN_CROSS_MIX";

// Filter (dual)
constexpr char SYN_FILTER_SLOT[] = "SYN_FILTER_SLOT"; // 0=Filter1 selected, 1=Filter2 selected (UI only)
constexpr char SYN_FILTER1_TYPE[] = "SYN_FILTER1_TYPE"; // enum 0..N for 25+ filter types
constexpr char SYN_FILTER1_CUT[]  = "SYN_FILTER1_CUT";
constexpr char SYN_FILTER1_RES[]  = "SYN_FILTER1_RES";
constexpr char SYN_FILTER1_DRV[]  = "SYN_FILTER1_DRV";
constexpr char SYN_FILTER1_ENV[]  = "SYN_FILTER1_ENV";
// (mirror for SYN_FILTER2_*)

// FLOW (voice-leading rebranded)
constexpr char SYN_FLOW_MODE[]    = "SYN_FLOW_MODE";    // enum 0..3 (ANCHOR/TRAIL/CASCADE/DUST)
constexpr char SYN_FLOW_TIME[]    = "SYN_FLOW_TIME";
constexpr char SYN_FLOW_SHAPE[]   = "SYN_FLOW_SHAPE";
constexpr char SYN_FLOW_SCATTER[] = "SYN_FLOW_SCATTER";
constexpr char SYN_FLOW_TRAJ[]    = "SYN_FLOW_TRAJ";    // per-voice trajectory amount
constexpr char SYN_FLOW_MORPH[]   = "SYN_FLOW_MORPH";   // morph between modes

// Envelopes (4)
constexpr char SYN_ENV_AMP_A[]   = "SYN_ENV_AMP_A";
constexpr char SYN_ENV_AMP_D[]   = "SYN_ENV_AMP_D";
constexpr char SYN_ENV_AMP_S[]   = "SYN_ENV_AMP_S";
constexpr char SYN_ENV_AMP_R[]   = "SYN_ENV_AMP_R";
// (mirror for SYN_ENV_FLT_*, SYN_ENV_PITCH_*, SYN_ENV_MOD_*)

// LFOs (3)
constexpr char SYN_LFO1_SHAPE[]  = "SYN_LFO1_SHAPE";
constexpr char SYN_LFO1_RATE[]   = "SYN_LFO1_RATE";
constexpr char SYN_LFO1_DEPTH[]  = "SYN_LFO1_DEPTH";
constexpr char SYN_LFO1_SYNC[]   = "SYN_LFO1_SYNC";  // sync mode (free Hz or beat division)
// (mirror for SYN_LFO2_*, SYN_LFO3_*)

// Ribbon
constexpr char SYN_RIBBON_MODE[]   = "SYN_RIBBON_MODE";   // enum (10 modes)
constexpr char SYN_RIBBON_RANGE[]  = "SYN_RIBBON_RANGE";
constexpr char SYN_RIBBON_GLIDE[]  = "SYN_RIBBON_GLIDE";
constexpr char SYN_RIBBON_DRIFT[]  = "SYN_RIBBON_DRIFT";
constexpr char SYN_RIBBON_RETURN[] = "SYN_RIBBON_RETURN";

// Voice
constexpr char SYN_VOICES[]  = "SYN_VOICES";  // polyphony 1..16
constexpr char SYN_UNISON[]  = "SYN_UNISON";  // 1..8
constexpr char SYN_SPREAD[]  = "SYN_SPREAD";  // 0..100%

// Tab nav
constexpr char SYN_PAGE[]    = "SYN_PAGE";    // 0=main, 1=more (page-2)
```

**Mod matrix integration:**
- Extend `ModulationEngine.h` `ParamIndex` enum with all `SYN_*` knobs as targets.
- Add sources: 4 envelopes (Amp/Flt/Pitch/Mod) + 3 LFOs + Ribbon (as a mod source itself).
- Existing 32-route limit stays.

---

## Architecture decisions made

### Synth pipeline: parallel to layers (NOT 5th layer)

The synth runs as its own polyphonic dispatcher next to the existing 4-layer sampler. Output routes through the same master FX chain or via `IndyFxChain.h` for independent routing.

- Add `SynthVoice` class — mirror `SamplerVoice.h` structure (juce::SynthesiserVoice subclass).
- Add `SynthSound` (juce::SynthesiserSound subclass — sentinel).
- Allocate 16 synth voices in `PluginProcessor` (independent of layer pool).
- `SYN_VOICES` parameter caps active count; voice allocator implements standard "free voice / oldest steal" with FLOW-aware preference (NEAR mode prefers reuse, RND mode prefers fresh).

### FLOW = polyphonic voice-leading allocator (the differentiator)

Each FLOW mode is a deterministic voice-allocation rule run on `noteOn` for the new chord vs. the existing held set:

- **ANCHOR**: Hungarian-algorithm nearest-pitch match between held voices and new chord notes
- **TRAIL**: Voices assign in chord-input order (first new note → oldest voice slot, second → next, etc.)
- **CASCADE**: Voices descend per pitch — highest new note → topmost held voice, then descend
- **DUST**: Random assignment with `SYN_FLOW_SCATTER` weighting

Plus the two Terrain innovations:
- `SYN_FLOW_TRAJ` adds per-voice random trajectory deviation (each voice's glide path is slightly different even within the same FLOW mode)
- `SYN_FLOW_MORPH` crossfades between two FLOW modes in real time

Glide curve uses `SYN_FLOW_TIME` (ms) + `SYN_FLOW_SHAPE` (curve exponent: linear/exponential/log/S-curve).

### Filter dual-slot (page 1 holds both, routing on page 2)

Both filter instances live in `SynthVoice::renderNextBlock`. Routing (parallel/serial/A-only/B-only) is a Page-2 `SYN_FILTER_ROUTING` enum param. Page 1 shows whichever filter slot is selected.

### Ribbon

Implemented as a JUCE custom touch slider that drives `SYN_RIBBON_*` params. Mode dropdown maps the touch position to different mod-matrix targets. Defaults to PITCH DRIFT — applies analog-style detuning (noise+filter pipeline) modulated by ribbon position.

---

## Phase plan (suggested decomposition for implementation session)

1. **Phase 1 — Minimum Playable Synth Voice (MPV)** ✅ SHIPPED 2026-06-01 (tag `mark-2-synth-phase-1-mpv`)
   PolyBLEP saw oscillator (engine 0) + juce::dsp::LadderFilter LPF24 + juce::ADSR AMP envelope + 8-voice juce::Synthesiser. 12 SYN_* APVTS params (OSC A: ENGINE/OCT/SEMI/CENT/LEVEL/PAN, FILTER1: CUT/RES, AMP env: A/D/S/R). #syn-panel UI with native HTML controls (canvas knobs deferred to Phase 2 visual polish). Parallel pipeline beside the 4 LayerStates — synth audio sums into master `buffer` after the layer loop, before the master FX chain, so it flows through grain/tape/space/delay/EQ/chorus. Build green at commit `bc70bac`.

2. **Phase 2 — Wavetable engine**
   - **2A SHIPPED 2026-06-02** (tag `mark-2-synth-phase-2a-wavetable-foundation`): frame-based wavetable engine, bilinear lookup, 6 iconic analog tables (Prophet Saw / Jupiter PWM / Moog Sqr / OB-X Saw / CS-80 Brass / Juno Str) generated additively at startup, 16 frames per table, WAVETABLE dropdown + FRAME slider in #syn-panel. Default preset = Prophet Saw.
   - **2B SHIPPED 2026-06-02** (tag `mark-2-synth-phase-2b-wavetable-expansion`): 14 more tables across 4 new categories — Digital (PPG Wave, DX7 EP, D-50 Bell, M1 Piano), Vocal (Choir A→O, Whisper, Vowel Morph), Metallic (Bowed Metal, Glass Harmonics, Railroad), Experimental (Dustbowl, Static Evolve, Spectral Drift, Serum HD). Bank now holds 20 wavetables. Dropdown uses `<optgroup>` headers to categorize. Right-click categorized browser overlay deferred to UI polish pass.
   - **2C SHIPPED 2026-06-02** (tag `mark-2-synth-phase-2c-warp-modes`): live wavetable warp modes — BEND (Casio CZ-style phase distortion), SYNC (virtual hard-sync slave oscillator), FORMANT (phase-scale formant shift). Applied to phase BEFORE wavetable lookup so warp composes with any of the 20 tables. 2 new APVTS params (`SYN_OSC_A_WARP_MODE` choice, `SYN_OSC_A_WARP_AMOUNT` float).
   - **2D (deferred):** USER imports — Import WAV, From Sample (resynthesis), From Image PNG, Random Gen. Requires file picker UI infrastructure.

### Phase 2 UI alignment debt (mockup vs current implementation)

The Phase 2A/B/C controls SHIP FUNCTIONALLY but DO NOT match their intended v12 mockup positions. To be addressed in a UI polish pass after Phase 8 completes:

| Control | Current spot | v12 mockup spot |
|---|---|---|
| ENGINE selector | `<select>` dropdown in OSC row | Row of 6 ICON pills above the wavetable display (per spec "Row 1 — OSC · A" §) |
| WAVETABLE preset | `<select>` with optgroups in OSC row | Right-click on the OSC wavetable display → categorized browser overlay |
| FRAME | `<input range>` slider in OSC row | Implicit — corner label "WAVETABLE 12 / 256" shows it; knob may be modulation-only |
| WARP MODE | `<select>` in OSC row | **Not yet designated** — needs explicit slot. Candidates: right-click overlay extras, dedicated "engine controls" row, or modulation-only |
| WARP AMOUNT | `<input range>` slider in OSC row | **Not yet designated** — same as WARP MODE |
| FRAME / WARP AMT (per-engine semantics) | Repurposed: NOISE=color/drive, FM=ratio/depth, WT=frame/warp | Mockup didn't anticipate per-engine semantics. Per user "ship functional, polish later" framing — knob labels stay generic ("FRAME", "WARP AMT") for now; future polish: contextual labels that swap per engine, OR per-engine right-click browsers that expose dedicated controls. |

**Rule going forward:** every new control added in Phase 3+ MUST be anchored to a specific v12 mockup region BEFORE adding the widget. If the mockup doesn't have a slot for the control, either propose where it goes (and update the spec) or fold the function into modulation matrix only.

3. **Phase 3 — Multi-engine OSC chassis (OSC A)**
   - **3 SHIPPED 2026-06-02** (tag `mark-2-synth-phase-3-osc-a-engines`): Engine dispatch in `tw::SynthVoice::renderNextBlock` via `switch (engine_)`. NOISE engine (xorshift32 white + one-pole LP "color" via FRAME knob + tanh "drive" via WARP AMT). FM engine (2-op stack — modulator ratio via FRAME knob 0.25×..8×, modulation depth via WARP AMT 0..2π). SAMP / GRAN / SPEC ship as silent stubs (labelled "(soon)" in the dropdown) — engine selection wires through to the dispatch switch, but those arms render zero pending future phases. WAVETABLE dropdown dims when engine != WT (visual hint only — APVTS param remains automatable). No new APVTS params, no new widgets: FRAME + WARP AMT are repurposed per-engine. OSC B chassis split to its own focused phase. Per-engine right-click categorized browsers also deferred (current WAVETABLE `<select>` covers the WT engine; NOISE/FM use FRAME+WARP AMT controls instead of preset browsers; SAMP/GRAN/SPEC will get browsers when their DSP lands).
   - **3.5 (deferred):** OSC B chassis — mirror of OSC A (12 new SYN_OSC_B_* APVTS + relays + UI + dual-osc render in SynthVoice + simple A+B sum). Will be its own phase before cross-mod work in Phase 8.

4. **Phase 4 — FLOW glide engine**
   Implement ANCHOR / TRAIL / CASCADE / DUST allocators. Wire TIME / SHAPE / SCATTER. Add TRAJ + MORPH (the innovations).

5. **Phase 5 — Mod matrix + LFOs + per-voice mod**
   Extend `ModulationEngine.h` with `SYN_*` targets. Wire 3 LFOs + 4 envelopes + drag-to-assign UX.

6. **Phase 6 — Multi-filter + filter envelope**
   Add 3-5 filter types initially, then expand to the full 25+. Wire filter env.

7. **Phase 7 — Ribbon + voice settings (VOICES/UNISON/SPREAD)**

8. **Phase 8 — Cross-mod (SYNC/FM/RING/MIX)**

(Phases 9+ live on Page 2 — Analog Character, SUB+NOISE, line LFOs, spectral, etc.)

---

## Page 2 — DEFERRED but committed (do not cut)

Accessed via the `2` tab. User said: "this is going to be THE ONE. lets make sure we can put all of this inside of Terrain Instrument bro, idc if we have to put it on PG 2."

1. **Analog Character strip** — DRIFT / WARMTH / DIRT / AGE
2. **SUB + NOISE dedicated slot** (Hydrasynth/ANA 2 pattern)
3. **Deeper modulation** — line LFOs (draw your own breakpoints), per-voice modulator toggle (Bitwig-style), env follower as mod source, drag-to-assign UX
4. **Master output stage** — final saturation, output gain, post-soft-clipper
5. **Spectral engine** — full-page spectral processor (filter / shifter / image-as-WT)
6. **Multi-filter routing** — parallel/serial routing for the two filters defined on page 1
7. **Synth-internal FX rack** — chorus / phaser / distortion (separate from Terrain's main DLY/EQ/MOD pills)
8. **Arpeggiator + chord memory**
9. **MPE config** — pressure / slide / pitch curves
10. **Preset randomizer / morph** — Hydrasynth-style A↔B snapshot glide

---

## Ambition reminder (DO NOT downgrade)

User framing throughout this brainstorm: "this is going to be THE ONE", "make history with it", "magnum opus", "authentic raw dirty analog", "Apple commissioned you to make them a plugin", "blockbuster."

Reference depth: **Vital + Serum 2 + Auren GL-01 + Pigments + Yamaha CS-80 + Hydrasynth**.

Differentiator: **per-voice trajectory + per-chop voice-leading rules** — composes multiplicatively with Terrain's existing per-chop architecture, impossible to replicate in any other plugin.

License caveats:
- Vital is GPLv3. Read for ideas only — clean-room reimplement. NO copy-paste into Terrain (closed-source commercial).
- Same for Surge XT, Helm, Dexed, Odin 2.
- PolyBLEP is standard DSP. Use freely. Don't market it (table-stakes).

---

## How to resume in a fresh session (code-writing intent)

1. Read this spec end-to-end.
2. Read memory `terrain-instrument-synth-page-1-mockup-locked` for full session context.
3. Open `Design/v1-syn-page-mockup.html` in a browser to visually re-anchor.
4. Inspect existing code:
   - `plugins/TerrainInstrument/Source/PluginProcessor.{h,cpp}` (processBlock structure, APVTS setup)
   - `plugins/TerrainInstrument/Source/LayerState.h` + `SamplerVoice.h` (voice allocation pattern to mirror)
   - `plugins/TerrainInstrument/Source/ParameterIDs.hpp` (existing naming convention)
   - `plugins/TerrainInstrument/Source/ModulationEngine.h` (extend ParamIndex enum)
   - `plugins/TerrainInstrument/Source/ui/public/index.html` (CSS theme tokens — must match this spec's values)
   - `plugins/TerrainInstrument/Source/IndyFxChain.h` (per-chop FX routing model)
5. Build via the APC protocol: `./scripts/build-and-install.sh -p TerrainInstrument` from repo root. **Never run cmake manually.**
6. Start with Phase 1 (MPV) per the suggested phase plan above. Don't skip ahead.
7. Use `superpowers:writing-plans` skill to create a step-by-step plan before touching code.
