// WavetableBank.h — owns all wavetables indexed by enum.
// SynthVoices hold a `const Wavetable*` into the bank's storage; the bank
// outlives all voices.
//
// fb496 — LAZY PER TABLE. The constructor used to build all 30 factory tables
// eagerly. MEASURED (Tests harness, M2 Max): 128.38 MB and 81.4 ms, per plugin
// instance, of which a patch can only ever SOUND four (one per osc) — and the
// bank is a plain member, so four Terrains paid it four times over.
// One table costs 4.36 MB / 2.5 ms, so the four a patch actually uses are
// ~17 MB: a ~111 MB saving per instance with the identical audio.
//
// 🚨 WHY THE BANK IS NOT A SHARED GLOBAL INSTEAD. It is NOT immutable after
// construction: PluginProcessor::wavetableForBlurTwin() const_casts a bank
// table and the message thread WRITES its blur twin into it (fb469). A static
// shared across instances would put two processors' message-thread work on one
// object — the exact class of bug the fb444/setDistortionTableSrc comment was
// left behind for. Lazy-per-table gets most of the memory with none of that.
//
// 🔑 THE BUILD IS MESSAGE-THREAD ONLY; getTable() NEVER BUILDS. A table costs
// 2.5 ms and an allocation, which is a dropout on the audio thread. So:
//   · ensureBuilt()  — message thread, idempotent, mutex-guarded, builds.
//   · getTable()     — any thread, wait-free: one acquire load. If the table
//                      is not built yet it hands back Sine (always built in the
//                      ctor) rather than a null or a half-built table.
// The processor prefetches the four osc presets from prepareToPlay and
// setStateInformation (synchronously — neither is realtime) and tops up one per
// 60 Hz timer tick, so the audio thread finding an unbuilt table is confined to
// the <=16.7 ms after a LIVE preset change; wavetableForOsc() covers even that
// by holding the osc's previous table instead of the Sine fallback.
#pragma once

