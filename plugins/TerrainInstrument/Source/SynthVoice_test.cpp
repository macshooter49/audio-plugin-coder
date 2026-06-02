// SynthVoice_test.cpp — Phase 1 MPV unit tests
#include "SynthVoice.h"
#include <juce_core/juce_core.h>

class SynthSoundTests : public juce::UnitTest
{
public:
    SynthSoundTests() : juce::UnitTest ("SynthSound", "TerrainInstrument") {}

    void runTest() override
    {
        beginTest ("SynthSound accepts all notes and channels");
        {
            tw::SynthSound s;
            expect (s.appliesToNote (0));
            expect (s.appliesToNote (60));
            expect (s.appliesToNote (127));
            expect (s.appliesToChannel (1));
            expect (s.appliesToChannel (16));
        }
    }
};

class SynthVoiceTests : public juce::UnitTest
{
public:
    SynthVoiceTests() : juce::UnitTest ("SynthVoice", "TerrainInstrument") {}

    void runTest() override
    {
        beginTest ("SynthVoice accepts SynthSound");
        {
            tw::SynthVoice v;
            tw::SynthSound s;
            expect (v.canPlaySound (&s), "SynthVoice should accept SynthSound");
            expect (! v.canPlaySound (nullptr), "should reject nullptr sound");
        }

        beginTest ("PolyBLEP saw produces non-zero output at 440 Hz");
        {
            tw::SynthVoice v;
            v.setCurrentPlaybackSampleRate (48000.0);

            // Trigger A4 = MIDI 69 (~440 Hz). We bypass the Synthesiser
            // dispatch and call startNote/renderNextBlock directly.
            tw::SynthSound sentinel;
            v.startNote (69, 1.0f, &sentinel, 8192);

            juce::AudioBuffer<float> buf (2, 512);
            buf.clear();
            v.renderNextBlock (buf, 0, 512);

            // RMS must be non-zero — oscillator is running.
            const float rmsL = buf.getRMSLevel (0, 0, 512);
            expect (rmsL > 0.01f, "saw should produce audible RMS");

            // Count zero crossings to confirm fundamental near 440 Hz.
            // 440 Hz at 48 kHz = 1 cycle / ~109 samples = 2 crossings / cycle
            // → 512 samples ≈ 4.7 cycles → ~9 crossings expected.
            const auto* L = buf.getReadPointer (0);
            int zc = 0;
            for (int i = 1; i < 512; ++i)
                if ((L[i] >= 0.0f) != (L[i - 1] >= 0.0f)) ++zc;
            expect (zc >= 7 && zc <= 12, juce::String ("zero crossings ") + juce::String (zc));
        }
    }
};

static SynthSoundTests sSynthSoundTests;
static SynthVoiceTests sSynthVoiceTests;
