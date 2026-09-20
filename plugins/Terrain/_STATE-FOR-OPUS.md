# TERRAIN — STATE FOR OPUS

**HEAD `43ba776`+ (tp63b)**, pushed to `feature/terrain-instrument`, `windows-test` and `main`.
Both Mac formats rebuilt from this tree and installed. Release target: **2026-10-10**.

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
