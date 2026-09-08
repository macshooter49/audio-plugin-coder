// ══════════════════════════════════════════════════════════════════════════════════════════════
//  bank2_probe.cpp — the ENGINE-DERIVED half of the Terrain factory wavetable bank.
//
//  These are the tables no other wavetable synth can ship, because no other wavetable synth has
//  these engines to render from: a 512-partial additive bank with six spectral families, and a
//  physical-modelling engine with THREE different cores behind nine instrument families.
//
//  ⚠️ CORRECTED DETECTOR. bank_probe reported GRAND as "*** SILENT ***" because it gated on
//  modeCountForTesting() > 0. That was the wrong question: ModalEngine::coreFor() routes
//  GRAND/PLUCK to a STRING WAVEGUIDE and BOW/FLUTE/REED/BRASS to a REEDBORE core — neither
//  populates the modal bank, so nModes_ is legitimately 0 while the engine is making sound
//  (measured peak 0.459). The gate is now on AUDIO, which is the property we actually need.
//
//  Build: c++ -std=c++17 -O2 -I Tests/shim -I Source <this> -framework Accelerate -o /tmp/bank2
// ══════════════════════════════════════════════════════════════════════════════════════════════
#include "HarmonicEngine.h"
#include "ModalEngine.h"
#include <cstdio>
#include <cstdint>
#include <vector>
#include <cmath>
#include <algorithm>

using namespace tw;

static constexpr int    kFrames   = 128;
static constexpr int    kHarm     = 1024;
static constexpr int    kModalLen = 16384;
static constexpr double SR        = 44100.0;
static constexpr double kAnaHz    = SR / 2048.0;   // 21.53 Hz — one frame IS one period
static constexpr double kModalHz  = 55.0;   // A1 — halving f0 doubles the projection ceiling to 400 harmonics, so families stop pinning at the same number and become distinguishable

static float lerpf (float a, float b, float t) { return a + (b - a) * t; }

struct HS { const char* n; int fam; float hue0,hue1, c0,c1, l0,l1, cv0,cv1; int sculpt; };
struct MS { const char* n; int fam; int form; float m0,m1, s0,s1, h0,h1, p0,p1, dec, breath; };

static void dumpH (const char* dir, const HS& s)
{
    std::vector<float> out ((size_t) kFrames * kHarm, 0.f);
    int mx = 0; double pk = 0.0;
    for (int f = 0; f < kFrames; ++f)
    {
        const float t = (float) f / (float) (kFrames - 1);
        HarmParams p;
        p.mainMode = s.fam; p.sculptMode = s.sculpt;
        p.hue   = lerpf (s.hue0, s.hue1, t);
        p.count = lerpf (s.c0,   s.c1,   t);
        p.lean  = lerpf (s.l0,   s.l1,   t);
        p.carve = lerpf (s.cv0,  s.cv1,  t);
        p.churn = 0.f; p.grit = p.fan = p.braid = 0.f;
        p.root = p.shine = p.wilt = p.forge = 0.f;
        HarmonicEngine e;
        e.prepare (SR, true); e.setParams (p); e.setPitchRatio (1.0);
        e.setPlayedHz (kAnaHz); e.noteOn (kAnaHz, 0x5EEDu + (std::uint32_t) f);
        e.prepareBank (512);
        int used = 0; float* row = &out[(size_t) f * kHarm];
        for (int j = 0; j < harm::kMaxPartials; ++j)
        {
            const float a = e.debugAmp (j), r = e.debugRatio (j);
            if (a <= 1e-7f || r <= 0.f) continue;
            const int n = (int) std::lround (r);
            if (n < 1 || n > kHarm) continue;
            row[n-1] += a; ++used; pk = std::max (pk, (double) a);
        }
        mx = std::max (mx, used);
    }
    char path[1024]; std::snprintf (path, sizeof path, "%s/H_%s.spec", dir, s.n);
    std::FILE* fp = std::fopen (path, "wb");
    std::fwrite (out.data(), sizeof (float), out.size(), fp); std::fclose (fp);
    std::printf ("  H %-20s fam %d sc %d  partials %4d  %s\n", s.n, s.fam, s.sculpt, mx,
                 (mx > 2 && pk > 0.0) ? "ok" : "*** TOO SPARSE FOR A TABLE ***");
}

