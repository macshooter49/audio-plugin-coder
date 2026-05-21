# Per-Chop Warp Modes — Research Brief

**Date:** 2026-05-20
**Author:** Research agent (Opus 4.7)
**Audience:** Terrain Instrument design lead
**Status:** Research only — no implementation decisions yet

---

## TL;DR

- Ableton ships **6 warp modes**, but only **3 distinct DSP techniques** underneath: (1) transient-aligned granular/slicing with crossfade looping (Beats), (2) granular synthesis with content-aware vs randomized grain selection (Tones, Texture), (3) phase vocoder via licensed **zplane élastique** (Complex, Complex Pro), plus simple resampling (Re-Pitch). Multiple credible sources confirm Ableton licenses zplane élastique for Complex/Complex Pro — Beats/Tones/Texture/Re-Pitch are Ableton-proprietary.
- For a closed-source $49.99 plugin in 2026, the strongest single library is **Signalsmith Stretch** (MIT, header-only, modern spectral algorithm by Geraint Luff, commercial-grade quality, formant control). It covers Tones, Texture, and Complex-class behavior in one engine. Rubberband R3 is technically competitive but **GPL** — commercial licence is **£1,490 GBP** for sub-10-person studios (non-attribution) or **£590 GBP** if you can credit them prominently. Bungee (MPL 2.0) is the dark-horse newcomer but younger and less battle-tested.
- **zplane élastique** is what Ableton, Cubase, FL, Studio One, Reaper, Pro Tools, Bitwig use. Pricing is "contact us" — historically thousands-of-USD upfront plus per-unit. Not realistic for a $49 indie product.
- **Beats mode is the architectural exception** — it's not a stretch library, it's a transient-detected slicer with crossfade loops between transients. Implementable from scratch in JUCE in 1–2 weeks. Should be a Terrain Instrument mode regardless of which stretch lib is chosen.
- **For sampler-context warping**, the question is "decouple pitch from playback speed within a voice render." Existing code already has Re-Pitch (linear-interp resampling). Adding Tones (Signalsmith) + Beats (DIY transient slicer) covers ~80% of realistic content. Texture and Complex Pro are bloat at this scope.
- **Recommended minimum viable scope:** Re-Pitch (already done), Beats (DIY), Tones (Signalsmith). Three modes. Ship. Defer Texture/Complex/formant work to v1.2+.

---

## 1. Ableton's 6 Warp Modes — What Each Actually Does

Ableton's manual organizes warp modes by content type, not by algorithm. Behind the curtain, the 6 modes collapse to ~3 distinct DSP families.

### 1.1 Beats — Transient-aligned slicer with crossfade loops

**Content target:** Drum loops, percussion, EDM, anything with sharp transients on a regular grid.

**Algorithm (publicly described):** Ableton performs transient analysis to identify percussive onsets, then places **warp markers at each transient**. At playback, audio between transients plays at the original speed, and Ableton fills/cuts time gaps by **looping the audio between transients** (forward loop, back-and-forth loop, or off), then applies a **transient envelope** (variable-decay gate) to mask discontinuities. This is essentially time-domain slicing + OLA crossfading at slice boundaries, *not* a stretch algorithm.

**Controls Ableton exposes (verbatim from manual):**
- **Preserve** — what divisions to honor: `Transients` (auto-detected onsets), `1/4`, `1/8`, `1/16`, `1/32` (force fixed-grid cuts even when no transient exists)
- **Transient Loop Mode** — `Loop Off` / `Loop Forward` / `Loop Back-and-Forth`
- **Transient Envelope** — 0 (heavy gate-style fade) to 100 (no fade)

**Artifacts/colorations:** Sounds like a slicer (audible repeats at slow tempos), can chop sustained material into beats-style chunks, "Loop Back-and-Forth" produces signature ping-pong texture popular in IDM/glitch. Excellent at preserving punch on drums; terrible on vocals/pads (chopping artifacts).

**Why this matters for Terrain:** Beats mode is the *only* warp mode that's straightforward to implement from scratch. It's essentially what the existing slicer does, *plus* per-chop time-stretching by looping within each chop. Could be implemented in 1–2 weeks of JUCE DSP work.

### 1.2 Tones — Spectral granular for monophonic pitched content

**Content target:** Vocals (mono), basslines, monophonic instruments.

