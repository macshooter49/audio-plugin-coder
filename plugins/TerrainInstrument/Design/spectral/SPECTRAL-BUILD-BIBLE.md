# SPECTRAL BUILD BIBLE — Terrain Instrument

**Synthesis of `00-INVENTORY` (what we ship) · `10-VITAL` · `11-SERUM2` · `12-PHASEPLANT` ·
`13-PAPERS` · `14-OTHERS`.** This is the build document. Everything here is either a line of
Terrain source, a line of somebody else's source, a published number, a measurement, or a
derivation — and each is marked. Where the six research files **disagree or are thin, this
document says so instead of averaging them.**

| Marker | Meaning |
|---|---|
| `[SRC]` | read out of source, `file:line` given |
| `[MEAS]` | measured, by the research pass or by the fb-numbered work already in the tree |
| `[DOC]` | stated in a vendor manual, page cited |
| `[DERIVED]` | closed form worked out here or in the research; checked numerically where it says so |
| `[INFERRED]` | reasoning, not a source. A proposal, not a fact. |
| `[GAP]` | nobody established it. Do not let it be filled in from memory. |

**The six constraints every proposal in this document satisfies** (from `00-INVENTORY §7`):

1. `FrameSpec::kMaxPartials = 96`, hard (`Wavetable.h:49`) — and it bites as a **step** at
   `amount = 0⁺`.
2. `WavetableSpec::kNumFrames = 16`, hard (`Wavetable.h:59`) — a 256-frame import collapses to 16
   the instant a mode engages.
3. The bake is **~21 ms** `[MEAS]`, throttled to ~20 Hz **per osc**, on the **message thread**.
4. `SpectralMorph::apply()` must stay **pure, stateless, amount-parameterised** — that contract is
   what lets `spectralEffAmt_` drive it from the mod matrix (`PluginProcessor.cpp:7542-7557`).
5. `amount = 0` must be an **exact identity**, and the approach to 0 must be continuous.
6. Band-limiting is not optional: output goes back through `buildFromSpec`, or the mode declares a
   rate multiplier in `warpRateMul` (`SynthVoice.h:2428-2433`).

---

# 1. WHERE TERRAIN STANDS

## 1.1 The seven modes, exactly as shipped

`SpectralMorph.h:39-50` (enum), `:105-304` (the switch). One shared `Type` choice (8 options) +
one shared `Amount` float **0…1, step 0.001, default 0.0**, per osc A/B/C/D
(`PluginProcessor.cpp:2345-2357`), modulatable via `ModDest::SpectralA..D`.

| # | Mode | Law as shipped `[SRC]` | Touches |
|---|---|---|---|
| 1 | **Harmonic Stretch** | `r' = 1 + (r−1)·s`, `s = 1 + 5.5a` (1 → 6.5); `amp ×= r'^(1.15a)` | ratio + amp |
| 2 | **Inharmonic Stretch** | `r' = r^p`, `p = 1 + 2.3a` (1 → 3.3); `amp ×= r'^(1.05a)` | ratio + amp |
| 3 | **Vocode** | 5 vowels (Hillenbrand 1995) as 3 Lorentzians; `f = r·F0` with **`F0 = 130.81 Hz` hard-wired**; `amp ×= (1−a) + a·env` | amp |
| 4 | **Smear** | three welded stages: rolloff `1/(1+(r/cut)⁴)`, `cut = 3 + (1−a)²·92`; triangular blur over partial index, `W = round(11a)`; phase scatter `+= a·4.1·rnd11()`, seed `0x5EED1234` | amp + phase |
| 5 | **Random Amplitudes** | `mul = 0.02 + r²·5.5`, seed `0x0A17C0DE`; fundamental protected (`r ≤ 1.01 → 0.7 + 0.3r`); `amp ×= (1−a) + a·mul` | amp |
| 6 | **Data Compress** | quantise to `levels = max(2, round(64 − 62a))` of `maxA`; decimate `keepEvery = 1 + round(3a)` on **list index** | amp |
| 7 | **Spectral Phaser** | `notch = |sin(π(r + 4.5a)/1.7)|²`; `amp ×= (1−a) + a·notch` — depth **and** sweep on one knob | amp |

Plus, adjacent and often confused with them:

- **WT Blur** — `renderBlend`, `Wavetable.h:341-441`. A **frame-axis** Gaussian weighted mean,
  per block, on the audio thread. §2 is entirely about this.
- **WARP** — 11 options × **2 chained slots**, per-sample, time domain (`SynthVoice.h:938-1015`).
- **HarmonicEngine sculpt** — a *separate*, real-time additive engine (≤512 partials) already
  shipping **KEEL · SPLAY · CULL · TIDE · TERRACE · CLANG** (`HarmonicEngine.h:692-808`). Anything
  proposed for `SpectralMorph` must be checked against this list first.

## 1.2 Terrain vs the field

| | Partial ceiling | Where the spectrum is computed | Rebuild rate | Frame interpolation | Spectral op count |
|---|---|---|---|---|---|
| **Terrain (today)** | `kMaxHarmonics = 512`, but `extract()` caps at **96** | message thread, offline bake, **34 mips** | **20 Hz**, ~21 ms/bake `[MEAS]` | convex **phasor** average, same mip — **subtractive-only** | **7** morph + 11 WARP ×2 |
| **Vital 1.0.6** | 1025 | **AUDIO THREAD**, per voice, no mip ladder | **143 Hz** (7 ms), 3.5 µs/iFFT `[MEAS]` | integer frame index + 7 ms time-domain crossfade | **11** morphs |
| **Serum 2.1.4** | full bin set; editor ladder is user-chosen | wavetable: offline at load. Spectral osc: per FFT hop `[INFERRED]` | frame morph = **zero** runtime | 4 algorithms, **computed at load time** `[DOC M2 p.39]` | **70** WT warps ×2 + **34** spectral warps ×2 |
| **Phase Plant 2.4.6** | **1024** (bank verified `[MEAS]`) | **100 % offline**, baked into the preset | zero | `frame: f32`; linear time-domain crossfade `[INFERRED]` | **16** editor effects, 0 runtime |
| **Zebra2** | 1023 (SpectroMorph) / 128 bipolar (SpectroBlend) | timer-driven table render | **4 s … <1 ms, user knob** | morph (move breakpoints) vs blend (crossfade) — **two named modes** | **24**, 2 in series |
| **Zebralette 3** | 1024; Additive 16–1024 (default 256) | table render **or** free-running additive | **200 / 800 / 2000 Hz** | Curve Morph + 3 Guides | **22** osc FX + **8** modifiers |
| **Hive 2** | 1024 | wavetable, ≤256×2048 | `[GAP]` | **switch / crossfade / spectral / zerophase**, plus UHM `morph1/morph2` | UHM script (unbounded) |
| **Pigments 6** | 512 (Harmonic engine) | real-time additive | n/a | **a boolean** (morph or step) | 7 phase transforms + 12×2 spectra + 3 partial shapers |
| **Falcon 3** | user-set (*"A2 has 200 harmonics at 44.1 kHz"* `[DOC]`) | real-time additive | n/a | Smooth Wave Index / Smooth Octaves | ~10 additive params + PD modes `[GAP]` |
| **Massive X** | `[GAP]`; **2–128** waveforms/table | wavetable readout | n/a | `[GAP]` | **10** modes × exactly 2 knobs |
| **Ableton Wavetable** | `[GAP]` | wavetable | n/a | `[GAP]` | **3** effects × 2 params |
| **Bitwig Grid** | — | **no FFT in the Grid at all** | — | — | 0 (4 device-level splitters) |

**Two rows of that table are the problem statement.**

- **96.** Every competitor with a stated ceiling is at **200–1025**. Terrain's `extract()` cap of 96
  against its own `kMaxHarmonics = 512` is below every documented competitor in the survey, and it
  is the measured cause of a **step, not a ramp**, at `amount 0 → 1e-6`: Pulse **−18.2 dBr**,
  Square **−21.6**, Triangle **−25.1**, Rise **−9.4**, SpectralSweep **−8.7** `[MEAS]`.
  25 of 30 factory tables are under the cap and unaffected — which is exactly why nobody caught it.
- **21 ms / 20 Hz.** u-he ships the rebuild rate as a **user-visible knob** (4 s … <1 ms) and calls
  it the reason they are CPU-efficient. Terrain's is fixed at 20 Hz and its true cost is **~4× what
  the code comment claims** (`PluginProcessor.h:1347-1349` still says "~5.6 ms" from the 8-mip era;
  it is now 34 mips). One osc sweeping its Spectral knob is **~40 % message-thread duty**.
  Four oscs saturates the thread.

## 1.3 What Terrain has that the field mostly does not

Do not lose these while chasing the gaps.

| | Why it matters |
|---|---|
| **34-level, sixth-octave mip ladder** (`Wavetable.h:79-81`) | 2^(1/6) ≈ 1.122× per step, already in RAM (4.46 MB/table), already selected per block. Finer than Massive X's, and it is a **free brick-wall filter with no resonance and no phase shift** if we ever expose it (§3.7). |
| **A pure, stateless, mod-matrix-driven morph** | `apply()` is `WavetableSpec → WavetableSpec`. Vital's morphs are welded into SIMD audio-thread kernels; ours can be unit-tested offline in a `clang++ -O2` harness. That is why the fb-numbered gate discipline works here at all. |
| **The energy-preserving inharmonic snap** (`Wavetable.h:126-155`) | A partial at any real ratio deposits `(1−frac)`/`frac` into the two adjacent integer harmonics **as phasors**. Any glided/stretched/scattered partial renders correctly with no new code. Vital has the same two-bin splat; Phase Plant and Serum do not expose one. |
| **A real FX rack that already accepts spectral output** | 16 kinds, `kMaxSlots = 128`, a Splitter with lane routing (fb444), 1,152 modulation destinations (fb453). Bitwig's whole spectral contribution is "decomposition should end in separate signal paths" — we already own the receiving end. |
| **A second, real-time, per-voice additive engine** (`HarmonicEngine`, ≤512 partials) | Every **VOICE-class** idea in the survey (per-note decay, per-note seed, beating) is impossible on a shared table and **natural in HARM**. That is where they should go, not into `SpectralMorph`. |

## 1.4 What Terrain lacks

