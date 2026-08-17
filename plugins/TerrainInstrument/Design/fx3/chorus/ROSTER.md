# CHORUS — the locked roster

**8 Types × 8 Characters · front Rate / Depth / Feedback / Mix · back 8.**
Everything below is measured by `chorus_cert.cpp` (85/85 green, exit 0). Numbers in the
Discriminator column are from that run, not from the bible and not from intuition.

---

## 0. The shape of the device

A chorus is one stereo buffer, up to four **read taps per channel** whose lengths move, mixed
with the dry. Every legendary chorus is exactly three choices — **how many taps**, **what moves
them**, and **what the delay path does to the tone**. The Type dropdown walks the first two axes
through history; the back panel exposes the third everywhere.

That is why the roster's discriminators mostly live in **d(t)** — the delay trace — rather than
in the wet spectrum. Two chorus Types can have near-identical magnitude spectra and be
completely different machines; what separates them is *how the taps move relative to each other*.
The harness samples d(t) straight out of the engine and measures its modulation spectrum,
periodicity, skew, and **L/R topology correlation** — the last of which turned out to be the
single cleanest tell in the whole file (Pedal **+1.000**, June **−1.000**, Vintage **−0.017 and
rotating**).

---

## 1. The Types

### 0 · `Vintage` — the legacy chorus, kept whole ★ R7
* **Lineage:** `Source/TerrainChorus.h`, the synth page's own chorus. Max: *"That's already your
  vintage chorus… use the parameters that it already has and place it on the other chorus."*
* **Mechanism:** 1 tap per side, sine LFO, base window 3–24 ms (knob 0.5 = **8.5 ms**, the legacy
  `BASE_DELAY_MS`), excursion **5.10 ms** at Depth 100 (the legacy `BASE·0.6·amount`), recon LP
  spanning 2–16 kHz over Colour (the legacy 2–8 kHz window sits at Colour 0–0.55). The right tap
  runs on a **second clock at ×1.07** — `RIGHT_RATE_RATIO`, kept verbatim.
* **What that ×1.07 does, measured:** the legacy file declares `RIGHT_PHASE_OFFSET = π` and then
  defeats it. Two clocks at a 1.07 ratio rotate their relative phase at 0.07·f, so the pair passes
  through **in phase** every 1/(0.14·f) seconds. The harness reproduces the legacy topology exactly
  and measures **18.27 s at 0.40 Hz** (predicted 17.86), **6.17 s at 1.13 Hz** (6.32), **4.69 s at
  1.50 Hz** (4.76). The bible's claim is confirmed — but the resulting sound (a stereo image that
  breathes open and shut over ~10 s) is *good*, so it is the Type's identity rather than a bug to
  erase, and the honest fixed version ships beside it as the `Locked` Character.
* **Discriminator:** the **only** Type whose stereo image is non-stationary.
  σ of the 3 s-framed L/R correlation = **0.221**; next highest in the roster **0.074** (Wow).
  `Locked` measures **0.006** on the same probe — the same Type, the bug removed, A/B in one click.
* **Characters** — `x1` re-wires the second clock:
  | | re-wires |
  |---|---|
  | `Classic` | the legacy defaults: 1.13 Hz, ×1.07 skew, recon ~5.4 kHz |
  | `Slow` | 0.40 Hz lock, recon ×0.42, compander ×1.2, deeper |
  | `Fast` | 1.50 Hz lock, recon ×1.9, compander ×0.8 |
  | `Deep` | window ×1.25, excursion ×1.8 |
  | `Wide 106` | skew **×1.14** — the rotation runs twice as fast |
  | `Locked` | skew **×1.000** — one clock, true antiphase, the fix |
  | `Thick` | skew ×1.02 (a ~50 s rotation), window ×1.15, compander ×1.3 |
  | `Hiss` | noise floor −44 dB, compander ×1.6 — the Juno-hiss caricature |

### 1 · `June` — the Juno-60 twin antiphase BBD
* **Lineage:** Juno-6/60/106, 2× MN3009, one triangle LFO with the right line inverted
  (pendragon-andyh capture: 0.513 / 0.863 / 9.75 Hz, 1.66–5.35 ms).
