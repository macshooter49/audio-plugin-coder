// ══════════════════════════════════════════════════════════════════════════════════════════════
//  ShaperTargets_test.cpp — tp92 · EVERY TARGET ON THE NATIVE LANES MOVES THE SOUND (offline, bit-exact).
//
//  Max: "some of these targets really don't move anything or they're not drastic enough." Each target of
//  Volume · Time · Filter · Pan · Repeat · Phaser (both built-ins) · Crush swept 0.03 → 0.97 on a fixed
//  chord, with a shape that EXERCISES it, scored on its own terms: the larger of the mid spectrum, the side
//  spectrum and the 5 ms envelope. This runs FlowShaper with no ext, so it is deterministic to the bit —
//  two runs hash identically, the noise floor is zero, and 3 dB is a hard bar. (The in-plugin sweep,
//  Tests/au_shaper_targets.cpp, has a 2–17 dB floor from the synth's free-running modulators — it finds
//  candidates; this file decides.) Drive and the roster types need the processor's engines: their Bias /
//  Knee fix is proven where the engine is, see Tests/README tp92.
//  FOUND DEAD OR TOOTHLESS, and fixed at tp92: Volume Attack 0.16 / Release 0.30 dB of envelope (they
//  scaled with Smooth only) · Phaser Drive 0.00 on the two built-ins (never read) · the Flanger type's
//  Centre 0.00 (never read) · Filter Punch (a BOOL into a follower the lane runs at zero depth).
//  EXEMPT, by the source not the knob: Pan Width scales EXISTING sides (0 on this mono chord, 22 dB on the
//  synth's stereo in the plugin) · the Filter fallback has no Spread (the roster engine does: 37 dB).
//
//  g++ -std=c++17 -O2 -ISource Source/ShaperTargets_test.cpp -o /tmp/shtgt && /tmp/shtgt
// ══════════════════════════════════════════════════════════════════════════════════════════════
#include "FlowShaper.h"
#include <cstdio>
#include <vector>
#include <cmath>
#include <algorithm>
using namespace wc;
static const double SR = 48000, BPM = 120, FPB = SR * 60 / BPM;
static double g (const std::vector<float>& x, size_t a, size_t n, double f)
{ const double w = 2 * M_PI * f / SR, c = 2 * std::cos (w); double s1 = 0, s2 = 0; for (size_t i = a; i < a + n; ++i) { double s0 = x[i] + c * s1 - s2; s2 = s1; s1 = s0; } return std::sqrt (std::max (0.0, s1*s1 + s2*s2 - c*s1*s2)) / n; }
static double spec (const std::vector<float>& u, const std::vector<float>& v, size_t a, size_t n)
{ double ref = 0, worst = 0; std::vector<double> P, Q; for (int b = 0; b < 32; ++b) { double f = 60 * std::pow (16000 / 60.0, b / 31.0); P.push_back (g (u, a, n, f)); Q.push_back (g (v, a, n, f)); ref = std::max (ref, std::max (P.back(), Q.back())); }
  if (ref < 1e-9) return 0; for (int b = 0; b < 32; ++b) if (std::max (P[b], Q[b]) > 0.02 * ref) worst = std::max (worst, std::fabs (20 * std::log10 ((P[b] + 1e-12) / (Q[b] + 1e-12)))); return worst; }
static double rmsDb (const std::vector<float>& x, size_t a, size_t b) { double e = 0; for (size_t i = a; i < b; ++i) e += (double) x[i] * x[i]; return 10 * std::log10 (e / (b - a) + 1e-30); }
static double env (const std::vector<float>& a, const std::vector<float>& b, size_t A, size_t B)
{ const size_t F = 240; std::vector<double> x, y; double pk = -300; for (size_t i = A; i + F <= B; i += F) { x.push_back (rmsDb (a, i, i + F)); y.push_back (rmsDb (b, i, i + F)); pk = std::max (pk, std::max (x.back(), y.back())); }
  double s = 0; for (size_t q = 0; q < x.size(); ++q) s += std::fabs (std::max (x[q], pk - 40) - std::max (y[q], pk - 40)); return s / x.size(); }