| Missing | Who ships it | Class | Verdict |
|---|---|---|---|
| **Phase dispersion** (magnitude-exact, quadratic phase) | Vital `kPhaseDisperse`; KH `Disperse`; Zebralette; the whole allpass literature | BAKE, O(P) | **Build it** — §3.1 |
| **Energy-*redistributing* parity** (odd↔even, octaves) | Zebralette `Spectral Focus`; Zebra2 `Odd for Even`; Pigments `Parity` | BAKE, O(P) | **Build it** — §3.2 |
| **Note-tracked formant shift with harmonic re-snap** | Falcon `Harmonics Shift`; Vital `kVocode`'s `×voice_increment`; Pigments Modal `Q` | BAKE, O(P) | **Build it — as the repair of `Vocode`** — §3.3 |
| **Directional (one-sided) smear** | Zebra2 `Smear` (bipolar); Vital's one-pole is upward-only | BAKE, O(P) | **Build it** — §3.4 |
| **Frequency-dependent frame offset** | Vital `kSkew` (`wavetableSkewMorph`) | BAKE, O(F·P) | **Build it, at ¼ Vital's range** — §3.5 |
| **A partial-index window on every mode** | Pigments (`Position`+`Win Size`, everywhere); UHM `Lowest/Highest`; Falcon `Keep Bass` | BAKE, 2 compares | **Build it — highest new-sounds-per-line** — §3.6 |
| **The mip ladder as a playable tone control** | Massive X `Standard→Filter`; Zebra2 `Filter` (>100 dB/oct) | READ, 1 int add | **Build it, and do not call it Filter** — §3.7 |
| **Seeded, order-preserving ratio scatter** | Zebralette `Chaos Patterns → Ordered`, 100 fixed seeds | BAKE, O(P) | **Build it** — §3.8 |
| **Frame phase alignment before blending** | KH `Align Fundamentals / Align Frames / Align All Phases`; Vital `postProcess`; Hive `zerophase` | BAKE, O(F·H) | **Build it — this is the blur fix**, §2.5 |
| Shepard tone | Vital, Pigments (`Phi`), Meld (`Shepard's Pi`) | VOICE-ish | **No** — §5 |
| Low Pass / High Pass as morph modes | Vital `kLowPass/kHighPass` | BAKE | **No** — §3.7 supersedes it |
| Peak-matched morph (`morph1/morph2`) | UHM only, and u-he refuse to document it | BAKE, O(P²) | **Not now** — §5 |
| Per-voice spectral (decay, twinkles, dissociate, beating) | Zebralette, Massive X, Falcon, Ableton | VOICE | **No — route to `HarmonicEngine`** — §5 |

## 1.5 The four structural facts that gate every recommendation

1. **The bake cost is the hand-rolled FFT and essentially nothing else.**
   `[MEAS]` 2048-pt inverse: **vDSP 0.92 µs vs naive radix-2 33.65 µs = 36.4×**. 544 iFFTs ×
   33.65 µs = **18.3 ms projected**; Terrain measures **20.8–23.1 ms**. Swapping
   `Wavetable::inverseFFT` (`Wavetable.h:1956-1983`) for `juce::dsp::FFT` takes the bake to
   **under 1 ms** — from ~40 % message-thread duty per osc to under 2 %. It also collapses the
   **three** hand-rolled radix-2 FFTs with three different normalisation conventions
   (`Wavetable.h`, `GeodeEngine.h`, `BlendEngine.h`) into one. **This change dwarfs every
   algorithmic choice in this document and should land before any of them.**
