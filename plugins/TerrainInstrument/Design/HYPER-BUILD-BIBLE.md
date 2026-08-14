# Terrain Instrument — Hyper/Dimension-Alternative Build Bible (the UNISON-WIDENER)

**v1 — research complete. The single authoritative spec for the 4th FX device.**
Written after fb345 (distortion certified). Every number below is either read out of a cited
primary source (manual, thesis, measured-mode documentation), read out of this repo at the
cited line, or derived with the math shown. No DSP written yet.
Companion to `DISTORTION-BUILD-BIBLE.md` and `REVERB-BUILD-BIBLE.md` — same laws, same chassis.

> **🔍 v1.1 — ADVERSARIALLY AUDITED 2026-08-14.** The v1 draft above was never reviewed. It has
> now been checked line-by-line against the repo at HEAD and against primary sources. **Nine
> substantive defects were found and fixed in place; every fix is marked 🔧 CORRECTED or 🚩 at the
> point of the error, so the wrong version is visible next to the right one.** Headlines:
> the **Width M/S rotation was wrong** (`π/4` made "neutral" +2.3 dB mid / −5.3 dB side — §3.4);
> the **Dimension LFO is a TRIANGLE, not a trapezoid** — Arturia says so verbatim, and the
> draft's flat-tops would have nulled the very detune they were meant to create (§1.2-3, §2.2);
> the **Detune depth clamp drove the read head negative** (§3.1); **Waves Doubler's detune
> ceiling is ±100 cents, not ±50** (§2.3); **`SYN_FX_ORDER` is a choice(6) that cannot grow —
> the 4th device is BLOCKED on a decision, §3.9-C / §11 Q9**; the exclusion landmine is **six**
> sites, not three (§4); there is **no stereo scope tap to recycle** (§5.2-2); FX-rack params
> **do not use WebSliderRelay** (pitfall 12); and `PV Glass` carries a **32 ms uncompensatable
> latency** that rack law A forbids fixing (§2.3). Unverifiable claims are now flagged ⚠️ inline
> rather than deleted — see the §12 verification key.

> **The mission question (Max):** what exactly IS Serum's Hyper/Dimension, is the combo their
> signature, and can we build our own better one under our own name?
> **The answer, up front:** Hyper is a 1–7-voice *micro-delay chorus* — LFO-modulated delay
> lines whose slope produces cyclic pitch scatter, i.e. **unison applied to audio** — and
> Dimension is a *Roland SDD-320 lineage pseudo-stereo widener* — "4 delay lines summed
> out-of-phase and slowly amplitude modulated" (Serum manual's own words, §1.3). Neither
> mechanism is theirs. Unison-on-audio is the JP-8000 supersaw (1996) played through a delay
> line; the Dimension trick is Roland 1979, re-shipped by BOSS (DC-2, 1985), Arturia, Audiority,
> and by **Steve Duda himself as the free "Dimension Expander" years before Serum existed**.
> The *pairing* is Serum's packaging, not protectable DNA — and §2 builds a 7-Type device that
> covers their 2 modes and 5 more besides, each from a documented public lineage.

---

## 0. Scope decision (proposed — needs Max's lock, §11 Q1)

**ONE device: a unison-widener.** Working name **`Widen`** (candidates in §0.2). It is the
"make it huge" device: it takes whatever the bus carries and multiplies/spreads it — in *pitch*
(unison voices), in *time* (micro-delays), and in *space* (decorrelation, M/S). It is NOT the
future Chorus device (Serum 2 ships Chorus AND Hyper/Dimension as separate menu entries; so
will we — Chorus = one audible cyclic voice pair, this device = a *crowd*). It is NOT a
mastering imager (no multiband width bands; the Splitter epic owns that).

### 0.1 Why one device and not two (Hyper + Dimension separately)

Serum itself ships them as ONE menu item ("Hyper / Dimension") with two mini-sections and two
Mix knobs. That is a confession: they are one job — *thicken then widen* — and the two halves
are always used together. Our chassis (2 dropdowns + 8 back knobs) holds both as **Types of one
mechanism family: N delayed copies of the input, differing in how the copies move and how they
are laid into the stereo field.** Exactly as 23 distortion modes live behind one Type dropdown.

### 0.2 Name candidates (pragmatic-naming law: the name says what it DOES)

| Candidate | For | Against |
|---|---|---|
| **`Widen`** ⭐ | The honest verb; every Type widens; Title-case; zero jargon | Soft-sounding; near "Width" knobs elsewhere (different word — no-doubles safe) |
| **`Swarm`** | Evocative + accurate for the unison Types (voices swarm the pitch) | Less literal for Mirror/Bands; if chosen, Type 1 renames to `Stack` |
| **`Thicken`** | What Max will reach for it to do | Says nothing about stereo |
| **`Stack`** | Unison stack, short, honest | Reads like a routing feature |
| **`Multiply`** | Copies of the input — the actual mechanism | Math-y |
| **`Broaden`** | Honest | Weak-sounding |
| **`Halo`** | The sound it makes around a patch | Poetic, not pragmatic — likely fails the law |
| **`Panorama`** | The result | Long; caps-lock-y in the rack |

