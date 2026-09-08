# Convolution — USER IR Loading + Waveform Display (research addendum, 2026-08-09)

Clean-room research for the fb291 follow-up: **drag/Load a user IR + show its waveform**. Fills the gap the
61K `REVERB-BUILD-BIBLE.md` (Convolution section, lines 204-215) did NOT cover — that section nails the
*algorithm* (y = x·h, partitioned FFT, RMS-normalize on load, "draw the baked IR as the space's fingerprint,
every knob reshapes it"). This addendum covers **taming an arbitrary user WAV** and **rendering an IR waveform
so it reads right**. Solo web pass (Max chose focused). Sources cited inline.

## What a proper IR IS (live it)
A room/space IR = the space's fingerprint: an onset **direct spike** → a cluster of discrete **early reflections**
→ a smooth **exponentially-decaying diffuse tail** (the RT60 log-decay). Measured via **exponential sine-sweep
(ESS) + Farina inverse-filter deconvolution** — "a measured IR appears as a straight vertical line followed by
a tail that represents the room reverberation" (Farina 2000; researchgate/scribd). ESS gives ~23% better SNR
than linear sweep + rejects the transducer's non-linearity into a separable pre-response. Anything can be an IR
though — a cab, a metal hit, a vocal chunk (Serum lets you drag ANY sample in) → the convolution imprints that
shape. KEY structural fact for BOTH the DSP and the viz: **the tail sits 40-60 dB below the onset.**

## TAMING A USER WAV — the pipeline (in order)
1. **Decode in memory** — `AudioFormatManager::registerBasicFormats()` + `createReaderFor(MemoryInputStream(...))`
   (WAV/AIFF/FLAC/OGG). Already the house pattern (`SampleLoader.h:184`, `PluginEditor.cpp:2947/12706`). NO disk
   writes ([[feedback-plugin-no-disk-writes-decode-in-memory]]). (Reading a dropped file is allowed; only WRITES
   are sandbox-blocked.)
2. **Sample-rate correct** — resample the IR from its file SR → the HOST SR. If you don't, "the echoes occur at a
   faster or slower rate" and the convolution **amplitude drifts** (LiquidSonics + HISE both confirm amplitude
   changes with SR because the sample-count of h changes). This is the #1 "sound exactly like the source" step.
   It's a **one-time, offline** resample on load → quality >> speed → use a high-order interpolator (windowed-sinc
   / Lagrange), not naive linear. (dsprelated: linear interp = triangular-kernel filter, audible for a fixed asset.)
3. **Onset / leading-silence trim** — many non-pro IRs have leading near-silence → reads as phantom latency
   (Fractal wiki, Gearspace). Auto-trim: measure the abs-peak over the whole IR, set a threshold (~−60 dB of peak),
   scan from the start, first sample past threshold = onset, **back up a few samples**, trim there (29a IR Creator
   method). Our **Pre-Delay** knob then adds *intentional* pre-delay on top of a clean-aligned onset.
4. **Channel handling** — mono → dual-mono (L=R); stereo → L/R. **True-stereo = 4-ch LL/LR/RL/RR** (direct + cross-
   feed; REVerence/ConvologyXT require exactly that order, ~2× CPU). v1: if ≥4-ch, take **ch0 (LL)→L, ch3 (RR)→R**
   (the two direct responses = a sensible stereo IR); full 4-path true-stereo matrix = a future enhancement.
5. **End-fade a truncation** — if the IR is longer than the ~6 s cap (MAXP=576) we hard-trim; a hard cut mid-decay
   = an audible energy "gate." Apply a short **raised-cosine fade-out over the last ~50-100 ms** of whatever we keep.
6. **DC-block** — remove any IR DC offset on load (the engine already DC-blocks the wet, but clean the source too).
7. **Normalize** — the pro method for a user IR is NOT raw peak (≠ level-match) nor raw RMS (makes long reverbs too
   quiet); it's the **peak of a ~200 ms running-RMS**, targeted ~−6 to −12 dB to leave headroom against a hot input
   (KVR "compensate IR volume", sageaudio). Our engine already **energy-normalizes** (Σx²=1 → length-independent
   loudness + bounded peak, fb291) + a wet safety-limiter downstream, which is equivalent-in-spirit and validated —
   KEEP it, and add a **peak-safety clamp** so a pathological hot IR can't spike. (Optional future: a running-RMS
   loudness match for cross-IR consistency; a Serum-style "IR Gain" — we don't have that front knob, energy-norm covers it.)

