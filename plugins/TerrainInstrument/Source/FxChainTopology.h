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
// ── fb375 — THE SECOND DEVICE CLASS: PURE-FX (whole-mix) devices ─────────────
// Max, 2026-08-16, holding a Serum 2 screenshot: "stuff like the fx filter,
// splitter, and the utility need to fall back on the whole FX MIX. it's all
// about the chains for these 3, not the per osc routing... filter is first but
// it still takes the sound and puts it thru to the delay, simple as that."
//
// So the rack has TWO device classes, and the analogy is Serum's own racks:
//   • PER-OSC devices (reverb/delay/distortion/granular/tape) = Serum's BUS 1/2.
//     They carry route pills and everything above still applies to them.
//   • PURE-FX devices (filter/splitter/utility) = Serum's MAIN. They have NO
//     route pills at all. At their chain position they are an insert on the
//     WHOLE mix: they tap every source that has not yet entered the rack (the
//     never-routed dry) AND eat every upstream output still looking for a home.
//     Their output carries all six sources, so by the ordinary feed rule below
//     every device beneath them is fed from them — which is exactly "filter
//     first, then the delay".
//
// 🔑 THE CONSEQUENCE, stated once so nobody files it as a bug: a whole-mix
// device MERGES the rack. Below one, the sources are summed and cannot be
// separated again (the same physics as the per-osc merge above, applied to all
// six at once), so a per-osc device placed under a Filter taps nothing new —
// its `entry` is 0 and its pills are inert. The UI greys them from this same
// model rather than inventing a second rule.
//
// PURE C++ (no JUCE) so it offline-validates standalone, same contract as
// DelayEngine.h. Bit s of a mask = source s (0=A 1=B 2=C 3=D 4=Sub 5=Noise).
// Proof harness: `Tests/fxtopo_test.cpp` — committed, not scratchpad-only.
// ─────────────────────────────────────────────────────────────────────────────
#include <cstdint>

namespace tw {

struct FxChainTopology
{
    static constexpr int kMaxSlots    = 44;   // fb375 — + 6 filter (fb365 had 38: reverb+delay+distortion+granular+tape)
    static constexpr uint8_t kAllSrc  = 0x3F; // all six sources: A B C D Sub Noise

    int      count = 0;
    uint8_t  entry    [kMaxSlots] = {};       // sources that TAP the oscillators at this slot
    uint32_t feed     [kMaxSlots] = {};       // bitmask of upstream slots whose output this slot eats
    bool     consumed [kMaxSlots] = {};       // this slot's output is eaten downstream ⇒ NOT summed to the main mix
    uint8_t  eff      [kMaxSlots] = {};       // sources this slot's output actually carries (its own + everything it ate)

    // masks[] must be in CHAIN ORDER (already sorted by _RANK).
    // wholeMix[] (fb375, optional) flags the PURE-FX devices — filter/splitter/utility. They have no
    // route pills, so their masks[] entry is always 0 and this array is the only thing that marks
    // them. Passing nullptr reproduces the fb351 behaviour bit-for-bit (that is a test row).
    void build (const uint8_t* masks, int n, const bool* wholeMix = nullptr) noexcept
    {
        count = n < kMaxSlots ? (n < 0 ? 0 : n) : kMaxSlots;
        for (int c = 0; c < count; ++c) { entry[c] = 0; feed[c] = 0; consumed[c] = false; eff[c] = 0; }

        uint8_t claimed = 0;                  // sources that have already entered the rack
        for (int c = 0; c < count; ++c)
        {
            if (wholeMix != nullptr && wholeMix[c])
            {
                // fb375 — a PURE-FX insert on the whole mix. It takes the never-routed dry (every
                // source not yet in the rack) AND eats every upstream output still unclaimed, so
                // everything above it merges here. Its output carries all six sources, which is what
                // makes the ordinary feed rule below hand it straight to the next device.
                entry[c] = (uint8_t) (kAllSrc & ~claimed);
                claimed  = kAllSrc;
                for (int j = 0; j < c; ++j)
                    if (! consumed[j]) { feed[c] |= (1u << (unsigned) j); consumed[j] = true; }
                eff[c] = kAllSrc;
                continue;
            }

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
