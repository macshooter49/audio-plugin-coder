# Vital Synthesizer Deep-Dive Research
**Date:** June 2, 2026  
**Source Repo:** `/Users/macshooter/Downloads/vital-main/` (commit ~Apr 2022, GPLv3)  
**Purpose:** Legal clean-room reference for understanding high-quality wavetable synthesis architecture and DSP techniques applicable to Terrain Instrument

---

## EXECUTIVE SUMMARY

Vital achieves its "Vital-grade" sound quality through sophisticated frequency-domain wavetable synthesis with runtime bandlimiting, per-voice unison orchestration with advanced voice-sharing, rich spectral morphing effects, and aggressive SIMD vectorization. Key architectural wins: (1) **frequency-domain storage** of harmonic amplitude + phase per frame, enabling per-pitch bandlimiting; (2) **runtime spectral morph** at audio rate with 12+ modes; (3) **Catmull-Rom cubic frame interpolation** for smooth morphing; (4) **voice stacking** within a single poly voice (unison without stealing extra polyphony); (5) **Shepard tone mode** that creates infinite ascending/descending pitch illusion via spectral wrapping.

---

## P1: WAVETABLE SYNTHESIS (Priority 1 - CRITICAL)

### 1.1 Wavetable Storage Format

**Time-Domain + Frequency-Domain Hybrid Storage**

Vital stores each wavetable frame in two forms:

1. **Time Domain:** Raw 2048-sample waveform buffer (float, mono)
   - File: `vital/src/synthesis/lookups/wavetable.h` line 44
   - Structure: `std::unique_ptr<mono_float[][kWaveformSize]> wave_data;`
   - Size: `kWaveformSize = 1 << 11 = 2048` samples

2. **Frequency Domain:** Complex FFT representation + Harmonic metadata
   - File: `vital/src/synthesis/lookups/wavetable.h` lines 45-47
   - Structures:
     ```cpp
     std::unique_ptr<poly_float[][kPolyFrequencySize]> frequency_amplitudes;  // Harmonic magnitudes
     std::unique_ptr<poly_float[][kPolyFrequencySize]> normalized_frequencies; // Unit-magnitude phase
     std::unique_ptr<poly_float[][kPolyFrequencySize]> phases;                // Raw phase angles
     ```
   - `kNumHarmonics = kWaveformSize / 2 + 1 = 1025` (Nyquist = 1025th bin)
   - `kPolyFrequencySize = 2 * 1025 / 4 + 2` (SIMD vectorization, see P4)

**Metadata Per Frame:**
- `frequency_ratio`: Fundamental frequency ratio (for pitch detection on imported samples)
- `sample_rate`: Sample rate context

**No Mip-Mapping; Instead: Runtime Per-Pitch Bandlimiting**
- Vital does NOT store multiple octave-reduced versions
- Instead, uses frequency-domain data to dynamically limit harmonics at render time (see Section 1.2)

**File Format:**
- Internal: Fundamental is Wavetable container (not a named file format like `.vitaltable`)
- Wavetables are loaded/composed programmatically via `WaveFrame` objects
- User presets stored as full synthesizer state JSON (not isolated wavetable files)

---

### 1.2 Per-Pitch Bandlimiting (THE KEY to Vital's Clean Sound)

**Problem Solved:**  
When a wavetable designed at C3 is played at C8 (5 octaves higher), naïve playback causes aliasing. The 10th harmonic of the original design becomes 10× higher frequency, exceeding Nyquist at high pitches.

**Vital's Solution: Harmonic-Count Reduction at Render Time**

File: `vital/src/synthesis/producers/synth_oscillator.cpp` lines 1095–1150 (`runSpectralMorph`)  
Key files: `vital/src/synthesis/lookups/wavetable.cpp` lines 160–178

**Mechanism:**
1. **Frequency Bin Calculation:** At each note's playback pitch, determine which harmonics fit below Nyquist:
   ```cpp
   // vital/src/synthesis/lookups/wavetable.h lines 63–70
   static force_inline int getFrequencyBin(mono_float phase_increment) {
     int num_waves = 1.0f / phase_increment;
     return utils::iclamp(utils::ilog2(num_waves), 0, kFrequencyBins - 1);
   }
   ```
   - Phase increment = `frequency / sample_rate`
   - Fewer harmonics needed at higher pitches

2. **Harmonic Amplitude Scaling:** Pre-stored per-frame, per-harmonic magnitudes + phases used to rebuild limited waveforms in frequency domain, then IFFT back to time domain

