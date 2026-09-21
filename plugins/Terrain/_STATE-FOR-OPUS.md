# TERRAIN — STATE FOR OPUS

**HEAD = tp64** (see `git log -1`), pushed to `feature/terrain-instrument`, `windows-test` and `main`.
Both Mac formats rebuilt from this tree and installed. Release target: **2026-10-10**.

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
`Tests/_tp61_gate.js` (14), `Tests/stem_memory_gate.py` (10 + 4 controls),
`fxtopo_test` case 22, `au_chopsend` [3] rewritten to the serial law.
⚠️ `capture_last_gate.py` had been STALE since tp20 and is live again (its anchor and rule [4]'s
window were both wrong). ⚠️ `Tests/_tp10.js` is a stale PROBE — it throws at HEAD too, not a tp61
regression, not chased.

## OPEN
The mod matrix clean-up (Max: "we can move onto the mod matrix tomorrow"), and everything still
carried in the VST-Plugins MEMORY.md.
