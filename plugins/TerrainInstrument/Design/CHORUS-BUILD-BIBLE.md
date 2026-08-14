# Terrain Instrument — Chorus Build Bible

*Researched 2026-08-14 (dedicated chorus researcher). Written to the DISTORTION-BUILD-BIBLE.md bar:
measured numbers, exact math, named lineage, zero hand-waving. A builder must be able to implement the
device from this file alone.*

> **AUDIT PASS — 2026-08-14 (adversarial).** Every repo citation in this file was re-opened and checked
> line-by-line; four external claims were re-fetched from source. **What changed:** the chorus/flanger
> boundary rationale (§0 — the old comb-fundamental argument was inverted), the CE-2 BBD stage count
> (§1.2), the Anwander/pendragon Juno I+II conflict (§1.3), the MicroShift time-varying correction
> (§1.7/§2.5), the `Pedal` mono-cancellation bug (§2.2/§3.8), the delay-buffer size (§3.2), the
> `Color` direction inversion (§3.4), the micro-shift crossfade-comb math (§3.5), the feedback loop-gain
> arithmetic (§3.6), the modulation-slope number (§3.7), the missing mono rows (§3.8), per-Type knob
> liveness (§4.3), the `SYN_FX_ORDER` "extend to 24" plan (§6/§9.13 — illegal under the birth-cardinality
> law), and the legacy-compander gain figure (§9.1). Anything still unproven is tagged **⟨UNVERIFIED⟩**
> inline — do not quote a tagged line as fact. Verification log: §12.

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
`SYN_DLY_*` exactly (`ParameterIDs.hpp:374-401`) as `SYN_CHR_*`.

### What is IN (7 Types — §2)
The BBD synth chorus (Juno), the pedal chorus (CE-1/CE-2), the LA studio tri-chorus
(Dytronics/Songbird), the string-machine ensemble (Solina/RS-202), the H3000 micro-pitch "chorus
without wobble", the tape/vinyl wow chorus, and the dark long-BBD chorus. Seven distinct
tap-count × LFO-topology answers, each with a measurable discriminator.

### What is OUT, and where its sound went
* **Flanger** — its own future device (`FLANGER-BUILD-BIBLE.md`, 0.05–40 ms, incl. through-zero).
  ⚠️ **CORRECTED BOUNDARY MATH (the old version of this paragraph was backwards).** A dry+wet comb at
  delay *d* has notches spaced **1/d Hz** apart with the first at **1/(2d)**. So *shorter* delay pushes
  the comb fundamental **UP** and makes notches **sparser**, not denser.

| d | notch spacing 1/d | first notch 1/(2d) | notches below 10 kHz | what the ear calls it |
|---|---|---|---|---|
| 0.5 ms | 2000 Hz | 1000 Hz | 5 | swept resonant filter — *flange* |
| 1.5 ms | 667 Hz | 333 Hz | 15 | jet, still tonal |
| 5 ms | 200 Hz | 100 Hz | 50 | doubling with a tint |
| 20 ms | 50 Hz | 25 Hz | 200 | two voices — pure *chorus* |
| 40 ms | 25 Hz | 12.5 Hz | 400 | slap/ADT |

  The perceptual split is **notch density** (can the ear resolve individual teeth?) plus two things
  delay time cannot express: **regeneration** (flangers run 50–95 % feedback to sharpen the teeth into
  a resonant sweep) and **through-zero** (a matched dry delay so the two paths cross d = 0 — the only
  way to get true null-sweep). There is **no single ms number** that separates the devices — the split
  is by *identity*, and the two Time ranges deliberately overlap (Chorus 0.5–40 ms, Flanger 0.05–40 ms).
  Our `Time` floors at 0.5 ms so the *edge* of flanger territory is reachable (no playing safe), but
  feedback-dominant through-zero flanging is not this device: our feedback ceiling is 0.82 (§3.6) and
  there is no dry-path delay.
* **Dimension / SDD-320** — lives in the future **Hyper/Dimension** device (Serum 2 ships Chorus AND
  Hyper/Dimension as separate menu entries; we mirror that split). Covered lightly in §1.4 because two
  of its tricks (bass-compensated dry, wet-only cross-mix) are stolen for our `Low Keep` and `Width`
  laws. The deep dive belongs to the Hyper bible.
