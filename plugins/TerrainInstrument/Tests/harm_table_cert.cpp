// ══════════════════════════════════════════════════════════════════════════════════════════════
//  harm_table_cert.cpp — fb588: THE ADDITIVE BANK BUILT FROM A WAVETABLE.
//
//    clang++ -O2 -std=c++17 -I Tests/shim -I Source Tests/harm_table_cert.cpp \
//            -o /tmp/harm_table_cert -framework Accelerate && /tmp/harm_table_cert
//
//  Max: "I want to be able to actually have additive synthesis with the wave tables."
//
//  THE BARS
//   1  THE TABLE SOUNDS LIKE THE TABLE — the bank's spectrum matches the wavetable oscillator's
//      on the same table, far closer than that table sits from any OTHER table
//   2  THE PHASE CAME THROUGH — the rendered WAVEFORM matches the table's, which is the whole
//      reason phase is carried rather than hashed. (A waveform test on purpose: magnitude-only
//      would pass a spectral test perfectly and still be the wrong answer.)
//   3  SPECTRAL DRIFT IS ALIVE — that table writes ONE CONSTANT amplitude across all 16 frames and
//      morphs only PHASE, so under magnitude-only its WT Pos knob would be stone dead. This is the
//      bar that decided the design; it must show real movement.
//   4  THE 16-VOICE HEADROOM HOLDS — handing every unison sibling the table's phases verbatim makes
//      them coherent and their note-on peaks sum LINEARLY instead of by root-N. The per-sibling
//      rigid rotation is what prevents that, and this measures it against both predictions.
//   5  THE SIX EXISTING FAMILIES ARE UNTOUCHED — asserted here as "still sane", and proven
//      bit-identical by the separate before/after null run the build script performs.
// ══════════════════════════════════════════════════════════════════════════════════════════════
#include "WavetableBank.h"
#include "HarmonicEngine.h"
#include "HarmTableSource.h"
#include <cstdio>
#include <cmath>
#include <vector>
#include <memory>
#include <string>
#include <algorithm>

using namespace tw;
static const double SR = 48000.0;
static const int    NREN = 8192;      // render length
static const int    HM   = 96;        // harmonics measured

static int gPass = 0, gFail = 0;
static void gate (bool c, const char* n, const std::string& d = "")
{ c ? ++gPass : ++gFail; std::printf ("  %-5s %-54s %s\n", c ? "ok" : "FAIL", n, d.c_str()); }

// ── render the additive engine ────────────────────────────────────────────────────────────────
static std::vector<float> renderHarm (const HarmParams& p, double f0, std::uint32_t seed, int n)
{
    HarmonicEngine e; e.prepare (SR, true);
    e.setParams (p); e.noteOn (f0, seed);
    std::vector<float> L ((size_t) n, 0.f), R ((size_t) n, 0.f);
    for (int off = 0; off < n; off += 256)
    { const int m = std::min (256, n - off);
      e.setParams (p);
      e.renderBlockAdd (&L[(size_t) off], &R[(size_t) off], m); }
    for (int i = 0; i < n; ++i) L[(size_t) i] = 0.5f * (L[(size_t) i] + R[(size_t) i]);
    return L;
}
static double rmsOf (const std::vector<float>& x, int from = 0)
{ double s = 0; int n = 0; for (size_t i = (size_t) from; i < x.size(); ++i) { s += (double) x[i]*x[i]; ++n; }
  return n ? std::sqrt (s / n) : 0.0; }
static double peakOf (const std::vector<float>& x, int from, int to)
{ double p = 0; for (int i = from; i < to && i < (int) x.size(); ++i) p = std::max (p, (double) std::fabs (x[(size_t) i])); return p; }

