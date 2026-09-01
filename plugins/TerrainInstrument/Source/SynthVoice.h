// SynthVoice.h — Terrain Instrument synth section, Phase 1 (MPV)
// One PolyBLEP saw oscillator → juce::dsp::LadderFilter (LPF24) → juce::ADSR.
// Mirrors the SamplerVoice.h pattern (header-only). 8 voices allocated by
// PluginProcessor against a single SynthSound sentinel.
#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include "Wavetable.h"
#include "TerrainFilters.h"
#include "Shapers.h"          // fb313 — shared waveshapers (tw::shapers): the fold + its ADAA antiderivative,
                              // one copy for the oscillator path AND the FX-rack Distortion FOLD family.
#include "TerrainEnvelope.h"
#include "SynthModConfig.h"   // Batch 1 — per-voice LFOs + mod routing (namespace wc)
#include "FlowRobin.h"        // fb122 — the ROBIN Wheel rotation brain (no-JUCE)
#include "SampleEngine.h"          // SAMPLE-ENGINE-VOICE — per-OSC sample playback core
#include "SampleBuffer.h"          // SAMPLE-ENGINE-VOICE — shared lock-free buffer
#include "GranularEngine.h"        // GRANULAR-ENGINE-VOICE — per-OSC granular core
#include "GeodeEngine.h"           // GEODE-ENGINE-VOICE — per-OSC resynthesis core (Engine::SPEC)
#include "HarmonicEngine.h"       // HARMONIC-ENGINE-VOICE — per-OSC additive bank (Engine::HARM)
#include "ModalEngine.h"          // MODAL-ENGINE-VOICE — per-OSC physical model (Engine::MODAL)
#include "SubOsc.h"               // SUB — voice-anchored sub oscillator (universal osc box)
#include "Warp/WarpProcessor.h"    // SAMPLE-ENGINE-VOICE — STRETCH + FORMANT (Signalsmith Tones)
#include <atomic>
#include <array>
#include <cmath>
#include <cstdint>

namespace tw
{
    /** Sentinel sound — accepts every MIDI note and channel so the
     *  Synthesiser will always dispatch to SynthVoice. */
    class SynthSound : public juce::SynthesiserSound
    {
    public:
        bool appliesToNote    (int /*midiNoteNumber*/) override { return true; }
        bool appliesToChannel (int /*midiChannel*/)    override { return true; }
    };

    /** One synth voice — Phase 1 MPV.
     *  PolyBLEP saw oscillator → AMP ADSR → pan.
     *  Subsequent phases (per Design/v1-syn-spec.md) add filter,
     *  more engines, filter envelope, cross-mod, FLOW glide, etc. */
    class SynthVoice : public juce::SynthesiserVoice
    {
    public:
        SynthVoice() = default;

        /** Phase 3 — OSC engine choice. Order matches the SYN_OSC_A_ENGINE
         *  StringArray in createParameterLayout: WT, SAMP, GRAN, SPEC, FM, HARM (slot 5
         *  was the never-exposed NOISE engine — ID frozen, meaning remapped to HARMONIC). */
        enum class Engine : int { WT = 0, SAMP = 1, GRAN = 2, SPEC = 3, FM = 4, HARM = 5, MODAL = 6 };

        static constexpr int kMaxUnison = 16;   // Serum-parity unison ceiling (was 8)

        // ══ fb523 · THE FM LAW — HZ OF DEVIATION (was: radians of index) ═════════════════════
        //  [M] The reference measures Δf = 88,480 Hz × depth⁴, PITCH-INDEPENDENT IN HZ to ±0.4 %
        //  over depth 0.2–0.7 at both C1 and C3, with the aliasing onset it predicts
        //  ((24000/88480)^¼ = 0.7215) confirmed between measured depth 0.70 (clean) and 0.80
        //  (aliased) ON BOTH NOTES. Ours measured β = 5.3094(e^{2d}−1), IDENTICAL at C1 and C3
        //  to 0.4 % — a constant INDEX, which is the property being inverted here.
        //
        //  kFmDeviationHz — the ceiling. 96,000 Hz is 8.50 % (+0.71 dB) ABOVE the reference's
        //  measured 88,480 Hz, i.e. 4× the 24 kHz Nyquist of a 48 kHz session. β at 100 %:
        //  733.9 at C3 (ref 676) and 2,935 at C1 (ref's extrapolated 2,705). We match the
        //  ceiling and beat it, on both notes.
        //  ⚠️ An Hz law is also SAMPLE-RATE independent by construction: at a 192 kHz session
        //  rate 96 kHz of deviation only just reaches Nyquist, so the top of the knob is
        //  correspondingly less destructive there. That is the reference's behaviour too.
        //
        //  kFmTaperRate — the taper, ln(361) = 5.888878. depth' = (e^{5.888878·d} − 1)/360,
        //  i.e. Δf = 96000·(361^d − 1)/360. Chosen against three requirements:
        //   · NO DEAD ZONE. A raw 4th power (the reference's law) has T'(0) = 0 — its first
        //     10 % of travel buys Δf = 8.8 Hz, β = 0.068 at C3, inaudible. Ours gives
        //     T(0.1) = 0.0022277 → Δf = 213.9 Hz → β = 1.63 at C3 / 6.54 at C1. Audible from
        //     the first 1 % of travel (d = 0.01 → Δf = 16.18 Hz → β = 0.49 at C1).
        //   · NO PLATEAU. T'(1)/T'(0) = 361; the curve is steepest at the top and T(1) = 1
        //     exactly. Every knob unit buys the same RATIO of deviation — the same law the
        //     shipped URANGE knob uses (cents = 5·960^t).
        //   · MUSICAL VALUES IN THE LOW HALF. T(0.5) = 1/(√361 + 1) = 0.05 exactly, i.e.
        //     Δf = 4,800 Hz = β 36.7 at C3 / 146.8 at C1. The whole classic-FM range
        //     (β 1…40 at C3) lives in the bottom half of the knob; the top quarter is the
        //     destructive region — Δf crosses Nyquist (24 kHz) at d = ln(91)/ln(361) = 0.7660.
        //  ALIASING CURVE (out-of-harmonic energy, square carrier, C3). Taken from the
        //  reference's own MEASURED off-grid sweep re-expressed as a function of DEVIATION
        //  (which is carrier/Nyquist physics, not a plugin property) and read back through
        //  our taper: 1 % at d = 0.491, 2 % at d = 0.588, 5 % at 0.744, 10 % at 0.791,
        //  20 % at 0.843, and the reference's own 100 % condition (73 % off-grid) at 0.986.
        //  On a SINE carrier there is no measurable off-grid energy at all below Δf ≈ 24 kHz
        //  (d = 0.766). So the knob is clean through its musical half, crosses the 2 % budget
        //  just past centre on a rich carrier, and its top quarter is deliberately unusable.
        //  ⚠️ THE TAPER IS PER-MODE and lives in setBlendSlot(); PD/AM/RM keep the house
        //     exp-bias curve (e^{2d}−1)/(e²−1) untouched.
        static constexpr float kFmDeviationHz = 96000.0f;   // peak deviation at depth = 1, per slot
        static constexpr float kFmTaperRate   = 5.888878f;  // ln(361) — see above
        static constexpr float kFmTaperNorm   = 1.0f / 360.0f;   // 1/(361−1)
        static constexpr float kFmLeak        = 0.9997f;    // UNCHANGED: leaky integrator, corner 2.29 Hz @48k
        //  THE SOFT BOUND, RE-DERIVED — it did not matter before and it does now.
        //  Excursion in CYCLES is Δf/(2π·f_mod), which under an Hz law EXPLODES as f_mod falls
        //  (under the old index law it was pitch-independent and topped out at 5.4 cycles, so
        //  the shipped ±16/±32 knee never engaged at all — measured β 33.95 vs a 50.27 wall).
        //   · At full deviation the excursion is 116.80 cycles at C3 and 467.20 at C1.
        //   · The bound is EXACTLY LINEAR (bit-identical, softBound returns x unchanged) for
        //     every modulator at or above f_mod = 96000/(2π·512) = 29.842 Hz at 100 % depth —
        //     i.e. across the entire keyboard from B0 (30.87 Hz) up, at every depth. It is
        //     therefore inaudible in the musical range by construction, not by taste.
        //   · Below that it is a tanh knee, asymptotic to ±1024 cycles. Two things it stops:
        //     (a) a sub-audio LFO source (src 7..16), whose DC excursion the leak alone caps at
        //         (Δf/fs)/(1−kFmLeak) = 6,667 cycles at 48 kHz — 6.5× past the bound;
        //     (b) float precision: fmPhase_ is a float added to a cycle-domain read phase, so at
        //         1024 cycles the ULP is 6.1e-5 cycles = 1/8 of a sample of a 2048-point table.
        //         At the leak's own 6,667-cycle ceiling it would be 4.9e-4 = 1 whole sample.
        //  ── PD / AM / RM — fb524 OVERPASS ───────────────────────────────────────────────
        //  kPdCycles 2.20 -> 8.50. The 2.20 was set by a brief that stopped where out-of-harmonic
        //  energy crossed 2 % — a safety net, and one Max has explicitly forbidden. Measured, the
        //  law is exactly linear in this constant: 2.20 gave β 13.82 rad = 6.283 rad per unit, so
        //  8.50 gives β 53.41 rad = 8.50 cycles of phase excursion. The reference measures 50.43
        //  rad, so this is +5.9 % PAST it — the same posture as FM's +8.50 %.
        //  WHERE IT GOES DESTRUCTIVE: the 2 % out-of-harmonic point sits at 2.226 cycles, which on
        //  the 361:1 taper is d = 0.774. Below that the knob is clean, above it is foldback, and
        //  foldback IS the product at the top (Lifeguard clause 2).
        //  EXPOSURE: a phase modulator stops meaning anything once its peak phase RATE exceeds
        //  half a sample, i.e. E = fs/(4π·f_mod) cycles — 38.2 at a 100 Hz modulator, 9.5 at
        //  400 Hz, 3.8 at 1 kHz. 8.50 delivers 22 % / 89 % / >100 % of that across the musical
        //  modulator range. There is no single number; this is the honest one.
        static constexpr float kPdCycles  = 8.50f;    // peak phase excursion in CYCLES at depth 1
        //  kAmIndex 2.0 -> 10.0. 2.0 was PARITY (peak ×2.999 vs the reference's ×3.007) and parity
        //  is not the target. The LAW is untouched — y = x·(1 + k·mod) holds the carrier at
        //  -0.000 dBc by construction because x·mod has no component at f0. Only the range moves.
        //  EXPOSURE: AM's mechanism is a carrier that is still THERE alongside its sidebands. The
        //  carrier/sideband ratio is 2/k, so k = 10 puts the carrier 14.0 dB under each sideband;
        //  by k ≈ 20 it is 20 dB down and the mode has become RM — a duplicate of mode 4, which is
        //  where the mechanism stops meaning anything. 10.0 of a useful 20.0 = 50 % exposure, and
        //  3.33× the reference. The gain factor swings [-9, +11]; blendAmp's softBound is linear
        //  to ±8 and bends above it, so the extreme corner compresses 11.0 -> 10.87 (-0.10 dB).
        static constexpr float kAmIndex   = 10.0f;    // AM modulation index at depth 1
        //  RM — kRmLevel STAYS AT 2.0 (see the algebra note at the call site). kRmModDrive is the
        //  new dimension: MODULATOR DRIVE, normalised by its own peak so it costs no level at all.
        //  [M] sine modulator, wsTanh drive, peak measured at EXACTLY 1.0000 at every setting:
        //      gd     0      2      4      6      8     10     12     16     24     48
        //      THD  0.00  17.19  30.95  36.47  39.33  41.08  42.26  43.75  45.26  46.78 %
        //      bw99  188    562    938   1312   1312   1688   1688   2062   2812   3938 Hz
        //  An ideal square is 48.34 %. 24.0 puts the driven modulator at 45.26 % = 93.6 % of a
        //  square in THD and 2,812 Hz = 15.0x the undriven modulator's bandwidth. EXPOSURE against
        //  gd = 48 (the point past which the clip transition is narrower than the ear can tell
        //  apart from an instantaneous one at any musical modulator pitch): 96.8 % on THD, 71 % on
        //  bw99. ⚠️ THE RING LAW IS UNTOUCHED — this drives the MODULATOR, so our bright 1/m
        //  product survives and is added to; the reference's darker 1/m^2 law is NOT adopted,
        //  because Max has not chosen between them and this line must not choose for him.
        static constexpr float kRmLevel    = 2.0f;
        static constexpr float kRmModDrive = 24.0f;

        static constexpr float kFmBoundLin = 512.0f;    // cycles — linear (inert) below this
        static constexpr float kFmBoundMax = 1024.0f;   // cycles — tanh asymptote
        //  ── fb551 · OVERPASS ITEM 7 — THE TWO MISSING FM LAWS ───────────────────────────────
        //  The reference ships THREE FM curves [M2 pp.55-56] and we shipped one. Mode 1 is the
        //  THRU-ZERO one: a deviation in Hz added to the read rate, free to drive the instantaneous
        //  frequency negative, at which point the carrier reverses and keeps oscillating.
        //
        //  · MODE 9 · FM EXP — the deviation is a RATIO, not a number of Hz: f = f_c·2^(N·d·mod).
        //    ⚠️ THIS MODE IS PITCH-PROPORTIONAL ON PURPOSE AND IT IS THE ONLY ONE THAT IS. fb523
        //    deliberately deleted repInc[] from mode 1's line to kill a pitch-proportional law
        //    there, and left a warning not to reintroduce it "anywhere". This is the one exemption
        //    and it is not a loophole: exponential FM is DEFINED in octaves — a 1 V/oct law — so
        //    its deviation must scale with the carrier or it is not exponential FM. That is why the
        //    carrier's rate comes back under its own name (blendCarrInc_) and why modes 9 and 10
        //    are the ONLY readers of it. Mode 1's line is untouched and stays Hz-absolute.
        //    Octaves SUM across slots (2^a·2^b = 2^(a+b)) — correct, and one exp per carrier per
        //    sample instead of one per slot.
        //    kFmExpOctaves = 10: at C3 the UPWARD peak is f_c·(2^10 − 1) = 133.8 kHz of deviation,
        //    39 % past mode 1's 96 kHz ceiling, while the DOWNWARD peak is bounded at −f_c — the
        //    frequency approaches zero but never crosses it. That asymmetry IS the mode, and it is
        //    what the manual means by "small changes in the modulator's amplitude cause dramatic
        //    changes … brighter or harsher".
        //    ⚠️ NO PITCH DRIFT, AND IT IS NOT LUCK. The mean of 2^(k·mod) over a bipolar modulator
        //    is > 1, so exponential FM classically runs SHARP. Our leaky integrator (kFmLeak,
        //    corner 2.29 Hz) turns a constant frequency offset into a constant PHASE offset, so
        //    the sharpening is removed for free by machinery that was already there. Anyone who
        //    "fixes" kFmLeak to a true integrator will make this mode drift.
        //
        //  · MODE 10 · FM CLAMP — linear FM that is NOT thru-zero: the total instantaneous rate is
        //    clamped at zero, so the carrier can stall but never reverses. Same kFmDeviationHz
        //    ceiling and the same 361:1 taper as mode 1; the clamp is the ONLY difference and it is
        //    the whole mode. It half-wave rectifies the frequency excursion, and that asymmetry is
        //    what splits the carrier — the measured signature of the reference's own FML.
        //
        //  🚨 NEITHER MODE FEEDS THE MIP PICKER, AND THAT IS DELIBERATE — read this before "fixing"
        //  it under fb545/fb550's rate law. Mode 1 does not feed it either: its aliasing above
        //  d ≈ 0.59 is a MEASURED, REFERENCE-MATCHED property (see the aliasing curve above — the
        //  reference's own 100 % condition is 73 % off-grid), not an oversight. And the exposure is
        //  the same order in all three: at C3 mode 1's full-depth read rate is 735× the carrier and
        //  mode 9's is 2^10 = 1024×. Dulling 9 and 10 while 1 stays bright would make the clamp
        //  mode darker than its own sibling for a reason that has nothing to do with the clamp.
        //  ── fb552 · THE FOLLOWERS (OVERPASS item 9A) — audio as a modulation source ──────────
        //  The amplitude envelope of osc A-D and of the noise, handed to the matrix as ordinary
        //  sources. The taps already existed: modPrev_[] and noiseModTap_ are the PRE-GAIN,
        //  one-sample-delayed outputs the blend slots read, which is why a follower keeps working
        //  with its source's Level at 0 (modSrcForce_/noiseForce_ make it render anyway).
        //
        //  🔑 PEAK DETECTOR, NOT A MEAN ONE, and the reason is ripple. |sin| has a DC term plus a
        //  ripple at 2·f0; a symmetric one-pole fast enough to catch a pluck (τ ≈ 3 ms, corner
        //  53 Hz) passes that ripple wholesale at any bass note, and the "envelope" then wobbles at
        //  twice the pitch. An INSTANT attack with a slow release holds the peak across the cycle,
        //  so the output is flat for every f0 above ~1/kFollowReleaseMs and the transient is still
        //  exact. It also fixes the scale for free: a full-scale oscillator reads exactly 1.0, with
        //  no fudge factor to go stale when the tables change.
        //  kFollowReleaseMs is therefore the ONE number that matters: it is the fastest decay the
        //  follower can express, and the slowest pitch it can hold without rippling. 25 ms holds
        //  everything above ~40 Hz and still lets a pluck read as a pluck.
        static constexpr double kFollowReleaseMs = 25.0;
        //  ── fb553 · BLEND MODE 6 `Warp` — TRUE AUDIO-RATE MODULATION (OVERPASS item 9B, and
        //  item 8 with it) ───────────────────────────────────────────────────────────────────
        //  9A gave the matrix a source's ENVELOPE. 9B is the source's own SAMPLE, per sample —
        //  and the honest reading of that is not "a new kind of route". Our block-staged matrix
        //  cannot deliver a per-sample value to 487 destinations, and it should not try: on the
        //  480 that are block-rate an audio-rate value is either inaudible or noise, and a route
        //  that silently does nothing is worse than one that does not exist.
        //  So 9B goes where per-sample modulation ALREADY LIVES — the blend slot, which is exactly
        //  a {source, depth} pair evaluated every sample. FM/PD spend it on phase and AM/RM on
        //  amplitude; mode 6 spends it on the WARP AMOUNT.
        //
        //  🔑 WHY THIS IS BIGGER THAN IT LOOKS, and why it closes item 8 as well. "Warp amount" is
        //  one knob with 37 different meanings: modes 9-34 are SHAPERS (so this is audio-rate
        //  distortion drive — item 8's `Dist`), modes 35/36 are the per-osc TPT filter (so this is
        //  per-oscillator FILTER FM — item 8's `Filter`), and mode 37 is the curve you drew. One
        //  blend mode delivers all three, with no new parameter anywhere, which is what the
        //  OVERPASS meant by "the modulatable version".
        //
        //  BOTH WARP SLOTS, on purpose. The pill says "this source drives my warp", and an osc has
        //  two warp slots; driving only slot 1 would make the feature work or not depending on
        //  which slot the shaper happens to sit in.
        //
        //  ⚠️ WT AND FM ENGINES ONLY. SAMP/GRAN/SPEC/HARM/MODAL overwrite their output from block
        //  buffers and never evaluate a per-sample warp amount at all, so there is nothing to
        //  modulate there. Not a limitation of this mode — a fact about those engines.
        //
        //  🚨 IT MUST WIDEN THE MIP, and fb545/fb550 are why. The mip level is chosen ONCE PER
        //  BLOCK from the warp amount; sweeping that amount at audio rate makes the instantaneous
        //  read rate exceed what the chosen mip is band-limited for, and the table's top octave
        //  aliases. blendWarpMax_ carries the worst-case swing into warpFan(), and it TRACKS DEPTH
        //  so depth 0 asks for exactly the base rate — fb550's floor bug, avoided by construction.
        //  kWarpModDepth = 1.0: a full-scale modulator at full depth sweeps the whole 0..1 knob.
        static constexpr float kWarpModDepth = 1.0f;
        static constexpr float kFmExpOctaves = 10.0f;   // mode 9 — peak pitch excursion in OCTAVES at depth 1
        static constexpr float kFmExpOctLin  = 10.0f;   // softBound: exactly linear to here …
        //  🚫 THE CLAMP IS A HARD CORNER AND IT STAYS ONE — this is a road already walked, so do not
        //  walk it again. A corner in instantaneous frequency has infinite bandwidth, so a C¹ knee
        //  (linear until the total rate falls to a quarter of the carrier's, then an exponential
        //  approach to 0⁺) was built and A/B'd against the corner on a stationary tone. IT MADE NO
        //  MEASURABLE DIFFERENCE: alias floor at knob 0.3/0.5/0.7/1.0 read −115/−110/−102/−94 dBc
        //  with the knee and −112/−114/−108/−94 without it — the same numbers inside the harness's
        //  own spread. So the corner stays, because it is literally what the mode is called and what
        //  the reference's own description says ("a clamp at zero"), and a softened clamp would be a
        //  quieter definition bought with nothing.
        //  ⚠️ The reading that ORIGINALLY argued for the knee (−71.4 dBc) was a measurement artefact:
        //  the first spectrum after a big parameter change carries the previous setting's tail. Every
        //  number above is taken on a settled state, with the row before it discarded.
        static constexpr float kFmExpOctMax  = 16.0f;   // … tanh asymptote past it. A modulator tap sitting on
                                                        // its own ±4 clamp (self-feedback into a hot block engine)
                                                        // would otherwise ask for 2^40; a HARD clamp there would
                                                        // click, so it gets the same knee every other bound uses.

        bool canPlaySound (juce::SynthesiserSound* s) override
        {
            return dynamic_cast<SynthSound*> (s) != nullptr;
        }

        void setCurrentPlaybackSampleRate (double sr) override
        {
            juce::SynthesiserVoice::setCurrentPlaybackSampleRate (sr);
            sampleRate_ = (sr > 0.0) ? sr : 48000.0;
            invSampleRate_ = (float) (1.0 / sampleRate_);   // fb523 — FM is Hz-of-deviation now: Δcycles/sample = Δf/fs
            followRelCoef_ = 1.0f - std::exp (-1.0f / (float) (kFollowReleaseMs * 0.001 * sampleRate_));   // fb552
            noiseSR_ = (float) sampleRate_;   // NOISE engine Hz-based math (hum/wind/rumble/SVF)
            ampEnv_.prepare (sampleRate_);
            ampEnv_.setMinRelease (0.005);   // fb297 — 5ms declick FLOOR on the AMP env only: release=0 stays tight
                                             // but the VCA ramps sustain→0 instead of stepping = no note-off click.
            for (auto& de : dynEnv_) de.prepare (sampleRate_);   // fb177 — dynamic pool
            fltEnvT_.prepare (sampleRate_);
            pitchEnvT_.prepare (sampleRate_);
            mod1EnvT_.prepare (sampleRate_);
            mod2EnvT_.prepare (sampleRate_);
            // SAMPLE-ENGINE-VOICE — prepare per-OSC sample engines + warp processors
            for (auto& e : sampleEngA_) e.prepare (sampleRate_);  for (auto& e : sampleEngB_) e.prepare (sampleRate_);
            for (auto& e : sampleEngC_) e.prepare (sampleRate_);  for (auto& e : sampleEngD_) e.prepare (sampleRate_);
            // GRANULAR-ENGINE-VOICE — prepare per-OSC granular engines
            for (auto& e : granEngA_) e.prepare (sampleRate_);  for (auto& e : granEngB_) e.prepare (sampleRate_);
            for (auto& e : granEngC_) e.prepare (sampleRate_);  for (auto& e : granEngD_) e.prepare (sampleRate_);
            for (auto& e : geodeEngA_) e.prepare (sampleRate_);  for (auto& e : geodeEngB_) e.prepare (sampleRate_);   // GEODE-ENGINE-VOICE
            for (auto& e : geodeEngC_) e.prepare (sampleRate_);  for (auto& e : geodeEngD_) e.prepare (sampleRate_);
            // fb517 — HARM IS NO LONGER PREPARED HERE (the fb498 modal cut, cloned). The four
            // banks assign ~9 vectors x kMaxPartials floats per engine x 16 unison x 96 voices
            // (~65 MB per instance, laneD-measured) in the PROCESSOR'S CONSTRUCTOR, paid by
            // every patch that never selects HARM. prepareHarmonicEnginesIfNeeded() (message
            // thread) arms them the first time an osc asks for Engine::HARM; dropping the flag
            // here is what makes a sample-rate change re-prepare.
            harmReady_.store (false, std::memory_order_release);   // HARMONIC-ENGINE-VOICE
            // fb498 — MODAL IS NO LONGER PREPARED HERE. This function is reached from
            // juce::Synthesiser::addVoice (juce_Synthesiser.cpp:117), i.e. from the PROCESSOR'S
            // CONSTRUCTOR loop (PluginProcessor.cpp:274), so preparing all four osc arrays here
            // allocated the waveguide delay lines for every unison slot of every osc of all 96
            // voices before the host had even called prepareToPlay:
            //     96 voices x 4 oscs x 16 unison x 6 lines x 8192 floats x 4 B = 1,152 MiB,
            // measured as 68% of this plugin's entire 1,694 MB peak working set on Windows — and
            // paid in full by every patch that never selects MODAL at all (the default is WT).
            // Preparation now happens on the MESSAGE THREAD in prepareModalEnginesIfNeeded(),
            // only once an osc actually asks for Engine::MODAL. Dropping the flag here is what
            // makes a SAMPLE-RATE CHANGE re-prepare: the lines are rate-independent in SIZE but
            // ModalEngine::prepare() also recomputes rate-dependent coefficients.
            modalReady_.store (false, std::memory_order_release);   // MODAL-ENGINE-VOICE
            sampleWarpA_.prepare (sampleRate_, 2, 1024); sampleWarpB_.prepare (sampleRate_, 2, 1024);
            sampleWarpC_.prepare (sampleRate_, 2, 1024); sampleWarpD_.prepare (sampleRate_, 2, 1024);
            airHpCoef_ = 1.0f - std::exp (-2.0f * juce::MathConstants<float>::pi * 3500.0f / (float) juce::jmax (1.0, sampleRate_));
            oscGateCoef_ = 1.0f - std::exp (-1.0f / (0.004f * (float) juce::jmax (1.0, sampleRate_)));  // ~4ms mute fade — click-free
            lvlSmCoef_ = 1.0f - std::exp (-1.0f / (0.0025f * (float) juce::jmax (1.0, sampleRate_)));   // fb180 — level glide
        }

        /** Set AMP envelope params. attackMs/decayMs/releaseMs are milliseconds;
         *  sustain is 0..1. Called from PluginProcessor each block. */
        void setAmpEnvelopeParameters (float attackMs, float decayMs,
                                       float sustain, float releaseMs) noexcept
        {
            // Back-compat 4-arg shim (no delay/hold/curve) — still callable.
            setEnvelopeDAHDSR (ampEnv_, 0.0f, attackMs, 0.0f, decayMs,
                               sustain, releaseMs, 0.0f, 0.0f, 0.0f, false);
        }

        /** Full DAHDSR setter for any of the five envelopes. Times in ms, sustain
         *  0..1, curves -1..+1. EFFECTIVE values (owner pre-sums modulation). */
        static void setEnvelopeDAHDSR (terrain::TerrainEnvelope& env,
                                       float delayMs, float attackMs, float holdMs,
                                       float decayMs, float sustain, float releaseMs,
                                       float curveA, float curveD, float curveR,
                                       bool loop) noexcept
        {
            env.setDelay   (juce::jmax (0.0f, delayMs)   * 0.001f);
            env.setAttack  (juce::jmax (0.0f, attackMs)  * 0.001f);
            env.setHold    (juce::jmax (0.0f, holdMs)    * 0.001f);
            env.setDecay   (juce::jmax (0.0f, decayMs)   * 0.001f);
            env.setSustain (juce::jlimit (0.0f, 1.0f, sustain));
            env.setRelease (juce::jmax (0.0f, releaseMs) * 0.001f);
            env.setAttackCurve  (juce::jlimit (-1.0f, 1.0f, curveA));
            env.setDecayCurve   (juce::jlimit (-1.0f, 1.0f, curveD));
            env.setReleaseCurve (juce::jlimit (-1.0f, 1.0f, curveR));
            env.setLoop (loop);
        }

        /** PITCH envelope depth in semitones (bipolar). env(0..1) × depth is summed
         *  into the oscillator pitch each block. 0 = off (preset-safe default). */
        void setPitchEnvDepth (float semis) noexcept { pitchEnvDepth_ = juce::jlimit (-48.0f, 48.0f, semis); }

        /** Per-envelope DAHDSR broadcasts from the processor (EFFECTIVE values). */
        void setAmpEnv (float dl,float a,float h,float d,float s,float r,
                        float ca,float cd,float cr,bool lp) noexcept
        { setEnvelopeDAHDSR (ampEnv_, dl,a,h,d,s,r,ca,cd,cr,lp); }

        /** fb177 — dynamic envelope pool (Env 6..32). Count + per-slot DAHDSR
            arrive from the processor's blob broadcast (no APVTS params). */
        static constexpr int kMaxDynEnvs = 27;
        void setDynEnvCount (int n) noexcept { dynEnvCount_ = juce::jlimit (0, kMaxDynEnvs, n); }
        void setDynEnvDAHDSR (int k, float dl,float a,float h,float d,float s,float r,
                              float ca,float cd,float cr,bool lp) noexcept
        { if (k >= 0 && k < kMaxDynEnvs) setEnvelopeDAHDSR (dynEnv_[k], dl,a,h,d,s,r,ca,cd,cr,lp); }

        /** fb178/fb179 — envelope value for a mod-matrix source. KNOB-IS-THE-PEAK
            semantics (Max's law): the matrix sees level−1, so at the envelope's PEAK
            the parameter equals the knob and everywhere below it follows the shape
            DOWN by depth×scale. A pluck on a maxed volume knob just WORKS — the old
            additive-up read as "does nothing" (clamped at the ceiling). */
        float envSourceValue (int sI) const noexcept
        {
            double lv = 0.0;
            switch ((wc::ModSource) sI)
            {
                case wc::ModSource::EnvAmp:    lv = ampEnv_.level();   break;
                case wc::ModSource::EnvFilter: lv = fltEnvT_.level();  break;
                case wc::ModSource::EnvPitch:  lv = pitchEnvT_.level();break;
                case wc::ModSource::EnvMod1:   lv = mod1EnvT_.level(); break;
                case wc::ModSource::EnvMod2:   lv = mod2EnvT_.level(); break;
                default:
                {
                    const int k = sI - (int) wc::ModSource::EnvD1;
                    if (k >= 0 && k < kMaxDynEnvs) lv = dynEnv_[k].level();
                    break;
                }
            }
            return (float) lv - 1.0f;
        }

        /** fb183 — RAW envelope level 0..1 for OWNERSHIP dests (Level): the shape
            itself. envSourceValue() keeps the knob-is-the-peak delta for offset dests. */
        float envSourceRaw01 (int sI) const noexcept { return envSourceValue (sI) + 1.0f; }

        /** fb552 — a follower's raw 0..1 level (the taps clamp at ±4, so this clamps at 1). */
        float followValue01 (int k) const noexcept
        { return (k >= 0 && k < wc::kNumFollowers) ? juce::jlimit (0.0f, 1.0f, follow_[k]) : 0.0f; }
        /** fb552 — and its ROUTING value, which is level−1 for exactly the reason an envelope's is:
            the knob is the peak, so a loud source sits AT the knob and a quiet one pulls below it. */
        float followSourceValue (int sI) const noexcept
        { return followValue01 (wc::followIndexOf (sI)) - 1.0f; }

        // ── Envelope follower taps (for the UI playhead dot) ──
        // Live amp-env output [0,1] and whether this voice is sounding. The editor
        // polls the most-active voice each timer tick and pushes this to the WebUI.
        float getAmpEnvLevel() const noexcept { return (float) ampEnv_.level(); }
        /** fb555 — the voice's KEY POSITION, 0 at kKtLowNote and 1 at kKtHighNote, latched at
            note-on. Deliberately the SAME ramp the KEYTRACK feature already uses (ktRamp_) rather
            than a second normalisation: "Note" and "keytrack" have to mean the same thing. */
        float getKeyRamp01() const noexcept { return ktRamp_; }
        float getCurrentVelocity() const noexcept { return currentVelocity_; }   // fb262 — most-active-voice velocity for the live streak viz
        bool  isAmpEnvActive() const noexcept { return ampEnv_.isActive(); }
        float dbgWarpEffA() const noexcept { return warpAmount_; }   // fb188 — probe tap
        float dbgLvlSm (int g) const noexcept   // fb183 — probe tap: the glided per-voice level
        { switch (g) { case 0: return lvlSmA_; case 1: return lvlSmB_; case 2: return lvlSmC_; default: return lvlSmD_; } }
        // SAMPLE-FOLLOWER — per-osc sample read position [0,1] for the UI MIDI follower,
        // or -1 if that oscillator isn't a sounding Sample engine. osc: 0=A,1=B,2=C,3=D.
        float sampleFollowPos01 (int osc) const noexcept
        {
            switch (osc)
            {
                case 0: return (engine_  == Engine::SAMP && sampleEngA_[0].isActive()) ? (float) sampleEngA_[0].position01() : -1.f;
                case 1: return (engineB_ == Engine::SAMP && sampleEngB_[0].isActive()) ? (float) sampleEngB_[0].position01() : -1.f;
                case 2: return (engineC_ == Engine::SAMP && sampleEngC_[0].isActive()) ? (float) sampleEngC_[0].position01() : -1.f;
                case 3: return (engineD_ == Engine::SAMP && sampleEngD_[0].isActive()) ? (float) sampleEngD_[0].position01() : -1.f;
                default: return -1.f;
            }
        }
        // GRANULAR-FOLLOWER — most-active voice's grain cloud for osc; fills pos[]/age[] (0..1),
        // returns grain count. 0 when that osc isn't a sounding Granular engine.
        int granCloudSnapshot (int osc, float* pos, float* age, int maxN) const noexcept
        {
            const std::array<tw::GranularEngine, kMaxUnison>* eng = nullptr;
            switch (osc)
            {
                case 0: if (engine_  == Engine::GRAN) eng = &granEngA_; break;
                case 1: if (engineB_ == Engine::GRAN) eng = &granEngB_; break;
                case 2: if (engineC_ == Engine::GRAN) eng = &granEngC_; break;
                case 3: if (engineD_ == Engine::GRAN) eng = &granEngD_; break;
                default: break;
            }
            if (eng == nullptr || ! (*eng)[0].isActive()) return 0;
            tw::GrainViz buf[16];
            const int cap = (maxN < 16) ? maxN : 16;
            const int n = (*eng)[0].cloudSnapshot (buf, cap);
            for (int i = 0; i < n; ++i) { pos[i] = buf[i].pos01; age[i] = buf[i].age01; }
            return n;
        }
        // GRANULAR-FOLLOWER — scan-head marker 0..1 for osc, or -1 when not a sounding Granular engine.
        float granScanPos01 (int osc) const noexcept
        {
            switch (osc)
            {
                case 0: return (engine_  == Engine::GRAN && granEngA_[0].isActive()) ? granEngA_[0].scanPos01() : -1.f;
                case 1: return (engineB_ == Engine::GRAN && granEngB_[0].isActive()) ? granEngB_[0].scanPos01() : -1.f;
                case 2: return (engineC_ == Engine::GRAN && granEngC_[0].isActive()) ? granEngC_[0].scanPos01() : -1.f;
                case 3: return (engineD_ == Engine::GRAN && granEngD_[0].isActive()) ? granEngD_[0].scanPos01() : -1.f;
                default: return -1.f;
            }
        }
        // RESYNTH-FOLLOWER — the geode read-head position 0..1 for osc (the white MIDI follower),
        // or -1 when that osc isn't a sounding Resynth (SPEC) engine. GeodeEngine has no isActive(),
        // so we gate on hasStore(); the "is this voice sounding" check is the caller's isAmpEnvActive().
        float geodeFollowPos01 (int osc) const noexcept
        {
            switch (osc)
            {
                case 0: return (engine_  == Engine::SPEC && geodeEngA_[0].hasStore()) ? geodeEngA_[0].readPos01() : -1.f;
                case 1: return (engineB_ == Engine::SPEC && geodeEngB_[0].hasStore()) ? geodeEngB_[0].readPos01() : -1.f;
                case 2: return (engineC_ == Engine::SPEC && geodeEngC_[0].hasStore()) ? geodeEngC_[0].readPos01() : -1.f;
                case 3: return (engineD_ == Engine::SPEC && geodeEngD_[0].hasStore()) ? geodeEngD_[0].readPos01() : -1.f;
                default: return -1.f;
            }
        }
        // MODAL-FOLLOWER — the exciter read-head position 0..1 for osc (the white MIDI follower,
        // IDENTICAL mechanism to Sample/Granular/Resynth), or -1 when that osc isn't a sounding
        // Modal engine reading a dropped sample exciter. One-shot sweeps once then parks.
        float modalFollowPos01 (int osc) const noexcept
        {
            switch (osc)
            {
                case 0: return (engine_  == Engine::MODAL && modalEngA_[0].isActive()) ? modalEngA_[0].readPos01() : -1.f;
                case 1: return (engineB_ == Engine::MODAL && modalEngB_[0].isActive()) ? modalEngB_[0].readPos01() : -1.f;
                case 2: return (engineC_ == Engine::MODAL && modalEngC_[0].isActive()) ? modalEngC_[0].readPos01() : -1.f;
                case 3: return (engineD_ == Engine::MODAL && modalEngD_[0].isActive()) ? modalEngD_[0].readPos01() : -1.f;
                default: return -1.f;
            }
        }
        // Packed follower position for an EXACT trace along the drawn curve:
        // stageIndex (0=Idle,1=Delay,2=Attack,3=Hold,4=Decay,5=Sustain,6=Release)
        // plus the fraction through that segment. Encoded as stage + frac (e.g. 2.37
        // = 37% through Attack). JS maps this straight onto the curve's x-axis.
        float getAmpEnvFollow() const noexcept
        {
            const int st = (int) ampEnv_.stage();
            return (float) st + (float) ampEnv_.segFraction();
        }

        // ── OSC SCOPE accessors (read on the AUDIO thread by the processor, right
        // after renderNextBlock — same thread that wrote the rings, so no sync) ──
        // Copy this voice's most-recent N samples for oscillator `osc` (0=A..3=D)
        // into `dest`, in chronological order (oldest -> newest). N is clamped to
        // the ring size. Plain reads — no allocation, no locking.
        void copyScopeWindow (int osc, float* dest, int n) const noexcept
        {
            if (osc < 0 || osc > 3 || dest == nullptr) return;
            const int N = (n > kScopeRingSize) ? kScopeRingSize : (n < 0 ? 0 : n);
            const float* ring = scopeRing_[osc];
            // newest sample is at (scopeRingPos_ - 1); oldest of the N-window is
            // (scopeRingPos_ - N). Walk forward so dest[0] is the oldest.
            int idx = (scopeRingPos_ - N) & kScopeRingMask;
            for (int k = 0; k < N; ++k) { dest[k] = ring[idx]; idx = (idx + 1) & kScopeRingMask; }
        }

        // Fundamental frequency of the currently-played note, in Hz (equal-temp,
        // A4=440). Used by the scope trigger to find one period. Honors glide so
        // the displayed period follows portamento.
        float getFundamentalHz() const noexcept
        {
            const double n = (glideProgress_ < 1.0) ? glideNote_ : (double) currentMidiNote_;
            return (float) (440.0 * std::pow (2.0, (n - 69.0) / 12.0));
        }

        void setFltEnvDAHDSR (float dl,float a,float h,float d,float s,float r,
                              float ca,float cd,float cr,bool lp) noexcept
        { setEnvelopeDAHDSR (fltEnvT_, dl,a,h,d,s,r,ca,cd,cr,lp); }
        void setPitchEnv (float dl,float a,float h,float d,float s,float r,
                          float ca,float cd,float cr,bool lp) noexcept
        { setEnvelopeDAHDSR (pitchEnvT_, dl,a,h,d,s,r,ca,cd,cr,lp); }
        void setMod1Env (float dl,float a,float h,float d,float s,float r,
                         float ca,float cd,float cr,bool lp) noexcept
        { setEnvelopeDAHDSR (mod1EnvT_, dl,a,h,d,s,r,ca,cd,cr,lp); }
        void setMod2Env (float dl,float a,float h,float d,float s,float r,
                         float ca,float cd,float cr,bool lp) noexcept
        { setEnvelopeDAHDSR (mod2EnvT_, dl,a,h,d,s,r,ca,cd,cr,lp); }

        /** Called from PluginProcessor::prepareToPlay. Sizes filter + caches
         *  block-size for the per-block AudioBlock view. Phase 9: always stereo
         *  (2 channels) so OSC A + OSC B can be panned independently.
         *  Phase 8a: also prepares per-channel HORIZON high-shelf filters. */
        void prepareToPlay (double sr, int samplesPerBlock, int /*numChannels*/) noexcept
        {
            setCurrentPlaybackSampleRate (sr);
            sampleRate_ = sr;
            juce::dsp::ProcessSpec spec;
            spec.sampleRate       = sr;
            spec.maximumBlockSize = (juce::uint32) samplesPerBlock;
            spec.numChannels      = 2;   // always stereo for OSC A + B per-osc pan

            // Batch 1 Filter — FilterSlot replaces the hardwired juce::dsp::LadderFilter.
            // Filter selection + cutoff + res + drive all come per-block from
            // PluginProcessor via setFilterType/setFilterCut/...; cutoff is
            // modulated per-sample inside renderNextBlock by the FLT envelope
            // and per-voice EROSION drift.
            filterSlot_.prepare (sr);
            filterSlot2_.prepare (sr);
            sendFilterSlot_.prepare (sr);    // fb287 — post-filter reverb send mirrors the two main filters
            sendFilterSlot2_.prepare (sr);
            sendFilterSlot3_.prepare (sr);   // fb296 — post-filter delay send (independent state)
            sendFilterSlot4_.prepare (sr);
            sendFilterSlot5_.prepare (sr);   // fb338 — post-filter DISTORTION send (independent state)
            sendFilterSlot6_.prepare (sr);
            sendFilterSlot7_.prepare (sr);   // fb347 — shared exclusion-bus filter pair
            poolFltSr_ = sr;                 // fb348 — pooled slots prepare themselves on first use
            for (auto& ps : poolSend_) { if (ps.flt1) ps.flt1->prepare (sr); if (ps.flt2) ps.flt2->prepare (sr); }
            sendFilterSlot8_.prepare (sr);

            // Batch 1 — prepare the per-voice LFO bank (sample rate only; each LFO's
            // frequency + shape are pushed per block via setModConfig).
            for (auto& lfo : synthLfo_) lfo.prepare (sr);

            // FLT envelope is a TerrainEnvelope (prepared alongside AMP in
            // setCurrentPlaybackSampleRate); it drives cutoff via SYN_FILTER1_ENV.

            // Per-voice EROSION drift state. One-pole-LP'd uniform noise at
            // ~0.5 Hz so the random walk happens slowly (analog-like). Per-
            // voice seed so two voices don't drift in lockstep.
            const std::uint32_t voiceHash = static_cast<std::uint32_t> (
                                                reinterpret_cast<std::uintptr_t> (this))
                                          ^ 0xC0FFEE17u;
            driftRng_.setSeed ((juce::int64) voiceHash);
            driftState_ = 0.0f;
            const float driftCutHz = 0.5f;   // §5 of prompt — sub-Hz LP
            driftCoef_  = 1.0f - std::exp (-2.0f * juce::MathConstants<float>::pi
                                                 * driftCutHz / (float) sr);

            // fb302 — ANALOG DETUNE rng (subtle always-on pitch character; decorrelated from
            // the erosion-drift rng). State must be non-zero for the xorshift in waverGaussian.
            analogRng_         = (voiceHash ^ 0x27D4EB2Fu) | 1u;
            analogStaticCents_ = 0.0f;
            analogDriftCents_  = 0.0f;
            analogDetuneSemis_ = 0.0;

            // Phase 8a — HORIZON shelves (one per stereo channel, mono spec)
            juce::dsp::ProcessSpec monoSpec;
            monoSpec.sampleRate       = sr;
            monoSpec.maximumBlockSize = (juce::uint32) samplesPerBlock;
            monoSpec.numChannels      = 1;
            horizonShelfL_.prepare (monoSpec);
            horizonShelfR_.prepare (monoSpec);
            horizonShelfL_.reset();
            horizonShelfR_.reset();
            *horizonShelfL_.coefficients = *juce::dsp::IIR::Coefficients<float>::makeHighShelf (sr, 2500.0f, 0.7071f, 1.0f);
            *horizonShelfR_.coefficients = *juce::dsp::IIR::Coefficients<float>::makeHighShelf (sr, 2500.0f, 0.7071f, 1.0f);
            lastHorizonTilt_ = -1.0e9f;   // sample rate changed → force the shelf recompute gate

            // Phase 11c — SPECTRAL filter init (per OSC per channel)
            spectralFilterAL_.prepare (monoSpec);
            spectralFilterAR_.prepare (monoSpec);
            spectralFilterBL_.prepare (monoSpec);
            spectralFilterBR_.prepare (monoSpec);
            spectralFilterAL_.reset();
            spectralFilterAR_.reset();
            spectralFilterBL_.reset();
            spectralFilterBR_.reset();
            // Initialize with safe passthrough (high-cutoff LP)
            auto passthrough = juce::dsp::IIR::Coefficients<float>::makeLowPass (sr, 20000.0f, 0.707f);
            *spectralFilterAL_.coefficients = *passthrough;
            *spectralFilterAR_.coefficients = *passthrough;
            *spectralFilterBL_.coefficients = *passthrough;
            *spectralFilterBR_.coefficients = *passthrough;
            // OSC C / D spectral filters (4-osc)
            spectralFilterCL_.prepare (monoSpec); spectralFilterCR_.prepare (monoSpec);
            spectralFilterDL_.prepare (monoSpec); spectralFilterDR_.prepare (monoSpec);
            spectralFilterCL_.reset(); spectralFilterCR_.reset();
            spectralFilterDL_.reset(); spectralFilterDR_.reset();
            *spectralFilterCL_.coefficients = *passthrough; *spectralFilterCR_.coefficients = *passthrough;
            *spectralFilterDL_.coefficients = *passthrough; *spectralFilterDR_.coefficients = *passthrough;

            // ── CPU / RT-SAFETY: PRE-SIZE the per-voice scratch buffers here (once, off the
            //    audio thread) instead of letting them malloc on their FIRST render. With a large
            //    voice pool, a chord/arp first-triggers many voices in ONE block → hundreds of
            //    audio-thread allocations cluster into that block = the polyphony CPU spike. Same
            //    setSize() flags as the in-render grow-guards (which stay as a fallback only for a
            //    host that sends a block bigger than samplesPerBlock). Buffers are cleared/fully
            //    written before any read every block, so pre-sizing is bit-identical to lazy sizing.
            //    (warpSrc_: stretchRatio is always ≥1 so its source length ≤ numSamples.)
            const int spb = juce::jmax (1, samplesPerBlock);
            scratch_.setSize    (2, spb, false, true,  true);
            fltBus2_.setSize    (2, spb, false, true,  true);   // per-osc filter routing buses
            fltDry_ .setSize    (2, spb, false, true,  true);
            rvbSendF1_ .setSize (2, spb, false, true,  true);   // fb287 — reverb-send routing buses (routed-osc subset)
            rvbSendF2_ .setSize (2, spb, false, true,  true);
            rvbSendDry_.setSize (2, spb, false, true,  true);
            dlySendF1_ .setSize (2, spb, false, true,  true);   // fb296 — delay-send routing buses (independent mask)
            dlySendF2_ .setSize (2, spb, false, true,  true);
            dlySendDry_.setSize (2, spb, false, true,  true);
            dstSendF1_ .setSize (2, spb, false, true,  true);   // fb338 — distortion-send routing buses
            dstSendF2_ .setSize (2, spb, false, true,  true);
            dstSendDry_.setSize (2, spb, false, true,  true);
            envScratch_.setSize (5, spb, false, true,  true);
            sampleBlkA_.setSize (2, spb, false, false, true);
            sampleBlkB_.setSize (2, spb, false, false, true);
            sampleBlkC_.setSize (2, spb, false, false, true);
            sampleBlkD_.setSize (2, spb, false, false, true);
            granBlkA_.setSize   (2, spb, false, false, true);   // GRANULAR-ENGINE-VOICE
            granBlkB_.setSize   (2, spb, false, false, true);
            granBlkC_.setSize   (2, spb, false, false, true);
            granBlkD_.setSize   (2, spb, false, false, true);
            warpSrc_.setSize    (2, spb, false, false, true);
            // HARMONIC/GEODE block buffers — pre-size so the first render never allocates on
            // the audio thread (the in-render setSize stays as an oversized-host fallback)
            harmBlkA_.setSize (2, spb, false, false, true);  harmBlkB_.setSize (2, spb, false, false, true);
            harmBlkC_.setSize (2, spb, false, false, true);  harmBlkD_.setSize (2, spb, false, false, true);
            modalBlkA_.setSize (2, spb, false, false, true); modalBlkB_.setSize (2, spb, false, false, true);   // MODAL-ENGINE-VOICE
            modalBlkC_.setSize (2, spb, false, false, true); modalBlkD_.setSize (2, spb, false, false, true);
            geodeBlkA_.setSize (2, spb, false, false, true); geodeBlkB_.setSize (2, spb, false, false, true);
            geodeBlkC_.setSize (2, spb, false, false, true); geodeBlkD_.setSize (2, spb, false, false, true);
        }

        /** Batch 1 Filter — per-block from PluginProcessor. Cutoff/res are
         *  the BASE values; the per-sample audio loop adds env and drift
         *  before feeding setParams to the active FilterSlot. */
        void setFilterParameters (float cutoffHz, float resonance) noexcept
        {
            baseCutHz_   = juce::jlimit (20.0f, 20000.0f, cutoffHz);
            baseRes01_   = juce::jlimit (0.0f,  1.0f,    resonance);
        }
        // Filter key-track amount (0..1). 1 = 1 semitone of cutoff per semitone
        // of played note, referenced to MIDI note 60 (middle C).
        void setFilterKeytrack  (float amt01) noexcept { filterKeytrack1_ = juce::jlimit (0.0f, 1.0f, amt01); }
        void setFilterKeytrack2 (float amt01) noexcept { filterKeytrack2_ = juce::jlimit (0.0f, 1.0f, amt01); }

        /** Batch 1 — publish the modulation config to this voice. Resolves each
         *  LFO's frequency now (free rate, or synced Hz from BPM). The per-sample
         *  audio loop ticks the LFOs and adds enabled routes into effective dests.
         *  Cheap to call every block; copies a small POD struct. */
        // LFO ARC L1 — wire the drawn-shape tables (processor-owned audio mirror, stable
        // lifetime; set once at prepare — table CONTENT updates in place, never the pointer).
        float lfoPhase (int k) const noexcept { return (k >= 0 && k < wc::NUM_LFOS) ? synthLfo_[k].currentPhase() : 0.0f; }   // fb231 — the follower's retrig truth
        void setLfoCustomTables (const float (*tables)[wc::kLfoTableN + 1]) noexcept
        {
            for (int i = 0; i < wc::NUM_LFOS; ++i) synthLfo_[i].setCustomTable (tables[i]);
        }

        void setModConfig (const wc::ModConfig& cfg, float bpm) noexcept
        {
            modConfig_ = cfg;
            for (int i = 0; i < wc::NUM_LFOS; ++i)
            {
                synthLfo_[i].setSettings (cfg.lfos[i]);
                const float hz = cfg.lfos[i].sync ? wc::syncedHz (cfg.lfos[i].syncIdx, bpm)
                                                  : cfg.lfos[i].rateHz;
                synthLfo_[i].setFrequency (hz);
            }
            // fb178 — scan which envelope sources the matrix references (once per push):
            // dormant dyn envs stay untouched; legacy envs 2-5 must TICK even at legacy
            // depth 0 when the matrix reads them (the CPU gate below honors this mask).
            dynEnvUsedMask_ = 0; legEnvUsedMask_ = 0; anyEnvSource_ = false;
            for (int a2 = 0; a2 < modConfig_.numAssignments; ++a2)
            {
                const auto& as2 = modConfig_.assignments[a2];
                if (! as2.enabled) continue;
                const int sI = (int) as2.source;
                if (! wc::isEnvModSource (sI)) continue;
                anyEnvSource_ = true;
                if      (sI == (int) wc::ModSource::EnvFilter) legEnvUsedMask_ |= 1u;
                else if (sI == (int) wc::ModSource::EnvPitch)  legEnvUsedMask_ |= 2u;
                else if (sI == (int) wc::ModSource::EnvMod1)   legEnvUsedMask_ |= 4u;
                else if (sI == (int) wc::ModSource::EnvMod2)   legEnvUsedMask_ |= 8u;
                const int k = sI - (int) wc::ModSource::EnvD1;
                if (k >= 0 && k < kMaxDynEnvs) dynEnvUsedMask_ |= (1u << k);
            }
        }

        /** Most-recent L1 value (bipolar -1..+1) for the editor's live LFO dot. */
        float getSynthLfoVis() const noexcept { return lfoVisValue_; }

        /** fb163 — LIVE FILTER CURVE: this voice's effective (post-mod: LFO + env routes +
            keytrack + drift + velocity) cutoff Hz and resonance per slot, for the display. */
        float getFltVisHz1()  const noexcept { return lastCutHz1_; }
        float getFltVisHz2()  const noexcept { return lastCutHz2_; }
        float getFltVisRes1() const noexcept { return visRes1_; }
        float getFltVisRes2() const noexcept { return visRes2_; }
        /** fb457 — OVERPASS 1, the WAVETABLE half. The same idea as getFltVisHz1() one line up,
            and deliberately the same mechanism: this is the frame the oscillator is ACTUALLY
            reading — base + mod-matrix + keytrack + route + FLOW, already one-pole smoothed —
            not the knob. The waterfall asked juce.getSliderState('..._WT_FRAME') for its
            position, and a parameter cannot know a route moved it, so the table sat still while
            the sound swept. Max: "whenever the LFO is on that wavetable position knob, I want it
            to move automatically." */
        float getWtFrameVis (int osc) const noexcept
        { return osc == 0 ? framePos_ : osc == 1 ? framePosB_ : osc == 2 ? framePosC_ : framePosD_; }

        /** fb458 — everything the WATERFALL needs to draw the table the oscillator is actually
            reading: the frame AND the shaping applied on top of it. All values are the voice's
            live, smoothed, post-modulation ones — the same members the render chain reads two
            thousand lines below — so the picture cannot disagree with the sound. */
        // fb460 — blur rides here too: it is the last thing between the table and the ear that the
        // waterfall could not show. blurX_ is the SMOOTHED value the per-block renderBlend uses.
        struct WtDisp { float frame, warpAmt, warp2Amt, foldAmt, blur; int warpMode, warp2Mode, foldShape; };
        WtDisp getWtDisplay (int osc) const noexcept
        {
            switch (osc)
            {
                case 1:  return { framePosB_, warpAmountB_, warp2AmountB_, foldAmountB_, blurB_, warpModeB_, warp2ModeB_, foldShapeB_ };
                case 2:  return { framePosC_, warpAmountC_, warp2AmountC_, foldAmountC_, blurC_, warpModeC_, warp2ModeC_, foldShapeC_ };
                case 3:  return { framePosD_, warpAmountD_, warp2AmountD_, foldAmountD_, blurD_, warpModeD_, warp2ModeD_, foldShapeD_ };
                default: return { framePos_,  warpAmount_,  warp2AmountA_, foldAmountA_, blurA_, warpMode_,  warp2ModeA_, foldShapeA_ };
            }
        }
        void setFilterType (int typeIdx) noexcept
        {
            const int clamped = juce::jlimit (0, (int) tw::filters::kNumTypes - 1, typeIdx);
            filterType1_ = clamped;
            filterSlot_.setType (static_cast<tw::filters::Type> (clamped));
            sendFilterSlot_.setType (static_cast<tw::filters::Type> (clamped));   // fb287 — send mirror
            sendFilterSlot3_.setType (static_cast<tw::filters::Type> (clamped));  // fb296 — delay-send mirror
            sendFilterSlot5_.setType (static_cast<tw::filters::Type> (clamped));  // fb338 — distortion-send mirror
            sendFilterSlot7_.setType (static_cast<tw::filters::Type> (clamped));  // fb347 — exclusion mirror
            for (auto& ps : poolSend_) if (ps.flt1) ps.flt1->setType (static_cast<tw::filters::Type> (clamped));   // fb348 — pooled mirrors
        }
        // ── Filter 2 (independent) + routing/mix setters ──
        void setFilterParameters2 (float cutoffHz, float resonance) noexcept
        {
            baseCutHz2_ = juce::jlimit (20.0f, 20000.0f, cutoffHz);
            baseRes012_ = juce::jlimit (0.0f,  1.0f,    resonance);
        }
        void setFilterType2 (int typeIdx) noexcept
        {
            const int clamped = juce::jlimit (0, (int) tw::filters::kNumTypes - 1, typeIdx);
            filterType2_ = clamped;
            filterSlot2_.setType (static_cast<tw::filters::Type> (clamped));
            sendFilterSlot2_.setType (static_cast<tw::filters::Type> (clamped));   // fb287 — send mirror
            sendFilterSlot4_.setType (static_cast<tw::filters::Type> (clamped));   // fb296 — delay-send mirror
            sendFilterSlot6_.setType (static_cast<tw::filters::Type> (clamped));   // fb338 — distortion-send mirror
            sendFilterSlot8_.setType (static_cast<tw::filters::Type> (clamped));   // fb347 — exclusion mirror
            for (auto& ps : poolSend_) if (ps.flt2) ps.flt2->setType (static_cast<tw::filters::Type> (clamped));   // fb348 — pooled mirrors
        }
        void setFilterDrive2 (float drv01) noexcept   { drv012_ = juce::jlimit (0.0f, 1.0f, drv01); }
        void setFilterEnvAmount2 (float env) noexcept { envAmount2_ = juce::jlimit (-1.0f, 1.0f, env); }
        void setFilterMix1 (float mix) noexcept       { filterMix1_ = juce::jlimit (0.0f, 1.0f, mix); }
        void setFilterMix2 (float mix) noexcept       { filterMix2_ = juce::jlimit (0.0f, 1.0f, mix); }
        void setFilterRouting (int mode) noexcept     { filterRouting_ = (mode != 0) ? 1 : 0; }
        // fb79 — per-oscillator CONTINUOUS filter sends (0..1 each; Sub arrives as 0/1). Each source
        // (A,B,C,D,Sub) mixes m1 into the F1 bus and m2 into F2, remainder dry — fully independent.
        void setFilterSources (const float s1[5], const float s2[5]) noexcept
        { for (int k = 0; k < 5; ++k) { fltSrc1_[k] = juce::jlimit (0.0f, 1.0f, s1[k]); fltSrc2_[k] = juce::jlimit (0.0f, 1.0f, s2[k]); } }
        // fb63 — the Noise layer as a 6th filter source (its own bus routing, mirrors the osc mask logic).
        void setNoiseFilterRouting (bool f1, bool f2) noexcept { noiseSrc1_ = f1; noiseSrc2_ = f2; }
        // Back-panel Vel (velocity→cutoff depth) + post-filter Drive, per filter.
        void setVelDepth (float d) noexcept { velDepth_ = juce::jlimit (0.0f, 1.0f, d); }   // fb260
        void setFilterVelocity (float v1, float v2) noexcept
        { velAmt1_ = juce::jlimit (0.0f, 1.0f, v1); velAmt2_ = juce::jlimit (0.0f, 1.0f, v2); }
        void setFilterPostDrive (float d1, float d2) noexcept
        { postDrv1_ = juce::jlimit (0.0f, 1.0f, d1); postDrv2_ = juce::jlimit (0.0f, 1.0f, d2); }
        // Back-panel Drive TYPE — which waveshaper the post-filter drive uses (0=Tube..5=Fuzz).
        void setFilterDriveType (int t1, int t2) noexcept
        { driveType1_ = juce::jlimit (0, 5, t1); driveType2_ = juce::jlimit (0, 5, t2); }
        // Post-filter drive waveshapers. Pure, STATELESS functions of the sample — node-ready
        // (lift straight into a drive node later). x=input, d=drive gain, a=amount (level-aware modes).
        // All DC-free at rest (x=0 -> 0) and bounded so switching type never jumps the level wildly.
        static inline float fShape (float x, int type, float d, float a) noexcept
        {
            switch (type)
            {
                default:
                case 0: return std::tanh (x * d);                                     // Tube  — warm soft saturation (default)
                case 1: return (x >= 0.0f) ? std::tanh (x * d)                         // Diode — asymmetric (even harmonics)
                                           : std::tanh (x * d * 0.5f) * 0.85f;
                case 2: return std::sin (x * d * 1.5f);                                // Fold  — sine wavefolder (metallic)
                case 3: return juce::jlimit (-1.0f, 1.0f, x * d);                      // Hard  — hard clip (buzzy)
                case 4: { const float lv = std::round (2.0f + (1.0f - a) * 30.0f);     // Crush — amplitude bitcrush (32->2 steps)
                          const float xc = juce::jlimit (-1.0f, 1.0f, x * d);
                          return std::round (xc * lv) / juce::jmax (1.0f, lv); }
                case 5: return juce::jlimit (-1.0f, 1.0f,                              // Fuzz  — hot asym -> squareish
                                             std::tanh (x * d * 4.0f + 0.15f) - 0.148885f);
            }
        }
        void setFilterPoles (int tap1, int tap2) noexcept
        { filterSlot_.setPoles (tap1); filterSlot2_.setPoles (tap2);      // tap 0..3 = 6/12/18/24 dB
          sendFilterSlot_.setPoles (tap1); sendFilterSlot2_.setPoles (tap2);     // fb287 — send mirror
          sendFilterSlot3_.setPoles (tap1); sendFilterSlot4_.setPoles (tap2);     // fb296 — delay-send mirror
          sendFilterSlot5_.setPoles (tap1); sendFilterSlot6_.setPoles (tap2);
          sendFilterSlot7_.setPoles (tap1); sendFilterSlot8_.setPoles (tap2);
          for (auto& ps : poolSend_) { if (ps.flt1) ps.flt1->setPoles (tap1); if (ps.flt2) ps.flt2->setPoles (tap2); } }  // fb348 — pooled mirrors
        // STEREO SPREAD — L/R cutoff offset (0..1), per filter.
        // filter SPREAD → POST-filter stereo width (mid/side all-pass, see widen()). NOTE: no longer
        // fed to the filter cores (their spread_ stays 0) — the old L/R cutoff offset DETUNED pitched
        // filters (comb). This is pure width: flat magnitude ⇒ zero pitch change.
        void setFilterSpread (float s1, float s2) noexcept
        { spread1_ = juce::jlimit (0.0f, 1.0f, s1); spread2_ = juce::jlimit (0.0f, 1.0f, s2); }

        // ── Per-envelope ROUTING (the mini mod-matrix per envelope) ──────────
        // Destination indices — MUST match the SYN_ENV*_DEST choice order and the
        // WebUI menu. Env 1 (AMP) is hardwired to amplitude and not in this enum.
        enum EnvDest { kEnvOff = 0, kEnvAmp = 1, kEnvFilt1 = 2, kEnvFilt2 = 3,
                       kEnvFilt12 = 4, kEnvMod1 = 5, kEnvMod2 = 6, kEnvPitch = 7 };
        /** Routing for the four FREE envelopes (UI 2,3,4,5 → internal 1,2,3,4 =
         *  FLT, PITCH, MOD1, MOD2). Each carries a destination + bipolar depth
         *  (-1..+1). Stored at internal slots [1..4]; [0] is AMP (not routed). */
        void setEnvRouting (int d2, float a2, int d3, float a3,
                            int d4, float a4, int d5, float a5) noexcept
        {
            envDest_[1] = d2; envDepth_[1] = juce::jlimit (-1.0f, 1.0f, a2);
            envDest_[2] = d3; envDepth_[2] = juce::jlimit (-1.0f, 1.0f, a3);
            envDest_[3] = d4; envDepth_[3] = juce::jlimit (-1.0f, 1.0f, a4);
            envDest_[4] = d5; envDepth_[4] = juce::jlimit (-1.0f, 1.0f, a5);
        }
        void setFilterDrive (float drv01) noexcept
        {
            drv01_ = juce::jlimit (0.0f, 1.0f, drv01);
        }
        /** Bipolar -1..+1 amount of the FLT envelope applied to cutoff
         *  in semitone space (±96 ST at ±1.0). Sign inverts the env. */
        void setFilterEnvAmount (float env) noexcept
        {
            envAmount_ = juce::jlimit (-1.0f, 1.0f, env);
        }
        /** FLT envelope ADSR (ms / 0..1 / ms / ms). */
        void setFilterEnvParameters (float attackMs, float decayMs,
                                     float sustain,  float releaseMs) noexcept
        {
            // Back-compat 4-arg shim → TerrainEnvelope (no delay/hold/curve).
            setEnvelopeDAHDSR (fltEnvT_, 0.0f, attackMs, 0.0f, decayMs,
                               sustain, releaseMs, 0.0f, 0.0f, 0.0f, false);
        }
        /** EROSION 0..1 (per-block from APVTS / 100). Scaled erosion^1.8
         *  applied as semitone drift to cutoff inside renderNextBlock. */
        void setErosionAmount_filter (float e) noexcept
        {
            // Filter-cutoff drift only. (Pitch drift is now WAVER — see setWaver().)
            fltErosionAmount_ = juce::jlimit (0.0f, 1.0f, e);
        }

        /** Linear gain 0..1 — applied after filter, before pan. */
        void setLevel (float level) noexcept
        {
            level_ = juce::jlimit (0.0f, 1.0f, level);
        }

        /** Equal-power pan -1 (full L) .. 0 (center) .. +1 (full R). */
        void setPan (float pan) noexcept
        {
            const float p = juce::jlimit (-1.0f, 1.0f, pan);
            const float angle = (p + 1.0f) * 0.25f * juce::MathConstants<float>::pi;
            panLT_ = std::cos (angle);   // fb202 — PAN GLIDE (Max: "no static"): targets only;
            panRT_ = std::sin (angle);   // the render loop one-poles the live gains (2.5ms, fb180 law)
        }

        /** Octave (-3..+3), semitone (-12..+12), cents (-100..+100). Applied
         *  on top of the MIDI note when computing the next note-on frequency.
         *  Updates current note pitch live if a note is already playing. */
        void setTuning (int oct, int semi, float cent) noexcept
        {
            octOffset_   = oct;
            semiOffset_  = semi;
            centsOffset_ = cent;
            if (playing_)
                updateUnisonPhaseIncrementsA (glideNote_);
        }

        /** Set which wavetable this voice reads from. Pointer is borrowed —
         *  caller (PluginProcessor) guarantees lifetime ≥ voice lifetime
         *  (WavetableBank lives for the entire plugin instance). */
        void setWavetable (const tw::Wavetable* wt) noexcept
        {
            currentWavetable_ = wt;
        }

        /** Set frame position 0..1 within the current wavetable. */
        void setWavetableFrame (float pos) noexcept
        {
            framePosBase_ = juce::jlimit (0.0f, 1.0f, pos);
        }

        /** Phase 2C — Warp mode (0=NONE, 1=BEND, 2=SYNC, 3=FORMANT) +
         *  amount 0..1. Applied to phase BEFORE wavetable lookup, so warp
         *  composes cleanly with any wavetable choice. */
        void setWarp (int mode, float amount) noexcept
        {
            warpMode_   = juce::jlimit (0, kWarpModeMax, mode);
            warpAmountBase_ = juce::jlimit (0.0f, 1.0f, amount);
        }

        /** Select which engine renders this voice. Idx 0..5 from
         *  SYN_OSC_A_ENGINE APVTS choice. Out-of-range clamps to nearest end. */
        void setEngine (int idx) noexcept
        {
            const int clamped = juce::jlimit (0, 6, idx);
            engine_ = static_cast<Engine> (clamped);
        }

        /** Test-only accessor — not used in production audio path. */
        Engine engineForTesting() const noexcept { return engine_; }

        /** VIZDBG — per-osc render-critical state for the on-screen forensics overlay:
         *  engine index, active unison count, current mip level, FM effective index,
         *  unison auto-gain. Read on the audio thread right after this voice rendered. */
        /** VIZDBG — this osc's solo/mute/enable gate TARGET (0 = configured silent).
         *  Lets the overlay tell "osc off → flat is correct" from "osc ON but window
         *  silent → real render dropout" (the per-osc class-D blind spot fix). */
        float oscGateTargetVal (int osc) const noexcept { return oscGateTarget_[juce::jlimit (0, 3, osc)]; }

        void getVizDiag (int osc, int& engineIdx, int& activeUni, int& mipLvl, float& d1Eff, float& uNorm) const noexcept
        {
            switch (osc)
            {
                default:
                case 0: engineIdx = (int) engine_;  activeUni = activeUnisonA_; mipLvl = currentMipLevelA_; uNorm = uNormA_; break;
                case 1: engineIdx = (int) engineB_; activeUni = activeUnisonB_; mipLvl = currentMipLevelB_; uNorm = uNormB_; break;
                case 2: engineIdx = (int) engineC_; activeUni = activeUnisonC_; mipLvl = currentMipLevelC_; uNorm = uNormC_; break;
                case 3: engineIdx = (int) engineD_; activeUni = activeUnisonD_; mipLvl = currentMipLevelD_; uNorm = uNormD_; break;
            }
            d1Eff = fmD1Eff_[(size_t) juce::jlimit (0, 3, osc)];
        }

        // ════════ SAMPLE-ENGINE-VOICE — per-OSC Sample engine params + setters ════════
        struct SampleEngineParams
        {
            float scan = 0.f, stretch = 0.f, formant = 0.f, spray = 0.f, xfade = 0.12f;
            float start = 0.f, end = 1.f, loopStart = 0.f, loopEnd = 1.f;
            int   loopMode = 1, snap = 0;
            int   stretchMode = 0;   // 0=Tones 1=Beats 2=Texture (warp algorithm)
            int   formantMode = 0;   // 0=Normal 1=Inverted 2=Cross-Formant 3=Spectral-Tilt
            float fadeIn = 0.f, fadeOut = 0.f;
            float fadeInCurve = 0.5f, fadeOutCurve = 0.5f;   // fade shape (0.5 = classic sin)
            float air = 0.f;   // AIR exciter amount 0..1
            float warp = 0.f;  // sample warp shaper amount 0..1
            int   warpMode = 0;  // 0=Off 1=Sine Shaper 2=Rectify 3=Fold 4=Drive 5=Crush

            // CPU: the processor change-gates its per-block 96-voice push on this.
            bool operator== (const SampleEngineParams& o) const noexcept
            {
                return scan == o.scan && stretch == o.stretch && formant == o.formant
                    && spray == o.spray && xfade == o.xfade && start == o.start && end == o.end
                    && loopStart == o.loopStart && loopEnd == o.loopEnd && loopMode == o.loopMode
                    && snap == o.snap && stretchMode == o.stretchMode && formantMode == o.formantMode
                    && fadeIn == o.fadeIn && fadeOut == o.fadeOut && air == o.air
                    && fadeInCurve == o.fadeInCurve && fadeOutCurve == o.fadeOutCurve
                    && warp == o.warp && warpMode == o.warpMode;
            }
        };
        void setSampleParamsA (const SampleEngineParams& p) noexcept { sampleParamsA_ = p; }
        void setSampleParamsB (const SampleEngineParams& p) noexcept { sampleParamsB_ = p; }
        void setSampleParamsC (const SampleEngineParams& p) noexcept { sampleParamsC_ = p; }
        void setSampleParamsD (const SampleEngineParams& p) noexcept { sampleParamsD_ = p; }
        void setSampleSources (tw::SampleBuffer* a, tw::SampleBuffer* b,
                               tw::SampleBuffer* c, tw::SampleBuffer* d) noexcept   // PEROSC-VOICE
        { sampleSource_[0] = a; sampleSource_[1] = b; sampleSource_[2] = c; sampleSource_[3] = d; }
        // NOISE IMPORT (P5) — shared looping-sample noise source. One atomic load per block; recache raw
        // pointers only when the buffer changes (CPU-safe). The held BufferPtr keeps it alive through render.
        void setNoiseSampleSource (tw::SampleBuffer* s) noexcept
        {
            noiseSampleSource_ = s;
            auto buf = (s != nullptr) ? s->load() : tw::SampleBuffer::BufferPtr();
            if (buf.get() != noiseBufLast_)
            {
                noiseHeldBuf_ = buf; noiseBufLast_ = buf.get();
                if (buf != nullptr && buf->getNumSamples() > 1)
                {
                    noiseSampLen_ = buf->getNumSamples();
                    noiseSampL_   = buf->getReadPointer (0);
                    noiseSampR_   = buf->getNumChannels() > 1 ? buf->getReadPointer (1) : noiseSampL_;
                    const double nr = (s != nullptr) ? s->getSampleRate() : 0.0;
                    noiseSampNativeOverOut_ = (nr > 0.0 && noiseSR_ > 0.0f) ? (nr / (double) noiseSR_) : 1.0;
                    if (noiseSampPos_ >= (double) noiseSampLen_) noiseSampPos_ = 0.0;
                }
                else { noiseSampLen_ = 0; noiseSampL_ = noiseSampR_ = nullptr; }
            }
        }
        // GRANULAR-ENGINE-VOICE — per-OSC granular params (granular reuses the same sampleSource_ buffers).
        // GLOBAL grain budget — the processor shares ONE live-grain counter + cap across every
        // granular engine in the instance (all 4 oscs × unison × voices), so stacked dense
        // granulars thin gracefully instead of stacking to thousands of grains. Set once.
        void setGrainBudget (int* used, int cap) noexcept
        {
            for (auto& e : granEngA_) e.setGrainBudget (used, cap);
            for (auto& e : granEngB_) e.setGrainBudget (used, cap);
            for (auto& e : granEngC_) e.setGrainBudget (used, cap);
            for (auto& e : granEngD_) e.setGrainBudget (used, cap);
        }
        void setGranParamsA (const tw::GranularEngineParams& p) noexcept { granParamsA_ = p; }
        void setGranParamsB (const tw::GranularEngineParams& p) noexcept { granParamsB_ = p; }
        void setGranParamsC (const tw::GranularEngineParams& p) noexcept { granParamsC_ = p; }
        void setGranParamsD (const tw::GranularEngineParams& p) noexcept { granParamsD_ = p; }

        // ── GEODE-ENGINE-VOICE — per-OSC resynthesis (Engine::SPEC). Stores are analyzed
        //    off-thread by the processor and atomic-published; the voice loads the pointer
        //    per block (same shape as sampleSource_). Partial budget mirrors the grain budget. ──
        void setGeodeStores (const std::atomic<const tw::GeodeFrameStore*>* a, const std::atomic<const tw::GeodeFrameStore*>* b,
                             const std::atomic<const tw::GeodeFrameStore*>* c, const std::atomic<const tw::GeodeFrameStore*>* d) noexcept
        { geodeStoreSrc_[0] = a; geodeStoreSrc_[1] = b; geodeStoreSrc_[2] = c; geodeStoreSrc_[3] = d; }
        void setPartialBudget (int* used, int cap) noexcept
        {
            for (auto& e : geodeEngA_) e.setPartialBudget (used, cap);
            for (auto& e : geodeEngB_) e.setPartialBudget (used, cap);
            for (auto& e : geodeEngC_) e.setPartialBudget (used, cap);
            for (auto& e : geodeEngD_) e.setPartialBudget (used, cap);
            for (auto& e : harmEngA_) e.setPartialBudget (used, cap);   // HARMONIC-ENGINE-VOICE — same pool
            for (auto& e : harmEngB_) e.setPartialBudget (used, cap);
            for (auto& e : harmEngC_) e.setPartialBudget (used, cap);
            for (auto& e : harmEngD_) e.setPartialBudget (used, cap);
            for (auto& e : modalEngA_) e.setPartialBudget (used, cap);   // MODAL-ENGINE-VOICE — same pool
            for (auto& e : modalEngB_) e.setPartialBudget (used, cap);
            for (auto& e : modalEngC_) e.setPartialBudget (used, cap);
            for (auto& e : modalEngD_) e.setPartialBudget (used, cap);
        }

        // fb498 — MODAL's LAZY ARM. MESSAGE THREAD ONLY (the processor's 60 Hz timerCallback and
        // prepareToPlay), because this allocates 12 MiB per voice and allocation must never
        // happen on the audio thread. One-way: once ready it is never un-prepared while the rate
        // holds, so a pointer the audio thread has can never go stale — the same invariant
        // WavetableBank relies on ("nothing ever un-builds a table", WavetableBank.h:186-187).
        // The release store is the publication: everything the audio thread will read is written
        // before it, and renderNextBlock's acquire load pairs with it.
        void prepareModalEngines()
        {
            if (modalReady_.load (std::memory_order_acquire)) return;
            { int u = 0; for (auto& e : modalEngA_) e.prepare (sampleRate_, u++ == 0); }   // MODAL-ENGINE-VOICE
            { int u = 0; for (auto& e : modalEngB_) e.prepare (sampleRate_, u++ == 0); }
            { int u = 0; for (auto& e : modalEngC_) e.prepare (sampleRate_, u++ == 0); }
            { int u = 0; for (auto& e : modalEngD_) e.prepare (sampleRate_, u++ == 0); }
            modalReady_.store (true, std::memory_order_release);
        }
        // fb517 — HARM's LAZY ARM, the modal pattern above cloned verbatim (same thread rules,
        // same one-way publication, same acquire/release pairing).
        void prepareHarmonicEngines()
        {
            if (harmReady_.load (std::memory_order_acquire)) return;
            { int u = 0; for (auto& e : harmEngA_) e.prepare (sampleRate_, u++ == 0); }   // HARMONIC-ENGINE-VOICE
            { int u = 0; for (auto& e : harmEngB_) e.prepare (sampleRate_, u++ == 0); }   //  (index 0 = bank anchor)
            { int u = 0; for (auto& e : harmEngC_) e.prepare (sampleRate_, u++ == 0); }
            { int u = 0; for (auto& e : harmEngD_) e.prepare (sampleRate_, u++ == 0); }
            harmReady_.store (true, std::memory_order_release);
        }
        // HARM-VIZ — downsample this voice's live anchor bank into UI bins (audio thread only)
        bool harmLiveBins (int osc, float* out, int nBins) const noexcept
        {
            const Engine oe[4] = { engine_, engineB_, engineC_, engineD_ };
            if (osc < 0 || osc > 3 || oe[osc] != Engine::HARM) return false;
            // fb517 — unarmed banks hold empty vectors; liveBins must not touch them.
            if (! harmReady_.load (std::memory_order_acquire)) return false;
            const std::array<tw::HarmonicEngine, kMaxUnison>* engs[4] = { &harmEngA_, &harmEngB_, &harmEngC_, &harmEngD_ };
            return (*engs[osc])[0].liveBins (out, nBins) > 0;
        }
        void setHarmParamsA (const tw::HarmParams& p) noexcept { harmParamsA_ = p; }   // HARMONIC-ENGINE-PUSH
        void setHarmParamsB (const tw::HarmParams& p) noexcept { harmParamsB_ = p; }
        void setHarmParamsC (const tw::HarmParams& p) noexcept { harmParamsC_ = p; }
        void setHarmParamsD (const tw::HarmParams& p) noexcept { harmParamsD_ = p; }
        void setBlendSlot (int osc, int slot, int mode, int src, float depth) noexcept   // BLEND-MODES-PUSH (cross-osc warp)
        {
            if (osc < 0 || osc > 3 || slot < 0 || slot > 3) return;
            BlendSlotV& b = blendSlot_[osc][slot];
            b.mode = mode; b.src = src;
            const float dc = juce::jlimit (0.f, 1.f, depth);
            // fb523 — THE TAPER IS PER-MODE NOW. Everything except FM keeps the house exp-bias
            //  curve (e^{2d}−1)/(e²−1) BIT-FOR-BIT. FM gets a 361:1 curve because its ceiling
            //  moved from a 33.9-radian index to 96 kHz of deviation — a 21.6× (C3) / 86× (C1)
            //  larger span that the house curve's 7.39:1 taper cannot spread without cramming
            //  every musical value into the bottom 5 % of the knob.
            //    T(d) = (361^d − 1)/360 = (e^{ln(361)·d} − 1)/360
            //    T(0.1) = 0.0022277 → Δf   213.9 Hz → β  1.63 (C3) /  6.54 (C1)
            //    T(0.5) = 0.050000  → Δf 4,800.0 Hz → β 36.69 (C3) / 146.8 (C1)   ← exactly 1/(√361+1)
            //    T(0.7660) = 0.2500 → Δf  24,000 Hz  = Nyquist at 48 kHz: aliasing onset
            //    T(1.0) = 1.000000  → Δf  96,000 Hz → β 733.87 (C3) / 2,935.5 (C1)
            //  This runs at BLOCK rate (16 calls/block), never per sample, so the exp is free;
            //  the per-sample de-zipper downstream is untouched.
            //  fb524 — PD AND AM JOIN FM ON THE 361:1 CURVE, for the same reason and by the same
            //  arithmetic. Their ceilings moved 2.20 -> 8.50 cycles and 2.0 -> 10.0 index, i.e.
            //  3.9x and 5.0x more span, and the house 7.39:1 curve cannot carry that: on the old
            //  taper the raised PD reaches its 2 % out-of-harmonic budget at HALF travel and the
            //  raised AM is already +3.8 dB of tremolo at 15 %. On 361:1 the same two constants
            //  give PD 0.034 cycles / AM index 0.039 at d = 0.15 (cleaner than what ships today),
            //  PD 0.425 cycles / AM index 0.50 at half travel, and the destructive quarter sits
            //  at the top where the Lifeguard law puts it. RM keeps the house curve: its depth is
            //  ALSO its dry/wet and its level, so a 361:1 curve there would just make the mode
            //  inaudible for the first two thirds of the knob.
            b.depth = blendIsLinTaper (b.mode)
                    ? dc                                                            // FM EXP — the depth IS the exponent (octaves); see blendIsLinTaper
                    : blendIsFmTaper (b.mode)                                       // fb551 — FM / PD / AM + FM CLAMP
                    ? (std::exp (kFmTaperRate * dc) - 1.0f) * kFmTaperNorm          // FM / PD / AM / FM CLAMP — 361:1
                    : (std::exp (2.0f * dc) - 1.0f) / (std::exp (2.0f) - 1.0f);     // house exp-bias curve (RM)
        }
        void setModalParamsA (const tw::ModalParams& p) noexcept { modalParamsA_ = p; }   // MODAL-ENGINE-PUSH
        void setModalParamsB (const tw::ModalParams& p) noexcept { modalParamsB_ = p; }
        void setModalParamsC (const tw::ModalParams& p) noexcept { modalParamsC_ = p; }
        void setModalParamsD (const tw::ModalParams& p) noexcept { modalParamsD_ = p; }
        void setGeodeParamsA (const tw::GeodeParams& p) noexcept { geodeParamsA_ = p; }
        void setGeodeParamsB (const tw::GeodeParams& p) noexcept { geodeParamsB_ = p; }
        void setGeodeParamsC (const tw::GeodeParams& p) noexcept { geodeParamsC_ = p; }
        void setGeodeParamsD (const tw::GeodeParams& p) noexcept { geodeParamsD_ = p; }

        // ── Phase 9 — OSC B setters (mirror of OSC A) ─────────────────────

        void setTuningB (int oct, int semi, float cent) noexcept
        {
            octOffsetB_   = oct;
            semiOffsetB_  = semi;
            centsOffsetB_ = cent;
            if (playing_)
                updateUnisonPhaseIncrementsB (glideNote_);
        }

        void setLevelB (float level) noexcept
        {
            levelB_ = juce::jlimit (0.0f, 1.0f, level);
        }

        void setPanB (float pan) noexcept
        {
            const float p = juce::jlimit (-1.0f, 1.0f, pan);
            const float angle = (p + 1.0f) * 0.25f * juce::MathConstants<float>::pi;
            panLBT_ = std::cos (angle);   // fb202 — glide target
            panRBT_ = std::sin (angle);
        }

        void setWavetableB (const tw::Wavetable* wt) noexcept { currentWavetableB_ = wt; }
        void setWavetableFrameB (float pos) noexcept { framePosBaseB_ = juce::jlimit (0.0f, 1.0f, pos); }

        void setWarpB (int mode, float amount) noexcept
        {
            warpModeB_   = juce::jlimit (0, kWarpModeMax, mode);
            warpAmountBaseB_ = juce::jlimit (0.0f, 1.0f, amount);
        }

        /** WARP 2 — second chained warp slot, per OSC. Runs in SERIES on slot 1's
         *  output phase (Serum WARP1→WARP2 parity). Same mode list as slot 1. */
        void setWarp2 (int modeA, float amountA, int modeB, float amountB) noexcept
        {
            warp2ModeA_       = juce::jlimit (0, kWarpModeMax, modeA);
            warp2AmountBaseA_ = juce::jlimit (0.0f, 1.0f, amountA);
            warp2ModeB_       = juce::jlimit (0, kWarpModeMax, modeB);
            warp2AmountBaseB_ = juce::jlimit (0.0f, 1.0f, amountB);
        }

        void setEngineB (int idx) noexcept
        {
            const int clamped = juce::jlimit (0, 6, idx);
            engineB_ = static_cast<Engine> (clamped);
        }

        // ── Phase 8b — Unison + EROSION + HORIZON setters ────────────────

        /** Per-OSC UNISON (back panel pill, replaces the old global UNISON+SPREAD).
         *  count   1..16 voices stacked per note (Serum-parity).
         *  detune  0..1 → the pitch TAPER only. fb522: the fan is ±(detune^2.5 · URANGE)
         *                 cents at the edges; the SCALE is the SYN_OSC_x_URANGE knob (5..4800 c,
         *                 default 50 = the retired kUniMaxDetuneCents), not a constant any more.
         *  blend   0..1 → balance of the centre voice vs the detuned/outer voices:
         *                 1 = all voices equal; 0 = only the centre voice (mono).
         *                 Modelled per-voice as gain = 1 − (1−blend)·|u_norm|.
         *  width  −1..+1 → stereo spread (equal-power pan) of the voices L↔R. fb522: BIPOLAR
         *                 (SYN_OSC_x_UWIDTH is −100..+100); negative mirrors the field.
         *  Blend gain is pre-multiplied into the pan tables so the render loop is
         *  unchanged; auto-gain (1/√Σgain²) holds perceived loudness as voices rise. */
        void setUnisonA (int count, float detune01, float blend01, float width01) noexcept
        {
            setUnisonImpl (0, activeUnisonA_, uDetuneCentsA_, uPanLTA_, uPanRTA_, uNormTA_,
                           uPanLA_, uPanRA_, uNormA_, uniSnapA_,
                           count, detune01, blend01, width01);
            updateUnisonFramePositions();
            if (currentMidiNote_ >= 0) updateUnisonPhaseIncrementsA (glideNote_);
        }
        void setUnisonB (int count, float detune01, float blend01, float width01) noexcept
        {
            setUnisonImpl (1, activeUnisonB_, uDetuneCentsB_, uPanLTB_, uPanRTB_, uNormTB_,
                           uPanLB_, uPanRB_, uNormB_, uniSnapB_,
                           count, detune01, blend01, width01);
            updateUnisonFramePositions();
            if (currentMidiNote_ >= 0) updateUnisonPhaseIncrementsB (glideNote_);
        }

        /** SOFT BOUND (fb522) — a ceiling with an EXACTLY LINEAR core.
         *    |x| <= lin : returns x bit-for-bit.
         *    |x| >  lin : lin + (lim-lin)*tanh((|x|-lin)/(lim-lin)), signed — C1-continuous
         *                 at the knee (slope 1 on both sides) and asymptotic to +/-lim.
         *  DEVIATION, deliberate, from OVERPASS-SPEC §1.1 items 2 and 5, which proposed a
         *  GLOBAL `lim*tanh(x/lim)`. That form is nonlinear EVERYWHERE: it costs 2 % of FM
         *  depth already at the old +/-8 wall, and it turns the inert AM/RM gain of exactly
         *  1.0 into 0.9987 (-0.011 dB on every voice that has any armed blend slot). The
         *  knee form removes the audible flat-top without touching the small-signal region,
         *  which is what the spec actually wanted. */
        // ── fb551 — ONE DEFINITION OF "WHICH BLEND MODES ARE WHAT". Six separate range tests
        //  (`mode >= 1 && mode <= 4`, `mode >= 1 && mode <= 3`, `mode == 1 || mode == 2`) decided
        //  whether a slot arms, forces its source to render, ticks its sub lane, gets the 361:1
        //  taper, and phase-modulates a block engine. fb470's law is exactly this shape: a new enum
        //  value that falls into an old branch BUILDS CLEAN AND IS SILENT. Adding modes 9/10 to
        //  five of the six sites would have shipped a mode that shows in the UI, stores in the
        //  patch, and never makes a sound. They are predicates now, so that cannot recur.
        static constexpr bool blendIsPhase   (int m) noexcept { return m == 1 || m == 2 || m == 9 || m == 10; }   // writes blendOff[] — FM / PD / FM EXP / FM CLAMP
        static constexpr bool blendIsLive    (int m) noexcept { return (m >= 1 && m <= 4) || m == 6 || m == 9 || m == 10; } // has a DSP law at all (fb553 — 6 = audio-rate Warp)
        static constexpr bool blendIsFmTaper (int m) noexcept { return (m >= 1 && m <= 3) || m == 10; }           // rides the 361:1 curve (RM keeps the house curve; FM EXP is linear — see blendIsLinTaper)
        // fb551 — FM EXP TAKES A LINEAR TAPER, AND THE MEASUREMENT IS WHY. Its depth is already an
        //  EXPONENT (octaves), so putting the 361:1 curve on top makes the knob doubly exponential
        //  and crushes the whole mode into its last 15 %. MEASURED with the 361:1 taper, sine
        //  carrier at 110 Hz, spectral centroid: knob 0.5 gave 113 Hz against mode 1's 3,076 Hz,
        //  and knob 0.8 gave 367 Hz against 16,297 Hz — the mode was there and inaudible.
        //  Linear in octaves lands almost exactly on mode 1's own curve, which is not a coincidence:
        //  Δf_exp = f_c·(1024^d − 1) against Δf_fm = 96000·(361^d − 1)/360, two exponentials in d
        //  with similar bases. At C1's 110 Hz the pair reads 3,410 vs 4,800 Hz at half travel and
        //  28,050 vs 29,376 Hz at 0.8 — the same musical territory, reached by a different law.
        static constexpr bool blendIsLinTaper (int m) noexcept { return m == 9; }

        /** 2^x to ~0.05 % relative error with no libm call. Mode 9 needs ONE exp PER SAMPLE PER
         *  CARRIER; std::exp2f there is a libm call on the audio thread and the CPU-friendly rule
         *  forbids it. Same polynomial as HarmonicEngine's own private fastExp2 — duplicated on
         *  purpose rather than made public, because it is eight flops and a pure approximation:
         *  unlike a TUNING CONSTANT (fb523's law about a value living in two places) there is
         *  nothing here for two copies to disagree about. */
        static inline float fastExp2 (float x) noexcept
        {
            x = x < -60.f ? -60.f : (x > 60.f ? 60.f : x);
            const float fl = std::floor (x);
            const float f  = x - fl;
            //  ⚠️ THE LAST COEFFICIENT IS 1 − 0.69583 − 0.22606, NOT the 0.078024 the source this
            //  came from uses. With 0.078024 the polynomial gives p(1) = 1.999914 while the exponent
            //  shift gives 2.000000 on the other side, so THE FUNCTION STEPS BY 4.3e-5 AT EVERY
            //  INTEGER. HarmonicEngine only ever feeds it slow control values, where that is nothing;
            //  mode 9 feeds it an AUDIO-RATE exponent that crosses integers thousands of times a
            //  second, and each crossing is a discontinuity in the instantaneous FREQUENCY.
            //  Forcing the coefficients to sum to exactly 1 makes p(1) = 2 and the seam vanish, for
            //  8.6e-5 of extra peak approximation error — which is nothing.
            //  ⚖️ HONEST SCOPE: this was A/B'd on a stationary tone and DID NOT MOVE THE ALIAS FLOOR
            //  (−108/−115/−110/−95 dBc with, −115/−115/−104/−97 without, across knob 0.3…1.0). It is
            //  kept because a discontinuous exp2 is a defect whatever this particular metric can
            //  resolve, and removing it costs nothing. It is NOT kept on the strength of a number.
            const float p  = 1.f + f * (0.69583f + f * (0.22606f + f * 0.07811f));
            union { float fv; std::int32_t bits; } u; u.fv = p;
            u.bits += (std::int32_t) fl << 23;
            return u.fv;
        }

        static inline float softBound (float x, float lin, float lim) noexcept
        {
            const float a = std::fabs (x);
            if (a <= lin) return x;
            const float span = lim - lin;
            const float y    = lin + span * std::tanh ((a - lin) / span);
            return (x < 0.0f) ? -y : y;
        }

        /** WARP phase-domain remap (modes 1-8) — EXACT math of the original inline
         *  switches, factored so two slots chain in series (slot 2 transforms slot 1's
         *  output). Pure function of the input phase p; FORMANT's window MULTIPLIES into
         *  `window` (slot-1 entry value is 1.0 → identical to the old assign) and PWM's
         *  silence gate ORs into `skipLookup`. Modes 0/9/10 pass phase through
         *  (9/10 are amp-domain — see applyAmpWarp). */
        // ═══ SYNC / FORMANT / FRACTALIZE RATIO — ONE DEFINITION EACH (fb545) ══════════════
        //  applyPhaseWarp() AND warpRateMul() both read these. They used to be two literals in two
        //  places, which is precisely the fb523 trap ("a constant that also feeds a SELECTION path
        //  lives in two places") — reverting one and not the other picks the mip for the wrong rate,
        //  compiles clean and is silent. Naming them makes that trap structurally impossible.
        //
        //  ⚠️ WHY 4.6 AND NOT 4.0 — MEASURED, and it is not a "raise".
        //  Hard sync's harmonics come from the DISCONTINUITY at the master wrap: the slave jumps
        //  from phase frac(R) back to 0. When R is an EXACT INTEGER the slave completes a whole
        //  number of cycles per master period, the jump height is table(0) - table(0) = 0, and
        //  there is no sync at all — just a transposed table, band-limited by a mip picked for the
        //  faster rate, so the harmonic count DIVIDES BY R. That is physics, not a bug.
        //  The bug was WHERE those zeros landed. 2^(4a) puts R = 1,2,4,8,16 at a = 0, .25, .5,
        //  .75, 1.0 — every detent, both endpoints, and the double-click default.
        //      MEASURED on Terra Stack (203 peaks dry), OLD mapping:
        //          a=0.25  R=2.00  npeaks 102   (= 203/2)      a=0.30  R=2.30  npeaks 212
        //          a=0.50  R=4.00  npeaks  51   (= 203/4)      a=0.55  R=4.59  npeaks 215
        //      Serum 2 swept 0->1 under its own Sync never drops below 200 until a = 1.0 exactly.
        //  This is also the true source of the "nharm 183 -> 45 -> 8" that got fb522's raise
        //  reverted in fb523: that cert sampled 0.25 / 0.5 / 0.75 — the three dead points.
        //  4.6 clears every detent AND every tenth (R = 2.22 / 4.93 / 10.93 / 24.25 at the
        //  quarters) and raises the ceiling 16x -> 24.25x, which is the raise fb522 wanted.
        //  ⚠️ Any replacement value must be checked the same way: 2^(k*a) is an integer at
        //  a = log2(m)/k, so k must not make log2(m)/k land on a round number for small m.
        // ═══ DRAW YOUR OWN WARP SHAPE — OVERPASS ONE item 6C (fb550) ═══════════════════════
        //  Mode 37 reads a curve the user drew: w = f(p), both 0..1. The curve is OWNED by the
        //  processor (message thread draws, audio thread reads) and reaches the voice as a plain
        //  pointer into a double-buffered store, so there is no lock and no allocation here.
        //  ⚠️ `slope` is not decoration. A steep stretch of a drawn curve reads the table FASTER
        //  than 1x, exactly like Sync does, so it needs the same mip treatment or it aliases —
        //  that is what warpRateMul() uses it for. It is computed once when the curve is written.
        struct DrawCurve { float pts[129] = {}; float slope = 1.0f; };
        static constexpr int kDrawPts = 129;

        // 8 entries, [osc * 2 + slot]; any may be null (nothing drawn -> mode 37 is the identity).
        // ATOMIC because the message thread re-points a slot while this thread is reading it; the
        // buffers themselves are double-buffered on the processor side, so a load always yields a
        // whole, self-consistent curve — never a half-redrawn one.
        using DrawSlot = std::atomic<const DrawCurve*>;
        void setDrawTable (const DrawSlot* t) noexcept { drawTable_ = t; }
        // fb554 — the mod-connection curves. A pointer to the processor's ATOMIC, not to a set:
        //  the set it points at is replaced on every matrix edit, and the voice must always read
        //  the current one.
        void setModCurves (const std::atomic<const wc::ModCurveSet*>* a) noexcept { modCurves_ = a; }
        const DrawCurve* drawFor (int osc, int slot) const noexcept
        { return (drawTable_ != nullptr) ? drawTable_[(size_t) (osc * 2 + slot)].load (std::memory_order_relaxed)
                                         : nullptr; }

        /** fb561 — THE READ-RATE A WARP MODE ASKS THE MIP LAW FOR. Lifted out of renderNextBlock's
            lambda so the PROCESSOR can ask the identical question when it captures a mode into a
            drawn curve: a copy that does not inherit its source's read rate picks a different mip
            and comes back at a different level, which is the whole of "the captured warp doesn't
            sound like the warp". MEASURED before this existed: Skew asks 1.0x, its captured copy
            asked 3.1x (its own steepest ramp) and landed +2.8 dB louder; Mirror, whose map is
            almost flat, asked ~1.0x either way and was faithful to -16.7 dB. One definition. */
        static double warpReadRate (int mode, float amt, double drawSlope = 1.0) noexcept
        {
            // fb550 — a drawn curve's steepest stretch IS a read-rate multiplier: without this a
            // near-vertical hand-drawn segment reads a mip band-limited for 1x and aliases.
            // ⚠️ IT MUST TRACK `amt`. applyPhaseWarp CROSSFADES the curve in (p + amt*(c-p)),
            // so at amt = 0 the read rate is exactly 1x and asking for the full slope there
            // picks a mip for a rate nothing is reading at.
            if (mode == 37) return juce::jlimit (1.0, 64.0, 1.0 + (double) amt * (drawSlope - 1.0));
            if (mode == 2)  return std::pow (2.0, (double) amt * kSyncExp2);      // SYNC    (1..24.25x)
            if (mode == 3)  return std::pow (2.0, (double) amt * kFormantExp2);   // FORMANT (1..16x)
            if (mode == 7)  return 1.0 + (double) amt * kFractalMul;              // FRACTALIZE (1..8.5x)
            return 1.0;
        }

        static constexpr double kSyncExp2    = 4.6;  // SYNC      : R = 2^(a*k), 1 .. 24.25x
        static constexpr double kFormantExp2 = 4.0;  // FORMANT   : R = 2^(a*k), 1 .. 16x — NOT RAISED, see below
        static constexpr double kFractalMul  = 7.5;  // FRACTALIZE: R = 1 + a*k, 1 ..  8.5x

        //  ⚠️ FORMANT SHARES SYNC'S LINE BUT NOT SYNC'S DEFECT — MEASURED, DO NOT "TIDY" THESE
        //  INTO ONE CONSTANT. Formant windows the grain with sin(pi*p), and that window is zero at
        //  BOTH ends of the master period, so it removes the very discontinuity Sync lives on. Its
        //  harmonics come from the WINDOW, not from the edge, so an integer ratio costs it nothing
        //  and it never had dead detents:
        //      Terra Stack, FORMANT, pre-fb545:  a = .25/.50/.75/1.0 -> npeaks 205 / 206 / 216 / 191
        //  Raising it to 4.6 alongside Sync was measured and REVERTED in the same session:
        //      the same four points became 202 / 213 / 196 / 111 — brighter (centroid 3524 -> 4730)
        //      but a THIRD of the density gone at the top, because a bigger ratio only transposes
        //      the grain into a harder mip. That is precisely the trade fb523 reverted, and it is
        //      still a bad one. Sync is the exception, not the rule: it is the only one of the
        //      three whose content is CREATED by the discontinuity.

        static double applyPhaseWarp (int mode, float amount, double p,
                                      float& window, bool& skipLookup, float var = 0.0f,
                                      const DrawCurve* draw = nullptr) noexcept
        {
            switch (mode)
            {
                case 37:   // DRAW — the user's own phase map (fb550)
                {
                    // No curve drawn yet == the identity, so the mode is transparent the moment it
                    // is selected and the fb462 floor holds without a special case.
                    if (draw == nullptr) return p;
                    const double x  = p * (double) (kDrawPts - 1);
                    const int    i0 = juce::jlimit (0, kDrawPts - 1, (int) x);
                    const int    i1 = juce::jlimit (0, kDrawPts - 1, i0 + 1);
                    const double f  = x - (double) i0;
                    const double c  = (double) draw->pts[i0] + ((double) draw->pts[i1] - (double) draw->pts[i0]) * f;
                    // CROSSFADE FROM THE IDENTITY, so amount is a real depth control and amount 0
                    // is bit-exact dry (p + 0*(c-p) == p in IEEE-754).
                    const double w = p + (double) amount * (c - p);
                    return w - std::floor (w);
                }
                case 1:  // BEND — full-cycle phase bend
                {
                    // fb522 OVERPASS: 0.5 -> 1.0. [M] we already beat Serum's `Bend +/-` here
                    // (cen 6022 / bw99 12428 vs 1396 / 1960); chained x2 (~= this 1.0) measures
                    // cen 8336 / bw99 20930 at OOHR <= 0.0101, so the headroom is free of aliasing.
                    const double pi2 = 2.0 * 3.14159265358979323846;
                    const double w = p + (double) amount * 1.0 * std::sin (pi2 * p);
                    return w - std::floor (w);
                }
                case 2:  // SYNC — 1x..64x exponential (Vital-style)
                {
                    // fb545 — the exponent is kSyncExp2 (4.6), defined once at the top of this class
                    // with the measurement behind it. The old note here said "LIVE EXPONENT IS 4,
                    // NOT 6" and described fb522's raise being reverted for LOSING harmonics
                    // (183 -> 45 -> 8). That reversion was right about the numbers and wrong about
                    // the cause: the cert sampled a = .25/.50/.75, which 2^(4a) maps to R = 2, 4, 8
                    // EXACTLY — the three ratios at which hard sync has no discontinuity and
                    // degenerates to a transposed, harder-mipped table. It was measuring the dead
                    // detents, not a bandwidth ceiling. Terra Stack, npeaks at those detents:
                    //     4.0 -> 102 / 51 / 27 / 13        4.6 -> 200 / 215 / 212 / 210
                    const double w = p * std::pow (2.0, (double) amount * kSyncExp2);
                    return w - std::floor (w);
                }
                case 3:  // FORMANT — windowed sync (half-sine bell keyed off the input phase)
                {
                    // fb545 — kFormantExp2 is 4.0 and stays 4.0. Formant was measured alongside
                    // Sync and does NOT share its defect (the window kills the edge — full note at
                    // the constant). Raising it to 4.6 cost a third of the density at the top.
                    const double w  = p * std::pow (2.0, (double) amount * kFormantExp2);
                    const double pi = 3.14159265358979323846;
                    // fb545 — THE WINDOW NOW OBEYS THE KNOB. It used to be applied at FULL depth
                    // regardless of `amount`, so Formant at 0 was not the dry signal at all:
                    // MEASURED, peak 0.078049 (warp off) vs 0.018869 with Formant at amount 0, a
                    // different render entirely (FNV1a 420f4c70e44f4039 vs cfecb7e0141d2eb0).
                    // That is the same "not transparent at amount 0" defect this repo already
                    // files against Sine Shaper. Crossfading to 1.0 makes amount 0 EXACTLY warp-off
                    // while leaving amount 1 bit-identical to what shipped — sin() is untouched
                    // there, so the mode's character at full travel does not move.
                    window *= static_cast<float> (1.0 - (double) amount
                                                  + (double) amount * std::sin (pi * p));
                    return w - std::floor (w);
                }
                case 4:  // PWM — duty-cycle window
                {
                    // fb522 OVERPASS — BUG FIX, not a raise. With 0.45 the duty at amount = 1
                    // is 0.55, so the 0.10 floor written on this very line was UNREACHABLE and
                    // the knob only ever swept half of its intended range: [M] centroid moved
                    // 5849 -> 6481, i.e. +11 % across 90 % of the knob. 0.90 makes the floor
                    // exactly reachable at amount = 1 → a real 10 % pulse.
                    const double duty = juce::jmax (0.10, 1.0 - (double) amount * 0.90);
                    if (p >= duty) { skipLookup = true; return p; }
                    return p / duty;
                }
                case 5:  // SKEW — piecewise 2-segment peak shift
                {
                    // fb522 OVERPASS — same unreachable-floor defect as PWM: at 0.4 the knee at
                    // amount = 1 is 0.10 while the coded floor is 0.05. 0.45 makes the floor
                    // reachable. [M] the remaining bw99 gap to Serum's `Asym` (4582 vs 9419) is
                    // STRUCTURAL — a piecewise-LINEAR remap has a bounded corner spectrum. The
                    // real fix is a smooth power warp p^(1/(1+k*a)), not a larger linear span.
                    const double knee = juce::jmax (0.05, 0.5 - (double) amount * 0.45);
                    return (p < knee) ? p / knee * 0.5
                                      : 0.5 + (p - knee) / (1.0 - knee) * 0.5;
                }
                case 6:  // MIRROR — squeezed-mirror blend, VAR = fold count
                {
                    // fb522 OVERPASS: `amount` already reaches 1.0 (fully mirrored) — there is no
                    // constant here to raise. [M] two folds already MEETS Serum's `Mirror`
                    // (cen 6862 / bw99 14651 vs 6000 / 14783), so the new dimension is the FOLD
                    // COUNT and it rides the per-slot VAR: N = 1 + 3*var, var default 0 → N = 1 →
                    // the exact expression that shipped (var = 0 is bit-identical by construction).
                    const double folds = 1.0 + 3.0 * (double) var;
                    // fb523 —  is NOT a no-op at folds = 1. A phase landing
                    // marginally outside [0,1) - which unison sines do - wraps here where the
                    // shipped expression did not, and cert measured -78.4 dB of difference on
                    // Mirror at unison >= 2 with VAR at 0 (N=1 was clean; only sine 0 is in play).
                    // Inaudible, but it falsified the in-code claim "bit-identical by
                    // construction". Taking p unchanged at folds == 1 makes that claim TRUE.
                    const double qf = (folds == 1.0) ? p
                                                     : (p * folds) - std::floor (p * folds);
                    const double mirrored = (qf < 0.5) ? qf * 2.0 : 2.0 - qf * 2.0;
                    const double w = p * (1.0 - (double) amount) + mirrored * (double) amount;
                    return w - std::floor (w);
                }
                case 7:  // FRACTALIZE — fmod cascade, N = 1..13
                {
                    // fb545 — the coefficient is kFractalMul (7.5), defined once at the top. 1 + 7a
                    // put R = 8 EXACTLY at a = 1.0, so the knob's maximum was its one dead point:
                    // npeaks 27 at full travel against 204 just below it. 7.5 moves that zero off
                    // the end of the knob — measured 215 / 213 / 215 / 208 across the detents.
                    const double w = p * (1.0 + (double) amount * kFractalMul);   // fb545 — was 7.0; the
                    // only dead point 1+7a had was the MAX (a=1 -> R=8 exactly). 7.5 moves it off the end.
                    return w - std::floor (w);
                }
                case 8:  // P-QUANTIZE — phase staircase, 32→2 steps, QUARTER-sampled.
                {
                    // FIX (Max 2026-06-11 + 2026-06-27): exponential 32→2 steps (never 1).
                    // The 2026-06-11 pass CENTER-sampled (+0.5), but the center phases 0.25/0.75
                    // are the odd-harmonic NULLS of the cosine-phase tables (Square/Pulse/Minimoog,
                    // Wavetable.h cosPhase=π/2), so P-Quantize went SILENT at low even step counts
                    // (steps=2 total silence past ~92%, steps=6 a ~18% dip at ~58-63% = Max's "64%").
                    // A QUARTER offset (+0.25) is grid-misaligned with BOTH the cosine nulls
                    // (0.25/0.75) AND the sine nulls (0/0.5), so no table convention can zero every
                    // sample point — audible across the whole 0–100% range; at steps=2 it reads a
                    // clean hard 2-step square at ±table peaks. (RT-safe, DSP-only, no UI mirror.)
                    // ⛔ fb522 OVERPASS — CONSTANT DELIBERATELY NOT CHANGED. The spec proposes
                    // 2^(9-8a) (512 -> 2 steps); it is blocked on an unexplained defect and the
                    // investigation is written here so nobody re-opens it from scratch:
                    //  (a) THE +63 % AT a = 0 IS NOT A BUG, IT IS THE RANGE. At a = 0 this line
                    //      gives steps = 32, not infinity — a 32-point sample-and-hold of the
                    //      wavetable. [M] cen 6362 vs the bare carrier's 3901 is exactly what a
                    //      32-point S&H does. There is no transparent end of this knob to find;
                    //      the floor of the range IS the defect, which is what 2^(9-8a) fixes.
                    //  (b) THE "DEAD KNOB" IS PARTLY A BLIND METRIC. A phase staircase makes the
                    //      output piecewise-constant, so its spectrum is dominated by the STEP
                    //      DISCONTINUITY, whose tail is -6 dB/oct regardless of how many steps
                    //      there are. 32 small edges and 2 large edges have nearly the same
                    //      spectral centroid — hence [M] +4.6 % end-to-end on cen and +5.4 % on
                    //      bw99 for a transform that plainly changes the timbre. Re-measure with
                    //      harmonic COUNT / fundamental-to-first-image ratio before re-lawing.
                    //  (c) OOHR IS BLIND HERE TOO, for a related reason: `steps` is an INTEGER,
                    //      so every S&H image lands at a multiple of steps*f0 — on the harmonic
                    //      grid — and an out-of-harmonic ratio can never see it. [M] OOHR <=
                    //      0.0016 across the sweep is therefore not evidence of cleanliness.
                    //  (d) A SEPARATE, REAL fb325 BREACH: std::round() makes this a STAIRCASE in
                    //      the knob, not a curve — steps = 2 for every a >= 0.9195, so the top
                    //      8 % of the knob is a hard plateau, and there are only 31 distinct
                    //      values in total. A re-law must also crossfade between adjacent integer
                    //      step counts, or it ships a new plateau on top of an unknown.
                    const double steps = std::round (std::pow (2.0, 5.0 - 4.0 * (double) amount));
                    return (std::floor (p * steps) + 0.25) / steps;
                }
                default: return p;   // NONE (0) + every AMP-domain mode (9-34) + the reserved tail
            }
        }

        /** WARP amp-domain stage (modes 9-34) — applied post-lookup, per slot.
         *  `var` is the per-slot VAR knob (SYN_OSC_x_WVAR / _W2VAR), default 0 = the exact
         *  expressions that shipped. Defaulted so the waterfall call site in
         *  PluginProcessor.cpp (getOscWavetableJson, 745-762) still compiles untouched. */
        static float applyAmpWarp (int mode, float amount, float s, float var = 0.0f,
                                   const DrawCurve* draw = nullptr) noexcept
        {
            /* fb559 — MODE 38 `Draw Amp`. Mode 37 `Draw` is a PHASE map, so "capture this warp
               mode's shape and edit it" only ever worked for the eight phase-domain modes — on
               Rectify or Sine Fold there was nothing to capture INTO, and the curve card could
               only show you the shape and not let you own it (Max: "every new warp mode I open,
               it shows us the exact shape and all we get to do is edit and customize and create
               our own custom warp mode").
                 38 is that mode for the amp domain, and it costs no new storage: it reads the
               SAME per-slot drawn table 37 does (drawTable_[osc*2+slot]) — the MODE decides
               whether those 129 points mean "where in the cycle to read" or "what to do to the
               sample". Input -1..1 maps to the curve's x; the curve's 0..1 y maps back to -1..1.
               Crossfaded from the identity by amount, so amount 0 is bit-exact dry (s + 0*(c-s)
               == s in IEEE-754) and the fb462 floor gate holds with no special case. */
            if (mode == 38)
            {
                if (draw == nullptr) return s;
                const float x  = juce::jlimit (0.0f, 1.0f, s * 0.5f + 0.5f) * (float) (kDrawPts - 1);
                const int   i0 = juce::jlimit (0, kDrawPts - 1, (int) x);
                const int   i1 = juce::jlimit (0, kDrawPts - 1, i0 + 1);
                const float f  = x - (float) i0;
                const float c  = (draw->pts[i0] + (draw->pts[i1] - draw->pts[i0]) * f) * 2.0f - 1.0f;
                return s + amount * (c - s);
            }
            // 🔑 THE GATE. Modes 0-8 are NONE + the eight phase-domain warps, i.e. every patch
            // that selects no shaper — which is every patch that exists today. ONE compare and
            // they are out, before any of the shaper family is even considered. This is also
            // CHEAPER than what shipped (two compares to fall through to `return s`), so the
            // bit-identity gates stay green and the idle cost goes DOWN, not up.
            if (mode < 9) return s;
            if (mode == 9)         // RECTIFY: blend dry with |x|×2−1 by amount, VAR = pre-gain
            {
                // fb522 OVERPASS: [M] we already beat Serum's `Rectify` by 1.89x on centroid and
                // 5.32x on bw99, so there is no parity gap — the headroom goes on VAR as a
                // PRE-GAIN (1 + 3*var), default 0 → `pre` is exactly 1.0f and `s * 1.0f == s`,
                // i.e. bit-identical. ⚠️ [M] pre-gain 2 measures OOHR 0.056 — over the 2 % budget.
                // VAR > 0 is the documented trigger for the 2x oversampling insertion point
                // (spec §1.2 oversampling plan), which is NOT in this wave: treat var as
                // destructive-by-request until it lands.
                // fb524 — the four DC gates that used to key on the literal `warpMode == 9`
                // now ask warpAmpNeedsDc() instead (below). Rectify's answer is unchanged:
                // true whenever amount > 0.001, at any var.
                const float pre  = 1.0f + 3.0f * var;
                const float sd   = s * pre;
                const float rect = std::abs (sd) * 2.0f - 1.0f;
                return s * (1.0f - amount) + rect * amount;
            }
            if (mode == 10)        // SINE SHAPER: sin(x × π/2 × (1 + amount×4))
            {
                // ⛔ fb522 OVERPASS — CONSTANT DELIBERATELY NOT RAISED. [M] this knob TURNS OVER
                // at 80 %: centroid climbs 4025 -> 11440 at a = 0.8 and then FALLS to 8855 at
                // a = 1.0, because sin() folds back through its own turning point. Its a = 0.8
                // peak already BEATS Serum's Sine Shaper (10305), and it is already outside the
                // 2 % aliasing budget inside its shipped range (OOHR 0.0177 at 0.7, 0.0570 at
                // 0.8; 1 % at a ~= 0.65). Raising the 4.0 moves the turnover DOWN the knob and
                // buys nothing. It needs a re-law (1 + 3.2*a, so the brightest point lands at
                // a = 1) plus 2x oversampling above a = 0.60 — one commit, not this one.
                const float drive = 1.0f + amount * 4.0f;
                return std::sin (s * (float) (3.14159265358979323846 * 0.5) * drive);
            }
            // 11..34 — THE SHAPER ROSTER (Shapers.h warpShaper). Pure, stateless, allocation-free,
            // so the waterfall display gets all 24 for free through getOscWavetableJson (fb458).
            // 35..47 are the RESERVED tail and fall through to identity there, by design.
            return tw::shapers::warpShaper (mode, amount, s, var);
        }

        /** ⛔ THE CARDINALITY. SYN_OSC_x_WARP_MODE / _WARP2_MODE is choice(48): 0-10 the shipped
         *  eleven, 11-34 the shaper roster, 35-47 RESERVED. It is FROZEN here (RACK LAW C) —
         *  growing a choice param renumbers every saved patch AND every host automation lane, and
         *  the UI normalises a selection by an array length (index.html), so the two counts must
         *  agree forever. Add a mode by filling a RESERVED slot, never by appending a 49th. */
        static constexpr int kWarpModeMax = 47;

        /** Does this amp-domain warp mode put DC on the output?
         *  Answers TRUE by DEFAULT — that is deliberate. The four DC gates below used to key on
         *  the literal `warpMode_ == 9`, so a new mode that rectifies silently shipped a DC
         *  offset; that is the fb470 failure class in its quietest form. Now the ONLY way to
         *  avoid the blocker is to be listed as provably odd-symmetric, so forgetting to think
         *  about it costs a 38 Hz high-pass and never a broken patch.
         *  ⚠️ Modes 0-8 (phase domain) and 10 (Sine Shaper, odd) return false, which is what
         *  keeps every shipped patch bit-identical. */
        // ═══ WARP FILTER — modes 35 (LP) / 36 (HP). OVERPASS ONE item 4. ═══════════════════════
        //  Serum has LPF and HPF as per-oscillator warp modes (measured off its own parameter
        //  strings: its warp list is 70 long, LPF at index 17 and HPF at 18). These are ours.
        //
        //  🔑 WHY THIS RUNS ON THE SUMMED OSC AND NOT PER UNISON SINE. A filter is a LINEAR
        //     operator, so sum(f(x_i)) == f(sum(x_i)) EXACTLY, as long as every sine gets the same
        //     coefficients. Filtering after the unison sum is therefore not an approximation, it is
        //     the same result for 1/16 of the work and 1/16 of the state (12 KB across all 96
        //     voices instead of ~530 KB). ⚠️ The one case where it diverges is UWARP ≠ 0, which fans
        //     the warp AMOUNT per sine so the coefficients would differ; the filter takes the
        //     osc's un-fanned amount there. That is a deliberate trade, not an oversight.
        //
        //  UNITS, per the fb467 content-independent law and the Low/High cuts that already ship:
        //  the corner is in HARMONIC NUMBER, so it rides the note instead of sitting at a fixed Hz.
        //     amount 0 -> 128 harmonics  = wide open = TRANSPARENT (the fb462 floor law: every warp
        //                                  mode must be the identity at 0)
        //     amount 1 -> 1 harmonic     = only the fundamental survives
        //  ⚠️ This is the OPPOSITE DIRECTION to Serum, measured: raising their warp ADDS harmonics,
        //     so their 0 is the closed end. Ours obeys our own transparency-at-zero law instead.
        //  VAR is the resonance (Q 0.5 .. 10), which is what the slot's second dimension is for.
        struct WarpFiltCoef { float g = 0.0f, k = 2.0f, a1 = 1.0f, a2 = 0.0f, a3 = 0.0f;
                              bool hp = false, on = false; };
        struct WarpFiltState { float ic1 = 0.0f, ic2 = 0.0f; };

        static inline void warpFiltCoef (WarpFiltCoef& c, int mode, float amount, float var,
                                         double f0Hz, double sr) noexcept
        {
            c.on = (mode == 35 || mode == 36) && amount > 0.001f && f0Hz > 0.0 && sr > 0.0;
            if (! c.on) return;
            c.hp = (mode == 36);
            const double harm = std::pow (128.0, 1.0 - (double) amount);      // 128 -> 1
            const double fc   = juce::jlimit (20.0, sr * 0.45, f0Hz * harm);
            const double g    = std::tan (3.14159265358979323846 * fc / sr);
            const float  Q    = 0.5f + juce::jlimit (0.0f, 1.0f, var) * 9.5f;  // 0.5 .. 10
            c.k  = 1.0f / Q;
            c.g  = (float) g;
            c.a1 = (float) (1.0 / (1.0 + g * (g + (double) c.k)));
            c.a2 = (float) (g * (double) c.a1);
            c.a3 = (float) (g * (double) c.a2);
        }
        /** fb553 — THE SAME COEFFICIENTS, CHEAP ENOUGH TO RECOMPUTE EVERY SAMPLE.
         *  warpFiltCoef() runs once per block, which is right for a knob and useless for a cutoff
         *  being swept at audio rate: MEASURED, blend mode 6 on warp mode 35 changed NOTHING —
         *  npeaks 1, centroid 110 Hz, identical to the static filter — because the per-sample warp
         *  amount never reached the filter at all. That is the silently-inert failure this codebase
         *  keeps paying for (fb470), and "the mode is live but does nothing on two of its 37
         *  settings" is exactly the shape of it.
         *  So: no std::tan and no std::pow on the audio thread. The corner is f0·128^(1−a), so
         *  x = π·f0/fs · 2^(7(1−a)) — one fastExp2 — and tan(x) comes from a Padé [3/2], exact to
         *  0.1 % through the musical range and 3 % at the very top of the sweep, where the filter is
         *  wide open and the error is a cutoff nobody can hear. ~20 flops, and only when armed.
         *  ⚠️ THE STATIC PATH STILL USES std::tan. That is deliberate: depth 0 does not arm, so an
         *  unmodulated filter is bit-identical to what shipped, and the approximation can never
         *  move a patch that is not using this mode. */
        static inline void warpFiltFast (WarpFiltCoef& c, float kx, float xMin, float amount) noexcept
        {
            float x = kx * fastExp2 (7.0f * (1.0f - amount));
            x = juce::jlimit (xMin, 1.41372f, x);            // the static path's own [20 Hz, 0.45·sr] clamp
            const float x2 = x * x;
            const float g  = x * (15.0f - x2) / (15.0f - 6.0f * x2);   // tan, Padé [3/2]
            c.g  = g;
            c.a1 = 1.0f / (1.0f + g * (g + c.k));
            c.a2 = g * c.a1;
            c.a3 = g * c.a2;
        }
        // Zavalishin TPT state-variable filter — one multiply-add chain, no allocation, no branches.
        static inline float warpFiltTick (const WarpFiltCoef& c, WarpFiltState& st, float v0) noexcept
        {
            const float v3 = v0 - st.ic2;
            const float v1 = c.a1 * st.ic1 + c.a2 * v3;
            const float v2 = st.ic2 + c.a2 * st.ic1 + c.a3 * v3;
            st.ic1 = 2.0f * v1 - st.ic1;
            st.ic2 = 2.0f * v2 - st.ic2;
            const float out = c.hp ? (v0 - c.k * v1 - v2) : v2;
            return std::isfinite (out) ? out : 0.0f;      // resonance + a pathological corner
        }

        static inline bool warpAmpNeedsDc (int mode, float amount, float var) noexcept
        {
            if (mode < 9 || amount <= 0.001f) return false;
            switch (mode)
            {
                case 10:                                        // Sine Shaper — odd
                case 22: case 24: case 25: case 32: case 33:    // odd-symmetric shapers
                    return false;
                case 11: case 12: case 13: case 14: case 15: case 16: case 18:
                case 20: case 23: case 26: case 29: case 34:
                    return var > 0.0f;                          // symmetric until VAR biases them
                case 37:
                    return false;   // 🚨 DRAW is PHASE-DOMAIN (fb550). `default: return true` would
                                    // arm the 38 Hz blocker on a mode that cannot make DC.
                case 35: case 36:
                    return false;   // 🚨 WARP FILTER. Falling into `default: return true` below
                                    // would arm the 38 Hz DC blocker on top of a LOW-PASS and
                                    // quietly high-pass it — the fb470 trap, exactly.
                case 38:
                    return true;    // fb559 — DRAW AMP. An arbitrary drawn transfer curve is the
                                    // ONE amp mode that can make DC at any var, so `true` is the
                                    // right answer — but it is written out rather than left to
                                    // fall into `default`, because fb470's law is that a new enum
                                    // value must never inherit an old branch by accident, even
                                    // when the branch happens to be correct.
                default: return true;                           // 9, 17, 19, 21, 27, 28, 30, 31 + anything new
            }
        }

        /** PORTAMENTO context, pushed per-block from the processor. fromNote = the last
         *  synth note (glide origin); anyHeld = a synth note was sounding (ALWAYS-off gate). */
        void setGlide (float portaTimeSec, float curve01, bool always, bool scaled,
                       float fromNote, bool anyHeld) noexcept
        {
            portaTime_     = juce::jmax (0.0f, portaTimeSec);
            glideCurve_    = juce::jlimit (0.0f, 1.0f, curve01);
            glideAlways_   = always;
            glideScaled_   = scaled;
            glideFromNote_ = fromNote;
            glideAnyHeld_  = anyHeld;
        }

        /** LEGATO — arm the next startNote() to retarget pitch WITHOUT retriggering
         *  envelopes/phases/waver. Set by UnisonSynth immediately before startVoice(). */
        void beginLegatoRetarget() noexcept { legatoRetarget_ = true; }

        /** LEGATO glide — slide from the CURRENT sounding pitch (even mid-glide) to the
         *  new note. Overlapped notes always glide when porta > 0: ALWAYS gates only
         *  fresh attacks, and a legato overlap is by definition the held case. */
        void beginGlideLegato (double fromPitch, int targetNote) noexcept
        {
            glideTarget_ = (double) targetNote;
            if (portaTime_ > 1.0e-4f && fromPitch != glideTarget_)
            {
                glideStart_    = fromPitch;
                glideNote_     = fromPitch;
                glideProgress_ = 0.0;
                const double dist   = std::abs (glideTarget_ - glideStart_);
                const double durSec = glideScaled_ ? ((double) portaTime_ * dist / 12.0)
                                                   : (double) portaTime_;
                glideDurSamples_ = juce::jmax (1.0, durSec * sampleRate_);
            }
            else
            {
                glideStart_ = glideNote_ = glideTarget_;
                glideProgress_ = 1.0;
                glideDurSamples_ = 1.0;
            }
        }

        /** Set up the glide for a note-on. Returns the starting pitch (glideNote_).
         *  Snaps when porta is off / no origin / (ALWAYS off and nothing held). */
        // fb122 ROBIN Glide — like beginGlideLegato but with an explicit duration
        // (independent of the global portamento time, which is usually 0 in poly)
        void beginGlideRobin (double fromPitch, int targetNote, float durSec) noexcept
        {
            glideTarget_ = (double) targetNote;
            if (fromPitch == glideTarget_) return;
            glideStart_    = fromPitch;
            glideNote_     = fromPitch;
            glideProgress_ = 0.0;
            glideDurSamples_ = juce::jmax (1.0, (double) durSec * sampleRate_);
        }

        void beginGlide (int targetNote) noexcept
        {
            glideTarget_ = (double) targetNote;
            const bool doGlide = (portaTime_ > 1.0e-4f)
                              && (glideFromNote_ >= 0.0f)
                              && (glideAlways_ || glideAnyHeld_)
                              && ((double) glideFromNote_ != glideTarget_);
            if (doGlide)
            {
                glideStart_    = (double) glideFromNote_;
                glideNote_     = glideStart_;
                glideProgress_ = 0.0;
                const double dist = std::abs (glideTarget_ - glideStart_);   // semitones
                const double durSec = glideScaled_ ? ((double) portaTime_ * dist / 12.0)  // const rate (per-octave)
                                                   : (double) portaTime_;                 // fixed total time
                glideDurSamples_ = juce::jmax (1.0, durSec * sampleRate_);
            }
            else
            {
                glideStart_ = glideNote_ = glideTarget_;
                glideProgress_ = 1.0;
                glideDurSamples_ = 1.0;
            }
        }

        /** Advance the glide by `numSamples`. Linear progress shaped by glideCurve_:
         *  exp = 4^((curve−0.5)·2) → curve 0 = ease-out (exp ¼, fast start), 0.5 = linear,
         *  curve 1 = ease-in (exp 4, slow start). */
        void advanceGlide (int numSamples) noexcept
        {
            if (glideProgress_ >= 1.0) { glideNote_ = glideTarget_; return; }
            glideProgress_ = juce::jmin (1.0, glideProgress_ + (double) numSamples / glideDurSamples_);
            const double exp    = std::pow (4.0, ((double) glideCurve_ - 0.5) * 2.0);
            const double shaped = std::pow (glideProgress_, exp);
            glideNote_ = glideStart_ + (glideTarget_ - glideStart_) * shaped;
            if (glideProgress_ >= 1.0) glideNote_ = glideTarget_;
        }
        /** UNISON STACK (fb522) — the semitone layers of each SYN_OSC_x_USTACK option, in the
         *  C++ option order registered by Lane P:
         *    0 Off · 1 "12 (1x)" · 2 "12 (2x)" · 3 "12 (3x)" ·
         *    4 "12+7 (1x)" · 5 "12+7 (2x)" · 6 "12+7 (3x)" · 7 Center-12 · 8 Center-24
         *  [M] Center-12 is the big one: an octave-down layer contributes a complete
         *  HALF-INTEGER partial series (0.5x, 1.5x, 2.5x … f0) and its fundamental measures
         *  5.9 dB ABOVE the played pitch. Center-24 gives the quarter-integer series. */
        static constexpr int kStackSemis[9][7] = {
            {   0,  0,  0,  0,  0,  0,  0 },   // 0 Off (never read — the add is skipped)
            {   0, 12,  0,  0,  0,  0,  0 },   // 1  12 (1x)
            {   0, 12, 24,  0,  0,  0,  0 },   // 2  12 (2x)
            {   0, 12, 24, 36,  0,  0,  0 },   // 3  12 (3x)
            {   0,  7, 12,  0,  0,  0,  0 },   // 4  12+7 (1x)
            {   0,  7, 12, 19, 24,  0,  0 },   // 5  12+7 (2x)
            {   0,  7, 12, 19, 24, 31, 36 },   // 6  12+7 (3x)
            { -12,  0,  0,  0,  0,  0,  0 },   // 7  Center-12
            { -24,  0,  0,  0,  0,  0,  0 } }; // 8  Center-24
        static constexpr int kStackLayers[9] = { 1, 2, 3, 4, 3, 5, 7, 2, 2 };

        void setUnisonImpl (int osc,
                            int& activeCount,
                            std::array<float, kMaxUnison>& detCents,
                            std::array<float, kMaxUnison>& panL,
                            std::array<float, kMaxUnison>& panR,
                            float& norm,
                            std::array<float, kMaxUnison>& panLLive,   // fb204 — glide pair: this fn writes
                            std::array<float, kMaxUnison>& panRLive,   // TARGETS; the render loop glides the
                            float& normLive, bool& snapped,            // live tables (Width/Blend de-zipper)
                            int count, float detune01, float blend01, float width01) noexcept
        {
            const int oldCount = activeCount;
            activeCount = juce::jlimit (1, kMaxUnison, count);
            const float det = juce::jlimit (0.0f, 1.0f, detune01);
            const float bl  = juce::jlimit (0.0f, 1.0f, blend01);
            // fb522 — WIDTH is BIPOLAR now (SYN_OSC_x_UWIDTH is -100..+100). The old
            // jlimit(0,1) swallowed the whole negative half SILENTLY: it built clean, the UI
            // moved, the sound never changed — fb470's exact failure class.
            const float wid = juce::jlimit (-1.0f, 1.0f, width01);
            // fb522 — cache the args so advanceUnisonRangeGlide() can re-run this function at
            // block rate while URANGE glides, with everything else held exactly as pushed.
            uniArgs_[(size_t) osc] = { count, detune01, blend01, width01 };
            const float range = uniRangeSm_[(size_t) osc];
            const int   stack = uniStack_[(size_t) osc];
            const int   nLay  = kStackLayers[stack];
            // a stack needs at least one WHOLE voice per layer; below that it is inert, which
            // is what [M] Serum does ("12 (2x)" is bit-identical to Off at 2 voices).
            const int   per   = (stack > 0) ? (activeCount / nLay) : 0;

            float gainSq = 0.0f;   // Σ (panL² + panR²) = Σ blendGain² → auto-gain
            // fb523 — THE MODULATOR-TAP PAN CORRECTION (the 3 dB bug).
            //  modPrev_[] taps each osc as the MONO SUM of its PANNED pair, 0.5·(L+R). A centred
            //  voice is panned EQUAL-POWER at cos(π/4) = sin(π/4) = 0.7071, so that mono sum is
            //  0.5·(0.7071x + 0.7071x) = 0.7071x — every blend modulator arrived 3.01 dB DOWN, and
            //  its depth VARIED with unison Width and count (a hard-panned pair gives 0.5·(1+0)=0.5).
            //  [M] FM at 100 % measured β = 33.946 where drive 48 predicts 48.000; 48/√2 = 33.9411,
            //  a 0.014 % match. (The RMS-normalised-table hypothesis is REFUTED: Wavetable.h states
            //  "Normalizes each mip level so peak == 1.0" in three places. The pan law is the cause.)
            //  THE FIX, and why it is a scalar rather than a second accumulator: the per-voice
            //  amplitude weight g is pan-INDEPENDENT by construction (panL² + panR² = g²·(cos²+sin²)
            //  = g²), so the pan-invariant mono tap is norm·Σ s_u·g_u while the shipped tap is
            //  norm·Σ s_u·g_u·0.5(cosθ_u + sinθ_u). Their ratio is a constant of the unison
            //  geometry alone — Σg / Σ g·0.5(cos+sin) — so ONE multiply at the tap site restores
            //  pan- and width-invariance and leaves the whole per-osc chain (warp, spectral,
            //  blendAmp, sub) inside the tap where it has always been.
            //  EXACT for unison = 1 (one voice, no weighting ambiguity: correction = √2), and exact
            //  in both the correlated and the decorrelated limit whenever the mirror pairs share g
            //  — which the fb255 blend law guarantees. Between those limits it is a weighting
            //  approximation whose error is bounded by the spread of 0.5(cos+sin) ∈ [0.5, 0.7071].
            float monoNum = 0.0f, monoDen = 0.0f;
            for (int u = 0; u < activeCount; ++u)
            {
                if (activeCount <= 1)
                {
                    detCents[(size_t) u] = 0.0f;
                    panL[(size_t) u] = 0.7071f;
                    panR[(size_t) u] = 0.7071f;
                    gainSq += 1.0f;     // 0.7071² + 0.7071² = 1 (centre voice, equal power)
                    monoNum += 1.0f; monoDen += 0.7071f;   // fb523 — correction = 1/0.7071 = √2 exactly
                    continue;
                }
                const float u_norm = ((float) u / (float) (activeCount - 1)) * 2.0f - 1.0f;  // -1..+1
                // fb522 — DETUNE = TAPER × RANGE (was: taper only, against a hard-coded 50 c).
                //  · RANGE is the new SYN_OSC_x_URANGE knob, 5..4800 cents of half-spread. Its
                //    default 50.0 reproduces the retired kUniMaxDetuneCents exactly.
                //  · TAPER is d^2.5. [M] Serum's total spread fits 400·d^2.5 EXACTLY at every
                //    one of five knob points (1.2649/12.500/70.711/194.87/400.00 predicted vs
                //    1.26/12.50/70.71/194.86/400.00 measured); ours was pure linear (100·d).
                //    ⚠️ [C] d^2.5 is NOT a no-op at the default range: at d = 0.25 it gives
                //    1.5625 c where linear gave 12.5 c. It IS exactly invertible —
                //    d_new = d_old^0.4 — so a migration can restore every stored patch. That
                //    migration is a HANDOFF to the parameter lane; it is not in this file.
                const float detT = det * det * std::sqrt (det);                 // d^2.5
                float cents = u_norm * detT * range;
                if (per >= 1)   // STACK — whole-semitone layers, engine-agnostic (it is just cents)
                    cents += 100.0f * (float) kStackSemis[stack][juce::jmin (nLay - 1, u / per)];
                detCents[(size_t) u] = cents;
                // BLEND — centre voice (u_norm≈0) full, outer voices scaled toward `blend`, SYMMETRICALLY
                // in |u_norm| (a voice and its mirror always share a gain → equal L/R energy).
                // fb255 — was `(u==0) ? 1.0f : …`, which pinned the LEFTMOST voice (u=0, u_norm=-1) to full
                // gain while its right mirror scaled down → the unison leaned LEFT (Max's bug, worse at high
                // Width). The floor keeps UNISON=2/BLEND=0 audible (the anchor's real job) without the bias.
                const float g = std::fmax (kUniBlendFloor, 1.0f - (1.0f - bl) * std::fabs (u_norm));
                // ── WIDTH — THE STEREO LAW (fb522) ────────────────────────────────────────
                // WAS: `angle = (u_norm*wid + 1)*π/4`, an ORDERED fan. Its energy-pan position
                // works out to −cos(π·u/(N−1)), which leaves the inner voices at only −0.105 at
                // N = 16 — nearly centred. The model predicts side/mid = 0.2542 there and the
                // harness MEASURED 0.2470, so the model IS the shipped law.
                // Serum measures corr −0.0005 / side-mid 1.0009 at N = 16 (fully decorrelated)
                // against our +0.6039 / 0.2470 = a 6.07 dB side-energy deficit. That is
                // alternate hard-panning, and the same model gives
                //     side/mid = (1 − cos(π·|w|/2)) / (1 + cos(π·|w|/2))
                // → exactly 0 at w = 0 (mono; side energy stays exactly 0.000e+00, which we
                // already beat Serum on) and exactly 1.0000 at |w| = 1.
                // The side assignment is ANTISYMMETRIC under the mirror u ↔ (N−1−u). That is
                // what protects the three things that must not move:
                //   · a mirror pair shares |u_norm|, hence shares the fb255 blend gain g, so L
                //     and R always carry the SAME multiset of gains → L/R balance is exact for
                //     every voice count, even AND odd (an odd count's centre voice maps to
                //     itself and stays centred). cos((1−w)π/4) = sin((1+w)π/4), so the two
                //     sides are exact mirrors of each other.
                //   · the auto-gain is not merely preserved, it is ALGEBRAICALLY UNTOUCHED:
                //     panL² + panR² = g²·(cos²θ + sin²θ) = g², and g does not depend on the pan
                //     law at all — so gainSq, and therefore norm = 1/√Σg², is bit-identical to
                //     the old law for every count, blend and width. ([M] flat to 0.08 dB across
                //     voice count, which BEATS Serum's +4.21 dB drift — we keep that win.)
                //   · N = 1, 2 and 3 reproduce the OLD law exactly (sides −1/+1 and 0), which
                //     is why [M] Terrain already matched Serum at N = 2.
                // Negative `wid` mirrors the field, which is what the bipolar knob now means.
                const int   mirror = activeCount - 1 - u;
                const float side   = (u == mirror) ? 0.0f
                                   : (u <  mirror) ? ((u      & 1) ?  1.0f : -1.0f)
                                                   : ((mirror & 1) ? -1.0f :  1.0f);
                // WIDTH — equal-power pan, angle in [0, π/2]; BLEND gain folded into the table
                // so the render loop stays a plain sAu·pan multiply.
                const float angle = (side * wid + 1.0f) * 0.25f * juce::MathConstants<float>::pi;
                panL[(size_t) u] = std::cos (angle) * g;
                panR[(size_t) u] = std::sin (angle) * g;
                gainSq += panL[(size_t) u] * panL[(size_t) u] + panR[(size_t) u] * panR[(size_t) u];
                monoNum += g;                                                        // fb523 — pan-INDEPENDENT weight
                monoDen += 0.5f * (panL[(size_t) u] + panR[(size_t) u]);            // fb523 — == g·0.5(cosθ+sinθ)
            }
            updateUniWarpSpread (osc);   // fb522 — per-sine WARP fan depends on activeCount
            for (int u = activeCount; u < kMaxUnison; ++u)
            {
                detCents[(size_t) u] = 0.0f;
                panL[(size_t) u] = 0.0f;
                panR[(size_t) u] = 0.0f;
            }
            // AUTO-GAIN — RMS-constant: holds perceived loudness as voices/blend change.
            norm = (gainSq > 1.0e-9f) ? (1.0f / std::sqrt (gainSq)) : 1.0f;
            monoTapCorrT_[(size_t) osc] = (monoDen > 1.0e-9f) ? (monoNum / monoDen) : 1.0f;   // fb523 — √2 at unison 1
            // fb204 — structural change (voice count) = a NEW stereo image, snap don't glide;
            // continuous Width/Blend moves ride the per-sample one-pole in the render loop.
            if (! snapped || activeCount != oldCount)
            { panLLive = panL; panRLive = panR; normLive = norm; snapped = true; monoTapCorr_[(size_t) osc] = monoTapCorrT_[(size_t) osc]; }
        }

        // ── fb522 OVERPASS — unison RANGE / WARP-SPREAD / STACK plumbing ────────────────
        /** Re-run setUnisonImpl for one osc with its cached args (used by the RANGE glide). */
        void reapplyUnison (int osc) noexcept
        {
            const UniArgs a = uniArgs_[(size_t) osc];
            switch (osc)
            {
                case 0: setUnisonImpl (0, activeUnisonA_, uDetuneCentsA_, uPanLTA_, uPanRTA_, uNormTA_, uPanLA_, uPanRA_, uNormA_, uniSnapA_, a.count, a.det, a.blend, a.width); break;
                case 1: setUnisonImpl (1, activeUnisonB_, uDetuneCentsB_, uPanLTB_, uPanRTB_, uNormTB_, uPanLB_, uPanRB_, uNormB_, uniSnapB_, a.count, a.det, a.blend, a.width); break;
                case 2: setUnisonImpl (2, activeUnisonC_, uDetuneCentsC_, uPanLTC_, uPanRTC_, uNormTC_, uPanLC_, uPanRC_, uNormC_, uniSnapC_, a.count, a.det, a.blend, a.width); break;
                default:setUnisonImpl (3, activeUnisonD_, uDetuneCentsD_, uPanLTD_, uPanRTD_, uNormTD_, uPanLD_, uPanRD_, uNormD_, uniSnapD_, a.count, a.det, a.blend, a.width); break;
            }
        }
        void setUniRangeImpl (int osc, float cents) noexcept
        {
            const float t = juce::jlimit (5.0f, 4800.0f, cents);
            uniRangeT_[(size_t) osc] = t;
            if (! uniRangeSeeded_[(size_t) osc])          // first push SEEDS — never glide into tune
            {
                uniRangeSeeded_[(size_t) osc] = true;
                const float prev = uniRangeSm_[(size_t) osc];
                uniRangeSm_[(size_t) osc] = t;
                // At the default 50.0 this is a no-op against the seeded constant, so a stock
                // patch never re-enters setUnisonImpl here at all — and, importantly, a URANGE
                // push that arrives BEFORE the first setUnison broadcast cannot rebuild the pan
                // tables from the placeholder uniArgs_.
                if (t != prev) reapplyUnison (osc);
            }
        }
        void setUniWarpImpl (int osc, float bip) noexcept
        {
            const float v = juce::jlimit (-1.0f, 1.0f, bip);
            if (v == uniWarp_[(size_t) osc]) return;
            uniWarp_[(size_t) osc] = v;
            updateUniWarpSpread (osc);
        }
        void setUniStackImpl (int osc, int mode) noexcept
        {
            const int m = juce::jlimit (0, 8, mode);
            if (m == uniStack_[(size_t) osc]) return;
            uniStack_[(size_t) osc] = m;
            reapplyUnison (osc);                          // the stack lives in the detune-cents table
        }
        /** Per-sine WARP fan: sine u gets warpAmount + uwarp·u_norm. `uniWarpOn_` is the
         *  bit-identity gate — at uwarp = 0 the render loop takes the untouched `warpAmount_`
         *  path and pays literally nothing. */
        void updateUniWarpSpread (int osc) noexcept
        {
            const float w   = uniWarp_[(size_t) osc];
            const int   cnt = (osc == 0) ? activeUnisonA_ : (osc == 1) ? activeUnisonB_
                            : (osc == 2) ? activeUnisonC_ : activeUnisonD_;
            std::array<float, kMaxUnison>& t = (osc == 0) ? uWarpOffA_ : (osc == 1) ? uWarpOffB_
                                             : (osc == 2) ? uWarpOffC_ : uWarpOffD_;
            for (int u = 0; u < kMaxUnison; ++u)
            {
                const float u_norm = (cnt > 1 && u < cnt)
                                   ? ((float) u / (float) (cnt - 1)) * 2.0f - 1.0f : 0.0f;
                t[(size_t) u] = w * u_norm;
            }
            const bool on = (w != 0.0f) && (cnt > 1);
            switch (osc) { case 0: uniWarpOnA_ = on; break; case 1: uniWarpOnB_ = on; break;
                           case 2: uniWarpOnC_ = on; break; default: uniWarpOnD_ = on; break; }
        }
        /** URANGE is CONTINUOUS, so it must GLIDE, not snap (the fb204 law: a voice-count
         *  change is a new image and snaps; a continuous move rides the 2.5 ms one-pole).
         *  It is evaluated ONCE PER BLOCK because every pitch quantity in this voice is
         *  block-rate by construction — portamento itself advances in advanceGlide(numSamples)
         *  and the per-sine increments are re-derived in updateUnisonPhaseIncrements*() right
         *  below this call. A per-sample RANGE would mean re-deriving 2,048 phase increments
         *  per sample. Coefficient is the exact 2.5 ms one-pole integrated over the block.
         *  Costs nothing and is BIT-IDENTICAL when the knob is not moving: the recompute is
         *  skipped entirely once |target − current| falls under 1e-4 cents. */
        void advanceUnisonRangeGlide (int numSamples) noexcept
        {
            const float c = 1.0f - std::exp (-(float) numSamples
                                             / (0.0025f * (float) juce::jmax (1.0, sampleRate_)));
            for (int o = 0; o < 4; ++o)
            {
                const float d = uniRangeT_[(size_t) o] - uniRangeSm_[(size_t) o];
                if (std::fabs (d) < 1.0e-4f) { uniRangeSm_[(size_t) o] = uniRangeT_[(size_t) o]; continue; }
                uniRangeSm_[(size_t) o] += d * c;
                reapplyUnison (o);
            }
        }

        /** Phase 11a — Set per-OSC FRAME SPREAD (0..1). Pushed per-block from
         *  PluginProcessor broadcast. Caches the amount; per-sine offsets are
         *  recomputed in updateUnisonFramePositions() each block (and on
         *  setUnison/startNote) so SPREAD tracks UNISON count changes correctly. */
        /** WT BLUR amount per OSC (0..1). Replaces the old per-sine FRAME_SPREAD: turns
         *  the knob into a frame-blend width. Smoothed + applied per block in renderNextBlock. */
        void setBlur (float blurA01, float blurB01) noexcept
        {
            blurTargetA_ = juce::jlimit (0.0f, 1.0f, blurA01);
            blurTargetB_ = juce::jlimit (0.0f, 1.0f, blurB01);
        }

        /** PHASE mode per OSC (0=RETRIG, 1=FREE, 2=RANDOM, 3=SPREAD). Governs how each
         *  unison sine's phase accumulator is initialised at note-on. Pushed per block. */
        void setSub (int o, int range, int form, float weight, float heatK) noexcept
        {
            if (o < 0 || o > 3) return;
            const int nf = juce::jlimit (0, 3, form);
            if (nf != sub_[o].form && sub_[o].on)
            { sub_[o].formOld = sub_[o].form; sub_[o].xf = 1.f; }   // Shape MORPHS mid-note
            sub_[o].form   = nf;
            sub_[o].range  = juce::jlimit (0, 8, range);   // 9 choices: idx 0..8 → -4..+4 Oct (idx 4 = 0)
            sub_[o].weight = juce::jlimit (0.f, 1.f, weight);
            sub_[o].heatK  = juce::jlimit (0.f, 1.f, heatK);
        }

        // ── NOISE ENGINE (center module — one shared source per voice, injected into the Filter 1 bus) ──
        void setNoise (bool on, int type, float level, float pitch, float pan) noexcept
        {
            noiseOn_    = on;
            noiseType_  = type;
            noiseLvlT_ = juce::jlimit (0.0f, 1.0f, level);   // fb202 — glided at the render site
            // "Scan" (formerly Pitch): drives the noise scan/playback RATE — 0 = very slow (0.1×) … 0.5 = 1× … 1 = 2×.
            const float sc = juce::jlimit (0.0f, 1.0f, pitch);
            noiseScanRateT_ = (sc < 0.5f) ? (0.1f + 1.8f * sc) : (1.0f + 2.0f * (sc - 0.5f));
            const float th = juce::jlimit (0.0f, 1.0f, pan) * 1.5707963268f;   // equal-power pan (−3 dB center)
            noisePanLT_ = std::cos (th);   // fb202 — glide targets
            noisePanRT_ = std::sin (th);
        }
        // fb66 — NOISE play mode (sample playback): 0 Random · 1 Envelope (one-shot) · 2 Free (global tape).
        void setNoisePlayMode (int m) noexcept { noisePlayMode_ = m; }
        // fb67 — Free tape clock: just REMEMBER the latest global position (pushed once per block). A Free note
        // reads this ONCE at note-on (startNote) and then free-runs/loops exactly like Random — NO per-block
        // resync of the playing head (that resync was the source of the Free-mode background static). Voices still
        // stay ~phase-locked because they all advance at the same rate from the same tape clock.
        void setNoiseFreePos (double posSamples) noexcept { noiseFreeLatest_ = posSamples; }
        void setNoiseCarrier (bool on) noexcept { noiseCarrierTarget_ = on ? 1.0f : 0.0f; }   // fb68 — Free-mode mono gate (poly modes push true to all)
        void setNoiseWidth (float w) noexcept { noiseWidthT_ = juce::jlimit (0.0f, 2.0f, w); }   // fb69 — stereo width (M/S) · fb202 glided
        // Representative follower position 0..1 for the waveform viz (Random/Envelope read this voice's head); -1 = no sample.
        float noiseFollowPos01 () const noexcept
        { return (noiseSampLen_ > 1) ? (float) (noiseSampPos_ / (double) noiseSampLen_) : -1.0f; }
    private:
        // xorshift32 → [-1,1). Two independent L/R streams = an instantly-decorrelated stereo field.
        static inline float noiseWhite (std::uint32_t& s) noexcept
        {
            s ^= s << 13; s ^= s >> 17; s ^= s << 5;
            return (float) ((std::int32_t) s) * (1.0f / 2147483648.0f);
        }
        // Cheap phase→sine, phase in [0,1). ~1% THD — plenty for LFOs / hum / SVF sweep. No std::sin per sample.
        static inline float noiseSine (float ph) noexcept
        {
            const float x = 2.0f * ph - 1.0f;                    // [-1,1)
            const float q = 4.0f * x * (1.0f - std::fabs (x));   // parabola
            return q * (0.775f + 0.225f * std::fabs (q));        // devmaster refine
        }
        inline void noiseTick (float& oL, float& oR) noexcept
        {
            const float wl = noiseWhite (noiseRngL_), wr = noiseWhite (noiseRngR_);
            switch (noiseType_)
            {
                case 1: {   // Pink — Paul Kellet economy filter (≈ -3 dB/oct)
                    auto pk = [] (float w, float* b) noexcept {
                        b[0] = 0.99886f*b[0] + w*0.0555179f; b[1] = 0.99332f*b[1] + w*0.0750759f;
                        b[2] = 0.96900f*b[2] + w*0.1538520f; b[3] = 0.86650f*b[3] + w*0.3104856f;
                        b[4] = 0.55000f*b[4] + w*0.5329522f; b[5] = -0.7616f*b[5] - w*0.0168980f;
                        const float o = b[0]+b[1]+b[2]+b[3]+b[4]+b[5]+b[6]+w*0.5362f;
                        b[6] = w*0.115926f; return o * 0.11f;
                    };
                    oL = pk (wl, pkL_); oR = pk (wr, pkR_); break;
                }
                case 2:     // Brown — leaky integrator (≈ -6 dB/oct)
                    brL_ = (brL_ + 0.02f*wl) * 0.996f; brR_ = (brR_ + 0.02f*wr) * 0.996f;
                    oL = brL_ * 3.5f; oR = brR_ * 3.5f; break;
                case 3: {   // Geiger — dry Poisson clicks, random amplitude, crisp fast decay, NO bed
                    auto click = [] (float w, float w2, float& env) noexcept {
                        if (w > 0.9993f || w < -0.9993f)
                            env = (0.6f + 0.4f*std::fabs (w2)) * (w > 0.0f ? 1.0f : -1.0f);
                        const float o = env; env *= 0.80f; return o;
                    };
                    oL = click (wl, wr, geValL_); oR = click (wr, wl, geValR_); break;
                }
                case 4: {   // Tape Hiss — band-limited upper-mid noise (~1.5–8 kHz), gentle top roll-off
                    tpL_  += 0.21f*(wl - tpL_);   tpR_  += 0.21f*(wr - tpR_);     // HP ~1.5 kHz (remove lows)
                    const float hpL = wl - tpL_,  hpR = wr - tpR_;
                    tpL2_ += 0.55f*(hpL - tpL2_); tpR2_ += 0.55f*(hpR - tpR2_);   // LP ~6 kHz (tame top)
                    oL = tpL2_ * 2.0f; oR = tpR2_ * 2.0f; break;
                }
                case 5: {   // Tape Hum — 60 Hz + 120 + 180 harmonics (low buzz) over faint hiss
                    humPh_ += 60.0f / noiseSR_; if (humPh_ >= 1.0f) humPh_ -= 1.0f;
                    float h2 = humPh_*2.0f; if (h2 >= 1.0f) h2 -= 1.0f;
                    float h3 = humPh_*3.0f; while (h3 >= 1.0f) h3 -= 1.0f;
                    const float hum = noiseSine (humPh_)*0.70f + noiseSine (h2)*0.22f + noiseSine (h3)*0.10f;
                    tpL_ += 0.25f*(wl - tpL_); tpR_ += 0.25f*(wr - tpR_);         // faint hiss bed
                    oL = hum*0.82f + (wl - tpL_)*0.12f;
                    oR = hum*0.82f + (wr - tpR_)*0.12f; break;
                }
                case 6: {   // Tape Air — breathy bright high-shelf, slow "breathing" amplitude
                    tpL_ += 0.38f*(wl - tpL_); tpR_ += 0.38f*(wr - tpR_);         // HP ~2.7 kHz (airy top)
                    const float airL = wl - tpL_, airR = wr - tpR_;
                    windPh_ += 0.25f / noiseSR_; if (windPh_ >= 1.0f) windPh_ -= 1.0f;  // ~0.25 Hz breath
                    const float breath = 0.72f + 0.28f * noiseSine (windPh_);
                    oL = airL * 1.3f * breath; oR = airR * 1.3f * breath; break;
                }
                case 7: {   // Tape Crackle — sparse ASYMMETRIC pops over faint hiss
                    tpL_ += 0.22f*(wl - tpL_); tpR_ += 0.22f*(wr - tpR_);         // faint hiss bed
                    const float hissL = wl - tpL_, hissR = wr - tpR_;
                    auto pop = [] (float w, float w2, float& env) noexcept {
                        if      (w >  0.9995f)  env =  (0.7f + 0.3f*std::fabs (w2)); // positive-biased pop
                        else if (w < -0.99985f) env = -(0.5f + 0.3f*std::fabs (w2)); // rare negative
                        const float o = env; env *= 0.86f; return o;
                    };
                    oL = pop (wl, wr, geValL_) + hissL*0.28f;
                    oR = pop (wr, wl, geValR_) + hissR*0.28f; break;
                }
                case 8: case 9: {   // Vinyl — LF rumble + pink surface + Poisson crackle (Dirty=denser/louder)
                    const bool dirty = (noiseType_ == 9);
                    rumbL_[0] += 0.006f*(wl - rumbL_[0]); rumbL_[1] += 0.006f*(rumbL_[0] - rumbL_[1]);   // ~40 Hz turntable rumble
                    rumbR_[0] += 0.006f*(wr - rumbR_[0]); rumbR_[1] += 0.006f*(rumbR_[0] - rumbR_[1]);
                    const float rmb = dirty ? 24.0f : 18.0f;
                    auto pk = [] (float w, float* b) noexcept {                   // pink surface (reuse Kellet state)
                        b[0]=0.99886f*b[0]+w*0.0555179f; b[1]=0.99332f*b[1]+w*0.0750759f;
                        b[2]=0.96900f*b[2]+w*0.1538520f; b[3]=0.86650f*b[3]+w*0.3104856f;
                        b[4]=0.55000f*b[4]+w*0.5329522f; b[5]=-0.7616f*b[5]-w*0.0168980f;
                        const float o=b[0]+b[1]+b[2]+b[3]+b[4]+b[5]+b[6]+w*0.5362f; b[6]=w*0.115926f; return o*0.11f;
                    };
                    const float surfL = pk (wl, pkL_) * (dirty ? 0.50f : 0.22f);
                    const float surfR = pk (wr, pkR_) * (dirty ? 0.50f : 0.22f);
                    const float thr = dirty ? 0.9975f : 0.9993f;                  // Poisson crackle
                    auto crk = [thr] (float w, float w2, float& env) noexcept {
                        if (w > thr || w < -thr) env = (0.55f + 0.45f*std::fabs (w2)) * (w > 0.0f ? 1.0f : -1.0f);
                        const float o = env; env *= 0.845f; return o;
                    };
                    const float ckL = crk (wl, wr, geValL_) * (dirty ? 0.9f : 0.7f);
                    const float ckR = crk (wr, wl, geValR_) * (dirty ? 0.9f : 0.7f);
                    oL = rumbL_[1]*rmb + surfL + ckL;
                    oR = rumbR_[1]*rmb + surfR + ckR; break;
                }
                case 10: case 11: case 12: {   // Space — Chamberlin SVF resonant band-pass (tonal wash, not flat noise)
                    const float w0 = 6.2831853f / noiseSR_;
                    float fc, qd, amp;
                    if (noiseType_ == 10) {          // Space Open — broad airy wash, slow drift
                        windPh2_ += 0.07f / noiseSR_; if (windPh2_ >= 1.0f) windPh2_ -= 1.0f;
                        fc = 1100.0f + 500.0f * noiseSine (windPh2_);
                        qd = 0.90f; amp = 2.6f;
                    } else if (noiseType_ == 11) {   // Space Helium — high, thin, resonant formant
                        windPh2_ += 0.05f / noiseSR_; if (windPh2_ >= 1.0f) windPh2_ -= 1.0f;
                        fc = 3200.0f + 400.0f * noiseSine (windPh2_);
                        qd = 0.28f; amp = 1.8f;
                    } else {                          // Space Wind — gusting swept band-pass
                        windPh_  += 0.13f / noiseSR_; if (windPh_  >= 1.0f) windPh_  -= 1.0f;
                        windPh2_ += 0.09f / noiseSR_; if (windPh2_ >= 1.0f) windPh2_ -= 1.0f;
                        fc = 700.0f + 450.0f * noiseSine (windPh2_);
                        qd = 0.60f;
                        gustL_ += 0.00035f*(wl - gustL_);
                        amp = 2.6f * juce::jlimit (0.0f, 1.4f,
                                      0.45f + 0.55f*noiseSine (windPh_) + 2.5f*gustL_);
                    }
                    float f = juce::jlimit (0.0f, 0.9f, fc * w0);   // f = 2·sin(π·fc/fs) ≈ 2π·fc/fs
                    spL_ += f * spL2_; const float hpL = wl - spL_ - qd*spL2_; spL2_ += f * hpL;
                    spR_ += f * spR2_; const float hpR = wr - spR_ - qd*spR2_; spR2_ += f * hpR;
                    oL = spL2_ * amp; oR = spR2_ * amp; break;
                }
                default:    // White (0)
                    oL = wl; oR = wr; break;
            }
            // (SCAN/speed is applied at the call site as a sample-and-hold + interpolation on this raw output.)
        }
        bool  noiseOn_    = false;
        int   noiseType_  = 0;
        float noiseLevel_ = 0.0f, noisePitch_ = 0.5f, noisePanL_ = 0.70710678f, noisePanR_ = 0.70710678f;
        float noiseLvlT_ = 0.0f, noisePanLT_ = 0.70710678f, noisePanRT_ = 0.70710678f;   // fb202 — glide targets (mod steps at block rate; gains glide 2.5ms)
        std::uint32_t noiseRngL_ = 0x9E3779B9u, noiseRngR_ = 0x85EBCA6Bu;
        float pkL_[7] = { 0 }, pkR_[7] = { 0 }, brL_ = 0.0f, brR_ = 0.0f, geValL_ = 0.0f, geValR_ = 0.0f;
        float tpL_ = 0.0f, tpR_ = 0.0f, spL_ = 0.0f, spL2_ = 0.0f, spR_ = 0.0f, spR2_ = 0.0f, noiseLpL_ = 0.0f, noiseLpR_ = 0.0f;
        // NOISE P2 DSP (researched, per-type distinct): 2nd tape pole, hum/breath/gust LFO phases, gust env, vinyl rumble.
        float tpL2_ = 0.0f, tpR2_ = 0.0f, humPh_ = 0.0f, windPh_ = 0.0f, windPh2_ = 0.0f, gustL_ = 0.0f;
        // SCAN (was Pitch): sample-and-hold + interpolation at noiseScanRate_ (0.1×…2×) → the noise "scans" slower/faster.
        float noiseScanRate_ = 1.0f, scanPh_ = 0.0f, nCurL_ = 0.0f, nCurR_ = 0.0f, nPrevL_ = 0.0f, nPrevR_ = 0.0f;
        float noiseScanRateT_ = 1.0f;   // fb202 — glide target
        float rumbL_[2] = { 0.0f, 0.0f }, rumbR_[2] = { 0.0f, 0.0f };   // Vinyl turntable rumble (2-pole LP, L/R)
        float noiseSR_ = 48000.0f;   // sample rate for Hz-based noise math (hum/wind/rumble/SVF); set in setCurrentPlaybackSampleRate
        // NOISE IMPORT (P5) — looping-sample source state (overrides the algorithmic type when a buffer is loaded).
        tw::SampleBuffer* noiseSampleSource_ = nullptr;
        tw::SampleBuffer::BufferPtr noiseHeldBuf_;                 // keeps the current buffer alive through render
        const juce::AudioBuffer<float>* noiseBufLast_ = nullptr;   // change-detect for the recache
        const float* noiseSampL_ = nullptr; const float* noiseSampR_ = nullptr;
        int    noiseSampLen_ = 0;
        double noiseSampPos_ = 0.0, noiseSampNativeOverOut_ = 1.0;
        // fb66 — NOISE play modes: 0 Random (random start/note) · 1 Envelope (one-shot/note) · 2 Free (global tape).
        int    noisePlayMode_    = 0;
        bool   noiseOneShotDone_ = false;   // Envelope: the one-shot has played through (silent until the next note)
        double noiseFreeLatest_  = 0.0;     // fb67 — latest global tape position (samples); a Free note enters here, then free-runs
        // fb68 — Free-mode MONO carrier gate: the processor marks the newest voice as the sole carrier (target 1)
        // and the rest 0; the audible noise multiplies by a ~ms-smoothed gain → mono (no polyphonic phasing) with
        // click-free hand-offs. In non-Free modes every voice is a carrier (target 1) so this is a no-op.
        float  noiseCarrierTarget_ = 1.0f, noiseCarrierGain_ = 1.0f;
        float  noiseWidth_ = 1.0f;   // fb69 — noise stereo width (M/S): 0 mono · 1 normal · 2 wide
        float  noiseWidthT_ = 1.0f;   // fb202 — glide target
    public:

        void setPhaseMode (int modeA, int modeB) noexcept
        {
            // fb522 — UN-HARDWIRED. From 2026-07-09 until now both lines below read
            // `phaseModeA_ = 1` behind an ignoreUnused, so all four modes rendered
            // BIT-IDENTICALLY ([M] 0.000000e+00 at two note-on offsets) — the modes were
            // provably dead. ⚠️ SHIP-TOGETHER LAW: this un-wiring is only safe in the SAME
            // BUILD as the version-3 migration that forces every stored PHASE_MODE to 1,
            // because the param's registered default was 2 (Random) — without the migration
            // 100 % of the library flips Free -> Random on load.
            phaseModeA_ = juce::jlimit (0, 3, modeA);
            phaseModeB_ = juce::jlimit (0, 3, modeB);
        }

        /** PHASE offset, degrees (SYN_OSC_x_PHASE). Stored in CYCLES. */
        void setPhaseOffset   (float degA, float degB) noexcept { phaseOff_[0] = juce::jlimit (0.0f, 360.0f, degA) * (1.0f / 360.0f); phaseOff_[1] = juce::jlimit (0.0f, 360.0f, degB) * (1.0f / 360.0f); }
        void setPhaseOffsetCD (float degC, float degD) noexcept { phaseOff_[2] = juce::jlimit (0.0f, 360.0f, degC) * (1.0f / 360.0f); phaseOff_[3] = juce::jlimit (0.0f, 360.0f, degD) * (1.0f / 360.0f); }
        /** PHASE amount 0..1 (SYN_OSC_x_PHASE_AMT) — scales RANDOM's draw and SPREAD's fan. */
        void setPhaseAmount   (float amtA, float amtB) noexcept { phaseAmt_[0] = juce::jlimit (0.0f, 1.0f, amtA); phaseAmt_[1] = juce::jlimit (0.0f, 1.0f, amtB); }
        void setPhaseAmountCD (float amtC, float amtD) noexcept { phaseAmt_[2] = juce::jlimit (0.0f, 1.0f, amtC); phaseAmt_[3] = juce::jlimit (0.0f, 1.0f, amtD); }

        // ── fb522 OVERPASS — UNISON RANGE / WARP SPREAD / STACK, per osc ─────────────
        /** UNISON RANGE, cents of half-spread at Detune = 100 % (SYN_OSC_x_URANGE, 5..4800).
         *  Replaces the hard-coded kUniMaxDetuneCents = 50.0f. CONTINUOUS → it GLIDES on a
         *  2.5 ms one-pole (advanceUnisonRangeGlide, per block); the first push seeds without
         *  a glide so a freshly loaded patch does not sweep into tune. */
        void setUnisonRangeA (float cents) noexcept { setUniRangeImpl (0, cents); }
        void setUnisonRangeB (float cents) noexcept { setUniRangeImpl (1, cents); }
        void setUnisonRangeC (float cents) noexcept { setUniRangeImpl (2, cents); }
        void setUnisonRangeD (float cents) noexcept { setUniRangeImpl (3, cents); }
        /** UNISON WARP SPREAD, bipolar -1..+1 (SYN_OSC_x_UWARP). Fans the WARP amount across
         *  the unison sines: sine u gets warpAmount + uwarp*u_norm, clamped to 0..1. Serum has
         *  two of these (Warp 1 / Warp 2 spread); we have one knob and apply it to BOTH slots. */
        void setUnisonWarpA (float bip) noexcept { setUniWarpImpl (0, bip); }
        void setUnisonWarpB (float bip) noexcept { setUniWarpImpl (1, bip); }
        void setUnisonWarpC (float bip) noexcept { setUniWarpImpl (2, bip); }
        void setUnisonWarpD (float bip) noexcept { setUniWarpImpl (3, bip); }
        /** UNISON STACK, 0..8 (SYN_OSC_x_USTACK). A semitone table applied to the unison
         *  voice pitches; 0 = Off is bit-identical (the add is skipped entirely). */
        void setUnisonStackA (int mode) noexcept { setUniStackImpl (0, mode); }
        void setUnisonStackB (int mode) noexcept { setUniStackImpl (1, mode); }
        void setUnisonStackC (int mode) noexcept { setUniStackImpl (2, mode); }
        void setUnisonStackD (int mode) noexcept { setUniStackImpl (3, mode); }
        /** WARP slot VAR, 0..1 (SYN_OSC_x_WVAR / _W2VAR). 0 = identity for every mode.
         *  Live today on MIRROR (fold count N = 1+3*var) and RECTIFY (pre-gain 1+3*var).
         *  PARKED: the RM modulator drive the spec also puts on VAR — see the RM comment at
         *  the blend-slot law; it is a blend-slot dimension, not a warp-slot one. */
        void setWarpVar    (float varA, float varB) noexcept { warpVar_[0]  = juce::jlimit (0.0f, 1.0f, varA); warpVar_[1]  = juce::jlimit (0.0f, 1.0f, varB); }
        void setWarpVarCD  (float varC, float varD) noexcept { warpVar_[2]  = juce::jlimit (0.0f, 1.0f, varC); warpVar_[3]  = juce::jlimit (0.0f, 1.0f, varD); }
        void setWarp2Var   (float varA, float varB) noexcept { warp2Var_[0] = juce::jlimit (0.0f, 1.0f, varA); warp2Var_[1] = juce::jlimit (0.0f, 1.0f, varB); }
        void setWarp2VarCD (float varC, float varD) noexcept { warp2Var_[2] = juce::jlimit (0.0f, 1.0f, varC); warp2Var_[3] = juce::jlimit (0.0f, 1.0f, varD); }

        /** WAVER depth per OSC, 0..1 (analog pitch drift; from SYN_OSC_A/B_WAVER / 100).
         *  Replaces the old EROSION pitch sine-LFO with a bounded Ornstein–Uhlenbeck
         *  drift, independent per (osc × unison sine). Pushed per block; modulatable. */
        void setWaver (float a, float b) noexcept
        {
            waverA_ = juce::jlimit (0.0f, 1.0f, a);
            waverB_ = juce::jlimit (0.0f, 1.0f, b);
        }

    private:
        // ── Batch 1 — per-voice modulation state ──
        wc::SynthLFO  synthLfo_[wc::NUM_LFOS];   // L1..L3 (Batch 1 drives L1)
        wc::ModConfig modConfig_;                // published per block by PluginProcessor
        float         lfoVisValue_ = 0.0f;       // most-recent L1 value for the editor dot

        // One-time decorrelated seed for FREE mode (per voice ptr / sine / osc), 0..1.
        double seedPhase (int u, int osc) const noexcept
        {
            const std::uint32_t h = static_cast<std::uint32_t> (reinterpret_cast<std::uintptr_t> (this))
                                  ^ static_cast<std::uint32_t> ((u   + 1) * 0x9E3779B9u)
                                  ^ static_cast<std::uint32_t> ((osc + 1) * 2654435761u);
            return (double) (h & 0xFFFF) / 65535.0;
        }
        // Fresh decorrelated phase each call (xorshift32) for RANDOM mode, 0..1.
        double nextPhaseRandom () noexcept
        {
            phaseRng_ ^= phaseRng_ << 13;
            phaseRng_ ^= phaseRng_ >> 17;
            phaseRng_ ^= phaseRng_ << 5;
            return (double) (phaseRng_ & 0xFFFFFFu) / (double) 0x1000000;
        }
        // Resolve one unison sine's start phase for a given mode. `carried` is the
        // accumulator's current value (used by FREE so it keeps running across notes).
        double resolvePhase (int mode, int u, int osc, double carried) noexcept
        {
            // fb522 — the two new knobs fold in here and NOWHERE else (note-on only, [M] zero CPU):
            //   base = SYN_OSC_x_PHASE / 360   (cycles)    amt = SYN_OSC_x_PHASE_AMT / 100
            // At base = 0 and amt = 1 every branch returns the EXACT expression that shipped, so
            // FREE (the only mode that has been reachable since 2026-07-09) is bit-identical.
            // ⚠️ SPREAD uses u/cnt, NOT u/(cnt-1): voice 0 anchors at phase 0 and the fan
            //    deliberately never closes the loop — the fb255 blend law assumes that anchor.
            //    DO NOT "fix" it.
            // ⚠️ FREE ignores `amt` on purpose: it carries an accumulator, there is nothing
            //    random to scale. `base` still offsets its one-time seed.
            const double base = (double) phaseOff_[(size_t) osc];
            const double amt  = (double) phaseAmt_[(size_t) osc];
            auto wrap = [] (double x) noexcept { return x - std::floor (x); };
            switch (mode)
            {
                case 0: return (base == 0.0) ? 0.0 : wrap (base);                                // RETRIG — aligned, punchy
                case 3: { const int cnt = (osc == 0) ? activeUnisonA_ : (osc == 1) ? activeUnisonB_ : (osc == 2) ? activeUnisonC_ : activeUnisonD_;
                          const double fan = (cnt > 1) ? (double) u / (double) cnt : 0.0;
                          return (base == 0.0 && amt == 1.0) ? fan : wrap (base + amt * fan); }   // SPREAD — even fan (per-OSC)
                case 2: { const double r = nextPhaseRandom();   // drawn ONCE either way — it mutates phaseRng_
                          return (base == 0.0 && amt == 1.0) ? r : wrap (base + amt * r); }       // RANDOM — fresh each note
                case 1: default: { if (phaseSeeded_) return carried;                             // FREE — seed once, then carry
                                   const double sd = seedPhase (u, osc);
                                   return (base == 0.0) ? sd : wrap (base + sd); }
            }
        }

        // ── WAVER — analog pitch drift (Ornstein–Uhlenbeck, per osc × unison sine) ──
        // OU is a mean-reverting (bounded) leaky-integrator of Gaussian noise: a slow,
        // low-frequency, decorrelated wander in cents — "wanders but never runs away".
        // Updated at block rate; the cents are added to each sine's phase increment in
        // updateUnisonPhaseIncrements*. Steady-state σ = stdCents, correlation time tauC.
        static constexpr float kWaverTauSeconds  = 1.0f;   // correlation time 1/θ (per-note breathing)
        static constexpr float kWaverStdMaxCents = 3.0f;   // steady-state σ at 100% (3σ ≈ 9 cents)
        static constexpr float kWaverCapCents    = 15.0f;  // hard excursion ceiling (never detune away)

        // Uniform [0,1) from a per-sine xorshift32 stream (one independent stream each).
        static float waverUniform (std::uint32_t& s) noexcept
        {
            s ^= s << 13; s ^= s >> 17; s ^= s << 5;
            return (float) ((s >> 8) & 0xFFFFFFu) * (1.0f / 16777216.0f);
        }
        // Murmur3 fmix32 finalizer — strong avalanche so the per-(osc × sine) seeds
        // decorrelate fully (xorshift32 alone stays correlated for near-identical seeds).
        static std::uint32_t waverSeedMix (std::uint32_t x) noexcept
        {
            x ^= x >> 16; x *= 0x7feb352du;
            x ^= x >> 15; x *= 0x846ca68bu;
            x ^= x >> 16;
            return x | 1u;                                  // nonzero for xorshift
        }
        // N(0,1) via Box–Muller (block-rate, cost negligible).
        static float waverGaussian (std::uint32_t& s) noexcept
        {
            float u1 = waverUniform (s);
            const float u2 = waverUniform (s);
            u1 = juce::jmax (u1, 1.0e-7f);                  // avoid log(0)
            return std::sqrt (-2.0f * std::log (u1)) * std::cos (6.2831853071795865f * u2);
        }
        // Advance one oscillator's per-unison OU drift by one block (dt seconds).
        // depth 0..1 scales σ; collapses to zero when off. Bounded + denormal-flushed.
        void updateWaverOU (float* cents, std::uint32_t* rng, float depth, float dt) noexcept
        {
            if (depth <= 0.0f)
            {
                for (int u = 0; u < kMaxUnison; ++u) cents[(size_t) u] = 0.0f;
                return;
            }
            const float phi      = std::exp (-dt / kWaverTauSeconds);   // AR(1) pole = e^(-dt/τ)
            const float stdCents = depth * kWaverStdMaxCents;
            const float sigStep  = stdCents * std::sqrt (1.0f - phi * phi);
            for (int u = 0; u < kMaxUnison; ++u)
            {
                float x = phi * cents[(size_t) u] + sigStep * waverGaussian (rng[(size_t) u]);
                x = juce::jlimit (-kWaverCapCents, kWaverCapCents, x);       // never run away
                if (x < 1.0e-20f && x > -1.0e-20f) x = 0.0f;                 // denormal flush
                cents[(size_t) u] = x;
            }
        }
    public:

        /** Phase 11d — Set per-OSC FOLD shape + amount. Pushed per-block from
         *  PluginProcessor broadcast. Applies in the unison loop, post engine compute. */
        void setFold (int shapeA, float amountA, int shapeB, float amountB) noexcept
        {
            foldShapeA_  = juce::jlimit (0, 2, shapeA);
            foldAmountBaseA_ = juce::jlimit (0.0f, 1.0f, amountA);
            foldShapeB_  = juce::jlimit (0, 2, shapeB);
            foldAmountBaseB_ = juce::jlimit (0.0f, 1.0f, amountB);
        }

        /** KEYTRACK — the first note→destination modulation route (the mod-matrix
         *  embryo). Source = note pitch (latched per voice at note-on); depth 0..1
         *  per OSC; destination selectable (0=FRAME/WT POS, 1=WARP, 2=FOLD).
         *  CROSSFADE: effective = base + depth·(noteRamp − base). depth 0 = the knob
         *  works normally; depth 1 = a pure pitch ramp where the lowest anchor note
         *  (C1) is SILENT and the highest (C6) is FULL (1.0), independent of the knob
         *  value (so it can't clamp-to-max or collapse-to-zero). Resolved at render
         *  entry. Architected so Env/LFO sources + a full matrix slot in later
         *  with no rewrite of the oscillator. Pushed per block. */
        void setKeytrack (float depthA, int destA, float depthB, int destB) noexcept
        {
            ktDepthA_ = juce::jlimit (0.0f, 1.0f, depthA);
            ktDestA_  = juce::jlimit (0, 2, destA);
            ktDepthB_ = juce::jlimit (0.0f, 1.0f, depthB);
            ktDestB_  = juce::jlimit (0, 2, destB);
        }

        /** ROUTE (back panel pill 4) — the generalized modulation slot, mod route #2.
         *  Source = Note ramp (reuses KEYTRACK's per-voice note ramp) or Velocity;
         *  destination selectable incl. the two per-voice filter cutoffs; amount is
         *  BIPOLAR (-1..+1). Resolved per-voice at render entry exactly like KEYTRACK:
         *  FRAME/WARP/FOLD accumulate into the effective members alongside KEYTRACK;
         *  CUT1/CUT2 add a semitone offset into the per-sample filter cutoff. Built so
         *  Env/LFO sources slot into the source list later with no oscillator rewrite. */
        void setRoute (int srcA, int destA, float amtA,
                       int srcB, int destB, float amtB) noexcept
        {
            // Note→Frame ROUTE tile RETIRED (2026-07-09): the route is DEAD — amounts are
            // forced to 0 so an old session's stored route can't apply invisible modulation.
            juce::ignoreUnused (amtA, amtB);
            routeSrcA_  = juce::jlimit (0, 1, srcA);
            routeDestA_ = juce::jlimit (0, 2, destA);
            routeAmtA_  = 0.0f;
            routeSrcB_  = juce::jlimit (0, 1, srcB);
            routeDestB_ = juce::jlimit (0, 2, destB);
            routeAmtB_  = 0.0f;
        }

        /** Phase 11c — Set per-OSC SPECTRAL type + amount. Pushed per-block from
         *  PluginProcessor broadcast. Updates biquad coefficients on the fly.
         *  Phase 11g: types 3/4/5 (Comb/RingMod/BitCrush) bypass the biquad filter. */
        void setSpectral (int typeA, float amtA, int typeB, float amtB) noexcept
        {
            spectralTypeA_ = juce::jlimit (0, 9, typeA);
            spectralAmtA_  = juce::jlimit (0.0f, 1.0f, amtA);
            spectralTypeB_ = juce::jlimit (0, 9, typeB);
            spectralAmtB_  = juce::jlimit (0.0f, 1.0f, amtB);

            spectralBypassA_ = (spectralAmtA_ < 1.0e-4f);
            spectralBypassB_ = (spectralAmtB_ < 1.0e-4f);

            updateSpectralCoefficients (spectralTypeA_, spectralAmtA_, spectralFilterAL_, spectralFilterAR_);
            updateSpectralCoefficients (spectralTypeB_, spectralAmtB_, spectralFilterBL_, spectralFilterBR_);
        }

        /** Phase 11g — Set per-OSC INTERP mode (0=Linear, 1=Stepped). */
        void setInterpMode (int modeA, int modeB) noexcept
        {
            interpModeA_ = juce::jlimit (0, 1, modeA);
            interpModeB_ = juce::jlimit (0, 1, modeB);
        }

        // ════ OSC C + D setters (4-osc, spec P5) — twins of the B / combined setters ════
        void setTuningC (int oct, int semi, float cent) noexcept { octOffsetC_=oct; semiOffsetC_=semi; centsOffsetC_=cent; if (playing_) updateUnisonPhaseIncrementsC (glideNote_); }
        void setTuningD (int oct, int semi, float cent) noexcept { octOffsetD_=oct; semiOffsetD_=semi; centsOffsetD_=cent; if (playing_) updateUnisonPhaseIncrementsD (glideNote_); }
        void setLevelC (float level) noexcept { levelC_ = juce::jlimit (0.0f, 1.0f, level); }
        void setLevelD (float level) noexcept { levelD_ = juce::jlimit (0.0f, 1.0f, level); }
        // SOLO/MUTE — set per-osc gate targets (A,B,C,D). Click-free: smoothed toward target in render.
        /** FLOW · ROUND ROBIN (mode 4 — replaced Drift/"human"): each note-on sounds exactly ONE
         *  oscillator, rotating through the enabled/audible set (Moog-Matriarch-style global
         *  rotation — the shared counter lives in the processor, audio-thread only). The pick is
         *  applied through the existing click-free osc gate; a voice keeps its osc through release. */
        // FLOW · ARP WAVE lane (fb105): per-step timbre value from the arp engine,
        // pushed every block (one-block latency, drift-lane pattern). Applied as a
        // bipolar offset on every osc's effective wavetable frame position below.
        void setFlowWave (float w) noexcept { flowWave_ = juce::jlimit (-0.5f, 0.5f, w); }

        void setRobin (bool on, wc::FlowRobin* brain, bool eA, bool eB, bool eC, bool eD) noexcept
        {
            robinOn_ = on; robinBrain_ = brain;        // fb122: the Wheel brain decides stations
            robinEn_[0] = eA; robinEn_[1] = eB; robinEn_[2] = eC; robinEn_[3] = eD;
            if (! on && ! playing_) { robinPick_ = -1; robinAmpL_ = robinAmpR_ = 1.0f; }
        }
        int  robinStation() const noexcept { return robinPick_; }
        void robinSwapStation (int st) noexcept { robinPick_ = st; }   // Legato New: gates crossfade (smoothed)
        // Fade/Overlap: the OLD station's ringing tail hands over — after `wait` samples
        // it arms the standard steal fade with a custom length. Click-free by construction.
        void robinHandover (int waitSamp, float fadeSec) noexcept
        {
            if (! playing_ || stealing_) return;
            robinHandWait_ = waitSamp < 0 ? 0 : waitSamp;
            robinHandFadeSec_ = fadeSec < 0.005f ? 0.005f : fadeSec;
        }
        // effective per-osc gate target = SOLO/MUTE gate masked by this note's round-robin pick
        float robinGate (int g) const noexcept
        { return (robinPick_ >= 0 && g != robinPick_) ? 0.0f : oscGateTarget_[g]; }

        void setOscGates (float a, float b, float c, float d) noexcept
        {
            oscGateTarget_[0] = a; oscGateTarget_[1] = b; oscGateTarget_[2] = c; oscGateTarget_[3] = d;
            if (! playing_) { for (int k = 0; k < 4; ++k) oscGate_[k] = robinGate (k); }  // snap when idle → fresh notes respect gate from sample 0, no blip
        }
        void setPanC (float pan) noexcept { const float p=juce::jlimit(-1.0f,1.0f,pan); const float a=(p+1.0f)*0.25f*juce::MathConstants<float>::pi; panLCT_=std::cos(a); panRCT_=std::sin(a); }   // fb202 — glide targets
        void setPanD (float pan) noexcept { const float p=juce::jlimit(-1.0f,1.0f,pan); const float a=(p+1.0f)*0.25f*juce::MathConstants<float>::pi; panLDT_=std::cos(a); panRDT_=std::sin(a); }   // fb202 — glide targets
        void setWavetableC (const tw::Wavetable* wt) noexcept { currentWavetableC_ = wt; }
        void setWavetableD (const tw::Wavetable* wt) noexcept { currentWavetableD_ = wt; }
        void setWavetableFrameC (float pos) noexcept { framePosBaseC_ = juce::jlimit (0.0f, 1.0f, pos); }
        void setWavetableFrameD (float pos) noexcept { framePosBaseD_ = juce::jlimit (0.0f, 1.0f, pos); }
        void setWarpC (int mode, float amount) noexcept { warpModeC_ = juce::jlimit(0,kWarpModeMax,mode); warpAmountBaseC_ = juce::jlimit(0.0f,1.0f,amount); }
        void setWarpD (int mode, float amount) noexcept { warpModeD_ = juce::jlimit(0,kWarpModeMax,mode); warpAmountBaseD_ = juce::jlimit(0.0f,1.0f,amount); }
        void setEngineC (int idx) noexcept { engineC_ = static_cast<Engine> (juce::jlimit(0,6,idx)); }
        void setEngineD (int idx) noexcept { engineD_ = static_cast<Engine> (juce::jlimit(0,6,idx)); }
        void setUnisonC (int count, float detune01, float blend01, float width01) noexcept { setUnisonImpl (2, activeUnisonC_, uDetuneCentsC_, uPanLTC_, uPanRTC_, uNormTC_, uPanLC_, uPanRC_, uNormC_, uniSnapC_, count, detune01, blend01, width01); updateUnisonFramePositions(); if (currentMidiNote_ >= 0) updateUnisonPhaseIncrementsC (glideNote_); }
        void setUnisonD (int count, float detune01, float blend01, float width01) noexcept { setUnisonImpl (3, activeUnisonD_, uDetuneCentsD_, uPanLTD_, uPanRTD_, uNormTD_, uPanLD_, uPanRD_, uNormD_, uniSnapD_, count, detune01, blend01, width01); updateUnisonFramePositions(); if (currentMidiNote_ >= 0) updateUnisonPhaseIncrementsD (glideNote_); }
        void setWarp2CD (int modeC, float amountC, int modeD, float amountD) noexcept { warp2ModeC_=juce::jlimit(0,kWarpModeMax,modeC); warp2AmountBaseC_=juce::jlimit(0.0f,1.0f,amountC); warp2ModeD_=juce::jlimit(0,kWarpModeMax,modeD); warp2AmountBaseD_=juce::jlimit(0.0f,1.0f,amountD); }

        /** FM-ENGINE-VOICE — per-OSC wavetable-carrier FM params (osc 0..3 = A..D).
         *  algo: 0 Stack (M2→M1→carrier) / 1 Split (M1,M2→carrier) / 2 Ring (M2→M1; M1 rings output). */
        void setFMOsc (int osc, int algo, float r1, float d1, float r2, float d2, float fb) noexcept
        {
            if (osc < 0 || osc > 3) return;
            const auto o = (size_t) osc;
            fmAlgo_[o]   = juce::jlimit (0, 2, algo);
            fmRatio1_[o] = juce::jlimit (0.25f, 16.0f, r1);
            fmDepth1_[o] = juce::jlimit (0.0f, 1.0f, d1);
            fmRatio2_[o] = juce::jlimit (0.25f, 16.0f, r2);
            fmDepth2_[o] = juce::jlimit (0.0f, 1.0f, d2);
            fmFbAmt_[o]  = juce::jlimit (0.0f, 1.0f, fb);
        }

        /** Fast odd-symmetric tanh (rational 135135… approx) — SCORCH's in-loop
            waveshaper; no libm call in the hot FM path. Input clamped to ±5. */
        static inline float fmFastTanh (float x) noexcept
        {
            x = juce::jlimit (-5.0f, 5.0f, x);
            const float x2 = x * x;
            const float a = x * (135135.0f + x2 * (17325.0f + x2 * (378.0f + x2)));
            const float b = 135135.0f + x2 * (62370.0f + x2 * (3150.0f + x2 * 28.0f));
            return a / b;
        }

        /** Hermite smoothstep on [a,b] → [0,1] — SCORCH/QUAKE macro tapers. */
        static inline float fmSmoothstep (float a, float b, float x) noexcept
        {
            const float t = juce::jlimit (0.0f, 1.0f, (x - a) / juce::jmax (1.0e-6f, b - a));
            return t * t * (3.0f - 2.0f * t);
        }

        /** FM WEATHERING SUITE — page-2 knobs (osc 0..3 = A..D), all 0..1.
            Slot 4 (kept param id …_FM_GALE) now drives QUAKE (subharmonic FM);
            slot 5 (kept param id …_FM_BEND) now drives SCORCH (in-loop drive).
            Param IDs are frozen to preserve the WebView bind chain + saved state. */
        void setFMOsc2 (int osc, float strike, float age, float rust, float quake, float scorch, float storm) noexcept
        {
            if (osc < 0 || osc > 3) return;
            const auto o = (size_t) osc;
            fmStrike_[o]     = juce::jlimit (0.0f, 1.0f, strike);
            fmAge_[o]        = juce::jlimit (0.0f, 1.0f, age);
            fmRust_[o]       = juce::jlimit (0.0f, 1.0f, rust);
            fmQuakeKnob_[o]  = juce::jlimit (0.0f, 1.0f, quake);
            fmScorchKnob_[o] = juce::jlimit (0.0f, 1.0f, scorch);
            fmStorm_[o]      = juce::jlimit (0.0f, 1.0f, storm);
        }
        void setBlurCD (float blurC01, float blurD01) noexcept { blurTargetC_=juce::jlimit(0.0f,1.0f,blurC01); blurTargetD_=juce::jlimit(0.0f,1.0f,blurD01); }
        void setPhaseModeCD (int modeC, int modeD) noexcept
        {
            phaseModeC_ = juce::jlimit (0, 3, modeC);   // fb522 — un-hardwired; see setPhaseMode
            phaseModeD_ = juce::jlimit (0, 3, modeD);
        }
        void setWaverCD (float c, float d) noexcept { waverC_=juce::jlimit(0.0f,1.0f,c); waverD_=juce::jlimit(0.0f,1.0f,d); }
        void setFoldCD (int shapeC, float amountC, int shapeD, float amountD) noexcept { foldShapeC_=juce::jlimit(0,2,shapeC); foldAmountBaseC_=juce::jlimit(0.0f,1.0f,amountC); foldShapeD_=juce::jlimit(0,2,shapeD); foldAmountBaseD_=juce::jlimit(0.0f,1.0f,amountD); }
        void setKeytrackCD (float depthC, int destC, float depthD, int destD) noexcept { ktDepthC_=juce::jlimit(0.0f,1.0f,depthC); ktDestC_=juce::jlimit(0,2,destC); ktDepthD_=juce::jlimit(0.0f,1.0f,depthD); ktDestD_=juce::jlimit(0,2,destD); }
        void setRouteCD (int srcC, int destC, float amtC, int srcD, int destD, float amtD) noexcept { juce::ignoreUnused (amtC, amtD); routeSrcC_=juce::jlimit(0,1,srcC); routeDestC_=juce::jlimit(0,2,destC); routeAmtC_=0.0f; routeSrcD_=juce::jlimit(0,1,srcD); routeDestD_=juce::jlimit(0,2,destD); routeAmtD_=0.0f; }   // route RETIRED — dead by force
        void setSpectralCD (int typeC, float amtC, int typeD, float amtD) noexcept { spectralTypeC_=juce::jlimit(0,9,typeC); spectralAmtC_=juce::jlimit(0.0f,1.0f,amtC); spectralTypeD_=juce::jlimit(0,9,typeD); spectralAmtD_=juce::jlimit(0.0f,1.0f,amtD); spectralBypassC_=(spectralAmtC_<1.0e-4f); spectralBypassD_=(spectralAmtD_<1.0e-4f); updateSpectralCoefficients(spectralTypeC_,spectralAmtC_,spectralFilterCL_,spectralFilterCR_); updateSpectralCoefficients(spectralTypeD_,spectralAmtD_,spectralFilterDL_,spectralFilterDR_); }
        void setInterpModeCD (int modeC, int modeD) noexcept { interpModeC_=juce::jlimit(0,1,modeC); interpModeD_=juce::jlimit(0,1,modeD); }

        // (Old per-voice pitch EROSION setter removed — pitch drift is now WAVER, set
        //  per-OSC via setWaver(). SYN_EROSION still drives the FILTER cutoff drift via
        //  setErosionAmount_filter() — that path is unchanged.)

        /** HORIZON tilt -1..+1 (set per-block from APVTS SYN_HORIZON/100). */
        void setHorizonAmount (float h) noexcept { horizonAmount_ = juce::jlimit (-1.0f, 1.0f, h); }

        void startNote (int midiNote, float velocity,
                        juce::SynthesiserSound*, int /*pitchWheelPos*/) override
        {
            // Phase 12 — monotonic stamp so UnisonSynth can find the oldest (steal)
            // or newest (mono retarget) voice. Declared up top so the LEGATO branch
            // shares it. Static atomic: one counter across all SynthVoice instances;
            // thread-safe even though startNote runs under the Synthesiser lock.
            static std::atomic<juce::uint32> globalNoteCounter { 1 };
            noteStartStamp_ = globalNoteCounter.fetch_add (1, std::memory_order_relaxed);

            // ── LEGATO retarget: slide pitch to the new note, retrigger NOTHING ──
            // Armed by UnisonSynth::beginLegatoRetarget() just before startVoice().
            if (legatoRetarget_)
            {
                legatoRetarget_ = false;

                // JUCE's startVoice() calls stopNote(0,false) on a sounding voice before
                // calling startNote — that armed the 30ms steal fade. Disarm it: no
                // samples rendered in between, so this is a pure flag flip (click-free).
                stealing_         = false;
                stealingFade_     = 1.0f;
                stealingFadeStep_ = 0.0f;

                const double fromPitch = glideNote_;   // mid-slide continuity
                currentMidiNote_ = midiNote;
                // currentVelocity_ intentionally NOT updated — classic legato keeps the
                // phrase's initial velocity (ROUTE velocity stays consistent).
                beginGlideLegato (fromPitch, midiNote);

                // Pitch-tracking context follows the new note; everything else carries.
                ktRamp_ = juce::jlimit (0.0f, 1.0f,
                              (float) (midiNote - kKtLowNote) / (float) (kKtHighNote - kKtLowNote));
                // FM key scaling follows the retargeted pitch (index rolloff above C5)
                fmKs_ = (float) std::pow (0.5, (double) std::max (0, midiNote - 72) / 18.0);
                updateUnisonFramePositions();
                updateUnisonPhaseIncrementsA (glideNote_);
                updateUnisonPhaseIncrementsB (glideNote_);
                playing_ = true;
                return;   // amp/filter envelopes, phases, waver, fold history all untouched
            }

            // ── FLOW · ROUND ROBIN — this note sounds ONE oscillator, rotating per note-on.
            // Cycles only through the enabled+audible set (≥2 participants, else a no-op);
            // legato retargets above keep the phrase's osc (Matriarch behavior). Gates snap
            // here (the amp envelope starts at silence, so the snap is click-free).
            robinPick_ = -1;
            robinAmpL_ = robinAmpR_ = 1.0f; robinDelay_ = 0;
            robinGlideFrom_ = -1.0; robinGlideSec_ = 0.0f;
            robinHandWait_ = -1;
            if (robinOn_ && robinBrain_ != nullptr)
            {
                // fb122 — the Wheel brain staged this hit in UnisonSynth::noteOn
                const wc::RobinHit h = robinBrain_->takeHit();
                robinPick_ = h.station;
                velocity  *= h.vel;                        // Vary + per-station Level
                robinAmpL_ = h.ampL; robinAmpR_ = h.ampR;  // per-station Pan
                robinDelay_ = h.delaySamp;                 // Wobble: humanized late start
                robinGlideFrom_ = h.glideFrom; robinGlideSec_ = h.glideSec;
            }
            for (int g = 0; g < 4; ++g) oscGate_[g] = robinGate (g);

            currentMidiNote_ = midiNote;
            currentVelocity_ = velocity;
            beginGlide (midiNote);     // PORTAMENTO — snap or start the slide (sets glideNote_)
            if (robinGlideFrom_ >= 0.0)                    // fb122 — Glide: slide in from the last station's note
                beginGlideRobin (robinGlideFrom_, midiNote, robinGlideSec_);
            // FM WEATHERING note-on: arm the STRIKE transient, roll AGE's per-note offset
            // (no two notes beat the same), reset QUAKE's sub phase (below), set DX key scaling
            // (index halves every 1.5 octaves above C5 so the top end stays sweet).
            fmKs_ = (float) std::pow (0.5, (double) std::max (0, midiNote - 72) / 18.0);
            for (size_t fo = 0; fo < 4; ++fo)
            {
                fmStrikeEnv_[fo] = 1.0f;
                fmNz_ ^= fmNz_ << 13; fmNz_ ^= fmNz_ >> 17; fmNz_ ^= fmNz_ << 5;
                fmAgeNote_[fo] = (float) (std::int32_t) fmNz_ * (1.0f / 2147483648.0f);
            }
            // OSC A resets
            noiseLpZ_        = 0.0f;     // Phase 3 — NOISE filter memory reset
            // OSC B resets (Phase 9)
            noiseLpZB_       = 0.0f;
            noiseLpZC_       = 0.0f;   // OSC C/D resets (4-osc)
            noiseLpZD_       = 0.0f;
            // SAMPLE-ENGINE-VOICE — arm note-on; the per-block render does the actual
            // noteOn AFTER the buffer pointer is refreshed (so SPRAY scatters in-bounds).
            sampleSprayRng_ = sampleSprayRng_ * 1664525u + 1013904223u;
            spraySeedA_ = sampleSprayRng_ ^ 0xA1u; spraySeedB_ = sampleSprayRng_ ^ 0xB2u;
            spraySeedC_ = sampleSprayRng_ ^ 0xC3u; spraySeedD_ = sampleSprayRng_ ^ 0xD4u;
            sampleNoteOnPending_ = true;
            granNoteOnPending_   = true;   // GRANULAR-ENGINE-VOICE
            geodeNoteOnPending_  = true;   // GEODE-ENGINE-VOICE
            harmNoteOnPending_   = true;   // HARMONIC-ENGINE-VOICE
            modalNoteOnPending_  = true;   // MODAL-ENGINE-VOICE

            // fb66 — NOISE play-mode note-on: Random drops the loop head at a fresh random spot each note
            // (the deliberate version of today's feel); Envelope restarts the one-shot from the top. Free
            // leaves the head alone — it gets resynced to the global tape by setNoiseFreePos each block.
            if (noiseSampLen_ > 1)
            {
                if (noisePlayMode_ == 0)   // Random
                {
                    sampleSprayRng_ = sampleSprayRng_ * 1664525u + 1013904223u;
                    noiseSampPos_ = ((double) (sampleSprayRng_ >> 8) * (1.0 / 16777216.0)) * (double) noiseSampLen_;
                }
                else if (noisePlayMode_ == 1)   // Envelope — one-shot from the top
                {
                    noiseSampPos_ = 0.0;
                    noiseOneShotDone_ = false;
                }
                else if (noisePlayMode_ == 2)   // Free — enter the running tape at its current spot (once), then loop like Random
                {
                    noiseSampPos_ = juce::jlimit (0.0, (double) (noiseSampLen_ - 1), noiseFreeLatest_);
                }
            }
            // fb68 — reset the Free-mode mono carrier gate. Poly modes (and algorithmic noise) = full immediately.
            // A Free + sample voice starts MUTED so a chord doesn't blast every voice for a block — the processor
            // promotes exactly ONE (the newest) to carrier next block and it ramps in click-free.
            if (noisePlayMode_ == 2 && noiseSampLen_ > 1) { noiseCarrierGain_ = noiseCarrierTarget_ = 0.0f; }
            else                                          { noiseCarrierGain_ = noiseCarrierTarget_ = 1.0f; }

            // PHASE — initialise each unison sine's phase accumulator per the selected
            // mode (RETRIG/FREE/RANDOM/SPREAD). The amp env starts at 0, so any reset here
            // is masked → click-free. FREE keeps the running accumulator (carried) across
            // notes for true analog behaviour; it's seeded decorrelated once (phaseSeeded_).
            if (phaseRng_ == 0u)
                phaseRng_ = (static_cast<std::uint32_t> (reinterpret_cast<std::uintptr_t> (this)) ^ 0xA5A5A5A5u) | 1u;

            // fb544 — every branch of resolvePhase folds `base` into the value it returns, so the
            // accumulators seeded below already CONTAIN the knob. Sync the tracker to that or the
            // first block after note-on would slide the note by the whole knob value on top.
            for (int o = 0; o < 4; ++o) phaseOffApplied_[(size_t) o] = (double) phaseOff_[(size_t) o];
            for (int u = 0; u < kMaxUnison; ++u)
            {
                uPhaseA_[(size_t) u]      = resolvePhase (phaseModeA_, u, 0, uPhaseA_[(size_t) u]);
                uModPhaseA_[(size_t) u]   = 0.0;
                uSyncPhaseA_[(size_t) u]  = 0.0;
                uPhaseB_[(size_t) u]      = resolvePhase (phaseModeB_, u, 1, uPhaseB_[(size_t) u]);
                uModPhaseB_[(size_t) u]   = 0.0;
                uSyncPhaseB_[(size_t) u]  = 0.0;
                uPhaseC_[(size_t) u]      = resolvePhase (phaseModeC_, u, 2, uPhaseC_[(size_t) u]);
                uModPhaseC_[(size_t) u]   = 0.0;
                uSyncPhaseC_[(size_t) u]  = 0.0;
                uPhaseD_[(size_t) u]      = resolvePhase (phaseModeD_, u, 3, uPhaseD_[(size_t) u]);
                uModPhaseD_[(size_t) u]   = 0.0;
                uSyncPhaseD_[(size_t) u]  = 0.0;
                // FM-ENGINE-VOICE — M2 phases + M1 feedback memory start clean each note
                uMod2PhaseA_[(size_t) u] = 0.0;  uMod2PhaseB_[(size_t) u] = 0.0;
                uMod2PhaseC_[(size_t) u] = 0.0;  uMod2PhaseD_[(size_t) u] = 0.0;
                fmFbA_[(size_t) u] = 0.0f;  fmFbB_[(size_t) u] = 0.0f;
                fmFbC_[(size_t) u] = 0.0f;  fmFbD_[(size_t) u] = 0.0f;
                fmPrevM1A_[(size_t) u] = 0.0f;  fmPrevM1B_[(size_t) u] = 0.0f;   // STORM cross memory
                fmPrevM1C_[(size_t) u] = 0.0f;  fmPrevM1D_[(size_t) u] = 0.0f;
                // QUAKE — per-voice subharmonic phase starts aligned to the note (phase-locked, click-free)
                fmQuakePhaseA_[(size_t) u] = 0.0;  fmQuakePhaseB_[(size_t) u] = 0.0;
                fmQuakePhaseC_[(size_t) u] = 0.0;  fmQuakePhaseD_[(size_t) u] = 0.0;
            }
            phaseSeeded_ = true;

            // WAVER — seed per-(osc × unison sine) OU drift streams, decorrelated per
            // voice+note. The (2u+1)/(2u+2) interleave gives all 16 streams distinct,
            // well-separated inputs; waverSeedMix() avalanches them to ~zero cross-corr.
            const std::uint32_t waverHash = static_cast<std::uint32_t> (reinterpret_cast<std::uintptr_t> (this))
                                          ^ static_cast<std::uint32_t> (midiNote * 2654435761u);
            for (int u = 0; u < kMaxUnison; ++u)
            {
                waverRngA_[(size_t) u]   = waverSeedMix (waverHash + (std::uint32_t) (2 * u + 1) * 0x9E3779B1u);
                waverRngB_[(size_t) u]   = waverSeedMix (waverHash + (std::uint32_t) (2 * u + 2) * 0x9E3779B1u);
                waverRngC_[(size_t) u]   = waverSeedMix ((waverHash ^ 0x68E31DA4u) + (std::uint32_t) (2 * u + 1) * 0x9E3779B1u);
                waverRngD_[(size_t) u]   = waverSeedMix ((waverHash ^ 0xB5297A4Du) + (std::uint32_t) (2 * u + 2) * 0x9E3779B1u);
                waverCentsA_[(size_t) u] = 0.0f;
                waverCentsB_[(size_t) u] = 0.0f;
                waverCentsC_[(size_t) u] = 0.0f;
                waverCentsD_[(size_t) u] = 0.0f;
            }

            // fb302 — pick THIS note's static ANALOG DETUNE: a gaussian tuning error (~±2¢ typical,
            // hard-capped ±6¢), seeded per voice so different notes/voices sit at slightly different
            // pitches. CENTERED on 0 → the synth averages perfectly in tune (this models an analog
            // VCO's per-note tuning imperfection, NOT a global flat offset). The slow drift is layered
            // on per-block below; together they make the pitch dance a few cents, like Serum's analog.
            analogStaticCents_ = 0.0f;   // fb325 — GLOBAL ANALOG DETUNE REMOVED (Max: perfectly tuned; tape wow covers analog drift)
            analogDriftCents_  = 0.0f;
            analogDetuneSemis_ = (double) analogStaticCents_ * 0.01;   // cents → semitones

            // KEYTRACK — latch the note-pitch source for this voice: a low-anchored
            // unipolar ramp, 0 at kKtLowNote up to 1 at kKtHighNote (held for the note).
            ktRamp_ = juce::jlimit (0.0f, 1.0f,
                          (float) (midiNote - kKtLowNote) / (float) (kKtHighNote - kKtLowNote));

            for (auto& sl : sub_) sl.osc.noteOn();   // SUB — fresh phase + reseeded heat shaper
            // Phase 8b — populate per-sine increments
            updateUnisonFramePositions();
            updateUnisonPhaseIncrementsA (glideNote_);
            updateUnisonPhaseIncrementsB (glideNote_);
            updateUnisonPhaseIncrementsC (glideNote_);
            updateUnisonPhaseIncrementsD (glideNote_);
            playing_         = true;
            for (int li = 0; li < wc::NUM_LFOS; ++li) synthLfo_[li].noteOn();   // fb228 — trigger modes LIVE: Retrig/Env reset per fresh note (the legato path above deliberately does NOT)
            foldStateA_.fill ({});   // Phase 11d ADAA — clear per-sine fold history on note start
            foldStateB_.fill ({});
            foldStateC_.fill ({});
            foldStateD_.fill ({});
            sampAirLpAL_ = sampAirLpAR_ = sampAirLpBL_ = sampAirLpBR_ = 0.f;
            airSmA_ = airSmB_ = airSmC_ = airSmD_ = 0.f;   // fb204
            sampAirLpCL_ = sampAirLpCR_ = sampAirLpDL_ = sampAirLpDR_ = 0.f;
            sampWarpFoldAL_ = sampWarpFoldAR_ = sampWarpFoldBL_ = sampWarpFoldBR_ = {};   // SAMPLE WARP fold history reset
            sampWarpFoldCL_ = sampWarpFoldCR_ = sampWarpFoldDL_ = sampWarpFoldDR_ = {};
            wtRectDcAL_.reset(); wtRectDcAR_.reset(); wtRectDcBL_.reset(); wtRectDcBR_.reset();   // RECTIFY DC-blocker reset (wavetable)
            wtRectDcCL_.reset(); wtRectDcCR_.reset(); wtRectDcDL_.reset(); wtRectDcDR_.reset();
            spRectDcAL_.reset(); spRectDcAR_.reset(); spRectDcBL_.reset(); spRectDcBR_.reset();   // RECTIFY DC-blocker reset (sample)
            spRectDcCL_.reset(); spRectDcCR_.reset(); spRectDcDL_.reset(); spRectDcDR_.reset();
            // Envelopes — fresh note starts from 0 (reset), then gate on. The legato
            // retarget path returns earlier (envelopes deliberately untouched), so this
            // only runs for true note starts. All five DAHDSR envelopes trigger together.
            ampEnv_.reset();    ampEnv_.noteOn();
            fltEnvT_.reset();   fltEnvT_.noteOn();
            pitchEnvT_.reset(); pitchEnvT_.noteOn();
            mod1EnvT_.reset();  mod1EnvT_.noteOn();
            mod2EnvT_.reset();  mod2EnvT_.noteOn();
            for (int k = 0; k < dynEnvCount_; ++k) { dynEnv_[k].reset(); dynEnv_[k].noteOn(); }   // fb177
            // Batch 1 — retrigger the per-voice LFO bank. Trig/Env/SustainLoop modes
            // reset phase to startPhase here; Free/Sync keep running.
            for (auto& lfo : synthLfo_) lfo.noteOn();
            // Reset filter state on note-on so a stale tail from a stolen
            // voice doesn't bleed into the new note's onset.
            filterSlot_.reset();
            filterSlot2_.reset();
            sendFilterSlot_.reset();     // fb287 — send mirror
            sendFilterSlot2_.reset();

            // Phase 8a polish — reset steal-fade state on new note
            stealing_         = false;
            stealingFade_     = 1.0f;
            stealingFadeStep_ = 0.0f;
            // Release-end declick — fresh note starts un-faded.
            finishing_     = false;
            finishFade_    = 1.0f;
            finishFadeStep_= 0.0f;
            // (noteStartStamp_ assigned at the top of startNote — shared with the LEGATO branch)
        }

        void stopNote (float, bool allowTailOff) override
        {
            if (allowTailOff)
            {
                ampEnv_.noteOff();
                fltEnvT_.noteOff();
                pitchEnvT_.noteOff();
                mod1EnvT_.noteOff();
                mod2EnvT_.noteOff();
                for (int k = 0; k < dynEnvCount_; ++k) dynEnv_[k].noteOff();   // fb177
            }
            else
            {
                // Phase 12 — Serum-2 style smooth voice steal. 30ms exponential fade
                // (audibly smooth, not just click-prevention) and the slot stays
                // OCCUPIED throughout (no clearCurrentNote here). Keeping the slot
                // means JUCE's findFreeVoice picks a different idle slot from the
                // 96-voice pool for the incoming note — so the dying note fades out
                // on its slot WHILE the new note rises on its own slot. The cap is
                // still enforced because UnisonSynth::noteOn excludes stealing
                // voices from the active count (see PluginProcessor.h). When the
                // fade completes, renderNextBlock calls clearCurrentNote().
                stealing_         = true;
                stealingFade_     = 1.0f;
                const float fadeSamples = static_cast<float>(0.030 * sampleRate_);
                stealingFadeStep_ = std::pow(0.001f, 1.0f / std::max(1.0f, fadeSamples));
            }
        }

        // Phase 12 — used by UnisonSynth to skip stealing voices when counting
        // toward the polyphony cap (a fading voice no longer "owns" a slot in
        // the user's perception, even though its currentlyPlayingNote is still
        // set so the slot doesn't get hijacked mid-fade).
        bool isStealing() const noexcept { return stealing_; }
        juce::uint32 getNoteStartStamp() const noexcept { return noteStartStamp_; }

        void pitchWheelMoved (int) override {}
        void controllerMoved (int, int) override {}

        // fb280 — per-osc reverb send (no-bleed). The processor hands each voice a shared
        // stereo send bus + 0/1 gains per source (A,B,C,D,Sub,Noise) each block; the voice
        // accumulates ONLY the routed oscillators' post-level/pan/amp-env samples into it.
        void setReverbSendTarget (float* L, float* R) noexcept { rvbSendL_ = L; rvbSendR_ = R; }
        void setReverbRoutes (float a, float b, float c, float d, float sub, float noise) noexcept
        {
            rvbG_[0] = a; rvbG_[1] = b; rvbG_[2] = c; rvbG_[3] = d; rvbG_[4] = sub; rvbG_[5] = noise;
            rvbAny_ = (a + b + c + d + sub + noise) > 0.0f;
        }
        // fb296 — per-osc DELAY send (parallel to the reverb send, fully independent mask).
        void setDelaySendTarget (float* L, float* R) noexcept { dlySendL_ = L; dlySendR_ = R; }
        void setDelayRoutes (float a, float b, float c, float d, float sub, float noise) noexcept
        {
            dlyG_[0] = a; dlyG_[1] = b; dlyG_[2] = c; dlyG_[3] = d; dlyG_[4] = sub; dlyG_[5] = noise;
            dlyAny_ = (a + b + c + d + sub + noise) > 0.0f;
        }
        // fb338 — per-osc DISTORTION send (third parallel bus, fully independent mask).
        void setDistortionSendTarget (float* L, float* R) noexcept { dstSendL_ = L; dstSendR_ = R; }
        void setDistortionRoutes (float a, float b, float c, float d, float sub, float noise) noexcept
        {
            dstG_[0] = a; dstG_[1] = b; dstG_[2] = c; dstG_[3] = d; dstG_[4] = sub; dstG_[5] = noise;
            dstAny_ = (a + b + c + d + sub + noise) > 0.0f;
        }
        // ════════ fb347 — THE SHARED "ROUTED DRY" EXCLUSION BUS ════════
        // The main-send exclusion used to be computed as (reverbSend + delaySend + distortionSend),
        // which DOUBLE-COUNTS any osc routed to more than one device: its dry is subtracted twice
        // from a mix that contains it once, so it returns PHASE-INVERTED. This bus carries each
        // routed osc EXACTLY ONCE — the union of every device's mask — so the exclusion becomes a
        // single subtraction that is correct no matter how many devices share a source.
        // It also retires the fb305/fb338 landmine: there is no longer a per-bus sum that a newly
        // added device can forget to join.
        // ════════ fb348 — POOLED INSTANCE SENDS (Delay 2..6, Distortion 2..6) ════════
        // Max: "there is no global send — an effect only affects what it is routed to."
        // Instance 1 of each device keeps its proven send path above; these are the extra
        // instances, each with its OWN mask and bus so a delay on osc C can never touch osc A.
        // The filter pair is heap-allocated ON DEMAND (message thread) the first time that
        // instance is routed, so an unrouted instance costs nothing: eager members would be
        // 10 extra FilterSlot PAIRS x 96 voices.
        static constexpr int kPoolSends = 93;              // fb352 — 5 delay + 5 distortion + 5 reverb · fb362 — + 6 granular · fb365 — + 6 tape · fb377 — + 6 filter · fb413 — + 6 chorus + 6 flanger + 6 phaser · fb426 — + 6 equalizer + 6 widen + 6 compress + 6 ott · fb444 — + 6 bode + 6 utility + 6 splitter
                                                           // ⚠️ must equal PluginProcessor::kPoolSendCount
        void setPoolSendTarget (int s, float* L, float* R) noexcept
        { if ((unsigned) s < kPoolSends) { poolSend_[s].L = L; poolSend_[s].R = R; } }
        void setPoolSendRoutes (int s, const float* g6) noexcept
        {
            if ((unsigned) s >= kPoolSends) return;
            auto& p = poolSend_[s];
            float sum = 0.0f;
            for (int k = 0; k < 6; ++k) { p.g[k] = g6[k]; sum += g6[k]; }
            p.any = sum > 0.0f;
        }
        void setExclusionSendTarget (float* L, float* R) noexcept { exSendL_ = L; exSendR_ = R; }
        // Build a pooled slot's send-filter pair on first use. ⚠️ ALLOCATES — call from the message
        // thread only (PluginProcessor does this in its per-block param scope, before render).
        void ensurePoolFilters (int s)
        {
            if ((unsigned) s >= kPoolSends) return;
            auto& p = poolSend_[s];
            if (p.flt1 != nullptr) return;
            p.flt1 = std::make_unique<tw::filters::FilterSlot>();
            p.flt2 = std::make_unique<tw::filters::FilterSlot>();
            p.flt1->prepare (poolFltSr_); p.flt2->prepare (poolFltSr_);
            p.flt1->setType (sendFilterSlot_.getType()); p.flt2->setType (sendFilterSlot2_.getType());
        }
        void setExclusionRoutes (float a, float b, float c, float d, float sub, float noise) noexcept
        {
            exG_[0] = a; exG_[1] = b; exG_[2] = c; exG_[3] = d; exG_[4] = sub; exG_[5] = noise;
            exAny_ = (a + b + c + d + sub + noise) > 0.0f;
        }

        void renderNextBlock (juce::AudioBuffer<float>& out,
                              int startSample, int numSamples) override
        {
            if (! playing_) return;

            // fb122 ROBIN Wobble — humanized late start: hold silence, then begin.
            // The envelope hasn't started, so the delayed entry is click-free.
            if (robinDelay_ > 0)
            {
                const int skip = juce::jmin (robinDelay_, numSamples);
                robinDelay_ -= skip; startSample += skip; numSamples -= skip;
                if (numSamples <= 0) return;
            }
            // fb122 ROBIN Fade/Overlap — a scheduled handover: after the wait, the old
            // station's tail arms the standard steal fade with the card's fade length.
            if (robinHandWait_ >= 0 && ! stealing_)
            {
                if (robinHandWait_ >= numSamples) robinHandWait_ -= numSamples;
                else
                {
                    robinHandWait_ = -1;
                    stealing_ = true; stealingFade_ = 1.0f;
                    stealingFadeStep_ = std::pow (0.001f, 1.0f / std::max (1.0f, robinHandFadeSec_ * (float) sampleRate_));
                }
            }

            // Phase 9: stereo scratch (OSC A + OSC B each pan independently).
            if (scratch_.getNumChannels() < 2 || scratch_.getNumSamples() < numSamples)
                scratch_.setSize (2, numSamples, false, true, true);
            scratch_.clear();
            auto* scratchL = scratch_.getWritePointer (0);
            auto* scratchR = scratch_.getWritePointer (1);
            // Per-osc filter routing buses (bus1 = scratch). Fully written each sample below.
            if (fltBus2_.getNumChannels() < 2 || fltBus2_.getNumSamples() < numSamples)
                fltBus2_.setSize (2, numSamples, false, true, true);
            if (fltDry_.getNumChannels() < 2 || fltDry_.getNumSamples() < numSamples)
                fltDry_.setSize (2, numSamples, false, true, true);
            auto* busB2L = fltBus2_.getWritePointer (0);
            auto* busB2R = fltBus2_.getWritePointer (1);
            auto* busDryL = fltDry_.getWritePointer (0);
            auto* busDryR = fltDry_.getWritePointer (1);
            // fb287 — reverb SEND buses (routed-osc subset, same filter split). Non-null only when a route
            // is active AND we have a send target, so the default path is untouched (no fill, no cost).
            const bool sendActive = (rvbSendL_ != nullptr) && rvbAny_;
            if (sendActive && (rvbSendF1_.getNumChannels() < 2 || rvbSendF1_.getNumSamples() < numSamples))
            { rvbSendF1_.setSize (2, numSamples, false, true, true); rvbSendF2_.setSize (2, numSamples, false, true, true); rvbSendDry_.setSize (2, numSamples, false, true, true); }
            float* sF1L = sendActive ? rvbSendF1_.getWritePointer (0) : nullptr;
            float* sF1R = sendActive ? rvbSendF1_.getWritePointer (1) : nullptr;
            float* sF2L = sendActive ? rvbSendF2_.getWritePointer (0) : nullptr;
            float* sF2R = sendActive ? rvbSendF2_.getWritePointer (1) : nullptr;
            float* sDryL = sendActive ? rvbSendDry_.getWritePointer (0) : nullptr;
            float* sDryR = sendActive ? rvbSendDry_.getWritePointer (1) : nullptr;
            // fb296 — DELAY send buses (independent mask, same filter split). Zero cost unless a delay route is active.
            const bool dlySendActive = (dlySendL_ != nullptr) && dlyAny_;
            if (dlySendActive && (dlySendF1_.getNumChannels() < 2 || dlySendF1_.getNumSamples() < numSamples))
            { dlySendF1_.setSize (2, numSamples, false, true, true); dlySendF2_.setSize (2, numSamples, false, true, true); dlySendDry_.setSize (2, numSamples, false, true, true); }
            float* dF1L = dlySendActive ? dlySendF1_.getWritePointer (0) : nullptr;
            float* dF1R = dlySendActive ? dlySendF1_.getWritePointer (1) : nullptr;
            float* dF2L = dlySendActive ? dlySendF2_.getWritePointer (0) : nullptr;
            float* dF2R = dlySendActive ? dlySendF2_.getWritePointer (1) : nullptr;
            float* dDryL = dlySendActive ? dlySendDry_.getWritePointer (0) : nullptr;
            float* dDryR = dlySendActive ? dlySendDry_.getWritePointer (1) : nullptr;
            // fb338 — DISTORTION send buses (third mask, same filter split). Zero cost unless routed.
            const bool dstSendActive = (dstSendL_ != nullptr) && dstAny_;
            if (dstSendActive && (dstSendF1_.getNumChannels() < 2 || dstSendF1_.getNumSamples() < numSamples))
            { dstSendF1_.setSize (2, numSamples, false, true, true); dstSendF2_.setSize (2, numSamples, false, true, true); dstSendDry_.setSize (2, numSamples, false, true, true); }
            float* tF1L = dstSendActive ? dstSendF1_.getWritePointer (0) : nullptr;
            float* tF1R = dstSendActive ? dstSendF1_.getWritePointer (1) : nullptr;
            float* tF2L = dstSendActive ? dstSendF2_.getWritePointer (0) : nullptr;
            float* tF2R = dstSendActive ? dstSendF2_.getWritePointer (1) : nullptr;
            float* tDryL = dstSendActive ? dstSendDry_.getWritePointer (0) : nullptr;
            float* tDryR = dstSendActive ? dstSendDry_.getWritePointer (1) : nullptr;
            // fb347 — the SHARED exclusion bus: same filter split, mask = the UNION of every device.
            // Zero cost unless at least one osc is routed somewhere.
            const bool exSendActive = (exSendL_ != nullptr) && exAny_;
            if (exSendActive && (exSendF1_.getNumChannels() < 2 || exSendF1_.getNumSamples() < numSamples))
            { exSendF1_.setSize (2, numSamples, false, true, true); exSendF2_.setSize (2, numSamples, false, true, true); exSendDry_.setSize (2, numSamples, false, true, true); }
            float* xF1L = exSendActive ? exSendF1_.getWritePointer (0) : nullptr;
            float* xF1R = exSendActive ? exSendF1_.getWritePointer (1) : nullptr;
            float* xF2L = exSendActive ? exSendF2_.getWritePointer (0) : nullptr;
            float* xF2R = exSendActive ? exSendF2_.getWritePointer (1) : nullptr;
            float* xDryL = exSendActive ? exSendDry_.getWritePointer (0) : nullptr;
            float* xDryR = exSendActive ? exSendDry_.getWritePointer (1) : nullptr;
            // fb348 — POOLED instance sends. Only slots that are BOTH routed and given a bus do any
            // work; everything else costs one bool test per block.
            bool  poolOn[kPoolSends] = {};
            for (int ps = 0; ps < kPoolSends; ++ps)
            {
                auto& P = poolSend_[ps];
                poolOn[ps] = (P.L != nullptr) && P.any && (P.flt1 != nullptr);
                if (poolOn[ps] && (P.f1.getNumChannels() < 2 || P.f1.getNumSamples() < numSamples))
                { P.f1.setSize (2, numSamples, false, true, true);
                  P.f2.setSize (2, numSamples, false, true, true);
                  P.dry.setSize (2, numSamples, false, true, true); }
            }
            // Per-block routing coefficients (independent + dry-bypass model): each source
            // (A,B,C,D,Sub) → F1 bus if in F1; → F2 bus if in F2 (parallel) or F2-only (series);
            // → dry if in neither. Multiply-by-0/1 keeps the per-sample sum branchless.
            {
                const bool par = (filterRouting_ != 0);
                anySrc1_ = anySrc2_ = false;
                for (int k = 0; k < 5; ++k)
                {
                    // fb79 — CONTINUOUS per-source sends: m1 of the source into the F1 bus; in series the
                    // F1 portion already flows on into F2, so the direct-to-F2 amount is only the excess
                    // max(0, m2−m1); dry = whatever's left (clamped — full dual sends leave no dry, same
                    // energy as the old binary dual-route). Reduces EXACTLY to the old 0/1 behaviour.
                    const float m1 = fltSrc1_[k], m2 = fltSrc2_[k];
                    busCo1_[k] = m1;
                    busCo2_[k] = par ? m2 : juce::jmax (0.0f, m2 - m1);
                    busCoD_[k] = juce::jmax (0.0f, 1.0f - m1 - busCo2_[k]);
                    anySrc1_ = anySrc1_ || (busCo1_[k] > 0.0005f);
                    anySrc2_ = anySrc2_ || (busCo2_[k] > 0.0005f);
                }
                // fb63 — NOISE routed like a 6th source (same F1/F2/parallel/dry rules).
                noiseCo1_ = noiseSrc1_ ? 1.0f : 0.0f;
                noiseCo2_ = (par ? noiseSrc2_ : (noiseSrc2_ && ! noiseSrc1_)) ? 1.0f : 0.0f;
                noiseCoD_ = (! noiseSrc1_ && ! noiseSrc2_) ? 1.0f : 0.0f;
                anySrc1_ = anySrc1_ || (noiseCo1_ != 0.0f);
                anySrc2_ = anySrc2_ || (noiseCo2_ != 0.0f);

                // fb123 — DRIVE NORMALIZATION: the post-filter drive is a SATURATOR; feeding it a
                // send-scaled signal ERASES the send once hot (tanh ceiling: a 5% send and a 100%
                // send come out identical — Max: "at 3% it's already 100"). pdrive now drives the
                // signal normalized to the bus's send level and restores it after: the TONE stays
                // constant, the LOUDNESS tracks the send. Full sends (norm 1) are bit-exact.
                float dn1 = 0.0f, dn2 = 0.0f;
                for (int k = 0; k < 5; ++k) { dn1 = juce::jmax (dn1, busCo1_[k]); dn2 = juce::jmax (dn2, busCo2_[k]); }
                dn1 = juce::jmax (dn1, noiseCo1_); dn2 = juce::jmax (dn2, noiseCo2_);
                if (! par) dn2 = juce::jmax (dn2, dn1);       // series: F1's restored output flows on
                drvNorm1_ = dn1 > 0.0005f ? juce::jlimit (0.05f, 1.0f, dn1) : 1.0f;
                drvNorm2_ = dn2 > 0.0005f ? juce::jlimit (0.05f, 1.0f, dn2) : 1.0f;
            }

            // KEYTRACK + ROUTE — resolve effective destination values, clamped.
            // KEYTRACK (mod route #1) is a CROSSFADE from the knob toward a pure pitch ramp:
            //   effective = base + depth·(ramp − base)
            //   depth 0   → base (knob works normally, keytrack off)
            //   depth 100 → ramp: lowest note (C1)=0 SILENT (no leak whatever the knob is),
            //               highest (C6)=1.0 FULL. Independent of the knob value, so it can
            //               neither clamp to max ("mix knob" bug) nor collapse to zero when
            //               the knob is at 0 ("does nothing" bug). = "bottom nothing, top full".
            // ROUTE (mod route #2) is ADDITIVE/bipolar. Sources: Velocity (linear — the one
            // that nails per-note realism) or Note. The NOTE source is CURVED (ramp^kRtNoteCurve)
            // so the bottom half of the keyboard stays closed and it only blooms open up top
            // (per Max: "half closed, half open, hella open at the top"). The render path reads
            // only the effective members; more sources/routes later accumulate into these.
            {
                const float ktDA = ktDepthA_, ktDB = ktDepthB_;   // 0..1 keytrack depth per OSC
                // ROUTE — per-OSC source value × bipolar amount. Velocity stays linear; Note is
                // shaped by a power curve so low notes contribute ~0 and the top blooms.
                const float noteCurved = std::pow (ktRamp_, kRtNoteCurve);
                const float rtSrcA = (routeSrcA_ == kRtSrcVel) ? currentVelocity_ : noteCurved;
                const float rtSrcB = (routeSrcB_ == kRtSrcVel) ? currentVelocity_ : noteCurved;
                const float rtA = routeAmtA_ * rtSrcA;     // bipolar -1..+1 × 0..1
                const float rtB = routeAmtB_ * rtSrcB;
                const float ktDC = ktDepthC_, ktDD = ktDepthD_;
                const float rtSrcC = (routeSrcC_ == kRtSrcVel) ? currentVelocity_ : noteCurved;
                const float rtSrcD = (routeSrcD_ == kRtSrcVel) ? currentVelocity_ : noteCurved;
                const float rtC = routeAmtC_ * rtSrcC;
                const float rtD = routeAmtD_ * rtSrcD;
                // ── Mod-matrix: LFO → frame/warp/fold per OSC (block-rate via peek(), so the
                //    per-sample OSC render stays cheap). LFO→LFO 'amt' scales the source first.
                // fb178 — matrix-routed dynamic envelopes advance once per block
                // (dormant slots stay untouched — the zero-CPU law).
                if (dynEnvUsedMask_ != 0)
                    for (int kD = 0; kD < dynEnvCount_; ++kD)
                        if (dynEnvUsedMask_ & (1u << kD))
                            for (int n2 = 0; n2 < numSamples; ++n2) dynEnv_[kD].tick();
                envCutBlk1_ = 0.0f; envCutBlk2_ = 0.0f;
                float lfoPk[wc::NUM_LFOS];
                for (int L = 0; L < wc::NUM_LFOS; ++L) lfoPk[L] = synthLfo_[L].peek();
                {
                    float amt[wc::NUM_LFOS] = { 0.0f };
                    for (int a = 0; a < modConfig_.numAssignments; ++a)
                    {
                        const auto& as = modConfig_.assignments[a];
                        if (! as.enabled) continue;
                        const int sI = (int) as.source, dI = (int) as.dest;
                        float sv2;
                        if      (sI >= 0 && sI < wc::NUM_LFOS) sv2 = lfoPk[sI];
                        else if (wc::isEnvModSource (sI))      sv2 = envSourceValue (sI);   // fb178
                        else continue;
                        if (dI >= (int) wc::ModDest::LfoAmt1 && dI < (int) wc::ModDest::LfoAmt1 + wc::NUM_LFOS)
                            amt[dI - (int) wc::ModDest::LfoAmt1] += sv2 * as.depth;
                    }
                    for (int L = 0; L < wc::NUM_LFOS; ++L) lfoPk[L] *= juce::jlimit (0.0f, 2.0f, 1.0f + amt[L]);
                }
                envLvlOwn_[0] = envLvlOwn_[1] = envLvlOwn_[2] = envLvlOwn_[3] = 0.0f;
                envLvlDrive_[0] = envLvlDrive_[1] = envLvlDrive_[2] = envLvlDrive_[3] = 0.0f;
                float vOwnW[12] = { 0 }, vOwnV[12] = { 0 };   // fb188 — ownership claims [Fr,Wp,Fd]×[A..D]: knob-0 + atten-100 follows the shape (Max's iffy warp)
                float mFrA = 0.0f, mWpA = 0.0f, mFdA = 0.0f, mFrB = 0.0f, mWpB = 0.0f, mFdB = 0.0f;
                float mFrC = 0.0f, mWpC = 0.0f, mFdC = 0.0f, mFrD = 0.0f, mWpD = 0.0f, mFdD = 0.0f;
                float mCrs[4] = { 0.f, 0.f, 0.f, 0.f };   // COARSE mod (semitones, per osc)
                float mSw[4]  = { 0.f, 0.f, 0.f, 0.f };   // SUB Weight mod
                float mSh[4]  = { 0.f, 0.f, 0.f, 0.f };   // SUB Heat mod
                for (int a = 0; a < modConfig_.numAssignments; ++a)
                {
                    const auto& as = modConfig_.assignments[a];
                    if (! as.enabled) continue;
                    const int sI = (int) as.source;
                    // source value: LFO peak (0..9), or a FLOW·DRIFT lane (Drift1..Drift8, block-rate bipolar)
                    float srcV;
                    if      (sI >= 0 && sI < wc::NUM_LFOS) srcV = lfoPk[sI];
                    else if (sI >= (int) wc::ModSource::Drift1 && sI < (int) wc::ModSource::Drift1 + 8)
                                                          srcV = modConfig_.driftLanes[sI - (int) wc::ModSource::Drift1];
                    else if (wc::isEnvModSource (sI))     srcV = envSourceValue (sI);   // fb178
                    else if (wc::isFollowModSource (sI))  srcV = followSourceValue (sI);   // fb552 — audio as a source
                    else if (wc::isNoteModSource (sI))    srcV = ktRamp_ - 1.0f;              // fb555 — key tracking, level-1 like every shape source
                    else if (sI == (int) wc::ModSource::Velocity) srcV = std::pow (juce::jlimit (0.0f, 1.0f, currentVelocity_), std::pow (3.0f, 1.0f - 2.0f * velDepth_));   // fb262 — velocity source, CURVE-shaped (velDepth_ repurposed as the curve: 0.5=linear, >0.5 lifts soft hits, <0.5 hardens)
                    else continue;
                    // fb554 — THE CONNECTION CURVE, applied here and nowhere else. This is the last
                    //  line before srcV fans out into the ownership laws, the semitone sums and the
                    //  per-sample paths, so every one of them sees the curved value and none of
                    //  them needs to know the curve exists.
                    if (as.curve >= 0)
                        srcV = wc::applyModCurve (modCurves_ != nullptr ? modCurves_->load (std::memory_order_acquire) : nullptr,
                                                  as.curve, sI, srcV);
                    // fb183 — env→LEVEL is OWNERSHIP, not offset: depth crossfades the knob
                    // toward the envelope's own shape (eff = (1−Σd)·knob + Σd·env). At 100%
                    // the shape IS the level — knob anywhere, Serum's level-down pluck included.
                    // Per-voice: each note plucks its OWN level (the mono-tap ghost is dead).
                    if (wc::isShapeModSource (sI)   // fb552 — a FOLLOWER owns Level exactly like an env: this line is the reel (the noise plucking Osc A's volume)
                        && (int) as.dest >= (int) wc::ModDest::LevelA
                        && (int) as.dest <= (int) wc::ModDest::LevelD)
                    {
                        const int gI = (int) as.dest - (int) wc::ModDest::LevelA;
                        const float dW = std::abs (as.depth);
                        envLvlOwn_[gI]   += dW;
                        envLvlDrive_[gI] += dW * (srcV + 1.0f);   // srcV is level−1 → restore raw 0..1
                        continue;
                    }
                    // fb188 — same OWNERSHIP law for the voice-evaluated wavetable trio
                    // (Frame/Warp/Fold, all four oscs). Semitone dests (Coarse/Cut) stay
                    // offset; LfoAmt stays multiplicative.
                    if (wc::isShapeModSource (sI))   // fb552 — and the wavetable trio the same way
                    {
                        int vi = -1;
                        switch (as.dest)
                        {
                            case wc::ModDest::Frame:  vi = 0;  break; case wc::ModDest::Warp:  vi = 1;  break; case wc::ModDest::Fold:  vi = 2;  break;
                            case wc::ModDest::FrameB: vi = 3;  break; case wc::ModDest::WarpB: vi = 4;  break; case wc::ModDest::FoldB: vi = 5;  break;
                            case wc::ModDest::FrameC: vi = 6;  break; case wc::ModDest::WarpC: vi = 7;  break; case wc::ModDest::FoldC: vi = 8;  break;
                            case wc::ModDest::FrameD: vi = 9;  break; case wc::ModDest::WarpD: vi = 10; break; case wc::ModDest::FoldD: vi = 11; break;
                            default: break;
                        }
                        if (vi >= 0)
                        {
                            const float dwV = std::abs (as.depth);
                            vOwnW[vi] += dwV; vOwnV[vi] += dwV * (srcV + 1.0f);
                            continue;
                        }
                    }
                    const float c = wc::routeContribution (wc::kDestInfo[(int) as.dest], srcV, as.depth);
                    // fb178 — env→cutoff joins the filter's semitone sum as a block constant
                    // (LFO→cutoff stays per-sample below; envs advance per block anyway).
                    if (wc::isShapeModSource (sI) || sI == (int) wc::ModSource::Velocity)   // fb260 · fb552 followers join the block-constant sum — velocity→cutoff joins the block-constant semitone sum, exactly like env
                    {
                        if      (as.dest == wc::ModDest::Cut1) { envCutBlk1_ += c; continue; }
                        else if (as.dest == wc::ModDest::Cut2) { envCutBlk2_ += c; continue; }
                    }
                    switch (as.dest)
                    {
                        case wc::ModDest::Frame:  mFrA += c; break;
                        case wc::ModDest::Warp:   mWpA += c; break;
                        case wc::ModDest::Fold:   mFdA += c; break;
                        case wc::ModDest::FrameB: mFrB += c; break;
                        case wc::ModDest::WarpB:  mWpB += c; break;
                        case wc::ModDest::FoldB:  mFdB += c; break;
                        case wc::ModDest::FrameC: mFrC += c; break;
                        case wc::ModDest::WarpC:  mWpC += c; break;
                        case wc::ModDest::FoldC:  mFdC += c; break;
                        case wc::ModDest::FrameD: mFrD += c; break;
                        case wc::ModDest::WarpD:  mWpD += c; break;
                        case wc::ModDest::FoldD:  mFdD += c; break;
                        case wc::ModDest::CoarseA: mCrs[0] += c; break;
                        case wc::ModDest::CoarseB: mCrs[1] += c; break;
                        case wc::ModDest::CoarseC: mCrs[2] += c; break;
                        case wc::ModDest::CoarseD: mCrs[3] += c; break;
                        case wc::ModDest::SubWeightA: mSw[0] += c; break;
                        case wc::ModDest::SubWeightB: mSw[1] += c; break;
                        case wc::ModDest::SubWeightC: mSw[2] += c; break;
                        case wc::ModDest::SubWeightD: mSw[3] += c; break;
                        case wc::ModDest::SubHeatA: mSh[0] += c; break;
                        case wc::ModDest::SubHeatB: mSh[1] += c; break;
                        case wc::ModDest::SubHeatC: mSh[2] += c; break;
                        case wc::ModDest::SubHeatD: mSh[3] += c; break;
                        default: break;
                    }
                }
                // FRAME/WARP/FOLD — keytrack crossfade + ROUTE + LFO mod, clamp once.
                coarseModA_ = mCrs[0]; coarseModB_ = mCrs[1]; coarseModC_ = mCrs[2]; coarseModD_ = mCrs[3];
                for (int o = 0; o < 4; ++o) { subWMod_[o] = mSw[o]; subHMod_[o] = mSh[o]; }
                // fb188 — ownership applied at the wavetable-trio app sites (w=0 → legacy exactly)
                auto ownV = [&] (float base, int vi) noexcept
                { const float w = juce::jmin (1.0f, vOwnW[vi]); return juce::jlimit (0.0f, 1.0f, base * (1.0f - w) + vOwnV[vi]); };
                framePosT_    = ownV (framePosBase_    + (ktDestA_ == kKtFrame ? ktDA * (ktRamp_ - framePosBase_)    : 0.0f) + (routeDestA_ == kRtFrame ? rtA : 0.0f) + mFrA + flowWave_, 0);
                warpAmtT_  = ownV (warpAmountBase_  + (ktDestA_ == kKtWarp  ? ktDA * (ktRamp_ - warpAmountBase_)  : 0.0f) + (routeDestA_ == kRtWarp  ? rtA : 0.0f) + mWpA, 1);
                foldAmtTA_ = ownV (foldAmountBaseA_ + (ktDestA_ == kKtFold  ? ktDA * (ktRamp_ - foldAmountBaseA_) : 0.0f) + (routeDestA_ == kRtFold  ? rtA : 0.0f) + mFdA, 2);
                framePosTB_   = ownV (framePosBaseB_   + (ktDestB_ == kKtFrame ? ktDB * (ktRamp_ - framePosBaseB_)   : 0.0f) + (routeDestB_ == kRtFrame ? rtB : 0.0f) + mFrB + flowWave_, 3);
                warpAmtTB_ = ownV (warpAmountBaseB_ + (ktDestB_ == kKtWarp  ? ktDB * (ktRamp_ - warpAmountBaseB_) : 0.0f) + (routeDestB_ == kRtWarp  ? rtB : 0.0f) + mWpB, 4);
                warp2AmtTA_ = warp2AmountBaseA_;   // WARP 2 base->effective (mod-matrix ready)
                warp2AmtTB_ = warp2AmountBaseB_;
                foldAmtTB_ = ownV (foldAmountBaseB_ + (ktDestB_ == kKtFold  ? ktDB * (ktRamp_ - foldAmountBaseB_) : 0.0f) + (routeDestB_ == kRtFold  ? rtB : 0.0f) + mFdB, 5);
                // OSC C / D — same keytrack + route + LFO mod, clamp once.
                framePosTC_   = ownV (framePosBaseC_   + (ktDestC_ == kKtFrame ? ktDC * (ktRamp_ - framePosBaseC_)   : 0.0f) + (routeDestC_ == kRtFrame ? rtC : 0.0f) + mFrC + flowWave_, 6);
                warpAmtTC_ = ownV (warpAmountBaseC_ + (ktDestC_ == kKtWarp  ? ktDC * (ktRamp_ - warpAmountBaseC_) : 0.0f) + (routeDestC_ == kRtWarp  ? rtC : 0.0f) + mWpC, 7);
                foldAmtTC_ = ownV (foldAmountBaseC_ + (ktDestC_ == kKtFold  ? ktDC * (ktRamp_ - foldAmountBaseC_) : 0.0f) + (routeDestC_ == kRtFold  ? rtC : 0.0f) + mFdC, 8);
                warp2AmtTC_ = warp2AmountBaseC_;
                framePosTD_   = ownV (framePosBaseD_   + (ktDestD_ == kKtFrame ? ktDD * (ktRamp_ - framePosBaseD_)   : 0.0f) + (routeDestD_ == kRtFrame ? rtD : 0.0f) + mFrD + flowWave_, 9);
                warpAmtTD_ = ownV (warpAmountBaseD_ + (ktDestD_ == kKtWarp  ? ktDD * (ktRamp_ - warpAmountBaseD_) : 0.0f) + (routeDestD_ == kRtWarp  ? rtD : 0.0f) + mWpD, 10);
                foldAmtTD_ = ownV (foldAmountBaseD_ + (ktDestD_ == kKtFold  ? ktDD * (ktRamp_ - foldAmountBaseD_) : 0.0f) + (routeDestD_ == kRtFold  ? rtD : 0.0f) + mFdD, 11);
                warp2AmtTD_ = warp2AmountBaseD_;
                // fb204 — FRAME glide (block pole, the blur pattern): the blend cache rebuilds
                // once per block, so smoothing lives at block rate — each hop shrinks the step ~4×.
                auto bpole = [] (float& live, float tgt) noexcept
                { if (std::abs (tgt - live) < 1.0e-4f) live = tgt; else live += (tgt - live) * 0.25f; };
                bpole (framePos_, framePosT_);   bpole (framePosB_, framePosTB_);
                bpole (framePosC_, framePosTC_); bpole (framePosD_, framePosTD_);
                // fb204 — FOLD ramp (start/step): lands EXACTLY on target at block end so the
                // ADAA change-gate re-idles; a free-running pole would thrash the Fx1 cache.
                const float invN = 1.0f / (float) juce::jmax (1, numSamples);
                foldStepA_ = (foldAmtTA_ - foldAmountA_) * invN;
                foldStepB_ = (foldAmtTB_ - foldAmountB_) * invN;
                foldStepC_ = (foldAmtTC_ - foldAmountC_) * invN;
                foldStepD_ = (foldAmtTD_ - foldAmountD_) * invN;
            }

            // WAVER — advance per-(osc × unison sine) OU pitch drift this block. Slow,
            // bounded, decorrelated; cents are consumed by updateUnisonPhaseIncrements*.
            {
                const float dt = static_cast<float> (numSamples) / static_cast<float> (sampleRate_);
                updateWaverOU (waverCentsA_, waverRngA_, waverA_, dt);
                updateWaverOU (waverCentsB_, waverRngB_, waverB_, dt);
                updateWaverOU (waverCentsC_, waverRngC_, waverC_, dt);
                updateWaverOU (waverCentsD_, waverRngD_, waverD_, dt);

                // fb302 — advance the subtle ALWAYS-ON analog pitch drift (single per-voice OU
                // wander; same AR(1) form as WAVER but slow τ≈1.2 s and shallow ~±2¢, no depth gate).
                // Combined with the per-note static offset → analogDetuneSemis_ feeds every osc pitch.
                {
                    const float phi = std::exp (-dt / 1.2f);                 // τ = 1.2 s (slow)
                    const float sig = 2.0f * std::sqrt (1.0f - phi * phi);   // ~±2¢ drift σ
                    float x = phi * analogDriftCents_ + sig * waverGaussian (analogRng_);
                    x = juce::jlimit (-6.0f, 6.0f, x);                       // never run away
                    if (x < 1.0e-20f && x > -1.0e-20f) x = 0.0f;            // denormal flush
                    analogDriftCents_  = x;
                    analogDetuneSemis_ = (double) (analogStaticCents_ + analogDriftCents_) * 0.01;
                }
            }
            // PORTAMENTO — advance the pitch slide for this block (no-op once arrived).
            advanceGlide (numSamples);

            // ── Per-envelope value PRE-PASS (mod-matrix foundation) ────────────
            // Tick all FIVE envelopes once per sample into envScratch_ (ch0=AMP,
            // 1=FLT, 2=PITCH, 3=MOD1, 4=MOD2). The amp loop, the filter loop and
            // the per-block pitch/mod routing below all read these SAME buffers, so
            // every envelope runs at audio rate — the exact shape the master mod
            // matrix needs (zero rewrite when it arrives).
            if (envScratch_.getNumChannels() < 5 || envScratch_.getNumSamples() < numSamples)
                envScratch_.setSize (5, numSamples, false, true, true);
            {
                float* eAmp = envScratch_.getWritePointer (0);
                float* eFlt = envScratch_.getWritePointer (1);
                float* ePit = envScratch_.getWritePointer (2);
                float* eM1  = envScratch_.getWritePointer (3);
                float* eM2  = envScratch_.getWritePointer (4);
                // CPU: envs 2–5 (ch1..4) are consumed ONLY as envDepth_[c] × value in every
                // routing site (amp/filter/pitch/mod). So depth 0 ⇒ that env contributes NOTHING
                // anywhere — skip its per-sample tick and write 0 (bit-identical output). By
                // default all four depths are 0, so an UNROUTED envelope now costs nothing
                // instead of ticking (and exp'ing its curve) 48 000×/s per voice. AMP always ticks.
                const bool needFlt = (envDepth_[1] != 0.0f) || (legEnvUsedMask_ & 1u) != 0;   // fb178
                const bool needPit = (envDepth_[2] != 0.0f) || (legEnvUsedMask_ & 2u) != 0;
                const bool needM1  = (envDepth_[3] != 0.0f) || (legEnvUsedMask_ & 4u) != 0;
                const bool needM2  = (envDepth_[4] != 0.0f) || (legEnvUsedMask_ & 8u) != 0;
                for (int k = 0; k < numSamples; ++k)
                {
                    eAmp[k] = (float) ampEnv_.tick();
                    eFlt[k] = needFlt ? (float) fltEnvT_.tick() : 0.0f;
                    ePit[k] = needPit ? (float) pitchEnvT_.tick() : 0.0f;
                    eM1[k]  = needM1  ? (float) mod1EnvT_.tick() : 0.0f;
                    eM2[k]  = needM2  ? (float) mod2EnvT_.tick() : 0.0f;
                }
            }
            // PITCH + MOD-bus routing (per-block; block-end value of each free env).
            // Any of envs 2–5 routed to Pitch sum into pitchEnvSemis_ (±48 ST × depth).
            // Mod 1/2 destinations fill the latent buses for the mod matrix.
            {
                const int last = (numSamples > 0) ? (numSamples - 1) : 0;
                double pit = 0.0, m1 = 0.0, m2 = 0.0;
                for (int k = 0; k < 4; ++k)                      // free env k → internal ch (k+1)
                {
                    const int    d  = envDest_[k + 1];
                    const double dv = (double) envDepth_[k + 1]
                                    * (double) envScratch_.getReadPointer (k + 1)[last];
                    if      (d == kEnvPitch) pit += dv * 48.0;   // ±48 semitones
                    else if (d == kEnvMod1)  m1  += dv;          // ±100% bus (latent)
                    else if (d == kEnvMod2)  m2  += dv;
                }
                pitchEnvSemis_ = pit;
                mod1Bus_ = m1; mod2Bus_ = m2;
            }
            // fb522 — URANGE glide (block-rate; see advanceUnisonRangeGlide). It must run BEFORE
            // the increments are re-derived, because it rewrites the per-sine detune-cents table.
            advanceUnisonRangeGlide (numSamples);
            // Re-derive per-sine phase increments with updated drift + glide pitch
            updateUnisonPhaseIncrementsA (glideNote_);
            updateUnisonPhaseIncrementsB (glideNote_);
            updateUnisonPhaseIncrementsC (glideNote_);
            updateUnisonPhaseIncrementsD (glideNote_);
            // fb522 — SUB AS A BLEND SOURCE: the arm pass further down (the one that fills
            // modSrcForce_/blkCarrierArmed_) runs AFTER prepareSubBlock, so the Sub force flag
            // has to be derived here or it would always be one block late. TRAP, measured:
            // prepareSubBlock gates the whole lane on `wEff > 1e-5f`, so at Sub Mix 0 the sub
            // oscillator does not tick at all and the modulator tap reads silence — which is
            // exactly the "turn it down, still hear it modulate" behaviour modSrcForce_ exists
            // to prevent for the oscs. Forcing the lane on is inaudible: its weight still ramps
            // to 0, so subMix contributes v*0 and multiplies the osc by n = 1.0f.
            for (int o = 0; o < 4; ++o) subForce_[o] = false;
            for (int c = 0; c < 4; ++c)
                for (int sl = 0; sl < 4; ++sl)
                {
                    const BlendSlotV& b = blendSlot_[c][sl];
                    if (blendIsLive (b.mode) && b.src == 4 && b.depth >= 1.0e-6f)   // fb523 — TRACKS the arm pass and the inner loop; all three thresholds are one threshold. A Sub-sourced slot below this gate never ticks its sub lane, so subModTap_ stays 0 and the slot is silently inert.
                        subForce_[c] = true;                     // Sub(4) = the CARRIER'S OWN sub
                }
            prepareSubBlock (numSamples);   // SUB — voice-anchored per-osc sub lanes (universal box)

            // Phase 10a / Phase 8b — pick mip level using sine 0 (centre-detuned,
            // no spread offset) as the reference — ±25 cents of unison detune
            // doesn't cross a mip boundary in practice.
            // Phase 11d AA — phase-multiply warps (SYNC, FORMANT, FRACTALIZE) read the
            // table faster than the base increment, so pick the mip for the WARPED rate
            // (more band-limited → far less aliasing). Other modes use rate ×1.
            // ⚠️ LOCKSTEP with applyPhaseWarp: these three constants ARE the ones at cases 2/3/7.
            //    fb545 — these now read kSyncExp2 / kFormantExp2 / kFractalMul, ONE definition each
            //    at the top of the class, so the trap below cannot recur by construction. The
            //    trap was real and is worth keeping written down:
            //    🔑 reverting a ratio here and not in applyPhaseWarp (or the reverse) compiles
            //    clean and is SILENT — the mip is then picked for a rate the reader never uses.
            //    A constant that also feeds a SELECTION path must live in exactly one place.
            auto warpRateMul = [] (int mode, float amt, double drawSlope = 1.0) -> double
            { return warpReadRate (mode, amt, drawSlope); };   // fb561 — one definition, now a static (below)
            // fb522 — UNISON-aware mip pick. The mip is chosen from sine 0's rate, and the old
            // premise ("±25 cents of unison detune doesn't cross a mip boundary") dies the moment
            // URANGE reaches 4800 cents or STACK adds +36 semitones: an outer sine reading a table
            // band-limited for the BASE pitch transposes its top harmonics straight through
            // Nyquist. Widen the reference by the largest UPWARD offset, counting only the part
            // that exceeds the legacy ±50-cent constant — so anything inside the old range picks
            // exactly the mip it always picked (that is the URANGE = 50 bit-identity gate), and
            // the widening is continuous across the threshold (no mip pop at 50.0001 cents).
            auto uniRateMul = [] (const std::array<float, kMaxUnison>& cents, int cnt) -> double
            {
                float m = 0.0f;
                for (int u = 0; u < cnt; ++u) m = juce::jmax (m, cents[(size_t) u]);
                return (m <= kUniMaxDetuneCents) ? 1.0
                     : std::pow (2.0, (double) (m - kUniMaxDetuneCents) / 1200.0);
            };
            // fb522 — the WARP FAN (UWARP) raises the warp amount on the outer sines, so the mip
            // must be picked for the fanned maximum too. |uwarp| = 0 → jlimit of the unchanged
            // amount → bit-identical.
            auto warpFan = [this] (int osc, float amt) -> float
            { // fb553 — the blend-warp swing widens the same worst case the unison fan does, and for
              //  the same reason: the mip is picked once per block for the widest amount anything
              //  will actually read. Zero when no mode-6 slot is armed, so this stays bit-identical.
              const float w = std::fabs (uniWarp_[(size_t) osc]) + blendWarpMax_[(size_t) osc];
              return w == 0.0f ? amt : juce::jlimit (0.0f, 1.0f, amt + w); };
            // ── FM-ENGINE-VOICE block-rate conditioning + WEATHERING SUITE slow processes ──
            // Smooth every knob, run STRIKE's decay and AGE's drift walks, then fold it all
            // into the per-osc EFFECTIVE values the per-sample core reads. All pow()/exp()
            // happen here, once per block — the sample loop stays lean.
            {
                const double blkSec = (double) numSamples / sampleRate_;
                const float ouK = (float) juce::jmin (0.5, blkSec * 3.0);           // ~0.3s correlation
                const float ouS = (float) std::sqrt (juce::jmax (1.0e-6, blkSec)) * 0.9f;
                const float kInvExp4m1 = 1.0f / (std::exp (4.0f) - 1.0f);   // SCORCH exp-bias normaliser
                fmIdxGlideCoef_ = 1.0f - std::exp (-1.0f / (float) (sampleRate_ * 0.0012));  // ~1.2ms — AGE de-zipper
                for (size_t o = 0; o < 4; ++o)
                {
                    fmD1Sm_[o] += (fmDepth1_[o] - fmD1Sm_[o]) * 0.35f;
                    fmD2Sm_[o] += (fmDepth2_[o] - fmD2Sm_[o]) * 0.35f;
                    fmFbSm_[o] += (fmFbAmt_[o]  - fmFbSm_[o]) * 0.35f;
                    fmStrikeSm_[o] += (fmStrike_[o]   - fmStrikeSm_[o]) * 0.35f;
                    fmAgeSm_[o]    += (fmAge_[o]      - fmAgeSm_[o])    * 0.35f;
                    fmRustSm_[o]   += (fmRust_[o]     - fmRustSm_[o])   * 0.35f;
                    fmQuakeSm_[o]  += (fmQuakeKnob_[o]  - fmQuakeSm_[o])  * 0.35f;
                    fmScorchSm_[o] += (fmScorchKnob_[o] - fmScorchSm_[o]) * 0.35f;
                    fmStormSm_[o]  += (fmStorm_[o]    - fmStormSm_[o])  * 0.35f;

                    // STRIKE — velocity-scaled index transient (the DX secret): exponential
                    // decay, tau grows with the knob (fast pluck → slow bloom).
                    const float stk = fmStrikeSm_[o];
                    if (fmStrikeEnv_[o] > 1.0e-4f)
                        fmStrikeEnv_[o] *= (float) std::exp (-blkSec / (0.025 + 0.230 * (double) stk));
                    const float strikeAdd = stk * stk * 2.2f * currentVelocity_ * fmStrikeEnv_[o];

                    // AGE — analog operator instability: two decorrelated OU walks (ratio +
                    // index) plus the per-note S&H offset rolled at note-on.
                    const float age2 = fmAgeSm_[o] * fmAgeSm_[o];
                    if (age2 > 1.0e-6f)
                    {
                        fmNz_ ^= fmNz_ << 13; fmNz_ ^= fmNz_ >> 17; fmNz_ ^= fmNz_ << 5;
                        const float r1n = (float) (std::int32_t) fmNz_ * (1.0f / 2147483648.0f);
                        fmNz_ ^= fmNz_ << 13; fmNz_ ^= fmNz_ >> 17; fmNz_ ^= fmNz_ << 5;
                        const float r2n = (float) (std::int32_t) fmNz_ * (1.0f / 2147483648.0f);
                        fmAgeOuR_[o] = juce::jlimit (-1.0f, 1.0f, fmAgeOuR_[o] - ouK * fmAgeOuR_[o] + ouS * r1n);
                        fmAgeOuI_[o] = juce::jlimit (-1.0f, 1.0f, fmAgeOuI_[o] - ouK * fmAgeOuI_[o] + ouS * r2n);
                    }
                    const float ratioWob1 = 1.0f + age2 * 0.018f * (fmAgeOuR_[o] + 0.6f * fmAgeNote_[o]);
                    const float ratioWob2 = 1.0f + age2 * 0.018f * (0.7f * fmAgeOuI_[o] - 0.5f * fmAgeNote_[o]);
                    const float idxWob    = 1.0f + age2 * 0.30f  * fmAgeOuI_[o];

                    // Page-1 refinement: hotter ceiling + more mid-knob throw (d^1.7 · 2.0
                    // turns, was d² · 1.5) and DX key scaling (fmKs_, set at note-on).
                    const float d1base = std::pow (fmD1Sm_[o], 1.7f) * 2.0f;
                    const float d2base = std::pow (fmD2Sm_[o], 1.7f) * 2.0f;
                    fmD1Eff_[o] = (d1base + strikeAdd) * idxWob * fmKs_;
                    fmD2Eff_[o] = (d2base + 0.5f * strikeAdd) * idxWob * fmKs_;
                    fmFbEff_[o] = fmFbSm_[o] * fmFbSm_[o];
                    fmR1Eff_[o] = (double) fmRatio1_[o] * (double) ratioWob1;
                    fmR2Eff_[o] = (double) fmRatio2_[o] * (double) ratioWob2;

                    // RUST — absolute-Hz inharmonic offset on M1 (Chowning's bell trick):
                    // detaches M1 from the harmonic grid → shimmer/beating → full clang.
                    fmRustTps_[o] = (double) (fmRustSm_[o] * fmRustSm_[o]) * 45.0 / sampleRate_;

                    // ── SCORCH — in-loop drive: asymmetric waveshaping of the modulators (breeds
                    // new sidebands), a self-feedback push (sine→saw grit), and an FM index push.
                    // exp-bias taper on drive (project DSP rule — never s^k); feedback quadratic.
                    const float sc  = fmScorchSm_[o];
                    const float sc2 = sc * sc;
                    fmScorchIdxMul_[o]   = 1.0f + 0.6f * sc2;                              // hotter FM (1.0 → 1.6)
                    const float scDrive  = (std::exp (4.0f * sc) - 1.0f) * kInvExp4m1;     // exp-bias 0..1
                    fmScorchPre_[o]      = 1.0f + 3.0f * scDrive;                          // shaper input gain (1 → 4)
                    fmScorchBias_[o]     = 0.35f * sc;                                     // asymmetry → even harmonics
                    fmScorchTanhBias_[o] = fmFastTanh (fmScorchBias_[o]);                  // DC re-center of the shaper
                    fmScorchMakeup_[o]   = 1.0f / juce::jmax (0.30f, fmFastTanh (fmScorchPre_[o] + fmScorchBias_[o]));
                    const float scFbAdd  = 0.30f * sc2;                                    // extra self-feedback grit
                    fmFbEff_[o] = juce::jmin (1.20f, fmFbEff_[o] + scFbAdd);               // fold grit into the feedback path

                    // ── QUAKE — phase-locked subharmonic FM: depth (q²), octave-anchored ratio
                    // (0.5 → 0.25, the second octave only opens up top), 1/ratio index comp (capped
                    // 2× so it holds Hz-deviation without mud), and a top-third "fry" shaper.
                    const float q   = fmQuakeSm_[o];
                    const float q2  = q * q;
                    const float qS  = fmSmoothstep (0.45f, 1.0f, q);
                    fmQuakeSubRatio_[o]   = 0.5f - 0.25f * qS;                             // 0.5 → 0.25
                    fmQuakeFry_[o]        = fmSmoothstep (0.60f, 1.0f, q);                 // fry only in the top third
                    const float quakeComp = juce::jmin (0.5f / fmQuakeSubRatio_[o], 2.0f); // 1/ratio index comp, capped
                    fmQuakeIdx_[o]        = 0.30f * q2 * quakeComp;                        // IDX0 = 0.30 turns base

                    // combined SCORCH+QUAKE bandwidth widening → dull the carrier mip under load (anti-alias)
                    fmFxMipAdd_[o] = 6.2832f * ( scDrive * 1.5f
                                               + (fmScorchIdxMul_[o] - 1.0f) * (fmD1Sm_[o] + fmD2Sm_[o])
                                               + scFbAdd * 1.5f
                                               + fmQuakeIdx_[o] * fmQuakeSubRatio_[o] );

                    // STORM — mutual modulator coupling (M1→M2 phase + M2→M1 leak, every algo).
                    const float st2 = fmStormSm_[o] * fmStormSm_[o];
                    fmStormM12_[o] = st2 * 0.65f;
                    fmStormM21_[o] = st2 * 0.35f;
                }
                // QUAKE subsonic fade — each osc's sub uses its own pitch; deep notes at 0.25×
                // must not dump near-DC energy, so fade the index below ~28 Hz.
                auto quakeSubsonic = [this] (size_t o, double inc0)
                {
                    const double subHz = inc0 * sampleRate_ * (double) fmQuakeSubRatio_[o];
                    if (subHz > 0.0 && subHz < 28.0) fmQuakeIdx_[o] *= (float) (subHz / 28.0);
                };
                quakeSubsonic (0, uPhaseIncA_[0]);
                quakeSubsonic (1, uPhaseIncB_[0]);
                quakeSubsonic (2, uPhaseIncC_[0]);
                quakeSubsonic (3, uPhaseIncD_[0]);
            }
            // FM-aware mip pick — phase modulation widens the carrier's instantaneous rate
            // (Carson-ish: 1 + 2π·D·ratio), so pick a duller mip under heavy FM or the
            // table's upper harmonics alias hard. Ring only shifts by ±f_m1 (linear term);
            // SCORCH (drive/feedback/index) + QUAKE (subharmonic) widen it further — added below.
            auto fmRateMul = [this] (Engine eng, size_t o) -> double
            {
                if (eng != Engine::FM) return 1.0;
                double m;
                if (fmAlgo_[o] == 2) m = 1.0 + (double) fmD1Sm_[o] * fmR1Eff_[o];
                else
                {
                    m = 1.0 + 6.2832 * (double) fmD1Eff_[o] * fmR1Eff_[o];
                    if (fmAlgo_[o] == 1) m += 6.2832 * (double) fmD2Eff_[o] * fmR2Eff_[o];
                }
                m += (double) fmFxMipAdd_[o];
                return juce::jmin (m, 64.0);   // sanity cap — extreme depth×ratio must dull, never vanish
            };
            // WARP FILTER coefficients — per block, per (osc, slot). f0 comes straight off
            // sine 0's phase increment, which is already computed here for the mip pick.
            {
                const int   md[4][2] = { { warpMode_,  warp2ModeA_ }, { warpModeB_, warp2ModeB_ },
                                         { warpModeC_, warp2ModeC_ }, { warpModeD_, warp2ModeD_ } };
                const float am[4][2] = { { warpAmount_,  warp2AmountA_ }, { warpAmountB_, warp2AmountB_ },
                                         { warpAmountC_, warp2AmountC_ }, { warpAmountD_, warp2AmountD_ } };
                const float vr[4][2] = { { warpVar_[0], warp2Var_[0] }, { warpVar_[1], warp2Var_[1] },
                                         { warpVar_[2], warp2Var_[2] }, { warpVar_[3], warp2Var_[3] } };
                const double inc[4]  = { uPhaseIncA_[0], uPhaseIncB_[0], uPhaseIncC_[0], uPhaseIncD_[0] };
                wfXMin_ = (float) (3.14159265358979323846 * 20.0 / sampleRate_);   // fb553
                for (int o = 0; o < 4; ++o)
                {
                    wfKx_[o] = (float) (3.14159265358979323846 * inc[o]);   // fb553 — π·f0/fs
                    for (int sl = 0; sl < 2; ++sl)
                    {
                        wfAmt_[o][sl] = am[o][sl];   // fb553 — the un-fanned block amount (see the filter's own note)
                        warpFiltCoef (wfCoef_[o][sl], md[o][sl], am[o][sl], vr[o][sl],
                                      inc[o] * sampleRate_, sampleRate_);
                    }
                }
            }
            currentMipLevelA_ = tw::Wavetable::mipLevelForPhaseIncrement (uPhaseIncA_[0] * warpRateMul (warpMode_,  warpFan (0, warpAmount_), (drawFor (0, 0) ? (double) drawFor (0, 0)->slope : 1.0))  * warpRateMul (warp2ModeA_, warpFan (0, warp2AmountA_), (drawFor (0, 1) ? (double) drawFor (0, 1)->slope : 1.0)) * fmRateMul (engine_,  0) * uniRateMul (uDetuneCentsA_, activeUnisonA_));
            currentMipLevelB_ = tw::Wavetable::mipLevelForPhaseIncrement (uPhaseIncB_[0] * warpRateMul (warpModeB_, warpFan (1, warpAmountB_), (drawFor (1, 0) ? (double) drawFor (1, 0)->slope : 1.0)) * warpRateMul (warp2ModeB_, warpFan (1, warp2AmountB_), (drawFor (1, 1) ? (double) drawFor (1, 1)->slope : 1.0)) * fmRateMul (engineB_, 1) * uniRateMul (uDetuneCentsB_, activeUnisonB_));
            currentMipLevelC_ = tw::Wavetable::mipLevelForPhaseIncrement (uPhaseIncC_[0] * warpRateMul (warpModeC_, warpFan (2, warpAmountC_), (drawFor (2, 0) ? (double) drawFor (2, 0)->slope : 1.0)) * warpRateMul (warp2ModeC_, warpFan (2, warp2AmountC_), (drawFor (2, 1) ? (double) drawFor (2, 1)->slope : 1.0)) * fmRateMul (engineC_, 2) * uniRateMul (uDetuneCentsC_, activeUnisonC_));
            currentMipLevelD_ = tw::Wavetable::mipLevelForPhaseIncrement (uPhaseIncD_[0] * warpRateMul (warpModeD_, warpFan (3, warpAmountD_), (drawFor (3, 0) ? (double) drawFor (3, 0)->slope : 1.0)) * warpRateMul (warp2ModeD_, warpFan (3, warp2AmountD_), (drawFor (3, 1) ? (double) drawFor (3, 1)->slope : 1.0)) * fmRateMul (engineD_, 3) * uniRateMul (uDetuneCentsD_, activeUnisonD_));

            // ── WT BLUR — smooth the amount, then (re)build each OSC's blended single-
            // cycle buffer ONCE per block (only when frame pos / blur / mip changed). Every
            // unison sine reads it via readCycle, so frame-blend cost is per-block, not
            // per-sample. The blend is the mip's frames summed at one band edge → alias-free;
            // RMS-matched inside renderBlend → no level change; blur 0 → exact old lookup.
            if (std::abs (blurTargetA_ - blurA_) < 1.0e-4f) blurA_ = blurTargetA_;
            else                                            blurA_ += (blurTargetA_ - blurA_) * 0.25f;
            if (std::abs (blurTargetB_ - blurB_) < 1.0e-4f) blurB_ = blurTargetB_;
            else                                            blurB_ += (blurTargetB_ - blurB_) * 0.25f;
            if (std::abs (blurTargetC_ - blurC_) < 1.0e-4f) blurC_ = blurTargetC_;
            else                                            blurC_ += (blurTargetC_ - blurC_) * 0.25f;
            if (std::abs (blurTargetD_ - blurD_) < 1.0e-4f) blurD_ = blurTargetD_;
            else                                            blurD_ += (blurTargetD_ - blurD_) * 0.25f;

            blendXfA_ = blendXfB_ = blendXfC_ = blendXfD_ = false;   // fb248 — armed only on a fresh blend rebuild below
            if (currentWavetable_ != nullptr)
            {
                float fpA = framePos_;
                if (interpModeA_ == 1) { const float Nf = 16.0f; fpA = std::round (fpA * (Nf - 1.0f)) / (Nf - 1.0f); }
                // Gate keys include the table's BUILD EPOCH: morph slots rebuild their two
                // Wavetable objects IN PLACE (same pointer, new content), so pointer identity
                // alone latched a mid-rebuild (zeroed) composite as permanent SILENCE.
                const int epA = currentWavetable_->buildEpoch();
                if (fpA != lastFpA_ || blurA_ != lastBlurA_ || currentMipLevelA_ != lastMipA_ || currentWavetable_ != lastWtA_ || epA != lastEpochA_)
                {
                    if (blendValidA_) { blendPrevA_ = blendA_; blendXfA_ = true; }   // fb248 — keep old, glide to new across the block
                    currentWavetable_->renderBlend (currentMipLevelA_, fpA, blurA_, blendA_.data());
                    blendValidA_ = true;
                    lastFpA_ = fpA; lastBlurA_ = blurA_; lastMipA_ = currentMipLevelA_; lastWtA_ = currentWavetable_; lastEpochA_ = epA;
                }
            }
            if (currentWavetableB_ != nullptr)
            {
                float fpB = framePosB_;
                if (interpModeB_ == 1) { const float Nf = 16.0f; fpB = std::round (fpB * (Nf - 1.0f)) / (Nf - 1.0f); }
                const int epB = currentWavetableB_->buildEpoch();
                if (fpB != lastFpB_ || blurB_ != lastBlurB_ || currentMipLevelB_ != lastMipB_ || currentWavetableB_ != lastWtB_ || epB != lastEpochB_)
                {
                    if (blendValidB_) { blendPrevB_ = blendB_; blendXfB_ = true; }   // fb248
                    currentWavetableB_->renderBlend (currentMipLevelB_, fpB, blurB_, blendB_.data());
                    blendValidB_ = true;
                    lastFpB_ = fpB; lastBlurB_ = blurB_; lastMipB_ = currentMipLevelB_; lastWtB_ = currentWavetableB_; lastEpochB_ = epB;
                }
            }
            if (currentWavetableC_ != nullptr)
            {
                float fpC = framePosC_;
                if (interpModeC_ == 1) { const float Nf = 16.0f; fpC = std::round (fpC * (Nf - 1.0f)) / (Nf - 1.0f); }
                const int epC = currentWavetableC_->buildEpoch();
                if (fpC != lastFpC_ || blurC_ != lastBlurC_ || currentMipLevelC_ != lastMipC_ || currentWavetableC_ != lastWtC_ || epC != lastEpochC_)
                {
                    if (blendValidC_) { blendPrevC_ = blendC_; blendXfC_ = true; }   // fb248
                    currentWavetableC_->renderBlend (currentMipLevelC_, fpC, blurC_, blendC_.data());
                    blendValidC_ = true;
                    lastFpC_ = fpC; lastBlurC_ = blurC_; lastMipC_ = currentMipLevelC_; lastWtC_ = currentWavetableC_; lastEpochC_ = epC;
                }
            }
            if (currentWavetableD_ != nullptr)
            {
                float fpD = framePosD_;
                if (interpModeD_ == 1) { const float Nf = 16.0f; fpD = std::round (fpD * (Nf - 1.0f)) / (Nf - 1.0f); }
                const int epD = currentWavetableD_->buildEpoch();
                if (fpD != lastFpD_ || blurD_ != lastBlurD_ || currentMipLevelD_ != lastMipD_ || currentWavetableD_ != lastWtD_ || epD != lastEpochD_)
                {
                    if (blendValidD_) { blendPrevD_ = blendD_; blendXfD_ = true; }   // fb248
                    currentWavetableD_->renderBlend (currentMipLevelD_, fpD, blurD_, blendD_.data());
                    blendValidD_ = true;
                    lastFpD_ = fpD; lastBlurD_ = blurD_; lastMipD_ = currentMipLevelD_; lastWtD_ = currentWavetableD_; lastEpochD_ = epD;
                }
            }

            // Phase 8a — HORIZON: per-note tilt depending on midiNote and amount.
            // midiNote 60 = neutral; lower notes get high-shelf cut (warmer),
            // higher notes get high-shelf boost (airier).
            // CPU: makeHighShelf heap-allocates a ref-counted temp — it used to run TWICE per
            // block per voice (even at amount 0). Recompute only when the tilt actually changes.
            {
                // Phase 8a polish — boost HORIZON range so it's audible at normal MIDI notes
                const float horizonTilt = horizonAmount_ * static_cast<float>(currentMidiNote_ - 60) / 24.0f;
                if (horizonTilt != lastHorizonTilt_)
                {
                    lastHorizonTilt_ = horizonTilt;
                    const float shelfGain = std::pow (2.0f, horizonTilt);  // ±12dB at extremes
                    *horizonShelfL_.coefficients = *juce::dsp::IIR::Coefficients<float>::makeHighShelf (
                        sampleRate_, 2500.0f, 0.7071f, shelfGain);
                    *horizonShelfR_.coefficients = *juce::dsp::IIR::Coefficients<float>::makeHighShelf (
                        sampleRate_, 2500.0f, 0.7071f, shelfGain);
                }
            }

            // Pre-pass env values for this loop: ch0 = AMP (the VCA); ch1..4 = the
            // four free envelopes (any routed to Amp add tremolo-style gain mod).
            const float* eAmpVca = envScratch_.getReadPointer (0);
            const float* eAmpFree[4] = { envScratch_.getReadPointer (1), envScratch_.getReadPointer (2),
                                         envScratch_.getReadPointer (3), envScratch_.getReadPointer (4) };

            // CPU: an osc whose gate has fully settled at silence — switched OFF (the white OSC
            // letters), muted, or un-soloed — SKIPS its whole render path (engines, unison
            // loop) instead of rendering into a ×0 gate. The 4 ms gate one-pole keeps on/off
            // click-free; skipping only begins once the fade has actually finished.
            for (int g = 0; g < 4; ++g)
                oscDead_[g] = robinGate (g) <= 0.0f && oscGate_[g] < 1.0e-4f;

            // ── BLEND MODES (all-engines): derive the two per-block flags from the warp matrix.
            //    Both stay false for any un-blended patch → the block renders + per-sample loop below
            //    behave exactly as before. modSrcForce_ opens a turned-down source engine's Level-0
            //    gate so it still modulates; blkCarrierArmed_ marks a block engine whose output the
            //    per-sample loop will phase-modulate (FM/PD on a sample). ──
            {
                const Engine eng[4] = { engine_, engineB_, engineC_, engineD_ };
                auto isBlock = [] (Engine e) noexcept {
                    return e == Engine::SAMP || e == Engine::GRAN || e == Engine::SPEC
                        || e == Engine::HARM || e == Engine::MODAL; };
                for (int o = 0; o < 4; ++o) { modSrcForce_[o] = false; blkCarrierArmed_[o] = false; }
                // fb551 — THE CARRIER'S OWN RATE, snapshotted once per block. Read by modes 9 and
                //  10 and by nothing else (mode 1 is Hz-absolute and must stay that way — fb523).
                //  Representative = unison sine 0, the same reference the mip pick uses: the unison
                //  voices share one fmPhase_ per carrier, so one rate has to stand for the set.
                blendCarrInc_[0] = uPhaseIncA_[0];  blendCarrInc_[1] = uPhaseIncB_[0];
                blendCarrInc_[2] = uPhaseIncC_[0];  blendCarrInc_[3] = uPhaseIncD_[0];
                noiseForce_ = false;   // fb64 — set when any osc blends WITH the noise (so its tap is generated even if noise output is off)
                anyBlendArmed_ = false;
                for (int c = 0; c < 4; ++c)
                    for (int s = 0; s < 4; ++s)
                    {
                        const BlendSlotV& b = blendSlot_[c][s];
                        // fb522 CPU — the arm flag has to survive the RELEASE of a slot as well as
                        // its engagement: the per-sample law reads the DE-ZIPPERED depth, so a slot
                        // whose knob just hit 0 must keep running until the smoothed value has
                        // decayed under the same threshold the inner loop uses, or turning a blend
                        // down would hard-cut instead of fading.
                        // 🚨 fb523 — 1e-4 -> 1e-6 IN ALL THREE PLACES, AND THEY MUST MOVE TOGETHER.
                        // This gate and the inner loop's `d < 1e-6` are the SAME threshold wearing
                        // two hats: if the arm pass keeps 1e-4 while the inner loop drops to 1e-6,
                        // every depth in between builds clean, shows in the UI, and is skipped
                        // before the DSP ever runs — fb373's "verify the path, not just the engine"
                        // failure exactly. The reason for the move is in the inner loop: under the
                        // Hz law a depth of 1e-4 is Δf = 9.6 Hz = an audible −17 dBc sideband at C1.
                        if (blendIsLive (b.mode)
                            && (b.depth >= 1.0e-6f || blendDepthSm_[c][s] >= 1.0e-6f))
                            anyBlendArmed_ = true;
                        if (! blendIsLive (b.mode) || b.depth < 1.0e-6f) continue;        // armed FM/PD/AM/RM/EXP/CLAMP only
                        if (blendIsPhase (b.mode) && isBlock (eng[c]))
                            blkCarrierArmed_[c] = true;                                   // FM/PD phase-modulate c's block (AM/RM don't need the ring)
                        if      (b.src < 4)  modSrcForce_[b.src] = true;                  // Osc A..D as source
                        else if (b.src == 5) noiseForce_         = true;                  // Noise as source (fb64)
                        else if (b.src == 6) modSrcForce_[c]     = true;                  // Self
                        // b.src == 4 (Sub) is handled earlier — it has to be known before
                        // prepareSubBlock() runs. See the subForce_ scan there.
                    }
                // fb552 — FOLLOWERS ARM THE SAME WAY A BLEND SLOT DOES, and they must, for the same
                //  reason: a follower on an oscillator you have turned down to 0 has to keep
                //  following it. modSrcForce_/noiseForce_ already mean exactly "render this source
                //  even though nothing is listening to it", so a routed follower just sets them.
                //  ⚠️ THIS MUST STAY ABOVE the uLoopA..D gate and the noise render gate — those read
                //  the flags. Below them it would build clean and follow silence.
                // fb553 — mode 6's worst-case warp swing, per osc, for the mip pick below. It
                //  TRACKS DEPTH: at depth 0 this is 0 and warpFan asks for exactly the base rate.
                for (int c = 0; c < 4; ++c)
                {
                    float mx = 0.0f;
                    for (int s = 0; s < 4; ++s)
                        if (blendSlot_[c][s].mode == 6)
                            // the SMOOTHED depth too: turning mode 6 down must fade, not hard-cut
                            // back onto the block-rate coefficients mid-decay (fb522's arm law).
                            mx += kWarpModDepth * juce::jmax (blendSlot_[c][s].depth, blendDepthSm_[c][s]);
                    blendWarpMax_[c]   = juce::jlimit (0.0f, 1.0f, mx);
                    blendWarpArmed_[c] = mx > 0.0f;
                }
                anyFollowArmed_ = false;
                for (int a = 0; a < modConfig_.numAssignments; ++a)
                {
                    const auto& as = modConfig_.assignments[a];
                    if (! as.enabled) continue;
                    const int fk = wc::followIndexOf ((int) as.source);
                    if (fk < 0) continue;
                    anyFollowArmed_ = true;
                    // ⚠️ fb556 — `else noiseForce_` was right when 4 was the last follower and is a
                    //  BUG the moment there are seven: a follower on Filter 1 would have forced the
                    //  NOISE to render. The filters need no forcing at all — a filter nothing is
                    //  routed into genuinely has no output to follow.
                    if      (fk < 4)  modSrcForce_[fk] = true;
                    else if (fk == 4) noiseForce_      = true;
                }
                // ── THE FREE CPU WIN (fb522). The per-sample blend law below is ~50 flops per
                // sample PER VOICE (16 de-zipper FMAs before any mode test, 4 unguarded fmPhase_
                // integrators, 8 soft bounds) and it ran unconditionally on every patch, including
                // the overwhelming majority that blend nothing — ≈89 Mflop/s of pure waste.
                // Skipping it is BIT-IDENTICAL, not approximately so, and here is why:
                //  · blendOff[] initialises to 0.f and blendAmp[] to 1.f each sample, which is
                //    exactly what the skipped code would have produced with every slot at mode 0.
                //  · the ONE piece of state that could survive a disarm is fmPhase_, whose leaky
                //    integrator decays but never reaches 0. So we do not disarm until it has: the
                //    flag stays true while any |fmPhase_| is above 1e-7, and we zero it on the
                //    transition. A 1e-7-cycle phase step is 8.7e-4 degrees — it cannot click, and
                //    it is bounded by construction rather than assumed.
                //  · the de-zipper is a state machine, so parking it could leave a STALE smoothed
                //    depth that pops the next time a slot arms (a slot at mode 0 with a high knob
                //    would freeze its smoothed value high, then arm at full depth). We converge it
                //    instead: while nothing is armed the smoothed value is written straight to its
                //    target, which is where the old code's exponential was heading anyway and is
                //    unobservable because nothing reads it. Same for the LFO taps.
                if (! anyBlendArmed_)
                    for (int c = 0; c < 4; ++c)
                        if (std::fabs (fmPhase_[c]) > 1.0e-7f) { anyBlendArmed_ = true; break; }
                if (! anyBlendArmed_)
                {
                    for (int c = 0; c < 4; ++c)
                    {
                        fmPhase_[c] = 0.0f;                       // |x| <= 1e-7 here, proven above
                        for (int s = 0; s < 4; ++s)
                        {
                            blendDepthSm_[c][s] = blendSlot_[c][s].depth;
                            const int src = blendSlot_[c][s].src;
                            if (src >= 7 && src <= 16) blendLfoSm_[c][s] = synthLfo_[src - 7].peek();
                        }
                    }
                }
            }

            // SAMPLE-ENGINE-VOICE — render any SAMP oscillators' stereo blocks for this
            // buffer (scan/loop/xfade/spray + STRETCH/FORMANT warp). Cheap no-op if none.
            renderSampleBlocks (numSamples);
            renderGranularBlocks (numSamples);   // GRANULAR-ENGINE-VOICE — render any GRAN oscillators' blocks
            renderGeodeBlocks (numSamples);      // GEODE-ENGINE-VOICE — render any SPEC oscillators' blocks
            renderHarmonicBlocks (numSamples);   // HARMONIC-ENGINE-VOICE — render any HARM oscillators' blocks
            renderModalBlocks (numSamples);      // MODAL-ENGINE-VOICE — render any MODAL oscillators' blocks

            // CPU: SAMP/GRAN/SPEC oscs render whole blocks above and their result REPLACES the
            // unison sum below — the per-sine u-loop only produces zeros for them (fold of 0,
            // two pan MACs, discarded). Skip it entirely; the sums already start at 0.
            // (|| modSrcForce_) — a WT/FM osc feeding an armed blend slot renders even when a FLOW
            // mode / round-robin gated it dead, so it can still modulate. Its audible output is still
            // zeroed by the per-osc gate (gA..gD) in the mix; only modPrev_ sees it. Bit-identical
            // when nothing is blended (modSrcForce_ all false).
            const bool uLoopA = (! oscDead_[0] || modSrcForce_[0]) && (engine_  != Engine::SAMP && engine_  != Engine::GRAN && engine_  != Engine::SPEC && engine_  != Engine::HARM && engine_  != Engine::MODAL);
            const bool uLoopB = (! oscDead_[1] || modSrcForce_[1]) && (engineB_ != Engine::SAMP && engineB_ != Engine::GRAN && engineB_ != Engine::SPEC && engineB_ != Engine::HARM && engineB_ != Engine::MODAL);
            const bool uLoopC = (! oscDead_[2] || modSrcForce_[2]) && (engineC_ != Engine::SAMP && engineC_ != Engine::GRAN && engineC_ != Engine::SPEC && engineC_ != Engine::HARM && engineC_ != Engine::MODAL);
            const bool uLoopD = (! oscDead_[3] || modSrcForce_[3]) && (engineD_ != Engine::SAMP && engineD_ != Engine::GRAN && engineD_ != Engine::SPEC && engineD_ != Engine::HARM && engineD_ != Engine::MODAL);
            // fb523 — does this osc's signal reach modPrev_ THROUGH the unison pan tables? Only WT
            //  and FM render inside the unison loop; every block engine bypasses cos/sin entirely.
            const bool mcOnA = (engine_  == Engine::WT || engine_  == Engine::FM);
            const bool mcOnB = (engineB_ == Engine::WT || engineB_ == Engine::FM);
            const bool mcOnC = (engineC_ == Engine::WT || engineC_ == Engine::FM);
            const bool mcOnD = (engineD_ == Engine::WT || engineD_ == Engine::FM);
            const float invNsBlend = 1.0f / (float) juce::jmax (1, numSamples);   // fb248 — frame-crossfade ramp denom
            // ── PHASE OFFSET — CONTINUOUS, not merely a note-on seed (fb544) ──────────────────
            //  MEASURED against Serum 2: move its `A Phase` while a note is HELD and the held note
            //  CHANGES (+4.7 dB rel). Ours was bit-identical (-79.8 dB) because resolvePhase runs
            //  at note-on and NOWHERE ELSE — so the knob was silent under your fingers, which is
            //  exactly what "it doesn't move and make sound" means.
            //  Serum's model is ONE mechanism, not two: the knob is a CONTINUOUS read offset and
            //  note-on seeds only the random part on top of it. Implemented here as a per-sample
            //  nudge to the phase INCREMENT, so every read site inherits it for free and not one
            //  read site has to be touched (fb523's seam law — the fewer call sites, the fewer
            //  places to forget). Adding d/numSamples for one block slides the accumulator by
            //  exactly d and leaves it there.
            //  ⚠️ Spreading d ACROSS the block is not a smoothing nicety — it is the whole effect.
            //  A phase slide is a momentary pitch bend, and that bend is the sound of the knob
            //  moving. Jumping instead would click and then be inaudible (steady-state absolute
            //  phase of one oscillator cannot be heard).
            //  FLOOR (fb462): a static knob gives d = 0.0 exactly, and `inc + 0.0` is bit-exact in
            //  IEEE-754, so an untouched patch renders bit-identically to the pre-fb544 build.
            for (int o = 0; o < 4; ++o)
            {
                double d = (double) phaseOff_[(size_t) o] - phaseOffApplied_[(size_t) o];
                d -= std::floor (d + 0.5);                                  // shortest way round the circle
                phaseOffStep_[(size_t) o]    = d / (double) juce::jmax (1, numSamples);
                phaseOffApplied_[(size_t) o] = (double) phaseOff_[(size_t) o];
            }
            for (int i = 0; i < numSamples; ++i)
            {
                const float blendFrac = (float) (i + 1) * invNsBlend;   // fb248 — 0→1 across the block: prev blend → new blend (seamless frame move)
                // fb204 — WARP/WARP2 glide (2.5ms) + FOLD ramp + UNISON table glide: every
                // block-pushed shape amount steps at block rate when modulated; the applied
                // values move per sample instead (the fb180 law, applied to the osc lane).
                warpAmount_   += (warpAmtT_   - warpAmount_)   * lvlSmCoef_;
                warpAmountB_  += (warpAmtTB_  - warpAmountB_)  * lvlSmCoef_;
                warpAmountC_  += (warpAmtTC_  - warpAmountC_)  * lvlSmCoef_;
                warpAmountD_  += (warpAmtTD_  - warpAmountD_)  * lvlSmCoef_;
                warp2AmountA_ += (warp2AmtTA_ - warp2AmountA_) * lvlSmCoef_;
                warp2AmountB_ += (warp2AmtTB_ - warp2AmountB_) * lvlSmCoef_;
                warp2AmountC_ += (warp2AmtTC_ - warp2AmountC_) * lvlSmCoef_;
                warp2AmountD_ += (warp2AmtTD_ - warp2AmountD_) * lvlSmCoef_;
                foldAmountA_ += foldStepA_; foldAmountB_ += foldStepB_;
                foldAmountC_ += foldStepC_; foldAmountD_ += foldStepD_;
                for (int gu = 0; gu < activeUnisonA_; ++gu) { uPanLA_[(size_t) gu] += (uPanLTA_[(size_t) gu] - uPanLA_[(size_t) gu]) * lvlSmCoef_; uPanRA_[(size_t) gu] += (uPanRTA_[(size_t) gu] - uPanRA_[(size_t) gu]) * lvlSmCoef_; }
                for (int gu = 0; gu < activeUnisonB_; ++gu) { uPanLB_[(size_t) gu] += (uPanLTB_[(size_t) gu] - uPanLB_[(size_t) gu]) * lvlSmCoef_; uPanRB_[(size_t) gu] += (uPanRTB_[(size_t) gu] - uPanRB_[(size_t) gu]) * lvlSmCoef_; }
                for (int gu = 0; gu < activeUnisonC_; ++gu) { uPanLC_[(size_t) gu] += (uPanLTC_[(size_t) gu] - uPanLC_[(size_t) gu]) * lvlSmCoef_; uPanRC_[(size_t) gu] += (uPanRTC_[(size_t) gu] - uPanRC_[(size_t) gu]) * lvlSmCoef_; }
                for (int gu = 0; gu < activeUnisonD_; ++gu) { uPanLD_[(size_t) gu] += (uPanLTD_[(size_t) gu] - uPanLD_[(size_t) gu]) * lvlSmCoef_; uPanRD_[(size_t) gu] += (uPanRTD_[(size_t) gu] - uPanRD_[(size_t) gu]) * lvlSmCoef_; }
                uNormA_ += (uNormTA_ - uNormA_) * lvlSmCoef_; uNormB_ += (uNormTB_ - uNormB_) * lvlSmCoef_;
                uNormC_ += (uNormTC_ - uNormC_) * lvlSmCoef_; uNormD_ += (uNormTD_ - uNormD_) * lvlSmCoef_;
                for (int mo = 0; mo < 4; ++mo) monoTapCorr_[mo] += (monoTapCorrT_[mo] - monoTapCorr_[mo]) * lvlSmCoef_;   // fb523 — the modulator-tap pan correction rides the SAME 2.5 ms glide as the pan tables
                // Per-osc SUB contributions this sample (mono, post-normalization) — filled by
                // subMix, used by the filter router to route the Sub independently of its osc.
                float subMono0 = 0.f, subMono1 = 0.f, subMono2 = 0.f, subMono3 = 0.f;
                // ── OSC A — sum across activeUnisonA_ sines (per-OSC unison) ─────
// ── BLEND MODES: per-carrier read-phase offset from the 4 cross-osc warp slots. PD = modulator
                //    injected direct (phase modulation); FM = modulator integrated per carrier (leaky) →
                //    true linear/thru-zero frequency modulation. Carrier accumulators stay clean — everything
                //    rides the read phase. Modulator taps = previous-sample pre-gain osc outputs (any-to-any). ──
                float blendOff[4] = { 0.f, 0.f, 0.f, 0.f };
                float blendAmp[4] = { 1.f, 1.f, 1.f, 1.f };   // AM/RM amplitude gain (1 = inert; multiplies the carrier)
                float blendWarp[4] = { 0.f, 0.f, 0.f, 0.f };   // fb553 — mode 6: audio-rate offset added to BOTH warp amounts (0 = inert)
                if (anyBlendArmed_)   // fb522 CPU — see the arm pass above for the bit-identity proof
                {
                    // fb523 — repInc[] (the per-carrier phase increment) is GONE: it was the ONLY
                    //  consumer of the old pitch-proportional FM law and nothing else in this block
                    //  reads it. Its removal is the load-bearing half of the unit change.
                    for (int c = 0; c < 4; ++c)
                    {
                        float pm = 0.f, fmDrive = 0.f, amp = 1.0f, expOct = 0.f, wrp = 0.f;   // fb551 — expOct: mode 9's octaves, summed across slots · fb553 — wrp: mode 6's warp offset
                        bool  clampZero = false;                                   // fb551 — mode 10 armed on this carrier
                        for (int s = 0; s < 4; ++s)
                        {
                            BlendSlotV& b = blendSlot_[c][s];
                            blendDepthSm_[c][s] += (b.depth - blendDepthSm_[c][s]) * 0.0025f;   // de-zipper
                            const float d = blendDepthSm_[c][s];
                            if (b.mode == 0 || d < 1.0e-6f) continue;   // Off / silent — fb523: 1e-4 -> 1e-6. Under the Hz law d = 1e-4 is Δf = 9.6 Hz = β 0.29 at C1 (a −17 dBc sideband), i.e. the old gate became an AUDIBLE dead zone in the bottom 0.6 % of the FM knob. At 1e-6 it is Δf = 0.096 Hz = β 0.0029 = −51 dBc, and the dead travel is 0.006 % of the knob.
                            float mod;
                            if      (b.src < 4)  mod = modPrev_[b.src];   // Osc A..D (any-to-any)
                            else if (b.src == 5) mod = noiseModTap_;      // Noise (fb64) — FM/PD/AM/RM an osc WITH the noise
                            else if (b.src == 6) mod = modPrev_[c];       // Self (feedback)
                            else if (b.src >= 7 && b.src <= 16)           // fb224/fb225 — WARP x LFO: the LIVE LFO value (the one the pane's dot rides) sweeps the warp. peek() steps once per BLOCK, so a per-sample 2.5ms glide (lvlSmCoef_, the same coefficient the warp knobs ride) melts the staircase — motion kept, static gone. Osc/noise taps stay raw (audio-rate must never be low-passed).
                            {
                                blendLfoSm_[c][s] += (synthLfo_[b.src - 7].peek() - blendLfoSm_[c][s]) * lvlSmCoef_;
                                mod = blendLfoSm_[c][s];
                            }
                            else if (b.src == 4) mod = subModTap_[c];      // fb522 — SUB: the CARRIER'S OWN sub, per-carrier exactly like Self (src 6). subMix runs AFTER blendOff[] is built, so this tap is one sample delayed, matching modPrev_ and noiseModTap_.
                            else                 mod = 0.f;               // (no src is unhandled any more)
                            // ── THE CEILINGS (fb522 OVERPASS §1.1). Every number here is measured;
                            //    the ones that are NOT raised are the interesting ones — see below.
                            if      (b.mode == 2) pm      += (kPdCycles * d) * mod;   // PD (phase offset, in CYCLES). ⛔ THE CONSTANT IS kPdCycles = 8.50 — READ ITS DEFINITION, not this line. fb524 raised it 2.20 -> 8.50 (+5.9 % past the reference's measured beta 50.43) and moved PD onto the 361:1 taper; the old note here described the 2.20 wave and its 2 % out-of-harmonic stopping rule, which no longer applies.
                            else if (b.mode == 1) fmDrive += (kFmDeviationHz * d) * mod;    // FM — fmDrive is now PEAK DEVIATION IN HZ, not a dimensionless index. Was (48.0f * d): deviation = 48*d*f_carrier, so the INDEX was pitch-independent and the DEVIATION scaled with pitch. Inverted here: 4 slots sum their deviations in Hz.
                            else if (b.mode == 3) amp     *= 1.0f + (kAmIndex * d) * mod;   // AM. ⛔ THE CONSTANT IS kAmIndex = 10.0 — READ ITS DEFINITION. The LAW is unchanged and still right: y = x*(1 + k*mod) holds the carrier at -0.000 dBc by construction, because x*mod has no component at f0. fb524 raised k 2.0 -> 10.0 (2.0 was exact parity, peak x2.999 vs the reference's x3.007) and moved AM onto the 361:1 taper so the low half stays gentle.
                            else if (b.mode == 4)
                            {
                                // RM — fb524 MODULATOR DRIVE. ⚠️ READ THE ALGEBRA BEFORE TOUCHING
                                // kRmLevel: at d = 1 this collapses to amp = K·mod, so K is PURE
                                // OUTPUT GAIN and raising it changes loudness and NOTHING ELSE.
                                // K therefore STAYS at 2.0 (level-matched to the reference at
                                // -0.03 dB on peak). The extra reach comes from the MODULATOR:
                                // it is driven through a saturator whose own peak is divided back
                                // out, so the ring product keeps its level and gains the
                                // modulator's odd harmonics. Our BRIGHT 1/m ring character is
                                // preserved and added to — the darker 1/m² law is NOT adopted
                                // here, because Max has not chosen between them.
                                const float gd = kRmModDrive * d;
                                float modDrv = mod;
                                if (gd > 1.0e-6f)
                                    modDrv = tw::shapers::wsTanh (gd * mod) / tw::shapers::wsTanh (gd);
                                amp *= (1.0f - d) + (kRmLevel * d) * modDrv;
                            }
                            // fb551 — FM EXP. Octaves, not Hz: they SUM here and become a single
                            //  multiplier below. ⛔ THE CONSTANT IS kFmExpOctaves = 10 — read its
                            //  definition for why this mode alone is allowed to be pitch-proportional.
                            else if (b.mode == 9)  expOct   += (kFmExpOctaves * d) * mod;
                            // fb551 — FM CLAMP. Identical drive to mode 1 (same Hz ceiling, same
                            //  taper); the ONLY difference is the flag, which clamps the total rate
                            //  at zero below so the carrier stalls instead of reversing.
                            else if (b.mode == 10) { fmDrive += (kFmDeviationHz * d) * mod; clampZero = true; }
                            // fb553 — AUDIO-RATE WARP. No integrator, no bound: the warp amount is
                            //  a plain 0..1 knob and the use site already clamps it there.
                            else if (b.mode == 6)  wrp     += (kWarpModDepth * d) * mod;
                            // fb523's own note on this line, kept because its LEVEL argument is
                            // still the reason kRmLevel is 2.0 and not something else:
                            // RM — fb523: 1.8 -> 2.0, LEVEL ONLY. The ledger is right that at d = 1 this collapses to amp = K*mod and K is pure output gain, so the SPECTRUM is untouched — [M] our quadrature ring product (delta = 90.00 deg, resid 0.37 dB over 40 evens) is deliberately kept; Max has not chosen between our bright 1/m and the reference dark 1/m^2 and this line must not decide it. What K fixes is the 4.4 dB level gap: measured peak factor was 1.8*0.7071 = 1.272 (measured 1.272) against the reference 2.006. Tap fix + K = 2.0 gives 2.000 = -0.03 dB of the reference on peak and +4.77 dB vs its measured +4.79 dB on RMS.
                        }
                        // fb523 — THE HZ LAW. WAS: `+ repInc[c] * fmDrive`, where repInc = f_carrier/fs,
                        //  so the added instantaneous frequency was f_carrier·fmDrive Hz — deviation
                        //  PROPORTIONAL TO CARRIER PITCH, which is what made the index pitch-independent
                        //  ([M] β identical at C1 and C3 to 0.4 %). NOW: fmDrive is already Hz, so
                        //  ×(1/fs) is cycles per sample and the deviation is a constant number of Hz at
                        //  every pitch — the index grows as pitch falls, 4× per two octaves down.
                        //  ⚠️ repInc is deliberately GONE from this line. Re-introducing it anywhere
                        //  (including a "mip selection" helper) re-creates the pitch-proportional law.
                        // fb551 — EXPONENTIAL, folded in as Hz so one integrator serves every FM
                        //  mode. Δf = f_c·(2^oct − 1), so oct = 0 contributes EXACTLY 0 (2^0 − 1)
                        //  and costs nothing: modes 1/2/3/4 remain bit-identical, gated not
                        //  approximated. The softBound is on the OCTAVES, not the Hz — see
                        //  kFmExpOctMax for the ±4-tap case it exists for.
                        if (expOct != 0.0f)
                            fmDrive += (float) (blendCarrInc_[c] * sampleRate_)
                                     * (fastExp2 (softBound (expOct, kFmExpOctLin, kFmExpOctMax)) - 1.0f);
                        //  FM CLAMP — the total instantaneous rate is (carrier + added); clamping it
                        //  at zero means the added rate can never be more negative than the carrier's
                        //  own. ⚠️ THE ELSE BRANCH IS THE ORIGINAL STATEMENT, CHARACTER FOR CHARACTER,
                        //  and it is written this way on purpose: hoisting `invSampleRate_ * fmDrive`
                        //  into a named local lets the compiler contract a DIFFERENT pair of operands
                        //  into an FMA, which changes the last bit of every FM/PD/AM/RM patch that
                        //  ever shipped. Bit-identity here is gated, not assumed — but it should not
                        //  have to rest on a gate when the alternative is one `else`.
                        if (clampZero)
                        {
                            float dCyc = invSampleRate_ * fmDrive;
                            const float floorCyc = -(float) blendCarrInc_[c];     // total rate = carrier + dCyc ≥ 0
                            if (dCyc < floorCyc) dCyc = floorCyc;                 // the carrier stalls; it never reverses
                            fmPhase_[c] = kFmLeak * fmPhase_[c] + dCyc;
                        }
                        else
                        fmPhase_[c] = kFmLeak * fmPhase_[c] + invSampleRate_ * fmDrive;   // integrate Hz → cycles
                        // fb523 — ±16/±32 -> ±512/±1024 CYCLES. Under the old index law the
                        //  excursion was pitch-independent and topped out at 5.4 cycles, so the
                        //  shipped knee NEVER ENGAGED (that is why [M] measured β 33.95 against a
                        //  50.27 "wall" and matched 48/√2 to 0.014 % instead). Under the Hz law the
                        //  excursion is Δf/(2π·f_mod) cycles, which explodes as f_mod falls. See
                        //  kFmBoundLin/kFmBoundMax for the derivation: LINEAR (bit-identical) for
                        //  every modulator at or above 29.842 Hz at full depth, i.e. the whole
                        //  keyboard from B0 up; a tanh knee only below that, where the leak alone
                        //  would allow 6,667 cycles of DC excursion.
                        fmPhase_[c] = softBound (fmPhase_[c], kFmBoundLin, kFmBoundMax);
                        blendOff[c] = pm + fmPhase_[c];
                        // fb522 — ±6 -> ±16 with a soft knee; the knee is linear to ±8, which keeps
                        // the INERT gain of exactly 1.0 exactly 1.0 (a global tanh would have made it
                        // 0.9987 and quietly re-levelled every blended patch by −0.011 dB).
                        // fb523 — CONSTANTS UNCHANGED, and now they are genuinely a safety net rather
                        // than part of the sound: with AM at k = 2.0 and RM at K = 2.0 against a
                        // modulator tap whose musical peak is 1.0, |amp| ≤ 3 (AM) / 2 (RM) — a
                        // quarter of the linear region. The knee is only reachable by the pathological
                        // case of a tap sitting on its own ±4 clamp (self-feedback, a hot block
                        // engine), which is exactly what it is for.
                        blendAmp[c] = softBound (amp, 8.0f, 16.0f);
                        blendWarp[c] = wrp;   // fb553 — clamped at the use site, together with the knob and the unison fan
                    }
                }

                float sumAL = 0.0f, sumAR = 0.0f;

                for (int u = 0; uLoopA && u < activeUnisonA_; ++u)
                {
                    float sAu = 0.0f;

                    switch (engine_)
                    {
                        case Engine::WT:
                        {
                            if (currentWavetable_ != nullptr)
                            {
                                double warpedPhase = uPhaseA_[(size_t) u];
                                // fb522 — UWARP: the per-sine WARP FAN. `uniWarpOnA_` is the
                                // bit-identity gate — at UWARP = 0 (the default, and every patch in
                                // the library) this is a predicted branch straight onto the untouched
                                // block value and costs nothing. Both slots share the one knob (Serum
                                // has two; the fan direction is the sine's own u_norm either way).
                                const float wAmt1A = (uniWarpOnA_ || blendWarpArmed_[0]) ? juce::jlimit (0.0f, 1.0f, warpAmount_ + (uniWarpOnA_ ? uWarpOffA_[(size_t) u] : 0.0f) + blendWarp[0]) : warpAmount_;
                                const float wAmt2A = (uniWarpOnA_ || blendWarpArmed_[0]) ? juce::jlimit (0.0f, 1.0f, warp2AmountA_ + (uniWarpOnA_ ? uWarpOffA_[(size_t) u] : 0.0f) + blendWarp[0]) : warp2AmountA_;
                                float  window      = 1.0f;   // PWM, FORMANT use this post-lookup window
                                bool   skipLookup  = false;  // PWM silence half-cycle

                                // WARP slot 1 — phase-domain remap (exact original math, factored to
                                // applyPhaseWarp so a second slot can chain on its output).
                                warpedPhase = applyPhaseWarp (warpMode_, wAmt1A, warpedPhase, window, skipLookup, warpVar_[0], drawFor (0, 0));
                                // WARP 2 — second slot, in SERIES on slot 1's output (Serum parity).
                                if (! skipLookup && warp2ModeA_ != 0)
                                    warpedPhase = applyPhaseWarp (warp2ModeA_, wAmt2A, warpedPhase, window, skipLookup, warp2Var_[0], drawFor (0, 1));

                                if (skipLookup)
                                {
                                    sAu = 0.0f;
                                }
                                else
                                {
                                    // WT BLUR — read the per-block blended single-cycle buffer
                                    // (frame position, stepped-interp and blur already applied at block rate).
                                    double rpA = warpedPhase + (double) blendOff[0]; rpA -= std::floor (rpA); sAu = wtBlendRead (blendA_.data(), blendPrevA_.data(), blendXfA_, blendFrac, (float) rpA);   // BLEND inject · fb248 crossfade
                                    sAu *= window;

                                    sAu = applyAmpWarp (warpMode_, wAmt1A, sAu, warpVar_[0], drawFor (0, 0));   // slot 1 amp-domain (RECTIFY / SINE SHAPER)
                                    sAu = applyAmpWarp (warp2ModeA_, wAmt2A, sAu, warp2Var_[0], drawFor (0, 1));   // WARP 2 amp-domain, chained
                                }
                            }
                            else
                            {
                                sAu = static_cast<float> (2.0 * uPhaseA_[(size_t) u] - 1.0);
                                sAu -= static_cast<float> (polyBlep (uPhaseA_[(size_t) u], uPhaseIncA_[(size_t) u]));
                            }
                            uPhaseA_[(size_t) u] += uPhaseIncA_[(size_t) u] + phaseOffStep_[0];   // fb544 — continuous PHASE
                            if (uPhaseA_[(size_t) u] >= 1.0) uPhaseA_[(size_t) u] -= 1.0;
                            else if (uPhaseA_[(size_t) u] < 0.0) uPhaseA_[(size_t) u] += 1.0;   // fb544 — the step can be NEGATIVE
                            break;
                        }


                        case Engine::FM:
                        {
                            // FM-ENGINE-VOICE — WAVETABLE-CARRIER FM + WEATHERING: this osc's
                            // blended wavetable cycle IS the carrier (frame morph / blur /
                            // spectral all still live); M1/M2 are sine modulators (turns).
                            //   0 STACK — M2 → M1 → carrier phase (3-op serial)
                            //   1 SPLIT — M1 and M2 both → carrier phase (parallel)
                            //   2 RING  — M2 → M1; M1 ring-modulates the carrier OUTPUT
                            // All depths/ratios come block-conditioned (fm*Eff_): Strike/Age/
                            // key-scale already folded in. STORM cross-couples the modulators
                            // (one-sample memory), RUST detunes M1 by absolute Hz, SCORCH drives
                            // the modulators (in-loop distortion), QUAKE adds a subharmonic operator.
                            const double pi2 = 6.2831853071795865;
                            const double inc = uPhaseIncA_[(size_t) u];
                            if (u == 0) {   // AGE de-zipper — glide the FM index per-sample (kills block-step crackle)
                                fmD1Now_[0] += (fmD1Eff_[0] - fmD1Now_[0]) * fmIdxGlideCoef_;
                                fmD2Now_[0] += (fmD2Eff_[0] - fmD2Now_[0]) * fmIdxGlideCoef_;
                                fmFbNow_[0]           += (fmFbEff_[0]        - fmFbNow_[0])           * fmIdxGlideCoef_;   // fb204 — FB/STORM/QUAKE/SCORCH ride the same de-zipper
                                fmStormM12Now_[0]     += (fmStormM12_[0]     - fmStormM12Now_[0])     * fmIdxGlideCoef_;
                                fmStormM21Now_[0]     += (fmStormM21_[0]     - fmStormM21Now_[0])     * fmIdxGlideCoef_;
                                fmQuakeIdxNow_[0]     += (fmQuakeIdx_[0]     - fmQuakeIdxNow_[0])     * fmIdxGlideCoef_;
                                fmQuakeFryNow_[0]     += (fmQuakeFry_[0]     - fmQuakeFryNow_[0])     * fmIdxGlideCoef_;
                                fmScorchIdxMulNow_[0] += (fmScorchIdxMul_[0] - fmScorchIdxMulNow_[0]) * fmIdxGlideCoef_;
                                fmScorchPreNow_[0]    += (fmScorchPre_[0]    - fmScorchPreNow_[0])    * fmIdxGlideCoef_;
                                fmScorchBiasNow_[0]   += (fmScorchBias_[0]   - fmScorchBiasNow_[0])   * fmIdxGlideCoef_;
                                fmScorchTanhBiasNow_[0] += (fmScorchTanhBias_[0] - fmScorchTanhBiasNow_[0]) * fmIdxGlideCoef_;
                                fmScorchMakeupNow_[0] += (fmScorchMakeup_[0] - fmScorchMakeupNow_[0]) * fmIdxGlideCoef_;
                            }
                            const float  d1  = fmD1Now_[0] * fmScorchIdxMulNow_[0];   // SCORCH index push (glided base)
                            const float  d2  = fmD2Now_[0] * fmScorchIdxMulNow_[0];
                            const float  fbk = fmFbNow_[0];                        // (SCORCH grit already folded in)
                            const int    alg = fmAlgo_[0];
                            float m2 = static_cast<float> (std::sin (pi2 * (uMod2PhaseA_[(size_t) u]
                                                        + (double) (fmStormM12Now_[0] * fmPrevM1A_[(size_t) u]))));
                            // SCORCH — asymmetric drive on M2 (adds harmonics → richer sidebands)
                            if (fmScorchPreNow_[0] > 1.0f) m2 = (fmFastTanh (fmScorchPreNow_[0] * m2 + fmScorchBiasNow_[0]) - fmScorchTanhBiasNow_[0]) * fmScorchMakeupNow_[0];
                            double m1Arg = uModPhaseA_[(size_t) u] + (double) (fbk * fmFbA_[(size_t) u])
                                         + (double) (fmStormM21Now_[0] * m2);
                            if (alg != 1) m1Arg += (double) (d2 * m2);       // STACK + RING: M2 → M1
                            float m1 = static_cast<float> (std::sin (pi2 * m1Arg));
                            // SCORCH — same drive on M1 (the operator that hits the carrier)
                            if (fmScorchPreNow_[0] > 1.0f) m1 = (fmFastTanh (fmScorchPreNow_[0] * m1 + fmScorchBiasNow_[0]) - fmScorchTanhBiasNow_[0]) * fmScorchMakeupNow_[0];
                            fmFbA_[(size_t) u] = 0.5f * (fmFbA_[(size_t) u] + m1);
                            fmPrevM1A_[(size_t) u] = m1;
                            // QUAKE — phase-locked subharmonic operator folded into the carrier phase
                            double qSubA = 0.0;
                            if (fmQuakeIdxNow_[0] > 1.0e-5f)
                            {
                                fmQuakePhaseA_[(size_t) u] += inc * (double) fmQuakeSubRatio_[0];
                                fmQuakePhaseA_[(size_t) u] -= std::floor (fmQuakePhaseA_[(size_t) u]);
                                float sub = static_cast<float> (std::sin (pi2 * fmQuakePhaseA_[(size_t) u]));
                                if (fmQuakeFryNow_[0] > 0.0f) sub += fmQuakeFryNow_[0] * (sub - sub * sub * sub * (1.0f / 6.0f));
                                qSubA = (double) (fmQuakeIdxNow_[0] * sub);
                            }
                            double cPh = uPhaseA_[(size_t) u] + qSubA + (double) blendOff[0];   // BLEND inject
                            if (alg != 2) cPh += (double) (d1 * m1);
                            if (alg == 1) cPh += (double) (d2 * m2);
                            cPh -= std::floor (cPh);
                            // WARP 2 on the FM carrier (2026-07-09): the back-panel pill works on
                            // FM now — phase warp remaps the carrier AFTER the modulators (classic
                            // warped-FM: Sync/PWM/Formant on the operator output), amp modes shape it.
                            float fmWin = 1.0f; bool fmSkip = false;
                            // fb522 — the WARP FAN reaches the FM carrier's warp slot too (see the WT branch).
                            const float wAmt2A = (uniWarpOnA_ || blendWarpArmed_[0]) ? juce::jlimit (0.0f, 1.0f, warp2AmountA_ + (uniWarpOnA_ ? uWarpOffA_[(size_t) u] : 0.0f) + blendWarp[0]) : warp2AmountA_;
                            if (warp2ModeA_ != 0)
                                cPh = applyPhaseWarp (warp2ModeA_, wAmt2A, cPh, fmWin, fmSkip, warp2Var_[0], drawFor (0, 1));
                            if (fmSkip) sAu = 0.0f;
                            else
                            {
                                sAu = (currentWavetable_ != nullptr)
                                        ? wtBlendRead (blendA_.data(), blendPrevA_.data(), blendXfA_, blendFrac, (float) cPh)
                                        : static_cast<float> (std::sin (pi2 * cPh));   // no table → pure-sine DX
                                sAu *= fmWin;
                                sAu = applyAmpWarp (warp2ModeA_, wAmt2A, sAu, warp2Var_[0], drawFor (0, 1));
                            }
                            if (alg == 2)
                                sAu *= (1.0f - fmD1Sm_[0]) + fmD1Sm_[0] * m1;      // ring dry→wet on depth 1
                            uModPhaseA_[(size_t) u]  += inc * fmR1Eff_[0] + fmRustTps_[0];
                            uModPhaseA_[(size_t) u]  -= std::floor (uModPhaseA_[(size_t) u]);
                            uMod2PhaseA_[(size_t) u] += inc * fmR2Eff_[0];
                            uMod2PhaseA_[(size_t) u] -= std::floor (uMod2PhaseA_[(size_t) u]);
                            uPhaseA_[(size_t) u] += inc + phaseOffStep_[0];   // fb544 — continuous PHASE
                            if (uPhaseA_[(size_t) u] >= 1.0) uPhaseA_[(size_t) u] -= 1.0;
                            else if (uPhaseA_[(size_t) u] < 0.0) uPhaseA_[(size_t) u] += 1.0;   // fb544 — the step can be NEGATIVE
                            break;
                        }

                        case Engine::SAMP:
                        case Engine::GRAN:
                        case Engine::SPEC:
                        case Engine::HARM:
                        case Engine::MODAL:
                            sAu = 0.0f; break;
                    }

                    // Phase 11d — FOLD applied per-sine, post-engine, pre-pan.
                    sAu = applyFoldADAA (sAu, foldShapeA_, foldAmountA_, foldStateA_[(size_t) u]);

                    // Per-sine pan into the OSC A stereo sum.
                    sumAL += sAu * uPanLA_[(size_t) u];
                    sumAR += sAu * uPanRA_[(size_t) u];
                }

                // Auto-gain (RMS-constant): holds loudness as voices / blend change.
                sumAL *= uNormA_;
                sumAR *= uNormA_;
                float sA_L = sumAL;
                float sA_R = sumAR;
                // WARP FILTER (modes 35/36) — on the SUMMED osc. Linear operator, so this is
                // identical to filtering every sine, at 1/16 the cost. Both slots chain.
                for (int sl = 0; sl < 2; ++sl)
                    if (wfCoef_[0][sl].on) {
                        // fb553 — AUDIO-RATE CUTOFF. Armed only by a mode-6 blend slot, so the
                        //  unmodulated path below is the original two lines, untouched.
                        if (blendWarpArmed_[0])
                        {
                            WarpFiltCoef cf = wfCoef_[0][sl];
                            warpFiltFast (cf, wfKx_[0], wfXMin_,
                                          juce::jlimit (0.0f, 1.0f, wfAmt_[0][sl] + blendWarp[0]));
                            sA_L = warpFiltTick (cf, wfState_[0][sl][0], sA_L);
                            sA_R = warpFiltTick (cf, wfState_[0][sl][1], sA_R);
                        }
                        else {
                        sA_L = warpFiltTick (wfCoef_[0][sl], wfState_[0][sl][0], sA_L);
                        sA_R = warpFiltTick (wfCoef_[0][sl], wfState_[0][sl][1], sA_R); }
                    }
                // RECTIFY DC block — only when this osc's wavetable warp == Rectify (slot 1 or 2)
                // with nonzero amount; dormant (bit-identical) otherwise.
                if ((engine_ == Engine::WT && (warpAmpNeedsDc (warpMode_, warpAmount_, warpVar_[0])
                                             || warpAmpNeedsDc (warp2ModeA_, warp2AmountA_, warp2Var_[0])))
                    || (engine_ == Engine::FM && warpAmpNeedsDc (warp2ModeA_, warp2AmountA_, warp2Var_[0])))
                { sA_L = wtRectDcAL_.process (sA_L); sA_R = wtRectDcAR_.process (sA_R); }
                if (engine_ == Engine::GRAN) { sA_L = granBlkAL_[(size_t) i]; sA_R = granBlkAR_[(size_t) i]; }   // GRANULAR-ENGINE-VOICE
                if (engine_ == Engine::SPEC) { sA_L = geodeBlkAL_[(size_t) i]; sA_R = geodeBlkAR_[(size_t) i]; } // GEODE-ENGINE-VOICE
                if (engine_ == Engine::HARM) { sA_L = harmBlkAL_[(size_t) i]; sA_R = harmBlkAR_[(size_t) i]; } // HARMONIC-ENGINE-VOICE
                if (engine_ == Engine::MODAL) { sA_L = modalBlkAL_[(size_t) i]; sA_R = modalBlkAR_[(size_t) i]; } // MODAL-ENGINE-VOICE
                if (engine_ == Engine::SAMP) { sA_L = sampBlkAL_[(size_t) i]; sA_R = sampBlkAR_[(size_t) i];  // SAMPLE-ENGINE-VOICE
                    airSmA_ += (sampleParamsA_.air - airSmA_) * lvlSmCoef_;   // fb204 — AIR glide (block-pushed mod stepped the shaper amount)
                    const float airA = airSmA_;
                    if (airA > 0.001f) {
                        const float drv = 1.0f + airA * 20.0f;   // AMPLIFIED — night-and-day "fresh air" / overdrive at 100%
                        sampAirLpAL_ += airHpCoef_ * (sA_L - sampAirLpAL_); const float hpL = sA_L - sampAirLpAL_;
                        sA_L += airA * 2.0f * (std::tanh (hpL * drv) - hpL);
                        sampAirLpAR_ += airHpCoef_ * (sA_R - sampAirLpAR_); const float hpR = sA_R - sampAirLpAR_;
                        sA_R += airA * 2.0f * (std::tanh (hpR * drv) - hpR);
                    }
                }
                // ── SAMPLE WARP shaper — shared by SAMPLE *and* GRANULAR (grain clouds run
                //    quiet; Drive/Fold/Sine Shaper is how a low one-shot gets turned UP). The
                //    granular AIR lives in-engine; the shaper state is per-osc, and an osc is
                //    only ever ONE of SAMP/GRAN, so reusing the DC-block/fold state is safe. ──
                if (engine_ == Engine::SAMP || engine_ == Engine::GRAN || engine_ == Engine::SPEC || engine_ == Engine::HARM || engine_ == Engine::MODAL) {
                    const float warpA = sampleParamsA_.warp;
                    if (warpA > 0.001f) {
                        switch (sampleParamsA_.warpMode) {
                            case 1: sA_L = applyAmpWarp (10, warpA, sA_L);  sA_R = applyAmpWarp (10, warpA, sA_R); break;   // Sine Shaper
                            case 2: sA_L = applyAmpWarp (9,  warpA, sA_L);  sA_R = applyAmpWarp (9,  warpA, sA_R);
                                    sA_L = spRectDcAL_.process (sA_L); sA_R = spRectDcAR_.process (sA_R); break;   // Rectify (+ DC block)
                            case 3: sA_L = applyFoldADAA (sA_L, 0, warpA, sampWarpFoldAL_); sA_R = applyFoldADAA (sA_R, 0, warpA, sampWarpFoldAR_); break;   // Fold
                            case 4: { const float d = 1.0f + warpA * 9.0f;
                                      sA_L = sA_L * (1.0f - warpA) + std::tanh (sA_L * d) * warpA;
                                      sA_R = sA_R * (1.0f - warpA) + std::tanh (sA_R * d) * warpA; } break;                 // Drive
                            case 5: { const float L = juce::jmax (4.0f, 64.0f - (warpA * warpA) * 60.0f);
                                      sA_L = sA_L * (1.0f - warpA) + (std::round (sA_L * L) / L) * warpA;
                                      sA_R = sA_R * (1.0f - warpA) + (std::round (sA_R * L) / L) * warpA; } break;          // Crush
                            default: break;   // Off
                        }
                    }
                }
                // BLEND MODES (carrier = block engine): phase-modulate OSC A's rendered block by the
                // cross-osc warp (FM/PD on a sample/granular/spec/harm/modal). Skipped unless A is a
                // block engine with an armed slot — WT/FM carriers were injected in the unison loop
                // above, and an un-blended osc bypasses this entirely (bit-identical to today).
                if (blkCarrierArmed_[0] || blkArmSm_[0] > 1.0e-4f)
                    blendReadBlock (0, blendOff[0], blkCarrierArmed_[0], sA_L, sA_R);
                if (anyBlendArmed_) { sA_L *= blendAmp[0]; sA_R *= blendAmp[0]; }   // BLEND AM/RM (amplitude-domain, all engines; 1.0 = inert) — fb522 skips the x1.0 when nothing is armed
                // SUB — voice-anchored sub layer, mono/centered, energy-neutral sum
                if (sub_[0].on) subMix (0, sA_L, sA_R, subMono0);
                if (! spectralBypassA_)
                {
                    if (spectralTypeA_ <= 2)
                    {
                        // LP, HP, Smear — biquad
                        sA_L = spectralFilterAL_.processSample (sA_L);
                        sA_R = spectralFilterAR_.processSample (sA_R);
                    }
                    else if (spectralTypeA_ == 3)
                    {
                        // Comb — feedforward y = x + x[n-N]
                        const int N = juce::jlimit (1, kSpectralCombSize - 1,
                                                     (int) (4.0f + spectralAmtA_ * (float) (kSpectralCombSize - 8)));
                        const int readIdx = (spectralCombWriteA_ - N + kSpectralCombSize) % kSpectralCombSize;
                        const float dryL = sA_L;
                        const float dryR = sA_R;
                        sA_L = dryL + spectralCombAL_[(size_t) readIdx] * spectralAmtA_;
                        sA_R = dryR + spectralCombAR_[(size_t) readIdx] * spectralAmtA_;
                        spectralCombAL_[(size_t) spectralCombWriteA_] = dryL;
                        spectralCombAR_[(size_t) spectralCombWriteA_] = dryR;
                        spectralCombWriteA_ = (spectralCombWriteA_ + 1) % kSpectralCombSize;
                        // Normalize loudness — comb output can grow up to 2x
                        sA_L *= 0.5f;
                        sA_R *= 0.5f;
                    }
                    else if (spectralTypeA_ == 4)
                    {
                        // Ring Mod — modulate by sine at frequency 30..2000 Hz scaled by amount²
                        const double modHz = 30.0 + (double) (spectralAmtA_ * spectralAmtA_) * 1970.0;
                        const double inc = modHz / sampleRate_;
                        const float modL = static_cast<float> (std::sin (6.2831853071795865 * spectralRingPhaseA_));
                        spectralRingPhaseA_ += inc;
                        if (spectralRingPhaseA_ >= 1.0) spectralRingPhaseA_ -= 1.0;
                        // Wet/dry blend by amount
                        sA_L = sA_L * (1.0f - spectralAmtA_) + (sA_L * modL) * spectralAmtA_;
                        sA_R = sA_R * (1.0f - spectralAmtA_) + (sA_R * modL) * spectralAmtA_;
                    }
                    else if (spectralTypeA_ == 5)
                    {
                        // Bit Crush — quantize to N levels, N = 64 → 4 by amount²
                        const float levels = 64.0f - (spectralAmtA_ * spectralAmtA_) * 60.0f;
                        const float L = juce::jmax (4.0f, levels);
                        sA_L = std::round (sA_L * L) / L;
                        sA_R = std::round (sA_R * L) / L;
                    }
                    else if (spectralTypeA_ == 6)
                    {
                        // Downsample — sample-and-hold at lower rate
                        const float divisor = 1.0f + spectralAmtA_ * spectralAmtA_ * 31.0f;  // 1..32
                        spectralDsCounterA_ += 1.0f;
                        if (spectralDsCounterA_ >= divisor)
                        {
                            spectralDsHeldAL_ = sA_L;
                            spectralDsHeldAR_ = sA_R;
                            spectralDsCounterA_ -= divisor;
                        }
                        sA_L = spectralDsHeldAL_;
                        sA_R = spectralDsHeldAR_;
                    }
                    else if (spectralTypeA_ == 7)
                    {
                        // Tube — asymmetric soft clipping with positive bias
                        const float drive = 1.0f + spectralAmtA_ * spectralAmtA_ * 9.0f;
                        const float bias = 0.15f * spectralAmtA_;
                        const float invSat = 1.0f / std::tanh (drive);
                        // fb313 — exact DC removal subtracts f(bias), NOT bias. The shaper's output at
                        // zero input is tanh(bias)·invSat; subtracting `bias` leaves a residual offset.
                        // Benign here (bias ≤ 0.15 ⇒ ~0.001 error) but the pattern is a guaranteed
                        // note-off click at the ±1.0 bias ranges the Distortion device will use.
                        // Correct form for every shaper: y = (f(g·x + b) − f(b)) · makeup.
                        const float dcOff = std::tanh (bias) * invSat;
                        sA_L = std::tanh (sA_L * drive + bias) * invSat - dcOff;
                        sA_R = std::tanh (sA_R * drive + bias) * invSat - dcOff;
                    }
                    else if (spectralTypeA_ == 8)
                    {
                        // Tilt — low-shelf cut + high-shelf boost, one-pole based
                        const float alpha = 0.005f;  // ~120 Hz at 48kHz
                        spectralTiltLowAL_ += alpha * (sA_L - spectralTiltLowAL_);
                        spectralTiltLowAR_ += alpha * (sA_R - spectralTiltLowAR_);
                        const float lowL = spectralTiltLowAL_;
                        const float lowR = spectralTiltLowAR_;
                        const float highL = sA_L - lowL;
                        const float highR = sA_R - lowR;
                        const float lowGain  = 1.0f - spectralAmtA_;
                        const float highGain = 1.0f + spectralAmtA_ * 2.0f;
                        sA_L = lowL * lowGain + highL * highGain;
                        sA_R = lowR * lowGain + highR * highGain;
                    }
                    else if (spectralTypeA_ == 9)
                    {
                        // Vibrato — short modulated delay creates pitch wobble
                        const double modHz = 1.0 + (double) spectralAmtA_ * 8.0;
                        const double inc   = modHz / sampleRate_;
                        spectralVibPhaseA_ += inc;
                        if (spectralVibPhaseA_ >= 1.0) spectralVibPhaseA_ -= 1.0;
                        const float lfo = static_cast<float> (std::sin (6.2831853071795865 * spectralVibPhaseA_));
                        const float depthSamples = spectralAmtA_ * 20.0f;
                        const float delaySamples = (float) (kSpectralVibSize - 4) * 0.5f + lfo * depthSamples;
                        const int   intDel       = juce::jlimit (1, kSpectralVibSize - 2, (int) delaySamples);
                        const int   readIdx      = (spectralVibWriteA_ - intDel + kSpectralVibSize) % kSpectralVibSize;
                        const float dryL = sA_L, dryR = sA_R;
                        sA_L = dryL * (1.0f - spectralAmtA_) + spectralVibAL_[(size_t) readIdx] * spectralAmtA_;
                        sA_R = dryR * (1.0f - spectralAmtA_) + spectralVibAR_[(size_t) readIdx] * spectralAmtA_;
                        spectralVibAL_[(size_t) spectralVibWriteA_] = dryL;
                        spectralVibAR_[(size_t) spectralVibWriteA_] = dryR;
                        spectralVibWriteA_ = (spectralVibWriteA_ + 1) % kSpectralVibSize;
                    }
                }

                // ── OSC B — sum across activeUnisonB_ sines (per-OSC unison) ─────
                float sumBL = 0.0f, sumBR = 0.0f;

                for (int u = 0; uLoopB && u < activeUnisonB_; ++u)
                {
                    float sBu = 0.0f;

                    switch (engineB_)
                    {
                        case Engine::WT:
                        {
                            if (currentWavetableB_ != nullptr)
                            {
                                double warpedPhase = uPhaseB_[(size_t) u];
                                // fb522 — UWARP: the per-sine WARP FAN. `uniWarpOnB_` is the
                                // bit-identity gate — at UWARP = 0 (the default, and every patch in
                                // the library) this is a predicted branch straight onto the untouched
                                // block value and costs nothing. Both slots share the one knob (Serum
                                // has two; the fan direction is the sine's own u_norm either way).
                                const float wAmt1B = (uniWarpOnB_ || blendWarpArmed_[1]) ? juce::jlimit (0.0f, 1.0f, warpAmountB_ + (uniWarpOnB_ ? uWarpOffB_[(size_t) u] : 0.0f) + blendWarp[1]) : warpAmountB_;
                                const float wAmt2B = (uniWarpOnB_ || blendWarpArmed_[1]) ? juce::jlimit (0.0f, 1.0f, warp2AmountB_ + (uniWarpOnB_ ? uWarpOffB_[(size_t) u] : 0.0f) + blendWarp[1]) : warp2AmountB_;
                                float  window      = 1.0f;
                                bool   skipLookup  = false;

                                // WARP slot 1 + chained WARP 2 (see OSC A — identical structure).
                                warpedPhase = applyPhaseWarp (warpModeB_, wAmt1B, warpedPhase, window, skipLookup, warpVar_[1], drawFor (1, 0));
                                if (! skipLookup && warp2ModeB_ != 0)
                                    warpedPhase = applyPhaseWarp (warp2ModeB_, wAmt2B, warpedPhase, window, skipLookup, warp2Var_[1], drawFor (1, 1));

                                if (skipLookup)
                                {
                                    sBu = 0.0f;
                                }
                                else
                                {
                                    // WT BLUR — read the per-block blended single-cycle buffer.
                                    double rpB = warpedPhase + (double) blendOff[1]; rpB -= std::floor (rpB); sBu = wtBlendRead (blendB_.data(), blendPrevB_.data(), blendXfB_, blendFrac, (float) rpB);   // BLEND inject · fb248 crossfade
                                    sBu *= window;

                                    sBu = applyAmpWarp (warpModeB_, wAmt1B, sBu, warpVar_[1], drawFor (1, 0));   // slot 1 amp-domain
                                    sBu = applyAmpWarp (warp2ModeB_, wAmt2B, sBu, warp2Var_[1], drawFor (1, 1));   // WARP 2 amp-domain, chained
                                }
                            }
                            else
                            {
                                sBu = static_cast<float> (2.0 * uPhaseB_[(size_t) u] - 1.0);
                                sBu -= static_cast<float> (polyBlep (uPhaseB_[(size_t) u], uPhaseIncB_[(size_t) u]));
                            }
                            uPhaseB_[(size_t) u] += uPhaseIncB_[(size_t) u] + phaseOffStep_[1];   // fb544 — continuous PHASE
                            if (uPhaseB_[(size_t) u] >= 1.0) uPhaseB_[(size_t) u] -= 1.0;
                            else if (uPhaseB_[(size_t) u] < 0.0) uPhaseB_[(size_t) u] += 1.0;   // fb544 — the step can be NEGATIVE
                            break;
                        }


                        case Engine::FM:
                        {
                            // FM-ENGINE-VOICE — wavetable-carrier FM + WEATHERING (see OSC A for the map)
                            const double pi2 = 6.2831853071795865;
                            const double inc = uPhaseIncB_[(size_t) u];
                            if (u == 0) {   // AGE de-zipper — glide the FM index per-sample (kills block-step crackle)
                                fmD1Now_[1] += (fmD1Eff_[1] - fmD1Now_[1]) * fmIdxGlideCoef_;
                                fmD2Now_[1] += (fmD2Eff_[1] - fmD2Now_[1]) * fmIdxGlideCoef_;
                                fmFbNow_[1]           += (fmFbEff_[1]        - fmFbNow_[1])           * fmIdxGlideCoef_;   // fb204 — FB/STORM/QUAKE/SCORCH ride the same de-zipper
                                fmStormM12Now_[1]     += (fmStormM12_[1]     - fmStormM12Now_[1])     * fmIdxGlideCoef_;
                                fmStormM21Now_[1]     += (fmStormM21_[1]     - fmStormM21Now_[1])     * fmIdxGlideCoef_;
                                fmQuakeIdxNow_[1]     += (fmQuakeIdx_[1]     - fmQuakeIdxNow_[1])     * fmIdxGlideCoef_;
                                fmQuakeFryNow_[1]     += (fmQuakeFry_[1]     - fmQuakeFryNow_[1])     * fmIdxGlideCoef_;
                                fmScorchIdxMulNow_[1] += (fmScorchIdxMul_[1] - fmScorchIdxMulNow_[1]) * fmIdxGlideCoef_;
                                fmScorchPreNow_[1]    += (fmScorchPre_[1]    - fmScorchPreNow_[1])    * fmIdxGlideCoef_;
                                fmScorchBiasNow_[1]   += (fmScorchBias_[1]   - fmScorchBiasNow_[1])   * fmIdxGlideCoef_;
                                fmScorchTanhBiasNow_[1] += (fmScorchTanhBias_[1] - fmScorchTanhBiasNow_[1]) * fmIdxGlideCoef_;
                                fmScorchMakeupNow_[1] += (fmScorchMakeup_[1] - fmScorchMakeupNow_[1]) * fmIdxGlideCoef_;
                            }
                            const float  d1  = fmD1Now_[1] * fmScorchIdxMulNow_[1];   // SCORCH index push (glided base)
                            const float  d2  = fmD2Now_[1] * fmScorchIdxMulNow_[1];
                            const float  fbk = fmFbNow_[1];                        // (SCORCH grit already folded in)
                            const int    alg = fmAlgo_[1];
                            float m2 = static_cast<float> (std::sin (pi2 * (uMod2PhaseB_[(size_t) u]
                                                        + (double) (fmStormM12Now_[1] * fmPrevM1B_[(size_t) u]))));
                            if (fmScorchPreNow_[1] > 1.0f) m2 = (fmFastTanh (fmScorchPreNow_[1] * m2 + fmScorchBiasNow_[1]) - fmScorchTanhBiasNow_[1]) * fmScorchMakeupNow_[1];
                            double m1Arg = uModPhaseB_[(size_t) u] + (double) (fbk * fmFbB_[(size_t) u])
                                         + (double) (fmStormM21Now_[1] * m2);
                            if (alg != 1) m1Arg += (double) (d2 * m2);       // STACK + RING: M2 -> M1
                            float m1 = static_cast<float> (std::sin (pi2 * m1Arg));
                            if (fmScorchPreNow_[1] > 1.0f) m1 = (fmFastTanh (fmScorchPreNow_[1] * m1 + fmScorchBiasNow_[1]) - fmScorchTanhBiasNow_[1]) * fmScorchMakeupNow_[1];
                            fmFbB_[(size_t) u] = 0.5f * (fmFbB_[(size_t) u] + m1);
                            fmPrevM1B_[(size_t) u] = m1;
                            double qSubB = 0.0;
                            if (fmQuakeIdxNow_[1] > 1.0e-5f)
                            {
                                fmQuakePhaseB_[(size_t) u] += inc * (double) fmQuakeSubRatio_[1];
                                fmQuakePhaseB_[(size_t) u] -= std::floor (fmQuakePhaseB_[(size_t) u]);
                                float sub = static_cast<float> (std::sin (pi2 * fmQuakePhaseB_[(size_t) u]));
                                if (fmQuakeFryNow_[1] > 0.0f) sub += fmQuakeFryNow_[1] * (sub - sub * sub * sub * (1.0f / 6.0f));
                                qSubB = (double) (fmQuakeIdxNow_[1] * sub);
                            }
                            double cPh = uPhaseB_[(size_t) u] + qSubB + (double) blendOff[1];   // BLEND inject
                            if (alg != 2) cPh += (double) (d1 * m1);
                            if (alg == 1) cPh += (double) (d2 * m2);
                            cPh -= std::floor (cPh);
                            float fmWin = 1.0f; bool fmSkip = false;   // WARP 2 on the FM carrier
                            // fb522 — the WARP FAN reaches the FM carrier's warp slot too (see the WT branch).
                            const float wAmt2B = (uniWarpOnB_ || blendWarpArmed_[1]) ? juce::jlimit (0.0f, 1.0f, warp2AmountB_ + (uniWarpOnB_ ? uWarpOffB_[(size_t) u] : 0.0f) + blendWarp[1]) : warp2AmountB_;
                            if (warp2ModeB_ != 0)
                                cPh = applyPhaseWarp (warp2ModeB_, wAmt2B, cPh, fmWin, fmSkip, warp2Var_[1], drawFor (1, 1));
                            if (fmSkip) sBu = 0.0f;
                            else
                            {
                                sBu = (currentWavetableB_ != nullptr)
                                        ? wtBlendRead (blendB_.data(), blendPrevB_.data(), blendXfB_, blendFrac, (float) cPh)
                                        : static_cast<float> (std::sin (pi2 * cPh));
                                sBu *= fmWin;
                                sBu = applyAmpWarp (warp2ModeB_, wAmt2B, sBu, warp2Var_[1], drawFor (1, 1));
                            }
                            if (alg == 2)
                                sBu *= (1.0f - fmD1Sm_[1]) + fmD1Sm_[1] * m1;
                            uModPhaseB_[(size_t) u]  += inc * fmR1Eff_[1] + fmRustTps_[1];
                            uModPhaseB_[(size_t) u]  -= std::floor (uModPhaseB_[(size_t) u]);
                            uMod2PhaseB_[(size_t) u] += inc * fmR2Eff_[1];
                            uMod2PhaseB_[(size_t) u] -= std::floor (uMod2PhaseB_[(size_t) u]);
                            uPhaseB_[(size_t) u] += inc + phaseOffStep_[1];   // fb544 — continuous PHASE
                            if (uPhaseB_[(size_t) u] >= 1.0) uPhaseB_[(size_t) u] -= 1.0;
                            else if (uPhaseB_[(size_t) u] < 0.0) uPhaseB_[(size_t) u] += 1.0;   // fb544 — the step can be NEGATIVE
                            break;
                        }

                        case Engine::SAMP:
                        case Engine::GRAN:
                        case Engine::SPEC:
                        case Engine::HARM:
                        case Engine::MODAL:
                            sBu = 0.0f; break;
                    }

                    // Phase 11d — FOLD applied per-sine, post-engine, pre-pan.
                    sBu = applyFoldADAA (sBu, foldShapeB_, foldAmountB_, foldStateB_[(size_t) u]);

                    // Per-sine pan into the OSC B stereo sum.
                    sumBL += sBu * uPanLB_[(size_t) u];
                    sumBR += sBu * uPanRB_[(size_t) u];
                }

                // Auto-gain (RMS-constant): holds loudness as voices / blend change.
                sumBL *= uNormB_;
                sumBR *= uNormB_;
                float sB_L = sumBL;
                float sB_R = sumBR;
                // WARP FILTER (modes 35/36) — on the SUMMED osc. Linear operator, so this is
                // identical to filtering every sine, at 1/16 the cost. Both slots chain.
                for (int sl = 0; sl < 2; ++sl)
                    if (wfCoef_[1][sl].on) {
                        // fb553 — AUDIO-RATE CUTOFF. Armed only by a mode-6 blend slot, so the
                        //  unmodulated path below is the original two lines, untouched.
                        if (blendWarpArmed_[1])
                        {
                            WarpFiltCoef cf = wfCoef_[1][sl];
                            warpFiltFast (cf, wfKx_[1], wfXMin_,
                                          juce::jlimit (0.0f, 1.0f, wfAmt_[1][sl] + blendWarp[1]));
                            sB_L = warpFiltTick (cf, wfState_[1][sl][0], sB_L);
                            sB_R = warpFiltTick (cf, wfState_[1][sl][1], sB_R);
                        }
                        else {
                        sB_L = warpFiltTick (wfCoef_[1][sl], wfState_[1][sl][0], sB_L);
                        sB_R = warpFiltTick (wfCoef_[1][sl], wfState_[1][sl][1], sB_R); }
                    }
                // RECTIFY DC block — wavetable warp == Rectify (slot 1 or 2), else dormant/bit-identical.
                if ((engineB_ == Engine::WT && (warpAmpNeedsDc (warpModeB_, warpAmountB_, warpVar_[1])
                                             || warpAmpNeedsDc (warp2ModeB_, warp2AmountB_, warp2Var_[1])))
                    || (engineB_ == Engine::FM && warpAmpNeedsDc (warp2ModeB_, warp2AmountB_, warp2Var_[1])))
                { sB_L = wtRectDcBL_.process (sB_L); sB_R = wtRectDcBR_.process (sB_R); }
                if (engineB_ == Engine::GRAN) { sB_L = granBlkBL_[(size_t) i]; sB_R = granBlkBR_[(size_t) i]; }   // GRANULAR-ENGINE-VOICE
                if (engineB_ == Engine::SPEC) { sB_L = geodeBlkBL_[(size_t) i]; sB_R = geodeBlkBR_[(size_t) i]; } // GEODE-ENGINE-VOICE
                if (engineB_ == Engine::HARM) { sB_L = harmBlkBL_[(size_t) i]; sB_R = harmBlkBR_[(size_t) i]; } // HARMONIC-ENGINE-VOICE
                if (engineB_ == Engine::MODAL) { sB_L = modalBlkBL_[(size_t) i]; sB_R = modalBlkBR_[(size_t) i]; } // MODAL-ENGINE-VOICE
                if (engineB_ == Engine::SAMP) { sB_L = sampBlkBL_[(size_t) i]; sB_R = sampBlkBR_[(size_t) i];  // SAMPLE-ENGINE-VOICE
                    airSmB_ += (sampleParamsB_.air - airSmB_) * lvlSmCoef_;   // fb204 — AIR glide (block-pushed mod stepped the shaper amount)
                    const float airB = airSmB_;
                    if (airB > 0.001f) {
                        const float drv = 1.0f + airB * 20.0f;   // AMPLIFIED — night-and-day "fresh air" / overdrive at 100%
                        sampAirLpBL_ += airHpCoef_ * (sB_L - sampAirLpBL_); const float hpL = sB_L - sampAirLpBL_;
                        sB_L += airB * 2.0f * (std::tanh (hpL * drv) - hpL);
                        sampAirLpBR_ += airHpCoef_ * (sB_R - sampAirLpBR_); const float hpR = sB_R - sampAirLpBR_;
                        sB_R += airB * 2.0f * (std::tanh (hpR * drv) - hpR);
                    }
                }
                // ── SAMPLE WARP shaper — shared by SAMPLE *and* GRANULAR (grain clouds run
                //    quiet; Drive/Fold/Sine Shaper is how a low one-shot gets turned UP). The
                //    granular AIR lives in-engine; the shaper state is per-osc, and an osc is
                //    only ever ONE of SAMP/GRAN, so reusing the DC-block/fold state is safe. ──
                if (engineB_ == Engine::SAMP || engineB_ == Engine::GRAN || engineB_ == Engine::SPEC || engineB_ == Engine::HARM || engineB_ == Engine::MODAL) {
                    const float warpB = sampleParamsB_.warp;
                    if (warpB > 0.001f) {
                        switch (sampleParamsB_.warpMode) {
                            case 1: sB_L = applyAmpWarp (10, warpB, sB_L);  sB_R = applyAmpWarp (10, warpB, sB_R); break;   // Sine Shaper
                            case 2: sB_L = applyAmpWarp (9,  warpB, sB_L);  sB_R = applyAmpWarp (9,  warpB, sB_R);
                                    sB_L = spRectDcBL_.process (sB_L); sB_R = spRectDcBR_.process (sB_R); break;   // Rectify (+ DC block)
                            case 3: sB_L = applyFoldADAA (sB_L, 0, warpB, sampWarpFoldBL_); sB_R = applyFoldADAA (sB_R, 0, warpB, sampWarpFoldBR_); break;   // Fold
                            case 4: { const float d = 1.0f + warpB * 9.0f;
                                      sB_L = sB_L * (1.0f - warpB) + std::tanh (sB_L * d) * warpB;
                                      sB_R = sB_R * (1.0f - warpB) + std::tanh (sB_R * d) * warpB; } break;                 // Drive
                            case 5: { const float L = juce::jmax (4.0f, 64.0f - (warpB * warpB) * 60.0f);
                                      sB_L = sB_L * (1.0f - warpB) + (std::round (sB_L * L) / L) * warpB;
                                      sB_R = sB_R * (1.0f - warpB) + (std::round (sB_R * L) / L) * warpB; } break;          // Crush
                            default: break;   // Off
                        }
                    }
                }
                // SUB — voice-anchored sub layer, mono/centered, energy-neutral sum
                // BLEND MODES (carrier = block engine): phase-modulate OSC B's rendered block (see OSC A).
                if (blkCarrierArmed_[1] || blkArmSm_[1] > 1.0e-4f)
                    blendReadBlock (1, blendOff[1], blkCarrierArmed_[1], sB_L, sB_R);
                if (anyBlendArmed_) { sB_L *= blendAmp[1]; sB_R *= blendAmp[1]; }   // BLEND AM/RM — fb522 skips the x1.0 when nothing is armed
                if (sub_[1].on) subMix (1, sB_L, sB_R, subMono1);
                if (! spectralBypassB_)
                {
                    if (spectralTypeB_ <= 2)
                    {
                        // LP, HP, Smear — biquad
                        sB_L = spectralFilterBL_.processSample (sB_L);
                        sB_R = spectralFilterBR_.processSample (sB_R);
                    }
                    else if (spectralTypeB_ == 3)
                    {
                        // Comb — feedforward y = x + x[n-N]
                        const int N = juce::jlimit (1, kSpectralCombSize - 1,
                                                     (int) (4.0f + spectralAmtB_ * (float) (kSpectralCombSize - 8)));
                        const int readIdx = (spectralCombWriteB_ - N + kSpectralCombSize) % kSpectralCombSize;
                        const float dryL = sB_L;
                        const float dryR = sB_R;
                        sB_L = dryL + spectralCombBL_[(size_t) readIdx] * spectralAmtB_;
                        sB_R = dryR + spectralCombBR_[(size_t) readIdx] * spectralAmtB_;
                        spectralCombBL_[(size_t) spectralCombWriteB_] = dryL;
                        spectralCombBR_[(size_t) spectralCombWriteB_] = dryR;
                        spectralCombWriteB_ = (spectralCombWriteB_ + 1) % kSpectralCombSize;
                        sB_L *= 0.5f;
                        sB_R *= 0.5f;
                    }
                    else if (spectralTypeB_ == 4)
                    {
                        // Ring Mod
                        const double modHz = 30.0 + (double) (spectralAmtB_ * spectralAmtB_) * 1970.0;
                        const double inc = modHz / sampleRate_;
                        const float modL = static_cast<float> (std::sin (6.2831853071795865 * spectralRingPhaseB_));
                        spectralRingPhaseB_ += inc;
                        if (spectralRingPhaseB_ >= 1.0) spectralRingPhaseB_ -= 1.0;
                        sB_L = sB_L * (1.0f - spectralAmtB_) + (sB_L * modL) * spectralAmtB_;
                        sB_R = sB_R * (1.0f - spectralAmtB_) + (sB_R * modL) * spectralAmtB_;
                    }
                    else if (spectralTypeB_ == 5)
                    {
                        // Bit Crush
                        const float levels = 64.0f - (spectralAmtB_ * spectralAmtB_) * 60.0f;
                        const float L = juce::jmax (4.0f, levels);
                        sB_L = std::round (sB_L * L) / L;
                        sB_R = std::round (sB_R * L) / L;
                    }
                    else if (spectralTypeB_ == 6)
                    {
                        // Downsample — sample-and-hold at lower rate
                        const float divisor = 1.0f + spectralAmtB_ * spectralAmtB_ * 31.0f;
                        spectralDsCounterB_ += 1.0f;
                        if (spectralDsCounterB_ >= divisor)
                        {
                            spectralDsHeldBL_ = sB_L;
                            spectralDsHeldBR_ = sB_R;
                            spectralDsCounterB_ -= divisor;
                        }
                        sB_L = spectralDsHeldBL_;
                        sB_R = spectralDsHeldBR_;
                    }
                    else if (spectralTypeB_ == 7)
                    {
                        // Tube — asymmetric soft clipping with positive bias
                        const float drive = 1.0f + spectralAmtB_ * spectralAmtB_ * 9.0f;
                        const float bias = 0.15f * spectralAmtB_;
                        const float invSat = 1.0f / std::tanh (drive);
                        sB_L = std::tanh (sB_L * drive + bias) * invSat - bias * invSat;
                        sB_R = std::tanh (sB_R * drive + bias) * invSat - bias * invSat;
                    }
                    else if (spectralTypeB_ == 8)
                    {
                        // Tilt — low-shelf cut + high-shelf boost, one-pole based
                        const float alpha = 0.005f;
                        spectralTiltLowBL_ += alpha * (sB_L - spectralTiltLowBL_);
                        spectralTiltLowBR_ += alpha * (sB_R - spectralTiltLowBR_);
                        const float lowL = spectralTiltLowBL_;
                        const float lowR = spectralTiltLowBR_;
                        const float highL = sB_L - lowL;
                        const float highR = sB_R - lowR;
                        const float lowGain  = 1.0f - spectralAmtB_;
                        const float highGain = 1.0f + spectralAmtB_ * 2.0f;
                        sB_L = lowL * lowGain + highL * highGain;
                        sB_R = lowR * lowGain + highR * highGain;
                    }
                    else if (spectralTypeB_ == 9)
                    {
                        // Vibrato — short modulated delay creates pitch wobble
                        const double modHz = 1.0 + (double) spectralAmtB_ * 8.0;
                        const double inc   = modHz / sampleRate_;
                        spectralVibPhaseB_ += inc;
                        if (spectralVibPhaseB_ >= 1.0) spectralVibPhaseB_ -= 1.0;
                        const float lfo = static_cast<float> (std::sin (6.2831853071795865 * spectralVibPhaseB_));
                        const float depthSamples = spectralAmtB_ * 20.0f;
                        const float delaySamples = (float) (kSpectralVibSize - 4) * 0.5f + lfo * depthSamples;
                        const int   intDel       = juce::jlimit (1, kSpectralVibSize - 2, (int) delaySamples);
                        const int   readIdx      = (spectralVibWriteB_ - intDel + kSpectralVibSize) % kSpectralVibSize;
                        const float dryL = sB_L, dryR = sB_R;
                        sB_L = dryL * (1.0f - spectralAmtB_) + spectralVibBL_[(size_t) readIdx] * spectralAmtB_;
                        sB_R = dryR * (1.0f - spectralAmtB_) + spectralVibBR_[(size_t) readIdx] * spectralAmtB_;
                        spectralVibBL_[(size_t) spectralVibWriteB_] = dryL;
                        spectralVibBR_[(size_t) spectralVibWriteB_] = dryR;
                        spectralVibWriteB_ = (spectralVibWriteB_ + 1) % kSpectralVibSize;
                    }
                }
                // ── OSC C — sum across activeUnisonC_ sines (per-OSC unison) ─────
                float sumCL = 0.0f, sumCR = 0.0f;

                for (int u = 0; uLoopC && u < activeUnisonC_; ++u)
                {
                    float sCu = 0.0f;

                    switch (engineC_)
                    {
                        case Engine::WT:
                        {
                            if (currentWavetableC_ != nullptr)
                            {
                                double warpedPhase = uPhaseC_[(size_t) u];
                                // fb522 — UWARP: the per-sine WARP FAN. `uniWarpOnC_` is the
                                // bit-identity gate — at UWARP = 0 (the default, and every patch in
                                // the library) this is a predicted branch straight onto the untouched
                                // block value and costs nothing. Both slots share the one knob (Serum
                                // has two; the fan direction is the sine's own u_norm either way).
                                const float wAmt1C = (uniWarpOnC_ || blendWarpArmed_[2]) ? juce::jlimit (0.0f, 1.0f, warpAmountC_ + (uniWarpOnC_ ? uWarpOffC_[(size_t) u] : 0.0f) + blendWarp[2]) : warpAmountC_;
                                const float wAmt2C = (uniWarpOnC_ || blendWarpArmed_[2]) ? juce::jlimit (0.0f, 1.0f, warp2AmountC_ + (uniWarpOnC_ ? uWarpOffC_[(size_t) u] : 0.0f) + blendWarp[2]) : warp2AmountC_;
                                float  window      = 1.0f;
                                bool   skipLookup  = false;

                                // WARP slot 1 + chained WARP 2 (see OSC A — identical structure).
                                warpedPhase = applyPhaseWarp (warpModeC_, wAmt1C, warpedPhase, window, skipLookup, warpVar_[2], drawFor (2, 0));
                                if (! skipLookup && warp2ModeC_ != 0)
                                    warpedPhase = applyPhaseWarp (warp2ModeC_, wAmt2C, warpedPhase, window, skipLookup, warp2Var_[2], drawFor (2, 1));

                                if (skipLookup)
                                {
                                    sCu = 0.0f;
                                }
                                else
                                {
                                    // WT BLUR — read the per-block blended single-cycle buffer.
                                    double rpC = warpedPhase + (double) blendOff[2]; rpC -= std::floor (rpC); sCu = wtBlendRead (blendC_.data(), blendPrevC_.data(), blendXfC_, blendFrac, (float) rpC);   // BLEND inject · fb248 crossfade
                                    sCu *= window;

                                    sCu = applyAmpWarp (warpModeC_, wAmt1C, sCu, warpVar_[2], drawFor (2, 0));   // slot 1 amp-domain
                                    sCu = applyAmpWarp (warp2ModeC_, wAmt2C, sCu, warp2Var_[2], drawFor (2, 1));   // WARP 2 amp-domain, chained
                                }
                            }
                            else
                            {
                                sCu = static_cast<float> (2.0 * uPhaseC_[(size_t) u] - 1.0);
                                sCu -= static_cast<float> (polyBlep (uPhaseC_[(size_t) u], uPhaseIncC_[(size_t) u]));
                            }
                            uPhaseC_[(size_t) u] += uPhaseIncC_[(size_t) u] + phaseOffStep_[2];   // fb544 — continuous PHASE
                            if (uPhaseC_[(size_t) u] >= 1.0) uPhaseC_[(size_t) u] -= 1.0;
                            else if (uPhaseC_[(size_t) u] < 0.0) uPhaseC_[(size_t) u] += 1.0;   // fb544 — the step can be NEGATIVE
                            break;
                        }


                        case Engine::FM:
                        {
                            // FM-ENGINE-VOICE — wavetable-carrier FM + WEATHERING (see OSC A for the map)
                            const double pi2 = 6.2831853071795865;
                            const double inc = uPhaseIncC_[(size_t) u];
                            if (u == 0) {   // AGE de-zipper — glide the FM index per-sample (kills block-step crackle)
                                fmD1Now_[2] += (fmD1Eff_[2] - fmD1Now_[2]) * fmIdxGlideCoef_;
                                fmD2Now_[2] += (fmD2Eff_[2] - fmD2Now_[2]) * fmIdxGlideCoef_;
                                fmFbNow_[2]           += (fmFbEff_[2]        - fmFbNow_[2])           * fmIdxGlideCoef_;   // fb204 — FB/STORM/QUAKE/SCORCH ride the same de-zipper
                                fmStormM12Now_[2]     += (fmStormM12_[2]     - fmStormM12Now_[2])     * fmIdxGlideCoef_;
                                fmStormM21Now_[2]     += (fmStormM21_[2]     - fmStormM21Now_[2])     * fmIdxGlideCoef_;
                                fmQuakeIdxNow_[2]     += (fmQuakeIdx_[2]     - fmQuakeIdxNow_[2])     * fmIdxGlideCoef_;
                                fmQuakeFryNow_[2]     += (fmQuakeFry_[2]     - fmQuakeFryNow_[2])     * fmIdxGlideCoef_;
                                fmScorchIdxMulNow_[2] += (fmScorchIdxMul_[2] - fmScorchIdxMulNow_[2]) * fmIdxGlideCoef_;
                                fmScorchPreNow_[2]    += (fmScorchPre_[2]    - fmScorchPreNow_[2])    * fmIdxGlideCoef_;
                                fmScorchBiasNow_[2]   += (fmScorchBias_[2]   - fmScorchBiasNow_[2])   * fmIdxGlideCoef_;
                                fmScorchTanhBiasNow_[2] += (fmScorchTanhBias_[2] - fmScorchTanhBiasNow_[2]) * fmIdxGlideCoef_;
                                fmScorchMakeupNow_[2] += (fmScorchMakeup_[2] - fmScorchMakeupNow_[2]) * fmIdxGlideCoef_;
                            }
                            const float  d1  = fmD1Now_[2] * fmScorchIdxMulNow_[2];   // SCORCH index push (glided base)
                            const float  d2  = fmD2Now_[2] * fmScorchIdxMulNow_[2];
                            const float  fbk = fmFbNow_[2];                        // (SCORCH grit already folded in)
                            const int    alg = fmAlgo_[2];
                            float m2 = static_cast<float> (std::sin (pi2 * (uMod2PhaseC_[(size_t) u]
                                                        + (double) (fmStormM12Now_[2] * fmPrevM1C_[(size_t) u]))));
                            if (fmScorchPreNow_[2] > 1.0f) m2 = (fmFastTanh (fmScorchPreNow_[2] * m2 + fmScorchBiasNow_[2]) - fmScorchTanhBiasNow_[2]) * fmScorchMakeupNow_[2];
                            double m1Arg = uModPhaseC_[(size_t) u] + (double) (fbk * fmFbC_[(size_t) u])
                                         + (double) (fmStormM21Now_[2] * m2);
                            if (alg != 1) m1Arg += (double) (d2 * m2);       // STACK + RING: M2 -> M1
                            float m1 = static_cast<float> (std::sin (pi2 * m1Arg));
                            if (fmScorchPreNow_[2] > 1.0f) m1 = (fmFastTanh (fmScorchPreNow_[2] * m1 + fmScorchBiasNow_[2]) - fmScorchTanhBiasNow_[2]) * fmScorchMakeupNow_[2];
                            fmFbC_[(size_t) u] = 0.5f * (fmFbC_[(size_t) u] + m1);
                            fmPrevM1C_[(size_t) u] = m1;
                            double qSubC = 0.0;
                            if (fmQuakeIdxNow_[2] > 1.0e-5f)
                            {
                                fmQuakePhaseC_[(size_t) u] += inc * (double) fmQuakeSubRatio_[2];
                                fmQuakePhaseC_[(size_t) u] -= std::floor (fmQuakePhaseC_[(size_t) u]);
                                float sub = static_cast<float> (std::sin (pi2 * fmQuakePhaseC_[(size_t) u]));
                                if (fmQuakeFryNow_[2] > 0.0f) sub += fmQuakeFryNow_[2] * (sub - sub * sub * sub * (1.0f / 6.0f));
                                qSubC = (double) (fmQuakeIdxNow_[2] * sub);
                            }
                            double cPh = uPhaseC_[(size_t) u] + qSubC + (double) blendOff[2];   // BLEND inject
                            if (alg != 2) cPh += (double) (d1 * m1);
                            if (alg == 1) cPh += (double) (d2 * m2);
                            cPh -= std::floor (cPh);
                            float fmWin = 1.0f; bool fmSkip = false;   // WARP 2 on the FM carrier
                            // fb522 — the WARP FAN reaches the FM carrier's warp slot too (see the WT branch).
                            const float wAmt2C = (uniWarpOnC_ || blendWarpArmed_[2]) ? juce::jlimit (0.0f, 1.0f, warp2AmountC_ + (uniWarpOnC_ ? uWarpOffC_[(size_t) u] : 0.0f) + blendWarp[2]) : warp2AmountC_;
                            if (warp2ModeC_ != 0)
                                cPh = applyPhaseWarp (warp2ModeC_, wAmt2C, cPh, fmWin, fmSkip, warp2Var_[2], drawFor (2, 1));
                            if (fmSkip) sCu = 0.0f;
                            else
                            {
                                sCu = (currentWavetableC_ != nullptr)
                                        ? wtBlendRead (blendC_.data(), blendPrevC_.data(), blendXfC_, blendFrac, (float) cPh)
                                        : static_cast<float> (std::sin (pi2 * cPh));
                                sCu *= fmWin;
                                sCu = applyAmpWarp (warp2ModeC_, wAmt2C, sCu, warp2Var_[2], drawFor (2, 1));
                            }
                            if (alg == 2)
                                sCu *= (1.0f - fmD1Sm_[2]) + fmD1Sm_[2] * m1;
                            uModPhaseC_[(size_t) u]  += inc * fmR1Eff_[2] + fmRustTps_[2];
                            uModPhaseC_[(size_t) u]  -= std::floor (uModPhaseC_[(size_t) u]);
                            uMod2PhaseC_[(size_t) u] += inc * fmR2Eff_[2];
                            uMod2PhaseC_[(size_t) u] -= std::floor (uMod2PhaseC_[(size_t) u]);
                            uPhaseC_[(size_t) u] += inc + phaseOffStep_[2];   // fb544 — continuous PHASE
                            if (uPhaseC_[(size_t) u] >= 1.0) uPhaseC_[(size_t) u] -= 1.0;
                            else if (uPhaseC_[(size_t) u] < 0.0) uPhaseC_[(size_t) u] += 1.0;   // fb544 — the step can be NEGATIVE
                            break;
                        }

                        case Engine::SAMP:
                        case Engine::GRAN:
                        case Engine::SPEC:
                        case Engine::HARM:
                        case Engine::MODAL:
                            sCu = 0.0f; break;
                    }

                    // Phase 11d — FOLD applied per-sine, post-engine, pre-pan.
                    sCu = applyFoldADAA (sCu, foldShapeC_, foldAmountC_, foldStateC_[(size_t) u]);

                    // Per-sine pan into the OSC C stereo sum.
                    sumCL += sCu * uPanLC_[(size_t) u];
                    sumCR += sCu * uPanRC_[(size_t) u];
                }

                // Auto-gain (RMS-constant): holds loudness as voices / blend change.
                sumCL *= uNormC_;
                sumCR *= uNormC_;
                float sC_L = sumCL;
                float sC_R = sumCR;
                // WARP FILTER (modes 35/36) — on the SUMMED osc. Linear operator, so this is
                // identical to filtering every sine, at 1/16 the cost. Both slots chain.
                for (int sl = 0; sl < 2; ++sl)
                    if (wfCoef_[2][sl].on) {
                        // fb553 — AUDIO-RATE CUTOFF. Armed only by a mode-6 blend slot, so the
                        //  unmodulated path below is the original two lines, untouched.
                        if (blendWarpArmed_[2])
                        {
                            WarpFiltCoef cf = wfCoef_[2][sl];
                            warpFiltFast (cf, wfKx_[2], wfXMin_,
                                          juce::jlimit (0.0f, 1.0f, wfAmt_[2][sl] + blendWarp[2]));
                            sC_L = warpFiltTick (cf, wfState_[2][sl][0], sC_L);
                            sC_R = warpFiltTick (cf, wfState_[2][sl][1], sC_R);
                        }
                        else {
                        sC_L = warpFiltTick (wfCoef_[2][sl], wfState_[2][sl][0], sC_L);
                        sC_R = warpFiltTick (wfCoef_[2][sl], wfState_[2][sl][1], sC_R); }
                    }
                // RECTIFY DC block — wavetable warp == Rectify (slot 1 or 2), else dormant/bit-identical.
                if ((engineC_ == Engine::WT && (warpAmpNeedsDc (warpModeC_, warpAmountC_, warpVar_[2])
                                             || warpAmpNeedsDc (warp2ModeC_, warp2AmountC_, warp2Var_[2])))
                    || (engineC_ == Engine::FM && warpAmpNeedsDc (warp2ModeC_, warp2AmountC_, warp2Var_[2])))
                { sC_L = wtRectDcCL_.process (sC_L); sC_R = wtRectDcCR_.process (sC_R); }
                if (engineC_ == Engine::GRAN) { sC_L = granBlkCL_[(size_t) i]; sC_R = granBlkCR_[(size_t) i]; }   // GRANULAR-ENGINE-VOICE
                if (engineC_ == Engine::SPEC) { sC_L = geodeBlkCL_[(size_t) i]; sC_R = geodeBlkCR_[(size_t) i]; } // GEODE-ENGINE-VOICE
                if (engineC_ == Engine::HARM) { sC_L = harmBlkCL_[(size_t) i]; sC_R = harmBlkCR_[(size_t) i]; } // HARMONIC-ENGINE-VOICE
                if (engineC_ == Engine::MODAL) { sC_L = modalBlkCL_[(size_t) i]; sC_R = modalBlkCR_[(size_t) i]; } // MODAL-ENGINE-VOICE
                if (engineC_ == Engine::SAMP) { sC_L = sampBlkCL_[(size_t) i]; sC_R = sampBlkCR_[(size_t) i];  // SAMPLE-ENGINE-VOICE
                    airSmC_ += (sampleParamsC_.air - airSmC_) * lvlSmCoef_;   // fb204 — AIR glide (block-pushed mod stepped the shaper amount)
                    const float airC = airSmC_;
                    if (airC > 0.001f) {
                        const float drv = 1.0f + airC * 20.0f;   // AMPLIFIED — night-and-day "fresh air" / overdrive at 100%
                        sampAirLpCL_ += airHpCoef_ * (sC_L - sampAirLpCL_); const float hpL = sC_L - sampAirLpCL_;
                        sC_L += airC * 2.0f * (std::tanh (hpL * drv) - hpL);
                        sampAirLpCR_ += airHpCoef_ * (sC_R - sampAirLpCR_); const float hpR = sC_R - sampAirLpCR_;
                        sC_R += airC * 2.0f * (std::tanh (hpR * drv) - hpR);
                    }
                }
                // ── SAMPLE WARP shaper — shared by SAMPLE *and* GRANULAR (grain clouds run
                //    quiet; Drive/Fold/Sine Shaper is how a low one-shot gets turned UP). The
                //    granular AIR lives in-engine; the shaper state is per-osc, and an osc is
                //    only ever ONE of SAMP/GRAN, so reusing the DC-block/fold state is safe. ──
                if (engineC_ == Engine::SAMP || engineC_ == Engine::GRAN || engineC_ == Engine::SPEC || engineC_ == Engine::HARM || engineC_ == Engine::MODAL) {
                    const float warpC = sampleParamsC_.warp;
                    if (warpC > 0.001f) {
                        switch (sampleParamsC_.warpMode) {
                            case 1: sC_L = applyAmpWarp (10, warpC, sC_L);  sC_R = applyAmpWarp (10, warpC, sC_R); break;   // Sine Shaper
                            case 2: sC_L = applyAmpWarp (9,  warpC, sC_L);  sC_R = applyAmpWarp (9,  warpC, sC_R);
                                    sC_L = spRectDcCL_.process (sC_L); sC_R = spRectDcCR_.process (sC_R); break;   // Rectify (+ DC block)
                            case 3: sC_L = applyFoldADAA (sC_L, 0, warpC, sampWarpFoldCL_); sC_R = applyFoldADAA (sC_R, 0, warpC, sampWarpFoldCR_); break;   // Fold
                            case 4: { const float d = 1.0f + warpC * 9.0f;
                                      sC_L = sC_L * (1.0f - warpC) + std::tanh (sC_L * d) * warpC;
                                      sC_R = sC_R * (1.0f - warpC) + std::tanh (sC_R * d) * warpC; } break;                 // Drive
                            case 5: { const float L = juce::jmax (4.0f, 64.0f - (warpC * warpC) * 60.0f);
                                      sC_L = sC_L * (1.0f - warpC) + (std::round (sC_L * L) / L) * warpC;
                                      sC_R = sC_R * (1.0f - warpC) + (std::round (sC_R * L) / L) * warpC; } break;          // Crush
                            default: break;   // Off
                        }
                    }
                }
                // SUB — voice-anchored sub layer, mono/centered, energy-neutral sum
                // BLEND MODES (carrier = block engine): phase-modulate OSC C's rendered block (see OSC A).
                if (blkCarrierArmed_[2] || blkArmSm_[2] > 1.0e-4f)
                    blendReadBlock (2, blendOff[2], blkCarrierArmed_[2], sC_L, sC_R);
                if (anyBlendArmed_) { sC_L *= blendAmp[2]; sC_R *= blendAmp[2]; }   // BLEND AM/RM — fb522 skips the x1.0 when nothing is armed
                if (sub_[2].on) subMix (2, sC_L, sC_R, subMono2);
                if (! spectralBypassC_)
                {
                    if (spectralTypeC_ <= 2)
                    {
                        // LP, HP, Smear — biquad
                        sC_L = spectralFilterCL_.processSample (sC_L);
                        sC_R = spectralFilterCR_.processSample (sC_R);
                    }
                    else if (spectralTypeC_ == 3)
                    {
                        // Comb — feedforward y = x + x[n-N]
                        const int N = juce::jlimit (1, kSpectralCombSize - 1,
                                                     (int) (4.0f + spectralAmtC_ * (float) (kSpectralCombSize - 8)));
                        const int readIdx = (spectralCombWriteC_ - N + kSpectralCombSize) % kSpectralCombSize;
                        const float dryL = sC_L;
                        const float dryR = sC_R;
                        sC_L = dryL + spectralCombCL_[(size_t) readIdx] * spectralAmtC_;
                        sC_R = dryR + spectralCombCR_[(size_t) readIdx] * spectralAmtC_;
                        spectralCombCL_[(size_t) spectralCombWriteC_] = dryL;
                        spectralCombCR_[(size_t) spectralCombWriteC_] = dryR;
                        spectralCombWriteC_ = (spectralCombWriteC_ + 1) % kSpectralCombSize;
                        sC_L *= 0.5f;
                        sC_R *= 0.5f;
                    }
                    else if (spectralTypeC_ == 4)
                    {
                        // Ring Mod
                        const double modHz = 30.0 + (double) (spectralAmtC_ * spectralAmtC_) * 1970.0;
                        const double inc = modHz / sampleRate_;
                        const float modL = static_cast<float> (std::sin (6.2831853071795865 * spectralRingPhaseC_));
                        spectralRingPhaseC_ += inc;
                        if (spectralRingPhaseC_ >= 1.0) spectralRingPhaseC_ -= 1.0;
                        sC_L = sC_L * (1.0f - spectralAmtC_) + (sC_L * modL) * spectralAmtC_;
                        sC_R = sC_R * (1.0f - spectralAmtC_) + (sC_R * modL) * spectralAmtC_;
                    }
                    else if (spectralTypeC_ == 5)
                    {
                        // Bit Crush
                        const float levels = 64.0f - (spectralAmtC_ * spectralAmtC_) * 60.0f;
                        const float L = juce::jmax (4.0f, levels);
                        sC_L = std::round (sC_L * L) / L;
                        sC_R = std::round (sC_R * L) / L;
                    }
                    else if (spectralTypeC_ == 6)
                    {
                        // Downsample — sample-and-hold at lower rate
                        const float divisor = 1.0f + spectralAmtC_ * spectralAmtC_ * 31.0f;
                        spectralDsCounterC_ += 1.0f;
                        if (spectralDsCounterC_ >= divisor)
                        {
                            spectralDsHeldCL_ = sC_L;
                            spectralDsHeldCR_ = sC_R;
                            spectralDsCounterC_ -= divisor;
                        }
                        sC_L = spectralDsHeldCL_;
                        sC_R = spectralDsHeldCR_;
                    }
                    else if (spectralTypeC_ == 7)
                    {
                        // Tube — asymmetric soft clipping with positive bias
                        const float drive = 1.0f + spectralAmtC_ * spectralAmtC_ * 9.0f;
                        const float bias = 0.15f * spectralAmtC_;
                        const float invSat = 1.0f / std::tanh (drive);
                        sC_L = std::tanh (sC_L * drive + bias) * invSat - bias * invSat;
                        sC_R = std::tanh (sC_R * drive + bias) * invSat - bias * invSat;
                    }
                    else if (spectralTypeC_ == 8)
                    {
                        // Tilt — low-shelf cut + high-shelf boost, one-pole based
                        const float alpha = 0.005f;
                        spectralTiltLowCL_ += alpha * (sC_L - spectralTiltLowCL_);
                        spectralTiltLowCR_ += alpha * (sC_R - spectralTiltLowCR_);
                        const float lowL = spectralTiltLowCL_;
                        const float lowR = spectralTiltLowCR_;
                        const float highL = sC_L - lowL;
                        const float highR = sC_R - lowR;
                        const float lowGain  = 1.0f - spectralAmtC_;
                        const float highGain = 1.0f + spectralAmtC_ * 2.0f;
                        sC_L = lowL * lowGain + highL * highGain;
                        sC_R = lowR * lowGain + highR * highGain;
                    }
                    else if (spectralTypeC_ == 9)
                    {
                        // Vibrato — short modulated delay creates pitch wobble
                        const double modHz = 1.0 + (double) spectralAmtC_ * 8.0;
                        const double inc   = modHz / sampleRate_;
                        spectralVibPhaseC_ += inc;
                        if (spectralVibPhaseC_ >= 1.0) spectralVibPhaseC_ -= 1.0;
                        const float lfo = static_cast<float> (std::sin (6.2831853071795865 * spectralVibPhaseC_));
                        const float depthSamples = spectralAmtC_ * 20.0f;
                        const float delaySamples = (float) (kSpectralVibSize - 4) * 0.5f + lfo * depthSamples;
                        const int   intDel       = juce::jlimit (1, kSpectralVibSize - 2, (int) delaySamples);
                        const int   readIdx      = (spectralVibWriteC_ - intDel + kSpectralVibSize) % kSpectralVibSize;
                        const float dryL = sC_L, dryR = sC_R;
                        sC_L = dryL * (1.0f - spectralAmtC_) + spectralVibCL_[(size_t) readIdx] * spectralAmtC_;
                        sC_R = dryR * (1.0f - spectralAmtC_) + spectralVibCR_[(size_t) readIdx] * spectralAmtC_;
                        spectralVibCL_[(size_t) spectralVibWriteC_] = dryL;
                        spectralVibCR_[(size_t) spectralVibWriteC_] = dryR;
                        spectralVibWriteC_ = (spectralVibWriteC_ + 1) % kSpectralVibSize;
                    }
                }
                // ── OSC D — sum across activeUnisonD_ sines (per-OSC unison) ─────
                float sumDL = 0.0f, sumDR = 0.0f;

                for (int u = 0; uLoopD && u < activeUnisonD_; ++u)
                {
                    float sDu = 0.0f;

                    switch (engineD_)
                    {
                        case Engine::WT:
                        {
                            if (currentWavetableD_ != nullptr)
                            {
                                double warpedPhase = uPhaseD_[(size_t) u];
                                // fb522 — UWARP: the per-sine WARP FAN. `uniWarpOnD_` is the
                                // bit-identity gate — at UWARP = 0 (the default, and every patch in
                                // the library) this is a predicted branch straight onto the untouched
                                // block value and costs nothing. Both slots share the one knob (Serum
                                // has two; the fan direction is the sine's own u_norm either way).
                                const float wAmt1D = (uniWarpOnD_ || blendWarpArmed_[3]) ? juce::jlimit (0.0f, 1.0f, warpAmountD_ + (uniWarpOnD_ ? uWarpOffD_[(size_t) u] : 0.0f) + blendWarp[3]) : warpAmountD_;
                                const float wAmt2D = (uniWarpOnD_ || blendWarpArmed_[3]) ? juce::jlimit (0.0f, 1.0f, warp2AmountD_ + (uniWarpOnD_ ? uWarpOffD_[(size_t) u] : 0.0f) + blendWarp[3]) : warp2AmountD_;
                                float  window      = 1.0f;
                                bool   skipLookup  = false;

                                // WARP slot 1 + chained WARP 2 (see OSC A — identical structure).
                                warpedPhase = applyPhaseWarp (warpModeD_, wAmt1D, warpedPhase, window, skipLookup, warpVar_[3], drawFor (3, 0));
                                if (! skipLookup && warp2ModeD_ != 0)
                                    warpedPhase = applyPhaseWarp (warp2ModeD_, wAmt2D, warpedPhase, window, skipLookup, warp2Var_[3], drawFor (3, 1));

                                if (skipLookup)
                                {
                                    sDu = 0.0f;
                                }
                                else
                                {
                                    // WT BLUR — read the per-block blended single-cycle buffer.
                                    double rpD = warpedPhase + (double) blendOff[3]; rpD -= std::floor (rpD); sDu = wtBlendRead (blendD_.data(), blendPrevD_.data(), blendXfD_, blendFrac, (float) rpD);   // BLEND inject · fb248 crossfade
                                    sDu *= window;

                                    sDu = applyAmpWarp (warpModeD_, wAmt1D, sDu, warpVar_[3], drawFor (3, 0));   // slot 1 amp-domain
                                    sDu = applyAmpWarp (warp2ModeD_, wAmt2D, sDu, warp2Var_[3], drawFor (3, 1));   // WARP 2 amp-domain, chained
                                }
                            }
                            else
                            {
                                sDu = static_cast<float> (2.0 * uPhaseD_[(size_t) u] - 1.0);
                                sDu -= static_cast<float> (polyBlep (uPhaseD_[(size_t) u], uPhaseIncD_[(size_t) u]));
                            }
                            uPhaseD_[(size_t) u] += uPhaseIncD_[(size_t) u] + phaseOffStep_[3];   // fb544 — continuous PHASE
                            if (uPhaseD_[(size_t) u] >= 1.0) uPhaseD_[(size_t) u] -= 1.0;
                            else if (uPhaseD_[(size_t) u] < 0.0) uPhaseD_[(size_t) u] += 1.0;   // fb544 — the step can be NEGATIVE
                            break;
                        }


                        case Engine::FM:
                        {
                            // FM-ENGINE-VOICE — wavetable-carrier FM + WEATHERING (see OSC A for the map)
                            const double pi2 = 6.2831853071795865;
                            const double inc = uPhaseIncD_[(size_t) u];
                            if (u == 0) {   // AGE de-zipper — glide the FM index per-sample (kills block-step crackle)
                                fmD1Now_[3] += (fmD1Eff_[3] - fmD1Now_[3]) * fmIdxGlideCoef_;
                                fmD2Now_[3] += (fmD2Eff_[3] - fmD2Now_[3]) * fmIdxGlideCoef_;
                                fmFbNow_[3]           += (fmFbEff_[3]        - fmFbNow_[3])           * fmIdxGlideCoef_;   // fb204 — FB/STORM/QUAKE/SCORCH ride the same de-zipper
                                fmStormM12Now_[3]     += (fmStormM12_[3]     - fmStormM12Now_[3])     * fmIdxGlideCoef_;
                                fmStormM21Now_[3]     += (fmStormM21_[3]     - fmStormM21Now_[3])     * fmIdxGlideCoef_;
                                fmQuakeIdxNow_[3]     += (fmQuakeIdx_[3]     - fmQuakeIdxNow_[3])     * fmIdxGlideCoef_;
                                fmQuakeFryNow_[3]     += (fmQuakeFry_[3]     - fmQuakeFryNow_[3])     * fmIdxGlideCoef_;
                                fmScorchIdxMulNow_[3] += (fmScorchIdxMul_[3] - fmScorchIdxMulNow_[3]) * fmIdxGlideCoef_;
                                fmScorchPreNow_[3]    += (fmScorchPre_[3]    - fmScorchPreNow_[3])    * fmIdxGlideCoef_;
                                fmScorchBiasNow_[3]   += (fmScorchBias_[3]   - fmScorchBiasNow_[3])   * fmIdxGlideCoef_;
                                fmScorchTanhBiasNow_[3] += (fmScorchTanhBias_[3] - fmScorchTanhBiasNow_[3]) * fmIdxGlideCoef_;
                                fmScorchMakeupNow_[3] += (fmScorchMakeup_[3] - fmScorchMakeupNow_[3]) * fmIdxGlideCoef_;
                            }
                            const float  d1  = fmD1Now_[3] * fmScorchIdxMulNow_[3];   // SCORCH index push (glided base)
                            const float  d2  = fmD2Now_[3] * fmScorchIdxMulNow_[3];
                            const float  fbk = fmFbNow_[3];                        // (SCORCH grit already folded in)
                            const int    alg = fmAlgo_[3];
                            float m2 = static_cast<float> (std::sin (pi2 * (uMod2PhaseD_[(size_t) u]
                                                        + (double) (fmStormM12Now_[3] * fmPrevM1D_[(size_t) u]))));
                            if (fmScorchPreNow_[3] > 1.0f) m2 = (fmFastTanh (fmScorchPreNow_[3] * m2 + fmScorchBiasNow_[3]) - fmScorchTanhBiasNow_[3]) * fmScorchMakeupNow_[3];
                            double m1Arg = uModPhaseD_[(size_t) u] + (double) (fbk * fmFbD_[(size_t) u])
                                         + (double) (fmStormM21Now_[3] * m2);
                            if (alg != 1) m1Arg += (double) (d2 * m2);       // STACK + RING: M2 -> M1
                            float m1 = static_cast<float> (std::sin (pi2 * m1Arg));
                            if (fmScorchPreNow_[3] > 1.0f) m1 = (fmFastTanh (fmScorchPreNow_[3] * m1 + fmScorchBiasNow_[3]) - fmScorchTanhBiasNow_[3]) * fmScorchMakeupNow_[3];
                            fmFbD_[(size_t) u] = 0.5f * (fmFbD_[(size_t) u] + m1);
                            fmPrevM1D_[(size_t) u] = m1;
                            double qSubD = 0.0;
                            if (fmQuakeIdxNow_[3] > 1.0e-5f)
                            {
                                fmQuakePhaseD_[(size_t) u] += inc * (double) fmQuakeSubRatio_[3];
                                fmQuakePhaseD_[(size_t) u] -= std::floor (fmQuakePhaseD_[(size_t) u]);
                                float sub = static_cast<float> (std::sin (pi2 * fmQuakePhaseD_[(size_t) u]));
                                if (fmQuakeFryNow_[3] > 0.0f) sub += fmQuakeFryNow_[3] * (sub - sub * sub * sub * (1.0f / 6.0f));
                                qSubD = (double) (fmQuakeIdxNow_[3] * sub);
                            }
                            double cPh = uPhaseD_[(size_t) u] + qSubD + (double) blendOff[3];   // BLEND inject
                            if (alg != 2) cPh += (double) (d1 * m1);
                            if (alg == 1) cPh += (double) (d2 * m2);
                            cPh -= std::floor (cPh);
                            float fmWin = 1.0f; bool fmSkip = false;   // WARP 2 on the FM carrier
                            // fb522 — the WARP FAN reaches the FM carrier's warp slot too (see the WT branch).
                            const float wAmt2D = (uniWarpOnD_ || blendWarpArmed_[3]) ? juce::jlimit (0.0f, 1.0f, warp2AmountD_ + (uniWarpOnD_ ? uWarpOffD_[(size_t) u] : 0.0f) + blendWarp[3]) : warp2AmountD_;
                            if (warp2ModeD_ != 0)
                                cPh = applyPhaseWarp (warp2ModeD_, wAmt2D, cPh, fmWin, fmSkip, warp2Var_[3], drawFor (3, 1));
                            if (fmSkip) sDu = 0.0f;
                            else
                            {
                                sDu = (currentWavetableD_ != nullptr)
                                        ? wtBlendRead (blendD_.data(), blendPrevD_.data(), blendXfD_, blendFrac, (float) cPh)
                                        : static_cast<float> (std::sin (pi2 * cPh));
                                sDu *= fmWin;
                                sDu = applyAmpWarp (warp2ModeD_, wAmt2D, sDu, warp2Var_[3], drawFor (3, 1));
                            }
                            if (alg == 2)
                                sDu *= (1.0f - fmD1Sm_[3]) + fmD1Sm_[3] * m1;
                            uModPhaseD_[(size_t) u]  += inc * fmR1Eff_[3] + fmRustTps_[3];
                            uModPhaseD_[(size_t) u]  -= std::floor (uModPhaseD_[(size_t) u]);
                            uMod2PhaseD_[(size_t) u] += inc * fmR2Eff_[3];
                            uMod2PhaseD_[(size_t) u] -= std::floor (uMod2PhaseD_[(size_t) u]);
                            uPhaseD_[(size_t) u] += inc + phaseOffStep_[3];   // fb544 — continuous PHASE
                            if (uPhaseD_[(size_t) u] >= 1.0) uPhaseD_[(size_t) u] -= 1.0;
                            else if (uPhaseD_[(size_t) u] < 0.0) uPhaseD_[(size_t) u] += 1.0;   // fb544 — the step can be NEGATIVE
                            break;
                        }

                        case Engine::SAMP:
                        case Engine::GRAN:
                        case Engine::SPEC:
                        case Engine::HARM:
                        case Engine::MODAL:
                            sDu = 0.0f; break;
                    }

                    // Phase 11d — FOLD applied per-sine, post-engine, pre-pan.
                    sDu = applyFoldADAA (sDu, foldShapeD_, foldAmountD_, foldStateD_[(size_t) u]);

                    // Per-sine pan into the OSC D stereo sum.
                    sumDL += sDu * uPanLD_[(size_t) u];
                    sumDR += sDu * uPanRD_[(size_t) u];
                }

                // Auto-gain (RMS-constant): holds loudness as voices / blend change.
                sumDL *= uNormD_;
                sumDR *= uNormD_;
                float sD_L = sumDL;
                float sD_R = sumDR;
                // WARP FILTER (modes 35/36) — on the SUMMED osc. Linear operator, so this is
                // identical to filtering every sine, at 1/16 the cost. Both slots chain.
                for (int sl = 0; sl < 2; ++sl)
                    if (wfCoef_[3][sl].on) {
                        // fb553 — AUDIO-RATE CUTOFF. Armed only by a mode-6 blend slot, so the
                        //  unmodulated path below is the original two lines, untouched.
                        if (blendWarpArmed_[3])
                        {
                            WarpFiltCoef cf = wfCoef_[3][sl];
                            warpFiltFast (cf, wfKx_[3], wfXMin_,
                                          juce::jlimit (0.0f, 1.0f, wfAmt_[3][sl] + blendWarp[3]));
                            sD_L = warpFiltTick (cf, wfState_[3][sl][0], sD_L);
                            sD_R = warpFiltTick (cf, wfState_[3][sl][1], sD_R);
                        }
                        else {
                        sD_L = warpFiltTick (wfCoef_[3][sl], wfState_[3][sl][0], sD_L);
                        sD_R = warpFiltTick (wfCoef_[3][sl], wfState_[3][sl][1], sD_R); }
                    }
                // RECTIFY DC block — wavetable warp == Rectify (slot 1 or 2), else dormant/bit-identical.
                if ((engineD_ == Engine::WT && (warpAmpNeedsDc (warpModeD_, warpAmountD_, warpVar_[3])
                                             || warpAmpNeedsDc (warp2ModeD_, warp2AmountD_, warp2Var_[3])))
                    || (engineD_ == Engine::FM && warpAmpNeedsDc (warp2ModeD_, warp2AmountD_, warp2Var_[3])))
                { sD_L = wtRectDcDL_.process (sD_L); sD_R = wtRectDcDR_.process (sD_R); }
                if (engineD_ == Engine::GRAN) { sD_L = granBlkDL_[(size_t) i]; sD_R = granBlkDR_[(size_t) i]; }   // GRANULAR-ENGINE-VOICE
                if (engineD_ == Engine::SPEC) { sD_L = geodeBlkDL_[(size_t) i]; sD_R = geodeBlkDR_[(size_t) i]; } // GEODE-ENGINE-VOICE
                if (engineD_ == Engine::HARM) { sD_L = harmBlkDL_[(size_t) i]; sD_R = harmBlkDR_[(size_t) i]; } // HARMONIC-ENGINE-VOICE
                if (engineD_ == Engine::MODAL) { sD_L = modalBlkDL_[(size_t) i]; sD_R = modalBlkDR_[(size_t) i]; } // MODAL-ENGINE-VOICE
                if (engineD_ == Engine::SAMP) { sD_L = sampBlkDL_[(size_t) i]; sD_R = sampBlkDR_[(size_t) i];  // SAMPLE-ENGINE-VOICE
                    airSmD_ += (sampleParamsD_.air - airSmD_) * lvlSmCoef_;   // fb204 — AIR glide (block-pushed mod stepped the shaper amount)
                    const float airD = airSmD_;
                    if (airD > 0.001f) {
                        const float drv = 1.0f + airD * 20.0f;   // AMPLIFIED — night-and-day "fresh air" / overdrive at 100%
                        sampAirLpDL_ += airHpCoef_ * (sD_L - sampAirLpDL_); const float hpL = sD_L - sampAirLpDL_;
                        sD_L += airD * 2.0f * (std::tanh (hpL * drv) - hpL);
                        sampAirLpDR_ += airHpCoef_ * (sD_R - sampAirLpDR_); const float hpR = sD_R - sampAirLpDR_;
                        sD_R += airD * 2.0f * (std::tanh (hpR * drv) - hpR);
                    }
                }
                // ── SAMPLE WARP shaper — shared by SAMPLE *and* GRANULAR (grain clouds run
                //    quiet; Drive/Fold/Sine Shaper is how a low one-shot gets turned UP). The
                //    granular AIR lives in-engine; the shaper state is per-osc, and an osc is
                //    only ever ONE of SAMP/GRAN, so reusing the DC-block/fold state is safe. ──
                if (engineD_ == Engine::SAMP || engineD_ == Engine::GRAN || engineD_ == Engine::SPEC || engineD_ == Engine::HARM || engineD_ == Engine::MODAL) {
                    const float warpD = sampleParamsD_.warp;
                    if (warpD > 0.001f) {
                        switch (sampleParamsD_.warpMode) {
                            case 1: sD_L = applyAmpWarp (10, warpD, sD_L);  sD_R = applyAmpWarp (10, warpD, sD_R); break;   // Sine Shaper
                            case 2: sD_L = applyAmpWarp (9,  warpD, sD_L);  sD_R = applyAmpWarp (9,  warpD, sD_R);
                                    sD_L = spRectDcDL_.process (sD_L); sD_R = spRectDcDR_.process (sD_R); break;   // Rectify (+ DC block)
                            case 3: sD_L = applyFoldADAA (sD_L, 0, warpD, sampWarpFoldDL_); sD_R = applyFoldADAA (sD_R, 0, warpD, sampWarpFoldDR_); break;   // Fold
                            case 4: { const float d = 1.0f + warpD * 9.0f;
                                      sD_L = sD_L * (1.0f - warpD) + std::tanh (sD_L * d) * warpD;
                                      sD_R = sD_R * (1.0f - warpD) + std::tanh (sD_R * d) * warpD; } break;                 // Drive
                            case 5: { const float L = juce::jmax (4.0f, 64.0f - (warpD * warpD) * 60.0f);
                                      sD_L = sD_L * (1.0f - warpD) + (std::round (sD_L * L) / L) * warpD;
                                      sD_R = sD_R * (1.0f - warpD) + (std::round (sD_R * L) / L) * warpD; } break;          // Crush
                            default: break;   // Off
                        }
                    }
                }
                // SUB — voice-anchored sub layer, mono/centered, energy-neutral sum
                // BLEND MODES (carrier = block engine): phase-modulate OSC D's rendered block (see OSC A).
                if (blkCarrierArmed_[3] || blkArmSm_[3] > 1.0e-4f)
                    blendReadBlock (3, blendOff[3], blkCarrierArmed_[3], sD_L, sD_R);
                if (anyBlendArmed_) { sD_L *= blendAmp[3]; sD_R *= blendAmp[3]; }   // BLEND AM/RM — fb522 skips the x1.0 when nothing is armed
                if (sub_[3].on) subMix (3, sD_L, sD_R, subMono3);
                if (! spectralBypassD_)
                {
                    if (spectralTypeD_ <= 2)
                    {
                        // LP, HP, Smear — biquad
                        sD_L = spectralFilterDL_.processSample (sD_L);
                        sD_R = spectralFilterDR_.processSample (sD_R);
                    }
                    else if (spectralTypeD_ == 3)
                    {
                        // Comb — feedforward y = x + x[n-N]
                        const int N = juce::jlimit (1, kSpectralCombSize - 1,
                                                     (int) (4.0f + spectralAmtD_ * (float) (kSpectralCombSize - 8)));
                        const int readIdx = (spectralCombWriteD_ - N + kSpectralCombSize) % kSpectralCombSize;
                        const float dryL = sD_L;
                        const float dryR = sD_R;
                        sD_L = dryL + spectralCombDL_[(size_t) readIdx] * spectralAmtD_;
                        sD_R = dryR + spectralCombDR_[(size_t) readIdx] * spectralAmtD_;
                        spectralCombDL_[(size_t) spectralCombWriteD_] = dryL;
                        spectralCombDR_[(size_t) spectralCombWriteD_] = dryR;
                        spectralCombWriteD_ = (spectralCombWriteD_ + 1) % kSpectralCombSize;
                        sD_L *= 0.5f;
                        sD_R *= 0.5f;
                    }
                    else if (spectralTypeD_ == 4)
                    {
                        // Ring Mod
                        const double modHz = 30.0 + (double) (spectralAmtD_ * spectralAmtD_) * 1970.0;
                        const double inc = modHz / sampleRate_;
                        const float modL = static_cast<float> (std::sin (6.2831853071795865 * spectralRingPhaseD_));
                        spectralRingPhaseD_ += inc;
                        if (spectralRingPhaseD_ >= 1.0) spectralRingPhaseD_ -= 1.0;
                        sD_L = sD_L * (1.0f - spectralAmtD_) + (sD_L * modL) * spectralAmtD_;
                        sD_R = sD_R * (1.0f - spectralAmtD_) + (sD_R * modL) * spectralAmtD_;
                    }
                    else if (spectralTypeD_ == 5)
                    {
                        // Bit Crush
                        const float levels = 64.0f - (spectralAmtD_ * spectralAmtD_) * 60.0f;
                        const float L = juce::jmax (4.0f, levels);
                        sD_L = std::round (sD_L * L) / L;
                        sD_R = std::round (sD_R * L) / L;
                    }
                    else if (spectralTypeD_ == 6)
                    {
                        // Downsample — sample-and-hold at lower rate
                        const float divisor = 1.0f + spectralAmtD_ * spectralAmtD_ * 31.0f;
                        spectralDsCounterD_ += 1.0f;
                        if (spectralDsCounterD_ >= divisor)
                        {
                            spectralDsHeldDL_ = sD_L;
                            spectralDsHeldDR_ = sD_R;
                            spectralDsCounterD_ -= divisor;
                        }
                        sD_L = spectralDsHeldDL_;
                        sD_R = spectralDsHeldDR_;
                    }
                    else if (spectralTypeD_ == 7)
                    {
                        // Tube — asymmetric soft clipping with positive bias
                        const float drive = 1.0f + spectralAmtD_ * spectralAmtD_ * 9.0f;
                        const float bias = 0.15f * spectralAmtD_;
                        const float invSat = 1.0f / std::tanh (drive);
                        sD_L = std::tanh (sD_L * drive + bias) * invSat - bias * invSat;
                        sD_R = std::tanh (sD_R * drive + bias) * invSat - bias * invSat;
                    }
                    else if (spectralTypeD_ == 8)
                    {
                        // Tilt — low-shelf cut + high-shelf boost, one-pole based
                        const float alpha = 0.005f;
                        spectralTiltLowDL_ += alpha * (sD_L - spectralTiltLowDL_);
                        spectralTiltLowDR_ += alpha * (sD_R - spectralTiltLowDR_);
                        const float lowL = spectralTiltLowDL_;
                        const float lowR = spectralTiltLowDR_;
                        const float highL = sD_L - lowL;
                        const float highR = sD_R - lowR;
                        const float lowGain  = 1.0f - spectralAmtD_;
                        const float highGain = 1.0f + spectralAmtD_ * 2.0f;
                        sD_L = lowL * lowGain + highL * highGain;
                        sD_R = lowR * lowGain + highR * highGain;
                    }
                    else if (spectralTypeD_ == 9)
                    {
                        // Vibrato — short modulated delay creates pitch wobble
                        const double modHz = 1.0 + (double) spectralAmtD_ * 8.0;
                        const double inc   = modHz / sampleRate_;
                        spectralVibPhaseD_ += inc;
                        if (spectralVibPhaseD_ >= 1.0) spectralVibPhaseD_ -= 1.0;
                        const float lfo = static_cast<float> (std::sin (6.2831853071795865 * spectralVibPhaseD_));
                        const float depthSamples = spectralAmtD_ * 20.0f;
                        const float delaySamples = (float) (kSpectralVibSize - 4) * 0.5f + lfo * depthSamples;
                        const int   intDel       = juce::jlimit (1, kSpectralVibSize - 2, (int) delaySamples);
                        const int   readIdx      = (spectralVibWriteD_ - intDel + kSpectralVibSize) % kSpectralVibSize;
                        const float dryL = sD_L, dryR = sD_R;
                        sD_L = dryL * (1.0f - spectralAmtD_) + spectralVibDL_[(size_t) readIdx] * spectralAmtD_;
                        sD_R = dryR * (1.0f - spectralAmtD_) + spectralVibDR_[(size_t) readIdx] * spectralAmtD_;
                        spectralVibDL_[(size_t) spectralVibWriteD_] = dryL;
                        spectralVibDR_[(size_t) spectralVibWriteD_] = dryR;
                        spectralVibWriteD_ = (spectralVibWriteD_ + 1) % kSpectralVibSize;
                    }
                }

                const float env    = eAmpVca[i];               // ENV1 AMP, from the pre-pass
                float ampMod = 0.0f;                           // ENV2–5 routed to Amp (bipolar gain)
                for (int k = 0; k < 4; ++k)
                    if (envDest_[k + 1] == kEnvAmp) ampMod += envDepth_[k + 1] * eAmpFree[k][i];
                const float velEnv = juce::jmax (0.0f, env * (1.0f + ampMod));   // fb262 — GLOBAL VELOCITY REMOVED: amp no longer scales by note velocity (it was globally quieting soft notes). Velocity is now a pure routable mod source — drop it on Volume for dynamics.

                // ── OSC SCOPE tap — per-osc, PRE-SUM, PRE-FILTER, pre-level/pan/VCA.
                // Write each oscillator's raw mono signal so the live oscilloscope
                // shows the actual waveform SHAPE (saw/triangle/warped) at a stable
                // amplitude regardless of that osc's volume/pan/envelope; unison
                // detune BEATING is already summed into sX_L/sX_R, so it stays
                // visible. Plain float writes only — audio thread, no atomics/alloc.
                {
                    const int wp = scopeRingPos_;
                    scopeRing_[0][wp] = 0.5f * (sA_L + sA_R);
                    scopeRing_[1][wp] = 0.5f * (sB_L + sB_R);
                    scopeRing_[2][wp] = 0.5f * (sC_L + sC_R);
                    scopeRing_[3][wp] = 0.5f * (sD_L + sD_R);
                    scopeRingPos_ = (wp + 1) & kScopeRingMask;
                }

                // SOLO/MUTE — advance the per-osc click-free gates one sample (one-pole toward target)
                for (int g = 0; g < 4; ++g) oscGate_[g] += (robinGate (g) - oscGate_[g]) * oscGateCoef_;
                const float gA = oscGate_[0], gB = oscGate_[1], gC = oscGate_[2], gD = oscGate_[3];

                // fb180 — LEVELS GLIDE (2.5ms one-pole, the slew law): fb178 made LevelA-D
                // live mod dests, so a plucking envelope stepped the gain at block rate —
                // audible crackle. Same pattern as the mute gates one line up.
                // fb183 — OWNERSHIP CROSSFADE: eff = (1−Σd)·knob + Σ(d·env), per voice.
                const float _loA = juce::jmin (1.0f, envLvlOwn_[0]), _loB = juce::jmin (1.0f, envLvlOwn_[1]);
                const float _loC = juce::jmin (1.0f, envLvlOwn_[2]), _loD = juce::jmin (1.0f, envLvlOwn_[3]);
                lvlSmA_ += (juce::jlimit (0.0f, 1.0f, level_  * (1.0f - _loA) + envLvlDrive_[0]) - lvlSmA_) * lvlSmCoef_;
                lvlSmB_ += (juce::jlimit (0.0f, 1.0f, levelB_ * (1.0f - _loB) + envLvlDrive_[1]) - lvlSmB_) * lvlSmCoef_;
                lvlSmC_ += (juce::jlimit (0.0f, 1.0f, levelC_ * (1.0f - _loC) + envLvlDrive_[2]) - lvlSmC_) * lvlSmCoef_;
                lvlSmD_ += (juce::jlimit (0.0f, 1.0f, levelD_ * (1.0f - _loD) + envLvlDrive_[3]) - lvlSmD_) * lvlSmCoef_;

                // fb202 — PAN GLIDE (Max: "no static"): the pan gains were still stepping at
                // block rate while the levels beside them glided (fb180) — an LFO/env on any
                // Pan crackled a sustained tone. Same one-pole, same 2.5ms coefficient.
                panL_  += (panLT_  - panL_)  * lvlSmCoef_;  panR_  += (panRT_  - panR_)  * lvlSmCoef_;
                panLB_ += (panLBT_ - panLB_) * lvlSmCoef_;  panRB_ += (panRBT_ - panRB_) * lvlSmCoef_;
                panLC_ += (panLCT_ - panLC_) * lvlSmCoef_;  panRC_ += (panRCT_ - panRC_) * lvlSmCoef_;
                panLD_ += (panLDT_ - panLD_) * lvlSmCoef_;  panRD_ += (panRDT_ - panRD_) * lvlSmCoef_;

                // Sum to stereo with INDEPENDENT per-osc level + pan (× solo/mute gate), split
                // into the 3 filter-routing buses. Each osc's full signal = osc-only (sX-subMono)
                // + its sub (subMono); routed by busCo*_ (F1 bus = scratch, F2 = fltBus2_, dry =
                // fltDry_). Default (all sources → F1) makes scratch = the old full mix exactly.
                const float gAL = lvlSmA_ * panL_  * gA * velEnv, gAR = lvlSmA_ * panR_  * gA * velEnv;   // fb180 — glided
                const float gBL = lvlSmB_ * panLB_ * gB * velEnv, gBR = lvlSmB_ * panRB_ * gB * velEnv;
                const float gCL = lvlSmC_ * panLC_ * gC * velEnv, gCR = lvlSmC_ * panRC_ * gC * velEnv;
                const float gDL = lvlSmD_ * panLD_ * gD * velEnv, gDR = lvlSmD_ * panRD_ * gD * velEnv;
                const float oAL = (sA_L - subMono0) * gAL, oAR = (sA_R - subMono0) * gAR;   // osc-only (sub removed)
                const float oBL = (sB_L - subMono1) * gBL, oBR = (sB_R - subMono1) * gBR;
                const float oCL = (sC_L - subMono2) * gCL, oCR = (sC_R - subMono2) * gCR;
                const float oDL = (sD_L - subMono3) * gDL, oDR = (sD_R - subMono3) * gDR;
                const float subBL = subMono0 * gAL + subMono1 * gBL + subMono2 * gCL + subMono3 * gDL;   // Sub source (idx 4)
                const float subBR = subMono0 * gAR + subMono1 * gBR + subMono2 * gCR + subMono3 * gDR;
                scratchL[i] = busCo1_[0]*oAL + busCo1_[1]*oBL + busCo1_[2]*oCL + busCo1_[3]*oDL + busCo1_[4]*subBL;
                scratchR[i] = busCo1_[0]*oAR + busCo1_[1]*oBR + busCo1_[2]*oCR + busCo1_[3]*oDR + busCo1_[4]*subBR;
                // NOISE ENGINE — compute the contribution once, then ROUTE it into F1/F2/dry per the N pill (fb63).
                float noiseAddL = 0.0f, noiseAddR = 0.0f;
                if (noiseOn_ || noiseForce_)   // fb64 — also generate when noise is a BLEND SOURCE (even if its own output is off)
                {
                    // fb202 — noise Level/Scan/Pan/Width glide (2.5ms): mod pushes step at block
                    // rate; the noise itself masks small steps but blends/routes downstream don't.
                    noiseLevel_    += (noiseLvlT_      - noiseLevel_)    * lvlSmCoef_;
                    noiseScanRate_ += (noiseScanRateT_ - noiseScanRate_) * lvlSmCoef_;
                    noisePanL_     += (noisePanLT_     - noisePanL_)     * lvlSmCoef_;
                    noisePanR_     += (noisePanRT_     - noisePanR_)     * lvlSmCoef_;
                    noiseWidth_    += (noiseWidthT_    - noiseWidth_)    * lvlSmCoef_;
                    float _nL, _nR;
                    if (noiseSampLen_ > 1 && noiseSampL_ != nullptr)
                    {
                        // NOISE IMPORT (P5) — a loaded sample plays as a looping texture (Scan = speed), riding the amp
                        // env, seam pre-crossfaded at load (click-free). fb66 PLAY MODE: Random/Free loop; Envelope is a
                        // one-shot (plays through once, then silent until the next note re-arms it via startNote).
                        if (noisePlayMode_ == 1 && noiseOneShotDone_)
                        {
                            _nL = 0.0f; _nR = 0.0f;   // Envelope — one-shot finished
                        }
                        else
                        {
                            const int i0 = (int) noiseSampPos_; int i1 = i0 + 1; if (i1 >= noiseSampLen_) i1 = 0;
                            const float fr = (float) (noiseSampPos_ - (double) i0);
                            _nL = noiseSampL_[i0] + (noiseSampL_[i1] - noiseSampL_[i0]) * fr;
                            _nR = noiseSampR_[i0] + (noiseSampR_[i1] - noiseSampR_[i0]) * fr;
                            if (noisePlayMode_ == 1)   // fb70 — Envelope one-shot fades out over its last ~60 ms instead of hard-cutting ("shooting the birds")
                            {
                                const double toEnd = (double) (noiseSampLen_ - 1) - noiseSampPos_;
                                const double fadeLen = 0.060 * (double) noiseSR_;
                                if (toEnd < fadeLen) { const float g = (float) juce::jlimit (0.0, 1.0, toEnd / fadeLen); _nL *= g; _nR *= g; }
                            }
                            noiseSampPos_ += (double) noiseScanRate_ * noiseSampNativeOverOut_;
                            if (noisePlayMode_ == 1)   // Envelope — one-shot: stop at the end, no wrap
                            {
                                if (noiseSampPos_ >= (double) (noiseSampLen_ - 1)) { noiseSampPos_ = (double) (noiseSampLen_ - 1); noiseOneShotDone_ = true; }
                            }
                            else                        // Random / Free — loop
                            {
                                while (noiseSampPos_ >= (double) noiseSampLen_) noiseSampPos_ -= (double) noiseSampLen_;
                            }
                        }
                    }
                    else
                    {
                        // SCAN — advance a phase at noiseScanRate_ (0.1×…2×); regenerate the noise only on wrap and
                        // interpolate between held samples. 0.5 knob = 1× (normal). Algorithmic types (colors/tape/vinyl/space).
                        scanPh_ += noiseScanRate_;
                        while (scanPh_ >= 1.0f) { scanPh_ -= 1.0f; nPrevL_ = nCurL_; nPrevR_ = nCurR_; noiseTick (nCurL_, nCurR_); }
                        const float _t = scanPh_;
                        _nL = nPrevL_ + (nCurL_ - nPrevL_) * _t;
                        _nR = nPrevR_ + (nCurR_ - nPrevR_) * _t;
                    }
                    noiseModTap_ = juce::jlimit (-4.0f, 4.0f, 0.5f * (_nL + _nR));   // fb64 — blend modulator tap (pre-gain raw noise, 1-sample delayed like modPrev_) — stays per-voice (not gated by the mono carrier)
                    noiseCarrierGain_ += (noiseCarrierTarget_ - noiseCarrierGain_) * 0.01f;   // fb68 — ~10 ms smoothing → click-free mono carrier hand-offs (no-op in poly modes, target = 1)
                    if (noiseOn_)   // audible bus contribution only when the noise engine is actually on
                    {
                        const float _ng = noiseLevel_ * velEnv * noiseCarrierGain_;   // fb68 — × carrier gain (Free = mono; poly = 1)
                        const float aL = _nL * _ng * noisePanL_, aR = _nR * _ng * noisePanR_;
                        // fb69 — STEREO WIDTH via mid/side: 0 = mono (side→0), 1 = normal (identity), 2 = wide (side×2).
                        const float mid = 0.5f * (aL + aR), side = 0.5f * (aL - aR) * noiseWidth_;
                        noiseAddL = mid + side;
                        noiseAddR = mid - side;
                    }
                }
                scratchL[i] += noiseAddL * noiseCo1_;   scratchR[i] += noiseAddR * noiseCo1_;   // → Filter 1 bus
                busB2L[i]   = busCo2_[0]*oAL + busCo2_[1]*oBL + busCo2_[2]*oCL + busCo2_[3]*oDL + busCo2_[4]*subBL + noiseAddL * noiseCo2_;
                busB2R[i]   = busCo2_[0]*oAR + busCo2_[1]*oBR + busCo2_[2]*oCR + busCo2_[3]*oDR + busCo2_[4]*subBR + noiseAddR * noiseCo2_;
                busDryL[i]  = busCoD_[0]*oAL + busCoD_[1]*oBL + busCoD_[2]*oCL + busCoD_[3]*oDL + busCoD_[4]*subBL + noiseAddL * noiseCoD_;
                busDryR[i]  = busCoD_[0]*oAR + busCoD_[1]*oBR + busCoD_[2]*oCR + busCoD_[3]*oDR + busCoD_[4]*subBR + noiseAddR * noiseCoD_;
                // fb287 — PER-OSC REVERB SEND (no-bleed, POST-FILTER): build the send BUSES here (the routed
                // oscillators only, split by the SAME per-osc filter routing busCo1/2/D as the audible path),
                // then run them through the dedicated send-filters in the filter loop below → the reverb hears
                // the FILTERED routed oscs (fb280 tapped PRE-filter → filtered oscs sent a dry "blind" signal).
                // Unrouted oscs contribute exactly zero; filter routing per osc is preserved (a bypass-routed
                // osc lands in the send-dry bus and passes unfiltered, matching its audible path).
                if (sendActive)
                {
                    const float rAL = rvbG_[0]*oAL, rBL = rvbG_[1]*oBL, rCL = rvbG_[2]*oCL, rDL = rvbG_[3]*oDL, rSL = rvbG_[4]*subBL, rNL = rvbG_[5]*noiseAddL;
                    const float rAR = rvbG_[0]*oAR, rBR = rvbG_[1]*oBR, rCR = rvbG_[2]*oCR, rDR = rvbG_[3]*oDR, rSR = rvbG_[4]*subBR, rNR = rvbG_[5]*noiseAddR;
                    sF1L[i]  = busCo1_[0]*rAL + busCo1_[1]*rBL + busCo1_[2]*rCL + busCo1_[3]*rDL + busCo1_[4]*rSL + rNL*noiseCo1_;
                    sF1R[i]  = busCo1_[0]*rAR + busCo1_[1]*rBR + busCo1_[2]*rCR + busCo1_[3]*rDR + busCo1_[4]*rSR + rNR*noiseCo1_;
                    sF2L[i]  = busCo2_[0]*rAL + busCo2_[1]*rBL + busCo2_[2]*rCL + busCo2_[3]*rDL + busCo2_[4]*rSL + rNL*noiseCo2_;
                    sF2R[i]  = busCo2_[0]*rAR + busCo2_[1]*rBR + busCo2_[2]*rCR + busCo2_[3]*rDR + busCo2_[4]*rSR + rNR*noiseCo2_;
                    sDryL[i] = busCoD_[0]*rAL + busCoD_[1]*rBL + busCoD_[2]*rCL + busCoD_[3]*rDL + busCoD_[4]*rSL + rNL*noiseCoD_;
                    sDryR[i] = busCoD_[0]*rAR + busCoD_[1]*rBR + busCoD_[2]*rCR + busCoD_[3]*rDR + busCoD_[4]*rSR + rNR*noiseCoD_;
                }
                if (dlySendActive)   // fb296 — same per-osc filter split, gated by the DELAY's independent route mask
                {
                    const float rAL = dlyG_[0]*oAL, rBL = dlyG_[1]*oBL, rCL = dlyG_[2]*oCL, rDL = dlyG_[3]*oDL, rSL = dlyG_[4]*subBL, rNL = dlyG_[5]*noiseAddL;
                    const float rAR = dlyG_[0]*oAR, rBR = dlyG_[1]*oBR, rCR = dlyG_[2]*oCR, rDR = dlyG_[3]*oDR, rSR = dlyG_[4]*subBR, rNR = dlyG_[5]*noiseAddR;
                    dF1L[i]  = busCo1_[0]*rAL + busCo1_[1]*rBL + busCo1_[2]*rCL + busCo1_[3]*rDL + busCo1_[4]*rSL + rNL*noiseCo1_;
                    dF1R[i]  = busCo1_[0]*rAR + busCo1_[1]*rBR + busCo1_[2]*rCR + busCo1_[3]*rDR + busCo1_[4]*rSR + rNR*noiseCo1_;
                    dF2L[i]  = busCo2_[0]*rAL + busCo2_[1]*rBL + busCo2_[2]*rCL + busCo2_[3]*rDL + busCo2_[4]*rSL + rNL*noiseCo2_;
                    dF2R[i]  = busCo2_[0]*rAR + busCo2_[1]*rBR + busCo2_[2]*rCR + busCo2_[3]*rDR + busCo2_[4]*rSR + rNR*noiseCo2_;
                    dDryL[i] = busCoD_[0]*rAL + busCoD_[1]*rBL + busCoD_[2]*rCL + busCoD_[3]*rDL + busCoD_[4]*rSL + rNL*noiseCoD_;
                    dDryR[i] = busCoD_[0]*rAR + busCoD_[1]*rBR + busCoD_[2]*rCR + busCoD_[3]*rDR + busCoD_[4]*rSR + rNR*noiseCoD_;
                }
                if (dstSendActive)   // fb338 — same per-osc filter split, gated by the DISTORTION's independent route mask
                {
                    const float rAL = dstG_[0]*oAL, rBL = dstG_[1]*oBL, rCL = dstG_[2]*oCL, rDL = dstG_[3]*oDL, rSL = dstG_[4]*subBL, rNL = dstG_[5]*noiseAddL;
                    const float rAR = dstG_[0]*oAR, rBR = dstG_[1]*oBR, rCR = dstG_[2]*oCR, rDR = dstG_[3]*oDR, rSR = dstG_[4]*subBR, rNR = dstG_[5]*noiseAddR;
                    tF1L[i]  = busCo1_[0]*rAL + busCo1_[1]*rBL + busCo1_[2]*rCL + busCo1_[3]*rDL + busCo1_[4]*rSL + rNL*noiseCo1_;
                    tF1R[i]  = busCo1_[0]*rAR + busCo1_[1]*rBR + busCo1_[2]*rCR + busCo1_[3]*rDR + busCo1_[4]*rSR + rNR*noiseCo1_;
                    tF2L[i]  = busCo2_[0]*rAL + busCo2_[1]*rBL + busCo2_[2]*rCL + busCo2_[3]*rDL + busCo2_[4]*rSL + rNL*noiseCo2_;
                    tF2R[i]  = busCo2_[0]*rAR + busCo2_[1]*rBR + busCo2_[2]*rCR + busCo2_[3]*rDR + busCo2_[4]*rSR + rNR*noiseCo2_;
                    tDryL[i] = busCoD_[0]*rAL + busCoD_[1]*rBL + busCoD_[2]*rCL + busCoD_[3]*rDL + busCoD_[4]*rSL + rNL*noiseCoD_;
                    tDryR[i] = busCoD_[0]*rAR + busCoD_[1]*rBR + busCoD_[2]*rCR + busCoD_[3]*rDR + busCoD_[4]*rSR + rNR*noiseCoD_;
                }
                // fb347 — THE SHARED EXCLUSION BUS: identical split, but the mask is the UNION of every
                // device's routes, so an osc routed to three devices lands here exactly ONCE. This is the
                // signal the main-send devices subtract; summing the per-device buses instead (as before)
                // subtracted a shared osc N times and handed the next device that osc INVERTED.
                if (exSendActive)
                {
                    const float rAL = exG_[0]*oAL, rBL = exG_[1]*oBL, rCL = exG_[2]*oCL, rDL = exG_[3]*oDL, rSL = exG_[4]*subBL, rNL = exG_[5]*noiseAddL;
                    const float rAR = exG_[0]*oAR, rBR = exG_[1]*oBR, rCR = exG_[2]*oCR, rDR = exG_[3]*oDR, rSR = exG_[4]*subBR, rNR = exG_[5]*noiseAddR;
                    xF1L[i]  = busCo1_[0]*rAL + busCo1_[1]*rBL + busCo1_[2]*rCL + busCo1_[3]*rDL + busCo1_[4]*rSL + rNL*noiseCo1_;
                    xF1R[i]  = busCo1_[0]*rAR + busCo1_[1]*rBR + busCo1_[2]*rCR + busCo1_[3]*rDR + busCo1_[4]*rSR + rNR*noiseCo1_;
                    xF2L[i]  = busCo2_[0]*rAL + busCo2_[1]*rBL + busCo2_[2]*rCL + busCo2_[3]*rDL + busCo2_[4]*rSL + rNL*noiseCo2_;
                    xF2R[i]  = busCo2_[0]*rAR + busCo2_[1]*rBR + busCo2_[2]*rCR + busCo2_[3]*rDR + busCo2_[4]*rSR + rNR*noiseCo2_;
                    xDryL[i] = busCoD_[0]*rAL + busCoD_[1]*rBL + busCoD_[2]*rCL + busCoD_[3]*rDL + busCoD_[4]*rSL + rNL*noiseCoD_;
                    xDryR[i] = busCoD_[0]*rAR + busCoD_[1]*rBR + busCoD_[2]*rCR + busCoD_[3]*rDR + busCoD_[4]*rSR + rNR*noiseCoD_;
                }
                // fb348 — pooled instance sends: identical per-osc split, each gated by ITS OWN mask.
                // This is what makes "delay on C" untouchable by "delay on A".
                for (int ps = 0; ps < kPoolSends; ++ps)
                {
                    if (! poolOn[ps]) continue;
                    auto& P = poolSend_[ps];
                    float* pF1L = P.f1.getWritePointer (0);  float* pF1R = P.f1.getWritePointer (1);
                    float* pF2L = P.f2.getWritePointer (0);  float* pF2R = P.f2.getWritePointer (1);
                    float* pDL  = P.dry.getWritePointer (0); float* pDR  = P.dry.getWritePointer (1);
                    const float rAL = P.g[0]*oAL, rBL = P.g[1]*oBL, rCL = P.g[2]*oCL, rDL = P.g[3]*oDL, rSL = P.g[4]*subBL, rNL = P.g[5]*noiseAddL;
                    const float rAR = P.g[0]*oAR, rBR = P.g[1]*oBR, rCR = P.g[2]*oCR, rDR = P.g[3]*oDR, rSR = P.g[4]*subBR, rNR = P.g[5]*noiseAddR;
                    pF1L[i] = busCo1_[0]*rAL + busCo1_[1]*rBL + busCo1_[2]*rCL + busCo1_[3]*rDL + busCo1_[4]*rSL + rNL*noiseCo1_;
                    pF1R[i] = busCo1_[0]*rAR + busCo1_[1]*rBR + busCo1_[2]*rCR + busCo1_[3]*rDR + busCo1_[4]*rSR + rNR*noiseCo1_;
                    pF2L[i] = busCo2_[0]*rAL + busCo2_[1]*rBL + busCo2_[2]*rCL + busCo2_[3]*rDL + busCo2_[4]*rSL + rNL*noiseCo2_;
                    pF2R[i] = busCo2_[0]*rAR + busCo2_[1]*rBR + busCo2_[2]*rCR + busCo2_[3]*rDR + busCo2_[4]*rSR + rNR*noiseCo2_;
                    pDL[i]  = busCoD_[0]*rAL + busCoD_[1]*rBL + busCoD_[2]*rCL + busCoD_[3]*rDL + busCoD_[4]*rSL + rNL*noiseCoD_;
                    pDR[i]  = busCoD_[0]*rAR + busCoD_[1]*rBR + busCoD_[2]*rCR + busCoD_[3]*rDR + busCoD_[4]*rSR + rNR*noiseCoD_;
                }
                // BLEND MODES: capture each osc's PRE-GAIN sample as the modulator tap (1-sample delay for
                // next iteration). These are pre level/pan/gate → a source at LEVEL 0 still modulates.
                // fb523 — × monoTapCorr_: the tap is now PRE-PAN in effect (see setUnisonImpl).
                //  ⚠️ The correction applies ONLY to the engines that actually pass through the
                //  unison pan tables (WT and FM). SAMP/GRAN/SPEC/HARM/MODAL overwrite sX_L/sX_R
                //  straight from their block buffers and never see cos/sin at all, so 0.5·(L+R)
                //  is already the right mono downmix for them — applying √2 there would be a
                //  spurious +3 dB. The mcOn* flags are block constants (see uLoop* above).
                modPrev_[0] = (mcOnA ? monoTapCorr_[0] : 1.0f) * 0.5f * (sA_L + sA_R);
                modPrev_[1] = (mcOnB ? monoTapCorr_[1] : 1.0f) * 0.5f * (sB_L + sB_R);
                modPrev_[2] = (mcOnC ? monoTapCorr_[2] : 1.0f) * 0.5f * (sC_L + sC_R);
                modPrev_[3] = (mcOnD ? monoTapCorr_[3] : 1.0f) * 0.5f * (sD_L + sD_R);
                for (int mc = 0; mc < 4; ++mc) modPrev_[mc] = juce::jlimit (-4.f, 4.f, modPrev_[mc]);
                // fb552 — THE FOLLOWERS, on the taps that were just written. Instant attack, one-pole
                //  release: see kFollowReleaseMs for why it is a peak detector and not a mean one.
                if (anyFollowArmed_)
                {
                    for (int fk = 0; fk < 4; ++fk)
                    {
                        const float a = std::fabs (modPrev_[fk]);
                        follow_[fk] = (a > follow_[fk]) ? a : follow_[fk] + (a - follow_[fk]) * followRelCoef_;
                    }
                    const float an = std::fabs (noiseModTap_);
                    follow_[4] = (an > follow_[4]) ? an : follow_[4] + (an - follow_[4]) * followRelCoef_;
                }
            }

            // fb122 ROBIN Pan — the station leans (unity at center, applied to every bus)
            if (robinAmpL_ != 1.0f || robinAmpR_ != 1.0f)
                for (int i = 0; i < numSamples; ++i)
                {
                    scratchL[i] *= robinAmpL_;  scratchR[i] *= robinAmpR_;
                    busB2L[i]   *= robinAmpL_;  busB2R[i]   *= robinAmpR_;
                    busDryL[i]  *= robinAmpL_;  busDryR[i]  *= robinAmpR_;
                    if (sendActive) { sF1L[i]*=robinAmpL_; sF1R[i]*=robinAmpR_; sF2L[i]*=robinAmpL_; sF2R[i]*=robinAmpR_; sDryL[i]*=robinAmpL_; sDryR[i]*=robinAmpR_; }   // fb287 — send matches
                    if (dlySendActive) { dF1L[i]*=robinAmpL_; dF1R[i]*=robinAmpR_; dF2L[i]*=robinAmpL_; dF2R[i]*=robinAmpR_; dDryL[i]*=robinAmpL_; dDryR[i]*=robinAmpR_; }   // fb296 — delay send matches
                    if (dstSendActive) { tF1L[i]*=robinAmpL_; tF1R[i]*=robinAmpR_; tF2L[i]*=robinAmpL_; tF2R[i]*=robinAmpR_; tDryL[i]*=robinAmpL_; tDryR[i]*=robinAmpR_; }   // fb338 — distortion send matches
                    if (exSendActive)  { xF1L[i]*=robinAmpL_; xF1R[i]*=robinAmpR_; xF2L[i]*=robinAmpL_; xF2R[i]*=robinAmpR_; xDryL[i]*=robinAmpL_; xDryR[i]*=robinAmpR_; }   // fb347 — the exclusion MUST track the sends exactly, or the subtraction stops cancelling
                    for (int ps = 0; ps < kPoolSends; ++ps) if (poolOn[ps]) { auto& P = poolSend_[ps];
                        P.f1.getWritePointer(0)[i]*=robinAmpL_; P.f1.getWritePointer(1)[i]*=robinAmpR_;
                        P.f2.getWritePointer(0)[i]*=robinAmpL_; P.f2.getWritePointer(1)[i]*=robinAmpR_;
                        P.dry.getWritePointer(0)[i]*=robinAmpL_; P.dry.getWritePointer(1)[i]*=robinAmpR_; }   // fb348
                }

            // Phase 8a polish — apply steal-fade and decide if voice should die
            if (stealing_)
            {
                for (int i = 0; i < numSamples; ++i)
                {
                    const float sf = stealingFade_;
                    scratchL[i] *= sf;  scratchR[i] *= sf;
                    busB2L[i]   *= sf;  busB2R[i]   *= sf;   // fade the routing buses too
                    busDryL[i]  *= sf;  busDryR[i]  *= sf;
                    if (sendActive) { sF1L[i]*=sf; sF1R[i]*=sf; sF2L[i]*=sf; sF2R[i]*=sf; sDryL[i]*=sf; sDryR[i]*=sf; }   // fb287 — send fades too
                    if (dlySendActive) { dF1L[i]*=sf; dF1R[i]*=sf; dF2L[i]*=sf; dF2R[i]*=sf; dDryL[i]*=sf; dDryR[i]*=sf; }   // fb296 — delay send fades too
                    if (dstSendActive) { tF1L[i]*=sf; tF1R[i]*=sf; tF2L[i]*=sf; tF2R[i]*=sf; tDryL[i]*=sf; tDryR[i]*=sf; }   // fb338 — distortion send fades too
                    if (exSendActive)  { xF1L[i]*=sf; xF1R[i]*=sf; xF2L[i]*=sf; xF2R[i]*=sf; xDryL[i]*=sf; xDryR[i]*=sf; }   // fb347 — exclusion fades identically
                    for (int ps = 0; ps < kPoolSends; ++ps) if (poolOn[ps]) { auto& P = poolSend_[ps];
                        P.f1.getWritePointer(0)[i]*=sf; P.f1.getWritePointer(1)[i]*=sf;
                        P.f2.getWritePointer(0)[i]*=sf; P.f2.getWritePointer(1)[i]*=sf;
                        P.dry.getWritePointer(0)[i]*=sf; P.dry.getWritePointer(1)[i]*=sf; }   // fb348
                    stealingFade_ *= stealingFadeStep_;
                }
                if (stealingFade_ < 0.001f)
                {
                    // Fade complete — clear and exit early; existing post-fade code will write
                    // mostly-silence to the output. Mark playing_ = false so the next block
                    // skips this voice entirely.
                    stealing_   = false;
                    playing_    = false;
                    ampEnv_.reset();
                    fltEnvT_.reset(); pitchEnvT_.reset(); mod1EnvT_.reset(); mod2EnvT_.reset();
                    // We still let the filter process this block's tiny tail so the filter
                    // state settles — don't return early.
                    clearCurrentNote();
                }
            }

            // Batch 1 Filter — per-sample FilterSlot processing with cutoff
            // modulated by FLT envelope (bipolar amount) + per-voice EROSION
            // drift, summed in semitone space and converted to Hz at the end
            // (so low cutoffs barely move and high cutoffs swing wide stays
            // musical, per report §6).
            {
                const double sr = sampleRate_;
                const float  baseCutSemis = hzToSemi (baseCutHz_);
                // EROSION scale: pow(e, 1.8) per prompt §5 / report §5. The
                // resulting random walk drifts within ±~6 ST at max erosion.
                const float  driftDepthSemis = std::pow (fltErosionAmount_, 1.8f) * 6.0f;
                const bool   driftActive     = fltErosionAmount_ > 0.001f;
                // Slight resonance wander at high erosion (§5 of prompt).
                const float  resWander = (fltErosionAmount_ > 0.7f)
                                            ? (fltErosionAmount_ - 0.7f) * 0.20f
                                            : 0.0f;

                float* sL = scratch_.getWritePointer (0);
                float* sR = scratch_.getWritePointer (1);
                const bool oversample = filterSlot_.needsOversampling()
                                     || filterSlot2_.needsOversampling();
                // Coefficient sample rate doubles when oversampling so the
                // filter's prewarp + ZDF math sees the upsampled Nyquist.
                const double coefSr = oversample ? sr * 2.0 : sr;
                const float  baseCutSemis2 = hzToSemi (baseCutHz2_);
                // Key-track: constant per held note (currentMidiNote_ default 60).
                // amount·(note−60) semitones of cutoff offset, added below.
                const float ktCutSemis1 = filterKeytrack1_ * ((float) currentMidiNote_ - 60.0f);
                const float ktCutSemis2 = filterKeytrack2_ * ((float) currentMidiNote_ - 60.0f);
                const int    kNoneType = (int) tw::filters::Type::NONE;
                // Free-envelope per-sample values (ch1..4 = envs 2–5) for filter routing.
                const float* eFltFree[4] = { envScratch_.getReadPointer (1), envScratch_.getReadPointer (2),
                                             envScratch_.getReadPointer (3), envScratch_.getReadPointer (4) };
                // CPU: only LFOs consumed PER SAMPLE in this loop need ticking — the sources of
                // enabled Cut1/Cut2 routes and both ends of any LfoAmt chain, plus L1 (the editor
                // viz dot). Every other LFO advances its phase ONCE per block (skipSamples below)
                // so the per-block mod matrix's peek() stays correct. With nothing routed this
                // drops 10 sin() calls per sample per voice to 1.
                unsigned lfoTickMask = 1u;   // L1 always (viz dot)
                bool anyCutRoute = false, anyAmtRoute = false;
                for (int a = 0; a < modConfig_.numAssignments; ++a)
                {
                    const auto& as = modConfig_.assignments[a];
                    if (! as.enabled) continue;
                    const int sI = (int) as.source, dI = (int) as.dest;
                    if (sI < 0 || sI >= wc::NUM_LFOS) continue;
                    if (as.dest == wc::ModDest::Cut1 || as.dest == wc::ModDest::Cut2)
                    { lfoTickMask |= (1u << sI); anyCutRoute = true; }
                    else if (dI >= (int) wc::ModDest::LfoAmt1 && dI < (int) wc::ModDest::LfoAmt1 + wc::NUM_LFOS)
                    { lfoTickMask |= (1u << sI) | (1u << (dI - (int) wc::ModDest::LfoAmt1)); anyAmtRoute = true; }
                }
                for (int i = 0; i < numSamples; ++i)
                {
                    // fb204 — FILTER-LANE GLIDE (2.5ms, fb180 law): every block-pushed value this
                    // loop consumes steps at block rate when modulated — res, mix, vel, keytrack,
                    // post-drive, and the env→cutoff latches all crackled under LFO/env routes.
                    envCutSm1_ += (envCutBlk1_ - envCutSm1_) * lvlSmCoef_;
                    envCutSm2_ += (envCutBlk2_ - envCutSm2_) * lvlSmCoef_;
                    resSm1_    += (baseRes01_  - resSm1_)    * lvlSmCoef_;
                    resSm2_    += (baseRes012_ - resSm2_)    * lvlSmCoef_;
                    mixSm1_    += (filterMix1_ - mixSm1_)    * lvlSmCoef_;
                    mixSm2_    += (filterMix2_ - mixSm2_)    * lvlSmCoef_;
                    velSm1_    += (velAmt1_    - velSm1_)    * lvlSmCoef_;
                    velSm2_    += (velAmt2_    - velSm2_)    * lvlSmCoef_;
                    ktSm1_     += (ktCutSemis1 - ktSm1_)     * lvlSmCoef_;
                    ktSm2_     += (ktCutSemis2 - ktSm2_)     * lvlSmCoef_;
                    pdrvSm1_   += (postDrv1_   - pdrvSm1_)   * lvlSmCoef_;
                    pdrvSm2_   += (postDrv2_   - pdrvSm2_)   * lvlSmCoef_;
                    drvSm1_    += (drv01_      - drvSm1_)    * lvlSmCoef_;
                    drvSm2_    += (drv012_     - drvSm2_)    * lvlSmCoef_;
                    // ── Batch 1 — per-voice LFO tick + route accumulation ──
                    // Tick the NEEDED LFOs once per output sample (free/synced Hz already
                    // resolved in setModConfig), then sum any enabled LFO→cutoff routes
                    // in semitone space. Other destinations (frame/warp/pitch/level…)
                    // are per-block via peek() — their LFOs are skip-advanced after the loop.
                    float lfoOut_[wc::NUM_LFOS];
                    for (int L = 0; L < wc::NUM_LFOS; ++L)
                        lfoOut_[L] = (lfoTickMask & (1u << L)) ? synthLfo_[L].processSample() : 0.0f;
                    lfoVisValue_ = lfoOut_[0];                 // L1 → editor viz dot
                    // LFO→LFO amt scales each source before it routes (per-sample).
                    if (anyAmtRoute)
                    {
                        float amt[wc::NUM_LFOS] = { 0.0f };
                        for (int a = 0; a < modConfig_.numAssignments; ++a)
                        {
                            const auto& as = modConfig_.assignments[a];
                            if (! as.enabled) continue;
                            const int sI = (int) as.source, dI = (int) as.dest;
                            float sv3;
                            if      (sI >= 0 && sI < wc::NUM_LFOS) sv3 = lfoOut_[sI];
                            else if (wc::isEnvModSource (sI))      sv3 = envSourceValue (sI);   // fb178
                            else continue;
                            if (dI >= (int) wc::ModDest::LfoAmt1 && dI < (int) wc::ModDest::LfoAmt1 + wc::NUM_LFOS)
                                amt[dI - (int) wc::ModDest::LfoAmt1] += sv3 * as.depth;
                        }
                        for (int L = 0; L < wc::NUM_LFOS; ++L) lfoOut_[L] *= juce::jlimit (0.0f, 2.0f, 1.0f + amt[L]);
                    }
                    float lfoSemis1 = 0.0f, lfoSemis2 = 0.0f;
                    if (anyCutRoute)
                        for (int a = 0; a < modConfig_.numAssignments; ++a)
                        {
                            const auto& as = modConfig_.assignments[a];
                            if (! as.enabled) continue;
                            const int sIdx = (int) as.source;
                            if (sIdx < 0 || sIdx >= wc::NUM_LFOS) continue;   // Batch 1: LFO sources only
                            const wc::DestInfo& info = wc::kDestInfo[(int) as.dest];
                            const float contrib = wc::routeContribution (info, lfoOut_[sIdx], as.depth);
                            if      (as.dest == wc::ModDest::Cut1) lfoSemis1 += contrib;
                            else if (as.dest == wc::ModDest::Cut2) lfoSemis2 += contrib;
                        }

                    // Per-envelope ROUTING → filter cutoff (semitone space). Sum any
                    // of envs 2–5 routed to Filter 1 / Filter 2 / Filter 1+2.
                    float fMod1 = 0.0f, fMod2 = 0.0f;
                    for (int k = 0; k < 4; ++k)
                    {
                        const int   d  = envDest_[k + 1];
                        const float dv = envDepth_[k + 1] * eFltFree[k][i];
                        if (d == kEnvFilt1 || d == kEnvFilt12) fMod1 += dv;
                        if (d == kEnvFilt2 || d == kEnvFilt12) fMod2 += dv;
                    }

                    // Per-sample drift (one-pole LP of uniform white noise).
                    if (driftActive)
                    {
                        const float w = driftRng_.nextFloat() * 2.0f - 1.0f;
                        driftState_ += driftCoef_ * (w - driftState_);
                    }
                    const float driftSemis = driftState_ * driftDepthSemis;
                    const float fmax = juce::jmin (20000.0f, 0.45f * (float) coefSr);

                    // Filter 1 cutoff: base + routed envelopes (±96 ST) + LFO + drift.
                    // CPU: the semitone→Hz pow(2,x) is gated on change — with nothing modulating,
                    // cutSemis is bit-identical every sample and the pow never re-runs.
                    const float cutSemis1 = baseCutSemis  + fMod1 * 96.0f + lfoSemis1 + envCutSm1_ + driftSemis + ktSm1_ + velSm1_ * currentVelocity_ * 72.0f;   // fb178 · fb204 glided
                    // fb441 — recompute when the cutoff moved > 1 cent (0.01 semitone), not on every LSB. A slow
                    //   LFO/glide then redesigns every few samples instead of every sample; a fast one is unchanged.
                    if (std::fabs (cutSemis1 - lastCutSemis1_) > 0.01f)
                    {
                        lastCutSemis1_ = cutSemis1;
                        lastCutHz1_ = juce::jlimit (20.0f, fmax, 440.0f * std::pow (2.0f, (cutSemis1 - 69.0f) / 12.0f));
                    }
                    const float res1Raw = juce::jlimit (0.0f, 1.0f,
                        resSm1_ + resWander * driftState_ * 0.5f);
                    // fb441 — hand the slots a value that only changes when the ear could (0.05 % of range)
                    if (std::fabs (res1Raw - sentRes1_) > 0.0005f) sentRes1_ = res1Raw;
                    if (std::fabs (drvSm1_ - sentDrv1_) > 0.001f)  sentDrv1_ = drvSm1_;
                    const float res1 = sentRes1_;
                    filterSlot_.setParams (lastCutHz1_, res1, sentDrv1_, coefSr); visRes1_ = res1Raw;   // fb204 — glided drive

                    // Filter 2 cutoff: base + routed envelopes (±96 ST) + LFO + drift.
                    const float cutSemis2 = baseCutSemis2 + fMod2 * 96.0f + lfoSemis2 + envCutSm2_ + driftSemis + ktSm2_ + velSm2_ * currentVelocity_ * 72.0f;   // fb204 glided
                    if (std::fabs (cutSemis2 - lastCutSemis2_) > 0.01f)   // fb441 — 1-cent threshold, see filter 1
                    {
                        lastCutSemis2_ = cutSemis2;
                        lastCutHz2_ = juce::jlimit (20.0f, fmax, 440.0f * std::pow (2.0f, (cutSemis2 - 69.0f) / 12.0f));
                    }
                    const float res2Raw = juce::jlimit (0.0f, 1.0f,
                        resSm2_ + resWander * driftState_ * 0.5f);
                    if (std::fabs (res2Raw - sentRes2_) > 0.0005f) sentRes2_ = res2Raw;   // fb441
                    if (std::fabs (drvSm2_ - sentDrv2_) > 0.001f)  sentDrv2_ = drvSm2_;
                    const float res2 = sentRes2_;
                    filterSlot2_.setParams (lastCutHz2_, res2, sentDrv2_, coefSr); visRes2_ = res2Raw;   // fb204 — glided drive

                    // PER-OSC ROUTING combine. Buses: bus1 = scratch (F1's sources), bus2 = fltBus2_
                    // (F2 sources in parallel / F2-only in series), dry = fltDry_ (unrouted, bypass).
                    // a1/a2 NONE-aware AND gated on bus content (skip a filter with no input).
                    const bool par = (filterRouting_ != 0);
                    const bool a1  = (filterType1_ != kNoneType) && anySrc1_;
                    const bool a2  = (filterType2_ != kNoneType) && (par ? anySrc2_ : (anySrc1_ || anySrc2_));
                    // Post-filter output drive (back-panel Drive) — soft tanh saturation blended by
                    // amount, applied to each filter's wet output (F1's lands pre-F2 in series).
                    auto pdrive = [] (float& L, float& R, float amt, int type, float nrm) noexcept
                    {
                        if (amt <= 0.0001f) return;
                        const float d = 1.0f + amt * 4.0f;
                        const float inv = 1.0f / nrm;          // fb123 — drive the SHAPE, not the send level
                        L = L + amt * (nrm * fShape (L * inv, type, d, amt) - L);
                        R = R + amt * (nrm * fShape (R * inv, type, d, amt) - R);
                    };
                    // POST-FILTER STEREO WIDTH (filter Spread) — mid/side all-pass widener. The mid stays
                    // centred; a decorrelated copy of it (first-order all-pass = FLAT magnitude ⇒ ZERO pitch
                    // change, fixing the comb detune) is injected into the side → width even from a mono
                    // source, and mono-safe (L+R = 2·mid). Applied to the WET output only.
                    auto widen = [this] (float& L, float& R) noexcept
                    {
                        const float sp = juce::jmax (spread1_, spread2_);
                        if (sp <= 0.001f) return;
                        const float mid = 0.5f * (L + R), side = 0.5f * (L - R);
                        const float k = 0.7f;
                        const float dcx = k * mid + apMx1_ - k * apMy1_;  apMx1_ = mid; apMy1_ = dcx;
                        const float sideW = side + sp * 0.9f * dcx;
                        L = mid + sideW; R = mid - sideW;
                    };
                    // Filter the two buses (dry is added by the caller). filterMix blends each
                    // filter's wet vs its own bus input, exactly as the old per-filter MIX did.
                    // fb287 — takes the two filter slots as params so the SAME combine drives the audible
                    // path (filterSlot_/filterSlot2_) AND the reverb SEND (sendFilterSlot_/…2_): identical
                    // series/parallel, mix, and drive → the reverb hears exactly what you hear, post-filter.
                    auto filterBuses = [&] (float b1L, float b1R, float b2L, float b2R, float& outL, float& outR,
                                            tw::filters::FilterSlot& f1, tw::filters::FilterSlot& f2)
                    {
                        if (! par)   // SERIES: F1(bus1) → drive1 → (+ bus2 F2-only) → F2 → drive2
                        {
                            float w1L = b1L, w1R = b1R;
                            if (a1) { float wl = b1L, wr = b1R; f1.processStereo (wl, wr);
                                      w1L = mixSm1_ * wl + (1.0f - mixSm1_) * b1L;
                                      w1R = mixSm1_ * wr + (1.0f - mixSm1_) * b1R; }
                            pdrive (w1L, w1R, pdrvSm1_, driveType1_, drvNorm1_);
                            const float pL = w1L + b2L, pR = w1R + b2R;
                            float w2L = pL, w2R = pR;
                            if (a2) { float wl = pL, wr = pR; f2.processStereo (wl, wr);
                                      w2L = mixSm2_ * wl + (1.0f - mixSm2_) * pL;
                                      w2R = mixSm2_ * wr + (1.0f - mixSm2_) * pR; }
                            pdrive (w2L, w2R, pdrvSm2_, driveType2_, drvNorm2_);
                            outL = w2L; outR = w2R;
                            // fb556 — OVERPASS 4B: the filters as modulation sources. ⚠️ ONLY the
                            //  MAIN pair: this lambda also serves the SEND filters and every pooled
                            //  duplicate, and without the identity test the last one to run each
                            //  sample would win and the tap would follow whatever the send happened
                            //  to be doing.
                            if (&f1 == &filterSlot_) { fltOut_[0] = 0.5f * (w1L + w1R); fltOut_[1] = 0.5f * (w2L + w2R); }
                        }
                        else         // PARALLEL: F1(bus1) + F2(bus2), each with its own post-drive
                        {
                            float w1L = b1L, w1R = b1R;
                            if (a1) { float wl = b1L, wr = b1R; f1.processStereo (wl, wr);
                                      w1L = mixSm1_ * wl + (1.0f - mixSm1_) * b1L;
                                      w1R = mixSm1_ * wr + (1.0f - mixSm1_) * b1R; }
                            pdrive (w1L, w1R, pdrvSm1_, driveType1_, drvNorm1_);
                            float w2L = b2L, w2R = b2R;
                            if (a2) { float wl = b2L, wr = b2R; f2.processStereo (wl, wr);
                                      w2L = mixSm2_ * wl + (1.0f - mixSm2_) * b2L;
                                      w2R = mixSm2_ * wr + (1.0f - mixSm2_) * b2R; }
                            pdrive (w2L, w2R, pdrvSm2_, driveType2_, drvNorm2_);
                            outL = w1L + w2L; outR = w1R + w2R;
                            if (&f1 == &filterSlot_) { fltOut_[0] = 0.5f * (w1L + w1R); fltOut_[1] = 0.5f * (w2L + w2R); }   // fb556
                        }
                    };

                    const float dryL = busDryL[i], dryR = busDryR[i];
                    if (oversample)
                    {
                        // 2× linear-interp upsample → filter twice → box decimate. Interp bus1 (via
                        // the existing osPrev feedback) + bus2; dry bypasses (added once, post-decimate).
                        const float m1L = 0.5f * (osPrevL_   + sL[i]),     m1R = 0.5f * (osPrevR_   + sR[i]);
                        const float m2L = 0.5f * (osPrevB2L_ + busB2L[i]), m2R = 0.5f * (osPrevB2R_ + busB2R[i]);
                        float yMidL, yMidR; filterBuses (m1L, m1R, m2L, m2R, yMidL, yMidR, filterSlot_, filterSlot2_);
                        float yL, yR;       filterBuses (sL[i], sR[i], busB2L[i], busB2R[i], yL, yR, filterSlot_, filterSlot2_);
                        float wetL = 0.5f * (yMidL + yL), wetR = 0.5f * (yMidR + yR);
                        // fb237 — ROUTING INTEGRITY: the interp history is the BUS INPUT (bus2's exact
                        // grammar below). It stored the OUTPUT (wet + dry) — so every UNROUTED source
                        // bled at half gain into the oversampled filter input (Ladder/Acid303 only) and
                        // got filtered + driven against the pills (Max: 'my osc C is still being shaped'),
                        // plus a covert half-sample output-feedback color on the routed signal itself.
                        osPrevL_ = sL[i]; osPrevR_ = sR[i];
                        osPrevB2L_ = busB2L[i]; osPrevB2R_ = busB2R[i];
                        widen (wetL, wetR);
                        sL[i] = wetL + dryL; sR[i] = wetR + dryR;
                    }
                    else
                    {
                        float oL, oR; filterBuses (sL[i], sR[i], busB2L[i], busB2R[i], oL, oR, filterSlot_, filterSlot2_);
                        widen (oL, oR);
                        sL[i] = oL + dryL; sR[i] = oR + dryR;
                    }
                    // fb556 — the FILTER FOLLOWERS, ticked here because this is where the filters
                    //  actually run. Same peak detector as fb552's; only the tap differs.
                    //  ⚠️ WHY THESE ARE FOLLOWERS AND NOT AUDIO TAPS, since the reference offers
                    //  `FM (Filter 1)`: renderNextBlock is TWO per-sample loops — oscillators and
                    //  the blend stage first (the taps land at the end of that one), then the
                    //  filters, here. A filter value read by the blend stage is therefore one whole
                    //  BLOCK old, not one sample, and a modulator whose delay is the host's buffer
                    //  size is not a sound you can ship. Merging the loops is the fix and it is a
                    //  real one: 94 filter types, oversampling, send and pooled duplicates, series
                    //  and parallel routing, all on the hottest path in the voice. An ENVELOPE of
                    //  the filter has no such problem — a block of delay on a contour is nothing —
                    //  so that is what ships, and the audio-rate version stays open with its price
                    //  written down rather than half-built.
                    if (anyFollowArmed_)
                        for (int fk = 0; fk < 2; ++fk)
                        {
                            const float af = std::fabs (fltOut_[fk]);
                            float& fv = follow_[5 + fk];
                            fv = (af > fv) ? af : fv + (af - fv) * followRelCoef_;
                        }

                    // fb287 — POST-FILTER REVERB SEND: run the routed-osc send buses through the dedicated
                    // send-filters with the SAME coefficients (cutoff/res/drive) computed this sample, single-
                    // rate (the diffuse tail masks send-side aliasing — no oversample needed). Accumulate the
                    // filtered send into the shared bus. When no filter is engaged, send = the raw routed sum
                    // (identical to fb280). This is what makes 'osc A → filter → reverb' send the FILTERED A.
                    if (sendActive)
                    {
                        const int oi = startSample + i;
                        if (a1 || a2)
                        {
                            if ((i & 3) == 0)   // fb441 — mirrors redesign every 4th sample (send path), thresholded values
                            { sendFilterSlot_.setParams (lastCutHz1_, res1, sentDrv1_, sr);
                              sendFilterSlot2_.setParams (lastCutHz2_, res2, sentDrv2_, sr); }
                            float soL, soR; filterBuses (sF1L[i], sF1R[i], sF2L[i], sF2R[i], soL, soR, sendFilterSlot_, sendFilterSlot2_);
                            rvbSendL_[oi] += soL + sDryL[i];
                            rvbSendR_[oi] += soR + sDryR[i];
                        }
                        else   // no filter engaged → the send is the raw routed sum (byte-identical to fb280)
                        {
                            rvbSendL_[oi] += sF1L[i] + sF2L[i] + sDryL[i];
                            rvbSendR_[oi] += sF1R[i] + sF2R[i] + sDryR[i];
                        }
                    }
                    // fb296 — DELAY send: identical treatment through independent send-filters + independent bus.
                    if (dlySendActive)
                    {
                        const int oi = startSample + i;
                        if (a1 || a2)
                        {
                            if ((i & 3) == 0)   // fb441 — mirrors redesign every 4th sample (send path), thresholded values
                            { sendFilterSlot3_.setParams (lastCutHz1_, res1, sentDrv1_, sr);
                              sendFilterSlot4_.setParams (lastCutHz2_, res2, sentDrv2_, sr); }
                            float soL, soR; filterBuses (dF1L[i], dF1R[i], dF2L[i], dF2R[i], soL, soR, sendFilterSlot3_, sendFilterSlot4_);
                            dlySendL_[oi] += soL + dDryL[i];
                            dlySendR_[oi] += soR + dDryR[i];
                        }
                        else
                        {
                            dlySendL_[oi] += dF1L[i] + dF2L[i] + dDryL[i];
                            dlySendR_[oi] += dF1R[i] + dF2R[i] + dDryR[i];
                        }
                    }
                    // fb338 — DISTORTION send: identical treatment through its own send-filters + bus.
                    if (dstSendActive)
                    {
                        const int oi = startSample + i;
                        if (a1 || a2)
                        {
                            if ((i & 3) == 0)   // fb441 — mirrors redesign every 4th sample (send path), thresholded values
                            { sendFilterSlot5_.setParams (lastCutHz1_, res1, sentDrv1_, sr);
                              sendFilterSlot6_.setParams (lastCutHz2_, res2, sentDrv2_, sr); }
                            float soL, soR; filterBuses (tF1L[i], tF1R[i], tF2L[i], tF2R[i], soL, soR, sendFilterSlot5_, sendFilterSlot6_);
                            dstSendL_[oi] += soL + tDryL[i];
                            dstSendR_[oi] += soR + tDryR[i];
                        }
                        else
                        {
                            dstSendL_[oi] += tF1L[i] + tF2L[i] + tDryL[i];
                            dstSendR_[oi] += tF1R[i] + tF2R[i] + tDryR[i];
                        }
                    }
                    // fb347 — THE SHARED EXCLUSION BUS: same post-filter treatment, own filter pair
                    // (its mask differs from every device's, so it needs its own filter state).
                    if (exSendActive)
                    {
                        const int oi = startSample + i;
                        if (a1 || a2)
                        {
                            if ((i & 3) == 0)   // fb441 — mirrors redesign every 4th sample (send path), thresholded values
                            { sendFilterSlot7_.setParams (lastCutHz1_, res1, sentDrv1_, sr);
                              sendFilterSlot8_.setParams (lastCutHz2_, res2, sentDrv2_, sr); }
                            float soL, soR; filterBuses (xF1L[i], xF1R[i], xF2L[i], xF2R[i], soL, soR, sendFilterSlot7_, sendFilterSlot8_);
                            exSendL_[oi] += soL + xDryL[i];
                            exSendR_[oi] += soR + xDryR[i];
                        }
                        else
                        {
                            exSendL_[oi] += xF1L[i] + xF2L[i] + xDryL[i];
                            exSendR_[oi] += xF1R[i] + xF2R[i] + xDryR[i];
                        }
                    }
                    // fb348 — pooled instance sends, same post-filter treatment, each through its
                    // OWN lazily-built filter pair (its mask differs from every other slot's).
                    for (int ps = 0; ps < kPoolSends; ++ps)
                    {
                        if (! poolOn[ps]) continue;
                        auto& P = poolSend_[ps];
                        const int oi = startSample + i;
                        const float f1l = P.f1.getReadPointer(0)[i], f1r = P.f1.getReadPointer(1)[i];
                        const float f2l = P.f2.getReadPointer(0)[i], f2r = P.f2.getReadPointer(1)[i];
                        const float dl_ = P.dry.getReadPointer(0)[i], dr_ = P.dry.getReadPointer(1)[i];
                        if (a1 || a2)
                        {
                            if ((i & 3) == 0)   // fb441 — see the named mirrors above
                            { P.flt1->setParams (lastCutHz1_, res1, sentDrv1_, sr);
                              P.flt2->setParams (lastCutHz2_, res2, sentDrv2_, sr); }
                            float soL, soR; filterBuses (f1l, f1r, f2l, f2r, soL, soR, *P.flt1, *P.flt2);
                            P.L[oi] += soL + dl_;
                            P.R[oi] += soR + dr_;
                        }
                        else
                        {
                            P.L[oi] += f1l + f2l + dl_;
                            P.R[oi] += f1r + f2r + dr_;
                        }
                    }
                }
                // CPU: unticked LFOs advance phase once for the whole block — peek() (the
                // per-block mod matrix) and any mid-note re-routing stay phase-correct.
                for (int L = 0; L < wc::NUM_LFOS; ++L)
                    if (! (lfoTickMask & (1u << L)))
                        synthLfo_[L].skipSamples (numSamples);

                // NaN/Inf guard — Pirkle/Stilson note that ZDF ladders can blow
                // up under pathological coefficient updates. One bad sample
                // cascades into a stuck-on voice; cheap to detect once/block.
                if (! std::isfinite (sL[numSamples - 1])
                 || ! std::isfinite (sR[numSamples - 1])
                 || std::abs (sL[numSamples - 1]) > 30.0f
                 || std::abs (sR[numSamples - 1]) > 30.0f)
                {
                    filterSlot_.reset();
            filterSlot2_.reset();
                    sendFilterSlot_.reset(); sendFilterSlot2_.reset();   // fb287 — send mirror
                    osPrevL_ = osPrevR_ = 0.0f;
                    osPrevB2L_ = osPrevB2R_ = 0.0f;
                    juce::FloatVectorOperations::clear (sL, numSamples);
                    juce::FloatVectorOperations::clear (sR, numSamples);
                }
            }

            // Phase 8a — HORIZON tilt filter (per-channel high-shelf).
            // CPU: at amount 0 the shelf is unity gain — pure pass-through — so skip the two
            // biquads entirely (their state is irrelevant while bypassed; a later non-zero
            // amount just starts the shelves clean).
            if (horizonAmount_ != 0.0f)
            {
                float* chL = scratch_.getWritePointer (0);
                float* chR = scratch_.getWritePointer (1);
                juce::dsp::AudioBlock<float> blockL (&chL, 1, 0, (size_t) numSamples);
                juce::dsp::ProcessContextReplacing<float> ctxL (blockL);
                horizonShelfL_.process (ctxL);
                juce::dsp::AudioBlock<float> blockR (&chR, 1, 0, (size_t) numSamples);
                juce::dsp::ProcessContextReplacing<float> ctxR (blockR);
                horizonShelfR_.process (ctxR);
            }

            // ── Release-end declick — "silent light switch" ───────────────────────
            // The amp VCA has already silenced the oscillator, but the filter / HORIZON
            // shelf / grain tail can still be ringing. Rather than cut that ring the
            // instant the env goes idle (→ click, and a click machine-guns through the
            // granular engine), ramp the FINAL post-filter signal to true zero over
            // ~8 ms, then release the slot. Click-free at any release/decay length & Q.
            // CPU fast-kill: a Release tail below -80 dBFS is inaudible but still burns the
            // FULL voice cost (grain cloud + filters + LFOs + envelopes) until the exponential
            // release actually crawls to zero — often seconds on a pad. Arm the same declick
            // ramp the moment the release drops under -80 dB: fading to zero FROM silence is
            // silent by construction, and the slot frees for the pool immediately after.
            const bool releaseInaudible = ampEnv_.stage() == terrain::TerrainEnvelope::Stage::Release
                                          && ampEnv_.level() < 1.0e-4;
            if ((! ampEnv_.isActive() || releaseInaudible) && ! stealing_ && playing_)
            {
                if (! finishing_)
                {
                    finishing_      = true;
                    const float fadeSamples = static_cast<float> (kFinishFadeSec * sampleRate_);
                    finishFadeStep_ = 1.0f / juce::jmax (1.0f, fadeSamples);   // linear → 0
                    finishFade_     = 1.0f;
                }
                for (int i = 0; i < numSamples; ++i)
                {
                    scratchL[i] *= finishFade_;
                    scratchR[i] *= finishFade_;
                    finishFade_ -= finishFadeStep_;
                    if (finishFade_ < 0.0f) finishFade_ = 0.0f;
                }
            }

            // Sum filtered stereo scratch into output.
            auto* L = out.getWritePointer (0, startSample);
            auto* R = out.getNumChannels() > 1
                          ? out.getWritePointer (1, startSample) : L;
            for (int i = 0; i < numSamples; ++i)
            {
                L[i] += scratchL[i];
                R[i] += scratchR[i];
            }

            // Release the slot only once the declick ramp has reached true zero (the env
            // being idle armed the fade above; we wait for it to finish so nothing is cut).
            if (finishing_ && finishFade_ <= 0.0f)
            {
                finishing_ = false;
                playing_   = false;
                clearCurrentNote();
            }
        }

    private:
        // Phase 8b — populate per-sine phase-increment update helpers to SynthVoice. They populate the `uPhaseIncA_` / `uPhaseIncB_` arrays from MIDI note + octave/semi/cents tuning + per-sine per-OSC `uDetuneCents{A,B}_[u]` + WAVER drift. Called from `startNote` after the existing scalar updates, and from `renderNextBlock` per-block right after the existing erosion-drift recompute.
        void updateUnisonPhaseIncrementsA (double pitchNote) noexcept
        {
            for (int u = 0; u < kMaxUnison; ++u)
            {
                const double semitones =
                      (pitchNote - 69.0)
                    + static_cast<double> (octOffset_) * 12.0
                    + static_cast<double> (semiOffset_)
                    + static_cast<double> (centsOffset_)             * 0.01
                    + static_cast<double> (uDetuneCentsA_[(size_t) u]) * 0.01
                    + static_cast<double> (waverCentsA_[(size_t) u])  * 0.01
                    + (double) coarseModA_                                   // COARSE mod lane (per-block)
                    + pitchEnvSemis_;                                  // PITCH envelope (Batch 3)
                const double hz = 440.0 * std::pow (2.0, semitones / 12.0);
                uPhaseIncA_[(size_t) u] = std::min (hz / sampleRate_, 0.5);   // ±64 st Coarse can exceed fs — clamp at Nyquist
            }
        }

        void updateUnisonPhaseIncrementsB (double pitchNote) noexcept
        {
            for (int u = 0; u < kMaxUnison; ++u)
            {
                const double semitones =
                      (pitchNote - 69.0)
                    + static_cast<double> (octOffsetB_) * 12.0
                    + static_cast<double> (semiOffsetB_)
                    + static_cast<double> (centsOffsetB_)            * 0.01
                    + static_cast<double> (uDetuneCentsB_[(size_t) u]) * 0.01
                    + static_cast<double> (waverCentsB_[(size_t) u])  * 0.01
                    + (double) coarseModB_                                   // COARSE mod lane (per-block)
                    + pitchEnvSemis_;                                  // PITCH envelope (Batch 3)
                const double hz = 440.0 * std::pow (2.0, semitones / 12.0);
                uPhaseIncB_[(size_t) u] = std::min (hz / sampleRate_, 0.5);   // ±64 st Coarse can exceed fs — clamp at Nyquist
            }
        }
        void updateUnisonPhaseIncrementsC (double pitchNote) noexcept
        {
            for (int u = 0; u < kMaxUnison; ++u)
            {
                const double semitones =
                      (pitchNote - 69.0)
                    + static_cast<double> (octOffsetC_) * 12.0
                    + static_cast<double> (semiOffsetC_)
                    + static_cast<double> (centsOffsetC_)            * 0.01
                    + static_cast<double> (uDetuneCentsC_[(size_t) u]) * 0.01
                    + static_cast<double> (waverCentsC_[(size_t) u])  * 0.01
                    + (double) coarseModC_                                   // COARSE mod lane (per-block)
                    + pitchEnvSemis_;
                const double hz = 440.0 * std::pow (2.0, semitones / 12.0);
                uPhaseIncC_[(size_t) u] = std::min (hz / sampleRate_, 0.5);   // ±64 st Coarse can exceed fs — clamp at Nyquist
            }
        }
        void updateUnisonPhaseIncrementsD (double pitchNote) noexcept
        {
            for (int u = 0; u < kMaxUnison; ++u)
            {
                const double semitones =
                      (pitchNote - 69.0)
                    + static_cast<double> (octOffsetD_) * 12.0
                    + static_cast<double> (semiOffsetD_)
                    + static_cast<double> (centsOffsetD_)            * 0.01
                    + static_cast<double> (uDetuneCentsD_[(size_t) u]) * 0.01
                    + static_cast<double> (waverCentsD_[(size_t) u])  * 0.01
                    + (double) coarseModD_                                   // COARSE mod lane (per-block)
                    + pitchEnvSemis_;
                const double hz = 440.0 * std::pow (2.0, semitones / 12.0);
                uPhaseIncD_[(size_t) u] = std::min (hz / sampleRate_, 0.5);   // ±64 st Coarse can exceed fs — clamp at Nyquist
            }
        }

        // Phase 11a — populate per-sine uFramePosA_/B_ offsets from current
        // frameSpreadA01_/B01_ and the per-OSC voice counts. Each sine u in [0, count)
        // gets offset u_norm × spread × 0.5 (max ±0.5 of [0,1] frame range).
        // At UNISON=1 or SPREAD=0 every entry is 0.0 → render path falls back
        // to the voice-global framePos_ exactly (zero behaviour change vs pre-11a).
        void updateUnisonFramePositions() noexcept
        {
            // OSC A frame offsets across its own voice count.
            for (int u = 0; u < kMaxUnison; ++u)
            {
                if (u >= activeUnisonA_ || activeUnisonA_ <= 1) { uFramePosA_[(size_t) u] = 0.0f; continue; }
                const float u_norm = ((float) u / (float) (activeUnisonA_ - 1)) * 2.0f - 1.0f;
                uFramePosA_[(size_t) u] = u_norm * frameSpreadA01_ * 0.5f;
            }
            // OSC B frame offsets across its own voice count.
            for (int u = 0; u < kMaxUnison; ++u)
            {
                if (u >= activeUnisonB_ || activeUnisonB_ <= 1) { uFramePosB_[(size_t) u] = 0.0f; continue; }
                const float u_norm = ((float) u / (float) (activeUnisonB_ - 1)) * 2.0f - 1.0f;
                uFramePosB_[(size_t) u] = u_norm * frameSpreadB01_ * 0.5f;
            }
            // OSC C frame offsets.
            for (int u = 0; u < kMaxUnison; ++u)
            {
                if (u >= activeUnisonC_ || activeUnisonC_ <= 1) { uFramePosC_[(size_t) u] = 0.0f; continue; }
                const float u_norm = ((float) u / (float) (activeUnisonC_ - 1)) * 2.0f - 1.0f;
                uFramePosC_[(size_t) u] = u_norm * frameSpreadC01_ * 0.5f;
            }
            // OSC D frame offsets.
            for (int u = 0; u < kMaxUnison; ++u)
            {
                if (u >= activeUnisonD_ || activeUnisonD_ <= 1) { uFramePosD_[(size_t) u] = 0.0f; continue; }
                const float u_norm = ((float) u / (float) (activeUnisonD_ - 1)) * 2.0f - 1.0f;
                uFramePosD_[(size_t) u] = u_norm * frameSpreadD01_ * 0.5f;
            }
        }

        // Phase 11d — wavefolder. 3 shapes, each output-bounded to ±1.
        //   0 = Linear (Serge — triangle-wave fold, near-infinite odd harmonics)
        //   1 = Sine   (Vital — sin(drive·x), bounded, bell character)
        //   2 = Triangle (Buchla 259 — 3-stage cascade, warm West-Coast)
        //
        // fb313 — THE IMPLEMENTATION MOVED TO Source/Shapers.h (namespace tw::shapers), VERBATIM,
        // so the FX-rack Distortion device's FOLD family shares ONE copy with the oscillator path.
        // These are thin forwarders: every call site below is unchanged and the audio is
        // byte-identical. 🔑 The fold pre-gain constants (9.0f / 5.28318530f / 5.0f) used to be
        // written TWICE — once here and once in foldAntideriv — and raising one without the other
        // breaks ADAA's "F is the antiderivative of f" invariant, making the fold LOUDER *and*
        // ALIAS WORSE simultaneously. Shapers.h now holds them in ONE table (kFoldPre), so a depth
        // change is a single edit. Change fold depths THERE, never here.
        static inline float applyFold (float x, int shape, float amount) noexcept
        {
            return tw::shapers::applyFold (x, shape, amount);
        }

        static inline float foldGtri (float q) noexcept { return tw::shapers::foldGtri (q); }
        static inline float foldFlin (float x, float a) noexcept { return tw::shapers::foldFlin (x, a); }

        static inline float foldAntideriv (float x, int shape, float amount) noexcept
        {
            return tw::shapers::foldAntideriv (x, shape, amount);
        }

        using FoldState = tw::shapers::FoldState;

        static inline float applyFoldADAA (float x, int shape, float amount, FoldState& st) noexcept
        {
            return tw::shapers::applyFoldADAA (x, shape, amount, st);
        }


        // 3 modes: 0=LowPass (20k → 200 Hz quadratic), 1=HighPass (20 → 8000 Hz quadratic),
        // 2=Smear (allpass, 4000 → 200 Hz quadratic, Q 0.707 → 4.0 linear).
        void updateSpectralCoefficients (int type, float amount,
                                          juce::dsp::IIR::Filter<float>& filterL,
                                          juce::dsp::IIR::Filter<float>& filterR) noexcept
        {
            const float amtSq = amount * amount;
            using Coeffs = juce::dsp::IIR::Coefficients<float>;

            switch (type)
            {
                case 0:  // Low Pass
                {
                    const float cutoff = 200.0f + (1.0f - amtSq) * 19800.0f;
                    auto c = Coeffs::makeLowPass (sampleRate_, cutoff, 0.707f);
                    *filterL.coefficients = *c;
                    *filterR.coefficients = *c;
                    break;
                }
                case 1:  // High Pass
                {
                    const float cutoff = 20.0f + amtSq * 7980.0f;
                    auto c = Coeffs::makeHighPass (sampleRate_, cutoff, 0.707f);
                    *filterL.coefficients = *c;
                    *filterR.coefficients = *c;
                    break;
                }
                case 2:  // Smear (allpass with rising Q)
                {
                    const float cutoff = 200.0f + (1.0f - amtSq) * 3800.0f;
                    const float Q      = 0.707f + amount * 3.293f;
                    auto c = Coeffs::makeAllPass (sampleRate_, cutoff, Q);
                    *filterL.coefficients = *c;
                    *filterR.coefficients = *c;
                    break;
                }
                default:
                {
                    // For non-biquad modes (Comb/RingMod/BitCrush, types 3-5), set passthrough
                    // so the IIR filter has no effect; the per-sample render code
                    // applies the actual transform.
                    auto c = Coeffs::makeLowPass (sampleRate_, 20000.0f, 0.707f);
                    *filterL.coefficients = *c;
                    *filterR.coefficients = *c;
                    break;
                }
            }
        }

        // Standard PolyBLEP residual — subtract from the naive saw at the
        // discontinuity to suppress alias harmonics above Nyquist. Public
        // domain reference: Välimäki & Huovilainen, "Antialiasing Oscillators
        // in Subtractive Synthesis," IEEE SP Mag 2007.
        static double polyBlep (double t, double dt) noexcept
        {
            if (t < dt)
            {
                t /= dt;
                return t + t - t * t - 1.0;
            }
            if (t > 1.0 - dt)
            {
                t = (t - 1.0) / dt;
                return t * t + t + t + 1.0;
            }
            return 0.0;
        }

        double sampleRate_      = 48000.0;
        float  invSampleRate_   = 1.0f / 48000.0f;   // fb523 — set in setCurrentPlaybackSampleRate; the FM Hz→cycles/sample scale
        int    currentMidiNote_ = 60;
        float  currentVelocity_ = 1.0f;
        float  velDepth_ = 0.5f;   // fb262 — velocity CURVE amount (0..1, repurposed from depth): 0.5=linear, >0.5 lifts soft hits, <0.5 hardens. NO LONGER touches amp.

        // ── OSC SCOPE — per-osc audio-thread ring buffers (A/B/C/D) ─────────────
        // Live oscilloscope tap: per output sample the render loop writes each
        // oscillator's mono signal 0.5*(sX_L+sX_R) here, BEFORE the pre-filter sum.
        // Written AND read on the AUDIO thread only (the processor copies them right
        // after renderNextBlock, same thread), so these are PLAIN floats — NO atomics,
        // NO locks, NO allocation in the hot path. The fixed mask (& kScopeRingMask)
        // makes the advance a single AND with no branch.
        static constexpr int kScopeRingSize = 1024;
        static constexpr int kScopeRingMask = kScopeRingSize - 1;   // 0x3FF
        float scopeRing_[4][kScopeRingSize] = {};
        int   scopeRingPos_ = 0;

        // ── PORTAMENTO / GLIDE — fractional-pitch slide between notes ───────────────
        // glideNote_ is the pitch actually feeding the oscillators (the increment
        // functions read it). On note-on it either snaps to the target or starts a
        // timed slide from glideStart_ → glideTarget_ shaped by glideCurve_.
        double glideNote_       = 60.0;    // current sounding pitch (fractional MIDI note)
        double glideStart_      = 60.0;    // pitch at the start of the current slide
        double glideTarget_     = 60.0;    // destination pitch
        double glideProgress_   = 1.0;     // 0..1 (1 = arrived, no slide in progress)
        double glideDurSamples_ = 1.0;     // samples for the full slide
        // Broadcast glide context (pushed per-block from the processor):
        float  portaTime_       = 0.0f;    // glide time in seconds (0 = off → snap)
        float  glideCurve_      = 0.5f;    // 0..1 shape (0.5 = linear, <0.5 ease-in, >0.5 ease-out)
        bool   glideAlways_     = true;    // ALWAYS = glide every note; else only when a note is held
        bool   glideScaled_     = false;   // SCALED = time-per-octave (const rate); else fixed total time
        float  glideFromNote_   = -1.0f;   // pitch to glide FROM (-1 = none; from processor last-note)
        bool   glideAnyHeld_    = false;   // a synth note was sounding at this block (for ALWAYS-off gating)
        bool   legatoRetarget_  = false;   // armed by UnisonSynth: next startNote retargets, no retrigger
        bool   playing_         = false;

        // Five DAHDSR envelopes (Batch 2/3). ampEnv_ = AMP (drives VCA),
        // fltEnvT_ = FLT (cutoff), pitchEnvT_/mod1EnvT_/mod2EnvT_ = assignable.
        terrain::TerrainEnvelope ampEnv_;
        terrain::TerrainEnvelope fltEnvT_;
        terrain::TerrainEnvelope pitchEnvT_;
        terrain::TerrainEnvelope mod1EnvT_;
        terrain::TerrainEnvelope mod2EnvT_;
        // fb177 — DYNAMIC envelope pool (Row 3 S1): Env 6..32, created from the UI.
        // Dormant slots cost nothing — never ticked or read until the mod matrix
        // routes them (S2); noteOn/noteOff state flips are O(1).
        terrain::TerrainEnvelope dynEnv_[kMaxDynEnvs];
        int dynEnvCount_ = 0;
        uint32_t dynEnvUsedMask_ = 0;      // fb178 — matrix-referenced dyn envs (advance per block)
        uint32_t legEnvUsedMask_ = 0;      // fb178 — matrix-referenced legacy envs (FLT/PIT/M1/M2 bits)
        bool     anyEnvSource_   = false;
        float    envCutBlk1_ = 0.0f, envCutBlk2_ = 0.0f;   // fb178 — env→cutoff, block-rate semis
        float    envCutSm1_ = 0.0f, envCutSm2_ = 0.0f;     // fb204 — glided (2.5ms)
        float    resSm1_ = 0.0f, resSm2_ = 0.0f;           // fb204 — glided res
        float    mixSm1_ = 1.0f, mixSm2_ = 1.0f;           // fb204 — glided filter mix
        float    velSm1_ = 0.0f, velSm2_ = 0.0f;           // fb204 — glided vel→cutoff
        float    ktSm1_ = 0.0f, ktSm2_ = 0.0f;             // fb204 — glided keytrack semis
        float    pdrvSm1_ = 0.0f, pdrvSm2_ = 0.0f;         // fb204 — glided post-drive
        float    drvSm1_ = 0.0f, drvSm2_ = 0.0f;           // fb204 — glided filter DRIVE (into setParams)
        float  pitchEnvDepth_ = 0.0f;     // semitones, bipolar (Batch 3)
        double pitchEnvSemis_ = 0.0;      // per-block: depth × pitchEnv tick

        // Batch 1 Filter — FilterSlot replaces juce::dsp::LadderFilter.
        // baseCutHz / baseRes01 are the knob values; the renderNextBlock
        // loop adds envAmount * fltEnv + drift before each sample's
        // filterSlot_.setParams call (per-sample modulation, semitone space).
        tw::filters::FilterSlot filterSlot_;
        float                   baseCutHz_   = 20000.0f;
        float                   baseRes01_   = 0.0f;
        float                   filterKeytrack1_ = 0.0f, filterKeytrack2_ = 0.0f;  // 0..1 (cutoff tracks note)
        float                   drv01_       = 0.0f;
        float                   envAmount_   = 0.0f;   // -1..+1 (bipolar)
        float                   fltErosionAmount_ = 0.0f;

        // Per-voice EROSION drift state (cutoff random walk, ~0.5 Hz LP)
        juce::Random            driftRng_;
        float                   driftState_  = 0.0f;
        float                   driftCoef_   = 0.0f;   // 1 - exp(-2π·fc/sr)
        // fb302 — ANALOG DETUNE (always-on, subtle, no knob): a per-note static tuning error +
        // a slow OU drift, per voice, centered on 0 (averages in tune). Added to every osc pitch.
        std::uint32_t           analogRng_         = 0x9E3779B9u;   // xorshift state (non-zero)
        float                   analogStaticCents_ = 0.0f;          // this note's static error (~±2¢)
        float                   analogDriftCents_  = 0.0f;          // slow OU wander (~±2¢)
        double                  analogDetuneSemis_ = 0.0;           // (static+drift)/100 → semitones

        // 2× oversampling input-prev for linear-interp upsample (Ladder + Acid303)
        float                   osPrevL_     = 0.0f;
        float                   osPrevR_     = 0.0f;
        float                   osPrevB2L_   = 0.0f;   // oversample prev-state for the F2 routing bus
        float                   osPrevB2R_   = 0.0f;

        // Filter 2 — fully independent second FilterSlot (own type/cut/res/drv/env).
        // Shares the FLT envelope shape + EROSION drift, with its own ENV amount.
        tw::filters::FilterSlot filterSlot2_;
        // fb287 — POST-FILTER REVERB SEND: dedicated send-filters that MIRROR filterSlot_/filterSlot2_
        // (same type/poles/cutoff/res/drive each sample) but run on the reverb-routed-osc subset, so the
        // reverb hears the FILTERED signal (the fb280 send tapped PRE-filter → filtered oscs sent dry).
        // Own state (a different input than the main filters), used only when a route is active + a filter
        // is engaged; otherwise the send is the raw routed sum (byte-identical to before).
        tw::filters::FilterSlot sendFilterSlot_;
        tw::filters::FilterSlot sendFilterSlot2_;
        tw::filters::FilterSlot sendFilterSlot3_;   // fb296 — delay-send filter mirror (independent from reverb send)
        tw::filters::FilterSlot sendFilterSlot4_;
        tw::filters::FilterSlot sendFilterSlot5_;   // fb338 — distortion-send filter mirror
        tw::filters::FilterSlot sendFilterSlot6_;
        tw::filters::FilterSlot sendFilterSlot7_;   // fb347 — the shared exclusion bus's own filter pair
        tw::filters::FilterSlot sendFilterSlot8_;   //   (its union mask differs from every device's)
        float                   baseCutHz2_  = 20000.0f;
        float                   baseRes012_  = 0.0f;
        float                   drv012_      = 0.0f;
        float                   envAmount2_  = 0.0f;   // -1..+1 (bipolar)
        int                     filterType1_ = 0;      // tracked for NONE-aware routing
        int                     filterType2_ = (int) tw::filters::Type::NONE;
        // CPU: semitone→Hz pow() change-gates (unmodulated cutoff = bit-identical per sample).
        // Sentinel -1e9 never matches a real semitone sum, so the first sample always computes.
        float                   lastCutSemis1_ = -1.0e9f, lastCutHz1_ = 20000.0f;
        // fb441 — CPU: the (res, drive) values last HANDED to setParams. The glided resSm/drvSm and the
        //   erosion wander move a hair every sample, and every FilterSlot's equality gate then recomputed
        //   tan()/exp() per sample — in the main pair AND in every send mirror AND every routed pooled
        //   device, per voice. Below a 1-cent / 0.05 %-of-range change the ear cannot tell; above it we
        //   recompute exactly as before (audio-rate modulation keeps its per-sample update).
        float                   sentRes1_ = -1.0f, sentDrv1_ = -1.0f, sentRes2_ = -1.0f, sentDrv2_ = -1.0f;
        float                   visRes1_ = 0.3f, visRes2_ = 0.3f;   // fb163 — live res for the display (post-drift)
        float                   lastCutSemis2_ = -1.0e9f, lastCutHz2_ = 20000.0f;
        // Routing between the two filters + per-filter wet/dry mix.
        int                     filterRouting_ = 0;    // 0 = series, 1 = parallel
        float                   filterMix1_  = 1.0f;   // 0 = dry, 1 = fully filtered
        float                   filterMix2_  = 1.0f;
        // ── Per-oscillator filter routing (independent + dry bypass) ──────────
        // masks: [0..4] = A,B,C,D,Sub. Default all-true ⇒ a fresh patch routes every source to
        // both filters, which in the default SERIES/F2=None case is byte-identical to the old
        // single-mix path. busCo*_ are per-block 0/1 coefficients derived from the masks+routing.
        float                   fltSrc1_[5] = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };   // fb79 — continuous sends, default DRY (the processor pushes real values every block)
        float                   fltSrc2_[5] = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
        float                   busCo1_[5]  = { 1,1,1,1,1 };   // → Filter 1 bus (reuses scratch_)
        float                   busCo2_[5]  = { 0,0,0,0,0 };   // → Filter 2 bus (fltBus2_)
        float                   busCoD_[5]  = { 0,0,0,0,0 };   // → dry/bypass  (fltDry_)
        bool                    anySrc1_ = true, anySrc2_ = false;   // any source routed to each filter this block
        // fb63 — NOISE filter routing (its own masks + bus coefficients; default DRY like the oscs).
        bool                    noiseSrc1_ = false, noiseSrc2_ = false;
        float                   noiseCo1_ = 0.0f, noiseCo2_ = 0.0f, noiseCoD_ = 1.0f;
        juce::AudioBuffer<float> fltBus2_, fltDry_;              // F2 + dry buses (bus1 = scratch_)
        // fb287 — per-osc reverb SEND buses (routed-osc subset split by the SAME filter routing): F1/F2/dry.
        // Filled in the summing loop, robin/steal-scaled alongside the main buses, then run through the
        // dedicated send-filters in the filter loop → the FILTERED post-filter reverb send.
        juce::AudioBuffer<float> rvbSendF1_, rvbSendF2_, rvbSendDry_;
        // fb280 — per-osc reverb send: shared bus pointers + 0/1 route gains (A,B,C,D,Sub,Noise).
        float*                  rvbSendL_ = nullptr;
        float*                  rvbSendR_ = nullptr;
        float                   rvbG_[6] = { 0, 0, 0, 0, 0, 0 };
        bool                    rvbAny_ = false;
        // fb296 — per-osc DELAY send: fully independent mask + send bus (parallel to the reverb send).
        juce::AudioBuffer<float> dlySendF1_, dlySendF2_, dlySendDry_;
        float*                  dlySendL_ = nullptr;
        float*                  dlySendR_ = nullptr;
        float                   dlyG_[6] = { 0, 0, 0, 0, 0, 0 };
        bool                    dlyAny_ = false;
        // fb338 — per-osc DISTORTION send: third parallel bus, fully independent mask.
        juce::AudioBuffer<float> dstSendF1_, dstSendF2_, dstSendDry_;
        float*                  dstSendL_ = nullptr;
        float*                  dstSendR_ = nullptr;
        float                   dstG_[6] = { 0, 0, 0, 0, 0, 0 };
        bool                    dstAny_ = false;
        // fb348 — one send slot per POOLED instance. Buffers and the filter pair are allocated only
        // when that instance is actually routed, so unrouted slots cost ~nothing.
        struct PoolSend
        {
            float* L = nullptr; float* R = nullptr;
            float  g[6] { 0, 0, 0, 0, 0, 0 };
            bool   any = false;
            juce::AudioBuffer<float> f1, f2, dry;
            std::unique_ptr<tw::filters::FilterSlot> flt1, flt2;   // lazy — see ensurePoolFilters()
        };
        PoolSend poolSend_[kPoolSends];
        double   poolFltSr_ = 44100.0;
        // fb347 — the SHARED routed-dry exclusion bus (union of every device mask, each osc once).
        juce::AudioBuffer<float> exSendF1_, exSendF2_, exSendDry_;
        float*                  exSendL_ = nullptr;
        float*                  exSendR_ = nullptr;
        float                   exG_[6] = { 0, 0, 0, 0, 0, 0 };
        bool                    exAny_ = false;
        float                   velAmt1_ = 0.0f, velAmt2_ = 0.0f;    // velocity → cutoff depth (back-panel Vel)
        float                   postDrv1_ = 0.0f, postDrv2_ = 0.0f;  // post-filter output drive (back-panel Drive)
        float                   drvNorm1_ = 1.0f, drvNorm2_ = 1.0f;  // fb123 — bus send level (drive normalization)
        int                     driveType1_ = 0, driveType2_ = 0;    // Drive TYPE (0=Tube..5=Fuzz)
        float                   spread1_ = 0.0f, spread2_ = 0.0f;    // filter SPREAD → post-filter stereo width (no detune)
        float                   apMx1_ = 0.0f, apMy1_ = 0.0f;        // width all-pass state (mid-channel decorrelator)

        // ── Per-envelope ROUTING state (mini mod-matrix) ──────────────────────
        // Index 0 = AMP (not routed); 1..4 = the free envelopes (FLT/PITCH/M1/M2,
        // UI 2/3/4/5). dest is an EnvDest index; depth is bipolar -1..+1.
        int                     envDest_[5]  = { kEnvAmp, kEnvFilt1, kEnvPitch, kEnvOff, kEnvOff };
        float                   envDepth_[5] = { 1.0f, 0.0f, 0.0f, 0.0f, 0.0f };
        // Per-sample value of all five envelopes for this block (ch0=AMP, 1=FLT,
        // 2=PITCH, 3=MOD1, 4=MOD2), filled by the pre-pass so the amp loop, the
        // filter loop, and the per-block pitch path all read the SAME values.
        juce::AudioBuffer<float> envScratch_;
        // Latent modulation buses (envs routed to Mod 1/2). Nothing reads these
        // yet — the master mod matrix will. Block-rate, bipolar.
        double                  mod1Bus_     = 0.0;
        double                  mod2Bus_     = 0.0;

        static float hzToSemi (float hz) noexcept
        {
            return 69.0f + 12.0f * std::log2 (juce::jmax (1.0f, hz) / 440.0f);
        }

        juce::AudioBuffer<float>       scratch_;
        float                          level_ = 0.7f;
        float lvlSmA_ = 0.0f, lvlSmB_ = 0.0f, lvlSmC_ = 0.0f, lvlSmD_ = 0.0f, lvlSmCoef_ = 0.02f;   // fb180
        float envLvlOwn_[4]   = { 0, 0, 0, 0 };   // fb183 — Σ|depth| of env routes owning each osc Level (block-rate)
        float envLvlDrive_[4] = { 0, 0, 0, 0 };   // fb183 — Σ|depth|·env: the owned level target
        float                          panL_  = 0.7071f;  // cos(pi/4)
        float                          panR_  = 0.7071f;  // sin(pi/4)
        float                          panLT_ = 0.7071f;  // fb202 — glide targets (setPan writes here;
        float                          panRT_ = 0.7071f;  //         the render loop glides the live gains)

        // SOLO/MUTE — per-osc (A,B,C,D) click-free gate (smoothed one-pole, ~4ms fade)
        float oscGate_[4]       { 1.0f, 1.0f, 1.0f, 1.0f };   // smoothed solo/mute gate (click-free)
        bool  oscDead_[4]       { false, false, false, false }; // gate fully settled at 0 → skip the osc's render entirely
        float oscGateTarget_[4] { 1.0f, 1.0f, 1.0f, 1.0f };
        float flowWave_ = 0.0f;   // FLOW · ARP WAVE lane frame offset (fb105), block-pushed

        // FLOW · ROBIN state (see setRobin) — the Wheel brain stages, startNote applies
        bool  robinOn_    = false;
        wc::FlowRobin* robinBrain_ = nullptr;
        bool  robinEn_[4] { true, false, false, false };
        int   robinPick_  = -1;
        float robinAmpL_ = 1.0f, robinAmpR_ = 1.0f;           // per-station Pan
        int   robinDelay_ = 0;                                // Wobble: late-start samples
        double robinGlideFrom_ = -1.0; float robinGlideSec_ = 0.0f;
        int   robinHandWait_ = -1; float robinHandFadeSec_ = 0.03f;   // Fade/Overlap handover
        float oscGateCoef_ = 0.006f;                          // one-pole coef, set in setCurrentPlaybackSampleRate

        int   octOffset_   = 0;
        int   semiOffset_  = 0;
        float centsOffset_ = 0.0f;

        const tw::Wavetable* currentWavetable_ = nullptr;
        float                framePos_         = 0.0f;

        // Phase 2C — Warp state.
        int                  warpMode_         = 0;     // 0=NONE,1=BEND,2=SYNC,3=FORMANT
        float                warpAmount_       = 0.0f;  // 0..1

        // Phase 3 — Engine choice.
        // ════════════════ SAMPLE-ENGINE-VOICE — state + render ════════════════
        // UNISON-ON-SAMPLE — one SampleEngine per unison voice (index 0 = the dry/centre voice,
        // byte-identical to the pre-unison single-engine path when the count is 1).
        std::array<tw::SampleEngine, kMaxUnison> sampleEngA_, sampleEngB_, sampleEngC_, sampleEngD_;
        tw::WarpProcessor  sampleWarpA_, sampleWarpB_, sampleWarpC_, sampleWarpD_;
        SampleEngineParams sampleParamsA_, sampleParamsB_, sampleParamsC_, sampleParamsD_;
        // PEROSC-VOICE — per-OSC sample sources (A/B/C/D each read their own buffer)
        tw::SampleBuffer*  sampleSource_[4] = { nullptr, nullptr, nullptr, nullptr };
        tw::SampleBuffer::BufferPtr sampleHeldBuf_[4];                                    // keep each alive
        const juce::AudioBuffer<float>* sampleBufLast_[4] = { nullptr, nullptr, nullptr, nullptr };
        double sampleNativeOverOut_[4] = { 1.0, 1.0, 1.0, 1.0 };
        juce::AudioBuffer<float> sampleBlkA_, sampleBlkB_, sampleBlkC_, sampleBlkD_, warpSrc_;
        const float *sampBlkAL_ = nullptr, *sampBlkAR_ = nullptr, *sampBlkBL_ = nullptr, *sampBlkBR_ = nullptr,
                    *sampBlkCL_ = nullptr, *sampBlkCR_ = nullptr, *sampBlkDL_ = nullptr, *sampBlkDR_ = nullptr;
        bool          sampleNoteOnPending_ = false;

        // ════════ GRANULAR-ENGINE-VOICE — per-OSC granular engines + state ════════
        std::array<tw::GranularEngine, kMaxUnison> granEngA_, granEngB_, granEngC_, granEngD_;
        tw::GranularEngineParams granParamsA_, granParamsB_, granParamsC_, granParamsD_;
        tw::SampleBuffer::BufferPtr granHeldBuf_[4];                                      // pin each alive during render
        const juce::AudioBuffer<float>* granBufLast_[4] = { nullptr, nullptr, nullptr, nullptr };
        double granNativeOverOut_[4] = { 1.0, 1.0, 1.0, 1.0 };
        juce::AudioBuffer<float> granBlkA_, granBlkB_, granBlkC_, granBlkD_;
        const float *granBlkAL_ = nullptr, *granBlkAR_ = nullptr, *granBlkBL_ = nullptr, *granBlkBR_ = nullptr,
                    *granBlkCL_ = nullptr, *granBlkCR_ = nullptr, *granBlkDL_ = nullptr, *granBlkDR_ = nullptr;
        bool granNoteOnPending_ = false;
        // ── GEODE-ENGINE-VOICE — per-OSC resynthesis state (mirrors the granular block-render) ──
        std::array<tw::GeodeEngine, kMaxUnison> geodeEngA_, geodeEngB_, geodeEngC_, geodeEngD_;
        tw::GeodeParams geodeParamsA_, geodeParamsB_, geodeParamsC_, geodeParamsD_;
        const std::atomic<const tw::GeodeFrameStore*>* geodeStoreSrc_[4] = { nullptr, nullptr, nullptr, nullptr };
        const tw::GeodeFrameStore* geodeStoreLast_[4] = { nullptr, nullptr, nullptr, nullptr };
        juce::AudioBuffer<float> geodeBlkA_, geodeBlkB_, geodeBlkC_, geodeBlkD_;
        const float *geodeBlkAL_ = nullptr, *geodeBlkAR_ = nullptr, *geodeBlkBL_ = nullptr, *geodeBlkBR_ = nullptr,
                    *geodeBlkCL_ = nullptr, *geodeBlkCR_ = nullptr, *geodeBlkDL_ = nullptr, *geodeBlkDR_ = nullptr;
        bool geodeNoteOnPending_ = false;

        // ── HARMONIC-ENGINE-VOICE (Engine::HARM, slot 5) — per-osc procedural additive banks.
        // Index 0 of each array = the unison ANCHOR (owns the spectrum-build arrays); siblings
        // are render-state-only and borrow the anchor's bank via adoptBank() every block.
        // ── SUB (universal osc box, 2026-07-09) — ONE voice-anchored sub per osc.
        // Tracks the osc's FINAL pitch (engine-matched source + Oct/Semi/Cent(+Coarse base)
        // + Coarse mod + pitch env) down Range octaves. Weight is exp-bias perceptual; the
        // sum is energy-normalized (1/sqrt(1+w^2)) so a heavy sub never blows headroom.
        // Mono = no stereo spread; injected pre-pan so it rides the osc's pan/level (by design).
        struct SubLane
        {
            tw::SubOsc osc;
            int    range = 0, form = 0, formOld = 0;
            float  weight = 0.f, heatK = 0.f;
            double inc = 0.0;
            float  w = 0.f, dw = 0.f;     // ramped effective weight
            float  n = 1.f, dn = 0.f;     // ramped energy normalizer
            float  hCur = 0.f, dh = 0.f;  // ramped drive (heat) — no block zipper
            float  xf = 0.f, dxf = 0.f;   // Shape crossfade (old→new form, one block)
            bool   on = false;
        };
        SubLane sub_[4];
        float coarseModA_ = 0.f, coarseModB_ = 0.f, coarseModC_ = 0.f, coarseModD_ = 0.f;
        float subWMod_[4] = { 0.f, 0.f, 0.f, 0.f }, subHMod_[4] = { 0.f, 0.f, 0.f, 0.f };

        void prepareSubBlock (int numSamples) noexcept
        {
            const float invN = 1.f / (float) juce::jmax (1, numSamples);
            const int   octs[4] = { octOffset_, octOffsetB_, octOffsetC_, octOffsetD_ };
            const int   sems[4] = { semiOffset_, semiOffsetB_, semiOffsetC_, semiOffsetD_ };
            const float cts[4]  = { centsOffset_, centsOffsetB_, centsOffsetC_, centsOffsetD_ };
            const float crs[4]  = { coarseModA_, coarseModB_, coarseModC_, coarseModD_ };
            const Engine eng4[4] = { engine_, engineB_, engineC_, engineD_ };
            for (int o = 0; o < 4; ++o)
            {
                SubLane& sl = sub_[o];
                // a dead/muted osc ramps its sub OUT (declick) instead of ticking for a ×0 gate
                const float wKnob = oscDead_[o] ? 0.f
                                  : juce::jlimit (0.f, 1.f, sl.weight + subWMod_[o]);
                // exponential-bias perceptual level (house curve law — never p^k)
                const float wEff = wKnob <= 0.f ? 0.f
                                 : (std::exp (2.0f * wKnob) - 1.0f) / (std::exp (2.0f) - 1.0f);
                // fb522 — `|| subForce_[o]` keeps the lane TICKING when a blend slot names Sub as
                // its source. Without it the tap reads silence at Sub Mix 0 (wEff = 0 → sl.on
                // false → subMix is never called), which is the same class of bug modSrcForce_
                // fixes for the oscs. It is inaudible: wEff is still 0, so sl.w ramps to 0 and
                // subMix adds v*0 to the mix and multiplies the osc by n, which settles at 1.0f.
                // MUTATION GATE: delete `|| subForce_[o]` and the Sub-as-blend-source test must
                // go red (the modulator flatlines at Sub Mix 0).
                sl.on = wEff > 1e-5f || sl.w > 1e-5f || subForce_[o];    // stays on to RAMP OUT (declick)
                if (! sl.on) { sl.dw = 0.f; sl.dn = (1.f - sl.n) * invN; sl.xf = 0.f; subModTap_[o] = 0.f; continue; }
                // pitch source MATCHES the engine: WT/FM glide (glideNote_); the sample-family
                // engines snap to currentMidiNote_ at note-on — the sub must stay glued to its
                // OWN osc, not slide away from it during portamento (cleanup-sweep fix)
                const bool glides = (eng4[o] == Engine::WT || eng4[o] == Engine::FM);
                const double noteSrc = glides ? glideNote_ : (double) currentMidiNote_;
                const double semis = (noteSrc - 69.0)
                                   + (double) octs[o] * 12.0 + (double) sems[o]
                                   + (double) cts[o] * 0.01 + (double) crs[o]
                                   + pitchEnvSemis_
                                   + 12.0 * (double) (sl.range - 4);   // Sub octave: idx 0..8 → -4..+4 (idx 4 = 0, regular pitch)
                const double hz = 440.0 * std::pow (2.0, semis / 12.0);
                sl.inc = juce::jlimit (0.0, 0.45, hz / sampleRate_);
                const float nT = 1.0f / std::sqrt (1.0f + wEff * wEff);
                const float hT = juce::jlimit (0.f, 1.f, sl.heatK + subHMod_[o]);
                sl.dw  = (wEff - sl.w) * invN;
                sl.dn  = (nT   - sl.n) * invN;
                sl.dh  = (hT   - sl.hCur) * invN;
                sl.dxf = (sl.xf > 0.f) ? -sl.xf * invN : 0.f;   // Shape morph completes in one block
            }
        }

        // subAcc receives THIS sub's post-normalization contribution (mono) so the filter router
        // can route the sub independently of its oscillator. l/r are updated EXACTLY as before
        // (osc+sub combined), so the scope/blend taps that read sX are unchanged; the osc-only
        // signal is recovered downstream as (sX - subAcc).
        inline void subMix (int o, float& l, float& r, float& subAcc) noexcept
        {
            SubLane& sl = sub_[o];
            sl.w += sl.dw; sl.n += sl.dn; sl.hCur += sl.dh;
            float v;
            if (sl.xf > 0.f)
            {
                v = sl.osc.tickXf (sl.inc, sl.form, sl.formOld, sl.xf);
                sl.xf += sl.dxf; if (sl.xf < 0.f) sl.xf = 0.f;
            }
            else v = sl.osc.tick (sl.inc, sl.form);
            v = sl.osc.heat (v, sl.hCur);
            // fb522 — the SUB modulator tap (blend src 4), taken PRE-WEIGHT for the same reason
            // modPrev_ is taken pre-gain: a source turned down to silence must still modulate.
            // Bounded to the same ±4 as modPrev_/noiseModTap_ so the blend ceilings above are
            // dimensioned against one known modulator range. (Splitting the old single line
            // `heat(...) * sl.w` in two is bit-identical — same operations, same order.)
            subModTap_[o] = juce::jlimit (-4.f, 4.f, v);
            v *= sl.w;
            subAcc += v * sl.n;          // sub's share of the normalized sum (mono)
            l = (l + v) * sl.n;
            r = (r + v) * sl.n;
        }

        std::array<tw::HarmonicEngine, kMaxUnison> harmEngA_, harmEngB_, harmEngC_, harmEngD_;
        tw::HarmParams harmParamsA_, harmParamsB_, harmParamsC_, harmParamsD_;
        juce::AudioBuffer<float> harmBlkA_, harmBlkB_, harmBlkC_, harmBlkD_;
        const float *harmBlkAL_ = nullptr, *harmBlkAR_ = nullptr, *harmBlkBL_ = nullptr, *harmBlkBR_ = nullptr,
                    *harmBlkCL_ = nullptr, *harmBlkCR_ = nullptr, *harmBlkDL_ = nullptr, *harmBlkDR_ = nullptr;
        bool harmNoteOnPending_ = false;

        // MODAL-ENGINE-VOICE — per-OSC physical model (Engine::MODAL), same shape as HARM
        std::array<tw::ModalEngine, kMaxUnison> modalEngA_, modalEngB_, modalEngC_, modalEngD_;
        // fb498 — false until prepareModalEngines() has run on the message thread. Until then the
        // six DelayA lines inside every one of these engines are EMPTY vectors (size 0), and
        // DelayA::setDelay would index straight past the end of one (ModalEngine.h:386-395), so
        // this flag gates the render calls at renderNextBlock — not merely as an optimisation but
        // as the bounds guard. readPos01() (line ~249) is safe unguarded because it is behind
        // isActive(), which stays false until a noteOn that only the gated render path can issue.
        std::atomic<bool> modalReady_ { false };
        // fb517 — HARM's arm flag, same contract as modalReady_ above (prepareHarmonicEngines
        // publishes with release; renderHarmonicBlocks and harmLiveBins load with acquire).
        std::atomic<bool> harmReady_ { false };
        tw::ModalParams modalParamsA_, modalParamsB_, modalParamsC_, modalParamsD_;

        // ── BLEND MODES (Serum-2-style cross-osc warp) — per-voice state ──
        struct BlendSlotV { int mode = 0; int src = 0; float depth = 0.f; };   // depth = exp-biased target
        BlendSlotV blendSlot_[4][4];
        float blendDepthSm_[4][4] = {};   // per-sample de-zippered depth
        float blendLfoSm_[4][4]   = {};   // fb225 — per-sample glide over the BLOCK-STEPPED LFO value (peek updates once per block; consumed per sample = a ~344Hz staircase = Max's 'heavy static'. The COMB-CLICK law applied at the consumption site.)
        float modPrev_[4] = { 0.f, 0.f, 0.f, 0.f };   // prev-sample pre-gain osc outputs = the modulator taps
        float noiseModTap_ = 0.0f;                    // fb64 — the NOISE modulator tap (src=5), pre-gain, 1-sample delayed
        float subModTap_[4] = { 0.f, 0.f, 0.f, 0.f }; // fb522 — the SUB modulator tap (src=4), PER CARRIER, pre-weight, 1-sample delayed
        bool  subForce_[4]  = { false, false, false, false };   // fb522 — a blend slot names Sub → keep the lane ticking at Sub Mix 0
        bool  anyBlendArmed_ = false;                 // fb522 — any FM/PD/AM/RM slot live (or still decaying) this block
        bool  noiseForce_  = false;                   // fb64 — noise is used as a blend source this block → generate it even if output off
        const std::atomic<const wc::ModCurveSet*>* modCurves_ = nullptr;   // fb554
        float fmPhase_[4] = { 0.f, 0.f, 0.f, 0.f };   // per-carrier FM integrator (freq-dev → phase; leaky, thru-zero)
        float follow_[wc::kNumFollowers] = {};   // fb552 — osc A-D + noise, fb556 + filter 1/2 — peak followers, 0..1
        float fltOut_[2] = { 0.f, 0.f };        // fb556 — the two filters' own output, per sample, main pair only
        float followRelCoef_ = 0.0f;                                       // fb552 — set with the sample rate
        bool  anyFollowArmed_ = false;                                     // fb552 — CPU: no routed follower, no per-sample tick
        double blendCarrInc_[4] = { 0.0, 0.0, 0.0, 0.0 };   // fb551 — carrier rate in cycles/sample, block-rate. MODES 9/10 ONLY (see kFmExpOctaves).
        float  blendWarpMax_[4]   = { 0.f, 0.f, 0.f, 0.f };   // fb553 — mode 6's worst-case warp swing, block-rate, for the mip pick
        float  wfKx_[4]     = { 0.f, 0.f, 0.f, 0.f };        // fb553 — π·f0/fs per osc, so the per-sample corner needs no divide
        float  wfXMin_      = 0.f;                            // fb553 — π·20/fs, the static path's own lower clamp
        float  wfAmt_[4][2] = { { 0.f, 0.f }, { 0.f, 0.f }, { 0.f, 0.f }, { 0.f, 0.f } };   // fb553 — each warp slot's block amount, to add the audio-rate offset to
        bool   blendWarpArmed_[4] = { false, false, false, false };   // fb553 — keeps the per-sample warp branch predicted when nothing is armed
        // BLEND MODES — ALL-ENGINES support (2026-07-12). Two per-block flags derived from the warp
        // matrix, both inert (false) for any patch with no active FM/PD slot → existing sound is
        // byte-identical whenever nothing is blended:
        //   modSrcForce_[o]     = osc o feeds an armed FM/PD slot → its block engine must RENDER even
        //                         at Level 0 (so it can modulate silently — the "turn D down, still
        //                         hear D's FM" behaviour). Output stays inaudible (real level_ = 0).
        //   blkCarrierArmed_[o] = osc o is a BLOCK engine (Sample/Granular/Spec/Harmonic/Modal) that
        //                         carries an armed FM/PD slot → its rendered block gets phase-modulated
        //                         through a short delay ring (below) = FM/PD *on a sample*.
        bool  modSrcForce_[4]     = { false, false, false, false };
        bool  blkCarrierArmed_[4] = { false, false, false, false };
        // fb522 OVERPASS — the block-carrier PD path was ~10x tamer than the WT path at the same
        // knob: [C] a WT carrier gets PD in CYCLES (±1.20 turns) while a block carrier got the
        // same number scaled to ±64 SAMPLES, which at 100 Hz / 48 kHz (one cycle = 480 samples)
        // tops out near ⅛ of a cycle. 64 -> 256 and 40 -> 160 closes it.
        // ⚠️ THE RING MUST GROW WITH IT. The read pointer sits `kBlkMaxOff` behind the write and
        // swings ±kBlkMaxOff, so the deepest read is 2·kBlkMaxOff behind — 512 samples now. At
        // the old ring of 256 that read would have wrapped PAST the write pointer and returned
        // the future, i.e. garbage, not a deeper effect. 1024 preserves the exact margin the old
        // geometry had (ring = 4·kBlkMaxOff). Cost: 8 KB -> 32 KB per voice.
        static constexpr int   kBlkRing     = 1024;     // ring length (power of two → mask 1023)
        static constexpr int   kBlkMaxOff   = 256;      // ± sample excursion at full depth (also the base delay)
        static constexpr float kBlkOffScale = 160.0f;   // blendOff (cycles) → samples  [EAR-TUNABLE: raise = deeper]
        static_assert (2 * kBlkMaxOff < kBlkRing, "blendReadBlock would read past the write pointer");
        static_assert ((kBlkRing & (kBlkRing - 1)) == 0, "kBlkRing must be a power of two (it is used as a mask)");
        static constexpr float kBlkArmCoef  = 0.0012f;  // ~20 ms one-pole to declick the delay engaging/leaving
        float blkRingL_[4][kBlkRing] = {};
        float blkRingR_[4][kBlkRing] = {};
        int   blkRingW_[4]  = { 0, 0, 0, 0 };
        float blkArmSm_[4]  = { 0.f, 0.f, 0.f, 0.f };   // 0→1 arm ramp (delay + mod depth fade together)
        juce::AudioBuffer<float> modalBlkA_, modalBlkB_, modalBlkC_, modalBlkD_;
        const float *modalBlkAL_ = nullptr, *modalBlkAR_ = nullptr, *modalBlkBL_ = nullptr, *modalBlkBR_ = nullptr,
                    *modalBlkCL_ = nullptr, *modalBlkCR_ = nullptr, *modalBlkDL_ = nullptr, *modalBlkDR_ = nullptr;
        bool modalNoteOnPending_ = false;
        // MODAL sample-as-exciter — the dropped one-shot rings THROUGH the physical model
        // ("noise into guitars"). Pinned per-osc at note-on so the borrowed read pointer the
        // engine holds stays valid for the note's lifetime (buffer swaps only take effect next note).
        tw::SampleBuffer::BufferPtr modalHeldBuf_[4];
        std::uint32_t sampleSprayRng_ = 0x12345u, spraySeedA_ = 0, spraySeedB_ = 0, spraySeedC_ = 0, spraySeedD_ = 0;
        // AIR exciter — per-voice/per-channel one-pole HP-split state + coefficient.
        float airSmA_ = 0.f, airSmB_ = 0.f, airSmC_ = 0.f, airSmD_ = 0.f;   // fb204 — glided AIR amounts
        float sampAirLpAL_ = 0.f, sampAirLpAR_ = 0.f, sampAirLpBL_ = 0.f, sampAirLpBR_ = 0.f,
              sampAirLpCL_ = 0.f, sampAirLpCR_ = 0.f, sampAirLpDL_ = 0.f, sampAirLpDR_ = 0.f;
        float airHpCoef_ = 0.37f;
        // SAMPLE WARP shaper — per-channel ADAA fold history (Fold mode only).
        FoldState sampWarpFoldAL_, sampWarpFoldAR_, sampWarpFoldBL_, sampWarpFoldBR_,
                  sampWarpFoldCL_, sampWarpFoldCR_, sampWarpFoldDL_, sampWarpFoldDR_;
        // RECTIFY DC-blocker — |x| has a nonzero mean → the Rectify warp injects a DC
        // offset that rides the amp env (low-freq "kick" + note-on/mode-switch step click).
        // One per-osc, per-channel 1-pole DC blocker, ON ONLY when that osc's warp == Rectify.
        // Wavetable path (slot-1 OR slot-2 == 9); Sample path (warpMode == 2 / Rectify).
        tw::filters::DCBlocker wtRectDcAL_, wtRectDcAR_, wtRectDcBL_, wtRectDcBR_,
                               wtRectDcCL_, wtRectDcCR_, wtRectDcDL_, wtRectDcDR_;
        tw::filters::DCBlocker spRectDcAL_, spRectDcAR_, spRectDcBL_, spRectDcBR_,
                               spRectDcCL_, spRectDcCR_, spRectDcDL_, spRectDcDR_;

        void renderSampleOsc (std::array<tw::SampleEngine, kMaxUnison>& engs, tw::WarpProcessor& warp,
                              const SampleEngineParams& p, bool isSamp,
                              int oct, int semi, float cent,
                              juce::AudioBuffer<float>& blk,
                              const float*& outL, const float*& outR,
                              int numSamples, std::uint32_t seed, bool doNoteOn, double nativeOverOut,
                              int uniCount, const float* detCents, const float* panL, const float* panR, float uNorm,
                              float level) noexcept
        {
            if (blk.getNumChannels() < 2 || blk.getNumSamples() < numSamples)
                blk.setSize (2, numSamples, false, false, true);
            float* wL = blk.getWritePointer (0);
            float* wR = blk.getWritePointer (1);
            outL = wL; outR = wR;
            if (! isSamp) return;
            // CPU: an osc at LEVEL 0 contributes exactly 0 to the sum (out × level_ × pan). Since
            // fb178 Level IS a mod dest (block-rate + fb180 glide): at exact 0 the glide has already
            // rung out below audibility, so the skip stays bit-safe. So skip
            // the whole tick/snap/region/warp render and just clear — bit-identical to ×0, but no
            // phase-vocoder etc. for a silent/unused sample osc. (Mute is separate: its gate is
            // per-sample smoothed, so it is NOT folded in here — only the true level knob at 0.)
            if (level <= 0.0f)
            {
                juce::FloatVectorOperations::clear (wL, numSamples);
                juce::FloatVectorOperations::clear (wR, numSamples);
                return;
            }
            if (! engs[0].hasSample())
            {
                juce::FloatVectorOperations::clear (wL, numSamples);
                juce::FloatVectorOperations::clear (wR, numSamples);
                return;
            }
            // pitch: root MIDI 60 = C3; resample ratio incl native/output SR
            const double noteSemis  = (double) (currentMidiNote_ - 60 + oct * 12 + semi) + (double) cent * 0.01;
            const double pitchRatio = nativeOverOut * std::pow (2.0, noteSemis / 12.0);
            const int    N          = juce::jlimit (1, kMaxUnison, uniCount);

            // params (modulatable — refreshed every block) applied to EVERY active unison voice.
            // Detune fans each voice by ±cents (the SAME table the wavetable unison uses); the
            // centre/count==1 voice always runs at the dry note pitch so count==1 is bit-identical.
            float ls = p.loopStart, le = p.loopEnd;
            if (p.snap == 1) { ls = engs[0].snapZeroCross01 (ls); le = engs[0].snapZeroCross01 (le); }
            for (int u = 0; u < N; ++u)
            {
                auto& e = engs[(size_t) u];
                // CPU: one combined region setter = ONE recomputeRegion() instead of 4 (setRegion/
                // setLoop/setXFade/setFades each recomputed; only the last survived). Bit-identical.
                e.setRegionParams (p.start, p.end, ls, le, p.xfade, p.fadeIn, p.fadeOut);
                e.setFadeCurves (p.fadeInCurve, p.fadeOutCurve);   // Ableton-style curve diamonds
                e.setLoopMode ((tw::SampleEngine::LoopMode) p.loopMode);
                e.setScan (p.scan);
                const double ratio = (N <= 1) ? pitchRatio
                                              : pitchRatio * std::pow (2.0, (double) detCents[u] / 1200.0);
                e.setPitchRatio (ratio);
                if (doNoteOn)
                {
                    // Decorrelate the OUTER unison voices' start positions at onset even when
                    // Spray=0 — otherwise all N voices begin byte-identical and sum COHERENTLY at
                    // the attack (RMS uNorm assumes decorrelation → up to +8.8 dB peak at 16 voices
                    // → clipping). Voice 0 stays clean: it drives the follower and keeps the dry
                    // transient. count==1 is untouched (vSpray=p.spray, vSeed=seed → bit-identical).
                    const std::uint32_t vSeed  = (N <= 1) ? seed : (seed ^ (0x9E3779B1u * (std::uint32_t) (u + 1)));
                    const float         vSpray = (N <= 1 || u == 0) ? p.spray : juce::jmax (p.spray, 0.04f);
                    e.noteOn (ratio, vSpray, vSeed);
                }
            }
            if (doNoteOn) warp.noteOnReset();

            // render — direct (resample) unless STRETCH/FORMANT engage the Warp (Tones) engine.
            // DEAD-ZONE (Max's CPU fix, 2026-07-01): the phase-vocoder is a hard on/off cliff, so a
            // barely-nudged knob used to spin up the FULL STFT for an INAUDIBLE shift. Snap tiny
            // values to neutral (skip the vocoder): stretch < 0.3 % (ratio ~1.009) and formant < 2 %
            // (~0.03 semitone) are inaudible, so stay on the cheap direct-resample path.
            const bool useWarp = (p.stretch > 0.003f) || (std::fabs (p.formant) > 0.02f);
            if (useWarp)
            {
                const tw::WarpMode wm = (p.stretchMode == 1) ? tw::WarpMode::Beats
                                      : (p.stretchMode == 2) ? tw::WarpMode::Texture
                                                             : tw::WarpMode::Tones;
                if (warp.getMode() != wm) { warp.setMode (wm); warp.noteOnReset(); }
                warp.setStretchRatio   (1.0f + p.stretch * 3.0f);     // 0 → 1x … 1 → 4x (slower; pitch held)
                warp.setPitchSemitones (0.0f);                        // note pitch already in the resampled read
                // FORMANT-MODE — reinterpret the FORMANT knob per creative mode (±2 octave shift).
                float fmFactor, fmTilt;
                const float fmAmp = 2.0f;
                switch (p.formantMode)
                {
                    case 1:  fmFactor = std::pow (2.0f, -p.formant * fmAmp); fmTilt = 0.f;        break;
                    case 2:  fmFactor = std::pow (2.0f,  p.formant * fmAmp); fmTilt = -p.formant; break;
                    case 3:  fmFactor = 1.0f;                                fmTilt =  p.formant; break;
                    default: fmFactor = std::pow (2.0f,  p.formant * fmAmp); fmTilt = 0.f;        break;
                }
                warp.setFormantFactor  (fmFactor);
                const int srcN = juce::jmax (1, warp.sourceSamplesPerBlock (numSamples));
                if (warpSrc_.getNumChannels() < 2 || warpSrc_.getNumSamples() < srcN)
                    warpSrc_.setSize (2, srcN, false, false, true);
                float* sL = warpSrc_.getWritePointer (0);
                float* sR = warpSrc_.getWritePointer (1);
                if (N <= 1)
                {
                    for (int k = 0; k < srcN; ++k) engs[0].tick (sL[k], sR[k]);
                }
                else
                {
                    // UNISON can't run 16 FFT phase-vocoders — sum the detuned reads into the warp
                    // SOURCE (detune + width survive), then warp ONCE. (Serum-class approach.)
                    juce::FloatVectorOperations::clear (sL, srcN);
                    juce::FloatVectorOperations::clear (sR, srcN);
                    for (int u = 0; u < N; ++u)
                    {
                        auto& e = engs[(size_t) u];
                        // PUNCH ANCHOR (Max: "unison turns shit down"): keep full-level voice(s) for
                        // the attack, RMS-normalise only the INNER bed — no 1/√N punch loss.
                        // fb256 — anchor the SYMMETRIC OUTER PAIR (voices 0 AND N-1), not voice 0 alone.
                        // Voice 0 is the LEFTMOST slot, so anchoring it alone pulled the image LEFT
                        // (Max's bug on ALL engines). A mirror pair is balanced → punchy AND centered.
                        const float gu = (u == 0 || u == N - 1) ? 1.0f : uNorm;
                        const float pl = panL[u] * gu, pr = panR[u] * gu;
                        for (int k = 0; k < srcN; ++k)
                        {
                            float l, r; e.tick (l, r);
                            const float m = 0.5f * (l + r);
                            sL[k] += m * pl; sR[k] += m * pr;
                        }
                    }
                }
                warp.process (sL, sR, wL, wR, numSamples);            // distinct in/out (Signalsmith requires)
                warp.processTilt (wL, wR, numSamples, fmTilt, sampleRate_);   // FORMANT-MODE — spectral tilt post-process
            }
            else if (N <= 1)
            {
                for (int k = 0; k < numSamples; ++k) engs[0].tick (wL[k], wR[k]);   // pre-unison path (bit-identical)
            }
            else
            {
                // UNISON — N detuned voices, each mono-collapsed then width-panned via the SAME
                // pan tables the wavetable unison uses, summed and RMS-normalised (loudness held).
                juce::FloatVectorOperations::clear (wL, numSamples);
                juce::FloatVectorOperations::clear (wR, numSamples);
                for (int u = 0; u < N; ++u)
                {
                    auto& e = engs[(size_t) u];
                    // fb256 — SYMMETRIC OUTER PAIR anchor (voices 0 AND N-1), see the warp-path
                    // comment above: anchoring voice 0 alone (leftmost slot) pulled the image LEFT.
                    // The mirror pair keeps the punch (no 1/√N loss) AND stays centered.
                    const float gu = (u == 0 || u == N - 1) ? 1.0f : uNorm;
                    const float pl = panL[u] * gu, pr = panR[u] * gu;
                    for (int k = 0; k < numSamples; ++k)
                    {
                        float l, r; e.tick (l, r);
                        const float m = 0.5f * (l + r);
                        wL[k] += m * pl; wR[k] += m * pr;
                    }
                }
            }
        }

        // BLEND MODES — Level-0 gate value for a block renderer. Normally the real level; bumped a
        // hair above 0 only when this osc feeds an armed FM/PD slot, so the engine still renders
        // (full amplitude) for the modulator tap. Output is NOT scaled by this (the true level_ is
        // applied later in the mix), so a forced-but-turned-down osc stays silent — it just modulates.
        float blkGateLevel (int o, float lvl) const noexcept
        {
            // A blend MODULATOR source must render even when Level-0 AND even when a FLOW mode
            // (round-robin / mute / solo) has gated this osc dead — otherwise round-robin starves
            // the modulator and the FM/PD silently stops. Output stays inaudible: the real level_
            // and the per-osc gate (gA..gD, which tracks round-robin) still zero it in the mix.
            if (modSrcForce_[o]) return juce::jmax (lvl, 1.0e-4f);
            return oscDead_[o] ? 0.0f : lvl;
        }

        // BLEND MODES — modulated re-read of a block engine's output = FM/PD ON a Sample/Granular/
        // Spec/Harmonic/Modal carrier. offCycles is the SAME per-carrier blend offset the WT/FM path
        // uses (PD = direct phase, FM = leaky-integrated → true frequency modulation); here it drives
        // a fractional read of a tiny per-osc delay ring, so the sample's read position wiggles and
        // the sidebands reflect the sample's own waveform. Runs ONLY while armed (or ramping out) —
        // an un-blended block osc never calls this, so it stays bit-identical to today.
        inline void blendReadBlock (int c, float offCycles, bool armed, float& L, float& R) noexcept
        {
            float* rL = blkRingL_[c]; float* rR = blkRingR_[c];
            const int w = blkRingW_[c];
            rL[w] = L; rR[w] = R;                                            // write the (shaped) block output
            blkArmSm_[c] += ((armed ? 1.0f : 0.0f) - blkArmSm_[c]) * kBlkArmCoef;   // declick delay in/out
            const float amt   = blkArmSm_[c];
            // fb523 — HARD CLAMP -> SOFT KNEE, and this is a REGRESSION FIX FOR MY OWN CHANGE,
            //  not a taste call. offCycles·160 saturates at ±256 samples, i.e. at 1.60 cycles of
            //  excursion. Under the old INDEX law the FM integrator reached 1.60 cycles at knob
            //  0.55, so a block-engine carrier had a 45 %-wide plateau. Under the new HZ law it
            //  reaches 1.60 cycles at knob 0.302 at C3 (Δf = 1,315 Hz) and 0.222 at C1 — a
            //  70 %-wide plateau, and a flat-topped ramp is discontinuity products, not depth.
            //  softBound is linear (bit-identical) below ±128 samples and asymptotic to ±256, so
            //  the ring geometry and its static_assert are untouched and the knob keeps
            //  developing all the way to 100 % on block engines too.
            //  ⚠️ This path is a SAMPLE-DELAY approximation of phase modulation and is bounded by
            //  the ring, so it can never be pitch-independent the way the wavetable path now is.
            //  Making it so is a separate build item, not a constant.
            const float offS  = softBound (offCycles * kBlkOffScale, 0.5f * (float) kBlkMaxOff, (float) kBlkMaxOff) * amt;
            const float delay = (float) kBlkMaxOff * amt;                    // base delay ramps in with the arm
            float rp = (float) w - delay + offS + (float) kBlkRing;          // read behind write (kept positive)
            int i0 = (int) rp; const float fr = rp - (float) i0;
            i0 &= (kBlkRing - 1); const int i1 = (i0 + 1) & (kBlkRing - 1);
            L = rL[i0] + (rL[i1] - rL[i0]) * fr;
            R = rR[i0] + (rR[i1] - rR[i0]) * fr;
            blkRingW_[c] = (w + 1) & (kBlkRing - 1);
        }

        void renderSampleBlocks (int numSamples) noexcept
        {
            if (engine_ != Engine::SAMP && engineB_ != Engine::SAMP
                && engineC_ != Engine::SAMP && engineD_ != Engine::SAMP)
                return;   // no sample oscillators → free no-op (common case)

            // PEROSC-VOICE — refresh each OSC's engines from its OWN buffer (independent samples).
            // Every unison instance shares the same buffer pointer (cheap; set only on change).
            std::array<tw::SampleEngine, kMaxUnison>* engs[4] = { &sampleEngA_, &sampleEngB_, &sampleEngC_, &sampleEngD_ };
            for (int o = 0; o < 4; ++o)
            {
                if (sampleSource_[o] == nullptr) continue;
                auto bp = sampleSource_[o]->load();
                if (bp.get() != sampleBufLast_[o])
                {
                    sampleHeldBuf_[o] = bp;
                    sampleBufLast_[o] = bp.get();
                    const int nCh = bp ? bp->getNumChannels() : 0;
                    const int nSm = bp ? bp->getNumSamples()  : 0;
                    const double nr = sampleSource_[o]->getSampleRate();
                    const float* const* rp = (bp && nSm > 0) ? bp->getArrayOfReadPointers() : nullptr;
                    for (auto& e : *engs[o]) e.setSample (rp, nCh, nSm, nr);
                    sampleNativeOverOut_[o] = (nr > 0.0 && sampleRate_ > 0.0) ? (nr / sampleRate_) : 1.0;
                }
            }
            const bool doOn = sampleNoteOnPending_;
            renderSampleOsc (sampleEngA_, sampleWarpA_, sampleParamsA_, engine_  == Engine::SAMP, octOffset_,  semiOffset_,  centsOffset_ + coarseModA_ * 100.f,  sampleBlkA_, sampBlkAL_, sampBlkAR_, numSamples, spraySeedA_, doOn, sampleNativeOverOut_[0], activeUnisonA_, uDetuneCentsA_.data(), uPanLA_.data(), uPanRA_.data(), uNormA_, blkGateLevel (0, level_));
            renderSampleOsc (sampleEngB_, sampleWarpB_, sampleParamsB_, engineB_ == Engine::SAMP, octOffsetB_, semiOffsetB_, centsOffsetB_ + coarseModB_ * 100.f, sampleBlkB_, sampBlkBL_, sampBlkBR_, numSamples, spraySeedB_, doOn, sampleNativeOverOut_[1], activeUnisonB_, uDetuneCentsB_.data(), uPanLB_.data(), uPanRB_.data(), uNormB_, blkGateLevel (1, levelB_));
            renderSampleOsc (sampleEngC_, sampleWarpC_, sampleParamsC_, engineC_ == Engine::SAMP, octOffsetC_, semiOffsetC_, centsOffsetC_ + coarseModC_ * 100.f, sampleBlkC_, sampBlkCL_, sampBlkCR_, numSamples, spraySeedC_, doOn, sampleNativeOverOut_[2], activeUnisonC_, uDetuneCentsC_.data(), uPanLC_.data(), uPanRC_.data(), uNormC_, blkGateLevel (2, levelC_));
            renderSampleOsc (sampleEngD_, sampleWarpD_, sampleParamsD_, engineD_ == Engine::SAMP, octOffsetD_, semiOffsetD_, centsOffsetD_ + coarseModD_ * 100.f, sampleBlkD_, sampBlkDL_, sampBlkDR_, numSamples, spraySeedD_, doOn, sampleNativeOverOut_[3], activeUnisonD_, uDetuneCentsD_.data(), uPanLD_.data(), uPanRD_.data(), uNormD_, blkGateLevel (3, levelD_));
            sampleNoteOnPending_ = false;
        }

        // ════════ GRANULAR-ENGINE-VOICE — render granular OSCs' stereo blocks ════════
        void renderGranularOsc (std::array<tw::GranularEngine, kMaxUnison>& engs,
                                const tw::GranularEngineParams& p, bool isGran,
                                int oct, int semi, float cent,
                                juce::AudioBuffer<float>& blk,
                                const float*& outL, const float*& outR,
                                int numSamples, std::uint32_t seed, bool doNoteOn,
                                double nativeOverOut, int uniCount, const float* uDetuneCents, float uNorm, float level) noexcept
        {
            if (blk.getNumChannels() < 2 || blk.getNumSamples() < numSamples)
                blk.setSize (2, numSamples, false, false, true);
            float* wL = blk.getWritePointer (0);
            float* wR = blk.getWritePointer (1);
            outL = wL; outR = wR;
            if (! isGran) return;   // CPU: this block is never read for a non-granular osc (outL/outR already point at it)
            if (level <= 0.0f || ! engs[0].hasSample())
            {
                juce::FloatVectorOperations::clear (wL, numSamples);
                juce::FloatVectorOperations::clear (wR, numSamples);
                return;
            }
            // Base pitch: root MIDI 60 = C3; resample ratio incl native/output SR (mirrors renderSampleOsc).
            const double noteSemis  = (double) (currentMidiNote_ - 60 + oct * 12 + semi) + (double) cent * 0.01;
            const double pitchRatio = nativeOverOut * std::pow (2.0, noteSemis / 12.0);
            const int    N          = juce::jlimit (1, kMaxUnison, uniCount);
            for (int u = 0; u < N; ++u)
            {
                auto& e = engs[(size_t) u];
                e.setParams (p);
                e.setRegion (p.regStart, p.regEnd);   // region handles now wired (start/end travel via params)
                // UNISON — per-voice detune so granular unison actually fattens (was flat: all one pitch)
                const double det = (uDetuneCents != nullptr) ? std::pow (2.0, (double) uDetuneCents[(size_t) u] / 1200.0) : 1.0;
                const double prU = pitchRatio * det;
                e.setPitchRatio (prU);
                if (doNoteOn)
                {
                    const std::uint32_t vSeed = (N <= 1) ? seed
                                                         : (seed ^ (0x9E3779B1u * (std::uint32_t) (u + 1)));
                    e.noteOn (prU, vSeed);
                }
            }
            // CPU: grain-major block render (renderBlockAdd) — was a per-sample tick() call per
            // engine. The engine adds into the cleared buffers; unison engines just stack.
            juce::FloatVectorOperations::clear (wL, numSamples);
            juce::FloatVectorOperations::clear (wR, numSamples);
            // VOICE-0-ANCHORED gain (matches renderSampleOsc): render the detuned BED voices
            // first and RMS-normalise them, then the dry voice 0 adds LAST at full level —
            // unison fattens AROUND the dry cloud instead of ducking it by 1/√N.
            for (int u = 1; u < N; ++u)
                engs[(size_t) u].renderBlockAdd (wL, wR, numSamples);
            if (N > 1)
            {
                juce::FloatVectorOperations::multiply (wL, uNorm, numSamples);
                juce::FloatVectorOperations::multiply (wR, uNorm, numSamples);
            }
            engs[0].renderBlockAdd (wL, wR, numSamples);
        }

        void renderGranularBlocks (int numSamples) noexcept
        {
            if (engine_ != Engine::GRAN && engineB_ != Engine::GRAN
                && engineC_ != Engine::GRAN && engineD_ != Engine::GRAN)
                return;   // no granular oscillators → free no-op (common case)

            // Refresh each granular OSC from its OWN buffer (the SAME buffers the Sample engine uses).
            // Pin the shared_ptr for the block so the buffer can't be freed mid-render (real-time safe).
            std::array<tw::GranularEngine, kMaxUnison>* engs[4] = { &granEngA_, &granEngB_, &granEngC_, &granEngD_ };
            const Engine oe[4] = { engine_, engineB_, engineC_, engineD_ };
            for (int o = 0; o < 4; ++o)
            {
                if (oe[o] != Engine::GRAN || sampleSource_[o] == nullptr) continue;
                auto bp = sampleSource_[o]->load();
                if (bp.get() != granBufLast_[o])
                {
                    granHeldBuf_[o] = bp;
                    granBufLast_[o] = bp.get();
                    const int nCh = bp ? bp->getNumChannels() : 0;
                    const int nSm = bp ? bp->getNumSamples()  : 0;
                    const double nr = sampleSource_[o]->getSampleRate();
                    const float* const* rp = (bp && nSm > 0) ? bp->getArrayOfReadPointers() : nullptr;
                    for (auto& e : *engs[o]) e.setSample (rp, nCh, nSm, nr);
                    granNativeOverOut_[o] = (nr > 0.0 && sampleRate_ > 0.0) ? (nr / sampleRate_) : 1.0;
                }
            }
            const bool doOn = granNoteOnPending_;
            renderGranularOsc (granEngA_, granParamsA_, engine_  == Engine::GRAN, octOffset_,  semiOffset_,  centsOffset_ + coarseModA_ * 100.f,  granBlkA_, granBlkAL_, granBlkAR_, numSamples, spraySeedA_, doOn, granNativeOverOut_[0], activeUnisonA_, uDetuneCentsA_.data(), uNormA_, blkGateLevel (0, level_));
            renderGranularOsc (granEngB_, granParamsB_, engineB_ == Engine::GRAN, octOffsetB_, semiOffsetB_, centsOffsetB_ + coarseModB_ * 100.f, granBlkB_, granBlkBL_, granBlkBR_, numSamples, spraySeedB_, doOn, granNativeOverOut_[1], activeUnisonB_, uDetuneCentsB_.data(), uNormB_, blkGateLevel (1, levelB_));
            renderGranularOsc (granEngC_, granParamsC_, engineC_ == Engine::GRAN, octOffsetC_, semiOffsetC_, centsOffsetC_ + coarseModC_ * 100.f, granBlkC_, granBlkCL_, granBlkCR_, numSamples, spraySeedC_, doOn, granNativeOverOut_[2], activeUnisonC_, uDetuneCentsC_.data(), uNormC_, blkGateLevel (2, levelC_));
            renderGranularOsc (granEngD_, granParamsD_, engineD_ == Engine::GRAN, octOffsetD_, semiOffsetD_, centsOffsetD_ + coarseModD_ * 100.f, granBlkD_, granBlkDL_, granBlkDR_, numSamples, spraySeedD_, doOn, granNativeOverOut_[3], activeUnisonD_, uDetuneCentsD_.data(), uNormD_, blkGateLevel (3, levelD_));
            granNoteOnPending_ = false;
        }

        // ════════ GEODE-ENGINE-VOICE — render SPEC oscillators' stereo blocks ════════
        // Mirrors renderGranularOsc: whole-block render into a per-osc buffer, voice-0-anchored
        // unison gain, publish const-float pointers the per-sample sum reads. The heavy analysis
        // lives in the shared GeodeFrameStore (processor, off-thread) — this is just resynthesis.
        void renderGeodeOsc (std::array<tw::GeodeEngine, kMaxUnison>& engs,
                             const tw::GeodeParams& p, bool isSpec,
                             int oct, int semi, float cent,
                             juce::AudioBuffer<float>& blk,
                             const float*& outL, const float*& outR,
                             int numSamples, std::uint32_t seed, bool doNoteOn,
                             int uniCount, const float* uDetuneCents, float uNorm, float level) noexcept
        {
            if (blk.getNumChannels() < 2 || blk.getNumSamples() < numSamples)
                blk.setSize (2, numSamples, false, false, true);
            float* wL = blk.getWritePointer (0);
            float* wR = blk.getWritePointer (1);
            outL = wL; outR = wR;
            if (! isSpec) return;   // CPU: this block is never read for a non-SPEC osc
            if (level <= 0.0f || ! engs[0].hasStore())
            {
                juce::FloatVectorOperations::clear (wL, numSamples);
                juce::FloatVectorOperations::clear (wR, numSamples);
                return;
            }
            // Resynthesis pitch = the played note's frequency (A4=440); partials scale by ratio.
            const double noteSemis = (double) (currentMidiNote_ - 69 + oct * 12 + semi) + (double) cent * 0.01;
            const double playedHz  = 440.0 * std::pow (2.0, noteSemis / 12.0);
            const int    N         = juce::jlimit (1, kMaxUnison, uniCount);
            for (int u = 0; u < N; ++u)
            {
                auto& e = engs[(size_t) u];
                e.setParams (p);
                e.setUnisonScale (N);   // CONSTANT-COST UNISON — N detuned banks cost ~one bank of partials
                const double det = (uDetuneCents != nullptr) ? std::pow (2.0, (double) uDetuneCents[(size_t) u] / 1200.0) : 1.0;
                e.setPlayedHz (playedHz * det);   // LIVE retune — Coarse/oct/semi/cent move mid-note (phase-continuous)
                if (doNoteOn)
                {
                    const std::uint32_t vSeed = (N <= 1) ? seed : (seed ^ (0x9E3779B1u * (std::uint32_t) (u + 1)));
                    e.noteOn (playedHz * det, vSeed);
                }
            }
            juce::FloatVectorOperations::clear (wL, numSamples);
            juce::FloatVectorOperations::clear (wR, numSamples);
            // CONSTANT-COST UNISON (rs7 CPU tighten): the ANCHOR prepares the sculpted bank ONCE
            // (head + smear + governor + sculpt + bitrate + children — identical for all siblings);
            // siblings ADOPT it and only render their own detuned sine banks. The anchor reserves
            // its partial budget while siblings render, so saturation thins siblings — never the core.
            if (! engs[0].prepareBank (numSamples)) return;
            engs[0].reserveBudget();
            for (int u = 1; u < N; ++u)
            {
                engs[(size_t) u].adoptBank (engs[0]);
                engs[(size_t) u].renderBankAdd (wL, wR, numSamples);
            }
            if (N > 1)
            {
                juce::FloatVectorOperations::multiply (wL, uNorm, numSamples);
                juce::FloatVectorOperations::multiply (wR, uNorm, numSamples);
            }
            engs[0].releaseBudget();
            engs[0].renderBankAdd (wL, wR, numSamples);
            // POST-SYNTH degrade (DRIVE soft-clip + CRUSH bit/rate) — once per osc, on the summed
            // unison signal, using voice-0's params (all unison instances share the same GeodeParams).
            engs[0].postProcess (wL, wR, numSamples);
        }

        void renderGeodeBlocks (int numSamples) noexcept
        {
            if (engine_ != Engine::SPEC && engineB_ != Engine::SPEC
                && engineC_ != Engine::SPEC && engineD_ != Engine::SPEC)
                return;   // no SPEC oscillators → free no-op (common case)

            std::array<tw::GeodeEngine, kMaxUnison>* engs[4] = { &geodeEngA_, &geodeEngB_, &geodeEngC_, &geodeEngD_ };
            const Engine oe[4] = { engine_, engineB_, engineC_, engineD_ };
            for (int o = 0; o < 4; ++o)
            {
                if (oe[o] != Engine::SPEC || geodeStoreSrc_[o] == nullptr) continue;
                const tw::GeodeFrameStore* st = geodeStoreSrc_[o]->load();
                if (st != geodeStoreLast_[o])
                {
                    geodeStoreLast_[o] = st;
                    for (auto& e : *engs[o]) e.setFrameStore (st);
                }
            }
            const bool doOn = geodeNoteOnPending_;
            renderGeodeOsc (geodeEngA_, geodeParamsA_, engine_  == Engine::SPEC, octOffset_,  semiOffset_,  centsOffset_ + coarseModA_ * 100.f,  geodeBlkA_, geodeBlkAL_, geodeBlkAR_, numSamples, spraySeedA_, doOn, activeUnisonA_, uDetuneCentsA_.data(), uNormA_, blkGateLevel (0, level_));
            renderGeodeOsc (geodeEngB_, geodeParamsB_, engineB_ == Engine::SPEC, octOffsetB_, semiOffsetB_, centsOffsetB_ + coarseModB_ * 100.f, geodeBlkB_, geodeBlkBL_, geodeBlkBR_, numSamples, spraySeedB_, doOn, activeUnisonB_, uDetuneCentsB_.data(), uNormB_, blkGateLevel (1, levelB_));
            renderGeodeOsc (geodeEngC_, geodeParamsC_, engineC_ == Engine::SPEC, octOffsetC_, semiOffsetC_, centsOffsetC_ + coarseModC_ * 100.f, geodeBlkC_, geodeBlkCL_, geodeBlkCR_, numSamples, spraySeedC_, doOn, activeUnisonC_, uDetuneCentsC_.data(), uNormC_, blkGateLevel (2, levelC_));
            renderGeodeOsc (geodeEngD_, geodeParamsD_, engineD_ == Engine::SPEC, octOffsetD_, semiOffsetD_, centsOffsetD_ + coarseModD_ * 100.f, geodeBlkD_, geodeBlkDL_, geodeBlkDR_, numSamples, spraySeedD_, doOn, activeUnisonD_, uDetuneCentsD_.data(), uNormD_, blkGateLevel (3, levelD_));
            geodeNoteOnPending_ = false;
        }

        // ════════ HARMONIC-ENGINE-VOICE — render HARM oscillators' stereo blocks ════════
        // Mirrors renderGeodeOsc minus the frame store: the spectrum is procedural (built from
        // HarmParams every block). Constant-cost unison: the ANCHOR builds the sculpted bank
        // once; siblings adopt the pointer and render their own detuned phase sets. Pitch is
        // pushed LIVE every block (setPlayedHz) so oct/semi/cents moves retune mid-note.
        void renderHarmonicOsc (std::array<tw::HarmonicEngine, kMaxUnison>& engs,
                                const tw::HarmParams& p, bool isHarm,
                                int oct, int semi, float cent,
                                juce::AudioBuffer<float>& blk,
                                const float*& outL, const float*& outR,
                                int numSamples, std::uint32_t seed, bool doNoteOn,
                                int uniCount, const float* uDetuneCents, float uNorm, float level) noexcept
        {
            if (blk.getNumChannels() < 2 || blk.getNumSamples() < numSamples)
                blk.setSize (2, numSamples, false, false, true);
            float* wL = blk.getWritePointer (0);
            float* wR = blk.getWritePointer (1);
            outL = wL; outR = wR;
            if (! isHarm) return;   // CPU: this block is never read for a non-HARM osc
            if (level <= 0.0f)
            {
                juce::FloatVectorOperations::clear (wL, numSamples);
                juce::FloatVectorOperations::clear (wR, numSamples);
                return;
            }
            const double noteSemis = (double) (currentMidiNote_ - 69 + oct * 12 + semi) + (double) cent * 0.01;
            const double playedHz  = 440.0 * std::pow (2.0, noteSemis / 12.0);
            const int    N         = juce::jlimit (1, kMaxUnison, uniCount);
            for (int u = 0; u < N; ++u)
            {
                auto& e = engs[(size_t) u];
                e.setParams (p);        // EVERY sibling: noteOn reads phase policy (Hornet buzz)
                e.setUnisonScale (N);   // …and arms uniScatCents_ so scatMul_ actually fills
                const double det = (uDetuneCents != nullptr) ? std::pow (2.0, (double) uDetuneCents[(size_t) u] / 1200.0) : 1.0;
                e.setPlayedHz (playedHz * det);
                if (doNoteOn)
                {
                    const std::uint32_t vSeed = (N <= 1) ? seed : (seed ^ (0x9E3779B1u * (std::uint32_t) (u + 1)));
                    e.noteOn (playedHz * det, vSeed);
                }
            }
            juce::FloatVectorOperations::clear (wL, numSamples);
            juce::FloatVectorOperations::clear (wR, numSamples);
            if (! engs[0].prepareBank (numSamples)) return;
            engs[0].reserveBudget();
            for (int u = 1; u < N; ++u)
            {
                engs[(size_t) u].adoptBank (engs[0]);
                engs[(size_t) u].renderBankAdd (wL, wR, numSamples);
            }
            if (N > 1)
            {
                juce::FloatVectorOperations::multiply (wL, uNorm, numSamples);
                juce::FloatVectorOperations::multiply (wR, uNorm, numSamples);
            }
            engs[0].releaseBudget();
            engs[0].renderBankAdd (wL, wR, numSamples);
            // FORGE (hm6) — analog saturation on the summed unison signal, once per osc,
            // anchor-owned state (same placement as Geode's postProcess drive).
            engs[0].postProcess (wL, wR, numSamples);
        }

        void renderHarmonicBlocks (int numSamples) noexcept
        {
            if (engine_ != Engine::HARM && engineB_ != Engine::HARM
                && engineC_ != Engine::HARM && engineD_ != Engine::HARM)
                return;   // no HARM oscillators → free no-op (common case)
            const bool doOn = harmNoteOnPending_;
            // fb517 — ONE acquire load, paired with prepareHarmonicEngines(). Same law as the
            // modal gate: THE GATE GOES ON *level*, NOT ON *isHarm* — renderHarmonicOsc's
            // `if (! isHarm) return;` deliberately leaves blk uninitialised, and the downstream
            // consumer keys off engine_. Forcing level 0 takes the path that CLEARS both
            // channels and returns without touching a single (possibly unarmed) engine.
            // blkGateLevel is still called either way so per-osc mute-fade state cannot desync.
            const bool  hReady = harmReady_.load (std::memory_order_acquire);
            const float hLvA = blkGateLevel (0, level_),  hLvB = blkGateLevel (1, levelB_);
            const float hLvC = blkGateLevel (2, levelC_), hLvD = blkGateLevel (3, levelD_);
            renderHarmonicOsc (harmEngA_, harmParamsA_, engine_  == Engine::HARM, octOffset_,  semiOffset_,  centsOffset_ + coarseModA_ * 100.f,  harmBlkA_, harmBlkAL_, harmBlkAR_, numSamples, spraySeedA_, doOn, activeUnisonA_, uDetuneCentsA_.data(), uNormA_, hReady ? hLvA : 0.0f);
            renderHarmonicOsc (harmEngB_, harmParamsB_, engineB_ == Engine::HARM, octOffsetB_, semiOffsetB_, centsOffsetB_ + coarseModB_ * 100.f, harmBlkB_, harmBlkBL_, harmBlkBR_, numSamples, spraySeedB_, doOn, activeUnisonB_, uDetuneCentsB_.data(), uNormB_, hReady ? hLvB : 0.0f);
            renderHarmonicOsc (harmEngC_, harmParamsC_, engineC_ == Engine::HARM, octOffsetC_, semiOffsetC_, centsOffsetC_ + coarseModC_ * 100.f, harmBlkC_, harmBlkCL_, harmBlkCR_, numSamples, spraySeedC_, doOn, activeUnisonC_, uDetuneCentsC_.data(), uNormC_, hReady ? hLvC : 0.0f);
            renderHarmonicOsc (harmEngD_, harmParamsD_, engineD_ == Engine::HARM, octOffsetD_, semiOffsetD_, centsOffsetD_ + coarseModD_ * 100.f, harmBlkD_, harmBlkDL_, harmBlkDR_, numSamples, spraySeedD_, doOn, activeUnisonD_, uDetuneCentsD_.data(), uNormD_, hReady ? hLvD : 0.0f);
            harmNoteOnPending_ = false;
        }

        // ── MODAL-ENGINE-VOICE — block-render clone of renderHarmonicOsc (physical models are
        // stateful/independent: each unison sibling runs its own core, detuned; no shared bank) ──
        void renderModalOsc (std::array<tw::ModalEngine, kMaxUnison>& engs,
                             const tw::ModalParams& p, bool isModal,
                             int oct, int semi, float cent,
                             juce::AudioBuffer<float>& blk,
                             const float*& outL, const float*& outR,
                             int numSamples, std::uint32_t seed, bool doNoteOn,
                             int uniCount, const float* uDetuneCents, float uNorm, float level,
                             const float* exData, int exLen, double exRate) noexcept
        {
            if (blk.getNumChannels() < 2 || blk.getNumSamples() < numSamples)
                blk.setSize (2, numSamples, false, false, true);
            float* wL = blk.getWritePointer (0);
            float* wR = blk.getWritePointer (1);
            outL = wL; outR = wR;
            if (! isModal) return;   // CPU: this block is never read for a non-MODAL osc
            if (level <= 0.0f)
            {
                juce::FloatVectorOperations::clear (wL, numSamples);
                juce::FloatVectorOperations::clear (wR, numSamples);
                return;
            }
            const double noteSemis = (double) (currentMidiNote_ - 69 + oct * 12 + semi) + (double) cent * 0.01;
            const double playedHz  = 440.0 * std::pow (2.0, noteSemis / 12.0);
            const int    N         = juce::jlimit (1, kMaxUnison, uniCount);
            const float  vel       = juce::jlimit (0.02f, 1.0f, currentVelocity_);
            for (int u = 0; u < N; ++u)
            {
                auto& e = engs[(size_t) u];
                e.setParams (p);
                e.setUnisonScale (N);
                e.setExciterSample (exData, exLen, exRate);   // dropped one-shot → the strike
                const double det = (uDetuneCents != nullptr) ? std::pow (2.0, (double) uDetuneCents[(size_t) u] / 1200.0) : 1.0;
                e.setPlayedHz (playedHz * det);
                if (doNoteOn)
                {
                    const std::uint32_t vSeed = (N <= 1) ? seed : (seed ^ (0x9E3779B1u * (std::uint32_t) (u + 1)));
                    e.noteOn (playedHz * det, vSeed, vel);
                }
            }
            juce::FloatVectorOperations::clear (wL, numSamples);
            juce::FloatVectorOperations::clear (wR, numSamples);
            engs[0].reserveBudget();
            for (int u = 1; u < N; ++u)
            {
                engs[(size_t) u].renderBankAdd (wL, wR, numSamples);   // each sibling is a full independent voice
            }
            if (N > 1)
            {
                juce::FloatVectorOperations::multiply (wL, uNorm, numSamples);
                juce::FloatVectorOperations::multiply (wR, uNorm, numSamples);
            }
            engs[0].releaseBudget();
            engs[0].renderBankAdd (wL, wR, numSamples);
        }

        void renderModalBlocks (int numSamples) noexcept
        {
            if (engine_ != Engine::MODAL && engineB_ != Engine::MODAL
                && engineC_ != Engine::MODAL && engineD_ != Engine::MODAL)
                return;   // no MODAL oscillators → free no-op (common case)
            const bool doOn = modalNoteOnPending_;
            // Pin each osc's dropped sample at note-on so the modal exciter can ring it through
            // the instrument. The read pointer stays valid for the whole note (swaps take next note).
            const float* exL[4] = { nullptr, nullptr, nullptr, nullptr };
            int    exN[4] = { 0, 0, 0, 0 };
            double exR[4] = { sampleRate_, sampleRate_, sampleRate_, sampleRate_ };
            for (int o = 0; o < 4; ++o)
            {
                if (doOn && sampleSource_[o] != nullptr)
                    modalHeldBuf_[o] = sampleSource_[o]->load();   // pin the current buffer for this note
                const auto& buf = modalHeldBuf_[o];
                if (buf != nullptr && buf->getNumSamples() > 1)
                {
                    exL[o] = buf->getReadPointer (0);
                    exN[o] = buf->getNumSamples();
                    if (sampleSource_[o] != nullptr && sampleSource_[o]->getSampleRate() > 1000.0)
                        exR[o] = sampleSource_[o]->getSampleRate();
                }
            }
            // fb498 — ONE acquire load, paired with the release store in prepareModalEngines().
            // While false the six DelayA lines inside every engine are still EMPTY vectors, so a
            // MODAL osc must not reach them.
            //
            // ⚠️ THE GATE GOES ON *level*, NOT ON *isModal*. renderModalOsc's `if (! isModal)
            // return;` (see its body) deliberately leaves `blk` UNINITIALISED — "this block is
            // never read for a non-MODAL osc". That contract holds only while `isModal` means
            // exactly `engine_ == Engine::MODAL`, because the DOWNSTREAM consumer keys off
            // engine_ too. Passing `mReady && engine_ == MODAL` broke the pair: the consumer
            // still read the block while the producer had bailed, and an unarmed MODAL osc
            // emitted GARBAGE — measured as NaN out of the plugin, silent and poisonous.
            // Forcing the level to 0 instead takes renderModalOsc's `level <= 0` path, which
            // CLEARS both channels and returns without touching a single engine. blkGateLevel is
            // still called either way so its per-osc mute-fade state cannot desync.
            const bool  mReady = modalReady_.load (std::memory_order_acquire);
            const float mLvA = blkGateLevel (0, level_),  mLvB = blkGateLevel (1, levelB_);
            const float mLvC = blkGateLevel (2, levelC_), mLvD = blkGateLevel (3, levelD_);
            renderModalOsc (modalEngA_, modalParamsA_, engine_  == Engine::MODAL, octOffset_,  semiOffset_,  centsOffset_ + coarseModA_ * 100.f,  modalBlkA_, modalBlkAL_, modalBlkAR_, numSamples, spraySeedA_, doOn, activeUnisonA_, uDetuneCentsA_.data(), uNormA_, mReady ? mLvA : 0.0f, exL[0], exN[0], exR[0]);
            renderModalOsc (modalEngB_, modalParamsB_, engineB_ == Engine::MODAL, octOffsetB_, semiOffsetB_, centsOffsetB_ + coarseModB_ * 100.f, modalBlkB_, modalBlkBL_, modalBlkBR_, numSamples, spraySeedB_, doOn, activeUnisonB_, uDetuneCentsB_.data(), uNormB_, mReady ? mLvB : 0.0f, exL[1], exN[1], exR[1]);
            renderModalOsc (modalEngC_, modalParamsC_, engineC_ == Engine::MODAL, octOffsetC_, semiOffsetC_, centsOffsetC_ + coarseModC_ * 100.f, modalBlkC_, modalBlkCL_, modalBlkCR_, numSamples, spraySeedC_, doOn, activeUnisonC_, uDetuneCentsC_.data(), uNormC_, mReady ? mLvC : 0.0f, exL[2], exN[2], exR[2]);
            renderModalOsc (modalEngD_, modalParamsD_, engineD_ == Engine::MODAL, octOffsetD_, semiOffsetD_, centsOffsetD_ + coarseModD_ * 100.f, modalBlkD_, modalBlkDL_, modalBlkDR_, numSamples, spraySeedD_, doOn, activeUnisonD_, uDetuneCentsD_.data(), uNormD_, mReady ? mLvD : 0.0f, exL[3], exN[3], exR[3]);
            modalNoteOnPending_ = false;
        }

        Engine               engine_           = Engine::WT;

        // Phase 3 — NOISE engine state.
        // xorshift32 PRNG seeded per-voice from a const offset XOR'd with the
        // voice's `this` pointer so each voice has decorrelated noise streams.
        // One-pole low-pass for "color" (FRAME=0 → bright/white, FRAME=1 →
        // dark/brown). Tanh post-saturation for "drive" (WARP AMT).
        std::uint32_t noiseState_  = 0x9E3779B9u
                                   ^ static_cast<std::uint32_t> (
                                         reinterpret_cast<std::uintptr_t> (this));
        float         noiseLpZ_    = 0.0f;

        // ── Phase 9 — OSC B state (mirror of OSC A, B suffix on each) ────
        float  levelB_          = 0.5f;      // default lower than A so they sum tastefully
        float  panLB_           = 0.7071f;   // cos(pi/4) — center
        float  panRB_           = 0.7071f;   // sin(pi/4) — center
        float  panLBT_ = 0.7071f, panRBT_ = 0.7071f;   // fb202 — glide targets
        int    octOffsetB_      = 0;
        int    semiOffsetB_     = 0;
        float  centsOffsetB_    = 0.0f;
        const tw::Wavetable* currentWavetableB_ = nullptr;
        float  framePosB_       = 0.0f;
        int    warpModeB_       = 0;
        // WARP 2 — second chained warp slot per OSC (same mode list as slot 1).
        // base→effective: amounts copied per-block, mod-matrix ready.
        int    warp2ModeA_       = 0;
        int    warp2ModeB_       = 0;
        float  warp2AmountA_     = 0.0f;
        // fb204 — GLIDE TARGETS for the wavetable trio + unison tables: the block prologue
        // writes targets; the render loop moves the live values (no block steps anywhere).
        float  framePosT_ = 0.0f, framePosTB_ = 0.0f, framePosTC_ = 0.0f, framePosTD_ = 0.0f;
        float  warpAmtT_ = 0.0f, warpAmtTB_ = 0.0f, warpAmtTC_ = 0.0f, warpAmtTD_ = 0.0f;
        float  warp2AmtTA_ = 0.0f, warp2AmtTB_ = 0.0f, warp2AmtTC_ = 0.0f, warp2AmtTD_ = 0.0f;
        float  foldAmtTA_ = 0.0f, foldAmtTB_ = 0.0f, foldAmtTC_ = 0.0f, foldAmtTD_ = 0.0f;
        float  foldStepA_ = 0.0f, foldStepB_ = 0.0f, foldStepC_ = 0.0f, foldStepD_ = 0.0f;
        std::array<float, kMaxUnison> uPanLTA_ { 0.7071f }, uPanRTA_ { 0.7071f }, uPanLTB_ { 0.7071f }, uPanRTB_ { 0.7071f };
        std::array<float, kMaxUnison> uPanLTC_ { 0.7071f }, uPanRTC_ { 0.7071f }, uPanLTD_ { 0.7071f }, uPanRTD_ { 0.7071f };
        float  uNormTA_ = 1.0f, uNormTB_ = 1.0f, uNormTC_ = 1.0f, uNormTD_ = 1.0f;
        bool   uniSnapA_ = false, uniSnapB_ = false, uniSnapC_ = false, uniSnapD_ = false;
        float  warp2AmountB_     = 0.0f;
        float  warp2AmountBaseA_ = 0.0f;
        float  warp2AmountBaseB_ = 0.0f;
        int currentMipLevelA_ = 0;   // Phase 10a — refreshed per block from uPhaseIncA_[0]
        int currentMipLevelB_ = 0;   // Phase 10a — refreshed per block from uPhaseIncB_[0]
        float  warpAmountB_     = 0.0f;
        Engine engineB_         = Engine::WT;
        // xorshift32 PRNG for OSC B — seeded with sqrt(2) fractional constant
        // (0x6A09E667) XOR'd with this pointer so OSC B has a decorrelated noise
        // stream from OSC A (which uses the golden-ratio constant 0x9E3779B9).
        std::uint32_t noiseStateB_ = 0x6A09E667u
                                   ^ static_cast<std::uint32_t> (
                                         reinterpret_cast<std::uintptr_t> (this));
        float  noiseLpZB_       = 0.0f;

        // ── Phase 8b — Unison-in-voice state (per-sine arrays) ──────────
        // Each unison sub-voice u in [0, count) has its own pitch state.
        // The single voice now renders all UNISON sines internally.
        std::array<double, kMaxUnison> uPhaseA_       {};
        std::array<double, kMaxUnison> uPhaseIncA_    {};
        std::array<double, kMaxUnison> uModPhaseA_    {};
        std::array<double, kMaxUnison> uSyncPhaseA_   {};

        std::array<double, kMaxUnison> uPhaseB_       {};
        std::array<double, kMaxUnison> uPhaseIncB_    {};
        std::array<double, kMaxUnison> uModPhaseB_    {};
        std::array<double, kMaxUnison> uSyncPhaseB_   {};

        // ── FM-ENGINE-VOICE — wavetable-carrier FM (per-osc, indexed 0..3 = A..D) ──
        // M1 phase reuses uModPhase*_; M2 gets its own accumulator; fmFb*_ is M1's
        // averaged self-feedback memory. Depth/feedback smoothed at block rate.
        std::array<int, 4>   fmAlgo_   {};
        std::array<float, 4> fmRatio1_ { 1.0f, 1.0f, 1.0f, 1.0f };
        std::array<float, 4> fmDepth1_ {};
        std::array<float, 4> fmRatio2_ { 2.0f, 2.0f, 2.0f, 2.0f };
        std::array<float, 4> fmDepth2_ {};
        std::array<float, 4> fmFbAmt_  {};
        std::array<float, 4> fmD1Sm_ {}, fmD2Sm_ {}, fmFbSm_ {};
        std::array<double, kMaxUnison> uMod2PhaseA_ {}, uMod2PhaseB_ {}, uMod2PhaseC_ {}, uMod2PhaseD_ {};
        std::array<float, kMaxUnison>  fmFbA_ {}, fmFbB_ {}, fmFbC_ {}, fmFbD_ {};
        // ── FM WEATHERING SUITE (page 2) — knob targets + smoothed + slow-process state ──
        std::array<float, 4> fmStrike_ {}, fmAge_ {}, fmRust_ {}, fmQuakeKnob_ {}, fmScorchKnob_ {}, fmStorm_ {};
        std::array<float, 4> fmStrikeSm_ {}, fmAgeSm_ {}, fmRustSm_ {}, fmQuakeSm_ {}, fmScorchSm_ {}, fmStormSm_ {};
        std::array<float, 4> fmStrikeEnv_ {};                       // note-on index transient, exp decay
        std::array<float, 4> fmAgeOuR_ {}, fmAgeOuI_ {};            // AGE — OU walks (ratio / index)
        std::array<float, 4> fmAgeNote_ {};                         // AGE — per-note S&H offset (±1)
        std::array<double, kMaxUnison> fmQuakePhaseA_ {}, fmQuakePhaseB_ {}, fmQuakePhaseC_ {}, fmQuakePhaseD_ {};  // QUAKE — per-voice subharmonic phase
        // block-rate EFFECTIVE values the per-sample core reads (all pow()/exp() here)
        std::array<float, 4>  fmD1Eff_ {}, fmD2Eff_ {}, fmFbEff_ {};
        // AGE DE-ZIPPER — the OU walk resteps fmD1Eff_/fmD2Eff_ every block; because the carrier does
        // cPh = phase + d1·m1, a per-block index step is a phase discontinuity = a click train (crackle).
        // Glide the applied index toward the block target per-SAMPLE (~1.2ms) so the step can't click.
        std::array<float, 4>  fmD1Now_ {}, fmD2Now_ {};
        // fb204 — the whole FM back panel rides the same de-zipper: feedback, STORM couples,
        // QUAKE idx/fry, and the SCORCH shaper factors all stepped at block rate (confirmed
        // zipper). Each glides toward its block-computed Eff value per sample.
        std::array<float, 4>  fmFbNow_ {}, fmStormM12Now_ {}, fmStormM21Now_ {};
        std::array<float, 4>  fmQuakeIdxNow_ {}, fmQuakeFryNow_ {};
        std::array<float, 4>  fmScorchIdxMulNow_ { 1.0f, 1.0f, 1.0f, 1.0f };
        std::array<float, 4>  fmScorchPreNow_ { 1.0f, 1.0f, 1.0f, 1.0f }, fmScorchMakeupNow_ { 1.0f, 1.0f, 1.0f, 1.0f };
        std::array<float, 4>  fmScorchBiasNow_ {}, fmScorchTanhBiasNow_ {};
        float fmIdxGlideCoef_ { 0.02f };
        std::array<double, 4> fmR1Eff_ { 1.0, 1.0, 1.0, 1.0 }, fmR2Eff_ { 2.0, 2.0, 2.0, 2.0 };
        std::array<double, 4> fmRustTps_ {};                        // RUST — abs-Hz offset in turns/sample
        // QUAKE — block coeffs (subharmonic FM): octave-anchored ratio, index (turns), fry amount.
        std::array<float, 4>  fmQuakeSubRatio_ { 0.5f, 0.5f, 0.5f, 0.5f }, fmQuakeIdx_ {}, fmQuakeFry_ {};
        // SCORCH — block coeffs (in-loop drive): shaper pre-gain, peak makeup, asymmetry bias + its
        // tanh (DC re-center), and the FM index push applied to the carrier depths.
        std::array<float, 4>  fmScorchPre_ { 1.0f, 1.0f, 1.0f, 1.0f }, fmScorchMakeup_ { 1.0f, 1.0f, 1.0f, 1.0f };
        std::array<float, 4>  fmScorchBias_ {}, fmScorchTanhBias_ {}, fmScorchIdxMul_ { 1.0f, 1.0f, 1.0f, 1.0f };
        std::array<float, 4>  fmFxMipAdd_ {};                       // SCORCH+QUAKE added carrier bandwidth for the mip picker
        std::array<float, 4>  fmStormM12_ {}, fmStormM21_ {};       // STORM cross-couple depths (M1↔M2)
        std::array<float, kMaxUnison> fmPrevM1A_ {}, fmPrevM1B_ {}, fmPrevM1C_ {}, fmPrevM1D_ {};  // STORM one-sample cross memory
        std::uint32_t fmNz_ { 0x9E3779B9u };                        // AGE / weathering RNG (per voice)
        float fmKs_ { 1.0f };                                        // DX key scaling — index rolloff above C5

        // ── KEYTRACK — note→destination modulation (the mod-matrix embryo) ──────────
        //   The render path keeps reading the EFFECTIVE members (framePos_/warpAmount_/
        //   foldAmount*_). The setters now write the BASE members below; at render entry
        //   effective = base + (selected ? depth·ktRamp_ : 0), clamped. Destinations are
        //   only the per-voice-modulatable timbre params (SPECTRAL is off-thread; sample
        //   START has no per-OSC param yet — both drop in here when ready).
        enum { kKtFrame = 0, kKtWarp = 1, kKtFold = 2 };
        static constexpr int kKtLowNote  = 36;    // C1 — ramp anchor (0 here = "regular", no tracking)
        static constexpr int kKtHighNote = 96;    // C6 — full sweep (+1.0 × depth) at the top of the range
        float ktDepthA_ = 0.0f, ktDepthB_ = 0.0f; // 0..1 depth per OSC (per-block from APVTS/100)
        int   ktDestA_  = 0,    ktDestB_  = 0;     // 0=FRAME,1=WARP,2=FOLD
        float ktRamp_   = 0.0f;                    // note-pitch source, latched per voice at note-on
        float framePosBase_    = 0.0f, framePosBaseB_   = 0.0f;   // knob bases (keytrack adds onto these)
        float warpAmountBase_  = 0.0f, warpAmountBaseB_ = 0.0f;
        float foldAmountBaseA_ = 0.0f, foldAmountBaseB_ = 0.0f;

        // ── ROUTE — mod route #2 (the generalized slot, back panel pill 4) ──────────
        //   Source 0=Note ramp (reuses ktRamp_), 1=Velocity. Dest 0=FRAME,1=WARP,2=FOLD,
        //   3=CUT1,4=CUT2. Amount BIPOLAR. FRAME/WARP/FOLD accumulate into the effective
        //   members alongside KEYTRACK (resolved at render entry); CUT1/CUT2 resolve into
        //   routeCut{1,2}Semis_ — a semitone offset added into the per-sample filter
        //   cutoff (musical, additive in semitone space). Both OSC routes may target one
        //   shared filter cutoff, so they sum.
        enum { kRtFrame = 0, kRtWarp = 1, kRtFold = 2 };   // CUT1/CUT2 removed — filter routing didn't work (filters act on the OSC sum)
        enum { kRtSrcNote = 0, kRtSrcVel = 1 };
        int   routeSrcA_  = 0,    routeSrcB_  = 0;         // 0=Note, 1=Velocity
        int   routeDestA_ = 0,    routeDestB_ = 0;         // 0=FRAME,1=WARP,2=FOLD
        static constexpr float kRtNoteCurve = 3.0f;        // Note-source shaping: bottom half stays closed, blooms up top
        float routeAmtA_  = 0.0f, routeAmtB_  = 0.0f;      // bipolar -1..+1 (per-block from APVTS/100)

        // Phase 11a — per-sine WT frame position (centre = framePos_, offset = SPREAD × u_norm × 0.5).
        // Render path wraps to [0,1] before wavetable lookup.
        std::array<float, kMaxUnison> uFramePosA_   {};
        std::array<float, kMaxUnison> uFramePosB_   {};

        // Phase 11a — SPREAD amount per OSC (0..1, pushed per-block from APVTS).
        float frameSpreadA01_ = 0.0f;
        float frameSpreadB01_ = 0.0f;

        // ── WT BLUR (frame blend) — repurposes the old FRAME_SPREAD knob ─────────
        // The param sets blurTarget*; it's smoothed into blur* per block for clickless
        // changes; blend* hold the pre-built blended single-cycle buffer that every
        // unison sine reads via Wavetable::readCycle. last* gate rebuilds to "on change".
        // blur 0 ⇒ renderBlend reproduces the old bilinear lookup exactly.
        float blurTargetA_ = 0.0f, blurTargetB_ = 0.0f;
        float blurA_ = 0.0f, blurB_ = 0.0f;
        std::array<float, tw::Wavetable::kFrameSize> blendA_ {};
        std::array<float, tw::Wavetable::kFrameSize> blendB_ {};
        // fb248 — FRAME-MOVE CROSSFADE: the per-block blend cache only rebuilds when the frame position
        // changes, so a moving WT Pos steps the read waveform at block boundaries = clicks (worse on
        // detailed tables). Hold the PREVIOUS block's blend + a "crossfade this block" flag, and glide
        // the read prev→new across the block (a per-sample lerp). Seamless sweeps + LFO/env-safe on WT Pos.
        std::array<float, tw::Wavetable::kFrameSize> blendPrevA_ {}, blendPrevB_ {}, blendPrevC_ {}, blendPrevD_ {};
        bool blendXfA_ = false, blendXfB_ = false, blendXfC_ = false, blendXfD_ = false;   // crossfade THIS block?
        bool blendValidA_ = false, blendValidB_ = false, blendValidC_ = false, blendValidD_ = false;   // a real previous blend exists (skip the very first render)
        static inline float wtBlendRead (const float* cur, const float* prev, bool xf, float frac, float ph) noexcept
        {   // frac: 0 at block start → prev (continuous with last block), 1 at block end → new
            const float c = tw::Wavetable::readCycle (cur, ph);
            return xf ? (c + (tw::Wavetable::readCycle (prev, ph) - c) * (1.0f - frac)) : c;
        }
        float lastFpA_ = -2.0f, lastBlurA_ = -2.0f; int lastMipA_ = -2; int lastEpochA_ = -1;
        float lastFpB_ = -2.0f, lastBlurB_ = -2.0f; int lastMipB_ = -2; int lastEpochB_ = -1;
        // The source table pointer is ALSO a blend dependency: Spectral Morph and live
        // preset changes swap currentWavetable_ with frame/blur/mip unchanged, so the
        // gate must watch the pointer too or the blend keeps stale (pre-morph) bytes.
        const tw::Wavetable* lastWtA_ = nullptr;
        const tw::Wavetable* lastWtB_ = nullptr;

        // ── PHASE (note-on phase-init mode) ──────────────────────────────────────
        // 0=RETRIG, 1=FREE, 2=RANDOM (default), 3=SPREAD. phaseSeeded_ guards the
        // one-time FREE seed; phaseRng_ is the per-voice xorshift state for RANDOM.
        int phaseModeA_ = 2, phaseModeB_ = 2;
        bool phaseSeeded_ = false;
        std::uint32_t phaseRng_ = 0u;

        // Phase 11d — FOLD state (per OSC).
        int   foldShapeA_   = 0;     // 0=Linear, 1=Sine, 2=Triangle
        float foldAmountA_  = 0.0f;
        int   foldShapeB_   = 0;
        float foldAmountB_  = 0.0f;
        // Phase 11d ADAA — per-sine fold state (1st-order antiderivative AA).
        std::array<FoldState, kMaxUnison> foldStateA_ {};
        std::array<FoldState, kMaxUnison> foldStateB_ {};

        // Phase 11c — SPECTRAL filter state (per OSC).
        int   spectralTypeA_   = 0;     // 0=LP, 1=HP, 2=Smear, 3=Comb, 4=RingMod, 5=BitCrush
        float spectralAmtA_    = 0.0f;
        int   spectralTypeB_   = 0;
        float spectralAmtB_    = 0.0f;
        bool  spectralBypassA_ = true;  // optimization: skip processing when amount near zero
        bool  spectralBypassB_ = true;

        // Phase 11g — INTERP mode (frame interpolation control).
        int interpModeA_ = 0;   // 0 = Linear (bilinear, default), 1 = Stepped (snap to nearest frame)
        int interpModeB_ = 0;

        // Phase 11g — Comb delay line + Ring Mod phase + Bit Crush state per OSC per channel
        static constexpr int kSpectralCombSize = 256;
        std::array<float, kSpectralCombSize> spectralCombAL_{}, spectralCombAR_{};
        std::array<float, kSpectralCombSize> spectralCombBL_{}, spectralCombBR_{};
        int spectralCombWriteA_ = 0;
        int spectralCombWriteB_ = 0;
        double spectralRingPhaseA_ = 0.0;   // for Ring Mod
        double spectralRingPhaseB_ = 0.0;

        juce::dsp::IIR::Filter<float> spectralFilterAL_, spectralFilterAR_;
        juce::dsp::IIR::Filter<float> spectralFilterBL_, spectralFilterBR_;

        // Phase 11i — Downsample S&H state per OSC per channel
        float spectralDsHeldAL_ = 0.0f, spectralDsHeldAR_ = 0.0f;
        float spectralDsHeldBL_ = 0.0f, spectralDsHeldBR_ = 0.0f;
        float spectralDsCounterA_ = 0.0f, spectralDsCounterB_ = 0.0f;

        // Phase 11i — Tilt EQ filter pair per OSC per channel (one-pole LP for low band)
        float spectralTiltLowAL_ = 0.0f, spectralTiltLowAR_ = 0.0f;
        float spectralTiltLowBL_ = 0.0f, spectralTiltLowBR_ = 0.0f;

        // Phase 11i — Vibrato modulator phase per OSC + tiny delay buffer
        static constexpr int kSpectralVibSize = 64;
        std::array<float, kSpectralVibSize> spectralVibAL_{}, spectralVibAR_{};
        std::array<float, kSpectralVibSize> spectralVibBL_{}, spectralVibBR_{};
        int spectralVibWriteA_ = 0, spectralVibWriteB_ = 0;
        double spectralVibPhaseA_ = 0.0, spectralVibPhaseB_ = 0.0;

        // Per-sine unison config — PER-OSC (computed at setUnisonA/B / startNote).
        // Detune (cents), pan L/R (with BLEND gain pre-multiplied in), and the auto-gain
        // normalization factor are all independent for OSC A and OSC B.
        std::array<float,  kMaxUnison> uDetuneCentsA_ {};
        std::array<float,  kMaxUnison> uDetuneCentsB_ {};
        // Pan gains default to the count-1 CENTRE result (0.7071/0.7071 on voice 0) so a
        // voice that has never received a setUnison broadcast still SOUNDS — all-zero
        // defaults rendered exact silence (harness soak find, 2026-07-05).
        std::array<float,  kMaxUnison> uPanLA_        { 0.7071f };
        std::array<float,  kMaxUnison> uPanRA_        { 0.7071f };
        std::array<float,  kMaxUnison> uPanLB_        { 0.7071f };
        std::array<float,  kMaxUnison> uPanRB_        { 0.7071f };

        // fb523 — THE MODULATOR-TAP PAN CORRECTION, per osc. Target written by setUnisonImpl;
        //  the live copy snaps on a structural change and otherwise glides on lvlSmCoef_ exactly
        //  like uPanL/uPanR, so a Width sweep cannot zipper the modulation depth. √2 = the
        //  unison-1 value, so a voice that never receives a broadcast is already correct.
        float monoTapCorr_ [4] = { 1.41421356f, 1.41421356f, 1.41421356f, 1.41421356f };
        float monoTapCorrT_[4] = { 1.41421356f, 1.41421356f, 1.41421356f, 1.41421356f };
        int   activeUnisonA_ = 1, activeUnisonB_ = 1;   // 1..kMaxUnison per OSC
        float uNormA_ = 1.0f,     uNormB_ = 1.0f;       // auto-gain: 1/sqrt(Σ blendGain²) — holds loudness as voices rise
        // fb522 — this is NO LONGER the detune scale: SYN_OSC_x_URANGE is (5..4800 cents,
        // default 50.0). What survives here is the LEGACY REFERENCE, and it has two live jobs:
        //   1. the seed for uniRangeT_/uniRangeSm_ below, so a voice that never receives a
        //      broadcast detunes exactly as it always did;
        //   2. the threshold in uniRateMul() — the mip pick only widens for the part of the
        //      spread that goes BEYOND this constant, which is what makes URANGE = 50
        //      bit-identical to the pre-fb522 build.
        static constexpr float kUniMaxDetuneCents = 50.0f;  // ±50 cents (±½ semitone): the pre-fb522 fixed scale
        // fb255 — UNISON BLEND FLOOR: the minimum per-voice blend gain, applied SYMMETRICALLY (both the
        // leftmost and rightmost voices). Replaces the old voice-0-only "always full gain" anchor, which
        // pinned the LEFTMOST voice loud while blend scaled the right down → the sound leaned LEFT (worse
        // at high Width). A symmetric floor keeps UNISON=2/BLEND=0 from going silent (the reason the anchor
        // existed) WITHOUT breaking L/R balance. Only active below ~blend 0.15; auto-gain restores loudness.
        static constexpr float kUniBlendFloor = 0.15f;

        // ── fb522 OVERPASS — new per-osc unison / warp / phase state ────────────────────
        struct UniArgs { int count = 1; float det = 0.0f, blend = 1.0f, width = 0.5f; };
        UniArgs uniArgs_[4] {};                       // last args pushed to setUnisonImpl, per osc
        // URANGE (cents of half-spread at Detune 100 %). Seeded from the retired constant so a
        // voice that never receives a broadcast detunes exactly as it did before.
        float uniRangeT_[4]  = { kUniMaxDetuneCents, kUniMaxDetuneCents, kUniMaxDetuneCents, kUniMaxDetuneCents };
        float uniRangeSm_[4] = { kUniMaxDetuneCents, kUniMaxDetuneCents, kUniMaxDetuneCents, kUniMaxDetuneCents };
        bool  uniRangeSeeded_[4] = { false, false, false, false };
        float uniWarp_[4]  = { 0.0f, 0.0f, 0.0f, 0.0f };   // UWARP, bipolar warp fan across the sines
        int   uniStack_[4] = { 0, 0, 0, 0 };               // USTACK option index (0 = Off)
        std::array<float, kMaxUnison> uWarpOffA_ {}, uWarpOffB_ {}, uWarpOffC_ {}, uWarpOffD_ {};
        // WARP FILTER (fb543) — [osc][slot] coefficients, [osc][slot][L/R] state. 8 B of state
        // per channel: 4 x 2 x 2 x 8 = 128 B per voice, 12 KB across all 96 voices.
        WarpFiltCoef  wfCoef_[4][2] {};
        WarpFiltState wfState_[4][2][2] {};
        bool  uniWarpOnA_ = false, uniWarpOnB_ = false, uniWarpOnC_ = false, uniWarpOnD_ = false;
        float warpVar_[4]  = { 0.0f, 0.0f, 0.0f, 0.0f };   // WVAR  — per-osc WARP slot 1 VAR
        float warp2Var_[4] = { 0.0f, 0.0f, 0.0f, 0.0f };   // W2VAR — per-osc WARP slot 2 VAR
        const DrawSlot* drawTable_ = nullptr;   // fb550 — processor-owned table, may be null
        float phaseOff_[4] = { 0.0f, 0.0f, 0.0f, 0.0f };   // SYN_OSC_x_PHASE, in CYCLES (param is degrees)
        float phaseAmt_[4] = { 1.0f, 1.0f, 1.0f, 1.0f };   // SYN_OSC_x_PHASE_AMT, 0..1 (param default 100 %)
        // fb544 — PHASE IS CONTINUOUS. `phaseOffApplied_` is how much of phaseOff_ is currently
        // baked into the running accumulators; `phaseOffStep_` is the per-sample slide that closes
        // the gap over exactly one block. Both are per-VOICE, because each voice's accumulator is.
        double phaseOffApplied_[4] = { 0.0, 0.0, 0.0, 0.0 };
        double phaseOffStep_[4]    = { 0.0, 0.0, 0.0, 0.0 };

        // ── WAVER — per-(osc × unison sine) OU analog pitch drift (replaces the old
        //    EROSION pitch sine-LFO). Depth 0..1 per osc; cents state + per-sine RNG. ──
        float         waverA_ = 0.0f, waverB_ = 0.0f;   // depth 0..1 (per-block from APVTS/100)
        float         waverCentsA_[kMaxUnison] {};       // OU drift state, cents (osc A)
        float         waverCentsB_[kMaxUnison] {};       // OU drift state, cents (osc B)
        std::uint32_t waverRngA_[kMaxUnison] {};         // per-(osc A × sine) xorshift32 state
        std::uint32_t waverRngB_[kMaxUnison] {};         // per-(osc B × sine) xorshift32 state

        // ════ OSC C + D state (4-osc, spec P2) — full twins of OSC B, same types/inits ════
        // ── OSC C ──
        float  levelC_ = 0.0f;                           // start silent (spec)
        float  panLC_ = 0.7071f, panRC_ = 0.7071f;
        float  panLCT_ = 0.7071f, panRCT_ = 0.7071f;   // fb202 — glide targets
        int    octOffsetC_ = 0, semiOffsetC_ = 0;
        float  centsOffsetC_ = 0.0f;
        const tw::Wavetable* currentWavetableC_ = nullptr;
        float  framePosC_ = 0.0f;
        int    warpModeC_ = 0;
        int    warp2ModeC_ = 0;
        float  warp2AmountC_ = 0.0f, warp2AmountBaseC_ = 0.0f;
        int    currentMipLevelC_ = 0;
        float  warpAmountC_ = 0.0f;
        Engine engineC_ = Engine::WT;
        std::uint32_t noiseStateC_ = 0xBB67AE85u ^ static_cast<std::uint32_t> (reinterpret_cast<std::uintptr_t> (this));
        float  noiseLpZC_ = 0.0f;
        std::array<double, kMaxUnison> uPhaseC_{}, uPhaseIncC_{}, uModPhaseC_{}, uSyncPhaseC_{};
        float  ktDepthC_ = 0.0f; int ktDestC_ = 0;
        float  framePosBaseC_ = 0.0f, warpAmountBaseC_ = 0.0f, foldAmountBaseC_ = 0.0f;
        int    routeSrcC_ = 0, routeDestC_ = 0; float routeAmtC_ = 0.0f;
        std::array<float, kMaxUnison> uFramePosC_{};
        float  frameSpreadC01_ = 0.0f;
        float  blurTargetC_ = 0.0f, blurC_ = 0.0f;
        std::array<float, tw::Wavetable::kFrameSize> blendC_{};
        float  lastFpC_ = -2.0f, lastBlurC_ = -2.0f; int lastMipC_ = -2; int lastEpochC_ = -1;
        const tw::Wavetable* lastWtC_ = nullptr;
        int    phaseModeC_ = 2;
        int    foldShapeC_ = 0; float foldAmountC_ = 0.0f;
        std::array<FoldState, kMaxUnison> foldStateC_{};
        int    spectralTypeC_ = 0; float spectralAmtC_ = 0.0f; bool spectralBypassC_ = true;
        int    interpModeC_ = 0;
        std::array<float, kSpectralCombSize> spectralCombCL_{}, spectralCombCR_{};
        int    spectralCombWriteC_ = 0; double spectralRingPhaseC_ = 0.0;
        juce::dsp::IIR::Filter<float> spectralFilterCL_, spectralFilterCR_;
        float  spectralDsHeldCL_ = 0.0f, spectralDsHeldCR_ = 0.0f, spectralDsCounterC_ = 0.0f;
        float  spectralTiltLowCL_ = 0.0f, spectralTiltLowCR_ = 0.0f;
        std::array<float, kSpectralVibSize> spectralVibCL_{}, spectralVibCR_{};
        int    spectralVibWriteC_ = 0; double spectralVibPhaseC_ = 0.0;
        std::array<float, kMaxUnison> uDetuneCentsC_{}, uPanLC_{ 0.7071f }, uPanRC_{ 0.7071f };
        int    activeUnisonC_ = 1; float uNormC_ = 1.0f;
        float  waverC_ = 0.0f; float waverCentsC_[kMaxUnison]{}; std::uint32_t waverRngC_[kMaxUnison]{};
        // ── OSC D ──
        float  levelD_ = 0.0f;                           // start silent (spec)
        float  panLD_ = 0.7071f, panRD_ = 0.7071f;
        float  panLDT_ = 0.7071f, panRDT_ = 0.7071f;   // fb202 — glide targets
        int    octOffsetD_ = 0, semiOffsetD_ = 0;
        float  centsOffsetD_ = 0.0f;
        const tw::Wavetable* currentWavetableD_ = nullptr;
        float  framePosD_ = 0.0f;
        int    warpModeD_ = 0;
        int    warp2ModeD_ = 0;
        float  warp2AmountD_ = 0.0f, warp2AmountBaseD_ = 0.0f;
        int    currentMipLevelD_ = 0;
        float  warpAmountD_ = 0.0f;
        Engine engineD_ = Engine::WT;
        std::uint32_t noiseStateD_ = 0x3C6EF372u ^ static_cast<std::uint32_t> (reinterpret_cast<std::uintptr_t> (this));
        float  noiseLpZD_ = 0.0f;
        std::array<double, kMaxUnison> uPhaseD_{}, uPhaseIncD_{}, uModPhaseD_{}, uSyncPhaseD_{};
        float  ktDepthD_ = 0.0f; int ktDestD_ = 0;
        float  framePosBaseD_ = 0.0f, warpAmountBaseD_ = 0.0f, foldAmountBaseD_ = 0.0f;
        int    routeSrcD_ = 0, routeDestD_ = 0; float routeAmtD_ = 0.0f;
        std::array<float, kMaxUnison> uFramePosD_{};
        float  frameSpreadD01_ = 0.0f;
        float  blurTargetD_ = 0.0f, blurD_ = 0.0f;
        std::array<float, tw::Wavetable::kFrameSize> blendD_{};
        float  lastFpD_ = -2.0f, lastBlurD_ = -2.0f; int lastMipD_ = -2; int lastEpochD_ = -1;
        const tw::Wavetable* lastWtD_ = nullptr;
        int    phaseModeD_ = 2;
        int    foldShapeD_ = 0; float foldAmountD_ = 0.0f;
        std::array<FoldState, kMaxUnison> foldStateD_{};
        int    spectralTypeD_ = 0; float spectralAmtD_ = 0.0f; bool spectralBypassD_ = true;
        int    interpModeD_ = 0;
        std::array<float, kSpectralCombSize> spectralCombDL_{}, spectralCombDR_{};
        int    spectralCombWriteD_ = 0; double spectralRingPhaseD_ = 0.0;
        juce::dsp::IIR::Filter<float> spectralFilterDL_, spectralFilterDR_;
        float  spectralDsHeldDL_ = 0.0f, spectralDsHeldDR_ = 0.0f, spectralDsCounterD_ = 0.0f;
        float  spectralTiltLowDL_ = 0.0f, spectralTiltLowDR_ = 0.0f;
        std::array<float, kSpectralVibSize> spectralVibDL_{}, spectralVibDR_{};
        int    spectralVibWriteD_ = 0; double spectralVibPhaseD_ = 0.0;
        std::array<float, kMaxUnison> uDetuneCentsD_{}, uPanLD_{ 0.7071f }, uPanRD_{ 0.7071f };
        int    activeUnisonD_ = 1; float uNormD_ = 1.0f;
        float  waverD_ = 0.0f; float waverCentsD_[kMaxUnison]{}; std::uint32_t waverRngD_[kMaxUnison]{};

        // Phase 8a — HORIZON tilt filter (per-voice high-shelf, gain depends on midiNote * horizon)
        float horizonAmount_   = 0.0f;  // -1..+1 from SYN_HORIZON/100
        juce::dsp::IIR::Filter<float> horizonShelfL_;
        juce::dsp::IIR::Filter<float> horizonShelfR_;
        float lastHorizonTilt_ = -1.0e9f;   // change-gate for the shelf coefficient recompute

        // Phase 8a polish — exponential fade on voice steal (~30ms, Phase 12) to avoid clicks
        float stealingFade_     = 1.0f;     // 1.0 = no fade, 0.0 = silent
        float stealingFadeStep_ = 0.0f;     // multiplier per sample during fade
        bool  stealing_         = false;

        // Release-end declick — "silent light switch". When the amp envelope finishes
        // its release the oscillator is already silent, but a resonant filter / HORIZON
        // shelf / in-flight grain can still be ringing. Clearing the voice the instant
        // the env goes idle cuts that ring → click (and a click machine-guns through the
        // granular engine). Instead, ramp the FINAL post-filter output to true zero over
        // kFinishFadeSec, THEN release the slot. Click-free for any release/decay length
        // and any filter resonance.
        static constexpr double kFinishFadeSec = 0.008;   // ~8 ms linear fade-to-zero
        bool  finishing_     = false;
        float finishFade_    = 1.0f;        // 1.0 = full, 0.0 = silent
        float finishFadeStep_= 0.0f;        // linear decrement per sample

        // Phase 12 — monotonic timestamp from startNote, used by UnisonSynth
        // to find the oldest non-stealing voice when the polyphony cap is hit.
        juce::uint32 noteStartStamp_ = 0;
    };
}
