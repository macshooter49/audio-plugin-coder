# Terrain Instrument — Serum 2 FX Complete Reference
## The formal competitive teardown of the Serum 2 effects rack (research file for the MULTI-DEVICE CHAIN epic)

**Date:** 2026-08-14 · **Researcher session:** fb345+ (post Phase G certification)
**Primary source:** the OFFICIAL Serum 2 User Guide — found LOCALLY at
`/Library/Audio/Presets/Xfer Records/Serum 2 Presets/Serum 2 User Guide.pdf` (355 pp., build of 2025-03-17;
FX chapter pp. 152–182, Mixer pp. 144–151, Quality p. 319). Every module strip on pp. 161–182 was **read as an
image**, so the visualizer descriptions below are eyewitness, not paraphrase. Secondary: the official
"What's New in Serum 2" PDF, Xfer support KB, v2.0.17 changelog coverage. ⚠️ The local manual is the
**v2.0 launch build**; the fb315 distortion research measured the CURRENT (mid-2026) Distortion menu at
**18 modes** vs the manual's 13 — version deltas are flagged inline wherever they matter.

**Sibling documents** (per-device depth lives THERE; this file is the cross-cutting map):
`DISTORTION-BUILD-BIBLE.md` (shipped fb345) · `REVERB-BUILD-BIBLE.md` (shipped) · delay memory arc fb306-310
(shipped) · `CHORUS-BUILD-BIBLE.md` · `COMPRESSOR-BUILD-BIBLE.md` · `EQUALIZER-BUILD-BIBLE.md` ·
`FLANGER-BUILD-BIBLE.md` · `PHASER-BUILD-BIBLE.md` · `HYPER-BUILD-BIBLE.md` · `SPLITTER-BUILD-BIBLE.md` ·
`CONVOLUTION-USER-IR-ADDENDUM.md`.

---

## 0. Scope — what this file is, and the one-sentence verdict

Max's words: Serum 2's FX chain feels like *"a whole different world."* This file answers **why, precisely,
with the manual open** — a full param inventory of all 16 FX menu entries, a precise description of every
module visualizer (Max bases card mockups on visualizer research), the rack mechanics, and the two-way gap
analysis against what Terrain has already shipped and bibled.

**The one-sentence verdict, honest:** Serum 2's FX world-feel is **architectural, not per-device** — three
parallel racks with arbitrary module order, counts, and nesting, everything modulatable and preset-able —
while every individual device is intentionally **shallow (5–11 params, mostly no live visualizer)**;
Terrain already beats every Serum device on per-device depth (Type × Character axes, measured extremity,
dramatic visualizers) and loses ONLY on chain architecture. The epic's job is to steal the architecture
without giving up the depth.

---

## 1. The rack — mechanics teardown (the "whole different world," itemized)

