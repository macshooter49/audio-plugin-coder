// =============================================================================
//  FlowRobin_test.cpp — offline proof for the ROBIN rotation brain
//  g++ -std=c++17 -Wall -Wextra -ISource Source/FlowRobin_test.cpp -o /tmp/fr && /tmp/fr
//
//  Headline: the Wheel card's every control provably decides where notes land.
//  Neutral defaults = a textbook A→B→C→D cycle; every mode, the order
//  permutation, Times, resets, Hold/Stay, and the humanize dice are checked —
//  including the fb111 bit-identical determinism proof.
// =============================================================================
#include "FlowRobin.h"
#include <cstdio>
#include <vector>

using namespace wc;

static int g_checks = 0, g_fail = 0;
static void check (bool ok, const char* what) { ++g_checks; if (! ok) { ++g_fail; std::printf ("  FAIL: %s\n", what); } }

constexpr double SR = 48000;

// play a run of quick note-ons (note press+release each ~50ms apart) and collect stations
static std::vector<int> run (FlowRobin& r, int count, int noteBase = 60, double gapSec = 0.05)
{
    std::vector<int> out;
    for (int i = 0; i < count; ++i)
    {
        r.onNoteOn (noteBase + (i % 12), false, -1);
        out.push_back (r.takeHit().station);
        r.onNoteOff (noteBase + (i % 12));
        r.tick (0.0, (int) (gapSec * SR), false);
    }
    return out;
}
static FlowRobin fresh (const RobinExtParams& p, bool a=true,bool b=true,bool c=true,bool d=true)
{
    FlowRobin r; r.prepare (SR); r.setActive (true); r.setExt (p); r.setAudible (a,b,c,d);
    return r;
}

