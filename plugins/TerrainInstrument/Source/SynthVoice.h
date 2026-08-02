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

        bool canPlaySound (juce::SynthesiserSound* s) override
        {
            return dynamic_cast<SynthSound*> (s) != nullptr;
        }

        void setCurrentPlaybackSampleRate (double sr) override
        {
            juce::SynthesiserVoice::setCurrentPlaybackSampleRate (sr);
            sampleRate_ = (sr > 0.0) ? sr : 48000.0;
            noiseSR_ = (float) sampleRate_;   // NOISE engine Hz-based math (hum/wind/rumble/SVF)
            ampEnv_.prepare (sampleRate_);
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
            { int u = 0; for (auto& e : harmEngA_) e.prepare (sampleRate_, u++ == 0); }   // HARMONIC-ENGINE-VOICE
            { int u = 0; for (auto& e : harmEngB_) e.prepare (sampleRate_, u++ == 0); }   //  (index 0 = bank anchor)
            { int u = 0; for (auto& e : harmEngC_) e.prepare (sampleRate_, u++ == 0); }
            { int u = 0; for (auto& e : harmEngD_) e.prepare (sampleRate_, u++ == 0); }
            { int u = 0; for (auto& e : modalEngA_) e.prepare (sampleRate_, u++ == 0); }   // MODAL-ENGINE-VOICE
            { int u = 0; for (auto& e : modalEngB_) e.prepare (sampleRate_, u++ == 0); }
            { int u = 0; for (auto& e : modalEngC_) e.prepare (sampleRate_, u++ == 0); }
            { int u = 0; for (auto& e : modalEngD_) e.prepare (sampleRate_, u++ == 0); }
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

        // ── Envelope follower taps (for the UI playhead dot) ──
        // Live amp-env output [0,1] and whether this voice is sounding. The editor
        // polls the most-active voice each timer tick and pushes this to the WebUI.
        float getAmpEnvLevel() const noexcept { return (float) ampEnv_.level(); }
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
        void setFilterType (int typeIdx) noexcept
        {
            const int clamped = juce::jlimit (0, (int) tw::filters::kNumTypes - 1, typeIdx);
            filterType1_ = clamped;
            filterSlot_.setType (static_cast<tw::filters::Type> (clamped));
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
        { filterSlot_.setPoles (tap1); filterSlot2_.setPoles (tap2); }   // tap 0..3 = 6/12/18/24 dB
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
            warpMode_   = juce::jlimit (0, 10, mode);
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
        // HARM-VIZ — downsample this voice's live anchor bank into UI bins (audio thread only)
        bool harmLiveBins (int osc, float* out, int nBins) const noexcept
        {
            const Engine oe[4] = { engine_, engineB_, engineC_, engineD_ };
            if (osc < 0 || osc > 3 || oe[osc] != Engine::HARM) return false;
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
            b.depth = (std::exp (2.0f * dc) - 1.0f) / (std::exp (2.0f) - 1.0f);   // house exp-bias curve
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
            warpModeB_   = juce::jlimit (0, 10, mode);
            warpAmountBaseB_ = juce::jlimit (0.0f, 1.0f, amount);
        }

        /** WARP 2 — second chained warp slot, per OSC. Runs in SERIES on slot 1's
         *  output phase (Serum WARP1→WARP2 parity). Same mode list as slot 1. */
        void setWarp2 (int modeA, float amountA, int modeB, float amountB) noexcept
        {
            warp2ModeA_       = juce::jlimit (0, 10, modeA);
            warp2AmountBaseA_ = juce::jlimit (0.0f, 1.0f, amountA);
            warp2ModeB_       = juce::jlimit (0, 10, modeB);
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
         *  detune  0..1 → pitch fan ±kUniMaxDetuneCents at the edges (the "fat").
         *  blend   0..1 → balance of the centre voice vs the detuned/outer voices:
         *                 1 = all voices equal; 0 = only the centre voice (mono).
         *                 Modelled per-voice as gain = 1 − (1−blend)·|u_norm|.
         *  width   0..1 → stereo spread (equal-power pan) of the voices L↔R.
         *  Blend gain is pre-multiplied into the pan tables so the render loop is
         *  unchanged; auto-gain (1/√Σgain²) holds perceived loudness as voices rise. */
        void setUnisonA (int count, float detune01, float blend01, float width01) noexcept
        {
            setUnisonImpl (activeUnisonA_, uDetuneCentsA_, uPanLTA_, uPanRTA_, uNormTA_,
                           uPanLA_, uPanRA_, uNormA_, uniSnapA_,
                           count, detune01, blend01, width01);
            updateUnisonFramePositions();
            if (currentMidiNote_ >= 0) updateUnisonPhaseIncrementsA (glideNote_);
        }
        void setUnisonB (int count, float detune01, float blend01, float width01) noexcept
        {
            setUnisonImpl (activeUnisonB_, uDetuneCentsB_, uPanLTB_, uPanRTB_, uNormTB_,
                           uPanLB_, uPanRB_, uNormB_, uniSnapB_,
                           count, detune01, blend01, width01);
            updateUnisonFramePositions();
            if (currentMidiNote_ >= 0) updateUnisonPhaseIncrementsB (glideNote_);
        }

        /** WARP phase-domain remap (modes 1-8) — EXACT math of the original inline
         *  switches, factored so two slots chain in series (slot 2 transforms slot 1's
         *  output). Pure function of the input phase p; FORMANT's window MULTIPLIES into
         *  `window` (slot-1 entry value is 1.0 → identical to the old assign) and PWM's
         *  silence gate ORs into `skipLookup`. Modes 0/9/10 pass phase through
         *  (9/10 are amp-domain — see applyAmpWarp). */
        static double applyPhaseWarp (int mode, float amount, double p,
                                      float& window, bool& skipLookup) noexcept
        {
            switch (mode)
            {
                case 1:  // BEND
                {
                    const double pi2 = 2.0 * 3.14159265358979323846;
                    const double w = p + (double) amount * 0.5 * std::sin (pi2 * p);
                    return w - std::floor (w);
                }
                case 2:  // SYNC — 1×..16× exponential (Vital-style)
                {
                    const double w = p * std::pow (2.0, (double) amount * 4.0);
                    return w - std::floor (w);
                }
                case 3:  // FORMANT — windowed sync (half-sine bell keyed off the input phase)
                {
                    const double w  = p * std::pow (2.0, (double) amount * 4.0);
                    const double pi = 3.14159265358979323846;
                    window *= static_cast<float> (std::sin (pi * p));
                    return w - std::floor (w);
                }
                case 4:  // PWM — duty-cycle window
                {
                    const double duty = juce::jmax (0.10, 1.0 - (double) amount * 0.45);
                    if (p >= duty) { skipLookup = true; return p; }
                    return p / duty;
                }
                case 5:  // SKEW — piecewise 2-segment peak shift
                {
                    const double knee = juce::jmax (0.05, 0.5 - (double) amount * 0.4);
                    return (p < knee) ? p / knee * 0.5
                                      : 0.5 + (p - knee) / (1.0 - knee) * 0.5;
                }
                case 6:  // MIRROR — squeezed-mirror blend
                {
                    const double mirrored = (p < 0.5) ? p * 2.0 : 2.0 - p * 2.0;
                    const double w = p * (1.0 - (double) amount) + mirrored * (double) amount;
                    return w - std::floor (w);
                }
                case 7:  // FRACTALIZE — fmod cascade, N = 1..8
                {
                    const double w = p * (1.0 + (double) amount * 7.0);
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
                    const double steps = std::round (std::pow (2.0, 5.0 - 4.0 * (double) amount));
                    return (std::floor (p * steps) + 0.25) / steps;
                }
                default: return p;   // NONE / RECTIFY / SINE SHAPER (amp-domain)
            }
        }

        /** WARP amp-domain stage (modes 9-10) — applied post-lookup, per slot. */
        static float applyAmpWarp (int mode, float amount, float s) noexcept
        {
            if (mode == 9)         // RECTIFY: blend dry with |x|×2−1 by amount
            {
                const float rect = std::abs (s) * 2.0f - 1.0f;
                return s * (1.0f - amount) + rect * amount;
            }
            if (mode == 10)        // SINE SHAPER: sin(x × π/2 × (1 + amount×4))
            {
                const float drive = 1.0f + amount * 4.0f;
                return std::sin (s * (float) (3.14159265358979323846 * 0.5) * drive);
            }
            return s;
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
        void setUnisonImpl (int& activeCount,
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
            const float wid = juce::jlimit (0.0f, 1.0f, width01);

            float gainSq = 0.0f;   // Σ (panL² + panR²) = Σ blendGain² → auto-gain
            for (int u = 0; u < activeCount; ++u)
            {
                if (activeCount <= 1)
                {
                    detCents[(size_t) u] = 0.0f;
                    panL[(size_t) u] = 0.7071f;
                    panR[(size_t) u] = 0.7071f;
                    gainSq += 1.0f;     // 0.7071² + 0.7071² = 1 (centre voice, equal power)
                    continue;
                }
                const float u_norm = ((float) u / (float) (activeCount - 1)) * 2.0f - 1.0f;  // -1..+1
                detCents[(size_t) u] = u_norm * det * kUniMaxDetuneCents;
                // BLEND — centre voice (u_norm≈0) full, outer voices scaled toward `blend`.
                // UNISON LAW (voice-0-anchored): voice 0 is ALWAYS full gain. Without this,
                // UNISON=2 has NO centre voice (u_norm = ±1 for both) and BLEND=0 zeroed
                // BOTH sines → the osc rendered EXACT SILENCE at full envelope.
                const float g = (u == 0) ? 1.0f : (1.0f - (1.0f - bl) * std::fabs (u_norm));
                // WIDTH — equal-power pan, angle in [0, π/2]; BLEND gain folded into the table
                // so the render loop stays a plain sAu·pan multiply.
                const float angle = (u_norm * wid + 1.0f) * 0.25f * juce::MathConstants<float>::pi;
                panL[(size_t) u] = std::cos (angle) * g;
                panR[(size_t) u] = std::sin (angle) * g;
                gainSq += panL[(size_t) u] * panL[(size_t) u] + panR[(size_t) u] * panR[(size_t) u];
            }
            for (int u = activeCount; u < kMaxUnison; ++u)
            {
                detCents[(size_t) u] = 0.0f;
                panL[(size_t) u] = 0.0f;
                panR[(size_t) u] = 0.0f;
            }
            // AUTO-GAIN — RMS-constant: holds perceived loudness as voices/blend change.
            norm = (gainSq > 1.0e-9f) ? (1.0f / std::sqrt (gainSq)) : 1.0f;
            // fb204 — structural change (voice count) = a NEW stereo image, snap don't glide;
            // continuous Width/Blend moves ride the per-sample one-pole in the render loop.
            if (! snapped || activeCount != oldCount)
            { panLLive = panL; panRLive = panR; normLive = norm; snapped = true; }
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
            // Phase tile RETIRED (2026-07-09, Max's call): phase is HARDWIRED to FREE —
            // accumulators never reset, every note starts where the wave happens to be.
            // The PHASE_MODE params stay registered (IDs FROZEN) but are ignored.
            juce::ignoreUnused (modeA, modeB);
            phaseModeA_ = 1; phaseModeB_ = 1;
        }

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
            switch (mode)
            {
                case 0: return 0.0;                                                              // RETRIG — aligned, punchy
                case 3: { const int cnt = (osc == 0) ? activeUnisonA_ : (osc == 1) ? activeUnisonB_ : (osc == 2) ? activeUnisonC_ : activeUnisonD_;
                          return (cnt > 1) ? (double) u / (double) cnt : 0.0; }                     // SPREAD — even fan (per-OSC)
                case 2: return nextPhaseRandom();                                                 // RANDOM — fresh each note
                case 1: default: return phaseSeeded_ ? carried : seedPhase (u, osc);              // FREE — seed once, then carry
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
        void setWarpC (int mode, float amount) noexcept { warpModeC_ = juce::jlimit(0,10,mode); warpAmountBaseC_ = juce::jlimit(0.0f,1.0f,amount); }
        void setWarpD (int mode, float amount) noexcept { warpModeD_ = juce::jlimit(0,10,mode); warpAmountBaseD_ = juce::jlimit(0.0f,1.0f,amount); }
        void setEngineC (int idx) noexcept { engineC_ = static_cast<Engine> (juce::jlimit(0,6,idx)); }
        void setEngineD (int idx) noexcept { engineD_ = static_cast<Engine> (juce::jlimit(0,6,idx)); }
        void setUnisonC (int count, float detune01, float blend01, float width01) noexcept { setUnisonImpl (activeUnisonC_, uDetuneCentsC_, uPanLTC_, uPanRTC_, uNormTC_, uPanLC_, uPanRC_, uNormC_, uniSnapC_, count, detune01, blend01, width01); updateUnisonFramePositions(); if (currentMidiNote_ >= 0) updateUnisonPhaseIncrementsC (glideNote_); }
        void setUnisonD (int count, float detune01, float blend01, float width01) noexcept { setUnisonImpl (activeUnisonD_, uDetuneCentsD_, uPanLTD_, uPanRTD_, uNormTD_, uPanLD_, uPanRD_, uNormD_, uniSnapD_, count, detune01, blend01, width01); updateUnisonFramePositions(); if (currentMidiNote_ >= 0) updateUnisonPhaseIncrementsD (glideNote_); }
        void setWarp2CD (int modeC, float amountC, int modeD, float amountD) noexcept { warp2ModeC_=juce::jlimit(0,10,modeC); warp2AmountBaseC_=juce::jlimit(0.0f,1.0f,amountC); warp2ModeD_=juce::jlimit(0,10,modeD); warp2AmountBaseD_=juce::jlimit(0.0f,1.0f,amountD); }

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
            juce::ignoreUnused (modeC, modeD);   // hardwired FREE (tile retired — see setPhaseMode)
            phaseModeC_ = 1; phaseModeD_ = 1;
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
                    else continue;
                    // fb183 — env→LEVEL is OWNERSHIP, not offset: depth crossfades the knob
                    // toward the envelope's own shape (eff = (1−Σd)·knob + Σd·env). At 100%
                    // the shape IS the level — knob anywhere, Serum's level-down pluck included.
                    // Per-voice: each note plucks its OWN level (the mono-tap ghost is dead).
                    if (wc::isEnvModSource (sI)
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
                    if (wc::isEnvModSource (sI))
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
                    if (wc::isEnvModSource (sI))
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
            // Re-derive per-sine phase increments with updated drift + glide pitch
            updateUnisonPhaseIncrementsA (glideNote_);
            updateUnisonPhaseIncrementsB (glideNote_);
            updateUnisonPhaseIncrementsC (glideNote_);
            updateUnisonPhaseIncrementsD (glideNote_);
            prepareSubBlock (numSamples);   // SUB — voice-anchored per-osc sub lanes (universal box)

            // Phase 10a / Phase 8b — pick mip level using sine 0 (centre-detuned,
            // no spread offset) as the reference — ±25 cents of unison detune
            // doesn't cross a mip boundary in practice.
            // Phase 11d AA — phase-multiply warps (SYNC, FORMANT, FRACTALIZE) read the
            // table faster than the base increment, so pick the mip for the WARPED rate
            // (more band-limited → far less aliasing). Other modes use rate ×1.
            auto warpRateMul = [] (int mode, float amt) -> double
            {
                if (mode == 2 || mode == 3) return std::pow (2.0, (double) amt * 4.0); // SYNC / FORMANT (1..16x)
                if (mode == 7)              return 1.0 + (double) amt * 7.0;            // FRACTALIZE (1..8x)
                return 1.0;
            };
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
            currentMipLevelA_ = tw::Wavetable::mipLevelForPhaseIncrement (uPhaseIncA_[0] * warpRateMul (warpMode_,  warpAmount_) * warpRateMul (warp2ModeA_, warp2AmountA_) * fmRateMul (engine_,  0));
            currentMipLevelB_ = tw::Wavetable::mipLevelForPhaseIncrement (uPhaseIncB_[0] * warpRateMul (warpModeB_, warpAmountB_) * warpRateMul (warp2ModeB_, warp2AmountB_) * fmRateMul (engineB_, 1));
            currentMipLevelC_ = tw::Wavetable::mipLevelForPhaseIncrement (uPhaseIncC_[0] * warpRateMul (warpModeC_, warpAmountC_) * warpRateMul (warp2ModeC_, warp2AmountC_) * fmRateMul (engineC_, 2));
            currentMipLevelD_ = tw::Wavetable::mipLevelForPhaseIncrement (uPhaseIncD_[0] * warpRateMul (warpModeD_, warpAmountD_) * warpRateMul (warp2ModeD_, warp2AmountD_) * fmRateMul (engineD_, 3));

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
                    currentWavetable_->renderBlend (currentMipLevelA_, fpA, blurA_, blendA_.data());
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
                    currentWavetableB_->renderBlend (currentMipLevelB_, fpB, blurB_, blendB_.data());
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
                    currentWavetableC_->renderBlend (currentMipLevelC_, fpC, blurC_, blendC_.data());
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
                    currentWavetableD_->renderBlend (currentMipLevelD_, fpD, blurD_, blendD_.data());
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
                noiseForce_ = false;   // fb64 — set when any osc blends WITH the noise (so its tap is generated even if noise output is off)
                for (int c = 0; c < 4; ++c)
                    for (int s = 0; s < 4; ++s)
                    {
                        const BlendSlotV& b = blendSlot_[c][s];
                        if (b.mode < 1 || b.mode > 4 || b.depth < 1.0e-4f) continue;      // armed FM/PD/AM/RM only
                        if ((b.mode == 1 || b.mode == 2) && isBlock (eng[c]))
                            blkCarrierArmed_[c] = true;                                   // FM/PD phase-modulate c's block (AM/RM don't need the ring)
                        if      (b.src < 4)  modSrcForce_[b.src] = true;                  // Osc A..D as source
                        else if (b.src == 5) noiseForce_         = true;                  // Noise as source (fb64)
                        else if (b.src == 6) modSrcForce_[c]     = true;                  // Self
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
            for (int i = 0; i < numSamples; ++i)
            {
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
                {
                    const float repInc[4] = { (float) uPhaseIncA_[0], (float) uPhaseIncB_[0],
                                              (float) uPhaseIncC_[0], (float) uPhaseIncD_[0] };
                    for (int c = 0; c < 4; ++c)
                    {
                        float pm = 0.f, fmDrive = 0.f, amp = 1.0f;
                        for (int s = 0; s < 4; ++s)
                        {
                            BlendSlotV& b = blendSlot_[c][s];
                            blendDepthSm_[c][s] += (b.depth - blendDepthSm_[c][s]) * 0.0025f;   // de-zipper
                            const float d = blendDepthSm_[c][s];
                            if (b.mode == 0 || d < 1.0e-4f) continue;                            // Off / silent
                            float mod;
                            if      (b.src < 4)  mod = modPrev_[b.src];   // Osc A..D (any-to-any)
                            else if (b.src == 5) mod = noiseModTap_;      // Noise (fb64) — FM/PD/AM/RM an osc WITH the noise
                            else if (b.src == 6) mod = modPrev_[c];       // Self (feedback)
                            else                 mod = 0.f;               // Sub(4): still no-op
                            if      (b.mode == 2) pm      += (1.20f * d) * mod;              // PD (phase, cycles)
                            else if (b.mode == 1) fmDrive += (12.0f * d) * mod;              // FM (freq deviation)
                            else if (b.mode == 3) amp     *= 1.0f + (1.8f * d) * mod;        // AM — carrier*(1+1.8·d·mod): fundamental KEPT; 1.8 drive → night-and-day at 100%
                            else if (b.mode == 4) amp     *= (1.0f - d) + (1.8f * d) * mod;   // RM — ring: dry fades, wet driven 1.8× so full ring reads hard [EAR-TUNABLE]
                        }
                        fmPhase_[c] = 0.9997f * fmPhase_[c] + repInc[c] * fmDrive;   // integrate freq → phase
                        fmPhase_[c] = juce::jlimit (-8.f, 8.f, fmPhase_[c]);         // bound (thru-zero + feedback safe)
                        blendOff[c] = pm + fmPhase_[c];
                        blendAmp[c] = juce::jlimit (-6.0f, 6.0f, amp);   // safety ceiling for stacked AM/RM on a hot modulator (never touches 1–2 slot use)
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
                                float  window      = 1.0f;   // PWM, FORMANT use this post-lookup window
                                bool   skipLookup  = false;  // PWM silence half-cycle

                                // WARP slot 1 — phase-domain remap (exact original math, factored to
                                // applyPhaseWarp so a second slot can chain on its output).
                                warpedPhase = applyPhaseWarp (warpMode_, warpAmount_, warpedPhase, window, skipLookup);
                                // WARP 2 — second slot, in SERIES on slot 1's output (Serum parity).
                                if (! skipLookup && warp2ModeA_ != 0)
                                    warpedPhase = applyPhaseWarp (warp2ModeA_, warp2AmountA_, warpedPhase, window, skipLookup);

                                if (skipLookup)
                                {
                                    sAu = 0.0f;
                                }
                                else
                                {
                                    // WT BLUR — read the per-block blended single-cycle buffer
                                    // (frame position, stepped-interp and blur already applied at block rate).
                                    double rpA = warpedPhase + (double) blendOff[0]; rpA -= std::floor (rpA); sAu = tw::Wavetable::readCycle (blendA_.data(), (float) rpA);   // BLEND inject
                                    sAu *= window;

                                    sAu = applyAmpWarp (warpMode_,    warpAmount_,    sAu);   // slot 1 amp-domain (RECTIFY / SINE SHAPER)
                                    sAu = applyAmpWarp (warp2ModeA_,  warp2AmountA_,  sAu);   // WARP 2 amp-domain, chained
                                }
                            }
                            else
                            {
                                sAu = static_cast<float> (2.0 * uPhaseA_[(size_t) u] - 1.0);
                                sAu -= static_cast<float> (polyBlep (uPhaseA_[(size_t) u], uPhaseIncA_[(size_t) u]));
                            }
                            uPhaseA_[(size_t) u] += uPhaseIncA_[(size_t) u];
                            if (uPhaseA_[(size_t) u] >= 1.0) uPhaseA_[(size_t) u] -= 1.0;
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
                            if (warp2ModeA_ != 0)
                                cPh = applyPhaseWarp (warp2ModeA_, warp2AmountA_, cPh, fmWin, fmSkip);
                            if (fmSkip) sAu = 0.0f;
                            else
                            {
                                sAu = (currentWavetable_ != nullptr)
                                        ? tw::Wavetable::readCycle (blendA_.data(), (float) cPh)
                                        : static_cast<float> (std::sin (pi2 * cPh));   // no table → pure-sine DX
                                sAu *= fmWin;
                                sAu = applyAmpWarp (warp2ModeA_, warp2AmountA_, sAu);
                            }
                            if (alg == 2)
                                sAu *= (1.0f - fmD1Sm_[0]) + fmD1Sm_[0] * m1;      // ring dry→wet on depth 1
                            uModPhaseA_[(size_t) u]  += inc * fmR1Eff_[0] + fmRustTps_[0];
                            uModPhaseA_[(size_t) u]  -= std::floor (uModPhaseA_[(size_t) u]);
                            uMod2PhaseA_[(size_t) u] += inc * fmR2Eff_[0];
                            uMod2PhaseA_[(size_t) u] -= std::floor (uMod2PhaseA_[(size_t) u]);
                            uPhaseA_[(size_t) u] += inc;
                            if (uPhaseA_[(size_t) u] >= 1.0) uPhaseA_[(size_t) u] -= 1.0;
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
                // RECTIFY DC block — only when this osc's wavetable warp == Rectify (slot 1 or 2)
                // with nonzero amount; dormant (bit-identical) otherwise.
                if ((engine_ == Engine::WT && ((warpMode_ == 9 && warpAmount_ > 0.001f) || (warp2ModeA_ == 9 && warp2AmountA_ > 0.001f)))
                    || (engine_ == Engine::FM && warp2ModeA_ == 9 && warp2AmountA_ > 0.001f))
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
                sA_L *= blendAmp[0]; sA_R *= blendAmp[0];   // BLEND AM/RM (amplitude-domain, all engines; 1.0 = inert)
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
                        sA_L = std::tanh (sA_L * drive + bias) * invSat - bias * invSat;
                        sA_R = std::tanh (sA_R * drive + bias) * invSat - bias * invSat;
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
                                float  window      = 1.0f;
                                bool   skipLookup  = false;

                                // WARP slot 1 + chained WARP 2 (see OSC A — identical structure).
                                warpedPhase = applyPhaseWarp (warpModeB_, warpAmountB_, warpedPhase, window, skipLookup);
                                if (! skipLookup && warp2ModeB_ != 0)
                                    warpedPhase = applyPhaseWarp (warp2ModeB_, warp2AmountB_, warpedPhase, window, skipLookup);

                                if (skipLookup)
                                {
                                    sBu = 0.0f;
                                }
                                else
                                {
                                    // WT BLUR — read the per-block blended single-cycle buffer.
                                    double rpB = warpedPhase + (double) blendOff[1]; rpB -= std::floor (rpB); sBu = tw::Wavetable::readCycle (blendB_.data(), (float) rpB);   // BLEND inject
                                    sBu *= window;

                                    sBu = applyAmpWarp (warpModeB_,   warpAmountB_,   sBu);   // slot 1 amp-domain
                                    sBu = applyAmpWarp (warp2ModeB_,  warp2AmountB_,  sBu);   // WARP 2 amp-domain, chained
                                }
                            }
                            else
                            {
                                sBu = static_cast<float> (2.0 * uPhaseB_[(size_t) u] - 1.0);
                                sBu -= static_cast<float> (polyBlep (uPhaseB_[(size_t) u], uPhaseIncB_[(size_t) u]));
                            }
                            uPhaseB_[(size_t) u] += uPhaseIncB_[(size_t) u];
                            if (uPhaseB_[(size_t) u] >= 1.0) uPhaseB_[(size_t) u] -= 1.0;
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
                            if (warp2ModeB_ != 0)
                                cPh = applyPhaseWarp (warp2ModeB_, warp2AmountB_, cPh, fmWin, fmSkip);
                            if (fmSkip) sBu = 0.0f;
                            else
                            {
                                sBu = (currentWavetableB_ != nullptr)
                                        ? tw::Wavetable::readCycle (blendB_.data(), (float) cPh)
                                        : static_cast<float> (std::sin (pi2 * cPh));
                                sBu *= fmWin;
                                sBu = applyAmpWarp (warp2ModeB_, warp2AmountB_, sBu);
                            }
                            if (alg == 2)
                                sBu *= (1.0f - fmD1Sm_[1]) + fmD1Sm_[1] * m1;
                            uModPhaseB_[(size_t) u]  += inc * fmR1Eff_[1] + fmRustTps_[1];
                            uModPhaseB_[(size_t) u]  -= std::floor (uModPhaseB_[(size_t) u]);
                            uMod2PhaseB_[(size_t) u] += inc * fmR2Eff_[1];
                            uMod2PhaseB_[(size_t) u] -= std::floor (uMod2PhaseB_[(size_t) u]);
                            uPhaseB_[(size_t) u] += inc;
                            if (uPhaseB_[(size_t) u] >= 1.0) uPhaseB_[(size_t) u] -= 1.0;
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
                // RECTIFY DC block — wavetable warp == Rectify (slot 1 or 2), else dormant/bit-identical.
                if ((engineB_ == Engine::WT && ((warpModeB_ == 9 && warpAmountB_ > 0.001f) || (warp2ModeB_ == 9 && warp2AmountB_ > 0.001f)))
                    || (engineB_ == Engine::FM && warp2ModeB_ == 9 && warp2AmountB_ > 0.001f))
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
                sB_L *= blendAmp[1]; sB_R *= blendAmp[1];   // BLEND AM/RM
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
                                float  window      = 1.0f;
                                bool   skipLookup  = false;

                                // WARP slot 1 + chained WARP 2 (see OSC A — identical structure).
                                warpedPhase = applyPhaseWarp (warpModeC_, warpAmountC_, warpedPhase, window, skipLookup);
                                if (! skipLookup && warp2ModeC_ != 0)
                                    warpedPhase = applyPhaseWarp (warp2ModeC_, warp2AmountC_, warpedPhase, window, skipLookup);

                                if (skipLookup)
                                {
                                    sCu = 0.0f;
                                }
                                else
                                {
                                    // WT BLUR — read the per-block blended single-cycle buffer.
                                    double rpC = warpedPhase + (double) blendOff[2]; rpC -= std::floor (rpC); sCu = tw::Wavetable::readCycle (blendC_.data(), (float) rpC);   // BLEND inject
                                    sCu *= window;

                                    sCu = applyAmpWarp (warpModeC_,   warpAmountC_,   sCu);   // slot 1 amp-domain
                                    sCu = applyAmpWarp (warp2ModeC_,  warp2AmountC_,  sCu);   // WARP 2 amp-domain, chained
                                }
                            }
                            else
                            {
                                sCu = static_cast<float> (2.0 * uPhaseC_[(size_t) u] - 1.0);
                                sCu -= static_cast<float> (polyBlep (uPhaseC_[(size_t) u], uPhaseIncC_[(size_t) u]));
                            }
                            uPhaseC_[(size_t) u] += uPhaseIncC_[(size_t) u];
                            if (uPhaseC_[(size_t) u] >= 1.0) uPhaseC_[(size_t) u] -= 1.0;
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
                            if (warp2ModeC_ != 0)
                                cPh = applyPhaseWarp (warp2ModeC_, warp2AmountC_, cPh, fmWin, fmSkip);
                            if (fmSkip) sCu = 0.0f;
                            else
                            {
                                sCu = (currentWavetableC_ != nullptr)
                                        ? tw::Wavetable::readCycle (blendC_.data(), (float) cPh)
                                        : static_cast<float> (std::sin (pi2 * cPh));
                                sCu *= fmWin;
                                sCu = applyAmpWarp (warp2ModeC_, warp2AmountC_, sCu);
                            }
                            if (alg == 2)
                                sCu *= (1.0f - fmD1Sm_[2]) + fmD1Sm_[2] * m1;
                            uModPhaseC_[(size_t) u]  += inc * fmR1Eff_[2] + fmRustTps_[2];
                            uModPhaseC_[(size_t) u]  -= std::floor (uModPhaseC_[(size_t) u]);
                            uMod2PhaseC_[(size_t) u] += inc * fmR2Eff_[2];
                            uMod2PhaseC_[(size_t) u] -= std::floor (uMod2PhaseC_[(size_t) u]);
                            uPhaseC_[(size_t) u] += inc;
                            if (uPhaseC_[(size_t) u] >= 1.0) uPhaseC_[(size_t) u] -= 1.0;
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
                // RECTIFY DC block — wavetable warp == Rectify (slot 1 or 2), else dormant/bit-identical.
                if ((engineC_ == Engine::WT && ((warpModeC_ == 9 && warpAmountC_ > 0.001f) || (warp2ModeC_ == 9 && warp2AmountC_ > 0.001f)))
                    || (engineC_ == Engine::FM && warp2ModeC_ == 9 && warp2AmountC_ > 0.001f))
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
                sC_L *= blendAmp[2]; sC_R *= blendAmp[2];   // BLEND AM/RM
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
                                float  window      = 1.0f;
                                bool   skipLookup  = false;

                                // WARP slot 1 + chained WARP 2 (see OSC A — identical structure).
                                warpedPhase = applyPhaseWarp (warpModeD_, warpAmountD_, warpedPhase, window, skipLookup);
                                if (! skipLookup && warp2ModeD_ != 0)
                                    warpedPhase = applyPhaseWarp (warp2ModeD_, warp2AmountD_, warpedPhase, window, skipLookup);

                                if (skipLookup)
                                {
                                    sDu = 0.0f;
                                }
                                else
                                {
                                    // WT BLUR — read the per-block blended single-cycle buffer.
                                    double rpD = warpedPhase + (double) blendOff[3]; rpD -= std::floor (rpD); sDu = tw::Wavetable::readCycle (blendD_.data(), (float) rpD);   // BLEND inject
                                    sDu *= window;

                                    sDu = applyAmpWarp (warpModeD_,   warpAmountD_,   sDu);   // slot 1 amp-domain
                                    sDu = applyAmpWarp (warp2ModeD_,  warp2AmountD_,  sDu);   // WARP 2 amp-domain, chained
                                }
                            }
                            else
                            {
                                sDu = static_cast<float> (2.0 * uPhaseD_[(size_t) u] - 1.0);
                                sDu -= static_cast<float> (polyBlep (uPhaseD_[(size_t) u], uPhaseIncD_[(size_t) u]));
                            }
                            uPhaseD_[(size_t) u] += uPhaseIncD_[(size_t) u];
                            if (uPhaseD_[(size_t) u] >= 1.0) uPhaseD_[(size_t) u] -= 1.0;
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
                            if (warp2ModeD_ != 0)
                                cPh = applyPhaseWarp (warp2ModeD_, warp2AmountD_, cPh, fmWin, fmSkip);
                            if (fmSkip) sDu = 0.0f;
                            else
                            {
                                sDu = (currentWavetableD_ != nullptr)
                                        ? tw::Wavetable::readCycle (blendD_.data(), (float) cPh)
                                        : static_cast<float> (std::sin (pi2 * cPh));
                                sDu *= fmWin;
                                sDu = applyAmpWarp (warp2ModeD_, warp2AmountD_, sDu);
                            }
                            if (alg == 2)
                                sDu *= (1.0f - fmD1Sm_[3]) + fmD1Sm_[3] * m1;
                            uModPhaseD_[(size_t) u]  += inc * fmR1Eff_[3] + fmRustTps_[3];
                            uModPhaseD_[(size_t) u]  -= std::floor (uModPhaseD_[(size_t) u]);
                            uMod2PhaseD_[(size_t) u] += inc * fmR2Eff_[3];
                            uMod2PhaseD_[(size_t) u] -= std::floor (uMod2PhaseD_[(size_t) u]);
                            uPhaseD_[(size_t) u] += inc;
                            if (uPhaseD_[(size_t) u] >= 1.0) uPhaseD_[(size_t) u] -= 1.0;
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
                // RECTIFY DC block — wavetable warp == Rectify (slot 1 or 2), else dormant/bit-identical.
                if ((engineD_ == Engine::WT && ((warpModeD_ == 9 && warpAmountD_ > 0.001f) || (warp2ModeD_ == 9 && warp2AmountD_ > 0.001f)))
                    || (engineD_ == Engine::FM && warp2ModeD_ == 9 && warp2AmountD_ > 0.001f))
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
                sD_L *= blendAmp[3]; sD_R *= blendAmp[3];   // BLEND AM/RM
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
                const float velEnv = currentVelocity_ * juce::jmax (0.0f, env * (1.0f + ampMod));

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
                // BLEND MODES: capture each osc's PRE-GAIN sample as the modulator tap (1-sample delay for
                // next iteration). These are pre level/pan/gate → a source at LEVEL 0 still modulates.
                modPrev_[0] = 0.5f * (sA_L + sA_R); modPrev_[1] = 0.5f * (sB_L + sB_R);
                modPrev_[2] = 0.5f * (sC_L + sC_R); modPrev_[3] = 0.5f * (sD_L + sD_R);
                for (int mc = 0; mc < 4; ++mc) modPrev_[mc] = juce::jlimit (-4.f, 4.f, modPrev_[mc]);
            }

            // fb122 ROBIN Pan — the station leans (unity at center, applied to every bus)
            if (robinAmpL_ != 1.0f || robinAmpR_ != 1.0f)
                for (int i = 0; i < numSamples; ++i)
                {
                    scratchL[i] *= robinAmpL_;  scratchR[i] *= robinAmpR_;
                    busB2L[i]   *= robinAmpL_;  busB2R[i]   *= robinAmpR_;
                    busDryL[i]  *= robinAmpL_;  busDryR[i]  *= robinAmpR_;
                }

            // Phase 8a polish — apply steal-fade and decide if voice should die
            if (stealing_)
            {
                for (int i = 0; i < numSamples; ++i)
                {
                    scratchL[i] *= stealingFade_;  scratchR[i] *= stealingFade_;
                    busB2L[i]   *= stealingFade_;  busB2R[i]   *= stealingFade_;   // fade the routing buses too
                    busDryL[i]  *= stealingFade_;  busDryR[i]  *= stealingFade_;
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
                    if (cutSemis1 != lastCutSemis1_)
                    {
                        lastCutSemis1_ = cutSemis1;
                        lastCutHz1_ = juce::jlimit (20.0f, fmax, 440.0f * std::pow (2.0f, (cutSemis1 - 69.0f) / 12.0f));
                    }
                    const float res1 = juce::jlimit (0.0f, 1.0f,
                        resSm1_ + resWander * driftState_ * 0.5f);
                    filterSlot_.setParams (lastCutHz1_, res1, drvSm1_, coefSr); visRes1_ = res1;   // fb204 — glided drive

                    // Filter 2 cutoff: base + routed envelopes (±96 ST) + LFO + drift.
                    const float cutSemis2 = baseCutSemis2 + fMod2 * 96.0f + lfoSemis2 + envCutSm2_ + driftSemis + ktSm2_ + velSm2_ * currentVelocity_ * 72.0f;   // fb204 glided
                    if (cutSemis2 != lastCutSemis2_)
                    {
                        lastCutSemis2_ = cutSemis2;
                        lastCutHz2_ = juce::jlimit (20.0f, fmax, 440.0f * std::pow (2.0f, (cutSemis2 - 69.0f) / 12.0f));
                    }
                    const float res2 = juce::jlimit (0.0f, 1.0f,
                        resSm2_ + resWander * driftState_ * 0.5f);
                    filterSlot2_.setParams (lastCutHz2_, res2, drvSm2_, coefSr); visRes2_ = res2;   // fb204 — glided drive

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
                    auto filterBuses = [&] (float b1L, float b1R, float b2L, float b2R, float& outL, float& outR)
                    {
                        if (! par)   // SERIES: F1(bus1) → drive1 → (+ bus2 F2-only) → F2 → drive2
                        {
                            float w1L = b1L, w1R = b1R;
                            if (a1) { float wl = b1L, wr = b1R; filterSlot_.processStereo (wl, wr);
                                      w1L = mixSm1_ * wl + (1.0f - mixSm1_) * b1L;
                                      w1R = mixSm1_ * wr + (1.0f - mixSm1_) * b1R; }
                            pdrive (w1L, w1R, pdrvSm1_, driveType1_, drvNorm1_);
                            const float pL = w1L + b2L, pR = w1R + b2R;
                            float w2L = pL, w2R = pR;
                            if (a2) { float wl = pL, wr = pR; filterSlot2_.processStereo (wl, wr);
                                      w2L = mixSm2_ * wl + (1.0f - mixSm2_) * pL;
                                      w2R = mixSm2_ * wr + (1.0f - mixSm2_) * pR; }
                            pdrive (w2L, w2R, pdrvSm2_, driveType2_, drvNorm2_);
                            outL = w2L; outR = w2R;
                        }
                        else         // PARALLEL: F1(bus1) + F2(bus2), each with its own post-drive
                        {
                            float w1L = b1L, w1R = b1R;
                            if (a1) { float wl = b1L, wr = b1R; filterSlot_.processStereo (wl, wr);
                                      w1L = mixSm1_ * wl + (1.0f - mixSm1_) * b1L;
                                      w1R = mixSm1_ * wr + (1.0f - mixSm1_) * b1R; }
                            pdrive (w1L, w1R, pdrvSm1_, driveType1_, drvNorm1_);
                            float w2L = b2L, w2R = b2R;
                            if (a2) { float wl = b2L, wr = b2R; filterSlot2_.processStereo (wl, wr);
                                      w2L = mixSm2_ * wl + (1.0f - mixSm2_) * b2L;
                                      w2R = mixSm2_ * wr + (1.0f - mixSm2_) * b2R; }
                            pdrive (w2L, w2R, pdrvSm2_, driveType2_, drvNorm2_);
                            outL = w1L + w2L; outR = w1R + w2R;
                        }
                    };

                    const float dryL = busDryL[i], dryR = busDryR[i];
                    if (oversample)
                    {
                        // 2× linear-interp upsample → filter twice → box decimate. Interp bus1 (via
                        // the existing osPrev feedback) + bus2; dry bypasses (added once, post-decimate).
                        const float m1L = 0.5f * (osPrevL_   + sL[i]),     m1R = 0.5f * (osPrevR_   + sR[i]);
                        const float m2L = 0.5f * (osPrevB2L_ + busB2L[i]), m2R = 0.5f * (osPrevB2R_ + busB2R[i]);
                        float yMidL, yMidR; filterBuses (m1L, m1R, m2L, m2R, yMidL, yMidR);
                        float yL, yR;       filterBuses (sL[i], sR[i], busB2L[i], busB2R[i], yL, yR);
                        float wetL = 0.5f * (yMidL + yL), wetR = 0.5f * (yMidR + yR);
                        osPrevL_ = wetL + dryL; osPrevR_ = wetR + dryR;   // OS feedback = un-widened output (keeps the loop stable)
                        osPrevB2L_ = busB2L[i]; osPrevB2R_ = busB2R[i];
                        widen (wetL, wetR);
                        sL[i] = wetL + dryL; sR[i] = wetR + dryR;
                    }
                    else
                    {
                        float oL, oR; filterBuses (sL[i], sR[i], busB2L[i], busB2R[i], oL, oR);
                        widen (oL, oR);
                        sL[i] = oL + dryL; sR[i] = oR + dryR;
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
        // amount in [0,1]; pre-gain is quadratic so the lower half of the knob
        // ramps gently and the upper half drives hard.
        static inline float applyFold (float x, int shape, float amount) noexcept
        {
            if (amount <= 1.0e-6f) return x;   // identity fast-path

            switch (shape)
            {
                case 0:
                {
                    // Linear (Serge) — pre 1..10, closed-form triangle wave fold.
                    const float pre    = 1.0f + amount * amount * 9.0f;
                    const float driven = x * pre;
                    const float q      = (driven + 1.0f) * 0.25f;
                    return 4.0f * std::fabs (q - std::round (q)) - 1.0f;
                }
                case 1:
                {
                    // Sine (Vital) — pre 1..2π, sin(drive·x), bounded ±1.
                    const float pre = 1.0f + amount * amount * 5.28318530f;
                    return std::sin (x * pre);
                }
                case 2:
                {
                    // Triangle (Buchla 259) — 3-stage cascade.
                    const float pre = 1.0f + amount * amount * 5.0f;
                    const float driven = x * pre;
                    auto linfold = [] (float v) -> float
                    {
                        const float q = (v + 1.0f) * 0.25f;
                        return 4.0f * std::fabs (q - std::round (q)) - 1.0f;
                    };
                    const float s1 = linfold (driven * 1.0f)        * 0.50f;
                    const float s2 = linfold (driven * 1.41421356f) * 0.35f;
                    const float s3 = linfold (driven * 2.0f)        * 0.15f;
                    return s1 + s2 + s3;
                }
                default: return x;
            }
        }

        // ── ADAA wavefolder (1st-order antiderivative anti-aliasing) ─────────
        // The triangle-based folds (Linear/Serge, Buchla) generate dense high
        // harmonics that alias badly at base rate. Rather than 2x oversample the
        // whole voice, we apply 1st-order ADAA — the same technique as the
        // nonlinear filters. Offline FFT proof: 2.4–5.6x less aliasing on the
        // triangle folds, neutral on the (already-smooth) sine fold, low-freq
        // shape preserved. State is per-sine (see foldStateA_/B_).
        //
        // foldAntideriv(x) = ∫ applyFold dx, so applyFold = d/dx foldAntideriv.
        //   triangle-wave antiderivative: G(q) = 2 r|r| − r,  r = q − round(q)
        //   ∫ linfold(x·a) dx = (4/a)·G((x·a+1)/4)
        static inline float foldGtri (float q) noexcept
        {
            const float r = q - std::round (q);
            return 2.0f * r * std::fabs (r) - r;
        }
        static inline float foldFlin (float x, float a) noexcept
        {
            return (4.0f / a) * foldGtri ((x * a + 1.0f) * 0.25f);
        }
        static inline float foldAntideriv (float x, int shape, float amount) noexcept
        {
            switch (shape)
            {
                case 1: { const float pre = 1.0f + amount * amount * 5.28318530f; return -std::cos (pre * x) / pre; }
                case 2: { const float pre = 1.0f + amount * amount * 5.0f;
                          return 0.5f  * foldFlin (x, pre)
                               + 0.35f * foldFlin (x, pre * 1.41421356f)
                               + 0.15f * foldFlin (x, pre * 2.0f); }
                default:{ const float pre = 1.0f + amount * amount * 9.0f; return foldFlin (x, pre); }
            }
        }

        struct FoldState
        {
            float x1  = 0.0f;    // previous input sample
            float Fx1 = 0.0f;    // cached antiderivative F(x1) for the (sh1,am1) curve below
            int   sh1 = -1;      // shape Fx1 was computed under (-1 = cache invalid)
            float am1 = -1.0f;   // amount Fx1 was computed under (-1 = cache invalid)
        };

        // 1st-order ADAA: y[n] = (F(x[n]) − F(x[n−1])) / (x[n] − x[n−1]).
        // WITHIN-BLOCK CACHE (CPU): shape/amount are pushed ONCE PER BLOCK (setFold), so across a
        // block F(x[n−1]) this sample is bit-identical to last sample's F(x[n]) — cache it (st.Fx1)
        // and reuse it ONLY while (shape,amount) are unchanged. ANY curve change (a block boundary
        // that moved the knob, or per-block automation) forces a live recompute of F(x1) on the
        // CURRENT curve, so the output is identical to the recompute-both version — this just skips
        // recomputing a value that is provably unchanged. After a note-on reset (x1=0, sh1=-1) the
        // first sample recomputes F(0) live (≠ 0 for any fold). The low-slew and fold-off branches
        // produce no F, so they invalidate the cache (sh1=-1) → the next real sample recomputes.
        // A midpoint-naive fallback handles the low-slew 0/0 case. [ADAA audit vs Waveshaper 75cb6a9]
        static inline float applyFoldADAA (float x, int shape, float amount, FoldState& st) noexcept
        {
            float y, Fx = 0.0f;
            bool  haveFx = false;
            if (amount <= 1.0e-6f)
            {
                y = x;                                                   // fold off → identity
            }
            else if (std::fabs (x - st.x1) < 1.0e-5f)
            {
                y = applyFold (0.5f * (x + st.x1), shape, amount);       // low-slew fallback
            }
            else
            {
                Fx = foldAntideriv (x, shape, amount);
                // F(x1): reuse the cache iff it was computed on the SAME curve, else recompute live.
                const float Fx1 = (st.sh1 == shape && st.am1 == amount)
                                    ? st.Fx1
                                    : foldAntideriv (st.x1, shape, amount);
                y = (Fx - Fx1) / (x - st.x1);
                haveFx = true;
            }
            st.x1 = x;
            if (haveFx) { st.Fx1 = Fx; st.sh1 = shape; st.am1 = amount; }
            else          st.sh1 = -1;                                    // invalidate cache
            return y;
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
        int    currentMidiNote_ = 60;
        float  currentVelocity_ = 1.0f;

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

        // 2× oversampling input-prev for linear-interp upsample (Ladder + Acid303)
        float                   osPrevL_     = 0.0f;
        float                   osPrevR_     = 0.0f;
        float                   osPrevB2L_   = 0.0f;   // oversample prev-state for the F2 routing bus
        float                   osPrevB2R_   = 0.0f;

        // Filter 2 — fully independent second FilterSlot (own type/cut/res/drv/env).
        // Shares the FLT envelope shape + EROSION drift, with its own ENV amount.
        tw::filters::FilterSlot filterSlot2_;
        float                   baseCutHz2_  = 20000.0f;
        float                   baseRes012_  = 0.0f;
        float                   drv012_      = 0.0f;
        float                   envAmount2_  = 0.0f;   // -1..+1 (bipolar)
        int                     filterType1_ = 0;      // tracked for NONE-aware routing
        int                     filterType2_ = (int) tw::filters::Type::NONE;
        // CPU: semitone→Hz pow() change-gates (unmodulated cutoff = bit-identical per sample).
        // Sentinel -1e9 never matches a real semitone sum, so the first sample always computes.
        float                   lastCutSemis1_ = -1.0e9f, lastCutHz1_ = 20000.0f;
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
                sl.on = wEff > 1e-5f || sl.w > 1e-5f;    // stays on to RAMP OUT (declick)
                if (! sl.on) { sl.dw = 0.f; sl.dn = (1.f - sl.n) * invN; sl.xf = 0.f; continue; }
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
            v = sl.osc.heat (v, sl.hCur) * sl.w;
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
        tw::ModalParams modalParamsA_, modalParamsB_, modalParamsC_, modalParamsD_;

        // ── BLEND MODES (Serum-2-style cross-osc warp) — per-voice state ──
        struct BlendSlotV { int mode = 0; int src = 0; float depth = 0.f; };   // depth = exp-biased target
        BlendSlotV blendSlot_[4][4];
        float blendDepthSm_[4][4] = {};   // per-sample de-zippered depth
        float modPrev_[4] = { 0.f, 0.f, 0.f, 0.f };   // prev-sample pre-gain osc outputs = the modulator taps
        float noiseModTap_ = 0.0f;                    // fb64 — the NOISE modulator tap (src=5), pre-gain, 1-sample delayed
        bool  noiseForce_  = false;                   // fb64 — noise is used as a blend source this block → generate it even if output off
        float fmPhase_[4] = { 0.f, 0.f, 0.f, 0.f };   // per-carrier FM integrator (freq-dev → phase; leaky, thru-zero)
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
        static constexpr int   kBlkRing     = 256;      // ring length (power of two → mask 255)
        static constexpr int   kBlkMaxOff   = 64;       // ± sample excursion at full depth (also the base delay)
        static constexpr float kBlkOffScale = 40.0f;    // blendOff (cycles) → samples  [EAR-TUNABLE: raise = deeper]
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
                        // VOICE-0-ANCHORED gain (Max: "unison turns shit down"): the dry voice —
                        // the only one carrying the un-sprayed attack — keeps single-voice level;
                        // only the detuned BED voices are RMS-normalised. Unison now adds around
                        // the dry sound instead of ducking it (≤ ~+3 dB total, no 1/√N punch loss).
                        const float gu = (u == 0) ? 1.0f : uNorm;
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
                    // VOICE-0-ANCHORED gain — see the warp-path comment above: dry voice at
                    // single-voice level, RMS-normalised bed around it. No 1/√N punch loss.
                    const float gu = (u == 0) ? 1.0f : uNorm;
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
            const float offS  = juce::jlimit (-(float) kBlkMaxOff, (float) kBlkMaxOff, offCycles * kBlkOffScale) * amt;
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
            renderHarmonicOsc (harmEngA_, harmParamsA_, engine_  == Engine::HARM, octOffset_,  semiOffset_,  centsOffset_ + coarseModA_ * 100.f,  harmBlkA_, harmBlkAL_, harmBlkAR_, numSamples, spraySeedA_, doOn, activeUnisonA_, uDetuneCentsA_.data(), uNormA_, blkGateLevel (0, level_));
            renderHarmonicOsc (harmEngB_, harmParamsB_, engineB_ == Engine::HARM, octOffsetB_, semiOffsetB_, centsOffsetB_ + coarseModB_ * 100.f, harmBlkB_, harmBlkBL_, harmBlkBR_, numSamples, spraySeedB_, doOn, activeUnisonB_, uDetuneCentsB_.data(), uNormB_, blkGateLevel (1, levelB_));
            renderHarmonicOsc (harmEngC_, harmParamsC_, engineC_ == Engine::HARM, octOffsetC_, semiOffsetC_, centsOffsetC_ + coarseModC_ * 100.f, harmBlkC_, harmBlkCL_, harmBlkCR_, numSamples, spraySeedC_, doOn, activeUnisonC_, uDetuneCentsC_.data(), uNormC_, blkGateLevel (2, levelC_));
            renderHarmonicOsc (harmEngD_, harmParamsD_, engineD_ == Engine::HARM, octOffsetD_, semiOffsetD_, centsOffsetD_ + coarseModD_ * 100.f, harmBlkD_, harmBlkDL_, harmBlkDR_, numSamples, spraySeedD_, doOn, activeUnisonD_, uDetuneCentsD_.data(), uNormD_, blkGateLevel (3, levelD_));
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
            renderModalOsc (modalEngA_, modalParamsA_, engine_  == Engine::MODAL, octOffset_,  semiOffset_,  centsOffset_ + coarseModA_ * 100.f,  modalBlkA_, modalBlkAL_, modalBlkAR_, numSamples, spraySeedA_, doOn, activeUnisonA_, uDetuneCentsA_.data(), uNormA_, blkGateLevel (0, level_),  exL[0], exN[0], exR[0]);
            renderModalOsc (modalEngB_, modalParamsB_, engineB_ == Engine::MODAL, octOffsetB_, semiOffsetB_, centsOffsetB_ + coarseModB_ * 100.f, modalBlkB_, modalBlkBL_, modalBlkBR_, numSamples, spraySeedB_, doOn, activeUnisonB_, uDetuneCentsB_.data(), uNormB_, blkGateLevel (1, levelB_), exL[1], exN[1], exR[1]);
            renderModalOsc (modalEngC_, modalParamsC_, engineC_ == Engine::MODAL, octOffsetC_, semiOffsetC_, centsOffsetC_ + coarseModC_ * 100.f, modalBlkC_, modalBlkCL_, modalBlkCR_, numSamples, spraySeedC_, doOn, activeUnisonC_, uDetuneCentsC_.data(), uNormC_, blkGateLevel (2, levelC_), exL[2], exN[2], exR[2]);
            renderModalOsc (modalEngD_, modalParamsD_, engineD_ == Engine::MODAL, octOffsetD_, semiOffsetD_, centsOffsetD_ + coarseModD_ * 100.f, modalBlkD_, modalBlkDL_, modalBlkDR_, numSamples, spraySeedD_, doOn, activeUnisonD_, uDetuneCentsD_.data(), uNormD_, blkGateLevel (3, levelD_), exL[3], exN[3], exR[3]);
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

        int   activeUnisonA_ = 1, activeUnisonB_ = 1;   // 1..kMaxUnison per OSC
        float uNormA_ = 1.0f,     uNormB_ = 1.0f;       // auto-gain: 1/sqrt(Σ blendGain²) — holds loudness as voices rise
        static constexpr float kUniMaxDetuneCents = 50.0f;  // ±50 cents (±½ semitone) of detune at 100 %

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
