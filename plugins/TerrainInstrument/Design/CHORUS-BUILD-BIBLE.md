# Terrain Instrument — Chorus Build Bible

*Researched 2026-08-14 (dedicated chorus researcher). Written to the DISTORTION-BUILD-BIBLE.md bar:
measured numbers, exact math, named lineage, zero hand-waving. A builder must be able to implement the
device from this file alone.*

**The one-paragraph thesis.** A chorus is one or more short (0.5–40 ms) delay lines whose lengths move,
mixed with the dry signal. Every legendary chorus is defined by exactly three choices: *how many taps*,
*what moves them* (the LFO topology), and *what the delay path does to the tone* (the BBD color chain).
That three-axis space — taps × modulation × color — is the whole device. Our Type dropdown walks the
first two axes through history's landmark answers; the back panel exposes the third axis everywhere.
The bus is −26 dBFS (house law 1), so every level-dependent block (compander, grit, noise gate) is
calibrated to 0.05 linear, not to 1.0 — this is the single most common way a copied chorus ships dead.

---

## 0. Scope decision (proposed — needs Max's lock)

**ONE Chorus device, the 4th FX-rack device**, on the frozen fb275 chassis: front card (3 hero knobs +
Mix + pills + live visualizer) + back panel (2 dropdowns + 8 knobs 4×2). Param grammar copies
`SYN_DLY_*` exactly (`ParameterIDs.hpp:374-394`) as `SYN_CHR_*`.

### What is IN (7 Types — §2)
The BBD synth chorus (Juno), the pedal chorus (CE-1/CE-2), the LA studio tri-chorus
(Dytronics/Songbird), the string-machine ensemble (Solina/RS-202), the H3000 micro-pitch "chorus
without wobble", the tape/vinyl wow chorus, and the dark long-BBD chorus. Seven distinct
tap-count × LFO-topology answers, each with a measurable discriminator.

### What is OUT, and where its sound went
* **Flanger** (sub-1 ms delay + strong feedback + through-zero) — its own future device. The boundary
  is delay time: below ~1.5 ms the comb fundamental enters the audible band and the effect reads as
  filtering, not doubling. Our `Time` knob floors at 0.5 ms so the *edge* of flanger territory is
  reachable (no playing safe), but feedback-dominant through-zero flanging is not this device.
* **Dimension / SDD-320** — lives in the future **Hyper/Dimension** device (Serum 2 ships Chorus AND
  Hyper/Dimension as separate menu entries; we mirror that split). Covered lightly in §1.4 because two
  of its tricks (bass-compensated dry, wet-only cross-mix) are stolen for our `Low Keep` and `Width`
  laws. The deep dive belongs to the Hyper bible.
