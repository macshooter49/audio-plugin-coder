// ════════════════════════════════════════════════════════════════════════════════════════════
//  fb417 — "I CAN'T HEAR WHAT IT'S DOING": Bounce, Tail, and Pedal's stereo.
//
//    clang++ -O2 -std=c++17 -I ../Tests/shim -I ../Source fx3_audibility.cpp -o /tmp/aud
//
//  Max: "the Bounce and the Tail isn't doing anything audible, it's just being there... the
//  Pedal chorus is in mono for some reason."
//
//  🔑 THE MEASUREMENT LAW THIS EXISTS TO OBEY (fb283): a knob is audible or it is not, and
//     GEOMETRY IS NOT HEARING. The flanger roster proves Bounce with a TRAJECTORY metric —
//     "the sweep departs from the un-bounced path by 0 → 57.8 % of its own excursion" — which
//     is a true statement about a control signal nobody listens to. This measures the OUTPUT:
//     the magnitude-spectrum distance between knob-at-0 and knob-at-100, in dB, which is the
//     thing an ear actually integrates.
//
//  And it measures each knob in the state Max is IN — holding a sustained note — because a
//  control that only acts on release is silent while you play, however well it measures.
// ════════════════════════════════════════════════════════════════════════════════════════════
#include "TerrainFlangerFx.h"
#include "TerrainChorusFx.h"
#include <cstdio>
#include <cmath>
#include <vector>
#include <algorithm>

static constexpr float FSR = 48000.0f;
static const double TAU = 6.283185307179586;

// a bright, band-limited square-ish probe — Max: "the flanger and phaser are incredible on a
// square wave", so that is what the knobs get judged on.
static std::vector<float> probe (int n, bool gated = false, double gateOffSec = 2.0)
{
    std::vector<float> x ((size_t) n);
    double ph = 0.0;
    for (int i = 0; i < n; ++i)
    {
        double acc = 0.0;
        for (int k = 1; k <= 39; k += 2) acc += std::sin (ph * (double) k) / (double) k;
        ph += TAU * 110.0 / FSR;
        double g = 1.0;
        if (gated && (double) i / FSR > gateOffSec)
        {
            const double t = (double) i / FSR - gateOffSec;
            g = t < 0.005 ? (1.0 - t / 0.005) : 0.0;      // a 5 ms release, then silence
        }
        x[(size_t) i] = (float) (0.16 * acc * g);
    }
    return x;
}

static void fft (std::vector<double>& re, std::vector<double>& im)
{
    const size_t n = re.size();
    for (size_t i = 1, j = 0; i < n; ++i)
    { size_t b = n >> 1; for (; j & b; b >>= 1) j ^= b; j ^= b;
      if (i < j) { std::swap (re[i], re[j]); std::swap (im[i], im[j]); } }
    for (size_t len = 2; len <= n; len <<= 1)
    {
        const double a = -TAU / (double) len, wr = std::cos (a), wi = std::sin (a);
        for (size_t i = 0; i < n; i += len)
        { double cr = 1, ci = 0;
          for (size_t k = 0; k < len / 2; ++k)
          { const double ur = re[i+k], ui = im[i+k];
            const double vr = re[i+k+len/2]*cr - im[i+k+len/2]*ci;
            const double vi = re[i+k+len/2]*ci + im[i+k+len/2]*cr;
            re[i+k]=ur+vr; im[i+k]=ui+vi; re[i+k+len/2]=ur-vr; im[i+k+len/2]=ui-vi;
            const double nr = cr*wr - ci*wi; ci = cr*wi + ci*wr; cr = nr; } }
    }
}

