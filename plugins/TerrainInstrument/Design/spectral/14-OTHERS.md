# 14 — SPECTRAL & WAVETABLE MORPH IN THE *OTHER* SYNTHS

**Scope.** Everything the big three (Serum · Vital · Phase Plant) are *not*: Arturia Pigments 6,
UVI Falcon 3, u-he (Zebra2 · Zebralette 3 · Hive 2), Ableton (Wavetable · Meld · Spectral
Resonator · Spectral Time), NI Massive X, Bitwig (Grid + Spectral Suite). Read for one purpose:
**find the spectral moves nobody in the mainstream wavetable world has made, that Terrain's
existing architecture can actually execute.** Max's brief: *"unique ways to use our spectral"*,
not a clone.

**Honesty rules applied throughout.**
- Every figure below is either **quoted from a manual** (page cited) or **read out of source**.
- Where a manual states behaviour but not maths, the maths is marked **INFERRED** and the
  reasoning is given. Where I could not determine something, §11 says so explicitly.
- No blog paraphrase is used as a primary source. Two blog/press items are cited only where the
  manual is silent, and are labelled as such.

**Primary sources (downloaded and read locally, not summarised from search results):**

| # | Document | Version / date | Public URL | Local extract |
|---|---|---|---|---|
| S1 | *User Manual Pigments* | **6.0.0** | `https://dl.arturia.net/products/pigments/manual/pigments_Manual_6_0_0_EN.pdf` | `…/scratchpad/spec/pigments6.txt` |
| S2 | *Falcon — Software User Manual* | **3.0.1** (latest changelog entry) | `https://uvi.s3.amazonaws.com/Manuals/falcon_manual.pdf` | `…/spec/falcon.txt` |
| S3 | *Zebra2 user guide* | 2.9.x | `https://u-he.com/downloads/manuals/plugins/zebra2/Zebra2-user-guide.pdf` | `…/spec/zebra2.txt` |
| S4 | *Zebralette 3 user guide* | **3.0, 4 Dec 2025** | `https://uhe-dl.b-cdn.net/manuals/plugins/zebralette3/Zebralette3%20user%20guide.pdf` | `…/spec/zebralette3.txt` |
| S5 | *Hive user guide* | 2.x | `https://u-he.com/downloads/manuals/plugins/hive/Hive-user-guide.pdf` | `…/spec/hive.txt` |
| S6 | *Hive Wavetables* (the **.uhm** scripting reference) | undated, ships with Hive | `https://u-he.com/downloads/manuals/plugins/hive/Hive-Wavetables.pdf` | `…/spec/uhm.txt` |
| S7 | *MASSIVE X Manual* | 1.4, **17 Mar 2023** | `https://www.native-instruments.com/fileadmin/ni_media/downloads/manuals/massive_x/MASSIVE_X__MANUAL_17_03_2023_ENGLISH.pdf` | `…/spec/massivex.txt` |
| S8 | Ableton *Live Instrument Reference* (Live 12) | live | `https://www.ableton.com/en/manual/live-instrument-reference/` | `…/spec/abl_inst.txt` |
| S9 | Ableton *Live Audio Effect Reference* (Live 12) | live | `https://www.ableton.com/en/manual/live-audio-effect-reference/` | `…/spec/abl_fx.txt` |
| S10 | Bitwig *User Guide* — Spectral | live | `https://www.bitwig.com/userguide/latest/spectral/` | (fetched) |
| S11 | Vital source (open) | `main` | `https://raw.githubusercontent.com/mtytel/vital/main/src/synthesis/producers/synth_oscillator.h` | `…/spec/synth_osc.h` |
| S12 | NI docs (HTML mirror of S7) | live | `https://docs.native-instruments.com/ni-tech-manuals/massive-x-manual/en/wavetable-oscillators` | (fetched) |

Terrain-side numbers below are read from source in this worktree, cited `file:line`.

---

## 1. THE FILTER THAT DECIDES WHAT IS PORTABLE

Before any competitor idea is worth writing down, it has to survive Terrain's architecture. From
`00-INVENTORY.md` and re-checked in source:

- The spectrum is a **pure function of the patch**, evaluated on the **message thread**, baked into
  a shared table, and atomic-published. `rebuildMorphIfNeeded` → `buildFromSpec` ≈ **21 ms
  measured** (544 iFFTs), gated at **20 Hz per osc**.
- The table is **shared by every voice**. `Wavetable.h:67` — `kNumMipLevels = 34`, and the comment
  states the cost in-source: **`34×16×2048×4 B = 4.46 MB/table (~134 MB across 30 factory
  tables)`**. There is no per-voice spectrum and no room for many more tables.
- The audio thread never sees a spectrum. Per block it picks a mip and calls `renderBlend()`;
  per sample it does `applyPhaseWarp() → readCycle() → applyAmpWarp() → fold`.

That gives **three classes**, and every idea in §9 is tagged with one:

| Class | Meaning | Cost budget |
|---|---|---|
| **BAKE** | a new pure op on `WavetableSpec` inside `SpectralMorph::apply()` | O(P) on ≤512 partials ≈ **microseconds**. Free. The 21 ms is `buildFromSpec`, which runs anyway. |
| **READ** | changes what the audio thread reads or how, no new FFT | per-block int / per-sample arithmetic — must be justified against the existing WARP budget |
| **VOICE** | needs per-note state (decay since note-on, per-note seed, per-note pitch) | **not portable** without a per-voice additive engine. Say so and stop. |

**The single most important consequence, and it kills a third of the competitor catalogue:**
u-he's Spectral Decay, Twinkles and Dissociate; Massive X's Random/Jitter; Falcon's Beating —
all are functions of *the note*, not *the patch*. They cannot ride a shared table. Anything below
tagged **VOICE** is documented for completeness and explicitly **not recommended**.

---

## 2. ARTURIA PIGMENTS 6

### 2.1 Wavetable engine — "Morph" is a **boolean**, not a spectral operation
Pigments' famous "Morph" button is the smallest possible feature and it is worth stating plainly
so nobody chases it: *"Transitions between wavetable positions will occur smoothly when the Morph
feature is enabled. When it is disabled the transitions will be immediate."* (S1 p.86). It is
**interpolate vs step** on the Position axis. Nothing spectral happens.