static void dumpM (const char* dir, const MS& s)
{
    std::vector<float> out ((size_t) kFrames * kModalLen, 0.f);
    double pk = 0.0; int modes = 0;
    for (int f = 0; f < kFrames; ++f)
    {
        const float t = (float) f / (float) (kFrames - 1);
        ModalParams p;
        p.family = s.fam; p.form = s.form;
        p.material = lerpf (s.m0, s.m1, t);
        p.stretch  = lerpf (s.s0, s.s1, t);
        p.hard     = lerpf (s.h0, s.h1, t);
        p.pos      = lerpf (s.p0, s.p1, t);
        p.decay = s.dec; p.breath = s.breath; p.body = 0.5f;
        p.bloom = p.halo = 0.f;
        ModalEngine e;
        e.prepare (SR, true); e.setParams (p); e.setPitchRatio (1.0);
        e.setPlayedHz (kModalHz); e.noteOn (kModalHz, 0x12345u, 0.95f);
        modes = std::max (modes, e.modeCountForTesting());
        std::vector<float> L (2048, 0.f), R (2048, 0.f);
        for (int b = 0; b < 2; ++b)
        { std::fill (L.begin(), L.end(), 0.f); std::fill (R.begin(), R.end(), 0.f);
          e.renderBlockAdd (L.data(), R.data(), 2048); }
        float* row = &out[(size_t) f * kModalLen]; int w = 0;
        while (w < kModalLen)
        {
            const int n = std::min (2048, kModalLen - w);
            std::fill (L.begin(), L.begin() + n, 0.f); std::fill (R.begin(), R.begin() + n, 0.f);
            e.renderBlockAdd (L.data(), R.data(), n);
            std::copy (L.begin(), L.begin() + n, row + w); w += n;
        }
        for (int i = 0; i < kModalLen; ++i) pk = std::max (pk, (double) std::fabs (row[i]));
    }
    char path[1024]; std::snprintf (path, sizeof path, "%s/M_%s.audio", dir, s.n);
    std::FILE* fp = std::fopen (path, "wb");
    std::fwrite (out.data(), sizeof (float), out.size(), fp); std::fclose (fp);
    const char* core = (s.fam <= modal::PLUCK) ? "waveguide" : (s.fam <= modal::BRASS ? "reedbore " : "modalbank");
    std::printf ("  M %-20s fam %d %s  modes %3d  peak %.3f  %s\n", s.n, s.fam, core, modes, pk,
                 (pk > 1e-4) ? "ok" : "*** NO AUDIO ***");
}

