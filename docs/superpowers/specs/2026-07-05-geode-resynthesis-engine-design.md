# GEODE — Resynthesis Engine (Engine::SPEC) — Design Spec

**Date:** 2026-07-05 · **Branch:** `feature/terrain-instrument` · **Engine slot:** `Engine::SPEC = 3` (SynthVoice.h:45, currently a silent stub)
**Status:** DESIGN LOCKED (Max signed off 2026-07-05: "GO"). Build bible — review before implementation.
**One-liner:** GEODE is a polyphonic **resynthesis** oscillator. Drop a sample *or* feed a wavetable/analog waveform → GEODE cracks it open into **sinusoidal partials + a noise residual + transients** (SMS model) → play it as a pitched voice with **Position scrub / Freeze**, and pull it apart / sculpt the spectrum.

> Naming law (Max, 2026-07-05): **no reused control names anywhere in Terrain.** GEODE's vocabulary is a fresh geological/erosion set — checked against DRIFT, ERODE, AIR, SCAN, BODY, BREATH, STRETCH, etc. See §12. The engine is called **GEODE** (no "spectral"/"resynth" suffix — the rock metaphor carries it).

---

## 1. Why this exists (positioning)

Serum 2's "Spectral" is a full-spectrogram **phase-vocoder + a magnitude-EQ mask** — powerful but, as Max put it, "basically a couple of filters changing frequencies." Its own manual's limitation list is our feature list:

| Serum 2 spectral CAN'T | GEODE CAN |
|---|---|
| Only manipulates raw FFT bins | True **SMS**: partials **+** noise residual **+** transients, each separable |
| No per-partial control / thinning | **DISTILL** to N loudest partials; per-partial sculpt |
| Freeze = a hidden loop-mode workaround | **FOSSIL** — first-class freeze knob |
| No live formant control | Formant **preserve + shift** (no munchkin repitch) |
| Sculpt = a static magnitude mask | The mask **plus** the erosion arsenal (SIEVE/HAZE/FRACTURE/…) |
| No shift / stretch / blur / smear / morph | All present (morph is phase-2) |
| Source: sample or wavetable (yes, Serum allows wavetable too) | Same, but wavetable is the **cheap** path for us (frames = partials) |

**The differentiator = the SMS split** (Serra/Smith 1989): separating the tonal partials from the noise residual is the entire reason you can pitch/stretch/freeze the body while breath, grit and attacks survive — and the reason you can *pull a sound apart*. Nobody ships this as a playable poly synth voice.

---

## 2. Core concept & the two doors in

GEODE has **one resynthesis+sculpt core** fed by two source types (this is the "hybrid partial core" from the brainstorm — math-seed OR analyzed sample, unified):

- **Door A — dropped sample** → heavy **offline SMS analysis** (STFT → peak-track partials → subtract → noise-residual model → transient marks). This is the "pull it apart" magic.
- **Door B — wavetable / analog** → **direct**, nearly free: our `Wavetable` already stores `WavetableSpec::FrameSpec::Partial{ratio,amp,phase}` (Wavetable.h:33-60, `kMaxPartials=96`, `kNumFrames=16`). The frames **are** the partial frames — no STFT needed. A raw saw/square becomes sculptable additive resynthesis instantly.

Both doors produce the same runtime structure — a **GeodeFrameStore** — that the voice resynthesizes.

**Signal chain (per voice):**
```
source (sample buf | wavetable spec)
  └─ (offline, cached, off-thread) → GeodeFrameStore { frames[]: {partials[], noiseEnv[], transient} , f0, spectralEnvelope[] }
        └─ per voice, per block:
             POSITION/FOSSIL read-head → interpolate frame(s)
               ├─ sine bank  (partials × pitch ratio, formant-aware)   ── under PARTIAL BUDGET
               ├─ noise layer (filtered noise shaped by noiseEnv, SILT gain)
               └─ transient re-inject (on onset, optional)
             → SCULPT (SIEVE/HAZE/FRACTURE/FORMANT/MODE·AMT/TILT) on partials pre-synth
             → spectral FILTER MASK (magnitude EQ, extension card)
             → per-partial limiter (Spectral Clip) + Safe-Bass floor
             → PAN/LEVEL → osc sum (block-render, replaces per-sample loop like SAMP/GRAN)
```

---

## 3. DSP architecture

### 3.1 Reuse map (most of the analyzer already exists — from the infra audit)

