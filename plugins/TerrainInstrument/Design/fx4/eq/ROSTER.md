# EQUALIZER — the locked roster (fx4, chain kind 9, `SYN_EQZ_*`)

**7 Types × 8 Characters × 5 Focus · 12 params (3 heroes + Mix + 8 back) · ±30 dB per band
× Amount 200 % = ±60 dB.** Engine: `TerrainEqualizerFx.h`. Certified by `eq_cert.cpp`
(**121 pass / 1 FAIL**, full output in `eq_cert_fb422.log`; mutation evidence in `MUTATION.md`).

> ## 🔴 fb422 — WHAT CHANGED SINCE fb420
>
> **Names.** Every EQUALIZER row of `Design/fx4/RENAMES.md` is applied verbatim, and the engine
> header is the single source of truth for all 87 of them (`EQ::label(i)` / `EQ::labelSlot(i)`;
> `charNames()` is now DERIVED from `charSpec().nm`, so the `Fixed Top` / `Iron Top` drift class
> is structurally impossible). Front hero `Tilt` → **`Slant`** · Type 6 `Sculpt` → **`Chisel`** ·
> P8 `Shape` → **`Trait`** and its seven relabels are now
> **Pinch · Slope · Taper · Dip · Silk · Pivot · Sting**. Twelve Characters moved. §O of the cert
> gates all of it against 3064 shipped strings and reports 10 collisions RENAMES.md did not rule
> on — see `FINDINGS.md` §F. That is the 1 FAIL, and it is a naming ruling, not a DSP defect.
>
> **`Slant` was non-monotonic and is fixed.** Its one-pole shelf's 0 dB crossing sat at
> `pivot / 10^(g/20)`, so the pivot slid 700 Hz → 2.8 Hz as the knob opened and the bass came back
> UP by 13.2 dB (48 dB at Amount 200 %). The corner is now placed by arithmetic so the pivot is
> exact at every gain and every sample rate. **Every `Slant` number below this box was measured on the
> BROKEN engine** (the prose was renamed, the measurements were not re-run) — the corrected ones are in `FINDINGS.md` §A/§B and `eq_cert_fb422.log` §F3/§K.
>
> **`Amount`'s smoother went 10 ms → 20 ms** (an instant Amount write measured 1.63 dB of wet-gain
> change in one sample; it scales as 1/tau, so it was a smoothing fault, not physics).
>
> **The ring gate now sweeps Amount to its ceiling and includes the Slant stage**, over an 8 s
> window: longest T60 in the device is **2405 ms** (fb420's sweep said 539 ms because it never
> opened Amount), and the Slant's own one-pole reaches **1841 ms**. Both under the 3 s law.

---

## 0. THE ANSWER TO MAX'S ACTUAL WORRY

> *"I don't know what an EQ could possibly offer on the back… it'll be annoying if we have a
> whole bunch of shit on the back panel that we can't see."*

**Nothing on this back panel is back-only. The back 8 ARE the curve nodes.**

| The front curve | writes | The back knob |
|---|---|---|
| drag node 1 in X | ⇄ | `Low Hz` (P1) |
| drag node 1 in Y | ⇄ | `Low` (P2) |
| drag node 2 in X / Y | ⇄ | `Body Hz` (P3) / `Body` (P4) |
| drag node 3 in X / Y | ⇄ | `Bite Hz` (P5) / `Bite` (P6) |
| drag node 4 in X | ⇄ | `Reach` (P7) |
| drag node 4 in Y | ⇄ | **`Air` — the FRONT hero knob** |
| wheel on any node | ⇄ | `Trait` (P8) — the Q law's one degree of freedom |

There are exactly twelve parameters and the visualizer reaches every one of them. The back
panel is the **numeric face of the curve**, not a second, hidden device. Turn a back knob and
the node moves; drag a node and the back knob moves. That is the whole design.

