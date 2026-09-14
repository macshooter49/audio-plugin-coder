// ══════════════════════════════════════════════════════════════════════════════════════════════
//  bank3_probe.cpp — Terrain's OWN ModalEngine cores pushed past their knobs, for the "Physical"
//  half of the 500-table factory library (gen2_physical.py reads what this writes).
//
//  bank2_probe rendered the nine families INSIDE their musical ranges and projected every render
//  onto a 55 Hz harmonic grid. That made Physical the bank's weakest folder (21 st of travel, three
//  tables within 6 dB of one another). This probe does two things differently:
//
//  1. IT RENDERS AT THE WAVETABLE'S OWN PERIOD. The engine is prepared at rate R and played at
//     f0 = R/2048 (R = 44.1k·k), so one loop period of the waveguide / reed-bore IS one 2048-sample
//     frame and every harmonic up to 1023 is reachable — the cycle is read back in the TIME domain
//     (gen2_physical periodises it), so each frame keeps the model's real waveform and phase, not a
//     magnitude projection onto a fixed phase set. Rate multiples keep the model's Hz-based filters
//     and ms-based bursts in a musical register (k=4 -> the note is F2 = 86 Hz).
//
//  2. IT REACHES PAST THE KNOBS. `#define private public` exposes the per-note coefficients that
//     buildVoice() derives, and a job may override them after noteOn(): breath drive past the
//     Schelleng clamp, reed / lip slopes, bow friction, flute jet length, loop gain, loop cutoff,
//     dispersion strength past the 0.85 cap, bridge buzz, the output drive into the soft clipper,
//     and — for the modal bank — the damping law itself (negative tilt = highs outlive the
//     fundamental: an impossible material) and the mode geometry (48-mode custom sets: free bar,
//     circular membrane, plate, power-law "impossible" spectra). Nothing in Source/ is edited;
//     the engine code that runs is the shipping code.
//
//  JOB LINE (one per table; the same syntax on the command line for exploration):
//     NAME FAMILY FORM RATE F0 FRAMES SEGLEN key=a[:b[:curve]] ...
//  Every key ramps a -> b across the frames (u = f/(FRAMES-1), value = a + (b-a)·u^curve).
//  Public ModalParams: hard pos decay material breath stretch bloom halo age body vel
//  Time:     t0      seconds of the note to skip before the dumped segment (the time axis)
//  Overrides (after noteOn): drive bowslope beta reedslope jet lpcut loopgain disp buzz combdepth
//            outgain lipf lipq lipdb   modal: mset mpow msc tilt ag gpow
//  Output: <dir>/<NAME>.f32 (FRAMES x SEGLEN float32, mono) and <dir>/<NAME>.txt (metadata).
//
//  Build (as bank2_probe, per README):
//    c++ -std=c++17 -O2 -I ../../Tests/shim -I ../../Source bank3_probe.cpp -framework Accelerate -o /tmp/bank3
//    /tmp/bank3 <dump dir>                 # every job in JOBS[]
//    /tmp/bank3 <dump dir> NAME            # one job from JOBS[]
//    /tmp/bank3 <dump dir> --job <line>    # an ad-hoc job (exploration)
// ══════════════════════════════════════════════════════════════════════════════════════════════
#include <cstdint>
#include <cmath>
#include <vector>
#include <array>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <map>
#include <sstream>
#define private public          // probe-only: read and override the per-note coefficients
#include "ModalEngine.h"
#undef private

using namespace tw;

struct Ramp
{
    float a = 0.f, b = 0.f, c = 1.f;
    float at (float u) const { return a + (b - a) * std::pow (std::max (0.f, u), c); }
};

struct Job
{
    std::string name; int fam = 0, form = 0; double rate = 44100.0, f0 = 55.0; int frames = 128, seglen = 16384;
    std::map<std::string, Ramp> r;
    bool  has (const char* k) const { return r.count (k) > 0; }
    float get (const char* k, float u, float def) const { auto it = r.find (k); return it == r.end() ? def : it->second.at (u); }
};