Lift these from `BlendEngine.h` — factor them into a shared `tw::spectral` namespace (or a new `GeodeAnalyzer.h` that includes the same helpers) so Blend and GEODE share ONE FFT/STFT implementation:

| Reuse (verbatim) | Anchor | Role in GEODE |
|---|---|---|
| radix-2 FFT + Hann LUT | BlendEngine.h:296-332 | analysis transform |
| `stft()` / `istft()` OLA (2048/512, 75%) | :336-378 | frame spectra + residual resynth |
| `decompose()` HPSS S/T/N Wiener masks (sum to identity) | :381-457 | the sines/transients/noise split |
| `findPeaks()` / `Peak{centroid,mass,lo,hi}` (cap 96) | :476-494 | per-frame partial detections |
| `detectF0()` NSDF autocorrelation (35–1200 Hz, clarity 0.72) | :249-293 | analysis pitch → MIDI transpose ratio |
| `smoothEnv()` + whitening (formant/excitation separation) | :496-618 | formant-preserve + FORMANT knob |
| 7 `SpectralMorph` modes on `Partial{ratio,amp,phase}` | SpectralMorph.h:39-69 | the MODE/AMT sculpt bank (free) |

### 3.2 What's genuinely NEW (the SMS gap — the only real new DSP)

1. **Cross-frame partial TRACKER** — connect `findPeaks()` detections across frames into trajectories: for each frame's peaks, match to previous active tracks within a frequency tolerance (~±1 semitone / ±3%), continue matched tracks, fade-kill unmatched (death), birth unmatched new peaks. Output: per-frame partial arrays with continuity. *(Door B skips this — wavetable frames are already coherent partials.)*
2. **Noise-residual synthesizer** — model the `breath` layer as per-frame band energies (≈24–32 log-spaced bands), resynthesize at runtime as band-limited filtered noise scaled by `noiseEnv` (cheaper + pitch-independent + stereo-decorrelatable) rather than replaying complex bins. Optional stereo decorrelation (2 noise gens) for air width.
3. **Position/Freeze read-head** — a `pos_ ∈ [0,1]` over the frame store, decoupled from pitch; advance by CREEP (rate/dir); FOSSIL = latch/blend the current frame (continuous 0–100%, not binary). Model the advance on GranularEngine `scanPos_` (GranularEngine.h:455-496) + SampleEngine loop modes.
4. **Oscillator-bank resynth** — sum of interpolated sinusoids with a **per-partial phase accumulator**; partial freq = `analysisFreq × noteRatio` (`noteRatio = playedHz / f0`). Formant-preserve = read each partial's amp from the (shifted) spectral envelope instead of moving amps rigidly.

### 3.3 Frame store data model

```
GeodeFrame {
    Partial partials[kMaxGeodePartials];   // reuse kMaxPartials=96 (QUALITY scales ACTIVE count 16..96)
    float   noiseEnv[kNoiseBands];         // ~24-32 log bands (residual magnitude)
    float   transientEnergy;               // onset strength for this frame
}
GeodeFrameStore {
    std::vector<GeodeFrame> frames;        // sample: hop-based, DOWNSAMPLED to kMaxFrames (~256) to bound cost
                                           // wavetable: 16 frames (native)
    float f0;                              // detected fundamental (0 = unvoiced → pitch = keytrack only)
    float spectralEnvelope[...];           // for formant preserve/shift
    bool  fromWavetable;                   // door flag
}
```
Analysis is **mono** for the tonal partial model (sum L+R); stereo width comes from unison/pan + the decorrelated residual — matches how the other engines get width, and halves analysis cost.

### 3.4 Real-time-safe handoff (non-negotiable — this is where the flatline bug lived)

Clone **MorphSlot** (PluginProcessor.h:816-844) as a per-osc **GeodeSlot**: analysis runs on the message-thread `timerCallback`, publishes the `GeodeFrameStore` via an atomic double-buffer with `audioReadingIdx` guard + `ready[2]`/`retireCooldown`, so a voice never resynthesizes from a store mid-rebuild. Change-gate analysis on `(sourceBuffer/wavetable, analysis params)`. **Never analyze on the audio thread** (BlendEngine allocates freely — off-thread only). Parks audio on the previous store during a rebuild.

### 3.5 CPU plan (Max's hard rule; we come in UNDER Serum because we pre-analyze)