Engine facts (S1 p.85): **up to 256 waveforms/positions per table, 2,048 samples each** — the same
2048 as Terrain, 16× the frame count Terrain's morph path can address (`WavetableSpec::kNumFrames
= 16`, `Wavetable.h:59`).

### 2.2 Phase Transform — 7 named remap curves (S1 pp.91–92)
*"changes the shape of a waveform according to one of seven modulator waves, which are known as
Types… The remap curves for each Target wave are based on the way they affected a sine wave, so
the results will vary when the input waveform is more complex."*

| Type | Manual's description (verbatim) |
|---|---|
| **Pulse Width** | "Adds subtle to sharp harmonic edge on most waves" |
| **Skew** | "peaks are spread to the left and right, leaving a valley" |
| **Round** | "The source is influenced by a semi-square; it could gain valleys and/or plateaus" |
| **Tri/Pulse** | "Takes the middle of the waveform and stretches it to the left" |
| **Octave Plus** | "Part of source wave is miniaturized on the right; some harmonics are emphasized" |
| **Pseudo PW** | "Stretches the whole waveform to the left and leaves a gap on the right" |
| **Fractalize** | "Creates up to 8 copies of the whole waveform, from smaller to larger" |

Controls: one amount knob (labelled with the type name) + a **Phase Mod** knob that lets the
engine's own Modulator drive the transform amount at audio rate (S1 p.92).
**Overlap with Terrain: heavy.** This is Terrain's WARP (`SynthVoice.h:938-1000`, 11 options ×
2 chained slots). *Fractalize* — up to 8 nested copies of decreasing size — is the one Terrain
does not have, and it is also Zebra2's `Fractalz` (§4.1). See §9.

### 2.3 Wavefolding — the folder is a **separate wave**, not the signal
*"Rather than folding the original wave back on top of itself, Pigments uses a selectable waveform
and 'folds' it downward onto the peaks of the current wavetable"* (S1 p.93). Three Fold Shapes.
This is a **two-input** fold: `out = fold(wave, folderWave)`, not `fold(wave)`.
Terrain's fold (`Shapers.h`, `kFoldPre={9.0, 5.28318530, 5.0}`, ADAA) is the classic one-input
kind. INFERRED: Pigments is computing something of the form `min(wave, folder) `/ reflection about
a moving boundary rather than a fixed ±1 — the manual does not give the function.

### 2.4 Harmonic engine — the real content (S1 pp.117–126)
Additive, **up to 512 partials**, with a **Partials Limit** dropdown that caps the count *for CPU
reasons* — CPU is explicitly proportional to partial count.

**Shape section** (pp.121–122): **two spectrum slots, 12 shapes each, with a Morph knob that
crossfades A↔B**, plus:
- **Section** — "shifts the position of the spectrum over the partial series, which changes the
  partials that it affects"
- **Depth** — amount
- **highpass / lowpass icons** — attenuate below / above the spectrum's range
- **Tilt** — slope steepness; **Tilt Offset** — "changes the partial at which the slope begins"
- **Parity** — "all odds, all evens, or any mix in between"

⚠️ **Manual inconsistency, flagged not resolved:** p.121 says the spectrum is "like a multi-point
EQ curve that notches out multiple frequencies" (amplitude), but p.122's Depth text says it
"controls how much the spectrum affects the **frequencies** of the partial series". One of the two
is wrong. INFERRED: it is an amplitude envelope (the vowel/"ee-ah-ow" tip on p.122 only makes
sense as amplitude formants).

**Imaging section** (p.123) — **per-partial stereo placement**, three modes:
- **Split** — Odd knob pans odd partials L/R, Even knob pans even partials L/R
- **Random** — Rate + Depth, randomly pans individual partials
- **Periodic** — Periods (cluster size) + Depth, pans *clusters* of partials

**Partial shaper** (pp.124–126) — three modes, and **all three operate on a WINDOW of the partial
series, not the whole spectrum**:
- **Window** — `Position` (lowest partial), `Win Size` (width), `FM` (modulator FM applied *to the
  partials inside the window only*), `Gain` (level of the window only)
- **Cluster** — `Position`, `Clusters` (window width → how many clusters), `Partials` (partials per
  cluster), `Density` ("how much the partials' frequency will shift towards the starting point of
  their cluster"). Manual's tip: *"try Density values at or near 25%, 50%, and 100%"*
- **Shepard** — `Position`, `Win Size`, **`Phi`** ("the amount of frequency shift towards the next
  partial up, within the window"), `Gain`. Manual's recipe: *"modulate Phi with a slow LFO set to a
  ramp waveform. Set the Phi knob to 0.500 and modulation depth to 0.50"*

### 2.5 Modal engine "Warp" — the **snap-to-harmonic** switch (S1 p.130)
- **Warp** — bipolar, "expands or compresses the group of partials relative to the fundamental"
- **Range** — **"the first harmonic below which partials are no longer warped"** (a protected low band)
- **Shape** — "subtly alters the shape of the warp, affecting individual partials within it"
- **Q (quantize)** — **"Snaps warped partials to the harmonic series"**; the manual's own tip:
  *"That Q button is your best friend… It locks every warped harmonic to the closest position in
  the harmonic series."*

### 2.6 Vocoder (new in Pigments 6, S1 pp.185–186) — one parameter worth stealing
Params: Mode (Vintage/Modern/Dirty), Enhance, **Bands**, Low bound, High bound, Bandwidth, Formant,
Decay, Gate, Sibilance, and — **`Freq Tilt`: "Adjusts the decay time of all bands relative to their
cutoff frequencies."** A per-band *decay-time* tilt. Same family as u-he's Spectral Decay (§4.2).

### 2.7 What is **unusual** in Pigments
1. **Every spectral op is windowed** (Position + Win Size). Pigments never applies an effect to the
   whole spectrum when it could apply it to a slice.
2. **Warp + Q**: an inharmonic warp with a switch that re-snaps to the harmonic grid.
3. **Shepard as a knob** (`Phi`), not a preset.
4. **Per-partial stereo** as a first-class section.
5. Two spectral envelopes with a **morph knob between them** — the "morph" the product name
   promises lives in the *Harmonic* engine, not the wavetable engine.

---

## 3. UVI FALCON 3 — the biggest find in this survey

### 3.1 Wavetable oscillator (S2 pp.133–134) — thin, with one strange import path
Params: Wave Index, **Smooth Wave Index**, **Smooth Octaves**, **Phase Distortion Mode** +
**Phase Distortion Amount**, Start Phase, FM (Enable/Depth/Ratio/Snapping/Fine/Hz), Unison with
**Wave Spread** ("the range of Wave Index values for each voice" — unison voices sit at *different
table positions*, up to 8 voices).

⚠️ **The manual never lists the Phase Distortion Modes.** It only names one indirectly, in the
changelog: *"fix Wavetable SymForm mode when phase distortion is at 0"* (S2 changelog, Falcon
1.5.4). A third-party source claims "just under a dozen" modes; I could not obtain the list from a
primary source. **See §11.**

**The import path is the unusual bit** (S2 p.133, verbatim): *"Image files are imported with each
row of pixels as the wave cycle, with one slice per row."* Also: audio files are sliced by an
explicit sample count encoded in the filename after an underscore — `MySweep_128.wav`.

### 3.2 ⭐ The **Additive** oscillator (S2 pp.120–121) — real formulas, real ranges
Header: *"inspired by classic subtractive synthesis, with additive twists like partial stretching,
frequency shifting, fractional order filtering, even/odd harmonic control, continuous morph from
square to saw and more."*

**MAX PARTIALS** — *"For example an A2 note has 200 harmonics for a sampling frequency of 44.1 kHz…
control the amount of CPU which is proportional to the number of partials."*

**FREQUENCY section:**
- **DISSONANCE** — stated verbatim: *"disturbs the harmonic series according to the law
  `fn = f * (1 + n * dissonance)`"* with these landmarks, all quoted:
  - `100%` → partials are harmonics: `f · (1 2 3…)`
  - `200%` → only odd harmonics: `f · (1 3 5 7)`
  - `50%` → *"harmonic but interlaced with the odd partials of its (missing) sub-octave:
    `f·(1, 3/2, 2, 5/2, 3) = f·(1 2 3…) + f/2·(3 5 7…)`"*
  - irrational amounts → inharmonic

  🔑 **This is algebraically identical to Terrain's `HarmonicStretch`.** Terrain:
  `r' = 1 + (r−1)·s`, `s = 1 + 5.5a` (`SpectralMorph.h:122`). With `n = r−1` (0-based partial
  index) that is `r' = 1 + n·s` — Falcon's law exactly, with `dissonance ≡ s`.
  **But the ranges differ, and that is the finding:** Terrain's `s` spans **[1, 6.5]**. Falcon's
  spans **[0, 2]+**. Terrain therefore **cannot reach `s < 1`** — the sub-octave interlace at
  `s = 0.5` and everything below it is unreachable. And Terrain's musically important point,
  `s = 2` (odd harmonics only, square-flavoured), sits at `a = (2−1)/5.5 = **0.1818**` — 18 % of
  the knob, with no detent and no label. (Terrain's amplitude tilt `amp *= r'^(1.15a)` still
  applies, so the *ratios* coincide at that point, not the levels.)

- **FREQUENCY SHIFT** — *"transposes the spectrum by a fixed amount in Hertz, making all the
  partials inharmonic."* INFERRED: `f_n' = f_n + Δ`, Δ in Hz, note-independent — genuinely
  different from a pitch shift, and *different again* from Dissonance.
- **STRETCH** — *"the amount of inharmonicity (partial stretching)… similar to the one present in
  stiff-stringed instruments like the piano or the guitar."* Formula not given.
  **⚠️ ALREADY IN TERRAIN.** `HarmonicEngine.h:714-731` **SPLAY** is exactly this and ships the
  standard stiff-string law in source: `str = sqrt(1 + B·max(0, n² − anchor²))`, `B` up to
  **0.138**, with a *fractional anchor* de-snap so partials near the anchor blend in smoothly.
  Do not build it twice.
- **HARMONICS SHIFT** — *"simulates transposition of the spectrum up to **+48 semitones**, but
  forces the resulting partials to stay in harmonic relation with the fundamental frequency. This
  can be compared to (soft-)hard-sync of analog oscillators."*
  🔑 That is **formant shift with harmonic re-snapping** — shift the spectral *envelope*, keep the
  partials on the integer grid. It is the correct, note-tracking answer to what Terrain's `Vocode`
  mode fails at (hard-wired F0 = 130.81 Hz that does not track the note, `SpectralMorph.h`).

**TIMBRE section:**
- **SLOPE** — three quoted anchors: default = **1/f** decay (sawtooth); **+100 % = flat spectrum**,
  a unipolar pulse train; **−100 % = 1/f²**, a parabolic wave (a triangle if even harmonics are
  also removed).
- **EVEN/ODD** — *"+100 % removes even harmonics and generates a square wave by default; −100 %
  removes odd harmonics and results in the same waveform at the octave e.g. (2f 4f 6f…) =
  2(f 2f 3f…)."* Continuous between.

**COMB/PWM section:**
- **FREQUENCY** — *"the relative frequency of a **comb filter applied to the harmonic series**…
  useful to simulate PWM by sweeping the frequency of notches in the spectrum"*; **DEPTH** — "the
  amount of cancellation". A comb in the *spectral index* domain, not a time-domain delay.

**FILTER section — the standout:**
- **ORDER** — *"adjust the order / slope of the filter. This includes **fractional filter slopes
  from 0.0 to order 8.0 (48 dB/octave)** that can not be achieved with traditional filters."*
- TYPE — Butterworth LP/BP/HP (no Q) and Resonant LP/BP/HP.

  🔑 Fractional order is only possible because you are multiplying per-partial magnitudes:
  `|H(f)|^order` for any real `order`. It is *free* in a bake and *impossible* in a biquad.

**UNISON:**
- **BEATING** — *"when Unison is activated, partials are shifted by a fixed amount in Hertz. This
  results in natural built-in Amplitude Modulation (with no LFO involved) whose frequency can be
  controlled by the Beating frequency. **(Only possible with additive synthesis)**"* — **VOICE**
  class: needs free-running per-partial oscillators. Not portable to a looped single-cycle table.

**GLOBAL:**
- **KEEP BASS** — *"forces the fundamental frequency of the oscillator to be preserved. Some
  spectral modifications like Comb or HarmShift may result in cancellation of the fundamental…
  SafeBass retains the fundamental frequency as a reference point while the remaining parts of the
  spectrum are being processed."*
- **RAMP TIME** — *"the Ramping time between amplitude changes."* i.e. a smoothing time constant
  on spectral change — directly analogous to Terrain's 20 Hz bake cadence.

### 3.3 IRCAM Stretch / Scrub — **Remix** (S2 pp.117–118)
*"Remix mode separates the signal into 3 discrete components and allows you to mix and automate
their levels. The 3 signal components are **SINE (harmonics), NOISE and TRANSIENTS**."*
Analysis params exposed: **WINDOW** (grain size — *"optimally set proportional to the fundamental
of the sample, with grain size twice the duration of the fundamental"*), **PADDING** (oversampling
×1/×2/×4), **OVERLAP**. Options: Transients / Envelope / Stereo (phase-locks channels) / Shape /
Legato preservation.
This is a phase-vocoder / SMS-style decomposition. **VOICE/offline class** — real numbers for
window and overlap, but not portable to Terrain's rectangular single-cycle path.

### 3.4 What is **unusual** in Falcon
1. **A stated formula in the manual** (`fn = f(1+n·dissonance)`) with musically-named landmarks at
   50 / 100 / 200 % — the parameterisation Terrain's HarmonicStretch already implements but hides.
2. **Fractional filter order 0.0–8.0.**
3. **Harmonics Shift** = formant shift *with* harmonic snapping ≡ soft sync.
4. **Keep Bass** as a global guard against spectral ops that cancel the fundamental.
5. **A spectral-domain comb** used deliberately as PWM.
6. **Image files → wavetables**, one pixel row per cycle.
7. **Sine / Noise / Transient remix** as three faders.

---

## 4. u-he — THREE GENERATIONS OF THE SAME IDEA

u-he has been shipping spectral oscillator effects since Zebra2 (2007). This is the deepest
catalogue in the survey and the closest in *architecture* to Terrain, because u-he also
**recalculates the waveform on a timer instead of per sample**.

### 4.1 Zebra2 — 24 "spectral effects", 2 in series (S3 pp.32–34)
*"The oscillator waveform can be processed by a couple of spectral effects, which are routed in
series (left > right)… When modulated, the speed and smoothness of most spectral effects DEPEND on
the Resolution setting."*

🔑 **The Resolution parameter is the direct architectural comparison for Terrain** (S3 p.35):
*"Controls the time between successive waveform calculations, ensuring that Zebra2 is still very
CPU-efficient compared with other synthesizers that calculate their waveforms in realtime. The
range is from **4 seconds (at 1.00) to below one millisecond (at 9.00)**… For most purposes, the
default value of **5.00** is best."*
Terrain's equivalent is a hard-coded **20 Hz = 50 ms**, i.e. somewhere near Zebra2's mid-scale, and
it is not user-adjustable. Zebra2 also warns that *low* resolution can be *better* — "intermediates
are smoothly interpolated".

Also on p.35: **Norm** — RMS analysis, low-level waves boosted so the result would be 0 dB at 100 %;
and **Renderer** — "soft or crisp", crisp trading aliasing for spikes.

**The full list of 24** (S3 pp.33–34, descriptions verbatim/condensed):

| Effect | What the manual says |
|---|---|
| **Fundamental** | Level of the fundamental. **Range −200 % (inverted) to +200 %.** At centre the fundamental is inaudible. |
| **Odd for Even** | *"Even-numbered harmonics are **cross-faded into** odd harmonics."* Negative = the opposite. |
| **Brilliance** | Boosts (+) / attenuates (−) higher harmonics. |
| **Filter** | LP (−) / HP (+). *"Because in reality the 'filter' code only manipulates amplitudes, its slope is **more than 100 dB/octave**."* |
| **Bandworks** | Bandpass (+) / notch (−). |
| **Registerizer** | Boosts octaves of the fundamental, attenuates all others → organ. |
| **Scrambler** | Phase of the waveform modulated by the wave itself (operator feedback). |
| **Turbulence** | *"Periodically **shuffles the harmonics at random**. Even if not modulated, the speed of this effect is dependent on the oscillator Resolution."* |
| **Expander** | Expands (+) / contracts (−) the spectrum. |
| **Symmetry** | Contracts the waveform toward the start or end of its cycle (PWM for a square). |
| **Phase Xfer** | The wave becomes the phase response of an extra sine (PD variant). |
| **Phase Root** | The wave **multiplies** the phase response of the sine. |
| **Trajector** | The wave **adds to** the phase response of the sine (≡ FM/PM). |
| **Ripples** | *"**Multiplies the waveform** with a variable harmonic"* → quasi-resonant. |
| **Formanzilla** | *"**Multiplies the spectrum** of the waveform with a variable harmonic, resulting in formant-like spectra with several strong peaks and troughs."* |
| **Sync Mojo** | Simulates hard sync by contracting the time axis and writing back into wave memory. |
| **Fractalz** | *"Like Sync Mojo, except that the already contracted wave is contracted again etc. This results in a **fractal waveform** with even more harmonics than Sync Mojo."* |
| **Exophase** | A classic **7-stage phaser** applied to the wave itself. |
| **Scale** | *"The relative amplitudes of harmonics are scaled, either to the **power of 2** (negative, softer) or **3** (positive, brighter). Results in finer resolution of quiet harmonics."* |
| **Scatter** | Like Scrambler but the phase is modulated by itself **squared**. |
| **ChopLift** | *"Negative values raise an **amplitude threshold below which harmonics are faded out** (Chop). Positive values **raise levels of fainter harmonics** (Lift)."* |
| **HyperComb** | *"Adds **3 copies of the original wave** to the wavetable. For positive values the phases are randomly shifted, resulting in a subtle to dramatic effect similar to chorus."* Resolution-dependent. |
| **PhaseDist** | Casio CZ phase distortion; amount crossfades no-effect ↔ full. |
| **Wrap** | Inverts parts of the wave above/below a threshold; multiple wrapping range is larger for negative values. |
| **DX** | *"Same as Trajector, but approximately **10 times stronger**."* |
| **Smear** | *"**Blurs the spectrum in one direction** (negative = down, positive = up)."* |

🔑 **`Smear` is directional and bipolar.** Terrain's `Smear` is a *symmetric* triangular blur over
partial index, `W = round(amount·11)` (`SpectralMorph.h:203`). One sign bit separates "shimmer/air" from
"growl", and Terrain does not have it.

**SpectroMorph vs SpectroBlend** (S3 pp.38–40) — u-he draws a distinction Terrain does not:
- **SpectroMorph** — *"1023 harmonics in the horizontal axis are scaled logarithmically for a total
  range of about 10 octaves"*; breakpoints **move**, so the morph is a *warp of the envelope*.
- **SpectroBlend** — *"The spectrum is represented by **128 bipolar columns**… scaled **linearly**
  for a total range of six octaves. The lower half is '**anti-phase**', so the same harmonic in
  adjacent waves but with **opposite phases can cancel each other out**… The main advantage of
  SpectroBlend is the total control over individual harmonics, **including polarity**. Waves are
  not morphed in this mode, they are **blended**."*

  🔑 u-he makes cancellation a *feature* by giving the spectrum a sign, and names the two behaviours
  differently on purpose: **morph = move the breakpoints; blend = crossfade the values.** Terrain's
  `renderBlend` is unambiguously a **blend** (a convex phasor average, so magnitude ≤ weighted mean;
  no new partials possible — proved in `00-INVENTORY.md` §3) but it is documented under the name
  "morph". That naming needs fixing before either is extended.

### 4.2 Zebralette 3 — the current state of the art (S4, v3.0 Dec 2025)
Two sources, two renderers, 22 oscillator effects, 8 spectral modifiers.

**Source** (S4 p.12): `Curve Geometry` (the curve is the waveform) or **`Curve Spectrum`**
(*"the curve represents the harmonic spectrum: **1024 harmonics are scaled logarithmically for a
range of about 10 octaves**"*).

**Renderer** (S4 p.12): `Wavetable` (classic, updated at the Resolution rate) or **`Additive`**
(*"Reproduces the spectrum with the number of partials specified by the Harmonics parameter. As
these are **free running and independently tunable**, they can be processed to create inharmonic
sounds"*).

**Real CPU numbers, quoted (S4 pp.12–13):**
- **Harmonics: 16 to 1024**, *"the default **256** is the recommended maximum unless you can hear a
  significant improvement at higher values."*
- **Resolution: 200 Hz, 800 Hz or 2000 Hz** — *"the density of waveform calculations… how often they
  are updated per second."* **No effect in Additive mode.**
- **Maths: Precise / Fast / Rough.**
- Unison 2–16, *"processed in blocks of 4, so CPU-usage does not rise linearly: Unison = 8 is no
  more CPU-intensive [than 5]."*

📌 Compare directly: **u-he offers 200 / 800 / 2000 Hz table rebuilds. Terrain does 20 Hz and pays
21 ms per rebuild.** u-he can afford 2 kHz because it renders **one** waveform, not 34 mips × 16
frames. That difference — 544 iFFTs vs 1 — is the whole reason Terrain's spectral is a
patch-level parameter and u-he's is a modulation-rate one. It is the ceiling on every idea below.

**The 8 Modifiers** (S4 pp.13–14, Additive renderer only; strength = the **Spectral Distortion**
knob; *"All modifiers are applied relative to the fundamental… the fundamental is not affected"*):

| Modifier | Verbatim behaviour |
|---|---|
| **Expansion** | *"Stretches harmonics up one octave. At maximum Spectral Distortion the result is **odd-numbered harmonics only**."* |
| **Compression** | *"Compresses all harmonics down towards the fundamental."* |
| **Curve** | *"Shifts overtone pitches according to the Guides or Curve Set… **Negative Y values bend pitches down towards the fundamental, positive values bend them upwards. The range increases with the harmonic index, peaking at about ± an octave.**"* Harmonic Grid view; each vertical gridline = one harmonic. |
| **Harmonic Clusters** | Organises the spectrum into equally spaced clusters via **Cluster Select**, a 0–100 knob over a *discrete table crossfaded between entries*: `0` Even · `10` Odd · `20` every 3rd from the 2nd · `30` every 3rd from the 4th · `40` every 4th from the 2nd · `50` every 4th from the 5th · `60` every 5th from the 2nd · `70` every 5th from the 6th · `80` every 6th from the 2nd · `90` every 6th from the 7th · `100` every 7th from the 2nd. *"Intermediate values are crossfades: the **pitches of clusters are shifted while their relative tuning remains intact**."* |
| **Log Clusters** | *"Similar to Harmonic Clusters, but instead of clusters being spaced evenly across the spectrum, they are distributed to ensure **equal energy across the spectrum**. Starting with **3 clusters**, at maximum level **10 clusters are spaced precisely octaves apart** – great for bells or organs."* |
| **Chaos Patterns** | *"Reorganises harmonics into random patterns."* `Random Seed` selects **one of 100 preset patterns**. `Distortion Range`: **Full Spectrum** · **One Octave** (±1 oct per harmonic) · **Four Octaves** (±4 oct) · **Ordered** (*"the frequency of each harmonic is juggled up or down, but **cannot cross paths with neighbouring harmonics: the order is preserved**"*) · **One Harmonic** (*"each harmonic is randomly shifted toward neighbouring harmonics only… lets you sequence or modulate randomness during playback: good for cymbals"*). |
| **Wild Randomness** | Same as Chaos Patterns but **resampled at Note On**, no repeats. **VOICE class.** |
| *(and the **Noise** knob)* | *"Spectral chaos effect. Tip: Low to medium values guard against unwanted rapid 'beating' created by Spectral Distortion."* |

**The 22 OSC FX, in four named families** (S4 pp.18–22). Full menu order:
`none` · **Spectral:** Curve Filter · Filter · Formant · Sparse · Spectral Focus · Tone Works ·
**Warping:** Delta X · Map-o-Matic · Phase Distortion · Scrambler · Symmetry · Sync · Wrap & Zap ·
**Windowing:** Dual Wave · Window · Zoom · **Animation:** Dissociate · Posterize · Spectral Decay ·
Spectral Noise · Twinkles

The ones with no equivalent anywhere else:

- **Curve Filter** — a *drawn* filter response over **10 octaves**; the left/right endpoint heights
  set the response outside the drawn range; a Frequency knob shifts the whole curve over
  **~20 Hz – 20 kHz** with *"about 50 % key follow (slightly less for higher notes)"*.
- **Formant** — *"Similar to Curve Filter except that the source attenuates partials within a
  **fixed spectrum**: the lines of the harmonic grid represent **overtones of 20 Hz** here, with a
  maximum close to 20 kHz."* — i.e. the curve is pinned to *absolute* frequency, so it behaves as a
  formant filter under pitch change. Curve Filter is 50 % key-tracked; Formant is 0 %.
- **Sparse** — *"Randomly generates **gaps** in the spectrum. Depth controls **the number of gaps as
  well as how strongly they are attenuated**."* (one knob, two coupled dimensions)
- **Spectral Focus** — 🔑 *"**Odd**: Reduces even harmonics, **boosting adjacent odd harmonics**.
  **Even**: Reduces odd harmonics, boosting adjacent even harmonics (as the fundamental is an odd
  harmonic, its level is also reduced). **Octaves**: Reduces harmonics that are not octaves of the
  fundamental, **boosting adjacent ones that are**. Turns a sawtooth into an organ-like waveform.
  **Fundamental**: the level of the lowest harmonic, **from zero to about 150 %**."*
  This is **energy redistribution, not attenuation** — the removed partial's energy is *deposited on
  its neighbour*. That is why it does not get quieter as you turn it up.
- **Tone Works** — four tonal controls: **Brilliance** (boost upper), **Smoothness** (attenuate
  upper), **Compression** (*"boosts quieter overtones"*), **Expansion** (*"attenuates quieter
  overtones"*). A **compander across the spectrum**.
- **Map-o-Matic** — applies a drawn Guide/Curve as one of four maps:
  **RePhase** (*"the Phase of the waveform is remapped to the source. A rising sawtooth plays the
  waveform as-is, a falling sawtooth plays it in reverse, and a triangle plays it forwards then
  backwards within a single cycle"*) · **Phase Offset** (*"about **20 times stronger** than
  RePhase. A horizontal line in the centre results in silence"*) · **Value Grade** (*"the **levels
  of harmonics** are remapped to the source"*) · **Curve Distort** (source as a waveshaper curve).
- **Window** — applies a curve as an envelope over one cycle, with a polarity switch:
  `+` unipolar (sub-zero values drag the wave to −1) vs `+ –` bipolar (sub-zero values drag toward
  0). Manual's use: *"remove the usual grunge from Sync sweeps by fading the left and right edges
  out."*
- **Zoom** — Depth = zoom factor, **Center = the point along the curve that stays fixed**.
- **Dual Wave** — appends a second curve within the same cycle; Depth = relative sizes of the two.
- **Spectral Decay** — *"Uses a Guide or Curve Set to make harmonics **decay differently: high
  values along the curve mean longer decay**."* **VOICE class** (decay is measured from note-on).
- **Twinkles** — *"Random overtones. The **Trigger Source 'pings' an overtone each time it leaves
  zero in the positive direction**. Depth controls how slowly the overtones decay."* **VOICE.**
- **Dissociate** — *"Independently shifts the phases / pitches of partials… **the phases of partials
  are random per note, even if oscillator Phase is set to Reset**."* **VOICE.**
- **Posterize** — *"Like a **lowpass filter applied to wave morphing**: Depth determines the
  smoothness of transitions, and the Trigger Source effectively applies a **sample & hold** to the
  waveform."* 🔑 A filter on the *morph axis* rather than on the audio.

**Guides** (S4 p.32) — three global curves, crossfaded by a Morph knob at fixed positions
(*"Guide 1 = 0.00, Guide 2 = 50.00, Guide 3 = 100.00"*), usable as the source for Curve Filter,
Formant, Map-o-Matic, Dual Wave, Window and Spectral Decay. p.13: *"the oscillator guides are also
available as **CPU-friendly shapers**"* — with Guide 2 centred it is *"very CPU efficient"*, while
*"[using a curve for] an oscillator effect will increase CPU usage considerably, as these curves are
calculated at [audio rate]"* (S4 pp.19, 27).

**Curve↔Guide algebra** (S4 p.31) — a set of *set operations* between the drawn curve and a guide:
`Move Points Up To Guide` · `Scale Curve Below Guide` · `Scale Curve Above Guide` ·
`Cut Away Curve Above Guide` · `Cut Away Curve Below Guide` · `Replace Curve With Guide` ·
`Skew Curve With Guide` (*"the Guide is added to the Curve, skewing it vertically"*).
*"Points are automatically added or removed wherever necessary."*

### 4.3 Hive 2 — the **Interpolator**, and the 2D table
(S5 pp.43–45.) Hive's oscillator is otherwise unremarkable; two things are not.

🔑 **Interpolator — four algorithms for moving between frames** (S5 p.45, verbatim):
> `switch` — no interpolation at all, sudden jumps between frames
> `crossfade` — smoothly interpolates waveform magnitudes
> `spectral` — like crossfade, but **also interpolates the phases of each partial**. CPU-hungry!
> `zero phase` — like spectral, but **also forces the phase of each partial to zero first**
>
> *"The spectral and zero phase modes shift the relative phases of partials differently… As
> blending different phases requires extra computation, spectral is actually the highest quality
> mode, and therefore the most CPU-intensive. Tip: The CPU-friendly crossfade is usually best."*

This is **the direct answer to Terrain's `renderBlend` limitation.** `00-INVENTORY.md` §3 proved
Terrain's blend is a convex phasor average — per bin a weighted vector sum, so magnitude can only
*shrink*, and the code then papers over the loss with an RMS match `G = √(Σref²/Σpre²)`. Hive names
the three alternatives and ships them. See §9.1 for the version of this that costs Terrain **zero**
audio-thread cycles.

**Tables × Position — a 2D wavetable** (S5 p.44): a `Tables` parameter (1, 2, 4, …) reshapes the
frame list into a grid; the main `Position` knob scans the x-axis, a second `Multi Position` knob
crossfades the y-axis. *"Things can get rather interesting if you set Tables to a value that
doesn't divide the number of frames evenly."* Note: *"the interpolation through Multi Position is
always crossfade"* — only the x-axis gets the Interpolator.

### 4.4 ⭐ UHM — the wavetable **scripting language** (S6)
`.uhm` files are text programs that *bake* wavetables. This is the closest thing in the industry to
Terrain's own `WavetableSpec → buildFromSpec` pipeline, and it is fully documented.

**Format facts** (S6 p.2): 1–256 frames, **2048 samples per frame**; `.wav` tables default to 2048
samples/frame with the count inferred from file size, capped at 256 frames, and a
`-WT<n>` filename suffix (`64, 128, 256, 512, 1024`) overrides the cycle length. Two auxiliary
buffers (`aux1`, `aux2`) act as *"the memory functions (M+, MR) in a calculator"*.

**The three parser commands are the architecture** (S6 pp.5–6):

| Command | Domain | Range args |
|---|---|---|
| `Wave` | **time domain** | `Start`/`End` frame, `Blend`, `Direction`, `Target` |
| `Spectrum` | **frequency-domain magnitudes** | + `Lowest=N` (default **1**, may be **0 = DC**), `Highest=N` (default **1024**) |
| `Phase` | **frequency-domain phases** | + `Lowest` (default **1**; *"1 is also the lowest possible, as DC has no phase information"*), `Highest` (default **1023**) |

Plus `Import` · `Export` · `Move` · **`Interpolate`** · `Normalize` · `Envelope`.

🔑 **`Interpolate`** (S6 p.7) — the one nobody else ships:
> `Type=X`, where X is `switch`, `crossfade`, `spectrum`, `zerophase`, **`morph1`, `morph2`**
> `Snippets=X` **(1–500)** maximum number of fragments to be morphed *(Morph1/2 only)*
> `Threshold=X` **(−120 – 0)** threshold for identifying snippets *(Morph1/2 only)*
> `Weighting=X` where X is `none`, `distance`, `level` or `both` *(Morph1/2 only)*
>
> *"Details of the 'morph' types and the last 3 options here will be explained at a later date."*

INFERRED (and I am confident, because the parameter names are the standard ones): `morph1/morph2`
implement a **peak/partial-matched morph**. Spectral peaks above `Threshold` dB are identified as
"snippets" (≤ `Snippets` of them), matched between the two endpoint frames by a cost function
weighted by frequency **distance** and/or **level**, and each matched pair is then *glided in
frequency and amplitude* rather than crossfaded. Unmatched peaks fade in/out. This is the classic
Serra & Smith sinusoidal-modelling morph (Serra & Smith, *"Spectral Modeling Synthesis"*,
Computer Music Journal 14(4), 1990), applied **offline, at bake time**. u-he does not document the
matching cost; the parameter names are the evidence.

**Normalize** (S6 p.7): `Metric = RMS | Peak | Average | Ptp`, `dB` target (default 0.00),
`Base = All | Each`.

**Blend modes** (S6 p.10) — 🔑 note the last line, verbatim:
> `replace` · `add` · `sub` · `multiply` · `multiplyAbs: (x + fabs(y))` ·
> `divide: sign(x*y)*(1-fabs(x))*(1-fabs(y))` · `divideAbs: sign(x)*(1-fabs(x))*(1-fabs(y))` ·
> `min: minimum, the smaller absolute value` ·
> **`max: maximum, the larger absolute value (good for emulating formants when used with Spectrum)`**

**Other primitives:** `lowpass/bandpass/highpass(x, cutoff, resonance)` with cutoff and resonance
both normalised **0–1**; `env(x)` from an 8-segment `Envelope` with
`Curve = linear | exponential | logarithmic | quadric`; the sample accessors
`main_fi(frame,index)` / `main_fp(frame,phase)` / `aux1_*` / `aux2_*`; three RNGs distinguished by
scope — **`rand`** (per operation), **`randf`** (per frame), **`rands`** (per sample) — all driven
by one script-level `Seed`.

### 4.5 What is **unusual** at u-he
1. **The Interpolator is a first-class choice** (switch / crossfade / spectral / zerophase), and
   the bake language adds **peak-matched morph1/morph2** with real ranges (Snippets 1–500,
   Threshold −120…0 dB, Weighting none/distance/level/both).
2. **`max` as a spectral blend mode**, explicitly *"good for emulating formants"* — the only
   documented way to combine two spectra that **adds** partials instead of cancelling them.
3. **Energy-redistributing parity** (Spectral Focus, Odd for Even) instead of energy-destroying.
4. **Directional Smear.**
5. **Two "spectral compander" designs** (ChopLift bipolar; Tone Works Compression/Expansion).
6. **Curve-driven per-partial pitch bend** whose range *grows with harmonic index*, peaking at
   ±1 octave.
7. **Chaos Patterns' "Ordered" range** — a random re-tuning that is forbidden from reordering the
   partials.
8. **Posterize** — a lowpass on the *morph axis*.
9. **The waveform calculation rate is a user parameter** (Zebra2 4 s…<1 ms; Zebralette 3
   200/800/2000 Hz).

---

## 5. ABLETON — thin in the synth, deep in the effects

### 5.1 Wavetable (S8, §30.13) — the minimalist end of the design space
Verbatim: *"As long as no modulation is applied, the raw output of the oscillators is perfectly
band-limited and will not produce aliasing artifacts at any pitch."*

**Exactly three oscillator effects, each with exactly two sliders**, and — a nice UI law —
*"the values of the two effects parameters don't change when the effect type changes. This makes
it possible to move between the effects to experiment with how the different processes affect the
timbre with the same values."*

| Effect | Params | Verbatim |
|---|---|---|
| **FM** | `Amt`, `Tune` | *"With a tuning of **50 % (and −50 %)**, the modulation oscillator is **one octave** higher (or lower). At **100 % (and −100 %)**, the modulation oscillator is **two octaves** higher (or lower). In between these values, the modulation oscillator is at **inharmonic ratios**, which is ideal for creating noisy overtones."* |
| **Classic** | `PW`, `Sync` | *"PW adjusts the pulse width of the waveform. Note that in hardware synthesizers, it is normally only possible to adjust the pulse width of square waves. **In Wavetable, the pulse width can be adjusted for all wavetables.** Sync applies a 'hidden' oscillator that resets the phase of the audible oscillator."* |
| **Modern** | `Warp`, `Fold` | *"Warp is similar to pulse width, and Fold applies wavefolding distortion."* |

**Raw mode** — *"Wavetable will automatically process imported samples to reduce unwanted
artefacts. Note that you can **bypass this processing** by activating the Raw mode switch… it can
also be 'misused' to create unpredictable, noisy or glitchy sounds."* A deliberate
**defeat-the-cleanup** switch on the import path.

Sub oscillator `Tone`: *"At 0 %, the oscillator produces a pure sine wave. Turning Tone up
increases the harmonic content."*

There is **no spectral morph of any kind** in Ableton Wavetable. Position interpolation is not
described in the manual (see §11).

### 5.2 Meld — `Shepard's Pi` (S8, §30.8.3)
Of Meld's **24 oscillator types × 2 macros each**, one is spectral:
> *"The **Shepard's Pi** oscillator has two macro knobs, **Rate** and **Width**. Rate changes the
> speed and direction of the oscillator. Values **0.0 through 49.9 produce falling** movements, and
> values **50.1 through 100.0 produce ascending** movements. **At 50.0, no movement is produced.**
> Width changes the **number of octaves** being used by the oscillator."*

A real, shipped Shepard with a bipolar-about-50 rate and an octave-count width. Compare Pigments'
`Phi` (§2.4) and Vital's `kShepardTone` (§8).

### 5.3 Spectral Resonator (S9, §28.36) — per-partial *modulation modes*
An FFT-domain resonator, tunable internally or by MIDI sidechain, up to **16 voices**.

- **Stretch** — *"adjust the spacing between the resonant harmonics. Values below 0 % compress the
  distance, while values above 0 expand it. **At 100 %, only odd harmonics are produced**, which
  leads to a square-wave type sound."* (Same family as Falcon Dissonance and Zebralette Expansion;
  the *odd-only at max* landmark recurs in all three.)
- **Shift** — transposes the spectrum of the **input** by **±48 semitones** (explicitly *not* the
  resonator's own spectrum).
- **Harmonics** — number of resonant harmonics; *"when using polyphony, the number of harmonics is
  **evenly distributed between the voices**"*; *"Increasing harmonics also increases CPU usage."*
- **Quantize** — *"each harmonic is quantized to the active scale or tuning system"* (chromatic if
  none). 🔑 A **scale-quantised spectrum** — the partials are snapped to musical pitches, not to
  the harmonic series.
- **HF Damp / LF Damp** — damping whose affected range **shifts with the current pitch**.
- **Pitch Bend Range** 0–24 st; **Transp.** ±48 st or ±28 scale degrees.

🔑 **Four modulation modes, applied per partial** (verbatim):
> `None` · `Chorus` — *"applies **triangle wave modulation to each partial**. When Mod Rate is set
> to 0, this mode only modulates the amplitudes of the partials."* · `Wander` — *"uses **random
> sawtooth waveforms** as the modulation source for each partial."* · `Granular` — *"modulates the
> amplitude of all partials randomly, using **exponential decay envelopes**. Partials are generated
> at irregular intervals, and the **Mod Rate parameter affects their density**."*
> *"Pch. Mod adjusts the range of pitch modulation **in semitones**… pitch modulation is **bipolar
> in all modes except Granular**, where the grain envelopes are only applied in a positive
> direction."*

**Unison** — *"detuned copies of the resonator's partials"*; in Wander and Granular *"each voice is
modulated independently."*

### 5.4 Spectral Time (S9, §28.37) — per-bin delay
Freezer + spectral Delay in series (order reversible via `Frz > Dly` / `Dly > Frz`).
- Freezer: Manual or Retrigger; Retrigger by **Onsets** (Sensitivity 0–100 %) or **Sync**
  (Interval in ms or beats); fade shapes **Crossfade** (X-Fade as a % of the sync interval) or
  **Envelope** (Fade In / Fade Out in ms, *"up to **eight simultaneous freezes** can be stacked"*).
- Delay: `Time` (ms / Notes / 16th / 16th-triplet / 16th-dotted), `Feedback`,
  **`Shift`** (*"Each successive delay will be shifted up or down by the specified frequency
  amount"* — a *cumulative* frequency shift per repeat), **`Tilt`** (*"skews the delay times for
  different frequencies. Positive delays high frequencies more than low"*), **`Spray`**
  (*"distributes the delay times for different frequencies randomly within the given time range"*),
  **`Mask`** (*"limits the effects of Tilt and Spray to either high or low frequencies"*),
  `Stereo` (*"the width of the Tilt and Spray controls"*).
- **Resolution** — *"sets the resolution used to process the incoming signal. **Lower values reduce
  the overall latency at the cost of accuracy and fidelity**."* Plus a context-menu
  **Zero Dry Signal Latency** toggle. The manual never states the FFT size (see §11).

### 5.5 What is **unusual** at Ableton
1. **Per-partial modulation as a *mode menu*** (Chorus / Wander / Granular) — one enum that
   converts a static spectrum into a moving one, with a rate and a depth in semitones.
2. **Scale-quantised harmonics** (Quantize) — snap partials to the *musical* grid.
3. **Tilt / Spray / Mask** — frequency-dependent *time*, masked to a band.
4. **Raw mode** — a switch that turns the import cleanup **off** on purpose.
5. The **two-parameters-per-effect discipline**, with values persisting across effect changes.

---

## 6. NI MASSIVE X — ten modes, and a hard two-knob law

(S7 pp.57–73; S12 mirrors it.) *"Every mode features **two dedicated parameters** for real time
manipulation and additional settings [menus] to define the behaviour."* Tables hold
**2–128 waveforms** (S7 p.59). Eleven wavetable categories.

| # | Mode | Menus | The two knobs | Manual's mechanism |
|---|---|---|---|---|
| 1 | **Standard** | Phase Direction (Fwd/Bwd) · Polarity ± · Internal Phase On/Off | **Filter**, **Phase** | *"Similar to Spectrum mode in original MASSIVE, the Filter control is used to reduce the higher frequency harmonics… **By scanning through the set of band-limited waveforms that are usually assigned to specific pitches**, a low-pass filtering effect is achieved… although the algorithm behind it is different from a standard filter design."* |
| 2 | **Bend** | Bend Curve (Strong/Medium/Gentle) · Direction (Neutral/Up-Down/For-Back) | **Filter**, **Bend** | *"raise and lower the readout speed depending on the position within the wavetable. Some parts of the waveform are compressed and other parts are expanded."* |
| 3 | **Mirror** | *(none)* | **Bend**, **Ratio** | *"reads the wavetable back and forth. Exceeding a certain Ratio will force the waveform to be folded, producing a hard sync-style sound."* |
| 4 | **Hardsync** | Window (Hard/Soft/Grain) · Direction | **2nd Level**, **Ratio** | 2nd Level = *"the amplitude of **every second** resetted repetition of the cycle."* |
| 5 | **Wrap** | Window · Direction | **Filter**, **Ratio** | *"Similar to Hardsync but behaves differently under modulation… **Hardsync mode will create more pitch artefacts**"*; Ratio is centred. |
| 6 | **Formant** (Formant Capture) | Direction | **2nd Level**, **Formant** | *"Manipulates the waveform in such a way that the **amplitude of the signal does not change over altering pitches**"* — static formants across pitch. The Formant knob **re-introduces** the Mickey-Mouse effect. |
| 7 | **ART** | Window (Hard/**Bity**/Soft) · Direction (+ **FU-DB**, only here) · Body (Body/Nobody) | **Width**, **Pitch** | *"**A**rtificial **R**esonance **T**echnology… utilises **hard sync techniques and windowing to mimic a resonant filter**. The basic idea was to create **filters without filters**."* Width ≈ resonance (narrows/widens the impulse envelope), Pitch ≈ cutoff. `Body` adds *"the response of an artificial body"* (bass); `Nobody` gives *"the excitation response"* only. |
| 8 | **Gorilla** | K!ngs (King/Kong/Kang) · Ratio (×1…×6) · Internal Phase | **Over**, **Bend** | *"the type of bend applied to the phase"*; **Bend "creates the formants together with the Over control"**; *"A ratio of ×2 is recommended to achieve the prime Gorilla sound."* |
| 9 | **Random** | Mode (Fluid/Thunder/Divide) · P.Rnd (Pitch Random / Pitch Switch) | **Pos Jitter/Clk Div**, **Jitter** | Fluid: PosJ randomises the position reader, Jitter randomises f0. Thunder: PosJ randomises *and* **downclocks** the randomiser. Divide: PosJ downclocks the frequency randomiser — *"only every 10th cycle"*. |
| 10 | **Jitter** | Jitter Rate (**J1 = every cycle · J2 = every 32 cycles · J3 = every 128 cycles**) · P.Rnd | **Filter**, **Jitter** | *"introduces random deviations at the end of each cycle… **Frequency randomization is only happening synchronous to the start of a wave cycle**."* |

Two mechanisms deserve their own callout.

🔑 **6.1 — Standard mode's `Filter` IS the band-limited mip ladder, exposed as a musical control.**
NI is explicit: it works *"by scanning through the set of band-limited waveforms that are usually
assigned to specific pitches."* That is the anti-aliasing ladder being reused as a tone control.
Terrain already has a finer one than Massive X: `Wavetable.h:79`, a **34-level, sixth-octave**
ladder `kMipMaxHarmonics = {512, 456, 406, 362, 323, 287, 256, 228, 203, 181, 161, 144, 128, 114,
102, 91, 81, 72, 64, 57, 51, 45, 40, 36, 32, 29, 25, 23, 20, 18, 16, 8, 4, 2}` — 2^(−1/6) ≈ 1.122×
per step, already built, already in RAM (4.46 MB/table), already selected per block by
`mipLevelForPhaseIncrement()`. Zebra2 makes the same point about *its* amplitude-only "Filter":
*"because in reality the 'filter' code only manipulates amplitudes, **its slope is more than
100 dB/octave**"* (S3 p.33). A ladder filter is a brick wall with no resonance and no phase shift —
a sound no analogue-modelled filter can make.

🔑 **6.2 — "Internal Phase Off" turns the wavetable into a waveshaper.** Verbatim (S7 pp.63, 70):
> *"Selecting **Int off fixes the oscillator frequency at 0 Hz, bypassing the main phase and turning
> the oscillator into a waveshaper.** The shaper must be used in conjunction with the PM oscillators
> or the PM Aux bus. **The level of the PM oscillator and/or Aux input determines the amount of
> waveshaping, and the Wavetable Position and Filter parameters control the shape function.**"*

Available in Standard and Gorilla modes. The **PM Aux bus** *"opens up the ability… for any source
in the signal path to be used to apply phase modulation"* — so *any* signal in the synth can be run
through the wavetable as a transfer function.

**Also note the `Direction` menus**, which appear in five modes and carry a stated spectral
consequence: *"**Up-Down inverts every second cycle of the waveform. Flipping every second cycle
cuts out all even harmonics (2, 4, 6, 8 etc.)**"*; `For-Back` reads every second cycle backwards
(*"identical to Up-Down if the waveform is perfectly symmetrical"*); ART adds `FU-DB` = both.

### 6.3 What is **unusual** in Massive X
1. **The band-limited ladder as a playable filter** (Standard mode).
2. **The wavetable as a transfer function at 0 Hz**, driven by an arbitrary bus.
3. **ART** — resonance built from sync + windowing rather than feedback.
4. **The two-knob law** — ten wildly different modes, every one of them exactly two knobs plus
   menus. A discipline, not a limitation.
5. **Cycle-rate randomisation with explicit divisors** (J1/J2/J3 = 1 / 32 / 128 cycles) rather than
   a free-running LFO.

---

## 7. BITWIG — say the true thing: there is no FFT in the Grid

The Grid has **~231 modules** and **no FFT / spectral module**. Its only frequency-domain object is
the `Spectrum` *display*. A well-known community FFT resynthesiser (polarity.me, Oct 2025 — cited
as a **blog**, not a manual) is built out of sine/cosine ring modulators and lowpass envelope
followers, one per note rather than per bin, and its own author calls it *"not a
commercial-grade, highly precise VST."* **Bitwig's Grid is therefore not a source of spectral
technique.** It is a source of one **routing** idea.

**Spectral Suite** (S10) — four splitters that turn spectral analysis into **audio lanes**:

| Device | Splits into | Notable params |
|---|---|---|
| **Freq Split** | 4 adjacent frequency groups | Frequency Split (count) · Split Insertion Direction (←/→/↔) · **Crossfade Amount 0–50 %+** · Split Nudge · **Split Spin** (spectrum-relative sliding) · **Split Bend** (curves the pattern around a new midpoint) · **Split Pinch** (bunches at the midpoint / spreads to the sides) · Spectral Limiter Threshold |
| **Harmonic Split** | non-harmonics · harmonics **A** · harmonics **B** | 🔑 **Harmonics Pattern** — *"default `2` = odd/even split; `1` = fundamental only to A"* · Nonharmonic Sensitivity · **Maximum Harmonics** · Tilt · Low-cut / High-cut (narrow the tracking area) · Detection Threshold |
| **Loud Split** | Quiet / Mid / Loud | Higher & Lower Thresholds with per-threshold **Knee** · **Relative Loudness Mode** (*"treats the strongest band at any moment as 0.0 dB"*) · Rise/Fall Time **in blocks** · Tilt |
| **Transient Split** | Transients / Tones | Transient Type (`Percussive` / `Noise`) · Transients Decay & Tones Smoothing **in blocks** · Analysis Bias · Tilt Mode (`Standard` frequency-based / **`Contour`** mid vs highs+lows) |

The user guide states **no FFT size, band count or latency** for any of them (§11).

🔑 **7.1 — What is unusual here is the *plumbing*, not the maths.** Harmonic Split's
`Harmonics Pattern = N` sends every Nth harmonic to lane A and the rest to lane B, with
non-harmonic residue on a third lane. Bitwig's contribution to this survey is the idea that
**a spectral decomposition should end in separate signal paths, not in one knob.** Terrain already
owns the receiving end of that idea: the **Splitter** FX device (fb444 — lanes are one `LANE` param
plus fb351's unclaimed-slot rejoin, 16 device kinds, `kMaxSlots = 128`).

---

## 8. BOUNDARY NOTE — Vital, and what Terrain already copied

Terrain's seven mode *names* come from Vital. Read out of the open source
(S11, `src/synthesis/producers/synth_oscillator.h:100-114`), Vital ships **eleven**:

```
kNoSpectralMorph, kVocode, kFormScale, kHarmonicScale, kInharmonicScale, kSmear,
kRandomAmplitudes, kLowPass, kHighPass, kPhaseDisperse, kShepardTone, kSkew
```
plus a separate `DistortionType` enum (`:116-131`):
`kNone, kSync, kFormant, kQuantize, kBend, kSqueeze, kPulseWidth, kFmOscillatorA/B, kFmSample,
kRmOscillatorA/B, kRmSample`.

Mapping onto Terrain (`SpectralMorph.h:39-50`): `Vocode`, `HarmonicStretch` (= `kHarmonicScale`),
`InharmonicStretch` (= `kInharmonicScale`), `Smear`, `RandomAmplitudes` are Vital's.
`DataCompress` and `SpectralPhaser` are Terrain's own.
**Terrain did not take: `FormScale`, `LowPass`, `HighPass`, `PhaseDisperse`, `ShepardTone`,
`Skew`.** Their implementations belong in the Vital document, not here — I flag only that
**`ShepardTone` is in Vital, in Pigments (`Phi`) and in Meld (`Shepard's Pi`), which makes it the
single most widely shipped spectral idea Terrain lacks**, and therefore *not* a differentiator on
its own. §9 proposes it only as a component, never as the headline.

---

## 9. THE SHORTLIST — twelve moves, ranked by (distinctiveness × feasibility)

Every entry states: the source, the maths, the parameter, the **cost class** (§1), the
**collision** risk against what Terrain already ships, and the honest risk.

### ⭐ 9.1 — ZERO-PHASE FRAMES: make the existing blur *additive* for free
**Source:** Hive `Interpolator = zero phase` (S5 p.45); UHM `Interpolate Type=zerophase` (S6 p.7).
**The mechanism, and it is exact, not approximate:** `renderBlend` is a convex phasor average of
same-mip frames (weights sum to 1, `Wavetable.h:353-441`). Per harmonic bin it computes
`Σ wᵢ · Aᵢ e^{jφᵢ}` — magnitude shrinks whenever the `φᵢ` disagree, which is why the code has to
finish with an RMS match `G = √(Σref²/Σpre²)`. **If every frame is baked with identical phases,
`φᵢ` is constant and the phasor average collapses to `(Σ wᵢ Aᵢ) e^{jφ}` — a *magnitude* average,
algebraically.** No cancellation, no gain fudge, no runtime FFT.
**Parameter:** one per-osc switch on the bake — `Frame Phase: As-Is | Aligned`. (`Aligned` = set
all `phases[h] = 0` in `writeBack()`.)
**Cost: BAKE.** One `memset`-equivalent inside a function that already runs. Audio thread: **zero**
added cycles.
**Collision:** none. It changes `renderBlend`'s *inputs*, not `renderBlend`.
**Risk:** zero-phase frames are impulsive (all partials in phase = a peaky, cosine-stacked wave),
so the peak/RMS ratio rises and `blur = 0` sounds different from today. Must be a switch, not a
replacement, and it must be gated by measuring peak *and* RMS before/after per table.
**Why it is the top of the list:** `00-INVENTORY.md` proved Terrain's blend "cannot create new
partials". This is the one change that makes the statement false at zero audio cost, and it is
provable by arithmetic before a line is written.

### ⭐ 9.2 — PEAK-MATCHED FRAME MORPH (the thing nobody else in the wavetable world ships)
**Source:** UHM `Interpolate Type = morph1 | morph2`, `Snippets 1–500`, `Threshold −120…0 dB`,
`Weighting = none | distance | level | both` (S6 p.7) — u-he explicitly declines to document the
algorithm. Precedent: Serra & Smith, *Spectral Modeling Synthesis*, CMJ 14(4), 1990.
**The maths (INFERRED from the parameter names, stated as a proposal not a reading):** for adjacent
frames `F` and `F+1`, take the ≤`Snippets` partials above `Threshold` in each, build a matching
that minimises `Σ (w_d·|log₂(f_a/f_b)| + w_l·|dB_a − dB_b|)`, then for morph position `t` emit
partials at `f = f_a^{1−t} · f_b^{t}` (log-frequency glide) with `A = (1−t)A_a + t·A_b`. Unmatched
partials fade. **Terrain's `buildFromSpec` already snaps arbitrary ratios by splitting energy across
the two adjacent integer bins as a phasor sum (`Wavetable.h:126-155`) — a glided partial is exactly
what that code was written to render.**
**Parameter:** `Morph: Blend | Track` + `Track Depth`, or fold it into the existing `blur`.
**Cost: BAKE**, but it changes *where* the morph happens: today the frame axis is flattened on the
audio thread by `renderBlend` per block; a tracked morph must be resolved at bake time into
intermediate frames (u-he's `Interpolate` literally *fills in* frames 1–99 between 0 and 100).
Concretely: raise `WavetableSpec::kNumFrames` from 16, or bake the tracked frames into the same 16.
Matching cost is O(P²) on ≤512 partials worst case ≈ 260 k ops — **~0.2 % of the 21 ms bake.**
**Collision:** it *replaces* nothing; it is a second interpretation of the frame axis.
**Risk:** the real one. It only sounds like a morph if the source frames have *identifiable* peaks;
on noise-like tables it degenerates to a crossfade. Gate: prove on a 2-frame table (sine at h=3 →
sine at h=7) that the output partial actually *sweeps* through 4, 5, 6 rather than crossfading —
and prove the gate fails on today's code.

### ⭐ 9.3 — THE LADDER AS A FILTER ("Brilliance")
**Source:** Massive X Standard mode `Filter` (S7 p.63); Zebra2 `Filter` (S3 p.33, *">100 dB/oct"*).
**Mechanism:** bias the mip index chosen by `mipLevelForPhaseIncrement()` **upward only** (darker),
clamped to `[chosen, 33]`. The caps are already `{512 … 2}` at 2^(1/6) spacing (`Wavetable.h:79`).
**Parameter:** `Brilliance`, 0…1, mapped to `0…(33 − chosen)` steps, modulatable.
**Cost: READ** — one `int` add and a clamp, **per block, per osc**. Nothing else changes; the table
is already resident.
**Collision:** ⚠️ Terrain has a full filter section and a Filter FX device (94 engines). The
defence: this cannot resonate, cannot self-oscillate, has no phase shift, tracks pitch exactly, and
can only *remove* harmonics that the ladder already removed for other notes. Name it so nobody
reads it as a filter — `Brilliance` (Zebra2's word) or `Focus`. **Do not call it Filter.**
**Risk:** the ladder's top is coarse (`…20, 18, 16, 8, 4, 2` — the last three are octave jumps), so
the last few steps of the knob will be lumpy. Either stop the knob at index 30 (16 harmonics) or
accept and document the lumpiness. Also: it can only darken — brightening would alias. Say so in
the UI (the knob's default is its maximum).

### ⭐ 9.4 — WINDOWED MODES: every spectral op gets a partial-range
**Source:** Pigments Partial Shaper (`Position` + `Win Size`, S1 pp.124–126); Pigments Modal
`Range` (*"the first harmonic below which partials are no longer warped"*, S1 p.130); Falcon
`Keep Bass` (S2 p.121); Zebralette `Curve Filter` endpoints (S4 p.19); UHM `Spectrum Lowest/Highest`
(S6 p.5).
**Mechanism:** `SpectralMorph::apply()` already runs on a flat partial list from `extract()`. Add
two shared parameters — `Range Lo`, `Range Hi` (partial index, log-mapped) — and a
`smoothstep` edge so the effect fades in over ~2 partials at each boundary instead of clicking.
Every one of the seven modes inherits it.
**Parameter:** two knobs, shared across all modes, exactly as `Amount` is shared today.
**Cost: BAKE.** Two comparisons per partial.
**Collision:** none. It is orthogonal to all seven existing modes.
**Risk:** it multiplies the test matrix by the window (fb425's law: sweep the full matrix). Budget
for it: 7 modes × 3 windows × the existing amount sweep.
**Why it matters:** four different manufacturers arrived at the same conclusion independently —
a spectral effect applied to the *whole* spectrum is a blunt instrument. Terrain's seven modes are
all whole-spectrum today. This is the highest ratio of new sounds to new code in the document.

### 9.5 — ENERGY-CONSERVING PARITY ("Focus")
**Source:** Zebralette `Spectral Focus` — *Odd / Even / Octaves / Fundamental*, with the explicit
*"boosting adjacent"* clause (S4 p.20); Zebra2 `Odd for Even` — *"even-numbered harmonics are
cross-faded into odd harmonics"*, bipolar (S3 p.33); Pigments `Parity` (S1 p.123).
**Mechanism:** for `Odd`, move a fraction `a` of each even partial's *energy* to its lower odd
neighbour: `A_odd ← √(A_odd² + a·A_even²)`, `A_even ← √(1−a)·A_even`. Sum of squares is invariant,
so the sound does not get quieter as the knob turns — which is precisely Max's
*"params evolve 0→100, no dead knobs"* rule. Four targets: Odd · Even · **Octaves** (only 1, 2, 4,
8, 16… survive → organ) · Fundamental (0 → ~150 %, per S4).
**Cost: BAKE.** O(P).
**Collision:** ⚠️ `HarmonicEngine.h:732-748` **CULL** is a sieve chain (full → odd → primes →
Fibonacci) that *multiplies amplitudes* — it deletes, it does not redistribute. The distinction is
real and audible (CULL gets quieter and thinner; Focus stays loud and changes register), but the
two must not share a name and ideally should not share a page.
**Risk:** at `a = 1` with `Even` selected the fundamental is removed (u-he warns of exactly this);
pair it with a `Keep Bass` guard (§9.7).

### 9.6 — DIRECTIONAL SMEAR (one sign bit)
**Source:** Zebra2 `Smear` — *"Blurs the spectrum in one direction (negative = down, positive =
up)"* (S3 p.34).
**Mechanism:** Terrain's `Smear` blurs symmetrically, triangular over partial index,
`W = round(amount·11)` (`SpectralMorph.h:203`). Replace the symmetric kernel with a one-sided one selected by the sign of a
bipolar amount: energy leaks only upward (air/shimmer, and it *adds* partials above the source) or
only downward (growl, and it *fills in* below).
**Parameter:** make `SYN_OSC_x_SPECTRAL_AMT` bipolar **for this mode only** — or, cleaner given the
shared-parameter design, add a `Direction` menu that all modes may use.
**Cost: BAKE.** Same loop, asymmetric weights.
**Collision:** none — it is a change to an existing mode.
**Risk:** trivial. This is the cheapest audible win in the document.

### 9.7 — KEEP BASS (a guard, not an effect)
**Source:** Falcon `KEEP BASS / SafeBass` (S2 p.121) — *"Some spectral modifications like Comb or
HarmShift may result in cancellation of the fundamental frequency… SafeBass retains the fundamental
as a reference point while the remaining parts of the spectrum are being processed."*
**Mechanism:** capture `A₁, φ₁` before `apply()`, restore after (optionally blended by a
`Keep` amount). Terrain already does a special case of this in two places — `RandomAmplitudes`
protects `ratio ≤ 1.01`, and `HarmonicEngine::postRoot()` (`:809`) re-asserts the fundamental after every
sculpt — so the pattern is proven in-tree; it just is not global.
**Cost: BAKE.** Two floats.
**Collision:** it *generalises* two existing special cases. Do it once, in `writeBack()`.
**Risk:** none, provided it is defaultable OFF so existing presets do not change.

### 9.8 — FRACTIONAL-ORDER SPECTRAL TILT
**Source:** Falcon Additive `ORDER` — *"fractional filter slopes from 0.0 to order 8.0 (48 dB/oct)
that can not be achieved with traditional filters"* (S2 p.121); Zebra2 `Scale` (powers 2 and 3,
S3 p.34); Falcon `SLOPE` (1/f² … 1/f … flat, S2 p.120).
**Mechanism:** per partial, `A_h ← A_h · |H(f_h)|^order` with `H` a one-pole prototype and `order`
any real in `[0, 8]`. Continuous slope, brick-wall-free, no resonance, no phase shift.
**Cost: BAKE.** One `pow` per partial (≤512) — measured cost class **microseconds**.
**Collision:** ⚠️ `HarmonicEngine::KEEL` is a *pivot tilt* in dB/octave with a slope of up to
9 dB/oct and a moving pivot. A fractional-order filter and a tilt are close cousins. If both ship,
the difference must be that KEEL tilts the whole spectrum about a pivot (bipolar), while this one
is a *corner-frequency* roll-off whose steepness is continuous. Consider extending KEEL instead of
adding a mode.
**Risk:** honest — this may be a *refinement of something Terrain already has* rather than a new
mode. Check KEEL first.

### 9.9 — THE WAVETABLE AS A SHAPER (highest ceiling, highest risk)
**Source:** Massive X `Internal Phase = Off` (S7 pp.63, 70); Zebralette `Map-o-Matic → Curve
Distort` (S4 p.20); UHM's whole `Wave` command.
**Mechanism:** `readCycle(buf, φ)` is already the only read. Feed it a signal instead of the
oscillator's phasor: `out = readCycle(buf, 0.5 + 0.5·s)` where `s` is another osc, an FX bus tap,
or the input. **Every spectral morph mode instantly becomes a distortion character** rather than a
tone colour — the design space multiplies without a single new spectral mode.
**Cost: READ**, per sample — comparable to the existing `applyPhaseWarp` path.
**Collision:** ⚠️ against the Distortion FX device (23 modes / 6 families) and against WARP 9/10.
The differentiator is that the transfer function here is *the user's own baked spectrum*, which no
distortion device can be.
**Risk: this is the one with a real DSP cost.** A waveshaper's output is **not band-limited** —
the mip ladder protects the *oscillator*, not the *shaper*, so `readCycle` on an arbitrary input
will alias. It needs oversampling (Terrain already has ADAA machinery in `Shapers.h`) or a hard
constraint (band-limit the input, cap the mip). **Do not ship this without an aliasing measurement
on the real AU.** Prototype it as a WARP source before it becomes a feature.

### 9.10 — HARMONICS SHIFT (formant shift that snaps back to the grid)
**Source:** Falcon `HARMONICS SHIFT` — *"simulates transposition of the spectrum up to +48
semitones, but forces the resulting partials to stay in harmonic relation with the fundamental…
comparable to (soft-)hard-sync"* (S2 p.120); Pigments Modal `Q` (S1 p.130); Ableton Spectral
Resonator `Quantize` (S9).
**Mechanism:** resample the *spectral envelope* `E(f)` by a factor `k = 2^(st/12)`, then re-evaluate
it on the integer harmonic grid: `A_h ← E(h·f₀/k)`. Partials never move; only their levels do. The
result is sync-like and, unlike Terrain's `Vocode`, it tracks the note by construction.
**Parameter:** `Shift`, **−24 … +48 st** (Falcon's ceiling), plus a `Snap` toggle that switches
between snapping (this) and free ratios (which `buildFromSpec`'s two-bin split already renders).
**Cost: BAKE.** O(P) with a linear interpolation of the envelope.
**Collision:** ⚠️ It overlaps `Vocode`'s territory, and it is a strictly better version of it.
Best outcome: `Vocode` is **fixed** (its 130.81 Hz constant is a documented defect) by rebuilding
it on this mechanism, rather than a new mode appearing beside a broken one.
**Risk:** low. Needs the envelope to be estimated from the partial list (a peak-hold / cepstral
smooth); INFERRED that a simple log-frequency 3-partial moving max is enough — must be measured.

### 9.11 — SPECTRAL LANES (Terrain's rack is the payoff)
**Source:** Bitwig `Harmonic Split` — `Harmonics Pattern` (S10); Pigments `Imaging → Split`
(S1 p.123).
**Mechanism:** bake **two** tables from one spec — table A holds partials where `h mod N == k`,
table B holds the rest — and route them to two lanes of the existing Splitter FX. Odd/even
(`N = 2`) is the default, exactly as Bitwig ships it. The extreme version is Pigments' Imaging:
pan A hard left and B hard right for a per-partial stereo field.
**Cost: BAKE ×2 and RAM ×2 — the expensive one in this document.** `Wavetable.h:67` states the
number in-source: **4.46 MB per table**, and the bake is ~21 ms. Two tables per osc × 4 oscs ×
2 morph slots is real memory and a ~42 ms message-thread burst. At the current 20 Hz cadence that
is ~84 % message-thread duty per osc — **not viable without lowering the rebuild rate for this
mode.**
**Collision:** none functionally; it *uses* the Splitter (fb444) rather than duplicating it.
**Risk:** the cost above. Ship it as `N = 2` only, restricted to one osc at a time, and measure
the message-thread duty on the real AU before anything else.

### 9.12 — ORDERED CHAOS (a random re-tuning that cannot reorder the partials)
**Source:** Zebralette `Chaos Patterns → Distortion Range = Ordered` — *"the frequency of each
harmonic is juggled up or down, but cannot cross paths with neighbouring harmonics: the order is
preserved"*, with `Random Seed` selecting **one of 100 preset patterns** (S4 p.14).
**Mechanism:** draw `u_h ~ U(0,1)` from a seeded PRNG, then map `f_h` to a point strictly inside
`(f_{h−1}, f_{h+1})` — e.g. `f_h' = f_h·(1 + d·(2u_h − 1)·min(f_h/f_{h−1}, f_{h+1}/f_h − 1))`.
Monotonicity is guaranteed by construction, so the timbre detunes without collapsing into noise.
Contrast the unconstrained version (Terrain has none; Zebra2's `Turbulence` shuffles freely).
**Parameter:** `Scatter` amount + a `Seed` stepper (100 presets, exactly as u-he does it — a
*named, recallable* randomness, not a re-roll).
**Cost: BAKE.** O(P).
**Collision:** ⚠️ `RandomAmplitudes` randomises *amplitudes* with a fixed seed. This randomises
*ratios* with a selectable seed. Different axis, and the seed *selector* is the distinctive part.
**Risk:** low. The one design decision that matters: 100 fixed seeds beats a "randomise" button,
because a patch must recall.

**Explicitly NOT recommended** (documented so nobody re-derives them): Zebralette's `Spectral
Decay`, `Twinkles`, `Dissociate`, `Wild Randomness`; Massive X `Random` / `Jitter`; Falcon
`Beating`; Ableton Spectral Resonator's `Granular` modulation. All are **VOICE** class — functions
of note time or per-note seed — and Terrain publishes one shared table to all voices. They require
a per-voice additive engine, which is a different project.

---

## 10. THE COMPARISON, IN ONE TABLE

| | Partial ceiling | Where the spectrum is computed | Rebuild rate | Frame interpolation | Spectral op count |
|---|---|---|---|---|---|
| **Terrain (today)** | `kMaxHarmonics = 512`, but `extract()` caps at **`kMaxPartials = 96`** | message thread, offline bake, 34 mips | **20 Hz** (~21 ms per bake) | convex phasor average, same-mip, subtractive-only | 7 morph + 11 WARP |
| **Pigments 6** | 512 (Harmonic engine) | real time, additive | n/a | boolean: morph or step | 7 phase transforms + 12×2 spectra + 3 partial shapers |
| **Falcon 3** | user-set; *"A2 has 200 harmonics at 44.1 kHz"* | real time, additive | n/a | `Smooth Wave Index` / `Smooth Octaves` | ~10 additive params + "just under a dozen" PD modes (unlisted) |
| **Zebra2** | **1023** (SpectroMorph) / 128 (SpectroBlend, **bipolar**) | timer-driven wavetable render | **4 s … <1 ms**, user knob, default mid | morph (move breakpoints) vs blend (crossfade) — two different modes | **24** spectral effects, 2 in series |
| **Zebralette 3** | **1024** (`Curve Spectrum`), Additive 16–1024 (default 256) | wavetable render **or** free-running additive | **200 / 800 / 2000 Hz** | Curve Morph over ≤15 curves + 3 Guides | **22** osc FX + **8** modifiers |
| **Hive 2** | 1024 (UHM `Spectrum Highest`) | wavetable, ≤256 frames × 2048 | not stated | **switch / crossfade / spectral / zerophase** (+ `morph1/2` in UHM) | UHM script (unbounded) |
| **Ableton Wavetable** | not stated | wavetable | n/a | not stated | **3** effects × 2 params |
| **Massive X** | not stated; **2–128 waveforms/table** | wavetable readout | n/a | not stated | **10** modes × exactly 2 knobs |
| **Bitwig Grid** | — | **no FFT in the Grid** | — | — | 0 (4 Spectral Suite splitters, device-level) |

Two rows of that table are Terrain's problem statement:
- **96.** Every competitor with a stated ceiling is at 200–1024. Terrain's `extract()` cap of 96
  against a `kMaxHarmonics` of 512 is the truncation defect already logged in `00-INVENTORY.md`
  (Pulse −18.2 dBr, Square −21.6, Triangle −25.1 at `amount → 0⁺`). It is also, now, an
  *industry-relative* number: 96 is **below every documented competitor in this survey.**
- **20 Hz / 21 ms.** u-he ships a user-visible rebuild-rate knob spanning four seconds to under a
  millisecond and calls it the reason they are CPU-efficient. Terrain's rate is fixed and its cost
  is 4× what the code comment claims. Both facts should be surfaced, not hidden.

---

## 11. WHAT I COULD NOT DETERMINE (do not let anyone fill these in from memory)

1. **Falcon's Phase Distortion Mode list.** The manual (S2 p.134) names the parameter and never
   enumerates the modes. The only mode name obtainable from a primary UVI source is **`SymForm`**,
   from the Falcon 1.5.4 changelog line *"fix Wavetable SymForm mode when phase distortion is at 0"*.
   A secondary source claims "just under a dozen". **Not established.** Getting this needs the
   plugin itself, or UVI's Lua scripting reference (`Wavetable` module parameter enum), which I did
   not obtain.
2. **Pigments' Harmonic-engine Shape section: amplitude or frequency?** The manual contradicts
   itself between p.121 and p.122 (§2.4). Not resolved.
3. **Pigments' Wavefolder transfer function.** "Folds a selectable waveform downward onto the peaks"
   is a description, not a formula.
4. **Every FFT size, window and overlap in Ableton's Spectral Resonator and Spectral Time.** The
   manual exposes only a relative `Resolution` control and says lower = less latency, less
   fidelity. No absolute numbers are published.
5. **Bitwig Spectral Suite internals.** The user guide states no FFT size, no band count, no
   latency. Its `Rise Time` / `Fall Time` / `Transients Decay` are given "in blocks" with no block
   size stated.
6. **u-he's `morph1` / `morph2` algorithm.** The manual says outright: *"Details of the 'morph'
   types… will be explained at a later date."* §9.2's maths is **INFERRED** from the parameter
   names plus the standard literature, and is offered as a design proposal, not as a reading of
   u-he's code.
7. **Massive X numeric ranges.** NI's manual is qualitative throughout. The only hard numbers it
   gives in the oscillator section are `2–128` waveforms per table, Gorilla's `×1…×6` ratios and
   Jitter's `J1/J2/J3 = 1 / 32 / 128` cycles. Knob ranges are not published.
8. **Zebra2's `Fundamental` is the only spectral effect with a published numeric range**
   (−200 %…+200 %). The other 23 have none.
9. **Ableton Wavetable's position interpolation.** Not described anywhere in the manual.

---

## 12. TWO THINGS THAT ARE TERRAIN'S OWN PROBLEM, FOUND WHILE DOING THIS

- ⚠️ **An internal double already exists.** `SpectralMorph::DataCompress` quantises partial
  amplitudes to `levels = max(2, round(64 − 62a))` and decimates every `1 + round(3a)`th partial.
  `HarmonicEngine.h:760-776` **TERRACE** quantises the *same quantity* in **dB**, step
  `0.75 → 32 dB`, with a dither term and a deletion floor that rises `−80 → −54 dB`. These are the
  same idea implemented twice with different units in two engines. Per the no-doubles rule, this
  should be reconciled *before* anything in §9 is added.
- ⚠️ **The two engines have disjoint feature sets and nobody has noticed.** `HarmonicEngine` ships
  KEEL, SPLAY, CULL, TIDE, TERRACE, CLANG. `SpectralMorph` ships HarmonicStretch,
  InharmonicStretch, Vocode, Smear, RandomAmplitudes, DataCompress, SpectralPhaser. Falcon's
  `STRETCH` — the stiff-string piano law — is already in the box as **SPLAY**
  (`str = sqrt(1 + B·max(0, n² − anchor²))`, `B ≤ 0.138`, `HarmonicEngine.h:727`) and absent
  from SpectralMorph. Before adding any mode from §9, check both lists.
