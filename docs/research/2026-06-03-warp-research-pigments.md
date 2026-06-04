# Pigments 7 Phase Transform research — Phase 11b

## Source: Pigments 7 manual pp 96-97 (section 8.6, "Phase Transform")

The manual's own framing: "Phase transformation (more commonly called phase distortion) changes
the shape of a waveform according to one of seven modulator waves, which are known as Types in
Pigments. Think of a mirror in a carnival funhouse: when you look in it, you see your image
reflected according to the shape of the mirror. It's still you, but it has transformed."

Key technical note from manual: "The remap curves for each Target wave are based on the way they
affected a sine wave, so the results will vary when the input (original) waveform is more complex."

The amount knob is labelled "PD Amount" (Phase Distortion Amount) — a single knob whose
range goes from 0 (no transform, pass-through) to 1.0 (maximum distortion). There is also a
separate "Phase Mod knob" to modulate the PD Amount from the Wavetable Modulator oscillator
(section 8.6.3). The seven types are selected from a waveform icon dropdown.

---

## The 7 Phase Transform shapes

---

### 1. Pulse Width

- **Pigments display name:** Pulse Width
- **Manual description:** "Adds subtle to sharp harmonic edge on most waves"
- **Math hint from manual:** None explicit — but "pulse width" in phase distortion context
  is the Casio CZ classic: the phase axis is warped so the first half of the cycle is compressed
  and the second half is stretched (or vice versa). This creates odd harmonics when applied to
  a symmetric wave, and nasty edge-like harmonics on a saw.
- **Sonic effect on sine input:** At low amount: gentle asymmetric thickening, mild odd harmonics.
  At high amount: sharp clipped-square-like edge — the sine's peak region is smeared into a
  plateau. On a saw, it progressively sharpens the transient edge.
- **Proposed C++ phase-warp formula:**
  ```cpp
  // Pulse Width: remap phase so [0, pw) spans first half, [pw, 1) spans second half
  // pw = 0.5 + warpAmount * 0.499  (range: 0.5 → ~1.0, asymmetric compression)
  float pw = 0.5f + warpAmount * 0.499f;
  float warpedPhase;
  if (uPhase < pw) {
      warpedPhase = uPhase * (0.5f / pw);           // first half stretched/compressed to [0, 0.5)
  } else {
      warpedPhase = 0.5f + (uPhase - pw) * (0.5f / (1.0f - pw)); // second half fills [0.5, 1.0)
  }
  // warpedPhase is in [0,1) — no wrap needed since both branches stay in range
  ```
  At `warpAmount = 0`: `pw = 0.5` — perfectly symmetric, no change.
  At `warpAmount = 1`: `pw ≈ 1.0` — first half expands to fill almost all of [0, 0.5), second half
  is an infinitely steep spike (pure impulse behaviour, maximum harmonics).
- **0% / 50% / 100% sonic trajectory:**
  - 0%: Unmodified waveform
  - 50%: Mild PWM-like asymmetry; on sine sounds like a mix of sine + odd harmonics
  - 100%: Hard edge / near-square timbre on sine; saw becomes very bright and aggressive

---

### 2. Skew

- **Pigments display name:** Skew
- **Manual description:** "Works with most waveforms: peaks are spread to the left and right,
  leaving a valley"
- **Manual worked example:** "Hold a note and slowly increase the PD Amount. Harmonics will
  be added gradually to the Sine wave as its amplitude peaks are skewed to the left and right."
- **Sonic effect on sine input:** The single crest of a sine divides into two crests that migrate
  toward 0° and 180°, with a growing valley in the middle. Result: even harmonics emerge (the
  symmetry is broken between peaks). Sounds like it gains a "W" shape — bright but with a midrange
  notch quality.
