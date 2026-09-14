// ══════════════════════════════════════════════════════════════════════════════════════════════
//  bank3h_probe.cpp — the HARMONIC half of the 500: Terrain's OWN HarmonicEngine, pushed.
//
//  bank2_probe read the 512-partial bank ONCE per frame, at tB = 0, with one family and one sculpt
//  riding a hue/count/lean ramp. That left most of the engine on the table: its TIME laws (Neon's
//  self-sweeping stab, Tide's travelling ripple, Terrace's re-dither, Grit's OU drift, Fan's golden
//  orbit), its ghosts (Root's sub, Shine's detuned octave), Braid's twins, FORGE (the post-render
//  saturator, which only exists on AUDIO), and the fact that the bank can be read at any
//  "virtual pitch" (Chant's formants are Hz-anchored, so the pitch you analyse at decides where the
//  vowels land in the table).
//
//  So each job here is a per-frame PROGRAM over the engine, not a two-point ramp:
//     prog(u, frame)   u = 0..1 along the table; sets HarmParams AND engine time / orbit / substeps
//  and one engine instance runs through all 128 frames (the OU drift and Fan orbit integrate).
//
//  GEOMETRY. A job has an integer MULT m: engine ratio r lands on table harmonic m*r. The engine is
//  prepared at SR' = noteHz * 2048 / m, so one 2048-sample frame is exactly m engine periods and
//  the engine's own Nyquist law (nEff = 0.48 SR'/f0) gives 512 partials at m = 1, ~495 at m = 2.
//     m = 1   the classic read: every engine partial on its own harmonic
//     m = 2   half-harmonic resolution: Root's sub ghost (ratio 0.5) and Console's 16' / 5 1/3'
//             bars (0.5, 1.5) land on real bins instead of being rounded away
//     m > 2   the engine as ONE VOICE of a chord / organ rank stacked in gen2_harmonic.py
//  noteHz only matters to what is Hz-anchored (Chant's formants, time constants).
//
//  OUTPUT (under argv[1], default <wt500>/engine3h):
//     S_<NAME>.spec   128 x 512 x {ratio*m, ampL, ampR} float32 — the raw post-sculpt bank, panned.
//                     gen2_harmonic.py projects it onto the 1024-bin grid (energy split between the
//                     two nearest bins, so a stretched partial GLIDES instead of snapping).
//     A_<NAME>.audio  128 x {2048 L, 2048 R} float32 — one steady-state period of the RENDERED
//                     engine incl. FORGE (fresh note per frame, 6 blocks, last one kept).
//
//  The private members are opened with #define private public — a probe-only trick (no Source
//  file is touched) that lets a frame set the engine clock tB_ and Fan's orbit_ directly and read
//  the per-partial pans, which the public debug API does not expose.
//
//  Build: c++ -std=c++17 -O2 -I Source Design/wavetables/bank3h_probe.cpp -o /tmp/bank3h
//  Run:   /tmp/bank3h <dump dir>          (about 2 s for all jobs)
// ══════════════════════════════════════════════════════════════════════════════════════════════
#include <cstdio>
#include <cstdint>
#include <cmath>
#include <vector>
#include <string>
#include <functional>
#include <algorithm>
#define private public
#include "HarmonicEngine.h"
#undef private

using namespace tw;

static constexpr int kFrames = 128, kSlots = 512, kN = 2048;
// 21.533 Hz (44100/2048) is the pitch whose period IS one frame: the m = 1 reference.

struct Frame
{
    HarmParams p;
    double T = -1.0;         // engine clock override (seconds); < 0 = let it run
    double orbit = -1.0;     // Fan orbit override (radians); < 0 = let it run
    int    substeps = 1;     // prepareBank calls this frame (OU drift integrates per call)
    double stepSec = 0.004;  // engine seconds per call
};
using Prog = std::function<void (float, Frame&)>;
struct Job
{
    std::string name;
    int mult; double noteHz; int audio; int phaseLaw; std::uint32_t seed;
    Prog prog;
};

static float cl01 (float x) { return x < 0.f ? 0.f : (x > 1.f ? 1.f : x); }
static float lerp (float a, float b, float t) { return a + (b - a) * t; }
static float seg (float u, float a, float b) { return cl01 ((u - a) / (b - a)); }        // 0..1 over [a,b]
static float ss (float u, float a, float b) { const float t = seg (u, a, b); return t * t * (3.f - 2.f * t); }

