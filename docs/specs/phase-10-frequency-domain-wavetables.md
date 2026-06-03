# Phase 10 — Frequency-Domain Wavetables (Vital-class quality)

> **Status:** Phase 10a SHIPPED on 385a83b. Phase 10b (cubic frame interp) + Phase 10c (remaining 17 wavetable migrations) pending.
> **Drafted:** 2026-06-02 after Vital research deep-dive
> **Reference:** `docs/research/vital-deep-dive-2026-06-02.md`
> **Predecessors:** Phase 2A-C (current time-domain wavetable engine), Phase 9 (OSC B chassis), Phase 8a (voice + flagship features)
> **Target ship date:** TBD — after Phases 5/6/7/8 of the original plan, OR can be promoted earlier if user wants the sonic upgrade first

## TL;DR

Replace Terrain's current time-domain wavetable storage with a **frequency-domain harmonic representation + bandlimited mip-mapped reconstruction**. Eliminates aliasing across the full MIDI keyboard. Same render CPU. Sets up Phase 11 spectral morph modes.

**One sentence:** Store wavetables as harmonics, not samples.

---

## Goal

Make Terrain's wavetable engine produce **clean, aliasing-free output at any MIDI pitch from C0 to C8**, matching the perceptual quality of Vital and Serum 2's wavetable engines.

**Success criteria:**
1. Hold C8 (or any high note) with full-harmonic wavetable (e.g., PPG Wave, Serum HD) — **no audible aliasing**. Should sound like a clean, slightly thinned version of the source, not buzzy/whining.
2. Sweep FRAME slowly while holding a note — frame transitions sound smooth, no harmonic stairstepping or zipper noise.
3. Render CPU per voice stays within ±10% of current (memory bandwidth bound, not arithmetic).
4. All 24 existing wavetable factories produce sonically-equivalent or better output (no regressions in character).
5. Same plugin load time ±2 seconds (mip generation at init is acceptable).

---

## Problem statement (why we're doing this)

### Current architecture (Phase 2A-C)

```
Wavetable {
    float tableData_[16 frames][2048 samples];   // raw time-domain
}

// Render: bilinear (frame, phase) lookup → sample value
```

Each wavetable stores 16 frames × 2048 samples = 32K floats × 4 bytes = **128KB per wavetable**.
24 wavetables × 128KB = ~3MB total wavetable storage. Fine for RAM.

### The aliasing problem

When a wavetable contains harmonics above Nyquist for the current pitch, those harmonics **fold back into the audible range as inharmonic buzz**. This sounds like:
- Cheap-digital-synth grit at high notes
- A "whine" overlaying the fundamental
- Pitch-correlated noise that gets WORSE as you play higher

Our basic-shape factories (Square, Pulse, Triangle) use 32 harmonics. At C5 (~523 Hz), the 32nd harmonic is at 16.7 kHz — below Nyquist (24 kHz at 48 kHz sample rate), so OK. At C7 (~2093 Hz), the 32nd harmonic is at 67 kHz — way above Nyquist, alias hell.

Vital does NOT have this problem because each render dynamically chops off harmonics that would alias.

### What we'd need to do in time-domain to fix this

Three options:
- **Mip-map at startup**: pre-generate multiple bandlimited copies per frame, one per octave. RAM cost: 8× current = 24MB. Doable but inflexible.
- **Per-sample harmonic limiting**: do an inverse-FFT-style sum at every sample. CPU cost: 256× current. Not viable without SIMD vectorization.
- **Per-block harmonic limiting**: do bandlimited reconstruction once per block. Compromise. Still 32× current CPU per block.

The cleanest path is the **mip-map approach driven by frequency-domain source data**. That's what Vital does (research §P1).

### Bonus problems this also solves

1. **Smooth frame morphing** — we currently use bilinear; Vital uses Catmull-Rom cubic. Easy upgrade once we have the frame-spec infrastructure.
2. **Spectral morph modes** (Phase 11+) — vocode, harmonic scale, formant shift, Shepard tone wrapping — only possible with frequency-domain representation.
3. **Cleaner basic-shape definitions** — Square is `amplitudes[h] = (h % 2 == 1) ? 4/(π*h) : 0`. Cleaner than 32 sin() calls per sample.

---

## Proposed architecture

### Two-layer storage: frequency-domain source + time-domain mip levels

