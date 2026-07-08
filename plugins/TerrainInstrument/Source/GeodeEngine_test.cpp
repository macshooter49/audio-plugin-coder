// ════════════════════════════════════════════════════════════════════════════
//  GeodeEngine_test.cpp — offline DSP proof for the RESYNTH oscillator.
//  (Pattern A — NOT in CMakeLists.) Build + run:
//    c++ -std=c++17 -O2 -Wall -Wextra -ISource Source/GeodeEngine_test.cpp -o /tmp/gd && /tmp/gd
//
//  Proves the load-bearing property Max cares about: NOTHING DETUNES. Every sculpt/
//  degrade control (Formant, Shape, Drive, Crush, Cut, Sieve, Tilt) must keep the
//  played fundamental fixed. Also checks audio is produced and CRUSH quantizes.
// ════════════════════════════════════════════════════════════════════════════
#include "GeodeEngine.h"
#include <cstdio>
#include <vector>
#include <cmath>
#include <complex>
#include <chrono>
#include <algorithm>
using namespace tw;

// A harmonic input "sample" spectrum: fundamental + H harmonics at 1/n (saw-ish).
static GeodeFrameStore makeStore (int H)
{
    GeodeFrameStore s; s.frames.resize (4); s.naturalSec = 2.0f; s.valid = true; s.f0 = 220.f;
    for (auto& f : s.frames)
    {
        int c = 0;
        for (int n = 1; n <= H; ++n) { f.ratio[(size_t) c] = (float) n; f.amp[(size_t) c] = 1.f / (float) n; ++c; }
        f.nPartials = c;
    }
    return s;
}

// McLeod-style first-peak NSDF fundamental estimate (octave-error resistant).
static double estFund (const float* x, int n, double sr)
{
    double e0 = 0; for (int i = 0; i < n; ++i) e0 += (double) x[i] * x[i]; if (e0 < 1e-9) return 0;
    int minLag = (int) (sr / 2000.0), maxLag = (int) (sr / 50.0);
    std::vector<double> nsdf ((size_t) maxLag + 1, 0.0);
    double best = 0;
    for (int lag = minLag; lag < maxLag; ++lag)
    {
        double ac = 0, e1 = 0, e2 = 0;
        for (int i = 0; i + lag < n; ++i) { ac += (double) x[i] * x[i + lag]; e1 += (double) x[i] * x[i]; e2 += (double) x[i + lag] * x[i + lag]; }
        double v = (e1 + e2 > 1e-9) ? 2 * ac / (e1 + e2) : 0; nsdf[(size_t) lag] = v; if (v > best) best = v;
    }
    double thr = 0.85 * best;
    for (int lag = minLag + 1; lag < maxLag - 1; ++lag)
        if (nsdf[(size_t) lag] >= thr && nsdf[(size_t) lag] >= nsdf[(size_t) (lag - 1)] && nsdf[(size_t) lag] >= nsdf[(size_t) (lag + 1)])
        {
            double ym1 = nsdf[(size_t) (lag - 1)], y0 = nsdf[(size_t) lag], y1 = nsdf[(size_t) (lag + 1)];
            double den = ym1 - 2 * y0 + y1, d = (std::fabs (den) > 1e-12) ? 0.5 * (ym1 - y1) / den : 0;
            return sr / (lag + d);
        }
    return 0;
}

static double renderFund (GeodeParams p, GeodeFrameStore& st, double playHz, double sr)
{
    GeodeEngine e; e.prepare (sr); e.setFrameStore (&st); e.setParams (p); e.noteOn (playHz, 999);
    int N = (int) (sr * 0.3); std::vector<float> L ((size_t) N, 0), R ((size_t) N, 0);
    int blk = 256;
    for (int off = 0; off < N; off += blk) { int m = std::min (blk, N - off); e.setParams (p); e.renderBlockAdd (&L[(size_t) off], &R[(size_t) off], m); e.postProcess (&L[(size_t) off], &R[(size_t) off], m); }
    return estFund (&L[(size_t) (N / 2)], N / 2, sr);
}

