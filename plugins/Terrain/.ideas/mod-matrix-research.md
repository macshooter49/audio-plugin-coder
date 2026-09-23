# Modulation Matrix Research (Terrain)

Research date: 2026-09-23. Sources are manuals where they could be fetched; anything not verified
against a primary source is flagged **[unverified]**.

Primary sources actually read:
- Serum 2 User Guide PDF (Xfer, v2.0, pp. 209-214 "Using the Modulation Matrix"): https://www.xferrecords.com/manual/serum-2/docs
- Serum 2 "What's New" PDF (p. 14 "Enhanced Modulation Matrix"): https://images.equipboard.com/uploads/item/manual/127411/xfer-records-serum-2-advanced-wavetable-synthesizer-manual.pdf
- Zebra2 User Guide v2.9.4 (u-he, "Modulation Matrix" p. 57-58): https://uhe-dl.b-cdn.net/manuals/plugins/zebra2/Zebra2-user-guide.pdf
- Hive 2 product page (u-he): https://u-he.com/products/hive/
- Vital user guide (community-maintained, David Vogel): https://davidmvogel.com/docs/Vital/UserGuide/Modulation
- Kilohearts modulation docs (Phase Plant): https://kilohearts.com/docs/modulation
- Bitwig user guide, Unified Modulation System: https://www.bitwig.com/userguide/latest/the_unified_modulation_system/
- Arturia Pigments manual v5.0.0 (3.4 Modulation Overview, ch. 12 Modulation Routings): https://dl.arturia.net/products/pigments/manual/pigments_Manual_5_0_0_EN.pdf
- NI Massive X manual, Modulation: https://docs.native-instruments.com/ni-tech-manuals/massive-x-manual/en/modulation
- Ableton Live 12 Instrument Reference, 30.13.5 Wavetable Matrix Tab / 30.13.7 MIDI Tab: https://www.ableton.com/en/manual/live-instrument-reference/

---

## 1. Plain-words glossary (for a producer, not an engineer)

Think of each matrix **row** as one cable: *something that moves* (source) -> *a knob it moves* (destination),
with a few little processors in between. Signal flow of one Serum 2 row, left to right:
`SOURCE -> CRV (source curve) -> AMOUNT/POL -> x (AUX SOURCE -> INV -> aux CRV) -> OUTPUT -> DESTINATION`.

**Source.** The thing doing the moving: an LFO, an envelope, velocity, mod wheel, a macro, a random value
per note. Example: LFO 1 set to a 1/4-note sine.

**Destination (target).** The knob being moved. Example: filter cutoff. The knob's own position is the
"home" value; modulation pushes it away from home and back.

**Amount (depth).** How far the source pushes the knob. Bipolar slider: right = pushes up, left = pushes
the opposite way (inverted). Example: amount +30% = gentle wah; +100% = the cutoff sweeps the whole range.

**POL (polarity: unipolar vs bipolar).** Unipolar: modulation only goes *one way* from the knob (knob is
the bottom of the range, LFO lifts it up). Bipolar: modulation swings *both ways* around the knob (knob is
the middle). Serum's manual says you can get similar results with either - it is about whether you want
the knob to sit at the start or the centre of the movement. Example: vibrato wants bipolar (pitch wobbles
above and below the note); a filter "opening" envelope wants unipolar.

**CRV (source curve).** Bends the *response* of the source before it hits the knob. Linear = the knob
follows the source evenly. Bent one way, small source movements stay subtle and only the top end gets
extreme; bent the other way, it jumps quickly and then flattens. Example: velocity -> filter with an
exponential curve so soft/medium notes barely change tone, but hard hits really open up. Serum 2's curve
editor also has RISE/FALL smoothing on the source (a "slew limiter" that rounds off jumps).

**AUX SOURCE (a.k.a. Via, Sidechain, Modulation scaling).** A *second* source that controls *how much of
the first one gets through*. The two are multiplied: aux at zero = no modulation; aux at full = the full
amount. Example: LFO -> filter cutoff with AUX = velocity: soft notes barely wobble, hard notes wobble a
lot. Or AUX = mod wheel: the vibrato only appears when you push the wheel.

**INV (invert aux).** Flips the aux source so it works backwards: aux at max = *no* modulation, aux at
min = full modulation. Example: mod wheel up *removes* the wobble (a "calm down" control).