```cpp
// SOURCE OF TRUTH (frequency-domain harmonic spec)
struct FrameSpec {
    static constexpr int kMaxHarmonics = 256;
    float amplitudes[kMaxHarmonics];  // amp of each harmonic. 0 = silent
    float phases[kMaxHarmonics];      // phase offset in radians (-π to +π)
    int   numHarmonics;               // count of non-zero harmonics
};

struct WavetableSpec {
    FrameSpec frames[16];
    // Optional metadata: name, category, source URL, etc.
};

// RENDER-READY (time-domain mip levels, generated from spec at construction)
class Wavetable {
public:
    static constexpr int kNumMipLevels = 8;
    static constexpr int kFramesPerTable = 16;
    static constexpr int kSamplesPerFrame = 2048;
    
    // Mip level N contains harmonics up to mipMaxHarmonics[N]
    // Level 0: 256 harmonics (for notes <= C2 / midi 36)
    // Level 1: 128 harmonics (C2-C3)
    // Level 2: 64 harmonics  (C3-C4)
    // Level 3: 32 harmonics  (C4-C5)
    // Level 4: 16 harmonics  (C5-C6)
    // Level 5: 8 harmonics   (C6-C7)
    // Level 6: 4 harmonics   (C7-C8)
    // Level 7: 2 harmonics   (>C8)
    float mipData_[kNumMipLevels][kFramesPerTable][kSamplesPerFrame];
    
    Wavetable() = default;
    
    /** Construct from frequency-domain spec — does the IFFT-style sum once at startup. */
    void buildFromSpec(const WavetableSpec& spec);
    
    /** Render-path lookup: bilinear in (frame, phase) using the appropriate mip level. */
    float lookup(int mipLevel, float framePos, float phase) const noexcept {
        // (bilinear interpolation between two frames at (framePos, phase))
    }
    
    /** Cubic version for Phase 10b polish — Catmull-Rom across 4 frames. */
    float lookupCubic(int mipLevel, float framePos, float phase) const noexcept;
    
    /** Static helper: which mip level to use for a given MIDI note. */
    static int mipLevelForMidiNote(int midiNote) noexcept;
};
```

Storage cost per wavetable:
- Spec: 16 × (256 + 256) × 4 = **32 KB** (frequency-domain, lightweight)
- Mip levels: 8 × 16 × 2048 × 4 = **1 MB** (time-domain, render-ready)

24 wavetables × 1 MB = **24 MB** total. Acceptable.

### Mip level selection

Per-voice, set at `startNote()` based on incoming MIDI note + sample rate. Static lookup table:

```cpp
// mipLevelForMidiNote[midiNote] → mip index
// Computed at startup based on sample rate. Stored as constexpr array if SR fixed,
// or recomputed in prepareToPlay() if SR varies.
constexpr int mipLevelForMidiNote(int midiNote) noexcept {
    if (midiNote <= 36)  return 0;  // C2 and below: full 256 harmonics
    if (midiNote <= 48)  return 1;  // C2-C3
    if (midiNote <= 60)  return 2;  // C3-C4
    if (midiNote <= 72)  return 3;  // C4-C5
    if (midiNote <= 84)  return 4;  // C5-C6
    if (midiNote <= 96)  return 5;  // C6-C7
    if (midiNote <= 108) return 6;  // C7-C8
    return 7;                       // above C8
}
```

The break points correspond to: `harmonics_count × note_freq <= Nyquist`. At 48 kHz sample rate (Nyquist = 24 kHz):
- 256 harmonics, max safe freq = 24000 / 256 = 93.75 Hz → C2 (65 Hz) safe, C3 (130 Hz) not
- 128 harmonics, max safe = 187.5 Hz → C3 (130 Hz) safe, C4 (260 Hz) not
- 64 harmonics, max safe = 375 Hz → C4 (260 Hz) safe, C5 (520 Hz) not
- ... etc

(Adjust for 44.1 kHz: shift one note down per mip threshold.)

### Render path (per sample)

Same shape as current bilinear lookup, just with mip level selection:

```cpp
// In SynthVoice::renderNextBlock, OSC A WT engine path:
const int mipIdx = mipLevelA_;  // cached at startNote based on currentMidiNote_
sA = currentWavetable_->lookup(mipIdx, framePos_, (float)warpedPhase);
```

**Render CPU per sample: unchanged.** Same one bilinear lookup. The fix happens at noteOn (pick the right mip level) and at startup (generate the mip levels).

---

## DSP details

### Reconstruction (IFFT sum, runs once per wavetable at construction)

For each mip level, for each frame, for each sample position 0..2047, sum the harmonics that survive the mip level's bandlimit:

