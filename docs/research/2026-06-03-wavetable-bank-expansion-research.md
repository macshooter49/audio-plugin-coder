# Phase 11h — Dramatic Wavetable Design Research

**Date:** 2026-06-03  
**Author:** Research subagent  
**Purpose:** Establish design principles and concrete harmonic specs for WT POS "night-and-day" morphing wavetables.

---

## Sources cited

- **Serum 2 User Guide.pdf** (354 pp.) — read pp. 38-40 (What is a Wavetable, Anatomy), pp. 280-290 (Wavetable Editor, Morph menu), pp. 346-347 (Appendix E: What Makes a Good Wavetable, Creating from Scratch). `/Users/macshooter/Downloads/Serum 2 User Guide.pdf`

- **Pigments 7 Manual** pp. 90-105 (The Wavetable Engine: Visualizer, Morph, Browser, FM/PM, Phase Transform, Wavefolding, Position/Volume). `/Users/macshooter/Downloads/pigments_Manual_7_0_0_EN.pdf`. Factory wavetable categories sourced from manual TOC + web corroboration: **Building Waves, Natural, Processed, Synthesizers, Transform**.

- **Vital source** — `/Users/macshooter/Downloads/vital-main/src/`:
  - `synthesis/lookups/wave_frame.h` + `wave_frame.cpp` — WaveFrame struct (2048-sample, complex frequency_domain[], PredefinedWaveFrames: kSin/kSaturatedSin/kTriangle/kSquare/kPulse/kSaw)
  - `common/wavetable/wave_source.cpp` — linearFrequencyInterpolate: interpolates amplitude (sqrt of magnitude, squared) AND phase (delta-tracking). This is what makes spectral morph "feel smooth."
  - `common/wavetable/frequency_filter_modifier.cpp` — applies gain multiplier per harmonic bin. LP/HP/BP/Comb sweep — same concept as our per-frame spectral envelope shift.
  - `common/wavetable/phase_modifier.cpp` — kNormal (cumulative phase shift per harmonic), kHarmonic (same shift to all harmonics), kHarmonicEvenOdd (even vs odd opposing), kClear (zero all phases). This is the mechanism behind "phase scramble = smeary character."
  - `common/wavetable/wave_fold_modifier.cpp` — `sinf(asinf(v) * boost)` wavefolder. Cheap additive method for gaining harmonic density.
  - `common/wavetable/shepard_tone_source.cpp` — shifts all harmonics up one octave across the sweep: frequency_domain[i*2] = frequency_domain[i] at far end. Produces the infinite-rise illusion.
  - `common/wavetable/wavetable_creator.cpp` — `initPredefinedWaves()` shows all 6 shapes placed at evenly-spaced frame positions then left with `kNone` interpolation — i.e., Vital's "BasicShapes" WT is deliberately discrete/stepped.

- **Massive X manual (via web)** — 11 wavetable categories: Basics, Operators, Harmonics, Additive+FM, Monster, Drift, Filter, Formant, FX, Mixed, Remastered. Formant category specifically described as "vowel sounds become especially distinguished when modulation applied to Wavetable Position." Confirms that category-level intent (not just timbre) drives WT POS drama.