* **Hyper / supersaw unison chorus** (Serum's 1–7 voice micro-delay detuner) — Hyper device.
* **Rotary/vibe** — different physics (AM + spectral rotation), out of scope entirely.

### The legacy June block is NOT this device
`TerrainChorus.h` (the "JUNE" section inside the DLY page of the legacy Terrain-FX-style chain,
`index.html:1492-1660` CSS / `:5447-5480` markup / `:9147-9149` param meta / `:9320-9322` juceIds,
params `CHORUS_AMOUNT/WIDTH/CHARACTER`, wired through `IndyFxChain.h:89-93,192-193,259-261`) **stays
untouched** where it is. The new device is a separate engine (`ChorusEngine.h`) in the SYN FX rack.
What we recycle from the old one is inventoried in Appendix A — and one of its "features" is a
measured trap (§9.1).

---

## 1. History and circuits — the lineage that defined the effect

### 1.1 The BBD physics (Raffel & Smith, DAFx-10 — the paper for this whole device)

A bucket-brigade device is an analog shift register: N capacitor stages clocked at f_clk, giving

```
delay = N / (2 · f_clk)          (two clock phases per stage-pair)
```

Chorus circuits use 512–1024 stages at high clock rates (a 1024-stage line at 10 ms needs
f_clk = 51.2 kHz). Everything musicians love about "BBD warmth" is a short list of measurable defects:

1. **Band-limiting.** The signal is *sampled* at f_clk, so anti-alias + reconstruction low-passes
   bracket the line, cut off at **1/3 to 1/2 of f_clk** (3rd-order Sallen-Key in + 3rd+2nd-order out is
   the canonical topology). Since delay ∝ 1/f_clk, **longer delay = darker signal** — the clock-darkening
   law. Our `DelayEngine.h:125` already implements exactly this (5.2 kHz → 2.3 kHz as time grows).
2. **Companding.** NE570/571 compander: a feedback compressor before the line, a feedforward expander
   after, each a 2:1 gain element driven by a one-pole average of |x| with
   **τ = 10000 · C_rect (C = 0.22–1 µF ⇒ τ ≈ 2.2–10 ms)**. Model: expander `f(x) = avg(|x|)·x`,
   compressor `f(x) = x / avg(|f(x)|)`. The pair is what "pumps" on transients. Raffel/Smith note
   short-delay chorus circuits often omit it — but the CE-1, SDD-320 and the Dytronics all have it,
   and its pump is audible character.
3. **Nonlinearity.** Unavoidable THD of an N-stage line ≈ `1.01^(N/1024) − 1` (~1 % per 1024 stages),
   *not* level-dependent clipping. Matched third-order fit: `f(x) = x − x²/8 − x³/18 + 1/8` on
   −1<x<1 (the +1/8 recenters the average). H2-dominant, falls linearly per harmonic.
4. **Insertion gain** is frequency-dependent: 0…+2 dB at LF, sagging to −4…−6 dB near the clock
   Nyquist — a gentle extra tilt on top of the reconstruction filter.
5. **Noise** ≥ 60 dB below max signal, *below* the compander (so it breathes with the expander).

### 1.2 Roland CE-1 (1976) → Boss CE-2 (1979): the pedal chorus

The CE-1 Chorus Ensemble is the chorus circuit lifted out of the JC-120 Jazz Chorus amp — "the mother
of chorus." One BBD line (MN3002-class), compander, **two modes**: *Chorus* (fixed preset sweep, one
Intensity knob) and *Vibrato* (Rate + Depth knobs, wet-only). Delay region ~5 ms with a slow sweep;
stereo out = **dry on one jack, wet on the other** — the "true stereo" that made the width, and the
version that collapses most gracefully in mono (dry+wet mono sum = classic single-comb chorus, never
cancellation). The CE-2 shrank it to MN3007/512-ish stages, mono dry+wet, pre-emphasis before the BBD
and de-emphasis after (a treble boost/cut pair that ducks BBD hiss — the same pre/de idea as
distortion's `Emphasis`).

### 1.3 Juno-60 chorus (1982) — the two-BBD antiphase trick ★ our default Type

Measured (pendragon-andyh/Juno60 repo, from the service notes + hardware capture):

| Mode | LFO rate | LFO shape | Delay sweep |
|---|---|---|---|
| **I** | **0.513 Hz** | triangle | **1.66 → 5.35 ms** |
| **II** | **0.863 Hz** | triangle | **1.66 → 5.35 ms** |
| **I+II** | **9.75 Hz** | LP-filtered triangle (≈sine) | **3.3 → 3.7 ms** (a vibrato) |

Architecture: **one triangle LFO, two 256-stage MN3009 BBD lines (~70 kHz clock), the right line's
modulation INVERTED (180°)**. Both outputs are wet+dry mixed per side. The antiphase inversion is the
whole trick: when L sweeps sharp, R sweeps flat, so the ear hears width and shimmer instead of pitch
wobble — and the mono sum averages the two opposite combs nearly flat (§3.8). A 12 dB/oct LP before
the line anti-aliases; the noise floor is famous ("Juno hiss").
Florian Anwander's Roland survey confirms 0.4/0.8 Hz triangle modes + ~8 Hz sine for I+II — same
circuit family, slightly different measured constants; we ship the pendragon numbers (captured from a
real unit) as the `I`/`II`/`I+II` Characters.

### 1.4 Roland SDD-320 Dimension D (1979) — light coverage (deep dive → Hyper bible)

2× MN3007, four preset buttons. Measured: modes 1-2 = **0.25 Hz**, modes 3-4 = **0.50 Hz**, base delay
**7.5–10 ms**, modulation **±1.5 to ±2.5 ms**, compander in the loop, dry passed with a **bass boost**
while wets are HP'd ~80 Hz, and the wet **cross-mixed to the opposite channel with inverted polarity**.
That polarity trick is "space without warble." We steal exactly two lessons: (a) keep the lows dry and
centered (`Low Keep`, §4), (b) width belongs on the WET only. The device itself → Hyper.

### 1.5 String machines — Solina / Roland RS-202 (1975-6): the 3-phase ensemble

The "ensemble" sound is **three BBD lines (3× MN3002) panned/summed, each driven by the SUM of two
LFOs — one slow ("chorus") ~0.6–0.66 Hz, one fast ("vibrato") ~6–6.25 Hz — with the three lines'
phases spaced 120°** (RS-202: six LFOs as three 120° pairs at 6.25 Hz + 0.66 Hz; Solina/Haible triple
chorus: 2 LFOs × 3 outputs each, 120° apart, one CV mix per BBD). Because the three combs rotate
around the phase circle, *some* pair is always maximally detuned — the sound never breathes in unison.
This is why one string machine sounds like a section. Deep modulation, wet-dominant mix; the fast LFO
rides on top of the slow (two visible sideband rates — the discriminator, §2.4).

### 1.6 The LA studio tri-chorus — Dytronics CS-5 / Songbird TSC-1380 / Dyno-My-Piano TSC-618 (early 80s)

**Three BBD voices panned hard L / C / R, each with a dedicated LFO** (same 120° family as the
ensemble but *panned apart* instead of summed), preset mode = slow sweep + secondary faster LFO;
manual mode measured on the UAD recreation at **0.03–7.45 Hz**; per-channel depth ("Chorus Wave Form")
knobs; optional per-channel feedback ("Choral Enhance"). The center voice keeps the mono sum solid
while the sides swirl — the "syrupy" Michael Landau / 80s session-guitar wall. Eventide's
TriceraChorus is the modern homage (Rate 0.1–20 Hz, per-voice Depth L/C/R, stereo Detune ±40 cents
L/R, Chorus vs Chorale modes, mix→100 % = vibrato).

### 1.7 Eventide H3000 micro-pitch (1987) — chorus without an LFO

Preset **#231 MICROPITCHSHIFT (Layered Shift algorithm)**: left voice a few cents sharp, right a few
cents flat, each behind a small unequal delay. No cyclic wobble at all — the "detune" is a *constant*
pitch offset made by a delay line whose read head slides at constant slope with crossfaded dual heads.
Preset #519 is the same trick on a different shifter (different delay variation + frequency response);
the AMS DMX 15-80 does it with wider delay wander and a harder de-glitch. Soundtoys MicroShift models
exactly these three as Styles I/II/III, with **Detune (% of the style's cents), Delay (% of the
style's ms, "Tight↔Loose"), Focus (2-band crossover 20 Hz–10 kHz, wet applied to the HIGH band only),
Mix**. The de-facto standard for "wide but not wobbly" vocals/synths — and the reason our device does
not need an LFO to be a chorus (§2.5).

### 1.8 Tape/vinyl wow — the aperiodic chorus

Wow (0.5–2 Hz), flutter (6–10 Hz) and random scrape from tape transports modulate delay just like an
LFO but *aperiodically*. Chase Bliss Warped Vinyl (1024-stage BBD modulated with warped-record ramps
+ a `Lag` asymmetric-glide knob) proved this as a chorus genre. We already own the exact machinery:
`TapeMachines.h` `SmoothRandom` (`:215`) and the Cassette triple-LFO wow stack (0.6 Hz ±2.0 ms /
2.2 Hz ±0.8 ms / 7 Hz ±0.4 ms — recorded in the distortion bible's recycle audit).

### 1.9 The modern references

* **Serum 2 Chorus** (confirmed from the Serum manual + Serum-2 coverage): **4-voice (2 L taps +
  2 R taps)**; `Rate` 0–20 Hz or synced (BPM Sync switch, snaps to musical divisions to 1/32),
  `Delay 1` / `Delay 2` (ms offsets of the two stereo tap pairs), `Depth` (mod amount = "how much
  pitch warble"), `Feedback`, wet `LP Filter`, `Mix` (Serum 2 adds a per-FX Level slider). Serum's
  chorus **panel is knobs-only — no dedicated animated visualizer** (Serum 2's FX view adds graphic
  headers per module, but nothing that renders the chorus motion itself). That is the opening our
  card walks through (§5). Serum 2 splits Hyper/Dimension into its own device — validating our §0 cut.
* **Valhalla UberMod** (Costello's chorus-topology encyclopedia, and the best public taxonomy):
  2TapChorus (antiphase triangle + quadrature vibrato = Dimension/Juno family), 4TapEnsemble (VP-330),
  SuperSix (6 taps, staggered triangles = supersaw), **6TapRandom (6 taps, RANDOMIZED triangle slow
  LFOs, antiphase L/R — "less audible patterns for the detuning")**, DualEnsemble (independent 3-phase
  per channel = Solina ×2), 8/16/32Tap. Two LFO banks: "slow LFOs create the base detuning, fast LFOs
  add string-ensemble vibrato." Series-allpass diffusion optional after the taps. The lesson we take:
  **slow+fast dual-bank modulation and per-tap unique phase are what create 'expensive' chorus**; and
  randomized-triangle LFOs kill the pattern-repetition that makes cheap chorus sound like a dishwasher.
* **TAL-Chorus-LX**: Juno chorus extracted verbatim — two mode buttons + volume/dry-wet only. Proof
  that a great Type needs almost no knobs (our Character presets carry that torch).
* **Arturia Chorus JUN-6**: modes I/II/I+II + a Manual mode (Rate, **Depth 0.00–10.0 ms, default
  4.44 ms**, **LFO Phase** as the width control, Mono-input switch, tempo sync, Mix). Arturia's
  Dimension-D manual documents the full SDD-320 flow (compressor+filters → BBD → expander+filters →
  polarity-inverted cross-mix → width) — our §1.4 numbers.

---

## 2. The Types — 7, each a different taps × LFO answer  *(law 5: night-and-day or cut)*

All Types share the color chain (§3.4) and the house glide/fade laws. `Character` (dropdown 2) selects
a per-Type voicing row from a `CharBias`-style constant table (the `VintageReverb.h:337-339` house
pattern — coefficients only, no code branches).

**The family tell** (the §1-thesis made measurable): sweep each Type at default and read the
**modulation spectrum of the wet delay trace d(t)** — periodic single-line (Pedal), two antiphase
lines (June), three lines rotating 120° (Trio, Ensemble), two rates at once (Ensemble), a DC line
with no AC (Micro), 1/f-ish noise (Wow), single line + heavy HF loss + pump (Dark). Seven visibly
different d(t) spectra = seven legitimate Types.

### 2.1 `June` — the Juno-60 twin antiphase BBD ★ default
* **Recipe:** 2 taps (1/side), ONE master triangle phase, right tap reads the LFO inverted
  (`lfoR = −lfoL` — exactly `TerrainChorus.h:19,42` `RIGHT_PHASE_OFFSET = π`). Base 3.5 ms, sweep
  1.66–5.35 ms at Depth 100 (the measured hardware window; Depth scales the excursion around the
  window center). Wet+dry per side.
* **Characters (8):** `I` (0.513 Hz lock), `II` (0.863 Hz lock), `I+II` (9.75 Hz, sweep pinned to
  ±0.2 ms, LP'd triangle), `Manual` (Rate knob live — the JUN-6 manual-mode gift), `Aged` (compander
  ×2, noise −54 dB, recon LP 6 kHz), `Clean` (compander off, recon 16 kHz), `Wide 106` (adds +2 %
  R-rate skew — the 106-style dual-clock shimmer), `Deep` (window doubled 1.7–10.7 ms).
  In `I`/`II`/`I+II` the front `Rate` knob *scales* the locked rate ±1 octave (never a dead knob —
  law 5); readout shows the resulting Hz.
* **Discriminator:** antiphase pair — stereo correlation dips to ~0 at sweep extremes while the mono
  sum's spectral ripple stays < 3 dB (§3.8). No other Type has exactly-opposite single combs.

### 2.2 `Pedal` — CE-1/CE-2 one-line chorus
* **Recipe:** ONE tap. Sine-ish LFO (LP'd triangle), base 5 ms, sweep ±3.5 ms at Depth 100. Input
  pre-emphasis +6 dB@3 kHz into the BBD poly, de-emphasis after (the CE-2 hiss-ducking pair). Output:
  L = dry + wet, R = dry − wet (wet polarity-flipped on the right = the pedal-into-two-amps stereo);
  at Width 0 both sides identical dry+wet (true CE-2 mono).
* **Characters:** `Chorus` (fixed preset sweep 0.4 Hz — the CE-1 one-knob mode; Rate scales it),
  `Vibrato` (wet-only regardless of Mix < 100 — the CE-1 second mode; Mix then blends vibrato level),
  `Grit` (input poly drive ×4 — the dirty-preamp CE-1 legend), `Slow Amp`, `Fast Amp` (JC-120
  rotary-ish fixed rates 0.8/6.5 Hz), `Dry L / Wet R` (the literal CE-1 output jacks — hard split),
  `Warm`, `Thin` (recon LP 4 kHz / HP'd wet 300 Hz).
* **Discriminator:** single moving comb — the only Type whose wet-solo'd spectrum shows ONE notch
  series sweeping; strongest audible pitch wobble per unit Depth (PK_AM-style probe reads highest
  periodic pitch deviation).

### 2.3 `Trio` — Dytronics/Songbird studio tri-chorus
* **Recipe:** 3 taps panned L / C / R (constant-power), three phases from ONE master accumulator at
  0°/120°/240° (the one-clock law — §3.3). Sine. Base 8 ms, sweep ±4 ms. Slow master rate + a fast
  secondary LFO mixed in at `Flutter` amount (the Dytronics preset-mode pair). Center tap mixed to
  both sides at −3 dB = the mono anchor.
* **Characters:** `Preset` (0.35 Hz slow + 4.5 Hz fast at 12 %), `Manual` (pure Rate knob 0.03–7.45 Hz
  mapping — the UAD range verbatim), `Enhance` (per-tap feedback 25 % — "Choral Enhance"),
  `Sides` (center tap −12 dB), `Center` (sides −6 dB — chorus focused in the middle, the CS-5 party
  trick), `Syrup` (depth ×1.5, recon 5 kHz), `86 Rack` (compander ×1.5 + noise), `Glassy` (recon
  16 kHz, depth ×0.7).
* **Discriminator:** 3-phase rotation with *panned* taps: instantaneous L−R detune difference rotates
  (a cents-vs-time Lissajous the visualizer literally draws), and the center tap keeps mono-sum ripple
  < 2 dB — measurably flatter than `June` at equal Depth.

### 2.4 `Ensemble` — Solina/RS-202 string-machine 3-phase
* **Recipe:** 3 taps SUMMED to both channels (L gets 0°/120°/240°, R gets the same taps re-weighted
  30° rotated — the stereo RS-202 output matrix), each tap's delay driven by
  `slow(0.66 Hz, ±1.8 ms) + fast(6.25 Hz, ±0.25 ms)` — BOTH always on, `Flutter` scales the fast
  bank, Depth the slow. Triangle slow, sine fast. Base 6 ms. Wet-dominant: at Mix 50 the wet sits
  +3 dB over the taps' equal-power sum (the string-machine "always ensembled" voicing).
* **Characters:** `Solina` (numbers above), `RS-202` (6.25/0.66 Hz pairs, deeper fast),
  `VP Choir` (adds 4th tap + UberMod-style per-channel independent 3-phase = DualEnsemble),
  `Random` (randomized-triangle slow LFOs — the 6TapRandom pattern-killer), `Dark Wine` (recon
  3.5 kHz + noise), `Brass` (fast bank ×2 = 8 Hz shimmer), `Slow Tide` (slow bank halved 0.33 Hz),
  `Phase Wide` (R rotation 90° instead of 30°).
* **Discriminator:** the ONLY Type with two simultaneous modulation rates — the wet sideband spectrum
  shows energy at both f±0.66 Hz-scale and f±6.25 Hz-scale spacings (dual-rate flux, trivially
  measurable); kill `Flutter` and it collapses toward `Trio`, so `Flutter` > 0 is the type default.

### 2.5 `Micro` — H3000 #231 layered micro-shift (chorus without wobble)
* **Recipe:** NO LFO. Per side, a dual-head pitch reader on the shared delay buffer: head delay ramps
  down (up) at constant slope `r = 2^(±c/1200) − 1` (c = cents), heads 180° apart on a raised-cosine
  crossfade of period T = 40 ms (excursion r·T ≈ 0.24 ms at 10 cents — tiny, glitch-free). LEFT
  shifts +c, RIGHT −c (`Detune` knob = c, 0–50 cents). Static stagger: L +8 ms, R +12 ms of base
  `Time` (the "dash of delay" from the Eventide recipe). `Low Keep` defaults engaged at 250 Hz for
  this Type (MicroShift's Focus lesson: keep the low band dry or the bottom goes loose).
* **Characters:** `H3K I` (matched #231: tight delays, gentle saturation), `H3K II` (#519 voicing:
  wider delay variation, darker response), `AMS` (DMX 15-80: delay wander ×3 via slow SmoothRandom +
  harder crossfade = audible de-glitch character), `Dual Mono` (both sides SAME sign +c — thickener,
  not widener), `Octaverse` (adds −12 dB copies at ±2c — 4-layer stack), `Tape Head`, `Choir`,
  `Subtle` (c ×0.4, delays halved).
* **Discriminator:** ZERO periodic modulation — spectral-flux flat vs time (the honest probe for
  "no free-running wobble"), but a constant measured frequency offset: a 440 Hz sine leaves L at
  442.3 Hz / R at 437.8 Hz with Detune = 9 cents. No other Type shifts steady-state frequency.
* ⚠️ Feedback around a pitch shifter = an upward/downward **spiral** (each pass adds +c). That is a
  *feature* (the classic H3000 shimmer-spiral) but the loop gain law applies doubly: cap fb at 0.7
  for this Type and keep the in-loop LP mandatory (§3.6).

### 2.6 `Wow` — tape/vinyl aperiodic chorus
* **Recipe:** 2 taps (antiphase weighting like June for width), but the modulator is
  `SmoothRandom(slow) + triple-LFO wow stack` (recycled verbatim from `TapeMachines.h`: 0.6 Hz
  ±2.0 ms, 2.2 Hz ±0.8 ms, 7 Hz ±0.4 ms), `Drift` scales the random component, Depth the deterministic
  stack. Base 7 ms. Asymmetric glide on the random target (`Lag` behavior, Warped Vinyl's knob):
  rises at τ 40 ms, falls at τ 400 ms — pitch sags fast, recovers slow, like a warped record passing
  the stylus.
* **Characters:** `Cassette` (stack above), `Vinyl 33` (adds a 0.55 Hz deterministic dip locked to a
  "revolution" — periodic warp), `Vinyl 45` (0.75 Hz), `Dictaphone` (drift ×3, recon 3 kHz, noise
  −50 dB), `Pro Reel` (drift ×0.3, flutter-dominant), `Dying Walkman` (drift ×4 + compander ×2 pump),
  `Underwater` (recon 2 kHz + depth ×1.5), `Breeze` (drift only, no stack — pure random shimmer).
* **Discriminator:** aperiodic d(t) — autocorrelation of the delay trace shows no stable peak
  (vs. every LFO Type); pitch-deviation histogram is skewed (the asymmetric glide), unlike any
  symmetric LFO.
* ⚠️ **The Worn-walk law (Phase G):** a per-sample noise *smoother* is not a *walk* — the random
  target must be a band-limited SmoothRandom (hold + slew), not white noise through a one-pole, or
  the drama measures near-zero exactly as the Worn character did before fb345.

### 2.7 `Dark` — the long-line dark BBD
* **Recipe:** 1 tap/side, staggered (L base, R base ×1.31), triangle LFO antiphase, base up to
  **40 ms** (`Time` at max — doubling/slapback edge), and the FULL color chain leaning in:
  clock-darkening recon LP that **tracks instantaneous delay** (recycle the `DelayEngine.h:120-126`
  law: 5.2 kHz → 2.3 kHz as delay grows — apply continuously per-sample from d(t)), compander at ×1.5,
  BBD poly always on, noise −60 dB env-gated. This is the "$99 pedal with the 4096-stage chip" —
  chorus, doubler and murk in one.
* **Characters:** `Standard`, `Double` (base 28 ms, depth ×0.4 — the ADT setting), `Murk` (recon
  floor 1.8 kHz), `Pumped` (compander ×2.5 — audible breathing at −26 dBFS, §3.4 calibration),
  `Hissy` (noise −48 dB), `Cheap` (adds ±0.4 ms zipper-jitter on the clock = the £15-pedal warble),
  `Slap Wide` (R stagger ×1.6), `Feedback Box` (fb default 45 %).
* **Discriminator:** wet HF cutoff measurably *moves with the LFO* (spectral centroid of the wet
  oscillates at the LFO rate — the only Type whose brightness breathes), plus transient compander
  pump (attack overshoot then 2:1 settle) visible on a click train.

**Cut candidates and where their sound went:** `Dimension` → Hyper device (§0). A dedicated
`Detune/Supersaw` type → Hyper. `Leslie` → out of scope. A 32-tap UberMod-style smear → the Diffuse
delay type already ships it (`DelayEngine` allpass bank).

---

## 3. DSP core — algorithms, math, laws, stability, oversampling verdict

### 3.1 Engine shape — the house FX-engine contract (copy or it won't drop into the rack)

`ChorusEngine.h`, pure C++ (no JUCE), mirroring `DelayEngine.h`/`DistortionEngine.h` exactly:
`prepare(double)`, clamped `setType/setCharacter/setX(...)` per-block setters,
`processSample(float inL, float inR, float& outL, float& outR)` **returning WET only** (the processor
owns Mix — Mix 100 % = fully wet, law 4), per-sample target→current glide idiom
(`DelayEngine.h:162-171` shape), `softClip` final bound (`DelayEngine.h:315`: linear below ±1.4 then
tanh — reuse verbatim as the BIBO net), `flush()` that snaps smoothers to targets (silent, not a ramp
from zero — `DistortionEngine.h:227`).

### 3.2 The shared core

* **One stereo circular buffer**, power-of-two, ≥ ceil(fs · 64 ms) (40 ms max base + 8 ms sweep +
  12 ms stagger + drift margin + interpolator guard). At 48 kHz: 4096 samples/channel. Write once,
  read up to 4 taps (Ensemble `VP Choir`) — taps are READS, so voices are nearly free (§8).
* **Fractional read: 4-point Hermite** — recycle `TerrainChorus.h:129-143 hermite4()` verbatim (it is
  the house-proven kernel, same family as `DelayLine::readCubic` in TapeMachines). Linear
  interpolation under modulation ripples HF (the JOS delay-interpolation result); Hermite at chorus
  depths is transparent and costs 4 MACs. **No allpass interpolation** — allpass state smears under
  fast modulation (Dattorro's own caveat) and its phase ripple is the Serum-HQ dispersion we
  deliberately rejected in the delay device.
* **Minimum-delay guard:** `d ≥ 2.0 samples` always (Hermite reads idx−1…idx+2 — below 2 the read
  window crosses the write head = garbage). Clamp AFTER modulation, and taper Depth so
  `base − excursion ≥ 2` by construction: `excursion = min(depth_ms, base_ms − 0.05)`.

### 3.3 The modulator bank — and the one-clock law

**ONE master phase accumulator** per engine; every periodic tap phase is
`ph_i = master + offset_i` (offsets: 0/π for 2-tap, 0/2π/3/4π/3 for 3-tap, +30°/90° channel
rotations). ⚠️ **Phase-G one-clock law:** separate per-tap accumulators integrate glide skew during
Rate glides and the taps drift out of their designed phase relationship permanently — the DIGITAL
family shipped that bug; do not repeat it. The random modulators (Wow `Drift`, Ensemble `Random`,
Micro `AMS`) are per-tap `SmoothRandom` instances (they are *supposed* to decorrelate).

Waveforms: `tri(ph)` (BBD-authentic — constant |slope| ⇒ constant pitch offset per half-cycle, the
"two detuned voices" illusion), `sin(ph)`, `lpTri` (one-pole at 4×rate on the triangle — the Juno
I+II measured shape). Per-Type from the Character table.

**Rate law:** 0.02–20 Hz, exponential taper (`hz = 0.02 · 1000^t` ⇒ mid-knob ≈ 0.63 Hz — the whole
classic register lives in the middle half, 5–20 Hz vibrato territory in the top fifth). Synced (pill):
4 bars → 1/256 per the house synced-range law, same resolver as `SYN_DLY_SYNCDIV` (host resolves to
Hz, engine never sees the transport).

**Depth law:** `Depth` is *milliseconds of excursion*, per-Type window (Character table), displayed in
ms AND cents-peak (`cents ≈ 1200·log2(1 + 2π·f_lfo·A/1000)` for sine at rate f_lfo, excursion A ms —
the readout that makes Depth honest). 0 → 100 continuous, no plateau: at 0 the taps sit at staggered
static delays (still a thickener — never a dead knob), at 100 the excursion hits the type window ×1.6
(past the hardware: Juno max ×1.6 ≈ ±3 ms ⇒ ~±40 cents at 1 Hz — seasick on purpose, law "max = just
past useful").

### 3.4 The BBD color chain — calibrated to the −26 dBFS bus (law 1) ⚠️ the measured trap

Order per tap-sum, per channel: `pre-emph (Pedal only) → poly grit → [delay reads] → recon LP →
de-emph → compander expand → noise inject`, compander *compress* at the input write.

* **⚠️ THE COMPANDER CALIBRATION TRAP — measured on the in-tree chorus.** `TerrainChorus.h:167-180`
  "compands" with `tanh(x·1.5)·0.7` in and `sinh(x·1.5)/1.5·1.4` out. At the real bus level
  (−26 dBFS = 0.05 linear): `tanh(0.075) = 0.0749` — the curve is in its LINEAR region; compress→
  expand is a pure ×0.98 gain. **The "NE570 character" is a costume that does nothing at our program
  level** (same failure class as fb283's inaudible "102 % divergence"). The real NE570 is not a static
  curve at all — it is a *level detector driving gain*. Correct model (Raffel/Smith §3.4):
  ```
  env   = onePole(|x|, τ = 5 ms)                    // rectifier average
  compress: y = x / max(sqrt(env/REF), floor)       // 2:1 toward REF
  expand:   y = x · sqrt(env_w/REF)                 // env_w tracked on the wet
  REF = 0.05  (the −26 dBFS program anchor — THE law-1 number)
  floor = 0.25 (max +12 dB comp gain — bounds noise breathing)
  ```
  With REF at 0.05 the compander idles at unity on program, *pumps* on transients (attack overshoot,
  ~2:1 settle over 5–10 ms) and *breathes* on decays — audible, honest, and exactly what the
  `Pumped`/`Aged` characters scale. τ per Character (2.2–10 ms — the C_rect range).
* **Grit:** the Raffel poly `x − x²/8 − x³/18 + 1/8`, drive-scaled: `g(x) = poly(x·k)/k`,
  k = 1 + Color·7 (Color > 60 pushes program into the bend: −26 dBFS × k=8 ⇒ 0.4 — real H2). The +1/8
  term is a DC offset by design ⇒ **the house DC blocker (10–20 Hz, the `TapeMachines.h` DCBlocker
  spec) is mandatory after the chain** (distortion §4.1 lesson).
* **Recon LP:** one-pole (recycle `onePole(hz)` `DelayEngine.h:324`), cutoff from the clock law:
  `hz = clamp(0.45 · (N_eq / (2·d_sec)) , 1800, 16000)` with N_eq = 1024 — i.e. cutoff tracks the
  *instantaneous* delay, so deep sweeps audibly darken at the bottom of the cycle (`Dark`'s
  discriminator; scaled by `Color` toward bright = modeling off).
* **Noise:** −60 dB (Character-scaled to −48) white through the recon LP, **multiplied by the input
  envelope** (nothing free-runs, law 6): `n·env_in/(env_in+0.003)` — silence in = silence out, the
  Phase-G silence-class discipline (env-tracked, squared release).

### 3.5 Micro-shift math (the only nontrivial block)

Dual-head reader per side on the shared buffer. Head phase `q ∈ [0,1)` advances at `q += r/T·dt`
(r = shift ratio − 1, T = 40 ms window): head A delay `dA = base + r·T·q`, head B same with
`q+0.5 mod 1`; gains `gA = 0.5−0.5·cos(2πq)`, `gB = 1−gA` (raised-cosine — constant-power enough at
these depths, and the crossfade points always land where both heads are within r·T/2 ≈ 12 samples of
each other so comb error is < 0.3 dB). Cents→ratio: `r = 2^(c/1200) − 1`. Down-shift = negative r
(delay grows — never reads past the write head; up-shift consumes delay ⇒ base must exceed
`r·T + 2 samples`, guaranteed by base ≥ 8 ms > 0.24 ms·margin). CPU: 2 heads × 2 sides × Hermite = 4
extra reads, still trivial.

### 3.6 Feedback — the loop gain law (law 6)

`fb` returns the post-color wet (pre-width) into the write sum: `write = in·comp + fb·wet_color`.
Gain stages INSIDE the loop, counted: Hermite (≤ 1.0, ripple +0.3 dB worst), recon LP (≤ 1), poly
grit (≤ 1 small-signal at k=1; ≤ 1.06 at full Color — measured slope at 0), **compander expander
(UNBOUNDED if env sags — the trap)**, de-emph (+6 dB max at Pedal). Therefore: **the expander sits
OUTSIDE the loop tap point** (feedback taps pre-expander), and max loop gain =
1.0 (interp) × 1.06 (grit) × 1.0 (LP) ⇒ `fb ≤ 0.90 / 1.06 / deemph_gain` → **knob max 0.85 linear**,
displayed 0–100 %. In-loop the recon LP guarantees decaying HF (BBD echo behavior); `softClip` bounds
any transient overshoot (BIBO). Micro Type: fb ≤ 0.7 (§2.5 spiral). Feedback polarity per Character
(`Enhance` uses +; a − option makes hollow combs — the CE-2-mod trick) — sign lives in the table, not
a knob.

### 3.7 Oversampling verdict: **NONE, ever**

The only nonlinearity is the gentle BBD poly (H2 ≈ −40 dB at program+grit); its aliases sit below
−80 dBFS at our bus level — under every perceptual gate in the distortion bible's §3.10 table. The
modulated-delay resampling itself is anti-aliased by the Hermite kernel at these mod rates
(max head speed = r or 2π·f·A/1000 ≤ 0.06 samples/sample — nowhere near pitch-shifter territory
except Micro, whose r ≤ 0.03 is equally safe). Fixed cost, no Quality dropdown needed; the CPU story
is §8. **Never oversample this device** — it would be pure waste (law 8).

### 3.8 Mono-compatibility analysis — MANDATORY numbers

Per-channel output `L = dry + w_L`, `R = dry + w_R` (Width via M/S on the WET only — §4).
Mono sum `M = 2·dry + w_L + w_R`.

* **June/antiphase:** w_L, w_R are combs with opposite instantaneous detune. Their sum's ripple:
  `|1 + ½e^{−jωd_L} + ½e^{−jωd_R}|` with d_L+d_R ≈ const ⇒ notches of the two combs interleave;
  measured on the Juno topology the mono ripple stays **< 3 dB** where either side alone shows
  > 12 dB notches. This is WHY the antiphase trick survived 40 years. Harness gate: mono-sum
  magnitude-spectrum deviation from dry < 3 dB at default Depth (pink-noise probe, 1/6-oct smoothing).
* **Trio/Ensemble:** center/summed taps put identical energy in both channels ⇒ correlation ≥ +0.3
  at default; the 120° rotation means no static notch parks anywhere. Gate: same < 3 dB rule.
* **Micro:** L/R combs *beat* slowly (notch positions drift at r·f) — mono sum has moving shallow
  combs, the known micro-shift trade; `Low Keep` ≥ 250 Hz (Type default) keeps the fundamental range
  ripple-free. Gate: below the crossover, mono deviation < 0.5 dB.
* **The two hard rules that make all of this true:** (1) **Width (M/S) touches WET ONLY** — dry is
  never side-boosted (dry M/S widening is the classic mono-collapse bug); (2) **wet polarity flips
  (Pedal R = −wet) are Character choices, never defaults**, and the harness runs the mono gate on
  every Character row.

### 3.9 Param glide table (law 7 — no clicks)

| Param | Law |
|---|---|
| Time (base delay) | **glide the delay length** at 15 ms one-pole (comb-click law — never snap a delay read) |
| Rate | glide Hz at 30 ms; phase continuous across changes (accumulator never resets) |
| Depth, Detune, Flutter, Drift, Feedback, Color, Width, Low Keep | 15 ms one-pole per-sample ramps |
| Mix | processor-owned equal-power, 15 ms (the delay/reverb idiom) |
| Type/Character switch | **fade-swap-recover** (the fb345 deferred-fade law): 8 ms wet dip → swap tables + `flush()` state that must not carry (compander env re-seeds from current env, NOT zero) → 30 ms recover. Never audible as a click; never a stuck half-state |
| Sync pill | resolve to Hz, then the Rate glide takes it — no phase jump |

---

## 4. Chassis map — the fb275 device, 11 params (2 dropdowns + 8 back knobs), front 3 + Mix

Param IDs: `SYN_CHR_*`, grammar cloned from `SYN_DLY_*` (`ParameterIDs.hpp:374-394`), including
`SRC_A/B/C/D/SUB/NOISE` pills + `POWER` (default **OFF**, dry init — house FX default) — and the
4-point WebSliderRelay chain for every one (the whole block is greenfield; the silent-no-op trap
applies in full).

### Front card — 3 hero + Mix (+2 pills)

| Knob | ID | Range / taper | Default | What it does (pragmatic name test) |
|---|---|---|---|---|
| **Rate** | `SYN_CHR_RATE` | 0.02–20 Hz, exp taper (§3.3); synced: 4 bars→1/256 | 0.5 Hz | How fast the voices swim |
| **Depth** | `SYN_CHR_DEPTH` | 0–100 → type ms-window ×1.6 (§3.3) | 45 | How far they swim (ms + cents readout) |
| **Width** | `SYN_CHR_WIDTH` | 0–100 → wet M/S 0…1.6 side gain | 70 | Stereo size of the wet |
| **Mix** | `SYN_CHR_MIX` | equal-power, 100 % = fully wet | 35 | Dry↔wet |

Pills: `Power` (house) + **`Sync`** (`SYN_CHR_SYNC`, bool — Rate becomes the synced divisions).
(One pill only besides Power — matches the Mod/Freeze pill economy; vibrato lives in Pedal `Vibrato`
Character, not a pill, because Mix 100 already = vibrato on every Type.)

### Back panel — 2 dropdowns + 8 knobs (4×2)

Dropdown 1 **Type** (7): `June · Pedal · Trio · Ensemble · Micro · Wow · Dark`
Dropdown 2 **Character** (8 per Type — the tables in §2; the `CharBias` constant-row pattern).
⚠️ fb342 law ①: the `AudioParameterChoice` cardinality MUST equal the UI list per Type — build the
Character list from one shared table so they cannot diverge.

| Slot | Knob | ID | Range / taper | Default | Role |
|---|---|---|---|---|---|
| P1 | **Time** | `SYN_CHR_TIME` | 0.5–40 ms, log | type base | Base voice delay (comb position; doubler at top) |
| P2 | **Detune** | `SYN_CHR_DETUNE` | 0–50 cents, x^1.4 | 9 (Micro) / 0 | Constant pitch split L+/R− (Micro's engine; adds static micro-shift to ANY type — never dead) |
| P3 | **Feedback** | `SYN_CHR_FB` | 0–100 % → 0–0.85 loop (§3.6) | 0 | Regeneration — comb bloom → near-flange wash |
| P4 | **Flutter** | `SYN_CHR_FLUTTER` | 0–100, fast-bank ms ±0.6 max | type | Fast vibrato bank (5–8 Hz) on top of Rate |
| P5 | **Drift** | `SYN_CHR_DRIFT` | 0–100, SmoothRandom ±2.5 ms max | 0 (Wow: 55) | Aperiodic wander — tape soul on any Type |
| P6 | **Color** | `SYN_CHR_COLOR` | 0–100 bipolar-ish: 50 = modeled BBD, 0 = murk (recon 1.8 k, grit ×8), 100 = studio-clean (16 k, poly off) | 50 | The whole §3.4 chain on one knob |
| P7 | **Low Keep** | `SYN_CHR_LOWKEEP` | 20 Hz–1 kHz, log (20 = off) | 20 (Micro: 250) | Crossover below which the signal stays DRY and CENTERED (MicroShift Focus + Dimension bass law) |
| P8 | **Phase** | `SYN_CHR_PHASE` | 0–180° | type (June 180) | L/R modulator phase — 0 = mono-thick, 180 = antiphase wide (the JUN-6 manual-mode width axis; distinct from M/S Width) |

Every knob passes the 0→100 evolution test: Time sweeps comb→doubler, Detune 0→50 is thickener→
out-of-tune 12-string, Flutter 0→100 is calm→nervous, Phase 0→180 audibly rotates width even in mono
(comb interleave), Color is a three-regime journey (murk→model→clean). No plateaus, no jargon names.

**Total: 2 dropdowns + 8 back knobs = the 11-param chassis; front Rate/Depth/Width/Mix + Sync/Power
pills + routing pills complete the APVTS block (~22 ids incl. SRC pills) — one-to-one with the
shipped delay device's shape.**

---

## 5. Visualizers

### 5.1 How the greats show chorus (survey — precise mechanisms)

* **Serum 1/2:** *nothing animated for chorus* — knobs + a static module header; the FX rack's
  strength is layout, not motion. (Serum 2's redesigned FX view adds per-module graphic strips, but
  the chorus strip does not render live modulation.) → the gap we exploit.
* **Arturia JUN-6 / Dimension-D:** skeuomorphic hardware faces — LED mode buttons, a VU-style output
  meter (Dimension-D), LFO shape selector buttons in an advanced drawer. Motion is implied, never drawn.
* **TAL-Chorus-LX:** a Juno panel photo with two buttons. Nothing live.
* **Soundtoys MicroShift:** hardware-brushed panel, zero metering. Style buttons with lamps.
* **Eventide TriceraChorus (H9/plugin):** ribbon of per-voice depth sliders; the pedal has status
  LEDs that pulse with the LFO rate — the only lineage reference that shows *rate* physically.
* **Valhalla UberMod:** flat sliders, color-coded sections; no scope.
* Take-away: **no shipping chorus draws its taps moving.** Every one of our competitors makes the user
  *hear* the topology blind. Drawing d(t) honestly is an instant, ownable differentiator — and our
  Phase-G "family tell" (§2, the d(t) trace) is literally a visualizer spec.

### 5.2 OUR card — 3 concepts (canvas, CPU-cheap, boldly audio-reactive, param-reflecting)

**A. `Voice Orbits` (recommended core).** Each active tap is a comet on the card: **x = its current
stereo position** (pan law per Type: June L/R, Trio L/C/R, Ensemble cluster), **y = its instantaneous
pitch deviation in cents** (computed exactly from the engine's d'(t) — the DSP telemetry, not a fake
LFO), trail = last ~1 s of path fading out. Depth literally stretches the orbits taller, Rate spins
them faster, Phase visibly rotates L vs R opposition, Detune parks the Micro comets at fixed ±y
offsets (a *still* picture that says "no wobble"), Drift makes trails wander, Flutter adds jitter
fuzz to the trail. **Audio-reactivity (law 9):** comet size + trail brightness = the per-tap wet
envelope (idle = dim ember dots; playing = bright comets with tails; the delta is unmissable);
Feedback draws each comet's echo ghosts at decaying alpha. Implementation: ≤ 6 comets × ≤ 32 trail
points, one `requestAnimationFrame` at the 60 Hz push-lane cadence, plain `arc()` + `globalAlpha`
strokes — **no shadowBlur, no per-frame filters** (fb342 session law ⑤), engine→UI telemetry rides
the existing fb90-at-birth push lanes (d(t) per tap + env per tap + master phase ≈ 14 floats/frame).
* **B. `Twin Comb Curtain` (under-layer or alt).** The analytic wet+dry magnitude
  `|1 − m + m·e^{−jωd(t)}|` per channel (m = mix, d = live delay) drawn as two translucent curtains
  (L up-facing, R down-facing mirror) that visibly *breathe* as the taps sweep — comb teeth widening/
  narrowing IS the effect explained in one picture. 64 log-spaced bins, closed-form (no FFT), tinted
  by the live input band energies (reuse the filter-analyzer's band probe) so it flares with the music.
  Low Keep shades the protected low region solid — a param made visible.
* **C. `Warp Ribbon` (Wow-flavored full-card alt).** A horizontal tape ribbon whose vertical
  displacement is the recorded d(t) history scrolling left (the echo-timeline grammar from fb312's
  delay viz — recycle that renderer), ribbon thickness = Depth, tear-speckle = Drift, sheen = wet RMS.
  Gorgeous for Wow/Dark, less informative for Trio — hence B/A preferred as the universal core.

Recommendation: **A over B composited** (orbits in front, faint curtain behind at 20 % alpha; curtain
alone when POWER off = dim static combs — the idle state reads "asleep", satisfying idle-dim/playing-
bright). The card claims the "everything audible interacts visually" rule: every one of the 11 params
moves a named pixel.

---

## 6. Interplay — the device in the chain

* **Unity-through discipline.** At POWER on + defaults (Mix 35, Feedback 0, Color 50): wet path gain
  is trimmed so wet RMS = dry RMS on pink noise at −26 dBFS (the compander idles at unity by the
  REF = 0.05 calibration; equal-power Mix then keeps output within ±0.5 dB of input). **No WET_GAIN
  2.5-style boost anywhere** — the legacy TerrainChorus's +8 dB wet hack existed because its amount²
  mixing buried the wet (same disease as distortion §2 root-cause 3); our Mix is processor-owned and
  linear in perception, so the hack must NOT be copied. Harness gate: bypass vs default null within
  ±0.5 dB RMS, no > 2 dB spectral shelf.
* **Spectrum/dynamics downstream:** adds ±cents sidebands (density ∝ Rate·Depth), combs the sustain
  spectrum, raises stereo side energy (watch the fb264 output limiter — Width 100 + Depth 100 on a
  full chord raises side peaks ~+4 dB; the limiter bounds it, nothing to fix), compander (Color < 50)
  softens transient crests ~2 dB. Feedback > 60 raises RMS density into whatever follows — reverbs
  bloom, distortions get *thicker* not louder (they're already saturated).
* **Classic ordering wisdom (preset-able, user-free via SYN_FX_ORDER):** distortion → **chorus** →
  delay → reverb is the canon (chorus post-dirt = wide smooth wall; chorus PRE-dirt = the saturator
  partially erases the mix — the fb-era "saturator-after-mix erases mix" law, worth a tooltip);
  chorus → delay makes repeats swim (BBD-echo vibes); delay → chorus wobbles each repeat coherently
  (rack-mount 80s). Chorus → reverb widens the tail feed; reverb → chorus (rare) smears tails —
  legitimate, druggy.
* **Stacking:** two modulated-delay devices in series beat at |f1−f2| (audible LFO moiré) — fine, but
  the presets avoid same-rate stacks. Chorus + the delay's own Mod knob duplicates function — presets
  keep delay Mod at 0 when the chorus carries the motion.
* **⚠️ THE FOURTH-DEVICE INTEGRATION LANDMINES (from fb305/fb338/fb341 — the known field):**
  1. A fourth send bus **re-breaks the main-send exclusion sums** unless the chorus send joins EVERY
     one: `PluginProcessor.cpp:7159`, `:7161`, `:7326`, `:7328`, `:7358`, `:7360` currently sum
     `rvbSend + dlySend + dstSend` — each gains a `+ chrSend` term (the fb338 law: *every send bus
     joins every main-send exclusion*), and the new device's own exclusion line must sum the other
     three.
  2. `SYN_FX_ORDER` is a 6-choice permutation of 3 devices (`jlimit(0,5)` at
     `PluginProcessor.cpp:5860`, switch at `:7383`). Four devices ⇒ **24 permutations** — either
     extend the choice list to 24 (state-compatible: old indices 0–5 map to the chorus-last block) or
     move to a per-device order index. Decide BEFORE params ship; APVTS choice cardinality is
     state-breaking to change later.
  3. `IndyFxChain.h` owns private copies of the legacy chain — the NEW device does not live there
     (the rack chain is separate), but confirm the rack path the reverb/delay/dst inserts use and add
     the chorus insert-lambda to the same `SYN_FX_ORDER` permutation switch.
  4. Latency: zero (no lookahead anywhere) — no §4.4-style send-math trap. State it in code comments.

---

## 7. Factory presets (13 sketches — name · intent · rough values)

Defaults unless noted: Mix 35, Width 70, Feedback 0, Drift 0, Low Keep 20, Phase = type.

| # | Name | Type/Char | The point | Key values |
|---|---|---|---|---|
| 1 | **Init June** | June/I | The device default = the Juno at noon | Rate ×1 (0.51 Hz), Depth 45 |
| 2 | **Polysynth II** | June/II | Faster Juno swirl for keys | Depth 60, Mix 40 |
| 3 | **Tears In Rain** | June/I+II | The 9.75 Hz vibrato-chorus | Mix 55, Width 85 |
| 4 | **First Pedal** | Pedal/Chorus | CE-1 into two amps | Time 5 ms, Depth 55, Width 100 |
| 5 | **Dirty Preamp** | Pedal/Grit | The clipping CE-1 legend | Color 22, Depth 50, Mix 45 |
| 6 | **Session 1984** | Trio/Preset | Dyno tri-chorus guitar wall | Depth 50, Flutter 25, Width 90 |
| 7 | **Syrup Rack** | Trio/Syrup | Thick UAD-style center-anchored | Depth 70, Feedback 20, Color 40 |
| 8 | **String Machine** | Ensemble/Solina | The Solina, verbatim numbers | Depth 65, Flutter 45, Mix 60 |
| 9 | **Choir Box** | Ensemble/VP Choir | 4-tap vocal pad ensemble | Depth 55, Flutter 30, Low Keep 150 |
| 10 | **Wide Vox** | Micro/H3K I | The #231 vocal widener | Detune 9, Time 8 ms, Low Keep 250, Mix 50 |
| 11 | **Double Tracker** | Micro/AMS | ADT thickener, mono-safe | Detune 14, Drift 20, Width 55, Mix 45 |
| 12 | **Warped Record** | Wow/Vinyl 33 | Seasick lo-fi pads | Depth 60, Drift 70, Color 30, Mix 50 |
| 13 | **Basement Tape** | Dark/Pumped | Long dark BBD murk + pump | Time 28 ms, Depth 40, Color 15, Feedback 35 |

Level discipline (the fb343 preset-spread lesson): every preset harness-checked to land within
±1.5 dB RMS of bypass on the reference pad — no Gargle-style +28 dB outliers, no Sludge-quiet ones.

---

## 8. CPU — budget and tiering

Per-sample worst case (Ensemble `VP Choir`, 4 taps stereo-summed): 8 Hermite reads (4 MAC each) +
1 master phase + 3 SmoothRandom ticks + 2 companders (2 one-poles + sqrt) + 4 one-pole filters +
poly grit ×2 + noise ×2 ≈ **~90 flops/sample stereo ≈ same order as one DelayEngine instance** —
which measured well under 1 % of a core after the fb344 −35 % pass. Typical Types (June/Pedal/Micro)
run 2–4 reads: cheaper than the delay device.

* **No oversampling ever** (§3.7) — the whole budget is flat.
* **Type-gated work:** tap count, random generators and the fast bank exist per Type row — `June`
  never pays for `Ensemble`'s third tap (the one-engine-active house pattern).
* **Sleep:** the awake-head/control-sleep law (fb342 ⑥): when POWER off → full bypass, zero work;
  when input env < −80 dBFS for 250 ms AND feedback tail decayed → skip color chain + reads, keep
  the master phase + envelope follower ticking (the "control head") so wake-up is phase-continuous
  and click-free.
* **`sqrt` in the compander:** replace with one Newton step seeded from the previous sample's result
  (env is 5 ms-smoothed — the seed is always close); or table it. Budget line, not a redesign.
* UI telemetry: 14 floats/frame at 60 Hz on the existing push lanes — noise-level.

---

## 9. Pitfalls — the traps, collected

1. **⚠️ The legacy-chorus recycle traps (measured, §3.4/§6):** (a) the `WET_GAIN = 2.5` hack — do not
   copy; it papers over amount² mixing we don't have. (b) the tanh/sinh "compander" is inert at
   −26 dBFS — replace with the env-driven 2:1 anchored at REF 0.05. (c) `setParams` hard-sets LFO
   rates and recon cutoffs per call — no glide; our engine glides everything. (d) its buffer guard is
   `jassert` only — release builds would read garbage if the window law is violated; clamp for real.
2. **Zipper on Time/Depth:** delay length must GLIDE (comb-click law) and the excursion clamp (§3.2)
   must apply post-glide, or a fast Depth ride pushes reads under the 2-sample floor → crackle.
3. **One-clock law:** one master accumulator + offsets (§3.3). Per-tap accumulators + Rate glide =
   permanent phase skew (the shipped-and-fixed DIGITAL bug class).
4. **Compander in the feedback loop:** the expander's gain > 1 on decaying env — tap feedback
   pre-expander or the loop exceeds unity exactly when the note dies (a self-oscillating "breather").
   Counted in §3.6; do not re-derive it wrong.
5. **DC:** the BBD poly's +1/8 term and any asymmetric Character = DC into the feedback loop → the
   blocker (10–20 Hz two-pole per distortion §4.1 spec) after the color chain, inside the device.
6. **Denormals:** recon LPs, compander envs, SmoothRandom states on silent tails — `flush()` them,
   `ScopedNoDenormals` in the processor block (house standard), and the sleep gate (§8) makes it moot.
7. **Mono collapse:** M/S width on dry (never — wet only), default polarity flips (never — Character
   only), Phase 180 + Width 100 + Mix 100 is the worst legal case — the harness's mono gate (§3.8)
   runs on every Character row, every preset.
8. **Free-running noise:** BBD hiss must ride the input envelope (§3.4) — a `Hissy` character that
   idles audibly is the fb325 violation class (5 factory presets shipped DEAD-silent last device from
   the inverse bug; both directions are the same law: sound ⇔ input).
9. **Micro head-crossing:** up-shift heads consume delay — base < r·T + margin reads the write head
   (metallic garbage). The §3.5 constraint is structural (base ≥ 8 ms), keep it in the clamp, not in
   a comment.
10. **Character/Type switch state carry:** compander env and drift targets survive the fade-swap
    re-seeded from *current* values (fb345 re-seat law — a zeroed env after swap = a pop as it
    re-converges; the Xfmr pk-50.4 lesson).
11. **Sync pill math:** 1/256 at 120 BPM = 7.8 ms period = 128 Hz "LFO" — beyond the 20 Hz free max
    by design (the synced law's top is deliberately absurd = ring-mod territory; it is the law, ship
    it, and let the taper spend most travel between 4 bars and 1/16).
12. **Choice cardinality (fb342 ①):** 7 Types × 8 Characters — the APVTS choice list, the UI list and
    the DSP table must be generated from ONE source of truth or a P6-style shared-slot boot trap
    returns.
13. **The 24-permutation order break (§6)** — decide before the first param ships.
14. **Triangle LFO at high Rate:** raw triangle's slope discontinuity at 20 Hz modulation puts a
    click-rate buzz on the delay derivative — the Juno's own LP'd triangle (I+II) is the fix: one-pole
    at 4× rate on the LFO output, always on above 5 Hz.

---

## 10. Hard-rule compliance checklist (laws 1–10, walked)

1. **Bus reality (−26 dBFS):** compander REF = 0.05 linear (§3.4); grit drive k spans 1–8 so program
   *reaches* the poly bend; noise floor −60 dB relative to REF not FS; unity-through gate at
   −26 dBFS pink (§6). No literature level copied anywhere.
2. **Chassis:** fb275 exact — 2 dropdowns (Type, Character) + 8 back knobs 4×2 + front 3 + Mix +
   pills (§4). Pragmatic Title-case names; no jargon (`Low Keep`, not "Crossover HPF"; `Color`, not
   "Reconstruction LP").
3. **Time params:** Sync pill spans 4 bars → 1/256 (§4, §9.11).
4. **Mix 100 % = fully wet** (processor-owned equal-power); Type/Character switches fade-swap-recover,
   never cut (§3.9).
5. **Params evolve 0→100:** per-knob evolution statements (§4); Types each carry a measured
   discriminator (§2, "the family tell"); locked-rate Characters keep Rate alive as a scaler.
6. **Nothing free-runs:** noise env-gated (§3.4); feedback tail dies with input (loop < 1 §3.6);
   max stable loop gain stated: **0.85 knob ceiling from 1.06 in-loop small-signal gain, expander
   excluded from the loop** (§3.6).
7. **No clicks:** full glide table §3.9; delay-length glide; LFO phase continuity; fade-swap-recover;
   compander re-seed.
8. **CPU:** ≤ one delay-device unit worst case, type-gated, sleep-gated, **zero oversampling** (§8).
9. **Audible ⇒ visible + dramatic:** Voice Orbits driven by real engine telemetry — every param
   listed with its pixel (§5.2); idle-dim/playing-bright explicit.
10. **Recycle first:** Appendix A — hermite4, softClip, onePole, BBD-darkening law, SmoothRandom +
    wow stack, CharBias table pattern, engine contract, glide idiom, pmenu presets, fx-rack chassis.
    All verified by reading with line numbers, none assumed.

---

## 11. Open questions for Max

1. **Type list lock:** 7 Types as proposed? In particular — keep `Pedal` AND `Dark` (both
   single-line; the discriminators differ — wobble vs breathing darkness) or merge into one
   BBD-pedal Type with Characters covering both?
2. **Dimension boundary:** confirmed that Dimension/SDD-320 ships in the future Hyper device, and
   this device deliberately has no "Dimension" Type (only its Low-Keep/wet-width lessons)?
3. **The 2nd pill:** `Sync` proposed. Alternative candidates: `Mono In` (JUN-6-style input summing)
   or `Duo` (tap doubling). Sync felt most Serum-parity. Your call.
4. **SYN_FX_ORDER:** extend to 24 permutations vs redesign to per-device order slots — this decision
   is state-format-breaking later, so it needs your lock before wiring (§6, §9.13).
5. **Character count:** 8 per Type = 56 voicing rows to tune. Trim to 6/Type to cut the sweep matrix?
   (The 23-mode distortion sweep says budget ~2 sessions for 56.)
6. **Detune ceiling:** 50 cents is past "chorus" into "broken 12-string" at the top — keep (no
   playing safe) or cap 30?
7. **Preset count:** 13 sketched — enough for launch, or match the delay's larger bank?
8. **Viz pick:** Voice Orbits + faint comb curtain composite (§5.2) — approve direction before the
   card mockup? (Mockup will be interactive + audible per the fb296 law.)

---

## Appendix A — Recycle inventory (verified by reading, not assumed)

* **`TerrainChorus.h`** — `hermite4()` `:129-143` (lift verbatim); `advancePhase` `:146-153` (master-
  phase seed); the antiphase constant `RIGHT_PHASE_OFFSET = π` `:19` + right-rate skew `1.07` `:18`
  (the `Wide 106` Character number); ReconLP 4th-order Butterworth pair `:189-219` (usable, though the
  engine's per-sample one-pole tracking law §3.4 supersedes it). ⚠️ Do NOT copy: `WET_GAIN 2.5` `:20`,
  the inert tanh/sinh compander `:161-183`, unglided `setParams` `:49-63` (§9.1).
* **`DelayEngine.h`** — THE engine contract to mirror (`:36-100` lifecycle/setters, `processSample`
  wet-only, `flush`); `softClip` `:315`; `onePole(hz)` `:324`; **the BBD clock-darkening law
  `:120-126` + its per-sample application `:291`**; companding follower coefficient idiom `:135`;
  per-sample glide idiom (`smCoef` ~15 ms) `:57` — all reusable as-is.
* **`TapeMachines.h`** — `SmoothRandom` `:215` (Drift, verbatim); the Cassette triple-LFO wow stack
  (0.6 Hz ±2.0 ms / 2.2 Hz ±0.8 ms / 7 Hz ±0.4 ms — Wow's deterministic bank, verbatim); `DCBlocker`
  (the mandated 10–20 Hz spec); `OnePoleLP/HP`.
* **`VintageReverb.h:337-339`** — `struct CharBias` + `static constexpr CHAR[8]`: the Character
  voicing-table pattern. Copy the pattern for the 7×8 chorus table.
* **`DistortionEngine.h`** — the deferred char-fade + re-seat machinery (`:207`, `:227` flush-snap
  discipline) — the §3.9 fade-swap-recover implementation precedent.
* **`ParameterIDs.hpp:374-394`** — the `SYN_DLY_*` block: clone the grammar (TYPE/CHARACTER/front/
  back/SRC/POWER) as `SYN_CHR_*`.
* **`PluginProcessor.cpp`** — `:6300-6301` kVoiceToFxPad (the −26 dBFS proof); `:7159/:7161/:7326/`
  `:7328/:7358/:7360` the exclusion sums that gain a `+ chrSend` term; `:5860` + `:7383` the
  SYN_FX_ORDER permutation switch to extend (§6).
* **UI** — `Design/fx-rack-v7-CANONICAL.html` + `fx-back-panel-mockup.html` (frozen chassis, do not
  re-author); `TIC.presets`/`.pmenu` glass for the preset menu (bullet `•`, name-first save); the
  echo-timeline renderer (fb312) as the Warp Ribbon base; the filter live-analyzer band probe for the
  comb curtain tint; the legacy chorus block (`index.html:1492-1660, :5447-5480, :9147-9149,
  :9320-9322`) stays untouched.
* **Confirmed greenfield:** no `SYN_CHR_*` ids exist; no multi-tap modulated-delay engine exists (the
  DelayEngine is 2-tap fixed-role); the Micro dual-head reader is new code (§3.5); the WebSliderRelay
  4-point chain must be built for every new id.

---

## Sources

**Serum**
* Serum manual (chorus module, 4-voice/params): https://s3.amazonaws.com/decembercymatics/Serum_Manual.pdf
* Serum 2 web manual root: https://xferrecords.com/web-manual/serum-2/welcome
* Serum 2 What's New (FX view, Hyper/Dimension split): https://static.xferrecords.com/Serum%202%20What's%20New.pdf
* MusicRadar Serum FX guide (chorus Delay 1/2, Feedback, LP): https://www.musicradar.com/how-to/a-quick-guide-to-xfer-records-serums-effects
* Serum 2 reviews (FX view/buses): https://polarity.me/posts/polarity-music/2025-03-18-serum-2-overview-and-thoughts/ · https://www.adsrsounds.com/serum-2-everything-new-in-serum-2-2025/
* Serum Hyper/Dimension explainer: https://www.patreon.com/polarity_music/posts/what-does-serums-159562938

**BBD physics**
* Raffel & Smith, *Practical Modeling of Bucket-Brigade Device Circuits*, DAFx-10 (read in full — filters, compander τ=10000·C, THD=1.01^(N/1024)−1, poly a=1/8 b=1/18, insertion gain): https://dafx10.iem.at/proceedings/papers/RaffelSmith_DAFx10_P42.pdf
* Companion filter script: https://ccrma.stanford.edu/~craffel/software/bbdmodeling/BBDfilter.m
* Dattorro, *Effect Design Part 2: Delay-Line Modulation and Chorus*, JAES 45 (interpolation, allpass caveat): https://ccrma.stanford.edu/~dattorro/EffectDesignPart2.pdf
* JOS, Delay-Line Interpolation (PASP): https://ccrma.stanford.edu/~jos/pasp/Delay_Line_Interpolation.html

**Lineage hardware**
* pendragon-andyh Juno-60 chorus measurements (0.513/0.863/9.75 Hz, 1.66–5.35 ms, ~70 kHz clock): https://github.com/pendragon-andyh/Juno60/blob/master/Chorus/README.md
* Juno chorus KVR analysis thread: https://www.kvraudio.com/forum/viewtopic.php?t=489346
* Florian Anwander, Roland choruses & ensembles (RS-202 6.25/0.66 Hz 120° pairs; SDD-320 0.25/0.5 Hz, 7.5–10 ms, ±1.5–2.5 ms, compander, 80 Hz HP; Juno MN3009): https://www.florian-anwander.de/roland_string_choruses/
* Roland CE-1 specs: https://support.roland.com/hc/en-us/articles/201927539-CE-1-Technical-Specifications · UA CE-1 manual: https://help.uaudio.com/hc/en-us/articles/33538165732500-Roland-CE-1-Chorus-Ensemble-Manual
* Boss CE-2 circuit analysis: https://www.electrosmash.com/boss-ce-2-chorus/pedals/delay/boss-ce-2-analysis.html
* Haible/Solina Triple Chorus: http://jhaible.com/legacy/triple_chorus/triple_chorus.html
* Dytronics/Songbird tri-chorus history: https://reverb.com/item/201801-tri-stereo-chorus-dytronics-cs-5-aka-songbird-tsc1380-aka-dyno-my-piano-618-michael-landau
* UAD Tri-Stereo Chorus review (0.03–7.45 Hz manual, per-voice waveform knobs, Choral Enhance): https://www.musicradar.com/reviews/universal-audio-dytronics-tri-stereo-chorus
* H3000 preset list (#231 MICROPITCHSHIFT / Layered Shift, #519): https://m.barryrudolph.com/recall/manuals/eventideh3000presets.pdf

**Modern references**
* Soundtoys MicroShift manual (Detune %, Delay %, Focus 20 Hz–10 kHz high-band-only, Styles I/II/III ↔ #231/#519/DMX 15-80): https://www.soundtoys.com/wp-content/uploads/MicroShift-Manual.pdf
* Eventide TriceraChorus QRG (Rate 0.1–20 Hz, Depth L/C/R, Detune ±40c, Chorus/Chorale): https://www.eventideaudio.com/downloads/tricerachorus-qrg/
* Valhalla UberMod modes & MOD parameters (tap taxonomy, slow/fast banks, randomized triangles, diffusion): https://valhalladsp.com/2012/01/06/valhallaubermod-the-modes/ · https://valhalladsp.com/2012/01/07/valhallaubermod-the-mod-parameters/ · https://valhalladsp.com/2012/03/09/valhallaubermod-the-history/ · https://valhalladsp.wordpress.com/tag/ubermod/
* Arturia Chorus JUN-6 (modes, Manual depth 0–10 ms default 4.44, LFO Phase width): https://www.arturia.com/products/software-effects/chorus-jun-6/overview · manual: https://downloads.arturia.net/products/chorus-jun-6/manual/juno-chorus_Manual_1_0_EN.pdf
* Arturia Chorus DIMENSION-D manual (signal flow: comp+filters → BBD → exp+filters → inverted cross-mix → width; read pp. 6–12): https://dl.arturia.net/products/chorus-dimension-d/manual/chorus-dimension-d_Manual_1_1_EN.pdf
* TAL-Chorus-LX: https://tal-software.com/products/tal-chorus-lx
* Chase Bliss Warped Vinyl (1024-stage BBD, Lag): https://www.chasebliss.com/warped-vinyl-hifi
* Mono-compatibility groundwork: https://www.sonible.com/blog/stereo-to-mono/ · https://kernaudio.io/guides/stereo/stereo-without-phase-issues

*(Repo citations throughout: TerrainChorus.h, DelayEngine.h, TapeMachines.h, VintageReverb.h,
DistortionEngine.h, IndyFxChain.h, ParameterIDs.hpp, PluginProcessor.cpp, index.html — exact lines in
Appendix A and §6.)*