#include "Wavetable.h"
#include <array>
#include <atomic>
#include <mutex>

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
            // fb496 — the 30 buildFromSpec() calls that used to live here (81.4 ms, 128.38 MB)
            // are gone; the per-preset maker mapping they carried is specForPreset() below,
            // which was already declared the single source of truth for it and validated to
            // round-trip every preset to its bank table. buildOne() goes through THAT, so a
            // lazily built table is the byte-identical table the ctor used to build.
            //
            // Sine is the one exception: it is built eagerly because it is both preset 0 (the
            // out-of-range answer getTable() has always given) and the fallback an audio thread
            // gets if it beats the message thread to a table. 4.36 MB, 2.5 ms.
            for (auto& b : built_) b.store (false, std::memory_order_relaxed);   // belt-and-braces
            buildOne (Sine);
        }

        /** Build `preset` if it is not built yet. MESSAGE THREAD ONLY — 2.5 ms and an
            allocation per table. Idempotent and safe to call every tick. The mutex is
            never taken by the audio thread (getTable does one atomic load), so it cannot
            invert priority against the RT path. */
        void ensureBuilt (int preset)
        {
            if (preset < 0 || preset >= kNumPresets) return;
            if (built_[(size_t) preset].load (std::memory_order_acquire)) return;
            const std::lock_guard<std::mutex> lock (buildLock_);
            buildOne (preset);
        }

        /** True once `preset` has been built. Wait-free — used to decide whether a prefetch
            still owes work, and by the audio thread to spot a table it must not touch yet. */
        bool isBuilt (int preset) const noexcept
        {
            if (preset < 0 || preset >= kNumPresets) return false;
            return built_[(size_t) preset].load (std::memory_order_acquire);
        }

        /** The table if it is built, else nullptr. Wait-free; callable from the audio thread.
            Callers that can hold a previous table (wavetableForOsc) prefer this over getTable
            so a live preset change never drops to Sine. */
        const Wavetable* getTableIfBuilt (int preset) const noexcept
        {
            if (preset < 0 || preset >= kNumPresets) return nullptr;
            if (! built_[(size_t) preset].load (std::memory_order_acquire)) return nullptr;
            return &tables_[(size_t) preset];
        }

        /** Wait-free, any thread, NEVER builds and NEVER returns null — the contract every
            existing caller was written against. An unbuilt table answers Sine (preset 0),
            which is exactly what an out-of-range preset has always answered. */
        const Wavetable* getTable (int preset) const noexcept
        {
            if (preset < 0 || preset >= kNumPresets) return &tables_[0];
            if (! built_[(size_t) preset].load (std::memory_order_acquire)) return &tables_[0];
            return &tables_[(size_t) preset];
        }

        // ── Spectral Morph support ───────────────────────────────────────────
        // Returns the BASE spectrum (pre-morph) for a preset, so the processor can
        // run SpectralMorph::apply(...) on it and buildFromSpec a morphed table.
        // Single source of truth: this MUST mirror the constructor's maker mapping
        // (validated offline — every preset round-trips to its bank table).
        static WavetableSpec specForPreset (int preset) noexcept
        {
            switch (preset)
            {
                case Sine:           return Wavetable::makeSineSpec();
                case Triangle:       return Wavetable::makeTriangleSpec();
                case Square:         return Wavetable::makeSquareSpec();
                case Pulse:          return Wavetable::makePulseSpec();
                case ProphetSaw:     return Wavetable::makeProphetSawSpec();
                case JupiterPWM:     return Wavetable::makeJupiterPWMSpec();
                case MoogSqr:        return Wavetable::makeMoogSqrSpec();
                case OBXSaw:         return Wavetable::makeOBXSawSpec();
                case CS80Brass:      return Wavetable::makeCS80BrassSpec();
                case JunoStr:        return Wavetable::makeJunoStrSpec();
                case PPGWave:        return Wavetable::makePPGWaveSpec();
                case DX7EP:          return Wavetable::makeDX7EPSpec();
                case D50Bell:        return Wavetable::makeD50BellSpec();
                case M1Piano:        return Wavetable::makeM1PianoSpec();
                case ChoirAtoO:      return Wavetable::makeChoirAtoOSpec();
                case Whisper:        return Wavetable::makeWhisperSpec();
                case VowelMorph:     return Wavetable::makeVowelMorphSpec();
                case BowedMetal:     return Wavetable::makeBowedMetalSpec();
                case GlassHarmonics: return Wavetable::makeGlassHarmonicsSpec();
                case Railroad:       return Wavetable::makeRailroadSpec();
                case Dustbowl:       return Wavetable::makeDustbowlSpec();
                case StaticEvolve:   return Wavetable::makeStaticEvolveSpec();
                case SpectralDrift:  return Wavetable::makeSpectralDriftSpec();
                case SerumHD:        return Wavetable::makeSerumHDSpec();
                case Rise:           return Wavetable::makeHarmonicRiseSpec();
                case OddEven:        return Wavetable::makeOddEvenSpec();
                case PhaseDrift:     return Wavetable::makePhaseDriftSpec();
                case SpectralSweep:  return Wavetable::makeSpectralSweepSpec();
                case FormantRise:    return Wavetable::makeFormantRiseSpec();
                case HarmonicSeries: return Wavetable::makeHarmonicSeriesSpec();
                default:             return Wavetable::makeSineSpec();
            }
        }

    private:
        /** The actual build. Caller holds buildLock_ (or is the ctor, pre-publication).
            🔑 built_ is stored with RELEASE and read with ACQUIRE, and it is stored LAST —
            an audio thread that sees `true` is guaranteed to see the finished table behind
            it. Nothing ever un-builds a table, so the pointer can never go stale. */
        void buildOne (int preset)
        {
            const size_t i = (size_t) preset;
            if (built_[i].load (std::memory_order_relaxed)) return;   // re-check under the lock
            tables_[i].buildFromSpec (specForPreset (preset));
            built_[i].store (true, std::memory_order_release);        // publish LAST
        }

        std::array<Wavetable, kNumPresets>       tables_;
        std::array<std::atomic<bool>, kNumPresets> built_ {};   // value-init → all false
        std::mutex                               buildLock_;
    };
}
