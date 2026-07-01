// ResonatorNode_test.cpp — offline validation for the Annulus hybrid engine.
//   g++ -std=c++17 -O2 -Wall -Wextra -Wpedantic ResonatorNode_test.cpp -o /tmp/rn && /tmp/rn
#include "ResonatorNode.h"
#include <cstdio>
#include <cmath>
#include <vector>
#include <random>
#include <string>

using wc::ResonatorNode;
static const double FS = 48000.0;
static int gPass = 0, gFail = 0;

static void check (bool ok, const std::string& name, const std::string& info = "")
{
    std::printf ("  [%s] %s%s%s\n", ok ? "PASS" : "FAIL", name.c_str(),
                 info.empty() ? "" : "  — ", info.c_str());
    if (ok) ++gPass; else ++gFail;
}
static double goertzel (const float* x, int N, double f)
{
    const double w = 2.0 * M_PI * f / FS, coeff = 2.0 * std::cos (w);
    double s1 = 0, s2 = 0;
    for (int n = 0; n < N; ++n) { const double s0 = x[n] + coeff * s1 - s2; s2 = s1; s1 = s0; }
    const double p = s1 * s1 + s2 * s2 - coeff * s1 * s2;
    return std::sqrt (p > 0 ? p : 0) / N;
}
static double rmsv (const float* x, int N)
{ double a = 0; for (int n = 0; n < N; ++n) a += (double) x[n] * x[n]; return std::sqrt (a / N); }
static double peakv (const std::vector<float>& x)
{ double mx = 0; for (float v : x) mx = std::max (mx, (double) std::fabs (v)); return mx; }
static bool finiteBounded (const float* x, int N, float lim = 1.0001f)
{ for (int n = 0; n < N; ++n) { if (!(x[n] == x[n]) || x[n] > lim || x[n] < -lim) return false; } return true; }
static double noteHz (int note) { return 440.0 * std::pow (2.0, (note - 69) / 12.0); }

struct Params { float structure=0.4f,bright=0.55f,damp=0.45f,pos=0.2f,mix=1.0f,kt=1.0f; int material=0; };
template <typename Gen>
static void run (ResonatorNode& rn, const Params& p, const int* held, int nHeld,
                 int blocks, int bs, Gen gen, std::vector<float>* cap = nullptr, int capFrom = 0)
{
    std::vector<float> L (bs), R (bs);
    for (int b = 0; b < blocks; ++b)
    {
        for (int i = 0; i < bs; ++i) { float s = gen (b, i); L[i] = s; R[i] = s; }
        rn.process (p.structure,p.bright,p.damp,p.pos,p.material,p.mix,p.kt, held,nHeld, FS, L.data(),R.data(),bs);
        if (cap && b >= capFrom) for (int i = 0; i < bs; ++i) cap->push_back (L[i]);
    }
}

