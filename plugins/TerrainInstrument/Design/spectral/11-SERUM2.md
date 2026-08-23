# 11 — SERUM 2 (Xfer Records): wavetable & spectral processing

Research target: what Serum 2 actually does in its warp / spectral / morph machinery, with
numbers, so Terrain can be compared against it deliberately rather than by ear.

Companion to `00-INVENTORY.md` (Terrain's own spectral inventory).

---

## 0. Provenance — what is documented, what is measured, what is guessed

Everything below carries one of these tags. **Read the tag before you trust the line.**

| Tag | Meaning |
|---|---|
| `[M2 p.N]` | **Serum 2 User Guide**, Version 2.0 / Manual Version 1.0.0, 2025-03-17, 355 pp. Local copy: `/Library/Audio/Presets/Xfer Records/Serum 2 Presets/Serum 2 User Guide.pdf`. Page N is the printed page number in the footer. |
| `[M1 p.N]` | **Serum (1) manual**, Version 1.2.3, March 2019. Local copy: `/Library/Audio/Presets/Xfer Records/Serum Presets/Serum_Manual.pdf`. |
| `[BIN off]` | String literal at decimal file offset `off` in the shipped binary `/Library/Audio/Plug-Ins/VST3/Serum2.vst3/Contents/MacOS/Serum2` (**v2.1.4**, arm64, 32,647,536 bytes). These are C++ `enum`→string dumps the build emits; they are the *authoritative* list of what exists. |
| `[MEAS]` | **Measured by me**, this session, by hosting the real AU (`aumu/Xf2X/XFER`, v2.1.4) in a purpose-built AudioUnit host and analysing rendered audio. Method in §4.0. |
| `[INFERRED]` | **My reasoning, not a source.** Nobody documented this. Treat as a hypothesis. |

### 0.1 The single most important provenance fact

**The shipped manual is out of date.** The PDF on disk documents **v2.0** (March 2025). The
installed plugin is **v2.1.4** (`CFBundleShortVersionString`, built 2026-04-15). Every
spectral-specific warp mode listed in §3 **exists in the binary but is absent from the manual** —
the manual's Spectral chapter simply says "See *Exploring the Warp Modes* on page 50" `[M2 p.122]`,
pointing at the *wavetable* warp table, which does not contain them.

I could not find authoritative prose for the spectral warps anywhere:

- `xferrecords.com/web-manual/serum-2/...` sub-pages return **HTTP 404** for every path I tried.
- There are **no tooltip strings** for the spectral warp modes in the binary (tooltips exist for
  other controls, e.g. the Noise menu at `[BIN 14821263]`, so their absence here is meaningful).
- Press/tutorial coverage describes them only in aggregate: the spectral engine has warps that
  "spread partials, boost harmonics, gate frequencies below a threshold, twist phases and even
  apply masks or vocoding from other oscillators"
  ([SYNTH ANATOMY](https://synthanatomy.com/2026/08/xfer-records-serum-2-super-popular-wavetable-synth-gets-massive-free-update.html)).

**Consequence: §3's mode *names and ordering* are hard fact (from the binary). §3's mode
*behaviour* is `[INFERRED]` unless tagged otherwise. I did not verify the spectral warps by ear or
by measurement** — switching an oscillator to Spectral mode is a non-automatable host message
(`ConvertOscType`, `[BIN 14826194]`), not a parameter, so my parameter-driven harness could not
reach them. That is a real gap; see §8.

---

## 1. The five oscillator engines — what Serum 2 added

```
kOsc_WT = 0, kOsc_MultiSample, kOsc_Sample, kOsc_Granular, kOsc_Spectral,
kOsc_Noise, kOsc_Sub, kNumOscTypes, kNumMainOscTypes = kOsc_Spectral + 1
```
`[BIN 14793967]`

So **OSC A / B / C can each be any of 5 main types** (`kNumMainOscTypes = 5`), plus the fixed
Noise and Sub oscillators. Serum 1 had wavetable only.

RTTI in the binary confirms a shared framework rather than five separate oscillators:

```
14WarpOscillator
17WarpOscControllerI10WTOscStateE
17WarpOscControllerI14SampleOscStateE
17WarpOscControllerI16GranularOscStateE
17WarpOscControllerI16SpectralOscStateE
17WarpOscControllerI19MultiSampleOscStateE
```

**One `WarpOscillator` template, five state types.** Each instantiation carries its own warp enum
(§2). This is the structural idea worth stealing: warp is a *layer over any source*, not a
wavetable feature.

Also present: `14PFFFTConvolver` — Serum 2 uses **PFFFT** for its convolution engine.

---

## 2. The warp system

### 2.1 Shape

Every main oscillator type exposes the same automatable parameter block `[BIN 14803964]`:

```
kParamWarp, kParamWarpVar, kParamWarpMenu,
kParamWarp2, kParamWarpVar2, kParamWarpMenu2
```

i.e. **two independent chained warp slots**, each with a *mode*, a *depth* knob, and a *Var*
secondary control. Serum 1 had **one** slot and no Var (`"Warp 2"` appears 15× in the 2.1.4
binary, 0× in the Serum 1.368 binary `[MEAS: string diff]`).

The spectral oscillator adds `kParamSpecFltShift, kParamSpecFltWetDry, kParamFreqLo, kParamFreqHi`
plus non-automatable `kParamPhaseLock, kParamTransients, kParamLoHiIsPost, kParamLoHiIsSmooth,
kParamYAxisAssignment` `[BIN 14803964, 14804: kParamPhaseLock block]`.

**Confirmed live:** the AU exposes **2,622 parameters**, all normalised 0..1 `[MEAS]`. OSC A's
block starts at parameter ID `1000000`; `+33 = Warp`, `+34 = Warp Var`, `+35 = Warp Mode`,
`+36/37/38 = Warp 2 / Var 2 / Mode 2`. OSC B is `1001000`, Noise `1003000`, Sub `1004000`.

The Warp knob is **unitless 0–100 %**. `kAudioUnitProperty_ParameterStringFromValue` returns no
formatted unit for it `[MEAS]`, and neither manual states a unit. **There is no published "FM depth
in Hz / index / octaves" figure anywhere.** §4 is my attempt to supply one by measurement.

### 2.2 Wavetable warp — 70 modes, verified against the live plugin

```
kNoWarp = 0, kSync, kBendPos, kBendNeg, kBendPosNeg, kPWM, kASYMPos, kASYMNeg, kASYMPosNeg,
kFlip, kDLM, kRemap_1, kRemap_2, kRemap_3, kRemap_4, kQuantize, kEvenOdd,
kFilterLPF, kFilterHPF,
kDistTube, kDistSoftClip, kDistHardClip, kDistDiode1, kDistDiode2, kDistLinFold, kDistSinFold,
kDistZeroSquare, kDistAsym, kDistRectify, kDistSineShaper, kDistStompBox, kDistTapeSat, kDistSoftSat,
kFM_OSC, kFM_OSC2, kFM_NOISE, kFM_SUB, kFM_FILT1, kFM_FILT2,
kFMX_OSC, ... kFMX_FILT2,      // 6
kFMP_OSC, ... kFMP_FILT2,      // 6
kPD_OSC, ... kPD_FILT2, kSelfPD,
kAM_OSC, ... kAM_FILT2,
kRM_OSC, ... kRM_FILT2,
kNumWarpModes,                 // = 70
kFirstDrawable = kSync, kLastDrawable = kDistSoftSat
```
`[BIN 14858354]`

**I enumerated the live menu by sweeping the Warp Mode parameter and reading Serum's own display
strings** — all 70 entries, in this exact order, normalised step `1/69 = 0.014493` `[MEAS]`:

```
idx  norm      label            idx  norm      label            idx  norm      label
  0  0.00000   Off               24  0.34783   Linear Fold       48  0.69565   FML (Sub)
  1  0.01449   Sync              25  0.36232   Sine Fold         49  0.71014   FML (Filter 1)
  2  0.02899   Bend +            26  0.37681   Zero-Square       50  0.72464   FML (Filter 2)
  3  0.04348   Bend -            27  0.39130   Asym              51  0.73913   PD (B)
  4  0.05797   Bend +/-          28  0.40580   Rectify           52  0.75362   PD (C)
  5  0.07246   PWM               29  0.42029   Sine Shaper       53  0.76812   PD (Noise)
  6  0.08696   Asym +            30  0.43478   Stomp Box         54  0.78261   PD (Sub)
  7  0.10145   Asym -            31  0.44928   Tape Sat.         55  0.79710   PD (Filter 1)
  8  0.11594   Asym +/-          32  0.46377   Soft Sat.         56  0.81159   PD (Filter 2)
  9  0.13043   Flip              33  0.47826   FM (B)            57  0.82609   PD (Self)
 10  0.14493   Mirror            34  0.49275   FM (C)            58  0.84058   AM (B)
 11  0.15942   Remap 1           35  0.50725   FM (Noise)        59  0.85507   AM (C)
 12  0.17391   Remap 2           36  0.52174   FM (Sub)          60  0.86957   AM (Noise)
 13  0.18841   Remap 3           37  0.53623   FM (Filter 1)     61  0.88406   AM (Sub)
 14  0.20290   Remap 4           38  0.55072   FM (Filter 2)     62  0.89855   AM (Filter 1)
 15  0.21739   Quantize          39  0.56522   FME (B)           63  0.91304   AM (Filter 2)
 16  0.23188   Odd/Even          40  0.57971   FME (C)           64  0.92754   RM (B)
 17  0.24638   LPF               41  0.59420   FME (Noise)       65  0.94203   RM (C)
 18  0.26087   HPF               42  0.60870   FME (Sub)         66  0.95652   RM (Noise)
 19  0.27536   Tube              43  0.62319   FME (Filter 1)    67  0.97101   RM (Sub)
 20  0.28986   Soft Clip         44  0.63768   FME (Filter 2)    68  0.98551   RM (Filter 1)
 21  0.30435   Hard Clip         45  0.65217   FML (B)           69  1.00000   RM (Filter 2)
 22  0.31884   Diode 1           46  0.66667   FML (C)
 23  0.33333   Diode 2           47  0.68116   FML (Noise)
```

(`norm` = index / 69, the normalised AU parameter value that selects each mode.)

Naming decoder:
- `kDLM` → UI **"Mirror"**; `kEvenOdd` → UI **"Odd/Even"**.
- **`FM` / `FME` / `FML`** are the manual's three FM curves `[M2 pp.55-56]`:
  `FM` = **thru-zero** ("the carrier … inverts its phase and continues oscillating"),
  `FME` = **Exp**onential ("small changes in the modulator's amplitude cause dramatic changes …
  brighter or harsher"), `FML` = **L**inear ("linear FM is thru-zero but with a clamp at zero").
  Enum names are `kFM_*`, `kFMX_*` (eXponential), `kFMP_*`.
- `kFirstDrawable`/`kLastDrawable` encodes which modes the 2D waveform view can render — Sync
  through Soft Sat. Matches `[M2 p.50]`: "you can see how the warp mode affects the waveform for
  Sync, Alt Warp, and Distortion modes." FM/PD/AM/RM are not drawable (they need the other source).

**Sync in Serum 2 is one mode with a Var fader**, not two modes: "a WARP Var fader control appears
directly below the menu. Use this to adjust the smoothness of the sync from traditional 'hard
sync' to a very soft 'soft sync'" `[M2 p.51]`. Serum 1.368 had **three** separate entries —
`Sync`, `Sync 1/2 Win.`, `Sync Window` `[BIN(Serum1) 52432-52434]`.

### 2.3 What Serum 2 ADDED to wavetable warp

Baseline is the **installed Serum 1.368 binary**, not the 1.2.3 manual (the manual is older than
the last Serum 1 release, so using it would overstate the delta).

| Added in Serum 2 | Count | Evidence |
|---|---|---|
| **Odd/Even** | 1 | absent from Serum 1.368 strings, present in 2.1.4 `[MEAS: string diff]` |
| **LPF / HPF** as warps | 2 | `[BIN 14858354]` |
| **14 distortion warps** (Tube … Soft Sat.) | 14 | Serum 1 had these only as an *FX module*, not as a per-oscillator warp — **proved by naming style** `[MEAS: string diff]`: Serum 1.368 contains `SoftClip`/`HardClip`/`Lin.Fold` (FX spelling) and **zero** occurrences of `Soft Clip`/`Hard Clip`/`Linear Fold`; Serum 2.1.4 contains **both** spellings — the spaced ones are the new warp-menu entries, the unspaced ones are the still-present FX module. |
| **PD (phase distortion)** — 6 sources + PD (Self) | 7 | entirely new; `"PD"` warp entries absent from Serum 1.368 |
| **FM widened**: 6 sources × 3 curves | 18 (was 4) | Serum 1.368 had `FM (from B/A/Noise/Sub)` |
| **AM widened**: 6 sources | 6 (was 2) | Serum 1.368 had `AM (from B/A)` |
| **RM widened**: 6 sources | 6 (was 2) | Serum 1.368 had `RM (from B/A)` |
| **Second warp slot (Warp 2 + Var 2)** | — | `[MEAS: string diff]` |
| **Sync consolidated** 3 modes → 1 mode + Var | −2 | `[M2 p.51]` |

Sources for FM/PD/AM/RM are now: **other OSC ×2, Noise, Sub, Filter 1, Filter 2** — *filters as
modulation sources* is new. Serum 1 also forbade bidirectional FM ("you can only use A->B or B->A
and not both" `[M1 p.16]`); no such restriction is stated in `[M2 pp.55-57]`.

### 2.4 Sample / Granular / Multisample warp — 54 modes

```
kNoWarp = 0, kFilterLPF, kFilterHPF, <14 distortion>, <FM ×6>, <FMX ×6>, <FMP ×6>,
<PD ×6>, kSelfPD, <AM ×6>, <RM ×6>, kNumWarpModes   // = 54
```
`[BIN 14819074 (granular), 14832044 (multisample)]` — byte-identical lists.

**The wave-shape warps (Sync/Bend/PWM/Asym/Flip/Mirror/Remap/Quantize/Odd/Even) are absent.**
That is the obvious call: they are single-cycle geometry operations and there is no single cycle
in a sample or a grain. Filter + distortion + modulation survive because they are pointwise or
time-domain.

---

## 3. The SPECTRAL oscillator's warp modes — 88 modes

This is the headline addition and it is **the list the manual does not have.**

```
kNoWarp = 0, kDetune, kSmear, kSpread, kAddharmonics, kAddsubharmonics, kGate, kRobotize,
kSpectralShift, kMirror, kPeakFollow, kPeakOctaveUp, kPeakOctaveDown, kPeakHarmUp, kPeakHarmDown,
kPeakHarmSweep, kShepardNarrow, kShepardFilter, kSpectralComb, kSpectralPitchShift,
kSpectralPitchShiftNew, kSpectralPhaseTwist, kSpectralFormantShift,
kMask_OSC, kMask_OSC2, kMask_NOISE, kMask_SUB, kMask_FILT1, kMask_FILT2,
kVocode_OSC, kVocode_OSC2, kVocode_NOISE, kVocode_SUB, kVocode_FILT1, kVocode_FILT2,
kFilterLPF, kFilterHPF, <14 distortion>, <FM ×6>, <FMX ×6>, <FMP ×6>, <PD ×6>, kSelfPD,
<AM ×6>, <RM ×6>, kNumWarpModes   // = 88
```
`[BIN 14804335]`

**Indices 1–34 (34 modes) are spectral-only and exist nowhere else in the plugin.** Indices 35–87
are the same filter/distortion/modulation tail as every other engine.

UI labels recovered from the display-name table `[BIN 13824587-13825224]` and the menu string block
`[BIN 3420728-3422043]`:

| # | Enum | UI label | Behaviour |
|---|---|---|---|
| 1 | `kDetune` | `Detune +/-` | Bipolar. `[INFERRED]` per-partial frequency offset — detunes bin frequencies away from the harmonic grid, inharmonic at extremes. |
| 2 | `kSmear` | `Smear` | `[INFERRED]` magnitude/phase blur across bins and/or time — the closest analogue to Terrain's `Smear`. |
| 3 | `kSpread` | `Spread +/-` | Bipolar. `[INFERRED]` partial-index stretch/compress about a pivot (harmonic → inharmonic). |
| 4 | `kAddharmonics` | `Harmonics` | `[INFERRED]` synthesises integer multiples of detected partials. |
| 5 | `kAddsubharmonics` | `Sub harm…` (label reconstructed from pooled string fragments `Subh` + `harm`) | `[INFERRED]` adds integer *divisors*. |
| 6 | `kGate` | `Gate` | `[INFERRED]` per-bin magnitude gate — zeroes bins below a threshold the knob sets. Matches the press description "gate frequencies below a threshold". |
| 7 | `kRobotize` | `Robotize` | `[INFERRED]` classic phase-vocoder robotisation: **zero all bin phases every frame**, forcing pitch to the frame rate. |
| 8 | `kSpectralShift` | `Shift` | `[INFERRED]` **linear** frequency shift (all bins + k Hz) — inharmonic, unlike Pitch Shift. |
| 9 | `kMirror` | `Mirror` | `[INFERRED]` reflect the spectrum about a pivot bin. |
| 10 | `kPeakFollow` | `Peak Follow` | `[INFERRED]` peak-tracking; keeps energy only at detected spectral peaks. |
| 11-12 | `kPeakOctaveUp/Down` | `Peak Oct +12` / `Peak Oct -12` | `[INFERRED]` copy tracked peaks ±1 octave. |
| 13-14 | `kPeakHarmUp/Down` | `Peak Hmx +12` / `Peak Hmx -12` | `[INFERRED]` as above but on the harmonic (Hmx) index rather than frequency. |
| 15 | `kPeakHarmSweep` | `Harm Sweep` | `[INFERRED]` sweeps which harmonic the tracked peaks map onto. |
| 16 | `kShepardNarrow` | `Shepard W` | `[INFERRED]` Shepard-tone construction; "W" = width/narrowness of the octave-spaced comb. |
| 17 | `kShepardFilter` | `Shepard ?` — **second Shepard entry; the distinguishing letter is not recoverable from the pooled strings** | `[INFERRED]` Shepard-shaped spectral envelope applied as a filter. |
| 18 | `kSpectralComb` | `Comb` | `[INFERRED]` periodic comb over bin index. |
| 19 | `kSpectralPitchShift` | `Pitch Blend` | `[INFERRED]` legacy resampling-style pitch shift (kept for preset compatibility — note the sibling below is named `…New`). |
| 20 | `kSpectralPitchShiftNew` | `Pitch Shift` | `[INFERRED]` current algorithm; formant-independent bin remap. |
| 21 | `kSpectralPhaseTwist` | `Phase Twist` | `[INFERRED]` progressive phase rotation vs bin index (dispersion / allpass smear). |
| 22 | `kSpectralFormantShift` | `Formant` | `[INFERRED]` shift the spectral envelope while holding partial frequencies. |
| 23-28 | `kMask_*` | `Mask (?)`, `Mask (Noise)`, `Mask (Sub)`, `Mask (Filter 1)`, `Mask (Filter 2)` | `[INFERRED]` per-bin **minimum/multiply** against the other source's spectrum — a spectral AND. |
| 29-34 | `kVocode_*` | `Voc (?)`, `Voc (Noise)`, `Voc (Sub)`, `Voc (Filter 1)`, `Voc (Filter 2)` | `[INFERRED]` classic vocoder: this oscillator's magnitude envelope replaced by the other source's. |

`(?)` is a runtime placeholder substituted with the other oscillator's letter (B / C), exactly as
`FM (B)` / `FM (C)` are built in the wavetable list.

**The spectral engine can therefore modulate from, mask against, and vocode with any of the other
five sound sources — while simultaneously running two chained warps.** That combinatorial reach
(88 × 88 across two slots) is the real design statement, more than any individual algorithm.

### 3.1 Spectral engine parameters that ARE documented

| Control | Documented behaviour | Source |
|---|---|---|
| **SCAN** | Speed + direction of sample playback. **Range switchable: ±200 % (default), ±400 %, ±800 %.** Options: Reverse, Key Track, Lock Scan Rate to Tempo, Sample Length to BPM. | `[M2 p.117]` |
| **Phase Lock** | "Adjust the FFT phases to minimize the audible phase change between FFT blocks… can result in a less **'smeared'** sound, more faithful to the original sample." | `[M2 p.118]` |
| **Transients** | "Preserves transients that would otherwise be smeared by FFT processing." | `[M2 p.118]` |
| **CUT** | Cutoff of the spectral filter. | `[M2 p.118]` |
| **FILTER** | A drawable **spectral mask**: freely editable breakpoint curve with per-segment curvature, multi-select, grid snapping, factory presets — *or* a wavetable used as the mask (not editable in that case). | `[M2 pp.118-122]` |
| **MIX** | Wet/dry. | `[M2 p.122]` |
| **Lo/Hi frequency markers** | Draggable, modulatable. Two options: **Smooth** = "Apply a **fourth-order Butterworth** filter at the low and high frequency boundaries"; **Post Warp** = "Apply the low/high filtering **after** processing spectral warps." | `[M2 p.108]` |
| **Y-axis assignment** | The X\|Y pad's Y axis can drive: Osc Volume, Warp, Warp 2, Spectral Filter Shift, Spectral Filter Wet/Dry, Freq Lo, Freq Hi. | `[BIN 14824377]` |

**The FFT size, hop size, overlap factor and analysis window of the spectral oscillator are
nowhere stated** — not in the manual, not as strings in the binary. I could not determine them.
See §8. (The *wavetable import* FFT sizes are documented and are a different code path — §5.4.)

---

## 4. Maxima: how far do FM / PD / RM / Sync actually go?

This is Max's question. **No published number exists.** So I measured it.

### 4.0 Method `[MEAS]`

Built a minimal AudioUnit host (`sp2.mm`, ~60 lines, `AudioComponentInstanceNew` →
`AudioUnitSetParameter` → `MusicDeviceMIDIEvent` note-on → `AudioUnitRender`) against the **real
shipped AU** `aumu/Xf2X/XFER` v2.1.4. Renders float, 2 ch, non-interleaved, 512-frame slices.
Default init patch. Analysis in numpy.

- Carrier = OSC A, default wavetable. Baseline measured as saw-like: h2 −5.58, h4 −12.66,
  h8 −17.28 dB rel. h1 (ideal saw: −6.02 / −12.04 / −18.06).
- Modulator silenced in the mix (`Level = 0`) but left enabled — the manual explicitly sanctions
  this: "you can turn down the volume of the other oscillator if you want to use the other
  oscillator simply as a modulation source" `[M2 p.55]`. Confirmed working: warp still bites.
- Steady-state window only (0.9–1.9 s after note-on) to exclude the amp attack.

**Deviation estimator.** For a *slow* sinusoidal modulator the carrier's fundamental sweeps over
`[fc − Δf, fc + Δf]` and piles up at the turning points, so the **lowest frequency in the whole
spectrum is exactly `fc − Δf`** — partial *n* of a saw only ever reaches `n(fc − Δf)`, which is
higher. Reading the low edge at −40 dB rel. peak therefore gives Δf directly, and is valid until
`Δf ≥ fc` (thru-zero), where it saturates.

**Validation of the estimator** — three independent cross-checks, all passed:
1. Two different carrier pitches (4,186 Hz and 16,744 Hz) gave **1651.60 vs 1651.93 Hz** at warp
   0.40 and **3875.38 vs 3875.08 Hz** at warp 0.50.
2. Three sample rates (44.1 / 48 / 96 kHz) gave **3697.0 Hz identically** at warp 0.50.
3. The fitted law predicted 14,046 Hz at warp 0.70; measured 14,062.9 Hz (0.1 % error).

### 4.1 The big finding: Serum's "FM" is TRUE frequency modulation with a FIXED Hz deviation

I varied carrier pitch, modulator pitch and sample rate independently:

| Varied | Result | Conclusion |
|---|---|---|
| Carrier 261.6 → 4,186 → 16,744 Hz | Δf **unchanged** | deviation is **not** proportional to carrier pitch |
| Modulator 16.378 → 2.579 → 0.406 Hz (warp 0.40) | Δf = 1651.5 → 1540.0 → **1536.0 Hz** (converging) | deviation is **not** proportional to modulator pitch → **not phase modulation** |
| SR 44.1 / 48 / 96 kHz | Δf = 3697.0 Hz identically | deviation is **not** in units of sample rate |

> **Serum 2's FM warp sets a peak frequency deviation in Hz, fixed by the knob alone.**
> It is true linear FM, not the DX7-style phase modulation that most "FM" synths ship.

(The residual +7 % at fm = 16.4 Hz is expected Carson broadening as the quasi-static approximation
weakens; the fm → 0 limit is the true peak deviation.)

### 4.2 The FM depth law `[MEAS]`

Carrier 16,744 Hz, modulator = Sub osc **sine** at 0.40617 Hz (quasi-static), 48 kHz, 16 s render:

| Warp | Peak deviation Δf | Δf / warp⁴ |
|---:|---:|---:|
| 0.20 | 110.9 Hz | 69,312 |
| 0.25 | 251.7 Hz | 64,435 |
| 0.30 | 501.3 Hz | 61,889 |
| 0.35 | 910.9 Hz | 60,701 |
| 0.40 | 1,533.8 Hz | 59,914 |
| 0.45 | 2,438.3 Hz | 59,462 |
| 0.50 | 3,697.0 Hz | 59,152 |
| 0.55 | 5,393.0 Hz | 58,936 |
| 0.60 | 7,618.1 Hz | 58,782 |
| 0.65 | 10,472.5 Hz | 58,667 |
| 0.70 | 14,062.9 Hz | 58,571 |
| ≥0.75 | > 16,744 Hz | *estimator saturated* |

Least-squares on warp ≥ 0.45 (max residual **0.11 %**):

```
Δf  ≈  57,818 · warp^3.966   Hz          →   effectively  Δf ≈ 58 kHz · warp⁴
```

- **Directly measured maximum: 14,063 Hz of peak deviation at Warp 70 %.**
- **Warp 100 % ≈ 58 kHz of peak deviation — this is an EXTRAPOLATION**, not a measurement. The
  fit is excellent over 0.45–0.70 and the exponent is still drifting very slightly downward
  (3.98 at 0.70), so the true value could be a few % lower. I could not measure past 0.70:
  raising the carrier above Nyquist to extend the range fails because **Serum clamps the phase
  increment** (a carrier nominally at 33,488 Hz did not behave as one — the overlap check with a
  known point disagreed by exactly the amount I had raised it).

**What ~58 kHz of deviation means in FM index.** Because Δf is fixed in Hz, the classical index
`β = Δf / f_mod` is **inversely proportional to pitch**. For the ordinary 1:1 patch (OSC B tracking
the note, the default when you enable it):

| Note | f₀ | β at Warp 100 % | Carson bandwidth 2(Δf+fm) |
|---|---:|---:|---:|
| C1 | 32.7 Hz | **≈ 1,790** | 117 kHz |
| C2 | 65.4 Hz | ≈ 894 | 117 kHz |
| C4 | 261.6 Hz | ≈ 224 | 117 kHz |
| C7 | 2,093 Hz | ≈ 28 | 121 kHz |

For scale: a DX7 operator's maximum modulation index is usually quoted at **β ≈ 12–13**
(*general FM-synthesis knowledge, not a source I verified this session*). Serum at 100 % on a C4 is roughly
**18× that**, and on a bass note **~140×**. Carson bandwidth at 100 % is ~117 kHz — **2.4× the
sample rate** — so the result is not a spectrum, it is band-limited noise.

> **This is the concrete answer to "why is Serum earsplitting at 100 %":** the knob's top is not a
> musical maximum, it is far past total spectral saturation, and it gets *worse the lower you
> play* because the deviation does not track the note.

**Caveat:** Δf also scales with the modulator's instantaneous amplitude, so the *waveform* of the
modulator matters. At warp 0.40 with the Sub oscillator `[MEAS]`:
Sine **1,533.8 Hz** · Triangle **2,013.4 Hz** · Saw **2,643.9 Hz** · Square **1,138.0 Hz**.
The law above is calibrated for a **sine** modulator. With OSC B's default saw the carrier is
already thru-zero by warp ≈ 0.40.

### 4.3 What each mode does at 0 % / 50 % / 100 % `[MEAS]`

OSC A default table, note C4, OSC B enabled as modulator at unity ratio with Level 0, 48 kHz.
Centroid and 95 %-energy rolloff computed on the steady state.

| Mode | Warp | Peak dBFS | RMS dBFS | Centroid | 95 % rolloff |
|---|---:|---:|---:|---:|---:|
| **Off (reference)** | — | −10.94 | −18.80 | **814 Hz** | **2,878 Hz** |
| Sync | 50 % | −11.19 | −18.70 | 1,892 Hz | 6,802 Hz |
| Sync | **100 %** | −12.13 | −19.32 | 6,539 Hz | 16,744 Hz |
| Hard Clip | 100 % | −10.74 | −14.66 | 477 Hz | 1,307 Hz |
| Sine Fold | 100 % | −9.40 | −17.55 | 4,142 Hz | 13,605 Hz |
| **FM (B)** | 50 % | −10.14 | −18.98 | 4,680 Hz | 14,935 Hz |
| **FM (B)** | **100 %** | **−8.59** | **−22.15** | **13,056 Hz** | **22,963 Hz** |
| FME (B) exp | 100 % | −9.70 | −22.22 | 11,908 Hz | 22,782 Hz |
| FML (B) lin | 100 % | −9.19 | −20.16 | 4,299 Hz | 20,674 Hz |
| PD (B) | 100 % | −11.02 | −19.88 | 7,218 Hz | 18,737 Hz |
| **PD (Self)** | **100 %** | **−7.69** | −21.50 | 10,841 Hz | 22,808 Hz |
| AM (B) | 100 % | −4.72 | −13.48 | 304 Hz | 785 Hz |
| RM (B) | 100 % | −6.84 | −14.99 | 143 Hz | 262 Hz |

**The crucial reading, and it is counter-intuitive:**

- FM at 100 % is **3.35 dB QUIETER in RMS** than warp 0 (−22.15 vs −18.80). It is **not louder.**
- Its **spectral centroid moves 814 Hz → 13,056 Hz: a 16× shift, exactly 4 octaves.**
- Its 95 % rolloff moves 2,878 → 22,963 Hz: **95 % of the energy is now spread across the whole
  audio band**, where the reference saw had 95 % of its energy below 2.9 kHz.

> **"Earsplitting" is not a level problem, it is a distribution problem.** Serum relocates
> essentially all the energy into the 5–20 kHz region — precisely where the ear is most sensitive
> and where a saw normally has almost nothing — while the meters go *down*. Any "tame it with
> makeup gain" instinct is wrong; the fix is where the energy sits, not how much of it there is.

Note also the split personality of the family: **FM/PD go bright** (centroid up 8–16×), while
**AM/RM go dark and loud** (RM centroid *down* 5.7×, RMS *up* 3.8 dB; AM RMS up 5.3 dB). If Terrain
wants "Serum-like reach", those two halves need different treatment.

### 4.4 Sync range `[MEAS]`

Measured by tracking the sync formant (smoothed spectral-envelope peak above 400 Hz) on a C4:

| Warp | 30 % | 50 % | 70 % | 80 % | 90 % | **100 %** |
|---|---:|---:|---:|---:|---:|---:|
| Slave : master ratio | 2.04 | 2.97 | 6.03 | 8.97 | 11.97 | **15.97** |

> **Sync spans 1:1 → 16:1 — exactly 4 octaves.** Above ~60 % it follows `ratio ≈ 16^warp` closely.

(The 30 % and 50 % readings are the least reliable: at shallow sync the tracked envelope peak is
still partly the carrier's own h2/h3 rather than a distinct sync formant.)

---

## 5. Smear / blur / frame interpolation — the documented parts

This is the area where Serum is *best* documented and where it maps most directly onto Terrain's
`renderBlend` and `SpectralMorph`.

### 5.1 The wavetable format

> "Serum … uses **2048 samples for a frame** (subtable)… the maximum file size is
> 2048 (samples) × **256 (frames)** × 32 (bits), which is exactly 2 megabytes." `[M2 p.39]`

Confirmed by the embedded header string `<!>2048 00000000 wavetable (www.xferrecords.com)`
`[BIN 13825457]` and by the editor's frame-reduction menu — `128 (Keep 1/2)`, `64 (Keep 1/4)`,
`32 (Keep 1/8)`, `16 (Keep 1/16)`, `8 (Keep 1/32)`, `4 (Keep 1/64)` `[BIN 13823607]`, which is a
÷2 ladder from 256.

### 5.2 Frame interpolation ("Morph") — four algorithms, computed at LOAD TIME

> "The remaining frames can be **interpolated** (in the Wavetable Editor)… These interpolated
> frames are generated through **crossfading (mix blend)** or **spectral morphing (frequency +
> phase blend)**. **These frames are computed at load time; Serum embeds the interpolation type
> rather than the interpolated waveforms** (reducing disk space)." `[M2 p.39]`

The MORPH menu `[M2 p.289]`, `[BIN 14822612-14822719]`:

| Menu item | Documented behaviour |
|---|---|
| **Morph - Crossfade** | "Create interpolated frames by crossfading the neighbouring frames together. **This is the recommended default, and what traditional wavetable synths do.**" |
| **Morph - Spectral** | "Use the spectral and phase content of neighbouring frames to re-synthesize the interpolated frames. **This is what additive synthesizers do.**" |
| **Morph - Spectral (Zero Fund. Phase)** | "the phase content of the fundamental is zeroed for all source frames. This way the lowest frequency does not shift/rotate between frames." |
| **Morph - Spectral (Zero All Phases)** | "all phase content is discarded. This might alter the sound of the source content drastically… However this option also creates the **smoothest transitions** between frames since no frequencies need to shift phase." |
| **Remove Morph Tables** | Revert. Note the two zero-phase modes **destructively alter the source tables** — undo is preferred. |

Mechanics: morph fills **all 256 slots**; thumbnails renumber to "1, 17, 33…"; interpolated frames
render **grey** in the 3D overview (real = green, current = yellow) `[M2 pp.289, 40]`. Requires
">1 and <256 frames".

> **The design lesson for Terrain:** Serum's answer to "how do you get smooth wavetable motion" is
> *pre-compute a dense table offline, then do nothing at all at runtime.* Runtime frame lookup is
> a plain 2-frame read. There is no per-block morph. Terrain's `rebuildMorphIfNeeded` at ~20 Hz
> per osc is already *more* dynamic than Serum's shipping behaviour — which is worth knowing
> before spending more CPU there.

### 5.3 Blur — offline, four axes

Four separate operations in the Wavetable Editor menu `[M2 p.288]`, `[BIN 14822446-14822570]`:

| Operation | Documented behaviour |
|---|---|
| **Blur Spectra - Adjacent Bins (Grid Size)** | "Interpolate (smooth) the harmonic content **between adjacent harmonics**. The **grid size value determines how many neighbouring harmonics** are factored into the smooth operation." |
| **Blur Phases - Adjacent Bins (Grid Size)** | as above, on **phase**. |
| **Blur Spectra - Adjacent Frames (Grid Size)** | "Interpolate (smooth) the frequency content **between adjacent frames**." |
| **Blur Phases - Adjacent Frames (Grid Size)** | as above, on **phase**. |

> **Serum separates four things Terrain currently fuses.** Terrain's `renderBlend` blurs
> *frames* only, and blurs magnitude and phase together (it is a convex phasor average, so phase
> cancellation is baked in — see `00-INVENTORY.md` §3). Terrain's `Smear` mode blurs *bins*
> (triangular over partial index, `W = round(11a)`) **and** scatters phase **and** rolls off highs,
> all welded to one knob. Serum's 2×2 (spectra|phases × bins|frames) is the cleaner factoring.
>
> The blur width is **"grid size"** — the editor's drawing grid divisor, a small integer
> (the manual suggests values like 4, 6, 12 `[M2 p.276, p.347]`). Not a continuous 0–1 amount.
> **No window function, no Gaussian σ is specified** — the manual says only "how many neighbouring
> harmonics are factored in". Whether the kernel is boxcar or triangular is **not documented**.

**Cost class: offline, user-invoked, destructive.** These are editor menu commands, not
parameters. Nothing here runs per-block or per-sample.

### 5.4 The other documented "smear": FFT resynthesis import

Wavetable import offers `FFT 256 / 512 / 1024 / 2048` `[M2 pp.293-294]`, `[BIN 13823260-13823354]`:

> "the FFT modes are a spectral import… divide the source audio into small snippets of time, and
> analyze the spectral content. One way of thinking of this is a **'blurred averaging of the
> frequency content'**." `[M2 p.293]`
> "The numbers 256, 512, 1024, and 2048 represent the number of samples used to perform the FFT
> analysis." `[M2 p.294]`

Sibling import modes: Dynamic Pitch Zero-Snap, Dynamic Pitch Follow, Constant framesize (pitch
average), Frequency Estimation `[BIN 13823153-13823432]`. The formula field accepts a raw sample
count (1–4 digits) or a MIDI note name, **assuming a 44,100 Hz source** `[M2 p.294]`.

**Overlap factor and analysis window for these FFT imports are not stated.** `[INFERRED]` Given
the phrase "blurred averaging", overlap-add with a Hann-family window is likely, but this is a
guess.

### 5.5 Editor spectral operations worth knowing about

From `[BIN 14821647-14824268]` and `[M2 pp.279-288]` — the offline toolkit Serum ships:

`Remove DC Offset` · `Flip Vertical / Horizontal` · `Shift Horizontal to Zero-Crossing` ·
`Fade Edges (Grid Size)` · `X-Fade Edges (16 Samples)` · `Filter (Grid Size)` ·
`Sample Redux at Grid Size` · `Remove Fundamental (HPF)` · `Remove Low Spectra/Phases (Grid Size)` ·
`Normalize Each` / `Normalize Same (Max. From All Frames)` · `Create PWM from This Table to All`
(spreads a PWM sweep across all 256 frames) · `Nudge All Phases for Fundamental to 50%` ·
`Set / Subtract Spectra from Osc A|B` · `Set Phases from Osc A|B` ·
`Sort by Spectrum (Peak Spect | Average Spect | Peak Amount | Num w/ Spect | Highest w/ Spect |
Fundamental Amt.)` · `Shift Octave Up/Down` (bin 1→2, 2→4 …) · `Repeat Bin Group` ·
`Progressive Fade` · `Randomize Low 16/32/64 Bins (with Half)` · `Create Random Series Gaps` ·
`Scale Freq Values by Bin Index`.

> Note the overlap with Terrain's shipped `HarmonicEngine.h` (SPLAY / CULL / TERRACE / CLANG) and
> with the proposed morph ideas in `00-INVENTORY.md`. **`Shift Octave Up/Down` is exactly
> `HarmonicStretch` at integer ratios; `Create Random Series Gaps` is `RandomAmplitudes` with a
> zero floor; `Progressive Fade` is `Smear`'s rolloff term alone.** Check the no-doubles rule
> before adding any of these under a new name.

---

## 6. CPU cost classes

Terrain's constraint (message-thread table bake, never audio thread) maps onto Serum like this:

| Serum mechanism | Cost class | Evidence |
|---|---|---|
| **Wave-shape warps** (Sync, Bend, PWM, Asym, Flip, Mirror, Remap, Quantize, Odd/Even) | **per-sample, per-voice, per-unison-voice** | They are drawable live in 2D `[M2 p.50]`, they have per-unison-voice spread (`Uni Warp`, `Uni Warp 2` params `[MEAS]`), and Appendix E's advice is "prioritize FX bus use over per-voice effects" `[M2 p.347+]`. |
| **Filter / distortion warps** | per-sample, per-voice | same slot, same knob. |
| **FM / FME / FML / PD / AM / RM warps** | per-sample, per-voice, **and they force the modulating source to render** even at Level 0 `[MEAS]` | measured: modulator at Level 0 still drives the warp. |
| **Spectral oscillator warps** (the 34) | **per FFT hop** `[INFERRED]` — not per sample | They are spectral-domain by construction; the engine already runs an STFT (Phase Lock / Transients options `[M2 p.118]` only make sense per-block). **Hop size and FFT size are undocumented**, so the actual rate is unknown. |
| **Frame interpolation / Morph** | **offline, at load time.** Zero runtime cost. | `[M2 p.39]` verbatim. |
| **Blur (all four)** | **offline, user-invoked**, destructive editor command | `[M2 p.288]` |
| **FFT resynth import** | **offline, at import** | `[M2 p.293]` |
| Convolution (FX) | PFFFT, partitioned `[INFERRED]` | `14PFFFTConvolver` RTTI. |

**The headline:** Serum spends its per-sample budget on *warp*, and spends nothing at runtime on
*morphing between frames* — that is all pre-baked. Terrain currently does the opposite: it rebuilds
tables at ~20 Hz per osc (~21 ms per bake, ~40 % message-thread duty per osc — `00-INVENTORY.md`
finding #2) and has a comparatively thin warp layer (11 options × 2 slots vs Serum's 70 × 2).

---

## 7. What this suggests for Terrain (opinion, clearly labelled)

`[INFERRED — this is my judgement, not sourced]`

1. **The "tamer than Serum" gap is mostly a range decision, not a quality gap.** Serum's FM top is
   ~58 kHz of deviation on a note that might be 32 Hz. It is not calibrated to be musical at 100 %;
   it is calibrated so that 100 % is unusable and the useful zone is 20–60 %. If Terrain wants that
   feel, the honest way to get it is to widen the *range* and keep the taper steep (Serum's is
   `warp⁴`), not to add distortion.
2. **Serum's deviation does not track the note.** That is the single biggest behavioural
   difference and it is what makes low notes explode. Whether Terrain wants to copy that is a real
   design choice — it violates the "params evolve 0→100" spirit in a specific way (the same knob
   value is mild at C7 and catastrophic at C1).
3. **Watch RMS vs centroid.** Serum's FM at 100 % is *quieter* and *brighter*. Any Terrain gate
   that certifies "the knob does something" using level will pass while the actual mechanism —
   energy relocation — is untested. Gate on **spectral centroid and 95 % rolloff**, per
   `feedback-geometry-is-not-hearing-fb417`.
4. **Serum's blur factoring (spectra|phases × bins|frames) is better than Terrain's.** Terrain's
   `Smear` welds three effects to one knob and `renderBlend` cannot brighten by construction. Worth
   revisiting when the morph work resumes.
5. **`kMaxPartials = 96` (00-INVENTORY finding #1) has no analogue in Serum** — Serum's editor
   works on the full bin set and its own reduction ladder is explicit and user-chosen. The silent
   truncation step at `amount 0 → 1e-6` is a Terrain-specific defect, not a shared idiom.

---

## 8. Honest gaps — what I could NOT establish

1. **The spectral oscillator's FFT size, hop size, overlap factor and analysis window.** Not in
   the manual, not in the binary strings, not in any source I found. This is the single most
   valuable missing number for Terrain.
2. **The DSP of all 34 spectral warp modes.** Names and ordering are certain `[BIN 14804335]`;
   behaviour is `[INFERRED]` throughout §3. I could not drive them because switching an oscillator
   to Spectral mode is a host *message* (`ConvertOscType`), not an automatable parameter, so my
   harness could not reach them. **A follow-up with a UI-driving or preset-loading approach could
   measure these the same way I measured FM.**
3. **The exact label of `kShepardFilter`** (index 17). String pooling left only fragments.
4. **Δf above Warp 70 %.** The ~58 kHz figure at 100 % is a fitted extrapolation (§4.2), not a
   measurement. Serum clamps super-Nyquist carriers, which closed the obvious workaround.
5. **Blur kernel shape** (boxcar vs triangular vs Gaussian) — the manual says only "how many
   neighbouring harmonics are factored in" `[M2 p.288]`.
6. **Whether Serum 2 still forbids bidirectional FM.** Serum 1 did `[M1 p.16]`; Serum 2's manual is
   silent, and I did not test A→B and B→A simultaneously.

---

## 9. Reproducing the measurements

Harness and analysis scripts are in this session's scratchpad:
`/private/tmp/claude-501/-Users-macshooter-Developer-VST-Plugins/982b122e-d4fe-4e00-ad51-0ad84297df2c/scratchpad/`

| File | Purpose |
|---|---|
| `dumpparams.mm` | Enumerate all 2,622 AU parameters with min/max/default/unit/flags |
| `sp2.mm` | Parametric render harness — `sp2 <out.raw> <paramID>=<val> …`, env `NOTE` / `SECS` / `SR` |
| `serumprobe.mm` | Warp-mode menu enumeration via `kAudioUnitProperty_ParameterStringFromValue` |
| `an.py` / `cmp.py` | Peak, RMS, spectral centroid, 95 % rolloff |
| `edge2.py`, `edge3.py` | Low-edge deviation estimator (§4.0) |

Build: `clang++ -O1 -fobjc-arc -o sp2 sp2.mm -framework AudioToolbox -framework Foundation -framework CoreAudio`

Key parameter IDs (OSC A base `1000000`, OSC B `1001000`, Sub `1004000`):
`+0 Enable · +1 Level · +3 Octave (±4) · +6 Coarse Pitch (±64 semitones) · +7 Ratio (0.250…64.000)
· +9 Pitch Track · +33 Warp · +34 Warp Var · +35 Warp Mode · +36 Warp 2 · +38 Warp 2 Mode`;
Sub `+33 Shape` (Sine · RoundRect · Triangle · Saw · Square · Pulse).

---

## Sources

- Xfer Records, *Serum 2 User Guide*, Version 2.0, Manual Version 1.0.0, 17 March 2025, 355 pp.
  (ships in `Serum 2 Presets/`; also at [xferrecords.com/web-manual/serum-2/welcome](https://xferrecords.com/web-manual/serum-2/welcome))
- Xfer Records, *Serum Synthesizer Manual*, Version 1.2.3, March 2019.
- Xfer Records, Serum 2 VST3 binary v2.1.4 (string/RTTI analysis) and Serum VST3 binary v1.368.
- Xfer Records, [Serum 2 product page](https://xferrecords.com/products/serum-2).
- SYNTH ANATOMY, [Xfer Records Serum 2 gets massive free update](https://synthanatomy.com/2026/08/xfer-records-serum-2-super-popular-wavetable-synth-gets-massive-free-update.html) — spectral warp overview.
- CDM, [Serum 2: wavetable champ becomes much more](https://cdm.link/serum-2/).
- Direct measurement of the shipped Audio Unit, this session (§4, §9).
