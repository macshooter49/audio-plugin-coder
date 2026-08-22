// ─────────────────────────────────────────────────────────────────────────────
// utl_cert — the certification harness for the UTILITY strip (kind 14), fb450: THE CHANNEL STRIP.
//   clang++ -O2 -std=c++17 -I Tests/shim -I Source Tests/utl_cert.cpp -o utl_cert
//
// THE LAW THIS HARNESS EXISTS FOR (Max, fb450): "the next time I load up utility I need to hear a damn
// near change" — every knob and every lamp on this card makes an AUDIBLE difference, ON A MONO SOURCE,
// at its first increment and at its end. fb444–448 shipped eight Characters, six Wirings and a desk of
// M/S controls that were all "different" by a metric and inaudible on the patches Max actually plays
// (mono oscillators), and a Drive whose exact-1/drive makeup put the top of the knob 21 dB DOWN.
// §E is that law, gated knob by knob on a MONO saw. §F is each control's own physics. §A is still
// first (a swapped or inverted strip builds clean and is the wrong device).
//
// fb441 — CERT MUST SEED BEFORE IT MEASURES: every capture runs a seed interval first.
// fb444 — THE METRIC: max |Δ| per octave over L / R / M / S — an ear notices the octave that changed.
// ─────────────────────────────────────────────────────────────────────────────
#include "TerrainUtilityFx.h"
#include <cstdio>
#include <cstdint>
#include <complex>
#include <string>
#include <vector>
#include <cmath>
#include <functional>

static int gPass = 0, gFail = 0;
static void gate (const char* what, bool ok, const std::string& d)
{
    if (ok) { ++gPass; std::printf ("  ok    %-62s %s\n", what, d.c_str()); }
    else    { ++gFail; std::printf ("  FAIL  %-62s %s\n", what, d.c_str()); }
}
static std::string f1 (double v) { char b[48]; std::snprintf (b, sizeof b, "%.1f", v); return b; }
static std::string f2 (double v) { char b[48]; std::snprintf (b, sizeof b, "%.2f", v); return b; }

typedef std::vector<std::complex<double>> CVec;
static void fft (CVec& a)
{
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i) { size_t bit = n >> 1; for (; j & bit; bit >>= 1) j ^= bit; j ^= bit; if (i < j) std::swap (a[i], a[j]); }
    for (size_t len = 2; len <= n; len <<= 1)
    {
        const double ang = -2.0 * M_PI / (double) len; const std::complex<double> wl (std::cos (ang), std::sin (ang));
        for (size_t i = 0; i < n; i += len) { std::complex<double> w (1.0, 0.0);
            for (size_t j = 0; j < len / 2; ++j) { const auto u = a[i + j], v = a[i + j + len / 2] * w; a[i + j] = u + v; a[i + j + len / 2] = u - v; w *= wl; } }
    }
}
static constexpr int   NFFT = 16384;
static float FS = 48000.0f;
static constexpr float BUS  = 0.05f;     // the rack bus sits near -26 dBFS (fb313)
using E = tw::TerrainUtilityFx;
using P = tw::TerrainUtilityFx::Params;

static double db (double x) { return 20.0 * std::log10 (std::max (1e-12, x)); }
static double binHz()       { return (double) FS / NFFT; }
static CVec cspec (const std::vector<float>& v)
{
    CVec b ((size_t) NFFT);
    for (int i = 0; i < NFFT; ++i) { const double w = 0.5 - 0.5 * std::cos (2.0 * M_PI * i / (NFFT - 1)); b[(size_t) i] = (i < (int) v.size() ? (double) v[(size_t) i] : 0.0) * w; }
    fft (b); b.resize ((size_t) NFFT / 2); return b;
}
static double energyNear (const CVec& s, double hz, double widthHz = 30.0)
{
    const int lo = (int) std::max (0.0, (hz - widthHz) / binHz()), hi = (int) std::min ((double) s.size() - 1, (hz + widthHz) / binHz());
    double e = 0.0; for (int i = lo; i <= hi; ++i) e += std::norm (s[(size_t) i]); return std::sqrt (e);
}
static double bandEnergy (const CVec& s, double lo, double hi)
{
    const int i0 = (int) (lo / binHz()), i1 = (int) std::min ((double) s.size() - 1, hi / binHz());
    double e = 0.0; for (int i = i0; i <= i1; ++i) e += std::norm (s[(size_t) i]); return std::sqrt (e / std::max (1, i1 - i0 + 1));
}
static double bandCorr (const CVec& L, const CVec& R, double lo, double hi)
{
    const int i0 = (int) (lo / binHz()), i1 = (int) (hi / binHz()); double num = 0.0, dl = 0.0, dr = 0.0;
    for (int i = i0; i <= i1 && i < (int) L.size(); ++i) { num += (L[(size_t) i] * std::conj (R[(size_t) i])).real(); dl += std::norm (L[(size_t) i]); dr += std::norm (R[(size_t) i]); }
    return num / std::sqrt (std::max (1e-30, dl * dr));
}
static double rms (const std::vector<float>& v) { double a = 0.0; for (float x : v) a += (double) x * x; return std::sqrt (a / std::max<size_t> (1, v.size())); }
static double peak (const std::vector<float>& v) { double p = 0.0; for (float x : v) p = std::max (p, (double) std::fabs (x)); return p; }

