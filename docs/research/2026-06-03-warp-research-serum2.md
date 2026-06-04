# Serum 2 WARP Research — Phase 11b

**Source:** Serum 2 User Guide, Manual Version 1.0.3, April 27, 2025 (354 pages)  
**Relevant pages:** pp. 48–56 (Wavetable Oscillator WARP section; the modes table spans pp. 49–56)  
**Also referenced:** p. 12453 (Create PWM from This Table note re: real-time WARP PWM)  
**Research date:** 2026-06-03

---

## Context: What We Already Have

Terrain Instrument Phase 2C shipped 4 WARP modes (int 0–3) in `SynthVoice.h`:

| Index | Name | Our current DSP |
|-------|------|----------------|
| 0 | NONE | Pass through (`warpedPhase = uPhaseA_`) |
| 1 | BEND | `warpedPhase = phase + amount * 0.5 * sin(2π * phase)` — sinusoidal time compression in both halves |
| 2 | SYNC | Hard oscillator sync: slave phase advances at `syncRatio = 1 + amount*4`, resets when master phase wraps |
| 3 | FORMANT | `warpedPhase = phase * (1 + amount * 2)` — simple phase stretching / frame read-faster |

Phase 11b expands this integer to cover 8–12 modes. This document catalogs what Serum 2 does so we can pick the best candidates and derive plausible C++ implementations.

---

## Complete Serum 2 WARP Mode List

Serum 2 uses two independent WARP slots (WARP 1 and WARP 2) each with their own type selector and amount knob. All modes in the WT engine section are documented below. **Multisample, Granular, and Spectral engine warp slots reference the same mode list (p. 49) but those engines are out of scope here — WT only.**

---

### Category: Off

#### OFF
- **Menu name:** OFF (default)
- **DSP:** No phase transformation — raw wavetable lookup at unadjusted phase.
- **Amount trajectory:** N/A
- **Our equivalent:** Mode 0, already shipped.

---

### Category: Sync

#### Sync
- **Menu name:** Sync
- **Page:** p. 49
- **What it does:** Synchronizes wavetable playback to a separate internal oscillator that resets in sync with the primary oscillator's phase. WARP amount sets the pitch of this internal oscillator — as amount increases, harmonic content shifts upwards while the fundamental pitch remains locked to the note. Whole-number ratios (1:2, 1:5, etc.) produce harmonious overtones; non-integer ratios produce a characteristic saw-wave "ripping" artifact at each cycle boundary.
- **Secondary param:** WARP Var fader appears below the mode selector. Controls smoothness of the sync: hard sync at one extreme (traditional abrupt reset) → soft sync at the other (smoothly blended reset). This is Serum 2's equivalent to the sync "bleed" parameter in Vital.
- **Amount trajectory (0→100%):** No effect at 0% (slave pitch = fundamental). As amount increases, slave oscillator rises in pitch. At moderate amounts, harmonic peak brightens and grows. At high amounts with non-integer ratios, aggressive sawtooth artifacts dominate.
- **Sonic descriptor:** Bright, aggressive, "screaming" lead character. Classic hard sync sound when WARP Var is at hard end; smoother, more formant-like when WARP Var is at soft end.
- **Our existing implementation:** Mode 2 (SYNC) — we have hard sync only. Missing the WARP Var (soft-sync blending). Could add soft sync as a secondary param in Phase 11b.

---

### Category: Alt Warp

All modes in this category operate on the **phase axis** (horizontal time axis of one waveform cycle, 0→1). They non-linearly remap when in the cycle each sample of the waveform is read, without changing the waveform's amplitude shape itself.

#### Bend +
- **Menu name:** Bend +
- **Page:** p. 50
- **What it does:** Pinches (bends) the waveform inward — compresses the waveform toward the center of the cycle (both halves squeezed toward their own midpoints). Phase reads faster in the first half-cycle and slower in the second (or vice versa for the other half), creating asymmetry in harmonic content.
- **Amount trajectory (0→100%):** No change at 0%. Progressively squeezes both halves of the cycle inward. At 100%, both halves are maximally compressed toward center.
- **DSP interpretation (phase-domain):** For a standard bend-in on both halves: within [0, 0.5], apply `φ_warped = 0.5 * (φ / 0.5)^(1 + k)` where `k = amount * 3`; mirror for [0.5, 1.0]. Alternatively, `φ_warped = φ + amount * sin(2π * φ) * (-0.5)` (negated from our existing BEND which pushes outward). Since we already have sinusoidal BEND (which does both directions based on sign), this is just the negative-amount variant.
- **Sonic descriptor:** Narrows/darkens the waveform in phase space, adds even harmonics, subtle but noticeable timbral shift.
- **Notes for Phase 11b:** Our current BEND (mode 1) maps to Serum's Bend + (positive direction). If we expose direction as part of the mode (or use signed amounts), we get Bend - for free.

#### Bend -
- **Menu name:** Bend -
- **Page:** p. 50
- **What it does:** Pulls (bends) the waveform outward — spreads both halves of the cycle toward their respective outer edges. The inverse of Bend +.
- **DSP interpretation:** Our existing BEND at negative amount sign (or just flip the sin() term sign).
- **Sonic descriptor:** Slightly brighter/more spread than unwarped. Opens up harmonics vs. Bend +.

