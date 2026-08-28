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
            expectEquals (wt.getNumMipLevels(), tw::Wavetable::kNumMipLevels, "spec table should have all mip tiers");
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

        beginTest ("mipLevelForPhaseIncrement picks the RICHEST alias-safe level across the whole range");
        {
            // fb300 — ladder-independent invariant (survives any retune of kMipMaxHarmonics):
            // for every MIDI note the selected level must (a) be alias-safe (its harmonic cap
            // sits at/under Nyquist for that pitch) and (b) be the richest such level (the next
            // level up would alias). This is the anti-aliasing + no-wasted-bandwidth guarantee.
            const double sr   = 48000.0;
            const auto&  caps = tw::Wavetable::kMipMaxHarmonics;
            for (int note = 12; note <= 127; ++note)
            {
                const double hz      = 440.0 * std::pow (2.0, (note - 69) / 12.0);
                const double maxSafe = 0.5 / (hz / sr);                 // harmonics that fit under Nyquist
                const int    lvl     = tw::Wavetable::mipLevelForMidiNote (note, sr);
                expect ((double) caps[(size_t) lvl] <= maxSafe + 1.0e-6,
                        juce::String ("note ") + juce::String (note) + " picked an ALIASING level");
                if (lvl > 0)
                    expect ((double) caps[(size_t) (lvl - 1)] > maxSafe,
                            juce::String ("note ") + juce::String (note) + " left safe harmonics on the table");
            }
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

        beginTest ("Anti-aliasing: Square at C7 selects the RICHEST alias-free cap, and nothing above it sounds");
        {
            tw::Wavetable wt;
            wt.buildFromSpec (tw::Wavetable::makeSquareSpec());
            // C7 ~ 2093 Hz at 48 kHz → maxSafe ~ 11.47.
            // 🚨 fb530 — THIS TEST USED TO ASSERT THE CAP WAS 8, AND THAT WAS THE BUG IT WAS
            //    GUARDING. fb301's ladder went 16, 8, 4, 2 at the top — OCTAVE-spaced — so C7 got
            //    8 of the 11 harmonics Nyquist allows and threw 3 away. The superset ladder adds
            //    the integers, so C7 now picks 11. Asserting a LITERAL cap froze the defect in
            //    place; the invariant that actually matters is the one the fb300 test above
            //    states — pick the richest alias-free level — so assert THAT and derive the cap.
            const double phaseIncC7 = 2093.0 / 48000.0;
            const int lvl = tw::Wavetable::mipLevelForPhaseIncrement (phaseIncC7);
            const int cap = tw::Wavetable::kMipMaxHarmonics[(size_t) lvl];
            const int maxSafe = (int) (0.5 / phaseIncC7);                     // 11
            expect (cap <= maxSafe, juce::String ("C7 picked an ALIASING cap ") + juce::String (cap));
            expect (lvl == 0 || tw::Wavetable::kMipMaxHarmonics[(size_t) (lvl - 1)] > maxSafe,
                    juce::String ("C7 left safe harmonics on the table: cap ") + juce::String (cap)
                    + " when " + juce::String (maxSafe) + " fit");

            // Quick DFT: read one cycle and find magnitude at each harmonic bin.
            constexpr int N = 2048;
            std::array<float, N> waveform;
            for (int i = 0; i < N; ++i)
                waveform[(size_t) i] = wt.lookup (lvl, 0.5f, (float) i / (float) N);

            // Every odd harmonic ABOVE the chosen cap must be silent — derived from `cap`, so this
            // keeps testing the real thing whatever the ladder is retuned to.
            // 🚨 fb530 — BOTH QUADRATURES. This projected onto sin() only, and makeSquareSpec writes
            //    EVERY harmonic at cosine phase (π/2), so the sine projection of a square is exactly
            //    0.00000 for every h — including the "harmonic 7 must be PRESENT" check below, which
            //    therefore could never have passed. It has never fired: Wavetable_test.cpp is compiled
            //    into the plugin (CMakeLists.txt:55) but nothing runs juce::UnitTestRunner, so these
            //    assertions are compile-only. Measured on the shipped table, framePos 0.5, mip cap 11:
            //    |X| h7 = 0.07038, h11 = 0.05446, h12 = 0.00000 — but sin-projection = 0.00000 for ALL.
            constexpr double pi2 = 2.0 * 3.14159265358979323846;
            const auto magAt = [&waveform] (int h)
            {
                double re = 0.0, im = 0.0;
                for (int i = 0; i < N; ++i)
                {
                    const double a = pi2 * (double) h * (double) i / (double) N;
                    re += (double) waveform[(size_t) i] * std::sin (a);
                    im += (double) waveform[(size_t) i] * std::cos (a);
                }
                return std::sqrt (re * re + im * im) * 2.0 / (double) N;
            };
            for (int h = cap + 1; h <= cap + 8; h += 2)
            {
                const double mag = magAt (h);
                expect (mag < 1.0e-3, juce::String ("harmonic ") + juce::String (h) + " mag=" + juce::String ((float) mag));
            }

            // And the highest ODD harmonic INSIDE the cap must be non-trivially present.
            {
                const int hIn = (cap % 2) ? cap : cap - 1;
                const double mag = magAt (hIn);
                expect (mag > 0.01, juce::String ("harmonic ") + juce::String (hIn) + " mag=" + juce::String ((float) mag));
            }
        }
    }
};

static WavetableTests sWavetableTests;
