# Terrain Instrument — Distortion Build Bible

**v2 — research complete. The single authoritative spec for the 3rd FX device.**
Written after fb310 (delay arc closed). Ten research agents; every number below is either measured
on this machine, read out of a cited paper, or read out of this repo. No DSP written yet.
Companion to `REVERB-BUILD-BIBLE.md` — same structure, same laws, same chassis.

> **What changed from v1:** the roster went from 9 types to **23 modes in 6 families**; the anti-aliasing
> budget is now **measured** rather than estimated (and v1's Fold row was wrong); the §3.4 latency trap has
> a concrete fix and a second, undiscovered half (§4.5); there is a new **§2 NO PLAYING SAFE** extremity
> table and a new **§6 transfer-curve editor**; `Console`, `Soft Sat`, `Sine Shaper` and `Custom` are **cut**
> with their sounds preserved as Character voicings.

> **AUDIT STAMP (2026-08-11, post-fb312).** Independently checked before this doc was handed over:
> **31/31 C++ line references verified exact** by reading each one. **8 `index.html` references were
> stale by a systematic +58 lines** — fb311 and fb312 landed while the research was running — and have
> been corrected; the two refs *before* the insertion point (`:7155` `.fxr-core`, `:7506` `CORES`) were
> already right and were left alone. ⚠️ **`index.html` line numbers drift with every UI build — re-grep
> the symbol, don't trust the number.** C++ refs have been stable.
> Three measured claims were reproduced on independent harnesses: **ADAA-2-in-float** (21.14 dB vs
> 61.00 dB naive here, vs 22.0/58.9 as written — same conclusion), the **Fold extremity** (1× naive
> −15.31 dB, and ADAA buys +18.5 dB at 8× — direction and magnitude confirmed; absolute dB differ
> because the check used a 193-tap Blackman decimator rather than JUCE max-quality polyphase), and the
> **latency residual** (`2|sin(πfN/fs)|` ⇒ +5.72 dB at 5 kHz / N=4, exact). Six actionable
> existing-bug claims were each confirmed against the source. §9 was sampled, not read line-by-line.

---

## 0. The scope decision (LOCKED — Max, 2026-08-11)

**ONE device, named `Distortion`.** The rack slot currently labelled *Saturate* is renamed.

### Why one and not two

Saturation and distortion are **not two effects**. They are one mechanism — a nonlinear transfer
function — observed at two points on one knob. Push a signal through a curve that isn't straight and
it grows harmonics that weren't there. At 20 % drive on a tube curve we call that "saturation"; at
100 % on a wavefolder we call it "distortion." Same maths, same code path, different amount.

There *is* a real technical split, but it does not run along the saturation/distortion words. It runs
along **three physical families** (§1), which resolve into **six parameter-set families** — and all six
fit inside one Type dropdown, exactly as nine different reverb engines (FDN, tank, spring, FIR
convolution) already live behind one Type dropdown.

Supporting arguments:

| | |
|---|---|
| **Serum is the bar** | Serum 2 ships **one** device, "Distortion", spanning 18 modes in a flat list. Vital: one. Phase Plant: one. Only *DAWs* split them (Ableton's Saturator / Overdrive / Amp / Pedal) — a DAW has infinite slots, a synth rack does not. |
| **Our chassis is built for it** | 11 params, 2 dropdowns, per-family relabelled APVTS slots (Model A). Tube-vs-Bitcrush is a *smaller* engine gap than Hall-FDN-vs-Convolution-FIR, which already ships. |
| **No-doubles** | Two devices force a duplicate `Drive` knob and duplicate `Tube`/`Tape` type names. Immediate rule violation. |
| **Placement is already solved** | "Warm glue on the whole synth, wild fold on osc C" is a *routing* question — main send vs pills vs drag order — shipped in fb303/fb305/fb307. It never needed a second device. |
| **Naming honesty** | A device called *Saturate* that bitcrushes is lying. A device called *Distortion* whose Tube type at 20 % drive gives warm glue is telling the truth — that is exactly what a tube does. (`Drive` collides with the front knob; `Shaper` collides with the LFO shaper. Both ruled out.) |

**The one cost:** you cannot run Tube warmth *and* Bitcrush simultaneously in one instance. That is a
**multi-instance rack** feature — the processor currently holds one engine object per device
(`DelayEngine delayEngine;`, one of each reverb) — not a reason for a second device. Tracked in §12.

### What the research added to the scope decision

The 18-mode Serum list turned out to be **wide and shallow in a specific, exploitable way**: it is
~5 params total (Drive, Mix, one filter with Off/Pre/Post + Freq + Q) spread across 18 curves, and two
of its most interesting modes are crippled. Its **X-Shaper has no Drive at all** — the manual states
"On the X-Shaper and X-Shaper (Asym) modes, the drive knob is a *morph* between the two waveshapes",
so you cannot push harder into your own drawn curve. And it has **no bit-reduction mode whatsoever**.
Both are places where we do not merely match the bar, we clear it: we split Morph from Drive (§6), and
we ship a whole Digital family (§9.6).

The scope decision therefore stands **and widens**: one device, 23 modes, 6 param sets, 8 Character
voicings per mode. Width matched, depth beaten.

---

## 1. The physics — three families, six param sets

Every mode in this device generates harmonics. They differ in **how**. Three physical families, and
because two of them are large, six *parameter-set* families keyed to the back-8.

### Family A — Dynamic / with memory  *(the expensive, interesting one)*

Output depends on input **history**, not just the current sample: `y[n] = f(x[n], S[n])` where `S` is a
state vector **the signal itself writes**. A memoryless shaper has one transfer *curve*; these have a
transfer *surface* the program material walks around on. Three physically distinct memory mechanisms,
and every ANALOG mode is built on one of them:

1. **Magnetic memory** — the state is magnetisation `M` (Tape) or core flux `Φ` (Transformer). The
   input/output relation is a double-valued **loop**, not a curve: output depends on the *direction* of
   travel. Jiles–Atherton makes this explicit via `δ = sign(dH/dt)`; Transformer makes it explicit
   because `Φ = ∫v dt`, so the state is literally the running integral of the input.
2. **Operating-point memory** — a reservoir capacitor (cathode bypass, grid coupling, supply rail)
   charges with the average rectified signal and **moves the bias point** of the nonlinearity. Tube
   (cathode sag + grid blocking) and Overdrive (rail sag) live here. The audible consequence: *a loud
   stab changes how the NEXT note distorts.* That is the one behaviour no curve can imitate and it is
   the family's whole reason to cost more CPU.
3. **Rate memory** — the output cannot move faster than a fixed slope, so distortion is a function of
   `dx/dt` rather than `x`. Overdrive's LM308 slew limiter (0.3 V/µs) is this.

* **Modes:** Tube · Tape · Transformer · Stomp Box · Overdrive
* **Tell:** THD changes with *program level and history*, not just instantaneous amplitude.
* **Cost:** high — ODE solvers, state per sample. Plain memoryless ADAA does **not** apply.
* **Honest limitation:** everything here **compresses** (crest factor falls with drive). Nothing here
  expands. Expansion belongs to Fold and to Diode 2. Do not try to fix that inside this family — it
  would mean faking the physics.

### Family B — Memoryless waveshapers

`y = f(x)`, full stop. Cheap, instant, aggressive, and **ADAA applies directly**, which is far cheaper
than brute-force oversampling. Four param-set families, separated by *what the curve does*:

* **CLIP** — bounded by algebra: `|f(x)| ≤ 1` for all `x` **by construction**, not by a limiter after
  it. The only free variables left are (a) the **order of continuity at which the curve meets the rail**
  and (b) **what happens at the zero crossing**. That single number — continuity order — predicts
  harmonic decay, aliasing budget, CPU cost and perceived aggression. Soft Clip is C∞/asymptotic
  (exponential decay); Hard Clip is C1 (`1/n²`, −12 dB/oct); Zero-Square is C⁻¹, a genuine jump
  **in the waveform** (`1/n`, −6 dB/oct).
* **DIODE** — the same physical device (Shockley p-n junction, `I = Is·(e^{V/nVT} − 1)`) read out through
  four topologies: shunt antiparallel pair (Diode 1), series/class-B dead zone (Diode 2), mismatched
  pair / bias injection (Asym), bridge rectifier (Rectify). What unites them is **how differently they
  treat the two halves of the wave** — which is why one bipolar front knob drives all four.
* **FOLD** — the slope **reverses sign** at one or more thresholds. A clipper's slope goes to zero and
  stays; a folder's flips to −1 and keeps flipping, so the harmonic count **multiplies** with every
  threshold crossed and grows *linearly* with drive gain rather than logarithmically. This is why the
  family aliases worse than everything else and why it is the most dramatic. Symmetric folding gives
  odd harmonics only; a DC offset before the folder breaks the symmetry and is the documented second
  timbral axis ("reminiscent of pulse-width modulation" — Esqueda et al.).
* **SHAPER** — the curve is **data**: drawn by the user, synthesised from a harmonic spectrum, or read
  out of a wavetable frame. Three things become true here that are false everywhere else: the aliasing
  profile is *user-determined*; ADAA and the A/B morph **commute** (antiderivatives are linear in `f`,
  so crossfading two `F1` tables is identical to crossfading the outputs — which makes Morph free); and
  every other mode in the device can dump its curve into it (§6).

### Family C — Sample-domain destruction

Not a curve at all. There are exactly three axes you can break in a PCM stream and we ship one mode per
axis: the **amplitude** axis (Bitcrush), the **time** axis (Downsample), and **the container itself**
(Overflow). What unites them is one aliasing policy: a quantiser staircase, a ZOH staircase and an
integer wrap are all discontinuous **in value**, so their harmonics fall at only `1/n` and cannot be
rescued — only deleted. **Baseline anti-aliasing for the whole family is 1× / none.** The artefacts
*are* the product.

* **Modes:** Downsample · Bitcrush · Overflow
* **Cost:** trivial — well under 1 % of a core for the whole family.

### 🔑 THE FAMILY TELL — the one measurement that proves the taxonomy is real

Feed the same tone at **−30 / −20 / −10 / 0 dBFS** with level-matching on, and compare the harmonic
profile **SHAPE** (H3/H2 ratio), not its magnitude.

* A **Family A** mode's shape must move by **> 2×** across that span. That is memory.
* A **Family B** mode's shape is **constant BY CONSTRUCTION**. Memorylessness is the definition.

That single metric is what separates the families, and it is the reason `Soft Sat` is cut (§9.1) — it
fails the Family A thesis *definitionally*, not marginally.

---

## 2. ⚡ NO PLAYING SAFE — the extremity table

> *"Serum lets you get very dangerous, and ours don't. At 100% it lets you get REALLY fucked up.
> I don't want to play safe anymore. If they want to make some trashy shit, let them."* — Max

**The law.** A knob's maximum is set by where the sound stops being **useful to somebody**, not where it
stops being **clean**. If nobody could ever want the maximum, the maximum is too low. **Clamp only for
stability** — BIBO / NaN / DC / solver divergence — **never for taste.** 100 % must be allowed to be
destructive, ear-splitting, ruinous.

### 2.1 Root cause — why Terrain ships timid, and it is not the knobs

Three structural facts, measured:

1. **THE GAIN-STAGING DEFICIT.** `PluginProcessor.cpp:6123` applies `kVoiceToFxPad = 0.5f` (−6.02 dB) to
   the voice mix **before** the FX chain; the compensating `kInstrumentMakeup = 2.0f` is applied at
   `:7149`, **after** every FX. On top of that `SynthVoice.h:4878` defaults `level_ = 0.7f`. The fb299
   comment at `PluginProcessor.cpp:46` records the measured result: a single note bounces at **−20 dBFS**
   pre-makeup. Subtract the pad and **a single note arrives at the FX bus at ≈ −26 dBFS (0.050 linear).**
   Any drive range copied from a reference plugin therefore lands **26 dB short**. Vital's +30 dB on a
   full-scale signal is equivalent to **+56 dB** on our bus.
2. **DRIVE IS EXPRESSED AS A LINEAR MULTIPLIER.** Every drive in Terrain is `1 + amount·k`. Vital's
   `distortion_drive` is declared **in dB** with a `kLinear` taper across −30…+30 dB — 0.6 dB per 1 % of
   travel, no dead zone anywhere, because the ear hears dB. Our `1 + amt·4` spends its first half getting
   to +9.5 dB and its **entire second half adding 4.5 dB more**.
3. **THE SAME KNOB IS DRIVE *AND* WET MIX.** `SynthVoice.h:4178-4185` uses `amt` twice — once setting
   `d = 1 + amt·4`, once as the wet-blend coefficient — so the effect scales as **amount²**. Measured THD
   on a 0 dBFS 110 Hz sine: 10 % → 1.09 %, 25 % → 4.71 %, 33 % → 7.33 %, 50 % → 13.71 %, 100 % → 33.39 %.
   That is a textbook dead first third produced by multiplying two small numbers.

### 2.2 The house drive law

```
driveDb  = D_max · (drive01)^0.8          // near-linear in dB, no dead first third
driveLin = 10^(driveDb / 20)
```

Plus, **inside the device only**, a `+6.02 dB` input trim and a matching `−6.02 dB` output trim that
exactly cancel `kVoiceToFxPad` for the device's own maths. **Do not remove the pad globally** —
`PluginProcessor.cpp:6117-6122` documents that the reverb and delay were tuned for a −12…−6 dBFS
operating point and removing it "causes audible distortion at subtle tape settings."

| Family | `D_max` | With `Slam` | Notes |
|---|---|---|---|
| ANALOG | **+30 dB** | +50 dB | Overdrive is the outlier: **+78 dB** (+98 with Slam) — it is a 3-stage cascade |
| CLIP | **+48 dB** | — | Threshold sits at −6 dBFS internally, so a −48 dBFS input is fully saturated at max |
| DIODE | **+48 dB** | — | Plus a **falling threshold** `V = V0·(1 − 0.85·d)` ⇒ ≈ **+64 dB effective** past the knee |
| FOLD | **+36 dB** | — | Drive is tapered so **fold count**, not gain, is linear in the knob (see 2.4) |
| SHAPER | **+48 dB** | — | Composed with the `Beyond` rule, which is where the real ceiling lives |
| DIGITAL | n/a | — | Drive is pre-gain into the destroyer; the front `Crush` knob is the extremity axis |

⚠️ **The DIODE agent proposed +60 dB and the ANALOG agent +30 dB.** Reconciled above: DIODE's falling
threshold already contributes ~16 dB of effective drive, so +48 dB of gain gets there; ANALOG's lower
figure is correct *because its modes self-limit*, and the `Slam` pill (Decapitator's Punish, +20 dB) is
what buys the nuclear option without destroying knob resolution in the useful 0…+12 dB region.

### 2.3 🔑 THE EXTREMITY TABLE

| Mode | What **100 %** actually does | The **dangerous** zone | Failure point (past here it is only noise) |
|---|---|---|---|
| **Tube** | +30 dB grid drive; a sine emerges as a squashed asymmetric blob, THD > 40 %, H2 dominant, the DC blocker visibly working | **Bias +70 or hotter · Sag ≥ 100 % · Recovery ≤ 1 ms** — the cathode tracks the envelope *inside* the audio band, so stage gain modulates at the note's own frequency: gated sputter, motorboating, IMD. Other direction: **Bias −80** gates the negative half off entirely → half-wave octave-up spit that eats quiet material | Recovery < 0.2 ms with Sag > 120 %: the sag corner rises above ~800 Hz and it becomes a full-wave gain modulator — broadband hash, no pitch. **Reachable on purpose**; the bottom decade of Recovery is deliberately spent there |
| **Tape** | `a → 0.083`, so `Q ≈ 12·H` — the Langevin saturates every sample; hysteretic square with a rounded **lagging** edge, THD > 50 %, crest → 1.4 | **Bias NEGATIVE, not Drive.** At Bias −80 / Drive 60 the deadzone eats the middle ±0.15: reverb tails, releases and quiet passages turn to gritty gated buzz while loud notes come through intact. The inverse dynamic of every other mode in the device | Solver divergence near Nyquist (the paper says so outright). Guard: clamp `H` to ±8a before the RK step; on a trip **HOLD** state and cross-fade wet to 0 over ~2 ms — never zero `M` (that is a click) |
| **Transformer** | Flux 20–30× past the knee at 50 Hz: bass emerges as **differentiated square edges** — a buzzing spike train — while the highs sail through untouched | **Long `τ_m` + Bias ≠ 0 + Drive > 70**: DC-magnetised core saturates one polarity only → half-wave differentiated spike train, huge H2, DC wander the blocker fights continuously. Counter-intuitively you must turn **Low Cut DOWN and Emphasis NEGATIVE** to get there | Leakage-resonance `Q > 12` at Drive 100 self-rings into a fixed ~30 kHz tone that aliases down as a whistle. **Clamp Q ≤ 8** (BIBO). Non-leaky integrator ⇒ DC walks the core into permanent saturation |
| **Stomp Box** | `Rf` = 2.5 MΩ (the real pedal stops at 551 k): small-signal gain ~530, THD > 35 % **while crest stays ≈ 2.5** — a combination nothing else in the device produces | **Bias at either extreme + Drive > 85 + Snarl > 60**: one polarity clips at ~2.1 V and the other at ~0.35 V — violently rectified, buzzing, octave-up snarl. Above Snarl 70 the stage sits on the edge of parasitic oscillation and **screams**. Real TS behaviour | Snarl > 85 at Drive 100: loop gain exceeds unity in the LUT's steepest region and it locks into a self-oscillating squeal. Clamp small-signal loop gain ≤ 0.98 (BIBO); spend the top 15 % of Snarl right at that edge |
| **Overdrive** | Cascade gain **2 → 8000 (+78 dB)**; a −40 dBFS input becomes a full-scale square. Everything becomes a buzzing square wave, and it should be allowed to be exactly that | **The slew limit.** `dmax → 0.02` units/sample rate-limits everything above **153 Hz**: the output is a pure triangle whose amplitude is set by frequency alone. Stack Bias−(crossover deadzone) + Sag 120 + Recovery 0.3 ms and the rail collapses *inside* the audio period — the note gates itself at its own frequency | `dmax → 0` freezes the output at DC: **floor at 2e-4**. Cascade × Snarl > unity can lock into an fs/2 limit cycle: clamp Snarl ≤ 0.85/N. Both BIBO, neither restricts anything anyone wants |
| **Soft Clip** | +48 dB (×251). A −12 dBFS sine arrives at +36 dBFS; with the cubic anchor **97.6 % of every cycle sits at the rail** — a square wave, THD ≈ 43 %, crest → 0 dB, all dynamics gone | Drive 55–80 % (+24…+38 dB), Knee 20–40, Bias ±30 — screaming, fully saturated, still unambiguously pitched | Drive > 85 % with Emphasis > +70 and Hi Cut open: de-emphasis restores +18 dB of treble onto a square's `1/n` series → pure fizz, no fundamental. **Test on a 3-note chord** (fb265 lesson) — a summed chord above Drive 70 % becomes broadband IMD, not harmonics |
| **Hard Clip** | +48 dB: a full-scale sine spends `(2/π)·asin(1/251)` = **0.25 % of each cycle** below threshold. A literal square wave, THD 43.5 %, crest 0 dB. Nothing left to destroy above this — which is what makes it the right ceiling | Drive 40–65 % at Knee 0 (razor) — shredded, harmonically enormous, still tonal. Add Bias 40–70 for a fat asymmetric roar; add `Wrap` and it is genuinely unhinged while still tracking pitch | With **Quality = Off** above ~+24 dB on any note above C4, aliasing exceeds the intended harmonics (the BLAMP paper measures naive at **24 dB SNR** on a 4186 Hz fundamental). Legitimate lo-fi, but document it as "this is now aliasing, deliberately" |
| **Zero-Square** | **Drive is not the extremity axis — Knee is.** At Knee 0 the pedestal `p = 0.92`: output is essentially a pure square **whose amplitude is independent of the input** above −72 dBFS. Total dynamic annihilation, full-scale `1/n` odd series | Knee 30–60 (`p` 0.37–0.64), Drive 15–35 %, Hi Cut ~6 kHz — octave-fuzz territory, unmistakably the original note with a brutal edge welded on. Bias ±60 sweeps duty 5 %/95 % (free PWM) | **This one WILL ship broken if missed:** with `p → 1` and no gate, *any* nonzero input becomes a full-scale square — dither, denormals, a released voice's noise floor. The **−72 dBFS gate (`x_g = 2.5e-4`) is a stability requirement**, dithered so it does not chatter on a decaying tail. Set it at −40 dBFS instead and the tails die — that is the timid failure in the other direction |
| **Slew Clip** | `s = 0.0005` FS/sample = total sludge: nothing above **3.8 Hz** survives intact. With Drive 100 the limiter is asked for 251× more slope than it can deliver → a pure triangle at the note's fundamental with **zero timbral information from the source**. You have turned the synth into a triangle oscillator | `s = 0.02…0.08` (Knee 55–72) with Drive 30–60 %: squares → trapezoids, plucks lose their attack and gain a soft thud. Reads as heavily-overdriven vintage gear, *not* as a filter | `s → 0` collapses the output to DC. Floor the taper at 0.0005 — a stability clamp (a zero slew ceiling means the output can never change), not a taste clamp |
| **Diode 1** | +48 dB into a threshold that has simultaneously fallen to 0.105 — **64.5 dB past the clip point**. At Knee 0 the output is a full-scale square regardless of input. A lush pad becomes a buzzsaw organ | Drive 65–85 · Knee 0–25 · Asym ±40–70 · Low Cut ~120 Hz · Hi Cut ~6 kHz — Rat-into-a-failing-amp. Add Snarl 30–45. At Asym ±100 the far side's threshold is `0.02·V` ⇒ a one-sided **pulse train**, thin/honky/hollow after the blocker | Drive 100 + Knee 0 + Snarl > 80 + Low Cut min: the loop latches to a near-DC square, the blocker turns it into motorboating, pitch is gone. **Bounded** by construction (`|y| ≤ max(Vp,Vm)`), no NaN path — ship it |
| **Diode 2** | Dead Zone 0.95 FS: **only the top 5 % of the waveform survives.** With Auto on, those shards get pulled up tens of dB, so a sustained note machine-guns and then **cuts out entirely mid-decay**. It sounds broken. It is meant to | Dead Zone 60–85 · Drive 40–70 · Knee 0 (razor notch) · Asym ±20–40 · Hi Cut ~8 kHz — sputtering, gated, aggressive, still tracks pitch | Dead Zone > 0.95 on a quiet source is **total silence** — a real failure mode, and we must **not** clamp the knob to prevent it. What gets capped is the **makeup (+36 dB)**, or Auto chases silence and amplifies the noise floor into a hiss wall. *Knob free, gain capped* |
| **Asym** | Bias ±1.5 puts the operating point **more than twice the clip threshold** past zero — the shaper is already on the rail before any signal arrives, so only the largest peaks modulate it: a one-sided, gated **spit**. Even/odd swings from ~0 to > 1 | Asym 45–75 · Drive 30–55 · Knee 25–60 · Emphasis +20–40 — Tube Screamer / Klon at the bottom, "broken amp with a failing bias supply" at the top. The most gratifying region in the family | Asym ±100 + Low Cut min + Emphasis −100: the blocker chases a 0.4 FS envelope-modulated offset and even the 2-pole @ 20 Hz leaves an audible thump per note-on. **That thump IS the sound at that setting** — but it must be a consequence of the setting, never of a default |
| **Rectify** | `a = 2` — **over-rectified**: the positive lobe passes untouched while the negative is inverted and **tripled**. Perceived pitch stops being a clean octave and becomes an inharmonic clang | Asym 45–60 (full-wave) · Drive 25–50 · **Dead Zone 10–25** · Hi Cut ~7 kHz · Character = Octavia. The Dead Zone is the secret ingredient — it gates the octave so it only speaks on the attack, exactly like the real pedal. **+ the `Octave` pill = two octaves up (4·f₀)** — the most destructive single click in the device | Asym > 90 on a polyphonic pad with Emphasis +100 and `Octave` lit → inharmonic hash with no pitch centre. It is noise. **Ship it.** The trap that caps it too low is stopping at `a = 1` "because that is the correct rectifier" — half the drama lives between 1 and 2 |
| **Linear Fold** | **32 folds.** A sine becomes a 32×-rate triangle whose envelope still tracks the source: a screaming metallic buzz, unmistakably *pitched* at the source's fundamental, where every 1 dB of input rewrites the timbre | +18…+30 dB (4–16 folds) with Spacing 1.6 and Symmetry ±25 — a bass sounds like a Buchla, a pad like a broken bell | Past `g ≈ 128` (64 folds) the corner rate exceeds 16× fs for anything above ~150 Hz. **No oversampler helps** — the required bandwidth genuinely exceeds the oversampled rate; the output loses its pitch centre. 32 folds sits below that with headroom for the Emphasis tilt |
| **Sine Fold** | **31 folds, modulation index 99, a 111-harmonic Bessel comb.** A screaming sheet of metal that still tracks pitch exactly. Because the fundamental passes through **J₁ nulls** repeatedly across the sweep, the perceived pitch centre *jumps* during a Drive automation — authentic FM behaviour no clipper can produce | `a` 20–60 (6–19 folds) — a pad becomes a choir of broken bells. Put an LFO on Symmetry and it howls through vowel shapes | `a > 300`: the input traverses > 10 lobes per sample, ADAA's box kernel stops correlating with the band-limited result, and float phase resolution collapses. **Clamp `a` at 300** — 3× above the front-panel ceiling, so nothing musical is withheld |
| **West Coast** | **32 folds on an irregular ladder.** A jagged, resonant, formant-swept scream that reshapes completely with every note and velocity, because the uneven ladder means the harmonic pattern depends on *which* thresholds the peak reaches. Symmetry ±100 makes it howl through vowels | Peak 8–20 V, Stages 8–16, LFO on Symmetry at 0.3 Hz / ±40 %. **This is THE patch — the one that sells the device.** | ⚠️ **The authentic Buchla 259 tops out at FIVE folds** (last threshold 5.46 V, no sixth cell) — reached at the **halfway point** of our knob. Ship the circuit faithfully and you ship exactly the timidity this rule exists to prevent, with an academic citation attached. The **`259 Extended`** Character is the fix and it must be the **default**. Also: the authentic 1.33 kHz output pole makes 32 folds sound *muffled* — open it to 8 kHz on every non-museum voicing |
| **Shaper** | +48 dB into your own curve. If you drew a flat top you get a pure square; if you drew a 12-step staircase you get a **12-level quantiser at any input level** — an unstoppable 4-bit sound a real bitcrusher can only do at one level | Drive 70–100 % with **`Beyond` ≥ 50 (wrap)**: a full-scale sine traverses the drawn curve **256 times per half-cycle** — a hard-sync/ring-mod scream whose **pitch is set by Drive, not by the note**. Automate Drive and you get a rising inharmonic siren locked to nothing | Traversal rate exceeds the table: `|u[n]−u[n−1]| > 1.0` in curve units for > ~30 % of samples. Past that you are aliasing the LUT itself — broadband white noise, no pitch. **Do not clamp it** (a legitimate noise-source patch); the alias meter turns red and Quality auto-floors to Ultra so it is at least the *best* version of that noise |
| **Shaper Asym** | Same +48 dB, asymmetric so the destruction is different **in kind**: with the negative half drawn at zero, the output is a positive-only pulse train — DC-blocked, so what you hear is a razor buzz an octave up with a huge thump on every attack | **Bias ±0.85…±1.0 with Drive ≥ 60 %.** At Bias ±1.0 the *entire* signal sits in one half of the curve and the other half is unreachable — a symmetric drawing becomes a one-sided gate. With `Beyond` ≥ 50 the wrap boundary lands mid-waveform: chaotic, velocity-dependent buzz | Bias ≥ 0.95 + Drive ≥ 90 % + a drawn jump within 0.05 of the operating point: the signal dithers across a vertical wall every sample, the ADAA escape branch fires constantly (input barely moves while output jumps) → full-scale white crackle. Mitigate with the `Smooth` floor (`ω ≥ 2Δ`) and `ε = 1e-4` in this mode; beyond that, let it be ugly |
| **Harmonics** | All 16 bars up: `T₁₆` has **16 lobes across [−1,1]**, so the curve *is* a 16-fold wavefolder before `Beyond` is touched. One sine in → a 16-harmonic wall at equal amplitude | **Bars 12–16 raised with bars 1–4 at zero** (no fundamental at all) + Drive ≥ 50: the output has **no energy at the played pitch** — a pure high-harmonic ring 1.5–4 octaves up. On a chord, a shrieking metallic cluster that will genuinely hurt at full Mix. One bar-drag away | > 10 bars up **and** input < 0.3 **and** `Beyond` = wrap: the polynomial's lobes are traversed faster than the LUT resolves → pitchless hash. Allowed. **Hard limit (stability):** the `[−1,1]` domain clamp can never be defeated — `T₁₆(1.5) ≈ 4.3e7` and outside the domain Chebyshev polynomials produce NaN within one sample |
| **Table** | A **noise or transient-heavy frame** as the transfer curve: a 2048-point random walk is a chaotic, everywhere-steep map, so *any* input comes out as full-scale broadband noise whose only relation to the input is amplitude. **Total destruction with a volume pedal**, one drag-and-drop away | A formant/vocal table, Morph ≈ 0.3 with Env 2 at ±0.15, Drive ≈ 45 %, Smooth ≈ 30 % — a talking, vowel-shifting distortion that follows the envelope | A frame with `maxSlope > ~200` (transient-heavy PCM imports do this routinely) traverses > 200 cells per sample at moderate drive — ADAA becomes decorative and the output is pitchless. Detected free at bake time; auto-floor to Ultra and let it be loud |
| **Downsample** | `fs_r = 20 Hz` — a 50 ms hold period. The output stops being the signal and becomes a **20 Hz square-edged gargle**, 20 full-scale steps (= clicks) per second. Rhythmic digital tearing, ear-splitting on anything bright | `fs_r` **300 Hz – 3 kHz**. At 1 kHz on a 220 Hz note the images land at 780/1220/1780/2220 Hz — a bright inharmonic metallic cloud that completely rewrites the timbre while the note stays identifiable. At 300 Hz the clock's pitch replaces the material's | Below ~15 Hz the holds separate into discrete unrelated clicks — a click generator, not an effect. Hence the **20 Hz floor**. Also Feedback > 0.9 with Jitter > 60 % → broadband noise with no signal identity (bounded by `softClip`; a legitimate destination) |
| **Bitcrush** | **0.5 bits**, `D = 1.414`. Mid-tread output is only `{0, ±1.414}`: every sample with `\|x\| < 0.707` snaps to **digital zero**, everything else fires at ~+3 dB over full scale. A **sputtering full-scale pulse train** — a percussive digital gate that follows the source. Switch to mid-riser (`Zero-Square` Character) and you get the opposite: a constant ±0.707 hard square with zero dynamics | **3 → 1 bit.** 3 bits (8 levels) is classic 8-bit console grind with the note fully intact; 1.5–1 bit leaves the pitch and destroys everything else | Below ~0.4 bits (`D > 2.0`) nothing survives unless Drive pushes past full scale — silence punctuated by isolated ±2.0 spikes. So **0.5 bits is the floor and Drive is the way back out**: at 0.5 bits Drive stops being "more distortion" and becomes a **density** control on the pulse train. Clamp output to **±2.0, never ±1.0** — a ±1.0 clamp silently amputates the whole sub-bit range |
| **Overflow** | Threshold `t = 0.05` with Drive up: a single sine half-cycle crosses the wrap point **20+ times**, so the output is a hard sawtooth ramp whose repetition rate is set by the input's **slew rate** rather than its pitch — a full ring-mod/FM scream that tracks dynamics instead of frequency. **The most destructive setting in the device** | `t` 0.5 → 0.15 with Drive 6–18 dB: the peaks tear violently and the body stays intact — a transient-shredder that is genuinely mixable and is what most people will preset | `t < 0.03` with Feedback > 0.7 → white noise, no signal identity (bounded by `softClip`). Real risk: an **asymmetric wrap** (`Bounce`, `Two's Complement`) parks the output off zero and **will click on note-off** — the 10–20 Hz DC blocker is mandatory, not optional |

### 2.4 Ceilings that exist because the reference is timid

Three modes had to be pushed **past the thing they model**, and the reason is worth stating once:

* **West Coast** — the real Buchla 259 has 5 folding cells. Faithful = 5 folds = the top half of the
  knob does nothing. **Extend the ladder** at the circuit's own mean spacing (1.215 V) and cycle its own
  irregular slope pattern, out to 32 thresholds. `Stages = 5` gives you the museum piece.
* **Stomp Box** — the real TS-808 stops at `Rf = 551 k`. Ours goes to **2.5 MΩ**. And map Drive so the
  pedal's *minimum* gain (×12, +21 dB) is reached by Drive ≈ 5, leaving 95 % of travel beyond the real
  circuit — which is how you get *both* no dead first third and a ceiling past the reference.
* **Tape** — real tape **self-limits**, so an accurate model is polite by default and cranking Drive just
  compresses harder. Three counters, all required together: (a) **negative Bias** into the deadzone,
  (b) **Snarl → `c` → 0** so the loop is maximally lossy and lagging, (c) put the input pre-gain
  **outside** the `drive → a` mapping so `Slam` pushes `H` far past the Langevin knee, where the loop
  **corners** instead of rounding. If Drive only ever moves `a`, the model can never be made nasty.

### 2.5 Taper laws — where the drama actually lives

Extremity is half the job; the other half is that the travel *between* 0 and 100 is all live.

| Mode class | Taper the knob so… | Because |
|---|---|---|
| Everything | `driveDb = D_max·t^0.8`, so 25 % of travel is already ~40 % of the dB | The ear hears dB. Linear-in-gain wastes the top half |
| **Fold** | **fold count** is linear in the knob (`g = 1 + t·62`), and the readout shows it (`"+22 dB · 12 folds"`) | Fold count is a **floor function** of gain. A quadratic taper buys 0–2 folds in the low half and 3 in the top half — the centroid *staircases* instead of sweeping |
| **Overflow** | **LOG** taper — the *opposite* of everything else | The first wrap at 25 % is already a total transformation; wraps 20–32 are perceptually much closer together than wraps 1–4. Log spreads 1–4 across the first 40 % |
| **Bitcrush** | `bits = 0.5 + 15.5·(1−t)^2.6` | 16 → 8 bits is inaudible on almost anything. This puts audible crush at t ≈ 0.12, brutal by 0.5, 1 bit at 0.78, sub-bit for the last 20 % |
| **Downsample** | **Exponential in Hz**, 20 Hz → sampleRate (~11 octaves) | A linear rate taper spends half the knob between 48 k and 24 k, which is inaudible. Specify in **Hz, never a ratio of fs**, or a patch changes sound with the host rate |
| **Zero-Square** | Cap the pedestal at `p ≤ 0.92` | At `p = 1.0` the `(1−p)` contour term vanishes and **the Drive knob does literally nothing**. This is a *dramaticism* clamp, not a taste clamp — the residual 8 % is 22 dB down and inaudible as "squareness" while keeping Drive alive |
| **Diode 1/2/Asym** | Threshold **falls** with drive: `V = V0·(1 − 0.85·d)` | A fixed threshold means the first third of Drive does nothing on any normal-level material — the fb286 Diffusion failure. Verified: 5 % Drive already sits 7.7 dB past threshold |

### 2.6 The three ways this ships timid anyway

1. **`Auto` on by default, compensating fully.** It converts a 48 dB drive sweep into a timbre-only
   change and deletes the visceral "louder AND nastier" that users read as power. Serum and Vital have
   **no** output compensation at all; Decapitator makes it an opt-in button *and* adds a +20 dB Punish;
   only Saturn normalises by default, and it hands the loudness back through a **+36 dB** Level knob.
   → **`Auto` ships OFF**, and when ON compensates ~70 % on a **slow RMS** tracker (§4.2).
2. **Auto's tracker too fast.** At ~10 ms it level-compensates the ANALOG family's **Sag duck** away and
   Tube/Overdrive measure and sound like static curves — the one behaviour worth their CPU, erased by a
   constant. **~300 ms for Family A.**
3. **Implementing a Family A mode as its static curve "for now."** A Dempwolf triode without the cathode
   and blocking loops is an asymmetric tanh with extra steps; a Jiles–Atherton normalised by a
   program-dependent gain is a curve wearing a tape costume. The level-dependence metric reads **flat**,
   which is the objective proof of failure. Build the state in step 2, not "later."

---

## 3. THE central problem: aliasing

For the reverb the make-or-break engineering issue was **stability**. For distortion it is **aliasing**.
Everything in this device is judged against it.

### 3.1 Why it happens

Nonlinearity is **frequency-expanding**. A polynomial of order *N* applied to a sine at `f0` produces
harmonics up to `N·f0`. A hard clip produces a theoretically infinite series. Every harmonic above
Nyquist folds back into the audible band at an **inharmonic** frequency. That folded energy is not a
musical overtone — it is the metallic, gritty, out-of-tune hash that makes cheap digital distortion
sound cheap. Terrain has already fought exactly this class of bug (fb265: a summed chord clipping into
broadband IMD hash).

**The continuity order predicts everything.** How the curve meets its boundary sets the harmonic decay,
and therefore the entire budget:

| Discontinuity | Harmonic decay | Modes |
|---|---|---|
| C∞ (asymptotic / Bessel-limited) | exponential / compactly supported | Soft Clip, Sine Fold, Slew Clip |
| C1 (slope corner) | `1/n²` = **−12 dB/oct** | Hard Clip, Diode 1, Linear Fold, West Coast, Rectify |
| C0 (jump in the waveform) | `1/n` = **−6 dB/oct** | Zero-Square, Overflow, Bitcrush, Downsample |

A `1/n` mode is **6 dB/oct more aggressive at every harmonic, forever**, and oversampling buys only
6 dB per doubling against it instead of 12 — which is precisely why the Digital family is not
anti-aliased at all and why Zero-Square gets polyBLEP rather than brute force.

### 3.2 The two weapons

**1. Oversampling.** Run the nonlinearity at 2×/4×/8× so the generated harmonics have somewhere to go,
then band-limit and decimate. Powers of two because **half-band** filters (≈ half the coefficients zero)
are cheap in polyphase form. Multi-stage is standard: a strong filter first, weaker after — JUCE's own
max-quality polyphase-IIR design uses transition width 0.05 / stopband −90 dB on stage 0, widening to
0.10 / −80 dB later, which matches de Soras' HIIR guidance (`TBW[k] = (TBW[k−1]+0.5)/2`).

**2. Antiderivative anti-aliasing (ADAA).** Parker, Zavalishin & Le Bivic (DAFx-16), extended by Bilbao,
Esqueda, Parker & Välimäki. Instead of upsampling, it approximates the upsample→distort→downsample chain
in closed form using the antiderivative of the shaper:

```
first-order:  y[n] = ( F₁(x[n]) − F₁(x[n−1]) ) / ( x[n] − x[n−1] )
              fallback when |Δx| < 1e-5:  f( (x[n]+x[n−1]) / 2 )      // ill-conditioned
              fallback when |Δx| > 0.9 :  f( x[n] )                   // big jump ⇒ no dropout
```

Both fallback branches are **already written and audited in this tree** — `TerrainFilters.h`
`struct WaveShaper` (~line 990). Lift it wholesale; do not write a new one.

### 3.3 🔑 THE MEASURED MASTER RULE

Compiled against this repo's own JUCE (`_tools/JUCE/modules/juce_dsp`) and measured on this machine.
`SNR_A` = harmonic-bin energy ÷ non-harmonic-bin energy, 65536-pt Hann FFT, coherent bins.

**Hard clip ×6 at f0 = 1244.5 Hz:**

| Strategy | SNR_A | CPU (% of one core, stereo @48k) |
|---|---|---|
| 1× naive | 35.7 dB | 0.022 % |
| 2× naive | 52.2 dB | 0.156 % |
| **2× + ADAA-1** | **69.5 dB** | **0.299 %** |
| 4× naive | 64.1 dB | 0.417 % |
| **4× + ADAA-1** | **83.8 dB** | ~0.70 % |
| 8× naive | 75.9 dB | 0.928 % |
| 2× FIR + naive | — | 0.591 % |
| 8× FIR + naive | — | 1.823 % |

> **2× + ADAA-1 delivers +5.4 dB more than 4× naive at 72 % of its CPU.
> 4× + ADAA-1 delivers +7.9 dB more than 8× naive at ~75 % of its CPU.**

This single rule sets the entire budget and satisfies the CPU-friendly hard rule without ever paying 8×
broadly. It matches the literature (Chowdhury: "ADAA + modest oversampling beats either alone"; Lockhart
SMC-17 Fig. 7: "ADAA + 2× OS is on par with 8× OS, and ~4× cheaper").

### 3.4 ⚠️ v1's Fold row was WRONG — measured

v1 §2 said Fold gets "8× OS (or ADAA-2 + 4×)". Measured on the in-tree linear wavefolder
(`SynthVoice.h:4567-4587`) at f0 = 1244.5 Hz:

| Fold depth | 1× naive | 2×+ADAA-1 | 4×+ADAA-1 | 8× naive | 8×+ADAA-1 |
|---|---|---|---|---|---|
| a = 8 | 21.5 | 50.5 | 68.5 | 60.1 | 79.2 |
| a = 32 | −3.8 | 35.4 | 43.4 | 33.2 | 56.9 |
| a = 128 | **−8.1 dB** | 14.0 | 20.0 | **22.4 dB** | **47.3 dB** |

Two corrections fall out:

* **8× naive is NOT sufficient for Fold** — 22.4 dB at the extremity is unusable, so v1's baseline would
  have shipped a broken Fold at exactly the setting Max most wants to be usable. **−8.1 dB at 1× means
  the aliasing is LOUDER than the signal**: pitch destroyed, pure inharmonic hash.
* **ADAA-2 + 4× was never a real option** for the triangle folder — see 3.5.

→ **Fold = ADAA-1 + 4× baseline, auto-promoted to 8× above 16 folds.** Free, because latency is fixed
device-wide (§4.4), so promotion never re-triggers PDC.

### 3.5 ⚠️ ADAA-2 IN SINGLE PRECISION IS CATASTROPHICALLY BROKEN — measured

The second-order form needs a **nested** fallback (with `x̄ₙ = (x[n]+x[n−2])/2`, `Δₙ = x̄ₙ − x[n−1]`):

```
y²[n] = (2/(xₙ−xₙ₋₂)) · [ (F2ₙ−F2ₙ₋₁)/(xₙ−xₙ₋₁) − (F2ₙ₋₁−F2ₙ₋₂)/(xₙ₋₁−xₙ₋₂) ]
  primary fallback   |xₙ−xₙ₋₂| < ε :  (2/Δₙ)·( F1(x̄ₙ) + (F2ₙ₋₁ − F2(x̄ₙ))/Δₙ )
  secondary fallback |Δₙ|      < ε :  f( (x̄ₙ + xₙ₋₁)/2 )
```

Built exactly that and swept precision × tolerance on a hard clip at **f0 = 220 Hz** (low frequency =
tiny sample deltas = worst conditioning):

| Precision / ε | SNR_A @ 220 Hz |
|---|---|
| **float / 1e-5** | **22.0 dB** ← *37 dB WORSE than doing nothing* |
| float / 1e-3 | 58.9 dB (= identical to naive: the fallback fires constantly and the ADAA is silently deleted) |
| **double / 1e-8** | **70.9 dB** |
| double / 1e-5 | 70.9 dB |
| double / 1e-3 | 58.9 dB (over-eager fallback again degenerates to naive) |

*(1× naive baseline at 220 Hz = 58.9 dB.)*

**Policy:** **ADAA-1 is the v1 baseline everywhere. ADAA-2 is permitted ONLY at `Quality = Ultra`, ONLY
on modes whose `F2` is a closed-form polynomial (Hard Clip, Zero-Square, Rectify, Diode 2), and ONLY in
double-precision state with ε = 1e-8.** ADAA-1 is far more forgiving (one division, not three) and the
existing float/1e-5 implementations in-tree are fine as they stand.

⚠️ This reconciles a real disagreement: the CLIP and DIODE agents both argued ADAA-2 is nearly free
because their `F2` is polynomial — which is true of the *arithmetic* and irrelevant to the
*conditioning*. The measurement wins; the polynomial-`F2` argument survives only as the gate on *which*
modes may use it at Ultra.

### 3.6 Vicanek's kernel — a free upgrade over plain ADAA-1

Vicanek (*Note on Alias Suppression in Digital Distortion*, 2023/2024) shifts the convolution by half a
sample:

```
y[n] = (F½ − F₁)/(xₙ − xₙ₋₁) + (F₁ − F_{3/2})/(xₙ₋₁ − xₙ₋₂)
       F½ = F((xₙ+xₙ₋₁)/2),  F₁ = F(xₙ₋₁),  F_{3/2} = F((xₙ₋₁+xₙ₋₂)/2)
```

The last two are **this sample's copies of last sample's values**, so with two cached floats it costs
**exactly one new antiderivative evaluation per sample — identical to plain ADAA-1**. Two wins:

* Plain ADAA-1 puts a **spectral null at Nyquist** (aperture `|sinc(πf/fs)|`: **−2.64 dB at 20 kHz at
  1×**, −0.63 dB at 2×, −0.16 dB at 4×). Vicanek's is only ≈ −6 dB there. This is also why **ADAA must
  never run at 1×** (it audibly dulls) and why `Quality = Off` bypasses ADAA as well as oversampling.
* Its latency is **exactly 1 integer sample** (the transparent case reduces to the symmetric `(1,6,1)/8`
  3-tap centred at `n−1`) instead of ADAA-1's 0.5 fractional sample.

Use it on every memoryless mode.

### 3.7 🔑 THE AUTHORITATIVE PER-MODE ANTI-ALIASING BUDGET

A mode declares its **floor**; the `Quality` dropdown may only **raise** it. Never lower.

| Mode | Family | **Standard (the floor)** | High | Ultra | Why |
|---|---|---|---|---|---|
| Tube | ANALOG | ADAA-1 + **2×** | +4× | +8× | Softplus knee is C∞ but tight; state ODEs stay at **1×**, so 2× costs 2.2× the *shaper*, not the mode. 4× buys only ~6 dB more — refused |
| Tape | ANALOG | **4×**, RK2 | 4×, RK4 | 8×, RK4 | Stateful ODE, ADAA inapplicable. `dM/dH` collapses abruptly ⇒ only −12 dB/oct. **4× not 16×** because we fold the 55 kHz bias carrier into `k` instead of running it explicitly |
| Transformer | ANALOG | **4×** | — | 8× | The leakage resonance genuinely lives at **20–60 kHz** — at 1× it does not exist, at 2× it barely fits. Plus the differentiator applies **+6 dB/oct to every harmonic the saturator made** |
| Stomp Box | ANALOG | ADAA-1 + **2×** | +4× | +8× | Cheapest mode in the device. Its own **720 Hz pre-HP** starves the clipper of low fundamentals and does half the AA job for free |
| Overdrive | ANALOG | ADAA-1 + **4×**, soft slew | — | 8× | Two independent alias sources that **compound**: shunt clipping (near-square) and the **hard slew limiter** (a clamped slope is a nonlinearity). Stages cascade, so stage 2 aliases stage 1's aliases |
| Soft Clip | CLIP | ADAA-1 + **2×** | ADAA-2 + 2× | ADAA-2 + 4× | Best-behaved at low drive. ⚠️ **At high drive ALL sigmoids converge to `sign(x)`** — above ~+30 dB the floor is set by the DRIVE, not the curve |
| Hard Clip | CLIP | ADAA-1 + **2×** | **ADAA-2** + 2× | ADAA-2 + 4× | Measured (DAFx-16 polyBLAMP study): naive 34 dB @1661 Hz / 24 dB @4186 Hz; 2× → 42/34; 4× → 43/38. **2× buys ~10 dB, 4× adds only 1–4 dB more** — the whole argument for ADAA+2× over brute force |
| Zero-Square | CLIP | **polyBLEP-2** + ADAA-1 + 2× | polyBLEP-4 + 2× | polyBLEP-4 + 4× | Worst aliaser in CLIP **and the cheapest to fix** — we know exactly where the discontinuity is (`x = 0`) and its sub-sample position is a closed-form linear solve. Targeted beats blind |
| Slew Clip | CLIP | **1× — none** | 2× | 2× | It *removes* high frequencies. Expect > 50 dB SNR_A at 1× even on C8 — better than a 4×-oversampled hard clipper. Paying here would be spending Fold's budget |
| Diode 1 | DIODE | ADAA-1 (Vicanek) + **2×** | ADAA-2 + 2× | ADAA-2 + 4× | Governed by **Knee**, not Drive: at Knee 100 (tanh) it barely aliases at all; at Knee 0 it is `1/n` across the audible band |
| Diode 2 | DIODE | ADAA-1 + **4×** | ADAA-2 + 4× | ADAA-2 + 8× | **Two** slope discontinuities per zero crossing, and the artefacts are loudest at LOW level — i.e. during decays, where nothing masks them. Branches are pure polynomial so 4× costs about what 2× costs elsewhere. The one mode where 8× is defensible |
| Asym | DIODE | ADAA-1 + **2×** | ADAA-2 + 2× | ADAA-2 + 4× | Same harmonic *order* as Diode 1, only denser. Even harmonics fold to **different** sites than odd, but they concentrate at low frequency where ADAA's kernel null is most effective |
| Rectify | DIODE | **ADAA-2 + 4×** | — | ADAA-2 + 8× | Worst in its family: `\|x\|` has a **sign reversal** of the derivative — the corner is literally twice as sharp as a clip corner. Worked case: 5 kHz in ⇒ the 40 kHz component folds to **8 kHz at −34 dB**, mid-band and exposed. `F1`/`F2` are pure polynomials so ADAA-2 costs three multiplies |
| Linear Fold | FOLD | ADAA-1 + **4×** + polyBLAMP | ADAA-2 + 8× | ADAA-2 + 16× | Auto-promote to 8× above 16 folds. Because the ladder is perfectly **even**, aliases stack **coherently** into discrete out-of-tune whistles — subjectively the worst kind |
| Sine Fold | FOLD | ADAA-1 + **2×** | ADAA-2 + 4× | ADAA-2 + 8× | The one mode that must **not** be paid 8×. C∞ with a genuine cut-off: `n_max ≈ I + 2.5·I^⅓`, so the needed factor is **computable**, not guessable. **No BLAMP/BLEP** — no corners to correct |
| West Coast | FOLD | **Buchla:** ADAA-1 + 4× + polyBLAMP · **Serge/Lockhart:** ADAA-1 + 2× | +8× / +4× | 16× / **cap at 8×** | Split by Character. The Lambert-W voicings are C1-continuous (the diode softens the fold point) so BLAMP does not apply and 16× would make two Characters cost more than every other mode combined |
| Shaper / Shaper Asym | SHAPER | **curve-adaptive** (see 3.9) | +1 tier | ADAA-2 + 4× | The curve tells the engine how much AA it needs |
| Harmonics | SHAPER | **exact** — `OS = 2^ceil(log2(2·K·f0/fs))`, capped 4× | — | 4× | A sum of Chebyshev polynomials to order K produces harmonics up to **exactly `K·f0`** and not one hertz further. The engine *knows* the bandwidth |
| Table | SHAPER | **mip-retreat first**, then curve-adaptive | — | stop retreating, pay 4× | Band-limit the **nonlinearity** (free — the 34-level mip ladder is already in RAM) before oversampling the **signal**. Best CPU/quality trade in the device |
| Downsample | DIGITAL | **1× — none** | — | — | Aliasing IS the effect |
| Bitcrush | DIGITAL | **1× — none** | — | — | Aliasing IS the effect |
| Overflow | DIGITAL | **1× — none** | — | — | Aliasing IS the effect, and `1/n` decay means oversampling buys only 6 dB per doubling anyway |

**Use `juce::dsp::Oversampling` with `filterHalfBandPolyphaseIIR`, `isMaximumQuality = true`,
`useIntegerLatency = true`.** Measured latency at base rate: polyphase-IIR **3.14 / 4.43 / 4.95** samples
at 2×/4×/8× (integer mode 4/6/6) versus FIR equiripple **49.0 / 59.5 / 64.25**. The FIR costs double the
CPU and buys only linear phase, which is worthless here — the nonlinearity destroys phase coherence
anyway and the dry path is separately compensated (§4.4). **`juce::dsp::Oversampling` is used nowhere in
this tree today** (the only "Oversampling" hits are `TerrainFilters::needsOversampling()`, a bool flag),
but `juce_dsp` is already linked, so this is a new *use* of an existing dependency, not new code.

### 3.8 The `Quality` dropdown — and the one family where it means something else

`Off / Standard / High / Ultra`. `Off` is honest: **1×, ADAA disabled too** (not ADAA-bare — ADAA at 1×
costs −2.64 dB at 20 kHz and just dulls the sound). It lets you *hear* the aliasing deliberately, which
is a legitimate lo-fi sound.

⚠️ **For the DIGITAL family, `Quality` must NOT map to oversampling.** Oversampling a sample-domain
destroyer deletes the effect. Instead it maps to **reconstruction-filter order** (1-pole / 2-pole /
4-pole Butterworth / 8-pole windowed-sinc) used by the `Smooth` knob and the `Clean` pill. Standard costs
~6 flops/sample; Ultra ~40 and only when `Clean` or `Smooth` is actually engaged.

**Auto-promotion** (free, because latency is fixed): promote one tier when `Knee < 0.15`, `Snarl > 0.30`,
`Drive > 80`, the `Octave` pill is lit, `Beyond ≥ 50`, or fold count > 16. Costs nothing at typical
settings and stops the extreme settings sounding cheap — which matters more here than anywhere, because
the no-playing-safe rule means people **will** live at the extremes.

### 3.9 Curve-adaptive AA (the SHAPER family only)

Two statistics, free at bake time: `maxSlope = max|f[k+1]−f[k]|/Δ` and `TV = Σ|f[k+1]−f[k]|`.

| Condition | Floor |
|---|---|
| `maxSlope ≤ 4` **and** `TV ≤ 4` | ADAA-1 @ **1×** — a gentle drawn saturator costs less than tanh |
| `maxSlope ≤ 16` **or** `TV ≤ 12` | ADAA-1 + **2×** (the working default) |
| any jump discontinuity within `\|u\| < 0.15` of the origin, or `Beyond ≥ 50` | **ADAA-2 + 4×** |

A zero-crossing gate fires on **every cycle of every note** — the single most alias-generating thing a
user can draw — and it is detectable for free. This is the CPU rule stated exactly: the curve tells the
engine how much anti-aliasing it needs, so nobody pays 8× by default.

### 3.10 The perceptual gate — grounded in audibility data

Lehtonen, Pekonen & Välimäki (JASA 132(4), 2012) measured that aliased components above the fundamental
must be attenuated by **0 / 19 / 26 / 27 / 32 / 41 dB** to be inaudible at f0 = 415 / 932 / 1480 / 2093 /
3136 / 3951 Hz. Worst case **41 dB**. Therefore:

* **Glue gate** — at Drive ≤ 35 %, `SNR_A ≥ 60 dB` at all three test fundamentals (≈19 dB of margin).
* **Working gate** — at Drive 70 %, `SNR_A ≥ 45 dB`.
* **Extremity** — at Drive 100 %, **no pass threshold. Record the number.** Fail only on **collapse**,
  `SNR_A < 20 dB`, the point where hash overtakes the harmonics and the note loses its pitch.
* **DIGITAL is exempt and asserts the inverse:** with `Clean` OFF, `SNR_A` must be **low**.

⚠️ **Judge the budget at the extremity, never at polite settings.** At Drive ×1 every strategy reads
88–116 dB. The differences that matter only appear past ×8, and the mode-defining ones only at ×32–×128.
If the harness runs at a default drive, Fold passes at 2× and ships at −8.1 dB.

---

## 4. The engineering musts

### 4.1 DC blocker after every asymmetric mode — MANDATORY, and the in-tree one is wrong

Asymmetric shaping (Tube grid bias, Diode, Asym, Rectify, Stomp Box, anything with `Bias ≠ 0`) generates
a **DC offset**. Left in, it silently eats headroom, shifts the limiter's working point, and produces a
**click on note-off** when the amp env cuts a signal sitting off zero. Direct violation of the
no-clicks hard rule, and it is the *default* behaviour of an asymmetric shaper, not an edge case.

**Rule: a one-pole high-pass at ~10 Hz sits after the nonlinearity on every asymmetric mode.**

⚠️ **`TerrainFilters.h:69` `struct DCBlocker` hardcodes `y = x − xPrev + 0.995f·yPrev` with no
sample-rate awareness.** Its corner is `fs(1−R)/2π` = **35.1 Hz @ 44.1k, 38.2 Hz @ 48k, 76.4 Hz @ 96k** —
not the ~10 Hz this section specifies, and it **changes character with sample rate**.

⚠️ **AUDIT CORRECTION (2026-08-11) — these are NOT "three copies of the same constant."** Verified by
reading: `TerrainFilters.h` uses **0.995** (38.2 Hz @48k), `ModalEngine.h:332` `struct DCBlock` uses
**0.9985** (11.5 Hz @48k), and `TapeMachines.h`'s is a proper ~10 Hz one-pole. Three *different* corners.
**Do NOT consolidate them onto one coefficient** — that would move ModalEngine's corner up by 27 Hz and
change the shipped, approved modal sound. The correct fix is to make each one **sample-rate-aware while
PRESERVING ITS OWN 48 kHz corner**, so 44.1/48k are unchanged and only 96k (where TerrainFilters' corner
currently doubles to 76 Hz) is repaired. Share the *implementation*, not the coefficient:

```
R = 1 − 2π·fc/fs        // fc = 10 Hz  →  0.998575 @44.1k · 0.998691 @48k · 0.999346 @96k
```

**Escalation rules:**

* `|Bias| > 0.5` (Asym) or any Rectify setting ⇒ **two cascaded 1-poles @ 20 Hz** (`R = 0.997385 @48k`,
  τ = 7.96 ms each). At `|a| = 1` the offset is ~0.35 FS **and envelope-modulated**; a single 10 Hz pole
  (τ = 15.9 ms) leaves an audible thump on every note-on.
* **Rectify needs TWO blockers, and the position of the first one is the mode's biggest trap.** A
  full-wave rectified full-scale sine deposits **0.6366 FS of DC**. If that reaches the clipper, the
  whole waveform sits on one rail, the clipper only ever sees one polarity, and the output is a **flat
  line with ripple** — the mode sounds broken and the bug looks like a gain-staging problem. Block it
  **mid-chain, before the clip stage, inside the oversampled region**, then again after.
* **Prime the state, don't let it charge from zero.** On a curve/bias version bump, seed the blocker's
  state with the steady-state offset `f(bias)` — otherwise every big Bias move thumps.
* ⚠️ **`SynthVoice.h:2911-2916` contains a bug — do not copy it.** It does
  `y = tanh(x·drive + bias)·invSat − bias·invSat`. Exact DC removal requires subtracting **`f(bias)`**,
  the shaper's output at zero input, **not `bias`**. At the local bias of 0.15 the error is invisible
  (tanh 0.15 = 0.1489); at our ±1.0 range it is **~0.24 of full scale** — a guaranteed note-off click.
  **Correct form for every mode: `y = (f(g·x + b) − f(b)) · makeup`**, then the 10 Hz HP as belt-and-braces.

### 4.2 `Auto` output compensation — universal pill, **default OFF**

Drive raises level, and the ear reads "louder" as "better" — you cannot judge tone, and DRAMATICISM
becomes untestable because every A/B is confounded. But **full normalisation is the timidity culprit**
(§2.6). Four rules:

1. **Default OFF.** Serum and Vital have no compensation at all; Decapitator makes it opt-in and adds a
   +20 dB Punish button; only Saturn normalises by default and hands the loudness back through a
   **+36 dB** Level knob.
2. **Partial when ON:** `makeup = shaperRmsGain^(−0.7)` — ~70 % compensation, so 100 % Drive still
   audibly gets **louder as well as nastier**.
3. **RMS / loudness-matched, never peak-matched.** A clipper *raises* RMS while *lowering* peak, so
   peak-matching makes the wet signal audibly **smaller** than dry and every A/B reads as a regression.
   For FOLD it must be a **loudness** match (LUFS-style, ~400 ms window) — a bounded folder has
   near-constant RMS at every drive, and its peak is pinned at ±1 from the first fold, so a peak-matched
   Auto does **literally nothing** across 90 % of the travel.
4. **~300 ms tracker.** `HarmonicEngine::postProcess` uses ~10 ms; at that speed it level-compensates the
   ANALOG family's **Sag duck** away and Tube/Overdrive measure as static curves. **Recycle the tracker,
   change the constant.** Also cap the makeup: **+12 dB** for DIGITAL (or Auto chases silence at sub-bit
   settings and amplifies the noise floor into a hiss wall) and **+36 dB** for Diode 2's dead zone.

**Harness note:** the harness level-matches its own measurements **offline**, independently of the pill.
Do not conflate the two — a 70 % shipping compensation is not a measurement tool.

### 4.3 Pre-emphasis / de-emphasis — what makes `Emphasis` a real param

Filter *before* the nonlinearity and you change **which frequencies distort**; the inverse filter after
restores the balance. The pair does **not** cancel, because a nonlinearity sits between them — that
non-cancellation is precisely the effect. Classic tape technique, and the reason a real tape machine
distorts highs differently from lows.

Three honestly-distinct tone controls instead of three redundant EQs:

* **Low Cut** — **pre** the shaper. Decides which lows are allowed to hog the curve at all.
* **Hi Cut** — **post** the shaper. Tames fizz *without changing what distorted*.
* **Emphasis** — the pre/de-emphasis **pair**, ±18 dB of tilt hinged at ~1–1.5 kHz. Changes *which
  frequencies distort*, then untilts. ±18 dB is deliberate; ±6 dB would be the timid version.

On **Transformer** this knob is inverted in spirit: to reach the extreme you turn Low Cut **down** and
Emphasis **negative** — the core needs bass to saturate. On **Fold** it is unusually powerful, because
tilting up pushes highs across more thresholds than lows, making the **fold count frequency-dependent** —
something no downstream EQ can do, so it does not duplicate the Terrain Patcher.

### 4.4 ⚠️🔑 LATENCY vs the fb305 send maths — the trap nobody would catch until it sounded wrong

**This is still the highest-risk item in the document, and the research made it worse before it made it
better.**

Three verified facts:

1. **`setLatencySamples` is never called anywhere in `PluginProcessor.cpp`** — the plugin reports zero
   latency today.
2. `ConvolutionReverb.h:166` exposes `int getLatency() const { return B; }` (B = 512) and **it has zero
   callers**. The convolution reverb runs 512 samples late and the host is never told. It survives only
   because a reverb's wet is diffuse noise, so a delayed wet does not comb against the retained dry. **A
   distortion's wet is dry-plus-harmonics. It will comb.**
3. **The real breakage is NOT where v1 said it was.** Re-reading `PluginProcessor.cpp:6979` and `:7111`,
   the `− duck·sgL` dry-removal term uses the **current** sample and stays correctly aligned regardless
   of internal latency — that part is safe. What actually breaks is:
   * **(a) Mix < 100 % combs.** The insert `leftChannel[i] += wet·processed − duck·sgL` retains
     **undelayed dry** and adds **delayed wet**.
   * **(b) Any device DOWNSTREAM in the fb307 drag order loses its fb305 exclusion**, because
     `sgL = leftChannel[i] − rtdL` subtracts an **undelayed** `rtdL` from a `leftChannel` the distortion
     has just delayed by N.

**Quantified — even 4 samples is fatal.** Residual `|H| = 2|sin(πfN/fs)|`:

| N | Routed-osc leak | @1 kHz | @5 kHz |
|---|---|---|---|
| 0.5 (ADAA-1) | 0 dB above 16 kHz | −23.7 dB | −9.7 dB |
| **4** (2× polyIIR) | **0 dB above 2 kHz, +6 dB above 6 kHz** | −5.7 dB | **+5.7 dB** |
| 6 (4×/8× polyIIR) | 0 dB above 1333 Hz | −2.3 dB | +5.3 dB |
| 49 (2× FIR) | 0 dB above 163 Hz | — | — |
| 512 (convolution) | 0 dB above 16 Hz | +4.8 dB | — |

At N = 4 the oscillator that fb305 is supposed to remove from the main send comes back **louder than the
original above 6 kHz**. And the Mix comb with the existing equal-power sin/cos crossfade: at Mix = 50 %
dry and wet are both 0.707, giving **total cancellation** at the notches — 3 nulls inside the audio band
for N = 4 (first at 6 kHz), 5 for N = 6, 40 for N = 49. **The comb is worst at LOW drive**, where
wet ≈ dry — i.e. loudest in the warm-glue setting that is supposed to be the safest use of the device.

#### 🔑 THE FIX — fixed latency, internally compensated, reported as zero

> **The device's latency is a compile-time constant of 8 samples for every mode, every Character and
> every Quality tier, including `Off`. `setLatencySamples` is never called at runtime. The host is told
> **0**.**

* **Why 8:** JUCE's polyphase-IIR integer latencies are 4 / 6 / 6 at 2× / 4× / 8×, and ADAA adds 0.5–1.0.
  8 covers everything with headroom.
* **Why fixed:** a Type-dependent latency re-triggers host PDC — a glitch or transport stall in most
  DAWs — and silently breaks the fb305 subtraction for as long as the host takes to re-latch. It also
  clicks, disables automation in some hosts, and **fails outright for VST3 in Studio One**.
* **How to pad:** `Oversampling::setUsingIntegerLatency(true)` (JUCE inserts a Thiran `DelayLine`
  internally — `juce_Oversampling.h:213`, `.cpp:760-770`), then top up with a plain integer delay.
* **How to compensate:** at the distortion's position in the chain, apply **one stereo 8-sample delay to
  the dry side** of the insert. Since `delay8(left) − duck·delay8(sg) = delay8(left − duck·sg)`, it is a
  single delay line on the existing expression. Then **delay the send-bus reads by 8 for every device
  downstream** in the drag order.
* **Why report 0 rather than 8:** for an *instrument*, an unreported 0.167 ms shift has nothing to phase
  against. Reporting 8 would force an always-on master delay and **break the byte-identical default** for
  0.167 ms of textbook correctness — a bad trade. **With the distortion powered OFF, no delay line is
  engaged anywhere**, so fb303/305/307's default-sound guarantee survives intact.

⚠️ **Agents disagreed here** — CLIP and DIGITAL wanted 0 reported *and* no padding (bypass the resampler
entirely for the digital family); DIODE wanted 1–2 reported; ANALOG and FOLD wanted a fixed 4×-worst-case
number reported. **The fixed-8/report-0 design is adopted** because it is the only one built on having
read the actual fb305 code *and* measured JUCE's oversampler latencies, and it is the only one where the
number never moves when the user changes Type.

**Separate small commit:** fix `ConvolutionReverb`'s 512-sample latency to participate in the same
mechanism rather than remain a silent outlier.

### 4.5 ⚠️ A THIRD DEVICE BREAKS fb305 UNLESS TWO EXACT LINES ARE EDITED

`PluginProcessor.cpp:6979` and `PluginProcessor.cpp:7111` are byte-identical and both read:

```cpp
const float rtdL = ((rvbSendL ? rvbSendL[i] : 0.0f)
                  + (dlySendL ? dlySendL[i] : 0.0f)) * outputGain * kVoiceToFxPad;
```

They sum **only** the reverb and delay send buses. The moment a `distortionSendBuf_` exists (alongside
`reverbSendBuf_` at `PluginProcessor.h:1515` and `delaySendBuf_` at `:1539`), an osc routed to the
distortion by its pills will **NOT** be subtracted out of the reverb's or the delay's main send — so it
gets its distortion bus **AND** the reverb main send. That is exactly the fb305 bug Max already had
fixed once, reappearing through a path nobody edited.

**Required, in the same commit that creates the send bus:** add `+ (dstSendL ? dstSendL[i] : 0.0f)` to
both lines (and the R twins at `:6981` / `:7112`), and give the distortion's own main-send branch the
symmetric three-way subtraction.

**Also:** `SYN_FX_ORDER` (`ParameterIDs.hpp:403`) **was** a bool — `false = Reverb→Delay`. Three devices
means **6 permutations**, so it had to become a choice/permutation index, and each device's
downstream-delay amount (§4.4) is derived from that order.

> 🔧 **[CROSS-BIBLE AUDIT 2026-08-14] STATUS UPDATE — this paragraph is history, not a to-do.**
> fb341 already did it: `SYN_FX_ORDER` is now an `AudioParameterChoice(6)` (declared
> `PluginProcessor.cpp:3488`, clamped `:5860`, dispatched by the 6-case switch `:7383`). 🛑 **And it
> can never be re-declared at any other size** — choice cardinality is fixed at birth (fb342 session
> law ①). A 4th device therefore does **not** "bump the permutation": see `FX-CHAIN-BIBLE.md` §3.4,
> which is the authority — the param is retired behind a `fxChainOrder` rank property and left
> registered as the migration table. Any remaining "promote `SYN_FX_ORDER`" wording in this file
> (e.g. §4.5 build-order notes) refers to work that is **already done**.

### 4.6 No clicks, no crackle — the five that will bite

1. **Per-sample param glide on everything.** Recycle `DelayEngine.h`'s `xC += (xT − xC) * smth` idiom
   (~15 ms) and `HarmonicEngine::postProcess`'s **30 ms drive glide** (`forgeZ_ += fgDK_*(dT − forgeZ_)`),
   which guarantees an LFO square or a hard knob jump can never burst.
2. **⚠️ ADAA state hygiene.** **Park and reseed the ADAA history on every bypass↔active and Type change**,
   or the first sample after re-entry is `(F(x) − 0)/x` — an onset spike. This is already documented
   in-tree (`HarmonicEngine::postProcess`: *"clean bypass — park state so re-entry reseeds (ADAA history
   gotcha)"*) and in `SubOsc.h:63-82` (state seeded with the **first input sample, never 0**). Reuse the
   fix; do not rediscover it.
   ⚠️ **The cache trap:** `SynthVoice::applyFoldADAA` (`SynthVoice.h:4607`) caches `F1(x[n−1])` and
   reuses it — valid **only** because shape/amount are pushed once per block. Our Drive/Knee/Bias are
   smoothed **per sample**, which invalidates that cache on every sample. **Budget two `F1` evaluations
   per sample, not one.** Copy the cache logic unchanged and you get a stale-curve `F1` and a low-level
   buzz **that only appears while a knob is moving** — the hardest possible bug to find.
3. **Type / Character switch dips the wet through 0** (fade-toggles rule), and any table swap crossfades
   over 40 ms (§6).
4. **Denormals.** `juce::ScopedNoDenormals` plus `DelayEngine.h`'s `flush()` idiom on every recirculating
   state. The Sag/Recovery integrator at 500 ms and the Transformer's 2 s leaky integrator are textbook
   denormal traps as a note decays; so are the DIGITAL family's `Smooth`/filter states on silence.
5. **Solver guards must fade, not zero.** ChowTape's guard is `if (isnan(M) || M > upperLim) { M = 0; }` —
   which converts an instability into a hard **DROP-OUT, i.e. an audible click**. Ours must HOLD the
   previous state and cross-fade the wet to 0 over ~2 ms.

**Transcendentals:** no `std::exp`, `std::tanh`, `std::pow` or `std::sin` in the audio loop. Use
`TerrainFilters.h:42` `fastTanh` (Padé `x(27+x²)/(27+9x²)`, clamped ±5) for every tanh and as the basis
for `coth = 1/tanh` with the `|Q| < 1e-4` branch; `HarmonicEngine.h:437` `fastExp2` for dB/cents;
`SubOsc.h:93` / `HarmonicEngine.h:445` `lncosh` (`a + log1p(exp(−2a)) − ln2`) for tanh's antiderivative.
The Jiles–Atherton Langevin is the hottest transcendental in the device — RK2 evaluates it **twice per
oversampled sample, i.e. 8× per output sample at 4×**.

---

## 5. Architecture — mapping 23 modes onto the locked chassis

Chassis is frozen (`terrain-instrument-fx-back-panel-official-spec`). **11 params per device:**
3 knobs + Mix on the front, 8 on the back, plus 2 back dropdowns and 2 front pills. Nothing here
changes that.

### 5.1 The Type dropdown — flat list, grouped by family

One real `<select>` with `<optgroup>` headers (never click-to-rotate). **23 modes, 6 groups.** The
optgroup header is the family name; selecting a mode swaps the back-8 **only if the family changed**.

```
┌ ANALOG ─────────┐   Tube · Tape · Transformer · Stomp Box · Overdrive
├ CLIP ───────────┤   Soft Clip · Hard Clip · Zero-Square · Slew Clip
├ DIODE ──────────┤   Diode 1 · Diode 2 · Asym · Rectify
├ FOLD ───────────┤   Linear Fold · Sine Fold · West Coast
├ SHAPER ─────────┤   Shaper · Shaper Asym · Harmonics · Table
└ DIGITAL ────────┘   Downsample · Bitcrush · Overflow
```

**The whole architectural trick:** the **BACK-8 is keyed to the FAMILY**, not to the mode. Six param
sets serve 23 modes. Switching Tube → Tape does not move a knob; switching Tube → Hard Clip swaps the
whole back panel once. That is how a 23-mode roster fits an 11-param chassis without either becoming a
soup of near-identical labels or forcing 23 relabel maps.

### 5.2 The cuts, and where their sounds went

Four modes were cut on the evidence. **No user need is lost — one dead type each is.**

| Cut | Why | Where the sound lives now |
|---|---|---|
| **Soft Sat** | Fails the Family A thesis **definitionally** — memorylessness *is* a constant harmonic profile. And it has **no extremity ceiling**: tanh's THD asymptotes at the square-wave limit (~43 %) and past `k ≈ 50` **nothing further happens**, so the top 40 % of its Drive is measurably dead. A mode with no edge is what this device must not ship | Tube · Character **"Vari-Gain Hi-Fi"** · Stomp Box · Character **"Transparent (Timmy)"** · Soft Clip · Character **"Glue"**. All three are *gentler* than Soft Sat **and still alive**, because they still have state or band shaping |
| **Sine Shaper** | It is `sin(a·x)` with `a ≤ π` — a strict subset of Sine Fold's bottom octave. At its ceiling THD is ~8 % and harmonics stop by the 5th; **it is incapable of being dangerous.** Also a **no-doubles violation**: `SynthVoice.h:700` already ships an oscillator warp mode literally named "Sine Shaper" | Sine Fold · Character **"Shaper"** (`a` capped at π, Stages pinned to 1) |
| **Custom** | Byte-identical to Shaper Asym for an identical drawing — same transfer function, same aliasing profile, same ceiling. Two dropdown entries producing identical audio is the exact failure the brief says to avoid | Absorbed into Shaper Asym; the *capability* becomes **device-wide "Send To Shaper"** (§6.6), which is strictly more powerful — a Custom mode starts from a blank graph, Send To Shaper starts from any of 23 real curves |
| **Console** | v1 flagged it as the mode most likely to fail dramaticism, and no research agent found a mechanism that separates "desk glue" from a gently-driven Tube, Stomp Box or Soft Clip. Its own back-8 (`Glue · Crosstalk · Drift · Headroom`) is a subset of ANALOG's | Tube/Vari-Gain, Stomp Box/Transparent, Soft Clip/Glue, Tape/Studer at low drive. **This answers v1 open question #2** |

The freed slots paid for **Harmonics** (Chebyshev spectrum-drawing — nobody else ships it), **Overflow**,
and **Slew Clip**.

### 5.3 Front — 3 + Mix

`Drive · [signature] · Tone · Mix`. The middle knob relabels **per family** (Model A slot reuse):

| Family | Signature knob | Range | What it *is* |
|---|---|---|---|
| ANALOG | **Bias** | −100 … 0 … +100, centre-detented | Physically the **same control in all five circuits** — triode grid bias, tape record bias, transformer core DC magnetisation, pedal diode offset, amp output-stage bias |
| CLIP | **Knee** | 0 (razor) … 100 (round) | The one control meaning something specific *and different* in every mode of the family: sigmoid anchor blend · literal knee width `w = 2·(K/100)` · pedestal height `p = 0.92(1−K/100)` · slew ceiling |
| DIODE | **Asym** | −100 … 0 … +100, detented | One knob, four physics: threshold imbalance (D1) · dead-zone offset (D2) · bias injection (Asym) · **rectification amount** (Rectify) |
| FOLD | **Symmetry** | −100 … 0 … +100, detented | The DC offset injected before the folder, DC removed after — the documented second timbral axis |
| SHAPER | **Morph** | 0 … 100 | Crossfades curve A → curve B. A real interpolated transfer function, not a wet/dry blend |
| DIGITAL | **Crush** | per mode | Relabels **Rate** (Downsample) · **Bits** (Bitcrush) · **Wrap** (Overflow) |

* **Drive** — universal, the amount. §2.2 law. Must be musical across its **whole** travel.
  **FOLD shows its achieved fold count in the readout** (`"+22 dB · 12 folds"`) — the dramaticism law
  says the sound must match the number; here the number can literally *be* the sound.
* **Tone** — post tilt. Universal, immediately gratifying.
* **Mix** — chassis-mandated. **100 % = fully wet** (hard rule; pad the send by `kVoiceToFxPad` per fb292,
  or the dry leaks back phase-inverted).
  ⚠️ **Rectify trap:** do NOT implement rectification as a blend at the device's Mix. Then full-wave
  never actually happens because you always hear the dry fundamental underneath, and the mode degenerates
  into "a bit of octave flavour". The rectifier must reach `a = 1.0` with **zero dry inside the mode**;
  Mix is the last thing in the chain and a separate concern.

### 5.4 Back — 2 dropdowns

* **d1 = `Character`** — **8 voicings per MODE** (not per family), mirroring the reverb. 23 × 8 = **184
  voicings**. These are where the depth lives; §9 lists every one.
* **d2 = `Quality`** — `Off / Standard / High / Ultra` (§3.7/3.8). Universal; a mode's baseline is its
  floor, and the DIGITAL family remaps it to reconstruction-filter order.

### 5.5 🔑 The six BACK-8 param sets

Shared APVTS slots `SYN_DST_P1..P8`, relabelled per family. **There is no `SYN_DST_*` or `SYN_SAT_*`
namespace in `ParameterIDs.hpp` today** — the whole ~24-param block is greenfield, which means the
WebSliderRelay 4-point binding trap applies in full (relay member + `withOptionsFrom` +
`WebSliderParameterAttachment` + JS read — miss one and it builds clean and silently no-ops).

#### ANALOG — the memory, exposed

| Slot | Label | Range | Role |
|---|---|---|---|
| P1 | **Low Cut** | 20 Hz – 1.2 kHz, exp | Pre HP. At max the entire low end bypasses the distortion |
| P2 | **Hi Cut** | 20 kHz – 400 Hz, exp | Post LP. At 400 Hz the output is a distorted telephone — usable, not a defect |
| P3 | **Emphasis** | ±100 → ±18 dB tilt @1.2 kHz | The pre/de pair (§4.3) |
| P4 | **Sag** | 0 – **150 %** | **THE family knob.** How far the operating point collapses under program level. Above 100 % it collapses further than any real circuit — deliberate |
| P5 | **Recovery** | 0.05 ms – 500 ms, log (4 decades) | The time constant Sag returns on. Its **bottom decade deliberately puts the sag corner inside the audio band** — sputter, motorboat, ring-mod. That region is the point of the knob |
| P6 | **Drift** | 0 – 100 % (±12 ms + ±0.4 bias) | Slow band-limited random wander on bias **and** a delay mod. **Bit-identical bypassed at 0** |
| P7 | **Drift Rate** | 0.02 – 30 Hz, log | Matched partner. Top end crosses out of "drift" into vibrato/FM |
| P8 | **Snarl** | 0 – 100 % → fb 0…0.85/N | One-sample feedback around the nonlinearity. Physically real in all five circuits; on Tape it maps to J-A `width` (`c = √(1−w) − 0.01`), on Transformer to the minor loop. **Top 15 % sits at the 0.98 loop-gain BIBO ceiling, where it screams** |

#### CLIP — the boundary, exposed

| Slot | Label | Range | Role |
|---|---|---|---|
| P1 | **Low Cut** | 20 Hz – 1.2 kHz, 12 dB/oct | Pre. Decides what hogs the transfer curve |
| P2 | **Hi Cut** | 600 Hz – 22 kHz (off at max) | Post |
| P3 | **Emphasis** | ±18 dB @1 kHz | The pair |
| P4 | **Width** | 0 (mid driven, **sides bypass entirely**) … 100 (matched) … 200 (sides +6 dB) | Mid/side **DRIVE balance**, not a widener. Clipping the mid while the sides pass clean is the modern master-bus move |
| P5 | **Bias** | ±100 → ±1.0 FS (a full clip-threshold's worth) | Even-harmonic generator; on Zero-Square it is **pulse width** (±0.9 ⇒ ~5 %/95 % duty). Safe designs stop at ±0.3 — that is the timidity being legislated against |
| P6 | **Gap** | 0 – 0.5 FS dead zone | Class-B crossover **before** the shaper. Distorts the QUIET parts. At max anything below −6 dBFS is silence: notes become stuttering torn fragments |
| P7 | **Punch** | ∓24 dB of drive swing on a 3 ms/80 ms follower | Transient-tracking **drive**, bipolar. Negative: transients escape clean, only sustain is destroyed. Positive: the attack is vaporised. A drive modulator, not a compressor |
| P8 | **Feedback** | 0 (exactly off, ADAA exact) … 0.95 loop gain | Nonlinear recursion — the Fuzz Face mechanism. Above ~0.75 it self-oscillates and screams on note-off |

#### DIODE — the junction, exposed

| Slot | Label | Range | Role |
|---|---|---|---|
| P1 | **Low Cut** | 20 Hz – 1.2 kHz | Pre, inside the OS region, before the drive gain |
| P2 | **Hi Cut** | 700 Hz – 22 kHz | Post, base rate, after the DC blocker |
| P3 | **Emphasis** | ±18 dB @1 kHz | The pair |
| P4 | **Width** | 0 (mono-sum into ONE shaper — correct for bass) … 50 (matched) … 100 (**±8 % Vf mismatch** + M/S) | The stereo topology of the junction. Decorrelation from the CLIPPING, so it survives mono-sum as a timbre change |
| P5 | **Knee** | 0 (razor, Schottky) … 45 (silicon) … 100 (germanium mush) | The diode's **ideality**. Morphs `f` **and its antiderivative** through three basis functions — exact, so ADAA survives the morph |
| P6 | **Dead Zone** | 0 … **0.95 FS** | The series/class-B gap. The **only expander in the device**. Absolute threshold on the post-drive signal, so Drive and Dead Zone **fight each other** — that fight is the instrument |
| P7 | **Slew** | Off … 0.25 FS/ms | Junction-capacitance edge limiter, post-shaper, inside the OS region. Only fast edges are touched — not a low-pass |
| P8 | **Snarl** | 0 … 0.98 | One-sample nonlinear feedback. Subharmonics, period doubling, chaos. Auto-promotes Quality above 0.30 |

#### FOLD — the ladder, exposed

| Slot | Label | Range | Role |
|---|---|---|---|
| P1 | **Low Cut** | 20 Hz – 1 kHz, log | Pre. Bass drives the most crossings and swamps the ladder. At 1 kHz: thin, spitting, telephone-through-a-fuzz |
| P2 | **Hi Cut** | 500 Hz – 20 kHz | Post. **Buchla Character boots at 1.33 kHz** — the circuit's own output pole, and the real reason the hardware 259 is usable |
| P3 | **Emphasis** | ±12 dB @1 kHz | Makes the **fold count frequency-dependent**. At +12 the top octave folds ~4× as often as the bottom |
| P4 | **Stages** | **1 … 32**, integer | The **length** of the ladder — how many thresholds exist at all. Distinct from Drive (how far *up* the ladder you reach). Not 1–8: 32 is where a 220 Hz sine at full drive produces a 28 kHz corner rate |
| P5 | **Spacing** | p = 0.5 (compressed) … 1.0 (even, detent) … 2.2 (expanded) | Argument power-warp — reshapes the ladder under a fixed signal. At 2.2 a note's **attack folds 30× and its sustain twice**: every note screams then collapses into a clean tone |
| P6 | **Rebound** | 40 % … 100 % (detent) … **160 %** | Reflection gain per fold `R^k`. Below 1 = the real diode circuits (warm, tapering, **free alias reduction**). **Above 1 = no hardware does this**: the curve amplifies as it folds and the outer folds slam the ceiling. `R^k ≤ 4.0` is the only clamp (BIBO — at R=1.6 a 32-fold ladder reaches 10⁶) |
| P7 | **Corner** | 0 % (razor cusp) … 100 % (fully radiused) | Sharpness of each fold. ~12 dB less alias energy at the same fold count when rounded. On Serge/Lockhart it maps to the real `R_L` (1 kΩ → 50 kΩ) |
| P8 | **Width** | 0 (mono) … 100 … 200 (**±5 % threshold skew** + 2× M/S) | Above 100 % it skews the fold **thresholds** between channels, so L and R cross different thresholds at different instants. **The only way a memoryless effect makes real stereo from a mono input** |

#### SHAPER — the curve, exposed

| Slot | Label | Range | Role |
|---|---|---|---|
| P1 | **Low Cut** | 20 Hz – 1.2 kHz, 12 dB/oct | Pre |
| P2 | **Hi Cut** | 400 Hz – 22 kHz | Post |
| P3 | **Emphasis** | ±18 dB @1.5 kHz | The pair |
| P4 | **Width** | −100 (mid driven, sides bypass) … 0 (**true no-op**) … +100 (sides +12 dB, mid −12 dB) | M/S **drive skew**, pre-shaper with the inverse re-matrix after. A drive-routing control, so it never phase-smears |
| P5 | **Smooth** | ω = 0.0039 (**hard floor = 2 table cells**) … 0.35, K ramps 1→4 | Corner rounding at **bake time** (free per-sample). Simultaneously the most musical control and the cheapest anti-aliasing control. A corner narrower than one cell is not a corner, it is a random alias generator |
| P6 | **Bias** | ±1.0 **full scale** | DC offset pre-curve. At ±1.0 the entire signal sits in one half and the other half is unreachable |
| P7 | **Beyond** | 0 = clamp · 50 = reflect · 100 = wrap, continuous | **The most extreme control in the family** (§6.4). Without it Drive asymptotes to a square and the top half is wasted; with it, Drive never stops rewriting the timbre. ≥ 50 forces ADAA-2 + 4× |
| P8 | **Squash** | 0 (bypassed, byte-identical) … 100 (~30 dB of levelling, 20:1) | 3 ms/80 ms pre-shaper leveller. Drive decides *how far* along the curve; Squash decides *how consistently* you get there. The direct fix for a beautiful curve only the loudest 6 dB ever reaches |

#### DIGITAL — the grid, exposed

| Slot | Label | Range | Role |
|---|---|---|---|
| P1 | **Low Cut** | Off (5 Hz) … 800 Hz, 12 dB/oct, default **OFF** | Pre. Keeps sub-bass out of the quantiser/hold |
| P2 | **Hi Cut** | 400 Hz … 22 kHz, default **OFF** | Post. Shapes the result; it cannot prevent the fold |
| P3 | **Bits** / **Rate** | 16 → 0.5 bits · or 20 Hz → sampleRate | **The second destruction axis**, in series after the front one. Relabels so a mode never shows the same label twice. **This is why Crush + Downsample need not merge** — you can stack both in one instance |
| P4 | **Smooth** | 0 % (raw staircase) … 100 % (fully band-limited) | Reconstruction. On Downsample it crossfades **ZOH → first-order hold** (sinc² aperture ⇒ images fall twice as fast) then applies an LP **tracking `0.45·fs_r`**. Ableton Redux's hard/soft axis and D16 Decimort's "Images filter" on one knob. **Order comes from `Quality`** |
| P5 | **Dither** | 0 (**RNG not called at all**) … **4 LSB** | TPDF **before** the quantiser. Decorrelates the error so a decaying tail **grains out** instead of sticking on a step and buzzing into an idle tone. Also a **partial anti-aliaser** — it converts folded harmonic energy into a flat floor at the documented +4.77 dB cost. 2 LSB is textbook; the top half goes past it deliberately |
| P6 | **Jitter** | 0 (exactly silent) … 100 % (±100 % deviation) | Instability of the **grid** — a different lane from Dither's additive noise. Deviates the ZOH increment / step size / wrap threshold |
| P7 | **Spread** | 0 (**byte-identical L/R — the DEFAULT**) … 100 (independent clocks + seeds + 200 % M/S) | Hard L/R decorrelation of the destroyer. The cheapest enormous stereo effect in the plugin. No mono-compatibility clamp at the top |
| P8 | **Feedback** | 0 … 100 % = loop gain **1.05** | Output back into the destroyer's input. Bitcrush → crude sigma-delta then chaos; Downsample → S&H chaos oscillator; Overflow → self-oscillation. Deliberately **above unity**; `DelayEngine::softClip` is what bounds it (stability, not taste) |

### 5.6 Front pills — `Auto` + one per family

**`Auto`** is universal (§4.2) — the `Freeze`-equivalent. The second pill is family-unique:

| Family | Pill | What it does |
|---|---|---|
| ANALOG | **`Slam`** | +20 dB **on top of** Drive, and lifts the internal headroom clamps to their BIBO limits. Total reach ≈ +50 dB (Overdrive +98). Precedent: Decapitator's Punish — the single most-used control on that plugin. **Rationale:** spanning 0…+50 dB on one knob leaves the useful 0…+12 dB region with no resolution; splitting it gives both. ⚠️ **Carries a hard requirement: every LUT in the family must be sized for the Slam-ON worst case**, or Slam degenerates into a hard clamp that sounds nothing like the mode |
| CLIP | **`Wrap`** | Instead of HOLDING at the rail, the pre-clamp value **teleports to the opposite rail** (`y = 2·frac((u+1)/2) − 1`). At Drive 100 a wrapped hard clip is a **modulo-sawtooth of the input** — a full-band screaming buzz that still tracks pitch. One `fmod`; reuses Zero-Square's crossing detector for polyBLEP verbatim. 20 ms crossfade on toggle |
| DIODE | **`Octave`** | Inserts a **full-wave rectifier + its own DC blocker BEFORE** the selected mode's shaper. Diode 1 + Octave = an Octavia; Asym + Octave = a Tone Machine; **Rectify + Octave = TWO octaves (4·f₀)**, because the mid-chain blocker re-centres the signal so the second rectification is not idempotent. **The most night-and-day pill in the device — one click and the pitch moves.** Rectification is a diode-only trick, so it is genuinely family-unique |
| FOLD | **`Track`** | Fold-depth **key tracking**: scales pre-gain by `(f0_ref/f0)^0.7` from the incoming pitch, so high notes fold less. Fold count sets the corner rate at `4·N·f0`, so a 32-fold setting that is glorious on a C1 bass is pure hash on a C6 lead. **Without it a wavefolder is a one-note effect**; it is also a free aliasing control (cuts the worst-case corner rate ~4× on the top two octaves) |
| SHAPER | **`Sym`** | Mirrors the positive half onto the negative, forcing odd symmetry. ON: pure odd harmonics, **mathematically zero DC** (the blocker can be bypassed). OFF: even+odd, DC-blocked. The same drawn curve reads as a hi-fi clipper or a fuzzed-out tube. Unique to a family that **owns its own curve** — no other family has a negative half to mirror. Forced ON and greyed on `Shaper` |
| DIGITAL | **`Clean`** | Engages a **4-pole pre band-limit at `0.45·fs_r`** — true anti-aliasing rather than post-hoc filtering. Turns raw hash into **band-limited lo-fi** (telephone / AM radio): two genuinely different, both-useful sounds. This is D16 Decimort's "Approximation filter"; `Smooth` is its "Images filter" |

**Matched-pair law:** any mode offering modulation exposes **both** depth and rate, or neither. ANALOG's
`Drift` + `Drift Rate` are a shared back-panel pair, so this is satisfied structurally.

### 5.7 Routing — inherited, no new work

Power ON + no pills = **main send** (whole synth through it). Pills = **own bus off the main send**
(fb305). Power OFF = bypass, routing disabled, **and no latency-compensation delay line engaged
anywhere** (§4.4). Serial position set by drag order (fb307). **Default: power OFF, dry init,
byte-identical default sound** — same as reverb and delay.

⚠️ `IndyFxChain.h` — the per-chop private FX chain owns its own copy of every global FX module. **A
Distortion device must be added there too**, or it silently vanishes on the chop path.

### 5.8 The core visual — the live transfer curve, with signal occupancy

Hard rule: *everything audible interacts visually*, and fb311 hardened it — the UI must be
**DRAMATICALLY** audio-reactive. The reverb core breathes with the tail; the distortion core draws **the
actual `f(x)` for the selected mode**, reshaping live as Drive / Bias / Symmetry / Knee / Character move.

⚠️ **A single peak float satisfies the letter of that rule and fails its spirit** — it gives you two dots
sliding on a static line. Instead:

* Publish a **24-bin occupancy histogram of the post-drive input `|x|`** over the last UI frame, plus
  `peak+`, `peak−` and output peak, as **one JSON string** from a single native (`getDistCurve`),
  following the `getReverbBloom` / `getOscLfoWave` precedent. Processor cost: one index + one increment
  per sample (~3 flops); bins decayed ×0.85 per read for inertia. ~200 bytes at 60 Hz = 12 KB/s.
* Draw the curve as **two layers** — a dim base path and a bright overlay whose **per-segment opacity is
  driven by the histogram bin under that segment**. The part of the curve the patch is actually *using*
  glows, and it glows harder where the signal dwells.
* Markers ride the curve at `x = ±peak`; shade the band between them.

Loud notes visibly sweep further into the bend. A quiet pad only lights the middle. Pushing Drive drags
the glow outward until it hits the edge and — in `Beyond` = reflect — starts lighting the curve
repeatedly. **No shipping plugin draws signal occupancy on its transfer curve.** Update via the existing
surgical attribute idiom (`shRefreshOne`), never a re-render (fixed-positions law).

---

## 6. The transfer-curve editor  *(NEW — the SHAPER family's engine)*

The differentiating feature of the whole device. Serum's X-Shaper is the nearest competitor and it has
**no Drive** (the manual: *"the drive knob is a 'morph' between the two waveshapes"*) and forces
symmetric-vs-asymmetric to be **two incompatible modes with different coordinate systems**. Bitwig's Grid
`Transfer` caps Drive at ±24 dB. Kilohearts `Shaper` has the right idea on overflow modes but only its
own factory shapes. **None of them** draw the live input excursion on the curve, modulate the curve's own
points, open a built-in mode's curve for editing, or antialias a user-drawn curve with ADAA.

We already own ~85 % of the machinery: the fb206 LFO shaper.

### 6.1 Representation — breakpoints + per-segment exponential-bias tension

**Not** cubic spline, **not** Bezier. Three reasons in order of weight:

1. **Monotonicity and no overshoot.** The house bias curve is strictly monotonic, hits (0,0) and (1,1)
   exactly, and has finite slope at both ends. A curve that overshoots between two user points adds
   harmonics the user did not draw, and a **non-monotonic transfer function** is a source of harsh,
   unpredictable IMD. The envelope-editor comment at `index.html:14736-14747` already records that this
   formula replaced a power curve for exactly this reason.
2. **One truth, three places.** The same formula is already in the LFO shaper (`index.html:23584`), the
   envelope editor (`:14738`) and the C++ bake (`PluginProcessor.cpp:8046`). A spline basis creates a
   fourth curve language and violates the recycle rule.
3. **Trivially integrable**, which is the whole ballgame (6.2).

```
y(t) = y0 + (y1 − y0) · (e^{Pt} − 1)/(e^{P} − 1),   P = −8c,  c ∈ [−1, 1]
```

**The bake costs one `exp()` per SEGMENT, not per entry** — since `e^{P(t+dt)} = e^{Pt}·e^{P·dt}` and
`dt` is constant within a segment. A 2048-entry bake over ~30 segments ≈ 30 exp + ~4k multiply-adds ≈
**6k flops**: negligible on the message thread *and* cheap enough to run at block rate for modulated
curves.

**The reverse direction — fitting a built-in mode's curve — has an exact closed form.** Given a segment's
true midpoint value `ym`, let `r = (ym − y0)/(y1 − y0)`. Since `shBias(0.5, c) = 1/(e^{P/2}+1)`:

```
c = 0.25 · ln( r / (1 − r) ),  clamped to [−1, 1]   // covers r ∈ [0.018, 0.982]; r = 0.5 → c = 0
```

That single line is what makes §6.6 possible.

### 6.2 🔑 ADAA on a LUT — store the antiderivative ONLY

Werner & Azelborn (DAFx23) prove ADAA applies to any piecewise-polynomial waveshaper including PWL.
Vanoli/Gabrielli et al. (DAFx25) then show you need **no symbolic antiderivative at all**: bake `f` into
a LUT, numerically integrate to get `F1`, read `F1` with interpolation. Their benchmark on 44,100
samples: analytic ADAA **495 µs** vs ADAA-LUT **162 µs** at `-O3` (**67 % faster**), with no measurable
SNR difference at K = 1024 or 8192.

**Three design calls the research settled:**

**(a) Table size K = 2048** (bipolar; 4096 for the symmetric magnitude-domain `Shaper`). **Do NOT reuse
`kLfoTableN = 256`** (`SynthLFO.h:29`) — an LFO table is read at 1–10 Hz, a transfer LUT is read every
sample. DAFx25 measured K = 1024 and K = 8192 as indistinguishable from analytic ADAA while K = 128 shows
quantisation noise; 256 sits in the risky middle.

**(b) ⚠️ NEVER linear-interpolate the `F1` table.** The KVR/Signalsmith trap: *"If you use linear
interpolation on the integral table, then it will act like nearest-neighbour interpolation for the actual
curve, giving you naff quantisation noise."* Measured curve-reconstruction error, RMS dB:

| K | linear | quadratic | cubic Hermite |
|---|---|---|---|
| 1024 | −52.1 | −65.9 | −67.7 |
| 4096 | −64.2 | −84.0 | −85.8 |
| 16384 | −76.3 | −102.7 | −103.9 |

Linear converges at only ~12 dB per 4× table growth (1st order); quadratic/cubic at ~19–20 dB (2nd
order). Because the baked `f` is **piecewise linear between cells**, the exact read is a quadratic:

```
s = |u|·K ; k = (int)s ; d = s − k ;
F1(u) = F1[k] + Δ·( f[k]·d + 0.5·(f[k+1] − f[k])·d² )
F2(u) = F2[k] + Δ·( F1[k]·d + Δ·( f[k]·d²/2 + (f[k+1] − f[k])·d³/6 ) )
```

This is **exact for the baked curve** (its derivative is exactly the linearly-interpolated `f`), i.e.
strictly better than the paper's linear read, for one extra multiply-add.

**(c) Store `F1` only; derive `f` as the table's own difference quotient** `f(u) = (F1[i+1] − F1[i])·K`.
This halves hot memory *and* removes a real defect: with separate `f` and `F1` tables the ADAA branch and
the low-slew escape branch **disagree slightly at the threshold**, producing a tiny discontinuity that
reads as grit on quiet material. Derived-`f` makes them algebraically identical inside a cell.

**Symmetry bookkeeping** (the symmetric `Shaper` mode): `f` is odd ⇒ `F1` is **even** ⇒ `F1(u) = F1(|u|)`
(one table, no sign branch); `F2` is **odd** ⇒ `F2(u) = sgn(u)·F2(|u|)`.

**Memory:** `F1` at float32 over K+1 cells ≈ 8.2 KB per curve; A + B double-buffered ≈ 33 KB; `F2` baked
lazily only at Ultra. **Peak ~192 KB**, 4× smaller than a single reverb tank, comfortably in L2. This is
an FX-rack device — **one instance, not per-voice**.

**Per-sample cost:** index (1 mul, 1 cvt, 1 sub) + quadratic `F1` read (3 mul, 2 add) + 1 sub + 1 div +
1 compare ≈ **12 flops** — **cheaper than `tanh`**, because it computes no transcendentals at all.
Multipliers: ×2 when Morph is strictly between 0 and 1, ×2 when `Beyond` sits between detents. Worst case
4 reads; both multipliers **gate to 1 at the endpoints**.

### 6.3 The drawn curve is NOT always the right tool

For **user-drawn** curves the LUT is right. But `Harmonics` synthesises a Chebyshev polynomial (which
*has* trivial analytic antiderivatives — and we use the LUT anyway, because DAFx25 measured the LUT path
as 67 % *faster* than analytic evaluation, giving one code path for the whole family). And for a curve
that arrives as breakpoints, Werner & Azelborn give the exact closed form: each segment's first
antiderivative is **piecewise-quadratic**, `P⁽¹⁾ⱼ(x) = Cⱼ + bⱼx + (mⱼ/2)x²`, with the constants `Cⱼ`
chosen for C⁰ continuity so the antiderivative has no jump. Zero table error, ~15 flops, no memory.

**Policy:** bake to the LUT (one code path, proven faster). Keep the closed form in the back pocket for
the day someone wants a zero-memory build.

### 6.4 The `Beyond` rule — where 100 % Drive actually lives

A drawn curve is defined only on `x ∈ [−1, +1]`. What happens past the edge is not a corner case — **it
is the extremity ceiling**, and it is the single knob that decides whether this feature is polite or
ruinous. Kilohearts proved the taxonomy (repeat / edge-hold / mirrored repeat); we ship all three on one
continuous knob and are honest about what they do.

* **HOLD (clamp, `Beyond` = 0).** The edge value extends forever — a conventional clipper. `G1` outside
  the domain is just linear: `G1(x) = F1(±1) + f(±1)·(|x| − 1)`.
* **MIRROR (reflect, `Beyond` = 50).** The input reflects, so the drawn curve is traversed back and
  forth: **a full wavefolder built out of your own drawn shape.** Still **exactly ADAA-able without
  extending the LUT**, using the decomposition our shipped `foldGtri`/`foldFlin` already uses
  (`SynthVoice.h:4567-4575`):
  ```
  G1(x) = n·A + s·( F1(u) − F1(edge) )
          u = fold(x),  n = completed-leg count,  s = ±1,  A = F1(1) − F1(−1)   // precomputed at bake
  ```
  Two floors, two multiplies, one table read.
* **WRAP (sawtooth, `Beyond` = 100).** Past ±1 the input jumps back to −1. Genuine **jump
  discontinuities**, which DAFx23 explicitly flags as needing separate attention and which ADAA-1 will
  not clean up. **Do not fight it** — declare Wrap the destruction mode, give it a 4× floor, and let the
  residual aliasing be part of the sound (Family C precedent).

`Beyond` blends only the two **neighbouring** rules (clamp↔reflect below 50, reflect↔wrap above) and
blends the **outputs**, so ADAA stays exact throughout.

**The number that makes it dangerous:** with Drive at +48 dB, HOLD gives a total square; MIRROR gives
~125 fold-throughs of whatever the user drew — an inharmonic scream that still tracks the drawn shape;
WRAP gives 125 hard jump discontinuities per half-cycle. **Destroyed-but-still-musical** is MIRROR at
Drive 60–70 % on a gently S-shaped curve: 5–8 folds, pitch survives, timbre completely rewritten.

⚠️ **ADAA'ing `f()` but not the overflow** is the trap here: Mirror and Wrap are themselves nonlinear,
and antialiasing the drawn curve while naively folding the domain in front of it leaves the folder's
aliasing entirely untouched — which is the loudest aliasing in the signal path.

### 6.5 Click-free editing — crossfade the two ADAA OUTPUTS, not the table

The existing shared→audio swap (`PluginProcessor.cpp:5578-5591`: version counter + `ScopedTryLock` +
block-top memcpy) is correct as a **transport** and should be reused verbatim. **But its inaudibility
argument does not carry over.** Its own comment says *"a one-block-stale table is inaudible"* — true for
a 1 Hz LFO read and post-slewed 2.5 ms; **false for a per-sample transfer map**, where swapping `f()`
mid-stream changes the output instantaneously by up to Δy × the local signal amplitude, **11 times a
second during a drag**. That is textbook zipper noise.

**Do this instead:**

1. UI edits → `shPush` debounced → JSON blob → `setDistortionCurves()` native (mirror of
   `setSynthLfoShapes`, `PluginProcessor.cpp:8093`).
2. Message thread bakes `f`/`F1`/`F2` under a `CriticalSection`, then `curveVersion_.fetch_add(1, release)`.
3. Audio thread at block top: `if (seen != version)` → **`ScopedTryLock` (never blocks)** → memcpy shared
   into the **inactive** of two table sets → `xfade = 0`. A missed try-lock just means one more block of
   the old curve.
4. **Per sample while `g < 1`: evaluate ADAA twice** — once against `F1_front`, once against `F1_back`,
   **sharing the same `u[n]`, `u[n−1]` and therefore the same divisor** — and output
   `(1−g)·y_front + g·y_back`, with `g` ramping over **40 ms**. Because both are memoryless maps of the
   same input, **the blend is itself a valid transfer curve at every instant**: no discontinuity, no
   phase error, no comb. Cost: exactly 2× for 40 ms (~0.25 % of a core), then back to 1×.

**Retune the push cadence.** `shPush` (`index.html:23603-23605`) throttles to a 90 ms leading edge —
tuned for a cross-window mirror, not for audio-rate shaping. At 90 ms with a 40 ms fade you get 40 ms of
movement and 50 ms of stasis, which **feels stepped even though it is click-free**. Drop the distortion
curve's leading edge to **40 ms** so consecutive fades butt together. A 2048-entry bake plus a ≤32-point
JSON parse at 25 Hz is free on the message thread.

**Table swap cost:** one 32 KB memcpy (~2 µs) per ~40 ms while dragging. Zero when not.

### 6.6 ⭐ "Send To Shaper" — the killer feature, and it is already built

Every memoryless mode in the roster is a closed-form `f(x)` we write anyway, and every one of them
already bakes an `f` table for the core visualiser (§5.8). So:

1. Sample the selected mode's `f(x)` at **512 points over `x ∈ [−1, +1]` at its CURRENT Drive / Bias /
   Character settings**.
2. Run **`rdpSimplify`** (`index.html:24080`, Douglas–Peucker — the same algorithm already shipped for
   fb248's WT→LFO exact shape) at `eps = 0.015`, retrying at 0.03 / 0.055 exactly as fb248 does — except
   **cap at 32 points, not 160**. A 32-breakpoint curve is what a human can edit, and more points buy
   nothing once tension is fitted.
3. Fit each segment's tension with the closed form `c = 0.25·ln(r/(1−r))` (§6.1), which recovers a smooth
   analytic curve to within a few tenths of a dB from a handful of points.
4. `seedTo(pts, name)` (`:23763`) drops it into the editor with the mode's name as the label.

**The user experience:** pick Tube, hear it, hit "Edit curve", and the actual Tube curve appears as ~12
draggable points that you can now ruin. **Serum cannot do this. Bitwig cannot do this.** Kilohearts'
Remap library gets closest but only offers *its* factory shapes, not the live state of the mode you were
just using.

Ship the affordance as a **small arrow on the core visualiser of EVERY mode**, not as a menu item — that
single affordance is what actually delivers the promise, far better than a mode named "Custom" ever
would. Show Trash 2's three-layer overlay while editing: **Base** (the algorithm's curve) · **Your
Curve** · **Result**.

### 6.7 Modulation — ship exactly three

| Ship | Why |
|---|---|
| **Morph** (A→B), a first-class APVTS mod destination | Serum's X-Shaper concept **without hijacking Drive**. Implementation is *identical to the declick crossfade already required* — the same two-table blend with `g` driven by the param instead of a ramp. One code path, not two. Draw a gentle curve as A, a razor staircase as B, put Env 1 on Morph, and the note's attack is destroyed while its tail is clean |
| **Per-point Y modulation**, recycled verbatim from fb238 | `shPtMenu` → `shModSrcMenu` → `shModAmtNode` (`:23960`/`:23943`/`:23912`), range-bar rendering (`:23652-23657`), the live effective-curve ghost `shEffPts`/`shGhostTick` (`:23898-23911`), and the audio-thread re-bake **gated on > 0.002 source movement** (`PluginProcessor.cpp:5597-5628`) so an unmodulated curve costs exactly zero. **No new UI code.** Cost when active: ~6k flops at block rate ≈ **0.2 % of a core** |
| **Symmetry / Bias** as ordinary APVTS params | Automatically mod-matrix destinations |

**Do NOT advertise per-point X modulation.** The code comes along free with the shared machinery, but a
moving x-breakpoint is a moving knee threshold, perceptually indistinguishable from moving Drive — it
duplicates an existing param's lane and violates params-play-their-roles. Leave it available; keep it out
of the manual.

### 6.8 The four adaptations the LFO shaper needs

The point model `[x, y, c, mods?]` stays in `[0,1]×[0,1]` so **every** drag clamp, snap, rubber band,
group bound, JSON blob, per-point mod and preset path is reused byte-for-byte. Only four things change:

1. **⚠️ PERIODICITY MUST DIE.** `shEvalPts` (`:23527`) opens with `p = ((p%1)+1)%1` and
   `bakeLfoShapeTable` writes `tb[kLfoTableN] = tb[0]` (`PluginProcessor.cpp:8091`). An LFO shape is a
   *cycle*; a transfer curve is not. **Copying the evaluator unchanged silently makes every curve a
   wavefolder** — it will sound "interesting" and pass a casual listen, and you will not find it until
   someone asks why the soft-clip curve folds. Replace with the `Beyond` rule.
2. **K = 2048, not 256** (§6.2).
3. **A centre anchor.** The drag handler (`:23878`) pins `x` only for index 0 and n−1. Pin a third at
   `x01 = 0.5` and treat it as an endpoint for x-clamping, so **`f(0) = 0` by default** — otherwise you
   are shipping DC (§4.1). Offer "unpin centre" as the deliberate Bias / dead-zone gesture. ~6 lines.
4. **The y-axis is already bipolar and free** — `bakeLfoShapeTable` already emits `tb[k] = 2y − 1`
   (`:8089`). Only the x-axis meaning changes: `xIn = 2·x01 − 1`.

Also: **cap the transfer curve at 32 points**, not the LFO's 160 (`:23856`). 160 breakpoints is
unreadable, unmodulatable, and — because each is a slope discontinuity — an aliasing generator.

### 6.9 Symmetry as a KNOB, not two modes

Serum forces the choice up front with two incompatible coordinate systems. We keep the full bipolar field
on screen always (which is what makes the excursion glow legible), draw a major cross at `x01 = 0.5` /
`y01 = 0.5` (the grid loop at `:23645` already emits a `class="maj"` line; add the vertical), and make
**Symmetry** a knob:

* **Odd (0 %)** — edit the right half only; the left is generated as `f(−x) = −f(x)`. Guaranteed zero DC,
  zero even harmonics, the classic clipper family. **The blocker can be bypassed.**
* **Free (100 %)** — both halves as drawn. Even harmonics and DC, which is why the 10 Hz blocker is
  mandatory rather than optional.
* **Between** — linearly blends the mirrored left half with the drawn left half.
* **Even/fold** gets its own discrete setting (`f(−x) = f(x)`, the rectifier/octave family) because it is
  not on the same axis.

The payoff is directly measurable on the even/odd-ratio metric: sweeping Symmetry 0→100 walks the
harmonic profile **continuously** from pure-odd to strongly-even **on the same drawn shape** — precisely
the "night and day across its whole travel" gate, and a knob Serum does not have because Serum made it a
mode. Implementation is a **bake-time mirror plus a blend**, not a second editor.

### 6.10 Surface placement — both already built

* **Always-visible core:** the FX-rack device core is a flex-filled div with an absolutely-positioned
  100 %-size SVG (`index.html:7155-7156`, `.fxr-core`), rendered from `CORES[d.core]()` at `:7506`.
  Read-only live curve + excursion glow, 60 fps, no editing.
* **Editable:** the LFO card pattern. `shPaintAll` (`:23523-23524`) already renders the same `scopeSVG()`
  into both `.mv-scope` and `.lfo-ext .card-scope`, band-cropping the card hero with
  `viewBox='0 2 456 76'` and a per-scope `__shGeo`; `shScopes()` (`:23677`) is literally called *"the
  doctor's two screens."* Reuse unchanged — the pop-out native-window path (fb82-90) and cross-window
  blob sync (fb232) come along for free.

⚠️ Any custom context-menu node must replicate the **fb258 geometry** from `shRedRow` (`:23888-23895`):
`padding: 5px 12px 5px 26px; font-size: 11px; font-weight: 300`, no letter-spacing, no radius — matching
`.syn-ctx-item` exactly (26px = 12 pad + 6 `::before` + 8 gap). Miss it and it reads visibly off.

---

## 7. Build order

Mirrors the reverb build order — one mode at a time, each fully validated before the next.

0. **⚠️ SAFETY COMMITS FIRST, before any DSP.** (a) Add the `dstSend` term to
   `PluginProcessor.cpp:6979` / `:7111` and their R twins, and promote `SYN_FX_ORDER` to a 6-way
   permutation index (§4.5). (b) Write the sample-rate-aware DC blocker and consolidate the three
   fixed-0.995 copies (§4.1). (c) Fix `ConvolutionReverb`'s unreported 512-sample latency. Each is a
   small, separately-verifiable commit and each is a landmine if left.
1. **Chassis + params, no DSP.** ~24 APVTS params (`SYN_DST_*`), relay chain, UI rename Saturate →
   Distortion, the 23-mode optgroup Type list, the six back-8 relabel maps (`DST_BACK` / `DST_PILLS`),
   all pushing real params. **Verify the 4-point bind chain** — a missing relay builds clean and
   silently no-ops.
2. **The shared shell:** `Source/DistortionEngine.h`. Oversampler (`juce::dsp::Oversampling`,
   `filterHalfBandPolyphaseIIR`, maxQuality, integerLatency), the ADAA harness lifted from
   `TerrainFilters.h` `WaveShaper` with Vicanek's kernel, the DC blocker, the pre/de-emphasis pair,
   auto-gain, per-sample param ramps, **fixed 8-sample latency + dry-path compensation (§4.4)**, and the
   `flush()` / `ScopedNoDenormals` hygiene. Mirror `DelayEngine.h`'s API exactly (`prepare(double)`,
   clamped setters, `processSample(inL, inR, outL&, outR&)`, wet-only return with the processor owning
   Mix). **This is the piece everything else plugs into. Build and validate it alone.**
3. **The harness** (§8) before the first shaper — including the `SNR_A` probe. Sources are in
   `scratchpad/os/{alias,drive,adaa2,lut2,t2}.cpp` and fold into the existing `rvb_perceptual.cpp` idiom.
4. **Soft Clip + Hard Clip** — the CLIP family. Cheapest, proves the shell, ADAA, the drive law, the
   `Wrap` pill, and the auto-gain, and gives the reference every other mode is heard against.
5. **Tube** — the reference "does saturation work at all" mode, and the first with state. Build the
   cathode-sag and grid-blocking loops **in this step, not later** (§2.6 trap 3).
6. **Linear Fold + West Coast** — deliberately early: the hardest aliasers, so they stress-test the shell
   before the roster is full. This is where the 4×/8× auto-promotion and `Track` get proven.
7. **Downsample + Bitcrush + Overflow** — trivial DSP, proves the "no anti-aliasing" path, the `Clean`
   pill, and the `Quality`→filter-order remap. Almost entirely recycled from `VintageReverb.h`.
8. **Zero-Square** — proves polyBLEP and the gate.
9. **Tape** — the big one. Jiles–Atherton, RK2, the bias deadzone, gap loss, head bump, wow/flutter pair.
10. **Diode 1/2 · Asym · Rectify** — one family, one param set; Rectify last because of the mid-chain
    blocker ordering.
11. **Transformer · Stomp Box · Overdrive · Sine Fold · Slew Clip** — fill out the roster.
12. **The SHAPER family** — Shaper, Shaper Asym, Harmonics, Table, plus the curve editor (§6) and
    **Send To Shaper** wired to every mode built above. Build it last precisely so it has 19 curves to
    import on day one.
13. **Core viz** last, once the curves are final.

Every step: build **both** formats, bust the BinaryData cache if `index.html` changed, `rm -rf` + `cp` +
re-sign on install, bump `TERRAIN_BUILD`, pluginval L5 on VST3 **and** AU.

---

## 8. Verify — the perceptual harness for distortion

Per the perceptual-test-harness hard rule: **phase-independent hearing metrics only. Sample-diff is
banned.** Every measurement level-matched **offline** (§4.2), else you are measuring loudness.

### 8.1 The metric table

| Metric | How | Proves |
|---|---|---|
| **THD** | harmonic energy ÷ total, matched output level | the mode distorts at all |
| **Even/odd ratio** | H2+H4… vs H3+H5… | Tube/Transformer read *warm* (even-rich); Overdrive/Fuzz read *square* (odd-rich). **The metric that proves the modes are different characters and not one curve with 23 labels** |
| **🔑 `SNR_A` (aliasing floor)** | 65536-pt Hann FFT, `f0 = k·fs/65536` so harmonics land on exact bins; harmonic = within ±3 bins of `m·k` (plus bins 0–8 excluded for DC); **`SNR_A = 10·log₁₀(Σharmonic / Σalias)`**. Test at **f0 = 220.0 / 1244.5 / 4186.0 Hz**, amplitude 0.8 | **the number the whole device lives on.** Gates in §3.10 |
| **🔑 Level-dependence** | same tone at −30/−20/−10/0 dBFS; compare harmonic **profile SHAPE** (H3/H2), not size | **proves Family A actually has memory.** Must move **> 2×** for ANALOG; must be **flat** for Family B (that flatness is a *feature* — predictability — not a shortfall) |
| **Crest factor** | peak ÷ RMS | saturation compresses. **The one number that separates Stomp Box (~2.5 at Drive 90) from Overdrive (~1.4)** — objective proof they are not the same thing wearing different filters |
| **Fundamental energy at `f0` vs Asym** | must fall **below −40 dB relative to 2·f0** at `a = 1` | Rectify is genuinely a pitch effect. No other mode can produce this |
| **Fold count** | sign changes of the derivative over a full-scale input sweep | ⚠️ **Gate FOLD on this, never on THD** — THD is non-monotonic for wavefolders (measured 811 % at 75 % of travel, 191 % at 100 %) because the fundamental partially nulls as fold count crosses certain values. THD would report that raising the fold range made things worse |
| **Spectral centroid** | brightness shift | Tone / Emphasis / Knee / Corner play their roles |
| **DC offset** | mean of output after asymmetric modes, **measured at the EXTREME** (Asym ±100, Bias ±1.0), never at the default | the DC blocker works (§4.1). Target **< −60 dB**, < −80 dB with `Sym` ON |
| **Note-on thump** | LF energy in the first 30 ms vs steady state, at Rectify `a = 1` | a single 10 Hz pole **fails** this; the 2-pole @ 20 Hz passes |
| **Click / peakiness** | sweep every knob 0→100 over 1 s, peakiness ratio | no-clicks hard rule; existing harness idiom |
| **Latency** | impulse in, find the peak | matches the fixed 8 samples and the dry compensation (§4.4) |

### 8.2 Per-family harness gates — run these before showing anyone

| Family | Gate |
|---|---|
| ANALOG | **Level-dependence H3/H2 must move > 2×** across −30…0 dBFS. If it does not, the ODE solver and the oversampling bought nothing and the mode should not ship. Bias must swing even/odd from ~0.15 at centre to **> 4** at +80, and raise THD-at-−30-dBFS by **> 20 dB** at −80 |
| Transformer | **THD at 50 Hz must exceed THD at 2 kHz by ≥ 15 dB at Drive 60.** If not, the topology is wrong — you built tanh with a low shelf |
| Overdrive | **Swept-sine chirp at full scale: output amplitude must DROP with frequency** as `SR/(2πf)` above the slew corner. Unfakeable by any waveshaper. **This is the mode's fingerprint — plot it** |
| Stomp Box vs Overdrive | crest **~2.5 vs ~1.4** at Drive 90 |
| CLIP | Even/odd must swing hugely with Bias; **>9th-harmonic energy** must swing with Knee. Zero-Square: harmonic decay slope must move from ~−6 to ~−12 dB/oct across Knee. Slew Clip: a **1 kHz square must morph square → trapezoid → triangle**, spectral centroid moving **> 2 octaves**, AND the corner frequency must differ at −30/−20/−10/0 dBFS — that last measurement is what proves it is not a filter in a costume |
| DIODE | **Crest must FALL monotonically with Drive on Diode 1 and RISE monotonically with Dead Zone on Diode 2** — measured on a **decaying note, not a steady tone**, or you miss the effect entirely. That single contrast is the objective proof Diode 2 is an expander and therefore genuinely not Diode 1 with a hat |
| Rectify | Fundamental energy at `f0` falls **> 40 dB below 2·f0** at `a = 1`. Alias floor measured at **1.2 kHz AND 5 kHz** — the 5 kHz case is where the 8 kHz image appears and a 1 kHz-only test would miss the mode's worst behaviour |
| FOLD | Level-matched 220 Hz sine, sweep Drive 0→100 %, log spectral centroid every 2 %: must climb **monotonically with no plateau longer than 4 % of travel**. Sine Fold: harmonic count above −60 dBFS must rise from 2 to **> 100**. West Coast: reproduce DAFx-17 Fig. 8's "complex harmonic patterns reminiscent of FM synthesis", then sweep Symmetry at fixed Drive and reproduce Fig. 18b — **even harmonics appearing without brightness changing much**. That second plot is the objective proof Drive and Symmetry are in separate lanes |
| SHAPER | Record the alias table for **a gentle curve AND a staircase curve** — two rows, they will differ by **> 30 dB**, and that difference IS the finding. `Harmonics`: with a single bar `k` raised, the spectrum must show **ONE harmonic at index `k` more than 40 dB above all others** — a falsifiable, exact test of the Chebyshev maths and the strongest verification target in the device |
| DIGITAL | **THD vs input level from −40 to 0 dBFS must show ~6 dB/bit slope for Bitcrush and FLAT for Downsample.** That is the discriminator that settles the merge question. Non-harmonic energy must move monotonically and hugely across the whole rate/bits/threshold sweep |

### 8.3 Dramaticism gate

Every one of the 11 params must move its metric **night and day**. Anything that cannot be perceived gets
cut, not shipped. Serum 2 is the bar. **And run the chord test** — fb265's lesson: a summed chord at high
drive becomes broadband IMD rather than harmonics, so the top of every drive range must be signed off on
a 3-note chord, not a single sine.

---

## 9. Per-mode specs

23 modes, 6 families. Each mode gives: mechanism · the maths · what makes it distinct · its 8 Character
voicings. Extremity lives in §2.3; anti-aliasing in §3.7. **Character voicings must change PHYSICS, not
EQ** — that is the test of whether a Character list is real.

---

### 9.1 ANALOG — dynamic, with memory  *(back-8: Low Cut · Hi Cut · Emphasis · Sag · Recovery · Drift · Drift Rate · Snarl · signature `Bias` · pill `Slam`)*

The shared shell, one signal path for all five modes:

```
in → Low Cut(pre) → Emphasis(pre) → [STATE UPDATE at 1×] → OS_up
   → nonlinearity(x, state) → Snarl tap → OS_down → Emphasis(de)
   → Hi Cut(post) → DC block(10 Hz) → Auto makeup → Tone → Mix
```

Only the `nonlinearity` box and the state ODE change per mode. **The state ODEs run at BASE rate** —
they are sub-audio integrators, they generate no HF, and keeping them at 1× is what makes Tube as cheap
as Stomp Box despite two extra state loops.

#### Tube  *(the reference mode)*

12AX7 common-cathode triode. Three real behaviours, all of which **are** the mode: (a) the triode
transfer curve, (b) **grid conduction** — when Vg goes positive the grid draws mA-level current:
asymmetric clipping and the source of the strong 2nd harmonic, (c) **two memory loops no waveshaper
has** — **cathode sag** (the Rk‖Ck cap charges with plate current, raising Vk, pushing toward cutoff) and
**grid blocking** (grid current charges the coupling cap, pushing the grid negative and holding it for
tens of ms — the "farting out" Aiken and GEOfex both describe as *the* defining overdriven-tube artefact).

**Model: Dempwolf & Zölzer, DAFx-11 pp. 257-262**, chosen over Koren because it is continuously
differentiable everywhere (no piecewise branches), models grid current with its own fitted equation
rather than a bolted-on diode, and publishes measured fits for **three real 12AX7s** — three Character
voicings for free.

```
softplus_C(u) = log(1 + exp(C·u)) / C                       // overflow-safe: log1p(exp(−|Cu|)) + max(Cu,0), all over C
I_k = G·( softplus_C( V_a/µ + V_g ) )^γ                     // eq 10
I_g = G_g·( softplus_Cg( V_g ) )^ξ + I_g0                   // eq 11
I_a = I_k − I_g                                             // eq 12

Fitted 12AX7s (Table 1) — Characters 1-3:
  RSD-1: G=2.242e-3  µ=103.2  γ=1.26   C=3.40  G_g=6.177e-4  ξ=1.314  C_g=9.901  I_g0=8.025e-8
  RSD-2: G=2.173e-3  µ=100.2  γ=1.28   C=3.19  G_g=5.911e-4  ξ=1.358  C_g=11.76  I_g0=4.527e-8
  EHX-1: G=1.371e-3  µ=86.9   γ=1.349  C=4.56  G_g=3.263e-4  ξ=1.156  C_g=11.99  I_g0=3.917e-8
```

⚠️ **Do NOT solve the load line per sample.** `V_a = V_b − R_a·I_a` is implicit in `I_a`. **Bake it ONCE
per Character at load time** into a 4096-point LUT of `V_a(V_g)` over `V_g ∈ [−14, +8] V`
(non-uniform — dense in `[−3, +2]` where the knee lives), plus a parallel LUT of its running integral
`F1` for **free ADAA**. Cubic interp both. Size for the **Slam-ON** worst case.

```
// per sample, BASE rate:
V_g[n] = Dgain·x[n] + V_bias − V_cc[n] − V_blk[n]
V_k[n] = V_k[n−1] + (Rk·I_k[n] − V_k[n−1])·ak,  ak = 1 − exp(−1/(fs·τ_k))   // τ_k = Recovery, 0.05–500 ms
V_cc[n] = sagAmt · V_k[n]                                                    // Sag, 0–1.5
V_blk[n] = V_blk[n−1]·(1 − 1/(fs·τ_c)) + I_g[n]·Rg/fs                        // Fender 0.1µF/220k ⇒ τ_c = 22 ms

Bias (bipolar):  b ≥ 0 → V_bias = −1.5 + 2.0·b   // +1 ⇒ +0.5 V, grid AT conduction
                 b < 0 → V_bias = −1.5 + 4.5·b   // −1 ⇒ −6.0 V, deep toward cutoff
```

**Distinct from:** the only mode whose **operating point walks with program material on two independent
time constants at once**. A loud stab charges both caps and the note *after* it is quieter, darker and
dirtier for 20–200 ms. Blind test: play a staccato line — only Tube ducks and dirties the following note.
Harmonically it sits at the **even-dominant pole** (H2/H3 > 2 with Bias ≠ 0), which the ear reads as
"warm" rather than "distorted."

**Characters:** `RSD 12AX7` (default — the DAFx-11 reference tube; tight knee, textbook preamp bloom) ·
`EHX 12AX7` (µ 86.9, wider knee — audibly rounder at identical settings; proof Character changes physics)
· `12AU7 Line` (µ ~20, huge headroom — the hi-fi/glue voice that replaces the cut Soft Sat) ·
`6V6 Power` (odd-dominant, hard cathode sag, screen droop — where Tube stops sounding *preamp*) ·
`Starved Plate` (plate supply at ~80 V, load line collapsed — no headroom, the tail distorts as hard as
the transient; fizzy and cheap) · `Cold Clipper` (−4 V bias, big Rk, **no bypass cap so no sag bloom** —
the Marshall cold-clipper: hard, immediate, unforgiving) · `Blocking Fender` (0.1 µF/220 k, τ = 22 ms:
maximum blocking distortion — transients fart out and the next note audibly ducks. **The most ALIVE
voicing and the best demo of Family A**) · `Vari-Gain Hi-Fi` (enormous headroom, minimal asymmetry,
blocking disabled — only peaks colour; the honest near-clean voicing and proof Character spans
clean→ruined).

#### Tape  *(the big one)*

> 🔑 **BOUNDARY LAW — added by the 2026-08-14 cross-bible sweep. Terrain has FOUR tape surfaces and
> they must never tangle.** `TAPE-BUILD-BIBLE.md` §0.2 is the authority; it is reproduced here so a
> Distortion builder cannot miss it, and the two files now say the same thing:
> ```
> DISTORTION.Tape (this mode) = the MAGNETISATION.  Jiles-Atherton hysteresis. What the tape IS.
> TAPE (the future device)    = the MACHINE.        Heads, motor, loop, splice, feedback. What it DOES.
> DLY.Tape                    = a delay FLAVOR.     One tanh + LP colour inside a clean delay chassis.
> TapeMachines.h / TapeLoop   = the CHANNEL COLOR / the LOOPER. Static sat+wow+hiss; recording.
> ```
> **Consequences a Distortion builder must respect:** this mode owns the hysteresis loop, record
> bias, gap loss, head bump and its own wow/flutter *as magnetisation artefacts* — it must **never**
> grow an echo, a motor, a transport or a loop. Conversely the Tape *device* deliberately ships a
> **cheap memoryless soft stage**, NOT this Jiles-Atherton core (`TAPE-BUILD-BIBLE.md` §3.5), so the
> deep smear stays exclusive to this mode. The user who wants both chains `Distortion (ANALOG/Tape)`
> → `Tape` — the two devices **compose** instead of competing. Nothing about this mode changes when
> the Tape device ships; nothing in the Tape device duplicates this mode.

Magnetic hysteresis — **genuinely not waveshaping**. Magnetisation `M` depends on the applied field `H`
**and on the state left by previous samples**; the I/O relation is a double-valued **loop** whose branch
is selected by `sign(dH/dt)`. **Jiles & Atherton (1986)**, real-time formulation from **Chowdhury,
DAFx-19 (CCRMA)** and the shipping `AnalogTapeModel`. On top of the hysteresis core: record **bias**
(including the under-bias deadzone), playhead **gap loss**, the **LF head bump**, and **wow/flutter**.

**Deliberate deviation from the paper:** it needs 16× oversampling ONLY because it runs an explicit
55 kHz bias carrier through the hysteresis. **We do not model the carrier explicitly** — we fold its
linearising effect into the pinning parameter `k`. That single decision buys **4× instead of 16×** and is
the difference between shipping this mode and not.

```
L(x)  = coth(x) − 1/x           for |x| > 1e-4, else x/3        // guards MANDATORY, coth blows up at 0
L'(x) = 1/x² − coth²(x) + 1     for |x| > 1e-4, else 1/3
Q = (H + α·M)/a ;  M_an = M_s·L(Q)

dM/dt = [ ((1−c)·δ_M·(M_an − M)) / ((1−c)·δ·k − α·(M_an − M)) · dH/dt
          + c·(M_s/a)·(dH/dt)·L'(Q) ] / [ 1 − c·α·(M_s/a)·L'(Q) ]              // eq 18
  δ   = +1 if dH/dt ≥ 0 else −1                                                 // eq 7
  δ_M = 1 if sign(δ) == sign(M_an − M) else 0                                   // eq 8

Hdot[n] = ((1+dα)/T)·(H[n] − H[n−1]) − dα·Hdot[n−1],  dα = 0.75    // alpha-transform, better than eq 21
RK2:  k1 = T·f(M₋₁, H₋₁, Hdot₋₁)
      k2 = T·f(M₋₁ + k1/2, (H+H₋₁)/2, (Hdot+Hdot₋₁)/2)
      M[n] = M₋₁ + k2
```

🔑 **The single most valuable number in this document — the AUDIO-NORMALISED parameter mapping**
(ChowTape `HysteresisProcessing::cook`). The physical-units version is unusable in float:

```
M_s   = 0.5 + 1.5·(1 − sat)          // sat 0..1 → M_s 2.0 .. 0.5
a     = M_s / (0.01 + 6.0·drive)
c     = sqrt(1 − width) − 0.01       // width ← our Snarl knob
k     = 0.47875
α     = 1.6e-3                       // constant
upperLim = 20.0
Sanity: drive=1, sat=1 → a = 0.0832, so Q ≈ 12·H (deep saturation).
        drive=0        → a = 50·M_s, L(Q) ≈ Q/3, perfectly linear. Drive 0 IS clean.
```

```
Bias (bipolar):
  b ≥ 0  over-bias:  k_eff = k, and gap-loss corner ×= (1 − 0.35·b)
                     (over-biasing a real machine linearises it AND kills top end — both must happen)
  b < 0  UNDER-BIAS: k_eff = k·(1 + 3.5·|b|)
                     wider pinning ⇒ wider un-linearised central region ⇒ crossover distortion
                     AT THE ZERO CROSSING, so quiet material distorts HARDEST. The documented
                     "thin and spitty" underbiased sound.

Playhead loss (eq 13), baked per Character as a 3-biquad shelf fit — do NOT run 100 taps:
  V(f) = V0·exp(−k_w·d)·[ (1 − exp(−k_w·δ))/(k_w·δ) ]·[ sin(k_w·g/2)/(k_w·g/2) ],  k_w = 2πf/v
  Sony TC-260 reference: d = 20 µm, g = 5 µm, δ = 35 µm
Head bump: peaking EQ at ~45 Hz @15 ips / ~90 Hz @7.5 ips, +2…+6 dB, Q ≈ 1.2
Wow/flutter: Drift + Drift Rate → SmoothRandom → cubic-interp delay read, 0 … ±12 ms
```

**Distinct from:** the only mode whose transfer relation is a **loop rather than a curve**. Because the
branch is selected by `sign(dH/dt)`, the output lags the input in a level-dependent way that reads as
**SMEAR** on transients — a static curve physically cannot do this, and it is why real tape *rounds* a
snare instead of clipping it. Odd-dominant at low level (H3 ≫ H2), the **opposite pole from Tube**.
Against Transformer despite both being magnetic: Transformer's state is the *integral of the input* (so
it is frequency-selective); Tape's is a *pinned domain population* (so it is history-selective).

⚠️ **Normalise the output by a fixed constant per Character**, never by anything program-dependent — a
program-dependent normaliser flattens exactly the level-dependence you paid for. And normalise **at all**,
or Drive degenerates into a volume knob.

**Characters:** `Studer A80 / 15 ips` (default — wide loop, 45 Hz +3 dB bump, flat to ~18 kHz) ·
`Ampex 350 / 15 ips` (the Decapitator-A lineage: softer knee, earlier compression — distinct by **where**
it breaks up, not by EQ) · `Studer / 30 ips` (bump halved and moved up, flat past 20 kHz, saturates much
later — clean modern mastering tape) · `Cassette / 1⅞ ips` (gap null at ~12 kHz so the band genuinely
closes, big bump, aggressive triple-LFO wow — **recycles `CassetteMachine`'s existing 0.6/2.2/7 Hz stack
verbatim**) · `Chrome (Type II)` (higher coercivity, `k` raised: saturates **later but harder**, brighter
top — different by knee shape, not filter) · `Dub Plate` (heavily over-biased and driven, enormous bump,
dark top) · `Worn / Shedding` (`k` modulated by a slow random — oxide dropouts — so the **loop width
itself wanders** and the distortion character flickers over seconds. Impossible with a static curve; the
best demonstration that this is a physical model) · `Under-Biased` (deadzone already open at Bias 0, so
the whole travel sits in spitty territory — the "trashy shit" voicing).

#### Transformer

Iron-core saturation. **The physical key every naive implementation misses: core flux is the TIME
INTEGRAL of the applied voltage**, `Φ = ∫v dt`. Therefore at a fixed voltage amplitude, flux density is
**inversely proportional to frequency**, and **low frequencies saturate first.** Measured, not theory:
practical audio transformers distort below ~100 Hz (small) or ~30 Hz (large) at levels where the midrange
is perfectly clean. Three effects stack: core saturation of the flux, core loss (eddy + hysteresis), and
a **leakage-inductance × winding-capacitance resonance at 20–60 kHz that RINGS on transients** — the
reason transformers add *snap* rather than just *warmth*.

🔑 **THE TOPOLOGY IS THE MODE — integrate, saturate the FLUX, differentiate back:**

```
ρ = 1 − T/τ_L,  τ_L ≈ 2 s
φ[n] = ρ·φ[n−1] + (T/τ_m)·x[n]              // leaky integrator; leak is MANDATORY (BIBO)
u = (φ[n] + φ_DC)/φ_k                        // φ_DC from Bias
B = B_sat·L(u)                               // reuse Tape's guarded Langevin verbatim — same curve
B += hys·sign(φ[n] − φ[n−1])·(1 − |B/B_sat|) // minor loop = Snarl
y[n] = (B[n] − B[n−1])·fs·τ_m                // differentiate — EXACTLY inverts step 1 when unsaturated
y -= gLoss·onepole_lp( (B[n]−B[n−1])·fs, 4 kHz )                    // eddy loss
y += leakageResonance(y)                     // 2-pole peaking BP, f_r 20–60 kHz, Q 1.5–6, 0…+6 dB
                                             // AFTER the differentiator. Reuse TapeMachineBase::SVFBandpass
```

Because the differentiator exactly inverts the integrator below the knee, the mode is **unity-gain,
phase-flat and THD = 0 for |φ| ≪ φ_k**. That is what makes it honest, and it gives Drive an unusually
honest zero.

**Distinct from:** the **ONLY mode whose distortion is a function of FREQUENCY as much as of level**.
Feed 40 Hz and 4 kHz at identical amplitude and the 40 Hz one is destroyed while the 4 kHz one is
untouched — a **15–20 dB THD difference at the same Drive**. Second tell: **transient RING** — a kick or
pluck comes out with a metallic overshoot no other mode produces.

⚠️ **Two traps.** (1) Implementing it as "tanh with a low shelf in front" produces LF-weighted distortion
but **no genuine frequency dependence**, and under the level-dependence metric it collapses into
Tube-with-EQ. The integrate→saturate→differentiate chain costs about **6 extra flops**. There is no
excuse. (2) Early prototypes sound **THIN** and the instinct is to add a low shelf on the output. **Do
not.** The flux normalisation (`×τ_m`) already restores unity gain; adding EQ back re-flattens the exact
frequency dependence you built. If it sounds thin, `φ_k` or `τ_m` is wrong — **fix the core, not the
tone.**

**Characters:** `Neve Marinair` (default — large core, resonance 42 kHz / Q 2.2 / +2 dB; round thick
lower-mids) · `API 2503` (smaller core, saturates earlier and harder; resonance 55 kHz / Q 4 gives the
famous **transient snap** — different by transient behaviour, not EQ) · `Jensen (Clean)` (huge nickel
laminate, essentially never saturates below Drive 80 — the honest "iron colour only" voicing) ·
`Cinemag Output` (steel, LF-heavy, generous headroom then a fat 2nd-harmonic bloom) · `Cheap Line
10k:10k` (tiny core, saturates from −20 dBFS, enormous LF distortion — the fastest route to ruin) ·
`Germanium Radio` (miniature transformer, leakage resonance pulled down to **14 kHz, i.e. IN BAND**, so
it rings audibly) · `DC Magnetised` (permanent flux offset baked in — the core is half-saturated at rest,
so even harmonics dominate from Drive 10; the abused/broken voicing) · `Toroid (Power)` (a mains
transformer: colossal core that saturates only below ~60 Hz and does so violently — pure sub-destroyer,
leaves everything above 100 Hz pristine).

#### Stomp Box

Op-amp **FEEDBACK** diode clipper — TS-808 / SD-1 / Klon, and the important word is **feedback**. The
diodes sit **across the feedback resistor**, which means the stage **can never produce a square**: past
the knee the transfer function retains a residual slope of ≈ `(R1 + r_d)/R1` instead of flattening. The
second half of the identity is band shaping: the TS's 4.7 k + 0.047 µF network means only content above
**~720 Hz** sees the full clipping gain, **so the bass passes through CLEAN**.

```
i = Vin/R1                                  R1 = 4.7k
v = Vout − Vin  solves:  i = v/Rf + 2·Is·sinh( v/(n·Vt) )
Rf = 51k + drive01·2.5M                     // real pedal stops at 551k
1N4148: Is = 2.52e-9 A, n = 1.752, Vt = 25.85 mV

Asymmetric (SD-1 = 2:1; Klon = 1N34A germanium, Vf ≈ 0.35 V):
i = v/Rf + Is·(exp(v/(m·n·Vt)) − 1) − Is·(exp(−v/(p·n·Vt)) − 1)
1S2473 (Boss original): Vf ≈ 0.9 V ⇒ ~2.7 Vpp threshold, NOT the usual 1.5 Vpp

Band shaping — 60 % of the identity, do NOT skip:
  pre-HP into the clipper: 4.7k + 0.047µF ⇒ fc = 720 Hz    // content BELOW this is not clipped
  diode LP: 51 pF across the diodes ;  tone LP: 1/(2π·1k·0.22µF) = 723.4 Hz
  tone HP shunt: 220 Ω + 0.22 µF above 3.2 kHz
```

**Solving for `v`: ship the LUT, not Lambert W.** A 4096-point non-uniform table of `v(i)` (dense near
`i = 0`), monotonic so interpolation is safe, rebuilt only on Drive/Bias/Character change at block rate,
change-gated. **One table read per sample, and its running integral is a second table that makes ADAA-1
nearly free.** (D'Angelo/Pirkle/Esqueda's fast Lambert W, DAFx-19, is the alternative at ~15 flops.)
⚠️ **CRITICAL SIZING: the table domain must cover the SLAM-ON worst case (`i` out to ±0.5 A), not the
nominal ±0.02 A.** If Slam drives past the table edge the clamp degenerates into a hard clip that sounds
nothing like the mode — a bug that builds clean, tests fine, and only breaks at the extreme control the
no-playing-safe rule exists to protect.

**Distinct from:** two structural differences, both audible blind. **(1) It leaves the bass clean by
construction** — on a full mix or a bass-plus-lead patch the low end stays intact while the mids shred.
That is exactly why a Tube Screamer works on material Overdrive destroys. **(2) It never squares** —
crest stays ~2.5 where Overdrive's collapses to ~1.4.

**Characters:** `Green 808` (default — 1N4148 symmetric, 720 Hz pre-HP, 723 Hz tone LP) · `Yellow SD-1`
(asymmetric 2:1 using 1S2473, Vf ≈ 0.9 V — noticeably more even-harmonic **and** more headroom, two real
changes at once) · `Klon Germanium` (1N34A + the Klon's parallel clean blend, gain 4.6–25.8: a compressed
clipped path with an **unclipped transient riding on top** — the most expensive-sounding voicing) ·
`Transparent (Timmy)` (large Rf, high-Vf diodes, pre-HP dropped to 120 Hz so the **whole band** distorts
gently — the family's replacement for the cut Soft Sat) · `LED Clipper` (Vf ≈ 1.7 V: enormous headroom
then a very hard corner, no middle ground) · `MOSFET Soft` (JFET/MOSFET pair: **square-law rather than
exponential**, so a genuinely different knee curvature and a much wider transition region) · `Mid Scoop`
(pre-HP at 1.4 kHz, tone LP at 400 Hz — honky, cocked-wah; proves band shaping is first-class) ·
`Starved Battery` (rail at 4.5 V so the op-amp itself clips into the diodes, with Sag/Recovery modelling
the droop — the dying-9V sputter, and **the reason this mode belongs in Family A**).

#### Overdrive

The other half of the pedal/amp world: a **CASCADE** of gain stages into **SHUNT** (to-ground) clipping,
with **slew-rate limiting**. RAT / DS-1 / cranked-amp. Two things make it a genuine Family A member:
**(1) slew-rate limiting** — the RAT's LM308 slews at 0.3 V/µs, ~40× slower than a TL071, so the op-amp
physically cannot follow fast edges: distortion becomes a function of `dx/dt`, highs are triangle-ised,
and **maximum output amplitude falls with frequency**. **(2) supply-rail sag** — the clipping ceiling
**moves** with program material.

```
for s in 1..N:                               // N = 2 or 3, a CHARACTER property, not always 3
    u = g_s·u                                // total cascade gain 2 … 8000  (0 … +78 dB)
    u = slew(u)
    u = shunt_clip(u, V_rail)                // diodes to GROUND: residual slope → 0, i.e. IT SQUARES
    u = onepole_lp(u, fc_s)                  // inter-stage LP, 2.2–8 kHz — INSIDE the loop

slew:  d = u − ySlew ;  dmax = SR_limit/fs
       hard: ySlew += clamp(d, ±dmax)                    // correct, and the sound
       soft: ySlew += dmax·tanh(d/dmax)                  // C1, ~12 dB/oct less alias energy, ~4 flops

NUMBERS (normalised units, 1.0 == 4.5 V, fs = 48 kHz):
  dmax = 1.39   → triangle-ises above 10.6 kHz   (stock RAT / LM308 @ 0.3 V/µs)
  dmax = 0.02   → triangle-ises above 153 Hz     (essentially the whole band)
  dmax = 5e-3   → triangle-ises above 38 Hz      (the Slew Kill extreme)
  dmax < 1e-4   → output freezes toward DC.  FLOOR AT 2e-4.  BIBO.

rail sag:  env += (|u| − env)·(|u| > env ? aAtk : aRel) ;  V_rail = V0·(1 − sagAmt·env)
Bias > 0: DC offset at the shunt node (asymmetric clip, gated half)
Bias < 0: CLASS-B DEADZONE, width w = 0.25·|bias|:  u = sign(u)·max(0, |u| − w)
```

**Distinct from Stomp Box** — three independent, measurable differences: **(a) shunt vs feedback** (shunt
squares, crest 1.4 vs 2.5); **(b) slew limiting** — Overdrive gets **DARKER** as you drive it and its max
output amplitude falls with frequency, where Stomp Box gets brighter and more mid-forward; **(c) cascade**
— three stages of ×20 with band-limiting between them is a fundamentally different sound from one stage
of ×8000, which is exactly why real high-gain amps cascade.

⚠️ **The trap that ships this timid: implementing it as `tanh(x·g)` into a low-pass.** That is
volume-then-squash and the mode collapses into exactly the Soft Sat we cut. **THE SLEW LIMITER IS THE
MODE. Build it first, before the clipper.** The chirp test (§8.2) is unfakeable.

**Three free anti-aliasing wins:** put the inter-stage LP **inside** the loop so it band-limits *before*
the next stage clips (zero extra cost — the filter existed anyway, its *position* is what matters); make
`N` a Character property (N = 2 for Characters 1–4) for a **33 % saving on half the voicings**; ADAA the
shunt clip so the 4× only carries the slew limiter's residue.

**Characters:** `LM308 Rat` (default — N=2, slew 0.3 V/µs, 1N914 shunt, 2.2 kHz inter-stage, the RAT's
backwards tone control) · `TL071 Rat` (**identical circuit, slew 13 V/µs** — brighter, harsher, audibly a
different pedal. **This voicing exists specifically to prove the slew limiter is doing real work**) ·
`DS-1 Orange` (transistor stage first, asymmetric shunt, big mid scoop) · `Big Muff (4-stage)` (N=3 soft
shunt with heavy inter-stage LP — infinite sustain, violin compression; the most compressed voicing in
the device) · `Plexi Crunch` (N=3 with **no shunt diodes at all**; the clipping is the stages' own supply
rails, heavy Sag — the best demo of the Sag/Recovery pair) · `Modern High-Gain` (N=3, tight 5 kHz
inter-stage, low Sag — percussive and tight; distinct from Plexi by *transient* behaviour) · `Slew Kill`
(slew at a 0.03 V/µs equivalent so everything becomes a triangle — the broken-op-amp voicing) ·
`Dying Battery` (rail at 3 V, Sag 130, Recovery 0.5 ms baked in: gated, sputtering, motorboating — the
trashy one, and **it should be genuinely hard to use**).

#### ✂️ Soft Sat — CUT

Formally recorded because the reasoning is load-bearing for the roster. A memoryless algebraic sigmoid:
`tanh(kx)/tanh(k)` / cubic / `x/(1+|x|^p)^{1/p}`. **Not distinct** — it is precisely the
Bias=0/Sag=0/Drift=0/Snarl=0 corner of Tube with the state loops switched off, and the small-`k` limit of
Stomp Box with the band shaping bypassed. Under the family's own defining metric it reads perfectly flat
**by construction**. **No extremity ceiling** (§5.2). Its antiderivative is already in the tree twice
(`TerrainFilters.h` `logcosh`, `HarmonicEngine.h` `lncoshf_`) — if it ever needs to exist it is ten
lines, not a mode.

⚠️ **The real risk if shipped:** it is by far the easiest thing on the list to implement, so it would be
finished first, sound acceptable, and then become the **reference point against which the expensive modes
are judged**. Four modes costing 2–4× more CPU would be asked to justify themselves against something
that took an afternoon. Worse, if any of Tube/Overdrive/Stomp Box is implemented lazily, it **collapses
into Soft Sat** and the roster genuinely contains duplicates. Ship four wild modes plus Tape rather than
five plus a curve.

---

### 9.2 CLIP — memoryless bounded shapers  *(back-8: Low Cut · Hi Cut · Emphasis · Width · Bias · Gap · Punch · Feedback · signature `Knee` · pill `Wrap`)*

**The family's engineering superpower:** every curve here is a convex blend of anchors that have
closed-form antiderivatives, and **ADAA is linear in `f`** — `F1(Σwᵢfᵢ) = Σwᵢ F1(fᵢ)`, same for `F2`. So
a continuously-morphing knee costs **nothing** in ADAA exactness: you blend the antiderivatives with the
same weights. Nothing else in the device gets that.

**Whole family < 0.4 % of one core at Standard, < 0.9 % at Ultra** — 20–40× cheaper than the shipped
ConvolutionReverb. The correct CPU conclusion is therefore the *opposite* of "economise": **do not skimp
here, and bank the saved budget for Fold and Tape**, where the 8× and RK-solver money actually has to go.

⚠️ **Per-mode DEFAULT Knee values must sit far apart** so the modes never boot into their overlap zone:
**Soft Clip 65 · Hard Clip 8 · Zero-Square 45 · Slew Clip 50.**

#### Soft Clip

The BJT **long-tailed (differential) pair**. Its large-signal characteristic is *exactly*
`Io = α·I_EE·tanh(qVi/2kT)` — tanh is not an approximation here, it is the literal I-V law, with the
thermal voltage `kT/q ≈ 25.85 mV` setting the knee width **in absolute volts**. 🔑 **That absolute-volts
fact is the whole design lever: the knee does NOT scale with how hard you hit the circuit**, which is why
a real diff pair goes from gentle to razor as you drive it, and why our knee must be specified in
absolute input units and **never normalised by Drive**.

```
ANCHORS (all: f(0)=0, f'(0)=1, |f|→1)
A(x) = x/(1+|x|)          F1 = |x| − ln(1+|x|)          F2 = sgn(x)·( x²/2 − |x| + ln(1+|x|) )
B(x) = x/√(1+x²)          F1 = √(1+x²) − 1              F2 = ( x√(1+x²) + asinh(x) )/2 − x
C(x) = x − (4/27)x³  for |x| ≤ 1.5 ; sgn(x) otherwise   // C1 at 1.5: f(1.5)=1.0, f'(1.5)=0 exactly
       F1 = x²/2 − x⁴/27 (|x|≤1.5) ; 0.9375 + (|x|−1.5) beyond
       F2 = x³/6 − x⁵/135 (|x|≤1.5) ; sgn(x)·( x²/2 − 0.5625|x| + 0.225 ) beyond

KNEE BLEND  k = Knee/100:   k ≤ 0.5 : f = (1−2k)·C + 2k·B
                            k > 0.5 : f = (2−2k)·B + (2k−1)·A
F1 and F2 blend with the SAME weights — ADAA stays exact through the whole morph.

FULL MODE:  u = g·x + b ;  y = ( f(u) − f(b) )·makeup ;  then 10 Hz HP, Hi Cut, Mix
🔑 CLIP THRESHOLD SITS AT −6 dBFS INTERNALLY, so a synth send peaking at −10…−6 dBFS is
   already kissing the knee at Drive 0 — this is what kills the dead first third.
```

⚠️ **The trap that would make this timid: self-normalising the curve.** Writing `y = tanh(g·x)/tanh(g)` —
the tidy thing to write, and what `SynthVoice.h:2914` does — makes every drive setting a *rescaled
version of one shape*. Auto-gain then removes the only difference left and Drive feels dead. The
physically-correct alternative is free: the knee lives in **absolute** input units, so as Drive rises the
knee automatically shrinks relative to the signal and the curve **genuinely morphs** from gentle to
razor. **Do not normalise; use a makeup LUT** (`[mode][knee:16][drive:32]`, 512 floats/mode, bilinear,
smoothed over 15 ms).

⚠️ **Honest overlap:** Soft Clip at Knee 0 and Hard Clip at Knee 100 land within ~1.5 dB on the even/odd
and HF-energy metrics. Mitigated by defaults placed far apart (65 vs 8), **not** by pretending it does
not exist.

**Characters:** `Diff Pair` (true `tanh(u)`, the literal BJT curve; ADAA-1 only, `F1 = lncosh` via the
recycled `SubOsc::lncosh`) · `Glue` (anchor A alone, knee locked wide — bends from the very first sample,
**nothing ever reaches the rail**; the console/desk end and the family's replacement for Soft Sat) ·
`Cubic` (anchor C alone — near-linear then a hard C1 corner: almost pure 3rd harmonic, then a sudden
bite) · `Sine` (`f = sin(π/2·clamp(u,−1,1))` — odd harmonics with **Bessel-shaped sign-alternating
amplitudes**, a genuinely different spectrum: hollow and wooden rather than warm. `F1 = −(2/π)cos(πu/2)`,
exact. **This is Serum's Sine-Shaper character captured without a fold**) · `Asym` (positive half uses
knee `k`, negative uses `min(1, 2k)` — grid-conduction asymmetry; H2 becomes dominant over H3) · `Slam`
(op-amp saturation-recovery **overshoot**: a raised-cosine bump lifts the curve to 1.12 just before the
rail — adds a spit at the exact moment of clipping. Real feedback-amp behaviour) · `Squeeze` (knee width
tracks a 30 ms level follower, so the curve **HARDENS when you play hard** — the one voicing with memory;
ADAA stays exact because the knee only updates at block rate) · `Wall` (anchor B with an 8× internal
pre-gain and 1/8 post-trim — the tightest asymptotic curve available: within ~2 dB of a corner but still
C∞, so it is the loudest, densest soft clip that still refuses to alias like a hard one).

#### Hard Clip

An op-amp or diode pair driven into its rails. What makes it a design rather than a `clamp()` call is the
**knee**: a real corner has finite width set by the device's transition region (a diode's ~52 mV, an
op-amp's open-loop rolloff), **fixed in absolute volts**, so it narrows relative to the signal as you
drive. Modelled as a soft-knee quadratic of width `w` — which has the enormous advantage that the entire
curve, `F1` and `F2`, are **piecewise POLYNOMIALS**. No transcendentals, so ADAA-2 costs almost nothing.

```
T = 1, w = 2·(Knee/100) ∈ [0,2],  a = 1 − w/2,  b = 1 + w/2,  v = |u|
  v ≤ a     : f = v
  a < v < b : f = v − (v−a)²/(2w)
  v ≥ b     : f = 1
y = sgn(u)·f(v)                  // verified C1: f(a)=a, f'(a)=1, f(b)=1, f'(b)=0 exactly

F1 (even):  v²/2  |  v²/2 − (v−a)³/(6w)  |  b²/2 − w²/6 + (v−b)
F2 (odd):   sgn(u)·[ v³/6  |  v³/6 − (v−a)⁴/(24w)  |  F2b + (v²−b²)/2 + K·(v−b) ]
            K = b²/2 − w²/6 − b ,  F2b = b³/6 − w³/24

DEGENERATE w < 1e-4 → exact razor branch (avoids the /w blow-up):
  F1 = v²/2 (v≤1) ; v − 1/2 (v>1)
  F2 = sgn(u)·[ v³/6 (v≤1) ; v²/2 − v/2 + 1/6 (v>1) ]
  (algebraically identical to the w→0 limit, and to Faust aanl.lib's hardclipJ1/J2)
```

⚠️ **The trap: the linear region.** A hard clipper below threshold does **literally nothing**, so a naive
mapping gives a genuinely dead first third — the fb286 Diffusion lesson in its purest form. **Three
fixes, all required together:** threshold at −6 dBFS not 0 dBFS; taper `dB = 48·t^0.8` so 25 % of travel
is already +15.8 dB; and a **non-zero default Knee (8 ⇒ w = 0.16)** so even the first degrees of Drive
put the peaks inside the knee.

⚠️ **Third trap: shipping only `Razor`.** A brickwall alone is one sound. The Knee knob plus
Stair/Bevel/Rails is what turns "a clamp" into a mode with a range.

**Characters:** `Razor` (default — `w = 0`, the pure brickwall; deliberately the worst-aliasing voicing) ·
`Round` (Knee mapping ×2 so a fully parabolic corner is reachable in the first half of the sweep — the
mastering-clipper knee) · `Bevel` (a **C2 Hermite** knee instead of quadratic, same width: ~8 dB less
energy above the 9th harmonic at identical THD — **proves knee ORDER matters, not just width**) ·
`Rails` (asymmetric thresholds +1.00/−0.62, a 4:1 diode-count asymmetry — strong H2 **without the
duty-cycle shift Bias causes**, a genuinely different flavour of asymmetry) · `Slam` (corner overshoots
to 1.15 before snapping — op-amp saturation recovery, an audible spit on every clip transition) ·
`Stair` (a **3-step diode-ladder ceiling**: slope 1 → 0.35 → 0.12 → 0 at 0.55/0.78/1.00. Piecewise-linear
so `F1`/`F2` are exact quadratics and cubics — the ugliest, most electrical harmonic set in the mode, and
it costs nothing) · `Sag` (the rail **droops with a 120 ms envelope**, 1.00 → 0.72 under sustained
overdrive — power-supply sag; makes a static clipper breathe) · `Wrap Tip` (beyond `|u| = 1.35` the
output wraps to the opposite rail — a per-sample taste of the `Wrap` pill baked into a voicing).

#### Zero-Square

**A comparator summed with a scaled clean path.** Serum's own behaviour: the zero-crossing becomes a
**cliff face** — the crossings effectively disappear and a sine takes on a square-like shape with sharp
vertical transitions where the smooth crossings used to be. Mathematically this is the **only true
discontinuous shaper in the device**: a jump of height `2p` at `x = 0`. Physically it is a comparator or
Schmitt trigger — the topology of comparator/octave fuzz pedals.

**It is the exact complement of a clipper:** a clipper flattens the peaks and leaves zero alone;
Zero-Square leaves the contour and **detonates zero**. That inversion, not "more drive," is what makes it
a separate mode.

```
p   = 0.92·(1 − Knee/100)          // 0.92 cap is a DRAMATICISM clamp — see below
x_g = 2.5e-4                       // −72 dBFS noise gate — MANDATORY, a STABILITY requirement
γ(v)= min(1, v/x_g)
f(u)= sgn(u)·[ p·γ(|u|) + (1−p)·min(|u|,1) ]
      └ term1: the CLIFF (level-INDEPENDENT above the gate)
        term2: the surviving CONTOUR (level-dependent — keeps Drive alive)

Above the gate:  F1 = p·|u| + (1−p)·F1_hardclip(u)      F2 = p·u|u|/2 + (1−p)·F2_hardclip(u)
Inside the gate: F1 = p·u²/(2x_g) + (1−p)·u²/2          F2 = p·u³/(6x_g) + (1−p)·u³/6

polyBLEP on the crossing (better than generic ADAA here — we know exactly WHERE the discontinuity is):
  if (u[n−1] < 0) != (u[n] < 0):
      d = u[n−1]/(u[n−1] − u[n]) ;  h = 2p
      y[n] −= h·blep(d) ;  y[n+1] += h·blep(d − 1)
  blep() is ALREADY IN THE TREE: SubOsc.h:86 (u+u−u·u−1 / u·u+u+u+1) — re-parameterise from
  phase form to crossing-time form; the polynomial is identical.
```

**Distinct from:** the tell no other mode has — **the output amplitude does not follow the input
amplitude.** A pedestal of height `p` is emitted for a −60 dBFS input and a 0 dBFS input alike. That is
why it flattens a decay into a constant-level square and sounds like an octave/comparator fuzz rather
than distortion. Against Hard Clip: **one full order of discontinuity apart** — waveform jump (`1/n`) vs
first-derivative corner (`1/n²`) — and it does its damage at the **opposite end of the waveform**.

**Bias directly sets PULSE WIDTH**: the flip point moves off zero, so a sine becomes a variable-width
pulse. `b = ±0.9` gives roughly a 5 %…95 % duty sweep — **full PWM, free, from a knob already on the
panel.**

⚠️ **The trap that would make this timid: scaling the pedestal by the input level.** It is the
instinctive "safe" fix and it destroys the mode completely — a level-scaled pedestal is just another
gain-shaped clipper. **The pedestal MUST be level-independent; that is the entire identity.** Solve the
noise-floor problem with the −72 dBFS gate and the Schmitt voicing, **never** by scaling. And do not set
the gate at −40 dBFS "to be safe" — the tails die and the mode reads as a noise gate.

⚠️ **A real dead-knob bug:** at `p` exactly 1.0 the `(1−p)` contour term vanishes and **the Drive knob
does literally nothing.** Hence the `p ≤ 0.92` cap. This is a **dramaticism** clamp, not a taste clamp —
the residual 8 % of contour is 22 dB down and inaudibly different from a pure square in "squareness"
terms, while keeping Drive fully alive across its whole travel. Worth stating explicitly, because
"clamp for stability only" would otherwise appear to forbid it.

**Characters:** `Comparator` (default at Knee 0 — `p = 0.92`, instantaneous edge; a pure square regardless
of input level) · `Pedestal` (`p` forced to 0.5: half square, half contour — the octave fuzz sitting
under the note, and the most musically usable) · `Slew` (the cliff ramps over ~12 µs, a real LM311-class
comparator's finite slew rate — adds 12 dB/oct of rolloff to the step, which **dramatically cuts
aliasing AND thickens the edge**) · `Schmitt` (hysteresis of ±0.03 FS on the flip threshold — kills
chatter on noisy input and introduces a level-dependent duty shift you hear as a growl; **musically
solves the problem the gate solves mechanically**) · `Gate` (the pedestal only engages above a −24 dBFS
input threshold — quiet passages pass clean, loud ones square up: the inverse of a fuzz gate, and the
voicing that makes the mode dynamically playable) · `Duty` (the flip point driven hard by Bias — full PWM
plus an enormous even-harmonic swing at zero extra cost) · `Ring` (pedestal height rides a 5 ms envelope
of `|x|` instead of being fixed, so the square **keeps the note's dynamics**) · `Octave` (the pedestal
fires on the rectified signal, `sgn(|u| − 0.35)`, producing a square at **2× the fundamental** — classic
octave-up comparator fuzz, a genuinely different pitch sensation).

#### Slew Clip  *(ADDED — the family's dual)*

**Instead of clipping the amplitude, clip the SLOPE.** Op-amp slew-rate limiting: the compensation
capacitor can only charge at a fixed current, so the output can only change at ±SR volts per microsecond
regardless of what the input asks for. Exceed it and you get **slew-induced distortion / TIM** — the
effect Otala identified in the 1970s and the reason 741-based gear sounds the way it does. It is also the
"slew" module of every modular system.

```
y[n] = y[n−1] + clamp( u[n] − y[n−1], −s, +s )
asym (Bias):  clamp( ·, −s(1−β), +s(1+β) ),  β = Bias·0.8
soft ('Charge' Character):  e = u[n] − y[n−1] ;  y[n] = y[n−1] + s·tanh(e/s)   // no corner at all

A full-scale sine of frequency f has max slope 2πf/fs per sample, so it tracks cleanly iff
f ≤ s·fs/(2π).  At 48 kHz:
  s = 2.0    → f ≤ 15.3 kHz   (effectively off)
  s = 0.05   → f ≤ 382 Hz     (the destructive sweet spot)
  s = 0.0005 → f ≤ 3.8 Hz     (total sludge)
Knee maps s logarithmically:  s = 2.0·10^(−3.6·(1 − Knee/100))
```

**Distinct from:** the only mode in the device whose distortion is a function of `dx/dt` rather than `x`.
Observable consequences no waveshaper can produce: it turns a square into a **trapezoid then a triangle**
(a shape change no amplitude curve can create); it is **transparent on slow material and destructive on
fast material at the SAME level**, so it distorts a hi-hat and ignores a sub at identical gain — every
other mode in the device is the other way round; it introduces a **level-dependent phase lag**; and it is
the only mode that gets **quieter and darker** at 100 %.

**vs Hi Cut:** a filter is linear and level-independent; Slew Clip's corner frequency **moves with the
signal's own amplitude**. That is genuinely a different lane, so it does not violate
params-play-their-roles. **The harness measurement that proves it** is in §8.2.

⚠️ **Caveat, stated honestly: this is the one mode here with state** (one variable). It is not Family A
hysteresis, it is not a saturation-with-memory model, and it needs no solver — but if the CLIP family must
remain strictly memoryless, this is the one to move. It is far too cheap (4 ops, **0.03 % of a core**) and
too distinct to lose without a fight. It also produces **less aliasing at 1× than any other mode does at
4×**, and adds **zero latency by construction** (no resampler in the path at all), making it the safest
mode to place anywhere in the drag-reorder chain.

⚠️ **The trap: limiting the range to analog-plausible slew rates.** Scaling a 0.5–13 V/µs op-amp spec
honestly gives a barely-audible HF softening — which is why almost nobody ships this as a user control.
**The whole value is in going ~3 orders of magnitude slower than any real op-amp.** Anchor the TOP of the
range in reality (`Fast`/`Slow`) and let the bottom go absurd.

**Characters:** `Fast` (a 5534-class ceiling ≈13 V/µs — only the very top octave is touched; the
expensive-op-amp voicing) · `Slow` (a 741-class ceiling ≈0.5 V/µs — the canonical TIM sound: transients
smear, cymbals go soft, everything sounds like 1974) · `Asym` (rise and fall ceilings at 3:1 — pulls the
waveform off-centre in a **level-dependent** way, strong even harmonics; DC block mandatory) · `Charge`
(the `tanh` soft-limit variant — an asymptotic slope ceiling with **no corner at all**, so essentially
zero new harmonics: the most transparent and most analog-feeling voicing) · `Stick` (a small dead-band of
0.004 FS must be exceeded before slewing begins, so tiny signals **freeze in place** — sticky, granular,
quantised-feeling decays) · `Ring` (the limiter overshoots and rings briefly when it **exits** limiting —
adds a bright zing on every transient edge, the opposite of what a slew limiter is supposed to do, and
instantly recognisable) · `Bounce` (the ceiling itself is modulated by a 40 ms envelope, so **loud
passages slew faster** — keeps transients alive on hard playing and sludges out on quiet playing; the
most dynamically responsive voicing in the family) · `Stereo` (L and R ceilings offset by 15 %, so the
limiting **decorrelates the channels on transients only** — the image opens exactly when the material is
busiest. Free width from a mono-safe mechanism).

---

### 9.3 DIODE — the semiconductor family  *(back-8: Low Cut · Hi Cut · Emphasis · Width · Knee · Dead Zone · Slew · Snarl · signature `Asym` · pill `Octave`)*

All four modes are the **same physical device** — the Shockley junction — read out through four
topologies. Real measured constants for the reference part (**1N4148**, Giangrandi's clipper analysis):
`Is = 4.352 nA, n = 1.906, VT = 26 mV @ 300 K`. Germanium **1N34A**: `Is ≈ 2.14 µA, n ≈ 1.3, Vf ≈ 0.30 V`.
LED: `Vf ≈ 1.8 V`.

**🔑 What Serum's Diode 1 vs Diode 2 actually are — the reasoning, stated honestly.** Xfer publishes
nothing. The only substantive descriptions in circulation describe Diode 1 as *"square-wave based with a
lowpass character"* and Diode 2 as *"a slight attack sound plus lowpass"*. **Two symmetric clippers with
slightly different knees would be blind-indistinguishable and would fail our own dramaticism gate on day
one.** The reading that both matches Serum's words and gives two genuinely different animals:
**Diode 1 = SHUNT CLIPPER (a compressor — crest goes DOWN); Diode 2 = DEAD ZONE / CROSSOVER (an EXPANDER
— crest goes UP, transients punch, sustain dies, which is literally "an attack sound")**. Whether or not
that is bit-for-bit what Xfer did, it is better than what Serum ships and it is defensible from the
circuits.

**The shared Knee-morph basis** (used by Diode 1 and Asym; `F = Σw·F_i` is EXACT because differentiation
is linear, so ADAA survives the morph):

```
hard (n→1, Schottky):  f_h = clamp(x,−1,1)
  F_h  = |x|<1 ? x²/2 : |x| − 1/2
  F2_h = |x|<1 ? x³/6 : sgn(x)·(x²/2 + 1/6) − x/2
exp (Shockley, silicon): f_e = sgn(x)·(1 − e^{−|x|})
  F_e  = |x| + e^{−|x|} − 1
  F2_e = sgn(x)·( x²/2 − e^{−|x|} + 1 − |x| )
tanh (germanium / large series R):  f_t = tanh(x),  F_t = ln·cosh(x)   ← HarmonicEngine::lncoshf_ verbatim
  (F2 needs the dilogarithm — do NOT; use ADAA-1 on the tanh weight)

k = knee01:  w_h = max(0, 1−2k) ;  w_e = 1 − |2k−1| ;  w_t = max(0, 2k−1)
⚠️ Hold the weights frozen across a sample PAIR; on any weight change recompute F(x[n−1]) on the
   CURRENT weights — exactly the cache-invalidation rule already in SynthVoice::applyFoldADAA.
```

#### Diode 1 — Shunt Pair

The clipper stage of an overdrive pedal: a series resistor into two **antiparallel diodes shunting to
ground** (RAT, DS-1, Big Muff). Symmetric. Governing law:
`(Vin − Vout)/R = 2·Is·sinh(Vout/(n·VT))` — transcendental, no analytic solution (Giangrandi solves it
numerically; the WDF literature with Lambert W). **In the resistor-dominated regime a driven pedal
actually operates in (`R·i ≫ Vout`) it collapses EXACTLY to `Vout = n·VT·asinh(Vin/(2·R·Is))`** — which
*does* have an exact antiderivative and ships as the `Log` Character.

```
d = drive01
g = 10^( (48·d^0.8)/20 )
V0 = Character Vf   (Si 0.70 · Ge 0.30 · LED 1.80 · Schottky 0.25 · Stacked 2.10)
V  = V0·(1 − 0.85·d)                     // 🔑 threshold FALLS as drive rises — kills the dead first third
a  = asym ∈ [−1,+1] ;  ρ = 0.98
Vp = V·(1 + a·ρ) ;  Vm = V·(1 − a·ρ)
x' = g·x
y     = x' ≥ 0 ?   Vp ·fK( x'/Vp, k) : −Vm ·fK(−x'/Vm, k)
F(x') = x' ≥ 0 ?   Vp²·FK( x'/Vp, k) :  Vm²·FK(−x'/Vm, k)
  proof: d/dx'[Vp²·FK(x'/Vp)] = Vp·fK(x'/Vp) ✓ ; both branches → 0 at x' = 0, so F is
  continuous across the origin and ADAA works through it ✓

Verified taper (house law 48·d^0.8 + the falling threshold — recomputed; the research agent
quoted 60·d^0.7, which §2.2 reconciled down to +48 dB because the falling threshold already
contributes ~16 dB of effective drive):
   5 % → +4.4 dB,  V = 0.670 →  4.8 dB past threshold
  20 % → +13.3 dB, V = 0.581 → 14.9 dB
  50 % → +27.6 dB, V = 0.402 → 32.4 dB
 100 % → +48.0 dB, V = 0.105 → 64.5 dB past threshold
```

**Distinct from:** the family's **symmetric reference** and the only compressor-shaped member. Against
Diode 2: opposite sign of everything — Diode 2 expands, kills low-level detail and misbehaves worst when
*quiet*; Diode 1 compresses, is **bit-transparent below threshold**, and misbehaves worst when loud.
Against Asym: Diode 1's asymmetry moves the two **thresholds** while the curve shape stays identical, so
the ratio is **level-independent**; Asym moves the **operating point**, so its even-harmonic content
grows and shrinks with the envelope. **Blind test: play a decaying note — Diode 1's even/odd ratio is
constant through the decay, Asym's collapses.**

**Characters:** `Silicon` (default — 1N4148, Vf 0.70, Knee 45: the RAT/DS-1 reference, tight and buzzy) ·
`Germanium` (1N34A, Vf 0.30, Knee 85 — low threshold **and** soft leaky knee, so *everything* clips a
little all the time; stays musical where Silicon has already squared) · `LED` (Vf 1.80, sharp above
threshold, Knee 20 — enormous clean headroom then a brick wall: **the most DYNAMIC voicing**, and the
loudest, since almost nothing is clipped away) · `Schottky` (Vf 0.25, n ≈ 1.05, Knee 5 — near-ideal knee
at a very low threshold: a square at almost any input; the nastiest, most digital voicing and the one
that most needs the auto-promoted oversampling) · `Stacked` (three diodes in series each way, Vf 2.10,
Knee 15 — headroom above full scale then instant square: transients survive, sustain is annihilated) ·
`MOSFET` (square-law body-diode region, `f ≈ x − x³/3`, Knee 70 — H2 and H3 together and **no higher
orders to speak of**; for people who reach for Diode but want Tube) · `Log`
(`f = A·asinh(Bx)`, `F = (A/B)[(Bx)asinh(Bx) − √((Bx)²+1)]` — **the only voicing that never plateaus**:
compresses 60 dB into ~12 dB and keeps growing; behaves like a limiter rather than a clipper) · `Leaky`
(badly-matched high-leakage germanium: ±15 % Vf mismatch between directions, droopy knee, and
**conduction-gated shot noise** with amplitude ∝ √(instantaneous conduction current). It hisses **only
while the diode is actually conducting** and is absolutely silent at rest — so it satisfies the no-noise
rule while giving the dying-battery farting nobody else ships).

#### Diode 2 — Dead Zone

The **other** thing a diode does: conduct in **series** rather than shunt. A series diode, or the
unbiased class-B output stage, passes **nothing** until the input exceeds `Vf` — leaving a **dead zone**
of roughly ±0.7 V around zero. Textbook crossover distortion: odd-order, and uniquely **WORST AT LOW
SIGNAL LEVEL**, because the gap is a fixed absolute width while the signal is not.

**It is the only EXPANDER in the whole device**, which is why it earns a slot outright.

```
d = deadZone ∈ [0, 0.95]        // this mode defaults it to 0.45
w = 0.02 + 0.35·knee01          // corner half-width; knee01=0 → razor notch
u = |x'| ,  s = sgn(x')         // x' = g·x, POST-drive
  u ≤ d − w      : y = 0
  d−w < u < d+w  : y = s·(u − d + w)²/(4w)
  u ≥ d + w      : y = s·(u − d)

F (EVEN, since f is odd; F(0)=0):
  0  |  (u − d + w)³/(12w)  |  (u − d)²/2 + w²/6
  ⚠️ continuity check at u = d+w: (2w)³/(12w) = (2/3)w² ; and w²/2 + w²/6 = (2/3)w² ✓
     Note w²/6, NOT w²/3 — an easy sign-of-life bug that puts a step in F and makes ADAA scream.

Folding the clip stage in (ONE shaper, ONE antiderivative, ONE sample of latency — not a 2-stage cascade):
  u ≥ d+w :  y = s·V·fK((u−d)/V, k) ,  F = V²·FK((u−d)/V, k) + w²/6
  (in the corner region fK is linear to within w/V, so the quadratic branch is exact there;
   continuity verified: V²·FK(w/V) → w²/2, plus w²/6 = (2/3)w² ✓)

Asym slides the gap off-centre:  d⁺ = d(1+a),  d⁻ = d(1−a)
  At a = +1 the negative half has NO gap at all (clean) and the positive has a gap of 2d.
```

**Distinct from:** the **mirror image of everything else in the device**, and it is objectively
measurable: **crest factor goes UP with the Dead Zone knob while it goes DOWN with every other control in
the family.** It removes the QUIET part rather than the loud part, so its artefacts get *worse as a note
decays* — the exact opposite of every clipper, and the reason it sounds alive and unstable where a
clipper sounds solid. Below threshold Diode 1 is bit-transparent while Diode 2 is **silent**; above it
Diode 1 flattens while Diode 2 passes straight through. Against Fuzz-style gates: Diode 2's gate is
**instantaneous and per-sample**, so it chops within a single cycle and produces a *buzz*, not a stutter.

⚠️ **THE trap, and it is very easy to fall into:** if you make the gap **proportional to the signal
level** (or place it after the auto-gain, or scale it with Drive), **it self-cancels and becomes an
inaudible constant.** Dead Zone at 10 and at 90 would sound nearly identical, and you would conclude the
mode was weak and cut it. **It is not weak; it would have been wired wrong.** Absolute threshold,
post-drive, pre-clip. Test by sweeping Drive at a **fixed** Dead Zone of 60 and confirming the sound goes
silence → buzz → solid. That fight between Drive and Dead Zone **is the instrument**, and it gives the
mode a genuinely two-dimensional character space nothing else here has.

⚠️ Second trap: clamping the range at 0.3 "because 0.95 is silent". Silence at extreme settings on quiet
material is **correct behaviour** for a crossover-distortion device. **Cap the makeup, free the knob.**

**Characters:** `Class B` (default — the textbook symmetric 0.7 V gap, razor corner `w = 0.02`: pure
crossover buzz, worst on decays; sounds like a genuinely broken amplifier) · `Class AB` (small gap
~0.15 FS with a soft corner — only the tail fizzes, attacks are clean; **the one that survives on a mix
bus**) · `Gate` (huge gap, razor corner, Auto running hard — only transients speak at all: rhythmic,
percussive, brutal) · `Ge Bridge` (germanium: narrow 0.3 Vf gap, soft corner — buzzy but musical, and the
gap is small enough that pitch survives all the way down the decay) · `Push-Pull` (**TWO gaps**, offset ±
around zero, so the notch is double-humped — hollow phasey rasp with a distinctly different harmonic
comb) · `Sputter` (gap width jittered ±30 % by a slow smoothed noise — the notch **breathes**, so the
sound spits unpredictably. It modulates the **gap**, never the level, so it is **silent at Dead Zone 0**
— no-noise rule satisfied) · `Half Gate` (Asym locked one-sided: one polarity completely clean, the other
fully gated — hollow, honky, octave-adjacent) · `Ratchet` (gap width driven by a fast envelope follower
that **re-closes during a decay**, so a held note machine-guns at a rate set by the follower.
⚠️ **This voicing is stateful — a deliberate exception to the family's memoryless charter.** Flag it in
the code; it means Ratchet cannot ADAA the gap term — run the gap naive inside the 4× and keep ADAA on
the clip stage).

#### Asym — bias-shifted junction

Asymmetry produced by moving the **OPERATING POINT** rather than by unbalancing the components — a DC
bias injected into a symmetric pair, **plus a per-half knee split**. Deliberately a *different mechanism*
from Diode 1's Asym knob.

**Why the distinction matters sonically:** threshold-unbalancing gives a **fixed** asymmetry ratio — the
even/odd balance is the same at every input level. Bias-injection gives a **LEVEL-DEPENDENT** asymmetry —
at low level the operating point sits in the linear region and the sound is nearly symmetric; as the
signal grows, one side reaches the wall far sooner and the even harmonics **bloom**. On a decaying note
the even content **collapses as the note dies.** That is literally *"asymmetric distortion emphasising
attack"*, exactly how Serum's Asym is described, and it is a genuinely different animal you can pick
blind.

```
a  = asym ∈ [−1,+1]
b  = 1.5·a                                  // bias, ±1.5 in units where V ≈ 0.7
kP = clamp(knee01 + 0.60·a, 0, 1)           // per-half knee split — MANDATORY (see below)
kM = clamp(knee01 − 0.60·a, 0, 1)
Vp = V·(1 + 0.85·a) ;  Vm = V·(1 − 0.85·a)

fA(u) = u ≥ 0 ?  Vp ·fK(u/Vp, kP) : −Vm ·fK(−u/Vm, kM)
FA(u) = u ≥ 0 ?  Vp²·FK(u/Vp, kP) :  Vm²·FK(−u/Vm, kM)
y     = fA(x' + b) − fA(b)                  // subtracting fA(b) pins y(0) = 0 ⇒ NO static DC,
F(x') = FA(x' + b) − FA(b) − fA(b)·x'       //   only PROGRAM-DEPENDENT DC — which is the point
  verify: d/dx'[F] = fA(x'+b) − fA(b) ✓ ;  F(0) = 0 ✓
```

**`Line 6` Character** — the Doidic et al. published asymmetric soft clip (US 5,789,689, reproduced in
Pakarinen & Yeh's CMJ review). ⚠️ **Verify the constants against the patent before shipping**; these are
the widely-reproduced values:

```
x < −0.08905            : f = −0.75·( 1 − (1 − (|x| − 0.032847))^12 + (1/3)(|x| − 0.032847) ) + 0.01
−0.08905 ≤ x < 0.320018 : f = −6.153·x² + 3.9375·x
x ≥ 0.320018            : f = 0.630035
```

Piecewise polynomial (plus one power-12 term) ⇒ closed-form antiderivative on every branch, so it is
ADAA-able like the rest.

**DC blocker:** mandatory and **not optional** here — bias injection is the family's biggest DC generator
after Rectify. 1-pole @ 10 Hz below `|a| = 0.5`; **two cascaded 1-poles @ 20 Hz above it** (auto-engaged),
because at `|a| = 1` the offset is ~0.35 FS **and envelope-modulated**.

⚠️ **The trap that would make this timid — and it decides whether the mode ships at all:** implementing
Asym as a *threshold-unbalance* (Vp/Vm only) and calling it done. That is Diode 1's knob with a different
label, it will be blind-indistinguishable from Diode 1 at the same setting, and it will correctly be cut
as a dead type. **The bias injection AND the per-half knee split are both load-bearing.** If either is
dropped in implementation, **delete the mode** and let Diode 1's Asym cover the territory — that is the
honest outcome and it is better than shipping a near-identical curve.

⚠️ Second trap: bias must reach **±1.5, not ±0.3**. `HarmonicEngine`'s FORGE already ships `bias = 0.30·d`
and it is a lovely warm thickener — **which is exactly the ceiling this rule exists to smash.** At 0.3 it
is a tone colour; at 1.5 it is a different instrument. Third: do not over-block the DC to "fix" the
thump — the whole point is that the offset is envelope-modulated.

⚠️ **ADAA gotcha specific to this mode:** the cached `F(x[n−1])` must be invalidated whenever `b`, `kP`,
`kM`, `Vp` or `Vm` change — i.e. on **ANY** Asym or Knee movement, not just at a block boundary.
`SynthVoice::applyFoldADAA` already implements exactly this (`st.sh1`/`st.am1` guard); reuse it rather
than reasoning about it again.

**Characters:** `Ge/Si` (the classic mixed pair: 0.30 V/n≈1.3 one way, 0.70 V/n≈1.906 the other —
**different threshold AND different knee**, which is what a real mixed pair gives and a single-curve
asymmetry cannot) · `Si/LED` (0.70/1.80, a 2.6:1 ratio — the most lopsided realistic voicing and the most
amp-like: one half compresses early while the other has huge headroom, exactly what a single-ended output
stage does) · `Two-Up-One-Down` (the TS-808 asymmetric mod, an exact 2:1 ratio — the reference asymmetric
overdrive and the tightest on a mix bus) · `Grid Bias` (**pure operating-point shift into a perfectly
MATCHED pair** — the curve stays symmetric, only *where you sit on it* moves. **The most level-dependent
voicing of the eight**: nearly clean and symmetric when quiet, violently lopsided when loud. The tube-like
flavour, and the best demonstration of the mode's whole reason for existing) · `Line 6` (the Doidic et al.
published curve verbatim — hard-limited at +0.630035 on one side and a smooth power-12 rolloff on the
other; a genuinely odd shape nothing else here produces, and a **named, citable** curve rather than a
guess) · `Half Hard` (maximum contrast: the positive side is pure `asinh` (never plateaus) while the
negative hard-clips at Vf — **the two halves are literally different KINDS of nonlinearity**) · `Screamer`
(feedback-loop topology rather than shunt: unity gain below threshold, log compression above, so quiet
passages stay genuinely clean and only peaks bend — the most dynamic and transparent voicing) · `Collapse`
(asymmetry depth pushed to 1.0, the negative half clamped at 0.005 FS ⇒ effectively half-wave clipping —
the bridge between this mode and Rectify).

#### Rectify — the octave machine

Fold one polarity onto the other. Physically either a single series diode (**half-wave**) or a bridge /
transformer-and-two-diodes arrangement (**full-wave**). The **Tycobrahe Octavia** does it with a small
transformer that splits the signal into two anti-phase copies, half-wave rectifies each with a germanium
diode, and recombines at the joined cathodes — a full-wave rectified signal at **double the frequency**.

🔑 **THIS IS A PITCH EFFECT DISGUISED AS A DISTORTION, AND THAT IS THE HUGE UNDERUSED LEVER.** The
Fourier facts, computed exactly:

```
FULL-WAVE |sin ωt|:  DC = 2/π = 0.6366 · 2f₀ = 4/(3π) = 0.4244 (−7.4 dB) · 4f₀ = 0.0849 (−21.4 dB)
                     · 6f₀ = 0.0364 (−28.8 dB) · 8f₀ = 0.0202 (−33.9 dB)
                     general term 4/(π(4k²−1)) at 2k·f₀ — decay 1/k², infinite.
                     ⇒ THE FUNDAMENTAL IS COMPLETELY ABSENT.
HALF-WAVE max(sin,0): DC = 1/π = 0.3183 · f₀ = 0.5 (−6.0 dB) · 2f₀ = 0.2122 (−13.5 dB) · 4f₀ = 0.0424
                     ⇒ THE FUNDAMENTAL SURVIVES AT HALF LEVEL — the Green Ringer tone.
```

So **one knob sweeps the fundamental from full → half → ZERO.** Nothing else in the device changes the
perceived **pitch**. Most plugins expose rectification as a fixed on/off or a shy wet blend and throw
that away.

```
a = 2·asym ∈ [−2, +2]
y_r = (1 − |a|)·x + a·|x|
  a =  0   → x                          (clean)
  a = ±0.5 → max(x,0) / min(x,0)        HALF-WAVE
  a = ±1   → ±|x|                       FULL-WAVE
  a = +1.5 → x>0: x ; x<0: −2x          negative lobe inverted AND DOUBLED
  a = +2   → x>0: x ; x<0: −3x          OVER-RECTIFIED

ANTIDERIVATIVES — pure polynomial, no transcendentals (why Rectify gets the highest ADAA order):
  F(x)  = (1−|a|)·x²/2 + a·x|x|/2
  F2(x) = (1−|a|)·x³/6 + a·|x|³/6        // check: d/dx(|x|³/6) = x²sgn(x)/2 = x|x|/2 ✓
  F(0) = F2(0) = 0 ✓
```

⚠️🔑 **CHAIN ORDER — the single most important decision in this mode, and the one that silently breaks
it:**

```
x → [pre-emphasis] → [Low Cut] → drive gain → RECTIFIER (ADAA-2)
  → ★ 2-POLE DC BLOCKER @ 20 Hz, MID-CHAIN, INSIDE THE OVERSAMPLED REGION ★
  → [Dead Zone] → Knee clip stage fK (ADAA-1) → [Slew]
  → downsample → 1-pole DC blocker @ 10 Hz → [de-emphasis] → [Hi Cut] → Auto → Mix
```

The mid-chain blocker is **not optional** and is **not the same** as the post-chain one. Full-wave
rectification of a full-scale sine deposits **0.6366 FS of DC**. If that reaches the clipper, the entire
waveform sits on one rail, the clipper only ever sees one polarity, and **the output is a FLAT LINE with
a bit of ripple** — the mode sounds broken and the bug looks like a level problem. You will spend a day
on it.

Note: aggressive high-passing is **free in this mode** in a way it is not in any other — the fundamental
is already gone.

**Distinct from:** the only mode in the device that changes **PITCH**. Every other mode adds harmonics on
top of a surviving fundamental; Rectify **deletes** it at `a = ±1` and replaces it with `2f₀`. On a bass
line that is a different note. Its transfer function is **non-monotonic** (slope reverses at `x = 0`), so
two input values map to the same output — something no clipper can do at any setting. **Against the FOLD
family:** a wavefolder reflects at a **threshold** and folds *repeatedly as amplitude grows*, so amplitude
becomes timbre; Rectify folds at **zero, exactly once, at a fixed place, regardless of amplitude** — so
its octave is stable and pitched where a wavefolder's timbre swims with the envelope. They sound nothing
alike and should not be confused just because both use the word "fold." **Against a pitch shifter:**
rectification of a *sum* is not the sum of rectifications, so on a chord every pair of partials produces
sum and difference tones — monophonically an octave, polyphonically a **ring modulator**. That dual
personality is unique in the device.

⚠️ **Trap 1, the fatal one:** implementing rectification as a wet/dry blend at the **device's Mix**. Then
full-wave never actually happens and the mode degenerates into "a bit of octave flavour" (§5.3).
⚠️ **Trap 3: do NOT port the repo's existing rectify idiom.** `SynthVoice::applyAmpWarp` mode 9 does
`rect = |s|·2 − 1; y = s(1−a) + rect·a`. The `·2 − 1` is a level hack that only pre-removes the DC **for
a full-scale signal** — on anything quieter it **injects** a negative offset instead of removing one, and
the blend never reaches true `|x|`. Use `y = (1−|a|)x + a|x|` with a real blocker. **Do** reuse the
precedent it establishes though: `spRectDcAL_`…`wtRectDcDR_` (`SynthVoice.h:5093-5100`) are per-osc,
per-channel DC blockers **armed ONLY when the warp is Rectify** — that gating pattern is exactly right.
⚠️ **Trap 4, the timid ceiling:** stopping at `a = 1` because that is "the correct rectifier." Half the
drama lives between 1 and 2.

**The dangerous-but-musical setting is worth calling out** because it is the strongest argument for
`Dead Zone` being a **shared** back knob rather than a Diode-2-only param: Asym 45–60 · Drive 25–50 ·
**Dead Zone 10–25** · Hi Cut ~7 kHz · Character `Octavia`. The Dead Zone gates the octave so it only
speaks on the picking attack and the loudest notes — exactly what the real Octavia does, and why it
sounds like a *performance* rather than an effect.

**Characters:** `Bridge` (true symmetric full-wave, matched diodes, clean `|x|` — the purest octave and
the reference) · `Half Wave` (single series diode: **the fundamental survives at −6 dB** alongside the
octave at −13.5 dB — fat, thick, and the most usable on a bass) · `Doubler` (a true squarer, `y = x²`,
DC-blocked: for a sine this produces **exactly ONE harmonic and nothing else** — a pure clean octave with
no series above it, and the only voicing that does not alias in the `1/k²` way. For anything polyphonic
it becomes a full ring modulator) · `Octavia` (rectify into a hot germanium clip, Vf 0.3, Knee 20 —
modelling the Tycobrahe transformer/germanium/fuzz topology: the Hendrix voicing, where the octave is
dirty and only fully speaks on the attack) · `Green Ringer` (rectified signal mixed back with the
original ~1:1, Dan Armstrong — more intermodulation, less pure octave; the fundamental and its octave
beat and produce the characteristic *ring*) · `Tone Machine` (rectify → fuzz → rectify, Foxx: two octaves
plus square clipping between them — the most extreme stock voicing, and the reason the `Octave` pill
exists as a general mechanism) · `Transformer` (models the **real** Octavia's transformer rather than an
idealised one: the two anti-phase halves mismatched by ±12 % plus a small LF droop, so the octave is
**imperfect and the fundamental leaks through** — exactly why octave pedals sound alive and a
mathematically perfect rectifier sounds sterile. The imperfection is the character, and it is worth
modelling deliberately) · `Over-Rect` (`a` locked at ±2 — spikes three times taller than the positive
half, harmonic series far extended, perceived pitch an inharmonic clang. The destroyed-but-still-musical
voicing, and the one nobody else ships).

---

### 9.4 FOLD — memoryless reflection  *(back-8: Low Cut · Hi Cut · Emphasis · Stages · Spacing · Rebound · Corner · Width · signature `Symmetry` · pill `Track`)*

**Why `Symmetry` and not `Folds` on the front.** v1 §4 proposed `Folds`. A "Folds" knob is a **second
pre-gain** — it multiplies the same variable Drive multiplies, so the two are not in separate lanes, a
direct violation of params-play-their-roles and the first thing Max would catch. Fold **count** belongs on
the back as `Stages` (which sets the ladder's *length*, a genuinely different job) and in Drive's live
readout, so the number matches the sound without spending a knob.

`Symmetry` is the documented orthogonal axis: input gain controls **brightness** (how many folds), DC
offset controls the **even/odd balance** *"without strongly affecting the overall brightness of the
sound"* (Esqueda/Pöntynen/Parker/Bilbao, SMC-17 §6, repeated in Applied Sci. 7(12) §6). Three further
reasons it earns the front slot: it is **the only control that restores a FUNDAMENTAL at high fold
counts** (a symmetric folder's fundamental is `J₁(I)`, which hits exact zeros at `I = 3.83, 7.02,
10.17…` — patches literally vanish at certain Drive settings, and Symmetry is the rescue); both papers
call an LFO on the DC offset *"reminiscent of pulse-width modulation"*, so it wants to be
mod-matrix-adjacent; and at 0 % the even/odd ratio is 0.00 while at ±100 % with 8 folds the even
harmonics **exceed** the odd — blind-testable across the whole travel, with no dead zone anywhere,
because the offset shifts which thresholds the waveform reaches from the very first percent.

**Range per NO-PLAYING-SAFE:** ±100 % maps to a DC offset of **±1.0**, i.e. at full travel the *entire*
signal sits on one side of the ladder and the folder becomes a **half-wave device**. Ugly, lopsided,
DC-hostile, and exactly what somebody wants.

**The generalised ladder — one code path, so Stages/Spacing/Rebound/Corner serve all three modes:**

```
T   = 1/Stages                                       // Stages = 1..32
u   = g·(x + b)
u2  = copysign( T·pow(|u|/T, p), u )                 // Spacing, p ∈ [0.5, 2.2]
k   = (int) floor( (|u2| + T)/(2T) )                 // fold index
e   = |u2| − 2T·k                                    // residue ∈ [−T, T]
if (|e| > (1−c)·T) { t = (|e| − (1−c)T)/(cT) ; e = copysign( T(1 − c·t²/2), e ); }   // Corner
kk  = min( k, ln4/max(ln R, 1e-6) )                  // BIBO clamp on Rebound
y   = copysign( pow(R, kk)·(e/T), u )
```

**⚠️ THE CPU HEADLINE, and it is the architectural reason the timidity in `SynthVoice` need not repeat
here:** `SynthVoice::applyFoldADAA` runs **PER SINE** — up to 4 osc × 16 unison × N voices ≈ **1024
concurrent instances**, so it could afford no oversampling at all and its pre-gain had to be capped at 10
(5 folds) or it would alias into hash. **The Distortion device runs 2 channels on a bus.** 8× on stereo
at 48 kHz = 768 k shaper evaluations/sec; a 3-stage polyphase cascade costs ≈ 55 flops per output sample.
Estimated **0.4–0.7 % of one core** for the whole device at High. The same algorithm per-voice would be
400 %. **The FX-bus fold can be six times wilder for ~1/500th of the relative cost.**

#### Linear Fold

Ideal triangle reflection — the mathematically pure folder. Slope alternates exactly ±1 at every odd
threshold, corners are razor cusps. This is the idealisation of the Serge/CGS linear wave multiplier and
it is byte-for-byte what Vital ships as `linearFold`.

```
tri(v):  q = (v + 1)·0.25 ;  r = q − round(q) ;  return 4|r| − 1
u = g·(x + b) ;  y = tri(u) − tri(g·b) ;  then DCBlocker

F1(x) = (4/a)·G((a·x + 1)·0.25),   G(q) = 2r|r| − r,        r = q − round(q)
F2(x) = (16/a²)·H((a·x + 1)·0.25), H(q) = (2/3)|r|³ − r²/2, r = q − round(q)
```

🔑 **`F1` and `G` are ALREADY IN THE REPO** — `SynthVoice.h:4567-4575` `foldGtri` / `foldFlin`. **Do not
re-derive them.** `F2`/`H` is the only new derivation; verified periodic and continuous
(`H(±½) = 1/12 − 1/8 = −1/24` on both sides).

**Fold count:** `N = floor((g·A + 1)/2)`. At `g = 63, A = 1` ⇒ **32 folds**, corner rate `4N·f0 = 128·f0`.

**Distinct from:** the razor corner is the identity. Against Sine Fold at equal fold count, Linear Fold's
spectrum falls as `1/n²` **forever** (a C1 discontinuity has infinite bandwidth), so it is measurably and
audibly brighter, more brittle and more aliased — spectral centroid ~1.6× higher. Sine Fold is
**hollow/nasal/FM-ish**; this is **glassy/buzzy**. Against West Coast: this ladder is **perfectly even**,
which means the fold corners are evenly spaced in time, so **the aliases stack COHERENTLY into a few
strong inharmonic tones** rather than a diffuse floor — subjectively the worst kind of aliasing, audible
as discrete out-of-tune whistles.

⚠️ **The trap: fold count is a FLOOR FUNCTION of the drive gain.** If Drive is tapered quadratically the
way the existing voice fold is (`pre = 1 + amount²·9`), the low half buys 0–2 folds and the top half buys
3, and **between thresholds NOTHING CHANGES** — the spectral centroid *staircases* instead of sweeping.
Taper so fold count is linear (§2.5); **a LOG taper is exactly wrong here.**

**Characters:** `Serge` (even ladder, Corner 0, Rebound 1.00, Stages 16 — perfect mirrors, maximum
brightness; the "what a folder is" preset) · `Ladder` (Stages 32, Corner 0 — maximum-density even ladder:
brightest, most aliased, most extreme, and it exists so 100 % actually means 100 %) · `Compressed`
(Spacing 0.55 — thresholds pile up toward zero so **the noise floor itself is folded** and dynamics are
annihilated) · `Expanded` (Spacing 1.9 — only the loudest peaks reach the ladder, so **velocity and
articulation become the timbre control**: the most PLAYABLE voicing in the family) · `Lossy`
(Rebound 0.55 — each fold smaller than the last, exactly as the real diode circuits behave: warm,
tapering, and **~8 dB less alias energy for free**. The one to reach for on a full mix) · `Runaway`
(Rebound 1.45 with the ceiling engaged — each fold BIGGER than the last, outer folds slam the tanh
ceiling flat. **Nothing in hardware does this**) · `Rounded` (Corner 75 % — about half the alias energy
and a distinctly softer, more analog linear folder that still keeps the piecewise character) ·
`Broken Mirror` (every second threshold suppressed — the ladder goes uneven, the spectrum grows
formant-like lumps: the closest a pure-math folder gets to the Buchla without the circuit).

#### Sine Fold

`y = sin(a·x)`. 🔑 **On a sinusoidal input this is not "distortion" at all — it is PHASE MODULATION:**

```
sin(I·sin ωt) = 2·Σ_{k≥0} J_{2k+1}(I)·sin((2k+1)ωt)        // Chowning FM/Bessel expansion, I = a·A
```

Everything follows: the spectrum is a **Bessel comb, COMPACTLY SUPPORTED** (harmonics die above
`n ≈ I + 2.5·I^⅓`), the fundamental has **real nulls**, and the timbre marches through Bessel space as
input level changes. **It sounds like FM because it IS FM.**

```
a = 1.571 + t·97.5                       // LINEAR IN THE MODULATION INDEX, not in dB — see below.
                                        // a ∈ [1.571, 99.1], i.e. the same ceiling as g = 10^(36/20).
u = a·(x + b) ;  y = sin(u) − sin(a·b) ;  then DCBlocker
F1(x) = −cos(a·x)/a          F2(x) = −sin(a·x)/a²          // the cheapest mode to keep clean
Fold count: N = floor(a·A/π + 0.5).  a = 99 ⇒ 31 folds.
🔑 ALIAS BUDGET IN CLOSED FORM: n_max ≈ I + 2.5·I^⅓.  I = 99 ⇒ n_max ≈ 111.
   You can COMPUTE the needed oversampling factor instead of guessing.
```

⚠️ **THE TRAP IS ALREADY IN OUR SHIPPED CODE and it is the textbook example.** `SynthVoice.h:4534` sets
`pre = 1 + amount²·5.28318530` (max `2π`) and computes `sin(x·pre)`. For `x ∈ [0,1]`:

* `amount = 0.30` ⇒ `pre = 1.475` ⇒ first turning point at `x = 1.065 > 1` ⇒ **ZERO FOLDS. The first
  30 % of the knob does not fold at all.** That is the dead first third, **provable from the source**.
* `amount = 1.00` ⇒ `pre = 6.283` ⇒ **TWO FOLDS AT MAXIMUM.** Vital ships ~16. **A factor of eight.**

Two compounding mistakes — a ceiling of `2π`, and an `amount²` taper that never reaches `a = π` in the
first third. **Do not repeat either.** Taper so the modulation index `I` is **linear in the knob**
(`I = 1.57 + t·97.5`); because harmonic count ≈ `I`, linear-in-index is linear-in-audible-complexity.

⚠️ **Second trap, unique to this mode and genuinely spectacular if handled right:** the **fundamental
vanishes at J₁ nulls** (`I = 3.83, 7.02, 10.17, 13.32…`). Sweeping Drive, the pitch centre drops out and
reappears. **Do NOT "fix" this** — it is authentic FM behaviour and it is dramatic in the best sense. But
**do** keep Symmetry reachable so the user can put the fundamental back, and **do** make sure Auto uses a
400 ms window, or it pumps on every null.

**Characters:** `Shaper` (`a` capped at π, Stages pinned to 1 — gentle rounding, H2/H3 only, **no folding
at all. THIS IS WHERE THE CUT "SINE SHAPER" MODE LIVES**: warm, subtle, mix glue) · `Bell` (mid index
~16, Rebound 1.0, Symmetry 0 — pure odd Bessel comb: hollow and clean, the reference FM voicing) ·
`Brass` (Symmetry +35 %, index ~24 — even harmonics come in, the comb thickens, the attack blares.
Chowning brass, achieved by DC offset exactly as the papers describe) · `Formant` (Spacing 1.7, high
index — only peaks fold, so a **resonant band appears and TRACKS the input level**: a moving formant
driven by dynamics, the most expressive voicing here) · `Cluster` (`a = 99`, Stages 32 — the full
111-harmonic comb: screaming, dense, borderline unusable, and the reason the ceiling exists) · `Hollow`
(Rebound 0.6 at high index — outer lobes shrink, the comb tilts down: nasal, reedy, clarinet-gone-wrong.
Very different from `Bell` despite the same maths) · `Ripple` (Spacing 0.6 at high index — folds pile up
near zero so even whisper-level material is fully folded; dynamics erased) · `Chime` (Corner 45 %,
blending a triangle cusp into each sine lobe — adds a razor edge and a `1/n²` tail on top of the Bessel
comb: **brighter than Bell without adding a single fold**).

#### West Coast (Wavefolder)

The circuit-modelled member and the flagship. Three real circuits, selectable by Character, all fully
derived in peer-reviewed papers.

**(a) BUCHLA 259 TIMBRE CIRCUIT** — Esqueda, Pöntynen, Välimäki & Parker, DAFx-17. **Five op-amp folding
cells in PARALLEL** alongside a direct path, summed by two inverting amplifiers with large
alternating-sign weights. Unlike every other folder the thresholds are **UNEQUAL** and the resulting
segment slopes are **UNEQUAL** — and that irregularity *is* the character.

```
s = sgn(Vin)
V1 = (|Vin| > 0.6000) ? 0.8333·Vin − 0.5000·s : 0        // check: 0.8333·0.6 − 0.5 = 0 ✓ (continuous turn-on)
V2 = (|Vin| > 2.9940) ? 0.3768·Vin − 1.1281·s : 0
V3 = (|Vin| > 5.4600) ? 0.2829·Vin − 1.5446·s : 0
V4 = (|Vin| > 1.8000) ? 0.5743·Vin − 1.0338·s : 0
V5 = (|Vin| > 4.0800) ? 0.2673·Vin − 1.0907·s : 0
Vout' = −12.000·V1 − 27.777·V2 − 21.428·V3 + 17.647·V4 + 36.363·V5 + 5.000·Vin
then one-pole LPF, fc = 1/(2π·R_F2·C) = 1.33 kHz  (R_F2 = 1.2 MΩ, C = 100 pF)

Resulting piecewise slopes (derived from the above — THIS is the character):
  |Vin| 0–0.6     slope +5.000   out at 0.6   = +3.00
        0.6–1.8   slope −5.000   out at 1.8   = −3.00
        1.8–2.994 slope +5.134   out at 2.994 = +3.13
        2.994–4.08 slope −5.332  out at 4.08  = −2.66
        4.08–5.46 slope +4.387   out at 5.46  = +3.39
        >5.46     slope −1.675   out at 10.0  = −4.21
Normalise by 1/4.21; small-signal gain is 5.0, so scale Drive accordingly.

EXTENDED LADDER (required — see below):
  authentic t = {0.6, 1.8, 2.994, 4.08, 5.46}, mean spacing ΔT = 1.215 V
  for k > 5:  t_k = 5.46 + 1.215·(k−5), slope pattern cycles with |slope| held near 5
  ADAA is closed form per segment: on segment j with slope m_j and offset c_j,
    F1 = m_j·x²/2 + c_j·|x| + K1_j          (K1_j for continuity at t_j)
    F2 = m_j·x³/6 + c_j·x|x|/2 + K1_j·x + K2_j
  Precompute the 33-entry K1/K2 table once per (Stages, Drive) change — block rate.
```

**(b) SERGE VCM** — Applied Sci. 7(12). **Six identical diode-pair stages in SERIES**, closed form via
Lambert W. The 1980 catalogue: *"a sweep of the odd harmonics (1, 3, 5, 7, 9, 11 and 13th) when a
triangle wave is applied."*

```
Vout = Vin − 2λ·η·VT·W( (R1·Is/(η·VT))·exp(λ·Vin/(η·VT)) )
  λ = sgn(Vin) ; Is = 2.52 nA ; η = 1.752 ; VT = 25.864 mV ; R1 = 33 kΩ
  F(Vin) = Vin²/2 − (η·VT)²·Ψ(Ψ + 2),  Ψ = W(...)          // eq 48
  Topology: GS → [SWF ×6 in series] → ×4 makeup.  GS ∈ [−8, +8] in the paper.
```

**(c) LOCKHART / CGS** — SMC-17. An NPN/PNP pair tied at base and collector, also Lambert W.

```
Vout = λ·VT·W( Δ·exp(λ·β·Vin) ) − α·Vin
  α = 2R_L/R ;  β = (R + 2R_L)/(VT·R) ;  Δ = R_L·Is/VT
  R = 15 kΩ ; R_L = 7.5 kΩ (unity small-signal gain) ; VT = 26 mV ; Is = 1e-17 A
  F(Vin) = (α/2)Vin² − (η·VT/(2β))·Ψ₁(Ψ₁ + 2)              // eq 46
  Topology: Vin → GL → (+dc) → ×⅓ → LWF ×4 → ×3 → tanh → LPF 1.3 kHz.  GL ∈ [−10, +15].
  Corner knob maps to R_L: 1 kΩ (soft) … 50 kΩ (razor) — exactly as the paper describes.
```

**W() evaluation:** Fritsch's iteration (Applied Sci. §4.1, ~11× faster than Halley), or better a
**1024-point log-domain LUT of `W(e^z)` over `z ∈ [−40, 900]` plus one Fritsch step** (~8 flops).
⚠️ **MANDATORY: for large `z` use `W(e^z) ≈ z − ln z + (ln z)/z`.** The paper notes the argument is
already `1.52e44` at Vin = 5 V; at our 40 V ceiling it is **~1e383, which overflows DOUBLE, not merely
float** ⇒ inf → NaN on the audio thread. Not optional.

**Distinct from:** the only member with an **UNEVEN ladder**, and it is audible, measurable and the whole
point. Linear Fold's thresholds are equally spaced with slopes all ±1, so its corners are **periodic in
time** and its harmonics form a regular comb. The Buchla's are aperiodic, so the harmonics **clump into
formant-like lumps** and — a real bonus — **the aliases do NOT stack coherently**, giving a diffuse floor
rather than discrete whistles. Note also the final slope of **−1.675 versus ±5**: the top of the transfer
curve **flattens**, so the loudest peaks *compress* instead of folding hard. **That is the "warmth"
people hear in a 259, and no ideal folder has it.** Blind test: this is the one people call "the analog
one."

⚠️🔑 **THE TRAP HERE IS OPPOSITE TO EVERY OTHER MODE: FIDELITY IS THE ENEMY.** Build the circuit exactly
right and you have a 5-fold folder with a 1.33 kHz output pole — warm, polite, and with the top 50 % of
Drive doing nothing because the ladder ran out of thresholds at the halfway point. It will measure as
authentic and **fail the dramaticism rule outright.** The `259 Extended` Character is not a bonus, it is
the fix, and **it must be the default.**

⚠️ Second: the authentic 1.33 kHz pole makes 32 folds sound **muffled**. Keep it on `Buchla 259` (that is
what makes it *right*) and open it to 8 kHz everywhere else, or nobody hears what Drive is doing.
⚠️ Third: the first threshold is 0.6 V and small-signal gain is 5, so a normalised ±1 input **already
folds once at Drive 0**. Scale Drive so 0 % lands the peak at 0.55 V — just below the first threshold —
or Drive 0–20 % is indistinguishable.
⚠️ Fourth: the −1.675 final slope is authentic and warm, but if the extended ladder inherits it every 5th
fold the whole thing goes soggy at high Stages. Renormalise the extension to `|slope| ≈ 5` and keep the
flattening **only as the FINAL segment**.

**polyBLAMP implementation note** (Buchla Characters only): rewrite each folding cell as an **inverse
clipper** (DAFx-17 eq. 25) — output = Vin when past threshold, clamped otherwise — apply polyBLAMP to
*that*, then undo with eq. 29. **That intermediate step is the whole trick** and it is what makes BLAMP
tractable per branch. Generic crossing detection (the paper assumes a known sinusoid; we cannot):
linear-interpolate the fractional crossing `d`, scale by `µ = |Δslope · ẋ|/fs`, add the two-point residual
(Table 2: span `[−T,0]` ⇒ `d³/6`; span `[0,T]` ⇒ `−d³/6 + d²/2 − d/2 + 1/6`).
⚠️ **Disable polyBLAMP above corner rate `fs_os/4`** — past that two corners land in one sample interval,
the isolated-corner assumption fails, and **the correction becomes WRONG rather than merely absent**,
which sounds worse than no correction. Hysteresis + a 10 ms crossfade on the switch.

**Characters:** `Buchla 259` (the authentic 5-cell parallel ladder, exact paper coefficients, 1.33 kHz
pole — the museum piece and the reference; 5 folds maximum, by design) · **`259 Extended`** (the same
irregular geometry continued to 32 thresholds at the circuit's own 1.215 V spacing, pole opened to 8 kHz
— **the thing Buchla never built. The flagship extreme voicing and the recommended DEFAULT**) ·
`Serge VCM` (six identical diode stages in series, ×4 makeup, GS to ±8 — softer fold points, more even
spacing, less aliasing) · `Lockhart / CGS` (four BJT stages, ⅓ pre / ×3 post, tanh buffer, 1.3 kHz pole,
R_L = 7.5 kΩ — evenly distributed folds, warmer and rounder than the Buchla) · `Metalizer` (Lockhart at
**R_L = 50 kΩ**, the paper's explicit worst case: *"as R_L is raised, the steepness increases… more
abrasive tones"* — sharpest fold points, brightest, most aliased) · `uFold` (series topology with
per-stage inter-gain spacing, Intellijel-style, 2–6 stages — the clean, controllable, modern eurorack
folder) · `Timbre Sweep` (**drive hard-wired to the input's own envelope follower**: loud passages fold
deep, quiet passages stay clean. **The AUTHENTIC 259 articulation** — the module was played by modulating
amplitude — and the most expressive thing in the device) · `Cold Solder` (static per-instance component
tolerance: every threshold offset by a fixed random ±8 %, every slope by ±5 %, plus an L/R mismatch. The
ladder stops sounding like a ladder; folds decorrelate and aliases spread further. **A fixed offset, not
noise** — passes the no-noise rule).

---

### 9.5 SHAPER — arbitrary / user-drawn curves  *(back-8: Low Cut · Hi Cut · Emphasis · Width · Smooth · Bias · Beyond · Squash · signature `Morph` · pill `Sym`)*

The mechanics — representation, the ADAA-LUT, `Beyond`, the declick crossfade, Send To Shaper, the
editor — are all in **§6**. This section is the four modes.

All four share one engine. **We write ONE core and four front-ends.**

#### Shaper *(symmetric drawn curve)*

Serum's X-Shaper, decoded from the manual: *"an X-Y graph, with X representing input level, and Y the
corresponding remapped output level… the lower-left point represents silence (−INF dB) and the top-right
the highest level (0 dB)."* That only makes sense one way: **the graph domain is `|x| ∈ [0,1]` and the
sign is restored after.** It is a magnitude-remap, which is why it is guaranteed symmetric.

```
v = drive·x + bias        // drive ∈ [−6, +48] dB, bias ∈ [−1,+1]
u = beyond(v)             // §6.4
y = sgn(u)·c(|u|)         // 🔑 the symmetry law: ONE table over [0,1]
Symmetry bookkeeping: f odd ⇒ F1 EVEN ⇒ F1(u) = F1(|u|) (no sign branch); F2 ODD ⇒ F2(u) = sgn(u)·F2(|u|)
```

**Distinct from Shaper Asym:** the **EDITOR IS A DIFFERENT PICTURE**. Here you draw on `|x| ∈ [0,1]`, so
you get the full 456 px of graph width for the half you actually care about (**2× the drawing
resolution**), the output is **provably odd** (zero even harmonics, zero DC — the blocker can be OFF,
Auto's makeup is exact), and **you physically cannot draw an asymmetry by accident.** The drawn data
cannot be shared between the two — different domains. This is a real distinction, not a preset, and it is
why Serum ships both.

**Characters:** `Clean` (linear-domain traversal, C¹ rounding, ADAA-1 + 2× — the neutral reference; the
graph is exactly what you hear) · **`Log`** (traverse in dB: `u' = (A^{|u|} − 1)/(A − 1)`, `A = 1000`, so
the left 20 % of the graph owns **40 dB** of quiet detail. Trash 2 ships exactly this and calls it
"smoother and less harsh". **Without it, 80 % of a drawn graph is inaudible on a real signal — this
voicing is what makes the editor usable**) · `Razor` (rounding at the `ω = 2Δ` floor and the ADAA escape
threshold raised to 1e-3, so the corners bite and the escape branch leaks raw curve — maximum grit,
deliberately the ugliest voicing we ship) · `Glass` (K = 4 rounding plus the emphasis pair at 2 kHz —
halves the harmonic order and moves the distortion into the mids: the drawn curve becomes a colour) ·
`Twice` (the curve applied **in series with itself**, `g(g(x))` — harmonic order **squares** (a 5th-order
curve becomes 25th) with nothing new drawn. The cheapest way to make any gentle curve violent) · `Growl`
(`0.35·y[n−1]` summed into the input before the curve — **the only voicing in the family with MEMORY**:
THD now tracks program level and history, so it passes the Family-A level-dependence metric a static
curve cannot) · `Sticky` (Schmitt-style hysteresis on the read index with a ±ω dead-zone — stepped
sample-and-hold-ish grit at low level that **cleans up when you play hard**, the opposite dynamic to
Squeeze) · `Squeeze` (a 3 ms/80 ms soft limiter normalises the input *into* the curve so a quiet passage
traverses the full drawn shape — the "curve character at any level" voice, and the direct answer to the
no-dead-first-third clause).

#### Shaper Asym *(bipolar drawn curve)*

Serum: *"the middle of the graph represents silence, the top-right the highest positive value, the
lower-left the highest possible negative value. Asymmetric distortion allows you to bring out the
even-order harmonics… often the case in guitar amps, one pole will be distorting while the other may
remain relatively undistorted."* Domain is the full bipolar `x ∈ [−1,1]`, `y = f(x)` directly, no sign
trick, **mandatory DC blocker**, K = 4096 cells over `[−1,+1]` (exactly 2× Shaper's table).

**Distinct from Shaper:** you draw the negative half **explicitly**, so "flatline one pole, leave the
other alone" is a two-second drag here and **IMPOSSIBLE** in Shaper. It is the **even-harmonic pole** of
the family: with the negative half drawn flat you get half-wave behaviour, H2 dominant, **+12 dB of
second harmonic** and a fat octave-down thickness Shaper cannot produce at any setting. From Tube/Diode
(the device's other even-rich modes): those have ONE fixed asymmetry shape; here the asymmetry **profile
is arbitrary** — you can make the positive half fold while the negative compresses, which no circuit does.

⚠️ **The trap: shipping this as "Shaper with a wider graph."** It must have its own identity from the
first second — **default curve A to a HALF-WAVE shape** (positive half a soft knee, negative flat) so
opening the mode immediately sounds fat and even-rich, not identical to the symmetric mode.
⚠️ Second trap: a naive DC blocker charging from zero thumps on every big Bias move and clicks on every
note-on — **prime the state** (§4.1), and put the DC metric and the click sweep in the harness.
⚠️ Third: Bias range. ±0.3 would be polite and useless; **±1.0 is the only range where the knob's top
does something you cannot get any other way.**

**Characters:** `Direct` (the drawn bipolar curve, C¹ rounding, blocker primed — offset removed, nothing
else) · `Halves` (**the positive half reads curve A and the negative half reads curve B**, so Morph stops
being a crossfade and becomes a per-polarity selector. Draw a clipper as A and a folder as B and one
waveform gets both treatments simultaneously — **maximum even-harmonic content available in the device,
and impossible in any other mode**) · `Log Both` (dB-domain traversal applied independently to each half,
so the low-level asymmetry — which is what the ear reads as "valve" — becomes reachable on the graph) ·
`Razor` · `Glass` (as Shaper) · `Twice` (`g(g(x))`: on an asymmetric curve this **compounds the offset as
well as the harmonics**, so it is far more dramatic here — the second pass operates on an already
DC-shifted signal; blocker after **both**) · `Growl` (memory + asymmetry = the closest this family gets to
a real tube: the THD profile changes **shape** with level, not just size) · `Bias Walk` (Bias re-derived
from a 30 ms RMS follower, range scaled by the knob, so the operating point **drifts with the program**
exactly as a real grid-biased stage does. Not an LFO, so the matched-pair law does not apply; at Bias 0 it
is completely inert).

#### Harmonics *(draw the spectrum — Chebyshev-synthesised curve)*

**You do not draw the curve; you draw the RESULT.** 16 bars = the amplitudes of harmonics 1…16, and the
engine synthesises the transfer function that produces exactly those harmonics. **Le Brun's classical
waveshaping synthesis (1979)** on Chebyshev polynomials of the first kind, whose defining property is
`T_k(cos θ) = cos(kθ)` — the k-th polynomial maps a unit-amplitude sinusoid to its **k-th harmonic and
nothing else**.

```
T₀ = 1, T₁ = x, T_{k+1} = 2x·T_k − T_{k−1}
f(x) = Σ_{k=1..16} a_k·T_k(x)          // a_k = user bar level; for a unit sine the output spectrum
                                       //   is EXACTLY {a₁ … a₁₆}

NORMALISATION (a stability clamp): |T_k(±1)| = 1 ∀k, so |f(±1)| ≤ Σ|a_k| ≤ 16.
  Bake, then scale by g = 1/max(1, max_k |f_baked[k]|).

⚠️ DOMAIN CLAMP (mandatory): outside [−1,1] Chebyshev polynomials EXPLODE — T_k(x) ~ (2x)^k,
   so T₁₆(1.5) ≈ 4.3e7. The `Beyond` knob's 0 position is a HARD clamp that cannot be defeated
   in this mode. BIBO, exactly the class no-playing-safe permits.

DISTORTION INDEX ('Index' Character): let the input amplitude ride instead of normalising it.
  Because T_k only produces the pure k-th harmonic AT amplitude 1, a quieter input produces a
  DIFFERENT, lower-order spectrum — Le Brun's classic result that input amplitude becomes a
  timbre control. A genuine dynamic response from a memoryless curve.
```

**Distinct from:** **the only mode in the entire device where you specify the OUTPUT, not the process.**
Everything else asks "what shape should the curve be?"; this asks "what should it sound like?" and solves
for the curve. That inverts the workflow and reaches sounds nobody would ever draw — try hand-drawing a
curve that produces **only** the 11th harmonic. It owns a region no other mode reaches: pure
single-harmonic multiplication (an instant 3-octave-up ring that tracks the note perfectly), formant-like
fixed combs, and precise even-only octaver behaviour. Small bar moves produce **large, uncorrelated**
curve changes.

**Its aliasing is computable analytically** — a sum of Chebyshev polynomials to order K applied to a sine
at `f0` produces harmonics up to **exactly `K·f0`** and not one hertz further. There is no infinite tail.
So the alias-free condition is exact: `K·f0 < fs/2`. **The engine knows the highest non-zero bar and can
pick the factor exactly** (caveat: that bound holds for a single sine at amplitude 1; real polyphonic
input intermodulates and `Beyond` re-introduces discontinuities, so it is a floor-selector, not a
guarantee).

⚠️ **The trap: normalising too hard.** If `Pure` is the only voicing the mode is level-independent, which
sounds impressive for ten seconds and then sounds like a static filter — the drama would live entirely in
dragging bars, not in *playing*. **Ship `Index` as the default.**
⚠️ Second: 16 flat bars is genuinely painful and it will be tempting to scale the top bars down "for
safety". **Do not** — ship `Ramp` for the people who want it musical and leave the raw draw raw.
⚠️ Third: too few bars. 8 would be safe and boring; **16 is where you reach four octaves up**, and the
exact-bandwidth AA means 16 costs no more CPU than 8 at low notes.

Note: Chebyshev polynomials **do** have trivial analytic antiderivatives
(`∫T_k = ½[T_{k+1}/(k+1) − T_{k−1}/(k−1)]` for `k ≥ 2`) — but DAFx25 measured the LUT path as **60.8 %
(-O0) to ~67 % (-O3) FASTER** than analytic evaluation, so we use the LUT anyway and get one code path
for the whole family.

**Characters:** `Pure` (exact Chebyshev sum with the input hard-normalised to ±1, so the drawn bars **ARE**
the output spectrum regardless of how the note was played — the reference, and what makes the editor
honest) · **`Index`** (Le Brun's distortion index, input **not** normalised: a quiet note produces a
lower-order spectrum and a loud note the full drawn one. A memoryless curve that behaves dynamically —
**the most musical voicing and the default for playing**) · `Odd Only` (even-`k` bars muted ⇒ the curve is
odd ⇒ zero DC, hollow square/clarinet family, and the Sym law satisfied by construction so the blocker can
be bypassed for a cleaner low end) · `Even Only` (odd-`k` muted except the fundamental — pure octave-up
tube colour; **the fastest route to an octaver in the whole plugin** and something no drawn curve gets to
reliably) · `Ramp` (bars multiplied by `1/k` before synthesis — turns a flat 16-bar draw from a shriek into
a rich brass-like spectrum; the "make my drawing musical" voicing) · `Comb` (alternating **sign** on `a_k`
— same magnitudes, flipped phases: the curve changes completely, the static spectrum barely does, but the
intermodulation and waveform shape become nasal and formant-like. **Proof that phase matters in
waveshaping**) · `Stretch` (evaluated on a warped domain `x → sgn(x)|x|^γ` from the Smooth knob, so the
harmonic ladder **detunes inharmonically** — bell/metallic/gong; the sound-design voicing) · `Beyond` (the
mandatory `[−1,1]` clamp replaced by **reflect**, so overdriven input re-enters the polynomial's lobes from
the other side and the Chebyshev curve **becomes a wavefolder** — the extremity voicing).

#### Table *(a wavetable frame as the transfer curve)*

The curve source is one frame of a Terrain wavetable (`Wavetable.h kFrameSize = 2048`): any factory table,
any user-imported WAV, any resynthesised frame becomes a transfer function, and **Morph sweeps the frame
index** so the curve continuously morphs through the whole table.

```
f[k] = frame[ floor(k·2048/K) ] with linear interpolation
Frames are already bipolar ~±1, so wavetable phase φ → curve domain u = 2φ − 1.
Morph m selects frame m·(numFrames−1); crossfade the two neighbouring frames AT BAKE TIME
(message thread), never per sample; rebake on Morph change with the same 40 ms table crossfade.
Rate-limit rebakes to one per 20 ms ⇒ ~50 bakes/sec of a 4096-cell table (~40 µs each) on the
message thread, ZERO audio-thread cost.
🔑 Peak-normalise each baked frame to 1.0 or switching frames steps the level — a click.
```

🔑 **The `Mip` voicing is the interesting one.** Terrain's tables already carry **34 sixth-octave mip
levels** (`Wavetable.h kNumMipLevels = 34`), each band-limited to a different harmonic cap. In a
wavetable oscillator the mip is chosen by **pitch**; here the analogue of pitch is **SIGNAL SLEW** — how
fast the input traverses the curve. Pick the mip from a running estimate of `|u[n] − u[n−1]|` and you get
**slew-adaptive band-limiting**: quiet slow signals read the sharp frame, loud fast signals automatically
read a smoother one. **That is anti-aliasing for free, using tables already in memory** — a genuinely
novel application of the existing mip ladder. Prototype it in the harness before trusting it.

**Distinct from:** its DSP core is **identical to Shaper Asym**, and that must be said plainly, because
it is exactly the argument used to cut `Custom`. What saves it is that **the CURVE SOURCE changes the
instrument, not just the preset**: Morph here sweeps **256 distinct transfer functions in one gesture**,
which is not reachable by crossfading two hand-drawn curves and is not reachable by any other mode. Drop
a drum loop in as a wavetable and its frames become 256 chaotic transfer curves you can play through — a
sound Serum, Vital and Phase Plant cannot make.

⚠️ **If the roster runs out of slots this is the first cut in the family.** Shaper, Shaper Asym and
Harmonics are all more essential. Better to say that now than defend it later.

⚠️ **Audio-rate Morph is rate-limited to 50 Hz of rebakes**, so a mod-matrix routing of an audio-rate LFO
to Morph becomes a **stepped, gargling, granular** destruction rather than smooth FM. That is a
legitimate and wanted sound — **document it rather than hiding it.**

**Characters:** `Frame` (one static frame, peak-normalised, C¹ smoothing — Morph is inert, so it behaves
exactly like a drawn curve you did not have to draw) · **`Sweep`** (Morph scans the whole table with
bake-time crossfade between neighbours — **the signature voicing: one knob, 256 transfer functions**, and
a mod destination, so an envelope can rewrite the distortion across a note) · `Mip` (slew-adaptive mip
selection — **the sound self-limits its own aliasing as you drive it harder**, costs nothing, uses tables
already in RAM. The CPU-friendly default) · `Spectral` (frame band-limited to a fixed harmonic cap before
baking — a deliberately soft-focus version of any table) · `Half` (only the frame's first half is used,
mirrored — forces odd symmetry ⇒ zero DC, no blocker needed, pure odd harmonics from an arbitrary
wavetable. The clean-up voicing for hostile frames) · `Phase` (the frame read with a phase offset — Bias
rotates it — so **which part of the waveform sits at the signal's zero-crossing changes**. Massively
asymmetric and totally different in character for a knob move; nothing else in the device does this) ·
`Noise` (a noise/PCM frame with normalisation defeated and mip-dropping OFF — **any input becomes
full-scale broadband noise gated by the input's amplitude**. Deliberately included, deliberately
horrible, deliberately reachable) · `Two-Table` (curve A from the wavetable, curve B from the hand-drawn
editor, so Morph crossfades **machine ↔ hand** — draw the tame end yourself, let the table supply the
wild end, put an envelope on Morph).

---

### 9.6 DIGITAL — sample-domain destruction  *(back-8: Low Cut · Hi Cut · Bits/Rate · Smooth · Dither · Jitter · Spread · Feedback · signature `Crush` · pill `Clean`)*

🔑 **ANSWER TO v1 OPEN QUESTION #1 — DO NOT MERGE Crush + Downsample.** Vital, the closest architectural
relative, ships them as **two separate distortion types** (`kBitCrush` and `kDownSample`) and is right to.
They are audibly opposite and **the discriminator is measurable**: sweep the input level −40 → 0 dBFS and
plot THD. **Bitcrush's artefacts are LEVEL-DEPENDENT** (THD falls ~6 dB per bit of headroom gained), so
what you hear is a note's **decay being progressively eaten**. **Downsample's are LEVEL-INDEPENDENT**
(flat THD vs level), so what you hear is a fixed inharmonic metallic partial set riding a normally-shaped
envelope. **One front gesture cannot express both.**

Instead: keep them separate as two front gestures and put **the OTHER axis on back knob 3**, so you can
still stack them **without a second device instance**. That keeps two dramatic single-gesture modes AND
full cross-crushing, and costs no roster slot — so the slot the merge would have freed is instead spent on
**Overflow**, which no competitor synth ships.

**Whole family < 1 % of one core, stereo.** Downsample ~8 flops/sample, Bitcrush ~6, Overflow ~5 + 4 for
the mandatory blocker. Downsample and Bitcrush **together** are cheaper than the reverb's input
diffusers. **This family is where the device banks its CPU budget for Fold and Tape.**

⚠️ **Hoist the transcendentals to block rate** — `D = exp2f(1−bits)`, `inc = fs_r/fs`,
`aRec = expf(−2.827·inc)`, `t = powf(0.05, k)` are all once per block, then per-sample-glided with the
existing `x += (target − x)·smth` idiom. `powf`/`exp2f` per sample would cost ~20× a multiply and would be
the only expensive thing in the file. ⚠️ **Gate the RNG:** skip the xorshift calls entirely when
`Dither == 0` **and** `Jitter == 0` (the default), so the default state is free **and bit-exactly
noise-free** per the no-noise rule.

#### Downsample

Zero-order-hold decimation with **no decimation filter** — the time axis quantised. A phase accumulator at
`inc = fs_r/fs` latches the input on overflow and holds. Spectrally: a Dirac comb at `fs_r` convolved with
a rect of width `1/fs_r` ⇒ replicas of `X(f)` at every `k·fs_r` weighted by the **sinc aperture**
(first sidelobe −13 dB, envelope ~3.9 dB/octave), **plus** everything above `fs_r/2` folding down.

🔑 **Two artefact classes that are usually confused and are removed by DIFFERENT filters:**
**ALIASING** (input content above `fs_r/2` — only a **PRE** filter stops it) and **IMAGING** (the replicas
above `fs_r/2` — only a **POST** filter removes them). D16's Decimort 2 names them exactly: *"Approximation
filter"* (pre) and *"Images filter"* (post). **Our `Clean` pill = the pre filter; our `Smooth` knob = the
post filter. Two genuinely different lo-fi sounds from one engine.**

```
fs_r = 20·pow(sampleRate/20, k)          // EXPONENTIAL IN HZ, ~11 octaves. Never a ratio of fs.
inc  = fs_r/sampleRate
aRec = expf(-2.827f·inc)                 // fc ≈ 0.45·fs_r   (2π·0.45 = 2.827 — the comment in
                                         //   VintageReverb.h:126 is exact; reuse verbatim)
incN  = inc·(1 + jitter·rand11())        // clamp to [1e-5, 1]
phase += incN ;  if (phase >= 1) { phase -= 1 ; heldPrev = held ; held = x ; }
zoh = held ;  foh = heldPrev + (held − heldPrev)·phase        // FOH aperture is sinc² ⇒ images
y   = zoh + (foh − zoh)·smooth                                //   fall TWICE as fast — Redux's hard/soft axis
recLp = (1 − aRec)·y + aRec·recLp ;  y += (recLp − y)·smooth
```

**Distinct from Bitcrush:** it leaves the amplitude axis untouched. On a decaying note Downsample keeps
the envelope shape intact but hangs a **fixed inharmonic partial cloud** at `|k·fs_r ± f|` over it — and
because `fs_r` is in Hz, **that cloud does NOT transpose with the note** (the *"same ringing pitch on all
notes"* tell that the Serum manual itself calls out for an FX-bus rate reducer). Feed it DC and it outputs
exactly the DC (identity), which no waveshaper does.

⚠️ **Three traps.** (1) **A linear rate taper** makes 48 → 24 kHz half the knob and almost inaudible.
(2) **Auto eating the effect:** low rates pump the level hard, and a fast makeup detector irons the
pumping out — **which IS the effect.** ~300 ms RMS, makeup capped +12 dB. (3) **`Smooth` defaulting above
zero** hides the mode's identity behind a low-pass and the first thing a user hears is "a bad filter."
**`Smooth` defaults 0 and `Clean` defaults OFF.** The polite band-limited version must be a deliberate
choice (the `Telephone` Character / the `Clean` pill), never the default.

**Characters:** `Modern` (default — pure ZOH, no pre or post filter, flat sinc aperture: the raw staircase
reference) · `SP-1200` (E-mu, 12-bit / 26.04 kHz: ZOH plus a gentle 2-pole image filter and a slight ladder
droop, back `Bits` biased toward 12 — **the hip-hop sampler grit that lives in the dynamics rather than the
buzz**) · `Fairlight` (8-bit CMI: image filter OFF, sinc droop exaggerated, brightest hash of the set —
harsh, glassy, era-correct) · `Telephone` (the `Clean` path pre-loaded: 4-pole pre band-limit plus a
300 Hz–3.4 kHz channel band (G.711) — **zero aliasing, pure band-limited lo-fi**. The "clean lo-fi" half of
the mode's personality and a completely different sound from the other seven) · `Warble` (Jitter floored at
+25 % and reshaped as a **low-frequency random WALK** rather than per-sample white, so the clock drifts
instead of fizzing — the seasick failing converter) · `Track & Hold` (the held value **droops toward zero**
with a ~2 ms time constant during the hold — a real analogue S&H capacitor leaking: every step becomes a
tiny ramp, adding even harmonics and a softer top. Audibly analogue rather than digital) · `Peak Hold` (the
clock is **signal-triggered** — latch on the input's rising zero crossings instead of the free-running
accumulator, so **the staircase locks to the MATERIAL's pitch**. The direct fix for the "same ringing pitch
on all notes" problem, bringing Serum's oscillator-level advantage to an FX bus) · `Shatter` (two fully
independent clocks and RNG seeds, plus a random polarity flip per hold once Jitter passes 50 % — a mono
source becomes a stereo field of unrelated hash).

#### Bitcrush

Uniform scalar quantisation of the amplitude axis. 🔑 **Two quantiser geometries, and the difference is
NOT cosmetic:** a **MID-TREAD** quantiser has a reconstruction level exactly at zero (so it has a dead zone
and produces **true digital silence / dropouts** on low-level samples), while a **MID-RISER** has a
classification *threshold* at zero and no zero output level (so at 1 bit it produces a **solid square with
no dropouts**). D16's Decimort 2 exposes exactly this as its DC Shift switch and calls the two *"drastically
different dynamics response."* **Serum spends a whole mode slot on the mid-riser 1-bit case ("Zero-Square");
we get it as one dropdown click.**

Note: **Serum 2's Distortion has NO bit-reduction mode at all** in its 18. This mode is where we beat the
bar rather than match it.

```
bits = 0.5f + 15.5f·powf(1 − k, 2.6f)     // block rate; 16.0 / 11.4 / 6.9 / 3.4 / 1.0 / 0.5
D    = exp2f(1 − bits)                     // step size, referred to ±1.0 FULL SCALE — ABSOLUTE
d    = dither > 0 ? dither·0.5·D·(rand11() + rand11()) : 0     // TPDF: span ±D = 2 LSB p-p
u    = x·driveGain + d                     // 🔑 Drive is PRE-gain; the step stays absolute
MID-TREAD: y = D·roundf(u/D)               // dead zone at 0 ⇒ dropouts, sputter
MID-RISER: y = D·(floorf(u/D) + 0.5f)      // threshold at 0 ⇒ no dropout, pure square at 1 bit
y = clamp(y, −2.0f, +2.0f)                 // ⚠️ ±2.0, NOT ±1.0 — sub-bit codes must survive

Companded Characters (ITU-T G.711):
  µ-law, µ=255: c = sgn(u)·log1p(µ|u|)/log1p(µ) ; q = D·round(c/D) ; y = sgn(q)·((1+µ)^|q| − 1)/µ
  A-law, A=87.6: |u| < 1/A → c = A|u|/(1 + lnA) ; else c = (1 + ln(A|u|))/(1 + lnA)
  Steps bunch near zero: quiet detail survives, loud content goes blocky. The telephone curve.

Noise-shaped Character (2nd-order error feedback), H(z) = (1 − z⁻¹)²:
  v = u − 2·e1 + e2 ;  y = D·round(v/D) ;  e2 = e1 ;  e1 = y − v
```

**Distinct from Downsample:** the time axis is untouched — feed a slow ramp and Bitcrush produces a clean
staircase in **amplitude** while Downsample produces one in **time**. Audibly, Bitcrush's artefacts are
level-dependent, so what you hear is **the DECAY of every note being eaten** (a crushed pad's tail turns to
gravel and then, undithered, **sticks on a single step and buzzes as a stationary idle tone**). Against
Overflow: thousands of tiny discontinuities distributed over the whole waveform, versus **one enormous
discontinuity at a single threshold** — so Overflow tears only the peaks and is playable dynamically.

**Dither is a musician-legible control, not a spec item:** TPDF makes the first *and* second moments of the
error independent of the input (Lipshitz/Vanderkooy/Wannamaker), so a decaying tail **grains out** instead
of buzzing — at a cost of **+4.77 dB** of floor. It is also **a partial anti-aliaser**, converting folded
harmonic energy into a flat noise floor. Say that in the manual.

⚠️ **THE DRIVE INVERSION — the trap that would silently make Drive backwards.** If the quantiser step is
defined **relative** to the signal (or you normalise before quantising, as `SynthVoice::fShape` case 4
does), turning Drive **up** makes the signal span **more codes**, i.e. **CLEANER** — the exact opposite of
what a Drive knob must do, and it will read as "Drive does nothing / Drive cleans it up." **The step `D` is
ABSOLUTE, referred to ±1.0 full scale; Drive is pre-gain into it; the clamp sits after.** Then Drive up =
the signal exceeds full scale = digital clipping stacked on the crush, and Drive down = the signal falls
into the bottom codes = gravel. **Both directions destroy.**
⚠️ Second trap: a linear bits taper (16 → 8 bits is inaudible on almost anything). ⚠️ Third: Dither
defaulting ON — at 0 it must call **no RNG at all** and be bit-exactly the raw stepping, or the mode's
signature (the buzzy stuck idle tone on a decaying tail) never appears.

**Characters:** `Linear` (default — plain mid-tread uniform PCM, no dither, no companding: the raw
reference) · `Zero-Square` (**mid-riser geometry** — threshold at zero, no reconstruction level at zero, so
at 1 bit it is a clean ±D/2 square with **no dropouts**: solid fuzz where Linear sputters) · `Sputter`
(mid-tread with the dead zone widened 2.5× — far more samples snap to digital zero: gated, dropout-heavy,
aggressively rhythmic crush on decays; the "broken sample player") · `Telephone` (µ-law, µ = 255 —
combine with `Clean` and back `Rate` at 8 kHz and it **is** the telephone) · `A-Law` (A = 87.6, a linear
segment near zero then a logarithmic one — brighter and harder than µ-law, more low-level resolution
preserved. A distinctly different voice, not a re-skin) · `Shaped` (2nd-order error-feedback noise shaping
— quantisation error pushed into the top octave, so at 4 bits it sounds like a **clean signal with tape
hiss over it** rather than grit. **The one Character you can put on a full mix**) · `12-Bit` (SP-1200 /
MPC60: quantiser near 12 bits with a companded input stage and a mild post image filter, back `Rate` biased
toward 26 kHz — the crush lives in the **dynamics** rather than the buzz; what people actually mean by
"sampler grit") · `Overs` (**the top code WRAPS instead of clamping** — real converter overflow. Drive past
0 dBFS and the peaks fold to the bottom of the range: the most violent voicing here and the deliberate
bridge to Overflow).

#### Overflow

**Fixed-point integer overflow — the one destruction axis nobody in this class of synth ships.** Past full
scale a real machine does not clip: the accumulator wraps modulo 2^N and the waveform **teleports** from
+FS to −FS within one sample. Memoryless, but **NOT a waveshaper in the Family B sense**: a fold *reflects*
(C0-continuous, `1/n²`, rescuable with BLAMP + 8×) while a wrap is a **step discontinuity** (`1/n`,
unbounded, **unrescuable**). Real-world referents: the DAW meter's "over", 8-bit tracker overflow, and the
bit-mangling of SoundHack ++bitcrusher.

```
t = powf(0.05f, k)                        // threshold 1.0 → 0.05, EXPONENTIAL (log taper on the knob)
u = x·driveGain/t
y = t·( u − 2·floorf(0.5f·u + 0.5f) )     // sawtooth wrap into [−t, +t)

TWO'S COMPLEMENT Character (true int16 wrap incl. the asymmetric −32768 code):
  i = (int32) lrintf(x·driveGain·32768) ;  y = (float)(int16)(i & 0xFFFF)·(1/32768)
CORRUPT / ROTATE Characters (integer bit mangling — chaotic rather than modulo):
  s ^= mask                                          // Corrupt: mask depth rides Drive
  s  = (int16)((uint16)s << rot | (uint16)s >> (16 − rot))   // Rotate: rot = 1..4

⚠️ MANDATORY DC BLOCKER, 10–20 Hz one-pole, AFTER the wrap. VintageReverb.h:46/:287 verbatim:
   o = y − dcx + dcR·dcy ;  dcx = y ;  dcy = flush(o) ;   dcR = 1 − 126/fs   (≈ 20 Hz)
```

**Distinct from the FOLD family:** a folder **reflects**, so amplitude becomes timbre smoothly and
musically (the West-Coast sound); a wrapper **teleports**, so amplitude becomes a **tear**. At matched
drive a folder sounds like a bright chorus of added partials; a wrapper sounds like the signal is being
**ripped**. Measured: **~+12 dB HF-ratio vs Linear Fold and ~+25 dB vs Sine Fold** at equal fold count, and
the alias floor ~12 dB higher at the same oversampling. Its dynamic behaviour differs too — a folder's
output amplitude is smooth in the input amplitude, whereas Overflow's **flips polarity discontinuously**,
so tiny input changes near a threshold produce full-scale output changes. **That is the "gnarly" quality
and there is no way to get it from a mirror.**

⚠️ **THIS IS WHERE THE THREE-WAY "WRAP" COLLISION WAS RESOLVED** (see §12). Two agents independently
proposed a Wrap *mode* (one in FOLD, one in DIGITAL) and a third proposed a Wrap *pill* in CLIP. The
deciding argument is the FOLD agent's own criterion, turned around: **a wrap cannot be band-limited without
becoming a fold, so it belongs in the family whose baseline is 1×.** It ships here as `Overflow` (the
precise name for what an accumulator does, and no-doubles-safe against CLIP's `Wrap` pill); **FOLD ships
three modes, not four.**

⚠️ **This mode has the OPPOSITE problem to the rest of the device: it is dramatic instantly, and the risk
is that it is dramatic ONLY at the bottom of the knob.** The first wrap arrives at 25 % of travel and is
already a total transformation; wraps 20–32 are perceptually much closer together than wraps 1–4, because
the ear resolves "a hard edge appeared" far better than "the hard edges got denser." **Log taper** (the
opposite of everything else), and make **Rebound-style behaviour do real work in the top half** — at high
wrap counts a descending step-height staircase or an overshooting one is audible even when the wrap count
is no longer distinguishable.
⚠️ **Auto must NOT peak-match this mode** — the peak is pinned at ±t from the first wrap onward, so a
peak-matched Auto does literally nothing across 90 % of the travel. Loudness match, and expect it to pull
down 6–9 dB at the top because the RMS genuinely rises.
⚠️ **Ship a post-decimation true-peak clamp** and verify with a 0 dBFS sine measured at 4×: this is the
mode where a careless user discovers an inter-sample peak, and its **derivative is unbounded** even though
its output is not.

**Characters:** `Overflow` (default — plain modulo sawtooth wrap, symmetric: the reference) ·
`Two's Complement` (true int16 wrap including the asymmetric −32768 code, so the down-wrap holds one extra
sample of full negative — nastier, slightly asymmetric, leans on the DC blocker) · `Bounce` (wraps on the
positive side, **clips** on the negative — heavily asymmetric, strong even-harmonic bed under the tearing;
the only voicing here that reads as "warm" at low thresholds) · `Mirror` (the wrap period **halves on
alternate input cycles**, so a sub-octave appears underneath the destruction — turns a lead into a monster
with no pitch shifter) · `Corrupt (XOR)` (integer XOR with a bit mask instead of modulo: chaotic sign flips
and random magnitude jumps — the damaged-file texture; mask depth rides Drive so the knob still means
something) · `Rotate` (bit-rotate the sample word 1–4 places so MSBs become LSBs — **loud content collapses
to quiet noise and quiet content explodes to full scale**: a total inversion of the dynamic map, unlike
anything else in the device) · `Soft Wrap` (the wrap edge replaced by a 1–2 sample raised-cosine ramp —
same gesture, roughly half the fizz; **the one Character that survives on a bus**) · `Melt` (Feedback
pre-loaded to 0.6 with the threshold itself modulated by the wrapped output — self-oscillating chaos that
still tracks the input's envelope. Bounded by `softClip`, unbounded in attitude).

---

## 10. Dramaticism — the traps, collected

100 % of any param must be **completely dramatic** — night and day, sound matching the number. The
specific traps for this device, in the order they will bite:

1. **Level ≠ distortion.** Every judgement level-matched (§4.2), or every param feels dramatic and none
   of them are. But **ship `Auto` OFF** — see §2.6.
2. **No dead first third.** Six mechanisms produce one, and each has a specific fix (§2.5): a linear-in-gain
   Drive taper · a fixed clip threshold · a floor-function fold count · a `p = 1.0` pedestal · a
   `amount²` pre-gain · a clip threshold at 0 dBFS instead of −6.
3. **Modes must be distinguishable blind.** The even/odd ratio, crest factor and level-dependence metrics
   are the objective proof. **23 modes that all sound like tanh-with-a-hat is failure.** The specific
   pairs to guard: Stomp Box vs Overdrive (crest 2.5 vs 1.4) · Diode 1 vs Diode 2 (crest falls vs rises)
   · Diode 1 vs Asym (even/odd constant vs collapsing through a decay) · Soft Clip vs Hard Clip (defaults
   65 vs 8) · Overflow vs Linear Fold (+12 dB HF-ratio) · Rectify vs Fold (fixed single fold at zero vs
   amplitude-dependent repeated folds at a threshold).
4. **Cut what you can't hear.** Four modes were cut on that basis (§5.2) and one more is flagged as the
   next candidate (`Table`, §9.5).
5. **The reference is sometimes the timid version.** Buchla's 5 folds, the TS's 551 kΩ, tape's
   self-limiting, an op-amp's real slew rate, `a = 1` on a rectifier, ±0.3 of bias. **Anchor the TOP of a
   range in reality and let the rest go absurd.**
6. **Fidelity is the enemy exactly once** — West Coast (§9.4). Everywhere else physics *is* the drama.
7. **Do not judge at polite settings.** §3.10.
8. **THD is the wrong metric for wavefolders** (§8.1) and **sample-diff is banned everywhere** (fb283).

---

## 11. Hard-rule compliance checklist

| Rule | How this device satisfies it |
|---|---|
| **No playing safe** | §2 — a per-mode extremity table, +48 dB drive law calibrated to the measured −26 dBFS bus, ceilings pushed past Buchla / TS-808 / real tape / real op-amps, and **every clamp in the document annotated as BIBO-or-taste** |
| Filter before effects | Inherited — per-osc sends already tap post-filter (fb287) |
| Power gates routing | `SYN_DST_POWER`, default **OFF**, zeroes gains, disables pills, **and engages no latency-compensation delay** (§4.4) so the default sound stays byte-identical |
| Mod is a matched pair | Only ANALOG modulates, and it ships **both** `Drift` and `Drift Rate` as shared back-panel slots — structurally impossible to ship one alone |
| Type-unique controls + pills | Six family-keyed back-8 sets, **184 Character voicings**, six family-unique 2nd pills. `Auto` universal |
| No noise unless natural/controlled | `Dither`, `Jitter`, Diode 1's `Leaky` shot noise (gated by instantaneous conduction current — **silent at rest by construction**), Diode 2's `Sputter` (modulates the **gap**, never the level), West Coast's `Cold Solder` (a **fixed** per-instance offset, not noise). All zero at 0 |
| Params play their roles | Low Cut **pre**, Hi Cut **post**, Emphasis the **pair** — three distinct jobs. `Stages` (ladder length) vs Drive (how far up it) vs `Spacing` (ladder geometry) are three separate lanes, which is why the front knob is `Symmetry` and not `Folds`. `Slew` is level-dependent where a filter is not. Per-point X modulation is **deliberately unadvertised** because it duplicates Drive's lane |
| No clicks / crackle | §4.6 — per-sample glide, **ADAA state park-and-reseed**, the per-sample-smoothing cache trap, 40 ms table crossfades, type-switch wet dip, solver guards that **fade rather than zero**, denormal flush, DC blockers with primed state |
| Mix 100 % = fully wet | Send padded by `kVoiceToFxPad` (fb292); **plus the Rectify-specific rule** that a mode must never use the device Mix as its own wet/dry (§5.3) |
| Dramaticism | §10, gated by §8 |
| Perceptual harness | §8 — phase-independent only, sample-diff banned, `SNR_A` defined exactly, per-family gates, **and the chord test** |
| CPU-friendly | §3.7 measured per-mode budget instead of 8× everywhere; ADAA-1 + 2× as the house default; state ODEs at base rate; **band-limit the nonlinearity before oversampling the signal** (Table's mip retreat); `N = 2` on half of Overdrive's Characters; the CLIP family deliberately under-spent so FOLD and Tape can afford their tiers |
| Recycle existing | Appendix A — the ADAA harness, the Snarl term, the Auto tracker, the drive glide, the Drift pair, the ZOH decimator, TPDF dither, the polyBLEP polynomial, the shaper editor, the RDP simplifier, the double-buffer swap, `softClip`, `fastTanh`, `lncosh`, `SVFBandpass`, `CassetteMachine`'s wow stack. **Three of v1's recycle claims were wrong and are corrected there** |
| Everything audible interacts visually | §5.8 — the live transfer curve with a **24-bin signal-occupancy histogram** driving per-segment glow, not a peak dot (fb311) |
| Fixed positions, no reflow | Surgical attribute updates via `shRefreshOne`; a Character or Type change never re-renders the panel |
| State persists | Every pill/dropdown writes back to the JS model **and** reads back from params on init |
| Menus never cut off | Type/Character/Quality reuse the `.pmenu` clamp |
| Pragmatic names | Drive · Bias · Knee · Asym · Symmetry · Morph · Crush · Sag · Gap · Punch · Snarl · Beyond · Squash — what it does, no jargon |
| Dropdowns not click-to-rotate | Type (with optgroups), Character and Quality are real `<select>` menus |
| Build both formats + cache-bust | §7 |

---

## 12. Open questions for Max

**Answered by the research — no longer open:**

* ~~Merge `Crush` + `Downsample`?~~ **No.** Measurable discriminator: THD vs input level is
  ~6 dB/bit for Bitcrush and **flat** for Downsample. Vital ships them separately and is right. The
  cross-axis lives on back knob 3 so you can still stack them in one instance, and the freed slot went to
  **Overflow** (§9.6).
* ~~Is `Console` worth a slot?~~ **Cut.** No mechanism separates it from a gently-driven Tube, Stomp Box
  or Soft Clip, and its back-8 was a subset of ANALOG's (§5.2).
* ~~Latency policy.~~ **Settled: fixed 8 samples, every mode, every tier, compensated internally,
  reported as 0** (§4.4). Also uncovered: the actual breakage is the Mix comb and the *downstream*
  send exclusion, not the local dry-removal term — and **a third device silently breaks fb305 at two
  exact lines** (§4.5).

**Still needs your call:**

1. **`Slew Clip` — keep it in CLIP, or move it?** It is the one member of a memoryless family that has
   state (one variable), and its slew-limiting mechanism overlaps with Overdrive's LM308 modelling.
   Discriminators: Overdrive squares (crest 1.4) and does three other things; Slew Clip only limits slope,
   goes ~3 orders of magnitude past any real op-amp, and gets *quieter* at max. **My lean: keep it in
   CLIP.** 4 ops, 0.03 % of a core, zero latency, and it is the only mode in the device that distorts a
   hi-hat while ignoring a sub at the same gain.
2. **`Table` — is 4 modes right for SHAPER?** Its DSP core is identical to Shaper Asym; what saves it is
   that Morph sweeps 256 transfer functions in one gesture, which nothing else reaches. **My lean: build
   it, but it is the first cut in the family** if the roster needs a slot.
3. **The `Wrap` name.** Resolved as: DIGITAL type = **`Overflow`**, CLIP pill = **`Wrap`**, FOLD ships
   three modes. **Confirm you are happy with `Overflow` as a Type name** — it is precise (it is what an
   accumulator does) but it is less immediately obvious than "Wrap" to someone browsing the list.
4. **Multi-instance rack.** Should `+ Add effect` be able to add a **second** Distortion (Tube into Fold)?
   Real work — engines are single-instance today — but it is the one thing the two-device split would
   have bought us, and it is now the *only* remaining argument for it. Worth its own pass after the
   device ships.
5. **Multiband?** Serum offers a low/high split so bass stays clean while highs shred. *My lean: not in
   v1 — it doubles the param surface, and `Emphasis` + `Low Cut` + `Width` already give most of the
   benefit with one knob each.* Revisit after the 23 modes land.
6. **`Auto` default OFF — confirm.** This is the single change most likely to be argued with, because it
   makes every A/B harder to judge. The research is unanimous that Serum, Vital and Decapitator all ship
   without full compensation and that full normalisation is what makes a distortion feel timid. **My
   lean: OFF, at 70 % when engaged, with a slow tracker.**
7. **`Bias Walk`, `Squeeze`, `Timbre Sweep`, `Growl`, `Ratchet`, `Sag`, `Bounce` are stateful
   Characters inside memoryless families.** Each is flagged in place. **Confirm you want the exception**
   — they are among the most alive voicings in the device, but they mean "memoryless family" is a
   statement about the *default* voicing, not an invariant.
8. **The oscillator WARP/FOLD arc (Appendix B).** Separate work, roughly 14 small edits with measured
   before/after numbers. **When do you want it?** It is independent of this device except that both want
   `Source/Shapers.h` extracted, and doing that extraction first makes this device's Fold/Rectify/Crush
   types nearly free.

---

## Appendix A — Recycle inventory (verified by reading, not assumed)

**This device is ~60 % already written.** Read these before writing a line.

### The ADAA harness — production-grade, already shipped
* **`TerrainFilters.h` `struct WaveShaper` (~line 990)** — 1st-order ADAA with **both** required fallback
  branches already correct (`|dx| < 1e-5` → midpoint; `|dx| > 0.9` → direct, so a big jump does not
  dropout), a numerically-safe `logcosh`, a TPT one-pole post-LP and a DC blocker. **LIFT THIS WHOLESALE**
  as `DistortionEngine`'s ADAA core; swap `f()`/`F1()` for the per-mode LUT reads.
* **`SynthVoice.h:4556-4633`** — `FoldState` + `applyFoldADAA` + `foldGtri`/`foldFlin`/`foldAntideriv`.
  The **cached-F1 variant** with correct invalidation on the fallback branches (`st.sh1 = -1`), the
  note-on reset discipline, and the offline ADAA audit recorded in the header comment.
  ⚠️ **See §4.6 trap 2 — the cache assumption does not survive per-sample smoothing.**
* **`SubOsc.h:63-82` `heat()`** — the house ADAA-1 pattern including the smoothstep wet fade (so 0 is
  bit-transparent with no kink at the bypass gate) and — critically — **state seeded with the FIRST input
  sample, never 0** (memory: `terrain-dsp-adaa-history-seed-gotcha`).

### Auto, Snarl, and the drive glide — already written, one constant to change
* **`HarmonicEngine.h::postProcess` (~lines 306-360, the FORGE stage)** contains, in one validated
  function: a **30 ms per-sample drive glide** (guarantees an LFO square or hard knob jump cannot burst),
  an asymmetric biased-tanh through 1st-order ADAA, a **one-sample feedback term (`fb = 0.55·dd`) which
  IS the Snarl knob**, a DC blocker, a **continuous per-sample RMS auto-gain (10 ms tracker, makeup
  clamped 0.02…8×) which IS the Auto pill**, and a **clean-bypass branch that parks the ADAA state**.
  **Take all of it. ONE CHANGE: slow the tracker from 10 ms to ~300 ms** (§4.2) and raise the makeup cap.

### The Drift / Drift Rate pair — already written
* **`TapeMachines.h` `class TapeMachineBase` (lines 37-317)** — `SmoothRandom` (band-limited random with
  `prepare(rateHz, smoothFreq, sr, rng)`) driving `DelayLine::readCubic`: **Drift + Drift Rate complete**,
  including the cubic-interpolated fractional read. Also directly reusable: `DCBlocker` (10 Hz one-pole —
  **exactly §4.1's mandated spec**, unlike `TerrainFilters`'), `OnePoleLP/HP/AP`, and **`SVFBandpass`
  (Andrew Simper TPT SVF — use verbatim for Transformer's leakage resonance)**.
* **`CassetteMachine`'s triple-LFO wow** (0.6 Hz ±2.0 ms, 2.2 Hz ±0.8 ms, 7 Hz ±0.4 ms) is Tape's
  `Cassette` Character **verbatim**. `StudioMachine::saturateSample` (~line 418) is the
  pre-emphasis → bias → shape → DC-block → post-rolloff → gain-comp chain, i.e. the exact **shape** of
  our shared shell.

### The house FX-engine contract — copy the shape or it will not drop into the rack
* **`DelayEngine.h`** — `prepare(double)`, clamped `setType/setCharacter/setX(...)`,
  `processSample(float inL, float inR, float& outL, float& outR)` (line 139), the per-sample
  target→current glide idiom (lines 162-171), **`softClip` (line 315: linear below ±1.4 then tanh — the
  BIBO bound, reusable as the family's final safety net)**, `onePole(hz)` (line 324), `flush()`.
  `DistortionEngine.h` must mirror this API exactly, including "processSample returns WET only; the
  processor owns Mix."

### Family C — essentially finished
* **`VintageReverb.h`** — the **ZOH decimator** (line 213: `srPhI += srRateC; if (srPhI >= 1) { srPhI -= 1;
  heldInL = x; } x = heldInL;`, including the fractional-rate property an integer counter would lose),
  the **reconstruction LP** (line 126: `recCoefT = exp(−2.827·srRateT)`, `fc ≈ 0.45·fs_r` — the arithmetic
  in the comment is exact), **`crush()`** (line 305, mid-tread, and its `levels = 2^bits·0.5` convention
  gives `D = 2^(1−bits)` — **exactly our formula, no maths change needed**), **`crushDither()`** (line 313,
  TPDF spanning ±D = 2 LSB — textbook Lipshitz/Vanderkooy, correct as written), **`rand11()`** (line 323,
  xorshift), the **10–20 Hz DC blocker** (lines 46/287), **M/S width** (line 285), the per-sample glide
  idiom (lines 162-171), `flush()` (line 295), and **`struct CharBias` + `static constexpr CharBias
  CHAR[8]`** (lines 337-339) — the house pattern for 8 Character voicings as a coefficient table. **Copy
  that pattern exactly rather than inventing a new one.**
  ⚠️ **Two edits:** widen `crush`'s `±1.5f` clamp to **±2.0f** (or sub-1-bit codes are amputated) and drop
  the `if (bits > 15.8f) return x` early-out. ⚠️ **`srRateT = 1 − 0.90·ageEff` is a RATIO of fs** and
  bottoms at ~4.8 kHz — **do not inherit that convention** (§2.5) and map exponentially in **Hz** to 20 Hz.

### The curve editor — ~85 % built
* **`index.html:23569-24107`** — the whole fb206 SHAPER module: `shBias` (`:23584`), `shEvalPts`,
  `shPathD` (segment-exact SVG), `shRefreshOne` (surgical updates), `shPos`/`shSnap`/`shNear` (the "Serum
  grab", `bd = 169` ⇒ 13 units), the drag handler (3 px threshold, tension at 0.012/px with a 0.06
  detent, **ties allowed so two stacked points = a true vertical step**), dblclick add/remove,
  `shBand`/`shGrpBounds`/`shDelSel`, `shFlipH` (**exactly what the `Sym` pill needs on the UI side**),
  `seedFor`/`seedTo`, `shGet/shPush/shPull` with the 90/130 ms debounce and the "local edit outranks
  pull" race guard, `GRIDS = [2,3,4,6,8,12,16,24,32]` snap, the fb238 per-point-mod chain
  (`shPtMenu`/`shModSrcMenu`/`shModAmtNode`/`shEffPts`/`shGhostTick`), the fb258 `shRedRow` geometry, and
  **`rdpSimplify` (`:24080`) + `wtToLfo` (`:24089`) — the working precedent for Send To Shaper**.
* **`PluginProcessor.cpp`** — `lfoShapeBias` (`:8046`), `bakeLfoShapeTable` (`:8078`, **already handles
  zero-width segments as hard steps — our jump-discontinuity support, free**), `setSynthLfoShapes`
  (`:8094`), the **shared→audio double-buffer with `ScopedTryLock` + version counter** (`:5578-5591`),
  and the fb238 live per-point-mod re-bake gated on >0.002 movement (`:5597-5628`).
* **`PluginProcessor.h:1059` `struct LfoShapePtM { float x, y, c; int xs; float xa; int ys; float ya; }`** —
  per-point modulation already exists. Reuse the struct and modulatable curve breakpoints are free.
* **`SubOsc.h:86-92` `pblep()`** — the 2-point polyBLEP residual (`u+u−u·u−1` / `u·u+u+u+1`). **Zero-Square
  and the `Wrap` pill both need exactly this**; it needs re-parameterising from phase form to crossing-time
  form, but the polynomial is identical.

### ⚠️ FIVE CORRECTIONS TO v1's RECYCLE LIST — verified by grep, not assumed

1. **There is NO Jiles–Atherton anywhere in the tree.** Grepped `jiles|hysteresis|langevin|coth` across all
   of `Source/`; the only hits are FlowChop's gate hysteresis and a scope-tail bool. **`TapeMachines.h`'s
   "saturation" is `tanh` with a bias term and pre/de-emphasis.** v1's "TapeMachines.h already exists, read
   it before writing a line" is **right about the scaffolding and wrong about the core** — the hysteresis
   ODE is genuinely new code; everything around it is free.
2. **`juce::dsp::Oversampling` is used NOWHERE.** The only "Oversampling" hits are
   `TerrainFilters::needsOversampling()` (a bool flag) and an ad-hoc scheme that doubles the coefficient
   sample rate. `HarmonicSculptor::processSample` (lines 95-105) has a shipped 2× "half-sample midpoint"
   cheap oversampler and `TapeMachineBase` has `prevOversampleInput` for the same idiom. **A real polyphase
   halfband oversampler is NEW CODE** — but `juce_dsp` is already linked, so use
   `juce::dsp::Oversampling<float>` rather than hand-rolling.
3. **There is no `SYN_DST_*` or `SYN_SAT_*` namespace.** `ParameterIDs.hpp`'s only SATURATION constants
   belong to the legacy Tape/Wire machines (lines 18, 23) and the Geode drive-mode enums (878-935). **The
   whole ~24-param block is greenfield**, so the 4-point WebSliderRelay trap applies in full.
4. **`setLatencySamples` is called nowhere**, and `ConvolutionReverb::getLatency()` has **zero callers**
   (§4.4).
5. **`TerrainFilters.h`'s `DCBlocker` is sample-rate dependent** and there are **three copies** of the same
   0.995 constant (§4.1). Consolidate, do not add a fourth.

### Also flagged
* `ModalEngine.h:299` `softClip` — the other house cheap soft clip; check it before adding a third.
* **`IndyFxChain.h`** — the per-chop private chain owns its own copy of every global FX module. **Add
  Distortion there too** or it silently vanishes on the chop path.
* `Design/fx-rack-v7-CANONICAL.html` + `Design/fx-back-panel-mockup.html` — the frozen chassis. Do not
  re-author. Reuse `TIC.presets` / the `.pmenu` glass (bullet `•`, not a check).

---

## Appendix B — OSCILLATOR WARP / FOLD punch-list  *(SEPARATE ARC — not part of this device)*

> ⚠️ **This is a different piece of work.** It shares `Source/Shapers.h` and the anti-aliasing findings
> with the Distortion device, but the oscillator warp/fold lives **inside the per-unison-sine loop** (up to
> 16 unison × 4 osc × polyphony ≈ 1024 concurrent instances), so its CPU budget is roughly **two orders of
> magnitude tighter** than a single stereo insert. **Do not conflate the two documents** — the
> "4×-oversampled fold" recommendation below is safe only at the amount gate specified; a blanket 4× on
> the whole voice is not.

**The diagnosis, measured.** An offline phase-independent harness (band-limited saw and square at 110 Hz /
48 kHz, exact harmonic bins, DC removed, spectral centroid + HF>2 kHz ratio + non-harmonic energy) run
against the **exact ported math** from `SynthVoice.h`. **Of eleven warp modes, three do their job and six
are effectively inert or backwards.** PWM moves the HF ratio by **−1.7 %** on a saw (a functionally dead
knob). MIRROR moves it 12 % across its entire travel. RECTIFY and SINE SHAPER get **duller** as you turn
them up. Our Serge wavefolder at 100 % is **30 % brighter than no wavefolder at all**, and its
`amount²` taper makes the first third of the knob *darker* before it ever gets brighter.

### B.1 The two literal dead-code floors — the cleanest proof of the pattern

The author wrote the extreme limit and then picked a coefficient that cannot reach it:

| File:line | Current | Problem | Fix |
|---|---|---|---|
| `SynthVoice.h:916` | `duty = jmax(0.10, 1.0 − amount·0.45)` | At `amount = 1.0` this is **0.55**, so the written `jmax(0.10, …)` floor is **unreachable dead code**. A PWM that never goes below 55 % duty is barely a PWM | `amount·0.90` |
| `SynthVoice.h:922` | `knee = jmax(0.05, 0.5 − amount·0.4)` | At `amount = 1.0` this is **0.10**; the 0.05 floor is likewise **unreachable** | `amount·0.45` |

Two-character edits. Measured PWM result: square centroid **289 → 407 Hz** becomes **289 → 5825 Hz**;
HF **0.0203 → 0.0292** becomes **0.0203 → 0.9234**. **A 32× improvement in HF travel from one number.**

### B.2 The full punch-list, with measured before → after

| # | File:line | Current | → Proposed | Measured effect (centroid Hz / HF ratio, full travel) |
|---|---|---|---|---|
| 1 | `SynthVoice.h:2296-2301` | `warpRateMul` covers only modes 2, 3, 7 | Extend to modes **1, 4, 5, 6** (BEND `1+π·amt` · PWM `1/duty` · SKEW `0.5/knee` · MIRROR `1+7·amt`). **Leave mode 8 (P-QUANTIZE) at 1.0** — its aliasing IS the product | **Safety, no audible change today. DO THIS FIRST, in its own commit** |
| 2 | — | `applyFold`/`applyAmpWarp` are private members of `TerrainSynthVoice` | **Extract `Source/Shapers.h`, `namespace tw::shapers`, verbatim**, with one-line forwarding wrappers left in SynthVoice | **No audible change. Unblocks the Distortion device's Fold / Rectify / Crush types** |
| 3 | `SynthVoice.h:916` | `amount·0.45` | `amount·0.90` | 289→407 / 0.0203→0.0292 **⇒ 289→5825 / 0.0203→0.9234** |
| 4 | `:4526` + **`:4585`** | `pre = 1 + amount²·9` (max 10 ⇒ **10 folds**) | `pre = pow(2, amount·6)` (1…64) | 348→1082 / 0.0236→0.0307 **⇒ 348→5521 / 0.0236→0.9955**; Vital = 32 folds |
| 5 | `:4534` + **`:4580`** | `pre = 1 + amount²·5.28318530` (max 2π ⇒ **2 folds**; **first 30 % = ZERO folds**) | `pre = pow(2, amount·6)` (1…64) | Vital's `sinFold` = 32 folds. **A factor of eight** |
| 6 | `:4540` + **`:4581`** | `pre = 1 + amount²·5` (max 6 ⇒ 16 folds) | `pre = pow(2, amount·5)` (1…32) | Buchla 259 uses **five** parallel cells; ours manages ~two |
| 7 | `:965` | `drive = 1 + amount·4` (**4 folds**, and **no anti-aliasing at all** on this path) | `drive = pow(2, amount·6)` (1…64) → route through `applyFoldADAA` shape 1 | 293→432 / 0.0188→0.0135 **⇒ 293→5421 / 0.0188→0.9959** |
| 8 | `:922` | `amount·0.4` | `amount·0.45` (floor becomes live) | 289→643 / 0.0203→0.0500 **⇒ 289→1724 / 0.0203→0.2299** |
| 9 | `:929` | MIRROR linear crossfade | **Mirror-REPEAT count:** `reps = 1 + amount·7; w = fabs(fmod(p·reps, 2) − 1)` | 289→323 / 0.0203→0.0226 (**dead**) **⇒ 289→1047 / 0.0203→0.0993**. *(The obvious alternative — moving the mirror pivot Serum-style — measured completely flat. Reject it.)* |
| 10 | `:948` | `steps = round(pow(2, 5 − 4·amount))` = 32…2 — **the knob STARTS inside the effect and walks out of it** | `pow(2, 9 − 8·amount)` = 512…2 | Buzz zone is 128…16 steps (peak HF 0.0333 at 32–64), which the current range mostly sits *below*. Vital's equivalent is transparent→2 |
| 11 | `:899` | BEND `p + amount·0.5·sin(2πp)` — **goes non-monotonic above `amount = 1/π = 0.318`** (an undeclared fold) and its travel **plateaus then REVERSES** (289→551 at 75 % →550 at 100 %) | Monotonic bipolar power bend: `pow(p, k)`, `k = pow(2, amount·6)`; twin `1 − pow(1−p, k)` for Bend− | Serum ships Bend +, − and +/− as three modes precisely because direction matters |
| 12 | `:960` | RECTIFY dry/wet blend — **only ever gets quieter and duller** (HF 0.0296→0.0126) | Sweep a **rectifier BIAS**: `b = (1−amount)·1.2; y = fabs(s + b) − b`, through the existing `wtRectDcAL_` blocker + `1/(0.5+0.5·amount)` makeup | Exact identity at 0, continuous through half-wave, `\|s\|` at 1. *(Naive "add pre-gain" variants measured flat — the rectified signal saturates.)* |
| 13 | `:904`/`:909`/`:934` | SYNC/FORMANT `pow(2, amount·4)` = 16× · FRACTALIZE `1 + amount·7` = 8× | `pow(2, amount·5)` = 32× · `1 + amount·31` = 32× | ⚠️ **Ship ONLY with a verified extension of the mip pyramid** (`:2296-2302` `warpRateMul` feeds `mipLevelForPhaseIncrement`) — doubling the sync multiplier doubles the effective read rate, and running off the top of the pyramid produces broadband aliasing that **sounds like a bug, not like extremity** |
| 14 | `:652`, `:838`, `:846`, `:1555`, `:1556`, `:1561` | `warpAmountBase_ = jlimit(0.0f, 1.0f, amount)` — **UNIPOLAR** | **Bipolar −1…+1** | Serum's Warp controls are bipolar. **Half the design space is simply absent, for no stability reason** — a taste clamp, which the rule bans |
| 15 | `:2349-2350` | FM `pow(fmD1Sm_, 1.7f)·2.0f` = max **2.0 cycles = 12.57 rad** | `pow(fmD1Sm_, 1.5f)·6.0f` = **6.0 cycles = 37.70 rad** | Vital: `kFmPhaseMult = kPhaseMult/8`, `kMaxFmModulation = 48` ⇒ **exactly 6 cycles**. Measured ratio 1: 819 Hz / HF 0.000 ⇒ **2509 Hz / HF 0.618**; ratio 4: 3520 ⇒ **10560 Hz**. **Exactly 3× too shallow.** Companions: raise the `fmRateMul` cap (`:2424`) **64 → 256** (at 6 cycles × ratio 16 the Carson term is 604) and soften DX key scaling (`:1663`, `:1699`) from `pow(0.5,(note−72)/18)` to `pow(0.5,(note−84)/24)`, which currently **halves the index every 18 semitones above C5** — exactly where FM brightness matters most |
| 16 | `:2633` | Cross-osc PD `pm += (1.20f·d)·mod` = **1.2 cycles** — the shallowest thing in the synth, 40 % below our own FM engine | `pm += (8.0f·d)·mod` | The FM slot on the same matrix already clamps to **±8 cycles** (`:2639`) and is proven stable, so this just gives PD the ceiling FM already has. 534 Hz / HF 0.000 ⇒ **3363 Hz / HF 0.741** |
| 17 | `:2831`, `:2834` | SAMPLE WARP Drive `1 + amount·9` (1…10) · Crush `jmax(4, 64 − amount²·60)` (64…4 levels) | Drive 1…32 · Crush 256…2 levels | **`SynthVoice.h:2812` twenty lines above already uses `drv = 1 + airA·20` with the in-code comment "AMPLIFIED — night-and-day".** We learned this lesson once in the same file and did not propagate it |
| 18 | `:2181-2192` vs `PluginProcessor.cpp:4738-4740` | **WARP 2 is modulated only at BLOCK rate and only GLOBALLY.** The per-voice gather WARP 1 gets never runs for slot 2; `ModDest::Warp2A..D` exist and *are* applied, but via `mdP`/`ownM` reading the shared `modSums` | Give WARP 2 the same per-voice gather WARP 1 has | A per-voice envelope or velocity modulates WARP 1 **polyphonically** and WARP 2 **monophonically** — exactly the two-path law in MEMORY.md. **Fix this BEFORE raising WARP 2's depth**, or polyphonic patches exhibit an obvious "all voices move together" artefact that will read as a new bug |

### B.3 ⚠️ The three traps for this arc specifically

1. **THE ADAA LOCKSTEP TRAP — the highest-probability way to break this build.** `applyFold`
   (`:4517-4553`) and `foldAntideriv` (`:4576-4587`) **duplicate the same `pre` expressions** (`9.0f` at
   4526 **and** 4585; `5.28318530f` at 4534 **and** 4580; `5.0f` at 4540 **and** 4581). Raise the shaper's
   `pre` without raising the antiderivative's **identically** and `F` is no longer the antiderivative of
   `f`: the ADAA quotient stops approximating the band-limited shaper, and **the fold gets louder AND
   aliases WORSE** — which will be misread as "the bigger range sounds bad, back it off" and cause exactly
   the timid retreat this arc exists to prevent. **Grep all six sites before touching any of them.**
2. **Level-match every A/B.** The fold, sine shaper and FM changes all raise perceived loudness *before*
   they raise brightness. Un-matched, "louder" gets scored as "more dramatic" and the genuinely dead modes
   (MIRROR, RECTIFY) slip through again.
3. **A saw is the WRONG probe for BEND, SKEW and MIRROR.** A monotonic phase remap of a saw is still
   approximately a saw, so these three measure nearly flat on a saw regardless of depth. Run them against
   a **square / feature-bearing table**, and **strip DC first** — an un-removed DC term from asymmetric
   warps dragged the first SKEW centroid reading from 1724 Hz down to 107 Hz and would have looked like
   the fix failing.

### B.4 CPU verdict for the deeper fold — and it CONTRADICTS §3.7 for this path

Measured non-harmonic energy, Linear/Serge fold, kaiser-window polyphase decimation:

| | 1× naive | ADAA-1 @1× | ADAA-1 + 2× | ADAA-1 + 4× | ADAA-1 + 8× |
|---|---|---|---|---|---|
| **pre = 10 (today)** | −21.3 dB | −33.8 | −36.6 | −37.2 | −37.5 |
| **pre = 64 (proposed)** | −12.2 dB | −23.7 | −29.2 | **−32.0** | −32.2 |

**4× buys back essentially all of the 10 dB lost by going 6.4× deeper** (within 1.8 dB of today's
cleanliness); **8× is worth only 0.2 dB more and is a pure waste.** ADAA-1 alone is worth 11.5 dB,
consistent with the ~12 dB polyBLAMP figure Esqueda et al. report for the Buchla 259. **Gate the
oversampling by amount: 1× below `pre = 10` (amount ≤ 0.42 on the new taper), 2× to 0.60, 4× above** —
applied to the **fold stage only**, inside the per-unison loop, never to the whole voice.

⚠️ **This is a genuine cross-arc correction:** §3.7 gives Fold an ADAA-1 + 4× floor rising to 8×, which is
right for the *device* (32 folds on an FX bus at extreme drive). For the *oscillator* path at `pre = 64`,
8× is measurably pointless. **The two budgets are different because the instance counts are different.**

---

## Sources

Every URL below was fetched by a research agent or read from the paper. Repo line references were verified
by reading the files.

### Anti-aliasing — ADAA, oversampling, BLEP/BLAMP
* Parker, Zavalishin & Le Bivic — *Reducing the Aliasing of Nonlinear Waveshaping Using Continuous-Time Convolution* (DAFx-16); Bilbao, Esqueda, Parker & Välimäki (IEEE TASLP) — [ADAA overview](https://ryukau.github.io/filter_notes/antiderivative_antialiasing/antiderivative_antialiasing.html) · [Antiderivative Antialiasing for Memoryless Nonlinearities](https://www.researchgate.net/publication/314162638_Antiderivative_Antialiasing_for_Memoryless_Nonlinearities) · [for Stateful Systems](https://www.researchgate.net/publication/338093154_Antiderivative_Antialiasing_for_Stateful_Systems) · [DAFx-20 paper 35 (ADAA in nonlinear WDFs)](https://dafx2020.mdw.ac.at/proceedings/papers/DAFx2020_paper_35.pdf)
* Chowdhury — [Practical Considerations for Antiderivative Anti-Aliasing](https://jatinchowdhury18.medium.com/practical-considerations-for-antiderivative-anti-aliasing-d5847167f510) · [ADAA experiments (GitHub)](https://github.com/jatinchowdhury18/ADAA)
* Werner & Azelborn (Soundtoys / NI) — [*Antialiasing Piecewise Polynomial Waveshapers* (DAFx-23)](https://www.dafx.de/paper-archive/2023/DAFx23_paper_61.pdf)
* Vanoli, Gabrielli & Squartini — [*Simplifying Antiderivative Antialiasing with Lookup Tables* (DAFx-25)](https://dafx25.dii.univpm.it/wp-content/uploads/2025/08/DAFx25_paper_30.pdf)
* Vicanek — [*Note on Alias Suppression in Digital Distortion* (2023/rev. 2024)](https://vicanek.de/articles/AADistortion.pdf)
* Esqueda, Bilbao & Välimäki — [Aliasing reduction in soft-clipping algorithms](https://www.researchgate.net/publication/282978216_Aliasing_reduction_in_soft-clipping_algorithms) · [polyBLAMP clipping study (DAFx-16)](https://dafx16.vutbr.cz/dafxpapers/18-DAFx-16_paper_33-PN.pdf) · [ICA 2016](http://www.ness.music.ed.ac.uk/wp-content/uploads/2016/12/ICA2016-0750.pdf)
* Lehtonen, Pekonen & Välimäki — [*Audibility of aliasing distortion in sawtooth signals*, JASA 132(4) 2012](https://pubs.aip.org/asa/jasa/article/132/4/2721/830882/Audibility-of-aliasing-distortion-in-sawtooth)
* de Soras — [HIIR / polyphase resampler design](https://ldesoras.fr/doc/articles/resampler-en.pdf) · [HIIR readme](https://github.com/unevens/hiir/blob/master/readme.txt) · [musicdsp.org: Polyphase Filters](https://www.musicdsp.org/en/latest/Filters/39-polyphase-filters.html) · [KVR: Multi-stage oversampling](https://www.kvraudio.com/forum/viewtopic.php?t=497984) · [Oversampling for Nonlinear Waveshaping: Choosing the Right Filters](https://www.researchgate.net/publication/333688079_Oversampling_for_Nonlinear_Waveshaping_Choosing_the_Right_Filters) · [Resampling Filter Design for Multirate Neural Audio Effect Processing](https://arxiv.org/pdf/2501.18470)
* [Faust `aanl.lib`](https://faustlibraries.grame.fr/libs/aanl/) · [source](https://raw.githubusercontent.com/grame-cncm/faustlibraries/master/aanl.lib) · [Parker/DAFX-AntiAliasing (GitHub)](https://github.com/julian-parker/DAFX-AntiAliasing) · [ADAA for ring mod](https://www.russellmcc.com/conformal/app_notes/5-adaa-for-ring-mod/) · [polyBLEP primer](https://tonalux.org/blog/blep-minblep-polyblep-antialiased-oscillators)
* JUCE latency behaviour — [prevent click when changing latency](https://forum.juce.com/t/prevent-click-when-changing-latency-with-setlatencysamples/29846) · [VST3 latency reporting fails in Studio One](https://forum.juce.com/t/vst3-latency-reporting-in-presonus-studio-one-fails/28249)
* **Measured locally** against `_tools/JUCE/modules/juce_dsp` (`juce_Oversampling.cpp:540-770`, `.h:213`); probe sources in `scratchpad/os/{t2,alias,adaa2,drive,lut2}.cpp`

### Family A — tube, tape, transformer, pedals
* Dempwolf & Zölzer — [*A Physically-Motivated Triode Model for Circuit Simulations* (DAFx-11)](https://dafx.de/paper-archive/2011/Papers/76_e.pdf) · [Enhanced Wave Digital Triode Model](https://www.researchgate.net/publication/224597228_Enhanced_Wave_Digital_Triode_Model_for_Real-Time_Tube_Amplifier_Emulation) · [Koren / preamp modelling](https://www.ampbooks.com/mobile/dsp/preamp/) · [uTracer triode theory](https://www.dos4ever.com/uTracer3/Theory.pdf) · [DAFx-07 p169](https://www.dafx.de/paper-archive/2007/Papers/p169.pdf) · [DAFx-23 paper 15](https://dafx.de/paper-archive/2023/DAFx23_paper_15.pdf)
* Blocking distortion — [Aiken: What is blocking distortion?](https://www.aikenamps.com/index.php/what-is-blocking-distortion) · [GEOfex: coupling caps](http://www.geofex.com/ampdbug/coupling.htm)
* Chowdhury — [*Real-time Physical Modelling for Analog Tape Machines* (DAFx-19 / CCRMA)](https://ccrma.stanford.edu/~jatin/420/tape/TapeModel_DAFx.pdf) · [AnalogTapeModel (GitHub)](https://github.com/jatinchowdhury18/AnalogTapeModel) · [HysteresisProcessing.cpp](https://raw.githubusercontent.com/jatinchowdhury18/AnalogTapeModel/master/Plugin/Source/Processors/Hysteresis/HysteresisProcessing.cpp) · [HysteresisOps.h](https://raw.githubusercontent.com/jatinchowdhury18/AnalogTapeModel/master/Plugin/Source/Processors/Hysteresis/HysteresisOps.h) · [ChowTape manual](https://chowdsp.com/manuals/ChowTapeManual.pdf) · [Complex Nonlinearities Ep. 3: Hysteresis](https://jatinchowdhury18.medium.com/complex-nonlinearities-episode-3-hysteresis-fdeb2cd3e3f6) · [Jiles–Atherton model](https://en.wikipedia.org/wiki/Jiles%E2%80%93Atherton_model)
* Tape bias — [Blackmer: how to bias analog tape recorders](http://blackmerdesign.com/resources/how-to-bias-analog-tape-recorders/) · [Mix: Analog Tape 101 pt.3 — Bias Magic](https://www.mixonline.com/recording/analog-tape-101-part-3-bias-magic-373029) · [Magnetic flux & analog tape recording](https://hometheaterhifi.com/technical/technical-reviews/magnetic-flux-and-the-world-of-analog-audio-tape-recording/)
* Transformers — [Jensen: *Audio Transformers* chapter (Whitlock)](https://www.jensen-transformers.com/wp-content/uploads/2014/08/Audio-Transformers-Chapter.pdf) · [KVR: transformer integrator/soft-clip/differentiator topology](https://www.kvraudio.com/forum/viewtopic.php?t=461007) · [Preisach / harmonic-domain core hysteresis (arXiv 2503.22129)](https://arxiv.org/pdf/2503.22129) · [Variety Of Sound: an update on audio transformer modeling](https://varietyofsound.wordpress.com/2023/10/25/an-update-on-audio-transformer-modeling/) · [sound-au.com: audio transformers](https://sound-au.com/articles/audio-xfmrs.htm) · [Real-Time Audio Transformer Emulation](https://www.researchgate.net/publication/220057543_Real-Time_Audio_Transformer_Emulation_for_Virtual_Tube_Amplifiers)
* Pedals — [ElectroSmash: Tube Screamer analysis](https://electrosmash.mas-effects.com/tube-screamer-analysis.html) · [Klon Centaur](https://electrosmash.mas-effects.com/klon-centaur-analysis.html) · [ProCo RAT](https://electrosmash.mas-effects.com/proco-rat.html) · [Boss SD-1](https://www.hobby-hour.com/electronics/s/sd1-super-overdrive.php) · [LTspice Tube Screamer](https://cushychicken.github.io/posts/ltspice-tube-screamer/) · [Clipping-diode primer](https://www.guitarpedalx.com/news/gpx-blog/a-brief-hobbyist-primer-on-clipping-diodes)
* D'Angelo, Pirkle & Esqueda — [*Fast Approximation of the Lambert W Function* (DAFx-19)](https://www.dafx.de/paper-archive/2019/DAFx2019_paper_5.pdf) · [DAFx-19 paper 4 (HSU)](https://www.hsu-hh.de/ant/wp-content/uploads/sites/699/2020/10/DAFx2019_paper_4.pdf)
* Slew-induced distortion — [Wikipedia: SID/TIM](https://en.wikipedia.org/wiki/Slew-induced_distortion)

### Family B — clippers, diodes, folders
* Giangrandi — [Diode clipper analysis (measured 1N4148 constants)](https://www.giangrandi.ch/electronics/diode-clipper/diode-clipper.shtml)
* Werner et al. — [*An Improved and Generalized Diode Clipper Model for Wave Digital Filters* (DAFx-16)](https://www.researchgate.net/publication/299514713_An_Improved_and_Generalized_Diode_Clipper_Model_for_Wave_Digital_Filters) · [DAFx-24 paper 33](https://dafx.de/paper-archive/2024/papers/DAFx24_paper_33.pdf)
* Crossover distortion — [Wikipedia](https://en.wikipedia.org/wiki/Crossover_distortion) · [Benchmark: amplifier crossover distortion](https://benchmarkmedia.com/blogs/application_notes/131424519-amplifier-crossover-distortion) · [ScienceDirect topic](https://www.sciencedirect.com/topics/engineering/crossover-distortion)
* Octave/rectifier circuits — [Tycobrahe Octavia analysis](http://revolutiondeux.blogspot.com/2012/07/tycobrahe-octavia.html) · [Octavia experiments/improvements](https://solgrind.wordpress.com/2009/03/05/octavia-experimentsimprovements/) · [tagboardeffects: Octavia](https://tagboardeffects.blogspot.com/2013/04/tycobrahe-octavia.html) · [diystompboxes thread](https://www.diystompboxes.com/smfforum/index.php?topic=118417.0) · [1N34A SPICE models](https://www.allaboutcircuits.com/textbook/semiconductors/chpt-3/spice-models/)
* Doidic et al. asymmetric soft clip — US patent 5,789,689, reproduced in Pakarinen & Yeh, *A Review of Digital Techniques for Modeling Vacuum-Tube Guitar Amplifiers* (CMJ 2009)
* Esqueda, Pöntynen, Välimäki & Parker — [*Virtual Analog Buchla 259 Wavefolder* (DAFx-17)](https://www.dafx17.eca.ed.ac.uk/papers/DAFx17_paper_82.pdf) · [project page](http://research.spa.aalto.fi/publications/papers/dafx17-wavefolder/) · [*Virtual Analog Models of the Lockhart and Serge Wavefolders*, Applied Sci. 7(12) 1328](https://www.mdpi.com/2076-3417/7/12/1328) · [PDF](https://res.mdpi.com/applsci/applsci-07-01328/article_deploy/applsci-07-01328.pdf) · [*VA Model of the Lockhart Wave Folder* (SMC-17)](http://smc2017.aalto.fi/media/materials/proceedings/SMC17_p336.pdf) · [Faust b259wf](https://github.com/georgezachos/b259wf) · [CGS wave folder](https://www.cgs.synth.net/modules/cgs52_folder.html) · [Chowdhury: Complex Nonlinearities Ep. 6 — Wavefolding](https://jatinchowdhury18.medium.com/complex-nonlinearities-episode-6-wavefolding-9529b5fe4102)
* Waveshaping fundamentals — [Berkeley EE142 distortion notes](https://rfic.eecs.berkeley.edu/courses/ee142/pdf/module14_disto_intro.pdf) · [NTNU gDSP tanh lessons](http://gdsp.hf.ntnu.no/lessons/3/17/) · [Till: harmonic distortion](https://till.com/articles/harmonicdistortion/) · [musicdsp: variable-hardness clipping](https://www.musicdsp.org/en/latest/Effects/104-variable-hardness-clipping-function.html) · [Wikipedia: Waveshaper](https://en.wikipedia.org/wiki/Waveshaper) · [Perfect Circuit: learning synthesis — waveshapers](https://www.perfectcircuit.com/signal/learning-synthesis-waveshapers) · [monotonic symmetrical soft-clipping polynomial](https://dsp.stackexchange.com/questions/36202/monotonic-symmetrical-soft-clipping-polynomial)

### Family B — arbitrary / drawn curves
* Le Brun — waveshaping synthesis via Chebyshev polynomials: [Puckette, *Theory & Technique* §80](https://msp.ucsd.edu/techniques/v0.08/book-html/node80.html) · [Smyth: matching a spectrum](https://musicweb.ucsd.edu/~trsmyth/waveshaping/Matching_Spectrum_Using.html) · [slides](https://musicweb.ucsd.edu/~trsmyth/waveshaping/waveshaping_4up.pdf) · [Chebyshev polynomials](https://en.wikipedia.org/wiki/Chebyshev_polynomials)
* [KVR/Signalsmith — the linear-interpolation-on-F1 trap](https://www.kvraudio.com/forum/viewtopic.php?p=9282206)
* [iZotope Trash 2 manual (Base / Your Curve / Results overlay; Log Mode)](https://help.izotope.com/docs/izotope-trash2-help.pdf) · [Trash product page](https://www.izotope.com/en/products/trash.html)
* [MeldaProduction MWaveShaper (harmonic mode)](https://www.meldaproduction.com/MWaveShaper/features) · [MWaveShaperMB](https://www.meldaproduction.com/MWaveShaperMB)
* [Bitwig Grid module reference (Transfer)](https://www.bitwig.com/userguide/latest/grid_modules/) · [Bitwig lookup/Transfer guide](https://polarity.me/posts/bitwig-guides/2026-01-21-lookup-data-tables-bitwig-grid-module-guide/)
* [Kilohearts Shaper (Overflow modes)](https://kilohearts.com/products/shaper) · [Shaper Table](https://kilohearts.com/docs/shaper_table) · [Snapins docs](https://kilohearts.com/docs/snapins) · [Phase Plant docs](https://kilohearts.com/docs/phase_plant)
* [Overdraw (GPL-3, automatable spline transfer functions, 32× OS)](https://github.com/unevens/Overdraw)
* Interpolation — [PCHIP (MathWorks)](https://www.mathworks.com/help/matlab/ref/pchip.html) · [SciPy PchipInterpolator](https://docs.scipy.org/doc/scipy/reference/generated/scipy.interpolate.PchipInterpolator.html) · [Fritsch–Carlson explainer](https://xuefeng-xu.github.io/blog/pchip.html)

### Family C — sample-domain
* Lipshitz, Vanderkooy & Wannamaker — [*Quantization and Dither: A Theoretical Survey* (JAES 1992)](https://hajim.rochester.edu/ece/sites/zduan/teaching/ece472/reading/Lipshitz_1992.pdf) · [Wannamaker, IEEE TSP](http://robertwannamaker.com/writings/ieee.pdf)
* [µ-law algorithm (ITU-T G.711)](https://en.wikipedia.org/wiki/%CE%9C-law_algorithm) · [mid-tread vs mid-riser quantiser characteristics](https://www.researchgate.net/figure/Quantizer-transfer-characteristics-a-mid-tread-b-mid-riser-with-denoting-the_fig1_3317523)
* [D16 Decimort 2 manual (Approximation filter / Images filter / Jitter / DC Shift)](https://manuals.plus/d16-group/decimort-2-high-quality-bit-crusher-manual) · [Splice feature guide](https://splice.com/blog/feature-guide-of-d16-decimort2/) · [MacProVideo overview](https://www.macprovideo.com/article/audio-software/d16-decimort-2-analog-style-filter-bit-crusher-now-available)
* ZOH / reconstruction — [Robertson: DAC Zero-Order Hold Models](https://www.dsprelated.com/showarticle/1627.php) · [dsplog: ZOH & FOH interpolation](https://dsplog.com/2007/03/25/zero-order-hold-and-first-order-hold-based-interpolation/)
* Vintage samplers — [E-mu SP-1200](https://en.wikipedia.org/wiki/E-mu_SP-1200) · [SP-1200 (Vintage Synth)](https://www.vintagesynth.com/e-mu/sp-1200) · [Fairlight CMI](https://en.wikipedia.org/wiki/Fairlight_CMI) · [Attack: replicating vintage samplers](https://www.attackmagazine.com/technique/character-crunch-replicating-the-sound-of-vintage-samplers/2/)
* [Bitcrusher (Wikipedia)](https://en.wikipedia.org/wiki/Bitcrusher) · [Perfect Circuit: Bitcrushers + SRR](https://www.perfectcircuit.com/signal/weird-fx-bitcrushers) · [ADSR: building bitcrushing FX](https://www.adsrsounds.com/reaktor-tutorials/building-fx-part-vi-basic-bitcrushing/) · [EDMProd: bitcrushing](https://www.edmprod.com/bitcrushing/)

### Reference plugins — the bar
* [Xfer Serum manual (Distortion module, X-Shaper / X-Shaper Asym, Warp menu, LFO graph gestures)](https://s3.amazonaws.com/decembercymatics/Serum_Manual.pdf) · [Serum 2 web manual](https://xferrecords.com/web-manual/serum-2/welcome) · [Serum 2 product page](https://xferrecords.com/products/serum-2) · [ADSR: Everything New in Serum 2](https://www.adsrsounds.com/serum-tutorials/serum-2-everything-new-in-serum-2-2025/) · [Splice: Serum 2 advanced features](https://splice.com/blog/serum-2-advanced-features/) · [mizonote Serum guide (Diode 1/2, Asym, Rectify, Zero-Square descriptions)](https://www.mizonote-m.com/how-to-use-serum-complete-guide-5/)
* Vital (GPL-3) — [repo](https://github.com/mtytel/vital) · [distortion.cpp](https://raw.githubusercontent.com/mtytel/vital/main/src/synthesis/effects/distortion.cpp) · [distortion.h](https://raw.githubusercontent.com/mtytel/vital/main/src/synthesis/effects/distortion.h) · [synth_parameters.cpp (drive declared in dB, kLinear taper)](https://raw.githubusercontent.com/mtytel/vital/main/src/common/synth_parameters.cpp) · [synth_oscillator.cpp (warp constants, kMaxFmModulation)](https://raw.githubusercontent.com/mtytel/vital/main/src/synthesis/producers/synth_oscillator.cpp) · [sound_engine.cpp (Clamp(−2.1, 2.1); kDefaultOversamplingAmount = 2)](https://raw.githubusercontent.com/mtytel/vital/main/src/synthesis/synth_engine/sound_engine.cpp)
* [Soundtoys Decapitator manual (Punish = +20 dB; auto-gain optional; styles A/E/N/T/P)](https://www.soundtoys.com/wp-content/uploads/Decapitator-Manual.pdf) · [product page](https://www.soundtoys.com/product/decapitator/) · [Barry Rudolph review](https://www.barryrudolph.com/newtoys/toys7/soundtoysdecapitator.html)
* [FabFilter Saturn 2 manual (auto output adjustment; Level −inf…+36 dB; crossover slopes)](https://www.fabfilter.com/downloads/pdf/help/ffsaturn2-manual.pdf) · [band controls](https://www.fabfilter.com/help/saturn/using/bandcontrols)
* [Arturia Dist COLDFIRE manual (11 algorithms; Wavefolder 7 shapes; Waveshaper 11 options)](https://dl.arturia.net/products/dist-coldfire/manual/dist-coldfire_Manual_1_0_0_EN.pdf)
* [Ableton Live manual (Saturator / Redux)](https://www.manualslib.com/manual/449421/Ableton-Live.html?page=331) · [SOS: Ableton Wavetable oscillator effects](https://www.soundonsound.com/techniques/wavetable-abletons-new-synth)
* [Sound On Sound: Multi-band Processing & Distortion](https://www.soundonsound.com/techniques/multi-band-processing-distortion) · [KVR: Tape saturation and pre/de-emphasis filters](https://www.kvraudio.com/forum/viewtopic.php?t=318629)

### Repo (read directly, line references verified)
`Source/PluginProcessor.cpp` (24-52 master constants · 46 fb299 measurement · 6112-6130 `kVoiceToFxPad` ·
5578-5628 shared→audio swap + per-point-mod rebake · 6957-7134 insert + **fb305 subtraction at :6979 and
:7111** · 7140-7165 master output stage · 8046-8094 shaper bias/bake/setter) ·
`Source/PluginProcessor.h` (695 `getReverbBloom` · 1044-1076 `LfoShapePtM` · 1515 `reverbSendBuf_` ·
1539 `delaySendBuf_`) · `Source/SynthVoice.h` (536-560 `fShape` · 652/838/846/1555-1561 warp clamps ·
891-969 `applyPhaseWarp`/`applyAmpWarp` · 2181-2192 WARP2 mod gap · 2296-2302 `warpRateMul` ·
2349-2352 FM index · 2413-2425 `fmRateMul` · 2633-2641 BLEND FM/PD · 2799-2803 rectify DC block ·
2812 AIR shaper · 2826-2837 SAMPLE WARP · 2911-2916 **the DC-subtraction bug** · 4155-4185 `pdrive` ·
4512-4633 fold + ADAA harness · 4878 `level_` · 5093-5100 per-osc rectify blockers) ·
`Source/TerrainFilters.h` (42 `fastTanh` · 69-79 **`DCBlocker` — the sample-rate bug** · 83+ `TPTOnePole` ·
990-1032 `WaveShaper` ADAA) · `Source/HarmonicEngine.h` (306-360 `postProcess` · 434-451 `fastExp2` /
`lncoshf_`) · `Source/DelayEngine.h` (117-127 `onePole` · 139 `processSample` · 162-171 glide ·
315-322 `softClip`) · `Source/VintageReverb.h` (46/287 DC blocker · 118-130 `recCoefT` · 213/278 ZOH ·
285 M/S width · 295 `flush` · 305-323 crush/dither/`rand11` · 337-339 `CharBias`) ·
`Source/TapeMachines.h` (37-317 `TapeMachineBase`, `SmoothRandom`, `SVFBandpass`, `CassetteMachine`,
`StudioMachine::saturateSample`) · `Source/SubOsc.h` (63-97 `heat`/`pblep`/`lncosh`) ·
`Source/ModalEngine.h` (299 `softClip` · 329 `DCBlock`) · `Source/ConvolutionReverb.h` (166
`getLatency`, zero callers) · `Source/Wavetable.h` (66-67 `kFrameSize`/`kNumMipLevels`/`kMaxHarmonics`) ·
`Source/SynthLFO.h` (27-29 `kLfoTableN` · 339-343 table read) · `Source/ParameterIDs.hpp` (403
`SYN_FX_ORDER` — **[AUDIT 2026-08-14] no longer a bool: fb341 made it an `AudioParameterChoice(6)`,
built at `PluginProcessor.cpp:3488`; cardinality is now frozen forever, see `FX-CHAIN-BIBLE.md`
§3.4**) · `Source/IndyFxChain.h` · `Source/ui/public/index.html` (7155-7156 `.fxr-core` ·
7506 `CORES` · 7717-7745 bloom rAF poll · 14736-14747 envelope bias · 23569-24107 the SHAPER module) ·
`Design/fx-rack-v7-CANONICAL.html` · `Design/fx-back-panel-mockup.html` · `Design/REVERB-BUILD-BIBLE.md`