* **Mechanism:** 1 tap per side, **ONE** accumulator, R offset by Phase (180° = exact antiphase for
  a triangle). Base 1–12 ms (knob 0.5 = 3.46 ms), excursion 2.95 ms at Depth 100 — the measured
  hardware half-window ×1.6, i.e. deliberately past the hardware.
* **Discriminator:** d(t) L/R topology **−1.000** (the two reads move in exact opposition) while
  audio L/R correlation collapses to **−0.136**. No other Type reaches −1.000 except Ensemble and
  Dark, which are separated from it on five other axes.
* **Characters:** `I` (0.513 Hz) · `II` (0.863 Hz) · `I Plus II` (9.75 Hz, excursion ×0.11, forced
  LP'd triangle — the vibrato mode) · `Manual` (Rate free) · `Aged` (compander ×2, recon ×0.45,
  grit ×1.3, noise −54) · `Clean` (compander **off**, no grit, recon ×2.6) · `Wide 106` (skew ×1.02)
  · `Deep` (window ×2, excursion ×1.8). Locked-rate rows keep Rate alive as a ±1 octave scaler.

### 2 · `Pedal` — the CE-1 / CE-2 one-line chorus
* **Lineage:** the JC-120 circuit in a box. One BBD line, **read twice**, into two amps.
* **Mechanism:** the delay line is fed the **mono sum** (one BBD, exactly like the hardware); L
  reads at `base`, R at `base + Phase·1.2 ms`, **both modulated identically**. LP'd triangle.
  Pre-emphasis +6 dB above 3 kHz into the line and its exact inverse after it — the CE-2
  hiss-ducking pair, and the reason its top end behaves differently from every other Type.
* **Discriminator:** d(t) L/R topology **+1.000** — the only Type whose two reads move *together*.
  ⚠️ Note that **audio** L/R correlation is a useless probe here: at a 1.2 ms read offset the two
  channels are already audio-decorrelated (**−0.07**) even though the modulator is perfectly
  co-phase. The topology metric is the one that means what the word means.