**Aux CRV (aux curve).** Same idea as CRV, but bends the *aux* source's response. Example: the mod wheel
does nothing for its first half, then brings the vibrato in fast near the top.

**OUTPUT.** The final trim for that one row, applied after everything else (after amount and aux).
Think of it as a small master fader for the cable. Serum 2: "Scale the final modulation output, allowing
for fine tuning." Example: you've shaped a great velocity-scaled wobble but it's slightly too strong -
pull OUTPUT down without touching the curve/aux setup.

**OUT (meter).** Serum 2 shows a small live graph per row of what the row is actually sending, so you can
see the combined result of curve + amount + aux.

**Bypass (mute).** Switches a row off without deleting it, so you can A/B what the modulation does or
silence a constantly sweeping filter while you work on something else. Serum 2, Vital, Pigments all have it.

**Delete (X).** Removes the row/assignment from the patch entirely.

**Stereo mod (Vital).** The modulation goes in *opposite directions* for left and right channels, so a
single LFO makes the sound widen/move in the stereo field instead of just moving up and down.
Example: LFO -> wavetable position in stereo = the left and right sides morph opposite ways = width.

**Per-voice (poly) vs global (mono).** Per-voice: every note gets its own copy of the modulator (each
note's envelope starts when *that* note starts; each note gets its own random value). Global/mono: one
shared modulator for the whole synth (one LFO everyone follows, e.g. a sidechain-pump LFO). Bitwig colours
these green (poly) vs blue (mono); Phase Plant draws the most recent voice in blue, other voices and the
global value in grey.

**Smoothing / slew / lag.** Rounds off sudden jumps in a mod signal so stepped or jittery sources don't
click or zipper. Serum 2: RISE/FALL on the curve editor; Hive 2: slew-limit modifier per target;
Bitwig modulators can be "discrete or slewed" (from Bitwig docs via search; exact per-modulator control
**[unverified]**).

---

## 2. Per-synth summaries

**Xfer Serum 2** - list-style matrix, 64 slots, integrated with drag-and-drop (dragging onto a knob adds
a row and vice-versa). Columns (manual pp. 210-212): SOURCE, CRV, AMOUNT (bipolar), POL, DESTINATION, OUT
(graph of the row's output), AUX SOURCE, INV, aux CRV, OUTPUT, bypass, remove. Rows can be reordered with a
drag handle; the matrix expands (Option/Alt-F) to show more rows. Header menu: Sort by Source, Sort by
Destination, Lock Matrix (keep assignments on preset change), Create Vibrato (unused LFO -> Main Tuning via
Mod Wheel), Create Velo->Amp, Apply and Delete Macros ("bake" macro offsets into knobs, then remove macro
routes). Some sources are matrix-only (Active Voices, Note-On Alt, Note-On Rand, oscillators/filters as
sources, Release Velocity, Voice Index, MPE X/Y/Z, "Fixed"). Tip in manual: use a macro as aux source and
modulate that macro to get a second aux. Source: https://www.xferrecords.com/manual/serum-2/docs

**Vital** - Matrix tab lists all mappings with source/destination popups, on/off toggle, polarity, amount,
response curve, and a secondary "Mod Remap" curve (identical to the LFO editor). Drag-and-drop assigning
highlights modulatable targets and lets you *audition* while hovering before releasing. Per-mapping
Bipolar and Stereo options (also in knob context menu "Make Bipolar"/"Make Stereo"). Source:
https://davidmvogel.com/docs/Vital/UserGuide/Modulation . Detail that clicking the row number turns it into
an "X" = bypass, 18 visible rows, and the "Morph" column name come from search-result excerpts of a Vital
manual copy (https://www.scribd.com/document/764609642/vital-user-manual) that could not be opened
**[partly unverified]**. Vital has no aux/via column (you'd modulate a macro instead) **[unverified]**.

**Kilohearts Phase Plant** - no grid; modulators are modules in a horizontal lane (up to 32). Connect via the
"+" on a modulator, then click a target; the amount knob sits next to the source. Each connection has a
curve ("diagonal line", like an inline Remap). Each modulator has output range unipolar/bipolar/inverted and
an output depth that can itself be modulated - i.e. "scale by" is done by modulating the modulator's
depth, not per-route. Utility modulators: Remap, Lower/Upper Limit, Scale, Sample & Hold. Voice display:
recent voice blue, others + global value grey. Source: https://kilohearts.com/docs/modulation . (A
dedicated "Multiply" modulator was not found in the current docs **[unverified]**.)

**Bitwig (Unified Modulation System)** - no matrix; click a modulator's routing button, then drag on any
target to set depth (orange). Per-routing "transfer functions": Linear (bipolar), Positives, Negatives,
Absolute, Toward Zero (unipolar), Exponential, Logarithmic. "Modulation scaling" lets one modulator scale
any single connection (the aux/via idea). Per-Voice toggle: green = polyphonic, blue = monophonic. Delete
by right-click. Source: https://www.bitwig.com/userguide/latest/the_unified_modulation_system/

**Arturia Pigments** - "Modulation Overview" strip of 24 sources with live animation; drag to a knob (all
eligible knobs get grey rings; hovering previews the mod at 25% before you release). Three views: Overview,
Mod Source view (per-parameter, 24 sliders), Mod Target view (per-source list of routes). Colour-coded
source families (MIDI pink, Envelopes orange, LFO amber, Functions green, Random purple, Combinate magenta,
Macros light blue) carried onto knob rings. Per-route mute button, X to delete, "Reassign"/"Copy modulation
to". "Modulation Quick Edit" mini pie-knobs under a modulated control. Per-route **SideChain** field = a
second source scaling the route (0-1, never exceeds the set amount) - Pigments' "modulation of
modulation". Source: https://dl.arturia.net/products/pigments/manual/pigments_Manual_5_0_0_EN.pdf

**NI Massive X** - no matrix; each knob has two modulation slots underneath; drag a source's arrow-cross
icon onto a slot, drag the slot up/down for positive/negative amount, colour-coded ring. A **sidechain
slot** between the two lets another source vary the modulation amount. Source families: Performers,
Modulators, Trackers, Voice Randomization. Source:
https://docs.native-instruments.com/ni-tech-manuals/massive-x-manual/en/modulation

**u-he Zebra2** - 12-slot matrix. Each slot: SRC (source + bipolar amount), VIA (a secondary source + amount
that scales how much of the source reaches the target), TARGET (right-click menu or drag the crosshair to a
knob). Documented quirk: with via source at minimum, negative via amounts scale depth 100%->200%. Example
from the manual: Env2 -> osc detune at 100%, via ModWheel. Zebra's VIA is the direct ancestor of Serum's AUX
SOURCE. Source: https://uhe-dl.b-cdn.net/manuals/plugins/zebra2/Zebra2-user-guide.pdf

**u-he Hive 2** - 12 matrix units (6 per page); each has a source, optional via source, and **two targets**
each with its own depth, plus 5 modifiers per target: curvature, rectify, quantize, sample & hold, slew.
Source: https://u-he.com/products/hive/ (product page; the Hive user guide PDF was not read **[unverified
detail]**).

**Ableton Wavetable** - true 2D grid: sources across (envelopes, LFOs), targets down; click-drag in a cell
to set amount. A parameter appears temporarily as a row when clicked and disappears if left unmodulated -
the matrix only ever shows what's in use. MIDI tab (Velocity, Note, Pitch Bend, Aftertouch, Mod Wheel,
Random) shares the same rows. Global "Time" (scales all modulator speeds) and "Amount" (scales all
modulation). Documents additive vs multiplicative targets. Source:
https://www.ableton.com/en/manual/live-instrument-reference/ (section 30.13.5-30.13.7)

