# 10 — VITAL: the spectral/wavetable engine, read from source

**Source of truth:** `github.com/mtytel/vital`, commit **`636ca0ef517a4db087a6a08a6a8a5e704e21f836`**
(2022-04-20, the final open-source snapshot; `standalone/vital.jucer` says `version="1.0.6"`).
GPLv3. Every claim below is cited `file:line` against that commit. Where the manual/community
docs disagree with the code, **the code wins** and I say so.

Reproduce: `git clone --depth 1 https://github.com/mtytel/vital.git` then read
`src/synthesis/producers/spectral_morph.h` (478 lines) — that one file **is** the whole
spectral morph system.

> **Honesty markers used below:** `MEASURED` = I ran it. `DERIVED` = closed form I worked out
> from the code and checked numerically. `INFERRED` = my reading, not stated by the code.
> Anything not marked is a direct quote of a constant or an expression from the source.

---

## 0. The headline: Vital's spectral morph is **REAL-TIME, on the audio thread, per voice**

This is the single most important structural fact and it inverts Terrain's whole model.

Vital has **no wavetable mip ladder and no time-domain wavetable in the audio path at all.**
`WavetableData` stores, per frame:

```cpp
// src/synthesis/lookups/wavetable.h:44-47
std::unique_ptr<mono_float[][kWaveformSize]> wave_data;              // TIME domain — UI ONLY
std::unique_ptr<poly_float[][kPolyFrequencySize]> frequency_amplitudes;   // |X[h]|, duplicated re/im
std::unique_ptr<poly_float[][kPolyFrequencySize]> normalized_frequencies; // e^{jφ_h} = (cos φ, sin φ)
std::unique_ptr<poly_float[][kPolyFrequencySize]> phases;                 // φ_h in radians
```

`wave_data` is **only** read by `Wavetable3d` (`src/interface/editor_components/wavetable_3d.cpp:39,77`).
The audio thread reads `frequency_amplitudes` / `normalized_frequencies` / `phases` and
**inverse-FFTs a fresh 2048-point cycle every 7 ms, per unison-voice pair, per voice, forever** —
even when the morph type is `None` (`passthroughMorph` is still an iFFT,
`spectral_morph.h:53-69`, dispatched at `synth_oscillator.cpp:762`).

```
 message thread (WavetableCreator, on edit only)     audio thread (SynthOscillator::process)
 ──────────────────────────────────────────────      ────────────────────────────────────────
 257 × WaveFrame render (sources+modifiers)          setSpectralMorphValues()      ← per BLOCK
   each: toTimeDomain / toFrequencyDomain                  │  curve-maps knob → morph arg
        │  2048-pt FFT pairs                               ▼
        ▼                                            every 7 ms (kWavetableFadeTime = 0.007f):
 Wavetable::loadWaveFrame → |X|, e^{jφ}, φ             ├ pick INTEGER frame index (0..256)
        │                                              ├ compute last_harmonic from the PITCH
        ▼                                              ├ <morph>(data, frame, dest, xform, amt, lh)
 Wavetable::postProcess(max_span)                      ├ transform->transformRealInverse(2048)
   frame-axis PHASE REPAIR (see §4)                    └ swap wave_buffers_[] ptr
        │                                                    │
        ▼                                                    ▼
 current_data_  ──(no double buffer, see §6.4)──────►  per SAMPLE: Catmull-Rom read of the
                                                       2048 buffer + LINEAR CROSSFADE from the
                                                       previous buffer over the 7 ms window
```

**Contrast with Terrain** (`00-INVENTORY.md`): Terrain bakes a 34-mip × 16-frame table on the
message thread at ~20 Hz for ~21 ms of CPU, then the audio thread only does time-domain reads.
Vital does the opposite: nothing is baked per-morph, and the "mip ladder" is replaced by an
**exact per-pitch harmonic truncation** recomputed every 7 ms (§6.2). Vital's cost is
**~3.5 µs MEASURED** per iFFT (§6.5) — cheap enough to sit on the audio thread; Terrain's
21 ms bake is not, and that difference is architectural, not incidental.

---

## 1. The 12 morph types (11 + None), by name

`enum SpectralMorph` — `src/synthesis/producers/synth_oscillator.h:100-113`
Display names — `src/interface/look_and_feel/synth_strings.h:318-331`
UI puts a separator after `None` only — `oscillator_section.cpp:154-156`.

| # | enum | UI name ("Frequency Morph") | implementation | reads whole table? | touches phase? |
|---|---|---|---|---|---|
| 0 | `kNoSpectralMorph` | None | `passthroughMorph` `spectral_morph.h:53` | no | no |
| 1 | `kVocode` | Vocode | `evenOddVocodeMorph` `:307` (`formant_shift=true`) | no | no |
| 2 | `kFormScale` | Formant Scale | `evenOddVocodeMorph` `:307` (`formant_shift=false`) | no | no |
| 3 | `kHarmonicScale` | Harmonic Stretch | `harmonicScaleMorph` `:349` | no | no† |
| 4 | `kInharmonicScale` | Inharmonic Stretch | `inharmonicScaleMorph` `:389` | no | no† |
| 5 | `kSmear` | Smear | `smearMorph` `:217` | no | **no** |
| 6 | `kRandomAmplitudes` | Random Amplitudes | `randomAmplitudeMorph` `:443` | no | **no** |
| 7 | `kLowPass` | Low Pass | `lowPassMorph` `:243` | no | no |
| 8 | `kHighPass` | High Pass | `highPassMorph` `:273` | no | no |
| 9 | `kPhaseDisperse` | Phase Disperse | `phaseMorph` `:180` | no | **ONLY phase** |
| 10 | `kShepardTone` | Shepard Tone | `shepardMorph` `:71` | no (needs a Shepard table) | yes (interpolates φ) |
| 11 | `kSkew` | Spectral Time Skew | `wavetableSkewMorph` `:132` | **YES — all 257 frames** | yes (bilinear on re/im) |

† = the source phasor travels with the partial; overlapping destinations sum as complex
phasors, so cancellation is possible.

**There is no "Vector Morph" in Vital.** (The brief listed it — that is a different synth.)
Vital's vector-ish axis is the *wave frame* parameter, which is a plain 0…256 index, not a morph
mode. Also: `kMaxSplitScale` / `kMaxSplitShift` (`spectral_morph.h:28-29`) are **declared and
referenced nowhere** — a planned "split" mode that never shipped.

### The one parameter pair (identical for all 11)

`src/common/synth_parameters.cpp:510-515`

| | |
|---|---|
| Type | `osc{1,2,3}_spectral_morph_type` — `kIndexed`, 0…`kNumSpectralMorphTypes-1` = **0…11**, default 0. Display name **"Frequency Morph Type"**. Added in version `0x000407` (0.4.7). |
| Amount | `osc{N}_spectral_morph_amount` — `kLinear`, **0.0…1.0, default 0.5**, `display_multiply = 100.0`, units `"%"`. Display name **"Frequency Morph Amount"**. |
| Unison spread | `osc{N}_spectral_morph_spread` — `kLinear`, **−0.5…+0.5, default 0.0**, display ×200 `"%"`. |
| Spectral unison | `osc{N}_spectral_unison` — `kIndexed` 0/1, **default 1 (ON)** (`:490`). |

**Default 0.5, not 0.** Because every curve in `setSpectralMorphValues` is centred so that
**a = 0.5 is the identity** for the ratio-style morphs. Terrain's amount default of 0.0 with an
`amount<=0` short-circuit is what produces the `0 → 1e-6` step documented in `00-INVENTORY.md`;
Vital structurally cannot have that bug because the identity is *inside* the range, and because
the `None` mode runs the same iFFT as every other mode.

### Unison spread + the identity that kills the work

```cpp
// synth_oscillator.cpp:968-1002
for (int v = 0; v < kNumPolyPhase; ++v) {          // kNumPolyPhase = 8
  poly_float t = (v / (utils::imax(2, num_phase_updates) - 1.0f)) * 2.0f;
  last_spectral_morph_values_[v] = spectral_morph_values_[v];
  spectral_morph_values_[v] = spectral_morph_amount + t * morph_spread;
}
if (spectral_morph == kShepardTone)   // WRAPS, does not clamp
  spectral_morph_values_[v] = utils::mod(v * 0.99f) * (1.0f / 0.99f);
else
  spectral_morph_values_[v] = utils::clamp(v, 0.0f, 1.0f);
```