- **Proposed C++ phase-warp formula:**
  ```cpp
  // Skew: warp using a sine-based double-speed remap
  // The remap curve for "Skew" is likely a half-rectified or folded sine applied to the phase axis.
  // The "peaks spread left and right leaving a valley" is consistent with a remap that has two
  // acceleration zones near 0 and 0.5 and a deceleration in the middle.
  //
  // Simplest model: bilinear skew toward the two poles
  // f(t) = t + A * sin(2*PI*t) * (A_scale)   — classic "sine phase distortion"
  float twoPi = 6.28318530718f;
  float warpedPhase = uPhase + warpAmount * 0.25f * std::sin(twoPi * uPhase);
  warpedPhase -= std::floor(warpedPhase);  // wrap to [0,1)
  ```
  Reasoning: adding `A * sin(2πt)` to the phase creates faster traversal near 0 and 0.5 (the zero
  crossings of sin) and slower traversal near the peaks at 0.25 and 0.75 — which, when the
  wavetable lookup runs faster through those regions, compresses the sine crest and stretches the
  flanks. At max amount (~0.25 coefficient) the phase curve still stays monotonic (derivative ≥ 0),
  so no folding occurs — pure harmonic expansion. This is the Casio CZ "resonant wave 1" family.
- **0% / 50% / 100% sonic trajectory:**
  - 0%: Unmodified
  - 50%: Gentle nasal formant-like coloration; even harmonics growing in
  - 100%: Rich odd+even harmonic spectrum with "W"-shaped waveform; buzzy, synth-brass quality

---

### 3. Round

- **Pigments display name:** Round
- **Manual description:** "The source is influenced by a semi-square; it could gain valleys and/or
  plateaus"
- **Sonic effect on sine input:** Sections of the waveform are driven toward flat plateaus (top or
  bottom clipping) while transitions sharpen. The phrase "semi-square" suggests the remap curve
  is roughly square-ish — meaning fast traversal through the transition regions and slow (plateau)
  traversal near the peak/trough. A sine through this remap rounds its top and bottom into flat
  sections, transforming toward a square wave.
- **Proposed C++ phase-warp formula:**
  ```cpp
  // Round: remap using a curve that creates plateaus at phase extremes
  // A tanh-based remap compresses the phase "travel" near 0 and 0.5 (plateau at peak/trough)
  // while accelerating through the midpoint (sharp zero crossing)
  // remap: output = asin(clamp(A * sin(2PI * input), -1, 1)) / PI + 0.5  — "inverse sine" stretch
  //
  // Simpler: use a hard-knee phase ramp with flat sections:
  float halfPi = 1.5707963268f;
  float k = 0.01f + warpAmount * 0.99f;  // k=0 → no effect, k→1 → hard square
  // Map uPhase [0,1) to centered [-0.5, 0.5), ramp, saturate, re-map
  float t = uPhase - 0.5f;  // center
  float shaped = std::tanh(t * (1.0f / (1.0f - k + 0.001f))) * (1.0f - k + 0.001f);
  // Renormalize and wrap
  // Use a blended version to stay within [0,1)
  float warpedPhase = uPhase + warpAmount * (
      (0.5f + (std::atan(k * 10.0f * std::sin(6.28318f * uPhase)) / 3.14159f)) - uPhase
  );
  warpedPhase -= std::floor(warpedPhase);
  ```
  More practical single-expression version:
  ```cpp
  // Cleaner Round formula:
  float warpedPhase = uPhase + warpAmount *
      (std::atan(10.0f * warpAmount * std::sin(6.28318530f * uPhase)) / 3.14159265f);
  warpedPhase -= std::floor(warpedPhase);
  ```
  The atan compressor drives the phase remap toward a sigmoid — fast at the zero crossing,
  flat at the crest. On a sine wavetable, this produces a shape that trends toward square wave.
- **0% / 50% / 100% sonic trajectory:**
  - 0%: Sine unchanged
  - 50%: Rounded plateaus forming; mild square-wave harmonics (odd dominant)
  - 100%: Near-square wave character; hard-edged, bright, hollow midrange

---

### 4. Tri/Pulse

- **Pigments display name:** Tri/Pulse
- **Manual description:** "Takes the middle of the waveform and stretches it to the left"
- **Sonic effect on sine input:** The middle zone of the waveform (the rising zero-crossing region)
  is stretched/dilated leftward — meaning the phase spends more time in the ascending portion and
  very little time in the descending portion, creating an asymmetric triangle/pulse hybrid. The name
  suggests it interpolates between a triangle (symmetric) and a pulse (extremely asymmetric duty-
  cycle). Very different from Pulse Width which manipulates the PWM via the peak position; this
  one stretches the phase slope itself.
