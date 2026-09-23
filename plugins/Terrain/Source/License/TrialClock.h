// =============================================================================
//  Terrain — Licensing SKELETON
//  TrialClock.h — 14-day demo window with monotonic, anti-rollback accounting.
// -----------------------------------------------------------------------------
//  This is the one piece implemented as REAL logic (not a stub) because it is
//  pure, deterministic and testable: given an observed wall-clock time it decides
//  how much trial remains. It NEVER lets remaining time go negative and NEVER lets
//  it reset by moving the system clock backwards. I/O and persistence are someone
//  else's job (see LicenseStore.h) — this class only transforms TrialState.
//
//  Anti-tamper model (honest, see design doc):
//    * We keep a monotonic HIGH-WATER MARK of the largest wall-clock time ever
//      observed (lastSeenHigh). Effective "now" = max(observed, lastSeenHigh).
//    * Consumed = effectiveNow - firstSeen. Moving the clock BACK cannot reduce
//      consumed, so remaining can only shrink or hold — never grow.
//    * Moving the clock FORWARD only expires the trial sooner (fail-closed).
//    * This stops CASUAL clock rollback. It cannot stop a full reinstall / fresh
//      user account / VM reset — with only an embedded PUBLIC key the client has
//      no server secret to bind the marker to. That is an accepted tradeoff: the
//      only consequence of a reset trial is more free demo time, and the gate is
//      still fail-closed. Documented, not hidden.
// =============================================================================
#pragma once

#include "LicenseTypes.h"

#include <algorithm>
#include <cstdint>

namespace terrain::license
{
    struct TrialPolicy
    {
        std::uint32_t trialDays = 14;   // Terrain's demo length.
    };

    class TrialClock
    {
    public:
        explicit TrialClock(TrialPolicy policy = {}) : policy_(policy) {}

        static constexpr std::int64_t kSecondsPerDay = 86400;

        // Begin the trial if it has never started; otherwise fold `nowUnix` into
        // the monotonic high-water mark. Returns the updated state (caller persists).
        TrialState observe(TrialState state, UnixTime nowUnix) const
        {
            if (! state.started)
            {
                state.started      = true;
                state.trialDays    = policy_.trialDays;
                state.firstSeen    = nowUnix;
                state.lastSeenHigh = nowUnix;
                state.rollbackSeen = false;
                return state;
            }

            // Detect (and record) a backwards clock move, but do not trust it.
            if (nowUnix < state.lastSeenHigh)
                state.rollbackSeen = true;

            // Monotonic high-water: effective now can only move forward.
            state.lastSeenHigh = std::max(state.lastSeenHigh, nowUnix);

            // Guard a corrupt/edited firstSeen in the future relative to the mark.
            if (state.firstSeen > state.lastSeenHigh)
                state.firstSeen = state.lastSeenHigh;

            return state;
        }

        // Seconds consumed so far, clamped to [0, trialLength].
        std::int64_t consumedSeconds(const TrialState& s) const noexcept
        {
            if (! s.started) return 0;
            const std::int64_t consumed = s.lastSeenHigh - s.firstSeen;
            return std::clamp<std::int64_t>(consumed, 0, trialLengthSeconds(s));
        }

        // Remaining seconds, never negative.
        std::int64_t remainingSeconds(const TrialState& s) const noexcept
        {
            if (! s.started) return trialLengthSeconds(s);
            return std::max<std::int64_t>(0, trialLengthSeconds(s) - consumedSeconds(s));
        }

        // Whole days remaining (ceil), clamped to [0, trialDays] for display.
        int daysRemaining(const TrialState& s) const noexcept
        {
            const std::int64_t rem = remainingSeconds(s);
            const std::int64_t days = (rem + kSecondsPerDay - 1) / kSecondsPerDay; // ceil
            return static_cast<int>(std::clamp<std::int64_t>(days, 0, static_cast<std::int64_t>(effectiveTrialDays(s))));
        }

        bool isExpired(const TrialState& s) const noexcept
        {
            return s.started && remainingSeconds(s) <= 0;
        }

    private:
        std::uint32_t effectiveTrialDays(const TrialState& s) const noexcept
        {
            return s.trialDays != 0 ? s.trialDays : policy_.trialDays;
        }

        std::int64_t trialLengthSeconds(const TrialState& s) const noexcept
        {
            return static_cast<std::int64_t>(effectiveTrialDays(s)) * kSecondsPerDay;
        }

        TrialPolicy policy_;
    };
}