```cpp
void Wavetable::buildFromSpec(const WavetableSpec& spec) {
    constexpr int mipMaxHarmonics[kNumMipLevels] = { 256, 128, 64, 32, 16, 8, 4, 2 };
    constexpr double pi2 = 2.0 * 3.14159265358979323846;
    
    for (int level = 0; level < kNumMipLevels; ++level) {
        const int hMax = std::min(mipMaxHarmonics[level], FrameSpec::kMaxHarmonics);
        
        for (int frame = 0; frame < kFramesPerTable; ++frame) {
            const FrameSpec& fs = spec.frames[frame];
            const int hCount = std::min(hMax, fs.numHarmonics);
            
            for (int sample = 0; sample < kSamplesPerFrame; ++sample) {
                const double normPhase = static_cast<double>(sample) / static_cast<double>(kSamplesPerFrame);
                double v = 0.0;
                
                for (int h = 1; h <= hCount; ++h) {
                    const double amp = fs.amplitudes[h - 1];
                    if (amp == 0.0) continue;  // skip silent harmonics
                    const double ph = fs.phases[h - 1];
                    v += amp * std::sin(pi2 * h * normPhase + ph);
                }
                
                mipData_[level][frame][sample] = static_cast<float>(v);
            }
        }
    }
    
    // Optional: normalize each mip level so peak == 1.0 (prevents level imbalance)
    normalizeMipLevels();
}
```

**Cost estimate:**
- Per mip level: 16 frames × 2048 samples × hMax harmonics × 1 sin() call
- Level 0 (256 harmonics): 16 × 2048 × 256 = 8.4M ops
- All 8 levels: ~17M ops per wavetable
- All 24 wavetables: ~400M ops total
- At ~10ns per sin() on modern Apple Silicon: **~4 seconds**

This is acceptable for plugin load time. If we want it faster, we can:
- Use FFT (juce::dsp::FFT) for IFFT instead of direct sum: ~10× speedup
- Cache to disk on first run, load from disk on subsequent runs
- Run in a background thread; the synth uses a fallback mode during init

For Phase 10 v1: direct sum at startup, accept ~4 seconds. Cache-to-disk + threaded init is a Phase 10 polish task.

### Frame morphing — bilinear → Catmull-Rom cubic (Phase 10b)

Current bilinear reads 2 frames and interpolates. Cubic reads 4 frames (the surrounding 2 + 1 on each side) for smoother curves:

```cpp
float Wavetable::lookupCubic(int mipLevel, float framePos, float phase) const noexcept {
    const float frameF = framePos * (kFramesPerTable - 1);
    const int   f1 = juce::jlimit(0, kFramesPerTable - 1, (int)frameF);
    const int   f0 = juce::jlimit(0, kFramesPerTable - 1, f1 - 1);
    const int   f2 = juce::jlimit(0, kFramesPerTable - 1, f1 + 1);
    const int   f3 = juce::jlimit(0, kFramesPerTable - 1, f1 + 2);
    const float t  = frameF - (float)f1;
    
    const float s0 = sampleFromFrame(mipLevel, f0, phase);
    const float s1 = sampleFromFrame(mipLevel, f1, phase);
    const float s2 = sampleFromFrame(mipLevel, f2, phase);
    const float s3 = sampleFromFrame(mipLevel, f3, phase);
    
    return catmullRom(s0, s1, s2, s3, t);
}

static inline float catmullRom(float p0, float p1, float p2, float p3, float t) noexcept {
    const float t2 = t * t;
    const float t3 = t2 * t;
    return 0.5f * (
        (2.0f * p1) +
        (-p0 + p2) * t +
        (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * t2 +
        (-p0 + 3.0f * p1 - 3.0f * p2 + p3) * t3
    );
}
```

CPU cost: 2× per-sample (4 frame reads + cubic eval vs 2 reads + linear). Acceptable.

Ship as Phase 10b optional polish — Phase 10a (bilinear) is the architectural win; cubic is the cherry on top.

---

## Compatibility with existing code

### What changes

1. **Wavetable class** — gains mip levels, mip selection helper, `buildFromSpec()` method. Old `tableData_[16][2048]` member can stay as a legacy fallback (mip level 0 alias) during transition.

2. **Wavetable factories** — every `makeXxx()` static method returns a `WavetableSpec` instead of a `Wavetable`. Bank constructor reconstructs from spec.

