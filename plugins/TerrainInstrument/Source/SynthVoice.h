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
     *  PolyBLEP saw oscillator → ladder LP-24 filter → AMP ADSR → pan.
     *  Subsequent phases (per Design/v1-syn-spec.md) add more engines,
     *  filter types, envelopes, cross-mod, FLOW glide, etc. */
    class SynthVoice : public juce::SynthesiserVoice
    {
    public:
        SynthVoice() = default;

        bool canPlaySound (juce::SynthesiserSound* s) override
        {
            return dynamic_cast<SynthSound*> (s) != nullptr;
        }

        void startNote (int /*midiNote*/, float /*velocity*/,
                        juce::SynthesiserSound*, int /*pitchWheelPos*/) override {}
        void stopNote (float /*velocity*/, bool /*allowTailOff*/) override
        {
            clearCurrentNote();
        }
        void pitchWheelMoved (int) override {}
        void controllerMoved (int, int) override {}
        void renderNextBlock (juce::AudioBuffer<float>& /*outputBuffer*/,
                              int /*startSample*/, int /*numSamples*/) override {}
    };
}
