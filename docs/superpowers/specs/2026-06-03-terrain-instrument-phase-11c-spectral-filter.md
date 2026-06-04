# Phase 11c V1 — SPECTRAL Filter Modes (Low Pass / High Pass / Smear)

> **Status:** SPEC drafted 2026-06-03 after spectral morph DSP research + architectural cost analysis.
> **Parent:** Phase 11d shipped (`mark-2-synth-phase-11d-fold-dsp`). All previous phases intact.
> **Research:** `docs/research/2026-06-03-spectral-morph-research.md`

## Scope decision — V1 vs full Vital-class spectral morph

The research recommended 6 modes using per-noteOn FrameSpec → IFFT → cached buffer architecture (~11ms per noteOn × dirty-flag recompute when modulated). The full architecture has TWO blockers:

1. **CPU cost when modulated:** 25% CPU per voice during live modulation. 8 voices = 200% CPU. Not viable for live SPECTRAL_AMT automation.
2. **Wavetable coverage:** only 7 of 24 wavetables have FrameSpec. The other 17 (DX7EP, D50Bell, M1Piano, etc.) would be silent to SPECTRAL until Phase 10c migrates them — and 6 of those use inharmonic partials that require a `PartialSpec` extension to FrameSpec.

**Phase 11c V1 ships 3 spectral filter modes** (time-domain biquad filters with cutoff curves designed to sound "spectral") — fully live-modulatable, works on all 24 wavetables + all engines (NOISE/FM/etc.), CPU cost negligible (~4 biquad samples per voice per block). Provides immediate night-and-day audible spectral shaping.

**Phase 11c.2 (future)** will add 3-5 TRUE spectral morph modes (Vocode, Harmonic Stretch, Phase Disperse) once Phase 10c migrates the legacy wavetables and the per-voice IFFT caching architecture is built. Phase 11c.2 will be 4-mode-or-more additions to the SPECTRAL_TYPE choice; the V1 LP/HP/SMEAR stays as the cheap-and-always-available baseline.

## TL;DR

3 SPECTRAL modes (Low Pass / High Pass / Smear) implemented as post-fold per-OSC per-channel `juce::dsp::IIR::Filter<float>` biquads. Cutoff and Q curves chosen so each mode produces a distinct sonic character at 100% AMT.

## The 3 SPECTRAL modes

| # | Mode | Filter | Cutoff at 0% / 100% | Q | 100% character |
|---|---|---|---|---|---|
| 0 | **Low Pass** | LP biquad | 20000 / 200 Hz (quad-eased) | 0.707 | only fundamental, "telephone" |
| 1 | **High Pass** | HP biquad | 20 / 8000 Hz (quad-eased) | 0.707 | only high harmonics, "hiss" |
| 2 | **Smear** | All-pass biquad | 4000 / 200 Hz (quad-eased) | 0.707 → 4.0 (linear) | phase-rotated, "diffuse" |

Cutoff curves (amount² for LP/HP/Smear cutoff scaling — gentle in lower half of knob, dramatic in upper half).

Math:
- LP cutoff = `200 + (1 − amount²) × 19800`
- HP cutoff = `20 + amount² × 7980`
- Smear cutoff = `200 + (1 − amount²) × 3800`
- Smear Q = `0.707 + amount × 3.293` (0.707 → 4.0 linear)

## Architecture

### Where in the signal chain

```
WT POS → WARP (Phase 11b) → engine compute (WT/FM/NS) → FOLD (Phase 11d) → SPECTRAL (Phase 11c) → per-OSC level + pan → voice mix → ladder filter → HORIZON → output
```

