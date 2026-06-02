// Wavetable_test.cpp — Phase 2A wavetable engine
#include "Wavetable.h"
#include <juce_core/juce_core.h>
#include <cmath>

class WavetableTests : public juce::UnitTest
{
public:
    WavetableTests() : juce::UnitTest ("Wavetable", "TerrainInstrument") {}

    void runTest() override
    {
        beginTest ("Single-frame sine wavetable: lookup at phase=0 ≈ 0, phase=0.25 ≈ 1");
        {
            auto wt = tw::Wavetable::makeSine();
            const float v0   = wt.lookup (0.0f, 0.0f);
            const float v90  = wt.lookup (0.0f, 0.25f);
            const float v180 = wt.lookup (0.0f, 0.5f);
            const float v270 = wt.lookup (0.0f, 0.75f);
            expect (std::abs (v0)   < 0.05f, juce::String("v0=")   + juce::String(v0));
            expect (std::abs (v90 - 1.0f) < 0.05f, juce::String("v90=")  + juce::String(v90));
            expect (std::abs (v180) < 0.05f, juce::String("v180=") + juce::String(v180));
            expect (std::abs (v270 - (-1.0f)) < 0.05f, juce::String("v270=") + juce::String(v270));
        }

        beginTest ("Wavetable has correct dimensions");
        {
            auto wt = tw::Wavetable::makeSine();
            expectEquals (wt.getNumFrames(), 1, "sine table is single-frame");
            expect (wt.getFrameSize() >= 256, "sine table should have ≥256 samples per frame");
        }

        beginTest ("Prophet Saw has 16 frames + non-zero RMS in every frame");
        {
            auto wt = tw::Wavetable::makeProphetSaw();
            expectEquals (wt.getNumFrames(), 16);
            for (int f = 0; f < 16; ++f)
            {
                float sumSq = 0.0f;
                for (int i = 0; i < wt.getFrameSize(); ++i)
                {
                    const float s = wt.lookup ((float) f / 15.0f, (float) i / (float) wt.getFrameSize());
                    sumSq += s * s;
                }
                const float rms = std::sqrt (sumSq / (float) wt.getFrameSize());
                expect (rms > 0.1f, juce::String ("Prophet Saw frame ") + juce::String (f) + " RMS=" + juce::String (rms));
            }
        }

        beginTest ("All 6 analog wavetable factories construct + produce audio");
        {
            tw::Wavetable tables[] = {
                tw::Wavetable::makeProphetSaw(),
                tw::Wavetable::makeJupiterPWM(),
                tw::Wavetable::makeMoogSqr(),
                tw::Wavetable::makeOBXSaw(),
                tw::Wavetable::makeCS80Brass(),
                tw::Wavetable::makeJunoStr(),
            };
            for (auto& wt : tables)
            {
                expectEquals (wt.getNumFrames(), 16);
                float sumSq = 0.0f;
                for (int i = 0; i < wt.getFrameSize(); ++i)
                {
                    const float s = wt.lookup (0.5f, (float) i / (float) wt.getFrameSize());
                    sumSq += s * s;
                }
                const float rms = std::sqrt (sumSq / (float) wt.getFrameSize());
                expect (rms > 0.1f, juce::String ("RMS=") + juce::String (rms));
            }
        }
    }
};

static WavetableTests sWavetableTests;
