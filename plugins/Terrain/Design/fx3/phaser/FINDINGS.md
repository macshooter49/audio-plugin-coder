# PHASER — FINDINGS

What I measured, what surprised me, what I cut, and what I could not prove.
Engine `TerrainPhaserFx.h` · harness `phaser_cert.cpp` · roster `ROSTER.md`.

```
clang++ -O2 -std=c++17 \
  -I <repo>/plugins/TerrainInstrument/Tests/shim \
  -I <repo>/plugins/TerrainInstrument/Source \
  -I <repo>/plugins/TerrainInstrument/Design/fx3/phaser \
  phaser_cert.cpp -o /tmp/phaser_cert && /tmp/phaser_cert
```

**65 gates, 65 passed, 0 FAILED.** Runtime ~15 s. Full output pasted verbatim at the end.

---

## 1. The headline: seven bugs the harness found, and one it found in itself

Every one of these built clean, ran clean, and sounded plausible. None would have been caught by
listening to one preset.

### 🐛 1. `Floor` was a filter, and it capped **every null in the device**
The first build high-passed the `(A{x} − x)` difference at the Floor frequency, exactly as the
bible's §4.2 tooltip implies ("sweep lower bound **+ matching 6 dB/oct wet HP**"). A perfect notch
needs that difference to be *exactly* `−2x` — magnitude **and** phase — so any filter in that path
destroys the null. Measured: a 20 Hz one-pole capped the deepest notch at **−37 dB**, and with Floor
at 1 kHz a 5 kHz notch was capped at **−14 dB**. The device could not make a real notch anywhere.
Fixed by deleting the filter: Floor now raises the sweep clamp so **the lowest notch cannot go below
it**. Below the lowest notch the cascade phase is small, `A ≈ 1`, and the output is dry regardless —
physics already did the job the filter was breaking. Nulls went from −37 dB to **−134 dB**, and the
Floor knob became *more* honest (measured lowest notch 29 / 95 / 309 / 1007 Hz at Floor 10/40/70/100).

### 🐛 2. Barber's descending ladder was not a notch bank at all
The DAFx-15 wrap fix hands a wrapping section its neighbour's state. I detected the wrap with
`pos < prevPos` — which is the wrap condition **only when the ladder ascends**. On the four `Fall`
Characters `pos` decreases every update, so the handoff fired every 16 samples, permanently forcing
`y1 = x1, y2 = x2`, and the descending bank was a different (wrong) filter. The harness saw it as
"Barber/Fall reverses it: 70 % monotone". Direction-aware wrap detection
(`|pos − prev| > M/2`) fixed it: **100 % monotone up and 100 % down**, wrap click −110.9 dBFS.

### 🐛 3. Topology params were applied ~4 ms before the swap dip reached the floor
`setParams` wrote the new stage count, loop topology, LFO shape and `apBlend` immediately, while the
stagger table still belonged to the old plan. For up to a block the cascade ran at the *new* length
with the *old* coefficients **at full gain**. The click test caught it at **−43.6 dBFS**. Fixed by
double-buffering the whole topology class (`t*` staged fields, committed at the dip floor). Now
−74 dBFS on the same sweep. A second instance of the same class: `nA` and the Floor clamp were
computed once per block and went stale when the commit happened mid-block — moved inside the loop.

### 🐛 4. `staggerMul` was wired to two stagger laws out of three
The Vibe cap law and the Barber interval used it; the **geometric** law (which seven Types use) did
not. `Ninety · Wide Stagger` and `Envy · Smooth Follow` were silent no-op Characters. The
Character-distinctness gate reported 0.22 JND, which is what that gate is for.

### 🐛 5. The LDR duty warp warped the wrong thing
`skewTri` warped the phase *inside each half* of the triangle, which changes the shape but leaves
the duty cycle at 50/50. Measured rise/fall asymmetry: **1.13 : 1** — i.e. nothing, on the Type whose
entire identity is asymmetry. Warping the phase *before* the triangle moves the peak, so at skew +1
the rise takes 80 % of the cycle. Now **2.47 : 1**.

