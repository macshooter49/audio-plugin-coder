# PHASER — the locked roster

Device: chain kind **8**. Engine: `TerrainPhaserFx.h`. Harness: `phaser_cert.cpp` (65 gates, 0 fail).
Contract: `Design/fx3/CONTRACT.md`. Bible: `Design/PHASER-BUILD-BIBLE.md`.

**9 Types × 8 Characters = 72 voicings. 3 front knobs + Mix + 8 back knobs = 12 params.**

---

## 0. The one thing that governs every decision here

A phaser is **all-pass**. `|A(f)| = 1` at every frequency, always. The effect exists only because
the all-pass branch is **summed** with the straight branch. Two consequences run through this whole
document:

1. **The wet magnitude spectrum is not a discriminator.** Measuring it proves nothing, and
   sample-difference RMS actively lies (fb282: 102 % "divergence", 0.02 dB of real magnitude
   change, Max heard nothing). Every discriminator below is **notch geometry** — count, centres,
   spacing ratios, depth, inter-notch peak gain — or the **trajectory** of that geometry in time.
2. **The dry/wet sum lives INSIDE the effect, not in the Mix knob:**
   `phaserOut = x + apBlend·(A{x} − x)`, and Mix crossfades dry against *that*.
   If Mix had been the summer, notch depth would peak at Mix 50 and vanish at Mix 100 —
   non-monotonic by construction, and Mix 100 would sound *less* phased than Mix 50. With the sum
   internal, notch depth is monotonic 0 → −134 dB (measured) and law 1 is satisfiable at all.
   `apBlend = 1.0` (pure all-pass, no sum) is the Uni-Vibe vibrato voicing and is a **Character** bit.

---

## 1. Types — the 9-entry header pill

