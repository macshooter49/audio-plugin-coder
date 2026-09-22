# TERRAIN — STATE FOR OPUS

**HEAD = tp83** (see `git log -1`), pushed to `feature/terrain-instrument`, `windows-test` and `main`.
Both Mac formats rebuilt from this tree and installed. Release target: **2026-10-10**.

## tp83 (2026-09-21) — THE CHAIN IS PARAMETERS · THE NINE BORROWED EFFECTS ARE SOUND-CERTIFIED
**The unblock:** the chain lived only in the shaperJson blob, so the host could not automate the order and no
harness could PLACE a kind — which is what left every borrowed effect uncertifiable. Eight choice parameters now
(`FLOW_CHOP_SLOT1..8`, all seventeen kinds each), pushed to the DSP per block, mirrored into the card's state,
cloned to instances 2..4 by the existing `FLOW_CHOP_` tap.

**`Tests/au_shaper_fx.cpp` (13/13)** — each lane against the same render with the lane dark, as a MAGNITUDE-spectrum
change over a log Goertzel bank (magnitude, not samples: the fb283 trap), then the same measurement in the shape's
TROUGH against its PEAK, because always-on is not a Shaper lane.
Reverb 14.7 dB · Delay 10.7 · Chorus 8.9 · Widen 8.9 · Granular 23.6 · Bode 46.1, each moving more at the peak.

🚨 **THE BATTERY FOUND ITS OWN BLIND SPOT.** Multiband and Tape failed the generic bar (0.1 dB / −1.1 dB) and
NEITHER was broken. A band TILT is invisible to a magnitude metric — tilted one way and tilted the other are both
"changed" by the same amount — and Tape's claim is the harmonics it ADDS, not its level. By their own signature:
**Multiband 1.90× high-against-low trough→peak, Tape 0.034 → 0.235 THD.** ⚠️ The metric must be the one the CLAIM
is about, or it fails working code and passes broken code with equal confidence.

Named signatures: Bode moves 19.9 dB off a held C4's own frequency · Widen side-against-mid 0.0000 → 0.3984 ·
**Noise sounds with nothing playing** (−240 → −15.3 dBFS), the only lane that adds signal.

`au_shaper_fx.cpp` 13/13, `au_shaper_lock.cpp` 9/9, `FlowShaper_test.cpp` 36/36, `TerrainNoise_test.cpp` 15/15,
`_tp77_gate.js` 33/33.

⚠️ **OWED.** (1) **THE NOISE LANE'S BROWSER** — Max: "noise needs to have the browser of all 200+ sounds we have."
The library and its two-pane browser already exist for the synth's noise module (`listNoiseImports` / factory list,
index.html:41082; `tw::SampleBuffer noiseSampleBuffer_`; `SynthVoice::setNoiseSampleSource`). The Shaper's lane needs
its OWN buffer + selection + the looping reader, then the browser on its type chip. (2) The battery's remaining
half: every target swept end to end, every type distinct, a click check on a send opening, Delay / Bode bounded at
full feedback. (3) Taste is not certified and cannot be — that is Max's ear. (4) Time against ShaperBox 3 beyond the
pitch law.

## tp82 (2026-09-21) — THE NOISE LANE · THE ROSTER IS CLOSED
Max: "it should just be like ShaperBox's noise engine, nothing too complicated … we already have a whole bunch of
noises … imagine the stuff we can break beat with."

**NOT A LEND.** Every other effect the Shaper borrows is a finished self-contained rack engine. The noise generator
lived INSIDE `SynthVoice`'s render (three functions, eleven lines of state, thirteen colours). It was **lifted into
`TerrainNoise.h`, shared by the synth and the Shaper** — one generator, not a second one bolted on beside the real one.

🚨 **MOVED, NOT REWRITTEN — AND PROVEN.** The body was captured and asserted byte-equal to the lines it came from (only
edit: `juce::jlimit` → `tnClamp`, so the header needs no JUCE). `TerrainNoise_test.cpp` holds a **FROZEN copy of the
generator as it was** and runs it in lockstep with the shared one for two seconds a colour, demanding **bit-for-bit**
agreement (`memcmp`, not an epsilon). Also: every colour must actually sound (a silent one cannot null trivially),
and `reset()` must return a used generator to a fresh one's exact state.
⚠️ If a future edit to `TerrainNoise.h` reds that file, that is the point — either Max has to hear the change first,
or the change is wrong.

**The lane:** the shape is the noise's LEVEL — the only kind that ADDS signal rather than processing it. Targets:
Tone · Width · Scan · Drive. Thirteen colours index-aligned with `SYN_NOISE_TYPE`. Boots on a GATE, not a sine.
⚠️ No lazy arm and no atomic: it carries no ring, so it simply is. `noiseLpL_`/`noiseLpR_` went with the move (dead).

**Seventeen kinds, eight chain positions. The roster Max asked for is closed.**
`TerrainNoise_test.cpp` 15/15, `FlowShaper_test.cpp` 36/36, `_tp77_gate.js` 33/33, `au_shaper_lock.cpp` 9/9.

⚠️ **OWED.** (1) **SOUND CERTIFICATION of the nine borrowed kinds.** The AU cert cannot place a kind, because the
chain lives in the shaperJson blob, not in parameters. **Make the eight positions eight CHOICE PARAMETERS** — it
unlocks the cert and buys host automation of the chain. Then a per-effect battery: the drawn curve must MOVE each
effect's own signature (reverb tail length · delay correlation peak at the Time knob's ms · chorus spectral flux ·
widen side/mid · multiband band ratio · tape THD + wander · granular onset density · bode where a pure tone's energy
moved to · noise level), every target swept end to end, every type distinct, no click on a send opening, and Delay /
Bode bounded at full feedback. (2) Time against ShaperBox 3 / Gross Beat beyond the pitch law.