static bool parseJob (const std::string& line, Job& J)
{
    std::istringstream is (line);
    if (! (is >> J.name >> J.fam >> J.form >> J.rate >> J.f0 >> J.frames >> J.seglen)) return false;
    std::string tok;
    while (is >> tok)
    {
        const auto eq = tok.find ('=');
        if (eq == std::string::npos) return false;
        Ramp rp; std::string v = tok.substr (eq + 1);
        float x[3] = { 0.f, 0.f, 1.f }; int n = 0; size_t s = 0;
        while (n < 3)
        {
            const auto c = v.find (':', s);
            x[n++] = std::strtof (v.substr (s, c == std::string::npos ? std::string::npos : c - s).c_str(), nullptr);
            if (c == std::string::npos) break;
            s = c + 1;
        }
        rp.a = x[0]; rp.b = (n >= 2) ? x[1] : x[0]; rp.c = (n >= 3) ? x[2] : 1.f;
        J.r[tok.substr (0, eq)] = rp;
    }
    return J.frames > 0 && J.seglen > 0 && J.rate > 1000.0 && J.f0 > 1.0;
}

static float clamp01f (float v) { return v < 0.f ? 0.f : (v > 1.f ? 1.f : v); }

// exact total phase delay of the dispersion cascade at w0 (same maths as ModalEngine::buildDispersion)
static float dispDelay (const ModalEngine& e, float w0)
{
    if (! e.dispActive_ || w0 <= 1e-5f) return 0.f;
    float pd = 0.f;
    for (int s = 0; s < modal::kDispStages; ++s)
    {
        const float A1 = e.disp_[s].a1, A2 = e.disp_[s].a2;
        const float c1 = std::cos (w0), s1 = std::sin (w0), c2 = std::cos (2.f * w0), s2 = std::sin (2.f * w0);
        const float Nre = A2 + A1 * c1 + c2, Nim = -(A1 * s1 + s2);
        const float Dre = 1.f + A1 * c1 + A2 * c2, Dim = -(A1 * s1 + A2 * s2);
        float ph = std::atan2 (Nim, Nre) - std::atan2 (Dim, Dre);
        while (ph > 0.f) ph -= 2.f * modal::kPi;
        while (ph <= -2.f * modal::kPi) ph += 2.f * modal::kPi;
        pd += -ph / w0;
    }
    return std::max (0.f, pd);
}

// The air columns octave-fold below their speaking floor (ModalEngine::buildLoops). Mirror it.
static float speakingHz (int fam, float f0)
{
    const float floorHz = (fam == modal::FLUTE) ? 90.f : (fam == modal::BRASS) ? 52.f : 34.f;
    float f = f0; while (f < floorHz) f *= 2.f; return f;
}

// 48-mode geometries the shipping tables do not have (ratios, first = 1)
static std::vector<float> customModes (int set, float p)
{
    std::vector<float> m;
    const int N = modal::kMaxModes;
    if (set == 1)        // free-free bar (Euler-Bernoulli): ((2k+1)/3.0112)^2
        for (int k = 1; k <= N; ++k) { const float b = (k == 1) ? 3.0112f : (2.f * k + 1.f); m.push_back ((b * b) / (3.0112f * 3.0112f)); }
    else if (set == 2)   // circular membrane: Bessel zeros j_{m,n} (McMahon), sorted, / j_{0,1}
    {
        std::vector<float> z;
        for (int mm = 0; mm < 14; ++mm)
            for (int nn = 1; nn <= 10; ++nn)
            {
                const double beta = (nn + 0.5 * mm - 0.25) * M_PI, mu = 4.0 * mm * mm;
                z.push_back ((float) (beta - (mu - 1.0) / (8.0 * beta) - 4.0 * (mu - 1.0) * (7.0 * mu - 31.0) / (3.0 * std::pow (8.0 * beta, 3.0))));
            }
        std::sort (z.begin(), z.end());
        for (int k = 0; k < N; ++k) m.push_back (z[(size_t) k] / z[0]);
    }
    else if (set == 3)   // simply-supported plate, aspect 1.37: (i/a)^2 + (j/b)^2, sorted
    {
        std::vector<float> z;
        for (int i = 1; i <= 16; ++i) for (int j = 1; j <= 16; ++j) z.push_back ((float) (i * i / (1.37 * 1.37) + j * j));
        std::sort (z.begin(), z.end());
        for (int k = 0; k < N; ++k) m.push_back (z[(size_t) k] / z[0]);
    }
    else                 // set 4: power law k^p — p<1 crowds the partials, p>1 is stiffer than any metal
        for (int k = 1; k <= N; ++k) m.push_back (std::pow ((float) k, p));
    return m;
}

