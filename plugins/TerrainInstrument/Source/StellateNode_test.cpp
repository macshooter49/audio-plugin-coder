// StellateNode_test.cpp — V2 offline validation (g++ -std=c++17 -Wall -Wextra -Wpedantic -O2)
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

// default toolkit: replace 0, feed 0, width 0, quality .8, tilt center, shine 0 (no lift → law-exact spectra)
struct P { float mix=1, rep=0, feed=0, wid=0, qual=0.8f, tilt=0.5f, shine=0.0f; int hard=1; };

// run blocks; input = sine at sf (amp), or saw partial-sum if sawIn, or silence sf<=0.
// capture: 0=wet-only (out-dry), 1=full output
struct Cap { std::vector<float> L, R; };
static Cap run (StellateNode& sn, int shape, const P& pp, const int* held, int nH,
                int blocks, int bs, double sf, int skip = 0, float amp = 0.5f,
                bool sawIn = false, int capMode = 0)
{
    Cap cap; std::vector<float> L (bs), R (bs), dl (bs);
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
            L[i] = s; R[i] = s; dl[i] = s;
        }
        sn.process (shape, pp.mix, pp.rep, pp.feed, pp.wid, pp.qual, pp.tilt, pp.shine, pp.hard,
                    held, nH, FS, L.data(), R.data(), bs);
        if (b >= skip)
            for (int i = 0; i < bs; ++i)
            {
                cap.L.push_back (capMode ? L[i] : L[i] - dl[i]);
                cap.R.push_back (capMode ? R[i] : R[i] - dl[i]);
            }
    }
    return cap;
}
static std::vector<float> mono (const Cap& c)
{ std::vector<float> m (c.L.size()); for (size_t i = 0; i < m.size(); ++i) m[i] = 0.5f * (c.L[i] + c.R[i]); return m; }

static const char* SHN[12] = { "SINE","TRIANGLE","SQUARE","SAW","PRIME","LATTICE",
                               "VEIL","HALO","EMBER","CROWN","PILLAR","GLACIER" };

