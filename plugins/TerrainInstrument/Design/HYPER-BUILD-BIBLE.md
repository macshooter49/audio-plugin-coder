# Terrain Instrument — Hyper/Dimension-Alternative Build Bible (the UNISON-WIDENER)

**v1 — research complete. The single authoritative spec for the 4th FX device.**
Written after fb345 (distortion certified). Every number below is either read out of a cited
primary source (manual, thesis, measured-mode documentation), read out of this repo at the
cited line, or derived with the math shown. No DSP written yet.
Companion to `DISTORTION-BUILD-BIBLE.md` and `REVERB-BUILD-BIBLE.md` — same laws, same chassis.

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

**Names ruled OUT:** `Hyper`, `Dimension` (Serum's exact menu string — the one thing that IS
their signature), `Wider` (Polyverse product), `Doubler` (Waves product), `Unison` (no-doubles:
collides with the osc Unison controls, SynthVoice.h:878), `Imager` (iZotope).
Bible below writes **`Widen`**; global-replace on Max's pick.

---

## 1. History and circuits — the lineage that defined the effect

Six hardware/algorithm bloodlines feed this device. Each becomes a Type in §2.

### 1.1 Roland JP-8000 "Super Saw" (1996) — unison, measured

The godfather of "huge". 7 sawtooth oscillators, 1 center + 6 detuned. **Adam Szabo's thesis
measured everything we need** (adamszabo.com, "How to Emulate the Super Saw"):

* **The detune fan at full detune** (ratios vs the center osc, measured at C5 523.3572 Hz):
  `1 − 0.11002313, 1 − 0.06288439, 1 − 0.01952356, 1, 1 + 0.01991221, 1 + 0.06216538, 1 + 0.10745242`
  — i.e. asymmetric, out to ±~180 cents at max. **The fan is not evenly spaced** — that
  unevenness is why a supersaw shimmers instead of phasing.
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
2. **Two BBD delay lines** (MN3007-class), one per channel, base delay in the ~1–6 ms class
   (community measurements quote L ≈ 1 ms / R ≈ 6 ms operating points; Arturia measured the
   mode *relationships*, below).
3. **One trapezoid-ish LFO, ANTI-PHASE across channels:** `dL(t) = d0 + m(t)`, `dR(t) = d0 − m(t)`.
   Slew-limited triangle ⇒ near-constant |slope| for most of the cycle ⇒ near-constant pitch
   offset that alternates sign — reads as *detune*, not vibrato. Rates measured in the
   0.25–0.5 Hz class ("roughly a 2-second and a 4-second cycle"), depth ~14 %. **This LFO shape
   is THE Dimension tell.**