## tp81 (2026-09-21) — THE ROSTER CLOSES BUT ONE · FOUR TARGETS ON EVERY LANE
Max: "still owed: Tape, Granular, Bode and Noise … I want everyone to have four targets, exactly four."

**Three more lends** — Tape · Granular · Bode — through the same `ShaperExt::fx` door. Sixteen kinds, eight chain
positions. ⚠️ Granular and Bode expose no `typeNames()`, so their menus are COPIES of the rack's lists at
PluginProcessor.cpp:6596 / :7096 and must move with the page's `LNMODE`.
🔑 **The shape and the blend arrive APART now** (`fx(kind, s, k, mode, blend, …)`). Pre-multiplying forced every kind
to spend its shape on wet amount — right for a reverb (the shape IS the send), wrong for Bode, where the shape is the
INTERVAL and the wet stays put.
**FOUR TARGETS ON ALL SIXTEEN.** The fourth on each original lane: Volume Hold · Time Tone · Filter Punch · Pan Tilt ·
Repeat Tone · Drive Knee · Phaser Centre · Crush Stereo. ⚠️ Filter's Punch and Drive's Knee were already in the rack
engines and had never been reached — the two lend signatures gained an argument, nothing was written.
🚨 **A DEFAULT IS BEHAVIOUR.** Hold booted at the table's existing 0.5, held the gate open, and every Volume bar in
the offline proof went red at once. Every fourth target boots neutral now (Hold 0, Punch 0, Stereo 0, Centre 0.2 =
the 200 Hz the phaser always swept from, Tone/Tilt 0.5).
⚠️ Crush had NO width: both channels shared one hold counter. Stereo gives the right its own.

`FlowShaper_test.cpp` 35/35 (T27 each new fourth target audible — ⚠️ measured over BOTH channels, since a left-only
reading called Crush Stereo inaudible; T27b Punch/Knee arrive at their engine), `_tp77_gate.js` 33/33,
`au_shaper_lock.cpp` 9/9, `_tp61_gate.js` 16/16.

⚠️ **OWED.** (1) **NOISE, and it is NOT a lend** — every other effect is a self-contained engine class, but the noise
generator lives inside `SynthVoice`'s render (a switch on `noiseType_`, 13 colours). A Noise lane needs it lifted into
its own header used by both, with a bit-identical null on the synth before it ships. (2) That the eight borrowed kinds
SOUND right is still uncertified: the AU cert cannot place a kind, because the chain lives in the shaperJson blob, not
in parameters — **make the eight positions eight CHOICE PARAMETERS** (it also buys host automation of the chain).
(3) Time against ShaperBox 3 / Gross Beat beyond the pitch law.

## tp80 (2026-09-21) — THE ROSTER: FIVE RACK EFFECTS BECOME SHAPER LANES
Max: "we're going to make each of these effects available to shape … reverb, tape, widen, multiband, granular,
delay, bode, chorus … and a noise engine too … I want everyone to have exactly four targets."

**Kinds and positions are no longer the same number:** `kShaperSlots` = 8 chain positions, `kShaperLanes` = 13 kinds.
The chain is eight DISTINCT kinds out of the roster. A kind already down SWAPS positions; one not down REPLACES what
is there — that is how a Reverb enters the chain at all.
**One door:** `ShaperExt::fx (kind, s, k, mode, mix, l, r)`. The lane hands over the shape value, its four target
knobs, its type and its mix; the PROCESSOR decides what to drive. FlowShaper.h stays free of every engine's Params
struct (the offline proof still builds with no JUCE) and a new kind is eight lines.
**Nothing re-implemented:** Reverb (Room/Plate/Hall/Shimmer), Delay, Chorus, Widen, Multiband are the shipped rack
engines, a private second instance each in `ShaperRoster`, armed lazily like Filter and Drive. Type lists come from
the engines' own `typeNames()` (the splitter's three "Reserved" slots trimmed).

🚨 **`setLaneCtl` INDEXED `ctl_[lane & 7]`.** A mask sized to the old lane count: lanes 8..12 wrapped onto 0..4 and
pushed their OFF state over Volume, Time, Filter, Pan and Repeat. **The whole Shaper went inert in the real plugin
while every parameter still read correctly** (AU cert 5/9, dry passing through). A mask is not a bounds check — it
aliases silently. Same shape as `fltLive[which & 3]`, fixed one batch earlier. `T26` exists for it.

`FlowShaper_test.cpp` 33/33, `_tp77_gate.js` 33/33, `au_shaper_lock.cpp` 9/9, `_tp61_gate.js` 16/16.

⚠️ **OWED.** (1) That each of the five SOUNDS right in the plugin is NOT certified: the AU cert cannot place a kind
because the chain lives in the shaperJson blob, not in parameters. **Make the eight positions eight CHOICE PARAMETERS**
— it unlocks the cert and gives Max host automation of the chain. (2) Tape · Granular · Bode · **Noise** still owed;
every engine ships (`tw::TapeFxEngine`, `tw::GranularFxEngine`, `tw::TerrainBodeFx`, the noise library), so each is
one more lend. (3) The original eight lanes still carry three knobs plus steps; Max wants EXACTLY four everywhere.
(4) Time measured properly against ShaperBox 3 / Gross Beat beyond the pitch law.

## tp79 (2026-09-21) — THE SHAPER IS A CHAIN YOU ARRANGE · TIME'S PITCH LAW IS BACK
Max: "these are in a chain obviously … I right click on Time, boom, Multiband … I want to put Bode second, Pan third
… stop starting off with the volume on … change the shape to a sine, that gate looks ugly … Phaser and Cycle is purple
on purple … our time is broken, I want it to sound exactly like ShaperBox 3 — every time my grid goes somewhere, the
pitch moves."

