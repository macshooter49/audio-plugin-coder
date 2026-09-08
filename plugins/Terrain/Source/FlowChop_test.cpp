// =============================================================================
//  FlowChop_test.cpp — offline proof for the CHOP varispeed slice engine
//  g++ -std=c++17 -Wall -Wextra -ISource Source/FlowChop_test.cpp -o /tmp/fc && /tmp/fc
//
//  Headline test: ZERO CLICKS. Under the most aggressive settings (every flip
//  style, repitch, reverse, Catch enter/exit), the max sample-to-sample jump in
//  the output must stay tiny — proving the cosine-enveloped overlapping-voice
//  architecture (the "silent light-switch") is click-free by construction.
// =============================================================================
#include "FlowChop.h"
#include <cstdio>
#include <vector>
#include <cmath>
#include <set>
#include <functional>

using namespace wc;

static int g_checks = 0, g_fail = 0;
static void check (bool ok, const char* what) { ++g_checks; if (! ok) { ++g_fail; std::printf ("  FAIL: %s\n", what); } }

constexpr double BPM = 120, SR = 48000;
constexpr int    STEP = 6000;                 // rate 0.5 @120/48k
constexpr uint16_t MAJOR = 0x0AB5;            // bits 0,2,4,5,7,9,11

// smooth low-freq source so legitimate per-sample slope is tiny (click test is sensitive)
static float sineSig (long long n) { return 0.5f * std::sin (2.0f * 3.14159265f * (float) n / 1500.0f); }
// counting source: bounded, locally-unique (for read-direction checks)
static float countSig (long long n) { return (float) (((n % 4096) + 4096) % 4096 - 2048) / 4096.0f; }

struct Run { std::vector<float> out, dry; };

static Run drive (const std::function<void(FlowChop&)>& setup,
                  const std::function<float(long long)>& sig,
                  float rate, float gate, float vary, float traj, float morph,
                  int nBlocks, int blk = 512,
                  const std::function<void(FlowChop&,int)>& perBlock = nullptr)
{
    FlowChop c; c.prepare (SR, 4.0); c.setScale (60, MAJOR); setup (c);
    Run r; std::vector<float> L ((size_t) blk), R ((size_t) blk);
    long long gc = 0; double ppq = 0; const double pps = (BPM / 60.0) / SR;
    for (int b = 0; b < nBlocks; ++b)
    {
        if (perBlock) perBlock (c, b);
        for (int i = 0; i < blk; ++i) { float v = sig (gc + i); L[(size_t) i] = v; R[(size_t) i] = v; r.dry.push_back (v); }
        c.process (rate, gate, vary, traj, morph, ppq, BPM, SR, L.data(), R.data(), blk, true);
        for (int i = 0; i < blk; ++i) r.out.push_back (L[(size_t) i]);
        gc += blk; ppq += pps * blk;
    }
    return r;
}

static float maxJump (const std::vector<float>& o, size_t from) { float m = 0.f; for (size_t i = from + 1; i < o.size(); ++i) m = std::max (m, std::fabs (o[i] - o[i-1])); return m; }

// per-step slice-index sequence (block == one step so lastSliceIndex is read per step)
static std::vector<int> sliceSeq (const std::function<void(FlowChop&)>& setup, float traj, int nSteps)
{
    FlowChop c; c.prepare (SR, 4.0); c.setScale (60, MAJOR); setup (c);
    std::vector<int> seq; std::vector<float> L (STEP), R (STEP);
    double ppq = 0; const double pps = (BPM / 60.0) / SR; long long gc = 0;
    for (int s = 0; s < nSteps; ++s)
    {
        for (int i = 0; i < STEP; ++i) { float v = sineSig (gc + i); L[(size_t)i] = v; R[(size_t)i] = v; }
        c.process (0.6111f, 1.0f, 0.0f, traj, 0.0f, ppq, BPM, SR, L.data(), R.data(), STEP, true);  // 0.6111 = 1/16 on the rich ladder (0.25 beats = STEP)
        seq.push_back (c.lastSliceIndex());
        gc += STEP; ppq += pps * STEP;
    }
    return seq;
}