Serum 2 ships **13 FX processors + 3 splitter modules = 16 menu entries** (manual p. 152: *"an effects
section with 13 different FX processors that you can use in any order or combination, including multiple
instances of the same processor. There are also three types of splitter modules."*).

### 1.1 Three racks + a routable mixer (the real superpower)

- FX tab holds **three tabs: MAIN, BUS 1, BUS 2** — three independent racks (p. 153).
- The **Mixer page** routes every source (each oscillator channel, each filter, noise) to Main, Bus 1 or
  Bus 2; the **busses themselves route to Main, Direct, or the OTHER bus** (p. 150) — so parallel chains,
  serial bus-into-bus chains, and a dry Direct path all coexist. Per-bus one-click FX bypass on the mixer.
- This is the fb303 "main-send insert" idea generalized: N racks × per-source sends. It is also exactly the
  territory where **fb305/fb338's exclusion-sum landmine** lives on our side — the distortion bible §4.5
  documents that adding a third send bus re-breaks fb305 at index.html:6979/:7111 unless two exact lines
  change. Any Terrain bus expansion starts there.

### 1.2 Module lifecycle (all p. 155–158, verified)

| Operation | Mechanics |
|---|---|
| Add | `+` button or right-click → Add FX Module; signal flows **top to bottom**; new module goes live in the chain instantly |
| Reorder | drag in list view (left) or rack view (right); a **yellow line** shows the landing slot |
| Duplicate | **Option-drag** copies WITHOUT modulations; **Shift-Option-drag** copies WITH modulations; also module-menu → Duplicate FX Module. **Multiple instances of the same processor are first-class** — yes, two delays, three distortions |
| Bypass | per-module button (red when bypassed) in list AND rack view; **Option-click = bypass ALL FX on the bus**; explicitly framed as a temporary A/B tool, "without having to set the MIX knob to 100% dry" |
| Remove | ✕ in list view |
| Rack presets | per-rack save/load ("Init" resets); right-click background: **Cut/Copy/Paste FX Bus** (move whole chains between busses), Clear, **Lock FX Bus / Lock All** (rack survives preset changes — but *"modulation assignments to module parameters in the locked rack are cleared"*), Load/Save FX Bus |
| Module presets | per-module menu: Factory presets, Save FX Preset, **Save as Default Preset** (your settings become the spawn state for that module type) |
| Expanded view | a toggle widens the FX tab into list-view + tall rack (Alt/Opt+F since v2.0.17) |

### 1.3 Modulation onto FX (p. 159 — the paraphonic caveat, quoted)

Drag any ENV/LFO onto most FX params. But: *"The FX rack is a DSP process that operates on the sum output
of the synth engine (rather than per voice)... the effects are monophonic... paraphonic behavior"* — so a
per-voice env assigned to an FX param **retriggers on every new note against the summed audio**. Serum
documents the wart; Terrain's mod-matrix-to-FX design should adopt the same block-rate path law we already
proved in fb260 (a mod SOURCE lives on two paths or it silently no-ops) and expose a "latest-note"
retrigger policy deliberately.

### 1.4 Splitter lane rendering (pp. 177–181, screenshots read)

A splitter is a rack module whose strip contains **colored lane panels** (LOWS/HIGHS, LOWS/MIDS/HIGHS, or
MID/SIDE) with a small crossover value between panels (default split shown: **210 Hz**) and a per-lane
bypass. Sub-modules are added by right-clicking a lane panel; they render **below the splitter in the rack**,
but the rack shows only the **currently selected lane's** modules — the left **list view shows the whole
tree** (SPLITTER L/H ▸ LOWS ▸ [Equalizer], HIGHS ▸ [Compressor, Hyper/Dimension]). Lane selection = click
the panel. Mix/Level on the splitter itself reblends the split output. The manual never shows a splitter
inside a splitter — nesting depth appears to be 1 (unverified; flagged in §11).

### 1.5 Quality / oversampling (p. 319 + What's New)

- **Global** quality menu: **Draft = 1× (none), High = 2×, Ultra = 4× oversampling**, with a **lock** so
  your choice survives preset loads; offline "Ultra quality when rendering" bounce option.
- **Per-module HQ** exists only where it matters: Delay's mode menu has a **High Quality option ("now
  default" per What's New)**; the Compressor's Limit ratio swaps in a true-peak limiter DSP path that can
  **introduce latency** (opt-in "Limiter Latency Comp" reports it to the host).
- Note Serum's global smoothing toggle: parameter smoothing is default-on engine-wide, with a global
  **Disable Smoothing** escape hatch for sample-accurate rhythmic automation. Terrain's no-clicks law is
  the same doctrine with the hatch welded shut.

### 1.6 CPU doctrine (official support KB)

Xfer's own optimization guide pushes users **off unison and onto the FX bus**: >3–7 unison voices is "a red
flag"; instead use *"a dedicated FX bus with a chorus effect — CPU-Friendly: applies the effect once instead
of processing it for every voice."* The Hyper module carries an in-manual tip: *"To conserve CPU, consider
using the HYPER effect as an alternative to high unison settings."* v2.0.17 fixed **Convolve CPU spikes**.
Lesson for Terrain: our bus-level FX already follow this shape; keep per-voice work out of devices, and
market the same "one chorus beats 16 unison voices" story.

### 1.7 Why it FEELS like a different world (the honest answer)

1. **Combinatorics, not depth** — 16 modules × any order × any count × 3 racks × nested band/MS lanes.
2. **Everything is a preset** — racks, busses, modules, module defaults. Sound designers ship FX racks as
   product. Lockable racks make the FX chain feel like an instrument of its own.
3. **Everything is a mod target** — LFO on a crossover, env on Bode shift.
4. **Direct manipulation** — the displays that do exist are draggable (drive in the transfer curve, delay
   filter freq/Q in the curve, filter cutoff/res in the response, splitter crossovers on the panels).
5. **Zero commitment friction** — yellow-line drag, Option-drag copy, one-click bypass everywhere.

None of those five require deep DSP. All five are UI/architecture. That is the epic.

---

## 2. Device-by-device teardown (all 16 entries)

Format per device: role → **param inventory** (ranges/defaults where the manual or screenshots state them) →
**visualizer, eyewitness** → the tell/weakness → the Terrain counter.

### 2.1 Bode  *(NEW in Serum 2 — the one device with NO Terrain bible yet)*

Implementation of the **Bode frequency shifter** (named for Harald Bode): shifts every partial by a fixed
Hz, breaking harmonic ratios — dissonance, phasing, motion. Crucially Serum's is not a bare shifter: it has
an **integrated feedback delay AROUND the shifter**, which is the classic **barberpole/spiral echo**
topology (each repeat shifts further).

| Param | What it does (manual verbatim distilled) |
|---|---|
| MONO INPUT | toggle — route mono input to the module |
| SHIFT | "percentage of the range to which to apply the pitch shift"; right-click → **Retrig** restarts the effect per note |
| RANGE | the range SHIFT sweeps over (small ranges = chorus-like Hz; big = mangling) |
| DIR | direction up/down; **center = both channels go opposite directions** (instant stereo divergence) |
| WIDTH | with DIR: both Bode channels (up + down) or a single channel |
| DELAY · BPM | delay time, sync toggle (BPM or Hz per manual wording) |
| FEED | "delay fed back into the Bode shifter, which can produce **pitched delays**" — the spiral |
| BALANCE | "delay input mix between a down and up shifted signal" — up-spiral vs down-spiral |
| BLUR | "chorus and wow/flutter effects" inside the loop |
| MIX / LEVEL | standard (0=dry, 100=fully wet; LEVEL in dB) |

**Visualizer:** none. A dark-red strip with a static zig-zag waveform logo badge; LED toggles; knobs.
**Tell/weakness:** SHIFT-as-percent-of-RANGE is two knobs for one number and hides the Hz; no display at
all for THE most disorienting effect in the menu.
**Terrain counter:** §5.3 proposes the full device (working name **Shift**) — 6 Types, 11-param chassis,
and a barberpole visualizer Serum doesn't have. This is the only Serum module with zero Terrain coverage.

### 2.2 Chorus

Four-voice chorus, two taps per side.

| Param | Range/notes |
|---|---|
| RATE · BPM | free **0–20 Hz**; synced **8 bars → 1/32** (house law 3 says OUR synced range is 4 bars → 1/256 — we out-range them on the fast end deliberately) |
| DELAY 1 / DELAY 2 | ms offset of first/second stereo voice pair |
| DEPTH | LFO modulation of the delay times ("how much pitch warble") |
| FEEDBACK | chorus-voice output back to input ("ringing") |
| LPF/HPF | post-wet one-knob filter; **click the label to toggle LPF↔HPF** |
| MIX / LEVEL | standard |

**Visualizer:** none — teal logo badge only.
**Terrain counter:** `CHORUS-BUILD-BIBLE.md` (7 Types incl. the Juno lineage we already ship per-layer as
`TerrainChorus.h` — "Header-only Juno-60-inspired BBD chorus", TerrainChorus.h:2). Depth axis is ours.

### 2.3 Compressor  *(single-band + hidden OTT + hidden limiter — three devices in one menu)*

| Param | Range/default (screenshot defaults in bold) |
|---|---|
| MODE | SINGLE / MULTIBAND (3-band, "upwards/downwards... an extreme setting" — this IS the OTT lineage) |
| THRESH | 0 % = 0 dB → 100 % = **−120 dB**; default **−18.1 dB** |
| RATIO | up to **Limit** at max — which swaps in "a completely different DSP circuit (a true peak limiter)"; attack becomes 0–10 ms, makeup becomes 0–36 dB; **latency, host-reported only via right-click opt-in**. Default **4:1** |
| ATTACK / RELEASE | ms; defaults **90.1 / 90.1** |
| GAIN | makeup, ~**30 dB** max (36 dB in limiter mode) |
| X-LOW / X-HIGH | multiband crossovers |
| BELOW | ratio **below** threshold = upward compression (the OTT half) |
| H / M / L | per-band output gains (tiny sliders, right of the strip) |
| MIX / LEVEL | parallel compression is one knob (MIX) |

**Visualizer:** none beyond numeric readouts over the knobs and the H/M/L mini-sliders. **No GR meter** —
a compressor with no gain-reduction display, in 2025+.
**BUS-REALITY note (law 1):** Serum's threshold range reaching −120 dB exists precisely because synth-bus
program sits far below mastering levels — the same measurement that gave Terrain the −26 dBFS law. Our
`COMPRESSOR-BUILD-BIBLE.md` §2 states all thresholds relative to −26 dBFS program; keep it.
**Terrain counter:** compressor bible (8 topologies, GR-meter visualizer). We ship the meter they forgot.

### 2.4 Convolve  *(NEW in Serum 2)*

Generic convolution engine, sold as reverb-and-beyond ("blend sounds in unique ways").

| Param | Notes |
|---|---|
| IMPULSE | factory IR menu + ‹ › stepping; **drag-and-drop user IRs**; **Embed in Preset** (IR travels inside the preset file, like their embedded oscillators) |
| SIZE | stretch/contract the IR |
| TONE | filter the IR |
| ϕ MIN | **minimum-phase conversion** — "keeping the frequency response unchanged and eliminating echoing" (turns any IR into a zero-smear EQ print) |
| PRE-DLY · BPM | offset the impulse in time, syncable |
| ATTACK / DECAY | fade-in / shorten the IR envelope |
| DAMP | shorten IR highs over time |
| IR GAIN | impulse volume |
| MIX / LEVEL | standard |

**Visualizer:** the best in the rack — a real **IR waveform display** (green on black) that redraws as
SIZE/ATTACK/DECAY/DAMP reshape the impulse; drag-and-drop target; embed badge top-right. Still **not
audio-reactive** — it shows the IR, never the program.
**Terrain counter:** `ConvolutionReverb.h` ships inside Reverb (type 9 of 9, ParameterIDs.hpp:345) and
`CONVOLUTION-USER-IR-ADDENDUM.md` covers user IRs — ⚠️ under the **NO DISK WRITES / decode-in-memory**
hard rule, and IR-embed-in-preset is exactly the pattern our addendum needs to match. ϕ MIN is the one
feature we should steal outright (minimum-phase IR = "any sound becomes an EQ").

### 2.5 Delay

| Param | Notes |
|---|---|
| MODE | **NORMAL** (independent L/R) / **PING-PONG** (L and R feed each other) / **TAP→DELAY** (mono series: left fires once with no feedback, right is the feedback delay) + **High Quality** option (default on since 2.0) |
| Delay Times | per-side: base time box + a **scalar offset box** below (e.g. 1.1 = 110 %); dragging the scalar snaps and labels **Trip at 1.333 and Dot at 1.5** (manual's own numbers) |
| BPM/MS | sync toggle; **LINK** locks R times to L |
| FEEDBACK | repeat count control |
| FREQ / Q | in-loop filter; **Q here = bandwidth, inverted** — manual admits "technically, this is the opposite of a standard Q control"; max Q = maximum bandwidth/minimum filtering |
| MIX / LEVEL | standard |

**Visualizer:** a purple **filter-curve display** (band-shape between HP and LP skirts) with a **draggable
node** setting FREQ and Q at once; **double-click toggles a real-time frequency (FFT) overlay** under the
curve — one of only two audio-reactive displays in the whole rack. No echo-timeline, no tap dots.
**Terrain counter:** shipped Delay (4 types Digital/Tape/BBD/Diffuse × per-type Characters,
DelayEngine.h:1-8 — one shared fractional line with ping-pong, HQ toggle, in-loop tone, wow/flutter, BBD
companding, diffusion, ducking, M/S width; ParameterIDs.hpp:374-375) + the **echo-timeline visualizer**
(fb312/313) that Serum simply does not have. We hold this ground.

### 2.6 Distortion

Manual (launch): *"13 types of distortion, including two dual-waveshaper modes."* What's New adds
**Overdrive mode + DC bias control** as new-vs-Serum-1; the fb315 research measured the CURRENT menu at
**18 modes**. Known mode families from both manuals + research: Tube (default), Soft/Hard Clip, Diode 1/2,
Lin/Sin Fold, Zero-Square, Downsample, Asym, Rectify, X-Shaper, X-Shaper (Asym), Overdrive (+ post-launch
additions to 18). **No bit-reduction mode at all** (fb315, verified) — Serum spends a slot on the 1-bit
special case (Zero-Square) instead of shipping a bit-depth axis.

| Param | Notes |
|---|---|
| MODE | menu + ‹ › stepping; **Tube** default |
| OFF/PRE/POST | one filter, placeable before or after the shaper |
| TYPE | filter morph slider — **drag a red control to morph LP → BP → HP continuously** |
| FREQ / Q | filter cutoff/res; defaults **425 Hz / 1.9**; v2.0.17 added Key Track via right-click |
| DRIVE | gain boost EXCEPT: Downsample → rate-reduction amount; **X-Shaper modes → the A↔B morph (you cannot push harder into your own curve — fb315's exploited flaw)** |
| MIX / LEVEL | standard |

**Visualizer:** the **transfer-curve display** (orange curve on black) — **drag vertically in the display
to set DRIVE**. X-Shaper opens a pop-up X-Y curve editor (Edit A / Edit B). Static with respect to audio —
no signal occupancy on the curve.
**Terrain counter:** shipped and certified (fb345): 23 modes × 6 families × 8 Characters
(ParameterIDs.hpp:406-407), family-keyed back-8, house drive law `driveDb = D_max·t^0.8`, ADAA budget,
transfer-curve display **with live signal occupancy** (distortion bible §5.8), Morph split from Drive, a
whole DIGITAL family they lack. Width matched, depth beaten — the bible's words stand.

### 2.7 Equalizer

Two-band parametric, each band a **three-state switch**: L band = Low Shelf / Peak / **High Pass**;
R band = High Shelf / Peak / **Low Pass**.

| Param | Defaults (screenshot) |
|---|---|
| FREQ / Q / GAIN (L) | **210 Hz / 0.60 / 0.0 dB** (GAIN dead when type=HP) |
| FILTER TYPE ×2 | icon switches |
| FREQ / Q / GAIN (R) | **2041 Hz / 0.60 / 0.0 dB** (GAIN dead when type=LP) |
| LEVEL | note: **no MIX knob on the EQ** (the one module without it) |

**Visualizer:** a thin **static response-curve line** between the two knob clusters. Not audio-reactive,
not draggable (the manual documents dragging only for Filter/Delay/Distortion displays).
**Terrain counter:** `EQUALIZER-BUILD-BIBLE.md` (7 Types on one chassis) + per-layer `ParametricEQ.h`
already in-tree (IndyFxChain slot 6). An EQ with a live analyzer under the curve (our fb-filter-analyzer
tech, SpectrumAnalyzer.h) beats a static line trivially.

### 2.8 Filter  *(the synth filter as a bus FX — their deepest single module)*

*"Operates identically to the per-voice synth filter... except it runs as a master effect."* One TYPE menu
spanning the full synth filter table (manual pp. 136–140), category by category:

- **Normal:** MG Low 6/12/18/24 (Moog-lineage ladder); SVF Low/High 6/12/18/24; Band/Peak/Notch 12/24 —
  Var knob = **FAT** (resonance-path saturation).
- **Multi:** dual SVFs (LH, LB, LP, LN, HB, HP, HN, BP, BN, PP, PN, NN — Var = **FREQ** of filter 2);
  morphing SVFs LBH/LPH/LNH/BPN (Var = **MORPH**).
- **Flanges:** Comb/Flanger/Phaser each in L / H / HL feedback-filter variants (Var = LP FREQ / HP FREQ /
  **HL WID**); manual note: set MIX 50 % for best results.
- **Misc:** Low/Band/High EQ 6/12 (Var = **DB ±**, morphs HP→shelf at extremes); Ring Mod / Ring Modx2
  (cutoff = carrier; Var = **SPREAD**); SampHold / SampHold− ; Combs/Allpasses/Reverb (Var = **DAMP** —
  yes, a tiny reverb hiding in the filter menu); French LP (nonlinear, Var = **BOEUF** second resonance);
  German LP (ZDF); Add Bass (phase-rotated LP + drive, Var = **THRU**); Formant-I/II/III (cutoff morphs
  vowels, Var = **FORMNT**); Bandreject (Var = **WIDTH**); Dist.Comb 1/2 LP/BP (comb in the pass-filter
  feedback path, Var = **COMBFRQ**); Scream LP/BP (Var = **SCREAM**; drive >50 % engages it).
- **New (Serum 2):** Wsp ("buzzes and burbles", Var = MORPH LPF/Notch/HPF); DJ Mixer (their freeware DJM
  filter); Diffusor (allpass chain, Var = **STAGES**); MG Ladder (clean transistor ladder, Var = **SMOOTH**
  cutoff-slew); Acid Ladder (diode ladder); EMS Ladder; **MG Dirty** (Var = **PAIN** — "how far you're
  holding a lighter from the circuit board (this is physically correct; not a joke)"); PZ SVF (drawable);
  Comb 2 (Var = FRQ2); Exp MM / Exp BPF (expander-module models).

Module params: TYPE · CUTOFF (keytrack toggle; 1 octave MIDI = 1 octave cutoff) · RES · DRIVE (right-click
**Clean Mode**: −24 dB pre / +24 dB post to bypass model saturation) · VAR (label changes per type: FAT /
FREQ / MORPH / … / PAIN / FRO2) · **PAN = stereo cutoff offset** (CCW: L cutoff up + R down; CW: opposite)
· MIX · LEVEL.

**Visualizer:** the best-instrumented display: response curve with **click-drag setting cutoff+res
simultaneously**, right-click menu of three modes — **Frequency Response**, **Frequency Response + live
FFT** (real-time program spectrum under the curve), **Phase Response + FFT**; Option-click cycles modes.
**Terrain counter:** `TerrainFilters.h` + the shipped filter live-analyzer already implement the FR+FFT
archetype (memory: terrain-instrument-filter-live-analyzer). A Filter-as-FX-device for the chain is a
**recycle job, not a build** — the open question is Type-menu curation (§11 Q3). Note their VAR-knob
pattern IS our Character axis flattened onto one continuous knob; our two-axis system is strictly wider.

### 2.9 Flanger

RATE·BPM (0–20 Hz / 8 bars–1/32) · DEPTH · FEEDBACK ("ringing") · **PHASE** (L/R LFO offset; 50 % = 180° —
"the flanger sweep rises on the left while falling on the right") · MIX · LEVEL. **No manual depth**, no
through-zero, no polarity switch.
**Visualizer:** none — ornate pink logo badge only.
**Terrain counter:** `FLANGER-BUILD-BIBLE.md` (6 Types incl. through-zero tape lineage). TZF alone is
night-and-day vs this module.

### 2.10 Hyper/Dimension  *(two effects in one strip — the unison-faker + the widener)*

**HYPER** = "micro-delay chorus with a variable number of voices (1–7)", the official CPU-cheap unison
substitute. RATE (voices oscillate sharp/flat) · **UNISON 1–7, default 4** (0 = Dimension only) · DETUNE ·
**RETRIG** (all voices reset to zero pitch offset at note-on — "a laser-like zap"; the paraphonic FX getting
a per-note behavior back) · MIX · LEVEL.
**DIMENSION** = "pseudo-stereo... **four delay lines summed out-of-phase and slowly amplitude-modulated**"
(the Roland Dimension-D lineage). SIZE ("adds an extra layer of phased delays") · MIX · LEVEL.
**Visualizer:** none — two hazard-stripe logo badges share the strip.
**Terrain counter:** `HYPER-BUILD-BIBLE.md` (7 Types + mono-fold harness). Their RETRIG is the one behavior
to make sure we match (env-gated by note-on, which also satisfies our law 6 for free).

### 2.11 Phaser

RATE·BPM (0–20 Hz / 8 bars–1/32) · **POLES (stepper, default 4)** · DEPTH · **DEPTH 2** ("offset between
phaser stages") · FREQ (base) · FEEDBACK · PHASE (L/R offset, 50 % = 180°) · MIX · LEVEL.
**Visualizer:** none — logo badge only. A phaser with no notch display.
**Terrain counter:** `PHASER-BUILD-BIBLE.md` (9 Types, notch-comb visualizer). 

### 2.12 Reverb  *(5 types; the manual names the lineage!)*

*"A plate and hall reverb, using a modified version of the **Tal Reverb algorithm (courtesy of Togu Audio
Line)**"* — a licensed TAL core, publicly admitted. Types: **PLATE (default) · HALL · VINTAGE · NITROUS ·
BASIN** (Vintage/Nitrous/Basin new in Serum 2 per What's New).

| Type | Params (beyond LO CUT / HI CUT which all share: 0 % = no effect → 100 % = none of that band left) |
|---|---|
| PLATE | SIZE · PRE-DLY · DAMP (HF decay speed) · WIDTH (stereo collapse→max) |
| HALL | SIZE ("reverb time + dimension") · PRE-DLY (ms) · DECAY (ms) · **SPIN RATE / SPIN DEPTH** (LFO modulating time differences — motion) |
| VINTAGE | SIZE · PRE-DLY · **ER SIZE** (early-reflection length) · DECAY · DAMP · **DIFF A / DIFF B** (diffusion + diffusion damping) · **CHORUS (dual value: speed over depth)** |
| NITROUS | SIZE · PRE-DLY · **FEEDBACK** (reverb back into input) · DIFFUSION · **MODE: Space / Marble / Rectangle / Hexagon / Box** · CHORUS (dual) |
| BASIN | SIZE · PRE-DLY · FEEDBACK · CHORUS (dual) |

**Visualizer:** a small green **spectral-tilt graph** whose ends fall as LO/HI CUT rise (numeric boxes
LO 0 / HI 35 beneath); knobs re-populate per type. Not audio-reactive.
**Terrain counter:** shipped Reverb = **9 types × per-type Characters** (ParameterIDs.hpp:345-346: Hall,
Room, Plate, Spring, Digital, Vintage, Basin, Shimmer, Convolution — note we already occupied the "Basin"
name before reading their menu). Spring + Shimmer + Convolution are three whole types Serum lacks. NITROUS's
in-loop FEEDBACK knob is their only wildness — and our loop-gain law (fb306-310) already governs that
territory safely.

### 2.13–2.15 Splitter L/H · Splitter L/M/H · Splitter M/S

Three separate menu entries, one concept: crossover (default **210 Hz**; L/M/H has two SPLIT FREQs) or M/S
encode, one **nested FX rack per lane**, per-lane bypass, MIX/LEVEL on the splitter. Lane UI per §1.4.
**Visualizer:** the lane panels themselves (colored blocks + crossover numerics) — structural, not signal.
No per-lane meters, no crossover curve.
**Terrain counter:** `SPLITTER-BUILD-BIBLE.md` locks OUR answer: **one device, a Mode dropdown** (L/H,
L/M/H, M/S + extensions), fb275-compliant, with live per-lane energy meters. Three menu slots for one idea
is Serum wasting shelf space; we consolidate and out-visualize.

### 2.16 Utility

POLARITY INV (per-channel L/R!) · LPF · HPF · **MONO BASS + FREQ** (mono-below-crossover) · WIDTH · PAN ·
MIX · LEVEL. **Visualizer:** none.
**Terrain counter:** no dedicated bible — deliberately. §5.4 proposes folding Utility duties into the
chain (see §11 Q4): width/pan/polarity/mono-bass are exactly the "lane trim" params the Splitter bible
already carries, and LPF/HPF duplicate the EQ device. A whole rack slot for trim is Serum's answer; ours
should be chain-level plumbing.

---

## 3. Visualizer doctrine — what Serum draws, and the gap we attack

### 3.1 Survey result (eyewitness, all 16 strips)

| Archetype | Modules | What actually reacts |
|---|---|---|
| **Logo badge only** (no display) | Bode, Chorus, Compressor*, Flanger, Hyper/Dimension, Phaser, Utility | nothing (*Compressor has numeric readouts + H/M/L mini-sliders, still no GR meter) |
| **Param-reactive curve** (redraws on knobs, blind to audio) | Equalizer (static line), Reverb (tilt graph), Distortion (transfer curve, drag-to-drive), Convolve (IR waveform, drag-drop + embed badge) | params only |
| **Audio-reactive** (live FFT) | Filter (FR+FFT / Phase+FFT modes), Delay (FFT overlay, **off by default**, double-click to enable) | the only two places the rack ever shows the signal |
| **Structural** | Splitters (lane panels + crossover values) | selection state |

**The honest conclusion:** Serum 2's rack is **~87 % visually dead to audio**. Two FFT overlays (one
opt-in) is the entire live story. Max's hard rule 9 (idle=dim, playing=bright, obvious delta) applied
across ten Terrain devices is, by itself, a visible product-level differentiator no Serum screenshot can
match. Their strength is **direct manipulation** (drag the curve, drag the node, drag the drive) — that we
must match, not just their prettiness.

### 3.2 Proposed concepts for OUR cards (canvas, CPU-cheap, law-9 compliant)

1. **Barber Falls** (the Shift/Bode card, §5.3): a slow vertical waterfall of spectral stripes scrolling at
   the actual shift rate (up, down, or counter-rotating L/R when DIR is centered), stripe brightness driven
   by live band energy from the existing `SpectrumAnalyzer.h` taps; FEED draws each stripe's ghost
   trail — the spiral is literally visible. Idle: near-black drift. Note-on: full-brightness cascade.
   (Canvas, one 128-bin column per frame, re-uses the fb342 push-lane laws — no per-frame filters.)
2. **Chain X-Ray** (the rack header, epic-level): a thin full-width in→out spectrum ribbon where each
   device's segment glows by its measured spectral delta (out/in energy ratio per band); dragging a device
   reorders the actual chain (yellow-line law, matched). Sells the whole chain at a glance — the "different
   world" feel, front and center, audio-reactive where Serum's list view is dead text.
3. **Split Lanes** (Splitter card, extends the Splitter bible §7): 2–3 horizontal lanes with live RMS fill
   and a draggable crossover post; each lane dims when its band is empty — the crossover is HEARD and seen
   moving. (Direct manipulation parity with their panels, plus the meters they don't have.)

---

## 4. Gap analysis — both directions, no flattery

### 4.1 Terrain HAS, Serum 2 LACKS (per-device depth + the modes)

| Terrain asset | Evidence | Serum's nearest answer |
|---|---|---|
| Distortion: 23 modes × 6 families × 8 Characters, drive law, ADAA budget, live-occupancy curve, Morph≠Drive, DIGITAL family | ParameterIDs.hpp:406-407; DISTORTION-BUILD-BIBLE | 13–18 modes, ~5 params, X-Shaper's Drive hijacked by morph, **no bit reduction** |
| Reverb: 9 types × Characters (incl. Spring, Shimmer, Convolution) | ParameterIDs.hpp:345 | 5 types on a licensed TAL core |
| Delay: 4 types × Characters (BBD companding, Diffuse), ducking, echo-timeline viz | DelayEngine.h:1-8 | 1 delay, 3 routings, in-loop filter |
| Tape machine (BPM-synced stereo tape looper) + TapeMachines/TapeProcessor | TapeLoopProcessor.h:6 | nothing |
| Granular engine | GranularEngine.h | nothing (sampler ≠ granular FX) |
| Per-layer independent FX chains (grain→delay→space→tape→june→eq per layer) | IndyFxChain.h:13 | none — their FX are strictly post-sum (self-admitted paraphonic) |
| Four front-page performance modes ARP/CHOP/GLITCH/ROBIN chainable | FlowChain.h, fb105-132 | arp/clip-sequencer (not FX-domain) |
| Dramatic audio-reactive visualizers as a hard rule | fb311 + shipped cards | two FFT overlays total (§3.1) |
| Perceptual certification harnesses per family | Phase G ledger | (not a shipped feature, but our quality moat) |

### 4.2 Serum 2 HAS, Terrain LACKS (the epic's shopping list, priced)

| Serum capability | Status in Terrain | Cost signal |
|---|---|---|
| **Free device ordering, any count, multiple instances** | fb307 ships ONE boolean (SYN_FX_ORDER, ParameterIDs.hpp:435 — Reverb↔Delay swap; index.html:7943/:8323); engines are one-instance members (distortion bible §0) | THE core epic work: N-slot chain + per-slot engine instances |
| **Parallel busses (3 racks) + per-source sends + bus→bus routing** | fb303 main-send only | fb305/fb338 exclusion-sum landmine documented at index.html:6979/:7111 (distortion bible §4.5) |
| **Splitters (band/MS lanes with nested racks)** | SPLITTER-BUILD-BIBLE ready, not built | chain-integration heavy (bible §6 is the hard part) |
| **Bode frequency shifter** | NOTHING — the only uncovered module | new device; proposal in §5.3 |
| Compressor / EQ / Flanger / Phaser / Hyper / Chorus-as-bus-device | bibles ready, not built | per-device builds on the locked chassis |
| Filter-as-FX device | engines exist (TerrainFilters.h) | mostly wiring + Type curation |
| Generic Convolve w/ user IR + embed + ϕ-min | Convolution reverb type + user-IR addendum | ϕ-min + embed are new work |
| **FX rack/module/default presets + rack lock** | 66 synth factory presets exist; no FX-rack preset system | UI + serialization epic-adjacent |
| Mod sources onto FX params (drag-drop breadth) | mod matrix exists for synth; FX params partially | routing table extension (fb260 two-path law applies) |
| Global quality tiering UI (Draft/High/Ultra ×1/2/4, lockable) | per-device Quality dropdowns (distortion §3.8) | unification decision, not DSP |
| Utility module | consciously skipping as a device (§2.16, §11 Q4) | — |
| S1-compat mode, Disable Smoothing | consciously skipping (no legacy engine; smoothing is law 7, non-negotiable) | — |

### 4.3 The competitive frame ($99 vs $249)

Serum 2 is $249 (intro $189, per the v2.0.17 coverage). Terrain at $99 does not need 16 shallow modules;
it needs the **chain architecture** (order/count/parallel/split) carrying its **ten deep devices**, each
with a visualizer that moves. "Fewer, deeper, visibly alive" is a defensible position no Serum screenshot
war can answer — and every deep device is already certified or bibled.

---

## 5. The Terrain FX device roster (the "Types" of this reference — the FX menu we ship)

Law-5 statement at device granularity: every entry is night-and-day distinct, with its discriminator named.
Status: ✅ shipped · 📘 bible ready · 🆕 proposed here · 🔧 recycle decision.

| # | Device | Status | Night-and-day discriminator (measurable) |
|---|---|---|---|
| 1 | Reverb | ✅ (9 types) | RT60 envelope + echo-density signature per type |
| 2 | Delay | ✅ (4 types) | repeat-N spectral decay (BBD companding curve vs tape wow vs diffuse smear) |
| 3 | Distortion | ✅ (23 modes) | family tell — the measured taxonomy proof (distortion bible §1) |
| 4 | Chorus | 📘 (7 types) | taps × LFO answer per type (chorus bible §2) |
| 5 | Compressor | 📘 (8 topologies) | GR-vs-time fingerprint per topology at −26 dBFS program |
| 6 | Equalizer | 📘 (7 types) | curve family + phase behavior per type |
| 7 | Phaser | 📘 (9 types) | notch count/spacing trajectory |
| 8 | Flanger | 📘 (6 types) | TZF null vs positive-comb signature |
| 9 | Hyper | 📘 (7 types) | voice-cloud pitch-spread histogram; mono-fold gate |
| 10 | Splitter | 📘 (one device, Mode dropdown: L/H · L/M/H · M/S + bible modes) | reconstruction null test per mode; absorbs Utility's width/mono-bass/polarity duties as lane trims |
| 11 | **Shift** (Bode answer) | 🆕 §5.3 | shifted-partial offset in Hz constant across notes (ring vs SSB vs barber measurably distinct sideband sets) |
| 12 | Filter | 🔧 (TerrainFilters recycle) | existing filter discriminators; Type curation open (§11 Q3) |

Convolve stays INSIDE Reverb (type 9 + user-IR addendum) unless Max wants the generic-IR device split out
(§11 Q5).

### 5.3 The missing device — **Shift** (our Bode), sketched to fb275

**Why build it:** the only Serum module with zero Terrain coverage; cheap DSP (one Hilbert pair + one
delay line — `MoogDelay.h`/`DelayEngine.h` grammar recycles); massive dramaticism ceiling (barberpole
spirals, metallic ring, Doppler falls); the visualizer (§3.2-1) is a showpiece.

**DSP core:** SSB frequency shift via analytic signal — cascade allpass Hilbert approximation (2×6
biquads, the classic Bode/Moog 90° network; coefficients from the standard dome-filter tables), then
`out = I·cos(2πf_s t) ∓ Q·sin(2πf_s t)` per sideband. Feedback path: shifted signal → delay (4 bars→1/256
synced per law 3) → damping LPF → back to shifter input, loop gain ≤ 0.97 **enforced including the
shifter's 0 dB passband and the damping filter's peak** (law 6 loop-gain law), envelope-gated so the
spiral dies with the note (nothing free-runs). DC/Nyquist images from the Hilbert error stay <−60 dB with
the 6-pole pair; at Ultra add one more biquad pair, never oversample (frequency shifting doesn't expand
bandwidth except the +f_s edge — clamp shift so f + f_s < 0.45·fs, reflect-fold above with a soft guard).

**Types (6, each a lineage):** `Up` (pure SSB up — constant-Hz inharmonic rise) · `Down` (SSB down —
through-zero into negative frequencies = spectral inversion tail) · `Barber` (shift-in-feedback spiral,
the Shepard-tone echo; BALANCE-style up/down loop blend) · `Counter` (L up / R down — auto-widening
dissonance, their DIR-center trick promoted to a Type) · `Ring` (both sidebands, carrier-suppressed AM —
the Bode-adjacent classic; discriminator: symmetric ± sidebands vs SSB's single set) · `Ghost` (small-Hz
shift 0.1–8 Hz + BLUR-style jitter — the phasing/"tape-detune" end).
**Chassis (2 + 8):** Type + Character (per-type voicings, e.g. Barber: Rise/Fall/Bloom…). Back 8:
`Shift` (±2 kHz, cubic taper through a ±20 Hz fine zone) · `Spread` (L/R shift skew) · `Echo` (loop time,
synced) · `Regen` (loop feedback) · `Damp` (loop LPF) · `Drift` (jitter/wow = their BLUR) · `Low Keep`
(mono-bass guard below crossover — the Utility duty absorbed where it's needed) · `Punch` (env-follow amount
riding Shift with input dynamics — params evolve, law 5). Front hero: Shift · Regen · Mix (+ Level trim pill).

---

## 6. Interplay — chain lessons to adopt (and the discipline Serum enforces)

1. **Unity-through:** every Serum module ends in MIX + LEVEL(dB); defaults pass ~unity. Terrain's law:
   device at default = ±0.5 dB through (chord test, fb265). Keep per-device Level as the trim; never bake
   makeup into Mix.
2. **Mix = fully wet at 100 %** on every module (their words each time) — identical to our law 4. Their EQ
   omits MIX; ours keeps the chassis uniform (EQ bible solved this).
3. **Ordering wisdom their design implies:** filters/EQ before distortion for tone-shaping (their PRE/POST
   switch bakes a mini pre-filter INTO distortion — worth copying as our Emphasis already does); splitters
   as outermost containers; Utility/trim last; reverb/delay after dynamics. Their top-to-bottom rack makes
   order visible — our Chain X-Ray (§3.2-2) makes it visible AND audible.
4. **What breaks when stacked (their warts, our laws):** multiple feedback devices in series (Nitrous
   FEEDBACK → Delay → Phaser FEEDBACK) can sum loop gains past unity — our loop-gain law demands each
   device state its max stable loop gain and the CHAIN clamp the product; bus→bus routing must forbid
   cycles (their menu simply omits self-routing); paraphonic env-on-FX retrigger (§1.3) needs a policy
   knob, not silence; splitter lanes must reconstruct to null when empty (SPLITTER bible verify gate).
5. **Latency bookkeeping:** their limiter adds unreported latency unless opted in — a real wart. Our fix
   is the distortion bible §4.4 law: fixed latency, internally compensated, reported as zero.

---

## 7. Factory FX-rack presets (12 chain sketches for the epic's rack-preset system)

Names Title-case, pragmatic (law: name = what it does). Values in device-knob units of the shipped/bibled
devices; all obey unity-through at load.

| # | Rack name | Chain (order) | Intent + rough values |
|---|---|---|---|
| 1 | Wide And Warm | Chorus(Juno, Depth 35) → Reverb(Plate, Mix 22) | default-on "instant synth polish" |
| 2 | Acid Bite | Filter(Acid Ladder, env-modded) → Distortion(Diode/Ge, Drive 55) → Delay(Digital 1/8, FB 35) | the 303 rack; matches their "Acid Dist Delay" factory rack head-on |
| 3 | Big Sky Spiral | Shift(Barber, Shift +3 Hz, Regen 60, synced 1/2) → Reverb(Shimmer, Mix 30) | the device Serum can't answer with a preset |
| 4 | Pump Bus | Compressor(VCA, Thresh −32 dBFS rel. bus, Ratio 4:1, Rel 90 ms) → EQ(HiShelf +2.5 dB) | program at −26 dBFS: threshold stated to OUR bus (law 1), not literature |
| 5 | OTT Lite | Compressor(Multiband Up/Down, Below 3:1, Depth 40) | the multiband upward answer, tamed |
| 6 | Low Mono Punch | Splitter(L/H @120 Hz): Lows[Compressor fast] / Highs[Chorus] + Low Keep mono | their mono-bass Utility duty done in-lane |
| 7 | Tape Postcard | Delay(Tape 1/4 dotted, Wow up, FB 45) → Reverb(Vintage, Damp 60) | the lo-fi echo card |
| 8 | Metal Bell Maker | Shift(Ring, carrier 640 Hz keytracked) → Phaser(12-pole slow) | inharmonic bell from any pad |
| 9 | Vowel Talk | Filter(Formant-II, LFO on Cutoff) → Distortion(Tube, Drive 30) | talky lead |
| 10 | Side Air | Splitter(M/S): Mid[EQ tight] / Side[Reverb Basin, Mix 45] | width without mono-collapse (null-tested) |
| 11 | Broken Console | Distortion(DIGITAL SP-1200 char) → Delay(BBD, FB 55) → Compressor(FET) | the crunch rack Serum's no-bitcrush menu cannot build |
| 12 | Ghost Detune | Shift(Ghost, 1.7 Hz, Spread 30) → Hyper(Dim-style, Size 40) | pseudo-unison width, CPU-cheap (their own doctrine §1.6, out-deviced) |

---

## 8. CPU — their strategy, our budget

**Serum:** global oversampling tiers (Draft 1× / High 2× / Ultra 4×) applied engine-wide + lockable; only
Delay carries a module HQ toggle; Convolve had CPU spikes patched (2.0.17); official doctrine pushes users
from per-voice unison to bus FX (§1.6).
**Terrain (keep + extend):** per-device Quality only where measured audible (distortion bible §3.7-3.8
budget: oversample only what aliases; NEVER oversample delays/reverbs/EQ/utility paths); dst CPU already
−35 % with awake-head sleep (fb342-344) — apply the same control-head sleep law to every chain slot so an
idle device costs ~zero; chain-level budget: 10 devices ≤ the Serum bar for an equivalent rack (their own
guide is our benchmark harness input: one chorus ≈ cheaper than 7-voice unison). Shift device estimate:
Hilbert 24 biquads + 2 osc + 1 delay line ≈ 0.3× the cost of one reverb — no oversampling ever (§5.3).

---

## 9. Pitfalls — collected for the epic (their warts + our known landmines)

1. **Exclusion-sum landmine:** any new bus/send re-breaks fb305 at index.html:6979/:7111 — two exact lines
   (distortion bible §4.5). First edit of the epic.
2. **Locked racks silently drop mods** (their documented behavior §1.2) — if we ship rack-lock, mods must
   survive or the UI must say so BEFORE the preset switch.
3. **Paraphonic retrigger surprise** (§1.3) — env-on-FX needs an explicit trigger policy.
4. **Unreported limiter latency** (opt-in comp) — forbidden by our §6.5 law.
5. **Q-that-isn't-Q** (Delay's inverted bandwidth knob) — pragmatic-names law forbids: call it `Focus`.
6. **Drive hijacking** (X-Shaper morph on the Drive knob) — one knob one meaning; Morph is its own param.
7. **Crossover phase:** splitter lanes must null when reconstructed (LR4 discipline, Splitter bible §3).
8. **Feedback stacking** across chain slots — chain clamps the loop-gain product; every feedback is
   env-gated (law 6); AC-couple every loop (Phase G DC-latch class — 5 dead presets taught us).
9. **Denormal tails** in convolve/reverb/delay slots when the chain idles — flush-to-zero at slot bounds +
   control-head sleep.
10. **DC from shifting/rectifying devices** (Bode at near-0 Hz shift leaks DC images; their Distortion
    added a DC bias control) — DC blocker after any asymmetric/shifting stage (distortion bible §4.1's
    corrected blocker, not the in-tree one).
11. **Type-switch clicks** across a 10-device menu — fade-swap-recover with deferred re-seat is already
    solved (Phase G char-switch machinery); reuse it chain-wide.
12. **Mono-sum collapse** from width devices (Dimension-style out-of-phase summing is *designed* to cancel
    in mono) — mono-fold gate in every width device's harness (Hyper bible §8).

---

## 10. Hard-rule compliance walk (laws 1–10 against this file's recommendations)

1. **Bus reality −26 dBFS:** compressor thresholds and the Shift `Punch` env stated vs −26 dBFS program
   (§2.3, §5.3); no literature ranges copied — Serum's own −120 dB threshold floor is the confirming datum.
2. **Chassis fb275:** every roster device keeps 2 dropdowns + 4×2 back knobs = 11 params; Shift chassis
   mapped (§5.3); Splitter's lane strip stays within its bible's fb275 solution.
3. **Time params 4 bars→1/256:** Shift `Echo`, all chorus/flanger/phaser Rates, delay times — noted where
   Serum stops at 8 bars→1/32 (§2.2); ours spans the house range.
4. **Mix 100 % = fully wet, switches never cut:** matches Serum's own convention (§6.2); type/character
   switches use the Phase G fade-swap machinery (§9.11).
5. **Params evolve / no dead knobs / Types night-and-day:** every roster device carries a measurable
   discriminator (§5 table); Shift Types each name theirs (§5.3); Serum's SHIFT%-of-RANGE two-knob wart
   avoided (one `Shift` knob, cubic taper, no plateau).
6. **Nothing free-runs + loop-gain law:** Shift spiral env-gated, loop gain ≤0.97 counting every in-loop
   stage (§5.3); chain-level product clamp (§9.8).
7. **No clicks:** glide every param; the global-smoothing-disable escape hatch is consciously NOT copied
   (§1.5).
8. **CPU:** §8 — tiering only where aliasing is measured; slot sleep; Shift never oversampled.
9. **Audible ⇒ visible + dramatic:** §3 is the doctrine; three concepts specified; the survey proves the
   competitive gap (87 % of their rack is visually dead to audio).
10. **Recycle first (verified by reading):** SpectrumAnalyzer.h taps for Chain X-Ray; DelayEngine.h line
    for Shift's loop; TerrainFilters.h for the Filter device; TerrainChorus.h Juno core; ConvolutionReverb.h
    + addendum for IR work; SYN_FX_ORDER plumbing (index.html:7943-8323) as the reorder seed; fx-rack-v7
    chassis untouched.

---

## 11. Open questions for Max

1. **Rack architecture scope:** full Serum-style (3 busses + splitters + free order + multiple instances)
   or staged — Stage A: N-slot single chain with drag order + multi-instance; Stage B: splitter lanes;
   Stage C: busses? (fb305 landmine cost lives in Stage C.)
2. **Multiple instances** require engine-per-slot allocation (today: one engine object per device,
   distortion bible §0). Cap at 2 per device type, or unlimited with a CPU meter?
3. **Filter device Type list:** ship all existing synth filter types on the FX bus (Serum's move), or curate
   ~12 with Characters? (Their VAR knob ≈ our Character axis flattened — §2.8.)
4. **Utility:** confirm fold-into-Splitter/chain-trim (§2.16) vs a tiny 4-knob "Tools" strip. My
   recommendation: fold — a trim strip can't meet law 9's drama bar.
5. **Convolve:** keep inside Reverb, or split a generic "Impulse" device with user-IR embed + ϕ-min
   (the two features worth stealing, §2.4)? Embed-in-preset must clear the NO-DISK-WRITES rule.
6. **FX rack presets** (racks + per-module defaults + lock): in the epic, or a later arc?
7. **Shift device (§5.3):** green-light the 6-Type proposal? Name check: `Shift` vs `Bode` (their name is
   an homage but reads as jargon under the pragmatic-names law).
8. **Research debt note:** this session's WebSearch budget was exhausted by the wider workflow (200/200)
   before this task started; findings rest on the OFFICIAL local manual (primary, complete, eyewitness
   images) + targeted fetches (What's New PDF, support KB, changelog coverage). Third-party review color
   (SOS review is HTTP-410) and YouTube-walkthrough visualizer motion (whether their logos animate with
   audio) remain unverified — worth 20 minutes of Max eyeballing Serum 2 live before card mockups lock.

---

## 12. Sources

**Primary (local, complete):**
- Serum 2 User Guide (official, v2.0 build 2025-03-17) — `/Library/Audio/Presets/Xfer Records/Serum 2
  Presets/Serum 2 User Guide.pdf` · FX chapter pp. 152–182 (all module strips read as images) · Mixer
  pp. 144–151 · Filter types pp. 136–141 · Quality p. 319.
- Serum 1 manual (distortion module lineage) — `/Library/Audio/Presets/Xfer Records/Serum
  Presets/Serum_Manual.pdf` p. 25.

**Web (fetched this session):**
- What's New in Serum 2 (official PDF): https://static.xferrecords.com/Serum%202%20What's%20New.pdf
- Serum 2 product page: https://xferrecords.com/products/serum-2
- Xfer support — Serum 2 category: https://support.xferrecords.com/category/45-serum-2
- Xfer support — CPU optimization guidelines:
  https://support.xferrecords.com/article/51-serum2-sound-design-guidelines-for-optimizing-cpu-usage
- Serum 2 v2.0.17 changelog coverage: https://rekkerd.org/xfer-records-updates-serum-2/
- RA.co launch coverage (Bode + three new reverb types + Overdrive; surfaced via search snippet).

**In-tree (read, cited by line):**
- `Source/ParameterIDs.hpp:345-346, 374-375, 406-407, 435` · `Source/IndyFxChain.h:8-13,149`
- `Source/DelayEngine.h:1-8` · `Source/TerrainChorus.h:1-3` · `Source/TapeLoopProcessor.h:5-8`
- `Source/DistortionEngine.h:1-8` · `Source/GranularEngine.h` · `Source/ParametricEQ.h` ·
  `Source/SpectrumAnalyzer.h` · `Source/ui/public/index.html:7943, 7974, 8323, 6979, 7111`
- Sibling bibles listed in the header (each carries its own §Sources with the per-effect DSP literature:
  Bode/Hilbert dome filters, TAL reverb lineage, OTT, Dimension-D, etc.).
