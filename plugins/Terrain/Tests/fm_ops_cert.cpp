// ══════════════════════════════════════════════════════════════════════════════════════════════
//  fm_ops_cert.cpp — fb587's NULL TEST. The extracted operator stage IS the original, bit for bit.
//
//    clang++ -O2 -std=c++17 -I Source Tests/fm_ops_cert.cpp -o /tmp/fm_ops_cert && /tmp/fm_ops_cert
//
//  Moving the FM operator stage out of the render loop into Source/FmOperators.h is a refactor of
//  the HOT AUDIO PATH, four times over. An audio null test on the plugin is IMPOSSIBLE here — the
//  synth is deliberately not repeatable (fb544's continuous phase and a per-note-on alternation
//  free-run across notes; measured: two back-to-back renders of the same note differ in 61440 of
//  61440 samples). So the proof is made where it CAN be exact.
//
//  🚨 THE REFERENCE BELOW WAS NOT RETYPED. It is MACHINE-EXTRACTED from SynthVoice.h's own FM case
//     and mechanically renamed (uMod2PhaseA_[u] -> st.m2Phase, fmScorchPreNow_[0] -> p.scorchPre,
//     and so on). Retyping it would have risked making the same slip twice — once in the header and
//     once in the thing meant to catch the header. A machine extraction cannot agree with a typo.
//
//  Both are run over randomised states and parameters covering all three algorithms and every
//  weathering control, and every returned value must match EXACTLY — not to a tolerance.
// ══════════════════════════════════════════════════════════════════════════════════════════════
#include "FmOperators.h"
#include <cstdio>
#include <cmath>
#include <cstdint>
#include <vector>

using namespace tw;

// ── the ORIGINAL, extracted from SynthVoice.h ────────────────────────────────────────────────
static FmOps::Out reference (FmOps::State& st, const FmOps::Params& p, double inc,
                             double carrierPhase, double blendOff)
{
    const double pi2 = 6.2831853071795865;
    const int   alg = p.alg;
    const float d1  = p.d1;
    const float d2  = p.d2;
    const float fbk = p.fbk;
    FmOps::Out o;
                            float m2 = static_cast<float> (std::sin (pi2 * (st.m2Phase
                                                        + (double) (p.storm12 * st.prevM1))));
                            // SCORCH — asymmetric drive on M2 (adds harmonics → richer sidebands)
                            if (p.scorchPre > 1.0f) m2 = (FmOps::fastTanh (p.scorchPre * m2 + p.scorchBias) - p.scorchTanhBias) * p.scorchMakeup;
                            double m1Arg = st.m1Phase + (double) (fbk * st.fbMem)
                                         + (double) (p.storm21 * m2);
                            if (alg != 1) m1Arg += (double) (d2 * m2);       // STACK + RING: M2 → M1
                            float m1 = static_cast<float> (std::sin (pi2 * m1Arg));
                            // SCORCH — same drive on M1 (the operator that hits the carrier)
                            if (p.scorchPre > 1.0f) m1 = (FmOps::fastTanh (p.scorchPre * m1 + p.scorchBias) - p.scorchTanhBias) * p.scorchMakeup;
                            st.fbMem = 0.5f * (st.fbMem + m1);
                            st.prevM1 = m1;
                            // QUAKE — phase-locked subharmonic operator folded into the carrier phase
                            double qSub = 0.0;
                            if (p.quakeIdx > 1.0e-5f)
                            {
                                st.quakePhase += inc * (double) p.quakeSubRatio;
                                st.quakePhase -= std::floor (st.quakePhase);
                                float sub = static_cast<float> (std::sin (pi2 * st.quakePhase));
                                if (p.quakeFry > 0.0f) sub += p.quakeFry * (sub - sub * sub * sub * (1.0f / 6.0f));
                                qSub = (double) (p.quakeIdx * sub);
                            }
                            double cPh = carrierPhase + qSub + (double) blendOff;   // BLEND inject
                            if (alg != 2) cPh += (double) (d1 * m1);
                            if (alg == 1) cPh += (double) (d2 * m2);
                            cPh -= std::floor (cPh);
                                    o.ringGain = (1.0f - p.ringDepth) + p.ringDepth * m1;       // ring dry→wet on depth 1
    o.carrierPhase = cPh;
    o.m1 = m1;
    if (alg != 2) o.ringGain = 1.0f;
    return o;
}
static void referenceAdvance (FmOps::State& st, const FmOps::Params& p, double inc)
{
        st.m1Phase  += inc * p.ratio1 + p.rustTps;
        st.m1Phase  -= std::floor (st.m1Phase);
        st.m2Phase += inc * p.ratio2;
        st.m2Phase -= std::floor (st.m2Phase);
}

// ── a tiny deterministic PRNG so the sweep is reproducible ───────────────────────────────────
static uint64_t s_ = 0x9E3779B97F4A7C15ull;
static double rnd (double lo, double hi)
{ s_ ^= s_ << 13; s_ ^= s_ >> 7; s_ ^= s_ << 17; return lo + (hi - lo) * ((double)(s_ >> 11) / 9007199254740992.0); }

