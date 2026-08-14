# Terrain Instrument — Serum 2 FX Complete Reference
## The formal competitive teardown of the Serum 2 effects rack (research file for the MULTI-DEVICE CHAIN epic)

**Date:** 2026-08-14 · **Researcher session:** fb345+ (post Phase G certification)
**AUDITED:** 2026-08-14 (adversarial pass). Every device param table below was re-checked line-by-line
against the extracted text of the local manual; every in-tree citation was re-checked by reading the file.
Corrections are marked **[AUDIT]**; anything that could not be verified is marked **[UNVERIFIED]** and left
in place rather than deleted. Do not quote an [UNVERIFIED] line as fact.

**Primary source:** the OFFICIAL Serum 2 User Guide — found LOCALLY at
`/Library/Audio/Presets/Xfer Records/Serum 2 Presets/Serum 2 User Guide.pdf` (355 pp., build of 2025-03-17;
FX chapter pp. 152–182, Mixer pp. 144–151, filter-type table pp. 136–140, Clean Mode p. 142, Quality p. 319).
Every module strip on pp. 161–182 was **read as an image**, so the visualizer descriptions below are
eyewitness, not paraphrase. Secondary: the official "What's New in Serum 2" PDF (re-fetched and text-extracted
during the audit), Xfer support KB, v2.0.17 changelog coverage.