// harmonic magnitudes of a signal at a known f0
static std::vector<double> harmsOf (const std::vector<float>& x, double f0, int from)
{
    std::vector<double> m ((size_t) HM + 1, 0.0);
    const int n = (int) x.size() - from;
    for (int h = 1; h <= HM; ++h)
    { const double w = 2.0 * M_PI * h * f0 / SR; double re = 0, im = 0;
      for (int i = 0; i < n; ++i) { re += x[(size_t)(from+i)] * std::cos (w*i); im += x[(size_t)(from+i)] * std::sin (w*i); }
      m[(size_t) h] = std::sqrt (re*re + im*im) / n; }
    return m;
}
static double dist (const std::vector<double>& a, const std::vector<double>& b)
{
    double pa=0,pb=0; for (int h=1;h<=HM;++h){pa=std::max(pa,a[(size_t)h]);pb=std::max(pb,b[(size_t)h]);}
    if (pa<=0||pb<=0) return 999.0; double num=0,den=0;
    for (int h=1;h<=HM;++h)
    { const double na=a[(size_t)h]/pa, nb=b[(size_t)h]/pb; const double w=std::max(na,nb);
      if (w<1e-4) continue;
      const double d=20*std::log10(std::max(na,1e-4))-20*std::log10(std::max(nb,1e-4));
      num+=w*d*d; den+=w; }
    return den>0? std::sqrt(num/den):0.0;
}

static HarmParams tableParams (const float* a, const float* ph, int n)
{
    HarmParams p;
    p.mainMode = 6; p.count = 1.0f; p.lean = 0.5f; p.carve = 0.0f; p.grit = 0.0f;
    p.braid = 0.0f; p.root = 0.0f; p.shine = 0.0f; p.wilt = 0.5f; p.fan = 0.0f; p.forge = 0.0f;
    p.tableAmp = a; p.tablePhase = ph; p.tableN = n;
    p.tableSig = HarmTableSource::signature (a, ph, n);
    return p;
}