| # | Type | Lineage | Mechanism (what is physically different) | Measurable discriminator | Measured |
|---|------|---------|------------------------------------------|--------------------------|----------|
| 1 | **Ninety** | MXR Phase 90, script & block | N **identical** 1st-order JFET stages (Spread 0 collapses the stagger), triangle LFO, feedback tapped from the last stage, linear loop | 2 notches from 4 stages, and their ratio is the **phase law** `tan(3π/8)/tan(π/8) = 5.83`, not a component property | 2 notches, **5.76 : 1** |
| 2 | **Stone** | EHX Small Stone (CA3094 OTA) | 4 stages **+ a dedicated extra all-pass INSIDE the feedback path**, OTA soft-clip standing in the loop, loop bandwidth ×0.35 (the OTA's own), hyper-triangle LFO | the extra loop stage changes the loop phase law, so the resonant peaks land where a 4-stage loop structurally cannot put them | peaks **+22.3 dB**, loop THD **0.20 %**, lowest notch **0.46 oct** from Ninety at matched knobs |
| 3 | **Duo** | Mu-Tron Bi-Phase / Arturia BI-TRON | **TWO cascades and TWO sweep generators**, both derived from one accumulator (`phase × ratio`); series / parallel / cross-feed / wide per Character | a single generator translates its comb **rigidly**; two generators sliding through each other change the comb's **shape**, so the best-shift frame correlation drops | rigidity **0.852** vs Ninety 0.978 / Twelve 0.965 |
| 4 | **Twelve** | Moog MF-103 | 12 identical stages (6 notches), sine LFO, full resonance, and the LFO's Hi range reaches **250 Hz — audio rate** | 6 notches; and at Rate 100 with `Hi Range` it makes **FM sidebands at ±250 Hz** that no other Type can reach (every other Type tops out at a 20 Hz LFO) | **6 notches**; sidebands **−13.9 dBc** vs Ninety **−84.1 dBc** |
| 5 | **Kraut** | Schulte Compact Phasing A | 8 stages, **LDR duty-warped** sweep (the warp moves the *peak*, so the rise and fall take different fractions of the cycle), lamp lag, **nonlinear filter in the feedback path** (ChowPhaser topology) | sweep rise/fall time asymmetry | **2.47 : 1** (triangle control 1.11 : 1) |
| 6 | **Vibe** | Shin-Ei Uni-Vibe | 4 stages staggered by the **measured capacitors** 0.015 µF / 0.22 µF / 470 pF / 4.7 nF — breaks ∝ 1/C ⇒ 0.616× / 0.042× / 19.66× / 1.966× of the geometric centre. Wildly unequal on purpose | notch ratios are **inharmonic** — they match no single-stagger law; and the wet-only voicing wobbles **pitch** | Vibe **57.25 : 1** vs Ninety **5.82 : 1**; **30.3 cents** at Mix 100 |
| 7 | **Barber** | Bode / Esqueda-Välimäki-Parker, DAFx-15 Method 1 | **Not an all-pass cascade at all**: M cascaded 2nd-order **notch** biquads, one interval apart, sawtooth centre + raised-cosine depth window. No loop, no delay line | the whole comb pattern translates in **one direction forever**, and the cycle wrap must be silent | **100 % monotone** up, **100 %** down (LFO control 53 %); wrap click **−110.9 dBFS** |
| 8 | **Envy** | Eventide Instant Phaser / Mu-Tron envelope mode | The **motion source is the circuit** (Eventide made the sweep source a first-class selector in 1971): a peak / RMS / transient follower drives the sweep, pre-wired hot (+2.6 oct before Touch), with env→resonance on `Quack` | the notch trajectory **tracks the program envelope**, and on silence it **parks** | rank **r = 0.852**; drift on silence **0.000 octaves** |
| 9 | **Steps** | S+H / modular practice | Sweep is a **sample & hold** clocked by Rate (sync divisions), 8 sources; Lag is the glide between holds | trajectory is **piecewise constant** | **99 %** of frames held (continuous LFO 27 %) |

**Cross-type distinctness (every pair, 19 phase-independent features, JND-normalised):
closest pair 8.57× JND (Stone / Vibe).** Full matrix in `FINDINGS.md`.

### ✂️ Cut from the bible's roster
- **`Ultra` as a Type** (already cut in the bible) — a rate range is not a circuit. It is
  `Twelve · Hi Range` / `Fast Hollow`.
- **SSB barberpole** — stays with the Bode device (bible §0 boundary table). Confirmed, nothing folded in.
- **Nothing else was cut.** All nine Types survived measurement; the roster is the bible's,
  validated. See `FINDINGS.md` for what had to *change* to make Ninety and Stone genuinely distinct
  (at first draft they measured 0.77× JND apart — "a Phase 90 with an extra loop stage" is not a
  second Type).

---

## 2. Characters — 8 per Type, each re-wiring PHYSICS

The Character table is data, not code branches: `stageBias · lfo shape · loop topology · S+H source /
detector law / ladder direction · signed feedback floor · stage scatter · stagger multiplier · lamp
time · LFO-B ratio · all-pass blend · excursion · loop drive · duty warp · notch Q · stereo phase`.
Nothing in it is a tone control (law R4 / fb345).

**Every Type's 8 were measured pairwise on 14 features. Weakest pair in the whole grid: 2.03× JND
(Steps `Ladder Up` / `Pendulum`, separated by LFO shape).**

| Type | 8 Characters — and what each one re-wires |
|---|---|
| **Ninety** | `Script 74` no feedback resistor (k floor 0) · `Block 78` R28 regeneration (k floor +0.40) · `Two Stage` −2 stages ⇒ **one** notch · `Eight Stage` +4 stages ⇒ **four** notches · `Slow Lamp` lamp lag ×8 · `Sine Sweep` LFO shape → sine · `Wide Stagger` stagger exponent ×2.4 · `Negative` **k floor −0.45** (peaks land ON the notches) |
| **Stone** | `Color Off` clean loop · `Color On` k +0.55 **and** depth ×1.4 (the real switch does both) · `Deep Sweep` excursion ×1.9 · `Two Loop Stages` a **second** all-pass in the loop (peaks move again) · `Hot OTA` loop drive ×4 · `Cold OTA` k floor +0.75, lamp ×2.5 · `Six Stage` +2 stages · `Inverted` k floor −0.60 |
| **Duo** | `Series 1:1.33` combs multiply · `Series 3:4` ratio 0.75 · `Parallel 1:1.33` combs **add** · `Parallel Golden` ratio φ + depth ×1.4 · `Counter` LFO-B **inverted** · `Wide Duo` phasor B on the **right channel only** · `Slow B` ratio 0.25, depth ×1.6 · `Cross Feed` A's loop tapped from B (product clamped to 0.92) |
| **Twelve** | `Full Range` · `Hi Range` **rate top ×12.5 → 250 Hz** · `Six Pole` −6 stages · `Sixteen Pole` +6 stages · `Resonant` k floor +0.80 · `Hollow` k floor −0.65 · `Aux Out` R channel counter-phased 180° (the MF-103's second output) · `Fast Hollow` audio rate **and** negative loop |
| **Kraut** | `Slow Bulbs` lamp ×3 · `Fast Bulbs` lamp ×0.3 · `Hard Skew` duty warp +2 (rise takes ~94 % of the cycle) · `Reverse Skew` duty warp −2 · `Twelve Bulb` +4 stages · `Four Bulb` −4 stages · `Hot Loop` loop drive ×4.5, k +0.60 · `Cold Loop` **the nonlinear loop filter is removed** (plain loop) and k floor −0.55 |
| **Vibe** | `Chorus Lamp` classic 50/50 sum · `Vibrato Lamp` **all-pass only** (`apBlend 1.0` — the Chorus/Vibrato switch) · `Cold Bulb` lamp ×12 · `Hot Bulb` lamp ×0.12, depth ×1.4 · `Eight Cap` +4 stages (the cap pattern repeats an octave up) · `Even Caps` cap exponents ×0.30 (nearly harmonic) · `Wide Caps` ×1.75 · `Vibrato Deep` all-pass only + depth ×1.7 |
| **Barber** | `Rise 8` · `Rise 12` +4 notches · `Fall 8` ladder **descends** · `Fall 12` · `Rise Wide` interval ×1.6 · `Fall Narrow` interval ×0.55 · `Sharp Notch` Q ×3 · `Deep Rise` −2 notches, Q ×0.45 (fewer, fatter) |
| **Envy** | `Fast Grab` follower ×0.2 · `Slow Swell` ×3.5 · `Transient` detector = fast−slow (blips on attacks only) · `Smooth Follow` RMS detector + stagger ×2.2 · `Four Stage` −4 stages · `Ten Stage` +4 stages · `Quack` **env → resonance** (+0.35 of k) · `Sink` k floor −0.60 |
| **Steps** | `Random 8` 8 levels · `Random Wide` 16 levels + depth ×1.5 · `Ladder Up` · `Ladder Down` · `Pendulum` · `Register` 8-bit Turing loop, 1/8 mutate · `Drunk` ±1-step random walk, depth ×0.8 · `Trance Gate` floor↔ceiling, depth ×1.4 |

---

## 3. Front card — 3 knobs + Mix

| Control | Range → law | Measured span |
|---|---|---|
| **Rate** | free `0.01 → 20 Hz` log; **Sync** uses the 20-entry list cloned whole from `PluginProcessor.cpp:3479` (`Free · 4 bar … 1/256`). `Twelve · Hi Range` re-maps the top to **250 Hz**. One accumulator; the increment glides, the phase never jumps. | 0.09 → 11.99 Hz measured out of the modulation spectrum, monotone |
| **Depth** | `0 → ±4.5·t^0.8` octaves × Character. On **Barber** it is the notch depth, `−4 → −70 dB` — a sawtooth on `f0` would snap back and destroy the very illusion the Type exists for. | 0.17 → **7.04 octaves** of sweep, monotone |
| **Feedback** | `0 → 0.95` **magnitude**; the **sign is a Character bit** — see §5. Every Type has at least one negative-geography Character. | inter-notch peak **+3.0 → +23.1 dB**, monotone |
| **Mix** | equal-power crossfade against the phaser output, with a **correlation-aware trim** (`1/‖(dry + wet·(1−apBlend), wet·apBlend)‖`) because a phaser's wet is 50 % dry by construction and a naive equal-power blend rings +1 dB mid-mix and −3 dB at 100. Trim is exactly 1.0 at Mix 0, so Mix 0 stays bit-transparent. | notch depth `−0.3 → −133.9 dB`, monotone; **dry residual at Mix 1.0 worse than −72 dB across all 9 Types** |

---

## 4. Back panel — the 8, and why each one earns its slot

The test for a slot: *would a producer reach for it on a bare sine, a pad, a clav, and a mono bass —
and is it a thing only a PHASER has?* Six of the eight are phaser-only; two (`Stereo`, `Touch`) are
shared vocabulary with the chorus and flanger and use the same names and curves there.

| Pos | Knob | Range → curve | What it DOES | Why it beats the alternatives |
|---|---|---|---|---|
| **P1** | **Center** | `40 Hz → 9 kHz`, log (×225) | Parks the comb. With Depth 0 this **is** the manual phaser — Serum's "Rate 0 trick" as a first-class knob, and the mod-matrix destination. | The single most-used phaser gesture that isn't Rate. Measured: lowest notch **28 → 4099 Hz**, monotone. Nothing else moves the whole effect through the spectrum. |
| **P2** | **Stages** | per-Type window, stepped 2…16 (Barber relabels **Notches**, 4…12) | More stages = more notches. 4→2 notches, 12→6, 16→8. | **The** phaser-only control. A flanger's comb density is set by its delay; a chorus has no comb. Measured 3 → 6 notches, monotone, and the swap is click-free at −74 dBFS. |
| **P3** | **Spread** | stagger `r = 1 + 3t²` (1.0 → 4.0) × Character; Vibe scales the cap exponents 0.35→1.65; Barber sets the ladder interval 0.35→1.70 oct | How far apart the notches sit. r = 1 is the tight Ninety cluster, r → 4 is inharmonic scatter. | Also phaser-only, and it is the axis NI Phasis charges money for. Measured **1.14 → 4.28 octaves** between notches, monotone. |
| **P4** | **Stereo** | LFO phase offset `0 → 158°` **AND** a per-channel centre split `0 → ±0.42 oct` | Widens; at 100 the channels counter-sweep. | The split is the reason this is not a mono-killer. A pure 180° phase offset lets the L notch fill the R notch and the phasing audibly vanishes in mono (bible §6, pitfall #8). With the split, **every Type keeps ≥ 6.5 dB of mono comb at Stereo 100** — measured. |
| **P5** | **Touch** | bipolar, `±8 octaves` of env → sweep, through a soft-knee follower normalised to the −26 dBFS bus | Playing louder pushes the comb up (or down). | The auto-phaser. On a clav or a guitar-ish pluck this is the knob; on a pad it is a slow swell. **±8 oct, not ±4**: measured on a real −26 dBFS program the follower sits near 0.35, so ±4 swung only ±1.1 octaves — audible but not night-and-day. Measured span now **6.61 octaves**, monotone, bipolar. |
| **P6** | **Lag** | `4 → 200 ms` × Character; also sets the follower's attack `1 → 60 ms` and release `30 → 600 ms` | How fast the motion happens — **whatever the motion source is**. | This is the improvement over the bible's separate `Grip`. One time constant serves the lamp thermal lag (Kraut/Vibe), the S+H glide (Steps), the envelope speed (Envy) *and* the LFO smoothing on every other Type, so it is never a dead knob on a Type that has no envelope. Measured: excursion **6.63 → 2.76 octaves**, monotonically slugged. The 4 ms floor is also the de-zipper that makes the Steps detents click-free. |
| **P7** | **Floor** | `20 Hz → 1 kHz`, log | Keeps the wobble out of the bass — it is the **lowest notch's** lower bound. | 🔑 It is a **clamp on the sweep, not a filter**. The first build high-passed the `(A{x} − x)` difference here and it capped *every* null in the device at ~−37 dB (and a 5 kHz notch at −14 dB with Floor at 1 kHz) because a perfect null needs that difference to be exactly −2x. Below the lowest notch the cascade phase is small, `A ≈ 1`, and the output is dry anyway — so physics does the job a filter was breaking. Measured: lowest notch **29 / 95 / 309 / 1007 Hz** at Floor 10/40/70/100 — the knob *is* the notch. |
| **P8** | **Color** | in-loop LP `18 kHz → 800 Hz` + in-loop drive `0 → +24 dB` with the `1/g` makeup **inside** the loop | Darkens and dirties the resonance; at 100 it growls. | In-loop, not post — that is what makes it a *voicing* control instead of a distortion, and the in-loop makeup means `sat'(0)·makeup = 1` so cranking it can never push loop gain past k. Two measured jobs: THD **−60.3 → −28.6 dB** monotone, and the centroid of the resonant excess **1720 → 699 Hz** monotone. |

### What was considered and rejected
- **`Grip`** (a second envelope knob, per the bible) — folded into `Lag`, which does the same job on
  four motion sources instead of one. Two slots for one envelope was the most expensive thing on
  the panel.
- **`Drift` / `Age`** (Eventide Mk II component drift) — subtle by nature; it cannot pass law 1
  without becoming something else. The per-stage scatter it models is now a fixed per-Type
  constant (Ninety 0.06 oct, Kraut 0.06, Stone/Duo/Envy/Steps 0.05, Twelve/Vibe 0.03).
- **A `Quality` / oversampling dropdown** — none. Bible §3.7: the cascade is LTI between coefficient
  updates and creates zero new spectral content; the only nonlinearities are the bounded in-loop
  tanh and the output limiter. Nothing to oversample.
- **Splitting `Notches` from `Stages`** (the NI Phasis two-axis idea) — 1st-order stages give
  ⌊N/2⌋ notches by construction; a separate axis would be a lie on eight of the nine Types.

---

## 5. Why the Feedback knob is not bipolar (a deliberate deviation from the bible)

The bible specifies `Feedback ±100 → k = ±0.95·t²`. The locked `Params` struct defaults
`feedback = 0.0f`, and CLAUDE.md §4 says a new knob should not default to an extreme (double-click
reset to a rail hides itself as a snap-back bug). A bipolar knob whose neutral is 0.5 cannot have a
0.0 default without meaning "full negative".

So: **the knob is the magnitude (0 → 0.95) and the sign is a Character bit.** Every Type ships at
least one negative-geography Character — `Ninety · Negative`, `Stone · Inverted`, `Twelve · Hollow`
and `Fast Hollow`, `Kraut · Cold Loop`, `Envy · Sink` — and this is *more* fb345-compliant, not
less: the polarity flip is a physics change, which is exactly what a Character is for.

The geography flip itself is real and measured, not just a sign: at `k > 0` the loop phase reaches 0
midway **between** the notches (the block-Phase-90 mid-hump vowel); at `k < 0` the extra π puts the
peaks **at** the k = 0 notch frequencies (hollow honk). It shows up in the harness as the
inter-notch-peak feature and as the Character-distinctness separation on those rows.

---

## 6. Presets — the bible's 14, re-pointed at the shipped grid

Sketches only; the knob values below are 0–100 unless noted.

| # | Name | Type · Character | Sketch |
|---|---|---|---|
| 1 | First Phaser | Ninety · Script 74 | Rate 1/2 sync · Depth 45 · Center 35 · Stages 25 (=4) · Spread 0 · Mix 45 · Feedback 0 · Stereo 20 — **the device default** |
| 2 | Seventies Strut | Ninety · Block 78 | Rate 1/4 · Depth 60 · Feedback 35 · Color 15 · Mix 50 |
| 3 | Deep Stone | Stone · Color On | Rate 1 bar · Depth 80 · Feedback 55 · Color 45 · Floor 40 · Mix 60 |
| 4 | Twin Orbit | Duo · Parallel 1:1.33 | Rate 2 bar · Depth 65 · Spread 55 · Stereo 60 · Mix 55 |
| 5 | Six Notch Scream | Twelve · Resonant | Rate 1/8 · Depth 90 · Feedback 60 · Color 30 · Mix 70 |
| 6 | Sideband Engine | Twelve · Hi Range | Rate 85 free (≈ 90 Hz) · Depth 40 · Feedback 50 · Mix 65 |
| 7 | Kosmische Bus | Kraut · Slow Bulbs | Rate 4 bar · Depth 75 · Feedback 55 · Color 60 · Lag 70 · Stereo 35 · Mix 55 |
| 8 | Throb | Vibe · Chorus Lamp | Rate 4.8 Hz free · Depth 55 · Mix 60 · Floor 25 |
| 9 | Leslie Liar | Vibe · Vibrato Lamp | Rate 6.5 Hz free · Depth 45 · **Mix 100** · Stereo 45 |
| 10 | Up Forever | Barber · Rise 8 | Rate 0.15 Hz free · Notches 50 (=8) · Spread 48 (=1.0 oct) · Depth 70 · Mix 55 |
| 11 | Down Stairwell | Barber · Fall 12 | Rate 0.25 Hz · Depth 80 · Feedback 40 (=Q) · Color 40 · Mix 65 |
| 12 | Auto Quack | Envy · Quack | Touch 90 · Lag 20 · Depth 0 · Center 45 · Feedback 40 · Mix 70 |
| 13 | Rubber Down | Envy · Sink | Touch 15 · Lag 45 · Depth 0 · Feedback 30 · Mix 60 |
| 14 | Clockwork | Steps · Register | Rate 1/16 sync · Depth 70 · Spread 40 · Feedback 45 · Lag 5 · Mix 65 |

---

## 7. Integration notes for the owner

- Engine interface is exactly `CONTRACT.md §2`. `kNumTypes = 9`, `kNumChars = 8`, `static_assert` present.
- `Viz` publishes at 60 Hz: `lfo` (−1..+1 lagged sweep), `lvl`, `notch[8]` in **Hz**, `depthNow` in
  **octaves**. The notch frequencies are exact, not decorative — `ν_m = f_notch/f_c` are constants of
  the stage plan (scaling every stage break by a common factor scales the notches identically), so
  they are solved once per plan change by bisection and the card reads them for free.
- **No allocation anywhere reachable from `processStereo`.** Fixed arrays only; the 2048-entry tan
  LUT is a member and is built in `prepare`.
- `prepare` is safe to call repeatedly; `reset` never allocates.
- Sample rates: notch geometry measured **identical to 0.00 %** at 44.1 and 96 kHz vs 48 kHz.
- CPU at 48 kHz / 128: **6.4 – 12.9 µs per block per instance** (0.24 – 0.48 % of one core).
  Worst case named in the brief — 16 stages × 6 instances — measured **81 µs = 3.05 % of one core**.
- No oversampling, no quality tiers, no latency, no lookahead.