- **Proposed C++ phase-warp formula:**
  ```cpp
  // Tri/Pulse: asymmetric phase ramp — slow rise, fast fall
  // Stretching the "middle" left means the phase advance is slow in [0, 0.5+w) and fast in [0.5+w, 1)
  // This creates a sawtooth-like ramp that tilts from triangle toward a very narrow pulse
  float knee = 0.5f - warpAmount * 0.45f;  // knee slides from 0.5 → 0.05 (triangle → near-pulse)
  float warpedPhase;
  if (uPhase < knee) {
      warpedPhase = uPhase * (0.5f / knee);           // slow rise occupies [0, 0.5)
  } else {
      warpedPhase = 0.5f + (uPhase - knee) * (0.5f / (1.0f - knee));  // fast fall [0.5, 1.0)
  }
  // no wrap needed — both branches stay within [0, 1)
  ```
  At `warpAmount = 0`: `knee = 0.5` → symmetric triangle-style ramp (no change).
  At `warpAmount = 1`: `knee = 0.05` → the ascending portion takes only 5% of the cycle;
  the rest is a steep falling slope. On a sine wavetable this produces a very sharp, narrow spike —
  near-impulse. Rich in all harmonics (like a very narrow pulse wave).
- **0% / 50% / 100% sonic trajectory:**
  - 0%: Unmodified
  - 50%: Asymmetric; tilted harmonic content, nasal + buzzy blend
  - 100%: Near-impulse / very narrow pulse; extremely bright, rich harmonics, thin sound

---

### 5. Octave Plus

- **Pigments display name:** Octave Plus
- **Manual description:** "Part of source wave is miniaturized on the right; some harmonics are
  emphasized"
- **Sonic effect on sine input:** The right half of the waveform (second half of phase cycle) is
  compressed into a miniaturized copy of the whole waveform. Result: the full cycle now contains
  1.5 cycles of the underlying wave — the left half is normal scale, the right half is a fast compressed
  copy. This emphasizes harmonics at 1.5x the fundamental (a 12th / octave+fifth partial region),
  hence "Octave Plus." The "emphasized harmonics" comment confirms it's adding frequency content
  above the octave.
- **Proposed C++ phase-warp formula:**
  ```cpp
  // Octave Plus: first half is full-speed (0→0.5 of WT), second half cycles the WT faster
  // The second half is a compressed copy: it traverses 0→1 in the remaining 0.5 of phase
  // With blend from original: lerp between identity and the "1.5x" remap
  float warpedPhaseA;  // identity component
  float warpedPhaseB;  // octave-plus component
  if (uPhase < 0.5f) {
      warpedPhaseA = uPhase;
      warpedPhaseB = uPhase * 2.0f;                        // first half → full cycle speed
  } else {
      warpedPhaseA = uPhase;
      warpedPhaseB = (uPhase - 0.5f) * 2.0f;              // second half → another full cycle
  }
  float warpedPhase = warpedPhaseA + warpAmount * (warpedPhaseB - warpedPhaseA);
  warpedPhase -= std::floor(warpedPhase);
  ```
  At `warpAmount = 0`: unmodified.
  At `warpAmount = 1`: the waveform runs at 2x speed — one full octave up. But at intermediate
  amounts, the blend between identity and 2x creates a complex remap with the "miniaturized on
  the right" character described. True "Octave Plus" might use a 3/2 ratio:
  ```cpp
  // Alternative: first half at 1.0x, second half at 3.0x speed (creates 1+1 = 2 partial emphasis)
  if (uPhase < 0.5f) {
      warpedPhaseB = uPhase;                               // normal
  } else {
      warpedPhaseB = std::fmod(uPhase * 3.0f, 1.0f);      // 3x in second half
  }
  float warpedPhase = uPhase + warpAmount * (warpedPhaseB - uPhase);
  warpedPhase -= std::floor(warpedPhase);
  ```
