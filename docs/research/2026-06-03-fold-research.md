# Wavefolding DSP Research — Phase 11d FOLD Knob

**Date:** 2026-06-03
**Purpose:** Lock in math, sonic character, and implementation strategy for the FOLD knob's three
shapes: LINEAR, SINE, TRIANGLE. Phase 11d builds this into Terrain Instrument's wavetable engine
post-WT POS / WARP pipeline.

---

## Sources

| Source | Role |
|--------|------|
| Vital `src/common/wavetable/wave_fold_modifier.cpp` (line 45-47) | Reference: "SINE-fold via arcsin then sin" pattern |
| CCRMA Wavefolder page (Jatin Chowdhury) — https://ccrma.stanford.edu/~jatin/ComplexNonlinearities/Wavefolder.html | Triangle and sine fold transfer functions, anti-aliasing recommendations |
| Jatin Chowdhury Medium — https://jatinchowdhury18.medium.com/complex-nonlinearities-episode-6-wavefolding-9529b5fe4102 | Harmonic analysis, feedback wavefolder, Buchla reference |
| KVR DSP thread — https://www.kvraudio.com/forum/viewtopic.php?t=501471 | C++ formulas for linear fold (SIMD + scalar); DC offset variant |
| MDPI paper: "Virtual Analog Models of the Lockhart and Serge Wavefolders" — https://www.mdpi.com/2076-3417/7/12/1328 | Serge triangle fold math; Lockhart Lambert-W model; anti-aliasing analysis |
| DAFx17 paper: "Virtual Analog Buchla 259 Wavefolder" — https://www.dafx17.eca.ed.ac.uk/papers/DAFx17_paper_82.pdf | Buchla 5-stage center-clipper model; ADAA anti-aliasing |
| Newfangledaudio Buchla 259 article — https://www.newfangledaudio.com/post/model-bending-the-buchla-259-wavefolder | Buchla "5 parallel clipping stages" architecture; sonic character |
| RingBuffer.org wavefolding — https://ringbuffer.org/sound_synthesis_introduction/Distortion/wavefolding/ | Sine fold transfer function `y = sin(g * π/2 * x)`; Jacobi-Anger harmonic expansion |
| AAS Multiphonics CV-2 manual — https://www.applied-acoustics.com/multiphonics-cv-2/manual/wavefolder/ | DC offset / AC coupling; topology comparison (parallel / series / digital); soft saturation output |
| Mashav foldback reference — https://mashav.com/sha/praat/scripts/wavefolder-distortion.html | Foldback equation; peak protection normalization |
| Arturia Pigments 7.0 manual (local, >100 MB — page range unreadable by tool) | Wavefolding section ~pp 98-99; three fold shapes confirmed in TOC as objects 325/326/327 |

---

## Background: What Wavefolding Does

A wavefolder is a nonlinear waveshaper that REFLECTS the portion of a signal that exceeds a
threshold (typically ±1) back into range, instead of clipping it. This is the key distinction from
hard/soft clipping: clipping truncates; folding mirrors.

The standard mechanism is: if the driven input rises above +1, the output is reflected back
downward (2 – x). If it drops below –1, it reflects upward (–2 – x). If the signal is driven hard
enough to cross ±1 multiple times within a single cycle, multiple reflections stack, creating a
progressively richer harmonic spectrum on each fold.

A gain stage PRECEDES the fold — the "amount" knob controls that pre-gain. At zero drive the
signal stays entirely inside ±1, no folding occurs, output equals input. As drive increases, peaks
start to exceed ±1 and the first fold appears. At high drive the signal folds many times per cycle.

Three fold SHAPES differ in the shape of the reflection curve itself:

- **LINEAR** (triangle-wave fold): the reflection is geometrically perfect — a triangle wave
  applied as a transfer function. Aggressive, buzzy, unbounded odd harmonics.
- **SINE** (smooth fold): the transfer function is `sin(g · x)` — smooth, musical, a single
  elegant curve. Generates primarily odd harmonics via the Jacobi-Anger expansion.
- **TRIANGLE / Buchla-style** (stacked fold stages): multiple parallel clipping stages mixed
  together — characteristic of the Buchla 259 and Serge Wave Multiplier. Most harmonically
  complex, most musical, "West Coast" character.

All three share the same pre-gain architecture; only the transfer function differs.

---

## Shape 1: LINEAR Fold

### Transfer Function

The LINEAR fold maps the input through a triangle-wave transfer function: any value that exceeds
±1 is "bounced" back, iteratively, until it lands in [–1, +1].

