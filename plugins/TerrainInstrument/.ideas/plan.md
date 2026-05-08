# Earcandy - Implementation Plan

**Plugin:** Earcandy (Rack Series)
**Complexity Score:** 4 / 5 (Expert)
**Strategy:** Phased Implementation (3 phases)

---

## Implementation Strategy

### Phase 1: Core Grain Engine (Foundation)
Priority: **Critical path** — everything else depends on this.

- [ ] `GrainBuffer` class — circular buffer with write head, feedback input, per-channel
- [ ] `GrainVoice` struct — read position, playback rate, envelope state, sample counter
- [ ] `GrainPool` class — pre-allocated array of 128 GrainVoice, spawn/free/steal logic
- [ ] `GrainScheduler` — timer-based spawning, spray offset calculation, pool assignment
- [ ] Basic `processBlock` — write input to buffer, tick scheduler, sum active grains
- [ ] Hanning window envelope on each grain
- [ ] Verify: grains play back from buffer, overlap smoothly, no clicks

### Phase 2: Pitch Shifting + Feedback Loop
Priority: **Core features** — transforms basic granular into the full Earcandy sound.

- [ ] Variable-rate playback in GrainVoice (fractional read position)
- [ ] Linear interpolation for fractional sample reads from circular buffer
- [ ] Pitch parameter → playback rate conversion: `rate = pow(2, semitones/12)`
- [ ] Feedback path: route wet sum back into circular buffer at write head
- [ ] Soft limiter (`tanh` saturation) on feedback signal for stability
- [ ] Feedback gain parameter control with smoothing
- [ ] Verify: pitch shifting sounds musical, feedback builds textures without blowup

### Phase 3: Parameter Integration + Polish
Priority: **Production readiness** — smooth, automatable, DAW-friendly.

- [ ] All 6 parameters as `juce::AudioParameterFloat` with proper ranges/skew
- [ ] `juce::SmoothedValue` on all parameters (20ms ramp)
- [ ] Dry/wet mix blending in OutputMixer
- [ ] Stereo processing (independent grain pools per channel, shared scheduler)
- [ ] Edge cases: sample rate changes (`prepareToPlay`), buffer size changes
- [ ] CPU profiling — ensure <5% CPU at 100 grains/s stereo @ 44.1kHz
- [ ] Parameter save/restore (`getStateInformation` / `setStateInformation`)

---

## Dependencies

**Required JUCE Modules:**
- `juce_audio_basics` — AudioBuffer, SmoothedValue
- `juce_audio_processors` — AudioProcessor, AudioParameterFloat
- `juce_dsp` — ProcessSpec, optional DSP utilities

**Optional Modules:**
- `juce_gui_basics` — for Visage path (custom UI rendering)
- `juce_gui_extra` — for WebView path (WebBrowserComponent)

**No external dependencies.** Pure JUCE + standard C++ math.

---

## Class Structure

```
PluginProcessor (juce::AudioProcessor)
├── GrainBuffer          — Circular buffer (one per channel)
│   ├── write()          — Write input + feedback
│   ├── read()           — Read with linear interpolation
│   └── getLength()      — Buffer length in samples
├── GrainPool            — Voice management
│   ├── spawn()          — Activate a grain voice
│   ├── process()        — Tick all active voices, return summed output
│   └── steal()          — Reclaim oldest voice when pool exhausted
├── GrainScheduler       — Timing and position logic
│   ├── tick()           — Check if it's time to spawn
│   └── calcPosition()   — Compute read offset from spray
└── Parameters           — 6 x AudioParameterFloat + SmoothedValue
```

---

## Risk Assessment

**High Risk:**
- **Grain overlap clicks** — If envelope windowing is wrong, overlapping grains will click. Mitigation: Hanning window with strict zero-crossing at grain boundaries.
- **Feedback instability** — High feedback + high density could cause runaway amplitude. Mitigation: `tanh` saturation before feedback write, hard limiter as safety net.

**Medium Risk:**
- **Pitch shifting artifacts** — Linear interpolation at extreme pitch values may alias. Mitigation: Linear interp is adequate for musical ranges (-12 to +12 st); no need for sinc interpolation at this stage.
- **CPU spikes at high density** — 100 grains/s with large grain sizes means many simultaneous voices. Mitigation: Pool cap at 128 voices, oldest-steal policy, profile early.

**Low Risk:**
- **Circular buffer wraparound** — Standard ring buffer pattern, well-understood.
- **Parameter smoothing** — JUCE SmoothedValue handles this cleanly.
- **State save/restore** — Standard JUCE XML serialization pattern.

---

## Estimated File Count

| File | Purpose |
|------|---------|
| `Source/PluginProcessor.h` | Processor class with grain engine members |
| `Source/PluginProcessor.cpp` | processBlock, prepareToPlay, parameter setup |
| `Source/PluginEditor.h` | UI editor (framework-dependent) |
| `Source/PluginEditor.cpp` | UI implementation |
| `Source/GrainEngine.h` | GrainBuffer, GrainVoice, GrainPool, GrainScheduler |
| `CMakeLists.txt` | Build configuration |

**Total: 6 source files** (lean, single-header for the grain engine)