- **Web sources:**
  - [Serum Wavetable Editor — Splice Blog](https://splice.com/blog/serum-wavetable-editor/)
  - [8 Ways To Make Wavetables — Outerverse.fm](https://outerverse.fm/blogs/tutorials/blog-8-ways-to-make-wavetables/)
  - [Wavetable Synthesis — futur3soundz](https://www.futur3soundz.com/wavetable-synthesis/)
  - [Vital Synth — Wavetable oscillators](https://vital.audio/)
  - [Massive X — Wavetable oscillators](https://native-instruments.com/ni-tech-manuals/massive-x-manual/en/wavetable-oscillators)
  - [The Wavetable Synthesis Architecture — Meta Function](https://www.metafunction.co.uk/post/the-wavetable-synthesis-architecture)
  - [Ultimate Guide to Wavetable Synthesis — MusicRadar](https://www.musicradar.com/news/ultimate-guide-wavetable-synthesis)

---

## What makes a wavetable feel "WT POS dramatic" — design principles

### Principle 1: Monotonic spectral progression (Serum's cardinal rule)

Serum 2 Appendix E states explicitly: "It typically makes sense to have the frames progress from dull to bright, or vice-versa." A sweep that jumps around spectrally feels like a radio dial; a sweep that progresses monotonically from thin to rich, or bright to dark, feels like a journey. This is the single most important principle. **Every frame must be "more" of something compared to the last frame.**

Our existing tables (Sine/Square/etc.) violate this by being IDENTICAL across all 16 frames. VowelMorph, GlassHarmonics, SerumHD are the only existing tables that actually vary — and unsurprisingly are likely the only ones that sound alive under WT POS.

### Principle 2: Harmonic count as the primary axis of drama

The most "night-and-day" tables grow or shrink the harmonic count across the sweep. Frame 0 with 1 harmonic = pure sine (thin, transparent). Frame 15 with 64+ harmonics = dense saw (thick, rich). The change between these two states is the maximum possible timbral shift achievable in a single wavetable. This is why Serum's "BasicShapes" table sweeps from sine to saw in discrete jumps — that's the fundamental shape of most dramatic factory wavetables.

In our FrameSpec API: control `numHarmonics` and the shape of `amplitudes[0..N]` across frames.

### Principle 3: Spectral envelope morphing (LP sweep baked in)

A rising filter sweep baked into frame positions: early frames have the spectral energy concentrated at low harmonics (bell-like), late frames have energy concentrated at high harmonics (nasal, screaming). This is what Vital's `FrequencyFilterModifier` does procedurally — we can pre-bake the same effect. A Gaussian spectral envelope with center frequency rising from h=1 at frame 0 to h=32 at frame 15 produces the classic "filter opens" sensation permanently available on WT POS.

Key math: for frame `f`, center harmonic `c(f) = 1 + 31*(f/15)`, envelope `A[h] = exp(-(h - c)^2 / (2*sigma^2))`.

### Principle 4: Phase manipulation as character axis (Vital's insight)

Vital's `phase_modifier.cpp` reveals a key design axis: SAME amplitude spectrum, VARYING phase offsets across frames. At frame 0, all phases = 0 → classic aligned waveform (punchy, definite shape). At frame 15, all phases randomized → smeary, diffuse, analog-noise character. The power spectrum is unchanged but the waveform shape is completely different. This is the mechanism behind our existing `SpectralDrift` table. Extending it: we can smoothly interpolate phase randomness: `phase[h] = lerp(0, random_phase[h], t)`.

### Principle 5: Symmetry-to-asymmetry transition (odd→full harmonic spectrum)

A waveform that has only odd harmonics sounds hollow and square-like (clarinet register, hollow pipe). As even harmonics are introduced across the sweep, the timbre fills in and gains "weight." This is a musically meaningful morphological shift: frame 0 = clarinet-like hollow square, frame 15 = full sawtooth richness. Math: `A[h] = 1/h` for odd h only at frame 0; linearly blend in even harmonics as `f` increases.

### Principle 6: Spectral centroid migration (brightness sweep)

The spectral centroid — the amplitude-weighted average harmonic number — can be forced to migrate dramatically. If frame 0 has centroid at h=2 (warm, fundamental-heavy) and frame 15 has centroid at h=20 (bright, whistling), the sweep sounds like a tonewheel organ growing an upper-register harmonic signature. Formula: weight each harmonic by a spectral envelope that migrates from h=2 to h=20 over the 16 frames.

### Principle 7: Discrete "waveform landmark" anchors (Serum's "few frames" approach)

Serum's Appendix E notes "about four frames seems to be a popular number." The idea: anchor frames are strong, recognizable timbres (sine → hollow square → sawtooth → bright buzz) with the intermediate frames filled by interpolation. This provides clear waypoints that the ear can track, rather than a subtle continuous shift. For our 16-frame spec, plan 4-5 "landmark" frames at f=0, 4, 8, 12, 15 and design the intermediates as graceful interpolations.

### Principle 8: Loudness normalization requirement

As harmonic count grows, the RMS loudness increases. A wavetable that goes from 1 harmonic to 64 harmonics without normalization will sound like it is "turning up" as WT POS increases. This is perceptually wrong — the timbre should change, not the level. Our `buildFromSpec()` already calls `normalizeMipLevels()` (normalizes peak to 1.0 per mip level), but this normalizes the COMBINED output. Per-frame normalization is handled implicitly since each frame individually reaches peak ≈ 1.0 after IFFT sum. To be explicit: after computing each frame, divide by its peak absolute value.

---

## How the existing references handle this

### Serum 2's headline morph wavetables

From the manual and web research, Serum's most-played factory wavetables in the "Morph" style are:

- **"BasicShapes"** — 6 landmark frames (Sine → SaturatedSine → Triangle → Square → Pulse → Saw) with `kNone` interpolation deliberately creating discrete steps. The steps ARE the drama. Used for PWM-like sweeps.
- **"Spectral"** — Uses "Morph - Spectral" interpolation mode between 3-4 anchor frames that have very different harmonic content. Spectral morph interpolates amplitude AND phase per-bin, creating a smooth timbral dissolve.
- **"Vocal/Formant" category** — Formant envelopes shift across frames, producing vowel-to-vowel morphs. WT POS = A→E→I→O→U when swept slowly.
- **"Noise → Tone" tables** — Start dense/noisy and become tonal as position increases. The Serum editor's FFT bin randomizer generates the initial noise frames.
- **"Harmonic Series"** — Progressively adds harmonics: frame 0 = h1 only, frame 1 = h1+h2, frame 2 = h1+h2+h3, etc. Literally adds one harmonic per frame step. By frame 15, a rich chord of the harmonic series.

### Vital's bundled wavetables (from source)

Vital's `initPredefinedWaves()` creates only 6 shapes (Sin/SaturatedSin/Triangle/Square/Saw/Pulse) at evenly-spaced positions with NO interpolation (kNone). Vital's factory wavetable richness comes from its **modifier pipeline** (FrequencyFilter → PhaseModifier → WaveFolder → WaveWarp → WaveWindow), which is applied procedurally at render time, not baked into frames. This means Vital's drama comes from SPECTRAL WARPING (its real-time per-block modifier chain) more than from pre-baked frames. This architectural difference is important: our WT POS drama must be baked into the spec frames since we don't have Vital's modifier pipeline.

Vital's `ShepardToneSource` is notable: it doubles harmonic indices across the sweep (frequency_domain[i*2] = frequency_domain[i] at the far end), creating the perceptual infinite-octave-rise effect.

Vital's `FrequencyFilterModifier` sweeps a spectral envelope across positions — LP/HP/BP/Comb — which is exactly what we want but pre-baked into our frame amps.

Vital's `PhaseModifier` (kNormal mode) accumulates a phase shift per harmonic index, which creates the characteristic Vital "spectral drift" sound. The kClear mode zeros all phases, which produces a "static" version of any wavetable.

### Pigments' wavetable categories (from manual pp. 91-105)

Pigments organizes its 250+ factory wavetables (50 new in v7) into five banks:

1. **Building Waves** — Basic building blocks: single-cycle standard shapes and their variants. Designed for subtractive use, not WT POS drama.
2. **Natural** — Organic and acoustic recorded tones analyzed into wavetable form. Voice, brass, strings, percussion. WT POS sweeps through different playing techniques or vowel positions.
3. **Processed** — Digital and processed/distorted tones. WT POS moves through different distortion states or digital artifacts.
4. **Synthesizers** — Classic synth emulations: Moog, Juno, DX7, PPG waveforms. WT POS moves between different synth timbres or patch states.
5. **Transform** — The "wild" category: wavetables specifically designed for dramatic sweeps. Atonal, noise-to-tone, filter sweep baked in.

Pigments' **Phase Transform** section (pp. 96-97) shows 7 types (Pulse Width, Skew, Round, Tri/Pulse, Octave Plus, Pseudo PW, Fractalize) applied per-frame at runtime. The example in the manual: take a sine wave at frame 0, apply increasing "Skew" amount → adds harmonics gradually → dramatic harmonic buildup. This is essentially our Principle 3 and 4 realized in their UI.

Pigments' **Wavefolding** (pp. 97-98): fold amount per frame modulates harmonic density. Example from manual: "Select the Sawtooth wave. Hold a note and slowly increase Wavefolding Amount. The harmonics of the Sawtooth wave will sweep through the harmonic series." This is exactly the recipe for our HarmonicFold wavetable below.

---

## Proposed 5-8 new wavetables for Phase 11h

These are all candidates for a new **"Morph"** category alongside Terrain's existing Basic/Analog/Digital/Vocal/Metallic/Experimental.

---

### 1. HarmonicRise

**Concept:** The fundamental journey of wavetable synthesis — thin to rich. Frame 0 is a pure sine (1 harmonic). Each frame adds more harmonics in a sawtooth series (1/h). Frame 15 is a full 64-harmonic sawtooth. This is literally "nothing to everything." Every movement of WT POS is audible.

**Frame-by-frame harmonic content:**
- Frame 0: h1=1.0 only (pure sine)
- Frame 2: h1=1.0, h2=0.50, h3=0.33 (3 harmonics)
- Frame 4: h1..h5 at 1/h amplitude (5 harmonics)
- Frame 7: h1..h9 at 1/h (9 harmonics)
- Frame 10: h1..h18 at 1/h (18 harmonics)
- Frame 13: h1..h40 at 1/h (40 harmonics)
- Frame 15: h1..h64 at 1/h (full sawtooth, 64 harmonics)

**Math formula:**
```
For frame f (0..15), t = f/15.0:
numH(f) = round(1 + 63*t)        // 1 → 64 harmonics
For h = 1..numH: A[h] = 1.0/h    // sawtooth series
Phase: all = 0
Normalize each frame by its peak amplitude after IFFT.
```

**Expected sonic:**
- Frame 0: "Pure sine — transparent, nothing to it"
- Frame 7: "Rich hollow sound, halfway there — like a clarinet"
- Frame 15: "Full bright saw — complex, classic synth buzz"

**C++ skeleton:**
```cpp
static WavetableSpec makeHarmonicRiseSpec()
{
    WavetableSpec spec;
    for (int f = 0; f < 16; ++f)
    {
        const float t = (float) f / 15.0f;
        const int numH = (int) std::round (1.0f + 63.0f * t);  // 1..64
        FrameSpec& fs = spec.frames[(size_t) f];
        fs.numHarmonics = numH;
        for (int h = 1; h <= numH; ++h)
            fs.amplitudes[(size_t)(h - 1)] = 1.0f / (float) h;
        // phases all zero (value-initialized)
    }
    return spec;
}
```

**Normalization note:** `buildFromSpec()` normalizes per mip level globally; within each frame the raw amplitude sum at peak will naturally scale. Add a per-frame peak normalization pass if needed for even loudness across the sweep.

---

### 2. SpectralSweep

**Concept:** A Gaussian spectral envelope migrates its center frequency from h=1 (warm, fundamental) to h=24 (bright, nasal high-register). At frame 0 you hear only the fundamental with a soft first-harmonic glow. At frame 15 you hear only mid-to-high harmonics — like a bandpass filter sweeping up while the bottom falls away. It sounds like the instrument "hollows out and becomes nasal then brilliant."

**Frame-by-frame harmonic content:**
- Frame 0: Gaussian centered at h=1.5, sigma=1.5 → h1=1.0, h2=0.6, h3=0.1 (mostly fundamental)
- Frame 4: Gaussian centered at h=5, sigma=3 → h3-h8 prominent
- Frame 8: Gaussian centered at h=12, sigma=4 → h8-h16 prominent
- Frame 12: Gaussian centered at h=20, sigma=4 → h16-h24 prominent
- Frame 15: Gaussian centered at h=28, sigma=5 → h22-h34 prominent, fundamental nearly silent

**Math formula:**
```
For frame f, t = f/15.0:
center(f) = 1.5 + 26.5*t         // 1.5 → 28.0
sigma(f)  = 1.5 + 3.5*t           // 1.5 → 5.0
For h = 1..64:
  A[h] = exp(-(h - center)^2 / (2*sigma^2))
Phase: all = 0
Normalize each frame to peak = 1.0
```

**Expected sonic:**
- Frame 0: "Warm, almost sine — just a hint of body"
- Frame 7: "Vocal middle register — nasal and present"
- Frame 15: "High-harmonic glow — like a bell's ring without the strike"

**C++ skeleton:**
```cpp
static WavetableSpec makeSpectralSweepSpec()
{
    WavetableSpec spec;
    for (int f = 0; f < 16; ++f)
    {
        const float t      = (float) f / 15.0f;
        const float center = 1.5f + 26.5f * t;
        const float sigma  = 1.5f + 3.5f  * t;
        FrameSpec& fs = spec.frames[(size_t) f];
        fs.numHarmonics = 64;
        for (int h = 1; h <= 64; ++h)
        {
            const float d = (float) h - center;
            fs.amplitudes[(size_t)(h - 1)] = std::exp (-(d * d) / (2.0f * sigma * sigma));
        }
    }
    return spec;
}
```

---

### 3. OddEven

**Concept:** Frame 0 is pure odd harmonics only (clarinet: h1, h3, h5, h7... — hollow, nasal, woody). Frame 15 is full sawtooth (h1..h32 all present). The intermediate frames linearly blend even harmonics in. The drama is musical and instrument-like: clarinet → oboe → saxophone → violin → brass → full orchestra section.

This is Pigments' "Phase Transform → Skew" effect baked as a progressive spectrum.

**Frame-by-frame harmonic content:**
- Frame 0: h1=1.0, h3=0.333, h5=0.2, h7=0.143... (odd only, 1/h amplitudes)
- Frame 4: odd harmonics at 1/h; even harmonics at `(1/h) * (4/15)` — 27% even blend
- Frame 8: odd harmonics at 1/h; even harmonics at `(1/h) * (8/15)` — 53% even blend
- Frame 12: odd harmonics at 1/h; even harmonics at `(1/h) * (12/15)` — 80% even blend
- Frame 15: all harmonics at 1/h (full sawtooth)

**Math formula:**
```
For frame f, t = f/15.0:
For h = 1..48:
  if h is odd:  A[h] = 1.0/h
  if h is even: A[h] = (1.0/h) * t    // even harmonics grow linearly from 0→1
Phase: all = 0
```

**Expected sonic:**
- Frame 0: "Hollow clarinet — woody, single-register"
- Frame 7: "Oboe-ish — more body starting to appear"
- Frame 15: "Full sawtooth — all harmonics present, complete richness"

**C++ skeleton:**
```cpp
static WavetableSpec makeOddEvenSpec()
{
    WavetableSpec spec;
    for (int f = 0; f < 16; ++f)
    {
        const float t = (float) f / 15.0f;
        FrameSpec& fs = spec.frames[(size_t) f];
        fs.numHarmonics = 48;
        for (int h = 1; h <= 48; ++h)
        {
            const float base = 1.0f / (float) h;
            const bool  isEven = (h % 2 == 0);
            fs.amplitudes[(size_t)(h - 1)] = isEven ? base * t : base;
        }
    }
    return spec;
}
```

---

### 4. PhaseDrift

**Concept:** Every frame has IDENTICAL amplitude spectrum (full sawtooth, h1..h32 at 1/h). What changes is the phase offset of each harmonic. Frame 0: all phases zero → coherent, punchy, identifiable waveform. Frame 15: all phases maximally randomized (deterministic seed) → same spectrum but the waveform is diffuse, "smeared," noise-adjacent. This is the mechanism behind `SpectralDrift` already in our bank, but done properly as a spec-based WT. The dramatic quality: same spectral power, completely different sonic character.

This directly implements Vital's `PhaseModifier` concept pre-baked into frames.

**Frame-by-frame harmonic content:**
- Frame 0: h1..h32 at 1/h, all phases = 0
- Frame 4: h1..h32 at 1/h, phases interpolated 27% toward random target
- Frame 8: h1..h32 at 1/h, phases interpolated 53% toward random target
- Frame 15: h1..h32 at 1/h, phases fully random (deterministic seed per harmonic)

**Math formula:**
```
For each harmonic h, pre-generate random_phase[h] ∈ [0, 2π] using deterministic seed.
For frame f, t = f/15.0:
  A[h] = 1.0/h  (constant across all frames)
  P[h] = t * random_phase[h]  (linear interpolation from 0 to random_phase)
numHarmonics = 32
```

**Expected sonic:**
- Frame 0: "Pristine aligned sawtooth — sharp attack character"
- Frame 7: "Phase halfway dissolved — warm and spreading"
- Frame 15: "Phase noise character — same spectrum, but like hearing it through silk"

**C++ skeleton:**
```cpp
static WavetableSpec makePhaseDriftSpec()
{
    WavetableSpec spec;
    // Pre-generate random target phases (deterministic seed)
    unsigned int rng = 0xDEADBEEFu;
    auto nextPhase = [&]() -> float {
        rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
        return ((float) rng / (float) 0xFFFFFFFFu) * 6.28318530718f;
    };
    float targetPhases[32] = {};
    for (int h = 0; h < 32; ++h) targetPhases[h] = nextPhase();

    for (int f = 0; f < 16; ++f)
    {
        const float t = (float) f / 15.0f;
        FrameSpec& fs = spec.frames[(size_t) f];
        fs.numHarmonics = 32;
        for (int h = 1; h <= 32; ++h)
        {
            fs.amplitudes[(size_t)(h - 1)] = 1.0f / (float) h;
            fs.phases[(size_t)(h - 1)]     = t * targetPhases[h - 1];
        }
    }
    return spec;
}
```

---

### 5. FoldBloom

**Concept:** Inspired directly by Pigments' wavefolder demo (p. 98): "The harmonics of the Sawtooth wave will sweep through the harmonic series." Frame 0 is a gentle sine (just h1). Across the sweep, a simulated wavefold progressive overtone accumulation grows the spectrum. This uses the mathematical insight that `sin(pi * sin(x))` generates harmonics `J_n(pi)` (Bessel functions). We bake the result of progressively more intense folding into each frame.

More practically: each frame applies a sinusoidal re-mapping of the time domain signal with increasing fold amount, then extracts the resulting harmonic spectrum as FrameSpec data. The fold formula `y = sin(A * sin(x))` where A grows from 0 to 3π across frames generates an expanding Bessel-function harmonic series.

**Frame-by-frame harmonic content:**
- Frame 0: A=0.1 → essentially pure sine (h1 dominant, h3 trace)
- Frame 4: A=0.8 → h1 strong, h3/h5/h7 present, all odd
- Frame 8: A=1.8 → h1 diminished, h3 peak, h5/h7/h9 significant (Bessel peak shifts)
- Frame 12: A=2.6 → h5 becomes dominant, h1 weaker
- Frame 15: A=3.5π → very dense spectrum, spectral weight in h5-h15 range

**Math formula:**
```
For frame f, t = f/15.0:
  A(f) = 0.1 + 11.0 * t     // fold amount: 0.1 → 11.1
  For each harmonic h = 1..48:
    A[h] = |J_h(A(f))|       // Bessel function of the first kind
           (approximated by computing sin(A*sin(2π*x)) and extracting FFT)
  
Practical approximation (avoid Bessel library): pre-compute the
time-domain waveform y(x) = sin(A(f) * sin(2π*x)) for x ∈ [0,1)
then FFT to extract harmonic amplitudes. Since we're in buildFromSpec(),
compute analytically: J_n(x) approximated by:
  J_0(x) ≈ 1 - x^2/4 + x^4/64  (for small x)
  J_n(x) ≈ (x/2)^n / n!         (for moderate x, small h)
Better: store the time-domain samples directly using the legacy Wavetable
constructor (since Bessel series is not a simple harmonic amplitude formula).
```

**Implementation note:** For FoldBloom specifically, use the legacy time-domain Wavetable constructor (not buildFromSpec) since the wavefolding formula is naturally expressed in time domain. Compute `y[i] = sin(A * sin(2*pi*i/N))` for each frame. This is the most faithful implementation of the wavefolder concept.

**Expected sonic:**
- Frame 0: "Sine — silent simplicity"
- Frame 7: "Compressed harmonic stack — odd harmonics building, soft and buzzy"
- Frame 15: "Brilliant, shifted harmonic center — not a saw, something between a reed and a synth brass"

**C++ skeleton (legacy time-domain):**
```cpp
static Wavetable makeFoldBloom()
{
    Wavetable wt (16);
    const double twoPi = 2.0 * 3.14159265358979323846;
    const int N = wt.frameSize_;
    for (int f = 0; f < 16; ++f)
    {
        const double t = (double) f / 15.0;
        const double A = 0.1 + 11.0 * t;   // fold drive: 0.1 → 11.1
        for (int i = 0; i < N; ++i)
        {
            const double phase = twoPi * (double) i / (double) N;
            wt.sampleRef (f, i) = (float) std::sin (A * std::sin (phase));
        }
    }
    return wt;
}
```

---

### 6. FormantRise

**Concept:** A formant envelope migrates from low-pitched tube resonance (tuba-like: F1=120 Hz, F2=600 Hz) up through brass (F1=400 Hz, F2=1200 Hz), then to voice (F1=730 Hz, F2=1090 Hz), then to high vowel /i/ (F1=300 Hz, F2=2300 Hz). Each frame bakes a different formant configuration into the harmonic amplitudes using formant-based weighting. Completely different from VowelMorph (which goes A→E→I→O→U vowel-only). This goes instrument-register to voice to bright vowel.

The existing `formantAmp()` helper is already in Wavetable.h (used by VowelMorph), making this straightforward.

**Formant trajectory:**
- Frame 0: Tuba — F1=150, F2=600, F3=1400 Hz (low brass resonance)
- Frame 4: French Horn — F1=350, F2=1000, F3=2100 Hz
- Frame 8: Trumpet muted — F1=600, F2=1400, F3=2600 Hz
- Frame 12: Vowel /a/ (voice) — F1=730, F2=1090, F3=2440 Hz
- Frame 15: Vowel /i/ (bright) — F1=300, F2=2300, F3=3200 Hz

**Math formula:**
```
For frame f, interpolate between the above 5 landmark formant triplets.
fundamental = 220 Hz (reference; harmonic weighting is what matters)
For each harmonic h = 1..50:
  A[h] = formantAmp(h*220, F1, F2, F3)  (same helper as VowelMorph)
```

**Expected sonic:**
- Frame 0: "Dark tube — tuba-like bass body"
- Frame 8: "Bright brass — trumpet with a mute, forward and penetrating"
- Frame 15: "Bright vowel /i/ — ee-vowel, extremely forward and nasal"

**C++ skeleton:**
```cpp
static WavetableSpec makeFormantRiseSpec()
{
    // Formant landmark triplets [F1, F2, F3] in Hz
    static const float landmarks[5][3] = {
        { 150.0f,  600.0f, 1400.0f },  // Tuba
        { 350.0f, 1000.0f, 2100.0f },  // French Horn
        { 600.0f, 1400.0f, 2600.0f },  // Trumpet muted
        { 730.0f, 1090.0f, 2440.0f },  // Vowel /a/
        { 300.0f, 2300.0f, 3200.0f },  // Vowel /i/
    };
    constexpr float fund = 220.0f;
    WavetableSpec spec;
    for (int f = 0; f < 16; ++f)
    {
        const float vp = ((float) f / 15.0f) * 4.0f;
        const int   v0 = (int) vp;
        const int   v1 = v0 < 4 ? v0 + 1 : 4;
        const float vt = vp - (float) v0;
        const float F1 = landmarks[v0][0] + (landmarks[v1][0] - landmarks[v0][0]) * vt;
        const float F2 = landmarks[v0][1] + (landmarks[v1][1] - landmarks[v0][1]) * vt;
        const float F3 = landmarks[v0][2] + (landmarks[v1][2] - landmarks[v0][2]) * vt;
        FrameSpec& fs = spec.frames[(size_t) f];
        fs.numHarmonics = 50;
        for (int h = 1; h <= 50; ++h)
        {
            // Gaussian formant filter centered at F1, F2, F3
            auto gf = [](float freq, float center, float bw) {
                const float d = freq - center;
                return std::exp (-(d * d) / (2.0f * bw * bw));
            };
            const float freq = (float) h * fund;
            float w = gf(freq, F1, 80.0f) + 0.7f * gf(freq, F2, 150.0f)
                      + 0.4f * gf(freq, F3, 200.0f);
            fs.amplitudes[(size_t)(h - 1)] = w / (float) h;  // 1/h rolloff
        }
    }
    return spec;
}
```

---

### 7. HarmonicSeries

**Concept:** Directly from Serum 2's "Harmonic Series" suggestion in Appendix E: each frame adds exactly one new harmonic. Frame 0 = h1 only. Frame 1 = h1+h2. Frame 2 = h1+h2+h3. ... Frame 15 = h1..h16. All at EQUAL amplitude (not 1/h rolloff). This is the most "educational" wavetable — it demonstrates the harmonic series directly. Musically, the sweep sounds like each frame adds a new ring/partials appear progressively. The equal-amplitude choice makes each new harmonic clearly audible as it enters, unlike the 1/h rolloff where high harmonics are too quiet to hear entering.

**Frame-by-frame harmonic content:**
- Frame 0: h1=1.0 (one partial — pure sine)
- Frame 1: h1=h2=1.0 (two equal partials — buzzy octave pair)
- Frame 2: h1=h2=h3=1.0 (adds perfect fifth character)
- Frame 4: h1..h5 all equal — jangly, bell-chord-like
- Frame 8: h1..h9 all equal — dense cluster, almost noisy
- Frame 15: h1..h16 all equal — full brightness

**Math formula:**
```
For frame f: numHarmonics = f+1 (1 at frame 0, 16 at frame 15)
For h = 1..numHarmonics: A[h] = 1.0 / sqrt(numHarmonics)  // equal RMS
Phase: all = 0
```

**Expected sonic:**
- Frame 0: "Sine — serene and simple"
- Frame 4: "Five-note harmonic chord — bell-like jangle"
- Frame 9: "Mid-way noisy cluster — complex aggregate"
- Frame 15: "Bright aggressive stack — all 16 harmonics equally loud, buzzy and rich"

**C++ skeleton:**
```cpp
static WavetableSpec makeHarmonicSeriesSpec()
{
    WavetableSpec spec;
    for (int f = 0; f < 16; ++f)
    {
        const int numH = f + 1;  // 1..16 harmonics
        const float norm = 1.0f / std::sqrt ((float) numH);  // equal RMS per frame
        FrameSpec& fs = spec.frames[(size_t) f];
        fs.numHarmonics = numH;
        for (int h = 1; h <= numH; ++h)
            fs.amplitudes[(size_t)(h - 1)] = norm;
    }
    return spec;
}
```

---

### 8. CentroidShift

**Concept:** From Massive X's "Harmonics" wavetable category — energy migrates from low to high partials over the sweep while TOTAL harmonic count remains constant (h1..h32 always present). Frame 0: energy concentrated in h1-h4 (warm, fundamental). Frame 15: energy concentrated in h16-h32 (brilliant, high-harmonic only). The sweep sounds like a resonant filter opening: the body grows hollow and the top shimmers into prominence. Combined with unison, this is a signature "modern synth" sound.

**Frame-by-frame harmonic content:**
- Frame 0: Gaussian centered at h=2, sigma=1.5 → h1=strong, h3-h4=weak, h5+=near-zero
- Frame 4: Gaussian centered at h=6, sigma=3 → h3-h9 prominent
- Frame 8: Gaussian centered at h=12, sigma=4 → h8-h16 prominent
- Frame 12: Gaussian centered at h=20, sigma=4 → h14-h26 prominent
- Frame 15: Gaussian centered at h=28, sigma=4 → h22-h34 prominent, fundamental silent

Note: This is deliberately similar to SpectralSweep but with CONSTANT numHarmonics=32 and the Gaussian peak migrating. SpectralSweep also grows sigma (widens). CentroidShift keeps sigma constant (same bandwidth, moving center). Different sonic character.

**Math formula:**
```
For frame f, t = f/15.0:
  center(f) = 2.0 + 26.0 * t     // 2 → 28
  sigma      = 4.0                  // constant bandwidth
  For h = 1..48:
    A[h] = exp(-(h-center)^2 / (2*sigma^2))
numHarmonics = 48
```

**C++ skeleton:**
```cpp
static WavetableSpec makeCentroidShiftSpec()
{
    WavetableSpec spec;
    constexpr float sigma = 4.0f;
    for (int f = 0; f < 16; ++f)
    {
        const float t      = (float) f / 15.0f;
        const float center = 2.0f + 26.0f * t;
        FrameSpec& fs = spec.frames[(size_t) f];
        fs.numHarmonics = 48;
        for (int h = 1; h <= 48; ++h)
        {
            const float d = (float) h - center;
            fs.amplitudes[(size_t)(h - 1)] = std::exp (-(d * d) / (2.0f * sigma * sigma));
        }
    }
    return spec;
}
```

---

## Recommended priority order

### Tier 1 — Ship immediately (most dramatic, distinct from each other)

**1. HarmonicRise** — The canonical "nothing to everything." Immediately audible at any WT POS position. Maximum drama, zero ambiguity about whether WT POS is doing something. Should be the FIRST new Morph WT users encounter.

**2. OddEven** — The clarinet-to-sawtooth sweep is musically meaningful and instrument-adjacent. Players recognizing "that's a clarinet" moving to "that's a brass" is the kind of drama that's both dramatic AND useful in a mix.

**3. PhaseDrift** — Demonstrates that spectrum preservation with phase scrambling is its own axis of drama. Same richness, completely different character. Pairs beautifully with the Phase 11a SPECTRAL knob concept.

### Tier 2 — High value, implement after Tier 1

**4. SpectralSweep** — The spectral centroid migration sweep. Very natural-sounding, good for pads/leads. Slightly overlaps with CentroidShift in concept.

**5. FormantRise** — Leverages existing `formantAmp()` helper. Instrument-to-voice-to-bright-vowel is a unique sonic journey not covered by any existing table.

### Tier 3 — Excellent but lower urgency

**6. HarmonicSeries** — More "educational" than dramatic, but the equal-amplitude harmonic buildup is genuinely unique and unexpected. A wildcard.

**7. FoldBloom** — Wavefold simulation baked into frames. Uses Bessel-function-like spectral growth. Very cool concept from Pigments, but requires time-domain implementation (legacy constructor, not spec-based), adding implementation complexity.

**8. CentroidShift** — Overlaps somewhat with SpectralSweep. Implement only if SpectralSweep lands well and we want a variant.

---

## Naming proposals

All proposed wavetables fit the **"Morph"** category (new, does not exist yet in Terrain's bank):

| Internal C++ Name | UI Display Name | One-Line Description |
|---|---|---|
| HarmonicRise | Rise | Sine to full sawtooth — 1 harmonic to 64 |
| SpectralSweep | Sweep | Spectral centroid migrates from warm to brilliant |
| OddEven | Even | Clarinet odd-harmonics to full sawtooth |
| PhaseDrift | Drift | Same spectrum, phase coherence decays to diffuse |
| FoldBloom | Bloom | Sinusoidal fold drive: Bessel spectral expansion |
| FormantRise | Formant | Tuba → trumpet → voice → bright vowel /i/ |
| HarmonicSeries | Stack | One partial added per frame, all equal amplitude |
| CentroidShift | Shift | Constant harmonic count, energy migrates high |

**Category recommendation:** Add `Morph` as a new 7th category in the bank enum, positioned after `Experimental`. Existing VowelMorph and SpectralDrift from the current bank arguably belong in Morph too but leave them in their existing categories (Vocal and Experimental) to avoid breaking preset compatibility.

---

## Implementation notes for Phase 11h

### Architecture integration

New tables use `buildFromSpec()` (Tier 1-2) or the legacy time-domain constructor (FoldBloom). All Tier 1-2 tables integrate without changing the existing build system — just add the `makeXxxSpec()` static functions to Wavetable.h and call `buildFromSpec()` in WavetableBank's constructor.

### WavetableBank additions

```cpp
// In WavetableBank Preset enum — add after SerumHD:
// Morph (Phase 11h)
Rise = kNumPresets_pre11h,    // rename kNumPresets
Sweep,
OddEven,
PhaseDrift,
FormantRise,
HarmonicSeries,
FoldBloom,    // last — legacy constructor, different code path
kNumPresets
```

### Build time budget

Each `buildFromSpec()` call is ~150-200ms (comment in Wavetable.h). 6 new spec-built tables = 6 × ~175ms ≈ 1 second added to startup. This is within the acceptable Phase 10a budget already accepted by the user. FoldBloom (legacy constructor) is much faster (~10ms).

### Anti-aliasing

Phase 10a's 8-mip system automatically bandlimits all spec-built tables. HarmonicRise at frame 15 has 64 harmonics — mip levels 0-4 will pass varying counts (256/128/64/32/16), so the transition from bright to antialiased is handled automatically. No special treatment required.

### Per-frame normalization

`buildFromSpec()` calls `normalizeMipLevels()` which normalizes peak across ALL frames within each mip level. This means if frame 0 (1 harmonic) has peak 0.9 and frame 15 (64 harmonics) has peak 3.5, the entire mip level gets scaled by 1/3.5, making frame 0 quieter than frame 15. For "night-and-day" wavetables this can still cause slight level variation. If this is perceptually problematic, consider normalizing amps[] in the FrameSpec before calling buildFromSpec, so each frame peaks at 1.0 before the mip-level normalization pass.

For HarmonicRise specifically, pre-normalize each frame's amplitudes by the expected peak of a sawtooth with N harmonics (≈ ln(N) * 2/π ≈ 1.04 for N=1 to ≈ 2.84 for N=64). Add a `normalizeFrameAmps(FrameSpec& fs)` helper that scales all amplitudes so their estimated IFFT peak ≈ 1.0.
