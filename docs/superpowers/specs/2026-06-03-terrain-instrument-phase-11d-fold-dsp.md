# Phase 11d — FOLD DSP (3 shapes: Linear / Sine / Triangle)

> **Status:** SPEC drafted 2026-06-03 after deep research into wavefolder DSP (Pigments + Buchla + Vital).
> **Parent:** Phase 11b shipped (`mark-2-synth-phase-11b-warp-expansion`). Phase 11a + 10a + 8b architecture intact.
> **Branch:** `feature/terrain-instrument` · **Starting HEAD:** Phase 11b tag commit
> **Research:** `docs/research/2026-06-03-fold-research.md`

## TL;DR

Activate the FOLD axis on the front panel. Implement 3 wavefolder shapes — LINEAR (Serge), SINE (Vital), TRIANGLE (Buchla 259, 3-stage cascade) — each with a quadratic pre-gain curve mapped to the FOLD AMT knob. Fold runs PER-UNISON-SINE on the engine output (after WARP/RECTIFY/SINE-SHAPER), so SPREAD + FOLD compose interestingly. All 3 shapes are output-bounded — no DC blocker this phase (TRIANGLE accepts mild DC at extremes — to address in polish if audible).

## Goal

After Phase 11d: pick any engine output, dial FOLD AMT 0→100% — at 100% the output should be dramatic and harmonically rich. Cycling FOLD SHAPE between Linear / Sine / Triangle should feel like three different distortion characters: Linear = "buzzy-Serge", Sine = "smooth bell warmth", Triangle = "thick analog grit".

## The 3 FOLD shapes

| # | Shape | Source | Math (closed form) | Sonic at 100% |
|---|---|---|---|---|
| 0 | **Linear** | Serge fold | `pre = 1 + amount² × 9`; `driven = x × pre`; `out = 4 × abs((driven + 1)/4 − round((driven + 1)/4)) − 1` | aggressive Serge buzz, near-infinite odd harmonics, slight aliasing |
| 1 | **Sine** | Vital | `pre = 1 + amount² × 5.28`; `out = sin(x × pre)` | smooth, bounded ±1, bell-like 5th harmonic resonance, no aliasing concerns |
| 2 | **Triangle** | Buchla 259 | `pre = 1 + amount² × 5`; cascade 3 stages: `(0.5 × linfold(driven × 1.0)) + (0.35 × linfold(driven × 1.414)) + (0.15 × linfold(driven × 2.0))` | warm West-Coast wavefold, mix of odd+even harmonics, mild DC at extremes |

Constants verified against the research doc + cross-referenced with Buchla 259 DAFx17 measurements.

### Why quadratic pre-gain (amount²)?

Linear pre-gain (amount × K) makes the bottom half of the knob (0..50%) too aggressive — you hit folding instantly. Quadratic puts the "sweet spot" in the upper half, giving 0..50% a clean ramp before harmonics start blooming. Matches Vital's amount-scaling philosophy.

### Why bounded shapes only?

Sine and Triangle are mathematically bounded to ±1 by construction. Linear is bounded by the triangle-wave closed form (output is ±1 at maximum drive). No need for output clipping anywhere.

### DC offset

TRIANGLE shape introduces mild DC at high drive on asymmetric inputs. **Phase 11d ships without a DC blocker** — accepted limitation. If audibly objectionable in DAW testing, a per-voice one-pole HP at 5Hz can be added in polish.

## Architecture

### Where in the signal chain

Per Pigments DSP order: `WT POS → WARP → SPECTRAL (Phase 11c) → FOLD → output`. So FOLD comes after WARP (and after the Phase 11b RECTIFY/SINE SHAPER post-transforms). In our code:

```cpp
for each unison sine u:
    float sAu = computeEngineSample(u);   // WT / NOISE / FM / etc.
    sAu = applyFold(sAu);                  // NEW Phase 11d
    sumAL += sAu * uPanL_[u];
    sumAR += sAu * uPanR_[u];
```

Fold runs PER-SINE so each unison voice gets independently folded — gives the unison stack a richer texture (Vital and Pigments do this).

### Apply to all engines (not just WT)

Fold transforms whatever the engine outputs. NOISE folds to nasal/buzzy; FM folds to even-more-clangorous; SAMP/GRAN/SPEC are silent stubs (fold of zero is zero). Fold is engine-agnostic.

### Implementation as an inline helper

A `static inline float applyFold(float x, int shape, float amount)` works because all 3 shapes are stateless. No DC blocker = no per-voice state required for Phase 11d.

## Param updates

| Param | Before 11d | After 11d |
|---|---|---|
| `SYN_OSC_A_FOLD_SHAPE` | choice {"LINEAR"} (1 option, placeholder) | choice {"Linear", "Sine", "Triangle"} (3 options) |
| `SYN_OSC_A_FOLD_AMT` | float 0..1, no DSP effect | float 0..1, drives `pre = 1 + amount² × K` |
| OSC B mirrors | same | same |

V1 + Phase 11a preset compat: index 0 stays "Linear" (was "LINEAR" — string change is cosmetic, index identity preserved). FOLD_AMT was 0 by default and had no DSP — V1 presets sound identical at default settings.

## Files modified

| File | Change |
|---|---|
| `plugins/TerrainInstrument/Source/PluginProcessor.cpp` | (a) Extend FOLD_SHAPE choice arrays (A + B) from 1 to 3. (b) In the broadcast block, read FOLD_SHAPE + FOLD_AMT (A + B) and push via new `tv->setFold(shapeA, amtA, shapeB, amtB)`. |
| `plugins/TerrainInstrument/Source/SynthVoice.h` | (a) Add `setFold(int shapeA, float amtA, int shapeB, float amtB)` setter. (b) Add private members `foldShapeA_/B_`, `foldAmountA_/B_`. (c) Add `static inline float applyFoldA(float)` / `applyFoldB(float)` helpers (or inline directly into render loop). (d) Call `applyFold` in the unison loops after engine compute, before pan accumulation. (e) Both OSC A + OSC B. |
| `plugins/TerrainInstrument/Source/ui/public/index.html` | OSC A + OSC B back-view FOLD `<select>` `<option>` lists extended from 1 to 3 ("Linear" / "Sine" / "Triangle"). |

## Success criteria

1. FOLD SHAPE selector on back of both OSCs shows 3 options (Linear / Sine / Triangle)
2. With UNISON=1, hold C4 on Square wavetable, dial FOLD AMT 0→100%:
   - Linear: at 50%, gritty buzz starts; at 100%, harsh harmonics
   - Sine: at 50%, gentle bell warmth; at 100%, complex folded sine
   - Triangle: at 50%, soft analog growl; at 100%, rich combined harmonics
3. With UNISON=4 + SPREAD=50, hold C4 on ProphetSaw, FOLD at 75%: textured & wide
4. V1 preset loads + sounds identical at default (FOLD_AMT=0)
5. Build green, both binaries fresh

## What 11d does NOT include

- DC blocker (deferred — accept mild DC at TRIANGLE extremes)
- Anti-aliasing for high-pitched LINEAR fold (deferred — Phase 11g/h might address)
- 4th fold shape (e.g. Chebyshev) — keep to 3 for now
- Pre-fold filter or post-fold filter
- Modulation of FOLD SHAPE (only AMT is continuous; SHAPE is a static choice)

## Future phases enabled

- Phase 11c (SPECTRAL DSP) — independent axis, no FOLD interaction needed
- Phase 11e (per-engine front panels)
- Phase 11f (B1-B4/MIX/MOD blender)