Then `setFourierWaveBuffers` (`:826`) only does per-unison-voice morphs when
`spectral_unison && (morph values differ || frame spread ≠ 0 || type == kVocode)`; otherwise it
computes **one** buffer and aliases all 16 unison pointers at it (`:855-861`). And inside a pair,
`computeSpectralWaveBufferPair` (`:797-802`) skips the second iFFT when the two lanes have the
same morph amount and same frame. That is the CPU story: worst case 16 iFFTs / 7 ms / voice,
typical case **2**.

---

## 2. The maths, mode by mode

### 2.0 Bin layout you must understand first

`poly_float::kSize == 4` on SSE2/NEON (`poly_values.h:56,59`); 8 on AVX2 (`:53`).
`loadFrequencyAmplitudes` (`wavetable.cpp:160-167`) writes the **magnitude into both** slots of
each complex pair: `amplitudes[2h] = amplitudes[2h+1] = |X[h]|`. `loadNormalizedFrequencies`
(`:169-178`) writes `normalized[h] = polar(1, arg X[h])` i.e. `(cos φ_h, sin φ_h)` and
`phases[2h] = phases[2h+1] = φ_h` (radians).

So `frequency_amplitudes[i] * normalized_frequencies[i]` (elementwise poly multiply)
**reconstructs the interleaved complex spectrum**. One `poly_float` holds **two harmonics**:
lanes (0,1) = harmonic 2i re/im, lanes (2,3) = harmonic 2i+1 re/im. Hence
`kLeftMask` = lanes {0,2} = the real parts, `kSecondMask` = lanes {2,3} = the odd harmonic
(`synth_constants.h:149-159`), and `utils::swapStereo` = `_mm_shuffle_ps(_MM_SHUFFLE(2,3,0,1))`
= swap within each pair (`poly_utils.h:258-266`).

`last_index = 2 * last_harmonic / poly_float::kSize` in every poly-loop morph → the band limit
is quantised to **2 harmonics**.

### 2.1 Amount → morph argument: the curve table

`SynthOscillator::setSpectralMorphValues(type, values, n, spread)` — **`synth_oscillator.cpp:1079-1118`**
`setPowerDistortionValues(v, n, exp, spread)` — **`:491-501`**: `v ← 2^((a − 0.5)·2·exp)`

| mode | curve | a=0 | a=0.25 | **a=0.5** | a=0.75 | a=1 |
|---|---|---|---|---|---|---|
| Vocode | `2^((a−.5)·2·(−1))`, `kMaxFormantShift=1.0` | 2.000 | 1.414 | **1.000** | 0.707 | 0.500 |
| Formant Scale | `2^((a−.5)·2·(−2))`, `kMaxEvenOddFormantShift=2.0` | 4.000 | 2.000 | **1.000** | 0.500 | 0.250 |
| Harmonic Stretch | `2^((a−.5)·2·(+4))`, `kMaxHarmonicScale=4.0` | 0.0625 | 0.250 | **1.000** | 4.000 | 16.000 |
| Inharmonic Stretch | `2^((a−.5)·2·(+12))`, `kMaxInharmonicScale=12.0` | 2⁻¹² | 2⁻⁶ | **1** | 2⁶ | 2¹² |
| Smear | `1 − (1−a)³` | 0 | 0.5781 | **0.8750** | 0.9844 | 1.0 |
| Random Amplitudes | `a · (kRandomAmplitudeStages − 1)` = `15a` | 0 | 3.75 | 7.5 | 11.25 | 15 |
| Phase Disperse | `−(2a − 1) · kPhaseDisperseScale`, scale `0.05` | +0.05 | +0.025 | **0** | −0.025 | −0.05 |
| Spectral Time Skew | `a² · kSkewScale`, scale `16.0` | 0 | 1.0 | 4.0 | 9.0 | 16.0 |
| Shepard Tone | `1 − a`, then `mod(v·0.99)/0.99` | 1 | 0.75 | 0.5 | 0.25 | 0 |
| Low Pass / High Pass | **none** (`default: break`) — raw `a` | 0 | 0.25 | 0.5 | 0.75 | 1 |

Note **Phase Disperse is bipolar** (identity in the middle, opposite dispersion signs either
side) and **Shepard is inverted and wrapping** (so a modulator can free-run it past the ends and
it keeps gliding). `kSkew` is `a²` because the useful range is bunched at the bottom.

Everything is computed with Vital's polynomial approximations, **not** libm:
`futils::exp2` / `futils::log2` are 5th-order minimax polys on the mantissa
(`futils.h:37-70`); `futils::pow(b,e) = exp2(log2(b)·e)` (`:144-146`);
`futils::sin1(p) = sin(2πp)` via the Q=0.776/P=0.224 parabola pair
(`futils.h:336-345`, ≈ −60 dB error). INFERRED: on ratios spanning 2⁻¹²…2¹² the poly `pow`
carries relative error of order 1e-6 — irrelevant here because everything is then *interpolated*.

---

### 2.2 `passthroughMorph` — None (`spectral_morph.h:53-69`)

```
X'[h] = |X[h]| · e^{jφ_h}   for h ≤ last_harmonic
X'[h] = 0                    above
```
Still a full 2048-point iFFT. There is no "bypass".

---

### 2.3 `evenOddVocodeMorph` — Vocode / Formant Scale (`spectral_morph.h:307-347`)

Let `s` = morph argument. For each **output** harmonic `i` in `1 … min(last_harmonic, ⌊1024/s⌋)`:

```
shifted      = max(1, i·s)
index_start  = ⌊shifted⌋ ;  index_start −= (i + index_start) mod 2     ← snap to SAME PARITY as i
t            = (shifted − index_start) · 0.5                           ← 0…1 across a 2-harmonic gap
X'[i]        = s · lerp( X[index_start], X[index_start + 2], t )
X'[0]        = X[0]                                                     ← DC passed through
```

Three things that matter:

1. **The parity snap is the trick.** Even output harmonics only ever read even source
   harmonics, odd only odd. A whole-spectrum resample would destroy the even/odd balance
   (turning a saw into a square as `s` sweeps); keeping parity preserves the *character* while
   the spectral envelope slides. This is the mechanism behind the "formant" naming.
2. **`× s` is the energy compensation** for stretching the envelope over `s`× the axis.
   `X'` is scaled by the same factor the harmonic spacing is scaled by. (Terrain's morphs have
   no such term and this is why several of ours change loudness with the knob.)
3. `last_index = min(last_harmonic, 2048/(2s))` — as the formant is pushed *down* (`s > 1`)
   fewer output harmonics are computed.

**Vocode vs Formant Scale = one multiplier.** In `computeSpectralWaveBufferPair`
(`synth_oscillator.cpp:784-785`):

```cpp
float shift = morph_amount[i];
if (formant_shift) shift *= voice_increment[i] * Wavetable::kWaveformSize;   // = 2048·f/fs
```
plus, for `kVocode` only (`synth_oscillator.cpp:991-1001`):
```cpp
frequency_ratio = (getSampleRate() / wavetable->getActiveSampleRate())
                * wavetable->getActiveFrequencyRatio();
spectral_morph_values_[i] *= frequency_ratio;
```

`FileSource::render` sets `frequency_ratio = window_size / 2048` and `sample_rate = the audio
file's rate` (`file_source.cpp:249-251`). Multiply it out — **DERIVED**:

```
s_total(Vocode) = 2^(1−2a) · window_size · f_played / sample_rate_of_file
```
and if the window size came from pitch detection (`FileSource::detectPitch`,
`file_source.cpp:366-370`, `window = fs_file / f0_file`) then

```
s_total(Vocode) = 2^(1−2a) · f_played / f0_of_the_sampled_note
```

i.e. **Vocode treats the wavetable frame's spectrum as an absolute Hz envelope and re-samples it
onto the played note's harmonic series.** At `a = 0.5` and the sample's original pitch it is the
identity. That's a real vocoder/formant-lock, not a metaphor. `kFormScale` skips both multipliers
so it scales the formant *relative to the fundamental* over ±2 octaves.

