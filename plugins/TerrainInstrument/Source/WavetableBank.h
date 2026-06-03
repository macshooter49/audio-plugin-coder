// WavetableBank.h — owns all wavetables indexed by enum.
// Constructed once at PluginProcessor startup (heavy — generates ~750KB+
// of data on the constructor call). SynthVoices hold a `const Wavetable*`
// into the bank's storage; the bank outlives all voices.
#pragma once

#include "Wavetable.h"
#include <array>

namespace tw
{
    class WavetableBank
    {
    public:
        // Order MUST match the SYN_OSC_A_WT_PRESET StringArray in createParameterLayout.
        enum Preset
        {
            // Basic (Phase 9 polish — fundamental waveforms at the top)
            Sine = 0,
            Triangle,
            Square,
            Pulse,
            // Analog (Phase 2A)
            ProphetSaw,
            JupiterPWM,
            MoogSqr,
            OBXSaw,
            CS80Brass,
            JunoStr,
            // Digital (Phase 2B)
            PPGWave,
            DX7EP,
            D50Bell,
            M1Piano,
            // Vocal (Phase 2B)
            ChoirAtoO,
            Whisper,
            VowelMorph,
            // Metallic (Phase 2B)
            BowedMetal,
            GlassHarmonics,
            Railroad,
            // Experimental (Phase 2B)
            Dustbowl,
            StaticEvolve,
            SpectralDrift,
            SerumHD,
            kNumPresets
        };

        WavetableBank()
        {
            tables_[(size_t) Sine]          .buildFromSpec (Wavetable::makeSineSpec());
            tables_[(size_t) Triangle]      .buildFromSpec (Wavetable::makeTriangleSpec());
            tables_[(size_t) Square]        .buildFromSpec (Wavetable::makeSquareSpec());
            tables_[(size_t) Pulse]         .buildFromSpec (Wavetable::makePulseSpec());
            tables_[(size_t) ProphetSaw]  .buildFromSpec (Wavetable::makeProphetSawSpec());
            tables_[(size_t) JupiterPWM]     = Wavetable::makeJupiterPWM();
            tables_[(size_t) MoogSqr]        = Wavetable::makeMoogSqr();
            tables_[(size_t) OBXSaw]      .buildFromSpec (Wavetable::makeOBXSawSpec());
            tables_[(size_t) CS80Brass]      = Wavetable::makeCS80Brass();
            tables_[(size_t) JunoStr]     .buildFromSpec (Wavetable::makeJunoStrSpec());
            tables_[(size_t) PPGWave]        = Wavetable::makePPGWave();
            tables_[(size_t) DX7EP]          = Wavetable::makeDX7EP();
            tables_[(size_t) D50Bell]        = Wavetable::makeD50Bell();
            tables_[(size_t) M1Piano]        = Wavetable::makeM1Piano();
            tables_[(size_t) ChoirAtoO]      = Wavetable::makeChoirAtoO();
            tables_[(size_t) Whisper]        = Wavetable::makeWhisper();
            tables_[(size_t) VowelMorph]     = Wavetable::makeVowelMorph();
            tables_[(size_t) BowedMetal]     = Wavetable::makeBowedMetal();
            tables_[(size_t) GlassHarmonics] = Wavetable::makeGlassHarmonics();
            tables_[(size_t) Railroad]       = Wavetable::makeRailroad();
            tables_[(size_t) Dustbowl]       = Wavetable::makeDustbowl();
            tables_[(size_t) StaticEvolve]   = Wavetable::makeStaticEvolve();
            tables_[(size_t) SpectralDrift]  = Wavetable::makeSpectralDrift();
            tables_[(size_t) SerumHD]        = Wavetable::makeSerumHD();
        }

        const Wavetable* getTable (int preset) const noexcept
        {
            if (preset < 0 || preset >= kNumPresets) return &tables_[0];
            return &tables_[(size_t) preset];
        }

    private:
        std::array<Wavetable, kNumPresets> tables_;
    };
}
