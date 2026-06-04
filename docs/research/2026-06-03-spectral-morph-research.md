# Spectral Morph DSP Research — Phase 11c

## Sources

- **Vital** `src/synthesis/producers/spectral_morph.h` (entire file, ~480 lines) — all spectral
  transform functions: `passthroughMorph`, `shepardMorph`, `wavetableSkewMorph`, `phaseMorph`,
  `smearMorph`, `lowPassMorph`, `highPassMorph`, `evenOddVocodeMorph`, `harmonicScaleMorph`,
  `inharmonicScaleMorph`, `randomAmplitudeMorph`.
- **Vital** `src/synthesis/producers/synth_oscillator.h` — `SpectralMorph` enum (11 types),
  `setSpectralMorphValues` signature, `kMaxFormantShift = 1.0f`,
  `kMaxHarmonicScale = 4.0f`, `kMaxInharmonicScale = 12.0f`, `kPhaseDisperseScale = 0.05f`,
  `kSkewScale = 16.0f`.
- **Vital** `src/synthesis/producers/synth_oscillator.cpp` lines 736-761, 968-1161 —
  dispatch switch, amount scaling (`setPowerDistortionValues` curves), and per-mode
  `setSpectralMorphValues` overloads.
- **Vital** `src/synthesis/lookups/wavetable.h` — `WavetableData` struct: `frequency_amplitudes`,
  `normalized_frequencies`, `phases` parallel arrays; `kFrequencyBins = 11`.
- **Serum 2 manual** — PDF failed to render (poppler not installed). Serum 2 web sources confirm
  spectral morph, smooth interpolation, and per-harmonic manipulation features, but exact page
  numbers for "Zero Fund. Phase" / "Zero All Phases" could not be verified from this session.
  Add to backlog: install poppler, read pp 38-50.
- **Web** — https://skytracks.io/blog/vital-is-a-spectral-morphing-wavetable-vst-synth/ — Vital
  spectral modes overview and sonic descriptions.
- **Web** — https://davidmvogel.com/docs/Vital/UserGuide/Oscillators-and-Sampler — Vital
  oscillator user guide, spectral morph parameter descriptions.
- **Web** — https://musictech.com/tutorials/weekend-workshop-sound-design-vital/ — sound design
  context for smear, vocode, harmonic scale in practice.

---

## Vital's Spectral Morph Architecture Overview

Vital stores wavetable frequency data in THREE parallel poly_float arrays per frame:
`frequency_amplitudes` (magnitude of each complex bin), `normalized_frequencies` (real/imag
unit-vector components of each bin, interleaved), and `phases` (argument of each bin in radians).
This separation makes amplitude-only operations and phase-only operations cheap: you index one
array without touching the others.

At render time, spectral morphing runs **per-voice per-buffer** via the template function
`setFourierWaveBuffers<spectralMorphFn>()`. Each function receives the wavetable's raw
`WavetableData*`, the integer frame index, a destination poly_float buffer, a
`FourierTransform*`, the scalar `shift` (the processed morph amount), and `last_harmonic`
(a per-pitch harmonic cutoff that Vital computes from `phaseIncrement` to stay below Nyquist).
The function writes its output into the destination buffer in frequency-domain layout, then calls
`transformAndWrapBuffer()` which runs the inverse FFT in-place and wraps the circular buffer
boundaries. The result is a fresh time-domain waveform at the correct bandlimit.

The `kSpectralMorphAmount` input (0–1) is pre-processed via `setSpectralMorphValues` before being
handed to the transform function. Each mode applies its own nonlinear curve to that 0–1 range:
vocode uses `setPowerDistortionValues(…, -kMaxFormantShift)` (a power-law mapping that converts
0–1 to a formant shift factor), harmonic scale uses `setPowerDistortionValues(…, kMaxHarmonicScale=4)`,
inharmonic uses scale 12, smear uses a cubic ease-in `1 - (1-t)^3`, and phase disperse maps
0–1 to a symmetric `-(t*2-1)*0.05` range (centered at 0.5 = no effect).

Cost model: every buffer boundary that requires a different morph amount triggers a new IFFT on a
2048-point frame. Vital amortizes this by caching the waveform buffer until the morph amount or
frame index changes.

---

## Mode 1: VOCODE (evenOddVocodeMorph)

**Vital source:** `spectral_morph.h:307-347`, `synth_oscillator.cpp:1082-1084, 739-740`

### What it does

Shifts all harmonics upward in frequency by a multiplicative factor (`shift`). Harmonic `i` of
the output is sourced from harmonic `i * shift` of the input, interpolated between adjacent bins.
The shift factor (`shift`) includes a frequency-ratio correction accounting for sample-rate
differences between the wavetable's recording sample rate and the playback rate. The result is
that the formant envelope (which harmonic is loudest) stays fixed while the fundamental pitch
changes — exactly a vocoder effect, hence the name. Even and odd harmonics are treated
separately to preserve parity (the `(i + index_start) % 2` alignment step), which is Vital's
"even-odd" variant.