---

## 3. Distilled essentials

### BASIC (must be obvious, big, fast)
- **Add a route by dragging source onto knob** - Serum 2, Vital, Pigments, Massive X, Zebra2 (target crosshair), Bitwig, Phase Plant.
- **Add route from the matrix (+ / empty row)** - Serum 2, Vital, Zebra2, Hive.
- **Pick source (menu, grouped by type)** - all; Pigments colour-codes groups.
- **Pick destination (menu or click-the-knob)** - all; Ableton adds a row when you touch a knob.
- **Amount, one bipolar slider, double/cmd-click to zero** - Serum 2 (Cmd/Ctrl-click resets), Pigments (double-click), all others.
- **Delete (X)** - Serum 2, Pigments, Bitwig (right-click), Vital.
- **See it working on the knob (ring/arc)** - Pigments, Massive X, Serum, Vital, Bitwig.
- **Audition while dragging** - Vital, Pigments (25% preview).

### PRO
- **Aux / via / sidechain / scaling source** - Serum 2 (AUX SOURCE), Zebra2 & Hive (Via), Pigments (SideChain), Massive X (sidechain slot), Bitwig (Modulation scaling), Phase Plant (modulate modulator depth).
- **Invert aux** - Serum 2 (INV); Zebra2 via negative amounts.
- **Source curve** - Serum 2 (CRV + editable curve), Vital (curve + Mod Remap), Phase Plant (per-connection), Bitwig (transfer functions), Hive (curvature modifier).
- **Aux curve (separate from source curve)** - Serum 2 only among those checked.
- **Polarity per route (uni/bi)** - Serum 2 (POL), Vital (Bipolar), Bitwig (transfer fn); per-modulator in Phase Plant.
- **Output trim per route** - Serum 2 (OUTPUT). Others fold it into amount.
- **Bypass/mute per route** - Serum 2, Vital, Pigments. (Solo per route: none found.)
- **Live per-row meter** - Serum 2 (OUT graph); Pigments animates sources; Phase Plant per-modulator display.
- **Per-voice vs global indicator** - Bitwig (green/blue), Phase Plant (blue/grey). Serum/Vital: not shown per row **[unverified]**.
- **Smoothing / slew** - Serum 2 (RISE/FALL in curve editor), Hive (slew modifier), Bitwig (slewed output).
- **Stereo modulation** - Vital (per mapping).
- **Sort** - Serum 2 (by source / by destination). **Search/filter**: none found in these manuals.
- **Reorder rows (drag handle)** - Serum 2.
- **Lock matrix across presets** - Serum 2.
- **Templates / one-click routes** - Serum 2 (Create Vibrato, Create Velo->Amp).
- **Bake macros** - Serum 2 (Apply and Delete Macros).
- **Reassign / copy route** - Pigments.
- **Global scale of all mod** - Ableton Wavetable (Amount, Time).
- **Multiple targets per slot** - Hive (2 per unit).