**THE CHAIN.** The eight lane blocks were a FIXED sequence (Time·Repeat·Drive·Crush·Filter·Phaser·Pan·Volume) nothing
on screen showed. They are lifted into lambdas and dispatched in `ShaperState::slot[]` order, so the tile row IS the
signal chain and a right-click places a lane at a position. A kind cannot appear twice, so every pick is a SWAP — and
that is why the views stay indexed by KIND and the reorder needed **no state surgery**: one set of parameters, one set
of DSP state, one clock per lane, all still at their own index. The order rides the `shaperJson` blob, so **no new
parameter and no saved patch lost one**. `sanitise()` guards the switch the audio thread indexes.

🚨 **GLIDE WAS DESTROYING TIME'S PITCH LAW.** The shape is the read position ⇒ its SLOPE is the playback rate ⇒ pitch.
Glide slew-limited the read position's TOTAL motion, clamping the pitch: measured, at Glide 0.7 the rate sat at 0.818
for EVERY shape. Now the target's own velocity passes through (pitch) and only the error a discontinuity leaves is
slew-limited (the slide). Measured after: 1.000 / 0.750 / 0.500 / 0.250, Glide off and on.
🚨 **The ring read is CATMULL-ROM.** ⚠️ A cubic reads two samples AHEAD and the ring's future is unwritten — a read on
the write head (what unity does) interpolated towards stale memory and unity measured 0.964. Within two samples of the
head it falls back to linear, so unity stays bit-identical.
⚠️ **A regression this batch made and caught:** the lift swallowed the ring's write-head advance into the Repeat
lambda. It still ran every sample, but at whatever POSITION Repeat occupied — wrong once the chain is reorderable. It
is outside the dispatch now.

**Also:** nothing lit at boot, default shape a sine (the C++ seed's "sine" was still tp77's three-point SPIKE — now
`SINE9` verbatim), ribbons white, `ShaperRoster::filter` bounds-checked (`& 3` against a 3-element array).

`_tp77_gate.js` 33/33, `FlowShaper_test.cpp` 30/30, `au_shaper_lock.cpp` 9/9 (re-aimed: it leaned on the old defaults
without setting them), `_tp61_gate.js` 16/16, older page gates green.

⚠️ **STILL OWED — Max's roster.** He wants each rack effect shapeable in a lane: Reverb · Delay · Tape · Chorus ·
Widen · Multiband · Granular · Bode · **Noise** (his late addition, off the existing noise library), flanger folded
into Phaser, four targets each, night and day. NOT wanted: EQ, Compress, Utility, Splitter, standalone Flanger.
**The DSP is already written** — every one exists as a self-contained engine class, all already multi-instance
(`tw::TerrainChorusFx`, `tw::TerrainWidenFx`, `tw::TerrainSplitterFx` (zero-heap), `tw::TapeFxEngine`,
`tw::GranularFxEngine`, `tw::TerrainBodeFx`, `DelayEngine`/`MoogDelay`, the nine reverbs) — so each new kind is a
`ShaperExt` lend + a lazy arm in `ShaperRoster`, exactly as filter and drive already do. Watch the footprint: Delay,
Granular and Bode are ~8.4 MB of ring EACH at 48 k, so arm only a lit lane of that kind.

## tp78 (2026-09-21) — THE CHOP MENU'S SELECTOR SAYS THE NUMBER, NOT "ALL"
Max: "whenever we right click one of our slices … instead of it saying ALL in the selector, can it just be the number
of slices? Same number, same style, same everything. No glow in the middle."
The `.ov-all` pill in the chop right-click header wears `state.slices.length` — the TOTAL chop count, not the
selection count. The header's own spans keep `All Chops` / `Chop 7` / `5 Chops`; **that phrase is Max's from tp61b and
is untouched.** `ovPaintAllPill()` runs from `ovPaintTitle` (panel open, selection move) AND from the end of
`redrawSliceOverlay`, the one function that re-runs on a slice-LIST change — without the second call a chop added under
an open panel left a stale count. Style unchanged: the same 15 px outline on the Off pill's left rule, dim-to-white
hover, purple border + white ink when live, and `background/box-shadow/text-shadow:none` in BOTH states.
`_tp61_gate.js` 16/16, bars [8e] runtime + [8f] source (the delete half needs the native `deleteSlice`, absent
headless), both mutation-checked. UI only, no DSP.