### Math (paraphrased — do not copy)

```pseudocode
// shift > 1.0 pushes harmonics higher (formants appear lower relative to pitch).
// shift < 1.0 compresses harmonics downward.
// shift is derived from: morph_amount * (playback_sample_rate / wavetable_sample_rate)
//                        * wavetable_frequency_ratio,  range approx 0.1..1.0

last_index = min(last_harmonic, frameSize / (2 * shift))

for i in 1..last_index:
    shifted_float = max(1.0, i * shift)
    // align to same parity (even/odd) as i:
    index_start = floor(shifted_float)
    index_start -= (i + index_start) % 2
    t = (shifted_float - index_start) * 0.5  // [0,0.5]

    amp1   = amplitude[index_start]
    amp2   = amplitude[index_start + 2]
    real1  = amp1 * normalized_real[index_start]
    real2  = amp2 * normalized_real[index_start + 2]
    imag1  = amp1 * normalized_imag[index_start]
    imag2  = amp2 * normalized_imag[index_start + 2]

    out_real[i] = shift * lerp(real1, real2, t)
    out_imag[i] = shift * lerp(imag1, imag2, t)

// Harmonics above last_index are zeroed.
```

### Amount trajectory (0 → 100%)

The 0–1 morph value is mapped through `setPowerDistortionValues(…, -kMaxFormantShift = -1.0f)`.
At 0% (shift ≈ wavetable_freq_ratio, typically ≈ 1.0): unprocessed. At 50%: formants shifted
approximately half an octave upward. At 100%: formants shifted by up to 1.0 in the power-law
mapping — the fundamental-harmonic parity clumping creates a strongly alien, buzz-phone timbre.

### Sounds most dramatic on

ProphetSaw and JunoStr — rich, harmonically dense sawwaves have strong formant peaks that slide
convincingly. Sine is silent (only one harmonic, no formant structure to shift).

### Implementation effort: HIGH

Requires a per-harmonic loop with fractional index interpolation, parity alignment, and a
frequency-ratio correction at noteOn. The frequency-ratio correction is the subtle part:
`shift *= (playback_sr / wavetable_sr) * frequency_ratio`.

### Recommended FrameSpec → FrameSpec transform (Terrain)

```cpp
FrameSpec applyVocode(const FrameSpec& in, float shiftFactor) {
    // shiftFactor = amount_processed * (playback_sr / 44100.0f)
    // Typical range: 0.1 (extreme compression) .. 1.0 (passthrough) .. 2.0 (rare expansion)
    FrameSpec out;
    const int last = std::min(in.numHarmonics,
                              (int)(FrameSpec::kMaxHarmonics / std::max(shiftFactor, 0.01f)));
    for (int i = 1; i <= last; ++i) {
        float srcF = std::max(1.0f, (float)i * shiftFactor);
        int   s0   = (int)srcF;                         // floor
        // Parity alignment: source bin must match i's even/odd parity
        if ((i + s0) % 2 != 0) s0 = std::max(1, s0 - 1);
        int   s1   = s0 + 2;                            // next same-parity bin
        float t    = (srcF - s0) * 0.5f;               // [0..0.5]
        if (s0 < 1 || s0 > in.numHarmonics) continue;
        if (s1 > in.numHarmonics) s1 = s0;
        float a0 = in.amplitudes[s0 - 1], a1 = in.amplitudes[s1 - 1];
        float p0 = in.phases[s0 - 1],     p1 = in.phases[s1 - 1];
        out.amplitudes[i - 1] = shiftFactor * (a0 + (a1 - a0) * t);
        out.phases[i - 1]     = p0 + (p1 - p0) * t;
        out.numHarmonics = i;
    }
    return out;
}
// WHERE TO APPLY: noteOn + per-block if SPECTRAL knob is modulated.
// COST: O(numHarmonics) per voice per block if modulated.
```

---

## Mode 2: HARMONIC SCALE (harmonicScaleMorph)

**Vital source:** `spectral_morph.h:349-387`, `synth_oscillator.cpp:1088-1090, 743-744`

### What it does

Expands the spacing between harmonics. Harmonic `i` of the input is mapped to a higher destination
bin `dest = 1 + (i-1) * shift`. Crucially, the expansion is fractional — each source harmonic
energy is split between two adjacent destination bins (a "splat" operation). This preserves
harmonicity at the fundamental (h=1 stays at h=1) while stretching the upper partials. At high
amounts, the upper harmonics land in very high bin positions and the lower harmonics breathe
apart, creating a piano-string-like stretched harmonic series.

### Math (paraphrased)

