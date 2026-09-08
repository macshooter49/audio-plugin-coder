// =============================================================================
//  FlowDrift_test.cpp — offline proof for the DRIFT generative mod source
//  g++ -std=c++17 -Wall -Wextra -ISource Source/FlowDrift_test.cpp -o /tmp/fd && /tmp/fd
//
//  A mod source has two cardinal rules: stay BOUNDED (never blow up the knob it
//  drives) and be ZIPPER-FREE when smoothed (GATE high => tiny per-sample change).
//  Plus the generative contract: déjà-vu locks a repeatable loop, lanes are
//  bipolar/centered and decorrelated, MORPH scales depth, same seed is reproducible.
// =============================================================================
#include "FlowDrift.h"
#include <cstdio>
#include <vector>
#include <cmath>

using namespace wc;

static int g_checks = 0, g_fail = 0;
static void check (bool ok, const char* what) { ++g_checks; if (! ok) { ++g_fail; std::printf ("  FAIL: %s\n", what); } }

constexpr double BPM = 120, SR = 48000;
const char* SHAPE_NM[9] = { "Walk", "SampleHold", "Smooth", "Drunk", "Value", "Chaos", "Human", "Pink", "Woggle" };

struct DRun { std::vector<float> lane[kDriftLanes]; int nLanes = 0; };

static DRun driveD (int nLanes, int shapeOv, float dejavu, uint32_t seed,
                    float rate, float gate, float vary, float traj, float morph,
                    int nBlocks, int blk = 512, bool bipolar = true)
{
    FlowDrift d; d.prepare (SR); d.setNumLanes (nLanes); d.setShapeOverride (shapeOv);
    d.setDejavu (dejavu); d.setSeed (seed); d.setLoopLen (4); d.setBipolar (bipolar); d.reset();
    DRun r; r.nLanes = nLanes;
    std::vector<float> out ((size_t) nLanes * (size_t) blk);
    double ppq = 0; const double pps = (BPM / 60.0) / SR;
    for (int b = 0; b < nBlocks; ++b)
    {
        d.process (rate, gate, vary, traj, morph, ppq, BPM, SR, out.data(), blk, true);
        for (int l = 0; l < nLanes; ++l)
            for (int i = 0; i < blk; ++i) r.lane[l].push_back (out[(size_t) l * (size_t) blk + (size_t) i]);
        ppq += pps * blk;
    }
    return r;
}

// sample value(lane) once per step (stepLen-sized chunks) -> per-step sequence
static std::vector<float> stepSeq (int shapeOv, float dejavu, uint32_t seed, float gate, float vary, int nSteps)
{
    FlowDrift d; d.prepare (SR); d.setNumLanes (4); d.setShapeOverride (shapeOv);
    d.setDejavu (dejavu); d.setSeed (seed); d.setLoopLen (4); d.reset();
    std::vector<float> seq; std::vector<float> out (4 * 6000);
    double ppq = 0; const double pps = (BPM / 60.0) / SR;
    for (int s = 0; s < nSteps; ++s) { d.process (0.6111f, gate, vary, 0.0f, 1.0f, ppq, BPM, SR, out.data(), 6000, true); ppq += pps * 6000; seq.push_back (d.value (0)); }
    return seq;
}

static float maxDelta (const std::vector<float>& v, size_t from) { float m = 0.f; for (size_t i = from + 1; i < v.size(); ++i) m = std::max (m, std::fabs (v[i] - v[i-1])); return m; }
static float maxAbs   (const std::vector<float>& v, size_t from) { float m = 0.f; for (size_t i = from; i < v.size(); ++i) m = std::max (m, std::fabs (v[i])); return m; }
static float meanOf   (const std::vector<float>& v, size_t from) { double s = 0; size_t n = 0; for (size_t i = from; i < v.size(); ++i) { s += v[i]; ++n; } return n ? (float) (s / (double) n) : 0.f; }

