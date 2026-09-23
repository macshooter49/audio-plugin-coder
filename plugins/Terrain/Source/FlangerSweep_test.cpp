// ══════════════════════════════════════════════════════════════════════════════════════════════
//  FlangerSweep_test.cpp — tp91 · THE FLANGER LANE: THE DRAWN LINE IS THE SWEEP.
//
//  Max: "flanger is a whole different beast than a phaser and it deserves its own dedicated panel."
//  The lane lends the rack card's TerrainFlangerFx and, like ShaperBox's LiquidShaper, the drawn line IS
//  the comb's sweep (its Centre) — fed through setSweep(), which replaces the engine's own modulator.
//  This file proves the four things that makes true, on the SHIPPED engine, with the lane's own mapping
//  (md = 1 − 2·shape; the right channel md·(1 − 2·Stereo)):
//
//   [B] THE LINE MOVES THE COMB — the top of the drawing is the shortest delay (the highest notches), the
//       bottom the longest, and the swing is wide; Tape Zero's line sweeps THROUGH zero at its middle.
//   [C] A DRAWN GATE DOES NOT CLICK — the line stepping between its extremes every sixteenth puts no
//       jump in the output beyond what a smooth sweep of the same span does.
//   [D] TAPE ZERO BREATHES AT ZERO — held at the middle of the line the subtractive machine dips, and (fb634's
//       law) does not drop out.
//   [E] EVERY ONE OF THE 32 VOICINGS ANSWERS THE LINE — top vs bottom of the drawing changes its spectrum.
//
//  (The rack card is untouched by construction — it never calls setSweep — and that was proven at tp91
//   by a bit-exact null of this engine against its pre-tp91 self across all 48 voicings; see Tests/README.)
//
//  g++ -std=c++17 -O2 -ISource Source/FlangerSweep_test.cpp -o /tmp/flsweep && /tmp/flsweep
// ══════════════════════════════════════════════════════════════════════════════════════════════
#include "TerrainFlangerFx.h"
#include <cstdio>
#include <cmath>
#include <vector>
#include <string>
#include <algorithm>

static const double SR = 48000.0;
static int npass = 0, nfail = 0;
static void check (bool ok, const std::string& what)
{ std::printf ("  %s  %s\n", ok ? "PASS" : "FAIL", what.c_str()); if (ok) ++npass; else ++nfail; }

//  a deterministic white source (xorshift), so every run reads the same numbers
struct Noise { unsigned s = 0x1234567u; float next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return (float) ((double) s / 4294967296.0 * 2.0 - 1.0) * 0.3f; } };

static tw::TerrainFlangerFx::Params laneParams (int t, float fb = 0.5f, float centre = 0.5f, float range = 0.6f, float stereo = 0.0f)
{ tw::TerrainFlangerFx::Params p; p.type = t / 8; p.character = t % 8; p.feedback = fb; p.b1 = centre; p.depth = range; p.b2 = stereo; p.mix = 1.0f; return p; }   // the rest at the rack card's defaults, as the lane sets them

static std::vector<float> dryNoise (int n = (int) (SR * 3.0)) { Noise ns; std::vector<float> o; for (int i = 0; i < n; ++i) o.push_back (ns.next()); return o; }
//  render with the line HELD at `shape` (0..1), return the left channel after it settles
static std::vector<float> held (int t, float shape, float fb = 0.5f, int n = (int) (SR * 3.0))
{
    tw::TerrainFlangerFx e; e.prepare (SR, 512); e.setParams (laneParams (t, fb));
    Noise ns; std::vector<float> out; out.reserve ((size_t) n);
    const float md = 1.0f - 2.0f * shape;
    for (int i = 0; i < n; ++i) { float l = ns.next(), r = l; e.setSweep (md, md); e.processStereo (&l, &r, 1); out.push_back (l); }
    return out;
}
static double goertzel (const std::vector<float>& x, size_t a, size_t n, double f)
{ const double w = 2.0 * M_PI * f / SR, c = 2.0 * std::cos (w); double s1 = 0, s2 = 0;
  for (size_t i = a; i < a + n; ++i) { const double s0 = x[i] + c * s1 - s2; s2 = s1; s1 = s0; }
  return std::sqrt (std::max (0.0, s1 * s1 + s2 * s2 - c * s1 * s2)) / (double) n; }
