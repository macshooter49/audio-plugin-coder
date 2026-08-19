// ─────────────────────────────────────────────────────────────────────────────
// fxtopo_test — the proof harness for FxChainTopology.
//
// WHY THIS FILE IS IN THE REPO. Every build bible in Design/ cites perceptual
// harnesses by path ("reuse rvb_perceptual.cpp") that do not exist: they were
// written in per-session scratchpads under /private/tmp and evaporated. The
// fb351 topology shipped with "9 proven cases" that are likewise gone. This one
// is committed, so the next session inherits the proof instead of re-deriving it.
//
//   clang++ -std=c++17 -O2 -o /tmp/fxtopo_test Tests/fxtopo_test.cpp && /tmp/fxtopo_test
//
// No JUCE, no deps — FxChainTopology.h is pure C++ precisely so this can exist.
// ─────────────────────────────────────────────────────────────────────────────
#include "../Source/FxChainTopology.h"

#include <cstdio>
#include <cstdint>
#include <string>

namespace {

constexpr uint8_t A = 1 << 0, B = 1 << 1, C = 1 << 2, D = 1 << 3,
                  SUB = 1 << 4, NOISE = 1 << 5;
constexpr uint8_t ALL = tw::FxChainTopology::kAllSrc;

int  gPass = 0, gFail = 0;
std::string gCase;

void head (const char* name) { gCase = name; }

void check (bool ok, const std::string& what)
{
    if (ok) { ++gPass; return; }
    ++gFail;
    std::printf ("  FAIL  %-46s %s\n", gCase.c_str(), what.c_str());
}

std::string srcStr (uint8_t m)
{
    if (m == 0) return "-";
    std::string s;
    const char* n[6] = { "A", "B", "C", "D", "Sub", "Noise" };
    for (int i = 0; i < 6; ++i) if (m & (1u << i)) { if (! s.empty()) s += "|"; s += n[i]; }
    return s;
}

// One slot's expected outcome.
struct Want { uint8_t entry; tw::SlotMask feed; bool consumed; uint8_t eff; };   // fb420 — 128-bit, 78 slots

void expect (tw::FxChainTopology& t, int slot, Want w)
{
    char buf[192];
    if (t.entry[slot] != w.entry)
    {
        std::snprintf (buf, sizeof buf, "slot %d entry = %s, wanted %s",
                       slot, srcStr (t.entry[slot]).c_str(), srcStr (w.entry).c_str());
        check (false, buf);
    } else ++gPass;

    if (t.feed[slot] != w.feed)
    {
        std::snprintf (buf, sizeof buf, "slot %d feed = 0x%llx:%llx, wanted 0x%llx:%llx",
                       slot, (unsigned long long) t.feed[slot].w[1], (unsigned long long) t.feed[slot].w[0],
                             (unsigned long long) w.feed.w[1],       (unsigned long long) w.feed.w[0]);
        check (false, buf);
    } else ++gPass;

    if (t.consumed[slot] != w.consumed)
    {
        std::snprintf (buf, sizeof buf, "slot %d consumed = %d, wanted %d",
                       slot, (int) t.consumed[slot], (int) w.consumed);
        check (false, buf);
    } else ++gPass;

    if (t.eff[slot] != w.eff)
    {
        std::snprintf (buf, sizeof buf, "slot %d eff = %s, wanted %s",
                       slot, srcStr (t.eff[slot]).c_str(), srcStr (w.eff).c_str());
        check (false, buf);
    } else ++gPass;
}

constexpr tw::SlotMask FROM (int slot) { return tw::SlotMask::bit (slot); }   // fb420 — never open-code a shift

} // namespace