// AVERAGE MAGNITUDE-SPECTRUM DISTANCE, dB. Frame by frame, so a change in MOTION counts —
// two signals with the same long-term spectrum but different modulation are far apart here,
// which is the whole point for a knob like Bounce.
static double specDistDb (const std::vector<float>& a, const std::vector<float>& b, size_t skip = 24000)
{
    const size_t N = 4096;
    double acc = 0; int frames = 0;
    for (size_t off = skip; off + N <= std::min (a.size(), b.size()); off += N)
    {
        std::vector<double> ar (N), ai (N, 0.0), br (N), bi (N, 0.0);
        for (size_t i = 0; i < N; ++i)
        { const double w = 0.5 - 0.5 * std::cos (TAU * (double) i / (double) (N - 1));
          ar[i] = (double) a[off+i] * w; br[i] = (double) b[off+i] * w; }
        fft (ar, ai); fft (br, bi);
        double d = 0; int bins = 0;
        for (size_t k = 2; k < N / 2; ++k)
        {
            const double ma = std::sqrt (ar[k]*ar[k] + ai[k]*ai[k]) + 1e-9;
            const double mb = std::sqrt (br[k]*br[k] + bi[k]*bi[k]) + 1e-9;
            if (ma < 1e-7 && mb < 1e-7) continue;
            const double dd = 20.0 * std::log10 (ma / mb);
            d += dd * dd; ++bins;
        }
        if (bins) { acc += std::sqrt (d / (double) bins); ++frames; }
    }
    return frames ? acc / (double) frames : 0.0;
}

static double rmsOf (const std::vector<float>& x, size_t from, size_t to)
{
    double e = 0; size_t n = 0;
    for (size_t i = from; i < std::min (to, x.size()); ++i) { e += (double) x[i] * x[i]; ++n; }
    return n ? std::sqrt (e / (double) n) : 0.0;
}

// ── FLANGER ─────────────────────────────────────────────────────────────────────────────────
static void runFla (tw::TerrainFlangerFx::Params p, const std::vector<float>& in,
                    std::vector<float>& L, std::vector<float>& R)
{
    tw::TerrainFlangerFx e; e.prepare (FSR, 64); e.setParams (p);
    L.assign (in.begin(), in.end()); R = L;
    for (size_t i = 0; i + 64 <= L.size(); i += 64) e.processStereo (&L[i], &R[i], 64);
}
static tw::TerrainFlangerFx::Params flaBase()
{
    tw::TerrainFlangerFx::Params p;
    p.type = 1; p.character = 0;                 // Jet / Silver — the plainest comb
    p.rate = 0.30f; p.depth = 0.55f; p.feedback = 0.85f; p.mix = 1.0f;
    p.b1 = 0.5f; p.b2 = 0.35f; p.b3 = 0.625f; p.b4 = 0.35f;
    p.b5 = 0.5f; p.b6 = 0.20f; p.b7 = 0.35f; p.b8 = 0.12f;
    return p;
}

