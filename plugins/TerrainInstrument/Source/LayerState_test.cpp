// LayerState_test.cpp
// JUCE UnitTest suite for the LayerState struct (Mark 2 Phase 1, Task 1).
// Tests run automatically when the plugin or standalone loads in debug/test mode.
#include "LayerState.h"
#include <juce_core/juce_core.h>

class LayerStateTests : public juce::UnitTest
{
public:
    LayerStateTests() : juce::UnitTest ("LayerState", "TerrainInstrument") {}

    void runTest() override
    {
        beginTest ("Default construction — empty sample, default modes");
        {
            tw::LayerState layer;
            expect (! layer.hasSample(), "hasSample() should be false on fresh layer");
            expectEquals (layer.rootMidiNote.load(), 60, "default ROOT is C4 (MIDI 60)");
            expectEquals (layer.sliceMode.load(),    0,  "default sliceMode is 0");
            expectEquals (layer.playMode.load(),     0,  "default playMode is 0 (1-SHOT)");
            expectEquals (layer.sliceCount.load(),   4,  "default sliceCount is 4");
            expectWithinAbsoluteError (layer.chopFadeMs.load(), 5.0f, 0.001f);
        }

        beginTest ("Default mixer state — full volume, no mute, no solo");
        {
            tw::LayerState layer;
            expectWithinAbsoluteError (layer.volume.load(), 1.0f, 0.001f);
            expect (! layer.mute.load(), "mute defaults to false");
            expect (! layer.solo.load(), "solo defaults to false");
        }

        beginTest ("layerIndex stays at default until assigned");
        {
            tw::LayerState layer;
            expectEquals (layer.layerIndex, 0, "layerIndex default is 0");
            layer.layerIndex = 2;
            expectEquals (layer.layerIndex, 2, "layerIndex assignable");
        }

        beginTest ("Synth has expected voice count after construction");
        {
            tw::LayerState layer;
            expectEquals (layer.synth.getNumVoices(), 32, "32 voices per layer (matches Mark 1.5)");
        }
    }
};

static LayerStateTests sLayerStateTests;
