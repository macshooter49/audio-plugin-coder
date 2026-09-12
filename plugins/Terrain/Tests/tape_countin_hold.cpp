// ══════════════════════════════════════════════════════════════════════════════════════════════
//  tape_countin_hold.cpp — fb636 review: THE COUNT-IN HOLDS ON ITS LAST BEAT, IT DOES NOT COUNT ON.
//
//    c++ -std=c++17 -O2 -I Source Tests/tape_countin_hold.cpp -o /tmp/tch && /tmp/tch
//    control: the same with -I <a Source whose TapeLoopProcessor.h predates the fix> -> [2] red
//
//  fb636 M4t made the 60 s ring lazy: the record native stores the flag, then armTapeLoop allocates it,
//  and the count-in may only COMPLETE once armed_ is set. Armed late, it held — but its beat index kept
//  counting (5, 6, ... one per beat), so getCountInBeat() handed the UI beats a 4-beat count-in has not
//  got. Now the index stops at 4 (the UI's GO) until the ring is there. TapeLoopProcessor.h is plain C++
//  (no JUCE), so this drives the class itself: 48 kHz, 120 BPM (24000 samples a beat), record held.
//
//  BARS
//   [1] ARMED FIRST (the normal order a few ms apart): the count-in completes after exactly 4 beats,
//       the index read 0,1,2,3 on the way — the timing the fix may not move
//   [2] NEVER ARMED for 8 beats: still counting in, and the index never went past 4
//   [3] ...and the moment arm() lands the count-in completes on the very next sample
//   [4] record released while holding: the count-in cancels (-1), nothing recorded
// ══════════════════════════════════════════════════════════════════════════════════════════════
#include "TapeLoopProcessor.h"
#include <cstdio>
#include <string>

static int pass = 0, fail = 0;
static void chk (bool ok, const char* label, const std::string& detail)
{ (ok ? pass : fail)++; std::printf ("  %s  %s\n        %s\n", ok ? "PASS" : "FAIL", label, detail.c_str()); }

static const double SR = 48000.0; static const float BPM = 120.0f; static const int BEAT = 24000;
// one sample through the looper, record held, play off, 1-bar loop — returns the count-in beat the UI would read
static int step (TapeLoopProcessor& t, bool rec)
{
    float l = 0.1f, r = 0.1f; bool wr = rec, wp = false;
    t.processStereo (l, r, wr, wp, 2.0f, 0.5f, 0.0f, 5.0f, BPM);
    return t.getCountInBeat();
}

int main()
{
    std::printf ("══ TAPE LOOP COUNT-IN HOLD ══   %g Hz, %g BPM, %d samples a beat\n", SR, (double) BPM, BEAT);
    {   // [1]
        TapeLoopProcessor t; t.prepare (SR, 512); t.arm();
        int seen[6] = { 0 }; long doneAt = -1;
        for (long n = 0; n < 6L * BEAT; ++n)
        {
            const int b = step (t, true);
            if (b >= 0 && b < 6) seen[b]++;
            if (b < 0 && doneAt < 0) { doneAt = n; break; }
        }
        chk (doneAt == 4L * BEAT - 1 && seen[0] && seen[1] && seen[2] && seen[3] && ! t.isCountingIn(),
             "[1] armed first: the count-in completes after exactly 4 beats (0,1,2,3), as before",
             "completed at sample " + std::to_string (doneAt) + " (4 beats = " + std::to_string (4L * BEAT - 1) + ")");
    }
    {   // [2] [3] [4]
        TapeLoopProcessor t; t.prepare (SR, 512);
        int maxBeat = -1; bool always = true;
        for (long n = 0; n < 8L * BEAT; ++n) { const int b = step (t, true); maxBeat = std::max (maxBeat, b); always &= t.isCountingIn(); }
        chk (always && maxBeat == 4, "[2] never armed for 8 beats: still counting in, the index held at 4 (the UI's GO), never 5, 6, ...",
             "max index " + std::to_string (maxBeat) + ", counting in throughout: " + (always ? "yes" : "NO"));
        t.arm();
        const int b = step (t, true);
        chk (b == -1 && ! t.isCountingIn(), "[3] ...and the sample after arm() lands, the count-in completes",
             "index after arm " + std::to_string (b));
        TapeLoopProcessor u; u.prepare (SR, 512);
        for (long n = 0; n < 6L * BEAT; ++n) step (u, true);
        const int c = step (u, false);
        chk (c == -1 && ! u.isCountingIn() && ! u.hasContent(), "[4] record released while holding: the count-in cancels, nothing recorded",
             "index " + std::to_string (c));
    }
    std::printf ("\n  %d pass, %d fail\n", pass, fail);
    return fail ? 1 : 0;
}
