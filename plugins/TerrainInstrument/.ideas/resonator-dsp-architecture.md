# Annulus Resonator-Morpher — DSP Architecture (research output, 2026-06-30)

> Source: background research workflow `resonator-dsp-research` (6 parallel research streams →
> synthesis → skeptical DSP review). 8 agents, ~500K tokens.
>
> **READ THE REVIEW SECTION AT THE BOTTOM FIRST** — it corrects several items in the spec
> below (Damping inversion between cores, a won't-compile `lastNoteHz_`, the Material relay
> pattern, the "accumulates ≈ Q" reasoning, and stale line-number anchors). Treat the spec as
> the plan and the review as the errata to fold in before writing C++.

---

# Annulus Resonator-Morpher — DSP Architecture & Implementation Spec

**Component:** Global resonator NODE ("Annulus") · **Engine:** Terrain Instrument (JUCE 8, WebView UI) · **Status of UI:** shipped (harmonograph + 4 pie-quadrant controls + right-click Material/Mix), DSP greenfield (zero `SYN_RESO_*` params exist today) · **Reference:** Mutable Instruments Rings/Elements (MIT, `pichenettes/eurorack`) · **Target:** 48 kHz, stereo, ≤64 modes, <1% CPU.

All file paths are absolute: base = `/Users/macshooter/Developer/VST-Plugins/audio-plugin-coder/.worktrees/terrain-instrument/plugins/TerrainInstrument/Source/`.

---

## 1. Architecture & Signal Flow

### 1.1 What it is

A **single global resonator instance** that operates on the finished, summed stereo output. It is **passive** — it never plays itself; it only rings when the synth pushes energy into it. The summed synth output (osc A–D + sample engine + both filters + master FX) is the **exciter**, scaled by **Mix**. Low Mix = mostly dry passthrough; high Mix = fully resonated.

It is **not per-voice**. One bank on the summed signal — this is the architectural win: 64 modes once, not 64 modes × 16 voices, so it stays <1% CPU regardless of polyphony.

### 1.2 Exact insertion point

Mirror the FLOW Chop/Glitch end-of-block insert. Per the codebase map, the FLOW inserts live at `PluginProcessor.cpp:4114-4163`, operating in place on raw write pointers to the finished master buffer. **Insert the resonator immediately after the FLOW block closes, at `PluginProcessor.cpp:4163-4164`**, just before `processBlock`'s closing brace.

```cpp
void TerrainInstrumentAudioProcessor::processBlock (AudioBuffer<float>& buffer, MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;          // already present at top of processBlock
    // ... renderVoices → sum into buffer (3507-3509) ...
    // ... master mix/gain (4052-4058), soft-clip (4069-4074) ...
    // ... FLOW Chop (4118) / Glitch / Drift (…4163) — in-place on buffer ...

    // ── NEW: Annulus resonator node ───────────────────────────────
    {
        const float structure  = apvts.getRawParameterValue (ParameterIDs::SYN_RESO_STRUCTURE)->load();
        const float brightness = apvts.getRawParameterValue (ParameterIDs::SYN_RESO_BRIGHTNESS)->load();
        const float damping    = apvts.getRawParameterValue (ParameterIDs::SYN_RESO_DAMPING)->load();
        const float position   = apvts.getRawParameterValue (ParameterIDs::SYN_RESO_POSITION)->load();
        const float mix        = apvts.getRawParameterValue (ParameterIDs::SYN_RESO_MIX)->load();
        // Material is a CHOICE — raw value is the *index as float* (see §4.4)
        const int   material   = (int) (apvts.getRawParameterValue (ParameterIDs::SYN_RESO_MATERIAL)->load() + 0.5f);
        // KeyTrack / Tune (Phase 3) — most-recent held MIDI note tracked in voice alloc
        const float keyTrack   = apvts.getRawParameterValue (ParameterIDs::SYN_RESO_KEYTRACK)->load();

        float* L = buffer.getWritePointer (0);
        float* R = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : L;
        reso.process (structure, brightness, damping, position, material, mix,
                      keyTrack, lastNoteHz_, getSampleRate(), L, R, numSamples);
    }
}
```

### 1.3 Placement vs. the master soft-clip — IMPORTANT

The master soft-clip (`kMasterCeiling = 0.96605f`) runs at `4069-4074`, **before** the FLOW block and therefore before the resonator. A continuously-driven high-Q bank can momentarily exceed that ceiling. Therefore **the node owns its own output ceiling** internally (`fastTanh` soft-clip on the wet path → `jlimit(-1,1)` → `isfinite` scrub, §3.5). This mirrors FlowChop/FlowGlitch, which own their own dry/wet and seam crossfades rather than relying on the host-level chain.

### 1.4 Block diagram

```
                       SYN_RESO_MIX (smoothed)
                            │
summed master out ─┬─ dry ─┼──────────────────────────────────┐
  (L,R post-FX)    │       │                                   │
                   │   driveFloor + (1-driveFloor)*mix          ▼
                   └─ DC-block → × drive → [ MODAL BANK + KS ] → equal-power xfade → fastTanh → jlimit → isfinite → out
                                            (Structure/Bright/                  (cos·dry + sin·wet)
                                             Damping/Position/
                                             Material → coeffs)
                                                  │
                                                  └─ publishes mode energies + out level → §6 viz
```

---

## 2. The DSP Engine

Two resonator cores, blended: a **modal biquad bank** (the headline) and a **Karplus-Strong / waveguide string** (Phase 2). Material selects mode-ratio tables; Structure/Brightness/Damping/Position/Mix are the macros.

### 2.1 Modal resonator bank (parallel 2-pole resonators)

A struck body's impulse response is a sum of exponentially-decaying sinusoids. Each is exactly the impulse response of a 2-pole resonator. The engine is a **parallel bank** of `N` biquads fed the same input `x(n)`, summed:

```
y(n) = Σ_{k<N_active} y_k(n)
```

**Per-mode biquad (resonator form, zeros at DC & Nyquist):**

```
            1 − z⁻²
H_k(z) = g_k · ─────────────────────
          1 + a1_k·z⁻¹ + a2_k·z⁻²
```

**Difference equation (DF1, shared input delay — `x1,x2` are bank-wide, only `y1_k,y2_k` are per-mode):**

