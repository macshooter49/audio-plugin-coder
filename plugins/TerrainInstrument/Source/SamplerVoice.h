// SamplerVoice.h
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_basics/juce_audio_basics.h>

namespace tw // terrain-waves
{
    /**
     * SamplerVoice — one polyphonic voice for Terrain Instrument.
     *
     * v0a stub: accepts note-on/off, allocates space, produces silence.
     * Real audio playback wired up in Task 6.
     *
     * Intentionally header-only to match the project convention.
     */
    class SamplerVoice : public juce::SynthesiserVoice
    {
    public:
        SamplerVoice() = default;
        ~SamplerVoice() override = default;

        bool canPlaySound (juce::SynthesiserSound* /*sound*/) override { return true; }

        void startNote (int midiNoteNumber, float velocity,
                        juce::SynthesiserSound* /*sound*/, int /*currentPitchWheelPosition*/) override
        {
            currentNote = midiNoteNumber;
            currentVelocity = velocity;
            isActive = true;
            // TODO Task 6: read shared sample buffer + start playhead at 0
        }

        void stopNote (float /*velocity*/, bool allowTailOff) override
        {
            if (! allowTailOff)
            {
                clearCurrentNote();
                isActive = false;
            }
            // TODO Task 8: trigger AR release stage
        }

        void pitchWheelMoved (int /*newPitchWheelValue*/) override {}
        void controllerMoved (int /*controllerNumber*/, int /*newControllerValue*/) override {}

        void renderNextBlock (juce::AudioBuffer<float>& outputBuffer,
                              int startSample, int numSamples) override
        {
            // v0a stub: silence. Real DSP wired in Task 6.
            juce::ignoreUnused (outputBuffer, startSample, numSamples);
        }

    private:
        int   currentNote     = -1;
        float currentVelocity = 0.0f;
        bool  isActive        = false;
    };

    /**
     * SamplerSound — placeholder. juce::Synthesiser requires at least one Sound
     * registered before any voice will play. We register a single always-active
     * sound; per-voice gating happens in canPlaySound (which always returns true).
     */
    struct SamplerSound : public juce::SynthesiserSound
    {
        bool appliesToNote    (int /*midiNoteNumber*/) override { return true; }
        bool appliesToChannel (int /*midiChannel*/)    override { return true; }
    };
}
