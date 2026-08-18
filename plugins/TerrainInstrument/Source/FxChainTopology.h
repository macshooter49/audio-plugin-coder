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
// ── fb376 — ONE DEVICE CLASS. THE DECISION RECORD, so it is not re-litigated ──
// fb375 briefly added a second class: "pure-FX" devices (filter/splitter/utility)
// with no route pills, which fell back on the whole mix. It was built, tested and
// then REVERTED the same night. Keeping the reasoning here because the idea is
// seductive and will occur to somebody again.
//
// It came from a Serum 2 screenshot — their rack is flat and post-sum, so a MAIN
// filter is the only filter they can have. Ours is not flat. Max: "we did all this
// work with the per-oscillator routing just for us to have our signal collapse into
// one thing and then never split up again... I don't ever want anything to collapse
// into one signal."
//
// 🔑 THE LAW THAT REPLACED IT: MERGING IS SOMETHING YOU CHOOSE, NEVER SOMETHING
// THAT HAPPENS TO YOU. A whole-mix device merges all six sources by default,
// without being asked — that is what made it wrong, not the merge itself (which
// this file has always done for any device spanning two sources, see above).
// So EVERY device that processes audio carries the same six route pills. No
// exceptions, no second class, one grammar for the whole rack — which is also the
// grammar the Terrain Patcher inherits, and it is per-oscillator by definition.
//
// The corollary that answers "I never want anything to collapse": route ONE device
// across many sources and you have asked them to fuse; use ONE DEVICE INSTANCE PER
// SOURCE (the rack holds 6 of each kind) and they never touch. Collapse is opt-in.
//
// And the reason it costs less than it sounds: a clean filter is LINEAR, so
// H(A+C) = H(A) + H(C) — merging two sources through it is mathematically identical
// to filtering them apart and summing. At low drive the merge costs no sound at all,
// only downstream separability. It only becomes audible once drive/resonance make
// the device nonlinear, which is also when you would want the intermodulation.
//
// PURE C++ (no JUCE) so it offline-validates standalone, same contract as
// DelayEngine.h. Bit s of a mask = source s (0=A 1=B 2=C 3=D 4=Sub 5=Noise).
// Proof harness: `Tests/fxtopo_test.cpp` — committed, not scratchpad-only.
// ─────────────────────────────────────────────────────────────────────────────
#include <cstdint>

namespace tw {

struct FxChainTopology
{
    // fb413 — 9 kinds x 6 instances = 54 (chorus/flanger/phaser joined at kinds 6/7/8), + headroom.
    // ⚠️ kMaxSlots MUST stay <= 64: `feed` is a per-slot bitmask over upstream SLOTS, so the mask
    // width is the slot ceiling. It was a uint32_t against kMaxSlots 44 — already, today, a chain
    // of 33+ devices would have shifted past bit 31, which is undefined behaviour, not a wrong
    // answer. 36 slots were reachable at fb377 and nobody had built a 33-device chain yet.
    static constexpr int kMaxSlots    = 56;
    static constexpr uint8_t kAllSrc  = 0x3F; // all six sources: A B C D Sub Noise

    int      count = 0;
    uint8_t  entry    [kMaxSlots] = {};       // sources that TAP the oscillators at this slot
    uint64_t feed     [kMaxSlots] = {};       // bitmask of upstream slots whose output this slot eats
    bool     consumed [kMaxSlots] = {};       // this slot's output is eaten downstream ⇒ NOT summed to the main mix
    uint8_t  eff      [kMaxSlots] = {};       // sources this slot's output actually carries (its own + everything it ate)

    // masks[] must be in CHAIN ORDER (already sorted by _RANK). Every device kind goes through
    // here — filter included (fb376). There is no second signature and no device class flag.
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
                    feed[c] |= (1ull << (unsigned) j);   // fb413 — 64-bit: j can now reach 53
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