Sanity check with numbers: a default 2048-sample table at 44.1 kHz has bin spacing
44100/2048 = **21.53 Hz**. Playing A4 (440 Hz) with `a = 0.5`, `s = 2048·440/44100 = 20.43` —
output harmonic 1 reads source bin ≈20, harmonic 2 reads bin ≈41, etc. Exactly a 21.53 Hz-grid
envelope sampled at 440 Hz spacing.

---

### 2.4 `harmonicScaleMorph` — Harmonic Stretch (`spectral_morph.h:349-387`)

Scatter (not gather). For each **source** harmonic `i` in `1 … min(1025, (last_harmonic−1)/s + 1)`:

```
dest      = max(1, (i − 1)·s + 1)          ← the FUNDAMENTAL IS PINNED (i=1 → dest 1)
d = ⌊dest⌋ ,  t = dest − d
X'[d]     += (1−t) · |X[i]| · e^{jφ_i}
X'[d+1]   +=    t  · |X[i]| · e^{jφ_i}
X'[0]      = X[0]
```

Additive linear splitting into two adjacent bins, each carrying the **source's own phasor**.
Because it accumulates with `+=`, when `s < 1` many source partials land on the same
destination and **sum as complex phasors — they can cancel**. No amplitude compensation.

Terrain's `HarmonicStretch` is the same `r' = 1 + (r−1)·s` law with `s = 1 + 5.5a` (0.5…6.5 in a
`step` from identity) plus an `amp × r'^(1.15a)` tilt. Vital's range is **1/16 … 16**, symmetric
in log space around the identity, with no tilt.

---

### 2.5 `inharmonicScaleMorph` — Inharmonic Stretch (`spectral_morph.h:389-441`)

The stretch factor is itself a function of harmonic number:

```
octave(n)  = log2(n)
power(n)   = octave(n) / (kFrequencyBins − 1) = log2(n) / 10       ← kFrequencyBins = 11
shift(n)   = m ^ (log2(n)/10)                                       ← m = 2^(24a−12)
dest(n)    = max(1, shift(n)·(n − 1) + 1)
```
then the same two-bin linear scatter-add as Harmonic Stretch, `X'[0] = X[0]`.

**DERIVED closed form:** for large `n`, `dest(n) ≈ n^p` with `p = 1 + log2(m)/10`:

| a | 0 | 0.25 | 0.5 | 0.75 | 1 |
|---|---|---|---|---|---|
| m | 2⁻¹² | 2⁻⁶ | 1 | 2⁶ | 2¹² |
| **effective power p** | **−0.20** | **0.40** | **1.00** | **1.60** | **2.20** |

So it is a power-law partial map `r' = r^p`, `p ∈ [−0.2, 2.2]`. Terrain's `InharmonicStretch` is
`r' = r^p, p = 1 + 2.3a` — the same law, half the range, one-sided. Vital's negative-`p` region
(a < 0.167) *reverses* the spectrum, which is where the bell/metallic character comes from.

The `poly_data_start = dest + 2 + kMaxPolyIndex` (`:392`) is scratch space **inside the same
output buffer** past the 2048-float transform region — a nice trick, and safe because
`kSpectralBufferSize = 2048·2/4 + 4` poly_floats = 4112 floats (`synth_oscillator.h:160`).

---

### 2.6 `smearMorph` — Smear (`spectral_morph.h:217-241`)

This is **not** a convolution. It is a **one-pole IIR running up the harmonic axis** on
magnitudes only:

```
u = 1 − (1−a)³
amp  ← |X[0]| · (1 − u)                              ; X'[0..1] = amp · e^{jφ}
for poly index i = 1 … last_index:
    amp ← lerp(|X[i]|, amp, u)  =  (1−u)·|X[i]| + u·amp     ← leaky integrator, coefficient u
    X'[i] = amp · e^{jφ_i}                                   ← ORIGINAL PHASE, untouched
    amp *= (i + 0.25) / i                                    ← droop compensation
```

Because a `poly_float` holds two harmonics, **`amp` is two independent chains: one over EVEN
harmonics, one over ODD.** The `(i+0.25)/i` gain is applied per *poly* step; the cumulative
product over 512 steps is `≈ exp(0.25·H₅₁₂) = exp(0.25·6.86) ≈ 5.6` ⇒ **+14.9 dB of upward tilt**
across the band, which is what stops the IIR from simply killing the top.

**MEASURED (numeric replay of the exact recursion on a 1/n spectrum):**

| u (a) | out[h=2] | out[h=16] | out[h=128] | out[h=1024] |
|---|---|---|---|---|
| 0.000 (a=0) | 1.000 | 0.125 | 0.0156 | 0.00195 |
| 0.578 (a=0.25) | 0.750 | 0.169 | 0.0160 | 0.00196 |
| 0.875 (a=0.5) | 0.234 | 0.328 | 0.0191 | 0.00199 |
| 0.990 | 0.0199 | 0.0546 | 0.0710 | 0.00414 |
| **1.000 (a = 1.0 exactly)** | **0** | **0** | **0** | **0** |

🚨 **At `a = 1.0` exactly, Smear outputs SILENCE.** `amp` is seeded `|X[0]|·(1−u) = 0` and
`lerp(·, amp, 1) = amp`, so the recursion is pinned at zero forever. This is in the shipped
code. It is reachable only at the exact top of the knob (the cubic curve hits 1.0 only at a=1),
which is presumably why nobody filed it. **If Terrain copies the leaky-integrator smear, seed the
state from `|X[0]|` unconditionally and cap `u` at ~0.995.**

The audible behaviour at the usable end (u≈0.875–0.99) is: low harmonics pulled *down*, mid
harmonics pushed *up*, spectrum flattened toward a running average with a +0.25/i climb. Phase is
never touched — which is why it sounds "soft/diffuse" rather than "phasey".

Terrain's `Smear` is a completely different animal (a `1/(1+(r/cut)⁴)` roll-off **plus** a
triangular blur over ±W partials **plus** a seeded phase scatter of `a·4.1`). Three mechanisms
welded to one knob. Vital's is one mechanism, and it's cheaper (O(N), no window, no scatter).

---

### 2.7 `randomAmplitudeMorph` — Random Amplitudes (`spectral_morph.h:443-477`)

```
u      = 15a                                    (kRandomAmplitudeStages = 16)
stage  = min(⌊u⌋, 14) ,  t = u − stage
r      = the fixed random table (below), broadcast so re and im share one scalar, r ∈ [−1, 1]
g(r)   = (1 + u) · max( (1 − u) − u·r , 0 )
X'[h]  = min( lerp(g(r_stage), g(r_stage+1), t) · |X[h]| , 1024 ) · e^{jφ_h}
```

The random table: `RandomValues` singleton, `synth_oscillator.h:34-56` —
`(16 + 1) × (1025 + 1) / 4` poly_floats, `std::mt19937` seeded **`kSeed = 0x4`**,
`std::uniform_real_distribution<float>(-1.0f, 1.0f)`. **17 fixed stages**, crossfaded pairwise
so the knob sweep is continuous and *deterministic across sessions and voices*.

**DERIVED — this is a random spectral DECIMATOR, not amplitude jitter:**
`g(r) > 0` only when `r < (1−u)/u`, so the surviving fraction of bins is `((1−u)/u + 1)/2`:

| a | u = 15a | surviving bins | peak gain |
|---|---|---|---|
| 0 | 0 | **100 %** (identity, g ≡ 1) | 1.0× |
| 0.1 | 1.5 | 33 % | 2.5× |
| 0.25 | 3.75 | 13 % | 4.8× |
| 0.5 | 7.5 | 6.7 % | 8.5× |
| 1.0 | 15 | **3.3 %** | 16× |

So the knob sweeps *identity → random amplitude scatter → sparse inharmonic sprinkle*, with the
`(1+u)` makeup keeping the loudness roughly constant as bins vanish. **Phase untouched.**
Terrain's `RandomAmplitudes` (`mul = 0.02 + r²·5.5`) is amplitude-only and never decimates —
the sparse end is the interesting part and we don't have it.

Dead code note: `poly_float random_t(amount, 1−amount, amount, 1−amount);` at `:461` is computed
and never used.

---

### 2.8 `lowPassMorph` / `highPassMorph` (`spectral_morph.h:243-271`, `:273-305`)