int main()
{
    std::printf ("\n══ harm_table_cert — fb588 ══  the additive bank, built from a wavetable\n\n");
    const double f0 = 110.0;
    const int    SKIP = 2048;                     // let the amp ramps settle
    static float A[512], P[512], A2[512], P2[512];

    // ── 1 + 2 · the table sounds like the table, and the phase came through ──────────────────
    {
        const int PR[4] = { 4, 2, 16, 12 };
        const char* PN[4] = { "ProphetSaw", "Square", "VowelMorph", "D50Bell" };
        double worstSpec = 0, bestOther = 999; std::string who, otherWho;
        double worstWave = 0;
        for (int t = 0; t < 4; ++t)
        {
            const WavetableSpec sp = WavetableBank::specForPreset (PR[t]);
            const int n = HarmTableSource::resolve (sp, 0.35f, A, P, 512);
            const auto out = renderHarm (tableParams (A, P, n), f0, 4242u, NREN);

            // the wavetable oscillator's own spectrum for the same table + position
            std::unique_ptr<Wavetable> w (new Wavetable()); w->buildFromSpec (sp);
            std::vector<float> cyc (2048, 0.f); w->renderBlend (0, 0.35f, 0.0f, cyc.data());
            std::vector<double> tabH ((size_t) HM + 1, 0.0);
            for (int h = 1; h <= HM; ++h)
            { double re=0, im=0;
              for (int i = 0; i < 2048; ++i) { const double an = 2.0*M_PI*h*i/2048.0;
                re += cyc[(size_t)i]*std::cos(an); im += cyc[(size_t)i]*std::sin(an); }
              tabH[(size_t)h] = std::sqrt(re*re+im*im)/2048.0; }

            const auto oh = harmsOf (out, f0, SKIP);
            const double d = dist (oh, tabH);
            if (d > worstSpec) { worstSpec = d; who = PN[t]; }

            // how close is this table to a DIFFERENT table? that is the scale the number lives on
            const WavetableSpec sp2 = WavetableBank::specForPreset (PR[(t + 1) % 4]);
            std::unique_ptr<Wavetable> w2 (new Wavetable()); w2->buildFromSpec (sp2);
            std::vector<float> c2 (2048, 0.f); w2->renderBlend (0, 0.35f, 0.0f, c2.data());
            std::vector<double> t2 ((size_t) HM + 1, 0.0);
            for (int h = 1; h <= HM; ++h)
            { double re=0, im=0;
              for (int i = 0; i < 2048; ++i) { const double an = 2.0*M_PI*h*i/2048.0;
                re += c2[(size_t)i]*std::cos(an); im += c2[(size_t)i]*std::sin(an); }
              t2[(size_t)h] = std::sqrt(re*re+im*im)/2048.0; }
            const double dOther = dist (tabH, t2);
            if (dOther < bestOther) { bestOther = dOther; otherWho = PN[t]; }
            std::printf ("        %-12s n=%-4d spec %6.2f dB   (vs a different table: %6.2f dB)\n",
                         PN[t], n, d, dOther);

            // WAVEFORM: one period of the render against the table's cycle, best rotation
            const int per = (int) std::lround (SR / f0);
            std::vector<float> onePer ((size_t) per);
            for (int i = 0; i < per; ++i) onePer[(size_t) i] = out[(size_t) (SKIP + i)];
            const double pw = peakOf (onePer, 0, per), pc = peakOf (cyc, 0, 2048);
            double best = 9e9;
            if (pw > 1e-9 && pc > 1e-9)
              for (int r = 0; r < per; ++r)
              { double acc = 0;
                for (int i = 0; i < per; ++i)
                { const double u = (double) i / per;
                  const double cv = cyc[(size_t) std::min (2047, (int) (u * 2048.0))] / pc;
                  acc += std::pow (onePer[(size_t) ((i + r) % per)] / pw - cv, 2.0); }
                best = std::min (best, std::sqrt (acc / per)); }
            if (best > worstWave) worstWave = best;
            std::printf ("        %-12s wave %.3f\n", PN[t], best);
        }
        char b[220];
        std::snprintf (b, sizeof b, "worst %.1f dB from the wavetable osc (%s); the NEAREST other table is %.1f dB away",
                       worstSpec, who.c_str(), bestOther);
        gate (worstSpec < bestOther * 0.5, "[1] THE TABLE SOUNDS LIKE THE TABLE", b);

        std::snprintf (b, sizeof b, "worst waveform difference over one period: %.3f (0 = the same wave)", worstWave);
        gate (worstWave < 0.45, "[2] THE PHASE CAME THROUGH — the WAVE matches, not just the spectrum", b);
    }

    // ── 3 · SPECTRAL DRIFT: the phase-only table must move ──────────────────────────────────
    {
        const WavetableSpec sp = WavetableBank::specForPreset (22);      // SpectralDrift
        const int n0 = HarmTableSource::resolve (sp, 0.0f, A,  P,  512);
        const int n1 = HarmTableSource::resolve (sp, 1.0f, A2, P2, 512);
        // its amplitudes are constant by construction — confirm, then show the SOUND still moves
        double ampDelta = 0; for (int j = 0; j < std::min (n0, n1); ++j) ampDelta = std::max (ampDelta, (double) std::fabs (A[j] - A2[j]));
        const auto o0 = renderHarm (tableParams (A,  P,  n0), f0, 777u, NREN);
        const auto o1 = renderHarm (tableParams (A2, P2, n1), f0, 777u, NREN);
        double num = 0, den = 0;
        for (int i = SKIP; i < NREN; ++i)
        { const double d = o0[(size_t) i] - o1[(size_t) i]; num += d*d; den += (double) o0[(size_t) i]*o0[(size_t) i]; }
        const double waveMove = den > 0 ? std::sqrt (num / den) : 0.0;
        char b[220];
        std::snprintf (b, sizeof b, "amplitudes move %.2e across the whole table (i.e. not at all); the SOUND moves %.3f",
                       ampDelta, waveMove);
        gate (ampDelta < 1e-6 && waveMove > 0.25,
              "[3] SPECTRAL DRIFT IS ALIVE — WT Pos works on a phase-only table", b);
    }

    // ── 4 · the 16-voice headroom ───────────────────────────────────────────────────────────
    {
        const WavetableSpec sp = WavetableBank::specForPreset (4);
        const int n = HarmTableSource::resolve (sp, 0.35f, A, P, 512);
        const HarmParams p = tableParams (A, P, n);
        const int NN = 2048;
        std::vector<float> sum ((size_t) NN, 0.f);
        double onePeak = 0;
        for (int u = 0; u < 16; ++u)
        { const auto s = renderHarm (p, f0, 1000u + (std::uint32_t) u * 7919u, NN);
          if (u == 0) onePeak = peakOf (s, 0, NN);
          for (int i = 0; i < NN; ++i) sum[(size_t) i] += s[(size_t) i]; }
        const double sumPeak = peakOf (sum, 0, NN);
        const double ratio = onePeak > 1e-9 ? sumPeak / onePeak : 0.0;
        char b[220];
        std::snprintf (b, sizeof b, "16 siblings sum to %.2fx one sibling's peak — coherent would be 16.0, incoherent ~%.1f",
                       ratio, std::sqrt (16.0));
        gate (ratio < 9.0, "[4] THE 16-VOICE HEADROOM HOLDS — the rotation decorrelates them", b);
    }

    // ── 5 · the six existing families still work ────────────────────────────────────────────
    {
        bool ok = true; std::string bad;
        for (int m = 0; m < 6; ++m)
        { HarmParams p; p.mainMode = m; p.count = 0.7f;
          const auto o = renderHarm (p, f0, 31u, 4096);
          const double r = rmsOf (o, 1024);
          if (! (r > 1e-4 && r < 4.0 && std::isfinite (r))) { ok = false; bad += std::to_string (m) + " "; } }
        gate (ok, "[5] THE SIX EXISTING FAMILIES STILL RENDER",
              ok ? "Blade Neon Console Chant Bronze Hornet all sane (bit-identity proven separately)"
                 : ("bad modes: " + bad));
    }

    // ── 6 · AN IMPORTED TABLE IS A LEGAL SOURCE ─────────────────────────────────────────────
    //  Max: "the same menu to select WT as well. Let's get it import, etc."
    //  This is the REAL import path, not a stand-in: raw PCM -> Wavetable::buildFromPcm (what a
    //  dropped file goes through) -> Wavetable::toSpec (what oscSourceSpec caches per import) ->
    //  the additive bank. If any link only accepted factory specs, this bar is where it shows.
    {
        // a synthetic "file": 16 windows that sweep a saw from 4 harmonics up to 40
        const int FR = 16, W = 2048;
        std::vector<float> pcm ((size_t) FR * W, 0.f);
        for (int f = 0; f < FR; ++f)
        {
            const int H = 4 + f * 2;
            for (int i = 0; i < W; ++i)
            { double v = 0;
              for (int h = 1; h <= H; ++h) v += std::sin (2.0 * M_PI * h * i / W) / h;
              pcm[(size_t) (f * W + i)] = (float) (v * 0.4); }
        }
        std::unique_ptr<Wavetable> w (new Wavetable());
        w->buildFromPcm (pcm.data(), FR * W, FR);
        const WavetableSpec imported = w->toSpec();
        const int n = HarmTableSource::resolve (imported, 0.60f, A, P, 512);
        const auto out = renderHarm (tableParams (A, P, n), f0, 909u, NREN);
        const auto oh  = harmsOf (out, f0, SKIP);

        // frame 9 of the sweep is a 22-harmonic saw; the bank should follow 1/h and then stop
        double pk = 0; for (int h = 1; h <= HM; ++h) pk = std::max (pk, oh[(size_t) h]);
        double worstTilt = 0; int topAudible = 0;
        for (int h = 1; h <= 20 && pk > 0; ++h)
        { const double want = 1.0 / h, got = oh[(size_t) h] / pk * (1.0 / (oh[1] / pk));
          worstTilt = std::max (worstTilt, std::fabs (20.0 * std::log10 (std::max (1e-6, got / want)))); }
        for (int h = 1; h <= HM; ++h) if (oh[(size_t) h] > pk * 0.02) topAudible = h;
        char b[220];
        std::snprintf (b, sizeof b, "n=%d partials; 1/h tilt holds to %.1f dB over h1-20, band ends at h%d",
                       n, worstTilt, topAudible);
        gate (n > 8 && worstTilt < 4.0 && topAudible >= 12 && topAudible <= 40,
              "[6] AN IMPORTED TABLE IS A LEGAL SOURCE — PCM -> buildFromPcm -> toSpec -> bank", b);
    }

    std::printf ("\n══ RESULT: %d pass, %d FAIL ══\n\n", gPass, gFail);
    return gFail == 0 ? 0 : 1;
}
