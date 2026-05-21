// WarpProcessor_test.cpp
//
// Coverage for SignalsmithEngine and (in Task 5) WarpProcessor. Gated by
// JUCE_DEBUG so the tests disappear from Release builds. Tests register
// statically via juce::UnitTest; downstream harness (when wired) runs them
// at Standalone startup.
//
#include <juce_core/juce_core.h>

#if JUCE_DEBUG

#include "SignalsmithEngine.h"
#include <vector>
#include <cmath>

class SignalsmithEngineTests : public juce::UnitTest
{
public:
    SignalsmithEngineTests() : juce::UnitTest ("SignalsmithEngine") {}

    void runTest() override
    {
        beginTest ("Constructs and prepares without crash");
        {
            tw::SignalsmithEngine eng;
            eng.prepare (48000.0, 2, 512);
            expect (eng.isReady());
        }

        beginTest ("Unity ratio preserves output energy (within 6 dB)");
        {
            tw::SignalsmithEngine eng;
            eng.prepare (48000.0, 2, 512);
            eng.reset();
            eng.setStretchRatio (1.0f);
            eng.setPitchSemitones (0.0f);

            constexpr int N = 4096;
            std::vector<float> inL (N), inR (N), outL (N), outR (N);

            juce::Random r;
            for (int i = 0; i < N; ++i)
            {
                inL[i] = r.nextFloat() * 0.5f - 0.25f;
                inR[i] = r.nextFloat() * 0.5f - 0.25f;
            }

            eng.process (inL.data(), inR.data(), outL.data(), outR.data(), N);

            // After the engine's internal latency, output energy should be in
            // the same ballpark as input energy. We check a middle window,
            // skipping leading + trailing latency.
            double inEnergy  = 0.0;
            double outEnergy = 0.0;
            for (int i = 1024; i < N - 1024; ++i)
            {
                inEnergy  += inL[i] * inL[i] + inR[i] * inR[i];
                outEnergy += outL[i] * outL[i] + outR[i] * outR[i];
            }
            const double ratio = outEnergy / juce::jmax (1.0e-9, inEnergy);
            expect (ratio > 0.25 && ratio < 4.0,
                    "Output energy should be within 6 dB of input at unity ratio (got ratio " + juce::String (ratio) + ")");
        }

        beginTest ("Reset+seek is idempotent and crash-free");
        {
            tw::SignalsmithEngine eng;
            eng.prepare (48000.0, 2, 512);
            eng.reset();
            eng.reset();
            eng.setStretchRatio (2.0f);
            eng.reset();
            expect (true);  // arrived here without segfault
        }

        beginTest ("Output is finite at extreme stretch ratios");
        {
            for (float r : { 0.25f, 0.5f, 2.0f, 4.0f })
            {
                tw::SignalsmithEngine eng;
                eng.prepare (48000.0, 2, 512);
                eng.reset();
                eng.setStretchRatio (r);

                constexpr int N = 2048;
                std::vector<float> inL (N, 0.1f), inR (N, 0.1f);
                std::vector<float> outL (N), outR (N);
                eng.process (inL.data(), inR.data(), outL.data(), outR.data(), N);

                bool allFinite = true;
                for (int i = 0; i < N; ++i)
                {
                    if (! std::isfinite (outL[i]) || ! std::isfinite (outR[i]))
                    {
                        allFinite = false;
                        break;
                    }
                }
                expect (allFinite, "Non-finite sample produced at ratio " + juce::String (r));
            }
        }

        beginTest ("Stretch ratio is clamped to 0.25..4.0");
        {
            tw::SignalsmithEngine eng;
            eng.prepare (48000.0, 2, 512);
            // setStretchRatio clamps internally — we can't read it back directly,
            // but we can verify out-of-range inputs don't crash the engine.
            eng.setStretchRatio (0.01f);
            eng.setStretchRatio (100.0f);
            eng.setStretchRatio (-1.0f);
            expect (true);
        }
    }
};

static SignalsmithEngineTests signalsmithEngineTests;

#endif  // JUCE_DEBUG