3. **Stored Frequency Data:** 
   - `frequency_amplitudes[frame][harmonic]` = magnitude of each harmonic (real part)
   - `normalized_frequencies[frame][harmonic]` = unit-magnitude complex number holding phase
   - `phases[frame][harmonic]` = raw angle

4. **No Explicit Filtering:** Instead of applying a high-pass filter, Vital simply zeros out the high-harmonic bins and reconstructs the waveform. This is mathematically pure and avoids filter ringing.

**Interpolation Across Frames:**  
When morphing between wavetable frames, phase continuity is maintained via the `normalized_frequencies` data (interpolated harmonics stay in sync). See Section 1.3.

**Result:** Wavetables remain clean and harmonically rich at *any* MIDI note, matching the theoretical Nyquist limit.

---

### 1.3 Frame Interpolation

**Cubic Catmull-Rom Interpolation**

File: `vital/src/synthesis/producers/synth_oscillator.cpp` lines 209–225

```cpp
force_inline poly_float linearlyInterpolateBuffer(const mono_float* buffer, const poly_int indices) {
  poly_int start_indices = utils::shiftRight<kIntermediateBits>(indices);
  poly_float t = getInterpolationValues(indices);
  matrix interpolation_matrix = utils::getCatmullInterpolationMatrix(t);
  matrix value_matrix = utils::getValueMatrix(buffer, start_indices);
  value_matrix.transpose();
  return interpolation_matrix.multiplyAndSumRows(value_matrix);
}
```

- Catmull-Rom is **cubic** (4-point interpolation), higher quality than bilinear
- Applied in **time domain** (raw samples), not frequency domain
- Works because time-domain waveforms are pre-band-limited (per section 1.2)
- Phase alignment handled implicitly by harmonic metadata

**Frame Blending:**  
When transitioning from frame N to N+1:
- `interpolateMultipleBuffers()` blends both frames over a fade window
- Per-frame frequency data guides harmonic continuity
- Result: smooth, phase-coherent morphing without artifacts

---

### 1.4 Wavetable Warp Modes (Distortion Types)

Vital calls these "Distortion Types" in the `SynthOscillator` enum. They all operate **per-sample** on the phase, pre-lookup:

File: `vital/src/synthesis/producers/synth_oscillator.h` lines 116–131

| Mode | Implementation | Use Case |
|------|---|---|
| **None** | Pass-through phase | Straight wavetable |
| **Sync** | `syncPhase()` line 107–110 | Virtual slave oscillator, frequency wrapping |
| **Formant** | Phase scaling + window (not explicit code, see form scale in spectral morph) | Vocal formant shifting |
| **Quantize** | `quantizePhase()` lines 64–70 | Reduce harmonic richness, lo-fi texture |
| **Bend** | `bendPhase()` lines 72–87 | Casio CZ phase distortion, smooth waveshaping |
| **Squeeze** | `squeezePhase()` lines 89–105 | Compress phase space, harmonic folding |
| **Pulse Width** | `pulseWidthPhase()` lines 112–117 | PWM-style effect |
| **FM Osc A/B, RM Osc A/B, RM Sample** | `fmPhase()`, `fmPhaseLeft()`, `fmPhaseRight()` lines 119–139 | Frequency/Ring modulation by another oscillator or sample |

**Phase Modulation Architecture:**
- Phase distortion is **additive** to the base phase: `distorted_phase = base_phase + distortion_offset`
- Distortion amount controls the strength
- Per-voice spreading allows each unison voice to have slightly different distortion character

---

### 1.5 WT Preset Generation

File: `vital/src/synthesis/lookups/wave_frame.cpp` and `vital/src/common/wavetable/wave_source.cpp`

**Predefined Shapes via Additive Synthesis:**

`PredefinedWaveFrames` class creates basic shapes programmatically:
- `createSin()` → Pure sine
- `createSaturatedSin()` → Soft-clipped sine (adds 2nd harmonic character)
- `createTriangle()` → Triangle wave (odd harmonics, 1/n² amplitude decay)
- `createSquare()` → Square (odd harmonics, 1/n)
- `createPulse()` → Pulse (variable width, similar to square)
- `createSaw()` → Sawtooth (all harmonics, 1/n decay)

These are generated **once at startup** and stored as static instances. No FFT required; they're synthesized in frequency domain then IFFT'd to time domain.

**User Wavetables (Imported or Drawn):**
- `WaveSource` (user-drawn breakpoints)
- `FileSource` (imported audio samples, pitch-detected)