### 🐛 6. Series and cross-feed Duo summed phasor B against the wrong reference
`runTopology` returned the raw cascade output and the caller did `x + apBlend·(v − x)`. For series,
phasor B's notch is a notch **of phasor A's output**, not of the device input, so the sum was wrong
and Duo's nulls were not nulls (its Mix-1.0 dry residual measured **−12.5 dB**). Fixed by moving the
sum inside `runTopology` so each stage sums against its own input.

### 🐛 7. `Touch` had a third of the authority the spec assumed
The bible normalises the follower as `env/0.05` (0.05 lin = −26 dBFS). On a real −26 dBFS program a
*peak* follower reads ~0.19, so that clamp sits pinned at 1.0 and the knob reads dead; with a soft
knee it sits near 0.35, so a ±4-octave Touch swung only **±1.1 octaves**. Both are law-1 failures in
opposite directions. Shipped: soft knee `g/(1+g)`, `g = det·10`, and **±8 octaves** — measured span
**6.61 octaves**, monotone and bipolar.

### 🪤 8. And one bug in the harness itself, worth recording
Four gates failed on an engine that was correct, because the *measurement* was wrong:
- The **notch tracker** locked onto "the deepest notch". At Mix 1.0 every notch is a true null, so
  which one is deepest is decided by measurement noise — the estimate hopped between notches and the
  Touch sweep came out **inverted**. Replaced with the position of the whole comb **pattern**
  (frame-to-frame normalised cross-correlation of the log-band gain curve, integrated).
- The **Center / Floor** sweeps looked non-monotonic because at Center 0 the first notch sits at
  20 Hz, off the bottom of the analysis grid, so the detector reported notch #2.
- The **Type-swap "bang"** measured +5.1 dB — at Rate 0.35 the LFO period is 7.5 s, so the two halves
  of a 2 s buffer sat at different points of the *sweep*. At 3 Hz the same swap measures **−0.35 dB**.
- The **mono gate** failed Barber at 3.0 dB because a Q≈11 notch is *narrower than a 1/10-octave
  analysis band*; short-time band averaging diluted it. Measured from the mono-sum impulse response
  at full resolution it is **6.5 dB**.

The general lesson, which is the fb345 probe-craft law again: **when a gate fails, the first
question is whether the probe can see the thing.** Four of nine failures at one point in this build
were the harness, not the engine — and two of the engine bugs (1 and 5) were only visible *because*
the probe was right.

---

## 2. Discriminator design — how a phaser is measured at all

The project's standard dramaticism metric is a magnitude-spectrum distance. **It reports ~0 dB for a
phaser**, because the wet path is all-pass and its magnitude spectrum is flat by construction. Worse,
the banned metric (sample-difference RMS) reports ~100 % for an inaudible change — fb282 exactly.

So this harness measures only:

| Family | Metric | Used for |
|---|---|---|
| Static geometry | exact transfer function from a **settled impulse response** (LTI when the LFO is frozen) → notch count, centres, log-spacing, spacing CV, inter-notch peak, notch depth | Ninety's 5.83:1 law · Twelve's 6 notches · Vibe's inharmonicity · Stone's peak geography · Stages · Spread · Center · Floor · Feedback · Mix |
| Motion | short-time mono-sum log-band gain curves → **comb pattern position** by normalised cross-correlation, integrated → range, rise/fall, step flatness, monotone drift, step size, rigidity, derivative crest | Kraut's asymmetry · Barber's monotonic drift · Steps' held trajectory · Duo's two clocks · Envy's env tracking · Depth · Rate · Lag · Touch |
| Nonlinearity | THD with the comb parked clear of the harmonics; centroid of the **resonant excess** (`TF(fb) − TF(fb 0)`) | Stone's OTA loop · Color (dirt and darkness) |
| Sidebands | carrier ± exactly the LFO rate | Twelve's audio-rate FM |
| Pitch | unwrapped phase of a carrier → instantaneous frequency → cents | Vibe's vibrato voicing |
| Artefacts | peak of a 4-pole 6 kHz high-pass under a 220 Hz sine (a sine cannot make HF; a click can) | every param sweep · Barber's wrap · Type swaps |

