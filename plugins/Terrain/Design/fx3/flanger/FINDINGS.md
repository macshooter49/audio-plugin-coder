# FLANGER — findings

*What I measured, what surprised me, what I cut, what I could not prove.*
*`flanger_cert.cpp`: **83 gates, 0 failures, exit 0**. Every number quoted here comes from that run.*

---

## 1. Bugs the harness found in my own engine (all fixed, all were silent)

These are here because each of them **built clean, ran clean, and sounded plausible.**

1. **The reference deck was reading the feedback loop, and it killed the ± Feedback feature.**
   With both decks on the recirculating buffer the transfer function is `(1 + pol*e)/(1 - g*e)` —
   at negative feedback the numerator's **zero** and the denominator's **pole** land on the *same*
   frequency and cancel. The comb *flattens* instead of its geography *moving*. The polarity gate
   measured **+5.6 dB** where it should invert. Moving the reference deck to a separate clean input
   line restores the classic `a + b*e/(1 - g*e)` and the gate now reads **+38.7 dB / -24.2 dB — a
   62.9 dB swing**. This is the single most important line in the engine and it is commented as such.

2. **`Envelope / Up` and `Envelope / Down` were BIT-IDENTICAL.** Sweep direction was applied
   twice — once in the envelope mapping and once in the geometry — so the two negations cancelled.
   Same bug on `Step / Stair Down`. The Character-distinctness matrix caught it by printing a
   distance of **exactly 0.0 dB**, which is the kind of number no eyeball finds in a listening test.
   Direction is now applied once, in `geometry()`.

3. **`Jet / Screamer` was a no-op at real program level.** Its only difference from `Silver` was a
   lower soft-clip knee — and at a -26 dBFS bus the loop never reaches any knee, so the Character
   was inaudible (1.4 dB from `Silver` on the fingerprint). Fixed by *driving into* the knee:
   `softClip(v*d, k)/d` with `d = (0.70/k)^2`, so a lower knee genuinely means "it distorts earlier".

4. **`Endless / Rise` was falling.** Notch frequency is `k/D`, so notches **rise** when the delay
   **falls**. The saw ran the wrong way and the Type's headline name was backwards.

5. **Feedback was capped too low for `Tail` to mean anything.** At |g| <= 0.97 the natural ring
   outlasts the gate release above about 1 s, so `Tail` plateaued at 0.7 s across its top half — a
   law-1 dead zone. Raised to 0.995 on Jet/Step and 0.97 on Tape Zero; `Tail` now measures
   **0.2 -> 3.1 s**, monotonic, and the 60 s max-feedback row proves the loop is still bounded.

6. **The BBD's band-limit was not audible where it counted.** The reconstruction filter was on the
   lag deck only, so the clean reference leg kept the whole wet bright and the "dark liquid" Type
   measured a **-0.7 dB** HF slope across Manual. Added a gentler 2-pole at the same tracking corner
   across the whole wet sum and widened the corner law to 11 k -> 1.5 kHz (the BBD clock rate goes as
   1/delay, so this is physics, not taste). Now **11.4 dB**, against Jet's **-2.2 dB**.

7. **The house `softClip` in `DelayEngine.h:315-322` is DISCONTINUOUS.** `if (x > 1.4f) return
   std::tanh(x)` jumps from 1.4 to 0.885 at the knee. Recycling it verbatim — which law 10 asks for —
   would have imported a click generator into a loop that is *designed* to reach its knee. I used the
   same idea made C1-continuous. **This is a live bug in shipped code**, and the delay device reaches
   that knee whenever its feedback runs away. Worth a look independently of this device.

---

## 2. Metric failures — the harness was wrong before the engine was

Six of the first-run "failures" were the measurement, not the DSP. Recording them because the same
traps will bite the Chorus and Phaser harnesses.

1. **The spectral centroid is nearly BLIND to a flanger.** A dense comb and a sparse comb have
   almost the same centroid, so a centroid trace reported a full **2.4-octave Depth sweep as 250
   cents** and a five-decade Rate sweep as **no change at all**. Every motion metric here was rebuilt
   on a **comb tracker**: the wet is `a*x + b*x[n-D]`, whose autocorrelation peaks at exactly lag D,
   so per-frame FFT autocorrelation on a noise probe recovers **D(t) directly, in ms** — the quantity
   the ear is actually following. Self-checked against a planted 2.854 ms delay: **reads 2.854 ms**.