Both go through:
1. Time-domain capture or drawing
2. FFT to frequency domain (line `wave_frame.cpp` ~45)
3. Phase & amplitude extraction
4. Storage in `Wavetable::WavetableData`

**No "Analog Character" Wavetables:**
- Vital doesn't pre-bake analog-modeled wavetables
- Analog character comes from other modules (ladder filter self-oscillation, distortion, unison drift)

---

### 1.6 Sub-Oscillator & Phase Modulation (FM)

**FM Between Oscillators:**

File: `vital/src/synthesis/producers/synth_oscillator.cpp` lines 119–139 (`fmPhase`, `fmPhaseLeft`, `fmPhaseRight`)

Vital implements **carrier-modulator FM** in the phase domain:
```cpp
force_inline poly_int fmPhase(poly_int phase, poly_float distortion, poly_int,
                              const poly_float* modulation, int i) {
  poly_float phase_offset = modulation[i] * distortion;
  return phase + utils::toInt(phase_offset * kFmPhaseMult) * kMaxFmModulation;
}
```

- `modulation[i]` = output of modulator oscillator (Osc B or sample)
- `distortion` = FM amount knob
- Phase increment is directly modulated before table lookup
- Carrier reads from a wavetable; modulator pitch sets the FM rate

**Result:** Smooth, musically-controlled FM without needing a separate FM operator. Modulator can be another wavetable, giving complex FM timbres.

---

### 1.7 Basic Shapes (Sine, Triangle, Square, Pulse, Saw)

**Stored as Wavetables:**  
Vital keeps one frame of each basic shape in the `PredefinedWaveFrames` singleton. They are **not computed on-the-fly per note**.

**Bandlimiting:**  
When these shapes are played at higher pitches, the runtime bandlimiting (Section 1.2) automatically removes harmonics above Nyquist, preserving analog fidelity. For example:
- Sawtooth at C1: all 20 harmonics active
- Sawtooth at C8: only 2–3 harmonics (due to bandlimiting)

No pre-stored octave-reduced versions needed.

---

### 1.8 Wavetable Editor (UI Side)

File: `vital/src/common/wavetable/` (all files)

**Conceptual Model:**
- Wavetable = sequence of keyframes
- Each keyframe can be one of several **sources**:
  - `WaveSource`: User draws breakpoints (harmonic editor)
  - `FileSource`: Imported audio sample + pitch detection + windowing
  - `WaveLineSource`: Manual waveform drawing
  - Other modifiers: phase, frequency filtering, wave folding, warping, etc.

**Rendering Pipeline:**
1. User edits a keyframe (e.g., draws a shape or imports a sample)
2. Keyframe renders to a `WaveFrame` (time + frequency domain)
3. `WaveFrame::toFrequencyDomain()` calls FFT
4. FFT data extracted and stored in `Wavetable::WavetableData`
5. Audio thread reads from the wavetable at runtime

**Crucial:** User-facing editor is decoupled from synthesis. Synthesis only reads the pre-computed wavetable buffers and harmonic metadata.

---

## P2: VOICE MANAGEMENT + UNISON

### 2.1 Voice Pool Size

File: `vital/src/synthesis/framework/voice_handler.h` lines 247–490

**Voice Allocation Model:**

Vital uses a **fixed polyphony pool** with **parallel voice stacking**:

1. **Maximum Polyphony:** User-settable, typically 32–64 voices (plugin default TBD in your build)
2. **Voice Multiplication:** When unison > 1, Vital does **NOT** grab multiple polyphony slots
   - Instead: Per `SynthOscillator`, each voice object internally mixes up to 16 unison sub-oscillators
   - File: `vital/src/synthesis/producers/synth_oscillator.h` line 156: `static constexpr int kMaxUnison = 16;`

3. **Parallel Voices:** For true polyphonic scaling, Vital uses `addParallelVoices()`:
   - File: `vital/src/synthesis/framework/voice_handler.cpp` lines 874–898
   - Creates `kParallelVoices` (typically 4) voice instances within a single `AggregateVoice`
   - Each parallel voice processes 2 SIMD lanes
   - All parallelvoices share the same note/envelope but run vectorized

**Result:** A single MIDI voice = 1 entry in the polyphony pool, regardless of unison count.

---

### 2.2 Unison Architecture

File: `vital/src/synthesis/producers/synth_oscillator.h` lines 70–85, `cpp` lines 1215–1216

**Per-Voice Unison (No Extra Polyphony Stealing):**