//  the averaged magnitude spectrum on a log grid (32 frames of 4096 after the settle)
static std::vector<double> spectrum (const std::vector<float>& x, const std::vector<double>& fr)
{ std::vector<double> m (fr.size(), 0.0); const size_t F = 4096, a0 = (size_t) (SR * 0.3);
  for (int k = 0; k < 32; ++k) for (size_t b = 0; b < fr.size(); ++b) m[b] += goertzel (x, a0 + (size_t) k * F, F, fr[b]);
  return m; }
static std::vector<double> logGrid (double lo, double hi, int n)
{ std::vector<double> f; for (int i = 0; i < n; ++i) f.push_back (lo * std::pow (hi / lo, (double) i / (n - 1))); return f; }
//  the first notch of the COMB — wet over dry, so the source's own ripple and the engine's low cut divide out —
//  looked for from 150 Hz up: the lowest local minimum that sits well under the comb's peaks
[[maybe_unused]] static double firstNotch (const std::vector<double>& wet, const std::vector<double>& fr)
{ static const std::vector<double> dry = spectrum (dryNoise(), fr);
  std::vector<double> h (wet.size()); for (size_t b = 0; b < h.size(); ++b) h[b] = wet[b] / (dry[b] + 1e-12);
  double peak = 0; for (size_t b = 0; b < h.size(); ++b) if (fr[b] >= 150.0) peak = std::max (peak, h[b]);
  for (size_t b = 1; b + 1 < h.size(); ++b)
      if (fr[b] >= 150.0 && h[b] < h[b-1] && h[b] <= h[b+1] && h[b] < 0.35 * peak) return fr[b];
  return 0.0; }
static double rmsDb (const std::vector<float>& x, size_t a, size_t b)
{ double e = 0; for (size_t i = a; i < b; ++i) e += (double) x[i] * x[i]; return 10.0 * std::log10 (e / (double) (b - a) + 1e-30); }

// ── tp97 — PERCEPTUAL METRICS for the type-distinctness verdict (fb283 law: phase-independent, ear-correlated) ──
//    spectral centroid (brightness), magnitude-spectrum divergence between two spectra (dB), intrinsic spectral
//    flux on a held line (movement). These decided which flanger types the Shaper lane keeps.
static double centroidOf (const std::vector<double>& mag, const std::vector<double>& fr)
{ double num = 0, den = 0; for (size_t b = 0; b < fr.size(); ++b) { num += fr[b] * mag[b]; den += mag[b]; } return den > 0 ? num / den : 0; }
static double specDivDb (const std::vector<double>& A, const std::vector<double>& B)
{ double pk = 0; for (size_t b = 0; b < A.size(); ++b) pk = std::max (pk, std::max (A[b], B[b]));
  double s = 0; int c = 0; for (size_t b = 0; b < A.size(); ++b) if (std::max (A[b], B[b]) > 0.05 * pk) { s += std::fabs (20.0 * std::log10 ((A[b] + 1e-12) / (B[b] + 1e-12))); ++c; } return c ? s / c : 0; }
static double fluxHeld (int t, float shape, const std::vector<double>& fr)
{ const auto x = held (t, shape); const size_t F = 4096, hop = 2048; std::vector<double> prev; double acc = 0; int c = 0;
  for (size_t a = (size_t) (SR * 0.4); a + F < x.size(); a += hop)
  { std::vector<double> m (fr.size(), 0.0); for (size_t b = 0; b < fr.size(); ++b) m[b] = goertzel (x, a, F, fr[b]);
    double e = 0; for (double v : m) e += v * v; e = std::sqrt (e) + 1e-12; for (double& v : m) v /= e;
    if (! prev.empty()) { double d = 0; for (size_t b = 0; b < m.size(); ++b) { const double df = m[b] - prev[b]; d += df * df; } acc += std::sqrt (d); ++c; }
    prev = m; }
  return c ? acc / c : 0; }
// a type's magnitude-spectrum SIGNATURE: char 0, averaged over five positions of the drawn line (its coloration family)
static std::vector<double> typeSig (int type, const std::vector<double>& fr)
{ std::vector<double> S (fr.size(), 0.0); const float pos[5] = { 0.1f, 0.3f, 0.5f, 0.7f, 0.9f }; int n = 0;
  for (float sh : pos) { const auto m = spectrum (held (type * 8 + 0, sh), fr); for (size_t b = 0; b < S.size(); ++b) S[b] += m[b]; ++n; }
  for (auto& v : S) v /= n; return S; }

