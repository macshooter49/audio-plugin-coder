# DYNAMICS — the locked roster for TWO devices over ONE core

Devices: **COMPRESS** = chain kind **11** (`SYN_CMP_*`) · **OTT** = chain kind **12** (`SYN_OTT_*`).
Shared core: `DynamicsCore.h`. Engines: `TerrainCompressFx.h`, `TerrainOttFx.h`.
Harness: `dynamics_cert.cpp` (114 gates). Contract: `Design/fx4/CONTRACT.md`.
Bibles: `Design/COMPRESSOR-BUILD-BIBLE.md` + `Design/OTT-BUILD-BIBLE.md`.

**Compress: 8 Types × 8 Characters = 64 voicings. OTT: 8 × 8 = 64.**
Each device: 3 front knobs + Mix + 8 back knobs + 2 back dropdowns = 12 params + Character + axis.

---

## 0. The one law that governs both devices

Every threshold in every compressor manual on earth is stated against a **0 dBFS** programme.
Terrain's FX bus is not that bus. A single note arrives at **−26 dBFS** (`PluginProcessor.cpp:46`
measures −20 dBFS at the master; `kVoiceToFxPad = 0.5f` at `:6300` pads the send 6 dB below it);
the reference chord sits at ≈ −20 dBFS. Port an LA-2A's −20 dB threshold or Vital's −28 and **the
programme never crosses it**: the device ships dead, and every gate you write passes because the
engine is, in isolation, correct.

So both devices work in **dBp** — 0 dBp = −26.02 dBFS = single-note nominal, chord ≈ +6 dBp — and
`DynamicsCore.h` owns the constant. The audio path is untouched; only the detector is lifted.

**This is also the one place where the bible turned out to be wrong, and we believed the
measurement instead.** See `FINDINGS.md` §2: the OTT bible's threshold table, ported exactly,
produced 19/26/29 dB of gain reduction per band on the reference chord — far past the 8–18 dB the
same bible gates for — and landed the device **10.7 dB below unity**. The tables below are
re-derived against **measured** band envelopes on this bus.

---

## 1. Why these are TWO devices (R2), and where the line is

| | COMPRESS | OTT |
|---|---|---|
| Bands | ONE | THREE (two on `Two Band`) |
| Computers | one downward, plus an upward lane on `Ride` only | SIX — downward AND upward, per band, simultaneously |
| Identity | topology: what the detector hears, what the gain element does while it works | calibration: how hard both jaws close on each band |
| The knob you reach for | `Push` — how far it digs in | `Amount` — how hard both jaws close |

The rack is duplicatable and chainable, so a combined device would still make you instantiate two
cards to get both jobs with half the knobs dead in each mode. Neither roster grows across the line:
Compress's `Ride` is **single-band** up+down (the OTT boundary Type); OTT never loses its bands.

---

# COMPRESS — chain kind 11

## 2. Types — the 8-entry header pill

Eight **topologies**, not eight voicings. A compressor is four blocks — detector → gain computer →
ballistics → gain element — and the lineages differ by **which block dominates**. Feedback vs
feedforward is a wiring difference (`d[n] = |y[n−1]|`, tapped **pre-colour** so the gain element's
own harmonics never re-enter its own detector; a 0.1 ms one-pole on the rectified tap is the
limit-cycle clamp).

