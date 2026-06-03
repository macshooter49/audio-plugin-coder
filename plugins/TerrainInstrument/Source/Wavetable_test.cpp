// Wavetable_test.cpp — Phase 2A wavetable engine
#include "Wavetable.h"
#include "WavetableBank.h"
#include <juce_core/juce_core.h>
#include <cmath>

class WavetableTests : public juce::UnitTest
{
public:
    WavetableTests() : juce::UnitTest ("Wavetable", "TerrainInstrument") {}

    void runTest() override
    {
        beginTest ("Spec-built sine wavetable: lookup at phase=0 ≈ 0, phase=0.25 ≈ 1");
        {
            tw::Wavetable wt;
            wt.buildFromSpec (tw::Wavetable::makeSineSpec());
            const float v0   = wt.lookup (0, 0.0f, 0.0f);
            const float v90  = wt.lookup (0, 0.0f, 0.25f);
            const float v180 = wt.lookup (0, 0.0f, 0.5f);
            const float v270 = wt.lookup (0, 0.0f, 0.75f);
            expect (std::abs (v0)   < 0.05f, juce::String("v0=")   + juce::String(v0));
            expect (std::abs (v90 - 1.0f) < 0.05f, juce::String("v90=")  + juce::String(v90));
            expect (std::abs (v180) < 0.05f, juce::String("v180=") + juce::String(v180));
            expect (std::abs (v270 - (-1.0f)) < 0.05f, juce::String("v270=") + juce::String(v270));
        }

        beginTest ("Spec-built wavetable has correct dimensions");
        {
            tw::Wavetable wt;
            wt.buildFromSpec (tw::Wavetable::makeSineSpec());
            expectEquals (wt.getNumFrames(),    tw::WavetableSpec::kNumFrames, "spec wavetable has 16 frames");
            expectEquals (wt.getFrameSize(),    tw::Wavetable::kFrameSize,     "spec wavetable has 2048 samples per frame");
            expectEquals (wt.getNumMipLevels(), tw::Wavetable::kNumMipLevels,  "spec wavetable has 8 mip levels");
        }

        beginTest ("Spec-built Prophet Saw has 16 frames + non-zero RMS at mip 0 and mip 4");
        {
            tw::Wavetable wt;
            wt.buildFromSpec (tw::Wavetable::makeProphetSawSpec());
            expectEquals (wt.getNumFrames(), 16);
            for (int lvl : { 0, 4 })
            {
                for (int f = 0; f < 16; ++f)
                {
                    float sumSq = 0.0f;
                    for (int i = 0; i < wt.getFrameSize(); ++i)
                    {
                        const float s = wt.lookup (lvl, (float) f / 15.0f, (float) i / (float) wt.getFrameSize());
                        sumSq += s * s;
                    }
                    const float rms = std::sqrt (sumSq / (float) wt.getFrameSize());
                    expect (rms > 0.05f, juce::String ("Prophet Saw lvl=") + juce::String (lvl) + " frame=" + juce::String (f) + " RMS=" + juce::String (rms));
                }
            }
        }

        beginTest ("All 6 analog wavetables (3 spec + 3 legacy) construct + produce audio");
        {
            // 3 migrated → spec-based
            tw::Wavetable specTables[3];
            specTables[0].buildFromSpec (tw::Wavetable::makeProphetSawSpec());
            specTables[1].buildFromSpec (tw::Wavetable::makeOBXSawSpec());
            specTables[2].buildFromSpec (tw::Wavetable::makeJunoStrSpec());
            // 3 legacy (Phase 10c migrates these)
            tw::Wavetable legacyTables[] = {
                tw::Wavetable::makeJupiterPWM(),
                tw::Wavetable::makeMoogSqr(),
                tw::Wavetable::makeCS80Brass()
            };
            const auto testRms = [this](const tw::Wavetable& wt, const char* label) {
                expectEquals (wt.getNumFrames(), 16, juce::String (label) + " not 16 frames");
                float sumSq = 0.0f;
                for (int i = 0; i < wt.getFrameSize(); ++i)
                {
                    const float s = wt.lookup (0, 0.5f, (float) i / (float) wt.getFrameSize());
                    sumSq += s * s;
                }
                const float rms = std::sqrt (sumSq / (float) wt.getFrameSize());
                expect (rms > 0.05f, juce::String (label) + " RMS=" + juce::String (rms));
            };
            testRms (specTables[0], "ProphetSawSpec");
            testRms (specTables[1], "OBXSawSpec");
            testRms (specTables[2], "JunoStrSpec");
            testRms (legacyTables[0], "JupiterPWM (legacy)");
            testRms (legacyTables[1], "MoogSqr (legacy)");
            testRms (legacyTables[2], "CS80Brass (legacy)");
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
            // Legacy makeJupiterPWM() goes through the OLD constructor → numMipLevels_ = 1.
            auto wt = tw::Wavetable::makeJupiterPWM();
            expectEquals (wt.getNumMipLevels(), 1, "legacy table should be single-tier");
            const float v_lvl0 = wt.lookup (0, 0.5f, 0.25f);
            const float v_lvl7 = wt.lookup (7, 0.5f, 0.25f);  // out-of-range, must clamp
            expectWithinAbsoluteError (v_lvl7, v_lvl0, 1.0e-6f);
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

        beginTest ("makeSquareSpec — odd harmonics only, 256-harmonic ceiling");
        {
            const auto spec = tw::Wavetable::makeSquareSpec();
            for (int f = 0; f < 16; ++f)
            {
                const auto& fs = spec.frames[(size_t) f];
                expect (fs.numHarmonics > 100, "square should have many harmonics");
                // Even harmonics must be zero.
                for (int h = 2; h <= fs.numHarmonics; h += 2)
                    expect (fs.amplitudes[(size_t)(h - 1)] == 0.0f,
                            juce::String ("frame=") + juce::String (f) + " harmonic " + juce::String (h) + " non-zero");
                // Odd harmonics must follow 1/h pattern roughly.
                const float a1 = fs.amplitudes[0];
                const float a3 = fs.amplitudes[2];
                expect (std::abs (a3 - a1 / 3.0f) < 1.0e-4f, "a3 != a1/3");
            }
        }

        beginTest ("makeSineSpec / makeTriangleSpec / makeSquareSpec / makePulseSpec — all frames are non-empty and identical (basic shapes)");
        {
            const auto sine     = tw::Wavetable::makeSineSpec();
            const auto triangle = tw::Wavetable::makeTriangleSpec();
            const auto square   = tw::Wavetable::makeSquareSpec();
            const auto pulse    = tw::Wavetable::makePulseSpec();
            for (const auto* sp : { &sine, &triangle, &square, &pulse })
            {
                expect (sp->frames[0].numHarmonics > 0);
                // All 16 frames identical → basic shape.
                for (int f = 1; f < 16; ++f)
                {
                    expectEquals (sp->frames[(size_t) f].numHarmonics, sp->frames[0].numHarmonics);
                    for (int h = 0; h < tw::FrameSpec::kMaxHarmonics; ++h)
                        expectWithinAbsoluteError (sp->frames[(size_t) f].amplitudes[(size_t) h],
                                                   sp->frames[0].amplitudes[(size_t) h], 1.0e-9f);
                }
            }
        }

        beginTest ("Square wavetable from spec → built mip 0 looks square-ish (peak ≈ ±1)");
        {
            tw::Wavetable wt;
            wt.buildFromSpec (tw::Wavetable::makeSquareSpec());
            // After normalize, peak should be ~1
            float peak = 0.0f;
            for (int i = 0; i < 2048; ++i)
                peak = std::max (peak, std::abs (wt.lookup (0, 0.0f, (float) i / 2048.0f)));
            expect (peak > 0.9f && peak < 1.05f, juce::String ("square peak=") + juce::String (peak));
        }

        beginTest ("makeProphetSawSpec — harmonic count decreases across frames");
        {
            const auto spec = tw::Wavetable::makeProphetSawSpec();
            expect (spec.frames[0].numHarmonics  >= 80, "frame 0 should be bright");
            expect (spec.frames[15].numHarmonics <= 20, "frame 15 should be warm");
            expect (spec.frames[0].numHarmonics > spec.frames[15].numHarmonics, "should decrease across frames");
            // Every harmonic should be present with ~1/h amplitude
            for (int h = 1; h <= 10; ++h)
                expect (spec.frames[0].amplitudes[(size_t)(h - 1)] > 0.0f,
                        juce::String ("frame 0 harmonic ") + juce::String (h) + " is zero");
        }

        beginTest ("makeOBXSawSpec + makeJunoStrSpec — 16 frames, saw spectrum, frame-dependent variation");
        {
            const auto obx  = tw::Wavetable::makeOBXSawSpec();
            const auto juno = tw::Wavetable::makeJunoStrSpec();
            for (const auto* sp : { &obx, &juno })
            {
                expect (sp->frames[0].numHarmonics > 30, "frame 0 should have substantial harmonics");
                // Saw-like: harmonic 1 amp > harmonic 5 amp
                expect (sp->frames[0].amplitudes[0] > sp->frames[0].amplitudes[4], "saw 1/h pattern");
                // Frame-to-frame variation must exist somewhere
                bool different = false;
                for (int h = 0; h < tw::FrameSpec::kMaxHarmonics && ! different; ++h)
                    if (std::abs (sp->frames[0].phases[(size_t) h] - sp->frames[15].phases[(size_t) h]) > 0.01f)
                        different = true;
                expect (different, "frame 0 phases must differ from frame 15 phases");
            }
        }

        beginTest ("All 3 analog-saw specs build into non-silent wavetables at mip 0 and mip 7");
        {
            tw::WavetableSpec specs[] = {
                tw::Wavetable::makeProphetSawSpec(),
                tw::Wavetable::makeOBXSawSpec(),
                tw::Wavetable::makeJunoStrSpec()
            };
            for (auto& spec : specs)
            {
                tw::Wavetable wt;
                wt.buildFromSpec (spec);
                for (int lvl : { 0, 7 })
                {
                    float sumSq = 0.0f;
                    for (int i = 0; i < 2048; ++i)
                    {
                        const float v = wt.lookup (lvl, 0.5f, (float) i / 2048.0f);
                        sumSq += v * v;
                    }
                    const float rms = std::sqrt (sumSq / 2048.0f);
                    expect (rms > 0.05f, juce::String ("lvl=") + juce::String (lvl) + " rms=" + juce::String (rms));
                }
            }
        }

        beginTest ("WavetableBank: all 24 wavetables construct + produce non-silent audio");
        {
            tw::WavetableBank bank;
            for (int p = 0; p < tw::WavetableBank::kNumPresets; ++p)
            {
                const auto* wt = bank.getTable (p);
                expect (wt != nullptr, juce::String ("preset ") + juce::String (p) + " is null");
                float sumSq = 0.0f;
                for (int i = 0; i < 2048; ++i)
                    sumSq += std::pow (wt->lookup (0, 0.5f, (float) i / 2048.0f), 2.0f);
                const float rms = std::sqrt (sumSq / 2048.0f);
                expect (rms > 0.03f, juce::String ("preset ") + juce::String (p) + " RMS=" + juce::String (rms));
            }
        }
    }
};

static WavetableTests sWavetableTests;