```
y_k(n) = g_k·( x(n) − x(n−2) )  −  a1_k·y_k(n−1)  −  a2_k·y_k(n−2)
```

**Coefficients (verified, JOS / CCRMA):**

```
θ_k  = 2π · f_k / fs
a1_k = −2 · R_k · cos(θ_k)
a2_k = R_k²
```

**Pole radius from T60 (−60 dB decay):**

```
R_k = exp( −6.9077553 / (T60_k · fs) )         // = 10^(−3/(T60·fs))
```

Clamp `R_k = min(R_k, 0.99995f)` (T60 ceiling ≈ 2.9 s at 48 k; raise toward 0.999999 + `double` only for Freeze, §7).

**Gain normalization (the single most important anti-clip line):**

```
g_k = a_k · (1 − R_k²) · 0.5
```

`(1−R²)` cancels the resonance height as Q→1 so Damping never makes the bank clip. `a_k` is the per-mode amplitude weight = `master · a_brightness[k] · a_position[k]` (§2.3).

**Stability invariants (don't ship without these):**
1. `R_k < 1` always (clamp 0.99995).
2. `g_k ∝ (1−R_k²)`.
3. Drop any mode with `f_k ≥ 0.49·fs` (aliased phantom resonance) → `a_k = 0`.
4. `ScopedNoDenormals` + flush (§5).
5. Smooth `f / T60 / amp` in the param domain, not raw coefficients (§5.3).
6. `β` (position) clamped to `[0.02, 0.98]` — never null the fundamental.

### 2.2 Material mode-ratio tables

Each material is a ratio set `{ρ_k}` multiplying the resonator fundamental `f₀`: `f_k = f₀ · ρ_k`. Store each as `std::array<float, kMaxModes>` + a `count`; pad short sets by continuing the last ratio with a fixed multiplier so Brightness can request more modes without overflow.

**Material 0 — String / Harmonic** (default; integer harmonics):
```
ρ = 1,2,3,4,5,6,7,8,9,10,11,12,…   (ρ_k = k)
```
Optional stiffness via Structure: `f_k = k·f₀·√(1 + B·k²)`, `B ≈ 1e−4`.

**Material 1 — Bar / Glockenspiel** (free-free uniform bar, transverse):
```
ρ = 1.000, 2.756, 5.404, 8.933, 13.344, 18.641, …
```

**Material 2 — Drum / Membrane** (ideal circular membrane, Bessel zeros):
```
ρ = 1.000, 1.593, 2.136, 2.296, 2.653, 2.918, 3.156, 3.501, 3.600, 3.652, 4.060, 4.154, …
```

**Material 3 — Metal / Plate** (inharmonic, dense; cymbal/sheet starter):
```
ρ = 1.00, 1.52, 2.04, 2.62, 3.34, 3.88, 4.62, 5.39, 6.18, 7.04, 8.01, 9.21, 10.5, 12.1, 13.9, 16.0
```
Metal wants ≥32 modes and benefits from ±1–2 % random per-mode detune to avoid a "ringing pattern."

> The UI's right-click Material menu is **String / Bar / Drum / Metal** → indices 0/1/2/3, exactly these four tables. (A 5th "Bell" set — `0.5, 1.0, 1.183, 1.506, 2.0, 2.514, 2.662, 3.011, 4.166` — is held for Phase 3 if Max wants it; see §9.)

### 2.3 Macro → DSP mapping (with formulas)

All four macros recompute `(f_k, T60_k, a_k)` → coefficients at control rate; smoothed (§5.3).

**STRUCTURE `s ∈ [0,1]` — harmonic ↔ inharmonic morph.** Geometric (multiplicative) interpolation between the harmonic anchor and the selected material:
```
ρ_k(s) = exp( (1−s)·ln ρ_harmonic[k] + s·ln ρ_material[k] )
f_k    = f₀ · ρ_k(s)
```
`s=0` → pure harmonic; `s=1` → full material character. (Mirrors Rings' `lut_stiffness` stretch but generalized to our material families.) Modes crossing each other as `s` sweeps is fine — the bank is parallel.

**BRIGHTNESS `b ∈ [0,1]` — active-mode count + spectral tilt + input-conditioning LP.** Three coupled effects (Rings folds the same control into exciter LP + mode rolloff):
```
N_active        = round( N_min + b·(N_max − N_min) )           // e.g. 3 → kMaxModes
tilt_dB_per_mode = −24·(1 − b)                                  // flat at b=1, −24 dB/mode at b=0
a_brightness[k]  = 10^( tilt_dB_per_mode·(k−1) / 20 )           // mode 1 = unity
```
Plus the **excitation pre-filter** cutoff (the LP that conditions the input before it hits the bank, §3.2) opens with brightness:
```
fc_exc = 0.4·fs · 2^((cut − 1)·9),   cut = b·(2 − b),   capped at 0.499·fs,   Q ≈ 0.8
```
Always enforce the Nyquist guard here: `if (f_k ≥ 0.49·fs) a_k = 0;`

**DAMPING `d ∈ [0,1]` — global T60 + frequency-dependent decay.** Exponential global scale (perceptually linear) + faster HF decay (the #1 realism cue):
```
T60_base = T60_min · (T60_max / T60_min)^(1 − d)               // d=0 → long (8 s), d=1 → dead (0.02 s)
T60_k    = T60_base · (f_ref / f_k)^α,   f_ref = 1000 Hz,  α ∈ [0,1]
R_k      = exp( −6.9077553 / (T60_k · fs) ),   clamp < 0.99995
```
`α=0` → all modes ring equally (metallic/bell); `α=1` → strong HF damping (woody). Damping can also push `α` for an evolving "material softens" sweep.

**POSITION `β ∈ [0.02, 0.98]` — excitation-point comb (pluck-point nulling).** A mode with a node at the strike point gets zero energy. For inharmonic materials use the ratio, not integer `k`:
```
a_position[k] = | sin( π · ρ_k · β ) |
```
At `β=0.5` even modes null (hollow); near the bridge `β→0` all modes present (bright). Clamp β off 0/1 so the fundamental is never zeroed. (This is exactly Rings' `CosineOscillator` position comb, expressed directly.)

**Combine:**
```
a_k = master_gain · a_brightness[k] · a_position[k]
g_k = a_k · (1 − R_k²) · 0.5
```

### 2.4 Karplus-Strong / waveguide string (Phase 2)

A fractional delay line (Hermite/allpass read) with a damping loop filter and an optional dispersion all-pass for inharmonicity. Fed continuously from the same conditioned input. Extended-KS per Rings `string.cc`:

**Loop / RT60 (Damping → loop feedback gain):**
```
rt60       = 0.07 · 2^(d·(2−d)·8) · fs
loop_gain  = 2^( max(−120·delay / rt60, −127) / 12 )           // FIR loss coefficient < 1
```

**Loop low-pass (Brightness → in-loop cutoff):**
```
damping_f  = f₀ · 2^( min(24 + 48·d² + 24·b², 84) / 12 ),  Q = 0.5    // one-pole/SVF LP in feedback
```

**Dispersion all-pass (Structure → inharmonicity, signed around center):**
```
disp        = (s − 0.5)·2                                       // −1..+1, center = pure KS
ap_gain     = −0.618 · disp / (0.15 + |disp|)
ap_delay    = delay · 0.475 · |disp|·(2 − |disp|)
main_delay  = delay − ap_delay
```

**Per-sample loop:**
```
s = line.ReadHermite(main_delay)
s = allpass(s, ap_delay, ap_gain)        // dispersion (if |disp|>0)
s += x_conditioned                       // inject continuous excitation
s = fir_loss(s) * loop_gain              // damping
s = loop_lp(s)                           // brightness
line.Write(s)
out += line.Tap(pos_a); aux += line.Tap(pos_b)   // two pickups → stereo (Position sets taps)
```

**KS pitch:** delay length `delay = fs / f₀`. f₀ comes from KeyTrack (§7) or a fixed Tune. `setDelay()` is allocation-free; slew the read pointer (fractional) on retune to avoid pitch clicks.

**Modal↔KS blend (Material/Structure crossfade):** run both cores, equal-power blend their outputs over the morph rather than morphing coefficients through unstable intermediate states. MVP ships modal-only; KS arrives in Phase 2 as a parallel core whose blend weight is exposed (right-click "Model", §7).

---

## 3. Excitation Design (continuous audio-in × Mix)

This is the most dangerous path: energy is injected **every sample, forever** (not a single struck burst). A high-Q mode driven continuously at its resonant frequency accumulates amplitude ≈ Q. Three guardrails are mandatory: bounded input drive, loop gain provably < 1, output limiter.

### 3.1 Drive coupled to Mix (with a floor)

Crossfade the output equal-power (§3.4), AND scale input drive with a shallow floored curve so the resonator has body the instant Mix is dialed in and never wastes CPU ringing inaudibly:
```cpp
const float driveFloor = 0.35f;                              // resonator always lightly warm
const float drive = driveFloor + (1.0f - driveFloor) * mixSm; // 0.35 → 1.0
excitation = drive * dcBlocked(summedInput);
```
(Without the floor, wet ∝ mix² and stays anemic until Mix is nearly maxed.)

### 3.2 Input conditioning

Before injection:
1. **DC blocker** — reuse the existing one-pole `DCBlocker` class (already on all four oscillators per the Rectify fix). Continuous drive makes DC chronic; modal biquads and KS loops integrate DC into a headroom-eating ramp.
2. **Brightness LP** — SVF low-pass at `fc_exc` (§2.3), Q ≈ 0.8. Brightness up = feed more synth highs into the bank.
3. **Model pre-scale** — modal multiplies input by `×0.125` at the bank input (Rings' value); KS scales by `1/sqrt(num_strings·2)`. Calibrate the input trim so a sustained ~0 dBFS input at the highest-Q setting lands the wet at roughly −6…−3 dBFS **before** the limiter.

### 3.3 Self-oscillation safety

- **Modal mode cannot self-oscillate from external drive** — it is a pure FIR-driven biquad bank with no output→input feedback. The only runaway is a hot input × high Q, contained by `g_k ∝ (1−R_k²)` + the output ceiling.
- **KS mode can approach self-oscillation** at high damping. Guarantee the closed loop is strictly `< 1` *including the injected term*: `loop_gain = jlimit(0.0f, 0.99995f, computed)`, and the in-loop LP bleeds energy each pass.
- **NaN poisons a feedback loop permanently.** On a detected non-finite output, zero it AND `bank.clearState()` (§3.5).

### 3.4 Equal-power dry/wet (the Mix math)

Dry and wet are correlated at low Mix, decorrelated at high Mix → **equal-power** (constant-energy) crossfade, not equal-gain:
```cpp
const float theta   = mixSm * juce::MathConstants<float>::halfPi;  // 0 … π/2
const float dryGain = std::cos(theta);   // 1 → 0
const float wetGain = std::sin(theta);   // 0 → 1   (dryGain² + wetGain² == 1)
out = dryGain * dry + wetGain * wet;
```
`m=0` → pure passthrough; `m=1` → fully resonated; `m=0.5` → both −3 dB. Compute the two gains **once per block** from the smoothed Mix (Mix is a slow control), interpolate per-sample only for sample-accurate automation sweeps. No per-sample `sin/cos`.

### 3.5 Output safety chain (in order)

```
wet → fastTanh(wet · ceiling) / ceiling   // soft-knee (reuse existing fastTanh; adds Elements-style saturation character)
    → jlimit(-1.0f, 1.0f)                  // brickwall NaN/clip guard
    → if (!std::isfinite(out)) { out = 0; bank.clearState(); }   // stuck-NaN scrub
```
Plus a DC blocker on the wet output (asymmetric position gains can introduce DC).

---

## 4. APVTS Params + WebSliderRelay Wiring

### 4.1 Param IDs (add to `ParameterIDs.hpp`, namespace `ParameterIDs`)

Follow the established `SYN_*` convention (`constexpr char SYN_RESO_X[] = "SYN_RESO_X";`):

| ID | Type | Range | Default | UI control |
|---|---|---|---|---|
| `SYN_RESO_STRUCTURE` | Float | 0..1 | **0.0** | pie-quadrant "Structure" |
| `SYN_RESO_BRIGHTNESS` | Float | 0..1 | **0.35** | pie-quadrant "Brightness" |
| `SYN_RESO_DAMPING` | Float | 0..1 | **0.55** | pie-quadrant "Damping" |
| `SYN_RESO_POSITION` | Float | 0..1 | **0.12** | pie-quadrant "Position" |
| `SYN_RESO_MATERIAL` | Choice | String/Bar/Drum/Metal | **0** (String) | right-click Material menu |
| `SYN_RESO_MIX` | Float | 0..1 | **0.0** (dry/inert) | Mix slider |
| `SYN_RESO_KEYTRACK` | Float | 0..1 | **0.0** (fixed) | Phase 3 (K-emblem) |

Defaults match the JS state object `P = {structure:0.0, brightness:0.35, damping:0.55, position:0.12}` + Mix 0.0. **Mix defaults to 0 so the node is inert until dialed** — correct for a NODE (and consistent with FLOW defaulting Off).

### 4.2 Creation site

In `createParameterLayout()` (`PluginProcessor.cpp:484`), after the FLOW block (~`2216`), add an `addResoKnob` lambda mirroring `addFlowKnob` (`2198-2200`):
```cpp
auto addResoKnob = [&] (const char* id, const char* name, float def) {
    layout.add (std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { id, 1 }, name, juce::NormalisableRange<float>(0.0f, 1.0f), def)); };
addResoKnob (ParameterIDs::SYN_RESO_STRUCTURE,  "Reso Structure",  0.00f);
addResoKnob (ParameterIDs::SYN_RESO_BRIGHTNESS, "Reso Brightness", 0.35f);
addResoKnob (ParameterIDs::SYN_RESO_DAMPING,    "Reso Damping",    0.55f);
addResoKnob (ParameterIDs::SYN_RESO_POSITION,   "Reso Position",   0.12f);
addResoKnob (ParameterIDs::SYN_RESO_MIX,        "Reso Mix",        0.00f);
addResoKnob (ParameterIDs::SYN_RESO_KEYTRACK,   "Reso Key Track",  0.00f);
// Material = choice (mirror FLOW_MODE at 2193-2195)
layout.add (std::make_unique<juce::AudioParameterChoice>(
    juce::ParameterID { ParameterIDs::SYN_RESO_MATERIAL, 1 }, "Reso Material",
    juce::StringArray { "String", "Bar", "Drum", "Metal" }, 0));
```

### 4.3 WebSliderRelay — the 4-point wiring (absolute rule; miss any one → silent no-op)

**(1) Relay members** — `PluginEditor.h`, next to FLOW relays (`442-451`):
```cpp
juce::WebSliderRelay resoStructureRelay  { ParameterIDs::SYN_RESO_STRUCTURE };
juce::WebSliderRelay resoBrightnessRelay { ParameterIDs::SYN_RESO_BRIGHTNESS };
juce::WebSliderRelay resoDampingRelay    { ParameterIDs::SYN_RESO_DAMPING };
juce::WebSliderRelay resoPositionRelay   { ParameterIDs::SYN_RESO_POSITION };
juce::WebSliderRelay resoMixRelay        { ParameterIDs::SYN_RESO_MIX };
juce::WebSliderRelay resoKeyTrackRelay   { ParameterIDs::SYN_RESO_KEYTRACK };
juce::WebComboBoxRelay resoMaterialRelay { ParameterIDs::SYN_RESO_MATERIAL };  // combo for the discrete menu
```
(Use `WebComboBoxRelay`/`WebComboBoxParameterAttachment` for Material — it's the honest fit for a right-click discrete menu and sidesteps the JUCE #1390 `numSteps=INT_MAX` quirk on stepped slider relays. Float knobs stay `WebSliderRelay`.)

**(2) Attachment members (`unique_ptr`)** — `PluginEditor.h`, in the attachment block (~`507`): `resoStructureAttachment … resoMaterialAttachment`. **Declared AFTER `webView`** so they destruct first.

**(3) `.withOptionsFrom(relay)`** — `PluginEditor.cpp`, append after the FLOW chain at `367`:
```cpp
.withOptionsFrom(resoStructureRelay).withOptionsFrom(resoBrightnessRelay).withOptionsFrom(resoDampingRelay)
.withOptionsFrom(resoPositionRelay).withOptionsFrom(resoMixRelay).withOptionsFrom(resoKeyTrackRelay)
.withOptionsFrom(resoMaterialRelay)
```

**(4) Attachment construction** — `PluginEditor.cpp`, after webView is created (comment at `2114`), mirror the FLOW `mkF` block (`2832-2845`):
```cpp
mkF(resoStructureAttachment,  ParameterIDs::SYN_RESO_STRUCTURE,  resoStructureRelay);
mkF(resoBrightnessAttachment, ParameterIDs::SYN_RESO_BRIGHTNESS, resoBrightnessRelay);
mkF(resoDampingAttachment,    ParameterIDs::SYN_RESO_DAMPING,    resoDampingRelay);
mkF(resoPositionAttachment,   ParameterIDs::SYN_RESO_POSITION,   resoPositionRelay);
mkF(resoMixAttachment,        ParameterIDs::SYN_RESO_MIX,        resoMixRelay);
mkF(resoKeyTrackAttachment,   ParameterIDs::SYN_RESO_KEYTRACK,   resoKeyTrackRelay);
// Material via combo attachment:
resoMaterialAttachment = std::make_unique<juce::WebComboBoxParameterAttachment>(
    *audioProcessor.getAPVTS().getParameter(ParameterIDs::SYN_RESO_MATERIAL), resoMaterialRelay, nullptr);
```

### 4.4 The `AudioParameterChoice` read gotcha

`getRawParameterValue("SYN_RESO_MATERIAL")->load()` returns the **selected index as a float** (`0.0, 1.0, 2.0, 3.0`), NOT a normalized 0..1. Read it as `(int)(load() + 0.5f)` (the `+0.5` guards FP drift like `2.999999`); never `raw * numChoices`. Clamp to `[0, 3]`.

### 4.5 JS side — bind the existing reso-cv controls

The harmonograph (index.html `7488-7565`) currently only mutates a plain JS object `P` and never touches slider state — purely visual today. Wire each interaction to also drive the matching state, and read back on init:
- Quadrant drag (`7540`) → `window.Juce.getSliderState("SYN_RESO_STRUCTURE").setNormalisedValue(v)` etc. (per-quadrant).
- Material menu (`7552`) → `getComboBoxState("SYN_RESO_MATERIAL").setChoiceIndex(i)` (or normalized `i/(numChoices-1)` if kept as a slider relay; hard-code `numChoices=4` in JS — do not trust the relay's step count).
- Mix slider (`7555`) → `getSliderState("SYN_RESO_MIX").setNormalisedValue(v)`.
- Init: read back each via `.getNormalisedValue()` and subscribe `valueChangedEvent` to re-render arcs on automation/preset recall.

Use `window.Juce.getSliderState` (NOT `window.__JUCE__`), per the repo convention.

---

## 5. Insertion Point + Real-Time Safety

### 5.1 New files / members

- New header `ResonatorNode.h` (`namespace wc`, mirror `FlowChop.h` structure). `#include` it next to the FLOW includes (`PluginProcessor.h:21-24`).
- DSP member: `wc::ResonatorNode reso;` alongside `wc::FlowChop chop; wc::FlowGlitch glitch;` (`PluginProcessor.h:719-722`).
- Engine signature (mirror FlowChop's): `prepare(double sr, double maxSeconds)`, `reset()`, `process(structure, brightness, damping, position, int material, mix, keyTrack, f0Hz, sr, float* L, float* R, int n)`.

### 5.2 `prepareToPlay` — allocate everything once, never on the audio thread

In `prepareToPlay` next to `chop.prepare(...)` (`PluginProcessor.cpp:2308-2309`):
```cpp
reso.prepare (sampleRate, /*maxSeconds for KS line*/ 4.0);
```
Inside `ResonatorNode::prepare`:
- Allocate mode state **SoA**: `float a1[kMaxModes], a2[kMaxModes], g[kMaxModes], y1[kMaxModes], y2[kMaxModes]` per channel (or one bank if mono-then-spread, §5.5). `kMaxModes = 64`.
- KS delay lines: `setMaximumDelayInSamples(ceil(sr / kLowestFreqHz) + 4)`.
- `SmoothedValue::reset(sr, 0.02)` for Mix; `0.03–0.08` for Structure/Brightness/Damping/Position.
- `reset()`: zero all `y1/y2`, clear KS lines, pre-charge DC-blocker/loop-filter state to steady value (avoids onset clicks — the same lesson as the Acid-303 saturator pre-charge).

`setMaximumDelayInSamples` and `std::vector` sizing happen **only here**. No `new`/`malloc`/lock/resize on the audio thread.

### 5.3 Per-block param flow + smoothing (no zipper)

Read atomics once per block → set `SmoothedValue` **targets**; advance per sample. Recompute `a1/a2/g` from the *smoothed* `f/T60/a` per block (param-domain smoothing keeps poles always valid — never hot-swap raw coefficients, that snaps the resonance audibly). For the discrete **Material switch**, recompute coefficients at the block boundary AND apply a ~5 ms internal equal-power crossfade between old/new wet (per the "no clicks = #1 mandate" history; mirrors FLOW Glitch's `reset()`-on-edge and the loop-mode equal-power crossfades).

### 5.4 Denormals / NaN

- `juce::ScopedNoDenormals` already sits at the top of `processBlock` — inherited.
- Belt-and-suspenders flush in the recursive inner loop: `y = (std::abs(y) < 1e-15f) ? 0.0f : y;` (high-Q tails decaying to silence are the classic denormal CPU-spike generator).
- NaN scrub + state clear per §3.5.

### 5.5 CPU plan / mode count

Cost ≈ 6–10 flops/mode/sample. 64 modes × 2 ch × 48 k ≈ 6.1 M mode-ticks/s ≈ <1 % of one core — and it does **not scale with polyphony** because the node is global. Plan:
- **SoA layout + SIMD across modes** (4 modes/iter via `juce::dsp::SIMDRegister`, or clean auto-vectorizable SoA loop) → 4–8×. Each mode is independent given the same `x[n]`; do NOT SIMD across time (serial recurrence).
- **Skip silent modes:** iterate only `k < N_active` (Brightness) with `a_k > 0`; pack active modes contiguously (branchless inner loop).
- **Mono-process-then-spread (recommended default):** excite one mono bank, spread to stereo via cheap decorrelation (two short all-pass/delay decorrelators or a per-mode L/R phase matrix). Halves mode-ticks, image-stable. True-stereo dual banks only if Max wants independent L/R Position. CPU headroom is ample either way.
- Mode-count guide: 64 cap; bells/plates 32+, strings/bars 8–16, drum 6–12.

---

## 6. Audio-Reactive Visualization Hook

The harmonograph must pulse with **actual** resonance, not free-run. Use the existing SPSC seqlock + 60 Hz editor-push pattern (the exact one behind `window.updateOscScope`).

**Audio thread (`ResonatorNode::process`)** — accumulate per-block energy in 4 modal bands + output level, publish via the odd/even seqlock fence (mirror `PluginProcessor.cpp:3480-3498`):
```cpp
// members on the processor: std::atomic<float> resoEnergy_[4]; std::atomic<float> resoOut_; std::atomic<uint32_t> resoSeq_;
// per block: bucket Σ y_k² into 4 bands (low/low-mid/high-mid/high by mode index), RMS the output
resoSeq_.fetch_add(1, std::memory_order_release);          // odd = writing
for (int b=0;b<4;++b) resoEnergy_[b].store(energy[b], std::memory_order_relaxed);
resoOut_.store(outRms, std::memory_order_relaxed);
resoSeq_.fetch_add(1, std::memory_order_release);          // even = done
```

**Editor thread** — in the timer tick where `window.__terrainEqAnalyzer` / `window.updateOscScope` are pushed (`PluginEditor.cpp:3015-3243`), add a guarded push mirroring `__terrainEqAnalyzer` (`3219-3243`):
```cpp
webView->evaluateJavascript(
  "window.__terrainReso && window.__terrainReso({energy:[" +
  String(e0)+","+String(e1)+","+String(e2)+","+String(e3)+"],out:"+String(outLvl)+"});");
```

**JS side** — near the reso IIFE (`7488-7565`), expose `window.__terrainReso = function(d){ ... }` that drives the existing `bloom` energy term (`7494`, decayed `7513`, drives amplitude `7502`) and the four per-arc `en[]` amplitudes from `d.energy`, plus overall glow from `d.out`. The harmonograph then literally blooms with real modal output and each quadrant pulses with its band's energy — the visualization reflects ACTUAL resonance.

---

## 7. What Makes OURS Unique vs. Rings

Rings is a fixed Eurorack module fed one external input. Annulus is a **node inside a full synth**, which unlocks things Rings structurally cannot do:

1. **Morphing material families fed by the entire synth chain.** The exciter is osc A–D + sample engine + both filters + master FX — a far richer, evolving excitation spectrum than a single Eurorack input. Structure morphs *geometrically* across our own String/Bar/Drum/Metal families (and optional Bell), not just Rings' single stiffness LUT.
2. **Harmonograph = real modal-energy visualization.** The shipped pure-white harmonograph blooms from actual per-band mode energy and output level (§6). Rings has 4 LEDs; we have a living instrument-grade viz that *is* the resonance.
3. **Node-in-FLOW topology.** It sits in the FLOW area as a first-class node alongside Chop/Glitch/Drift/Arp — composable with the rest of the play engine, not a fixed insert.
4. **Mod-matrix-modulatable resonator params.** Structure/Brightness/Damping/Position/Mix are APVTS params → drag any of the 10 LFOs / Drift lanes / envelopes onto them (the existing 32-slot mod matrix). A wandering Structure or LFO'd Position is something a hardware Rings can only do with extra CV modules.
5. **Continuous key-track / sympathetic mode (K-emblem).** Reuse the shipped filter `KEYTRACK` pattern: `resonatorPitch = baseTune + keyTrack·(note − 60)` semitones. 100 % = fully sympathetic (resonance reinforces the played note), 0 % = fixed struck bell/inharmonic object, **intermediate = drifts with the keyboard but not in tune** — a gorgeous in-between Rings can't easily do. Default 0 % (fixed) for the MVP "everything turns into one resonant object" character; expose via the K-emblem affordance.
6. **Freeze / infinite sustain.** A right-click "Freeze" sets `R_k → 1.0` (switch those modes to `double`) and cuts drive → the current modal state rings forever, a frozen resonant snapshot. Trivial given our coefficient model; impossible on stock Rings.

---

## 8. Phased Implementation Plan

Each phase is independently shippable and testable. "No clicks" is the #1 gate at every phase.

### Phase 1 — MVP: Modal bank + audio-in + Mix (click-free)
- New `ResonatorNode.h`: modal bank only, Material = String (harmonic) default, all 4 materials' tables present.
- Insert at `PluginProcessor.cpp:4164`; the 6 APVTS params + full 4-point WebSliderRelay wiring; bind the existing reso-cv UI (quadrants/Material/Mix → slider states).
- Excitation: DC-block → Brightness LP → ×0.125 → bank; equal-power dry/wet (§3.4) with drive floor; output `fastTanh`→`jlimit`→`isfinite` chain.
- Real-time safety: `prepareToPlay` allocation, SmoothedValue on all params, denormal flush, mode-count skip.
- **Test:** `ResonatorNode_test.cpp` — (a) Mix=0 is byte-identical passthrough; (b) impulse in → measured T60 matches `R_k` formula within tolerance per material; (c) no NaN/denormal under 10 s sustained 0 dBFS noise at max Damping/Brightness; (d) Material switch click-free (peak-delta threshold across the boundary); (e) Position β=0.5 nulls even modes (FFT check). Headless-render the harmonograph to confirm it still draws.
- **Ship criterion:** Mix sweep on a held chord sounds like a resonant body, zero clicks, <1 % CPU.

### Phase 2 — KS / waveguide blend
- Add the extended-KS core (§2.4) as a parallel instance; expose a right-click "Model" (Modal / Sympathetic-KS / Inharmonic-KS) and/or a modal↔KS blend, equal-power crossfaded.
- KS pitch from a fixed Tune for now; fractional-delay slew on retune.
- **Test:** KS-only impulse rings at delay-line pitch; blend morph click-free; loop gain provably < 1 at max Damping (no runaway under sustained drive).

### Phase 3 — Unique features
- **Key-track / sympathetic** (`SYN_RESO_KEYTRACK`, K-emblem): track most-recent held note → resonator f₀; tuning glide ~5–15 ms (no pitch click). Optional 2/4-voice sympathetic bank later.
- **Freeze** (right-click): `R_k→1` + drive→0, infinite sustain.
- **Mod-matrix targets:** register Structure/Brightness/Damping/Position/Mix as `ModDest`s so LFOs/Drift/envelopes can drive them (extend the existing matrix).
- **Bell material** (5th table) if Max wants it; optional per-mode random detune toggle for Metal.
- **Stereo:** mono-then-spread default; optional true-stereo dual-bank toggle.
- **Test:** mod-matrix sweep of each param is click-free; key-track 100 % reinforces played pitch (FFT peak at note); Freeze holds level indefinitely with no drift/denormal.

---

## 9. Open Questions for Max

1. **Material set:** ship the 4 shipped-UI materials (String/Bar/Drum/Metal), or add **Bell** as a 5th now? (Table ready; would need the UI menu to grow to 5.)
2. **Default character:** MVP default key-track **0 % (fixed struck object — "everything becomes one resonant body")** vs **100 % (sympathetic, in-tune)**? I propose **0 % fixed** for MVP (more distinctive, no retune logic), with the K-emblem to flip to sympathetic in Phase 3. OK?
3. **Stereo:** **mono-process-then-spread** (cheaper, image-stable — my recommendation) vs true-stereo dual banks (independent L/R Position) as default?
4. **Drive decoupling:** keep Mix-coupled drive (one knob, `driveFloor=0.35`) for MVP, or expose a separate right-click **Drive** for overdriven-resonator-at-low-wet sound design?
5. **KS priority:** is the modal-only MVP enough to ship and gather feedback, or do you want KS in the first drop (Phase 1+2 together)?
6. **Mod-matrix in MVP:** wire Structure/Brightness/Damping/Position/Mix as mod destinations in Phase 1, or hold to Phase 3? (Cheap to add early; makes the node feel alive immediately.)
7. **Freeze affordance:** right-click menu item, or a dedicated emblem on the harmonograph?

---

# ⚠️ DSP REVIEW — corrections to fold in before coding

# DSP Architecture Review — Annulus Resonator-Morpher

Verdict: the modal-bank core is fundamentally sound and the safety instincts are mostly right, but the document contains **one genuine DSP bug, several ungrounded codebase claims, and three load-bearing API/pattern divergences** that will cause silent no-ops or rework if coded as written. Corrections below, most severe first.

## CRITICAL (will cause bugs / silent failures)

**C1. Damping direction is INVERTED between the modal and KS cores (real bug).**
§2.3 modal: `d=0 → 8 s ring, d=1 → dead (0.02 s)`. §2.4 KS (copied verbatim from Rings `string.cc`, where the control is *decay/RT60*, not *damping*): `rt60 = 0.07·2^(d(2−d)·8)·fs` yields `d=0 → 0.07 s (dead), d=1 → 17.9 s (long)` — I simulated it. When the two cores blend on the *same* Damping knob (Phase 2), turning Damping **up** shortens the modal ring but **lengthens** the KS ring, and drives KS `loop_gain → 0.998` (near self-oscillation) precisely where the user expects silence. Fix: feed KS `(1−d)` so both cores agree, and re-verify the loop-gain ceiling at the new `d=0` end.

**C2. `lastNoteHz_` does not exist anywhere in the codebase.** The §1.2 `processBlock` snippet passes `lastNoteHz_` into `reso.process(...)`, and §2.4/§7.5 rely on "most-recent held MIDI note tracked in voice alloc." Grep confirms there is no such member and no global most-recent-note Hz atomic — the synth only tracks notes per-voice inside `renderVoices`. This must be built from scratch (an `std::atomic<float> lastNoteHz_` updated on note-on in the MIDI loop ~line 3369). As written the Phase-1 snippet **will not compile**. Either drop the keyTrack/f0 args from the Phase-1 signature (they're Phase 3 anyway) or land the note-tracking member first.

**C3. WebComboBoxRelay for Material diverges from the established repo pattern and risks a silent no-op.** The doc (§4.3, §4.5) recommends `WebComboBoxRelay` / `WebComboBoxParameterAttachment` / JS `getComboBoxState().setChoiceIndex()`. But the repo's **only** Choice param wired to the WebView (`FLOW_MODE`) uses a plain `WebSliderRelay` + `WebSliderParameterAttachment` via the `mkF` lambda (PluginEditor.cpp:2836), and the index.html has **zero** `getComboBoxState` calls (79 `getSliderState`, 0 combo). The shipped convention for choices from JS is `state.setNormalisedValue(i/(numChoices−1))` (see `wrChoice` at index.html:9904 and line 9618). Introducing a combo relay is untested here and is exactly the kind of 4-point mismatch that produces a silent no-op. Recommendation: wire Material as a `WebSliderRelay` + `mkF` like FLOW_MODE, set from JS with `setNormalisedValue(i/3)` (hard-code 3, do not trust step count — the doc's own §4.5 caveat). The §4.4 read-side `(int)(load()+0.5f)` is correct and matches `flowMode = (int)...->load()` at 3318.

**C4. The "accumulates amplitude ≈ Q" claim (§3 intro) is wrong and undercuts the spec's reasoning.** I simulated a single normalized mode (`g=(1−R²)·0.5`) under continuous resonant drive: peak ≈ **1.0**, not ≈ Q — the `(1−R²)` normalization is specifically what prevents Q-scaling. The real clipping risk is **not** single-mode accumulation; it is **correlated broadband drive summing across N modes**. I confirmed: a 0 dBFS sawtooth whose 16 harmonics each land on a harmonic mode hits **peak ≈ 2.05 with prescale=1.0** but only ≈ 0.26 with the `×0.125` prescale. So the `×0.125` (§3.2) is genuinely load-bearing — keep it — but the doc should state the correct failure mode (N-mode constructive sum under correlated input), not Q-accumulation.

## HIGH (math / safety precision)

**H1. `g = a·(1−R²)·0.5` is an approximate, not exact, peak-normalization, and degrades at high R and near band edges.** Verified: peak gain ≈ 1.000 for R ≤ 0.999 mid-band, but falls to ≈ 0.90 at R=0.9999 and is lower for modes near DC/Nyquist. Consequence: Freeze (R→1, §7.6) and very long T60s will be **quieter** than nominal, and the bank's loudness will shift as Damping sweeps R — a subtle level-zipper independent of the Mix crossfade. Either accept and document it, or normalize by the measured per-mode peak. Don't present it as exact ("the single most important anti-clip line") — it's a good approximation, not a guarantee; the output limiter is the actual guarantee.

**H2. Output safety-chain ordering (§3.5) double-counts the ceiling and is mildly wrong.** `fastTanh(wet·ceiling)/ceiling` with `ceiling=0.96605`: `fastTanh` (verified in TerrainFilters.h:43) hard-clamps its input to ±5 and uses a Padé form accurate only to ~1e-4 over [−5,5]. For a runaway `wet` (e.g. 20), input ≈ 19 → clamps to 1.0 → `/0.96605 = 1.035` → then `jlimit(−1,1)` saves it. It works, but `·ceiling` then `/ceiling` is a no-op scaling that adds nothing; you want `out = fastTanh(wet) ` shaped to taste, or `ceiling·tanh(wet/ceiling)` (the master-clip form at line 4072), not the inverted order. Also note `fastTanh` lives in `namespace tw::filters` in TerrainFilters.h — it is NOT a free global; the new header must `#include "TerrainFilters.h"` and qualify `tw::filters::fastTanh`.

**H3. The soft-clip placement note (§1.3) is correct but under-stated — the resonator runs entirely outside the per-sample render loop.** Confirmed: the master soft-clip (kMasterCeiling, line 4072) executes **inside** the main `for(i…)` sample loop that closes before the FLOW block; FLOW (and the proposed reso) run on already-soft-clipped data after the loop. So the resonator's wet path has **no downstream limiter at all** except its own §3.5 chain — making that internal ceiling mandatory, not optional. The doc says this but frames it as "can momentarily exceed"; it's stronger than that: there is zero host-side safety after the node. Good that the node owns its ceiling; just don't rely on any outer net.

**H4. Position comb can null the fundamental for inharmonic materials despite the β clamp.** §2.3 uses `a_position[k] = |sin(π·ρ_k·β)|` with β∈[0.02,0.98]. The β clamp protects the *harmonic* fundamental (ρ=1) but for inharmonic ρ_k, `sin(π·ρ_k·β)=0` whenever `ρ_k·β` is an integer — e.g. Metal ρ≈16.0 nulls at β≈0.0625, 0.125, … which are inside the allowed range. Modes can be silently zeroed mid-range. Either floor `a_position[k]` to a small epsilon, or accept it as a feature but don't claim "fundamental never zeroed" as a general invariant — it's only guaranteed for ρ=1.

## MEDIUM (estimates / robustness)

**M1. CPU "<1%" is optimistic without SIMD.** Mode-tick math checks out (64×2×48000 = 6.14 M biquad-evals/s; 0.04–0.06 GFLOP/s). But the cost driver is the **serial per-sample recurrence** — a 64-iteration inner loop × 2 ch × 48 kHz with cross-iteration dependency (`y1,y2`). Unvectorized this is realistically a few percent of a core, not <1%. The "<1%" only holds **with** the SIMD-across-modes plan in §5.5. State the figure as conditional on SIMD; the SoA-then-SIMD note is right, but the headline number assumes it's already done.

**M2. Drive/Mix calibration tension.** With `×0.125` prescale + `driveFloor=0.35`, the worst correlated case peaks ≈ 0.26 (−11.7 dBFS) — *more* conservative than the §3.2 target of "−6…−3 dBFS before the limiter." Net effect: at typical (uncorrelated) program material the wet will be **quiet/anemic**, the opposite of the clipping fear. The 0.125 (Rings' value, for a single Eurorack input) may be too low for a full synth sum. Needs an explicit makeup-gain stage between bank and limiter, calibrated by ear — flag as a tuning unknown, not a solved constant.

**M3. Material-switch crossfade needs *both* banks live during the fade (memory/CPU note).** §5.3 calls for a ~5 ms equal-power crossfade on Material change, but the SoA plan (§5.5) describes one bank. A click-free coefficient swap requires either running old+new coefficient sets in parallel for 5 ms (double the state + double cost during the fade) or crossfading the *output* against a held tail. Spec the mechanism; "recompute at block boundary + 5 ms xfade" is hand-wavy about what is being crossfaded against what. Same applies to Structure morphs that re-target every mode's f_k.

**M4. Denormal flush at the right place.** §5.4's `(abs(y)<1e-15)?0:y` per mode per sample adds a branch to the hottest loop (64×2×48k branches/s) and partly defeats the SIMD plan. `ScopedNoDenormals` (confirmed at line 2422, inherited) already sets FTZ/DAZ on the audio thread, so the manual flush is usually redundant. Keep it only if profiling shows denormal stalls; don't put a scalar branch inside the vectorizable inner loop by default.

## LOW / grounding nits

- **L1.** The shipped harmonograph `FAM` table (index.html:7494: `[[1,2,3,4],[1,2.0,2.76,3.4],[1,1.59,2.14,2.65],[1,2.0,2.4,3.0]]`) does **not** match the doc's §2.2 material ratio tables. It's visualization-only today, but §6/§7.2 claim the viz "is" the resonance — the JS material ratios will need to be replaced with the real tables (or driven from C++) for that claim to hold.
- **L2.** Line-number anchors are stale/approximate. Actual `processBlock` starts at **2420** (not implied ~3507); soft-clip at **4069–4074** ✓; FLOW inserts at **4118–4163** (doc said 4114–4163, close); `chop.prepare` at **2308** ✓; `createParameterLayout` at **484** ✓; `mkF` at **2834** ✓; FLOW relays around **443**. The insertion point (after the `flowMode==4` else-if block, before the function's closing brace at ~4163) is correct.
- **L3.** T60 math verified correct: `−6.9077553 = ln(0.001)` exactly; R-clamp 0.99995 → T60 ≈ 2.88 s ✓ (doc "~2.9 s"). Coefficient forms `a1=−2R·cosθ, a2=R²` ✓.
- **L4.** `DCBlocker` exists (TerrainFilters.h:69) but is also `namespace tw::filters` and uses pole 0.995 — fine for reuse; the §3.2 "reuse the existing one-pole DCBlocker" is grounded.

---

## TOP OPEN QUESTIONS (must answer before coding)

1. **Damping coherence (C1):** Confirm the KS core's `d` is inverted to `(1−d)` so both cores ring longer as Damping decreases. Without this, Phase 2 blend is incoherent and approaches self-oscillation.
2. **Note-tracking (C2):** Is `lastNoteHz_` going to be added (atomic, set in the MIDI note-on loop ~3369) before Phase 1, or are the keyTrack/f0 args deferred out of the Phase-1 `process()` signature entirely? The current snippet won't compile.
3. **Material relay type (C3):** Slider-relay-as-choice (matches FLOW_MODE, repo-proven) vs the proposed WebComboBoxRelay (untested here)? Strong recommendation: match FLOW_MODE.
4. **Output gain staging (M2):** Where is the makeup gain between `×0.125` bank input and the limiter, and what's the target wet level for *typical* (uncorrelated) program — by ear, not the Rings constant?
5. **Material/Structure morph mechanism (M3):** Exactly what is crossfaded against what during a Material switch and during a continuous Structure sweep (parallel dual-bank for 5 ms? output-tail hold? per-mode f_k smoothing only)? This is the #1 click-risk surface and the spec is under-specified.
6. **Normalization fidelity (H1):** Accept the approximate `(1−R²)·0.5` (with audible level drift under Damping/Freeze) or measure per-mode peak? Affects Freeze loudness directly.
7. **SIMD commitment (M1):** Is the SoA+SIMD-across-modes implementation in scope for the MVP, or does Phase 1 ship scalar (in which case drop the "<1% CPU" headline to "a few %")?