Three probe-craft notes worth carrying to the chorus and flanger:
- **THD needs the comb parked away from the harmonics.** Measuring THD at 220 Hz with the notches at
  350 Hz reads Color's THD going *down*, because the comb eats harmonics 2–8.
- **Envelope tests need an AM probe with a period the follower can follow.** `Envy · Fast Grab` opens
  4+ octaves in 3 ms, which is faster than one analysis frame — the tracker cannot see a jump larger
  than its own search span, and it reported r = 0.23 on a working device.
- **"Held" must be an absolute threshold.** Scaling it to the mean derivative means that when 90 %
  of frames are held the mean is tiny and estimator jitter exceeds it: S+H read 14 % and a triangle
  read 16 %, i.e. the metric had stopped working entirely while still returning numbers.

---

## 3. Cross-type distinctness

19 features, each normalised by a stated JND; a pair is distinct if **any** feature differs by more
than its JND. Closest pair **8.57 ×** — Stone / Vibe, carried by LFO shape. The matrix is in the
pasted output below.

The interesting history: at first draft **Ninety and Stone measured 0.77 ×** — i.e. not two Types.
With Color at 0 and matched stage counts, "an OTA cascade with an extra loop stage" is a Phase 90
with an extra loop stage. Three structural changes fixed it honestly rather than by tuning defaults:
the OTA now has its own loop bandwidth (`colHz × 0.35`), a standing soft clip even at Color 0
(`loopDrv 2.0` on `Color Off`), and the hyper-triangle LFO. Measured separation is now **51.7 ×**,
and `Stone's comb is not Ninety-with-feedback` is its own gate (lowest notch 0.46 octaves apart at
matched knobs).

---

## 4. What I cut, and what I changed against the bible