4. **The polarity cross-mix:** the delayed signal is mixed into the OPPOSITE channel with
   **inverted polarity** (Arturia: "The delayed signal is cross-mixed to the other channel with
   opposite polarity"), and a compensating low-shelf on the dry avoids the bass loss.
   This anti-phase side energy is the width.
5. **The 4 buttons, as Arturia measured them** (not the internet folklore): Mode 1 = softest,
   longest delays; Mode 2 = delay times ≈ **half** of Mode 1; Mode 3 = delay between 1 and 2
   but LFO depth **×2** (the deep one); **Mode 4 = wet gain boost only, always combined with
   1–3** ("despite many different descriptions that can be found online... this is what we
   actually measured on original units").

BOSS DC-2 (1985) minified the circuit; SOS's DC-2W review confirms the two lines modulated in
opposite directions and the mono sum staying clean. **Steve Duda's free Xfer "Dimension
Expander"** is a 4-voice restatement — "four chorus parts with extended delay times... two out
of the voices being out of phase with the other two", controls: power/size/mix — and Serum's
Dimension half is that plugin folded in (manual: "4 delay lines summed out-of-phase and slowly
amplitude modulated", Size = delay time, Mix 0 % = off).

### 1.3 Serum's Hyper/Dimension itself — the exact reference text

From the Serum manual (PDF, §FX):

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
slap delays. Soundtoys MicroShift documents its three styles as H3000 preset #231, H3000 #519
(different shift algorithm), and AMS DMX 15-80s ("much wider delay variation... separate,
harder 'de-glitching' circuit"); its knobs are Detune (% of the style's time-varying recipe),
Delay, Focus (low/mid sculpting), Mix. Discriminator vs chorus: the offset is **static** —
sidebands sit at fixed Hz offsets, no periodic flux.

### 1.5 String-machine ensemble — ARP/Eminent Solina (1974)

Triple chorus: 3 BBD taps modulated by a **3-phase LFO pair** (0°/120°/240°): one slow/deep
("chorus") + one fast/shallow ("vibrato") summed per phase. jpcima's open-source
`ensemble-chorus` (GitHub) models 3–6 BBD lines with paired LFOs at fixed phase offsets;
Valhalla's ÜberMod documents modeling "the 3-phase LFOs found in the Solina and Crumar
Performer". Community-standard rates: slow ≈ 0.6–0.9 Hz deep, fast ≈ 5.5–6.5 Hz shallow.
The result is a WALL — dense, symmetric, always-moving — unmistakably different from a 2-voice
chorus or a supersaw fan.

### 1.6 Mono-compatible decorrelators — the modern school

* **Polyverse/Infected Mushroom Wider** (2018, free): "all-pass and comb filtering algorithm"
  generating **pure side-channel content** — "Wider cancels itself out when summed to mono, so
  the original signal is left intact"; width to 200 %. The trick class: any `L = m + s`,
  `R = m − s` is mono-EXACT by construction, whatever s is.
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
| 2 | **Dimension** | Roland SDD-320 / DC-2 / Duda | 2×2 anti-phase trapezoid-modulated lines + inverted-polarity cross-mix | Near-zero centroid flux (motionless!) yet side/mid energy ≥ target; trapezoid slope = bimodal pitch histogram |
| 3 | **Shift** | H3000 #231 / AMS / MicroShift | Dual granular pitch shifters, +c cents L / −c cents R + unequal slap | STATIC sideband offset (no periodicity in flux); L/R spectra mirror-detuned |
| 4 | **Ensemble** | Solina triple chorus | 3 taps/ch, 3-phase dual LFO (slow-deep + fast-shallow) | Modulation spectrum has BOTH LFO lines (≈0.7 Hz and ≈6 Hz); 6-lobe pan distribution |
| 5 | **Doubler** | ADT / Waves Doubler | 2–4 discrete voices, static 15–80 ms delays, RANDOM-WALK pitch wander, per-voice pan | Aperiodic flux (no LFO line in mod spectrum); discrete echo peaks in cepstrum |
| 6 | **Mirror** | Wider / Ozone Stereoize / VND | Pure-side injection: s = w·[APcascade(m) − m]/2, L = m+s, R = m−s | Mono sum ≡ dry (< 0.1 dB ripple by construction); smooth (comb-free) per-channel spectrum |
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
  (the JP-8000 itself reaches ±~180; Serum's Hyper audibly reaches ± tens). No playing safe.
* **Retrig** (front pill): on note-on, glide all φ_v → 0 over 8 ms (glide, never jump — §9.5):
  all voices sweep out from unison = the "laser zap", now click-free.
* **Characters:** `JP Classic` (default: exact §1.1 fan + curve) · `Even Fan` (linear-spaced
  offsets — audibly "phasier", proof the JP unevenness matters) · `Analog Drift` (adds
  SmoothRandom ±15 % on each r_v) · `Tight Digital` (bases 4–9 ms — glassier, more metallic) ·
  `Wide Fan` (bases 15–45 ms — thicker, doublier) · `Octave Bloom` (voices 7–8 read at
  2× buffer rate = +1 octave shimmer, level −12 dB) · `Laser` (retrig always-on + Rate ×4 on
  the first 60 ms after note-on) · `Sub Anchor` (voice 2 = −1 octave via half-rate read, mono,
  −9 dB — the hardstyle trick).

#### 2.2 DIMENSION
* **Engine (per §1.2, all five stages):** tilt pre-emphasis (+3 dB LF shelf 200 Hz / −3 dB HF
  shelf 5 kHz — cheap 2 shelves) → optional compander (Character) → per-channel delay lines,
  `dL = d0·Size + m(t)`, `dR = d0·Size − m(t)`; m = **trapezoid** = triangle at `Rate`
  (0.25–0.5 Hz zone at knob center) through a slew clamp (slew limit = 8×triangle slope ⇒ flat
  tops ~25 % of cycle); depth = 14 %·Depth-law of d0 → de-emphasis (inverse shelves) →
  **cross-mix**: `wetL' = wetL − k·wetR`, `wetR' = wetR − k·wetL`, k = 0.35 + 0.35·Amount →
  dry low-shelf +2 dB @150 Hz compensation.
* d0 = 5 ms nominal; `Delay` (P3) spans 1.5–20 ms. 4 lines total (2/ch, second pair at
  d0×1.6, LFO inverted) = Serum's "4 delay lines" density at Character `Quad` (default `Duo`
  = the SDD-320 2-line truth — audibly drier/rawer).
* **Slow amplitude modulation** (Serum's "slowly amplitude modulated"): ±1.5 dB sine at
  0.11·Rate on the wet pair, anti-phase — the breathing.
* **Characters:** `Duo 320` (default) · `Quad Expander` (Duda 4-line) · `Mode 1/2/3 fixed`
  (the measured Arturia ratios: 2 = delays ×0.5, 3 = depth ×2) as three chars ·
  `No Compander` (clean modern) · `Dark BBD` (recon LP 4 kHz, TerrainChorus.h:21 idiom) ·
  `Wobble` (trapezoid→sine + depth ×2.5 — deliberately breaks the motionless rule = the
  night-and-day proof char).

#### 2.3 SHIFT
* **Engine:** per channel one granular shifter — 2 read taps 180° apart on a `W = 30 ms`
  window, triangular crossfade, read-rate ratio `ρ = 2^(±cents/1200)`; L gets +cents, R gets
  −cents. Plus per-side slap: L +8 ms, R +12.5 ms (unequal — H3000 recipe) with ±20 %
  SmoothRandom wander at 0.3 Hz (the "time-varying" MicroShift documents).
* `Amount` → cents = `50·t^1.4` (±50 = the Waves Doubler ceiling; honest doubler territory
  ends ~±25, the last stretch is the past-useful zone).
* **Characters:** `H3K Silk` (default, W = 30 ms) · `AMS Punch` (W = 12 ms — tighter, glitchier
  transients, "harder de-glitch") · `Tape Warble` (adds 0.8 Hz wander ×3) · `Fifth Up`
  (+700 cents on voice 2 at −15 dB — shimmer-adjacent) · `Down Double` (both sides −cents,
  L≠R magnitudes) · `Wide Slap` (slaps 18/29 ms) · `PV Glass` (OPTIONAL, §11 Q5: reuse
  ShimmerReverb's phase-vocoder `ShimmerPV`, ShimmerReverb.h:27/:360 — clean but +CPU) ·
  `Gritty` (linear-interp reads + W = 8 ms — intentional AM grit).

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
  (Waves Doubler grammar: per-voice gain/pan/delay/detune, delays to ~100 ms.)
* **Characters:** `Vocal 2x` (default, 2 voices) · `Vocal 4x` · `ADT Tape` (walk ×2 +
  wow-shaped 0.6 Hz component — Abbey Road ADT lore) · `Tight Inst` (delays ×0.5) ·
  `Loose Crowd` (delays ×1.8, walk ×1.6) · `Detuned Twins` (walk replaced by static ±8 cents —
  crosses into Shift territory ON PURPOSE, but with echo) · `Slapback` (voice 1 at 84 ms,
  the Sun Records edge) · `Humanize Max` (walk ±20 cents — seasick past-useful top).

#### 2.6 MIRROR
* **Engine (mono-exact family, §1.6):** m = (L+R)/2, s0 = (L−R)/2;
  `a(m) = [AP1..AP6](m)` — 6 first-order allpasses, fc log-spaced 180 Hz–5.6 kHz, coefficients
  randomized per Character seed; generated side `s = Amount·(a(m) − m)·0.5`;
  out: `L = m + (s0 + s)·Width`, `R = m − (s0 + s)·Width`. **Mono sum = m exactly, at every
  knob position** — the by-construction guarantee.
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
Clamp `A_v ≤ A_max = 30 ms` (protects the buffer & keeps Rate→0 finite: as f→0, A hits the
clamp and cents honestly fall — display law, not a lie). This is a straight "beat Serum"
measurable: sweep Rate at fixed Detune → our sideband spread stays put, theirs walks.

**Base-delay scatter:** voice bases in ms `{0, 9.7, 13.1, 17.3, 21.9, 11.3, 15.7, 24.1}` —
pairwise non-multiple (comb law: DelayEngine.h chose its allpass lengths "mutually non-multiple"
for the same reason, DelayEngine.h:46-48).

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
`mod_v` = sine LFO (Stack), trapezoid (Dimension), 3-phase pair (Ensemble), SmoothRandom walk
(Doubler), const-ratio granular taps (Shift). One switch, one buffer, one read path.

### 3.3 Summing + unity law (bus reality, law 1)

N incoherent voices sum to ~√N power. Normalize: `g_norm = 1/√(center² + Σ sides²)` with
center/sides from the §1.1 Balance law — **computed at control rate, glided 20 ms** (a
program-independent normalizer — the Tape lesson: never normalize by program level,
DISTORTION-BUILD-BIBLE §9.1). Result: Mix 100 %, Amount anywhere, Voices anywhere ⇒ wet RMS
within ±1 dB of dry (harness gate §8). The FX bus sits ≈ −26 dBFS (kVoiceToFxPad = 0.5,
PluginProcessor.cpp:6300-6301; program measured −26 dBFS, DISTORTION-BUILD-BIBLE §2.1) — this
device has **no thresholds** so the bus level costs nothing, but the normalizer is what keeps
default-settings ≈ unity-through (§6).

### 3.4 Width stage (universal, post-everything)

Equal-power M/S rotation, NOT naked side gain:
`M' = M·cos(θ) ·√2, S' = S·sin(θ)·√2, θ = Width·π/4` — Width 0 = mono, 0.5 = untouched,
1.0 = +3 dB side / −3 dB mid (the 200 % zone). Keeps total energy constant (naked S·2
boosts loud programs +4–6 dB = a fake-drama violation). Width lives OUTSIDE the feedback loop
(§3.5) and AFTER the mono-exact construction (Mirror/Bands stay mono-exact at every Width —
scaling s preserves L+R = 2m).

### 3.5 Feedback (P7) — loop-gain law (house law 6)

Wet block output recirculates into the voice-block input: `in' = in + fb·softclip(wetSum)`.
Gain stages inside the loop, counted: voice sum (normalized to 1.0 by §3.3) × in-loop damping
LP (≤1) × fb. **Max stable loop gain = fb ⇒ cap fb = 0.90**, tanh softclip in the loop
(DelayEngine's fb303 bounded-feedback pattern), 6 dB/oct damping at 7 kHz in the loop so the
bloom darkens as it regenerates (Choral's "Scatter" documented this feedback-chorus =
reverb-adjacent bloom). **Env-gated:** loop input is multiplied by the device input-follower
env (20 ms attack / 150 ms release, squared release — the Phase-G law) so the bloom DIES with
the note. Nothing free-runs.

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
_SRC_A.._SRC_NOISE / _POWER / _RETRIG / _MONO` — clone the SYN_DST block grammar
(ParameterIDs.hpp:406-431). AudioParameterChoice reads = INDEX direct (the CLAUDE.md §4 law).

**Routing:** per-osc pills + main-send, inherited. ⚠️ **THE LANDMINE (fb305/fb338):** a 4th
send bus must join **every** main-send exclusion sum — currently three identical lines,
PluginProcessor.cpp:**7159**, **7326**, **7358** (`the fb305 law: EVERY send bus joins EVERY
main-send exclusion`) — each gains a `+ widSend` term, AND the new device's own applyWid lambda
subtracts all four buses. Miss one line ⇒ a routed osc leaks dry through another device's main
send. This is the exact §4.5-class trap the distortion bible predicted; it recurs per device.

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
   polar wedge sweeping ±45°, filled by recent L/R sample dots (128/frame from the existing
   scope tap — RollingCaptureBuffer.h), plus a needle at the live correlation
   `r = Σ L·R / √(ΣL²·ΣR²)`; needle green r > 0.3, amber 0..0.3, red < 0 (mono danger, tied
   to the §8 spec). `Hear Mono` pill collapses the wedge to a single beam — the visual states
   the audio. Cheap: dots into an offscreen with 0.92 fade multiply.
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
  factor barely moves (all-LTV); long Feedback blooms raise RMS tail ≈ RT ~0.4 s at fb 0.9 —
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

**Budget:** voice loop = 1 hermite read (4 taps) + LFO + 2 mults per voice per channel; 8
voices ≈ 16 reads/sample stereo ≈ the Diffuse delay path (shipped fine). Estimate **≤ 0.4 %
of one core @ 48 k / M-series** worst-Type (Stack 8v + Feedback); Mirror ≈ 12 first-order
APs = trivial; Shift = 4 reads + 2 crossfades. No oversampling (§3.7). **Sleep law:** the
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
  | Mirror/Bands | mag-spectrum of (L+R)/2 vs dry, any knob position | ripple < **0.1 dB** (construction proof) |
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
11. **The 4th-bus exclusion landmine** — three lines + the new lambda (§4). Grep
    `fb305 law` and count FOUR after the edit.
12. **WebSliderRelay 4-point binding** for every new SYN_WID param or it silently no-ops
    (CLAUDE.md §4) — 15 relays.
13. **Latency honesty:** wet copies are *the effect*, not latency — report zero; do NOT
    "compensate" base delays or the doubles vanish (§4.4-class trap, opposite direction).
14. **Stereo input** (osc unison upstream is already stereo): Stack/Ensemble/Doubler run
    dual-mono with mirrored pans + independent phases (widens further); Mirror/Bands process
    m and PRESERVE the incoming side (s0 term §2.6) — never discard existing width.

---

## 10. Hard-rule compliance checklist (laws 1–10, walked)

1. **Bus reality:** no thresholds; normalizer + unity gate stated vs the measured −26 dBFS
   program (§3.3, §6, kVoiceToFxPad cited). ✅
2. **Chassis:** 2 dropdowns (Type 7 / Character 8) + 8 back knobs 4×2 + front 3 heroes + Mix +
   2 pills + power; pragmatic Title-case names throughout (§4). ✅
3. **Time params:** no tempo-relevant times in this device (all sub-50 ms micro-delays; Rate
   is Hz, not a division). If Max wants synced Rate (§11 Q6), it takes the house 4-bars→1/256
   table verbatim. ✅ (flagged)
4. **Mix 100 % = fully wet** (equal-power, engine-owned per fb318); Type/Character switches
   dip-swap/crossfade, never cut (§4). ✅
5. **Evolve 0→100:** every knob's law + taper stated (§3.6); Amount maxes past useful (±120
   cents, full comb split, ±20-cent wander); 7 Types each with a stated measurable
   discriminator + harness confusion gate (§8). ✅
6. **Nothing free-runs:** Feedback env-gated squared-release; LFOs are silent without input by
   construction; loop gain counted, cap 0.90 + in-loop tanh (§3.5). ✅
7. **No clicks:** glide table §3.6 + pitfalls 1/4/5/6; click gates in harness (§8). ✅
8. **CPU:** ≤0.4 % worst case, zero oversampling with the LTV argument (§3.7), sleep law (§8). ✅
9. **Audible⇄visible:** voice fan + width wedge + correlation needle reflect all 11 params,
   idle-dim/playing-bright (§5.2). ✅
10. **Recycle:** DelayEngine buffer/hermite/smoother grammar (DelayEngine.h:33/:240),
    TerrainChorus BBD/compander/anti-phase idioms (TerrainChorus.h:11-23,:109 — in-tree, used
    by IndyFxChain.h:30/:259; note its header says it belongs to the Terrain FX sibling — copy
    idioms, don't couple), ShimmerPV option (ShimmerReverb.h:27), SmoothRandom/wow grammar
    (DelayEngine wow path), dip-swap + 40 ms crossfade declick (PluginProcessor.cpp:7207,
    DistortionEngine.h:154), insert-lambda + exclusion edits (PluginProcessor.cpp:7310-7358),
    SYN_DST param-block grammar (ParameterIDs.hpp:406-431), pmenu presets, fx-rack-v7 chassis,
    RollingCaptureBuffer scope tap. ✅

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
   for v1? (The per-Type lists in §2 mark defaults first.)
5. **`PV Glass` char** (phase-vocoder Shift) — ship, or cut for CPU sanity? (Granular covers
   95 % of the use.)
6. **Synced Rate** — want a Rate sync toggle (then: house synced-division table), or keep Hz
   only like every reference in §1?
7. **`Hear Mono` pill** — momentary (press-hold) or latching? Bible assumes momentary-style
   latching OFF on release… pick one, it's 5 lines either way.
8. **Card core** — voice fan + wedge fused (§5.2 recommendation) OK, or wedge-only minimal?

---

## 12. Sources

Serum / Xfer
* Serum manual PDF (Hyper/Dimension section, quoted §1.3): https://s3.amazonaws.com/decembercymatics/Serum_Manual.pdf
* Serum 2 What's New (13 FX, direct-manipulation displays): https://images.equipboard.com/uploads/item/manual/127411/xfer-records-serum-2-advanced-wavetable-synthesizer-manual.pdf
* Serum 2 web manual root: https://xferrecords.com/web-manual/serum-2/welcome
* Xfer Dimension Expander (Duda's free 4-voice widener): https://bedroomproducersblog.com/2021/06/30/xfer-dimension-expander/ · https://www.audiotechnology.com/free-stuff/xfer-records-dimensionexpander
* Hyper recreation discussion (HISE forum): https://forum.hise.audio/topic/7885/recreate-serum-hyper-effect
* Crontis Studio Hyper/Dimension guide: https://note.com/crontis_studio/n/na209991b5ced
* Polarity Music — "What Does Serum's Hyper/Dimension Effect Actually Do?": https://www.patreon.com/polarity_music/posts/what-does-serums-159562938 · rebuild video: https://www.youtube.com/watch?v=zK80F4d8-oY
* MusicRadar Serum FX guide: https://www.musicradar.com/how-to/a-quick-guide-to-xfer-records-serums-effects

Supersaw
* Adam Szabo, "How to Emulate the Super Saw" (all §1.1 numbers): https://www.adamszabo.com/internet/adam_szabo_how_to_emulate_the_super_saw.pdf

Dimension D lineage
* Arturia Chorus DIMENSION-D manual (measured modes, circuit walk): https://dl.arturia.net/products/chorus-dimension-d/manual/chorus-dimension-d_Manual_1_1_EN.pdf
* Roland SDD-320 service notes (archive): https://archive.org/details/roland_SDD-320_SERVICE_NOTES
* SDD-320 history/circuit: https://www.astrepairperth.com.au/post/roland-sdd-320-dimension-d-the-chorus-unit-that-changed-everything
* Fractal forum measurements (rates/depths/delays): https://forum.fractalaudio.com/threads/roland%C2%AE-dimension-d-sdd-320.56231/
* BOSS DC-2W review (dual anti-phase lines): https://www.soundonsound.com/reviews/boss-dc-2w-dimension-c · https://www.musicradar.com/how-to/the-fx-files-boss-dc-2-dimension-chorus
* Aion FX Blueshift (DC-2 circuit project): https://aionfx.com/project/blueshift-spatial-chorus/

Wideners / decorrelation
* Polyverse Wider (mono-cancel side trick, 200 %): https://polyversemusic.com/products/wider/ · https://www.soundonsound.com/news/polyverse-launch-free-mono-compatible-wider-plug
* Ozone Imager docs (Stereoize I/II, vectorscopes): https://s3.amazonaws.com/izotopedownloads/docs/ozone9/en/imager/index.html · http://help.izotope.com/docs/ozone/pages/meters_vectorscope.htm
* DAFx-24 open-source StereoWidener (allpass vs velvet, mono ripple numbers): https://www.dafx.de/paper-archive/2024/papers/DAFx24_paper_92.pdf · https://github.com/orchidas/StereoWidener
* Velvet-Noise Decorrelator (DAFx-17) / Optimized VND (DAFx-18): https://www.researchgate.net/publication/319666882_Velvet-Noise_Decorrelator · https://www.audiolabs-erlangen.de/resources/2018-DAFx-VND

Doubling / micro-pitch / ensemble
* Soundtoys MicroShift (H3000 #231/#519, AMS 15-80s styles): https://www.soundtoys.com/product/microshift/
* Waves Doubler user guide (per-voice params): https://assets.wavescdn.com/pdf/plugins/doubler.pdf
* Kilohearts Ensemble: https://kilohearts.com/products/ensemble
* NI Choral (4 modes, Scatter feedback): https://native-instruments.com/ni-tech-manuals/mod-pack-manual/en/choral
* jpcima ensemble-chorus (open-source Solina model): https://github.com/jpcima/ensemble-chorus
* Valhalla ÜberMod history (3-phase Solina/Crumar LFOs): https://valhalladsp.com/2012/03/09/valhallaubermod-the-history/
* Arturia Chorus JUN-6 (depth-in-ms grammar, anti-phase L/R LFO): https://manualsnet.com/arturia/chorus-jun-6

In-repo (cited at line throughout)
* PluginProcessor.cpp:26/46 (bus level story) · :6300 (kVoiceToFxPad) · :7159/:7326/:7358 (fb305/fb338 exclusions) · :7207 (dip-swap) · :7284-7305 (engine setter grammar) · :7310-7344 (applyDst insert pattern)
* DelayEngine.h:33 (engine grammar) · :240 (hermite4 HQ read) · TerrainChorus.h:11-23/:109 · ShimmerReverb.h:27/:360 · SynthVoice.h:878/:1081/:5908 (kUniMaxDetuneCents 50) · DistortionEngine.h:154/:2912 · ParameterIDs.hpp:345-431