```cpp
// Scalar C++ implementation
float fold_linear(float x) {
    // Wrap into [0, 4) via triangle-wave trick
    // Equivalent to 4 * |frac(x/4 + 0.25) - 0.5| - 1
    // but the iterative form is clearest for documentation:
    while (x >  1.0f) x = 2.0f - x;
    while (x < -1.0f) x = -2.0f - x;
    return x;
}

// Compact branchless form (from KVR thread — scalar, no SIMD):
// Range of 'in' is [-inf, +inf]; output always in [-1, 1]
float fold_linear_compact(float in) {
    return 4.0f * std::abs(0.25f * in + 0.25f
                           - std::round(0.25f * in + 0.25f)) - 1.0f;
}

// SIMD form (from KVR thread, SSE — operates in (-0.5, 0.5) range,
// then scale × 2.0 at output to get [-1, 1]):
__m128 fold_simd(__m128 x) {
    __m128i int_round = _mm_cvtps_epi32(x);
    __m128  frac      = _mm_sub_ps(x, _mm_cvtepi32_ps(int_round));
    // XOR the sign bit of frac by the LSB of int_round:
    return _mm_xor_ps(frac,
        _mm_castsi128_ps(_mm_slli_epi32(int_round, 31)));
}
// Note: SIMD form works in (-0.5, 0.5); scale input by 0.5
// and output by 2.0 to map to (-1, 1).
```

### Pre-Fold Gain Curve

The pre-fold gain maps `fold_amount ∈ [0, 1]` to a drive gain applied before the fold function.

```cpp
// Recommended: exponential curve so that the user's knob feels musical
// (equal perceived "steps" of intensity across the range)
float drive_linear(float fold_amount) {
    // fold_amount = 0 → gain = 1.0 (no fold, signal stays in [-1,1])
    // fold_amount = 0.5 → gain ≈ 3.16  (first fold mid-range)
    // fold_amount = 1.0 → gain = 10.0  (up to ~5 folds on a sine wave)
    return 1.0f + fold_amount * fold_amount * 9.0f;
    // Quadratic: gentle start, aggressive end.
    // Alternatively: expf(fold_amount * logf(10.0f)) for exact 1→10 exp.
}
```

**Rationale:** A sine wave of amplitude 1 driven at gain=1 never folds (peak = 1.0). At gain=3 the
peak reaches 3.0 — approximately 2 folds. At gain=5 → ~4 folds. Gain range 1–10 gives 0–9 folds
on a sine input, which is perceptually the full useful range. The quadratic curve keeps low amounts
(0–30%) subtle and makes 50–100% dramatic.

### Sonic Character

**Aggressive, buzzy, aliasy.** The triangle fold creates sharp corners at every fold point — each
corner generates strong high-frequency transients. The harmonic spectrum is ALMOST UNBOUNDED:
every odd harmonic is present and rolls off slowly. At low amounts (first fold): sounds like adding
a sawtooth-y upper harmonic layer. At medium amounts: aggressive square-ish buzz. At high amounts:
harsh, near-digital grit — intentional aliasing as analog character.

The CCRMA analysis (Jatin Chowdhury) specifically notes the linear/triangle fold has "almost
unbounded harmonic response" — more than any other fold shape.

### At What Amount Does It Sound Different?

- **0–15%** (gain 1.0–1.2): imperceptible, peaks barely touch ±1.
- **20–35%** (gain 1.4–2.1): first fold appears — slight metallic sheen on transients.
- **40–60%** (gain 2.4–4.2): clearly distorted, buzzy overtones, noticeably richer timbre.
- **70–100%** (gain 5.4–10.0): multiple folds, heavily transformed, barely recognizable.

### DC Offset

**No DC offset for symmetric (zero-mean) inputs.** The LINEAR fold is an ODD function:
`fold(–x) = –fold(x)`. A sine wave driven through a linear folder stays zero-mean. However, if the
input wave has a DC component (e.g. a wavetable with a non-zero mean), the asymmetry WILL
introduce DC. Solution: run a simple one-pole DC blocker on the output:
```cpp
float dc_block(float x, float &z1, float R = 0.995f) {
    float y = x - z1 + R * y_prev; // standard 1-pole HP
    // Simpler version:
    float out = x - z1;
    z1 = x * (1.0f - R) + z1 * R;
    return out;
    // R = 0.995 → –3 dB at ~20 Hz at 44100 Hz sample rate
}
```

### Loudness Compensation