| | Decision |
|---|---|
| ✂️ | **`Grip`** (a second envelope knob) — folded into **`Lag`**, one motion time constant that serves the lamp lag, the S+H glide, the envelope speed *and* the LFO smoothing. Two back slots for one envelope was the most expensive thing on the panel, and `Grip` is a dead knob on the six Types with no envelope. |
| ✂️ | **Per-stage scatter as a Character axis.** `Matched JFETs` vs `Loose Batch` measured **0.08 × JND**. Scatter of a plausible size does not move notches perceptibly on a 4-stage cascade. It is now a fixed per-Type constant, and Ninety's freed slots went to `Two Stage`, `Eight Stage`, `Sine Sweep` and `Wide Stagger` — four real physics changes. |
| ✂️ | **`Drift` / `Age`** as a back knob — subtle by nature; cannot pass law 1 without becoming a different control. |
| 🔧 | **Feedback is a magnitude knob, sign in Character** — not the bible's bipolar knob. The locked `Params` defaults `feedback = 0.0f`, and a bipolar knob whose neutral is 0.5 cannot have a 0.0 default without meaning "full negative"; CLAUDE.md §4 forbids that shape. Six Characters carry the negative geography. Arguably more fb345-compliant, not less. |
| 🔧 | **The dry/wet sum is inside the effect**, not in Mix (ROSTER §0). Without this, Mix cannot obey law 1 — notch depth would peak at 50 and vanish at 100. |
| 🔧 | **Mix carries a correlation-aware trim** on top of the contract's equal-power law, because a phaser's wet is 50 % dry by construction. Without it, mid-mix rings +1 dB and Mix 100 drops 3 dB. The trim is exactly 1.0 at Mix 0, so bit-transparency is preserved (measured: worst sample delta 0.000e+00). |
| 🔧 | **Barber `Lmin = 0 dB`**, not the paper's −3 dB. At exactly 0 dB the wrapping section is an algebraic identity and the state handoff is provably click-free (`b == a ⇒ y = x` for any coefficients when `y1 = x1, y2 = x2`). With −3 dB there is a real discontinuity to fade. Measured wrap click **−110.9 dBFS** against a −60 dBFS bar. |
| 🔧 | **Barber `Depth` is notch depth only** (−4 → −70 dB) and does **not** modulate `f0`. A sawtooth on `f0` at the same rate as the ladder scroll snaps back once per cycle and destroys the monotonic illusion the Type exists for. |
| 🔧 | **Touch ±8 octaves, soft-knee follower** — see bug 7. The bible's `env/0.05` clamp is pinned on our bus. |
| 🔧 | **Stereo also splits the centre frequency (±0.42 oct)**, not just the LFO phase. This is what makes the mono law survivable at Stereo 100 (bible §6 says "collapses badly … the price every stereo phaser pays"; it does not have to be). |
| 📝 | **Bible contradiction found:** §4.2 tooltip specifies Floor as "sweep lower bound **+ matching 6 dB/oct wet HP**". The wet HP is not a harmless extra — it caps every null in the device (bug 1). The bible's own §3.2 statement that Floor is a sweep bound is the correct one; the HP clause should be struck. |
| 📝 | **Bible number checked and confirmed:** the Uni-Vibe cap ratios. `C_gm = (0.015 · 0.22 · 0.00047 · 0.0047)^(1/4) = 0.00924 µF`, giving 0.616 / 0.042 / 19.66 / 1.966 — matching §2 row 6's "≈ 0.62×, 0.042×, 19.7×, 1.97×". Implemented as log2 offsets. |
| 📝 | **Bible number checked and confirmed:** Ninety's 5.83:1. `tan(3π/8)/tan(π/8) = 2.4142/0.4142 = 5.828`; measured **5.76** with the shipped 0.06-octave stage scatter. |

---

## 5. What I could NOT prove, and what it would take

1. **That it reaches Max's ears.** This is the fb373 law and it is outside a DSP harness by
   construction: a green engine harness never proves the plugin reaches the engine. The
   `SYN_PHZ_*` params, the four-constant pool move (`kPoolSendCount` and friends), the exclusion
   sums and the UI round trip are the integration owner's serial work, and they need their own
   headless gate. **Check the `auval` exit code.**
2. **That it sounds good.** Every gate here is a discriminator, not a judgement. Nine Types being
   provably different is not nine Types being *worth having*. That is Max's ears, and the worklet
   exists so he can use them before any C++ is integrated.
3. **The card.** The bible's §5 Breathing Comb + Notch Rain is not built. The engine publishes
   everything it needs (`Viz.notch[8]` in Hz are exact, not decorative; `lfo`, `lvl`, `depthNow`),
   but the canvas work and the mockup-in-Safari approval are a separate pass.
4. **Interaction with the rest of the chain.** Bible §6 predicts phaser→distortion flattens the
   notches and distortion→phaser is the good order. Not measured — it needs the chain, not the
   engine.
5. **`Random 8` vs `Random Wide` as *random sequences*.** They measure 2.7 × apart, but that
   separation is carried by depth and step size, not by the quantisation grid (8 vs 16 levels). I do
   not have a metric that hears "8 levels" versus "16 levels" as such, and I am not convinced the ear
   does either — hence the depth difference, which is honest about what actually distinguishes them.
6. **88.2 kHz.** Proven at 44.1, 48 and 96 (notch geometry identical to 0.00 %). 88.2 was not run;
   there is no mechanism by which it would differ, but it was not measured.
7. **Denormal cost.** Every recirculating state is flushed and 60 s at max feedback is stable and
   bounded, but I did not measure CPU *with* a decaying tail on a machine without FTZ. The flush is
   a compare-and-select on ~34 states per sample and is inside the reported 6.4–12.9 µs.