int main()
{
    std::printf ("\n══ fb417 — CAN YOU HEAR IT? spectral distance between knob 0 and knob 100 ══\n");
    std::printf ("   dB is an AVERAGE per-frame magnitude-spectrum distance. For scale, on this\n");
    std::printf ("   same probe and engine: Depth 0 vs 100 and Feedback 0 vs 100 are printed as\n");
    std::printf ("   the reference for what 'obviously audible' looks like.\n");

    const auto sus = probe ((int) (FSR * 6.0f));

    // ── reference: knobs nobody disputes
    std::printf ("\n[REFERENCE — knobs Max can plainly hear]\n");
    {
        std::vector<float> a, b, c, d;
        auto p0 = flaBase(); p0.depth = 0.0f;  runFla (p0, sus, a, c);
        auto p1 = flaBase(); p1.depth = 1.0f;  runFla (p1, sus, b, d);
        std::printf ("  Depth      0 -> 100    %6.2f dB\n", specDistDb (a, b));
        auto q0 = flaBase(); q0.feedback = 0.5f; runFla (q0, sus, a, c);
        auto q1 = flaBase(); q1.feedback = 1.0f; runFla (q1, sus, b, d);
        std::printf ("  Feedback  50 -> 100    %6.2f dB\n", specDistDb (a, b));
        auto r0 = flaBase(); r0.b4 = 0.0f; runFla (r0, sus, a, c);
        auto r1 = flaBase(); r1.b4 = 1.0f; runFla (r1, sus, b, d);
        std::printf ("  Damping    0 -> 100    %6.2f dB\n", specDistDb (a, b));
    }

    // ── the two Max cannot hear
    std::printf ("\n[THE TWO IN QUESTION — same probe, same scale]\n");
    {
        std::vector<float> a, b, c, d;
        auto p0 = flaBase(); p0.b6 = 0.0f; runFla (p0, sus, a, c);
        auto p1 = flaBase(); p1.b6 = 1.0f; runFla (p1, sus, b, d);
        std::printf ("  BOUNCE     0 -> 100    %6.2f dB   <- Max: \"isn't doing anything audible\"\n", specDistDb (a, b));
        auto q0 = flaBase(); q0.b7 = 0.0f; runFla (q0, sus, a, c);
        auto q1 = flaBase(); q1.b7 = 1.0f; runFla (q1, sus, b, d);
        std::printf ("  TAIL       0 -> 100    %6.2f dB   <- while a note is HELD\n", specDistDb (a, b));
    }

    // ── Tail, in the only state where it was ever designed to act
    std::printf ("\n[TAIL — after the note stops, which is the only place it acts]\n");
    {
        const auto gat = probe ((int) (FSR * 8.0f), true, 3.0);
        for (float t : { 0.0f, 0.5f, 1.0f })
        {
            std::vector<float> L, R;
            auto p = flaBase(); p.b7 = t; runFla (p, gat, L, R);
            const double after1 = rmsOf (L, (size_t) (FSR * 3.5f), (size_t) (FSR * 4.0f));
            const double after4 = rmsOf (L, (size_t) (FSR * 7.0f), (size_t) (FSR * 7.5f));
            std::printf ("  Tail %3.0f    0.5 s after note-off %8.6f   4 s after %10.8f\n",
                         t * 100.f, after1, after4);
        }
    }

    // ── CHORUS: is Pedal actually mono?
    std::printf ("\n[PEDAL — Max: \"that is in mono for some reason\"]\n");
    std::printf ("   side/mid dB: -inf is dead mono. Every Type at its own default Character.\n");
    {
        const char* NM[8] = { "Vintage","June","Pedal","Trio","Ensemble","Micro","Wow","Dark" };
        for (int t = 0; t < 8; ++t)
        {
            tw::TerrainChorusFx e; e.prepare (FSR, 64);
            tw::TerrainChorusFx::Params p;
            p.type = t; p.character = 0;
            p.rate = 0.35f; p.depth = 0.60f; p.feedback = 0.0f; p.mix = 1.0f;
            p.b1 = 0.5f; p.b2 = 0.0f; p.b3 = 0.7f; p.b4 = 0.25f;
            p.b5 = 0.0f; p.b6 = 0.5f; p.b7 = 0.0f; p.b8 = 1.0f;
            e.setParams (p);
            std::vector<float> L (sus.begin(), sus.end()), R = L;
            for (size_t i = 0; i + 64 <= L.size(); i += 64) e.processStereo (&L[i], &R[i], 64);
            double em = 0, es = 0; size_t n = 0;
            for (size_t i = (size_t) FSR; i < L.size(); ++i)
            { const double m = 0.5 * ((double) L[i] + R[i]), s = 0.5 * ((double) L[i] - R[i]);
              em += m * m; es += s * s; ++n; }
            const double sm = 10.0 * std::log10 ((es + 1e-18) / (em + 1e-18));
            std::printf ("  %-9s  side/mid %+7.2f dB%s\n", NM[t], sm,
                         (t == 2) ? "     <- Pedal" : "");
        }
    }
    std::printf ("\n");
    return 0;
}
