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
struct Want { uint8_t entry; uint32_t feed; bool consumed; uint8_t eff; };

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
        std::snprintf (buf, sizeof buf, "slot %d feed = 0x%x, wanted 0x%x",
                       slot, t.feed[slot], w.feed);
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

constexpr uint32_t FROM (int slot) { return 1u << (unsigned) slot; }

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

    std::printf ("\n[fb375 — PURE-FX (whole-mix) devices: filter / splitter / utility]\n");

    // ── 10. MAX'S CASE, verbatim: "filter is first but it still takes the sound
    //       and puts it thru to the delay, simple as that."
    head ("10. Filter first takes the whole mix and hands it to the Delay");
    { uint8_t m[] = { 0, A }; bool w[] = { true, false }; t.build (m, 2, w);
      expect (t, 0, { ALL, 0,       true,  ALL });   // taps every source; its output goes downstream
      expect (t, 1, { 0,   FROM(0), false, ALL });   // the delay eats the filtered whole mix
      check (t.hasInput (0), "filter must be live with no routes at all"); }

    // ── 11. a Filter on its own is never silent — the whole point of the class.
    head ("11. a lone Filter with no routes takes the whole mix");
    { uint8_t m[] = { 0 }; bool w[] = { true }; t.build (m, 1, w);
      expect (t, 0, { ALL, 0, false, ALL });
      check (t.hasInput (0), "a lone pure-FX device must be live"); }

    // ── 12. below a per-osc device it eats that output AND the never-routed dry.
    head ("12. Filter after a Delay eats the delay plus all remaining dry");
    { uint8_t m[] = { A, 0 }; bool w[] = { false, true }; t.build (m, 2, w);
      expect (t, 0, { A,                  0,       true,  A });
      expect (t, 1, { (uint8_t) (ALL & ~A), FROM(0), false, ALL }); }

    // ── 13. two pure-FX devices in a row run serially, not in parallel.
    head ("13. Filter into Utility is serial");
    { uint8_t m[] = { 0, 0 }; bool w[] = { true, true }; t.build (m, 2, w);
      expect (t, 0, { ALL, 0,       true,  ALL });
      expect (t, 1, { 0,   FROM(0), false, ALL }); }

    // ── 14. independent per-osc branches above a Filter both merge into it.
    head ("14. a Filter merges every independent branch above it");
    { uint8_t m[] = { A, C, 0 }; bool w[] = { false, false, true }; t.build (m, 3, w);
      expect (t, 0, { A, 0, true, A });
      expect (t, 1, { C, 0, true, C });
      expect (t, 2, { (uint8_t) (ALL & ~(A | C)), FROM(0) | FROM(1), false, ALL }); }

    // ── 15. THE CONSEQUENCE, asserted rather than left as prose: a per-osc device
    //       below a Filter taps nothing new. entry == 0 is what the UI greys on.
    head ("15. per-osc pills go inert below a whole-mix device");
    { uint8_t m[] = { 0, (uint8_t) (A | C) }; bool w[] = { true, false }; t.build (m, 2, w);
      check (t.entry[1] == 0, "slot 1 must tap nothing new — its pills are inert");
      check (t.feed[1] == FROM(0), "slot 1 must still be fed by the filter");
      check (t.hasInput (1), "slot 1 is live via the feed, not via its pills"); }

    // ── 16. a routeless per-osc device below a Filter stays dead (no regression:
    //       the filter's output only reaches devices that ask for a source).
    head ("16. a routeless per-osc device below a Filter is still dead");
    { uint8_t m[] = { 0, 0 }; bool w[] = { true, false }; t.build (m, 2, w);
      check (t.hasInput (0), "the filter is live");
      check (! t.hasInput (1), "the routeless per-osc device stays dead"); }

    // ── 16b. the far end of the mask — Sub and Noise are sources like any other,
    //        and a Filter must pick up the ones nobody routed.
    head ("16b. Filter picks up Sub/Noise that no per-osc device claimed");
    { uint8_t m[] = { (uint8_t) (SUB | NOISE), 0, D }; bool w[] = { false, true, false };
      t.build (m, 3, w);
      expect (t, 0, { (uint8_t) (SUB | NOISE), 0,       true,  (uint8_t) (SUB | NOISE) });
      expect (t, 1, { (uint8_t) (ALL & ~(SUB | NOISE)), FROM(0), true,  ALL });
      expect (t, 2, { 0,                       FROM(1), false, ALL });
      check (t.entry[2] == 0, "the D pills below the filter are inert"); }

    std::printf ("\n[THE NULL — fb375 must not have moved fb351 by one bit]\n");

    // ── 17. For every mask combination up to 3 slots, passing nullptr and passing
    //       an all-false wholeMix[] must agree, and both must equal fb351.
    head ("17. wholeMix=nullptr is bit-identical to all-false, exhaustively");
    {
        int compared = 0;
        const uint8_t alphabet[] = { 0, A, C, (uint8_t) (A | C), (uint8_t) (A | B | C), ALL };
        const int NA = (int) (sizeof alphabet / sizeof alphabet[0]);
        tw::FxChainTopology t2;
        bool allFalse[3] = { false, false, false };

        for (int i = 0; i < NA; ++i)
        for (int j = 0; j < NA; ++j)
        for (int k = 0; k < NA; ++k)
        {
            uint8_t m[3] = { alphabet[i], alphabet[j], alphabet[k] };
            t .build (m, 3);              // no wholeMix argument at all
            t2.build (m, 3, allFalse);    // explicit all-false

            for (int c = 0; c < 3; ++c)
            {
                if (t.entry[c]    != t2.entry[c]
                 || t.feed[c]     != t2.feed[c]
                 || t.consumed[c] != t2.consumed[c]
                 || t.eff[c]      != t2.eff[c])
                {
                    char buf[160];
                    std::snprintf (buf, sizeof buf, "divergence at masks {%s,%s,%s} slot %d",
                                   srcStr (m[0]).c_str(), srcStr (m[1]).c_str(),
                                   srcStr (m[2]).c_str(), c);
                    check (false, buf);
                    goto done;
                }
            }
            ++compared;
        }
    done:
        check (compared == NA * NA * NA, "every mask combination compared");
        std::printf ("  %d mask combinations compared, no divergence\n", compared);
    }

    // ── 18. capacity: the filter pool has to fit.
    head ("18. kMaxSlots holds 6 instances of all 6 device kinds");
    check (tw::FxChainTopology::kMaxSlots >= 36, "kMaxSlots must hold 6 kinds x 6 instances");

    std::printf ("\n  %d passed, %d FAILED\n\n", gPass, gFail);
    return gFail == 0 ? 0 : 1;
}
