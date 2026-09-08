# 12 — KILOHEARTS PHASE PLANT: the spectral inventory

Research target: what Phase Plant actually does in the spectral domain, with numbers,
so Terrain can borrow the good parts and refuse the rest.

Written 2026-08-23. Companion to `Design/spectral/00-INVENTORY.md`.

---

## 0. PROVENANCE — how each fact in this document was obtained

Four classes of evidence. Every claim below is tagged with one of them.

| Tag | Meaning |
|---|---|
| **[MANUAL]** | Stated verbatim in the official Kilohearts online documentation at `https://kilohearts.com/docs/...`. This *is* the manual — Kilohearts ships no separate PDF; the `lootaudio` PDF that surfaces in search is a print of an older revision of the same web pages. |
| **[CHANGELOG]** | `https://kilohearts.com/changelog`, with version + date. |
| **[MEASURED]** | I measured it on this machine. Phase Plant **2.4.6** (`CFBundleShortVersionString`, `/Library/Audio/Plug-Ins/Components/Phase Plant.component/Contents/Info.plist`) and the Kilohearts Essentials snapins are installed here, together with the 402-file factory wavetable bank. Methods given inline. |
| **[FORMAT]** | Read out of `synthahol-phase-plant`, Sheldon Young's independently reverse-engineered Phase Plant preset reader — `https://github.com/softdevca/synthahol-phase-plant`. Not a Kilohearts product; it is a *format* authority (what the preset stores and in what units), not a *DSP* authority. |
| **[INFERRED]** | My reasoning from the above. Explicitly flagged. Treat as a hypothesis, not a fact. |

**The single most important honest finding up front:**

> **Phase Plant has no spectral generator, no resynthesis engine, and no frequency-domain
> Snapin except (arguably) Convolver.** Every generator is a time-domain oscillator/player.
> Every Essentials Snapin is a time-domain filter, delay line, or waveshaper. The *only*
> place Kilohearts does honest FFT-domain work in this product is the **wavetable editor**,
> and that work is **100% offline** — it bakes a new 256×2048 table which is then stored
> inside the preset file.
>
> If you read a blog that says "Phase Plant's Frequency Shifter works in the spectral
> domain", it is wrong, and the manual does not say it.

That negative result is the useful one for Terrain: Kilohearts shipped a critically
acclaimed synth whose entire spectral capability is an **offline table baker** — which
is architecturally *exactly* what Terrain already has (`SpectralMorph::apply` →
`buildFromSpec`, 00-INVENTORY §2).

---

## 1. THE GENERATORS

### 1.1 There are five, and none of them are spectral

> "There are five different kinds: the analog oscillator, the noise generator, the granular
> engine, the sample player, and the wavetable oscillator. All of these sound sources are
> keytracked" — **[MANUAL]** `/docs/phase_plant#generator_area`

The generator area holds **up to 32 modules** **[MANUAL]**, which also includes the
in-generator Distortion / Filter / Non-Linear Filter effects, Mix, Aux, and the two output
modules (Envelope Output, Curve Output). Generators must live inside a **group**; groups
break the automatic top-down routing **[MANUAL]**.

There is **no resynthesis generator, no additive generator, and no spectral generator.**
**[MANUAL]** — the list of five is exhaustive.

### 1.2 Shared generator parameters (all five)

**[MANUAL]** `/docs/phase_plant#generator_area`, with format-level confirmation **[FORMAT]**
(`spp/src/io/generators.rs` `GeneratorBlock`).

| Param | Range / unit | Default | Notes |
|---|---|---|---|
| **Level** | 0 % … **200 %** | 100 % **[FORMAT]** | linear amplitude scale. **[MANUAL]**: "ranges from 0% to 200%" |
| **Pitch (Semi, Cent)** | semitones + cents | 0.00 | example given in the manual is ±12.00 for an octave |
| **Harmonic** | multiplier, `×0.000` … `×N` | `×1.000` (`×4.000` for Noise **[FORMAT]**) | `×0.000` **turns keytracking off entirely** and the frequency is then set purely by Shift **[MANUAL]**. Upper bound not stated in the manual; `×5.000` observed in a preset **[FORMAT]** |
| **Shift** | Hz, signed | 0 Hz | added *after* Pitch and Harmonic. **May push the final frequency below zero — this is allowed and the generator "runs backwards"** **[MANUAL]**. Values −99 Hz and +125 Hz observed **[FORMAT]**; the numeric limit is not documented |
| **Phase** | two values: fixed offset in ° and randomness `±°` | 0°, ±0° | stored as a *percentage of 360°* in the preset **[FORMAT]** (`phase_offset: Ratio`, `phase_jitter: Ratio`). Randomness is applied **per unison voice**, not per note, when unison is on **[MANUAL]** |

`Harmonic` is the parameter Kilohearts tells you to FM: *"It is preferable to modulate the
Harmonic because it will create the same resulting waveform for all played pitches"*
**[MANUAL]** `/docs/phase_plant#audio_rate_modulation`.

### 1.3 Analog Oscillator

**[MANUAL]**: waveforms **Sawtooth, Pulse, Triangle, Sine** (4). Plus:

| Param | What | Range |
|---|---|---|
| **Sync** | "runs the oscillator at a higher frequency, but resets its phase back to zero at the normal frequency" — classic hard sync | multiplier; default `1.0` **[FORMAT]** (`sync_multiplier`), max not documented |
| **PW** | pulse width, pulse wave only | default 50 % **[FORMAT]** |

Supports oscillator unison. No spectral controls at all.

### 1.4 Noise Generator — the closest thing to a spectral control on any generator

**[MANUAL]** `/docs/phase_plant#noise_generator`:

| Param | Range | Default | Detail |
|---|---|---|---|
| **Noise type** | Colored / Keytracked Stepped / Keytracked Smooth | Colored **[FORMAT]** | enum values 0/1/2 in the preset **[FORMAT]** |
| **Slope** | flat (white) → **−3 dB/oct** (pink) → **−6 dB/oct** (brown) | **3.0103 dB** **[FORMAT]** (`Decibels::new(3.0103)`, i.e. exactly `10·log10 2`) | this is a real *spectral tilt*, applied to the noise source |
| **Stereo** | 0 % (mono) … 100 % (stereo) | 0 % **[FORMAT]** | blend, not a widener |
| **Seed** | Stable / Random | Stable **[FORMAT]** | Stable = the same noise sequence every note |

**Band limits of the colored noise: 20 Hz – 20 kHz.** **[CHANGELOG]** v1.7.4, 2 July 2019:
*"Fixed band limits being wrong on sloped noise (40 Hz - 40 kHz rather than the correct
20 Hz - 20 kHz)."* This is the only hard band-edge number Kilohearts has ever published
for a generator.

The default `Harmonic` for Noise is **×4.000**, not ×1.000 **[FORMAT]**
(`noise_generator.rs:158`) — noise is pitched an octave-and-a-bit up by default.

The slope is presumably implemented as a first-order (−6 dB/oct) / half-order
(−3 dB/oct) shaping filter on white noise rather than an FFT — **[INFERRED]**, the manual
does not say, and −3.0103 dB/oct is not realisable exactly by an integer-order IIR
(the usual trick is a cascade of interleaved poles/zeros, e.g. Voss/Gardner or the
Robert Bristow-Johnson 3-pole/3-zero pink filter).

### 1.5 Sample Player

**[MANUAL]**: Root (fundamental pitch, with a two-cycle visual alignment aid), Offset,
Loop Mode (**Infinite / Sustain / Ping Pong / Reverse**), Loop Start, Loop Length,
**X-Fade** (crossfade across the loop boundary, to kill the wrap click).
Supports unison. Default loop mode `Infinite` **[FORMAT]**, default root **C4**
(`base_pitch: midi!(C,4)` **[FORMAT]**).

No resynthesis, no spectral analysis. It is a plain interpolating sample player.
**[MEASURED]** the binary contains a `SampleMipMaps` class and a
`SampleMipMapGenerationTask` — samples get a band-limited mip ladder built on a
background task, same idea as Terrain's 34-level ladder.

### 1.6 Granular Generator

Not spectral, but it is the module people mistake for one. **[MANUAL]**
`/docs/phase_plant#granular_generator`:

