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
         *  block-size for the per-block AudioBlock view. */
        void prepareToPlay (double sr, int samplesPerBlock, int numChannels) noexcept
        {
            setCurrentPlaybackSampleRate (sr);
            juce::dsp::ProcessSpec spec;
            spec.sampleRate       = sr;
            spec.maximumBlockSize = (juce::uint32) samplesPerBlock;
            spec.numChannels      = (juce::uint32) juce::jmax (1, numChannels);
            filter_.prepare (spec);
            filter_.setMode (juce::dsp::LadderFilterMode::LPF24);
            filter_.reset();
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

        void startNote (int midiNote, float velocity,
                        juce::SynthesiserSound*, int /*pitchWheelPos*/) override
        {
            currentMidiNote_ = midiNote;
            currentVelocity_ = velocity;
            phase_           = 0.0;
            updatePhaseIncrementFromMidi (midiNote);
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

            // Render mono into the scratch buffer (resized lazily).
            if (scratch_.getNumSamples() < numSamples)
                scratch_.setSize (1, numSamples, false, true, true);
            scratch_.clear();
            auto* mono = scratch_.getWritePointer (0);

            for (int i = 0; i < numSamples; ++i)
            {
                float s = 0.0f;

                switch (engine_)
                {
                    case Engine::WT:
                    {
                        // Existing Phase 2A+2C wavetable+warp path — verbatim
                        // from the prior implementation.
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
                            s = currentWavetable_->lookup (framePos_, (float) warpedPhase);
                        }
                        else
                        {
                            // PolyBLEP fallback (never expected in practice).
                            s = static_cast<float> (2.0 * phase_ - 1.0);
                            s -= static_cast<float> (polyBlep (phase_, phaseIncrement_));
                        }
                        phase_ += phaseIncrement_;
                        if (phase_ >= 1.0) phase_ -= 1.0;
                        break;
                    }

                    case Engine::NOISE:
                    {
                        // xorshift32 step
                        noiseState_ ^= noiseState_ << 13;
                        noiseState_ ^= noiseState_ >> 17;
                        noiseState_ ^= noiseState_ << 5;
                        // Map to -1..+1
                        const float white = static_cast<float> (static_cast<int32_t> (noiseState_))
                                          * (1.0f / 2147483648.0f);

                        // One-pole low-pass for "color" — FRAME=0 (alpha~1) lets
                        // most of the white through (bright); FRAME=1 (alpha~0.02)
                        // heavily smooths (dark/brown-ish). Equivalent to a -6 dB
                        // RC LP at f_c = sampleRate * alpha / (2π·(1-alpha)).
                        const float alpha = 1.0f - 0.98f * framePos_;  // 1.0 → 0.02
                        noiseLpZ_ += alpha * (white - noiseLpZ_);

                        // Tanh saturation driven by WARP AMT (0 = clean, 1 = squashed).
                        const float drive = 1.0f + 8.0f * warpAmount_;
                        s = std::tanh (noiseLpZ_ * drive);
                        // Pre-emphasize a bit so heavy LP-then-drive doesn't kill level.
                        s *= 1.0f + 0.5f * framePos_;
                        // Phase_ accumulator NOT advanced — NOISE is pitchless.
                        break;
                    }
                    case Engine::FM:
                        s = 0.0f;  // implemented in Task 8
                        break;

                    case Engine::SAMP:
                    case Engine::GRAN:
                    case Engine::SPEC:
                        // Phase 3 stubs — silent renders. Real DSP added in later
                        // phases (SAMP reuses Terrain's existing SamplerVoice
                        // infrastructure, GRAN reuses GrainEngine.h, SPEC needs
                        // FFT pipeline). User can switch to them without crashes
                        // but hears nothing — labelled "(coming soon)" in the
                        // ENGINE dropdown in Task 9.
                        s = 0.0f;
                        break;
                }

                const float env = ampEnv_.getNextSample();
                mono[i] = s * currentVelocity_ * env;
            }

            // Run the ladder filter in-place on the mono scratch.
            juce::dsp::AudioBlock<float> block (scratch_);
            auto sub = block.getSubBlock (0, (size_t) numSamples);
            juce::dsp::ProcessContextReplacing<float> ctx (sub);
            filter_.process (ctx);

            // Pan-spread into the output's L + R, with level applied.
            auto* L = out.getWritePointer (0, startSample);
            auto* R = out.getNumChannels() > 1
                          ? out.getWritePointer (1, startSample) : L;
            const float gL = level_ * panL_;
            const float gR = level_ * panR_;
            for (int i = 0; i < numSamples; ++i)
            {
                L[i] += mono[i] * gL;
                R[i] += mono[i] * gR;
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
                + static_cast<double> (centsOffset_) * 0.01;
            const double hz = 440.0 * std::pow (2.0, semitones / 12.0);
            phaseIncrement_ = hz / sampleRate_;
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
    };
}