3. **WavetableBank** — adds a build step that walks specs → wavetables at construction:
   ```cpp
   WavetableBank() {
       const std::array<WavetableSpec(*)(), kNumPresets> factories = {
           &Wavetable::makeSineSpec, &Wavetable::makeTriangleSpec, ...
       };
       for (int i = 0; i < kNumPresets; ++i) {
           tables_[i].buildFromSpec(factories[i]());
       }
   }
   ```

4. **SynthVoice** — adds a `currentMipLevel_` cached at startNote based on midi note. Existing `setWavetable()` setter takes a `const Wavetable*` (unchanged), but render path passes `currentMipLevel_` to `lookup()`.

5. **WavetableBank.getTable()** — unchanged signature.

6. **PluginProcessor broadcast** — unchanged.

### What stays the same

- APVTS params: SYN_OSC_A_WT_PRESET, SYN_OSC_A_WT_FRAME, SYN_OSC_A_WARP_MODE, SYN_OSC_A_WARP_AMOUNT — all unchanged.
- UI: preset dropdown, FRAME knob, WARP mode select, WARP AMT knob — all unchanged.
- Warp modes (BEND, SYNC, FORMANT) — still applied to the phase before lookup. No change.
- Voice management, UNISON, EROSION, HORIZON — all unaffected.
- DAW automation, preset save/load — no changes (param IDs and ranges identical).

### Migration of existing factories

Each existing factory generates time-domain frames directly. The new pattern generates frequency-domain specs.

**Before (current `makeSquare`):**
```cpp
static Wavetable makeSquare() {
    Wavetable wt;
    for (int frame = 0; frame < 16; ++frame)
        for (int sample = 0; sample < 2048; ++sample) {
            double phase = (double)sample / 2048.0;
            double v = 0.0;
            for (int n = 1; n <= 32; n += 2)
                v += std::sin(2π * n * phase) / n;
            v *= 4.0 / π;
            wt.tableData_[frame][sample] = (float)v;
        }
    return wt;
}
```

**After (new `makeSquareSpec`):**
```cpp
static WavetableSpec makeSquareSpec() {
    WavetableSpec spec;
    for (int frame = 0; frame < 16; ++frame) {
        FrameSpec& fs = spec.frames[frame];
        fs.numHarmonics = 0;
        for (int n = 1; n <= 256; n += 2) {  // odd harmonics only, up to 256
            fs.amplitudes[n - 1] = (4.0f / 3.14159265f) / static_cast<float>(n);
            fs.phases[n - 1] = 0.0f;
            fs.numHarmonics = n;
        }
    }
    return spec;
}
```

**Cleaner code AND more harmonics (256 vs 32) AND no aliasing at any pitch.** Triple win.

Same migration pattern for Triangle, Pulse, Sine.

The 20 character wavetables (Prophet Saw, PPG Wave, etc.) are more complex but the same pattern: instead of generating time-domain samples directly, define the harmonic content per frame.

### Migration of factories — detailed plan per wavetable

Each existing factory needs migration. They're roughly categorized:

| Category | Wavetables | Migration difficulty |
|---|---|---|
| Pure shapes | Sine, Triangle, Square, Pulse | Easy — already harmonic-defined math |
| Analog saw-based | Prophet Saw, OB-X Saw, Juno Str | Easy — saw = 1/h, modify amp envelope per frame |
| PWM/pulse-based | Jupiter PWM | Medium — PWM harmonics math is well-known (sin(πhd)/h where d=duty) |
| Bandlimited classics | Moog Sqr, CS-80 Brass | Medium — additive recipes already in code |
| Digital | PPG Wave, DX7 EP, D-50 Bell, M1 Piano | Medium — designed-by-frequency anyway |
| Vocal | Choir A→O, Whisper, Vowel Morph | Medium — formant-based, define formant peaks as amp clusters |
| Metallic | Bowed Metal, Glass Harmonics, Railroad | Hard — inharmonic spectra, need design care |
| Experimental | Dustbowl, Static Evolve, Spectral Drift, Serum HD | Hard — currently use noise-modulated time-domain, need spectral redesign |

For Phase 10a, ship the 4 basic shapes + 6 analog saws + 1 PWM as spec-based. The remaining 13 stay as time-domain via a fallback codepath (Mip Level 0 only, current aliasing behavior). Phase 10c migrates the rest over a few sessions.

---

## Implementation roadmap

### Phase 10a — Foundation (the big one)