Raw `a`, no curve. `kFrequencyBins = 11`.

```
LowPass :  cutoff = 2^(10a) + 1
HighPass:  cutoff = 2^(10a) · 1026/1025
```

| a | 0 | 0.25 | 0.5 | 0.75 | 1 |
|---|---|---|---|---|---|
| LP cutoff harmonic | 2.00 | 6.66 | 33.0 | 182.0 | 1025.0 |
| HP cutoff harmonic | 1.001 | 5.66 | 32.03 | 181.2 | 1025.0 |

Both are **brick walls with a one-harmonic linear taper**. The boundary `poly_float` gets a
per-lane weight:

```cpp
// lowPassMorph:263-268
if (t >= 1.0f) last_mult = poly_float(1, 1, t − 1, t − 1);   // even harmonic full, odd fading
else           last_mult = poly_float(t, t, 0, 0);           // even harmonic fading, odd gone
```
so the transition is exactly 2 harmonics wide and moves continuously with the knob — no zipper,
no slope control, no resonance. HighPass mirrors it with `(0,0,2−t,2−t)` / `(1−t,1−t,1,1)`.

**Design note worth stealing:** the *only* reason this reads as a filter and not a stair-step is
the per-lane fractional weight at the boundary. It costs two multiplies.

---

### 2.9 `phaseMorph` — Phase Disperse (`spectral_morph.h:180-215`)

The **only** magnitude-preserving morph. `p = −(2a−1)·0.05`, `kCenterMorph = 24.0`,
`offset = −(24−1)²·p = −529p`.

```
δ(n) = p · [ (n − 24)² − 529 ]                       ← radians
X'[n] = X[n] · e^{ j·δ(n) }
```

Implementation is a hand-rolled complex multiply in SIMD: `shift = (cos δ, sin δ, …)` from
`futils::sin1` with a per-lane `phase_offset = (0.25, 0, 0.25, 0)` (cos = sin1(x+¼)), then

```cpp
real = match_mult − swapStereo(match_mult);        // r·cosδ − i·sinδ
imag = switch_mult + swapStereo(switch_mult);      // r·sinδ + i·cosδ
wave_start[i] = amplitude * utils::maskLoad(imag, real, constants::kLeftMask);
```

**DERIVED — quadratic phase = linear group delay = a chirp allpass.** `dδ/dn = 2p(n−24)`, so
group delay is linear in harmonic number, minimum at n = 24, and **harmonic 1 has δ = 0 exactly**
(the anchor: `(1−24)² = 529`).

| a | δ(n=1) | δ(n=24) | δ(n=64) | δ(n=1024) |
|---|---|---|---|---|
| 0.0 | 0 | −26.45 rad | +53.6 rad | **+49 974 rad = 7954 cycles** |
| 0.25 | 0 | −13.2 rad | +26.8 rad | +3977 cycles |
| **0.5** | **0** | **0** | **0** | **0** (identity) |
| 0.75 | 0 | +13.2 | −26.8 | −3977 cycles |
| 1.0 | 0 | +26.45 | −53.6 | −7954 cycles |

At the extremes the top of the spectrum wraps thousands of times, which spreads the cycle's
energy across all 2048 samples: the wave stops being an impulse-ish shape and becomes a
full-cycle chirp. That is the "swirl". **Magnitude spectrum is bit-identical to the input** — it
is inaudible on a spectrum analyser and completely audible on the wave.

Terrain's `SpectralPhaser` (`|sin(π(r+4.5a)/1.7)|²` on the *amplitude*) is not this at all —
ours is a comb on magnitude, Vital's is an allpass on phase. **Different mode. Both can ship.**

---

### 2.10 `shepardMorph` — Shepard Tone (`spectral_morph.h:71-130`)

Two halves. Constants: `kMinAmplitudeRatio = 2.0f`, `kMinAmplitudeAdd = 0.001f`.

**(a) Odd harmonics**, poly loop `:83-86`: `X'[odd] = X[odd] · (1 − shift)`, masked with
`constants::kSecondMask` (lanes 2,3). They fade out as `shift → 1`.

**(b) Even harmonics**, scalar loop `:96-127`, for `i = 0, 2, 4, …`:

```
A_fund = |X[i]|        (the partial at i)
A_shep = |X[i/2]|      (the partial one octave down in the table)
A'     = lerp(A_fund, A_shep, shift)

ratio  = (A_fund + 0.001) / (A_shep + 0.001)
if 0.5 < ratio < 2:                       ← both partials comparable ⇒ phase interp is meaningful
    Δφ (in cycles) = φ_shep − φ_fund, unwrapped into ≈[−1,1] by
        wraps = (int)Δφ ; wraps = (wraps+1)/2 ; Δφ −= 2·wraps
    φ' = φ_fund + Δφ·shift
    (re, im) = (cos 2πφ', sin 2πφ')       ← futils::sin, ≈−60 dB error
else:                                     ← one side is near-silent ⇒ phase is noise
    (re, im) = lerp( normalized[i], normalized[i/2], shift )      ← plain linear on the phasor
X'[i] = A' · (re, im)
```

**This is the phase-continuity lesson.** Vital refuses to interpolate an *angle* when one of
the two magnitudes is negligible, because an angle attached to a −80 dB partial is meaningless
and interpolating toward it produces a wandering, audible phase sweep. Above the 2:1 amplitude
ratio it interpolates the angle (constant-modulus, no cancellation); below it, it interpolates
the complex components (which passes through the origin, but the magnitude is ~0 anyway).

**(c) The pitch machinery**, `setupShepardWrap` `synth_oscillator.cpp:637-659`:

```cpp
poly_float spectral_diff = last_spectral_morph_values_[i] − spectral_morph_values_[i];
poly_float mult = futils::exp2(-spectral_morph_values_[i]);     // pitch × 2^(−shift)
phase_inc_mults_[i] *= mult;  detunings_[i] *= mult;
double_mask = ... | poly_float::lessThan(spectral_diff, −0.6f);  // the value wrapped 1→0
half_mask   = ... | poly_float::greaterThan(spectral_diff, +0.6f);
```
and `doShepardWrap` (`:672`) applies the ×2 / ×½ **at a 7 ms buffer boundary**, where the
crossfade hides it. Because `setSpectralMorphValues` uses `mod(v·0.99)/0.99` for Shepard
(`:980-983`), a slow LFO on the amount produces a genuinely endless Risset glissando.

The table must be built by `ShepardToneSource` (`shepard_tone_source.cpp:34-37`), which makes the
loop frame by putting frame 0's spectrum on the **even harmonics only**
(`loop[2i] = key[i]; loop[2i+1] = 0`) — the same wave an octave up. `Wavetable::setShepardTable`
flags it (`wavetable.h:134`).

---

### 2.11 `wavetableSkewMorph` — Spectral Time Skew (`spectral_morph.h:132-178`)

The only morph that reads the **whole 257-frame table**. Falls back to passthrough if
`num_frames <= 1` (`:138-141`).

```
t₀ = wavetable_index / 256                                   ← current frame, normalised
for harmonic n = 1 … last_harmonic:
    shift_scale = log2(n) / kFrequencyBins        = log2(n)/11
    x           = (t₀ + s·shift_scale) · 0.5
    base        = 1 − 2·mod(x)                     ∈ (−1, 1]
    frame(n)    = (1 − |base|) · 256               ← PING-PONG fold into [0,256]
    from = min(⌊frame⌋, num_frames−2) ; t = min(1, frame − from)
    |X'[n]| = lerp(|X_from[n]|, |X_{from+1}[n]|, t)
    (re,im) = lerp(normalized_from[n], normalized_{from+1}[n], t)     ← linear on re/im, NOT polar
    X'[n]   = |X'| · (re, im)
X'[0] = X_current[0]
```

**Each harmonic is read from a different frame of the wavetable, displaced by
`s·log2(n)/11` frame-spans, folded back and forth.** `s = 16a²`:

| a | s | displacement at n=2 | n=32 | n=1024 |
|---|---|---|---|---|
| 0.25 | 1.0 | 0.09 spans | 0.45 | 0.91 |
| 0.5 | 4.0 | 0.36 | 1.82 | 3.64 |
| 1.0 | 16.0 | 1.45 | 7.27 | **14.5 spans** |