| # | Type | Lineage | Mechanism (what is physically different) | Discriminator | Measured |
|---|------|---------|------------------------------------------|---------------|----------|
| 1 | **Exact** | digital VCA / Pro-C "Clean" | Feedforward, exact Giannoulis knee, plain exponential ballistics. The curve you set is the curve you get. | output slope above threshold at ∞:1 | **0.002 dB/dB** (flat) |
| 2 | **Bus** | SSL 4000 / Cytomic's Glue | FF + the **diode level-adaptive attack** (small overs attack up to 5× slower) + a **dual-pool auto release** whose slow pool also FILLS slowly, so a short tap and a long ride recover differently | release after a 600 ms ride ÷ release after a 50 ms tap | **12.2×** (Exact: 1.00×) |
| 3 | **FET 76** | UREI 1176 | **Feedback.** 20–800 µs attack window. Odd-harmonic gain element, Colour floor 0.20. Push maps to detector drive (the hardware has no threshold — you crank Input) | time to 63 % GR at mid-window | **0.12 ms** (Exact: 5.2 ms) · 21 µs at knob 0 |
| 4 | **Opto** | Teletronix LA-2A T4 cell | **Feedback**, and the release is **two pools plus a 10 s memory integrator**: the slow pool's constant grows the longer the cell has been lit | fraction of peak GR still present at 5 measured release constants | **17.0 %** (Exact: 1.5 %) |
| 5 | **Vari-Mu** | Fairchild 670 | **Feedback**, and the **slope grows with GR**: `s_eff = s·(1 + GR/18dB)`. Hit it harder and the curve steepens. Release to 25 s | realised GR-per-dB at +12 dB of extra drive | **0.729** (Exact: 0.643) |
| 6 | **OverEasy** | dbx 160/165, Blackmer | True-RMS detector (Attack knob **is** the RMS window), knee floored at 6 dB, and **the Ratio knob continues past ∞ into slope −1** — dbx's shipped "Infinity+" | measured knee width; and the negative zone | knee **12.7 dB**; `Anti` gives **−6.02 dB out for +6 dB in** |
| 7 | **Ride** | the OTT boundary Type | Downward **and upward** in one band, feedforward only (upward gain inside a feedback loop is a genuine runaway). T_up sits 6 dB under T_dn so both jaws close on a narrow window | gain applied to a −40 dBp probe | **+10.50 dB** (every other Type: 0.00) |
| 8 | **Limit** | Serum 2 "Limit" + the in-tree fb264 master limiter | ∞ slope, peak+1 ms-hold detector, 0.1–5 ms attack, and a soft-clip catch scaled to sit **exactly 1 dB over the ceiling** | peak overshoot above its own ceiling | **+0.71 dB** (Exact at the same knobs: +5.03 dB) |

**Cross-type distinctness: every pair ≥ 3× JND; closest pair 5.86× (Opto / Vari-Mu).** Eleven
phase-independent features (GR · attack · release · dynamic-range removed · THD · output slope ·
knee width · staircase DRR · gain on a quiet probe · release adaptation · second-time-constant
survival). Full matrix and JND definitions in `FINDINGS.md`.