Sub-tasks:
1. **T1: Add `FrameSpec` + `WavetableSpec` types** to Wavetable.h.
2. **T2: Add `Wavetable::buildFromSpec()` + mip storage** + bilinear `lookup(mipLevel, frame, phase)` method.
3. **T3: Add `mipLevelForMidiNote()` static helper.**
4. **T4: Add `currentMipLevel_` to SynthVoice**, set in startNote, pass to lookup in renderNextBlock for both OSC A and OSC B.
5. **T5: Convert basic-shape factories** (Sine, Triangle, Square, Pulse) from time-domain `makeX()` to spec `makeXSpec()`.
6. **T6: Convert analog-saw factories** (Prophet Saw, OB-X Saw, Juno Str) to spec format.
7. **T7: WavetableBank reconstructs from specs at construction.**
8. **T8: Legacy fallback** — time-domain factories that aren't migrated yet get treated as "mip level 0 only" (their existing behavior at all pitches, accepting aliasing for that subset until migrated).
9. **T9: Build + verify in DAW** — hold high notes with Square/Sine/Saw, should sound clean. Compare against current build A/B.
10. **T10: Tag + commit + spec update.**

Estimated effort: ~6-10 hours.

### Phase 10b — Cubic frame interpolation

11. **T11: Add `lookupCubic()` method.** Wire into SynthVoice as default.
12. **T12: Verify smoother FRAME sweeps in DAW.**

Estimated effort: ~1 hour.

### Phase 10c — Migrate remaining 13 factories

13-25. **One commit per wavetable migration.** Verify A/B against original sound. Some (vocal, metallic, experimental) may need creative reinterpretation in the harmonic domain.

Estimated effort: ~4-8 hours total.

### Phase 10d — Performance polish (only if needed)

26. **FFT-based IFFT for faster init** (juce::dsp::FFT).
27. **Cache mip data to disk** for fast subsequent loads.
28. **SIMD vectorization of the harmonic sum** (manual NEON/SSE intrinsics or rely on auto-vectorization).

Only ship Phase 10d if Phase 10a init time is unacceptable (>5 seconds).

---

## Testing strategy

### Manual DAW tests (every implementer task)

1. Sweep test: play a chromatic scale C2 → C8 with Square wavetable. **Pre-Phase 10**: notes above C6 sound buzzy/whining. **Post-Phase 10**: all notes sound clean, tone gets thinner/purer but never buzzy.

2. Sustain test: hold high note (C7) for 5 seconds with each wavetable preset. No audible alias hash.

3. Frame sweep test: hold mid note (C4), slowly sweep FRAME 0→1. Should sound smooth, no zipper noise or harmonic stairstepping. With cubic (Phase 10b), should sound even smoother.

4. Warp test: hold note, enable BEND/SYNC/FORMANT each with WARP AMT > 0. Warp still works correctly (applied to phase before lookup — phase math is mip-independent).

5. UNISON + EROSION test: UNISON=8, SPREAD=70, EROSION=40, hold C5 chord. Should be lush + drifty + clean.

### Unit tests (Wavetable_test.cpp additions)

- Each new spec factory produces non-zero output across all 16 frames.
- `buildFromSpec()` populates all 8 mip levels with finite values.
- `mipLevelForMidiNote()` returns 0 for low notes, 7 for high notes, monotonically.
- `lookup()` returns values in [-1, +1] at all (mip, frame, phase) combinations.

### Regression tests

- Plugin load time: measure before/after. Should be ≤4 seconds for the spec → mip build.
- Memory: report total wavetable RAM. Should be ≤30MB.
- CPU per voice (steady-state): measure with profiler. Should be ±10% of pre-Phase-10.

---

## CPU/RAM budget

### RAM

- Phase 2C: 24 wavetables × 16 frames × 2048 samples × 4 bytes = 3 MB
- Phase 10a: 24 wavetables × 8 mip levels × 16 frames × 2048 samples × 4 bytes = 24 MB

Net change: +21 MB. Acceptable on any modern machine.

### CPU

- Render per voice: identical (same one bilinear lookup per sample).
- Phase 10b cubic: ~2× per-voice WT CPU. Still under 0.5% per voice. Acceptable.
- Plugin init: +4 seconds first load (acceptable). Phase 10d can cache.

### Memory bandwidth

- Each render samples 2 frames × 4 bytes = 8 bytes per mip-level lookup.
- 32 voices × 48 kHz × 8 bytes = 12 MB/s. Far under any modern CPU's memory bandwidth.

---

## Risks & mitigations

