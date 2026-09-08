// StellateNode_test.cpp — V4 offline validation (g++ -std=c++17 -Wall -Wextra -Wpedantic -O2)
// Full-replacement engine + V4: spectral LP/HP filters, delay-regeneration FEED, auto-pan
// WIDTH, digitalis QUALITY, character shapes (Hyper/Pluck/Crown/Radio/Razor), engine-side
// resting preview — plus everything carried (FLL/poly-ET, harmonic audit, silence, bypass).
#include "StellateNode.h"
#include <cstdio>
#include <cmath>
#include <vector>
#include <string>
#include <random>

using namespace wc;
static int gPass = 0, gFail = 0;
static void check (bool ok, const std::string& name, const std::string& detail = "")
{
    std::printf ("  [%s] %s%s%s\n", ok ? "PASS" : "FAIL", name.c_str(),
                 detail.empty() ? "" : "  — ", detail.c_str());
    ok ? ++gPass : ++gFail;
}
static const double FS = 48000.0;
static double noteHz (int n) { return 440.0 * std::pow (2.0, (n - 69) / 12.0); }
static double goertzel (const float* x, int N, double f)
{
    const double w = 2.0 * M_PI * f / FS, c = std::cos (w);
    double q1 = 0, q2 = 0;
    for (int i = 0; i < N; ++i) { const double q0 = 2 * c * q1 - q2 + x[i]; q2 = q1; q1 = q0; }
    return std::sqrt (q1 * q1 + q2 * q2 - 2 * c * q1 * q2) / N;
}
static float peakAbs (const std::vector<float>& v)
{ float m = 0; for (float x : v) m = std::max (m, std::fabs (x)); return m; }
static double rmsOf (const std::vector<float>& v)
{ double s = 0; for (float x : v) s += (double) x * x; return std::sqrt (s / std::max<size_t> (1, v.size())); }

struct P { int eng=1; float air=0, mot=0, feed=0, wid=0, qual=0.8f, tilt=0.5f, shine=0.0f, lp=1.0f, hp=0.0f; int hard=1; };

struct Cap { std::vector<float> L, R; };
static Cap run (StellateNode& sn, int shape, const P& pp, const int* held, int nH,
                int blocks, int bs, double sf, int skip = 0, float amp = 0.5f,
                bool sawIn = false, float nz = 0.0f, unsigned seed = 5)
{
    Cap cap; std::vector<float> L (bs), R (bs);
    std::mt19937 rng (seed); std::uniform_real_distribution<float> u (-1.0f, 1.0f);
    double ph = 0.0;
    for (int b = 0; b < blocks; ++b)
    {
        for (int i = 0; i < bs; ++i)
        {
            float s = 0.0f;
            if (sf > 0.0)
            {
                if (sawIn) { for (int m = 1; m <= 8; ++m) s += (amp / (float) m) * (float) std::sin (2.0 * M_PI * ph * m); }
                else         s = amp * (float) std::sin (2.0 * M_PI * ph);
                ph += sf / FS; if (ph >= 1.0) ph -= 1.0;
            }
            if (nz > 0.0f) s += nz * u (rng);
            L[i] = s; R[i] = s;
        }
        sn.process (shape, pp.eng, pp.air, pp.mot, pp.feed, pp.wid, pp.qual, pp.tilt, pp.shine,
                    pp.lp, pp.hp, pp.hard, held, nH, FS, L.data(), R.data(), bs);
        if (b >= skip)
            for (int i = 0; i < bs; ++i) { cap.L.push_back (L[i]); cap.R.push_back (R[i]); }
    }
    return cap;
}
static std::vector<float> mono (const Cap& c)
{ std::vector<float> m (c.L.size()); for (size_t i = 0; i < m.size(); ++i) m[i] = 0.5f * (c.L[i] + c.R[i]); return m; }

static const char* SHN[12] = { "SINE","TRIANGLE","SQUARE","SAW","HYPER","PLUCK",
                               "EMBER","VEIL","CROWN","RADIO","RAZOR","GLACIER" };

