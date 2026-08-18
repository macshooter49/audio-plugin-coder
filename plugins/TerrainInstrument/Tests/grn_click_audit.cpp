// ════════════════════════════════════════════════════════════════════════════════════════════
//  fb416 — THE GRANULAR MICRO-CLICK AUDIT
//
//    clang++ -O2 -std=c++17 -I ../Source grn_click_audit.cpp -o /tmp/gca && /tmp/gca
//
//  Max: "there's these little clicks that still kind of annoy me... the LOWER the window is,
//  the more clicks, the harsher it is... the window acts like a delay, a repeater, something
//  that takes the sound and chops it up... and our main grain engine doesn't do this."
//
//  🔑 WHY THE EXISTING HARNESS COULD NOT SEE THIS. GranularFxEngine_test.cpp measures
//     max|Δx| / p99.9|Δx| — an outlier in the FIRST difference. That finds a STEP: a splice
//     where the waveform jumps. It cannot find a SLOPE reversal, where the waveform is
//     perfectly continuous in value and its derivative flips sign. A reflection is exactly
//     that, and its spectrum still rolls off only as 1/f² — plenty audible as a tick on
//     bright material, and there is one per bounce per grain.
//
//     So this harness measures the SECOND difference too. That is the whole trick.
//
//  Probe is a SMOOTH sine (the fb362 lesson: a probe with its own transients has a huge
//  outlier ratio of its own, and a granular that faithfully re-reads it inherits the number).
//  Against a 220 Hz sine, ANY energy above 6 kHz is manufactured by the engine.
// ════════════════════════════════════════════════════════════════════════════════════════════
#include "GranularFxEngine.h"
#include <cstdio>
#include <cmath>
#include <vector>
#include <algorithm>
#include <string>

static constexpr double FS = 48000.0;

struct Score
{
    double d1 = 0;      // max|Δx|  / p99.9|Δx|   — STEP discontinuity (the old detector)
    double d2 = 0;      // max|Δ²x| / p99.9|Δ²x|  — SLOPE discontinuity (a reflection)
    int    ticks = 0;   // samples where |Δ²x| > 8 × p99.9 — how MANY, not just the worst
    double hf = 0;      // dB: energy above 6 kHz vs total, on a 220 Hz sine
    double rms = 0;
    long   bounces = 0; // reflections the engine actually performed
};

static double pctl (std::vector<double> v, double q)
{
    if (v.empty()) return 0.0;
    const size_t k = (size_t) (q * (double) (v.size() - 1));
    std::nth_element (v.begin(), v.begin() + (long) k, v.end());
    return v[k];
}

// ⚠️ THE FIRST VERSION OF THIS WAS WRONG AND IT MATTERS. It used a one-pole HP at 6 kHz, which
// rolls off at only 6 dB/oct: a PURE 220 Hz sine leaks through it at 220/6000 = −28.7 dB. So it
// reported "−32 dB of HF" on every case and I nearly read that as the engine manufacturing
// broadband hash, when −32 dB is actually BELOW what the probe alone would score. A detector
// that reads the same on clean and dirty is worse than no detector.
//
// Real FFT, real answer: energy in 8–20 kHz against total. A 220 Hz sine chopped into 9 ms
// grains with smooth envelopes puts sidebands within about ±1 kHz of each partial; anything up
// at 8 kHz+ is an EDGE, not a sideband.
static void fftMag (std::vector<double>& re, std::vector<double>& im)
{
    const size_t n = re.size();
    for (size_t i = 1, j = 0; i < n; ++i)
    {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) { std::swap (re[i], re[j]); std::swap (im[i], im[j]); }
    }
    for (size_t len = 2; len <= n; len <<= 1)
    {
        const double ang = -2.0 * 3.14159265358979 / (double) len;
        const double wr = std::cos (ang), wi = std::sin (ang);
        for (size_t i = 0; i < n; i += len)
        {
            double cr = 1.0, ci = 0.0;
            for (size_t k = 0; k < len / 2; ++k)
            {
                const double ur = re[i + k],           ui = im[i + k];
                const double vr = re[i + k + len / 2] * cr - im[i + k + len / 2] * ci;
                const double vi = re[i + k + len / 2] * ci + im[i + k + len / 2] * cr;
                re[i + k] = ur + vr;  im[i + k] = ui + vi;
                re[i + k + len / 2] = ur - vr;  im[i + k + len / 2] = ui - vi;
                const double nr = cr * wr - ci * wi; ci = cr * wi + ci * wr; cr = nr;
            }
        }
    }
}
static double hfRatioDb (const std::vector<float>& x, double loHz = 8000.0)
{
    const size_t N = 32768;
    if (x.size() < N) return -99.0;
    double eh = 0, et = 0; int frames = 0;
    for (size_t off = 0; off + N <= x.size(); off += N, ++frames)
    {
        std::vector<double> re (N), im (N, 0.0);
        for (size_t i = 0; i < N; ++i)
        {
            const double w = 0.5 - 0.5 * std::cos (2.0 * 3.14159265358979 * (double) i / (double) (N - 1));
            re[i] = (double) x[off + i] * w;
        }
        fftMag (re, im);
        const double binHz = FS / (double) N;
        for (size_t k = 1; k < N / 2; ++k)
        {
            const double f = (double) k * binHz;
            const double e = re[k] * re[k] + im[k] * im[k];
            et += e;
            if (f >= loHz && f <= 22000.0) eh += e;
        }
    }
    if (! frames) return -99.0;
    return 10.0 * std::log10 ((eh + 1e-30) / (et + 1e-30));
}

