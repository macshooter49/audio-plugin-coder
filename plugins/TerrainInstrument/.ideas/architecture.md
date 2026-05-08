# Earcandy - DSP Architecture Specification

**Plugin:** Earcandy (Rack Series)
**Category:** Granular Texturizer / Generative Soundscape Engine

---

## Core Components

### 1. Circular Buffer (`GrainBuffer`)
- Continuously records incoming audio into a fixed-length ring buffer
- Buffer length: ~5 seconds at current sample rate (e.g., 220,500 samples at 44.1kHz)
- Write head advances linearly; old audio is overwritten naturally
- Feedback signal is mixed into the buffer at the write head position

### 2. Grain Scheduler (`GrainScheduler`)
- Timer-based spawner: fires at intervals derived from `density` parameter
- Spawn interval = `1.0 / density` seconds (e.g., 20 grains/s = every 50ms)
- On spawn: calculates grain read position based on write head and `spray`
  - `spray = 0%`: read position = write head - grain_size (tight echo)
  - `spray = 100%`: read position = random point anywhere in buffer
  - Intermediate: random offset within `spray * buffer_length` behind write head
- Assigns grain parameters: size, pitch rate, start position

### 3. Grain Pool (`GrainPool`)
- Pre-allocated pool of grain voices (max ~128 simultaneous grains)
- Each grain holds: read position, playback rate, remaining samples, envelope phase
- Grain lifecycle: spawn → attack → sustain → release → free
- Avoids real-time allocation — grains are recycled from the pool

### 4. Grain Voice (`GrainVoice`)
- Reads from circular buffer at calculated position
- Applies window envelope (Hanning) for smooth overlap:
  ```
  envelope(t) = 0.5 * (1 - cos(2π * t / grain_length))
  ```
- Applies pitch shift via variable-rate playback:
  ```
  playback_rate = 2^(pitch_semitones / 12.0)
  ```
  - Rate > 1.0 = higher pitch (reads buffer faster)
  - Rate < 1.0 = lower pitch (reads buffer slower)
- Uses linear interpolation for fractional sample positions

### 5. Output Mixer (`OutputMixer`)
- Sums all active grain voices into a wet signal
- Applies soft clipping / saturation to prevent feedback runaway
- Blends dry (input) and wet (grains) based on `mix` parameter:
  ```
  output = dry * (1 - mix) + wet * mix
  ```

### 6. Feedback Path
- Routes the wet output back to the circular buffer's write head
- Mixed with incoming audio: `buffer[write] = input + wet * feedback`
- Feedback capped at 95% in the parameter, with additional soft limiting in DSP
- Creates self-generating textures that evolve and sustain after input stops

---

## Processing Chain

```
Input Audio ─────────────────────────────────────────┐
    │                                                 │
    v                                                 │
[Circular Buffer] ◄── feedback ── [Soft Limiter] ◄──┐│
    │                                                ││
    │  (spray selects read positions)                ││
    v                                                ││
[Grain Scheduler] ── spawns ──► [Grain Pool]         ││
                                    │                ││
                          (per grain:)               ││
                          ├─ Hanning envelope        ││
                          ├─ Variable-rate read      ││
                          └─ Linear interpolation    ││
                                    │                ││
                                    v                ││
                              [Voice Summer] ────────┘│
                                    │                  │
                                    v                  │
                              [Output Mixer] ◄─────────┘
                                (dry/wet)
                                    │
                                    v
                              Output Audio
```

---

## Parameter Mapping

| Parameter | Component | Function | Range | DSP Variable |
|-----------|-----------|----------|-------|--------------|
| `grain_size` | GrainScheduler / GrainVoice | Sets grain duration in samples | 5-500ms | `grainLengthSamples = sampleRate * grainSize / 1000` |
| `density` | GrainScheduler | Sets spawn interval | 1-100 grains/s | `spawnInterval = sampleRate / density` |
| `spray` | GrainScheduler | Randomizes grain read offset | 0-100% | `maxOffset = spray * bufferLength` |
| `pitch` | GrainVoice | Sets playback rate | -12 to +12 st | `rate = pow(2.0, pitch / 12.0)` |
| `feedback` | Feedback Path | Wet signal recirculation | 0-95% | `feedbackGain = feedback / 100.0` |
| `mix` | OutputMixer | Dry/wet blend | 0-100% | `mixAmount = mix / 100.0` |

---

## Memory Layout

| Buffer | Size | Purpose |
|--------|------|---------|
| Circular buffer (per channel) | ~220K samples (~5s @ 44.1kHz) | Incoming audio storage |
| Grain pool | 128 x GrainVoice (~16 bytes each) | Active grain state |
| Smoothed parameters | 6 x SmoothedValue | Click-free parameter changes |

**Total estimated memory:** ~1.8 MB stereo (dominated by circular buffer)

---

## Real-Time Safety

- **No allocations in processBlock:** Grain pool is pre-allocated, circular buffer is fixed-size
- **No locks:** Grain spawning and voice summing are single-threaded in audio callback
- **Parameter smoothing:** All 6 parameters use `juce::SmoothedValue` (ramp 20ms)
- **Feedback stability:** Soft limiter (`tanh` saturation) before feedback write prevents runaway
- **Grain overflow:** If pool is exhausted, oldest grains are stolen (no dropped frames)

---

## Complexity Assessment

**Score: 4 / 5 (Expert)**

**Rationale:**
- Granular engine with overlapping voices requires careful state management
- Variable-rate playback with interpolation is non-trivial DSP
- Feedback loop needs stability safeguards (saturation, gain limiting)
- Grain scheduling with randomized positions requires good random distribution
- Memory management for grain pool needs real-time-safe design
- Stereo processing doubles buffer requirements

**Mitigating factors:**
- Only 6 parameters (no modulation matrix, no envelopes beyond grain window)
- No FFT or spectral processing needed
- Pitch shifting via resampling (not phase vocoder) keeps complexity manageable
- Fixed buffer size avoids dynamic allocation concerns
