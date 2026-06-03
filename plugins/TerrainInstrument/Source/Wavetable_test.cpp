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

        beginTest ("All 14 Phase 2B wavetable factories construct + produce audio");
        {
            tw::Wavetable tables[] = {
                tw::Wavetable::makePPGWave(),
                tw::Wavetable::makeDX7EP(),
                tw::Wavetable::makeD50Bell(),
                tw::Wavetable::makeM1Piano(),
                tw::Wavetable::makeChoirAtoO(),
                tw::Wavetable::makeWhisper(),
                tw::Wavetable::makeVowelMorph(),
                tw::Wavetable::makeBowedMetal(),
                tw::Wavetable::makeGlassHarmonics(),
                tw::Wavetable::makeRailroad(),
                tw::Wavetable::makeDustbowl(),
                tw::Wavetable::makeStaticEvolve(),
                tw::Wavetable::makeSpectralDrift(),
                tw::Wavetable::makeSerumHD(),
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
                expect (rms > 0.05f, juce::String ("Phase 2B table RMS=") + juce::String (rms));
            }
        }

        beginTest ("FrameSpec + WavetableSpec defaults are zero-filled and empty");
        {
            tw::FrameSpec fs;
            expectEquals (fs.numHarmonics, 0);
            for (int h = 0; h < tw::FrameSpec::kMaxHarmonics; ++h)
            {
                expect (fs.amplitudes[(size_t) h] == 0.0f, juce::String ("amplitudes[") + juce::String (h) + "] not zero");
                expect (fs.phases[(size_t) h]     == 0.0f, juce::String ("phases[") + juce::String (h) + "] not zero");
            }
            tw::WavetableSpec spec;
            for (int f = 0; f < tw::WavetableSpec::kNumFrames; ++f)
                expectEquals (spec.frames[(size_t) f].numHarmonics, 0);
        }
    }
};

static WavetableTests sWavetableTests;