- **Analysis is offline + cached + off-thread** → per-voice cost is only resynthesis (sine bank + one noise layer).
- **Shared PARTIAL BUDGET** — mirror the grain budget exactly (GranularEngine.h:95-97,:499-500; PluginProcessor.h:771-772; .cpp:62,:2468): one `int geodePartialsLive_` + `kGeodePartialBudget`, wired to every GEODE engine; on overflow **skip-activating the quietest partials first** (graceful spectral thinning — pads thin at the top of the spectrum, no click, no voice-drop). Reset only in `prepareToPlay`.
- **`QUALITY`** knob = active partial cap (16→96), the Harmor 12–516 analog — direct CPU/fidelity trade.
- **Vertical/harmonic HAZE is the known CPU hog** → bounded/opt-in; time-blur is cheap.
- Block-rate param derivation (no per-sample `pow`/`exp`); per-block change-gates on every sculpt stage (skip when neutral, SpecOps-style).
- **Safety rails:** per-partial limiter (**Razor Spectral Clip** analog) to stop resynthesis blow-ups; **Safe-Bass** minimum-fundamental floor so aggressive SIEVE/DISTILL never kills the note. (Aligns with the healers-everywhere mandate.)

---

## 4. Sculpt layer (what you do to the partials, pre-synth)

Each is a single modulatable depth (amount=0 = bit-identical bypass = mod-matrix safe + CPU-gated). Applied to the partial/noise arrays before the sine bank.

| Control | Does | Built from |
|---|---|---|
| **SILT** | partials ↔ noise-residual balance (crossfade tonal body vs breath) | body vs breath gain (we own the split) |
| **SIEVE** | spectral gate — sift out partials/bins below a threshold (denoise → at extreme, isolate to resonant spikes = instant drone) | amplitude-threshold on partials |
| **DISTILL** | keep only the N loudest partials (trace/purify) — drum loop → pulsing chord | sort partials by amp, thin |
| **HAZE** | blur/smear — **Time** (cheap, reverb/wash) + **Harmonic** (opt-in, de-focus) | Smear mode + time-avg of frames |
| **FRACTURE** | harmonic ↔ inharmonic remap (bell/metal without changing pitch class) | InharmonicStretch mode |
| **FORMANT** | formant shift (± semis) with a **preserve** toggle; couples with pitch play | smoothEnv/whitening |
| **MODE · AMT** | the 7 `SpectralMorph` modes as a selectable sculpt (HarmonicStretch, InharmonicStretch, Vocode, Smear, RandomAmplitudes, DataCompress, SpectralPhaser) | SpectralMorph.h (verbatim) |
| **TILT** | spectral tilt/brightness (single-knob Slope LP/HP, magnitude) | per-partial gain slope |

**Isolate toolbox** (the "pull it apart" selling point — nearly free once split exists): `Harmonic` (keep tonal) · `SILT` (keep residual/breath) · `Transient` (keep attacks) · `DISTILL` (keep N loudest). Exposed as a right-click Isolate ▸ mode and/or the SILT/SIEVE/DISTILL knobs at extremes.

---

## 5. Play controls, Position / Freeze / loop modes

- **POSITION** — 0..1 scrub across frame store (modulatable/automatable). Hero control.
- **FOSSIL** — freeze: continuous 0–100% blend between the live frame and a held frame (evolving infinite pads; residual can stay live so frozen notes still breathe).
- **CREEP** — scan rate + direction of POSITION advance in non-Manual loop modes (Serum SCAN taxonomy: Range ±200/400/800%, Reverse, Key Track, tempo-lock, Sample-Length-to-BPM so loops stay in time while playing chromatically). *(New name — "SCAN" is taken by granular.)*
- **Loop modes** (Max: "always remember loop modes") — One-shot / Fwd / Rev / Fwd-Rev / Tailed / Manual(=POSITION scrub) / Exit-on-Release, + **loop start/end** + **loop crossfade** to kill seams. Mirror Serum's spectral loop taxonomy.
- **Phase Lock / Transients** — the two cheap phase-vocoder quality toggles (tonal fidelity vs attack preservation).

---

## 6. Parameters (per-osc, ×4 A/B/C/D — namespace `SYN_OSC_x_GEODE_*`)

> MUST NOT collide with the existing per-osc spectral FILTER (`SYN_OSC_x_SPECTRAL_TYPE/AMT`, ParameterIDs.hpp:318-319, SynthVoice.h:990-1005) — that's a different subsystem (biquad/comb/ringmod). GEODE gets its own IDs and its own voice members.