static HarmParams base (int fam, int sculpt)
{
    HarmParams p;
    p.mainMode = fam; p.sculptMode = sculpt;
    p.hue = 0.f; p.count = 1.f; p.lean = 0.5f; p.fan = 0.f; p.grit = 0.f; p.braid = 0.f;
    p.carve = 0.f; p.churn = 0.5f; p.root = 0.f; p.shine = 0.f; p.wilt = 0.5f; p.forge = 0.f;
    return p;
}

static void runSpec (const char* dir, const Job& J)
{
    const double sr = J.noteHz * kN / J.mult;
    std::vector<float> out ((size_t) kFrames * kSlots * 3, 0.f);
    HarmonicEngine e;
    e.prepare (sr, true);
    int mxAct = 0, mnAct = 1 << 30;
    for (int f = 0; f < kFrames; ++f)
    {
        const float u = (float) f / (float) (kFrames - 1);
        Frame fr; fr.p = base (0, 0);
        J.prog (u, fr);
        e.setParams (fr.p); e.setPitchRatio (1.0); e.setPlayedHz (J.noteHz);
        if (f == 0) e.noteOn (J.noteHz, J.seed);
        for (int s = 0; s < std::max (1, fr.substeps); ++s)
        {
            if (fr.T >= 0.0)     e.tB_ = (float) fr.T;
            if (fr.orbit >= 0.0) e.orbit_ = (float) fr.orbit;
            const int n = std::max (128, (int) std::lround (fr.stepSec * sr));
            e.prepareBank (n);
        }
        float* row = &out[(size_t) f * kSlots * 3];
        int act = 0;
        for (int j = 0; j < std::min (e.nP_, kSlots); ++j)
        {
            const float a = e.amp_[(size_t) j];
            if (! (a > 1e-9f)) continue;
            row[j * 3 + 0] = e.ratio_[(size_t) j] * (float) J.mult;
            row[j * 3 + 1] = a * e.panL_[(size_t) j];
            row[j * 3 + 2] = a * e.panR_[(size_t) j];
            ++act;
        }
        mxAct = std::max (mxAct, act); mnAct = std::min (mnAct, act);
    }
    char path[1024]; std::snprintf (path, sizeof path, "%s/S_%s.spec", dir, J.name.c_str());
    std::FILE* fp = std::fopen (path, "wb");
    if (! fp) { std::printf ("  !! cannot write %s\n", path); return; }
    std::fwrite (out.data(), sizeof (float), out.size(), fp); std::fclose (fp);
    std::printf ("  S %-22s m %2d  note %7.2f Hz  partials %3d..%3d  %s\n", J.name.c_str(), J.mult, J.noteHz,
                 mnAct, mxAct, mnAct > 0 ? "ok" : "*** EMPTY FRAME ***");
}

static void runAudio (const char* dir, const Job& J)
{
    const double sr = J.noteHz * kN / J.mult;
    std::vector<float> out ((size_t) kFrames * kN * 2, 0.f);
    std::vector<float> L (kN), R (kN);
    double pkAll = 0.0;
    for (int f = 0; f < kFrames; ++f)
    {
        const float u = (float) f / (float) (kFrames - 1);
        Frame fr; fr.p = base (0, 0);
        J.prog (u, fr);
        HarmonicEngine e;                           // fresh note per frame: FORGE snaps to its knob
        e.prepare (sr, true);
        e.setParams (fr.p); e.setPitchRatio (1.0); e.setPlayedHz (J.noteHz);
        e.noteOn (J.noteHz, J.seed);
        if (J.phaseLaw == 1) for (auto& ph : e.phase_) ph = 0.f;            // sine phase: coherent ramps
        if (J.phaseLaw == 2) for (auto& ph : e.phase_) ph = 0.25f;          // cosine phase: impulse-like
        for (int b = 0; b < 6; ++b)
        {
            std::fill (L.begin(), L.end(), 0.f); std::fill (R.begin(), R.end(), 0.f);
            if (fr.T >= 0.0) e.tB_ = (float) fr.T;
            e.prepareBank (kN);
            e.renderBankAdd (L.data(), R.data(), kN);
            e.postProcess (L.data(), R.data(), kN);
        }
        float* row = &out[(size_t) f * kN * 2];
        std::copy (L.begin(), L.end(), row);
        std::copy (R.begin(), R.end(), row + kN);
        for (int i = 0; i < kN; ++i) pkAll = std::max (pkAll, (double) std::fabs (L[(size_t) i]));
    }
    char path[1024]; std::snprintf (path, sizeof path, "%s/A_%s.audio", dir, J.name.c_str());
    std::FILE* fp = std::fopen (path, "wb");
    if (! fp) { std::printf ("  !! cannot write %s\n", path); return; }
    std::fwrite (out.data(), sizeof (float), out.size(), fp); std::fclose (fp);
    std::printf ("  A %-22s m %2d  note %7.2f Hz  peak %.3f  %s\n", J.name.c_str(), J.mult, J.noteHz, pkAll,
                 pkAll > 1e-4 ? "ok" : "*** NO AUDIO ***");
}

