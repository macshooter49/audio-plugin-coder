// SamplerVoice.h
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include "SampleBuffer.h"
#include <atomic>
#include <cmath>

namespace tw
{
    class SamplerVoice : public juce::SynthesiserVoice
    {
    public:
        SamplerVoice (SampleBuffer& sb,
                      std::atomic<int>&   rootNoteRef,
                      std::atomic<float>& attackMsRef,
                      std::atomic<float>& releaseMsRef,
                      std::atomic<int>&   loopModeRef) noexcept
            : sample (sb),
              rootNoteParam (rootNoteRef),
              attackMsParam (attackMsRef),
              releaseMsParam (releaseMsRef),
              loopModeParam (loopModeRef) {}

        bool canPlaySound (juce::SynthesiserSound*) override { return true; }

        void setCurrentPlaybackSampleRate (double sr) override
        {
            juce::SynthesiserVoice::setCurrentPlaybackSampleRate (sr);
            sampleRateForEnv = sr > 0.0 ? sr : 48000.0;
        }

        void startNote (int midiNoteNumber, float velocity,
                        juce::SynthesiserSound*, int /*pitchWheelPos*/) override
        {
            currentNote     = midiNoteNumber;
            currentVelocity = velocity;
            playhead        = 0.0;
            updatePitchRatio();

            // Compute envelope increments from current attack/release in ms.
            const float attackSec  = juce::jmax (0.0f, attackMsParam.load()  * 0.001f);
            const float releaseSec = juce::jmax (0.001f, releaseMsParam.load() * 0.001f);
            attackInc  = attackSec  > 0.0f ? (1.0f / (attackSec  * (float) sampleRateForEnv)) : 1.0f;
            releaseDec = 1.0f / (releaseSec * (float) sampleRateForEnv);

            envLevel = 0.0f;
            envStage = (attackInc >= 1.0f) ? EnvStage::Sustaining : EnvStage::Attack;
            if (envStage == EnvStage::Sustaining) envLevel = 1.0f;
            isActive = true;
        }

        void stopNote (float, bool allowTailOff) override
        {
            if (! allowTailOff)
            {
                envStage = EnvStage::Off;
                envLevel = 0.0f;
                clearCurrentNote();
                isActive = false;
                playhead = 0.0;
                return;
            }
            envStage = EnvStage::Release;
        }

        void pitchWheelMoved (int newPitchWheelValue) override
        {
            // 0..16383 → -1..+1 → ±2 semitones
            const double normalized = (newPitchWheelValue - 8192) / 8192.0;
            pitchBendSemis = normalized * 2.0;
            if (isActive) updatePitchRatio();
        }

        void controllerMoved (int, int) override {}

        void renderNextBlock (juce::AudioBuffer<float>& outputBuffer,
                              int startSample, int numSamples) override
        {
            if (! isActive || envStage == EnvStage::Off) return;

            auto buf = sample.load();
            if (! buf || buf->getNumSamples() == 0) return;

            const int    bufLen   = buf->getNumSamples();
            const int    bufChans = buf->getNumChannels();
            const double pitchInc = pitchRatio;

            auto* outL = outputBuffer.getWritePointer (0, startSample);
            auto* outR = outputBuffer.getNumChannels() > 1
                         ? outputBuffer.getWritePointer (1, startSample) : outL;

            const auto* inL = buf->getReadPointer (0);
            const auto* inR = bufChans > 1 ? buf->getReadPointer (1) : inL;

            const int loopMode = loopModeParam.load();
            const double endIdx = static_cast<double> (bufLen - 1);

            for (int i = 0; i < numSamples; ++i)
            {
                if (playhead >= endIdx)
                {
                    if (loopMode == 1)
                    {
                        // Forward loop: wrap to start, preserve fractional offset
                        // so pitch ratio stays consistent across the seam.
                        playhead = std::fmod (playhead, endIdx);
                        if (playhead < 0.0) playhead += endIdx;
                    }
                    else
                    {
                        // One-shot: voice goes silent regardless of env stage.
                        envStage = EnvStage::Off;
                        envLevel = 0.0f;
                        clearCurrentNote();
                        isActive = false;
                        return;
                    }
                }

                // Tick the envelope.
                switch (envStage)
                {
                    case EnvStage::Attack:
                        envLevel += attackInc;
                        if (envLevel >= 1.0f) { envLevel = 1.0f; envStage = EnvStage::Sustaining; }
                        break;
                    case EnvStage::Sustaining:
                        envLevel = 1.0f;
                        break;
                    case EnvStage::Release:
                        envLevel -= releaseDec;
                        if (envLevel <= 0.0f)
                        {
                            envLevel = 0.0f;
                            envStage = EnvStage::Off;
                            clearCurrentNote();
                            isActive = false;
                            return;
                        }
                        break;
                    case EnvStage::Off:
                        return;
                }

                const auto i0 = static_cast<int> (playhead);
                const auto frac = static_cast<float> (playhead - i0);
                const auto sampleL = inL[i0] + frac * (inL[i0 + 1] - inL[i0]);
                const auto sampleR = inR[i0] + frac * (inR[i0 + 1] - inR[i0]);

                // Anti-click tail fade for one-shot mode: linear ramp to zero
                // across the last 256 samples (~5 ms at 48 kHz) so samples
                // that don't end at zero crossings don't pop on cut-off.
                // Loop mode wraps so this never triggers there.
                float tailFade = 1.0f;
                if (loopMode == 0)
                {
                    const double samplesToEnd = endIdx - playhead;
                    constexpr double kTailLen = 256.0;
                    if (samplesToEnd < kTailLen)
                        tailFade = juce::jmax (0.0f, static_cast<float> (samplesToEnd / kTailLen));
                }

                const auto gain = currentVelocity * envLevel * tailFade;
                outL[i] += sampleL * gain;
                outR[i] += sampleR * gain;

                playhead += pitchInc;
            }
        }

    private:
        enum class EnvStage { Off, Attack, Sustaining, Release };

        void updatePitchRatio()
        {
            const int rootNote = rootNoteParam.load();
            const double semitones = static_cast<double> (currentNote - rootNote) + pitchBendSemis;
            pitchRatio = std::pow (2.0, semitones / 12.0);
        }

        SampleBuffer&       sample;
        std::atomic<int>&   rootNoteParam;
        std::atomic<float>& attackMsParam;
        std::atomic<float>& releaseMsParam;
        std::atomic<int>&   loopModeParam;

        int      currentNote      = -1;
        float    currentVelocity  = 0.0f;
        double   playhead         = 0.0;
        double   pitchRatio       = 1.0;
        double   pitchBendSemis   = 0.0;
        bool     isActive         = false;

        // AR envelope (one-shot model — no Decay/Sustain knee, just Attack-then-hold-then-Release).
        EnvStage envStage         = EnvStage::Off;
        float    envLevel         = 0.0f;
        float    attackInc        = 1.0f;
        float    releaseDec       = 0.001f;
        double   sampleRateForEnv = 48000.0;
    };

    struct SamplerSound : public juce::SynthesiserSound
    {
        bool appliesToNote    (int) override { return true; }
        bool appliesToChannel (int) override { return true; }
    };
}