* **Characters:** `Chorus` (0.4 Hz, the CE-1 one-knob mode) · `Vibrato` (wet-only regardless of Mix
  — the CE-1's second mode) · `Grit` (input poly drive ×4) · `Slow Amp` / `Fast Amp` (0.8 / 6.5 Hz)
  · **`Wet Flip`** (right wet polarity inverted — ⚠️ **mono-hostile, badged**, see §4) · `Warm`
  (recon ×0.35) · `Thin` (wet high-passed at 300 Hz, recon ×1.6).

### 3 · `Trio` — the LA studio tri-chorus
* **Lineage:** Dytronics CS-5 / Songbird TSC-1380 — three BBD voices panned hard L / C / R.
* **Mechanism:** 3 taps off one mono line, panned constant-power L / C / R, phases 0° / 120° / 240°
  from **one** accumulator, sine. Phase re-homes to the **tap spread** (unison → 120°). Base
  3–21 ms. A fast secondary bank is always on at 0.10 ms (the Dytronics preset mode).
* **Discriminator:** the centre tap is *identical in both channels*, so Trio has the best mono
  survival of the LFO Types — **−3.03 dB** vs June's **−3.65 dB** on the same probe, and the widest
  mono ripple (17.2 dB) because that centre tap combs coherently rather than cancelling.
* **Characters:** `Preset` (0.35 Hz + fast bank) · `Manual` (Rate free) · `Enhance` (**4 taps**,
  0/90/180/270 — a topology change, not a tone control) · `Sides` (centre −12 dB) · `Centre`
  (sides −6 dB) · `Syrup` (0.28 Hz, depth ×1.5, recon ×0.42) · `Rack 86` (compander ×1.5, noise −48)
  · `Glassy` (recon ×2.6, no grit, no compander, depth ×0.7).

### 4 · `Ensemble` — the string machine
* **Lineage:** Solina / Roland RS-202 — 3× MN3002, six LFOs as three pairs, one at 6.25 Hz and one
  at 0.66 Hz, spaced 120°.
* **Mechanism:** 3 (or 4) taps **summed to both channels**, the right channel's set **rotated** by
  Phase. **Both banks always on**: the slow bank rides Depth, and a fast bank at **6.25 Hz** is
  intrinsic at 0.25 ms whether or not Flutter is up — that is what makes a string machine sound
  like a section instead of one chorus.
* **Discriminator:** the **only** Type with two simultaneous modulation rates. d(t) shows lines at
  **0.54 Hz and 6.30 Hz** with the second at **0.152** of the first; the strongest second rate of
  any periodic Type is **0.084**. ⚠️ Measuring this needs harmonic exclusion — a triangle LFO puts
  1/9 of its energy at 3f, which read as a "second rate" of 0.11–0.15 on every triangle Type.
* **Characters:** `Solina` · `RS 202` (fast bank ×1.6) · `Choir` (**4 taps**) · `Random` (per-tap
  slow modulators become band-limited random walks — the UberMod 6TapRandom pattern-killer, a real
  topology swap) · `Dark Wine` (recon ×0.30, noise −54) · `Brass` (fast bank rate **×2 = 12.5 Hz**)
  · `Slow Tide` (0.33 Hz, depth ×1.3) · `Phase Wide` (tap spread 90° instead of 120°).

### 5 · `Micro` — the digital micro-pitch shift ★ *this is the "digital chorus"*
* **Lineage:** Eventide H3000 preset #231 MICROPITCHSHIFT (Layered Shift), #519, AMS DMX 15-80;
  Soundtoys MicroShift models the same three as Styles I/II/III.
* **Mechanism:** **NO LFO at all.** A dual (or quad) head reader per side runs at a constant slope
  — LEFT +cents, RIGHT −cents — crossfaded on a raised cosine. Detune floors at **6 cents** so the
  Type's engine is alive at the stored default without the Type ever writing a parameter. Rate,
  Depth, Flutter and Phase are all **re-homed** (§3) so none of them is dead.
* **Discriminator:** the only Type that shifts steady-state pitch. A 440 Hz tone comes out at
  **+5.62 cents** on L at the Detune floor and **±24.65 / ∓24.67 cents** at Detune 50; every other
  Type stays within **2.60 cents**.
* 🛑 **The bible's §3.5 recipe for this Type does not work** — see FINDINGS §1. It specifies a
  fixed 40 ms crossfade period with a deliberately tiny excursion, and built as written it shifts
  **nothing**. The free parameter has to be the ramp **span**; the period follows as span/r.
* **Characters:** `Studio I` (#231: tight, gentle) · `Studio II` (#519: base ×1.35, darker, more
  wander) · `Wander` (AMS: wander ×3 + a hard crossfade) · `Dual Mono` (both sides **+c** — a
  thickener, not a widener) · `Layers` (a −12 dB second pair further out, forced 4 heads, detune
  ×1.3) · `Tape Head` (compander ×1.4, recon ×0.45, drift ×1.5) · `Chorale` (4 heads, wander ×1.6,
  base ×1.2) · `Subtle` (detune ×0.4, base ×0.6).

### 6 · `Wow` — the aperiodic tape / vinyl chorus
* **Lineage:** transport wow and flutter; Chase Bliss Warped Vinyl. The deterministic stack is
  recycled verbatim from `TapeMachines.h` (0.6 Hz ±2.0 ms / 2.2 Hz ±0.8 ms / 7 Hz ±0.4 ms).
* **Mechanism:** 2 antiphase-weighted taps; the modulator is that stack (Depth, and the whole stack
  rides the Rate knob so Rate is never dead) **plus** a band-limited random walk (Drift, floored at
  0.35) with an **asymmetric glide** — rise τ 40 ms, fall τ 400 ms. Pitch sags fast and recovers
  slow, like a warped record passing the stylus.
* **Discriminator:** two, and they agree. d(t) periodicity **0.828** where every LFO Type measures
  0.994–1.000; and d(t) **skew −0.386** where every symmetric-LFO Type is within ±0.155. The skew is
  the asymmetric glide showing up as a statistic — nothing else in the roster is skewed.
* **Characters:** `Cassette` · `Vinyl 33` / `Vinyl 45` (add a locked 0.55 / 0.75 Hz revolution warp
  on its own accumulator) · `Dictaphone` (drift ×3, recon ×0.30, noise −50) · `Pro Reel` (drift ×0.3,
  fast bank ×2 — flutter-dominant) · `Dying Deck` (drift ×4, compander ×2) · `Underwater` (recon
  ×0.18, depth ×1.5) · `Breeze` (**stack off** — pure random shimmer, a topology change).

### 7 · `Dark` — the long-line dark BBD
* **Lineage:** the cheap 4096-stage pedal: chorus, doubler and murk in one.
* **Mechanism:** 2 taps, R staggered **×1.31** (asymmetric — every other 2-tap Type is near
  symmetric), triangle antiphase, base window **6–40 ms** (the only Type that reaches slapback),
  compander ×1.5, and the **full 3-pole BBD reconstruction chain** whose corner also tracks the
  instantaneous delay.
* **Discriminator:** the **steepest reconstruction slope in the roster** — **11.7 dB/oct** measured
  on a deterministic 140-harmonic probe, against a next-steepest of **9.2** (Micro) and 3.5–7.0 for
  everything else. Colour moves the *corner* on every Type; **nothing moves the slope**, so this is
  a Type-only property no knob can imitate.
* 🛑 **This is NOT the discriminator the bible specifies**, and the bible's one measured RED. §2.7
  says Dark's tell is that its cutoff tracks d(t) so its brightness breathes where June's does not,
  gated at ≥6 semitones vs June's <0.5. Measured at matched geometry: **June 7.48 dB of brightness
  excursion, Dark 7.33 dB** — Dark is a hair *lower*. Every Type's brightness already breathes under
  a sweep. Full write-up in FINDINGS §2. The tracking stays (the physics is right) but it is not
  what makes Dark a Type.
* **Characters:** `Standard` · `Double` (base ×1.8, depth ×0.4 — the ADT setting) · `Murk` (recon
  ×0.35) · `Pumped` (compander ×2.5 — audible breathing at the real bus level) · `Hissy` (noise −44)
  · `Cheap` (±0.4 ms clock jitter — the £15-pedal warble) · `Slap Wide` (R stagger ×1.6) ·
  `Regen Box` (feedback **floor** 45 %: the knob still travels 45→82 %, it just never starts dry).

---

## 2. Cross-type distinctness matrix

Every pair, on a **phase-independent** composite. 1.00 = one JND unit; the gate is 1.00.
Axes and their JND denominators: mono spec /3 dB · side spec /4 dB · L/R audio correlation /0.15 ·
correlation drift /0.07 · centroid modulation /0.10 · spectral flux /0.60 dB · d(t) periodicity
/0.15 · d(t) second rate /0.15 · steady-state pitch /2 cents · **d(t) L/R topology /0.20**.
(Values are capped at 10.00 for display.)

```
         Vintage    June   Pedal    Trio Ensemble  Micro     Wow    Dark
Vintage        .    4.94    6.10    5.97    4.92    6.29    4.37    5.16
June        4.94       .   10.00   10.00    5.12    6.01    4.03    2.66
Pedal       6.10   10.00       .    6.17   10.00    6.15    9.13   10.00
Trio        5.97   10.00    6.17       .   10.00    6.30    9.13   10.00
Ensemble    4.92    5.12   10.00   10.00       .    5.54    3.87    4.82
Micro       6.29    6.01    6.15    6.30    5.54       .    4.18    6.00
Wow         4.37    4.03    9.13    9.13    3.87    4.18       .    4.02
Dark        5.16    2.66   10.00   10.00    4.82    6.00    4.02       .
```
**Worst pair 2.66 — June/Dark, separated on side spectrum.** That is the pair the bible put on
probation and it clears the gate by 2.7×. The raw descriptors:

```
Type       corr   drift  centMod  flux  dPeriod  d2rate  dSkew  d(t)L/R   cents  mono/L
Vintage   -0.04  0.221   x1.53  4.66    1.000   0.039  -0.03   -0.017   +0.09   -3.26 dB
June      -0.14  0.042   x1.36  3.68    0.996   0.082  -0.03   -1.000   +1.03   -3.65 dB
Pedal     -0.07  0.003   x1.38  3.52    0.994   0.061  +0.07   +1.000   +0.02   -3.31 dB
Trio      -0.01  0.028   x1.85  4.63    0.999   0.038  -0.15   +1.000   -1.10   -3.03 dB
Ensemble  +0.63  0.009   x1.60  4.88    0.999   0.152  -0.00   -1.000   -1.41   -0.89 dB
Micro     -0.12  0.030   x1.41  4.41    0.936   0.983  -0.17   +0.010   +5.62   -3.58 dB
Wow       +0.09  0.074   x1.41  4.04    0.828   0.686  -0.39   -0.827   +0.67   -2.73 dB
Dark      -0.09  0.014   x1.32  3.36    0.995   0.084  -0.01   -1.000   +2.60   -3.43 dB
```

---

## 3. The controls

### Front — 3 heroes + Mix

| Knob | Range / taper | Default | What it does |
|---|---|---|---|
| **Rate** | 0.02–20 Hz exponential (mid-knob 0.63 Hz); synced: the 20-entry `Free · 4 bar → 1/256` list | 0.35 | How fast the voices swim. On locked-rate Characters it scales the locked rate ±1 octave and the readout shows the result — never a dead knob. |
| **Depth** | 0–100 → the Type's ms window | 0.50 | How far they swim. Measured 0.00 → 5.68 ms peak-to-peak on June, monotonic in five steps. |
| **Feedback** | 0–100 → **0–0.82** loop (Micro 0–0.65) | 0.00 | Regeneration: comb bloom → near-flange wash. Measured 28.1 → 39.2 dB of wet comb depth. |
| **Mix** | equal power; **100 % = fully wet, zero dry** | 0.50 | Dry ↔ wet. Dry residual at Mix 100 measured **−142.4 dB** on every Type. |

> **Why Feedback is on the front and Width is on the back.** `CONTRACT.md` §2 names the third front
> field `feedback`, and §4 asks the three fx3 devices to share vocabulary where the concept is
> genuinely the same. Labelling that field "Width" while the struct says `feedback` is the fb373
> failure class — a control that says one thing and drives another. So the front row is
> **Rate / Depth / Feedback / Mix on all three devices**, and Width takes back slot 3.

### Back — 8 knobs, 4×2

| Slot | Knob | Range / taper | Default | What it does, and why it beat the alternatives |
|---|---|---|---|---|
| P1 | **Time** | 0.5–40 ms, log, **per-Type window** | 0.50 | The base voice delay = where the comb sits. Measured 1.00 → 12.00 ms on June by autocorrelation lag. This is the knob that turns a shimmer into a doubler; nothing else can move the comb. |
| P2 | **Detune** | 0–50 cents, floored per Type | 0.00 | A **constant** pitch split, L up / R down, from the micro-shift reader. **This is the only control that does anything to a bare sine wave** — a comb does nothing to a single partial, so without Detune the device has no answer to "make this sine wide". Measured ±24.65 cents at 50 %, symmetric to 0.02 cents. |
| P3 | **Width** | wet M/S side gain 0…1.6 | 0.70 | Stereo size of the **wet only**. Dry is never side-boosted (that is the classic mono-collapse bug) and Width 0 is **mono, not silent** — no Type produces a pure-side wet. Measured −7.9 → +4.1 dB side/mid. |
| P4 | **Flutter** | 0–100, a 4.5–7 Hz bank | 0.25 | The fast modulation bank riding on top of Rate — calm ↔ nervous. This is what separates "a pad breathing" from "a pad shimmering"; it is the string-machine vibrato LFO made a knob. Measured 0.000 → 1.000 ms, and its energy is provably **high-rate** (d(t) energy above 3 Hz: 0.019 → 0.206). |
| P5 | **Drift** | 0–100, band-limited random walk ±2.5 ms | 0.00 | Aperiodic wander — tape soul on any Type. A hold-and-slew **walk**, not noise through a one-pole (the Worn-walk law: a smoother is not a walk and measures near-zero drama). Measured 0.00 → 4.53 ms with the LFO off. |
| P6 | **Colour** | 0 = murk (grit ×8, recon 1.8 k) · 50 = modelled BBD · 100 = studio clean (recon 16 k, poly off) | 0.50 | The whole BBD chain on one knob, and it is **physics, not EQ**: HF ratio −32.7 → −24.5 dB *and* THD −29 → −46 dB across the sweep, because the poly comes off as the filter opens. |
| P7 | **Low Keep** | 20 Hz–1 kHz, log (20 = off) | 0.00 | A real **2-band split**: below the crossover the signal stays **dry and centred**, and only its width follows Mix. This is the "do not lose the mono of the bass" knob, and it is the one control on the panel that makes the device safe on a bass patch at Mix 100. Measured: wet side at 90 Hz −64.2 → −87.6 dB while the mono bass stays at −54.2 dB against a −55.0 dB dry reference. |
| P8 | **Phase** | 0–180°, **re-homed per Type** | 1.00 | The L/R modulator relationship: mono-thick ↔ antiphase-wide. Measured L/R correlation +0.84 → −0.13. Distinct from Width: Width scales what is *already* there, Phase decides whether there is anything to scale. |

**Rejected for the back panel, and why:** a wet **Tone/LP** (Colour already owns the recon corner —
two knobs on one axis is the fb-era "params play their roles" violation); **Voices / tap count**
(that is a Type and Character property, per R4 — a knob that changes topology makes the dropdowns
meaningless); **Pre-delay** (Time is the pre-delay); **Spread** (Width and Phase already span the
stereo axis and a third would be a dead knob on half the roster); **Dimension-style cross-mix**
(the bible's §0 boundary sends that to the Hyper device, and I am respecting it).

### Per-Type knob liveness — the 96-cell sweep

Every (Type × knob) cell, 0 → 100, scored on `max(mono-spec dB, side-spec dB, 40·Δcorr,
25·Δcentroid-mod, 6·Δflux)`. Gate 1.5. **No dead cells; weakest is Micro/Feedback at 6.35.**

```
             Rate   Depth  Feedbk     Mix    Time  Detune   Width Flutter   Drift  Colour LowKeep   Phase
Vintage     10.79   23.90    9.94  207.27    8.00   28.22  210.37    9.97    7.32   16.83   31.74   19.78
June        12.71   24.73   18.04  200.43    6.92   24.00  203.53    7.86    8.64   16.10   32.19   22.98
Pedal        8.04   15.45    8.76  200.62    6.93   24.78  203.72   10.83   24.22   15.65   29.11   74.26
Trio         7.17   15.95    8.82  204.41    9.47   23.85  207.51    8.04    9.27   15.93   33.74   22.41
Ensemble     7.81   14.16    7.22  196.56   15.31   23.53  199.66    8.83   18.22   16.54   25.36   66.52
Micro       11.71    8.80    6.35  207.05    7.68   23.36  210.15   11.65    8.18   12.00   32.70    9.08
Wow         10.21   17.91   11.50  199.76    8.86   25.93  202.86   10.86    8.59   16.66   33.79   12.40
Dark        14.87   17.30   12.00  205.14   16.27   27.62  208.24    7.95    8.02   36.69   31.93   19.32
```

**Micro has no LFO, which would strand four knobs.** All four are re-homed rather than greyed —
a greyed knob on a fixed 4×2 chassis is a dead slot, which the chassis does not allow:

| Knob | every LFO Type | **Micro** |
|---|---|---|
| Rate | LFO Hz | the **crossfade span** 45 → 20 ms (fewer artifacts ↔ faster cycling) |
| Depth | ms excursion | **delay wander** on a slow random walk (the AMS axis) |
| Flutter | the fast bank | **crossfade jitter**, ±12 % of the span (the DMX de-glitch tell) |
| Phase | L/R modulator opposition | the **L/R stagger ratio** 1.0 → 1.5× |

`Pedal` likewise maps Phase to its **second read offset** (0 → 1.2 ms) rather than to modulator
opposition — one line read twice has no opposition to give. Measured cell value **74.26**, the
strongest Phase cell in the roster.

---

## 4. Mono-compatibility

Width touches the **wet only**; every Type's wet pair carries real mid energy, checked structurally
rather than by ear. Mono fold-down at Mix 50 / Depth 60 / Width 70:

```
Type       mono/L dB   ripple vs dry dB
Vintage      -0.63         9.20
June         -0.61         9.62
Pedal        -0.55         9.78
Trio         -0.57        17.20
Ensemble     -0.26        10.66
Micro        -0.88         5.49
Wow          -0.56        12.46
Dark         -0.78        13.90
```
**Nothing cancels — the worst Type loses 0.88 dB folding to mono.** All 64 Character rows were swept
as well; the worst *unflagged* row is **Pedal/Vibrato at −2.32 dB**.

**One row is mono-hostile, and it ships labelled rather than hidden** ("no playing safe"):
`Pedal / Wet Flip` inverts the right wet polarity — the two-amps hack. Measured **−5.2 dB** at
Phase 180 and **−64.5 dB at Phase 0**, where the two reads coincide and the wet nulls completely.
`TerrainChorusFx::charIsMonoHostile(type, chr)` returns true for exactly this one row of 64; the UI
must badge it.

---

## 5. What was cut, and why

| Cut | Where its sound went |
|---|---|
| **`Modern` / a dedicated "digital chorus" Type** | Considered and cut. The only things separating a clean digital chorus from `June` or `Trio` are (a) colour, which is the **Colour knob at 100**, and (b) tap geometry, which the Characters already vary. *A Type a knob can imitate is a fake Type* — the bible's own law. Max's "digital chorus" is answered by **`Micro`**, which is genuinely digital (a pitch shifter that could not exist in the analog domain), plus `June/Clean` and `Trio/Glassy`. |
| **Dimension / SDD-320** | The bible's §0 boundary: it ships in the future Hyper/Dimension device, mirroring Serum 2's own split. Only its two lessons are stolen — `Low Keep` and wet-only width. |
| **Hyper / supersaw unison detune** | Hyper device. |
| **Rotary / vibe** | Different physics (AM + spectral rotation). Out of scope. |
| **A 32-tap smear** | The delay device's `Diffuse` type already ships it. |
| The bible's **§3.5 micro-shift geometry** | Replaced, not cut — it does not work as specified. FINDINGS §1. |
| The bible's **Dark discriminator** | Replaced with the one that measures. FINDINGS §2. |
| `WET_GAIN 2.5`, the tanh/sinh "compander", the unglided `setParams`, the `jassert`-only buffer guard | The four legacy traps. Not copied; each is documented at its site in the header. |

---

## 6. Integration notes for the owner

* **Birth cardinality (fb342).** `kNumTypes = 8` is the LIVE roster. Declare the APVTS choice at
  **`kNumTypeSlots = 12`** (8 live + 4 reserved, greyed, clamped to 0 by `setParams`). Character is
  **always 8**. Cardinality is fixed at construction and growing it renormalises every stored value.
* **Zero latency, structurally.** Never call `setLatencySamples` for this device and never add
  lookahead. The rack's main-send exclusion sums subtract the routed dry sample-aligned; a
  latency-reporting device makes the host delay-compensate the plugin while the subtracted dry stays
  un-delayed, and the dry leaks back phase-smeared. This is rack-wide, not local.
* **Tempo sync** uses `divNames()` / `divBeats()` — the 20-entry list cloned whole from
  `PluginProcessor.cpp:3479`, `Free` at index 0 included. Identical in all three fx3 devices.
* **Telemetry for the card:** `viz()` gives `lfo` (−1..+1 needle), `lvl`, `notch[8]` (each live tap's
  first comb null in Hz, 0 = unused) and `depthNow` (ms). `liveRateHz / liveBaseMs / liveCents /
  liveDelayMs / liveTaps` are there for readouts.
* **Suggested factory presets** (values are knob positions): `Init June` June/I · `Polysynth II`
  June/II Depth 60 Mix 40 · `Tears In Rain` June/I Plus II Mix 55 Width 85 · `First Pedal`
  Pedal/Chorus Time 0.42 Phase 0.29 Width 100 · `Dirty Preamp` Pedal/Grit Colour 22 · `Session 1984`
  Trio/Preset Flutter 25 Width 90 · `Syrup Rack` Trio/Syrup Feedback 20 Colour 40 · `String Machine`
  Ensemble/Solina Depth 65 Flutter 45 Mix 60 · `Choir Box` Ensemble/Choir Low Keep 150 Hz ·
  `Wide Vox` Micro/Studio I Detune 18 Low Keep 250 Hz Mix 50 · `Double Tracker` Micro/Wander
  Detune 28 Width 55 · `Warped Record` Wow/Vinyl 33 Depth 60 Drift 70 Colour 30 · `Basement Tape`
  Dark/Pumped Time 0.72 Colour 15 Feedback 35 · `Bass Safe` June/Clean Low Keep 320 Hz Width 45.
  Every Character is already level-matched to 0.05 dB, so presets only need Mix and Low Keep checked.