int main()
{
    tw::FxChainTopology t;

    std::printf ("\n[fb351 — PER-OSC routing: the model the serial chain shipped with]\n");

    // ── 1. one device, one source: it taps, it is heard.
    head ("1. single device on A taps A and is heard");
    { uint8_t m[] = { A }; t.build (m, 1);
      expect (t, 0, { A, 0, false, A });
      check (t.hasInput (0), "slot 0 should be live"); }

    // ── 2. a source enters ONCE: the second device is fed, it does not re-tap.
    head ("2. two devices on A — A enters once, first feeds second");
    { uint8_t m[] = { A, A }; t.build (m, 2);
      expect (t, 0, { A, 0,       true,  A });   // consumed: its output goes downstream, not to the mix
      expect (t, 1, { 0, FROM(0), false, A }); }

    // ── 3. the "four tires" rule: no shared source ⇒ they never see each other.
    head ("3. delay on C and distortion on A stay independent");
    { uint8_t m[] = { C, A }; t.build (m, 2);
      expect (t, 0, { C, 0, false, C });
      expect (t, 1, { A, 0, false, A }); }

    // ── 4. a device spanning two sources MERGES them downstream.
    head ("4. A|C device merges; a later C device eats the whole merge");
    { uint8_t m[] = { (uint8_t) (A | C), C }; t.build (m, 2);
      expect (t, 0, { (uint8_t) (A | C), 0,       true,  (uint8_t) (A | C) });
      expect (t, 1, { 0,                 FROM(0), false, (uint8_t) (A | C) }); }

    // ── 5. merged reachability — the reason feed compares eff[j], not masks[j].
    //      slot0 merges A|B, slot1 claims it via B, slot2 (routed to A only) must
    //      still find A inside slot1 or it would be fed silence.
    head ("5. merged signal stays reachable through the slot that ate it");
    { uint8_t m[] = { (uint8_t) (A | B), B, A }; t.build (m, 3);
      expect (t, 0, { (uint8_t) (A | B), 0,       true,  (uint8_t) (A | B) });
      expect (t, 1, { 0,                 FROM(0), true,  (uint8_t) (A | B) });
      expect (t, 2, { 0,                 FROM(1), false, (uint8_t) (A | B) }); }

    // ── 6. a device with no routes is dead (this is why "add a device, hear nothing" is reachable).
    head ("6. a device with no routes has no input");
    { uint8_t m[] = { 0 }; t.build (m, 1);
      expect (t, 0, { 0, 0, false, 0 });
      check (! t.hasInput (0), "slot 0 should be dead"); }

    // ── 7. order matters — the whole point of fb351.
    head ("7. reversing the chain reverses who feeds whom");
    { uint8_t m[] = { (uint8_t) (A | C), A }; t.build (m, 2);
      expect (t, 0, { (uint8_t) (A | C), 0, true, (uint8_t) (A | C) });
      uint8_t m2[] = { A, (uint8_t) (A | C) }; t.build (m2, 2);
      expect (t, 0, { A,  0,       true,  A });
      expect (t, 1, { C,  FROM(0), false, (uint8_t) (A | C) }); }

    // ── 8. one source routed to three devices is still summed only ONCE.
    head ("8. A routed to three devices taps the oscillator once");
    { uint8_t m[] = { A, A, A }; t.build (m, 3);
      expect (t, 0, { A, 0,       true,  A });
      expect (t, 1, { 0, FROM(0), true,  A });
      expect (t, 2, { 0, FROM(1), false, A }); }

    // ── 9. nothing routed anywhere ⇒ the rack is inert, the dry passes untouched.
    head ("9. an unrouted rack is completely inert");
    { uint8_t m[] = { 0, 0, 0 }; t.build (m, 3);
      for (int c = 0; c < 3; ++c) check (! t.hasInput (c), "slot should be dead"); }

    std::printf ("\n[fb376 — the FILTER is an ordinary per-osc device. No second class.]\n");

    // ── 10. Filter (chain kind 5) needs no special handling whatsoever. Routed to
    //       A it taps A, exactly like a reverb would. That is the whole point:
    //       fb375's "pure-FX" class was reverted, so there is nothing to special-case.
    head ("10. a Filter routed to A behaves like any other device");
    { uint8_t m[] = { A }; t.build (m, 1);
      expect (t, 0, { A, 0, false, A }); }

    // ── 11. MAX'S CASE, now per-osc: "filter is first but it still takes the sound
    //       and puts it thru to the delay". Both routed to A ⇒ filter feeds delay.
    head ("11. Filter first on A hands its output to a Delay on A");
    { uint8_t m[] = { A, A }; t.build (m, 2);
      expect (t, 0, { A, 0,       true,  A });
      expect (t, 1, { 0, FROM(0), false, A }); }

    // ── 12. THE OPT-IN MERGE. One Filter across A+C fuses them for everything
    //       downstream. This is allowed because the user ASKED by lighting both
    //       pills — "merging is something you choose, never something that happens
    //       to you" (the law that replaced fb375).
    head ("12. one Filter across A+C fuses them — because you asked");
    { uint8_t m[] = { (uint8_t) (A | C), C }; t.build (m, 2);
      expect (t, 0, { (uint8_t) (A | C), 0,       true,  (uint8_t) (A | C) });
      check (t.eff[1] == (uint8_t) (A | C), "downstream C device receives the fused A+C"); }

    // ── 13. THE ESCAPE HATCH, and the direct answer to "I don't ever want anything
    //       to collapse into one signal": use ONE INSTANCE PER SOURCE. Two Filters,
    //       one on A and one on C, never touch each other — no merge, ever.
    head ("13. two Filter instances (A, C) never collapse into one signal");
    { uint8_t m[] = { A, C }; t.build (m, 2);
      expect (t, 0, { A, 0, false, A });      // neither is consumed: both reach the mix
      expect (t, 1, { C, 0, false, C });      // separately, still distinct sources
      check (t.eff[0] != t.eff[1], "the two branches must carry different sources"); }

    // ── 14. and the same holds all the way down a long chain: A's devices and C's
    //       devices form two independent lines that never see each other.
    head ("14. two independent 3-device lines stay independent end to end");
    { uint8_t m[] = { A, C, A, C, A, C }; t.build (m, 6);
      check (t.entry[0] == A && t.entry[1] == C, "each source enters once, at its first device");
      for (int c = 2; c < 6; ++c) check (t.entry[c] == 0, "later devices re-tap nothing");
      check (t.eff[4] == A && t.eff[5] == C, "the two lines carry different sources at the end");
      check (! t.consumed[4] && ! t.consumed[5], "both lines reach the mix separately"); }

    // ── 15. the UI's inherit-on-add default: a device added carrying the same mask
    //       as the one above it simply extends that line (zero clicks, reads as an
    //       insert). Asserted so the default and the model can't drift apart.
    head ("15. inherit-on-add extends the line above it");
    { uint8_t m[] = { (uint8_t) (A | B) }; t.build (m, 1);
      const uint8_t inherited = t.eff[0];                 // what the UI would copy
      uint8_t m2[] = { (uint8_t) (A | B), inherited }; t.build (m2, 2);
      expect (t, 0, { (uint8_t) (A | B), 0,       true,  (uint8_t) (A | B) });
      expect (t, 1, { 0,                 FROM(0), false, (uint8_t) (A | B) });
      check (t.entry[1] == 0, "the added device re-taps nothing — it inserts"); }

    // ── 16. Sub and Noise are sources like any other; nothing about the far end of
    //       the mask is special.
    head ("16. Sub/Noise route and merge exactly like A..D");
    { uint8_t m[] = { (uint8_t) (SUB | NOISE), D, SUB }; t.build (m, 3);
      expect (t, 0, { (uint8_t) (SUB | NOISE), 0, true,  (uint8_t) (SUB | NOISE) });
      expect (t, 1, { D,                       0, false, D });
      expect (t, 2, { 0, FROM(0), false, (uint8_t) (SUB | NOISE) });
      check (t.eff[1] == D, "the D branch never sees the Sub/Noise branch"); }

    std::printf ("\n[INVARIANTS — swept exhaustively over every 3-slot mask combination]\n");

    // ── 17. The properties that must hold for EVERY chain, not just the hand-picked
    //       ones above. This is what would catch a change nobody thought to test.
    head ("17. model invariants hold for all mask combinations");
    {
        int swept = 0, tapBad = 0, feedBad = 0, liveBad = 0, effBad = 0, selfBad = 0;
        const uint8_t alphabet[] = { 0, A, C, (uint8_t) (A | C), (uint8_t) (A | B | C), ALL };
        const int NA = (int) (sizeof alphabet / sizeof alphabet[0]);

        for (int i = 0; i < NA; ++i)
        for (int j = 0; j < NA; ++j)
        for (int k = 0; k < NA; ++k)
        {
            uint8_t m[3] = { alphabet[i], alphabet[j], alphabet[k] };
            t.build (m, 3);

            // (a) NO DOUBLE-TAP: a source may enter the rack at most once, or it
            //     would be summed twice and read as +6 dB on that oscillator.
            uint8_t seen = 0;
            for (int c = 0; c < 3; ++c) { if (t.entry[c] & seen) ++tapBad; seen = (uint8_t) (seen | t.entry[c]); }

            // (b) EVERY source that is routed anywhere must enter exactly once.
            uint8_t routed = (uint8_t) (m[0] | m[1] | m[2]);
            if (seen != routed) ++tapBad;

            for (int c = 0; c < 3; ++c)
            {
                // (c) `consumed` must agree with reality in BOTH directions, or the
                //     check passes vacuously — a self-check caught exactly that: an
                //     engine that never sets `consumed` sailed through the one-way
                //     version. A slot is eaten by at most one downstream slot (twice
                //     would duplicate the signal), and `consumed` is true iff it was
                //     eaten at all (a consumed slot also skips the main mix, so a
                //     disagreement here is a dropped or double-counted device).
                {
                    int eaters = 0;
                    for (int d = c + 1; d < 3; ++d)
                        if (t.feed[d] & FROM(c)) ++eaters;

                    if (eaters > 1) ++feedBad;
                    if (t.consumed[c] != (eaters == 1)) ++feedBad;
                }
                // (d) feed points STRICTLY upstream: it may only name slots above c,
                //     never itself and never anything below (that would be a cycle).
                for (int d = c; d < 3; ++d)
                    if (t.feed[c] & FROM(d)) ++selfBad;

                // (e) a slot's eff must contain everything it taps.
                if ((uint8_t) (t.eff[c] & t.entry[c]) != t.entry[c]) ++effBad;

                // (f) hasInput agrees with the fields it is derived from.
                if (t.hasInput (c) != (t.entry[c] != 0 || t.feed[c].any())) ++liveBad;
            }
            ++swept;
        }

        check (tapBad  == 0, "a source must enter the rack exactly once");
        check (feedBad == 0, "a consumed output must be eaten by exactly one slot");
        check (selfBad == 0, "feed must only ever point upstream");
        check (effBad  == 0, "eff must contain everything the slot taps");
        check (liveBad == 0, "hasInput must agree with entry/feed");
        check (swept == NA * NA * NA, "every mask combination swept");
        std::printf ("  %d chains swept, all invariants hold\n", swept);
    }

    // ── 18. capacity: every kind's full pool has to fit, and the FEED MASK has to reach it.
    head ("18. kMaxSlots holds 6 instances of all 13 device kinds");
    check (tw::FxChainTopology::kMaxSlots >= 78, "kMaxSlots must hold 13 kinds x 6 instances");
    check (tw::FxChainTopology::kMaxSlots <= 128, "kMaxSlots must fit inside the 128-bit feed mask");

    // ── 19. 🚨 THE DEEP CHAIN. `feed` is a bitmask over upstream SLOT INDICES, so its width is
    //    the real slot ceiling. It was a uint32_t against kMaxSlots 44: slot 33 eating slot 32
    //    shifts 1u by 32, which is UNDEFINED BEHAVIOUR in C++, not merely a wrong answer. 36
    //    slots were already reachable at fb377 (6 kinds x 6) and nobody had built a chain that
    //    long. 9 kinds x 6 makes 54, so this is now ordinary usage, not a corner.
    //    The chain below is 50 devices ALL routed to source A: each one eats its predecessor,
    //    so slot 49 must name slot 48 — bit 48, seventeen bits past where a uint32 ends.
    head ("19. a 50-device chain: the feed mask reaches past bit 31");
    {
        uint8_t masks[50]; for (int i = 0; i < 50; ++i) masks[i] = A;
        tw::FxChainTopology t; t.build (masks, 50);
        check (t.count == 50, "all 50 slots survive the build");
        check (t.entry[0] == A, "the FIRST device taps A");
        int tapped = 0; for (int i = 1; i < 50; ++i) if (t.entry[i] != 0) ++tapped;
        check (tapped == 0, "and no later device taps it a second time");
        int chained = 0;
        for (int i = 1; i < 50; ++i) if (t.feed[i] == tw::SlotMask::bit (i - 1)) ++chained;
        check (chained == 49, "every slot eats exactly its predecessor, all the way to bit 48");
        check (t.feed[49] == tw::SlotMask::bit (48), "slot 49's feed mask IS bit 48");
        int eaten = 0; for (int i = 0; i < 49; ++i) if (t.consumed[i]) ++eaten;
        check (eaten == 49, "every slot but the last is consumed downstream");
        check (! t.consumed[49], "and the last one reaches the mix");
    }


    // ── 20. 🚨 fb420 — THE CHAIN THAT REACHES PAST BIT 63.
    //    This gate exists because the SAME defect has now shipped twice: a uint32 mask over 44
    //    slots (fb377) and a uint64 mask over 54 (fb413, fixed only as far as 64). Adding the
    //    Equalizer/Widen/Compress/OTT kinds makes 13 x 6 = 78 addressable slots, so slot 77
    //    naming slot 76 needs BIT 76 — seventeen bits past where a uint64 ends.
    //    ⚠️ VERIFIED TO FAIL ON THE OLD CODE: against uint64 `feed` + kMaxSlots 56 this test
    //    reports `count == 56` (the build clamps) and the chained count collapses, so the gate
    //    goes red rather than passing vacuously. A gate that has never failed has never been
    //    tested (fb393).
    head ("20. a 78-device chain: the feed mask reaches past bit 63");
    {
        constexpr int N = 78;
        uint8_t masks[N]; for (int i = 0; i < N; ++i) masks[i] = A;
        tw::FxChainTopology t; t.build (masks, N);
        check (t.count == N, "all 78 slots survive the build (13 kinds x 6 instances)");

        int chained = 0;
        for (int i = 1; i < N; ++i) if (t.feed[i] == tw::SlotMask::bit (i - 1)) ++chained;
        check (chained == N - 1, "every slot eats exactly its predecessor, all the way to bit 76");

        // the specific bits a uint64 could not name
        check (t.feed[64].test (63), "slot 64 names bit 63 — the last bit the old mask had");
        check (t.feed[65].test (64), "slot 65 names bit 64 — the FIRST bit the old mask could not");
        check (t.feed[77].test (76), "slot 77 names bit 76 — the deepest reachable chain today");
        check (! t.feed[77].test (75), "and it names ONLY its predecessor");

        int eaten = 0; for (int i = 0; i < N - 1; ++i) if (t.consumed[i]) ++eaten;
        check (eaten == N - 1, "every slot but the last is consumed downstream");
        check (! t.consumed[N - 1], "and the last one reaches the mix");
        check (t.hasInput (77), "the deepest slot is live");
    }

    // ── 21. the two words are independent — a mask spanning the 64-bit seam.
    head ("21. SlotMask spans the word seam correctly");
    {
        auto lo = tw::SlotMask::bit (5), hi = tw::SlotMask::bit (70);
        auto both = lo | hi;
        check (both.test (5) && both.test (70), "a mask can hold bits in BOTH words");
        check (! both.test (69) && ! both.test (6), "and nothing either side of them");
        check ((both & lo) == lo, "AND isolates the low word");
        check ((both & hi) == hi, "AND isolates the high word");
        check (tw::SlotMask::bit (-1) == tw::SlotMask{}, "an out-of-range bit is empty, not UB");
        check (tw::SlotMask::bit (128) == tw::SlotMask{}, "and so is one past the top");
        check (! tw::SlotMask{}.any(), "a default mask is empty");
    }

    std::printf ("\n  %d passed, %d FAILED\n\n", gPass, gFail);
    return gFail == 0 ? 0 : 1;
}