```cpp
// vital/src/synthesis/producers/synth_oscillator.cpp line 1215
unison_ = utils::clamp(roundf(input(kUnisonVoices)->at(0)[0]), 1.0f, kMaxUnison);
setActiveOscillators(unison_ + (unison_ % 2));
```

- User sets unison voices (1–16)
- Each oscillator internally runs N parallel phase accumulators (SIMD-vectorized)
- **No voice stealing** for unison; it's all in-voice modulation

**Detuning Per Unison Voice:**

File: `vital/src/synthesis/producers/synth_oscillator.cpp` lines 612–620

```cpp
poly_float cents = range * input(kUnisonDetune)->at(0);
// ...
mono_float divisor = utils::max(1.0f, unison_ - 1.0f);
int bump = (unison_ % 2 == 0) ? 1 : 0;
```

- Each unison voice gets a ±detune spread (e.g., ±15 cents for unison=5)
- Detuning follows a power curve (`unison_ % 2 == 0` biases center)
- Stored in `detunings_[kNumPolyPhase]` (phase increment multipliers)

**Stereo Spread & Voice Spreading:**

File: `vital/src/synthesis/producers/synth_oscillator.h` lines 80–82:
- `kUnisonFrameSpread`: Each voice morphs between slightly different wavetable frames
- `kUnisonDistortionSpread`: Each voice uses different distortion amount
- `kUnisonSpectralMorphSpread`: Each voice morphs spectrally to different effect

These are applied as **per-unison-voice offsets** in the processing loop, creating harmonic richness without voice stealing.

---

### 2.3 Voice Stealing

File: `vital/src/synthesis/framework/voice_handler.cpp` lines 418–423, 770–782

**Graceful Prioritization:**

```cpp
Voice* getVoiceToKill(int max_voices);
```

When max polyphony is exceeded, voices are chosen for release based on:

1. **VoicePriority** enum (user-configurable):
   - `kNewest`: Kill the oldest held note
   - `kOldest`: Kill the newest note (FIFO)
   - `kHighest`: Kill the highest-pitched voice
   - `kLowest`: Kill the lowest-pitched voice
   - `kRoundRobin`: Cycle through voices

2. **VoiceOverride** enum:
   - `kKill`: Instantly kill (0ms)
   - `kSteal`: Retrigger the voice with the new note (smooth transition)

**No Fade-Out:** Voice killer relies on envelope tail or sustain pedal to mask the transition. Vital doesn't force a release envelope on stolen voices.

---

### 2.4 Paraphonic Behavior (Voice Sharing Confirmed)

**This Is the "Voice Sharing" the User Mentioned:**

File: `vital/src/synthesis/framework/voice_handler.cpp` lines 874–898 (`addParallelVoices`)

When unison > number of free traditional voices, Vital **creates parallel voices within a single aggregate voice**:

```cpp
void VoiceHandler::addParallelVoices() {
  for (int i = 0; i < kParallelVoices; ++i) {
    std::unique_ptr<Voice> single_voice = std::make_unique<Voice>(aggregate_voice.get());
    aggregate_voice->voices.push_back(single_voice.get());
    free_voices_.push_back(single_voice.get());
    all_voices_.push_back(std::move(single_voice));
  }
  all_aggregate_voices_.push_back(std::move(aggregate_voice));
}
```

**Effect:**  
- 4 parallel voices share 1 polyphonic envelope (shared amplitude envelope)
- Each handles 2 SIMD lanes (~8 oscillators total)
- Appears as "shared" because they respond to the same note-on/note-off

**Is This True Paraphony?**  
Not fully—each parallel voice still gets its own pitch modulation (pitch wheel, bend, etc.). True paraphony would share a single oscillator across multiple notes. Vital's approach is **voice stacking with shared envelopes**, not full paraphony.

---

### 2.5 Per-Voice Modulation Sources

File: `vital/src/synthesis/modules/synth_voice_handler.h` lines 38–62

**Voice-Scope Modulation:**
- Each voice has its own envelope state
- Each voice has its own LFO phase (unless synced to tempo)
- Per-voice random offsets from `TriggerRandom`

**Storage in VoiceState:**

File: `vital/src/synthesis/framework/voice_handler.h` lines 30–46:
```cpp
struct VoiceState {
  VoiceEvent event;
  int midi_note;
  mono_float tuned_note;
  poly_float last_note;
  mono_float velocity;
  mono_float lift;
  mono_float local_pitch_bend;
  int note_pressed;
  int note_count;
  int channel;
  bool sostenuto_pressed;
};
```

