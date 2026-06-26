# Serum 2 SAMPLE / GRANULAR / SPECTRAL Engine — Deep Technical Reference for Terrain Instrument (Waves Crate)

## TL;DR
- Serum 2 implements Sample, Granular, and Spectral as three *modes of the same oscillator slot* that all read from one loaded audio buffer and share a common control vocabulary (OCT/SEM/FIN/CRS tuning, SCAN, UNISON/DETUNE/BLEND, PAN, LEVEL, two WARP slots, a loop brace with LS/LE + crossfade); they differ only in the playback engine and a handful of mode-specific controls — exactly the shared-buffer architecture you are replicating.
- The most important buildable facts: SCAN is a playback rate/position/direction control (Xfer's product page calls the sample engine's traversal "a Rate control for 'tape stop' effects" — not just a start offset); the dual WARP slots run in series (WARP 1 → WARP 2, with a "Swap Warps" command) and expose the full Serum warp catalog (Sync, Bend, PWM, Asym, Flip, Mirror, Remap 1–4, Quantize, Odd/Even, LPF/HPF, ~14 distortion shapes, and FM/PD/AM/RM from any other source) plus spectral-only warps; and the loop crossfade auto-scales so it never reads outside the sample bounds.
- To "beat Serum," target its documented weak spots: heavy CPU on granular/spectral (forces bounce-to-audio), a granular density that maxes around 30 grains on the density knob (vs. an architectural "up to 256 simultaneous grains"), no per-voice randomization modulation source, a multisample engine that Dubspot calls "the most 'version 1.0' feature in the box," and single-core behavior in some hosts — while matching its strengths (one buffer shared across three engines, warps usable on samples, drawable spectral filter, snap loop detection, X/Y macro pad, filename root-note auto-mapping).

## Key Findings

**Architecture.** Each of the three main oscillator slots (A/B/C) can be set to one of five engines: Wavetable, Multisample (SFZ), Sample, Granular, Spectral. Sample, Granular, and Spectral are time-domain-or-spectral *readers of one loaded audio file*. The official Serum 2 manual's table of contents confirms all three share the same control families: a sample Start/End brace, a Loop Menu, Loop Start/End, Crossfade, Scan, Unison, Warp, Pan, Level, plus "Switching a Sample to a Wavetable." This is why switching a loaded sound between the three engines keeps the same audio — they are alternate front-ends on the same buffer.

**The warp catalog is shared and runs as two serial slots.** Every main oscillator (including Sample/Granular/Spectral) has WARP 1 and WARP 2; a "Swap Warps" command exchanges them, confirming a defined series order (WARP 1 then WARP 2). The secondary "WARP Var"/VAR control re-labels itself per mode.

**SCAN is a rate control.** Xfer's "What's New" wording calls the sample SCAN a "Scan Rate — set the speed and direction of the sample playback," and the Serum 2 product page documents that the sample oscillator "offers ... a Rate control for 'tape stop' effects and similar, sample slicing with realtime score extraction / playback and tails mode." SCAN governs playback traversal speed/direction across the buffer and is fully modulatable — distinct from the static Start point.

## Details

### PART 1 — SAMPLE OSCILLATOR

**Tuning row (top).** OCT (octave, integer steps), SEM (semitone, ±), FIN (fine, cents), CRS (coarse). Serum deliberately splits these into separate automatable/modulatable controls (per the Serum 1 manual rationale: so an LFO can be assigned to octave or coarse independently, e.g. for siren sweeps). Pitch tracking (keytrack) is a per-oscillator toggle; with keytracking on, the sample repitches across the keyboard relative to its root note.

**Root note / keyboard mapping.** The sample's root is set by the OCT/SEM/FIN/CRS controls and, importantly, by **automatic root-note detection from the filename**. Per Xfer Records Support ("Automatic Sample Root Note Mapping"), a note name at the end of the filename (e.g. "Morning Bass F2.flac", "Analog Lead A#4.wav") sets the root note automatically; verbatim: "If no note is specified in the filename, Serum 2 assumes the sample's root note is C3 by default ... Serum 2 uses the convention where MIDI Note 69 = A3 (440Hz)." (Version 2.0.17 added support for flat-note names containing 'b'.) Multisample/key-zone mapping (multiple samples mapped to key zones, velocity layers, round-robin, root keys) is handled by the separate **Multisample (SFZ)** engine, which defines zones, velocity layers and root keys via the human-readable SFZ text format — and per MusicRadar supports far more layers than Pigments' six-layer limit, though it lacks a visual editor.