static Score analyse (const std::vector<float>& y, long bounces)
{
    Score s; s.bounces = bounces;
    if (y.size() < 64) return s;
    std::vector<double> d1, d2;
    d1.reserve (y.size()); d2.reserve (y.size());
    for (size_t i = 1; i < y.size(); ++i)             d1.push_back (std::fabs ((double) y[i] - y[i - 1]));
    for (size_t i = 2; i < y.size(); ++i)             d2.push_back (std::fabs ((double) y[i] - 2.0 * y[i - 1] + y[i - 2]));
    const double p1 = pctl (d1, 0.999), p2 = pctl (d2, 0.999);
    double m1 = 0, m2 = 0;
    for (double v : d1) m1 = std::max (m1, v);
    for (double v : d2) m2 = std::max (m2, v);
    s.d1 = (p1 > 1e-12) ? m1 / p1 : 0.0;
    s.d2 = (p2 > 1e-12) ? m2 / p2 : 0.0;
    for (double v : d2) if (p2 > 1e-12 && v > 8.0 * p2) ++s.ticks;
    s.hf = hfRatioDb (y);
    double e = 0; for (float v : y) e += (double) v * v;
    s.rms = std::sqrt (e / (double) y.size());
    return s;
}

// fb416 — the probe matters as much as the detector. A 220 Hz sine is smooth but DARK, and Max
// hears this on an oscillator: bright, harmonic, and the harshness he describes lives up top.
// 40 harmonics of 110 Hz tops out at 4.4 kHz, so it is band-limited by construction and every
// dB the FFT finds above 6 kHz was manufactured downstream. `bright=false` keeps the old sine.
static inline float probeSample (double& ph, bool bright)
{
    const double TWO_PI = 6.283185307179586;
    if (! bright) { const float v = (float) (0.25 * std::sin (ph)); ph += TWO_PI * 220.0 / FS; return v; }
    double acc = 0.0;
    for (int k = 1; k <= 40; ++k) acc += std::sin (ph * (double) k) / (double) k;
    ph += TWO_PI * 110.0 / FS;
    return (float) (0.09 * acc);
}

// A SMOOTH probe: one sine, nothing else. Any tick in the output was made here.
static Score runCase (float windowMs, float shape, float size, float density,
                      int type = tw::GrnCloud, float pitch = 0.f, float detune = 0.f,
                      double seconds = 4.0, bool bright = false, float spray = 0.f)
{
    tw::GranularFxEngine e; e.prepare (FS);
    tw::GranularFxParams p;
    p.type = type; p.character = tw::GrnClean;
    p.density = density; p.size = size; p.pitch = pitch; p.detune = detune;
    p.spray = spray; p.scan = 0.f; p.windowMs = windowMs; p.width = 0.f;
    p.shape = shape; p.decay = 0.f; p.freeze = 0.f;
    e.setParams (p);

    const long N = (long) (seconds * FS);
    std::vector<float> y; y.reserve ((size_t) N);
    double ph = 0.0;
    for (long i = 0; i < N; ++i)
    {
        const float in = probeSample (ph, bright);
        float ol = 0, orr = 0;
        e.processSample (in, in, ol, orr);
        if (i > (long) (0.75 * FS)) y.push_back (ol);       // skip the fill-in
    }
    return analyse (y, e.bouncesForTesting());
}