// ══ THE PROGRAMS ═══════════════════════════════════════════════════════════════════════════════
//  families: 0 BLADE 1 NEON 2 CONSOLE 3 CHANT 4 BRONZE 5 HORNET · sculpts: 0 KEEL 1 SPLAY 2 CULL
//  3 TIDE 4 TERRACE 5 CLANG. Every job names what its frame axis does to the ENGINE.
enum { BLADE = 0, NEON, CONSOLE, CHANT, BRONZE, HORNET, TABLE };
enum { KEEL = 0, SPLAY, CULL, TIDE, TERRACE, CLANG };

// Neon's self-sweeping stab: resonance centre c = 2.5 + 9.5 e^(-3.2 T). Invert to place c exactly.
static double neonT (float c) { c = std::max (2.55f, std::min (12.f, c)); return -std::log ((c - 2.5) / 9.5) / 3.2; }

// ── STACKS: several engine voices on harmonics m1, m2 ... of one table, summed as AUDIO, then ONE
//    Forge (the engine's own postProcess) on the sum — so the drive intermodulates the chord.
struct StackJob
{
    std::string name; double f0; std::vector<int> mults; std::uint32_t seed;
    std::function<void (float, int, Frame&, float&)> voice;   // (u, voice index, params, gain)
    std::function<void (float, Frame&)> drive;                 // the Forge stage's params
};
static void runStack (const char* dir, const StackJob& S)
{
    const double sr = S.f0 * kN;
    const int blocks = 6;
    std::vector<float> out ((size_t) kFrames * kN * 2, 0.f);
    std::vector<float> L ((size_t) kN * blocks), R ((size_t) kN * blocks), vl (kN), vr (kN);
    double pkAll = 0.0;
    for (int f = 0; f < kFrames; ++f)
    {
        const float u = (float) f / (float) (kFrames - 1);
        std::fill (L.begin(), L.end(), 0.f); std::fill (R.begin(), R.end(), 0.f);
        for (int vi = 0; vi < (int) S.mults.size(); ++vi)
        {
            Frame fr; fr.p = base (0, 0); float g = 0.f;
            S.voice (u, vi, fr, g);
            if (g <= 0.f) continue;
            const double hz = S.f0 * S.mults[(size_t) vi];
            HarmonicEngine e; e.prepare (sr, true);
            e.setParams (fr.p); e.setPitchRatio (1.0); e.setPlayedHz (hz);
            e.noteOn (hz, S.seed + 977u * (std::uint32_t) vi);
            for (int b = 0; b < blocks; ++b)
            {
                std::fill (vl.begin(), vl.end(), 0.f); std::fill (vr.begin(), vr.end(), 0.f);
                if (fr.T >= 0.0) e.tB_ = (float) fr.T;
                e.prepareBank (kN);
                e.renderBankAdd (vl.data(), vr.data(), kN);
                for (int i = 0; i < kN; ++i) { L[(size_t) (b * kN + i)] += g * vl[(size_t) i]; R[(size_t) (b * kN + i)] += g * vr[(size_t) i]; }
            }
        }
        Frame df; df.p = base (0, 0); S.drive (u, df);
        HarmonicEngine fx; fx.prepare (sr, true); fx.setParams (df.p); fx.setPlayedHz (S.f0); fx.noteOn (S.f0, S.seed);
        for (int b = 0; b < blocks; ++b) fx.postProcess (&L[(size_t) b * kN], &R[(size_t) b * kN], kN);
        float* row = &out[(size_t) f * kN * 2];
        std::copy (L.end() - kN, L.end(), row);
        std::copy (R.end() - kN, R.end(), row + kN);
        for (int i = 0; i < kN; ++i) pkAll = std::max (pkAll, (double) std::fabs (row[i]));
    }
    char path[1024]; std::snprintf (path, sizeof path, "%s/A_%s.audio", dir, S.name.c_str());
    std::FILE* fp = std::fopen (path, "wb");
    if (! fp) { std::printf ("  !! cannot write %s\n", path); return; }
    std::fwrite (out.data(), sizeof (float), out.size(), fp); std::fclose (fp);
    std::printf ("  A %-22s stack of %zu  f0 %6.2f Hz  peak %.3f  %s\n", S.name.c_str(), S.mults.size(), S.f0, pkAll,
                 pkAll > 1e-4 ? "ok" : "*** NO AUDIO ***");
}