### ✂️ Cut, and why
- **Nothing was cut from the Type list** — all eight survived measurement.
- **The `Grab` pill** (attack and release ×0.25 on one click, from the bible's §5.1). Cut: the
  Characters already own that axis honestly (`Fast City`, `Hard Stop`, `Fast Clamp`), and a pill
  that silently re-times two back knobs makes their readouts lie.
- **A 9th `Smooth` Type** (Airwindows ButterComp) was already cut in the bible for having no
  night-and-day discriminator. Confirmed: its whole point is imperceptibility.

## 3. Characters — 8 per Type, each re-wiring PHYSICS

The table is data, not code branches: `attack ×` · `release ×` · window ends · knee · slope
multiplier / cap / **floor** · threshold offset · colour floor · detector tilt · forced link ·
opto memory · GR-slope coupling · damping ζ · RMS window × · minimum Hear Cut · upward slope /
threshold / cap · forced detector · release SHAPE · flags. Nothing in it is a tone control.

| Type | 8 Characters — what each re-wires |
|---|---|
| **Exact** | `Precise` the reference · `Soft Touch` the knee auto-widens by up to 26 dB below 12 dB of GR · `Loose Grip` the slope is capped at **2.5:1** with 8 dB of extra knee — the clamp never gets tight · `Blunt` the curve is applied **twice at half slope**, which rounds the corner and roughly halves the reduction at the top (⚠️ both of these were `RMS Ears` and `Spike Ears`, which set `detForce` and SILENTLY OVERRODE the `Detect` dropdown: pick `RMS Ears`, then `Detect → Peak`, and the card still read `RMS Ears` while a peak detector ran. That is a visible label disagreeing with the DSP — the fb373 failure mode — and two controls on one axis, which is what fb418 fixed on the flanger. `detForce` is DELETED FROM THE STRUCT, not merely unused, and `Detect` owns detection outright; cert §4c2 asserts it over all 256 Type×Character×Detect combinations via the engine's own `detectId()`) · `Deep Release` τR scales with GR · `Line Attack` a linear-in-dB attack RAMP, not an RC · `Poise` a critically-damped 2nd-order smoother (ζ = 1.0) · `Judder` ζ = 0.42, a controlled release overshoot |
| **Bus** | `Quad Bus` dual-pool auto release, link locked 100 · `Hand Set` auto release OFF, window recentred on 400 ms · `Two Easy` 2:1 cap, slow attack, +2 dB threshold · `Ten Punchy` slope floored at 0.75 (4:1 minimum) · `Fast City` attack ×0.03 — the Glue's documented 0.01 ms extremity · `Big Desk` attack ×2.2 and Heat floored at 0.65 · `Pump Bus` release ×0.12, the EDM breathe · `No Diode` the level-adaptive attack is REMOVED (plain RC) |
| **FET 76** | `Blackface` Heat 0.20 · `Blue Stripe` Heat 0.55 + asymmetric (DC blocker engages) · `All Buttons` the British mode: knee 10, a GR-coupled wandering slope, ζ = 0.6 release wobble · `Twenty Lock` slope floored at 0.95 (20:1+), attack ×0.15, −4 dB threshold · `Loose Four` 4:1 cap + GR-dependent release · `Broken Bias` Heat 0.65, asymmetric FET · `Waiting Fet` the whole attack WINDOW ×20 — an 1176 that can wait · `Two Pass` two half-depth stages in series |
| **Opto** | `Cell Classic` +0.35 detector tilt · `Fresh Cell` release ×0.30, memory ×0.18 · `Tired Cell` release ×4.5, memory ×7 · `Quick Cell` both pools ×0.25, memory ×0.45 · `Even Pools` the pool mix moves to **0.28/0.72** — toward the SLOW pool, which is what makes the two-stage release audible (the first draft moved it the other way and measured 0.76× JND) · `Crystal` attack ×4, knee +14, release ×1.8 — an invisible ride · `Tube Stage` Heat floored at 0.40 · `Bright Ears` detector tilt ×7 — highs duck the patch |
| **Vari-Mu** | `Studio 670` Heat 0.25 · `Time One` TC position 1: attack window ×[1, 0.1], release [100, 900] ms · `Time Four` attack ×[4, 0.4], release [1.5, 15] s · `Auto Peaks` dual-pool release on a fast window · `Long Haul` release window [2, 25] s ×2 — the 25 s ocean · `Push Pull` Heat 0.60, asymmetric — driven iron · `Lateral` **M/S detector split**: mid compressed, sides ride · `Triode Soft` knee +8, 4:1 cap, and the GR-slope coupling **inverted** |
| **OverEasy** | `Over Easy` knee +6 · `Hard 160` knee forced to 0 · `Infinity` ∞:1 reached at ~66 % of the Ratio knob · `Infinity Plus` the negative zone spread across the top 40 % · `Slow Window` RMS window ×9, release ×1.8 · `Crush RMS` RMS window ×0.10 — RMS chatter grit · `Decilinear` Heat floored at 0.40 (log-domain error) · `Anti` slope FLOORED at 2.0 — slope −1 everywhere. Louder in, quieter out |
| **Ride** | `Level Rider` · `Deep Floor` upward slope ×1.35, T_up +10 dB, cap 36 dB · `Only Up` downward OFF — pure upward compression · `Only Down` upward OFF — a dense leveller · `Fast Clamp` attack ×0.06, release ×0.25 · `Slow Iron` ×2 / ×4 — invisible levelling · `Bright Bias` detector tilt ×7, upward slope ×1.15 · `Vocal Sit` Hear Cut floored at 260 Hz, detector tilt −2, release ×0.5 |
| **Limit** | `Clean Wall` · `Soft Ceiling` knee 6 into the clamp · `Hard Stop` attack ×0.02 · `Pump Limit` release window ×4, −6 dB ceiling · `Loud War` auto-makeup forced FULL — the maximizer · `Clip Guard` the soft-clip catch is always on, −3 dB ceiling · `Springy` release ×2.5 and GR-dependent · `Porous` **6:1 above the ceiling instead of ∞** — an over-limit leak that keeps 2–3 dB of life |

**Every Character ≥ 2× JND from its Type's default** (weakest pair named in `FINDINGS.md`).
Four of these rows were *rewritten* because the harness proved them to be no-ops. That is the
gate doing its job.

## 4. The chassis

### 4.1 Front — 3 heroes + Mix

| Slot | Name | Range | What it DOES | Default |
|---|---|---|---|---|
| 1 | **Push** | T = +9 → −39 dBp, `t^0.9` in dB | How far the compressor digs into the sound. 0 touches nothing; 100 puts the threshold 39 dB inside the programme. | 0.20 |
| 2 | **Ratio** | 1:1 → **∞:1** (OverEasy: → −1:1) | How hard it holds what it catches. `s = 1 − 1/R = t^0.85` — GR-per-dB-over is LINEAR in the knob, so every degree of travel adds the same audible dB of squash. **∞:1 is reached at 1.0 exactly.** | 0.50 |
| 3 | **Lift** | 0 → +24 dB | Brings the level back up after squashing. | 0.25 |
| 4 | **Mix** | 0 → 100 % | 100 % = fully wet, ZERO dry. The crossfade is exactly linear against the untouched input, verified to **−144 dB**. | 1.0 |
| pill | **Auto** | off / on | Auto-makeup from the STATIC curve at the CHORD (fb249), 70 % / 300 ms. Default **OFF** — full compensation turns a Push sweep into a timbre-only change and deletes the "louder AND denser" that reads as power. |

At the defaults the card **does something without changing the level**: 6.8 dB of gain reduction,
**−0.84 dB** against bypass on the reference chord.

### 4.2 Back — dropdown 1 `Character`, dropdown 2 `Detect`, 8 knobs 4×2

**The second dropdown is `Detect`, and it is NOT `Type`** (R6). `Type` is the header pill.
`Detect` = `Auto` · `Peak` · `Average` · `Long` · `Spike` — five different **rectifiers /
averagers**, i.e. five different things the compressor physically hears. `Auto` gives the Type its
native ears. This is the one device in the rack where swapping the usual `Quality` dropdown for a
detector selector is honest, because there is no oversampling to tier.

| Pos | Name | Range | Does |
|---|---|---|---|
| P1 | **Attack** | per-Type window, log | How fast it grabs. FET 76 spans 20–800 µs; Exact 0.05–300 ms. On OverEasy the knob IS the RMS window. |
| P2 | **Release** | per-Type window, log | How fast it lets go. Feedforward Types floor at **5 ms** — the bottom decade is where the gain tracks the waveform inside its own period and the compressor becomes a waveshaper. Feedback Types keep the 20 ms stability floor. |
| P3 | **Round** | 0 → 48 dB | How round the corner is. At 48 the compression begins **24 dB below** the threshold. |
| P4 | **Hear Cut** | Off → 500 Hz | Cuts lows out of what the compressor HEARS. Detector only; the audio is untouched, so bass stops pumping the whole patch. |
| P5 | **Edge** | −100 → 0 → +100 | + : the detector goes blind for the first ~3 ms of every hit, so transients escape over a crushed sustain. − : attacks get +24 dB of EXTRA reduction and every note becomes a swell. |
| P6 | **Latch** | 0 → 250 ms | Freezes the clamp before release runs — pump shaping, bass de-chatter. Bit-bypassed at 0. |
| P7 | **Tie** | 0 → 100 % | 100 = one clamp for both channels (solid image, the console law); 0 = each side breathes alone. Gain-only, so mono-safe either way. Measured on an unbalanced probe: **+16.90 dB vs +8.16 dB** of L−R balance. |
| P8 | **Heat** | 0 → 100 | How much the gain element distorts while it works. **Scaled by current GR**, so a patch with no compression is bit-clean at any Heat, and the dirt BREATHES with the compression. |

### 4.3 Viz (contract §2)
`grDb` (+ve = reduction) · `inDb` · `outDb` · `knee[32]` (the published transfer curve, −60…+12 dBp)
· `lvl`. The knee array is live at idle too (curves-must-move).

## 5. The R11 ceiling — Compress

Three legs, each with a metric and a threshold, all measured:

| Leg | Metric | Threshold | Measured |
|---|---|---|---|
| (a) | Dynamic-range ratio of a **48 dB staircase** at Push 100 / Ratio 100 | ≤ 0.05 | **0.0158** (bypassed control: 1.0000) |
| (b) | Attack reaches the microsecond decade | ≤ 0.05 ms | **21 µs** (FET 76 at Attack 0) |
| (c) | Added THD on an 80 Hz sine at Attack 0 / Release 0 / Push 100 | ≥ 10 % | **13.3 %** (bypassed control: 0.126 %) |

**Why these thresholds.** (a) Ratio 100 is `s = 1`, i.e. ∞:1 *exactly*, and Push 100 puts the
threshold 39 dB inside the programme, so every tread of the staircase is over threshold. If more
than 5 % of a 48 dB span survives, the ratio law never actually reached infinity — the gate tests
the *claim*, not a taste. (b) 20 µs is the 1176's own documented floor and one sample at 48 k.
(c) At an attack of 20 µs and a release of 5 ms the gain tracks the waveform **inside** its own
period below ~500 Hz. That is precisely how a real 1176 grinds LF, it is the audible top of the
Release range, and 10 % THD is unambiguous — the same measurement through a bypassed engine reads
0.126 %, so the scale is legible.

Plus, said the way a listener would: at Push/Ratio 100 the reference chord's crest collapses from
9.25 dB toward a **sine's 3.01 dB**.

---

# OTT — chain kind 12

## 6. The device name — recommendation

**Recommend `OTT`.** Candidates were `OTT` and `Jaws`.
- It is a real acronym (Over The Top), so caps are legal under the type mandate; it is a **genre
  term**, not a trademark string — the effect is universally called this, including by Max.
- `Jaws` names the *visualiser*, not the function. The pragmatic-name rule says the name should
  say what it does; between two names that neither fully does, the one the user already searches
  for wins.
- And the operational reason: the param IDs are `SYN_OTT_*` regardless. A card labelled `Jaws`
  driving `SYN_OTT_*` params is exactly the shape of the fb373 bug — a visible label and the
  thing behind it disagreeing. Keep them the same word.

## 7. Types — the 8-entry header pill

| # | Type | The idea | Key deltas | Discriminator | Measured |
|---|------|----------|-----------|---------------|----------|
| 1 | **Over Top** | The Xfer/Ableton calibration, re-derived for this bus | 88.3 Hz / 2.5 kHz; 2.8/40 · 1.4/28 · 0.7/15 ms; slopes dn 0.90/0.857/1.0, up 0.8 | a 20 dB level step must come out compressed | see `FINDINGS.md` §4 — and a **30 dB** step (which crosses BOTH thresholds) is the honest family tell |
| 2 | **Gentle** | Ableton with taste / MO-TT "Smooth" | 25 ms RMS pre-average, ballistics ×4, **12 dB knee**, slopes ×0.7, thresholds 6 dB deeper to reach the pre-averaged level at all | gain-ripple THD on a 100 Hz sine at full Amount | far below Over Top's |
| 3 | **Heavy** | OTT on OTT | ∞:1 everywhere, thresholds 6 dB deeper, up 0.9, **per-band cubic soft clip** at T_dn + 3 dB | dynamic range removed | more than Over Top |
| 4 | **Sheen** | Max's mandate — the "expensive top end" | X-High down to **1.8 kHz**, high-band floor moved toward the programme, **0.35/8 ms** so the shimmer reads as texture not pumping | 8–12 kHz on a genuinely dark pad | **+53.3 dB vs Over Top's +33.3 dB** |
| 5 | **Bass Safe** | Every mix engineer's OTT complaint, fixed | Low band: **upward OFF**, slope 0.75, 10/120 ms, **mono-summed detection**, threshold +4 dB | harmonics MANUFACTURED from a settled 50 Hz tone | see `FINDINGS.md` §5 |
| 6 | **Surge** | Pure upward — resurrection, no squash | All downward slopes **0**; up 0.85; makeup 0 (it is already unity on anything loud, by construction) | signed GR never positive; pluck tail rises relative to its own attack | GR **−0.00 / +0.00 / −0.00**; tail **+5.6 dB** |
| 7 | **Two Band** | Body / sparkle, kHs-style | ONE split at 650 Hz. The mid disappears, so a loud 1 k and a quiet 5 k share ONE detector and **duck each other** | how much a quiet 5 kHz tone loses when a loud 500 Hz arrives | **15.98 dB** (Over Top: 0.84 dB) |
| 8 | **Stagger** | Bands that decouple in TIME | 40/700 · 1.4/28 · 0.10/2.5 ms — a **280:1** ballistic spread against Over Top's 4:1, so the spectrum MORPHS through the note instead of holding still | extra HF-vs-LF tilt swing across a decaying note, vs the dry | see `FINDINGS.md` |

### ✂️ CUT: `Quad` (the bible's 8th Type)
The 4-band variant is **cut**, and it is the only Type cut from either roster. Reason: the contract
locks OTT's Viz to `grDb[3]` and `xoverHz[2]`. A four-band device drawn on a three-lane viz would
have to lie about its own band count — and "everything audible interacts visually" is a hard rule,
not a nice-to-have. The bible's §12 Q6 flags the Type list as a **pre-ship-only** decision (rack law
C freezes choice cardinality at birth), so this is the moment. `Stagger` takes the slot: it is
3-band, and its mechanism (ballistic decoupling) is one nothing else in either roster owns.

**Cross-type distinctness: every pair ≥ 3× JND.** Thirteen features — three per-band GRs, air,
level step, ripple THD, dynamic range removed, tail rise, cross-band ducking, tilt trajectory,
broadband settle time, **per-band settle times measured separately on a 50 Hz and an 8 kHz probe**,
and manufactured bass grit.

## 8. Characters — 8 per Type

| Type | 8 Characters |
|---|---|
| **Over Top** | `Straight Up` · `Sharp Ears` **instant-attack peak** detection (the computers see the crest, not the mean square) · `Long Ears` a 60 ms RMS pre-average · `Wide Corner` 44 dB knee · `One Detector` all three bands driven by the LOUDEST band — the console's one-sidechain law, which turns the device into a full-band ducker · `Slow Low` ballistic spread exponent 2.2 · `Twice Deep` both thresholds 6 dB further apart · `Full Crest` attack ×0.35 with the transient upward-hold baked in |
| **Gentle** | `Round Corner` · `Slow Hands` release ×4.5 · `Long Window` 60 ms RMS pre-average · `Half Slopes` both slopes ×0.5 · `Long Tail` the release SLOWS with a 300 ms memory of how hard the band has been working — so it stays slow **after** the note · `Soft Top` high-band ears −9 dB · `Even Bands` **spread 0** — all three bands share the mid band's timing · `Barely There` slopes ×0.35, thresholds 6 dB apart |
| **Heavy** | `Welded Shut` · `Band Clip` the per-band clipper moves to T_dn + 1 dB · `No Clip` the clipper is **removed** · `Deeper Jaws` thresholds ±8, cap 30 · `Fast Grind` ballistics ×0.25 · `Peak Grab` instant-attack peak ears · `Wall Ears` one shared detector · `Total Squeeze` slopes ×1.2, cap 30, clipper at T_dn + 2 |
| **Sheen** | `Top Sheet` · `Higher Split` X-High ×1.6 · `Lower Split` ×0.6 · `Glass Ceiling` T_up −10, knee 0, cap 10, clipper at +4 — a hard lid on the air · `Slow Shimmer` high-band times ×4 · `Fast Shimmer` ×0.25 · `Dark Source` high-band ears +6 dB, cap 30 · `Sheen Wall` upward ×1.25, T_up +8, cap 30 |
| **Bass Safe** | `Anchor Low` · `Mono Low` spread 1.9 with the low detector mono-summed · `Slower Low` release ×2 · `Low Ceiling` low threshold −4, knee 0 · `Reese Guard` downward ×1.25, Low Cross ×1.7 · `Free Low` the low band **stops being special**: it gets its upward computer back, its stereo detector back, and the mid band's timing · `Wide Corner Low` 34 dB knee · `Tight Low` ballistics ×0.4, Low Cross ×0.6 |
| **Surge** | `Tail Riser` · `Deep Riser` upward ×1.15, T_up +6, cap 36 · `Slow Riser` release ×3 · `Fast Riser` ×0.25 · `Capped Riser` the lift is capped at **5 dB** · `Top Riser` high band ×0.3 in time, X-High ×0.7, cap 30 · `Low Riser` spread 2.6, mono low, memory-slowed release · `Riser Wall` T_up +10, cap 36 |
| **Two Band** | `Body Sparkle` · `Low Split` ~300 Hz · `High Split` ~1.4 kHz · `Hard Body` downward ×1.35, threshold −5, knee 0 · `Soft Body` downward ×0.6, knee 14 · `Sparkle Wall` upward ×1.2, T_up +6, cap 30, clipper at +4 · `Slow Pair` ×2.5 · `Fast Pair` ×0.2 |
| **Stagger** | `Time Spread` · `Wider Spread` exponent 2.1 · `Narrow Spread` 0.5 · `Reverse Spread` **−1** — the low band becomes the FAST one and the high band the slow one · `Slow Anchor` Low Cross ×1.8, mono low · `Fast Top` high-band times ×0.25 · `Deep Spread` thresholds ±6, cap 30 · `Spread Wall` exponent 1.3, upward ×1.1, knee 0, clipper at +5 |

## 9. The chassis

### 9.1 Front — 3 heroes + Mix

| Slot | Name | Range | What it DOES | Default |
|---|---|---|---|---|
| 1 | **Amount** | 0..1 | THE knob. How hard both jaws close. ≤ 0.5 scales the slopes off zero and **scales the makeup with them** (so Amount 0 is genuinely unity, not a +13 dB gain stage); > 0.5 drives both slopes to their maxima AND closes the two thresholds on each other until, at 1.0, **T_up MEETS T_dn** and the band's output is a constant. | 0.50 |
| 2 | **Chase** | ×20 … ×0.05 of the Type's ballistics, log | How hard the six followers chase the signal. Left = breathing walls; right = the gain tracks the waveform itself and bass turns to fuzz. **400:1 of travel.** | 0.50 |
| 3 | **Top Lift** | 0..1 | How much quiet top gets resurrected. The whole top-band window slides toward the programme (T_dn +10, T_up +14 dB), the upward slope steepens, the cap grows 24 → 36 dB, and X-High moves down 25 % so more spectrum counts as "high". | 0.25 |
| 4 | **Mix** | 0..1 | 100 % = fully wet. **Phase-matched dry** — see §10. | 1.0 |
| pill | **Bite** | off / on | When a transient arrives, hold the UPWARD computer at unity for 10 ms so attacks keep their bite instead of being pre-inflated by the lift that was riding the gap in front of them. Default OFF. |

`Amount 0.5` **is** Ableton's fixed OTT preset by this calibration. It is the floor of the range.

### 9.2 Back — dropdown 1 `Character`, dropdown 2 `Stereo`, 8 knobs 4×2

**The second dropdown is `Stereo`, and it is NOT `Type`** (R6): `Linked` (one shared clamp per
band) · `Twin` (two independent clamps) · `Mid-Side` (two clamps on a rotated basis, side
thresholds 6 dB deeper so M and S see the same `over` and `under` — spectral processing rather than
an accidental widener). All three are detection **topologies**, not tone. Measured on an unbalanced
probe they give three different L−R balances: **+12.04 / +0.80 / +9.71 dB**.

| Pos | Name | Range | Does |
|---|---|---|---|
| P1 | **Low Cross** | 30–300 Hz log (Two Band: 150 Hz–2 kHz) | The low/mid split. |
| P2 | **High Cross** | 1–8 kHz log, clamped ≥ 4× Low Cross | The mid/high split. Below two octaves of separation the mid thins to a phase sliver and the band trims stop meaning anything. |
| P3 | **Raise** | 0–150 % of the Type's upward slopes | The detail / air / tail-resurrection dial. |
| P4 | **Press** | 0–150 % of the downward slopes | The squash / ceiling dial. |
| P5 | **Grip** | ±18 dB | How deep the jaws sit in the programme — OTT's "In Gain" without clipping the input. **Bipolar**: at −18 the UPWARD computer does all the work, at +18 the DOWNWARD one does. |
| P6 | **Bass** | ±12 dB | Low band level (the built-in 3-band EQ). |
| P7 | **Mids** | ±12 dB | Mid band level. |
| P8 | **Treble** | ±12 dB | High band level — **static**, as against Top Lift's **dynamic** lift. Different name, different mechanism (§10). |

### 9.3 Viz (contract §2)
`grDb[3]` **SIGNED** — positive is downward reduction, **negative is upward LIFT**, and that
distinction is the device's whole identity — · `xoverHz[2]` (0 = the band is unused, i.e.
`Two Band`) · `bandDb[3]` · `lvl`.

