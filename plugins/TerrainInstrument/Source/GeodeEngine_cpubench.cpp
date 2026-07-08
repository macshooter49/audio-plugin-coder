// ════════════════════════════════════════════════════════════════════════════
//  geode_cpu_bench.cpp — CPU cost benchmark for the RESYNTH (Geode) oscillator.
//  Proves the CPU cliff Max hit: measures cost/partial-sample, then computes what
//  the shared partial budget (kGeodePartialBudget) actually costs in % of one core.
//
//    c++ -std=c++17 -O3 -ffast-math -ISource geode_cpu_bench.cpp -o /tmp/gcb && /tmp/gcb
// ════════════════════════════════════════════════════════════════════════════
#include "GeodeEngine.h"
#include <cstdio>
#include <vector>
#include <cmath>
#include <chrono>
#include <algorithm>
#include <functional>
using namespace tw;
using clk = std::chrono::high_resolution_clock;

// A rich harmonic "sample" spectrum with H partials (mimics a sample after analysis).
static GeodeFrameStore makeStore (int H)
{
    GeodeFrameStore s; s.frames.resize (8); s.naturalSec = 2.0f; s.valid = true; s.f0 = 110.f;
    for (auto& f : s.frames)
    {
        int c = 0;
        for (int n = 1; n <= H && c < geode::kMaxPartials; ++n)
        { f.ratio[(size_t) c] = (float) n; f.amp[(size_t) c] = 1.f / (float) n; ++c; }
        f.nPartials = c;
    }
    return s;
}

// Render `blocksToRun` blocks of `blk` samples through `nEng` engine instances that
// SHARE one partial budget of `budget`. Returns average wall-ns PER BLOCK (all engines).
static double benchInstance (int nEng, int budget, double quality, float stretch,
                             int blk, int blocksToRun, GeodeFrameStore& st, double sr,
                             int* outLivePartials, int unisonN = 1)
{
    std::vector<GeodeEngine> engs ((size_t) nEng);
    int liveBudget = 0;
    GeodeParams p; p.quality = (float) quality; p.stretch = stretch; p.scan = 0.5f;
    for (int i = 0; i < nEng; ++i)
    {
        engs[(size_t) i].prepare (sr);
        engs[(size_t) i].setFrameStore (&st);
        engs[(size_t) i].setPartialBudget (&liveBudget, budget);
        engs[(size_t) i].setUnisonScale (unisonN);   // constant-cost unison divisor
        engs[(size_t) i].setParams (p);
        // spread start positions so unison-like instances aren't identical
        GeodeParams pi = p; pi.start = (float) i / (float) std::max (1, nEng);
        engs[(size_t) i].setParams (pi);
        engs[(size_t) i].noteOn (261.63 * (1.0 + 0.001 * i), (std::uint32_t) (7 + i));
    }
    std::vector<float> L ((size_t) blk, 0.f), R ((size_t) blk, 0.f);

    // warm up a couple blocks (fill declick ramps to steady state)
    for (int w = 0; w < 4; ++w) { liveBudget = 0; for (auto& e : engs) e.renderBlockAdd (L.data(), R.data(), blk); }

    int sampledLive = 0;
    auto t0 = clk::now();
    for (int b = 0; b < blocksToRun; ++b)
    {
        liveBudget = 0;                          // per-block reset (mirrors processor)
        std::fill (L.begin(), L.end(), 0.f);
        std::fill (R.begin(), R.end(), 0.f);
        for (auto& e : engs) e.renderBlockAdd (L.data(), R.data(), blk);
        sampledLive = liveBudget;                // total partials actually rendered this block
    }
    auto t1 = clk::now();
    if (outLivePartials) *outLivePartials = sampledLive;
    double totalNs = std::chrono::duration<double, std::nano> (t1 - t0).count();
    return totalNs / (double) blocksToRun;       // ns per block for the whole instance
}