- **0% / 50% / 100% sonic trajectory:**
  - 0%: Unmodified
  - 50%: Octave partial emerging; sounds like adding a faint upper octave blend
  - 100%: Clear second-cycle "mini" in the right half; the octave harmonic is dominant, bright

---

### 6. Pseudo PW

- **Pigments display name:** Pseudo PW
- **Manual description:** "Stretches the whole waveform to the left and leaves a gap on the right"
- **Sonic effect on sine input:** The entire waveform is time-compressed into the left portion of
  the phase cycle, leaving a "dead zone" (zero or flat) at the right end. This is functionally similar to
  classic pulse-width modulation but operating on the whole waveform shape rather than just a
  rectangle — hence "Pseudo" PW. At maximum, the waveform occupies only a narrow slice of the
  cycle with silence after it, creating a very short pulse. Adds strong odd harmonics as the duty
  cycle shrinks.
- **Proposed C++ phase-warp formula:**
  ```cpp
  // Pseudo PW: compress waveform into [0, 1-w) of phase, phase [1-w, 1) maps to 0 (or held end)
  // "Stretches to the left" = the WT is traversed faster, completing before cycle end
  float activeDuration = 1.0f - warpAmount * 0.90f;  // active portion of phase: 1.0 → 0.1
  float warpedPhase;
  if (uPhase < activeDuration) {
      warpedPhase = uPhase / activeDuration;  // traverse full WT in compressed phase zone
  } else {
      warpedPhase = 1.0f - 1e-6f;             // hold at end of WT (near-0 on a sine = ~0 output)
      // Or: warpedPhase = 0.0f;              // reset to start — creates a second harmonic spike
  }
  // No floor-wrap needed; warpedPhase already in [0,1)
  ```
  The "gap on the right" is the silence zone when `uPhase >= activeDuration`.
  Holding at end-of-cycle (0.9999) means the sine rests near its starting value (0), producing
  a zero-voltage silence gap — true PWM behavior applied to the whole waveform.
- **0% / 50% / 100% sonic trajectory:**
  - 0%: Unmodified
  - 50%: Waveform crammed into 50% of cycle with silence after; harmonics above 2nd dominant
  - 100%: Extreme narrow pulse — very short, bright spike; massive upper harmonic content

---

### 7. Fractalize