---

## 6. Numbers the integration owner will want

- **CPU, 48 kHz / 128 samples, per instance:** Ninety 6.4 · Stone 6.9 · Duo 11.8 · Twelve 10.1 ·
  Kraut 12.9 · Vibe 8.7 · Barber 11.5 · Envy 7.0 · Steps 7.1 µs. That is 0.24 – 0.48 % of one core.
  **Worst case named in the brief — 16 stages × 6 instances — 81 µs = 3.05 % of one core.**
- **No allocation, no locks, no `std::function`** reachable from `processStereo`. Fixed arrays only.
  The 2048-entry tan LUT is a member, built in `prepare`.
- **Zero latency, zero lookahead, no oversampling, no quality tiers.**
- **Mix 0 is bit-transparent** (worst sample delta 0.000e+00) — safe to leave instantiated.
- **Mix 1.0 dry residual worse than −72 dB on all nine Types.**
- Sample rates: notch geometry **identical to 0.00 %** at 44.1 and 96 kHz.
- Stability: 60 s of white noise at Feedback/Depth/Color/Spread/Stages/Touch/Stereo = 100 on all
  nine Types — finite, bounded, worst peak 0.526.

---

## 7. Harness output, verbatim

```

══ PHASER FX ENGINE — certification ══  (bus program -26 dBFS, fs 48000)
   9 Types x 8 Characters. Metrics are NOTCH GEOMETRY, never wet magnitude.

[A. The Mix law — and how you measure 'zero dry' on an ALL-PASS effect]
   At Mix 1.0 the output is x + apBlend*(A{x}-x). At a notch A{x} = -x, so the
   output is EXACTLY the bypass-dry leak d. The measured null depth IS the residual.
  ok    Mix 0 is bit-transparent                               worst sample delta 0.000e+00
  ok    Mix 1.0 dry residual < -60 dB (all 9 Types)            worst -72.2 dB on Barber
  ok    Vibe/Vibrato all-pass leg is flat (implied leak)       ripple 0.0000 dB => leak -111.4 dB
  ok    unity-through at defaults (+-2 dB)                     1.43 dB vs dry

[B. Per-Type discriminators (law 2 — a MECHANISM, not an EQ flavour)]
  ok    Ninety: exactly 2 notches from 4 identical stages      2 notches
  ok    Ninety: notch ratio = the 5.83:1 phase law             5.76 : 1 (theory 5.83)
  ok    Stone: inter-notch peaks lift >= +6 dB                 22.3 dB peak
  ok    Stone: OTA loop is nonlinear (THD in 0.05..8 %)        -54.2 dB (= 0.196 %)
  ok    Stone's comb is not Ninety-with-feedback               lowest notch 0.46 octaves apart
  ok    Duo: the comb does NOT translate rigidly (two clocks)  Duo 0.852 vs Ninety 0.978 / Twelve 0.965
  ok    Twelve: 6 notches from 12 poles                        6 notches
  ok    Twelve/Hi Range: audio-rate FM sidebands >= -30 dBc    -13.9 dBc
  ok    no other Type can produce them (Ninety @ Rate 100)     -84.1 dBc
  ok    Kraut: sweep rise/fall asymmetry >= 1.8:1              2.47 : 1
  ok    Ninety's triangle is symmetric (control)               1.11 : 1
  ok    Vibe: notch ratio is inharmonic vs the identical-stage law Vibe 57.25:1 vs Ninety 5.82:1
  ok    Vibe/Vibrato at Mix 100 wobbles pitch >= 8 cents       30.3 cents
  ok    Barber/Rise: comb drifts one way >= 90 % of frames     100 % monotone, mean shift +0.056 oct/frame
  ok    Barber/Fall reverses it                                100 % monotone, mean shift -0.056 oct/frame
  ok    an LFO Type turns around (control)                     53 % monotone
  ok    Barber cycle-wrap click <= -60 dBFS                    -110.9 dBFS above 6 kHz
  ok    Envy: notch trajectory tracks the program envelope     rank r = 0.852
  ok    Envy PARKS on silence (nothing free-runs)              0.000 octaves of drift
  ok    Steps: trajectory is piecewise-constant                99 % of frames held
  ok    a continuous LFO is not (control)                      Ninety 27 %

[C. CROSS-TYPE DISTINCTNESS MATRIX (every pair, phase-independent)]
   distance = max over 19 features of |delta| / JND. A pair is distinct at > 1.00.
         Ninety  Stone    Duo Twelve  Kraut   Vibe Barber   Envy  Steps
  Ninety      .  51.67  53.45  34.26  31.78  43.10  46.92  62.38  27.17
  Stone   51.67      .  14.76  17.41  19.88   8.57  53.98  10.71  24.50
  Duo     53.45  14.76      .  19.19  23.51  14.16  55.76   8.93  26.28
  Twelve  34.26  17.41  19.19      .  18.75   9.40  36.57  28.12  22.35
  Kraut   31.78  19.88  23.51  18.75      .  11.31  34.09  30.59  23.78
  Vibe    43.10   8.57  14.16   9.40  11.31      .  45.41  19.28  15.93
  Barber  46.92  53.98  55.76  36.57  34.09  45.41      .  64.69  50.35
  Envy    62.38  10.71   8.93  28.12  30.59  19.28  64.69      .  35.21
  Steps   27.17  24.50  26.28  22.35  23.78  15.93  50.35  35.21      .
  ok    every Type pair is distinguishable (> 1.00 JND)        closest pair 8.57 = Stone/Vibe (carried by LFO shape (deriv crest))

[D. Every param evolves 0 -> 100, monotonically and dramatically (law 1)]
  ok    Rate: monotonic and spans decades                      [0.09 0.64 2.66 11.99] Hz
  ok    Depth: monotonic, 0 -> multi-octave excursion          [0.17 2.67 4.73 5.90 7.04] octaves
  ok    Feedback: monotonic resonance lift                     [3.0 4.3 6.2 10.0 23.1] dB inter-notch peak
  ok    Mix: notch depth is MONOTONIC 0 -> full                [-0.3 -1.8 -4.0 -8.0 -133.9] dB null
  ok    Center: monotonic, spans the audio band                [28 93 324 1133 4099] Hz lowest notch
  ok    Stages: notch count climbs monotonically               [3 4 5 6] notches
  ok    Spread: notch spacing widens monotonically             [1.14 1.16 2.24 4.28] octaves between notches
  ok    Stereo: monotonic L/R decorrelation                    [-140.0 -5.4 -3.1 -4.7] dB L-R
  ok    Touch: bipolar and monotonic, multi-octave             6.61 octaves of travel [-3.14 -1.56 0.00 1.61 3.48] oct vs centre
  ok    Lag: monotonically slugs the motion                    [6.63 4.99 3.79 2.76] octaves of excursion
  ok    Floor: IS the lowest notch (a clamp, never a filter)   [29 95 309 1007] Hz lowest notch vs Floor knob 10/40/70/100
  ok    Color: monotonically dirties the resonance             [-60.3 -33.9 -29.0 -28.6] dB THD
  ok    Color: monotonically darkens the resonance (in-loop LP) [1720 1451 1051 699] Hz centroid of the resonant excess

[E. Every Character re-wires PHYSICS, not tone (law R4 / fb345)]
  ok    Ninety: all 8 Characters differ physically             closest pair 4.28 JND (Script 74 / Negative, separated by LFO shape)
  ok    Stone: all 8 Characters differ physically              closest pair 2.47 JND (Color Off / Color On, separated by spacing)
  ok    Duo: all 8 Characters differ physically                closest pair 12.37 JND (Series 3:4 / Cross Feed, separated by sweep range)
  ok    Twelve: all 8 Characters differ physically             closest pair 2.34 JND (Hi Range / Aux Out, separated by sweep range)
  ok    Kraut: all 8 Characters differ physically              closest pair 5.27 JND (Fast Bulbs / Four Bulb, separated by depth)
  ok    Vibe: all 8 Characters differ physically               closest pair 2.11 JND (Hot Bulb / Vibrato Deep, separated by sweep range)
  ok    Barber: all 8 Characters differ physically             closest pair 4.13 JND (Rise 8 / Deep Rise, separated by L/R)
  ok    Envy: all 8 Characters differ physically               closest pair 5.63 JND (Fast Grab / Transient, separated by LFO shape)
  ok    Steps: all 8 Characters differ physically              closest pair 2.03 JND (Ladder Up / Pendulum, separated by LFO shape)
        weakest Character pair overall: 2.03 JND - Steps Ladder Up / Pendulum

[F. NO CLICKS — every param swept under a sustained tone (law 4)]
  ok    full sweep of all 12 params: no click above -55 dBFS   worst -74.4 dBFS on Stages
  ok    Type swap mid-note never overshoots either steady state (< +3 dB) worst +-0.35 dB Duo->Twelve
  ok    Type swap emits no click (HF residual <= -55 dBFS)     worst -67.3 dBFS Barber->Envy

[G. MONO-SAFE — the effect must survive a fold-down (law 5)]
  ok    mono sum keeps a real comb at Stereo 25 (>= 6 dB)      weakest 6.3 dB on Barber  [17.2 31.5 28.4 26.2 41.1 20.4 6.3 17.5 18.9]
  ok    mono sum keeps a real comb at Stereo 100 (>= 6 dB)     weakest 6.5 dB on Barber  [18.8 19.4 39.3 34.2 13.6 19.4 6.5 21.3 11.9]
  ok    Duo: every Character survives mono                     weakest 17.8 dB Parallel Golden

[H. Stability — 60 s of white noise at maximum everything, all 9 Types]
  ok    60 s at max feedback: finite and bounded, all Types    0 unstable, worst peak 0.526
  ok    in-loop AC coupling: feedback never amplifies DC       worst DC gain x1.000 vs Feedback 0, on Duo
  ok    notch geometry identical at 44.1 kHz                   2 notches, worst 0.00 % off 48 k
  ok    notch geometry identical at 96.0 kHz                   2 notches, worst 0.00 % off 48 k
  ok    stable at max feedback at 44.1 kHz                     peak 0.1085
  ok    stable at max feedback at 96.0 kHz                     peak 0.1091

[I. CPU — us per 128-sample block at 48 kHz, per Type]
        Ninety     6.54 us/block  (0.25 % of one core)
        Stone      6.87 us/block  (0.26 % of one core)
        Duo       11.86 us/block  (0.44 % of one core)
        Twelve    10.48 us/block  (0.39 % of one core)
        Kraut     13.10 us/block  (0.49 % of one core)
        Vibe       8.77 us/block  (0.33 % of one core)
        Barber    11.48 us/block  (0.43 % of one core)
        Envy       6.89 us/block  (0.26 % of one core)
        Steps      7.07 us/block  (0.26 % of one core)
        WORST     82.22 us/block  (3.08 % of one core)  <- 16 stages x 6 INSTANCES
  ok    worst case (16 stages x 6 instances) under 12 % of a core 3.08 %
  ok    no single Type exceeds 2 % of a core                   worst 13.10 us on Kraut

[J. Self-check — can these gates actually fail?]
  ok    (self-check) the notch detector sees an injected notch 2 -> 3
  ok    (self-check) the HF click detector sees an injected click -30.7 dBFS
  ok    (self-check) it does NOT fire on the clean tone        -121.3 dBFS

  65 passed, 0 FAILED

```
