// ══════════════════════════════════════════════════════════════════════════════════════════════
//  glitch_chance_cert.cpp — DOES GLITCH'S CHANCE KNOB DO WHAT IT SAYS?  (Lane C, fb517)
//
//    clang++ -std=c++17 -O2 -I Tests/shim -I Source -o /tmp/gcc_cert Tests/glitch_chance_cert.cpp
//    /tmp/gcc_cert
//
//  Chance on the Monitor card = the VARY macro = FLOW_GLI_VARY (0..1, 1:1 — the card's BIND
//  pushes S.v.chance straight into setNormalisedValue, the param is NormalisableRange(0,1),
//  flowBase() clamps and hands it to FlowGlitch::process as `vary`).
//
//  The spec (FlowGlitch.h onBoundaryExt):  chance = clamp01(vary * (1 + traj*0.5)); a slot
//  fires when its hash die < chance. Déjà Vu picks per slot between a LOCKED die stream
//  (keyed to Seed — the SAME die forever) and a FRESH one (re-keyed every pattern loop).
//
//  What this cert pins down, with numbers:
//    T1  fire-rate vs Chance: 0 -> ZERO, 100 -> EVERY slot, strictly monotone between,
//        empirical rate within tol of the knob value (dejavu 0 = fresh dice).
//    T2  the LOCKED stream (dejavu 1): the fired set is IDENTICAL loop after loop and
//        run after run; sweeping Chance 0..1 in 1% steps crosses at most loopLen
//        thresholds — the largest do-nothing plateau of knob travel is MEASURED and
//        printed (this is the "sometimes works" a user feels when Déjà Vu is high).
//    T3  Chaos (traj) boosts the odds — and cannot create fires at Chance 0.
//    T4  Drop at 100% keeps every fire but punches it silent — Chance reads "on" while
//        the ear hears holes (masking quantified in dB).
//    T5  Burst extends landed fires (streaks); it cannot seed a fire at Chance 0.
//    T6  Roll forces a fire at Chance 0 (spec: works independent of Chance).
//    T7  no dead zone at the knob's bottom, no early saturation at its top.
//
//  A GATE THAT HAS NEVER FAILED HAS NEVER BEEN TESTED: run the two mutations and the
//  relevant gates MUST go red (kill evidence recorded in the lane report):
//    -DMUT_CHANCE_CONST  chance hard-wired to 0.5 (a dead relay)   -> T1/T3/T7 fail
//    -DMUT_NO_LOCK       the locked stream never selected           -> T2 fails
//  (The mutations are applied by sed to a COPY of FlowGlitch.h in the scratchpad and the
//  harness is compiled with that copy first on the include path — Source/ stays untouched.)
// ══════════════════════════════════════════════════════════════════════════════════════════════
#include "FlowGlitch.h"
#include <cstdio>
#include <cstring>
#include <vector>
#include <cmath>
#include <functional>

using namespace wc;

static int g_checks = 0, g_fail = 0;
static void check (bool ok, const char* what)
{ ++g_checks; std::printf ("  %s  %s\n", ok ? "PASS" : "FAIL:", what); if (! ok) ++g_fail; }

constexpr double BPM = 120.0, SR = 48000.0;
constexpr int    BLK = 512;
// rate knob 0.6111 = the shipped default = ladder index 11 = 1/16 -> 0.25 beats/step
constexpr float  RATE = 0.6111f;
constexpr int    STEP = 6000;             // 0.25 beats @120BPM @48k

static float sig (long long n) { return 0.5f * std::sin (2.0f * 3.14159265f * (float) n / 1500.0f); }

// ── drive the ext path exactly the way PluginProcessor does (setExt every block) ──────────────
struct Run
{
    long long fires = 0;                  // vizFireCount at the end
    std::vector<int> fireStep;            // step index of each fire commit
    double rmsOut = 0.0, rmsDry = 0.0;
};

static Run drive (const GlitchExtParams& P, float vary, float traj, int nSteps,
                  bool roll = false, float mix = 1.0f)
{
    FlowGlitch g; g.prepare (SR, 4.0);
    g.setMix (mix);
    Run r;
    const long long total = (long long) nSteps * STEP;
    std::vector<float> L ((size_t) BLK), R ((size_t) BLK);
    double ppq = 0.0; const double pps = (BPM / 60.0) / SR;
    long long gc = 0, lastFc = 0;
    double so = 0.0, sd = 0.0; long long ns = 0;
    while (gc < total)
    {
        for (int i = 0; i < BLK; ++i) { const float v = sig (gc + i); L[(size_t) i] = v; R[(size_t) i] = v; }
        g.setExt (P);                                        // per block, like the processor
        if (roll && gc >= (long long) STEP * 4 && gc < (long long) STEP * 4 + BLK)
            g.rollNow();
        g.process (RATE, 0.55f, vary, traj, 0.0f, ppq, BPM, SR, L.data(), R.data(), BLK, true);
        const long long fc = g.vizFireCount();
        if (fc != lastFc)
        {
            for (long long k = lastFc; k < fc; ++k) r.fireStep.push_back ((int) (gc / STEP));
            lastFc = fc;
        }
        // steady-state RMS window: skip the first 4 steps (buffer priming)
        if (gc >= (long long) STEP * 4)
            for (int i = 0; i < BLK; ++i)
            { so += (double) L[(size_t) i] * L[(size_t) i];
              const float d = sig (gc + i); sd += (double) d * d; ++ns; }
        gc += BLK; ppq += pps * BLK;
    }
    r.fires = lastFc;
    r.rmsOut = ns ? std::sqrt (so / (double) ns) : 0.0;
    r.rmsDry = ns ? std::sqrt (sd / (double) ns) : 0.0;
    return r;
}

