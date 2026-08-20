# FX4 UI — Equalizer · Widen · Compress · Multiband — the design contract (fb436)

**Status: MOCKUP, awaiting Max's approval.** `Design/fx4/mockup/fx4-ui-mockup.html`
(built by `build-fx4-ui.py` — lifts the shipped card verbatim through `fx3lift.py`, embeds the
four real worklets, adds the four cores). Nothing in `Source/` changes until he approves.

## 0. What this pass found

* **`CORES` has no `eqz / wid / cmp / ott` entry**, and `devHTML()` calls `CORES[d.core]()`
  unguarded (`index.html:9001`). Adding any of the four from the ＋ menu in the plugin throws inside
  the rack module's try/catch — the card never renders. The UI pass is what makes them appear.
* No `__fx4VizPush` lane, no `FX_FMT` entries, and the per-Type relabels the templates promise
  (Widen's hero = `frontNames(type)`, the EQ's `Trait` = `shapeName(type)`) are not wired.
* The Pro-Q-grammar drag UI the bible names (`redrawEqHandles`) lives in the synth-panel EQ; R12
  recycles its *grammar* (drag X=Hz / Y=gain, wheel=shape, dbl-click reset, delta-based gain) as
  SVG inside the card — a canvas inside the 226×78 core would not be the house (fb403).

## 1. Shared laws (all four cores)

* `<svg viewBox="0 0 226 78" preserveAspectRatio="none">`, plot **X 6.5 → 219.5**, centre **y 39**
  (the shared rack plot bounds, fb360). **Log-f axis 20 Hz → 20 kHz** across the plot — the SAME
  axis on the Equalizer and the Multiband, so a crossover line and an EQ node at the same x are the
  same frequency. `hzX(hz) = 6.5 + 213·log(hz/20)/log(1000)`.
* **White = structure and controls** (`currentColor`, `.dst-curve` for the hero line), **purple
  `#B794FF` = the live signal**. No grey fills, no glow, no blur. Audio brightness lives on
  separate `.*-glow` / purple elements — **never a per-frame opacity on a `.dst-curve`** (strobe law).
* Everything drawn **glides** toward the push: dB linearly (α≈0.3–0.5), **Hz in log space**
  (fb163). Idle = dim, playing = bright from `lvl` (fb311). Nothing free-runs (fb325).
* One rAF loop (`fx4Tick`) for all four, reading `window.__fx4VizPush = {eqz:[6],wid:[6],cmp:[6],ott:[6]}`
  (null per inactive instance) on the 60 Hz C++ lane (fb354 push-not-poll).
* Readout law: every knob whose value means something prints it (§5). Switch law: Voices detents.
  Double-click = template default. Drag starts from the model (fb364).

## 2. Equalizer — THE LIVING CURVE

Layers, bottom → top:
1. grid: hairlines at 100 / 1k / 10k (white .07) + the 0 dB centre line (white .13)
2. `.eqz-spec` — the spectrum, **the filter card's grammar** (white .07 fill, .34 stroke @ .7):
   v1 = the main output FFT (`__fltBinMag/__fltSpecY`, exactly what the Filter card draws, zero new
   CPU); v2 option = per-device pre/post tap for the bible's "delta ink" (needs C++ analyzers).
3. `.eqz-fill` — the area between the curve and 0 dB, purple, opacity `.04 + .12·lvl`
4. `.eqz-glow` — purple 3.4 stroke under the curve, opacity `.05 + .30·lvl` (fx3 precedent)
5. `.eqz-curve.dst-curve` — the summed response from **`viz.curve[96]`** (the engine evaluates it
   from its LIVE ramped coefficients — the drawn curve cannot disagree with the audio). Drawn as
   `effDb(c, mix) = 20·log10|(1−m) + m·10^(c/20)|` so **Mix below 100 visibly flattens it**.
6. `.eqz-n` × 4 — **the nodes**: white ring r 3.2 with a dark centre, at `hzX(nodeHz[b])`,
   `dbY(nodeDb[b])` — the engine's APPLIED centre and gain (post-law, post-Amount; **under the
   Dynamic type they ride the audio** — the Pro-Q/TDR grammar for free). Held node = purple ring.
7. `.eqz-ro` — a 7 px readout top-left while dragging (`Body  550  +4.5`), fades after 900 ms.

dB scale: ±30 dB → 1.1 units/dB, clamped to the plot (Amount 200 % = ±60 clips the frame: honest).