| Param | Range / unit | Default | Detail |
|---|---|---|---|
| Play Cursor (position) | 0…100 % of sample | 0 % **[FORMAT]** | grains start here |
| **Grain Length** | ms | **250 ms** **[FORMAT]** | with optional **length keytracking** — "high pitched voices will use shorter grains … so that grains cover the same section of the sample regardless of what note is played" |
| Grain Envelope | attack time, decay time, attack curvature, decay curvature | — | "stretched to cover the whole lifetime of the grain" |
| **Spawn Rate** | one of three modes: **Free Rate** (Hz) · **Synced Rate** (note length, default 1/16 **[FORMAT]**) · **Density** (target number of *simultaneously playing* grains) | **Density** **[FORMAT]** | Density is the overlap-preserving mode |
| Root | fundamental pitch | C4 | |
| **Align Phases** | on/off | off **[FORMAT]** | "nudge the position of each grain so that they are in phase with each other in regards to the fundamental frequency" |
| **Warm Start** | on/off | off **[FORMAT]** | all grains already running at note-on |
| Randomizations | Position, Timing, Pitch, Level, Pan, **Reverse** (a probability) | 0 | |
| Chords | chord type, **Range** in octaves, picking pattern (up/down/up-down/random) | off | |

🔑 **Direct relevance to Terrain fb416** (`terrain-instrument-granular-overlap-law-fb416`):
Kilohearts' **default spawn mode is Density**, i.e. *"grains are spawned at a rate computed
automatically to hit a certain number of simultaneously playing grains. Adjusting the length
of the grains will thus change the grain spawn rate."* **[MANUAL]**. That is the exact fix
for the fb416 bug — density-as-overlap, not density-as-rate. Kilohearts shipped it as the
**default**, with Free Rate (Hz) and Synced Rate as the two opt-in alternatives. Terrain's
granular should offer the same three-way and default to the overlap one.

### 1.7 Wavetable Oscillator — the numbers

> "It is backed by a wavetable, which contains **256 frames**, each one holding a sampled
> waveform **2048 samples** long … Phase Plant can load wavetables from wav and flac files,
> provided they have the right length (**256 × 2048 = 524 288 samples**)."
> — **[MANUAL]** `/docs/phase_plant#wavetable_oscillator`

**[MEASURED]** — I verified this against the shipped bank. All 402 factory wavetables in
`/Library/Application Support/Kilohearts/dependencies/factory_wavetables/`:

```
$ find "$WT" -name '*.flac' | wc -l
     402
$ find "$WT" -name '*.flac' -print0 | xargs -0 -n1 afinfo | grep -oE 'audio [0-9]+ valid frames' | sort | uniq -c
 402 audio 524288 valid frames
```

**402 of 402 are exactly 524 288 samples. Zero exceptions.** Encoding: mono FLAC,
24-bit source, nominal 96 000 Hz (the rate is a container artifact and carries no meaning —
a 2048-sample frame is one cycle regardless).

2048 samples per frame ⇒ **1024 harmonics maximum**. **[MEASURED]** I confirmed the bank
actually uses that headroom: the highest harmonic above −80 dB relative to H1 reaches
**1024** in `Spectral/Harmonic Blend`, `Filters/Combs/Bandspread`, and
`Morphs/Saw to Square to Triangle`.

🚨 **Terrain comparison** (see 00-INVENTORY escalation #1): Terrain's `kMaxHarmonics = 512`
and, worse, `SpectralMorph::extract()` caps at **`kMaxPartials = 96`**. Phase Plant carries
**1024**. Terrain's 96-partial cap is 10.7× coarser than the commercial reference, and it is
the direct cause of the measured −18.2 to −25.1 dBr *step* when a morph mode engages.

Wavetable oscillator parameters:

| Param | Range | Default | Source |
|---|---|---|---|
| **Frame** | 1 … 256 in the UI, stored **0-based as an `f32`** | 0 | **[FORMAT]** `wavetable_oscillator.rs:28` `pub frame: f32`; the test fixture named `…-frame33-bandlimit8k-…` asserts `generator.frame == 32.0` (`:199`) — so the UI is 1-based and the store is 0-based |
| **Bandlimit** | not documented; **22 050 Hz default**, 8 000 Hz observed | **22 050 Hz** | **[FORMAT]** `io/generators.rs:216` `Frequency::new::<hertz>(22050.0)`; `wavetable_oscillator.rs:134,200` |

**[MANUAL]** on Bandlimit: *"Bandlimits the wavetable using a very sharp internal low pass
filter. This feature can be used to tame a wavetable which is under heavy phase modulation,
**since the filter is applied before the phase modulation**."*

🔑 That last clause is the whole design. Bandlimit is not a tone control — it is a
**pre-emphasis for FM/PM**: you kill the partials *before* they get spread by the phase
modulator, so the modulator cannot alias them. Terrain's WARP chain (`applyPhaseWarp`,
`SynthVoice.h:938-1000`) has **no equivalent** — there is no per-oscillator band limit
applied ahead of the phase warp. That is a real, cheap, Serum/Phase-Plant-parity feature
Terrain is missing, and it is one mip-index clamp away from existing
(`mipLevelForPhaseIncrement` already knows how to pick a band-limited copy).

`frame` being `f32` and `band_limit` being a `Frequency` are both stored per generator, and
the *whole edited wavetable* is stored in the preset as `wavetable_contents: Vec<u8>`
**[FORMAT]** — Phase Plant embeds the baked table, it does not re-derive it.

---

## 2. THE SNAPINS — which ones are spectral (answer: essentially none)

I read every Essentials snapin page in `/docs/snapins`, and then **[MEASURED]** the real
parameter ranges by running Apple's `auval` against the installed AudioUnits, which prints
`min / default / max` **with the plugin's own unit strings**. Command form:

```
auval -v aufx <subtype> " kHs" | awk '/PUBLISHED PARAMETER INFO/,/Testing that parameters retain/'
```

Version reported by every one of them: **2.4.6 (0x20406)**.

### 2.1 The domain table

| Snapin | Domain | Why |
|---|---|---|
| Comb Filter | **time** | "mix the signal with a delayed version of itself" **[MANUAL]** — a delay line |
| Formant Filter | **time** | two resonant peaks; the preset stores them as two *frequencies* X and Y **[FORMAT]** |
| Frequency Shifter | **time** | single-sideband shift. Hilbert/analytic-signal — **[INFERRED]**, see §2.3 |
| Haas | **time** | a pure delay on one channel |
| Phase Distortion | **time** | self-modulating phase; broadband allpass — **[INFERRED]** |
| Resonator | **time** | tuned feedback comb / Karplus-Strong |
| Ring Mod | **time** | multiplication |
| Pitch Shifter | **time** | granular or correlated (WSOLA-class); grain size is a *parameter* |
| Phaser | **time** | cascaded allpass, `Order` is a published integer 1…7 |
| Filter / Ladder / Nonlinear Filter | **time** | IIR |
| Multipass (host) | **time** | *"uses **Linkwitz–Riley** crossover filters, which ensure that a flat frequency response is achieved when the bands are mixed back together"* **[MANUAL]** `/docs/multipass` |
| Carve EQ / Slice EQ | **time** filters, FFT **display** | the analyser is FFT; the filters are IIR |
| **Convolver** | **frequency — probably** | see §2.6 |

**Not one Essentials Snapin does STFT / phase-vocoder / bin-domain processing.**
No Snapin publishes an FFT size, an overlap factor, or a window. The manual never uses the
words *FFT*, *bin*, *window*, *overlap*, or *phase vocoder* anywhere.

### 2.2 Measured parameter ranges (auval, Phase Plant / Essentials 2.4.6)

**Comb Filter** (`aufx kscf`)

| Param | Min | Default | Max |
|---|---|---|---|
| Cutoff | 20.0 Hz | 440 Hz | **20.5 kHz** |
| Mix | 0.0 % | 100 % | 100 % |
| Polarity | Plus | Plus | Minus |
| Stereo | Off | Off | On |

"Cutoff" is *"the distance between each peak"* **[MANUAL]** — i.e. delay = 1/f, from
50 ms down to 48.8 µs. Polarity Plus = peak at 0 Hz (feed-forward `+`), Minus = trough at
0 Hz (`−`). Stereo **flips the polarity on the right channel only**, "allowing the comb
filter to be used for **mono compatible** stereo widening" **[MANUAL]** — that is a genuinely
clever trick worth stealing for Terrain's Widen card: L gets `x[n] + x[n−d]`, R gets
`x[n] − x[n−d]`; the mono sum is `2x[n]`, i.e. **perfectly mono-compatible by construction**.

**Formant Filter** (`aufx ksvf`) — directly comparable to Terrain's `Vocode` morph mode

| Param | Min | Default | Max |
|---|---|---|---|
| **X** (≈ F1) | **200 Hz** | 550 Hz | **900 Hz** |
| **Y** (≈ F2) | **500 Hz** | 1.50 kHz | **2.50 kHz** |
| **Q** | **2.000** | 4.000 | **16.000** |
| Lows | Off | On | On |
| Highs | Off | On | On |

🔑 The vowel selector is a **2-D pad over two independent formant frequencies**, not a
5-position enum. The preset stores `x: Frequency, y: Frequency` **[FORMAT]**
(`formant_filter.rs`) — the vowel names are labels printed on a continuous plane.
F1 ∈ [200, 900] Hz and F2 ∈ [500, 2500] Hz are the real published ranges, and Q ∈ [2, 16].
Lows/Highs are pass-through gates for the out-of-band energy.