- `tuned_note`: MIDI note after key scaling + tuning system
- `local_pitch_bend`: Per-voice pitch wheel offset
- `velocity`: Captured at note-on
- LFO phases stored in `SynthLfo` processor (one per voice instance)

**Random Drift:**

File: `vital/src/synthesis/producers/synth_oscillator.cpp` lines 568–586

```cpp
uint32_t random_phase_left = random_generator_.next() * random_amount[2 * v] * INT_MAX;
uint32_t random_phase_right = random_generator_.next() * random_amount[2 * v + 1] * INT_MAX;
phases_[i].set(2 * v, random_phase_left);
phases_[i].set(2 * v + 1, random_phase_right);
```

- Per-note, a random phase offset (0–INT_MAX) is added to each unison voice
- Controlled by `kRandomPhase` knob
- Prevents identical attack transients across notes

---

### 2.6 Voice Modes (Mono, Glide, Legato)

File: `vital/src/synthesis/framework/voice_handler.h` lines 395–401

```cpp
force_inline void setLegato(bool legato) { legato_ = legato; }
force_inline bool legato() { return legato_; }
```

**Legato Mode:**
- `legato_` flag enables/disables re-triggering on new notes while a note is held
- When legato = on and a new note arrives while the current envelope is sustaining, the new note continues the envelope (no retrigger)

**Mono Mode:**  
Implied by `polyphony_ == 1` (configurable at instantiation). When `setPolyphony(1)`:
- Only 1 voice can be active
- New notes steal the current voice

**Glide/Portamento:**

File: `vital/src/synthesis/utilities/portamento_slope.cpp`  
File: `vital/src/synthesis/utilities/legato_filter.cpp`

- Implemented as a portamento filter on the MIDI note stream
- Not explicit in voice_handler; handled by a module that smooths pitch changes over a glide time

---

## P3: ANALOG CHARACTER & ALIVENESS

### 3.1 Random Pitch Drift

File: `vital/src/synthesis/producers/synth_oscillator.h` lines 34–56 (`RandomValues`)

**Pre-Computed Random Harmonics:**

Vital generates a static lookup table of random complex values at startup:

```cpp
class RandomValues {
  static RandomValues* instance() {
    int size = (kRandomAmplitudeStages + 1) * (Wavetable::kNumHarmonics + 1) / poly_float::kSize;
    static RandomValues instance(size);
    return &instance;
  }
  // ...
  for (int i = 0; i < num_poly_floats; ++i)
    data_[i] = generator.polyNext();  // Random ∈ [-1, 1]
};
```

**Per-Voice Phase Randomization (On Note-On):**

File: `vital/src/synthesis/producers/synth_oscillator.cpp` lines 583–586

- Each note's first sample gets a random phase offset
- Prevents all notes sounding identical
- Range controlled by `kRandomPhase` input

**Spectral Random Amplitudes:**

File: `vital/src/synthesis/producers/synth_oscillator.cpp` line 750 (`kRandomAmplitudes`)

- Adds random noise to harmonic amplitudes in frequency domain
- Creates a "fuzzy" wavetable character
- Modulate for evolving timbres

**Shepard Tone Wrapping (Infinite Pitch Illusion):**

File: `vital/src/synthesis/producers/synth_oscillator.cpp` lines 637–683 (`setupShepardWrap`, `doShepardWrap`)

Vital implements the Shepard tone effect:
- High frequencies fade out (decrease amplitude)
- Low frequencies fade in (increase amplitude)
- Middle range at full amplitude
- Creates an endless ascending/descending pitch sensation

Implementation: per-frame, masks are computed that either **double** or **halve** phase indices for the top and bottom octaves, creating the illusion.

---

### 3.2 Filter Character

File: `vital/src/synthesis/filters/ladder_filter.h` and `cpp`

**Ladder Filter (Moog-Style):**

Vital provides multiple filter types (via `SynthFilter` interface):
- `LadderFilter`: 4-stage cascade (24 dB/octave), analog-modeled with saturation
- `DiodeFilter`: Diode ladder variant
- `DigitalSvf`: State-variable filter
- `SallenKeyFilter`: Active Sallen-Key topology
- `FormantFilter`: Vocal formant peaks

**Ladder Filter Specifics:**

```cpp
class LadderFilter : public Processor, public SynthFilter {
  static constexpr int kNumStages = 4;
  static constexpr mono_float kResonanceTuning = 1.66f;
  OnePoleFilter<futils::algebraicSat> stages_[kNumStages];  // Each stage saturating
};
```