Then `setUserIR(irL, irR, len)` → the existing bake applies Distance→Density→Decay→Attack→Size→Reverse→normalize
to the user IR exactly as to a synthetic one (`synth()` line 266 already makes a loaded IR the base).

## HOW TO DISPLAY AN IR WAVEFORM (the subtle one)
A **raw LINEAR peak-envelope of an IR is a spike then a near-flat line** — because the tail is 40-60 dB down, so
Decay/Size would barely move the picture. Real IR displays use a **compressed amplitude**: REW's IR view uses a
**dBFS Y-axis = "log-squared" view** precisely because "it makes it much easier to see where the response has
decayed"; Serum's Convolve "WET CHIMES" waveform shows a clearly *visible decaying* tail (not a flatline) → it's
log/energy-compressed, not linear. **DECISION:** compute the linear peak-per-bucket envelope of the **baked** IR
(~200 buckets) in the engine, and in the draw apply a mild perceptual compression — `disp = sign·|peak|^γ` with
γ≈0.45 (√-ish), or a soft-log — so the onset stays a bold spike AND the decaying tail stays visible and **reshapes
with every bake knob** (Size stretches it wider, Decay shortens the tail, Attack fades the front, Reverse flips it,
Distance slides ER↔tail, Density smooths spikes→curve) — satisfies [[feedback-everything-audible-interacts-visually]]
for free. Reuse the noise-engine draw (`drawNoiseWave`, index.html:27692 — flatline baseline + symmetric filled
contour + white stroke). Front-loaded energy = onset spike at the LEFT tapering right = Max's "energy bottom-left,
compact, no dead space." Keep γ tunable in the Safari mockup.

## Reference param sets (the greats — for our knob semantics, NOT to copy)
- **Serum 2 Convolve**: IR library + drag ANY sample to load; **IR Gain** + Mix; **Min-Phase** mode (frontloads
  magnitude energy, snaps transient to sample 0 — a nice future toggle, out of v1 scope).
- **Waves IR-1 / IR-L**: the IR-edit master set = Predelay · Size · Density · Reverb Time (decay) · Damping
  (freq-dependent decay) · Start · Direct/ER/Tail balance — matches our Size/Decay/Attack/Distance/Density/Reverse
  philosophy (bake-offline, real-time path stays pure convolution — the Waves IR-1 "master key").
- **LiquidSonics Reverberate 3 / Seventh Heaven**: modulated true-stereo (4-file), the "Motion" that defeats the
  frozen-IR metallic ring — we already do this (real-time chorus/Motion on the wet).

## Sources
- Serum 2 Convolve behavior: modeaudio.com/magazine/an-introduction-to-serum-2 · lostaud.io drum-envelope-resynthesis
- LiquidSonics true-stereo + SR-amplitude: liquidsonics.com/software/reverberate-3 · .../reverb-processing-topologies
- IR normalization: kvraudio.com/forum viewtopic t=507155 (compensate IR volume) · sageaudio.com/articles/how-to-mix-with-impulse-responses
- IR display (dBFS/log-squared): roomeqwizard.com/help .../graph_impulse.html · .../impulseresponse.html
- True-stereo LL/LR/RL/RR: steinberg REVerence true-stereo · wavearts ConvologyXT · impulserecord.com/true-stereo-4
- ESS/Farina IR measurement: Farina AES (scribd aes122-farina) · researchgate swept-sine technique
- SR resample: gearspace "will IR sample rate matter" · forum.hise.audio convolution-gain-sample-rate · dsprelated linear-interp-resampling
- Onset/leading-silence trim: 29a.ch/impulse-response-creator · wiki.fractalaudio.com Impulse_responses · gearspace latency-using-IRs
