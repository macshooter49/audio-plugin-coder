#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// FxChainTopology — fb351. Works out how signal FLOWS through the FX rack when
// every device has its own per-osc route pills AND the rack is a serial chain.
//
// THE PROBLEM IT SOLVES (Max, 2026-08-14): "when it's last in the chain, whenever
// there's delay coming through it should hit that distortion; if the distortion is
// at the beginning it should then hit that delay — it doesn't do any of that."
// fb348 gave every device its own per-osc send bus and had each one ADD its result
// into the output, which made the whole rack PARALLEL: dragging a card changed
// nothing audible, so a device read as "not even in the chain".
//
// THE MODEL (satisfies both of Max's rules at once):
//   • a source ENTERS the rack exactly once, at the FIRST device routed to it;
//   • a device's output then FLOWS to the next device that shares any of its
//     sources, instead of going straight to the main mix;
//   • devices that share no source never see each other — the "four tires" rule:
//     a delay on osc C and a distortion on osc A stay completely independent.
//
// The one consequence worth knowing: when a device spans two sources it MERGES
// them (a distortion on A+C hands one combined signal downstream), so a later
// device routed to only C picks up that whole combined signal. Once two sources
// are summed through a non-linear device they cannot be separated again — the
// alternative would be a private device instance per source.
//
// PURE C++ (no JUCE) so it offline-validates standalone, same contract as
// DelayEngine.h. Bit s of a mask = source s (0=A 1=B 2=C 3=D 4=Sub 5=Noise).
// ─────────────────────────────────────────────────────────────────────────────
#include <cstdint>

namespace tw {

struct FxChainTopology
{
    static constexpr int kMaxSlots = 16;      // 1 reverb + 6 delay + 6 distortion + headroom

    int      count = 0;
    uint8_t  entry    [kMaxSlots] = {};       // sources that TAP the oscillators at this slot
    uint32_t feed     [kMaxSlots] = {};       // bitmask of upstream slots whose output this slot eats
    bool     consumed [kMaxSlots] = {};       // this slot's output is eaten downstream ⇒ NOT summed to the main mix
    uint8_t  eff      [kMaxSlots] = {};       // sources this slot's output actually carries (its own + everything it ate)

    // masks[] must be in CHAIN ORDER (already sorted by _RANK).
    void build (const uint8_t* masks, int n) noexcept
    {
        count = n < kMaxSlots ? (n < 0 ? 0 : n) : kMaxSlots;
        for (int c = 0; c < count; ++c) { entry[c] = 0; feed[c] = 0; consumed[c] = false; eff[c] = 0; }

        uint8_t claimed = 0;                  // sources that have already entered the rack
        for (int c = 0; c < count; ++c)
        {
            entry[c]  = (uint8_t) (masks[c] & ~claimed);   // first device routed to a source taps it
            claimed  |= masks[c];
            eff[c]    = masks[c];
            // Eat every upstream output still looking for a home that shares a source with us.
            // Comparing against eff[j] (not masks[j]) is what keeps a merged signal reachable:
            // if j merged A+B and k took it for B, a later device routed to A must still find it
            // in k — otherwise that device would be fed silence.
            for (int j = 0; j < c; ++j)
                if (! consumed[j] && (uint8_t) (eff[j] & masks[c]) != 0)
                {
                    feed[c] |= (1u << (unsigned) j);
                    consumed[j] = true;
                    eff[c] = (uint8_t) (eff[c] | eff[j]);
                }
        }
    }

    // A slot is live if it taps oscillators or is fed by something upstream.
    bool hasInput (int c) const noexcept
    { return c >= 0 && c < count && (entry[c] != 0 || feed[c] != 0); }
};

} // namespace tw