2. **The identity must be *inside* the range.** Vital's morph default is **0.5 = identity** for every
   ratio morph, and `None` runs the *same* iFFT as every other mode — so Vital structurally cannot
   have Terrain's `amount 0 → 1e-6` step. Ours comes from two causes stacking: the `amount <= 0`
   short-circuit (`SpectralMorph.h:62-63`) plus the 96-partial truncation firing the instant the
   short-circuit stops. Fix both: raise the cap **and** make every multiplier satisfy `f(x, 0) = x`
   exactly (Vital's `g(r,0) = 1` by construction; ours is `0.02 + r²·5.5`, never 1).
3. **Blur can only subtract, by construction** — proof in §2.2. Any "richer/brighter" claim about
   the frame axis is false today.
4. **No doubles.** `SPLAY` (stiff-string stretch, `HarmonicEngine.h:714-731`, `B ≤ 0.138`) and
   `TERRACE` (dB-domain spectral quantiser, `:760-776`) already exist — and **`TERRACE` and
   `DataCompress` are the same idea implemented twice in two engines with different units.**
   Reconcile that before adding anything.

---

# 2. THE BLUR VERDICT

## 2.1 What blur is, exactly

`Wavetable::renderBlend`, `Wavetable.h:341-441`. Param `SYN_OSC_x_FRAME_SPREAD` ("Blur"),
float 0…1, default 0, smoothed per block (`blur += (target − blur)·0.25`), mod dests
`ModDest::BlurA..D`. Called **once per block per osc**; the audio thread never sees a spectrum.

```
N      = numFrames_ ;  fIdx = clamp01(framePos)·(N−1) ;  f0 = ⌊fIdx⌋ ;  f1 = min(f0+1, N−1)
ref[n] = x[f0][n]·(1−fFrac) + x[f1][n]·fFrac                       ← the un-blurred bilinear read

σ      = 1e-4 + N·1.05·blur^1.25                                    (:394)
band   = min(N, ⌈4σ⌉ + 2) ;  taps T = {lo, lo+stride, …} , |T| ≤ 32 (:395-404)
g_f    = exp(−½·((f − fIdx)/σ)²) ,  w_f = blur·g_f / Σ_{j∈T} g_j    (:411-418)

pre[n] = Σ_{f∈T} w_f·x[f][n]  +  (1 − blur)·ref[n]                  (:425-431)
G      = sqrt( Σ_n ref[n]² / Σ_n pre[n]² )                          (:437-438)
out[n] = G·pre[n]                                                   (:440)
```

Total weight is **exactly 1** by construction. All taps are the **same mip**, so the result is
band-limited with no imaging, ever. Cost `[MEAS]`: **0.16 µs** on the fast path (`blur ≤ 1e-4`),
**13.2 µs** at `blur = 1` on a 16-frame table, bounded at 32 taps for a 256-frame import.

## 2.2 Why it can only subtract — the proof

Per harmonic bin `h`, writing `X_f[h]` for frame `f`'s complex coefficient at that mip:

```
P[h] = Σ_f w_f · X_f[h]                    with  Σ_f w_f = 1,  w_f ≥ 0

|P[h]| = |Σ_f w_f X_f[h]|  ≤  Σ_f w_f |X_f[h]|          (triangle inequality)
```

Equality holds **iff every `arg X_f[h]` is identical**. So the blurred magnitude of every harmonic
is **at most** the weighted mean of the frames' magnitudes, and strictly below it whenever the
phases disagree. **Blur can cancel; it can never create.** The trailing RMS match `G` restores
broadband level, which is precisely why the loss reads as *timbral* rather than as *quieter* — but
`G` is one scalar, and it cannot put energy back into any individual harmonic.

At `blur = 1`, `σ ≈ 1.05·N` means the Gaussian is essentially flat across the whole table: blur 100 %
**is the uniform mean of every frame**. That is the mathematical maximum a frame-axis mean can do.
There is no more headroom in the mechanism.

## 2.3 Why it DULLS — the derivation that explains the measurement

Measured ceiling: **only −12 dB of harmonic change at blur = 1**, and spectral centroid
**6.07 → 3.81** (harmonic number, as reported by the measurement brief; the exact weighting is not
recorded in the research files — treat the *ratio* 0.628 as the load-bearing figure, not the
absolute values). `[MEAS]` `[GAP: the centroid's weighting definition]`

Here is the mechanism, and it explains **both** symptoms with one term. `[DERIVED]`

Decompose the inter-frame phase disagreement into the part that is a **pure circular time shift**
(removable, and dominant on real tables — it is what happens when a table's frames were authored or
imported without a common phase origin) and the part that is genuine spectral difference
(irreducible, and what blur is *supposed* to average). Take the removable part alone:

```
X_f[h] = X_ref[h] · e^{ j·2π h τ_f / N }              τ_f = frame f's time offset, samples

P[h] = Σ_f w_f X_f[h] = X_ref[h] · Ψ(h) ,   Ψ(h) = Σ_f w_f e^{ j h θ_f } ,  θ_f = 2π τ_f / N
```

`Ψ(h)` is the characteristic function of the `θ` distribution under the blur weights. For a spread
of `θ` with standard deviation `σ_θ`:

```
|Ψ(h)| ≈ exp( −½ · h² · σ_θ² )
```

**That is a Gaussian low-pass in HARMONIC NUMBER**, applied by the blur itself, with a −3 dB corner at

```
h₃ = 0.8326 / σ_θ = 0.8326·N / (2π σ_τ) = 271.4 / σ_τ            (N = 2048)
```

| inter-frame misalignment `σ_τ` | −3 dB corner `h₃` |
|---:|---:|
| 5 samples | 54 harmonics |
| 10 samples | 27 |
| 20 samples | 13.6 |
| 40 samples | 6.8 |

Then `G` re-raises the whole thing. **Result: the sound gets darker but not quieter, the total
harmonic change saturates around −12 dB because the low harmonics (`h ≪ h₃`) are barely touched,
and the centroid collapses because the high harmonics are killed first.** Both measurements, one
term.

**Pre-registered, falsifiable prediction:** the measured centroid ratio of 0.628 implies an
effective `h₃` in the **10–20 harmonic** band, i.e. `σ_τ ≈ 14–27 samples` of real inter-frame
misalignment on the tables that were measured. *Measure it:* cross-correlate each of the 16 frames
of the same spec against frame 0 and report the standard deviation of the peak lag. If it lands in
that window, the mechanism above is confirmed and §2.5's fix is the right fix. **If it does not, this
whole section is wrong and must be redone** — say so rather than shipping on the strength of a
derivation.

## 2.4 What fb464 did and did not fix — the research needs reconciling

`00-INVENTORY §3.2` records that the **old** law (`blur²·9`, absolute-frame spread) measured
−53…−102 dB of harmonic change at 25 % and only **−13.5 dB at 100 %** (`Tests/blur_audit.cpp`), and
that fb464 changed the exponent to 1.25 and made σ scale with `N`, because Max said *"blur isn't
really doing much, like not at all."*

Hold that next to the new measurement: **−12 dB at 100 %.**

> **fb464 redistributed the knob; it did not raise the ceiling — and mathematically it could not
> have.** −13.5 dB → −12 dB is the same ceiling. The old law's failure was that the bottom half of
> the travel was inaudible; the new law fixed the *travel* and left the *destination* where it was,
> because the destination is "the uniform mean of all frames", which §2.2 proves is the maximum of
> the mechanism. **Any further work on σ, the exponent, or the tap count is wasted.** The ceiling is
> a property of "weighted phasor mean", not of its parameterisation.

This reconciliation is not stated in any of the six research files. It is the single most important
consequence of reading them together.

## 2.5 What blur SHOULD be — two recommendations, in order

### R1 — ALIGN FRAMES at bake time. Free, exact, ships first.

Remove the removable part of the phase disagreement, in the **spec domain**, before `buildFromSpec`
ever runs. A pure circular time shift changes **no frame's own waveform, magnitude spectrum, or
crest factor** — it is inaudible on a looped single cycle by itself (`13-PAPERS §A4`: linear phase =
a pure time shift). It only changes the *relationship between* frames, which is exactly the term
§2.3 identified.

**Cheap version — Align Fundamentals** (this is the one to build first; it needs no FFT at all,
because Terrain's spec *is* `{amplitude[h], phase[h]}`):

```
ref = frame 0
for each frame f, for each partial i:
    dφ  = phase_ref[1] − phase_f[1]           (fundamental only)
    phase_f[i] += ratio_i · dφ                (a pure time shift: φ_h += h·τ)
```

Cost: `16 frames × ≤512 partials` adds ≈ **microseconds**. Reuses `extract()`/`writeBack()`
verbatim (`SpectralMorph.h:76-103`).

**Full version — Align Frames** (max-correlation, Kilohearts' `[DOC]` recipe):

```
τ_f = argmax_τ  Σ_h A_f[h]·A_ref[h]·cos( φ_ref[h] − φ_f[h] − 2π h τ / N )
    = argmax  IFFT( conj(X_f) · X_ref )[τ]                       ← one 2048-pt iFFT per frame
then  φ_f[h] += 2π h τ_f / N                                     ← the same pure time shift
```

16 iFFTs = **0.015 ms with vDSP** `[MEAS]`, i.e. free once §1.5.1 lands. Refine `τ_f` by parabolic
interpolation on the three bins around the peak.

**What R1 buys, stated as a gate, not a claim:** after alignment, `|Ψ(h)| → 1` and the blurred
magnitude of harmonic `h` should equal the **arithmetic weighted mean of the 16 frames'
magnitudes** at that harmonic, to within `G`. Today it is that mean times `exp(−½h²σ_θ²)`.
Measure both, per harmonic, per table. **Mutation gate (fb453): skip the alignment step and the
gate must fail on the high harmonics specifically** — if it fails uniformly across `h`, the harness
is measuring level, not the mechanism.

⚠️ **Do NOT ship "Align All Phases" (set every `phase[h] = 0`) as the default.** `14-OTHERS §9.1`
proposes zero-phase frames and warns they will be *"impulsive (all partials in phase = a peaky,
cosine-stacked wave)"*. **That warning is convention-dependent and is backwards for Terrain.**
Terrain's convention is **sine-referenced** — `X[h] = (A_h/2)(sin φ_h − i·cos φ_h)`, so the raw
inverse yields `Σ A_h sin(2πhn/N + φ_h)` (`Wavetable.h:165-190`). Therefore `[DERIVED]`:

| all `φ_h = ` | Terrain (sine convention) | crest on a 256-partial `1/h` spectrum |
|---|---|---|
| **0** | a **sawtooth** — `Σ (1/h)·sin(hx) → (π−x)/2` | **≈ 1.89** (peak 1.71 w/ Gibbs, RMS 0.907) |
| **π/2** | the cosine stack — `Σ (1/h)·cos(hx)`, log singularity at 0 | **≈ 6.75** |

That is an **11 dB** difference in the opposite direction from the research's warning. Zero-phase in
*our* convention lands on the **low**-crest end for `1/h`-ish content, not the high-crest end.
It is still the nuclear option (every frame becomes a saw-family wave, destroying timbral identity
across the table) — but the stated *reason* to avoid it is wrong, and anyone who lifts a Vital
formula that *sets* an absolute phase needs the `φ → φ − π/2` correction (`13-PAPERS §0`, and note
`13-PAPERS §H.5` flags that correction as **unverified** — null-test before shipping any lifted
formula). A phase-only *additive* op (`φ += δ`) needs no correction in either convention.

### R2 — after the FFT swap: a true magnitude-domain frame blur

Once the bake is ~1 ms, blur can move out of `renderBlend` and into the spec domain, where it can be
done correctly. The law is Vital's `WaveSourceKeyframe::linearFrequencyInterpolate`
(`wave_source.cpp:88-114`), generalised from 2 frames to `N`:

```
M[h] = ( Σ_f w_f · |X_f[h]|^{1/2} )²                  ← interpolate the SQRT, then square
Φ[h] = arg( Σ_f w_f · e^{ j φ_f[h] } )                ← unit phasors only; the modulus is DISCARDED
       (fall back to φ_ref[h] when Σ w_f·A_f[h] is below a floor — do not lerp from a meaningless angle)
X'[h] = M[h] · e^{ j Φ[h] }
```

Two laws, both load-bearing:

- **The magnitude is interpolated in the √ domain.** A linear magnitude mean between two spectra
  dips in perceived level at the midpoint; the sqrt-mean does not. It is not equal-power (that would
  be `sqrt(Σ w A²)`); it is the "interpolate the perceptual half-power" heuristic — cheap and
  audibly better than linear `[SRC: Vital]`.
- **Phase and magnitude are interpolated *independently*.** `|X'[h]| = M[h]` by construction, so
  **cancellation is impossible.** The `G` fudge factor disappears. This is the change that makes
  `00-INVENTORY §3.1`'s "blur can only subtract" statement **false**.

**Cost:** BAKE class, O(F·H). Blur becomes a ~20 Hz baked parameter like `Amount` rather than a
per-block one, which is a real behavioural change — see the fork in §6.1.

**What blur must NOT become:** a frequency-axis magnitude blur wearing blur's name. That is a
different device (§3.4) and welding it onto the frame-axis knob repeats the exact mistake `Smear`
already made (three mechanisms, three curves, one knob).

## 2.6 PHASE DISPERSION as the "razzle-dazzle" candidate

**The maths.** A phase-only operator is magnitude-exact:

```
X'[h] = X[h]·e^{ j φ(h) }     ⇒     |X'[h]| = |X[h]|     exactly
```

`[MEAS]` Vital's maximum dispersion applied to a 256-harmonic saw, re-analysed: max relative
magnitude error **8.5 × 10⁻¹⁷**. That is float round-off. **Bit-for-bit the same spectrum.**

Linear group delay ⇔ quadratic phase (integrate once), so the classical `M`-section allpass cascade's
*entire* contribution can be evaluated at the 1024 known harmonic frequencies and applied in one
pass at bake time:

```
τ(h) = τ₀ + k·h   (samples)   ⇒   φ(h) = c·[ (h − h_c)² − (1 − h_c)² ] ,   τ(h) = −(N/π)·c·(h − h_c)
```

The `−(1 − h_c)²` offset pins `φ(1) = 0` so the knob **disperses without also rotating the wave**.
Vital ships exactly this with `h_c = 24` and `c = 0.05·(1 − 2a)` `[SRC: spectral_morph.h:180-215,
synth_oscillator.cpp:1105-1108]`. Theory anchor: Schroeder phase (1970) — the low-crest closed form
for a flat spectrum is `φ_h = −πh²/H`, a quadratic. `[PAPER]`

**What it costs the waveform** `[MEAS]`, 256-harmonic saw, N = 2048:

| `c` | crest | φ(256) | wraps | τ(256) samples | |X'| error |
|---|---|---|---|---|---|
| 0.000 | 6.761 | 0 | 0 | 0 | — |
| 0.005 | 4.080 | 266 rad | 42.4 | −756 | |
| 0.020 | 3.399 | 1066 | 169.6 | −3025 | |
| **0.050** (Vital max) | **2.177** | **2665** | **424.1** | **−7562 = 3.7 cycles** | **8.5e-17** |

Crest falls **−9.8 dB**. 22.6 dB of peak reduction is available on a *flat* spectrum (Schroeder vs
zero-phase). It is 1.77 µs per 512-harmonic frame, **28.3 µs for a whole 16-frame spec** `[MEAS]`.

### Does it beat a frequency-axis magnitude blur? — the honest answer

**For "razzle-dazzle" and for "does not dull": yes, outright, and it is not close.**

| | Phase dispersion | Frequency-axis magnitude blur |
|---|---|---|
| magnitude spectrum | **unchanged, exactly** | low-passed *along `h`* — it is a smoother on the envelope |
| can it dull? | **No. Mathematically cannot.** | **Yes — that is literally what it does.** It fills notches, flattens peaks, melts formants |
| RMS side-effect | none | **−4.32 dB at W = 11** on a saw `[MEAS]` — needs renormalisation or the knob is a volume knob |
| what changes | the waveform, the crest factor (−9.8 dB), the scope | the timbre, the contrast between harmonics |
| Terrain has it? | **No** | **Yes** — inside `Smear`, welded to a rolloff and a phase scatter |
| cost | 28 µs/spec | 134 µs/spec at W=11; 26 µs with a one-pole |

The blur Terrain has *already* dulls (centroid 6.07 → 3.81). Adding a **second** dulling mechanism to
fix the first one's ceiling would be perverse. Dispersion is the only candidate on the table that is
*constitutionally incapable* of dulling, and it is the highest sound-per-µs item in the whole
research corpus.

**But — and this is the part the research does not say, and Terrain's own history does:**

> **For replacing what blur is *for*, phase dispersion does NOT beat a magnitude blur — because it
> changes the magnitude spectrum by exactly zero, and this project has already measured, on its own
> hardware, that Max cannot hear that.**

fb282 shipped Plate **Dispersion** and the harness reported *"102 % divergence"*. Max heard
**nothing**. fb283 caught why: the real **magnitude-spectrum** change was **0.02 dB** — and that
incident is the reason `feedback-perceptual-test-harness-hardrule` **bans sample-difference RMS as a
dramaticism metric**. A steady, looped, single-cycle oscillator into a linear chain is precisely the
worst case for a phase-only operator: the ear's steady-state timbre percept is overwhelmingly
magnitude-determined.

Dispersion becomes audible in Terrain by three routes, and **all three must be in the gate**:

1. **Downstream nonlinearity** — the fold (`Shapers.h`, ADAA), WARP 9/10 (Rectify / Sine Shaper),
   Drive, the FX-rack Distortion (23 modes). A **−9.8 dB crest change** is an enormous change to
   what a folder does. This is the primary route and it is a magnitude change, measurable on the
   output spectrum.
2. **Transient / envelope interaction** — where in the cycle the peak sits, at note-on and under a
   fast Env 1 attack.
3. **The scope** — the UI hard rule ("if it changes the sound, the viz changes"; fb417
   "geometry is not hearing" cuts the other way here: it *is* geometry, and geometry alone is not
   enough).

**THE RULING:**

- **Blur's fix is `Align Frames` (§2.5 R1), then the magnitude-domain law (§2.5 R2). Not dispersion.**
- **Dispersion ships as its own mode (`Disperse`, §3.1), bipolar, identity at centre — and it is
  gated on the output spectrum *downstream of the fold*, never on the bare oscillator, and never on
  sample-difference RMS.** If the gate can only be passed with the fold engaged, say that in the
  UI: it belongs next to the wave-shaping controls conceptually, even if it lives in the morph menu.
- **Do not let `Disperse` be sold internally as "the blur fix".** It is a different device solving a
  different problem, and conflating them is how `Smear` got three mechanisms on one knob.

## 2.7 Blur gate list (all must be green, all must be mutation-proven)

| # | Gate | Fails if |
|---|---|---|
| B1 | Cross-correlate frames 1…15 against frame 0 on ≥6 factory tables; report `σ_τ`. | `σ_τ` outside 5–40 samples ⇒ §2.3's mechanism is wrong, stop and re-derive |
| B2 | Per-harmonic `\|blur=1\|` vs the arithmetic weighted mean of the 16 frames' magnitudes, **before and after** R1. | after R1 they must agree within `G`; before R1 the ratio must follow `exp(−½h²σ_θ²)` |
| B3 | **Mutation:** disable Align Frames, B2 must fail **on the high harmonics specifically**. | a uniform failure means the harness is measuring level (fb453) |
| B4 | Centroid and 95 % rolloff at blur 0 / 0.25 / 0.5 / 0.75 / 1, **after** R1. | centroid must not fall more than the *true* frame-spectral spread of the table justifies |
| B5 | Every frame's own crest and magnitude spectrum, before vs after alignment. | **must be bit-identical** — a pure time shift changes neither. If it doesn't, the shift isn't pure |
| B6 | The path, not the engine (fb373): drive `SYN_OSC_A_FRAME_SPREAD` **through the real AU**, not the harness. | a green harness proves the engine works, never that the plugin reaches it |
| B7 | Seed before measuring (fb441): run ≥8 blocks before the first measurement. | a fresh engine snaps on block 1 and hides every steady-state bug |

---

# 3. NEW SPECTRAL MODES TO ADD — ranked

Ranked by **(musical payoff) / (implementation risk)**. Cost classes from `14-OTHERS §1`:
**BAKE** = a pure op inside `SpectralMorph::apply()`, microseconds, free against the 21 ms bake ·
**READ** = changes what the audio thread reads, must be justified against the WARP budget ·
**VOICE** = needs per-note state, **not portable** (§5).

| Rank | Name | Kind | Payoff | Risk | Cost | Terrain primitive reused |
|---|---|---|---|---|---|---|
| 1 | **Disperse** | MODE | very high (nothing like it in the box) | low (pure phase, no cancellation possible) | BAKE 28 µs/spec | `extract`/`writeBack`; the phasor snap |
| 2 | **Partial Range** (Lo/Hi) | MODIFIER on all modes | very high (multiplies 7 modes × 3 windows) | low (2 compares) | BAKE ~0 | `extract` loop |
| 3 | **Focus** (energy-conserving parity) | MODE | high (loud at 100 %, obeys evolve-0→100) | low | BAKE O(P) | `extract`/`writeBack` |
| 4 | **Directional Smear** | MODE (repairs #4) | high (one sign bit = two characters) | very low | BAKE 1.6 µs/frame | `Smear`'s own loop |
| 5 | **Formant** (Harmonics Shift) | MODE (**repairs `Vocode`**) | high (fixes a documented defect) | medium (needs an envelope estimate) | BAKE O(P) | `Wavetable::lorentzian` |
| 6 | **Brilliance** (ladder as filter) | READ-PATH | high (free brick wall, tracks pitch exactly) | medium (**naming collision**) | READ, 1 int add/block | `kMipMaxHarmonics`, `mipLevelForPhaseIncrement` |
| 7 | **Skew** (frequency-dependent frame offset) | MODE | very high (genuinely distinctive) | medium-high (16-frame axis is coarse) | BAKE O(F·P) | the whole-spec view `apply()` already has |
| 8 | **Ordered Scatter** | MODE | medium-high (recallable randomness) | low | BAKE O(P) | the phasor snap; `RandomAmplitudes`' seed pattern |

**Prerequisite, not a mode: `Keep Bass`.** Capture `A₁, φ₁` before `apply()`, restore after
(optionally blended by a `Keep` amount), **in `writeBack()`, once**. Terrain already does two
ad-hoc special cases of this — `RandomAmplitudes` protects `ratio ≤ 1.01`
(`SpectralMorph.h:246`) and `HarmonicEngine::postRoot()` re-asserts the fundamental after every
sculpt (`:809`) — so the pattern is proven in-tree; it is just not global. Falcon ships it as
`SafeBass` `[DOC S2 p.121]`. Two floats. **Default OFF so existing presets do not change.**
Modes 3, 5, 7, 8 all want it.

---

### 3.1 — DISPERSE ⭐

**One line:** a magnitude-exact chirp — the wave's shape and crest change completely, its spectrum
by zero.

**Formula.** In `apply()`, on the flat partial list from `extract()`, evaluated at the *continuous*
harmonic index `r` (partials need not be integers; the phasor snap in `buildFromSpec` handles the
rest):

```
h_c = 24                                              (the hinge; harmonics below it disperse backwards)
φ_i += c · [ (r_i − h_c)² − (1 − h_c)² ]              radians;  the offset pins φ(r=1) = 0
```

No amplitude term. No convention correction needed (this is `φ += δ`, not `φ := δ`).

**Parameter.** `Amount` reused, but **bipolar around 0.5**:

```
c(a) = c_max · sign(2a − 1) · |2a − 1|^2.5            c_max = 0.08 rad/harmonic²
```

`a = 0.5` is an **exact identity** (`c = 0`) — which is also the fix for constraint #5 on this mode.
The `^2.5` exponent exists because **70 % of the crest travel happens in the first 5 % of a linear
knob** `[MEAS]` — Vital ships a linear map and lives with the plateau; Terrain's
`feedback-params-evolve-0-100-no-freerun-hardrule` does not permit it. The exponent is **eyeballed
off the measured crest curve, not fitted** `[13-PAPERS §H.7]` — fit it properly against Terrain's own
crest measurements before locking it.

**Alternative unit worth considering:** expose `τ_max` in **whole cycles of spread** instead of `c`,
since `τ(H) = −(N/π)·c·(H − h_c)`. At H = 512, N = 2048: "1 cycle of spread" = `c = 0.00644`.
A musician can reason about cycles; nobody can reason about rad/harmonic².

**CPU class:** BAKE, O(P). **1.77 µs / 512 harmonics; 28.3 µs / 16-frame spec** `[MEAS]`.
Use real `sinf`/`cosf` in the bake, not a polynomial approximation.

**Reuses:** `SpectralMorph::extract`/`writeBack` (`SpectralMorph.h:76-103`) unchanged; the
energy-preserving inharmonic snap (`Wavetable.h:126-155`) carries the rotated phase through.

**Gate:** per §2.6 — magnitude spectrum must null at ≤ −100 dBr against the un-dispersed table
(prove it *is* magnitude-exact); crest factor must move ≥ 6 dB across the knob; and the **output
spectrum measured downstream of the fold at `FOLD_AMT = 0.5`** must show ≥ 6 dB of change in
centroid or HF-ratio. **Sample-difference RMS is banned** as evidence (fb283).

---

### 3.2 — PARTIAL RANGE (Lo / Hi) ⭐

**One line:** every existing mode gets a partial-index window, so a spectral effect stops being a
blunt instrument applied to the whole spectrum.

**Why it is rank 2 despite being trivial:** *four different manufacturers arrived at this
independently* — Pigments puts `Position` + `Win Size` on **every** partial shaper `[DOC S1
pp.124-126]`, Pigments Modal has `Range` (*"the first harmonic below which partials are no longer
warped"*) `[DOC S1 p.130]`, UHM's `Spectrum`/`Phase` commands take `Lowest`/`Highest`
`[DOC S6 p.5]`, Zebralette's Curve Filter has drawn endpoints `[DOC S4 p.19]`. Terrain's seven modes
are **all whole-spectrum**. This is the highest ratio of new sounds to new code in the corpus.

**Formula.** Two shared params, applied as a wet/dry weight per partial with smoothstep edges so
the boundary does not click:

```
w(r) = smoothstep(Lo − E, Lo + E, r) · (1 − smoothstep(Hi − E, Hi + E, r))     E = 2 partials
amp_i, ratio_i, phase_i  ←  lerp( original_i , morphed_i , w(r_i) )
```

Note the lerp is on **amplitude, ratio and phase separately** — never on the phasor. Lerping a
phasor toward a rotated copy of itself is a **comb filter with an infinite null at mix = 0.5**
`[MEAS, 13-PAPERS §A4]`. This is a real trap and it is exactly what Vital's own offline
`PhaseModifier::multiplyAndMix` walks into.

**Parameter.** `Range Lo` **1 … 256**, `Range Hi` **8 … 512**, both **log-mapped**, shared across
all modes exactly as `Amount` is. Enforce `Hi ≥ Lo + 4` (§4). Defaults `Lo = 1`, `Hi = 512`
(= today's behaviour, so no preset changes).

**CPU class:** BAKE, two comparisons per partial. Immeasurable.

**Reuses:** the `extract()` loop; nothing else changes.

**Risk:** it multiplies the test matrix (fb425's law: sweep the **full** matrix, it is finite).
Budget **7 modes × 3 window settings × the existing amount sweep**.

---

### 3.3 — FOCUS (energy-conserving parity)

**One line:** removes odd, even or non-octave partials by **depositing their energy on the surviving
neighbour**, so the sound changes register without getting quieter.

**Formula.** For `Odd` (move a fraction `a` of each even partial's *energy* down to its odd
neighbour):

```
for each even h:
    A[h−1] ← sqrt( A[h−1]² + a·A[h]² )
    A[h]   ← sqrt( 1 − a )·A[h]
```

`Σ A²` is invariant. Four targets, per Zebralette's `Spectral Focus` `[DOC S4 p.20]`:
**Odd · Even · Octaves** (only `h ∈ {1,2,4,8,16,…}` survive → organ) **· Fundamental**
(level of `h = 1`, 0 → ~150 %).

**Why it matters:** this is *redistribution*, not attenuation, and that is the whole point.
`HarmonicEngine::CULL` (`:732-748`) is a sieve chain (full → odd → primes → Fibonacci) that
**multiplies amplitudes** — it deletes, and it gets quieter and thinner. Focus stays loud and
changes register. The distinction is real and audible, **but the two must not share a name and
ideally not a page** (no-doubles).

**Parameter.** `Amount` 0…1 linear (the full range is useful — §4). `Target` needs a second control;
the cleanest fit with the existing chassis is to spend **four of the eight `Type` slots** on
`Focus Odd / Focus Even / Focus Octaves / Focus Fund`, rather than adding a per-mode sub-menu the
other six modes would not use.

**CPU class:** BAKE, O(P).

**Reuses:** `extract`/`writeBack`.

**Risk:** at `a = 1` with `Even` selected the **fundamental is removed** (it is an odd harmonic —
u-he warn of exactly this). Pair with `Keep Bass`.

---

### 3.4 — DIRECTIONAL SMEAR (repairs mode 4)

**One line:** one sign bit turns Terrain's symmetric blur into either shimmer (leaks upward) or
growl (leaks downward).

**Formula.** Replace the symmetric triangular kernel with a **one-pole IIR along the partial index**,
direction chosen by the sign of a bipolar amount. Vital's construction `[SRC: spectral_morph.h:217-241]`,
with the two bugs it ships fixed:

```
s = 1 − (1 − |a|)³                     (cubic knob — this is doing real work, keep it)
s = min(s, 0.995)                      🚨 FIX 1: Vital outputs SILENCE at s = 1.0 exactly
m = |X[first]|                         🚨 FIX 2: seed from the real magnitude, NOT |X[0]|·(1−s)

upward   (a > 0):  for i ascending :  m ← (1−s)·A[i] + s·m ;  A'[i] = m ;  m ×= (i + 0.25)/i
downward (a < 0):  for i descending:  m ← (1−s)·A[i] + s·m ;  A'[i] = m ;  (no tilt — see below)
phase untouched throughout
```

The `(i + 0.25)/i` factor is a **built-in +14.4 dB upward tilt** over 1024 harmonics
(`512^0.25/Γ(1.25) = 5.248` `[MEAS]`) — without it, a causal smear on a `1/h` spectrum just goes
dull. **Downward smear needs the mirror-image compensation, not this one** — a downward one-pole on
`1/h` *brightens* the bottom, so it needs a downward tilt or nothing at all. `[INFERRED — measure it
both ways before choosing.]`

**The two Vital bugs are real and in shipped code.** At `a = 1.0` exactly, `amp` is seeded at
`|X[0]|·(1−1) = 0` and `lerp(·, amp, 1) = amp`, so the recursion is pinned at zero forever and the
oscillator is **silent** `[MEAS, numeric replay]`.

**Add the missing renormalisation.** `SpectralMorph::Smear` has **no gain trim** `[SRC]`, and a blur
costs **−4.32 dB RMS at W = 11** on a saw `[MEAS]`. `renderBlend` already renormalises; `Smear`
does not, so today the knob is partly a volume knob. Add `G = sqrt(Σ A_orig² / Σ A_new²)` on the
partial list.

**Parameter.** Bipolar. Given the shared-parameter chassis, the clean answer is a **`Direction`
menu shared by all modes** (`−` / `off` / `+`) rather than making `SPECTRAL_AMT` bipolar for one
mode only — mixed knob semantics across a shared parameter is how the fb373 class of bug happens.
See the fork in §6.3.

**CPU class:** BAKE, O(P), **width-independent**. 1.60 µs / 512 harmonics; 25.7 µs / 16-frame spec
`[MEAS]` — **5× cheaper than the current triangular W = 11 (8.36 µs / 133.7 µs) and unlimited in
reach.**

**Reuses:** `Smear`'s existing loop and snapshot discipline.

**Also, while in there:** `Smear` welds a rolloff + a blur + a phase scatter to one knob, with three
different curves. And its scatter of `a·4.1` radians **exceeds π**, so past `a = π/4.1 = 0.766` the
knob only re-randomises — it does not scatter *more*. `a·π` is the saturation point `[DERIVED]`.
Split the three mechanisms or clamp the scatter; do not leave it as is.

---

### 3.5 — FORMANT (Harmonics Shift) — the repair of `Vocode`

**One line:** move the spectral **envelope** while every partial stays exactly on the harmonic grid —
formant shift that tracks the played note, which is what `Vocode` was supposed to be.

**The defect it fixes.** `Vocode` takes each partial's frequency as `f = r · F0` with
**`F0 = 130.81 Hz` hard-wired** (`SpectralMorph.h:165`). The envelope does **not** track the played
note. On a single cycle there is no absolute pitch until it is played, so today the mode is shifting
formants relative to a fiction. Both Vital and Phase Plant get this right and do it differently:

- Vital multiplies the formant shift by `voice_increment · 2048` at call time, so
  `s_total = 2^(1−2a)·f_played / f0_of_the_sampled_note` — **formants stay at fixed absolute Hz as
  you play up the keyboard** `[SRC + DERIVED]`.
- Phase Plant's Formant Filter has **no F0 at all** — F1 ∈ **[200, 900] Hz**, F2 ∈ **[500, 2500] Hz**,
  Q ∈ **[2, 16]**, a 2-D pad over two *absolute* frequencies `[MEAS: auval]`. Physically correct: a
  vocal-tract resonance does not move when you sing a different pitch.

**Formula.** Two variants; build the second, keep the first as the cheap path.

*(a) Parametric (fix `Vocode` in place, no envelope estimation):* keep the Lorentzian bank
(`Wavetable::lorentzian`, `Wavetable.h:1489-1493`) but express formant centres in **harmonic index
as a function of the played note**, and drop the constant:

```
h_F = F_hz / f0_played           (f0 from the voice, not 130.81)
env(r) = 0.012 + 2.10·L(r, h_F1, Q1) + 1.55·L(r, h_F2, Q2) + 1.05·L(r, h_F3, Q3)
```
⚠️ **This requires the played note at bake time, and the bake is per-patch, not per-voice.**
That is the hard part — see §6.4.

*(b) Envelope resample with harmonic re-snap (Falcon `HARMONICS SHIFT`, `[DOC S2 p.120]`):*

```
E(x)  = the smooth envelope through the partial amplitudes        (see below)
k     = 2^(st/12)
A'[h] = E(h / k)                        ← partials NEVER move; only their levels do
   or  A'[h] = E(h/k) · (A[h] / E(h))   ← preserve the fine structure, shift only the envelope
```

**Getting `E(x)`, in ascending cost:** (1) a log-frequency **3-partial moving max** — `[INFERRED]`
that this is enough, **must be measured**; (2) Vital's shortcut: don't estimate an envelope at all,
**resample the harmonic array with parity preserved** (`evenOddVocodeMorph`, `spectral_morph.h:307-347`)
— `index_start -= (i + index_start) % 2` snaps to the same parity, which is what stops a formant
shift turning a square (odd only) into a saw, and the output is multiplied by `s` as the analytically
correct energy compensation for stretching the envelope over `s`× the axis; (3) cepstral true-envelope
(Röbel & Rodet, DAFx-05) — 2 extra FFTs + iterations, **unmeasured on a 2048 single cycle** `[GAP]`.

**Parameter.** `Shift` in **semitones, −24 … +48** (Falcon's published ceiling), bipolar, 0 = identity.
Plus a `Snap` toggle: snapping (this) vs free ratios (which the two-bin phasor split already renders).

**CPU class:** BAKE, O(P) with a linear envelope interpolation.

**Reuses:** `Wavetable::lorentzian`; the parity-snap idea; `buildFromSpec`'s two-bin split.

**Collision:** it *is* `Vocode`, done correctly. Best outcome is that `Vocode` is **rebuilt on this
mechanism**, not that a good mode appears beside a broken one. That changes every existing patch
using `Vocode` — see §6.4.

---

### 3.6 — BRILLIANCE (the mip ladder as a playable filter)

**One line:** bias the anti-aliasing ladder darker on purpose — a >100 dB/oct brick wall with no
resonance, no phase shift, that tracks pitch exactly, for one integer add per block.

**The insight, and two vendors state it outright.** Massive X's Standard-mode `Filter` works
*"by scanning through the set of band-limited waveforms that are usually assigned to specific
pitches"* `[DOC S7 p.63]`. Zebra2: *"because in reality the 'filter' code only manipulates
amplitudes, its slope is more than 100 dB/octave"* `[DOC S3 p.33]`. Terrain's ladder is **finer than
Massive X's** — 34 levels, `2^(1/6)` spacing, `kMipMaxHarmonics = {512, 456, 406, 362, 323, 287,
256, 228, 203, 181, 161, 144, 128, 114, 102, 91, 81, 72, 64, 57, 51, 45, 40, 36, 32, 29, 25, 23, 20,
18, 16, 8, 4, 2}` (`Wavetable.h:79-81`) — and it is already built, already resident (4.46 MB/table),
already selected per block by `mipLevelForPhaseIncrement()`.

**Formula.**

```
lvl = mipLevelForPhaseIncrement(inc)
lvl = clamp( lvl + round( brilliance_bias ), lvl, 30 )        ← DARKER ONLY, never brighter
```

Brightening is forbidden: a lower mip index than the increment justifies **aliases**. Say so in the
UI — the knob's default is its maximum.

**Parameter.** `Brilliance` (Zebra2's word) or `Focus`. **Never `Filter`.** 0…1 mapped to
`0 … (30 − lvl)` steps, modulatable.

**CPU class:** **READ** — one `int` add and a clamp, per block, per osc. Nothing else changes.

**Reuses:** `kMipMaxHarmonics`, `mipLevelForPhaseIncrement` (`Wavetable.h:472-482`), the per-block
mip selection already in `SynthVoice.h:2558-2561`.

**Risk — and it is the naming, not the DSP.** Terrain ships 27 filter types, two independent filter
slots, and a 94-engine Filter FX device. The defence is that this thing **cannot resonate, cannot
self-oscillate, has no phase shift, and can only remove harmonics the ladder already removes for
other notes** — it is a sound no analogue-modelled filter can make. But if it is labelled "filter"
it will be judged as one and found wanting.

**Second risk:** the ladder's top is coarse — the last three steps are `16 → 8 → 4 → 2`, octave
jumps. **Stop the knob at index 30 (16 harmonics remaining)** or document the lumpiness. Above 30 it
is a sine and a rumour.

---

### 3.7 — SKEW (frequency-dependent frame offset)

**One line:** each harmonic reads a *different frame* of the table — the frame axis becomes a
frequency-dependent time axis, and `renderBlend` mathematically cannot produce it.

**Formula.** Per output frame `F` (of 16) and per harmonic `h`, with `t₀ = F/(N_f − 1)`:

```
shift_scale = log2(h) / 11
x           = ( t₀ + s·shift_scale ) · 0.5
base        = 1 − 2·frac(x)                       ∈ (−1, 1]
frame(h)    = ( 1 − |base| ) · (N_f − 1)          ← PING-PONG FOLD, not a wrap
from = min(⌊frame⌋, N_f − 2) ,  t = min(1, frame − from)
A'[h] = lerp( A_from[h], A_{from+1}[h], t )
φ'[h] = φ_from[h] + t·arg( conj(X_from[h])·X_{from+1}[h] )     ← shortest arc, NOT a phasor lerp
```

**The fold is why it never clicks:** `1 − |1 − 2·frac(x)|` is a continuous triangle, so `frame(h)` is
C⁰ in both `t₀` and `s`. A wrap would discontinuity-jump every span `[SRC: Vital]`.

**⚠️ Do not copy Vital's interpolation here.** Vital lerps `(re, im)` of a unit phasor, which
shortens the phasor mid-interpolation and dips where two frames disagree; Vital accepts it because
it runs per-harmonic every 7 ms on the audio thread and `atan2` is not affordable there. **We are in
a bake. Use the shortest-arc form above** — it is the same law as §2.5 R2 and it costs nothing at
20 Hz.

**Parameter.** `Amount` → `s = 4a²` (squared, because the useful range is bunched at the bottom).
**Not Vital's `16a²`** — see §4 for the derivation: our frame axis is 16× coarser, so Vital's range
is pure scramble here.

**CPU class:** BAKE, O(F·P) — 16 frames × ≤512 partials of lerp + one `atan2` per partial per frame.
Budget ~2 ms per spec at 512 partials `[INFERRED from the 1.77 µs/512-harmonic phase-rotation
measurement × 16 frames × the extra `atan2`]` — **measure it**; it is the most expensive proposal
here and the only one that could be a meaningful fraction of the (post-vDSP) bake.

**Reuses:** the whole-spec view `apply()` already has — this is the only mode that needs all 16
frames simultaneously, and `apply()` is the only place in the system that has them.

**Risk:** medium-high. The 16-frame axis is the binding constraint (§6.8), and on a 256-frame import
that collapses to 16 the skew is coarse. Gate: on a 2-frame test table (all energy at h=3 in frame 0,
all at h=7 in frame 1) prove that at `s > 0` the **high harmonics read a different frame from the
fundamental**, and prove the gate fails on today's code.

---

### 3.8 — ORDERED SCATTER

**One line:** a seeded random re-tuning of the partials that is **forbidden from reordering them**,
so the timbre detunes into bell/clangour instead of collapsing into noise.

**Formula.** `[INFERRED — the mechanism is mine; the constraint is u-he's]`

```
u_h ~ U(0,1) from a frozen table, indexed by (seed, h)
margin_h = min( r_h/r_{h−1} − 1 , r_{h+1}/r_h − 1 )        ← how far it can move without crossing
r'_h = r_h · ( 1 + d · (2u_h − 1) · margin_h )
```

Monotonicity is guaranteed **by construction**, which is the entire point: Zebra2's `Turbulence`
shuffles freely and turns everything into noise; Zebralette's `Ordered` range keeps the order and
stays musical `[DOC S4 p.14]`.

**Parameter.** `Scatter` amount `d` 0…1 (the ceiling is structural — §4) **plus a `Seed` stepper
over 100 fixed patterns**, exactly as u-he do it. **The seed selector is the distinctive part:** a
patch must recall, so 100 named seeds beats a "randomise" button. Build the table once, immutably,
Vital-style (`RandomValues`, `synth_oscillator.h:34-56`: a `std::mt19937` with a frozen `kSeed`,
generated once process-wide, ~70 kB — determinism *and* zero RNG cost at use time).

**CPU class:** BAKE, O(P). Table lookup; the RNG cost is zero.

**Reuses:** the energy-preserving phasor snap (this mode produces non-integer ratios, which is
exactly what `Wavetable.h:126-155` was written to render); `RandomAmplitudes`' frozen-seed pattern.

**Collision:** `RandomAmplitudes` randomises **amplitudes** with one fixed seed. This randomises
**ratios** with a *selectable* seed. Different axis, and the selector is the differentiator.

**While in there — fix `RandomAmplitudes` too.** Vital's `g(r, s) = (1+s)·max((1−s) − s·r, 0)` is an
**exact identity at `s = 0` for every `r`** `[SRC]`. Terrain's `mul = 0.02 + r²·5.5` is never 1, so
it *cannot* be continuous at `amount → 0` — that is one half of the step-at-0⁺ defect, and it is a
five-line fix. Vital's version also **decimates**: `g > 0` only when `r < (1−s)/s`, so the surviving
bin fraction is `((1−s)/s + 1)/2` — 100 % at `a = 0`, 13 % at `a = 0.25`, **3.3 % at `a = 1`**, with
`(1+s)` makeup holding the loudness. **The sparse end is the interesting part and Terrain does not
have it.**

---

# 4. RANGES AND CEILINGS

Max's rule: **the maximum is where it stops being USEFUL, not where it stops being clean.** Real
numbers, with the reason each one is the ceiling.

### 4.1 Disperse — `c_max = 0.08 rad/harmonic²`

| `c` | crest (256-h saw) | τ at h=512 | wraps |
|---|---|---|---|
| 0.005 | 4.08 `[MEAS]` | −1590 samples | 0.78 cycles |
| 0.050 (Vital's max) | **2.18** `[MEAS]` | −15,900 | 7.8 cycles |
| **0.080 (recommended max)** | ~2.0 `[INFERRED]` | **−25,450 samples = 12.4 cycles** | |

Why 0.08 and not more: `τ(h) = −(N/π)·c·(h − 24)`, so `c = 0.08` at `h = 512` is
`651.9 × 0.08 × 488 = 25,450` samples. **Past ~12 cycles of spread the low harmonics have wrapped
too, and every further increment produces the same wash** — the character stops changing, which is
the definition of "stops being useful". Vital's 0.05 is audibly short of that. `c = 0.02` is where
most of the *musical* travel already is (crest 3.40); the `^2.5` curve is what puts that at
mid-knob instead of at 5 %.

### 4.2 Focus — full range is useful, `a = 1` is the ceiling and it is musical

Removing **every even harmonic from a saw costs only −1.25 dB RMS** `[MEAS]` (the evens carry
`Σ1/(2k)² = π²/24` of `Σ1/h² = π²/6` — exactly a quarter of the power). So `a = 1` is a strong
timbral move that is almost free in level. **No taper needed; linear knob, full range.** This is the
one candidate where "no playing safe" costs nothing.

For `Octaves`: at `a = 1` only `h ∈ {1,2,4,8,16,…}` survive — 10 partials out of 512, an organ. Still
useful. Ceiling = 1.0.

### 4.3 Formant / Harmonics Shift — useful **−24 … +24 st** with a 512 cap

The mechanism is `A'[h] = E(h/k)`, `k = 2^(st/12)`. Shifting **up** by `k` needs source envelope up
to `k·h`, so with a 512-partial source:

| shift | `k` | highest output partial with real source support |
|---|---|---|
| +12 st | 2 | 256 |
| **+24 st** | 4 | **128** |
| +36 st | 8 | 64 |
| +48 st (Falcon's published max) | 16 | **32** |

**Above +24 st the top three quarters of the spectrum is extrapolated from nothing** — it goes
silent or has to be flat-held, and either way the mode stops behaving. **+24 is the honest ceiling at
`kMaxPartials = 512`; +36 becomes honest only at 1024.** Downward, `k = 0.25` (−24 st) crushes every
formant below `h = 128` — dark and vowelly, still useful; **−36 st is where it collapses.**

Recommended: **−24 … +24 st**, and say in the release note that Falcon's +48 is reachable only
because Falcon runs a live additive engine with a user-set partial count, not a 512-cap bake.

### 4.4 Directional Smear — clamp `s ≤ 0.995`; the knob saturates at `a ≈ 0.85`

Reach is `τ = 1/ln(1/s)` partials with `s = 1 − (1−|a|)³`:

| `a` | `s` | reach τ (partials) |
|---|---|---|
| 0.25 | 0.578 | 1.8 |
| 0.50 | 0.875 | 7.5 |
| 0.75 | 0.984 | 62 |
| **0.85** | 0.9966 | **296** |
| 0.90 | 0.999 | 1000 |
| 1.00 | 1.000 | **∞ — SILENCE in Vital's shipped form** |

With a 512-partial ceiling, a reach of ~300 already carries the low partials' value across most of
the spectrum. **Past `a ≈ 0.85` nothing new happens**, and at `a = 1.0` exactly Vital is silent.
**Clamp `s` at 0.995 (τ = 200) and seed the recursion from the real first magnitude.** The clamp is
the ceiling; it is a genuine musical maximum, not a safety margin.

Also: `Smear`'s existing phase scatter of `a·4.1` radians **exceeds π at `a = 0.766`**; past that
the knob only re-randomises. `a·π` is the saturation point.

### 4.5 Skew — `s_max = 4`, **not** Vital's 16, and here is the arithmetic

Displacement in **frames** is `s · log2(h)/11 · (N_f − 1)`. On Vital's 257-frame table at `h = 1024`,
`s = 16`: `16 × 10/11 × 256 = 3723` frames = **14.5 sweeps of the table**. On Terrain's **16-frame**
spec at `h = 512`:

```
displacement(h=512) = s · (9/11) · 15 = 12.3·s frames        one full sweep = 15 frames  ⇒  s = 1.22
```

| `s` | sweeps of the 16-frame axis at h=512 |
|---|---|
| 1.22 | 1.0 |
| **4** | **3.3** |
| 8 | 6.6 |
| 16 (Vital's max) | **13.1 — indistinguishable scramble on 16 frames** |

**Past ~3 sweeps of a 16-frame axis the result is noise, because 16 frames cannot resolve the
folding.** `s = 4a²` puts one full sweep at `a = 0.55` and 3.3 sweeps at the top. If
`kNumFrames` is ever raised (§6.8), scale `s_max` proportionally: `s_max ≈ 4·(N_f/16)`.

### 4.6 Brilliance — stop at mip index **30 (16 harmonics remaining)**

The ladder's last four entries are `20, 18, 16, 8, 4, 2` — the tail is **octave** jumps, not
sixth-octave. Steps 31–33 remove three quarters of what is left per click. **16 harmonics is where a
tone control stops being a tone control**; below it you have a sine and an artefact. Clamp the bias
so `lvl ≤ 30`.

### 4.7 Ordered Scatter — `d = 1` is the structural ceiling

At `d = 1` each partial can move all the way to (not past) its neighbour. That IS the maximum of the
mechanism — clangour, maximal detune, order preserved. There is nothing beyond it that does not
break monotonicity, and breaking monotonicity is the thing that turns it into noise. **Full range,
linear knob.**

### 4.8 The existing modes' ranges, re-examined against the field

| Mode | Today | Finding |
|---|---|---|
| **Harmonic Stretch** | `s = 1 + 5.5a` ⇒ **s ∈ [1, 6.5]**, unipolar, linear | Falcon states the same law with musically-named landmarks: `s = 1` harmonics, `s = 2` **odd only** (square-flavoured), `s = 0.5` **interlaced with the sub-octave's odd partials** `[DOC S2 p.120]`. **Terrain cannot reach `s < 1` at all** — the whole compression half is unreachable. And `s = 2`, the one landmark a musician would look for, sits at **`a = 0.1818`** with no detent and no label. Vital's range is `2^(8a−4) = [1/16, 16]`, **bipolar, exponential** — a stretch factor is a *ratio*, and ratios want log knobs. **Recommend: `s = 2^(8a−4)`, identity at `a = 0.5`.** |
| **Inharmonic Stretch** | `p = 1 + 2.3a` ⇒ **p ∈ [1, 3.3]**, one-sided | Vital's effective power is `p ∈ [−0.2, +2.2]`, bipolar `[DERIVED]`. **The negative-`p` region reverses the spectrum, and that is where the bell/metallic character actually lives.** Terrain has the boring half. Physical anchor if a defensible range is wanted: the stiff-string law `f_n = n·f₀·√(1+Bn²)` with `B ≈ 1e-4` mid-piano, `~1e-3` top octaves (Fletcher & Rossing) — **but note `HarmonicEngine::SPLAY` already ships exactly this with `B ≤ 0.138`**, so do not build it twice. |
| **Vocode** | 5 vowels welded to depth on one knob; `F0 = 130.81` | Cannot hold /a/ at 30 % depth — vowel position and depth share the knob. Phase Plant's published ranges for the same job: **F1 ∈ [200, 900] Hz, F2 ∈ [500, 2500] Hz, Q ∈ [2, 16]** `[MEAS: auval]`. §3.5. |
| **Data Compress** | quantise 64→2 levels; decimate keep-1-in-4 | Decimation is on **list index**, not harmonic number, so on inharmonic sources (D50Bell, GlassHarmonics) it thins whichever partials happen to occupy those slots — arbitrary. And it duplicates `HarmonicEngine::TERRACE` (dB-domain, step 0.75→32 dB, with dither and a rising deletion floor −80→−54 dB). **Reconcile before touching either.** |
| **Spectral Phaser** | depth **and** sweep on one knob; `period = 1.7` fixed | An LFO on `amount` marches the notches *and* breathes the depth. Phase Plant splits the same idea into four orthogonal controls (Drive / Bias / Tone / Spread) — and Terrain's morph is offline, so **splitting it costs nothing**. |
| **kMaxPartials** | **96** | Raise to **512** (= `kMaxHarmonics`). Memory: `512 × 12 B = 6.1 kB` of partials per frame (vs 1.1 kB), **98 kB per 16-frame spec** vs 17.6 kB. Trivial. This is half the fix for the step at `0⁺`. |

---

# 5. WHAT NOT TO BUILD, AND WHY

**1. Identity / scaled / loose phase locking (Laroche & Dolson 1999; Puckette 1995).**
These exist to repair an *estimate*. A 2048-sample single cycle is **periodic** — its DFT is not an
estimate of a spectrum, it *is* the spectrum, exactly, with no window, no leakage, no bin ambiguity.
Harmonic `h` lives in bin `h` and nowhere else. **There is no "region of influence" and nothing to
lock.** Implementing them would be a no-op at best. Anyone proposing them has not understood what
they are for. `[13-PAPERS §C4]`

**2. A time-domain analysis window (Hann etc.) to blur the spectrum.**
The windowing/convolution duality is real, but a non-constant `w[n]` **destroys the periodicity** —
the cycle stops being a cycle and looping it produces a wrap discontinuity. `00-INVENTORY §5.4`
already states the rule and it is right. The legitimate version of the same idea on a harmonic grid
is a 3-tap frequency convolution `[0.25, 0.5, 0.25]`, which is §3.4 at `W = 1`.

**3. Shepard tone.**
It is in Vital (`kShepardTone`), Pigments (`Phi`), and Meld (`Shepard's Pi`) — which makes it **the
single most widely shipped spectral idea Terrain lacks, and therefore not a differentiator.** It
also needs machinery we do not have: a specially built table (frame 0's spectrum on even harmonics
only), a per-voice `phase_inc × 2^(−shift)` pitch multiplier, and a `doShepardWrap` that applies the
×2 / ×½ at a buffer boundary where a crossfade hides it — i.e. **audio-thread, per-voice pitch
state**. On a 16-frame spec the octave-copy trick is lossy on top of that. Build it only if a real
Risset glissando is a named product goal, and then build it properly.

**4. Every VOICE-class idea — but route them, don't just refuse them.**
Zebralette `Spectral Decay` / `Twinkles` / `Dissociate` / `Wild Randomness`; Massive X `Random` /
`Jitter`; Falcon `Beating`; Ableton Spectral Resonator's `Granular` per-partial modulation. All are
functions of **note time** or a **per-note seed**, and Terrain publishes **one shared table to every
voice**. They cannot ride it. **But Terrain already has a real-time per-voice additive engine —
`HarmonicEngine`, ≤512 partials — and that is exactly where they belong.** Refuse them in
`SpectralMorph`; propose them for HARM.

**5. Spectral Lanes (bake two tables split by parity, route to the Splitter).**
Conceptually excellent and it uses fb444's Splitter rather than duplicating it. But: **2 × 4.46 MB
of RAM per osc, and 2 × 21 ms of bake = a ~42 ms message-thread burst, ~84 % duty per osc at the
current 20 Hz cadence.** Not viable today. **Revisit after the vDSP swap** (§1.5.1), when the bake is
~1 ms and the whole objection evaporates.

**6. The wavetable as a waveshaper (Massive X `Internal Phase = Off`).**
Highest ceiling in the corpus — every spectral morph mode instantly becomes a *distortion character*,
and the transfer function is the user's own baked spectrum, which no distortion device can be. **And
the highest real DSP cost:** a waveshaper's output is **not band-limited**. The mip ladder protects
the *oscillator*, not the *shaper*, so `readCycle` on an arbitrary input **will alias**. It needs
oversampling or ADAA (we have the machinery in `Shapers.h`) plus a hard input band-limit. **Prototype
it as a WARP source and measure aliasing on the real AU before it is a feature.** Not a spectral mode.

**7. Low Pass / High Pass as morph modes (Vital `kLowPass` / `kHighPass`).**
Terrain ships 27 filter types, two filter slots, and a 94-engine Filter FX device. The one thing
Vital's version has that ours do not — a >100 dB/oct brick wall with no resonance and no phase shift
— **is delivered for free by §3.6 Brilliance**, using a ladder we already built. Do not spend two of
eight `Type` slots on it. (Do steal one detail if anything similar is ever built: the **one-harmonic
linear taper at the boundary**, `poly_float(1,1,t−1,t−1)` — two multiplies, and it is the entire
difference between "filter" and "stair".)

**8. Fractional-order spectral tilt as a new mode.**
`HarmonicEngine::KEEL` is already a pivot tilt in dB/octave, up to 9 dB/oct, with a moving pivot.
Falcon's fractional order 0.0–8.0 is a close cousin. **Extend KEEL; do not add a mode.** (`14-OTHERS
§9.8` reaches the same conclusion and flags it honestly as *"this may be a refinement of something
Terrain already has"*.)

**9. Peak-matched frame morph (u-he `morph1` / `morph2`).**
The most interesting unshipped idea in the survey, and the least established: u-he state outright
*"Details of the 'morph' types… will be explained at a later date."* `[DOC S6 p.7]` The maths in
`14-OTHERS §9.2` is **INFERRED from four parameter names** plus Serra & Smith (1990). It also
degenerates to a plain crossfade on noise-like tables, and it needs frames baked *between* the
source frames — i.e. it wants `kNumFrames > 16`. **Not now. Revisit with §6.8.**

**10. Any further tuning of `renderBlend`'s σ, exponent, or tap count.** §2.4. The ceiling is the
mechanism, not the parameterisation.

**11. A fourth hand-rolled FFT.** Three already exist with three normalisation conventions
(`Wavetable.h`, `GeodeEngine.h`, `BlendEngine.h`). Adding a fourth is a defect. `[00-INVENTORY §5.6]`

**12. Dry/wet on a phase rotation, ever.** `lerp(X, X·e^{jφ}, m)` is **not** a phase operator — it is
`|X|·|1 − m + m·e^{jφ}|`, a comb filter, with a **total null at `m = 0.5`** `[MEAS]`. Vital's own
offline `PhaseModifier` walks into this. **Interpolate the angle (`φ_applied = m·φ_target`), never
the phasor.** Terrain's `Smear` already does the correct thing (`p.phase += scatter`) — keep it.

**13. Copying Serum's FM range philosophy by reflex.** Measured: Serum's FM warp is **true frequency
modulation with a fixed Hz deviation**, `Δf ≈ 58 kHz · warp⁴`, **independent of the played note** —
so at 100 % on a C4 the modulation index is β ≈ 224 (a DX7 operator maxes near 12–13) and Carson
bandwidth is ~117 kHz, **2.4× the sample rate**. And it is **3.35 dB QUIETER in RMS** than warp 0
while its centroid moves 814 Hz → 13,056 Hz — *"earsplitting" is a distribution problem, not a level
problem.* Terrain may well want that reach, but "the deviation does not track the note" is a
deliberate design choice that makes the same knob mild at C7 and catastrophic at C1 — which
conflicts with "params evolve 0→100". **It is a fork (§6.7), not a default.**

---

# 6. OPEN QUESTIONS FOR MAX

Genuine forks only. Each has two defensible answers and this document deliberately does not pick.

### 6.1 — Does Blur stay modulatable?
§2.5 gives two fixes. **R1 (Align Frames)** is free, keeps blur exactly where it is — a per-block,
LFO-modulatable knob — and removes the artefact that makes it dull. **R2 (true magnitude-domain
blur)** removes the "can only subtract" property outright, but it is a **bake-class** operation:
blur would become a ~20 Hz parameter like `Amount`, with the same 21 ms-per-move cost (≈1 ms after
the vDSP swap). Kilohearts made exactly this call and pay **zero** runtime cost by making frame
blend an offline commit; u-he pay for it with a user-visible rebuild-rate knob.
**Fork: R1 only (blur stays a live modulation destination), or R1 then R2 (blur becomes richer but
coarser)?** Recommendation: R1 now, decide R2 after hearing R1.

### 6.2 — Is Align Frames ON by default, and does it change existing presets?
A pure circular time shift changes **no frame's own sound** — but it absolutely changes what
`blur = 0.5` sounds like on every preset that uses blur, because it stops the cancellation. That is
the *point*, and it is also a behaviour change to shipped patches.
**Fork: (a) globally ON, accept that blur sounds different (brighter) everywhere; (b) a per-preset
version flag — ON for new, OFF for loaded; (c) a per-osc switch, default OFF.**
(b) is the most conservative and the most bookkeeping.

### 6.3 — Bipolar per mode, or a shared `Direction` control?
`Disperse`, `Focus`, `Formant`, `Directional Smear` and the recommended `HarmonicStretch` /
`InharmonicStretch` re-ranges all want the identity at the **middle** of the knob. Today all seven
modes share one `Amount` whose identity is at **0**.
**Fork: (a) make `SPECTRAL_AMT` bipolar per-mode (mixed semantics on one shared parameter — this is
the shape of the fb373 class of bug); (b) add one shared `Direction` menu (−/off/+) that every mode
may use, keeping `Amount` unipolar everywhere; (c) move the identity to 0.5 for ALL modes,
Vital-style, and accept that every existing preset's Spectral knob means something new.**
(c) is the cleanest engineering and the most disruptive.

### 6.4 — Is `Vocode` repaired, or replaced-and-deprecated?
`F0 = 130.81 Hz` is a documented defect: the formant envelope does not track the played note.
Fixing it changes every patch using the mode. **And the deeper problem: the fix needs the played
note, but the bake is per-patch, not per-voice.** Three ways out:
(a) express formants in **harmonic index** and accept that they then track the note *perfectly*
(i.e. they stop being formants and become fixed harmonic bumps — physically wrong, musically maybe
fine); (b) build §3.5's **envelope-resample** version, which is note-relative by construction and
needs no F0 at all; (c) bake per-note-range variants and pick by mip-style selection (expensive,
novel, probably wrong).
**Fork: repair in place (b), or ship `Formant` as a new mode and leave `Vocode` frozen for preset
compatibility?**

### 6.5 — `kMaxPartials` 96 → 512: ship the behaviour change?
This removes a measured **−8.7 … −25.1 dBr step** on Pulse / Square / Triangle / Rise /
SpectralSweep the instant any mode engages. It is unambiguously a bug fix. It is also a **sound
change to five factory tables** in every existing preset that morphs them. **Ship it, or gate it
behind a preset version?**

### 6.6 — `juce::dsp::FFT` or `vDSP` directly?
The swap is worth **36×** and takes the bake from ~21 ms to under 1 ms `[MEAS]`. `vDSP` is faster and
Apple-only; `juce::dsp::FFT` is portable **and uses vDSP on Apple anyway**, but it means
`Wavetable.h` takes a real JUCE dependency where today it includes `juce_core` only for
`jlimit/jmax/jmin` (which is why the offline harness needs a 3-function shim). The monorepo has a
Windows path.
**Fork: portability (`juce::dsp::FFT`) or a hand-rolled vDSP wrapper with a scalar fallback?**

### 6.7 — Do we want Serum's *reach*, and do we want it note-independent?
Measured, Serum's FM tops out around **58 kHz of peak deviation** on a note that might be 32 Hz, with
a `warp⁴` taper, and it **does not track the note**. Its top is not a musical maximum — the useful
zone is 20–60 % and 100 % is deliberately unusable. Terrain's "no playing safe" rule says the max is
where it stops being *useful*; Serum's says the max is past where it stops being *anything*.
**Fork: do we widen Terrain's ranges toward Serum's philosophy (steep taper, unusable top), or hold
the line that 100 % must still be a sound?**

### 6.8 — Do we raise `WavetableSpec::kNumFrames` above 16?
It is the binding constraint on §3.7 Skew (§4.5 derives that Vital's range is 13× too wide for our
axis), on peak-matched morph (§5.9), and on every 256-frame import (which collapses to 16 the moment
a mode engages). Raising 16 → 64 multiplies the bake **linearly** (4× the iFFTs) — unaffordable
today, ~4 ms after the vDSP swap. It also multiplies `FrameSpec` memory by 4 (98 kB → 392 kB per
spec at 512 partials — still trivial).
**Fork: hold at 16 and scale every frame-axis mode to it, or raise to 32/64 after the FFT swap and
let imports keep more of their detail?**

---

# 7. SOURCES

## 7.1 Terrain source (this worktree, read line-by-line)
`Source/SpectralMorph.h` (306 ln — the 7 modes) · `Source/Wavetable.h` (2018 ln — storage, 34 mips,
`buildFromSpec`, `renderBlend`, `toSpec`, the FFT pair, `lorentzian`, `besselJ`,
`normalizeMipLevels`, `amplifyFramesInPlace`) · `Source/WavetableBank.h` (179 ln — 30 factory specs)
· `Source/SynthVoice.h` (6266 ln — WARP, `warpRateMul`, the blend read, mip selection) ·
`Source/Shapers.h` (169 ln — the fold + ADAA) · `Source/PluginProcessor.{h,cpp}`
(`rebuildMorphIfNeeded`, `MorphSlot`, the 60 Hz timer, `spectralEffAmt_`) · `Source/GeodeEngine.h`
(STFT peak-track analyser, 96-partial cap) · `Source/HarmonicEngine.h` (`applySculpt`: KEEL / SPLAY /
CULL / TIDE / TERRACE / CLANG; `applyNyquistTaper`; `thinToBudget`; `renorm`; `postRoot`) ·
`Source/SpectrumAnalyzer.h` · `Source/BlendEngine.h` · `Tests/blur_audit.cpp`.

## 7.2 Reference implementations read as source
- **Vital**, Matt Tytel, GPL-3, `github.com/mtytel/vital`, commit
  `636ca0ef517a4db087a6a08a6a8a5e704e21f836` (2022-04-20, v1.0.6).
  `src/synthesis/producers/spectral_morph.h` (all 11 morph kernels, 53–477) ·
  `synth_oscillator.{h,cpp}` (`RandomValues` 34–56; enum 100–113; `setSpectralMorphValues`
  1079–1118; `setPowerDistortionValues` 491–501; `computeSpectralWaveBufferPair` 766–803;
  `kWavetableFadeTime` 47; Shepard wrap 637–714) · `lookups/wavetable.{h,cpp}` (mag/phasor/phase
  split 160–178; `postProcess` phase repair 111–158) · `lookups/wave_frame.h` ·
  `common/fourier_transform.h` · `common/synth_parameters.cpp` (490, 510–515) ·
  `common/wavetable/*` (`wave_source.cpp:88-161` frequency interpolation; `phase_modifier.cpp:22-82`;
  `frequency_filter_modifier.cpp:96-113`; `shepard_tone_source.cpp:34-37`; `file_source.cpp:110-370`;
  `wavetable_creator.cpp`; `wavetable_keyframe.cpp:26-39`) ·
  `interface/look_and_feel/synth_strings.h:318-331`.
  ⚠️ **Vital has no manual documenting any of this.** `vital.audio/manual` returns 404. Every Vital
  number in this bible is from source or from measurement.
- **CDP8**, Composers Desktop Project, `github.com/ComposersDesktop/CDP8` —
  `dev/blur/blur.c` (`specavrg` 94–134 = frequency-axis boxcar; `specspread` 319–340 = lerp toward
  the envelope + renormalise) · `dev/blur/ap_blur.c` (`do_the_bltr` 773–800 = frame-axis **ramp**,
  not a mean) · docs `composersdesktop.com/docs/html/cblur.htm`.
- **synthahol-phase-plant**, Sheldon Young, `github.com/softdevca/synthahol-phase-plant` — a
  *format* authority (what the preset stores, in what units), **not** a DSP authority.
  `disperser.rs` (`amount: u32` — the smoking gun), `wavetable_oscillator.rs`, `formant_filter.rs`,
  `phase_distortion.rs`, `frequency_shifter.rs`, `granular_generator.rs`, `io/generators.rs`.

## 7.3 Vendor documentation
- Xfer Records, **Serum 2 User Guide** v2.0 / manual 1.0.0, 2025-03-17, 355 pp — pp. 39, 50–51,
  55–57, 108, 117–122, 276, 279–294, 347. ⚠️ **The shipped manual documents v2.0; the installed
  plugin is v2.1.4 — every spectral warp in §1.2's "34" is absent from it.**
- Xfer Records, **Serum Manual** v1.2.3, March 2019 (p.16, the FM directionality restriction).
- Xfer Records, **Serum2 VST3 binary v2.1.4** (arm64) and **Serum v1.368** — string / RTTI analysis,
  offsets cited in `11-SERUM2`. Mode names and orderings are hard fact; **behaviours are INFERRED.**
- Kilohearts, **official docs** (there is no PDF; the web pages *are* the manual) —
  `/docs/phase_plant`, `/docs/wavetables`, `/docs/snapins`, `/docs/disperser`, `/docs/convolver`,
  `/docs/multipass`; **changelog** v1.7.4 (2019-07-02), v1.8.4 (2020-03-23), v2.1.1 (2023-10-04),
  v2.4.5 (2025-12-11, *"Reduced some artifacts in wavetable interpolation"*).
- Arturia, **Pigments 6.0.0 manual** — pp. 85–86, 91–93, 117–126, 130, 185–186.
- UVI, **Falcon 3.0.1 manual** — pp. 117–121, 133–134 (the `fn = f(1 + n·dissonance)` law, the
  fractional filter order, Keep Bass, Harmonics Shift).
- u-he, **Zebra2 user guide** (pp. 32–40, the 24 spectral effects, Resolution, SpectroMorph vs
  SpectroBlend) · **Zebralette 3 user guide** v3.0, 2025-12-04 (pp. 12–14, 18–22, 27, 31–32) ·
  **Hive user guide** (pp. 43–45, the Interpolator) · **Hive Wavetables / UHM reference**
  (pp. 2, 5–7, 10).
- Native Instruments, **MASSIVE X manual** v1.4, 2023-03-17 (pp. 57–73).
- Ableton, **Live 12 Instrument Reference** §30.8.3, §30.13 · **Audio Effect Reference** §28.36–28.37.
- Bitwig, **User Guide — Spectral** (`bitwig.com/userguide/latest/spectral/`).

## 7.4 Papers
**Dispersion / allpass:** Van Duyne & Smith, *A Simplified Approach to Modeling Dispersion Caused by
Stiffness in Strings and Plates*, ICMC 1994, 407–410 · Välimäki, Abel & Smith, *Spectral Delay
Filters*, JAES 57(7/8), 2009, 521–531 · Rauhala & Välimäki, DAFx-06 · Abel, Berners, Costello &
Smith, *Spring Reverb Emulation Using Dispersive Allpass Filters*, AES 121, 2006 · Pekonen &
Välimäki, DAFx-09 · Timoney, Lazzarini, Pekonen & Välimäki, *Spectrally Rich Phase Distortion Sound
Synthesis Using an Allpass Filter*, ICASSP 2009, 293–296.
**Crest / phase design:** Schroeder, *Synthesis of low-peak-factor signals*, IEEE Trans. IT-16(1),
1970, 85–89.
**Phase vocoder:** Flanagan & Golden, BSTJ 45(9), 1966 · Portnoff, IEEE ASSP 24(3), 1976 · Dolson,
CMJ 10(4), 1986 · Puckette, *Phase-locked Vocoder*, WASPAA 1995 · Laroche & Dolson, WASPAA 1997 and
*Improved Phase Vocoder Time-Scale Modification*, IEEE SAP 7(3), 1999, 323–332 · Röbel, DAFx-03 ·
Harris, Proc. IEEE 66(1), 1978.
**Envelope / formants:** Röbel & Rodet, DAFx-05 · Moulines & Laroche, Speech Comm. 16(2), 1995.
**Modelling / inharmonicity:** Serra & Smith, *Spectral Modeling Synthesis*, CMJ 14(4), 1990 ·
Fletcher & Rossing, *The Physics of Musical Instruments* 2nd ed., ch. 12 · Sethares, *Tuning,
Timbre, Spectrum, Scale*, 1998.
**Vocabulary:** Wishart, *Audible Design*, 1994 (the source of CDP's blur vocabulary) · Roads,
*Microsound*, 2001.

## 7.5 Terrain's own precedent files (house law — these outrank any vendor)
`feedback-perceptual-test-harness-hardrule` (fb283 — phase-only change measured 102 % divergence and
was **inaudible**; sample-difference RMS is **banned** as a dramaticism metric) ·
`feedback-geometry-is-not-hearing-fb417` · `feedback-mutation-testing-mandatory-fb421` ·
`feedback-a-gate-that-cannot-fail-is-decoration-fb453` · `feedback-sweep-the-full-matrix-fb425` ·
`feedback-cert-must-seed-before-measuring-fb441` · `feedback-verify-the-path-not-just-the-engine`
(fb373) · `feedback-params-evolve-0-100-no-freerun-hardrule` ·
`feedback-no-playing-safe-max-ranges-hardrule` · `feedback-no-new-code-recycle-existing` ·
`feedback-terrain-no-duplicate-labels` · `terrain-instrument-lfo-arc-epic` (fb75 — Frame/Blur/
Spectral were once excluded from LFO modulation on CPU grounds; fb464's 32-tap bound is what
retired that for blur).

## 7.6 Measurements referenced (all reproducible; harnesses named in the research files)
Terrain bake / morph / blend timings and the `amount 0⁺` null table (`00-INVENTORY §6`) · Vital
iFFT 3.503 µs and the bin loop 0.148 µs, plus the Smear-silence and Random-Amplitudes survival
replays (`10-VITAL §6.5`) · the Serum AU harness `sp2.mm` / `serumprobe.mm` / `edge2.py`
(`11-SERUM2 §9`) · `auval` parameter dumps, the 402-file factory-bank verification, and the
21-table linear-interpolation residual study (`12-PHASEPLANT §2.2, §4.4-4.5`) · `disp.py`,
`wtdisp.py`, `harm.py`, `harm2.py`, `specbench.cpp`, `specbench2.cpp` (`13-PAPERS §E`).

## 7.7 What nobody established — do not let these be filled in from memory
1. **Serum 2's spectral oscillator FFT size, hop, overlap and window.** Not in the manual, not in the
   binary strings. *The single most valuable missing number in the corpus.*
2. **The DSP of all 34 Serum spectral warp modes.** Names and ordering are certain; every behaviour
   is `[INFERRED]`. They could not be driven because switching an oscillator to Spectral is a host
   *message* (`ConvertOscType`), not an automatable parameter.
3. **Serum's FM deviation above Warp 70 %.** The ~58 kHz at 100 % is a **fitted extrapolation**.
4. **Phase Plant's runtime frame interpolation algorithm.** Inferred from `frame: f32` + a mip
   ladder + the existence of three phase-alignment repair tools. Verifiable in ~1 hour by rendering
   the installed AU at `frame = 32.5` against a 50/50 mix. **Not done.**
5. **Every parameter range in the Phase Plant wavetable editor** (Frame Blend `Distance`, Disperse's
   amount, Reset Phases' angle, Phase Offset's blend scaling, Symmetrize, Fix Seam). Prose only;
   the label strings are not extractable from `HeartCore`.
6. **u-he's `morph1` / `morph2` algorithm.** The manual says outright it *"will be explained at a
   later date."*
7. **Laroche & Dolson's `β = 2/3 + α/3`** — full text paywalled; the constant is **unverified**,
   the structure is safe.
8. **The Välimäki/Abel/Smith 2009 recommended `a` and `M` for musical use** — paywalled; the allpass
   formulas in `13-PAPERS §A1` are textbook and were verified numerically to 1e-15, but the paper's
   own recommendations are not quoted.
9. **Terrain's phase convention vs Vital's** — the `φ → φ − π/2` correction is a *derivation*, not a
   measurement. **Null-test before shipping any lifted formula.** (§2.5 shows one place where the
   convention flips a research conclusion by 11 dB.)
10. **True-envelope (Röbel & Rodet) cost on a 2048-point single cycle** — never implemented, never
    benchmarked.
11. **The `^2.5` exponent for Disperse** — eyeballed off a measured crest curve, not fitted.
12. **The weighting used for the "centroid 6.07 → 3.81" blur measurement** — not recorded in any
    research file. The *ratio* is the usable figure.
13. **Falcon's Phase Distortion mode list** · **Pigments' Harmonic Shape section: amplitude or
    frequency?** (the manual contradicts itself between p.121 and p.122) · **Massive X's numeric
    knob ranges** · **Ableton's Spectral Resonator / Spectral Time FFT sizes** · **Bitwig Spectral
    Suite internals** — all `[GAP]`.
