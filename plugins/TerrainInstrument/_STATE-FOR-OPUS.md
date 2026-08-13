# TERRAIN INSTRUMENT — STATE (fb335, 2026-08-12, CLEAN-UP CLOSE-OUT)
<!-- HEAD: __HEADHASH__ -->

## Session close-out (fb334-335, after the fb326-333 MAJOR CHECKPOINT commit 377dcb2)
* **fb334 ALWAYS EVOLVING**: mini-viz geometry repaints from LOCAL CV every rAF frame (mirror
  pass in the rack driver, fully feed-independent) + a 300 ms `busy` watchdog on the viz poll —
  a never-settling JUCE promise can no longer freeze the mini/light/type-follow. LAW: busy-gated
  native polls need settle watchdogs; state-mirroring viz redraws from LOCAL state per frame.
* **fb335 GRID TO GRID**: footer `snap on/off` toggle (visible state — it persisted invisibly in
  the blob) + right-click 'Snap points to grid' (whole-bank quantise; vertical steps survive;
  centre anchor keeps x=0.5). Root cause of "crooked flats": Send To Shaper lands points
  VERBATIM (by law) — mixing off-grid sent points with snapped drags = micro-slants.

## Where we are
THE CURVE ERA is complete and Max-approved through fb335. The Distortion device (3rd FX device,
ONE device / 23 modes / 6 families) has a live transfer-curve core viz on all 23 modes and a full
curve-editor extension card ('crv') for the drawn modes.

## The display laws (hard-won this arc — do not regress)
1. **ONE LINE (fb332)**: the curve editor draws exactly ONE path — the user's curve wearing
   `.mv-stroke` (mockup: `Design/dst-curve-editor-mockup.html`). Never a second/ghost line.
2. **AUTHORITY FLIP (fb333)**: on drawn modes (Shaper 16 / Shaper Asym 17) the DRAWN curve is the
   master picture on BOTH the card and the mini viz (`window.__crvDrawnCurve(N)` + boot pullBlob).
   The engine feed (`getDistortionCurveViz`) never reshapes geometry there — it only LIGHTS the
   line (occupancy y rides the drawn curve; opacity o.o, brightness o.b). Non-drawn modes stay
   feed-shaped (approved fb328 behavior).
3. **BANK A BOOT (fb331)**: SYN_DST_SIG is the shared sig slot AND the Shaper ABCD morph (×3
   sweep since fb330). DST_SIG_DEF Shaper/Asym = 0; `sendToShaper` force-zeros SIG+P7 itself
   (card-only setTypeUI skips dstApplyType's boots — that leak was the "curves don't align" bug,
   probe-proven corrA .218→1.000). THE SHARED-DEFAULT AUDIT LAW: new per-family meaning on a
   shared slot ⇒ audit its default on EVERY boot path.
4. Both windows (main + popped card) each carry the full native list (setDistortionCurves /
   getDistortionCurves / getDistortionCurveViz) — a card window misses natives silently.
   `?card=` regex in the page head AND popOutCardWindow C++ whitelist both include 'crv'.

## Engine state
* 4 user banks A-D (`setUserCurves(a,b,c,d,257)`, per-bank uCvHas_), morph = adjacent-pair blend
  on kneeC_×3 for drawn modes; cvMk_[4][2] slope-norm (unity law); 40 ms bake crossfade.
* fb331 probe harness: scratchpad fb331_probe.cpp (corr-per-bank pattern).

## Next (queued, in order)
1. Family pills Wrap/Octave/Track/Clean (UI-only today; Slam+Sym wired).
2. Quality dropdown real (oversampling ladder + ADAA inside OS region).
3. Phase E per-osc routing — ⚠️ MUST add dstSend at PluginProcessor.cpp:6979/:7111 (fb305 trap).
4. Table osc-frame sources (needs engine frame-upload path).
5. Per-point modulation (fb238 machinery) + Morph as mod destination.
6. FX order 6-way permutation (SYN_FX_ORDER is a bool today; Distortion pinned last).
7. Phase G full sweep (dst_matrix + perceptual harnesses over all 23 modes).

## Known residue
* Saved sessions from before fb331 can carry a leaked SIG (e.g. 0.65) — user drags Morph
  hard-left once; no auto-heal by design.
* 'Harmonics':40 / 'Table':35 sig boots unaudited against their kneeC_ meanings.
* pluginval VST3 editor-test flaky ~1/3 (known, not a bug).