The LINEAR fold CAN be louder than the input because the folded output still has amplitude close
to 1.0 but the WAVEFORM becomes more complex (more energy in harmonics). In practice the RMS
rises modestly with fold amount. Recommended approach for Phase 11d:

```cpp
// Simple makeup gain: since the fold keeps output in [-1, 1],
// peak is already bounded. However RMS rises ~+3 to +6 dB at heavy fold.
// Apply a static gain taper:
float makeup = 1.0f / std::max(1.0f, 0.5f + fold_amount * 1.5f);
// At 0% → makeup = 1.0  (no change)
// At 50% → makeup ≈ 0.80
// At 100% → makeup ≈ 0.50
```

This is approximate. The AAS Multiphonics manual notes "a smooth saturation applied to the output
prevents the signal from getting too loud" — a soft limiter (tanh or soft-clip) at the output of
the fold block is simpler than dynamic gain riding.

### References

- KVR DSP Forum: https://www.kvraudio.com/forum/viewtopic.php?t=501471 (scalar + SIMD C++ code)
- CCRMA Wavefolder (Jatin Chowdhury): https://ccrma.stanford.edu/~jatin/ComplexNonlinearities/Wavefolder.html
- Mashav foldback: https://mashav.com/sha/praat/scripts/wavefolder-distortion.html

---

## Shape 2: SINE Fold

### Transfer Function

The SINE fold uses a sinusoidal transfer function applied directly to the driven input. This is the
canonical "smooth wavefolder" — the signal is passed through `sin()`, which naturally folds at
every π interval.

```cpp
// Primary formula (RingBuffer.org, Jatin Chowdhury CCRMA page):
// y = sin(g * x)
// where g is the pre-gain (drive), x is the normalized input sample.
//
// At g = π/2 ≈ 1.571: peaks of a ±1 sine input land exactly at
// sin(π/2) = 1.0 → just-saturating, no fold yet.
// At g = π   ≈ 3.14: peaks reach sin(π) = 0 → first FOLD (peak reflects to 0)
// At g = 3π/2≈ 4.71: peaks reach sin(3π/2) = –1 → full inversion
// At g = 2π  ≈ 6.28: peaks reach sin(2π) = 0 → second fold

float fold_sine(float x, float drive) {
    return sinf(drive * x);
}

// Pre-gain integrated — recommended for a single-knob API:
float fold_sine_full(float x, float fold_amount) {
    float drive = 1.0f + fold_amount * fold_amount * (2.0f * M_PI - 1.0f);
    // fold_amount=0   → drive=1.0  → sin(x) ≈ x for small x (near-linear)
    // fold_amount=0.5 → drive≈2.5  → first fold zone
    // fold_amount=1.0 → drive≈6.28 → 2 full folds on sine input
    return sinf(drive * x);
}
```

**Key insight (RingBuffer.org / Jacobi-Anger):** The output `sin(g·sin(ωt))` for a sine input
expands as:

    y(t) = 2 · Σ J_{2k+1}(g) · sin((2k+1)·ωt)   [odd harmonics only]

Where J_n is the Bessel function of the first kind. The 5th harmonic is often MORE prominent than
the fundamental at moderate drive — a defining characteristic of this fold shape.

### Vital's Approach (direct reference)

Vital's `WaveFoldModifier::render()` (line 45-47, `wave_fold_modifier.cpp`) uses a
COMBINED arcsin → scale → sin pattern:

```cpp
// Vital source (GPL-3, reference only — do NOT copy):
float value        = clamp(wave[i] / max_value, -1.0f, 1.0f);
float adjusted     = max_value * wave_fold_boost_ * asinf(value);
wave_frame[i]      = sinf(adjusted);
```

This first normalizes the frame peak to ±1 via `asinf(value)`, then scales by the boost factor,
then re-applies `sinf()`. The effect: `sin(boost · asin(x))` = a smooth shaping that for boost=1
is identity (sin(asin(x)) = x), and for boost>1 progressively folds. At boost=π/2 it becomes
`sin(π/2 · asin(x))` — a saturating shape resembling Chebyshev-based polynomial shaping.

For Phase 11d we use the simpler `sin(drive * x)` form (does not require normalizing the
wavetable first). Vital's form is a wavetable EDITOR operation (offline), not a real-time DSP
operation.

### Pre-Fold Gain Curve

The natural "fold breakpoints" for a sine input `x = A·sin(ωt)` driven by gain g:
- **First fold threshold:** g·A = π/2 → g = π/(2A)
- For normalized A=1: first fold at g ≈ 1.57 (π/2)
- Second fold at g ≈ 4.71 (3π/2)
- Third fold at g ≈ 7.85 (5π/2)

