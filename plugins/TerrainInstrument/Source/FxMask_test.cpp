// FxMask_test.cpp
// JUCE UnitTest for FX-mask packing helpers (option 1).
#include "FxMask.h"
#include <juce_core/juce_core.h>

class FxMaskTests : public juce::UnitTest
{
public:
    FxMaskTests() : juce::UnitTest ("FxMask", "TerrainInstrument") {}

    void runTest() override
    {
        beginTest ("packMask — all off");
        {
            const auto m = tw::fxmask::packMask (false, 0, false, false, false, false);
            expectEquals ((int) m, 0, "all-off mask should be 0");
            expect (tw::fxmask::isEmpty (m), "isEmpty should be true for 0");
        }

        beginTest ("packMask — GRAIN occupies bit 0");
        {
            const auto m = tw::fxmask::packMask (true, 0, false, false, false, false);
            expectEquals ((int) m, 0b0000001);
            expect (! tw::fxmask::isEmpty (m), "non-zero mask is not empty");
        }

        beginTest ("packMask — TAPE machine field is bits 1..2");
        {
            expectEquals ((int) tw::fxmask::packMask (false, 1, false, false, false, false), 0b0000010, "Studio = 01 in bits 1..2 = value 2");
            expectEquals ((int) tw::fxmask::packMask (false, 2, false, false, false, false), 0b0000100, "Cassette = 10 in bits 1..2 = value 4");
            expectEquals ((int) tw::fxmask::packMask (false, 3, false, false, false, false), 0b0000110, "Wire = 11 in bits 1..2 = value 6");
        }

        beginTest ("packMask — SPACE/DELAY/EQ/JUNE bit positions");
        {
            expectEquals ((int) tw::fxmask::packMask (false, 0, true,  false, false, false), 0b0001000);
            expectEquals ((int) tw::fxmask::packMask (false, 0, false, true,  false, false), 0b0010000);
            expectEquals ((int) tw::fxmask::packMask (false, 0, false, false, true,  false), 0b0100000);
            expectEquals ((int) tw::fxmask::packMask (false, 0, false, false, false, true),  0b1000000);
        }

        beginTest ("packMask — all on with Studio tape");
        {
            const auto m = tw::fxmask::packMask (true, 1, true, true, true, true);
            expectEquals ((int) m, 0b1111011);
        }

        beginTest ("isEmpty — bit 7 (reserved) is ignored");
        {
            // Synthesize a mask with only bit 7 set (would never happen via packMask,
            // but the function shouldn't be tricked by reserved-bit garbage)
            const std::uint8_t weird = 0b10000000;
            expect (tw::fxmask::isEmpty (weird), "bit 7 is reserved; isEmpty checks bits 0..6 only");
        }

        beginTest ("union of two masks via OR");
        {
            const auto a = tw::fxmask::packMask (true, 0, false, false, false, false);  // GRAIN
            const auto b = tw::fxmask::packMask (false, 0, true, false, false, false);  // SPACE
            const std::uint8_t unioned = (std::uint8_t) (a | b);
            expectEquals ((int) unioned, 0b0001001, "GRAIN | SPACE = bits 0 + 3");
            expect (! tw::fxmask::isEmpty (unioned));
        }
    }
};

static FxMaskTests fxMaskTests;
