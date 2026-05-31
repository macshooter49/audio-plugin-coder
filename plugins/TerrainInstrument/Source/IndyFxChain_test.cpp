// IndyFxChain_test.cpp
// JUCE UnitTest for the single shared indy FX chain (option 1).
#include "IndyFxChain.h"
#include "FxMask.h"
#include <juce_core/juce_core.h>
#include <juce_audio_basics/juce_audio_basics.h>

class IndyFxChainTests : public juce::UnitTest
{
public:
    IndyFxChainTests() : juce::UnitTest ("IndyFxChain", "TerrainInstrument") {}

    void runTest() override
    {
        const double sr        = 48000.0;
        const int    blockSize = 256;

        beginTest ("prepare allocates without crashing");
        {
            tw::IndyFxChain chain;
            chain.prepare (sr, blockSize);
            chain.reset();
        }

        beginTest ("empty mask: dry pass-through (output == input, not silence)");
        {
            tw::IndyFxChain chain;
            chain.prepare (sr, blockSize);

            juce::AudioBuffer<float> input (2, blockSize);
            juce::AudioBuffer<float> output (2, blockSize);
            output.clear();

            // Fill input with a deterministic ramp
            for (int i = 0; i < blockSize; ++i)
            {
                const float s = 0.5f * std::sin (juce::MathConstants<float>::twoPi * 440.0f * (float) i / (float) sr);
                input.setSample (0, i, s);
                input.setSample (1, i, s);
            }

            const std::uint8_t empty = tw::fxmask::packMask (false, 0, false, false, false, false);
            chain.setMask (empty);
            // No setParamTargets call — defaults must produce sensible passthrough behavior.
            chain.processInto (input, output, blockSize);

            // Empty mask must NOT drop the signal. Output should equal input
            // sample-for-sample (additive into a cleared output).
            for (int i = 0; i < blockSize; ++i)
            {
                expectWithinAbsoluteError (output.getSample (0, i), input.getSample (0, i), 1e-6f,
                                           "empty-mask output[0] must equal input[0]");
                expectWithinAbsoluteError (output.getSample (1, i), input.getSample (1, i), 1e-6f,
                                           "empty-mask output[1] must equal input[1]");
            }
        }

        beginTest ("EQ-only mask: non-silent output (EQ passthrough by default param targets)");
        {
            tw::IndyFxChain chain;
            chain.prepare (sr, blockSize);

            juce::AudioBuffer<float> input (2, blockSize);
            juce::AudioBuffer<float> output (2, blockSize);
            output.clear();
            for (int i = 0; i < blockSize; ++i)
            {
                const float s = 0.5f * std::sin (juce::MathConstants<float>::twoPi * 440.0f * (float) i / (float) sr);
                input.setSample (0, i, s);
                input.setSample (1, i, s);
            }

            const std::uint8_t eqOnly = tw::fxmask::packMask (false, 0, false, false, true, false);
            chain.setMask (eqOnly);
            tw::IndyFxChain::ParamTargets defaults;  // all band/hp/lp bypass = true → near-passthrough EQ
            chain.setParamTargets (defaults);

            chain.processInto (input, output, blockSize);

            float peak = 0.0f;
            for (int i = 0; i < blockSize; ++i)
                peak = juce::jmax (peak, std::abs (output.getSample (0, i)));
            expect (peak > 0.01f, "EQ-only with default passthrough targets should produce non-silent output");
        }

        beginTest ("additive behavior: processInto adds to existing output content");
        {
            tw::IndyFxChain chain;
            chain.prepare (sr, blockSize);

            juce::AudioBuffer<float> input (2, blockSize);
            juce::AudioBuffer<float> output (2, blockSize);
            for (int i = 0; i < blockSize; ++i)
            {
                input.setSample (0, i, 0.25f);
                input.setSample (1, i, 0.25f);
                output.setSample (0, i, 0.1f);
                output.setSample (1, i, 0.1f);
            }

            const std::uint8_t empty = tw::fxmask::packMask (false, 0, false, false, false, false);
            chain.setMask (empty);
            chain.processInto (input, output, blockSize);

            // Output should be 0.25 (input) + 0.1 (prior) = 0.35
            for (int i = 0; i < blockSize; ++i)
            {
                expectWithinAbsoluteError (output.getSample (0, i), 0.35f, 1e-6f);
                expectWithinAbsoluteError (output.getSample (1, i), 0.35f, 1e-6f);
            }
        }
    }
};

static IndyFxChainTests indyFxChainTests;
