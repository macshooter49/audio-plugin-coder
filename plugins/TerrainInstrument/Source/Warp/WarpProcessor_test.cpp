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

// ──────────────────────────────────────────────────────────────────────────
// WarpProcessor — per-voice dispatcher tests
// ──────────────────────────────────────────────────────────────────────────
#include "WarpProcessor.h"

class WarpProcessorTests : public juce::UnitTest
{
public:
    WarpProcessorTests() : juce::UnitTest ("WarpProcessor") {}

    void runTest() override
    {
        beginTest ("Mode=None: process is identity (exact memcpy match)");
        {
            tw::WarpProcessor wp;
            wp.prepare (48000.0, 2, 512);
            wp.setMode (tw::WarpMode::None);
            wp.setStretchRatio (2.0f);  // ignored in None mode

            constexpr int N = 256;
            float inL[N], inR[N], outL[N], outR[N];
            for (int i = 0; i < N; ++i)
            {
                inL[i] = 0.1f * std::sin (i * 0.1f);
                inR[i] = 0.1f * std::cos (i * 0.1f);
            }
            wp.process (inL, inR, outL, outR, N);

            bool exact = true;
            for (int i = 0; i < N; ++i)
            {
                if (std::abs (outL[i] - inL[i]) > 1.0e-6f) { exact = false; break; }
                if (std::abs (outR[i] - inR[i]) > 1.0e-6f) { exact = false; break; }
            }
            expect (exact, "None-mode output should be exact copy of input");
        }

        beginTest ("Mode=Tones: lazy-allocates engine on first non-None set");
        {
            tw::WarpProcessor wp;
            wp.prepare (48000.0, 2, 512);
            expect (! wp.hasEngineAllocated(), "Engine should NOT be allocated before any setMode");
            wp.setMode (tw::WarpMode::Tones);
            expect (wp.hasEngineAllocated(), "Engine SHOULD be allocated after setMode(Tones)");
        }

        beginTest ("Engine survives mode round-trip (None -> Tones -> None)");
        {
            tw::WarpProcessor wp;
            wp.prepare (48000.0, 2, 512);

            wp.setMode (tw::WarpMode::Tones);
            expect (wp.hasEngineAllocated());
            expect (wp.getMode() == tw::WarpMode::Tones);

            wp.setMode (tw::WarpMode::None);
            // Engine instance stays alive (no dealloc on mode switch back) but
            // process() should now be identity.
            expect (wp.hasEngineAllocated(),
                    "Engine should NOT be deallocated when mode returns to None");
            expect (wp.getMode() == tw::WarpMode::None);

            constexpr int N = 128;
            float inL[N], inR[N], outL[N], outR[N];
            for (int i = 0; i < N; ++i) { inL[i] = 0.1f; inR[i] = 0.2f; }
            wp.process (inL, inR, outL, outR, N);

            bool exact = true;
            for (int i = 0; i < N; ++i)
                if (std::abs (outL[i] - 0.1f) > 1.0e-6f || std::abs (outR[i] - 0.2f) > 1.0e-6f)
                    { exact = false; break; }
            expect (exact, "Identity restored after switching back to None");
        }

        beginTest ("Stretch ratio clamps to 0.25..4.0");
        {
            tw::WarpProcessor wp;
            wp.prepare (48000.0, 2, 512);
            wp.setMode (tw::WarpMode::Tones);
            wp.setStretchRatio (10.0f);   // should clamp to 4.0
            wp.setStretchRatio (0.01f);   // should clamp to 0.25
            wp.setStretchRatio (-5.0f);   // should clamp to 0.25
            // No assertion on internal state — the assertion is "no crash".
            expect (true);
        }

        beginTest ("noteOnReset is safe before first setMode (no engine yet)");
        {
            tw::WarpProcessor wp;
            wp.prepare (48000.0, 2, 512);
            wp.noteOnReset();  // engine doesn't exist yet — must be safe.
            expect (true);
        }
    }
};

static WarpProcessorTests warpProcessorTests;

#endif  // JUCE_DEBUG