At full tilt the top octave ping-pongs through the entire wavetable 14.5 times while the
fundamental sits still. The frame axis becomes a **frequency-dependent time axis**. It is
genuinely the most distinctive mode in Vital and it costs no more than the others because the
table is already resident.

The **fold** (rather than a wrap) is why it never clicks: `1 − |1 − 2·mod(x)|` is a continuous
triangle, so `frame(n)` is C⁰ in both `t₀` and `s`. A wrap would discontinuity-jump every span.

⚠️ Note the interpolation here is **linear on `(re, im)` of a unit phasor**, so the result is not
unit-modulus mid-interpolation — the phasor shortens toward the chord. Combined with the
separately-interpolated magnitude this makes a small dip when two frames' phases disagree.
Vital accepts it; `WaveSourceKeyframe::linearFrequencyInterpolate` (§4.3) does *not*, and uses
proper polar interpolation. Inconsistent, and the audio-thread one is the sloppy one — INFERRED:
because it's per-harmonic per-7 ms and `atan2` is not affordable there.

---

## 3. Where it lives — the file:line index

| what | file | lines |
|---|---|---|
| **All 11 morph kernels** | `src/synthesis/producers/spectral_morph.h` | 53–477 |
| morph constants | same | 23–33 |
| `transformAndWrapBuffer` (iFFT + guard-sample wrap) | same | 35–51 |
| `enum SpectralMorph` | `src/synthesis/producers/synth_oscillator.h` | 100–113 |
| `RandomValues` (fixed seed 0x4) | same | 34–56 |
| `kSpectralBufferSize` | same | 160 |
| amount → morph-arg curves | `src/synthesis/producers/synth_oscillator.cpp` | 1079–1118 |
| `setPowerDistortionValues` (2^((a−.5)·2·e)) | same | 491–501 |
| per-unison spread + Shepard wrap of the value | same | 968–1002 |
| `runSpectralMorph` (the UI's entry point) | same | 1121–1161 |
| `setWaveBuffers` → template dispatch | same | 736–764 |
| `computeSpectralWaveBufferPair` (**band limit + iFFT**) | same | 766–803 |
| `setFourierWaveBuffers` (spectral-unison gate) | same | 805–864 |
| `kWavetableFadeTime = 0.007f` | same | 47 |
| Shepard pitch wrap | same | 637–659, 672–714 |
| start-phase randomisation | same | 576–596 |
| the 7 ms crossfade + Catmull read | same | 218–344, 1409–1432 |
| storage layout, `postProcess` phase repair | `src/synthesis/lookups/wavetable.{h,cpp}` | h:30–48, cpp:111–178 |
| `kWaveformBits = 11` | `src/synthesis/lookups/wave_frame.h` | 27 |
| forward/inverse FFT backends | `src/common/fourier_transform.h` | 22–171 |
| `kNumOscillatorWaveFrames = 257` | `src/common/synth_constants.h` | 27 |
| parameter definitions | `src/common/synth_parameters.cpp` | 490, 510–515 |
| display names | `src/interface/look_and_feel/synth_strings.h` | 318–331 |
| **offline** wavetable components | `src/common/wavetable/*` | see §5 |
| the same morph, run for the 3D view | `src/interface/editor_components/wavetable_3d.cpp` | 782–789 |

---

## 4. How Vital handles PHASE

Four distinct mechanisms. This is the part most worth stealing.

### 4.1 Phase is stored **separately from magnitude**, twice

`wavetable.cpp:160-178`: `|X[h]|` in `frequency_amplitudes` (duplicated across re/im slots),
the **unit phasor** `e^{jφ}` in `normalized_frequencies`, and the **raw angle** `φ` in radians in
`phases`. Three arrays, 1025 complex bins each, per frame, per table.

Cost: `kPolyFrequencySize = 2·1025/4 + 2 = 514` poly_floats = 2056 floats × 3 arrays × 257
frames × 4 B = **6.34 MB per wavetable**, plus 2.1 MB for the (UI-only) time domain.
**MEASURED from the declarations**, not guessed. Vital pays 8.4 MB per oscillator table to make
magnitude-only and phase-only operations trivial. Terrain's 34-mip ladder pays a comparable
price for a different benefit.

Why three and not two: `normalized_frequencies` is what you multiply by (no trig at use site);
`phases` is what you *interpolate* (Shepard, `:107-116`) because you cannot lerp an angle you
only have as a phasor without an `atan2`.

### 4.2 Runtime phase randomisation is **start phase only**

`SynthOscillator::reset` (`synth_oscillator.cpp:576-596`):

```cpp
poly_float random_amount = input(kRandomPhase)->at(0);
uint32_t random_phase_left  = random_generator_.next() * random_amount[2*v]     * INT_MAX;
uint32_t random_phase_right = random_generator_.next() * random_amount[2*v + 1] * INT_MAX;
```

with `random_generator_(-1.0f, 1.0f)` (`:534`). Parameter `random_phase`: `kLinear` 0…1,
**default 1.0**, display ×100 `"%"`, display name **"Phase Randomization"**
(`synth_parameters.cpp:502-503`). Independent per unison voice **and per stereo lane**. This is
a *read-pointer* offset into the rendered cycle — it does **not** touch the spectrum.

There is no per-bin phase randomisation on the audio thread anywhere in Vital.

### 4.3 Bake-time phase: three explicit styles

**`PhaseModifier`** (UI: "PHASE SHIFTER") — `phase_modifier.cpp:46-82`, styles at
`phase_modifier.h:24-31`. `phase_` is **radians 0…2π** (UI shows degrees,
`phase_modifier_overlay.cpp:151,181-185`), `mix_` 0…1 default 1
(`phase_modifier_overlay.cpp:68-69`). `multiplyAndMix(v, m, mix) = mix·(v·m) + (1−mix)·v`.

| style | law | effect |
|---|---|---|
| `kNormal` | `X[h] ·= e^{−j·h·phase}` (running product) | **linear phase = a pure time shift** of the cycle |
| `kEvenOdd` | even h get `e^{−j·2k·phase}`, odd get its reciprocal | splits the wave into two counter-rotating halves |
| `kHarmonic` | `X[h] ·= e^{−j·phase}` (**same** shift every bin) | constant phase offset — a Hilbert-ish rotation, changes shape not position |
| `kHarmonicEvenOdd` | even `e^{−jφ}`, odd `e^{+jφ}` | ditto, split by parity |
| `kClear` | `X[h] ← \|X[h]\|` | **zero-phase**: every partial cosine-aligned, maximally peaky |

**`FileSource::writePhaseOverrideBuffer`** — `file_source.cpp:340-352`, styles at
`file_source.h:41-46`:

```cpp
if (phase_style_ == kClear)                       // alternate ±π/2  →  sine-phase, symmetric wave
  for (i < 1024) { overridden[2i] = −0.5π; overridden[2i+1] = +0.5π; }
else if (phase_style_ == kVocode) {               // FULL per-bin randomisation
  random_generator_.seed(random_seed_);           // seed saved in the preset JSON
  for (i < 2048) overridden[i] = random_generator_.next();   // uniform in [−π, +π)
}
```
applied at `file_source.cpp:110-115` as `X[h] ← polar(|X[h]|, overridden[h])`.

**That is the answer to "phase randomisation":** uniform over **[−π, +π) per bin, all bins, no
amount knob**, done **offline at bake time**, with the seed persisted in the preset so the table
is reproducible. Randomising phase is what turns a sampled frame into a noise-like/"vocoded" pad;
doing it per-bin at 60 Hz on the audio thread would be a chorus of moving noise.

**Terrain contrast:** our `Smear` mixes a seeded phase scatter of `a·4.1` radians into a mode
that also filters and blurs. Vital keeps *phase randomisation* (bake-time, file source),
*phase shift* (bake-time, PhaseModifier), and *phase dispersion* (realtime, Phase Disperse) as
three separate, separately-reachable things. **That separation is the design lesson.**

### 4.4 Frame-axis phase **repair** — `Wavetable::postProcess` (`wavetable.cpp:111-158`)

This is the cleverest small thing in the codebase and Terrain has no equivalent.

```cpp
static constexpr float kMinAmplitudePhase = 0.1f;
for each harmonic h in 0..1024:
  walk frames w = 0..num_frames−1
  if |X_w[h]| > 0.1:
      linearly interpolate normalized_frequencies[h]  over ALL frames between the previous
      such frame and this one, and write it back
  after the loop: hold the last good phasor forward to the end
```

**The problem it solves:** when a partial dies out in the middle of a wavetable, its phase in
those frames is numerically meaningless (arg of ~0). If you then morph *through* those frames,
or if the partial comes back, the phase jumps arbitrarily — you hear a click or a swirl. Vital
overwrites the meaningless phases with a straight line between the nearest *trustworthy* ones,
so a partial that fades out and back in comes back **in phase with where it was going**.

Note it interpolates the **complex phasor linearly** (not the angle), so mid-span the phasor
is short — but it is only ever used where the magnitude is ≤ 0.1, so the error is bounded.

`full_normalize_` also lets `postProcess` rescale everything by `2/max_span`
(`wavetable_creator.cpp:113-118`, `wavetable.cpp:114-125`) — a single peak-normalisation of the
whole table, magnitudes and time domain together.

---

## 5. Frame-axis BLUR / smear — the honest answer

**Vital has no frame-axis blur.** Not in the audio thread, not in the editor. There is nothing
resembling Terrain's `renderBlend` (`Wavetable.h:353-441`, σ-weighted convex combination of
neighbouring frames).