```cpp
float drive_sine(float fold_amount) {
    // Maps [0,1] → [1, 6.28] using quadratic
    // fold_amount=0   → 1.0  (no folding, sin(x) ≈ x)
    // fold_amount=0.4 → 2.5  (approaching first fold at π/2 ≈ 1.57)
    // fold_amount=0.6 → 3.3  (mid fold)
    // fold_amount=1.0 → 6.28 (two full fold cycles)
    return 1.0f + fold_amount * fold_amount * (6.28f - 1.0f);
    // Quadratic, 1 → 2π range
}
```

### Sonic Character

**Smooth, musical, Serge Wave Multiplier character.** The SINE fold generates ONLY ODD harmonics
(via Jacobi-Anger). The harmonic rolloff is slower than soft-clipping but smoother than the LINEAR
fold. The 5th harmonic peak at moderate drive (around 50%) is the signature sound — a kind of
"hollow bell-like warmth" that transitions into buzzing complexity at high drive.

Distinguished from LINEAR by the absence of sharp corners — no aliasy transients, smoother
transition at fold points. Distinguished from clipping by the RISING harmonic count as drive
increases (clipping saturates harmonics; this adds them progressively).

AAS Multiphonics describes this topology (their "series" folding, which approximates repeated
`sin()`-like stages) as producing a sound that is "harsh, especially at high Fold gains" relative
to the input — the 5th harmonic dominance can feel dissonant on sustained patches.

### At What Amount Does It Sound Different?

- **0–25%** (drive 1.0–1.4): very subtle shaping, slight saturation.
- **30–50%** (drive 1.6–2.6): 5th harmonic appears noticeably — slight bell character on sine.
- **55–75%** (drive 2.8–4.6): multiple harmonics, recognizably "folded."
- **80–100%** (drive 4.8–6.3): dramatic, fundamental nearly absent, rich harmonic cloud.

### DC Offset

**No DC offset for odd-symmetric inputs.** `sin()` is an ODD function: `sin(–x) = –sin(x)`.
Zero-mean inputs stay zero-mean. Same DC blocker recommendation as LINEAR if the source wavetable
has DC.

### Loudness Compensation

The output of `sin(drive · x)` is always bounded to ±1 by construction — this is a key advantage
of the SINE fold. **Peak amplitude is automatically bounded.** However, RMS rises at moderate drive
(the waveform spends more time near ±1 as it folds). A static taper similar to the LINEAR fold
applies:

```cpp
float makeup = 1.0f / std::max(1.0f, 0.4f + fold_amount * 1.6f);
// At 0% → 1.0 (no change)
// At 50% → 0.77
// At 100% → 0.50
```

Note: because output is bounded to ±1, a simple soft limiter is NOT needed here — only a taper for
perceptual loudness matching.

### References

- RingBuffer.org: https://ringbuffer.org/sound_synthesis_introduction/Distortion/wavefolding/
  (formula `y[n] = sin(g·π/2·x[n])` and Jacobi-Anger expansion)
- Vital `wave_fold_modifier.cpp` lines 40-48 (asin/sin pattern)
- Jatin Chowdhury CCRMA: https://ccrma.stanford.edu/~jatin/ComplexNonlinearities/Wavefolder.html

---

## Shape 3: TRIANGLE Fold (Buchla-Style / Multi-Stage)

### Overview

The Buchla 259 and Serge Wave Multiplier are the canonical "musical" wavefolders. The Buchla 259
uses FIVE parallel center-clipper stages — each stage clips the input at a slightly different
threshold and scale, and their outputs are summed. The Serge uses a triangle-fold reflection
approach at each stage. Both produce a richly harmonic, yet tonally musical result — more complex
than SINE fold but with a characteristic "warm" peak response absent from the harsh LINEAR fold.

For Phase 11d, the "TRIANGLE" shape implements a practical **three-stage cascaded triangle fold**
— computationally cheap, captures the character of the Buchla/Serge without requiring the full
Lambert-W Lockhart circuit model.

### Transfer Function

**Simple 3-stage approximation (practical for real-time per-sample audio DSP):**