int main()
{
    std::printf ("\nFlangerSweep — the Flanger lane's drawn line, on the shipped TerrainFlangerFx\n\n");
    const std::vector<double> fr = logGrid (60.0, 16000.0, 400);

    // ── [B] THE LINE MOVES THE COMB — read as the engine's own comb delay (dbgComb, ms), the mechanism itself.
    //    ⚠️ a notch-finder on the spectrum was the first cut and it lied: Jet · Silver is an ADDITIVE comb, so its
    //    first notch at the bottom of the drawing sits at 1/(2Δ) = 117 Hz and the finder skipped to the second.
    auto combAt = [] (int t, float shape) -> double
    { tw::TerrainFlangerFx e; e.prepare (SR, 512); e.setParams (laneParams (t)); Noise ns; const float md = 1.0f - 2.0f * shape;
      for (int i = 0; i < (int) SR; ++i) { float l = ns.next(), r = l; e.setSweep (md, md); e.processStereo (&l, &r, 1); }
      return e.dbgComb(); };
    {
        const double lo = combAt (8, 0.0f), mid = combAt (8, 0.5f), hi = combAt (8, 1.0f);
        char b[260]; std::snprintf (b, sizeof b, "[B] THE LINE MOVES THE COMB: Jet · Silver's delay at the bottom / middle / top of the drawing = %.3f / %.3f / %.3f ms (the notches rise %.1fx)",
                                    lo, mid, hi, hi > 0 ? lo / hi : 0.0);
        check (lo > mid && mid > hi && lo / hi > 4.0, b);
        const double zlo = combAt (0, 0.0f), zmid = combAt (0, 0.5f), zhi = combAt (0, 1.0f);
        std::snprintf (b, sizeof b, "[B] TAPE ZERO SWEEPS THROUGH ZERO: |delay| at the bottom / middle / top = %.3f / %.3f / %.3f ms", zlo, zmid, zhi);
        check (zlo > 2.0 && zhi > 2.0 && zmid < 0.05, b);
    }
    // ── [C] A DRAWN GATE DOES NOT CLICK ──
    {
        auto run = [] (bool gate) -> double
        {   // a 220 Hz sine through Jet · Silver at +35 % feedback, the line a 1/16 gate or a sine of the same span
            tw::TerrainFlangerFx e; e.prepare (SR, 512); e.setParams (laneParams (8, 0.75f));
            const int n = (int) (SR * 3.0), step = (int) (SR * 0.125); double mx = 0; float prev = 0;
            for (int i = 0; i < n; ++i)
            { const double ph = (double) i / (double) (2 * step);
              const float shape = gate ? (((i / step) & 1) ? 1.0f : 0.0f) : (float) (0.5 - 0.5 * std::cos (2.0 * M_PI * ph));
              const float md = 1.0f - 2.0f * shape; e.setSweep (md, md);
              float l = 0.3f * (float) std::sin (2.0 * M_PI * 220.0 * i / SR), r = l; e.processStereo (&l, &r, 1);
              if (i > (int) (SR * 0.3)) mx = std::max (mx, (double) std::fabs (l - prev)); prev = l; }
            return mx;
        };
        const double g = run (true), s = run (false);
        char b[220]; std::snprintf (b, sizeof b, "[C] A DRAWN GATE DOES NOT CLICK: the largest sample step with the line gating every 1/16 is %.4f, against %.4f for a smooth sweep of the same span (%.2fx)",
                                    g, s, g / (s + 1e-12));
        check (g < 1.5 * s, b);
    }
    // ── [D] TAPE ZERO BREATHES AT ZERO — and does not drop out. fb634 (Max: "tape zero has some weird-ass
    //    clicks … fix that dropout") made the crossing a BREATH: the lag deck's weight rolls off inside the last
    //    few hundred µs, so at Δ = 0 the wet dips instead of vanishing. The drawn line parks there as long as the
    //    user draws it, so both halves matter here: a dip you hear, and no hole.
    {
        const auto zero = held (0, 0.5f), open = held (0, 0.0f);   // Tape Zero · Sub — the subtractive two-deck machine
        const double dz = rmsDb (zero, (size_t) (SR * 0.5), zero.size()), dop = rmsDb (open, (size_t) (SR * 0.5), open.size());
        char b[240]; std::snprintf (b, sizeof b, "[D] TAPE ZERO BREATHES AT ZERO: held at the middle of the line the machine reads %.1f dB against %.1f dB at the bottom — a %.1f dB dip, not a hole",
                                    dz, dop, dop - dz);
        check (dop - dz > 2.0 && dop - dz < 20.0, b);
    }
    // ── [E] EVERY VOICING ANSWERS THE LINE ──
    {
        int dead = 0; double weakest = 1e9; int weakT = -1;
        for (int t = 0; t < 32; ++t)
        {
            //  bottom against the MIDDLE and against the TOP, the larger: Tape Zero's top is its bottom's mirror (±Δ is
            //  the same comb), so for it the move that matters is toward the zero in the middle
            const auto a = spectrum (held (t, 0.1f), fr);
            double worst = 0;
            for (float other : { 0.5f, 0.9f })
            { const auto c = spectrum (held (t, other), fr);
              double pk = 0; for (size_t q = 0; q < a.size(); ++q) pk = std::max (pk, std::max (a[q], c[q]));
              for (size_t q = 0; q < a.size(); ++q) if (std::max (a[q], c[q]) > 0.05 * pk)
                  worst = std::max (worst, std::fabs (20.0 * std::log10 ((a[q] + 1e-12) / (c[q] + 1e-12)))); }
            if (worst < 6.0) ++dead;
            if (worst < weakest) { weakest = worst; weakT = t; }
        }
        char b[200]; std::snprintf (b, sizeof b, "[E] EVERY ONE OF THE 32 VOICINGS ANSWERS THE LINE: %d move less than 6 dB between the top and bottom of the drawing (weakest: voicing %d, %.1f dB)",
                                    dead, weakT, weakest);
        check (dead == 0, b);
    }
    // ── [F] tp97 — PERCEPTUAL TYPE-DISTINCTNESS: which of the four engine types are night-and-day on THIS lane,
    //    where the drawn line REPLACES the modulator. Metrics per the fb283 law (phase-independent): magnitude-
    //    spectrum divergence between types, spectral centroid (brightness), intrinsic spectral flux (movement).
    //    VERDICT: the lane keeps only the distinct pair (Jet, BBD); Tape Zero and Endless were removed.
    {
        const char* const nm[4] = { "Tape Zero", "Jet", "BBD", "Endless" };
        std::printf ("  ── [F] flanger TYPE perceptual comparison (char 0, dry white noise) ──\n");
        std::vector<std::vector<double>> sig (4); double cen[4], fl[4];
        for (int t = 0; t < 4; ++t) { sig[t] = typeSig (t, fr); cen[t] = centroidOf (sig[t], fr); fl[t] = fluxHeld (t * 8 + 0, 0.3f, fr);
            std::printf ("      %-10s  centroid %6.0f Hz   intrinsic flux %.3f\n", nm[t], cen[t], fl[t]); }
        const double jetBbd = specDivDb (sig[1], sig[2]);    // the two KEPT types
        const double tzJet  = specDivDb (sig[0], sig[1]);    // removed: Tape Zero ~ Jet
        const double endJet = specDivDb (sig[3], sig[1]);    // removed: Endless  ~ Jet
        char b[320];
        std::snprintf (b, sizeof b, "[F] THE KEPT TYPES ARE NIGHT-AND-DAY: Jet vs BBD = %.2f dB magnitude-spectrum divergence (centroid %.0f vs %.0f Hz) — a different instrument, not a variant",
                       jetBbd, cen[1], cen[2]);
        check (jetBbd > 6.0, b);
        std::snprintf (b, sizeof b, "[F] THE REMOVED TYPES WERE NEAR-DUPLICATES OF JET: Tape Zero %.2f dB, Endless %.2f dB (both < 3 dB — the drawn line replaces the modulator that made them distinct; Tape Zero also carries fb634 crackle)",
                       tzJet, endJet);
        check (tzJet < 3.0 && endJet < 3.0, b);
    }
    std::printf ("\n%d checks, %d failed\n%s\n", npass + nfail, nfail, nfail ? "SOME CHECKS FAILED" : "ALL CHECKS PASSED");
    return nfail ? 1 : 0;
}
