// ══════════════════════════════════════════════════════════════════════════════════════════════
//  mod_route_bound_cert.cpp — tp101 · THE MOD MATRIX HAS NO ROUTE MAX (offline, JUCE-free).
//
//    clang++ -std=c++17 -O2 -I Tests/shim -I Source Tests/mod_route_bound_cert.cpp -o /tmp/mrb && /tmp/mrb
//
//  Max: the MOD page read "4 routes · 32 max" and he has built more than 32. The page capped at 32,
//  the drag adders at 128 and the engine (wc::MAX_ASSIGNMENTS) at 128; a curve past the 32nd drawn
//  connection silently played straight (kMaxModCurves). The bound is now 256 routes and 256 curve
//  slots — preallocated arrays, no allocation on the audio thread — and it is never shown.
//  This drives the SHIPPED code (SynthModConfig.h, FxModValue.h):
//    A  the bounds: 256 routes, one curve slot per route
//    B  copyModConfig moves the live prefix exactly (LFOs, drift lanes, every route field) and
//       never reads past numAssignments — the per-voice broadcast copy
//    C  a FULL 256-route matrix on 256 DISTINCT rack parameters: the 256th route still gets a slot
//       and moves its knob (at the old bound of 128, slotFor returned −1 from the 129th on)
//    D  route 41 and route 200 move their knobs; the curve in slot 200 applies to route 200
//    E  an empty matrix touches nothing (the early-out)
//  The installed-AU twin (a route's POSITION does not decide whether it is heard) is
//  Tests/au_mod_routes_uncapped.cpp.
// ══════════════════════════════════════════════════════════════════════════════════════════════
#include <cstdio>
#include <cmath>
#include <string>
#include "SynthModConfig.h"
#include "FxModValue.h"

static int pass = 0, fail = 0;
static void gate (const char* what, bool ok, const std::string& d = {})
{ (ok ? pass : fail)++; std::printf ("  %-5s %s%s%s\n", ok ? "ok" : "FAIL", what, d.empty() ? "" : "   ", d.c_str()); }

using namespace wc;

// One float per rack cell: every (kind, inst, knob) is its own parameter, so N routes on N dests need N slots.
static float gCell[kFxModKinds][kFxModInsts][kFxModKnobs];
static float gLfo[NUM_LFOS];

static void build (FxModAccum& acc, const ModConfig& cfg, const ModCurveSet* curves)
{
    buildFxMod (acc, cfg, curves,
        [] (int k, int i, int n) -> const void* { return &gCell[k][i][n]; },
        [] (const void* p) { return *static_cast<const float*> (p); },
        [] (int sI, int, bool& ok) { ok = (sI >= 0 && sI < NUM_LFOS); return ok ? gLfo[sI] : 0.0f; });
}
static int destN (int n) { return (int) ModDest::FxModBase + n; }   // the n-th distinct rack parameter
static const void* refN (int n)
{ const FxModAddr ad = fxModDecode (destN (n)); return &gCell[ad.kind][ad.inst][ad.knob]; }

