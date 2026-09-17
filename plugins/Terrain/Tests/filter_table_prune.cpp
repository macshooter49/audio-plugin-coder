// ══════════════════════════════════════════════════════════════════════════════════════════════
//  filter_table_prune.cpp — tp25 · PRUNING THE FLAT BANDS MUST NOT CHANGE THE SOUND.
//
//    clang++ -std=c++17 -O2 -I../../_tools/JUCE/modules -I Source Tests/filter_table_prune.cpp \
//            -o /tmp/ftp && /tmp/ftp                      (from plugins/Terrain)
//
//  WHY.  TableBank is PER VOICE and PER CHANNEL, so its 32-biquad cascade is 64 biquads per
//  sounding voice no matter what the table says. tp25 makes process() walk only the bands whose
//  gain is outside +/-kSkipDb, which is a real CPU saving precisely because a mean-centred table
//  curve leaves many bands near flat — and at RESONANCE 0 leaves ALL of them flat.
//
//  But "a 0.2 dB bell is inaudible" is a claim, and 32 of them in series is a different claim
//  again: small errors CASCADE. So this measures the shipped TableBank both ways — the real
//  arm()/process() against the same bank with every band forced live — by running an impulse
//  through each and comparing the magnitude spectra. Nothing here models a biquad; it runs the
//  one that ships.
// ══════════════════════════════════════════════════════════════════════════════════════════════
#include <juce_audio_basics/juce_audio_basics.h>
#include "../Source/TerrainFilters.h"
#include <cstdio>
#include <cmath>
#include <vector>
#include <memory>

static int g_checks = 0, g_fail = 0;
static void chk (bool ok, const char* what, double detail = 0.0)
{ ++g_checks; if (! ok) { ++g_fail; std::printf ("  FAIL: %s  (%.4f)\n", what, detail); }
  else std::printf ("  PASS: %s  (%.4f)\n", what, detail); }

using Curve = tw::FilterTableSource::Curve;
using Grid  = tw::HarmTableSource::Grid;
static constexpr int B = tw::FilterTableSource::kBands;
static constexpr double FS = 48000.0;
static constexpr int N = 4096;

/** A grid whose frames carry a harmonic series f(h) — the same shape generator the curve test uses. */
static std::unique_ptr<Grid> makeGrid (float (*f) (int h))
{
    auto g = std::make_unique<Grid>();
    for (int fr = 0; fr < tw::FilterTableSource::kFrames; ++fr)
    {
        g->n[fr] = tw::HarmTableSource::kMaxN;
        for (int h = 1; h <= g->n[fr]; ++h) { g->amp[fr][h - 1] = f (h); g->phase[fr][h - 1] = 0.0f; }
    }
    return g;
}

/** Arm a bank from a curve exactly as TerrainFilters does, and report how many bands survived. */
static int armBank (tw::filters::TableBank& bank, const Curve& c, float pos01, float res, double cutHz, bool forceAll)
{
    constexpr float kTblQ = 4.9f, kTblMaxDb = 24.0f;
    float db[B] = {};
    tw::FilterTableSource::blend (c, pos01, res * tw::FilterTableSource::kDepthDb, db);   /* tp27 — the shipped depth */
    const float* ratio = tw::FilterTableSource::bandRatios();
    const float fLo = 20.0f, fHi = 0.45f * (float) FS;
    float g[B] = {};
    for (int k = 0; k < B; ++k)
    {
        const float gg = juce::jlimit (-kTblMaxDb, kTblMaxDb, db[k]);
        const float fk = (float) cutHz * ratio[k];
        g[k] = (fk >= fLo && fk <= fHi) ? gg : 0.0f;
        bank.band[k].setBell (juce::jlimit (fLo, fHi, fk), g[k], kTblQ, FS);
    }
    bank.arm (g);
    if (forceAll) { for (int k = 0; k < B; ++k) bank.live[k] = k; bank.nLive = B; }
    return bank.nLive;
}

static void impulse (tw::filters::TableBank& bank, std::vector<float>& out)
{
    bank.reset();
    out.assign (N, 0.0f);
    for (int i = 0; i < N; ++i) out[(size_t) i] = bank.process (i == 0 ? 1.0f : 0.0f);
}

/** max |dB| difference between two impulse responses, measured at log-spaced probe frequencies.
    A Goertzel-style direct evaluation rather than an FFT: it needs no juce_dsp to link, it puts the
    probes exactly where hearing is (log spacing), and it cannot leak between bins. Masked to where
    the reference actually has energy — an empty bin's ratio is noise, not a difference. */