// ── sources. A MONO SAW at the bus is the one that matters (Max's patches); a decorrelated noise pair for
//    the stereo laws; two different tones for "which channel did this come out of".
struct Noise { uint32_t a = 0x1234567u, b = 0x89abcdefu; void reset() { a = 0x1234567u; b = 0x89abcdefu; }
    float l() { a = a * 1664525u + 1013904223u; return ((float) (a >> 8) / 16777216.0f * 2.0f - 1.0f) * BUS * 0.8f; }
    float r() { b = b * 1664525u + 1013904223u; return ((float) (b >> 8) / 16777216.0f * 2.0f - 1.0f) * BUS * 0.8f; } };
struct Cap { std::vector<float> inL, inR, L, R; };
typedef std::function<std::pair<float,float> (int)> Gen;
static Cap capture (E& e, Gen gen, int n, double seedSec = 0.30)
{
    Cap c; const int seed = (int) (seedSec * FS); int i = 0;
    for (int k = 0; k < seed; ++k, ++i) { auto s = gen (i); float l, r; e.processStereo (s.first, s.second, l, r); }
    for (int k = 0; k < n; ++k, ++i) { auto s = gen (i); float l, r; e.processStereo (s.first, s.second, l, r); c.inL.push_back (s.first); c.inR.push_back (s.second); c.L.push_back (l); c.R.push_back (r); }
    return c;
}
// a 55 Hz saw, 300 partials (to 16.5 kHz), mono, at the bus: sub to air, so every band-limited control has
// something to act on — the honest source for "audible on MONO" (a patch with no sub cannot hear a 40 Hz cut)
static std::vector<float> gSaw;
static std::pair<float,float> monoSaw (int i)
{
    if (gSaw.empty() || (int) gSaw.size() != (int) std::lround (FS / 55.0))
    {   const int per = (int) std::lround (FS / 55.0); gSaw.assign ((size_t) per, 0.0f);
        for (int k = 0; k < per; ++k) { double v = 0.0; for (int h = 1; h <= 300; ++h) { if (55.0 * h > 0.45 * FS) break; v += std::sin (2.0 * M_PI * h * k / per) / h; } gSaw[(size_t) k] = (float) (v * 0.55 * BUS); } }
    const float x = gSaw[(size_t) (i % (int) gSaw.size())]; return { x, x };
}
static std::pair<float,float> twoTone (int i) { return { (float) (BUS * std::sin (2.0 * M_PI * 440.0 * i / FS)), (float) (BUS * std::sin (2.0 * M_PI * 1000.0 * i / FS)) }; }
static std::pair<float,float> monoTone (int i) { const float x = (float) (BUS * std::sin (2.0 * M_PI * 440.0 * i / FS)); return { x, x }; }
static Noise gNoise; static std::pair<float,float> noisePair (int) { return { gNoise.l(), gNoise.r() }; }
static E fresh (const P& p) { E e; e.prepare (FS); e.setParams (p); return e; }

// the fingerprint: FOUR spectra (L, R, M, S), the octave-max |Δ| metric
static std::vector<CVec> spectra4 (const Cap& c)
{
    std::vector<float> M (c.L.size()), Sd (c.L.size());
    for (size_t i = 0; i < c.L.size(); ++i) { M[i] = 0.5f * (c.L[i] + c.R[i]); Sd[i] = 0.5f * (c.L[i] - c.R[i]); }
    return { cspec (c.L), cspec (c.R), cspec (M), cspec (Sd) };
}
static double octaveMaxDelta (const std::vector<CVec>& a, const std::vector<CVec>& b)
{
    double worst = 0.0;
    for (int k = 0; k < 4; ++k) for (double lo = 40.0; lo < 16000.0; lo *= 2.0)
    {
        const double ea = bandEnergy (a[(size_t) k], lo, lo * 2.0), eb = bandEnergy (b[(size_t) k], lo, lo * 2.0);
        if (ea < 1e-7 && eb < 1e-7) continue;            // both empty: nothing to compare (the side of a mono source)
        worst = std::max (worst, std::fabs (db (ea) - db (eb)));
    }
    return worst;
}
static double thd (const std::vector<float>& v, double f0)   // harmonics 2..9 over the fundamental, dB
{
    const CVec s = cspec (v); double h = 0.0; for (int k = 2; k <= 9; ++k) h += std::pow (energyNear (s, f0 * k, 20.0), 2.0);
    return db (std::sqrt (h) / std::max (1e-12, energyNear (s, f0, 20.0)));
}
static int lagOfPeakXcorr (const std::vector<float>& a, const std::vector<float>& b, int maxLag)   // lag where a ≈ b delayed
{
    int best = 0; double bv = -1e300;
    for (int lag = -maxLag; lag <= maxLag; ++lag) { double acc = 0.0; for (size_t i = (size_t) std::max (0, lag); i + (size_t) std::max (0, -lag) < a.size(); ++i) acc += (double) a[i] * b[i - (size_t) lag]; if (acc > bv) { bv = acc; best = lag; } }
    return best;
}

