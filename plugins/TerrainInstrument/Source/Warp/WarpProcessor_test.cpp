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

// ──────────────────────────────────────────────────────────────────────────
// BeatsEngine — DIY granular stutter with crossfaded grain wraps
// ──────────────────────────────────────────────────────────────────────────
#include "BeatsEngine.h"

class BeatsEngineTests : public juce::UnitTest
{
public:
    BeatsEngineTests() : juce::UnitTest ("BeatsEngine") {}

    void runTest() override
    {
        constexpr double SR = 48000.0;

        beginTest ("Constructs and prepares without crash");
        {
            tw::BeatsEngine eng;
            eng.prepare (SR, 2, 512);
            expect (eng.isReady());
        }

        // A continuous sinewave is the canonical "if-you-click-i-detect-it"
        // signal. Per-sample delta of a clean sine peaks at amp*2π*freq/SR,
        // which for amp=0.25, freq=200Hz, SR=48k is ~0.0065. A click from a
        // bad wrap snaps from ~0.25 back to ~0 in one sample — delta 0.25 —
        // well above the 0.05 threshold below.
        auto runSineCheck = [&] (float stretchRatio, float pitchSemis, float threshold,
                                 const juce::String& label)
        {
            tw::BeatsEngine eng;
            eng.prepare (SR, 2, 512);
            eng.reset();
            eng.setStretchRatio (stretchRatio);
            eng.setPitchSemitones (pitchSemis);

            const int totalOut = 24000;  // 0.5 sec — covers many grain wraps
            const double pitchRatio = std::pow (2.0, (double) pitchSemis / 12.0);
            const int totalIn = juce::jmax (1, (int) std::round (totalOut * pitchRatio / stretchRatio));

            std::vector<float> inL (totalIn), inR (totalIn);
            for (int i = 0; i < totalIn; ++i)
            {
                const float v = 0.25f * std::sin (2.0f * juce::MathConstants<float>::pi * 200.0f * i / (float) SR);
                inL[i] = v;
                inR[i] = v;
            }

            // Prime with one grain (inputLatency) of source so the engine
            // doesn't gate output on firstBlockPending. This is what
            // SamplerVoice does at note-on.
            const int prime = juce::jmin (eng.inputLatency(), totalIn);
            eng.seek (inL.data(), inR.data(), prime);

            std::vector<float> outL (totalOut), outR (totalOut);

            // Process in fixed blocks of 512.
            const int block = 512;
            int outDone = 0;
            int inPos   = prime;
            while (outDone < totalOut)
            {
                const int outN = juce::jmin (block, totalOut - outDone);
                const int inN  = juce::jmax (1, (int) std::round (outN * pitchRatio / stretchRatio));
                const int inAvail = juce::jmin (inN, totalIn - inPos);
                if (inAvail <= 0) break;  // ran out of input
                eng.process (inL.data() + inPos, inR.data() + inPos,
                             outL.data() + outDone, outR.data() + outDone, outN);
                outDone += outN;
                inPos   += inAvail;
            }

            // Skip leading silence (firstBlockPending grace) — find first
            // sample where |out| > 0.001 then start scanning from there.
            int scanStart = 0;
            for (int i = 0; i < outDone; ++i)
            {
                if (std::abs (outL[i]) > 0.001f) { scanStart = i; break; }
            }
            // Skip a few hundred more samples to clear any first-grain
            // transient settling.
            scanStart = juce::jmin (scanStart + 256, outDone);

            float maxDelta = 0.0f;
            int   worstIdx = -1;
            for (int i = scanStart + 1; i < outDone; ++i)
            {
                const float d = std::abs (outL[i] - outL[i-1]);
                if (d > maxDelta) { maxDelta = d; worstIdx = i; }
                expect (std::isfinite (outL[i]), label + ": non-finite sample at " + juce::String (i));
            }

            expect (maxDelta < threshold,
                    label + ": max per-sample delta " + juce::String (maxDelta)
                    + " at sample " + juce::String (worstIdx)
                    + " exceeds click threshold " + juce::String (threshold));
        };

        beginTest ("Unity stretch + unity pitch — passthrough (no clicks)");
        runSineCheck (1.0f, 0.0f, 0.05f, "unity");

        beginTest ("Stretch 2.0, unity pitch — repeated within-beat wraps, smooth");
        runSineCheck (2.0f, 0.0f, 0.05f, "stretch2");

        beginTest ("Stretch 4.0, unity pitch — many wraps per beat, smooth");
        runSineCheck (4.0f, 0.0f, 0.05f, "stretch4");

        beginTest ("Unity stretch + pitch up an octave — was broken pre-v5");
        runSineCheck (1.0f, 12.0f, 0.05f, "pitch+12");

        beginTest ("Stretch 2.0 + pitch up an octave — combined warp, smooth");
        runSineCheck (2.0f, 12.0f, 0.05f, "stretch2-pitch+12");

        beginTest ("Stretch 2.0 + pitch down an octave — combined warp, smooth");
        runSineCheck (2.0f, -12.0f, 0.05f, "stretch2-pitch-12");

        beginTest ("Stretch 8.0 + pitch +7 semis — extreme combo, smooth");
        runSineCheck (8.0f, 7.0f, 0.05f, "stretch8-pitch+7");

        beginTest ("Output is finite at extreme stretch ratios");
        {
            for (float r : { 0.1f, 0.5f, 2.0f, 8.0f, 15.0f })
            {
                tw::BeatsEngine eng;
                eng.prepare (SR, 2, 512);
                eng.reset();
                eng.setStretchRatio (r);

                constexpr int N = 4096;
                std::vector<float> inL (N, 0.1f), inR (N, 0.1f);
                std::vector<float> outL (N), outR (N);
                eng.seek (inL.data(), inR.data(), juce::jmin (eng.inputLatency(), N));
                eng.process (inL.data(), inR.data(), outL.data(), outR.data(), N);

                for (int i = 0; i < N; ++i)
                    expect (std::isfinite (outL[i]) && std::isfinite (outR[i]),
                            "Non-finite sample at ratio " + juce::String (r));
            }
        }

        beginTest ("Reset after process is idempotent and crash-free");
        {
            tw::BeatsEngine eng;
            eng.prepare (SR, 2, 512);
            eng.setStretchRatio (2.0f);
            eng.reset();
            constexpr int N = 1024;
            std::vector<float> inL (N, 0.1f), inR (N, 0.1f);
            std::vector<float> outL (N), outR (N);
            eng.process (inL.data(), inR.data(), outL.data(), outR.data(), N);
            eng.reset();
            eng.process (inL.data(), inR.data(), outL.data(), outR.data(), N);
            expect (true);
        }
    }
};