The one asymmetry is deliberate: the AIR band's **gain** lives on the FRONT as the hero knob
`Air` (Max's mandate word) while its **frequency** lives on the back as `Reach`. One band, two
surfaces, zero duplication.

---

## 1. THE CHASSIS SOLVE — why Q is not a knob

A four-band parametric wants **twelve** controls (4 × freq/gain/Q) before Slant, Air or
dynamics. The fb275 back panel gives **eight**.

**Q is never a knob. Q is a LAW, and the Type owns it.** Freq + gain per band = 8 = exactly
the grid; the heroes go on the front; and the single remaining degree of freedom is the P8
`Trait` slot, which relabels per Type and always changes CURVE MATH, never cosmetics.

Serum 2's Equalizer is six parameters (two bands × Freq/Gain/Q, a small static curve, no
analyzer, no M/S, no dynamics). This is twelve, plus seven Q laws, plus a fully interactive
Pro-Q-grammar curve. Width matched, depth beaten.

---

## 2. THE SEVEN TYPES

`kNumTypes = 7` live, `kNumTypeSlots = 12` declared (fb342 birth-cardinality law: an
`AudioParameterChoice`'s option count is fixed at construction and state-format-breaking to
grow later — declare 12, light 7, clamp the rest).

| # | Type | Lineage | The MECHANISM (what is physically different) | Measured discriminator |
|---|------|---------|----------------------------------------------|------------------------|
| 0 | **Surgical** | Massenburg 1972 / Pro-Q | Matched, decramped biquads. Constant Q, exact dB, cuts are the *exact* mirror of boosts. The null-test Type. | A +18 dB knob measures **+17.84 dB**; bandwidth ratio between a +6 and a +30 dB move = **1.00×** (Q genuinely constant) |
| 1 | **British** | Neve 1073 inductor | The shelves RESONATE. `Slope` drives shelf Q 0.5→4.5, so a boost undershoots below 0 dB before it rises — the L-C signature. Gains lean on themselves at the top (−0.79 dB at ±30). | **−7.58 dB** undershoot just above a +24 dB low-shelf corner, where Surgical shows **−0.61 dB** |
| 2 | **American** | API 550 proportional Q | Q is a FUNCTION OF GAIN: `Q = 0.55 + 13·(|g|/30)^grip`. Small moves are broad, big moves are lasers, from one knob position. | bandwidth(+6 dB) / bandwidth(+30 dB) = **3.67×** (Surgical: 1.00×) |
| 3 | **Passive** | Pultec EQP-1A | Boosting the Low shelf ALSO engages an attenuation shelf at 2.2×fc. One knob, hump *and* scoop — the interaction an ideal parametric cannot make. Cuts run 1.15× deeper (the EQP's 17.5 vs 13.5 dB). BITE is a fixed wide peak that ignores every Q law. | **−2.61 dB** trough at 150–700 Hz from a +18 dB @ 60 Hz boost, where Surgical shows **−0.56 dB** |
| 4 | **Open** | Maag EQ4 / Siemens W295b | Everything widens (bell Q 0.40–0.45) and gains pass a `tanh` knee, so "boost it forever" never gets shrill. **AIR is 6 dB/oct**, so a corner beyond Nyquist still lifts in band. | bells are **3.04 octaves** wide (Surgical: 1.57); at Reach 40 kHz it still delivers **+3.08 dB at 18 kHz** where every 2-pole Type delivers **+0.01 dB** |
| 5 | **Dynamic** | TDR Nova / Pro-Q 3 | The ONE level-dependent Type (contract §4 allows exactly one). Per-band SVF detector → envelope → smoothstep around a **program-anchored** threshold. Cuts fade IN with level, boosts fade OUT. | **17.84 dB** of response change from a −40 to a −12 dBFS program; every other Type measures **0.000 dB** |
| 6 | **Chisel** | digital-only, no ancestor | Q is coupled to gain (`Ring × (1 + |g|/12)`, to Q 90) and the bottom of the downward travel MORPHS the bell into a true notch: −30 dB on the knob becomes **−90 dB** of hole. | notch floor **−90.0 dB** (Surgical at the same knob: −30.0 dB) and T60 **338 ms** where Surgical rings **8.7 ms** |

### Cross-type distinctness (measured, phase-independent, gate 1.00 JND)

```
       Surgical BritishAmerican Passive    Open Dynamic  Chisel
Surgic       .    4.87    8.23    5.48    7.03    9.95   10.00
Britis    4.87       .    8.95    6.99    9.26   10.00   10.00
Americ    8.23    8.95       .   10.00   10.00   10.00   10.00
Passiv    5.48    6.99   10.00       .    7.51   10.00   10.00
Open      7.03    9.26   10.00    7.51       .   10.00   10.00
Dynami    9.95   10.00   10.00   10.00   10.00       .   10.00
Chisel   10.00   10.00   10.00   10.00   10.00   10.00       .
```
Closest pair **4.87 = Surgical/British** — nearly five JND units apart. Ten axes: reference-patch
max and rms spectral delta, +18 dB peak height, bandwidth in octaves, proportional-Q ratio,
shelf undershoot, Pultec scoop, level dependence, notch floor, 18 kHz top-octave behaviour.

### What was CUT, and why

* **SSL** — it is American with different numbers. The proportional-Q law is the mechanism and
  it is already spoken for; a second Type with the same mechanism and a different constant is
  the exact near-twin failure Phase G caught in the distortion *after* building it. Cut at spec.
* **Slant** as a Type — Slant is a permanent front hero. A Type cannot own a knob.
* **Graphic** — a band-count fantasy on a four-band chassis.
* **Linear Phase** — permanently rejected, not deferred. FIR latency re-opens the fb305
  main-send alignment trap for every downstream device (§5 below).

---

## 3. THE 56 CHARACTERS

Back dropdown 1. **Every one changes physics and every one is measured**: within each Type,
all 28 pairs are compared on the 96-bin output spectrum at the reference patch, and the
closest pair must exceed 1.5 dB. Worst case across the whole roster: **2.38 dB**
(Chisel: Gain Peak / Metal). Dynamic is measured differently and separately — see below.

**Surgical** — re-voices the Q law and the band shapes.
`Plain` reference · `Tight` Q ×2.6 · `Broad` Q ×0.35 · `Steep` LOW+AIR become 24 dB/oct
(two Butterworth-paired shelves at half gain each) · `Scoop` cuts get Q ×3.5 while boosts stay
wide (the classic asymmetry) · `Deep Pivot` slant pivot → 150 Hz · `Bright Pivot` → 3 kHz ·
`Four Bells` LOW and AIR stop being shelves and become bells (the sub and the top octave are
left alone entirely — the single biggest topology change in the device).
*Closest pair 3.90 dB.*

**British** — moves the iron.
`Desk` reference · `Big Knob` mid bells Q ×0.5 · `Forward` mids Q ×2.2/2.4 and pushed up ·
`Iron Top` AIR Q ×1.9 at 0.78× the corner with a stronger Bump (the 1073's fixed 12 kHz) ·
`Sub Iron` LOW down to 0.42× with Q ×1.6 · `Steep Iron` 24 dB/oct shelves ·
`Full Swing` the gain softener is switched OFF (±30 dB exact instead of ±27) and Bump is
de-rated — the "modern" British · `Mid Rise` BODY up an octave, wider, ×1.25 gain.
*Closest pair 4.32 dB (Desk / Full Swing).*

**American** — moves the LAW, not its numbers.
`Proportional` reference · `Lasers` exponent +1.2, Q ×1.6 · `Mellow` exponent −0.55, Q ×0.7 ·
`Floor Lift` raises the Q FLOOR ×2.2 so even tiny moves are tight · `Boost Only` the law
applies to boosts only, cuts stay wide · `Cut Only` the reverse · `Shelf Ride` the shelves go
proportional too (S rides gain) · `Runaway` exponent +2.0 with Q ×2.2 — at ±30 dB the bells
reach Q 30.
*Closest pair 2.49 dB.*

**Passive** — moves where the ride-along cut lands.
`Program` cut shelf at 2.2×fc · `Close Cut` 1.4× · `Far Cut` 4.4× · `Both Ends` the AIR band
gets a ride-along attenuator too (the EQP's high boost + high atten engaged together) ·
`Bell Top` AIR becomes the EQP high **peak** · `Slow Top` AIR becomes a 6 dB/oct shelf — the
slowest curve in the device · `Deep Atten` cuts ×1.45 · `Revival` the ride-along is a BELL
instead of a shelf, so the scoop is local rather than a whole region.
*Closest pair 4.14 dB.*

**Open** — moves how far the top reaches and how wide it opens.
`Gloss` Silk shelf one octave above Reach · `Very Wide` Q ×0.55 (bells past 5 octaves) ·
`Two Shelves` BODY becomes a second low shelf · `Stacked` Silk one octave BELOW Reach, so the
two shelves compound into a steeper top · `Deep Reach` Silk two octaves below with AIR Q ×1.5 ·
`Soft Knee` tanh knee 12 dB (heavy compression of extremes) · `Hard Knee` knee 90 dB (the
softener effectively off — exact gains) · `Bell Air` AIR becomes a very wide peak, the Sie-Q
"boost it forever" band.
*Closest pair 2.73 dB.*

**Chisel** — moves the notch morph and the ring.
`Resonator` morph knee −18 dB · `Razor` Q ×2.5, knee −12 · `Triple Notch` BODY and BITE pulled
onto 2× and 4× spacing (a harmonic comb) · `Gain Peak` Q/gain coupling ×2.6 · `Shallow` knee
−90 dB, i.e. the morph never engages — pure resonator, no notches · `Telephone` LOW and AIR
become bells pulled to 2.5× and 0.35× (band-pass carving) · `Sub Kill` LOW at 0.35× with Q ×3
and a −10 dB knee · `Metal` Q ×4 with coupling ×1.8 — Q 90 on every band.
*Closest pair 2.38 dB.*

**Dynamic** — the DETECTOR is the physics, so a static spectrum cannot grade it. Measured on
the **ride trajectory** instead (attack, release, the ride at a hot and a quiet program, and
crucially the ride at −29 / −26 / −23 dBFS, because a threshold window is invisible at the
endpoints — both a 2.5 dB and a 34 dB window are fully on at −10 dBFS).

| Character | attack | release | hot | quiet | −26 dBFS | −29 dBFS | −23 dBFS |
|---|---|---|---|---|---|---|---|
| `Program Ride` | 2.00 ms | 61 ms | −24.0 | 0.0 | −13.98 | −6.71 | −20.57 |
| `Quick` | 0.67 ms | 14.7 ms | −24.0 | 0.0 | −12.43 | −5.79 | −19.21 |
| `Lazy` | 5.33 ms | 307 ms | −24.0 | 0.0 | −14.47 | −7.03 | −20.94 |
| `Wideband` | 0.67 ms | — | −24.0 | −5.06 | −23.67 | −23.67 | −23.67 |
| `Upward` | 2.00 ms | 61.3 ms | 0.0 | −24.0 | −9.14 | −16.62 | −2.75 |
| `Hard Window` | 1.33 ms | 57 ms | −24.0 | 0.0 | −15.75 | −0.16 | −23.67 |
| `Soft Window` | 2.00 ms | 68 ms | −24.0 | 0.0 | −12.87 | −9.76 | −15.92 |
| `Peak Hold` | 2.00 ms | — | −24.0 | 0.0 | −21.57 | −15.46 | −23.67 |

*Closest pair 1.22 — above the 1.00 gate, and the pair (`Program Ride` / `Hard Window`) is
separated almost entirely by the −29 dBFS column, which is exactly the axis a window width
lives on.*

---

## 4. THE PARAMETERS

### Front — 3 heroes + Mix

| Knob | Range / taper | Glide | What it does | Measured 0→100 % |
|---|---|---|---|---|
| **Slant** | ±24 dB, linear-in-dB, centre detent at 0.5 | 15 ms | One knob, whole spectrum: a Baxandall seesaw around 700 Hz (150 Hz / 3 kHz on the pivot Characters). 6 dB/oct — the spectrum LEANS, it does not step. | **40.4 dB** of spread swing (8 kHz minus 80 Hz), monotonic, zero reversal |
| **Air** | ±30 dB, linear-in-dB, centre detent | 12 ms | The shelf above everything. ×1.33 in `Open`. This is also node 4's Y axis. | **54.2 dB** at 19 kHz |
| **Amount** | 0–200 %, linear, default 100 % | 10 ms | Scales every band gain, Slant and Air *before* design. 0 % = provably flat at any knob state; 200 % = the caricature. One automation lane performs the whole device. | **43.1 dB** of max spectral deviation, perfectly monotonic |
| **Mix** | 0–100 %, default **100 %** | 10 ms | **LINEAR** crossfade, not equal-power: dry and wet here are 100 % correlated (same signal, minimum phase), so a sin/cos law would bump +3 dB at 50 %. Verified: a −90 dB notch reads exactly **−6.02 dB** at Mix 50 %. | **28.7 dB** of notch depth |

### Back — 2 dropdowns + 8 knobs (4 × 2, columns = bands)

**Dropdown 1: `Character` (8).** **Dropdown 2: `Focus` (Stereo / Mid / Side / Left / Right).**

**Why the second axis is `Focus` and not `Type`** — `Type` is the **header pill**
(`DEVS[].tp`), exactly as on Reverb, Delay, Distortion and all three fx3 devices. fb418
removed a back-panel Type duplicate from Chorus, Flanger and Phaser for precisely this reason:
it duplicated the most visible label the card has and broke the no-doubles rule. Putting it
back would also throw away a dropdown this device is entitled to.

`Focus` earns the slot because it changes **physics, not tone** (R6): it changes *which signal
the filters see*. Mid/Side/Left/Right process one matrix channel and pass the other
**bit-exactly** (measured: worst delta 0.000e+00). `Side` + Air is the cleanest widener in the
synth; `Side` + a low-shelf cut is a mono anchor. The M/S round trip at zero gain measures
**−149.4 dB** of residual.

| Col → | LOW | BODY | BITE | AIR |
|---|---|---|---|---|
| **Row 1 — frequency** | **Low Hz** P1 · 20–500 Hz log · 20 ms | **Body Hz** P3 · 100 Hz–3 kHz log · 20 ms | **Bite Hz** P5 · 700 Hz–14 kHz log · 20 ms | **Reach** P7 · 6–40 kHz log · 20 ms |
| **Row 2 — gain** | **Low** P2 · ±30 dB · 12 ms | **Body** P4 · ±30 dB · 12 ms | **Bite** P6 · ±30 dB · 12 ms | **Trait** P8 · per-Type · 20 ms |

APVTS order is **column-major** — `LowHz, Low, BodyHz, Body, BiteHz, Bite, Reach, Trait` — so
each band's two parameters are adjacent. That is what makes "drag a node, two knobs move"
legible in automation lanes and in the preset diff.

**Every default is 0.5 and 0.5 is the neutral point of every one of them**, so the device boots
provably flat and its output is **bit-identical** to its input (measured: worst delta
0.000e+00, at 44.1, 48 and 96 kHz).

**Why each back knob earns its slot over the alternatives**

* **Low Hz / Body Hz / Bite Hz** — a fixed-frequency 500-series pedal is the other solve and it
  is worse: half the reason to reach for an EQ is *where*. Measured span: the Low shelf's
  half-gain corner moves **4.6 octaves**, the Body peak **4.9 octaves**, the Bite peak **4.3**.
* **Reach** — the AIR corner runs to **40 kHz**, past Nyquist at every supported rate. This is
  the Maag law and it is the single hardest thing in the device: a cramped (RBJ) design cannot
  even be *specified* above Nyquist, and its top half is a provably dead knob (measured: RBJ
  moves 15 kHz by **0.0000 dB** between Reach 22 kHz and 40 kHz). The matched design moves it
  **18.6 dB**, monotonically. Reach is a POSITION knob: high Reach trades in-band level for
  gentleness, which is the entire Maag/Sie-Q behaviour — and at the device's ceiling it still
  delivers +17.8 dB at 20 kHz.
* **Low / Body / Bite gains** — ±30 dB each, ×Amount 200 % = ±60 dB. Measured spans **61.1 /
  58.8 / 58.4 dB** on their own bands.
* **Trait** — the Q law's one degree of freedom. It is the only way to expose seven different
  Q laws through one physical control, and each relabel is a different equation:

| Type | `Trait` reads | Range | Measured span 0→100 % |
|---|---|---|---|
| Surgical | **Width** | global Q ×0.25 → ×40, centre detent at ×1.0 | **6.76** in log2 bandwidth (4.3 octaves → 0.038 octaves) |
| British | **Bump** | shelf Q 0.5 → 4.5 | **12.76 dB** of shelf undershoot |
| American | **Grip** | proportional exponent 0.4 → 3.0 | **2.50** in log2 bandwidth *at a small +8 dB move* |
| Passive | **Dip** | ride-along cut 0 → 120 % | **4.86 dB** of Pultec scoop |
| Open | **Silk** | second shelf 0 → 100 % of 0.8×Air | **14.43 dB** at 19 kHz |
| Dynamic | **Sense** | threshold **−20…+20 dB around −26 dBFS**, centre detent | **23.67 dB** of applied ride |
| Chisel | **Ring** | Q 2 → 64, then ×(1+|g|/12) to the Q 90 cap | **4.17** in log2 bandwidth |

---

## 5. THE VIZ CONTRACT (contract §2)

```
float curve[96];        // magnitude of the WHOLE cascade, dB
float nodeHz[4], nodeDb[4];
float lvl;              // 0..1, for the dead-feed fade
```

**Bin mapping, and get this right or the card draws a different EQ than it plays:**

```
f(i) = 20 · 10^(3i/95)  Hz,   i = 0 … 95
f(0)  = 20 Hz exactly        f(95) = 20000 Hz exactly
31.667 bins per decade · 1.0754× per bin · 3 decades total
```

* `curve` is evaluated from the **live, already-ramped coefficients**, not from the knob
  values, so the drawn curve physically cannot disagree with the audio. Gated: worst
  |drawn − measured| = **0.35 dB** against an independently measured white-noise output
  spectrum.
* It is evaluated in the **φ basis** (φ = sin²(ω/2)), not the textbook cosine form. This is not
  a style choice — see FINDINGS §2.
* `nodeHz[3]` reports AIR's **true** corner, which reaches 40 kHz. The card clamps the dot to
  the right edge; it must not clamp the number.
* `nodeDb` is the **applied** gain — post Amount, post the Type's gain law, post the Dynamic
  ride. Under `Dynamic` the dots slide with the audio, which is the Pro-Q / TDR grammar.
* Push cadence: recomputed at ~60 Hz inside `setParams`, never in the sample loop.

---

## 6. WHAT THE INTEGRATION OWNER INHERITS

* **Zero latency, structurally.** Minimum-phase IIR, no oversampling, no FIR, no lookahead.
  Never call `setLatencySamples` from this device.
* **`prepare` allocates nothing** — the engine has no heap state at all (fixed arrays only),
  so the fb415 malloc-on-the-audio-thread trap cannot occur here by construction.
* **`setParams` is per block.** All design, all trig, all resolution happens there, dirty-flagged.
* **No pills beyond the chassis.** The bible proposed `Delta` (monitor wet − dry) and `Auto`
  (loudness makeup); the locked `Params` block has no slot for them and the card is the
  integration owner's. Both are cheap and both are recommended — deferred, not forgotten.
* **fb305:** `eqzSend*` must join **every** `rtd` exclusion sum in the same commit that creates
  it, and the Equalizer's own main-send branch needs the symmetric N-way subtraction.
* **Choice cardinality is forever:** declare `SYN_EQZ_TYPE` at **12** slots (7 live),
  `SYN_EQZ_CHARACTER` at 8, `SYN_EQZ_FOCUS` at 5. Read all three with `(int)*rawParam(id)` —
  the index, never `round(v·(N−1))` (the fb50 law, and the fb373 law right behind it).