What it has instead, in order of relevance:

### 5.1 The audio thread does not even *interpolate* between frames

```cpp
// synth_oscillator.cpp:839, 851
poly_int wave_index = utils::toInt(utils::clamp(wave_frame, 0.0f, kNumOscillatorWaveFrames − 1));
int table_index = std::min<int>(wave_index[i], wavetable_data->num_frames − 1);
```

An **integer frame index**. There is no fractional blend of adjacent frames anywhere in the
oscillator. Vital gets away with it because:

1. the frame axis is **pre-rendered offline to 257 frames** (`kNumOscillatorWaveFrames = 257`,
   `synth_constants.h:27`; `WavetableCreator::render()` loops `last_waveframe + 1` frames,
   `wavetable_creator.cpp:93-111`), so the quantisation step is 1/256 of the morph range; and
2. the **7 ms linear crossfade** between the outgoing and incoming iFFT'd buffers
   (`processDetuned`/`processCenter`, `synth_oscillator.cpp:296-333`, `t` ramps 0→1 over
   `num_buffer_samples = 0.007·fs` = 309 samples at 44.1 kHz) smooths the step.

INFERRED but well-founded: a 1/256 frame step, low-passed by a 7 ms linear crossfade, is below
audibility for any table whose adjacent frames are themselves smooth — which the offline
interpolation (§5.2) guarantees.

Caveat worth flagging: `utils::toInt` is `_mm_cvtps_epi32` on SSE2 (**round-to-nearest**) but
`vcvtq_s32_f32` on NEON (**truncate toward zero**) — `poly_utils.h:499-509`. So Apple Silicon and
Intel pick a frame that differs by one for half of all fractional positions. Harmless here;
would not be harmless if you copied the idiom somewhere that matters.

### 5.2 Frame-axis smoothing is **offline, between keyframes**

`WavetableComponent::interpolate` — `wavetable_component.cpp:73-107`, styles
`kNone / kLinear / kCubic` (`wavetable_component.h:32-37`, default `kLinear`).
`kCubic` calls `smoothInterpolate(prev, from, to, next, t)` with the Catmull-ish tween in
`wavetable_keyframe.cpp:26-39`:

```cpp
slope_from = (point_to − point_prev) / (1 + range_prev/range);
slope_to   = (point_next − point_from) / (1 + range_next/range);
smooth = t(1−t)[ (1−t)(slope_from − delta) + t(delta − slope_to) ];
return lerp(from, to, t) + smooth;
```
Non-uniform-spacing-aware — the slopes are scaled by the ratio of neighbouring keyframe gaps.

### 5.3 The frequency-domain frame interpolation — **this is the bit to steal**

`WaveSourceKeyframe::linearFrequencyInterpolate` — `wave_source.cpp:88-114`
(`WaveSource::interpolation_mode_` defaults to `kFrequency`, `wave_source.cpp:23`):

```cpp
amplitude_from = sqrtf(|X_from[h]|);
amplitude_to   = sqrtf(|X_to[h]|);
amplitude      = lerp(amplitude_from, amplitude_to, t);
amplitude     *= amplitude;                                    // ← interpolate the SQRT, then square

phase_from  = arg(X_from[h]);
phase_delta = arg( conj(X_from[h]) · X_to[h] );                // ← SHORTEST-ARC delta, always in (−π,π]
phase       = phase_from + t·phase_delta;
if (amplitude_from == 0) phase = arg(X_to[h]);                 // ← don't lerp from a meaningless angle
X'[h] = polar(amplitude, phase);
DC and the Nyquist bin are lerped as REAL numbers (:104-111).
```

Two laws here that Terrain's `renderBlend` does not implement:

- **Magnitude is interpolated in the √ domain** (i.e. amplitude^0.5, squared back). A linear
  magnitude lerp between two spectra dips in perceived level at t = 0.5; the sqrt-lerp does not.
  It is not equal-power (that would be a sqrt of the *sum of squares*), it is an
  "interpolate the perceptual half-power" heuristic — cheap and audibly better than linear.
- **Phase is interpolated along the shortest arc** via `arg(conj(a)·b)`, which is the correct,
  branch-cut-free way to get the delta, and it explicitly refuses to start from a zero-magnitude
  bin. **This is the anti-cancellation mechanism.** Terrain's `renderBlend` is a weighted phasor
  average — as documented in `00-INVENTORY.md` it is provably subtractive-only, so at blur=1 it
  converges on a near-uniform mean and everything with disagreeing phase cancels. Vital's polar
  interpolation *cannot* cancel: the magnitude is interpolated independently of the phase.

`cubicFrequencyInterpolate` (`:116-161`) does the same with a cubic on both, chaining the
per-step phase deltas so the unwrapped trajectory is monotone.

### 5.4 The four things that *look* like a frame blur but are not

| thing | what it actually is |
|---|---|
| the 7 ms crossfade | a **time-domain** crossfade between two iFFT results, not a spectral blend |
| Spectral Time Skew | a **shear** of the frame axis by frequency, not a blur |
| `Smear` | a **harmonic-axis** leaky integrator (§2.6), nothing to do with frames |
| `postProcess` | frame-axis **phase repair** only, magnitudes untouched (§4.4) |

**Terrain implication:** if we want a frame-axis blur (and `renderBlend` says we do), Vital gives
no prior art for the *blur* but gives the correct prior art for the *pairwise* case — port
§5.3's sqrt-magnitude + shortest-arc-phase law into `renderBlend`'s weighted sum and the
cancellation problem documented in `00-INVENTORY.md §3` becomes a magnitude-domain weighted mean
with an independently-averaged phase. That is a real, bounded change.

---

## 6. Sizes, counts, and where the work happens

### 6.1 The numbers

| quantity | value | source |
|---|---|---|
| `kWaveformBits` | **11** | `wave_frame.h:27` |
| Waveform / FFT size | **2048** samples | `wave_frame.h:28` |
| Harmonics stored | **`2048/2 + 1 = 1025`** (`kNumHarmonics`) | `wavetable.h:33`, `spectral_morph.h:23` |
| `kFrequencyBins` | **11** (= `kWaveformBits`; used as "octaves of harmonic axis") | `wavetable.h:30` |
| Frames per wavetable | **257** (`kNumOscillatorWaveFrames`) | `synth_constants.h:27` |
| Per-frame spectral storage | `kPolyFrequencySize = 2·1025/4 + 2 = 514` poly_float = 2056 floats × 3 arrays | `wavetable.h:34,44-47` |
| Oscillator scratch | `kSpectralBufferSize = 2048·2/4 + 4 = 1028` poly_float, ×2 banks ×32 buffers | `synth_oscillator.h:160`, `:312-313` |
| Buffer refresh period | **`kWavetableFadeTime = 0.007f`** s → 309 samples @44.1 k, **≈143 Hz** | `synth_oscillator.cpp:47` |
| Max unison | 16 (`kMaxUnison`), 8 phase-update pairs (`kNumPolyPhase`) | `synth_oscillator.h:156-159` |
| Max polyphony | 32 active (`kMaxActivePolyphony`) | `synth_constants.h:34` |
| Oscillators | 3 (`kNumOscillators`) | `synth_constants.h:26` |