static GlitchExtParams base()
{
    GlitchExtParams P;          // defaults: REP only, dejavu 0, seed 0 (free), loop 8, sync on
    return P;
}

int main()
{
    std::printf ("GLITCH CHANCE cert — vary -> fire probability (the ext/Monitor path, the one the plugin runs)\n");
#if defined (MUT_CHANCE_CONST)
    std::printf ("  !! MUTATED BUILD: chance hard-wired to 0.5 — the T1/T3/T7 gates MUST fail\n");
#endif
#if defined (MUT_NO_LOCK)
    std::printf ("  !! MUTATED BUILD: locked dice stream disabled — the T2 gates MUST fail\n");
#endif

    const int N = 640;                                       // 640 steps = 80 loops of 8 = 66,000+ dice
    // how many boundaries actually fire at chance >= 1 (the denominator for every rate below)
    const Run all = drive (base(), 1.0f, 0.0f, N);
    const double nB = (double) all.fires;

    // ── T1: fire rate tracks the knob ─────────────────────────────────────────────────────────
    {
        char buf[160];
        const Run z = drive (base(), 0.0f, 0.0f, N);
        std::snprintf (buf, sizeof buf, "T1 Chance 0 -> ZERO fires (got %lld)", z.fires);
        check (z.fires == 0, buf);

        std::snprintf (buf, sizeof buf, "T1 Chance 100 -> EVERY slot fires (%lld of %d boundaries)", all.fires, N);
        check (all.fires >= N - 2, buf);                     // clock anchoring can eat the first boundary

        const float cs[4] = { 0.10f, 0.25f, 0.50f, 0.75f };
        long long prev = 0; bool mono = true, tol = true;
        double rates[4];
        for (int i = 0; i < 4; ++i)
        {
            const Run r = drive (base(), cs[i], 0.0f, N);
            rates[i] = (double) r.fires / nB;
            if (r.fires <= prev) mono = false;
            if (std::fabs (rates[i] - (double) cs[i]) > 0.05) tol = false;
            prev = r.fires;
        }
        std::snprintf (buf, sizeof buf,
                       "T1 rate ~= knob: 10->%.1f%%  25->%.1f%%  50->%.1f%%  75->%.1f%% (tol 5%%)",
                       rates[0] * 100, rates[1] * 100, rates[2] * 100, rates[3] * 100);
        check (tol, buf);
        check (mono && all.fires > prev, "T1 strictly monotonic across 0 < 10 < 25 < 50 < 75 < 100");
    }

    // ── T2: the LOCKED stream (Deja Vu 1) — same pattern forever, thresholds counted ─────────
    {
        char buf[200];
        GlitchExtParams P = base(); P.dejavu = 1.0f; P.seed = 7;

        // (a) the pattern repeats loop after loop and run after run
        const Run a = drive (P, 0.5f, 0.0f, 64);
        const Run b = drive (P, 0.5f, 0.0f, 64);
        bool same = a.fireStep.size() == b.fireStep.size();
        if (same) for (size_t i = 0; i < a.fireStep.size(); ++i) if (a.fireStep[i] != b.fireStep[i]) { same = false; break; }
        check (same, "T2 locked dice: identical fired steps run-to-run at fixed settings");

        bool loopSame = true;                                // slots fired in loop k == loop k+1
        {
            std::vector<int> s0, s1;
            for (int st : a.fireStep) { if (st >= 8 && st < 16) s0.push_back (st % 8); if (st >= 16 && st < 24) s1.push_back (st % 8); }
            loopSame = s0 == s1 && ! s0.empty();
        }
        check (loopSame, "T2 locked dice: the SAME slots fire every pattern loop (true deja vu)");

        // (b) sweep Chance 1%-wise: the fired-per-loop count is a staircase with <= loopLen
        //     risers; measure the widest do-nothing plateau (the knob travel a user must cross
        //     before ANYTHING changes) — this is the spec'd "sometimes works" quirk, quantified.
        int prevCnt = -1, changes = 0; double plateau = 0.0, widest = 0.0; double lastChange = 0.0;
        for (int c = 0; c <= 100; c += 1)
        {
            const Run r = drive (P, (float) c / 100.0f, 0.0f, 24);   // 3 loops is plenty (locked)
            int cnt = 0; for (int st : r.fireStep) if (st >= 8 && st < 16) ++cnt;
            if (prevCnt >= 0 && cnt != prevCnt)
            { ++changes; plateau = (double) c / 100.0 - lastChange; if (plateau > widest) widest = plateau; lastChange = (double) c / 100.0; }
            prevCnt = cnt;
        }
        const double tail = 1.0 - lastChange; if (tail > widest) widest = tail;
        std::snprintf (buf, sizeof buf,
                       "T2 locked sweep: %d threshold crossings (loopLen=8 -> expect <=8); widest dead plateau = %.0f%% of knob travel",
                       changes, widest * 100.0);
        check (changes >= 1 && changes <= 8, buf);
        std::printf ("       (Deja Vu 1: nudging Chance inside a plateau changes NOTHING — by design. Turn Deja Vu down for fresh dice.)\n");
    }

    // ── T3: Chaos boosts the odds; cannot conjure fires at Chance 0 ───────────────────────────
    {
        char buf[160];
        const Run t0 = drive (base(), 0.40f, 0.0f, N);
        const Run t1 = drive (base(), 0.40f, 1.0f, N);
        const double r0 = (double) t0.fires / nB, r1 = (double) t1.fires / nB;
        std::snprintf (buf, sizeof buf, "T3 Chaos boost: Chance 40 fires %.1f%% -> %.1f%% with Chaos 100 (spec: x1.5 = 60%%)",
                       r0 * 100, r1 * 100);
        check (r1 > r0 + 0.10 && std::fabs (r1 - 0.60) < 0.06, buf);
        const Run tz = drive (base(), 0.0f, 1.0f, N);
        std::snprintf (buf, sizeof buf, "T3 Chance 0 + Chaos 100 -> still ZERO fires (got %lld)", tz.fires);
        check (tz.fires == 0, buf);
    }

    // ── T4: Drop 100 keeps the fires but punches them silent (masks Chance at the ear) ────────
    {
        char buf[200];
        GlitchExtParams P = base(); P.drop = 1.0f;
        const Run d = drive (P, 1.0f, 0.0f, 96);
        const double dB = 20.0 * std::log10 (d.rmsOut / (d.rmsDry > 0 ? d.rmsDry : 1.0) + 1e-9);
        std::snprintf (buf, sizeof buf,
                       "T4 Drop 100 @ Chance 100: every slot still FIRES (%lld) but output is holes (%.1f dB vs dry)",
                       d.fires, dB);
        check (d.fires >= 90 && dB < -12.0, buf);
        std::printf ("       (a user reading the Chance knob hears SILENCE here — Drop is the masker, not Chance)\n");
    }

    // ── T5: Burst streaks landed fires; cannot seed at Chance 0 ───────────────────────────────
    {
        char buf[160];
        GlitchExtParams P = base(); P.burst = 1.0f;
        const Run s = drive (P, 0.15f, 0.0f, N);
        const double rs = (double) s.fires / nB;
        std::snprintf (buf, sizeof buf, "T5 Burst 100 @ Chance 15: streaks chain the loop (rate %.1f%% >> 15%%)", rs * 100);
        check (rs > 0.60, buf);
        const Run z = drive (P, 0.0f, 0.0f, N);
        std::snprintf (buf, sizeof buf, "T5 Burst 100 @ Chance 0 -> ZERO fires (a streak needs a seed; got %lld)", z.fires);
        check (z.fires == 0, buf);
    }

    // ── T6: Roll fires at Chance 0 ────────────────────────────────────────────────────────────
    {
        char buf[120];
        const Run r = drive (base(), 0.0f, 0.0f, 24, /*roll*/ true);
        std::snprintf (buf, sizeof buf, "T6 Roll punch-in fires at Chance 0 (got %lld fire%s)", r.fires, r.fires == 1 ? "" : "s");
        check (r.fires >= 1, buf);
    }

    // ── T7: knob ends — no dead zone at 2, no saturation at 98 ────────────────────────────────
    {
        char buf[160];
        const Run lo = drive (base(), 0.02f, 0.0f, N);
        const Run hi = drive (base(), 0.98f, 0.0f, N);
        std::snprintf (buf, sizeof buf, "T7 Chance 2 still fires sometimes (%lld of %.0f); Chance 98 still skips sometimes (%lld of %.0f)",
                       lo.fires, nB, hi.fires, nB);
        check (lo.fires >= 1 && hi.fires < (long long) nB, buf);
    }

    std::printf ("\n%d checks, %d failed\n", g_checks, g_fail);
    return g_fail == 0 ? 0 : 1;
}