**Playback / loop modes (the "ONE-SHOT" dropdown).** Confirmed modes include One-Shot, Forward Loop, Reverse (reverse looping), and "more"; the manual's Loop Menu plus the product page's "forward-reverse looping tails for real time stretching" and "tails mode" confirm a tails/release behavior and bidirectional options. The complete verbatim per-mode note-on/off list was not extractable from the official body text and should be confirmed in-app; treat the set as: **One-Shot, Forward Loop, Reverse Loop, Ping-Pong/Bidirectional, Loop-with-release/Tails.** On note-off, loop-with-release/tails modes play a release tail rather than cutting; one-shot plays through ignoring loop points (Serum also has an explicit "deactivate forward arrow → one-shot" behavior inherited from the noise oscillator).

**LS / LE and crossfade.** Loop Start (LS) and Loop End (LE) are set numerically and via the blue loop brace on the waveform display. The Loop Menu has a dedicated "Setting the Crossfade" control that smooths the loop seam; per the official changelog (Serum 2.0.17, 2025-04-23), the **loop crossfade length auto-scales so the crossfade never starts outside the sample bounds.** Right-click options on the waveform expose start-point snapping (incl. zero-crossing / snap loop detection), sample fades, reverse, and trim. LS/LE are modulatable for glitch/scrub effects.

**SCAN.** Speed + direction of playback traversal; supports tape-stop slowdowns and is a modulation destination. Right-clicking SCAN (documented in granular, and analogous in sample) reveals hidden options: reverse scan direction, enable key tracking, and adjust sample length to project tempo.