// ── CHAINS: the engine run twice. Pass 1 (any family + sculpt) is resolved onto the harmonic grid
//    and handed to pass 2 as its TABLE source (mainMode 6 — the same path a factory wavetable takes
//    into the Harmonic oscillator), where a SECOND sculpt acts on it. Two sculpts in series.
struct ChainJob
{
    std::string name; int mult; double noteHz; std::uint32_t seed;
    std::function<void (float, Frame&)> pass1, pass2;
};
static void runChain (const char* dir, const ChainJob& C)
{
    const double sr = C.noteHz * kN / C.mult;
    std::vector<float> out ((size_t) kFrames * kSlots * 3, 0.f), tab (kSlots), ph0 (kSlots, 0.f);
    HarmonicEngine e1, e2; e1.prepare (sr, true); e2.prepare (sr, true);
    int mnAct = 1 << 30, mxAct = 0;
    for (int f = 0; f < kFrames; ++f)
    {
        const float u = (float) f / (float) (kFrames - 1);
        Frame f1; f1.p = base (0, 0); C.pass1 (u, f1);
        e1.setParams (f1.p); e1.setPitchRatio (1.0); e1.setPlayedHz (C.noteHz);
        if (f == 0) e1.noteOn (C.noteHz, C.seed);
        for (int s = 0; s < std::max (1, f1.substeps); ++s)
        { if (f1.T >= 0.0) e1.tB_ = (float) f1.T; e1.prepareBank (std::max (128, (int) std::lround (f1.stepSec * sr))); }
        std::fill (tab.begin(), tab.end(), 0.f);
        for (int j = 0; j < e1.nP_; ++j)
        {
            const int k = (int) std::lround (e1.ratio_[(size_t) j]) - 1;
            if (k >= 0 && k < kSlots) tab[(size_t) k] = std::sqrt (tab[(size_t) k] * tab[(size_t) k] + e1.amp_[(size_t) j] * e1.amp_[(size_t) j]);
        }
        const float pk = *std::max_element (tab.begin(), tab.end());
        if (pk > 0.f) for (auto& v : tab) v /= pk;
        Frame f2; f2.p = base (TABLE, 0); C.pass2 (u, f2);
        f2.p.mainMode = TABLE;
        f2.p.tableAmp = tab.data(); f2.p.tablePhase = ph0.data(); f2.p.tableN = kSlots; f2.p.tableSig = (float) (f + 1);
        e2.setParams (f2.p); e2.setPitchRatio (1.0); e2.setPlayedHz (C.noteHz);
        if (f == 0) e2.noteOn (C.noteHz, C.seed ^ 0x5A5Au);
        if (f2.T >= 0.0) e2.tB_ = (float) f2.T;
        e2.prepareBank (512);
        float* row = &out[(size_t) f * kSlots * 3];
        int act = 0;
        for (int j = 0; j < std::min (e2.nP_, kSlots); ++j)
        {
            const float a = e2.amp_[(size_t) j];
            if (! (a > 1e-9f)) continue;
            row[j * 3 + 0] = e2.ratio_[(size_t) j] * (float) C.mult;
            row[j * 3 + 1] = a * e2.panL_[(size_t) j];
            row[j * 3 + 2] = a * e2.panR_[(size_t) j];
            ++act;
        }
        mnAct = std::min (mnAct, act); mxAct = std::max (mxAct, act);
    }
    char path[1024]; std::snprintf (path, sizeof path, "%s/S_%s.spec", dir, C.name.c_str());
    std::FILE* fp = std::fopen (path, "wb");
    if (! fp) { std::printf ("  !! cannot write %s\n", path); return; }
    std::fwrite (out.data(), sizeof (float), out.size(), fp); std::fclose (fp);
    std::printf ("  C %-22s m %2d  note %7.2f Hz  partials %3d..%3d  %s\n", C.name.c_str(), C.mult, C.noteHz,
                 mnAct, mxAct, mnAct > 0 ? "ok" : "*** EMPTY FRAME ***");
}

