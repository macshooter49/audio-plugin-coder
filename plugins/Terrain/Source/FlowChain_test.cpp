// =============================================================================
//  FlowChain_test.cpp — offline proof for the fb131 MODE-CHAIN resolver
//  g++ -std=c++17 -Wall -Wextra -ISource Source/FlowChain_test.cpp -o /tmp/fch && /tmp/fch
//
//  Headline: click order = signal path, provably. Legacy saves (chain empty →
//  single FLOW_MODE) keep working; a non-empty chain owns the truth; duplicates
//  and garbage from host automation can never wedge the resolved set.
// =============================================================================
#include "FlowChain.h"
#include <cstdio>

using namespace wc;

static int g_checks = 0, g_fail = 0;
static void check (bool ok, const char* what) { ++g_checks; if (! ok) { ++g_fail; std::printf ("  FAIL: %s\n", what); } }

static FlowChainState R (int a, int b, int c, int d, int legacy)
{
    const int s[4] = { a, b, c, d };
    return resolveFlowChain (s, legacy);
}

int main()
{
    std::printf ("— fb131 MODE CHAIN resolver —\n");

    {   // T1 — everything off
        auto s = R (0,0,0,0, 0);
        check (s.len == 0 && ! s.arp && ! s.chop && ! s.glitch && ! s.robin, "T1 all-empty + legacy Off = nothing runs");
    }
    {   // T2 — legacy fallback: old save with FLOW_MODE=Chop and no chain params
        auto s = R (0,0,0,0, 2);
        check (s.len == 1 && s.order[0] == 2 && s.chop && ! s.glitch, "T2 empty chain falls back to legacy FLOW_MODE");
    }
    {   // T3 — Max's headline: chop clicked first, glitch second
        auto s = R (2,3,0,0, 0);
        check (s.len == 2 && s.order[0] == 2 && s.order[1] == 3, "T3 chop->glitch order preserved");
        check (s.chop && s.glitch && ! s.arp && ! s.robin,       "T3 flags match the chain");
    }
    {   // T4 — the flip: glitch first, chop second
        auto s = R (3,2,0,0, 0);
        check (s.len == 2 && s.order[0] == 3 && s.order[1] == 2, "T4 glitch->chop order preserved");
    }
    {   // T5 — a non-empty chain OWNS the truth (stale legacy FLOW_MODE ignored)
        auto s = R (2,0,0,0, 3);
        check (s.len == 1 && s.chop && ! s.glitch, "T5 chain wins over stale FLOW_MODE");
    }
    {   // T6 — duplicate mode keeps its FIRST sighting
        auto s = R (2,2,3,2, 0);
        check (s.len == 2 && s.order[0] == 2 && s.order[1] == 3, "T6 duplicates dedupe to first sighting");
    }
    {   // T7 — empty slots in the middle never break the order
        auto s = R (0,4,0,1, 0);
        check (s.len == 2 && s.order[0] == 4 && s.order[1] == 1 && s.robin && s.arp, "T7 gaps skipped, order kept");
    }
    {   // T8 — all four chained
        auto s = R (4,1,2,3, 0);
        check (s.len == 4 && s.order[0] == 4 && s.order[1] == 1 && s.order[2] == 2 && s.order[3] == 3,
               "T8 all four in click order");
        check (s.arp && s.chop && s.glitch && s.robin, "T8 all flags on");
    }
    {   // T9 — out-of-range garbage (host mishap) can't wedge anything
        auto s = R (7,-1,2,0, 9);
        check (s.len == 1 && s.order[0] == 2 && s.chop, "T9 garbage slots skipped, valid one survives");
        auto t = R (0,0,0,0, 9);
        check (t.len == 0, "T9 garbage legacy mode = nothing runs");
    }
    {   // T10 — note-only chain (arp+robin): no audio stage flags
        auto s = R (1,4,0,0, 0);
        check (s.len == 2 && s.arp && s.robin && ! s.chop && ! s.glitch, "T10 note-only chain has no audio stages");
    }

    std::printf ("\n%d checks, %d failed\n", g_checks, g_fail);
    if (g_fail == 0) std::printf ("ALL %d CHECKS PASSED\n", g_checks);
    return g_fail == 0 ? 0 : 1;
}
