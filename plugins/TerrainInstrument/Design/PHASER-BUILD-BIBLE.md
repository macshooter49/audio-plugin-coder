# Terrain Instrument — Phaser Build Bible

**Status: RESEARCH COMPLETE — ready for Max's read + mockup pass.**
Researcher: dedicated Phaser agent, 2026-08-14. A builder must be able to implement the device from
this file alone. House style and depth bar: `Design/DISTORTION-BUILD-BIBLE.md`.

The one-line pitch: **a phaser is 2–16 multiplies of DSP wearing fifty years of mythology.** The
allpass cascade is nearly free — every dollar of this device's budget goes to *voicing*: the sweep
laws, the feedback geography, the stagger, the motion sources, and a card that shows the comb
actually breathing. Serum 2 ships a five-knob phaser with one LFO. We ship nine circuits.

---

## 0. Scope decision (proposed, mirrors the distortion precedent)

**ONE device, 9 Types, 8 Characters each**, on the locked fb275 chassis. Not two devices
(no separate "vibe" or "barberpole" plugin-lets) for the same reason distortion is one device:
the families share one shell — *N allpass stages + a motion source + a feedback loop + a mixer* —
and only the stage law, the motion law and the loop nonlinearity change per Type.

Explicitly **out of scope** (they are separate future devices in the Serum 2 menu we're chasing):
**Flanger** (delay-line comb, not allpass comb — different physics, different bible), **Chorus**,
and **Bode/frequency shifter** (Serum 2 lists Bode separately; our Barber Type gets the barberpole
*illusion* without SSB, see §3.6 — if Max later wants a true Bode device, the Hilbert core belongs
there, not here).

**The scale of the competition, measured by param count:** Serum 2's phaser exposes Rate (sync),
Depth, Freq, Feedback, Phase (stereo LFO offset), Mix + per-FX Level — one topology, one LFO shape
[src: Serum manual / MusicRadar FX guide]. Soundtoys PhaseMistress — the genre reference — ships
2–24 stages (odd counts allowed), 69 Styles across the entire hardware canon, and five modulation
sources (LFO/Rhythm/Envelope/Random/Step + ADSR) [src: CDM PhaseMistress tour]. Our 9-Type ×
8-Character × bipolar-feedback roster out-guns Serum by an order of magnitude and lands in
PhaseMistress territory with a live visualizer neither of them has.

---

## 1. History and circuits — the lineage that defined the effect

Every loved phaser is one of six circuits. Learn the six, and every Style menu ever shipped
(PhaseMistress's 69, Arturia's BI-TRON, Eventide's Instant Phaser) collapses into recombinations.

### 1.1 The studio rack: Eventide Instant Phaser (1971)
The first studio phaser — 8 stages, built to fake tape flanging without two tape machines. Modes
Shallow/Deep/Wide (Wide = *different* sweep depth per channel = pseudo-stereo), and four sweep
sources: Manual / Oscillator / Envelope / Remote. The Mk II plugin adds an `Age` knob (component
drift) and a sidechain input for the envelope. Lesson: **the motion source is a first-class
selector, not a fixed LFO** — we inherit this via the Touch/Grip axis (§4).

### 1.2 The JFET pedal: MXR Phase 90 (1974)
Four identical first-order allpass stages, each `R=24 kΩ, C=47 nF` on the fixed side with a
2N5952 JFET as the swept resistor; triangle LFO ("tenths of a Hz to some Hz"); output mixes dry
and wet 50/50 through equal 150 kΩ resistors. ElectroSmash calculates the two notches at
**58.5 Hz and 340.8 Hz** at one LFO instant — note the ~2.5-octave notch spacing from *identical*
stages (spacing comes from the phase law, not from stagger, §3.2). **Script vs Block:** the
script-logo original has *no* feedback resistor; the block reissue adds `R28=24 kΩ` regeneration,
which puts a mid hump between the notches. That one resistor is the most argued-about tone
difference in pedal history and is exactly one Character bit for us. Because all four stages move
together from one LFO and JFET Vgs cutoffs scatter (−1.3…−3.5 V), real units sweep slightly
unevenly — Character voicings model the scatter as per-stage offset noise.

### 1.3 The OTA pedal: EHX Small Stone (1975)
Four CA3094 OTA allpass stages (a CA3080-style core with an onboard buffer — Aion's Sunstone
doc) **plus a dedicated extra feedback stage**; the `Color` switch turns
up feedback *and* depth simultaneously. OTAs clip softly when driven — the Small Stone's "watery"
deep sweep is partly loop saturation. Lesson encoded in our `Color` knob (§4): in-loop tone+drive
is a *voicing* control, not distortion for its own sake.

### 1.4 The lamp: Shin-Ei Uni-Vibe (1968) and Schulte Compact Phasing A (1971)
- **Uni-Vibe:** 4 stages with **staggered caps 0.015 µF / 0.22 µF / 470 pF / 4.7 nF** — the stages
  are deliberately NOT aligned, so notches never form a tidy harmonic comb; one incandescent lamp +
  LDRs sweeps them with the lamp's thermal lag. Chorus/Vibrato switch = mix 50/50 vs **wet-only**
  (wet-only = the ear reads the swept phase as pitch wobble). The stagger is why a Uni-Vibe
  "throbs" instead of "swooshes".
- **Schulte Compact Phasing A** — the krautrock unit (Schulze, Tangerine Dream, Kraftwerk): bulbs +
  LDR banks around ~8 stages; LDR attack/decay asymmetry skews the sweep (fast rise, slow fall) and
  the resonance peaks wander because LDR tracks mismatch. ChowPhaser (open source, our structural
  reference) models it as: LDR law `R = R0 · x^(−λ)`, a **skewed LFO** warped by that same curve,
  **8 first-order allpass sections** where the first 2 sit inside a feedback loop with gain G and
  all 8 form the modulation path, plus a *nonlinear biquad* in the feedback path.

### 1.5 The dual: Mu-Tron Bi-Phase (1977) → Arturia Phaser BI-TRON
Two independent 6-stage phasors, two sweep generators, series or parallel routing, per-phasor
depth (from BOTH sweep gens — a 2×2 mod matrix), feedback per phasor, pedal input with envelope
follower. Arturia's BI-TRON extends each phasor to 12 poles, gives each sweep gen 3 waveforms +
tempo sync, and adds an HP filter. **The dual-sweep beat** — two combs drifting through each other
at |f₁−f₂| — is a sound no single-LFO phaser can make and is our `Duo` Type's discriminator.

### 1.6 The synth rack: Moog MF-103 (2000)
6 or 12 stages (3 or 6 notches), Sweep over a **6-octave range**, big Resonance, and the detail
everyone forgets: the LFO's Hi range reaches **250 Hz** — *audio-rate phaser FM*, which NI Phasis
later re-sold as `Ultra` mode. An Aux out carries a second, opposite-phased output for stereo.
Our `Twelve` Type keeps the audio-rate top end because law #5 says max = just past useful.

### 1.7 The illusions: Bode's barberpole and the DAFx-15 recipes
Harald Bode built a phaser whose notches rise (or fall) forever — the Shepard–Risset illusion in
filter form. Esqueda/Välimäki/Parker (DAFx-15) publish three implementable recipes; we use their
**Method 1** (cascaded time-varying notch bank with raised-cosine depth window) because it needs
no Hilbert transform and no delay line — full math in §3.6.

---

## 2. Types — the 9-entry Type dropdown

House law 5: each Type must be night-and-day with a **measurable discriminator**, or be cut.
Names are pragmatic (what it does / whose myth it carries), Title-case, no trademark strings.

| # | Type | Lineage | Engine recipe (delta from shared core §3) | Measurable discriminator (harness gate, §8 of DST bible style) |
|---|------|---------|-------------------------------------------|----------------------------------------------------|
| 1 | **Ninety** | MXR Phase 90 script/block | 4 identical 1st-order stages, triangle LFO, fb 0 (script chars) or +0.35 (block chars), per-stage JFET scatter ±4% | Exactly 2 notches 20 Hz–20 k; notch-spacing ratio ≈ 5.8:1 (58.5→340.8 Hz law); THD < 0.1% |
| 2 | **Stone** | EHX Small Stone OTA | 4 stages + loop tanh (Color pre-wired ≈ 35), hypertriangle LFO (rounded), fb default +0.55 | 2 notches + inter-notch peaks ≥ +6 dB above unity; loop THD 0.3–1% at −26 dBFS program |
| 3 | **Duo** | Mu-Tron Bi-Phase / BI-TRON | TWO 6-stage cascades in series (Characters switch to parallel), LFO B = LFO A × ratio {1.00, 1.33, 0.75, golden 1.618…} per Character | 6 notches whose pairwise distance oscillates at \|f₁−f₂\|; spectral-crossing count > 0 per cycle (single-LFO types = 0) |
| 4 | **Twelve** | Moog MF-103 | 12 identical stages, sine LFO, resonance emphasized (fb range full ±0.95), Character `Hi Range` maps Rate knob top to 250 Hz | 6 notches; at Rate > 20 Hz, sidebands at f ± n·f_LFO ≥ −40 dBc (FM discriminator — no other type may produce them) |
| 5 | **Kraut** | Schulte Compact Phasing A | 8 stages, **skewed LFO** (LDR warp `u^(1/(1+2s))`), lamp-lag 1-pole (τ ≈ 40 ms) on the sweep, nonlinear biquad in loop (ChowPhaser topology) | Sweep asymmetry: rise/fall time ratio ≥ 2:1 measured on notch-1 trajectory; notch spacing warps ≥ 15% across the cycle |
| 6 | **Vibe** | Shin-Ei Uni-Vibe | 4 stages **staggered per the 0.015 µF/0.22 µF/470 pF/4.7 nF caps** (breaks ∝ 1/C ⇒ ≈ 0.62×, 0.042×, 19.7×, 1.97× of the geometric center), lamp-lag LFO | Notches inharmonic: f_notch2/f_notch1 ≠ any single-stagger law within ±10%; at Mix 100% (wet-only) pitch deviation ≥ ±8 cents (vibrato tell) |
| 7 | **Barber** | Bode / DAFx-15 Method 1 | M cascaded 2nd-order notch EQs, octave-spaced, sawtooth center sweep + raised-cosine depth window; `Flip` pill relabels **Down** | Spectrogram notch trajectories strictly monotonic (one direction, no return) over ≥ 3 cycles; cycle-wrap click ≤ −60 dBFS |
| 8 | **Envy** | Mu-Tron/Instant-Phaser envelope mode | Sweep driven by input envelope follower (Touch/Grip axis promoted to the front of the voice); LFO depth still available on top | Sweep correlates with program envelope: r ≥ 0.9 between env(t) and notch-1 freq (log domain); with silence, notches PARK (no motion) |
| 9 | **Steps** | S+H stepped phasers (Maestro-era myth, modular practice) | Sweep = sample-and-hold source clocked by Rate (sync divisions!), sources per Character: random, 8-step quantized ladder, shift-register melodies; 5 ms glide between holds | Notch-1 trajectory is piecewise-constant: ≥ 90% of frames within ±2% of a held value; step timing locks to host grid when Sync on |
| — | ✂️ CUT: “Ultra” as a Type | NI Phasis | folded into `Twelve`'s Hi-Range Characters | a rate range is not a circuit — fails the night-and-day law as a standalone Type |

**Character dropdown (8 per Type, must change PHYSICS not EQ — the fb345 law):** worked examples:
- Ninety: `Script 74` (fb 0) · `Block 78` (fb +0.35) · `Matched JFETs` (scatter 0) · `Loose Batch`
  (scatter ±9%) · `Slow Lamp` (adds 25 ms sweep lag) · `Two Stage` (2 stages, 1 notch — the minimal
  phaser) · `Six Stage` (6) · `Negative` (fb −0.35 — peak geography flips, §3.4).
- Duo: `Series 1:1.33` · `Series 3:4` · `Parallel 1:1.33` · `Parallel Golden` · `Counter` (LFO B
  inverted) · `Wide Duo` (phasor B only on R channel) · `Slow B` (ratio 0.25) · `Cross Feed`
  (phasor A feedback tapped from phasor B output — loop gain law applies to the PAIR, §3.4).
- Steps: `Random 8` · `Random 16` · `Ladder Up` · `Ladder Down` · `Pendulum` · `Register`
  (Turing-style 8-bit loop, 1/16 mutate) · `Drunk` (random walk ±1 step) · `Trance Gate`
  (alternates Floor↔ceiling).
Full 9×8 grid is a build-phase task; the bible locks the *pattern*: Characters re-wire stage
count / stagger / LFO shape / loop topology — never just a tone control.

---

## 3. DSP core — algorithms, math, param laws, stability, oversampling verdict

### 3.1 The stage — recycle `TerrainFilters.h` verbatim

The entire linear engine already exists in-tree and is certified by the filter system:

- `TerrainFilters.h:878` `struct AllpassStage` — first-order allpass, one multiply:
  `y = g·(x − y1) + x1; x1 = x; y1 = y;`
- `TerrainFilters.h:891` `struct PhaserCore` — **MAXST = 16 stages**, geometric stage spread
  around a center (`fratio 1.5`), coefficient `g = (t − 1)/(t + 1)`, `t = tan(π·f_k/fs)`,
  feedback `fb = res·0.9` with one-sample loop delay (`fbState`), tanh input drive, output
  `0.5·(in + v) · makeup · 1.45` and a transparent soft limiter `4·tanh(0.25·out)`.
- Already deployed as filter types `Phaser 4P/8P` (`PluginProcessor.cpp:1666-1667`) and
  `Phaser 6P/12P/16P` (`PluginProcessor.cpp:1720-1722`).

**The device is NOT the filter re-badged** — the FX device adds motion sources, stereo, the
feedback geography, Types/Characters, and the card. But the inner loop is this exact code,
promoted into a new `PhaserEngine.h` that mirrors `DelayEngine.h`'s API so it drops into the rack:
`prepare(double) · clamped setters · processSample(inL,inR,outL&,outR&) · flush()`, wet-only out,
processor owns Mix (the `DistortionEngine.h:20-24` contract).

Digital coefficient (JOS, *Physical Audio Signal Processing*): the bilinear-transform pole is
`p = (1 − tan(ω_b·T/2)) / (1 + tan(ω_b·T/2))` — algebraically identical to the in-tree
`g=(t−1)/(t+1)` up to sign convention. Clamp `f_k ∈ [20 Hz, 0.45·fs]` as PhaserCore already does.

### 3.2 Notch count and spacing laws (1st order)

With 50/50 mix and cascade phase `Φ(ω) = Σᵢ φᵢ(ω)`, `φᵢ = −2·atan(f/f_bᵢ)` (analog form):

```
|H(ω)| = |cos(Φ(ω)/2)|          → notches where Φ = −(2m+1)π,  m = 0…⌊N/2⌋−1
```

- **N first-order stages ⇒ ⌊N/2⌋ notches** (4→2, 6→3, 8→4, 12→6, 16→8). Odd N leaves a
  half-turn dangling — a shelf-like tilt at Nyquist, PhaseMistress ships odd counts for exactly
  that slightly-wrong charm; we allow odd `Stages` too.
- **Identical stages (Ninety):** all breaks at f_b ⇒ notches at `f_b·tan((2m+1)π/(2N))`.
  For N=4: tan(π/8)=0.414, tan(3π/8)=2.414 ⇒ notch ratio 5.83:1 — matching ElectroSmash's
  measured 58.5/340.8 Hz (ratio 5.83). **The spacing is a property of the phase law, not of
  component stagger.** This is the §2 Ninety discriminator's origin.
- **Geometric stagger ratio r (in-tree default 1.5):** breaks at `f_c·r^(k−(N−1)/2)` spread the
  notches toward log-uniform. Our `Spread` knob sweeps `r` continuously `1.0 → 4.0` (t taper
  `r = 1 + 3t²`): r=1 collapses to the tight Ninety cluster, r≈1.5 = house PhaserCore, r→4
  approaches Vibe-style inharmonic scatter. **No dead zone anywhere on the knob** — notch
  geography visibly re-spaces from the first degree (law 5).
- **Vibe fixed stagger** overrides Spread's uniform law with the measured cap ratios
  (§2 row 6); Spread then scales the *scatter about* that pattern.

### 3.3 Second-order stages — where Barber lives, and the Notches law

A 2nd-order allpass drops 2π per stage ⇒ **exactly one notch per stage, AT its center f_k**, width
set by its Q. That is how you place notches *independently* (NI Phasis' `Spread`/`Notches` axes).
We use 2nd-order sections only for `Barber` (§3.6) — the LFO Types keep 1st-order cascades because
their slightly-coupled, unevenly-widening notches are the analog sound. (This answers the
mandate's "1st vs 2nd order" question: 1st = character, 2nd = precision; we ship both, keyed by
Type.)

### 3.4 Feedback — polarity, where resonance blooms, and the LOOP GAIN LAW

Feedback k wraps the whole cascade with one sample of loop delay (in-tree `fbState` pattern):

```
v[n] = drive(x[n]) + k · A{v}[n−1]         wet = v after cascade
```

`|A(ω)| = 1` (allpass), so the small-signal loop gain **is k** and stability is exactly `|k| < 1`.
- UI `Feedback` ±100 → `k = sign·0.95·t²` (squared taper — the drama lives past 60, law 5's
  "movement is the magic"). **Hard cap |k| ≤ 0.95** (house margin under the 0.97 theoretical
  ceiling; in-tree PhaserCore caps 0.90 — we raise it because the FX device has the soft limiter
  and the AC-coupler, see below).
- **LOOP GAIN LAW (house law 6) accounting** — every gain stage inside the loop:
  `g_loop = k × |A|(=1) × colorLP(≤1) × sat'(0)`. The `Color` drive is `tanh(g_d·x)/g_d` — the
  `1/g_d` makeup lives INSIDE the loop so `sat'(0)·makeup = 1` and cranking Color can never push
  the loop over k. Duo `Cross Feed` Characters route A's tap through B: the product of both taps
  must obey the same 0.95 — clamp at the setter, not the UI.
- **Polarity geography (the bipolar knob's payoff):** closed loop `W = A/(1 − k·z⁻¹A)` peaks where
  the loop phase ≈ 0 (mod 2π).
  - `k > 0`: loop phase 0 falls where cascade phase = 2mπ ⇒ **peaks midway BETWEEN the notches**
    — the classic block-Phase-90 mid-hump "vowel".
  - `k < 0`: adds π ⇒ **peaks sit AT the k=0 notch frequencies**, narrowing and deepening the
    notches on either side — hollow, "phase-cancelled honk" (Small Stone down-Color territory).
  Discriminator: sweep Feedback −100→+100 and the peak centroids shift by half the notch spacing.
  This is audible night-and-day and free — one sign bit.
- **DC TRAP (Phase G's DC-latch class, pre-empted):** a 1st-order allpass has H(DC)=+1, so the
  loop passes DC with gain k → at k=0.95 any offset is amplified ×20 and *latches*. **A 10 Hz
  one-pole high-pass INSIDE the loop is MANDATORY** (AC-coupled feedback everywhere — the fb345
  law, applied at birth, not discovered in certification). Same pole doubles as the Floor HP's
  fixed lower bound.
- Nothing free-runs (law 6): |k|<1 means the loop rings only while fed; silence in ⇒ silence out
  — the resonant ring decays ~20·log10|k| dB per loop transit (transit ≈ the cascade's group
  delay, roughly 1/notch-spacing), a short tail, never sustained. No env-gate needed, verified
  by the harness silence metric (fb345 probe-craft: bias axes need a SILENCE metric — here,
  feedback axes do).

### 3.5 Motion — sweep laws, tapers, glides

One **shared phase accumulator** per device (the fb342 ONE-CLOCK LAW: rate changes glide the
increment, never the phase — phase accumulators integrate glide skew, so L/R read the SAME
accumulator with an offset, never two clocks):

```
sweep01[n] = shape(phase + stereoOffset_ch) · depth · lag()   → f_c(n) = Floor…9 kHz log map
f_k(n) = f_c(n) · r^(k−(N−1)/2)                                → g_k via tan law, per sample
```

- **Rate:** free `0.01 → 20 Hz`, log taper. Sync ON (front pill, default ON): the delay's 20-entry
  division list, **4 bars → 1/256** (house law 3; reuse the `SYN_DLY_SYNCDIV` list verbatim,
  `ParameterIDs.hpp:376`). `Twelve`'s Hi-Range Characters re-map the top decade to 250 Hz.
- **Depth:** 0→100 ⇒ excursion `±4.5·t^0.8` octaves around Sweep center, clamped to
  [Floor, 0.45·fs]. At 100 the sweep slams both rails — just past useful (law: NO PLAYING SAFE).
- **Sweep (center):** 40 Hz → 9 kHz, log. With Depth 0 this is the *manual phaser* — Serum's
  "Rate 0 trick" as a first-class knob, and the mod-matrix destination.
- **Shapes** are Type property, not a knob: triangle (Ninety), rounded hypertriangle (Stone —
  `tri − 0.15·tri³`), sine (Twelve/Duo), LDR-skew (`Kraut`: `u' = u^(1/(1+2·skew))` on the rising
  half only — measured-style rise/fall asymmetry), sawtooth (Barber), S+H (Steps).
- **Lamp lag:** Kraut/Vibe put a one-pole (τ = 25–60 ms per Character) after the LFO —
  the incandescent-bulb thermal integrator. It is also the free de-zipper for Steps' 5 ms glide.
- **Envelope (Touch/Grip)**, live on every Type, the whole voice of Envy: peak-tracking follower
  with attack 1→60 ms / release 30→600 ms (Grip scales both, log). **BUS REALITY (law 1):**
  the follower normalizes against the measured −26 dBFS program: `env01 = clamp(env / 0.05, 0, 1)`
  (0.05 lin ≈ −26 dBFS), so Touch at ±100 swings ±4 octaves on *our* bus, not on a literature bus
  26 dB away. Never copy a hardware sensitivity range.
- **Per-sample smoothing:** g_k moves every sample from the smoothed f_c (15 ms one-pole, the
  `DelayEngine.h` `xC += (xT−xC)·smth` idiom). `tan()` per stage per sample is affordable (§8) —
  but use the same guard as PhaserCore (clamp before tan). Knob params (Spread, Feedback, Color,
  Floor) glide 15 ms; Stages and Type crossfade (§9.3).

### 3.6 Barber — the DAFx-15 Method-1 notch bank, exact

Esqueda/Välimäki/Parker, DAFx-15 (full paper archived in Sources):

- `M` cascaded parametric-EQ **cut** biquads (Orfanidis form, eq. 5 of the paper), centers one
  interval apart: `f_c(m,k) = f0 · 2^([K(m−1)+k−1]/K)` with `K = ⌊Fs/ρ⌋` steps per cycle,
  ρ = cycle rate. Paper reference build: `M=10, f0=20 Hz, Q=15 constant-Q, ρ=0.1 Hz`.
- Depth window (Shepard's raised cosine, inverted): gain at each center
  `L_c(m,k) = Lmin + (Lmax − Lmin)·(1 − cos θ(m,k))/2`, `θ = 2π(K(m−1)+k−1)/(MK)`,
  paper values `Lmax = −20 dB (mid-band), Lmin = −3 dB (edges)` — notches are born shallow at one
  spectral edge, deepen through the middle, die shallow at the other edge. That window IS the
  illusion.
- **Constant Q**, not constant Δω, so notch width is uniform on the log axis
  (`β = sqrt((G_B²−1)/(G²−G_B²))·tan(Δω/2)`, `G_B² = (1+G²)/2` — paper eqs. 6/8; with that
  arithmetic-mean G_B the radical collapses to 1, so in practice `β = tan(Δω/2)` with
  `Δω = 2π·f_c/(Q·Fs)` per eq. 7).
- **The wrap click (paper §2, our Pitfall #7):** when filter m wraps from k=K to k=1 it must
  **inherit the state variables of its neighbor**, or every cycle end clicks. The paper says so
  explicitly; the fix is a state handoff, not a fade.
- Our mapping: `Notches` (Stages knob relabel) = M ∈ 4…12 · `Spread` = interval 0.4…1.6 oct ·
  `Depth` = Lmax −6…−32 dB · `Rate` = ρ (best < 0.3 Hz — the paper's audibility bound — but the
  knob runs to 20 Hz because destruction is allowed; past ~1 Hz it reads as a strange chatter,
  "just past useful" is ~0.5) · `Flip` pill (relabelled **Down**) mirrors the sawtooth.
- Verdict on the other two DAFx-15 methods: dual-flanger needs delay lines (that's the future
  Flanger device); SSB/Bode needs a Hilbert pair and collides with Serum's separate Bode slot —
  both rejected here, noted for the record.

### 3.7 Oversampling verdict — **NONE. Anywhere. Ever.**

The cascade is LTI between coefficient updates — it creates **zero** new spectral content; the
only nonlinearities are (a) the Color loop tanh, bounded, at most ~1% THD by design, and (b) the
output soft limiter, transparent at program level. Alias products of a ≤1%-THD tanh at −26 dBFS
program sit below −80 dBFS — inaudible under the distortion bible's §3.10 perceptual gate.
Audio-rate sweep (Twelve Hi) modulates coefficients, which scatters sidebands like ring-mod FM —
that scatter IS the documented MF-103/Phasis-Ultra sound; oversampling would only make it
politer (and Bitcrush precedent says: the artefact is the effect, do NOT oversample it).
**The entire CPU budget goes to voicing (§8), exactly as the mandate predicted.**

---

## 4. Chassis map — fb275, 11 params, pragmatic names

Grammar mirrors `SYN_DLY_*`/`SYN_DST_*` (`ParameterIDs.hpp:374-421`). New prefix: `SYN_PHZ_*`.

### 4.1 Front card — 3 knobs + Mix + 2 pills (the dst §5.3 pattern)

| Control | ID | Range → law | Glide |
|---|---|---|---|
| **Rate** | `SYN_PHZ_RATE` (+ `SYN_PHZ_SYNCDIV` choice when Sync on) | free 0.01–20 Hz log; sync **4 bars → 1/256** (delay's 20-entry list) | increment glides 30 ms; phase NEVER jumps (one-clock law) |
| **Depth** | `SYN_PHZ_DEPTH` | 0–100 → ±4.5·t^0.8 octaves | 15 ms |
| **Sweep** | `SYN_PHZ_SWEEP` | 40 Hz → 9 kHz log center; the manual/mod-matrix axis | 15 ms |
| **Mix** | `SYN_PHZ_MIX` | equal-power, **100% = FULLY WET** (= Vibe's vibrato mode for free — no extra pill, no doubled control) | equal-power ramp |
| pill **Sync** | `SYN_PHZ_SYNC` | bool, default ON | — |
| pill **Flip** | `SYN_PHZ_FLIP` | wet-polarity invert (comb turns upside-down — notches↔peaks); **relabels `Down` on Barber** (direction), per-type pill relabel precedent = dst family pills (`index.html:7625`) | 20 ms fade-through |

Plus the standard non-counted booleans: `SYN_PHZ_POWER` (default **OFF**, fb303),
`SYN_PHZ_SRC_A…SRC_NOISE` routing pills (main-send when none lit — fb305 grammar).

### 4.2 Back panel — 2 dropdowns + 8 knobs (4×2)

Dropdowns: **Type** (`SYN_PHZ_TYPE`, 9 — §2) · **Character** (`SYN_PHZ_CHARACTER`, 8 per Type).
Both fade-swap-recover (law 4/7; the fb344 deferred-fade + re-seat law applies verbatim).

| Pos | Knob | ID | Range → law (taper) | What it DOES (tooltip voice) |
|---|---|---|---|---|
| P1 | **Feedback** | `SYN_PHZ_FB` | ±100 → k = ±0.95·t² | resonance; sign moves the peaks between ↔ onto the notches |
| P2 | **Stages** | `SYN_PHZ_STAGES` | stepped 2–16 (odd allowed); Barber relabel `Notches` 4–12 | more stages = more notches; crossfade on change |
| P3 | **Spread** | `SYN_PHZ_SPREAD` | 0–100 → stagger r = 1+3t² (Barber: 0.4–1.6 oct interval) | how far apart the notches sit |
| P4 | **Stereo** | `SYN_PHZ_STEREO` | 0–100 → 0–180° LFO offset R vs L | widens; 100 = counter-sweeping channels |
| P5 | **Touch** | `SYN_PHZ_TOUCH` | ±100 → env→sweep ±4 oct (env normalized to −26 dBFS program, §3.5) | playing louder pushes the sweep up (or down) |
| P6 | **Grip** | `SYN_PHZ_GRIP` | 0–100 → atk 60→1 ms, rel 600→30 ms (log, coupled) | how fast Touch grabs and lets go |
| P7 | **Floor** | `SYN_PHZ_FLOOR` | 20 Hz → 1 kHz log; sweep lower bound + matching 6 dB/oct wet HP | keeps the wobble out of the bass |
| P8 | **Color** | `SYN_PHZ_COLOR` | 0–100 → in-loop LP 18 k→1.2 kHz + loop drive 0→+18 dB (makeup inside loop) | darkens and dirties the resonance; 100 growls |

Every knob audible 0→100, no plateaus; Feedback/Depth/Color carry the drama past 60 by taper
design. **Glides:** all continuous back knobs ride the 15 ms one-pole (§3.5); **Stages** is the
one stepped knob — each detent is a 40 ms equal-power crossfade (§9.5), and both dropdowns
fade-swap-recover (fb344). Every knob moves the card (§5). Signature-knob relabel machinery already exists
(`index.html:7625` front relabel; `:8247/:8251` per-type back rebuild — reuse, do not reinvent).

---

## 5. Visualizers

### 5.1 How the greats show a phaser (survey, mechanism-precise)

- **NI Phasis** — the genre's best viz: a horizontal log-frequency strip; animated bright blobs =
  resonant peaks and dark gaps = notches slide left/right with the LFO in real time; `Notches`
  adds blobs, `Spread` re-spaces them, `Feedback` brightens/sharpens them. It renders the FILTER
  STATE, not the audio — informative but not audio-reactive (idle it keeps dancing).
- **Serum 2** — each FX panel carries a small animated graphic tied to its params (their design
  language: the panel moves when the sound moves — level slider per FX). Exact phaser-panel
  content could not be verified from the manual/press sweep — **flagged in §11 for Max's
  screenshot**, since Max bases card mockups on visualizer research.
- **Kilohearts Phaser** — essentially knobs-only (Cutoff/Depth/Rate/Order/Spread/Mix); honest
  minimalism, nothing to steal except the param set's brevity.
- **Arturia BI-TRON** — skeuomorphic hardware: twin sweep-generator lamps blink at each LFO's
  rate; the panel teaches the dual-clock idea by making you watch two heartbeats drift.
- **Soundtoys PhaseMistress / Eventide IP MkII / Strymon Zelzah** — a single rate LED. The
  hardware answer: the *sound* is the display.
- **ChowPhaser** — a small skew-curve plot: shows the LFO's LDR warp shape, static.

Verdict: nobody puts the live comb ON the live spectrum. Phasis animates the filter but ignores
the audio; the skeuomorphs ignore both. That gap is our card.

### 5.2 Our card — three concepts (canvas, CPU-cheap, laws fb311 + everything-visible)

**A. THE BREATHING COMB (recommended core).** One canvas: live input spectrum as a dim area fill
(recycle the filter live-analyzer FFT feed — the [[terrain-instrument-filter-live-analyzer]]
plumbing, same push lane), with the *closed-form* |H(f)| curve stroked on top, computed per frame
in JS from the same param state the engine uses: 128 log-spaced bins × N stages of
`φ = −2·atan(f/f_k)` (JS `Math.atan`, ~2k calls/frame ≈ nothing), feedback bloom approximated by
`|W| ≈ |cos(Φ/2)| / (1 − k·cos(Φ))`. Wet-path curve modulates the fill: spectrum bins are
*carved* where notches sit — the audio visibly gets eaten. Idle = dim flat line (fb311: obvious
delta); playing = bright, comb chews the fill. Every param maps: Rate = motion itself, Depth =
excursion width, Feedback = peaks lift above the 0 dB rule, Flip mirrors the curve vertically,
Stereo draws L and R combs as two offset strokes, Touch visibly yanks the comb with your playing,
Color tints + slumps the HF end, Floor draws the no-fly zone. No shadowBlur, no per-frame
filters (fb342 session law ④).

**B. NOTCH RAIN (the Barber/Steps teller — composite layer behind A).** A 2.5 s scrolling ribbon:
offscreen canvas shifted 1 px/frame, current comb column painted at the right edge (darkness =
notch depth × local input energy — silent input paints nothing, law fb311). LFO Types draw
S-curves, Kraut draws shark-fins (skew visible!), Barber draws parallel diagonals that never turn
around, Steps draws staircases, Duo draws two interleaved weaves. This single mechanism makes
FIVE Type discriminators visible at a glance. rAF laws from the echo-timeline apply (no
early-return, no data↔DOM sort — fb312/313).

**C. ORBIT DOTS (cheapest fallback / small-card state).** One glowing dot per notch riding a log-f
axis line, dot size = notch depth × band energy at that frequency, 300 ms motion trails via
alpha-fade compositing. Reads at 90 px tall; candidate for the collapsed-card state.

Ship: **A + B composited** (both are one canvas each, < 0.5 ms/frame combined), C for collapsed.

---

## 6. Interplay — the device in the chain

- **Unity-through discipline:** at defaults (Depth 35, Feedback 0, Color 0, Mix 35) broadband
  program passes at −0.4 ± 0.5 dB (equal-power mix of a unity-gain wet path; the in-tree 1.45
  level-match trim already calibrated this for PhaserCore — keep it, re-measure at cert). At
  Feedback ±95 peaks can add up to +9 dB narrowband — the output soft limiter
  (`4·tanh(0.25·x)`, in-tree) bounds it transparently; state in the harness: unity gate applies
  at DEFAULTS, not at max resonance (max is allowed to be loud, not allowed to clip the bus).
- **Spectrum downstream:** a phaser is a moving comb EQ — it creates no new energy (Color ≤1%
  THD aside) but *modulates spectral tilt*. Reverb after phaser = the room inhales/exhales
  (classic, gorgeous). Phaser after reverb = the whole tail combs — the "phased room" 70s trick;
  both orders are legitimate, which is why the device joins `SYN_FX_ORDER` (§10.4).
- **Dynamics downstream:** near-zero crest change at Feedback 0. High +Feedback narrows crest
  (resonant ring sustains); a compressor after will pump on the resonance — document in tips,
  not code.
- **With Distortion:** phaser → distortion = the notches barely survive (a saturator flattens
  spectral contrast — same physics as the "saturator-after-mix erases mix" law class);
  distortion → phaser = deep vowel sweeps on the harmonic wall. Default rack order should place
  Phaser AFTER Distortion. Classic pedalboard wisdom agrees (phaser late, before delay/reverb).
- **Stacked with itself / Duo:** two combs in series multiply |H| — notches deepen where they
  align and beat where they don't; that's the Duo Type *inside* one device, so users rarely need
  two instances.
- **Mono-sum:** Stereo=100 (counter-sweep) collapses badly — L notch fills R notch and the effect
  audibly vanishes in mono (it's the price every stereo phaser pays). The card's dual-stroke comb
  makes it visible; the manual states it; default Stereo = 25.

---

## 7. Presets — 14 factory sketches (front/back in knob units)

| # | Name | Type · Character | Sketch |
|---|---|---|---|
| 1 | First Phaser | Ninety · Script 74 | Rate 1/2 bar sync, Depth 45, Sweep 350 Hz, Mix 35, FB 0, Stereo 20 — the reference tone, ships as device default |
| 2 | Seventies Strut | Ninety · Block 78 | Rate 1/4, Depth 60, FB +45, Color 15, Mix 40 — funk rhythm chops |
| 3 | Deep Stone | Stone · Color Up | Rate 1 bar, Depth 80, FB +70, Color 45, Floor 120 Hz, Mix 50 — the wooshy pad eater |
| 4 | Twin Orbit | Duo · Parallel 1:1.33 | Rate 2 bars, Depth 65, Spread 55, Stereo 60, Mix 45 — combs drifting through each other |
| 5 | Six Notch Scream | Twelve · Full Range | Rate 1/8, Depth 90, FB +88, Color 30, Mix 60 — resonant acid sweep |
| 6 | Sideband Engine | Twelve · Hi Range | Rate knob 85 (≈ 90 Hz), Depth 40, FB +60, Mix 55 — FM-ish clangor from a phaser (the Ultra myth) |
| 7 | Kosmische Bus | Kraut · Slow Bulbs | Rate 4 bars, Depth 75, FB +55, Color 60, Stereo 35, Mix 45 — the Tangerine Dream pad |
| 8 | Throb | Vibe · Chorus Lamp | Rate 4.8 Hz free, Depth 55, Mix 50, Floor 80 Hz — the university of wobble |
| 9 | Leslie Liar | Vibe · Vibrato Lamp | Rate 6.5 Hz free, Depth 45, **Mix 100** (wet-only = pitch wobble), Stereo 45 |
| 10 | Up Forever | Barber · Rise 8 | Rate 0.15 Hz free, Notches 8, Spread 1.0 oct, Depth 70, Mix 45 — the infinite staircase |
| 11 | Down Stairwell | Barber · Fall 10 + Flip/Down | Rate 0.25 Hz, Notches 10, FB +30, Mix 55, Color 40 — descending dread |
| 12 | Auto Quack | Envy · Fast Grab | Touch +85, Grip 75, Depth 20, Sweep 500 Hz, FB +50, Mix 60 — the envelope-phaser funk duck |
| 13 | Rubber Down | Envy · Slow Sink | Touch −70, Grip 30, FB −60, Mix 55 — notes sag the comb downward; negative-FB hollow |
| 14 | Clockwork | Steps · Register | Rate 1/16 sync, Depth 70, Spread 40, FB +65, Mix 60 — melodic S+H phaser lock-step |

Preset LAW reminders from Phase G: check the level spread across all 14 at cert (the
Sludge/Gargle ±28 dB class), and every preset must make sound the moment a note plays (P6
shared-slot boot trap: verify each preset's Type/Character pair boots audible).

---

## 8. CPU — budget and tiering

Measured-analog arithmetic (worst case, 48 kHz stereo):
- 16 first-order stages × 2 ch: 32 mul + 64 add ≈ **~100 flops/sample ≈ 0.005 GFLOP/s** — under
  0.1% of one core. The mandate's premise is confirmed by counting: **allpass chains are nearly
  free.**
- Per-sample coefficient path: 16 × tan() — the real cost. Options: (a) straight `std::tan` ≈
  16×~20 cycles = still < 1%; (b) house rule says no transcendentals in the loop
  (DST bible §4.6) → a 512-entry `g(log2 f)` LUT with linear interp, one lookup per stage —
  choose (b), it's 20 lines and makes audio-rate `Twelve Hi` free.
- Barber: 10 biquads × 2 ch = 50 MAC ≈ nothing; the per-step center/gain table is precomputed
  per cycle (the paper's K-step table — memory a few kB).
- Envelope follower, LFO, lamp lag: scalar one-poles, negligible.
- Viz: §5 concepts ≤ 0.5 ms/frame on the UI thread, zero audio-thread cost beyond the existing
  spectrum push lane.
- **Quality dropdown: NONE.** No oversampling (§3.7), no quality tiers — the device runs
  identically everywhere. Sleep rule: adopt the fb342 control-head sleep (awake-head idiom from
  dst) — when POWER off or fully dry + input silent for 0.5 s, skip the block.
- Total budget claim for cert: **< 0.5% single core** at 16 stages + Color + viz push,
  i.e. cheaper than one reverb slot by an order of magnitude. Spend nothing; voice everything.

---

## 9. Pitfalls — the traps, collected

1. **Zipper on g:** coefficients must move from the 15 ms-smoothed f_c per SAMPLE (not per block)
   or fast Depth×Rate combos graunch. The LUT (§8) makes this affordable. (No-clicks law.)
2. **DC latch in the loop (fb345 class):** H(DC)=+1 ⇒ k·DC recirculates. The 10 Hz in-loop HP is
   MANDATORY; without it, +0.95 Feedback turns any converter offset into a ×20 latch and the
   Phase-G silence class returns. AC-couple at birth.
3. **Feedback blowup by composition:** Color drive pre-gain inside the loop without its 1/g_d
   makeup multiplies loop gain ×8 at Color 100 ⇒ instant scream. Makeup INSIDE the loop (§3.4);
   clamp composite loop gain ≤ 0.95 at the setter (Duo Cross Feed too).
4. **Denormals:** 16 recirculating allpass states + fbState on decaying tails — classic trap.
   `juce::ScopedNoDenormals` + `flush()` idiom from `DelayEngine.h` on every state.
5. **Stage/Type/Character switch clicks:** allpass states are wrong for the new topology. Never
   hot-swap: run old+new cascades in parallel for a 40 ms equal-power crossfade, then park the
   old (the dst §6.5 pattern + fb344 deferred-fade/re-seat law). Stages is a *stepped knob* —
   the choice-param cardinality law (fb342 ①) applies: each detent is a crossfade, and rapid
   scrubbing coalesces to the final value.
6. **State re-seed on bypass/power (ADAA-history analog):** on re-entry, seed each stage's
   x1/y1 with the first input sample, never 0 (`SubOsc.h:63-82` precedent) — else the first
   block is a comb of onset spikes.
7. **Barber wrap click:** hand the wrapped filter its neighbor's state variables (DAFx-15's own
   warning, §3.6) — a fade does NOT fix it, the illusion breaks audibly every cycle.
8. **Mono-sum collapse at Stereo=100** (§6): document, default 25, show both combs on the card.
9. **The one-clock law (fb342):** ONE phase accumulator; L/R/dual-LFO read it with offsets/ratio
   multiplies. Two accumulators + rate glide = the DIGITAL Spread bug all over again.
10. **Env follower chatter:** at Grip fast + bass-heavy program the follower tracks cycles of the
    waveform (~audio-rate sweep = accidental FM). Slew-limit the sweep line to 1 oct/ms — keeps
    Envy's grab fast but kills the buzz. (fb345 probe-craft: use an AM probe to certify, static
    sines will miss it.)
11. **Serum-range plagiarism (law 1):** any threshold copied from pedal literature (env
    sensitivities in volts, drive in "10×") lands 26 dB wrong on our −26 dBFS bus. Every level
    in this bible is already restated relative to the measured bus; keep it that way.
12. **Viz honesty:** Phasis-style param-only animation violates fb311 (it dances while idle).
    Every bright pixel must be gated by program energy (§5.2's energy terms are not optional).

---

## 10. Hard-rule compliance checklist (laws 1–10, walked)

1. **BUS REALITY (−26 dBFS):** env follower normalized at 0.05 lin (§3.5); Color drive stated as
   loop-relative dB with in-loop makeup (§4.2); unity gate measured at bus level (§6). ✅
2. **CHASSIS fb275:** 2 back dropdowns (Type, Character) + 8 back knobs 4×2 + front 3+Mix+2 pills;
   full 11-param map with pragmatic Title-case names (§4). ✅
3. **TIME PARAMS:** Rate sync spans **4 bars → 1/256** via the delay's existing 20-entry list. ✅
4. **MIX 100% = FULLY WET** (equal-power; wet-only IS the Vibe vibrato); Type/Character switches
   crossfade 40 ms, never cut (§9.5). ✅
5. **PARAMS EVOLVE 0→100:** tapers place drama past 60 (Feedback t², Depth t^0.8, Spread 1+3t²);
   no plateaus; every Type has a stated measurable discriminator or was cut (Ultra was cut). ✅
6. **NOTHING FREE-RUNS / LOOP GAIN LAW:** loop gain enumerated stage-by-stage
   (k × |A| × LP × sat'·makeup), max stable 0.95 declared; silence in ⇒ decays out; env-driven
   Types park on silence. ✅
7. **NO CLICKS:** per-sample g glide, 15 ms knob glides, phase-continuous rate, crossfade swaps,
   barber state handoff, re-seed on re-entry (§9). ✅
8. **CPU:** < 0.5% core, no oversampling anywhere (verdict §3.7 with the perceptual argument),
   LUT for tan, control-head sleep; the budget is spent on voicing by design (§8). ✅
9. **AUDIBLE ⇒ VISIBLE + DRAMATIC:** the Breathing Comb + Notch Rain map every one of the 11
   params to a visible change and gate all brightness on program energy (§5.2). ✅
10. **RECYCLE FIRST (verified by reading, with lines):** `AllpassStage`/`PhaserCore`
    (`TerrainFilters.h:878/891`) · `fastTanh` (`TerrainFilters.h:42`) · smoothing idiom
    (`DelayEngine.h`) · engine API contract (`DistortionEngine.h:20-24`) · sync-division list
    (`ParameterIDs.hpp:376`) · relabel machinery (`index.html:7625/:8247/:8251`) · spectrum push
    lane (filter live analyzer) · fb305 exclusion lines (`PluginProcessor.cpp:7159/7326/7358`) ·
    `kVoiceToFxPad` (`PluginProcessor.cpp:6300`). ✅

### 10.4 The wiring trap inherited from fb305/fb338 (do this IN THE SAME COMMIT)

A fourth send bus (`phzSendL/R`) re-breaks the exclusion sums unless
`+ (phzSendL ? phzSendL[i] : 0.0f)` joins **all three** existing exclusion lines
(`PluginProcessor.cpp:7159, 7326, 7358` — "EVERY send bus joins EVERY main-send exclusion") AND
the phaser's own main-send branch gets the symmetric four-way subtraction. Also `SYN_FX_ORDER`
(`PluginProcessor.cpp:3488`) is a 6-entry permutation choice for 3 devices; 4 devices = **24
permutations** — either extend the choice list (state-compat: first 6 entries must preserve
existing indices, the fb341 raw-=-index law) or graduate to the insert-lambda reorder grammar
from the delay arc. Decision flagged in §11.

### Build order (dst §7 style)

1. `PhaserEngine.h` shell: PhaserCore promoted, stereo, one-clock motion block, Ninety only,
   front four knobs — audible end-to-end behind `SYN_PHZ_*` + POWER.
2. fb305 four-way exclusion + FX_ORDER extension (the §10.4 commit — isolated, reviewable).
3. Feedback geography (bipolar, AC-coupled loop, Color loop) + harness gates for §2 rows 1-2.
4. Types 3–6 (Duo/Twelve/Kraut/Vibe) — all cascade variants, one Type per commit, discriminator
   test each.
5. Barber (notch bank is a separate inner engine behind the same API) + Steps + Envy.
6. Characters 9×8 + presets ×14 + level-spread cert.
7. Card viz A+B, mockup-in-Safari-first (house mockup law), then wire.
8. Solo certification sweep (the Phase-G ritual: per-family harness + silence metrics + click
   floors + preset boot audit).

---

## 11. Open questions for Max

1. **Serum 2's phaser panel screenshot** — the sweep-visualizer survey (§5.1) could not verify
   what Serum 2 actually draws on its phaser FX panel. One screenshot from your copy settles the
   mockup baseline (and whether the Breathing Comb visibly outguns it — I believe it does).
   (Audit 2026-08-14 re-tried: the Serum 2 manual is not fetchable online — the screenshot
   really is the only path.)
2. **9 Types or 8?** `Envy` (envelope circuit) vs folding envelope-drive into every Type via
   Touch/Grip and cutting the Type. I kept it because Serum can't do an auto-quack at all and
   the Type slot advertises it; your call on menu length.
3. **Flip pill double-duty** (invert wet ↔ Barber direction): OK per the relabel precedent, or
   would you rather Barber direction live in Character (4 up / 4 down voicings)?
4. **FX_ORDER at 24 permutations** — extend the dropdown, or is this the moment the rack gets
   drag-to-reorder (the insert-lambda grammar already supports it)?
5. **Audio-rate top (Twelve Hi to 250 Hz):** keep as Characters (my rec) or promote a front
   `Ultra` pill Phasis-style?
6. **A tenth Type — true SSB barberpole** (Bode's own method, §3.6): cut here to keep Bode
   territory clean. Confirm you want Bode as its own future device (it's in your Serum 2 menu
   screenshot), else I fold an SSB voicing into Barber's Characters.
7. **Vibe wet-only:** happy with Mix-100 as the vibrato mode, or do you want a dedicated
   `Solo` pill on Vibe (costs the Flip slot on that Type)?

---

## 12. Sources

**Repo (read, cited by line):**
`Source/TerrainFilters.h` (878 AllpassStage, 891 PhaserCore, 42 fastTanh) ·
`Source/ParameterIDs.hpp` (345-423 device grammar, 376 sync list) ·
`Source/PluginProcessor.cpp` (1666/1720 phaser filter types, 3488 FX_ORDER, 6300 kVoiceToFxPad,
7159/7326/7358 fb305 exclusions) · `Source/DistortionEngine.h` (API contract + laws header) ·
`Design/DISTORTION-BUILD-BIBLE.md` (house style, §4.5/4.6/5.3 patterns) ·
`Source/ui/public/index.html` (7625/8247/8251 relabel machinery).

**Circuits & history:**
- ElectroSmash, *MXR Phase 90 Analysis* — https://electrosmash.mas-effects.com/mxr-phase90.html
  (mirror; primary https://www.electrosmash.com/mxr-phase90)
- R.G. Keen, *The Technology of Phase Shifters and Flangers* —
  http://www.geofex.com/article_folders/phasers/phase.html
- R.G. Keen, *The Technology of the Univibe* — http://www.geofex.com/article_folders/univibe/univtech.htm
- Aion FX, *Sunstone (EHX Small Stone) project history* — https://aionfx.com/project/sunstone-ota-phaser/
- General Guitar Gadgets, *Small Stone Information* — https://generalguitargadgets.com/effects-projects/phase-shifters/small-stone-information/
- Synthtopia, *The Schulte Compact Phasing A* — https://www.synthtopia.com/content/2021/01/22/the-schulte-compact-phasing-a-aka-the-krautrock-phaser/
- Cytomic, *MXR Phase 90 JFET-based Phaser* — https://cytomic.com/mxr-phase-90-jfet-based-phaser/
- Eichas et al., *Physical Modeling of the MXR Phase 90* (DAFx-14) —
  https://dafx.de/paper-archive/2014/dafx14_felix_eichas_physical_modeling_of_the_.pdf

**DSP:**
- J.O. Smith, *Physical Audio Signal Processing* — Phasing / First-Order Allpass / Classic VA
  Phase Shifters — https://ccrma.stanford.edu/~jos/pasp/Phasing_First_Order_Allpass_Filters.html ·
  https://ccrma.stanford.edu/~jos/pasp/Classic_Virtual_Analog_Phase.html
- Esqueda, Välimäki, Parker, *Barberpole Phasing and Flanging Illusions* (DAFx-15) —
  https://www.dafx.de/paper-archive/2015/DAFx-15_submission_67.pdf (full PDF read; eqs. 1-8
  re-verified against the archive copy 2026-08-14)
- Kiiski, Esqueda, Välimäki, *Time-Variant Gray-Box Modeling of a Phaser Pedal* (DAFx-16) —
  https://www.dafx.de/paper-archive/2016/dafxpapers/05-DAFx-16_paper_42-PN.pdf
- Carson et al., *Differentiable Grey-box Modelling of Phaser Effects* (DAFx-23) —
  https://www.dafx.de/paper-archive/2023/DAFx23_paper_38.pdf
- Jatin Chowdhury, *Under the Hood of ChowPhaser* — https://jatinchowdhury18.medium.com/under-the-hood-of-chow-phaser-a950de9677c5
  · code https://github.com/jatinchowdhury18/ChowPhaser
- Will Pirkle lineage (NSC six-stage 16 Hz–20 kHz) via https://github.com/bradhowes/SimplyPhaser
- KVR DSP, *Lin/Exp control of allpass phaser* — https://www.kvraudio.com/forum/viewtopic.php?t=437372

**The greats (params/UX):**
- Soundtoys PhaseMistress — https://www.soundtoys.com/product/phasemistress/ · CDM complete tour
  https://cdm.link/free-soundtoys-phasemistress-guide/
- Arturia Phaser BI-TRON manual — https://dl.arturia.net/products/phaser-bi-tron/manual/phaser-bi-tron_Manual_1_1_EN.pdf
  · overview https://www.arturia.com/products/software-effects/phaser-bi-tron/overview
- Moog MF-103 manual — https://www.manua.ls/moog/12-stage-phaser-mf-103/manual
- Eventide Instant Phaser Mk II — https://www.eventideaudio.com/plug-ins/instant-phaser-mk-ii/
- Strymon Zelzah — https://www.strymon.net/product/zelzah/ ·
  https://www.strymon.net/secondary-functions-zelzah-multidimensional-phaser/
- NI Phasis manual — https://www.native-instruments.com/fileadmin/ni_media/downloads/manuals/PHASIS_Manual_English.pdf
- Kilohearts Phaser docs — https://kilohearts.com/docs/snapins
- Xfer Serum manual (v1 PDF; Serum 2 phaser panel pending Max's screenshot) —
  https://s3.amazonaws.com/decembercymatics/Serum_Manual.pdf
- Valhalla DSP, *The Inspiration for PhaserDDL* — https://valhalladsp.com/2021/04/26/the-inspiration-for-phaserddl/