int main()
{
    const double sr = 48000.0;
    const int    blk = 128;                                   // typical DAW block
    const double blockBudgetNs = (double) blk / sr * 1e9;     // realtime ns available per block (one core)
    auto st = makeStore (96);                                 // worst-case rich sample: 96 partials/frame

    std::printf ("=== RESYNTH (Geode) CPU BENCHMARK ===\n");
    std::printf ("sr=%.0f  block=%d samples  realtime budget/block = %.1f us (100%% of one core)\n\n",
                 sr, blk, blockBudgetNs / 1000.0);

    // 1) Cost per partial-sample — single engine, no shared cap, max partials.
    {
        int live = 0;
        double nsBlk = benchInstance (/*nEng*/1, /*budget*/100000, /*quality*/1.0, /*stretch*/0.f,
                                      blk, 20000, st, sr, &live);
        double nsPerPS = nsBlk / ((double) live * blk);
        std::printf ("[cost] 1 voice, quality=1.0 -> %d live partials, %.2f us/block, %.3f ns per partial-sample\n\n",
                     live, nsBlk / 1000.0, nsPerPS);
    }

    // 2) THE CLIFF, RE-TESTED with the rs2 governor (budget 640 + constant-cost unison + 64 ceil).
    //    Each "engine" stands in for one unison/voice partial bank; unisonN drives setUnisonScale.
    const int BUD = 640;   // = kGeodePartialBudget (rs2)
    struct Row { const char* name; int nEng; int uni; double quality; float stretch; };
    Row rows[] = {
        { "1 note, no unison (q0.8)",        1,  1, 0.80, 0.0f },
        { "1 note x8 unison (q0.8)",         8,  8, 0.80, 0.0f },
        { "1 note x16 unison (q0.8)",       16, 16, 0.80, 0.0f },
        { "4 notes x16 unison (q0.8)",      64, 16, 0.80, 0.0f },
        { "STRETCH pad: 8 notes x16 uni",  128, 16, 0.80, 0.98f },
        { "STRETCH pad: 16 notes x16 uni", 256, 16, 0.80, 0.98f },
        { "worst: 32 notes x16, q1.0",     512, 16, 1.00, 0.98f },
    };
    std::printf ("%-32s  %6s  %8s  %10s  %8s\n", "scenario (rs2 governor)", "live", "us/blk", "%1core", "verdict");
    std::printf ("--------------------------------------------------------------------------------\n");
    for (auto& r : rows)
    {
        int live = 0;
        double nsBlk = benchInstance (r.nEng, BUD, r.quality, r.stretch, blk, 4000, st, sr, &live, r.uni);
        double pct = nsBlk / blockBudgetNs * 100.0;
        const char* verdict = pct < 15 ? "OK" : (pct < 35 ? "heavy" : (pct < 80 ? "DANGER" : "PEGGED"));
        std::printf ("%-32s  %6d  %8.1f  %9.1f%%  %8s\n", r.name, live, nsBlk / 1000.0, pct, verdict);
    }

    // 3) PER-PARAMETER cost sweep — every knob at a saturating setting, 16-voice instance,
    //    to prove no single parameter hides a second cliff (FORMANT is O(nP^2) with exp()).
    std::printf ("\n=== PER-PARAMETER cost (16 banks, budget saturating, worst case each) ===\n");
    std::printf ("%-24s  %8s  %8s\n", "param engaged", "us/blk", "%1core");
    std::printf ("------------------------------------------------\n");
    auto benchParam = [&] (const char* name, std::function<void(GeodeParams&)> setup)
    {
        std::vector<GeodeEngine> engs (16);
        int liveBudget = 0;
        GeodeParams p; p.quality = 1.0f; p.scan = 0.5f; setup (p);
        for (int i = 0; i < 16; ++i)
        { auto& e = engs[(size_t) i]; e.prepare (sr); e.setFrameStore (&st);
          e.setPartialBudget (&liveBudget, 640); e.setUnisonScale (16); GeodeParams pi = p; pi.start = i / 16.f;
          e.setParams (pi); e.noteOn (261.63 * (1.0 + 0.001 * i), (std::uint32_t) (7 + i)); }
        std::vector<float> L ((size_t) blk, 0.f), R ((size_t) blk, 0.f);
        for (int w = 0; w < 4; ++w) { liveBudget = 0; for (auto& e : engs) e.renderBlockAdd (L.data(), R.data(), blk); }
        auto t0 = clk::now();
        for (int b = 0; b < 4000; ++b)
        { liveBudget = 0; std::fill (L.begin(), L.end(), 0.f); std::fill (R.begin(), R.end(), 0.f);
          for (auto& e : engs) { e.renderBlockAdd (L.data(), R.data(), blk); e.postProcess (L.data(), R.data(), blk); } }
        auto t1 = clk::now();
        double nsBlk = std::chrono::duration<double, std::nano> (t1 - t0).count() / 4000.0;
        std::printf ("%-24s  %8.1f  %8.1f%%\n", name, nsBlk / 1000.0, nsBlk / blockBudgetNs * 100.0);
    };
    benchParam ("neutral (baseline)", [] (GeodeParams&){});
    benchParam ("FORMANT=1.0 (O(n^2))", [] (GeodeParams& p){ p.formant = 1.0f; });
    benchParam ("TILT=0.9 (pow/partial)", [] (GeodeParams& p){ p.tilt = 0.9f; });
    benchParam ("SHAPE=1.0 saw",         [] (GeodeParams& p){ p.shape = 1.0f; p.shapeTarget = 2; });
    benchParam ("CUT=LP 0.3",            [] (GeodeParams& p){ p.cut = 0.3f; });
    benchParam ("SIEVE=0.5",             [] (GeodeParams& p){ p.sieve = 0.5f; });
    benchParam ("DRIVE=1.0 (postproc)",  [] (GeodeParams& p){ p.drive = 1.0f; });
    benchParam ("CRUSH=0.9 (postproc)",  [] (GeodeParams& p){ p.crush = 0.9f; });
    benchParam ("ALL engaged",           [] (GeodeParams& p){ p.formant=1.f; p.tilt=0.9f; p.shape=1.f; p.cut=0.4f; p.sieve=0.3f; p.drive=0.7f; p.crush=0.5f; });

    std::printf ("\nNote: %% is of ONE core, for the Geode oscillator ALONE (no filters/FX/other engines).\n");
    std::printf ("Multiple plugin instances multiply this. Target: worst case well under ~25%%.\n");
    return 0;
}
