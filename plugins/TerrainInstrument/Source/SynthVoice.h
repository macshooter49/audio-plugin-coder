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
#include "SampleEngine.h"          // SAMPLE-ENGINE-VOICE — per-OSC sample playback core
#include "SampleBuffer.h"          // SAMPLE-ENGINE-VOICE — shared lock-free buffer
#include "GranularEngine.h"        // GRANULAR-ENGINE-VOICE — per-OSC granular core
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
         *  StringArray in createParameterLayout: WT, SAMP, GRAN, SPEC, FM, NOISE. */
        enum class Engine : int { WT = 0, SAMP = 1, GRAN = 2, SPEC = 3, FM = 4, NOISE = 5 };

        static constexpr int kMaxUnison = 16;   // Serum-parity unison ceiling (was 8)

        bool canPlaySound (juce::SynthesiserSound* s) override
        {
            return dynamic_cast<SynthSound*> (s) != nullptr;
        }

        void setCurrentPlaybackSampleRate (double sr) override
        {
            juce::SynthesiserVoice::setCurrentPlaybackSampleRate (sr);
            sampleRate_ = (sr > 0.0) ? sr : 48000.0;
            ampEnv_.prepare (sampleRate_);
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
            sampleWarpA_.prepare (sampleRate_, 2, 1024); sampleWarpB_.prepare (sampleRate_, 2, 1024);
            sampleWarpC_.prepare (sampleRate_, 2, 1024); sampleWarpD_.prepare (sampleRate_, 2, 1024);
            airHpCoef_ = 1.0f - std::exp (-2.0f * juce::MathConstants<float>::pi * 3500.0f / (float) juce::jmax (1.0, sampleRate_));
            oscGateCoef_ = 1.0f - std::exp (-1.0f / (0.004f * (float) juce::jmax (1.0, sampleRate_)));  // ~4ms mute fade — click-free
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

        // ── Envelope follower taps (for the UI playhead dot) ──
        // Live amp-env output [0,1] and whether this voice is sounding. The editor
        // polls the most-active voice each timer tick and pushes this to the WebUI.
        float getAmpEnvLevel() const noexcept { return (float) ampEnv_.level(); }
        bool  isAmpEnvActive() const noexcept { return ampEnv_.isActive(); }
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
        }

        /** Most-recent L1 value (bipolar -1..+1) for the editor's live LFO dot. */
        float getSynthLfoVis() const noexcept { return lfoVisValue_; }
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
            panL_ = std::cos (angle);
            panR_ = std::sin (angle);
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
            const int clamped = juce::jlimit (0, 5, idx);
            engine_ = static_cast<Engine> (clamped);
        }

        /** Test-only accessor — not used in production audio path. */
        Engine engineForTesting() const noexcept { return engine_; }

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
            panLB_ = std::cos (angle);
            panRB_ = std::sin (angle);
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
            const int clamped = juce::jlimit (0, 5, idx);
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
            setUnisonImpl (activeUnisonA_, uDetuneCentsA_, uPanLA_, uPanRA_, uNormA_,
                           count, detune01, blend01, width01);
            updateUnisonFramePositions();
            if (currentMidiNote_ >= 0) updateUnisonPhaseIncrementsA (glideNote_);
        }
        void setUnisonB (int count, float detune01, float blend01, float width01) noexcept
        {
            setUnisonImpl (activeUnisonB_, uDetuneCentsB_, uPanLB_, uPanRB_, uNormB_,
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
                            int count, float detune01, float blend01, float width01) noexcept
        {
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
                const float g = 1.0f - (1.0f - bl) * std::fabs (u_norm);
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
        void setPhaseMode (int modeA, int modeB) noexcept
        {
            phaseModeA_ = juce::jlimit (0, 3, modeA);
            phaseModeB_ = juce::jlimit (0, 3, modeB);
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
            routeSrcA_  = juce::jlimit (0, 1, srcA);
            routeDestA_ = juce::jlimit (0, 2, destA);
            routeAmtA_  = juce::jlimit (-1.0f, 1.0f, amtA);
            routeSrcB_  = juce::jlimit (0, 1, srcB);
            routeDestB_ = juce::jlimit (0, 2, destB);
            routeAmtB_  = juce::jlimit (-1.0f, 1.0f, amtB);
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
        void setOscGates (float a, float b, float c, float d) noexcept
        {
            oscGateTarget_[0] = a; oscGateTarget_[1] = b; oscGateTarget_[2] = c; oscGateTarget_[3] = d;
            if (! playing_) { for (int k = 0; k < 4; ++k) oscGate_[k] = oscGateTarget_[k]; }  // snap when idle → fresh notes respect gate from sample 0, no blip
        }
        void setPanC (float pan) noexcept { const float p=juce::jlimit(-1.0f,1.0f,pan); const float a=(p+1.0f)*0.25f*juce::MathConstants<float>::pi; panLC_=std::cos(a); panRC_=std::sin(a); }
        void setPanD (float pan) noexcept { const float p=juce::jlimit(-1.0f,1.0f,pan); const float a=(p+1.0f)*0.25f*juce::MathConstants<float>::pi; panLD_=std::cos(a); panRD_=std::sin(a); }
        void setWavetableC (const tw::Wavetable* wt) noexcept { currentWavetableC_ = wt; }
        void setWavetableD (const tw::Wavetable* wt) noexcept { currentWavetableD_ = wt; }
        void setWavetableFrameC (float pos) noexcept { framePosBaseC_ = juce::jlimit (0.0f, 1.0f, pos); }
        void setWavetableFrameD (float pos) noexcept { framePosBaseD_ = juce::jlimit (0.0f, 1.0f, pos); }
        void setWarpC (int mode, float amount) noexcept { warpModeC_ = juce::jlimit(0,10,mode); warpAmountBaseC_ = juce::jlimit(0.0f,1.0f,amount); }
        void setWarpD (int mode, float amount) noexcept { warpModeD_ = juce::jlimit(0,10,mode); warpAmountBaseD_ = juce::jlimit(0.0f,1.0f,amount); }
        void setEngineC (int idx) noexcept { engineC_ = static_cast<Engine> (juce::jlimit(0,5,idx)); }
        void setEngineD (int idx) noexcept { engineD_ = static_cast<Engine> (juce::jlimit(0,5,idx)); }
        void setUnisonC (int count, float detune01, float blend01, float width01) noexcept { setUnisonImpl (activeUnisonC_, uDetuneCentsC_, uPanLC_, uPanRC_, uNormC_, count, detune01, blend01, width01); updateUnisonFramePositions(); if (currentMidiNote_ >= 0) updateUnisonPhaseIncrementsC (glideNote_); }
        void setUnisonD (int count, float detune01, float blend01, float width01) noexcept { setUnisonImpl (activeUnisonD_, uDetuneCentsD_, uPanLD_, uPanRD_, uNormD_, count, detune01, blend01, width01); updateUnisonFramePositions(); if (currentMidiNote_ >= 0) updateUnisonPhaseIncrementsD (glideNote_); }
        void setWarp2CD (int modeC, float amountC, int modeD, float amountD) noexcept { warp2ModeC_=juce::jlimit(0,10,modeC); warp2AmountBaseC_=juce::jlimit(0.0f,1.0f,amountC); warp2ModeD_=juce::jlimit(0,10,modeD); warp2AmountBaseD_=juce::jlimit(0.0f,1.0f,amountD); }
        void setBlurCD (float blurC01, float blurD01) noexcept { blurTargetC_=juce::jlimit(0.0f,1.0f,blurC01); blurTargetD_=juce::jlimit(0.0f,1.0f,blurD01); }
        void setPhaseModeCD (int modeC, int modeD) noexcept { phaseModeC_=juce::jlimit(0,3,modeC); phaseModeD_=juce::jlimit(0,3,modeD); }
        void setWaverCD (float c, float d) noexcept { waverC_=juce::jlimit(0.0f,1.0f,c); waverD_=juce::jlimit(0.0f,1.0f,d); }
        void setFoldCD (int shapeC, float amountC, int shapeD, float amountD) noexcept { foldShapeC_=juce::jlimit(0,2,shapeC); foldAmountBaseC_=juce::jlimit(0.0f,1.0f,amountC); foldShapeD_=juce::jlimit(0,2,shapeD); foldAmountBaseD_=juce::jlimit(0.0f,1.0f,amountD); }
        void setKeytrackCD (float depthC, int destC, float depthD, int destD) noexcept { ktDepthC_=juce::jlimit(0.0f,1.0f,depthC); ktDestC_=juce::jlimit(0,2,destC); ktDepthD_=juce::jlimit(0.0f,1.0f,depthD); ktDestD_=juce::jlimit(0,2,destD); }
        void setRouteCD (int srcC, int destC, float amtC, int srcD, int destD, float amtD) noexcept { routeSrcC_=juce::jlimit(0,1,srcC); routeDestC_=juce::jlimit(0,2,destC); routeAmtC_=juce::jlimit(-1.0f,1.0f,amtC); routeSrcD_=juce::jlimit(0,1,srcD); routeDestD_=juce::jlimit(0,2,destD); routeAmtD_=juce::jlimit(-1.0f,1.0f,amtD); }
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
                updateUnisonFramePositions();
                updateUnisonPhaseIncrementsA (glideNote_);
                updateUnisonPhaseIncrementsB (glideNote_);
                playing_ = true;
                return;   // amp/filter envelopes, phases, waver, fold history all untouched
            }

            currentMidiNote_ = midiNote;
            currentVelocity_ = velocity;
            beginGlide (midiNote);     // PORTAMENTO — snap or start the slide (sets glideNote_)
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

            // Phase 9: stereo scratch (OSC A + OSC B each pan independently).
            if (scratch_.getNumChannels() < 2 || scratch_.getNumSamples() < numSamples)
                scratch_.setSize (2, numSamples, false, true, true);
            scratch_.clear();
            auto* scratchL = scratch_.getWritePointer (0);
            auto* scratchR = scratch_.getWritePointer (1);

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
                float lfoPk[wc::NUM_LFOS];
                for (int L = 0; L < wc::NUM_LFOS; ++L) lfoPk[L] = synthLfo_[L].peek();
                {
                    float amt[wc::NUM_LFOS] = { 0.0f };
                    for (int a = 0; a < modConfig_.numAssignments; ++a)
                    {
                        const auto& as = modConfig_.assignments[a];
                        if (! as.enabled) continue;
                        const int sI = (int) as.source, dI = (int) as.dest;
                        if (sI < 0 || sI >= wc::NUM_LFOS) continue;
                        if (dI >= (int) wc::ModDest::LfoAmt1 && dI < (int) wc::ModDest::LfoAmt1 + wc::NUM_LFOS)
                            amt[dI - (int) wc::ModDest::LfoAmt1] += lfoPk[sI] * as.depth;
                    }
                    for (int L = 0; L < wc::NUM_LFOS; ++L) lfoPk[L] *= juce::jlimit (0.0f, 2.0f, 1.0f + amt[L]);
                }
                float mFrA = 0.0f, mWpA = 0.0f, mFdA = 0.0f, mFrB = 0.0f, mWpB = 0.0f, mFdB = 0.0f;
                float mFrC = 0.0f, mWpC = 0.0f, mFdC = 0.0f, mFrD = 0.0f, mWpD = 0.0f, mFdD = 0.0f;
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
                    else continue;
                    const float c = wc::routeContribution (wc::kDestInfo[(int) as.dest], srcV, as.depth);
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
                        default: break;
                    }
                }
                // FRAME/WARP/FOLD — keytrack crossfade + ROUTE + LFO mod, clamp once.
                framePos_    = juce::jlimit (0.0f, 1.0f, framePosBase_    + (ktDestA_ == kKtFrame ? ktDA * (ktRamp_ - framePosBase_)    : 0.0f) + (routeDestA_ == kRtFrame ? rtA : 0.0f) + mFrA);
                warpAmount_  = juce::jlimit (0.0f, 1.0f, warpAmountBase_  + (ktDestA_ == kKtWarp  ? ktDA * (ktRamp_ - warpAmountBase_)  : 0.0f) + (routeDestA_ == kRtWarp  ? rtA : 0.0f) + mWpA);
                foldAmountA_ = juce::jlimit (0.0f, 1.0f, foldAmountBaseA_ + (ktDestA_ == kKtFold  ? ktDA * (ktRamp_ - foldAmountBaseA_) : 0.0f) + (routeDestA_ == kRtFold  ? rtA : 0.0f) + mFdA);
                framePosB_   = juce::jlimit (0.0f, 1.0f, framePosBaseB_   + (ktDestB_ == kKtFrame ? ktDB * (ktRamp_ - framePosBaseB_)   : 0.0f) + (routeDestB_ == kRtFrame ? rtB : 0.0f) + mFrB);
                warpAmountB_ = juce::jlimit (0.0f, 1.0f, warpAmountBaseB_ + (ktDestB_ == kKtWarp  ? ktDB * (ktRamp_ - warpAmountBaseB_) : 0.0f) + (routeDestB_ == kRtWarp  ? rtB : 0.0f) + mWpB);
                warp2AmountA_ = warp2AmountBaseA_;   // WARP 2 base->effective (mod-matrix ready)
                warp2AmountB_ = warp2AmountBaseB_;
                foldAmountB_ = juce::jlimit (0.0f, 1.0f, foldAmountBaseB_ + (ktDestB_ == kKtFold  ? ktDB * (ktRamp_ - foldAmountBaseB_) : 0.0f) + (routeDestB_ == kRtFold  ? rtB : 0.0f) + mFdB);
                // OSC C / D — same keytrack + route + LFO mod, clamp once.
                framePosC_   = juce::jlimit (0.0f, 1.0f, framePosBaseC_   + (ktDestC_ == kKtFrame ? ktDC * (ktRamp_ - framePosBaseC_)   : 0.0f) + (routeDestC_ == kRtFrame ? rtC : 0.0f) + mFrC);
                warpAmountC_ = juce::jlimit (0.0f, 1.0f, warpAmountBaseC_ + (ktDestC_ == kKtWarp  ? ktDC * (ktRamp_ - warpAmountBaseC_) : 0.0f) + (routeDestC_ == kRtWarp  ? rtC : 0.0f) + mWpC);
                foldAmountC_ = juce::jlimit (0.0f, 1.0f, foldAmountBaseC_ + (ktDestC_ == kKtFold  ? ktDC * (ktRamp_ - foldAmountBaseC_) : 0.0f) + (routeDestC_ == kRtFold  ? rtC : 0.0f) + mFdC);
                warp2AmountC_ = warp2AmountBaseC_;
                framePosD_   = juce::jlimit (0.0f, 1.0f, framePosBaseD_   + (ktDestD_ == kKtFrame ? ktDD * (ktRamp_ - framePosBaseD_)   : 0.0f) + (routeDestD_ == kRtFrame ? rtD : 0.0f) + mFrD);
                warpAmountD_ = juce::jlimit (0.0f, 1.0f, warpAmountBaseD_ + (ktDestD_ == kKtWarp  ? ktDD * (ktRamp_ - warpAmountBaseD_) : 0.0f) + (routeDestD_ == kRtWarp  ? rtD : 0.0f) + mWpD);
                foldAmountD_ = juce::jlimit (0.0f, 1.0f, foldAmountBaseD_ + (ktDestD_ == kKtFold  ? ktDD * (ktRamp_ - foldAmountBaseD_) : 0.0f) + (routeDestD_ == kRtFold  ? rtD : 0.0f) + mFdD);
                warp2AmountD_ = warp2AmountBaseD_;
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
                const bool needFlt = (envDepth_[1] != 0.0f);
                const bool needPit = (envDepth_[2] != 0.0f);
                const bool needM1  = (envDepth_[3] != 0.0f);
                const bool needM2  = (envDepth_[4] != 0.0f);
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
            currentMipLevelA_ = tw::Wavetable::mipLevelForPhaseIncrement (uPhaseIncA_[0] * warpRateMul (warpMode_,  warpAmount_) * warpRateMul (warp2ModeA_, warp2AmountA_));
            currentMipLevelB_ = tw::Wavetable::mipLevelForPhaseIncrement (uPhaseIncB_[0] * warpRateMul (warpModeB_, warpAmountB_) * warpRateMul (warp2ModeB_, warp2AmountB_));
            currentMipLevelC_ = tw::Wavetable::mipLevelForPhaseIncrement (uPhaseIncC_[0] * warpRateMul (warpModeC_, warpAmountC_) * warpRateMul (warp2ModeC_, warp2AmountC_));
            currentMipLevelD_ = tw::Wavetable::mipLevelForPhaseIncrement (uPhaseIncD_[0] * warpRateMul (warpModeD_, warpAmountD_) * warpRateMul (warp2ModeD_, warp2AmountD_));

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
                if (fpA != lastFpA_ || blurA_ != lastBlurA_ || currentMipLevelA_ != lastMipA_ || currentWavetable_ != lastWtA_)
                {
                    currentWavetable_->renderBlend (currentMipLevelA_, fpA, blurA_, blendA_.data());
                    lastFpA_ = fpA; lastBlurA_ = blurA_; lastMipA_ = currentMipLevelA_; lastWtA_ = currentWavetable_;
                }
            }
            if (currentWavetableB_ != nullptr)
            {
                float fpB = framePosB_;
                if (interpModeB_ == 1) { const float Nf = 16.0f; fpB = std::round (fpB * (Nf - 1.0f)) / (Nf - 1.0f); }
                if (fpB != lastFpB_ || blurB_ != lastBlurB_ || currentMipLevelB_ != lastMipB_ || currentWavetableB_ != lastWtB_)
                {
                    currentWavetableB_->renderBlend (currentMipLevelB_, fpB, blurB_, blendB_.data());
                    lastFpB_ = fpB; lastBlurB_ = blurB_; lastMipB_ = currentMipLevelB_; lastWtB_ = currentWavetableB_;
                }
            }
            if (currentWavetableC_ != nullptr)
            {
                float fpC = framePosC_;
                if (interpModeC_ == 1) { const float Nf = 16.0f; fpC = std::round (fpC * (Nf - 1.0f)) / (Nf - 1.0f); }
                if (fpC != lastFpC_ || blurC_ != lastBlurC_ || currentMipLevelC_ != lastMipC_ || currentWavetableC_ != lastWtC_)
                {
                    currentWavetableC_->renderBlend (currentMipLevelC_, fpC, blurC_, blendC_.data());
                    lastFpC_ = fpC; lastBlurC_ = blurC_; lastMipC_ = currentMipLevelC_; lastWtC_ = currentWavetableC_;
                }
            }
            if (currentWavetableD_ != nullptr)
            {
                float fpD = framePosD_;
                if (interpModeD_ == 1) { const float Nf = 16.0f; fpD = std::round (fpD * (Nf - 1.0f)) / (Nf - 1.0f); }
                if (fpD != lastFpD_ || blurD_ != lastBlurD_ || currentMipLevelD_ != lastMipD_ || currentWavetableD_ != lastWtD_)
                {
                    currentWavetableD_->renderBlend (currentMipLevelD_, fpD, blurD_, blendD_.data());
                    lastFpD_ = fpD; lastBlurD_ = blurD_; lastMipD_ = currentMipLevelD_; lastWtD_ = currentWavetableD_;
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
                oscDead_[g] = oscGateTarget_[g] <= 0.0f && oscGate_[g] < 1.0e-4f;

            // SAMPLE-ENGINE-VOICE — render any SAMP oscillators' stereo blocks for this
            // buffer (scan/loop/xfade/spray + STRETCH/FORMANT warp). Cheap no-op if none.
            renderSampleBlocks (numSamples);
            renderGranularBlocks (numSamples);   // GRANULAR-ENGINE-VOICE — render any GRAN oscillators' blocks

            // CPU: SAMP/GRAN/SPEC oscs render whole blocks above and their result REPLACES the
            // unison sum below — the per-sine u-loop only produces zeros for them (fold of 0,
            // two pan MACs, discarded). Skip it entirely; the sums already start at 0.
            const bool uLoopA = ! oscDead_[0] && (engine_  != Engine::SAMP && engine_  != Engine::GRAN && engine_  != Engine::SPEC);
            const bool uLoopB = ! oscDead_[1] && (engineB_ != Engine::SAMP && engineB_ != Engine::GRAN && engineB_ != Engine::SPEC);
            const bool uLoopC = ! oscDead_[2] && (engineC_ != Engine::SAMP && engineC_ != Engine::GRAN && engineC_ != Engine::SPEC);
            const bool uLoopD = ! oscDead_[3] && (engineD_ != Engine::SAMP && engineD_ != Engine::GRAN && engineD_ != Engine::SPEC);
            for (int i = 0; i < numSamples; ++i)
            {
                // ── OSC A — sum across activeUnisonA_ sines (per-OSC unison) ─────
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
                                    sAu = tw::Wavetable::readCycle (blendA_.data(), (float) warpedPhase);
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

                        case Engine::NOISE:
                        {
                            // Noise is a single stream per voice — all unison sines hear the
                            // same noise sample. Compute PRNG + LP filter once at u==0,
                            // then broadcast the same noiseLpZ_ to all sines.
                            if (u == 0)
                            {
                                noiseState_ ^= noiseState_ << 13;
                                noiseState_ ^= noiseState_ >> 17;
                                noiseState_ ^= noiseState_ << 5;
                                const float white = static_cast<float> (static_cast<int32_t> (noiseState_))
                                                  * (1.0f / 2147483648.0f);
                                const float alpha = 1.0f - 0.98f * framePos_;
                                noiseLpZ_ += alpha * (white - noiseLpZ_);
                            }
                            const float drive = 1.0f + 8.0f * warpAmount_;
                            sAu = std::tanh (noiseLpZ_ * drive);
                            sAu *= 1.0f + 0.5f * framePos_;
                            break;
                        }

                        case Engine::FM:
                        {
                            const double ratio  = 0.25 + std::pow (32.0, (double) framePos_) * 0.234375;
                            const double modInc = uPhaseIncA_[(size_t) u] * ratio;
                            const double depth  = (double) warpAmount_ * 6.2831853071795865;
                            const double pi2    = 6.2831853071795865;
                            const double modOut = std::sin (pi2 * uModPhaseA_[(size_t) u]);
                            sAu = static_cast<float> (std::sin (pi2 * uPhaseA_[(size_t) u] + depth * modOut));
                            uModPhaseA_[(size_t) u] += modInc;
                            if (uModPhaseA_[(size_t) u] >= 1.0) uModPhaseA_[(size_t) u] -= std::floor (uModPhaseA_[(size_t) u]);
                            uPhaseA_[(size_t) u] += uPhaseIncA_[(size_t) u];
                            if (uPhaseA_[(size_t) u] >= 1.0) uPhaseA_[(size_t) u] -= 1.0;
                            break;
                        }

                        case Engine::SAMP:
                        case Engine::GRAN:
                        case Engine::SPEC:
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
                if (engine_ == Engine::WT
                    && ((warpMode_ == 9 && warpAmount_ > 0.001f) || (warp2ModeA_ == 9 && warp2AmountA_ > 0.001f)))
                { sA_L = wtRectDcAL_.process (sA_L); sA_R = wtRectDcAR_.process (sA_R); }
                if (engine_ == Engine::GRAN) { sA_L = granBlkAL_[(size_t) i]; sA_R = granBlkAR_[(size_t) i]; }   // GRANULAR-ENGINE-VOICE
                if (engine_ == Engine::SAMP) { sA_L = sampBlkAL_[(size_t) i]; sA_R = sampBlkAR_[(size_t) i];  // SAMPLE-ENGINE-VOICE
                    const float airA = sampleParamsA_.air;   // AIR exciter — add generated high harmonics
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
                if (engine_ == Engine::SAMP || engine_ == Engine::GRAN) {
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
                                    sBu = tw::Wavetable::readCycle (blendB_.data(), (float) warpedPhase);
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

                        case Engine::NOISE:
                        {
                            // OSC B noise is single-stream per voice — all unison sines hear the same noise.
                            if (u == 0)
                            {
                                noiseStateB_ ^= noiseStateB_ << 13;
                                noiseStateB_ ^= noiseStateB_ >> 17;
                                noiseStateB_ ^= noiseStateB_ << 5;
                                const float white = static_cast<float> (static_cast<int32_t> (noiseStateB_))
                                                  * (1.0f / 2147483648.0f);
                                const float alpha = 1.0f - 0.98f * framePosB_;
                                noiseLpZB_ += alpha * (white - noiseLpZB_);
                            }
                            const float drive = 1.0f + 8.0f * warpAmountB_;
                            sBu = std::tanh (noiseLpZB_ * drive);
                            sBu *= 1.0f + 0.5f * framePosB_;
                            break;
                        }

                        case Engine::FM:
                        {
                            const double ratio  = 0.25 + std::pow (32.0, (double) framePosB_) * 0.234375;
                            const double modInc = uPhaseIncB_[(size_t) u] * ratio;
                            const double depth  = (double) warpAmountB_ * 6.2831853071795865;
                            const double pi2    = 6.2831853071795865;
                            const double modOut = std::sin (pi2 * uModPhaseB_[(size_t) u]);
                            sBu = static_cast<float> (std::sin (pi2 * uPhaseB_[(size_t) u] + depth * modOut));
                            uModPhaseB_[(size_t) u] += modInc;
                            if (uModPhaseB_[(size_t) u] >= 1.0) uModPhaseB_[(size_t) u] -= std::floor (uModPhaseB_[(size_t) u]);
                            uPhaseB_[(size_t) u] += uPhaseIncB_[(size_t) u];
                            if (uPhaseB_[(size_t) u] >= 1.0) uPhaseB_[(size_t) u] -= 1.0;
                            break;
                        }

                        case Engine::SAMP:
                        case Engine::GRAN:
                        case Engine::SPEC:
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
                if (engineB_ == Engine::WT
                    && ((warpModeB_ == 9 && warpAmountB_ > 0.001f) || (warp2ModeB_ == 9 && warp2AmountB_ > 0.001f)))
                { sB_L = wtRectDcBL_.process (sB_L); sB_R = wtRectDcBR_.process (sB_R); }
                if (engineB_ == Engine::GRAN) { sB_L = granBlkBL_[(size_t) i]; sB_R = granBlkBR_[(size_t) i]; }   // GRANULAR-ENGINE-VOICE
                if (engineB_ == Engine::SAMP) { sB_L = sampBlkBL_[(size_t) i]; sB_R = sampBlkBR_[(size_t) i];  // SAMPLE-ENGINE-VOICE
                    const float airB = sampleParamsB_.air;   // AIR exciter — add generated high harmonics
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
                if (engineB_ == Engine::SAMP || engineB_ == Engine::GRAN) {
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
                                    sCu = tw::Wavetable::readCycle (blendC_.data(), (float) warpedPhase);
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

                        case Engine::NOISE:
                        {
                            // OSC C noise is single-stream per voice — all unison sines hear the same noise.
                            if (u == 0)
                            {
                                noiseStateC_ ^= noiseStateC_ << 13;
                                noiseStateC_ ^= noiseStateC_ >> 17;
                                noiseStateC_ ^= noiseStateC_ << 5;
                                const float white = static_cast<float> (static_cast<int32_t> (noiseStateC_))
                                                  * (1.0f / 2147483648.0f);
                                const float alpha = 1.0f - 0.98f * framePosC_;
                                noiseLpZC_ += alpha * (white - noiseLpZC_);
                            }
                            const float drive = 1.0f + 8.0f * warpAmountC_;
                            sCu = std::tanh (noiseLpZC_ * drive);
                            sCu *= 1.0f + 0.5f * framePosC_;
                            break;
                        }

                        case Engine::FM:
                        {
                            const double ratio  = 0.25 + std::pow (32.0, (double) framePosC_) * 0.234375;
                            const double modInc = uPhaseIncC_[(size_t) u] * ratio;
                            const double depth  = (double) warpAmountC_ * 6.2831853071795865;
                            const double pi2    = 6.2831853071795865;
                            const double modOut = std::sin (pi2 * uModPhaseC_[(size_t) u]);
                            sCu = static_cast<float> (std::sin (pi2 * uPhaseC_[(size_t) u] + depth * modOut));
                            uModPhaseC_[(size_t) u] += modInc;
                            if (uModPhaseC_[(size_t) u] >= 1.0) uModPhaseC_[(size_t) u] -= std::floor (uModPhaseC_[(size_t) u]);
                            uPhaseC_[(size_t) u] += uPhaseIncC_[(size_t) u];
                            if (uPhaseC_[(size_t) u] >= 1.0) uPhaseC_[(size_t) u] -= 1.0;
                            break;
                        }

                        case Engine::SAMP:
                        case Engine::GRAN:
                        case Engine::SPEC:
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
                if (engineC_ == Engine::WT
                    && ((warpModeC_ == 9 && warpAmountC_ > 0.001f) || (warp2ModeC_ == 9 && warp2AmountC_ > 0.001f)))
                { sC_L = wtRectDcCL_.process (sC_L); sC_R = wtRectDcCR_.process (sC_R); }
                if (engineC_ == Engine::GRAN) { sC_L = granBlkCL_[(size_t) i]; sC_R = granBlkCR_[(size_t) i]; }   // GRANULAR-ENGINE-VOICE
                if (engineC_ == Engine::SAMP) { sC_L = sampBlkCL_[(size_t) i]; sC_R = sampBlkCR_[(size_t) i];  // SAMPLE-ENGINE-VOICE
                    const float airC = sampleParamsC_.air;   // AIR exciter — add generated high harmonics
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
                if (engineC_ == Engine::SAMP || engineC_ == Engine::GRAN) {
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
                                    sDu = tw::Wavetable::readCycle (blendD_.data(), (float) warpedPhase);
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

                        case Engine::NOISE:
                        {
                            // OSC D noise is single-stream per voice — all unison sines hear the same noise.
                            if (u == 0)
                            {
                                noiseStateD_ ^= noiseStateD_ << 13;
                                noiseStateD_ ^= noiseStateD_ >> 17;
                                noiseStateD_ ^= noiseStateD_ << 5;
                                const float white = static_cast<float> (static_cast<int32_t> (noiseStateD_))
                                                  * (1.0f / 2147483648.0f);
                                const float alpha = 1.0f - 0.98f * framePosD_;
                                noiseLpZD_ += alpha * (white - noiseLpZD_);
                            }
                            const float drive = 1.0f + 8.0f * warpAmountD_;
                            sDu = std::tanh (noiseLpZD_ * drive);
                            sDu *= 1.0f + 0.5f * framePosD_;
                            break;
                        }

                        case Engine::FM:
                        {
                            const double ratio  = 0.25 + std::pow (32.0, (double) framePosD_) * 0.234375;
                            const double modInc = uPhaseIncD_[(size_t) u] * ratio;
                            const double depth  = (double) warpAmountD_ * 6.2831853071795865;
                            const double pi2    = 6.2831853071795865;
                            const double modOut = std::sin (pi2 * uModPhaseD_[(size_t) u]);
                            sDu = static_cast<float> (std::sin (pi2 * uPhaseD_[(size_t) u] + depth * modOut));
                            uModPhaseD_[(size_t) u] += modInc;
                            if (uModPhaseD_[(size_t) u] >= 1.0) uModPhaseD_[(size_t) u] -= std::floor (uModPhaseD_[(size_t) u]);
                            uPhaseD_[(size_t) u] += uPhaseIncD_[(size_t) u];
                            if (uPhaseD_[(size_t) u] >= 1.0) uPhaseD_[(size_t) u] -= 1.0;
                            break;
                        }

                        case Engine::SAMP:
                        case Engine::GRAN:
                        case Engine::SPEC:
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
                if (engineD_ == Engine::WT
                    && ((warpModeD_ == 9 && warpAmountD_ > 0.001f) || (warp2ModeD_ == 9 && warp2AmountD_ > 0.001f)))
                { sD_L = wtRectDcDL_.process (sD_L); sD_R = wtRectDcDR_.process (sD_R); }
                if (engineD_ == Engine::GRAN) { sD_L = granBlkDL_[(size_t) i]; sD_R = granBlkDR_[(size_t) i]; }   // GRANULAR-ENGINE-VOICE
                if (engineD_ == Engine::SAMP) { sD_L = sampBlkDL_[(size_t) i]; sD_R = sampBlkDR_[(size_t) i];  // SAMPLE-ENGINE-VOICE
                    const float airD = sampleParamsD_.air;   // AIR exciter — add generated high harmonics
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
                if (engineD_ == Engine::SAMP || engineD_ == Engine::GRAN) {
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
                for (int g = 0; g < 4; ++g) oscGate_[g] += (oscGateTarget_[g] - oscGate_[g]) * oscGateCoef_;
                const float gA = oscGate_[0], gB = oscGate_[1], gC = oscGate_[2], gD = oscGate_[3];

                // Sum to stereo with INDEPENDENT per-osc level + pan (× solo/mute gate)
                scratchL[i] = (sA_L * level_ * panL_ * gA + sB_L * levelB_ * panLB_ * gB + sC_L * levelC_ * panLC_ * gC + sD_L * levelD_ * panLD_ * gD) * velEnv;
                scratchR[i] = (sA_R * level_ * panR_ * gA + sB_R * levelB_ * panRB_ * gB + sC_R * levelC_ * panRC_ * gC + sD_R * levelD_ * panRD_ * gD) * velEnv;
            }

            // Phase 8a polish — apply steal-fade and decide if voice should die
            if (stealing_)
            {
                for (int i = 0; i < numSamples; ++i)
                {
                    scratchL[i] *= stealingFade_;
                    scratchR[i] *= stealingFade_;
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
                const float  nyq = 0.5f * (float) sr;
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
                            if (sI < 0 || sI >= wc::NUM_LFOS) continue;
                            if (dI >= (int) wc::ModDest::LfoAmt1 && dI < (int) wc::ModDest::LfoAmt1 + wc::NUM_LFOS)
                                amt[dI - (int) wc::ModDest::LfoAmt1] += lfoOut_[sI] * as.depth;
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
                    const float cutSemis1 = baseCutSemis  + fMod1 * 96.0f + lfoSemis1 + driftSemis + ktCutSemis1;
                    if (cutSemis1 != lastCutSemis1_)
                    {
                        lastCutSemis1_ = cutSemis1;
                        lastCutHz1_ = juce::jlimit (20.0f, fmax, 440.0f * std::pow (2.0f, (cutSemis1 - 69.0f) / 12.0f));
                    }
                    const float res1 = juce::jlimit (0.0f, 1.0f,
                        baseRes01_ + resWander * driftState_ * 0.5f);
                    filterSlot_.setParams (lastCutHz1_, res1, drv01_, coefSr);

                    // Filter 2 cutoff: base + routed envelopes (±96 ST) + LFO + drift.
                    const float cutSemis2 = baseCutSemis2 + fMod2 * 96.0f + lfoSemis2 + driftSemis + ktCutSemis2;
                    if (cutSemis2 != lastCutSemis2_)
                    {
                        lastCutSemis2_ = cutSemis2;
                        lastCutHz2_ = juce::jlimit (20.0f, fmax, 440.0f * std::pow (2.0f, (cutSemis2 - 69.0f) / 12.0f));
                    }
                    const float res2 = juce::jlimit (0.0f, 1.0f,
                        baseRes012_ + resWander * driftState_ * 0.5f);
                    filterSlot2_.setParams (lastCutHz2_, res2, drv012_, coefSr);

                    // Routing + per-filter wet/dry mix. NONE-aware so a bypassed
                    // slot drops out of the sum cleanly. Filters are independent.
                    const bool a1 = (filterType1_ != kNoneType);
                    const bool a2 = (filterType2_ != kNoneType);
                    auto applyFilters = [&] (float& L, float& R)
                    {
                        const float dryL = L, dryR = R;
                        if (filterRouting_ == 0)          // SERIES: x → F1 → F2
                        {
                            float l = L, r = R;
                            if (a1) { float wl = l, wr = r; filterSlot_.processStereo (wl, wr);
                                      l = filterMix1_ * wl + (1.0f - filterMix1_) * l;
                                      r = filterMix1_ * wr + (1.0f - filterMix1_) * r; }
                            if (a2) { float wl = l, wr = r; filterSlot2_.processStereo (wl, wr);
                                      l = filterMix2_ * wl + (1.0f - filterMix2_) * l;
                                      r = filterMix2_ * wr + (1.0f - filterMix2_) * r; }
                            L = l; R = r;
                        }
                        else                              // PARALLEL: F1(x) + F2(x)
                        {
                            float b1L = dryL, b1R = dryR, b2L = dryL, b2R = dryR;
                            if (a1) { filterSlot_.processStereo (b1L, b1R);
                                      b1L = filterMix1_ * b1L + (1.0f - filterMix1_) * dryL;
                                      b1R = filterMix1_ * b1R + (1.0f - filterMix1_) * dryR; }
                            if (a2) { filterSlot2_.processStereo (b2L, b2R);
                                      b2L = filterMix2_ * b2L + (1.0f - filterMix2_) * dryL;
                                      b2R = filterMix2_ * b2R + (1.0f - filterMix2_) * dryR; }
                            if (a1 && a2) { L = 0.5f * (b1L + b2L); R = 0.5f * (b1R + b2R); }
                            else if (a1)  { L = b1L; R = b1R; }
                            else if (a2)  { L = b2L; R = b2R; }
                            else          { L = dryL; R = dryR; }
                        }
                    };

                    if (oversample)
                    {
                        // 2× linear-interp upsample → filter twice → box decimate.
                        const float midL = 0.5f * (osPrevL_ + sL[i]);
                        const float midR = 0.5f * (osPrevR_ + sR[i]);
                        float yMidL = midL, yMidR = midR; applyFilters (yMidL, yMidR);
                        float yL = sL[i], yR = sR[i];      applyFilters (yL, yR);
                        sL[i] = 0.5f * (yMidL + yL);
                        sR[i] = 0.5f * (yMidR + yR);
                        osPrevL_ = sL[i]; osPrevR_ = sR[i];
                    }
                    else
                    {
                        applyFilters (sL[i], sR[i]);
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
                    + pitchEnvSemis_;                                  // PITCH envelope (Batch 3)
                const double hz = 440.0 * std::pow (2.0, semitones / 12.0);
                uPhaseIncA_[(size_t) u] = hz / sampleRate_;
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
                    + pitchEnvSemis_;                                  // PITCH envelope (Batch 3)
                const double hz = 440.0 * std::pow (2.0, semitones / 12.0);
                uPhaseIncB_[(size_t) u] = hz / sampleRate_;
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
                    + pitchEnvSemis_;
                const double hz = 440.0 * std::pow (2.0, semitones / 12.0);
                uPhaseIncC_[(size_t) u] = hz / sampleRate_;
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
                    + pitchEnvSemis_;
                const double hz = 440.0 * std::pow (2.0, semitones / 12.0);
                uPhaseIncD_[(size_t) u] = hz / sampleRate_;
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

        struct FoldState { float x1 = 0.0f; };

        // 1st-order ADAA: y[n] = (F(x[n]) − F(x[n−1])) / (x[n] − x[n−1]).
        // NEVER cache F — recompute BOTH antiderivatives live each sample. This is
        // correct after a note-on reset (x1=0 evaluates the real F(0), which is NOT 0
        // for any fold shape) and stays correct when shape/amount change between samples
        // (both terms evaluate on the current curve). [ADAA audit vs Waveshaper 75cb6a9]
        // A midpoint-naive fallback handles the low-slew 0/0 case.
        static inline float applyFoldADAA (float x, int shape, float amount, FoldState& st) noexcept
        {
            float y;
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
                const float Fx  = foldAntideriv (x,     shape, amount);
                const float Fx1 = foldAntideriv (st.x1, shape, amount);  // recomputed live, never cached
                y = (Fx - Fx1) / (x - st.x1);
            }
            st.x1 = x;
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
        float                   lastCutSemis2_ = -1.0e9f, lastCutHz2_ = 20000.0f;
        // Routing between the two filters + per-filter wet/dry mix.
        int                     filterRouting_ = 0;    // 0 = series, 1 = parallel
        float                   filterMix1_  = 1.0f;   // 0 = dry, 1 = fully filtered
        float                   filterMix2_  = 1.0f;

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
        float                          panL_  = 0.7071f;  // cos(pi/4)
        float                          panR_  = 0.7071f;  // sin(pi/4)

        // SOLO/MUTE — per-osc (A,B,C,D) click-free gate (smoothed one-pole, ~4ms fade)
        float oscGate_[4]       { 1.0f, 1.0f, 1.0f, 1.0f };   // smoothed solo/mute gate (click-free)
        bool  oscDead_[4]       { false, false, false, false }; // gate fully settled at 0 → skip the osc's render entirely
        float oscGateTarget_[4] { 1.0f, 1.0f, 1.0f, 1.0f };
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
        std::uint32_t sampleSprayRng_ = 0x12345u, spraySeedA_ = 0, spraySeedB_ = 0, spraySeedC_ = 0, spraySeedD_ = 0;
        // AIR exciter — per-voice/per-channel one-pole HP-split state + coefficient.
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
            // CPU: an osc at LEVEL 0 contributes exactly 0 to the sum (out × level_ × pan), and
            // level_ is block-constant (not per-sample modulated — Level isn't a mod dest). So skip
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
            renderSampleOsc (sampleEngA_, sampleWarpA_, sampleParamsA_, engine_  == Engine::SAMP, octOffset_,  semiOffset_,  centsOffset_,  sampleBlkA_, sampBlkAL_, sampBlkAR_, numSamples, spraySeedA_, doOn, sampleNativeOverOut_[0], activeUnisonA_, uDetuneCentsA_.data(), uPanLA_.data(), uPanRA_.data(), uNormA_, oscDead_[0] ? 0.0f : level_);
            renderSampleOsc (sampleEngB_, sampleWarpB_, sampleParamsB_, engineB_ == Engine::SAMP, octOffsetB_, semiOffsetB_, centsOffsetB_, sampleBlkB_, sampBlkBL_, sampBlkBR_, numSamples, spraySeedB_, doOn, sampleNativeOverOut_[1], activeUnisonB_, uDetuneCentsB_.data(), uPanLB_.data(), uPanRB_.data(), uNormB_, oscDead_[1] ? 0.0f : levelB_);
            renderSampleOsc (sampleEngC_, sampleWarpC_, sampleParamsC_, engineC_ == Engine::SAMP, octOffsetC_, semiOffsetC_, centsOffsetC_, sampleBlkC_, sampBlkCL_, sampBlkCR_, numSamples, spraySeedC_, doOn, sampleNativeOverOut_[2], activeUnisonC_, uDetuneCentsC_.data(), uPanLC_.data(), uPanRC_.data(), uNormC_, oscDead_[2] ? 0.0f : levelC_);
            renderSampleOsc (sampleEngD_, sampleWarpD_, sampleParamsD_, engineD_ == Engine::SAMP, octOffsetD_, semiOffsetD_, centsOffsetD_, sampleBlkD_, sampBlkDL_, sampBlkDR_, numSamples, spraySeedD_, doOn, sampleNativeOverOut_[3], activeUnisonD_, uDetuneCentsD_.data(), uPanLD_.data(), uPanRD_.data(), uNormD_, oscDead_[3] ? 0.0f : levelD_);
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
            renderGranularOsc (granEngA_, granParamsA_, engine_  == Engine::GRAN, octOffset_,  semiOffset_,  centsOffset_,  granBlkA_, granBlkAL_, granBlkAR_, numSamples, spraySeedA_, doOn, granNativeOverOut_[0], activeUnisonA_, uDetuneCentsA_.data(), uNormA_, oscDead_[0] ? 0.0f : level_);
            renderGranularOsc (granEngB_, granParamsB_, engineB_ == Engine::GRAN, octOffsetB_, semiOffsetB_, centsOffsetB_, granBlkB_, granBlkBL_, granBlkBR_, numSamples, spraySeedB_, doOn, granNativeOverOut_[1], activeUnisonB_, uDetuneCentsB_.data(), uNormB_, oscDead_[1] ? 0.0f : levelB_);
            renderGranularOsc (granEngC_, granParamsC_, engineC_ == Engine::GRAN, octOffsetC_, semiOffsetC_, centsOffsetC_, granBlkC_, granBlkCL_, granBlkCR_, numSamples, spraySeedC_, doOn, granNativeOverOut_[2], activeUnisonC_, uDetuneCentsC_.data(), uNormC_, oscDead_[2] ? 0.0f : levelC_);
            renderGranularOsc (granEngD_, granParamsD_, engineD_ == Engine::GRAN, octOffsetD_, semiOffsetD_, centsOffsetD_, granBlkD_, granBlkDL_, granBlkDR_, numSamples, spraySeedD_, doOn, granNativeOverOut_[3], activeUnisonD_, uDetuneCentsD_.data(), uNormD_, oscDead_[3] ? 0.0f : levelD_);
            granNoteOnPending_ = false;
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
        float lastFpA_ = -2.0f, lastBlurA_ = -2.0f; int lastMipA_ = -2;
        float lastFpB_ = -2.0f, lastBlurB_ = -2.0f; int lastMipB_ = -2;
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
        std::array<float,  kMaxUnison> uPanLA_        {};
        std::array<float,  kMaxUnison> uPanRA_        {};
        std::array<float,  kMaxUnison> uPanLB_        {};
        std::array<float,  kMaxUnison> uPanRB_        {};

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
        float  lastFpC_ = -2.0f, lastBlurC_ = -2.0f; int lastMipC_ = -2;
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
        std::array<float, kMaxUnison> uDetuneCentsC_{}, uPanLC_{}, uPanRC_{};
        int    activeUnisonC_ = 1; float uNormC_ = 1.0f;
        float  waverC_ = 0.0f; float waverCentsC_[kMaxUnison]{}; std::uint32_t waverRngC_[kMaxUnison]{};
        // ── OSC D ──
        float  levelD_ = 0.0f;                           // start silent (spec)
        float  panLD_ = 0.7071f, panRD_ = 0.7071f;
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
        float  lastFpD_ = -2.0f, lastBlurD_ = -2.0f; int lastMipD_ = -2;
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
        std::array<float, kMaxUnison> uDetuneCentsD_{}, uPanLD_{}, uPanRD_{};
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
