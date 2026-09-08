// plugins/Terrain/Source/MoogDelay_test.cpp
//
// Linked into the Terrain target. The juce_core include must come BEFORE
// the JUCE_DEBUG gate — without the include, the macro is undefined and
// the entire file compiles away to nothing.
//
#include <juce_core/juce_core.h>

#if JUCE_DEBUG

#include "MoogDelay.h"

class MoogDelayTests : public juce::UnitTest
{
public:
    MoogDelayTests() : juce::UnitTest ("MoogDelay") {}

    void runTest() override
    {
        beginTest ("Dry passthrough at mix=0");
        {
            MoogDelay d;
            d.prepare (48000.0, 512);

            juce::Random r;
            for (int i = 0; i < 1000; ++i)
            {
                float L = r.nextFloat() * 2.0f - 1.0f;
                float R = r.nextFloat() * 2.0f - 1.0f;
                const float origL = L;
                const float origR = R;

                MoogDelay::Params p;
                p.timeMs = 350.0f;
                p.feedback = 0.0f;
                p.mix = 0.0f;
                d.processStereo (L, R, p);

                expectWithinAbsoluteError (L, origL, 1.0e-5f);
                expectWithinAbsoluteError (R, origR, 1.0e-5f);
            }
        }

        beginTest ("Fractional delay reproduces impulse at the requested time");
        {
            MoogDelay d;
            const double sr = 48000.0;
            d.prepare (sr, 512);

            const float delayMs = 100.0f;
            const int delaySamples = static_cast<int> (sr * delayMs / 1000.0);

            MoogDelay::Params p;
            p.timeMs = delayMs;
            p.feedback = 0.0f;
            p.mix = 1.0f;
            p.tone = 0.0f;
            p.character = 0.0f;
            p.modDepth = 0.0f;
            p.duck = 0.0f;

            float L = 1.0f, R = 1.0f;
            d.processStereo (L, R, p);  // sample 0: impulse
            for (int n = 1; n < delaySamples; ++n)
            {
                L = 0.0f; R = 0.0f;
                d.processStereo (L, R, p);
                expectLessThan (std::abs (L), 0.05f, "Pre-delay leak at sample " + juce::String (n));
            }
            float maxAround = 0.0f;
            for (int n = 0; n < 8; ++n)
            {
                L = 0.0f; R = 0.0f;
                d.processStereo (L, R, p);
                maxAround = std::max (maxAround, std::abs (L));
            }
            expectGreaterThan (maxAround, 0.2f, "Impulse did not arrive at expected delay");
        }

        beginTest ("Long delay attenuates highs more than short delay (BBD bandwidth tracking)");
        {
            const double sr = 48000.0;
            auto driveAndMeasureHighEnergy = [sr] (float delayMs) -> float
            {
                MoogDelay d;
                d.prepare (sr, 512);

                MoogDelay::Params p;
                p.timeMs = delayMs;
                p.feedback = 0.0f;
                p.mix = 1.0f;
                p.tone = 0.0f;
                p.character = 0.0f;
                p.modDepth = 0.0f;

                const float freq = 12000.0f;
                const int totalSamples = static_cast<int> (sr * (delayMs / 1000.0 + 0.2));
                float sumSq = 0.0f;
                int counted = 0;
                for (int n = 0; n < totalSamples; ++n)
                {
                    const float t = static_cast<float> (n) / static_cast<float> (sr);
                    float L = 0.5f * std::sin (2.0f * 3.14159265f * freq * t);
                    float R = L;
                    d.processStereo (L, R, p);
                    if (n > static_cast<int> (sr * delayMs / 1000.0) + 100)
                    {
                        sumSq += L * L;
                        ++counted;
                    }
                }
                return counted > 0 ? std::sqrt (sumSq / counted) : 0.0f;
            };

            const float shortHigh = driveAndMeasureHighEnergy (50.0f);
            const float longHigh  = driveAndMeasureHighEnergy (1000.0f);

            expectGreaterThan (shortHigh, longHigh,
                               "Long delay should attenuate highs more than short delay");
            expectLessThan (longHigh / shortHigh, 0.5f,
                            "Long-delay high-frequency energy should be at most half of short-delay");
        }

        beginTest ("Feedback at 1.0+ produces sustained tail without NaN");
        {
            MoogDelay d;
            d.prepare (48000.0, 512);
            MoogDelay::Params p;
            p.timeMs = 200.0f;
            p.feedback = 1.10f;
            p.mix = 1.0f;
            p.character = 0.5f;

            float L = 1.0f, R = 1.0f;
            d.processStereo (L, R, p);

            float maxOut = 0.0f;
            for (int n = 1; n < 96000; ++n)
            {
                L = 0.0f; R = 0.0f;
                d.processStereo (L, R, p);
                expect (std::isfinite (L), "Non-finite output at sample " + juce::String (n));
                expect (std::isfinite (R));
                maxOut = std::max (maxOut, std::abs (L));
            }
            expectLessThan (maxOut, 4.0f, "Self-oscillation should be soft-clipped");
        }

        beginTest ("Feedback at 0 silences tail after one delay period");
        {
            MoogDelay d;
            const double sr = 48000.0;
            d.prepare (sr, 512);
            MoogDelay::Params p;
            p.timeMs = 100.0f;
            p.feedback = 0.0f;
            p.mix = 1.0f;
            p.character = 0.0f;

            float L = 1.0f, R = 1.0f;
            d.processStereo (L, R, p);
            const int oneDelay = static_cast<int> (sr * 0.1) + 50;
            for (int n = 1; n < oneDelay; ++n)
            {
                L = 0.0f; R = 0.0f;
                d.processStereo (L, R, p);
            }
            float maxAfter = 0.0f;
            for (int n = 0; n < oneDelay; ++n)
            {
                L = 0.0f; R = 0.0f;
                d.processStereo (L, R, p);
                maxAfter = std::max (maxAfter, std::abs (L));
            }
            expectLessThan (maxAfter, 0.05f, "Tail should die without feedback");
        }

        beginTest ("Freeze sustains current buffer indefinitely");
        {
            MoogDelay d;
            d.prepare (48000.0, 512);
            MoogDelay::Params p;
            p.timeMs = 100.0f;
            p.feedback = 0.0f;
            p.mix = 1.0f;
            p.character = 0.0f;

            juce::Random r;
            for (int n = 0; n < 9600; ++n)
            {
                float L = r.nextFloat() * 0.2f - 0.1f;
                float R = L;
                d.processStereo (L, R, p);
            }
            p.freezeHeld = true;
            float maxDuringFreeze = 0.0f;
            for (int n = 0; n < 48000; ++n)
            {
                float L = 0.0f, R = 0.0f;
                d.processStereo (L, R, p);
                maxDuringFreeze = std::max (maxDuringFreeze, std::abs (L));
            }
            expectGreaterThan (maxDuringFreeze, 0.02f, "Freeze should sustain buffer content");
            expect (std::isfinite (maxDuringFreeze));
        }
    }
};

static MoogDelayTests moogDelayTests;

#endif
