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
            // ── Phase 11l — 30 total spec-based wavetables ────────────────────
            //
            // Basic (4): 4 fundamental waveforms (Phase 10a, unchanged)
            tables_[(size_t) Sine]          .buildFromSpec (Wavetable::makeSineSpec());
            tables_[(size_t) Triangle]      .buildFromSpec (Wavetable::makeTriangleSpec());
            tables_[(size_t) Square]        .buildFromSpec (Wavetable::makeSquareSpec());
            tables_[(size_t) Pulse]         .buildFromSpec (Wavetable::makePulseSpec());

            // Analog (6): Phase 11l research-driven — each circuit-grounded and distinct
            // ProphetSaw: SSM 2030 even-harmonic boost + soft taper (vintage→modern bright)
            tables_[(size_t) ProphetSaw]    .buildFromSpec (Wavetable::makeProphetSawSpec());
            // JupiterPWM: Pure pulse-wave formula with authentic harmonic nulls (hollow→narrow)
            tables_[(size_t) JupiterPWM]    .buildFromSpec (Wavetable::makeJupiterPWMSpec());
            // MoogSqr: Near-square DC-leakage + even harmonics grow quadratically (square→fat saw)
            tables_[(size_t) MoogSqr]       .buildFromSpec (Wavetable::makeMoogSqrSpec());
            // OBXSaw: Gaussian rolloff above h=22 + serial VCA distortion on h=2,3 (clean→gritty)
            tables_[(size_t) OBXSaw]        .buildFromSpec (Wavetable::makeOBXSawSpec());
            // CS80Brass: Bandpass formant (HPF reduces h=1,2; bell-curve at h=4-7; dual-VCO scatter)
            tables_[(size_t) CS80Brass]     .buildFromSpec (Wavetable::makeCS80BrassSpec());
            // JunoStr: Zero scatter at frame 0 (DCO stability), sub-osc grows, chorus scatter grows
            tables_[(size_t) JunoStr]       .buildFromSpec (Wavetable::makeJunoStrSpec());

            // Digital (4): Phase 11l research-driven
            // PPGWave: Gaussian peak h=1.5→20 + 8-bit grit floor (spec-based, buildFromSpec)
            tables_[(size_t) PPGWave]       .buildFromSpec (Wavetable::makePPGWaveSpec());
            // DX7EP: FM β=0.5→4.5 with 3.5× tine modulator (legacy — non-integer partials)
            tables_[(size_t) DX7EP]          = Wavetable::makeDX7EP();
            // D50Bell: Acoustic bell decay (fundamental fast, upper partials sustain) (legacy)
            tables_[(size_t) D50Bell]        = Wavetable::makeD50Bell();
            // M1Piano: Inharmonicity B=0.00015, velocity model frame 0=soft→15=hard strike (legacy)
            tables_[(size_t) M1Piano]        = Wavetable::makeM1Piano();

            // Vocal (3): Phase 11l research-driven — Lorentzian formants (Peterson & Barney 1952)
            // ChoirAtoO: /a/→/o/ sweep, singer's formant ring, Lorentzian resonance (spec-based)
            tables_[(size_t) ChoirAtoO]     .buildFromSpec (Wavetable::makeChoirAtoOSpec());
            // Whisper: Formant-shaped noise (randomized phases = incoherent = noise-like) (spec-based)
            tables_[(size_t) Whisper]       .buildFromSpec (Wavetable::makeWhisperSpec());
            // VowelMorph: A→E→I→O→U with /i/→/o/ as most dramatic transition (spec-based)
            tables_[(size_t) VowelMorph]    .buildFromSpec (Wavetable::makeVowelMorphSpec());

            // Metallic (3): Phase 11l research-driven — physics-based partial ratios
            // BowedMetal: Vibraphone h=1:4:10 exact integers, bow-lift sweep (spec-based)
            tables_[(size_t) BowedMetal]    .buildFromSpec (Wavetable::makeBowedMetalSpec());
            // GlassHarmonics: Free-free bowl modes, h=3 for 2.756× (537¢ error — pending 10c)
            tables_[(size_t) GlassHarmonics].buildFromSpec (Wavetable::makeGlassHarmonicsSpec());
            // Railroad: Euler-Bernoulli decay sweep, h=3 for 2.756× (537¢ error — pending 10c)
            tables_[(size_t) Railroad]      .buildFromSpec (Wavetable::makeRailroadSpec());

            // Experimental (4): Phase 11l research-driven — all spec-based, all distinct process
            // Dustbowl: 78rpm degradation (LP cutoff + mid-band shellac noise + varispeed jitter)
            tables_[(size_t) Dustbowl]      .buildFromSpec (Wavetable::makeDustbowlSpec());
            // StaticEvolve: Static → choir /a/ formant narrative (noise→tone with phase collapse)
            tables_[(size_t) StaticEvolve]  .buildFromSpec (Wavetable::makeStaticEvolveSpec());
            // SpectralDrift: IDENTICAL amplitude spectrum, phases drift 0→random (psychoacoustic)
            tables_[(size_t) SpectralDrift] .buildFromSpec (Wavetable::makeSpectralDriftSpec());
            // SerumHD: Gaussian center h=3→45 with t^1.5, 20% even boost (brightest in bank)
            tables_[(size_t) SerumHD]       .buildFromSpec (Wavetable::makeSerumHDSpec());

            // Morph (6): Phase 11h/11j — unchanged
            tables_[(size_t) Rise]          .buildFromSpec (Wavetable::makeHarmonicRiseSpec());
            tables_[(size_t) OddEven]       .buildFromSpec (Wavetable::makeOddEvenSpec());
            tables_[(size_t) PhaseDrift]    .buildFromSpec (Wavetable::makePhaseDriftSpec());
            tables_[(size_t) SpectralSweep] .buildFromSpec (Wavetable::makeSpectralSweepSpec());
            tables_[(size_t) FormantRise]   .buildFromSpec (Wavetable::makeFormantRiseSpec());
            tables_[(size_t) HarmonicSeries].buildFromSpec (Wavetable::makeHarmonicSeriesSpec());
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