int main()
{
    std::printf ("\n══ fb416 — GRANULAR MICRO-CLICK AUDIT ══   (220 Hz sine in, Mix 100 %%, 48 kHz)\n");
    std::printf ("   d1 = step detector (the OLD gate)   d2 = SLOPE detector   ticks = |d2| outliers\n");
    std::printf ("   HF = dB of 8-20 kHz vs total (real FFT) — on a 220 Hz sine every dB is manufactured\n");

    // ── the CONTROL. Without this number every row below is unreadable.
    {
        std::vector<float> probe; probe.reserve (1 << 18);
        double ph = 0.0; const double inc = 2.0 * 3.14159265358979 * 220.0 / FS;
        for (int i = 0; i < (1 << 18); ++i) { probe.push_back ((float) (0.25 * std::sin (ph))); ph += inc; }
        std::printf ("\n[0] CONTROL — the bare 220 Hz probe, no engine:  HF %+7.1f dB\n", hfRatioDb (probe));
    }

    // ── A. THE WINDOW SWEEP. Max's own observation, measured.
    std::printf ("\n[A] Window sweep — Shape 0.5 (Hann), Size 0.25, Density 0.40\n");
    std::printf ("    window     d1      d2   ticks     HF dB    bounces\n");
    for (float w : { 50.f, 100.f, 250.f, 500.f, 1000.f, 2000.f, 4000.f, 8000.f, 16000.f })
    {
        const Score s = runCase (w, 0.5f, 0.25f, 0.40f);
        std::printf ("  %7.0f ms  %5.1f   %5.1f  %6d   %+7.1f   %8ld\n",
                     w, s.d1, s.d2, s.ticks, s.hf, s.bounces);
    }

    // ── B. does SHAPE rescue it? Max: "it should actually pay attention to the shape."
    std::printf ("\n[B] Shape sweep at the WORST window (50 ms)\n");
    std::printf ("    shape                    d1      d2   ticks     HF dB    bounces\n");
    {
        const char* nm[3] = { "0.00 Flat-top", "0.50 Hann    ", "1.00 Bell    " };
        int k = 0;
        for (float sh : { 0.0f, 0.5f, 1.0f })
        {
            const Score s = runCase (50.f, sh, 0.25f, 0.40f);
            std::printf ("  %s        %5.1f   %5.1f  %6d   %+7.1f   %8ld\n",
                         nm[k++], s.d1, s.d2, s.ticks, s.hf, s.bounces);
        }
    }

    // ── C. SIZE vs WINDOW — the ratio is the whole story if reflection is the cause
    std::printf ("\n[C] Size sweep at a short window (100 ms) — grain length vs span\n");
    std::printf ("    size      grain ms      d2   ticks     HF dB    bounces\n");
    for (float sz : { 0.05f, 0.15f, 0.25f, 0.45f, 0.65f, 0.85f })
    {
        const Score s = runCase (100.f, 0.5f, sz, 0.40f);
        const double ms = 2.0 * std::pow (450.0, (double) sz);
        std::printf ("  %5.2f     %7.1f     %5.1f  %6d   %+7.1f   %8ld\n",
                     sz, ms, s.d2, s.ticks, s.hf, s.bounces);
    }

    // ── D. the control: a LONG window, where a grain never reaches its own edge
    std::printf ("\n[D] Control — long window, same grains. If reflection is the cause this is clean.\n");
    {
        const Score a = runCase (50.f,    0.5f, 0.45f, 0.40f);
        const Score b = runCase (16000.f, 0.5f, 0.45f, 0.40f);
        std::printf ("     50 ms window:  d2 %5.1f   ticks %6d   HF %+7.1f   bounces %8ld\n", a.d2, a.ticks, a.hf, a.bounces);
        std::printf ("  16000 ms window:  d2 %5.1f   ticks %6d   HF %+7.1f   bounces %8ld\n", b.d2, b.ticks, b.hf, b.bounces);
        std::printf ("  → HF delta %+.1f dB, tick delta %d\n", a.hf - b.hf, a.ticks - b.ticks);
    }

    // ── E. every Type at the worst window, so a fix cannot be tuned to Cloud alone
    std::printf ("\n[E] Every Type at a 100 ms window\n");
    {
        const char* T[8] = { "Cloud","Rise","Swarm","Suspend","Scatter","Rewind","Stretch","Pulverize" };
        for (int t = 0; t < 8; ++t)
        {
            const Score s = runCase (100.f, 0.5f, 0.25f, 0.40f, t);
            std::printf ("  %-10s  d2 %5.1f   ticks %6d   HF %+7.1f   bounces %8ld\n",
                         T[t], s.d2, s.ticks, s.hf, s.bounces);
        }
    }
    // ═══ THE DECISIVE PART ═══════════════════════════════════════════════════════════════
    // Sections A-E all read the same to a tenth of a dB, which is itself the finding: with
    // Spray 0 and Scan 0 every grain is born at the SAME age, so the Window knob changes almost
    // nothing except how long a grain may be. To learn what the artifact is made OF, scale the
    // things that could make it — one at a time — on a BRIGHT probe, where Max hears it.
    {
        std::vector<float> probe; probe.reserve (1 << 18);
        double ph = 0.0;
        for (int i = 0; i < (1 << 18); ++i) probe.push_back (probeSample (ph, true));
        std::printf ("\n[0b] CONTROL — the bright 110 Hz probe (40 harmonics, band-limited to 4.4 kHz)\n");
        std::printf ("     bare probe, no engine:  HF %+7.1f dB\n", hfRatioDb (probe));
    }

    std::printf ("\n[F] DENSITY sweep, bright probe — if the artifact is GRAIN EDGES it tracks the rate\n");
    std::printf ("    density   grains/s      d2   ticks     HF dB\n");
    for (float d : { 0.10f, 0.25f, 0.40f, 0.55f, 0.70f, 0.85f, 1.00f })
    {
        const Score s = runCase (1000.f, 0.5f, 0.25f, d, tw::GrnCloud, 0.f, 0.f, 4.0, true);
        std::printf ("  %7.2f   %8.1f   %5.1f  %6d   %+7.1f\n",
                     d, std::pow (220.0, (double) d), s.d2, s.ticks, s.hf);
    }

    std::printf ("\n[G] SIZE sweep, bright probe, long window — grain edges per second held ~constant\n");
    std::printf ("    size     grain ms      d2   ticks     HF dB\n");
    for (float sz : { 0.05f, 0.15f, 0.25f, 0.45f, 0.65f, 0.85f })
    {
        const Score s = runCase (8000.f, 0.5f, sz, 0.40f, tw::GrnCloud, 0.f, 0.f, 4.0, true);
        std::printf ("  %6.2f   %8.1f   %5.1f  %6d   %+7.1f\n",
                     sz, 2.0 * std::pow (450.0, (double) sz), s.d2, s.ticks, s.hf);
    }

    std::printf ("\n[H] SHAPE, bright probe — Max: \"it should actually pay attention to the shape\"\n");
    std::printf ("    shape            9 ms grains        90 ms grains\n");
    for (float sh : { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f })
    {
        const Score a = runCase (8000.f, sh, 0.25f, 0.40f, tw::GrnCloud, 0.f, 0.f, 4.0, true);
        const Score b = runCase (8000.f, sh, 0.62f, 0.40f, tw::GrnCloud, 0.f, 0.f, 4.0, true);
        std::printf ("  %6.2f            HF %+7.1f        HF %+7.1f\n", sh, a.hf, b.hf);
    }

    std::printf ("\n[I] WINDOW, bright probe, WITH SPRAY — now the window actually selects material\n");
    std::printf ("    window      d2   ticks     HF dB   bounces\n");
    for (float w : { 50.f, 100.f, 250.f, 1000.f, 4000.f, 16000.f })
    {
        const Score s = runCase (w, 0.5f, 0.25f, 0.40f, tw::GrnCloud, 0.f, 0.f, 4.0, true, 0.35f);
        std::printf ("  %7.0f ms  %5.1f  %6d   %+7.1f  %8ld\n", w, s.d2, s.ticks, s.hf, s.bounces);
    }

    std::printf ("\n[J] PITCH / DETUNE, bright probe — a resampled read is a different artifact class\n");
    std::printf ("    case                   d2   ticks     HF dB      HF14 dB     bounces\n");
    // ⚠️ 8 kHz is the WRONG floor for a transposed read. The probe tops at 4.4 kHz, so at +12 st
    //  its own content legitimately reaches 8.8 kHz and lands inside an 8-22 kHz band — that
    //  would score as "artifact" when it is just the note, played higher. 14 kHz is above 3x the
    //  probe's top, so nothing musical can reach it at any pitch tested here.
    std::printf ("    (HF14 = 14-22 kHz: above 3x the probe's top, so no transposed PARTIAL can land there)\n");
    for (float st : { -24.f, -12.f, -7.f, 0.f, +7.f, +12.f, +19.f, +24.f })
    {
        tw::GranularFxEngine e; e.prepare (FS);
        tw::GranularFxParams p;
        p.type = tw::GrnCloud; p.character = tw::GrnClean;
        p.density = 0.40f; p.size = 0.25f; p.pitch = st; p.detune = 0.f;
        p.spray = 0.f; p.scan = 0.f; p.windowMs = 2000.f; p.width = 0.f;
        p.shape = 0.5f; p.decay = 0.f; p.freeze = 0.f;
        e.setParams (p);
        std::vector<float> y; double ph = 0.0;
        for (long i = 0; i < (long) (4.0 * FS); ++i)
        {
            const float in = probeSample (ph, true);
            float ol = 0, orr = 0; e.processSample (in, in, ol, orr);
            if (i > (long) (0.75 * FS)) y.push_back (ol);
        }
        const Score sc = analyse (y, e.bouncesForTesting());
        std::printf ("  pitch %+3.0f st         %5.1f  %6d   %+7.1f   HF14 %+7.1f   bounces %7ld\n",
                     st, sc.d2, sc.ticks, sc.hf, hfRatioDb (y, 14000.0), sc.bounces);
    }

    std::printf ("\n");
    return 0;
}