```pseudocode
// shift = 1.0 → passthrough (each harmonic stays at its bin)
// shift = 4.0 → maximum stretch (from setPowerDistortionValues with kMaxHarmonicScale=4)

out = zeroed array of size kNumHarmonics

for i in 1..min(kNumHarmonics, (last_harmonic - 1)/shift + 1):
    dest_float = max(1.0, 1 + (i - 1) * shift)
    dest       = floor(dest_float)
    t          = dest_float - dest              // fractional part [0,1)
    amp        = amplitude[i]
    real_unit  = normalized_real[i]
    imag_unit  = normalized_imag[i]

    out[dest]     += (1-t) * amp * real_unit    // energy split lower bin
    out[dest+1]   +=   t   * amp * real_unit    // energy split upper bin
    // same for imag
```

### Amount trajectory (0 → 100%)

At 0% (shift = 1.0): passthrough. At 50% (shift ≈ 2.5): harmonics noticeably spread — intervals
between partials grow, timbre becomes bell-like and organ-like simultaneously. At 100% (shift = 4.0):
extreme spreading, only the lowest few harmonics survive under `last_harmonic`; timbre becomes
very sparse and pure.

### Sounds most dramatic on

Square and Triangle — rich in odd harmonics at fixed spacings. Spreading those creates the
"stretched overtone" character (like a pianoforte string). Also dramatic on JunoStr.

### Implementation effort: MEDIUM

Simple splat loop. No parity constraints. Watch out for out-of-bounds writes on the destination
array (cap dest to `numHarmonics - 1`).

### Recommended FrameSpec → FrameSpec transform (Terrain)

```cpp
FrameSpec applyHarmonicScale(const FrameSpec& in, float shift) {
    // shift range: 1.0 (no-op) .. 4.0 (maximum stretch)
    // amount 0->1 maps to shift 1.0->4.0 via power curve
    FrameSpec out;
    const int maxOut = FrameSpec::kMaxHarmonics;
    const int maxIn  = std::min(in.numHarmonics,
                                (int)((maxOut - 1.0f) / std::max(shift - 1.0f, 0.001f)) + 1);
    for (int i = 1; i <= maxIn; ++i) {
        float df = std::max(1.0f, 1.0f + (float)(i - 1) * shift);
        int   d0 = (int)df;
        float t  = df - d0;
        if (d0 >= maxOut) break;
        float a = in.amplitudes[i - 1];
        float p = in.phases[i - 1];
        out.amplitudes[d0 - 1] += (1.0f - t) * a;
        out.phases[d0 - 1]      = p;       // approximate: use source phase for lower bin
        if (d0 < maxOut) {
            out.amplitudes[d0]  += t * a;
            out.phases[d0]       = p;
        }
        out.numHarmonics = std::max(out.numHarmonics, std::min(d0 + 1, maxOut - 1));
    }
    return out;
}
// WHERE TO APPLY: per-block (amount is likely knob-modulated).
// COST: O(numHarmonics) per voice per block. Cheap.
```

---

## Mode 3: INHARMONIC SCALE (inharmonicScaleMorph)

**Vital source:** `spectral_morph.h:389-441`, `synth_oscillator.cpp:1091-1093, 745-746`

### What it does

Like Harmonic Scale but the stretch amount is **non-linear per harmonic** — it grows logarithmically
with harmonic index. The shift for harmonic `i` is:

    shifted_i = 1 + mult^(log2(i) / (kFrequencyBins - 1)) * (i - 1)

where `mult` is the processed morph amount (range ~0.1 to 12). The log-frequency dependence means
low harmonics are barely moved while high harmonics get stretched dramatically — exactly the
inharmonicity pattern of real struck strings (pianos, bells). This creates clangorous, metallic,
bell-like tones from harmonic sources.

### Math (paraphrased)

```pseudocode
// mult = kMaxInharmonicScale^(amount-based power curve), range 1.0..12.0
// Vital precomputes all shifted indices into a scratch buffer (poly_data_start) for SIMD

for i in 1..kNumHarmonics:
    octave = log2(i)                                  // e.g., h=8 → octave=3
    power  = octave / (kFrequencyBins - 1)            // normalized [0,1]
    s      = mult^power                               // stretch factor, more for high h
    shifted_i = max(1, s * (i - 1) + 1)              // h=1 stays fixed

    dest       = floor(shifted_i)
    t          = shifted_i - dest
    // splat (1-t) of harmonic i to dest, t to dest+1 (same as HarmonicScale)
```

### Amount trajectory (0 → 100%)

At 0% (mult ≈ 1.0): passthrough. At 30%: slight metallic shimmer, strings start to sound like
piano body resonance. At 70%: prominent clang, overtones clearly inharmonic. At 100% (mult ≈ 12):
bell/marimba-style radical inharmonicity.

### Sounds most dramatic on

ProphetSaw and Square — abundant harmonic content to distort. DX7-EP-style wavetables with already-
inharmonic character will be amplified further. Less interesting on Triangle (odd-harmonics-only,
gaps become larger gaps).

### Implementation effort: MEDIUM

