// plugins/Terrain/Source/ParametricEQ_test.cpp
//
// Linked into the Terrain target, runs only in Debug Standalone via
// juce::UnitTestRunner kicked off from main(). Skipped in Release.
//
// To run: build Terrain_Standalone in Debug, launch with --tests flag.
//
#if JUCE_DEBUG

#include <juce_core/juce_core.h>
#include <juce_dsp/juce_dsp.h>
#include "ParametricEQ.h"

class ParametricEQTests : public juce::UnitTest
{
public:
    ParametricEQTests() : juce::UnitTest ("ParametricEQ") {}

    void runTest() override
    {
        beginTest ("Flat passthrough at default settings");
        {
            ParametricEQ eq;
            eq.prepare (48000.0, 512);
            // All bands at 0 dB gain — should be near-identity.
            for (int b = 0; b < ParametricEQ::NUM_BANDS; ++b)
                eq.setBandParams (b, 1000.0f, 0.0f, 0.707f, false);
            // HP/LP off.
            eq.setHp (20.0f,    ParametricEQ::Slope12, true);
            eq.setLp (20000.0f, ParametricEQ::Slope12, true);

            // Drive 10000 samples of impulse-spaced noise and check finite + bounded.
            juce::Random r;
            float maxOut = 0.0f;
            for (int i = 0; i < 10000; ++i)
            {
                const float x = r.nextFloat() * 2.0f - 1.0f;
                const float y = eq.processSample (x);
                expect (std::isfinite (y));
                maxOut = std::max (maxOut, std::abs (y));
            }
            expect (maxOut <= 1.5f, "Output exceeded reasonable bound");
        }

        beginTest ("Single bell band frequency response (+12 dB at 1 kHz)");
        {
            constexpr double sr = 48000.0;
            constexpr int   N   = 8192;
            ParametricEQ eq;
            eq.prepare (sr, N);
            // Set band 0 to +12 dB at 1 kHz, Q=4. All other bands flat.
            eq.setBandParams (0, 1000.0f, 12.0f, 4.0f, false);
            for (int b = 1; b < ParametricEQ::NUM_BANDS; ++b)
                eq.setBandParams (b, 1000.0f * (b + 1), 0.0f, 0.707f, false);
            eq.setHp (20.0f,    ParametricEQ::Slope12, true);
            eq.setLp (20000.0f, ParametricEQ::Slope12, true);

            // Generate a 1 kHz sine, run through the EQ, measure RMS gain.
            constexpr int warmUp = 4096;
            juce::AudioBuffer<float> buf (1, N + warmUp);
            auto* w = buf.getWritePointer (0);
            for (int i = 0; i < N + warmUp; ++i)
                w[i] = std::sin (2.0 * juce::MathConstants<double>::pi * 1000.0 * i / sr);
            for (int i = 0; i < warmUp; ++i)
                (void) eq.processSample (w[i]);
            double sumSqIn = 0.0, sumSqOut = 0.0;
            for (int i = 0; i < N; ++i)
            {
                const float x = w[warmUp + i];
                const float y = eq.processSample (x);
                sumSqIn  += (double) x * x;
                sumSqOut += (double) y * y;
            }
            const double gainDb = 10.0 * std::log10 (sumSqOut / sumSqIn);
            expectWithinAbsoluteError (gainDb, 12.0, 1.0, "Band gain at 1 kHz should be +12 dB");
        }

        beginTest ("HP slope verification at 24 dB/oct");
        {
            constexpr double sr = 48000.0;
            constexpr int N = 16384;
            ParametricEQ eq;
            eq.prepare (sr, N);
            for (int b = 0; b < ParametricEQ::NUM_BANDS; ++b)
                eq.setBandParams (b, 1000.0f, 0.0f, 0.707f, false);
            eq.setHp (1000.0f, ParametricEQ::Slope24, false);
            eq.setLp (20000.0f, ParametricEQ::Slope12, true);

            // Probe at 500 Hz (one octave below) — expect ~-24 dB.
            constexpr int warmUp = 4096;
            auto rmsGainDb = [&] (double freq) {
                // Reset filter state by re-preparing.
                ParametricEQ local;
                local.prepare (sr, N);
                for (int b = 0; b < ParametricEQ::NUM_BANDS; ++b)
                    local.setBandParams (b, 1000.0f, 0.0f, 0.707f, false);
                local.setHp (1000.0f, ParametricEQ::Slope24, false);
                local.setLp (20000.0f, ParametricEQ::Slope12, true);

                double sumSqIn = 0.0, sumSqOut = 0.0;
                for (int i = 0; i < warmUp + N; ++i)
                {
                    const float x = std::sin (2.0 * juce::MathConstants<double>::pi * freq * i / sr);
                    const float y = local.processSample (x);
                    if (i >= warmUp) { sumSqIn += (double) x * x; sumSqOut += (double) y * y; }
                }
                return 10.0 * std::log10 (sumSqOut / sumSqIn);
            };

            const double dbAtHalf = rmsGainDb (500.0);
            expectWithinAbsoluteError (dbAtHalf, -24.0, 2.0, "HP at 500 Hz with 1 kHz/24 should be ~-24 dB");
        }
    }
};

static ParametricEQTests parametricEQTests;

#endif // JUCE_DEBUG
