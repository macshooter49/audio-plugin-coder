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

        beginTest ("buildFromSpec produces finite values across all 8 mip levels");
        {
            tw::WavetableSpec spec;
            // Plant a single fundamental harmonic in every frame.
            for (int f = 0; f < tw::WavetableSpec::kNumFrames; ++f)
            {
                spec.frames[(size_t) f].amplitudes[0] = 1.0f;
                spec.frames[(size_t) f].numHarmonics  = 1;
            }
            tw::Wavetable wt;
            wt.buildFromSpec (spec);
            for (int lvl = 0; lvl < tw::Wavetable::kNumMipLevels; ++lvl)
            {
                for (int f = 0; f < tw::WavetableSpec::kNumFrames; ++f)
                {
                    for (int s = 0; s < 16; ++s)  // sample a few points per frame
                    {
                        const float v = wt.lookup (lvl, (float) f / 15.0f, (float) s / 16.0f);
                        expect (std::isfinite (v), juce::String ("non-finite at lvl=") + juce::String (lvl));
                        expect (std::abs (v) < 2.0f, juce::String ("|v| >= 2 at lvl=") + juce::String (lvl));
                    }
                }
            }
        }

        beginTest ("lookup(int, framePos, phase) for legacy single-tier table clamps mipLevel to 0");
        {
            // Legacy makeSine() goes through the OLD constructor → numMipLevels_ = 1.
            auto wt = tw::Wavetable::makeSine();
            const float v_lvl0 = wt.lookup (0, 0.0f, 0.25f);
            const float v_lvl7 = wt.lookup (7, 0.0f, 0.25f);  // out-of-range, must clamp
            expectWithinAbsoluteError (v_lvl7, v_lvl0, 1.0e-6f);
            expect (std::abs (v_lvl0 - 1.0f) < 0.05f, juce::String ("expected ~1, got ") + juce::String (v_lvl0));
        }

        beginTest ("spec-built sine wavetable: lookup at phase=0.25 ~= 1.0 at all mip levels");
        {
            tw::WavetableSpec spec;
            for (int f = 0; f < 16; ++f)
            {
                spec.frames[(size_t) f].amplitudes[0] = 1.0f;
                spec.frames[(size_t) f].numHarmonics  = 1;
            }
            tw::Wavetable wt;
            wt.buildFromSpec (spec);
            for (int lvl = 0; lvl < tw::Wavetable::kNumMipLevels; ++lvl)
            {
                const float v = wt.lookup (lvl, 0.5f, 0.25f);
                expect (std::abs (v - 1.0f) < 0.05f,
                        juce::String ("lvl=") + juce::String (lvl) + " got " + juce::String (v));
            }
        }

        beginTest ("mipLevelForPhaseIncrement breakpoints at 48 kHz");
        {
            const double sr = 48000.0;
            // C2 ~ 65.4 Hz → phaseInc ~ 0.00136 → maxSafe ~ 367 → level 0 (256 OK)
            expectEquals (tw::Wavetable::mipLevelForMidiNote (36, sr), 0);
            // C3 ~ 130.8 Hz → maxSafe ~ 183 → level 1 (128 OK, 256 no)
            expectEquals (tw::Wavetable::mipLevelForMidiNote (48, sr), 1);
            // C4 ~ 261.6 Hz → maxSafe ~ 91 → level 2 (64 OK, 128 no)
            expectEquals (tw::Wavetable::mipLevelForMidiNote (60, sr), 2);
            // C5 ~ 523 Hz → maxSafe ~ 45 → level 3 (32 OK, 64 no)
            expectEquals (tw::Wavetable::mipLevelForMidiNote (72, sr), 3);
            // C6 ~ 1046 Hz → maxSafe ~ 22 → level 4 (16 OK, 32 no)
            expectEquals (tw::Wavetable::mipLevelForMidiNote (84, sr), 4);
            // C7 ~ 2093 Hz → maxSafe ~ 11 → level 5 (8 OK, 16 no)
            expectEquals (tw::Wavetable::mipLevelForMidiNote (96, sr), 5);
            // C8 ~ 4186 Hz → maxSafe ~ 5 → level 6 (4 OK, 8 no)
            expectEquals (tw::Wavetable::mipLevelForMidiNote (108, sr), 6);
            // Above C8 → level 7
            expectEquals (tw::Wavetable::mipLevelForMidiNote (120, sr), 7);
        }

        beginTest ("mipLevelForPhaseIncrement monotonic — higher phaseInc never picks lower level");
        {
            int prev = 0;
            for (int n = 0; n <= 127; ++n)
            {
                const int lvl = tw::Wavetable::mipLevelForMidiNote (n, 48000.0);
                expect (lvl >= prev, juce::String ("midi ") + juce::String (n) + " lvl=" + juce::String (lvl) + " < prev=" + juce::String (prev));
                prev = lvl;
            }
        }
    }
};

static WavetableTests sWavetableTests;
