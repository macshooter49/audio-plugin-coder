# 13 — PAPERS: the DSP literature basis for spectral smear, blur and dispersion in a wavetable

Companion to `00-INVENTORY.md` (what Terrain ships today). This file is the **maths**: for every
technique, the formula, the parameter, a defensible range, the cost class, and a citation you can check.

## Provenance of every number in this file

| Marker | Meaning |
|---|---|
| **[SRC]** | Read out of real source code on this machine. File + line given. |
| **[PAPER]** | From a published paper. Author/title/year/venue given; URL in §G. |
| **[MEAS]** | Measured today by me, on this machine, with a throwaway harness. The script is named. |
| **[INFERRED]** | My derivation or judgement. Not stated by any source. Treat as a proposal, not a fact. |

Reference implementation read for this document: **Vital**, `github.com/mtytel/vital`, GPL-3,
cloned at commit `636ca0e`, sitting at
`…/scratchpad/vital/`. I read the actual `.cpp`/`.h`, not blogs.
**Vital has no official manual that documents any of this maths** — the community manual describes
what the knobs sound like, not what they compute. Every Vital number below is from the source.

Second reference implementation: **CDP** (Composers Desktop Project), `github.com/ComposersDesktop/CDP8`,
`dev/blur/blur.c` and `dev/blur/ap_blur.c`, fetched today.

Harnesses written today (all in `…/scratchpad/`):
`disp.py`, `wtdisp.py`, `harm.py`, `harm2.py`, `specbench.cpp`, `specbench2.cpp`.

---

## 0. The single-cycle contract (what makes wavetable spectral work different)

A 2048-sample single cycle is a **periodic** signal. Its DFT is not an estimate of a spectrum — it
*is* the spectrum, exactly, with no window, no leakage, no bin-frequency ambiguity. Harmonic `h`
lives in bin `h` and nowhere else. Terrain gets this right: `00-INVENTORY.md` §5 —
"There is no analysis window in the wavetable path (rectangular full-cycle, correct — don't add one)."

Consequences that govern everything below:

1. **There is no time axis inside a frame.** Every "time-domain smear" idea from the phase-vocoder
   literature has to be re-expressed as either (a) a phase manipulation, or (b) a *frame*-axis
   operation. §C is where this bites.
2. **The frequency axis is the harmonic index.** Convolving along it is convolving along `h`, and
   the kernel width is in *harmonics*, not Hz. A width of 11 harmonics is 11 × f0 wide — 2.4 kHz at
   A3 (220 Hz), 550 Hz at A1. That is a large, pitch-dependent difference and it is not a bug, it is
   what "blur" means on a harmonic grid. [INFERRED — worth stating in the UI]
3. **Everything is circular.** A delay of 3000 samples on a 2048-sample cycle is a delay of 952
   samples. Group delays larger than one period wrap. §A3 shows Vital deliberately running 3.7 wraps.
4. Vital: `WaveFrame::kWaveformBits = 11`, `kWaveformSize = 2048`, `kNumRealComplex = 1025`
   **[SRC]** `src/synthesis/lookups/wave_frame.h:27-29`. Unnormalised real FFT: a unit-amplitude sine
   is written as `frequency_domain[1] = 1024` (= N/2) **[SRC]** `wave_frame.cpp:110-114`. Terrain uses
   the same N and an equivalent `X[h] = (A_h/2)(sin φ − i cos φ)` convention
   (`00-INVENTORY.md` §5). **The two are compatible up to the phase-origin convention** — Terrain's
   is sine-referenced, Vital's is cosine-referenced. Any formula lifted from Vital needs `φ → φ − π/2`
   or the waveform arrives rotated by a quarter cycle. [INFERRED]

Vital's per-frame storage is worth copying and Terrain does not have it **[SRC]** `wavetable.cpp:160-177`:

```
frequency_amplitudes[2i] = frequency_amplitudes[2i+1] = |X[i]|      // duplicated for SIMD
normalized_frequencies[i] = X[i] / |X[i]|                            // unit phasor, complex
phases[2i]  = phases[2i+1] = arg(X[i])                               // duplicated for SIMD
```

i.e. **magnitude and unit-phasor are stored separately**. Every magnitude-only morph is then one
multiply against `frequency_amplitudes` with `normalized_frequencies` carried through untouched —
no `atan2`, no `polar()`, no phase ever recomputed. `poly_float::kSize = 4` on NEON and SSE
**[SRC]** `src/synthesis/framework/poly_values.h:56`, so one `poly_float` = 2 complex bins = 2 harmonics.
That fact is load-bearing for reading §B2.

---

# §A — PHASE DISPERSION

**The key idea, stated once and proved below:** a phase-only operator `X'[h] = X[h]·e^{jφ(h)}`
leaves `|X'[h]| = |X[h]|` **exactly**. The magnitude spectrum is untouched. The *waveform* is not.
So a dispersion knob is a knob that changes the shape of the wave on the scope, changes the crest
factor by up to 10 dB, changes how the wave interacts with any downstream nonlinearity (fold, clip,
saturate) — and changes the steady-state spectrum by **zero**.

[MEAS — `wtdisp.py`] I applied Vital's maximum dispersion to a 256-harmonic saw and re-analysed:
max relative magnitude error **8.5 × 10⁻¹⁷**. That is float round-off. The magnitude spectrum is
bit-for-bit the same.

## A1 — The classical construction: a cascade of first-order allpasses

This is the canonical dispersion element in music DSP. First-order allpass:

```
          a + z⁻¹
H(z) = ───────────── ,   a real, |a| < 1
        1 + a·z⁻¹
```

Magnitude `|H(e^{jω})| = 1` for all ω. Phase and group delay:

```
θ(ω) = −ω + 2·atan2( a·sin ω , 1 + a·cos ω )

              1 − a²
τ(ω) = ──────────────────────      (samples)
        1 + 2a·cos ω + a²

τ(0) = (1−a)/(1+a)        τ(π) = (1+a)/(1−a)
```

[MEAS — `disp.py`] Verified numerically over 200 001 frequency points: `||H|−1| ≤ 8.9e-16`,
closed-form phase matches `angle(H)` to 4.4e-16, closed-form `τ` matches `−dθ/dω` to 1.5e-8.

Cascade **M** identical sections → phase and group delay both multiply by M:

| a | M | τ(0) | τ(π) | spread | chirp length @44.1 kHz | [MEAS `disp.py`] |
|---|---|---|---|---|---|---|
| +0.50 | 100 | 33.3 | 300.0 | 266.7 samp | **6.05 ms** | |
| +0.50 | 200 | 66.7 | 600.0 | 533.3 samp | **12.09 ms** | |
| +0.80 | 50 | 5.6 | 450.0 | 444.4 samp | **10.08 ms** | |
| −0.60 | 120 | 30.0 | 480.0 | 450.0 samp | **10.20 ms** | |

Sign of `a` picks the direction: `a > 0` delays the highs (falling chirp, "boing"/spring),
`a < 0` delays the lows (rising chirp, "zap").

**Sources.**
- Van Duyne & Smith, *"A Simplified Approach to Modeling Dispersion Caused by Stiffness in Strings
  and Plates"*, ICMC 1994, Århus, pp. 407–410 — **the** cascaded-first-order-allpass dispersion paper;
  the whole stiff-string / piano dispersion literature descends from it. [PAPER]
- Välimäki, Abel & Smith, *"Spectral Delay Filters"*, JAES vol. 57 no. 7/8, Aug 2009, pp. 521–531 —
  M cascaded low-order allpasses + an equalising filter, used deliberately as an *audio effect*;
  the paper's own words are "chirp-like impulse responses causing a large, frequency-dependent
  delay". This is the citation for "dispersion as an effect", not as a string model. [PAPER]
- Rauhala & Välimäki, *"Dispersion modeling in waveguide piano synthesis using tunable allpass
  filters"*, DAFx-06 — the tuning rules for `a` given a target inharmonicity. [PAPER]
- Abel, Berners, Costello & Smith, *"Spring Reverb Emulation Using Dispersive Allpass Filters in a
  Waveguide Structure"*, AES 121st Convention, 2006 — the same machine, hundreds of sections. [PAPER]
- Pekonen & Välimäki, *"Spectral delay filters with feedback and time-varying coefficients"*, DAFx-09. [PAPER]
- Timoney, Lazzarini, Pekonen & Välimäki, *"Spectrally Rich Phase Distortion Sound Synthesis Using
  an Allpass Filter"*, ICASSP 2009, Taipei, pp. 293–296 — allpass phase distortion as *synthesis*,
  the modern restatement of Casio CZ phase distortion. [PAPER]

**Cost class: PER-SAMPLE, and therefore WRONG for Terrain's wavetable path.** M=100 sections is 100
biquad-ish states per voice per sample. Terrain must not do this on the audio thread. It belongs in
the FX rack if anywhere. §A2 is the version that belongs in the table bake.

## A2 — The same chirp, done once, in the bake: quadratic phase

The allpass cascade's contribution is *only* its phase curve. On a periodic 2048-sample signal you
already know every frequency present (they are the 1024 harmonics), so you can evaluate the phase
curve at those 1024 points and apply it directly:

```
X'[h] = X[h] · e^{ j·φ(h) },     h = 1 … H
```

For a **constant group delay slope** (the defining property of a dispersive line: delay rises
linearly with frequency) integrate once:

```
τ(h) = τ₀ + k·h    (samples)        ⇒       φ(h) = −(2π/N)·( τ₀·h + (k/2)·h² )
```

so **linear group delay ⇔ quadratic phase**. That is the whole of dispersion in a wavetable, and it
costs one `sin`/`cos` per harmonic, once, at bake time. The generic form:

```
φ(h) = c · ( h − h_c )²  + const                     [the "chirp"]
τ(h) = −(N/2π) · dφ/dh = −(N/π)·c·(h − h_c)          [samples, linear in h]
```

`h_c` is the harmonic that stays put (zero group delay); `c` is the dispersion strength in
radians per harmonic². The additive constant is a whole-waveform rotation and is free — choose it so
the fundamental keeps phase 0 and the knob does not also rotate the wave.

Theory anchor: this is **Schroeder phase**. Schroeder, M. R., *"Synthesis of low-peak-factor signals
and binary sequences with low autocorrelation"*, IEEE Trans. Information Theory, vol. IT-16 no. 1,
1970, pp. 85–89. [PAPER] For a flat spectrum of H harmonics his closed-form low-crest phase is
exactly a quadratic:

```
φ_h = −π·h² / H            (flat spectrum)

                    h−1
φ_h = φ₁ − 2π · Σ  Σ  p_m / P     (general, p_m = power of harmonic m, P = Σp)
                    n=1 m=1
```

[MEAS — `wtdisp.py`] Crest factor of a 256-harmonic multisine, N=2048:

| spectrum | zero phase | Schroeder (general) | `−πh²/H` | random phase (mean of 64) |
|---|---|---|---|---|
| flat | **22.63** | **1.67** | **1.67** | 3.45 |
| 1/h (saw) | **6.76** | **2.30** | 3.47 | 2.35 |

On a flat spectrum Schroeder buys **22.6 dB** of peak reduction and beats random phase by 6.3 dB.
This is *why* dispersion sounds "softer/wider" with no EQ change: it is not removing energy, it is
removing the peak.

## A3 — What Vital actually does: `phaseMorph` **[SRC]** `src/synthesis/producers/spectral_morph.h:180-215`

```cpp
static constexpr float kCenterMorph = 24.0f;                      // line 183
float offset = -(kCenterMorph - 1.0f) * (kCenterMorph - 1.0f) * phase_shift;   // line 191
...
poly_float delta_center = (index - kCenterMorph) * (index - kCenterMorph) * phase_shift + offset; // 200
poly_float phase = utils::mod(delta_center * (0.5f/kPi) + phase_offset);      // 201  (radians→turns)
poly_float shift = futils::sin1(phase);                                        // 203
// then a complex multiply of `normalized` by `shift` via swapStereo (lines 204-209)
```

Decoded — Vital's dispersion law is **exactly the A2 chirp with `h_c = 24`**:

```
φ(h) = c · [ (h − 24)² − 23² ]        c = phase_shift  [radians per harmonic²]
```

The `−23²` offset makes `φ(1) = 0`, so the fundamental never moves — the knob disperses, it does not
rotate. `h_c = 24` means harmonics 1…24 disperse *backwards* and 25…1024 forwards, hinging around
the 24th harmonic. `futils::sin1(x) ≈ sin(2πx)` is a parabola-plus-correction approximation
**[SRC]** `futils.h:326-339` — fine at audio rate, but for an offline bake use real `sinf`/`cosf`.

**The parameter, exactly** **[SRC]** `synth_oscillator.cpp:1105-1108`:

```cpp
case kPhaseDisperse:
  values[i] = -(values[i] * 2.0f - 1.0f) * kPhaseDisperseScale;   // kPhaseDisperseScale = 0.05f
```

with `kPhaseDisperseScale = 0.05f` **[SRC]** `spectral_morph.h:31`. So the knob `a ∈ [0,1]` is
**bipolar** (confirmed: `isBipolarSpectralMorphType()` lists `kPhaseDisperse`
**[SRC]** `oscillator_section.cpp:189-196`), and `c = 0.05·(1 − 2a)`, i.e. `c ∈ [−0.05, +0.05]`
rad/harmonic², **linear in the knob**, centred at `a = 0.5` → `c = 0` → bypass.

**What that costs the waveform** [MEAS — `wtdisp.py`, 256-harmonic saw, N=2048]:

| c | crest | φ(256) | wraps | τ(256) samp | τ span (h=1…256) | |X'| error |
|---|---|---|---|---|---|---|
| 0.000 | 6.761 | 0 rad | 0.0 | 0 | 0 | — |
| 0.002 | 4.520 | 107 | 17.0 | −302 | 333 | |
| 0.005 | 4.080 | 266 | 42.4 | −756 | 831 | |
| 0.010 | 3.751 | 533 | 84.8 | −1512 | 1662 | |
| 0.020 | 3.399 | 1066 | 169.6 | −3025 | 3325 | |
| **0.050** (Vital max) | **2.177** | **2665** | **424.1** | **−7562** | **8312** | **8.5e-17** |

Read that last row: at full dispersion Vital pushes harmonic 256 by **7562 samples = 3.7 whole
cycles** of the 2048-sample table. It is completely wrapped. Crest falls 6.761 → 2.177 = **−9.8 dB**
of peak. And 70 % of the crest reduction has already happened by `c = 0.005`, i.e. **within 5 % of the
knob's travel from centre.** That is a plateau problem (Terrain HARD RULE: no plateaus, "params
evolve 0→100"). Vital ships a linear map and lives with it.

**[INFERRED] Recommended parameterisation for Terrain.** Take the pain out of the top 90 % of the
knob by putting the range where the ear is:

```
c(a) = c_max · sign(2a−1) · |2a−1|^2.5      c_max ≈ 0.05 rad/harm²,  h_c = 24
```

An exponent of ~2.5 spreads the 6.76→2.18 crest travel roughly evenly across the knob. [INFERRED —
I measured the crest curve, I did not fit the exponent formally.]
Better still, expose it in a unit a musician can reason about: **`τ_max` in whole cycles**, since
`τ(H) = −(N/π)·c·(H − 24)` ⇒ `c = π·τ_max / (N·(H−24))`. At H=512, N=2048, "1 cycle of spread"
= `c = 3.1416·2048/(2048·488) = 0.00644`.

## A4 — The degenerate case: LINEAR phase = rotation, and why "mix" turns it into a comb

Vital's *wavetable-editor* `PhaseModifier` (a different, offline component from `phaseMorph`) is worth
reading because it demonstrates the trap **[SRC]** `src/common/wavetable/phase_modifier.cpp:46-82`:

| style | code | what it is |
|---|---|---|
| `kNormal` | `current_phase_shift *= phase_shift` each bin, i.e. `e^{−jhφ}` | **linear** phase ramp = a pure circular *time shift* of φ/2π cycles. Alone, inaudible on a looping table. |
| `kHarmonic` | same constant `e^{−jφ}` on every bin | constant phase rotation. Magnitude-preserving, waveform-changing (saw → "sine-ish ramp"). This is the real one. |
| `kEvenOdd` / `kHarmonicEvenOdd` | even bins `e^{+jhφ}`, odd bins `e^{−j(h+1)φ}` | opposed ramps; splits the wave into two counter-rotating halves. |
| `kClear` | `frequency_domain[i] = abs(frequency_domain[i])` | **zero-phase / cosine phase**. Maximum crest (22.6 on a flat spectrum, §A3). The opposite end of dispersion. |

And the trap, `multiplyAndMix` **[SRC]** `phase_modifier.cpp:22-25`:

```cpp
std::complex<float> result = value * mult;
return mix * result + (1.0f - mix) * value;
```

With `mix < 1` this is **not** a phase-only operator any more:

```
X'[h] = X[h]·( (1−m) + m·e^{−jhφ} )        ⇒        |X'[h]| = |X[h]| · |1 − m + m·e^{−jhφ}|
```

[MEAS — `wtdisp.py`] the multiplier sweeps between:

| mix m | max | min |
|---|---|---|
| 0.25 | 0.00 dB | −6.02 dB |
| **0.50** | 0.00 dB | **−∞ (total null)** |
| 0.75 | 0.00 dB | −6.02 dB |
| 1.00 | 0.00 dB | 0.00 dB |

At `mix = 0.5` the "phase" control is a **perfect comb filter with infinitely deep notches**.
**Lesson for Terrain: never dry/wet a phase rotation. Interpolate the phase, not the phasor.**
`φ_applied = mix · φ_target` keeps it allpass; `lerp(X, X·e^{jφ}, mix)` does not. Terrain's `Smear`
currently does `p.phase += scatter` (interpolating the angle) — that is the correct form, keep it.

## A5 — Cost class for §A

| variant | when | cost |
|---|---|---|
| M-section allpass cascade | per sample, per voice | M multiply-adds/sample. M=100 ⇒ ~100× a one-pole. **Never on Terrain's oscillator.** |
| quadratic phase in the bake | once per table rebuild | 1 sin + 1 cos + 1 complex multiply per harmonic. **[MEAS `specbench2.cpp`] 1.77 µs for 512 harmonics; 28.3 µs for a 16-frame spec.** Negligible next to the iFFT (§E). |

---

# §B — SPECTRAL BLUR: smearing along the FREQUENCY axis

## B1 — The primitive: convolution along the harmonic index

```
|X'[h]| = Σ_k w[k] · |X[h+k]|  /  Σ_k w[k]        (edge-truncated, weights renormalised)
arg X'[h] = arg X[h]                              (phase carried through untouched)
```

This is a **low-pass filter applied to the spectral envelope**, not to the signal. It does not
remove partials and it does not add partials — on a harmonic grid every output bin is still a
harmonic. What it removes is *contrast*: notches fill in, peaks flatten, formants melt.

Kernels, in ascending order of "how much like a Gaussian":

| kernel | `w[k]`, `|k| ≤ W` | notes |
|---|---|---|
| boxcar / moving average | `1` | CDP's choice. Cheapest. Sinc-shaped sidelobes → can *ring* along frequency. |
| triangular (Bartlett) | `1 − |k|/(W+1)` | Terrain's choice. = boxcar ⊛ boxcar. No ringing. |
| Gaussian | `exp(−k²/2σ²)`, `W = ⌈3σ⌉` | smoothest; `σ` is the natural parameter. |
| one-pole IIR | recursive, see B2 | O(1) per bin regardless of width. Asymmetric (bleeds upward only). |