**Algorithm (publicly described):** Granular synthesis where grain size **adapts to the audio's pitch content** ("the actual grain size determined by the clarity of pitch changes in the audio"). Each grain is windowed and crossfaded with neighbors using OLA. This is closer to PSOLA (Pitch-Synchronous Overlap-Add) — grain boundaries align to estimated pitch periods, which is why it works on monophonic pitched signals but smears on chords. Some sources call this granular; others phase-vocoder-adjacent. Ableton's literature consistently uses "granular synthesis" language.

**Controls:** `Grain Size` (small for distinct pitch variations, larger for noise-resistant but slurred output).

**Artifacts:** Pitchy/warbly on polyphony. Clean on solo vocals if grain size is well-tuned. Can produce "robotic" formant artifacts at large pitch shifts.

### 1.3 Texture — Randomized granular for non-pitched/polyphonic material

**Content target:** Pads, drones, polyphonic orchestral, ambient.

**Algorithm:** Granular synthesis like Tones, but **grain size is decoupled from audio content** (purely user-controlled) and there's an additional **Flux** parameter that randomizes grain size/position. This is creative-tool territory — it's expected to color the source. At low Flux it behaves like a basic granular stretcher; at high Flux it becomes a sound-design effect.

**Controls:** `Grain Size`, `Fluctuation` (randomness).

**Artifacts:** Smearing on transients (no transient detection), audible repeats at high grain sizes (which is presented as a feature, not a bug — "the highest grain settings leads to audible snippet repeats, which could be cool!").

### 1.4 Re-Pitch — Resampling (no decoupling)

**Content target:** Anything where you *want* the tape-machine/turntable effect.

**Algorithm:** Linear (or in production tools, polyphase / sinc) resampling. Speed up = pitch up. This is exactly what Terrain Instrument's current pitchSemitones implementation does.

**Controls:** None ("transposition controls are deactivated because changing the playback speed directly affects the pitch").

**Artifacts:** Aliasing if quality is low; otherwise totally clean. The trade-off is just that pitch and speed are linked.

### 1.5 Complex — Multiband phase vocoder via zplane élastique Efficient

**Content target:** Full songs, anything with mixed beats + tones + textures.

**Algorithm:** Phase vocoder. Multiple sources confirm Ableton licenses **zplane élastique** for Complex and Complex Pro. Wide Blue Sound's 2025 DAW survey states: *"Complex and Complex Pro warp modes are powered by zplane élastique Pro"* (other modes are proprietary Ableton). KVR/Ableton forum threads from élastique developers consistently describe élastique as having three engines: **élastique Efficient** (multiband phase vocoder, lower CPU), **élastique Pro** (higher-quality multiband PV), and **élastique SOLOIST** (monophonic-optimized).

**Controls:** None exposed in Ableton beyond per-clip transpose.

**CPU cost:** "Complex Mode uses around 10 times more CPU resources than the other warp modes" (Ableton forum / documentation lore). This is consistent with the multiband phase vocoder workload.

**Artifacts:** Phasiness, transient smearing on bass drums (the classic phase-vocoder failure mode). Fine on full-mix material at modest stretch factors.

### 1.6 Complex Pro — Multiband phase vocoder with formant control

**Algorithm:** Higher-quality variant of Complex. Specifically adds formant preservation, which suggests it uses spectral envelope estimation (e.g., LPC or cepstral lifter) to separate excitation from spectral envelope, transpose only the excitation, then reapply the envelope. Vendor-documented as a *"variation of the algorithm found in Complex mode"*.

**Controls (verbatim):**
- **Formants** — 0–100%. At 100% original formants preserved during pitch transposition. *"This control has no effect if the sample's transposition is not changed."*
- **Envelope** — default 128. *"For high-pitched samples, lower Envelope values may provide better results, while low-pitched samples may sound better with higher values."* This controls the spectral envelope estimation window/resolution.

**No "Gender" parameter** despite common confusion — Complex Pro exposes Formants + Envelope only.

**Artifacts:** Same family as Complex but cleaner, especially on vocals at non-trivial pitch shifts. Still phase-vocoder family — bass transients still smear.

### Summary table — Ableton modes → algorithms

| Mode         | Family                          | Engine               | CPU   | Best for                  |
|--------------|---------------------------------|----------------------|-------|---------------------------|
| Re-Pitch     | Resample                        | Proprietary          | ~0    | Tape FX, intentional pitch+speed link |
| Beats        | Transient-sliced + loop fill    | Proprietary          | Low   | Drums, rhythm loops       |
| Tones        | Pitch-adaptive granular (PSOLA-ish) | Proprietary       | Low-mid | Mono vocals, bass, leads |
| Texture      | Randomized granular             | Proprietary          | Low-mid | Pads, drones, sound design |
| Complex      | Multiband phase vocoder         | zplane élastique Efficient | 10x   | Full mixes, songs   |
| Complex Pro  | Multiband PV + formant correction | zplane élastique Pro | 10–15x | Vocals, full mixes, pitch-critical |