static std::vector<Job> makeJobs()
{
    std::vector<Job> J;

    // Console drawbars (m = 2 so the 16' and 5 1/3' bars are real bins), rendered as AUDIO through
    // FORGE: full registration, the drive climbing into its feedback snarl; late on the bars detune
    // into the carillon and Clang's intermod lattice joins, all under the drive (the rendered period
    // stops closing: a storm).
    J.push_back ({ "OVERDRIVE_ORGAN", 2, 110.0, 1, 0, 0x0E6A1u, [] (float u, Frame& f) {
        f.p = base (CONSOLE, CLANG);
        f.p.hue = lerp (0.5f, 1.f, ss (u, 0.35f, 1.f));
        f.p.carve = ss (u, 0.5f, 1.f);
        f.p.root = 1.f;
        f.p.forge = 0.55f * std::pow (seg (u, 0.f, 0.7f), 1.2f);
    }});

    // Console full registration -> detuned carillon, the bells' intermod lattice (Clang) and the
    // +1 octave ghost (Shine) growing on top, Braid twins at the end.
    J.push_back ({ "CARILLON", 2, 110.0, 0, 0, 0xCA11u, [] (float u, Frame& f) {
        f.p = base (CONSOLE, CLANG);
        f.p.hue = lerp (0.5f, 1.f, u);
        f.p.root = 1.f;
        f.p.lean = lerp (0.7f, 0.9f, u);
        f.p.shine = 0.9f * ss (u, 0.2f, 0.7f);
        f.p.carve = ss (u, 0.3f, 1.f);
        f.p.braid = ss (u, 0.6f, 1.f);
        f.p.churn = 0.8f; f.T = 3.0 * u;
    }});

    // Organ RANKS for gen2_harmonic's plenum: one Blade voice per footage, stacked in Python.
    // Every rank runs the same late program (a stretch and a haunt) so the whole organ breaks at once.
    for (int m : { 1, 2, 3, 4, 5, 6, 8, 10, 12, 16, 24, 32 })
    {
        char nm[64]; std::snprintf (nm, sizeof nm, "RANK_%02d", m);
        J.push_back ({ nm, m, 65.41 * m, 0, 0, 0x0A00u + (std::uint32_t) m, [m] (float u, Frame& f) {
            f.p = base (BLADE, SPLAY);
            f.p.hue = (m <= 2) ? 0.9f : 0.6f;
            f.p.lean = (m <= 2) ? 0.5f : 0.58f;
            f.p.carve = 0.8f * ss (u, 0.72f, 1.f);
            f.p.grit = ss (u, 0.8f, 1.f);
            f.substeps = 2; f.stepSec = 0.004;
        }});
    }
    J.push_back ({ "RANK_REED", 1, 65.41, 0, 0, 0x0AEEu, [] (float u, Frame& f) {
        f.p = base (BLADE, SPLAY);
        f.p.hue = 0.0f; f.p.lean = 0.56f;
        f.p.carve = 0.8f * ss (u, 0.72f, 1.f);
        f.p.grit = ss (u, 0.8f, 1.f);
        f.substeps = 2; f.stepSec = 0.004;
    }});

    // Neon at full resonance (Q 12): the self-sweep clock is parked so the peak WALKS UP the
    // harmonic series 2.6 -> 12 (an overtone melody); then Tide cuts its comb through the whistle,
    // Shine's octave ghost and Grit's drift break it up.
    J.push_back ({ "OVERTONE_WHISTLE", 1, 65.41, 0, 0, 0x0B0Eu, [] (float u, Frame& f) {
        f.p = base (NEON, TIDE);
        f.p.hue = 1.f;
        const float c = 2.6f * std::pow (12.f / 2.6f, seg (u, 0.f, 0.62f));
        f.T = neonT (c);
        f.p.lean = 0.76f;
        f.p.carve = ss (u, 0.2f, 1.f);
        f.p.shine = 0.8f * ss (u, 0.7f, 1.f);
        f.p.grit = ss (u, 0.8f, 1.f);
        f.substeps = 2;
    }});

    // Chant read at C2 (65 Hz) so the formants sit on harmonics 5..46: the vowel walk oo -> ah -> ee
    // is an overtone singer's melody; the choir scatter, Clang lattice and Braid twins take over.
    J.push_back ({ "OVERTONE_CHANT", 1, 65.41, 0, 0, 0xC4A7u, [] (float u, Frame& f) {
        f.p = base (CHANT, CLANG);
        f.p.hue = u;
        f.p.lean = lerp (0.55f, 0.8f, u);
        f.p.carve = ss (u, 0.55f, 1.f);
        f.p.braid = 0.7f * ss (u, 0.7f, 1.f);
        f.T = 6.0 * u;
    }});

    // Chant through Cull: the choir's formants on a sieve — full -> odd -> primes -> Fibonacci.
    J.push_back ({ "PRIME_CHOIR", 1, 98.0, 0, 0, 0x9C40u, [] (float u, Frame& f) {
        f.p = base (CHANT, CULL);
        f.p.hue = lerp (0.25f, 0.9f, u);
        f.p.lean = lerp (0.6f, 0.7f, u);
        f.p.carve = u;
        f.T = 5.0 * u;
    }});

    // Hornet's full swarm through the whole Cull chain: full -> odd -> primes -> Fibonacci survivors.
    J.push_back ({ "FIB_SIEVE", 1, 21.533, 0, 0, 0xF1Bu, [] (float u, Frame& f) {
        f.p = base (HORNET, CULL);
        f.p.hue = 0.9f; f.p.lean = lerp (0.45f, 0.72f, u);
        f.p.carve = u;
    }});

    // Tide: the travelling amplitude ripple, its clock running with the frames — a saw -> swell ->
    // comb -> spectral Leslie flutter, the tilt opening as it goes.
    J.push_back ({ "TIDE_STORM", 1, 21.533, 0, 0, 0x71DEu, [] (float u, Frame& f) {
        f.p = base (BLADE, TIDE);
        f.p.hue = 0.13f; f.p.lean = lerp (0.5f, 0.72f, u);
        f.p.carve = u; f.p.churn = 0.7f;
        f.T = 1.2 * u;
    }});

    // Fan read from the LEFT channel only while the tilt opens: dark saw -> odd/even split (the left
    // ear hears a square) -> golden-angle scatter, the orbit turning three times across the table.
    J.push_back ({ "GOLDEN_ORBIT", 1, 21.533, 0, 0, 0x60D0u, [] (float u, Frame& f) {
        f.p = base (BLADE, KEEL);
        f.p.hue = 0.3f; f.p.lean = lerp (0.45f, 0.68f, u);
        f.p.fan = u;
        f.orbit = 6.0 * 3.14159265 * u;
    }});

    // Hornet buzz (near-aligned phases) as AUDIO through FORGE: 12-partial buzz -> swarm -> fuzz brick.
    J.push_back ({ "FORGED_SWARM", 1, 110.0, 1, 0, 0xF0E6u, [] (float u, Frame& f) {
        f.p = base (HORNET, KEEL);
        f.p.hue = ss (u, 0.f, 0.6f);
        f.p.forge = std::pow (seg (u, 0.2f, 1.f), 1.5f);
    }});

    // Keel: saw -> the felt-blanket tilt -> the pivot slides up to 18 and the lows duck (megaphone),
    // Shine's octave ghost on top.
    J.push_back ({ "MEGAPHONE", 1, 21.533, 0, 0, 0x3E6Au, [] (float u, Frame& f) {
        f.p = base (BLADE, KEEL);
        f.p.hue = 0.13f;
        f.p.carve = u;
        f.p.lean = lerp (0.5f, 0.68f, u);
        f.p.shine = 0.6f * ss (u, 0.6f, 1.f);
    }});

    // Bronze (stiff string -> gong) at m = 2, Root's sub ghost on bin 1, Clang lattice rising.
    J.push_back ({ "SUB_BELL", 2, 55.0, 0, 0, 0x5BE1u, [] (float u, Frame& f) {
        f.p = base (BRONZE, CLANG);
        f.p.hue = lerp (0.15f, 0.8f, u);
        f.p.lean = lerp (0.62f, 0.8f, u);
        f.p.root = 0.8f;
        f.p.carve = u;
    }});

    // Terrace over Hornet's full swarm: fine dB shelves -> coarse steps -> dithered sputter,
    // re-dithered as the clock runs.
    J.push_back ({ "TERRACE_SPUTTER", 1, 21.533, 0, 0, 0x7E77u, [] (float u, Frame& f) {
        f.p = base (HORNET, TERRACE);
        f.p.hue = 1.f; f.p.lean = lerp (0.5f, 0.75f, u);
        f.p.carve = u; f.p.churn = 0.9f;
        f.T = 2.0 * u;
    }});

    // Chord VOICES for gen2_harmonic's in-cycle chords (voice m sits on harmonics m, 2m, 3m ...).
    for (int m : { 1, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 16 })
    {
        char nm[64]; std::snprintf (nm, sizeof nm, "VOICE_%02d", m);
        J.push_back ({ nm, m, 32.7 * m, 0, 0, 0x0C00u + (std::uint32_t) m, [m] (float u, Frame& f) {
            f.p = base (BLADE, SPLAY);
            f.p.hue = (m == 1) ? 0.9f : 0.3f;
            f.p.lean = (m == 1) ? 0.55f : lerp (0.5f, 0.68f, u);
            f.p.carve = 0.85f * ss (u, 0.7f, 1.f);
            f.p.braid = ss (u, 0.75f, 1.f);
        }});
    }

    // Hornet swarm at m = 2 with the Root sub ghost locked on bin 1: Splay is already pulling it to
    // glass at frame 0 (wild from the start, the sub keeps it playable), then gong, partials flying
    // off the top of the grid.
    J.push_back ({ "SUB_SHATTER", 2, 55.0, 0, 0, 0x5A77u, [] (float u, Frame& f) {
        f.p = base (HORNET, SPLAY);
        f.p.hue = 0.8f; f.p.lean = lerp (0.45f, 0.72f, u);
        f.p.root = 1.f;
        f.p.carve = lerp (0.25f, 1.f, u);
    }});

    // Hollow reed (evens faded) + Shine's detuned octave ghost filling the even slots, beating as the
    // clock runs (churn 1 = the fastest ghost detune) while the tilt opens; Braid, then Splay
    // stretches the filled saw into glass.
    J.push_back ({ "OCTAVE_GHOST", 1, 21.533, 0, 0, 0x6405u, [] (float u, Frame& f) {
        f.p = base (BLADE, SPLAY);
        f.p.hue = 0.95f; f.p.lean = lerp (0.6f, 0.8f, u);
        f.p.carve = 0.8f * ss (u, 0.35f, 1.f);
        f.p.shine = u; f.p.churn = 1.f;
        f.p.braid = ss (u, 0.45f, 1.f);
        f.T = 6.0 * u;
    }});

    // Chant as AUDIO through FORGE: the vowel walk, then the choir distorts into a fuzz scream.
    J.push_back ({ "FUZZ_CHANT", 1, 110.0, 1, 0, 0xF7C4u, [] (float u, Frame& f) {
        f.p = base (CHANT, KEEL);
        f.p.hue = u; f.p.lean = 0.6f;
        f.p.forge = std::pow (seg (u, 0.3f, 1.f), 1.3f);
        f.T = 4.0 * u;
    }});

    // Chant (ah, choir) through Splay: the formants stay where the voice put them while every partial
    // under them is stretched off the harmonic series — a choir turning to glass.
    J.push_back ({ "GLASS_CHOIR", 1, 82.41, 0, 0, 0x6C40u, [] (float u, Frame& f) {
        f.p = base (CHANT, SPLAY);
        f.p.hue = lerp (0.45f, 0.75f, u);
        f.p.lean = lerp (0.6f, 0.72f, u);
        f.p.carve = u;
        f.T = 5.0 * u;
    }});

    // A true saw (sine-phase Blade) through the Cull chain as AUDIO into FORGE: as the sieve empties the
    // spectrum the drive rises, and the survivors' intermodulation refills every hole.
    J.push_back ({ "SIEVE_FUZZ", 1, 110.0, 1, 1, 0x51F2u, [] (float u, Frame& f) {
        f.p = base (BLADE, CULL);
        f.p.hue = 0.13f; f.p.lean = lerp (0.5f, 0.8f, u);
        f.p.carve = ss (u, 0.f, 0.7f);
        f.p.forge = std::pow (seg (u, 0.35f, 1.f), 1.2f);
    }});

    // Chant at m = 2 over Root's sub ghost (bin 1, an octave under the voice): the vowel walk over a
    // sub, then Tide's ripple turns the choir into a spinning Leslie.
    J.push_back ({ "SUB_CHOIR", 2, 110.0, 0, 0, 0x5C40u, [] (float u, Frame& f) {
        f.p = base (CHANT, TIDE);
        f.p.hue = u;
        f.p.root = 1.f;
        f.p.lean = lerp (0.55f, 0.7f, u);
        f.p.carve = ss (u, 0.45f, 1.f); f.p.churn = 0.8f;
        f.T = 3.0 * u;
    }});

    return J;
}