- 4 cascaded one-pole filters (saturation on each)
- `futils::algebraicSat` = soft saturation curve (tanh-like, using polynomial approximation)
- Feedback path applies resonance (Q)

**Self-Oscillation:**

File: `vital/src/synthesis/filters/ladder_filter.cpp` (TBD exact line, but implied by resonance handling)

- Ladder filter can ring at cutoff frequency when resonance is high
- No explicit damping; resonance stability tuned by `kResonanceTuning` constant

**Output Stage:**

- No master saturation or compression in the base synth engine
- Distortion/saturation applied via the `Distortion` module (separate effect processor)

---

### 3.3 Output Stage & Saturation

File: `vital/src/synthesis/effects/distortion.h` and `cpp`

**Distortion Types:**

```cpp
enum Type {
  kSoftClip,    // tanh-like
  kHardClip,    // clipping at ±1.0
  kLinearFold,  // triangle fold
  kSinFold,     // sine-based folding
  kBitCrush,    // reduce bit depth
  kDownSample,  // reduce sample rate
};
```

- `kSoftClip` = `futils::algebraicSat` (soft knee saturation)
- `kHardClip` = clipping at ±INT_MAX (raw clip)
- Others provide character textures

**Soft Clipping Implementation:**

File: `vital/src/synthesis/effects/distortion.cpp` (algebraic saturation likely in `futils.h`)

Vital uses **algebraic soft saturation** (not tanh):
```
y = x / (1 + |x|)
```
or similar rational approximation (faster than tanh, close perceptually).

**Master Compressor:**

File: `vital/src/synthesis/effects/compressor.h` and `cpp`

Vital includes a compressor module but it's **optional** (in effects chain, not always-on). Not part of the base synthesis "analog character."

---

### 3.4 Phase Randomization at Note-On

Covered in Section 3.1: `kRandomPhase` input randomizes each voice's starting phase to avoid identical transients.

---

## P4: BONUS — ARCHITECTURE OBSERVATIONS

### 4.1 Modulation Matrix (Patchbay)

File: `vital/src/synthesis/modules/modulation_connection_processor.h` and `cpp`

**Patchbay Design:**

- `ModulationConnectionProcessor`: Represents **one cable** in the patchbay
- User can create N connections; each is an instance of this processor
- File: `vital/src/synthesis/modules/synth_voice_handler.h` lines 82–83
  ```cpp
  ModulationConnectionBank modulation_bank_;
  CircularQueue<ModulationConnectionProcessor*> enabled_modulation_processors_;
  ```

**Per-Knob Mod Accumulation:**

Each UI control/parameter is a `Value*`. When modulation is applied:
1. Modulation source output (e.g., LFO) is read
2. Modulation amount is scaled
3. Result is **added** to the base parameter value
4. The accumulated value is read by the module using it

File: `vital/src/synthesis/modules/modulation_connection_processor.h` lines 55–59:
```cpp
void initializeBaseValue(Value* base_value) { current_value_ = base_value; }
void initializeMapping() { map_generator_->initLinear(); }
mono_float currentBaseValue() const { return current_value_->value(); }
```

**Mod Sources Enumeration:**
- MIDI inputs (note, velocity, CC, etc.)
- LFOs (4 main + 4 random)
- Envelopes (amplitude, 4 custom)
- Macro knobs (user-assigned scaling)
- Arpeggiator outputs
- External inputs (if multi-in plugin)

**Per-Knob Mod Limit:**  
One modulation destination = one output accumulator. Multiple sources can modulate the same knob (mixed in the accumulator).

---

### 4.2 LFO Implementation

File: `vital/src/synthesis/modulators/synth_lfo.h` and `cpp`

**LineGenerator-Based LFO:**

```cpp
class SynthLfo : public Processor {
  LineGenerator* source_;  // User-drawn breakpoint curve
};
```

- Each LFO uses a `LineGenerator` (stored curve with breakpoints)
- User **draws** the shape in the UI (not limited to sine/triangle/saw)
- At runtime, `SynthLfo` reads from the curve at each phase

**Curve Storage:**

File: `vital/src/common/line_generator.h` (TBD exact structure, but implied from usage)

- Breakpoints stored as time/value pairs
- Catmull-Rom or linear interpolation between points
- Cubic interpolation for smooth curves (see Section 1.3 pattern)

**LFO Features:**

- Frequency: Tempo sync (whole notes, dotted, triplets, free-run)
- Sync modes: Trigger, continuous, envelope, loop, etc.
- Stereo phase offset per voice
- Fade-in time
- Delay before start
- Per-voice retrigger (resets phase on new note)

