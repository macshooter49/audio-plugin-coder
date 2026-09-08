// ════════════════════════════════════════════════════════════════════════════
//  HarmonicEngine offline proof suite (Pattern A — NOT in CMakeLists)
//    c++ -std=c++17 -O2 -Wall -Wextra -ISource Source/HarmonicEngine_test.cpp -o /tmp/hm && /tmp/hm
//  Proves, without a DAW: every mode/knob extreme keeps the played pitch, the
//  renorm keeps loudness steady, budget thinning works, the unison adopt path
//  matches, param jumps don't click, and the whole thing stays inside CPU.
// ════════════════════════════════════════════════════════════════════════════
#include "HarmonicEngine.h"
#include <cstdio>
#include <cstring>
#include <chrono>

using tw::HarmonicEngine;
using tw::HarmParams;

static int pass = 0, tot = 0;

// autocorrelation fundamental estimate — NORMALIZED scores, octave guard restricted to
// integer divisions of the best lag (a broad near-sine peak must not bias the pick), and
// parabolic refinement of the winning lag.
static double estFund (const float* x, int n, double sr)
{
    const int lagMin = (int) (sr / 1200.0);
    const int lagMax = std::min ((int) (sr / 40.0), n / 2 - 2);
    auto score = [&] (int lag) -> double
    {
        double s = 0.0, e1 = 0.0, e2 = 0.0;
        for (int i = 0; i < n / 2; ++i)
        { s += (double) x[i] * x[i + lag]; e1 += (double) x[i] * x[i]; e2 += (double) x[i + lag] * x[i + lag]; }
        return s / std::sqrt (std::max (1e-12, e1 * e2));
    };
    int bestLag = lagMin; double best = -2.0;
    for (int lag = lagMin; lag < lagMax; ++lag)
    { const double s = score (lag); if (s > best) { best = s; bestLag = lag; } }
    for (int k = 4; k >= 2; --k)
    {
        const int cand = (int) std::lround ((double) bestLag / k);
        if (cand >= lagMin && cand < lagMax && score (cand) > best * 0.93)
        { bestLag = cand; best = score (cand); break; }
    }
    const double s0 = score (bestLag - 1), s1 = score (bestLag), s2 = score (bestLag + 1);
    double d = 0.5 * (s0 - s2) / (s0 - 2.0 * s1 + s2 + 1e-12);
    if (std::fabs (d) > 1.0) d = 0.0;
    return sr / ((double) bestLag + d);
}

static double rmsOf (const float* x, int n)
{
    double s = 0.0; for (int i = 0; i < n; ++i) s += (double) x[i] * x[i];
    return std::sqrt (s / (double) n);
}

// render 0.45 s mono-summed; returns buffer via out (sized), fund estimated on back half
static double renderFund (const HarmParams& p, double playHz, double sr,
                          double* outRms = nullptr, HarmParams* p2 = nullptr)
{
    HarmonicEngine e; e.prepare (sr, true);
    e.setParams (p); e.noteOn (playHz, 1234567u);
    const int N = (int) (sr * 0.45);
    static std::vector<float> L, R; L.assign ((size_t) N, 0.f); R.assign ((size_t) N, 0.f);
    for (int off = 0; off < N; off += 256)
    {
        const int m = std::min (256, N - off);
        if (p2 != nullptr && off > N / 2) e.setParams (*p2);   // mid-note param jump (declick probe)
        else e.setParams (p);
        e.renderBlockAdd (&L[(size_t) off], &R[(size_t) off], m);
        e.postProcess   (&L[(size_t) off], &R[(size_t) off], m);   // full chain incl. FORGE
    }
    static std::vector<float> mono; mono.assign ((size_t) N, 0.f);
    bool finite = true;
    for (int i = 0; i < N; ++i) { mono[(size_t) i] = 0.5f * (L[(size_t) i] + R[(size_t) i]); if (! std::isfinite (mono[(size_t) i])) finite = false; }
    if (! finite) return -1.0;
    if (outRms != nullptr) *outRms = rmsOf (&mono[(size_t) (N / 3)], N - N / 3);
    return estFund (&mono[(size_t) (N / 2)], N / 2, sr);
}