---

## 4. Recommendation: Basic vs Pro for Terrain

What the references show:
- Nobody ships a hard "Basic/Pro mode" switch. Instead the **default surface is simple** and depth sits
  one step away: Pigments hides SideChain until you click the field; Massive X shows the sidechain slot
  only as a small slot; Bitwig hides scaling/transfer functions in a right-click; Ableton hides unused rows
  entirely; Serum 2 has a compact vs expanded matrix and shows curves grey (= bypassed/linear) until used.
- Serum 2 is the only one that exposes *everything as columns at once* - powerful, and exactly what
  prompted "what are AUX SOURCE, OUTPUT, CRV?".

Recommended approach - **progressive disclosure per row, plus a global column toggle**:
1. **Basic row (always visible):** [bypass] Source | Amount (big bipolar slider, live meter in its track) |
   Destination | [X]. Drag-from-source-to-knob and a "+ Add" row as the two creation paths. Uni/bi polarity
   defaults sensibly per destination (pitch = bipolar, cutoff/levels = unipolar) so beginners never need POL.
2. **Pro expander per row (chevron):** reveals Curve (with smoothing), POL, Aux source + INV + aux curve,
   Output trim, stereo. A row that uses any pro setting shows a small badge so hidden state is never
   invisible (Serum's grey-when-unused curve is the right cue).
3. **Global "Pro columns" toggle** in the matrix header for power users who want the full Serum-style
   table at once; remember it per user (not per preset). Put sort-by-source/destination, lock, and
   templates (Vibrato-on-wheel, Velocity->Amp, Velocity-scaled LFO) in the header menu.
4. **Per-voice vs global**: colour the source chip (Bitwig convention: green poly / blue mono) in both modes -
   it explains otherwise-confusing behaviour ("why does each note wobble differently?").
5. **Plain-language tooltips** using the glossary above (e.g. Aux: "How much of this modulation gets through
   - e.g. velocity: hard notes = more").

Reasoning: a per-row expander keeps the beginner's view to 4 controls while never hiding that pro settings
exist on a given row; the global toggle serves the Serum-trained user who wants to scan all routes. A
single hard mode switch risks hiding active settings in Basic mode (a patch sounds "wrong" and the reason
is invisible) - avoid by always badging rows with non-default pro settings.
