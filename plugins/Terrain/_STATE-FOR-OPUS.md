# TERRAIN — STATE FOR OPUS

**HEAD `9d99853` (tp61)**, pushed to `feature/terrain-instrument`, `windows-test` and `main`.
Both Mac formats rebuilt from this tree and installed. Release target: **2026-10-10**.

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