// Pitch-CLASS check: the octave may legitimately shift (16' drawbars, sub ghosts, vowel
// masks parking a formant on harmonic 2) but the pitch class must never break.
static void checkFund (const char* name, double fund, double want, double tolPct)
{
    bool ok = false;
    for (double m : { 0.5, 1.0, 2.0 })
        if (fund > 0.0 && std::fabs (fund - want * m) / (want * m) * 100.0 < tolPct) ok = true;
    std::printf ("[%-22s] fund=%7.1fHz  %s\n", name, fund, ok ? "PASS" : "FAIL");
    pass += ok; ++tot;
}

int main()
{
    const double sr = 48000.0, f0 = 261.6256;
    std::printf ("══ HarmonicEngine offline proof ══\n");

    static const char* MAIN[6]   = { "Blade", "Neon", "Console", "Chant", "Bronze", "Hornet" };
    static const char* SCULPT[6] = { "Keel", "Splay", "Cull", "Tide", "Terrace", "Clang" };

    // ── 1. every MAIN mode × hue extremes keeps the key ──
    for (int m = 0; m < 6; ++m)
        for (float k : { 0.25f, 0.9f })
        {
            HarmParams p; p.mainMode = m; p.hue = k;
            char nm[48]; std::snprintf (nm, sizeof (nm), "%s hue=%.2f", MAIN[m], (double) k);
            if (m == 4 && k > 0.5f)
            {   // Bronze at high hue is a GONG by design — autocorrelation pitch is meaningless;
                // the guarantee is the ANCHORED ROOT (ratio[1] == 1) + finite audible output.
                HarmonicEngine e; e.prepare (sr, true); e.setParams (p); e.noteOn (f0, 8u);
                std::vector<float> L (2048, 0.f), R (2048, 0.f);
                e.renderBlockAdd (L.data(), R.data(), 2048);
                const bool okRoot = e.ratio1ForTesting() > 0.97f && e.ratio1ForTesting() < 1.03f;
                bool fin = true; double s = 0.0;
                for (int i = 0; i < 2048; ++i) { if (! std::isfinite (L[(size_t) i])) fin = false; s += std::fabs (L[(size_t) i]); }
                const bool ok = okRoot && fin && s > 1.0;
                std::printf ("[%-22s] root=%.3f  %s\n", nm, (double) e.ratio1ForTesting(), ok ? "PASS" : "FAIL");
                pass += ok; ++tot;
            }
            else
                checkFund (nm, renderFund (p, f0, sr), f0, 4.0);
        }

    // ── 2. every SCULPT mode × carve extremes keeps the key (Blade base) ──
    for (int s = 0; s < 6; ++s)
        for (float k : { 0.4f, 0.95f })
        {
            HarmParams p; p.sculptMode = s; p.carve = k;
            char nm[48]; std::snprintf (nm, sizeof (nm), "%s carve=%.2f", SCULPT[s], (double) k);
            checkFund (nm, renderFund (p, f0, sr), f0, (s == 1 && k > 0.5f) ? 10.0 : 5.0);
        }

    // ── 3. voice-knob extremes stay pitched + finite ──
    { HarmParams p; p.grit = 1.f;  checkFund ("grit=1.0", renderFund (p, f0, sr), f0, 5.0); }
    { HarmParams p; p.braid = 1.f; checkFund ("braid=1.0", renderFund (p, f0, sr), f0, 5.0); }
    { HarmParams p; p.fan = 1.f;   checkFund ("fan=1.0 orbit", renderFund (p, f0, sr), f0, 4.0); }
    { HarmParams p; p.forge = 0.5f; checkFund ("forge=0.5", renderFund (p, f0, sr), f0, 5.0); }
    { HarmParams p; p.shine = 1.f; p.root = 1.f; checkFund ("shine+root=1", renderFund (p, f0, sr), f0, 5.0); }
    { HarmParams p; p.wilt = 0.f;  checkFund ("wilt=pluck", renderFund (p, f0, sr), f0, 5.0); }
    { HarmParams p; p.wilt = 1.f;  checkFund ("wilt=bloom", renderFund (p, f0, sr), f0, 5.0); }
    { HarmParams p; p.lean = 1.f;  checkFund ("lean=bright", renderFund (p, f0, sr), f0, 5.0); }

    // ── 4. RMS steadiness: the PARTIALS knob must not pump loudness ──
    {
        double r0 = 0, r1 = 0, r2 = 0;
        HarmParams p; p.count = 0.0f; renderFund (p, f0, sr, &r0);
        p.count = 0.5f;               renderFund (p, f0, sr, &r1);
        p.count = 1.0f;               renderFund (p, f0, sr, &r2);
        const double spanDb = 20.0 * std::log10 (std::max ({ r0, r1, r2 }) / std::max (1e-9, std::min ({ r0, r1, r2 })));
        const bool ok = spanDb < 3.0;
        std::printf ("[%-22s] rms %.4f/%.4f/%.4f span=%.1fdB  %s\n", "count renorm", r0, r1, r2, spanDb, ok ? "PASS" : "FAIL");
        pass += ok; ++tot;
    }
    // carve renorm per sculpt mode
    for (int s = 0; s < 6; ++s)
    {
        double rA = 0, rB = 0;
        HarmParams p; p.sculptMode = s; p.carve = 0.f; renderFund (p, f0, sr, &rA);
        p.carve = 0.9f;                                renderFund (p, f0, sr, &rB);
        const double d = 20.0 * std::log10 (std::max (rA, rB) / std::max (1e-9, std::min (rA, rB)));
        const bool ok = d < 4.5;
        char nm[48]; std::snprintf (nm, sizeof (nm), "%s renorm", SCULPT[s]);
        std::printf ("[%-22s] delta=%.1fdB  %s\n", nm, d, ok ? "PASS" : "FAIL");
        pass += ok; ++tot;
    }

    // ── 5. declick proxy: hard hue jump mid-note must not spike the first difference ──
    {
        HarmParams pA; pA.mainMode = 0; pA.hue = 0.0f;
        HarmParams pB = pA; pB.hue = 1.0f; pB.lean = 0.9f;
        HarmonicEngine e; e.prepare (sr, true); e.setParams (pA); e.noteOn (f0, 99u);
        const int N = (int) (sr * 0.4);
        std::vector<float> L ((size_t) N, 0.f), Rr ((size_t) N, 0.f);
        std::vector<double> diffRms;
        for (int off = 0; off < N; off += 256)
        {
            const int m = std::min (256, N - off);
            e.setParams (off > N / 2 ? pB : pA);
            e.renderBlockAdd (&L[(size_t) off], &Rr[(size_t) off], m);
            double s = 0.0;
            for (int i = std::max (1, off); i < off + m; ++i)
            { const double d = (double) L[(size_t) i] - L[(size_t) i - 1]; s += d * d; }
            diffRms.push_back (std::sqrt (s / m));
        }
        std::vector<double> sorted = diffRms; std::sort (sorted.begin(), sorted.end());
        const double med = sorted[sorted.size() / 2];
        double worst = 0.0; for (double d : diffRms) worst = std::max (worst, d);
        const bool ok = worst < med * 4.0 + 1e-9;
        std::printf ("[%-22s] med=%.4f worst=%.4f  %s\n", "declick hue-jump", med, worst, ok ? "PASS" : "FAIL");
        pass += ok; ++tot;
    }

    // ── 6. constant-cost unison: sibling adopts the anchor's bank, pitch intact ──
    {
        HarmParams p; p.mainMode = 1; p.hue = 0.4f;
        HarmonicEngine anchor, sib;
        anchor.prepare (sr, true); sib.prepare (sr, false);
        anchor.setParams (p);
        anchor.noteOn (f0, 42u); sib.noteOn (f0, 43u);
        const int N = (int) (sr * 0.4);
        std::vector<float> L ((size_t) N, 0.f), R ((size_t) N, 0.f);
        for (int off = 0; off < N; off += 256)
        {
            const int m = std::min (256, N - off);
            anchor.setParams (p); anchor.prepareBank (m);
            sib.adoptBank (anchor); sib.renderBankAdd (&L[(size_t) off], &R[(size_t) off], m);
            anchor.renderBankAdd (&L[(size_t) off], &R[(size_t) off], m);
        }
        std::vector<float> mono ((size_t) N);
        for (int i = 0; i < N; ++i) mono[(size_t) i] = 0.5f * (L[(size_t) i] + R[(size_t) i]);
        checkFund ("unison-adopt", estFund (&mono[(size_t) (N / 2)], N / 2, sr), f0, 4.0);
    }

    // ── 7. budget thinning: a tight pool caps active partials, root survives ──
    {
        int used = 0;
        HarmParams p; p.count = 1.0f;               // ask for everything
        HarmonicEngine e; e.prepare (sr, true);
        e.setPartialBudget (&used, 48);
        e.setParams (p); e.noteOn (55.0, 7u);       // A1 — deep bank actually reachable
        std::vector<float> L (512, 0.f), R (512, 0.f);
        used = 0; e.renderBlockAdd (L.data(), R.data(), 512);
        const bool okCap  = e.preparedActive() <= 48 + 2;
        const bool okRoot = e.ratio1ForTesting() > 0.97f && e.ratio1ForTesting() < 1.03f;
        std::printf ("[%-22s] active=%d root=%.3f  %s\n", "budget thin", e.preparedActive(), (double) e.ratio1ForTesting(), (okCap && okRoot) ? "PASS" : "FAIL");
        pass += (okCap && okRoot); ++tot;
    }

    // ── 8. braid appends twins; display bins sane ──
    {
        HarmParams p; p.braid = 1.0f;
        HarmonicEngine e; e.prepare (sr, true);
        e.setParams (p); e.noteOn (f0, 5u);
        std::vector<float> L (512, 0.f), R (512, 0.f);
        e.renderBlockAdd (L.data(), R.data(), 512);
        const int withBraid = e.partialsForTesting();
        HarmParams q; q.braid = 0.0f;
        HarmonicEngine e2; e2.prepare (sr, true);
        e2.setParams (q); e2.noteOn (f0, 5u);
        e2.renderBlockAdd (L.data(), R.data(), 512);
        const bool ok = withBraid > e2.partialsForTesting();
        std::printf ("[%-22s] %d > %d  %s\n", "braid twins", withBraid, e2.partialsForTesting(), ok ? "PASS" : "FAIL");
        pass += ok; ++tot;

        float wBins[96], gBins[96];
        HarmonicEngine d; d.prepare (sr, true); d.setDisplayMode (true);
        d.setParams (p); d.noteOn (110.0, 3u);
        d.renderBlockAdd (L.data(), R.data(), 512);
        d.displayBins (wBins, gBins, 96);
        bool fin = true; float mx = 0.f;
        for (int b = 0; b < 96; ++b) { if (! std::isfinite (wBins[b]) || ! std::isfinite (gBins[b])) fin = false; mx = std::max (mx, wBins[b]); }
        const bool ok2 = fin && mx > 0.5f && mx <= 1.f;
        std::printf ("[%-22s] max=%.2f finite=%d  %s\n", "display bins", (double) mx, (int) fin, ok2 ? "PASS" : "FAIL");
        pass += ok2; ++tot;
    }

    // ── 8b. cleanup-sweep regressions: slot shrink and budget saturation must RAMP, not cut ──
    {
        // BRAID 0.7 -> 0 mid-note drops ~20 twin slots; the dropped slots must ramp out
        HarmParams pA; pA.braid = 0.7f;
        HarmParams pB = pA; pB.braid = 0.0f;
        HarmonicEngine e; e.prepare (sr, true); e.setParams (pA); e.noteOn (f0, 31u);
        const int N = (int) (sr * 0.3);
        std::vector<float> L ((size_t) N, 0.f), R ((size_t) N, 0.f);
        std::vector<double> diffRms;
        for (int off = 0; off < N; off += 256)
        {
            const int m = std::min (256, N - off);
            e.setParams (off > N / 2 ? pB : pA);
            e.renderBlockAdd (&L[(size_t) off], &R[(size_t) off], m);
            double sum = 0.0;
            for (int i = std::max (1, off); i < off + m; ++i)
            { const double d = (double) L[(size_t) i] - L[(size_t) i - 1]; sum += d * d; }
            diffRms.push_back (std::sqrt (sum / m));
        }
        std::vector<double> sorted = diffRms; std::sort (sorted.begin(), sorted.end());
        const double med = sorted[sorted.size() / 2];
        double worst = 0.0; for (double d : diffRms) worst = std::max (worst, d);
        const bool ok = worst < med * 4.0 + 1e-9;
        std::printf ("[%-22s] med=%.4f worst=%.4f  %s\n", "declick braid-drop", med, worst, ok ? "PASS" : "FAIL");
        pass += ok; ++tot;
    }
    {
        // budget flips to saturated mid-note: the bank must RAMP to silence (one block), never truncate
        int used = 0;
        HarmonicEngine e; e.prepare (sr, true);
        e.setPartialBudget (&used, 640);
        HarmParams p; e.setParams (p); e.noteOn (f0, 77u);
        const int N = (int) (sr * 0.2);
        std::vector<float> L ((size_t) N, 0.f), R ((size_t) N, 0.f);
        double preRms = 0.0; int preN = 0; float maxStep = 0.f; float prev = 0.f;
        for (int off = 0; off < N; off += 256)
        {
            const int m = std::min (256, N - off);
            used = (off > N / 2) ? 100000 : 0;            // saturate the pool mid-note
            e.setParams (p); e.renderBlockAdd (&L[(size_t) off], &R[(size_t) off], m);
            for (int i = off; i < off + m; ++i)
            {
                if (off <= N / 2) { preRms += (double) L[(size_t) i] * L[(size_t) i]; ++preN; }
                maxStep = std::max (maxStep, std::fabs (L[(size_t) i] - prev));
                prev = L[(size_t) i];
            }
        }
        preRms = std::sqrt (preRms / std::max (1, preN));
        double tailAbs = 0.0;
        for (int i = N - 2048; i < N; ++i) tailAbs = std::max (tailAbs, (double) std::fabs (L[(size_t) i]));
        // sounding before, silent at the end, and no sample step bigger than a plausible ramp slope
        const bool ok = preRms > 0.02 && tailAbs < 1e-4 && maxStep < (float) (preRms * 6.0);
        std::printf ("[%-22s] pre=%.3f tail=%.5f step=%.3f  %s\n", "saturation ramp-out", preRms, tailAbs, (double) maxStep, ok ? "PASS" : "FAIL");
        pass += ok; ++tot;
    }

    // ── 9. kitchen-sink finiteness: all knobs hot, low note, long render ──
    {
        HarmParams p; p.mainMode = 4; p.sculptMode = 5; p.hue = 1.f; p.carve = 1.f;
        p.count = 1.f; p.lean = 0.8f; p.fan = 1.f; p.grit = 1.f; p.braid = 1.f;
        p.churn = 1.f; p.root = 1.f; p.shine = 1.f; p.wilt = 0.f; p.forge = 1.f;
        double r = 0;
        const double f = renderFund (p, 41.2, sr, &r);   // E1
        const bool ok = f > 0.0 && r > 1e-4 && r < 2.0;
        std::printf ("[%-22s] fund=%.1f rms=%.3f  %s\n", "kitchen sink finite", f, r, ok ? "PASS" : "FAIL");
        pass += ok; ++tot;
    }

    // ── 10. CPU bench — the hard rule: measure before shipping ──
    {
        int used = 0;
        HarmParams p; p.mainMode = 1; p.sculptMode = 3; p.hue = 0.8f; p.carve = 0.8f;
        p.count = 1.f; p.grit = 0.6f; p.braid = 0.7f; p.fan = 1.f; p.forge = 0.4f; p.shine = 0.5f;
        HarmonicEngine e; e.prepare (sr, true);
        e.setPartialBudget (&used, 640);
        e.setParams (p); e.noteOn (55.0, 11u);
        std::vector<float> L (512, 0.f), R (512, 0.f);
        // warm up
        for (int w = 0; w < 20; ++w) { used = 0; e.setParams (p); e.renderBlockAdd (L.data(), R.data(), 512); e.postProcess (L.data(), R.data(), 512); }
        const int iters = 400;
        auto t0 = std::chrono::steady_clock::now();
        for (int w = 0; w < iters; ++w) { used = 0; e.setParams (p); e.renderBlockAdd (L.data(), R.data(), 512); e.postProcess (L.data(), R.data(), 512); }
        auto t1 = std::chrono::steady_clock::now();
        const double usPerBlock = std::chrono::duration<double, std::micro> (t1 - t0).count() / iters;
        const double blockMs = 512.0 / sr * 1000.0;
        const double pct = usPerBlock / (blockMs * 1000.0) * 100.0;
        std::printf ("[%-22s] %.1f us/blk = %.1f%% of one core (A1, budget 640, all-hot)\n", "cpu bench voice", usPerBlock, pct);
        const bool ok = pct < 15.0;
        std::printf ("[%-22s] %s\n", "cpu ceiling", ok ? "PASS" : "FAIL");
        pass += ok; ++tot;

        // prepare-only cost (the per-anchor spectral build)
        auto t2 = std::chrono::steady_clock::now();
        for (int w = 0; w < iters; ++w) { e.setParams (p); e.prepareBank (512); }
        auto t3 = std::chrono::steady_clock::now();
        std::printf ("[%-22s] %.1f us/blk\n", "prepare-only cost",
                     std::chrono::duration<double, std::micro> (t3 - t2).count() / iters);
    }

    // ── FORGE: harmonic drive (hm5 — replaced Fizz) ─────────────────────────
    { HarmParams p; p.forge = 1.0f; checkFund ("forge=1.0 keeps pitch", renderFund (p, f0, sr), f0, 5.0); }
    { HarmParams p; p.mainMode = 1; p.hue = 0.4f; p.forge = 1.0f; checkFund ("neon forge=1.0", renderFund (p, f0, sr), f0, 5.0); }
    {
        // night-and-day: HF share (derivative RMS over RMS) must rise hard with drive,
        // while renorm holds the LEVEL — timbre flips, loudness doesn't pump
        auto bright = [&] (float fg, double& rmsOut)
        {
            HarmParams p; p.forge = fg; p.count = 0.1f;   // DARK source — drive must regenerate highs
            HarmonicEngine e; e.prepare (sr, true); e.setParams (p); e.noteOn (f0, 5u);
            const int N = (int) sr / 2;
            std::vector<float> L ((size_t) N, 0.f), R ((size_t) N, 0.f);
            for (int off = 0; off < N; off += 256)
            { const int m = std::min (256, N - off);
              e.setParams (p); e.renderBlockAdd (&L[(size_t) off], &R[(size_t) off], m);
              e.postProcess (&L[(size_t) off], &R[(size_t) off], m); }
            double sq = 0.0, dq = 0.0;
            for (int i = N / 2 + 1; i < N; ++i)
            {
                sq += (double) L[(size_t) i] * L[(size_t) i];
                const double df = (double) L[(size_t) i] - (double) L[(size_t) i - 1];
                dq += df * df;
            }
            rmsOut = std::sqrt (sq / (double) (N / 2));
            return std::sqrt (dq / std::max (1e-12, sq));
        };
        double r0 = 0.0, r1 = 0.0;
        const double b0 = bright (0.f, r0), b1 = bright (1.f, r1);
        const bool okB = b1 > b0 * 1.8;
        const double lvl = 20.0 * std::log10 (std::max (1e-9, r1) / std::max (1e-9, r0));
        const bool okL = std::fabs (lvl) < 4.0;
        std::printf ("[%-22s] hf x%.2f  lvl %+.1f dB  %s\n", "forge night-and-day",
                     b1 / std::max (1e-9, b0), lvl, (okB && okL) ? "PASS" : "FAIL");
        pass += (okB && okL); ++tot;
    }
    {
        // declick: a hard forge jump 0 -> 1 mid-note must ramp inside one block — the
        // honest yardstick is the worst per-sample step of a STEADY forge=1 render
        auto worstStep = [&] (bool jump)
        {
            HarmParams p; p.forge = jump ? 0.f : 1.f;
            HarmonicEngine e; e.prepare (sr, true); e.setParams (p); e.noteOn (f0, 91u);
            const int N = (int) (sr * 0.2);
            std::vector<float> L ((size_t) N, 0.f), R ((size_t) N, 0.f);
            float mx = 0.f, prev = 0.f;
            for (int off = 0; off < N; off += 256)
            {
                const int m = std::min (256, N - off);
                if (jump) p.forge = (off > N / 2) ? 1.f : 0.f;
                e.setParams (p); e.renderBlockAdd (&L[(size_t) off], &R[(size_t) off], m);
                e.postProcess (&L[(size_t) off], &R[(size_t) off], m);
                for (int i = off; i < off + m; ++i)
                {
                    if (i > (int) sr / 100)   // past the note-on ramp-in
                        mx = std::max (mx, std::fabs (L[(size_t) i] - prev));
                    prev = L[(size_t) i];
                }
            }
            return mx;
        };
        const float ref = worstStep (false), jmp = worstStep (true);
        const bool ok = jmp < ref * 1.5f + 1e-4f;
        std::printf ("[%-22s] jump=%.4f steady=%.4f  %s\n", "declick forge-jump", jmp, ref, ok ? "PASS" : "FAIL");
        pass += ok; ++tot;
    }

    {
        // forge=0 bypass must be bit-identical (postProcess early-outs, buffer untouched)
        HarmParams p; HarmonicEngine e; e.prepare (sr, true); e.setParams (p); e.noteOn (f0, 3u);
        std::vector<float> L (2048, 0.f), R (2048, 0.f);
        e.renderBlockAdd (L.data(), R.data(), 2048);
        std::vector<float> Lc = L, Rc = R;
        e.postProcess (L.data(), R.data(), 2048);
        bool same = true;
        for (int i = 0; i < 2048; ++i) if (L[(size_t) i] != Lc[(size_t) i] || R[(size_t) i] != Rc[(size_t) i]) { same = false; break; }
        std::printf ("[%-22s] %s\n", "forge=0 bit-identical", same ? "PASS" : "FAIL");
        pass += same; ++tot;
    }
    {
        // the asymmetry bias injects DC by design — the blocker must remove it
        HarmParams p; p.forge = 1.f;
        HarmonicEngine e; e.prepare (sr, true); e.setParams (p); e.noteOn (f0, 21u);
        const int N = (int) sr / 2;
        std::vector<float> L ((size_t) N, 0.f), R ((size_t) N, 0.f);
        for (int off = 0; off < N; off += 256)
        { const int m = std::min (256, N - off);
          e.setParams (p); e.renderBlockAdd (&L[(size_t) off], &R[(size_t) off], m);
          e.postProcess (&L[(size_t) off], &R[(size_t) off], m); }
        double mean = 0.0, rms = 0.0;
        for (int i = N / 2; i < N; ++i) { mean += (double) L[(size_t) i]; rms += (double) L[(size_t) i] * L[(size_t) i]; }
        mean /= (double) (N / 2); rms = std::sqrt (rms / (double) (N / 2));
        const bool ok = std::fabs (mean) < 0.02 * std::max (1e-6, rms);
        std::printf ("[%-22s] dc=%.5f rms=%.3f  %s\n", "forge DC clean", mean, rms, ok ? "PASS" : "FAIL");
        pass += ok; ++tot;
    }

    std::printf ("═══ %d/%d PASS ═══\n", pass, tot);
    return pass == tot ? 0 : 1;
}