int main()
{
    std::mt19937 rng (1);
    std::uniform_real_distribution<float> uni (-1.0f, 1.0f);
    auto noise  = [&] (int, int) { return 0.25f * uni (rng); };
    auto silent = [] (int, int) { return 0.0f; };
    const int bs = 128;
    const char* matName[6] = { "String", "Pluck", "Piano", "Bar", "Metal", "Drum" };

    std::printf ("ANNULUS ResonatorNode — hybrid engine validation\n");
    std::printf ("================================================\n");

    // ── 1. Mix=0 EXACT bypass ───────────────────────────────────────────────
    std::printf ("\n[1] Mix=0 exact bypass\n");
    {
        ResonatorNode rn; rn.prepare (FS);
        int held[1] = { 60 };
        std::vector<float> in; std::mt19937 r2 (7); std::uniform_real_distribution<float> u2 (-1, 1);
        std::vector<float> L (bs), R (bs); bool exact = true; double md = 0;
        for (int b = 0; b < 40; ++b)
        {
            for (int i = 0; i < bs; ++i) { float s = 0.3f * u2 (r2); L[i] = s; R[i] = s; in.push_back (s); }
            rn.process (0.4f,0.55f,0.45f,0.2f,0,0.0f,1.0f, held,1, FS, L.data(),R.data(),bs);
            for (int i = 0; i < bs; ++i) { double d = std::fabs (L[i]-in[b*bs+i]); if (d>md) md=d; if (d>1e-6) exact=false; }
        }
        check (exact, "output == input at Mix 0", "max|d|=" + std::to_string (md));
    }

    // ── 2. Playable strike rings with NO input + healthy level (not railed) ──
    std::printf ("\n[2] Note-on strike: rings, decays, level in healthy range\n");
    for (int m = 0; m < 6; ++m)
    {
        ResonatorNode rn; rn.prepare (FS);
        Params p; p.material=m; p.mix=1.0f; p.damp=0.35f;
        int held[1] = { 57 };
        std::vector<float> ring; run (rn,p,held,1, 60,bs, silent, &ring, 6);
        const double e = rmsv (ring.data(),(int)ring.size()), pk = peakv (ring);
        std::vector<float> after; run (rn,p,nullptr,0, 120,bs, silent, &after, 80);
        const double eA = rmsv (after.data(),(int)after.size());
        check (e > 1e-4, std::string(matName[m]) + " rings from strike", "rms=" + std::to_string(e));
        check (eA < e,   std::string(matName[m]) + " decays after release", "tail=" + std::to_string(eA));
        check (pk > 0.04 && pk < 0.97, std::string(matName[m]) + " strike peak healthy (not railed)", "peak=" + std::to_string(pk));
    }

    // ── 3. Modal energy concentrates on the physics tables (strike → ring) ──
    std::printf ("\n[3] Modal partials concentrate on the acoustics tables\n");
    {
        const std::vector<double> barR = { 1.0, 4.0, 9.2, 16.7, 25.0 };
        const std::vector<double> belR = { 0.5, 1.0, 1.2, 1.5, 2.0, 2.5, 3.0, 4.2, 5.4 };
        const std::vector<double> drmR = { 1.0, 1.593, 2.136, 2.296, 2.653, 2.917, 3.156, 3.500, 3.598 };
        struct MT { int mat; const char* name; const std::vector<double>* r; };
        MT tests[3] = { { 3, "Bar", &barR }, { 4, "Metal", &belR }, { 5, "Drum", &drmR } };
        const int note = 60; const double f0 = noteHz (note);
        for (auto& t : tests)
        {
            ResonatorNode rn; rn.prepare (FS);
            Params p; p.material=t.mat; p.mix=1.0f; p.damp=0.18f; p.bright=1.0f; p.structure=0.0f; p.pos=0.3f;
            int held[1] = { note };
            std::vector<float> cap; run (rn,p,held,1, 70,bs, silent, &cap, 2);   // strike, capture full ring
            const int N = (int) cap.size();
            const auto& R = *t.r;
            double onSum = 0, offSum = 0;
            for (size_t k = 0; k < R.size(); ++k) onSum += goertzel (cap.data(), N, f0 * R[k]);
            for (size_t k = 0; k + 1 < R.size(); ++k)
                offSum += goertzel (cap.data(), N, f0 * std::sqrt (R[k] * R[k+1]));   // geo-mean midpoints
            offSum += 1e-12;
            check (onSum > 3.0 * offSum, std::string(t.name) + " ring energy sits on the mode ratios",
                   "onSum/offSum=" + std::to_string (onSum / offSum));
        }
    }

    // ── 4. Waveguide String harmonic; Piano stretched (inharmonic) ──────────
    std::printf ("\n[4] Waveguide harmonicity\n");
    {
        const int note = 48; const double f0 = noteHz (note);
        {
            ResonatorNode rn; rn.prepare (FS);
            Params p; p.material=0; p.mix=1.0f; p.damp=0.2f; p.bright=0.8f; p.structure=0.0f;
            int held[1] = { note };
            std::vector<float> cap; run (rn,p,held,1, 120,bs, noise, &cap, 50);
            const int N = (int) cap.size();
            const double e = goertzel (cap.data(),N, 2.0*f0), es = goertzel (cap.data(),N, 2.0*f0*1.012);
            check (e > es, "String 2nd partial at exact 2*f0 (harmonic)", "exact/sharp=" + std::to_string(e/(es+1e-12)));
        }
        {
            // STRUCTURE=1 → heavy stiffness. The 4th partial must ride SHARP of 4*f0.
            // Scan a band and confirm the spectral peak sits above the harmonic, not on it.
            ResonatorNode rn; rn.prepare (FS);
            Params p; p.material=2; p.mix=1.0f; p.damp=0.15f; p.bright=0.85f; p.structure=1.0f;
            int held[1] = { note };
            std::vector<float> cap; run (rn,p,held,1, 140,bs, noise, &cap, 60);
            const int N = (int) cap.size();
            const double eHarm = goertzel (cap.data(),N, 4.0*f0);
            double ePeak = 0.0, fPeak = 4.0*f0;
            for (double f = 4.0*f0; f <= 4.8*f0; f += 1.0)
            { const double e = goertzel (cap.data(),N, f); if (e > ePeak) { ePeak = e; fPeak = f; } }
            check (fPeak > 4.02*f0 && ePeak > eHarm, "Piano upper partial stretched sharp (stiffness)",
                   "peak@" + std::to_string(fPeak/f0) + "*f0 vs harmonic 4.0");
        }
    }

    // ── 5. Polyphony — two struck notes both ring (off-point not a harmonic) ─
    std::printf ("\n[5] Polyphony\n");
    {
        ResonatorNode rn; rn.prepare (FS);
        Params p; p.material=0; p.mix=1.0f; p.damp=0.25f; p.bright=0.8f; p.structure=0.0f; // clean harmonic: isolates polyphony
        int held[2] = { 50, 57 };                       // D3 146.8, A3 220.0
        const double fA = noteHz(50), fB = noteHz(57);
        std::vector<float> cap; run (rn,p,held,2, 100,bs, noise, &cap, 40);
        const int N = (int) cap.size();
        const double eA = goertzel (cap.data(),N, fA), eB = goertzel (cap.data(),N, fB);
        const double eOff = goertzel (cap.data(),N, 181.0) + 1e-12;  // between, not a harmonic of either
        check (eA > 2.0*eOff && eB > 2.0*eOff, "both held pitches present",
               "A/off=" + std::to_string(eA/eOff) + " B/off=" + std::to_string(eB/eOff));
    }

    // ── 6. Stability — full poly, full drive, loud input ────────────────────
    std::printf ("\n[6] Stability under stress\n");
    for (int m = 0; m < 6; ++m)
    {
        ResonatorNode rn; rn.prepare (FS);
        Params p; p.material=m; p.mix=1.0f; p.damp=0.0f; p.bright=1.0f; p.structure=1.0f; p.pos=0.7f;
        int held[6] = { 40, 47, 52, 55, 59, 64 };
        std::mt19937 r3 (m+3); std::uniform_real_distribution<float> u3 (-1,1);
        auto loud = [&] (int,int) { return 0.9f * u3 (r3); };
        std::vector<float> cap; run (rn,p,held,6, 200,bs, loud, &cap, 0);
        check (finiteBounded (cap.data(),(int)cap.size()), std::string(matName[m]) + " finite & in [-1,1] under stress",
               "peak=" + std::to_string(peakv(cap)));
    }

    // ── 7. No portamento (pitch snaps) ──────────────────────────────────────
    std::printf ("\n[7] No portamento (pitch snaps on note change)\n");
    {
        ResonatorNode rn; rn.prepare (FS);
        Params p; p.material=0; p.mix=1.0f; p.damp=0.3f; p.bright=0.8f;
        int hA[1] = { 48 }; int hB[1] = { 60 };
        run (rn,p,hA,1, 30,bs, noise, nullptr);
        std::vector<float> cap; run (rn,p,hB,1, 60,bs, noise, &cap, 10);
        const int N = (int) cap.size();
        const double eN = goertzel (cap.data(),N, noteHz(60)), eO = goertzel (cap.data(),N, noteHz(48));
        check (eN > eO, "new note dominates quickly (snap, not glide)", "new/old=" + std::to_string(eN/(eO+1e-12)));
    }

    // ── 8. Macro reactivity (night & day) ───────────────────────────────────
    std::printf ("\n[8] Macro reactivity\n");
    {
        auto ringEnergy = [&] (int mat, float damp) {
            ResonatorNode rn; rn.prepare (FS);
            Params p; p.material=mat; p.mix=1.0f; p.damp=damp; p.bright=0.7f;
            int held[1] = { 57 };
            run (rn,p,held,1, 40,bs, noise, nullptr);
            std::vector<float> after; run (rn,p,nullptr,0, 80,bs, silent, &after, 20);
            return rmsv (after.data(),(int)after.size());
        };
        for (int m = 0; m < 6; ++m)
        {
            const double lng = ringEnergy (m, 0.05f), sht = ringEnergy (m, 0.95f);
            check (lng > sht*2.0, std::string(matName[m]) + " DAMPING long>>short tail", "long/short=" + std::to_string(lng/(sht+1e-9)));
        }
        // Brightness on modal Metal — upper bell modes get much louder as it opens up.
        // BRIGHTNESS shapes the note-on mallet, so (like POSITION) it is settled BEFORE the strike.
        {
            const std::vector<double> belR = { 0.5, 1.0, 1.2, 1.5, 2.0, 2.5, 3.0, 4.2, 5.4 };
            auto upperLower = [&] (float bright) {
                ResonatorNode rn; rn.prepare (FS);
                Params p; p.material=4; p.mix=1.0f; p.bright=bright; p.damp=0.22f; p.structure=1.0f;
                std::vector<float> L (bs), R (bs);
                for (int b = 0; b < 24; ++b)   // pre-roll: settle BRIGHTNESS with no note held
                { for (int i=0;i<bs;++i){L[i]=0.0f;R[i]=0.0f;} rn.process (p.structure,p.bright,p.damp,p.pos,p.material,p.mix,p.kt, nullptr,0, FS, L.data(),R.data(),bs); }
                int held[1] = { 60 };          // now strike — brightness is fully settled
                std::vector<float> cap; run (rn,p,held,1, 20,bs, silent, &cap, 1);
                const int N = (int) cap.size(); const double f0 = noteHz(60);
                double up=0, lo=0;
                for (double r : belR) { double e = goertzel (cap.data(),N, f0*r); if (r >= 1.2) up += e; else lo += e; }
                return up / (lo + 1e-12);
            };
            const double dark = upperLower (0.05f), brite = upperLower (0.98f);
            check (brite > dark*3.0, "Metal BRIGHTNESS opens up the upper modes", "bright/dark=" + std::to_string(brite/(dark+1e-12)));
        }
        // STRUCTURE is PITCH-LOCKED — turning it must NOT move the fundamental (the detuning fix).
        // Scan a ±70-cent window around f0 at structure 0 and 1; the spectral peak must stay put.
        {
            auto peakCents = [&] (int mat, int note, float structure) {
                const double f0 = noteHz (note);
                ResonatorNode rn; rn.prepare (FS);
                Params p; p.material=mat; p.mix=1.0f; p.damp=0.15f; p.bright=0.7f; p.structure=structure;
                int held[1] = { note };
                std::vector<float> cap; run (rn,p,held,1, 160,bs, silent, &cap, 6);   // struck tone → clean pitch
                const int N = (int) cap.size();
                double ePk = 0.0, fPk = f0;
                for (double c = -70; c <= 70; c += 1.0)
                { const double f = f0 * std::pow (2.0, c/1200.0); const double e = goertzel (cap.data(),N,f);
                  if (e > ePk) { ePk = e; fPk = f; } }
                return 1200.0 * std::log2 (fPk / f0);   // cents off f0
            };
            const double sA = peakCents (0, 45, 0.0f), sB = peakCents (0, 45, 1.0f);   // String
            check (std::fabs (sA) < 25 && std::fabs (sB) < 25 && std::fabs (sA - sB) < 20,
                   "String STRUCTURE stays in tune (no detune)",
                   "fundamental: " + std::to_string(sA) + "c -> " + std::to_string(sB) + "c");
            const double mA = peakCents (3, 43, 0.0f), mB = peakCents (3, 43, 1.0f);   // Bar (modal)
            check (std::fabs (mA) < 25 && std::fabs (mB) < 25,
                   "Bar STRUCTURE stays in tune (no detune)",
                   "fundamental: " + std::to_string(mA) + "c -> " + std::to_string(mB) + "c");
        }
        // STRUCTURE must DO something — it morphs harmonic content (purity → richness), pitch-locked.
        // Modal: high structure brings up the upper modes vs the fundamental. Waveguide: high
        // structure adds upper-harmonic energy. Both measured at fixed pitch.
        {
            const std::vector<double> barR = { 1.0, 4.0, 9.2, 16.7, 25.0 };
            const int note = 55; const double f0 = noteHz (55);
            auto upperRatio = [&] (float structure) {
                ResonatorNode rn; rn.prepare (FS);
                Params p; p.material=3; p.mix=1.0f; p.damp=0.18f; p.bright=0.6f; p.structure=structure;
                int held[1] = { note };
                std::vector<float> cap; run (rn,p,held,1, 60,bs, silent, &cap, 2);
                const int N = (int) cap.size();
                double up=0; for (size_t k=1;k<barR.size();++k) up += goertzel (cap.data(),N, f0*barR[k]);
                const double fund = goertzel (cap.data(),N, f0*barR[0]) + 1e-12;
                return up / fund;
            };
            const double pure = upperRatio (0.0f), rich = upperRatio (1.0f);
            check (rich > pure*1.5, "Bar STRUCTURE morphs purity->richness (pitch-locked)",
                   "upper/fund: " + std::to_string(pure) + " -> " + std::to_string(rich));
        }
        // TUNING ACCURACY — "a C gives a C" on EVERY material. Play C3 (48), the strongest
        // partial in a ±70-cent window must land within 25 cents of the note. This is the fix.
        {
            const int note = 48; const double f0 = noteHz (48);
            for (int m = 0; m < 6; ++m)
            {
                ResonatorNode rn; rn.prepare (FS);
                Params p; p.material=m; p.mix=1.0f; p.damp=0.20f; p.bright=0.6f; p.structure=0.30f;
                int held[1] = { note };
                std::vector<float> cap; run (rn,p,held,1, 140,bs, silent, &cap, 5);   // struck tone → clean pitch
                const int N = (int) cap.size();
                double ePk=0.0, fPk=f0;
                for (double c=-70; c<=70; c+=1.0)
                { const double f=f0*std::pow(2.0,c/1200.0); const double e=goertzel(cap.data(),N,f);
                  if (e>ePk){ePk=e; fPk=f;} }
                const double cents = 1200.0*std::log2 (fPk/f0);
                check (std::fabs (cents) < 25.0, std::string(matName[m]) + " tunes to the played note (C=C)",
                       "peak " + std::to_string(cents) + "c off C3");
            }
        }
        // Position on modal Bar — strike position must re-weight the modes (nodes null out).
        // POSITION is settled BEFORE the strike (as a player sets the knob, then plays).
        {
            const std::vector<double> barR = { 1.0, 4.0, 9.2, 16.7, 25.0 };
            const int note = 55; const double f0 = noteHz (55);
            auto ratio2over1 = [&] (float pos) {
                ResonatorNode rn; rn.prepare (FS);
                Params p; p.material=3; p.mix=1.0f; p.damp=0.25f; p.bright=1.0f; p.structure=0.0f; p.pos=pos;
                std::vector<float> L (bs), R (bs);
                for (int b = 0; b < 24; ++b)   // pre-roll: settle POSITION with no note held
                { for (int i=0;i<bs;++i){L[i]=0.0f;R[i]=0.0f;} rn.process (p.structure,p.bright,p.damp,p.pos,p.material,p.mix,p.kt, nullptr,0, FS, L.data(),R.data(),bs); }
                int held[1] = { note };        // now strike — position is fully settled
                std::vector<float> cap; run (rn,p,held,1, 40,bs, silent, &cap, 2);
                const int N = (int) cap.size();
                const double m1 = goertzel (cap.data(),N, f0*barR[1]);   // mode 2 (4*f0)
                const double m0 = goertzel (cap.data(),N, f0*barR[0]) + 1e-12; // mode 1 (f0)
                return m1 / m0;
            };
            const double near = ratio2over1 (0.05f), far = ratio2over1 (0.95f);
            const double spread = (near > far) ? near / (far + 1e-9) : far / (near + 1e-9);
            check (spread > 1.5, "Bar POSITION re-weights the modal balance (strike-node nulling)",
                   "mode2/mode1 changes " + std::to_string(spread) + "x across position");
        }
    }
    std::printf ("\n================================================\n");
    std::printf ("RESULT: %d passed, %d failed\n", gPass, gFail);
    return gFail == 0 ? 0 : 1;
}
