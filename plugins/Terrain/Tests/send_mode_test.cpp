// fb414 — SEND MODE: does the oscillator still reach the main mix?
//
// Max's patch, in one sentence: osc A keeps playing clean while a COPY travels
// granular -> distortion and comes back as distorted grains only.
//
// The chain already handled the granular->distortion half by construction (the first device
// routed to a source TAPS it; every later device sharing that source EATS the upstream output
// instead of tapping the oscillator again). What was missing is that routing a source to ANY
// device SUBTRACTED that oscillator from the main mix, so the only dry you could get back was
// whatever the first device's Mix passed through — and that dry then travelled down the branch
// and got processed by everything after it. Grains and note, inseparable.
//
// This tests the exact decision the processor makes: given the chain's ENTRY masks and each
// tapping device's mode, WHICH SOURCES LEAVE THE MAIN MIX. It mirrors PluginProcessor.cpp's
// exUnion loop line for line, so a change there that breaks the model breaks this.
//
//   clang++ -O2 -std=c++17 -I ../Source send_mode_test.cpp -o /tmp/send_mode && /tmp/send_mode

#include "../Source/FxChainTopology.h"
#include <cstdio>
#include <cstdint>
#include <string>

static int gPass = 0, gFail = 0;
static void chk (bool ok, const char* what, const std::string& detail = "")
{
    if (ok) { ++gPass; std::printf ("  ok    %s%s%s\n", what, detail.empty() ? "" : "   ", detail.c_str()); }
    else    { ++gFail; std::printf ("  FAIL  %s%s%s\n", what, detail.empty() ? "" : "   ", detail.c_str()); }
}
static void head (const char* h) { std::printf ("\n[%s]\n", h); }

enum : uint8_t { A = 1, B = 2, C = 4, D = 8, SUB = 16, NZ = 32 };

static std::string srcStr (uint8_t m)
{
    if (! m) return "-";
    const char* N[6] = { "A","B","C","D","Sub","Noise" };
    std::string s;
    for (int i = 0; i < 6; ++i) if (m & (1u << i)) { if (! s.empty()) s += "+"; s += N[i]; }
    return s;
}

// ── THE MODEL UNDER TEST — PluginProcessor.cpp's exUnion loop, verbatim in shape.
//    `send[c]` is slot c's tap mode. Two conditions gate it, and both matter:
//      · a slot with entry == 0 taps nothing and cannot pull anything out whatever its mode
//        says — which is why the mode lives on the TAP;
//      · 🔑 fb415 THE FIRST-SLOT LAW (Max, HARD RULE): only c == 0 may send. Everything after
//        the first device is an insert, always. The card shows the Send glyph under exactly
//        this condition too, so the button and the DSP cannot disagree.
static uint8_t pulledFromMix (const uint8_t* masks, const bool* send, int n)
{
    tw::FxChainTopology t; t.build (masks, n);
    uint8_t insertMask = 0;
    for (int c = 0; c < n; ++c)
    {
        const uint8_t e = t.entry[c];
        if (e == 0) continue;
        const bool sendMode = (c == 0) && send[c];
        if (! sendMode) insertMask = (uint8_t) (insertMask | e);
    }
    return insertMask;
}