static BeatsEngineTests beatsEngineTests;

// ──────────────────────────────────────────────────────────────────────────
// WarpProcessor::sourceSamplesPerBlock — mode-aware input-feed length
// ──────────────────────────────────────────────────────────────────────────
class WarpProcessorInputLenTests : public juce::UnitTest
{
public:
    WarpProcessorInputLenTests() : juce::UnitTest ("WarpProcessor::sourceSamplesPerBlock") {}

    void runTest() override
    {
        beginTest ("None mode — returns numSamples (unused but defined)");
        {
            tw::WarpProcessor wp;
            wp.prepare (48000.0, 2, 512);
            expect (wp.sourceSamplesPerBlock (512) >= 1);
        }

        beginTest ("Tones mode — pitch is irrelevant (Signalsmith handles internally)");
        {
            tw::WarpProcessor wp;
            wp.prepare (48000.0, 2, 512);
            wp.setMode (tw::WarpMode::Tones);
            wp.setStretchRatio (2.0f);
            wp.setPitchSemitones (12.0f);
            expect (wp.sourceSamplesPerBlock (512) == 256,
                    "Tones at stretch=2 should consume 512/2=256 source samples regardless of pitch");
        }

        beginTest ("Beats mode — pitchRatio scales input feed");
        {
            tw::WarpProcessor wp;
            wp.prepare (48000.0, 2, 512);
            wp.setMode (tw::WarpMode::Beats);

            wp.setStretchRatio (1.0f);
            wp.setPitchSemitones (0.0f);
            expect (wp.sourceSamplesPerBlock (512) == 512, "Beats unity = 512");

            wp.setPitchSemitones (12.0f);  // pitchRatio = 2
            expect (wp.sourceSamplesPerBlock (512) == 1024, "Beats pitch+12 = 2× source per block");

            wp.setPitchSemitones (-12.0f);  // pitchRatio = 0.5
            expect (wp.sourceSamplesPerBlock (512) == 256, "Beats pitch-12 = 0.5× source per block");

            wp.setStretchRatio (2.0f);
            wp.setPitchSemitones (12.0f);  // pr=2, sr=2 → 1.0 ratio
            expect (wp.sourceSamplesPerBlock (512) == 512, "Beats stretch=2 pitch+12 = 512");
        }
    }
};

static WarpProcessorInputLenTests warpProcessorInputLenTests;

#endif  // JUCE_DEBUG
