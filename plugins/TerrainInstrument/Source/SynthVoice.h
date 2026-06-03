// SynthVoice.h — Terrain Instrument synth section, Phase 1 (MPV)
// One PolyBLEP saw oscillator → juce::dsp::LadderFilter (LPF24) → juce::ADSR.
// Mirrors the SamplerVoice.h pattern (header-only). 8 voices allocated by
// PluginProcessor against a single SynthSound sentinel.
#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include "Wavetable.h"
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

        static constexpr int kMaxUnison = 8;

        bool canPlaySound (juce::SynthesiserSound* s) override
        {
            return dynamic_cast<SynthSound*> (s) != nullptr;
        }

        void setCurrentPlaybackSampleRate (double sr) override
        {
            juce::SynthesiserVoice::setCurrentPlaybackSampleRate (sr);
            sampleRate_ = (sr > 0.0) ? sr : 48000.0;
            ampEnv_.setSampleRate (sampleRate_);
            ampEnv_.setParameters (ampParams_);
        }

        /** Set AMP envelope params. attackMs/decayMs/releaseMs are milliseconds;
         *  sustain is 0..1. Called from PluginProcessor each block. */
        void setAmpEnvelopeParameters (float attackMs, float decayMs,
                                       float sustain, float releaseMs) noexcept
        {
            ampParams_.attack  = juce::jmax (0.001f, attackMs  * 0.001f);
            ampParams_.decay   = juce::jmax (0.001f, decayMs   * 0.001f);
            ampParams_.sustain = juce::jlimit (0.0f, 1.0f, sustain);
            ampParams_.release = juce::jmax (0.001f, releaseMs * 0.001f);
            ampEnv_.setParameters (ampParams_);
        }

        /** Called from PluginProcessor::prepareToPlay. Sizes filter + caches
         *  block-size for the per-block AudioBlock view. Phase 9: always stereo
         *  (2 channels) so OSC A + OSC B can be panned independently.
         *  Phase 8a: also prepares per-channel HORIZON high-shelf filters. */
        void prepareToPlay (double sr, int samplesPerBlock, int /*numChannels*/) noexcept
        {
            setCurrentPlaybackSampleRate (sr);
            juce::dsp::ProcessSpec spec;
            spec.sampleRate       = sr;
            spec.maximumBlockSize = (juce::uint32) samplesPerBlock;
            spec.numChannels      = 2;   // always stereo for OSC A + B per-osc pan
            filter_.prepare (spec);
            filter_.setMode (juce::dsp::LadderFilterMode::LPF24);
            filter_.reset();

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
        }

        /** Cutoff in Hz (20..20000), resonance 0..1. Called per block from
         *  PluginProcessor. */
        void setFilterParameters (float cutoffHz, float resonance) noexcept
        {
            filter_.setCutoffFrequencyHz (juce::jlimit (20.0f, 20000.0f, cutoffHz));
            filter_.setResonance         (juce::jlimit (0.0f,  1.0f,    resonance));
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
                updatePhaseIncrementFromMidi (currentMidiNote_);
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
            framePos_ = juce::jlimit (0.0f, 1.0f, pos);
        }

        /** Phase 2C — Warp mode (0=NONE, 1=BEND, 2=SYNC, 3=FORMANT) +
         *  amount 0..1. Applied to phase BEFORE wavetable lookup, so warp
         *  composes cleanly with any wavetable choice. */
        void setWarp (int mode, float amount) noexcept
        {
            warpMode_   = juce::jlimit (0, 3, mode);
            warpAmount_ = juce::jlimit (0.0f, 1.0f, amount);
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

        // ── Phase 9 — OSC B setters (mirror of OSC A) ─────────────────────

        void setTuningB (int oct, int semi, float cent) noexcept
        {
            octOffsetB_   = oct;
            semiOffsetB_  = semi;
            centsOffsetB_ = cent;
            if (playing_)
                updatePhaseIncrementBFromMidi (currentMidiNote_);
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
        void setWavetableFrameB (float pos) noexcept { framePosB_ = juce::jlimit (0.0f, 1.0f, pos); }

        void setWarpB (int mode, float amount) noexcept
        {
            warpModeB_   = juce::jlimit (0, 3, mode);
            warpAmountB_ = juce::jlimit (0.0f, 1.0f, amount);
        }

        void setEngineB (int idx) noexcept
        {
            const int clamped = juce::jlimit (0, 5, idx);
            engineB_ = static_cast<Engine> (clamped);
        }

        // ── Phase 8a — Unison + EROSION + HORIZON setters ─────────────────

        /** Set extra unison detune + pan offset on this voice. Called by
         *  UnisonSynth::noteOn before startVoice so each stack member has a
         *  unique position in the chorus spread. */
        void setUnisonOffsets (float detuneCents, float panOffset) noexcept
        {
            unisonDetuneCents_ = detuneCents;
            unisonPanOffset_   = juce::jlimit (-1.0f, 1.0f, panOffset);
        }

        /** Phase 8b — Set UNISON config (count + spread) on this voice. Called
         *  per-block from PluginProcessor broadcast. Computes per-sine detune
         *  cents (max ±25 at spread=1.0) + equal-power pan L/R. */
        void setUnison (int count, float spread01) noexcept
        {
            activeUnison_   = juce::jlimit (1, kMaxUnison, count);
            unisonSpread01_ = juce::jlimit (0.0f, 1.0f, spread01);

            // Compute per-sine detune cents + pan.
            for (int u = 0; u < activeUnison_; ++u)
            {
                if (activeUnison_ <= 1)
                {
                    uDetuneCents_[(size_t) u] = 0.0f;
                    uPanL_[(size_t) u] = 0.7071f;
                    uPanR_[(size_t) u] = 0.7071f;
                    continue;
                }
                // u_norm in [-1, +1] across the unison stack.
                const float u_norm = ((float) u / (float) (activeUnison_ - 1)) * 2.0f - 1.0f;
                uDetuneCents_[(size_t) u] = u_norm * unisonSpread01_ * 25.0f;
                // Equal-power pan: angle in [0, π/2].
                const float panAmt = u_norm * unisonSpread01_;       // -1..+1
                const float angle  = (panAmt + 1.0f) * 0.25f * juce::MathConstants<float>::pi;
                uPanL_[(size_t) u] = std::cos (angle);
                uPanR_[(size_t) u] = std::sin (angle);
            }
            // Zero out unused slots so render-loop summation is safe even if
            // activeUnison_ changes mid-playback.
            for (int u = activeUnison_; u < kMaxUnison; ++u)
            {
                uDetuneCents_[(size_t) u] = 0.0f;
                uPanL_[(size_t) u] = 0.0f;
                uPanR_[(size_t) u] = 0.0f;
            }
        }

        /** EROSION amount 0..1 (set per-block from APVTS SYN_EROSION/100). */
        void setErosionAmount (float a) noexcept { erosionAmount_ = juce::jlimit (0.0f, 1.0f, a); }

        /** HORIZON tilt -1..+1 (set per-block from APVTS SYN_HORIZON/100). */
        void setHorizonAmount (float h) noexcept { horizonAmount_ = juce::jlimit (-1.0f, 1.0f, h); }

        void startNote (int midiNote, float velocity,
                        juce::SynthesiserSound*, int /*pitchWheelPos*/) override
        {
            currentMidiNote_ = midiNote;
            currentVelocity_ = velocity;
            // OSC A resets
            phase_           = 0.0;
            modPhase_        = 0.0;      // Phase 3 — FM modulator reset
            noiseLpZ_        = 0.0f;     // Phase 3 — NOISE filter memory reset
            syncPhase_       = 0.0;      // Phase 2C SYNC reset (good hygiene)
            // OSC B resets (Phase 9)
            phaseB_          = 0.0;
            modPhaseB_       = 0.0;
            noiseLpZB_       = 0.0f;
            syncPhaseB_      = 0.0;

            // Phase 8b — reset per-sine arrays for all kMaxUnison slots, with
            // per-sine random initial phase so unison voices don't all start
            // in lock-step (which would create a transient click at t=0).
            for (int u = 0; u < kMaxUnison; ++u)
            {
                // Hash voice pointer XOR midiNote XOR u for decorrelated phase per sine.
                const std::uint32_t h = static_cast<std::uint32_t> (reinterpret_cast<std::uintptr_t> (this))
                                        ^ static_cast<std::uint32_t> ((midiNote + 1) * 2654435761u)
                                        ^ static_cast<std::uint32_t> ((u + 1) * 0x9E3779B9u);
                const double initPhase = (double) (h & 0xFFFF) / 65535.0;  // 0..1
                uPhaseA_[(size_t) u]      = initPhase;
                uModPhaseA_[(size_t) u]   = 0.0;
                uSyncPhaseA_[(size_t) u]  = 0.0;
                uPhaseB_[(size_t) u]      = initPhase;
                uModPhaseB_[(size_t) u]   = 0.0;
                uSyncPhaseB_[(size_t) u]  = 0.0;
            }

            // Phase 8a — EROSION: randomize rate + initial phase per voice/note combination
            // Hash voice pointer XOR midiNote for decorrelated drift across voices
            const std::uint32_t hash = static_cast<std::uint32_t> (reinterpret_cast<std::uintptr_t> (this))
                                       ^ static_cast<std::uint32_t> (midiNote * 2654435761u);
            const float r1 = static_cast<float> (hash & 0xFFFF)         / 65535.0f;  // 0..1
            const float r2 = static_cast<float> ((hash >> 16) & 0xFFFF) / 65535.0f;  // 0..1
            erosionRate_         = 0.3f + r1 * 0.4f;  // 0.3..0.7 Hz
            erosionPhase_        = r2;                 // random initial phase
            currentErosionCents_ = 0.0f;

            updatePhaseIncrementFromMidi (midiNote);
            updatePhaseIncrementBFromMidi (midiNote);
            playing_         = true;
            ampEnv_.reset();
            ampEnv_.noteOn();

            // Phase 8a polish — reset steal-fade state on new note
            stealing_         = false;
            stealingFade_     = 1.0f;
            stealingFadeStep_ = 0.0f;
        }

        void stopNote (float, bool allowTailOff) override
        {
            if (allowTailOff)
            {
                ampEnv_.noteOff();
            }
            else
            {
                // Phase 8a polish — fade to zero over ~5ms instead of hard cut
                stealing_         = true;
                stealingFade_     = 1.0f;
                // 5ms at current sampleRate: total samples = 0.005 * sampleRate
                const float fadeSamples = static_cast<float>(0.005 * sampleRate_);
                stealingFadeStep_ = std::pow(0.0001f, 1.0f / std::max(1.0f, fadeSamples));
                // Don't clearCurrentNote here — let the fade complete in renderNextBlock
            }
        }

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

            // Phase 8a — Compute this block's EROSION drift (slow sine LFO, per-voice)
            {
                constexpr double pi2 = 6.2831853071795865;
                currentErosionCents_ = std::sin (static_cast<float> (pi2 * erosionPhase_))
                                       * erosionAmount_ * 15.0f;  // max ±15 cents
                erosionPhase_ += static_cast<float> (erosionRate_ * numSamples / sampleRate_);
                if (erosionPhase_ >= 1.0f) erosionPhase_ -= std::floor (erosionPhase_);
            }
            // Re-derive phaseIncrements with updated erosion drift
            updatePhaseIncrementFromMidi  (currentMidiNote_);
            updatePhaseIncrementBFromMidi (currentMidiNote_);

            // Phase 10a — pick mip level for this block's pitch (auto-tracks
            // EROSION, octave/semi/cents tuning, unison detune, and SR).
            currentMipLevelA_ = tw::Wavetable::mipLevelForPhaseIncrement (phaseIncrement_);
            currentMipLevelB_ = tw::Wavetable::mipLevelForPhaseIncrement (phaseIncrementB_);

            // Phase 8a — HORIZON: per-note tilt depending on midiNote and amount.
            // midiNote 60 = neutral; lower notes get high-shelf cut (warmer),
            // higher notes get high-shelf boost (airier).
            {
                // Phase 8a polish — boost HORIZON range so it's audible at normal MIDI notes
                const float horizonTilt = horizonAmount_ * static_cast<float>(currentMidiNote_ - 60) / 24.0f;
                const float shelfGain   = std::pow (2.0f, horizonTilt);  // ±12dB at extremes
                *horizonShelfL_.coefficients = *juce::dsp::IIR::Coefficients<float>::makeHighShelf (
                    sampleRate_, 2500.0f, 0.7071f, shelfGain);
                *horizonShelfR_.coefficients = *juce::dsp::IIR::Coefficients<float>::makeHighShelf (
                    sampleRate_, 2500.0f, 0.7071f, shelfGain);
            }

            for (int i = 0; i < numSamples; ++i)
            {
                // ── OSC A sample ─────────────────────────────────────────
                float sA = 0.0f;

                switch (engine_)
                {
                    case Engine::WT:
                    {
                        if (currentWavetable_ != nullptr)
                        {
                            double warpedPhase = phase_;
                            switch (warpMode_)
                            {
                                case 1:  // BEND
                                {
                                    const double pi2 = 2.0 * 3.14159265358979323846;
                                    warpedPhase = phase_ + (double) warpAmount_ * 0.5 * std::sin (pi2 * phase_);
                                    warpedPhase -= std::floor (warpedPhase);
                                    break;
                                }
                                case 2:  // SYNC
                                {
                                    const double syncRatio = 1.0 + (double) warpAmount_ * 4.0;
                                    syncPhase_ += phaseIncrement_ * syncRatio;
                                    if (phase_ < phaseIncrement_) syncPhase_ = 0.0;
                                    if (syncPhase_ >= 1.0) syncPhase_ -= std::floor (syncPhase_);
                                    warpedPhase = syncPhase_;
                                    break;
                                }
                                case 3:  // FORMANT
                                {
                                    warpedPhase = phase_ * (1.0 + (double) warpAmount_ * 2.0);
                                    warpedPhase -= std::floor (warpedPhase);
                                    break;
                                }
                                case 0:
                                default: break;
                            }
                            sA = currentWavetable_->lookup (currentMipLevelA_, framePos_, (float) warpedPhase);
                        }
                        else
                        {
                            sA = static_cast<float> (2.0 * phase_ - 1.0);
                            sA -= static_cast<float> (polyBlep (phase_, phaseIncrement_));
                        }
                        phase_ += phaseIncrement_;
                        if (phase_ >= 1.0) phase_ -= 1.0;
                        break;
                    }

                    case Engine::NOISE:
                    {
                        noiseState_ ^= noiseState_ << 13;
                        noiseState_ ^= noiseState_ >> 17;
                        noiseState_ ^= noiseState_ << 5;
                        const float white = static_cast<float> (static_cast<int32_t> (noiseState_))
                                          * (1.0f / 2147483648.0f);
                        const float alpha = 1.0f - 0.98f * framePos_;
                        noiseLpZ_ += alpha * (white - noiseLpZ_);
                        const float drive = 1.0f + 8.0f * warpAmount_;
                        sA = std::tanh (noiseLpZ_ * drive);
                        sA *= 1.0f + 0.5f * framePos_;
                        break;
                    }

                    case Engine::FM:
                    {
                        const double ratio  = 0.25 + std::pow (32.0, (double) framePos_) * 0.234375;
                        const double modInc = phaseIncrement_ * ratio;
                        const double depth  = (double) warpAmount_ * 6.2831853071795865;
                        const double pi2    = 6.2831853071795865;
                        const double modOut = std::sin (pi2 * modPhase_);
                        sA = static_cast<float> (std::sin (pi2 * phase_ + depth * modOut));
                        modPhase_ += modInc;
                        if (modPhase_ >= 1.0) modPhase_ -= std::floor (modPhase_);
                        phase_    += phaseIncrement_;
                        if (phase_ >= 1.0) phase_ -= 1.0;
                        break;
                    }

                    case Engine::SAMP:
                    case Engine::GRAN:
                    case Engine::SPEC:
                        sA = 0.0f; break;
                }

                // ── OSC B sample (Phase 9 — mirror of OSC A, all _B vars) ─
                float sB = 0.0f;

                switch (engineB_)
                {
                    case Engine::WT:
                    {
                        if (currentWavetableB_ != nullptr)
                        {
                            double warpedPhase = phaseB_;
                            switch (warpModeB_)
                            {
                                case 1:  // BEND
                                {
                                    const double pi2 = 2.0 * 3.14159265358979323846;
                                    warpedPhase = phaseB_ + (double) warpAmountB_ * 0.5 * std::sin (pi2 * phaseB_);
                                    warpedPhase -= std::floor (warpedPhase);
                                    break;
                                }
                                case 2:  // SYNC
                                {
                                    const double syncRatio = 1.0 + (double) warpAmountB_ * 4.0;
                                    syncPhaseB_ += phaseIncrementB_ * syncRatio;
                                    if (phaseB_ < phaseIncrementB_) syncPhaseB_ = 0.0;
                                    if (syncPhaseB_ >= 1.0) syncPhaseB_ -= std::floor (syncPhaseB_);
                                    warpedPhase = syncPhaseB_;
                                    break;
                                }
                                case 3:  // FORMANT
                                {
                                    warpedPhase = phaseB_ * (1.0 + (double) warpAmountB_ * 2.0);
                                    warpedPhase -= std::floor (warpedPhase);
                                    break;
                                }
                                case 0:
                                default: break;
                            }
                            sB = currentWavetableB_->lookup (currentMipLevelB_, framePosB_, (float) warpedPhase);
                        }
                        else
                        {
                            sB = static_cast<float> (2.0 * phaseB_ - 1.0);
                            sB -= static_cast<float> (polyBlep (phaseB_, phaseIncrementB_));
                        }
                        phaseB_ += phaseIncrementB_;
                        if (phaseB_ >= 1.0) phaseB_ -= 1.0;
                        break;
                    }

                    case Engine::NOISE:
                    {
                        noiseStateB_ ^= noiseStateB_ << 13;
                        noiseStateB_ ^= noiseStateB_ >> 17;
                        noiseStateB_ ^= noiseStateB_ << 5;
                        const float white = static_cast<float> (static_cast<int32_t> (noiseStateB_))
                                          * (1.0f / 2147483648.0f);
                        const float alpha = 1.0f - 0.98f * framePosB_;
                        noiseLpZB_ += alpha * (white - noiseLpZB_);
                        const float drive = 1.0f + 8.0f * warpAmountB_;
                        sB = std::tanh (noiseLpZB_ * drive);
                        sB *= 1.0f + 0.5f * framePosB_;
                        break;
                    }

                    case Engine::FM:
                    {
                        const double ratio  = 0.25 + std::pow (32.0, (double) framePosB_) * 0.234375;
                        const double modInc = phaseIncrementB_ * ratio;
                        const double depth  = (double) warpAmountB_ * 6.2831853071795865;
                        const double pi2    = 6.2831853071795865;
                        const double modOut = std::sin (pi2 * modPhaseB_);
                        sB = static_cast<float> (std::sin (pi2 * phaseB_ + depth * modOut));
                        modPhaseB_ += modInc;
                        if (modPhaseB_ >= 1.0) modPhaseB_ -= std::floor (modPhaseB_);
                        phaseB_    += phaseIncrementB_;
                        if (phaseB_ >= 1.0) phaseB_ -= 1.0;
                        break;
                    }

                    case Engine::SAMP:
                    case Engine::GRAN:
                    case Engine::SPEC:
                        sB = 0.0f; break;
                }

                const float env    = ampEnv_.getNextSample();
                const float velEnv = currentVelocity_ * env;

                // Sum to stereo with INDEPENDENT per-osc level + pan
                scratchL[i] = (sA * level_ * panL_ + sB * levelB_ * panLB_) * velEnv;
                scratchR[i] = (sA * level_ * panR_ + sB * levelB_ * panRB_) * velEnv;
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
                    // We still let the filter process this block's tiny tail so the filter
                    // state settles — don't return early.
                    clearCurrentNote();
                }
            }

            // Run the ladder filter in-place on the stereo scratch.
            juce::dsp::AudioBlock<float> block (scratch_);
            auto sub = block.getSubBlock (0, (size_t) numSamples);
            juce::dsp::ProcessContextReplacing<float> ctx (sub);
            filter_.process (ctx);

            // Phase 8a — Apply unison pan offset (post-filter, pre-HORIZON).
            // unisonPanOffset_ is in -1..+1; rebalance L/R amplitudes with
            // equal-power panning (sqrt(2) normalised so centre = unity).
            if (std::abs (unisonPanOffset_) > 0.001f)
            {
                const float panAngle = (unisonPanOffset_ + 1.0f) * 0.25f * juce::MathConstants<float>::pi;
                const float gainL = std::cos (panAngle) * 1.4142f;
                const float gainR = std::sin (panAngle) * 1.4142f;
                for (int i = 0; i < numSamples; ++i)
                {
                    scratchL[i] *= gainL;
                    scratchR[i] *= gainR;
                }
            }

            // Phase 8a — HORIZON tilt filter (per-channel high-shelf).
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

            // Sum filtered stereo scratch into output.
            auto* L = out.getWritePointer (0, startSample);
            auto* R = out.getNumChannels() > 1
                          ? out.getWritePointer (1, startSample) : L;
            for (int i = 0; i < numSamples; ++i)
            {
                L[i] += scratchL[i];
                R[i] += scratchR[i];
            }

            if (! ampEnv_.isActive())
            {
                playing_ = false;
                clearCurrentNote();
            }
        }

    private:
        void updatePhaseIncrementFromMidi (int midiNote) noexcept
        {
            const double semitones =
                  static_cast<double> (midiNote - 69)
                + static_cast<double> (octOffset_) * 12.0
                + static_cast<double> (semiOffset_)
                + static_cast<double> (centsOffset_)       * 0.01
                + static_cast<double> (unisonDetuneCents_) * 0.01   // Phase 8a
                + static_cast<double> (currentErosionCents_) * 0.01; // Phase 8a
            const double hz = 440.0 * std::pow (2.0, semitones / 12.0);
            phaseIncrement_ = hz / sampleRate_;
        }

        // Phase 9 — OSC B phase increment helper
        void updatePhaseIncrementBFromMidi (int midiNote) noexcept
        {
            const double semitones =
                  static_cast<double> (midiNote - 69)
                + static_cast<double> (octOffsetB_) * 12.0
                + static_cast<double> (semiOffsetB_)
                + static_cast<double> (centsOffsetB_)      * 0.01
                + static_cast<double> (unisonDetuneCents_) * 0.01   // Phase 8a
                + static_cast<double> (currentErosionCents_) * 0.01; // Phase 8a
            const double hz = 440.0 * std::pow (2.0, semitones / 12.0);
            phaseIncrementB_ = hz / sampleRate_;
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
        double phase_           = 0.0;
        double phaseIncrement_  = 0.0;
        int    currentMidiNote_ = 60;
        float  currentVelocity_ = 1.0f;
        bool   playing_         = false;

        juce::ADSR             ampEnv_;
        juce::ADSR::Parameters ampParams_ { 0.005f, 0.1f, 0.7f, 0.2f };

        juce::dsp::LadderFilter<float> filter_;
        juce::AudioBuffer<float>       scratch_;
        float                          level_ = 0.7f;
        float                          panL_  = 0.7071f;  // cos(pi/4)
        float                          panR_  = 0.7071f;  // sin(pi/4)

        int   octOffset_   = 0;
        int   semiOffset_  = 0;
        float centsOffset_ = 0.0f;

        const tw::Wavetable* currentWavetable_ = nullptr;
        float                framePos_         = 0.0f;

        // Phase 2C — Warp state.
        int                  warpMode_         = 0;     // 0=NONE,1=BEND,2=SYNC,3=FORMANT
        float                warpAmount_       = 0.0f;  // 0..1
        double               syncPhase_        = 0.0;   // virtual slave-oscillator phase for SYNC mode

        // Phase 3 — Engine choice.
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

        // Phase 3 — FM engine state.
        // Modulator phase (carrier reuses phase_ from the WT path; both run
        // at multiples of phaseIncrement_ each sample). modRatio_ resolved
        // from FRAME in the render loop; modDepth_ resolved from WARP AMT.
        double modPhase_ = 0.0;

        // ── Phase 9 — OSC B state (mirror of OSC A, B suffix on each) ────
        double phaseB_          = 0.0;
        double phaseIncrementB_ = 0.0;
        float  levelB_          = 0.5f;      // default lower than A so they sum tastefully
        float  panLB_           = 0.7071f;   // cos(pi/4) — center
        float  panRB_           = 0.7071f;   // sin(pi/4) — center
        int    octOffsetB_      = 0;
        int    semiOffsetB_     = 0;
        float  centsOffsetB_    = 0.0f;
        const tw::Wavetable* currentWavetableB_ = nullptr;
        float  framePosB_       = 0.0f;
        int    warpModeB_       = 0;
        int currentMipLevelA_ = 0;   // Phase 10a — refreshed per block from phaseIncrement_
        int currentMipLevelB_ = 0;   // Phase 10a — refreshed per block from phaseIncrementB_
        float  warpAmountB_     = 0.0f;
        double syncPhaseB_      = 0.0;
        Engine engineB_         = Engine::WT;
        // xorshift32 PRNG for OSC B — seeded with sqrt(2) fractional constant
        // (0x6A09E667) XOR'd with this pointer so OSC B has a decorrelated noise
        // stream from OSC A (which uses the golden-ratio constant 0x9E3779B9).
        std::uint32_t noiseStateB_ = 0x6A09E667u
                                   ^ static_cast<std::uint32_t> (
                                         reinterpret_cast<std::uintptr_t> (this));
        float  noiseLpZB_       = 0.0f;
        double modPhaseB_       = 0.0;

        // ── Phase 8a — Unison offset (set per-voice by UnisonSynth on noteOn) ─
        float unisonDetuneCents_ = 0.0f;  // additional cents for OSC A + OSC B pitch
        float unisonPanOffset_   = 0.0f;  // -1..+1 applied after filter, pre-HORIZON

        // ── Phase 8b — Unison-in-voice state (per-sine arrays) ──────────
        // Each unison sub-voice u in [0, activeUnison_) has its own pitch state.
        // The single voice now renders all UNISON sines internally.
        std::array<double, kMaxUnison> uPhaseA_       {};
        std::array<double, kMaxUnison> uPhaseIncA_    {};
        std::array<double, kMaxUnison> uModPhaseA_    {};
        std::array<double, kMaxUnison> uSyncPhaseA_   {};

        std::array<double, kMaxUnison> uPhaseB_       {};
        std::array<double, kMaxUnison> uPhaseIncB_    {};
        std::array<double, kMaxUnison> uModPhaseB_    {};
        std::array<double, kMaxUnison> uSyncPhaseB_   {};

        // Per-sine unison config (computed at setUnison / startNote).
        std::array<float,  kMaxUnison> uDetuneCents_  {};
        std::array<float,  kMaxUnison> uPanL_         {};
        std::array<float,  kMaxUnison> uPanR_         {};

        int   activeUnison_     = 1;       // 1..kMaxUnison
        float unisonSpread01_   = 0.0f;    // 0..1

        // Phase 8a — EROSION state (per-voice slow LFO wobbles pitch ±2 cents max)
        float erosionAmount_       = 0.0f;  // 0..1 from SYN_EROSION/100
        float erosionRate_         = 0.5f;  // sub-1Hz, randomized per voice in startNote
        float erosionPhase_        = 0.0f;  // 0..1, advances per block
        float currentErosionCents_ = 0.0f;  // cached this-block drift value

        // Phase 8a — HORIZON tilt filter (per-voice high-shelf, gain depends on midiNote * horizon)
        float horizonAmount_   = 0.0f;  // -1..+1 from SYN_HORIZON/100
        juce::dsp::IIR::Filter<float> horizonShelfL_;
        juce::dsp::IIR::Filter<float> horizonShelfR_;

        // Phase 8a polish — exponential fade on voice steal (~5ms) to avoid clicks
        float stealingFade_     = 1.0f;     // 1.0 = no fade, 0.0 = silent
        float stealingFadeStep_ = 0.0f;     // multiplier per sample during fade
        bool  stealing_         = false;
    };
}