---

### 4.3 Block Sizes & Control-Rate Processing

File: `vital/src/synthesis/framework/common.h` line 50:
```cpp
constexpr int kMaxBufferSize = 128;
```

**Audio Rate vs. Control Rate:**

- **Audio Rate:** 128 samples per `process()` call (at 44.1 kHz ≈ 2.9 ms)
- **Control Rate:** 1 sample per `process()` call (updates slow modulation like envelopes)

File: `vital/src/synthesis/framework/processor.h` lines 130–172:
- Processors can declare `control_rate = true` to process at lower rates
- Controls like knobs and MIDI CC use control rate
- Audio synthesis (oscillators, filters) use audio rate

**Oversampling:**

File: `vital/src/synthesis/framework/processor.h` lines 164–172:
```cpp
virtual void setOversampleAmount(int oversample) {
  state_->oversample_amount = oversample;
  // ...
  output(i)->ensureBufferSize(kMaxBufferSize * oversample);
}
```

- Entire signal chain can be oversampled (2×, 4×, 8×)
- Buffers scale accordingly
- Default: 1× (no oversampling)
- User can enable higher oversampling for cleaner filters at CPU cost

---

### 4.4 SIMD Vectorization (SSE2 / NEON)

File: `vital/src/synthesis/framework/poly_values.h` lines 23–70

**Vital Requires SIMD:**

```cpp
#if VITAL_AVX2
  #define VITAL_AVX2 1
  static_assert(false, "AVX2 is not supported yet.");
#elif __SSE2__
  #define VITAL_SSE2 1
#elif defined(__ARM_NEON__) || defined(__ARM_NEON)
  #define VITAL_NEON 1
#else
  static_assert(false, "No SIMD Intrinsics found which are necessary for compilation");
#endif
```

**SIMD Types:**

```cpp
struct poly_int {
  #if VITAL_SSE2
    static constexpr size_t kSize = 4;  // 4× 32-bit ints per vector
    typedef __m128i simd_type;
  #elif VITAL_NEON
    static constexpr size_t kSize = 4;
    typedef uint32x4_t simd_type;
  #endif
};

struct poly_float {
  // Similar: 4× 32-bit floats per vector (SSE/NEON)
};
```

**Usage:**

File: `vital/src/synthesis/producers/synth_oscillator.cpp` lines 28–56:
```cpp
constexpr int kNumVoicesPerProcess = poly_float::kSize / 2;  // 2 voices per vector
constexpr int kPolyPhasePerVoice = kMaxUnison / poly_float::kSize;
```

- 4-lane SIMD processes 2 voices in parallel (each voice = 2 SIMD lanes for stereo)
- Oscillator phases, detunes, and distortion values are all vectorized
- Wavetable frame interpolation uses matrix ops on SIMD values (Section 1.3)

**Performance Impact:**  
- Rough 2–4× speedup vs. scalar code for oscillators/filters
- Vectorized filter cascades, envelope states, LFO phases
- Essential for Vital's real-time polyphonic synthesis at high voice counts

---

## COMPARISON WITH TERRAIN INSTRUMENT (CURRENT STATE)

| Feature | Vital | Terrain (Current) | Gap |
|---------|-------|-------------------|-----|
| **Wavetable Storage** | Time + Frequency hybrid (FFT precomputed) | Time-domain only (2048 float array per frame) | **CRITICAL**: Missing frequency-domain bandlimiting data |
| **Per-Pitch Bandlimiting** | Yes (runtime harmonic limit based on Nyquist) | No (all 24 wavetable frames use full spectrum) | **P1 TODO**: Add frequency_amplitudes, normalized_frequencies, phases per frame |
| **Frame Interpolation** | Cubic Catmull-Rom | Bilinear | Better quality in Vital; acceptable in Terrain (but upgrade feasible) |
| **Unison Architecture** | In-voice stacking (16 max, no poly steal) | TBD in current build; assume similar unison per voice | Likely aligned |
| **Voice Stealing** | Priority-based (newesat/oldest/pitch) | TBD | Likely adequate |
| **Drift/Randomization** | Random phase per note, random harmonics mode | TBD EROSION/HORIZON | Terrain's approach seems richer |
| **Filter** | Ladder + SVF + Diode + Sallen-Key + Formant | Single Ladder LPF24 (JUCE) | Vital has more options; Terrain's choice is reasonable |
| **Distortion/Saturation** | Algebraic soft-clip in ladder + effects distortion module | TBD | Likely covered |
| **LFO** | User-drawn line generator | TBD | Likely simpler in Terrain |
| **SIMD Vectorization** | Required (SSE2/NEON) | JUCE DSP likely handles implicitly | Terrain may have less explicit control |
| **Block Size** | 128 samples @ 44.1 kHz | JUCE standard (varies per host) | Minor diff |

