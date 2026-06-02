// WavetableBank.h — owns the 6 Phase 2A analog wavetables, indexed by enum.
// Constructed once at PluginProcessor startup (heavy — generates ~750KB of
// data on the constructor call). SynthVoices hold a `const Wavetable*` into
// the bank's storage; the bank outlives all voices.
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
            ProphetSaw = 0,
            JupiterPWM,
            MoogSqr,
            OBXSaw,
            CS80Brass,
            JunoStr,
            kNumPresets
        };

        WavetableBank()
        {
            tables_[(size_t) ProphetSaw] = Wavetable::makeProphetSaw();
            tables_[(size_t) JupiterPWM] = Wavetable::makeJupiterPWM();
            tables_[(size_t) MoogSqr]    = Wavetable::makeMoogSqr();
            tables_[(size_t) OBXSaw]     = Wavetable::makeOBXSaw();
            tables_[(size_t) CS80Brass]  = Wavetable::makeCS80Brass();
            tables_[(size_t) JunoStr]    = Wavetable::makeJunoStr();
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