int main (int argc, char** argv)
{
    const char* dir = (argc > 1) ? argv[1] : ".";
    std::printf ("\n══ bank2_probe ══ engine-derived tables · %d frames · analysis %.2f Hz\n\n", kFrames, kAnaHz);

    const HS H[] = {
        {"BLADE_RISE",    0, 0.00f,1.00f, 0.25f,1.00f, 0.15f,0.94f, 0.0f,0.0f, 0},
        {"BLADE_SPLAY",   0, 0.10f,0.90f, 0.80f,1.00f, 0.50f,0.80f, 0.0f,0.9f, 1},
        {"BLADE_CULL",    0, 0.60f,0.05f, 0.90f,0.55f, 0.75f,0.35f, 0.0f,0.8f, 2},
        {"NEON_RISE",     1, 0.00f,1.00f, 0.25f,1.00f, 0.15f,0.94f, 0.0f,0.0f, 0},
        {"NEON_CLANG",    1, 0.20f,0.95f, 0.85f,1.00f, 0.45f,0.85f, 0.0f,0.9f, 5},
        {"CONSOLE_DRIVE", 2, 0.00f,1.00f, 0.95f,1.00f, 0.30f,0.95f, 0.0f,0.6f, 0},
        {"CHANT_RISE",    3, 0.00f,1.00f, 0.25f,1.00f, 0.15f,0.94f, 0.0f,0.0f, 0},
        {"CHANT_TERRACE", 3, 0.15f,0.85f, 0.75f,1.00f, 0.40f,0.70f, 0.0f,0.85f,4},
        {"BRONZE_RISE",   4, 0.00f,1.00f, 0.30f,1.00f, 0.20f,0.92f, 0.0f,0.0f, 0},
        {"BRONZE_TIDE",   4, 0.10f,0.90f, 0.80f,1.00f, 0.40f,0.80f, 0.0f,0.85f,3},
        {"HORNET_RISE",   5, 0.00f,1.00f, 0.25f,1.00f, 0.15f,0.96f, 0.0f,0.0f, 0},
        {"HORNET_CULL",   5, 0.55f,0.10f, 0.95f,0.60f, 0.85f,0.40f, 0.0f,0.9f, 2},
    };
    for (const auto& s : H) dumpH (dir, s);
    std::printf ("\n");

    // All nine families across all three cores. pos (excitation point) is swept on the waveguide
    // and reedbore families because that is THEIR strongest timbral axis — material/stretch only
    // mean something to the modal-bank three.
    const MS M[] = {
        {"GRAND_STRIKE",  modal::GRAND, 0, 0.10f,0.90f, 0.40f,0.60f, 0.15f,0.95f, 0.05f,0.45f, 0.80f, 0.0f},
        {"PLUCK_NAIL",    modal::PLUCK, 0, 0.15f,0.85f, 0.40f,0.65f, 0.10f,0.95f, 0.02f,0.48f, 0.75f, 0.0f},
        {"BOW_PRESSURE",  modal::BOW,   0, 0.10f,0.90f, 0.40f,0.60f, 0.10f,0.90f, 0.08f,0.42f, 0.85f, 0.35f},
        {"FLUTE_BREATH",  modal::FLUTE, 0, 0.15f,0.85f, 0.40f,0.60f, 0.15f,0.90f, 0.10f,0.40f, 0.80f, 0.55f},
        {"REED_BITE",     modal::REED,  0, 0.10f,0.90f, 0.35f,0.70f, 0.20f,0.95f, 0.06f,0.44f, 0.80f, 0.30f},
        {"BRASS_BLARE",   modal::BRASS, 0, 0.10f,0.95f, 0.35f,0.70f, 0.20f,0.98f, 0.05f,0.45f, 0.82f, 0.25f},
        {"BARS_WOOD",     modal::BARS,  0, 0.02f,0.70f, 0.30f,0.75f, 0.25f,0.85f, 0.10f,0.40f, 0.82f, 0.0f},
        {"BARS_GLASS",    modal::BARS,  2, 0.50f,0.99f, 0.60f,0.30f, 0.40f,0.95f, 0.30f,0.10f, 0.88f, 0.0f},
        {"BELLS_METAL",   modal::BELLS, 0, 0.02f,0.98f, 0.25f,0.80f, 0.30f,0.88f, 0.10f,0.45f, 0.86f, 0.0f},
        {"BELLS_GONG",    modal::BELLS, 3, 0.30f,0.95f, 0.75f,0.25f, 0.20f,0.70f, 0.40f,0.08f, 0.90f, 0.0f},
        {"SKIN_DRUM",     modal::SKIN,  0, 0.05f,0.80f, 0.30f,0.75f, 0.20f,0.90f, 0.06f,0.46f, 0.72f, 0.0f},
        {"SKIN_TENSION",  modal::SKIN,  2, 0.40f,0.95f, 0.70f,0.20f, 0.35f,0.95f, 0.35f,0.05f, 0.78f, 0.20f},
    };
    for (const auto& s : M) dumpM (dir, s);

    std::printf ("\n   %d harmonic + %d modal dumps -> %s\n\n",
                 (int)(sizeof H/sizeof H[0]), (int)(sizeof M/sizeof M[0]), dir);
    return 0;
}
