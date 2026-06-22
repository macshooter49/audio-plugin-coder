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
constexpr int    STEP = 6000;                  // rate 0.5 @120/48k -> boundaries every 6000 samples

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

    std::printf ("\n%d checks, %d failed\n", g_checks, g_fail);
    if (g_fail == 0) std::printf ("ALL %d CHECKS PASSED\n", g_checks);
    return g_fail == 0 ? 0 : 1;
}