### 6.2 There is no mip ladder — the band limit is exact and per-note

`computeSpectralWaveBufferPair` — `synth_oscillator.cpp:772-790`:

```cpp
mono_float adjust_phase_inc = voice_increment[i] * phase_adjustment;      // phase_adjustment = 2^⌊log2(fs/44100)⌋
float bin = Wavetable::getFrequencyFloatBin(adjust_phase_inc);            // = log2(1/x)
float bin_shift = Wavetable::kFrequencyBins + 1.0f − bin;                 // = 12 − bin
int last_harmonic = std::max<int>(0, WaveFrame::kWaveformSize * futils::exp2(−bin_shift));
last_harmonic = std::min(last_harmonic, WaveFrame::kWaveformSize / 2);    // ≤ 1024
```

**DERIVED:** `last_harmonic = 1 / (2·phase_inc·adjust) = fs / (2·adjust·f)`, capped at 1024. With
`getPhaseIncAdjustment` (`synth_oscillator.h:264-275`) returning `2^⌊log2(⌊fs/44100⌋)⌋`, the
effective ceiling is `fs / (2·adjust)`:

| f | fs | adjust | `last_harmonic` | top partial |
|---|---|---|---|---|
| 55 Hz | 44 100 | 1 | 400 | 22.0 kHz |
| 440 Hz | 44 100 | 1 | 50 | 22.0 kHz |
| 1760 Hz | 44 100 | 1 | 12 | 21.1 kHz |
| 440 Hz | 96 000 | 2 | 54 | 23.8 kHz |
| 440 Hz | 88 200 | 2 | 50 | 22.0 kHz |

So the timbre is **sample-rate invariant at 44.1/88.2/176.4 kHz** (always 22.05 kHz of content)
and 24 kHz at 48/96/192 kHz. Every buffer is exactly band-limited for the pitch it will be read
at — **zero aliasing by construction, zero mip storage, zero mip-transition artefacts.**
Compare Terrain: 34 mip levels × 16 frames × 2048 = 1.1 M floats per table, built in 21 ms, with
`mipLevelForPhaseIncrement` crossfade seams to manage.

Cost of the approach: pitch modulation is quantised to the 7 ms buffer (a fast portamento
changes its band limit in 143 Hz steps — inaudible, since the *content* only changes when a
harmonic crosses Nyquist).

### 6.3 Offline vs realtime, precisely

| work | thread | when | cost |
|---|---|---|---|
| `WavetableCreator::render()` — 257 frames × (sources + modifiers), each doing `toTimeDomain`/`toFrequencyDomain` | **message** | only on wavetable *edit* / preset load (`wavetable_edit_section.cpp:469-471`) | **≈ 1.8 ms** for a bare Wave Source (2 FFTs/frame), **≈ 7 ms** with 3 modifiers (8 FFTs/frame) — DERIVED from the 3.5 µs/FFT measurement in §6.5 |
| `Wavetable::postProcess` frame-axis phase repair | **message** | end of every `render()` | 1025 harmonics × 257 frames = 263 k complex lerps, ≈ 1 ms INFERRED |
| **the spectral morph** | **AUDIO** | every 7 ms, per voice, per unison pair | 1 iFFT + one O(1025) bin loop |
| the per-sample read | **AUDIO** | every sample | Catmull-Rom 4-tap + crossfade |

**There is no per-morph bake at all.** Changing the morph type or amount costs nothing extra —
the next 7 ms buffer just runs a different kernel.

### 6.4 The publish is *not* double-buffered (know this before copying it)

`Wavetable::loadWaveFrame` (`wavetable.cpp:102-109`) writes **in place** into `current_data_`,
which is the same object the audio thread is reading through `active_audio_data_`. The only
synchronisation is:

- `SynthOscillator::process` brackets itself with `wavetable_->markUsed()` / `markUnused()`
  (`synth_oscillator.cpp:1204`, `:1283`), which just stores/clears the atomic pointer;
- `Wavetable::setNumFrames` allocates a **new** `WavetableData`, copies the old frames in, bumps
  `version`, and then **spin-waits** `while (active_audio_data_.load()) std::this_thread::yield();`
  (`wavetable.cpp:86-88`);
- the version bump makes the audio thread drop its stale `wave_buffers_` pointers
  (`synth_oscillator.cpp:1205-1209`).

So a frame edit can be read half-updated for one block. Vital accepts that (the worst case is one
7 ms buffer built from a torn spectrum). **Terrain's atomic slot publish is stricter and should
stay stricter** — but note that Vital's spin-wait on the message thread is a real priority
inversion hazard that we should not copy.

### 6.5 CPU — MEASURED on this machine

`clang++ -O2 -framework Accelerate`, Apple Silicon, replaying Vital's exact
`FourierTransform::transformRealInverse` body (`fourier_transform.h:103-111`) — 200 000 iterations:

```
vDSP 2048-pt real inverse FFT (+ 1/N scale + memset):  3.503 µs each
scalar bin loop, 2050 float multiplies:                0.148 µs each
```

**Cost class per voice per oscillator:**

| case | iFFTs / 7 ms | µs / 7 ms | % of one core |
|---|---|---|---|
| spectral unison OFF, or all unison voices identical | 2 | 7.3 | **0.10 %** |
| spectral unison ON, 16 voices, spread ≠ 0 | 16 | 58 | **0.83 %** |

At 8 voices × 3 oscillators with spectral unison on that is ~20 % of a core just for the
transforms — which is exactly why `setFourierWaveBuffers:822-826` gates spectral unison behind
"do the morph values actually differ" and why `computeSpectralWaveBufferPair:797-802` aliases
the second lane whenever it can.

**Terrain relevance.** Our message-thread bake is 21 ms MEASURED for 544 iFFTs
(`00-INVENTORY.md`), i.e. **≈ 38 µs per 2048-point iFFT** with the hand-rolled radix-2 in
`Wavetable.h:1947-1983`. That is **11× slower than vDSP**. Two independent conclusions:

1. Replacing the hand-rolled FFT with `vDSP_fft_zrip` (or a single shared `juce::dsp::FFT`)
   would take the 21 ms bake to **≈ 2 ms**, which fixes the 40 %-message-thread-duty problem
   outright and is a smaller change than any of the alternatives.
2. At 3.5 µs, a *realtime* per-block spectral morph is affordable in Terrain too — a 2048-point
   iFFT every 7 ms per voice is 0.1 % of a core. The reason we bake is the **34-mip ladder**,
   not the FFT. Vital's per-pitch `last_harmonic` truncation is what makes the ladder
   unnecessary. That is the trade to think about.

---

## 7. The offline wavetable editor (for completeness — this is where our "morph" lives)

`WavetableComponentFactory` — `wavetable_component_factory.cpp:30-109`. 4 sources + 6 modifiers,
each with keyframes on the 0…256 frame axis, each rendering a `WaveFrame` at a position.

**Sources**

| name | file | note |
|---|---|---|
| Wave Source | `wave_source.cpp` | keyframes are literal 2048-sample waves (base64 in the preset, `:193-207`); frame interpolation `kFrequency` (§5.3) or `kTime` |
| Line Source | `wave_line_source.cpp` | a `LineGenerator` drawn shape |
| Audio File Source | `file_source.cpp` | window size (default 2048), fade styles `kWaveBlend/kNoInterpolate/kTimeInterpolate/kFreqInterpolate` (`file_source.h:33-39`), phase styles `kNone/kClear/kVocode` (§4.3), pitch detect (`kPitchDetectMaxPeriod = 8096`), max 176 400 samples |
| Shepard Tone Source | `shepard_tone_source.cpp:26-44` | frame 0's spectrum on even harmonics only; frame position = `position/256` |

**Modifiers** (all render to `frequency_domain`, then `toTimeDomain()`)

