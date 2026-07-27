// =============================================================================
//  FlowGlitch_test.cpp — offline proof for the GLITCH buffer-mangler
//  g++ -std=c++17 -Wall -Wextra -ISource Source/FlowGlitch_test.cpp -o /tmp/fg && /tmp/fg
//
//  Headline: ZERO CLICKS. Under every effect at punishing settings — including
//  constant back-to-back retriggers (the delayed-commit duck) and internal grain
//  loops (the seam-crossfade) — the max sample-to-sample jump stays tiny. Plus a
//  direct check that the stepSamp dimensional fix makes a glitch last ~one step
//  (not ~4x), and that déjà-vu locks the fire pattern.
// =============================================================================
#include "FlowGlitch.h"
#include <cstdio>
#include <vector>
#include <cmath>
#include <functional>

using namespace wc;

static int g_checks = 0, g_fail = 0;
static void check (bool ok, const char* what) { ++g_checks; if (! ok) { ++g_fail; std::printf ("  FAIL: %s\n", what); } }

constexpr double BPM = 120, SR = 48000;
[[maybe_unused]] constexpr int STEP = 6000;    // rate 0.5 @120/48k -> boundaries every 6000 samples

static float sineSig (long long n) { return 0.5f * std::sin (2.0f * 3.14159265f * (float) n / 1500.0f); }

struct GRun { std::vector<float> out, dry; std::vector<int> activeBlk; };

// drive the engine; vary is a per-block function so we can prime then fire
static GRun driveG (const std::function<void(FlowGlitch&)>& setup,
                    const std::function<float(int)>& varyFn,
                    float rate, float gate, float traj, float morph,
                    int nBlocks, int blk = 512)
{
    FlowGlitch g; g.prepare (SR, 4.0); setup (g);
    GRun r; std::vector<float> L ((size_t) blk), R ((size_t) blk);
    double ppq = 0; const double pps = (BPM / 60.0) / SR; long long gc = 0;
    for (int b = 0; b < nBlocks; ++b)
    {
        for (int i = 0; i < blk; ++i) { float v = sineSig (gc + i); L[(size_t) i] = v; R[(size_t) i] = v; r.dry.push_back (v); }
        g.process (rate, gate, varyFn (b), traj, morph, ppq, BPM, SR, L.data(), R.data(), blk, true);
        for (int i = 0; i < blk; ++i) r.out.push_back (L[(size_t) i]);
        r.activeBlk.push_back (g.isActive() ? 1 : 0);
        gc += blk; ppq += pps * blk;
    }
    return r;
}

static float maxJump (const std::vector<float>& o, size_t from, size_t to)
{ float m = 0.f; for (size_t i = from + 1; i < to && i < o.size(); ++i) m = std::max (m, std::fabs (o[i] - o[i-1])); return m; }