SPECTRAL runs POST-FOLD on the summed mono OSC output (per-channel — applied to OSC A's summed L + R after unison average, before final pan/level/mix into the stereo voice).

### Per-voice state

```cpp
juce::dsp::IIR::Filter<float> spectralFilterAL_;   // OSC A left channel
juce::dsp::IIR::Filter<float> spectralFilterAR_;   // OSC A right channel
juce::dsp::IIR::Filter<float> spectralFilterBL_;   // OSC B left channel
juce::dsp::IIR::Filter<float> spectralFilterBR_;   // OSC B right channel

int   spectralTypeA_   = 0;   // 0=LP, 1=HP, 2=Smear
float spectralAmtA_    = 0.0f;
int   spectralTypeB_   = 0;
float spectralAmtB_    = 0.0f;
bool  spectralBypassA_ = true;  // if amount very small, skip processing
bool  spectralBypassB_ = true;
```

Memory cost: 4 biquads × ~6 floats state = 24 floats per voice. Negligible.

### Coefficient updates

In `setSpectral(typeA, amtA, typeB, amtB)`:
1. Update `spectralTypeA_` / `spectralAmtA_` (B-equivalents)
2. Compute cutoff + Q from the spec table
3. Set both A channel filters' coefficients via `*spectralFilterAL_.coefficients = *juce::dsp::IIR::Coefficients<float>::makeLowPass(...)` (etc.)
4. Same for B
5. Set bypass flags: `spectralBypassA_ = (amtA < 0.001f)` — at amount = 0, the filter has zero impact regardless, but skipping is faster

### Per-sample application

In `renderNextBlock`, after the per-OSC unison sum + average, before mixing into the final scratch buffer:

```cpp
float sA_L = sumAL, sA_R = sumAR;
if (! spectralBypassA_)
{
    sA_L = spectralFilterAL_.processSample (sA_L);
    sA_R = spectralFilterAR_.processSample (sA_R);
}
// Same for OSC B
```

### prepareToPlay

```cpp
juce::dsp::ProcessSpec monoSpec;
monoSpec.sampleRate       = sr;
monoSpec.maximumBlockSize = (juce::uint32) samplesPerBlock;
monoSpec.numChannels      = 1;
spectralFilterAL_.prepare (monoSpec);
spectralFilterAR_.prepare (monoSpec);
spectralFilterBL_.prepare (monoSpec);
spectralFilterBR_.prepare (monoSpec);
spectralFilterAL_.reset();
// (etc.)
// Initialize with passthrough coefficients
const auto bypass = juce::dsp::IIR::Coefficients<float>::makeAllPass (sr, 20000.0f, 0.707f);
*spectralFilterAL_.coefficients = *bypass;
*spectralFilterAR_.coefficients = *bypass;
*spectralFilterBL_.coefficients = *bypass;
*spectralFilterBR_.coefficients = *bypass;
```

## Param updates

| Param | Before 11c | After 11c |
|---|---|---|
| `SYN_OSC_A_SPECTRAL_TYPE` | choice {"NONE"} (1 option, placeholder) | choice {"Low Pass", "High Pass", "Smear"} (3 options) |
| `SYN_OSC_A_SPECTRAL_AMT` | float 0..1, no DSP | float 0..1, drives cutoff + Q curves |
| OSC B mirrors | same | same |

V1 + Phase 11a/b/d preset compat: index 0 changes meaning from "NONE" to "Low Pass". This is a behavior change for V1 presets — but since SPECTRAL_AMT defaulted to 0 in 11a (filter has near-zero effect), V1 presets at default sound identical (the filter has a 200-Hz upper-cutoff LP at amount=0... wait that's wrong, at amount=0 the LP cutoff is 20000 Hz so the LP filter is passthrough). ✓ V1 presets sound identical at amount=0.

## Files modified

| File | Change |
|---|---|
| `plugins/TerrainInstrument/Source/PluginProcessor.cpp` | (a) Extend SPECTRAL_TYPE choice arrays (A + B) from 1 to 3 (`"Low Pass", "High Pass", "Smear"`). (b) In broadcast block, read SPECTRAL_TYPE + SPECTRAL_AMT (A + B), push via `tv->setSpectral(typeA, amtA, typeB, amtB)`. |
| `plugins/TerrainInstrument/Source/SynthVoice.h` | (a) Add `setSpectral(int typeA, float amtA, int typeB, float amtB)`. (b) Add 4 `juce::dsp::IIR::Filter<float>` members + cached type/amt/bypass state. (c) Initialize filters in `prepareToPlay`. (d) Apply filters per-channel post-fold per-OSC in `renderNextBlock`. |
| `plugins/TerrainInstrument/Source/ui/public/index.html` | OSC A + OSC B back-view SPECTRAL `<select>` `<option>` lists extended from 1 ("NONE") to 3 ("Low Pass" / "High Pass" / "Smear"). |

## Success criteria

1. SPECTRAL TYPE selector on back of both OSCs shows 3 options
2. Hold C4 on ProphetSaw, SPECTRAL TYPE = Low Pass, dial AMT 0→100% — tone goes from full saw to dull fundamental
3. Same with TYPE = High Pass — tone goes from full saw to high-only sizzle
4. Same with TYPE = Smear — phase rotates audibly, character softens
5. V1 preset loads + sounds identical at default (SPECTRAL_AMT=0)
6. No crackles / discontinuities when switching modes (filter state reset implicit on new note)
7. Build green, both binaries fresh

## What 11c V1 does NOT include

- TRUE spectral morph modes (Vocode, Harmonic Stretch, Inharmonic Stretch, Phase Disperse) — these require per-voice IFFT caching + Phase 10c FrameSpec migration. Deferred to Phase 11c.2.
- Smooth coefficient interpolation when SPECTRAL_AMT moves rapidly — accepted as known limitation. May add 1-pole smoothing in polish if user reports zipper noise.
- Per-mode filter slope choice (always 2nd-order/12dB-oct)
- Modulation routing of SPECTRAL TYPE — only AMT is continuous

## Future enabled

- Phase 11c.2 — TRUE spectral morph (4+ more modes added to the choice array, requires Phase 10c)
- Phase 11e — per-engine front panels
- Phase 11f — B1-B4/MIX/MOD blender