- **Pigments display name:** Fractalize
- **Manual description:** "Creates up to 8 copies of the whole waveform, from smaller to larger"
- **Sonic effect on sine input:** The amount knob controls the number of repetitions of the
  waveform within one cycle. At 0%, one normal cycle. As Amount increases, the phase traverses
  the waveform multiple times per cycle — like a simple frequency multiplier but applied in the
  phase domain (so it's a continuous morph, not a step). "From smaller to larger" suggests the
  copies increase in pitch (shorter wavelength) as Amount increases, possibly with amplitude scaling.
  At 8 copies, the output is 8 harmonics stacked = very bright, organ-like or metallic.
- **Proposed C++ phase-warp formula:**
  ```cpp
  // Fractalize: multiply phase by a number that ramps from 1.0 to 8.0
  // The result is the same wavetable traversed N times per cycle → N upper harmonics
  float numCopies = 1.0f + warpAmount * 7.0f;  // 1 to 8 copies
  float warpedPhase = std::fmod(uPhase * numCopies, 1.0f);
  // Note: fmod is equivalent to uPhase * N - floor(uPhase * N), result stays in [0,1)
  ```
  At `warpAmount = 0`: `numCopies = 1.0`, no change.
  At `warpAmount = 0.14`: 2 copies → octave partial prominent.
  At `warpAmount = 1.0`: 8 copies → 8th harmonic dominant (three octaves up).
  "From smaller to larger" might instead mean the copies are scaled in size (amplitude-weighted),
  but since this is a phase operation not an amplitude one, the most natural reading is a phase
  multiplier. The visual of "smaller to larger copies" would be the wavetable viewer showing 8
  shorter-wavelength repetitions in the window.
  
  For non-integer amounts (which is where it sounds best), intermediate fractional copies blend:
  ```cpp
  // Smooth blend between integer multiples:
  float n = 1.0f + warpAmount * 7.0f;
  int nLow = (int)n;
  int nHigh = nLow + 1;
  float frac = n - (float)nLow;
  float phLow  = std::fmod(uPhase * (float)nLow,  1.0f);
  float phHigh = std::fmod(uPhase * (float)nHigh, 1.0f);
  float warpedPhase = phLow + frac * (phHigh - phLow);
  warpedPhase -= std::floor(warpedPhase);
  ```
- **0% / 50% / 100% sonic trajectory:**
  - 0%: Unmodified
  - 50%: ~4 copies per cycle — sounds like a 4x multiplied sine; organ/pipe quality
  - 100%: 8 copies — very bright, almost bell-like on sine, metallic on complex waves

---

## Implementation notes for all 7 shapes

All formulas follow the Terrain phase-warp convention:
```cpp
// In the render loop, before wavetable lookup:
float uPhase = phaseAccumulator;  // [0.0, 1.0)
float warpAmount = normalizedKnobValue;  // [0.0, 1.0]

// Apply chosen warp:
float warpedPhase = applyPhaseWarp(uPhase, warpAmount);
// warpedPhase stays in [0, 1) — either guaranteed by formula or via:
// warpedPhase -= std::floor(warpedPhase);

// Wavetable lookup with warpedPhase instead of uPhase
float sample = wavetable.lookup(mipLevel, framePos, warpedPhase);
```

The existing Terrain warp modes (BEND / SYNC / FORMANT) are also phase operations — these
7 Pigments shapes slot in identically.

**Monotonicity warning:** Pulse Width, Tri/Pulse, and Pseudo PW are piecewise-linear — always
monotonic (no phase reversals), so no aliasing artifacts from the warp itself. Skew, Round,
Octave Plus, and Fractalize CAN introduce abrupt phase jumps at the wrap boundary — these may
need a brief crossfade / de-click envelope at high warpAmount values, same as SYNC does.

---

## Recommendations for Phase 11b (priority order)

### Tier 1 — Highest impact, add first

**1. Pseudo PW** — This is the killer addition. It is sonically unique (true whole-waveform PWM),
musically intuitive (users understand PWM already), and the formula is clean two-branch piecewise
linear. At moderate amounts it thickens any wavetable dramatically. Combined with LFO mod = classic
PWM sweep. Should be the first new shape in Phase 11b.

**2. Fractalize** — Simple `fmod(phase * N, 1.0)` implementation, but the sonic reward is enormous:
a smooth morph from 1× to 8× repetition makes any wavetable sound like an organ, a comb-filtered
brass, or a metallic bell depending on the source. Non-integer N values produce beautiful inharmonic
blends. This is the most unique shape — nothing in BEND/SYNC/FORMANT does this.

**3. Skew** — The manual's own worked example showcases this one, which means it's the most
musicially immediate. The `sin(2πt)` additive phase ramp is trivially implemented and produces the
Casio CZ "resonant" family of harmonics — a smooth sweep from clean to nasal to buzzy. Excellent
as an LFO modulation target.

### Tier 2 — Add in Phase 11c or alongside Tier 1

**4. Tri/Pulse** — The asymmetric knee formula is elegant and the sound (triangle → narrow pulse)
covers territory that FORMANT does not. At extreme settings it becomes an impulse-like shape that
emphasizes all harmonics evenly. High-value for percussive pluck timbres.

**5. Pulse Width** — Classic PWM on any wavetable. Lower priority only because users already know
it from SPREAD/UNISON; but on non-sine wavetables it does entirely different things. Worth adding
alongside Tri/Pulse.

### Tier 3 — Polish phase

**6. Round** — Transforms toward square wave. Overlaps somewhat with what a heavily-driven FORMANT
can produce. Still musically useful (particularly: rounding soft pads toward a harder timbre mid-phrase),
but less distinctive than Tiers 1-2.

**7. Octave Plus** — The 1.5× speed-in-second-half formula needs careful tuning. The math is clear
but the blend curve needs experimentation to avoid a sharp click at the phase midpoint. Good but
requires more polish work than the others.