int main()
{
    std::printf ("\n[A. the bounds]\n");
    gate ("MAX_ASSIGNMENTS is 256 (was 128; the page showed 32)", MAX_ASSIGNMENTS == 256, "MAX_ASSIGNMENTS=" + std::to_string (MAX_ASSIGNMENTS));
    gate ("one curve slot per route: kMaxModCurves >= MAX_ASSIGNMENTS (was 32)", kMaxModCurves >= MAX_ASSIGNMENTS, "kMaxModCurves=" + std::to_string (kMaxModCurves));
    gate ("the rack has room for 256 distinct destinations", (int) ModDest::FxModEnd - (int) ModDest::FxModBase >= MAX_ASSIGNMENTS);

    for (auto& a : gCell) for (auto& b : a) for (auto& c : b) c = 0.5f;
    for (auto& l : gLfo) l = 0.0f;
    gLfo[0] = 0.8f;   // LFO 1 is up; LFO 2 (the fillers' source) sits at 0

    std::printf ("\n[B. copyModConfig — the live prefix]\n");
    {
        static ModConfig src, dst;
        src.numAssignments = MAX_ASSIGNMENTS;
        for (int a = 0; a < MAX_ASSIGNMENTS; ++a)
        { auto& r = src.assignments[a]; r.source = ModSource::L2; r.dest = (ModDest) destN (a); r.depth = 0.001f * (float) a;
          r.enabled = true; r.curve = a; r.pol = a % 3; r.auxInv = (a & 1) != 0; r.auxCrv = -0.5f; r.useAux = (a % 5) == 0; r.auxSource = ModSource::L3; }
        src.lfos[3].rateHz = 7.25f; src.driftLanes[5] = 0.33f;
        dst.assignments[MAX_ASSIGNMENTS - 1].depth = 99.0f;
        copyModConfig (dst, src);
        bool same = dst.numAssignments == MAX_ASSIGNMENTS && dst.lfos[3].rateHz == 7.25f && dst.driftLanes[5] == 0.33f;
        for (int a = 0; a < MAX_ASSIGNMENTS; ++a)
        { const auto& x = src.assignments[a]; const auto& y = dst.assignments[a];
          same &= x.source == y.source && x.dest == y.dest && x.depth == y.depth && x.enabled == y.enabled && x.curve == y.curve
               && x.pol == y.pol && x.auxInv == y.auxInv && x.auxCrv == y.auxCrv && x.useAux == y.useAux && x.auxSource == y.auxSource; }
        gate ("a full 256-route config copies every field of every route", same);
        src.numAssignments = 3; dst.assignments[10].depth = 42.0f;
        copyModConfig (dst, src);
        gate ("a 3-route config moves 3 routes and leaves the tail alone (no reader walks past numAssignments)",
              dst.numAssignments == 3 && dst.assignments[10].depth == 42.0f && dst.assignments[2].depth == src.assignments[2].depth);
        src.numAssignments = MAX_ASSIGNMENTS + 50; copyModConfig (dst, src);
        gate ("an out-of-range count is clamped to the bound", dst.numAssignments == MAX_ASSIGNMENTS);
    }

    static FxModAccum acc;
    static ModConfig cfg;
    auto filler = [] (ModConfig& c, int n) { auto& a = c.assignments[c.numAssignments++];
        a = Assignment {}; a.source = ModSource::L2; a.dest = (ModDest) destN (n); a.depth = 0.0f; a.enabled = true; };
    auto audible = [] (ModConfig& c, int n, int curve) { auto& a = c.assignments[c.numAssignments++];
        a = Assignment {}; a.source = ModSource::L1; a.dest = (ModDest) destN (n); a.depth = 0.5f; a.enabled = true; a.curve = curve; };

    std::printf ("\n[C. a full matrix — 256 routes on 256 distinct parameters]\n");
    {
        cfg.numAssignments = 0;
        for (int n = 0; n < MAX_ASSIGNMENTS - 1; ++n) filler (cfg, n);
        audible (cfg, MAX_ASSIGNMENTS - 1, -1);
        build (acc, cfg, nullptr);
        const float v = acc.lookup (refN (MAX_ASSIGNMENTS - 1), -1.0f), f = acc.lookup (refN (0), -1.0f);
        char b[160]; std::snprintf (b, sizeof b, "slots %d · route 256's knob %.3f (base 0.500) · a filler's knob %.3f", acc.count, v, f);
        gate ("every route gets a slot, and route 256 moves its knob", acc.count == MAX_ASSIGNMENTS && v > 0.5f + 0.05f && std::fabs (f - 0.5f) < 1e-6f, b);
    }

    std::printf ("\n[D. route 41 and route 200 are heard; slot 200's curve is theirs]\n");
    {
        static ModCurveSet cs {};
        for (int k = 0; k < kModCurvePts; ++k) cs.c[199].pts[k] = 1.0f - (float) k / (float) (kModCurvePts - 1);   // an inverting curve in slot 200
        cs.c[199].set = true;
        auto at = [&] (int pos, int curve) {
            cfg.numAssignments = 0;
            for (int n = 0; n < pos - 1; ++n) filler (cfg, n);
            audible (cfg, 250, curve);
            build (acc, cfg, &cs);
            return acc.lookup (refN (250), -1.0f); };
        const float r1 = at (1, -1), r41 = at (41, -1), r200 = at (200, -1), r200c = at (200, 199);
        char b[200];
        std::snprintf (b, sizeof b, "knob %.3f at position 41, %.3f at position 1", r41, r1);          gate ("route 41 moves its knob exactly as route 1 does (past the old page cap of 32)", std::fabs (r41 - r1) < 1e-6f && r1 > 0.55f, b);
        std::snprintf (b, sizeof b, "knob %.3f at position 200, %.3f at position 1", r200, r1);        gate ("route 200 moves its knob exactly as route 1 does (past the old engine cap of 128)", std::fabs (r200 - r1) < 1e-6f, b);
        std::snprintf (b, sizeof b, "knob %.3f through the curve in slot 200, %.3f straight", r200c, r200); gate ("the curve in slot 200 bends route 200 (curve slots past the old 32)", r200c < r200 - 0.1f, b);
    }

    std::printf ("\n[E. the early-out]\n");
    {
        cfg.numAssignments = 0; build (acc, cfg, nullptr);
        gate ("an empty matrix opens no slot", acc.count == 0);
    }
    std::printf ("\n  %d passed, %d failed\n\n", pass, fail);
    return fail ? 1 : 0;
}
