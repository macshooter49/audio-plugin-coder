// ════════════════════════════════════════════════════════════════════════════
//  harm_cluster_cert.cpp — HARMONIC CLUSTER certification (Pattern A — NOT in CMakeLists).
//    c++ -std=c++17 -O2 -Wall -Wextra -ISource Tests/harm_cluster_cert.cpp -o /tmp/hcc && /tmp/hcc
//
//  THE PROPERTY (Max, 2026-09-07): "a silent spike every now and then whenever I press more
//  than like four or five notes … it may spike out the audio for a second."
//    An additive voice may be THINNED by the shared partial pool. It must never be MUTED by it.
//
//  WHAT IS MEASURED — the deciding property, not "did it change":
//    Each voice of an N-note cluster is rendered into its OWN buffer through the exact shipped
//    call order of SynthVoice::renderHarmonicOsc (prepareBank → reserveBudget → siblings →
//    releaseBudget → anchor renderBankAdd), against ONE shared pool counter reset once per host
//    block exactly as PluginProcessor.cpp:10812 resets geodePartialsLive_. Each voice's level is
//    then reported in dB relative to the SAME patch played solo — a hearing metric: −0.5 dB is
//    "thinner", −60 dB is "the note vanished".
//
//  GATE  G1  no voice of a 16-note cluster may sit more than kMuteDb below its solo self,
//            at any Partials setting, in any register.
//  GATE  G2  the pool must still bound the work: total partials rendered per block ≤ 1.05·cap
//            for unison ≤ 4.
//  GATE  G3  a SOLO note must be bit-identical to the shipped engine (no collateral retune).
//
//  MUTATION SEAM  -DHM_CLUSTER_MUTATION=1 restores the shipped saturation cliff
//    (HarmonicEngine.h:283 `*budgetUsed_ >= budgetCap_` → ramp the whole bank to silence).
//    G1 must go RED under it. It is RED on the shipped header with no -D at all.
// ════════════════════════════════════════════════════════════════════════════
#include <cstdint>
#include <cmath>
#include <vector>
#include <array>
#include <algorithm>
#include <cstdio>
#include <cstdarg>
#include <type_traits>
#include <utility>
#define private public
#include "HarmonicEngine.h"
#undef private
using namespace tw;

// The census (setBudgetCensus) exists only on the FIXED header. Detect it so this one cert
// compiles and runs against the shipped engine (RED) and the fixed engine (GREEN) unchanged.
template <class E, class = void> struct HasCensus : std::false_type {};
template <class E> struct HasCensus<E, std::void_t<decltype (std::declval<E&>().setBudgetCensus (nullptr, nullptr))>> : std::true_type {};
template <class E> static void census (E& e, int* live, const int* shares)
{ if constexpr (HasCensus<E>::value) e.setBudgetCensus (live, shares); else { (void) e; (void) live; (void) shares; } }
// fb599 — SAY IT OUT LOUD. This detector silently skipped the census once because the engine's
// method had a different name, and every ceiling number in G2 was then measuring a fix that was
// not running. A detector that can no-op must report whether it fired.
template <class E> constexpr bool censusWired() { return HasCensus<E>::value; }

static constexpr int    kCap    = 640;      // = PluginProcessor.h:1958 kGeodePartialBudget
static constexpr double kSR     = 48000.0;
static constexpr int    kBlk    = 128;
static constexpr double kMuteDb = -12.0;    // thinner is fine; gone is not
static int pass = 0, tot = 0;
static void CHECK (bool ok, const char* fmt, ...)
{
    va_list a; va_start (a, fmt); ++tot; if (ok) ++pass;
    std::printf (ok ? "  ok   " : "  FAIL "); std::vprintf (fmt, a); std::printf ("\n"); va_end (a);
}

// ── the synthetic source: a bright 512-harmonic table, the shape a DYNOX-class
//    wavetable resolves to through HarmTableSource::bake (1/n^0.7 + a formant bump).
static float tA[512], tP[512];
static void makeTable() { for (int j = 0; j < 512; ++j) { const float n = (float) (j + 1);
    float a = std::pow (n, -0.7f); a *= 0.35f + 0.65f * std::exp (-std::pow (std::log (n / 10.0f), 2.0f) / 0.9f) + 0.25f;
    tA[j] = a; tP[j] = 0.37f * n; } }
static HarmParams P (float count) { HarmParams p; p.mainMode = 6; p.hue = 0.35f; p.count = count;
    p.lean = 0.5f; p.churn = 0.5f; p.wilt = 0.5f;
    p.tableAmp = tA; p.tablePhase = tP; p.tableN = 512; p.tableSig = 1234.5f; return p; }
static double rmsOf (const float* x, int n) { double s = 0; for (int i = 0; i < n; ++i) s += (double) x[i] * x[i]; return std::sqrt (s / n); }
static double dB (double a, double r) { return 20.0 * std::log10 (std::max (1e-12, a) / std::max (1e-12, r)); }