Same splat loop as Harmonic Scale, plus one `pow()` per harmonic (can precompute at noteOn as a
lookup table of 256 entries). Do not skip the precompute step — 256 pow() calls per block is
acceptable; per-sample is not.

### Recommended FrameSpec → FrameSpec transform (Terrain)

```cpp
FrameSpec applyInharmonicScale(const FrameSpec& in, float mult,
                               float freqBins = 11.0f) {
    // mult range: 1.0 (passthrough) .. 12.0 (maximum inharmonicity)
    // amount 0->1 maps to mult via power curve (see setSpectralMorphValues kInharmonicScale)
    FrameSpec out;
    for (int i = 1; i <= in.numHarmonics; ++i) {
        float octave  = std::log2((float)i);
        float power   = octave / (freqBins - 1.0f);  // 0..1
        float stretch = std::pow(mult, power);
        float shiftF  = std::max(1.0f, stretch * (float)(i - 1) + 1.0f);
        int   d0      = (int)shiftF;
        float t       = shiftF - d0;
        if (d0 >= FrameSpec::kMaxHarmonics) break;
        float a = in.amplitudes[i - 1];
        float p = in.phases[i - 1];
        out.amplitudes[d0 - 1] += (1.0f - t) * a;
        out.phases[d0 - 1]      = p;
        if (d0 < FrameSpec::kMaxHarmonics - 1) {
            out.amplitudes[d0]  += t * a;
            out.phases[d0]       = p;
        }
        out.numHarmonics = std::max(out.numHarmonics, std::min(d0 + 1, FrameSpec::kMaxHarmonics - 1));
    }
    return out;
}
// WHERE TO APPLY: per-block.
// OPTIMIZATION: precompute stretch[i] table once per note (256 pow() at noteOn), reuse per block.
```

---

## Mode 4: SMEAR (smearMorph)

**Vital source:** `spectral_morph.h:217-241`, `synth_oscillator.cpp:1094-1098, 747-748`

### What it does

Replaces each harmonic's amplitude with a running average of itself and all lower harmonics,
weighted by the `smear` parameter. Specifically, the running amplitude is updated as:

    running_amp = lerp(original_amp[i], running_amp[i-1], smear)
    output_amp[i] = running_amp * (i + 0.25) / i     // slight de-emphasis of high harmonics

This blurs the amplitude envelope across harmonics — a sharp spectral peak gets spread into its
neighbours. At 100%, all harmonics approach the same amplitude (white-spectrum noise character).
The `(i + 0.25) / i` growth factor partially counteracts the natural decay of amplitude across
the running average, keeping mid harmonics present.

### Math (paraphrased)

```pseudocode
// smear in [0, 1], cubic-eased from UI amount (1-(1-t)^3 mapping)
// smear = 0 → no effect; smear = 1 → maximum blurring

running = amplitude[0] * (1 - smear)   // DC seed

for i in 1..last_harmonic:
    original = amplitude[i]
    running  = lerp(original, running, smear)          // IIR-style smoothing
    out_amp[i] = running * (i + 0.25) / i              // partial de-emphasis
    out[i]     = out_amp[i] * normalized[i]            // keep original phase direction
```

Phases are NOT changed by smear — only the amplitude envelope is blurred.

### Amount trajectory (0 → 100%)

At 0%: identical to source. At 30%: slight high-frequency blur, subtle noise-like sheen on
transients. At 70%: prominent spectral diffusion — sounds like the waveform was played through a
short dense reverb. At 100%: near-white-spectrum noise character (all harmonics approximately
equal amplitude), particularly striking on periodic sources. The cubic ease means the knob is
nearly linear-feeling from 0–70%, then accelerates sharply to white-noise at 100%.

### Sounds most dramatic on

ProphetSaw and JunoStr — the gradual sawwave rolloff gets replaced by a flat spectrum. Also
interesting on Square for the same reason. On Sine: silent except at very high smear values
where DC seeps through.

### Implementation effort: LOW

Single-pass loop, no frequency-domain indexing, no pow(). The simplest of the non-trivial modes.

### Recommended FrameSpec → FrameSpec transform (Terrain)

```cpp
FrameSpec applySmear(const FrameSpec& in, float smear) {
    // smear in [0,1]; apply cubic ease: smear_applied = 1 - (1-smear)^3
    const float s   = 1.0f - std::pow(1.0f - std::max(0.0f, std::min(1.0f, smear)), 3.0f);
    FrameSpec   out = in;   // copy phases — smear leaves them intact
    float running   = in.amplitudes[0] * (1.0f - s);
    out.amplitudes[0] = running;
    for (int i = 1; i < in.numHarmonics; ++i) {
        running = in.amplitudes[i] * (1.0f - s) + running * s;
        out.amplitudes[i] = running * ((float)(i + 1) + 0.25f) / (float)(i + 1);
        // (i+1 because array is 0-indexed but harmonic is 1-indexed)
    }
    out.numHarmonics = in.numHarmonics;
    return out;
}
// WHERE TO APPLY: per-block (cheap enough).
// NOTE: This is a FrameSpec→FrameSpec transform only — phases unchanged.
```