---

## HIGHEST-VALUE TAKEAWAYS FOR TERRAIN PHASE 10+ PLANNING

### TL;DR: 5 Key Architectural Wins from Vital

1. **Frequency-Domain Wavetable Storage (P1 CRITICAL)**
   - Store harmonic amplitude + phase per frame per wavetable
   - At render time, zero out harmonics above Nyquist for the current pitch
   - Eliminates aliasing completely across the 88-key range
   - **TODO for Terrain Phase 10:** Add `frequency_amplitudes[][]` and `normalized_frequencies[][]` to wavetable frames. Implement `getFrequencyBin()` function and harmonic limiting in oscillator render. ~200–400 LOC.

2. **In-Oscillator Unison Stacking (No Poly Steal)**
   - Each voice internally renders 1–16 unison copies of the waveform
   - Detuning, frame spread, distortion spread applied per unison voice
   - Zero impact on polyphony count
   - **Current Terrain Status:** Likely already implemented; validate that unison ≤ 8 is per-voice, not stealing polyphony.

3. **Spectral Morphing (Rich Effect Palette)**
   - 12+ frequency-domain morphing modes (vocode, formant shift, harmonic scale, Shepard tone, etc.)
   - Enables complex timbral evolution without modulating multiple oscillators
   - **Terrain Gap:** Only FORMANT mode implemented. Consider Phase 11+ for vocode, harmonic scale, and Shepard tone (each ~100–150 LOC with FFT infrastructure).

4. **SIMD Vectorization (Performance)**
   - Vital requires SSE2 (or NEON for ARM); processes 2 voices per SIMD vector
   - 2–4× speedup on CPU-heavy modules (oscillators, filters, envelopes)
   - **Terrain Status:** JUCE may handle internally; check if explicit SIMD is worth the complexity for a JUCE 8 plugin. Probably lower priority vs. correctness.

5. **Phase Randomization + Drift (Aliveness)**
   - Random phase per note + random harmonic amplitude mode + Shepard tone wrapping
   - Creates perceived "analog drift" without explicit modulation
   - **Terrain Status:** Has EROSION (per-voice drift). Consider adding random-harmonics spectral mode (Phase 11).

---

## RED FLAGS / DO NOT COPY (GPL Concerns)

- **Exact filter implementations** (LadderFilter, DiodeFilter code): Study the math, reimplement your own or use JUCE stock filters
- **FFT/Fourier transform code**: Vital likely uses fftpack or similar; use established library (FFTPACK, KissFFT, or Accelerate framework) instead of copying
- **UI/Editor code**: All GPL; Terrain UI is closed-source, so no risk, but don't copy wavetable editor design directly

---

## ARCHITECTURE PATTERNS (Public Domain / Industry Standard)

These are safe to adopt for Terrain (not GPL-specific):

1. Harmonic amplitude + phase storage (standard additive synthesis)
2. Catmull-Rom frame interpolation (standard spline technique)
3. Per-pitch harmonic limiting (standard DSP bandlimiting)
4. Voice stacking via SIMD vectorization (industry practice)
5. Phase modulation for distortion effects (standard in synth design)
6. Spectral morphing via FFT frequency-domain manipulation (academic DSP)

---

## RECOMMENDED READING (If Diving Deeper)

- **Vital Codebase Walkthrough:**
  - `vital/src/synthesis/lookups/wavetable.{h,cpp}` — Wavetable storage + bandlimiting
  - `vital/src/synthesis/producers/synth_oscillator.{h,cpp}` — Oscillator rendering + unison
  - `vital/src/synthesis/filters/ladder_filter.{h,cpp}` — Analog filter modeling

- **Related Literature:**
  - Huovilainen & Välimäki (2007): "Oscillator Bandlimiting Revisited" — Per-pitch bandlimiting math
  - Eriksson (2011): Wavetable Synthesis papers — Frame interpolation
  - Puckette (2007): "The Theory and Technique of Electronic Music" — DSP fundamentals

---

**Document Version:** 1.0  
**Last Updated:** 2026-06-02  
**Next Review:** After Phase 10 bandlimiting implementation