---

## 2. DSP Techniques in Scope

Confirmation/correction of the user's hypotheses, expanded:

### Beats → transient-detected granular with crossfaded grains aligned to transients
**Confirmed and correct.** But "granular" overstates it — these are slice-sized chunks (50ms–2s), not grains (typically 10–100ms). Implementation:
- Onset detection (spectral flux, complex spectral difference, or HFC-based)
- Place markers at onsets
- Between markers, audio plays at original speed
- When stretch ratio requires more time than the slice provides, **loop within slice** (forward or back-and-forth, choosing zero-crossing midpoints to avoid clicks)
- When stretch ratio requires less time, **truncate slice and crossfade to next**
- Apply transient envelope (decaying gate) to mask boundaries

CPU: very low. JUCE has all primitives (AudioBuffer, smoothing, transient detection via spectral flux). 1–2 weeks for a competent dev.

### Tones → phase vocoder, monophonic-friendly
**Partially correct.** Ableton's docs explicitly frame Tones as **granular** with pitch-adaptive grain sizing. This is essentially PSOLA (Pitch-Synchronous Overlap-Add) territory — grain boundaries align to estimated pitch period, which makes monophonic pitched audio work. Phase vocoder is a different approach (frequency-domain). Both can produce similar perceptual results on solo vocals, but PSOLA is typically lower CPU and lower latency. A modern phase vocoder library like Signalsmith Stretch can emulate Tones-mode behavior with much better quality than classical PSOLA.

### Texture → phase vocoder with grain randomization / chaos
**Mostly wrong.** Ableton's Texture is explicitly granular ("audio's tonal characteristics are not taken into consideration when the grain size is adjusted"). Grain size is fixed-by-user (not pitch-adaptive), and **Flux** randomizes it. This is classical granular synthesis, not phase vocoder. A grain engine (you already have GrainEngine.h!) can emulate Texture mode directly — just expose grain size + randomization params.

### Re-Pitch → simple resample
**Confirmed.** Already done in Terrain Instrument.

### Complex → multi-band phase vocoder
**Confirmed.** This is zplane élastique Efficient. To DIY: STFT with overlap (75% common), per-band phase propagation, phase-locking around spectral peaks (Laroche-Dolson 1999), iSTFT with OLA. The "multiband" part means dividing the spectrum into ~3–8 bands and handling phase coherence independently per band. Substantial work — weeks-to-months of DIY DSP for production quality.

### Complex Pro → multi-band PV with formant correction
**Confirmed.** Adds spectral envelope estimation (LPC or cepstral) to preserve formants across pitch shifts. Signalsmith Stretch has formant controls built in (`setFormantFactor`, `setFormantBase`).

### CPU cost ordering (cheapest → most expensive)
1. **Re-Pitch** (resample) — trivial
2. **Beats** (slicer + crossfade) — very low
3. **Texture** (granular with random) — low
4. **Tones** (pitch-adaptive granular / PSOLA) — low-mid
5. **Complex** (multiband PV) — high
6. **Complex Pro** (multiband PV + formant correction) — highest

A single Signalsmith Stretch instance is roughly Complex-Pro-tier CPU, but it covers Tones/Texture/Complex behavior via parameter tuning. **One library, multiple "modes" via configuration** is the modern indie pattern.

### Key DSP reference papers worth knowing
- Laroche & Dolson 1999, *"Improved phase vocoder time-scale modification of audio"* — foundational, introduces phase locking around peaks. Bell Labs lineage.
- Laroche & Dolson 1999, *"New phase-vocoder techniques for pitch-shifting, harmonizing and other exotic effects"* — the formant-preservation method most libraries derive from.
- Duxbury, Davies, Sandler 2002 — transient detection + phase reset for impulsive material.
- Bernsee blog post on stretching/pitch — practical implementer-friendly overview.
- Luff 2023 (Signalsmith), *"The Design of Signalsmith Stretch"* + ADC22 talk *"Four Ways To Write A Pitch-Shifter"* — current state-of-the-art for spectral approaches.

---

## 3. Library Landscape (2026)

