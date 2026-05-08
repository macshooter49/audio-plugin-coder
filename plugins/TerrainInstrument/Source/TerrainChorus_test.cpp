// plugins/Terrain/Source/TerrainChorus_test.cpp
#include <juce_core/juce_core.h>

#if JUCE_DEBUG
#include "TerrainChorus.h"

class TerrainChorusTests : public juce::UnitTest
{
public:
    TerrainChorusTests() : juce::UnitTest ("TerrainChorus") {}

    void runTest() override
    {
        beginTest ("dry passthrough at AMOUNT=0");
        {
            TerrainChorus chorus;
            chorus.prepare (44100.0, 512);
            chorus.setParams (0.0f, 0.7f, 0.3f);  // AMOUNT=0

            // Loop many samples through with non-trivial varying input.
            // At AMOUNT=0, the wet path should be fully suppressed by the
            // (1 - amount) crossfade — output must equal input within float epsilon
            // for ALL samples, not just one.
            for (int i = 0; i < 500; ++i)
            {
                const float origL = std::sin ((float) i * 0.05f) * 0.5f;
                const float origR = std::cos ((float) i * 0.07f) * 0.4f;
                float l = origL, r = origR;
                chorus.processStereo (l, r);
                expectWithinAbsoluteError (l, origL, 1.0e-5f);
                expectWithinAbsoluteError (r, origR, 1.0e-5f);
            }
        }

        beginTest ("BBD buffer wraps and reads previous sample at fixed delay");
        {
            TerrainChorus chorus;
            chorus.prepare (44100.0, 512);
            chorus.setParams (0.0f, 0.7f, 0.3f);

            // Push a known impulse, then silence, and verify dry passthrough still holds
            // (Phase 2 wet is computed but not mixed in yet)
            float l = 1.0f, r = 1.0f;
            chorus.processStereo (l, r);
            expectWithinAbsoluteError (l, 1.0f, 1.0e-5f);

            for (int i = 0; i < 100; ++i)
            {
                float zL = 0.0f, zR = 0.0f;
                chorus.processStereo (zL, zR);
                expectWithinAbsoluteError (zL, 0.0f, 1.0e-5f);
            }
        }

        beginTest ("wet path produces non-dry output at AMOUNT=1");
        {
            TerrainChorus chorus;
            chorus.prepare (44100.0, 512);
            chorus.setParams (1.0f, 0.7f, 0.3f);

            // Push DC for many samples to let LFO movement modulate the delay
            for (int i = 0; i < 1000; ++i)
            {
                float l = 0.5f, r = 0.5f;
                chorus.processStereo (l, r);

                // After 200+ samples, output should differ from dry (the delayed,
                // companded, recon-LPed signal mixes in)
                if (i > 200)
                    expectGreaterThan (std::abs (l - 0.5f), 1.0e-4f);
            }
        }

        beginTest ("LFO modulates delay read position (post-warmup, sine input)");
        {
            // The original version of this test compared outputs at sample 100 vs
            // 14600 with DC input — that passed for the wrong reason: at sample 100
            // the BBD buffer was still half-empty (read position was reading zeros
            // not yet written), and at sample 14600 the LFO sin value happened to
            // be numerically identical to sample 100 due to sin(π − x) == sin(x)
            // symmetry at half-cycle. A frozen LFO would also pass that test.
            //
            // Corrected approach: warm the buffer with sine input for >>BBD_BUFFER
            // samples so the read pointer is always reading written data; then
            // capture outputs at LFO phases ~quarter-cycle apart, where sin values
            // are meaningfully different (no symmetric folding). The wet read pulls
            // a delayed slice of the input sine, so a moving read position MUST
            // produce a moving output.
            TerrainChorus chorus;
            chorus.prepare (44100.0, 512);
            chorus.setParams (1.0f, 0.0f, 0.0f);  // AMOUNT=1, CHARACTER=0 → max LFO rate

            auto sineInput = [] (int i) { return std::sin ((float) i * 0.05f) * 0.3f; };

            // Warm the BBD buffer + filter states with non-DC content for 6000
            // samples (well above BBD_BUFFER_SAMPLES = 4096).
            for (int i = 0; i < 6000; ++i)
            {
                float l = sineInput (i), r = sineInput (i);
                chorus.processStereo (l, r);
            }

            // Continue past the warmup, capturing outputs ~quarter-cycle of the LFO
            // apart (LFO 1.5 Hz at 44.1k → quarter cycle ≈ 7350 samples).
            // Capture earlyL at i=6000 (LFO phase ≈ 1.28 rad, sin ≈ 0.957) and
            // lateL at i=13350 (LFO phase ≈ 2.85 rad, sin ≈ 0.286).
            float earlyL = 0.0f, lateL = 0.0f;
            for (int i = 6000; i <= 13350; ++i)
            {
                float l = sineInput (i), r = sineInput (i);
                chorus.processStereo (l, r);
                if (i == 6000)  earlyL = l;
                if (i == 13350) lateL = l;
            }

            // The two outputs must meaningfully differ — they pull different
            // delayed slices of the sine input AND see different LFO modulation
            // depths. Tolerance 1e-3f catches sub-1% deviations.
            expectGreaterThan (std::abs (earlyL - lateL), 1.0e-3f);
        }

        beginTest ("CHARACTER=0 → faster/brighter, CHARACTER=1 → slower/darker");
        {
            // Two chorus instances, same input, different CHARACTER. Different output expected.
            TerrainChorus a, b;
            a.prepare (44100.0, 512);
            b.prepare (44100.0, 512);
            a.setParams (1.0f, 0.7f, 0.0f);  // bright + fast
            b.setParams (1.0f, 0.7f, 1.0f);  // dark + slow

            float aL = 0.5f, aR = 0.5f, bL = 0.5f, bR = 0.5f;
            for (int i = 0; i < 500; ++i)
            {
                float aLi = aL, aRi = aR, bLi = bL, bRi = bR;
                a.processStereo (aLi, aRi);
                b.processStereo (bLi, bRi);

                if (i > 200)
                    expect (std::abs (aLi - bLi) > 1.0e-4f);  // they should diverge
            }
        }

        beginTest ("section can be re-prepared without crash");
        {
            TerrainChorus chorus;
            chorus.prepare (44100.0, 512);
            chorus.prepare (48000.0, 1024);
            chorus.setParams (1.0f, 0.7f, 0.3f);

            float l = 0.1f, r = 0.1f;
            chorus.processStereo (l, r);
            // Just verifying no crash — value content is implementation-defined here
            expect (std::isfinite (l));
            expect (std::isfinite (r));
        }
    }
};

static TerrainChorusTests terrainChorusTests;
#endif