**UNISON / DETUNE / BLEND / PAN / LEVEL.** Sample mode supports stacked unison voices (up to 16, same as the wavetable engine; the classic "magic number" is 7). DETUNE spreads voice pitch; **BLEND** (default 75%) is the level offset of the unison voices versus the "central" unison voice(s) — effectively a wet/dry between unison (wet) and non-unison (dry) sound, and per the manual is "only applicable when the number of unison voices is greater than two." PAN spreads the stereo image; LEVEL is output gain (Serum gain-scales output down as unison count rises so you aren't fooled by a volume jump).

**Slicing.** Auto (Serum picks transient slices automatically) and Manual (Alt/Option-click to add slices). Slices support real-time "score extraction"/playback and tempo sync, enabling rhythmic chopping of loops.

**Warps on samples.** Both WARP slots work on samples — FM, PD, AM, RM, distortion, filter, etc. (see Part 2). This blurs the line between sampling and synthesis (e.g., FM-ing a loaded vocal chop).

**Format / import.** Drag-and-drop WAV/FLAC (and similar) onto the display, or pick factory samples; "Switch to Wavetable" converts a sample into a wavetable via several import algorithms: dynamic pitch zero-snap (snaps single-cycle captures to zero crossings; best for simple waves), dynamic pitch follow (uses a Melodyne-style pitch map for complex pitch-moving material), constant-frame pitch-average, and FFT resynthesis (analyzes overlapping time slices and resynthesizes frames). Serum streams/embeds sample data in the preset when you choose to embed content on save.

**Formant / pitch-vs-speed.** True pitch-independent time control lives in the Spectral engine (transient-detection timestretch) and in granular scan; the plain Sample engine repitches with playback speed unless warps or the spectral/granular engines are used.

**Modulation.** Effectively every sample parameter is modulatable (Start, LS/LE, SCAN, tuning, unison/detune/blend, pan, level, warp amounts). Sample-relevant mod sources include the NoteOn Random / "Random on Note" source (great for per-note start/scan variation), Note and Velocity curves, 10 LFOs (incl. Lorenz/Rössler chaos and 2D Path modes), 4 envelopes, 8 macros, and — notably — any oscillator (incl. Sub/Noise) as an audio-rate mod source.

### PART 2 — WARP MODES (verbatim from the official Serum 2 manual warp table)

Two warp slots per oscillator, in series, swappable ("Swap Warps"). Modes:

- **Off** — warp disabled.
- **Sync** — synchronizes wavetable playback to an internal oscillator that restarts in phase with the original; WARP sets the internal oscillator's pitch (harmonics shift up while base pitch is retained). A WARP Var fader sets hard→soft sync. *(Phase/timing-domain.)*
- **Bend +** — pinch (bend) the waveform inward toward the cycle middle. **Bend −** — pull outward toward the edges. **Bend +/−** — both, depending on WARP value; 50% = no change. *(Phase-domain / phase-distortion-like reshaping.)*
- **PWM** — push the entire waveform to the left (classic pulse-width sound, esp. square). *(Phase-domain.)*
- **Asym + / − / ±** — bend the *entire* waveform right/left (vs. Bend's per-half action). *(Phase-domain.)*
- **Flip** — instantaneous polarity flip (phase inversion); WARP sets where in the cycle it occurs. *(Amplitude-domain.)*
- **Mirror** — mirror-image the second half of the duty cycle ("octaved" quality; always audible). *(Amplitude/phase reshaping.)*
- **Remap 1** — custom wave-cycle remap via a drawn graph (diagonal y=x = no change); WARP = strength. **Remap 2** — mirrored remap applied to each half independently. **Remap 3** — sinusoidal remap. **Remap 4** — 4× remap (busier). *(Amplitude transfer-function / waveshaping along the phase axis.)*
- **Quantize** — sample-rate reduction applied to the waveform itself, so aliasing follows pitch perfectly (vs. a fixed-pitch SR-redux effect). *(Amplitude/bit-domain.)*
- **Odd/Even** — vertically scale to emit only odd (0%) or only even (100%) harmonics; 50% = original. *(Spectral/amplitude.)*
- **LPF / HPF** — low/high-pass filter at the warp stage. *(Frequency-domain.)*
- **Distortion shapes:** Tube, Soft Clip, Hard Clip, Diode 1, Diode 2, Linear Fold, Sine Fold, Zero-Square, Asym, Rectify, Sine Shaper, Stomp Box, Tape Sat., Soft Sat. *(Amplitude-domain waveshaping.)*
- **FM (from B / C / Noise / Sub / Filter1 / Filter2)** — frequency modulation from another enabled source; plus FM scaling variants **Thru-Zero**, **Exp**, and **Linear** (linear FM is thru-zero but clamped at zero). Note: Serum 2's FM is true modular-style FM, distinct from Serum 1's "FM from B," which was technically a PD mode. *(Phase/frequency-domain.)*
- **PD (from other source, incl. PD (Self))** — phase distortion; like FM but modulating phase. PD (Self) modulates the oscillator with itself (rare in synths). *(Phase-domain.)*
- **AM (from other source)** — amplitude modulation. **RM (from other source)** — ring modulation. *(Amplitude-domain.)*
- **Swap Warps** — exchange WARP 1 and WARP 2.

**Spectral-only warps** (available when the oscillator is in Spectral mode; names below are from reputable reviews/walkthroughs pending in-app verbatim confirmation): harmonic **smear/blur**, **spread partials**, **harmonic boost**, **gate** (mute partials below a threshold), **phase twist**, **formant** shift, **pitch shift**, and **mask/vocode** from another oscillator. *(Frequency/spectral-domain — operate on FFT magnitude/phase, fundamentally different from phase-distortion + time-stretch.)*

**Comparison to your Tones/Beats/Texture time-stretch + phase-distortion warp:** Serum's "warp" is overwhelmingly a *single-cycle waveform reshaping* concept — phase-domain (Bend/PWM/Asym/Sync/PD) and amplitude-domain (Flip/Mirror/Remap/Quantize/Odd-Even/distortion/AM/RM) — **NOT time-stretch.** Time-stretch-like behavior in Serum lives in (a) the Sample engine's SCAN rate + forward-reverse tails, and (b) the Spectral engine's transient-detection resynthesis. So your phase-distortion warps map to Serum's Bend/PWM/Asym/PD family (phase-domain); your time-stretch engines (Tones/Beats/Texture) map to Serum's SCAN + Spectral resynthesis (time/spectral domain) — an area where you can *exceed* Serum by exposing explicit, named stretch algorithms as first-class warp/scan behaviors rather than burying them in the spectral engine.

### PART 3 — GRANULAR OSCILLATOR

Official control set (from the manual ToC): **Scan, Density, Length, Window Amount, Unison, Warp Mode, X|Y Control, Pan, Level, Grain Randomization**, plus the shared sample Start/End, Loop Menu, Loop Start/End, and Crossfade.

- **Scan** — grain source-position/rate traversal across the buffer; right-click for reverse, keytrack, tempo-fit. With keytracking, high notes scan fast and low notes stretch out.
- **Density** — number/spacing of simultaneous grains. Architecturally Serum 2's granular "now allows for up to 256 simultaneous grains" (Splice/Xfer); the DATABROTH review observed the density knob maxing at "30 grains with a length as low as 0 ms with 1 ms resolution" — the 30 is likely the density-knob value while 256 is the simultaneous-grain ceiling; verify in-app.
- **Length** — grain length/size (down to ~0 ms for buzzy/tonal results that spawn new overtones; long for smooth clouds).
- **Window Amount / Skew / Shape** — grain envelope window: choose a window shape and modify it with Amount, Skew, and Shape (sets grain fade/overlap from smooth/overlapping to choppy).
- **Grain Randomization** (the lower knob row visible when Warp is folded) — dedicated randomize/spread dials for playback **offset**, **direction (DIR)**, **pitch**, **length**, **pan**, and **volume**, injecting organic variation per grain/note.
- **Loop modes unique to granular** — **Manual Mode** (adds a draggable dot for grain control) and **Loop Grains** (grain playback respects loop markers).
- **X|Y pad** — right-click the display to turn it into an X/Y macro pad; X = grain scan, Y = a user-chosen destination.
- **Unison + dual Warps** — up to 16 unison voices, and both warp slots apply on top of granular (unusual and powerful; warps can also be randomized).

Real-world: producers use it for lush pads, risers, glitch sequences and vocal-chop clouds; CFA-Sound calls it "a granular gold mine" for cinematic textures. But it is widely flagged as a **CPU hog** — KVR users suspected a granular-specific CPU bug at release ("just switching it on and enabling the warp fx sent my i7 ... from 3% up to 20%"), and many resample/bounce to audio to cope. EDMProd's author preferred Phase Plant's granular interface.

### PART 4 — SPECTRAL OSCILLATOR

Official control set (from the manual ToC): **Scan, Cut, Filter, Mix, Sample High/Low Frequencies, Unison, X|Y Control, Warp Mode, Pan, Level**, plus shared Start/End, Loop Menu, Loop Start/End, Crossfade.

- It performs, per the Xfer product page, "realtime resynthesis of samples at the harmonic level, and transient detection processing similar to that found in advanced timestretching algorithms," giving independent control over time and pitch.
- **Scan** traverses the analyzed spectrum/time; the **Hi/Low Freq brace** narrows the frequency band that gets resynthesized; **Filter** is a drawable spectral filter (custom curve in a popup window, edited like an LFO) to boost/cut individual frequency regions/partials; **Cut** and **Mix** shape and blend the spectral processing; **Timbre Shifting** is adjustable and modulatable.
- It reads the same loaded buffer (and can even ingest a PNG image, Harmor-style) and fills an FFT buffer; the changelog confirms it tracks loop direction ("filling FFT buffer from wrong side of loop when playhead changes direction" was a fixed bug) and respects modulated loop-marker positions.
- Spectral-unique warps (smear/blur, spread partials, harmonic boost, gate, phase twist, formant, pitch shift, mask/vocode) operate on the spectrum.

Real-world: regarded as the standout, rare feature ("could be a powerful synthesizer all on its own," DATABROTH), with "a pleasing blurry quality great for smooth textures and rich harmonic density"; ideal for cinematic pads and stretched-vocal "jungle" effects with pinpoint control over which part is stretched and for how long. Complaints: heaviest CPU of all engines; the diffuse/inaccurate FFT can cause pre-ringing or "messy/random" results on some material.

### PART 5 — SHARED-SOURCE ARCHITECTURE & SUBSYSTEM LINK

All three engines load **one audio buffer** and share: the Start/End brace, Loop Menu + LS/LE, Crossfade, Scan, Unison/Detune/Blend, Pan, Level, two Warps, and "Switch to Wavetable." Switching an oscillator between Sample, Granular, and Spectral keeps the same audio loaded. Loop points and scan are conceptually the same controls, but each engine interprets them in its own domain: Sample = literal playback head and loop region; Granular = grain source region + scan position; Spectral = analysis/resynthesis window over the buffer. Unison/detune/blend behave consistently across all three. The cohesion comes from: an identical control vocabulary across engines, the same drag-and-drop modulation onto any of those knobs, the X/Y pad available on granular and spectral, and warps usable everywhere. This is precisely the model for your shared `SampleBuffer` + per-engine voice front-ends.

### PART 6 — COMPETITIVE / "BEAT SERUM" ANGLE

**Serum 2 complaints to exceed:**
- **CPU.** Granular and spectral are heavy; KVR/Xfer-forum users report stutter and "Audio Processing Disabled," and Serum reportedly uses a single core on an M3 Max in Logic in at least one report. Steve Duda has acknowledged spectral CPU cost is compounded by stereo files, polyphony and unison. This is the #1 opening — a more efficient, multi-threaded granular/spectral engine is a real differentiator.
- **Granular density** is modest vs. dedicated granular tools (density knob ~30), and keytracked grain-rate curves are fiddly to build.
- **No per-voice randomization mod source** — KVR users explicitly requested "per-voice randomness" to randomize a warp/param per unison voice/note; Serum lacks it.
- **Multisample engine** — Dubspot: "the most 'version 1.0' feature in the box," with "a dedicated multisample editor and smoother drag-and-drop" still missing (SFZ text only).
- **Discoverability** — loop-mode list and keytracking behaviors are somewhat hidden, and keytracked filter tuning can be a couple of semitones off, requiring manual correction.

**Serum 2 strengths to match:**
- One buffer shared by three engines with identical controls; warps usable on samples; drawable spectral filter; snap loop detection + auto-scaling loop crossfade; X/Y macro pad on the display; "up to 256 simultaneous grains"; transient-detection spectral resynthesis; dual serial warps with swap; filename root-note auto-mapping.

**How competitors handle it (ideas to steal/surpass):**
- **Vital** — spectral *warp* on wavetables and a sample osc with granular capability, drawable spectral filter, very CPU-light (a complex wavetable patch peaked ~8% in one review); but no dedicated multisample or deep granular engine. Steal: CPU efficiency and near-zero-aliasing oscillators.
- **Arturia Pigments (7)** — separate Sample/Granular engine + Harmonic (additive) engine + a new Modal/physical-modeling engine; up to ~six sample layers; strong visual modulation. Steal: multi-engine layering and modal/physical modeling; surpass its six-layer sample limit.
- **Phase Plant** — fully modular signal path, many oscillators, superb unison; but historically lacks true granular and multisample. Steal: modular routing and unison quality.
- **UVI Falcon** — deep multi-engine (granular, additive, sampler, wavetable) with scripting; surpass with equal depth and a friendlier UI.
- **Ableton Granulator II / Sampler, Output Portal** — Portal's clean macro-driven granular and Granulator's window/spray controls are reference points; surpass with higher grain density, per-grain randomization, and lower CPU.

## Recommendations

1. **Build the shared buffer with three domain front-ends now, as Serum does.** Keep one `SampleBuffer`; expose Start/End, Loop (LS/LE), Crossfade, Scan, Unison/Detune/Blend, Pan, Level as shared controls; let SAMP/GRAN/SPEC reinterpret loop/scan per domain. You already have the SamplerVoice, WarpProcessor (Tones/Beats/Texture), and lock-free shared buffer — wire GRAN and SPEC to read the same buffer and switch engines without reloading audio.
2. **Make SCAN a rate + position + direction control** (not just a start offset), fully modulatable, with right-click options for reverse, keytrack, and tempo-fit — mirror Serum, then exceed it by exposing your Tones/Beats/Texture stretch engines as selectable SCAN/warp behaviors so pitch-independent stretch is a first-class control, not buried in spectral.
3. **Implement loop crossfade that auto-scales to stay in-bounds** (Serum's documented behavior) and add zero-crossing snap loop detection; then beat Serum by also exposing a crossfade *curve* (equal-power vs. linear) and length — Serum exposes only length.
4. **Ship the warp catalog as two serial, swappable slots** covering the phase-domain set (Bend/PWM/Asym/Sync/PD incl. self), the amplitude-domain set (Flip/Mirror/Remap 1–4/Quantize/Odd-Even/distortion/AM/RM), and a *spectral-warp set* (smear/blur/spread/gate/phase-twist/formant/pitch-shift/mask) for the SPEC engine. Label each warp's domain (phase / amplitude / spectral) in the UI — a clarity win over Serum.
5. **Prioritize four differentiators with thresholds:** (a) per-voice and per-grain randomization as a modulation *source* (Serum lacks it entirely); (b) granular density ≥256 simultaneous grains with multi-threaded voicing so a dense pad stays under ~15% CPU on a mid-range CPU (Serum's biggest pain point); (c) formant-preserving, pitch-independent stretch *inside* the SAMP engine (Serum reserves this for Spectral); (d) a visual multisample/key-zone editor with velocity layers and round-robin (Serum's weakest area, SFZ-text-only). Hitting (a)–(d) exceeds Serum on its four most-cited gaps.
6. **Benchmarks that change the plan:** profile granular/spectral CPU per voice early — if it exceeds Serum's, the "beat Serum on CPU" thesis collapses and you should instead differentiate on per-grain randomization and warp depth. If you cannot match transient-detection stretch quality, lean harder on granular + warp differentiation. If your multisample editor slips, ship SFZ import first (matching Serum) and add the visual editor as the headline 1.1 feature.

## Caveats
- The exact verbatim Sample loop-mode dropdown names with per-mode note-on/off behavior, and the exact spectral-warp mode names, could not be extracted from the official manual body within research limits; the warp *table* (Part 2) and all engine control *names* (Scan, Density, Length, Window Amount, Cut, Filter, Mix, Hi/Low Freqs, Crossfade, LS/LE, etc.) are verbatim-official from the Serum 2 manual. Verify the loop-mode list and spectral-warp names in-app before committing UI strings.
- Granular grain-count figures conflict: the official "up to 256 simultaneous grains" (Splice/Xfer) vs. DATABROTH's observed density-knob max of "30 grains with a length as low as 0 ms with 1 ms resolution." These likely measure different things (total simultaneous grains vs. density-knob value); confirm in-app.
- CPU complaints date from the v2.0.x launch window — Serum 2 shipped March 18, 2025 (Dubspot), with the $189 intro price running until June 1, 2025 ($249 thereafter). Xfer has shipped optimizations since (2.0.17–2.0.2x changelogs), so current CPU may differ from the cited forum reports.
- Several behavioral details (right-click SCAN options, X/Y pad, Manual Mode / Loop Grains, slicing modes) are corroborated by reputable third-party tutorials (Production Music Live, DATABROTH, CFA-Sound, Splice) rather than the official manual body text; flagged accordingly.

## Summary Table — Sample-Engine Controls

| Control | Range / units (where known) | Behavior |
|---|---|---|
| OCT | integer octaves | Octave transpose; modulatable separately |
| SEM | ± semitones | Semitone transpose |
| FIN | cents | Fine tune |
| CRS | coarse steps | Coarse tune (separate for automation/mod) |
| Keytrack | on/off | Repitch across keyboard vs. fixed |
| Root note | note name / C3 default | Auto-detected from filename suffix; MIDI 69 = A3 (440 Hz) |
| Loop mode dropdown | One-Shot, Forward Loop, Reverse Loop, Ping-Pong, Loop+Release/Tails | Sets playback/loop behavior on note-on/off/release (verify exact list in-app) |
| LS (Loop Start) | sample position | Loop start; set numerically + blue brace; modulatable |
| LE (Loop End) | sample position | Loop end; modulatable |
| Crossfade | length (auto-scaled in-bounds) | Smooths loop seam; curve not exposed by Serum |
| SCAN | rate + direction | Playback traversal speed/direction (tape-stop); modulatable; right-click reverse/keytrack/tempo-fit |
| UNISON | 1–16 voices | Voice stacking; gain-scaled |
| DETUNE | spread | Pitch spread across unison voices |
| BLEND | 0–100% (default 75) | Wet/dry of unison vs. central voice; only >2 voices |
| PAN | L–R | Stereo spread |
| LEVEL | gain | Output volume |
| WARP 1 / WARP 2 | mode + amount + Var | Two serial warp slots, swappable |
| Slicing | Auto / Manual | Transient slices; Alt-click manual; tempo-synced playback |

## Summary Table — Warp Modes (official Serum 2 table)

| Mode | Domain | Function |
|---|---|---|
| Off | — | Disabled |
| Sync | Phase/timing | Internal osc restarts in phase; WARP = its pitch; Var = hard→soft |
| Bend +/−/± | Phase | Pinch/pull waveform; 50% = no change |
| PWM | Phase | Push waveform left (pulse-width) |
| Asym +/−/± | Phase | Bend entire waveform left/right |
| Flip | Amplitude | Polarity flip at a cycle point |
| Mirror | Amplitude/phase | Mirror 2nd half of cycle (octaved) |
| Remap 1–4 | Amplitude transfer | Drawn remap; 2 = mirrored, 3 = sinusoidal, 4 = 4× |
| Quantize | Bit/amplitude | SR reduction on waveform; aliasing follows pitch |
| Odd/Even | Spectral/amplitude | Emit only odd (0%) or even (100%) harmonics |
| LPF / HPF | Frequency | Low/high-pass at warp stage |
| Distortion (Tube, Soft/Hard Clip, Diode 1/2, Linear Fold, Sine Fold, Zero-Square, Asym, Rectify, Sine Shaper, Stomp Box, Tape Sat., Soft Sat.) | Amplitude | Waveshaping/saturation |
| FM (B/C/Noise/Sub/Filter1/2; Thru-Zero, Exp, Linear) | Phase/freq | Frequency modulation from another source |
| PD (sources incl. Self) | Phase | Phase distortion; Self = self-modulation |
| AM (sources) | Amplitude | Amplitude modulation |
| RM (sources) | Amplitude | Ring modulation |
| Swap Warps | — | Exchange WARP 1 ↔ WARP 2 |
| **Spectral-only** (SPEC engine): smear/blur, spread partials, harmonic boost, gate, phase twist, formant, pitch shift, mask/vocode | Spectral (FFT) | Operate on FFT magnitude/phase (verify exact names in-app) |

## Summary Table — Granular & Spectral Controls

| Engine | Controls |
|---|---|
| **Granular** | Scan (position/rate, right-click reverse/keytrack/tempo), Density (up to 256 simultaneous grains; knob ~30), Length (grain size, ~0 ms+), Window Amount + Skew + Shape (grain envelope), Grain Randomization (offset/DIR/pitch/length/pan/volume), Unison (16), Warp 1/2, X\|Y pad (X=scan), Pan, Level, Loop Menu incl. Manual Mode + Loop Grains, LS/LE, Crossfade |
| **Spectral** | Scan (spectrum/time), Hi/Low Freq brace (resynthesis band), Filter (drawable spectral curve), Cut, Mix, Timbre Shifting, Unison, Warp 1/2 + spectral warps, X\|Y pad, Pan, Level, Loop Menu, LS/LE, Crossfade; transient-detection resynthesis; can ingest PNG images |