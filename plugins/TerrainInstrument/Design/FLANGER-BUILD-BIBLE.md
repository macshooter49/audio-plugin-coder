# Terrain Instrument — Flanger Build Bible

*Research bible for the 4th FX-rack device. Written 2026-08-14 against the tree at fb345.*
*House laws honored throughout: the −26 dBFS bus reality, the fb275 chassis (2 dropdowns + 8 knobs
= 11 params), 4-bar→1/256 synced time, Mix 100 % = fully wet, params evolve 0→100, nothing
free-runs, no clicks, CPU-first, everything audible is visible, recycle first.*
*Style and depth bar: `Design/DISTORTION-BUILD-BIBLE.md`. A builder must be able to implement the
device from this file alone.*

---

## 0. The scope decision

**One Flanger device, six Types.** Not a "modulation" device (chorus/phaser/flanger in one) — Serum 2
ships Flanger, Phaser, and Chorus as three separate FX-menu entries, and so do we. Phaser (allpass
chains) and Chorus (long-delay ensemble) are *different machines* with different param sets; jamming
them into one device forces the exact param-soup the distortion bible's family system exists to avoid.
The flanger owns the **0.05–40 ms modulated-comb territory**, and inside that territory it goes deeper
than anyone: through-zero tape flanging (Serum 2 does NOT have it — this is a headline differentiator),
barberpole, envelope-driven, and stepped flanging alongside the classic jet and BBD pedal voices.

Chorus territory (>20 ms centers, multi-voice ensembles, no feedback identity) is explicitly OUT —
that is the future Chorus device. The `TerrainChorus.h` engine in-tree stays untouched; we steal its
*components* (§Appendix A), not its role.

**Why the flanger earns flagship treatment:** Serum 2's flanger is 5 params and no through-zero. Every
$99-tier competitor treats the flanger as a checkbox. A flanger with real TZF (u-he Satin / Strymon
Deco territory), a real barberpole (DAFx-certified algorithm), and an envelope mode (A/DA / Bel BF-20
territory) is a device reviewers screenshot. It is also the **cheapest flagship we will ever build** —
the entire device is two fractional delay reads and a handful of one-poles (§9).

---

## 1. History and circuits — the lineage

### 1.1 Tape flanging (1966–1975): the effect is a STUDIO ACCIDENT, not a circuit

Two tape machines play identical program; their outputs are summed; the engineer presses a thumb on
one reel's flange. The delta delay sweeps from 0 up to ~10 ms and back. Sound On Sound's teardown of
the Record Plant technique (Hendrix *Electric Ladyland*) gives the real numbers:

* Fixed (reference) machine path delay ≈ **0.66 ms**; the varispeed machine dipped to ≈ **0.62 ms**
  at the LFO peak — i.e. the moving machine passes **through and beyond** the fixed one. That
  crossing is **through-zero flanging (TZF)**: at Δ = 0 with one path polarity-inverted, the entire
  spectrum cancels — the famous "tape sucking the sound into a hole" on *Itchycoo Park* (Small
  Faces, 1967) and *Bold as Love* (Hendrix).
* Authentic sweep LFO ≈ **0.45 Hz** — slow. Fast flanging is a pedal-era invention.
* The capstan motor cannot follow the control voltage instantly — **motor inertia** rounds every
  reversal and adds drift. Eventide's FL-201 "Instant Flanger" (1975) modeled this as **Bounce**:
  the servo overshoots and rings when the thumb lifts. This wobble is *why tape flanging sounds
  liquid and every naive DSP flanger sounds like a sewing machine*. It is a Type-defining param
  for us (§5, `Wobble`).
* Two sub-flavors with names that survive today: **additive** (paths summed in phase — doubling
  that combs) and **subtractive** (one path inverted — the deep-null "hollow" flange).

### 1.2 The comb math (the whole effect in four lines)

Feedforward: `y[n] = a·x[n] + b·x[n − M(n)]` (JOS, *Physical Audio Signal Processing*, "Flanging").

* Delay τ = M/fs. **Additive** (b > 0): notches at `f = (2k+1)/(2τ)`, peaks at `f = k/τ`.
  **Subtractive** (b < 0): notches at `f = k/τ` — including **DC**, which is why subtractive flange
  sounds bass-hollowed and "watery".
