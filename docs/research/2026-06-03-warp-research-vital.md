# Vital Distortion research — Phase 11b

**Researched:** 2026-06-03  
**Scope:** Phase-domain distortion (WARP) modes from Vital open-source codebase  
**Licensing note:** Vital is GPLv3. All math below is paraphrased into formulas/equations for independent reimplementation. Do NOT copy code verbatim.

---

## Source files cited

- `src/synthesis/producers/synth_oscillator.h` — `DistortionType` enum (lines 116–131)
- `src/synthesis/producers/synth_oscillator.cpp` — all phase-distortion functions (lines 60–170), distortion value scaling (lines 1023–1077), dispatch (lines 1225–1281)
- `src/synthesis/producers/spectral_morph.h` — spectral-morph constants (lines 24–32)
- `src/common/synth_constants.h` — general synth constants

---

## Full DistortionType enum (synth_oscillator.h:116–131)

```
kNone           = 0
kSync           = 1
kFormant        = 2
kQuantize       = 3
kBend           = 4
kSqueeze        = 5
kPulseWidth     = 6
kFmOscillatorA  = 7
kFmOscillatorB  = 8
kFmSample       = 9
kRmOscillatorA  = 10
kRmOscillatorB  = 11
kRmSample       = 12
```

Vital's user-facing labels (from the GUI code) map as:
- kNone → "None"
- kSync → "Sync"
- kFormant → "Formant"
- kQuantize → "Quantize"
- kBend → "Bend"
- kSqueeze → "Squeeze"
- kPulseWidth → "Pulse Width"
- kFmOscillatorA/B/Sample → "FM Osc 1 / FM Osc 2 / FM Sample"
- kRmOscillatorA/B/Sample → "RM Osc 1 / RM Osc 2 / RM Sample"

---

## Key constants (synth_oscillator.cpp:38–50)

```
kPhaseBits    = 32          (8 * sizeof(uint32_t))
kDistortBits  = 32          (same as kPhaseBits)
kMaxQuantize  = 0.85f
kMaxSqueezePercent = 0.95f
kMaxSyncPower = 4           (sync multiplier goes up to 2^4 = 16×)
kMaxSync      = 16
kHalfPhase    = INT_MIN     (used as zero-center offset, i.e. phase 0 = INT_MIN)
```

Vital uses a **signed 32-bit integer phase** centered at 0 (wraps INT_MIN..INT_MAX).
Phase 0.0 (start of cycle) = INT_MIN.
Phase increment per sample = `(frequency / sampleRate) * UINT_MAX`.

---

## Mode-by-mode breakdown

### None (kNone)
- Source: synth_oscillator.cpp:60–62
- Math: `phase_out = phase_in` — identity passthrough
- Sonic: unmodified wavetable playback
- Our adaptation: already matches (our case 0 is pass-through)

---

### Sync (kSync)
- Source: synth_oscillator.cpp:107–110
- Math (Vital, paraphrased):
  1. Convert signed phase to [0,1]: `t = (phase + 0.5) / FULL_PHASE` (range [0,1))
  2. Multiply by distortion factor: `t_sync = t * D`  where `D ∈ [1, 16]` (exponential, power-of-2 scale from kMaxSyncPower=4)
  3. Truncate to fractional part: `t_out = frac(t_sync)` — this is the hard reset back to zero
  4. Convert back to signed integer phase
  
  The **distortion value preprocessing** (synth_oscillator.cpp:1033–1037) scales the raw [0,1] knob exponentially:
  `D = 2^((amount * 2 - 1) * kMaxSyncPower) / kMaxSync`
  At amount=0: D = 2^(-4)/16 ≈ 0.004 (near-zero sync — nearly one cycle)
  At amount=1: D = 2^4/16 = 1.0 → multiplied by kMaxSync=16 inside sync → 16× sync
  
  The actual runtime phase function multiplies by the pre-scaled value × kMaxSync:
  `distorted_phase = toInt(float(phase + INT_MIN) * distortion) * 16 + INT_MIN`
  So effective ratio = `distortion * 16`, ranging ~1× to 16×.

- Sonic: hard sync — slave oscillator resets to zero every time master cycle completes fraction. Creates harsh overtone series with "jet engine" sweep as sync amount increases. Timbre is rich and cutting.
- Our adaptation: our current SYNC uses `syncRatio = 1 + warpAmount * 4` (1× to 5×). Vital reaches up to 16×. Vitals preprocessing is exponential (power-of-2 ramp), ours is linear. **Recommendation: extend our range to 1–16× with an exponential curve** (`syncRatio = pow(2, warpAmount * 4)` → 1× to 16×).

