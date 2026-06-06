// Wavetable_test.cpp — Phase 2A wavetable engine
#include "Wavetable.h"
#include "WavetableBank.h"
#include <juce_core/juce_core.h>
#include <cmath>
#include <array>

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

        beginTest ("All 6 analog wavetables (all spec-based as of Phase 11l) construct + produce audio");
        {
            // Phase 11l: all 6 analog tables now spec-based
            const auto testSpecRms = [this](const tw::WavetableSpec& spec, const char* label) {
                tw::Wavetable wt;
                wt.buildFromSpec (spec);
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
            testSpecRms (tw::Wavetable::makeProphetSawSpec(), "ProphetSawSpec");
            testSpecRms (tw::Wavetable::makeJupiterPWMSpec(), "JupiterPWMSpec");
            testSpecRms (tw::Wavetable::makeMoogSqrSpec(),    "MoogSqrSpec");
            testSpecRms (tw::Wavetable::makeOBXSawSpec(),     "OBXSawSpec");
            testSpecRms (tw::Wavetable::makeCS80BrassSpec(),  "CS80BrassSpec");
            testSpecRms (tw::Wavetable::makeJunoStrSpec(),    "JunoStrSpec");
        }

        beginTest ("All 14 Phase 11l wavetable factories construct + produce audio");
        {
            // PPGWave is now spec-based; DX7EP / D50Bell / M1Piano remain legacy
            // Vocal, Metallic, Experimental: all now spec-based
            const auto testSpecRms2 = [this](const tw::WavetableSpec& spec, const char* label) {
                tw::Wavetable wt;
                wt.buildFromSpec (spec);
                expectEquals (wt.getNumFrames(), 16, juce::String (label) + " not 16 frames");
                float sumSq = 0.0f;
                for (int i = 0; i < wt.getFrameSize(); ++i)
                    sumSq += std::pow (wt.lookup (0, 0.5f, (float) i / (float) wt.getFrameSize()), 2.0f);
                const float rms = std::sqrt (sumSq / (float) wt.getFrameSize());
                expect (rms > 0.01f, juce::String (label) + " RMS=" + juce::String (rms));
            };
            testSpecRms2  (tw::Wavetable::makePPGWaveSpec(),       "PPGWaveSpec");
            testSpecRms2  (tw::Wavetable::makeDX7EPSpec(),        "DX7EPSpec");
            testSpecRms2  (tw::Wavetable::makeD50BellSpec(),      "D50BellSpec");
            testSpecRms2  (tw::Wavetable::makeM1PianoSpec(),      "M1PianoSpec");
            testSpecRms2  (tw::Wavetable::makeChoirAtoOSpec(),     "ChoirAtoOSpec");
            testSpecRms2  (tw::Wavetable::makeWhisperSpec(),       "WhisperSpec");
            testSpecRms2  (tw::Wavetable::makeVowelMorphSpec(),    "VowelMorphSpec");
            testSpecRms2  (tw::Wavetable::makeBowedMetalSpec(),    "BowedMetalSpec");
            testSpecRms2  (tw::Wavetable::makeGlassHarmonicsSpec(),"GlassHarmonicsSpec");
            testSpecRms2  (tw::Wavetable::makeRailroadSpec(),      "RailroadSpec");
            testSpecRms2  (tw::Wavetable::makeDustbowlSpec(),      "DustbowlSpec");
            testSpecRms2  (tw::Wavetable::makeStaticEvolveSpec(),  "StaticEvolveSpec");
            testSpecRms2  (tw::Wavetable::makeSpectralDriftSpec(), "SpectralDriftSpec");
            testSpecRms2  (tw::Wavetable::makeSerumHDSpec(),       "SerumHDSpec");
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

        beginTest ("lookup(int, framePos, phase) clamps an out-of-range mipLevel");
        {
            // Batch 2: DX7EP/D50Bell/M1Piano are now spec-based + band-limited (8 mips) —
            // no legacy single-tier tables remain. Verify lookup() still clamps an
            // out-of-range mipLevel into [0, numMipLevels-1].
            tw::Wavetable wt;
            wt.buildFromSpec (tw::Wavetable::makeDX7EPSpec());
            expectEquals (wt.getNumMipLevels(), 8, "spec table should be 8-tier");
            const float vHi  = wt.lookup (99, 0.5f, 0.25f);                       // clamps to top mip
            const float vTop = wt.lookup (wt.getNumMipLevels() - 1, 0.5f, 0.25f);
            expectWithinAbsoluteError (vHi, vTop, 1.0e-6f);
            const float vLo   = wt.lookup (-5, 0.5f, 0.25f);                      // clamps to mip 0
            const float vZero = wt.lookup (0, 0.5f, 0.25f);
            expectWithinAbsoluteError (vLo, vZero, 1.0e-6f);
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

        beginTest ("Anti-aliasing: Square at C7 mip-5 contains only ≤8 harmonics");
        {
            tw::Wavetable wt;
            wt.buildFromSpec (tw::Wavetable::makeSquareSpec());
            // C7 ~ 2093 Hz at 48 kHz → phaseInc ~ 0.0436 → mip-5 (8 harmonics).
            const double phaseIncC7 = 2093.0 / 48000.0;
            const int lvl = tw::Wavetable::mipLevelForPhaseIncrement (phaseIncC7);
            expectEquals (lvl, 5);

            // Quick DFT: read one cycle and find magnitude at each harmonic bin.
            constexpr int N = 2048;
            std::array<float, N> waveform;
            for (int i = 0; i < N; ++i)
                waveform[(size_t) i] = wt.lookup (lvl, 0.5f, (float) i / (float) N);

            // Check harmonics 9, 11, 13 (odd, beyond our mip-5 cap of 8) are silent.
            constexpr double pi2 = 2.0 * 3.14159265358979323846;
            for (int h : { 9, 11, 13, 15 })
            {
                double mag = 0.0;
                for (int i = 0; i < N; ++i)
                {
                    const double phase = pi2 * (double) h * (double) i / (double) N;
                    mag += waveform[(size_t) i] * std::sin (phase);
                }
                mag = std::abs (mag) * 2.0 / (double) N;
                expect (mag < 1.0e-3, juce::String ("harmonic ") + juce::String (h) + " mag=" + juce::String ((float) mag));
            }

            // And harmonic 7 (within mip-5 cap) should be non-trivially present.
            {
                double mag = 0.0;
                for (int i = 0; i < N; ++i)
                {
                    const double phase = pi2 * 7.0 * (double) i / (double) N;
                    mag += waveform[(size_t) i] * std::sin (phase);
                }
                mag = std::abs (mag) * 2.0 / (double) N;
                expect (mag > 0.01, juce::String ("harmonic 7 mag=") + juce::String ((float) mag));
            }
        }
    }
};

static WavetableTests sWavetableTests;