**PAGE 1 — Play (front knob row):**
`GEODE_POSITION` (0..1, def .0) · `GEODE_FOSSIL` (0..1, def 0) · `GEODE_CREEP` (bipolar rate, def +100%) · `GEODE_SILT` (0..1 body↔noise, def .15) · `GEODE_CUT` + `GEODE_MIX` (spectral filter front) · shared PAN/LEVEL/pitch (OCT/SEM/FIN, existing).

**PAGE 2 — Sculpt (thin-white-arrow second row, ~9):**
`GEODE_SIEVE` (def 0) · `GEODE_DISTILL` (def 0) · `GEODE_HAZE` (def 0) · `GEODE_FRACTURE` (bipolar, def 0) · `GEODE_FORMANT` (bipolar, def 0) + `GEODE_FORMANT_KEEP` (bool, def on) · `GEODE_MODE` (choice 0=None..7) + `GEODE_AMT` (def 0) · `GEODE_TILT` (bipolar, def 0) · `GEODE_QUALITY` (16..96, def 64).

**Back "+" panel (per Max):** hook up **UNISON** (with Serum-style START/SPAN frame-position spread + voice-0-anchored gain law) + **WARP 1/WARP 2** slots for GEODE, exactly like the other engines.

**Source / loop (menu + extension, not front knobs):** `GEODE_SRCTYPE` (sample|wavetable|analog) · `GEODE_LOOPMODE` (choice) · `GEODE_SRCSTART`/`GEODE_SRCEND` · `GEODE_LOOPXFADE` · `GEODE_PHASELOCK` (bool) · `GEODE_TRANSIENTS` (0..1).

> Every new param needs the **4-point WebSlider bind** (relay + `.withOptionsFrom` + `WebSliderParameterAttachment` + JS read) or it silently no-ops (CLAUDE.md §4). index.html change ⇒ **bust the WebUI BinaryData tree** before rebuild (CLAUDE.md §2B). Prefer defaults of 0/neutral so dblclick-reset never reads as a snap-back.

~23 new params per osc (Play 6 + Sculpt 10 + Source/loop 7; unison/warp reuse existing) × 4 oscs (A/B/C/D) — consistent with how SAMP/GRAN/FM already scale ×4.

---

## 7. UI plan (Max's hard constraint: nothing moves, small + additive)

