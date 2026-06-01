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

static SynthSoundTests sSynthSoundTests;
