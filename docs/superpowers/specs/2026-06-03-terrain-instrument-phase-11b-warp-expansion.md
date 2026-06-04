# Phase 11b — WARP Expansion (7 new modes)

> **Status:** SPEC drafted 2026-06-03 after deep research into Serum 2 + Vital + Pigments WARP catalogs.
> **Parent:** Phase 11a shipped (5-knob front / back panel scaffold). Phase 8b (per-sine arrays) + 10a (frequency-domain wavetables) underpin this work.
> **Branch:** `feature/terrain-instrument` · **Starting HEAD:** `077030f` (Phase 11a polish landed)
> **Research docs:**
> - `docs/research/2026-06-03-warp-research-serum2.md`
> - `docs/research/2026-06-03-warp-research-vital.md`
> - `docs/research/2026-06-03-warp-research-pigments.md`

## TL;DR

Expand `SYN_OSC_A/B_WARP_MODE` from 4 choices (`NONE`, `BEND`, `SYNC`, `FORMANT`) to **11 choices** by adding 7 new modes drawn from the consensus of Serum 2, Vital, and Pigments research. Mix of phase-domain remaps and post-lookup amplitude transforms. **Rebuild FORMANT** with Vital's true windowed-sync algorithm (current implementation is naive frequency multiply).

## Goal

After Phase 11b: pick a wavetable (e.g. ProphetSaw) → flip WARP mode through the 11 options at 50% amount. Each mode should be **audibly distinct** within 100ms — no "subtle EQ-like" tweaks. Cycling through them while holding a note should feel like switching synth voices.

## The 11 WARP modes

Existing 4 (re-numbered + 1 rebuild):

| # | Mode | Status | What changes |
|---|---|---|---|
| 0 | NONE | unchanged | identity |
| 1 | BEND | unchanged | `phase + amount × 0.5 × sin(2π×phase)` |
| 2 | SYNC | extended | range `1×→16×` exponential (was `1×→5×` linear); algorithm preserved |
| 3 | FORMANT | **rebuild** | windowed-sync: sync ratio × half-sine bell envelope keyed off master phase |

7 new modes:

| # | Mode | Domain | Source | Math (paraphrased) | Sonic at 100% |
|---|---|---|---|---|---|
| 4 | **PWM** | phase | Vital + Serum 2 + Pigments triple-consensus | duty-cycle window: `phase ∈ [0,d) → phase/d; phase ∈ [d,1) → silence` where `d = 1 − amount × 0.45` | thin, narrow-pulse, classic synth lead |
| 5 | **SKEW** | phase | Pigments + Vital | piecewise 2-segment: peak shifts toward 0 or 1: `if (p < knee) → p/knee × 0.5; else → 0.5 + (p−knee)/(1−knee) × 0.5` where `knee = 0.5 − amount × 0.4` | asymmetric harmonic balance, Casio CZ resonant character |
| 6 | **MIRROR** | phase | Serum 2 | `phase < 0.5 → phase × 2; phase ≥ 0.5 → 2 − phase × 2` mapped to a horizontally squeezed-mirror copy, blended with original by `amount` | octave-doubled harmonic emphasis, hollow/glassy |
| 7 | **FRACTALIZE** | phase | Pigments | `warpedPhase = fmod(phase × N, 1.0)` where `N = 1 + amount × 7` (1 → 8 fractional cascade) | comb-filtered organ / brass / metallic depending on integer vs non-integer N |
| 8 | **P-QUANTIZE** | phase | Vital `kQuantize` | `warpedPhase = floor(phase × steps) / steps` where `steps = round(1 + (1 − amount)² × 31)` (cubic-eased 32→1) | harsh digital staircase, lo-fi bit-crush feel |
| 9 | **RECTIFY** | amp | Serum 2 | post-lookup: `out = abs(sAu) × 2 − 1` blended with dry by `amount` (0→100% wet) | octave-up, metallic, fundamental removed |
| 10 | **SINE SHAPER** | amp | Serum 2 | post-lookup: `out = sin(sAu × π/2 × (1 + amount × 4))` | smooth warm distortion, progressive harmonics |

**Skipped (rationale below):**
- AMP QUANTIZE — too similar territory to P-QUANTIZE
- HARD CLIP — too similar to FOLD's LINEAR shape (Phase 11d)
- SQUEEZE (Vital) — covered by SKEW which is the equivalent Pigments form
- Linear Fold / Sine Fold — belong to FOLD axis (Phase 11d)
- FM-from-* / RM-from-* / PD-from-* — cross-osc routing, Phase 11f+

## Architecture

### Phase-domain modes (NONE, BEND, SYNC, FORMANT, PWM, SKEW, MIRROR, FRACTALIZE, P-QUANTIZE)