// Re-derive the modal bank with the engine's own laws, with the job's overrides applied.
static void rebuildModes (ModalEngine& e, const Job& J, float u, float f0)
{
    std::vector<float> ratios;
    const int mset = (int) std::lround (J.get ("mset", u, 0.f));
    if (mset == 0)
    {
        const float* rr; int n;
        ModalEngine::modeTable (e.p_.family, ModalEngine::formOf (e.p_.family, e.p_.form).modeSet, rr, n);
        ratios.assign (rr, rr + n);
    }
    else ratios = customModes (mset, J.get ("mpow", u, 1.f));
    const int n = std::min ((int) ratios.size(), modal::kMaxModes);
    const float sc   = J.get ("msc", u, (clamp01f (e.p_.stretch) - 0.5f) * 2.f);
    const float mtrl = clamp01f (e.p_.material), dec = clamp01f (e.p_.decay), pos = clamp01f (e.p_.pos);
    const float tilt = J.get ("tilt", u, 1.9f + (0.16f - 1.9f) * mtrl);
    const float aG   = J.get ("ag",   u, 2.4f + (0.55f - 2.4f) * mtrl);
    const float gpow = J.get ("gpow", u, 1.f);
    const float t60base = 0.05f * std::pow (2.f, dec * 9.5f);
    const float alphaBase = 6.9078f / (t60base + 1e-4f);
    const float rate = (float) e.rate_;
    float peakA = 1e-6f;
    e.nModes_ = n;
    for (int k = 0; k < n; ++k)
    {
        float ratio = ratios[(size_t) k];
        const float harmk = (float) (k + 1);
        if (sc < 0.f)      ratio = ratio + (harmk - ratio) * std::min (1.f, -sc);
        else if (sc > 0.f) ratio = ratio * (1.f + 0.5f * sc * (float) k / (float) std::max (1, n));
        const float fk = f0 * ratio;
        auto& m = e.modes_[(size_t) k];
        if (fk <= 0.f || fk >= 0.49f * rate) { m.amp = 0.f; m.freqHz = 0.f; continue; }
        const float alpha = alphaBase * aG * std::pow (fk / std::max (1.f, f0), tilt);
        float r = std::exp (-alpha / rate); if (r > 0.99998f) r = 0.99998f;
        float g = std::fabs (std::sin (harmk * modal::kPi * pos));
        g = (0.15f + 0.85f * g) / std::pow (0.6f + 0.5f * (float) k, gpow);
        m.set (2.f * modal::kPi * fk / rate, r); m.amp = g; m.freqHz = fk; m.reset();
        peakA = std::max (peakA, g);
    }
    for (int k = 0; k < n; ++k) e.modes_[(size_t) k].amp *= 0.6f / peakA;
}