## 10. The two OTT laws that are easy to get wrong and impossible to hear until you do

**THE MIX LAW.** Σ bands = `AP2(f_lo)·AP2(f_hi)·x` **even at zero compression**, because LR4
recombination is an allpass, not the identity. Blend that against the raw dry at Mix 50 % and you
get comb notches at both crossovers. The dry path therefore goes through **the same** AP cascade.
`LP4 + HP4 = AP2(fc)` is verified in the harness as a **−134.8 dB null**, and the device's own wet
path nulls against its phase-matched dry at Amount 0. Worst notch at Mix 50 %: **0.00 dB**.

**THE FLOOR GATE.** With no input the upward computer pins at its cap and amplifies the noise floor
for ever. Below **−78 dBFS** the upward gain smoothstep-ramps back to unity over 12 dB — not a
comparator, which would gate-flutter on a decaying tail. Measured: 3 s after a note, at Amount 100
with Top Lift 100 and Raise 100, the gap reads **−280 dBFS**. Two OTTs in series still go silent.

## 11. The R11 ceiling — OTT

Ableton's fixed preset is the **floor** of this range, not the ceiling.

| Leg | Metric | Threshold | Measured |
|---|---|---|---|
| (a) | Residual envelope spread (p90−p10 of a 20 ms RMS envelope) at Amount 100, vs at Amount 50 | ≤ 65 % of it, and ≤ 7 dB absolute | probe 22.78 dB → **9.63 dB** at Amount 50 (= the preset) → **5.52 dB** at 100 |
| (b) | Dynamic-range ratio of a **46 dB staircase** at Amount 100 | ≤ 0.10 | **0.0742** (Amount 0 control: 0.9988) |
| (c) | A **−65 dBFS noise bed** lifted into a wall | ≥ +18 dB | **+30.9 dB** (Amount 0 control: +14.0 dB) |
| (d) | ... and true silence stays silent 3 s after the note | ≤ −90 dBFS | **−280 dBFS** |