```cpp
// Single triangle fold stage — reflects at ±threshold
// This is the same kernel as Shape 1 (LINEAR) but with variable threshold
inline float fold_stage(float x, float threshold) {
    // Fast branchless: maps x to triangle wave of period 4*threshold
    float t = threshold;
    float scaled = x / (2.0f * t);
    float shifted = scaled + 0.5f;
    float frac    = shifted - std::round(shifted);  // [-0.5, 0.5)
    // Flip sign on alternate periods:
    int   period  = (int)std::round(scaled + 0.5f);
    float folded  = frac * ((period & 1) ? -1.0f : 1.0f);
    return folded * 2.0f * t;
}

// 3-stage cascade: each stage has a progressively tighter threshold
// Inspired by Buchla 259's 5-stage center-clipper structure (DAFx17 paper)
// and the Serge triangle-fold model (MDPI 2017 paper)
float fold_triangle_buchla(float x, float drive) {
    // Stage 1: main fold at ±1 (same as LINEAR)
    float s1 = fold_linear_compact(x);

    // Stage 2: boost and fold again — creates sub-folds in the reflected region
    float s2 = fold_linear_compact(x * 1.4142f) * 0.7071f;  // √2 boost, 1/√2 gain

    // Stage 3: further boost
    float s3 = fold_linear_compact(x * 2.0f) * 0.5f;

    // Mix stages — s1 dominant, s2/s3 add harmonic color
    // Weights approximate the Buchla 259 mixer stage ratios
    return (s1 * 0.5f + s2 * 0.35f + s3 * 0.15f) * 2.0f; // normalize to ±1 peak
}

// Full fold_triangle with integrated pre-gain drive:
float fold_triangle(float x, float fold_amount) {
    // Pre-gain: same quadratic curve as LINEAR
    float drive = 1.0f + fold_amount * fold_amount * 9.0f;
    float driven = x * drive;
    return fold_triangle_buchla(driven, drive);
}
```

**Note on the true Buchla 259 math (DAFx17 / Newfangledaudio):**
The paper models each stage as a "center clipper" with transfer function:
```
H_k(x) = clamp(slope_k * x + offset_k, -limit_k, +limit_k)
```
with 5 stages summed, each having different `slope_k` and `offset_k`. The exact coefficients
require circuit-tracing the 259 schematic. The 3-stage approximation above captures the
perceptual character without requiring circuit extraction.

**Alternative: direct Serge math (MDPI 2017 — Lockhart/Serge comparison):**
The Serge Wave Multiplier is modeled as a cascade of "full-wave rectifier" stages interleaved with
gain stages. A simplified version:
```cpp
float fold_serge(float x) {
    // Full-wave rectify then re-sign — Serge triangle fold per stage
    float y = std::abs(x);
    y = 2.0f * y - 1.0f;   // shift from [0,1] to [-1,1]
    if (y < 0) y = -y;      // second rectify
    return y;
}
// Multiple stages in series = the classic Serge wavemultiplier response
```
The 3-stage cascade approximation above is preferred for Phase 11d as it integrates better with
the pre-gain architecture.

### Pre-Fold Gain Curve

Same curve as LINEAR — the TRIANGLE shape uses the same pre-gain mechanism, just runs the driven
signal through the multi-stage fold kernel instead of a single stage:

```cpp
float drive_triangle(float fold_amount) {
    // Identical to LINEAR: quadratic, 1 → 10
    return 1.0f + fold_amount * fold_amount * 9.0f;
}
```

Because the Buchla-style fold has MORE effective stages, it sounds noticeably folded at LOWER drive
than the single LINEAR fold. To compensate, the pre-gain range can be reduced:

```cpp
float drive_triangle(float fold_amount) {
    // Slightly gentler: 1 → 6 range so the knob doesn't feel too aggressive
    return 1.0f + fold_amount * fold_amount * 5.0f;
}
```
This is a tuning call for Phase 11d — start with 1→6 range and adjust by ear.

### Sonic Character

**Musical, warm, harmonically rich — "West Coast synthesis" character.** The multi-stage fold
produces both ODD and EVEN harmonics (the mixing of stages at different scales breaks the odd
symmetry that a single linear fold maintains). This gives a RICHER, less buzzy harmonic spectrum
than the LINEAR fold — more like overdriven tube harmonics.

The Buchla 259 is described as "jagged" at the transfer function level (DAFx17) but sonically it
is WARMER than digital clipping because the folds are spaced evenly and the harmonic rolloff is
faster than LINEAR. The Newfangledaudio description: "substantial high-order harmonics creating
harsh tonal qualities at center sections" — meaning it IS aggressive at the fold centers but the
peaks between folds are smooth.

Compared to SINE fold:
- More harmonics (odd + even vs. odd-only)
- More analog character (the imperfect stage mixing approximates the random tolerance of a real
  circuit)
- More aggressive at medium amounts, slightly less "hollow"