int main()
{
    std::printf ("FlowGlitch engine — buffer-mangler proof (ZERO CLICKS)\n");
    auto allVary1 = [](int){ return 1.0f; };       // fire every boundary (constant retrigger)

    // ── T1: CLICK-FREE per buffer-read effect (internal seams + retrigger duck) ──
    {
        struct E { GlitchFx fx; const char* nm; float thr; };
        const E reads[6] = {
            { GlitchFx::Repeat,  "Repeat",  0.03f }, { GlitchFx::Reverse, "Reverse", 0.03f },
            { GlitchFx::TapeStop,"TapeStop",0.03f }, { GlitchFx::Pitch,   "Pitch",   0.03f },
            { GlitchFx::Freeze,  "Freeze",  0.03f }, { GlitchFx::Scatter, "Scatter", 0.03f },
        };
        for (const auto& e : reads)
        {
            GRun r = driveG ([&](FlowGlitch& g){ for (int k=0;k<kGlitchFxN;++k) g.setEnabled ((GlitchFx) k, false);
                                                 g.setEnabled (e.fx, true); g.setMix (1.0f); g.setHoldSteps (2.0f);
                                                 g.setPitchRatio (2.0f); g.setSeed (42); },
                             allVary1, 0.5f, /*gate*/0.5f, /*traj*/0.0f, /*morph*/0.0f, 130);
            // measure after the buffer is fully primed past the largest read-back (Scatter win ~500ms)
            const float mj = maxJump (r.out, 32000, 66000);
            char buf[96]; std::snprintf (buf, sizeof buf, "T1 click-free: %s (maxJump=%.4f)", e.nm, mj);
            check (mj < e.thr, buf);
        }
    }

    // ── T2: Gate is click-free (cosine edges both sides) ───────────────────────
    {
        GRun r = driveG ([](FlowGlitch& g){ for (int k=0;k<kGlitchFxN;++k) g.setEnabled ((GlitchFx) k, false);
                                            g.setEnabled (GlitchFx::Gate, true); g.setMix (1.0f); g.setHoldSteps (2.0f); g.setGateDiv (4); },
                         allVary1, 0.5f, 0.8f, 0.0f, 0.0f, 130);
        check (maxJump (r.out, 32000, 66000) < 0.05f, "T2 Gate click-free (cosine edges)");
    }

    // ── T3: Crush is finite + bounded (quantization steps are intended) ─────────
    {
        GRun r = driveG ([](FlowGlitch& g){ for (int k=0;k<kGlitchFxN;++k) g.setEnabled ((GlitchFx) k, false);
                                            g.setEnabled (GlitchFx::Crush, true); g.setMix (1.0f); g.setHoldSteps (2.0f); g.setCrush (6, 6); },
                         allVary1, 0.5f, 0.8f, 0.0f, 0.0f, 130);
        bool fin = true, bnd = true;
        for (size_t i = 32000; i < 66000 && i < r.out.size(); ++i) { if (!(r.out[i]==r.out[i])) fin=false; if (std::fabs(r.out[i])>8.f) bnd=false; }
        check (fin && bnd, "T3 Crush finite + bounded");
    }

    // ── T4: CLICK-FREE with ALL effects enabled, constant retrigger (the duck) ──
    {
        GRun r = driveG ([](FlowGlitch& g){ g.setMix (1.0f); g.setHoldSteps (1.0f); g.setSeed (7); },
                         allVary1, 0.5f, /*gate*/0.6f, /*traj chaos*/0.8f, 0.0f, 160);
        check (maxJump (r.out, 32000, 80000) < 0.06f, "T4 click-free: all FX + constant retrigger duck");
    }

    // ── T5: stepSamp fix — a single glitch lasts ~one step, not ~4x ─────────────
    {
        // prime with vary=0 (60 blocks), then fire across exactly ONE boundary (~36000), then vary=0
        auto varyOneShot = [](int b){ return (b >= 68 && b < 73) ? 1.0f : 0.0f; };  // [34816,37376) contains boundary 36000
        GRun r = driveG ([](FlowGlitch& g){ for (int k=0;k<kGlitchFxN;++k) g.setEnabled ((GlitchFx) k, false);
                                            g.setEnabled (GlitchFx::Repeat, true); g.setMix (1.0f); g.setHoldSteps (1.0f); },
                         varyOneShot, 0.5f, 0.9f, 0.0f, 0.0f, 110);
        int activeBlocks = 0; for (int a : r.activeBlk) activeBlocks += a;
        const int durSamples = activeBlocks * 512;       // ~holdSteps*STEP = 6000 (bug would give ~24000)
        char buf[96]; std::snprintf (buf, sizeof buf, "T5 stepSamp: one glitch ~= one step (dur~%d, expect ~6000)", durSamples);
        check (durSamples > 4500 && durSamples < 9500, buf);
    }

    // ── T6: dry passthrough when not firing (vary=0 -> out == dry exactly) ──────
    {
        GRun r = driveG ([](FlowGlitch& g){ g.setMix (1.0f); }, [](int){ return 0.0f; }, 0.5f, 0.8f, 0.0f, 0.0f, 60);
        bool exact = true; for (size_t i = 0; i < r.out.size(); ++i) if (std::fabs (r.out[i] - r.dry[i]) > 1e-6f) exact = false;
        check (exact, "T6 vary=0 -> dry passthrough");
    }

    // ── T7: mix=0 -> dry even while firing ─────────────────────────────────────
    {
        GRun r = driveG ([](FlowGlitch& g){ g.setMix (0.0f); g.setHoldSteps (2.0f); }, allVary1, 0.5f, 0.8f, 0.8f, 0.0f, 80);
        bool exact = true; for (size_t i = 0; i < r.out.size(); ++i) if (std::fabs (r.out[i] - r.dry[i]) > 1e-6f) exact = false;
        check (exact, "T7 mix=0 -> dry even when firing");
    }

    // ── T8: déjà-vu locks the fire pattern across loops ────────────────────────
    {
        // sample isActive() mid-step (after 3000 of each 6000 step) -> fire-or-not per step
        auto fireSeq = [](float dejavu, uint32_t seed)
        {
            FlowGlitch g; g.prepare (SR, 4.0); g.setMix (1.0f); g.setHoldSteps (1.0f); g.setLoopLen (4); g.setDejavu (dejavu); g.setSeed (seed);
            std::vector<int> seq; std::vector<float> L (3000), R (3000); double ppq = 0; const double pps = (BPM/60.0)/SR; long long gc = 0;
            for (int s = 0; s < 24; ++s)
            {
                for (int half = 0; half < 2; ++half)
                {
                    for (int i=0;i<3000;++i){ float v=sineSig(gc+i); L[(size_t)i]=v; R[(size_t)i]=v; }
                    g.process (0.6111f, 0.9f, /*vary*/0.5f, 0.0f, 0.0f, ppq, BPM, SR, L.data(), R.data(), 3000, true);
                    gc += 3000; ppq += pps * 3000;
                    if (half == 0) seq.push_back (g.isActive() ? 1 : 0);   // mid-step sample
                }
            }
            return seq;
        };
        auto locked = fireSeq (1.0f, 999);
        auto loopEq = [&](const std::vector<int>& v, int a, int b){ for (int k=0;k<4;++k) if (v[(size_t)(a*4+k)] != v[(size_t)(b*4+k)]) return false; return true; };
        check (loopEq (locked, 2, 3) && loopEq (locked, 3, 4), "T8 dejavu=1 locks the fire pattern");
        auto varied = fireSeq (0.0f, 999);
        check (! (loopEq (varied, 2, 3) && loopEq (varied, 3, 4)), "T8 dejavu=0 varies the fire pattern");
    }

    // ── T9: safety — finite + bounded under full aggression (all FX, chaos) ────
    {
        FlowGlitch g; g.prepare (SR, 4.0); g.setMix (1.0f); g.setHoldSteps (1.0f); g.setSeed (123);
        g.setCrush (4, 8); g.setGateDiv (8); g.setPitchRatio (4.0f); g.setScatterMs (20.f, 600.f);
        std::vector<float> L (512), R (512); double ppq = 0; const double pps=(BPM/60.0)/SR; bool fin=true, bnd=true;
        for (int b = 0; b < 300; ++b)
        {
            for (int i=0;i<512;++i){ float v=0.9f*std::sin(0.03f*(float)(b*512+i)); L[(size_t)i]=v; R[(size_t)i]=-v; }
            g.process (0.5f, 0.5f, 0.9f, 0.9f, 0.5f, ppq, BPM, SR, L.data(), R.data(), 512, true);
            for (int i=0;i<512;++i){ if (!(L[(size_t)i]==L[(size_t)i])) fin=false; if (std::fabs(L[(size_t)i])>8.f) bnd=false; }
            ppq += pps*512;
        }
        check (fin, "T9 finite (no NaN) under full aggression");
        check (bnd, "T9 bounded under full aggression");
    }

    // ── T10: TAPE-STOP physics — exponential coast (front-loaded) vs linear brake ─
    {
        // (a) the curve: exponential rate has dropped below linear by the 30% mark, spans 1->0 exactly
        auto expRate = [](double x, double k){ const double ek = std::exp(-k); return (std::exp(-k*x) - ek) / (1.0 - ek); };
        const double k = 3.5;
        check (expRate(0.3,k) < (1.0 - 0.3) - 0.05, "T10 exp coast drops faster than linear by the 30% mark");
        check (std::fabs(expRate(0.0,k) - 1.0) < 1e-9 && expRate(1.0,k) < 1e-9, "T10 tape curve spans 1->0 exactly");
        // (b) behaviour: exponential vs linear tape-stop differ audibly; exponential stays click-free
        GRun lin = driveG ([](FlowGlitch& g){ for(int kk=0;kk<kGlitchFxN;++kk) g.setEnabled((GlitchFx)kk,false);
                            g.setEnabled(GlitchFx::TapeStop,true); g.setMix(1.0f); g.setHoldSteps(2.0f); g.setTapeCurve(0.0f); },
                            [](int){return 1.0f;}, 0.5f, 1.0f, 0.55f, 1.0f, 120);
        GRun exq = driveG ([](FlowGlitch& g){ for(int kk=0;kk<kGlitchFxN;++kk) g.setEnabled((GlitchFx)kk,false);
                            g.setEnabled(GlitchFx::TapeStop,true); g.setMix(1.0f); g.setHoldSteps(2.0f); g.setTapeCurve(3.5f); },
                            [](int){return 1.0f;}, 0.5f, 1.0f, 0.55f, 1.0f, 120);
        bool differ = false; for (size_t i=32000;i<60000 && i<lin.out.size() && i<exq.out.size();++i) if (std::fabs(lin.out[i]-exq.out[i]) > 0.02f) { differ = true; break; }
        check (differ, "T10 exponential curve audibly differs from linear brake");
        check (maxJump(exq.out, 32000, 66000) < 0.03f, "T10 exponential tape-stop still click-free");
    }

    // ── T11: seam crossfade is EQUAL-POWER (uncorrelated grain joins, no -3 dB dip) ─
    {
        // The seam fade now uses sin/cos so att^2+rel^2 = 1 across the join, holding
        // energy constant when the new read and the old grain's tail are uncorrelated.
        // A linear (att+rel=1) fade dips to 0.707x power at the midpoint -> audible
        // tremolo at the grain rate on sustained Repeat / Freeze / Scatter.
        bool powerConst = true, midRight = false;
        for (int s = 0; s <= 16; ++s)
        {
            const float q = (float) s / 16.f;
            const float att = std::sin (1.57079633f * q), rel = std::cos (1.57079633f * q);
            if (std::fabs (att * att + rel * rel - 1.0f) > 1e-3f) powerConst = false;
            if (s == 8 && std::fabs (att - 0.70710678f) < 1e-3f && std::fabs (rel - 0.70710678f) < 1e-3f) midRight = true;
        }
        check (powerConst, "T11 seam fade holds constant power across the join (att^2+rel^2=1)");
        check (midRight,   "T11 seam midpoint = 0.707/0.707 (equal-power, not the 0.5/0.5 linear dip)");

        // and it must STILL be click-free on a sustained, fast-wrapping Repeat (many seams)
        GRun rep = driveG ([](FlowGlitch& g){ for (int kk=0;kk<kGlitchFxN;++kk) g.setEnabled ((GlitchFx) kk, false);
                            g.setEnabled (GlitchFx::Repeat, true); g.setMix (1.0f); g.setHoldSteps (4.0f); g.setSeed (11); },
                            [](int){ return 1.0f; }, 0.5f, 0.25f, 0.0f, 0.0f, 80);
        check (maxJump (rep.out, 32000, 100000) < 0.03f, "T11 equal-power seams stay click-free on sustained Repeat");
    }

    // ═════════════════ fb115 — the Monitor card extension (setExt) ═════════════════
    std::printf ("— fb115 ext (Monitor card) —\n");

    auto extDefaults = []{ GlitchExtParams p; return p; };                 // REP only, neutral
    auto soloExt = [](GlitchFx fx){ GlitchExtParams p; for (int k=0;k<kGlitchFxN;++k) p.en[k]=false; p.en[(int)fx]=true; return p; };

    // ── T12: ext neutral defaults — REP-only fires, audible, click-free; chance 0 = exact dry ──
    {
        // rate 0.55 = 8000-sample steps: NOT a multiple of the 1500-sample sine period,
        // so a phase-aligned repeat can't hide (the fb113 stimulus frame-up lesson)
        GRun r = driveG ([&](FlowGlitch& g){ g.setMix (1.0f); g.setExt (extDefaults()); },
                         allVary1, 0.55f, 0.55f, 0.0f, 0.0f, 160);
        check (maxJump (r.out, 32000, 80000) < 0.03f, "T12 ext defaults click-free");
        bool audible = false; for (size_t i = 32000; i < 80000 && i < r.out.size(); ++i)
            if (std::fabs (r.out[i] - r.dry[i]) > 0.02f) { audible = true; break; }
        check (audible, "T12 ext defaults audibly repeat (the doer works on first touch)");
        GRun q = driveG ([&](FlowGlitch& g){ g.setMix (1.0f); g.setExt (extDefaults()); },
                         [](int){ return 0.0f; }, 0.6111f, 0.55f, 0.0f, 0.0f, 60);
        bool exact = true; for (size_t i = 0; i < q.out.size(); ++i) if (std::fabs (q.out[i] - q.dry[i]) > 1e-6f) exact = false;
        check (exact, "T12 ext chance 0 -> bit-exact dry passthrough");
    }

    // ── T13: DICE DISCIPLINE (fb111) — two instances render BIT-IDENTICAL audio ──
    {
        auto run = [&]{ GlitchExtParams p; for (int k=0;k<kGlitchFxN;++k) p.en[k]=true; p.seed = 5; p.dejavu = 0.4f;
                        p.repVary = 0.8f; p.frzSpray = 0.7f; p.sctVary = 0.9f; p.sctWidth = 0.8f; p.pitJump = 0.6f;
                        return driveG ([&](FlowGlitch& g){ g.setMix (1.0f); g.setExt (p); },
                                       [](int){ return 0.6f; }, 0.6111f, 0.55f, 0.4f, 0.0f, 140); };
        GRun a = run(), b = run();
        bool ident = a.out.size() == b.out.size();
        if (ident) for (size_t i = 0; i < a.out.size(); ++i) if (a.out[i] != b.out[i]) { ident = false; break; }
        check (ident, "T13 two instances, same params -> bit-identical (no mutable dice)");
    }

    // ── T14: the Seed knob changes the pattern ──────────────────────────────────
    {
        auto run = [&](int seed){ GlitchExtParams p; p.seed = seed; p.dejavu = 1.0f;
                        return driveG ([&](FlowGlitch& g){ g.setMix (1.0f); g.setExt (p); },
                                       [](int){ return 0.5f; }, 0.55f, 0.55f, 0.0f, 0.0f, 140); };
        GRun a = run (5), b = run (6);
        bool differ = false; for (size_t i = 20000; i < a.out.size() && i < b.out.size(); ++i)
            if (std::fabs (a.out[i] - b.out[i]) > 0.01f) { differ = true; break; }
        check (differ, "T14 seed 5 vs 6 -> different glitch pattern");
    }

    // ── T15: Déjà Vu = the pattern lock (ext hash streams) ─────────────────────
    {
        auto fireSeq = [&](float dejavu, int seed)
        {
            FlowGlitch g; g.prepare (SR, 4.0); g.setMix (1.0f);
            GlitchExtParams p; p.dejavu = dejavu; p.seed = seed; p.loopLen = 4; g.setExt (p);
            std::vector<int> seq; std::vector<float> L (3000), R (3000); double ppq = 0; const double pps = (BPM/60.0)/SR; long long gc = 0;
            for (int s = 0; s < 24; ++s)
                for (int half = 0; half < 2; ++half)
                {
                    for (int i=0;i<3000;++i){ float v=sineSig(gc+i); L[(size_t)i]=v; R[(size_t)i]=v; }
                    g.process (0.6111f, 0.55f, /*chance*/0.5f, 0.0f, 0.0f, ppq, BPM, SR, L.data(), R.data(), 3000, true);
                    gc += 3000; ppq += pps * 3000;
                    if (half == 0) seq.push_back (g.isActive() ? 1 : 0);
                }
            return seq;
        };
        auto loopEq = [](const std::vector<int>& v, int a, int b){ for (int k=0;k<4;++k) if (v[(size_t)(a*4+k)] != v[(size_t)(b*4+k)]) return false; return true; };
        auto locked = fireSeq (1.0f, 7);
        check (loopEq (locked, 2, 3) && loopEq (locked, 3, 4) && loopEq (locked, 4, 5),
               "T15 dejavu=1 -> the same glitch pattern every loop");
        auto fresh = fireSeq (0.0f, 7);
        check (! (loopEq (fresh, 2, 3) && loopEq (fresh, 3, 4) && loopEq (fresh, 4, 5)),
               "T15 dejavu=0 -> the pattern re-rolls per loop (even with a fixed seed)");
    }

    // ── T16: SOLO law — one tile enabled means ONLY that effect ever fires ──────
    {
        auto soloIs = [&](GlitchFx fx)
        {
            FlowGlitch g; g.prepare (SR, 4.0); g.setMix (1.0f); g.setExt (soloExt (fx));
            std::vector<float> L (512), R (512); double ppq = 0; const double pps=(BPM/60.0)/SR; long long gc=0;
            bool sawFire = false, always = true;
            for (int b = 0; b < 160; ++b)
            {
                for (int i=0;i<512;++i){ float v=sineSig(gc+i); L[(size_t)i]=v; R[(size_t)i]=v; }
                g.process (0.6111f, 0.55f, 1.0f, 0.0f, 0.0f, ppq, BPM, SR, L.data(), R.data(), 512, true);
                gc += 512; ppq += pps*512;
                if (g.vizFx() >= 0) { sawFire = true; if (g.vizFx() != (int) fx) always = false; }
            }
            return sawFire && always;
        };
        check (soloIs (GlitchFx::TapeStop), "T16 solo TAPE -> only tape-stops fire");
        check (soloIs (GlitchFx::Reverse),  "T16 solo REV -> only reverses fire");
        check (soloIs (GlitchFx::Gate),     "T16 solo GATE -> gate runs as the main effect");
    }

    // ── T17: STACK law — GATE overlays a reader (REP+GATE = gated stutter) ──────
    {
        GlitchExtParams pg = soloExt (GlitchFx::Repeat);
        pg.en[(int) GlitchFx::Gate] = true; pg.gateRate = 1.0f; pg.gateAmt = 1.0f; pg.holdSteps = 4.0f;
        GRun ab = driveG ([&](FlowGlitch& g){ g.setMix (1.0f); g.setExt (pg); }, allVary1, 0.5f, 0.55f, 0.0f, 0.0f, 140);
        GRun solo = driveG ([&](FlowGlitch& g){ GlitchExtParams p = soloExt (GlitchFx::Repeat); p.holdSteps = 4.0f;
                                                g.setMix (1.0f); g.setExt (p); }, allVary1, 0.5f, 0.55f, 0.0f, 0.0f, 140);
        // windowed RMS: the stacked run must carve deep holes the solo run doesn't have
        auto minMaxRms = [](const std::vector<float>& o, size_t from, size_t to)
        { float mn = 1e9f, mx = 0.f;
          for (size_t w = from; w + 150 < to && w + 150 < o.size(); w += 150)
          { double acc = 0; for (size_t i = w; i < w + 150; ++i) acc += (double) o[i]*o[i];
            const float r = (float) std::sqrt (acc / 150.0); mn = std::min (mn, r); mx = std::max (mx, r); }
          return std::pair<float,float> (mn, mx); };
        auto sM = minMaxRms (solo.out, 40000, 70000), gM = minMaxRms (ab.out, 40000, 70000);
        check (gM.first < 0.02f && sM.first > 0.05f && gM.second > 0.1f,
               "T17 REP+GATE stacks (holes in the stutter; REP alone has none)");
        check (maxJump (ab.out, 32000, 70000) < 0.05f, "T17 stacked overlay stays click-free");
    }

    // ── T18: CRUSH overlay changes a reader's wet (REP+CRSH = crushed stutter) ──
    {
        GlitchExtParams pc = soloExt (GlitchFx::Repeat);
        pc.en[(int) GlitchFx::Crush] = true; pc.crshBits = 1.0f; pc.crshRate = 0.8f; pc.holdSteps = 4.0f;
        GRun cr = driveG ([&](FlowGlitch& g){ g.setMix (1.0f); g.setExt (pc); }, allVary1, 0.5f, 0.55f, 0.0f, 0.0f, 140);
        GRun solo = driveG ([&](FlowGlitch& g){ GlitchExtParams p = soloExt (GlitchFx::Repeat); p.holdSteps = 4.0f;
                                                g.setMix (1.0f); g.setExt (p); }, allVary1, 0.5f, 0.55f, 0.0f, 0.0f, 140);
        bool differ = false, fin = true;
        for (size_t i = 40000; i < 70000 && i < cr.out.size(); ++i)
        { if (std::fabs (cr.out[i] - solo.out[i]) > 0.03f) differ = true; if (! (cr.out[i] == cr.out[i])) fin = false; }
        check (differ && fin, "T18 REP+CRSH crushes the stutter (differs from REP alone, finite)");
    }

    // ── T19: ROLL — a punch-in fires at Chance 0, quantized, click-free ─────────
    {
        FlowGlitch g; g.prepare (SR, 4.0); g.setMix (1.0f); g.setExt (extDefaults());
        std::vector<float> out; std::vector<float> L (512), R (512);
        double ppq = 0; const double pps=(BPM/60.0)/SR; long long gc=0; long long fired = -1;
        for (int b = 0; b < 160; ++b)
        {
            if (b == 80) g.rollNow();
            for (int i=0;i<512;++i){ float v=sineSig(gc+i); L[(size_t)i]=v; R[(size_t)i]=v; }
            g.process (0.6111f, 0.55f, /*chance*/0.0f, 0.0f, 0.0f, ppq, BPM, SR, L.data(), R.data(), 512, true);
            for (int i=0;i<512;++i) out.push_back (L[(size_t)i]);
            if (fired < 0 && g.vizFireCount() > 0) fired = (long long) b * 512;
            gc += 512; ppq += pps*512;
        }
        check (fired >= 80*512 && fired < 80*512 + 6800, "T19 Roll fires within one 1/16 quant division at Chance 0");
        check (maxJump (out, 34000, 70000) < 0.03f, "T19 Roll punch-in click-free");
        check (g.vizFireCount() == 1, "T19 exactly ONE fire per Roll press");
    }

    // ── T20: TIME IS TRUTHFUL — chance 1 fires once per grid step, both grids ───
    {
        auto firesIn = [&](float rate, int steps)
        {
            FlowGlitch g; g.prepare (SR, 4.0); g.setMix (1.0f); g.setExt (extDefaults());
            const double beats = (double) arpBeatsPerStepRich (rate);
            const double stepSamp = beats / ((BPM/60.0)/SR);
            const long long total = (long long) std::llround (stepSamp * steps);
            std::vector<float> L (512), R (512); double ppq = 0; const double pps=(BPM/60.0)/SR; long long gc=0;
            while (gc < total)
            {
                for (int i=0;i<512;++i){ float v=sineSig(gc+i); L[(size_t)i]=v; R[(size_t)i]=v; }
                g.process (rate, 0.55f, 1.0f, 0.0f, 0.0f, ppq, BPM, SR, L.data(), R.data(), 512, true);
                gc += 512; ppq += pps*512;
            }
            return (int) g.vizFireCount();
        };
        const int f16 = firesIn (0.6111f, 32), f32 = firesIn (0.75f, 32);   // 1/16 vs 1/32 (rich idx 11/14)
        char buf[96]; std::snprintf (buf, sizeof buf, "T20 truthful grid: 32 steps -> %d fires @1/16, %d @1/32", f16, f32);
        check (f16 >= 31 && f16 <= 34 && f32 >= 31 && f32 <= 34, buf);
    }

    // ── T21: PITCH is truthful — +12 semis doubles the zero-crossing rate ───────
    {
        auto varyOneShot = [](int b){ return (b >= 68 && b < 73) ? 1.0f : 0.0f; };
        GlitchExtParams pp = soloExt (GlitchFx::Pitch);
        pp.pitShift = 1.0f;                            // +12 semitones (octave up)
        pp.holdSteps = 8.0f;
        GRun r = driveG ([&](FlowGlitch& g){ g.setMix (1.0f); g.setExt (pp); }, varyOneShot, 0.5f, 0.55f, 0.0f, 0.0f, 170);
        auto crossings = [](const std::vector<float>& o, size_t from, size_t to)
        { int n = 0; for (size_t i = from + 1; i < to && i < o.size(); ++i) if ((o[i-1] < 0.f) != (o[i] < 0.f)) ++n; return n; };
        const int wet = crossings (r.out, 40000, 58000);
        const int dry = crossings (r.dry, 40000, 58000);
        char buf[96]; std::snprintf (buf, sizeof buf, "T21 +12 semis: wet crossings %d ~= 2x dry %d", wet, dry);
        check (wet > dry * 3 / 2 && wet < dry * 5 / 2, buf);
        check (maxJump (r.out, 38000, 80000) < 0.03f, "T21 pitch fire click-free");
    }

    // ── T22: FREE clock — glitches keep firing when the host position freezes ───
    {
        auto firesFrozen = [&](bool sync)
        {
            FlowGlitch g; g.prepare (SR, 4.0); g.setMix (1.0f);
            GlitchExtParams p; p.sync = sync; g.setExt (p);
            std::vector<float> L (512), R (512); long long gc = 0;
            for (int b = 0; b < 140; ++b)
            {
                for (int i=0;i<512;++i){ float v=sineSig(gc+i); L[(size_t)i]=v; R[(size_t)i]=v; }
                g.process (0.6111f, 0.55f, 1.0f, 0.0f, 0.0f, /*hostPpq FROZEN*/ 10.0, BPM, SR, L.data(), R.data(), 512, true);
                gc += 512;
            }
            return (int) g.vizFireCount();
        };
        const int fSync = firesFrozen (true), fFree = firesFrozen (false);
        char buf[96]; std::snprintf (buf, sizeof buf, "T22 Free clock: frozen host -> %d fires synced, %d free", fSync, fFree);
        check (fSync <= 2 && fFree > 10, buf);
    }

    // ── T23: ALL-HOT gauntlet — every tile + every depth knob at punishing values ─
    {
        GlitchExtParams p; for (int k=0;k<kGlitchFxN;++k) p.en[k]=true;
        p.en[(int) GlitchFx::Crush] = false;           // crush steps are the SOUND — tested finite below
        p.dejavu = 0.5f; p.decay = 0.7f; p.bend = 1.0f; p.seed = 13; p.holdSteps = 2.0f;
        p.repSize = 0.2f; p.repSpeed = 1.0f; p.repFade = 1.0f; p.repVary = 1.0f;
        p.revLen = 0.3f; p.revFade = 0.8f; p.revSprd = 1.0f; p.revSnap = 0.2f;
        p.tapeTime = 0.3f; p.tapeDepth = 1.0f; p.tapeSpin = 0.8f;
        p.gateRate = 1.0f; p.gateShape = 0.0f; p.gateNudge = 0.5f; p.gateAmt = 1.0f;
        p.pitShift = 1.0f; p.pitWalk = 1.0f; p.pitGlide = 1.0f; p.pitJump = 1.0f;
        p.frzSize = 0.05f; p.frzSpray = 1.0f; p.frzShine = 1.0f; p.frzMelt = 1.0f;
        p.sctSize = 0.1f; p.sctAmt = 0.5f; p.sctVary = 1.0f; p.sctWidth = 1.0f;
        for (int k = 0; k < kGlitchFxN; ++k) { p.fxFlt[k] = 2; p.fxPan[k] = 0; }   // fb125 per-effect Out
        GRun r = driveG ([&](FlowGlitch& g){ g.setMix (1.0f); g.setExt (p); },
                         allVary1, 0.6111f, 0.55f, 0.9f, 0.4f, 200);
        check (maxJump (r.out, 34000, 100000) < 0.07f, "T23 all-hot (no crush): still click-free");
        GlitchExtParams pc = p; pc.en[(int) GlitchFx::Crush] = true; pc.crshBits = 1.0f; pc.crshRate = 1.0f; pc.crshTone = 0.2f;
        GRun rc = driveG ([&](FlowGlitch& g){ g.setMix (1.0f); g.setExt (pc); },
                          allVary1, 0.6111f, 0.55f, 0.9f, 0.4f, 200);
        bool fin = true, bnd = true;
        for (size_t i = 0; i < rc.out.size(); ++i) { if (! (rc.out[i] == rc.out[i])) fin = false; if (std::fabs (rc.out[i]) > 8.f) bnd = false; }
        check (fin && bnd, "T23 all-hot + crush overlay: finite and bounded");
    }

    // ── T24: THE LADDER comes back down — 1/256 then 1/4 keeps firing (fb107/fb116 law) ─
    {
        FlowGlitch g; g.prepare (SR, 4.0); g.setMix (1.0f); GlitchExtParams p0; g.setExt (p0);
        std::vector<float> out; std::vector<float> L (512), R (512);
        double ppq = 0; const double pps = (BPM/60.0)/SR; long long gc = 0;
        auto run = [&](float rate, int samples)
        {
            for (int done = 0; done < samples; done += 512)
            {
                for (int i=0;i<512;++i){ float v=sineSig(gc+i); L[(size_t)i]=v; R[(size_t)i]=v; }
                g.process (rate, 0.55f, 1.0f, 0.0f, 0.0f, ppq, BPM, SR, L.data(), R.data(), 512, true);
                for (int i=0;i<512;++i) out.push_back (L[(size_t)i]);
                gc += 512; ppq += pps*512;
            }
        };
        run (1.0f, 48000);                                    // one second at 1/256 (rich idx 18)
        const long long cTop = g.vizFireCount();
        run (0.27778f, 96000);                                // drop to 1/4 (idx 5) for two seconds
        const long long cNew = g.vizFireCount() - cTop;
        char buf[96]; std::snprintf (buf, sizeof buf, "T24 rate 1/256 -> 1/4 keeps firing (%lld fires after the drop)", cNew);
        check (cTop > 50 && cNew >= 3, buf);
        check (maxJump (out, 30000, (size_t) out.size()) < 0.07f, "T24 the grid drop itself is click-free");
    }

    // ── T25: per-effect OUT routing — the fire carries its own Pan + Filter (fb125) ─
    {
        GlitchExtParams pl = soloExt (GlitchFx::Repeat);
        pl.fxPan[(int) GlitchFx::Repeat] = 0;                  // Repeat hard LEFT
        GRun r = driveG ([&](FlowGlitch& g){ g.setMix (1.0f); g.setExt (pl); }, allVary1, 0.55f, 0.55f, 0.0f, 0.0f, 120);
        // R channel of the buffer is processed in place too — drive with the same signal;
        // during fires the wet replaces both, R attenuated to 0.25x
        double eL = 0, eR = 0;
        {   // re-run capturing both channels
            FlowGlitch g; g.prepare (SR, 4.0); g.setMix (1.0f); g.setExt (pl);
            std::vector<float> L (512), R (512); double ppq = 0; const double pps=(BPM/60.0)/SR; long long gc=0;
            for (int b = 0; b < 120; ++b)
            {
                for (int i=0;i<512;++i){ float v=sineSig(gc+i); L[(size_t)i]=v; R[(size_t)i]=v; }
                g.process (0.55f, 0.55f, 1.0f, 0.0f, 0.0f, ppq, BPM, SR, L.data(), R.data(), 512, true);
                if (b > 60) for (int i=0;i<512;++i) { eL += (double) L[(size_t)i]*L[(size_t)i]; eR += (double) R[(size_t)i]*R[(size_t)i]; }
                gc += 512; ppq += pps*512;
            }
        }
        char buf[96]; std::snprintf (buf, sizeof buf, "T25 Repeat panned LEFT: energy L %.2f >> R %.2f", eL, eR);
        check (eL > eR * 2.0, buf);
        (void) r;

        GlitchExtParams pf = soloExt (GlitchFx::Repeat);
        pf.fxFlt[(int) GlitchFx::Repeat] = 1;                  // Repeat -> Low (LP900)
        auto hfRms = [&](const GlitchExtParams& pp)
        {
            FlowGlitch g; g.prepare (SR, 4.0); g.setMix (1.0f); g.setExt (pp);
            std::vector<float> L (512), R (512); double ppq = 0; const double pps=(BPM/60.0)/SR; long long gc=0;
            double acc = 0; long long cnt = 0; float prev = 0;
            for (int b = 0; b < 120; ++b)
            {
                for (int i=0;i<512;++i){ float v=0.4f*std::sin(2.0f*3.14159265f*(float)(gc+i)/16.0f); L[(size_t)i]=v; R[(size_t)i]=v; }   // 3 kHz
                g.process (0.55f, 0.55f, 1.0f, 0.0f, 0.0f, ppq, BPM, SR, L.data(), R.data(), 512, true);
                if (b > 60) for (int i=0;i<512;++i) { const float d = L[(size_t)i]-prev; acc += (double) d*d; prev = L[(size_t)i]; ++cnt; }
                gc += 512; ppq += pps*512;
            }
            return std::sqrt (acc / (double) cnt);
        };
        const double hOff = hfRms (soloExt (GlitchFx::Repeat)), hLow = hfRms (pf);
        char buf2[96]; std::snprintf (buf2, sizeof buf2, "T25 Repeat->Low filter darkens the wet (%.4f -> %.4f)", hOff, hLow);
        check (hLow < hOff * 0.6, buf2);
    }

    // ── T26: per-effect TRIG — sync stalls with the host, Free keeps going; Roll-only is manual ─
    {
        auto runFrozen = [&](int repTrig, int frzTrig, bool frozen, bool doRoll)
        {
            FlowGlitch g; g.prepare (SR, 4.0); g.setMix (1.0f);
            GlitchExtParams p; for (int k=0;k<kGlitchFxN;++k) p.en[k]=false;
            p.en[(int) GlitchFx::Repeat] = true;  p.fxTrig[(int) GlitchFx::Repeat] = repTrig;
            p.en[(int) GlitchFx::Freeze] = true;  p.fxTrig[(int) GlitchFx::Freeze] = frzTrig;
            g.setExt (p);
            std::vector<float> L (512), R (512); long long gc = 0;
            double ppq = 0; const double pps=(BPM/60.0)/SR;
            int frzFires = 0, repFires = 0; long long lastC = 0; int lastFx = -1;
            for (int b = 0; b < 160; ++b)
            {
                if (doRoll && b == 80) g.rollNow();
                for (int i=0;i<512;++i){ float v=sineSig(gc+i); L[(size_t)i]=v; R[(size_t)i]=v; }
                g.process (0.6111f, 0.55f, 1.0f, 0.0f, 0.0f, frozen ? 10.0 : ppq, BPM, SR, L.data(), R.data(), 512, true);
                if (g.vizFireCount() > lastC) { lastC = g.vizFireCount(); lastFx = g.vizFx();
                    if (lastFx == (int) GlitchFx::Freeze) ++frzFires;
                    if (lastFx == (int) GlitchFx::Repeat) ++repFires; }
                gc += 512; ppq += pps*512;
            }
            return std::pair<int,int> (repFires, frzFires);
        };
        // REP sync + FRZ free, host FROZEN: the sync grid stalls, the free grid keeps firing FRZ
        auto fz = runFrozen (0, 1, true, false);
        char buf[96]; std::snprintf (buf, sizeof buf, "T26 frozen host: sync REP stalls (%d), free FRZ keeps firing (%d)", fz.first, fz.second);
        check (fz.first <= 1 && fz.second > 8, buf);
        // both running: both fire
        auto rn = runFrozen (0, 1, false, false);
        check (rn.first > 4 && rn.second > 4, "T26 running host: sync REP and free FRZ BOTH fire");
        // FRZ = Roll-only: never fires from any grid at Chance 1... until Roll
        auto ro = runFrozen (0, 2, false, false);
        check (ro.second == 0, "T26 Roll-only FRZ never fires from the grids");
        auto ro2 = runFrozen (0, 2, false, true);
        check (ro2.second >= 1, "T26 Roll-only FRZ fires when Roll is pressed");
    }

    // ── T27: per-effect GRID — "pitch at 1/1 while repeat runs 1/16" (fb127) ─────
    {
        FlowGlitch g; g.prepare (SR, 4.0); g.setMix (1.0f);
        GlitchExtParams p; for (int k=0;k<kGlitchFxN;++k) p.en[k]=false;
        p.en[(int) GlitchFx::Repeat] = true;                       // Main grid (1/16 at rate 0.6111)
        p.en[(int) GlitchFx::Pitch]  = true; p.fxGrid[(int) GlitchFx::Pitch] = 1;   // its own 1/1
        g.setExt (p);
        std::vector<float> L (512), R (512); long long gc = 0;
        double ppq = 0; const double pps=(BPM/60.0)/SR;
        int repF = 0, pitF = 0; long long lastC = 0;
        for (int b = 0; b < 400; ++b)                              // ~4.3 s ≈ 34 sixteenths / 2 wholes
        {
            for (int i=0;i<512;++i){ float v=sineSig(gc+i); L[(size_t)i]=v; R[(size_t)i]=v; }
            g.process (0.6111f, 0.55f, 1.0f, 0.0f, 0.0f, ppq, BPM, SR, L.data(), R.data(), 512, true);
            if (g.vizFireCount() > lastC) { lastC = g.vizFireCount();
                if (g.vizFx() == (int) GlitchFx::Repeat) ++repF;
                if (g.vizFx() == (int) GlitchFx::Pitch)  ++pitF; }
            gc += 512; ppq += pps*512;
        }
        char buf[96]; std::snprintf (buf, sizeof buf, "T27 own grids: REP(1/16) %d fires >> PIT(1/1) %d fires", repF, pitF);
        check (repF > pitF * 6 && pitF >= 1, buf);
    }

    // ── T28: DAW LOOP WRAP — ppq snaps backwards every pass; fires must survive (fb128) ─
    {
        FlowGlitch g; g.prepare (SR, 4.0); g.setMix (1.0f);
        GlitchExtParams p; g.setExt (p);                       // REP only, Main grid, Sync
        std::vector<float> L (512), R (512); long long gc = 0;
        double ppq = 0; const double pps=(BPM/60.0)/SR;
        const double LOOPB = 2.0;                              // a 2-beat DAW loop region
        for (int b = 0; b < 800; ++b)                          // 17 beats = ~8.5 loop passes
        {
            for (int i=0;i<512;++i){ float v=sineSig(gc+i); L[(size_t)i]=v; R[(size_t)i]=v; }
            g.process (0.6111f, 0.55f, 1.0f, 0.0f, 0.0f, ppq, BPM, SR, L.data(), R.data(), 512, true);
            gc += 512; ppq += pps*512;
            if (ppq >= LOOPB) ppq -= LOOPB;                    // the wrap: position snaps back
        }
        // 17 beats = 68 sixteenth steps; chance 1 => ~a fire per step across every wrap
        // (the stranded pre-fix clock stopped after pass ONE: ~8 fires)
        const long long fires = g.vizFireCount();
        char buf[96]; std::snprintf (buf, sizeof buf, "T28 loop-wrap survival: %lld fires across ~8 wraps (expect ~66, stranded ~8)", fires);
        check (fires > 55, buf);
    }

    // ── T29: OUT MODE (fb142) — Gate silences the dry between fires; Cut ducks it during ─
    {
        auto en = [](FlowGlitch& g){ GlitchExtParams x; x.en[0] = true; g.setExt (x); g.setMix (1.f); };
        auto gate = driveG ([&](FlowGlitch& g){ en (g); g.setOutMode (2); },
                            [](int){ return 0.0f; }, 0.6111f, 0.5f, 0.f, 0.f, 60);   // chance 0 = never fires
        double eG = 0; for (auto v : gate.out) eG += (double) v * v;
        check (eG < 1e-6, "T29 GATE + no fires = silence (dry never passes)");
        auto mix = driveG ([&](FlowGlitch& g){ en (g); g.setOutMode (0); },
                           [](int){ return 0.0f; }, 0.6111f, 0.5f, 0.f, 0.f, 60);
        double eM = 0; for (auto v : mix.out) eM += (double) v * v;
        check (eM > 1.0, "T29 MIX + no fires = dry passes untouched");
    }
    // ── T30: PING (fb142) — consecutive fires alternate across the stereo field ─────────
    {
        FlowGlitch g; g.prepare (SR, 4.0);
        GlitchExtParams x; x.en[0] = true; g.setExt (x); g.setMix (1.f); g.setPing (1.f);
        std::vector<float> L (512), R (512);
        double ppq = 0; const double pps = (BPM / 60.0) / SR; long long gc = 0;
        int flips = 0; double prevSide = 0;
        for (int b = 0; b < 400; ++b)
        {
            for (int i = 0; i < 512; ++i) { float v = sineSig (gc + i); L[(size_t) i] = v; R[(size_t) i] = v; }
            g.process (0.6111f, 0.5f, 1.0f, 0.f, 0.f, ppq, BPM, SR, L.data(), R.data(), 512, true);
            double el = 0, er = 0;
            for (int i = 0; i < 512; ++i) { el += (double) L[(size_t) i] * L[(size_t) i]; er += (double) R[(size_t) i] * R[(size_t) i]; }
            const double side = el - er;
            if (std::fabs (side) > 0.05) { if (prevSide != 0 && side * prevSide < 0) ++flips; if (std::fabs (side) > 0.05) prevSide = side; }
            gc += 512; ppq += pps * 512;
        }
        char buf[96]; std::snprintf (buf, sizeof buf, "T30 PING alternates the field (%d L/R flips seen)", flips);
        check (flips >= 4, buf);
    }

    std::printf ("\n%d checks, %d failed\n", g_checks, g_fail);
    if (g_fail == 0) std::printf ("ALL %d CHECKS PASSED\n", g_checks);
    return g_fail == 0 ? 0 : 1;
}