2. **That tracker needed three fixes before it was trustworthy**, each of which produced confident
   nonsense first: (a) an integer-lag argmax quantises the trace into a staircase, so every
   median-based shape statistic reads the *quantum* — parabolic peak interpolation; (b) a windowed
   frame's `r[k]` is the true autocorrelation **times the window's own**, which tapers with lag, so
   noise at a short lag routinely outranks the real peak at a long one — de-bias by the window
   autocorrelation, and stop the search at N/3 where de-biasing amplifies noise faster than signal;
   (c) a 7-point median filter, because it is still an argmax.

3. **The analysis frame must be short next to the modulation.** A 170 ms frame smears the
   autocorrelation peak across a moving sweep; the Envelope Type measured **r = 0.26** on a 170 ms
   frame and **r = 0.99** on a 43 ms one. Same engine, same audio.

4. **Empty log bands are not -280 dB, they are unmeasured.** A 160-band grid at 200 Hz on an 8 k FFT
   has bands with no bins in them; they poisoned every max/min metric (the "low comb survives" row
   printed **301 dB**). Bands are now nearest-filled and peak-to-valley runs on 32 k frames.

5. **Peak-to-VALLEY is the wrong metric for resonance.** A feedforward comb already has a *true
   zero*, so P-V saturates around 26 dB with **no feedback at all** and then barely moves.
   Peak-over-**median** is what `1/(1-|g|)` actually does: **3.2 -> 34.3 dB**.

6. **A sweeping resonance under-reads by ~12 dB.** The "Feedback is alive on Tape Zero" row measured
   **+6.8 dB** with the comb sweeping and **+18.1 dB** static, on the identical engine — a moving
   narrow peak smears across analysis bands. Measured static, like the Jet row.

7. **Noise alone cannot see a nonlinearity** (the harmonics of noise are noise) and **a chord alone
   has nothing above 3 kHz to comb.** The distinctness matrices run on a 50/50 chord+noise probe.

---

## 3. What I cut, and why

- **`Endless / Shift` (SSB / Hilbert barberpole).** It is literally single-sideband frequency
  shifting, which is the Bode device's entire reason to exist, on the exact axis Bode is sold on.
  The bible's own cross-bible audit flagged it and recommended the cut; I took it. Replaced with
  `Tight Rise`. The dual-comb Characters carry the Type on their own — trajectory one-signedness
  **0.96** vs 0.03-0.25 for everything else.
- **The bible's `Tone` back-panel knob** -> replaced by **`Damping`**. `Tone` is an output tilt: an
  EQ the downstream Terrain-Patcher already covers, and CLAUDE.md section 5 explicitly forbids that.
  On a device that runs +/-0.995 regeneration, the in-loop band limit is the knob you actually reach
  for, and it changes the **physics** of the resonance rather than the tone of the output. Measured,
  it collapses the comb's 4-15 kHz teeth **20.8 -> 5.6 dB** while the low comb survives at
  27.9 -> 24.4 dB.
- **The bible's bipolar `Rate` on Endless** (CCW descend / CW ascend, centre-detent frozen). A
  bipolar Rate cannot be shown monotonic 0->100 without special-casing the law, and a centre detent
  that means "stopped" is a plateau. Direction moved into the Characters (`Rise`/`Fall`,
  `Rise Deep`/`Fall Deep`); Rate stays unipolar and monotonic on all six Types.