| name | file:line | maths | UI range |
|---|---|---|---|
| Phase Shift | `phase_modifier.cpp:46-82` | see §4.3 | phase 0…2π rad (shown 0…360°), mix 0…1 |
| Wave Window | `wave_window_modifier.cpp:21-75` | multiply the ends of the cycle by a window; shapes `kCos = 0.5−0.5cos(πt)`, `kHalfSin = sin(πt/2)`, `kLinear = t`, `kSquare`, `kWiggle = t·cos(π(1.5t+0.5))` | left/right 0…1, default 0.25 / 0.75 |
| Frequency Filter | `frequency_filter_modifier.cpp:96-113` | `cutoff_index = 2^cutoff`; `slope = 1/lerp(1, 128, shape²)`; LP `clamp(1 − slope·Δ)`, BP `clamp(1 − \|slope·Δ\|)`, HP `clamp(1 + slope·Δ)`, Comb `2·powerScale(triangle(index/(2·cutoff_index)), lerp(−9, +9, shape))`. Optional re-normalise. | cutoff 0…10 (i.e. harmonic 1…1024), shape 0…1, `kMaxSlopeReach = 128` |
| Slew Limiter | `slew_limit_modifier.cpp:42-61` | time-domain slew on the cycle, wrapped twice (`i < 2·2048`, `index = i mod 2048`) so it converges; `max_delta = (2/2048)/max(run_rise, 1/2048)` | up/down 0…1 |
| Wave Folder | `wave_fold_modifier.cpp:40-50` | `y = sin( max_v · boost · asin(clamp(x/max_v, ±1)) )` | boost **1…32** |
| Wave Warp | `wave_warp_modifier.cpp:62-88` | horizontal (phase) and vertical (amplitude) `powerScale(v, p) = (e^{p\|v\|}−1)/(e^p−1)`, symmetric or asymmetric | both **−20…+20** |

`powerScale` (`frequency_filter_modifier.cpp:28-40`, `wave_warp_modifier.cpp:24-35`) is an
**exponential bias**, not a power curve — the same law our
`feedback-envelope-curve-math-power-vs-exponential-bias` note already mandates. Vital uses it
everywhere a "shape/curve" knob appears. Worth noting: it is computed in `double` in both places.

The frame axis of every component is combined by `WavetableCreator::render(position)`
(`wavetable_creator.cpp:64-88`): each **group** renders into `compute_frame_`, all groups are
**summed** and divided by the group count, optional `removedDc()`, then the peak span is tracked
for `postProcess`.

---

## 8. What Terrain should take, and what it must not

**Take (high value, bounded work):**

1. **Replace the hand-rolled radix-2 FFT with vDSP/`juce::dsp::FFT`.** 38 µs → 3.5 µs MEASURED.
   Takes the 21 ms bake to ≈2 ms and retires the "40 % message-thread duty per osc" finding.
   Also collapses the three-different-normalisation-conventions problem
   (`Wavetable.h` / `GeodeEngine.h` / `BlendEngine.h`) into one convention.
2. **The identity must be INSIDE the amount range, and "None" must run the same code path.**
   Vital's default 0.5 = identity for every ratio morph, and `passthroughMorph` is a real morph.
   Terrain's `amount <= 0` short-circuit + `kMaxPartials = 96` truncation is the direct cause of
   the −8.7…−25.1 dBr step at `amount 0 → 1e-6` documented in `00-INVENTORY.md`. Vital's design
   makes that class of bug unreachable.
3. **The sqrt-magnitude + shortest-arc-phase interpolation law** (`wave_source.cpp:88-114`) into
   `renderBlend`. This is the fix for the "provably subtractive-only" property.
4. **`postProcess`'s frame-axis phase repair** (`wavetable.cpp:127-157`). ~10 lines. Any table
   where a partial dies out and returns currently morphs through garbage phase.
5. **Energy compensation as an explicit term.** Vocode multiplies by `s`; Random Amplitudes
   multiplies by `(1+u)`. Neither is a normalise-after — it is the analytically correct factor.
6. **Separate the three phase concepts.** Randomisation (bake), shift (bake), dispersion
   (realtime, magnitude-preserving) are three different controls in Vital and one welded knob in
   our `Smear`.
7. **The one-harmonic linear taper at a spectral brick wall** (`lowPassMorph:262-268`). Two
   multiplies; it is the whole difference between "filter" and "stair".

**Consider (bigger architectural bets):**

8. **Phase Disperse** as a new Terrain mode — quadratic group delay, magnitude-preserving,
   bipolar around the centre. We have nothing like it and our `SpectralPhaser` is not it.
9. **Spectral Time Skew** — needs the whole frame array live at morph time, which our bake model
   already has. The triangle-fold (`1 − |1 − 2·mod(x)|`) is what keeps it click-free.
10. **Vital's no-mip band limit** (`last_harmonic = fs/(2·adjust·f)`). If Terrain ever moves the
    morph to realtime, this is what makes it possible.

**Do not take:**

11. **The message-thread spin-wait in `setNumFrames`** (`wavetable.cpp:86-88`) — priority
    inversion; our atomic slot publish with retire cooldown is strictly better.
12. **In-place writes to a live table** (`loadWaveFrame`) — Vital tolerates torn reads; we don't
    have to.
13. **Naming collisions.** `HarmonicEngine.h:692-808` already ships SPLAY / CULL / TERRACE /
    CLANG. Before adding "Smear" or "Low Pass" as a *morph mode*, check them against those and
    against the no-doubles rule — Vital's `Low Pass`/`High Pass` in particular overlap heavily
    with things we already have on the filter page.

---

## 9. Sources

**Primary (read directly, all citations above are against this tree):**
- Matt Tytel, *Vital*, GPLv3, `github.com/mtytel/vital`, commit `636ca0ef517a4db087a6a08a6a8a5e704e21f836`, 2022-04-20 (`standalone/vital.jucer` → `version="1.0.6"`).
  - `src/synthesis/producers/spectral_morph.h`
  - `src/synthesis/producers/synth_oscillator.{h,cpp}`
  - `src/synthesis/lookups/{wavetable,wave_frame}.{h,cpp}`
  - `src/common/fourier_transform.h`, `src/common/synth_constants.h`, `src/common/synth_parameters.cpp`
  - `src/common/wavetable/*` (10 components + creator + keyframe)
  - `src/synthesis/framework/{futils.h,poly_utils.h,utils.h,poly_values.h}`
  - `src/interface/look_and_feel/synth_strings.h`, `src/interface/wavetable/overlays/*`

**Secondary (used only to cross-check naming; the code governs):**
- David Vogel, *Vital User Guide — Oscillators & Sampler*, `https://davidmvogel.com/docs/Vital/UserGuide/Oscillators-and-Sampler`. Community guide, not official. It calls Spectral Time Skew "skews partials in time, yielding various phase-based effects" — the code (§2.11) shows it is a per-harmonic *frame index* displacement, which is more specific and more useful.
- `https://vital.audio/` — product page; the phrase "spectral warping wavetable synth" and the stretch/shift/smear/skew vocabulary. **`https://vital.audio/manual` returns 404**; there is no official written manual in or out of the repo, and there are no help/tooltip strings for these parameters in the source (`synth_parameters.cpp:612` just copies `display_name` into `local_description`).

**Measurements** made for this document: `clang++ -O2 -framework Accelerate` micro-benchmark of
`FourierTransform::transformRealInverse` (2048-pt real inverse via `vDSP_fft_zrip`) and of the
2050-multiply bin loop; plus numeric replay of the Smear recursion, the Random Amplitudes
survival fraction, the Phase Disperse δ(n), the Skew displacement, and the `last_harmonic` table.
Scratch: `/private/tmp/claude-501/-Users-macshooter-Developer-VST-Plugins/982b122e-d4fe-4e00-ad51-0ad84297df2c/scratchpad/vitalfft.cpp`, `vitalfft2.cpp`.

**Explicitly not known:**
- No official Vital documentation states any of these formulas; every number above comes from the source or from my own measurement. Anything I could not derive is marked INFERRED.
- `kSmoothlyInterpolate` (UI: **"HI-RES WAVETABLE"**, `oscillator_advanced_section.cpp:44`) is created as a parameter and plugged into the oscillator (`oscillator_module.cpp:36,71`) but **has no read site anywhere in `src/`**. I do not know what it was meant to do in 1.0.6; grep says it is dead.
- The commercial Vital (post-1.0.6) may differ. This document describes only the open-source snapshot.