int main()
{
    const int bs = 128;
    std::printf ("STELLATE V2 — spectral shaper validation\n");
    std::printf ("=========================================\n");

    std::printf ("\n[1] Mix=0 exact bypass\n");
    {
        StellateNode sn; sn.prepare (FS);
        int held[1] = { 60 }; P pp; pp.mix = 0.0f;
        std::vector<float> L (bs), R (bs), cl (bs), cr (bs);
        std::mt19937 rng (7); std::uniform_real_distribution<float> u (-0.8f, 0.8f);
        float md = 0.0f;
        for (int b = 0; b < 40; ++b)
        {
            for (int i = 0; i < bs; ++i) { L[i] = u (rng); R[i] = u (rng); cl[i] = L[i]; cr[i] = R[i]; }
            sn.process (3, 0.0f, 0.7f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0, held, 1, FS, L.data(), R.data(), bs);
            for (int i = 0; i < bs; ++i) md = std::max (md, std::max (std::fabs (L[i]-cl[i]), std::fabs (R[i]-cr[i])));
        }
        check (md < 1.0e-7f, "output == input at Mix 0 (even with toolkit hot)", "max|d|=" + std::to_string (md));
    }

    std::printf ("\n[2] SILENCE GUARANTEE — silent input, every shape (incl. feed up)\n");
    for (int s = 0; s < 12; ++s)
    {
        StellateNode sn; sn.prepare (FS);
        int held[1] = { 60 }; P pp; pp.feed = 1.0f; pp.rep = 1.0f;
        Cap c = run (sn, s, pp, held, 1, 60, bs, -1.0);
        const float pk = std::max (peakAbs (c.L), peakAbs (c.R));
        check (pk < 1.0e-5f, std::string (SHN[s]) + " silent in -> silent out", "peak=" + std::to_string (pk));
    }

    std::printf ("\n[3] PITCH TRACK — mis-rooted one-shot: stack locks to the AUDIO, not the key\n");
    {
        // input sine 37c SHARP of the MIDI note (classic un-mapped chop). HARD mode would
        // generate at the MIDI grid (37c flat of the audio); TRACK must land on the audio.
        const int note = 55; const double fMidi = noteHz (note);
        const double fAud = fMidi * std::pow (2.0, 37.0 / 1200.0);
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
        { StellateNode sn; sn.prepare (FS); P pp; pp.hard = 0;         // TRACK
          Cap c = run (sn, 2, pp, held, 1, 400, bs, fAud, 200); auto m = mono (c);
          const double off3 = centsAt (m, fAud * 3.0);                  // 3rd harmonic OF THE AUDIO
          check (std::fabs (off3) < 6.0, "TRACK: 3rd harmonic sits on the AUDIO's pitch (+37c source)",
                 std::to_string (off3) + "c from audio grid"); }
        { StellateNode sn; sn.prepare (FS); P pp; pp.hard = 0;          // flat source too
          const double fAud2 = fMidi * std::pow (2.0, -52.0 / 1200.0);
          Cap c = run (sn, 2, pp, held, 1, 400, bs, fAud2, 200); auto m = mono (c);
          const double off3 = centsAt (m, fAud2 * 3.0);
          check (std::fabs (off3) < 6.0, "TRACK: locks a -52c flat source too",
                 std::to_string (off3) + "c from audio grid"); }
        { StellateNode sn; sn.prepare (FS); P pp; pp.hard = 1;          // HARD stays on MIDI
          Cap c = run (sn, 2, pp, held, 1, 400, bs, fMidi, 200); auto m = mono (c);
          const double off3 = centsAt (m, fMidi * 3.0);
          check (std::fabs (off3) < 3.0, "HARD: in-tune source, stack exact on MIDI grid",
                 std::to_string (off3) + "c"); }
    }

    std::printf ("\n[4] CARVE — boost AND take away: source harmonics cancelled at Replace 1\n");
    {
        const int note = 48; const double f0 = noteHz (note); int held[1] = { note };
        // saw-ish input (8 harmonics). SINE shape + full replace: harmonics 2..8 should DUCK
        // hard vs bypass while the fundamental (kept by the shape) stays healthy.
        StellateNode dryRef; dryRef.prepare (FS); P p0; p0.mix = 0.0f;
        Cap cd = run (dryRef, 0, p0, held, 1, 260, bs, f0, 100, 0.4f, true, 1); auto md = mono (cd);
        StellateNode sn; sn.prepare (FS); P pp; pp.rep = 1.0f; pp.mix = 1.0f;
        Cap cw = run (sn, 0, pp, held, 1, 260, bs, f0, 100, 0.4f, true, 1); auto mw = mono (cw);
        const double h2d = goertzel (md.data(), (int) md.size(), f0*2), h2w = goertzel (mw.data(), (int) mw.size(), f0*2);
        const double h3d = goertzel (md.data(), (int) md.size(), f0*3), h3w = goertzel (mw.data(), (int) mw.size(), f0*3);
        const double f1w = goertzel (mw.data(), (int) mw.size(), f0);
        check (h2w < h2d * 0.35 && h3w < h3d * 0.35, "Replace 1: source 2nd/3rd harmonics carved away",
               "h2 " + std::to_string (h2w/h2d) + "x  h3 " + std::to_string (h3w/h3d) + "x of bypass");
        check (f1w > 1e-4, "fundamental survives (shape keeps it)", "E1=" + std::to_string (f1w));
    }

    std::printf ("\n[5] Per-shape spectra — laws hold (shine 0)\n");
    {
        const int note = 48; const double f0 = noteHz (note); int held[1] = { note };
        auto E = [&] (const std::vector<float>& m, double f) { return goertzel (m.data(), (int) m.size(), f); };
        { StellateNode sn; sn.prepare (FS); P pp;
          Cap c = run (sn, 0, pp, held, 1, 160, bs, f0, 60); auto m = mono (c);
          check (E (m, f0) > 10.0 * (E (m, f0*2) + E (m, f0*3)), "SINE: fundamental only",
                 "E1/(E2+E3)=" + std::to_string (E(m,f0)/(E(m,f0*2)+E(m,f0*3)+1e-12))); }
        { StellateNode sn; sn.prepare (FS); P pp;
          Cap c = run (sn, 2, pp, held, 1, 160, bs, f0, 60); auto m = mono (c);
          check (E (m, f0*3) > 6.0 * E (m, f0*2), "SQUARE: odd only (3 vs 2)",
                 "E3/E2=" + std::to_string (E(m,f0*3)/(E(m,f0*2)+1e-12))); }
        { StellateNode sn; sn.prepare (FS); P pp;
          Cap c = run (sn, 3, pp, held, 1, 160, bs, f0, 60); auto m = mono (c);
          const double r23 = E (m, f0*2) / (E (m, f0*3) + 1e-12);
          check (E (m, f0*2) > 1e-5 && r23 > 1.1 && r23 < 2.2, "SAW: all harmonics, 1/n slope",
                 "E2/E3=" + std::to_string (r23)); }
        { StellateNode sn; sn.prepare (FS); P pp;
          Cap c = run (sn, 1, pp, held, 1, 160, bs, f0, 60); auto m = mono (c);
          const double r13 = E (m, f0) / (E (m, f0*3) + 1e-12);
          check (r13 > 5.0, "TRIANGLE: 1/n^2 rolloff (1 vs 3)", "E1/E3=" + std::to_string (r13)); }
        { StellateNode sn; sn.prepare (FS); P pp;
          Cap c = run (sn, 4, pp, held, 1, 160, bs, f0, 60); auto m = mono (c);
          check (E (m, f0*3) > 8.0 * E (m, f0*4), "PRIME skips non-primes (3 vs 4)",
                 "E3/E4=" + std::to_string (E(m,f0*3)/(E(m,f0*4)+1e-12))); }
        { StellateNode sn; sn.prepare (FS); P pp;
          Cap c = run (sn, 6, pp, held, 1, 420, bs, f0, 100); auto m = mono (c);
          check (E (m, f0*0.5) > 3.0 * E (m, f0*3), "VEIL: sub-octave body (f0/2 vs 3f0)",
                 "sub/high=" + std::to_string (E(m,f0*0.5)/(E(m,f0*3)+1e-12))); }
        { StellateNode sn; sn.prepare (FS); P pp;
          Cap c = run (sn, 9, pp, held, 1, 160, bs, f0, 60); auto m = mono (c);
          check (E (m, f0*7) > 4.0 * E (m, f0*2), "CROWN: high harmonics only (7 vs 2)",
                 "E7/E2=" + std::to_string (E(m,f0*7)/(E(m,f0*2)+1e-12))); }
        { StellateNode sn; sn.prepare (FS); P ppq; ppq.qual = 0.9f;
          Cap cq = run (sn, 8, ppq, held, 1, 200, bs, f0, 80, 0.08f); auto mq = mono (cq);
          StellateNode sn2; sn2.prepare (FS);
          Cap cl = run (sn2, 8, ppq, held, 1, 200, bs, f0, 80, 0.85f); auto ml = mono (cl);
          const double rq = E (mq, f0*7) / (E (mq, f0) + 1e-12), rl = E (ml, f0*7) / (E (ml, f0) + 1e-12);
          check (rl > rq * 2.0, "EMBER tilt opens with input level",
                 "loud/quiet=" + std::to_string (rl/(rq+1e-12))); }
    }

    std::printf ("\n[6] STRICTLY HARMONIC AUDIT — every live partial on the integer grid, all 12 shapes\n");
    {
        const int note = 50; const double f0 = noteHz (note); int held[1] = { note };
        bool allOK = true; std::string worst;
        double worstC = 0;
        for (int s = 0; s < 12; ++s)
        {
            StellateNode sn; sn.prepare (FS); P pp;
            run (sn, s, pp, held, 1, 120, bs, f0, 0, 0.5f);
            for (int q = 0; q < sn.vizN; ++q)
            {
                const double r = sn.vizF[q] / f0;
                const double rq = (r >= 0.75) ? std::round (r) : (1.0 / std::round (1.0 / r)); // integers or 1/2,1/4
                const double cents = 1200.0 * std::log2 (r / rq);
                if (std::fabs (cents) > std::fabs (worstC)) { worstC = cents; worst = SHN[s]; }
                if (std::fabs (cents) > 6.0) allOK = false;
            }
        }
        check (allOK, "no inharmonic content anywhere (grid audit, 12 shapes)",
               "worst=" + std::to_string (worstC) + "c (" + worst + ")");
    }

    std::printf ("\n[7] TOOLKIT — shine, tilt, width, feed, quality\n");
    {
        const int note = 48; const double f0 = noteHz (note); int held[1] = { note };
        auto E = [&] (const std::vector<float>& m, double f) { return goertzel (m.data(), (int) m.size(), f); };
        { StellateNode sn; sn.prepare (FS); P pp; pp.shine = 0.5f;      // +1 octave lift
          Cap c = run (sn, 0, pp, held, 1, 160, bs, f0, 60); auto m = mono (c);
          check (E (m, f0*2) > 8.0 * E (m, f0), "SHINE 0.5 lifts SINE to +1 octave",
                 "E(2f0)/E(f0)=" + std::to_string (E(m,f0*2)/(E(m,f0)+1e-12))); }
        { StellateNode snA; snA.prepare (FS); P pa; pa.tilt = 0.08f;    // dark
          Cap ca = run (snA, 3, pa, held, 1, 160, bs, f0, 60); auto ma = mono (ca);
          StellateNode snB; snB.prepare (FS); P pb; pb.tilt = 0.92f;    // bright
          Cap cb = run (snB, 3, pb, held, 1, 160, bs, f0, 60); auto mb = mono (cb);
          const double rA = E (ma, f0*7) / (E (ma, f0) + 1e-12), rB = E (mb, f0*7) / (E (mb, f0) + 1e-12);
          check (rB > rA * 6.0, "TILT sweeps the harmonic filter dark -> bright",
                 "bright/dark 7th ratio=" + std::to_string (rB/(rA+1e-12))); }
        { StellateNode sn; sn.prepare (FS); P pp; pp.wid = 1.0f;        // width: partial 1 R, partial 2 L
          Cap c = run (sn, 3, pp, held, 1, 200, bs, f0, 80);
          const double e1R = goertzel (c.R.data(), (int) c.R.size(), f0);
          const double e1L = goertzel (c.L.data(), (int) c.L.size(), f0);
          const double e2R = goertzel (c.R.data(), (int) c.R.size(), f0*2);
          const double e2L = goertzel (c.L.data(), (int) c.L.size(), f0*2);
          check (e1R > e1L * 2.0 && e2L > e2R * 2.0, "WIDTH fans odd/even partials L/R",
                 "h1 R/L=" + std::to_string (e1R/(e1L+1e-12)) + " h2 L/R=" + std::to_string (e2L/(e2R+1e-12))); }
        { StellateNode snA; snA.prepare (FS); P pa; pa.feed = 0.0f;
          Cap ca = run (snA, 2, pa, held, 1, 220, bs, f0, 100); auto ma = mono (ca);
          StellateNode snB; snB.prepare (FS); P pb; pb.feed = 1.0f;
          Cap cb = run (snB, 2, pb, held, 1, 220, bs, f0, 100); auto mb = mono (cb);
          const double hiA = E (ma, f0*9) + E (ma, f0*5)*0.0 + E (ma, f0*15);
          const double hiB = E (mb, f0*9) + E (mb, f0*15);
          check (hiB > hiA * 1.5 && peakAbs (cb.L) < 1.5f, "FEED regenerates (richer highs) and stays bounded",
                 "hi B/A=" + std::to_string (hiB/(hiA+1e-12)) + " peak=" + std::to_string (peakAbs (cb.L))); }
        { StellateNode sn; sn.prepare (FS); P pp; pp.qual = 0.05f;      // full degradation
          Cap c = run (sn, 3, pp, held, 1, 220, bs, f0, 100);
          const float pk = peakAbs (c.L);
          check (pk > 0.04f && pk < 1.2f, "QUALITY floor: degraded but alive & bounded",
                 "peak=" + std::to_string (pk)); }
    }

    std::printf ("\n[8] Envelope tracking + polyphony + stability + viz\n");
    {
        for (int s : { 2, 9 })
        {
            StellateNode sn; sn.prepare (FS);
            const int note = 60; const double f0 = noteHz (note); int held[1] = { note }; P pp;
            run (sn, s, pp, held, 1, 120, bs, f0);
            Cap tail = run (sn, s, pp, held, 1, 80, bs, -1.0, 60);
            const float pk = std::max (peakAbs (tail.L), peakAbs (tail.R));
            check (pk < 0.02f, std::string (SHN[s]) + " wet follows input death (<-34 dB by ~160 ms)",
                   "tail peak=" + std::to_string (pk));
        }
        {
            StellateNode sn; sn.prepare (FS); P pp;
            const int nA = 48, nB = 55; int held[2] = { nA, nB };
            const double fA = noteHz (nA), fB = noteHz (nB);
            std::vector<float> L (bs), R (bs); Cap cap;
            double pa = 0, pb = 0;
            for (int b = 0; b < 200; ++b)
            {
                for (int i = 0; i < bs; ++i)
                { const float s = 0.35f * (float) (std::sin (2*M_PI*pa) + std::sin (2*M_PI*pb));
                  L[i] = s; R[i] = s; pa += fA/FS; if (pa>=1) pa-=1; pb += fB/FS; if (pb>=1) pb-=1; }
                std::vector<float> d (L);
                sn.process (2, 1, 0, 0, 0, 0.8f, 0.5f, 0.0f, 1, held, 2, FS, L.data(), R.data(), bs);
                if (b >= 80) for (int i = 0; i < bs; ++i) cap.L.push_back (L[i]-d[i]);
            }
            const double eA = goertzel (cap.L.data(), (int) cap.L.size(), fA*3);
            const double eB = goertzel (cap.L.data(), (int) cap.L.size(), fB*3);
            const double off = goertzel (cap.L.data(), (int) cap.L.size(), fA*3.41) + 1e-12;
            check (eA > off*4 && eB > off*4, "polyphony: both notes' stacks live",
                   "A/off=" + std::to_string (eA/off) + " B/off=" + std::to_string (eB/off));
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
                sn.process ((b/12) % 12, 1.0f, 1.0f, 1.0f, 1.0f, (b%2) ? 0.1f : 0.95f, 0.9f, 0.9f, (b/24)&1,
                            held, 3, FS, L.data(), R.data(), bs);
                for (int i = 0; i < bs; ++i)
                { if (! (L[i] == L[i]) || std::fabs (L[i]) > 5.0f) ok = false;
                  pk = std::max (pk, std::fabs (L[i])); }
            }
            check (ok, "torture: noise + all params slamming, finite & bounded", "peak=" + std::to_string (pk));
        }
        {
            StellateNode sn; sn.prepare (FS); P pp; pp.shine = 1.0f;    // +2 oct on a high note
            const int note = 106; const double f0 = noteHz (note); int held[1] = { note };
            run (sn, 9, pp, held, 1, 80, bs, f0);
            bool ok = true;
            for (int q = 0; q < sn.vizN; ++q) if (sn.vizF[q] > 0.46f * FS) ok = false;
            check (ok, "note 106 + shine +2: alias skip holds", "vizN=" + std::to_string (sn.vizN));
        }
        {
            StellateNode sn; sn.prepare (FS); P pp; pp.mix = 0.8f;
            const int note = 60; const double f0 = noteHz (note); int held[1] = { note };
            run (sn, 3, pp, held, 1, 120, bs, f0);
            bool ok = sn.vizN >= 4 && sn.vizOut > 0.02f;
            for (int q = 0; q < sn.vizN; ++q) if (! (sn.vizM[q] >= 0.0f && sn.vizM[q] <= 1.0f)) ok = false;
            check (ok, "viz feed live + sane", "vizN=" + std::to_string (sn.vizN) + " out=" + std::to_string (sn.vizOut));
        }
        {
            for (int s = 0; s < 12; ++s)
            {
                StellateNode sn; sn.prepare (FS); P pp;
                const int note = 57; const double f0 = noteHz (note); int held[1] = { note };
                Cap c = run (sn, s, pp, held, 1, 160, bs, f0, 60);
                const float pk = std::max (peakAbs (c.L), peakAbs (c.R));
                check (pk > 0.04f && pk < 0.98f, std::string (SHN[s]) + " level healthy",
                       "peak=" + std::to_string (pk));
            }
        }
    }

    std::printf ("\n=========================================\n");
    std::printf ("RESULT: %d passed, %d failed\n", gPass, gFail);
    return gFail == 0 ? 0 : 1;
}
