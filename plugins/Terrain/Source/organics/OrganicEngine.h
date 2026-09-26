// OrganicEngine.h — THE ORGANICS ENGINE runtime (the class itself is declared in the frozen OrganicsApi.h;
// OrganicEngine.cpp implements OrganicEngine::Impl). The integration includes OrganicsApi.h only.
//
// HOW THE VOICE DRIVES IT (one OrganicEngine per oscillator slot per voice; players live inside):
//   message thread : OrganicsLibrary::get().request (id, cb) → cb(shared_ptr) → hand it to the audio thread.
//   audio thread   : engine.setInstrument (ptr)            — when the osc's instrument changes (also nullptr
//                                                             when the osc leaves Organic, so the cache can free)
//                    engine.noteOn (note, vel01, players, detuneCents /*already ×0.25*/, seed)
//                    engine.noteOff (pedalHeld) / engine.pedal (down) / engine.kill()
//                    engine.render (params, pitchCents, L, R, n)   — ADDS into L/R; call every block while
//                                                             isActive() (the engine is 0 µs when idle)
//   pitchCents = everything the voice applies relative to the PLAYED note (oct·1200 + semi·100 + fine +
//   coarse-mod·100 + bend + glide + MPE). The engine owns sample-root → played-note repitch, Body, Human
//   detune and the per-player unison detuneCents.
//   Region selection happens at the first render() after noteOn (it needs the params: artic, Dynamics,
//   Body, Attack, Human). Events land at the start of the next rendered block.
#pragma once

#include "OrganicsApi.h"

namespace tw
{
    namespace organics_debug
    {
        /** Readers (sample regions) rendered by the most recent render() of ANY engine (tests only). */
        int lastRenderReaders() noexcept;
        /** Region index of the loudest attack region chosen by the most recent note start (tests only). */
        int lastNoteRegion() noexcept;
        /** Non-fading readers after the most recent render() (the 48-region cap applies to these). */
        int lastLiveReaders() noexcept;
        /** Total steal-fades started (process-wide counter). */
        int steals() noexcept;
        /** tp105b: total note-offs that took the TAIL release (the requested release outlived the recording). */
        int tailReleases() noexcept;
        /** tp107 round-robin noise: every note-on / note-off noise DECISION (a knob > 0 and an instrument with noise for that
            event), the ones that sounded, and the last sounding event's variant index (−1 = the cell has no variants), level
            offset (dB) and start delay (frames). Process-wide counters (tests only). */
        int   noiseDecisions() noexcept;
        int   noiseHits() noexcept;
        int   lastNoiseVariant() noexcept;
        int   lastNoiseVariantCount() noexcept;
        float lastNoiseDb() noexcept;
        int   lastNoiseDelay() noexcept;
        /** tp108 Tone on pure tones — the most recent engine that ran a Tone stage: the tilt's predicted swing
            (centroid(+1)/centroid(−1), and the same in f_rms), the note's spectral flatness, the note's SPARSE score (0 = the tilt is enough … 1 = a sine), the dominant frequency (Hz),
            the exciter depth and the low-pass depth now, the stages that ran, and the exciter's tonality gate (0..1) with the residual
            it came from (in units of a tone 5 % off f_dom) (1 exciter · 2 low-pass · 4 measuring).
            Process-wide (tests only). */
        struct ToneState { float swing, swingRms, flat, sparse, fDom, exc, lp; int stages; float gate, rho; };
        ToneState lastTone() noexcept;
        /** tp108: false = the Tone stages never run (the tp107 tilt alone) — the CPU A/B and the null (tests only). */
        void setToneStages (bool on) noexcept;
        /** tp114 the safety limiter (organics::kLimiterCeilingDb): false = bypassed = the tp113 engine exactly (the before/after
            A/B, the library tools that measure the RECORDINGS: peaktrim / calibration). Process-wide (tests only). */
        void setLimiter (bool on) noexcept;
        /** Since the last reset: the deepest gain reduction (dB, ≥ 0), the samples rendered under unity gain, the last of them
            (samples since that engine's most recent noteOn), and the limiting chunks (process-wide, tests only). */
        struct LimiterStats { float maxGrDb; int64_t limitedSamples; int64_t lastLimitedAt; int64_t chunks; };
        LimiterStats limiterStats() noexcept;
        void resetLimiterStats() noexcept;
    }
}
