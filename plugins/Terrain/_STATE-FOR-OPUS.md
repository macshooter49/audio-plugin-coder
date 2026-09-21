# TERRAIN — STATE FOR OPUS

**HEAD = tp74** (see `git log -1`), pushed to `feature/terrain-instrument`, `windows-test` and `main`.
Both Mac formats rebuilt from this tree and installed. Release target: **2026-10-10**.

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
`Tests/_tp74_gate.js` (17, supersedes tp73's), `Tests/au_shaper_lock.cpp` (9), `Source/FlowShaper_test.cpp` (23), `Tests/_tp71_gate.js` (superseded by tp72's), `Tests/_tp61_gate.js` (14), `Tests/stem_memory_gate.py` (10 + 4 controls),
`fxtopo_test` case 22, `au_chopsend` [3] rewritten to the serial law.
⚠️ `capture_last_gate.py` had been STALE since tp20 and is live again (its anchor and rule [4]'s
window were both wrong). (tp70 cleanup: the stale probes `_tp10.js`, `_tp11.js`, `_probe59.js`, `_probe60.js` are deleted — they threw at HEAD and proved nothing; the gates in `Tests/README.md` are the record.)

## OPEN
The mod matrix clean-up (Max: "we can move onto the mod matrix tomorrow"), and everything still
carried in the VST-Plugins MEMORY.md.