int main()
{
    const int bs = 128;
    std::printf ("STELLATE V4 — character spectral engine validation\n");
    std::printf ("===================================================\n");

    std::printf ("\n[1] BYPASS — disengaged is bit-exact (whole toolkit hot)\n");
    {
        StellateNode sn; sn.prepare (FS);
        int held[1] = { 60 };
        std::vector<float> L (bs), R (bs), cl (bs), cr (bs);
        std::mt19937 rng (7); std::uniform_real_distribution<float> u (-0.8f, 0.8f);
        float md = 0.0f;
        for (int b = 0; b < 40; ++b)
        {
            for (int i = 0; i < bs; ++i) { L[i] = u (rng); R[i] = u (rng); cl[i] = L[i]; cr[i] = R[i]; }
            sn.process (4, 0, 0.7f, 0.8f, 0.6f, 0.7f, 0.3f, 0.7f, 0.5f, 0.6f, 0.4f, 0, held, 1, FS, L.data(), R.data(), bs);
            for (int i = 0; i < bs; ++i) md = std::max (md, std::max (std::fabs (L[i]-cl[i]), std::fabs (R[i]-cr[i])));
        }
        check (md < 1.0e-7f, "output == input when disengaged", "max|d|=" + std::to_string (md));
    }

    std::printf ("\n[2] SILENCE GUARANTEE — engaged, silent input, every shape (feed/air/motion up)\n");
    for (int s = 0; s < 12; ++s)
    {
        StellateNode sn; sn.prepare (FS);
        int held[1] = { 60 }; P pp; pp.feed = 1.0f; pp.air = 1.0f; pp.mot = 1.0f; pp.qual = 0.1f;
        Cap c = run (sn, s, pp, held, 1, 60, bs, -1.0);
        const float pk = std::max (peakAbs (c.L), peakAbs (c.R));
        check (pk < 1.0e-5f, std::string (SHN[s]) + " engaged + silence -> silence", "peak=" + std::to_string (pk));
    }

    std::printf ("\n[3] PITCH — mono FLL locks mis-rooted one-shots; poly eases to ET\n");
    {
        const int note = 55; const double fMidi = noteHz (note);
        int held[1] = { note };
        auto centsAt = [&] (const std::vector<float>& m, double fx)
        {
            double ePk = 0, fPk = fx;
            for (double cc = -80; cc <= 80; cc += 0.5)
            { const double f = fx * std::pow (2.0, cc / 1200.0);
              const double e = goertzel (m.data(), (int) m.size(), f);
              if (e > ePk) { ePk = e; fPk = f; } }
            return 1200.0 * std::log2 (fPk / fx);
        };
        { StellateNode sn; sn.prepare (FS); P pp; pp.hard = 0;
          const double fAud = fMidi * std::pow (2.0,  37.0/1200.0);
          Cap c = run (sn, 7, pp, held, 1, 400, bs, fAud, 220); auto m = mono (c);
          const double off = centsAt (m, fAud * 3.0);
          check (std::fabs (off) < 6.0, "TRACK mono: +37c source -> stack on the AUDIO grid", std::to_string (off) + "c"); }
        { StellateNode sn; sn.prepare (FS); P pp; pp.hard = 0;
          const double fAud = fMidi * std::pow (2.0, -52.0/1200.0);
          Cap c = run (sn, 7, pp, held, 1, 400, bs, fAud, 220); auto m = mono (c);
          const double off = centsAt (m, fAud * 3.0);
          check (std::fabs (off) < 6.0, "TRACK mono: -52c source locked too", std::to_string (off) + "c"); }
        { StellateNode sn; sn.prepare (FS); P pp; pp.hard = 1;
          Cap c = run (sn, 7, pp, held, 1, 300, bs, fMidi, 150); auto m = mono (c);
          const double off = centsAt (m, fMidi * 3.0);
          check (std::fabs (off) < 3.0, "HARD: exact on the MIDI grid", std::to_string (off) + "c"); }
        {
            StellateNode sn; sn.prepare (FS);
            const int tri[3] = { 48, 52, 55 };
            const double fA = noteHz (48), fB = noteHz (52), fC = noteHz (55);
            std::vector<float> L (bs), R (bs);
            double pa = 0, pb = 0, pcs = 0;
            for (int b = 0; b < 400; ++b)
            {
                for (int i = 0; i < bs; ++i)
                { const float s = 0.28f * (float) (std::sin (2*M_PI*pa) + std::sin (2*M_PI*pb) + std::sin (2*M_PI*pcs));
                  L[i] = s; R[i] = s;
                  pa += fA/FS; if (pa>=1) pa-=1; pb += fB/FS; if (pb>=1) pb-=1; pcs += fC/FS; if (pcs>=1) pcs-=1; }
                sn.process (3, 1, 0, 0, 0, 0, 0.8f, 0.5f, 0.0f, 1.0f, 0.0f, 0, tri, 3, FS, L.data(), R.data(), bs);
            }
            double worst = 0; bool ok = sn.vizN > 6 && sn.vizLive;
            for (int q = 0; q < sn.vizN; ++q)
            {
                double best = 1e9;
                for (double f0 : { fA, fB, fC })
                { const double r = sn.vizF[q] / f0, rq = std::max (1.0, std::round (r));
                  best = std::min (best, std::fabs (1200.0 * std::log2 (r / rq))); }
                worst = std::max (worst, best);
                if (best > 6.0) ok = false;
            }
            check (ok, "POLY triad (TRACK): every partial on the chord's ET grid", "worst=" + std::to_string (worst) + "c vizN=" + std::to_string (sn.vizN));
        }
    }

    std::printf ("\n[4] FULL REPLACEMENT + AIR\n");
    {
        const int note = 48; const double f0 = noteHz (note); int held[1] = { note };
        StellateNode byp; byp.prepare (FS); P p0; p0.eng = 0;
        Cap cd = run (byp, 0, p0, held, 1, 260, bs, f0, 100, 0.4f, true); auto md = mono (cd);
        StellateNode sn; sn.prepare (FS); P pp;
        Cap cw = run (sn, 0, pp, held, 1, 260, bs, f0, 100, 0.4f, true); auto mw = mono (cw);
        const double h2d = goertzel (md.data(), (int) md.size(), f0*2), h2w = goertzel (mw.data(), (int) mw.size(), f0*2);
        const double h3d = goertzel (md.data(), (int) md.size(), f0*3), h3w = goertzel (mw.data(), (int) mw.size(), f0*3);
        const double f1w = goertzel (mw.data(), (int) mw.size(), f0);
        check (h2w < h2d * 0.10 && h3w < h3d * 0.10, "SINE shape: source h2/h3 fully replaced (<10% of bypass)",
               "h2 " + std::to_string (h2w/h2d) + "x  h3 " + std::to_string (h3w/h3d) + "x");
        check (f1w > 1.0e-4, "fundamental present", "E1=" + std::to_string (f1w));
        {
            const int nt = 57; int hh[1] = { nt };
            StellateNode a0; a0.prepare (FS); P pa; pa.air = 0.0f;
            Cap c0 = run (a0, 3, pa, hh, 1, 260, bs, -1.0, 100, 0.0f, false, 0.30f); auto m0 = mono (c0);
            StellateNode a1; a1.prepare (FS); P pb; pb.air = 1.0f;
            Cap c1 = run (a1, 3, pb, hh, 1, 260, bs, -1.0, 100, 0.0f, false, 0.30f, 5); auto m1 = mono (c1);
            const double r0 = rmsOf (m0), r1 = rmsOf (m1);
            check (r1 > r0 * 3.0 && r1 > 0.12, "AIR 1 passes the breath the stack can't hold (noise in)",
                   "rms air0=" + std::to_string (r0) + " air1=" + std::to_string (r1));
        }
    }

    std::printf ("\n[5] SPECTRAL FILTERS — LP saves the ears, HP guts the lows (harmonic domain)\n");
    {
        const int note = 48; const double f0 = noteHz (note); int held[1] = { note };
        auto E = [&] (const std::vector<float>& m, double f) { return goertzel (m.data(), (int) m.size(), f); };
        { StellateNode a; a.prepare (FS); P pa;                      // LP open (default)
          Cap ca = run (a, 3, pa, held, 1, 180, bs, f0, 80); auto ma = mono (ca);
          StellateNode b; b.prepare (FS); P pb; pb.lp = 0.22f;       // LP closed to ~lowest harmonics
          Cap cb = run (b, 3, pb, held, 1, 180, bs, f0, 80); auto mb = mono (cb);
          const double rA = E (ma, f0*7) / (E (ma, f0) + 1e-12), rB = E (mb, f0*7) / (E (mb, f0) + 1e-12);
          check (rB < rA * 0.08 && E (mb, f0) > 1e-5, "SPECTRAL LOWPASS kills the high harmonics, keeps the note",
                 "h7/h1 open=" + std::to_string (rA) + " closed=" + std::to_string (rB)); }
        { StellateNode a; a.prepare (FS); P pa;                      // HP open (default)
          Cap ca = run (a, 3, pa, held, 1, 180, bs, f0, 80); auto ma = mono (ca);
          StellateNode b; b.prepare (FS); P pb; pb.hp = 0.85f;       // HP guts the lows
          Cap cb = run (b, 3, pb, held, 1, 180, bs, f0, 80); auto mb = mono (cb);
          const double rA = E (ma, f0) / (E (ma, f0*7) + 1e-12), rB = E (mb, f0) / (E (mb, f0*7) + 1e-12);
          check (rB < rA * 0.08 && E (mb, f0*7) > 1e-5, "SPECTRAL HIGHPASS guts the lows, keeps the shine",
                 "h1/h7 open=" + std::to_string (rA) + " gutted=" + std::to_string (rB)); }
    }

    std::printf ("\n[6] FEED = DELAY regeneration — echoes, blooms, dies (not a volume knob)\n");
    {
        const int note = 55; const double f0 = noteHz (note); int held[1] = { note };
        auto tailAt = [&] (float feed, int skipB, int lenB)
        {
            StellateNode sn; sn.prepare (FS); P pp; pp.feed = feed;
            run (sn, 3, pp, held, 1, 140, bs, f0);                   // establish
            Cap t = run (sn, 3, pp, held, 1, skipB + lenB, bs, -1.0, skipB);
            return rmsOf (mono (t));
        };
        const double e0 = tailAt (0.0f, 56, 60);                     // ~150–310 ms after input death
        const double e9 = tailAt (0.95f, 56, 60);
        check (e9 > e0 * 6.0, "FEED 0.95: strong echo tail vs FEED 0 (delay behavior)",
               "tail rms feed0=" + std::to_string (e0) + " feed.95=" + std::to_string (e9));
        {
            StellateNode sn; sn.prepare (FS); P pp; pp.feed = 0.95f;
            run (sn, 3, pp, held, 1, 140, bs, f0);
            Cap late = run (sn, 3, pp, held, 1, 1300, bs, -1.0, 1200);   // ~3.2 s later
            check (peakAbs (late.L) < 0.01f, "FEED loop dies (<-40 dB by ~3 s)", "late peak=" + std::to_string (peakAbs (late.L)));
        }
    }

    std::printf ("\n[7] WIDTH = auto-pan chorus motion (not a static split)\n");
    {
        const int note = 50; const double f0 = noteHz (note); int held[1] = { note };
        StellateNode sn; sn.prepare (FS); P pp; pp.wid = 1.0f;
        Cap c = run (sn, 3, pp, held, 1, 100 + 16*40, bs, f0, 100, 0.5f, true);
        const int W = 40 * bs; int flips = 0; double prev = 0, depth = 0;
        for (int w = 0; w < 16; ++w)
        {
            const double eL = goertzel (c.L.data() + w*W, W, f0);
            const double eR = goertzel (c.R.data() + w*W, W, f0);
            const double d = eL - eR;
            depth = std::max (depth, std::fabs (d) / std::max (1e-9, eL + eR));
            if (w > 0 && ((d > 0) != (prev > 0)) && std::fabs (d) > 0.03 * (eL + eR)) ++flips;
            prev = d;
        }
        check (flips >= 2 && depth > 0.15, "WIDTH 1: fundamental PANS back and forth over time",
               "flips=" + std::to_string (flips) + " depth=" + std::to_string (depth));
    }

    std::printf ("\n[8] QUALITY = digitalis degradation (frame-drops + steppy envelopes), bounded\n");
    {
        const int note = 50; const double f0 = noteHz (note); int held[1] = { note };
        auto jerk = [&] (float q)
        {
            StellateNode sn; sn.prepare (FS); P pp; pp.qual = q;
            Cap c = run (sn, 0, pp, held, 1, 440, bs, f0, 140, 0.5f); auto m = mono (c);   // steady SINE in
            const int W2 = 15 * bs;                                    // ~40 ms ≈ 6 periods: kills sub-period ripple
            std::vector<double> rb; double med = 0;
            for (size_t k = 0; k + W2 <= m.size(); k += W2)
            { double s = 0; for (int i = 0; i < W2; ++i) s += (double) m[k+i]*m[k+i];
              rb.push_back (std::sqrt (s / W2)); med += rb.back(); }
            med /= std::max<size_t> (1, rb.size());
            int spikes = 0;
            for (size_t k = 1; k < rb.size(); ++k)
                if (std::fabs (rb[k] - rb[k-1]) > 0.15 * std::max (0.01, med)) ++spikes;
            return std::pair<int,float> (spikes, peakAbs (m));
        };
        auto lo = jerk (0.03f), hi = jerk (1.0f);
        check (lo.first > (hi.first + 1) * 4 && lo.first > 8 && lo.second < 1.4f,
               "QUALITY floor: heavy frame-level breakage vs clean at 1.0, bounded",
               "spikes lo=" + std::to_string (lo.first) + " hi=" + std::to_string (hi.first) + " peak=" + std::to_string (lo.second));
    }

    std::printf ("\n[9] CHARACTER SHAPES — night and day\n");
    {
        const int note = 48; const double f0 = noteHz (note); int held[1] = { note };
        auto E = [&] (const std::vector<float>& m, double f) { return goertzel (m.data(), (int) m.size(), f); };
        { StellateNode sn; sn.prepare (FS); P pp;
          Cap c = run (sn, 0, pp, held, 1, 160, bs, f0, 60); auto m = mono (c);
          check (E (m, f0) > 10.0 * (E (m, f0*2) + E (m, f0*3)), "SINE law",
                 "E1/(E2+E3)=" + std::to_string (E(m,f0)/(E(m,f0*2)+E(m,f0*3)+1e-12))); }
        { StellateNode sn; sn.prepare (FS); P pp;
          Cap c = run (sn, 2, pp, held, 1, 160, bs, f0, 60); auto m = mono (c);
          check (E (m, f0*3) > 6.0 * E (m, f0*2), "SQUARE odd-only", "E3/E2=" + std::to_string (E(m,f0*3)/(E(m,f0*2)+1e-12))); }
        { StellateNode sn; sn.prepare (FS); P pp;
          Cap c = run (sn, 3, pp, held, 1, 160, bs, f0, 60); auto m = mono (c);
          const double r23 = E (m, f0*2) / (E (m, f0*3) + 1e-12);
          check (r23 > 1.1 && r23 < 2.2, "SAW 1/n slope", "E2/E3=" + std::to_string (r23)); }
        { StellateNode sn; sn.prepare (FS); P pp;                    // RAZOR: RISING spectrum
          Cap c = run (sn, 10, pp, held, 1, 160, bs, f0, 60); auto m = mono (c);
          check (E (m, f0*9) > 1.5 * E (m, f0*2), "RAZOR: rising chainsaw spectrum (h9 > h2)",
                 "E9/E2=" + std::to_string (E(m,f0*9)/(E(m,f0*2)+1e-12))); }
        { StellateNode sn; sn.prepare (FS); P pp;                    // CROWN: highs only
          Cap c = run (sn, 8, pp, held, 1, 160, bs, f0, 60); auto m = mono (c);
          check (E (m, f0*9) > 4.0 * E (m, f0*2), "CROWN high-only glass", "E9/E2=" + std::to_string (E(m,f0*9)/(E(m,f0*2)+1e-12))); }
        { StellateNode sn; sn.prepare (FS); P pp;                    // RADIO: band + built-in breakage
          Cap c = run (sn, 9, pp, held, 1, 300, bs, f0, 120, 0.5f, true); auto m = mono (c);
          const double band = E (m, f0*5), out = E (m, f0) + E (m, f0*12) + 1e-12;
          int spikes = 0; const int hb = bs;
          for (size_t k = hb; k + hb < m.size(); k += hb)
          { double a=0,b2=0; for (int i=0;i<hb;++i){a+=std::fabs(m[k-hb+i]);b2+=std::fabs(m[k+i]);}
            a/=hb;b2/=hb; if (std::fabs(b2-a) > 0.05*std::max(0.02,a)) ++spikes; }
          check (band > out * 1.5 && spikes > 8, "RADIO: band-passed digital breakage even at q=0.8",
                 "band/out=" + std::to_string (band/out) + " spikes=" + std::to_string (spikes)); }
        {   // PLUCK: highs die fast after input death vs SQUARE
            auto tailH7 = [&] (int shp)
            { StellateNode sn; sn.prepare (FS); P pp;
              run (sn, shp, pp, held, 1, 140, bs, f0, 0, 0.5f, true);
              Cap t = run (sn, shp, pp, held, 1, 40, bs, -1.0, 24);      // 64–107 ms after death
              auto m = mono (t); return goertzel (m.data(), (int) m.size(), f0*7); };
            const double pl = tailH7 (5), sq = tailH7 (2);
            check (pl < sq * 0.30, "PLUCK: high harmonics decay FAST (vs SQUARE tail)",
                   "h7 tail pluck=" + std::to_string (pl) + " square=" + std::to_string (sq));
        }
        {   // HYPER: ensemble — channels move independently even at width 0 (widFloor)
            StellateNode sn; sn.prepare (FS); P pp; pp.wid = 0.0f;
            Cap c = run (sn, 4, pp, held, 1, 100 + 14*40, bs, f0, 100, 0.5f, true);
            const int W = 40 * bs; int diff = 0;
            for (int w = 0; w < 14; ++w)
            { const double eL = goertzel (c.L.data()+w*W, W, f0*2), eR = goertzel (c.R.data()+w*W, W, f0*2);
              if (std::fabs (eL - eR) > 0.10 * std::max (1e-9, eL + eR)) ++diff; }
            check (diff >= 5, "HYPER: built-in stereo ensemble motion at width 0", "moving windows=" + std::to_string (diff));
        }
        { StellateNode sn; sn.prepare (FS); P ppq; ppq.qual = 0.9f;  // EMBER breathes with level
          Cap cq = run (sn, 6, ppq, held, 1, 200, bs, f0, 80, 0.08f); auto mq = mono (cq);
          StellateNode sn2; sn2.prepare (FS);
          Cap cl = run (sn2, 6, ppq, held, 1, 200, bs, f0, 80, 0.85f); auto ml = mono (cl);
          const double rq = E (mq, f0*7)/(E (mq, f0)+1e-12), rl = E (ml, f0*7)/(E (ml, f0)+1e-12);
          check (rl > rq * 2.0, "EMBER tilt opens with input level", "loud/quiet=" + std::to_string (rl/(rq+1e-12))); }
        { StellateNode sn; sn.prepare (FS); P pp;
          Cap c = run (sn, 7, pp, held, 1, 420, bs, f0, 100); auto m = mono (c);
          check (E (m, f0*0.5) > 3.0 * E (m, f0*3), "VEIL sub-octave body", "sub/high=" + std::to_string (E(m,f0*0.5)/(E(m,f0*3)+1e-12))); }
    }

    std::printf ("\n[10] MOTION + strict-harmonic audit (all 12, incl. dual layers)\n");
    {
        const int note = 50; const double f0 = noteHz (note); int held[1] = { note };
        auto cvOfH3 = [&] (float mot)
        {
            StellateNode sn; sn.prepare (FS); P pp; pp.mot = mot;
            Cap c = run (sn, 3, pp, held, 1, 100 + 8*90, bs, f0, 100, 0.5f, true); auto m = mono (c);
            const int W = 90 * bs; double mean = 0, var = 0; double e[8];
            for (int w = 0; w < 8; ++w) { e[w] = goertzel (m.data() + w*W, W, f0*3); mean += e[w]/8.0; }
            for (int w = 0; w < 8; ++w) var += (e[w]-mean)*(e[w]-mean)/8.0;
            return std::sqrt (var) / (mean + 1e-12);
        };
        const double cv0 = cvOfH3 (0.0f), cv1 = cvOfH3 (1.0f);
        check (cv1 > cv0 * 2.0 && cv1 > 0.10, "MOTION 1: h3 undulates (vs static at 0)",
               "cv0=" + std::to_string (cv0) + " cv1=" + std::to_string (cv1));
        bool allOK = true; double worstC = 0; std::string worst;
        for (int s = 0; s < 12; ++s)
        {
            StellateNode sn; sn.prepare (FS); P pp;
            run (sn, s, pp, held, 1, 120, bs, f0, 0, 0.5f, s == 9);
            for (int q = 0; q < sn.vizN; ++q)
            {
                const double r = sn.vizF[q] / f0;
                const double rq = (r >= 0.75) ? std::round (r) : (1.0 / std::round (1.0 / r));
                const double cents = 1200.0 * std::log2 (r / std::max (0.1, rq));
                if (std::fabs (cents) > std::fabs (worstC)) { worstC = cents; worst = SHN[s]; }
                if (std::fabs (cents) > 6.0) allOK = false;
            }
        }
        check (allOK, "no inharmonic content anywhere (grid audit, 12 shapes)",
               "worst=" + std::to_string (worstC) + "c (" + worst + ")");
        {
            StellateNode sn; sn.prepare (FS); P pp; pp.mot = 1.0f;
            run (sn, 3, pp, held, 1, 150, bs, f0, 0, 0.5f, true);
            double w2 = 0;
            for (int q = 0; q < sn.vizN; ++q)
            { const double r = sn.vizF[q] / f0;
              w2 = std::max (w2, std::fabs (1200.0 * std::log2 (r / std::max (1.0, std::round (r))))); }
            check (w2 < 5.0, "MOTION shimmer stays harmonic-centered (<5c)", "worst=" + std::to_string (w2) + "c");
        }
        {
            StellateNode sn; sn.prepare (FS); P pp; pp.shine = 0.25f;
            run (sn, 3, pp, held, 1, 120, bs, f0, 0, 0.5f);
            double w2 = 0;
            for (int q = 0; q < sn.vizN; ++q)
            { const double r = sn.vizF[q] / f0;
              w2 = std::max (w2, std::fabs (1200.0 * std::log2 (r / std::max (1.0, std::round (r))))); }
            check (w2 < 6.0, "SHINE 0.25 crossfade layers stay harmonic", "worst=" + std::to_string (w2) + "c");
        }
    }

    std::printf ("\n[11] TILT / SHINE / RESTING PREVIEW\n");
    {
        const int note = 48; const double f0 = noteHz (note); int held[1] = { note };
        auto E = [&] (const std::vector<float>& m, double f) { return goertzel (m.data(), (int) m.size(), f); };
        { StellateNode sn; sn.prepare (FS); P pp; pp.shine = 0.5f;
          Cap c = run (sn, 0, pp, held, 1, 160, bs, f0, 60); auto m = mono (c);
          check (E (m, f0*2) > 8.0 * E (m, f0), "SHINE 0.5 = clean +1 octave", "E(2f0)/E(f0)=" + std::to_string (E(m,f0*2)/(E(m,f0)+1e-12))); }
        { StellateNode a; a.prepare (FS); P pa; pa.tilt = 0.08f;
          Cap ca = run (a, 3, pa, held, 1, 160, bs, f0, 60); auto ma = mono (ca);
          StellateNode b; b.prepare (FS); P pb; pb.tilt = 0.92f;
          Cap cb = run (b, 3, pb, held, 1, 160, bs, f0, 60); auto mb = mono (cb);
          const double rA = E (ma, f0*7)/(E (ma, f0)+1e-12), rB = E (mb, f0*7)/(E (mb, f0)+1e-12);
          check (rB > rA * 6.0, "TILT dark -> bright", "ratio=" + std::to_string (rB/(rA+1e-12))); }
        {   // ENGINE-SIDE RESTING PREVIEW: no notes, silence — the star still has the law
            StellateNode sn; sn.prepare (FS);
            std::vector<float> L (bs, 0.0f), R (bs, 0.0f);
            float pk = 0;
            for (int b = 0; b < 20; ++b)
            {
                std::fill (L.begin(), L.end(), 0.0f); std::fill (R.begin(), R.end(), 0.0f);
                sn.process (8, 1, 0, 0.5f, 0, 0, 0.8f, 0.5f, 0.0f, 1.0f, 0.0f, 0, nullptr, 0, FS, L.data(), R.data(), bs);
                pk = std::max (pk, peakAbs (L));
            }
            bool hiOnly = sn.vizN >= 6;                               // CROWN preview: all partials ≥ 7·f0(C3)
            for (int q = 0; q < sn.vizN; ++q) if (sn.vizF[q] < 130.81f * 6.5f) hiOnly = false;
            check (! sn.vizLive && sn.vizN >= 6 && hiOnly && pk < 1e-7f,
                   "resting preview: engine publishes CROWN's law, silent output, live=false",
                   "vizN=" + std::to_string (sn.vizN) + " pk=" + std::to_string (pk));
            // switch shape while idle → preview follows
            for (int b = 0; b < 4; ++b)
                sn.process (7, 1, 0, 0.5f, 0, 0, 0.8f, 0.5f, 0.0f, 1.0f, 0.0f, 0, nullptr, 0, FS, L.data(), R.data(), bs);
            bool hasSub = false;
            for (int q = 0; q < sn.vizN; ++q) if (sn.vizF[q] < 100.0f) hasSub = true;
            check (! sn.vizLive && hasSub, "resting preview morphs with the shape (VEIL sub appears)",
                   "vizN=" + std::to_string (sn.vizN));
        }
        {   // live flag flips on when audio flows
            StellateNode sn; sn.prepare (FS); P pp;
            const int nt = 60; int hh[1] = { nt };
            run (sn, 3, pp, hh, 1, 60, bs, noteHz (nt));
            check (sn.vizLive && sn.vizN >= 4, "vizLive=true when the engine is singing", "vizN=" + std::to_string (sn.vizN));
        }
    }

    std::printf ("\n[12] Auto-gain, envelope death, polyphony, torture, alias, levels\n");
    {
        {
            const int note = 57; const double f0 = noteHz (note); int held[1] = { note };
            StellateNode a; a.prepare (FS); P pp;
            Cap cq = run (a, 3, pp, held, 1, 260, bs, f0, 140, 0.10f, true);
            StellateNode b; b.prepare (FS);
            Cap cl = run (b, 3, pp, held, 1, 260, bs, f0, 140, 0.70f, true);
            const float pq = peakAbs (cq.L), pl = peakAbs (cl.L);
            check (pq > 0.03f && pl < 1.3f && pl > pq * 1.8f, "auto-gain: louder in -> louder out, both usable",
                   "quiet=" + std::to_string (pq) + " loud=" + std::to_string (pl));
        }
        for (int s : { 2, 8 })
        {
            StellateNode sn; sn.prepare (FS);
            const int note = 60; const double f0 = noteHz (note); int held[1] = { note }; P pp;
            run (sn, s, pp, held, 1, 120, bs, f0);
            Cap tail = run (sn, s, pp, held, 1, 140, bs, -1.0, 105);   // ~280 ms (CROWN rings 1.5×)
            const float pk = std::max (peakAbs (tail.L), peakAbs (tail.R));
            check (pk < 0.02f, std::string (SHN[s]) + " engaged: input dies -> output dies", "tail=" + std::to_string (pk));
        }
        {
            StellateNode sn; sn.prepare (FS);
            const int nA = 48, nB = 55; int held[2] = { nA, nB };
            const double fA = noteHz (nA), fB = noteHz (nB);
            std::vector<float> L (bs), R (bs); Cap cap;
            double pa = 0, pb = 0;
            for (int b = 0; b < 220; ++b)
            {
                for (int i = 0; i < bs; ++i)
                { const float s = 0.35f * (float) (std::sin (2*M_PI*pa) + std::sin (2*M_PI*pb));
                  L[i] = s; R[i] = s; pa += fA/FS; if (pa>=1) pa-=1; pb += fB/FS; if (pb>=1) pb-=1; }
                sn.process (2, 1, 0, 0, 0, 0, 0.8f, 0.5f, 0.0f, 1.0f, 0.0f, 1, held, 2, FS, L.data(), R.data(), bs);
                if (b >= 100) for (int i = 0; i < bs; ++i) cap.L.push_back (L[i]);
            }
            const double eA = goertzel (cap.L.data(), (int) cap.L.size(), fA*3);
            const double eB = goertzel (cap.L.data(), (int) cap.L.size(), fB*3);
            const double off = goertzel (cap.L.data(), (int) cap.L.size(), fA*3.41) + 1e-12;
            check (eA > off*4 && eB > off*4, "polyphony: both stacks live", "A/off=" + std::to_string (eA/off) + " B/off=" + std::to_string (eB/off));
        }
        {
            StellateNode sn; sn.prepare (FS);
            int held[3] = { 36, 60, 84 };
            std::mt19937 rng (11); std::uniform_real_distribution<float> u (-1.0f, 1.0f);
            std::vector<float> L (bs), R (bs);
            bool ok = true; float pk = 0;
            for (int b = 0; b < 500; ++b)
            {
                for (int i = 0; i < bs; ++i) { L[i] = u (rng); R[i] = u (rng); }
                sn.process ((b/12) % 12, (b/7)&1, (b%3)*0.5f, 1.0f, 1.0f, 1.0f, (b%2) ? 0.05f : 0.95f,
                            0.9f, 0.9f, (b%5)*0.25f, (b%4)*0.33f, (b/24)&1,
                            held, 3, FS, L.data(), R.data(), bs);
                for (int i = 0; i < bs; ++i)
                { if (! (L[i] == L[i]) || std::fabs (L[i]) > 5.0f) ok = false;
                  pk = std::max (pk, std::fabs (L[i])); }
            }
            check (ok, "torture: noise + engage toggling + ALL params slamming (incl. LP/HP)", "peak=" + std::to_string (pk));
        }
        {
            StellateNode sn; sn.prepare (FS); P pp; pp.shine = 1.0f;
            const int note = 106; const double f0 = noteHz (note); int held[1] = { note };
            run (sn, 10, pp, held, 1, 80, bs, f0);
            bool ok = true;
            for (int q = 0; q < sn.vizN; ++q) if (sn.vizF[q] > 0.46f * FS) ok = false;
            check (ok, "note 106 + shine +2 + RAZOR: alias skip", "vizN=" + std::to_string (sn.vizN));
        }
        for (int s = 0; s < 12; ++s)
        {
            StellateNode sn; sn.prepare (FS); P pp;
            const int note = 57; const double f0 = noteHz (note); int held[1] = { note };
            Cap c = run (sn, s, pp, held, 1, 200, bs, f0, 100, 0.5f, s == 9 || s == 5);
            const float pk = std::max (peakAbs (c.L), peakAbs (c.R));
            check (pk > 0.05f && pk < 1.3f, std::string (SHN[s]) + " level usable", "peak=" + std::to_string (pk));
        }
    }

    std::printf ("\n===================================================\n");
    std::printf ("RESULT: %d passed, %d failed\n", gPass, gFail);
    return gFail == 0 ? 0 : 1;
}