* **Hyper / supersaw unison chorus** (Serum's 1–7 voice micro-delay detuner) — Hyper device.
* **Rotary/vibe** — different physics (AM + spectral rotation), out of scope entirely.

### The legacy June block is NOT this device
`TerrainChorus.h` (the "JUNE" section — its own FX page `id="fxPageChorus"`, *not* a sub-block of the
DLY page: `index.html:1492-1660` CSS / `:5446-5481` markup / `:9147-9149` param meta / `:9320-9322`
juceIds, params `CHORUS_AMOUNT/WIDTH/CHARACTER`, wired through `IndyFxChain.h:89, 116, 130, 193,
259-261`) **stays untouched** where it is. *(All of the above re-opened and confirmed 2026-08-14; only
the "inside the DLY page" phrasing and the two end-line numbers were wrong.)* The new device is a separate engine (`ChorusEngine.h`) in the SYN FX rack.
What we recycle from the old one is inventoried in Appendix A — and one of its "features" is a
measured trap (§9.1).

---

## 1. History and circuits — the lineage that defined the effect

### 1.1 The BBD physics (Raffel & Smith, DAFx-10 — the paper for this whole device)

A bucket-brigade device is an analog shift register: N capacitor stages clocked at f_clk, giving

```
delay = N / (2 · f_clk)          (two clock phases per stage-pair)
```

Chorus circuits use **128–1024** stages at high clock rates (a 1024-stage line at 10 ms needs
f_clk = 51.2 kHz; the Juno's short lines are 128/256-class — §1.3). *Formula sanity-checked against a
published datapoint: the MN3208 is 2048 stages and gives ~102 ms at a 10 kHz clock — 2048/(2·10 000) =
102.4 ms ✓.* Everything musicians love about "BBD warmth" is a short list of measurable defects:

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
   −1<x<1. H2-dominant, falls linearly per harmonic. ⚠️ **The `+1/8` is a DC OFFSET, not a
   recentering** — `f(0) = +0.125` exactly, and for any zero-mean program the `−x²/8` term removes
   only `E[x²]/8` (≈ 0.0002 at our −26 dBFS bus, six hundred times smaller than the constant). The old
   wording here said it "recenters the average" and contradicted §3.4/§9.5 in the same breath. It is
   why the DC blocker after the color chain is **mandatory**, not optional.
4. **Insertion gain** is frequency-dependent: 0…+2 dB at LF, sagging to −4…−6 dB near the clock
   Nyquist — a gentle extra tilt on top of the reconstruction filter.
5. **Noise** ≥ 60 dB below max signal, *below* the compander (so it breathes with the expander).

### 1.2 Roland CE-1 (1976) → Boss CE-2 (1979): the pedal chorus

The CE-1 Chorus Ensemble is the chorus circuit lifted out of the JC-120 Jazz Chorus amp — "the mother
of chorus." One BBD line (MN3002-class), compander, **two modes**: *Chorus* (fixed preset sweep, one
Intensity knob) and *Vibrato* (Rate + Depth knobs, wet-only). Delay region ~5 ms with a slow sweep;
stereo out = **dry on one jack, wet on the other** — the "true stereo" that made the width, and the
version that collapses most gracefully in mono (dry+wet mono sum = classic single-comb chorus, never
cancellation). ⚠️ **CORRECTED:** the CE-2 did **not** "shrink" the line — the CE-2 uses an **MN3007,
which is 1024 stages** (Wikipedia/BBD datasheet listings), i.e. a *longer* line than the CE-1's
MN3002 (512-class ⟨UNVERIFIED — 512 is the common datasheet listing, not re-confirmed here⟩). What
the CE-2 actually shrank was the **box, the mode count and the output** (single mono dry+wet, no
Vibrato mode). It keeps pre-emphasis before the BBD and de-emphasis after (a treble boost/cut pair
that ducks BBD hiss — the same pre/de idea as distortion's `Emphasis`).

### 1.3 Juno-60 chorus (1982) — the two-BBD antiphase trick ★ our default Type

Measured (pendragon-andyh/Juno60 repo, from the service notes + hardware capture):

| Mode | LFO rate | LFO shape | Delay sweep |
|---|---|---|---|
| **I** | **0.513 Hz** | triangle | **1.66 → 5.35 ms** |
| **II** | **0.863 Hz** | triangle | **1.66 → 5.35 ms** |
| **I+II** | **9.75 Hz** | LP-filtered triangle (≈sine) | **3.3 → 3.7 ms** (a vibrato) |

Architecture (**re-fetched and confirmed verbatim from the pendragon README, 2026-08-14**): *"The
module contains 1 triangle-wave LFO, modulating 2 256-step Bucket-Brigade delay-lines (1 for left and
1 for right). The modulation signal of the right delay-line is inverted so it is effectively 180
degrees out of phase with the left channel"*, the chips *"effectively sampling at about 70 kHz"*.
Both outputs are wet+dry mixed per side. The antiphase inversion is the whole trick: when L sweeps
sharp, R sweeps flat, so the ear hears width and shimmer instead of pitch wobble — and the mono sum
averages the two opposite combs nearly flat (§3.8). A 12 dB/oct LP before the line anti-aliases; the
noise floor is famous ("Juno hiss").

*Chip:* Anwander independently lists the Juno-6/60/106 chorus as **2× MN3009** — the two sources agree
on the topology. ⟨UNVERIFIED: the MN3009's stage count. pendragon models 256 steps; several datasheet
listings give 128. It changes only the clock↔delay mapping (256 stages at f_clk = 77 kHz gives the
1.66 ms floor; 128 stages needs 38.6 kHz), never the topology or the audible result — we model the
*delay window*, not the clock.⟩

⚠️ **UNRESOLVED SOURCE CONFLICT on the I+II rate — do not paper over it.** Anwander's table gives
**Juno-6** as 0.4 Hz / 0.6 Hz triangle + **8 Hz** sine-ish I+II, and **Juno-60** as 0.5 Hz / 0.8 Hz +
an I+II at **1 Hz at 8 % amount** — a **10× disagreement** with pendragon's 9.75 Hz for the *same
machine*. (The previous version of this paragraph mis-attributed Anwander's Juno-**6** figures to the
60 and called the gap "slightly different measured constants". It is not slight.) We ship the
pendragon numbers (captured from a real unit) as the `I`/`II`/`I+II` Characters, and the sweep
harness must A/B a 9.75 Hz vs a 1 Hz `I+II` against reference recordings before the Character locks.
Both agree on I ≈ 0.5 Hz and II ≈ 0.8 Hz, so only `I+II` is in doubt.

### 1.4 Roland SDD-320 Dimension D (1979) — light coverage (deep dive → Hyper bible)

2× MN3007 (1024-stage), four preset buttons. **Confirmed from Anwander 2026-08-14:** modes 1-2 =
**0.25 Hz**, modes 3-4 = **0.50 Hz**, base delay **7.5 or 10 ms**, modulation **±1.5 to ±2.5 ms**,
"a compander circuit for noise reduction", and "a passive 3 dB highpass around ~80 Hz". Anwander
confirms cross-channel mixing of the original signal. ⟨UNVERIFIED: the **inverted polarity** on the
cross-mix and the dry **bass boost** — both come from the Arturia Dimension-D manual, which was not
re-read this pass.⟩ That polarity trick is "space without warble." We steal exactly two lessons: (a) keep the lows dry and
centered (`Low Keep`, §4), (b) width belongs on the WET only. The device itself → Hyper.

### 1.5 String machines — Solina / Roland RS-202 (1975-6): the 3-phase ensemble

The "ensemble" sound is **three BBD lines (3× MN3002) panned/summed, each driven by the SUM of two
LFOs — one slow ("chorus") ~0.6–0.66 Hz, one fast ("vibrato") ~6–6.25 Hz — with the three lines'
phases spaced 120°**. **Confirmed verbatim from Anwander 2026-08-14:** RS-202 = *"BBDs: 3x MN3002"*
with *"6 LFOs organized as three pairs of LFOs, in each pair one LFO runs at 6.25 Hz the other LFO
runs at 0.66 Hz"* and *"three separate LFOs, which trigger each other to provide a 120 degree phase
ratio"*. (Solina/Haible triple chorus: 2 LFOs × 3 outputs each, 120° apart, one CV mix per BBD.) Because the three combs rotate
around the phase circle, *some* pair is always maximally detuned — the sound never breathes in unison.
This is why one string machine sounds like a section. Deep modulation, wet-dominant mix; the fast LFO
rides on top of the slow (two visible sideband rates — the discriminator, §2.4).

### 1.6 The LA studio tri-chorus — Dytronics CS-5 / Songbird TSC-1380 / Dyno-My-Piano TSC-618 (early 80s)

**Three BBD voices panned hard L / C / R, each with a dedicated LFO** (same 120° family as the
ensemble but *panned apart* instead of summed), preset mode = slow sweep + secondary faster LFO;
manual mode ⟨UNVERIFIED: **0.03–7.45 Hz**, per-channel "Chorus Wave Form" depth knobs and the
"Choral Enhance" per-channel feedback — from a MusicRadar review of the UAD recreation, not re-read
this pass⟩. The center voice keeps the mono sum solid while the sides swirl — the "syrupy" Michael
Landau / 80s session-guitar wall. Eventide's TriceraChorus is the modern homage ⟨UNVERIFIED:
Rate 0.1–20 Hz, per-voice Depth L/C/R, stereo Detune ±40 cents L/R, Chorus vs Chorale modes,
mix→100 % = vibrato⟩.

### 1.7 Eventide H3000 micro-pitch (1987) — chorus without an LFO

Preset **#231 MICROPITCHSHIFT (Layered Shift algorithm)**: left voice a few cents sharp, right a few
cents flat, each behind a small unequal delay. No cyclic wobble at all — the "detune" is a *constant*
pitch offset made by a delay line whose read head slides at constant slope with crossfaded dual heads.
Preset #519 is the same trick on a different shifter (different delay variation + frequency response);
the AMS DMX 15-80 does it with wider delay wander and a harder de-glitch. Soundtoys MicroShift models
exactly these three as Styles I/II/III (**manual re-read verbatim 2026-08-14**): **Focus** = *"the
crossover point of a 2-band crossover filter, applying the affected signal only to the high band…
defaults to 20 Hz, but can go all the way up to 10 kHz"* ✓ — the direct source of our `Low Keep`;
**Detune** and **Delay** are *percentages* (50 % = half, 200 % = double) of each style's amount.

⚠️ **CORRECTION to the old text (which called MicroShift a constant offset):** the manual is explicit
that *"the pitch shifting for each style is continually time varying"* and *"the delay for each style
is continually time varying"* — MicroShift is **not** a static detune. The *constant*-offset claim
belongs to the **H3000 preset itself** (a real pitch shifter), not to Soundtoys' emulation. That
distinction is load-bearing for our §2.5 discriminator: `Micro`'s **steady-state offset must be
constant** (that is the honest, measurable tell), and any wander is supplied by the `AMS` Character /
the `Drift` knob — never baked into `Detune`. There is no "Tight↔Loose" label on the Delay knob;
that phrasing was invented and has been removed.

### 1.8 Tape/vinyl wow — the aperiodic chorus

Wow (0.5–2 Hz), flutter (6–10 Hz) and random scrape from tape transports modulate delay just like an
LFO but *aperiodically*. Chase Bliss Warped Vinyl (1024-stage BBD modulated with warped-record ramps
+ a `Lag` asymmetric-glide knob) proved this as a chorus genre. We already own the exact machinery:
`TapeMachines.h` `SmoothRandom` (`:215`) and the Cassette triple-LFO wow stack (0.6 Hz ±2.0 ms /
2.2 Hz ±0.8 ms / 7 Hz ±0.4 ms — recorded in the distortion bible's recycle audit).

### 1.9 The modern references

* **Serum 2 Chorus.** Param list confirmed from MusicRadar's Serum FX guide, verbatim: *"an adjustable
  modulation **Rate**. The **Delay 1** and **2** dials let you change the timing of the left and right
  chorus signals, and **Depth** heightens the strength of the effect. The **Feedback** dial lets you
  boost the effect's resonance, and the handy **LP** filter is used to cull the wet signal's high
  frequencies."* ⟨UNVERIFIED: the **"4-voice / 2 L taps + 2 R taps"** count and the **0–20 Hz** Rate
  range — no reachable Serum 2 manual page documents the chorus (the web-manual chorus URL 404s), and
  MusicRadar describes Delay 1/2 as **one per side**, not as two stereo *pairs*. Treat the topology as
  "at least one tap per side, feedback and a wet LP", and do not cite a Serum voice count to Max.⟩
  Serum 2 adds a per-FX Level slider. Serum's
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
  (`lfoR = −lfoL`). ⚠️ The legacy `TerrainChorus.h` does declare `RIGHT_PHASE_OFFSET = π` (`:19`,
  seeded at `:42`/`:231`) — **but it does not achieve antiphase**: it also skews the right rate by
  `RIGHT_RATE_RATIO = 1.07` (`:18`, applied `:56`), so the two phases drift apart continuously and the
  π offset is meaningless after a few seconds. It is also a **sine**, not a triangle (`:152`). Copy the
  constant, not the topology — our `June` locks ONE accumulator and negates the *output* (§3.3). Base 3.5 ms, sweep
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
* **Recipe:** ONE BBD line, **read TWICE** — L at `base`, R at `base + Phase·1.2 ms` (§4.3; ≈0.35 ms
  at the factory `First Pedal` setting — the CE-1's two output jacks feeding two amps a few feet
  apart; a 0.35 ms offset combs first at 1.4 kHz, shallow and mono-safe; Phase 0 = true mono CE-2). Sine-ish LFO (LP'd triangle), base 5 ms, sweep ±3.5 ms at Depth 100, **same** modulation
  on both reads (this is the one Type with *no* L/R modulator opposition — that is its tell). Input
  pre-emphasis +6 dB@3 kHz into the BBD poly, de-emphasis after (the CE-2 hiss-ducking pair). At
  Width 0 both reads collapse to `base` ⇒ both sides identical dry+wet = true CE-2 mono.
* 🛑 **FIXED DEFECT — the old recipe silently deleted itself.** The previous text specified
  `L = dry + wet, R = dry − wet`. With a **single mono wet** `w`, that is fatal twice over:
  (a) **mono sum** `= 2·dry + w − w = 2·dry` — the wet cancels **completely**, the effect vanishes on
  any mono fold-down (not "< 3 dB ripple"; **−∞ dB**); and (b) the wet-only M/S Width of §4 computes
  `mid = (w + (−w))/2 = 0`, so **at Width 0 the Pedal Type outputs no wet at all** — the exact opposite
  of the "true CE-2 mono" the same bullet claimed. The polarity flip survives only as the
  `Wet Flip` Character below, tagged mono-hostile, and §3.8's mono gate runs on it.
* **Characters:** `Chorus` (fixed preset sweep 0.4 Hz — the CE-1 one-knob mode; Rate scales it),
  `Vibrato` (wet-only regardless of Mix < 100 — the CE-1 second mode; Mix then blends vibrato level),
  `Grit` (input poly drive ×4 — the dirty-preamp CE-1 legend), `Slow Amp`, `Fast Amp` (JC-120
  rotary-ish fixed rates 0.8/6.5 Hz), **`Wet Flip`** (R wet polarity inverted — the two-amps hack;
  ⚠️ **mono-hostile by construction: wet nulls on fold-down**, badge it in the tooltip and let the
  §3.8 gate report it rather than block it — "no playing safe" means we ship it *labelled*, not
  hidden), `Warm`, `Thin` (recon LP 4 kHz / HP'd wet 300 Hz).
  *(The old `Dry L / Wet R` "hard split" Character is folded into `Wet Flip`: a hard dry/wet jack
  split is the same mono math with the additional problem that one channel would carry no dry at all,
  which the rack's exclusion-sum dry accounting cannot express — §6.)*
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
  **442.29 Hz** / R at **437.72 Hz** with Detune = 9 cents (`440·2^(±9/1200)`; the old text's 437.8
  was rounded off the wrong way). No other Type shifts steady-state frequency. Gate the probe on
  **≥ 30 s** of tone — a *slow* wander would also read flat over 1 s (§1.7 correction).
* ⚠️ Feedback around a pitch shifter = an upward/downward **spiral** (each pass adds +c). That is a
  *feature* (the classic H3000 shimmer-spiral) but the loop gain law applies doubly: cap fb at
  **0.65** for this Type (0.7 × the §3.6 headroom correction) and keep the in-loop LP mandatory.

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
* ⚠️ **TWIN RISK — `Dark` vs `June` (this is the real near-twin, not Pedal/Dark; law 5 cuts both
  ways).** Structurally `Dark` is `June` with a longer `Time`, a lower `Color` and a fixed R stagger:
  three things a user can already reach with the knobs. The only thing a knob *cannot* reach is the
  **per-sample recon-LP tracking of instantaneous d(t)** (June's cutoff is per-block static). Falsify
  before ship: run both Types at Time 28 ms, Color 15, Depth 40 and measure **wet spectral-centroid
  peak-to-peak over one LFO cycle**. Gate: `Dark` ≥ **6 semitones** of centroid swing where `June`
  measures **< 0.5**. If `June` at those settings lands within 2 dB spectrally of `Dark`, **cut
  `Dark`**, let `Time` reach 40 ms on `June`, and re-home `Murk`/`Pumped`/`Hissy`/`Cheap` as June
  Characters — a Type that a knob can imitate is a fake Type.
* ⚠️ **`Pedal` vs `Dark`** (Max's §11 Q1) are *not* twins after the §2.2 fix: `Pedal` is one line read
  twice with **co-phase** modulation and pre/de-emphasis; `Dark` is two **antiphase** taps with a
  d(t)-tracked LP. Pairwise gate: inter-channel correlation at Depth 100 — `Pedal` stays **> +0.8**,
  `Dark` dips **< +0.2**. If that gate fails, the merge Max asked about is the right call.

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

* **One stereo circular buffer**, power-of-two, **≥ ceil(fs · 128 ms)** ⇒ **8192 samples/channel at
  48 kHz** (16384 at 96 k). Write once, read up to 4 taps (Ensemble `VP Choir`) — taps are READS, so
  voices are nearly free (§8).
  🛑 **FIXED DEFECT — the old 64 ms / 4096 figure overflows.** Worst legal case is `Dark`: base 40 ms,
  R stagger ×1.31 ⇒ **52.4 ms**, and the excursion clamp below permits `excursion ≤ base − 0.05`, so
  the R read can legally reach **≈ 104.8 ms**, plus 12 ms Micro stagger + 2.5 ms Drift + interpolator
  guard ≈ **120 ms**. 64 ms would have wrapped the read past the write head at high Depth on `Dark` —
  metallic garbage, and the legacy engine's `jassert`-only guard (§9.1d) is exactly this bug shipped.
  Either size to 128 ms as above **or** cap the excursion per Type (`excursion ≤ min(depth_ms,
  base_ms − 0.05, 12 ms)`); the bible picks the buffer, because a Depth cap on `Dark` would flatten
  the 0→100 sweep (law 5). **8192 × 2 ch × 4 B = 64 kB per instance** — free.
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
`ph_i = master + offset_i` (offsets: `{0, π}` for 2-tap; **`{0, 2π/3, 4π/3}`** for 3-tap — the old
line read "0/2π/3/4π/3", which parses as 0, 2, π/3, 4π/3 and is not a 120° set; plus the +30°/90°
channel rotations). ⚠️ **Phase-G one-clock law:** separate per-tap accumulators integrate glide skew during
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
(past the hardware: the Juno window's half-excursion is (5.35−1.66)/2 = 1.845 ms, ×1.6 ≈ **±2.95 ms**
⇒ **±16 cents at the stock 0.513 Hz** and **±32 cents at 1 Hz** — the old text's "±40 cents at 1 Hz"
was ~25 % hot; `1200·log2(1 + 2π·1·2.95/1000) = 32.3`. Seasick on purpose, law "max = just past
useful": to actually reach ±100 cents the user must also push Rate past 3 Hz, which the exp taper
makes easy).

### 3.4 The BBD color chain — calibrated to the −26 dBFS bus (law 1) ⚠️ the measured trap

Order per tap-sum, per channel: `pre-emph (Pedal only) → poly grit → [delay reads] → recon LP →
de-emph → compander expand → noise inject`, compander *compress* at the input write.

* **⚠️ THE COMPANDER CALIBRATION TRAP — measured on the in-tree chorus.** `TerrainChorus.h:167-180`
  "compands" with `tanh(x·1.5)·0.7` in and `sinh(x·1.5)/1.5·1.4` out. At the real bus level
  (−26 dBFS = 0.05 linear) both curves sit deep in their LINEAR region, so the pair is a **plain
  fixed gain**: `(1.5·0.7) × (1.4) = ×1.47 = +3.35 dB`, level-independent. *(Worked: 0.05 →
  tanh(0.075)·0.7 = 0.05243 → sinh(0.07865)/1.5·1.4 = 0.07348.)* 🛑 **The old text said "a pure ×0.98
  gain" — wrong by 3.4 dB and wrong in sign; it is a hidden boost, not a null.** It is also tracking
  an `env` it never reads (`:163`, and the file's own TODO at `:160` admits it). Either way the
  conclusion stands and gets stronger: **the "NE570 character" is a costume — it contributes a static
  trim and ZERO level-dependence at our program level** (same failure class as fb283's inaudible
  "102 % divergence"), *and* it is 3.4 dB of un-budgeted make-up hiding inside a "character" block.
  The real NE570 is not a static
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
* **Grit:** the Raffel poly `x − x²/8 − x³/18 + 1/8`, drive-scaled: `g(x) = poly(x·k)/k`.
  🛑 **FIXED DEFECT — the Color direction was inverted here.** The old line said `k = 1 + Color·7`
  ("Color > 60 pushes into the bend"), which contradicts §4 P6 where **Color 0 = murk (grit ×8)** and
  **Color 100 = studio-clean (poly off)**. Correct law, matching the knob:
  `k = 1 + (1 − color01)·7` (so Color 0 ⇒ k = 8, Color 50 ⇒ k = 4.5, Color 100 ⇒ k = 1 and the poly
  is bypassed outright). At Color 0 the bus lands at −26 dBFS × 8 = **0.4** — real, audible H2. The
  +1/8 term is a DC offset by design ⇒ **the house DC blocker (10–20 Hz, the `TapeMachines.h`
  DCBlocker spec) is mandatory after the chain** (distortion §4.1 lesson).
* **Recon LP:** one-pole (recycle `onePole(hz)` `DelayEngine.h:324`), cutoff from the clock law:
  `hz = clamp(0.45 · (N_eq / (2·d_sec)) , 1800, 16000)` with N_eq = 1024 — i.e. cutoff tracks the
  *instantaneous* delay, so deep sweeps audibly darken at the bottom of the cycle (`Dark`'s
  discriminator; scaled by `Color` toward bright = modeling off).
* **Noise:** −60 dB (Character-scaled to −48) white through the recon LP, **multiplied by the input
  envelope** (nothing free-runs, law 6): `n·env_in/(env_in+0.003)` — silence in = silence out, the
  Phase-G silence-class discipline (env-tracked, squared release).

### 3.5 Micro-shift math (the only nontrivial block)

Dual-head reader per side on the shared buffer. Head phase `q ∈ [0,1)` advances at `q += r/T·dt`
(r = shift ratio − 1, T = crossfade window): head A delay `dA = base + r·T·q`, head B same with
`q+0.5 mod 1`; gains `gA = 0.5−0.5·cos(2πq)`, `gB = 1−gA` (raised-cosine). Cents→ratio:
`r = 2^(c/1200) − 1`.

🛑 **FIXED DEFECT — the crossfade comb.** The old text claimed "both heads within r·T/2 ≈ 12 samples
… so comb error is < 0.3 dB". Both halves are wrong. At the equal-gain instants (`q = 0.25, 0.75`,
`gA = gB = 0.5`) the two heads are exactly **r·T/2** apart in delay, and two equal-gain taps at
spacing Δ produce a **full-depth** notch at `1/(2Δ) = 1/(r·T)` — not 0.3 dB, **−∞ dB** at that
instant. This is the well-known dual-head shimmer, and it is the real reason micro-shift is a
*small-cents* effect. The numbers (T = 40 ms, 48 kHz):

| Detune | r | r·T/2 (Δ) | Δ in samples @48 k | first notch `1/(r·T)` |
|---|---|---|---|---|
| 5 c | 0.00289 | 0.058 ms | 2.8 | 8.6 kHz — inaudible |
| 10 c | 0.00579 | 0.116 ms | 5.6 | 4.3 kHz — "air", the H3000 sound |
| 25 c | 0.01452 | 0.290 ms | 13.9 | 1.7 kHz — audible hollowness |
| 50 c | 0.02930 | 0.586 ms | 28.1 | **853 Hz** — honky, wrong |

**Mitigation (build this, don't hope):** scale the window so `r·T` stays put — `T = clamp(0.232/r,
20 ms, 60 ms)` keeps the notch ≥ 4.3 kHz up to ~11 cents and ≥ 1.7 kHz to 25 cents, and the T floor of
20 ms caps the crossfade AM sidebands at 2/T = 100 Hz. **Above 25 cents go to 4 heads** (`q` offsets
0/0.25/0.5/0.75 with a 4-way raised-cosine): equal-gain pairs are then r·T/4 apart, doubling every
notch frequency in the table. This is what §11 Q6 (the 50-cent ceiling) is really asking — the ceiling
is an **artifact budget**, not a taste call; 50 cents on 2 heads is a defect, on 4 heads it is a Type.

Down-shift = negative r
(delay grows — never reads past the write head; up-shift consumes delay ⇒ base must exceed
`r·T + 2 samples`, guaranteed by base ≥ 8 ms > 0.24 ms·margin). CPU: 2 heads × 2 sides × Hermite = 4
extra reads (8 at 4 heads), still trivial.

### 3.6 Feedback — the loop gain law (law 6)

`fb` returns the post-color wet **tapped pre-de-emphasis and pre-expander** into the write sum:
`write = in·comp + fb·wet_color`.

Gain stages and the **two blocks that must sit outside the tap point**:

| In-loop stage | worst small-signal gain | note |
|---|---|---|
| Hermite read | **1.035** (+0.3 dB passband ripple) | overshoots; the old text listed "≤ 1.0" and then quoted the ripple anyway |
| recon LP | 1.000 | one-pole, never > 1 |
| poly grit | **1.060** | slope at 0 at full drive (k = 8) |
| **compander expander** | **UNBOUNDED as env sags** | ⛔ **tap the feedback BEFORE it** |
| **de-emphasis** | **up to ×2 (+6 dB, Pedal)** | ⛔ **tap the feedback BEFORE it too** |

🛑 **FIXED ARITHMETIC.** The old line wrote `fb ≤ 0.90 / 1.06 / deemph_gain` and then announced
**0.85** — but 0.90/1.06/2 = **0.42**, so the stated ceiling silently ignored the divisor it had just
introduced, *and* it dropped the Hermite ripple it had just measured. Both are fixed by **moving the
tap**, which is the correct engineering answer anyway (real BBD pedals regenerate from the BBD output,
before the de-emphasis network). With expander and de-emph excluded:

```
loop_gain_max = 1.035 (interp) × 1.060 (grit) × 1.000 (LP) = 1.097
target loop gain ≤ 0.90  ⇒  fb_knob_max = 0.90 / 1.097 = 0.820
```

**Knob max 0.82 linear**, displayed 0–100 %. In-loop the recon LP guarantees decaying HF (BBD echo
behavior); `softClip` (`DelayEngine.h:315` — linear to ±1.4 then tanh) bounds any transient overshoot
(BIBO). Micro Type: **fb ≤ 0.65** (0.7 × the same 1.097 headroom correction — §2.5 spiral). Feedback
polarity per Character (`Enhance` uses +; a − option makes hollow combs — the CE-2-mod trick) — sign
lives in the table, not a knob. **Envelope gate (law 6):** the loop input is multiplied by
`env_in/(env_in+0.003)` alongside the noise inject, so a decayed note cannot sustain the loop even at
fb 0.82 — nothing free-runs.

### 3.7 Oversampling verdict: **NONE, ever**

The only nonlinearity is the gentle BBD poly (H2 ≈ −40 dB at program+grit); its aliases sit below
−80 dBFS at our bus level — under every perceptual gate in the distortion bible's §3.10 table.
**That, and only that, is the oversampling argument** — oversampling buys nothing where there is no
significant nonlinearity to alias.

🛑 **FIXED NUMBER.** The old text justified the verdict with "max head speed ≤ 0.06 samples/sample".
That is off by 6×: head speed is `|d'| = 2π·f·A/1000` s/s, and the legal corner is **Rate 20 Hz ×
excursion 2.95 ms ⇒ 2π·20·2.95/1000 = 0.371 samples/sample** — a −5.6-semitone downsweep at the peak,
i.e. deliberate FM/ring-mod territory at the top of the Rate taper, nothing like "0.06". (Micro's
constant `r ≤ 0.0293` at 50 cents really is tiny.) What that corner actually stresses is the
**interpolator's stopband**, not aliasing of a nonlinearity: at |d'| ≈ 0.37 the 4-point Hermite's HF
image rejection degrades and the wet dulls/roughens. **The fix if it ever measures badly is a per-Type
`Rate × Depth` soft-limit or a 6-point kernel — never oversampling**, which would not touch it.
Fixed cost, no Quality dropdown; the CPU story is §8. **Never oversample this device** (law 8).
*Harness gate: sweep Rate 0.02→20 Hz at Depth 100 on a 1 kHz sine and log THD+N; flag if it exceeds
−45 dB anywhere.*

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
* **Pedal** *(was missing — added by the audit)*: after the §2.2 fix the two reads are the SAME line at
  `base` and `base + 0.35 ms`, both modulated in phase. Mono sum = `2·dry + w(d) + w(d+0.35 ms)` — a
  fixed shallow comb first notching at **1.43 kHz**, riding on top of the moving base comb. Gate:
  mono ripple **< 4 dB** (looser than the antiphase Types by design — a single-line pedal is a comb,
  that IS the sound). ⚠️ The `Wet Flip` Character deliberately **fails** this gate (wet nulls to
  −∞ dB in mono); the harness must **report and badge** it, not block the Character.
* **Wow** *(was missing)*: 2 antiphase-weighted taps like June, but the modulators decorrelate
  (per-tap `SmoothRandom`), so the notches wander independently and the mono sum has no stationary
  notch at all. Gate: mono deviation < 3 dB **averaged over 10 s** (an instantaneous frame will
  exceed it — that is the aperiodic point, so the gate must be time-averaged or it fails a good
  engine).
* **Dark** *(was missing)*: 2 antiphase taps with an asymmetric R stagger (×1.31), so unlike June the
  two combs are NOT mirror images and their notches do not interleave cleanly. Gate: mono ripple
  **< 5 dB** at Depth 45 — and at Time 40 ms the comb is dense enough (25 Hz spacing) that the ear
  reads it as tone, not as a notch. Cross-check against `Slap Wide` (R stagger ×1.6), the worst row.
* **The three hard rules that make all of this true:** (1) **Width (M/S) touches WET ONLY** — dry is
  never side-boosted (dry M/S widening is the classic mono-collapse bug); (2) **wet polarity flips are
  Character choices, never defaults** (`Pedal/Wet Flip` only), and the harness runs the mono gate on
  every Character row; (3) **the wet must carry real MID energy on every Type** — a wet pair that is
  pure side (`w`, `−w`) makes `Width 0` output silence, which is how the pre-audit `Pedal` recipe
  deleted itself (§2.2). Structural check the engine must satisfy at every Type/Character/Width:
  `‖(w_L + w_R)/2‖ > 0` for at least one Width setting in [0, 1].

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

## 4. Chassis map — the fb275 device: 2 dropdowns + 8 back knobs + Mix (the house "11"), front 3 + pills

Param IDs: `SYN_CHR_*`, grammar cloned from `SYN_DLY_*` (**`ParameterIDs.hpp:374-401`** — the old
citation said `:374-394`, which stops two lines before `SYN_DLY_POWER` at `:397`, the very id it
claimed to include), covering `SRC_A/B/C/D/SUB/NOISE` pills + `POWER` (default **OFF** — confirmed:
`PluginProcessor.cpp:3486` registers `SYN_DLY_POWER` with `false`, "fb303 — OFF by default, dry
init") — and the 4-point WebSliderRelay chain for every one (the whole block is greenfield; the
silent-no-op trap applies in full).

**Param count, stated so it can be counted.** The house "11 params" label (used identically in the
Flanger/Compressor/EQ bibles) is **2 dropdowns + 8 back knobs + Mix = 11**. On top of that sit the
3 front hero knobs, the 2 pills and the 6 route pills. Full APVTS block: 12 floats (Rate, Depth,
Width, Mix + the 8 back knobs), 2 choices (Type, Character), 1 sync choice, 8 bools (Sync, Power,
6 route pills) = **23 ids**. *(The old header read "11 params (2 dropdowns + 8 back knobs)", which
counts to 10 — the arithmetic, not the chassis, was wrong.)*

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

> 🔧 **[CROSS-BIBLE AUDIT 2026-08-14] CHASSIS CORRECTION — `Type` is the HEADER PILL, not back-d1.**
> Verified in the shipped tree: on Reverb, Delay **and** Distortion, `*_TYPE` renders in the header
> `.fxr-type` `<select>` on the card centerline (`index.html` `DEVS[].tp` +
> `Design/fx-back-panel-mockup.html`); the two **back** dropdowns are `Character` + a second
> selector (`Mod Mode` / `Sync` / `Quality`). Spending back-d1 on `Type` duplicates the header pill
> — the most visible label the card has — and silently throws away a back dropdown this device is
> entitled to. Move `Type` to the header, slide `Character` to back-d1, and back-d2 is free.
> Full ruling (incl. that the honest knob count is **12** = 3 heroes + Mix + 8 back, not the "11"
> four bibles reconstructed four different ways): `FX-CHAIN-BIBLE.md` §7.1.

Dropdown 1 **Type**: `June · Pedal · Trio · Ensemble · Micro · Wow · Dark` (7 live)
Dropdown 2 **Character** (8 per Type — the tables in §2; the `CharBias` constant-row pattern).

⚠️ **fb342 law ① + the birth-cardinality law — SIZE FOR THE FINAL ROSTER ON DAY ONE.** A JUCE
`AudioParameterChoice`'s option count is **fixed at construction and is state-format-breaking to
change later**, and JUCE cannot create parameters at runtime at all (`FX-CHAIN-BIBLE.md`: the `+`
button can only claim a **pre-allocated slot**, never create one). Therefore:
* **Type is declared with 10 entries** — the 7 above plus three **reserved/disabled** slots
  (`Reserved 1/2/3`, greyed in the UI, `setType` clamps them to `June`). §11 Q1 asks whether to merge
  `Pedal`+`Dark`, §2.7 flags `Dark` as a cut candidate, and the Cut-candidates list names three more
  ideas — every one of those decisions is *irreversible after ship* unless the slots exist now.
  Declaring 10 costs nothing; declaring 7 and needing 8 costs every user's session.
* **Character is declared with 8 for every Type** (never per-Type cardinality) — §11 Q5's "trim to 6"
  is then a *table* decision, not a param decision: rows 7–8 become duplicates of row 1 with a
  disabled UI entry. Build the APVTS list, the UI list and the DSP table from **one shared table** so
  they cannot diverge (the P6 shared-slot boot trap).

| Slot | Knob | ID | Range / taper | Default | Role |
|---|---|---|---|---|---|
| P1 | **Time** | `SYN_CHR_TIME` | 0.5–40 ms, log | type base | Base voice delay (comb position; doubler at top) |
| P2 | **Detune** | `SYN_CHR_DETUNE` | 0–50 cents, x^1.4 | 9 (Micro) / 0 | Constant pitch split L+/R− (Micro's engine; adds static micro-shift to ANY type — never dead) |
| P3 | **Feedback** | `SYN_CHR_FB` | 0–100 % → **0–0.82** loop (§3.6; Micro 0–0.65) | 0 | Regeneration — comb bloom → near-flange wash |
| P4 | **Flutter** | `SYN_CHR_FLUTTER` | 0–100, fast-bank ms ±0.6 max | type | Fast vibrato bank (5–8 Hz) on top of Rate |
| P5 | **Drift** | `SYN_CHR_DRIFT` | 0–100, SmoothRandom ±2.5 ms max | 0 (Wow: 55) | Aperiodic wander — tape soul on any Type |
| P6 | **Color** | `SYN_CHR_COLOR` | 0–100 bipolar-ish: 50 = modeled BBD, 0 = murk (recon 1.8 k, grit ×8), 100 = studio-clean (16 k, poly off) | 50 | The whole §3.4 chain on one knob |
| P7 | **Low Keep** | `SYN_CHR_LOWKEEP` | 20 Hz–1 kHz, log (20 = off) | 20 (Micro: 250) | Crossover below which the signal stays DRY and CENTERED (MicroShift Focus + Dimension bass law) |
| P8 | **Phase** | `SYN_CHR_PHASE` | 0–180° | type (June 180) | L/R modulator phase — 0 = mono-thick, 180 = antiphase wide (the JUN-6 manual-mode width axis; distinct from M/S Width) |

Every knob passes the 0→100 evolution test: Time sweeps comb→doubler, Detune 0→50 is thickener→
out-of-tune 12-string, Flutter 0→100 is calm→nervous, Phase 0→180 audibly rotates width even in mono
(comb interleave), Color is a three-regime journey (murk→model→clean). No plateaus, no jargon names.

### 4.1 ⚠️ "Default: type base" is NOT a default — read this before wiring APVTS

Four rows above say `type base` / `type` / `0 (Wow: 55)` / `9 (Micro) / 0`. **A JUCE parameter has
exactly one default and switching Type must never write a param** (it would destroy the user's edits,
break undo, and fight automation — and the house `CharBias` pattern, `VintageReverb.h:337-339`, is
*coefficients only, no code branches, no writes*). The law:

> The APVTS default is a **single fixed number** (the values in the Default column are the *June* row).
> The per-Type figures are **table entries that reshape the knob's mapping**, never param writes:
> `value_ms = window_lo[type] + knob01 · (window_hi[type] − window_lo[type])`.

So `Time` at knob 0.5 means 3.5 ms on `June` and 20 ms on `Dark` **without the stored value changing**;
`Drift` at knob 0 is 0 everywhere, and `Wow`'s "55" is delivered by the *preset*, not by the Type.
Anything a Type genuinely needs at a different knob position belongs in a **factory preset** (§7).

### 4.2 Fixed defaults (one number each, as APVTS requires)

Rate 0.5 Hz · Depth 45 · Width 70 · Mix 35 · Time knob 0.5 (= type window centre) · Detune 0 ·
Feedback 0 · Flutter 0.25 · Drift 0 · Color 50 · Low Keep 0 (= 20 Hz, off) · Phase 1.0 (= 180°).

### 4.3 Per-Type knob liveness — the law-5 audit the first draft skipped

Law 5 says no dead knobs. `Micro` has **no LFO**, which would strand *four* controls; every one is
re-homed rather than greyed (a greyed knob on the fixed 4×2 chassis is a dead slot, which the chassis
does not allow):

| Knob | June/Pedal/Trio/Ensemble/Dark | Wow | **Micro** (no LFO) |
|---|---|---|---|
| **Rate** | LFO Hz | wow-stack Hz | **crossfade window** `T` = 60→20 ms (also the §3.5 artifact axis) |
| **Depth** | ms excursion | ms excursion | **delay wander** ±0…1.5 ms on a slow SmoothRandom (the `AMS` axis, live on all Characters) |
| **Flutter** | fast bank | flutter bank | **head-crossfade jitter** ±0…8 % of `T` (de-glitch character, the DMX tell) |
| **Phase** | L/R modulator opposition | modulator opposition | **L/R stagger ratio** 1.0→1.5× (the 8 ms / 12 ms base split becomes user-controllable) |
| **Detune** | static ±cents on top of the LFO | same | **the Type's engine** — 0→50 c |
| Time · Feedback · Drift · Color · Low Keep · Width · Mix | live on every Type | | |

`Pedal` is 1 line read twice, so `Phase` maps to its **read offset** 0→1.2 ms (0 = true mono CE-2,
max = wide two-amps) rather than to modulator opposition — same knob, same story, never dead.
**Harness gate:** every (Type × knob) cell must move the magnitude spectrum, spectral centroid,
spectral flux **or** the inter-channel correlation by a measurable amount across 0→100. 7 × 12 = 84
cells; that sweep is the ship gate.

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
  full chord raises side peaks ⟨estimated ~+4 dB — NOT measured; the harness must confirm before this
  number is quoted⟩; the limiter bounds it, nothing to fix), compander (Color < 50)
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
     one. **All six lines re-opened and confirmed verbatim 2026-08-14** — `PluginProcessor.cpp:7159`,
     `:7161`, `:7326`, `:7328`, `:7358`, `:7360` each read
     `((rvbSend? …) + (dlySend? …) + (dstSend? …)) * outputGain * kVoiceToFxPad` and each gains a
     `+ (chrSend? chrSend[i] : 0.0f)` term (the fb338 law: *every send bus joins every main-send
     exclusion*), and the new device's own exclusion line must sum the other three.
     📍 **These sums live in `PluginProcessor.cpp`, NOT in `index.html`.** Older session notes point at
     `index.html:6979` / `:7111`; those lines are the fb120 robin SVG and the ribbon-redesign CSS —
     unrelated. Do not go hunting there.
  2. 🛑 **`SYN_FX_ORDER` CANNOT BE EXTENDED IN PLACE — the old text's first option is illegal.**
     It is a **6-entry `AudioParameterChoice`** (`PluginProcessor.cpp:3488-3494`, strings
     `"Reverb > Delay > Distortion"` … `"Delay > Reverb > Distortion"`), read via `jlimit(0,5)` at
     `:5860`, dispatched by the 6-case switch at `:7383`. The old bullet offered "extend the choice
     list to 24 (state-compatible: old indices 0–5 map to the chorus-last block)". **A choice
     parameter's cardinality is fixed at birth (fb342);** growing 6 → 24 renormalises every stored
     value (VST3/AU persist choices as *normalised floats*: old index 3 of 6 = 0.60 reloads as index
     14 of 24), so every existing session's FX order silently changes. Legal options only:
     * **(a) Retire and replace.** Add a NEW id (`SYN_FX_ORDER2`) declared at the **final** roster
       size — 24 for four devices, or 120 if a fifth device is ever plausible (the FX-chain epic says
       it is) — and migrate on load: read the legacy `SYN_FX_ORDER` once, map 0–5 → the
       chorus-last block of the new list, then write `SYN_FX_ORDER2` and ignore the old id forever.
     * **(b) Per-device order slots.** One small int per device (`SYN_*_SLOT`, 0–7), sorted at block
       start. Grows to any device count with **no** cardinality change ever again — the only option
       that survives the multi-device chain epic, and the recommendation.
     Either way **decide before the first `SYN_CHR_*` param ships.**
  3. `IndyFxChain.h` owns private copies of the legacy chain — the NEW device does not live there
     (the rack chain is separate), but confirm the rack path the reverb/delay/dst inserts use and add
     the chorus insert-lambda to the same `SYN_FX_ORDER` permutation switch.
  4. **Latency: ZERO, and this is structural, not a preference.** `setLatencySamples` must never be
     called and no block of this device may look ahead. The fb305 exclusion sums above subtract the
     routed dry **sample-aligned**; any latency-reporting device in the rack makes the host delay-
     compensate the whole plugin while the subtracted dry stays un-delayed, so the dry leaks back
     **phase-smeared** — a comb you cannot null out. This kills lookahead limiting, linear-phase
     filtering and any "smart" transient-aware option **rack-wide**, not just here. Nothing in this
     device wants lookahead anyway (the compander is a feedback/feedforward pair, §3.4). State it in a
     code comment next to the constructor so nobody re-adds it.
  5. **No parameter may be created at runtime.** JUCE/VST3/AU cache the parameter list at
     construction; a "+ another chorus" affordance in the future node chain must claim a
     **pre-allocated slot** from a fixed pool (`FX-CHAIN-BIBLE.md`, K slots × N params), never
     allocate. Nothing in §4 implies runtime creation — keep it that way when the Patcher lands.

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
* **Memory:** one power-of-two stereo buffer at ≥ 128 ms (§3.2) = **64 kB/instance at 48 kHz**, 128 kB
  at 96 k. Fits L2 comfortably; the reads are within ±105 ms of the write head so they stay hot.
* **The 4-head Micro option (§3.5, needed if Detune keeps its 50-cent ceiling):** 8 Hermite reads
  instead of 4 = +16 MAC/sample stereo — under 20 % on top of the worst-case line below, and Micro is
  otherwise the cheapest Type (no LFO, no fast bank, no random generators). Not a budget problem.
* **`sqrt` in the compander:** replace with one Newton step seeded from the previous sample's result
  (env is 5 ms-smoothed — the seed is always close); or table it. Budget line, not a redesign.
* UI telemetry: 14 floats/frame at 60 Hz on the existing push lanes — noise-level.

---

## 9. Pitfalls — the traps, collected

1. **⚠️ The legacy-chorus recycle traps (all re-read at fb345, §3.4/§6):**
   (a) **`WET_GAIN = 2.5` (`:20`, +7.96 dB, applied *inside* a tanh at `:118-119`)** — do not copy.
   Precise root cause (the old text's "amount² mixing" was hand-waved): `amount` is used **twice** —
   once as the modulation depth scaler (`:79`, `depthSamples = BASE·0.6·amount·fs/1000`) and once as
   the dry↔wet crossfade (`:122-123`). Two independent uses of the same control means the *perceived*
   effect grows ≈ quadratically, so the bottom half of the knob reads dead and someone reached for a
   fixed +8 dB instead of separating the two axes. **We separate them** (Depth ≠ Mix), so the hack has
   no reason to exist and must not be copied.
   (b) the tanh/sinh "compander" (`:161-183`) is a **static ×1.47 (+3.35 dB) trim** at −26 dBFS with
   zero level-dependence (§3.4) — and it tracks an `env` it never reads (`:163`; the file's own TODO
   at `:160` says so). Replace with the env-driven 2:1 anchored at REF 0.05.
   (c) `setParams` (`:49-63`) hard-sets LFO rates, recon cutoffs and drive per call — **no glide**;
   our engine glides everything (§3.9).
   (d) its buffer guard is **`jassert` only** (`:87-88`) — a release build falls through to the `%`
   wrap at `:93-96` and reads whatever is at the wrapped index (stale audio / the write head), i.e.
   metallic garbage rather than a crash. Clamp for real, and size the buffer per §3.2.
   (e) it is a **sine** LFO (`:152`) whose right channel is *also* rate-skewed ×1.07 (`:18`, `:56`),
   so the declared `RIGHT_PHASE_OFFSET = π` (`:19`) does **not** hold — the channels drift. It is not
   a Juno topology; only the constants are worth lifting.
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
   runs on every Character row, every preset. **And the inverse failure, which the first draft
   shipped:** a wet pair that is *pure side* (`w`, `−w`) has no mid, so `Width 0` outputs **silence**
   and the mono sum is **−∞ dB** — check `‖w_L + w_R‖ > 0` structurally, not by ear (§2.2, §3.8).
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
12. **Choice cardinality (fb342 ①):** the APVTS choice list, the UI list and the DSP table must be
    generated from ONE source of truth or a P6-style shared-slot boot trap returns — **and both lists
    must be born at their FINAL size** (Type = 10 slots, 7 live + 3 reserved; Character = 8 always).
    Cardinality is fixed at construction; growing it later renormalises every stored session value.
13. **The `SYN_FX_ORDER` break (§6.2)** — the 6-entry choice **cannot be extended**; replace it or move
    to per-device slots, and decide before the first `SYN_CHR_*` param ships.
14. **Triangle LFO at high Rate:** raw triangle's slope discontinuity at 20 Hz modulation puts a
    click-rate buzz on the delay derivative — the Juno's own LP'd triangle (I+II) is the fix: one-pole
    at 4× rate on the LFO output, always on above 5 Hz.
15. **Sizing the delay buffer from the base time alone** — the excursion clamp lets `Dark`'s R read
    reach ~105 ms from a 40 ms base (§3.2). Size from *worst read*, never from *max base*.
16. **Micro's dead-knob cliff** — `Micro` has no LFO, so Rate/Depth/Flutter/Phase have nothing to do
    unless they are re-homed (§4.3). Greying four of eight back knobs is not an option on a fixed 4×2
    chassis.
17. **A wet pair with no mid** — see §9.7 and §2.2: `Width 0` on a pure-side wet outputs silence and
    the mono sum nulls. Structural check, not an ear check.
18. **The crossfade comb on the micro-shifter** — two heads at equal gain are `r·T/2` apart and notch
    at `1/(r·T)`; at 50 cents / 40 ms that is 853 Hz (§3.5). Scale `T`, or go to 4 heads.

---

## 10. Hard-rule compliance checklist (laws 1–10, walked)

1. **Bus reality (−26 dBFS):** compander REF = 0.05 linear (§3.4); grit drive k spans 1–8 so program
   *reaches* the poly bend; noise floor −60 dB relative to REF not FS; unity-through gate at
   −26 dBFS pink (§6). No literature level copied anywhere.
2. **Chassis:** fb275 exact — 2 dropdowns (Type, Character) + 8 back knobs 4×2 + Mix = the house 11;
   front 3 hero + pills on top; 23 APVTS ids total (§4). Pragmatic Title-case names; no jargon
   (`Low Keep`, not "Crossover HPF"; `Color`, not "Reconstruction LP").
3. **Time params:** Sync pill spans 4 bars → 1/256 (§4, §9.11).
4. **Mix 100 % = fully wet** (processor-owned equal-power); Type/Character switches fade-swap-recover,
   never cut (§3.9).
5. **Params evolve 0→100:** per-knob evolution statements (§4); **per-Type liveness matrix + the 84-cell
   sweep gate (§4.3)** — the law-5 check that must pass before ship; Types each carry a measured
   discriminator (§2, "the family tell") **plus explicit pairwise twin gates for June↔Dark and
   Pedal↔Dark (§2.7)** — law 5 cuts both ways, and `Dark` is the Type on probation; locked-rate
   Characters keep Rate alive as a scaler.
6. **Nothing free-runs:** noise env-gated (§3.4); feedback tail dies with input (loop < 1 §3.6) and the
   loop input is env-gated too; max stable loop gain stated: **0.82 knob ceiling from 1.097 in-loop
   small-signal gain (1.035 interp × 1.060 grit), with BOTH the expander and the de-emphasis outside
   the tap point** (§3.6). Micro 0.65.
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

0. **⚠️ NEW, and it blocks everything: `SYN_FX_ORDER` replace-or-reslot (§6.2).** The old plan to
   "extend the choice list to 24" is not legal — a choice param's cardinality is fixed at birth and
   growing it renormalises every stored session. Pick **(a)** a new final-size id with a one-time
   migration, or **(b)** per-device order slots (recommended — the only one that survives the
   multi-device chain epic). Nothing can ship until this is picked.
1. **Type list lock:** 7 live Types as proposed — **declared in a 10-slot choice** so the roster can
   move later (§4). In particular: `Dark` is on probation (§2.7 twin gate vs `June`); and keep `Pedal`
   AND `Dark`, or merge into one BBD-pedal Type with Characters covering both? *(After the §2.2 fix
   these two are structurally further apart than before — Pedal is co-phase, Dark is antiphase.)*
2. **Dimension boundary:** confirmed that Dimension/SDD-320 ships in the future Hyper device, and
   this device deliberately has no "Dimension" Type (only its Low-Keep/wet-width lessons)?
3. **The 2nd pill:** `Sync` proposed. Alternative candidates: `Mono In` (JUN-6-style input summing)
   or `Duo` (tap doubling). Sync felt most Serum-parity. Your call.
4. *(Superseded — this is now Q0 above. The old wording offered "extend to 24 permutations", which
   §6.2 shows is not a legal option; the choice is replace-with-migration vs per-device slots.)*
5. **Character count:** 8 per Type = 56 voicing rows to tune. Trim to 6/Type to cut the sweep matrix?
   (The 23-mode distortion sweep says budget ~2 sessions for 56.) ⚠️ Whatever you pick, the
   **declared cardinality stays 8** (§4) — trimming means duplicate rows + disabled UI entries, never
   a smaller choice list. Same for Q1's Type roster: 10 declared slots regardless.
6. **Detune ceiling:** 50 cents is past "chorus" into "broken 12-string" at the top — but §3.5 shows
   it is an **artifact** question first: on 2 heads, 50 cents parks the crossfade comb's first notch
   at **853 Hz** (honky). Options: (a) keep 50 c and build the **4-head** reader above 25 c (~8 extra
   reads, still trivial per §8), (b) cap at 25 c (notch ≥ 1.7 kHz on 2 heads), (c) keep 50 c on
   2 heads and accept the coloration as "Detune's own dramatic top end". Recommendation: **(a)**.
7. **Preset count:** 13 sketched — enough for launch, or match the delay's larger bank?
8. **Viz pick:** Voice Orbits + faint comb curtain composite (§5.2) — approve direction before the
   card mockup? (Mockup will be interactive + audible per the fb296 law.)
9. **Juno `I+II` rate:** pendragon's **9.75 Hz** or Anwander's **1 Hz** (§1.3 — the sources disagree
   10×, and it is a *character-defining* number). We ship 9.75 pending an ear A/B.

---

## 12. Verification log — what was actually checked, and what was not (audit pass 2026-08-14)

**Re-opened in the repo at fb345 and confirmed line-exact:** `TerrainChorus.h` `:14 :18 :19 :20 :42
:49-63 :56 :79 :87-88 :93-96 :118-123 :129-143 :146-153 :152 :160-183 :189-219`; `DelayEngine.h`
`:55 :122-125 :135 :138 :291 :315 :324 :330`; `TapeMachines.h` `:64 :87 :106 :215-255 :484 :571-600`;
`VintageReverb.h:337-349`; `DistortionEngine.h:207 :226-228 :231-236`; `ParameterIDs.hpp:374-401`;
`IndyFxChain.h:30 :89 :116 :130 :193 :259-261 :282`; `PluginProcessor.cpp:3455-3459 :3486 :3488-3494
:5860 :6300-6301 :7159 :7161 :7326 :7328 :7358 :7360 :7383-7391`; `index.html:1492-1660 :5446-5481
:9147-9149 :9320-9322`. **Confirmed greenfield:** `grep -rn SYN_CHR Source/` returns nothing.

**Re-fetched from the source of record:**
* pendragon-andyh/Juno60 `Chorus/README.md` — 1 triangle LFO, 2 × 256-step BBDs, right modulation
  inverted 180°, ~70 kHz sampling, 0.513/0.863/9.75 Hz, 1.66–5.35 ms, 3.3–3.7 ms. **All confirmed.**
* florian-anwander.de/roland_string_choruses — Juno 2× MN3009 ✓; RS-202 3× MN3002, 6 LFOs as three
  pairs of 6.25 Hz + 0.66 Hz at 120° ✓; SDD-320 2× MN3007, 7.5/10 ms, ±1.5–2.5 ms, 0.25/0.5 Hz,
  ~80 Hz HP, compander ✓. **Surfaced the I+II rate conflict (§1.3).**
* Soundtoys MicroShift manual v5 — Focus = 2-band crossover, wet on the **high band only**, default
  20 Hz, max 10 kHz ✓; Detune/Delay are **percentages of a continually time-varying** amount
  (**corrected §1.7**); Styles I/II/III = H3000 #231 / H3000 #519 / AMS DMX 15-80 ✓.
* Wikipedia, *Bucket-brigade device* — **MN3007 = 1024 stages** (corrects §1.2); MN3208 = 2048 stages
  ≈ 102 ms at a 10 kHz clock, which **validates the `delay = N/(2·f_clk)` formula** in §1.1.

**Still ⟨UNVERIFIED⟩ — do not quote as fact:** the MN3009 stage count (256 vs 128); the MN3002's 512
stages; Serum 2's chorus voice/tap count and its 0–20 Hz Rate range; the UAD Tri-Stereo Chorus
0.03–7.45 Hz manual range; the Eventide TriceraChorus ±40-cent detune spread; the Arturia JUN-6
"Depth default 4.44 ms"; the H3000 #231/#519 internals; the Raffel/Smith poly coefficients
`a = 1/8, b = 1/18` (the paper was not re-read this pass — the formula's *shape* and DC behaviour were
checked algebraically, the fit constants were not); the SDD-320's polarity-inverted cross-mix
(Anwander confirms cross-mixing, not the polarity inversion); "±4 dB side-peak rise at Width 100 +
Depth 100" in §6 (an estimate, not a measurement); every "measured on the UAD recreation" figure.

---

## Appendix A — Recycle inventory (verified by reading, not assumed)

*(Every line number below was re-opened and confirmed on 2026-08-14 at fb345. Corrections from the
first draft are marked ✎.)*

* **`TerrainChorus.h`** (234 lines) — `hermite4()` `:129-143` ✓ (lift verbatim); `advancePhase`
  `:146-153` ✓ (**sine**, not triangle — `:152`); the antiphase constant `RIGHT_PHASE_OFFSET = π`
  `:19` ✓ + right-rate skew `1.07` `:18` ✓ (the `Wide 106` Character number — but see §9.1e: the
  skew *defeats* the offset in the original); ReconLP 4th-order Butterworth pair `:189-219` ✓ (usable,
  though the engine's per-sample one-pole tracking law §3.4 supersedes it); buffer sizing constant
  `BBD_BUFFER_SAMPLES = 4096` `:14` ✓ — **do not reuse this number, it is too small for us (§3.2)**.
  ⚠️ Do NOT copy: `WET_GAIN 2.5` `:20` ✓, the static tanh/sinh "compander" `:161-183` ✓, unglided
  `setParams` `:49-63` ✓, the `jassert`-only guard `:87-88` ✓ (§9.1).
* **`DelayEngine.h`** (361 lines) — THE engine contract to mirror (`:37-97` lifecycle/setters,
  `processSample` `:138` wet-only, `reset` `:64`); `softClip` `:315` ✓; `onePole(hz)` `:324` ✓;
  **the BBD clock-darkening law ✎`:122-125`** (`bbdHz = 5200 − 2900·tnorm`, i.e. 5.2 kHz → 2.3 kHz
  over 0–600 ms — the earlier "`:125`" pointed at the `onePole()` call, the law itself is `:124`)
  **+ its per-sample application `:291`** ✓; companding follower coefficient idiom `:135` ✓;
  per-sample glide idiom (`smCoef` ~15 ms) ✎**`:55`** (was cited as `:57`) — all reusable as-is.
  ⚠️ `DelayEngine::flush` (`:330`) is a **denormal flusher** (`|x| < 1e-20 → 0`), *not* the
  smoother-snapping `flush()` §3.1 asks for — that pattern is `DistortionEngine.h:226-228`.
* **`TapeMachines.h`** (1204 lines) — `SmoothRandom` `:215-255` ✓ (hold-and-slew: phase accumulator +
  uniform target + one-pole smoothing — **exactly** the "band-limited walk, not white-noise-through-a-
  one-pole" the Worn-walk law demands; Drift, verbatim); the Cassette triple-LFO wow stack ✓ **confirmed
  verbatim at `:484` and `:571-600`**: *"Primary 0.6Hz ±2.0ms, Secondary 2.2Hz ±0.8ms, Flutter 7Hz
  ±0.4ms"*, primary = triangle (capstan eccentricity), secondary + flutter = sine — Wow's
  deterministic bank; `DCBlocker` `:64` ✓ (the mandated 10–20 Hz spec); `OnePoleLP` `:87` / `OnePoleHP`
  `:106` ✓.
* **`VintageReverb.h:337-349`** ✓ — `struct CharBias` (`:338`, 12 float scalars) +
  `static constexpr CharBias CHAR[8]` (`:339-348`): the Character voicing-table pattern — **coefficients
  only, no code branches, and crucially no parameter writes** (§4.1). Copy the pattern for the
  10-slot × 8-Character chorus table.
* **`DistortionEngine.h`** (3035 lines) — the deferred char-fade + re-seat machinery: `:207`
  (`chrPend_ = chr_; dipT_ = 1.0f;` — "a flush cancels any armed char fade") ✓ and `:226-228` ✓
  (*"Snap every smoothed value to its target so a flush is silent, not a ramp from zero"*) — the §3.9
  fade-swap-recover implementation precedent; `setMode` `:231-236` shows the dip-on-type-switch idiom.
* **✎`ParameterIDs.hpp:374-401`** (was `:374-394`) — the `SYN_DLY_*` block: clone the grammar
  (TYPE `:374` / CHARACTER `:375` / SYNCDIV `:376` / front + back knobs `:377-388` / SRC `:389-394` /
  SYNC `:395` / PING `:396` / **POWER `:397`** / HQ `:398` / TIME_R, SYNCDIV_R, LINK `:399-401`) as
  `SYN_CHR_*`. The old range stopped at `:394` and so excluded `POWER`, which the same sentence
  claimed to include.
* **`PluginProcessor.cpp`** — ✎`:6300-6301` `kVoiceToFxPad = 0.5f` — this is the **−6 dB pre-FX pad**,
  *one contributor* to the −26 dBFS bus figure, **not "the −26 dBFS proof"**; the −26 dBFS program
  level is the measured house number (see the distortion bible's root-cause measurement), and `:46`'s
  `kInstrumentMakeup` commentary is the surrounding gain-staging story. `:7159/:7161/:7326/:7328/`
  `:7358/:7360` ✓ the six exclusion sums that each gain a `+ chrSend` term. `:3488-3494` the 6-entry
  `SYN_FX_ORDER` choice, `:5860` its `jlimit(0,5)` read, `:7383-7391` its 6-case switch — **to be
  replaced, not extended** (§6.2). `:3486` `SYN_DLY_POWER` default `false` = the house FX power-OFF
  precedent. `:3455-3459` the 20-entry sync list (`Free`, `4 bar` → `1/256`) = the resolver to reuse
  for `Sync` (house law 3 ✓ verified in code, not assumed).
* **UI** — `Design/fx-rack-v7-CANONICAL.html` + `fx-back-panel-mockup.html` (frozen chassis, do not
  re-author); `TIC.presets`/`.pmenu` glass for the preset menu (bullet `•`, name-first save); the
  echo-timeline renderer (fb312) as the Warp Ribbon base; the filter live-analyzer band probe for the
  comb curtain tint; the legacy chorus block (✎`index.html:1492-1660` CSS ✓, `:5446-5481` markup
  (`id="fxPageChorus"` — its own FX page), `:9147-9149` param meta ✓, `:9320-9322` juceIds ✓) stays
  untouched.
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
* Wikipedia, *Bucket-brigade device* — MN3007 = **1024 stages**, MN3005/MN3205 = 4096, MN3207 = 1024,
  MN3208 = 2048 (≈102 ms at a 10 kHz clock — the datapoint that validates `delay = N/(2·f_clk)`):
  https://en.wikipedia.org/wiki/Bucket-brigade_device
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