static std::vector<StackJob> makeStacks()
{
    std::vector<StackJob> S;
    // Power chord of Neon voices (root, fifth, octave on harmonics 2 3 4, each with its own parked
    // resonance) summed, then ONE Forge on the sum: clean -> driven -> the major third (5) and harmonic
    // seventh (7) walk into the drive and intermodulate. (Measured: Blade saws here sat 6.0 dB from a
    // shipped saw pad, Hornet buzz voices 5.7 dB from a Metallic candidate — driven chords converge on
    // generic dense spectra; Neon's per-voice resonances are what keep this one its own.)
    S.push_back ({ "POWER_CHORD", 55.0, { 2, 3, 4, 5, 7 }, 0x9C0Du,
        [] (float u, int vi, Frame& f, float& g) {
            f.p = base (NEON, KEEL);
            f.p.hue = 0.4f;
            static const float g0[5] = { 1.f, 0.85f, 0.7f, 0.f, 0.f };
            g = g0[vi];
            if (vi == 3) g = 0.75f * ss (u, 0.5f, 0.66f);
            if (vi == 4) g = 0.65f * ss (u, 0.72f, 0.88f);
        },
        [] (float u, Frame& f) { f.p = base (BLADE, KEEL); f.p.forge = 0.7f * std::pow (seg (u, 0.f, 0.8f), 1.2f); } });
    // A harmonic-series TONE CLUSTER: eight Blade voices on harmonics 8 .. 15 join one by one (frame 0
    // is the lone voice on 8), then ONE Forge on the sum grinds the cluster into a roar.
    S.push_back ({ "HARMONIC_CLUSTER", 27.5, { 8, 9, 10, 11, 12, 13, 14, 15 }, 0xC105u,
        [] (float u, int vi, Frame& f, float& g) {
            f.p = base (BLADE, KEEL);
            f.p.hue = 0.85f; f.p.lean = 0.5f;
            g = (vi == 0) ? 1.f : 0.9f * ss (u, 0.05f * (float) vi, 0.05f * (float) vi + 0.06f);
        },
        [] (float u, Frame& f) { f.p = base (BLADE, KEEL); f.p.forge = std::pow (seg (u, 0.42f, 1.f), 1.2f); } });
    return S;
}