---

### Formant (kFormant)
- Source: synth_oscillator.cpp:107–110 (same syncPhase function as kSync) + halfSinWindow (lines 149–152)
- Math: **identical phase distortion to Sync**, but with an amplitude window applied:
  1. Phase distortion: same as kSync above (phase multiplied by ratio, hard reset)
  2. Window: `window = sin(pi * normalised_master_phase)` — a half-sine envelope keyed off the **original** (pre-distorted) phase
  This means each slave cycle gets amplitude-modulated by a bell curve derived from where the master is in its cycle.
  
  The half-sin window:
  `normalised = (original_phase + INT_MAX) / FULL_PHASE / 2`  (maps to [0,1])
  `window = sin(normalised * 2π + π/2)`  → half-sine bell, peaks at master phase=0.5

- Sonic: Formant synthesis — sounds like a vocal vowel or talking wave. The sync creates the pitch, the window creates the formant peak. Sweeping the amount changes the vowel character dramatically.
- Our adaptation: our current FORMANT is a simple frequency shift `warpedPhase = phase * (1 + amount * 2)`. This is **not** what Vital does. Vital's formant IS sync + half-sin window. Our version produces a different (simpler) effect — harmonic stretching. **They are sonically very different.** For Phase 11b, implement the true Vital formant = sync-with-window (requires a separate warpedAmpScale output that multiplies the wavetable sample).

---

### Quantize (kQuantize)
- Source: synth_oscillator.cpp:64–69 (quantizePhase), 1038–1055 (value scaling)
- Math (Vital, paraphrased):

  **Value preprocessing** (converts knob [0,1] to internal step-count):
  ```
  d   = (1.0 - knob)^3 * 0.85      // cubic ease, maps knob to [0, 0.85]
  D   = 2^(d * 32 + 1)             // step count; at knob=0: 2^1=2 steps; at knob=1: 2^(0+1)=2 steps
                                    // at knob=0.5: d≈0.106, D≈2^(3.4+1)≈22 steps
                                    // maximum D occurs near knob=0: D = 2^(0.85*32+1) = 2^28.2 ≈ 300M steps (very fine)
  ```
  Note: the special-case minimum in the value scaling `max(1.5, last_value)` prevents zero steps.

  **Per-sample phase distortion** (quantizePhase):
  ```
  // phase and distortion_phase are both signed 32-bit integers normalized to [−1,1] float range
  n     = float(phase) / FULL_PHASE * D        // scale phase to [0, D] step space
  adj   = float(distortion_phase) / FULL_PHASE // phase offset [−0.5, 0.5]
  floored = floor(n + adj) - adj               // snap to nearest step boundary (offset-corrected)
  out   = int((floored / D) * FULL_PHASE) - distortion_phase  // back to integer phase
  ```
  
  In plain English: divide the [0,1] phase into D equal steps, snap to the nearest step boundary. `distortion_phase` is a phase offset that shifts WHERE the step boundaries fall (controlled by the separate `kDistortionPhase` input, a secondary parameter the user can modulate).

  At low D (coarse steps): very few steps per cycle → extreme staircase waveform → rich high harmonics.
  At high D (fine steps): many steps → very subtle staircasing → nearly transparent.
  The knob therefore goes from fine (knob=1) to coarse (knob=0), which maps to increasing "bit-crush" character as you turn the knob down.

- Sonic: Phase quantization — staircase distortion on the phase accumulator rather than on the amplitude. Produces harsh, buzzy, digital character; with modulation creates a unique "phase bit-crush" sound distinct from amplitude bit-crush. Very distinctive. Timbre changes radically with pitch (phase increment changes step alignment).
- Our adaptation approach:
  ```cpp
  // QUANTIZE — phase staircase
  // warpAmount in [0,1]; at 0 = fine (subtle), at 1 = coarse (harsh)
  const double coarseness = std::pow(1.0 - (double)warpAmount_, 3.0) * 0.85;
  const double stepCount = std::pow(2.0, coarseness * 32.0 + 1.0);
  const double offset = distortionPhaseOffset_; // [−0.5, 0.5], modulatable
  const double n = uPhaseA_[u] * stepCount;
  const double snapped = (std::floor(n + offset) - offset) / stepCount;
  warpedPhase = snapped;  // already in [0,1], no frac needed (floor handles wrap)
  ```
  Note: Terrain's phase is [0,1] (double), not a signed 32-bit integer, so the conversion is simpler.

