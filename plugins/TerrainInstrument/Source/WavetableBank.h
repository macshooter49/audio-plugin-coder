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
            // Morph (Phase 11h) — designed for dramatic WT POS sweeps
            Rise,
            OddEven,
            PhaseDrift,
            SpectralSweep,
            FormantRise,
            HarmonicSeries,
            kNumPresets
        };

        WavetableBank()
        {
            // Phase 10a — basic shapes (spec-based, full 8 mip levels)
            tables_[(size_t) Sine]          .buildFromSpec (Wavetable::makeSineSpec());
            tables_[(size_t) Triangle]      .buildFromSpec (Wavetable::makeTriangleSpec());
            tables_[(size_t) Square]        .buildFromSpec (Wavetable::makeSquareSpec());
            tables_[(size_t) Pulse]         .buildFromSpec (Wavetable::makePulseSpec());
            // Phase 10a — migrated analog saws (spec-based, full 8 mip levels)
            tables_[(size_t) ProphetSaw]  .buildFromSpec (Wavetable::makeProphetSawSpec());
            // Phase 11k — Analog legacy tables: amplify for dramatic WT POS sweep
            tables_[(size_t) JupiterPWM]     = Wavetable::makeJupiterPWM();
            tables_[(size_t) JupiterPWM].amplifyFramesInPlace (Wavetable::AmplifyMode::Spectrum);
            tables_[(size_t) MoogSqr]        = Wavetable::makeMoogSqr();
            tables_[(size_t) MoogSqr].amplifyFramesInPlace (Wavetable::AmplifyMode::Warmth);
            tables_[(size_t) OBXSaw]      .buildFromSpec (Wavetable::makeOBXSawSpec());
            tables_[(size_t) CS80Brass]      = Wavetable::makeCS80Brass();
            tables_[(size_t) CS80Brass].amplifyFramesInPlace (Wavetable::AmplifyMode::Brightness);
            tables_[(size_t) JunoStr]     .buildFromSpec (Wavetable::makeJunoStrSpec());
            // Phase 11k — Legacy time-domain wavetables: amplifyFramesInPlace for dramatic WT POS.
            // These are mip-0-only — high notes will alias until Phase 10c migrates them.
            tables_[(size_t) PPGWave]        = Wavetable::makePPGWave();
            tables_[(size_t) PPGWave].amplifyFramesInPlace (Wavetable::AmplifyMode::Drive);
            tables_[(size_t) DX7EP]          = Wavetable::makeDX7EP();
            tables_[(size_t) DX7EP].amplifyFramesInPlace (Wavetable::AmplifyMode::Brightness);
            tables_[(size_t) D50Bell]        = Wavetable::makeD50Bell();
            tables_[(size_t) D50Bell].amplifyFramesInPlace (Wavetable::AmplifyMode::Spectrum);
            tables_[(size_t) M1Piano]        = Wavetable::makeM1Piano();
            tables_[(size_t) M1Piano].amplifyFramesInPlace (Wavetable::AmplifyMode::Brightness);
            tables_[(size_t) ChoirAtoO]      = Wavetable::makeChoirAtoO();
            tables_[(size_t) ChoirAtoO].amplifyFramesInPlace (Wavetable::AmplifyMode::Warmth);
            tables_[(size_t) Whisper]        = Wavetable::makeWhisper();
            tables_[(size_t) Whisper].amplifyFramesInPlace (Wavetable::AmplifyMode::Drive);
            tables_[(size_t) VowelMorph]     = Wavetable::makeVowelMorph();
            tables_[(size_t) VowelMorph].amplifyFramesInPlace (Wavetable::AmplifyMode::Spectrum);
            tables_[(size_t) BowedMetal]     = Wavetable::makeBowedMetal();
            tables_[(size_t) BowedMetal].amplifyFramesInPlace (Wavetable::AmplifyMode::Drive);
            tables_[(size_t) GlassHarmonics] = Wavetable::makeGlassHarmonics();
            tables_[(size_t) GlassHarmonics].amplifyFramesInPlace (Wavetable::AmplifyMode::Brightness);
            tables_[(size_t) Railroad]       = Wavetable::makeRailroad();
            tables_[(size_t) Railroad].amplifyFramesInPlace (Wavetable::AmplifyMode::Drive);
            tables_[(size_t) Dustbowl]       = Wavetable::makeDustbowl();
            tables_[(size_t) Dustbowl].amplifyFramesInPlace (Wavetable::AmplifyMode::Drive);
            tables_[(size_t) StaticEvolve]   = Wavetable::makeStaticEvolve();
            tables_[(size_t) StaticEvolve].amplifyFramesInPlace (Wavetable::AmplifyMode::Spectrum);
            tables_[(size_t) SpectralDrift]  = Wavetable::makeSpectralDrift();
            tables_[(size_t) SpectralDrift].amplifyFramesInPlace (Wavetable::AmplifyMode::Spectrum);
            tables_[(size_t) SerumHD]        = Wavetable::makeSerumHD();
            tables_[(size_t) SerumHD].amplifyFramesInPlace (Wavetable::AmplifyMode::Brightness);
            // Phase 11h — Morph category (spec-based, full 8 mip levels)
            tables_[(size_t) Rise]            .buildFromSpec (Wavetable::makeHarmonicRiseSpec());
            tables_[(size_t) OddEven]         .buildFromSpec (Wavetable::makeOddEvenSpec());
            tables_[(size_t) PhaseDrift]      .buildFromSpec (Wavetable::makePhaseDriftSpec());
            tables_[(size_t) SpectralSweep]   .buildFromSpec (Wavetable::makeSpectralSweepSpec());
            tables_[(size_t) FormantRise]     .buildFromSpec (Wavetable::makeFormantRiseSpec());
            tables_[(size_t) HarmonicSeries]  .buildFromSpec (Wavetable::makeHarmonicSeriesSpec());
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