* Notch spacing is `1/τ` Hz — **linear** in frequency, which is the audible fingerprint separating a
  flanger from a phaser (a phaser's few notches sit at allpass-determined, non-harmonic spots).
  τ = 1 ms ⇒ notches every 1 kHz (≈20 in band); τ = 10 ms ⇒ every 100 Hz (≈200 in band = the
  dense "metallic" zone).
* Add feedback `g` around the delay and peaks sharpen with gain `1/(1−|g|)`: g = 0.9 ⇒ +20 dB,
  g = 0.95 ⇒ +26 dB. **Positive** feedback boosts the harmonic peak series (bright, "jet");
  **negative** feedback shifts the emphasized series to the odd set (hollow, "underwater"). Both
  polarities are canonical voicings, not a gimmick.
* **Notch motion is essential** (JOS, verbatim). A static comb is EQ; the flange IS the sweep.
  Corollary for us: the visualizer must show the notches *moving* (§6), and Rate = 0 must still be
  a *playable* state (Manual knob = the Mistress Filter Matrix, §2.3).

### 1.3 The BBD pedals (1976–1984) — each one contributes a Type or a param

| Box | Chip / delay | What it taught us (and where it lands in our device) |
|---|---|---|
| **Electro-Harmonix Electric Mistress** (1976, David Cockerell) | SAD1024, sweep up to ≈ 20 ms | **Filter Matrix mode**: a switch disconnects the LFO — the comb freezes and the Range knob plays it by hand. The lesson: *the static comb is an instrument*. → our `Manual` knob is fully playable at Rate 0, every Type (§5). Also the archetypal "liquid" scoop: EHX band-limited the wet path hard around the BBD, so the flange rides *inside* a dark band — → `BBD` Type voicing |
| **MXR M-117** (1976) | SAD1024 (dual 512) | The four-knob grammar the whole industry copied: `Manual · Width · Speed · Regen` — literally our `Manual · Depth · Rate · Feedback` |
| **A/DA Flanger** (1977) | SAD1024A (reissue MN3010) | **The deep-sweep king: a 40:1 sweep ratio** where competitors did ≤ 20:1 — the sweep covers > 5 octaves of comb pitch, which is why an A/DA at slow speed sounds like a dive-bomber. → our exponential sweep law hits 40:1 at Depth 100 (§3.2). Its **even/odd Harmonics switch** = feedback polarity flip → our bipolar `Feedback`. Its **Threshold** gate → ancestor of our `Tail` env-gate |
| **Boss BF-2** (1980) | MN3207 + MN3102 clock, **1–13 ms**, LFO period 100 ms–16 s (the Boss spec sheet) | The "everyman jet". Its published delay span and rate range define our `Jet` Type's comfortable center. In-loop `Res` up to the edge of oscillation |
| **Bel BF-20** (late '70s, Mick Barnard) | SAD512, stereo | The *studio* flanger (Phil Collins, Yes, The Rolling Stones): **LFO + envelope-follower + manual CV, combinable** — the direct ancestor of Arturia's Flanger BL-20 plugin and of our `Envelope` Type |
| **Eventide FL-201 Instant Flanger** (1975) | BBD delay lines, studio rack | **Bounce** (servo-motor hunting), env follower with sidechain, ±Depth (**−100 % subtracts the dry** — additive/subtractive as one bipolar control), Low Cut in the wet path. MkII plugin keeps all of it |

### 1.4 The modern references (what we verified, product by product)

* **Serum 2 Flanger** (the bar to beat): `BPM Sync` switch; `Rate` 0–20 Hz free or synced (Serum 1
  spans to 1/32; Serum 2 house style keeps snapped divisions); **Rate 0 = manual mode** — the LFO
  parks and you automate the sweep yourself (their manual documents this explicitly); `Depth`;
  `Feed`; `Phase` = stereo LFO offset, "50 % = 180°, sweep rising on the left while falling on the
  right". Serum 2 adds a per-module **Mix + Level** pair, drag-and-drop order, dual FX busses.
  **No through-zero. No barberpole. No envelope mode. No visualizer beyond the knobs.** That is
  the opening we drive through.
* **Arturia Flanger BL-20**: three combinable modulation sources (`Auto` LFO with tri/sine/square/
  saw/reverse-saw, `Env` internal-or-external with Threshold + Decay, `Manual`); **through-zero via
  "an undetectable delay applied to the dry signal"** (their manual's own wording — the exact
  architecture we use, §3.4); stereo offset + reverse sweep (one channel sweeps inverted); a
  12 dB/oct input high-pass; a Pigments-style function generator on sync.
* **u-he Satin** (Flange mode): true two-deck TZF — a pair of emulated tape machines and a center
  slider mixing the decks; a `Trigger` fires the sweep. The reference for *sound*; our `Trig` pill
  is its descendant.
* **Strymon Deco**: `Lag Time` −0.3…3 ms = the flange zone (then chorus 3–50 ms, slap, echo);
  **`Wobble` = random tape-speed modulation** of the lag deck; `Blend` = deck mix (center = deepest);
  **Auto-Flange = "a virtual studio engineer" riding the reels**. Wobble-as-a-knob is stolen
  gratefully (§5 `Wobble`).
* **Kilohearts Flanger**: `Delay · Depth · Rate · Offset · Spread · Scroll · Motion · Feedback ·
  Mix`. The sleeper feature: `Scroll` + `Motion` continuously rotate the wet path's phase offset —
  a **barberpole by phase-scrolling** (SSB-adjacent). Confirms barberpole belongs in a mainstream
  flanger.
* **Valhalla Space Modulator** (free!): eleven algorithms including TZF and barberpole. Sets the
  floor: if the free plugin has TZF + barberpole, a $99 synth's flanger cannot ship without them.
* **Eventide Instant Flanger MkII**: `Depth` −100…+100 (negative subtracts = subtractive flange),
  `Bounce`, osc/env/remote/manual control, sidechain, Low Cut, Shallow/Deep/Wide modes.

---

## 2. ⚡ The Types — six, each with a measurable discriminator

Roster philosophy (law 5): each Type owns a **mechanism**, not an EQ flavor. The discriminator
column is the perceptual-harness measurement that proves night-and-day; run all six through the
harness before showing Max (§11).

```
┌ TYPE ─────────┐  mechanism                                    lineage
│ Tape Zero     │  dual-deck through-zero, polarity null        Itchycoo/Satin/Deco/BL-20
│ Jet           │  high-resonance feedback comb, ± polarity     MXR 117 / Boss BF-2 / A-DA
│ BBD           │  band-limited compander loop + Matrix freeze  Electric Mistress
│ Barberpole    │  synchronized dual sawtooth combs, crossfade  DAFx-15 / Kilohearts Scroll
│ Envelope      │  input-envelope-driven sweep                  A/DA Threshold / Bel BF-20 / BL-20
│ Step          │  tempo-quantized S+H sweep, glided            Subdecay Starlight / trance gate
└───────────────┘
```

### 2.1 Tape Zero *(the flagship — the one Serum 2 cannot do)*

Two reads ("decks") off one shared buffer. The **reference deck** sits at fixed τ0 = 8 ms. The
**lag deck** sweeps Δ(t) around τ0 so the *difference* passes through 0.
`wet = 0.5·(ref ± lag)` — Character picks the polarity (`Sub` = inverted lag = the full-band null;
`Add` = doubling flange). At Δ = 0 in Sub the output cancels **broadband** — that dip *is* the
effect; SOS's numbers (0.66 vs 0.62 ms) say the historical crossing is shallow and slow, so default
Depth crosses gently twice per LFO cycle.

* `Zero Bias` behavior (via `Manual`, §5): shifts the Δ center. Fully CW the sweep never crosses
  zero (classic one-sided flange); centered it crosses twice per cycle; the *approach* to zero is
  audible as the comb widening to infinity (notch spacing 1/Δ → ∞) then collapsing.
* `Wobble` here adds the **Bounce** physics: on every LFO direction reversal, a damped 2nd-order
  overshoot (ζ ≈ 0.35, fn ≈ 1.8 Hz, both scaled by Wobble) rings the sweep — the Eventide servo
  model — PLUS the `TapeMachines.h` SmoothRandom drift stack (§Appendix A) at low level.
* **Discriminator (measured, harness):** short-time broadband RMS at the crossing instant drops
  > 30 dB below the surrounding program in `Sub` at Depth 100/Mix 100 (a normal flanger's deepest
  comb costs ~3–6 dB broadband). Spectrogram: the notch fan *collapses to a full-band vertical
  null* — no other Type produces a vertical event.

### 2.2 Jet *(the pedal — what 95 % of users mean by "flanger")*

Single sweep, feedback up to the edge. Sweep spans the Boss/MXR zone (center 0.5–8 ms typical),
`Feedback` bipolar ±0.95 with the in-loop damping and env-gate of §3.5. Positive regen = the
rising-harmonic-series jet scream; negative = the hollow "underwater" A/DA even/odd flip.

* Characters vary the in-loop damping corner and the LFO shape (§2.7).
* **Discriminator:** with pink noise, the wet magnitude spectrum shows a harmonic peak train with
  peak-to-valley ≥ 26 dB at Feedback 100 (1/(1−0.95)); at Feedback −100 the emphasized series
  shifts by half a notch spacing (measurable as the cross-correlation lag of the two spectra =
  1/(2τ)). No other Type exceeds ~12 dB peak-to-valley at Feedback 0.

### 2.3 BBD *(the Mistress — dark liquid + the Filter Matrix instrument)*

The Jet topology wrapped in the BBD signal conditioning we already own (§Appendix A):
`compressIn → delay read → 4th-order Butterworth recon LP → expandOut`, with the LP corner
**tracking the current delay** exactly like `DelayEngine.h:124`'s clock-droop law (long delay =
dark) — scaled for flange times: corner = 9 kHz at τ ≤ 1 ms falling to 2.8 kHz at τ = 20 ms. The
compander mismatch (tanh in / sinh out, `TerrainChorus.h:161-183`) breathes at high feedback — the
authentic BBD pump. **No noise injection ever** (the no-noise law); the BBD identity is band-limit
+ pump + grit, exactly as fb308 already settled for the delay's BBD type.

* At Rate = 0 this Type IS the **Filter Matrix**: `Manual` plays the frozen comb by hand (or by
  mod-matrix / LFO routing once FX params join the matrix — §12 Q4).
* **Discriminator:** wet HF-ratio (>5 kHz / total) falls monotonically as `Manual` rises —
  a measured slope of ≥ 8 dB across the Manual travel that NO other Type has (their recon path is
  flat); plus compander gain-pump: 4 Hz amplitude modulation depth ≥ 2 dB on a sustained pad at
  Feedback 80.

### 2.4 Barberpole *(the illusion — notches that rise forever)*

The DAFx-15 **synchronized dual flanger** (Esqueda/Välimäki/Parker), implemented verbatim:

* Two combs in series-parallel (Fig. 7 of the paper): each comb's delay runs a **sawtooth**
  `s(n) = (Dmax − Dmin)·((nΔ) mod 1) + Dmin`, Δ = ρ/fs; comb 2's sawtooth is **90° offset**.
* Each comb's wet gain rides a **triangular window at the same ρ, 90° offset from its own saw** —
  each line is silent while its saw wraps; the crossfade hides every reset.
* Paper-validated params: ρ = 0.1 Hz, Dmax = 66, Dmin = 44 samples @ 44.1 k. **The Dmin > Dmax/2
  rule is load-bearing** — below it the resets poke through the crossfade. Our mapping (§3.2)
  enforces `Dmin = 0.55·Dmax` at all Depth settings; Depth scales the pair, not the ratio.
* Direction: up vs down = mirror the saw (`s'(n) = (Dmax + Dmin) − s(n)`). We expose it as the
  **bipolar Rate knob** (CCW = descend, CW = ascend, center-detent = frozen — which degenerates to
  a static comb, still audible, no dead zone).
* `Character: Shift` variant = the paper's **SSB method**: quadrature (Hilbert) network → single-
  sideband shift at ρ, notch count M = D/2. Sounds smoother/glassier than the dual-comb (linear
  notch spacing in constant motion + a faint detune shimmer) — a genuinely different voice for
  the cost of 8 first-order allpasses (§3.6).
* **Discriminator:** spectrogram of pink noise shows strictly **monotonic** notch trajectories
  (dF/dt one-signed for > 10 s); every other Type's trajectories reverse at the LFO rate.

### 2.5 Envelope *(nothing free-runs — the poster child)*

The sweep position is the **input envelope**, not a clock: follower → exponential mapping →
sweep octave position. Play soft = comb parks low; dig in = the comb whips up (or down —
Character flips direction). This is the A/DA Threshold + Bel BF-20 + BL-20 Env mode, and it is
**calibrated to the real bus** (law 1): the follower's knee sits at **−26 dBFS program**, full
sweep at −14 dBFS, floor at −38 dBFS — a ±12 dB musical window around the measured bus level,
NOT a literature threshold.

* `Rate` is never dead here: it scales the follower attack (1→60 ms as Rate goes fast→slow) —
  "how fast the comb chases the playing". The follower *release* belongs to `Tail` (§5 knob 7)
  — one owner per time constant, never two knobs on the same constant.
* `Shape` = response curve (soft-knee log → hard exponential snap).
* **Discriminator:** drive with a −26 dBFS AM-modulated probe (the Phase-G honest-probe law: an
  AM probe, because a static tone hides a dynamics-driven param): the wet spectral-centroid
  trace correlates with the probe envelope at r > 0.9; every LFO Type's centroid trace is
  periodic and uncorrelated with the envelope.

### 2.6 Step *(the rhythmic one — S+H comb locked to the grid)*

The sweep target is **sampled, not swept**: a new delay target lands on every sync-division tick
(Sync on: the house 4 bar → 1/256 list; Sync off: at 1/Rate). Between ticks the delay **glides**
over `max(5 ms, 15 % of the step)` — the comb-click law; a snapped delay read is a click machine.

* `Shape` = quantize resolution: 2…24 distinct comb positions (2 = trance two-step, 24 ≈ smooth
  random). Characters pick the pattern: `Random` (S+H), `Stair Up`, `Stair Down`, `Pendulum`
  (up-down staircase), `Ratchet` (short-short-long).
* **Discriminator:** the delay-position trace (poll `getSweepViz()`, §6) is piecewise-constant —
  spectral-flux shows impulses at the division grid and near-zero between; every other Type's
  flux is continuous.

### 2.7 Characters (dropdown 2 — per-Type voicings, the reverb/distortion grammar)

Each Type ships 4–6 Characters that change the **mechanism's constants**, never just EQ:

* **Tape Zero:** `Sub (Itchycoo)` · `Add (Double)` · `Worn Deck` (Wobble floor raised, drift stack
  ×2) · `Servo` (Bounce ζ down to 0.2 — audible ring on every reversal) · `Wide Zero` (R channel
  Δ inverted — the null sweeps L→R across the image).
* **Jet:** `Silver` (MXR: sine LFO, damping 12 kHz) · `Compact` (BF-2: tri LFO, damping 8 kHz,
  regen knee earlier) · `Deep Sweep` (A/DA: 40:1 span unlocked below Depth 60, damping 10 kHz) ·
  `Hollow` (wet polarity inverted — subtractive comb; DC-notch bass hollow) · `Screamer` (loop
  softclip engages 6 dB earlier — regen distorts before it runs away).
* **BBD:** `Mistress` (recon 9→2.8 kHz law, matrix-ready) · `Deluxe` (recon corner +40 %, pump
  halved) · `Dark Bucket` (recon capped 4 kHz — dub flange) · `Pumped` (compander mismatch ×1.6).
* **Barberpole:** `Rise/Fall` (dual-comb, bipolar Rate) · `Shift` (SSB) · `Double Helix` (L/R
  runs opposite directions — mono-safe check mandatory, §10) · `Stacked` (two dual-comb pairs an
  octave apart — 4 reads, the CPU ceiling voice).
* **Envelope:** `Up` · `Down` · `Duck Zero` (env drives Δ toward zero — loud notes *cancel*,
  a TZF-envelope hybrid nobody ships) · `Snap` (attack floor 1 ms, exponential Shape floor).
* **Step:** `Random` · `Stair Up` · `Stair Down` · `Pendulum` · `Ratchet`.

Character switches **fade-swap-recover** exactly like the delay's type swap
(`PluginProcessor.cpp:7207` `dlySwapping_` grammar) — and per the Phase-G char-switch law, the
new character **re-seats its state** (delay reads, follower, compander env) before the fade-in.

---

## 3. DSP core

### 3.1 Topology (one engine, all six Types)

```
             ┌────────────────────────────────────────────────────────┐
in L/R ──┬───┤  shared ring buffer (per channel, 4096 @48k = 85 ms)   │
         │   └────────────────────────────────────────────────────────┘
         │        │ read A (ref deck / comb 1)     │ read B (lag deck / comb 2)
         │        ▼                                ▼
         │   [Hermite-4 fractional read]      [Hermite-4 fractional read]
         │        │                                │
         │        └────── Type mixer (±polarity, crossfade gains) ─────┐
         │                                                             ▼
         │   feedback path:  fbGate·fb·( LP(damp) → HP(LowCut) → DCblock → softClip )
         │        ▲                                                    │
         │        └───────────────────── written back into buffer ◄────┤
         │                                                             ▼
         │                              wet post: toneTilt → M/S Width → wet trim
         └──────────────────────────────► Mix (equal-power sin/cos) ──► out
```

The engine follows the `DelayEngine.h` house contract **exactly**: pure C++ header, no JUCE,
`prepare(sr) / reset() / setters / updateCoefficients() (per block) / processSample(inL,inR,&outL,&outR)`
returning **wet only** — the processor owns Mix, the send padding, and the routing (Appendix A).

### 3.2 The sweep law — exponential, 40:1, and the house numbers

Delay is swept in **octaves of delay-time**, not milliseconds — an exponential sweep reads as a
linear pitch dive (the A/DA sound); a linear-ms sweep bunches all the action at the short end.

```
manualMs  = 0.1 · 200^m              // m = Manual 0..1  → 0.1 .. 20 ms, log taper
octSpan   = 2.66 · d                 // d = Depth 0..1   → ±2.66 oct at 100 = 2^5.32 ≈ 40:1 (A/DA)
τ(t)      = manualMs · 2^( octSpan · lfo(t) + bounce(t) + wobble(t) )   // lfo ∈ [−1,+1]
clamp τ to [0.05 ms, 42 ms]          // fold the clamp SOFTLY: tanh-limit the octave arg, never hard-clip τ
```

* Buffer: 4096 samples @ 48 k (recycle the `TerrainChorus.h:14` size) covers 42 ms + Hermite guard.
  At 96 k allocate `2^ceil(log2(0.045·fs))`.
* Per-Type overrides: **Tape Zero** sweeps `Δ = ±spanMs·lfo` *linearly* around τ0 = 8 ms (through-
  zero needs a linear crossing — an exponential sweep can never reach Δ = 0), spanMs = 7.5·d, and
  `Manual` becomes **Zero Bias** (Δ center −6…+6 ms). **Barberpole** maps
  `Dmax = manualMs·(1 + 1.2·d)`, `Dmin = 0.55·Dmax` (the paper's reset-hiding rule, §2.4).
* **Zipper law:** τ targets update per block; the *current* τ glides per sample with the
  `DelayEngine.h:141-150` one-pole idiom (~15 ms). The LFO itself runs per-sample (it must — a
  block-rate LFO staircases the sweep into audible steps at fast rates).

### 3.3 The LFO — one clock, morphable, retrig

* **One master phase accumulator per device** (the Phase-G one-clock law: two phase accumulators
  "at the same rate" integrate their glide skew forever — `terrain-instrument-phase-g` §DIGITAL).
  R's phase = master + Spread offset, **derived at read time, never accumulated separately.**
* `Shape` morphs **sine → triangle → ramp** continuously: shape s ∈ [0,1]; for s < 0.5 crossfade
  sin ↔ tri, for s > 0.5 crossfade tri ↔ ramp (ramp = the one-directional sweep-and-return that
  reads as "tape drop"). Step Type re-purposes Shape as quantize resolution (§2.6); Envelope Type
  as response curve (§2.5). No dead knob on any Type (law 5).
* Sync: `SYN_FLG_SYNCDIV` reuses the **exact 20-entry list** of `SYN_DLY_SYNCDIV`
  (`PluginProcessor.cpp:3455-3459`): Free · 4 bar … 1/256 — the house time law. One LFO cycle per
  division. Free Rate range **0.02 → 20 Hz**, log taper (Serum's ceiling; their floor of true 0
  becomes our 0.02 Hz ≈ visually-frozen-but-alive, because a literally-parked LFO is a dead knob —
  the *Manual* knob is the sanctioned static-comb control).
* `Trig` pill: note-on (first voice of a phrase, the delay device's env grammar) resets the master
  phase to 0 **by gliding the read position, never jumping it** (comb-click law) — a 10 ms glide
  through the existing per-sample τ smoother is inaudible as a click and audible as intent.
  This is Satin's Trigger: every note restarts the dive.

### 3.4 Through-zero — the architecture and the latency trap

The Arturia manual states the trick in one line: *delay the dry*. Ours: **both decks live inside
the wet path** — ref at τ0 = 8 ms, lag at τ0 + Δ. The device reports **zero latency** and does no
PDC games (the distortion bible §4.4 lesson: a latency-reporting FX device breaks the fb305 send
maths — fixed internal delay, reported as zero, documented).

* Consequence, stated honestly: at **Mix < 100 %** the true dry (0 ms) combs against the ref deck
  (8 ms) — a static 125 Hz-spaced comb under the flange. At Mix 100 % (the canonical TZF setting,
  and the factory presets ship it there) the effect is pure. This is exactly Satin/Deco behavior
  (their "dry" IS a deck) and the preset sheet says so (§8, `Itchycoo`).
* The null: `Sub` Character inverts the lag deck. As Δ → 0, notch spacing 1/Δ → ∞ (the comb
  "opens"), then at Δ = 0 cancellation is broadband. Hermite interpolation keeps the two reads
  phase-accurate through the crossing; **linear interpolation audibly shallows the null at HF**
  (its HF droop differs with fractional position — the two decks stop matching) — this is the
  measured reason interpolation choice matters for TZF, and why the flanger has **no Eco/linear
  read path at all** (§9).
* Bounce (Tape Zero `Wobble`): 2nd-order resonant LPF on the LFO *velocity* reversals —
  implement as a damped spring on the sweep target: `s̈ = ωn²(target − s) − 2ζωn·ṡ` integrated
  per block (ωn = 2π·1.8 Hz, ζ = 0.5 − 0.3·wobble), which overshoots every reversal exactly like
  a servo hunting. Costs nothing (block-rate).

### 3.5 Feedback — the loop-gain law, env-gated, AC-coupled

Every gain stage inside the loop, accounted (law 6):

| stage | gain |
|---|---|
| user `Feedback` | ≤ 0.95 (knob 100) |
| Hermite-4 read | ≤ 1.06 worst-case overshoot (4-pt Hermite can exceed unity on alternating-sign content) |
| damping LP, LowCut HP, DC blocker | ≤ 1.0 each |
| **worst loop gain** | **0.95 · 1.06 ≈ 1.007** — nominally unstable! |

**Therefore the loop soft-clip is not optional.** Recycle `DelayEngine.h:315-322` `softClip`
(linear to ±1.4, tanh beyond): the loop is BIBO-bounded and the >1 excursion becomes musical
compression exactly like the delay's near-runaway wash (its shipped `fb` cap is 0.98 with the same
guard — precedent). At the −26 dBFS bus level the clip engages **only** during deliberate
self-oscillation — resonant peaks reach ≈ 0 dBFS at Feedback 100 (+26 dB over program), which is
the NO-PLAYING-SAFE ceiling: 100 % is *just past useful*, screaming but bounded.

* **Env-gate (nothing free-runs):** the feedback coefficient is multiplied by an input-follower
  gate `fbGate` (attack 3 ms; release = `Tail` knob, 80 ms → 2.5 s, **squared-release** per the
  Phase-G grid-leak law so the tail dies convincingly instead of latching). Input silent ⇒ the
  ringing comb decays and STOPS. This kills the classic flanger sin of a patch that howls forever
  after note-off. At Tail 100 the bloom is long enough to play; it still always dies.
* **AC-coupled loop (the Phase-G DC-latch lesson, applied preemptively):** a 5 Hz one-pole DC
  blocker lives INSIDE the loop. High-regen combs with asymmetric program can integrate a DC
  pedestal (the softClip then rectifies it — the exact silence-class mechanism Phase G paid to
  learn); the blocker costs one state per channel.
* **Polarity:** `Feedback` knob is **bipolar** (−100 … +100, center-detented): sign flips the
  loop polarity (jet ↔ hollow, §1.2/§2.2). Both directions evolve 0→100 in resonance depth; the
  detent center = no feedback, which is a *voiced* state (pure comb), not a dead zone.

### 3.6 The SSB barberpole variant (Character `Shift`)

Quadrature via the standard **2×4 cascaded first-order allpass phase-difference network**
(the '"dome filter" digitalization the DAFx paper cites): two allpass chains whose outputs sit 90°
apart across 20 Hz–20 kHz (coefficient set: {0.6923878, 0.9360654, 0.9882295, 0.9987488} /
{0.4021921, 0.8561711, 0.9722910, 0.9952885} — the classic Olli-Niemitalo pair, ±0.1° error
above 40 Hz). Shift the delayed path by ρ (ring-mod with a quadrature oscillator at ρ ≤ 4 Hz),
sum with dry: notches in constant one-way motion, M = D/2 notches for D samples of fixed delay
(paper §4). Fixed D = 64 samples @48 k (τ = 1.33 ms, 32 notches). Cost: 16 first-order allpasses
+ 1 quadrature osc — still trivial (§9).

### 3.7 Filters in and around the loop

* **Damping LP (in-loop):** one-pole, per-Character corner (§2.7) — the reason Jet regen sings
  instead of fizzing. In-loop by design: repeats darken (delay-device precedent).
* **`Low Cut` HP (in-loop + wet):** 20 Hz → 1 kHz, log (`20·50^t` — the exact house mapping at
  `PluginProcessor.cpp:7262`). Eventide ships Low Cut on the Instant Flanger because flanged bass
  wobbles the mix floor; in-loop placement also stops sub-bass regen buildup.
* **`Tone` tilt (wet only, NEVER in-loop):** recycle `DelayEngine.h:263-271` `toneTilt` verbatim —
  fb310 already proved in-loop tone starves feedback; the split is settled law.
* **BBD conditioning (BBD Type only):** compander + `ReconLP` from `TerrainChorus.h` (Appendix A),
  recon corner tracking τ (§2.3).

### 3.8 Stereo — Spread, Width, mono safety

* `Spread` = L/R LFO phase offset 0…180° (Serum's Phase, in degrees not %). Implemented as a
  read-time phase offset off the master clock (§3.3).
* `Width` = M/S on the **wet only** (`DelayEngine.h:209-211` idiom), 0…160 %.
* Mono-sum reality (stated for the manual + §10): Spread 180° + Width high = combs in antiphase —
  wet **thins** on mono sum (the comb notches interleave; it does not silence, but the flange
  halves). The harness must include a mono-sum metric on every stereo preset.

### 3.9 Oversampling verdict: **NONE — never, at any Quality tier**

The flanger is linear-time-varying except (a) the loop softClip, which engages only in deliberate
self-osc and produces low-order tanh harmonics of an already-band-limited loop signal, and (b) the
compander, whose gain varies at envelope rate (< 30 Hz sidebands). Modulated-read Doppler shift at
our extremes (τ̇ max ≈ 40 ms/s) detunes by < 4 % — no spectral content approaches Nyquist that was
not already there. **Oversampling would be pure CPU waste** (law 8); the device has no Quality
dropdown slot anyway (both dropdowns are spoken for) and needs none.

---

## 4. Engineering musts (the traps with names)

1. **⚠️ THE FOURTH SEND BUS RE-BREAKS fb305/fb338 — three exact edit sites.** The distortion
   bible predicted this at device 3; it is now measured law at device 4. The exclusion sums at
   `PluginProcessor.cpp:7159`, `:7326`, `:7358` (+ R twins) currently sum **three** buses
   (`rvbSendL + dlySendL + dstSendL`); a `flangerSendBuf_` (alongside `distortionSendBuf_` at
   `PluginProcessor.h:1572`) MUST join **every** sum in the same commit, and the flanger's own
   main-send branch gets the symmetric four-way subtraction — or a flanger-routed osc double-dips
   into every other device's main send (the exact fb305 bug, fourth incarnation).
2. **⚠️ `SYN_FX_ORDER` is a 6-entry permutation choice (fb341, `PluginProcessor.cpp:3488-3494`),
   switch at `:7383`.** Four devices = 24 permutations — a combinatorial dropdown is dead on
   arrival. Recommendation (§12 Q1): keep the 6 reverb/delay/distortion permutations and slot the
   flanger at a FIXED point — **after distortion, before delay** in whatever permutation is active
   (modulation-into-echo is the classic order, §7). One new insert-lambda (`applyFlg`) in the
   existing fb307 insert-lambda chain; the choice list does not grow.
3. **No clicks, the flanger-specific five:** delay glide per sample (§3.2); Type/Character
   fade-swap through zero + state re-seat (§2.7); `Trig` glides phase (§3.3); Sync-division
   changes glide τ (comb-click law — a division jump is a delay jump); Step Type's inter-step
   glide floor 5 ms (§2.6).
4. **Denormals:** `flush()` (`DelayEngine.h:330`) on buffer writes, compander envelopes, follower
   states, DC-blocker states, and the SSB allpass chains; `ScopedNoDenormals` at the processor.
   The Tail-gated feedback decaying into silence is a textbook denormal generator.
5. **The AudioParameterChoice law:** `getRawParameterValue` returns the **index** — read Type /
   Character / SyncDiv as `(int)*rawParam(...)` (CLAUDE.md §4; the fb50 noise-type scar).
6. **WebSliderRelay 4-point chain** for every one of the 11 params + pills (miss one = silent
   no-op; the delay device's relay block is the copy source), and **BinaryData cache-bust** on
   every index.html change.
7. **Transcendentals in the loop:** per-sample cost is 1 sine (master LFO) + smoothing exps
   folded to precomputed coefficients. `DelayEngine.h:157` ships `std::sin` per sample ×3 and
   certified fine — matching precedent is acceptable; the parabolic sine approx is the free
   upgrade if the CPU cert wants margin. `fastTanh` (`TerrainFilters.h:42`) for the loop clip.

---

## 5. Chassis map — the locked 11

**Param IDs** (grammar clone of `SYN_DLY_*`, `ParameterIDs.hpp:374-401`): `SYN_FLG_TYPE`,
`SYN_FLG_CHARACTER`, `SYN_FLG_SYNCDIV`, `SYN_FLG_RATE`, `SYN_FLG_DEPTH`, `SYN_FLG_FEEDBACK`,
`SYN_FLG_MIX`, `SYN_FLG_MANUAL`, `SYN_FLG_SPREAD`, `SYN_FLG_WIDTH`, `SYN_FLG_TONE`,
`SYN_FLG_SHAPE`, `SYN_FLG_WOBBLE`, `SYN_FLG_TAIL`, `SYN_FLG_LOWCUT`, plus
`SYN_FLG_SRC_A/B/C/D/SUB/NOISE` (route pills), `SYN_FLG_SYNC`, `SYN_FLG_TRIG`, `SYN_FLG_POWER`
(default **OFF** — house rule).

### Front — 3 + Mix (hero knobs) + 2 pills

| Knob | Range / taper | Glide | What it does (pragmatic name check) |
|---|---|---|---|
| **Rate** | 0.02–20 Hz log free; synced = the 20-division list 4 bar→1/256. **Barberpole: bipolar** (CCW descend · CW ascend) | phase-continuous (rate changes never jump phase) | How fast the sweep moves. Envelope Type: how fast the comb chases the playing (§2.5) |
| **Depth** | 0–100 → octSpan 0–±2.66 oct (40:1 at max) | target glide 15 ms | How far the sweep travels. 0 = parked comb (Manual is then the instrument — alive, not dead) |
| **Feedback** | −100…+100, center-detent, `t^1.5` taper each side → |g| 0–0.95 | 15 ms | Resonance. CW = jet scream, CCW = hollow underwater, 100 = env-gated self-osc |
| **Mix** | 0–100 equal-power sin/cos (house grammar `PluginProcessor.cpp:7113`) | 15 ms | 100 % = fully wet, zero dry (hard law) |
| pill **Sync** | bool, default ON | — | Rate snaps to divisions |
| pill **Trig** | bool, default OFF | — | note-on restarts the sweep (glided, §3.3) |

### Back — 2 dropdowns + 8 knobs (4×2, three separators, fb275 chassis)

* **d1 `Type`** — Tape Zero · Jet · BBD · Barberpole · Envelope · Step (real `<select>`, never
  click-to-rotate; switch = fade-swap).
* **d2 `Character`** — per-Type voicings (§2.7), 4–6 entries each.

| # | Knob | Range / taper | Glide | Role (and the per-Type remap, always alive) |
|---|---|---|---|---|
| 1 | **Manual** | 0.1–20 ms, log (`0.1·200^t`) | τ glide per-sample | Sweep center / the playable frozen comb. Tape Zero: **Zero Bias** −6…+6 ms (where the null sits) |
| 2 | **Spread** | 0–180° | 15 ms | L/R sweep phase offset (Serum's Phase). 180 = counter-sweep |
| 3 | **Width** | 0–160 % | 15 ms | Wet M/S width (never in-loop) |
| 4 | **Tone** | bipolar tilt, 0.5 neutral | 15 ms | Output-only tilt (`toneTilt` verbatim; fb310 law) |
| 5 | **Shape** | 0–100 | 15 ms | LFO morph sine→tri→ramp. Step: 2–24 steps. Envelope: response curve |
| 6 | **Wobble** | 0–100, `t^1.2` | block-rate | Analog instability: SmoothRandom drift stack; Tape Zero adds servo **Bounce** ring (§3.4) |
| 7 | **Tail** | 80 ms–2.5 s, log | — | How long feedback rings after the input stops (the env-gate release; squared-release). Envelope Type: also the follower release |
| 8 | **Low Cut** | 20 Hz–1 kHz, `20·50^t` | coeff per block | Keeps bass solid; in-loop + wet HP (Eventide's lesson) |

Every knob does something real on every Type (the delay device's "no dead knob on any character"
standard, `DelayEngine.h:160-162`). No doubles: no name above collides with any existing device's
label set (`Manual`/`Shape`/`Wobble`/`Tail` are new to the rack; `Spread`/`Width`/`Tone`/
`Low Cut`/`Rate`/`Depth`/`Feedback`/`Mix` reuse the established house meanings).

---

## 6. Visualizers

### 6.1 How the greats show flanging (surveyed)

* **Serum 2:** no flanger graphic — knobs and a Level/Mix pair in the rack row. (Their EQ/filter
  modules get curves; modulation FX do not.) The bar is LOW — this is where we win visibly.
* **Arturia BL-20:** photoreal hardware panel; the modern additions are a **function-generator
  curve editor** (draw the sweep) and a small env-follower activity lamp. Motion is implied, not
  drawn.
* **Eventide Instant Flanger MkII:** hardware skin; a **DEPTH meter-style sweep indicator** (the
  original FL-201's "sweep" lamp row) — the sweep position is shown as a moving illuminated bar.
  The one hardware-lineage viz that actually shows the *state*.
* **Kilohearts:** flat vector panel, animated only via knob positions.
* **Valhalla Space Modulator:** flat color UI, zero animation.
* **SoundToys (PhaseMistress, for grammar):** analog panel + blinking rate lamp.

Nobody in the field draws **the comb itself moving**. That is the obvious, cheap, dramatic win —
and it is exactly the house law (everything audible is visible; the notches ARE the sound).

### 6.2 Our card — three concepts, one recommended

**A. THE MOVING COMB (recommended core).** Canvas, ~128-point closed-form magnitude of the actual
current response, redrawn per rAF frame from a tiny state push:
`|H(f)| = |mixDry + mixWet·(a + b·e^{−j2πfτ} )/(1 − g·D(f)·e^{−j2πfτ})|` evaluated at the *current
smoothed* τ_L, τ_R, g, polarity — L and R drawn as two strokes (Spread visibly splits them; Width
fattens the pair). Barberpole draws the dual-comb crossfade sum — the fan visibly scrolls one way
forever. Feedback sharpens the drawn peaks 1:1 with the audible scream (peak height = 1/(1−|g|),
the real number). **Audio-reactive drama (law 9):** the comb curve is the dim idle skeleton;
the LIVE input envelope (poll the existing FX-viz push channel — the `window.__dstViz` grammar,
`index.html:8163`) scales a bright fill under the curve: idle = thin dim line, playing = the comb
pumping with the program, loud = the fill blooming to the frame. Engine exports one struct per
block: `{ tauL, tauR, g, pol, envIn, envWet, zeroFlash }` — the `getFeedbackViz()` precedent
(`DelayEngine.h:226`) grown by five floats.

**B. THE ZERO STRIPE (Tape Zero overlay, embedded in A).** A horizontal lag strip under the comb:
two deck ticks (ref fixed, lag sweeping); the gap between them = Δ, annotated in ms. When Δ
crosses 0 in Sub the whole card fires a **white broadband flash** (`zeroFlash` = the measured
crossing-instant cancellation depth) — the null is a visual *event*, matching the audible one.
Envelope Type re-uses the strip as the follower meter driving the tick. Step Type shows the tick
jumping the quantize grid.

**C. THE NOTCH WATERFALL (rejected for CPU).** A scrolling mini-spectrogram with notch trails —
the classic flanger textbook picture. Rejected: an FFT per frame in JS (or a second C++ analyzer
push) violates the frame-drop laws fb342 paid for (no per-frame heavy paint; the halo/shadowBlur
ban). Concept A shows the same information analytically for the cost of 128 `cos` per frame.

Card rules inherited: no per-frame `shadowBlur`/filters (fb342 law), visible×fresh pushes only,
push lanes at 60 Hz, dim-idle/bright-playing delta mandatory.

---

## 7. Interplay — the flanger in the chain

* **Unity-through discipline:** at defaults (Feedback 0, Depth 50, Mix 50, all back knobs neutral)
  broadband RMS out = in ±1 dB. Two trims make it true: the comb sum `a = b = 0.5` (+0…+6 dB
  comb ripple centers near unity), and the wet path carries a fixed **−1.5 dB trim** compensating
  the correlated-sum lift at small τ (verify with the harness, adjust the constant once,
  per-Character if the compander shifts it — the reverb/delay devices settled their trims the
  same way).
* **Ordering wisdom (why the fixed slot is post-distortion, pre-delay):** flanging *after*
  distortion combs a harmonically-dense signal — notches have material to bite (the classic
  guitar-rig order; the DAFx paper's own demo material is "distorted guitars and drum loops").
  Flanging *before* distortion gets its nulls refilled by the nonlinearity — audibly weaker.
  Flanger into delay/reverb smears the sweep into space (lush); reverb into flanger combs the
  tail (weird — available by reordering the three-device permutation around the fixed slot).
* **Downstream:** the sweep imposes ±2 dB slow broadband ripple and periodic spectral tilt —
  compressors after it will pump at the Rate; note it in the manual, it is a feature (the "chewy"
  '80s chain). Subtractive/hollow voicings cut DC-adjacent bass — after the flanger, bass loses
  up to 6 dB at the notch-0 frequencies; `Low Cut` at default 20 Hz leaves bass alone because
  the *feedback* HP is what protects the loop.
* **Stacking (multi-device future):** two flangers in series multiply comb densities (τ1 + τ2
  interleaved notches — lush but −6 dB broadband worst-case); flanger feedback around a delay
  device is the fb306 loop-gain law's jurisdiction — every future chain node counts this device's
  loop as `1/(1−|g|)` peak gain, stated here for the chain epic's ledger.
* **Mono:** Spread 180 / Double Helix thins on mono sum (§3.8) — presets that use them carry a
  mono-checked variant or moderate Width.

---

## 8. Factory presets (12 sketches — name · intent · the loaded values)

Format: `Type/Character · Rate · Depth · Feedback · Mix · [Manual · Spread · Width · Tone · Shape · Wobble · Tail · LowCut]` (0–100 knob units unless stated).

1. **Itchycoo** — the 1967 tape null. Tape Zero/Sub · 0.35 Hz free · 55 · +15 · **100** ·
   [Bias 50 (centered) · 0° · 100 · 50 · 20 (near-sine) · 35 · 30 · 20 Hz]. Sync OFF, Trig ON.
2. **Bold As Love** — slower, wider, add-side shimmer into the null. Tape Zero/Wide Zero ·
   4 bar sync · 45 · 0 · 100 · [45 · 90° · 130 · 55 · 25 · 45 · 25 · 20 Hz].
3. **Jet Takeoff** — the runway scream. Jet/Silver · 0.15 Hz · 80 · **+92** · 70 ·
   [Manual 35 · 20° · 110 · 60 · 15 · 8 · 70 · 120 Hz]. The Feedback showcase; env-gated so it
   lands when the note ends.
4. **Underwater** — negative-regen hollow wash. Jet/Hollow · 1/2 sync · 60 · **−75** · 60 ·
   [50 · 60° · 120 · 40 · 40 · 12 · 45 · 80 Hz].
5. **Silver Mistress** — the Mistress liquid on pads. BBD/Mistress · 0.4 Hz · 50 · +35 · 55 ·
   [55 · 45° · 115 · 45 · 30 · 18 · 40 · 60 Hz].
6. **Matrix Chime** — frozen comb, played by hand. BBD/Mistress · Rate 0.02 Hz · Depth 0 ·
   +55 · 65 · [**Manual = the performance knob** init 62 · 0° · 100 · 55 · — · 5 · 55 · 100 Hz].
   Ships with a mod-matrix suggestion in the description (LFO→Manual once FX destinations land).
7. **Barber Up** — the infinite riser. Barberpole/Rise-Fall · Rate +0.12 Hz · 65 · +40 · 60 ·
   [40 · 30° · 120 · 55 · — · 0 · 35 · 60 Hz]. 30-second bounce audition: notches never reverse.
8. **Barber Down (Shift)** — SSB glassy descent. Barberpole/Shift · −0.08 Hz · 55 · +30 · 55 ·
   [35 · 0° · 100 · 50 · — · 0 · 30 · 40 Hz]. Mono-safe.
9. **Touch Dive** — the comb chases velocity. Envelope/Up · Rate 70 (fast chase) · 75 · +45 ·
   60 · [30 · 30° · 110 · 55 · Shape 65 (snappy) · 10 · 50 · 80 Hz].
10. **Duck Zero** — loud notes cancel themselves. Envelope/Duck Zero · Rate 55 · 85 · 0 · 100 ·
    [Bias 50 · 0° · 100 · 50 · 45 · 20 · 35 · 20 Hz]. The TZF-envelope hybrid headline.
11. **Stair Machine** — 1/8 stepped comb groove. Step/Stair Up · 1/8 sync · 70 · +50 · 65 ·
    [45 · 90° · 125 · 55 · Shape 8 steps · 0 · 30 · 100 Hz]. Trig ON.
12. **Ratchet Comb** — S+H chaos, gated tight. Step/Random · 1/16 sync · 85 · +65 · 70 ·
    [50 · 120° · 130 · 60 · 16 steps · 10 · **Tail 10 (tight)** · 150 Hz].

Preset law reminders: every preset audible ≥ night-and-day vs bypass at its Mix; level spread
within ±3 dB of unity-through (the Phase-G preset-level lesson); the two 180°-Spread presets
(11, 12) get the mono-sum check.

---

## 9. CPU — budget and tiers

Per sample per channel, worst Type (Barberpole/Stacked): 4 Hermite reads (≈ 10 mul each), the
loop filters (4 one-poles), softClip branch, ≈ 6 smoothers — ≈ **90 mul-adds**. Typical Types
(Jet/Tape Zero/Envelope/Step): 2 reads ≈ **55 mul-adds**. SSB Character adds 16 first-order
allpasses ≈ +35. Block-rate: LFO increments, coefficient cooks, Bounce spring, viz push.

* Reference: `DelayEngine` runs comparable per-sample work (2 Hermite reads + filters + 3 sins)
  and certified at a fraction of the dst budget; the flanger lands **≤ 0.5 % of one core @48 k**
  — an order of magnitude under the distortion device. Estimated ≲ delay-device cost ±10 %.
* **Tiering: none needed and none built.** No Quality dropdown, no oversampling (§3.9), no
  linear-read Eco mode (linear reads break the TZF null, §3.4 — the quality floor IS Hermite).
  The one conditional: the SSB allpass bank and BBD compander exist only while their
  Type/Character is active (one engine, one voice active — the delay-device pattern).
* **Sleep:** power off / env silent + tails dead ⇒ processSample early-outs after the fade
  (`hallPower_ || env > 1e-4` grammar, `PluginProcessor.cpp:7139`); the control head sleeps with
  it (the fb344 awake-head lesson — no coefficient cooking for a sleeping device).

---

## 10. Pitfalls — collected, named

1. **Zipper on the sweep** — τ must glide per sample (§3.2); block-stepped τ at fast Rate =
   audible staircase FM.
2. **Allpass interpolation is BANNED for the modulated read** — its state memory scrambles under
   a moving fractional delay (clicks); Hermite-4 only (KVR/JOS consensus + our own
   `TerrainChorus.h:129` precedent).
3. **Hermite loop overshoot** — |interp| can exceed 1; without the loop softClip the "stable"
   0.95 feedback diverges (§3.5 table). Never remove the clip.
4. **Feedback runaway ≠ instability** — bounded self-osc is a feature; UNGATED self-osc violates
   law 6. The gate lives on the *coefficient*, not the output (gating the output clicks).
5. **DC latch in the loop** — asymmetric program + regen integrates DC; the in-loop 5 Hz blocker
   is mandatory (the Phase-G silence-class, preempted).
6. **TZF at Mix < 100** — the ref-deck static comb under the flange (§3.4); document, preset at
   100, never "fix" with PDC (fb305 send maths).
7. **Mono-sum collapse** — Spread 180°/counter-sweep voicings thin to half on mono; harness
   metric + preset discipline (§3.8).
8. **Barberpole reset leak** — Dmin < 0.55·Dmax lets the sawtooth wrap poke through the
   crossfade (DAFx-15's own warning); the mapping enforces the ratio at every Depth.
9. **Two phase accumulators drift** — one master clock, offsets derived (Phase-G one-clock law);
   an L/R pair of accumulators integrates skew forever.
10. **Env-follower on the wrong level** — thresholds copied from pedal literature land 26 dB
    wrong on our bus (law 1); the §2.5 calibration (knee at −26 dBFS) is the only correct one.
11. **Character/Type switch without re-seat** — stale compander/follower/deck states click on
    fade-in (Phase-G re-seat law).
12. **Sync-division jump** — a division change is a delay-target jump; it rides the same τ
    glide (comb-click law), never a snap.
13. **The fb305/fb338 landmine** — §4.1. Three edit sites + the symmetric main-send subtraction,
    same commit.
14. **Choice params read as index** — `(int)*rawParam` (the fb50 scar).
15. **Viz push discipline** — visible×fresh only, no per-frame shadowBlur, one push struct per
    block (fb342 frame-drop laws).

---

## 11. Hard-rule compliance checklist (laws 1–10, walked)

| # | Law | How this design complies |
|---|---|---|
| 1 | Bus reality −26 dBFS | Feedback resonance ceiling computed against −26 program (peaks ≈ 0 dBFS at max, §3.5); Envelope Type knee AT −26 dBFS ±12 dB window (§2.5); no literature threshold copied anywhere |
| 2 | fb275 chassis, 11 params | 2 dropdowns (Type, Character) + 8 back knobs + front Rate/Depth/Feedback + Mix (§5); pragmatic Title-case names, no jargon (`Manual`,`Spread`,`Width`,`Tone`,`Shape`,`Wobble`,`Tail`,`Low Cut`) |
| 3 | Time 4 bars→1/256 | `SYN_FLG_SYNCDIV` reuses the delay's 20-entry list verbatim (§3.3) |
| 4 | Mix 100 % = wet; switches never cut | equal-power sin/cos mix; Type/Character fade-swap-recover; Trig glides (§4.3) |
| 5 | Evolve 0→100, no dead zones, Types night-and-day | every knob remapped-alive per Type (§5 table); six mechanisms with harness discriminators (§2); Feedback detent center is a voiced state; Depth 0 leaves Manual playable |
| 6 | Nothing free-runs; loop-gain law | fbGate follower with squared release (§3.5); the loop-gain table names every stage incl. Hermite overshoot; softClip bounds BIBO; max |g| = 0.95 stated |
| 7 | No clicks | per-sample τ glide, 15 ms param smoothers, fade-swaps, glided Trig, glided divisions, denormal flush (§4.3-4.4) |
| 8 | CPU-friendly | ≤ 0.5 % core, no oversampling ever (§3.9), one active engine, sleeping head (§9) |
| 9 | Audible ⇒ visible, dramatic | the Moving Comb card: real transfer curve + live-envelope fill, dim-idle/bright-playing, zero-crossing flash, one-way barberpole scroll (§6.2) |
| 10 | Recycle first | every borrowed line named with file:line in Appendix A — engine contract, Hermite, softClip, toneTilt, compander, ReconLP, SmoothRandom, sync list, fade-swap, viz push |

---

## 12. Open questions for Max

1. **Chain slot:** fixed post-distortion / pre-delay insert (recommended, §4.2) — or grow
   `SYN_FX_ORDER` to 24 permutations? (I say fixed; the dropdown at 24 is unusable.)
2. **Front pill 2:** `Trig` (note-retriggered sweeps — the Satin move, my pick) or `Zero`
   (a Tape-Zero-only polarity flip pill)? Trig serves all six Types; Zero serves one.
3. **Type roster trim:** is 6 right? `Step` is the most cuttable (its sound is reachable via the
   future mod-matrix S+H → Manual); cutting it frees Character budget for a 7th idea we shelved —
   `Dissolve` (dual-rate crossed combs). My vote: ship 6 as specced; Step earns its slot by being
   tempo-locked, which the matrix route cannot do per-division without setup.
4. **FX params as mod-matrix destinations** (Manual especially — preset 6 begs for it): in scope
   for this device, or the chain epic? The device is designed to survive either answer.
5. **Barberpole default direction** at the bipolar Rate detent: frozen comb (technically honest)
   or nudge to +0.05 Hz so the default always moves (dramaticism)? I lean +0.05 Hz default load.
6. **The `Hollow` question:** wet-polarity lives in Characters (Jet/Hollow, Tape Zero/Sub). Fine,
   or promote to a global back-knob and drop `Low Cut` to per-Character fixed? (I keep Low Cut —
   bass discipline is worth a knob.)

---

## Appendix A — Recycle inventory (verified by reading, not assumed)

* **The engine contract** — `DelayEngine.h` is the template, wholesale: `prepare/reset/setters/
  updateCoefficients/processSample` wet-only shape (`:37-63`, `:139-223`); per-sample smoothing
  idiom `:141-150`; `onePole` `:324-329`; `flush` denormal guard `:330`; `softClip` `:315-322`;
  cubic Hermite `readAt` `:230-249`; output-only `toneTilt` `:263-271` (fb310 law included);
  M/S width `:209-211`; power-of-two ring + mask `:40-43`; viz getter precedent `:226`.
* **BBD conditioning** — `TerrainChorus.h`: NE570-style `Compander` (`:161-183`, tanh-in/sinh-out
  mismatch = the pump), `ReconLP` 4th-order Butterworth cascade (Q 0.541/1.307, `:189-219`),
  `hermite4` (`:129-143`), BBD buffer sizing (`:14`). The chorus engine itself stays untouched.
* **Analog instability** — `TapeMachines.h`: `SmoothRandom` (`:215`), the triple-LFO wow stack
  0.6/2.2/7 Hz with flutter drift (`:571-597`) — the `Wobble` knob's drift source, block-rate.
* **Integration grammar** — `ParameterIDs.hpp:374-401` (`SYN_DLY_*` = the ID/pill/SRC template);
  `PluginProcessor.cpp:3455-3459` (the 20-division sync list, reuse the exact StringArray);
  `:7207` (fade-swap + `reset()` on type change); `:7245-7272` (setter-cook block shape);
  `:6300` (`kVoiceToFxPad`); `:7113-7114` (equal-power mix); `:3488/:5860/:7383` (fxPerm insert
  lambdas); `PluginProcessor.h:1572` (`distortionSendBuf_` — the send-bus template AND the
  landmine, §4.1).
* **Viz plumbing** — `index.html:8163` (`window.__dstViz` push grammar), the fx-rack v7 card
  chassis (`Design/fx-rack-v7-CANONICAL.html`), rr-left/min-width law (`index.html:7174`).
* **Perceptual harness** — the per-family dst cert pattern (compile `clang++ -O2 -I shim
  -I Source`) + `rvb_perceptual.cpp` metrics: magnitude-spectrum delta, centroid, HF-ratio,
  spectral-flux, mono-sum RMS; add the §2 discriminators (crossing-null depth, notch-trajectory
  monotonicity, env-correlation, step-flux impulses).

## Sources

*DSP / papers*
* J. O. Smith, *Physical Audio Signal Processing* — Flanging: https://ccrma.stanford.edu/~jos/pasp/Flanging.html
* Esqueda, Välimäki, Parker — *Barberpole Phasing and Flanging Illusions*, DAFx-15: https://www.dafx.de/paper-archive/2015/DAFx-15_submission_67.pdf
* Raffel & Smith — *Practical Modeling of Bucket-Brigade Device Circuits*, DAFx-10: https://www.dafx.de/paper-archive/2010/DAFx10/RaffelSmith_DAFx10_P42.pdf
* JOS — Delay-Line Interpolation (allpass-vs-Lagrange guidance): https://www.dsprelated.com/freebooks/pasp/Delay_Line_Interpolation.html
* KVR DSP forum — allpass interpolation under modulation (memory-scramble clicks): https://www.kvraudio.com/forum/viewtopic.php?t=486842

*History / hardware*
* Sound On Sound, *Flanger Management* (tape-flange measurements, additive/subtractive): https://www.soundonsound.com/techniques/flanger-management
* Strymon, *What is a Flanger?*: https://www.strymon.net/flanger/
* Eventide, FL-201 Instant Flanger flashback (Bounce): https://www.eventideaudio.com/blog/50th-flashback-5-fl-201-instant-flanger/
* Eventide, Instant Flanger Mk II: https://www.eventideaudio.com/plug-ins/instant-flanger-mk-ii/
* Legendary Tones, A/DA Flanger (even/odd Harmonics switch, Threshold gate): https://legendarytones.com/ada-flanger/
* UA A/DA Flanger manual ("most other flangers offer a 20:1 time delay ratio, the Flanger delivers 40:1"): https://media.uaudio.com/support/manuals/dd/ADA%20Flanger%20Manual.pdf
* Boss BF-2 teardown (MN3207 + MN3102 chips): https://mirosol.kapsi.fi/2014/12/boss-bf-2-flanger/
* Boss BF-2 spec sheet via Sweetwater (Delay 1–13 ms, LFO speed 100 ms–16 s): https://www.sweetwater.com/sweetcare/articles/roland-bf-2-specifications/
* Boss BF-2 schematic: https://www.hobby-hour.com/electronics/s/boss-bf2-flanger.php
* EHX Deluxe Electric Mistress manual (Filter Matrix): https://www.ehx.com/wp-content/uploads/2021/01/deluxe-electric-mistress-manual.pdf
* Electric Mistress history/versions: https://paulreno.com/ehx-electric-mistress/
* Bel BF-20 (SOS news, lineage): https://www.soundonsound.com/news/classic-70s-bel-flange-effect-comes-500-series

*Modern references*
* Serum manual (Flanger params — Rate/Depth/Feed/Phase/BPM): https://s3.amazonaws.com/decembercymatics/Serum_Manual.pdf
* Serum 2 What's New (13 FX, dual busses, Level per module): https://static.xferrecords.com/Serum%202%20What's%20New.pdf
* Serum 2 web manual: https://xferrecords.com/web-manual/serum-2/welcome
* Arturia Flanger BL-20 overview (TZF wording, env, function generator): https://www.arturia.com/products/software-effects/flanger-bl-20/overview
* Arturia BL-20 manual PDF: https://dl.arturia.net/products/flanger-bl-20/manual/flanger-bl-20_Manual_1_1_EN.pdf
* u-he Satin (two-deck TZF): https://u-he.com/products/satin/
* Strymon Deco secondary functions (lag zones, Wobble, auto-flange): https://www.strymon.net/secondary-functions-deco/
* Kilohearts Flanger (Scroll/Motion barberpole): https://kilohearts.com/products/flanger
* Valhalla Space Modulator (11 modes incl. TZF/barberpole): https://valhalladsp.com/shop/modulation/valhalla-space-modulator/
* MusicRadar, Serum FX guide: https://www.musicradar.com/how-to/a-quick-guide-to-xfer-records-serums-effects
* Subdecay Starlight v2 (random S+H flange): https://subdecay.com/exploring-the-depths-of-the-starlight-flanger-v2