- **`Wobble` as the knob name** -> **`Bounce`**. `Wobble` is already a shipped knob on the Tape FX
  device (`index.html:8732`). `Bounce` is the Eventide FL-201's own name for exactly this servo
  physics and collides with nothing (it appears in `index.html` only as a *Character* inside the
  Distortion device's Slew Clip / Overflow lists).
- **The mono-hostile tag on all three counter-running Characters.** The tag was doing double duty as
  a behaviour switch and a warning. Measured, `Double Helix` decorrelates by **41 dB** and loses
  **0.05 dB** on a mono sum; `Wide Zero` loses 1.4 dB; `Wide Steps` 1.8 dB. None of them is
  mono-hostile. The flags are now separate (`F_COUNTER_LR` behaviour, `F_MONO_RISK` warning) and
  **0 of 48 voicings carry the warning, by measurement.** The worst mono sum anywhere in the device,
  at Spread 180 deg + Width 160 %, is **-5.0 dB**.
- **Nothing else.** No Type was cut for blurring into a sibling: the closest pair is 8.0 dB against a
  4.0 dB gate. Six Types shipped as specced.

---

## 4. Roster validation against the literature

Max asked whether the six are the right six. Walking the flanger literature: the *mechanisms* with
real papers or real circuits behind them are (a) tape through-zero — SOS's Record Plant measurements,
Satin, Deco, BL-20's "delay the dry"; (b) the feedforward+feedback comb — JOS, *PASP*, "Flanging" —
which is MXR/Boss/A-DA; (c) the BBD-specific signal chain — Raffel & Smith, DAFx-10, and the
Electric Mistress's Filter Matrix; (d) the barberpole illusion — Esqueda/Valimaki/Parker, DAFx-15,
which gives two flanger methods (dual sawtooth comb, and SSB); (e) envelope-controlled flanging —
A/DA's Threshold, the Bel BF-20's combinable LFO+env+manual, BL-20's Env mode; (f) stepped/S+H
flanging — Subdecay Starlight v2. **All six are represented and nothing with a citation is missing.**
DAFx-15's SSB method is the only one deliberately absent, and it is absent because it belongs to Bode.

The one I pushed back on internally and kept: **`Step`**, which the bible itself calls the most
cuttable. It survives on two grounds — its discriminator is real and orthogonal (jump ratio 51.6 vs
20.5 next best, i.e. the comb genuinely *jumps* where every other Type sweeps), and it is
tempo-locked, which the future mod-matrix S+H route cannot do per-division without setup.

**What I would add if the roster ever grows**, in priority order: a **`Dual`** Type (two combs at a
fixed musical interval sweeping together — the two-flangers-in-series sound the bible mentions under
"stacking", worth a Type rather than a Character because it doubles the comb count); and a
**`Resonator`** Type (pure feedback comb, no feedforward leg — peaks with no notches, the one comb
geography this device does not currently produce). Neither has the citation weight of the six; both
are real sounds.

---

## 5. Contradictions and errors in the bible

The bible has been through two audit passes and is still not scripture. Found:

1. **Section 3.4's "at Mix < 100 % the true dry combs against the ref deck at 8 ms — a static 125 Hz
   comb under the flange"** is true only for Tape Zero. Applying the 8 ms reference deck to *all six*
   Types, as the section 3.1 topology diagram implies, would put an audible 125 Hz-spaced comb under
   every patch at every intermediate Mix. tau0 = 2 samples for the five short-delay Types puts that
   comb's first notch at 12 kHz instead.
2. **Section 5's Feedback range "+/-0.95" is not enough for section 5's own `Tail` range to work.**
   At |g| <= 0.95 the natural ring is shorter than the top half of the 80 ms - 2.5 s Tail sweep, so
   Tail plateaus. Measured; the cap is now 0.995.
3. **Section 2.7's Character lists are 4-6 entries; the contract mandates exactly 8 per Type.**
   Extended to 8 everywhere, which is a strict improvement (more variety, and the birth-cardinality
   law means the count has to be right on day one).
4. **Section 3.3's "Shape morphs sine -> triangle -> ramp"** cannot satisfy law 1. A triangle's sweep
   speed is *constant* and a sine's is not, so that ordering folds back on itself on every shape
   statistic — a plateau in the middle of the knob by construction. Reordered to
   triangle -> sine -> ramp, which is monotonic (crest 1.9 -> 11.0, measured).
5. **Section 9's "<= 0.5 % of one core" is right for five Types and wrong for BBD** (0.82 %). See 7.
6. **Section 10.2 is right and worth restating**: allpass interpolation is banned under modulation.
   I did not implement one to check.

---

## 6. Cross-device naming conflicts — an integration-owner call

Not mine to resolve, but they will be visible to Max in one session:

| Concept | Chorus | Flanger | Phaser |
|---|---|---|---|
| L/R offset of the modulator | `Phase` | **`Spread`** | `Stereo` |
| the in-loop tone / band limit | `Color` | **`Damping`** | `Color` |
| the sweep centre / base delay | `Time` | **`Manual`** | `Sweep` (front) |
| the low-frequency protection | `Low Keep` | **`Low Cut`** | `Floor` |
| the S+H Type | — | **`Step`** | `Steps` |

I picked `Spread` because it is what the *shipped* code already means by it (`DelayEngine::setSpread`
pushes the right tap later; `FilterFxEngine::setSpread` splits the L/R cutoff) — the Phaser's use of
`Spread` for allpass stage stagger is the outlier. I picked `Damping` because it is the same concept
as the reverb's `SYN_RVB_HIDAMP`, and `Color` in the siblings is a compound knob (mine is a pure band
limit, no drive). `Low Cut` already appears 17 times in `index.html`. **`Step` vs `Steps` is one
letter apart for the same concept and should be settled one way or the other.**