int main()
{
    std::printf ("FlowDrift engine — generative mod source proof (BOUNDED + ZIPPER-FREE)\n");

    // ── T1: BOUNDED — every lane stays in [-1,1] under every shape (bipolar) ────
    {
        for (int sh = 0; sh < kDriftShapeN; ++sh)
        {
            DRun r = driveD (4, sh, 0.0f, 11, 0.5f, /*gate*/0.4f, /*vary*/1.0f, 0.0f, /*morph*/1.0f, 80);
            bool inRange = true;
            for (int l = 0; l < 4; ++l) for (float x : r.lane[l]) if (x < -1.0001f || x > 1.0001f) inRange = false;
            char buf[80]; std::snprintf (buf, sizeof buf, "T1 bounded [-1,1]: %s", SHAPE_NM[sh]);
            check (inRange, buf);
        }
    }

    // ── T2: BOUNDED unipolar — every lane stays in [0,1] ───────────────────────
    {
        DRun r = driveD (4, -1, 0.0f, 5, 0.5f, 0.4f, 1.0f, 0.5f, 1.0f, 80, 512, /*bipolar*/false);
        bool inRange = true; for (int l = 0; l < 4; ++l) for (float x : r.lane[l]) if (x < -0.0001f || x > 1.0001f) inRange = false;
        check (inRange, "T2 bounded [0,1] unipolar");
    }

    // ── T3: ZIPPER-FREE — GATE=1 gives tiny per-sample delta on every shape ────
    {
        for (int sh = 0; sh < kDriftShapeN; ++sh)
        {
            DRun r = driveD (1, sh, 0.0f, 7, 0.5f, /*gate*/1.0f, 1.0f, 0.0f, 1.0f, 120);
            const float md = maxDelta (r.lane[0], 20000);
            char buf[96]; std::snprintf (buf, sizeof buf, "T3 zipper-free GATE=1: %s (maxDelta=%.5f)", SHAPE_NM[sh], md);
            check (md < 0.01f, buf);
        }
    }

    // ── T4: GATE matters — SampleHold steps hard at GATE=0 (intentional S&H) ────
    {
        DRun r = driveD (1, 1 /*SampleHold*/, 0.0f, 7, 0.5f, /*gate*/0.0f, 1.0f, 0.0f, 1.0f, 120);
        check (maxDelta (r.lane[0], 20000) > 0.1f, "T4 GATE=0 SampleHold steps (S&H works)");
    }

    // ── T5: déjà-vu (two-sided) — 0.5 locks a repeatable loop; 0 evolves ───────
    {
        auto locked = stepSeq (1 /*SampleHold*/, 0.5f, 999, 0.3f, 0.9f, 16);
        auto eqLoop = [&](const std::vector<float>& v, int a, int b){ for (int k=0;k<4;++k) if (std::fabs (v[(size_t)(a*4+k)] - v[(size_t)(b*4+k)]) > 1e-4f) return false; return true; };
        check (eqLoop (locked, 1, 2) && eqLoop (locked, 2, 3), "T5 dejavu=0.5 locks a repeatable loop (the notch)");
        auto varied = stepSeq (1, 0.0f, 999, 0.3f, 0.9f, 16);
        check (! (eqLoop (varied, 1, 2) && eqLoop (varied, 2, 3)), "T5 dejavu=0 evolves (no lock)");
    }

    // ── T6: bipolar & centered — symmetric shapes span ± with mean ~ 0 ─────────
    {
        DRun r = driveD (1, 1 /*SampleHold*/, 0.0f, 3, 0.7f, 0.3f, 1.0f, 0.0f, 1.0f, 400);
        bool pos = false, neg = false; for (float x : r.lane[0]) { if (x > 0.2f) pos = true; if (x < -0.2f) neg = true; }
        check (pos && neg, "T6 output spans both polarities");
        check (std::fabs (meanOf (r.lane[0], 5000)) < 0.15f, "T6 mean ~ 0 (centered) over long run");
    }

    // ── T7: lane decorrelation — independent streams differ ────────────────────
    {
        DRun r = driveD (4, 1 /*SampleHold*/, 0.0f, 21, 0.5f, 0.4f, 1.0f, 0.0f, 1.0f, 200);
        // Pearson correlation lane0 vs lane1
        size_t from = 5000, n = 0; double m0=0,m1=0; for (size_t i=from;i<r.lane[0].size();++i){ m0+=r.lane[0][i]; m1+=r.lane[1][i]; ++n; } m0/=n; m1/=n;
        double cov=0,v0=0,v1=0; for (size_t i=from;i<r.lane[0].size();++i){ double a=r.lane[0][i]-m0,b=r.lane[1][i]-m1; cov+=a*b; v0+=a*a; v1+=b*b; }
        double corr = (v0>0&&v1>0) ? cov/std::sqrt(v0*v1) : 0.0;
        char buf[80]; std::snprintf (buf, sizeof buf, "T7 lanes decorrelated (corr=%.3f)", corr);
        check (std::fabs (corr) < 0.8, buf);
        // also: lanes are not identical sample-for-sample
        bool diff = false; for (size_t i = from; i < r.lane[0].size(); ++i) if (std::fabs (r.lane[0][i] - r.lane[1][i]) > 1e-3f) { diff = true; break; }
        check (diff, "T7 lanes are distinct");
    }

    // ── T8: depth (MORPH) scales output amplitude ──────────────────────────────
    {
        DRun lo = driveD (1, 1, 0.0f, 4, 0.5f, 0.3f, 1.0f, 0.0f, /*morph*/0.0f, 120);
        DRun hi = driveD (1, 1, 0.0f, 4, 0.5f, 0.3f, 1.0f, 0.0f, /*morph*/1.0f, 120);
        check (maxAbs (lo.lane[0], 5000) < 0.05f, "T8 MORPH=0 -> ~no motion (centered)");
        check (maxAbs (hi.lane[0], 5000) > 0.5f,  "T8 MORPH=1 -> full-range motion");
    }

    // ── T9: determinism — same seed reproduces the sequence ────────────────────
    {
        auto a = stepSeq (4 /*Perlin*/, 0.0f, 12345, 0.5f, 0.8f, 24);
        auto b = stepSeq (4, 0.0f, 12345, 0.5f, 0.8f, 24);
        bool same = a.size() == b.size(); for (size_t i = 0; same && i < a.size(); ++i) if (std::fabs (a[i] - b[i]) > 1e-6f) same = false;
        check (same, "T9 same seed -> identical sequence");
        auto c = stepSeq (4, 0.0f, 6789, 0.5f, 0.8f, 24);
        bool diff = false; for (size_t i = 0; i < a.size() && i < c.size(); ++i) if (std::fabs (a[i] - c[i]) > 1e-4f) { diff = true; break; }
        check (diff, "T9 different seed -> different sequence");
    }

    // ── T10: rate response — faster RATE produces more motion per unit time ────
    {
        DRun slow = driveD (1, 1 /*SampleHold*/, 0.0f, 8, /*rate*/0.2f, 0.0f, 1.0f, 0.0f, 1.0f, 200);
        DRun fast = driveD (1, 1,               0.0f, 8, /*rate*/0.8f, 0.0f, 1.0f, 0.0f, 1.0f, 200);
        // count significant jumps (S&H changes) — faster grid => more
        auto jumps = [](const std::vector<float>& v){ int c=0; for (size_t i=1;i<v.size();++i) if (std::fabs(v[i]-v[i-1])>0.05f) ++c; return c; };
        check (jumps (fast.lane[0]) > jumps (slow.lane[0]), "T10 faster RATE -> more changes");
    }

    // ── T11: shapes differ — different TRAJ yields different motion ────────────
    {
        auto walk = stepSeq (0 /*Walk*/, 0.0f, 50, 0.4f, 1.0f, 24);
        auto chao = stepSeq (5 /*Chaos*/, 0.0f, 50, 0.4f, 1.0f, 24);
        bool diff = false; for (size_t i = 0; i < walk.size() && i < chao.size(); ++i) if (std::fabs (walk[i] - chao[i]) > 1e-3f) { diff = true; break; }
        check (diff, "T11 different shapes -> different output");
    }

    // ── T12: safety — finite + bounded under full aggression; reset clears ─────
    {
        FlowDrift d; d.prepare (SR); d.setNumLanes (8); d.setSeed (777); d.setLoopLen (3);
        std::vector<float> out (8 * 512); double ppq = 0; const double pps=(BPM/60.0)/SR; bool fin = true, bnd = true;
        for (int b = 0; b < 400; ++b)
        {
            const int sh = b % kDriftShapeN; d.setShapeOverride (sh);
            d.process (0.6f, 0.5f, 1.0f, 0.0f, 1.0f, ppq, BPM, SR, out.data(), 512, true);
            for (float x : out) { if (!(x==x)) fin = false; if (std::fabs (x) > 1.0001f) bnd = false; }
            ppq += pps*512;
        }
        check (fin, "T12 finite (no NaN) under full aggression");
        check (bnd, "T12 bounded under full aggression");
        d.reset(); check (std::fabs (d.value (0)) < 1e-6f && std::fabs (d.value (7)) < 1e-6f, "T12 reset clears lane values");
    }

    // ── T13: HUMAN (Ornstein-Uhlenbeck) — bounded, centered, and actually moves ─
    {
        DRun r = driveD (1, 6 /*Human*/, 0.0f, 31, 0.5f, /*gate*/0.5f, /*vary*/0.9f, 0.0f, 1.0f, 400);
        bool inRange = true; for (float x : r.lane[0]) if (x < -1.0001f || x > 1.0001f) inRange = false;
        check (inRange, "T13 Human bounded [-1,1]");
        check (std::fabs (meanOf (r.lane[0], 8000)) < 0.20f, "T13 Human mean-reverts (centered ~0)");
        check (maxAbs (r.lane[0], 8000) > 0.15f, "T13 Human breathes (non-trivial motion)");
    }

    // ── T14: SPREAD/BIAS distribution shaping stays BOUNDED at the extremes ─────
    {
        for (int sh = 0; sh < kDriftShapeN; ++sh)
        {
            FlowDrift d; d.prepare (SR); d.setNumLanes (1); d.setShapeOverride (sh); d.setSeed (123);
            d.setSpread (1.0f); d.setBias (1.0f);              // extremes + full positive skew
            std::vector<float> out (6000); double ppq = 0; const double pps=(BPM/60.0)/SR; bool bnd = true;
            for (int b = 0; b < 30; ++b) { d.process (0.5f, 0.3f, 1.0f, 0.0f, 1.0f, ppq, BPM, SR, out.data(), 6000, true); for (float x: out) if (std::fabs(x) > 1.0001f) bnd = false; ppq += pps*6000; }
            d.setSpread (0.0f); d.setBias (0.0f);              // constant + full negative skew
            for (int b = 0; b < 10; ++b) { d.process (0.5f, 0.3f, 1.0f, 0.0f, 1.0f, ppq, BPM, SR, out.data(), 6000, true); for (float x: out) if (std::fabs(x) > 1.0001f) bnd = false; ppq += pps*6000; }
            char buf[80]; std::snprintf (buf, sizeof buf, "T14 spread/bias bounded: %s", SHAPE_NM[sh]);
            check (bnd, buf);
        }
    }

    // ── T15: SEEDED déjà-vu (the Marbles fix) — a LOCKED loop still responds to ─
    //         SPREAD live. Proves the lock regenerates from a seed, not a value snapshot.
    {
        auto runLockedSpread = [&](float spread){
            FlowDrift d; d.prepare (SR); d.setNumLanes (1); d.setShapeOverride (1 /*SampleHold*/);
            d.setSeed (4242); d.setLoopLen (4); d.setDejavu (0.5f); d.setSpread (spread); d.reset();
            std::vector<float> out (4*6000), seq; double ppq=0; const double pps=(BPM/60.0)/SR;
            for (int s=0;s<16;++s){ d.process (0.6111f, 0.2f, 0.9f, 0.0f, 1.0f, ppq, BPM, SR, out.data(), 6000, true); ppq+=pps*6000; seq.push_back (d.value (0)); }
            return seq;
        };
        auto narrow = runLockedSpread (0.40f);   // locked loop, mild spread
        auto wide   = runLockedSpread (0.95f);   // SAME locked loop, extreme spread
        // 1) each is still a repeatable loop (lock holds)
        auto eqLoop = [&](const std::vector<float>& v, int a, int b){ for (int k=0;k<4;++k) if (std::fabs (v[(size_t)(a*4+k)] - v[(size_t)(b*4+k)]) > 1e-4f) return false; return true; };
        check (eqLoop (wide, 2, 3), "T15 locked loop stays repeatable under spread");
        // 2) but the two spreads give DIFFERENT shaped loops -> value-snapshot would have made them identical
        bool differs = false; for (size_t i=8;i<narrow.size();++i) if (std::fabs (narrow[i] - wide[i]) > 0.05f) { differs = true; break; }
        check (differs, "T15 SPREAD reshapes a LOCKED loop (seeded regen, not frozen values)");
    }

    // ── T16: shuffle side — dejavu=1.0 draws past loops (varies) but stays bounded
    {
        DRun r = driveD (1, 1 /*SampleHold*/, 1.0f, 88, 0.5f, 0.3f, 0.9f, 0.0f, 1.0f, 200);
        bool inRange = true; for (float x : r.lane[0]) if (x < -1.0001f || x > 1.0001f) inRange = false;
        check (inRange, "T16 dejavu=1.0 (shuffle) stays bounded");
    }

    std::printf ("\n%d checks, %d failed\n", g_checks, g_fail);
    if (g_fail == 0) std::printf ("ALL %d CHECKS PASSED\n", g_checks);
    return g_fail == 0 ? 0 : 1;
}
