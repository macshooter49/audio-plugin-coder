// SynthVoice.h — Terrain Instrument synth section, Phase 1 (MPV)
// One PolyBLEP saw oscillator → juce::dsp::LadderFilter (LPF24) → juce::ADSR.
// Mirrors the SamplerVoice.h pattern (header-only). 8 voices allocated by
// PluginProcessor against a single SynthSound sentinel.
#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include <atomic>
#include <cmath>

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

        void startNote (int midiNote, float velocity,
                        juce::SynthesiserSound*, int /*pitchWheelPos*/) override
        {
            currentMidiNote_ = midiNote;
            currentVelocity_ = velocity;
            phase_           = 0.0;
            const double hz  = 440.0 * std::pow (2.0, (midiNote - 69) / 12.0);
            phaseIncrement_  = hz / sampleRate_;
            playing_         = true;
            ampEnv_.reset();
            ampEnv_.noteOn();
        }

        void stopNote (float, bool allowTailOff) override
        {
            if (allowTailOff)
            {
                ampEnv_.noteOff();
            }
            else
            {
                playing_ = false;
                ampEnv_.reset();
                clearCurrentNote();
            }
        }

        void pitchWheelMoved (int) override {}
        void controllerMoved (int, int) override {}

        void renderNextBlock (juce::AudioBuffer<float>& out,
                              int startSample, int numSamples) override
        {
            if (! playing_) return;
            auto* L = out.getWritePointer (0, startSample);
            auto* R = out.getNumChannels() > 1
                          ? out.getWritePointer (1, startSample) : L;

            for (int i = 0; i < numSamples; ++i)
            {
                // Naive saw in [-1, +1] driven by phase_ in [0, 1).
                float s = static_cast<float> (2.0 * phase_ - 1.0);
                s -= static_cast<float> (polyBlep (phase_, phaseIncrement_));

                const float env = ampEnv_.getNextSample();
                const float g   = currentVelocity_ * env;
                L[i] += s * g;
                R[i] += s * g;

                phase_ += phaseIncrement_;
                if (phase_ >= 1.0) phase_ -= 1.0;
            }

            // Auto-end when the envelope has fully released.
            if (! ampEnv_.isActive())
            {
                playing_ = false;
                clearCurrentNote();
            }
        }

    private:
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
    };
}