---

## Mode 5: PHASE DISPERSE (phaseMorph)

**Vital source:** `spectral_morph.h:180-215`, `synth_oscillator.cpp:1104-1107, 755-756`

### What it does

Rotates the phase of each harmonic by an amount proportional to `(h - kCenterMorph)^2 *
phase_shift`, where `kCenterMorph = 24` and `phase_shift` is the processed morph value. This
is a **quadratic phase dispersion** centered on harmonic 24. Below harmonic 24, phases are
rotated in one direction; above, the other. At low amounts this produces subtle comb-filter-like
coloring without changing the spectrum. At high amounts it progressively scrambles phase
relationships, producing a noise-like (but spectrally identical) signal.

The `phase_shift` value is mapped from the 0–1 UI amount via:
    `phase_shift = -(amount * 2 - 1) * kPhaseDisperseScale = -(amount * 2 - 1) * 0.05`
which means amount = 0.5 is the NEUTRAL point (no rotation); the knob sweeps from one polarity
to the other through neutral. This is unusual — the knob is NOT zero-at-left.

### Math (paraphrased)

```pseudocode
// phase_shift = -(amount * 2 - 1) * 0.05   (signed, centered at 0 when amount=0.5)
// offset = -(kCenterMorph - 1)^2 * phase_shift  (normalization constant)

for each complex bin at harmonic h (real+imag pair):
    delta = (h - kCenterMorph)^2 * phase_shift + offset
    // rotate original complex value by delta radians
    out_real[h] = amp[h] * (cos(delta) * normalized_real[h] - sin(delta) * normalized_imag[h])
    out_imag[h] = amp[h] * (cos(delta) * normalized_real[h] + sin(delta) * normalized_imag[h])
    // exact form: Vital uses sin1(mod(delta * 0.5/pi + phase_offset))
    // for Terrain purposes a standard complex rotation is cleaner
```

Amplitudes are NOT changed — only phases rotate. The power spectrum is preserved exactly.

### Amount trajectory (0 → 100%)

Amount = 50% is the neutral point (zero rotation). Moving toward 0% or 100% increasingly
scrambles phase. The effect is subtle at low modulation depths — a slight "swirling" or
"widening" quality — and becomes increasingly noise-like at extremes. Unlike Smear which destroys
spectral character, Phase Disperse preserves the harmonic structure entirely while destroying
phase coherence. The result is a spectrally "correct" sound that has lost its periodic
waveform character.

### Sounds most dramatic on

OBXSaw and JunoStr which already have some phase scatter — Phase Disperse extends that scatter
further and more controllably. Dramatic on Square: the harsh buzz of perfectly-aligned odd
harmonics dissolves into a diffuse chord-like texture.

### Implementation effort: MEDIUM

Requires sin/cos per harmonic (or a fast sin approximation). Can be reduced to a table lookup
of `sin(k*h^2)` for fixed k. Note the unusual amount mapping (50% = neutral).

### Recommended FrameSpec → FrameSpec transform (Terrain)

```cpp
FrameSpec applyPhaseDisperse(const FrameSpec& in, float amount) {
    // amount in [0,1]; 0.5 = neutral. Maps to signed phase_shift:
    constexpr float kPhaseDisperseScale = 0.05f;
    constexpr float kCenter = 24.0f;
    const float phaseShift = -(amount * 2.0f - 1.0f) * kPhaseDisperseScale;
    const float offset     = -(kCenter - 1.0f) * (kCenter - 1.0f) * phaseShift;
    FrameSpec out = in;  // copy amplitudes — untouched
    for (int i = 0; i < in.numHarmonics; ++i) {
        const float h     = (float)(i + 1);  // 1-indexed harmonic
        const float delta = (h - kCenter) * (h - kCenter) * phaseShift + offset;
        // Rotate phase by delta radians:
        out.phases[i] = in.phases[i] + delta;
        // Normalize to [-pi, pi] optionally — buildFromSpec handles arbitrary phases
    }
    out.numHarmonics = in.numHarmonics;
    return out;
}
// WHERE TO APPLY: per-block.
// NOTE: amplitudes unchanged. This is cheapest mode — just add a computed offset per harmonic.
// UI KNOB BEHAVIOR: must document that 50% = center/neutral, not 0%.
```

---

## Mode 6: LOW PASS (lowPassMorph)

**Vital source:** `spectral_morph.h:243-271`, `synth_oscillator.cpp:1146-1148, 751-752`

### What it does

Zeros all harmonics above a cutoff frequency determined by the morph amount. The cutoff is
exponential in harmonic index:

    cutoff_harmonic = 2^((kFrequencyBins - 1) * cutoff_t) + 1