- **Two knob rows** (Play + Sculpt) live in the **existing engine knob-row footprint** with the thin-white chevron — reuse `.fm-knob-wrap`/`.fm-pg1`/`.fm-pg2`/`.fm-arrow` (index.html markup :5196-5217, CSS :3846-3857, page-flip JS :10162-10169). **Zero new panel; no existing button moves.**
- **Visualizer** — reuse the `.osc-display` canvas box (CSS :3795-3808); render partials/spectrogram in the **white-wireframe + glow** house style via a new C++→JS push modeled on `window.updateOscScope` (`paintWave` :21805, scope pipe :21870-21882). Playhead = POSITION. No new space.
- **Extension card** (Max's exact idea) — the **canonical extension-card emblem** (the one used for LFO/filter) placed as one small button in the **top strip by the A/B/C engine tabs + the `+`**. Opens a body-level draggable overlay — reuse `.mv-ext`/`.mv-menu` (:2382-2408; duplicate styling at body level per the CSS-scope gotcha). Overlay holds: the **drawable spectral-filter MASK editor** (Serum parity: multi-node curve, GRID-8 snap, per-segment curve handles, multi-select, presets, vowel/analog mask sources) + isolate modes + safety-rail toggles (Safe-Bass, Spectral Clip, Phase-Lock, Transients). This is the "3rd set" answer — an overlay, **not** a third knob row.
- **Right-click the GEODE osc** — reuse `.samp-menu`/`.oscq` glass (:4016-4067): Re-analyze · Freeze-here · Isolate ▸ (Harmonic/SILT/Transient) · Quality ▸ · Reverse/Normalize · Loop mode.

**Every response curve/viz must mirror the DSP and MOVE** (project hard rule) — no flat placeholder spectra; the partial view and filter mask animate with the knobs.

---

## 8. Integration points (Engine::SPEC wiring)

- Add per-osc arrays `geodeEngA_..D_`, member `geodeBlkA_..` AudioBuffers + `geodeBlk{L,R}` const-float pointers; a `renderGeodeBlocks(numSamples)` called right after `renderGranularBlocks()` (SynthVoice.h:1723); SPEC-branch pointer reads (`sX_L = geodeBlkXL_[i]`) alongside SAMP/GRAN at :1890-1891 (+ the C/D twins ~:2165/:2437/:2709). The SPEC case is **already** excluded from the per-sample osc loop (uLoop gates :1728-1731) and present in the switch (:1866-1869, :2144-2145) — replace the `sAu=0` stub.
- **Source plumbing:** read the existing per-osc `tw::SampleBuffer` (`getOscSampleBuffer`/`setSampleSources`, PluginProcessor.h:360/871, .cpp:3599) so a dropped sample already reaches GEODE; for Door B, read the osc's `WavetableSpec`. Trigger off-thread analysis on source change (same change-detect as `renderGranularBlocks` :3883).
- Back-panel unison/warp reuse the existing per-osc unison + warp routing.

---

## 9. Scope

**v1 (ship — best-only, the BANG):**
1. Resynthesis core — **sample (SMS) + wavetable/analog** source → polyphonic play
2. **POSITION** + **FOSSIL** + **loop modes** + CREEP scan taxonomy
3. **Isolate toolbox** — Harmonic / SILT / Transient / DISTILL (the differentiator)
4. Page-2 sculpt: **SIEVE · HAZE · FRACTURE · FORMANT(+preserve) · MODE/AMT · TILT · SILT · QUALITY**
5. **Drawable spectral-filter mask** (Serum-parity "basics") in the extension card
6. Pitch + formant-preserve, **unison + warp** (back panel), partial-budget CPU + Spectral-Clip/Safe-Bass rails
7. White-wireframe partial visualizer

**Phase 2+ (deferred, explicitly):** two-source **morph** (reuse Blend optimal-transport, BlendEngine.h:496-586) · cross-osc **vocode/mask** (ties [[project-terrain-osc-warp-routing-vision]]) · **PNG paint** import (and it must cover wavetables too) · quantize-partials-to-key · stochastic-freeze zoo (glitchy/random/resonant/fuzzy) · per-partial compander · WAVER / spectral-time-skew mods.
**Cut entirely (Max):** XY performance pad.

---

## 10. Risks / open

- **Partial tracker robustness** — weak/leaky tracking makes the residual gurgle; needs solid birth/death continuation. Main new-DSP risk.
- **Transient smearing** — the #1 phase-vocoder pitfall; mitigate with transient detect + dry re-inject on onset (Zynaptiq/Serum approach) so drums stay usable.
- **Long-sample memory/latency** — downsample frames to `kMaxFrames (~256)` and keep the analysis window modest/fixed for a playable instrument (avoid SpecOps' huge-FFT latency).
- **Wavetable vs sample unification** — both must land in the same `GeodeFrameStore` so all sculpt/UI is source-agnostic; Door B just populates frames directly (16 frames, `f0` from ratio, no residual unless synthesized).

## 11. Success criteria (v1 done)

- Drop a vocal → plays polyphonically, tuned, keeps breath; Isolate ▸ Harmonic gives a clean pitched core, Isolate ▸ SILT gives pure breath.
- Feed a saw wavetable → SIEVE/FRACTURE/DISTILL reshape it into additive tones with **no** analysis lag.
- FOSSIL holds an infinite, breathing pad; POSITION scrub morphs smoothly (no grain artifacts).
- The filter-mask overlay draws + snaps like Serum's, curve mirrors the DSP, opens from the top-strip emblem, nothing else moved.
- CPU at full poly ≤ Serum spectral (benchmark before/after); thins gracefully past the partial budget, no clicks/voice-drops; pluginval clean, zero NaN.

## 12. Fresh vocabulary (no reuse — see [[feedback-terrain-new-button-names-only]])

**GEODE** (engine) · **POSITION** (scrub) · **FOSSIL** (freeze) · **CREEP** (scan rate) · **SILT** (noise residual) · **SIEVE** (spectral gate) · **DISTILL** (trace to N partials) · **HAZE** (blur/smear) · **FRACTURE** (inharmonic remap) · **FORMANT** (shift/preserve) · **TILT** (spectral tilt) · **QUALITY** (partial budget) · **RUBBLE** (stochastic texture — phase-2 candidate).
Checked clear of: ARP/CHOP/GLITCH/DRIFT (FLOW), STRIKE/AGE/RUST/QUAKE/SCORCH/STORM (FM), Morph/Attack/Body/Breath/Sculpt/Dice (Blend), SCAN/AIR/STRETCH (sample/granular), BEND/FOLD/RECTIFY (warp).