🚨 **This is the fix for Terrain's `Vocode` mode.** 00-INVENTORY §1 records that Terrain's
Vocode sweeps 5 vowels with a **hard-wired F0 = 130.81 Hz that does not track the note**.
Phase Plant's formant filter has no F0 at all — formants are *absolute* frequencies, which is
physically correct (a vocal tract resonance does not move when you sing a different pitch).
Terrain should drop the F0 entirely and place two Lorentzians at absolute
F1 ∈ [200, 900] Hz, F2 ∈ [500, 2500] Hz with Q ∈ [2, 16], exactly these ranges.

**Frequency Shifter** (`aufx ksfs`)

| Param | Min | Default | Max |
|---|---|---|---|
| **Shift** | **−5.00 kHz** | 0.00 Hz | **+5.00 kHz** |

One knob. That is the entire plugin. ±5 kHz, bipolar, zero at centre. Confirmed by the
format reader's own commented-out constants **[FORMAT]** (`frequency_shifter.rs:31-33`:
`MIN_FREQUENCY = -5000.0`, `MAX_FREQUENCY = 5000.0`).

**Haas** (`aufx ksha`)

| Param | Min | Default | Max |
|---|---|---|---|
| Channel | Left | **Right** | Right |
| **Delay** | **0.10 ms** | 5.00 ms | **100 ms** |

A single-channel delay. Not spectral in any sense; it is in this document only because the
brief listed it. 0.1 ms is 4.4 samples at 44.1 k, so it must interpolate — **[INFERRED]**.

**Phase Distortion** (`aufx kspd`)

| Param | Min | Default | Max |
|---|---|---|---|
| Drive | 0.0 % | 0.0 % | 100 % |
| **Bias** | **−180°** | 0° | **+180°** |
| **Spread** | 0° | 0° | **90°** |
| Normalize | 0.0 % | 0.0 % | 100 % |
| **Tone** | **20.0 Hz** | 440 Hz | **10.0 kHz** |
| Mix | 0.0 % | 100 % | 100 % |

> "Phase Distortion distorts the signal by offsetting the phases of the individual harmonics
> of the input signal. **The amount of phase offset is controlled by the signal itself, much
> like FM feedback.**" — **[MANUAL]**
> "**Bias** — Adds a constant phase offset to all harmonics." — **[MANUAL]**

🔑 Note what Bias *is*: a **constant phase rotation of every partial**, in degrees, over the
full ±180°. Rotating every partial by the same angle is exactly a mix of the signal with its
Hilbert transform: `y = x·cos θ + H{x}·sin θ`. So Phase Distortion must contain an
analytic-signal (Hilbert / allpass-pair) stage — **[INFERRED]**, but Bias's *unit being
degrees over ±180* is very hard to implement any other way in the time domain. `Tone` is
a low-pass on the *modulator* path only (20 Hz…10 kHz), which is the classic anti-noise
guard on a feedback-FM loop. `Spread` de-correlates L/R by up to 90° — a quarter turn.
The preset stores Bias and Spread as **percentages of 360°** **[FORMAT]**
(`phase_distortion.rs`), confirming the degree semantics.

Terrain already ships something very close in `SpectralPhaser` (`|sin(π(r+4.5a)/1.7)|²`,
00-INVENTORY §1), but Terrain's version welds depth and sweep to one knob. Phase Plant
splits it into **Drive (amount)**, **Bias (absolute rotation)**, **Tone (modulator LPF)**,
**Spread (stereo)** — four orthogonal controls where Terrain has one. That separation is the
lesson, and it is free: Terrain's morph is offline, so adding a bias term costs nothing.

**Resonator** (`aufx ksre`)

| Param | Min | Default | Max |
|---|---|---|---|
| **Pitch** | **8.18 Hz** (= C−1) | A4 | **C9** (≈ 8.37 kHz) |
| **Decay** | **1.00 ms** | 31.0 ms | **1.00 s** |
| Intensity | 0.0 % | 0.0 % | 100 % |
| **Timbre** | **Saw** (all harmonics) | Saw | **Sqr** (odd harmonics only) |
| Mix | 0.0 % | 100 % | 100 % |

Timbre = "all harmonics (saw tooth) or odd harmonics (square)" **[MANUAL]**. That is a
feed-forward vs feed-back comb sign flip, stored as a single bool `sawtooth: bool`
**[FORMAT]** (`resonator.rs`). Time domain, ~1 delay line.

**Ring Mod** (`aufx ksrm`)

| Param | Min | Default | Max |
|---|---|---|---|
| **Frequency** | **10.0 Hz** | 440 Hz | **10.2 kHz** |
| Bias | 0.0 % | 0.0 % | 100 % |
| **Rectify** | **−100 %** | 0.0 % | **+100 %** |
| Spread | 0.0 % | 0.0 % | 100 % |
| Mix | 0.0 % | 100 % | 100 % |