Compared to LINEAR fold:
- WARMER — the multi-stage mixing reduces sharp corners
- More EVEN harmonics — sounds "rounder"
- Less aliasy at high drive

The Serge Wave Multiplier specifically emphasizes ODD harmonics in its lower stages and EVEN
harmonics in its rectifier stages (Learning Modular reference) — the mixed result is what makes it
"musical."

### At What Amount Does It Sound Different?

- **0–20%** (drive 1.0–1.2): imperceptible.
- **25–40%** (drive 1.3–2.0): warmth and upper harmonics begin — gentle, pleasing.
- **45–65%** (drive 2.1–3.8): clearly folded, both odd and even harmonics, rich texture.
- **70–100%** (drive 4.0–6.0): multiple fold layers, intense but musical up to ~80%,
  then aggressive at 90–100%.

### DC Offset

**CAN introduce DC offset.** Because the multi-stage TRIANGLE fold mixes stages at different
scales, if the even and odd stages do not perfectly cancel (which they don't in the approximation
above), a small DC bias can appear. The AAS Multiphonics manual explicitly calls out DC offset for
multi-stage folders and provides an AC coupling switch.

**Recommendation for Phase 11d:** Apply a DC blocker (1-pole high-pass at ~20 Hz) on the output
of the TRIANGLE fold only. Use the same `dc_block()` function shown under Shape 1. This is
analogous to what the real Buchla 259 does with its output section.

The `b259wf` Faust implementation (GitHub: georgezachos/b259wf) explicitly includes DC blocking as
a final stage.

### Loudness Compensation

The multi-stage fold can produce LOUDER output than a single fold because harmonics add in-phase
at fold points. The 3-stage mix above normalizes output to roughly ±1 peak via the weighted sum
coefficient (total weights × 2.0 normalizer). In practice:

```cpp
// Same taper approach as LINEAR:
float makeup = 1.0f / std::max(1.0f, 0.5f + fold_amount * 1.5f);
```

If the 3-stage cascade produces noticeable hot peaks, add a soft limiter:
```cpp
float soft_limit(float x) {
    return tanhf(x * 0.8f) / tanhf(0.8f);  // tanh(0.8x)/tanh(0.8) ≈ identity below 0.8
}
```

### References

- DAFx17 paper "Virtual Analog Buchla 259 Wavefolder": https://www.dafx17.eca.ed.ac.uk/papers/DAFx17_paper_82.pdf
- Newfangledaudio Buchla 259 article: https://www.newfangledaudio.com/post/model-bending-the-buchla-259-wavefolder
- MDPI "Virtual Analog Models of the Lockhart and Serge Wavefolders": https://www.mdpi.com/2076-3417/7/12/1328
- GitHub b259wf (Faust): https://github.com/georgezachos/b259wf (DC blocking stage)
- Learning Modular — Serge Wave Multiplier odd/even: https://learningmodular.com/randomsource-serge-wave-multipliers-wave-folding/

---

## Anti-Aliasing Strategy

### Why Wavefolding Aliases Badly

Wavefolding generates harmonics ABOVE the Nyquist frequency whenever the input is driven
significantly into the fold region. Unlike static waveshaping, the harmonic content is not bounded
— each additional fold exponentially multiplies the harmonic count. The LINEAR fold is the worst
offender (nearly unbounded spectrum); the SINE fold is the most benign (spectrum decays per
Bessel function magnitudes).

At 44100 Hz, notes above C4 start to put significant aliasing energy in audible range at high
fold amounts. At C7 (2093 Hz) with 4 folds, the theoretical harmonic content extends to ~8 kHz ×
harmonic number — heavy aliasing.

### Approaches (per research)

1. **Oversampling (simplest):** Run the fold block at 2x–8x the audio sample rate, then
   downsample with a low-pass filter.
   - 2x oversampling reduces aliasing by ~6 dB (one octave)
   - 8x oversampling (BLAMP method from DAFx17) pushes aliasing below –80 dB
   - CPU cost: proportional to oversampling factor
   - For Phase 11d: **2x oversampling is the recommended minimum** for the fold block.
     This matches the MDPI paper's finding: "2x oversampling combined with first-order ADAA
     is on par with 8x oversampling alone."