### Signalsmith Stretch — RECOMMENDED
- **License:** MIT (commercial-friendly, no royalties, no attribution requirement)
- **Author:** Geraint Luff (Signalsmith Audio)
- **Technique:** Modern phase vocoder variant with non-linear frequency mapping and dual horizontal/vertical phase coherence. Does pitch + time *together* in spectral domain (most libs chain stretch → resample).
- **Quality:** Community consensus puts it at-or-above Rubberband R3 quality on most material. Excellent for moderate stretch (0.75x–1.5x) and any pitch shift up to several octaves. Cleaner on polyphonic material than time-domain libs (SoundTouch, etc).
- **CPU:** Mid-range. Roughly 50–100% faster than reference librosa phase vocoder. Per-voice cost is realistic for ~8–16 voices on Apple Silicon.
- **Integration:** **Header-only C++11.** Drop `signalsmith-stretch.h` into project, instantiate `SignalsmithStretch<float>`, call `presetDefault(channels, sampleRate)`, feed buffers to `process()`. Optional FFT backend acceleration (`SIGNALSMITH_USE_ACCELERATE` on macOS, IPP on Windows, PFFFT cross-platform).
- **Real-time:** Yes, with `splitComputation` flag for tight DSP threads. Latency is ~one block size (~5–20ms typical at 48kHz).
- **Formant controls:** Yes — `setFormantFactor()`, `setFormantBase()`. Not as sharp as monophonic PSOLA but usable for sampler context.
- **Limitations:** Time stretches beyond 2x use a known hack ("vertical phase scaling capped at 2x"). For sampler chops at musical pitch ranges this is irrelevant.
- **Pricing:** Free.
- **Production examples:** Used in HISE, OpenMPT (in progress), multiple JUCE plugins.

### Rubberband (Breakfast Quay, Chris Cannam)
- **License:** GPLv2+ (open source) OR commercial:
  - **£590 GBP** Standard Licence — must credit Breakfast Quay prominently in the app
  - **£1,490 GBP** Non-Attribution Licence (Small Publishers, <10 employees) — no attribution required
  - **£9,320 GBP** Non-Attribution for larger companies
  - One-time fee, no royalties, no per-product limits
- **Technique:** R3 engine (Finer, default since v4.0 Oct 2024) is a multiband phase vocoder with sophisticated phase reset on transients. R2 (Faster) is older + faster.
- **Quality:** R3 is genuinely commercial-grade. Excellent on polyphonic mixes, vocals with soft onsets, bass-heavy material. Strong reputation as the open-source standard.
- **CPU:** R3 is ~3x R2. Higher CPU than Signalsmith for similar quality on most material per community comparisons.
- **Integration:** CMake-friendly, builds as static or shared lib. Not header-only. Used as the time-stretch engine in Mixxx, Ardour, Tracktion Waveform, Cubase has it as an option.
- **Real-time:** Yes (`OptionProcessRealTime`). Latency higher than Signalsmith.
- **Formant controls:** Yes (`OptionFormantPreserved`).
- **For a $49 plugin:** £590 attribution license is technically affordable. £1,490 non-attribution is the realistic floor for a clean commercial integration. Compare to MIT alternatives below.

### zplane élastique (zplane.development)
- **License:** Commercial only, pricing on request. Historically reported in the thousands USD + per-unit royalty.
- **Technique:** Industry standard. Pro = high-quality multiband phase vocoder, Efficient = lower CPU variant, SOLOIST = monophonic optimized.
- **Quality:** The gold standard — what Ableton, Cubase, FL, Studio One, Reaper, Pro Tools, Bitwig all license.
- **CPU:** Pro is the most expensive. Efficient is competitive with Rubberband R3.
- **Integration:** Static library, well-documented SDK.
- **For a $49 plugin:** Not realistic. Pricing typically excludes solo dev / low-volume titles. Skip.

### Bungee (Parabola Research)
- **License:** Open Bungee = **MPL 2.0** (commercial-friendly, file-level copyleft, works fine for closed-source plugins). Bungee Pro = commercial, contact for pricing.
- **Technique:** "Adaptive phase vocoder" — implementation details closed. Supports negative/zero playback speed (rare).
- **Quality:** Newer (public release early 2024). Community feedback on KVR generally positive but less battle-tested than Rubberband/Signalsmith.
- **CPU:** Pro version advertised as "optimized for realtime usage on consumer devices with very low CPU".
- **Integration:** C++ API, CMake. Cross-platform (Windows/macOS/Linux/iOS/Android, plus a web build).
- **Real-time:** Yes, designed for it.
- **For a $49 plugin:** Worth evaluating as a Signalsmith alternative if you hit edge cases Signalsmith doesn't handle. MPL 2.0 is a clean license.

