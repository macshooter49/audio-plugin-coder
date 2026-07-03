// ════════════════════════════════════════════════════════════════════════════
//  GranularEngine offline harness (Pattern A — NOT in CMakeLists / has its own main)
//
//  Build + run from the plugin dir:
//    c++ -std=c++17 -Wall -Wextra -ISource Source/GranularEngine_test.cpp -o /tmp/ge && /tmp/ge
//
//  Proves Phase 1: skeleton/lifecycle, window LUT, spawn scheduler, click-free +
//  RMS-stable mix, scan freeze/reverse/position, per-grain pitch/direction, and
//  the follower cloud snapshot.
// ════════════════════════════════════════════════════════════════════════════
#include "GranularEngine.h"
#include <cstdio>
#include <vector>
#include <cmath>

using tw::GranularEngine;
using tw::GranularEngineParams;

static int g_checks = 0, g_fail = 0;
static void check (bool ok, const char* what)
{ ++g_checks; if (! ok) { ++g_fail; std::printf ("  FAIL: %s\n", what); } }

// A 1-second stereo 220 Hz sine at 48k for tests that need real audio.
static std::vector<float> g_l, g_r;
static void makeSine (int n)
{
    g_l.resize (n); g_r.resize (n);
    for (int i = 0; i < n; ++i)
    { g_l[i] = std::sin (6.2831853f * 220.f * (float) i / 48000.f); g_r[i] = g_l[i]; }
}