int main()
{
    std::printf ("FlowRobin brain — rotation proof\n");

    // ── T1: NEUTRAL DEFAULTS — textbook A→B→C→D cycle, forever ────────────────
    {
        FlowRobin r = fresh (RobinExtParams{});
        auto s = run (r, 12);
        bool ok = true;
        for (int i = 0; i < 12; ++i) if (s[(size_t) i] != i % 4) ok = false;
        check (ok, "T1 defaults: A,B,C,D,A,B,C,D... exactly");
    }

    // ── T2: BANK subset + never empty ────────────────────────────────────────
    {
        RobinExtParams p; p.bank[1] = false; p.bank[3] = false;   // A + C only
        FlowRobin r = fresh (p);
        auto s = run (r, 8);
        bool ok = true;
        for (size_t i = 0; i < s.size(); ++i) if (s[i] != (i % 2 == 0 ? 0 : 2)) ok = false;
        check (ok, "T2 bank {A,C}: alternates A,C only");
        RobinExtParams q; q.bank[0]=q.bank[1]=q.bank[2]=q.bank[3]=false;   // user turned all off
        FlowRobin r2 = fresh (q);
        auto s2 = run (r2, 4);
        bool audFallback = true;
        for (int v : s2) if (v < 0 || v > 3) audFallback = false;
        check (audFallback, "T2 empty bank falls back to the audible set (never dead)");
    }

    // ── T3: ORDER permutation + Run backward ─────────────────────────────────
    {
        RobinExtParams p; p.order[0]=3; p.order[1]=1; p.order[2]=0; p.order[3]=2;   // D,B,A,C
        FlowRobin r = fresh (p);
        auto s = run (r, 8);
        const int want[4] = { 3, 1, 0, 2 };
        bool ok = true; for (int i = 0; i < 8; ++i) if (s[(size_t) i] != want[i % 4]) ok = false;
        check (ok, "T3 order D,B,A,C respected");
        RobinExtParams pb; pb.backward = true;
        FlowRobin r2 = fresh (pb);
        auto s2 = run (r2, 6);   // starts at first (A), then walks backward: A,D,C,B,A,D
        const int wantB[6] = { 0, 3, 2, 1, 0, 3 };
        bool okB = true; for (int i = 0; i < 6; ++i) if (s2[(size_t) i] != wantB[i]) okB = false;
        check (okB, "T3 Run backward: A,D,C,B,A,D");
    }

    // ── T4: MODES — Pong bounces; Shuffle = fair bags; Random stays in-cycle ──
    {
        RobinExtParams p; p.mode = 3;                            // Pong
        FlowRobin r = fresh (p);
        auto s = run (r, 8);                                     // A B C D C B A B
        const int want[8] = { 0, 1, 2, 3, 2, 1, 0, 1 };
        bool ok = true; for (int i = 0; i < 8; ++i) if (s[(size_t) i] != want[i]) ok = false;
        check (ok, "T4 Pong bounces at the ends (no repeat at the turn)");

        RobinExtParams ps; ps.mode = 1;                          // Shuffle
        FlowRobin r2 = fresh (ps);
        auto s2 = run (r2, 24);
        bool fair = true, noRep = true;
        for (int bag = 0; bag < 5; ++bag)                        // bags start AFTER the aFirst note
        {
            int seen[4] = { 0,0,0,0 };
            for (int i = 1 + bag*4; i < 1 + bag*4 + 4 && i < 24; ++i) seen[s2[(size_t) i]]++;
            for (int k = 0; k < 4; ++k) if (seen[k] != 1) fair = false;
        }
        for (size_t i = 1; i < s2.size(); ++i) if (s2[i] == s2[i-1]) noRep = false;
        check (fair,  "T4 Shuffle: every station exactly once per bag");
        check (noRep, "T4 Shuffle: never the same station twice in a row");

        RobinExtParams pr; pr.mode = 2; pr.bank[3] = false;      // Random over A,B,C
        FlowRobin r3 = fresh (pr);
        auto s3 = run (r3, 40);
        bool inCyc = true, noRep2 = true;
        for (size_t i = 0; i < s3.size(); ++i) { if (s3[i] == 3 || s3[i] < 0) inCyc = false;
                                                 if (i && s3[i] == s3[i-1]) noRep2 = false; }
        check (inCyc && noRep2, "T4 Random: in-cycle only, no immediate repeats");
    }

    // ── T5: TIMES — each station plays N notes before moving on ──────────────
    {
        RobinExtParams p; p.times = 3;
        FlowRobin r = fresh (p);
        auto s = run (r, 12);
        bool ok = true;
        for (int i = 0; i < 12; ++i) if (s[(size_t) i] != (i / 3) % 4) ok = false;
        check (ok, "T5 Times 3: AAA BBB CCC DDD");
    }

    // ── T6: A FIRST + AFTER — silence resets the phrase to the first station ──
    {
        RobinExtParams p; p.after = 1.0f;
        FlowRobin r = fresh (p);
        auto s1 = run (r, 3);                                    // A B C
        r.tick (0.0, (int) (2.0 * SR), false);                   // 2s of silence
        r.onNoteOn (60, false, -1);
        const int back = r.takeHit().station;
        check (s1[2] == 2 && back == 0, "T6 aFirst: after silence the phrase restarts on A");
    }

    // ── T7: RESET Bar — crossing a bar restarts the cycle ────────────────────
    {
        RobinExtParams p; p.reset = 1; p.aFirst = false;
        FlowRobin r = fresh (p);
        r.tick (0.0, 64, true);
        r.onNoteOn (60, false, -1); (void) r.takeHit(); r.onNoteOff (60);   // A
        r.onNoteOn (61, false, -1); (void) r.takeHit(); r.onNoteOff (61);   // B
        r.tick (4.1, 64, true);                                  // bar line crossed
        r.onNoteOn (62, false, -1);
        check (r.takeHit().station == 0, "T7 Reset Bar: first note after the barline is A");
    }

    // ── T8: LEGATO Keep vs New ───────────────────────────────────────────────
    {
        RobinExtParams p;                                        // Keep (default)
        FlowRobin r = fresh (p);
        r.onNoteOn (60, false, -1); (void) r.takeHit();
        check (r.onLegatoRetarget (62) == -1, "T8 Legato Keep: retargets stay on their station");
        RobinExtParams pn; pn.legato = 1;
        FlowRobin r2 = fresh (pn);
        r2.onNoteOn (60, false, -1); (void) r2.takeHit();        // A
        check (r2.onLegatoRetarget (62) == 1, "T8 Legato New: the retarget advances to B");
    }

    // ── T9: STEAL Stay + RELEASE Hold ────────────────────────────────────────
    {
        RobinExtParams p; p.steal = 1;
        FlowRobin r = fresh (p);
        run (r, 2);                                              // now on B
        r.onNoteOn (70, true, 3);                                // stole a voice on station D
        check (r.takeHit().station == 3, "T9 Steal Stay: the new note reuses the stolen station");
        RobinExtParams ph; ph.release = 0;
        FlowRobin r2 = fresh (ph);
        r2.onNoteOn (60, false, -1);
        const int first = r2.takeHit().station; r2.onNoteOff (60);
        r2.tick (0.0, 4800, false);                              // 0.1s — still ringing
        r2.onNoteOn (60, false, -1);
        check (r2.takeHit().station == first, "T9 Release Hold: a re-pressed ringing key keeps its station");
    }

    // ── T10: DETERMINISM (fb111) — two brains, same notes, identical decisions ─
    {
        RobinExtParams p; p.mode = 1; p.vary = 0.8f; p.lvl = 0.7f; p.pan = 0.9f; p.wobble = 0.6f;
        FlowRobin a = fresh (p), b = fresh (p);
        bool ident = true;
        for (int i = 0; i < 32; ++i)
        {
            a.onNoteOn (60 + i % 7, false, -1); b.onNoteOn (60 + i % 7, false, -1);
            const RobinHit ha = a.takeHit(), hb = b.takeHit();
            if (ha.station != hb.station || ha.vel != hb.vel || ha.ampL != hb.ampL
                || ha.ampR != hb.ampR || ha.delaySamp != hb.delaySamp) ident = false;
            a.onNoteOff (60 + i % 7); b.onNoteOff (60 + i % 7);
            a.tick (0, 2400, false);  b.tick (0, 2400, false);
        }
        check (ident, "T10 two instances, same notes -> bit-identical hits (no mutable dice)");
    }

    // ── T11: HUMANIZE — bounded + neutral at zero ────────────────────────────
    {
        FlowRobin r = fresh (RobinExtParams{});                  // all humanize 0
        r.onNoteOn (60, false, -1);
        const RobinHit h = r.takeHit();
        check (h.vel == 1.f && h.ampL == 1.f && h.ampR == 1.f && h.delaySamp == 0
               && h.glideFrom < 0.0, "T11 neutral: unity hit (no vel/pan/delay/glide)");
        RobinExtParams p; p.vary = 1.f; p.lvl = 1.f; p.pan = 1.f; p.wobble = 1.f; p.glide = 1.f;
        FlowRobin r2 = fresh (p);
        bool bounded = true;
        for (int i = 0; i < 24; ++i)
        {
            r2.onNoteOn (60 + i, false, -1);
            const RobinHit h2 = r2.takeHit();
            if (h2.vel < 0.30f || h2.vel > 1.f) bounded = false;
            if (h2.ampL < 0.55f || h2.ampR < 0.55f || h2.ampL > 1.f || h2.ampR > 1.f) bounded = false;
            if (h2.delaySamp < 0 || h2.delaySamp > (int) (0.021 * SR)) bounded = false;
            if (h2.changed && (h2.glideSec < 0.03f || h2.glideSec > 0.36f)) bounded = false;
            r2.onNoteOff (60 + i);
        }
        check (bounded, "T11 humanize maxed: vel/pan/delay/glide inside perceptual ceilings");
    }

    // ── T12: station glide fires only on CHANGES ─────────────────────────────
    {
        RobinExtParams p; p.glide = 1.f; p.times = 2;
        FlowRobin r = fresh (p);
        r.onNoteOn (60, false, -1); const RobinHit h1 = r.takeHit(); r.onNoteOff (60);
        r.onNoteOn (62, false, -1); const RobinHit h2 = r.takeHit(); r.onNoteOff (62);   // same station (times 2)
        r.onNoteOn (64, false, -1); const RobinHit h3 = r.takeHit(); r.onNoteOff (64);   // station change
        check (h1.glideFrom < 0 && h2.glideFrom < 0 && h3.glideFrom >= 0 && h3.changed,
               "T12 glide arms only when the station changes");
    }

    std::printf ("\n%d checks, %d failed\n", g_checks, g_fail);
    if (g_fail == 0) std::printf ("ALL %d CHECKS PASSED\n", g_checks);
    return g_fail == 0 ? 0 : 1;
}