Compute `warpedPhase` from `uPhaseA_[u]`, then lookup wavetable at `(currentMipLevelA_, fp, warpedPhase)`. PWM also requires a "skip-lookup" branch when in the silence half of the duty cycle.

### FORMANT (rebuild)

True formant = windowed hard-sync:
```
sync_phase = (uPhaseA_[u] × ratio) mod 1.0    where ratio = 2^(amount × 4)
window     = sin(π × uPhaseA_[u])             // half-sine bell, 0 at edges, 1 at center
sAu        = lookup(mip, fp, sync_phase) × window
```

The window keyed off the **un-multiplied master phase** is what makes this "formant" vs. "sync" — the per-cycle amplitude envelope creates vowel character.

### Amp-domain modes (RECTIFY, SINE SHAPER)

Apply normal lookup at `uPhaseA_[u]` (no phase warp), then post-process:
```
RECTIFY:     sAu = lerp(sAu, abs(sAu) × 2 − 1, amount)
SINE SHAPER: sAu = sin(sAu × π/2 × (1 + amount × 4))
```

### Per-mode constants table (lock these for implementation)

| Mode | Constant | Value | Why |
|---|---|---|---|
| SYNC | max ratio | `2^(amount × 4)` = 1×..16× | matches Vital's exponential |
| FORMANT | max ratio | same as SYNC | parallel |
| PWM | min duty | `1 − amount × 0.45` clamped ≥ 0.10 | avoid full silence at amount=1 |
| SKEW | knee | `0.5 − amount × 0.4` clamped ≥ 0.05 | symmetric range |
| FRACTALIZE | max N | `1 + amount × 7` | 1→8 cascade |
| P-QUANTIZE | min steps | `round(1 + (1-amount)² × 31)` | 32→1 cubic |
| RECTIFY | wet blend | linear `amount` | smooth fade |
| SINE SHAPER | max drive | `1 + amount × 4` | 1→5 drive |

## What we ship in Phase 11b

| Layer | Change |
|---|---|
| **APVTS** | `SYN_OSC_A_WARP_MODE` + `SYN_OSC_B_WARP_MODE` choice array extends from 4 to 11 options. V1 preset compat preserved (indices 0-3 unchanged). |
| **SynthVoice** | OSC A + OSC B WT engine cases: extend `switch (warpMode_)` with new cases 4-10. FORMANT case rebuilt. |
| **PluginProcessor** | No change (broadcast already pushes WARP_MODE int). |
| **PluginEditor** | No change to `WebSliderRelay` — still binds the choice param. |
| **index.html** | Update OSC A + OSC B back-view WARP `<select>` `<option>` lists from 4 to 11 items. Display names: `NONE`, `Bend`, `Sync`, `Formant`, `PWM`, `Skew`, `Mirror`, `Fractalize`, `P-Quantize`, `Rectify`, `Sine Shaper`. |

## V1 preset compatibility

Existing presets store `SYN_OSC_A_WARP_MODE` as int 0-3. After 11b, the choice array accepts 0-10. Indices 0-3 keep same meaning (NONE/BEND/SYNC/FORMANT). FORMANT is REBUILT internally — V1 presets will sound slightly different on FORMANT because the new windowed-sync replaces the naive multiply. Acceptable cost given the headline upgrade.

## Success criteria

1. WARP MODE selector on the back of OSC A + OSC B shows 11 options
2. Loading a V1 preset doesn't crash and audio sounds correct for indices 0-3 (modulo FORMANT improvement)
3. Each of the 7 new modes produces audibly distinct timbre at WARP knob = 50% on ProphetSaw at C4
4. PWM at amount=1 produces a thin pulse (not silence)
5. RECTIFY at amount=1 produces an octave-up harmonic
6. P-QUANTIZE at amount=1 produces a fundamental-only square (extreme staircase)
7. FRACTALIZE at amount=0.5 (N≈4.5) produces inharmonic comb-filtered tone
8. Build + install green, both binaries fresh
9. No regressions on NONE/BEND/SYNC

## What Phase 11b does NOT include

- New choice param for SPECTRAL/FOLD — those stay 1-option until 11c/11d
- Per-mode WARP 2 (Serum's dual warp) — phase 11b+ work, not now
- New parameters beyond WARP_MODE expansion — no new APVTS entries
- UI visual cue for which mode is selected beyond the dropdown label
- Anti-aliasing for the new modes — accept some aliasing for analog character; Phase 11c/Phase 10c will address WT harmonics more rigorously

## Future phases enabled

- Phase 11c (SPECTRAL DSP) — separate axis, doesn't touch WARP
- Phase 11d (FOLD DSP) — separate axis, doesn't touch WARP
- Phase 11e (per-engine front panels for FM/NS/SAMP/GRAN/SPEC)
- Phase 11f (B1-B4/MIX/MOD blender)