int main()
{
    std::printf ("FlowChop engine — varispeed chop proof (ZERO CLICKS)\n");

    // ── T1: CLICK-FREE across every flip style (the headline) ──────────────────
    {
        const char* names[6] = { "Forward","Reverse","Shuffle","PingPong","StutterRoll","GrainSpray" };
        for (int s = 0; s < 6; ++s)
        {
            const float traj = (s + 0.5f) / 6.0f;   // land in style s
            Run r = drive ([](FlowChop& c){ c.setMode (ChopMode::AlwaysOn); c.setMix (1.0f); c.setLoopLen (6);
                                            c.setPitchRangeDeg (5); c.setReverseProb (0.5f); c.setSeed (99); },
                           sineSig, 0.5f, 0.9f, /*vary*/0.9f, traj, /*morph*/0.7f, 120);
            const float mj = maxJump (r.out, 40000);
            char buf[96]; std::snprintf (buf, sizeof buf, "T1 click-free: style=%s (maxJump=%.4f)", names[s], mj);
            check (mj < 0.03f, buf);
        }
    }

    // ── T2: CLICK-FREE through Catch enter/exit transitions ────────────────────
    {
        // toggle the Catch trigger every 16 blocks → exercises wet-gate crossfades
        Run r = drive ([](FlowChop& c){ c.setMode (ChopMode::Catch); c.setMix (1.0f); c.setLoopLen (6); c.setPitchRangeDeg (5); c.setReverseProb (0.5f); },
                       sineSig, 0.5f, 0.9f, 0.9f, /*traj Shuffle*/0.45f, 0.6f, 160,
                       512, [](FlowChop& c, int b){ c.setCatchHeld (((b / 16) % 2) == 1); });
        check (maxJump (r.out, 20000) < 0.03f, "T2 click-free across Catch enter/exit");
    }

    // ── T3: CLICK-FREE with heavy repitch + reverse (varispeed read path) ──────
    {
        Run r = drive ([](FlowChop& c){ c.setMode (ChopMode::AlwaysOn); c.setMix (1.0f); c.setLoopLen (4);
                                        c.setPitchRangeDeg (12); c.setReverseProb (1.0f); c.setRatchet (4); c.setSeed (7); },
                       sineSig, 0.5f, 0.8f, 1.0f, /*StutterRoll*/0.75f, 1.0f, 120);
        check (maxJump (r.out, 40000) < 0.03f, "T3 click-free under max repitch+reverse+roll");
    }

    // ── T4: Forward at unity == delayed passthrough (continuous-read varispeed) ─
    {
        Run r = drive ([](FlowChop& c){ c.setMode (ChopMode::AlwaysOn); c.setMix (1.0f); c.setLoopLen (4);
                                        c.setPitchRangeDeg (0); c.setReverseProb (0.0f); c.setRatchet (1); },
                       sineSig, 0.6111f, /*gate full*/1.0f, /*vary*/0.0f, /*Forward*/0.0f, /*morph*/0.0f, 140);
        const long long latency = (long long) 1 * STEP;             // Forward now ≈ 1 slice (live-anchored)
        bool ok = true; int tested = 0;
        for (size_t g = (size_t) latency + 2000; g + 1 < r.out.size(); ++g)
        {
            const long long stepPhase = (long long) g % STEP;
            if (stepPhase < 200 || stepPhase > STEP - 200) continue; // sustain region (away from crossfade seams)
            const float want = r.dry[(size_t) ((long long) g - latency)];
            if (std::fabs (r.out[g] - want) > 2e-3f) ok = false;
            ++tested;
        }
        check (ok && tested > 5000, "T4 Forward/unity reconstructs input delayed (continuous read)");
    }

    // ── T5: reverse reads the buffer backwards (recovered index decreases) ─────
    {
        // counting signal, force audio-reverse, unity pitch, Forward order; in a sustain
        // region the single voice reads backwards -> recovered source index decreases.
        Run r = drive ([](FlowChop& c){ c.setMode (ChopMode::AlwaysOn); c.setMix (1.0f); c.setLoopLen (4);
                                        c.setPitchRangeDeg (0); c.setReverseProb (1.0f); c.setRatchet (1); },
                       countSig, 0.6111f, 1.0f, /*vary so revProb applies*/1.0f, 0.0f, 0.0f, 140);
        auto idxOf = [] (float v) { return (long long) std::llround ((double) v * 4096.0 + 2048.0); };
        // scan a sustain window well past latency; count decreasing vs increasing steps
        int dec = 0, inc = 0; long long prev = -1;
        const long long base = (long long) 6 * STEP + 1500;
        for (long long g = base; g < base + 1200; ++g)
        {
            const long long li = idxOf (r.out[(size_t) g]);
            if (prev >= 0) { long long d = li - prev; if (d < 0 && d > -100) ++dec; else if (d > 0 && d < 100) ++inc; }
            prev = li;
        }
        check (dec > inc * 3 && dec > 200, "T5 reverse plays the slice backwards");
    }

    // ── T6: CATCH gating — silent (dry) when not held, active when held ────────
    {
        // not held the whole time -> output == dry exactly (wet gate closed)
        Run idle = drive ([](FlowChop& c){ c.setMode (ChopMode::Catch); c.setMix (1.0f); c.setLoopLen (6); },
                          sineSig, 0.5f, 0.9f, 0.9f, 0.45f, 0.5f, 60,
                          512, [](FlowChop& c, int){ c.setCatchHeld (false); });
        bool exact = true; for (size_t i = 0; i < idle.out.size(); ++i) if (std::fabs (idle.out[i] - idle.dry[i]) > 1e-6f) exact = false;
        check (exact, "T6 CATCH not held -> dry passthrough (no wet)");

        // held -> diverges from dry (chopping active)
        Run held = drive ([](FlowChop& c){ c.setMode (ChopMode::Catch); c.setMix (1.0f); c.setLoopLen (6); c.setPitchRangeDeg (5); },
                          sineSig, 0.5f, 0.9f, 0.9f, 0.45f, 0.7f, 120,
                          512, [](FlowChop& c, int){ c.setCatchHeld (true); });
        float diff = 0.f; for (size_t i = 40000; i < held.out.size(); ++i) diff = std::max (diff, std::fabs (held.out[i] - held.dry[i]));
        check (diff > 0.05f, "T6 CATCH held -> output diverges (chopping)");
    }

    // ── T7: ALWAYS-ON is active while playing ──────────────────────────────────
    {
        FlowChop c; c.prepare (SR, 4.0); c.setMode (ChopMode::AlwaysOn); c.setMix (1.0f); c.setLoopLen (6);
        std::vector<float> L (512), R (512); double ppq = 0; const double pps = (BPM/60.0)/SR; long long gc = 0; bool active = false;
        for (int b = 0; b < 40; ++b) { for (int i=0;i<512;++i){float v=sineSig(gc+i);L[(size_t)i]=v;R[(size_t)i]=v;} c.process(0.5f,0.9f,0.5f,0.0f,0.0f,ppq,BPM,SR,L.data(),R.data(),512,true); if (c.isActive()) active=true; gc+=512; ppq+=pps*512; }
        check (active, "T7 ALWAYS-ON active while playing");
    }

    // ── T8: déjà-vu lock — Shuffle order repeats per loop when locked, varies when 0 ──
    {
        auto locked = sliceSeq ([](FlowChop& c){ c.setLoopLen (4); c.setDejavu (1.0f); c.setSeed (321); }, /*Shuffle*/0.45f, 24);
        // compare loop 2 vs 3 vs 4 (steps grouped by 4)
        auto loopEq = [&](int a, int b){ for (int k=0;k<4;++k) if (locked[(size_t)(a*4+k)] != locked[(size_t)(b*4+k)]) return false; return true; };
        bool lk = loopEq (2,3) && loopEq (3,4);
        check (lk, "T8 dejavu=1 locks the shuffle order across loops");
        auto varied = sliceSeq ([](FlowChop& c){ c.setLoopLen (4); c.setDejavu (0.0f); c.setSeed (321); }, 0.45f, 24);
        auto loopEqV = [&](int a, int b){ for (int k=0;k<4;++k) if (varied[(size_t)(a*4+k)] != varied[(size_t)(b*4+k)]) return false; return true; };
        check (! (loopEqV (2,3) && loopEqV (3,4)), "T8 dejavu=0 varies the order");
    }

    // ── T9: flip-style slice orders (Forward in order, Reverse reversed) ───────
    {
        auto fwd = sliceSeq ([](FlowChop& c){ c.setLoopLen (4); }, /*Forward*/0.0f, 8);
        bool fok = (fwd[0]==0 && fwd[1]==1 && fwd[2]==2 && fwd[3]==3);
        check (fok, "T9 Forward plays slices 0,1,2,3");
        auto rev = sliceSeq ([](FlowChop& c){ c.setLoopLen (4); }, /*Reverse*/0.2f, 8);
        bool rok = (rev[0]==3 && rev[1]==2 && rev[2]==1 && rev[3]==0);
        check (rok, "T9 Reverse plays slices 3,2,1,0");
    }

    // ── T10: safety — finite + bounded under full aggression ───────────────────
    {
        FlowChop c; c.prepare (SR, 4.0); c.setMode (ChopMode::AlwaysOn); c.setMix (1.0f); c.setLoopLen (8);
        c.setPitchRangeDeg (12); c.setReverseProb (0.7f); c.setRatchet (8); c.setSeed (555);
        std::vector<float> L (512), R (512); double ppq = 0; const double pps=(BPM/60.0)/SR; bool fin=true, bnd=true;
        for (int b = 0; b < 240; ++b)
        {
            for (int i=0;i<512;++i){ float v=0.9f*std::sin(0.03f*(float)(b*512+i)); L[(size_t)i]=v; R[(size_t)i]=-v; }
            const float traj = (float)(b % 6)/6.0f;       // sweep all styles
            c.process (0.5f, 0.6f, 0.9f, traj, 0.5f, ppq, BPM, SR, L.data(), R.data(), 512, true);
            for (int i=0;i<512;++i){ if (!(L[(size_t)i]==L[(size_t)i])) fin=false; if (std::fabs(L[(size_t)i])>8.f) bnd=false; }
            ppq += pps*512;
        }
        check (fin, "T10 finite (no NaN) under full aggression");
        check (bnd, "T10 bounded under full aggression");
    }

    // ── T11: MIX knob — mix=0 is dry-exact; mix between blends dry+wet ──────────
    {
        // mix=0 must pass the dry through untouched even while actively chopping
        Run dryRun = drive ([](FlowChop& c){ c.setMode (ChopMode::AlwaysOn); c.setMix (0.0f); c.setLoopLen (4);
                                             c.setPitchRangeDeg (7); c.setReverseProb (0.6f); },
                            sineSig, 0.5f, 0.6f, /*vary*/0.9f, /*shuffle*/0.4f, 0.0f, 80);
        bool exact = true; for (size_t i = 0; i < dryRun.out.size(); ++i) if (std::fabs (dryRun.out[i] - dryRun.dry[i]) > 1e-6f) exact = false;
        check (exact, "T11 mix=0 -> dry passthrough (testable underneath the chop)");

        // mix=0.5 must differ from both pure dry and pure wet (a real blend)
        Run wetRun = drive ([](FlowChop& c){ c.setMode (ChopMode::AlwaysOn); c.setMix (1.0f); c.setLoopLen (4); c.setReverseProb (0.6f); },
                            sineSig, 0.5f, 0.6f, 0.9f, 0.4f, 0.0f, 80);
        Run midRun = drive ([](FlowChop& c){ c.setMode (ChopMode::AlwaysOn); c.setMix (0.5f); c.setLoopLen (4); c.setReverseProb (0.6f); },
                            sineSig, 0.5f, 0.6f, 0.9f, 0.4f, 0.0f, 80);
        float dDry = 0.f, dWet = 0.f; size_t from = 30000;
        for (size_t i = from; i < midRun.out.size(); ++i) { dDry = std::max (dDry, std::fabs (midRun.out[i] - midRun.dry[i])); dWet = std::max (dWet, std::fabs (midRun.out[i] - wetRun.out[i])); }
        check (dDry > 0.02f && dWet > 0.02f, "T11 mix=0.5 -> genuine dry/wet blend");
    }

    // ── T12: NO DROPOUT — live-anchored Forward (gate=1) stays continuous ──────
    {
        // full-gate Forward at full wet should cover the timeline: no long silent gap,
        // and latency is ~1 slice (not a multi-slice loop). Proves the latency-fix.
        Run r = drive ([](FlowChop& c){ c.setMode (ChopMode::AlwaysOn); c.setMix (1.0f); c.setLoopLen (8); },
                       sineSig, 0.5f, /*gate full*/1.0f, /*vary*/0.0f, /*Forward*/0.0f, 0.0f, 120);
        // after the first slice fills, scan for the longest near-silent run
        int longest = 0, cur = 0; const float eps = 0.02f;
        for (size_t i = (size_t) (2 * STEP); i < r.out.size(); ++i) { if (std::fabs (r.out[i]) < eps) { if (++cur > longest) longest = cur; } else cur = 0; }
        char buf[96]; std::snprintf (buf, sizeof buf, "T12 no dropout: longest silent run=%d samp (< half a slice)", longest);
        check (longest < STEP / 2, buf);   // continuous coverage, no "goes out"
    }


    // T13 — RATE sweep up to 1/256 and back down must keep grooving (step-clock re-anchor).
    //   Regression for: nextStep_ was a raw step counter anchored once; moving RATE up rapid-fired
    //   the boundary loop, moving it back down parked the next boundary ~41 s in the future ->
    //   permanent silence ("can't go back down, no sound left"). One live instance, no reset.
    {
        std::printf ("[13] RATE up-to-1/256-then-down stays alive\n");
        FlowChop c; c.prepare (SR, 4.0); c.setMode (ChopMode::AlwaysOn); c.setMix (1.0f); c.setLoopLen (8);
        std::vector<float> L (512), R (512); double ppq = 0; const double pps = (BPM / 60.0) / SR; long long gc = 0;
        auto run = [&] (float rate, int blocks, double& sumSq, long long& cnt) {
            for (int b = 0; b < blocks; ++b) {
                for (int i = 0; i < 512; ++i) { float v = sineSig (gc + i); L[(size_t) i] = v; R[(size_t) i] = v; }
                c.process (rate, 0.6f, 0.0f, 0.0f, 0.0f, ppq, BPM, SR, L.data(), R.data(), 512, true);
                for (int i = 0; i < 512; ++i) { sumSq += (double) L[(size_t) i] * (double) L[(size_t) i]; ++cnt; }
                gc += 512; ppq += pps * 512;
            }
        };
        double junk = 0; long long jn = 0;
        run (0.5f, 40, junk, jn);                          // warm + groove at 1/16
        double sUp = 0; long long nUp = 0; run (1.0f, 30, sUp, nUp);   // slam to 1/256
        run (0.5f, 8, junk, jn);                           // settle after coming back down
        double sDn = 0; long long nDn = 0; run (0.5f, 30, sDn, nDn);   // measure after the return
        const double rmsDn = std::sqrt (sDn / (double) nDn);
        const double rmsUp = std::sqrt (sUp / (double) nUp);
        char b2[120]; std::snprintf (b2, sizeof b2, "T13 still grooves after RATE 1/256 -> back down (rms=%.3f, not dead)", rmsDn);
        check (rmsDn > 0.05, b2);
        check (rmsUp > 0.01 && rmsUp == rmsUp, "T13 not silent/NaN at 1/256 itself");
    }

    // ── T14 (fb106): EXTENSION CARD — click-free under the full card, every knob live ──
    {
        auto cardExt = [] (float dAmt) {
            FlowChop::ChopExtParams x;
            x.slices = 8; x.loopCells = 8; x.scan = 0.5f; x.wander = 0.4f; x.spread = 0.3f;
            x.speed = 0.4f; x.rpts = 2; x.modeOrder = 2;
            x.oSpread = .7f; x.oBias = .4f; x.oLock = .2f; x.oSeed = .44f;
            x.pRange = .5f; x.pSteps = .8f; x.pGlide = .4f; x.pQuant = .6f;
            x.rvOdds = .5f; x.rvRun = .5f; x.rvSpread = .25f; x.rvSnap = .6f;
            x.tLen = .6f; x.tCurve = .7f; x.tRand = .4f; x.tGate = .3f;
            x.rCount = .8f; x.rDecay = .5f; x.rCurve = .7f; x.rOdds = .6f;
            x.dAmt = dAmt; x.dSize = .4f; x.dSpray = .5f; x.dTone = .5f;
            x.steps = .8f; x.detune = .5f; x.wow = .6f; x.smooth = .3f;
            x.filter = 2; x.grit = .6f; x.trim = .6f;
            return x; };

        // click-free with EVERYTHING engaged (drops, revs, rolls, glide, wet bus)
        Run r = drive ([&] (FlowChop& c) { c.setExt (cardExt (0.3f)); },
                       sineSig, 0.6111f, 0.8f, 0.6f, 0.0f, 0.5f, 240);
        const float mj = maxJump (r.out, (size_t) (SR * 0.5));
        char b[120]; std::snprintf (b, sizeof b, "T14 CARD click-free: all 40+ controls hot, maxJump=%.4f", mj);
        check (mj < 0.075f, b);
        double e = 0; for (float v : r.out) e += std::fabs (v);
        check (e / (double) r.out.size() > 0.01, "T14 card settings still make sound (not silent)");
    }
    {
        // fb109 — TIME IS TRUTHFUL: the audible grid is ALWAYS the Time division;
        // Slices/Loop change pattern length + source jumps, NEVER the chop rate.
        auto fires = [] (int slices, int loopCells) {
            FlowChop c; c.prepare (SR, 8.0); c.setScale (60, MAJOR);
            FlowChop::ChopExtParams x; x.slices = slices; x.loopCells = loopCells;
            x.scan = 1.0f; x.dAmt = 0; x.rvOdds = 0; x.rOdds = 0; x.pSteps = 0; x.tGate = 0; x.tRand = 0;
            x.wander = 0; x.spread = 0; x.speed = 0; x.detune = 0; x.wow = 0; x.grit = 0; x.steps = 0; x.filter = 0;
            c.setExt (x);
            std::vector<float> L (512), R (512); double ppq = 0; const double pps = (BPM / 60.0) / SR; long long gc = 0;
            for (int bb = 0; bb < 200; ++bb)
            {
                for (int i = 0; i < 512; ++i) { float v = sineSig (gc + i); L[(size_t) i] = v; R[(size_t) i] = v; }
                c.process (0.6111f, 0.8f, 0.f, 0.f, 0.f, ppq, BPM, SR, L.data(), R.data(), 512, true);
                gc += 512; ppq += pps * 512;
            }
            return (int) c.vizFireCount(); };
        const int f88 = fires (8, 8), f84 = fires (8, 4), f416 = fires (4, 16);
        char b[160]; std::snprintf (b, sizeof b, "T15 TIME truthful: fire rate identical across geometries (8/8=%d, 8/4=%d, 4/16=%d)", f88, f84, f416);
        check (std::abs (f88 - f84) <= 2 && std::abs (f88 - f416) <= 2, b);
        // and the count matches the Time division itself: 200 blocks * 512 @48k = 2.133s
        // at 1/16 @120 BPM = 0.125 s/step ≈ 17 fires
        check (f88 >= 15 && f88 <= 19, "T15 fire count == the Time division (1/16 -> ~17 fires in 2.13 s)");
    }
    {
        // DROP at full density, size 0 → the groove becomes mostly holes (wet ≈ dry-off gaps)
        FlowChop::ChopExtParams x; x.dAmt = 1.0f; x.dSize = 0.0f;
        x.rvOdds = 0; x.rOdds = 0; x.pSteps = 0; x.wander = 0; x.spread = 0; x.speed = 0;
        x.detune = 0; x.wow = 0; x.grit = 0; x.steps = 0; x.filter = 0; x.tGate = 0; x.tRand = 0;
        Run r = drive ([&] (FlowChop& c) { c.setExt (x); c.setMix (1.0f); },
                       sineSig, 0.6111f, 0.9f, 0.f, 0.f, 0.f, 160);
        double e = 0; size_t from = (size_t) (SR * 0.5);
        for (size_t i = from; i < r.out.size(); ++i) e += std::fabs (r.out[i]);
        e /= (double) (r.out.size() - from);
        char b[120]; std::snprintf (b, sizeof b, "T16 DROP 100%%/size 0 = holes (mean |out| %.4f << dry)", e);
        check (e < 0.06, b);
    }
    {
        // WIPE: memory clear → wet goes near-flat until new audio is recorded
        FlowChop c; c.prepare (SR, 8.0); c.setScale (60, MAJOR);
        FlowChop::ChopExtParams x; x.dAmt = 0; x.rvOdds = 0; x.rOdds = 0; x.pSteps = 0;
        x.wander = 0; x.spread = 0; x.speed = 0; x.detune = 0; x.wow = 0; x.grit = 0; x.steps = 0; x.filter = 0;
        x.freeze = true;   // freeze AFTER wipe → memory stays empty → wet stays flat
        c.setExt (x); c.setMix (1.0f);
        std::vector<float> L (512), R (512); double ppq = 0; const double pps = (BPM / 60.0) / SR; long long gc = 0;
        auto go = [&] (int blocks, bool measure, double& acc, long long& n) {
            for (int bb = 0; bb < blocks; ++bb) {
                for (int i = 0; i < 512; ++i) { float v = sineSig (gc + i); L[(size_t) i] = v; R[(size_t) i] = v; }
                c.process (0.6111f, 0.9f, 0.f, 0.f, 0.f, ppq, BPM, SR, L.data(), R.data(), 512, true);
                if (measure) for (int i = 0; i < 512; ++i) { acc += std::fabs (L[(size_t) i]); ++n; }
                gc += 512; ppq += pps * 512; } };
        double junk = 0; long long jn = 0;
        { FlowChop::ChopExtParams w = x; w.freeze = false; c.setExt (w); }
        go (60, false, junk, jn);                        // fill memory, grooving
        c.wipe(); { FlowChop::ChopExtParams w = x; w.freeze = true; c.setExt (w); }
        go (8, false, junk, jn);                         // let tails release
        double e = 0; long long n = 0; go (40, true, e, n);
        e /= (double) n;
        char b[120]; std::snprintf (b, sizeof b, "T17 WIPE + freeze: memory empty, wet flat (mean |out| %.4f)", e);
        check (e < 0.02, b);
    }
    {
        // locked SEED: the flip repeats — slice order identical across two loops
        auto seqOf = [] () {
            FlowChop c; c.prepare (SR, 4.0); c.setScale (60, MAJOR);
            FlowChop::ChopExtParams x; x.modeOrder = 2; x.oSeed = 0.5f; x.oLock = 0.f; x.rpts = 1;
            x.dAmt = 0; x.rvOdds = 0; x.rOdds = 0; x.pSteps = 0; x.wander = 0; x.spread = 0; x.speed = 0;
            x.detune = 0; x.wow = 0; x.grit = 0; x.steps = 0; x.filter = 0; x.tGate = 0; x.tRand = 0;
            c.setExt (x);
            std::vector<int> seq; std::vector<float> L (STEP), R (STEP);
            double ppq = 0; const double pps = (BPM / 60.0) / SR; long long gc = 0;
            for (int st = 0; st < 32; ++st)
            {
                for (int i = 0; i < STEP; ++i) { float v = sineSig (gc + i); L[(size_t) i] = v; R[(size_t) i] = v; }
                c.process (0.6111f, 1.0f, 0.f, 0.f, 0.f, ppq, BPM, SR, L.data(), R.data(), STEP, true);
                seq.push_back (c.lastSliceIndex());
                gc += STEP; ppq += pps * STEP;
            }
            return seq; };
        auto sq = seqOf();
        bool locked = true;
        for (int i = 8; i < 16; ++i) locked = locked && (sq[(size_t) i] == sq[(size_t) (i + 8)]) && (sq[(size_t) i + 8] == sq[(size_t) (i + 16)]);
        check (locked, "T18 locked SEED: slice order repeats loop after loop");
        bool moved = false; for (int i = 9; i < 16; ++i) moved = moved || sq[(size_t) i] != sq[(size_t) i - 1] - 0; // shuffled, not constant
        bool notFwd = false; for (int i = 8; i < 16; ++i) notFwd = notFwd || sq[(size_t) i] != (i % 8);
        check (notFwd, "T18 locked SEED is a real shuffle (not forward order)");
    }

    // ── T19 (fb110): ORDER lane works in EVERY mode ──────────────────────────
    {
        auto seqIn = [] (float sprd, int mode) {
            FlowChop c; c.prepare (SR, 8.0); c.setScale (60, MAJOR);
            FlowChop::ChopExtParams x; x.slices = 8; x.loopCells = 8; x.modeOrder = mode;
            x.oSpread = sprd; x.oSeed = 0.5f; x.rpts = 1;
            x.dAmt = 0; x.rvOdds = 0; x.rOdds = 0; x.pSteps = 0; x.wander = 0; x.spread = 0;
            x.speed = 0; x.detune = 0; x.wow = 0; x.grit = 0; x.steps = 0; x.filter = 0; x.tGate = 0; x.tRand = 0;
            c.setExt (x); c.setMix (1.0f);
            std::vector<int> seq; std::vector<float> L (STEP), R (STEP);
            double ppq = 0; const double pps = (BPM / 60.0) / SR; long long gc = 0;
            for (int st = 0; st < 16; ++st)
            {
                for (int i = 0; i < STEP; ++i) { float v = sineSig (gc + i); L[(size_t) i] = v; R[(size_t) i] = v; }
                c.process (0.6111f, 0.55f, 0.f, 0.f, 0.f, ppq, BPM, SR, L.data(), R.data(), STEP, true);
                seq.push_back (c.lastSliceIndex());
                gc += STEP; ppq += pps * STEP;
            }
            return seq; };
        auto s0 = seqIn (0.0f, 0), s1 = seqIn (1.0f, 0);
        bool fwd = true; for (int i = 0; i < 16; ++i) fwd = fwd && (s0[(size_t) i] == i % 8);
        bool strays = false; for (int i = 0; i < 16; ++i) strays = strays || (s1[(size_t) i] != i % 8);
        check (fwd,    "T19 ORDER Sprd 0 in Step mode = exact forward (transparent default)");
        check (strays, "T19 ORDER Sprd 1 in Step mode = slices stray (the lane works in EVERY mode)");
    }
    // ── T20 (fb110): TRIM Len is the authority — 1.0 reaches (near-)full legato ──
    {
        auto meanAt = [] (float tLen) {
            FlowChop c; c.prepare (SR, 8.0); c.setScale (60, MAJOR);
            FlowChop::ChopExtParams x; x.tLen = tLen;
            x.dAmt = 0; x.rvOdds = 0; x.rOdds = 0; x.pSteps = 0; x.wander = 0; x.spread = 0;
            x.speed = 0; x.detune = 0; x.wow = 0; x.grit = 0; x.steps = 0; x.filter = 0; x.tGate = 0; x.tRand = 0;
            c.setExt (x); c.setMix (1.0f);
            std::vector<float> L (512), R (512);
            double ppq = 0; const double pps = (BPM / 60.0) / SR; long long gc = 0; double acc = 0; long long n = 0;
            for (int b = 0; b < 300; ++b)
            {
                for (int i = 0; i < 512; ++i) { float v = sineSig (gc + i); L[(size_t) i] = v; R[(size_t) i] = v; }
                c.process (0.6111f, 0.55f, 0.f, 0.f, 0.f, ppq, BPM, SR, L.data(), R.data(), 512, true);
                if (b > 40) for (int i = 0; i < 512; ++i) { acc += std::fabs (L[(size_t) i]); ++n; }
                gc += 512; ppq += pps * 512;
            }
            return acc / (double) n; };
        const double full = meanAt (1.0f), tiny = meanAt (0.05f);
        char b[140]; std::snprintf (b, sizeof b, "T20 TRIM authority: Len 1.0 near-legato vs Len 0.05 minced (%.4f vs %.4f)", full, tiny);
        check (full > tiny * 2.5 && full > 0.15, b);
    }

    // ── T21 (fb111): PITCH is a deterministic musical pattern — no dice anywhere ──
    {
        auto runOut = [] (float range, float steps) {
            FlowChop c; c.prepare (SR, 8.0); c.setScale (60, MAJOR);
            FlowChop::ChopExtParams x;
            x.pRange = range; x.pSteps = steps; x.pQuant = 0.0f; x.pGlide = 0.0f;
            x.dAmt = 0; x.rvOdds = 0; x.rOdds = 0; x.wander = 0; x.spread = 0; x.speed = 0;
            x.detune = 0; x.wow = 0; x.grit = 0; x.steps = 0; x.filter = 0; x.tGate = 0; x.tRand = 0;
            c.setExt (x); c.setMix (1.0f);
            std::vector<float> out; std::vector<float> L (512), R (512);
            double ppq = 0; const double pps = (BPM / 60.0) / SR; long long gc = 0;
            for (int b = 0; b < 200; ++b)
            {
                for (int i = 0; i < 512; ++i) { float v = sineSig (gc + i); L[(size_t) i] = v; R[(size_t) i] = v; }
                c.process (0.6111f, 0.55f, 0.f, 0.f, 0.f, ppq, BPM, SR, L.data(), R.data(), 512, true);
                for (int i = 0; i < 512; ++i) out.push_back (L[(size_t) i]);
                gc += 512; ppq += pps * 512;
            }
            return out; };
        auto a1 = runOut (1.0f, 1.0f), a2 = runOut (1.0f, 1.0f), b0 = runOut (0.0f, 1.0f);
        bool identical = a1.size() == a2.size();
        for (size_t i = 0; identical && i < a1.size(); ++i) identical = a1[i] == a2[i];
        check (identical, "T21 PITCH pattern fully deterministic (two fresh instances = bit-identical)");
        double d = 0; for (size_t i = 0; i < a1.size(); ++i) d += std::fabs (a1[(size_t) i] - b0[(size_t) i]);
        check (d / (double) a1.size() > 0.01, "T21 Range 12 / Steps All audibly repitches (octave jumps present)");
    }

    // ── T22 (fb113): COLLECT keeps the MOMENT, not the silence ───────────────
    {
        // half a second of melody, then true silence. The old slow gate recorded
        // ~160 ms of dead air after the phrase, so the "newest memory" was silence
        // and collected playback dropped out (Max: "collect is ass"). Now the
        // memory must end where the music ends — chopping during the silence
        // replays the MELODY; and the crossfaded splices must stay impulse-free.
        auto noteThenSilence = [] (long long t) {
            // real notes END with a release fade — an instant cut would put the
            // input's own click into the memory and frame the engine for it
            float env = 0.0f;
            if      (t < 23520) env = 1.0f;
            else if (t < 24000) env = 0.5f + 0.5f * std::cos (3.14159265f * (float) (t - 23520) / 480.0f);
            return 0.4f * env * std::sin (2.0f * 3.14159265f * (float) t / 218.0f); };
        auto silRms = [&] (bool collect) {
            FlowChop c; c.prepare (SR, 8.0); c.setScale (60, MAJOR);
            FlowChop::ChopExtParams x; x.collect = collect; x.tLen = 1.0f;
            x.dAmt = 0; x.rvOdds = 0; x.rOdds = 0; x.pSteps = 0; x.wander = 0; x.spread = 0;
            x.speed = 0; x.detune = 0; x.wow = 0; x.grit = 0; x.steps = 0; x.filter = 0; x.tGate = 0; x.tRand = 0;
            c.setExt (x); c.setMix (1.0f);
            std::vector<float> L (512), R (512);
            double ppq = 0; const double pps = (BPM / 60.0) / SR; long long gc = 0;
            double acc = 0, d2max = 0; long long n = 0; float p1 = 0, p2 = 0;
            for (int b = 0; b < 400; ++b)
            {
                for (int i = 0; i < 512; ++i) { float v = noteThenSilence (gc + i); L[(size_t) i] = v; R[(size_t) i] = v; }
                c.process (0.6111f, 0.55f, 0.f, 0.f, 0.f, ppq, BPM, SR, L.data(), R.data(), 512, true);
                for (int i = 0; i < 512; ++i)
                {
                    if (gc > 96256)   // history warm — never seed the detector with zeros mid-signal
                    {
                        acc += (double) L[(size_t) i] * (double) L[(size_t) i]; ++n;
                        const double d2 = std::fabs ((double) L[(size_t) i] - 2.0 * p1 + p2);
                        if (d2 > d2max) d2max = d2;
                    }
                    p2 = p1; p1 = L[(size_t) i];
                }
                gc += 512; ppq += pps * 512;
            }
            return std::pair<double,double> (std::sqrt (acc / (double) n), d2max); };
        auto on = silRms (true), off = silRms (false);
        char b[160];
        std::snprintf (b, sizeof b, "T22 COLLECT keeps the moment: melody replays through the silence (RMS %.3f vs %.3f off)", on.first, off.first);
        check (on.first > 0.05 && off.first < 0.005, b);
        std::snprintf (b, sizeof b, "T22 collected splices are fades, not joints (max 2nd-diff %.4f)", on.second);
        check (on.second < 0.012, b);
    }
    // ── T23 (fb113): REPEAT Count is instantly audible (Odds carves down from All) ──
    {
        auto fires = [] (float count) {
            FlowChop c; c.prepare (SR, 8.0); c.setScale (60, MAJOR);
            FlowChop::ChopExtParams x; x.rCount = count;   // Odds stays at its default (All)
            x.dAmt = 0; x.rvOdds = 0; x.pSteps = 0; x.wander = 0; x.spread = 0;
            x.speed = 0; x.detune = 0; x.wow = 0; x.grit = 0; x.steps = 0; x.filter = 0; x.tGate = 0; x.tRand = 0;
            c.setExt (x); c.setMix (1.0f);
            std::vector<float> L (512), R (512);
            double ppq = 0; const double pps = (BPM / 60.0) / SR; long long gc = 0;
            for (int b = 0; b < 200; ++b)
            {
                for (int i = 0; i < 512; ++i) { float v = sineSig (gc + i); L[(size_t) i] = v; R[(size_t) i] = v; }
                c.process (0.6111f, 0.55f, 0.f, 0.f, 0.f, ppq, BPM, SR, L.data(), R.data(), 512, true);
                gc += 512; ppq += pps * 512;
            }
            return (int) c.vizFireCount(); };
        const int f1 = fires (0.0f), f4 = fires (1.0f);
        char b[140]; std::snprintf (b, sizeof b, "T23 REPEAT: Count 4 rolls ~4x Count 1 on first touch (%d vs %d fires)", f4, f1);
        check (f4 > f1 * 3 && f4 < f1 * 5, b);
    }

    std::printf ("\n%d checks, %d failed\n", g_checks, g_fail);
    if (g_fail == 0) std::printf ("ALL %d CHECKS PASSED\n", g_checks);
    return g_fail == 0 ? 0 : 1;
}