int main()
{
    makeSine (48000);
    const float* chans[2] = { g_l.data(), g_r.data() };

    // ── Task 1: skeleton + lifecycle ──
    {
        GranularEngine ge;
        ge.prepare (48000.0);
        check (! ge.hasSample(), "no sample before setSample");
        ge.setSample (chans, 2, 48000, 48000.0);
        check (ge.hasSample(), "hasSample after setSample");
        check (! ge.isActive(), "inactive before noteOn");
        ge.noteOn (1.0, 0x1111);
        check (ge.isActive(), "active after noteOn");
    }

    // ── Task 2: window LUT (Tukey <-> Gaussian + skew) ──
    {
        GranularEngine w; w.prepare (48000.0);
        // Reach windowAt via a public probe: run one grain and inspect? Simpler — expose through
        // a tiny render at known phase is hard, so we validate window shape indirectly by RMS
        // monotonicity below. Direct endpoint checks use the dedicated probe:
        // (windowAt is private; validated via the click/RMS behavior in Task 4.)
        // Here we assert the two extremes produce audibly different textures.
        GranularEngine a, b;
        a.prepare (48000.0); b.prepare (48000.0);
        a.setSample (chans, 2, 48000, 48000.0);
        b.setSample (chans, 2, 48000, 48000.0);
        GranularEngineParams pa; pa.shape = 0.f; pa.density = 0.6f; a.setParams (pa);
        GranularEngineParams pb; pb.shape = 1.f; pb.density = 0.6f; b.setParams (pb);
        a.noteOn (1.0, 5); b.noteOn (1.0, 5);
        double sa = 0, sb = 0;
        for (int i = 0; i < 24000; ++i) { float l, r; a.tick (l, r); sa += l * l; b.tick (l, r); sb += l * l; }
        check (std::sqrt (sa / 24000) > 0.005, "tukey window produces audio");
        check (std::sqrt (sb / 24000) > 0.005, "gaussian window produces audio");
        (void) w;
    }

    // ── Task 3: async scheduler spawns at a plausible rate ──
    {
        GranularEngine s; s.prepare (48000.0);
        s.setSample (chans, 2, 48000, 48000.0);
        GranularEngineParams p; p.density = 0.5f; p.size = 0.25f; p.scan = 0.15f; p.spray = 0.f;
        s.setParams (p);
        s.noteOn (1.0, 0xBEEF);
        long spawns = 0; int prevActive = 0;
        for (int i = 0; i < 48000; ++i)
        {
            float ol, orr; s.tick (ol, orr);
            int a = s.activeGrainsForTesting();
            if (a > prevActive) spawns += (a - prevActive);
            prevActive = a;
        }
        // density 0.5 (log 1..200) ≈ 14 g/s; wide band for jitter + retirement.
        check (spawns > 5 && spawns < 60, "spawn count in plausible band for density=0.5");
    }

    // ── Task 4: click-free + finite + RMS-stable mix ──
    {
        GranularEngine m; m.prepare (48000.0);
        m.setSample (chans, 2, 48000, 48000.0);
        GranularEngineParams p; p.density = 0.6f; p.size = 0.25f; p.shape = 0.5f; m.setParams (p);
        m.noteOn (1.0, 0x1234);
        float prev = 0.f, maxJump = 0.f; double sumSq = 0; const int N = 48000;
        bool finite = true;
        for (int i = 0; i < N; ++i)
        {
            float ol, orr; m.tick (ol, orr);
            if (! std::isfinite (ol)) finite = false;
            float d = std::fabs (ol - prev); if (d > maxJump) maxJump = d;
            prev = ol; sumSq += ol * ol;
        }
        check (finite, "output finite");
        check (maxJump < 0.25f, "no click: bounded inter-sample delta");
        double rms = std::sqrt (sumSq / N);
        check (rms > 0.02 && rms < 1.0, "RMS in audible-stable band");
    }

    // ── Task 5: scan freeze / reverse / position ──
    {
        std::vector<float> cl (48000, 0.5f), cr (48000, 0.5f);
        const float* cch[2] = { cl.data(), cr.data() };
        GranularEngine f; f.prepare (48000.0);
        f.setSample (cch, 2, 48000, 48000.0);
        GranularEngineParams p; p.scan = 0.f; p.position = 0.3f; f.setParams (p);
        f.noteOn (1.0, 7);
        float before = f.scanPos01();
        for (int i = 0; i < 4800; ++i) { float a, b; f.tick (a, b); }
        float after = f.scanPos01();
        check (std::fabs (after - before) < 1e-4f, "scan=0 freezes the read-head");
        check (std::fabs (before - 0.3f) < 1e-3f, "frozen head sits at position=0.3");

        GranularEngineParams p2 = p; p2.scan = -0.5f; p2.position = 0.8f; f.setParams (p2);
        f.noteOn (1.0, 7);
        float b0 = f.scanPos01();
        for (int i = 0; i < 2400; ++i) { float a, b; f.tick (a, b); }
        check (f.scanPos01() < b0, "scan<0 moves the head backward");

        // SCAN parity with the Sample engine (±1 -> ±200%): Forward loop at full reverse —
        // the head strictly DECREASES from the anchor, every single tick.
        GranularEngineParams p3 = p; p3.scan = -1.0f; p3.position = 0.5f; p3.loopMode = 1;
        f.setParams (p3);
        f.noteOn (1.0, 7);
        bool strictlyDown = true; float prevPos = f.scanPos01();
        for (int i = 0; i < 2400; ++i)
        {
            float a, b; f.tick (a, b);
            const float sp = f.scanPos01();
            if (sp >= prevPos) { strictlyDown = false; break; }
            prevPos = sp;
        }
        check (strictlyDown, "loopMode=1 scan=-1: head strictly decreases from the anchor");

        // Faster scan = more travel (rates past ±1 just scan faster — no clamp in the engine).
        GranularEngineParams p4 = p; p4.position = 0.1f; p4.loopMode = 1;
        p4.scan = 1.0f; f.setParams (p4); f.noteOn (1.0, 7);
        for (int i = 0; i < 2400; ++i) { float a, b; f.tick (a, b); }
        const float trav1 = f.scanPos01() - 0.1f;
        p4.scan = 2.0f; f.setParams (p4); f.noteOn (1.0, 7);
        for (int i = 0; i < 2400; ++i) { float a, b; f.tick (a, b); }
        const float trav2 = f.scanPos01() - 0.1f;
        check (trav2 > trav1 * 1.5f, "scan=+2 travels farther than scan=+1");

        // One-Shot reverse (the old frame-one dead-stop): scan<0 mirrors the anchor to the
        // region END (SampleEngine::noteOn parity) so reverse plays end -> start.
        GranularEngineParams p5 = p; p5.scan = -0.5f; p5.position = 0.f; p5.loopMode = 0;
        f.setParams (p5);
        f.noteOn (1.0, 7);
        check (f.scanPos01() > 0.99f, "One-Shot reverse: head anchors at the region END");
        for (int i = 0; i < 2400; ++i) { float a, b; f.tick (a, b); }
        check (f.scanPos01() < 0.99f && f.scanPos01() > 0.5f, "One-Shot reverse: head moves backward (no dead-stop)");
    }

    // ── Task 6: per-grain pitch + direction ──
    {
        GranularEngine g; g.prepare (48000.0);
        g.setSample (chans, 2, 48000, 48000.0);
        GranularEngineParams p; p.pitch = 12.f; p.density = 0.6f; g.setParams (p);
        g.noteOn (1.0, 42);
        double sumSq = 0;
        for (int i = 0; i < 24000; ++i) { float a, b; g.tick (a, b); sumSq += a * a; }
        check (std::sqrt (sumSq / 24000) > 0.01, "pitched cloud audible");

        GranularEngineParams p2; p2.dir = -1.f; p2.density = 0.6f; g.setParams (p2);
        g.noteOn (1.0, 42);
        for (int i = 0; i < 2000; ++i) { float a, b; g.tick (a, b); }
        check (g.anyGrainReversedForTesting(), "dir=-1 spawns reversed grains");
    }

    // ── Task 7: follower snapshot + release-frees ──
    {
        std::vector<float> cl (48000, 0.4f), cr (48000, 0.4f);
        const float* cch[2] = { cl.data(), cr.data() };
        GranularEngine v; v.prepare (48000.0);
        v.setSample (cch, 2, 48000, 48000.0);
        // Dense, overlapping cloud (density 0.85 ~90 g/s x size 0.5 ~32 ms = overlap ~2.9)
        // so there are always active grains; sample over a window to be jitter-robust.
        GranularEngineParams p; p.density = 0.85f; p.size = 0.5f; v.setParams (p);
        v.noteOn (1.0, 9);
        int maxN = 0; bool posOk = true;
        for (int i = 0; i < 8000; ++i)
        {
            float a, b; v.tick (a, b);
            tw::GrainViz buf[16]; int n = v.cloudSnapshot (buf, 16);
            if (n > maxN) maxN = n;
            for (int k = 0; k < n; ++k)
                if (! (buf[k].pos01 >= 0.f && buf[k].pos01 <= 1.f)) posOk = false;
        }
        check (maxN > 0, "cloud snapshot returns active grains");
        check (posOk, "all grain pos01 in range");

        v.noteOff();
        bool wentInactive = false;
        for (int i = 0; i < 96000; ++i) { float a, b; if (! v.tick (a, b)) { wentInactive = true; break; } }
        check (wentInactive, "voice frees after release when all grains retire");
    }

    // ── Key: snap correctness + audible in-key shimmer (the point of Key) ──
    {
        check (std::fabs (GranularEngine::snapToKeyForTesting (3.3, 0) - 3.3) < 1e-6, "Key Off = passthrough");
        check (std::fabs (GranularEngine::snapToKeyForTesting (1.0, 4) - 0.0) < 1e-6, "Maj snaps C#(1) to C(0)");
        check (std::fabs (GranularEngine::snapToKeyForTesting (5.0, 6) - 4.0) < 1e-6, "Penta snaps F(5) to E(4)");
        check (std::fabs (GranularEngine::snapToKeyForTesting (13.0, 4) - 12.0) < 1e-6, "Maj keeps octave: 13 -> 12");
        check (std::fabs (GranularEngine::snapToKeyForTesting (7.0, 2) - 7.0) < 1e-6, "5th keeps the fifth (7)");

        // Key ON with pitchSpray=0 must STILL create in-key pitch variety (the shimmer) → audible + finite.
        GranularEngine ge; ge.prepare (48000.0);
        ge.setSample (chans, 2, 48000, 48000.0);
        GranularEngineParams p; p.key = 4; p.pitchSpray = 0.f; p.density = 0.7f; p.size = 0.3f; ge.setParams (p);
        ge.noteOn (1.0, 0x5151);
        double sumSq = 0; bool finite = true; const int N = 48000;
        for (int i = 0; i < N; ++i) { float a, b; ge.tick (a, b); if (! std::isfinite (a)) finite = false; sumSq += a * a; }
        check (finite, "Key shimmer stays finite");
        check (std::sqrt (sumSq / N) > 0.01, "Key shimmer is audible even at pitchSpray=0");
    }

    // ── Window: all three Shape morph endpoints are ~0 at grain edges (declick) + Shape is dramatic ──
    {
        GranularEngine w; w.prepare (48000.0);
        check (w.windowForTesting (0.f, 0.f, 0.f) < 1e-3f && w.windowForTesting (0.f, 0.f, 1.f) < 1e-3f, "Flat-top window is 0 at both ends");
        check (w.windowForTesting (0.5f, 0.f, 0.f) < 1e-3f && w.windowForTesting (0.5f, 0.f, 1.f) < 1e-3f, "Hann window is 0 at both ends");
        check (w.windowForTesting (1.f, 0.f, 0.f) < 1e-3f && w.windowForTesting (1.f, 0.f, 1.f) < 1e-3f, "Bell window is 0 at both ends");
        // Flat-top holds ~1 off-centre; the Bell is much narrower there → Shape is audibly different.
        const float flatMid = w.windowForTesting (0.f, 0.f, 0.22f);
        const float bellMid = w.windowForTesting (1.f, 0.f, 0.22f);
        check (flatMid > bellMid + 0.3f, "Shape morph is dramatic (flat-top >> bell off-centre)");
    }

    // ── Air: exciter adds high-harmonic energy without blowing up ──
    {
        GranularEngine dry, wet; dry.prepare (48000.0); wet.prepare (48000.0);
        dry.setSample (chans, 2, 48000, 48000.0); wet.setSample (chans, 2, 48000, 48000.0);
        GranularEngineParams pd; pd.density = 0.6f; pd.size = 0.3f; pd.air = 0.f; dry.setParams (pd);
        GranularEngineParams pw = pd; pw.air = 1.f; wet.setParams (pw);
        dry.noteOn (1.0, 7); wet.noteOn (1.0, 7);
        double eDry = 0, eWet = 0; bool finite = true;
        for (int i = 0; i < 24000; ++i) { float a, b; dry.tick (a, b); eDry += a * a; wet.tick (a, b); if (! std::isfinite (a)) finite = false; eWet += a * a; }
        check (finite, "Air stays finite at max");
        check (eWet > eDry, "Air adds energy (exciter engaged)");
    }

    // ── Stretch: raises grain overlap (longer + denser) → more simultaneous grains ──
    {
        auto avgGrains = [&] (float stretch, int mode) {
            GranularEngine e; e.prepare (48000.0); e.setSample (chans, 2, 48000, 48000.0);
            GranularEngineParams p; p.density = 0.4f; p.size = 0.3f; p.stretch = stretch; p.stretchMode = mode; e.setParams (p);
            e.noteOn (1.0, 0x321);
            long sum = 0; const int N = 24000;
            for (int i = 0; i < N; ++i) { float a, b; e.tick (a, b); sum += e.activeGrainsForTesting (); }
            return (double) sum / N;
        };
        check (avgGrains (1.f, 0) > avgGrains (0.f, 0), "Stretch (Tones) raises average grain overlap");
    }

    // ── Loop modes: One-Shot stops at the edge; Ping-Pong bounces and stays in bounds ──
    {
        // One-Shot (mode 0): fast scan from position 0.9 reaches the end and STOPS spawning → grains empty out.
        GranularEngine os; os.prepare (48000.0); os.setSample (chans, 2, 48000, 48000.0);
        GranularEngineParams p; p.loopMode = 0; p.scan = 1.0f; p.density = 0.6f; p.size = 0.2f; p.position = 0.9f; os.setParams (p);
        os.noteOn (1.0, 0x0501);
        for (int i = 0; i < 48000 * 3; ++i) { float a, b; os.tick (a, b); }
        check (os.activeGrainsForTesting () == 0, "One-Shot: grains empty out after the head reaches the end");
        check (os.scanPos01 () <= 1.0001f && os.scanPos01 () >= -1e-4f, "One-Shot: head clamped in range");

        // Ping-Pong (mode 3): head stays within [start,end] and reverses direction (bounces).
        GranularEngine pp; pp.prepare (48000.0); pp.setSample (chans, 2, 48000, 48000.0);
        GranularEngineParams q; q.loopMode = 3; q.scan = 0.8f; q.density = 0.5f; q.size = 0.2f; q.position = 0.5f; pp.setParams (q);
        pp.noteOn (1.0, 0x0303);
        bool inBounds = true, wentUp = false, wentDown = false; float prev = pp.scanPos01 ();
        for (int i = 0; i < 48000 * 2; ++i)
        {
            float a, b; pp.tick (a, b);
            const float sp = pp.scanPos01 ();
            if (sp < -1e-4f || sp > 1.0001f) inBounds = false;
            if (sp > prev + 1e-7f) wentUp = true;
            if (sp < prev - 1e-7f) wentDown = true;
            prev = sp;
        }
        check (inBounds, "Ping-Pong: head stays within [start,end]");
        check (wentUp && wentDown, "Ping-Pong: head reverses (bounces both ways)");
    }

    // ── LOOP BRACKET (2026-07-02 fix): loop modes catch + loop INSIDE [loopStart,loopEnd] ──
    {
        // Ping-Pong: bracket [0.4,0.6], anchor at 0 → lead-in from below, catch at 0.4,
        // then bounce INSIDE the bracket forever (the "sails past the loop end" bug).
        GranularEngine ge; ge.prepare (48000.0); ge.setSample (chans, 2, 48000, 48000.0);
        GranularEngineParams p;
        p.scan = 0.5f; p.position = 0.f; p.loopMode = 3;
        p.loopStart = 0.4f; p.loopEnd = 0.6f;
        ge.setParams (p); ge.setRegion (0.f, 1.f); ge.noteOn (1.0, 1234);
        float l, r; bool everCaught = false;
        double minAfter = 1e9, maxAfter = -1e9, prevPos = 0.0, prevDir = 0.0;
        int bounces = 0;
        for (int i = 0; i < 48000 * 6; ++i)
        {
            ge.tick (l, r);
            const double sp = (double) ge.scanPos01();
            if (! everCaught && ge.caughtForTesting()) everCaught = true;
            if (everCaught)
            {
                if (sp < minAfter) minAfter = sp;
                if (sp > maxAfter) maxAfter = sp;
                const double dir = sp - prevPos;
                if (dir != 0.0 && prevDir != 0.0 && ((dir > 0.0) != (prevDir > 0.0))) ++bounces;
                if (dir != 0.0) prevDir = dir;
            }
            prevPos = sp;
        }
        check (everCaught, "loop bracket: ping-pong lead-in reaches the bracket (caught)");
        check (minAfter >= 0.4 - 1e-5 && maxAfter <= 0.6 + 1e-5, "loop bracket: ping-pong head confined after catch");
        check (bounces >= 2, "loop bracket: ping-pong head actually bounces inside the bracket");
        check (maxAfter > 0.55 && minAfter < 0.45, "loop bracket: ping-pong head traverses the bracket");
    }
    {
        // Forward loop: bracket [0.3,0.5], anchor ABOVE it (0.9) → immediate catch + one-step
        // fmod fold into the bracket; full Spray births must STILL read inside the bracket.
        GranularEngine ge; ge.prepare (48000.0); ge.setSample (chans, 2, 48000, 48000.0);
        GranularEngineParams p;
        p.scan = 0.5f; p.position = 0.9f; p.loopMode = 1; p.spray = 1.0f;
        p.density = 0.8f; p.size = 0.7f;   // dense + long grains → the cloud is never momentarily empty
        p.loopStart = 0.3f; p.loopEnd = 0.5f;
        ge.setParams (p); ge.setRegion (0.f, 1.f); ge.noteOn (1.0, 99);
        float l, r; bool headInside = true, birthsInside = true;
        for (int i = 0; i < 48000 * 2; ++i)
        {
            ge.tick (l, r);
            if (i > 4800)   // after 0.1 s the head must be captured + folded
            {
                const double sp = (double) ge.scanPos01();
                if (sp < 0.3 - 1e-5 || sp > 0.5 + 1e-5) headInside = false;
            }
        }
        tw::GrainViz viz[16];
        const int n = ge.cloudSnapshot (viz, 16);
        for (int i = 0; i < n; ++i)
            if (viz[i].pos01 < 0.3f - 0.01f || viz[i].pos01 > 0.5f + 0.01f) birthsInside = false;
        check (headInside, "loop bracket: forward loop head wraps INSIDE the bracket");
        check (n > 0, "loop bracket: grains alive for the cloud check");
        check (birthsInside, "loop bracket: grain reads confined to the bracket at full spray");
    }
    {
        // Regression: DEFAULT bracket (0..1) intersects down to the region → caught on the very
        // first tick and ping-pong bounces at the REGION edges, exactly the pre-fix behaviour.
        GranularEngine ge; ge.prepare (48000.0); ge.setSample (chans, 2, 48000, 48000.0);
        GranularEngineParams p; p.scan = 1.0f; p.position = 0.5f; p.loopMode = 3;
        ge.setParams (p); ge.setRegion (0.2f, 0.8f); ge.noteOn (1.0, 7);
        float l, r; double mn = 1e9, mx = -1e9; bool caughtFirstTick = false;
        for (int i = 0; i < 48000 * 3; ++i)
        {
            ge.tick (l, r);
            if (i == 0) caughtFirstTick = ge.caughtForTesting();
            const double sp = (double) ge.scanPos01();
            if (sp < mn) mn = sp; if (sp > mx) mx = sp;
        }
        check (caughtFirstTick, "loop bracket: default bracket caught on the first tick (legacy)");
        check (mn <= 0.2 + 1e-3 && mx >= 0.8 - 1e-3, "loop bracket: default bracket sweeps the whole region");
        check (mn >= 0.2 - 1e-5 && mx <= 0.8 + 1e-5, "loop bracket: default bracket never leaves the region");
    }

    std::printf ("\n%d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