**Why these thresholds.** At Amount 1.0 `T_up` meets `T_dn` with slopes 1.0 / 0.95, so a band's
output is a constant regardless of input — (b) tests exactly that claim and 46 dB in / 3.4 dB out
is the answer. (c) is R11's own words made measurable: the noise floor is lifted to within ~11 dB
of the programme. (d) is the pair (c) must be measured with, or "lifts the floor" would just mean
"never shuts up".

## 12. Shared vocabulary, and the names that had to change

Reused with identical meaning per contract §4: `Mix` · `Attack` · `Release` · `Ratio` · `Amount` ·
`Character` · `Auto` · `Type` · `Power` · `Stereo` · `Peak`.

**The no-doubles law is gated, not remembered.** `shipped_labels.inc` is a snapshot of every
capitalised quoted string in `Source/` (1762 of them, including `ui/public/index.html`), and the
harness checks all 177 of this roster's Type names, Character names, dropdown entries and knob
labels against it, plus every name against every other name across both devices. Six names moved
because of it:

| Wanted | Already shipped as | Now |
|---|---|---|
| `Squeeze` (the contract's own word for Compress's up+down Type) | a **distortion Character** (`PluginProcessor.cpp:3533`) | **`Ride`** |
| `Glue` (the SSL Type) | a **distortion Character** (same list) | **`Bus`** |
| `Clean` (the reference Type) | shipped | **`Exact`** |
| `Speed` (OTT's ballistics hero) | shipped | **`Chase`** |
| `Duo` (OTT's 2-band Type) | a **shipped phaser Type** | **`Two Band`** |
| `Punch`, `Knee`, `Color`, `Hold`, `Link`, `Smooth`, `Slow`, `Dual`, `Bloom`, `Classic`, `Heavy`(ok), `Air` | shipped knob labels / Characters / the EQ agent's hero | **`Edge`, `Round`, `Burn`, `Cling`, `Tie`, `Average`, `Long`, `Twin`, `Surge`, `Over Top`, —, `Top Lift`** |

`Squash` was unavailable from the start (a shipped SHAPER knob label) and `Air` belongs to the EQ
agent — both honoured. Two near-collisions were left in place deliberately and are named here so
nobody "fixes" them later: `Bass Safe` (Type) vs `Bass` (knob), and `Lift` (Compress knob) vs
`Top Lift` (OTT knob). Different devices, different words, and shortening either at UI time would
break the rule.

## 13. Integration notes for the owner

- Both `Params` structs add **one bool** beyond contract §2 (`autoMakeup`, `bite`), the fx3
  precedent (the phaser added `invert`, the flanger `motion`). Both default `false`, so wiring them
  later is safe and not wiring them at all changes nothing.
- Front knobs are named, not `f1/f2/f3` — also the fx3 precedent (`rate/depth/feedback`).
- `axis` is `Detect` (0..4) on Compress and `Stereo` (0..2) on OTT. Cardinalities are locked at
  birth: **Compress** Type 8 / Character 8 / Detect 5; **OTT** Type 8 / Character 8 / Stereo 3.
- Both engines expose harness-only introspection (`grNow()`, `attackMs()`, `thresholdDn(b)` …).
  It is not part of the integration surface; ignore it.
- **Neither engine allocates outside `prepare`.** Both have crossovers and followers; fb415's
  malloc-on-the-audio-thread came from exactly this shape being copied from the Filter.
- Zero latency, reported zero. No lookahead anywhere in either device.