**Names ruled OUT** (every rule-out re-checked by the audit, all four hold): `Hyper`,
`Dimension` (Serum's exact menu string — the one thing that IS their signature), `Wider`
(✅ Polyverse product, verified live), `Doubler` (✅ Waves product, user guide verified),
`Unison` (✅ no-doubles: **verified** — `setUnisonA()` at SynthVoice.h:878, the osc unison
control block, `count 1..16 · detune · blend · width`, with `kUniMaxDetuneCents = 50.0f` at
:5908), `Imager` (iZotope Ozone Imager).
Bible below writes **`Widen`**; global-replace on Max's pick.

---

## 1. History and circuits — the lineage that defined the effect

Six hardware/algorithm bloodlines feed this device. Each becomes a Type in §2.

### 1.1 Roland JP-8000 "Super Saw" (1996) — unison, measured

The godfather of "huge". 7 sawtooth oscillators, 1 center + 6 detuned. **Adam Szabo's thesis
measured everything we need** (adamszabo.com, "How to Emulate the Super Saw"):

* **The detune fan at full detune** (ratios vs the center osc, measured at C5 523.3572 Hz):
  `1 − 0.11002313, 1 − 0.06288439, 1 − 0.01952356, 1, 1 + 0.01991221, 1 + 0.06216538, 1 + 0.10745242`
  — in cents that is **−202 / −112 / −34 / 0 / +34 / +104 / +177** (derived: `1200·log2(ratio)`;
  the earlier draft's "±~180 cents" understated the DOWN side by ~22 cents — the fan is
  **asymmetric, and the down-detuned voices travel further**, which is exactly why Szabo's
  pitch-tracked HPF exists). **The fan is not evenly spaced** — that unevenness is why a
  supersaw shimmers instead of phasing.
* **The detune knob curve is an 11th-order polynomial** (Szabo eq., MIDI-sampled): flat-ish rise
  to 0.5, steeper after, "drastic rise after 0.9". Coefficients (x = knob 0..1):
  `y = 10028.7312891634x^11 − 50818.8652045924x^10 + 111363.4808729368x^9 − 138150.6761080548x^8
  + 106649.6679158292x^7 − 53046.9642751875x^6 + 17019.9518580080x^5 − 3425.0836591318x^4
  + 404.2703938388x^3 − 24.1878824391x^2 + 0.6717417634x + 0.0030115596`
* **The mix law:** center voice falls LINEARLY, sides rise as a PARABOLA:
  `center(b) = −0.55366·b + 0.99785` · `sides(b) = −0.73764·b² + 1.2841·b + 0.044372`
  (equal loudness at b = 0.75). This is our `Balance` knob (§4), verbatim, for free.
* Phases strictly random per note; a pitch-tracked HPF at the fundamental cleans the rumble of
  the detuned-down voices. Both lessons transfer (§3.1, §9).

### 1.2 Roland SDD-320 Dimension D (1979) — the widener

The 4-button box on Bowie's "Let's Dance", Peter Gabriel's "In Your Eyes" (studio lore) — famous
for widening WITHOUT audible modulation. Circuit, per the Arturia DIMENSION-D manual (which
documents the measured original) + service notes + community analyses:

1. **Pre-emphasis + compander:** low-shelf-up/high-shelf-down EQ → compressor → BBD → expander
   → inverse EQ. (Noise management, but it *is* the tone of the box.)
2. **Two BBD delay lines**, one per channel (the Arturia signal-walk describes a two-channel
   BBD stage with a cross-mix; it names no chip and no delay times).
   ⚠️ **UNVERIFIED — do not quote as fact:** the "MN3007-class" chip identity and the
   "L ≈ 1 ms / R ≈ 6 ms" operating points are *community/forum* claims (Fractal thread), NOT in
   any primary document we could read. Treat 5 ms nominal (§2.2) as OUR choice, not a measurement.
3. 🔧 **CORRECTED (2026-08-14 audit, primary source read):** the original LFO is a **TRIANGLE**,
   not a trapezoid. Arturia, verbatim: *"The original unit's LFO only allows a triangle
   waveform"* (Control Panel §4.3.1, Oscillator Shape — the Default button "selects the original
   (default) triangle waveform"; Sine/Ramp/S&G/S&H are Arturia's own *additions*).
   **ANTI-PHASE across channels:** `dL(t) = d0 + m(t)`, `dR(t) = d0 − m(t)`.
   A triangle already has **constant |slope|** for the whole cycle ⇒ constant pitch offset whose
   sign flips at the apexes — reads as *detune*, not vibrato. **This is THE Dimension tell, and
   the earlier draft's slew-clamped "trapezoid with ~25 % flat tops" would have DESTROYED it**:
   a flat top is zero slope = zero pitch offset, so the detune would periodically drop out
   (a tri-modal pitch histogram with a big zero spike, not the bimodal ±c signature).
   The only legitimate slew work is a **short apex smoothing** (≈1–2 ms, §9.5) to round the
   slope discontinuity at the turn — otherwise the sign flip is a delay-slope step (a tick).
   ⚠️ **UNVERIFIED:** the "0.25–0.5 Hz / depth ~14 %" figures are forum-measured, not Arturia's;
   Arturia publishes no LFO rate or depth number. Keep as a starting zone, confirm by ear.
4. **The polarity cross-mix — VERIFIED verbatim (Arturia manual, Overview §):** *"The delayed
   signal is cross-mixed to the other channel with opposite polarity. Normally this would result
   in a loss of lower frequencies, but the filtering circuit applies a kind of low-shelf EQ to the
   entire signal. This filtering circuit also slightly boosts the bass of the direct signal, this
   way avoiding the loss in the bass region."* This anti-phase side energy is the width, and the
   dry bass boost (§2.2's +2 dB @150 Hz) is the documented compensation — not our invention.
5. **The 4 buttons, as Arturia measured them — VERIFIED verbatim** (not the internet folklore):
   *"Mode 1 has the softest chorus effect, while Mode 2 has more chorus intensity (the delay
   times are about half those of Mode 1). Mode 3 has a delay time more or less between the first
   two modes, but a modulation intensity by the LFO that is twice that of modes 1 and 2… Mode 4
   is a special button. It doesn't work alone but in combination with each of the first three. It
   injects more wet signal in the output… (despite many different descriptions that can be found
   online for the Mode 4 button, this is what we actually measured on original units)."*
   Note also **"Mode 0"** — Arturia's own addition, *not* on the original: compander + shelving
   filters, BBD bypassed = colour without delay. Honest pedigree for a `Color Only` Character —
   a swap-in candidate only, since §2.2's list is already full at the choice(8) cardinality.

BOSS DC-2 (1985) minified the circuit; SOS's DC-2W review confirms the two lines modulated in
opposite directions and the mono sum staying clean. **Steve Duda's free Xfer "Dimension
Expander"** is a 4-voice restatement — "four chorus parts with extended delay times... two out
of the voices being out of phase with the other two", controls: power/size/mix — and Serum's
Dimension half is that plugin folded in (manual: "4 delay lines summed out-of-phase and slowly
amplitude modulated", Size = delay time, Mix 0 % = off).

### 1.3 Serum's Hyper/Dimension itself — the exact reference text

From the Serum manual (PDF, §FX pp. 27–28). **VERIFIED verbatim against the PDF, 2026-08-14
audit** — every quote below is exact. One nuance the earlier draft missed: Unison's floor is
**0**, not 1 — the manual's own advice is *"If you only wish to use the Dimension effect and not
the Hyper, it is recommended to set the 'Unison' control to 0"*, i.e. their voice box is 0–7 and
0 is how you bypass the Hyper half. Our `Voices` starts at 1 because Mix/Power already own the
bypass job (§4) — a "0 voices" state that silently means off is a dead-knob trap (law 5).

* **Hyper** — "a micro-delay chorus effect with a variable number of voices (1–7)... can be
  configured to re-trigger on every MIDI note, which adds to the potential simulation of a
  unison." Params: **Rate** ("the speed at which the various Hyper voices are oscillating
  sharp/flat in pitch"), **Detune** ("amount / depth for the Hyper voice oscillations"),
  **Retrig** ("reset all of the Hyper voices to start over from a zeroed pitch offset. This
  provides a 'laser' like zap effect on each note on"), **Unison** (voice count number box),
  **Mix** (independent of Dimension).
* **Dimension** — "a pseudo-stereo effect made out of 4 delay lines summed out-of-phase and
  slowly amplitude modulated to provide a subtle amount motion... useful for adding a perceived
  width to an otherwise mono signal." Params: **Size** ("the amount of delay time for the
  delays"), **Mix** ("When set to 0%, the Dimension effect is disabled").
* **Serum 2 changed nothing inside it** — the What's-New doc lists Bode/Convolve/Delay-HQ/
  Distortion-Overdrive/3 reverbs/Utility as the FX changes; Hyper/Dimension is carried over.
  What Serum 2 DID add: every FX module has a graphical display with **Direct Manipulation**
  ("directly adjust controls using the graphical display") — the bar our card must clear (§5).
* **The gap we exploit** (same shape as the distortion finding): 7 params total, Rate×Detune
  coupled (depth in *delay* domain, so apparent cents CHANGE when you move Rate — nobody's
  Detune knob reads honestly), voice pitch fan undocumented/flat, no pan-fan control, no
  feedback, no mono-safety features, no bass anchoring, and the two halves can't rebalance
  (fixed serial order). Eleven params + Characters beat this cleanly.

### 1.4 Micro-pitch doubling — Eventide H3000 / AMS DMX 15-80s (1980s)

The studio vocal-width standard: **constant** ±5–20 cent pitch shifts L/R plus small unequal
slap delays. Soundtoys MicroShift, **VERIFIED from soundtoys.com**: *"3 different flavors of
widening based on sought-after hardware"* — the **Eventide H3000** ("which featured algorithms
originally designed by Soundtoys' founders") and the **AMS DMX 15-80s** — with knobs **Detune**
("adjust the amount of micro pitch shifting"), **Delay** ("change the amount of time-varying
delay") and **Focus** ("sculpt away low and mid frequencies").
⚠️ **UNVERIFIED — the specific preset numbers "H3000 #231 / #519" are NOT on Soundtoys' product
page** (they circulate in forum/press coverage). Do not quote them as fact; the *styles* and the
two hardware ancestors are documented, the preset IDs are not. Discriminator vs chorus stands
regardless: the offset is **static** — sidebands sit at fixed Hz offsets, no periodic flux.

### 1.5 String-machine ensemble — ARP/Eminent Solina (1974)

Triple chorus: 3 BBD taps modulated by a **3-phase LFO pair** (0°/120°/240°): one slow/deep
("chorus") + one fast/shallow ("vibrato") summed per phase. jpcima's open-source
`ensemble-chorus` (GitHub) models 3–6 BBD lines with paired LFOs at fixed phase offsets;
Valhalla's ÜberMod documents modeling "the 3-phase LFOs found in the Solina and Crumar
Performer". Community-standard rates: slow ≈ 0.6–0.9 Hz deep, fast ≈ 5.5–6.5 Hz shallow.
The result is a WALL — dense, symmetric, always-moving — unmistakably different from a 2-voice
chorus or a supersaw fan.

### 1.6 Mono-compatible decorrelators — the modern school

* **Polyverse/Infected Mushroom Wider — VERIFIED** (free; launched **May 2018**, SOS news
  18 May 2018): polyversemusic.com verbatim — *"Wider's diverse all-pass and comb filtering
  algorithm"*, *"any signal that has been widened will always remain in phase with itself, even
  when it is summed back into mono"*, *"Wider cancels itself out when summed to mono"*, and
  *"increase the stereo image of any mono signal by up to 200 % of full stereo"*. The trick
  class: any `L = m + s`, `R = m − s` is mono-EXACT by construction, whatever s is.
* **iZotope Ozone Imager Stereoize**: Mode I = "Haas Effect-based decorrelation... delayed copy
  of the mid channel injected into the side channel"; Mode II = newer, "preserves transients at
  higher settings"; both "create pure side-channel content" ⇒ mono compatible.
* **Academic anchor:** Velvet-Noise Decorrelator (DAFx-17) + Optimized VND (DAFx-18) + the
  open-source StereoWidener (DAFx-24, CCRMA/orchidas): decorrelate via sparse velvet-noise FIR
  or **cascaded allpasses with randomized phase**; the DAFx-24 paper measures the mono-sum cost
  of dual-allpass decorrelation at **"mild spectral ripple, typically less than 1 to 2 dB...
  no deep notches and no comb filtering"**. These are our Mirror/Bands Types' pedigree.
* Prior art for band-alternation: Orban 245E "Stereo Synthesizer" (1975) and ultimately
  Schroeder's complementary-comb pseudostereo (1958). Public domain several times over.

**Legal/energetic conclusion:** every mechanism in this device predates Serum by 20–60 years
and ships today in a dozen products. We take the *mechanisms* (public), tune our own voicings
and laws (ours), and never use their menu string.

---

## 2. THE TYPES — 7, each night-and-day, each with a measurable discriminator

Type dropdown order = drama order (subtle → destroyed at identical Amount is NOT the goal —
each is a different *mechanism*; order below is family-logical). Names obey pragmatic law.
Every Type: **8 Characters** (distortion pattern, ParameterIDs.hpp:407 precedent) — voicing
changes the *physics* (rates, fan shapes, filters), never just EQ.

| # | Type | Lineage | One-line mechanism | THE discriminator (harness-measurable) |
|---|------|---------|--------------------|----------------------------------------|
| 1 | **Stack** | Serum Hyper + JP-8000 laws | N (1–8) LFO-modulated micro-delay voices, supersaw fan + mix law | Sideband count ∝ Voices; cyclic spectral flux at Rate; Detune reads TRUE cents (constant-cents law §3.1) |
| 2 | **Dimension** | Roland SDD-320 / DC-2 / Duda | 2×2 anti-phase **triangle**-modulated lines + inverted-polarity cross-mix | Near-zero centroid flux (motionless!) yet side/mid energy ≥ target; constant triangle slope ⇒ **BIMODAL** pitch histogram (two sharp lobes at ±c, no zero spike — the test that catches a sine or a flat-topped LFO sneaking in) |
| 3 | **Shift** | H3000 #231 / AMS / MicroShift | Dual granular pitch shifters, +c cents L / −c cents R + unequal slap | STATIC sideband offset (no periodicity in flux); L/R spectra mirror-detuned |
| 4 | **Ensemble** | Solina triple chorus | 3 taps/ch, 3-phase dual LFO (slow-deep + fast-shallow) | Modulation spectrum has BOTH LFO lines (≈0.7 Hz and ≈6 Hz); 6-lobe pan distribution |
| 5 | **Doubler** | ADT / Waves Doubler | 2–4 discrete voices, static 15–80 ms delays, RANDOM-WALK pitch wander, per-voice pan | Aperiodic flux (no LFO line in mod spectrum); discrete echo peaks in cepstrum |
| 6 | **Mirror** | Wider / Ozone Stereoize / VND | Pure-side injection: s = w·[APcascade(m) − m]/2, L = m+s, R = m−s | Mono sum ≡ dry (< 0.1 dB ripple by construction); per-channel ripple is **APERIODIC** — cepstrum shows NO single peak (the anti-Bands test) |
| 7 | **Bands** | Orban 245E / Schroeder 1958 | Complementary comb side: s = w·delay(m, 0.5–10 ms), L = m+s, R = m−s | Mono sum ≡ dry; per-channel PERIODIC comb (ripple period = 1/τ) — the anti-Mirror |
| ✂ | ~~Rotor~~ | Leslie Doppler widener | CUT — Doppler crossfade panner belongs to the future Phaser/Rotary device | — |

**Why these 7 are a superset of Serum:** Stack ⊃ Hyper (adds pan fan, honest cents, feedback,
retrig glide); Dimension ⊃ their Dimension (adds mode-continuum, real char set); the other 5
Serum simply does not have. Width matched, depth beaten — same shape as the distortion win.

### Per-Type recipes (with Character sketches)

#### 2.1 STACK *(the default Type — the flagship)*
* **Engine:** N = `Voices` (1–8) voices per channel-pair; voice v reads one shared circular
  buffer at `d_v(t) = base_v + A_v·lfo_v(t)`; base_v scattered 9–24 ms (co-prime-ish ms values,
  §3.1) so voices never comb statically; lfo_v = sine, phase φ_v strictly scattered (§9.6),
  rate `r_v = Rate·ρ_v` with ρ_v ∈ {1.00, 1.07, 0.93, 1.13, 0.89, 1.19, 0.83, 1.23} (mutually
  irrational-ish — the TerrainChorus RIGHT_RATE_RATIO 1.07 idea, TerrainChorus.h:18, times 8).
* **The fan:** per-voice depth `A_v ∝ |offset_v| / 0.11002313` using the JP-8000 offsets §1.1 —
  the *uneven* supersaw spacing, applied to modulation depth. Center voice = the DRY read
  (A_0 = 0, base_0 = 0: the un-delayed input), so `Balance` reproduces the center/sides law.
* **Detune knob law (constant-cents, §3.1):** target peak cents c(t) = `120 · szabo11(t)`
  (the §1.1 polynomial, rescaled to 1.0 max) — yes **±120 cents max**: past-useful mayhem
  (the JP-8000 itself reaches −202/+177 cents, §1.1; Serum's Hyper audibly reaches ± tens).
  No playing safe. **Reachability floor:** the honest max cents depends on Rate *and* on the
  Character's base scatter — see the corrected clamp law in §3.1 (±120 needs ≈1.3 Hz on the
  default bases, ≈3.2 Hz on `Tight Digital`).
* **Retrig** (front pill) — 🔧 **CORRECTED (audit):** "glide φ_v → 0 over 8 ms" is *not* a
  click-free number and the earlier draft's claim that it was is false. Phase is not a free
  variable — φ maps to a read position, so forcing φ→0 in 8 ms drags the read head by up to
  `2·A_v` (≈46 ms at full depth) in 8 ms = a delay slew of 5.75 samples/sample ≈ **+32 semitones**
  of chirp. That is a zap, but an *unbounded* one that changes pitch with Detune. **The law:
  slew-cap the READ POSITION, not the phase** — `|Δd/Δt| ≤ 0.5` (a hard ±1-octave chirp ceiling,
  the same bound the delay's time-glide uses), so retrig completes in `2·A_v / 0.5` samples
  (≈92 ms at A = 23 ms, ≈8 ms at A = 2 ms) and the zap sounds *identical* at every Detune
  setting. Voices then sweep out from unison — the "laser", bounded and reproducible.
* **Characters:** `JP Classic` (default: exact §1.1 fan + curve) · `Even Fan` (linear-spaced
  offsets — audibly "phasier", proof the JP unevenness matters) · `Analog Drift` (adds
  SmoothRandom ±15 % on each r_v) · `Tight Digital` (bases 4–9 ms — glassier, more metallic) ·
  `Wide Fan` (bases 15–45 ms — thicker, doublier) · `Octave Bloom` (voices 7–8 read at
  2× buffer rate = +1 octave shimmer, level −12 dB) · `Laser` (retrig always-on + Rate ×4 on
  the first 60 ms after note-on) · `Sub Anchor` (voice 2 = −1 octave via half-rate read, mono,
  −9 dB — the hardstyle trick).
  ⚠️ **Costed by the audit: `Octave Bloom` and `Sub Anchor` are NOT plain reads.** A read head
  moving at 2× or 0.5× the write rate drifts through the write pointer and must wrap — so both
  need the **2-tap 180°-offset window + triangular crossfade from §2.3**, i.e. Stack quietly
  pulls in the Shift machinery for two Characters (or they click once per traversal). Either
  budget that (it is the same code path, ~+2 reads/sample on 1–2 voices) or drop them to a
  fixed large detune. Do not ship them as "a read at 2× rate".

#### 2.2 DIMENSION
* **Engine (per §1.2, all five stages):** tilt pre-emphasis (+3 dB LF shelf 200 Hz / −3 dB HF
  shelf 5 kHz — cheap 2 shelves) → optional compander (Character) → per-channel delay lines,
  `dL = d0·Size + m(t)`, `dR = d0·Size − m(t)`; m = **TRIANGLE** at `Rate` (0.25–0.5 Hz zone at
  knob center) with only a **1–2 ms apex smoothing** (the corner is a slope step = a tick; the
  flat-top "trapezoid" of the earlier draft is CUT — see §1.2-3: flat tops null the detune);
  depth = 14 %·Depth-law of d0 → de-emphasis (inverse shelves) →
  **cross-mix**: `wetL' = wetL − k·wetR`, `wetR' = wetR − k·wetL`, k = 0.35 + 0.35·Amount →
  dry low-shelf +2 dB @150 Hz compensation.
* 🔑 **THE COMPANDER IS THE ONE THRESHOLD IN THIS DEVICE — calibrate it to the bus (law 1).**
  The SDD-320's compressor exists to hide BBD noise, and its knee sat around a *hardware* line
  level. Copying any dBFS number from that world lands **26 dB wrong** on our bus. **State it
  relative to program:** compressor knee at **−12 dB relative to the −26 dBFS FX-bus program
  (≈ −38 dBFS absolute)**, ratio ≈2:1 with the expander exactly inverse, 5 ms / 80 ms.
  Anchor it to `kVoiceToFxPad` at build time (`knee = programRef · 10^(−12/20)`), never to a
  literal −38.0f, so it follows if the pad ever moves. If the compander is inaudible at
  defaults it is mis-calibrated, not "subtle" — harness it with the §8 dramaticism sweep.
* d0 = 5 ms nominal; `Delay` (P3) spans 1.5–20 ms. 4 lines total (2/ch, second pair at
  d0×1.6, LFO inverted) = Serum's "4 delay lines" density at Character `Quad` (default `Duo`
  = the SDD-320 2-line truth — audibly drier/rawer).
* **Slow amplitude modulation** (Serum's "slowly amplitude modulated"): ±1.5 dB sine at
  0.11·Rate on the wet pair, anti-phase — the breathing.
* **Characters:** `Duo 320` (default) · `Quad Expander` (Duda 4-line) · `Mode 1/2/3 fixed`
  (the VERIFIED Arturia ratios: 2 = delays ×0.5, 3 = depth ×2) as three chars ·
  `No Compander` (clean modern) · `Dark BBD` (recon LP 4 kHz, TerrainChorus.h:20-21 idiom) ·
  `Wobble` (triangle→sine + depth ×2.5 — a sine's slope goes to zero at the peaks, so the
  detune *breathes* instead of holding: deliberately breaks the motionless rule = the
  night-and-day proof char, and the A/B that proves why the original is a triangle).

#### 2.3 SHIFT
* **Engine:** per channel one granular shifter — 2 read taps 180° apart on a `W = 30 ms`
  window, triangular crossfade, read-rate ratio `ρ = 2^(±cents/1200)`; L gets +cents, R gets
  −cents. Plus per-side slap: L +8 ms, R +12.5 ms (unequal — H3000 recipe) with ±20 %
  SmoothRandom wander at 0.3 Hz (the "time-varying" MicroShift documents).
  ⚠️ **Budget the shifter's own delay:** a 2-tap granular shifter on a W = 30 ms window sits a
  mean W/2 = **15 ms** behind, *on top of* the slaps — so `H3K Silk` really lands at 23 / 27.5 ms
  L/R, and `AMS Punch` (W = 12 ms) at 14 / 18.5 ms. Subtract W/2 from the slap targets at build
  time or Shift drifts out of doubler range into slapback. NOT reported as latency (§3.9 law A).
* `Amount` → cents = `100·t^1.4` — 🔧 **CORRECTED (audit):** the earlier draft's "±50 = the Waves
  Doubler ceiling" is **factually wrong**. Waves' own user guide (verified, p.3) states
  **"DETUNE: −100 to +100 cents"** per voice, plus a separate **"MODULATION DEPTH: −200 to +200
  in cents"**. ±50 was therefore *playing safe against a misread source* (law: no playing safe).
  Ship **±100 cents**; honest doubler territory ends ~±25, ±25→±100 is the past-useful zone.
* **Characters:** `H3K Silk` (default, W = 30 ms) · `AMS Punch` (W = 12 ms — tighter, glitchier
  transients, "harder de-glitch") · `Tape Warble` (adds 0.8 Hz wander ×3) · `Fifth Up`
  (+700 cents on voice 2 at −15 dB — shimmer-adjacent) · `Down Double` (both sides −cents,
  L≠R magnitudes) · `Wide Slap` (slaps 18/29 ms) · `PV Glass` (**RECOMMEND CUT — below**) ·
  `Gritty` (linear-interp reads + W = 8 ms — intentional AM grit).
* 🚩 **`PV Glass` — the audit's verdict: CUT for v1.** Reusing `ShimmerPV` (verified in tree:
  ShimmerReverb.h:27 comment block, :360 member) drags two costs the earlier draft waved at:
  1. **LATENCY, and it is disqualifying.** `ShimmerPV` is `PN = 2048, PH = PN/4, PLAT = PN − PH
     = 1536 samples` = **32 ms @48 k** (its own header says "~43 ms" for the full window). In a
     shimmer *reverb* that hides inside a tail. In a **micro-pitch doubler it IS the effect**: a
     32 ms offset turns a ±10-cent double into a slapback, it cannot be compensated (rack law A,
     §3.9 — zero latency reporting anywhere in this rack), and it is 2–4× the whole slap budget.
  2. **CPU.** 2048-pt FFT pairs (analysis + synthesis) every 512 samples × 2 channels ≈ 1.7 k
     flop/sample ⇒ **~1–2 % of one M-series core — ORDER-OF-MAGNITUDE ESTIMATE, unverified,
     MEASURE before believing** — i.e. 3–5× the *entire rest of the device* (§8: ≤0.4 %) for one
     Character. The earlier draft's bare "+CPU" hid a 4× budget blow-out.
  If Max ships it anyway (§11 Q5) it must be re-pitched honestly as an **echo-double** Character
  with the 32 ms stated on the card — never sold as "clean micro-shift".

#### 2.4 ENSEMBLE
* **Engine:** one delay line per channel, 3 taps each; tap phases 0/120/240° on BOTH LFOs:
  slow = `0.75·Rate` Hz deep (±1.5–3 ms via Amount), fast = `6.1·Rate^0.5` Hz shallow (±0.35 ms);
  taps panned L/C/R (left ch) and R/C/L (right ch — mirrored). BBD darkening 1-pole LP 6 kHz
  in the wet. Sum /√3.
* **Characters:** `Solina` (default) · `Hexa` (6 taps, 60° spacing — denser wall) ·
  `Crumar Slow` (slow LFO 0.45 Hz, deeper) · `Bright String` (LP 9 kHz) · `Dark Organ`
  (LP 3.5 kHz + fast LFO off) · `Vibrato Wall` (fast LFO ×2 depth) · `Junk BBD` (adds
  compander + noise-free BBD droop tilt) · `Choir` (adds ±7 cents SmoothRandom per tap).

#### 2.5 DOUBLER
* **Engine:** V = min(Voices, 4) discrete voices; static delays {17, 29, 41, 53} ms × `Delay`
  scale; per-voice pitch = SmoothRandom walk, depth `±9·Amount^1.3` cents, walk bandwidth
  0.4–2.8 Hz by `Rate`; pans {−0.7, +0.7, −0.3, +0.3}·Spread; levels {0, −1.5, −3, −4.5} dB.
  **Waves Doubler grammar — VERIFIED from the user guide (p.3):** 2 *or* 4 voices; per voice
  **Gain · Pan (−45°…+45°) · Delay (0–100 ms, default 8 ms) · Detune (−100…+100 cents) ·
  Modulation Depth (−200…+200 cents) · Modulation Rate (0.1–200 Hz, default 1 Hz) ·
  Feedback (0–100 %, "be careful above 25 %") · Octaver (−1 oct switch)**. Two consequences:
  our walk ceiling (`Humanize Max` ±20 cents) is *timid* against their ±200 — push it to ±50;
  and their Octaver is the documented pedigree for §2.1's `Sub Anchor`/`Fifth Up`.
* **Characters:** `Vocal 2x` (default, 2 voices) · `Vocal 4x` · `ADT Tape` (walk ×2 +
  wow-shaped 0.6 Hz component — Abbey Road ADT lore) · `Tight Inst` (delays ×0.5) ·
  `Loose Crowd` (delays ×1.8, walk ×1.6) · `Detuned Twins` (walk replaced by static ±8 cents —
  crosses into Shift territory ON PURPOSE, but with echo) · `Slapback` (voice 1 at 84 ms,
  the Sun Records edge) · `Humanize Max` (walk **±50** cents — seasick past-useful top; ±20 was
  timid against Waves' own ±200 Mod Depth, above).

#### 2.6 MIRROR
* **Engine (mono-exact family, §1.6):** m = (L+R)/2, s0 = (L−R)/2;
  `a(m) = [AP1..AP6](m)` — 6 first-order allpasses, fc log-spaced 180 Hz–5.6 kHz, coefficients
  randomized per Character seed; generated side `s = Amount·(a(m) − m)·0.5`;
  out: `L = m + (s0 + s)`, `R = m − (s0 + s)`. **Mono sum = m exactly, at every knob position** —
  the by-construction guarantee.
  🔧 **CORRECTED (audit): Width is NOT applied here.** The earlier draft multiplied the side by
  `Width` in this formula *and* ran the universal §3.4 M/S rotation after it — the one hero knob
  applied twice (`Width²`), which is a dead first half of the knob (law 5) and a lie in the viz.
  The mono-exact frame emits `m ± (s0+s)`; **§3.4 owns Width, alone.** Same edit for §2.7 Bands.
  ⚠️ **Honest caveat on "mono-exact at every Width":** the §3.4 rotation scales mid by
  `√2·cos θ`, so the mono sum's *spectrum* is untouched at every Width (the guarantee that
  matters) but its *level* tracks the mid gain — mono sum is spectrally exact, not gain-exact.
  The §8 gate is a magnitude-spectrum ripple test after level normalisation; state it that way.
* `Rate` slowly drifts the allpass fcs ±20 % (0.02–0.4 Hz SmoothRandom) — the image *breathes*
  (law 5: no dead knob; at Rate 0 the drift freezes).
* **Characters:** `Smooth 6` (default) · `Deep 12` (12 APs — more decorrelation, more per-channel
  ripple) · `Dual Pass` (the DAFx-24 variant: L = APa(x), R = APb(x) — per-channel FLAT, mono
  ripple ≤ 2 dB — the documented trade, audibly "solider" sides) · `Velvet` (sparse 30-tap
  velvet-noise FIR side — DAFx-17; airier) · `Low Anchor` (APs start at 500 Hz — bass stays
  dead-center) · `Air Only` (APs 2–10 kHz) · `Seed B` / `Seed C` (different random phases —
  different rooms).

#### 2.7 BANDS
* **Engine:** same mono-exact frame, but `s = Amount·delay(m, τ)`, τ = 0.5–10 ms by `Delay`
  (P3). L gets comb peaks exactly where R has notches (complementary) — the hard L/R spectral
  SPLIT. `Rate` glides τ ±12 % slowly = the combs sweep = spectral rotation.
* At Amount = 1 the channels are fully complementary (each ±∞ dB comb) — the past-useful "two
  different signals" extreme; the useful zone is 0.2–0.6. Movement is the magic: default
  Rate > 0.
* **Characters:** `Coarse` (τ = 6 ms — wide bands, default) · `Fine` (τ = 1.2 ms) · `Tilted`
  (adds ±2 dB L/R tilt pair) · `Rotor Slow`/`Rotor Fast` (Rate law ×0.3/×4 — the rotating-
  speaker illusion without Doppler) · `Notch Guard` (comb depth capped 0.7 — polite) ·
  `Octaver Comb` (τ pitch-tracked to the last note via setKeyHz, fb336 precedent — combs land
  ON harmonics) · `Split Duo` (adds a 2nd τ×1.618 comb pair).

---

## 3. DSP core — algorithms, laws, stability, oversampling verdict

### 3.1 🔑 THE CONSTANT-CENTS LAW (the single most important equation in this device)

A delay line modulated by `d(t) = A·sin(2πft)` pitch-shifts by the slope:
`ratio(t) = 1 − d'(t) = 1 − 2πfA·cos(2πft)`. Peak cents = `1200·log2(1 + 2πfA)`.
**Serum exposes A (Detune) and f (Rate) raw** — turn Rate up and the same Detune doubles its
cents; the knob lies. **Our law: the knob IS cents.** Solve for depth per voice:
```
A_v = (2^(c_v/1200) − 1) / (2π f_v)          // c_v = fan-scaled target cents
```
🔧 **CORRECTED (audit) — the clamp must be PER-VOICE against its own base, not a flat 30 ms.**
The earlier draft's `A_v ≤ A_max = 30 ms` is unsafe: the read offset is `base_v + A_v·mod`, and
the tightest voice's base is 9.7 ms, so a 30 ms depth drives the read head to **−20 ms** —
past the write pointer. `clamp(…, 1, max)` then flattens the LFO troughs, which (a) breaks the
constant-cents law it was written to serve, (b) hard-clips the modulator = a buzz, and (c) makes
Detune stop evolving well before 100 (law 5). **The law:**
```
A_v ≤ 0.9 · base_v                      // the read head never approaches the write head
c_max(f, v) = 1200·log2(1 + 2π f · 0.9·base_v)     // the honest reachable cents, per voice
```
Consequences to design around, not hide: the full **±120 cents is reachable only above ≈1.3 Hz**
on the 9.7 ms voice (`0.9·9.7 ms`, solving the above); `Tight Digital` (bases 4–9 ms) needs
≈3.2 Hz; `Wide Fan` (15–45 ms) reaches it by ≈0.85 Hz. Below that the display shows the achieved
cents falling — a display law, not a lie — and **the Character's base scatter is therefore a
first-class part of its voicing, not cosmetics.** Still a straight "beat Serum" measurable:
sweep Rate at fixed Detune above the per-Character floor → our sideband spread stays put,
theirs walks.

**Base-delay scatter:** voice bases in ms `{0, 9.7, 13.1, 17.3, 21.9, 11.3, 15.7, 24.1}` —
pairwise non-multiple (comb law: DelayEngine.h chose its allpass lengths "mutually non-multiple"
for the same reason — **verified at DelayEngine.h:45-47**, `apMs[NAP] = {4.77, 3.59, 12.7, 9.31}`;
the earlier draft's ":46-48" was off by two lines).

### 3.2 The shared voice engine (one code path, 7 Types)

All 7 Types are configurations of ONE `WidenEngine` (recycle grammar = DelayEngine.h:33's
power-of-two masked buffer + per-sample smoothers + hermite reads):
```
for each channel c:  ring buffer B_c (2^n ≥ 0.25 s)
for each voice v:    d_v(t) = clamp(base_v·delayScale + A_v·mod_v(t), 1, max)
                     y_v = hermite4(B_c, wr − d_v)          // DelayEngine.h:240 idiom
                     out += gain_v · pan_v · y_v
mono-exact Types (Mirror/Bands) bypass the voice loop and run the m/s frame §2.6/§2.7
```
`mod_v` = sine LFO (Stack), **triangle** (Dimension — §1.2-3), 3-phase pair (Ensemble), SmoothRandom walk
(Doubler), const-ratio granular taps (Shift). One switch, one buffer, one read path.

### 3.3 Summing + unity law (bus reality, law 1)

N incoherent voices sum to ~√N power. Normalize: `g_norm = 1/√(center² + Σ sides²)` with
center/sides from the §1.1 Balance law — **computed at control rate, glided 20 ms** (a
program-independent normalizer — the Tape lesson: never normalize by program level,
DISTORTION-BUILD-BIBLE §9.1). Result: Mix 100 %, Amount anywhere, Voices anywhere ⇒ wet RMS
within ±1 dB of dry **on broadband program** (harness gate §8).
⚠️ **Stated limit, don't let it surprise us:** the √-power law assumes the voices are mutually
INCOHERENT. At Detune → 0 on a *sustained sine* they are coherent copies at fixed base offsets,
so the sum tends toward `Σg` rather than `√Σg²` — worst case `√N` = **+9 dB at 8 voices**. That
is why the §8 unity gate is specified on the harness CHORD, and why the fb264 master limiter is
the backstop. Do not "fix" it with a program-tracking normalizer (that is the Tape trap).
**Bus reality (house law 1):** the FX bus sits ≈ **−26 dBFS** (`kVoiceToFxPad = 0.5f`, verified
PluginProcessor.cpp:6300-6301; program measured −26 dBFS, DISTORTION-BUILD-BIBLE §2.1). This
device declares **no thresholds, no drive, no compressor-class controls anywhere**, so there is
nothing in it to mis-calibrate against the bus — the *only* level law it owns is this normalizer
+ the §6 unity gate, both defined relative to that −26 dBFS program. If any future Character
adds a threshold (a compander gate, a duck), it must be re-stated as "x dB **below −26 dBFS
program**", never as an absolute dBFS number copied from hardware.

### 3.4 Width stage (universal, post-everything)

Equal-power M/S rotation, NOT naked side gain:
`M' = √2·cos(θ)·M , S' = √2·sin(θ)·S`.
🔧 **CORRECTED (audit) — the earlier draft's `θ = Width·π/4` does not do what it claimed.**
At Width 0.5 it gives θ = 22.5° ⇒ M' = ×1.307 (**+2.3 dB mid**), S' = ×0.541 (**−5.3 dB side**)
— i.e. "0.5 = untouched" was **narrower and louder than dry**, the exact opposite of neutral;
and its "1.0 = +3 dB side / −3 dB mid" is self-contradictory (+3 dB side requires sin θ = 1,
which puts mid at −∞, not −3 dB). **The correct law:**
```
θ = Width · π/2
Width 0.0 → θ = 0°   : S' = 0,      M' = √2·M   → MONO (the +3 dB is the equal-power pan law)
Width 0.5 → θ = 45°  : S' = S,      M' = M      → EXACTLY NEUTRAL (unity, bit-transparent)
Width 1.0 → θ = 90°  : S' = √2·S,   M' = 0      → SIDE ONLY (+3 dB side, mid gone) = past-useful
```
Neutral therefore sits at the knob centre (the house default-0.5 rule) and both ends are real
destinations, no plateau. Keeps power constant across the rotation (naked `S·2` boosts loud
programs +4–6 dB = a fake-drama violation). Width lives OUTSIDE the feedback loop (§3.5), and
it is the **only** place Width is applied (§2.6 correction) — after the mono-exact construction,
so Mirror/Bands keep a spectrally-identical mono sum at every Width.

### 3.5 Feedback (P7) — loop-gain law (house law 6)

Wet block output recirculates into the voice-block input:
```
in'(n) = in(n) + fb · env(n) · softclip( wetSum(n−1) )        // env multiplies the RECIRCULATED term
```
**The env multiplies the recirculated signal, not just the fresh input** — that is the whole
mechanism by which the bloom dies (the earlier draft's "loop input is multiplied by the env" was
ambiguous enough to be built the wrong way, which free-runs).
**Loop-gain accounting**, every stage counted: voice sum (normalised to 1.0 by §3.3, knob-only)
× in-loop damping LP (|H| ≤ 1) × `fb` × `env` (≤ 1). ⇒ **max loop gain = fb; cap fb = 0.90**,
tanh softclip in the loop (DelayEngine's fb303 bounded-feedback pattern), 6 dB/oct damping at
7 kHz so the bloom darkens as it regenerates (Choral's "Scatter" documented this feedback-chorus
= reverb-adjacent bloom).
**Tail length, stated (was hand-waved):** one loop trip ≈ the mean base delay ≈ 17 ms, and
20·log10(0.9) = −0.915 dB per trip ⇒ **RT60 ≈ 17 ms × 65.6 ≈ 1.1 s while a note is HELD**
(shorter above 7 kHz because of the damping). §6's "RT ~0.4 s" was the *released* figure — after
note-off the env's 150 ms squared release drives `fb·env` down, so the audible tail is
≈0.3–0.4 s. Both numbers are true of different states; §6 now says which.
**Env-gated:** device input-follower, 20 ms attack / 150 ms release, squared release (the
Phase-G law). Nothing free-runs.

### 3.6 Param laws table (range · taper · glide)

| Param | Range | Taper | Glide |
|---|---|---|---|
| Amount | 0..1 → per-Type law (§2) | Stack: szabo11 · Shift: t^1.4 · others t^1.2 | 20 ms |
| Rate | 0.02–12 Hz | log | phase-continuous (NEVER reset phase on rate change — one-clock law, Phase G) |
| Width | 0..1 (θ law §3.4) | linear | 15 ms |
| Mix | 0..1 equal-power | sin/cos | 15 ms |
| Voices | 1–8 stepped | — | new voices fade IN 30 ms; removed voices fade OUT 30 ms then stop reading (§9.4) |
| Spread | 0..1 pan fan | linear | 20 ms |
| Delay | 0.25×–2.5× base scale | log | **delay-length GLIDE 30 ms** (comb-click law — never snap a read head) |
| Wander | 0..1 | t^1.5 | 20 ms |
| Low Keep | 0 (off)–500 Hz | log | coefficient glide 20 ms |
| Tone | 0..1 bipolar tilt (0.5 neutral) | linear | 15 ms |
| Feedback | 0..0.90 | t^1.2 | 25 ms |
| Balance | 0..1 (§1.1 law) | measured curves | 20 ms |

### 3.7 Oversampling verdict: **NONE — anywhere, ever**

Every path is linear-time-variant (delays, allpasses, gains) — LTV systems create sidebands,
not harmonic aliasing stacks; the only nonlinearity is the loop tanh at |x| ≤ 1 territory on a
−26 dBFS bus (negligible harmonics, and inside a dark loop). The REAL quality axis is
**interpolation**: hermite4 everywhere (already "bright without phasey dispersion",
DelayEngine.h:23-25). No Quality dropdown needed — that back-panel slot is freed for real
params. CPU law satisfied by construction. (If `PV Glass` ships, the PV runs at 1× too.)

### 3.8 DC / denormal traps

* Feedback loop: DC blocker 10 Hz inside the loop (asymmetric softclip + recirculation =
  slow DC latch — the fb345 silence-class lesson generalizes: ANY loop gets AC-coupled).
* SmoothRandom walks are zero-mean by construction (leaky integrator, λ = 0.9995).
* Flush: all shelves/LPs + compander envelopes on reset; `ScopedNoDenormals` already wraps the
  block. Allpass cascades denormal-prone at silence → add ±1e-20 dither DC to the cascade input
  (the standard JUCE trick) or rely on FTZ — state: rely on ScopedNoDenormals (verified
  in-tree), flush on reset() only.

### 3.9 🚨 RACK-WIDE LAWS A–D (added by the 2026-08-14 audit — the earlier draft stated none of them)

These are not this device's laws; they are the *rack's*, and three of the four bite here.

**A. ZERO LOOKAHEAD, ZERO REPORTED LATENCY — anywhere in the rack.** The fb305/fb338 main-send
exclusion sums subtract the routed dry **sample-aligned** (verified: `rtdL = (rvbSendL + dlySendL
+ dstSendL)·outputGain·kVoiceToFxPad`, PluginProcessor.cpp:7159). Any device that reports latency
makes the host delay-compensate the wet while the exclusion still subtracts an *undelayed* dry —
the dry leaks back, phase-smeared. Therefore: **no lookahead limiting, no linear-phase anything,
no latency-compensating "align the wet" option, `getLatencySamples()` stays 0.** Practical hits
here: (i) pitfall 13 (base delays are the effect, never compensated) ✅ already right;
(ii) `PV Glass`'s 32 ms is *uncompensatable*, which is half of why §2.3 recommends cutting it;
(iii) Shift's granular W/2 must be budgeted into the slap targets (§2.3), never reported.

**B. NO RUNTIME PARAMETER CREATION.** JUCE/VST3/AU cache the parameter list at construction; a
param added later is invisible to the host and silently unreadable. Everything Widen needs —
all 12 sliders, both choices, all 9 bools — is declared in `createParameterLayout()` at birth.
Per-Type differences are **relabels of a fixed slot pool** (Model A), never new params. If a
future Type wants a 9th back knob, it does not get one.

**C. CHOICE-PARAM CARDINALITY IS FIXED AT BIRTH (fb342) — and this is the device's biggest
build-order landmine.**
* `SYN_WID_TYPE` must be created at its **FINAL** roster size on day one. §2 ships 7 and CUTS
  `Rotor`. **Declare choice(8) with slot 7 reserved and disabled** ("— reserved —"), or Rotor can
  never be added without breaking every saved preset's normalised type index.
* `SYN_WID_CHARACTER` is **choice(8), always**, even for a Type whose list is shorter — the
  distortion precedent is verified in the UI: `Math.min(o1.length-1, Math.round(v*7))`
  (index.html, `fxrRestoreDistortion`), i.e. read back on the *choice(8)* scale and clamp to the
  Type's list. This directly answers §11 Q4: trimming to 6 per Type is a **UI-list** decision,
  never a cardinality decision.
* 🔴 **`SYN_FX_ORDER` CANNOT GROW IN PLACE, AND IT BLOCKS THE 4TH DEVICE.** Verified: it is an
  `AudioParameterChoice` with exactly **6** entries — the 3! permutations of Reverb/Delay/
  Distortion (PluginProcessor.cpp:3488-3495, read as an index and clamped `jlimit(0,5,…)` at
  :5860, dispatched by `switch (fxPerm_)` at :7383). The WebView mirrors the same 6-row `PERMS`
  table and **bails out unless it finds exactly 3 devices** (`if (slots.length !== 3 …) return;`,
  index.html `fxrRestoreOrder`), and writes it as `pi/5`. Four devices need **4! = 24**
  permutations. Cardinality is fixed at birth ⇒ **do not resize SYN_FX_ORDER.** Two legal paths,
  pick one before writing a line of DSP (§11 Q9):
  1. **New param `SYN_FX_ORDER4`, choice(24)**, ordered so index 0 = `Reverb > Delay >
     Distortion > Widen`; keep `SYN_FX_ORDER` alive as a read-only legacy input and migrate old
     sessions once on load (map its 6 orders into the 24-list with Widen appended last).
     Also: new 24-row `PERMS`, and the `slots.length !== 3` guard becomes `!== 4`.
  2. **Pin Widen's position** (always last in the serial chain, un-draggable for v1). Zero
     param churn, zero migration — but it breaks the rack's drag-to-reorder promise for one
     device, which Max will notice. Recommend path 1; path 2 only if v1 must ship this week.

**D. A NEW SEND BUS MUST JOIN EVERY EXCLUSION SUM.** See §4's landmine block — corrected there
to the real **six** edit sites.

---

## 4. Chassis map — the fb275 device, 11 params

**Front card** (3 hero + Mix + pills + live viz, §5):

| Slot | Name | Maps to |
|---|---|---|
| Hero 1 | **`Amount`** (relabel per Type — Model A, the SIG-knob precedent ParameterIDs.hpp:410): Stack `Detune` · Dimension `Depth` · Shift `Shift` · Ensemble `Depth` · Doubler `Drift` · Mirror `Scatter` · Bands `Split` | §3.6 Amount |
| Hero 2 | **`Width`** | §3.4 rotation |
| Hero 3 | **`Rate`** | §3.6 Rate |
| 4th | **`Mix`** | equal-power, 100 % = FULLY WET (engine owns Mix — the fb318 law: wet/dry latency-aligned inside the engine) |
| Pill 1 | **`Retrig`** | §2.1 note-on phase glide (uses the last-note feed — setKeyHz/glide-tracker precedent, PluginProcessor.cpp:7289) |
| Pill 2 | **`Hear Mono`** | auditions (L+R)/2 on both outs — the mono-integrity check ON the card; UI-momentary, 15 ms fade both ways |
| Power | standard | default OFF (distortion precedent, ParameterIDs.hpp:428) |

**Back panel** — dropdown 1 `Type` (7), dropdown 2 `Character` (8 per Type), 8 knobs 4×2:

| P | Name | What it does (one honest clause) |
|---|---|---|
| P1 | **`Voices`** | how many copies (1–8; Dimension: 2/4 lines; Mirror: AP stages 4/6/8/12; Bands: comb pairs 1–3) |
| P2 | **`Spread`** | fans the copies across the stereo field |
| P3 | **`Delay`** | how far behind the copies sit (scales base delays; Dimension `Size` lives here) |
| P4 | **`Wander`** | humanizes: random drift on every copy's time/pitch |
| P5 | **`Low Keep`** | keeps everything below this frequency mono and centered (0 = off → 500 Hz; elliptical-filter style S-highpass) |
| P6 | **`Tone`** | darkens/brightens the wet only (tilt, 0.5 neutral — the MicroShift Focus job) |
| P7 | **`Feedback`** | regenerates the copies into a bloom (env-gated, ≤0.90) |
| P8 | **`Balance`** | dry-center voice vs the copies (the measured JP-8000 mix law §1.1) |

Every knob does something real in every Type (law 5): where a literal reading is meaningless
the Type re-purposes it along the same *idea* (P1 Voices → Mirror stages; P3 Delay → Bands comb
τ) — relabeled per Type in the UI exactly like the distortion back-8 relabel (Model A).

**Param IDs:** `SYN_WID_TYPE / _CHARACTER / _AMOUNT / _WIDTH / _RATE / _MIX / _P1.._P8 /
_SRC_A.._SRC_NOISE / _POWER / _RETRIG / _MONO` — clone the SYN_DST block grammar (verified,
ParameterIDs.hpp:406-431). **23 params: 12 float + 2 choice + 9 bool.** Counts: "11" is the
house chassis count (3 heroes + 8 back knobs); Mix, the 2 dropdowns, the 2 pills, Power and the
6 route bools are the universal furniture every device carries on top.
`AudioParameterChoice` reads = **INDEX direct** (`(int)*rawParam(id)` — the CLAUDE.md §4 law,
and the fb50 bug that proves it). Cardinality is fixed at birth — see §3.9-C: **TYPE = choice(8)
with slot 7 reserved/disabled, CHARACTER = choice(8) always.** `_MIX` default 0.5, `_POWER`
default **OFF** (dry init, distortion precedent ParameterIDs.hpp:428), routes default OFF.

**Routing:** per-osc pills + main-send, inherited. ⚠️ **THE LANDMINE (fb305/fb338) — recounted
by the audit: it is SIX edit sites, not three.** Each of the three exclusion blocks has an **L
line and an R line**, and only the L line carries the `fb305 law` comment. Verified at HEAD:

| Block | L site | R site |
|---|---|---|
| Reverb main-send | PluginProcessor.cpp:**7159** | :**7161** |
| Distortion main-send (`applyDst`) | :**7326** | :**7328** |
| Delay main-send | :**7358** | :**7360** |

Each of the six gains a `+ widSendL/R` term, **AND** the new `applyWid` lambda subtracts all
four buses in its own else-branch. Miss one ⇒ a routed osc leaks dry through another device's
main send (and an L-only fix produces the nastiest version: a *stereo-asymmetric* leak).
📌 **These line numbers drift — the file is 9916 lines and grows every session. Do not trust
them; grep the marker.** For reference, the memory index's older ":6979/:7111" refs are already
stale by ~180–250 lines, and they are *not* index.html line numbers (index.html:6979 is the fb120
robin SVG, :7111 is the ribbon-row CSS — the exclusion sums have never lived in the HTML).

**Insert pattern:** copy `applyDst` (PluginProcessor.cpp:7310-7344): env-gated replace,
`leftChannel[i] += e·(wl − sgL)`, engine returns finished (Mix-applied) signal, contributes
EXACTLY 0 at env 0. Type swap = the delay's dip-swap (dlyEnv → 0, swap+reset, recover —
PluginProcessor.cpp:7207) plus DistortionEngine's 40 ms output-crossfade idiom for Character
swaps (DistortionEngine.h:154, chrPend_ deferred-swap :2912).

---

## 5. Visualizers — how the greats show it, then OUR card

### 5.1 The survey (mechanisms, precisely)

* **Serum 2:** every FX module = its param strip + a graphical display with **direct
  manipulation** (drag IN the graphic). Hyper/Dimension's graphic is modest — the device
  reads as two slider groups; no dramatic scope. (Bar to clear, not to copy.)
* **iZotope Ozone Imager:** the reference visual grammar for width: **Polar Sample
  vectorscope** (per-sample dots on polar axes; inside ±45° = in-phase, outside = anti-phase;
  slow fade history) and **Polar Level** (rays whose length = amplitude, angle = stereo
  position, shrinking history), plus a **correlation bar meter** beside it. Instantly reads
  "how wide am I, and am I mono-safe".
* **Waves Doubler:** a 2-D voice grid — each voice a draggable node positioned by pan (x) and
  its gain/delay — the "voices are objects" idea.
* **Polyverse Wider:** one huge Width knob + a simple mirrored level meter — drama through
  minimalism, no analysis.
* **Arturia DIMENSION-D:** photoreal hardware — 4 buttons, zero metering (period-correct,
  useless for us).
* **Kilohearts Ensemble:** static graphic + knobs; motion select as icons. Nothing live.

### 5.2 OUR card — 3 concepts (canvas, CPU-cheap, boldly audio-reactive, param-reflecting)

1. **⭐ THE VOICE FAN (recommended).** A stage-like arc: the dry center a bright vertical
   beam; each active voice a beam fanned left/right by its CURRENT pan (Spread moves them),
   leaning by its CURRENT pitch offset (each beam tip sways with its actual LFO/walk phase —
   engine ships per-voice `d'(t)` at 30 Hz), thickness = voice gain (Balance visibly starves
   the center), color-heat = Amount. Feedback draws fading ghost-beams behind each voice.
   Idle = dim ember beams barely breathing; note-on = full brightness riding the input env
   (idle/bright delta = the fb311 hard rule). Retrig visibly snaps the fan closed then blooms
   it open. Cost: ≤ 9 beams × 2 quads, one glow pass, no per-frame shadowBlur (fb342 law).
   Every one of the 11 params changes the picture. **This is the one no competitor has.**
2. **THE WIDTH ARC + CORRELATION NEEDLE.** Bottom strip under the fan (or standalone): a
   polar wedge sweeping ±45°, filled by recent L/R sample dots, plus a needle at the live
   correlation `r = Σ L·R / √(ΣL²·ΣR²)`; needle green r > 0.3, amber 0..0.3, red < 0 (mono
   danger, tied to the §8 spec). `Hear Mono` pill collapses the wedge to a single beam — the
   visual states the audio. Cheap: dots into an offscreen with 0.92 fade multiply.
   🔧 **CORRECTED (audit) — there is NO reusable stereo scope tap; the earlier draft named two
   things that cannot do this job:**
   * `RollingCaptureBuffer.h` is **not a scope tap.** It is the always-on **10-minute export
     capture** (`MAX_CAPTURE_SECONDS = 600.0`), read one-shot from the message thread via
     `copyForExport` — verified RollingCaptureBuffer.h:19-35. Wrong tool entirely.
   * The real scope, `scopeBuffer`, is **MONO and 256 samples**, taken at the **master output
     post-limiter**: `SCOPE_SIZE = 256` / `std::array<std::atomic<float>, SCOPE_SIZE>`
     (PluginProcessor.h:1125-1126), written as `(L+R)*0.5f` at PluginProcessor.cpp:7428-7429
     under the comment *"Write to scope buffer (mono mix for visualization)"*. A mono tap can
     never render a vectorscope or a correlation needle, and a master tap would show the whole
     mix, not Widen's wet.
   * **So Widen publishes its own.** Follow the per-device viz precedent that IS in tree: the
     distortion's `dstBlockWetPk → dstBloomEnv_` block-rate atomics (PluginProcessor.cpp:7341,
     :7405-7406, fb315) and the `oscScope` **seqlock** ring pattern (PluginProcessor.h:855-865:
     `oscScopeSeq` odd = write in progress, even = complete; the editor's 60 Hz timer reads the
     seq before and after and retries on mismatch). Widen's version: one **interleaved L/R ring
     of 128 pairs** + published `r`, `sideRms/midRms`, and per-voice `d'(t)` — all written from
     the audio thread inside `applyWid`, ~60 Hz, seqlock-guarded. Budget this as real work; it
     is not a recycle.
3. **THE TWIN RIBBON.** L and R wet waveforms as two horizontal ribbons that physically
   separate (vertical gap = 1 − r, live) and tint apart as Width/Amount rise; braided into one
   rope in mono. Dramatic and literal, but shows less per-param detail than 1+2.

**Ship 1 + 2 fused** (fan above, wedge+needle strip below): the fan shows the *mechanism*,
the wedge shows the *result*, the needle shows the *safety* — everything audible is visible.

---

## 6. Interplay — the device in a chain

* **Unity-through:** defaults (Amount 0.35, Width 0.5, Mix 0.5, Balance 0.4, normalizer §3.3)
  pass program within ±1 dB RMS and identical crest ±0.5 dB — harness-gated (§8). A widener
  that pumps level is lying about width (loudness bias).
* **Ordering wisdom (the classics):** thicken → space: Widen sits AFTER distortion/EQ and
  BEFORE (or instead of) reverb. Chorus-class effects into reverb = lush; reverb into Widen =
  the whole tail wobbles (sometimes wanted: `Dimension` after Hall = the 80s wash). Widen LAST
  = maximal image control (imager position).
* **What breaks, named:**
  - **Widen → Distortion:** any nonlinearity intermodulates the detuned voices (Δf beats
    become IMD grit) and partially RE-correlates the channels (waveshaping compresses the side
    differences) — width collapses and roughens. Legit as an effect; default order avoids it.
  - **Widen → mono-summing device:** anything that mids the signal downstream (a mono utility,
    a mono-input reverb char) folds Stack/Doubler/Dimension combs back in — audible 1–3 dB
    ripple. Mirror/Bands survive by construction.
  - **Two wideners stacked:** correlation over-rotation — r driven < 0, phasey/hollow; the
    needle (§5.2-2) is the guardrail. Document, don't prevent.
  - **Widen → Delay with cross-feedback:** L/R decorrelated content ping-pongs into a wash;
    loop gain unaffected (our width stage is outside our loop; their loop is theirs).
* **Downstream spectrum/dynamics:** wet adds dense sidebands ±cents around every partial
  (Stack/Ensemble), static mirrored offsets (Shift), or pure-S energy (Mirror/Bands); crest
  factor barely moves (all-LTV); long Feedback blooms raise the RMS tail — **RT60 ≈ 1.1 s while
  a note is HELD, ≈0.3–0.4 s after note-off** (both derived in §3.5; the earlier draft quoted
  only the released figure as if it were the whole story) —
  downstream compressors will breathe on it (state it in the manual).

---

## 7. Presets — 12 factory sketches

| # | Name | Type/Char | Intent | Rough values (Amount/Width/Rate/Mix · back) |
|---|---|---|---|---|
| 1 | `Init Wide` | Stack/JP Classic | honest default | .35/.5/.25/.5 · V4 Sp.6 Dl.5 Wn0 LK120 Tn.5 Fb0 Bal.4 |
| 2 | `Super Stack` | Stack/JP Classic | the supersaw wall | .6/.7/.3/.8 · V8 Sp.85 Dl.5 Wn.1 LK150 Tn.55 Fb.15 Bal.75 |
| 3 | `Laser Zap` | Stack/Laser | Retrig demo, mono-ish | .8/.4/.7/1.0 · V6 Sp.3 + Retrig ON |
| 4 | `Dimension Pad` | Dimension/Duo 320 | motionless 80s width | .5/.75/.3/.45 · V2 Dl.5 LK100 |
| 5 | `Quad Expanse` | Dimension/Quad | the Duda four-liner | .65/.85/.35/.6 · V4 Dl.65 |
| 6 | `Vocal Silk` | Shift/H3K Silk | classic ±9-cent double | .3/.6/.2/.5 · Dl.4 Tn.6 LK200 |
| 7 | `String Machine` | Ensemble/Solina | the Solina wall | .55/.65/.5/.7 · V6 Sp.8 Tn.45 |
| 8 | `Tape ADT` | Doubler/ADT Tape | Lennon double | .45/.55/.4/.55 · V2 Dl.6 Wn.5 |
| 9 | `Mono-Safe Sheen` | Mirror/Smooth 6 | width that folds flat | .6/.8/.15/.65 · V6 LK0 |
| 10 | `Rotor Split` | Bands/Rotor Fast | rotating spectral L/R | .5/.7/.8/.6 · Dl.35 |
| 11 | `Ghost Choir` | Ensemble/Choir + Fb | env-gated bloom | .5/.7/.35/.75 · Fb.8 Tn.35 |
| 12 | `Total Smear` | Stack/Wide Fan | the past-useful flag | 1.0/1.0/.9/1.0 · V8 Wn.9 Fb.85 Bal1 |

Preset LEVEL spread gate (the fb345 lesson): all 12 within ±3 dB RMS of `Init Wide` on the
harness chord before shipping.

---

## 8. CPU + the perceptual/mono harness

**Budget (per instance, one core @ 48 k / M-series):** voice loop = 1 hermite read (4 taps) +
LFO + 2 mults per voice per channel; 8 voices ≈ 16 reads/sample stereo ≈ the Diffuse delay path
(shipped fine). Estimate **≤ 0.4 %** worst-Type (Stack 8v + Feedback); Mirror ≈ 12 first-order
APs = trivial; Shift = 4 reads + 2 crossfades + the granular crossfade.
⚠️ **These are ESTIMATES, not measurements — nothing here has been profiled. Gate the build on
a real `wid_cpu` run** (the `dst_cpu` harness grammar), and hold the whole device under the
spring-reverb reference point that fb342 actually measured: 6 springs went 12.5 % → 2.6 % of a
core. **The one outlier is `PV Glass` at ~1–2 % (§2.3) — 3–5× everything else, for one
Character. That asymmetry is the argument for cutting it.** No oversampling (§3.7).
**Oversampling / Quality tiers, explicit:** there is **no Quality dropdown and no oversampled
path at any tier** — 1× everywhere, in every Type, in every Character, including the loop tanh
(justified in §3.7 by the all-LTV argument on a −26 dBFS bus). If a future Character adds a real
nonlinearity, it needs its own oversampling verdict before it ships. **Sleep law:** the
fb342 awake-head pattern — input env < −80 dBFS for 0.5 s AND Feedback tail dead ⇒ skip the
block (device passthrough, envs frozen). Card viz throttles to 30 Hz and fully sleeps when idle
(no per-frame filters/shadowBlur — session law ⑤).

**Harness (offline, the dst_cert grammar — compiles `clang++ -O2 -I shim -I Source`):**
* **Dramaticism:** per knob 0→100 sweep on the standard chord: magnitude-spectrum delta,
  spectral flux, side/mid ratio, sideband count — every knob must move its metric monotonic-ish
  with NO plateau (law 5), Amount@100 past-useful confirmed by ear.
* **Type discriminators (§2 table):** 7×7 confusion — every pair separated by its stated
  metric ≥ 6 dB / ≥ 2× — or the Type is cut (the near-twin rule that flagged D1 Si/Ge).
* **🔑 MONO-COMPAT GATES (the make-or-break spec), per technique:**
  | Type | Test | Gate |
  |---|---|---|
  | Mirror/Bands | mag-spectrum of (L+R)/2 vs dry, **level-normalised first**, any knob position | ripple < **0.1 dB** (construction proof). Normalise because §3.4's mid gain `√2·cos θ` legitimately changes the mono LEVEL with Width — the guarantee is spectral, not gain (§2.6) |
  | Dimension | same, Mix 50 % | < **2 dB** ripple, no notch > 3 dB |
  | Stack/Ensemble | 400 ms windows over 8 s | worst window < 3 dB, 8-s AVERAGE < 1 dB (moving combs must average out) |
  | Shift | spectrogram of mono sum | NO static notch (any notch must move > 1 Hz/s) |
  | Doubler | cepstrum of mono sum | comb peak < 25 % of dry-spike; wander decorrelates the rest |
  | ALL | correlation r at defaults | 0.2 ≤ r ≤ 0.8 (wide but not phase-broken) |
* **Unity gate (§6):** defaults within ±1 dB RMS; every preset within ±3 dB of Init.
* **Click gates:** Type swap, Character swap, Voices step, Retrig, Power toggle, Delay sweep —
  per-char honest click floors (Phase-G probe-craft: AM-probe transient params, silence-metric
  the gated paths).

---

## 9. Pitfalls — collected

1. **Retrig = a delay JUMP.** Resetting LFO phase teleports the read head → click. Glide the
   phase to 0 over 8 ms (§2.1); never write the head.
2. **LFO phase clustering.** Random phases can land aligned → periodic flange instead of a
   crowd. Enforce stratified phases (φ_v = v/N + jitter·0.3/N) at voice alloc.
3. **Program-dependent normalizer** flattens the very level life you built (Tape lesson).
   §3.3's normalizer reads KNOBS only.
4. **Voices step pops.** Fade voices in/out 30 ms; a removed voice finishes its fade BEFORE
   its buffer read stops (§3.6).
5. **Delay-length snaps** = comb clicks (the house comb-click law) — every ms-domain change
   glides ≥ 30 ms, incl. Character swaps that move bases (dip-swap them instead).
6. **Rate change resets phase** → chirp (the one-clock law, Phase G: phase accumulators
   integrate; change the increment, never the accumulator).
7. **Feedback DC latch** — AC-couple the loop (§3.8; the fb345 silence-class generalization).
8. **Mono-sum collapse** shipping unmeasured — the §8 gates exist because Haas-style widening
   (Ozone Stereoize I lineage, our Bands at Rate 0) combs at exactly `n/τ` Hz in mono; τ = 6 ms
   ⇒ notches at 83/250/417 Hz — audible bass holes. Low Keep ≥ 120 Hz default guards the worst.
9. **Width before feedback** would multiply loop gain by the side boost — Width stays outside
   the loop (§3.5).
10. **Denormals in idle allpass cascades** (Mirror at silence) — covered §3.8; verify in the
    sleep test.
11. **The 4th-bus exclusion landmine** — **six** lines + the new lambda (§4). 🔧 The earlier
    draft's check ("grep `fb305 law` and count FOUR") is **wrong twice**: that marker appears on
    only the three **L** lines (the R lines carry no comment), and adding a bus does not add a
    comment, so the count stays 3 whatever you do. **The real check:**
    `grep -c 'widSendL ?' PluginProcessor.cpp` → **3** AND `grep -c 'widSendR ?'` → **3**,
    then confirm each of the six sits in the same expression as `rvbSendL/R`, `dlySendL/R`,
    `dstSendL/R`. Six or it is broken.
12. 🔧 **CORRECTED: FX-rack params do NOT use WebSliderRelay — the "15 relays" line is wrong and
    would send the builder down a dead path.** Verified at PluginEditor.cpp:756-780: the rack
    writes through the generic **`setSynParam`** native, a **DIRECT APVTS write** whose own
    comment says it *"bypasses the WebSliderRelay. At this plugin's scale (700+ relays) the
    MODAL engine's relays silently failed to reach the APVTS"* — and reads back through
    `getSynParam` (normalised 0..1) *"WITHOUT a WebSliderRelay (dodges the relay-scale bug)"*.
    There are **zero** `SYN_DST_*` relays in PluginEditor.\*, and Widen gets zero too.
    **The real 3-point contract, and the real silent-no-op traps:**
    (1) declare every `SYN_WID_*` in `createParameterLayout()`;
    (2) put the exact ID string in the `DEVS` entry (`p:'SYN_WID_…'`, `tp`, `pwrP`, `pp`, `rp`)
        — `getParameter()` returns **nullptr on a typo and the write is silently dropped**,
        no error anywhere: a mistyped ID is the whole failure mode here;
    (3) write `fxrRestoreWiden()` in the `fxrRestoreDistortion` shape — **omit it and the device
        shows defaults on reopen while the DSP holds the restored state** (the fb294 /
        state-persists law). Remember choices go over the wire **normalised**: write
        `idx/(N−1)`, read back `Math.round(v·(N−1))` — and Character always on the /7 scale.
13. **Latency honesty:** wet copies are *the effect*, not latency — report zero; do NOT
    "compensate" base delays or the doubles vanish (§4.4-class trap, opposite direction).
14. **Stereo input** (osc unison upstream is already stereo): Stack/Ensemble/Doubler run
    dual-mono with mirrored pans + independent phases (widens further); Mirror/Bands process
    m and PRESERVE the incoming side (s0 term §2.6) — never discard existing width.

---

## 10. Hard-rule compliance checklist (laws 1–10, walked)

1. **Bus reality:** normalizer + unity gate stated vs the measured −26 dBFS program (§3.3, §6,
   `kVoiceToFxPad` verified at PluginProcessor.cpp:6300). The draft's blanket "no thresholds" was
   **not true** — the Dimension compander is one, and it is now calibrated **relative to the
   −26 dBFS program** (§2.2: knee −12 dB re program, anchored to `kVoiceToFxPad`, never a literal
   dBFS constant). Nothing else in the device has a threshold, drive law or level detector. ✅
2. **Chassis:** 2 dropdowns (Type = choice(**8**), 7 live + 1 reserved-disabled per §3.9-C /
   Character choice(8)) + 8 back knobs 4×2 + front 3 heroes + Mix + 2 pills + power; pragmatic
   Title-case names throughout (§4). ✅
3. **Time params:** no tempo-relevant times in this device (all sub-50 ms micro-delays; Rate
   is Hz, not a division). If Max wants synced Rate (§11 Q6), it takes the house 4-bars→1/256
   table verbatim. ✅ (flagged)
4. **Mix 100 % = fully wet** (equal-power, engine-owned per fb318); Type/Character switches
   dip-swap/crossfade, never cut (§4). ✅
5. **Evolve 0→100:** every knob's law + taper stated (§3.6); Amount maxes past useful (±120
   cents Stack, **±100 cents Shift** — was a mis-sourced ±50, §2.3 — full comb split, **±50-cent
   wander**); Width's plateau bug fixed (§3.4). 7 Types, each with a stated measurable
   discriminator + the harness confusion gate (§8); **no Type invented to fill the dropdown** —
   `Rotor` was CUT rather than padded, and slot 7 ships disabled rather than filled. ✅
6. **Nothing free-runs:** Feedback env-gated squared-release; LFOs are silent without input by
   construction; loop gain counted, cap 0.90 + in-loop tanh (§3.5). ✅
7. **No clicks:** glide table §3.6 + pitfalls 1/4/5/6; click gates in harness (§8). **State-
   resetting swaps called out explicitly:** a Type change re-seats bases, LFO phases, allpass
   states and the granular window, and a Character change moves base delays — both therefore run
   the **dip → swap+reset → recover** cycle (PluginProcessor.cpp:7207 precedent) and NOT a naked
   `setType()`; Character additionally rides the 40 ms output crossfade (DistortionEngine.h:154).
   Retrig's read-head slew cap is §2.1. ✅
8. **CPU:** ≤0.4 % worst case **(estimate, unprofiled — gate on a real `wid_cpu` run)**, zero
   oversampling at every tier with the LTV argument (§3.7), sleep law (§8). ⚠️ `PV Glass` breaks
   the budget at ~1–2 % and is recommended CUT (§2.3). ✅ (flagged)
9. **Audible⇄visible:** voice fan + width wedge + correlation needle reflect all 11 params,
   idle-dim/playing-bright (§5.2) — **but the stereo/correlation data has no existing tap; the
   device must publish its own seqlock ring (§5.2-2). Unbuilt work, not a recycle.** ✅ (flagged)
10. **Recycle:** see the verified inventory below. ⚠️ (the earlier draft's one-line list carried
    two bad citations — both now corrected in §10.1)

### 10.1 RECYCLE INVENTORY — every entry OPENED AND READ at HEAD (2026-08-14 audit)

The house law is "recycle, verified by reading code — never assumed." This table is the receipt.

| Want | In-tree source | Status |
|---|---|---|
| Masked ring buffer + engine skeleton | `DelayEngine.h:33` (`class DelayEngine`), power-of-two `mask`, `prepare()` at :37 | ✅ verified |
| Fractional read (4-pt cubic Hermite) | `DelayEngine.h:240` + the design note at :23-25 ("bright AND (near) linear-phase → HQ without Serum's phasey dispersion") | ✅ verified |
| "Mutually non-multiple" length law | `DelayEngine.h:45-47`, `apMs = {4.77, 3.59, 12.7, 9.31}` | ✅ **line refs fixed** (draft said :46-48) |
| BBD / compander / anti-phase LFO idioms | `TerrainChorus.h:14-21` (`BASE_DELAY_MS 8.5`, `RIGHT_RATE_RATIO 1.07f` at **:18**, `RECON_LP_CUTOFF_MIN/MAX` :20-21), `RIGHT_PHASE_OFFSET = pi` at :19 = the anti-phase precedent | ✅ verified |
| What NOT to copy from it | `TerrainChorus.h:109-111` — naked side gain `side = (wetL−wetR)·0.5·(1+width)`. This is exactly the fake-drama law §3.4 rejects. **Read it, then don't use it.** | ✅ verified |
| Its ownership caveat | `TerrainChorus.h:1` literally reads `// plugins/Terrain/Source/TerrainChorus.h` — it belongs to the **Terrain FX sibling** and is pulled in via `IndyFxChain.h:30` / used at :259. Copy idioms, do not couple. | ✅ verified |
| Phase-vocoder shifter | `ShimmerReverb.h:27` (fb295 note) / `:360` (`ShimmerPV pv_`), `PN=2048, PH=PN/4, PLAT=1536` | ✅ verified — **and §2.3 recommends NOT using it** (32 ms latency) |
| Dip-swap on type change | `PluginProcessor.cpp:7207` (`dlySwapping_`, swap+`reset()` only once `dlyEnv_ < 1e-3`) | ✅ verified |
| Deferred Character crossfade | `DistortionEngine.h:154` + the `chrPend_` deferred swap | ✅ verified (file is 3035 lines; the draft's ":2912" is in range) |
| Engine setter grammar | `PluginProcessor.cpp:7284-7305`; `setKeyHz` last-note feed at **:7289** | ✅ verified |
| Insert lambda | `PluginProcessor.cpp:7310-7344` (`applyDst`) | ✅ verified |
| Exclusion sums | `:7159/:7161`, `:7326/:7328`, `:7358/:7360` — **six**, see §4 | ✅ verified, count corrected |
| Param-block grammar | `ParameterIDs.hpp:406-431` (SYN_DST), POWER-default-OFF at :428, SIG relabel precedent :410, Character choice(8) :407 | ✅ verified |
| UI param transport | `setSynParam` / `getSynParam` natives, `PluginEditor.cpp:756-780` — **NOT WebSliderRelay** | ✅ **corrected** (see pitfall 12) |
| Per-device viz atomics | `dstBlockWetPk`/`dstBloomEnv_` (`PluginProcessor.cpp:7341`, :7405-7406); `oscScope` seqlock (`PluginProcessor.h:855-865`) | ✅ verified |
| ~~Stereo scope tap~~ | ❌ **DOES NOT EXIST.** `scopeBuffer` is mono/256/master (`PluginProcessor.h:1125-1126`, written :7428-7429); `RollingCaptureBuffer` is the 10-min **export** capture, not a scope. **Widen must publish its own — budget it as new work** (§5.2-2). | ❌ **draft was wrong** |
| Chain-order param | `SYN_FX_ORDER` = choice(**6**), `PluginProcessor.cpp:3488-3495` / `:5860` / `:7383`; JS `PERMS` + the `slots.length !== 3` guard | ❌ **cannot grow — see §3.9-C** |
| Preset menu, chassis | `.pmenu` / `TIC.presets` glass; `Design/fx-rack-v7-CANONICAL.html` | ✅ named by house law, unchanged |

### 10.2 Rack laws A–D, walked

| Law | Verdict |
|---|---|
| **A** zero lookahead / zero reported latency | ✅ `getLatencySamples() = 0`; base delays never compensated (pitfall 13); Shift's W/2 budgeted into its slaps; `PV Glass`'s uncompensatable 32 ms is a CUT argument (§2.3, §3.9-A) |
| **B** no runtime param creation | ✅ all 23 params declared at birth; per-Type differences are relabels of a fixed slot pool (§3.9-B) |
| **C** choice cardinality fixed at birth | ⚠️ **ACTION REQUIRED BEFORE ANY CODE** — TYPE choice(8) w/ reserved slot, CHARACTER choice(8) always, and **`SYN_FX_ORDER` cannot be resized: pick path 1 or 2 in §3.9-C, §11 Q9** |
| **D** every send bus joins every exclusion sum | ✅ six sites enumerated + the real grep check (§4, pitfall 11) |

---

## 11. Open questions for Max

1. **Device name** — `Widen` is the front-runner (§0.2). Pick one; if `Swarm` wins, Type 1
   renames `Stack`→ stays, no conflict.
2. **Type 2 name** — keep `Dimension` (defensible: generic word, BOSS/Roland lineage,
   Xfer/Arturia/Audiority all trade on it) or go clean with `Bloom`/`Cube`? The *pair string*
   "Hyper/Dimension" is what we never use.
3. **Default Type** — Stack (the flagship drama) or Dimension (the subtle default that never
   embarrasses)? Bible assumes Stack.
4. **Character depth** — 8×7 = 56 voicings is the distortion-scale build. Trim to 6 per Type
   for v1? (The per-Type lists in §2 mark defaults first.) **Note this is a UI-list question
   only** — the param is choice(8) whatever you answer (§3.9-C).
5. **`PV Glass` char** (phase-vocoder Shift) — the audit **recommends CUT**: not just CPU
   (~1–2 % of a core, 3–5× the rest of the device) but a **32 ms latency that rack law A forbids
   compensating**, which turns a micro-double into a slapback. Ship it only if you want it
   relabelled as an echo-double. (Granular covers 95 % of the use.) §2.3.
6. **Synced Rate** — want a Rate sync toggle, or keep Hz only like every reference in §1?
   ⚠️ Costed by the audit: the house table is **4 bars → 1/256**, which at 120 BPM spans
   **0.125 Hz → 128 Hz** — an order of magnitude past this device's 0.02–12 Hz range, and above
   ~15 Hz the "LFO" becomes audible FM, not chorus. Sync means either accepting that (and
   voicing the top of the table as an effect) or shipping a truncated division list, which is
   its own kind of dead knob. Recommend: **Hz only for v1.**
7. **`Hear Mono` pill** — momentary (press-hold) or latching? Bible assumes momentary-style
   latching OFF on release… pick one, it's 5 lines either way.
8. **Card core** — voice fan + wedge fused (§5.2 recommendation) OK, or wedge-only minimal?
9. 🔴 **THE BLOCKING ONE (added by the audit — answer this FIRST, it changes the param list and
   the param list is immutable after birth):** `SYN_FX_ORDER` is a **choice(6)** = the 3!
   permutations of Reverb/Delay/Distortion, and cardinality is fixed at birth (fb342). A 4th
   device needs **4! = 24**. Pick: **(1)** new `SYN_FX_ORDER4` choice(24) + one-time legacy
   migration + a 24-row JS `PERMS` + the `slots.length !== 3 → !== 4` guard (correct, more work),
   or **(2)** pin Widen last in the chain, un-draggable for v1 (zero churn, breaks the rack's
   drag-to-reorder promise for one device). Details + line refs in §3.9-C.
10. **Reserved Type slot** — ship `SYN_WID_TYPE` as choice(8) with slot 7 disabled (so a future
    `Rotor`/8th Type is addable), or lock it at 7 forever? The audit recommends the reserved slot;
    it costs nothing and is the only moment it can be decided.

---

## 12. Sources

**Verification key (2026-08-14 audit — sources actually opened and read are marked):**
✅ = primary source fetched and the quote checked verbatim · ⚠️ = claim could NOT be confirmed in
a primary source; kept but flagged inline as unverified — **do not quote it as fact** ·
(unmarked) = cited by the researcher, not re-checked by the audit.

Serum / Xfer
* ✅ Serum manual PDF (Hyper/Dimension pp.27-28 — every §1.3 quote checked verbatim): https://s3.amazonaws.com/decembercymatics/Serum_Manual.pdf
* Serum 2 What's New (13 FX, direct-manipulation displays): https://images.equipboard.com/uploads/item/manual/127411/xfer-records-serum-2-advanced-wavetable-synthesizer-manual.pdf
* Serum 2 web manual root: https://xferrecords.com/web-manual/serum-2/welcome
* Xfer Dimension Expander (Duda's free 4-voice widener): https://bedroomproducersblog.com/2021/06/30/xfer-dimension-expander/ · https://www.audiotechnology.com/free-stuff/xfer-records-dimensionexpander
* Hyper recreation discussion (HISE forum): https://forum.hise.audio/topic/7885/recreate-serum-hyper-effect
* Crontis Studio Hyper/Dimension guide: https://note.com/crontis_studio/n/na209991b5ced
* Polarity Music — "What Does Serum's Hyper/Dimension Effect Actually Do?": https://www.patreon.com/polarity_music/posts/what-does-serums-159562938 · rebuild video: https://www.youtube.com/watch?v=zK80F4d8-oY
* MusicRadar Serum FX guide: https://www.musicradar.com/how-to/a-quick-guide-to-xfer-records-serums-effects

Supersaw
* Adam Szabo, "How to Emulate the Super Saw" (all §1.1 numbers; the polynomial sums to y(1)≈1.001 and the mix curves cross at b=0.75 — both re-derived and correct): https://www.adamszabo.com/internet/adam_szabo_how_to_emulate_the_super_saw.pdf

Dimension D lineage
* ✅ Arturia Chorus DIMENSION-D manual (measured modes + cross-mix + "triangle only" LFO — all §1.2 quotes checked verbatim in the PDF; note it gives NO chip number, NO delay ms, NO LFO Hz): https://dl.arturia.net/products/chorus-dimension-d/manual/chorus-dimension-d_Manual_1_1_EN.pdf
* Roland SDD-320 service notes (archive): https://archive.org/details/roland_SDD-320_SERVICE_NOTES
* SDD-320 history/circuit: https://www.astrepairperth.com.au/post/roland-sdd-320-dimension-d-the-chorus-unit-that-changed-everything
* ⚠️ Fractal forum measurements (rates/depths/delays) — COMMUNITY, unverified; the source of the MN3007 / 1-6 ms / 0.25-0.5 Hz / 14 % figures: https://forum.fractalaudio.com/threads/roland%C2%AE-dimension-d-sdd-320.56231/
* BOSS DC-2W review (dual anti-phase lines): https://www.soundonsound.com/reviews/boss-dc-2w-dimension-c · https://www.musicradar.com/how-to/the-fx-files-boss-dc-2-dimension-chorus
* Aion FX Blueshift (DC-2 circuit project): https://aionfx.com/project/blueshift-spatial-chorus/

Wideners / decorrelation
* ✅ Polyverse Wider (all-pass+comb, "cancels itself out when summed to mono", 200 %, free; launch date confirmed 18 May 2018 via SOS): https://polyversemusic.com/products/wider/ · https://www.soundonsound.com/news/polyverse-launch-free-mono-compatible-wider-plug
* Ozone Imager docs (Stereoize I/II, vectorscopes): https://s3.amazonaws.com/izotopedownloads/docs/ozone9/en/imager/index.html · http://help.izotope.com/docs/ozone/pages/meters_vectorscope.htm
* DAFx-24 open-source StereoWidener (allpass vs velvet, mono ripple numbers): https://www.dafx.de/paper-archive/2024/papers/DAFx24_paper_92.pdf · https://github.com/orchidas/StereoWidener
* Velvet-Noise Decorrelator (DAFx-17) / Optimized VND (DAFx-18): https://www.researchgate.net/publication/319666882_Velvet-Noise_Decorrelator · https://www.audiolabs-erlangen.de/resources/2018-DAFx-VND

Doubling / micro-pitch / ensemble
* ⚠️ Soundtoys MicroShift — the page confirms "3 flavors", H3000, AMS DMX 15-80s, and the Detune/Delay/Focus knobs, but NOT the preset numbers #231/#519 (those are unverified): https://www.soundtoys.com/product/microshift/
* ✅ Waves Doubler user guide p.3 (2 or 4 voices; Detune **-100..+100 cents**, Mod Depth ±200 cents, Mod Rate 0.1-200 Hz, Delay 0-100 ms, Pan ±45°, Feedback 0-100 %, Octaver) — this is the source that DISPROVED the draft's "±50 ceiling": https://assets.wavescdn.com/pdf/plugins/doubler.pdf
* Kilohearts Ensemble: https://kilohearts.com/products/ensemble
* NI Choral (4 modes, Scatter feedback): https://native-instruments.com/ni-tech-manuals/mod-pack-manual/en/choral
* jpcima ensemble-chorus (open-source Solina model): https://github.com/jpcima/ensemble-chorus
* Valhalla ÜberMod history (3-phase Solina/Crumar LFOs): https://valhalladsp.com/2012/03/09/valhallaubermod-the-history/
* Arturia Chorus JUN-6 (depth-in-ms grammar, anti-phase L/R LFO): https://manualsnet.com/arturia/chorus-jun-6

In-repo — **every line below was OPENED at HEAD during the audit (see the §10.1 receipt table)**
* PluginProcessor.cpp:26/46 (bus level story) · :6300 (`kVoiceToFxPad = 0.5f`) ·
  **:7159/:7161, :7326/:7328, :7358/:7360** (fb305/fb338 exclusions — **six** sites, not three) ·
  :7207 (dip-swap) · :7284-7305 (engine setters, :7289 setKeyHz) · :7310-7344 (applyDst) ·
  :7341/:7405-7406 (per-device viz envelope) · **:3488-3495 / :5860 / :7383 (`SYN_FX_ORDER`
  choice(6) — the 4th-device blocker, §3.9-C)** · :7428-7429 (the mono master scope write)
* PluginProcessor.h:855-865 (oscScope seqlock) · **:1125-1126 (`SCOPE_SIZE = 256`, MONO)**
* PluginEditor.cpp:756-780 (`setSynParam`/`getSynParam` natives — the rack's real param
  transport; **no WebSliderRelay**)
* DelayEngine.h:33 (engine grammar) · :23-25 (hermite HQ rationale) · :45-47 (non-multiple
  lengths) · :240 (hermite4 read) · TerrainChorus.h:1 (it belongs to the Terrain FX sibling)
  /:14-21/:109-111 · IndyFxChain.h:30/:259 · ShimmerReverb.h:27/:360 (`PN=2048, PLAT=1536`) ·
  SynthVoice.h:878 (`setUnisonA` — the name collision behind the `Unison` rule-out) /:1081/:5908
  (`kUniMaxDetuneCents = 50.0f`) · DistortionEngine.h:154 · ParameterIDs.hpp:345-431 (:407
  Character choice(8), :410 SIG relabel, :428 POWER default OFF) · RollingCaptureBuffer.h:19-35
  (**the 10-minute export capture — NOT a scope tap**)
* index.html — `DEVS` table (3 devices), `fxrRestoreDistortion` (Character read-back on the /7
  scale), `fxrRestoreOrder` (6-row `PERMS`, `slots.length !== 3` guard, `SYN_FX_ORDER` written as
  `pi/5`). ⚠️ **index.html line numbers in older notes are wrong for this topic** — the exclusion
  sums have only ever lived in PluginProcessor.cpp.