## tp77 (2026-09-21) — ONE MIDLINE, ONE CARD COLOUR, A REAL SINE, THE LFO'S GRID BOTH WAYS
Max: "every time I click point or custom it gets lower … draw a line through the center of that bottom, it needs to go
through the middle of them both … the shaper doesn't follow the same grid rules as the LFO … purple dots, I want pure
white … sign is not in the middle … the Ladder LP is supposed to have the arrows, hug the word … our card is a couple
of shades lighter, I need ALL the cards the exact same colour … capital glitch / ARP / Robin down to lowercase … I
don't want that purple button at the top, move Shaper to the very top left … random curve isn't random, it's the same
… that's not a sine wave, that's a triangle spike … free draw sometimes turns into a ramp".
🚨 **The bar walked because a focused `<select>` scrolled its `overflow:hidden` screen** — the chip's invisible select
sat at `inset:-3px -2px`, overhanging the floor, so the browser scrolled the box to reveal it (measured
`.screen.scrollTop` 0 → 2.85 on the first click). Flush at `inset:0` + a `scroll` belt. The bar is a three-column grid
(`minmax(0,1fr) auto minmax(0,1fr)`) so the middle chip is centred in the BAR, and every chip owns its full height.
🚨 **`SINE9`** — the segment law is the exponential `bias()` (same one at `PluginProcessor.cpp:1914`), which cannot draw
a cosine across three points. Nine points on the eighths, each segment's curve fitted min-max to the cosine it covers
(±0.252 / ±0.041): max error 0.5 % of full height. The Sine brush stamps it too.
🚨 **`FREE_EAT = 0.02`** — a stroke starting inside the field left the old shape's breakpoint a hair outside the swept
span and the line fell off a wall to reach it (measured `M0.0 10.0 L3.6 98.0`). The sweep eats a margin past each end.
**`snapY()`** mirrors `snapX()` (the LFO's `shSnap` on both axes, `index.html:32257/32264`). **Random** rows leave the
`<select>` deselected, because a native select fires no change when you re-pick the selected row. **One card colour:**
`.ti-card` was translucent over a 22 px blur — now the popped window's opaque pair `#26223E → #1C1932`, docked and
popped, no blur. **Header:** grip and purple pip gone, `text-transform:none` on the title, `.h` padding 14 px so the
first letter stands on the screen's left rule. **Tiles:** lit = plain white, no ring, no border; the current lane is
told by its ink. Dots pure white in both renderers. **Type chip** wears `‹ … ›` above the select.
⚠️ Harness trap: the card is ~560 × 800 device px and opened near x 880, so a 1200 px viewport cut its right third off
and a drawn stroke stopped at the edge (59 of 91 moves landed) — the gate parks the card at 6,6 before any gesture.
`_tp77_gate.js` 30/30 (mutation-checked on the sine, the snap and the seam), `FlowShaper_test.cpp` 25/25,
`au_shaper_lock.cpp` 9/9 on the installed AU, older page gates green.
⚠️ Still unproven: the popped-window browser growth (tp74), in a real second window.

## tp76 (2026-09-21) — ONE LINE, ONE GRID; THE LFO'S GRAMMAR IN THE SHAPER; THE MENU ON TOP
Max (a beat made on the Shaper — "if I can make a beat with it, it's a go"): consistency — "three different grid lines …
I want the shaper's thin white line to be our new LFO … the LFO's functionality on the shaper … the point is glitchy …
the right click menu pops behind my shaper … fill in white like the LFO / glitch … a whole list of shapes".
**One line / one grid:** the LFO pane's stroke 1.3 units (≈ the Shaper's 1.07 px), the LFO card 1.1 px (non-scaling),
the expanded pane 1.7; the pane's nodes via CSS `r`; every grid white .065 / .14 (the stock grid was purple).
**The LFO's grammar on the field:** click selects, rubber-band, group drag, double-click adds / deletes / straightens,
3 px threshold, near-grab, zero detent. **The menu:** `#syn-panel` is a stacking context (z 30) — the menu is PORTALED
to the body (fixed, the panel's origin + local px) while a card floats and comes home on hide; the 44 rules are
`:is(#syn-panel .syn-ctx-menu, body > .syn-ctx-menu)`; trimmed to Grid / Level / Snap / flips / Random / Wavetable /
selection rows / Clear. **The library:** grouped shape dropdown (Basic · Gates · Curves · Steps; Time: Basic ·
Stutter · Tape), generators. Lit lanes fill white; chips on the floor. The Shaper page gate (now `_tp77_gate.js`) was 20/20 here, sweep green,
`au_shaper_lock.cpp` 9/9. ⚠️ the popped-window browser growth (tp74) is still unproven in a real second window.

## tp75 (2026-09-21) — THE SHAPER'S EMBLEM IS A SAWTOOTH
Max: "I think I want the Shaper emblem to be a sawtooth wave." `path.shpWave` on the Flow tile: two saw periods (rise, a
straight drop at x = 8 + 13·phase and 13 further, rise), phase 0 = the drop mid-tile = the home (one period read as a plain
line at rest); ⚠️ a `//` comment inside a one-line loop swallowed its brace and the frame dispatcher's whole script block died —
`_tp74_gate.js` [0] now asserts the page booted whole; the flowTiles painter's `sawD(tau)`
replaces `sineD`. One period per beat of `__hostBpm`, wind-up / wind-down, still with Motion off — `_tp74_gate.js` [0b] / [13].

## tp74 (2026-09-21) — THE SHAPER, COMPACTED
Max on tp73: the bar "way smaller … no emblems", "increase the height of the screen", "no on and off button in Target
— three buttons", "Filt is purple instead of white", "a regular Apple default drop-down menu (only the filters need
the browser)", "the browser menu gets cut off when popped out", "draw mode … free-flowing like water", "see the shaper
lines in the background", "stay away from the all capitals", "Smooth and Tension overlap", "am I really hearing it?".
Screen 124 px, bar 18 px of 7 px chips = native `<select>`s (Filter keeps the rack's browser), no glyphs, no caps but
the tiles, lit tile white. Target: three knobs a lane, no On — new in the DSP: Volume **Punch**, Time **Range** knob,
Filter **Spread** (`FilterFxEngine::Params.spread`), Pan **Haas**; `k[6]`; Smooth to 250 ms. Ghost lanes behind the
line. Free draw = every pointer sample, unsnapped. **Popped browser:** `openTwoPaneBrowser` grows the native card
window (`resizeCardWindow`) to hold the panel, the size push restores it on close — ⚠️ NOT proven in a real popped
window (no harness drives a second WKWebView); the law is in the code, Max's DAW is the test. `_tp74_gate.js` 17/17,
`FlowShaper_test.cpp` 25/25, sweep green, `au_shaper_lock.cpp` 9/9.

## tp73 (2026-09-21) — THE SHAPER IS THE GLITCH CARD WITH THE LFO IN ITS SCREEN
Max on tp72: "does not look anything like the LFO … why are you not taking the code from the LFO and pasting it here …
way too big … take the exact same glitch and just replace everything there with the LFO at the screen". Done literally:
the Glitch chassis (316 wide, 96 px screen, the eight text tiles + dots, Shape / Target / Clock, two boxes a row, the
chain), and the screen calls `window.__lfoField` — the LFO pane's own renderer, lent from its IIFE (shX / shY / shPathD /
shEvalPts, the node / curve-dot / hit markup, icons, chevron, dropdown). `_tp73_gate.js` [1] proves the stroke is
byte-equal to the LFO's `shPathD`. The bar is the LFO's: brush chip, shape chip (LFO glyphs + dropdown; Time = its
presets), type chip (roster lanes) → the RACK'S two-pane browser, normal case. Phase / Swing / Tension / Floor / Duck move
the breakpoints the LFO draws (nodes on the line); no display smoothing, no dashed raw line. Target = two Glitch boxes
(knobs + On | steps as dropdowns: Poles / Char, Range, Law, Mode). Clock = the OUT box (Rate · Grid · Trigger
dropdowns) + Sense. DSP unchanged from tp72. `_tp73_gate.js` 16/16, the sweep green, `au_shaper_lock.cpp` 9/9.
⚠️ lend `CHEV` / `ICON_CUSTOM` through getters (declared after the lend point). ⚠️ the shifted lane's seam law.

## tp72 (2026-09-21) — THE SHAPER, TO THE BRIEF
Max on tp71: "looks nothing like the mock-up … the grid to literally be the same thing as the LFO … the follower is
trash, it bounces … the target is fucked up … too lengthy, needs to be wider … I need to be able to right click".
**The card is the mockup's** (`.ti-card.shp-ext` 462 wide, on the Patcher too): the screen, the eight targets, Shape /
Target / Clock, pill rows in titled boxes (Brush · Shape · Rate · Grid · Trigger + Sense). **The screen is the LFO
editor's field** — the same grid / stroke / node / curve-dot / follower classes and numbers — and the line drawn is the
line the DSP reads: Tension, Phase, Floor, Smooth, Swing all move it (the raw breakpoints dashed behind when they differ).
Right-click opens on the right pointer-DOWN (the EQ's macOS law) with the LFO's menu (Grid + Level, Snap, flips,
Random ×3, the tools, Wavetable → Shape A–D, stock, Clear; no Extend). The follower rides the push and advances on
the tempo between pushes (never backwards, snaps on a jump, fades in silence).

**DSP (`FlowShaper.h`, 23/23):** `wc::ShaperExt` — the processor lends the rack's engines: Filter lane = the 118-engine
roster (`FilterFxEngine`, `FLOW_CHOP_FILT_MODE` = engine index, picked through the rack's two-pane browser), Drive =
the 23 distortions (`DistortionEngine`, mixes its own aligned dry), Phaser = built-in phaser / flanger + the roster's
phasers / flangers / combs (`kShaperPhaserRoster`), Crush = built-ins + Bit-Crush / Samp-Hold / Radio + the digital
family. Engines ARM LAZILY on the timer (`ShaperRoster`, atomics; the built-in runs until then). Triggers per lane:
Sync · Free · Audio (onset, Sense in the blob) · MIDI (`FLOW_CHOP_<LN>_TRIG`, the note's own sample). The Target tab
is every lane's back panel, all in the engine: Volume Attack/Release + Duck · Time Fade/Glide (a pitch-bent slide) +
Range · Filter Reso/Drive/Poles/Character · Pan Width/Bass mono + Width mode · Repeat Seam/Decay/Pitch + Reverse ·
Drive Tone/Makeup/Character/Bias · Phaser Feedback/Stereo/Drive · Crush Bits/Rate/Tone. Smooth now smooths every
shaped lane (not Time / Repeat).

**Proof:** `au_shaper_lock.cpp` 9/9 on the installed AU (the tp71 bars + Acid 303 on the Filter lane sweeps, Soft Clip
on the Drive lane drives, Trigger MIDI runs the gate with the transport stopped); `_tp72_gate.js` 16/16; the tp57–tp67
sweep green. ⚠️ the lit-pill bar first read the BASE colour: `getComputedStyle` is live — snapshot before the next
click. ⚠️ `.chop-ext .pane .kin` forces four columns on every box — outranked with `.ti-card.shp-ext .gbox .kin`.

**Not done:** lane Depth / On / the target knobs are not mod destinations (Max: "eventually we can modulate").

## tp71 (2026-09-21) — THE TERRAIN SHAPER
**The Chop card is the Terrain Shaper** — a ShaperBox 3 / Gross Beat-style multi-lane shaper on the
Glitch card's chassis (mix bar, presets, pop-out, duplicatable ×4, routable in the Patcher; the `chop`
kind key, chain kind 16 and the `FLOW_CHOP_*` pool are all REUSED, so nothing else moved). Eight targets
under the screen (Volume / Time / Filter / Pan / Repeat / Drive / Phaser / Crush — Max's names: Liquid →
Phaser, Width → Repeat "same as the Glitch"), each a drawn shape on the LFO editor's own law (breakpoints
`[x,y,c]`, pinned ends, bias curve, the brushes, free draw that DRAWS, right-click = the LFO menu with the
grid slider, snap, flips, random, stock shapes, time presets). No captions on the screen; the bottom strip
is a centred ‹ selector › (filter type / drive type / crush mode / time preset per lane). Tabs: Shape
(ONE Depth knob, plain like every other knob; Tension / Floor / Blend), Target (the "back panel": On, Type,
per-target knobs), Clock (Rate / Grid / Trigger boxes + Phase / Swing / Smooth). Selected buttons: purple
outline, white inside, no glow. The Flow tile's emblem is one moving sine in the bull's-eye centre — tp71b: a FULL
period between fixed ends (Max: "not cut off"), phase-driven by the `flowTiles` painter on the motion clock at one period
per beat of `window.__hostBpm` (a one-statement frame stamp), wind-up / wind-down onto phase 0, still with Motion off.

**DSP (`Source/FlowShaper.h`, JUCE-free, `FlowShaper_test.cpp` 14/14):** every lane reads its 2048-cell
baked table at `phase = ppq / cycleBeats mod 1` PER SAMPLE — "as soon as you press play it instantly
shapes, no offset". Time = Gross Beat's read-position law on a 16 s ring (armed lazily on the timer when
Time/Repeat switch on; jump law so a jump lands on a settled target with a ≥ 48-sample fade); Repeat =
grid-captured slice loop (height → slice length); Filter = SVF LP/HP/BP/Notch; Drive = soft/hard/fold/tube
(a wire at zero — fade-in over the first quarter); Phaser 6-stage / Flanger; Crush bits+rate; Pan
constant-power; Volume smoothed; `mix` blends the whole card. Per lane: `FLOW_CHOP_<LN>_ON/DEPTH/RATE/MODE`
(named "Shaper <Lane> On/Depth/Rate/Mode", cloned to instances 2..4 by the pool block); the shapes travel as
JSON (`setShaperJson`/`getShaperJson` natives; `shaperJson<i>` in the state beside `lfoShapesJson`; cleared
by clearPatchBlobs). Feed: `{"ph":[8],"v":[8],"ln":[8],"b","on","pl"}` at 60 Hz through
`__flowFeedPush.chop[inst]`.

**Proof on the installed AU (`Tests/au_shaper_lock.cpp`, HostCallbacks = a real transport):** play from
bar 1 → sixteenth 0 ON / 1 OFF from the first samples; play pressed a sixteenth IN → the first sixteenth
heard is OFF (read at the DAW's position); stopped → phase 0 held; Volume On = 0 → no gate; Time unity =
a wire. 6/6. Page: `_tp71_gate.js` 11/11; the whole sweep (tp57…tp67) green; `_tp67_gate.js` [0] learned
the `chop:/./` entry in `DICE_TIME_RX` (the Shaper never rolls — a shape is drawn).

**⚠️ NOT DONE (say so to Max):** the Filter lane's type picker is the SVF's four modes, not the rack
filter roster (ladder / acid 303 … `FilterFxEngine` not wired); the Drive lane's four types are built in
(the `DistortionEngine` roster not wired); Trigger is Sync only (Free / Audio / MIDI are listed, not live);
lane Depth / On are not mod destinations; the Patcher node's visuals for the Shaper were not specifically
checked. The pop-out and presets ride the TIC factory's own paths, as the Glitch's do.

## tp64 (2026-09-20, evening) — THE MACHINES AS CARDS, THE DECK, THE CHOP PAGE FOLLOWS THE PRESET, BANK B
**The three tape machines are card TYPES again — on the machine DSP this time.** Max: "make them like
everything else … duplicatable and per-routable … we can run cables in and out". `TAPEFX` lists five
(Studio / Cassette with the back panel; Reel / Porta / Wire three knobs, `d.noBack`), each ×6
(`kFxInstances`). 🚨 **THE ROOT OF "THE DSP IS THE SAME":** `applyTpe` clamped `wantType` to `ty <= 1`
from the day the card was born, so tp43's Reel and tp60's Porta / Wire ran as Studio in every host.
Widened to 4. The Reel reads `SYN_TPE*_SCULPT/WEAVE/TILT` (declared per instance since fb365, read by
nothing) into `Params::sculpt/weave/tilt`; only StudioMachine consumes them, so Studio / Cassette are
untouched. `Tests/au_tape_types.cpp` on the installed AU: closest pair 2.83 dB, Reel/Porta 14.3 dB,
Tilt moves the Sculptor 9 dB (⚠️ the card needs `Tape SRC_A` lit or the engine is never built and the
slot passes through). The browser's `tape:N` (global machine) entries are gone; `kind:'tape'` stays for
a patch that has the hero machine on. `fxrRestoreTapeOne` reads v[2..4] for a Reel.
**The Deck** was ~98 % alive (kind `tapeloop`, `adoptLoop`/`returnLoop`, `kDeckKind` 18 in the chain,
`SYN_DCK_*`) and had NEVER had a browser entry. Now `k:'deck'` on the Tape shelf; presence =
`SYN_DCK_ACTIVE` (the tp61 sampler law) or `layout.deck` for an uncabled one; Remove clears its routes.
🚨 **The Chop page follows the preset.** The preset carried everything (embedded FLAC per layer, slices,
markers, mixer) and `setStateInformation` restored it all — the page hydrated ONCE at boot
(`heroOverlay`'s two IIFEs), so a recall showed the previous patch's waveform over the new sound.
`window.__tiChopRepull` (= `tiHydrateLayersFromCpp` → `tiPullSlicerFromCpp`, plus every registered
`__tiChopPulls` entry: strips/trigger/stems, hold, ARM/BPM/lib strip) runs from `onPatchLoaded`. Also
saved now: per-layer `sampleLoopMode` (the 1-SHOT/LOOP pill — every .terrain came back one-shot) and
`sourceBpmUser`. A user sample is written once as a spare `<data dir>/Samples/Imported/<name>.flac`
when first encoded for a save (`tiWriteSampleSpare`; factory paths and our own folder skipped).
**Bank B is released** (`releaseIdleBankBIfUnused`: unwanted 3 s + no voice → `bankB_ = nullptr` →
+3 audioSeq → `synthEngineB_.reset()`). ⚠️ read it on **malloc size_in_use** — the footprint does not
move for a release of many small blocks (the harness prints both now).
**tp70 — PHASE MODE DEFAULTS TO RANDOM (Serum's law), at Max's word "yes I'd like this, just to see".** ⚠️ This
REVERSES his own tp12b/tp12c rule of 2026-09-15 ("I want it all to have 180 no matter what … not a random 180"):
the four `SYN_OSC_x_PHASE_MODE` defaults are 2 (E–H clone it; verified on the AU: def 2) and the Patcher's
`phase180()` spawns oscillators at Random (was 0). PHASE stays 0.5 (180°) and PHASE_AMT 1, so Random = 180° + a
fresh random offset per note, exactly fb631's "180 with the random 100". ONE LINE back: `2));   // tp70` → 0 in the
four declarations and `choiceNorm(X+'PHASE_MODE', 2, 4)` → 0. ⚠️ The attack-peak "discriminator" I first quoted
(-1.8 vs -4.0 dBFS) was noise: repeated takes vary in every mode (tails, stealing) — do not re-use it as proof.
**tp69 — THE MASTER IS LINEAR IN A HOST (the sine-chord "clipping").** Measured against Serum 2's own sine
(`Tests/au_chord_*.cpp`): same per-note level (-15.14 dBFS), Terrain's Sine pure, the beating identical — but a
7-note Cm11 through the limiter + soft clip carried distortion 46 dB under the notes (9 notes: 29 dB) while Serum
added nothing and simply passed +0.74 / +1.65 dBFS. The limiter (0.8 ms) cannot hold a sine peak; the clip did
the work. `masterGuard_` (ctor: `wrapperType == wrapperType_Standalone`) keeps the limiter + soft clip for the
standalone D/A only; a host gets a wire. After: 81 / 79 dB under, peaks like Serum's. ⚠️ -80 dB second-order
residual between the voice sum and the output trim (gone at half OSC Level, present at half Output Gain) — inaudible,
unlocated. Phase Mode Manual (all voices start together) left as is; Serum ships random phase.
**tp68 — THE GLITCH'S ANCHOR IS ON THE GRID.** `FlowGlitch::process`: the first clock latched `curStepP` (a
boundary BEHIND p → a mid-step play fired at once, off-grid) and every re-anchor latched `curStepP + 1` (a loop
wrap landing exactly on a boundary — every DAW loop — missed the downbeat, a step late every pass). One `anchor()`
now (main tracker + every fire group): on a boundary within max(2 % of a step, 32 samples) → that step, else the
next. "Main" on a module's grid already meant "follow the master grid" (`fxGrid 0`), and the Clock default was
always Sync — what he saw was the card's dice (tp67) plus these anchors plus Swing/Chaos/Nudge/Vary rolled by the
dice (now in `DICE_TIME_RX`). `FlowGlitch_test.cpp` T35a-c. ⚠️ Max's Terrain Shaper (ShaperBox-style card
replacing Chop: LFO-shaper-driven volume / filter / drive / pan / width / "liquid", DAW-locked) — his brief is
next; see the tp68 reply for the design position.
**tp67 — THE GLITCH IS ALWAYS IN TIME.** `FLOW_GLI_SYNC` ("Glitch Clock") has defaulted to Sync since birth and
instances 2..4 clone it; the card's Init is Sync. What put cards on Free was the CARD's own dice (`diceMode('gli')`,
index.html ~16440): `syncf` and every module's `_otrg` (Trig, Sync/Free) are binaries the dice coin-flipped, and
`diceGrids` re-dealt every `FLOW_GLI_*_GRID`. `DICE_TIME_RX.gli` (syncf / grate / quant / *_otrg / *_ogrd) is
never rolled, `DICE_SYNC.gli` snaps the Clock and every Trig to Sync on every roll, the grid re-deal is gone.
Free stays the hand's. Max's item 3 ("we're replacing Chop") is pending his brief. `Tests/_tp67_gate.js` (4).
**tp66 — ONE WAVEFORM; THE AUDIO PICTURES MOVE WITH MOTION OFF.** Max: "they're not the same thing … they
look very random … pick something and stick with it." `window.__tiWave` / `__tiWaveGeom` / `__tiWaveSvgHtml`
(index.html, the early block) is THE sample picture; the Chop hero (`drawWaveform`, ampFrac .70 for its 50 px
reserve), the per-chop tile (`drawChopWaveform`, `peakRef` = the whole sample's peak), the OSC · Sample slot
(`drawPeaks`), the noise wave view (`drawNoiseWave`, 220 columns interpolated) and the convolution IR
(`drawConvWave`, SVG) all call it; the dead `sampDraw` is gone. Law: min/max envelope on a hairline baseline,
one column per CSS px folding its bins (never nearest-bin), outer envelope held ±1 column + 5-tap smooth,
ink 16 % body + 92 % 1.25 px contours, no glow; a hot source scaled to 92 %, a quiet one never pumped.
NOT converted: the granular card's live-ring line (a different picture: grains ride it) and the Flow Chop
ribbon (synthetic). Motion off: the oscilloscope no longer parks (editor `oscActive`), the noise WAVE view
follows `act` unscaled by the motion envelope (the particle cloud still rests), the waterfall already moved
on MIDI. `Tests/_tp66_gate.js` (8), `_shot66.js` renders the three pictures — look at them.
**tp65 — THE FRONT PAGE IS NEVER SHOWN.** Max: "just make sure I can't go back to it … I just don't want to
see it." `setActivePanel` hides `#controls` for MOD/EQ/DLY (the Chop page's two close handlers used to restore
them under MOD: CHOP → MOD showed the hero behind the panel — they now respect `currentActivePanel`);
`restoreUiPage(0)` lands on SYN (a page saved with Chop open stores 0 and the editor pushes it); the hidden
EQ/DLY toggles and back buttons go to SYN, never null. The hero section is still in the DOM (the tape section,
the deck's home) — it is simply unreachable. `_tp64_gate.js` [3c] walks fourteen pill presses + page 0.
**Also:** MOD and CHOP pills never toggle off (only SYN has a back); the Settings bend "2"/"st" are
white in the label's font (the root ink follows the Theme setting, Light by default, while the UI is
hard-coded dark — the light theme is still the last pass).
**Gates:** `Tests/_tp64_gate.js` (15), `au_tape_types.cpp` (5), `au_lazy_memory.cpp` (+2); tp63 [0]/[0b]
and tp60 [0]/[2] rewritten to the tp64 law. All page gates green.

## tp63 (2026-09-20, afternoon) — THE MACHINES, THE FILTER, THE RANDOMIZE MEMORY
**The three tape machines are modules again.** `TAPE_MACHINE` (Reel = Harmonic Sculptor, Porta, Wire)
never left the engine or the hero page; tp43a deleted their three browser entries (`k:'tape:N'`) and
tp60 built three card TYPES on `TapeFxEngine` by the same names. Entries restored, the card offers
Studio/Cassette only, no "Tape ·" anywhere. TapeFxEngine voices 3/4 remain in code, unreachable.
**The master filter always wins**: a cable into the filter turns every raw tap on that oscillator
post-filter (flow cards + devices); a card cabled while the osc is in the filter takes it filtered.
**Randomize memory**: `au_lazy_memory.cpp` — MODAL +1,213 MB PER BANK, HARM +55, bank B +298, never
freed. `releaseIdleEnginesIfUnused()` (timer): unwanted ≥3 s + no voice active → disarm (render guard)
→ +3 audioSeq → release. Measured 2,791→711 MB. ⚠️ `phys_footprint` does not count malloc's reused
zero pages: a re-arm reads +0; proved by sound. Bank B is NOT released (298 MB). The per-roll growth was the WAVETABLE BANK (every table
visited stayed built, +234 MB/6 rounds) — `releaseIdleWavetables()` unpublishes a preset no oscillator names
for 3 s and frees its storage +3 audioSeq later (voices get the pointer every block). Net +2 MB now.
**Also**: shaper follows type with motion off (one counter per feed — tp62 nested two); no stem
caption; capture strip OFF word white; Slices pill fixed width; pitch-bend ink.
**Not reproduced**: the Patcher LFO right-click "edit as shape" (Chrome opens it, real mouse too) and
the glitchy double-click — needs Max's exact steps in the real host.

## tp62 (2026-09-20, morning) — MOTION ON/OFF
Settings → Motion. Off = the fb636 motion envelope (`MOT`) never rises, MIDI or not, so every
decorative animator (they read `MOT.tau/e` or a `__tiWind` key) holds its rest pose; the Flow
tiles' keys (`/^card-(gli|chop|arp)/`) are exempt; the waterfall + Patcher node lights read
`__tiLive()`. C++: `motionEnabled_` (marker `~/Library/Caches/Terrain/motion-off`, read in the
ctor), `uiStatic()`, `vz()` on 28 audio-driven numbers in the viz builders, `vizReverbBloom/
vizDelayBloom` = mix × decay / mix × feedback, editor lane at 30 Hz with decorative feeds every
8th tick (`decoTick`), scope parked, spectrum at half rate. **MEASURED on real editors**
(`Tests/mac_motion_cpu.mm`): 1 editor 41.2→27.3 % of a core, 4 editors 160.8→91.3 %. DSP
bit-identical (`Tests/au_motion_null.cpp`). ⚠️ `proc_pid_rusage` times are Mach ticks.

## tp61 (2026-09-20, early hours)

**A CHOP LAYER IS A REAL SOURCE IN THE CHAIN.** `FxChainTopology` bits 11..14 = layers A..D
(`kChopShift`). Before this, tp56 ADDED a layer's raw audio to *every* device bus that claimed it —
parallel by construction, which is why a Glitch and a Delay both got a dry copy. Now the layer
ENTERS at the first device routed to it and everything downstream eats the upstream OUTPUT through
`fxTopo_.feed`. No new signal path. ⚠️ FIFTEEN of sixteen mask bits are spent; one is left.
`poolChopEntry_`/`hallChopEntry_`/… are scattered right after `fxTopo_.build` and are what the feed
block reads; `poolChopMask_` (the full mask) still arms `poolRouteAny_` deliberately.
The canvas sorts by the same table (`NALL=15`) so it cannot draw a chain the audio is not playing.

**CAPTURE OFF MEANS NO RINGS.** ~1,058 MB of stem rings + ~202 MB export ring per instance.
Guards on `ensureStemLayerAllocated` / `allocateStemBuffers`, `releaseStemBuffers()` on the way off,
and the preference is read in the CONSTRUCTOR (tp43 read it in `createEditor`, far too late — the
timer arms stem rings the moment a layer holds a sample, with or without a UI).

**THE REST:** sampler input removed (Max reversed tp58); a cabled sampler is adopted from its
cables, not `layout.samp` (a localStorage key every instance on the machine shares — that is why
they disappeared); E–H right-click menu (`/SYN_OSC_([A-D])_/`); exclusive solo × 8; no purple fills
or glows anywhere; ALL pill + header rewritten ("All Chops" / "Chop 7" / "5 Chops", thin white).

## GATES
`Tests/_tp77_gate.js` (30 — the ONE Shaper page gate; tp71–tp76's were each renamed into the next and tp71's deleted at the tp76 cleanup), `Tests/au_shaper_lock.cpp` (9), `Source/FlowShaper_test.cpp` (25), `Tests/_tp61_gate.js` (14), `Tests/stem_memory_gate.py` (10 + 4 controls),
`fxtopo_test` case 22, `au_chopsend` [3] rewritten to the serial law.
⚠️ `capture_last_gate.py` had been STALE since tp20 and is live again (its anchor and rule [4]'s
window were both wrong). (tp70 cleanup: the stale probes `_tp10.js`, `_tp11.js`, `_probe59.js`, `_probe60.js` are deleted — they threw at HEAD and proved nothing; the gates in `Tests/README.md` are the record. tp76 cleanup: `_tp71_gate.js` deleted for the same reason.)

## OPEN
The mod matrix clean-up (Max: "we can move onto the mod matrix tomorrow"), and everything still
carried in the VST-Plugins MEMORY.md.
