// GeodeEngine offline proof (Pattern A — NOT in CMakeLists):
//   c++ -std=c++17 -O2 -Wall -Wextra -ISource Source/GeodeEngine_test.cpp -o /tmp/gd && /tmp/gd
#include "GeodeEngine.h"
#include <cstdio>
#include <cmath>
#include <vector>

using namespace tw;

static int failures = 0;
static void check (bool cond, const char* what)
{
    std::printf ("%s  %s\n", cond ? "  ok" : "FAIL", what);
    if (! cond) ++failures;
}

static bool finiteBlock (const float* a, int n)
{
    for (int i = 0; i < n; ++i) if (! std::isfinite (a[i])) return false;
    return true;
}
static float rms (const float* a, int n)
{
    double s = 0.0; for (int i = 0; i < n; ++i) s += (double) a[i] * a[i];
    return (float) std::sqrt (s / std::max (1, n));
}

int main()
{
    const double SR = 48000.0;

    // ── 1. synthesize a test sample: 220 Hz sawtooth + a little breath noise ──
    const int N = (int) (SR * 1.0);
    std::vector<float> samp ((size_t) N);
    std::uint32_t rng = 12345u;
    auto white = [&] { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return (float) ((int32_t) rng) * (1.f / 2147483648.f); };
    const double f0 = 220.0;
    for (int i = 0; i < N; ++i)
    {
        double t = (double) i / SR;
        double saw = 0.0;
        for (int h = 1; h <= 20; ++h) saw += std::sin (2.0 * M_PI * f0 * h * t) / h;
        samp[(size_t) i] = (float) (0.3 * saw) + 0.02f * white();
    }

    // ── 2. analyze ──
    GeodeFrameStore store;
    GeodeAnalyzer::analyzeSample (samp.data(), N, SR, store);
    check (store.valid, "sample store is valid");
    check (store.numFrames() > 4, "produced multiple frames");
    std::printf ("  ..f0 detected = %.1f Hz (expect ~220), frames = %d\n", store.f0, store.numFrames());
    check (store.f0 > 200.f && store.f0 < 240.f, "f0 detected near 220 Hz");
    int maxP = 0; for (auto& f : store.frames) maxP = std::max (maxP, f.nPartials);
    check (maxP >= 8, "extracted a healthy set of partials");

    // ── 3. resynthesize at MIDI note (A3 = 220) ──
    GeodeEngine eng; eng.prepare (SR);
    int budget = 4096; eng.setPartialBudget (&budget, 100000);
    eng.setFrameStore (&store);
    GeodeParams p; eng.setParams (p);
    eng.noteOn (220.0, 999u);

    std::vector<float> L (512, 0.f), R (512, 0.f);
    for (int blk = 0; blk < 8; ++blk)   // a few blocks to settle the read-head/noise
    {
        std::fill (L.begin(), L.end(), 0.f); std::fill (R.begin(), R.end(), 0.f);
        eng.renderBlockAdd (L.data(), R.data(), 512);
    }
    check (finiteBlock (L.data(), 512) && finiteBlock (R.data(), 512), "resynth output is finite (no NaN)");
    check (rms (L.data(), 512) > 1e-4f, "resynth output is audible (non-silent)");
    std::printf ("  ..resynth RMS L = %.4f\n", rms (L.data(), 512));

    // ── 4. sculpt sweep never NaNs or blows up ──
    bool sculptOk = true; float peak = 0.f;
    for (float v = 0.f; v <= 1.001f; v += 0.25f)
    {
        GeodeParams q;
        q.silt = v; q.fossil = v; q.creep = v; q.sieve = v; q.distill = v;
        q.haze = v; q.fracture = v; q.tilt = v; q.formant = v; q.cut = 1.f - v; q.quality = v;
        eng.setParams (q); eng.noteOn (330.0, 7u);
        for (int blk = 0; blk < 4; ++blk)
        {
            std::fill (L.begin(), L.end(), 0.f); std::fill (R.begin(), R.end(), 0.f);
            eng.renderBlockAdd (L.data(), R.data(), 512);
            if (! finiteBlock (L.data(), 512) || ! finiteBlock (R.data(), 512)) sculptOk = false;
            for (int i = 0; i < 512; ++i) peak = std::max (peak, std::fabs (L[(size_t) i]));
        }
    }
    check (sculptOk, "full sculpt sweep stays finite");
    check (peak < 8.f, "output stays bounded across sculpt (< 8.0)");
    std::printf ("  ..sculpt-sweep peak = %.3f\n", peak);

    // ── 5. wavetable door: harmonic partials (saw: ratio h, amp 1/h) ──
    const int WF = 16, WP = 32;
    std::vector<float> wr ((size_t) WF * WP, 0.f), wa ((size_t) WF * WP, 0.f);
    for (int f = 0; f < WF; ++f)
        for (int h = 1; h <= WP; ++h)
        { wr[(size_t) f * WP + (h - 1)] = (float) h; wa[(size_t) f * WP + (h - 1)] = 1.f / (float) h; }
    GeodeFrameStore ws;
    GeodeAnalyzer::buildFromWave (wr.data(), wa.data(), WF, WP, ws);
    check (ws.valid && ws.fromWave, "wavetable store built + flagged fromWave");
    GeodeEngine eng2; eng2.prepare (SR); eng2.setPartialBudget (&budget, 100000);
    eng2.setFrameStore (&ws);
    GeodeParams wp; eng2.setParams (wp); eng2.noteOn (110.0, 3u);
    std::fill (L.begin(), L.end(), 0.f); std::fill (R.begin(), R.end(), 0.f);
    for (int blk = 0; blk < 4; ++blk) { eng2.renderBlockAdd (L.data(), R.data(), 512); }
    check (finiteBlock (L.data(), 512), "wavetable resynth is finite");
    check (rms (L.data(), 512) > 1e-4f, "wavetable resynth is audible");

    // ── 6. no store → silent no-op, never crashes ──
    GeodeEngine eng3; eng3.prepare (SR);
    std::fill (L.begin(), L.end(), 0.f);
    eng3.renderBlockAdd (L.data(), R.data(), 512);
    check (rms (L.data(), 512) < 1e-9f, "no-store engine is a silent no-op");

    std::printf ("\n%s — %d failure(s)\n", failures == 0 ? "ALL GREEN" : "RED", failures);
    return failures == 0 ? 0 : 1;
}