Interaction (the whole core is the target):
* pointerdown: nearest node within 12 units; a miss grabs **the band that owns that part of the
  axis** (<170 Hz Low · <1.5 k Body · <7 k Bite · else Air) — a drag anywhere always works.
* drag X → that band's Hz knob (`LOWHZ/BODYHZ/BITEHZ/REACH`, the engine's own lo/hi per band:
  20–500 · 100–3k · 700–14k · 6k–40k log); drag Y → its gain knob **as a delta from the grab**
  (`LOW/BODY/BITE`, Air = the FRONT `Air` knob) — the node never jumps.
* wheel → `Trait` (P8, the Type's shape slot); double-click → that band to template defaults.
* The nodes and the 8 back knobs + front Air are ONE state: `DEVS[].back.knobs[i][1]` /
  `knobs[1].v` and `__setSynParam`; `__fxRedrawKnobs` after every write (fb363/364).

## 3. Multiband — THE JAWS (on the log-f axis)

* Lanes split at the **live** `xoverHz` (Two Band → one split, third lane hidden). Lane
  boundaries = `.ott-xl` white lines, **draggable** → `Low Cross` / `High Cross` (inverse of the
  engine's `expMap` — 30–300 / 1k–8k, 150–2k on Two Band). Readout while dragging.
* Per lane (dBp scale +6 → −66 over y 4 → 75): `.ott-col` purple level column (40 % lane width)
  from the floor to `bandDb`; two **white jaw lines** spanning 72 % of the lane: the ceiling at
  `tdn − max(0,gr)` and the floor at `tup + max(0,−gr)`; **purple bite rects** between each jaw's
  rest position and its live edge (what is being eaten / poured, right now); a dotted rest mark
  appears only once a jaw has moved > .3 dB.
* Push payload: `grDb[3]` (signed), `bandDb[3]`, `xoverHz[2]`, **`tdn[3]`, `tup[3]`** (added to
  the worklet post this pass; C++ gets them from `thresholdDn/Up(b)`), `lvl`, `bands`.

## 4. Compress — THE PRESS RIVER + THE KNEE

* Left (x 6.5–138): a 150-frame history of `inDb` (dim white outline .42) and `outDb` (white .06
  body + `.cmp-outl.dst-curve` top line); `.cmp-gap` purple fill between them = the crushed dB;
  `.cmp-thr` white ceiling bar at `thrDb` (Push) with `.cmp-press` purple pressing down by the
  live GR; `.cmp-gr` readout top-right (`−7.3`). dBp +6 → −60.
* Right (x 146–219.5): `.cmp-knee.dst-curve` from `viz.knee[32]` (input −60…+12 → x, output → y),
  the 1:1 diagonal dashed (the saturate core's grammar), `.cmp-ball` purple at the live input.
* Push payload: `grDb, inDb, outDb, knee[32]` (send knee only when changed), `lvl`, **`thrDb,
  kneeDb, ratio, attackMs, releaseMs`** (for the readouts — Type-dependent, only the engine knows).

## 5. Widen — THE VOICE FAN + THE CORRELATION RAIL

* Apex (113,70). `.wid-dry` white centre beam (.30 + .40·lvl). `.wid-v` × nV purple beams, angle
  `pan·0.78 rad`, length `54·(0.7+0.3·lvl)`, tip leaning `cents/100·5` (sways with the engine's own
  LFO/walk); beams ≥ nV hidden. Mono pill closes the fan (the engine's pans do it).
* Rail y 75: white line, left half dashed = mono-danger (r<0), centre tick, `.wid-r` purple needle
  at `113 + 73·corr`.
* Push payload: `corr, voicePan[8], voiceCents[8], widthNow, lvl, nV` (nV added this pass).

## 6. Readouts (`FX_FMT`, keyed `core|SUFFIX`, engine maps only)

eqz: LOWHZ 20·25^t · BODYHZ 100·30^t · BITEHZ 700·20^t · REACH 6k·6.67^t (Hz) · LOW/BODY/BITE/AIR
±30 dB · SLANT ±24 (`flat` at centre) · AMOUNT 0–200 %. wid: WIDTH `mono`/signed about 50 ·
RATE 0.08·175^t Hz · VOICES 3–8 (`FX_STEP` 6). cmp: PUSH = threshold 9−48·t^0.9 dBp · RATIO/ATTACK/
RELEASE/ROUND from the push · LIFT 0–24 dB. ott: LOWCROSS/HIGHCROSS = live `xoverHz` · RAISE/PRESS
0–1.5× · GRIP ±18 dB · BASS/MIDS/TREBLE ±12 dB. Mix = plain percent everywhere (house).

Relabels on Type change: Widen `knobs[0].l = frontNames(type)[0]`
(Detune/Depth/Cents/Sway/Wash/Cleave); Equalizer `back.knobs[7][0] = shapeName(type)`
(Pinch/Slope/Taper/Dip/Silk/Pivot/Sting). Both from the engines' tables, never a card-side list.

## 7. The build list (after approval — `Source/` only then)

1. `index.html`: `CORES.eqz/wid/cmp/ott` (the four generators verbatim), `fx4Tick()` + the four
   draw functions, the node + crossover drag handlers, `FX4_FMT` merged into `FX_FMT`, `FX_STEP`
   Voices, the two relabels in the Type-change handler, back-panel `fxr-bk-knob` redraw on node drag.
2. `PluginProcessor`: `getFx4VizJson()` mirroring `getFx3VizJson()` — eqz curve gated to ~20 Hz on
   change (96 × 60 Hz × 6 instances would exceed the fb342 40–80 KB/s line), everything else per
   frame, null for inactive instances; Compress/OTT expose `thrDb/kneeDb/ratio/atk/rel` and
   `tdn/tup` via the existing getters.
3. `PluginEditor`: `js << "window.__fx4VizPush=" << getFx4VizJson()` on the same 60 Hz line.
4. Gates: `Tests/fx4_ui.js` (headless, fb413's `fx3_ui.js` shape): node drag writes the Hz+gain
   params and moves the back knob; crossover drag writes Low/High Cross; tick moves every path with
   a synthetic push; no inline opacity on any `.dst-curve`; every label of the four in the roster;
   run it against the pre-change tree first (a gate that cannot fail is worth nothing).
5. Build both formats, embed gate, pluginval ×3 both, `auval -v aumu Tern Wvcr` **exit code**.

## 7b. What shipped beyond the list (fb437 → fb438)

* **fb437** — everything in §7 built and installed; `Tests/fx4_ui.js` 39/39 (fails on the old tree
  with `cards=0`); route pill lit = white; EQ `Delta` pill; the generic restore (cho/fla/pha + the
  four re-rendered from the TEMPLATE on reopen); `window.__fxRedrawKnobs` (the filter card had called
  it undefined since fb384).
* **fb438** — Max's review changes: EQ nodes white, small, **pinned on the drawn line**; Compress no
  number + transparent; Widen blooms; the cassette card redrawn (big white line-art shell, thin white
  meters). **Character names per Type** on all seven relabelling devices (`FX_CHARS`, mirrored from
  the engines by `Tests/fx_chars_dump.cpp`, diffed by the gate). **Presets**: a per-core namespace
  + factory table (the old chain filed cho/fla/pha + the four under the reverb's folder and listed
  ITS factory set); 94 factory presets authored from the bibles for the seven. **The free bells**:
  4 extra EQ bands added by double-click / removed by right-click (engine stages 9–12, constant-Q
  bells, surgical by design; `SYN_EQZ_X1..4 HZ / X1..4 / X1..4 ON`; worklet mirrored; eq_cert
  147/147 unchanged; a free bell OFF is bit-exact). Gate 52/52.

## 8. Decided (was: open)

* **Band count → 4 role bands + up to 4 free bells** (Max: "double-click to add dots, right-click to
  delete, like the LFO"). The back-8 stay the four roles; free bells are nodes only.
* Pop-out editor: still later (`TerrainCardWindow`).

## 8-old. Open for Max (one decision, one option)

* **Band count.** This pass keeps the engine's **4 fixed-role bands** (Low/Body/Bite/Air — the
  SSL/console solve; every band draggable on the live spectrum, which is the Ableton/Pro-Q feel).
  Ableton EQ Eight's **8 free bands** (each with its own type/Q/on-off) is an engine + params
  rebuild (~40 params × 6 instances, a selected-band editing model that breaks "nothing hidden" and
  knob=param parity) — a different device, not a UI pass. A middle step that IS cheap: **Low Cut /
  High Cut edge handles** (2 params + two SVF cuts in the engine) — the `‹ ›` handles of the panel EQ.
* **Pop-out.** A bigger EQ editor later = the existing `TerrainCardWindow` pop-out with the same
  core; not v1.