| Risk | Likelihood | Impact | Mitigation |
|---|---|---|---|
| Init time >5 seconds | Medium | Medium | Phase 10d: FFT-based sum + disk cache |
| Memory >30 MB | Low | Low | Reduce mip levels from 8 to 6; lose some quality at top octave |
| Migration of vocal/metallic/experimental factories sounds different | High | Medium | Treat them as Phase 10c work; allow time for creative tuning of frequency-domain recipes |
| Cubic interpolation introduces ringing artifacts | Low | Low | Catmull-Rom is well-tested in audio; can fall back to bilinear |
| Bandlimited basic shapes sound TOO clean (lose character) | Low | Medium | User can add EROSION + filter drive for analog character; pure clean is the right baseline |
| Mip level boundary audible (notes near C3/C4 transition sound different) | Medium | Low | Mip levels overlap enough; differences are subtle. If audible, crossfade between mip levels around boundary notes. |

---

## Future work enabled by Phase 10

Frequency-domain wavetables open up these later phases:

### Phase 11 — Spectral morph modes

In frequency domain, spectral transformations are cheap and musical:

- **Vocode**: shape OSC A's spectrum by an external source's spectrum (per-harmonic amp multiply).
- **Harmonic scale**: shift harmonics inharmonically (3rd harmonic plays as 4th, etc.). Bell-like.
- **Formant shift**: stretch/squish the spectral envelope without changing pitch.
- **Smear**: blur harmonics into noise.
- **Shepard tone wrapping**: continuously rising/falling perception via overlapping octaves.
- **Inharmonic scale**: shift partial frequencies by fixed multiplier (1.05x, 1.1x, etc.) for clangy/metallic.

Each is ~50-150 lines on top of the Phase 10 foundation.

### Phase 12 — Resynthesis from samples

Per the original spec's USER imports plan:
- Drop a WAV → run FFT → extract harmonic content → store as wavetable spec
- "From sample" / "Import WAV" / "From chop" all become viable
- True bridge between Terrain's sampler and synth

### Phase 13 — Random / probabilistic spectra

- Random Gen wavetable: at each note-on, generate a random harmonic distribution
- Probabilistic morphs: per-harmonic probability of being active
- Inspired by Vital's various "Random" modes

---

## Decision points (asked of user before implementation)

1. **Promote Phase 10 ahead of the original plan** (Phase 5 FLOW glide, Phase 6 mod matrix, etc.) or **ship in order**?
   - Promoting = sonic quality jumps immediately
   - In order = systems get filled in completely; Phase 10 is the polish capstone

2. **Phase 10a only, or include Phase 10b cubic + Phase 10c factory migrations** in the same shipping window?
   - 10a alone: ~6-10 hours
   - 10a+b+c bundle: ~12-20 hours (better, single migration moment)

3. **Init time tolerance**: accept ~4 seconds for first-build, or do FFT optimization upfront?

4. **Migrate ALL 24 factories in Phase 10c, or just the core 11 (basic + analog + digital) and treat vocal/metallic/experimental as time-domain legacy permanently**?

---

## Related memories + docs

- `docs/research/vital-deep-dive-2026-06-02.md` — the research that surfaced this approach
- `Design/v1-syn-spec.md` — main synth spec (this phase fits as Phase 10, after Phases 5-9)
- `plugins/TerrainInstrument/Source/Wavetable.h` — current implementation to refactor
- `plugins/TerrainInstrument/Source/WavetableBank.h` — current bank to update
- `plugins/TerrainInstrument/Source/SynthVoice.h` — render path to update
- Phase 8a polish commit `ba8793b` — current head, baseline for Phase 10 work

---

## TL;DR for the user

**What we're doing**: Replacing how Terrain stores wavetables. From "raw samples" to "harmonics + phases per frame," with pre-computed bandlimited versions per octave so they play clean at any pitch.

**Why**: Eliminates the buzzy/whining digital quality that creeps in at high notes. Makes Terrain wavetables match Vital quality. Sets up spectral morph modes for Phase 11.

**Cost**: ~24 MB extra RAM, ~4 seconds extra init time on first load, ~6-10 hours of implementation. Zero render-time CPU change.

**What stays the same**: All UI, all APVTS params, all DAW automation, all preset compatibility. The sound just gets cleaner at high pitches.

**What gets cleaner immediately**: Every basic shape (Sine/Triangle/Square/Pulse) and every analog wavetable. Vocal/Metallic/Experimental wavetables migrate over a few sessions and may sound subtly different (which we'll tune for the better).