### SoundTouch (Olli Parviainen)
- **License:** LGPL v2.1. For a *static-linked* closed-source plugin, LGPL technically requires dynamic linking or supplying object files — annoying. Possible to ship as a dylib but adds packaging complexity. There are workarounds but the license is the friction point.
- **Technique:** WSOLA (Waveform-Similarity Overlap-Add) — time-domain technique.
- **Quality:** Acceptable for solo monophonic material. Audible warbling on polyphonic content. Latency ~100ms.
- **CPU:** Very low (time-domain).
- **Integration:** JUCE module wrapper exists (eyalamirmusic/JUCE_SoundTouch).
- **For a $49 plugin:** Dated. Quality below modern alternatives. Skip unless you specifically want a vintage WSOLA character (like TAL-Sampler's intentionally gritty algorithms).

### Dirac LE / Dirac Pro (Stephan Bernsee, originally The DSP Dimension)
- **License:** Commercial. Was acquired/discontinued — Bernsee moved to Zynaptiq.
- **Status:** Effectively dead as a separately licensable library. Zynaptiq products use the tech internally. Skip.

### Custom phase vocoder in JUCE (DIY)
- **License:** Free (your code).
- **Technique:** Whatever you write. Reference: Laroche-Dolson 1999.
- **Quality:** Whatever you can engineer in the time budget. A naive PV is doable in 2–3 days; a production-quality one with phase locking, transient handling, and formant preservation is 2–6 months. Real risk of artifacts that pre-built libs have already solved.
- **CPU:** Whatever you achieve.
- **Integration:** Native JUCE FFT (`juce::dsp::FFT`).
- **For a $49 plugin:** **Not recommended.** You're competing against decades of refined work. Signalsmith Stretch gives you essentially all of this for free under MIT.

### Other 2024–2026 entrants worth knowing
- **ML-based stretchers** (e.g., Meta's Demucs lineage, various startup APIs): Not real-time for in-plugin use yet on consumer CPUs. Cloud-only or offline. Not viable for a sampler voice engine.
- **HISE built-in stretcher**: Uses Signalsmith Stretch under the hood (confirmed by HISE forum threads). Not separately licensable.
- **Tracktion Engine TimeStretcher**: Wraps multiple backends (Rubberband, Elastique, SoundTouch). License inherits from chosen backend. Useful if you're already in Tracktion's framework.

### Recommendation for Terrain Instrument
**Signalsmith Stretch** is the right pick for a $49.99 closed-source plugin:
- MIT license — clean, no attribution, no royalties
- Header-only — trivial CMake integration (just add to includes)
- Modern algorithm — competitive with élastique on most material
- Real-time capable per voice
- Active maintenance (2023+, Geraint Luff is well-respected in audio DSP)
- Macros for Apple Accelerate (faster on arm64 — matches your macOS focus)

Combined with a DIY Beats-mode slicer (for drum chops, where Signalsmith would smear transients), you cover the realistic use cases. Use the existing GrainEngine.h foundation for an optional "Texture" creative mode if you want it later.

---

## 4. Sampler Context vs Clip Context Warping

### Clip warping (Ableton's case)
A clip has a fixed source audio file with a known original tempo. The DAW knows the project tempo and computes a stretch ratio per playhead position. Warp markers allow non-uniform stretch ratios across the clip. Pitch is set separately and decoupled (in modes 2.1–2.4). The warp algorithm renders the clip's contribution to the master output at the host sample rate.

### Sampler-context warping (what Terrain Instrument needs)
A chop is defined by `[startSample, endSample]` in a source buffer. When MIDI triggers a voice:
- **Current behavior:** voice reads source buffer with playhead increment = `2^(pitchSemitones/12)`. Speed and pitch are linked (Re-Pitch only).
- **Desired warp behavior:** voice plays the chop at a *target duration* with an *independent pitch*. So a chop of length 1 second, played at +12 semitones, should still take 1 second (or some user-controlled tempo-locked duration).

**Mapping clip-warp → sampler-warp:**
- The "clip tempo" maps to "chop natural duration"
- The "project tempo" maps to either (a) a fixed-stretch ratio user-controls or (b) host BPM with chop duration locked to bars/beats (e.g., "this chop plays for 1 bar regardless of pitch")
- Pitch in the sampler is the MIDI note offset from the root note + the chop's `pitchSemitones`
- Time stretch ratio = (target chop duration) / (source chop duration in samples / sample rate)
- Pitch ratio = `2^(semitones/12)` applied *independently* via the warp algorithm

**Key implementation detail:** Most stretch libraries expect a continuous input stream. In a sampler, each voice has its own bounded chop region and its own stretch parameters. Two options:
1. **Per-voice library instance** — instantiate one Signalsmith Stretch per voice (16 voices = 16 instances). Each voice feeds its own chop sample range through its own instance. Highest CPU but cleanest. This is what Kontakt does for Time Machine Pro.
2. **Pre-stretched chop cache** — when a chop's pitch/duration is set in the UI (not at voice trigger), pre-process the chop once with the stretcher and cache the result. Voices just play back the cached version with linear interp + envelope. Lowest CPU at trigger time, but requires re-render on parameter change and uses more RAM.

For Terrain Instrument with real-time per-voice pitch changes via MIDI, **option 1 is the right architecture** despite the CPU cost. This is how Kontakt's Time Machine modes work.

### Precedents — how other samplers handle this

**Kontakt — Time Machine modes:**
- TM Standard, TM Pro, Beat Machine, Tone Machine
- TM Pro is the high-quality option (analogous to Complex Pro)
- "Per-zone" setting: each zone has its own tempo metadata and the engine stretches per voice
- Voice limit is set independently because TM Pro is CPU-heavy
- Same architecture pattern recommended for Terrain Instrument

**Logic Pro Sampler — Flex modes:**
- Monophonic (PSOLA-like) — has a "Percussive" toggle for plucked strings
- Polyphonic (phase vocoder) — has "Complex" toggle for more transients
- Slicing — pure transient slicer, no time compression
- Rhythmic, Tempophone, Speed — varieties for specific material
- Per-track setting in Logic, not per-zone (different from Kontakt). For a sampler instrument like Terrain, per-chop is the right granularity.

**Native Instruments Maschine — Stretch + Pitch tabs:**
- Time-stretch and pitch-shift can be applied independently
- Recently added real-time stretching as an audio plugin device
- Maschine MK3/Plus has STRETCH as a destructive operation, MK4 added non-destructive per-pad warp

**Battery 4:** Uses Time Machine internally per cell. Same per-voice model.

**TAL-Sampler:** Intentionally uses gritty Akai-style time-stretch as a feature. Different aesthetic — relevant if Terrain wants a "lo-fi" warp mode as a colorful option.

**Serato Sample:** Per-slice warp with quality presets. Closer architectural match to Terrain Instrument than Kontakt is.

### What changes in sampler context vs clip context
1. **No warp markers** — chops are uniform stretch regions, not segments with per-marker ratios. Simpler.
2. **MIDI-driven pitch** — pitch changes happen at note-on, not via clip transpose. Library needs to handle pitch parameter changes at voice instantiation time. Signalsmith Stretch handles this with `seek()` + reset.
3. **Voice lifecycle** — voices start, render N samples, then release. Library instances must be cheap to reset. Per-voice cost matters.
4. **Latency tolerance** — sampler voices triggered from MIDI must have very low latency (<10ms ideal). This favors libraries with small block sizes. Signalsmith's `splitComputation` flag is useful here.
5. **Polyphony** — 8–16 simultaneous stretching voices on consumer CPUs. Means the algorithm needs to be cheap per voice. Signalsmith with Accelerate backend is realistic. Rubberband R3 is borderline.

---

## 5. Recommended Scope (Opinionated)

### Minimum viable mode set (ship in v1.0 warp release)

1. **Re-Pitch** (already implemented) — keep as default for backward compatibility. Don't break existing users' chops.
2. **Beats** — DIY transient slicer + crossfade loop. Use the existing slicer infrastructure (chop boundaries are already transients in most user workflows). Add an internal "loop within chop to fill time" mechanism with a transient envelope decay control. This is the killer feature for drum chops.
3. **Tones** — Signalsmith Stretch integration with formant preservation enabled. Covers vocals, bass, lead-instrument chops.

Three modes. UI fits cleanly into a dropdown or radio set per chop. Each mode is genuinely useful and addresses a different content type. CPU budget is realistic for 8–16 voice polyphony on Apple Silicon.

### Right "stretch" of ambition (one notch beyond MVP)

4. **Texture** — same Signalsmith Stretch backend with different parameter mapping (larger grain-equivalent setting, optional jitter/randomization in the C++ wrapper). Re-uses the same library instance. Low marginal cost. Adds creative-tool credibility for ambient/sound-design users.

Four modes is the sweet spot. Matches the natural taxonomy (rhythmic / tonal / textural / vari-speed) without adding library count.

### Feature bloat (defer or skip)

- **Complex / Complex Pro equivalent** — Signalsmith Stretch already covers full-mix material adequately. A separate "Complex" mode would be redundant marketing surface. The Tones mode at appropriate settings handles full mixes.
- **Formant slider on Tones** — Signalsmith exposes formant factor as a single number. UI cost is low, but in a sampler context with per-chop pitch up to ±24 semitones, formant correction matters less than in a clip-warp context (where users transpose entire vocals). Add only if a beta tester explicitly asks.
- **Multiple stretch quality tiers** ("Eco / Standard / Pro" like élastique) — unnecessary for a single-engine setup. Pick one quality preset.
- **Per-voice background pre-rendering** — premature optimization. Run the stretcher per voice in real-time. If CPU becomes a problem at v1.5, then optimize.
- **Custom warp markers within a chop** — that's clip warping. Out of scope for a sampler. Slice the chop further instead.

### Implementation order
1. Add Signalsmith Stretch as a header-only dependency. Stand up one stretching voice. Verify it works with the existing voice lifecycle. (3–5 days)
2. Plumb Tones mode through VoiceConfig + UI. Ship as opt-in alongside Re-Pitch. (2–3 days)
3. Implement Beats slicer + crossfade-loop renderer. Test against drum-loop chops. (1–2 weeks)
4. Add Texture mode as a Tones variant with different param mapping. (2–3 days)
5. Voice-management: set polyphony cap for stretched voices, add CPU warning if needed. (3–5 days)

Total: ~3–4 weeks for production-quality 4-mode per-chop warp.

### Watch out for
- **Per-voice Signalsmith instances** can blow up CPU if all 16 voices use Tones simultaneously. Profile early, consider a voice-stealing strategy where the *N* most-recently-triggered voices use Tones and overflow falls back to Re-Pitch.
- **Stretch ratio range** — Signalsmith warns 0.75x–1.5x is best. Beyond ±50% time stretch, quality degrades. For a sampler this is usually fine (chops play near original speed when pitched up/down at MIDI rates), but if you expose a "chop duration" lock that forces extreme ratios, set UI limits.
- **Pitch range** — Signalsmith handles multiple octaves. ±24 semitones is comfortable.
- **CMake integration** — Signalsmith Stretch is header-only but ships with optional FFT backends. On macOS/arm64 enable `SIGNALSMITH_USE_ACCELERATE` for ~2x speedup using vDSP.
- **Voice reset on note-on** — call `reset()` and `seek()` to position the stretcher at the chop's start sample. Otherwise tail of previous voice will bleed.

---

## Sources

### Ableton warp modes / official docs
- [Ableton Reference Manual — Audio Clips, Tempo, and Warping (v12)](https://www.ableton.com/en/manual/audio-clips-tempo-and-warping/)
- [PCAudioLabs — Beats Warp Mode](https://pcaudiolabs.com/how-to-use-beats-warp-mode-in-ableton-live/)
- [PCAudioLabs — Tones Warp Mode](https://pcaudiolabs.com/ableton-live-warping-part-4-how-to-use-tones-warp-mode-in-ableton-live/)
- [PCAudioLabs — Texture Warp Mode](https://pcaudiolabs.com/ableton-live-warping-part-5-how-to-use-texture-warp-mode-in-ableton-live/)
- [PCAudioLabs — Complex Pro Warp Mode](https://pcaudiolabs.com/ableton-live-warping-part-8-how-to-use-complex-pro-warp-mode-in-ableton-live/)
- [Sound on Sound — Ableton Live: Warping Revisited](https://www.soundonsound.com/techniques/ableton-live-warping-revisited)
- [Audeobox — How to Warp Audio in Ableton (every mode)](https://www.audeobox.com/learn/ableton/how-to-warp-audio-in-ableton/)
- [Wide Blue Sound — Top DAWs and Their Time-Stretch Algorithms (2025)](https://www.widebluesound.com/blog/top-daws-and-their-time%E2%80%91stretch-algorithms-2025/)

### DSP papers + technical background
- [Laroche & Dolson — Improved phase vocoder time-scale modification](https://www.semanticscholar.org/paper/Improved-phase-vocoder-time-scale-modification-of-Laroche-Dolson/8312d42cab3f14152d8e6406a9c0463737b6aa45)
- [Laroche & Dolson — New phase-vocoder techniques for pitch-shifting](https://www.ee.columbia.edu/~dpwe/papers/LaroD99-pvoc.pdf)
- [HAL — Transient detection and preservation in the phase vocoder](https://hal.science/hal-01161125/document)
- [Bernsee — Time Stretching And Pitch Shifting of Audio Signals Overview](http://blogs.zynaptiq.com/bernsee/time-pitch-overview/)
- [ESOLA paper — Epoch-Synchronous Overlap-Add](https://arxiv.org/pdf/1801.06492)

### Signalsmith Stretch
- [GitHub — Signalsmith-Audio/signalsmith-stretch](https://github.com/Signalsmith-Audio/signalsmith-stretch)
- [Signalsmith Audio — Stretch code page](https://signalsmith-audio.co.uk/code/stretch/)
- [Signalsmith Audio — The Design of Signalsmith Stretch (2023)](https://signalsmith-audio.co.uk/writing/2023/stretch-design/)
- [PracticeSession — Signalsmith Stretch Deep Dive](https://www.practicesession.app/blog/signalsmith-stretch-deep-dive/)
- [JUCE forum — Using SignalsmithStretch inside JUCE](https://forum.juce.com/t/using-signalsmithstretch-inside-juce/65437)
- [KVR — timestretch/pitchshift rubberband vs signalsmith comparison](https://www.kvraudio.com/forum/viewtopic.php?t=623537)

### Rubberband
- [Breakfast Quay — Rubber Band Library](https://breakfastquay.com/rubberband/)
- [Breakfast Quay — Commercial license tiers](https://breakfastquay.com/technology/license.html)
- [Mixxx forum — Rubberband R3 huge leap in quality](https://mixxx.discourse.group/t/rubberband-r3-rubberband-better-huge-leap-in-quality/32794)
- [The Breakfast Post — Performance improvements in Rubber Band Library](https://thebreakfastpost.com/2022/09/30/performance-improvements-in-rubber-band-library/)

### zplane élastique
- [zplane Licensing](https://licensing.zplane.de/)
- [zplane ELASTIQUE PRO 3.3.7 SDK manual (PDF)](https://licensing.zplane.de/uploads/SDK/ELASTIQUE-PRO/V3/manual/elastique_pro_v3_sdk_documentation.pdf)
- [Ableton Forum — Elastique vs Elastique Pro discussion](https://forum.ableton.com/viewtopic.php?t=96984)

### Bungee
- [Bungee Audio Technology](https://bungee.parabolaresearch.com/)
- [GitHub — bungee-audio-stretch/bungee](https://github.com/bungee-audio-stretch/bungee)
- [KVR — bungee audio time stretcher discussion](https://www.kvraudio.com/forum/viewtopic.php?t=607012)

### SoundTouch / other libs
- [SoundTouch library README](https://www.surina.net/soundtouch/README.html)
- [GitHub — JUCE_SoundTouch module](https://github.com/eyalamirmusic/JUCE_SoundTouch)
- [JUCE forum — Looking for low-latency timestretch lib](https://forum.juce.com/t/looking-for-a-timestretching-pitchshifting-library-that-has-very-low-latency/45404)

### Sampler precedents (Kontakt, Logic, Maschine)
- [Native Instruments — Kontakt Time Machine Pro on VI-Control](https://vi-control.net/community/threads/kontakt-time-machine-pro.154226/)
- [ADSR — Back To The Future With Time Machine Pro](https://www.adsrsounds.com/kontakt-tutorials/back-to-the-future-with-time-machine-pro/)
- [Apple Support — Flex Time algorithms and parameters](https://support.apple.com/guide/logicpro-ipad/flex-time-algorithms-and-parameters-lpipab631a74/ipados)
- [Native Instruments — Maschine MK3 manual (sampling and mapping)](https://www.native-instruments.com/ni-tech-manuals/maschine-mk3-manual/en/sampling-and-sample-mapping)
- [CDM — Maschine real-time timestretching announcement](https://djmag.com/news/maschine-finally-gets-real-time-timestretching-watch)

### Other notable plugins
- [SynthAnatomy — Baby Audio Warp (Transit 2 algorithm)](https://synthanatomy.com/2024/12/baby-audio-warp-free-pitch-shifter-time-stretch-fusion-effect-plugin-based-on-transit-2.html)
- [JUCE forum — A lightweight Akai-style time-stretch algorithm](https://forum.juce.com/t/a-lightweight-akai-style-time-stretch-algorithm-realtime/60514)