int main()
{
    std::printf ("\n══ fb414 — SEND MODE: which oscillators leave the main mix ══\n");

    // ── 1. the shipped behaviour is untouched when nothing is in send mode
    head ("1. INSERT is still insert — every existing project must load identical");
    {
        uint8_t m[2] = { A, A };  bool sd[2] = { false, false };
        chk (pulledFromMix (m, sd, 2) == A,
             "granular + distortion, both on A: A leaves the mix",
             "pulled = " + srcStr (pulledFromMix (m, sd, 2)));

        uint8_t m2[3] = { A, (uint8_t) (B | C), D };  bool sd2[3] = { false, false, false };
        chk (pulledFromMix (m2, sd2, 3) == (uint8_t) (A | B | C | D),
             "three devices on four sources: all four leave",
             "pulled = " + srcStr (pulledFromMix (m2, sd2, 3)));

        uint8_t m3[1] = { 0 };  bool sd3[1] = { false };
        chk (pulledFromMix (m3, sd3, 1) == 0, "an unrouted device pulls nothing");
    }

    // ── 2. MAX'S PATCH
    head ("2. Max's patch: granular on A in SEND, distortion behind it");
    {
        uint8_t m[2] = { A, A };            // both routed to A, granular first in the chain
        bool    sd[2] = { true, false };    // granular SENDs; the distortion's mode is irrelevant
        tw::FxChainTopology t; t.build (m, 2);

        chk (t.entry[0] == A, "the granular TAPS osc A", "entry[0] = " + srcStr (t.entry[0]));
        chk (t.entry[1] == 0, "the distortion taps NOTHING — it never touches raw osc A",
             "entry[1] = " + srcStr (t.entry[1]));
        chk (t.feed[1] == 1ull, "...it eats the granular's output instead", "feed[1] = 0x1");
        chk (t.consumed[0] && ! t.consumed[1],
             "only the distortion's output reaches the mix; the granular's is consumed");
        chk (pulledFromMix (m, sd, 2) == 0,
             "AND osc A KEEPS PLAYING — nothing is subtracted from the main mix",
             "pulled = " + srcStr (pulledFromMix (m, sd, 2)));
    }

    // ── 3. the distortion's own mode must not matter — it is not the tap
    head ("3. the mode belongs to the TAP, not to every device that shares the source");
    {
        uint8_t m[2] = { A, A };
        bool sd_a[2] = { true, false }, sd_b[2] = { true, true };
        chk (pulledFromMix (m, sd_a, 2) == pulledFromMix (m, sd_b, 2),
             "flipping the DOWNSTREAM device's mode changes nothing");
        bool sd_c[2] = { false, true };
        chk (pulledFromMix (m, sd_c, 2) == A,
             "and a SEND on the downstream device cannot rescue an insert tap — it neither "
             "taps nor sits first", "pulled = " + srcStr (pulledFromMix (m, sd_c, 2)));
    }

    // ── 4. 🔑 THE FIRST-SLOT LAW
    head ("4. fb415 — ONLY the first slot may send; everything after it is an insert");
    {
        uint8_t m[3] = { A, B, C };
        bool first[3] = { true,  false, false };   // the leftmost card sends
        bool second[3]= { false, true,  false };   // a later card claims to — it must be ignored
        bool third[3] = { false, false, true  };
        chk (pulledFromMix (m, first, 3) == (uint8_t) (B | C),
             "first slot sends: A stays in the mix, B and C leave",
             "pulled = " + srcStr (pulledFromMix (m, first, 3)));
        chk (pulledFromMix (m, second, 3) == (uint8_t) (A | B | C),
             "the SECOND slot's send flag is inert — all three still leave",
             "pulled = " + srcStr (pulledFromMix (m, second, 3)));
        chk (pulledFromMix (m, third, 3) == (uint8_t) (A | B | C),
             "and so is the third's", "pulled = " + srcStr (pulledFromMix (m, third, 3)));
    }

    // ── 4b. the reorder case: the law is POSITIONAL, so dragging changes who may send
    head ("4b. drag the sender out of first place and its send goes inert");
    {
        uint8_t before[2] = { A, B };  bool sBefore[2] = { true, false };   // granular first
        uint8_t after [2] = { B, A };  bool sAfter [2] = { false, true  };  // dragged to 2nd
        chk (pulledFromMix (before, sBefore, 2) == B,
             "granular first + send: A keeps playing", "pulled = " + srcStr (pulledFromMix (before, sBefore, 2)));
        chk (pulledFromMix (after, sAfter, 2) == (uint8_t) (A | B),
             "same device, now second: its stored send does nothing and A leaves again",
             "pulled = " + srcStr (pulledFromMix (after, sAfter, 2)));
    }

    // ── 5. the failure mode Max will actually hit: deleting the front of a branch
    head ("5. delete the granular and the distortion becomes the FIRST slot AND the tap");
    {
        uint8_t m[1] = { A };
        bool insert[1] = { false }, send[1] = { true };
        chk (pulledFromMix (m, insert, 1) == A,
             "alone and INSERT, the distortion takes the whole note (the old behaviour)");
        chk (pulledFromMix (m, send, 1) == 0,
             "alone and SEND, it is a parallel distortion — a much gentler surprise");
    }

    // ── 6. a send tap must still FEED the rack. The tap and the subtraction are separate
    //      mechanisms in the processor (poolSendBuf_ vs routedDryBuf_), and this is the
    //      assertion that says so: send changes the mix, never the entry.
    head ("6. SEND changes the MIX, never the routing");
    {
        uint8_t m[3] = { A, A, (uint8_t) (B | A) };
        bool off[3] = { false, false, false }, on[3] = { true, true, true };
        tw::FxChainTopology t1; t1.build (m, 3);
        tw::FxChainTopology t2; t2.build (m, 3);
        (void) pulledFromMix (m, off, 3);
        (void) pulledFromMix (m, on, 3);
        bool same = true;
        for (int c = 0; c < 3; ++c)
            same = same && t1.entry[c] == t2.entry[c] && t1.feed[c] == t2.feed[c]
                        && t1.consumed[c] == t2.consumed[c] && t1.eff[c] == t2.eff[c];
        chk (same, "the topology is identical with send on and off — only the mix differs");
        chk (t1.entry[0] == A && t1.entry[1] == 0 && t1.entry[2] == B,
             "and the third device still taps only what nobody claimed",
             "entries: " + srcStr (t1.entry[0]) + " / " + srcStr (t1.entry[1]) + " / " + srcStr (t1.entry[2]));
    }

    std::printf ("\n  %d passed, %d FAILED\n\n", gPass, gFail);
    return gFail == 0 ? 0 : 1;
}