static void applyOverrides (ModalEngine& e, const Job& J, float u, float f0)
{
    const float rate = (float) e.rate_;
    if (J.has ("outgain"))   e.outGain_   = J.get ("outgain", u, 1.f);
    if (J.has ("drive"))     e.contDrive_ = J.get ("drive", u, 0.f);
    if (J.has ("buzz"))      e.ageBuzz_   = J.get ("buzz", u, 0.f);
    if (J.has ("combdepth")) e.combDepth_ = J.get ("combdepth", u, 0.5f);
    if (J.has ("loopgain"))  e.loopGain_  = J.get ("loopgain", u, 0.99f);
    if (J.has ("bowslope"))  e.bowSlope_  = J.get ("bowslope", u, 3.f);
    if (J.has ("reedslope")) e.reedSlope_ = J.get ("reedslope", u, -0.3f);
    if (J.has ("lipf") || J.has ("lipq") || J.has ("lipdb"))
        e.lipBq_.peaking (std::min (J.get ("lipf", u, 850.f), 0.45f * rate), rate, J.get ("lipq", u, 1.2f), J.get ("lipdb", u, 6.f));

    const bool loops = (e.core_ != 2);
    if (loops && (J.has ("lpcut") || J.has ("disp") || J.has ("beta") || J.has ("jet")))
    {
        if (J.has ("lpcut"))
        {
            const float c = std::min (J.get ("lpcut", u, 5000.f), 0.45f * rate);
            e.lpA_.setCutoff (c, rate); e.lpB_.setCutoff (c * 0.96f, rate);
        }
        if (J.has ("disp"))            // past the engine's 0.85 cap; a2 = 0 keeps it first-order
        {
            const float a = std::max (0.f, std::min (0.995f, J.get ("disp", u, 0.f)));
            e.dispActive_ = a > 1e-4f;
            for (int s = 0; s < modal::kDispStages; ++s) { e.disp_[s].set (-a, 0.f); e.dispB_[s].set (-a, 0.f); e.disp_[s].reset(); e.dispB_[s].reset(); }
        }
        // re-anchor the loop so the fundamental stays at f0 (the engine's a-C-is-a-C law)
        const float period = rate / std::max (20.f, f0);
        const float lpPd = e.lpA_.phaseDelay();
        const float ozPd = (e.core_ == 0) ? 0.5f : 0.f;
        const float dPd  = dispDelay (e, 2.f * modal::kPi * std::min (f0, 0.45f * rate) / rate);
        e.dispPhaseDelay_ = dPd;
        const float La = std::max (2.f, std::min ((float) modal::kMaxDelay - 3.f, period - lpPd - ozPd - dPd - 1.f));
        if (e.core_ == 0) { e.dlA_.setDelay (La); e.dlB_.setDelay (La); }
        const float fEff = speakingHz (e.p_.family, f0);
        switch (e.p_.family)
        {
            case modal::BOW:
            {
                const float beta = J.get ("beta", u, 0.08f + 0.28f * clamp01f (e.p_.pos));
                e.boreN_.setDelay (std::max (2.f, La * (1.f - beta)));
                e.boreB_.setDelay (std::max (2.f, La * beta));
                break;
            }
            case modal::FLUTE:
            {
                const float bore = rate / std::max (20.f, fEff * 0.6667f);
                e.boreN_.setDelay (std::max (4.f, bore - lpPd - 1.f));
                e.boreB_.setDelay (std::max (2.f, bore * J.get ("jet", u, 0.08f + 0.48f * clamp01f (e.p_.pos))));
                break;
            }
            case modal::REED:
            {
                const bool cone = (ModalEngine::formOf (e.p_.family, e.p_.form).modeSet == 1);
                const float bore = cone ? rate / std::max (20.f, fEff) : 0.5f * rate / std::max (20.f, fEff);
                e.boreN_.setDelay (std::max (4.f, bore - lpPd - 1.f));
                break;
            }
            case modal::BRASS:
                e.boreN_.setDelay (std::max (4.f, rate / std::max (20.f, fEff) - lpPd - 1.f));
                break;
            default: break;
        }
    }
    if (! loops && (J.has ("mset") || J.has ("mpow") || J.has ("msc") || J.has ("tilt") || J.has ("ag") || J.has ("gpow")))
        rebuildModes (e, J, u, f0);
}