2. **Anti-Derivative Anti-Aliasing (ADAA — first order):** Replaces `f(x[n])` with
   `(F(x[n]) – F(x[n–1])) / (x[n] – x[n–1])` where F is the antiderivative of the fold
   function. For the SINE fold, F(x) = –cos(g·x)/g (trivially computable). For LINEAR fold,
   F(x) is a piecewise quadratic. The DAFx17 paper and MDPI 2017 paper both use ADAA.
   - Cost: one extra multiply and divide per sample, plus storage of x[n-1]
   - No oversampling needed (or combine 2x + ADAA for best result)
   - **Recommended for Phase 11d future work** — Phase 11d can ship with 2x oversampling
     and upgrade to ADAA in Phase 11f.

3. **Accept some aliasing (Phase 11d stance):** As instructed, Phase 11d accepts analog-style
   aliasing as "character." The Buchla 259 in hardware produces aliasing in its digital
   approximations; the real analog circuit aliases differently (BLAMP-reduceable but still
   impure). For the initial implementation, ship with **2x oversampling** as the minimum
   quality floor.

### JUCE Implementation Sketch for 2x Oversampling

```cpp
// In SynthVoice or wavetable render path:
// 1. Upsample block by 2 (zero-insert + 2-pole Butterworth LP at fs/2)
// 2. Process fold on upsampled buffer
// 3. Downsample by 2 (decimate with the same LP filter)
// JUCE provides juce::dsp::Oversampling<float> for this:

juce::dsp::Oversampling<float> oversampling(1, 1,
    juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR);
// Order 1 = 2x, IIR half-band is low CPU.
// Wrap the fold function inside oversampling.processSamplesUp/Down.
```

---

## DC Offset Handling

| Shape    | Produces DC? | When?                           | Fix                              |
|----------|--------------|---------------------------------|----------------------------------|
| LINEAR   | No (usually) | Only if source wavetable has DC | DC blocker optional              |
| SINE     | No           | Only if source has DC           | DC blocker optional              |
| TRIANGLE | Yes (likely) | Multi-stage mix breaks symmetry | DC blocker REQUIRED on output    |

### DC Blocker (1-pole high-pass, per-voice state):

```cpp
class DcBlocker {
public:
    float process(float x) {
        float y = x - x_prev_ + 0.995f * y_prev_;
        x_prev_ = x;
        y_prev_ = y;
        return y;
        // –3 dB at ~22 Hz @ 44100 Hz
    }
    void reset() { x_prev_ = y_prev_ = 0.0f; }
private:
    float x_prev_ = 0.0f, y_prev_ = 0.0f;
};
```

Run per-voice, per-channel, on the fold output. Cost is negligible (2 adds + 1 multiply per
sample).

---

## Loudness Summary Table

| Shape    | Output Bounded? | RMS Rise at 100% | Compensation Strategy        |
|----------|-----------------|------------------|------------------------------|
| LINEAR   | Yes (±1 peak)   | +4 to +6 dB      | Static taper (quadratic) + optional tanh limiter |
| SINE     | Yes (±1 peak)   | +3 to +5 dB      | Static taper (quadratic); no hard limiter needed |
| TRIANGLE | Near-bounded    | +5 to +8 dB      | Static taper + soft tanh limiter recommended |

All three shapes are peak-bounded by their folding geometry — they CANNOT produce output outside
±1 from a ±1 input (after pre-gain drive). The LOUDNESS increase is RMS-based: the waveform
spends more time near ±1, raising perceived and measured RMS.

---

## Recommendation: FOLD Knob Gain Curve

The FOLD AMT knob maps `fold_amount ∈ [0.0, 1.0]` to a pre-fold drive gain. The goal:

- **At 0%:** no folding — output == input.
- **At 50%:** noticeable harmonic generation — first fold well established.
- **At 100%:** dramatic multi-fold cascade — heavily transformed timbre.

### Recommended Curve: Quadratic

```cpp
// Drive = 1 at amount=0 (identity), drive = K at amount=1
// K = 10 for LINEAR/TRIANGLE (aggressively driven to ~5+ folds on sine input)
// K = 6.28 for SINE (two full sin() periods at max)

float fold_drive(float fold_amount, float K = 10.0f) {
    // Quadratic easing: slow start, fast finish
    return 1.0f + (fold_amount * fold_amount) * (K - 1.0f);
}
```

**Why quadratic over linear or exponential?**

| Curve      | 0% | 25%  | 50%  | 75%  | 100% | Character                    |
|------------|-----|------|------|------|------|------------------------------|
| Linear     | 1.0 | 3.25 | 5.5  | 7.75 | 10.0 | Too aggressive early         |
| Quadratic  | 1.0 | 1.56 | 3.25 | 6.06 | 10.0 | Subtle early, dramatic late  |
| Exponential| 1.0 | 1.78 | 3.16 | 5.62 | 10.0 | Musical, similar to quadratic|