int main()
{
    const double sr = 48000.0, playHz = 261.63;   // C4
    auto st = makeStore (12);
    GeodeParams base; base.scan = 0.f;             // HOLD read-head → measure a stable frame

    std::printf ("=== RESYNTH DSP proof (played C4 = %.2f Hz) ===\n", playHz);
    int pass = 0, tot = 0;
    auto check = [&] (const char* nm, double f) { bool ok = (f > 1 && std::fabs (f - playHz) < 6); std::printf ("[%-16s] fund=%7.1fHz  %s\n", nm, f, ok ? "PASS" : "FAIL"); pass += ok; ++tot; };

    check ("audio/neutral",  renderFund (base, st, playHz, sr));
    { GeodeParams p = base; p.formant = 0.0f; check ("formant=0.0", renderFund (p, st, playHz, sr)); }
    { GeodeParams p = base; p.formant = 1.0f; check ("formant=1.0", renderFund (p, st, playHz, sr)); }
    { GeodeParams p = base; p.shape = 1.0f; p.shapeTarget = 2; check ("shape=saw", renderFund (p, st, playHz, sr)); }
    { GeodeParams p = base; p.shape = 1.0f; p.shapeTarget = 1; check ("shape=square", renderFund (p, st, playHz, sr)); }
    { GeodeParams p = base; p.shape = 1.0f; p.shapeTarget = 0; check ("shape=sine", renderFund (p, st, playHz, sr)); }
    // all 11 SHAPE targets must keep the played fundamental (they tune overtones onto harmonics)
    { const char* shN[11] = { "sine","square","saw","triangle","pulse","hollow","organ","half","vowel","bright","metal" };
      for (int t = 3; t <= 10; ++t) { GeodeParams p = base; p.shape = 1.0f; p.shapeTarget = t;
        char nm[28]; std::snprintf (nm, sizeof nm, "shape=%s", shN[t]); check (nm, renderFund (p, st, playHz, sr)); } }
    { GeodeParams p = base; p.drive = 1.0f; check ("drive=1.0", renderFund (p, st, playHz, sr)); }
    // rs6 — every DRIVE mode keeps the played fundamental (spectral children ≥ 0.75×fund; folders period-preserving)
    { const char* dm[6] = { "saturate","bloom","glint","moire","foldback","ember" };
      for (int m = 1; m <= 5; ++m) { GeodeParams p = base; p.drive = 0.9f; p.driveMode = m;
        char nm[28]; std::snprintf (nm, sizeof nm, "drive=%s", dm[m]); check (nm, renderFund (p, st, playHz, sr)); } }
    // rs6 — every SIEVE mode keeps the fundamental (they only kill/attenuate partials; loudest = fund survives)
    { const char* sm[6] = { "floor","sparse","cloak","flicker","rake","parity" };
      for (int m = 1; m <= 5; ++m) { GeodeParams p = base; p.sieve = 0.6f; p.sieveMode = m;
        char nm[28]; std::snprintf (nm, sizeof nm, "sieve=%s", sm[m]); check (nm, renderFund (p, st, playHz, sr)); } }
    // rs6 — MELT (temporal smear) + QUALITY-as-bitrate extremes stay in tune
    { GeodeParams p = base; p.smear = 0.9f; check ("melt=0.9", renderFund (p, st, playHz, sr)); }
    { GeodeParams p = base; p.quality = 0.05f; check ("quality=trash", renderFund (p, st, playHz, sr)); }

    // ── rs7 — SAMPLER-PARITY region / loop / reverse / fades + constant-cost unison adopt ──────
    {
        auto mkEng = [&] (GeodeParams p) { GeodeEngine e; e.prepare (sr); e.setFrameStore (&st); e.setParams (p); e.noteOn (playHz, 42); return e; };
        std::vector<float> L (512, 0.f), R (512, 0.f);
        // region confinement: head stays inside [0.2,0.8]; forward loop settles into [0.3,0.6]
        { GeodeParams p; p.scan = 1.0f; p.regionStart = 0.2f; p.regionEnd = 0.8f;
          p.loopStart = 0.3f; p.loopEnd = 0.6f; p.loopMode = 1;
          GeodeEngine e = mkEng (p);
          bool inRegion = true; float last = -1.f;
          for (int b = 0; b < 400; ++b) { std::fill (L.begin(), L.end(), 0.f); std::fill (R.begin(), R.end(), 0.f);
            e.setParams (p); e.renderBlockAdd (L.data(), R.data(), 512); last = e.readPos01();
            if (last < 0.199f || last > 0.801f) inRegion = false; }
          const bool inLoop = (last >= 0.295f && last <= 0.605f);
          std::printf ("[region+loop      ] pos=%.3f inRegion=%d inLoop=%d  %s\n", last, inRegion, inLoop,
                       (inRegion && inLoop) ? "PASS" : "FAIL"); pass += (inRegion && inLoop); ++tot; }
        // REVERSE really reverses: head decreases from region end
        { GeodeParams p; p.scan = 1.0f; p.loopMode = 2;
          GeodeEngine e = mkEng (p);
          const float p0 = e.readPos01();
          for (int b = 0; b < 8; ++b) { std::fill (L.begin(), L.end(), 0.f); std::fill (R.begin(), R.end(), 0.f);
            e.setParams (p); e.renderBlockAdd (L.data(), R.data(), 512); }
          const float p1 = e.readPos01();
          const bool ok = (p0 > 0.9f && p1 < p0 - 1e-4f);
          std::printf ("[reverse          ] pos %.3f -> %.3f  %s\n", p0, p1, ok ? "PASS" : "FAIL"); pass += ok; ++tot; }
        // FADE IN: audio near the region start is quieter than after the fade completes
        { GeodeParams p; p.scan = 1.0f; p.fadeIn = 0.4f; p.loopMode = 0;
          GeodeEngine e = mkEng (p);
          auto rms = [&] { double s2 = 0; for (int i = 0; i < 512; ++i) s2 += (double) L[i] * L[i]; return std::sqrt (s2 / 512.0); };
          std::fill (L.begin(), L.end(), 0.f); std::fill (R.begin(), R.end(), 0.f);
          e.setParams (p); e.renderBlockAdd (L.data(), R.data(), 512); e.setParams (p); e.renderBlockAdd (L.data(), R.data(), 512);
          const double early = rms();
          for (int b = 0; b < 200; ++b) { std::fill (L.begin(), L.end(), 0.f); std::fill (R.begin(), R.end(), 0.f);
            e.setParams (p); e.renderBlockAdd (L.data(), R.data(), 512); }
          const double late = rms();
          const bool ok = late > early * 1.6;
          std::printf ("[fade-in          ] early=%.4f late=%.4f  %s\n", early, late, ok ? "PASS" : "FAIL"); pass += ok; ++tot; }
        // constant-cost unison adopt path keeps the pitch (anchor prepares, sibling adopts+renders)
        { GeodeParams p; p.scan = 0.f;
          GeodeEngine anchor = mkEng (p), sib = mkEng (p);
          int Nn = (int) (sr * 0.3); std::vector<float> LA ((size_t) Nn, 0.f), RA ((size_t) Nn, 0.f);
          for (int off = 0; off < Nn; off += 256)
          { int m = std::min (256, Nn - off);
            anchor.setParams (p); anchor.prepareBank (m);
            sib.adoptBank (anchor); sib.renderBankAdd (&LA[(size_t) off], &RA[(size_t) off], m);
            anchor.renderBankAdd (&LA[(size_t) off], &RA[(size_t) off], m); }
          check ("unison-adopt", estFund (&LA[(size_t) (Nn / 2)], Nn / 2, sr)); }
    }
    { GeodeParams p = base; p.crush = 0.8f; check ("crush=0.8", renderFund (p, st, playHz, sr)); }
    { GeodeParams p = base; p.cut = 0.4f; p.cutMode = 0; check ("cut=LP.4", renderFund (p, st, playHz, sr)); }
    { GeodeParams p = base; p.sieve = 0.5f; check ("sieve=0.5", renderFund (p, st, playHz, sr)); }
    { GeodeParams p = base; p.tilt = 0.9f; check ("tilt=0.9", renderFund (p, st, playHz, sr)); }

    // CRUSH must quantize the output to far fewer distinct levels.
    auto distinct = [] (std::vector<float>& v) { std::vector<int> q; for (float x : v) q.push_back ((int) std::round (x * 1000)); std::sort (q.begin(), q.end()); q.erase (std::unique (q.begin(), q.end()), q.end()); return (int) q.size(); };
    int N = 2000; std::vector<float> L0 ((size_t) N, 0), R0 ((size_t) N, 0), L1 ((size_t) N, 0), R1 ((size_t) N, 0);
    { GeodeEngine e; e.prepare (sr); e.setFrameStore (&st); e.setParams (base); e.noteOn (playHz, 7); e.renderBlockAdd (L0.data(), R0.data(), N); }
    { GeodeParams pc = base; pc.crush = 0.9f; GeodeEngine e; e.prepare (sr); e.setFrameStore (&st); e.setParams (pc); e.noteOn (playHz, 7); e.renderBlockAdd (L1.data(), R1.data(), N); e.postProcess (L1.data(), R1.data(), N); }
    int d0 = distinct (L0), d1 = distinct (L1);
    bool crushOk = d1 < d0 / 2;
    std::printf ("[crush-quantize   ] nocrush=%d crush=%d  %s\n", d0, d1, crushOk ? "PASS" : "FAIL");
    pass += crushOk; ++tot;

    // ── SHAPE anti-rumble regression (rs2) — the load-bearing SHAPE test. A REAL sample's analyzed
    // bank is inharmonic and has a SUB-fundamental peak; the old clean-harmonic makeStore() couldn't
    // expose the bug Max hit ("rumble slop / detuned spray"). SHAPE→saw must yield a fundamental-
    // dominant, in-tune harmonic series with ~no octave-down energy. ───────────────────────────────
    {
        GeodeFrameStore ms; ms.frames.resize (4); ms.naturalSec = 2.0f; ms.valid = true; ms.f0 = (float) playHz;
        const float MR[] = { 0.50f, 1.00f, 1.71f, 2.00f, 2.34f, 3.00f, 3.13f, 4.02f, 4.83f, 5.10f, 6.00f, 7.29f };
        const float MA[] = { 0.55f, 1.00f, 0.62f, 0.50f, 0.44f, 0.33f, 0.30f, 0.26f, 0.22f, 0.20f, 0.17f, 0.14f };
        for (auto& f : ms.frames) { for (int i = 0; i < 12; ++i) { f.ratio[(size_t) i] = MR[i]; f.amp[(size_t) i] = MA[i]; } f.nPartials = 12; }
        GeodeParams sp; sp.scan = 0.f; sp.shape = 1.0f; sp.shapeTarget = 2; sp.quality = 1.0f;   // full saw
        const int NN = 16384; std::vector<float> xs ((size_t) NN, 0.f), rr2 ((size_t) NN, 0.f);
        GeodeEngine e; e.prepare (sr); e.setFrameStore (&ms); e.setParams (sp); e.noteOn (playHz, 5);
        for (int off = 0; off < NN; off += 256) { int m = std::min (256, NN - off); e.setParams (sp); e.renderBlockAdd (&xs[(size_t) off], &rr2[(size_t) off], m); }
        std::vector<std::complex<float>> bf ((size_t) NN);
        for (int i = 0; i < NN; ++i) { float w = 0.5f - 0.5f * std::cos (2.f * 3.14159265f * (float) i / (NN - 1)); bf[(size_t) i] = { xs[(size_t) i] * w, 0.f }; }
        geodedsp::fft (bf.data(), NN, false);
        const double binHz = sr / NN; double subE = 0, totE = 0, peakMag = 0; int peakBin = 0;
        const int subLim = (int) (0.75 * playHz / binHz);
        for (int b = 1; b < NN / 2; ++b) { const double mg = std::abs (bf[(size_t) b]), en = mg * mg; totE += en; if (b <= subLim) subE += en; if (mg > peakMag) { peakMag = mg; peakBin = b; } }
        const double subPct = totE > 0 ? 100.0 * subE / totE : 0.0, peakHz = peakBin * binHz;
        const bool shapeOk = (subPct < 5.0) && (std::fabs (peakHz - playHz) < playHz * 0.06);  // no rumble + fundamental-dominant
        std::printf ("[shape-antirumble ] sub=%.1f%% peak=%.1fHz (fund %.1f)  %s\n", subPct, peakHz, playHz, shapeOk ? "PASS" : "FAIL");
        pass += shapeOk; ++tot;
    }

    // ── sparse-bank regression (cleanup sweep): the tracker leaves zero-amp gap slots below
    // nPartials; keepLoudest must NOT count gaps against the QUALITY quota (it silenced real
    // partials living in high slot indices). Store: nPartials=56 but only 2 live partials @50/55.
    {
        GeodeFrameStore sp; sp.frames.resize (2); sp.naturalSec = 1.0f; sp.valid = true; sp.f0 = (float) playHz;
        for (auto& f : sp.frames)
        { f.ratio[50] = 1.f; f.amp[50] = 0.30f; f.ratio[55] = 2.f; f.amp[55] = 0.15f; f.nPartials = 56; }
        GeodeParams p; p.scan = 0.f; p.quality = 0.5f;    // active ≈ 34 < 56 → keepLoudest engages
        GeodeEngine e; e.prepare (sr); e.setFrameStore (&sp); e.setParams (p); e.noteOn (playHz, 3);
        const int NN = (int) (sr * 0.3); std::vector<float> LS ((size_t) NN, 0.f), RS ((size_t) NN, 0.f);
        for (int off = 0; off < NN; off += 256)
        { int m = std::min (256, NN - off); e.setParams (p); e.renderBlockAdd (&LS[(size_t) off], &RS[(size_t) off], m); }
        const double f = estFund (&LS[(size_t) (NN / 2)], NN / 2, sr);
        const bool ok = f > 1 && std::fabs (f - playHz) < 6;   // pre-fix: total silence (fund=0)
        std::printf ("[sparse-bank      ] fund=%7.1fHz  %s\n", f, ok ? "PASS" : "FAIL"); pass += ok; ++tot;
    }

    // ── CPU CEILING GUARD (rs2) — never ship a Resynth that pegs a core again. ──────────────
    // Saturate the shared partial budget the way STRETCH + a big unison chord does, worst-case
    // knobs on, and assert the per-block cost stays a small fraction of one core. (Full profiling
    // harness: GeodeEngine_cpubench.cpp.) Threshold is generous so it won't false-fail on a slow
    // box but WILL catch the 40-60% cliff that shipped in rs1.
    {
        const int   blk = 128, banks = 256, BUD = 640;   // BUD mirrors kGeodePartialBudget
        const double blockNs = (double) blk / sr * 1e9;  // realtime ns/block (one core)
        std::vector<GeodeEngine> engs ((size_t) banks);
        int live = 0;
        GeodeParams wp; wp.quality = 1.0f; wp.stretch = 0.98f; wp.scan = 0.5f;      // STRETCH pad
        wp.formant = 1.0f; wp.tilt = 0.9f; wp.shape = 1.0f; wp.cut = 0.4f; wp.drive = 0.7f; // all sculpt ON
        wp.smear = 0.8f; wp.driveMode = 1; wp.sieve = 0.3f; wp.sieveMode = 2;       // rs6 kitchen sink: MELT + BLOOM children + CLOAK O(n²)
        for (int i = 0; i < banks; ++i)
        { auto& e = engs[(size_t) i]; e.prepare (sr); e.setFrameStore (&st);
          e.setPartialBudget (&live, BUD); e.setUnisonScale (16);
          GeodeParams pi = wp; pi.start = (float) i / banks; e.setParams (pi);
          e.noteOn (261.63 * (1.0 + 0.001 * i), (std::uint32_t) (7 + i)); }
        std::vector<float> L ((size_t) blk, 0.f), R ((size_t) blk, 0.f);
        for (int w = 0; w < 8; ++w) { live = 0; for (auto& e : engs) { e.renderBlockAdd (L.data(), R.data(), blk); e.postProcess (L.data(), R.data(), blk); } }
        auto t0 = std::chrono::high_resolution_clock::now();
        for (int b = 0; b < 3000; ++b)
        { live = 0; std::fill (L.begin(), L.end(), 0.f); std::fill (R.begin(), R.end(), 0.f);
          for (auto& e : engs) { e.renderBlockAdd (L.data(), R.data(), blk); e.postProcess (L.data(), R.data(), blk); } }
        auto t1 = std::chrono::high_resolution_clock::now();
        double nsBlk = std::chrono::duration<double, std::nano> (t1 - t0).count() / 3000.0;
        double pct = nsBlk / blockNs * 100.0;
        bool cpuOk = pct < 40.0;   // rs1 shipped at ~40-62%; rs2 target is ~10-15% (~2x headroom here for -O2/slow box)
        std::printf ("[cpu-ceiling      ] worst-case %d live partials -> %.1f%% of one core  %s\n",
                     live, pct, cpuOk ? "PASS" : "FAIL");
        pass += cpuOk; ++tot;
    }

    std::printf ("=== %d/%d PASS ===\n", pass, tot);
    return (pass == tot) ? 0 : 1;
}