static int runJob (const char* dir, const Job& J)
{
    std::vector<float> out ((size_t) J.frames * (size_t) J.seglen, 0.f);
    const int B = 512;
    std::vector<float> L (B), R (B);
    double pk = 0.0; int silentFrames = 0, bad = 0;
    for (int f = 0; f < J.frames; ++f)
    {
        const float u = (J.frames > 1) ? (float) f / (float) (J.frames - 1) : 0.f;
        ModalParams p;
        p.family = J.fam; p.form = J.form;
        p.hard = J.get ("hard", u, 0.5f); p.pos = J.get ("pos", u, 0.28f); p.decay = J.get ("decay", u, 0.6f);
        p.material = J.get ("material", u, 0.5f); p.breath = J.get ("breath", u, 0.f);
        p.stretch = J.get ("stretch", u, 0.5f); p.bloom = J.get ("bloom", u, 0.f); p.halo = J.get ("halo", u, 0.f);
        p.age = J.get ("age", u, 0.f); p.body = J.get ("body", u, 0.5f);
        ModalEngine e;
        e.prepare (J.rate, true); e.setParams (p); e.setPitchRatio (1.0); e.setPlayedHz (J.f0);
        e.noteOn (J.f0, 0x3A11CEu, J.get ("vel", u, 0.95f));
        applyOverrides (e, J, u, (float) J.f0);
        long skip = (long) std::llround (J.get ("t0", u, 0.f) * J.rate);
        while (skip > 0)
        {
            const int n = (int) std::min<long> (B, skip);
            std::fill (L.begin(), L.end(), 0.f); std::fill (R.begin(), R.end(), 0.f);
            e.silentBlocks_ = 0;                       // a quiet tail is still a tail: never free the voice
            e.renderBlockAdd (L.data(), R.data(), n); skip -= n;
        }
        float* row = &out[(size_t) f * (size_t) J.seglen];
        int w = 0; double fp = 0.0;
        while (w < J.seglen)
        {
            const int n = std::min (B, J.seglen - w);
            std::fill (L.begin(), L.end(), 0.f); std::fill (R.begin(), R.end(), 0.f);
            e.silentBlocks_ = 0;
            e.renderBlockAdd (L.data(), R.data(), n);
            for (int i = 0; i < n; ++i)
            {
                float v = L[(size_t) i];
                if (! std::isfinite (v)) { v = 0.f; ++bad; }
                row[w + i] = v; fp = std::max (fp, (double) std::fabs (v));
            }
            w += n;
        }
        if (fp < 1e-7) ++silentFrames;
        pk = std::max (pk, fp);
    }
    char path[2048];
    std::snprintf (path, sizeof path, "%s/%s.f32", dir, J.name.c_str());
    std::FILE* fp = std::fopen (path, "wb");
    if (! fp) { std::printf ("  !! cannot write %s\n", path); return 1; }
    std::fwrite (out.data(), sizeof (float), out.size(), fp); std::fclose (fp);
    std::snprintf (path, sizeof path, "%s/%s.txt", dir, J.name.c_str());
    fp = std::fopen (path, "w");
    std::fprintf (fp, "%.6f %.9f %d %d %d %d\n", J.rate, J.f0, J.frames, J.seglen, J.fam, J.form);
    std::fclose (fp);
    static const char* core[] = { "string", "reedbore", "modal" };
    std::printf ("  %-22s fam %d form %d %-8s rate %6.0f f0 %8.3f period %7.1f  peak %.3f  silent %d  nonfinite %d  %s\n",
                 J.name.c_str(), J.fam, J.form, core[modal::coreOf (J.fam)], J.rate, J.f0, J.rate / J.f0, pk,
                 silentFrames, bad, (pk > 1e-5 && silentFrames == 0 && bad == 0) ? "ok" : "*** CHECK ***");
    return (pk > 1e-5 && silentFrames == 0) ? 0 : 2;
}