where `cutoff_t` is the processed amount [0,1] and `kFrequencyBins = 11`. This gives a range
of harmonic 2 (at 0%) to harmonic 1025 (at 100%, effectively passthrough). Harmonics at the
boundary are scaled fractionally for a gentle edge. The tone at 0% is nearly a sine wave.

Vital does NOT apply a roll-off slope — it's a hard brick-wall cutoff with a one-bin fractional
edge. This is intentional: a soft slope would interact poorly with the wavetable's own
spectral character.

### Amount trajectory (0 → 100%)

At 0%: pure sine (only fundamental). At 25%: harmonics up to ~4. At 50%: harmonics up to ~32
(sounds like a bright-ish sound with no air). At 75%: harmonics up to ~128. At 100%: full
spectrum. The exponential mapping means most of the character change happens in the 0–60% range.

### Sounds most dramatic on

Square and ProphetSaw — removing harmonics from these is exactly the LP filter effect but
applied at the wavetable level rather than as a running filter. Useful for pre-filtering
wavetables to avoid aliasing before pitch-range is determined (though mip-map already handles
that in Terrain's architecture).

### Implementation effort: LOW

Just zero harmonics above the cutoff index. Simplest possible transform.

### Recommended FrameSpec → FrameSpec transform (Terrain)

```cpp
FrameSpec applyLowPass(const FrameSpec& in, float cutoff_t) {
    // cutoff_t in [0,1]; 0 = sine only, 1 = passthrough
    constexpr int kFreqBins = 11;
    const float cutoff = std::pow(2.0f, (float)(kFreqBins - 1) * cutoff_t) + 1.0f;
    FrameSpec out = in;
    const int last = std::min(in.numHarmonics, (int)cutoff);
    // Apply fractional fade at cutoff boundary
    if (last < in.numHarmonics) {
        const float t = cutoff - (float)last;  // [0,1) fractional part
        out.amplitudes[last] *= t;             // fade the boundary harmonic
        for (int i = last + 1; i < in.numHarmonics; ++i)
            out.amplitudes[i] = 0.0f;
    }
    out.numHarmonics = std::min(in.numHarmonics, last + 1);
    return out;
}
// WHERE TO APPLY: per-block.
// ALTERNATIVE: this can also be applied time-domain post-IFFT as a simple biquad LP —
//   saves re-IFFT if only LP/HP are used. Per-harmonic is more correct for wavetables.
```

---

## Mode 7 (bonus): HIGH PASS (highPassMorph)

Included for completeness — symmetric to Low Pass.

**Vital source:** `spectral_morph.h:273-305`

```cpp
FrameSpec applyHighPass(const FrameSpec& in, float cutoff_t) {
    constexpr int kFreqBins = 11;
    const float cutoff = std::pow(2.0f, (float)(kFreqBins - 1) * cutoff_t)
                         * ((float)FrameSpec::kMaxHarmonics + 1.0f) / (float)FrameSpec::kMaxHarmonics;
    FrameSpec out = in;
    const int start = (int)cutoff;
    const float t   = cutoff - (float)start;  // fractional leading edge
    for (int i = 0; i < std::min(start, in.numHarmonics); ++i)
        out.amplitudes[i] = 0.0f;
    if (start < in.numHarmonics)
        out.amplitudes[start] *= (1.0f - t);  // fade the boundary harmonic
    // numHarmonics unchanged
    return out;
}
// At 0%: passthrough. At 100%: almost all harmonics removed (only highest remain).
// Sounds most useful at moderate amounts to remove fundamental warmth for "airy" character.
```

---

## Architecture Recommendation: Where to Apply Spectral DSP in Terrain

### The Core Problem

Terrain's `buildFromSpec()` takes a `WavetableSpec` and pre-renders 8 mip levels × 16 frames ×
2048 samples. This is the expensive batch-build step done at startup (~150–200ms per table). The
runtime `lookup(mipLevel, framePos, phase)` is cheap bilinear interpolation from the pre-rendered
table — no spectral math at runtime.

A spectral morph that modifies `WavetableSpec` before `buildFromSpec()` would be correct but
requires ~200ms rebuild time at noteOn — unacceptable.

### Recommended Architecture: Per-Voice Modified FrameSpec + Per-Block IFFT

For Phase 11c, apply spectral transforms **per voice, per block** to the live FrameSpec at the
currently-active `framePos`, then synthesize directly from the modified FrameSpec into the voice
render buffer — bypassing the pre-rendered mip table entirely for that voice.

This is conceptually identical to what Vital does (it always renders from frequency domain at
render time), adapted to Terrain's architecture:

```
Per block, per voice:
1. Read current framePos (from WT_POS + WARP).
2. Bilinearly interpolate FrameSpec between the two adjacent frames:
      frame0 = spec.frames[floor(framePos * 15)]
      frame1 = spec.frames[ceil(framePos * 15)]
      liveSpec = lerp(frame0, frame1, frameFrac)  // per-harmonic amplitude+phase lerp
3. Apply SPECTRAL transform to liveSpec → modifiedSpec.
4. Additive synthesis from modifiedSpec at current voice phase:
      sample = sum over h: modifiedSpec.amplitudes[h] * sin(2*pi*h*phase + modifiedSpec.phases[h])
   where phase = voicePhase_ (accumulates at phaseInc per sample).
5. Clamp to mipMaxHarmonics[mipLevel] to bandlimit (same as buildFromSpec).
```

This is **additive synthesis at render time** — exactly what `buildFromSpec` does offline but now
done live with the modified spectrum. CPU cost:

- **Per sample:** sum over `min(numHarmonics, mipMaxHarmonics[mipLevel])` sinusoids.
- At C4 (mip level ~2), that's 64 harmonics per sample.
- At C7 (mip level ~5), that's 8 harmonics per sample.
- At 48kHz, 256-sample buffer, 16 voices, C4: 64 × 256 × 16 = 262,144 sin() calls per block.

**This is expensive.** Current Phase 8b/10a architecture already uses the pre-rendered mip table
for exactly this reason. Two options:

#### Option A: IFFT-Based Per-Block Render (Vital's approach)

Use a small FFT (2048-point or 1024-point) per voice per block:
1. Build the complex frequency buffer from modifiedSpec (256 complex bins, zero-pad to 1024).
2. Run inverse FFT (JUCE's `dsp::FFT` or `kiss_fft`) → 1024-sample time-domain frame.
3. Extract the sample at `voicePhase_ * 1024` (single index into the frame).

Cost: 1 × 1024-FFT per voice per block = ~10k multiply-adds per voice. For 16 voices = 160k ops.
This is roughly 2–3% of a 48kHz budget on a modern CPU. Acceptable.

**This is the recommended implementation path.**

#### Option B: Precompute at NoteOn, Cache Rendered Frame

At noteOn, apply spectral transform to the FrameSpec at the current framePos, call
`buildFromSpec()` on a voice-local temporary Wavetable (single-mip, single-frame), and use the
resulting time-domain buffer for the duration of the note. If SPECTRAL knob is modulated,
update every N blocks (e.g., every 8 blocks ≈ 5ms at 48kHz) rather than every block.

Cost: one `buildFromSpec()` at noteOn (~0.5ms for a single frame, vs 200ms for all 16×8).
Modulation latency: 5ms at 8-block update rate.

**Recommended for initial Phase 11c (simpler to implement, lower CPU baseline).**

#### Option C: Amplitude-Only Modes via Time-Domain Filtering (LP/HP/Smear)

For modes that only modify amplitudes (LP, HP, Smear), the FrameSpec transform can be applied
to the amplitude envelope and the result baked into a one-time mip rebuild at noteOn. No per-block
cost at all for static amounts. For modulated amounts, treat as Option B.

### Recommended Phase 11c Implementation Path

1. **NoteOn:** For the voice's current `framePos`, linearly interpolate between adjacent
   `FrameSpec` frames → `liveSpec`. Apply the selected SPECTRAL mode transform to get
   `morphedSpec`. Call `buildSingleFrame(morphedSpec, mipMaxHarmonics[mipLevel])` to get a
   2048-sample time-domain buffer. Cache this as `voice.spectralBuffer_`.
2. **Render loop:** Use `voice.spectralBuffer_` exactly as the current mip table is used —
   bilinear phase lookup in the 2048-sample buffer.
3. **Modulation update:** If `SPECTRAL` knob is modulated, rebuild `spectralBuffer_` every 8
   blocks (dirty flag pattern). Skip rebuild if `spectralAmount` unchanged.
4. **WT_POS change:** Rebuild when framePos crosses a frame boundary.

This design reuses the existing phase-accumulator and lookup infrastructure entirely. The only
new code is:
- `FrameSpec spectralTransform(const FrameSpec&, SpectralMode, float amount)` dispatch function.
- `std::vector<float> buildSingleFrame(const FrameSpec&, int maxHarmonics)` — same as
  `buildFromSpec()` inner loop for a single frame.
- `float spectralBuffer_[kFrameSize]` per voice (or shared for voices on same note).

---

## CPU Cost Estimates

Modes ranked from cheapest to most expensive per block (16 voices, 48kHz, 256-sample buffer):

| Rank | Mode             | Per-block cost           | Notes |
|------|------------------|--------------------------|-------|
| 1    | Low Pass         | ~256 compares + zeroing  | Trivial — just truncate harmonic array |
| 2    | High Pass        | ~256 compares + zeroing  | Same as LP |
| 3    | Phase Disperse   | ~256 additions + trig    | One sin/cos per harmonic for phase rotation |
| 4    | Smear            | ~512 multiplies          | Single-pass IIR, no trig |
| 5    | Harmonic Scale   | ~512 multiplies + interp | Splat loop, no trig |
| 6    | Inharmonic Scale | ~512 mul + 256 pow()     | One pow() per harmonic; precompute at noteOn |
| 7    | Vocode           | ~512 mul + interp        | Parity alignment adds complexity |

All modes are O(numHarmonics) = O(256) per voice per block, which is negligible on its own.
The expensive part is the **NoteOn rebuild** (Option A/B above) which involves an IFFT or
additive render. Under Option B (noteOn cache + 8-block dirty refresh), the per-block steady-
state cost of spectral morph is near zero — only the rebuild events cost anything significant.

At C4 with 16 voices and Option B (8-block refresh rate):
- Rebuild cost per voice: ~256 sin() × 2048 samples ≈ 500k sin() calls.
- At 16 voices: 8M sin() calls per rebuild event, amortized over 8 blocks = 1M sin()/block.
- At 48kHz × 256 samples per block = ~78M CPU cycles at 1 cycle/sin (SIMD estimate): feasible.

**Recommendation:** Use `std::sin` replacement via fast sine approximation (polynomial), or
`pffft`/JUCE `dsp::FFT` for the rebuild path to reduce that cost to ~10k ops per voice per
rebuild.

---

## The 6 Modes Recommended for Phase 11c

In priority order for implementation:

| # | Mode             | Rationale | CPU rank |
|---|------------------|-----------|----------|
| 1 | **SMEAR**        | Cheapest non-trivial mode. Immediately dramatic on saws. LP/HP comparison: actually more musically interesting. | 4 |
| 2 | **LOW PASS**     | Extremely cheap. Great for making wavetables sound filtered/thin. Natural pair with FILTER 1. | 1 |
| 3 | **PHASE DISPERSE** | Cheap. Only mode that changes timbre without touching spectrum — unique effect unavailable in any Terrain FX chain. | 3 |
| 4 | **HARMONIC SCALE** | Medium cost. Bell-like stretching is a headline effect. Pairs well with JunoStr and Square. | 5 |
| 5 | **INHARMONIC SCALE** | Medium cost. Metallic/bell inharmonicity is the "most Serum 2" effect. Critical for Phase 11c's headline promise. | 6 |
| 6 | **VOCODE**       | High complexity but the Vital signature mode. Most dramatic on sawtooth wavetables. Should be the headline mode if only one ships. | 7 |

Modes NOT recommended for Phase 11c (defer to 11d or later):
- **Shepard Tone** — requires a multi-frame cross-wavetable morph; architectural change, not a FrameSpec transform.
- **Skew (Spectral Time Skew)** — requires multi-frame look-ahead; complex and requires Wavetable-level access, not just FrameSpec.
- **Random Amplitudes** — fun but random seed management per-voice adds state complexity; Phase 11d.
- **High Pass** — useful but LP covers the concept; add as a variant toggle (LP ↔ HP) in Phase 11c with no extra implementation cost.

---

## Constraint: Only 7 of 24 Wavetables Have FrameSpec

The following 7 wavetables use `WavetableSpec` / `FrameSpec` format and will respond to spectral
DSP from day one of Phase 11c:

| WT Name     | Factory method              | Best spectral modes |
|-------------|-----------------------------|---------------------|
| Sine        | `makeSineSpec()`            | HP, Phase Disperse (all others are silent — only 1 harmonic) |
| Triangle    | `makeTriangleSpec()`        | Harmonic Scale (spreads odd harmonics beautifully), Smear |
| Square      | `makeSquareSpec()`          | Vocode, Harmonic Scale, Inharmonic Scale, Phase Disperse |
| Pulse       | `makePulseSpec()`           | Vocode, LP (narrows duty cycle character) |
| ProphetSaw  | `makeProphetSawSpec()`      | All 6 modes — richest harmonic content |
| OBXSaw      | `makeOBXSawSpec()`          | Vocode, Phase Disperse (phase scatter already built in) |
| JunoStr     | `makeJunoStrSpec()`         | Harmonic Scale, Smear, Vocode |

The remaining 17 legacy wavetables (JupiterPWM, MoogSqr, CS80Brass, PPGWave, DX7EP, D50Bell,
M1Piano, ChoirAtoO, Whisper, VowelMorph, BowedMetal, GlassHarmonics, Railroad, Dustbowl,
StaticEvolve, SpectralDrift, SerumHD) use time-domain construction and have no `FrameSpec`.
The SPECTRAL knob will have **no audible effect on these 17 tables** until Phase 10c migrates
them to FrameSpec format.

**Known limitation — document in UI:** display a subtle indicator (e.g., greyed SPECTRAL
label, or a tooltip) when the selected wavetable is a legacy table and SPECTRAL is engaged.
Alternatively, if FrameSpec migration of the most common legacy tables (ProphetSaw is already
done; MoogSqr, PPGWave, CS80Brass are highest priority) is fast, add them to the 11c scope.

Phase 10c (wavetable migration to FrameSpec) should be scheduled before Phase 11c ships for
maximum impact, or immediately after as a "free the remaining 17" patch.