static double specMaxDiff (const std::vector<float>& a, const std::vector<float>& b)
{
    const int P = 400;
    double peak = 0.0;
    std::vector<double> A ((size_t) P), Bm ((size_t) P);
    for (int p = 0; p < P; ++p)
    {
        const double f = 20.0 * std::pow (1000.0, (double) p / (double) (P - 1));   // 20 Hz .. 20 kHz
        const double w = 2.0 * M_PI * f / FS;
        double ar = 0, ai = 0, br = 0, bi = 0;
        for (int n = 0; n < N; ++n)
        {
            const double c = std::cos (w * n), s = std::sin (w * n);
            ar += a[(size_t) n] * c; ai -= a[(size_t) n] * s;
            br += b[(size_t) n] * c; bi -= b[(size_t) n] * s;
        }
        A[(size_t) p]  = std::sqrt (ar * ar + ai * ai);
        Bm[(size_t) p] = std::sqrt (br * br + bi * bi);
        peak = std::max (peak, A[(size_t) p]);
    }
    if (peak <= 0.0) return 0.0;
    const double floorMag = peak * 1.0e-3;           // -60 dB of the reference peak
    double worst = 0.0;
    for (int p = 0; p < P; ++p)
        if (A[(size_t) p] >= floorMag)
            worst = std::max (worst, std::fabs (20.0 * std::log10 ((Bm[(size_t) p] + 1e-20) / (A[(size_t) p] + 1e-20))));
    return worst;
}

int main()
{
    std::printf ("— tp25 TABLE BANK PRUNING —\n");

    struct Shape { const char* name; float (*f) (int); };
    Shape shapes[] = {
        { "saw",    [] (int h) { return 1.0f / (float) h; } },
        { "square", [] (int h) { return (h % 2) ? 1.0f / (float) h : 0.0f; } },
        { "formant",[] (int h) { const float d = (float) h - 9.0f; return std::exp (-d * d / 26.0f) + 0.12f / (float) h; } },
    };

    double worstAll = 0.0; long longFull = 0, longLive = 0; int cells = 0;
    for (auto& sh : shapes)
    {
        auto grid = makeGrid (sh.f);
        auto c = std::make_unique<Curve>();
        tw::FilterTableSource::bake (*grid, *c);
        for (float pos : { 0.0f, 0.37f, 1.0f })
            for (float res : { 0.25f, 0.5f, 1.0f })
                for (double cut : { 120.0, 700.0, 4000.0 })
                {
                    tw::filters::TableBank full, pruned;
                    const int nFull = armBank (full,   *c, pos, res, cut, true);
                    const int nLive = armBank (pruned, *c, pos, res, cut, false);
                    std::vector<float> a, b;
                    impulse (full, a); impulse (pruned, b);
                    const double d = specMaxDiff (a, b);
                    worstAll = std::max (worstAll, d);
                    longFull += nFull; longLive += nLive; ++cells;
                }
    }
    std::printf ("  %d cells · bands run: %ld of %ld (%.0f%% of the cascade)\n",
                 cells, longLive, longFull, 100.0 * (double) longLive / (double) longFull);
    chk (worstAll < 0.5, "pruned bank matches the full bank within 0.5 dB, everywhere", worstAll);
    chk (longLive < longFull, "and it really does skip work", (double) (longFull - longLive));

    // RESONANCE 0 — the curve is flat, so the whole bank must vanish, not merely get cheaper.
    {
        auto grid = makeGrid ([] (int h) { return 1.0f / (float) h; });
        auto c = std::make_unique<Curve>();
        tw::FilterTableSource::bake (*grid, *c);
        tw::filters::TableBank z;
        const int n = armBank (z, *c, 0.4f, 0.0f, 900.0, false);
        chk (n == 0, "resonance 0 runs ZERO biquads (a flat filter costs nothing)", (double) n);
        std::vector<float> imp; impulse (z, imp);
        chk (std::fabs (imp[0] - 1.0f) < 1e-6f, "and passes the signal through untouched", (double) imp[0]);
    }

    std::printf ("\n%d checks, %d failed\n", g_checks, g_fail);
    if (! g_fail) std::printf ("ALL %d CHECKS PASSED\n", g_checks);
    return g_fail ? 1 : 0;
}

// JUCE expects the build system to supply these; a standalone test compile has no build system.
namespace juce { extern const char* const juce_compilationDate = __DATE__; extern const char* const juce_compilationTime = __TIME__; }