static float gate8 (double p) { return ((int) (p * 16) & 1) ? 0.f : 1.f; }
static float stairs (double p) { return 1.f - (float) ((int) (p * 8)) / 8.f; }
static float sine (double p) { return (float) (0.5 - 0.5 * std::cos (2 * M_PI * p)); }
static float flat5 (double) { return 0.5f; }
static float open1 (double) { return 1.f; }
static float step8 (double p) { return ((int) (p * 8) & 1) ? 0.3f : 0.8f; }
static void render (int kind, int mode, const float k[6], float (*shape) (double), std::vector<float>& M, std::vector<float>& S)
{
    FlowShaper fs; fs.prepare (SR); fs.armRing(); auto st = std::make_shared<ShaperState>();
    for (auto& L : st->lanes) L.fill (flat5);
    auto& L = st->lanes[kind]; L.fill (shape); for (int q = 0; q < 6; ++q) L.k[q] = k[q]; L.smooth = 0.02f; L.blend = 1;
    st->slot[0] = kind; for (int q = 1, c = 0; q < 8; ++c) if (c != kind) st->slot[q++] = c;
    fs.setState (st);
    auto& V = st->lanes[kind]; (void) V;
    fs.setLaneCtl (kind, true, 1.0f, 4, mode, 0);   // on, depth 1, 1 bar, the mode, sync
    const int N = (int) (SR * 3); std::vector<float> l (N), r (N);
    for (int i = 0; i < N; ++i) { double t = i / SR; float x = (float) (0.2 * std::sin (2 * M_PI * 130.81 * t) + 0.15 * std::sin (2 * M_PI * 196 * t) + 0.12 * std::sin (2 * M_PI * 261.63 * t)
                                                                  + 0.05 * std::sin (2 * M_PI * 1046.5 * t) + 0.03 * std::sin (2 * M_PI * 3136 * t)); l[i] = x; r[i] = x; }
    double ppq = 0; for (int i = 0; i < N; i += 512) { int n = std::min (512, N - i); fs.process (l.data() + i, r.data() + i, n, ppq, BPM, true, 1.0f); ppq += n / FPB; }
    M.resize (N); S.resize (N); for (int i = 0; i < N; ++i) { M[i] = 0.5f * (l[i] + r[i]); S[i] = 0.5f * (l[i] - r[i]); }
}
struct T { int kind, mode; const char* n[4]; int ki[4]; float k[6]; float (*sh) (double); const char* lane; };
int main()
{
    int weak = 0, total = 0;
    static const T L[] = {
        { 0, 0, { "Attack","Release","Punch","Hold" }, { 0,1,2,3 }, { .5f,.5f,0,0,.5f,.5f }, gate8,  "Volume" },
        { 1, 0, { "Fade","Glide","Range","Tone" },     { 0,1,2,3 }, { .3f,0,.5f,.5f,.5f,.5f }, stairs, "Time" },
        { 2, 0, { "Reso","Drive","Spread","Punch" },   { 0,1,4,5 }, { .3f,0,1,0,0,0 }, step8, "Filter (built-in fallback)" },
        { 3, 0, { "Width","Bass","Haas","Tilt" },      { 0,1,2,3 }, { .5f,0,0,.5f,.5f,.5f }, sine,   "Pan" },
        { 4, 0, { "Seam","Decay","Pitch","Tone" },     { 0,1,2,3 }, { .3f,0,.5f,.5f,.5f,.5f }, flat5,  "Repeat" },
        { 6, 0, { "Feedbk","Stereo","Drive","Centre" },{ 0,1,2,3 }, { .5f,.5f,0,.2f,.5f,.5f }, sine,   "Phaser" },
        { 6, 1, { "Feedbk","Stereo","Drive","Centre" },{ 0,1,2,3 }, { .5f,.5f,0,.2f,.5f,.5f }, sine,   "Phaser: Flanger type" },
        { 7, 0, { "Bits","Rate","Tone","Stereo" },     { 0,1,2,3 }, { .5f,.5f,1,0,.5f,.5f }, open1,  "Crush" } };
    for (const T& t : L)
    {
        printf ("  %s\n", t.lane);
        for (int q = 0; q < 4; ++q)
        {
            float lo[6], hi[6]; for (int z = 0; z < 6; ++z) lo[z] = hi[z] = t.k[z]; lo[t.ki[q]] = 0.03f; hi[t.ki[q]] = 0.97f;
            std::vector<float> m1, s1, m2, s2; render (t.kind, t.mode, lo, t.sh, m1, s1); render (t.kind, t.mode, hi, t.sh, m2, s2);
            const size_t A = (size_t) SR, n = m1.size() - A;
            const double sp = spec (m1, m2, A, n), sd = std::max (rmsDb (s1, A, m1.size()), rmsDb (s2, A, m1.size())) > -60 ? spec (s1, s2, A, n) : 0.0, ev = env (m1, m2, A, m1.size());
            const double best = std::max (sp, std::max (sd, ev));
            const bool exempt = (t.kind == 3 && q == 0) || (t.kind == 2 && q == 2);
            const bool bad = best < 3.0 && ! exempt; weak += bad; ++total;
            printf ("    %-8s spec %6.2f · side %6.2f · env %6.2f  %s\n", t.n[q], sp, sd, ev, bad ? "<<< WEAK" : exempt ? "(exempt: mono source)" : "PASS");
        }
    }
    printf ("\n%d targets, %d weak\n%s\n", total, weak, weak ? "SOME TARGETS ARE WEAK" : "ALL TARGETS MOVE THE SOUND");
    return weak ? 1 : 0;
}