⚠️ **VERSION REALITY [AUDIT].** The local manual is the **v2.0 launch build**. The Serum 2 actually installed
on this machine is **v2.1.4** (`/Library/Audio/Plug-Ins/VST3/Serum2.vst3/Contents/Resources/moduleinfo.json`
→ `"Version": "2.1.4"`) — so "v2.0.17 coverage" is no longer the current build, and any manual-only claim may
be stale by two minor versions. Where the shipping binary could settle a question it was used directly
(mode/type enums are present verbatim in the executable's string table — see §12). Version deltas are flagged
inline wherever they matter.

**Sibling documents** (per-device depth lives THERE; this file is the cross-cutting map).
**[AUDIT — the draft's sibling list was incomplete and led it to claim two devices were uncovered when
their bibles were sitting in the same folder.] Read `FX-RACK-RESEARCH-INDEX.md` FIRST** — it is the wake-up
document for the whole 16-file sweep and carries the build order. Full roster on disk:
`DISTORTION-BUILD-BIBLE.md` (shipped fb345) · `REVERB-BUILD-BIBLE.md` (shipped) · delay memory arc fb306-310
(shipped) + `DELAY-MOOG-PORT-PLAN.md` · `FX-CHAIN-BIBLE.md` (the chain architecture — **the epic's own
bible**) · `GRANULAR-FX-BUILD-BIBLE.md` · `TAPE-BUILD-BIBLE.md` · `FILTER-BUILD-BIBLE.md` ·
`COMPRESSOR-BUILD-BIBLE.md` · `OTT-BUILD-BIBLE.md` · `EQUALIZER-BUILD-BIBLE.md` · `SPLITTER-BUILD-BIBLE.md` ·
`FLANGER-BUILD-BIBLE.md` · `PHASER-BUILD-BIBLE.md` · `CHORUS-BUILD-BIBLE.md` · `HYPER-BUILD-BIBLE.md` ·
**`BODE-BUILD-BIBLE.md`** · **`UTILITY-BUILD-BIBLE.md`** · `CONVOLUTION-USER-IR-ADDENDUM.md`.

⚠️ **Where this file is SUPERSEDED by a sibling — defer, do not re-decide:**
- **Bode / frequency shifter** → `BODE-BUILD-BIBLE.md` (device named **`Bode`**, 7 Types, range **±5 kHz**,
  built on the **existing in-tree `BodeShifter`**). This file's §5.3 "Shift" sketch predates it and is
  **retained only as the competitive read-out**, not as the spec. See the box in §5.3.
- **Utility** → `UTILITY-BUILD-BIBLE.md` (it IS a device, with Route + Flip dropdowns and the rack's first
  metering surface). This file's §2.16/§11-Q4 "fold it into the chain" recommendation is **superseded**.
- **The chain / slot architecture** → `FX-CHAIN-BIBLE.md` (K = 5 pre-allocated slots × 26 params = 130 new
  params). §4.4-B below states the same law; the sibling owns the numbers.
- **Filter device** → `FILTER-BUILD-BIBLE.md` (9 Types; 94 in-tree filter types to curate from).

---

## 0. Scope — what this file is, and the one-sentence verdict

Max's words: Serum 2's FX chain feels like *"a whole different world."* This file answers **why, precisely,
with the manual open** — a full param inventory of all 16 FX menu entries, a precise description of every
module visualizer (Max bases card mockups on visualizer research), the rack mechanics, and the two-way gap
analysis against what Terrain has already shipped and bibled.

**The one-sentence verdict, honest:** Serum 2's FX world-feel is **architectural, not per-device** — three
parallel racks with arbitrary module order, counts, and nesting, everything modulatable and preset-able —
while every individual device is intentionally **shallow (7–14 params, mostly no live visualizer)**
[AUDIT — the draft said "5–11"; counted off the manual's own control tables the real spread is Flanger 7 at
the low end to Compressor 14 in multiband at the high end]; Terrain beats every Serum device on per-device
depth (Type × Character axes, measured extremity, dramatic visualizers) and loses on chain architecture.

**[AUDIT] Even the Filter — their deepest module — does not reverse this.** Serum's FX filter re-hosts their
whole per-voice table, roughly 50 selectable types across five categories (§2.8). Ours is bigger:
`Source/TerrainFilters.h` is **2,195 lines carrying 94 filter types** behind one `FilterSlot` façade
(94 = the count of `filterTypeChoices.add(...)` calls in `PluginProcessor.cpp`; corroborated by
`FILTER-BUILD-BIBLE.md`). The Filter-as-FX device is therefore a **hosting-and-motion job, not a DSP
project** — which is exactly what the Filter bible concludes. The epic's job is to steal the architecture
without giving up the depth.

---

## 0.5 History / lineage — how this rack got here (and whose ideas are in it)

Required context: almost nothing in Serum's FX rack is original DSP. The rack is a *curation* of known
lineages wrapped in a very good chassis. Knowing the lineage tells us which literature our own bibles must
answer, and which "Serum feature" is actually a 1970s box.

| Serum 2 module | Lineage it implements | Our bible's answer to the same lineage |
|---|---|---|
| Bode *(new in 2)* | Harald Bode's frequency shifter (Bode/Moog 1621 ring-modulator/shifter, late 1960s) — analytic-signal SSB shift; the feedback-around-the-shifter topology is the barberpole/spiral echo published by Bode and reused in Eventide/Doppler designs | `BODE-BUILD-BIBLE.md` + the in-tree `BodeShifter` (`TerrainFilters.h:1127`) |
| Chorus | Juno-60 / BBD-chorus (two taps per side, LFO on delay time) | `CHORUS-BUILD-BIBLE.md` + `TerrainChorus.h:2` "Header-only Juno-60-inspired BBD chorus" |
| Compressor (Multiband, upward+downward) | the OTT/"Xfer OTT" preset of Ableton's Multiband Dynamics — Xfer's own freeware, folded back in as a MODE | `OTT-BUILD-BIBLE.md` + `COMPRESSOR-BUILD-BIBLE.md` |
| Convolve *(new in 2)* | classic partitioned convolution; the ϕ MIN control is minimum-phase IR conversion (cepstral / Hilbert-fold) | `ConvolutionReverb.h` + `CONVOLUTION-USER-IR-ADDENDUM.md` |
| Delay | standard stereo/ping-pong/tap topology with in-loop filter | `DelayEngine.h` (4 characters) |
| Distortion | Tube/diode/fold/rectify waveshaper canon + Serum 1's dual-waveshaper "X-Shaper" | `DISTORTION-BUILD-BIBLE.md` (shipped fb345) |
| Filter | Serum's own per-voice filter table re-hosted as a bus FX (Moog ladder, SVF, EMS, diode/acid ladder, formant, Xfer's freeware DJM filter) | `TerrainFilters.h` — **2,195 lines / 94 types** (wider than theirs) + `FILTER-BUILD-BIBLE.md` |
| Hyper/Dimension | Roland Dimension-D (four out-of-phase, slowly AM'd delay lines) + the micro-detune "hyper unison" trick | `HYPER-BUILD-BIBLE.md` |
| Reverb | **licensed**: "a modified version of the Tal Reverb algorithm (courtesy of Togu Audio Line)" — manual p. 175, verbatim | `REVERB-BUILD-BIBLE.md` (9 types, all in-house) |
| Splitters | Ableton-style band/MS containers with nested device chains | `SPLITTER-BUILD-BIBLE.md` |
| Utility *(new in 2)* [AUDIT] | mixer-strip trim (polarity/width/pan/mono-bass), the "Utility"/"Sub Bass mono" idiom | folded into chain trim + Splitter lanes (§2.16) |

**The lineage lesson:** Serum 2's FX rack is Serum 1's five effects (Chorus/Delay/Distortion/Filter/Reverb,
plus Compressor/EQ/Flanger/Hyper/Phaser) re-chassised into an orderable rack, plus **four genuinely new
modules — Bode, Convolve, Utility, and the three Splitters** (What's New pp. 12–13, verbatim headings:
"Bode — New frequency shifter effect", "Convolve — New convolution effect", "Utility — New utility effect").
The competitive gap they closed in v2 was *architecture*; the DSP they added was a shifter, a convolver,
and a trim strip. That is exactly the shape of the epic in front of us.

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
  territory where **fb305/fb338's exclusion-sum landmine** lives on our side.
  **[AUDIT — the citation this file shipped with was WRONG.]** The exclusion sums are **not** in
  `index.html` and there are **three** of them, not two. Verified by reading the tree:
  `Source/PluginProcessor.cpp:7159` (reverb block), **`:7326`** (distortion / per-osc routing block) and
  **`:7358`** (delay block) — each computes
  `rtdL = ((rvbSendL?…:0) + (dlySendL?…:0) + (dstSendL?…:0)) * outputGain * kVoiceToFxPad;`
  and subtracts it from the main send. The comment on every one of those lines states the law itself:
  *"fb338 — the fb305 law: EVERY send bus joins EVERY main-send exclusion."* Any Terrain bus expansion edits
  **all three** sites or the new bus double-counts. (`index.html:6979`/`:7111` are unrelated UI markup —
  a robin SVG and the ribbon-row CSS.)

### 1.2 Module lifecycle (all p. 155–158, verified)

| Operation | Mechanics |
|---|---|
| Add | `+` button or right-click → Add FX Module; signal flows **top to bottom**; new module goes live in the chain instantly |
| Reorder | drag in list view (left) or rack view (right); a **yellow line** shows the landing slot |
| Duplicate | **Option-drag** copies WITHOUT modulations; **Shift-Option-drag** copies WITH modulations; also module-menu → Duplicate FX Module. **Multiple instances of the same processor are first-class** — yes, two delays, three distortions |
| Bypass | per-module button (red when bypassed) in list AND rack view; **Option-click = bypass ALL FX on the bus**; explicitly framed as a temporary A/B tool, "without having to set the MIX knob to 100% dry" |
| Remove | ✕ in list view |
| Rack presets | per-rack Save/Load FX Bus (the manual's operations table lists **Add FX Module · Cut / Copy / Paste FX Bus** (move whole chains between busses) **· Clear FX Bus · Lock FX Bus / Lock All FX Busses** (rack survives preset changes — but *"modulation assignments to module parameters in the locked rack are cleared when changing presets"*) **· Load FX Bus · Save FX Bus**). An "Init" entry is **[UNVERIFIED]** — it is not in the manual's operations table |
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

**[AUDIT] Confidence key for this section.** Every *param name, function and range* below was re-verified
against the manual's extracted text and **all 16 inventories hold**. But every **numeric default in bold**
(compressor −18.1 dB / 4:1 / 90.1 ms; EQ 210 Hz / 0.60 / 2041 Hz; distortion 425 Hz / 1.9; phaser POLES 4;
hyper UNISON 4; splitter 210 Hz) was read off a **screenshot**, not from text — no source states them.
Treat all bold defaults as **[UNVERIFIED]**: fine for "what ballpark did Xfer choose", never quotable as
spec, and never a reason to move one of our own calibrated defaults.

### 2.1 Bode  *(NEW in Serum 2)*
> ⚠️ **[AUDIT] The draft's subtitle — "the one device with NO Terrain bible yet" — was FALSE.**
> `BODE-BUILD-BIBLE.md` exists in this folder (written the same day, already adversarially audited), and
> the **DSP already exists in the tree**: `Source/TerrainFilters.h:1110` (`struct BodeAP`) and `:1127`
> (`struct BodeShifter`) implement the Niemitalo quadrature network, the recursive quadrature oscillator
> with renorm, and a DC-blocked feedback tap. Bode is not our least-covered device — it may be our
> **cheapest** one. Everything below is the competitive read of *Serum's* module; build from the bible.

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
**Terrain counter:** `BODE-BUILD-BIBLE.md` — device **`Bode`**, 7 Types, ±5 kHz range (the Moog 1630's, not
Echobode's ±20 k: past ~5 kHz it is all foldover chatter on synth program), built on the in-tree
`BodeShifter`, plus a barberpole visualizer Serum doesn't have. [AUDIT — the draft called this "the only
Serum module with zero Terrain coverage"; that was wrong on both counts, bible *and* engine.]

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
**18 modes**.

**[AUDIT — fb315's count is CONFIRMED and the roster is now exact, not inferred.]** The shipping v2.1.4
binary carries the FX distortion enum verbatim in its string table:

```
kTube = 0, kSoftClip, kHardClip, kDiode1, kDiode2, kLinFold, kSinFold, kZeroSquare,
kDownsample, kAsym, kRectify, kXShaper, kXShaperAsym, kSineShaper, kStompBox,
kTapeSat, kOverdrive, kSoftSat, kNumDistortions
```

That is **18 modes in menu order**: Tube · Soft Clip · Hard Clip · Diode 1 · Diode 2 · Lin Fold · Sin Fold ·
Zero-Square · Downsample · Asym · Rectify · X-Shaper · X-Shaper (Asym) · **Sine Shaper · Stomp Box · Tape
Sat. · Overdrive · Soft Sat.** The last five were *missing or guessed* in this file's first draft — the four
in bold are post-launch additions the v2.0 manual never documents. (Note Serum keeps a **separate, shorter**
distortion list for the oscillator warp path — `kDistTube…kDistSoftSat`, which has **no** Downsample and
**no** X-Shaper. Do not conflate the two lists; they are different enums in the same binary.)

**No bit-reduction mode at all** (fb315, re-confirmed against the enum above) — Serum spends a slot on the
1-bit special case (Zero-Square) and one on sample-**rate** reduction (Downsample), but ships **no bit-depth
axis**. Our DIGITAL family remains uncontested.

**DC bias [UNVERIFIED form].** What's New lists "Distortion — New Overdrive mode and DC bias control" as a
v2 feature, but the v2.0 manual's Distortion control table does not contain a bias row and the word "bias"
does not appear anywhere in the FX chapter. Whether it is a knob, a right-click option, or display-only
could not be determined from the manual or the binary's strings. Treat the *existence* as sourced and the
*form* as unknown.

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

*(Menu names below are the manual's verbatim strings, corrected in the audit — the first draft paraphrased
several and got two Var labels wrong.)*

- **Normal:** MG Low 6/12/18/24 (Moog-lineage ladder); **Low 6/12/18/24** and **High 6/12/18/24** (these are
  the state-variable LP/HP — the menu does *not* prefix them "SVF" [AUDIT]); Band/Peak/Notch 12/24 —
  Var knob = **FAT** (resonance-path saturation).
- **Multi:** dual SVFs (LH, LB, LP, LN, HB, HP, HN, BP, BN, PP, PN, NN — "first letter is primary, second is
  secondary"; resonance applies to BOTH; Var = **FREQ** of filter 2); morphing SVFs LBH/LPH/LNH/BPN
  (Var = **MORPH**).
- **Flanges:** Cmb/Flg/Phs each in **L / H / HL** feedback-filter variants (Var = LP FREQ / HP FREQ /
  **HL WID**); manual note: set MIX 50 % for best results.
- **Misc:** Low/Band/High EQ 6/12 (Var = **DB +/−**, morphs HP→shelf at extremes; resonance does nothing on
  the 6 dB variants); Ring Mod / Ring Modx2 (cutoff = carrier; Var = **SPREAD**, and the manual is explicit
  that SPREAD exists **only on the x2 variant** [AUDIT] — plain Ring Mod has no Var); SampHold / SampHold−
  (Var = **N/A**); Combs/Allpasses/Reverb (Var = **DAMP** — yes, a tiny reverb hiding in the filter menu);
  French LP (nonlinear, Var = **BOEUF** second resonance); German LP (ZDF, Var = **N/A**); Add Bass
  (phase-rotated LP + drive, Var = **THRU**); Formant-I/II/III (cutoff morphs vowels, Var = **FORMNT**);
  Bandreject (Var = **WIDTH**); Dist.Comb 1/2 LP/BP (comb in the pass-filter feedback path — v1 positive
  feedback, v2 negative; Var = **COMBFRQ**); Scream LP/BP (Var = **SCREAM**; drive >50 % engages it).
- **New (Serum 2):** Wsp ("buzzes and burbles", Var = **MORPH** LPF/Notch/HPF); DJ Mixer (their freeware DJM
  filter, Var = **N/A**); Diffusor (allpass chain, Var = **STAGES**); MG Ladder (clean transistor ladder,
  Var = **SMOOTH** cutoff-slew); **Acid Ladder** (diode ladder, Var = **SMOOTH** [AUDIT — the first draft
  listed no Var]); **EMS Ladder** (Var = **SMOOTH** [AUDIT]); **MG Dirty** (Var = **PAIN** — "how far you're
  holding a lighter from the circuit board (this is physically correct; not a joke)"); **PZ SVF** (drawable,
  Var = **SMOOTH** [AUDIT]); Comb 2 (Var = **FRQ2**); **Exp MM** (expander-module multimode, Var = **MIX**,
  blends LPF/Notch/HPF [AUDIT]) / **Exp BPF** (Var = **N/A**).

Module params: TYPE · CUTOFF (keytrack toggle; 1 octave MIDI = 1 octave cutoff) · RES · DRIVE (right-click
**Clean Mode**, manual p. 142: stages the filter input **−24 dB with a +24 dB post-filter boost**, reducing
saturation in the models) · VAR (label changes per type: FAT / FREQ / MORPH / LP FRQ / HP FRQ / HL WID /
DB +/− / SPREAD / DAMP / BOEUF / THRU / FORMNT / WIDTH / COMBFRQ / SCREAM / STAGES / SMOOTH / PAIN /
**FRQ2** — [AUDIT] the first draft wrote "FRO2"; the manual's type table reads **FRQ2**, and the "FRO"
spellings that appear in the PDF's control list are a font/text-layer artefact of the same "FRQ" glyphs) ·
**PAN = stereo cutoff offset** (default 50 % = no effect; CCW: L cutoff up + R down; CW: opposite)
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

### 2.16 Utility  *(NEW in Serum 2 [AUDIT] — What's New p. 12: "Utility — New utility effect")*

POLARITY INV (per-channel L/R!) · LPF · HPF · **MONO BASS + FREQ** (mono-below-crossover) · WIDTH ·
PAN (stereo balance) · MIX · LEVEL. **Visualizer:** none.
**Terrain counter:** ⚠️ **[AUDIT] `UTILITY-BUILD-BIBLE.md` EXISTS** — the draft's "no dedicated bible —
deliberately" was false, and its "fold the duties into chain plumbing" recommendation is **superseded**.
The bible ships Utility as a real device: dropdowns **Route** and **Flip** (both discrete and
sound-changing — "no fake Types, law 5 cuts both ways; a utility that *colors* is a broken utility"),
gain/width/bass-mono, and **the rack's first real metering surface** (which is how it satisfies law 9
without pretending to be dramatic). Note also that the draft's §5.4 cross-reference is dangling — there is
no §5.4 in this file.

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
across our SIXTEEN devices (§5 [AUDIT] — the draft said ten) is, by itself, a visible product-level differentiator no Serum screenshot can
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
| Distortion: 23 modes × 6 families × 8 Characters, drive law, ADAA budget, live-occupancy curve, Morph≠Drive, DIGITAL family | ParameterIDs.hpp:406-407; DISTORTION-BUILD-BIBLE | **18 modes** (exact, from the v2.1.4 binary enum — §2.6 [AUDIT]; the manual's 13 is the v2.0 launch count), **8 params**, no Character axis, X-Shaper's Drive hijacked by morph, **no bit reduction** |
| Reverb: 9 types × Characters (incl. Spring, Shimmer, Convolution) | ParameterIDs.hpp:345 | 5 types on a licensed TAL core |
| Delay: 4 types × Characters (BBD companding, Diffuse), ducking, echo-timeline viz | DelayEngine.h:1-8 | 1 delay, 3 routings, in-loop filter |
| Tape machine (BPM-synced stereo tape looper) + TapeMachines/TapeProcessor | TapeLoopProcessor.h:8 [AUDIT — was cited as :6] | nothing |
| Granular engine | GranularEngine.h | nothing (sampler ≠ granular FX) |
| Per-layer independent FX chains (grain→delay→space→tape→june→eq per layer) | IndyFxChain.h:13 | none — their FX are strictly post-sum (self-admitted paraphonic) |
| Four front-page performance modes ARP/CHOP/GLITCH/ROBIN chainable | FlowChain.h, fb105-132 | arp/clip-sequencer (not FX-domain) |
| Dramatic audio-reactive visualizers as a hard rule | fb311 + shipped cards | two FFT overlays total (§3.1) |
| Perceptual certification harnesses per family | Phase G ledger | (not a shipped feature, but our quality moat) |

### 4.2 Serum 2 HAS, Terrain LACKS (the epic's shopping list, priced)

| Serum capability | Status in Terrain | Cost signal |
|---|---|---|
| **Free device ordering, any count, multiple instances** | **[AUDIT — this row was stale.]** fb307's boolean was replaced in **fb341**: `SYN_FX_ORDER` is now an `AudioParameterChoice` with the **6 permutations of THREE devices** (`PluginProcessor.cpp:3488-3496`, strings "Reverb > Delay > Distortion" … "Delay > Reverb > Distortion"; read as an index at `:5860` into `fxPerm_`; UI at `index.html:7943/:7974/:8323`, which writes `pi/5`). ⚠️ `ParameterIDs.hpp:435`'s comment still says "bool" and is itself stale. Engines remain one-instance members (distortion bible §0) | THE core epic work: N-slot chain + per-slot engine instances. **Note the cardinality trap (§4.4-C): a 6-choice param CANNOT grow to 4 devices (24 orders) in place — that needs a new, final-sized param, or an order representation that is not a choice list at all.** |
| **Parallel busses (3 racks) + per-source sends + bus→bus routing** | fb303 main-send only | fb305/fb338 exclusion-sum landmine at **`PluginProcessor.cpp:7159`/`:7326`/`:7358`** (three sites — see §1.1 [AUDIT]; distortion bible §4.5) |
| **Splitters (band/MS lanes with nested racks)** | SPLITTER-BUILD-BIBLE ready, not built | chain-integration heavy (bible §6 is the hard part) |
| **Bode frequency shifter** | [AUDIT — row was wrong] bible AND engine both exist: `BODE-BUILD-BIBLE.md` + `TerrainFilters.h:1110/:1127` (`BodeAP`/`BodeShifter`) | device build on the fb275 chassis; the SSB core is already written and certified as a filter type |
| Compressor / EQ / Flanger / Phaser / Hyper / Chorus-as-bus-device | bibles ready, not built | per-device builds on the locked chassis |
| Filter-as-FX device | engines exist — [AUDIT] `TerrainFilters.h` is **2,195 lines / 94 types** behind one `FilterSlot` façade (verified: 94 `filterTypeChoices.add` calls in `PluginProcessor.cpp`), i.e. **wider than their ~50** | wiring + Type curation + the env-follow/key-track motion block. `FILTER-BUILD-BIBLE.md` (9 Types) already specs it — this row is not a gap, it is a hosting job |
| Generic Convolve w/ user IR + embed + ϕ-min | Convolution reverb type + user-IR addendum | ϕ-min + embed are new work |
| **FX rack/module/default presets + rack lock** | 66 synth factory presets exist; no FX-rack preset system | UI + serialization epic-adjacent |
| Mod sources onto FX params (drag-drop breadth) | mod matrix exists for synth; FX params partially | routing table extension (fb260 two-path law applies) |
| Global quality tiering UI (Draft/High/Ultra ×1/2/4, lockable) | per-device Quality dropdowns (distortion §3.8) | unification decision, not DSP |
| Utility module | [AUDIT — row was wrong] `UTILITY-BUILD-BIBLE.md` exists and specs it as a REAL device (Route + Flip dropdowns, the rack's first metering surface) | device build; also the chain's gain-staging joint |
| S1-compat mode, Disable Smoothing | consciously skipping (no legacy engine; smoothing is law 7, non-negotiable) | — |

### 4.3 The competitive frame ($99 vs $249)

**[AUDIT — pricing re-verified live on the Xfer product page, 2026-08-14.]** Serum 2 is **$249.00 USD**
today. The **$189 intro price EXPIRED on 1 June 2025** — quoting it as current pricing is wrong. And the
number that actually matters competitively was missing from this file: the product page states
**"Serum 2 is a free upgrade for Serum 1 owners"** plus **"Lifetime free updates."** So the realistic
competitive frame is not "$99 vs $249" — it is **"$99 vs $0 for the enormous installed base of Serum 1
owners, $249 for everyone else."**

That sharpens, not weakens, the strategy. Against a free upgrade you cannot win on module count; you win on
what the incumbent's rack demonstrably does not do. Terrain at $99 does not need 16 shallow modules; it
needs the **chain architecture** (order/count/parallel/split) carrying its **sixteen deep devices** (§5 [AUDIT] — the draft undercounted at ten), each with a
visualizer that moves. "Fewer, deeper, visibly alive" is the only defensible position — 14 of Serum's 16
strips are visually dead to audio (§3.1) and every one of our deep devices is already certified or bibled.

### 4.4 RACK-WIDE ARCHITECTURAL LAWS — the constraints the epic cannot design around

**[AUDIT — this section was entirely missing, and §11's open questions were written as if these constraints
did not exist. They are hard limits of the host formats and of our own signal topology, not preferences.]**

**A. ZERO LOOKAHEAD ANYWHERE IN THE RACK.** The fb305/fb338 main-send exclusion sums subtract the routed dry
**sample-aligned** (`PluginProcessor.cpp:7159/:7326/:7358` — a plain per-sample subtract, no delay
compensation on the dry path). The instant any device reports latency, the dry it is supposed to cancel
leaks back *phase-smeared* instead of nulling. Consequences, stated so nobody re-proposes them:
- **No lookahead limiting.** The Compressor device's Limit topology must be a zero-lookahead true-peak
  design (or a soft-knee clipper), never Serum's opt-in-latency limiter — §6.5 and §9.4 already call their
  behaviour a wart; this is *why* we cannot copy it even with the opt-in.
- **No linear-phase EQ option** in the Equalizer device, and no linear-phase crossovers in the Splitter
  (LR4 minimum-phase only — which the Splitter bible §3 already assumes).
- **No FFT/spectral device** with a frame delay in the rack path.
- Any internal latency a device genuinely needs (e.g. a halfband oversampling FIR) must be **internally
  compensated and reported as zero** — the distortion bible §4.4 law, which stands.

**B. PARAMETERS CANNOT BE CREATED AT RUNTIME.** JUCE/VST3/AU cache the parameter list at construction; the
host builds automation lanes from it once. "Add a device to the chain" therefore **cannot** allocate that
device's 11 params on demand. The epic's chain must be a **PRE-ALLOCATED SLOT POOL**: N slots × the full
fb275 param set (2 dropdowns + 8 knobs + Mix + hero knobs), created at construction, with a per-slot
"device type" choice that *routes* those params to whichever engine occupies the slot. Anything in §11 Q1/Q2
that reads as "unlimited instances" resolves to "N is chosen once, at birth, and never changes." Pick N with
the automation-lane cost in mind (N × the per-slot param set × the host's lane UI), not with the CPU cost —
CPU is handled by slot sleep (§8), lanes are not. **`FX-CHAIN-BIBLE.md` owns this decision and has already
costed it: K = 5 slots × 26 params = 130 new params.** This file states the law; the chain bible states the
number. (Corroboration that the law is real and independently derived: the chain bible's own headline find
is *"JUCE cannot create parameters at runtime — the `+` button can therefore never create a device, only
claim a pre-allocated slot."*)

**C. CHOICE-PARAM CARDINALITY IS FIXED AT BIRTH (fb342 law).** A `juce::AudioParameterChoice`'s list length
is frozen when it is constructed; growing it later silently re-maps every saved preset's normalised value.
Two direct consequences for this file's own proposals:
- The **per-slot device-type dropdown must be sized for the FINAL roster on day one** — all 12 entries of
  §5 (Reverb, Delay, Distortion, Chorus, Compressor, Equalizer, Phaser, Flanger, Hyper, Splitter, Shift,
  Filter) plus deliberate headroom, with not-yet-built entries shipped **disabled**. Shipping "the three we
  have now and we'll extend it" is the defect.
- **`SYN_FX_ORDER` cannot grow in place.** It is a 6-entry choice (3 devices). Four devices is 24 orders,
  five is 120 — a choice list is the wrong representation past three. The N-slot pool makes this moot
  (order = which engine sits in which slot), which is another reason B and C must be decided together,
  before any device build.

**D. EVERY NEW SEND BUS JOINS ALL THREE EXCLUSION SUMS.** Restated here because it is a rack law, not a
distortion note: `PluginProcessor.cpp:7159`, `:7326`, `:7358`. Miss one and that bus's dry is counted twice.
The verify gate is a null test: route one osc to the new bus, set its device Mix to 0, and confirm the
output is bit-identical to the un-routed render.

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
| 11 | **Bode** (freq shifter) | 📘 `BODE-BUILD-BIBLE.md` (7 Types) [AUDIT — was listed as 🆕 "Shift", §5.3; that sketch is superseded] | shifted-partial offset in Hz constant across notes (SSB-up vs SSB-down vs barberpole vs ring = distinct sideband sets). ⚠️ the bible carries its own law-5 audit box on its 7-Type roster — settle it there, not here |
| 12 | Filter | 📘 `FILTER-BUILD-BIBLE.md` (9 Types) + `TerrainFilters.h` recycle (94 in-tree types) | existing filter discriminators + the env-follow/key-track motion block; Type curation open (§11 Q3) |
| 13 | **Utility** | 📘 `UTILITY-BUILD-BIBLE.md` [AUDIT — was omitted entirely] | Route × Flip are discrete and sound-changing (no fake Types); reconstruction/null and correlation metering are its measurable surface |
| 14 | **Granular** | 📘 `GRANULAR-FX-BUILD-BIBLE.md` (8 Types) [AUDIT — omitted] | grain-cloud density/size/scatter signature per type; **the headline differentiator — Serum has no answer at all** |
| 15 | **Tape** (the machine) | 📘 `TAPE-BUILD-BIBLE.md` (7 Types) [AUDIT — omitted] | wow/flutter spectrum + head-gap HF loss per machine; boundary law: Distortion owns the *magnetisation*, this owns the *machine* |
| 16 | **OTT** | 📘 `OTT-BUILD-BIBLE.md` (8 Types) [AUDIT — omitted] | 3-band up+down GR fingerprint; distinct from Compressor (fixed opinionated instrument) and from Splitter (routing) — Serum ships both too |

Convolve stays INSIDE Reverb (type 9 + user-IR addendum) unless Max wants the generic-IR device split out
(§11 Q5).

⚠️ **[AUDIT] This roster shipped with 12 entries and was missing four devices that already have bibles**
(Utility, Granular, Tape, OTT). That mattered beyond bookkeeping: rack-law C (§4.4) freezes the device-type
dropdown length **at birth**, so a roster that undercounts by four produces a dropdown that can never hold
the real product. Size the choice list against `FX-RACK-RESEARCH-INDEX.md`'s full 16-file sweep — plus
headroom — not against this table.

### 5.3 ~~The missing device — **Shift**~~ → **SUPERSEDED by `BODE-BUILD-BIBLE.md`**

> 🛑 **[AUDIT] DO NOT BUILD FROM THIS SECTION.** It was written as if no Bode bible and no in-tree shifter
> existed; both do. The sibling bible wins on every point it covers:
>
> | | This sketch (superseded) | `BODE-BUILD-BIBLE.md` (authoritative) |
> |---|---|---|
> | Device name | `Shift` | **`Bode`** (and §11 Q7's name question is already settled there) |
> | Types | 6 (2 of which fail law 5 — see below) | 7, with its own law-5 audit box at its §4.0 |
> | Shift range | ±2 kHz | **±5 kHz** (the Moog 1630's; past ~5 k it is foldover chatter on synth program) |
> | DSP origin | "one Hilbert pair + one delay line, recycles DelayEngine grammar" | **the shifter is ALREADY IN THE TREE** — `TerrainFilters.h:1110` `BodeAP`, `:1127` `BodeShifter` (Niemitalo network, quadrature osc with renorm, DC-blocked feedback tap) |
> | Known traps | none listed | three carry-over traps in `setParams` (`TerrainFilters.h:1145-1157`): shift derived from a *log* cutoff param, `FMAX=1000` clamped to ±2000 Hz, and `fb = 0.95·res01` with **no sideband de-rate and no envelope gate** (law 6) |
>
> The sketch violated law 10 (recycle first, **verified by reading code**) by proposing to build a Hilbert
> pair that was already written and certified as a filter type. That is the lesson worth keeping.
>
> **What survives from this section:** the competitive framing (Serum's SHIFT-%-of-RANGE two-knob wart, and
> the fact that their most disorienting module has no display at all), the **Barber Falls** visualizer
> concept (§3.2-1), and the chassis/law-5 critique below — which applies to any 6+ Type shifter roster,
> including the bible's 7.

**Why it is worth building (still true):** massive dramaticism ceiling (barberpole spirals, metallic ring,
Doppler falls); the visualizer (§3.2-1) is a showpiece; and the DSP is already paid for.

**DSP core:** SSB frequency shift via analytic signal — cascade allpass Hilbert approximation (2×6
biquads, the classic Bode/Moog 90° network; coefficients from the standard dome-filter tables), then
`out = I·cos(2πf_s t) ∓ Q·sin(2πf_s t)` per sideband. Feedback path: shifted signal → delay (4 bars→1/256
synced per law 3) → damping LPF → back to shifter input, loop gain ≤ 0.97 **enforced including the
shifter's 0 dB passband and the damping filter's peak** (law 6 loop-gain law), envelope-gated so the
spiral dies with the note (nothing free-runs). DC/Nyquist images from the Hilbert error stay <−60 dB with
the 6-pole pair; at Ultra add one more biquad pair, never oversample (frequency shifting doesn't expand
bandwidth except the +f_s edge — clamp shift so f + f_s < 0.45·fs, reflect-fold above with a soft guard).

**Types (6 proposed, each a lineage):** `Up` (pure SSB up — constant-Hz inharmonic rise) · `Down` (SSB down
— through-zero into negative frequencies = spectral inversion tail) · `Barber` (shift-in-feedback spiral,
the Shepard-tone echo; BALANCE-style up/down loop blend) · `Counter` (L up / R down — auto-widening
dissonance, their DIR-center trick promoted to a Type) · `Ring` (both sidebands, carrier-suppressed AM —
the Bode-adjacent classic; discriminator: symmetric ± sidebands vs SSB's single set) · `Ghost` (small-Hz
shift 0.1–8 Hz + BLUR-style jitter — the phasing/"tape-detune" end).

⚠️ **[AUDIT — law 5 cuts both ways, and two of these six look like padding.]** Law 5 forbids inventing Types
to fill a dropdown as firmly as it forbids dead knobs. On this proposal's own chassis:
- **`Counter` is `Up` with `Spread` at maximum.** L-up/R-down is a *setting* of the L/R shift-skew knob that
  is already in the back 8, not a different DSP topology.
- **`Ghost` is `Up` at a small `Shift` value with `Drift` raised.** Both of those knobs are also in the
  back 8.
Neither survives a night-and-day discriminator test against `Up` (there is no measurable feature present in
one and absent in the other — only a knob position). **Either cut them to a 4-Type roster
(`Up · Down · Barber · Ring` — which genuinely differ: single upper sideband / single lower sideband /
recirculating shift / symmetric double sideband, each with a distinct measurable sideband set), or promote
them to Characters of `Up`.** Do NOT ship six Types where four are real — and note that under rack-law C
(§4.4) the Type list's LENGTH is frozen at birth, so this must be settled before the param is created, not
after. Recommended: **ship the choice list sized for 6, with `Counter`/`Ghost` disabled**, so the roster can
still grow if a real topology is found for those slots.

**Chassis (fb275) [AUDIT — the draft's mapping was malformed].** The shipped chassis, read off the delay
(`ParameterIDs.hpp:374-390`), is: **2 dropdowns (Type, Character) + 3 FRONT hero knobs + MIX + 8 BACK knobs
(4×2)** — and the front three are **distinct** params, never repeats of the back eight (delay's front is
Time/Feedback/Tone; its back eight are LowCut/HiCut/Spread/Width/ModRate/ModDepth/Wow/Duck). The draft
listed only **two** front heroes and duplicated both of them (`Shift`, `Regen`) into the back 8. Corrected
mapping:
- **Dropdowns:** `Type` · `Character` (per-type voicings, e.g. Barber: Rise/Fall/Bloom…).
- **Front hero (3) + Mix:** `Shift` (±2 kHz, cubic taper through a ±20 Hz fine zone — the one knob that
  replaces Serum's SHIFT%-of-RANGE pair) · `Regen` (loop feedback) · `Damp` (loop LPF — the tone hero,
  mirroring delay's Tone) · **`Mix`** (100 % = fully wet).
- **Back 8 (4×2):** `Echo` (loop time, synced 4 bars→1/256) · `Spread` (L/R shift skew) · `Drift`
  (jitter/wow = their BLUR) · `Balance` (up-spiral vs down-spiral loop blend — their BALANCE, and the knob
  that makes `Barber` steerable) · `Low Keep` (mono-bass guard below crossover — the Utility duty absorbed
  where it's needed) · `Punch` (env-follow amount riding Shift with input dynamics — params evolve, law 5) ·
  `Feed Tone` (in-loop tilt, pre-Regen) · `Width` (M/S width of the wet, the mono-fold-gated one).
- Front pills: `Sync` (Echo sync on/off) + one per-Type pill; power gates everything.

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
   is the distortion bible §4.4 law: fixed latency, internally compensated, reported as zero. **[AUDIT] And
   the reason is stronger than tidiness: rack-law A (§4.4) forbids ANY latency-reporting device, because our
   exclusion sums subtract the dry sample-aligned. Their opt-in-latency limiter is not a wart we could
   choose to copy — it is a topology we are structurally barred from.**

---

## 7. Factory FX-rack presets (12 chain sketches for the epic's rack-preset system)

Names Title-case, pragmatic (law: name = what it does). Values in device-knob units of the shipped/bibled
devices; all obey unity-through at load.

| # | Rack name | Chain (order) | Intent + rough values |
|---|---|---|---|
| 1 | Wide And Warm | Chorus(Juno, Depth 35) → Reverb(Plate, Mix 22) | default-on "instant synth polish" |
| 2 | Acid Bite | Filter(Acid Ladder, env-modded) → Distortion(Diode/Ge, Drive 55) → Delay(Digital 1/8, FB 35) | the 303 rack; matches their "Acid Dist Delay" factory rack head-on |
| 3 | Big Sky Spiral | Bode(Barber, Shift +3 Hz, Regen 60, synced 1/2) → Reverb(Shimmer, Mix 30) | the device Serum can't answer with a preset |
| 4 | Pump Bus | Compressor(VCA, **Thresh −32 dBFS absolute = −6 dB relative to the −26 dBFS program mean**, Ratio 4:1, Rel 90 ms) → EQ(HiShelf +2.5 dB) | law 1 made explicit [AUDIT]: state BOTH the absolute number and its offset from −26 dBFS, so nobody re-derives it from mastering literature and lands 26 dB wrong. Expect ~4–6 dB GR on peaks |
| 5 | OTT Lite | Compressor(Multiband Up/Down, Below 3:1, Depth 40) | the multiband upward answer, tamed |
| 6 | Low Mono Punch | Splitter(L/H @120 Hz): Lows[Compressor fast] / Highs[Chorus] + Low Keep mono | their mono-bass Utility duty done in-lane |
| 7 | Tape Postcard | Delay(Tape 1/4 dotted, Wow up, FB 45) → Reverb(Vintage, Damp 60) | the lo-fi echo card |
| 8 | Metal Bell Maker | Bode(Ring, carrier 640 Hz keytracked) → Phaser(12-pole slow) | inharmonic bell from any pad |
| 9 | Vowel Talk | Filter(Formant-II, LFO on Cutoff) → Distortion(Tube, Drive 30) | talky lead |
| 10 | Side Air | Splitter(M/S): Mid[EQ tight] / Side[Reverb Basin, Mix 45] | width without mono-collapse (null-tested) |
| 11 | Broken Console | Distortion(DIGITAL SP-1200 char) → Delay(BBD, FB 55) → Compressor(FET) | the crunch rack Serum's no-bitcrush menu cannot build |
| 12 | Ghost Detune | Bode(**Up**, Shift 1.7 Hz, Spread 30, Drift up) → Hyper(Dim-style, Size 40) | pseudo-unison width, CPU-cheap (their own doctrine §1.6, out-deviced). [AUDIT] rewritten off the `Ghost` Type — this preset IS the proof that `Ghost` was a knob position, not a Type (§5.3) |

---

## 8. CPU — their strategy, our budget

**Serum:** global oversampling tiers (Draft 1× / High 2× / Ultra 4×) applied engine-wide + lockable; only
Delay carries a module HQ toggle; Convolve had CPU spikes patched (2.0.17); official doctrine pushes users
from per-voice unison to bus FX (§1.6).
**Terrain (keep + extend):** per-device Quality only where measured audible (distortion bible §3.7-3.8
budget: oversample only what aliases; NEVER oversample delays/reverbs/EQ/utility paths); dst CPU already
−35 % with awake-head sleep (fb342-344) — apply the same control-head sleep law to every chain slot so an
idle device costs ~zero.

**[AUDIT] Per-instance budget, stated as numbers (law 8 requires a budget, not a slogan).** Reference point
from the tree: the fb342 spring-reverb SIMD pass moved 6 springs from 2353 → 544 ns/sample, quoted as
**12.5 % → 2.6 % of one core** — i.e. **~210 ns/sample ≈ 1 % of one core** is the working conversion at our
sample rate. Budget the chain against that:

| Slot state | Budget (per instance, one core) | Enforcement |
|---|---|---|
| Empty / powered-off slot | **≤ 0.05 %** | control-head sleep + silence sleep (fb342); no per-sample loop runs |
| Idle (device on, input silent) | **≤ 0.1 %** | denormal flush at slot bounds + silence sleep (§9.9) |
| Cheap device active (EQ, Utility-trim, Chorus, Flanger, Phaser, Hyper) | **≤ 1 %** | no oversampling, ever |
| Mid device active (Delay, Compressor, Filter, **Shift**) | **≤ 2.5 %** | Shift = 24 Hilbert biquads + 2 osc + 1 delay line ≈ 0.3× one reverb; no oversampling ever (§5.3) |
| Expensive device active (Reverb, Distortion at High/Ultra, Convolve) | **≤ 5 %** | oversample only the modes that measurably alias |
| **Whole chain, 8 slots, realistic patch (3–4 active)** | **≤ 10 % of one core** | the ship gate |
| Absolute worst case (8 expensive slots, Ultra) | ≤ 35 % | must not glitch; may warn |

Their own optimisation guide is our benchmark harness input: one bus chorus must measure cheaper than a
7-voice unison, or we have no right to repeat their "one chorus beats 16 unison voices" story.

---

## 9. Pitfalls — collected for the epic (their warts + our known landmines)

1. **Exclusion-sum landmine:** any new bus/send re-breaks fb305 at **`PluginProcessor.cpp:7159`, `:7326`,
   `:7358` — THREE exact sites, not two, and not in index.html** [AUDIT, re-verified by reading the file;
   see §1.1 and rack-law D in §4.4]. First edit of the epic.
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

## 10. Hard-rule compliance walk (laws 1–10 + rack-laws A–D against this file's recommendations)

**Rack-laws (§4.4) first, because they gate the rest:** **A** zero lookahead — no lookahead limiter, no
linear-phase EQ, no framed spectral device; internal latency compensated and reported as zero (§6.5).
**B** no runtime params — the chain is a pre-allocated slot pool, N frozen at birth (§11 Q2). **C** choice
cardinality frozen — the device-type dropdown ships its FINAL 12-entry roster with unbuilt entries disabled;
`SYN_FX_ORDER` cannot grow past 3 devices (§4.2). **D** every new send bus joins all three exclusion sums at
`PluginProcessor.cpp:7159/:7326/:7358`, null-tested (§9.1).


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
10. **Recycle first:** see the verified inventory in §10.1 — nothing below was assumed; every row was
    opened and read during the audit.

### 10.1 Recycle inventory (verified by reading the file, 2026-08-14)

| What the epic needs | Reuse this, in-tree | Verified evidence | Note |
|---|---|---|---|
| Live spectrum taps (Chain X-Ray §3.2-2, Barber Falls §3.2-1, EQ analyzer) | `Source/SpectrumAnalyzer.h` | file present, already driving the shipped filter live-analyzer | do NOT write a second FFT |
| Shift's feedback loop + damping + sync | `Source/DelayEngine.h:1-14` | header comment: one shared fractional stereo delay line, ping-pong, HQ interpolation, in-loop tone, wow/flutter, BBD companding, allpass diffusion, ducking, M/S width; **pure C++, no JUCE** (offline-validatable) | also the model for "returns WET only; processor owns Mix" |
| Filter-as-FX device | `Source/TerrainFilters.h` — 2,195 lines, **94 types** behind one `FilterSlot` façade | line count read; 94 = `filterTypeChoices.add` calls in `PluginProcessor.cpp`; filter live-analyzer shipped | hosting + motion, not a DSP build (§11 Q3) |
| Chorus device core | `Source/TerrainChorus.h:2` "Header-only Juno-60-inspired BBD chorus" | present AND already instantiated twice: `PluginProcessor.h:1502` and `IndyFxChain.h:282` | proves it survives multi-instancing — the slot-pool precedent |
| EQ device core | `Source/ParametricEQ.h` | included at `IndyFxChain.h:31`; 7-band + HP/LP with per-band slope/bypass (`IndyFxChain.h:96-98`); it is the **6th** module of the per-layer chain (`IndyFxChain.h:13`) | |
| IR / Convolve work | `Source/ConvolutionReverb.h` + `CONVOLUTION-USER-IR-ADDENDUM.md` | Convolution is reverb type **9 of 9** (`ParameterIDs.hpp:345`, choice list at `PluginProcessor.cpp:3414`: Hall, Room, Plate, Spring, Digital, Vintage, Basin, Shimmer, Convolution) | embed-in-preset must clear the NO-DISK-WRITES rule |
| Per-slot independent FX state (the slot-pool precedent) | `Source/IndyFxChain.h:1-16` | "Owns its own copies of all 7 global FX modules… **shares APVTS parameter VALUES… but has INDEPENDENT STATE**" — exactly the shape a slot pool needs | read this before designing slots |
| Chain-order plumbing | `SYN_FX_ORDER` — `PluginProcessor.cpp:3488-3496` (6-way choice), `:5860` (`fxPerm_` index read), `index.html:7943/:7974/:8323` | [AUDIT] it is a choice, not a bool | seed only — see the cardinality trap §4.4-C |
| Exclusion-sum sites (touch all three) | `PluginProcessor.cpp:7159`, `:7326`, `:7358` | read verbatim during the audit | §4.4-D |
| Bus-level calibration constant | `kVoiceToFxPad = 0.5f` (−6 dB), `PluginProcessor.cpp:6300-6301` | the pad every send is scaled by; the −26 dBFS program figure lives downstream of it (`DistortionEngine.h:28, 640`) | law 1 |
| Type/character fade-swap machinery | Phase G char-switch deferred fade + state re-seat (fb345) | `DistortionEngine.h`, certified | reuse chain-wide (§9.11) |
| Chassis + preset menu + dropdowns | `Design/fx-rack-v7-CANONICAL.html`, `Design/fx-back-panel-mockup.html`, `TIC.presets` / `.pmenu` glass | frozen canonical | never re-implement |

---

## 11. Open questions for Max

1. **Rack architecture scope:** full Serum-style (3 busses + splitters + free order + multiple instances)
   or staged — Stage A: N-slot single chain with drag order + multi-instance; Stage B: splitter lanes;
   Stage C: busses? (fb305 landmine cost lives in Stage C.) ⚠️ **[AUDIT] Whatever the staging, N and the
   device-type list length are decided ONCE, in Stage A, and are frozen forever (§4.4-B/C). This question
   cannot be deferred past the first slot-pool commit.**
2. **Multiple instances** require engine-per-slot allocation (today: one engine object per device,
   distortion bible §0). ⚠️ **[AUDIT] "Unlimited with a CPU meter" is NOT AVAILABLE** — params cannot be
   created at runtime (§4.4-B). The real question is: **what is N?** The cost of a large N is host
   automation-lane clutter (N × ~11 lanes), not CPU (idle slots sleep, §8). **`FX-CHAIN-BIBLE.md` already
   costed this and is the authority: K = 5 pre-allocated slots × 26 params = 130 new params.** Reconcile the
   device-list length against the full 16-device roster in §5 (not the draft's 12) before that param is
   created — see rack-law C.
3. **Filter device Type list:** ship all existing synth filter types on the FX bus (Serum's move), or curate
   ~12 with Characters? (Their VAR knob ≈ our Character axis flattened — §2.8.)
4. ~~**Utility:** fold into Splitter/chain-trim?~~ **[AUDIT] ANSWERED AND SUPERSEDED** —
   `UTILITY-BUILD-BIBLE.md` specs it as a real device (Route + Flip dropdowns, gain/width/bass-mono, and the
   rack's first metering surface). The draft's "a trim strip can't meet law 9's drama bar" reasoning was the
   wrong test: the bible satisfies law 9 with **meters**, not drama. Nothing to decide here.
5. **Convolve:** keep inside Reverb, or split a generic "Impulse" device with user-IR embed + ϕ-min
   (the two features worth stealing, §2.4)? Embed-in-preset must clear the NO-DISK-WRITES rule.
6. **FX rack presets** (racks + per-module defaults + lock): in the epic, or a later arc?
7. ~~**Shift device (§5.3):** green-light the 6-Type proposal? Name check `Shift` vs `Bode`?~~
   **[AUDIT] BOTH ANSWERED ELSEWHERE** — `BODE-BUILD-BIBLE.md` settles the name (**`Bode`**), the roster
   (7 Types, with its own law-5 audit box), and the range (±5 kHz). The only question this file still adds
   is the law-5 one, and it belongs in the bible: **any shifter Type that is reachable by moving a back-8
   knob of another Type is not a Type.** (In the superseded sketch, `Counter` was `Up` with `Spread` at max
   and `Ghost` was `Up` at small `Shift` with `Drift` up — both fail. Check the bible's 7 against the same
   test before its choice-param cardinality is frozen, §4.4-C.)
8. **Research debt note — UPDATED BY THE AUDIT.** The original draft was written with the WebSearch budget
   exhausted (200/200) and flagged its own param inventories as unchecked. The audit pass closed most of
   that debt **without** web search, by going to harder sources:
   - **All 16 device param tables re-verified** against the extracted text of the local manual. Result:
     every param name, range and quoted phrase in §2 holds up. The inventories were good.
   - **The 16-module roster triple-confirmed**: the manual's "13 FX processors… three types of splitter
     modules", the installed `Serum 2 Presets/Effect Chains/` folder (exactly 16 module folders + `FX Rack`),
     and the binary's own preset-path strings.
   - **The Distortion roster upgraded from inferred to exact** (18 modes, menu order, from the shipping
     binary's enum — §2.6). This was the biggest single correction.
   - **Pricing corrected** (§4.3): $249 current, $189 intro expired 2025-06-01, **free for Serum 1 owners**.
   - **v2.0.17 changelog items confirmed**: Alt/Opt+F expanded-view shortcut, Key Track on Filter and
     Distortion freq right-click menus, "Improved CPU usage spikes in Conv reverb". ⚠️ The installed build
     is **2.1.4** — everything between 2.0.17 and 2.1.4 is undocumented here.
   - **Still [UNVERIFIED], deliberately left in place:** the form of the Distortion DC-bias control (§2.6);
     splitter nesting depth (§1.4); the "Init" entry in the rack-preset menu (§1.2); all screenshot-derived
     numeric defaults (compressor −18.1 dB / 4:1 / 90.1 ms, EQ 210 Hz / 0.60 / 2041 Hz, distortion 425 Hz /
     1.9, splitter 210 Hz default) — these came from reading images, not text, and no text source confirms
     them; and whether the logo-badge strips animate with audio at all.
   - Third-party review colour (SOS review is HTTP-410) and YouTube-walkthrough visualizer motion still
     want 20 minutes of Max eyeballing Serum 2 live before card mockups lock. That is now the *only*
     remaining research debt.

---

## 12. Sources

**Primary (local, complete) — text-extracted and re-read during the audit:**
- Serum 2 User Guide (official, v2.0 build 2025-03-17) — `/Library/Audio/Presets/Xfer Records/Serum 2
  Presets/Serum 2 User Guide.pdf` · FX chapter pp. 152–182 (all module strips also read as images) · Mixer
  pp. 144–151 · Filter-type & Var table pp. 136–140 · Drive Clean Mode p. 142 · Quality/Smoothing/S1-compat
  pp. 319–320. Extract with `pdftotext -layout` to re-check any quote here.
- Serum 1 manual (distortion module lineage) — `/Library/Audio/Presets/Xfer Records/Serum
  Presets/Serum_Manual.pdf` p. 25.

**Primary (local, the shipping product itself) — added by the audit, the strongest source available:**
- `/Library/Audio/Plug-Ins/VST3/Serum2.vst3/Contents/Resources/moduleinfo.json` → **`"Version": "2.1.4"`**
  (the installed build; the manual is two minor versions behind it).
- `/Library/Audio/Plug-Ins/VST3/Serum2.vst3/Contents/MacOS/Serum2` — the executable's string table carries
  the enums verbatim. Reproduce with `strings -a <binary> | grep kNumDistortions`:
  - FX distortion: `kTube = 0, kSoftClip, kHardClip, kDiode1, kDiode2, kLinFold, kSinFold, kZeroSquare,
    kDownsample, kAsym, kRectify, kXShaper, kXShaperAsym, kSineShaper, kStompBox, kTapeSat, kOverdrive,
    kSoftSat, kNumDistortions` → **18 modes** (§2.6).
  - Reverb: `kPlate = 0, kHall, kVintage, kAbyss, kSpace, kNumReverbTypes` → 5 types; **`kAbyss`/`kSpace` are
    the internal names of the NITROUS and BASIN menu entries**.
  - The 16 FX module identities also appear as preset paths: `Effect Chains/Distortion`, `…/Bode`,
    `…/Convolve`, `…/Utility`, `…/Splitter LH`, `…/Splitter LMH`, `…/Splitter MS`, etc.
- `/Library/Audio/Presets/Xfer Records/Serum 2 Presets/Effect Chains/` — **17 folders: the 16 FX modules
  plus `FX Rack`** (whole-rack presets). Independent confirmation of the 13 + 3 = 16 roster.

**Web (fetched — re-fetched and re-read during the audit):**
- Serum 2 product page: https://xferrecords.com/products/serum-2 — **"$249.00 USD"**, *"Serum 2 is a free
  upgrade for Serum 1 owners"*, *"Lifetime free updates."* (§4.3)
- What's New in Serum 2 (official PDF): https://static.xferrecords.com/Serum%202%20What's%20New.pdf —
  pp. 11–13 verbatim: "Bode — New frequency shifter effect" · "Convolve — New convolution effect" ·
  "Delay — New HQ mode (now default)" · "Distortion — New Overdrive mode and DC bias control" ·
  "Reverb — Three new reverb types: Vintage, Nitrous, and Basin" · "Utility — New utility effect" ·
  "Dual FX Busses" · "Multiple Instances" · "Expanded View" · "Direct Manipulation".
- Serum 2 v2.0.17 changelog coverage: https://rekkerd.org/xfer-records-updates-serum-2/ — confirms
  *"Added keyboard shortcut Alt/Option + F to toggle FX and Matrix expanded view"*, *"Added Key Track option
  to Filter and Distortion FX freq right-click menus"*, *"Improved CPU usage spikes in Conv reverb"*, and
  the pricing history (**intro $189 through 1 June 2025; regular $249**).
- Xfer support — Serum 2 category: https://support.xferrecords.com/category/45-serum-2
- Xfer support — CPU optimization guidelines:
  https://support.xferrecords.com/article/51-serum2-sound-design-guidelines-for-optimizing-cpu-usage
- RA.co launch coverage (Bode + three new reverb types + Overdrive; surfaced via search snippet) —
  [UNVERIFIED, snippet only; all three claims are independently confirmed by What's New above].

**In-tree (each opened and read during the audit; line numbers re-checked):**
- `Source/ParameterIDs.hpp:345-346` (reverb type/character) · `:374-375` (delay) · `:406-407` (distortion,
  23 modes / 8 characters) · `:435` (`SYN_FX_ORDER` — ⚠️ its comment still says "bool" and is **stale**;
  the live definition is the 6-way choice at `PluginProcessor.cpp:3488`).
- `Source/PluginProcessor.cpp:3414` (9 reverb types) · `:3488-3496` (FX chain order, 6 permutations) ·
  `:5860` (`fxPerm_` index read) · `:6300-6301` (`kVoiceToFxPad = 0.5f`, −6 dB) ·
  **`:7159`, `:7326`, `:7358` (the THREE fb305/fb338 exclusion sums)**.
- `Source/IndyFxChain.h:1-16` (independent-state chain), `:13` (module order), `:31`/`:96-98` (ParametricEQ),
  `:282` (second TerrainChorus instance) · `Source/PluginProcessor.h:1502` (first TerrainChorus instance).
- `Source/DelayEngine.h:1-14` · `Source/TerrainChorus.h:2` · `Source/TapeLoopProcessor.h:8`
  (⚠️ the draft cited `:6`) · `Source/DistortionEngine.h:28, 640` (the −26 dBFS bus calibration) ·
  `Source/GranularEngine.h` · `Source/ParametricEQ.h` · `Source/SpectrumAnalyzer.h` ·
  `Source/ui/public/index.html:7943, 7974, 8323` (chain-order UI).
  ⚠️ **`index.html:6979` and `:7111` do NOT contain exclusion sums** — the draft's citation was wrong; those
  lines are a robin SVG and ribbon-row CSS respectively.
- **Sibling research files in this folder (read during the audit — the draft cited none of them and
  contradicted three):** `FX-RACK-RESEARCH-INDEX.md` (the sweep's wake-up doc + build order — **read first**)
  · `BODE-BUILD-BIBLE.md` (device `Bode`, 7 Types, ±5 kHz, built on `TerrainFilters.h:1110/:1127`) ·
  `UTILITY-BUILD-BIBLE.md` (Utility IS a device: Route + Flip, the rack's metering surface — and it
  independently flags the same stale `:6979/:7111` citation this audit corrected) · `FX-CHAIN-BIBLE.md`
  (K = 5 pre-allocated slots × 26 params; "JUCE cannot create parameters at runtime") ·
  `FILTER-BUILD-BIBLE.md` (94 in-tree filter types) · `COMPRESSOR-BUILD-BIBLE.md` (zero lookahead is
  non-negotiable — the independent derivation of rack-law A) · `OTT-BUILD-BIBLE.md` ·
  `GRANULAR-FX-BUILD-BIBLE.md` · `TAPE-BUILD-BIBLE.md` · `DELAY-MOOG-PORT-PLAN.md` · plus the per-effect
  DSP literature each one carries (Bode/Hilbert dome filters, TAL reverb lineage, Dimension-D, etc.).