static std::vector<ChainJob> makeChains()
{
    std::vector<ChainJob> C;
    // Hornet swarm sieved to its PRIMES (Cull), then that prime spectrum stretched (Splay) —
    // the primes fan out into glass and fly apart.
    C.push_back ({ "PRIME_GLASS", 1, 21.533, 0x9A55u,
        [] (float u, Frame& f) { f.p = base (BLADE, CULL); f.p.hue = 0.13f; f.p.carve = 0.75f * ss (u, 0.f, 0.35f); },
        [] (float u, Frame& f) { f.p = base (TABLE, SPLAY); f.p.carve = ss (u, 0.3f, 1.f); f.p.lean = lerp (0.5f, 0.72f, u); } });
    return C;
}

int main (int argc, char** argv)
{
    const char* dir = (argc > 1) ? argv[1] : ".";
    const char* only = (argc > 2) ? argv[2] : nullptr;
    std::printf ("\n══ bank3h_probe ══ HarmonicEngine programs · %d frames\n\n", kFrames);
    int n = 0;
    auto want = [&] (const std::string& s) { return ! only || s.find (only) != std::string::npos; };
    for (const auto& J : makeJobs())   if (want (J.name)) { if (J.audio) runAudio (dir, J); else runSpec (dir, J); ++n; }
    for (const auto& S : makeStacks()) if (want (S.name)) { runStack (dir, S); ++n; }
    for (const auto& C : makeChains()) if (want (C.name)) { runChain (dir, C); ++n; }
    std::printf ("\n   %d jobs -> %s\n\n", n, dir);
    return 0;
}