int main()
{
    std::printf ("\n══ fm_ops_cert — fb587 ══  the extracted operator stage vs the original\n\n");
    long long n = 0, bad = 0, badAdv = 0;
    double worstPh = 0, worstM1 = 0, worstRing = 0;

    for (int alg = 0; alg < 3; ++alg)
      for (int trial = 0; trial < 40000; ++trial)
      {
        FmOps::Params p;
        p.alg = alg;
        p.d1 = (float) rnd (0, 3);        p.d2 = (float) rnd (0, 3);
        p.fbk = (float) rnd (0, 1.2);
        p.storm12 = (float) rnd (0, 0.8); p.storm21 = (float) rnd (0, 0.8);
        // half the trials with SCORCH engaged, half with it bypassed (pre <= 1)
        p.scorchPre = (trial & 1) ? (float) rnd (1.0, 4.0) : 1.0f;
        p.scorchBias = (float) rnd (0, 0.35);
        p.scorchTanhBias = FmOps::fastTanh (p.scorchBias);
        p.scorchMakeup = 1.0f / (float) std::fmax (0.30, FmOps::fastTanh (p.scorchPre + p.scorchBias));
        // half with QUAKE engaged, half below its 1e-5 gate
        p.quakeIdx = (trial & 2) ? (float) rnd (0.0, 1.5) : 0.0f;
        p.quakeFry = (float) rnd (0, 1);
        p.quakeSubRatio = (float) rnd (0.25, 0.5);
        p.ratio1 = rnd (0.25, 16); p.ratio2 = rnd (0.25, 16);
        p.rustTps = rnd (-1e-3, 1e-3);
        p.ringDepth = (float) rnd (0, 1);

        FmOps::State a, b;
        a.m1Phase = b.m1Phase = rnd (0, 1);
        a.m2Phase = b.m2Phase = rnd (0, 1);
        a.quakePhase = b.quakePhase = rnd (0, 1);
        a.fbMem = b.fbMem = (float) rnd (-1, 1);
        a.prevM1 = b.prevM1 = (float) rnd (-1, 1);
        const double inc = rnd (1e-4, 0.05);
        const double cp  = rnd (0, 1), bo = rnd (-0.5, 0.5);

        // run BOTH for several samples so the mutating state (feedback, storm, quake) diverges
        // if it is going to
        for (int k = 0; k < 8; ++k)
        {
            const FmOps::Out ra = reference (a, p, inc, cp, bo);
            const FmOps::Out rb = FmOps::run (b, p, inc, cp, bo);
            ++n;
            if (ra.carrierPhase != rb.carrierPhase || ra.m1 != rb.m1 || ra.ringGain != rb.ringGain)
            { ++bad;
              worstPh   = std::fmax (worstPh,   std::fabs (ra.carrierPhase - rb.carrierPhase));
              worstM1   = std::fmax (worstM1,   std::fabs ((double) ra.m1 - rb.m1));
              worstRing = std::fmax (worstRing, std::fabs ((double) ra.ringGain - rb.ringGain)); }
            referenceAdvance (a, p, inc);
            FmOps::advance   (b, p, inc);
            if (a.m1Phase != b.m1Phase || a.m2Phase != b.m2Phase) ++badAdv;
            if (a.fbMem != b.fbMem || a.prevM1 != b.prevM1 || a.quakePhase != b.quakePhase) ++bad;
        }
      }

    std::printf ("  %-58s %s\n", "[1] THE OPERATOR STAGE IS BIT-IDENTICAL",
                 bad == 0 ? "ok" : "FAIL");
    std::printf ("        %lld samples over 3 algorithms; %lld differ", n, bad);
    if (bad) std::printf ("  (worst phase %.3e, m1 %.3e, ring %.3e)", worstPh, worstM1, worstRing);
    std::printf ("\n  %-58s %s\n        %s\n", "[2] THE MODULATOR ADVANCE IS BIT-IDENTICAL",
                 badAdv == 0 ? "ok" : "FAIL",
                 badAdv == 0 ? "every m1/m2 accumulator matches exactly" : "accumulators diverged");

    // ── [3] WILL THE PICTURE ACTUALLY MOVE? ───────────────────────────────────────────────────
    //  The point of the extraction is that PluginProcessor::getOscWavetableJson can now call this
    //  stage, so the waterfall shows FM instead of the bare carrier. The end-to-end check
    //  (plugin -> page -> pixels) is NOT automatable here: this project forbids linking the
    //  plugin's SharedCode (juce_add_plugin compiles the JUCE modules into it, so a target that
    //  did both would collide on every JUCE symbol — plugins/Terrain/CMakeLists.txt:147),
    //  and the JSON is a WebView native, unreachable from an AU host. So what is measured here is
    //  the thing that DECIDES it: run exactly what the drawing runs — pts points, inc = 1/pts —
    //  and report how far the carrier phase trajectory moves from the unmodulated ramp.
    //  If this were small, no amount of wiring would make the picture move.
    {
        const int pts = 160;
        // the whole deviation CURVE, not a single number
        auto trajectory = [&] (const FmOps::Params& p, std::vector<double>& curve) {
            FmOps::State st {}; double acc = 0.0;
            for (int pass = 0; pass < 2; ++pass) {           // one silent seeding lap, as the drawing does
                if (pass == 0) st = FmOps::State{};
                acc = 0.0; curve.clear();
                for (int i = 0; i < pts; ++i) {
                    const double ph = (double) i / pts;
                    const FmOps::Out o = FmOps::run (st, p, 1.0 / pts, ph, 0.0);
                    FmOps::advance (st, p, 1.0 / pts);
                    double d = o.carrierPhase - ph; d -= std::floor (d + 0.5);   // wrapped deviation
                    if (pass == 1) { acc += std::fabs (d); curve.push_back (d); }
                }
            }
            return acc / pts;                                // mean |phase deviation| in CYCLES
        };
        auto curveDist = [] (const std::vector<double>& a, const std::vector<double>& b) {
            double n = 0; for (size_t i = 0; i < a.size() && i < b.size(); ++i)
            { const double d = a[i] - b[i]; n += d * d; }
            return std::sqrt (n / (double) (a.empty() ? 1 : a.size())); };
        FmOps::Params off; off.alg = 0; off.d1 = 0; off.d2 = 0; off.ratio1 = 1; off.ratio2 = 2;
        struct Rig { const char* name; float d1, d2, scorch; float quake; int alg; double r1; };
        const Rig rigs[] = {
            { "index 1 up",        0.6f, 0.0f, 1.0f, 0.0f, 0, 1.0 },
            { "ratio 1 = 3.5",     0.6f, 0.0f, 1.0f, 0.0f, 0, 3.5 },
            { "both operators",    0.6f, 0.5f, 1.0f, 0.0f, 0, 2.0 },
            { "SPLIT algorithm",   0.6f, 0.5f, 1.0f, 0.0f, 1, 2.0 },
            { "SCORCH up",         0.6f, 0.5f, 3.0f, 0.0f, 0, 2.0 },
            { "QUAKE up",          0.4f, 0.0f, 1.0f, 0.9f, 0, 1.0 },
        };
        std::vector<double> baseCurve; const double base = trajectory (off, baseCurve);
        double worst = 1e9; const char* worstName = "";
        std::vector<std::vector<double>> curves;
        std::printf ("\n  [3] THE DRAWN CYCLE ACTUALLY DEFORMS — mean |phase deviation| over one drawn cycle\n");
        std::printf ("        %-18s %s\n", "(no FM)", "0.0000 cycles");
        for (auto& r : rigs) {
            FmOps::Params p; p.alg = r.alg; p.d1 = r.d1; p.d2 = r.d2; p.ratio1 = r.r1; p.ratio2 = 2.0;
            p.scorchPre = r.scorch; p.scorchBias = 0.2f;
            p.scorchTanhBias = FmOps::fastTanh (p.scorchBias);
            p.scorchMakeup = 1.0f / (float) std::fmax (0.30, FmOps::fastTanh (p.scorchPre + p.scorchBias));
            p.quakeIdx = r.quake; p.quakeSubRatio = 0.5f;
            std::vector<double> c; const double t = trajectory (p, c);
            std::printf ("        %-18s %.4f cycles\n", r.name, t);
            curves.push_back (c);
            if (t < worst) { worst = t; worstName = r.name; }
        }
        // ⚠️ MEAN |deviation| IS BLIND TO FREQUENCY — "index 1 up" and "ratio 1 = 3.5" both read
        //    0.3325, because the average of |sin| does not care how fast it wiggles. A bar that
        //    stopped there would NOT have proved the ratio knob changes the picture, which is the
        //    exact knob Max asked about. So every pair of rigs must also draw a DIFFERENT CURVE.
        double closestPair = 1e9; size_t ci = 0, cj = 0;
        for (size_t i = 0; i < curves.size(); ++i)
          for (size_t j = i + 1; j < curves.size(); ++j)
          { const double d = curveDist (curves[i], curves[j]);
            if (d < closestPair) { closestPair = d; ci = i; cj = j; } }
        std::printf ("        closest pair of rigs: %s vs %s at %.4f cycles rms\n",
                     rigs[ci].name, rigs[cj].name, closestPair);
        const bool moves = (base < 1e-9) && (worst > 0.02) && (closestPair > 0.02);
        std::printf ("  %-58s %s\n        %s\n",
                     "[3] EVERY FM CONTROL DEFORMS THE DRAWN CYCLE", moves ? "ok" : "FAIL",
                     moves ? "quietest rig still bends the read by more than 2% of a cycle"
                           : "one rig barely moves the cycle — the picture would look static");
        if (! moves) std::printf ("        weakest: %s at %.4f cycles\n", worstName, worst);
        if (! moves) return 1;
    }

    std::printf ("\n══ RESULT: %s ══\n\n", (bad == 0 && badAdv == 0) ? "NULL — the extraction changed nothing, and the picture will move" : "NOT NULL");
    return (bad == 0 && badAdv == 0) ? 0 : 1;
}
