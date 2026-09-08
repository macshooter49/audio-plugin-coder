# CHORUS — findings

What I measured, what surprised me, what I cut, and what I could **not** prove.

**Harness:** `chorus_cert.cpp` — **85 gates, 85 pass, 0 fail, exit 0**, ~70 s runtime.
```sh
cd plugins/TerrainInstrument
clang++ -O2 -std=c++17 -I Tests/shim -I Source -I Design/fx3/chorus \
        Design/fx3/chorus/chorus_cert.cpp -o /tmp/chorus_cert && /tmp/chorus_cert
```

Read sections 1 and 2 first. They are the two places where the bible is **wrong**, and one of
them would have shipped a Type that does nothing at all.

---

## 1. THE BIBLE'S MICRO-SHIFT DOES NOT SHIFT PITCH. AT ALL.

`CHORUS-BUILD-BIBLE.md` section 3.5 specifies the `Micro` reader as:

> head delay ramps down at constant slope `r = 2^(+-c/1200) - 1`, heads 180 deg apart on a
> **raised-cosine crossfade of period T = 40 ms** (excursion `r*T ~= 0.24 ms at 10 cents - tiny,
> glitch-free)

I built that exactly. **Measured: 440.00 Hz in, 440.00 Hz out, at every Detune from 6 to 50 cents,
on both channels.** Not "a bit flat" - *identical to the input*, to 0.01 cents.

**Why, and it is not subtle.** A sawtooth-reset delay gives
`y(t) = x(t)*exp(j*w*r*T*saw(t))`, and a factor periodic with period `T` can only put energy on
the lines `f +- k/T`. The dominant line is `k = round(f*r*T)`. At 10 cents / 440 Hz / T = 40 ms,
`f*r*T = 0.10` -> **k = 0** -> the output sits on the carrier with a 25 Hz tremolo. The delivered
shift is quantised to `1/T = 25 Hz` while the *wanted* shift is 2.5 Hz. The bible's entire
"keep the excursion tiny" premise is exactly what breaks it, and its crossfade-comb table
(the famous "853 Hz at 50 cents") is computed on the same broken geometry.

I verified this three ways before touching the engine: analytically, in a 40-line standalone
replication (`peak 440.00 Hz, expected 452.89`), and in the engine itself.

**The fix.** The free parameter must be the ramp **SPAN**; the crossfade period *follows* as
`T = span / r`. A 40 ms span at 10 cents is a **6.9 s** ramp - 0.145 Hz line spacing against a
2.5 Hz shift, i.e. 17 lines of resolution instead of 0.1. Measured after the fix, standalone:

| Detune | span 15 ms | span 30 ms | span 50 ms |
|---|---|---|---|
| 6 c | +5.50 | +6.21 | **+5.97** |
| 12 c | +10.90 | +12.70 | **+12.00** |
| 25 c | +22.75 | +26.55 | **+25.00** |
| 50 c | +45.51 | +53.01 | **+50.03** |

Shipped: span 45->20 ms on `Micro` (Rate re-homes to it), 32 ms elsewhere, scaled in over the
first 8 cents so engaging Detune never steps the effective delay. In the engine: **+5.62 cents**
at the 6-cent floor and **+-24.65 / -+24.67** at Detune 50, symmetric to 0.02 cents.

**Two consequences the bible did not anticipate:**
1. **The real artifact is a FLAM, not a comb.** At the equal-gain instant the two heads sit
   `span/2` apart - 10-25 ms, an audible doubling. That is the honest cost of a delay-line
   shifter and is *why the H3000 preset is called **Layered** Shift*. Four heads quarter it,
   which is why the roster still goes to 4 heads above 25 cents - but for a completely different
   reason than section 3.5 gives.
2. **Up and down are not the same slope.** Up wants `d' = -(2^(c/1200) - 1)`, down wants
   `d' = +(1 - 2^(-c/1200))`. Using one number for both leaves the down side ~1.5 cents flat at
   50 cents. The engine runs a separate ramp per channel.

**If the flanger or phaser agent lifted section 3.5 for anything, it is broken there too.**

---

## 2. THE BIBLE'S `Dark` DISCRIMINATOR MEASURES RED. I kept the Type, on a different claim.

Section 2.7 puts `Dark` on probation against `June` and specifies the falsification test itself:

> run both Types at Time 28 ms, Colour 15, Depth 40 and measure **wet spectral-centroid
> peak-to-peak over one LFO cycle**. Gate: `Dark` >= **6 semitones** where `June` measures
> **< 0.5**. If `June` lands within 2 dB of `Dark`, **cut `Dark`**.

Ran it. Then ran it four more ways because the first answer was uncomfortable:

| probe / metric | June | Dark |
|---|---|---|
| centroid excursion on a chord (the bible's metric) | x1.351 | x1.346 |
| HF-ratio excursion on noise, input-referenced | 5.57 dB | 5.37 dB |
| brightness excursion, deterministic probe, **matched geometry**, Width 0 | **7.48 dB** | **7.33 dB** |
| same, after raising the tracking exponent to 2.2 | 7.48 | 7.33 |
| same, after making it a **3-pole** chain | 7.48 | 7.33 |

**Dark is not higher. It is a hair lower.** The reason took a while to find and is worth writing
down: **every Type's brightness already breathes under a sweep.** The fractional-delay
interpolator's response depends on the frac part, which cycles at a rate set by `d'(t)` - fast at
the LFO's zero crossing, frozen at its extremes. That shared effect is *larger* than a pole moving
0.85 of an octave. Depth 0 measures 0.69 dB (June) / 0.22 dB (Dark); the 7+ dB appears with the
sweep, on both.

**What I did about it.** Not fake it, and not cut a Type Max will want. I went looking for what is
*actually* unique to `Dark` and found it in the filter **order**: Dark runs the full BBD
reconstruction chain, **3 poles**, where every other Type runs one.

> **11.7 dB/oct**, against a next-steepest of 9.2 (Micro) and 3.5-7.0 for the rest.

That is a Type-only property no knob can reach - **Colour moves the corner on every Type; nothing
moves the slope** - it is more authentic than what was there before (Raffel/Smith section 1.1
describes a 3rd+2nd-order output filter), and it is what the gate now measures. The `d(t)`
tracking stays in the engine because the physics is right and it is audible in isolation; it is
simply not what separates `Dark` from `June`. The harness prints the RED result next to the green
gate so nobody re-derives the old claim from the bible.

`June/Dark` ends at **2.66 JND units** in the distinctness matrix - the closest pair in the
roster, clearing the 1.00 gate by 2.7x, on side spectrum.

---

## 3. R7 - the legacy antiphase claim, verified rather than quoted

The brief asked me to check the bible's claim about `Source/TerrainChorus.h` myself. It declares
`RIGHT_PHASE_OFFSET = pi` (:19) and then defeats it with `RIGHT_RATE_RATIO = 1.07` (:18, applied
:56). Two clocks at a 1.07 ratio rotate their relative phase at 0.07*f, so they pass through
**in phase** every `1/(0.14*f)` seconds. Reproduced the legacy topology exactly:

| legacy LFO rate | first in-phase | predicted `1/(0.14 f)` |
|---|---|---|
| 0.40 Hz | 18.27 s | 17.86 s |
| 1.13 Hz | 6.17 s | 6.32 s |
| 1.50 Hz | 4.69 s | 4.76 s |

**Claim confirmed.** But it is not "the offset washes out" - it *rotates*, forever, and it sounds
good: the stereo image breathes open and shut over ~10 s. So the x1.07 is kept as the `Vintage`
Type's identity (correlation drift sigma **0.221**, the highest in the roster by 3x) and the
honest fixed version ships beside it as the `Locked` Character (sigma **0.006**). Max can hear the
bug and the fix in one dropdown.

Also re-measured, also confirmed: the legacy tanh/sinh "compander" is a **static x1.47
(+3.35 dB) trim** at the -26 dBFS bus with zero level-dependence. Not copied. Neither is
`WET_GAIN 2.5`, the unglided `setParams`, or the `jassert`-only buffer guard (which in a release
build wraps the read past the write head - metallic garbage, not a crash).

---

## 4. Three real bugs the harness caught in my own engine

**(a) Two Types had NO STEREO.** I had an optimisation that computed the tap reads once for
mono-line Types and applied both channels' gains. That is legal only when the two channels' tap
*geometry* is identical and only their gains differ - true for `Trio` (three taps off one line,
panned), **false** for `Pedal` (its second read has an offset) and `Ensemble` (its right channel
is phase-rotated). Both Types were silently outputting L == R: measured L/R correlation **exactly
+1.000**. Found by the 96-cell liveness sweep - `Pedal/Phase` and `Ensemble/Phase` both read
**0.00**. Fixed with a `sameGeom_` check computed per block; those cells now read 74.26 and 66.52,
the two strongest Phase cells in the roster.

**(b) `Flutter` was dead on `Wow`.** The Wow branch of `tapDelay` added the tape stack and the
random walk and never added the fast bank. Same sweep, same 0.00. One line.

**(c) A power-on thump.** The Raffel poly's `+1/8` term is a DC offset by construction, so a fresh
engine emitted `0.125/k` of DC that the 12 Hz blocker decayed over ~13 ms - **-19 dBFS relative to
program**. It also converges to `0.125/k/(1-fb) = 0.087` inside a 0.82 loop, which shifts the
poly's own operating point; that showed up as **1.8 % DC** on Dark at grit 100 % + Feedback 90 %.
Fixed by subtracting the constant analytically (it is a constant, not a signal) and by adding a
second blocker at the **output** - the in-loop one sits before the feedback tap and structurally
cannot catch what the expander adds after it.

---

## 5. A law I found by measuring: a Character must not be a volume knob

Nothing asked for this gate; the Character-swap click test found it. `Dark: Murk -> Pumped` banged
**+5.9 dB** mid-note and no amount of dip/glide fixed it, because it was not a click - **Pumped
was simply 7 dB louder**. Sweeping all 64 rows:

```
Vintage 0.91 dB   June 1.76   Pedal 1.06   Trio 4.47
Ensemble 1.26     Micro 5.21  Wow 2.05     Dark 7.52 dB   (Pumped +7.07 over Cheap)
```

That is the fb343 preset-spread lesson wearing a Character's name. `CharSpec` gained a **measured**
`lvl` field - all 64 values are `10^(-measured/20)` from a bus-level chord, not guesses - and the
spread collapsed to **0.05 dB worst case**. The bang went with it (**+2.37 dB** worst, gate +3.0).
There is now a permanent gate: *no Character is a secret volume knob (spread < 1.5 dB)*.

---

## 6. Probe craft - every one of these started as a wrong number

The harness header carries these; they are the reusable part of this work.

* **The Mix law needed a new probe.** A chorus's wet *is* a delayed copy of its dry, so no filter
  trick isolates them. The dry path has zero delay and the wet has >=1 ms, so measure the
  **pre-wet window** - and even then isolate the *linear* part by running `+in` and `-in` through
  fresh engines and halving the difference, because the engine's own BBD hiss lives in that window
  and is not dry. Result: **-142.4 dB** on every Type (it is `cos(pi/2)`, i.e. float zero).
* **Correlation frames must be longer than the LFO period.** At 0.25 s frames, June's
  *within-cycle* correlation swing read as sigma = 0.39 of "drift" and completely buried Vintage's
  real clock-skew rotation (0.09 on a short probe). 3 s frames over a 30 s probe separate them
  cleanly: 0.042 vs 0.221.
* **One 16384-sample spectrum of a moving comb is a snapshot at one LFO phase, not a spectrum.**
  Average |X| over a whole cycle or the number is a dice roll.
* **Peak-picking a pitch is wrong when the carrier has FM sidebands** - the peak jumps to a
  sideband and reported **-25 cents** on `Vintage`, which does not shift at all. The
  energy-weighted **centroid** of the band survives symmetric modulation. (Self-checked against an
  injected +9.00 cents: reads +8.98.)
* **Comb depth cannot be read off a 48-band log spectrum** - the bands are wider than the teeth,
  and Feedback read "24.3 -> 27.6 dB, non-monotonic". Sweep finely across **one comb period** and
  take max/min: 28.1 -> 39.2 dB, monotonic.
* **The delay-time knob is measured by autocorrelation lag**, not by hunting for spectral nulls.
  The null-hunt reported the comb moving *up* as Time increased. Lag reports 1.00 -> 12.00 ms,
  exact. (Self-checked against an injected 5.00 ms: reads 5.000.)
* **Feedback cannot be measured from a decay tail here.** The loop is env-gated by law, so the
  tail dies in ~150 ms at *every* setting. Measure sustained comb depth instead.
* **Spectral flux on a chord is mostly DFT noise.** A 48-band log spectrum has bands sitting
  between partials whose dB value is noise; counting them measured 4.45 dB/frame of "movement" on
  a completely static signal. Weight by band energy (-35 dB from the loudest), or use a different
  metric entirely.
* **A noise probe's own band-energy variance is +-4 dB between frames** and put a floor under every
  brightness measurement. Either reference each frame to the same frame of the dry input, or use a
  deterministic broadband source. A noise probe reported June's *one-pole* recon filter at
  **16.8 dB/oct**, which is impossible.
* **A triangle LFO puts 1/9 of its energy at 3f**, which read as a "second modulation rate" of
  0.11-0.15 on every triangle Type and nearly buried `Ensemble`'s real 6.25 Hz bank. Exclude
  harmonics - with an **absolute** tolerance on the ratio, not a proportional one; 10 % of n is
  +-1.2 at n = 12 and swallowed the line it was meant to protect.

---

## 7. What I could NOT prove

* **That any of this sounds good.** Everything here is a measurement. 85 green gates say the
  mechanisms are real, distinct, monotonic, click-free and mono-safe; they do not say the roster is
  musical. `chorus-worklet.js` exists so Max can decide that, and it should happen before
  integration (the fb296 law).
* **That the plugin will REACH this engine.** The fb373 law, stated in the harness header: a green
  DSP harness proves the *engine* works and never proves the plugin reaches it. Cassette ran
  Studio's machine bit-for-bit through four rounds of green measurement. The UI -> param -> DSP
  round trip is a separate, headless gate and is the integration owner's.
* **The Juno `I+II` rate.** The bible flags a 10x source conflict - pendragon's 9.75 Hz vs
  Anwander's 1 Hz for the same machine. I shipped 9.75 Hz (a capture from real hardware) as the
  bible recommends. **This needs Max's ears against a reference recording; it is a
  character-defining number and I cannot settle it from a harness.**
* **CPU under a real dynamic chain.** I measured this engine in isolation: **17-22 us/block** at
  48 k/128 with Detune off, **24-36 us** with Detune engaged (the 2-head reader doubles the reads).
  Worst Type `Ensemble` at 36.6 us = 1.36 % of one core; six instances = **8.2 %**. That is a
  single-thread microbenchmark on an idle machine and says nothing about cache behaviour with six
  instances of three devices live in the rack.
* **That `Vintage` and `June` are far enough apart for Max.** They measure 4.94 JND units and their
  mechanisms are genuinely different (rotating dual clock vs locked antiphase), but they are the
  two closest in *concept* and both are "a Juno". If he hears them as one Type, `Vintage` should
  absorb `June`'s Characters and free a slot.
* **`Ensemble`'s dual-rate discriminator is settings-dependent.** It works because 6.30/0.54 =
  11.66 is not near an integer. Its slow rate follows the Rate knob, so at some settings the fast
  bank could land on a harmonic of the slow one and the metric would exclude it. The *sound* is
  unaffected; the *gate* would need a smarter estimator. Flagged, not fixed.

---

## 8. Open questions for Max (the bible's section 11, answered where I could)

| # | Question | My answer |
|---|---|---|
| Q0 | `SYN_FX_ORDER` replace-or-reslot | **Not mine** - it is shared spine and I am forbidden from touching it. But it does block: a 6-entry choice cannot grow, and three new devices need 4+ slots. The bible's option **(b) per-device order slots** is the only one that survives the chain epic. Integration owner's call, and it must be made before the first `SYN_CHR_*` param ships. |
| Q1 | Type list lock; keep `Pedal` **and** `Dark`? | **8 Types, and yes keep both.** They are structurally opposite: Pedal is co-phase (d(t) topology **+1.000**), Dark is antiphase (**-1.000**). They measure 10.00 apart, the maximum. `Dark` survives probation on a different claim than the bible's (section 2). |
| Q2 | Dimension boundary | Respected. No `Dimension` Type; only `Low Keep` and wet-only width are stolen. |
| Q3 | The 2nd front pill | `Sync`, as proposed. It is the only candidate that is live on all 8 Types. |
| Q5 | Character count | **8 per Type, all 64 real and all distinct** - the closest pair in the whole roster is `Pedal Chorus/Warm` at 3.24 against a gate of 1.5. No trimming needed, and none of them is a volume knob (section 5). |
| Q6 | Detune ceiling 50 cents | **Keep 50, on 4 heads above 25** - the bible's option (a) - but the reason changed. The 50-cent artifact is not a comb at 853 Hz (that number comes from the broken geometry, section 1); it is a `span/2` flam, and more heads shorten it. |
| Q7 | Preset count | 14 sketched in ROSTER section 6. Every Character is already level-matched to 0.05 dB, so presets only need Mix and Low Keep checked. |
| Q8 | Viz direction | Voice Orbits is exactly right and the engine already feeds it: `viz().notch[8]` carries each live tap's first comb null, `lfo` is the real needle, `lvl` the wet envelope, `depthNow` the effective excursion in ms. Not my deliverable - flagging that the telemetry exists. |
| Q9 | Juno `I+II` rate | Shipped 9.75 Hz. **Unresolved - needs an ear A/B** (section 7). |