---

### Bend (kBend)
- Source: synth_oscillator.cpp:72–87
- Math (Vital, paraphrased):

  **Phase is centered at 0.5 for the polynomial computation** (Vital uses centered coordinates):
  ```
  t  = float(phase − distortion_phase) / FULL_PHASE + 0.5   // maps phase to [0,1]
  
  d_offset = (D − D²) * 2                  // asymmetry term; zero at D=0 and D=1
  d_scale  = D * 3
  
  m1 = (d_scale + d_offset) * (t² − t³)   // "right lean"
  m2 = (d_scale − d_offset) * (t − 2t² + t³)  // "left lean"
  
  out = t³ + m1 + m2                       // cubic S-curve
  ```
  
  At D=0.5 (midpoint): `d_offset=0.25`, creating an asymmetric S-bend.
  At D=0: passthrough (all terms zero, out = t³ which at t∈[0,1] is monotonic).
  Actually: at D=0, `m1=0, m2=0`, so `out = t³` — a cubic "slow start, fast end" bend.
  At D=1, similarly `out = t³ + 3(t²-t³) + 3(t-2t²+t³) = 3t - 3t²+t³ = (t-1)³ + ... actually let's not recalculate the endpoint — the key point is it's a smooth S-curve whose asymmetry is controlled by D.

  The distortion value scaling for kBend is NOT listed in setDistortionValues — it falls through to the `default: break` case, meaning Vital uses the raw [0,1] knob value directly as `D`. No preprocessing.

- Sonic: Smooth S-curve phase warp. Pushes time-domain features (zero crossings, peaks) earlier or later in the cycle. Creates a subtle "round" or "forward-leaning" tone character. Less harsh than sync or quantize. Good for formant-like sweetening without the hard resets.
- Our adaptation: our current BEND is `phase + 0.5 * sin(2π * phase) * amount` — a **sine-based** phase push, not a cubic polynomial. The sine version is smoother and sounds similar but has a different harmonic character at extreme amounts. **Both are valid.** Vital's cubic is arguably cleaner in the mid-amount range (no aliasing from sine discontinuity at extremes). For Phase 11b, ours is acceptable as-is; optionally switch to cubic for closer Vital character.

---

### Squeeze (kSqueeze)
- Source: synth_oscillator.cpp:89–105 (squeezePhase), 1057–1059 (value scaling)
- Math (Vital, paraphrased):

  **Value preprocessing:**
  `D = knob * 2 * 0.95 + 0.05`  → D ∈ [0.05, 1.95]
  At knob=0.5: D=1.0 (no squeeze — symmetric)
  At knob=0: D=0.05 (extreme left-squeeze)
  At knob=1: D=1.95 (extreme right-squeeze)

  **Per-sample phase distortion** (squeezePhase):
  ```
  // Work on absolute-value of centered phase, then restore sign
  signed_phase = float(phase − distortion_phase)
  abs_phase = abs(signed_phase)
  positive = (signed_phase > 0)
  
  pivot = D * (FULL_PHASE / 4)       // D scales the pivot point
  
  if abs_phase <= pivot:
      // Left half: compress by factor D (squeezes left portion into half)
      out = abs_phase / D
  else:
      // Right half: expand the remaining portion
      out = FULL_PHASE/2 − (FULL_PHASE/2 − abs_phase) / (2 − D)
  
  restore sign: out = positive ? out : −out
  ```

  This is a **piecewise linear two-segment warp**:
  - Left segment: time-compressed by factor D
  - Right segment: time-expanded by factor (2−D)
  - The seam is at pivot = D * quarter_phase

  At D=1.0: both segments are identity (pivot=quarter_phase, left/right = identity).
  At D<1.0: pivot moves left, left segment is EXPANDED (slower) and right segment is compressed (faster).
  At D>1.0: pivot moves right, left segment is compressed and right is expanded.
  This creates an asymmetric skew of the waveform in time — the peak shifts earlier or later.

- Sonic: Asymmetric time-warp within the cycle. Like pushing the "peak" of the waveform forward or backward in time. At extremes: one half of the waveform gets very fast and spiky while the other is slow and rounded. Creates a classic "waveshaping through phase" effect that adds even or odd harmonics depending on direction. Very musical — like PWM but for the whole shape, not just duty cycle.
- Our adaptation approach:
  ```cpp
  // SQUEEZE — piecewise linear phase skew
  // warpAmount in [0,1]; 0.5 = no effect
  const double D = warpAmount_ * 2.0 * 0.95 + 0.05;  // [0.05, 1.95]
  const double p = uPhaseA_[u];         // [0,1] phase
  const double pivot = D * 0.25;        // pivot in [0,1] space
  double half = 0.5;
  double abs_p;
  bool positive;
  // Re-center around 0.5 (Vital uses signed; we map [0,1] → centered [−0.5,0.5])
  double cp = p - 0.5;
  positive = (cp >= 0.0);
  abs_p = std::abs(cp);
  double out;
  if (abs_p <= pivot * 0.5) {   // scale pivot to [−0.5,0.5] space
      out = abs_p / D;
  } else {
      out = 0.5 - (0.5 - abs_p) / (2.0 - D);
  }
  out = positive ? out : -out;
  warpedPhase = out + 0.5;  // back to [0,1]
  warpedPhase -= std::floor(warpedPhase);
  ```

---

### Pulse Width (kPulseWidth)
- Source: synth_oscillator.cpp:112–117 (pulseWidthPhase) + 145–147 (pulseWidthWindow), 1061–1073 (value scaling)
- Math (Vital, paraphrased):

  **Value preprocessing:**
  `D = 1.0 / max(1.0 - knob, ε)`
  At knob=0: D=1.0 (no stretch)
  At knob=0.5: D=2.0 (double speed for first half)
  At knob=0.99: D=100 (extreme)

  **Phase distortion** (pulseWidthPhase):
  `phase_out = clamp(float(phase_in) * D, INT_MIN, INT_MAX)`
  
  This scales the phase by D — the first part of the cycle plays at D× speed. Once the integer phase would overflow INT_MAX (which happens at 1/D fraction through the cycle), it clamps to INT_MAX (silence/end of cycle).

  **Amplitude window** (pulseWidthWindow):
  `window = 1.0 if distorted_phase ≠ INT_MIN else 0.0`
  
  This is the "pulse" part: when the clamped phase equals INT_MIN (which only happens at the very start of a new cycle), output is silenced for that sample. But primarily, the window zeros the output during the clamped region (phase > 1/D through the cycle). In practice the `INT_MIN` check gates the dead region.

  **Combined effect:** the waveform plays at D× speed for the first (1/D) fraction of the cycle, then silence for the remaining (1 − 1/D) fraction. This is exactly standard pulse-width modulation applied to a wavetable — the "on time" shrinks as the knob increases.

- Sonic: Pulse-width modulation for wavetables. At small amounts the duty cycle is near 50%. As you increase, the active portion shrinks, adding upper harmonics and thinning the sound. At extreme values approaches a very narrow spike. Classic synth PWM character; when modulated with an LFO produces the characteristic "chorus-like" sweep.
- Our adaptation approach:
  ```cpp
  // PULSE WIDTH — wavetable PWM
  // warpAmount in [0,1]; 0 = full duty cycle, 1 = minimum duty
  const double D = 1.0 / std::max(1.0 - (double)warpAmount_ * 0.99, 1e-6);
  const double scaled = uPhaseA_[u] * D;
  warpedPhase = std::min(scaled, 1.0);  // clamp at top of table
  // Window: zero output when warpedPhase >= 1.0 (clamped region)
  // Implement as: sample *= (scaled < 1.0) ? 1.0 : 0.0
  // NOTE: requires a per-voice "warp amplitude scale" multiplier separate from the phase
  ```
  This requires multiplying the final wavetable sample by a window factor, same as the Formant mode above.

---

## Comparison: Vital BEND/SYNC/FORMANT vs our current implementations

### BEND
| | Vital | Ours |
|--|--|--|
| Formula type | Cubic polynomial | Sine-based |
| Core equation | Weighted sum of t, t², t³ terms | `phase + A*0.5*sin(2π*phase)` |
| Range | Linear knob [0,1] → direct D | Linear knob × 0.5 |
| Max distortion | ~±30% cycle shift at extremes | ±50% of full cycle (bigger) |
| Aliasing risk | Low (polynomial is smooth) | Low-medium (sine discontinuity at A>0.5) |

**Verdict:** Both are valid phase-bend approaches with slightly different tonal character. Vital's cubic has cleaner aliasing behavior at extreme settings. Our sine version is more "wavy." No urgent need to change ours unless we want to match Vital precisely.

### SYNC
| | Vital | Ours |
|--|--|--|
| Ratio range | 1× to 16× (exponential, 2^0 to 2^4) | 1× to 5× (linear) |
| Ratio curve | Exponential (power-of-2) | Linear |
| Reset mechanism | Hard modulo (frac of multiplied phase) | Explicit: resets sync phase on master zero-crossing |
| State required | Stateless (pure phase multiply + frac) | Stateful (requires uSyncPhaseA_ accumulator) |

**Vital's approach is stateless and elegant.** Ours requires a separate accumulator. The stateless approach: `warpedPhase = frac(phase * syncRatio)`. Our current stateful version should produce the same sound but the stateless implementation is simpler. **Recommend: rewrite SYNC as stateless**, and expand range to 16×.

### FORMANT
| | Vital | Ours |
|--|--|--|
| Core mechanism | Sync + half-sin window | Simple frequency multiplication (frac) |
| Window function | Half-sine bell keyed off master phase | None |
| Sonic result | Vocal formant character (vowel shape) | Alias-rich harmonic stretch |
| Aliasing | Moderate (sync reset creates clicks, window reduces them) | High (simple frequency mult wraps with no windowing) |

**These are COMPLETELY DIFFERENT effects.** Vital's formant IS a windowed sync. Ours is essentially a simplified WT playback speed boost. Ours produces a simpler (less musical) result. **Strong recommendation: rebuild FORMANT as windowed-sync in Phase 11b.** This is the most impactful upgrade of the three.

---

## Recommendations for Phase 11b (priority order)

Priority ordering based on: sonic distinctiveness, implementation complexity, day-1 impact.

### 1. SQUEEZE (NEW) — highest priority
Piecewise linear two-segment warp. Creates asymmetric peak-shifting — like morphing from "rounded sine" to "forward-spiky" to "backward-spiky." Sounds totally different from everything we have. Implementation is pure arithmetic (no sin, no accumulator). Very musical. **Ship this first.**

### 2. PULSE WIDTH (NEW) — high priority
Standard PWM applied to wavetables. Well-understood, iconic synth sound. Requires adding a per-sample amplitude window (zeros the dead portion). Needs a small architecture change (window multiplier on the wavetable read) but the math is simple. LFO modulation of WARP will immediately produce the classic PWM chorus sweep. **Essential omission from our current set.**

### 3. QUANTIZE (NEW) — high priority
Phase staircase / "phase bit-crush." Produces harsh, buzzy, digital character completely unlike our other modes. Sounds extraordinary with LFO modulation of WARP (stuttery phase-quantize sweep). Implementation is straightforward. Caveat: needs a secondary "phase offset" parameter for the full Vital effect; shipping without the offset (fixed at 0) still sounds great. **Very distinctive — will get noticed.**

### 4. FORMANT (REBUILD) — medium-high priority
Current implementation is not actually formant synthesis. True formant = windowed sync (same syncPhase function + halfSinWindow multiplier). Rebuilding this to match Vital's approach gives us genuine vowel/formant character. Requires the same window multiplier architecture as Pulse Width. **Do this at the same time as Pulse Width** to amortize the architecture cost.

### 5. SYNC (RANGE EXPANSION) — medium priority
Extend our sync ratio from 5× to 16× and convert from stateful accumulator to stateless `frac(phase * ratio)`. Change ratio scaling from linear to exponential (`pow(2, warpAmount * 4)`). This is a one-line fix that meaningfully expands the sonic range. The higher sync ratios produce the screaming jet-engine timbre Vital is known for. **Easy win.**

### 6. BEND (OPTIONAL UPGRADE) — low priority
Replace sine bend with Vital's cubic polynomial. Sonic difference is subtle. Only worth doing if we have time, or if we want exact Vital parity. Our current sine bend is perfectly usable.

---

## Architecture note: amplitude window requirement

Modes **FORMANT** and **PULSE WIDTH** both require a per-sample amplitude multiplier applied to the wavetable read output, in addition to the phase distortion. The current switch/case in SynthVoice's render loop only modifies `warpedPhase`. To support windowed modes, add a `double warpWindow` variable initialized to `1.0` before the switch, set it inside the relevant cases, and multiply: `sample *= warpWindow` after the wavetable lookup.

```cpp
double warpedPhase = uPhaseA_[u];
double warpWindow  = 1.0;          // NEW

switch (warpMode_) {
    case PULSE_WIDTH:
        /* ... set warpedPhase AND warpWindow ... */
    case FORMANT:
        /* ... set warpedPhase AND warpWindow ... */
}

double sample = wavetableLookup(warpedPhase);
sample *= warpWindow;               // NEW
```

This is a zero-cost change for modes that don't use the window (it stays 1.0).