---

## 7. What I could not prove, and what it would take

1. **That the plugin reaches this engine.** The fb373 law: a green DSP harness proves the ENGINE
   works, never that the UI -> param -> DSP round trip does. That gate is headless, separate, and the
   integration owner's. Nothing in this directory touches `ParameterIDs.hpp`, the processor, or
   `index.html`.
2. **BBD's CPU, against the bible's <= 0.5 % claim.** Measured **21.96 us/block = 0.82 % of one
   core**, against 0.41-0.50 % for the other five. Six BBD instances = ~5 % of one core. The cost is
   the 4-pole delay-tracking reconstruction cascade plus the compander, and I did the cheap
   optimisations (block-rate compander coefficients, fast exp2/log2/tanh/sinh, an 8x-decimated drift
   stack, the Character table off a function-local static). ~11 us is the floor for the *shared*
   architecture — 3-4 fractional reads per channel scattered across a 32 kB ring, which is
   latency-bound rather than flop-bound. **If the budget bites, dropping the reconstruction cascade
   from 4 poles to 2 is the first cut and it costs about 3 dB of the BBD discriminator.** Not done
   speculatively.
3. **That it sounds good.** Everything here is measurement. `flanger-worklet.js` exists so Max can
   hear it in Safari before any integration; I ran it headlessly in Node (shimming
   `AudioWorkletProcessor`) and it reproduces the flagship — Tape Zero/Sub nulls **42.6 dB** below
   its own median on a 128-sample window where Tape Zero/Add dips **10.0 dB**, and all six Types run
   finite and bounded. It is not sample-identical to the C++ and is not meant to be.
4. **Latency/PDC behaviour in the host.** The device reports zero latency and has a fixed internal
   wet-path delay of 2 samples (42 us) on five Types and 8 ms on Tape Zero. That is deliberate — a
   latency-reporting FX device breaks the fb305 send maths — but it means Tape Zero's wet is 8 ms
   late against a parallel dry bus outside this device. Documented, not compensated.
5. **A spectrogram picture of the null.** I proved the null in the time domain (short-time broadband
   RMS) and proved it is broadband by construction (`ref - lag` with both decks landing on the same
   integer sample index at Delta = 0). I did not render a spectrogram; the harness has no image output.

---

## 8. The one place I would push back on the design if asked again

**Regeneration and a perfect through-zero null are mutually exclusive on a two-deck machine**, and I
chose the null. Measured: Feedback 0 -> **-61 dB**, Feedback 50 -> **-17 dB**, Feedback 100 -> **-16 dB**.
Feeding *both* decks off the recirculating buffer would restore the null under regeneration — and
would put the numerator's zero on top of the denominator's pole, so Feedback would stop doing
anything on `Sub`, the flagship Character. Real tape flanging has no feedback path at all, so the
current behaviour is the physical one and the factory presets keep `Itchycoo` near centre. It is a
trade, it is documented in the engine and gated in the harness, and it is reversible per-Type in
about four lines if Max would rather have the null than the resonance on Tape Zero.

Related, and the reason this section exists: the parallel Phaser build found a wet-only filter that
capped every null it could produce at -37 dB (-14 dB at the extreme). I audited every filter in this
path against that. `Low Cut` and the M/S width are **post-sum and linear** (filtering both legs then
summing is the same operation, so they cannot break deck symmetry); the in-loop damping, DC blocker
and soft clip exist **only on the feedback path**; and the delay-path `Damping` is applied to **both**
decks, with separate state, whenever the Type is a matched two-deck machine — filtering one leg and
not the other would leave a high-passed residue at Delta = 0 and destroy the null. That argument is
now a number: the null holds at **-47 dB with every tone control simultaneously at its extreme**
(Damping 100 / Low Cut 100 / Width 160 / Spread 180 / Bounce 100), and at **-53 dB** with Low Cut
alone at 1 kHz.

And per the same advice, the null detector is now shown to work **in both directions**: it reads
**-64.3 dB** on a planted 60 dB hole and **-7.1 dB** on clean program. My first version of that
self-check planted a hole that was only deep at its midpoint and the detector correctly read -7 dB —
the probe was wrong, not the detector, which is exactly the failure mode a one-directional
self-check would have shipped.