// ── THE JOBS (the tables gen2_physical.py ships). Filled in as the tables are designed. ──
static const char* JOBS[] = {
    // REED-BORE · clarinet: breath inside the speaking window, loop cutoff 250 Hz -> 40 kHz, then driven into the clipper
    "CLARINET_BLOWOUT 4 0 176400 86.1328125 128 16384 t0=0.5 drive=0.72:1.05 lpcut=250:40000:2.2 outgain=0.12:0.9:3",
    // REED-BORE · sax (cone): a stiff bright reed relaxing and the bore darkening until the tone collapses into breath.
    //   f0 is DOUBLED: the cone bore + inverting reflection oscillates an octave below the played note (see gen2_physical).
    "REED_COLLAPSE 4 1 176400 172.265625 128 16384 t0=0.5 drive=0.75 reedslope=-0.9:-0.2 lpcut=40000:600:0.5 outgain=0.12",
    // REED-BORE · trumpet: a +22 dB, Q6 mouthpiece formant swept 3 kHz -> 150 Hz until the lip loop loses its period
    "BRASS_LIP_CHAOS 5 0 176400 172.265625 128 16384 t0=0.5 drive=0.9 lipf=3000:150:0.7 lipq=6 lipdb=22 outgain=0.1",
    // REED-BORE · trumpet: lips blown across the speaking window, bore loss opening 700 Hz -> 40 kHz (a 300 Hz start let
    //   the breath turbulence bury the tone at frame 0), mouthpiece formant climbing 600 Hz -> 4 kHz / +4 -> +26 dB, the
    //   output pushed into the soft clipper
    "BRASS_RIP 5 0 176400 172.265625 128 16384 t0=0.5 drive=0.62:1.35 lipf=600:4000 lipdb=4:26 outgain=0.08:1.4:2 lpcut=700:40000:2.5",
    // REED-BORE · flute: the jet delay pushed from its speaking length to 1.3x the bore -> register hops and chaos
    "FLUTE_REGISTER_HOP 3 0 176400 129.19921875 128 16384 t0=0.5 jet=0.34:1.3 outgain=0.03",
    // STRING · lute pluck at the very end (pos 0.03), dispersion 0.99 (engine cap 0.85), loop cutoff wide open:
    //   3 s of one note, played BACKWARDS by gen2
    "WIRE_REWIND 1 2 176400 86.1328125 128 16384 t0=0.0:3:1.8 hard=0.97 material=1 decay=0.995 disp=0.99 lpcut=80000 pos=0.03",
    // STRING · steel pluck, HALO 1: the detuned second polarisation beating against the first over 3 s of one note
    "SYMPATHETIC_BEAT 1 0 176400 86.1328125 128 16384 t0=0.02:3.0:1.3 halo=1 hard=0.95 decay=0.97 material=0.95",
    // STRING · concert grand at 21.5 Hz (below A0), STRETCH 1; each frame is its own note, so the hammer hardens and
    //   the material brightens as the read point moves into the attack (2.5 s -> 0 once gen2 plays it BACKWARDS)
    "SUB_PIANO_CLANG 0 0 44100 21.533203125 128 16384 t0=0.0:2.5:1.4 hard=1:0 material=1:0 stretch=1 decay=0.9",
    // MODAL · 48-mode free bar, soft mallet, damping law INVERTED: the highs outlive the fundamental (time axis)
    "IMPOSSIBLE_BELL 7 0 176400 86.1328125 128 16384 t0=0:4:1.6 tilt=-0.5 ag=1.2 decay=0.8 hard=0.15 mset=1",
    // MODAL · 48 modes at ratios k^p, p 0.7 -> 2.0: crowded below harmonic, then stiffer than any metal
    "MODE_FAN 6 0 176400 86.1328125 128 16384 t0=0.02 hard=0.95 decay=0.9 material=0.8 mset=4 mpow=0.7:2.0 gpow=0.5",
    // MODAL · circular-membrane Bessel set (48 modes): 2 s of a hit whose strike walks rim -> centre, the damping tilt
    //   going woody and the clipper drive easing off (played BACKWARDS by gen2: thud -> crackling rim shot)
    "BESSEL_DRUM 8 0 176400 86.1328125 128 16384 t0=0.005:2.0:1.6 mset=2 hard=0.95 decay=0.8 material=0.8 pos=0.05:0.5 tilt=0.3:2.0 outgain=1.6:0.3",
    nullptr
};

int main (int argc, char** argv)
{
    const char* dir = (argc > 1) ? argv[1] : ".";
    if (argc > 3 && std::strcmp (argv[2], "--job") == 0)
    {
        std::string line;
        for (int i = 3; i < argc; ++i) { line += argv[i]; line += ' '; }
        Job J; if (! parseJob (line, J)) { std::printf ("bad job line: %s\n", line.c_str()); return 3; }
        return runJob (dir, J);
    }
    int rc = 0, n = 0;
    for (const char* const* l = JOBS; *l; ++l)
    {
        Job J; if (! parseJob (*l, J)) { std::printf ("bad job line: %s\n", *l); rc = 3; continue; }
        if (argc > 2 && J.name != argv[2]) continue;
        rc |= runJob (dir, J); ++n;
    }
    std::printf ("\n  bank3_probe: %d job(s) -> %s\n", n, dir);
    return rc;
}