Terrain's `Smear` **[SRC]** `Source/SpectralMorph.h:203-218` is triangular with
`W = round(amount · 11)` and edge-truncated renormalisation by `wsum` — textbook-correct.

[MEAS — `harm.py`] Put a one-harmonic notch at h=40 in a 256-harmonic saw and measure how far the
blur fills it back in (dB relative to the *unblurred* saw's value there — 0 dB = notch fully erased):

| W | boxcar fills to | triangular fills to | total RMS change (boxcar) |
|---|---|---|---|
| 1 | −3.52 dBr | −6.02 dBr | −0.83 dB |
| 3 | −1.31 dBr | −2.48 dBr | −1.98 dB |
| **11** | **−0.13 dBr** | **−0.61 dBr** | −4.32 dB |
| 32 | +2.80 dBr | +1.09 dBr | −6.90 dB |

So Terrain's `W = 11` maximum erases a one-harmonic notch to within 0.6 dB, and costs 4.3 dB of RMS
(because a saw's `1/h` envelope loses energy when you average it — the average of `1/h` over a
window is above `1/h` at the top and below at the bottom, and the low bins dominate).
**Any blur along frequency needs energy re-normalisation** or the knob is also a volume knob.
Terrain's `renderBlend` already does this (`G = √(Σref²/Σpre²)`, `00-INVENTORY.md` §3);
`SpectralMorph::Smear` does **not**. [SRC — I read `SpectralMorph.h:203-218`; there is no gain trim.]

**Range guidance.** `W` in *harmonics*. `W = 0` identity; `W = 1…3` "soft"; `W = 8…16` audible wash;
`W > 32` is "the spectrum is now its own envelope" and stops changing character. Terrain's 0…11 is a
good range. [INFERRED, but the notch-fill table above is the evidence.]

**Cost class: per-bake, O(H·W).** [MEAS `specbench2.cpp`, H=512, one frame]
W=1 → **0.37 µs**; W=11 → **8.36 µs**; W=64 → **60.4 µs**. Times 16 frames: W=11 costs **136 µs**
per spec. Still small next to the iFFT ladder (§E) but it is the most expensive *morph* op measured.
If wider blur is ever wanted, switch to two boxcar passes (O(H) via a running sum, giving the same
triangular kernel) or to the one-pole of §B2.

## B2 — Vital's `smearMorph`: a one-pole IIR **along frequency** **[SRC]** `spectral_morph.h:217-241`

This is a different and cheaper animal, and it is worth understanding precisely.

```cpp
poly_float amplitude = frequency_amplitudes[0] * (1.0f - smear);
wave_start[0] = amplitude * normalized_frequencies[0];
for (int i = 1; i <= last_index; ++i) {
  poly_float original_amplitude = frequency_amplitudes[i];
  amplitude = utils::interpolate(original_amplitude, amplitude, smear);   // a = (1−s)·A[i] + s·a
  wave_start[i] = amplitude * normalized_frequencies[i];
  amplitude *= (i + 0.25f) / i;                                          // upward tilt
}
```

As maths, with `s` the smear coefficient and `i` the **poly index** (= 2 harmonics, §0):

```
a[0] = (1−s)·A[0]
a[i] = (1−s)·A[i] + s·a[i−1]·(i−0.75)/(i−1)          i ≥ 1
|X'[i]| = a[i] ,   arg X'[i] = arg X[i]              (phase untouched — magnitude-only)
```

Three things to notice:

1. **Causal, one-directional.** Energy bleeds *upward* in frequency only. That is a deliberate,
   different sound from a symmetric blur — it turns a peak into a peak-with-a-tail, like a resonance
   ringing, not into a hump.
2. **The `(i+0.25)/i` factor is a built-in tilt.** [MEAS] the cumulative product over 512 poly
   indices (= 1024 harmonics) is **×5.250 = +14.40 dB** (closed form `512^0.25/Γ(1.25)` = 5.2480).
   Without it a one-pole smear on a `1/h` spectrum just goes dull; this deliberately re-brightens.
3. **The knob is cubed.** **[SRC]** `synth_oscillator.cpp:1095-1100`:
   `s = 1 − (1 − a)³`, `a ∈ [0,1]`.

[MEAS] The reach of the smear, `τ = 1/ln(1/s)` in poly indices (×2 for harmonics):

| knob `a` | `s` | τ (harmonics) | −60 dB reach (harmonics) |
|---|---|---|---|
| 0.00 | 0.0000 | 0 | 0 |
| 0.10 | 0.2710 | 1.5 | 10.6 |
| 0.25 | 0.5781 | 3.6 | 25.2 |
| 0.50 | 0.8750 | **15.0** | 103.5 |
| 0.75 | 0.9844 | 127.0 | 877.3 |
| 0.90 | 0.9990 | 1999 | 13809 |
| 1.00 | 1.0000 | ∞ | ∞ (whole spectrum = DC's value, carried up) |

The cubic knob map is doing real work: at knob 0.5 the reach is 15 harmonics (comparable to
Terrain's max W=11 triangular), and the top half of the knob is where it goes surreal.

**Cost class: per-bake, O(H) — width-independent.** [MEAS `specbench2.cpp`] **1.60 µs** for 512
harmonics; **25.7 µs** for 16 frames. Compare the triangular W=11 at 8.36 µs / 136 µs. A one-pole is
**5× cheaper and unlimited in width.** [INFERRED recommendation: if Terrain wants a *wide* blur,
this is the cheap way; if it wants a *symmetric* blur, run the one-pole forward and backward and
average — that gives a two-sided exponential kernel at 2× the cost, still O(H).]

## B3 — Leakage-based smear (and why a single cycle can't have it for free)

The other classical route to frequency-axis smearing is the **windowing/convolution duality**:
multiplying in time convolves in frequency.

```
x[n]·w[n]   ⟷   (1/N)·(X ⊛ W)[k]
```

So if you want to convolve the spectrum with kernel `W`, you can instead multiply the time signal by
the kernel's inverse transform `w`. Classic kernels:

| window | main lobe (bins, for an N-point rectangular-referenced transform) | first sidelobe |
|---|---|---|
| rectangular | 2 | −13 dB |
| Hann | 4 | −31 dB |
| Hamming | 4 | −41 dB |
| Blackman | 6 | −57 dB |
| Blackman-Harris (4-term) | 8 | −92 dB |

**This does not work on a single-cycle wavetable and Terrain must not try it.** A window `w[n]`
that is not constant destroys the periodicity — the "cycle" is no longer a cycle, and looping it
produces a discontinuity at the wrap. `00-INVENTORY.md` §5 already states the rule ("don't add one")
and it is right. The Hann kernel `[0.25, 0.5, 0.25]` *can* be applied directly as a 3-tap frequency
convolution (that is exactly the boxcar/triangular of B1 with W=1), which is the legitimate version
of this idea on a harmonic grid. [INFERRED — the duality is textbook (Harris, F. J., *"On the use of
windows for harmonic analysis with the discrete Fourier transform"*, Proc. IEEE 66(1), 1978, 51–83
[PAPER]); the "don't do it here" conclusion is mine.]

## B4 — CDP: the two axes, named and separated  **[SRC]** — real code, fetched today

CDP's BLUR suite is the canonical vocabulary and it is worth adopting because it keeps the two axes
distinct in the *names*.

**`BLUR AVRG` — frequency axis.** Parameter `N` = number of adjacent channels to average
(doc: "≤ half the channels in infile"). **[SRC]** `dev/blur/blur.c:94-134`, function `specavrg`:

```c
dz->iparam[AVRG_AVRGSPAN] = dz->iparam[AVRG_AVRG]/2;
dz->iparam[AVRG_AVRG]     = (dz->iparam[AVRG_AVRGSPAN] * 2) + 1;   /* always odd */
...
for(n = vc - m; n <= vc + m; n += 2)                  /* TRUE AVERAGE */
    dz->amp[cc] = dz->amp[cc] + dz->flbufptr[0][n];
dz->amp[cc] = dz->amp[cc]/(double)dz->iparam[AVRG_AVRG];
```

A **boxcar of odd width `2·span+1` over magnitudes only**, with shrinking partial windows at both
edges (no wrap, no zero-pad). Frequencies/phases untouched. Doc: "broadens, or defocus, any energy
peaks in the spectrum."

**`BLUR SPREAD` — flatten toward the formant envelope.** Parameter `spread` range **0–1, default 1**.
**[SRC]** `blur.c:319-340`, `specspread`:

```c
ampdiff = specenv_amp - dz->flbufptr[0][AMPP];
dz->flbufptr[0][AMPP] = AMPP + (ampdiff * dz->param[SPREAD_SPRD]);
...
normalise(pre_totalamp, post_totalamp, dz);           /* energy preserved */
```

i.e. `|X'| = |X| + spread·(env(f) − |X|)` — a lerp of every bin toward the *spectral envelope*,
followed by total-energy renormalisation. **Note the renormalisation** — CDP agrees with §B1 that a
blur must not be a volume knob.

**`BLUR BLUR` — frame/time axis, and it is NOT a mean.** Parameter `blurring` = number of windows.
**[SRC]** `dev/blur/ap_blur.c:773-800`, `do_the_bltr`:

```c
ampdif[cc]  = (thisWindow[AMPP] - dz->amp[cc]) / (float)blurfactor;
freqdif[cc] = (thisWindow[FREQ] - dz->freq[cc]) / (float)blurfactor;
...
dz->flbufptr[1][AMPP] = dz->amp[cc]  + ((float)j * ampdif[cc]);
dz->flbufptr[1][FREQ] = dz->freq[cc] + ((float)j * freqdif[cc]);
```

**It decimates the STFT by `blurfactor` and linearly re-interpolates between the surviving frames** —
amplitude *and* frequency. That is a *ramp*, not an average. The CDP manual's own words: "does not
interpolate continuously over all windows in between, just between the data in the start and end
windows." Effect: temporal detail below `blurfactor` frames is replaced by straight lines. Transients
become ramps.

Other BLUR functions with real parameter ranges, from the CDP docs (`composersdesktop.com/docs/html/cblur.htm`):

| function | parameter | range |
|---|---|---|
| `BLUR CHORUS` | `aspread` (random amplitude scatter) | 1–1028 |
| `BLUR CHORUS` | `fspread` (random frequency scatter) | 1–4 |
| `BLUR NOISE` | `noise` | 0 (none) … 1 (spectrum saturated) |
| `BLUR SCATTER` | `keep` (blocks kept per window) | 1 … n_channels |
| `BLUR DRUNK` | `range` (max step, windows) | ≤ 64 |
| `BLUR SUPPRESS` | `N` components rejected | integer |
| `CALTRAIN` | `blurabov` (freq above which blurring occurs) | 0 … SR/2 Hz |

## B5 — How this differs from Terrain's `renderBlend` (a FRAME-axis mean)

Terrain's existing `renderBlend` (`Wavetable.h:353-441`, transcribed in `00-INVENTORY.md` §3) is a
**Gaussian-weighted convex combination of neighbouring FRAMES at the same mip**:
`σ = 1e-4 + N·1.05·blur^1.25`, weights summing to `blur`, bilinear `ref` carrying `(1−blur)`,
total weight exactly 1, then RMS-matched.

The two are orthogonal and produce completely different sounds:

| | frame axis (`renderBlend`) | frequency axis (`Smear`, `specavrg`) |
|---|---|---|
| operates on | phasors (complex) | magnitudes only |
| can it cancel? | **yes** — it is a weighted phasor average, so `|Σ w·X| ≤ Σ w·|X|`. Confirmed subtractive-only in `00-INVENTORY.md` §3. Two frames in antiphase → silence. | **no** — averaging non-negative magnitudes can never null. |
| what it destroys | the *morph* — frame-to-frame contrast | the *timbre* — harmonic-to-harmonic contrast |
| at max | near-uniform mean of the whole table ("one static average wave") | the spectrum becomes its own envelope ("one formant, no partials") |
| CDP name | `BLUR BLUR` / `BLUR AVRG` across windows | `BLUR AVRG` across channels |

**[INFERRED] The gap in Terrain's inventory:** there is a frame-axis blur (`renderBlend`) and a
frequency-axis blur (`Smear`), but the frequency-axis one is welded into a 3-in-1 mode that also
low-passes and scatters phase. A standalone `Blur` (frequency axis, magnitude only, one knob = kernel
width, energy-renormalised) would be a distinct, honest device and it is 1.6–8.4 µs/frame.

---

# §C — SPECTRAL FREEZE and phase-vocoder smear

This section exists because the phase-vocoder literature is where "smear" was invented, and because
**most of it does not transfer to a single-cycle table.** I will be explicit about what does.

## C1 — The machine, with real numbers

Analysis: STFT with window `w`, size `N_fft`, hop `R_a`; synthesis hop `R_s`; stretch `α = R_s/R_a`.

Standard settings in the literature and in shipping tools:

| setting | typical values | source |
|---|---|---|
| `N_fft` | 1024, 2048, 4096 | CDP `pvoc` default 1024; Terrain's `SpectrumAnalyzer.h` uses 4096 |
| window | Hann (COLA at 75 % overlap), Hamming | Portnoff 1976 [PAPER] |
| overlap | **4×** (`R_a = N/4`) standard; 8× for high-quality stretch | Laroche & Dolson 1999 [PAPER] |
| bin spacing | `SR/N_fft` — 43.1 Hz at N=1024, 10.8 Hz at N=4096, SR=44.1 k | |
| frame rate | `SR/R_a` — 172 Hz at N=1024/×4 | |

Analysis phase advance for bin `k` between frames:

```
Δφ_k = ∠X_i[k] − ∠X_{i−1}[k]
ω_k  = 2πk/N                                  (bin centre frequency, rad/sample)
Δφ_k^wrapped = princarg( Δφ_k − R_a·ω_k )     (heterodyned, principal value in (−π,π])
ω̂_k = ω_k + Δφ_k^wrapped / R_a               (instantaneous frequency estimate)
∠Y_i[k] = ∠Y_{i−1}[k] + R_s·ω̂_k              (synthesis phase propagation)
```

Foundational citations:
- Flanagan & Golden, *"Phase Vocoder"*, Bell System Technical Journal 45(9), 1966, 1493–1509. [PAPER]
- Portnoff, *"Implementation of the digital phase vocoder using the fast Fourier transform"*,
  IEEE Trans. ASSP 24(3), 1976, 243–248. [PAPER]
- Dolson, *"The Phase Vocoder: A Tutorial"*, Computer Music Journal 10(4), 1986, 14–27. [PAPER]

## C2 — Freeze, and magnitude smoothing: time axis vs frequency axis

**Freeze** = stop advancing the analysis frame, keep advancing the synthesis phase:

```
|Y_i[k]| = |X_f[k]|                  (magnitude frozen at frame f)
∠Y_i[k] = ∠Y_{i−1}[k] + R_s·ω̂_f[k]  (phase keeps running at the frozen instantaneous freq)
```

Freezing the phase too gives a buzzy, static, "one-frame-on-loop" artefact — the freeze must keep
the phase advancing or it is not a freeze, it is a loop.

**Magnitude smoothing over TIME** (one-pole along the frame index — the phase-vocoder analogue of
CDP's `BLUR BLUR`, and the analogue of Terrain's `renderBlend`):

```
M_i[k] = (1−λ)·|X_i[k]| + λ·M_{i−1}[k]      λ ∈ [0,1)
τ_frames = 1/ln(1/λ)      τ_seconds = τ_frames · R_a/SR
```

At `N=1024, R_a=256, SR=44.1k` (frame period 5.8 ms): `λ = 0.9` → τ = 9.5 frames = **55 ms**;
`λ = 0.99` → τ = 99.5 frames = **578 ms**. Those are the two ends of "shimmer" and "pad freeze".
[INFERRED — the formula is standard; the numbers are my arithmetic on standard STFT settings.]

**Magnitude smoothing over FREQUENCY** is §B, applied per frame. The two commute and sound nothing
alike: time-smoothing kills transients and keeps timbre; frequency-smoothing kills timbre and keeps
transients.

## C3 — Phasiness, and the two phase-locking schemes

The problem: propagating each bin's phase independently destroys the *relative* phase between the
2–4 bins that one sinusoid occupies. The partial's bins drift apart, the resynthesised sinusoid
loses coherence, and you get the reverberant, chorused, "phasey" sound. Laroche & Dolson named and
diagnosed it.

- Laroche & Dolson, *"Phase-vocoder: about this phasiness business"*, IEEE ASSP Workshop on
  Applications of Signal Processing to Audio and Acoustics (WASPAA), Mohonk, 1997. [PAPER]
- **Laroche & Dolson, *"Improved Phase Vocoder Time-Scale Modification of Audio"*, IEEE Trans.
  Speech and Audio Processing, vol. 7, no. 3, May 1999, pp. 323–332.** [PAPER] — the one the brief asks for.

### Identity phase locking (Laroche & Dolson 1999)

Cheapest fix, and the one nearly everything ships. Find the spectral **peaks**; propagate the phase
**only at the peak bins**; every bin in a peak's *region of influence* is then given a phase that
preserves its original offset from that peak:

```
for each peak bin p (a bin whose |X| exceeds its 2 nearest neighbours on each side):
    ∠Y_i[p] = ∠Y_{i−1}[p] + R_s·ω̂_p                 (normal propagation, peaks only)
for each bin k in the region of influence of p:
    ∠Y_i[k] = ∠Y_i[p] + ( ∠X_i[k] − ∠X_i[p] )        (identity: keep the analysis offset)
```

Equivalently, as a phasor rotation applied to the *whole region* at once:

```
Z_i[k] = X_i[k] · e^{ j( ∠Y_i[p] − ∠X_i[p] ) }        for all k in region(p)
```

"Region of influence" in the 1999 paper is the bins between adjacent peaks (split at the minimum, or
at the midpoint). "Identity" because within the region the *rotation is identical* for every bin.
Cost: one `atan2` per peak instead of per bin, plus one complex multiply per bin.

### Scaled phase locking (Laroche & Dolson 1999)

For time-scale factor `α`, the peak's *neighbours* should have their offsets scaled, not copied,
because a stretched partial's sidelobe phases don't stay put:

```
∠Y_i[k] = ∠Y_i[p] + β·( ∠X_i[k] − ∠X_i[p] ) ,     β ≈ 2/3 + α/3
```

`β = 1` recovers identity locking (and `α = 1` gives `β = 1`, consistent). The paper reports scaled
locking as the better-sounding of the two for large stretches.

### Loose phase locking (Puckette 1995)

- Puckette, M., *"Phase-locked Vocoder"*, IEEE ASSP Workshop on Applications of Signal Processing to
  Audio and Acoustics, Mohonk NY, 1995. [PAPER]

No peak-picking at all — just steer each bin toward its neighbours before taking the phase.
The paper's Eq. 8 and 9, verbatim from the mirrored text at `users.iem.at/zmoelnig/publications/phaslock/`:

```
Z[u(i−1),k] = Y[u(i−1),k] − μ·Y[u(i−1),k−1] − μ·Y[u(i−1),k+1]              (Eq. 8)

Y[u(i),k] = X[t(i),k] · ( Z[u(i−1),k]/X[s(i),k] ) / | Z[u(i−1),k]/X[s(i),k] |   (Eq. 9)
```

i.e. take the previous synthesis frame, subtract `μ` × each neighbour, and use the **phase of that
combination** (the second line normalises to unit modulus and re-imposes `|X|`). The negative sign
is correct and is the point: adjacent bins of a Hann-windowed sinusoid are in *antiphase*, so
`−(neighbours)` adds constructively. The paper notes that for `μ > 1` the exact value has little
audible effect. **The document does not state the window type, FFT size, or hop used in the
examples** — I checked, it is not there. `μ = 1` is the usual shipped choice. [INFERRED]

Cost: **no peak-picking, no `atan2` per bin** — 2 complex multiply-adds and one normalisation per bin.
This is the cheap one and it is why it stayed popular.

Related, for completeness:
- Röbel, A., *"A new approach to transient processing in the phase vocoder"*, DAFx-03, London —
  reset phases at detected transients so locking doesn't smear the attack. [PAPER]

## C4 — What of this survives into Terrain's wavetable path — honestly

| technique | transfers? | why |
|---|---|---|
| identity / scaled / loose phase locking | **No.** | They exist to repair an *estimate*. A single-cycle table has no estimate to repair: every partial occupies exactly one bin, exactly. There is no "region of influence" and nothing to lock. Implementing them would be a no-op at best. |
| magnitude smoothing over frequency | **Yes** — that is §B, verbatim. | |
| magnitude smoothing over time | **Yes, as the FRAME axis** — that is `renderBlend`. | The frame index *is* the time axis of a wavetable. |
| freeze | **Yes, but it is trivially the existing frame lock.** | Freezing frame `f` = not modulating the frame parameter. Nothing to build. |
| the *phasiness sound itself* | **Yes, and this is the interesting one.** | Phasiness is a partial whose components have drifted apart in phase — which, on a harmonic grid, you get by deliberately scattering the phases. §D7. |

**[INFERRED] So the honest summary is: the phase vocoder contributes one thing to a wavetable synth,
and it is the vocabulary, not the algorithms.** Anyone proposing "identity phase locking" for a
2048-sample single cycle has not understood what it is for. Say so in the design doc.

---

# §D — HARMONIC / SPECTRAL TECHNIQUES suited to a 2048-sample single cycle

All of these are **per-bake, O(H)**, and cost 0.4–1.8 µs per 512-harmonic frame (§E). The iFFT
dominates all of them by two orders of magnitude.

## D1 — Harmonic stretching

```
h' = 1 + (h − 1)·s          "stretch about the fundamental" (fundamental stays put)
h' = h·s                    "scale" (everything moves, pitch changes)
```

The stretched index is fractional, so the energy must be **split across the two neighbouring integer
bins** or the morph becomes a sequence of jumps:

```
d = floor(h'),   t = h' − d
X'[d]   += (1−t)·A_h · e^{jφ_h}
X'[d+1] +=    t ·A_h · e^{jφ_h}
```

This is exactly what Vital does **[SRC]** `spectral_morph.h:349-387`, `harmonicScaleMorph`:

```cpp
float shifted_index = std::max(1.0f, (i - 1) * shift + 1);          // h' = 1 + (h−1)·s
int dest_index = shifted_index;  float t = shifted_index - dest_index;
float amplitude1 = (1.0f - t) * amplitude;   float amplitude2 = t * amplitude;
wave_start[real_index1] += amplitude1 * real_amount;   // linear splat, 2 bins
```

with `memset` of the destination first (line 353) — it is a *scatter*, so the target must be cleared.

**Range** **[SRC]** `synth_oscillator.cpp:1089-1090`, `kMaxHarmonicScale = 4.0f` at `spectral_morph.h:26`:

```cpp
setPowerDistortionValues(values, num_values, kMaxHarmonicScale, spread);
// = pow(2, (a − 0.5) · 2 · exponent)
s = 2^(8a − 4)      a ∈ [0,1]   ⇒   s ∈ [1/16, 16],  s = 1 at a = 0.5 (bipolar)
```

**±4 octaves of harmonic spacing, exponential in the knob.** Terrain's `HarmonicStretch`
(`s = 1 + 5.5a`, unipolar, linear) covers 1…6.5× — a narrower, one-sided, linearly-mapped range.
Vital's exponential map is the better musical law (a stretch factor is a ratio; ratios want log knobs).

**Cost [MEAS `specbench2.cpp`]: 0.36 µs / 512 harmonics** (the cheapest operation measured — it is
one multiply, one floor, two multiply-adds per harmonic).

## D2 — Inharmonic stretching, and the physical anchor

The physical law for a stiff string (piano) is:

```
f_n = n·f₀·√(1 + B·n²)
```

`B` is the inharmonicity coefficient. Real values: **B ≈ 1e-4 for the mid range of a grand piano**,
rising to ~1e-3 in the top octaves and ~2e-4 to 1e-3 in the bass of a short piano.
(Fletcher & Rossing, *The Physics of Musical Instruments*, 2nd ed., Springer 1998, ch. 12. [PAPER])
Cite this when you want "bell/metallic" to be *defensible* rather than arbitrary.

Vital's inharmonic law is different — a power-law in `log h`
**[SRC]** `spectral_morph.h:389-441`, `inharmonicScaleMorph`:

```cpp
poly_float octave = futils::log2(index);
poly_float power  = octave * (1.0f / (Wavetable::kFrequencyBins - 1.0f));   // kFrequencyBins = 11
poly_float shift  = futils::pow(mult, power);
poly_float shifted_index = utils::max(1.0f, shift * (index - 1.0f) + 1.0f);
```

i.e. `h' = 1 + (h−1)·m^{log₂(h)/10}` — the stretch factor itself grows with octave, so it is a
*progressive* stretch. Range **[SRC]** `synth_oscillator.cpp:1092-1093`, `kMaxInharmonicScale = 12.0f`:
`m = 2^(24a − 12) ∈ [2⁻¹², 2¹²]`, again bipolar with unity at `a = 0.5`.

Terrain's `InharmonicStretch` is `r' = r^p, p = 1 + 2.3a` — a pure power law. All three are
legitimate; the power law is the simplest and the piano law is the only one with a physical claim.

## D3 — Odd/even weighting

```
A'_h = A_h · w_odd    (h odd)
A'_h = A_h · w_even   (h even)
```

with the natural one-knob parameterisation `x ∈ [0,1]`: `w_odd = 1`, `w_even = 1 − x`
(x = 1 ⇒ square-wave-like, odd only).

[MEAS `harm.py`] On a 256-harmonic saw:

| x | even gain | RMS change | crest |
|---|---|---|---|
| 0.00 | 0.00 dB | 0.00 dB | 6.761 |
| 0.25 | −2.50 dB | −0.50 dB | 6.369 |
| 0.50 | −6.02 dB | −0.90 dB | 5.836 |
| 0.75 | −12.04 dB | −1.16 dB | 5.154 |
| 1.00 | −∞ | **−1.25 dB** | 4.342 |

Note the total RMS cost of removing **every even harmonic from a saw is only 1.25 dB** (because a
saw's even harmonics carry `Σ1/(2k)² = π²/24` of `Σ1/h² = π²/6`, i.e. a quarter of the power).
So this is a strong *timbral* move that is almost free in level — a good knob.

Vital does this in the wavetable editor as opposed phase ramps rather than gains
(`PhaseModifier::kEvenOdd`, §A4) — a different and complementary idea.

## D4 — Spectral tilt

```
A'_h = A_h · h^(−t)      ⇒   adds exactly  −6.0206·t  dB/octave
```

[MEAS `harm.py`]:

| t | slope added | total span, h=1…256 |
|---|---|---|
| 0.5 | −3.01 dB/oct | −24.08 dB |
| 1.0 | −6.02 dB/oct | −48.16 dB |
| 2.0 | −12.04 dB/oct | −96.33 dB |

**Range: `t ∈ [−1, +2]`** covers +6 dB/oct (a differentiator — saw → square-ish brightness) to
−12 dB/oct (very dark). Bipolar knob, `t = 0` at centre. [INFERRED, but the arithmetic above is exact.]
A tilt is the cheapest possible morph (one `pow` per harmonic, or a running multiply) and it is the
one modulation destination that will *always* read as "the sound changed" on a scope and a spectrum.

Terrain already uses tilt implicitly inside `HarmonicStretch` (`amp × r'^(1.15a)`) and
`InharmonicStretch` (`amp × r'^(1.05a)`) — it is welded to the stretch. As a standalone it is a
better modulation target. [INFERRED]

## D5 — Formant shifting on a single cycle

**The idea:** move the *envelope* of the harmonic amplitudes without moving the harmonics. Pitch
unchanged, character changed. On a single cycle:

```
env(h) = the smooth curve through the A_h        (see below for how to get it)
A'_h   = env(h / σ) · (A_h / env(h))             σ = formant shift factor
```

or, if you are happy to discard the fine structure, simply `A'_h = env(h/σ)`.

[MEAS `harm2.py`] A saw with a Gaussian formant bump at h=30, envelope-resampled:

| σ | bump lands at | predicted |
|---|---|---|
| 0.50 | h = 15 | 15.0 |
| 1.00 | h = 30 | 30.0 |
| 2.00 | h = 59 | 60.0 |
| 4.00 | h = 117 | 120.0 |

Exact to within the 1-bin quantisation of the detector. Note the RMS side-effect measured in
`harm.py`: shifting a formant *up* on a `1/h` background **raises** RMS (+2.19 dB at σ=2, +4.73 dB at
σ=4) because the bump now multiplies larger-index bins against a shallower part of the curve.
Renormalise. [MEAS]

**How to get `env(h)`.** Three options, in ascending cost and quality:

1. **Nothing — use a parametric envelope.** Terrain already does this: `Vocode` uses a 5-vowel
   Lorentzian formant bank (`SpectralMorph.h`, `lorentzian` at `Wavetable.h:1489`). Cheapest, and
   the vowel identity is authored rather than estimated. **But Terrain's is hard-wired to
   F0 = 130.81 Hz and does not track the note** (`00-INVENTORY.md` §1) — which means on a single
   cycle it is shifting formants relative to a fictional pitch. Since a single cycle has no absolute
   pitch until it is played, the honest fix is to express the formant centres in **harmonic index**
   (as a function of the played note), not Hz. [INFERRED]
2. **Cepstral / "true envelope".** Röbel & Rodet, *"Efficient Spectral Envelope Estimation and its
   application to pitch shifting and envelope preservation"*, DAFx-05, Madrid. [PAPER] Iterative:
   take `log|X|`, low-quefrency-liftered cepstrum → envelope, then `max(log|X|, envelope)` and
   repeat. The paper's contributions are (a) sub-sampling the log-amplitude spectrum and step-size
   control for a **2.5–11× speedup**, and (b) using a **Hamming window rather than a rectangular
   one as the cepstral smoothing filter**, to stop the envelope ringing. Both matter if you build it.
3. **Vital's shortcut: don't estimate an envelope at all, resample the harmonic array with parity
   preserved.** **[SRC]** `spectral_morph.h:307-347`, `evenOddVocodeMorph`:

```cpp
float shifted_index = std::max(1.0f, i * shift);
int index_start = shifted_index;
index_start = index_start - (i + index_start) % 2;      // snap to the SAME PARITY as i
float t = (shifted_index - index_start) * 0.5f;
int real_index1 = 2 * index_start;  int real_index2 = real_index1 + 4;   // step of 2 harmonics
wave_start[real_index]     = shift * utils::interpolate(real_from, real_to, t);
wave_start[real_index + 1] = shift * utils::interpolate(imag_from, imag_to, t);
```

Read: harmonic `i` is filled from the source at `i·shift`, **rounded to the nearest harmonic of the
same parity**, interpolating between harmonics `index_start` and `index_start+2`. Preserving parity
is the trick — it is what stops a formant shift from turning a square wave (odd only) into a saw.
The `shift *` on the output is a gain compensation for the resampling density.

Vital exposes this twice with two ranges **[SRC]** `synth_oscillator.cpp:1083-1088`, constants at
`spectral_morph.h:24-25`:

| mode | constant | `shift = 2^(−2·exponent·(a−0.5))` | range |
|---|---|---|---|
| `kVocode` | `kMaxFormantShift = 1.0` | `2^(1−2a)` | ×2 … ×0.5 (**±1 octave**) |
| `kFormScale` | `kMaxEvenOddFormantShift = 2.0` | `2^(2−4a)` | ×4 … ×0.25 (**±2 octaves**) |

Both bipolar, unity at `a = 0.5`. `kVocode` additionally multiplies the shift by
`voice_increment · 2048` at call time (`formant_shift = true`
**[SRC]** `synth_oscillator.cpp:740-741` and `computeSpectralWaveBufferPair` line ~96) — i.e.
**Vocode's formant shift is scaled by the played frequency, so the formants stay at a fixed
absolute Hz as you play up the keyboard.** That is the missing piece in Terrain's `Vocode`.

**Cost: per-bake, O(H).** One interpolation per harmonic. Cheaper than the blur.

## D6 — Random amplitudes with a frozen seed

**The requirement is determinism**: same preset ⇒ same table ⇒ same sound, forever, across machines.
That means a **fixed seed and a fixed generator**, and — importantly — a **pre-computed table**, not
a call to `rand()` inside the morph.

Vital's construction is worth copying wholesale **[SRC]** `src/synthesis/producers/synth_oscillator.h:34-56`:

```cpp
class RandomValues {
  static constexpr int kSeed = 0x4;
  static RandomValues* instance() {
    int size = (kRandomAmplitudeStages + 1) * (Wavetable::kNumHarmonics + 1) / poly_float::kSize;
    static RandomValues instance(size);           // built once, process-wide
    return &instance;
  }
  RandomValues(int num_poly_floats) {
    data_ = std::make_unique<poly_float[]>(num_poly_floats);
    utils::RandomGenerator generator(-1.0f, 1.0f);
    generator.seed(kSeed);                        // FROZEN
    for (int i = 0; i < num_poly_floats; ++i) data_[i] = generator.polyNext();
  }
};
```

`kRandomAmplitudeStages = 16` **[SRC]** `spectral_morph.h:30`, `kNumHarmonics = 1025`, so the table is
`17 × 1026 / 4 ≈ 4360` poly_floats ≈ **69.8 kB**. Uniform on `[−1, +1]`. Built once, immutable.

Then the morph **cross-fades between 16 independent random draws** as the knob turns
**[SRC]** `spectral_morph.h:443-477`:

```cpp
int index = std::min<int>(shift, kRandomAmplitudeStages - 2);   // stage
float t = shift - index;                                        // fraction into next stage
poly_float scale  = shift;
poly_float center = poly_float(1.0f) - scale;
poly_float mult   = 1.0f + shift;
...
poly_float random1 = mult * utils::max(center - scale * random_value1, 0.0f);
poly_float random2 = mult * utils::max(center - scale * random_value2, 0.0f);
poly_float amplitude = utils::min(utils::interpolate(random1, random2, t) * frequency_amplitudes[i], 1024.0f);
```

As maths, with `r ∈ [−1,1]` the frozen random value and `s` the amount:

```
g(r,s) = (1 + s) · max( (1 − s) − s·r , 0 )        clamped so |X'| ≤ 1024
A'_h = g(r_h, s) · A_h
```

The parameter **[SRC]** `synth_oscillator.cpp:1101-1104`: `s = a · (kRandomAmplitudeStages − 1) = 15a`,
so `s ∈ [0, 15]` and `a` walks through 15 cross-faded random landscapes.

**Two properties Terrain's `RandomAmplitudes` does not have, and should:**

1. **`g(r, 0) = 1·max(1 − 0, 0) = 1` exactly.** At `s = 0` the morph is a **perfect identity** — no
   step, no discontinuity, for any `r`. This is designed. Terrain's `RandomAmplitudes` uses
   `mul = 0.02 + r²·5.5` which is ≠ 1 at any `r`, so it *cannot* be continuous at amount → 0 —
   and this is one half of the 🚨 step-at-`amount 0+` finding in `00-INVENTORY.md`
   (the other half being the `kMaxPartials = 96` truncation, which fires the instant `apply()` stops
   short-circuiting). **Fix the mode to be an identity at amount 0 and the step is half solved.**
2. **The randomness itself evolves with the knob.** With 16 stages cross-faded, turning the knob does
   not just deepen one fixed pattern, it *travels* through patterns. That is why Vital's Random
   Amplitudes is a performable knob and a single-draw version is not.
3. `max(…, 0)` means bins are **hard-zeroed** once `s·r > 1 − s`. At `s = 0.5` that is `r > 1`, never;
   at `s = 5` it is `r > −0.8`, i.e. ~90 % of bins get killed. The `(1+s)` makeup partly compensates.

**Cost: per-bake, O(H), and the RNG cost is ZERO** — it is a table lookup. This is the whole point
of the frozen-table design: determinism *and* speed.

## D7 — Random phase with a frozen seed — and how to parameterise it

Same frozen-table discipline. But **the obvious parameterisation is the wrong one**, and I measured it.

Three candidate laws for "amount `a` → phase scatter", on a 256-harmonic saw
[MEAS — `harm2.py`, crest factor, mean of 48 seeds for the random ones]:

| a | **A**: randomise only `h > (1−a)·H`, `U[0,2π)` | **B**: every `h` gets `U[−πa, +πa)` | **C**: deterministic `φ_h = −a·π·h²/H` |
|---|---|---|---|
| 0.00 | 6.761 | 6.761 | 6.761 |
| 0.10 | 6.645 | 6.651 | **4.715** |
| 0.25 | 6.445 | 6.089 | 4.215 |
| 0.50 | 5.992 | 4.309 | 3.833 |
| 0.75 | 5.253 | 2.508 | 3.609 |
| 1.00 | 2.365 | 2.365 | 3.470 |

**Law A — "amount scales the fraction of bins affected" — is a dead knob.** From 0 to 0.75 it moves
the crest by 1.5 out of 4.4 (34 % of its travel across 75 % of the knob), then does everything in the
last quarter. The reason is structural: a `1/h` spectrum's peak is built by the *low* harmonics, and
law A leaves them alone until the very end. Anyone who writes "phase is randomised uniformly over
[0,2π) per bin above bin N, amount scales the fraction of bins affected" has specified a knob that
does nothing for three quarters of its travel — **on a `1/h` spectrum**. On a flat spectrum it would
behave better. State the spectrum or the claim is meaningless.

**Law B — "amount scales the phase deviation, all bins" — is smooth and monotone.** This is the one
to ship:

```
φ'_h = φ_h + a · π · r_h ,     r_h ∈ [−1, 1) from the frozen table
```

**Law C — Schroeder-scaled** is deterministic (no table at all), does 70 % of its work in the first
10 % of the knob, and levels off at a *higher* crest than the random laws. It is a different
character: coherent smear (§A) rather than incoherent scatter.

**[INFERRED] Ship B for "scatter" and A2/C for "disperse", and do not merge them into one knob.**
Terrain's current `Smear` welds a low-pass, a triangular blur, *and* `phase scatter a·4.1` into one
control — three mechanisms with three different curves sharing one knob is exactly why an
"amount" ends up feeling non-linear.

Note on the range: `a·4.1` radians in Terrain's `Smear` exceeds `π`, so at full amount the scatter
is already wrapping — beyond `a·π` the knob only re-randomises, it does not scatter *more*.
`a·π` is the saturation point. [INFERRED, follows from `φ` being an angle.]

## D8 — Two more from Vital worth knowing about

**`wavetableSkewMorph` — per-harmonic frame skew** **[SRC]** `spectral_morph.h:132-178`. Instead of
every harmonic reading the same frame, harmonic `h` reads a frame offset by `shift·log₂(h)/11`, with
a triangular wrap so it never runs off the table:

```cpp
float shift_scale = futils::log2(i) / Wavetable::kFrequencyBins;          // /11
poly_float base_value = 1.0f - utils::mod((base_wavetable_t + shift*shift_scale) * 0.5f) * 2.0f;
float shifted_index = (1.0f - fabs(base_value)) * max_frame;              // triangle wrap
```

Parameter `shift = a²·kSkewScale` with `kSkewScale = 16.0f` **[SRC]** `synth_oscillator.cpp:1109-1112`
and `spectral_morph.h:32` — squared knob, 0…16 frames of skew per octave. **This is a genuinely
different kind of smear: it smears along the FRAME axis but with a frequency-dependent offset**, so
the low harmonics show you frame 40 while the high harmonics show you frame 90. Terrain has nothing
like it and `renderBlend` cannot produce it (a convex combination of frames is frequency-flat by
construction).

**`shepardMorph`** **[SRC]** `spectral_morph.h:71-130` — crossfades the table against a copy of itself
with every harmonic moved to `2h` (odd bins zeroed), i.e. an octave-up Shepard illusion, with a
phase-continuity guard: if the two amplitudes are within a factor of `kMinAmplitudeRatio = 2.0`
(with `kMinAmplitudeAdd = 0.001` to avoid divide-by-zero), it interpolates the **phase angles**
(unwrapped) rather than the phasors — precisely the §A4 lesson (don't lerp phasors when you mean to
lerp phase). The same harmonic-doubling shows up offline in
`ShepardToneSource::render` **[SRC]** `src/common/wavetable/shepard_tone_source.cpp:34-37`:

```cpp
loop_wave_frame->frequency_domain[i * 2]     = key_wave_frame->frequency_domain[i];
loop_wave_frame->frequency_domain[i * 2 + 1] = 0.0f;
```

**And one more from the wavetable editor, `FrequencyFilterModifier`**
**[SRC]** `src/common/wavetable/frequency_filter_modifier.cpp:96-113` — the reference for "cutoff as a
harmonic index", with real numbers:

```cpp
float cutoff_index = std::pow(2.0f, cutoff_);              // cutoff_ default 4.0 ⇒ bin 16
float slope  = 1.0f / interpolate(1.0f, kMaxSlopeReach, shape_*shape_);   // kMaxSlopeReach = 128
float power  = interpolate(kMinPower, kMaxPower, shape_);                 // −9 … +9
// low  : clamp(1 − slope·(h − cutoff_index), 0, 1)
// band : clamp(1 − |slope·(h − cutoff_index)|, 0, 1)
// high : clamp(1 + slope·(h − cutoff_index), 0, 1)
// comb : combWave(h / (2·cutoff_index), power)
```

`cutoff_` is in **octaves of harmonic index** (`h = 2^cutoff`), `shape_ ∈ [0,1]` controls the slope
via `1/lerp(1, 128, shape²)` — i.e. the transition spans 1 to 128 harmonics. `normalize_` defaults
true and re-normalises the time-domain peak after filtering (lines 77-80). Terrain's `Smear`
low-pass (`1/(1+(r/cut)⁴)`, `cut = 3 + (1−a)²·92`) is the same idea with a Butterworth-ish shape
instead of a linear ramp; Vital's is cheaper, Terrain's is smoother.

---

# §E — COST, measured on this machine today

All figures from `specbench.cpp` / `specbench2.cpp`, `clang++ -O2`, this Mac.

### The FFT is everything. **[MEAS]**

| operation | µs |
|---|---|
| 2048-point real inverse FFT, **vDSP / Accelerate** | **0.92** |
| 2048-point complex inverse FFT, **naive radix-2** (the shape Terrain hand-rolls) | **33.65** |
| ratio | **36.4× slower** |

Project that onto Terrain's bake (34 mips × 16 frames = 544 iFFTs, `00-INVENTORY.md` finding #2):

| | total |
|---|---|
| 544 × naive radix-2 | **18.3 ms** |
| 544 × vDSP | **0.50 ms** |
| 16 × vDSP (single mip) | **0.015 ms** |

**Terrain's measured bake is 20.8–23.1 ms** (`00-INVENTORY.md` finding #2). My naive-FFT projection
is 18.3 ms. **The bake cost is the hand-rolled FFT and essentially nothing else.** Moving
`Wavetable::inverseFFT` onto `vDSP_fft_zrip` (or `juce::dsp::FFT`, which uses vDSP on Apple) would
take the bake from ~21 ms to **under 1 ms**, i.e. from ~40 % message-thread duty per osc at 20 Hz to
under 2 %. That single change dwarfs every algorithmic choice in this document.
(Related: `00-INVENTORY.md` finding #4 — Terrain currently ships **three** hand-rolled radix-2 FFTs
with three normalisation conventions. One vDSP wrapper replaces all three.)

### The morph maths is free by comparison. **[MEAS]** H = 512 harmonics, one frame:

| operation | µs / frame | µs / 16-frame spec | class |
|---|---|---|---|
| harmonic stretch (2-bin splat) | 0.36 | 5.7 | O(H) |
| triangular blur, W=1 | 0.37 | 6.0 | O(H·W) |
| one-pole magnitude smear (any width) | 1.60 | 25.7 | O(H) |
| phase rotation (`sinf`+`cosf` per harmonic) | 1.77 | 28.3 | O(H) |
| triangular blur, W=11 | 8.36 | 133.7 | O(H·W) |
| triangular blur, W=64 | 60.38 | 966.1 | O(H·W) |

Everything except a wide triangular blur is **under 30 µs for a whole 16-frame spec** — 0.15 % of
the current bake. Even the W=64 blur is 1 ms, i.e. 5 % of the current bake.

### Cost classes, summarised

| technique | class | verdict for Terrain |
|---|---|---|
| M-section allpass cascade | **per sample, per voice** | Audio-thread only. FX rack, not the oscillator. |
| quadratic phase (dispersion) | **per bake, O(H)** | 28 µs/spec. Ship it. |
| frequency blur, triangular W | **per bake, O(H·W)** | ≤ 134 µs at W=11. Ship it. |
| frequency blur, one-pole | **per bake, O(H)** | 26 µs, any width. Ship it if wide blur is wanted. |
| harmonic stretch / tilt / odd-even / random amp / random phase | **per bake, O(H)** | 6–30 µs. All free. |
| formant shift (parametric or Vital-style resample) | **per bake, O(H)** | Free. |
| formant shift (true-envelope cepstral) | **per bake, O(N log N) × iterations** | 2 extra FFTs + iterations. Would roughly double a *single-mip* bake; irrelevant against a 34-mip one. Needs a real measurement before shipping. |
| the iFFT ladder | **per bake, 544 × 33.65 µs** | **This is the whole cost. Fix it first.** |

### One structural note on Vital's cost model, for contrast

Vital does **not** bake a mip ladder. It runs the morph + **one** 2048-point inverse FFT
**on the audio thread**, per unison voice-pair, every `kWavetableFadeTime = 0.007f` seconds
**[SRC]** `synth_oscillator.cpp:47` and `1398` — i.e. **~143 rebuilds/second per voice pair** — and
cross-fades old↔new across that 7 ms window. Band-limiting is not a ladder; it is a
**per-rebuild bin cutoff computed from the current phase increment**
**[SRC]** `synth_oscillator.cpp` in `computeSpectralWaveBufferPair`, ~lines 100-102:

```cpp
float bin_shift = Wavetable::kFrequencyBins + 1.0f - bin;                 // kFrequencyBins = 11
int last_harmonic = std::max<int>(0, WaveFrame::kWaveformSize * futils::exp2(-bin_shift));
last_harmonic = std::min(last_harmonic, WaveFrame::kWaveformSize / 2);
```

Every morph function then zeroes bins above `last_harmonic` before the transform. **That is why
Vital needs 1 iFFT where Terrain needs 34: the band-limit is a zeroing inside the same transform,
not a separate table.** [SRC + INFERRED conclusion.] This is the second-largest architectural
observation in this document after §E's FFT finding, and it also explains why Vital can afford
per-voice, audio-rate spectral morphing at all.

---

# §F — What this literature says Terrain should do (ranked, and honest)

1. **Replace the hand-rolled radix-2 FFT with vDSP/`juce::dsp::FFT`.** [MEAS: 36.4×] Nothing else in
   this document matters as much. It also collapses the three-conventions problem
   (`00-INVENTORY.md` finding #4).
2. **Make every mode an exact identity at `amount = 0`.** Vital's `randomAmplitudeMorph` does this by
   construction (`g(r,0) = 1`, §D6); Terrain's does not, and combined with the
   `kMaxPartials = 96` truncation it produces the measured −8.7 to −25.1 dBr step at `amount 0+`.
   Raise `kMaxPartials` to `kMaxHarmonics` **and** rewrite the multipliers so `f(x, 0) = x`.
3. **Add a real dispersion mode** — §A2, `φ(h) = c·(h − h_c)²` with `h_c = 24`, `c` exponent ~2.5,
   bipolar. It is 28 µs, it is magnitude-exact, it changes the scope dramatically (crest 6.76 → 2.18),
   and Terrain has nothing that does it. This is the highest sound-per-µs item on the list.
4. **Split `Smear` into its three mechanisms.** Low-pass, blur width, phase scatter — three curves,
   one knob is why it feels non-linear. And clamp the scatter at `a·π` (§D7).
5. **Add energy renormalisation to `Smear`** the way `renderBlend` already has it — a blur costs
   4.32 dB RMS at W=11 (§B1) and currently that leaks into the knob.
6. **Give `Vocode` note tracking** — Vital scales the formant shift by `voice_increment · 2048`
   (§D5) so formants stay at fixed Hz; Terrain's F0 = 130.81 Hz is a fixed fiction.
7. **Store magnitude and unit phasor separately** (§0) — it makes every magnitude-only morph a single
   multiply and removes all `atan2` from the morph path.
8. Consider `wavetableSkewMorph` (§D8): a frequency-dependent frame offset is a smear `renderBlend`
   mathematically cannot produce.

---

# §G — Bibliography

**Phase dispersion / allpass**
- Van Duyne, S. A. & Smith, J. O. (1994). *A Simplified Approach to Modeling Dispersion Caused by
  Stiffness in Strings and Plates.* Proc. ICMC 1994, Århus, pp. 407–410.
- Välimäki, V., Abel, J. S. & Smith, J. O. (2009). *Spectral Delay Filters.* JAES 57(7/8), 521–531.
  https://www.aes.org/e-lib/browse.cfm?elib=14832
- Pekonen, J. & Välimäki, V. (2009). *Spectral Delay Filters with Feedback and Time-Varying
  Coefficients.* Proc. DAFx-09, Como.
- Rauhala, J. & Välimäki, V. (2006). *Dispersion Modeling in Waveguide Piano Synthesis Using Tunable
  Allpass Filters.* Proc. DAFx-06, Montreal.
- Abel, J. S., Berners, D. P., Costello, S. & Smith, J. O. (2006). *Spring Reverb Emulation Using
  Dispersive Allpass Filters in a Waveguide Structure.* AES 121st Convention, San Francisco.
- Timoney, J., Lazzarini, V., Pekonen, J. & Välimäki, V. (2009). *Spectrally Rich Phase Distortion
  Sound Synthesis Using an Allpass Filter.* Proc. ICASSP 2009, Taipei, pp. 293–296.
  https://research.aalto.fi/en/publications/spectrally-rich-phase-distortion-sound-synthesis-using-an-allpass/
- Lazzarini, V., Timoney, J. & Pekonen, J. (2009). *Adaptive Phase Distortion Synthesis.* Proc.
  DAFx-09. https://www.dafx.de/paper-archive/2009/papers/paper_12.pdf
- Smith, J. O. *Introduction to Digital Filters with Audio Applications* and *Physical Audio Signal
  Processing*, CCRMA online books. https://ccrma.stanford.edu/~jos/

**Crest factor / phase design**
- Schroeder, M. R. (1970). *Synthesis of low-peak-factor signals and binary sequences with low
  autocorrelation.* IEEE Trans. Information Theory IT-16(1), 85–89.
  https://www.semanticscholar.org/paper/cb75785e6c3efdc3edc2717b6558070e1559d9f2

**Phase vocoder / spectral smear**
- Flanagan, J. L. & Golden, R. M. (1966). *Phase Vocoder.* Bell System Technical Journal 45(9), 1493–1509.
- Portnoff, M. R. (1976). *Implementation of the digital phase vocoder using the fast Fourier
  transform.* IEEE Trans. ASSP 24(3), 243–248.
- Dolson, M. (1986). *The Phase Vocoder: A Tutorial.* Computer Music Journal 10(4), 14–27.
- Puckette, M. (1995). *Phase-locked Vocoder.* Proc. IEEE ASSP Workshop on Applications of Signal
  Processing to Audio and Acoustics, Mohonk NY.
  https://users.iem.at/zmoelnig/publications/phaslock/
- Laroche, J. & Dolson, M. (1997). *Phase-vocoder: about this phasiness business.* Proc. IEEE WASPAA,
  Mohonk NY.
- **Laroche, J. & Dolson, M. (1999). *Improved Phase Vocoder Time-Scale Modification of Audio.*
  IEEE Trans. Speech and Audio Processing 7(3), 323–332.**
  https://www.semanticscholar.org/paper/8312d42cab3f14152d8e6406a9c0463737b6aa45
- Röbel, A. (2003). *A New Approach to Transient Processing in the Phase Vocoder.* Proc. DAFx-03, London.
- Harris, F. J. (1978). *On the use of windows for harmonic analysis with the discrete Fourier
  transform.* Proc. IEEE 66(1), 51–83.

**Spectral envelope / formants**
- Röbel, A. & Rodet, X. (2005). *Efficient Spectral Envelope Estimation and its application to pitch
  shifting and envelope preservation.* Proc. DAFx-05, Madrid. https://hal.science/hal-01161334
- Moulines, E. & Laroche, J. (1995). *Non-parametric techniques for pitch-scale and time-scale
  modification of speech.* Speech Communication 16(2), 175–205.

**Spectral modelling / inharmonicity**
- Serra, X. & Smith, J. O. (1990). *Spectral Modeling Synthesis: A Sound Analysis/Synthesis System
  Based on a Deterministic Plus Stochastic Decomposition.* Computer Music Journal 14(4), 12–24.
- Fletcher, N. H. & Rossing, T. D. (1998). *The Physics of Musical Instruments*, 2nd ed., Springer.
  (Ch. 12 for the piano inharmonicity law `f_n = n f₀ √(1+Bn²)` and measured `B`.)
- Sethares, W. A. (1998). *Tuning, Timbre, Spectrum, Scale.* Springer. (Stretched/inharmonic spectra.)

**Compositional practice / vocabulary**
- Wishart, T. (1994). *Audible Design.* Orpheus the Pantomime. (The source of the CDP blur vocabulary.)
- Roads, C. (2001). *Microsound.* MIT Press.

**Reference implementations read for this document**
- **Vital** — Matt Tytel, GPL-3. https://github.com/mtytel/vital
  - `src/synthesis/producers/spectral_morph.h` (all 10 morphs; lines cited throughout §A3, §B2, §D)
  - `src/synthesis/producers/synth_oscillator.h` (`RandomValues`, the frozen seed, lines 34–56;
    `SpectralMorph` enum, lines 100–113)
  - `src/synthesis/producers/synth_oscillator.cpp` (`setSpectralMorphValues` 1079–1120 = every
    parameter range; `setPowerDistortionValues` 491–501; `kWavetableFadeTime` 47)
  - `src/synthesis/lookups/wave_frame.h/.cpp` (2048/1025, FFT convention)
  - `src/synthesis/lookups/wavetable.h/.cpp` (`kFrequencyBins = 11`, the mag/phasor/phase split 160–177)
  - `src/common/wavetable/phase_modifier.cpp` (offline phase styles; the `mix` comb trap, §A4)
  - `src/common/wavetable/frequency_filter_modifier.cpp` (harmonic-index cutoff, §D8)
  - `src/common/wavetable/shepard_tone_source.cpp` (harmonic doubling)
  - `src/interface/look_and_feel/synth_strings.h:318-330` (the shipped mode names)
- **CDP8** — Composers Desktop Project, https://github.com/ComposersDesktop/CDP8
  - `dev/blur/blur.c` (`specavrg` 94–134 = frequency-axis boxcar; `specspread` 319–340)
  - `dev/blur/ap_blur.c` (`do_the_bltr` 773–800 = frame-axis ramp interpolation)
  - Docs: https://www.composersdesktop.com/docs/html/cblur.htm

---

# §H — What I could NOT establish

Stated plainly, because a confident guess would be worse than this list.

1. **Vital has no manual that documents any of this.** Every Vital number in this file is from source.
   If you want a "the manual says X" citation for Phase Disperse, there isn't one.
2. **Puckette 1995 does not state its window, FFT size, hop size, or a numeric `μ`.** I fetched the
   mirrored text and checked. It says only that for `μ > 1` the exact value has little audible effect.
   `μ = 1` is a convention, not a citation.
3. **I did not verify the Laroche & Dolson 1999 `β = 2/3 + α/3` coefficient against the paper's own
   text** — the full text is paywalled at IEEE. The formula is as it is universally reproduced, and
   it satisfies the necessary sanity check (`α = 1 ⇒ β = 1 ⇒` identity locking). Treat the exact
   constants as **unverified**; the *structure* (`∠Y[k] = ∠Y[p] + β·(∠X[k] − ∠X[p])`) is safe.
4. **I could not fetch the Välimäki/Abel/Smith 2009 "Spectral Delay Filters" full text** (AES
   paywall; the Aalto mirror refused the connection). The allpass formulas in §A1 are textbook and I
   verified them numerically to 1e-15 — but the paper's *own* recommended `a` and `M` for musical use
   are not quoted here because I have not read them.
5. **Terrain's phase convention vs Vital's.** I read both conventions but did not run a
   cross-check that transplants a Vital formula into Terrain and compares waveforms. The `φ → φ − π/2`
   note in §0 is a derivation, not a measurement. **Verify it with a null test before shipping any
   lifted formula.**
6. **True-envelope (Röbel & Rodet) cost on a 2048 single cycle is unmeasured.** I did not implement
   it. The "2 extra FFTs + iterations" class in §E is from the paper's description, not a benchmark.
7. **The `c` exponent of 2.5 in §A3 is eyeballed off the measured crest curve, not fitted.**
8. **Every "sounds like" claim in this document is absent on purpose.** I measured crest factors,
   dB, samples and microseconds. I did not listen to anything.