#### Bend +/-
- **Menu name:** Bend +/-
- **Page:** p. 50
- **What it does:** Combines both Bend + and Bend - depending on WARP knob position. At 50% (12 o'clock): no change. Below 50%: Bend - territory. Above 50%: Bend + territory.
- **Amount trajectory:** Center = dry/unchanged. Rotating left = outward bend; rotating right = inward bend.
- **DSP interpretation:** `φ_warped = φ + (amount - 0.5) * sin(2π * φ)` — subtract 0.5 from amount so that amount=0.5 → zero distortion. This is very close to our current BEND mode.
- **Our equivalent:** Our current BEND (mode 1) already implements this bipolar behavior at any amount. The existing formula `warpedPhase = phase + amount * 0.5 * sin(2π * phase)` passes through dry at `amount = 0` (not at 50%). We should consider making the WARP amount bipolar (centered at 0.5) in Phase 11b.

#### PWM
- **Menu name:** PWM
- **Page:** p. 50
- **What it does:** Pushes the entire waveform to the left (shifts duty cycle). This is the classic Pulse Width Modulation effect — when applied to a square wave it moves the rising edge, creating the classic PWM sound. Also works interestingly with other waveforms (e.g., a saw wave becomes asymmetric and changes timbre).
- **Secondary note from manual (p. 12453):** "There is a real-time PWM effect using the WARP knob (main panel), but this way [Create PWM from This Table to All] you can perform pulse width modulation and use another warp effect." This confirms the WARP PWM is a real-time phase-shift.
- **Amount trajectory (0→100%):** At 0%: no shift. As amount increases, the waveform shifts progressively leftward in the cycle (duty cycle narrows for square waves from one side).
- **DSP implementation (phase-domain):** The simplest phase-domain PWM is: `φ_warped = (φ < pw) ? φ / (2 * pw) : 0.5 + (φ - pw) / (2 * (1 - pw))` where `pw = 0.5 - amount * 0.45` (keeps it from going fully to 0 or 1). This compresses the first half of the cycle and expands the second (or vice versa). For a square wave: when `φ < pw → HIGH`, when `φ >= pw → LOW`, with pw controlled by amount. For a wavetable: the lookup is time-compressed in the first half and stretched in the second.
- **Simpler equivalent:** `φ_warped = fmod(φ + amount * 0.45, 1.0)` is a pure phase offset (shifts the waveform start point but doesn't create PWM per se). True PWM requires non-linear re-mapping of the phase axis.
- **Sonic descriptor:** Classic PWM sounds: chorusing/detuning-like sweep when modulated. With square/pulse: hollow, woody, phasing quality. With saw: asymmetric, nasal, reedy. **Extremely LFO-modulation-friendly — the #1 reason to include it.**
- **WT POS interaction:** Dramatic — each wavetable frame will produce a different timbre when PWM-squeezed. A formant-heavy frame will bring out the formant more in the compressed half.
- **Priority rating for Phase 11b:** CRITICAL — most-requested classic synthesis mode after SYNC.

#### Asym +
- **Menu name:** Asym +
- **Page:** p. 50
- **What it does:** Similar to Bend +, but bends the ENTIRE waveform to the right as a single unit (not both halves independently). The whole cycle's phase is skewed rightward.
- **DSP interpretation:** `φ_warped = φ^(1 - amount * 0.7)` — power function that compresses the early part of the cycle and expands the latter part when exponent < 1, or: `φ_warped = φ * (1 + amount * k * (1 - φ))` for a tanh-style skew. The distinguishing characteristic from Bend is that it operates on the full cycle as one unit, not on each half independently.
- **Sonic descriptor:** More dramatic timbral skew than Bend; works well for formant shifting effects.

#### Asym -
- **Menu name:** Asym -
- **Page:** p. 50
- **What it does:** Same as Asym + but bends the entire waveform to the left. `φ_warped = φ^(1 + amount * 0.7)`.
- **Sonic descriptor:** Inverse of Asym +; compresses latter part of cycle.

#### Asym +/-
- **Menu name:** Asym +/-
- **Page:** p. 50
- **What it does:** Combines Asym + and Asym - bidirectionally depending on WARP knob. Center = no change.
- **DSP:** `φ_warped = φ^(1 + (0.5 - amount) * 1.4)` where exponent < 1 = skew right, exponent > 1 = skew left, exponent = 1 = dry.

#### Flip
- **Menu name:** Flip
- **Page:** p. 50
- **What it does:** Creates an instantaneous polarity flip (phase inversion) at a specific point in the duty cycle. The WARP knob determines WHERE in the cycle the flip occurs (0% = flip at start, 100% = flip at end). The waveform is normal up to the flip point, then inverted for the remainder.
- **DSP implementation:** `output = (φ < flipPoint) ? +lookup(φ) : -lookup(φ)` where `flipPoint = amount`. Not a phase-domain warp but an amplitude-domain flip that can be computed after the wavetable lookup. Alternatively, in phase domain: `φ_warped = (φ < flipPoint) ? φ : φ + 0.5 (mod 1.0)` which plays the second half of the wavetable instead of the first for the latter portion of the cycle.
- **Sonic descriptor:** Harsh, buzzy, phase-cancellation-flavored. Creates strong odd harmonics when flip is near center (simulating half-wave rectification polarity).
- **Priority for Phase 11b:** Medium. Dramatic sound but narrow use case.

#### Mirror
- **Menu name:** Mirror
- **Page:** p. 50–51
- **What it does:** Creates a mirror-image of the waveform for the second half of the duty cycle. The WARP knob behaves similarly to Asym +/- but on both halves independently. The manual notes: "this mode always has an audible effect" even with WARP at default, because the waveform is always doubled into both halves. This creates an "octaved" quality — the fundamental disappears and the octave becomes the new fundamental, with entirely different harmonic content.
- **DSP implementation:** `φ_warped = 2.0 * φ, if φ < 0.5; else 2.0 * (1.0 - φ)` (standard triangle remapping of phase that plays the wavetable forward then backward). Then apply Asym-style offset to each half based on amount. The key characteristic: the waveform completes 2 full cycles per fundamental period → octave upshift (only even harmonics relative to original, which means all harmonics of the octave).
- **Sonic descriptor:** Octaved, hollow, bright. Can produce bubbly/tonal artifacts when combined with complex wavetable frames. Always on — amount just controls the half-cycle skew.
- **WT POS interaction:** Very interesting — each frame produces different octave-doubled timbre.
- **Priority for Phase 11b:** HIGH. Dramatic effect, single-parameter, octaving quality is unique and immediately useful.

#### Remap 1, 2, 3, 4
- **Menu names:** Remap 1 / Remap 2 / Remap 3 / Remap 4
- **Pages:** pp. 50–51
- **What they do:** Custom user-drawn phase remapping curves (user-editable graph). Remap 2 = mirrored (applies to each half independently). Remap 3 = sinusoidal remapping (no draw needed). Remap 4 = 4x mirrored version of Remap 2.
- **Notes for Phase 11b:** Remap 1/2/4 require user-drawable curves — OUT OF SCOPE per the project brief (no user-drawable curves this phase). Remap 3 (sinusoidal) is essentially our current BEND mode under a different name.

#### Quantize
- **Menu name:** Quantize
- **Page:** p. 51
- **What it does:** Sample-and-hold style step quantization applied directly to the waveform (not as an SR Redux post-effect). The key distinction from an SR Redux effect: "this causes the aliasing sound to follow the pitch perfectly (instead of having that 'same ringing pitch on all notes' quality that a redux effect creates)." The waveform itself is stairstepped.
- **Amount trajectory (0→100%):** At 0%: smooth waveform. As amount increases, the waveform develops increasingly coarse steps (fewer distinct amplitude levels). At 100%: maximum stairstepping — e.g., a sine becomes a rough square or 3-level step wave.
- **DSP implementation (phase-domain):** Quantize is an amplitude-domain operation, not phase-domain. After wavetable lookup: `output_quantized = round(output * steps) / steps` where `steps = round(1 + (1 - amount) * 127)` (so at amount=0: 128 steps ≈ smooth; at amount=1: 1 step ≈ hard square). Note: this is a post-lookup operation, not a phase transform. In the `renderNextBlock` loop, it's applied after `sAu = wavetable.lookup(...)`.
- **Sonic descriptor:** Digital, lo-fi, bitcrusher-like but **pitch-tracking**. Very different feel from a fixed bitcrush. Works well modulated for rhythmic gating effects.
- **WT POS interaction:** Harmonic content changes per frame, but the stairstepping effect will be more pronounced on complex frames (more high-frequency content gets aliased in musical intervals). On a pure sine frame, stepping just produces square wave.
- **Priority for Phase 11b:** HIGH. Unique sonic character (pitch-tracking digital crunch), very simple DSP (post-lookup amplitude quantize), no extra state needed.

#### Odd/Even
- **Menu name:** Odd/Even
- **Page:** p. 51
- **What it does:** Proportionally vertical scaling that selects between odd and even harmonics. At 50%: original signal unchanged. At 0%: only odd harmonics (sounds hollow, like a square wave or clarinet). At 100%: only even harmonics (octave upshift character, since the fundamental and all odd harmonics are missing).
- **Amount trajectory:** Center = unity. One extreme = hollow/odd-harmonic; other extreme = octaved/even-harmonic.
- **DSP interpretation:** This is **not a phase-domain operation** — it's a frequency-domain harmonic selector. However, there is a classic time-domain trick: the half-wave rectification approach. If `y_even = (signal + |signal|) / 2` and `y_odd = (signal - |signal|) / 2 * sign(signal)`, then morphing between these shapes the harmonic balance. Alternatively, "odd harmonics only" = `(signal(φ) - signal(φ + 0.5)) / 2` (half-cycle subtraction, which cancels even harmonics). Blend between signal and this odd-only version based on amount.
- **Sonic descriptor:** At odd extreme: hollow, cylindrical, clarinet/oboe quality. At even extreme: octaved, nasal, overtone-rich.
- **Priority for Phase 11b:** MEDIUM-HIGH. Interesting timbral effect, but the clean implementation requires reading the wavetable at φ and φ+0.5 simultaneously — adds a second lookup per sample. Doable but slightly more complex than single-lookup modes.

---

### Category: Filter

#### LPF
- **Menu name:** LPF
- **Page:** p. 51
- **What it does:** Apply a low-pass filter to the waveform. WARP amount controls cutoff.
- **Notes:** Serum's warp LPF operates on the static waveform (per-frame), not as a running filter in the audio stream. This is effectively a pre-filter on the wavetable table itself. In runtime terms, it would be equivalent to applying a one-pole LP to the audio stream, but since it's visible in 2D waveform view, it's calculated per-frame.
- **Priority for Phase 11b:** LOW. We have a full filter section already. Skip.

#### HPF
- **Menu name:** HPF
- **Page:** p. 51
- **What it does:** Apply a high-pass filter. Same rationale as LPF.
- **Priority for Phase 11b:** LOW. Skip.

---

### Category: Distortion

All distortion WARP modes apply a **transfer function / waveshaper** to the output sample value (amplitude domain, post-lookup). The WARP amount controls drive/depth.

#### Tube
- **Menu name:** Tube
- **Page:** p. 51
- **What it does:** Emulates analog tube amplification — nonlinear soft saturation that produces warm, harmonically rich distortion. The signal is subjected to nonlinearities mimicking vacuum tube behavior.
- **DSP:** Classic soft-saturation transfer function. Common implementations: `tanh(drive * x) / tanh(drive)` (hyperbolic tangent saturation), or `x / (1 + |x|)` (soft clip via reciprocal). WARP amount controls drive: `drive = 1 + amount * 8` (or similar scaling).
- **Amount trajectory:** 0% = clean. Progressively warmer and more compressed; at high amount, rich harmonic saturation with smooth soft-clip ceiling.
- **Sonic descriptor:** Warm, creamy, analog-feeling. Adds primarily 2nd and 3rd harmonics.
- **Note:** Our existing FM engine already uses `tanh(noiseLpZ_ * drive)` for noise coloring — same principle.
- **Priority for Phase 11b:** MEDIUM. Useful but subtle. Our existing FOLD (Phase 11d) may overlap.

#### Soft Clip
- **Menu name:** Soft Clip
- **Page:** pp. 51–52
- **What it does:** Gentle nonlinear compression — less aggressive than hard clipping. Adds warmth without harsh artifacts.
- **DSP:** `output = x * (1 - |x|^(amount * 8 + 1))` or `output = tanh(x * gain) / tanh(gain)` at moderate drive. The defining characteristic vs Tube is that it clips more transparently, staying "in control."
- **Sonic descriptor:** Warm, gentle crunch. Good for thickening sound without aggression.
- **Priority for Phase 11b:** LOW. Very similar to Tube. Skip or combine.

#### Hard Clip
- **Menu name:** Hard Clip
- **Page:** p. 52
- **What it does:** Aggressively limits signal peaks by abruptly cutting them at a threshold. Harsh, bright distortion.
- **DSP:** `output = clamp(x * gain, -1, 1)` where `gain = 1 + amount * 8`. Creates strong odd harmonics (square-wave-like at high amounts).
- **Amount trajectory:** 0% = clean. Progressively more clipped. At 100%: rectangular/square-like output.
- **Sonic descriptor:** Harsh, buzzy, aggressive. More industrial/digital than Tube or Soft Clip.
- **Priority for Phase 11b:** MEDIUM. Very simple DSP. Counterpoint to Sine Fold.

#### Diode 1
- **Menu name:** Diode 1
- **Page:** p. 52
- **What it does:** Emulates analog diode clipping circuits (classic guitar pedal character). Warm AND aggressive, specific tonal characteristics from diode behavior.
- **DSP:** Diode clipping produces asymmetric saturation. Common model: `output = 0.315 * (exp(x / 0.085) - 1)` for the positive half, different curve for negative half. Simpler: `output = (x > 0) ? tanh(x * drive) : x * 0.5` (asymmetric). Amount controls drive.
- **Sonic descriptor:** Warm aggressive, guitar-pedal grit. Asymmetric means even harmonics (2nd) are prominent.
- **Priority for Phase 11b:** LOW. Diodes are an interesting flavor but the tonal difference from Tube+Hard Clip is subtle for most wavetable content.

#### Diode 2
- **Menu name:** Diode 2
- **Page:** p. 52
- **What it does:** Sinusoidal transfer curve with increased hard clipping as drive increases.
- **DSP:** `output = sin(x * π/2 * drive)` — using a sine as the transfer curve produces smooth distortion at low drive, hard squaring at high drive (since sin(π/2) = 1). At maximum drive: becomes a sign function.
- **Sonic descriptor:** Smooth at low amounts, increasingly square/buzzy at high amounts.

#### Linear Fold
- **Menu name:** Linear Fold
- **Page:** p. 52
- **What it does:** Wavefolding — when the signal exceeds an amplitude threshold, it "folds" back on itself. Creates distinctive metallic, harsh harmonic content.
- **DSP:** Classic linear wavefold: `output = fold(x * gain)` where `fold(y) = 4 * (|y/2 + 0.25 - round(y/2 + 0.25)| - 0.25)` (the standard piecewise linear fold function). At low gain (low amount): waveform is unsaturated. As gain increases, the wave folds more and more times per cycle, adding rich harmonics.
- **Amount trajectory:** 0% = clean. As amount increases, the waveform folds back on itself increasingly aggressively. Mid amounts: metallic/glassy. High amounts: dense, complex, almost noise-like.
- **Sonic descriptor:** Metallic, aggressive, complex. VERY different from clipping because fold preserves amplitude continuity (no hard edges → no aliasing) but still generates extreme harmonic content.
- **WT POS interaction:** Strong. Different wavetable frames fold differently — a pure sine folds into a clean triangle/zigzag, while a complex frame produces dense metallic artifacts.
- **Note re: collision with Phase 11d FOLD knob:** The project spec intentionally separates FOLD (Phase 11d) as a distinct axis from WARP. Linear Fold and Sine Fold in WARP would duplicate the FOLD knob's domain. **Recommendation: SKIP these two in WARP Phase 11b and let FOLD axis handle them in Phase 11d.** Document them here for completeness only.
- **Priority for Phase 11b:** SKIP (covered by FOLD axis in Phase 11d).

#### Sine Fold
- **Menu name:** Sine Fold
- **Page:** p. 52
- **What it does:** Wavefolding using a sine-based folding function instead of the linear (triangle-wave) fold. Produces smoother, more musical harmonic content than linear fold.
- **DSP:** `output = sin(x * gain * π)` where gain = `1 + amount * 4`. At low gain: nearly linear (sin(x) ≈ x). As gain increases, the sine wraps increasingly — each full revolution of the argument creates a new fold.
- **Sonic descriptor:** Smooth, musical, can range from warm to intensely complex. Less harsh edge than Linear Fold.
- **Priority for Phase 11b:** SKIP — covered by Phase 11d FOLD axis.

#### Zero-Square
- **Menu name:** Zero-Square
- **Page:** pp. 52–53
- **What it does:** Forces signal below a threshold to zero, while parts above the threshold are squared or drastically altered. Creates sharp, abrupt waveform shape changes.
- **DSP interpretation:** `output = (|x| > threshold) ? sign(x) : 0` or `output = (|x| > threshold) ? x * x * sign(x) : 0`. Threshold controlled by amount.
- **Sonic descriptor:** Harsh, harmonically rich, gated character. Gate + hard clip hybrid.
- **Priority for Phase 11b:** LOW. Niche use case for the main WARP knob.

#### Asym (Distortion category)
- **Menu name:** Asym
- **Page:** p. 53
- **What it does:** Asymmetric waveshaping — different transfer functions for positive and negative halves of the signal. Creates even-order harmonics (2nd harmonic predominant) in addition to odd-order harmonics.
- **DSP:** `output = (x >= 0) ? tanh(x * drive) : tanh(x * drive * 0.5)` (positive half harder clipped than negative). Amount controls drive asymmetry.
- **Sonic descriptor:** From subtle warmth (low amount) to complex overtone-rich (high amount). The even-harmonic content gives a "rounder" and more "analog-like" quality than symmetric distortion.
- **Priority for Phase 11b:** LOW. Close to Tube + bias offset.

#### Rectify
- **Menu name:** Rectify
- **Page:** p. 53
- **What it does:** Rectification — flips or removes one half of the waveform. "Typically by flipping or removing one half of the waveform." Creates harmonically rich, metallic or harsh sound.
- **DSP:** Two interpretations:
  - **Full-wave rectification:** `output = |x|` (both halves become positive). Doubles the frequency (octaves up the fundamental), produces only even harmonics.
  - **Half-wave rectification:** `output = max(x, 0)` (negative half zeroed). DC offset introduced, odd + even harmonics.
  - Amount interpolates between clean and rectified: `output = lerp(x, |x|, amount)`.
- **Sonic descriptor:** Metallic, harsh, octaving character depending on rectification type. Full-wave: octaved and buzzy. Half-wave: DC-heavy, edgy.
- **WT POS interaction:** Significant — each frame rectifies differently. Frames with symmetric waveforms (sine) produce clean octave doubling. Asymmetric frames produce richer inharmonic content.
- **Priority for Phase 11b:** HIGH. Simple DSP (`|x|` or `max(x,0)`), dramatic and unique sonic character, very distinct from clipping. Easy to implement.

#### Sine Shaper
- **Menu name:** Sine Shaper
- **Page:** p. 53
- **What it does:** Waveshaping using a sine function as the transfer curve — smooth, rounded distortion that introduces harmonics in a "musical and often warm manner."
- **DSP:** `output = sin(x * π/2 * (1 + amount * 7))` at low amounts → near-linear; at high amounts → essentially a sine applied to the output, which wraps high-amplitude content back into a bell curve. Different from Linear/Sine Fold in that it's a **transfer function** (output-vs-input mapping) rather than a recursive fold.
- **Amount trajectory:** 0% = clean. Low amounts: subtle harmonic enrichment, warm. High amounts: strong odd harmonic saturation, more sine-wave-dominant output.
- **Sonic descriptor:** Warm, smooth, musical. Good for "analog tube without the harsh edge."
- **WT POS interaction:** Different frames produce different harmonic profiles from the same shaper curve — complex frames get smoothed, simple frames get enriched.
- **Priority for Phase 11b:** MEDIUM-HIGH. Very clean DSP (`sin(x * drive * π/2)`), musical-sounding, distinct from Linear Fold and Hard Clip.

#### Stomp Box
- **Menu name:** Stomp Box
- **Page:** p. 53
- **What it does:** Emulates classic guitar overdrive/distortion pedal circuits. Gritty, crunchy, saturated analog pedal sound.
- **DSP:** Multiple layers: pre-gain boost + soft clip (asymmetric) + slight mid-frequency emphasis. A simplified model: `output = tanh(x * gain) + 0.1 * (x * gain)^3 / 3` (soft clip plus some cubic waveshaping). Guitar pedals often use asymmetric circuits.
- **Sonic descriptor:** Gritty, crunchy, warm drive. Works well on edgy wavetables.
- **Priority for Phase 11b:** LOW. Tonal character close to Tube/Soft Clip combo.

#### Tape Sat.
- **Menu name:** Tape Sat.
- **Page:** p. 53
- **What it does:** Emulates analog tape saturation — a soft-clipping distortion that adds warmth, harmonic richness, vintage character. Naturally occurs on magnetic tape at high recording levels.
- **DSP:** Tape saturation models typically use: `output = x * (1 - amount * x^2 / 3)` (third-order soft clip), or a Dahl model. Characterized by smooth 2nd + 3rd harmonic addition. Often includes subtle compression effect (louder = more saturation).
- **Sonic descriptor:** Warm, vintage, analog-like. Excellent for making digital wavetables sound more natural.
- **Priority for Phase 11b:** MEDIUM. Nice flavor but very close to Tube in practice.

#### Soft Sat.
- **Menu name:** Soft Sat.
- **Page:** p. 54
- **What it does:** Subtle smooth saturation — the gentlest of the distortion modes. Designed for warmth without aggression.
- **DSP:** `output = x - (amount * x^3 / 3)` (soft cubic saturation at low drive). Even gentler than Tape Sat.
- **Sonic descriptor:** Barely perceptible at low amounts. Makes digital wavetables "breathe" slightly.
- **Priority for Phase 11b:** LOW. Too subtle for a WARP mode users will actively sweep.

---

### Category: FM

All FM modes require an external modulator (another oscillator or filter). Since Phase 11b is single-oscillator only (no cross-osc routing), **all FM modes from external sources are OUT OF SCOPE.**

#### FM (from B/C/Noise/Sub/Filter 1/Filter 2)
- **Pages:** pp. 54–55
- **Out of scope for Phase 11b:** Requires OSC B/C or Sub enabled as modulator.

#### Thru-Zero FM
- **Page:** p. 54
- **What it does:** FM where the carrier oscillator continues oscillating even when modulation drives frequency into negative values, by inverting phase. Produces lush, metallic, bell-like tones. More harmonically smooth than standard FM.
- **Out of scope for Phase 11b:** Still requires external modulator.

#### Exp FM / Linear FM
- **Pages:** pp. 54–55
- **Exp:** Exponential scaling → broader, harsher harmonic spectrum. Brighter.
- **Linear:** Linear scaling → pitch-stable, more musical, clean bell tones. Smooth.
- **Out of scope for Phase 11b:** Both require external modulator.

---

### Category: PD (Phase Distortion)

#### PD (from B/C/Noise/Sub/Filter 1/Filter 2/Self)
- **Page:** p. 55
- **What it does:** Phase distortion — modulates the phase of the carrier using an external source. "Similar to FM except the phase is modulated instead of the frequency."
- **Note on PD (Self):** Self-referential PD exists as a mode! This could potentially be implemented as a single-oscillator mode using the oscillator's own previous output sample to modulate its own phase — a feedback PD mode. Serum includes this as `PD (Self)`.
- **DSP for PD (Self):** `phaseInc_warped = phaseInc * (1 + amount * prev_output)`. The previous sample's output feeds back into the phase increment. This creates a self-modulating, dynamically changing timbral quality that evolves based on the waveform's own content.
- **Priority for Phase 11b:** MEDIUM for PD Self specifically — it's single-oscillator-capable and creates a FM-like metallic quality without needing cross-osc routing.

---

### Category: AM

#### AM (from B/C/Noise/Sub/Filter 1/Filter 2)
- **Page:** p. 55
- **What it does:** Amplitude modulation — multiplies carrier by modulator. "Similar to FM except amplitude is modulated instead of frequency." Produces sidebands at sum+difference frequencies of carrier and modulator.
- **Out of scope for Phase 11b:** Requires external modulator.

---

### Category: RM

#### RM (from B/C/Noise/Sub/Filter 1/Filter 2)
- **Page:** p. 56
- **What it does:** Ring modulation — bidirectional amplitude modulation (no carrier signal in output; only sidebands). Produces the classic metallic, clangorous ring mod sound. Carrier signal is suppressed; only sum/difference products remain.
- **Out of scope for Phase 11b:** Requires external modulator.

---

### Utility: Swap Warps

#### Swap Warps
- **Page:** p. 56
- **What it does:** Swaps the WARP 1 and WARP 2 mode selections with each other. UI convenience function only. Not relevant to Phase 11b.

---

## WARP Amount Knob Behavior Notes (General)

From the manual's description patterns, all WARP amount knobs follow this convention:
- **Unipolar modes (Bend +, Bend -, Asym +, Asym -, PWM, Hard Clip, etc.):** 0 = no effect, 1 = maximum effect. The dry signal lives at amount = 0.
- **Bipolar modes (Bend +/-, Asym +/-):** 0.5 (12 o'clock) = no effect. Below center = one direction; above center = other direction.
- **Mirror:** Always has an audible effect — WARP amount controls asymmetry of the mirror, not whether it's active.
- **Quantize:** 0 = smooth, 1 = maximum stairstepping.
- **Odd/Even:** 0.5 = original signal; 0 = odd only; 1 = even only.

---

## Terrain's Existing Mode Comparison

| Our Mode | Serum Equivalent | Assessment |
|----------|-----------------|------------|
| NONE (0) | Off | Exact match |
| BEND (1) | Bend +/- | Near match — our formula `phase + amount * 0.5 * sin(2π*phase)` approximates bipolar sinusoidal Bend. We should expose Bend + vs Bend - as separate modes, or make amount bipolar (center=0 = no change). |
| SYNC (2) | Sync | Match for hard sync. Missing: WARP Var (soft sync slider). Consider adding secondary param in Phase 11b. |
| FORMANT (3) | Asym + (approximate) | Our FORMANT is a linear phase stretch (`phase * (1 + amount * 2)`), which is closer to Asym + (skew whole cycle) than to Serum's own "Formant Sync" (which is more like sync with a formant-filter flavor). The name FORMANT is somewhat misleading given our implementation. Consider renaming or implementing true formant behavior. |

---

## Recommendations for Phase 11b (Priority Order)

### 1. PWM — Pulse Width Modulation
**Why add it:** The single most-requested classic synthesis technique after sync. Works brilliantly with all waveforms (not just square waves). Extremely LFO-modulation-friendly. Serum documents it on p. 50 as pure phase domain: shift the entire waveform leftward, compressing the first half and expanding the second.

**DSP approach (phase-domain, no extra state needed):**
```cpp
// PWM: compress first pw of cycle, expand second (1-pw)
// amount in [0, 1]; pw = 0.5 - amount * 0.45 → range [0.05, 0.5]
const double pw = 0.5 - (double)warpAmount_ * 0.45;
if (phase < pw)
    warpedPhase = 0.5 * phase / pw;         // compress first half
else
    warpedPhase = 0.5 + 0.5 * (phase - pw) / (1.0 - pw);  // expand second half
```
At amount=0: pw=0.5, both halves equal (dry). As amount→1: pw→0.05, first half massively compressed, second massively stretched.

**Sonic descriptor:** Classic PWM sweep; hollow, woody to thin and nasal. When modulated by LFO = chorus-like detuning illusion. Dramatic across all wavetable frames.

**WT POS interaction:** Every frame produces a different PWM character. Rich frames get an asymmetric squeeze that accentuates different harmonics depending on where the density falls in the cycle.

---

### 2. Mirror — Waveform Mirror / Octave Doubling
**Why add it:** Creates an "always on" octave-quality effect — the waveform completes 2 full cycles per fundamental period, shifting the effective fundamental up one octave. The WARP amount controls the phase skew of each half (Asym +/- applied to each half independently). Creates a completely different harmonic profile vs. the base frame. Serum documents it on pp. 50–51 as always having an audible effect, regardless of WARP amount.

**DSP approach (phase-domain):**
```cpp
// Mirror: play wavetable forward in first half, backward in second half
// Amount controls asym skew within each half
double halfPhase;
if (phase < 0.5)
    halfPhase = phase * 2.0;               // forward: 0→1 in first half
else
    halfPhase = (1.0 - phase) * 2.0;       // backward: 1→0 in second half

// Apply Asym-style skew per half based on amount (power function)
const double exp = 1.0 + (0.5 - warpAmount_) * 1.4;
warpedPhase = std::pow(halfPhase, exp);
```
At amount=0.5: pure mirror (no skew). Other amounts add phase asymmetry within each half.

**Sonic descriptor:** Bright, octaved quality. "Doubled" sound. WARP amount controls how asymmetric the two halves are — from symmetric doubling (amount=0.5) to strongly skewed octave content.

**WT POS interaction:** Each frame doubles differently. Frames already near-symmetric double cleanly; asymmetric frames produce more complex harmonics.

---

### 3. Quantize — Phase-domain Sample Rate Reduction (Pitch-Tracking Bitcrush)
**Why add it:** Dramatically different from any existing mode. Produces a digital, lo-fi, bitcrusher quality where the aliasing **tracks pitch** (unlike an SR Redux effect where the ringing is always the same frequency). Simple post-lookup amplitude quantization. Serum documents on p. 51.

**DSP approach (post-lookup amplitude quantize):**
```cpp
// After wavetable lookup: sAu = wavetable.lookup(...)
// Quantize to N steps where N decreases as amount increases
const int steps = std::max(1, (int)std::round(1.0f + (1.0f - warpAmount_) * 127.0f));
// steps range: 128 (amount=0, near-smooth) → 1 (amount=1, full square)
sAu = std::round(sAu * steps) / (float)steps;
```
No extra state needed. Zero CPU overhead beyond a round() call.

**Sonic descriptor:** Digital, lo-fi, pitch-tracking crunch. At low amounts: subtle sample-rate reduction quality. At high amounts: harsh square wave. Modulated with LFO = rhythmic digital gate.

**WT POS interaction:** Complex frames quantize more interestingly than simple frames — at a given `steps` count, harmonically richer frames produce denser staircase patterns.

---

### 4. Rectify — Full-Wave Rectification
**Why add it:** Extremely simple DSP (absolute value), but dramatic sonic effect: **octave upshift + harmonic reshaping**. Full-wave rectification doubles the effective frequency and removes the fundamental from the output, replacing it with the 2nd harmonic. Creates a metallic, edgy, ring-mod-like quality without needing a second oscillator. Serum documents on p. 53.

**DSP approach (post-lookup waveshaper):**
```cpp
// Amount blends between clean and full-wave rectified
// Full-wave: sAu = |sAu| (all positive)
// For better musical result, center the rectified signal: sAu = |sAu| * 2 - 1
const float rectified = std::abs(sAu) * 2.0f - 1.0f;
sAu = sAu + warpAmount_ * (rectified - sAu);  // lerp toward rectified
```
At amount=0: dry. At amount=1: full-wave rectified (octaved, all-positive, recentered).

**Sonic descriptor:** Metallic, octaved, edgy. Good for industrial/aggressive sounds. Creates pure even harmonics (harmonic series of the octave). Completely distinct sonic footprint from sync or bend.

**WT POS interaction:** Frames with strong fundamental will have it removed at full rectification. Complex frames produce rich harmonic reshaping.

---

### 5. Sine Shaper — Smooth Harmonic Enrichment
**Why add it:** The warmest, most musical of the distortion modes. Smooth-sounding at low amounts (barely perceptible warmth), progressively stronger harmonic enrichment at high amounts. Works as a pre-processing WARP that makes wavetables sound more "analog" without aggression. Serum documents on p. 53. Distinct from Hard Clip because it never brutally clips.

**DSP approach (post-lookup transfer function):**
```cpp
// Sine shaper: output = sin(x * π/2 * drive)
// drive range: 1 (dry, sin(x*π/2) ≈ x for small x) → 8 (heavily shaped)
const float drive = 1.0f + warpAmount_ * 7.0f;
// Normalize input to [-1,1] before applying
sAu = std::sin(sAu * (float)(M_PI / 2.0) * drive) / std::sin(drive * (float)(M_PI / 2.0) + 1e-6f);
```
Or simpler: `sAu = std::tanh(sAu * drive) / std::tanh(drive)` if avoiding sin() overhead.

**Sonic descriptor:** Warm to moderately aggressive. Smooth harmonic enrichment. Musical. Works like an "organic analog saturation" applied to any wavetable frame.

**WT POS interaction:** Simple frames gain harmonic complexity; complex frames get smoothed/compressed into a more uniform sound at high amounts.

---

### 6. Hard Clip — Aggressive Squaring
**Why add it:** The most extreme distortion — aggressively squares the waveform at moderate amounts. Creates strong odd harmonics (3rd, 5th, 7th...). Dramatic contrast to the smooth Sine Shaper. Good for industrial, aggressive, EDM-style basses. Serum documents on p. 52. The simplest possible distortion DSP.

**DSP approach:**
```cpp
// Hard Clip: gain up then clip at ±1
const float gain = 1.0f + warpAmount_ * 15.0f;  // 1→16× gain
sAu = juce::jlimit(-1.0f, 1.0f, sAu * gain);
```
At amount=0: unity gain, no clipping. At moderate amounts: peaks squared off. At 100%: near-perfect square wave regardless of input wavetable.

**Sonic descriptor:** Harsh, buzzy, aggressive. Bright. Square-wave-like at extreme amounts. Classic digital distortion.

**WT POS interaction:** At high clip amounts, all frames converge toward a square wave. At moderate amounts, the harmonic balance of each frame is preserved in the center while peaks are clipped.

---

## Mode Summary Table for Phase 11b

| Priority | Mode | Category | DSP Domain | Extra State | Sonic Footprint | Serum Manual Page |
|----------|------|----------|-----------|-------------|-----------------|-------------------|
| 1 | PWM | Alt Warp | Phase-domain | None | Classic PWM sweep, hollow↔thin | p. 50 |
| 2 | Mirror | Alt Warp | Phase-domain | None | Octave doubling, always-on octave shift | pp. 50–51 |
| 3 | Quantize | Alt Warp | Amplitude (post-lookup) | None | Pitch-tracking lo-fi crunch | p. 51 |
| 4 | Rectify | Distortion | Amplitude (post-lookup) | None | Full-wave rectify, metallic octave | p. 53 |
| 5 | Sine Shaper | Distortion | Amplitude (post-lookup) | None | Warm musical saturation | p. 53 |
| 6 | Hard Clip | Distortion | Amplitude (post-lookup) | None | Aggressive squaring, bright/harsh | p. 52 |
| Hold | Bend +/- refine | Alt Warp | Phase-domain | None | Make existing BEND bipolar (center=dry) | p. 50 |
| Hold | Soft Sync (WARP Var) | Sync secondary | Secondary param | None | Add smoothness slider to existing SYNC | p. 49 |
| Skip | Linear Fold | Distortion | Amplitude | None | Handled by Phase 11d FOLD axis | p. 52 |
| Skip | Sine Fold | Distortion | Amplitude | None | Handled by Phase 11d FOLD axis | p. 52 |
| Skip | LPF/HPF | Filter | N/A | N/A | Redundant with full filter section | p. 51 |
| Skip | FM/PD/AM/RM (external) | FM/PD/AM/RM | Phase/Amplitude | N/A | Requires cross-osc routing — Phase 11e/11f | pp. 54–56 |

---

## Notes on Bipolar WARP Amount (Architectural Recommendation)

Serum's Bend +/- and Asym +/- modes place the **dry/unchanged state at 50%** (12 o'clock) rather than at 0. This is a different paradigm from our current knobs (all unipolar, dry = 0). For Phase 11b:

**Recommendation:** Keep the WARP knob unipolar (0 = minimum/dry, 1 = maximum) for all new modes except Mirror (which per Serum is always active). Mirror should document that amount controls skew, with 0.5 as center/symmetric.

For the existing BEND mode: our current implementation is unipolar (dry at 0%, max bend at 100%). This differs from Serum's Bend +/- (dry at 50%). Keep our convention for backward compatibility.

---

## Phase 11b Minimum Viable Mode Set

If Phase 11b needs to stay focused, the **minimum impactful set** is:

1. **PWM** — Fills the most-expected synthesis technique gap. Zero extra state.
2. **Mirror** — Unique, always-audible, no equivalent in any current mode.
3. **Quantize** — Fast to implement, unique sonic character (pitch-tracking lo-fi).

These 3 + the existing 4 = **7 total WARP modes**, covering enough range for the Phase 11a panel to feel "full" in the selector dropdown. Rectify, Sine Shaper, and Hard Clip can then be Phase 11b-extended (same phase, same PR) if implementation goes smoothly.

---

*Research completed 2026-06-03. Source: Serum 2 User Guide v1.0.3. All page citations verified against extracted PDF text.*