int main()
{
    std::printf ("\n══ utl_cert — THE UTILITY STRIP (fb450: a channel strip, every control audible on MONO) ══\n");
    const int N = NFFT;

    // ═══════════════════════════════════════════════════════════════════════
    std::printf ("\n§A — TRANSPARENCY, THE MATRIX AND POLARITY. Run FIRST.\n");
    {
        { E e = fresh (P {}); Cap c = capture (e, twoTone, N); double md = 0.0; for (size_t i = 0; i < c.L.size(); ++i) md = std::max (md, (double) std::max (std::fabs (c.L[i] - c.inL[i]), std::fabs (c.R[i] - c.inR[i])));
          gate ("the DEFAULT strip is BIT-EXACT transparent (max |out-in|)", md == 0.0, "max=" + std::to_string (md)); }
        { E e = fresh (P {}); Cap c = capture (e, twoTone, N); const CVec L = cspec (c.L), R = cspec (c.R);
          gate ("Through: 440 stays LEFT, 1 k stays RIGHT (which tone came out where is a fact)", energyNear (L, 440) > 10 * energyNear (L, 1000) && energyNear (R, 1000) > 10 * energyNear (R, 440), ""); }
        { P p; p.flipL = true; E e = fresh (p); Cap c = capture (e, twoTone, N); double md = 0.0, mr = 0.0; for (size_t i = 0; i < c.L.size(); ++i) { md = std::max (md, (double) std::fabs (c.L[i] + c.inL[i])); mr = std::max (mr, (double) std::fabs (c.R[i] - c.inR[i])); }
          gate ("Flip L: out_L == -in_L EXACTLY, R untouched (polarity is a sign, measured)", md < 1e-6 && mr == 0.0, "L " + std::to_string (md) + " R " + std::to_string (mr)); }
        { P p; p.flipR = true; E e = fresh (p); Cap c = capture (e, twoTone, N); double md = 0.0; for (size_t i = 0; i < c.L.size(); ++i) md = std::max (md, (double) std::fabs (c.R[i] + c.inR[i]));
          gate ("Flip R: out_R == -in_R EXACTLY", md < 1e-6, std::to_string (md)); }
        { P p; p.swap = true; E e = fresh (p); Cap c = capture (e, twoTone, N); const CVec L = cspec (c.L), R = cspec (c.R);
          gate ("Swap: 440 comes out RIGHT, 1 k comes out LEFT", energyNear (R, 440) > 10 * energyNear (R, 1000) && energyNear (L, 1000) > 10 * energyNear (L, 440), ""); }
        { P p; p.swap = true; p.steer = 0.0f; E e = fresh (p); Cap c = capture (e, monoTone, N);
          gate ("Swap is AT THE OUTPUT: Pan hard left + Swap puts the mono tone on the RIGHT (a swap at the input would be nothing)", rms (c.R) > 10.0 * rms (c.L) && rms (c.R) > 0.5 * BUS, "L " + f1 (db (rms (c.L))) + " dB  R " + f1 (db (rms (c.R))) + " dB"); }
        { P p; p.sum = true; E e = fresh (p); Cap c = capture (e, twoTone, N); double md = 0.0; for (size_t i = 0; i < c.L.size(); ++i) md = std::max (md, (double) std::fabs (c.L[i] - c.R[i]));
          gate ("Mono: L == R EXACTLY (the sum is a sum)", md < 1e-6, std::to_string (md)); }
    }

    // ═══════════════════════════════════════════════════════════════════════
    std::printf ("\n§B — GAIN. The one control that justifies the whole device.\n");
    {
        auto refDb = [] (float t) { if (t <= 0.0f) return -999.0; if (std::fabs (t - 2.0f / 3.0f) < 1e-4f) return 0.0; return (double) (-60.0f + 90.0f * t); };
        bool ok = true; std::string d;
        for (float t : { 0.2f, 0.4f, 2.0f / 3.0f, 0.8f, 1.0f }) { P p; p.gain = t; E e = fresh (p); Cap c = capture (e, monoTone, N); const double g = db (rms (c.L) / rms (c.inL)); if (std::fabs (g - refDb (t)) > 0.1) ok = false; d += f1 (g) + " "; }
        gate ("the fader law matches an INDEPENDENT recomputation at 5 positions (±0.1 dB)", ok, d);
        { P p; p.gain = 2.0f / 3.0f; E e = fresh (p); Cap c = capture (e, monoTone, N); double md = 0.0; for (size_t i = 0; i < c.L.size(); ++i) md = std::max (md, (double) std::fabs (c.L[i] - c.inL[i])); gate ("unity (2/3) is EXACTLY 0 dB — bit-exact", md == 0.0, std::to_string (md)); }
        { P p; p.gain = 0.0f; E e = fresh (p); Cap c = capture (e, monoTone, N, 0.5); gate ("Gain 0 is SILENCE (a true, glided -inf)", peak (c.L) == 0.0, std::to_string (peak (c.L))); }
        { P p; p.dim = true; E e = fresh (p); Cap c = capture (e, monoTone, N); const double g = db (rms (c.L) / rms (c.inL)); gate ("Dim = exactly -20 dB", std::fabs (g + 20.0) < 0.1, f1 (g) + " dB"); }
    }

    // ═══════════════════════════════════════════════════════════════════════
    std::printf ("\n§C — THE BOUNDED-WIDTH LAW (the contrast with Widen R11) and THE TYPES.\n");
    {
        { P p; p.image = 1.0f; E e = fresh (p); gNoise.reset(); Cap c = capture (e, noisePair, N); std::vector<float> M (c.L.size()), Mi (c.L.size()); for (size_t i = 0; i < M.size(); ++i) { M[i] = 0.5f * (c.L[i] + c.R[i]); Mi[i] = 0.5f * (c.inL[i] + c.inR[i]); }
          gate ("Width 300 %: the mono fold-down SURVIVES — mid gain ≥ -7 dB (never zero; Widen's cert demands the opposite)", db (rms (M) / rms (Mi)) > -7.0, "mid " + f1 (db (rms (M) / rms (Mi))) + " dB"); }
        { P p; p.image = 0.0f; E e = fresh (p); gNoise.reset(); Cap c = capture (e, noisePair, N); double md = 0.0; for (size_t i = 0; i < c.L.size(); ++i) md = std::max (md, (double) std::fabs (c.L[i] - c.R[i])); gate ("Width 0 % is MONO (L == R to 1e-4)", md < 1e-4, std::to_string (md)); }
        // every Type is a different image LAW, measured pairwise on a WIDE source
        std::vector<std::vector<CVec>> fps; for (int t = 0; t < E::kNumTypes; ++t) { P p; p.type = t; p.image = 0.9f; E e = fresh (p); gNoise.reset(); fps.push_back (spectra4 (capture (e, noisePair, N))); }
        double worst = 1e9; std::string d;
        for (int a = 0; a < E::kNumTypes; ++a) for (int b = a + 1; b < E::kNumTypes; ++b) { const double x = octaveMaxDelta (fps[(size_t) a], fps[(size_t) b]); worst = std::min (worst, x); }
        gate ("every Type is a different image LAW, measured pairwise (min over pairs > 1.5 dB)", worst > 1.5, "min pair Δ = " + f1 (worst) + " dB");
        { P p; p.type = 1; p.image = 0.95f; E e = fresh (p); Cap c = capture (e, monoSaw, N); std::vector<float> Sd (c.L.size()); for (size_t i = 0; i < Sd.size(); ++i) Sd[i] = 0.5f * (c.L[i] - c.R[i]);
          gate ("'Turn' widens a MONO source (manufactures side); 'Strip' provably cannot", db (rms (Sd)) > -60.0, "side " + f1 (db (rms (Sd))) + " dBFS"); }
    }

    // ═══════════════════════════════════════════════════════════════════════
    std::printf ("\n§D — MONO BELOW. Below the corner it is mono; above it the stereo survives; the MID is untouched.\n");
    {
        { P p; p.monoBelow = 0.55f; E e = fresh (p); gNoise.reset(); Cap c = capture (e, noisePair, N); const CVec L = cspec (c.L), R = cspec (c.R); const double hz = E::monoBelowHzFor (0.55f);
          gate ("corr below the corner > 0.9, above 2x the corner < 0.5", bandCorr (L, R, 40.0, hz * 0.5) > 0.9 && bandCorr (L, R, hz * 2.0, 12000.0) < 0.5, "corner " + f1 (hz) + " Hz  lo " + f2 (bandCorr (L, R, 40.0, hz * 0.5)) + "  hi " + f2 (bandCorr (L, R, hz * 2.0, 12000.0))); }
        { P p; p.monoBelow = 0.8f; E e = fresh (p); Cap c = capture (e, monoSaw, N); double md = 0.0; for (size_t i = 0; i < c.L.size(); ++i) md = std::max (md, (double) std::fabs (c.L[i] - c.inL[i]));
          gate ("on a MONO source Mono Below is EXACTLY nothing (side-only by construction — the documented exception)", md < 1e-5, std::to_string (md)); }
        { P p; p.monoBelow = 0.0f; E e = fresh (p); gNoise.reset(); Cap c = capture (e, noisePair, N); double md = 0.0; for (size_t i = 0; i < c.L.size(); ++i) md = std::max (md, (double) std::fabs (c.L[i] - c.inL[i])); gate ("Mono Below at the detent is OFF, bit-exact", md == 0.0, std::to_string (md)); }
    }

    // ═══════════════════════════════════════════════════════════════════════
    std::printf ("\n§E — THE LAW: EVERY KNOB IS AUDIBLE ON A MONO SOURCE, at its FIRST increment and at its END.\n"
                 "      (octave-max |Δ| over L/R/M/S against the default strip; a 110 Hz saw at the bus)\n");
    {
        const std::vector<CVec> ref = [&] { E e = fresh (P {}); return spectra4 (capture (e, monoSaw, N)); }();
        struct K { const char* nm; std::function<void (P&, float)> set; float first, last; double barFirst, barLast; };
        const K knobs[] = {
            { "High Pass",  [] (P& p, float v) { p.hp = v; },        0.02f, 1.0f, 1.0, 12.0 },
            { "Low Pass",   [] (P& p, float v) { p.lp = v; },        0.98f, 0.0f, 1.0, 12.0 },
            { "Bass",       [] (P& p, float v) { p.bass = v; },      0.58f, 0.0f, 1.0,  8.0 },
            { "Air",        [] (P& p, float v) { p.air = v; },       0.58f, 1.0f, 1.0,  8.0 },
            { "Rotate",     [] (P& p, float v) { p.rotate = v; },    0.58f, 1.0f, 1.0,  6.0 },
            { "Haas",       [] (P& p, float v) { p.haas = v; },      0.58f, 1.0f, 1.0,  6.0 },
            { "Drive",      [] (P& p, float v) { p.drive = v; },     0.30f, 1.0f, 1.0,  8.0 } };   // a saw is already all harmonics: the THD and loudness gates in §F carry the rest
        for (const K& k : knobs)
        {
            P a; k.set (a, k.first); E ea = fresh (a); const double dF = octaveMaxDelta (ref, spectra4 (capture (ea, monoSaw, N)));
            P b; k.set (b, k.last);  E eb = fresh (b); const double dL = octaveMaxDelta (ref, spectra4 (capture (eb, monoSaw, N)));
            gate ((std::string (k.nm) + ": audible on MONO at its first increment and dramatic at its end").c_str(), dF >= k.barFirst && dL >= k.barLast,
                  "first " + f1 (dF) + " dB (bar " + f1 (k.barFirst) + ")  end " + f1 (dL) + " dB (bar " + f1 (k.barLast) + ")");
        }
        { P p; p.drive = 0.04f; E e = fresh (p); Cap c = capture (e, monoTone, N); E r = fresh (P {}); Cap d = capture (r, monoTone, N);
          gate ("Drive's FIRST increment (+13 dB into the tanh) already makes harmonics on a tone (THD up ≥ 20 dB over the dry)", thd (c.L, 440.0) > thd (d.L, 440.0) + 20.0, "dry " + f1 (thd (d.L, 440.0)) + " → " + f1 (thd (c.L, 440.0)) + " dB THD"); }
        // the lamps, on MONO: Flip L / Flip R change the image (L ≠ R) — Swap on a PANNED mono source is the whole image
        { P p; p.flipL = true; E e = fresh (p); Cap c = capture (e, monoSaw, N); std::vector<float> Sd (c.L.size()); for (size_t i = 0; i < Sd.size(); ++i) Sd[i] = 0.5f * (c.L[i] - c.R[i]); gate ("Flip L on MONO: the side goes from nothing to the whole signal (audible, by physics)", db (rms (Sd)) > db (rms (c.L)) - 1.0, f1 (db (rms (Sd))) + " dBFS side"); }
        { P p; p.steer = 0.15f; E e0 = fresh (p); Cap c0 = capture (e0, monoSaw, N); p.swap = true; E e1 = fresh (p); Cap c1 = capture (e1, monoSaw, N);
          gate ("Swap on a panned MONO source moves the image to the other side (> 10 dB per channel)", std::fabs (db (rms (c1.L)) - db (rms (c0.L))) > 10.0 && std::fabs (db (rms (c1.R)) - db (rms (c0.R))) > 10.0, "L " + f1 (db (rms (c0.L))) + "→" + f1 (db (rms (c1.L))) + "  R " + f1 (db (rms (c0.R))) + "→" + f1 (db (rms (c1.R)))); }
    }

    // ═══════════════════════════════════════════════════════════════════════
    std::printf ("\n§F — EACH CONTROL'S OWN PHYSICS.\n");
    {
        // High Pass: 2 poles — two octaves below the corner ≥ 20 dB down, an octave above within 2 dB
        { P p; p.hp = 0.5f; const double fc = E::hpHzFor (0.5f); E e = fresh (p); gNoise.reset(); Cap c = capture (e, noisePair, N); gNoise.reset(); E r = fresh (P {}); Cap d = capture (r, noisePair, N);
          const CVec A = cspec (c.L), B = cspec (d.L); const double lo = db (bandEnergy (A, fc * 0.22, fc * 0.28) / bandEnergy (B, fc * 0.22, fc * 0.28)), hi = db (bandEnergy (A, fc * 2.0, fc * 2.4) / bandEnergy (B, fc * 2.0, fc * 2.4));
          gate ("High Pass is 12 dB/oct: 2 octaves below the corner ≥ 20 dB down, an octave above within 2 dB", lo < -20.0 && std::fabs (hi) < 2.0, "corner " + f1 (fc) + "  -2oct " + f1 (lo) + " dB  +1oct " + f1 (hi) + " dB"); }
        { P p; p.lp = 0.5f; const double fc = E::lpHzFor (0.5f); E e = fresh (p); gNoise.reset(); Cap c = capture (e, noisePair, N); gNoise.reset(); E r = fresh (P {}); Cap d = capture (r, noisePair, N);
          const CVec A = cspec (c.L), B = cspec (d.L); const double hi = db (bandEnergy (A, fc * 3.6, fc * 4.4) / bandEnergy (B, fc * 3.6, fc * 4.4)), lo = db (bandEnergy (A, fc * 0.4, fc * 0.5) / bandEnergy (B, fc * 0.4, fc * 0.5));
          gate ("Low Pass is 12 dB/oct: 2 octaves above the corner ≥ 20 dB down, an octave below within 2 dB", hi < -20.0 && std::fabs (lo) < 2.0, "corner " + f1 (fc) + "  +2oct " + f1 (hi) + " dB  -1oct " + f1 (lo) + " dB"); }
        { P p; p.hp = 0.0f; p.lp = 1.0f; E e = fresh (p); gNoise.reset(); Cap c = capture (e, noisePair, N); double md = 0.0; for (size_t i = 0; i < c.L.size(); ++i) md = std::max (md, (double) std::fabs (c.L[i] - c.inL[i])); gate ("High Pass at 0 and Low Pass at 1 are OFF, bit-exact", md == 0.0, std::to_string (md)); }
        // the shelves: ±12 dB at the extremes, measured a decade away from the corner; 0 dB at centre bit-exact
        { P p; p.bass = 1.0f; E e = fresh (p); gNoise.reset(); Cap c = capture (e, noisePair, N); gNoise.reset(); E r = fresh (P {}); Cap d = capture (r, noisePair, N); const double g = db (bandEnergy (cspec (c.L), 25.0, 40.0) / bandEnergy (cspec (d.L), 25.0, 40.0));
          P q; q.bass = 0.0f; E e2 = fresh (q); gNoise.reset(); Cap c2 = capture (e2, noisePair, N); const double g2 = db (bandEnergy (cspec (c2.L), 25.0, 40.0) / bandEnergy (cspec (d.L), 25.0, 40.0));
          gate ("Bass: +12 / -12 dB at the ends, measured below the shelf (±1.5)", std::fabs (g - 12.0) < 1.5 && std::fabs (g2 + 12.0) < 1.5, f1 (g) + " / " + f1 (g2) + " dB"); }
        { P p; p.air = 1.0f; E e = fresh (p); gNoise.reset(); Cap c = capture (e, noisePair, N); gNoise.reset(); E r = fresh (P {}); Cap d = capture (r, noisePair, N); const double g = db (bandEnergy (cspec (c.L), 15000.0, 20000.0) / bandEnergy (cspec (d.L), 15000.0, 20000.0));
          P q; q.air = 0.0f; E e2 = fresh (q); gNoise.reset(); Cap c2 = capture (e2, noisePair, N); const double g2 = db (bandEnergy (cspec (c2.L), 15000.0, 20000.0) / bandEnergy (cspec (d.L), 15000.0, 20000.0));
          gate ("Air: +12 / -12 dB at the ends, measured above the shelf (±1.5)", std::fabs (g - 12.0) < 1.5 && std::fabs (g2 + 12.0) < 1.5, f1 (g) + " / " + f1 (g2) + " dB"); }
        { P p; p.bass = 0.5f; p.air = 0.5f; E e = fresh (p); gNoise.reset(); Cap c = capture (e, noisePair, N); double md = 0.0; for (size_t i = 0; i < c.L.size(); ++i) md = std::max (md, (double) std::fabs (c.L[i] - c.inL[i])); gate ("Bass and Air at centre are 0 dB, bit-exact", md == 0.0, std::to_string (md)); }
        // Haas: one channel delayed by EXACTLY the asked samples, the other bit-exact; centre bit-exact
        { P p; p.haas = 0.75f; const double ms = E::haasMsFor (0.75f); const int want = (int) std::lround (ms * 0.001 * FS); E e = fresh (p); gNoise.reset(); Cap c = capture (e, noisePair, N, 0.6);
          double mr = 0.0; for (size_t i = 0; i < c.R.size(); ++i) mr = std::max (mr, (double) std::fabs (c.R[i] - c.inR[i])); const int lag = lagOfPeakXcorr (c.L, c.inL, want + 40);
          gate ("Haas right of centre delays the LEFT by exactly the asked samples; the right is bit-exact", std::abs (lag - want) <= 1 && mr == 0.0, "want " + std::to_string (want) + " got " + std::to_string (lag) + "  R diff " + std::to_string (mr)); }
        { P p; p.haas = 0.25f; const int want = (int) std::lround (std::fabs (E::haasMsFor (0.25f)) * 0.001 * FS); E e = fresh (p); gNoise.reset(); Cap c = capture (e, noisePair, N, 0.6); double ml = 0.0; for (size_t i = 0; i < c.L.size(); ++i) ml = std::max (ml, (double) std::fabs (c.L[i] - c.inL[i])); const int lag = lagOfPeakXcorr (c.R, c.inR, want + 40);
          gate ("Haas left of centre delays the RIGHT; the left is bit-exact", std::abs (lag - want) <= 1 && ml == 0.0, "want " + std::to_string (want) + " got " + std::to_string (lag)); }
        { P p; p.haas = 0.5f; E e = fresh (p); gNoise.reset(); Cap c = capture (e, noisePair, N); double md = 0.0; for (size_t i = 0; i < c.L.size(); ++i) md = std::max (md, (double) std::max (std::fabs (c.L[i] - c.inL[i]), std::fabs (c.R[i] - c.inR[i]))); gate ("Haas at centre is OFF, bit-exact", md == 0.0, std::to_string (md)); }
        // Rotate: manufactures side from mono, bounded (the mid never dies), bipolar sign
        { P p; p.rotate = 1.0f; E e = fresh (p); Cap c = capture (e, monoSaw, N); std::vector<float> M (c.L.size()), Sd (c.L.size()); for (size_t i = 0; i < M.size(); ++i) { M[i] = 0.5f * (c.L[i] + c.R[i]); Sd[i] = 0.5f * (c.L[i] - c.R[i]); }
          gate ("Rotate +40° on MONO: side appears (> -10 dB re mid) and the mid survives (> -3 dB)", db (rms (Sd)) - db (rms (M)) > -10.0 && db (rms (M)) > db (rms (c.inL)) - 3.0, "side-mid " + f1 (db (rms (Sd)) - db (rms (M))) + " dB  mid " + f1 (db (rms (M)) - db (rms (c.inL))) + " dB"); }
        { P a; a.rotate = 1.0f; P b; b.rotate = 0.0f; E ea = fresh (a); E eb = fresh (b); Cap ca = capture (ea, monoSaw, N), cb = capture (eb, monoSaw, N); double s = 0.0; for (size_t i = 0; i < ca.L.size(); ++i) s += (double) (ca.L[i] - ca.R[i]) * (cb.L[i] - cb.R[i]);
          gate ("Rotate is bipolar: +40° and -40° put the side in OPPOSITE polarity", s < 0.0, ""); }
        // Drive: LOUDER AND DENSER, never quieter — RMS at the bus holds or rises; THD rises monotonically; bounded
        { std::string d; bool okL = true, okT = true; double lastT = -1e9, lastR = 0.0; E r = fresh (P {}); Cap cr = capture (r, monoTone, N); const double r0 = db (rms (cr.L));
          for (float t : { 0.0f, 0.2f, 0.4f, 0.6f, 0.8f, 1.0f }) { P p; p.drive = t; E e = fresh (p); Cap c = capture (e, monoTone, N); const double rr = db (rms (c.L)) - r0, th = thd (c.L, 440.0); if (rr < -1.0 || rr > 6.0) okL = false; if (t > 0.0f && th < lastT - 0.5) okT = false; lastT = th; lastR = rr; d += f1 (rr) + "/" + f1 (th) + " "; }
          gate ("Drive holds the bus level (-1..+6 dB across the knob — it gets LOUDER, never quieter) and THD RISES monotonically", okL && okT, "rms/thd: " + d); }
        { P p; p.drive = 1.0f; E e = fresh (p); Cap c = capture (e, [] (int i) { const float x = (float) (0.9 * std::sin (2.0 * M_PI * 100.0 * i / FS)); return std::make_pair (x, x); }, N); gate ("Drive at the top on a HOT input stays bounded (peak ≤ 1.0)", peak (c.L) <= 1.0001, std::to_string (peak (c.L))); }
        { P p; p.drive = 1.0f; E e = fresh (p); Cap c = capture (e, monoTone, N); gate ("Drive at the top is a SQUARE (THD > -10 dB) at the bus", thd (c.L, 440.0) > -10.0, f1 (thd (c.L, 440.0)) + " dB THD"); }
        { P p; p.drive = 0.0f; E e = fresh (p); Cap c = capture (e, monoSaw, N); double md = 0.0; for (size_t i = 0; i < c.L.size(); ++i) md = std::max (md, (double) std::fabs (c.L[i] - c.inL[i])); gate ("Drive at 0 is a bit-exact bypass", md == 0.0, std::to_string (md)); }
    }

    // ═══════════════════════════════════════════════════════════════════════
    std::printf ("\n§G — THE MIX LAW. Equal power, both endpoints exact.\n");
    {
        { P p; p.mix = 0.0f; p.drive = 1.0f; p.hp = 0.8f; E e = fresh (p); Cap c = capture (e, monoSaw, N); double md = 0.0; for (size_t i = 0; i < c.L.size(); ++i) md = std::max (md, (double) std::fabs (c.L[i] - c.inL[i])); gate ("Mix 0 is the dry, bit-exact, whatever the strip is doing", md == 0.0, std::to_string (md)); }
        { P p; p.mix = 0.5f; p.gain = 0.0f; E e = fresh (p); Cap c = capture (e, monoTone, N, 0.5); gate ("Mix 50 % with the strip muted is the dry at -3 dB (equal power)", std::fabs (db (rms (c.L) / rms (c.inL)) + 3.0) < 0.2, f1 (db (rms (c.L) / rms (c.inL))) + " dB"); }
    }

    // ═══════════════════════════════════════════════════════════════════════
    std::printf ("\n§H — NO CLICKS. Every knob swept under a sustained tone; every lamp toggled under it.\n");
    {
        auto sweep = [&] (const char* nm, std::function<void (P&, float)> set, float from, float to) {
            P p; set (p, from); E e = fresh (p); const int n = (int) (0.8 * FS); const int blk = 64; double worst = 0.0; float l = 0.0f, r = 0.0f, pl = 0.0f; double pk = 1e-4;
            for (int i = 0; i < n; ++i) { if (i % blk == 0) { P q; set (q, from + (to - from) * (float) i / (float) n); e.setParams (q); } const float x = (float) (BUS * std::sin (2.0 * M_PI * 220.0 * i / FS)); e.processStereo (x, x, l, r);
                pk = std::max (1e-4, std::max (pk * 0.9995, (double) std::fabs (l)));                      // the output's own running peak
                if (i > (int) (0.1 * FS)) worst = std::max (worst, (double) std::fabs (l - pl) / pk); pl = l; }
            gate ((std::string ("sweep ") + nm + " under a 220 Hz tone: max step / own peak < 0.08 (a clean sine is 0.029)").c_str(), worst < 0.08, "ratio " + f2 (worst)); };
        sweep ("High Pass 0→1", [] (P& p, float v) { p.hp = v; }, 0.0f, 1.0f);
        sweep ("Low Pass 1→0", [] (P& p, float v) { p.lp = v; }, 1.0f, 0.0f);
        sweep ("Bass 0→1",     [] (P& p, float v) { p.bass = v; }, 0.0f, 1.0f);
        sweep ("Air 0→1",      [] (P& p, float v) { p.air = v; }, 0.0f, 1.0f);
        sweep ("Mono Below",   [] (P& p, float v) { p.monoBelow = v; }, 0.0f, 1.0f);
        sweep ("Rotate 0→1",   [] (P& p, float v) { p.rotate = v; }, 0.0f, 1.0f);
        sweep ("Haas 0→1",     [] (P& p, float v) { p.haas = v; }, 0.0f, 1.0f);
        sweep ("Drive 0→0.35", [] (P& p, float v) { p.drive = v; }, 0.0f, 0.35f);   // the smooth third: past it the wave is a square whose edges are honest, not clicks
        sweep ("Gain 0→1",     [] (P& p, float v) { p.gain = v; }, 0.0f, 1.0f);
        sweep ("Width 0→1",    [] (P& p, float v) { p.image = v; }, 0.0f, 1.0f);
        sweep ("Pan 0→1",      [] (P& p, float v) { p.steer = v; }, 0.0f, 1.0f);
        auto toggle = [&] (const char* nm, std::function<void (P&, bool)> set) {
            P p; p.steer = 0.3f; E e = fresh (p); const int n = (int) (0.6 * FS); double worst = 0.0; float l = 0.0f, r = 0.0f, pl = 0.0f, pr = 0.0f; double pk = 1e-4;
            for (int i = 0; i < n; ++i) { if (i == (int) (0.3 * FS)) { P q; q.steer = 0.3f; set (q, true); e.setParams (q); } const float x = (float) (BUS * std::sin (2.0 * M_PI * 220.0 * i / FS)); e.processStereo (x, x, l, r);
                pk = std::max (1e-4, std::max (pk * 0.9995, (double) std::max (std::fabs (l), std::fabs (r))));
                if (i > (int) (0.1 * FS)) worst = std::max (worst, (double) std::max (std::fabs (l - pl), std::fabs (r - pr)) / pk); pl = l; pr = r; }
            gate ((std::string ("toggle ") + nm + " under a tone does not click (step / peak < 0.08)").c_str(), worst < 0.08, "ratio " + f2 (worst)); };
        toggle ("Flip L", [] (P& p, bool v) { p.flipL = v; }); toggle ("Flip R", [] (P& p, bool v) { p.flipR = v; }); toggle ("Swap", [] (P& p, bool v) { p.swap = v; });
        toggle ("Mono", [] (P& p, bool v) { p.sum = v; }); toggle ("Dim", [] (P& p, bool v) { p.dim = v; });
    }

    // ═══════════════════════════════════════════════════════════════════════
    std::printf ("\n§I — SAMPLE RATE. The corners mean the same Hz at 44.1 and 96.\n");
    {
        for (float fs : { 44100.0f, 96000.0f })
        {
            FS = fs; P p; p.hp = 0.5f; const double fc = E::hpHzFor (0.5f); E e = fresh (p); gNoise.reset(); Cap c = capture (e, noisePair, N); gNoise.reset(); E r = fresh (P {}); Cap d = capture (r, noisePair, N);
            const double lo = db (bandEnergy (cspec (c.L), fc * 0.22, fc * 0.28) / bandEnergy (cspec (d.L), fc * 0.22, fc * 0.28));
            gate ((std::string ("High Pass at ") + f1 (fs) + " Hz: 2 octaves below the corner ≥ 20 dB down").c_str(), lo < -20.0, f1 (lo) + " dB");
            P q; q.haas = 0.75f; const int want = (int) std::lround (E::haasMsFor (0.75f) * 0.001 * FS); E e2 = fresh (q); gNoise.reset(); Cap c2 = capture (e2, noisePair, N, 0.6); const int lag = lagOfPeakXcorr (c2.L, c2.inL, want + 40);
            gate ((std::string ("Haas at ") + f1 (fs) + " Hz: the delay is the same MILLISECONDS").c_str(), std::abs (lag - want) <= 1, "want " + std::to_string (want) + " got " + std::to_string (lag));
        }
        FS = 48000.0f;
    }

    // ═══════════════════════════════════════════════════════════════════════
    std::printf ("\n§J — NOTHING FREE-RUNS, NOTHING BLOWS UP.\n");
    {
        { P p; p.drive = 1.0f; p.bass = 1.0f; p.air = 1.0f; p.haas = 1.0f; p.rotate = 1.0f; p.hp = 0.3f; E e = fresh (p); Cap c = capture (e, [] (int) { return std::make_pair (0.0f, 0.0f); }, N, 1.0); gate ("silence in → silence out with everything up (no self-noise)", peak (c.L) == 0.0 && peak (c.R) == 0.0, std::to_string (peak (c.L))); }
        { bool bad = false; for (int t = 0; t < E::kNumTypes; ++t) { P p; p.type = t; p.drive = 1.0f; p.image = 1.0f; p.bass = 1.0f; p.air = 1.0f; p.hp = 1.0f; p.lp = 0.0f; p.haas = 0.0f; p.rotate = 1.0f; p.monoBelow = 1.0f; E e = fresh (p); Cap c = capture (e, noisePair, N); for (float v : c.L) if (! std::isfinite (v) || std::fabs (v) > 4.0f) bad = true; for (float v : c.R) if (! std::isfinite (v) || std::fabs (v) > 4.0f) bad = true; }
          gate ("no NaN, no runaway, on any Type with everything at its end", ! bad, ""); }
    }

    std::printf ("\n══ RESULT: %d pass, %d FAIL ══\n\n", gPass, gFail);
    return gFail ? 1 : 0;
}