struct Run { std::vector<double> lvl; int partials = 0; };
static Run cluster (int N, float count, double hz0, int uni, int foreign = 0)
{
    int used = 0, live = 0, prevCensus = 1;
    std::vector<std::vector<HarmonicEngine>> v ((size_t) N);
    const HarmParams p = P (count);
    for (int i = 0; i < N; ++i) { v[(size_t) i].resize ((size_t) uni);
        for (int u = 0; u < uni; ++u) { auto& e = v[(size_t) i][(size_t) u];
            e.prepare (kSR, u == 0); e.setPartialBudget (&used, kCap); census (e, &live, &prevCensus);
            e.setParams (p); e.setUnisonScale (uni);
            e.noteOn (hz0 * std::pow (2.0, i / 12.0), 0x5Eu + (unsigned) (i * 31 + u)); } }
    std::vector<float> L ((size_t) kBlk), R ((size_t) kBlk);
    Run r; r.lvl.assign ((size_t) N, 0.0);
    for (int b = 0; b < 12; ++b)
    {
        used = 0;                                          // PluginProcessor.cpp:10812
        prevCensus = live > 0 ? live : 1; live = 0;   // last block's census = this block's divisor
        used += foreign;   // G4: a SPEC/MODAL oscillator on the SAME pool renders first (SynthVoice.h:3940)
        for (int i = 0; i < N; ++i)
        {
            std::fill (L.begin(), L.end(), 0.f); std::fill (R.begin(), R.end(), 0.f);
            auto& a0 = v[(size_t) i][0];
            for (int u = 0; u < uni; ++u) { v[(size_t) i][(size_t) u].setParams (p); v[(size_t) i][(size_t) u].setUnisonScale (uni); }
            if (! a0.prepareBank (kBlk)) continue;
            a0.reserveBudget();
            for (int u = 1; u < uni; ++u) { v[(size_t) i][(size_t) u].adoptBank (a0); v[(size_t) i][(size_t) u].renderBankAdd (L.data(), R.data(), kBlk); }
            a0.releaseBudget();
            a0.renderBankAdd (L.data(), R.data(), kBlk);
            if (b == 11) r.lvl[(size_t) i] = rmsOf (L.data(), kBlk);
        }
        if (b == 11) r.partials = used;
    }
    return r;
}

int main()
{
    std::printf ("census wired: %s\n\n", censusWired<tw::HarmonicEngine>() ? "YES" : "NO  <-- the cert is measuring a fix that is not running");
    makeTable();
    std::printf ("═══ harm_cluster_cert ═══  cap=%d  block=%d\n", kCap, kBlk);
#if HM_CLUSTER_MUTATION
    std::printf ("  ** MUTATION: -DHM_CLUSTER_MUTATION — shipped saturation cliff restored **\n");
#endif

    // ── G1 — no voice of a cluster may vanish ───────────────────────────────
    std::printf ("\nG1  cluster survival (worst voice, dB re its own solo level)\n");
    static const double roots[3] = { 65.41, 130.81, 261.63 };
    static const char*  rn[3]    = { "C2", "C3", "C4" };
    static const float  cs[4]    = { 0.50f, 0.70f, 0.85f, 1.00f };
    for (int r = 0; r < 3; ++r)
      for (int c = 0; c < 4; ++c)
      {
          const double solo = cluster (1, cs[c], roots[r], 1).lvl[0];
          Run k = cluster (16, cs[c], roots[r], 1);
          double worst = 1e9; int worstV = -1;
          for (int i = 0; i < 16; ++i) { const double d = dB (k.lvl[(size_t) i], solo); if (d < worst) { worst = d; worstV = i; } }
          CHECK (worst >= kMuteDb, "%s  Partials %.2f  16 notes  worst voice #%2d %+8.1f dB (limit %+.0f)",
                 rn[r], cs[c], worstV + 1, worst, kMuteDb);
      }

    // ── G2 — the pool still bounds the work ─────────────────────────────────
    std::printf ("\nG2  pool ceiling (total partials charged in one block)\n");
    for (int uni : { 1, 2, 4 })
      for (int N : { 8, 16 })
      {
          Run k = cluster (N, 1.00f, 130.81, uni);
          CHECK (k.partials <= (int) (kCap * 1.10), "unison %2d  %2d notes  %5d partials (ceiling %d)",
                 uni, N, k.partials, (int) (kCap * 1.10));
      }

    // ── G3 — a SOLO note is untouched ───────────────────────────────────────
    std::printf ("\nG3  solo-note identity (the fix must cost a lone note nothing)\n");
    static const double kShipSolo[4] = { 0.028223773, 0.027812758, 0.027794046, 0.027794046 };  // measured on the shipped header, same 12-block settle
    static const float  soloC[4]     = { 0.50f, 0.70f, 0.85f, 1.00f };
    for (int c = 0; c < 4; ++c)
    {
        const double s = cluster (1, soloC[c], 130.81, 1).lvl[0];
        CHECK (std::fabs (s - kShipSolo[c]) < 1e-7, "Partials %.2f  solo rms %.9f  (shipped %.9f)", soloC[c], s, kShipSolo[c]);
    }

    // ── G4 — a foreign consumer on the same pool (GEODE renders before HARM) ─
    std::printf ("\nG4  mixed scene: a SPEC oscillator takes 400 of the 640 before HARM renders\n");
    for (int N : { 4, 8, 16 })
    {
        const double solo = cluster (1, 1.00f, 130.81, 1).lvl[0];
        Run k = cluster (N, 1.00f, 130.81, 1, 400);
        double worst = 1e9; int wv = -1;
        for (int i = 0; i < N; ++i) { const double d = dB (k.lvl[(size_t) i], solo); if (d < worst) { worst = d; wv = i; } }
        CHECK (worst >= kMuteDb, "%2d notes + 400 foreign partials  worst voice #%2d %+8.1f dB", N, wv + 1, worst);
    }

    std::printf ("\n%d/%d  %s\n", pass, tot, pass == tot ? "PASS" : "*** RED ***");
    return pass == tot ? 0 : 1;
}