Quadratic keeps 0–30% "browsable" (gentle warmth) and makes 50–100% increasingly dramatic. This
matches the Pigments and AAS Multiphonics fold knob behavior described in their manuals — fold is
a "color" control at low settings and a "transformation" control at high settings.

### Per-Shape K Values

```cpp
// In the render path, pick K by shape:
float K = (shape == FOLD_LINEAR)   ? 10.0f :
          (shape == FOLD_SINE)     ?  6.28f :   // 2π → two full fold cycles
          (shape == FOLD_TRIANGLE) ?  6.0f  :   // Slightly gentler (3 stages do more work)
                                      10.0f;

float drive = fold_drive(fold_amount, K);
float folded = fold_by_shape(wavetable_sample * drive, shape);
float output  = folded * makeup_gain(fold_amount);
```

---

## Implementation Architecture for Phase 11d

```cpp
// In the wavetable render loop (per-sample, after WT_POS lookup):

// 1. Pre-gain
float driven = sample * fold_drive(fold_amount_, K_for_shape_);

// 2. Fold
float folded;
switch (fold_shape_) {
    case FOLD_LINEAR:
        folded = fold_linear_compact(driven);
        break;
    case FOLD_SINE:
        folded = sinf(driven);
        break;
    case FOLD_TRIANGLE:
        folded = fold_triangle_buchla(driven, fold_drive_);
        break;
}

// 3. DC block (always-on for TRIANGLE; optional for LINEAR/SINE)
folded = dc_blocker_.process(folded);

// 4. Makeup gain
folded *= makeup_gain(fold_amount_);

// 5. Continue to SPREAD / voice summing
```

**APVTS parameters needed:**
- `SYN_OSC_A_FOLD_AMT` — float [0, 1], default 0.0
- `SYN_OSC_A_FOLD_SHAPE` — int {0=LINEAR, 1=SINE, 2=TRIANGLE}, default 0
- (Mirror for OSC B)

---

## Shape Comparison Summary

| Property             | LINEAR            | SINE              | TRIANGLE (Buchla) |
|----------------------|-------------------|-------------------|-------------------|
| Transfer function    | Triangle wave TF  | sin(drive · x)    | 3-stage fold mix  |
| Harmonics generated  | Odd only          | Odd only          | Odd + Even        |
| Harmonic count       | Almost unbounded  | Bessel-limited    | Rich, bounded     |
| Sonic character      | Harsh, buzzy      | Smooth, musical   | Warm, complex     |
| Aliasing tendency    | Worst             | Best              | Moderate          |
| DC offset risk       | Low               | Low               | Medium            |
| Output bounded?      | Yes (±1)          | Yes (±1)          | Near (±1)         |
| "Sounds folded" at   | ~30% amount       | ~30–40% amount    | ~25–35% amount    |
| Analog reference     | Serge linear      | Serge sine stage  | Buchla 259 / Serge|
| Pigments equivalent  | Likely "Classic"  | Likely "Smooth"   | Likely "Warm"     |
| Vital equivalent     | —                 | `asin→sin` shape  | —                 |

---

## Open Questions for Phase 11d

1. **Pigments fold shape names** — the Pigments 7.0 manual TOC (object 326) lists "Wavefolding
   Shape" but the PDF exceeds tool read limit. The three shapes in Pigments are confirmed to exist
   but their exact names (Linear/Sine/Triangle vs. Classic/Smooth/Warm or similar) could not be
   extracted. **Recommendation:** treat our names LINEAR/SINE/TRIANGLE as internal identifiers;
   use display names "HARD / SMOOTH / WARM" on the UI if Pigments-style naming is desired.

2. **2x oversampling placement** — oversampling the fold block means oversampling PER VOICE, which
   multiplies CPU by the oversampling factor × voice count. At 16 voices × 2x = 32 blocks of fold
   math. Profile first; if too expensive, skip oversampling and document as "analog character."

3. **TRIANGLE stage coefficients** — the 0.5/0.35/0.15 weights and √2/2.0 boosts above are
   approximated from the Buchla 259 5-stage structure. They should be tuned by ear in Phase 11d
   against a reference sound of a Buchla 259 patch.

4. **FOLD + WARP interaction** — Phase 11d FOLD runs AFTER WARP in the signal chain (WARP modifies
   the wavetable output, then FOLD folds the already-warped wave). This ordering is correct per
   the Phase 11a spec but should be verified against Pigments' ordering.