The modulator can be an internal **sine oscillator**, an internal **noise generator** (in
which case `Frequency` becomes that noise's filter cutoff **[MANUAL]**), or the **sideband
input**; stored as `modulation_mode: ModulationMode` **[FORMAT]** (`ring_mod.rs`).
`Bias` adds DC to the modulator (ring mod → amplitude mod continuum);
`Rectify` is signed, ±100 %, so you can half-wave rectify the modulator either way, which
doubles the modulator's fundamental and injects even harmonics.

**Pitch Shifter** (`aufx ksps`) — the closest Kilohearts gets to a vocoder and it still isn't one

| Param | Min | Default | Max |
|---|---|---|---|
| Pitch | −24.00 st | +0.00 | +24.00 st |
| Jitter | 0.0 % | 0.0 % | 100 % |
| **Grain Size** | **20.0 ms** | **80.0 ms** | **200 ms** |
| **Algorithm** | **Grains** | Grains | **Correlated** |
| Latency Compensation | Off | **Low** | High |
| Mix | 0.0 % | 100 % | 100 % |

A user-visible **Grain Size in ms** and an algorithm switch between *Grains* (naive
overlap-add) and *Correlated* (cross-correlation splice search, i.e. WSOLA-class) is
conclusive: this is **time-domain granular pitch shifting, not a phase vocoder**.
A phase vocoder has an FFT size and a hop, not a grain size in milliseconds.
`Latency Compensation` (Off / Low / High) is the only latency knob in the entire Essentials
set — and it exists precisely *because* the correlated algorithm needs lookahead.

**Phaser** (`aufx ksph`) — for comparison with Disperser

| Param | Min | Default | Max |
|---|---|---|---|
| **Order** | **1** | 3 | **7** |
| Cutoff | 40.0 Hz | 500 Hz | 10.0 kHz |
| Depth | 0.0 % | 50 % | 100 % |
| Rate | 0.05 Hz | 0.60 Hz | 6.00 Hz |
| Spread | 0.0 % | 50 % | 100 % |
| Mix | 0.0 % | 100 % | 100 % |

**Order is a published integer, 1…7.** That is the number of allpass stages. Hold this
number next to Disperser's `amount` in §3.1.

### 2.3 Frequency Shifter: what it almost certainly is

**[INFERRED].** The manual gives one sentence and one knob. But:

* the range is ±5 kHz **bipolar**, which is the signature of a **single-sideband** shifter
  (analytic signal × complex exponential), not a spectral bin rotation;
* an FFT bin-shift cannot do the sub-bin resolution the ±0.01 Hz-precision UI implies;
* `auval` reports **no meaningful latency requirement** and Kilohearts advertise the whole
  Essentials set as *"optimised to be CPU-efficient so they can be stacked together without
  melting your computer"* **[MANUAL]** `/docs/snapins` — an STFT shifter would cost latency
  and they would have had to publish it, as they did for the Pitch Shifter.

So: Hilbert transform pair (a pair of cascaded allpass networks with a 90° phase difference
over the band), quadrature oscillator, `y = I·cos(ωt) − Q·sin(ωt)`. Per-sample, ~8–12
biquads. **I did not verify this; treat it as a hypothesis.**

### 2.4 What the binary actually contains

**[MEASURED].** All Kilohearts plugins on macOS are 216 KB shims that dlopen one shared
core: `/Library/Application Support/Kilohearts/HeartCore.core/Contents/MacOS/HeartCore`,
**86 089 568 bytes**. It is stripped of ordinary symbols, but C++ RTTI type names survive.
`strings -a` over it yields, among ~224 000 strings:

* **`N4ffft7FFTRealIfEE`** → `ffft::FFTReal<float>`. That is **Laurent de Soras' FFTReal**
  (`http://ldesoras.free.fr/prod.html`, public domain / WTFPL), a real-input split-radix FFT.
  So Kilohearts has *exactly one* FFT implementation in the whole product, and it is a
  third-party one. **There is no evidence of a streaming STFT framework, a window function
  library, or an overlap-add engine.**
* `WavetableMipMaps`, `SampleMipMaps`, `LfoTableMipMaps`, `WaveshaperMipMaps`,
  `FilterTableMipMapsClassic` → **five separate mip ladders**, one per table type.
  `SampleMipMapGenerationTask` + `TaskQueue` → ladders are built on a **background task**,
  not inline.
* The 16 wavetable-editor effect classes (§4).

I could not extract UI label strings (they are not stored as plain ASCII or UTF-16 in the
binary), so I have **no way to read the wavetable-editor effects' parameter ranges**. Those
are marked UNKNOWN below and it would take a live GUI session to get them.

### 2.5 Phase Plant's own published automation surface

**[MEASURED]** `auval -v aumu kphp " kHs"` → **94 global parameters**:

* `Enabled`
* `Lane 1..3 > Gain` (−Inf … **+10.00 dB**, default −0.00), `Lane 1..3 > Mix` (0…100 %),
  `Lane 1..3 > Mute`, `Lane 1..3 > Solo`
* `Macro 1..8` (0…100 %)
* `Pitch Wheel` (−100…+100 %), `Mod Wheel` (0…100 %)
* `Master Pitch` (**−60.00 … +60.00** semitones), `Master Gain` (−Inf … +10.00 dB)
* `Polyphony Voices` (**1 … 64**, default 8), `Monophonic Mode` (Legato/Retrig)
* `Glide Enabled`, `Glide Mode` (Always/Legato), `Glide Time` (**0.00 ms … 5.00 s**, default 100 ms)
* `Automation slot 1..64 > Value` (0…100 %)

🔑 **No generator parameter is host-automatable.** Phase Plant exposes 8 macros plus **64
generic "automation slots"** and nothing else; internal parameters reach the host only by
being assigned to a slot. That is a deliberate architecture — the module graph is dynamic, so
a stable parameter ID list is impossible. Terrain made the opposite choice (a fixed,
fully-enumerated parameter tree, now 1152 FX-mod destinations after fb453). Both are
defensible; the point is that **Kilohearts pays for modularity with a generic slot layer**,
and Terrain pays for a fixed tree with `FxModBase=694` bookkeeping.

### 2.6 Convolver — the one plausible FFT-domain effect

**[MANUAL]** `/docs/convolver`. Parameters: Start, End, FadeIn, Fade Out, Stretch, Select
Sample, Reverse, Delay (pre-delay, ms or tempo fraction), Tone, Feedback, Mix.

The manual says of Start, End, FadeIn, Fade Out and Stretch: *"**Requires a precomputation
step before the effect can be heard.**"* A precomputation step that must complete before you
hear anything is the signature of **partitioned FFT convolution**: the IR is re-blocked and
forward-transformed once, then each partition is complex-multiplied per block.

**[INFERRED]** — the manual never says FFT, never gives a partition size, never gives a
latency figure. The presence of `ffft::FFTReal<float>` in the binary is consistent but is
also explained by the wavetable editor alone. **Do not cite Convolver as a proven
frequency-domain processor.**

Convolver is a **paid** Snapin and is **not installed on this machine**, so I could not
measure its parameter ranges or its reported latency. That measurement is the obvious
follow-up if it ever matters.

---

## 3. ANYTHING THAT BLURS, SMEARS, OR DISPERSES A SPECTRUM

There are exactly **two** dispersion mechanisms in the product, plus **five** wavetable-editor
effects that touch phase or the frame axis.

### 3.1 Disperser (a standalone plugin, also a Snapin) — real-time, time domain

> "Disperser is a **stack of all-pass filters** tuned to cause frequency dependent delay in
> the signal … The **amount** knob adjusts how pronounced the effect is **by increasing the
> order of the all-pass filter** … the **pinch** knob adjusts the **Q** setting of the filter,
> which will have the effect of concentrating the delay around the cutoff."
> — **[MANUAL]** `/docs/disperser`

Parameterisation **[FORMAT]** (`spp/src/effect/disperser.rs:23-42`):

```rust
pub struct Disperser {
    pub frequency: Frequency,   // default 130.0 Hz
    pub amount: u32,            // default 18   <-- INTEGER: the filter ORDER
    pub pinch: f32,             // default 0.5
}
```

| Param | Type | Default | Observed | Documented range |
|---|---|---|---|---|
| **Amount** | **`u32` — an integer count of allpass stages** | **18** | 10 | not published |
| **Frequency** | Hz | **130.0 Hz** | 200 Hz | not published |
| **Pinch** | `f32`, a Q | **0.5** | **3.0** | not published |

🔑 **`amount` is a `u32`.** That settles the mechanism beyond doubt: Disperser is
`amount` cascaded **second-order allpass sections**, all tuned to the same cutoff and Q.
It is not an FFT, not a phase ramp, not a table — it is N biquads. Compare Phaser, whose
equivalent knob is literally called **`Order`** and is published as **1…7**. Disperser's
default is **18**, so its range is at least an order of magnitude wider than the Phaser's.

Behavioural numbers the manual does give:

* At low Amount: *"a tiny bit of phase offset … usually not enough to be audible. It is still
  a usable tool with a low amount setting however, since it can **reduce the crest factor**"*
  — Kilohearts explicitly frame low-amount dispersion as a **headroom tool**, citing radio
  stations putting allpass on mics. **[MANUAL]**
* At medium Amount: *"a kind of frequency aware transient shaper. It will transform sharp
  transients from clicks into short zaps, since **the low frequencies will be delayed more
  than the high ones**."* **[MANUAL]**
* At maximum Amount: *"the group delay will be even higher, **on the order of several hundred
  milliseconds**."* **[MANUAL]**

That is the only quantitative statement about the effect size: **several hundred ms of group
delay at the low end, at maximum amount, with the cutoff kept low.**

The group delay of one 2nd-order allpass at cutoff `f0` with quality `q` peaks at roughly
`2q/(π f0)` seconds. With `f0 = 130 Hz` and `amount = 18` stages that is ≈ `18 · 2q/(π·130)`
= `0.088·q` s — so a few hundred ms at `q ≈ 3` (the observed Pinch value) falls out of the
arithmetic exactly as the manual describes. **[INFERRED]** — my algebra, from their numbers.

**Cost class: per-sample.** `amount` biquads per channel. At the default 18, that is 36
biquads for stereo — cheap. The knob is directly a CPU multiplier, which is presumably why
they let you turn it up.

### 3.2 The wavetable editor's **Disperse** effect — offline, per frame, keyframed

> "**Disperse** — This effect is similar to the Disperser plugin. It will add a phase shift to
> all partials which increases in amount for higher partials."
> — **[MANUAL]** `/docs/wavetables#effects`

**[MEASURED]** the class exists: `wavetableeffects::DisperseEffect` with a companion
`DisperseParameters` struct, wrapped in `WaveTableEffectTool<DisperseEffect, DisperseParameters>`
and `WaveTableToolBase<DisperseEffect>` — i.e. it is one of the 16 keyframe-animatable modal
tools, not a live effect. Parameter names and ranges: **UNKNOWN** (see §2.4).

This is the exact operation Terrain's `Smear` mode does with its `seeded phase scatter a·4.1`
term — except Kilohearts' is **monotonic in partial index** (a dispersion), where Terrain's is
**random per partial** (a scatter). Those are different sounds: dispersion smears a transient
in time and is invertible; random scatter destroys the waveform's identity and is not.
Terrain has the random one and lacks the monotonic one.

### 3.3 **Frame Blend** — the direct analogue of Terrain's `renderBlend`

> "**Frame Blend** — Frame blend weighs together adjacent frames. One example: Frame 5 will
> after processing become a mixture of frames **2, 3, 4, 5, 6, 7 and 8**. The amount of frames
> to be mixed can be set with the **"Distance"** parameter."
> — **[MANUAL]** `/docs/wavetables#effects`

Read the example carefully: frame 5 → mixture of {2,3,4,5,6,7,8}. That is **Distance = 3**,
giving a symmetric window of `2·Distance + 1 = 7` frames. So:

* **Distance is a half-width in frames**, an integer.
* The kernel is symmetric and centred.
* The manual says "weighs together", not "averages" — the weights are not published, so
  whether it is a box or a triangle or a Gaussian is **UNKNOWN**.
* Added in **[CHANGELOG]** v1.8.4, 23 March 2020: *"Added Wavetable effects Squarify, Frame
  Blend and Distortion."*
* It is **offline** and **keyframe-animatable**, so Distance can vary across the table.

🚨 **Terrain's `renderBlend` is the same idea with a different parameterisation and it is
strictly better documented.** From 00-INVENTORY §3: `σ = 1e-4 + N·1.05·blur^1.25`,
`band = min(N, ⌈4σ⌉+2)`, weights summing to `blur` with the bilinear `ref` carrying
`(1−blur)` so total weight is exactly 1, then RMS-matched by `G = √(Σref²/Σpre²)`.
Terrain's is a **Gaussian with a continuous σ and a dry/wet convex blend**; Kilohearts' is a
**fixed-width symmetric kernel with an integer half-width**. Terrain's is the more musical
control (continuous, and `blur=0` is exactly the dry frame by construction). Do **not** switch
to Distance.

The one thing to take: Kilohearts made Frame Blend an **offline table operation you commit**,
whereas Terrain evaluates `renderBlend` at table-build time and therefore pays the ~21 ms bake
(00-INVENTORY escalation #2) every time `blur` moves. Kilohearts pays it **once, ever**.

### 3.4 **Phase Offset** — the parameterisation worth stealing

> "**Phase Offset** — Adds a phase offset to the wavetable frames. You can **blend between a
> linearly frequency dependent phase shift, which simply offsets the waveform, to shifting
> every partial by the same angle**."
> — **[MANUAL]** `/docs/wavetables#effects`

🔑 This is the most elegant single control in the whole product and it costs nothing to copy.
One knob interpolates between the two canonical phase manipulations:

| End of the knob | Phase applied to partial *h* | What it is | Audible result |
|---|---|---|---|
| "linearly frequency dependent" | `φ_h = h · θ` | a pure **time shift** of the waveform | **inaudible** on a looping single cycle |
| "every partial by the same angle" | `φ_h = θ` | a **Hilbert rotation** of the waveform | changes the waveshape and the crest factor, no spectral change |

So the knob is `φ_h = θ · ((1−k)·h + k)` for blend `k ∈ [0,1]`. At `k=0` you are just sliding
the wave; at `k=1` you are rotating it in the analytic plane. Everything in between is a
partial dispersion. **Two well-understood extremes, one knob, no dead zone** — which is exactly
what Terrain's `feedback-params-evolve-0-100-no-freerun-hardrule` demands and what Terrain's
`SpectralPhaser` (depth and sweep welded together) currently violates.

### 3.5 **Reset Phases**, **Comb**, **Automatic EQ**, and the rest

The full **[MANUAL]** list from `/docs/wavetables#effects`, cross-checked against the 16 RTTI
class names I extracted from the binary **[MEASURED]**. All are offline, modal, and
keyframe-animatable.

| Manual name | Binary class **[MEASURED]** | Domain | What the manual says |
|---|---|---|---|
| Automatic EQ | `EqualizeEffect` | spectral | *"try to flatten out the spectrum somewhat to a gentle slope, **while still preserving the finer detail in the spectrum**. You can use this to clean up a wavetable where different frames vary wildly in spectral energy."* |
| **Frame Blend** | `FrameBlendEffect` | frame axis | §3.3. Param: **Distance** |
| **Comb Filter** | `CombEffect` | **spectral** | *"Since this effect is **implemented in the frequency domain**, it can do some things that a normal comb filter can't, like **warping the comb pattern in the spectrum**."* ← the only place Kilohearts explicitly claims frequency-domain implementation |
| **Disperse** | `DisperseEffect` | spectral (phase) | §3.2 |
| Distortion | `DistortionEffect` | time | Drive, Bias, Mix, 6 types |
| **Phase Offset** | `PhaseOffsetEffect` | spectral (phase) | §3.4 |
| Power Sync | `PowerSyncEffect` | time | *"a novel sync-like effect"* |
| Rectify | `RectifyEffect` | time | |
| **Reset Phases** | `ResetPhasesEffect` | spectral (phase) | *"Resets the phases of all partials to a specified value. **The effect can be blended.**"* |
| Self FM | `SelfFmEffect` | time | phase-modulate the wave with itself |
| Sine FM | `SineFmEffect` | time | phase-modulate the wave with a sine |
| **Squarify** | `SquarifyEffect` | **spectral** | *"**Removes all even harmonics**, mimicking the harmonic profile of a square wave"* |
| Sync | `SyncEffect` | time | |
| Tilt EQ | `TiltEffect` | spectral | sloped EQ |
| **Symmetrize** | `SymmetrifyEffect` | ? | **[CHANGELOG]** v2.1.1, 4 Oct 2023 — *"New Wavetable Editor Effect: Symmetrize"*. **Not documented on the docs page.** |
| **Fix Seam** | `FixSeamEffect` | ? | **[CHANGELOG]** v2.1.1 — *"New Wavetable Editor Fix Tool: Fix Seam"*. **Not documented on the docs page.** |

16 classes, 16 rows. The list is complete.

**None of these publish a parameter range anywhere.** The manual gives prose only. I flag
this as a genuine gap — if Terrain wants to match a specific Kilohearts effect numerically,
someone has to open the GUI and read the knobs.

### 3.6 The "Fixes" — non-modal, immediate commands

**[MANUAL]** `/docs/wavetables#fixes`. These are the housekeeping operations, and two of
them are load-bearing for Terrain.

| Fix | What it does |
|---|---|
| **Normalize** | *"**Removes DC and normalizes the volume of all frames, measured using RMS.** Also **makes sure that no peak goes over 100%.**"* |
| Remove DC | DC only |
| Invert | Invert **Time** (play the waveform backwards) or Invert **Amplitude** (flip phase) |
| **Align Fundamentals** | *"sets the phase of the **first harmonic** in all frames to be identical to the selected frame"* |
| **Align All Phases** | *"sets the phase of **all** harmonics to be identical to the selected frame"* |
| **Align Frames** | *"**phase-shifts each frame without changing the phase ratio of its harmonics**, so that they get **maximum correlation** to the selected frame"* |

> "The point of these three fixes is to **reduce disturbing phase effects when modulating the
> frame** in the wavetable oscillator." — **[MANUAL]**

🔑 That sentence is the whole problem statement for frame interpolation, stated by the vendor.
When you sweep Frame, adjacent frames whose partials disagree in phase **cancel** rather than
crossfade. Kilohearts' answer is three escalating repairs:

1. **Align Fundamentals** — cheapest, fixes only H1.
2. **Align Frames** — a **cross-correlation maximiser** that applies a *pure time shift* per
   frame (`φ_h += h·τ_f`, preserving the phase ratios). This is the right one: it removes the
   inter-frame phase disagreement without altering any frame's own waveshape.
3. **Align All Phases** — the nuclear option; every frame ends up with the same phase
   spectrum, so interpolation is guaranteed monotonic but every frame's waveshape is changed.

Terrain has **none of these three**. `Wavetable::buildFromSpec` performs a phasor sum with an
inharmonic snap (00-INVENTORY §5) but nothing aligns frame `n` against frame `n−1`.

---

## 4. FRAME INTERPOLATION AND SMOOTHING ACROSS FRAMES

This is the question the brief most wants answered, so here is exactly what is known,
what is measured, and what is not known.

### 4.1 What the manual says: nothing

**The Phase Plant manual never describes the wavetable oscillator's runtime frame
interpolation.** It documents the `Frame` parameter as *"Selects what frame in the wavetable
to play back"* **[MANUAL]** and stops. There is no statement about crossfading, no statement
about smoothing, no statement about what happens between integer frames.

**[MEASURED]** I grepped the whole documentation set — `/docs/phase_plant`,
`/docs/wavetables`, `/docs/snapins`, `/docs/curves_lfos_remaps`, `/docs/glossary`,
`/docs/samples`, `/docs/multipass`, `/docs/basic_usage`, `/docs/effects`, `/docs/disperser`,
`/docs/convolver`, `/docs/content_types` — for *interpolat\**. It appears **exactly twice in
the entire corpus**, and both hits are in the wavetable **editor**, not the oscillator:

* Brush tool: *"Each frame you draw on will become a keyframe. **Inbetween frames are
  interpolated linearly from the keyframes.**"*
* Harmonic Edit tool: *"The result is interpolated similarly to the brush tool."*

`/docs/phase_plant` — the page that documents the wavetable oscillator itself — contains the
word **zero** times. The words *fft*, *bin*, *overlap*, *window* and *phase vocoder* appear
nowhere in the corpus at all. **Say it plainly: the manual does not specify runtime frame
interpolation.**

### 4.2 What is known for certain

| Evidence | Source | What it establishes |
|---|---|---|
| `pub frame: f32` in the preset's wavetable oscillator block | **[FORMAT]** `wavetable_oscillator.rs:28` | the frame position is a **float**, not an index. There *is* a fractional frame. |
| Test fixture `wavetable_oscillator-frame33-bandlimit8k-1.8.13.phaseplant` asserts `frame == 32.0` | **[FORMAT]** `:199` | UI is **1-based** (1…256), storage is **0-based** (0…255) |
| *"Reduced some artifacts in **wavetable interpolation**."* | **[CHANGELOG]** **v2.4.5, 11 December 2025** | interpolation exists, it produced artifacts for ~6 years, and it was improved 8 months ago |
| `WavetableMipMaps` RTTI class in HeartCore | **[MEASURED]** | the table is stored as a **band-limited mip ladder**, so any interpolation happens *within a mip level* |
| Frame is a modulation target with a continuous knob | **[MANUAL]** | it must be smooth or it would zipper |

### 4.3 What is very likely and is flagged as such

**[INFERRED]:** Phase Plant reads the two bracketing frames of the selected mip level and
**linearly crossfades them in the time domain** by the fractional part of `frame`, i.e.
`y = (1−f)·T[⌊n⌋] + f·T[⌈n⌉]`. Reasons:

1. it is what `frame: f32` plus a mip ladder implies;
2. it is what the three **Align** fixes exist to compensate for — a *magnitude*-domain
   interpolator would not care about inter-frame phase disagreement at all, so the fact that
   Kilohearts ships three phase-alignment repair tools **proves the runtime interpolator is
   phase-sensitive**, which a linear time-domain crossfade is and a magnitude morph is not;
3. it is the only option cheap enough to run per-sample per-voice per-unison-voice.

**I did not verify this by rendering audio.** It would take driving the installed AU with a
harness and comparing a `frame = 32.5` render against a synthetic 50/50 mix of frames 32 and
33. That is a ~1-hour job and is the right next step if this matters.

### 4.4 MEASURED: how much does linear frame interpolation actually cost?

Since the frame axis is fixed at 256 and interpolation is (almost certainly) linear, the
useful engineering question is: **how non-linear is the frame axis of real content?**

Method: decode a 21-table stratified sample of the factory bank (every 20th file, sorted, so
all 20 folders are represented) to 32-bit float, reshape to 256×2048, and for every interior
frame `i` compute the residual of predicting it from its neighbours,
`err = frame[i] − ½(frame[i−1] + frame[i+1])`, expressed in dB relative to that frame's own
RMS. That residual *is* the error a linear interpolator makes at the midpoint, up to a factor
of 2.

```
table                                          medErr dBr maxErr dBr rmsSpread dB   peak
Sweeps/Static                                     -135.57     -17.95         2.87  1.000
Waveshaping/Distortion/Beef 2                     -102.23     -60.49         4.04  1.000
Triangles/Tension Triangle                         -95.87     -91.63         0.00  1.000
Chords/Saw Power Chord                             -95.03     -92.88         0.00  1.000
Filters/Classic/Driven Bandpass                    -85.84     -84.02         8.31  1.000
Squares/Dephased Square                            -75.28     -75.28         0.00  1.000
Modulators/Asym Hard                               -68.40     -47.65         0.00  1.000
LFOs/Complex/Folded Steps                          -64.63     -35.16         1.71  1.000
Waveshaping/Noisy/The Crispulator                  -61.86     -57.68         0.13  1.000
Evolving/Byll                                      -54.18     -22.37         5.29  1.000
FM/Growl FM 2                                      -47.89     -20.90         0.00  1.000
Filters/Formants/Squary Boy                        -42.53     -37.39         7.05  1.000
Sines/Sync Power Sine                              -37.93     -20.30         5.95  1.000
Noisy/Bipolar                                      -37.91     -19.56         5.84  1.000
Waveshaping/Folding/Kick Back                      -35.22     -14.29         1.37  1.000
Acoustic/Brass                                     -30.15     -10.54         0.00  1.000
Filters/Sweeping/Phaser Bandpass                   -28.63     -13.05         7.18  1.000
Filters/Combs/Cepstral                             -25.23     -10.68        13.43  1.000
LFOs/Simple/Arpeggios (60 Semis)                   -18.42      -2.62         5.08  1.000
Saws/Sweeping Saw                                  -15.22     -10.53         3.08  1.000
Growls/Ew                                           -9.22      -3.45         7.46  1.000

median-of-medians linear-interp residual = -47.89 dBr over 21 tables (min -135.57, max -9.22)
```

**Read that table.** On a benign table (`Sweeps/Static`) the frame axis is linear to
**−135 dBr** — 256 frames is wild overkill. On an aggressive one (`Growls/Ew`) the *median*
residual is **−9.22 dBr** (the error is **34.6 %** of the frame's own RMS) and the worst frame
is **−3.45 dBr** (**67.2 %** of the frame's own RMS). That is not a subtle artifact — at the
worst frame the linear interpolator is wrong by two-thirds of the signal.

🔑 **The design conclusion: 256 frames plus linear interpolation is transparent for smooth
morph content and openly broken for aggressive content.** Kilohearts ships both in the same
bank and the same oscillator. This is almost certainly what *"Reduced some artifacts in
wavetable interpolation"* (v2.4.5) was about, six years in.

For Terrain: the fix is **not** more frames. The fix is either (a) phase-align adjacent frames
so the linear crossfade behaves (Kilohearts' three Align fixes, §3.6), or (b) interpolate in
the magnitude/phase domain rather than the sample domain. Terrain's `renderBlend` already
does (b) partially — it is a *phasor* average across frames, which is why the inventory
correctly notes it is subtractive-only (magnitude ≤ weighted mean, no new partials).

### 4.5 MEASURED: how Kilohearts actually authors a morph table

Method: decode `Morphs/Saw to Sine.flac` and `Morphs/Saw to Square to Triangle.flac`,
reshape 256×2048, `rfft` each frame, amplitude = `|X|/1024`.

**`Saw to Sine` — it is a filter sweep, not a crossfade, and it is RMS-normalised.**

```
frame   RMS      H1      H2      H3      H4      H5     topHarmonic(>-80dB rel H1)
   0  0.48996  0.5404  0.2702  0.1801  0.1351  0.1081   1023
  32  0.48996  0.5410  0.2705  0.1803  0.1352  0.1082    854
  64  0.48996  0.5422  0.2711  0.1807  0.1355  0.1084    379
  96  0.48996  0.5453  0.2725  0.1815  0.1360  0.1087    163
 128  0.48996  0.5532  0.2758  0.1829  0.1362  0.1079     69
 160  0.48996  0.5728  0.2805  0.1806  0.1290  0.0970     29
 192  0.48996  0.6167  0.2680  0.1414  0.0764  0.0401     12
 224  0.48996  0.6800  0.1321  0.0182  0.0015  0.0001      5
 255  0.48996  0.6929  0.0000  0.0000  0.0000  0.0000      1
```

Findings, all measured:

* **The RMS of every one of the 256 frames is 0.489964 — identical to six significant
  figures. Spread = 0.0000 dB.** Every frame was normalised to the same RMS. And
  `0.69291/√2 = 0.48996` exactly: the normalisation target is the RMS of the final pure
  sine. Meanwhile the **peak of frame 0 is exactly 1.0000** and no frame exceeds it.
  That is precisely the documented `Fixes > Normalize` recipe — *"normalizes the volume of
  all frames, measured using RMS … makes sure that no peak goes over 100%"* — executed:
  **equalise RMS across frames, then scale the whole table so the global peak is 1.0.**
* **|DC| is exactly 0.000e+00 on every frame.** DC removal was applied.
* **The morph is a low-pass sweep.** `topHarmonic` falls 1023 → 854 → 379 → 163 → 69 → 29 →
  12 → 5 → 1. From frame 64 onwards the cutoff drops by a factor of ≈0.42 every 32 frames,
  i.e. ≈**1.25 octaves per 32 frames ≈ 0.039 octaves per frame**, spanning ≈9.7 octaves.
  Nobody crossfaded a saw into a sine; somebody keyframed the **Filter tool** across the table
  and let `Normalize` clean up.
* **H1 rises 0.54042 → 0.69291 = +2.156 dB across the table** — purely a *consequence* of the
  constant-RMS normalisation, not an authored gesture. As the upper partials leave, the
  fundamental has to grow to hold the RMS.

**`Saw to Square to Triangle` — three mathematically exact corners on an even grid.**

```
frame     H1    H2/H1     H3/H1   H5/H1   H7/H1    RMS      identity
    0  0.6366  5.00e-01  0.3333  0.2000  0.1429  0.57735   exact SAW
   86  1.2732  1.34e-08  0.3332  0.1997  0.1425  0.99609   exact SQUARE
  170  0.8106  0.00e+00  0.1111  0.0400  0.0204  0.57735   exact TRIANGLE
  255  0.6366  5.00e-01  0.3333  0.2000  0.1429  0.57735   exact SAW  (bit-identical to frame 0)
```

Those are not approximations. Frame 0: `H1 = 0.6366 = 2/π`, `Hn ∝ 1/n`, `RMS = 0.57735 = 1/√3`
— a mathematically exact band-limited sawtooth of unit peak. Frame 86: `H1 = 1.2732 = 4/π`,
odd harmonics only (`H2/H1 = 1.3e-08`), `H3/H1 = 1/3`, `H5/H1 = 1/5`, `H7/H1 = 1/7`,
`RMS = 0.996` — an exact square. Frame 170: `H1 = 0.8106 = 8/π²`, odd only,
`H3/H1 = 0.1111 = 1/9`, `H5/H1 = 0.0400 = 1/25`, `H7/H1 = 0.0204 = 1/49` — an exact triangle,
`Hn ∝ 1/n²`.

* Frames **86 … 170** have `H2/H1 < 1e-6` — an **85-frame stretch of strictly odd-harmonic
  content**, i.e. the whole square→triangle leg preserves the odd-only structure exactly.
  A naive time-domain crossfade would *not* do that, because band-limited square and triangle
  do not share a phase spectrum; a **spectral** morph does. This is the **Morph tool's
  "spectral" option** visible in the output. **[MANUAL]** confirms the option exists:
  *"You can either do a **linear or spectral morph**."*
* Frame **255 is bit-identical to frame 0** (`‖f255 − f0‖ = 0.00e+00`). The table returns to
  its start; the keyframes sit at 0 / 86 / 170 / 255, an even ≈85-frame split of the axis.
* This table is **not** RMS-normalised: RMS spread is **4.77 dB** (0.5774 at the saw corners
  to 0.9998 mid-morph). Only **6 of the 21** sampled factory tables are RMS-flat. So
  `Normalize` is a *choice per table*, not a house style.
* **Peak is 1.0000 on all 21 sampled tables** — peak normalisation *is* the house style.
* The largest adjacent-frame difference in this table is **1.0000, between frames 85 and 86,
  at sample 1024** — which is exactly the discontinuity sample of the band-limited square.
  The RMS of that same difference is only 0.059. It is an **edge-sample effect at the
  waveform's own discontinuity**, not a spectral discontinuity in the morph. Worth knowing
  because a naive "max |Δ| between frames" gate would flag it as a defect and be wrong.

### 4.6 Smoothing across frames — the complete answer

Phase Plant offers frame-axis smoothing in exactly **three** places, and all three are
**offline authoring tools**, never a runtime control:

1. **Morph tool** — place keyframes, the tool fills the frames between them with a
   **linear or spectral** crossfade. **[MANUAL]** `/docs/wavetables#morph_tool`
2. **Frame Blend effect** — symmetric weighted mixture of `2·Distance+1` adjacent frames.
   **[MANUAL]** §3.3
3. **Brush / Harmonic Edit / Pen / Wave / Filter tools** — keyframe animation with **linear**
   interpolation of the *tool's parameters* between keyframes. **[MANUAL]**:
   *"Inbetween frames are interpolated linearly from the keyframes."*

**There is no runtime frame smoothing knob.** The `Frame` parameter goes to the oscillator
raw; whatever smoothing you want must be baked into the table, or applied to the *modulation
signal* using the Slew Limiter modulator.

🚨 **This is the architectural difference from Terrain and it is worth stating flatly.**
Terrain's `blur` is a **live, modulatable knob** whose every movement forces a
`rebuildMorphIfNeeded` → `buildFromSpec` cycle at ~20 Hz per oscillator, measured at
**~21 ms per bake** (00-INVENTORY escalation #2), i.e. **~40 % message-thread duty per
oscillator**. Kilohearts made the same operation an **offline commit** and pays **zero**
runtime cost. Terrain's version is more expressive and vastly more expensive. Both facts
are true; the question the team should answer is whether `blur` needs to be modulatable at
all, or whether a small set of **pre-baked blur levels** with a cheap runtime crossfade
between them would buy back 40 % of a thread.

---

## 5. SAMPLE → WAVETABLE CONVERSION (the closest thing to resynthesis)

**[MANUAL]** `/docs/wavetables#sample_conversion`. Implemented as a **keyframe-animated modal
tool**, i.e. offline, in the editor.

| Control | What it does | Numbers |
|---|---|---|
| **Root pitch** | *"the tool needs to know the root pitch of the sample. It will try to **detect this automatically**, but you can also edit the root pitch manually"* | detection algorithm undocumented |
| **Pitch bend** (per keyframe) | offsets that keyframe's pitch from the root, *"for samples where the pitch is not static. Playing around with the pitch bend can also create interesting effects **similar to a formant shift**"* | range undocumented |
| **Source** (per keyframe) | which position in the sample this keyframe maps to | |
| **Mix** | *"The old wavetable data is still present underneath the sample data you are importing, and you can **blend between them** using the mix parameter"* | |
| **Phase alignment strategy** | *"Since it is unlikely that the root pitch is spot on for the whole sample, some **phase drift** will occur. The tool mitigates this problem by trying to **align the phases**. You can choose between a **few different strategies**"* | strategies not named, not counted |

🔑 The important admission is the phase-drift one. Kilohearts state outright that
single-cycle extraction from a real sample **cannot** be phase-coherent, that they mitigate
it with alignment, and that **no single alignment strategy wins** — they ship several and
tell you to *"try them out to see which one performs best for your use case."* That is an
honest vendor statement about a hard problem, and it is the same problem Terrain's `toSpec`
faces on import.

**[CHANGELOG]** v1.8.5 or nearby: *"Added error messages for failing to import samples in the
Wavetable editor, as well as **a limit on sample length** to avoid bad behaviour on too big
samples."* — the limit is not published.

There is **no runtime resynthesis**. A converted sample becomes a static 256×2048 table
embedded in the preset.

---

## 6. CPU COST CLASSES

Terrain's constraint (00-INVENTORY): wavetables rebuild on the **message thread**, measured
**~21 ms** per bake, gated to ~20 Hz per oscillator. Here is where every Phase Plant
mechanism sits.

| Mechanism | Cost class | Evidence |
|---|---|---|
| Analog / Noise / Sample / Wavetable oscillator playback | **per-sample, per-voice, per-unison-voice** | by construction |
| Wavetable frame interpolation | **per-sample** — 2 table reads + 1 lerp, within one mip level | **[INFERRED]** §4.3 |
| Wavetable **Bandlimit** | **per-sample or free** — a mip-level clamp, applied *before* phase modulation | **[MANUAL]** + `WavetableMipMaps` **[MEASURED]** |
| Mip ladder construction (wavetable, sample, LFO table, waveshaper, filter table — **5 ladders**) | **background task** | `SampleMipMapGenerationTask` + `TaskQueue` **[MEASURED]**; **[CHANGELOG]**: *"Fixed wavetable oscillator being stuck in **processing mode** for a long time after doing many small edits in the wavetable editor"* — a visible async processing state |
| **All 16 wavetable-editor effects** | **offline, one commit, zero runtime** | *"click on the button labelled **done** … to **commit** your changes to the wavetable"* **[MANUAL]** |
| Sample → wavetable conversion | **offline** | **[MANUAL]** |
| Disperser | **per-sample**, `amount` biquads/channel (default 18) | **[FORMAT]** `amount: u32` |
| Phaser | **per-sample**, `Order` biquads, 1…7 | **[MEASURED]** auval |
| Comb Filter, Haas, Resonator, Ring Mod, Frequency Shifter, Phase Distortion, Formant Filter | **per-sample**, a delay line / a few biquads / a multiply | **[MANUAL]** + **[MEASURED]** |
| Pitch Shifter | **per-block** overlap-add, 20–200 ms grains, + lookahead in Correlated mode | **[MEASURED]** auval |
| Multipass band splitter | **per-sample**, Linkwitz–Riley, up to 5 bands | **[MANUAL]** |
| Convolver IR precomputation | **offline / on parameter change** — *"Requires a precomputation step before the effect can be heard"* | **[MANUAL]** |
| Convolver runtime | **per-block**, probably partitioned FFT | **[INFERRED]** |
| Polyphonic Snapin lanes | **per-voice** effect processing; *"polyphonic mode can only be enabled on the lanes in order from left to right"* | **[MANUAL]** |

**The headline:** Phase Plant spends **zero audio-thread cycles on spectral processing**.
Its entire spectral surface is offline table baking whose result is stored in the preset.
Its most expensive routine operation is the **mip ladder rebuild**, which it moves to a
**background task with a visible "processing" state in the UI**.

🚨 Terrain does the equivalent work on the **message thread**, synchronously, at 20 Hz, at
21 ms a go. Kilohearts' answer to the same problem — a task queue plus a UI state that says
"I'm busy" — is available to Terrain and is the obvious remedy for escalation #2.

---

## 7. WHAT TERRAIN SHOULD TAKE, AND WHAT IT SHOULD NOT

**Take:**

1. **1024 harmonics, not 96.** Phase Plant's 2048-sample frame carries 1024 partials and the
   factory bank genuinely uses all of them. Terrain's `kMaxPartials = 96` truncation is the
   single largest fidelity gap and it is the measured cause of the −18 to −25 dBr step at
   `amount 0 → 1e-6` (00-INVENTORY escalation #1).
2. **Bandlimit applied *before* phase modulation.** One clamp on `mipLevelForPhaseIncrement`
   ahead of `applyPhaseWarp`. Phase Plant ships it as a per-oscillator knob; Terrain has the
   machinery and not the control.
3. **The three Align fixes**, especially **Align Frames** (a pure per-frame time shift chosen
   to maximise correlation with a reference frame). This is the correct, cheap repair for
   inter-frame phase disagreement, and it is what makes a linear frame crossfade behave.
   Terrain has nothing equivalent.
4. **Phase Offset's one-knob blend** between `φ_h = h·θ` (a time shift) and `φ_h = θ`
   (a Hilbert rotation). Two canonical extremes, no dead zone, satisfies the
   "params evolve 0→100" hard rule that `SpectralPhaser` currently breaks.
5. **The Normalize recipe, measured:** equalise **per-frame RMS**, remove DC, then scale the
   whole table so the **global peak is exactly 1.0**. Terrain's `renderBlend` already
   RMS-matches (`G = √(Σref²/Σpre²)`) but does it per-blend, not as a table-wide fix.
6. **Formant frequencies are absolute, not F0-relative.** F1 ∈ [200, 900] Hz,
   F2 ∈ [500, 2500] Hz, Q ∈ [2, 16]. Delete Terrain's hard-wired 130.81 Hz.
7. **Comb Filter's mono-compatible stereo trick:** flip the comb polarity on the right
   channel only, so the mono sum is exactly the dry signal.
8. **Granular Density = a target overlap count, not a rate** — and make it the *default*,
   with Free Rate (Hz) and Synced Rate as alternatives. Kilohearts shipped fb416's fix as
   their default mode.
9. **Move the bake off the message thread onto a task queue with a visible processing state.**

**Do not take:**

1. **Frame Blend's `Distance`.** Terrain's continuous Gaussian σ with an exact convex
   dry/wet blend is a better control than an integer half-width.
2. **The generic "automation slot" parameter surface.** Terrain's fixed, fully-enumerated
   parameter tree is worth its bookkeeping; 64 numbered slots is a worse user experience.
3. **Any belief that Kilohearts' Snapins are spectral.** They are not, and copying a
   "frequency shifter" as an FFT bin rotation would be both slower and worse-sounding than
   the Hilbert SSB approach the range strongly implies.
4. **Random per-partial phase scatter as the *only* smear.** Terrain's `Smear` has
   `seeded phase scatter a·4.1`; Kilohearts' `Disperse` is **monotonic in partial index**.
   Add the monotonic one; keep the random one as a separate mode, and do not conflate them.

---

## 8. WHAT I COULD NOT DETERMINE

Stated plainly so nobody mistakes a gap for a fact.

1. **The runtime frame interpolation algorithm.** Not in the manual. §4.3 is an inference from
   `frame: f32`, the mip ladder, and the existence of three phase-alignment repair tools.
   Verifiable by rendering the installed AU at `frame = 32.5` and differencing against a 50/50
   mix of frames 32 and 33. **Not done.**
2. **Every parameter range in the wavetable editor** — Frame Blend's `Distance`, Disperse's
   amount, Reset Phases' angle, Comb's warp, Automatic EQ's target slope, Phase Offset's blend
   scaling, Symmetrize and Fix Seam entirely. Kilohearts publish prose only, and the UI label
   strings are not extractable from `HeartCore`. Requires a live GUI session.
3. **Disperser's Amount and Pinch min/max.** Default 18 and 0.5 **[FORMAT]**; 10 and 3.0
   observed in presets. The plugin is not installed here so I could not `auval` it.
   Compare: Phaser's equivalent `Order` is a published 1…7.
4. **Convolver's partition size, latency, and whether it is FFT at all.** Paid plugin, not
   installed, manual silent.
5. **Frequency Shifter's implementation.** Hilbert SSB is a strong inference (§2.3), not a
   verified fact.
6. **The Noise generator's slope filter topology.** −3.0103 dB/oct is not an integer-order
   response.
7. **The band-limiting used inside the wavetable oscillator's mip ladder** — number of levels,
   crossover policy, filter design. `WavetableMipMaps` exists; nothing else is knowable from
   outside. (Terrain uses 34 levels.)
8. **The Morph tool's "spectral" mode maths.** §4.5 shows it preserves exact odd-only
   structure across an 85-frame leg, which rules out a plain time-domain crossfade, but the
   interpolation law (magnitude-linear? magnitude-log? phase-unwrapped?) is not published and
   I did not solve for it. **This is the single highest-value follow-up measurement**, because
   it is precisely the operation Terrain needs for its own morph — and the data to solve it is
   sitting in `Morphs/Saw to Square to Triangle.flac` on this machine.

---

## 9. SOURCES

**Official Kilohearts documentation** (this *is* the manual; retrieved 2026-08-23):

* Phase Plant — `https://kilohearts.com/docs/phase_plant`
  (sections `#generator_area`, `#analog_oscillator`, `#granular_generator`,
  `#noise_generator`, `#sample_player`, `#wavetable_oscillator`, `#generator_groups`,
  `#audio_rate_modulation`)
* Wavetables / wavetable editor — `https://kilohearts.com/docs/wavetables`
  (`#wavetable_editor`, tools, `#sample_conversion`, effects, fixes)
* Kilohearts Essentials (Snapins) — `https://kilohearts.com/docs/snapins`
* Disperser — `https://kilohearts.com/docs/disperser`
* Convolver — `https://kilohearts.com/docs/convolver`
* Multipass — `https://kilohearts.com/docs/multipass`
* Samples, Grains and IRs — `https://kilohearts.com/docs/samples`
* Curves, LFOs, Remaps and Shapes — `https://kilohearts.com/docs/curves_lfos_remaps`
* Changelog — `https://kilohearts.com/changelog`
  (v1.7.4 2019-07-02 noise band limits · v1.8.4 2020-03-23 Squarify/Frame Blend/Distortion ·
  v2.1.1 2023-10-04 Symmetrize + Fix Seam · v2.4.5 2025-12-11 wavetable interpolation artifacts)
* Product pages — `https://kilohearts.com/products/disperser`,
  `.../frequency_shifter`, `.../phase_distortion`

**Independent format authority:**

* Sheldon Young, `synthahol-phase-plant` — `https://github.com/softdevca/synthahol-phase-plant`
  (`src/generator/wavetable_oscillator.rs`, `noise_generator.rs`, `granular_generator.rs`,
  `src/io/generators.rs`, `src/effect/disperser.rs`, `frequency_shifter.rs`,
  `formant_filter.rs`, `phase_distortion.rs`, `resonator.rs`, `ring_mod.rs`,
  `pitch_shifter.rs`, `comb_filter.rs`). Not a Kilohearts product.
* Companion tool `kibank` — `https://github.com/softdevca/kibank`

**Third-party library identified inside the shipped binary:**

* Laurent de Soras, **FFTReal** — `http://ldesoras.free.fr/prod.html`
  (RTTI symbol `ffft::FFTReal<float>` present in `HeartCore`)

**Measurements performed on this machine (2026-08-23):**

* Phase Plant **2.4.6**, `/Library/Audio/Plug-Ins/Components/Phase Plant.component`
  (`aumu` / `kphp` / ` kHs`), and Kilohearts Essentials 2.4.6 AudioUnits.
* `auval -v aufx <subtype> " kHs"` for `kscf` (Comb Filter), `ksvf` (Formant Filter),
  `ksha` (Haas), `kspd` (Phase Distortion), `ksre` (Resonator), `ksrm` (Ring Mod),
  `ksps` (Pitch Shifter), `ksfi` (Filter), `ksun` (Ensemble), `ksph` (Phaser),
  `ksst` (Stereo); and `auval -v aumu kphp " kHs"` for Phase Plant's 94 published parameters.
* `strings -a` over
  `/Library/Application Support/Kilohearts/HeartCore.core/Contents/MacOS/HeartCore`
  (86 089 568 bytes) → 224 054 strings; RTTI class-name extraction.
* `afinfo` over all 402 files in
  `/Library/Application Support/Kilohearts/dependencies/factory_wavetables/`.
* `afconvert -f WAVE -d LEF32` + NumPy 2.0.2 `rfft` analysis of
  `Morphs/Saw to Sine`, `Morphs/Saw to Square to Triangle`, `Morphs/Saw to Square`,
  `Morphs/Default Wavetable`, `Spectral/Harmonic Blend`, `Spectral/Overtone Scanner`,
  `Sweeps/Freq Shift`, `Sweeps/Phase Sweep 1`, `Filters/Combs/Bandspread`,
  plus a 21-table stratified sample of the full bank (every 20th file, sorted).
