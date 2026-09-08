// ─────────────────────────────────────────────────────────────────────────────
// bod_cert — the certification harness for the BODE frequency shifter (kind 13).
//   clang++ -O2 -std=c++17 -I Tests/shim -I Source Tests/bod_cert.cpp -o bod_cert
//
// THE ORDER OF THESE GATES IS THE POINT.
//   §A runs FIRST and it is DIRECTION. A frequency shifter with the sideband
//   inverted is a working frequency shifter going the other way: it passes every
//   level gate, every click gate, every spectrum-changed gate, and it is not the
//   instrument that was asked for. The build bible's own first draft had this
//   formula swapped and only a time-domain probe caught it (fb444 recon). So the
//   very first number this harness prints is WHERE THE ENERGY WENT.
//
//   §B is RANGE, for the same reason: the engine this device grew out of caps at
//   +-1000 Hz behind a clamp that reads +-2000, and every knob position still
//   "does something" at a fifth of the asked-for range.
//
//   §T (fb445) is THE TYPES ARE DIFFERENT, and it exists because fb444 shipped
//   eight Type NAMES over ONE topology: `Params::type` was a field nothing read.
//   Every gate in this file was green while seven of the eight names were a lie.
//   A gate that proves "the engine works" never proves "the CONTROL reaches a
//   different engine" -- the same lesson as fb373, one level up. §T measures all
//   28 pairs on two probes and then proves each Type's own mechanism engages.
//
// fb441 — CERT MUST SEED BEFORE IT MEASURES. A fresh engine snaps to its first
// block's parameters and hides every steady-state bug; each measurement below
// runs the engine for a seed interval FIRST and only then captures. fb445 makes
// the seed DELAY-AWARE: an Echobode with a 4 s repeat needs 5 s of seed, and a
// fixed 0.45 s seed measured two adjacent Time positions as identical silence.
// ─────────────────────────────────────────────────────────────────────────────
#include "TerrainBodeFx.h"
#include <cstdio>
#include <cstring>
#include <complex>
#include <string>
#include <vector>
#include <cmath>

static int gPass = 0, gFail = 0;
static void gate (const char* what, bool ok, const std::string& d)
{
    if (ok) { ++gPass; std::printf ("  ok    %-56s %s\n", what, d.c_str()); }
    else    { ++gFail; std::printf ("  FAIL  %-56s %s\n", what, d.c_str()); }
}

// ── a small radix-2 FFT ──────────────────────────────────────────────────────
static void fft (std::vector<std::complex<double>>& a)
{
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i)
    {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap (a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1)
    {
        const double ang = -2.0 * M_PI / (double) len;
        std::complex<double> wl (std::cos (ang), std::sin (ang));
        for (size_t i = 0; i < n; i += len)
        {
            std::complex<double> w (1.0, 0.0);
            for (size_t k = 0; k < len / 2; ++k)
            {
                auto u = a[i + k], v = a[i + k + len / 2] * w;
                a[i + k] = u + v; a[i + k + len / 2] = u - v; w *= wl;
            }
        }
    }
}

static constexpr int   NFFT = 32768;
static constexpr float FS   = 48000.0f;
static constexpr float BUS  = 0.05f;      // the rack bus sits near -26 dBFS

struct Spec { std::vector<double> mag; double binHz; };

// Run the engine and return the magnitude spectrum of its output.
// seedSec: how long to run BEFORE capturing (fb441 — never measure a cold engine).
static Spec runTone (tw::TerrainBodeFx& e, float toneHz, float amp,
                     double seedSec = 0.35, int chan = 0)
{
    const int seedN = (int) (seedSec * FS);
    float l, r;
    double ph = 0.0;
    const double inc = 2.0 * M_PI * (double) toneHz / FS;
    for (int i = 0; i < seedN; ++i)
    {
        const float s = amp * (float) std::sin (ph); ph += inc;
        e.processStereo (s, s, l, r);
    }
    std::vector<std::complex<double>> buf ((size_t) NFFT);
    for (int i = 0; i < NFFT; ++i)
    {
        const float s = amp * (float) std::sin (ph); ph += inc;
        e.processStereo (s, s, l, r);
        const double w = 0.5 - 0.5 * std::cos (2.0 * M_PI * i / (NFFT - 1));   // Hann
        buf[(size_t) i] = (chan == 0 ? l : r) * w;
    }
    fft (buf);
    Spec sp; sp.binHz = (double) FS / NFFT;
    sp.mag.resize ((size_t) NFFT / 2);
    for (int i = 0; i < NFFT / 2; ++i) sp.mag[(size_t) i] = std::abs (buf[(size_t) i]);
    return sp;
}

static double energyNear (const Spec& s, double hz, double widthHz = 25.0)
{
    const int lo = (int) std::max (0.0, (hz - widthHz) / s.binHz);
    const int hi = (int) std::min ((double) s.mag.size() - 1, (hz + widthHz) / s.binHz);
    double e = 0.0;
    for (int i = lo; i <= hi; ++i) e += s.mag[(size_t) i] * s.mag[(size_t) i];
    return e;
}
static double energyBand (const Spec& s, double loHz, double hiHz)
{
    const int lo = (int) std::max (0.0, loHz / s.binHz);
    const int hi = (int) std::min ((double) s.mag.size() - 1, hiHz / s.binHz);
    double e = 0.0;
    for (int i = lo; i <= hi; ++i) e += s.mag[(size_t) i] * s.mag[(size_t) i];
    return e;
}
static double peakHz (const Spec& s)
{
    size_t best = 1; double bv = 0.0;
    for (size_t i = 1; i < s.mag.size(); ++i) if (s.mag[i] > bv) { bv = s.mag[i]; best = i; }
    return (double) best * s.binHz;
}
static double db (double x) { return 20.0 * std::log10 (std::max (1e-12, x)); }

static tw::TerrainBodeFx::Params base()
{
    tw::TerrainBodeFx::Params p;      // defaults: no shift, no feedback, full wet
    p.shift = 0.5f; p.dir = 1.0f; p.fdbk = 0.0f; p.mix = 1.0f;
    p.fine = 0.5f; p.spread = 1.0f; p.time = 0.45f; p.blur = 0.0f;
    p.lowKeep = 0.0f; p.damping = 1.0f; p.touch = 0.5f; p.drift = 0.0f;
    return p;
}
// Turn a wanted shift in Hz into the knob position that produces it.
static float knobForHz (float hz)
{
    const float v = (hz < 0.0f ? -1.0f : 1.0f)
                  * std::log (std::fabs (hz) + 1.0f) / std::log (tw::TerrainBodeFx::kShiftMax + 1.0f);
    return 0.5f * (v + 1.0f);
}

// ═══ fb445 — THE TYPE TABLES. These are the engine's own per-Type mappings,
// restated here so the harness can ask for "200 Hz on THIS Type" and so §T can
// gate that the mapping really is the Type's and not one shared table.
static const float kSpanT[8] = { 5000.0f, 2500.0f, 4000.0f,   25.0f,
                                 5000.0f, 3000.0f, 2000.0f, 1500.0f };
static const float kTLo[8]   = { 0.02f,   1.0f,   30.0f,  0.5f, 0.05f,  5.0f,   2.0f,   40.0f };
static const float kTHi[8]   = { 1000.0f, 300.0f, 4000.0f, 60.0f, 500.0f, 700.0f, 400.0f, 2000.0f };
static const char* kTypeName[8] = { "Shift", "Barberpole", "Echobode", "Detune",
                                    "Ring", "Spiral", "Chorale", "Freeze" };

static float knobForTypeHz (int t, float hz)
{
    if (t == 4)   // RING — the Shift knob is a UNIPOLAR carrier, 0.1 Hz .. 5 kHz
        return std::log (std::max (0.1f, hz) / 0.1f) / std::log (50000.0f);
    const float v = (hz < 0.0f ? -1.0f : 1.0f)
                  * std::log (std::fabs (hz) + 1.0f) / std::log (kSpanT[t] + 1.0f);
    return 0.5f * (v + 1.0f);
}
static float knobForTypeMs (int t, float ms)
{
    return std::log (ms / kTLo[t]) / std::log (kTHi[t] / kTLo[t]);
}
static float typeDelaySec (int t, float timeKnob)
{
    const float k = timeKnob < 0.0f ? 0.0f : (timeKnob > 1.0f ? 1.0f : timeKnob);
    return 0.001f * kTLo[t] * std::pow (kTHi[t] / kTLo[t], k);
}
// fb445 — THE SEED MUST OUTLAST THE LOOP. An Echobode at 4 s repeats nothing
// inside a 0.45 s seed, and two adjacent Time positions then measure as the same
// silence. Seed for the delay the Type actually has.
static double seedFor (int t, float timeKnob, double floorSec = 0.6)
{
    return std::max (floorSec, 0.5 + 1.6 * (double) typeDelaySec (t, timeKnob));
}

// ── §T's fingerprint: 1/6-OCTAVE BANDS, floored 80 dB under the band peak.
//    fb445 — THE METRIC, NOT THE BAR, one more time. The first version of §T
//    averaged |dB difference| PER BIN over 40 Hz..16 kHz, and reported Echobode
//    and Detune -- a 271 ms echo device and a 0.9 Hz beater -- as 0.91 dB apart
//    on a tone probe. Nothing was wrong with either engine: a pure tone leaves
//    ~16000 of 16384 bins holding numerical noise, and the mean over a spectrum
//    that is mostly empty is mostly the empty part. Bands are what an ear has,
//    they are what the brief asks for ("3 dB mean per band"), and the same two
//    Types come out 9.6 dB apart under one. (§F keeps its own per-bin lambda
//    untouched, so its published numbers do not move.)
static std::vector<double> fpFinish (std::vector<std::complex<double>>& buf)
{
    fft (buf);
    const double binHz = (double) FS / NFFT;
    const double step  = std::pow (2.0, 1.0 / 6.0);
    std::vector<double> out;
    double pk = 1e-30;
    for (double f = 40.0; f < 16000.0; f *= step)
    {
        const int lo = std::max (1, (int) (f / binHz));
        const int hi = std::min (NFFT / 2 - 1, (int) (f * step / binHz));
        double acc = 0.0;
        for (int i = lo; i <= hi; ++i)
        { const double m = std::abs (buf[(size_t) i]); acc += m * m; }
        acc = std::sqrt (acc / (double) std::max (1, hi - lo + 1));
        out.push_back (acc);
        pk = std::max (pk, acc);
    }
    const double fl = pk * 1.0e-4;               // -80 dB under the loudest band
    for (auto& v : out) v = db (std::max (v, fl));
    return out;
}
static std::vector<double> fpNoise (tw::TerrainBodeFx& e, double seedSec)
{
    float l, r; uint32_t rng = 2463534242u;
    auto nxt = [&rng]() { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
                          return BUS * (((float) (rng & 0xFFFFu) / 32768.0f) - 1.0f); };
    for (int i = 0; i < (int) (seedSec * FS); ++i) { const float n = nxt(); e.processStereo (n, n, l, r); }
    std::vector<std::complex<double>> buf ((size_t) NFFT);
    for (int i = 0; i < NFFT; ++i)
    {
        const float n = nxt(); e.processStereo (n, n, l, r);
        const double w = 0.5 - 0.5 * std::cos (2.0 * M_PI * i / (NFFT - 1));
        buf[(size_t) i] = l * w;
    }
    return fpFinish (buf);
}
// ── THE BROADBAND PROBE HAS TO HAVE PARTIALS IN IT (fb445, and it is fb417's
//    law again: prove it on what the ear is given). A frequency shifter is
//    DEFINED by what it does to partials — it moves them by a fixed number of
//    Hz, which destroys their ratios. WHITE NOISE HAS NO PARTIALS, and a
//    shifting feedback loop turns white noise into white noise whatever its
//    topology: measured over the same 28 pairs, white noise put Barberpole and
//    Spiral 1.60 dB apart while a 220 Hz tone put them 9.19 dB apart, and it was
//    the probe that was blind, not the engine. So the broadband probe here is a
//    band-limited SAWTOOTH — flat-ish band energy to 16 kHz, i.e. broadband by
//    any measure, but built out of the one thing this device exists to move.
//    The white-noise number is still measured below, as a NEGATIVE CONTROL.
static std::vector<double> fpSaw (tw::TerrainBodeFx& e, float f0, double seedSec)
{
    float l, r; double ph = 0.0; const double inc = 2.0 * M_PI * (double) f0 / FS;
    const int H = (int) (16000.0f / f0);
    auto nxt = [&]() {
        double v = 0.0;
        for (int h = 1; h <= H; ++h) v += std::sin ((double) h * ph) / (double) h;
        ph += inc;
        return (float) (BUS * v * 0.55);
    };
    for (int i = 0; i < (int) (seedSec * FS); ++i) { const float v = nxt(); e.processStereo (v, v, l, r); }
    std::vector<std::complex<double>> buf ((size_t) NFFT);
    for (int i = 0; i < NFFT; ++i)
    {
        const float v = nxt(); e.processStereo (v, v, l, r);
        const double w = 0.5 - 0.5 * std::cos (2.0 * M_PI * i / (NFFT - 1));
        buf[(size_t) i] = l * w;
    }
    return fpFinish (buf);
}
static std::vector<double> fpTone (tw::TerrainBodeFx& e, float hz, double seedSec)
{
    float l, r; double ph = 0.0; const double inc = 2.0 * M_PI * (double) hz / FS;
    for (int i = 0; i < (int) (seedSec * FS); ++i)
    { const float s = BUS * (float) std::sin (ph); ph += inc; e.processStereo (s, s, l, r); }
    std::vector<std::complex<double>> buf ((size_t) NFFT);
    for (int i = 0; i < NFFT; ++i)
    {
        const float s = BUS * (float) std::sin (ph); ph += inc; e.processStereo (s, s, l, r);
        const double w = 0.5 - 0.5 * std::cos (2.0 * M_PI * i / (NFFT - 1));
        buf[(size_t) i] = l * w;
    }
    return fpFinish (buf);
}
// mean |dB difference| PER BAND -- 52 bands of 1/6 octave from 40 Hz to 16 kHz.
static double specDistT (const std::vector<double>& a, const std::vector<double>& b)
{
    double acc = 0.0; size_t n = std::min (a.size(), b.size());
    for (size_t i = 0; i < n; ++i) acc += std::fabs (a[i] - b[i]);
    return acc / (double) std::max<size_t> (1, n);
}

// ── §G's probe: NOISE + A TONE. fb417's lesson, run the other way round. A
//    magnitude spectrum of steady NOISE is blind to a frequency shift — noise
//    shifted by 5 Hz and by 10 Hz have the same expected spectrum — so a
//    noise-only travel gate would call Detune's Shift knob dead when it is the
//    most audible control on the Type. A tone in the probe puts a LINE in the
//    spectrum whose position the shift moves, and the noise keeps the filters
//    and the diffusion measurable. One probe, both mechanisms.
static std::vector<double> fpMix (tw::TerrainBodeFx& e, double seedSec)
{
    float l, r; uint32_t rng = 2463534242u; double ph = 0.0;
    const double inc = 2.0 * M_PI * 220.0 / FS;
    auto nxt = [&]() {
        rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
        const float n = BUS * (((float) (rng & 0xFFFFu) / 32768.0f) - 1.0f);
        const float s = BUS * (float) std::sin (ph); ph += inc;
        return 0.7f * (n + s);
    };
    for (int i = 0; i < (int) (seedSec * FS); ++i) { const float v = nxt(); e.processStereo (v, v, l, r); }
    std::vector<std::complex<double>> buf ((size_t) NFFT);
    for (int i = 0; i < NFFT; ++i)
    {
        const float v = nxt(); e.processStereo (v, v, l, r);
        const double w = 0.5 - 0.5 * std::cos (2.0 * M_PI * i / (NFFT - 1));
        buf[(size_t) i] = l * w;
    }
    return fpFinish (buf);
}

// The same probe, read PER BIN instead of per band. §T asks "is this a different
// machine", which is a gross-spectral-shape question and wants bands. §G asks
// "does this knob keep transforming", which fb444 already established wants BINS:
// Blur is a chain of ALLPASSES and Time moves comb fine structure, and a band
// average smooths away precisely the thing they move. Same probe, two questions,
// two instruments. (Safe unfloored because fpMix's noise fills every bin: there
// is no empty bin whose numerical dither could dominate the mean.)
static std::vector<double> fpMixBins (tw::TerrainBodeFx& e, double seedSec)
{
    float l, r; uint32_t rng = 2463534242u; double ph = 0.0;
    const double inc = 2.0 * M_PI * 220.0 / FS;
    auto nxt = [&]() {
        rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
        const float n = BUS * (((float) (rng & 0xFFFFu) / 32768.0f) - 1.0f);
        const float sg = BUS * (float) std::sin (ph); ph += inc;
        return 0.7f * (n + sg);
    };
    for (int i = 0; i < (int) (seedSec * FS); ++i) { const float v = nxt(); e.processStereo (v, v, l, r); }
    std::vector<std::complex<double>> buf ((size_t) NFFT);
    for (int i = 0; i < NFFT; ++i)
    {
        const float v = nxt(); e.processStereo (v, v, l, r);
        const double w = 0.5 - 0.5 * std::cos (2.0 * M_PI * i / (NFFT - 1));
        buf[(size_t) i] = l * w;
    }
    fft (buf);
    std::vector<double> out ((size_t) (NFFT / 2));
    for (int i = 0; i < NFFT / 2; ++i) out[(size_t) i] = db (std::abs (buf[(size_t) i]));
    return out;
}
static double specDistBin (const std::vector<double>& a, const std::vector<double>& b)
{
    double acc = 0.0; int n = 0;
    const int lo = (int) (40.0    / ((double) FS / NFFT));
    const int hi = (int) (16000.0 / ((double) FS / NFFT));
    for (int i = lo; i < hi && i < (int) a.size(); ++i)
    { acc += std::fabs (a[(size_t) i] - b[(size_t) i]); ++n; }
    return acc / std::max (1, n);
}

// Peak-of-|out| per hop from a burst then silence. Proves an ECHO arrives at the
// delay time (Echobode), and measures how long a hold lasts (Freeze).
static std::vector<double> burstEnv (tw::TerrainBodeFx& e, float hz, float amp,
                                     double burstSec, double totalSec, int hop)
{
    std::vector<double> out;
    const int total = (int) (totalSec * FS), bn = (int) (burstSec * FS);
    float l, r; double ph = 0.0; const double inc = 2.0 * M_PI * (double) hz / FS;
    double acc = 0.0; int k = 0;
    for (int i = 0; i < total; ++i)
    {
        float s = 0.0f;
        if (i < bn) { s = amp * (float) std::sin (ph); ph += inc; }
        e.processStereo (s, s, l, r);
        acc = std::max (acc, (double) std::max (std::fabs (l), std::fabs (r)));
        if (++k >= hop) { out.push_back (acc); acc = 0.0; k = 0; }
    }
    return out;
}

// Short-window RMS under a steady tone, after a seed. BEATING lives here: a
// 0.9 Hz amplitude cycle is invisible to any FFT window this harness can afford.
static std::vector<double> rmsTrack (tw::TerrainBodeFx& e, float hz, float amp,
                                     double seedSec, double measSec, int hop)
{
    float l, r; double ph = 0.0; const double inc = 2.0 * M_PI * (double) hz / FS;
    for (int i = 0; i < (int) (seedSec * FS); ++i)
    { const float s = amp * (float) std::sin (ph); ph += inc; e.processStereo (s, s, l, r); }
    std::vector<double> out; double acc = 0.0; int k = 0;
    for (int i = 0; i < (int) (measSec * FS); ++i)
    {
        const float s = amp * (float) std::sin (ph); ph += inc;
        e.processStereo (s, s, l, r);
        acc += (double) l * (double) l;
        if (++k >= hop) { out.push_back (std::sqrt (acc / hop)); acc = 0.0; k = 0; }
    }
    return out;
}

// The dominant modulation rate of an envelope track and its modulation index.
static void envRate (const std::vector<double>& env, double hopRate,
                     double loHz, double hiHz, double& rateOut, double& indexOut)
{
    double mean = 0.0; for (double v : env) mean += v;
    mean /= (double) std::max<size_t> (1, env.size());
    double best = 0.0, bestF = 0.0;
    for (double f = loHz; f <= hiHz; f += 0.01)
    {
        double re = 0.0, im = 0.0;
        for (size_t i = 0; i < env.size(); ++i)
        {
            const double t = (double) i / hopRate;
            re += (env[i] - mean) * std::cos (2.0 * M_PI * f * t);
            im += (env[i] - mean) * std::sin (2.0 * M_PI * f * t);
        }
        const double mg = 2.0 * std::sqrt (re * re + im * im) / (double) std::max<size_t> (1, env.size());
        if (mg > best) { best = mg; bestF = f; }
    }
    rateOut = bestF; indexOut = best / std::max (1e-12, mean);
}

// A common knob setting for §T: IDENTICAL positions on all eight Types. Whatever
// comes out is the Type, not the knobs.
static tw::TerrainBodeFx::Params tBase (int t)
{
    auto p = base();
    p.type = t;   p.shift = 0.72f; p.dir = 1.0f;  p.fdbk = 0.85f; p.mix = 1.0f;
    p.fine = 0.5f; p.spread = 1.0f; p.time = 0.45f; p.blur = 0.15f;
    p.lowKeep = 0.0f; p.damping = 0.80f; p.touch = 0.5f; p.drift = 0.0f;
    return p;
}

int main()
{
    std::printf ("\n══ bod_cert — BODE frequency shifter (kind 13) ══\n");

    // ═══════════════════════════════════════════════════════════════════════
    std::printf ("\n§A — DIRECTION. Run first. A flipped sideband is a different instrument.\n");
    {
        tw::TerrainBodeFx e; e.prepare (FS);
        auto p = base(); p.shift = knobForHz (100.0f); p.dir = 1.0f;   // UP
        e.setParams (p);
        auto s = runTone (e, 1000.0f, BUS);
        const double up = energyNear (s, 1100.0), dn = energyNear (s, 900.0);
        char d[192];
        std::snprintf (d, sizeof d, "1 kHz + 100 Hz, dir=up -> peak %.0f Hz  ·  1100 is %.1f dB over 900",
                       peakHz (s), db (std::sqrt (up)) - db (std::sqrt (dn)));
        gate ("dir=UP puts a 1 kHz tone at 1100 Hz, NOT 900", up > dn * 100.0, d);
    }
    {
        tw::TerrainBodeFx e; e.prepare (FS);
        auto p = base(); p.shift = knobForHz (100.0f); p.dir = 0.0f;   // DOWN
        e.setParams (p);
        auto s = runTone (e, 1000.0f, BUS);
        const double up = energyNear (s, 1100.0), dn = energyNear (s, 900.0);
        char d[192];
        std::snprintf (d, sizeof d, "dir=down -> peak %.0f Hz  ·  900 is %.1f dB over 1100",
                       peakHz (s), db (std::sqrt (dn)) - db (std::sqrt (up)));
        gate ("dir=DOWN mirrors it to 900 Hz", dn > up * 100.0, d);
    }
    {
        tw::TerrainBodeFx e; e.prepare (FS);
        auto p = base(); p.shift = knobForHz (100.0f); p.dir = 0.5f;   // RING
        e.setParams (p);
        auto s = runTone (e, 1000.0f, BUS);
        const double up = energyNear (s, 1100.0), dn = energyNear (s, 900.0);
        const double car = energyNear (s, 1000.0);
        char d[192];
        std::snprintf (d, sizeof d, "both sidebands within %.1f dB, carrier suppressed %.1f dB",
                       std::fabs (db (std::sqrt (up)) - db (std::sqrt (dn))),
                       db (std::sqrt (car)) - db (std::sqrt (up)));
        gate ("dir=CENTRE is ring mod: BOTH sidebands, no carrier",
              std::fabs (db (std::sqrt (up)) - db (std::sqrt (dn))) < 1.5
              && car < up * 0.25, d);
    }

    // ═══════════════════════════════════════════════════════════════════════
    std::printf ("\n§B — RANGE. The engine this grew from caps at 1000 Hz behind a clamp reading 2000.\n");
    {
        tw::TerrainBodeFx e; e.prepare (FS);
        auto p = base(); p.shift = 1.0f; p.dir = 1.0f; p.fine = 0.5f;
        e.setParams (p);
        auto s = runTone (e, 1000.0f, BUS);
        const double pk = peakHz (s);
        char d[192];
        std::snprintf (d, sizeof d, "knob at 100%%: 1 kHz -> %.0f Hz (want 6000)  ·  engine meter says %+.1f Hz",
                       pk, e.meterShiftHz());
        gate ("full travel really is +5000 Hz (not the inherited 1000)",
              pk > 5900.0 && pk < 6100.0, d);
    }
    {
        tw::TerrainBodeFx e; e.prepare (FS);
        auto p = base(); p.shift = 0.0f; e.setParams (p);
        auto s = runTone (e, 6000.0f, BUS);
        const double pk = peakHz (s);
        char d[192];
        std::snprintf (d, sizeof d, "knob at 0%%: 6 kHz -> %.0f Hz (want 1000)  ·  meter %+.1f Hz",
                       pk, e.meterShiftHz());
        gate ("and -5000 Hz the other way", pk > 940.0 && pk < 1060.0, d);
    }
    {
        // The negative control: this is the gate that would have caught a
        // verbatim lift of the old mapping.
        const float knob = 1.0f;
        const float vOld = 2.0f * knob - 1.0f;
        const float oldHz = std::pow (1001.0f, std::fabs (vOld)) - 1.0f;
        char d[192];
        std::snprintf (d, sizeof d, "the OLD FMAX=1000 mapping reaches only %.0f Hz at full travel", oldHz);
        gate ("... and the gate FAILS on the inherited mapping (negative control)",
              oldHz < 5900.0f, d);
    }

    // ═══════════════════════════════════════════════════════════════════════
    std::printf ("\n§C — NOTHING FREE-RUNS. Feedback maxed, input removed, the tail must die.\n");
    for (int chr = 0; chr < tw::TerrainBodeFx::kNumChars; ++chr)
    {
        tw::TerrainBodeFx e; e.prepare (FS);
        auto p = base(); p.fdbk = 1.0f; p.chr = chr; p.shift = knobForHz (37.0f);
        p.time = 0.35f; p.damping = 1.0f;
        e.setParams (p);
        float l, r; double ph = 0.0; const double inc = 2.0 * M_PI * 220.0 / FS;
        for (int i = 0; i < (int) (0.5 * FS); ++i)
        { const float s = BUS * (float) std::sin (ph); ph += inc; e.processStereo (s, s, l, r); }
        double pk = 0.0;
        for (int i = 0; i < (int) (1.0 * FS); ++i)
        { e.processStereo (0.0f, 0.0f, l, r);
          if (i > (int) (0.6 * FS)) pk = std::max (pk, (double) std::max (std::fabs (l), std::fabs (r))); }
        char d[160]; std::snprintf (d, sizeof d, "char %d: tail after 1 s of silence = %.1f dBFS", chr, db (pk));
        gate ("0.5 s note -> 1 s silence, tail below -70 dBFS", pk < 3.16e-4, d);
    }
    // fb445 — SWEEP THE FULL MATRIX (fb425). Above is Type 0 x 8 Characters, the
    // fb444 gate, unchanged. Below is every OTHER Type x 8 Characters, because
    // Freeze deliberately relaxes the very mechanism this law is about and the
    // other six changed their loop ceilings. One gate per Type, worst cell shown.
    for (int t = 1; t < tw::TerrainBodeFx::kNumTypes; ++t)
    {
        double worst = 0.0; int worstC = 0;
        for (int chr = 0; chr < tw::TerrainBodeFx::kNumChars; ++chr)
        {
            tw::TerrainBodeFx e; e.prepare (FS);
            auto p = base(); p.type = t; p.fdbk = 1.0f; p.chr = chr;
            p.shift = knobForTypeHz (t, 37.0f); p.time = 0.35f; p.damping = 1.0f;
            e.setParams (p);
            float l, r; double ph = 0.0; const double inc = 2.0 * M_PI * 220.0 / FS;
            for (int i = 0; i < (int) (0.5 * FS); ++i)
            { const float s = BUS * (float) std::sin (ph); ph += inc; e.processStereo (s, s, l, r); }
            double pk = 0.0;
            for (int i = 0; i < (int) (1.0 * FS); ++i)
            { e.processStereo (0.0f, 0.0f, l, r);
              if (i > (int) (0.6 * FS)) pk = std::max (pk, (double) std::max (std::fabs (l), std::fabs (r))); }
            if (pk > worst) { worst = pk; worstC = chr; }
        }
        char d[176];
        std::snprintf (d, sizeof d, "%-10s worst of 8 Characters = %.1f dBFS (char %d)",
                       kTypeName[t], db (worst), worstC);
        gate ("... and on every other Type, at max feedback", worst < 3.16e-4, d);
    }

    // ═══════════════════════════════════════════════════════════════════════
    std::printf ("\n§D — LOOP STABILITY. A shifter in a loop walks its content out of band.\n");
    {
        bool bad = false; double worst = 0.0; int badT = -1, badC = -1;
        for (int t = 0; t < tw::TerrainBodeFx::kNumTypes && ! bad; ++t)
            for (int c = 0; c < tw::TerrainBodeFx::kNumChars && ! bad; ++c)
            {
                tw::TerrainBodeFx e; e.prepare (FS);
                auto p = base(); p.type = t; p.chr = c; p.fdbk = 1.0f;
                p.shift = knobForTypeHz (t, 211.0f); p.time = 0.30f; p.blur = 0.7f;
                e.setParams (p);
                float l, r; double ph = 0.0; const double inc = 2.0 * M_PI * 110.0 / FS;
                for (int i = 0; i < (int) (12.0 * FS); ++i)
                {
                    const float s = BUS * (float) std::sin (ph); ph += inc;
                    e.processStereo (s, s, l, r);
                    const double m = std::max (std::fabs (l), std::fabs (r));
                    if (! std::isfinite (m) || m > 12.0) { bad = true; badT = t; badC = c; }
                    worst = std::max (worst, m);
                }
            }
        char d[176];
        std::snprintf (d, sizeof d, "12 s at max feedback x %d types x %d chars: worst peak %.2f%s",
                       tw::TerrainBodeFx::kNumTypes, tw::TerrainBodeFx::kNumChars, worst,
                       bad ? "  <- DIVERGED" : "");
        gate ("never diverges, never NaNs, on any Type x Character", ! bad, d);
        (void) badT; (void) badC;
    }
    {
        // The in-loop makeup must have unit slope at zero, or Character is a
        // second, secret feedback control. fb445 — the POST stage is outside the
        // loop and so cannot move loop gain, but it is an EXPANDER, so this gate
        // also holds it honest: an expander with a real small-signal gain would
        // show up here as surely as an in-loop one.
        double worst = 0.0;
        for (int c = 0; c < tw::TerrainBodeFx::kNumChars; ++c)
        {
            tw::TerrainBodeFx e; e.prepare (FS);
            auto p = base(); p.chr = c; p.fdbk = 0.0f; p.shift = 0.5f; p.mix = 1.0f;
            e.setParams (p);
            float l, r; double sum = 0.0; int n = 0;
            for (int i = 0; i < (int) (0.4 * FS); ++i)
            {
                const float s = 1e-3f * (float) std::sin (2.0 * M_PI * 300.0 * i / FS);
                e.processStereo (s, s, l, r);
                if (i > (int) (0.2 * FS)) { sum += std::fabs (l); ++n; }
            }
            const double g = (sum / std::max (1, n)) / (1e-3 * 0.6366);
            worst = std::max (worst, std::fabs (db (g)));
        }
        char d[160];
        std::snprintf (d, sizeof d, "worst small-signal gain deviation across Characters: %.2f dB", worst);
        gate ("Character has unit slope at zero (it cannot move loop gain)", worst < 1.5, d);
    }

    {
        // fb444 — THE CLIP GATE, CORRECTED. The first version of this section
        // asked "does it diverge", and mutation testing proved that question can
        // never fail: max loop gain is 0.95, Blur is allpass (unity) and Damping
        // is cut-only, so the loop is provably BIBO-stable with NO clip at all.
        // Deleting the soft clip turned zero gates red -- the gate was blind
        // because it was gating the wrong property. What the in-loop clip really
        // buys is a BOUNDED OUTPUT when the input is hot.
        // fb425 — SWEEP THE MATRIX, DO NOT SAMPLE IT. The second version pinned
        // Blur at 0.4 and mutation testing still came back 0 RED: diffusion
        // spreads the loop's energy in TIME, so at blur 0.4 an unclipped loop
        // peaks at 1.95 and slips under a 2.0 bar -- while the SAME loop at blur
        // 0 reaches 9.4. One arbitrary knob position hid a 14 dB defect.
        // fb445 — AND GUARD IS NOW AN AXIS OF THAT MATRIX, because the same
        // sweep run twice gates TWO DIFFERENT PROPERTIES and one bar cannot hold
        // both. With Guard ON the new post stage ends in a tanh, so the output is
        // bounded at 1.0 whatever the loop is doing -- which would have made this
        // gate blind to the in-loop clip all over again, exactly the fb444 trap.
        // So: Guard ON gates THE CEILING (the wet never leaves +-1), Guard OFF
        // gates THE CLIP (the only thing left holding the loop), and the second
        // one is where the mutation coverage now lives. Bars are set from what
        // the mechanism is worth, not from what the engine happens to measure:
        // deleting the clip takes the Guard-OFF cells to 90.8.
        double worst[2] = { 0.0, 0.0 }; bool bad = false;
        float wBlur[2] = { 0.0f, 0.0f }, wTime[2] = { 0.0f, 0.0f }; int wChr[2] = { 0, 0 };
        for (int c = 0; c < tw::TerrainBodeFx::kNumChars && ! bad; ++c)
        for (int gd = 0; gd < 2; ++gd)
        for (float bl : { 0.0f, 0.5f, 1.0f })
        for (float tm : { 0.20f, 0.45f, 0.70f })
        {
            tw::TerrainBodeFx e; e.prepare (FS);
            auto p = base(); p.chr = c; p.fdbk = 1.0f; p.shift = knobForHz (23.0f);
            p.time = tm; p.blur = bl; p.guard = (gd == 1);
            e.setParams (p);
            float l, r; double ph = 0.0; const double inc = 2.0 * M_PI * 90.0 / FS;
            for (int i = 0; i < (int) (5.0 * FS); ++i)
            {
                const float sIn = 0.7f * (float) std::sin (ph); ph += inc;   // HOT
                e.processStereo (sIn, sIn, l, r);
                const double m = std::max (std::fabs (l), std::fabs (r));
                if (! std::isfinite (m)) bad = true;
                if (m > worst[gd]) { worst[gd] = m; wBlur[gd] = bl; wTime[gd] = tm; wChr[gd] = c; }
            }
        }
        char d[208];
        std::snprintf (d, sizeof d,
                       "72 cells, Guard ON · worst %.2f (%.1f dBFS) at blur %.1f / time %.2f / char %d",
                       worst[1], db (worst[1]), wBlur[1], wTime[1], wChr[1]);
        gate ("the in-loop clip keeps a HOT input bounded (not just stable)",
              ! bad && worst[1] < 2.0, d);
        char d1[208];
        std::snprintf (d1, sizeof d1,
                       "worst %.3f over the same 72 cells (tanh stops at 1.000; the DC blocker after it "
                       "has a few %% of transient gain)", worst[1]);
        gate ("Guard ON is a HARD ceiling on the wet path", ! bad && worst[1] < 1.10, d1);
        char d2[208];
        std::snprintf (d2, sizeof d2,
                       "72 cells, Guard OFF · worst %.2f (%.1f dBFS) at blur %.1f / time %.2f / char %d",
                       worst[0], db (worst[0]), wBlur[0], wTime[0], wChr[0]);
        gate ("... and with Guard OFF the CLIP alone still bounds it",
              ! bad && worst[0] < 8.0, d2);
    }
    {
        // fb445 — and the same question on EVERY Type. Freeze runs a 2000x loop
        // and Spiral runs two cross-fed ones; neither existed when the gate above
        // was written, and neither is covered by a Type-0 sweep.
        double worst = 0.0; bool bad = false; int wT = 0, wC = 0;
        for (int t = 0; t < tw::TerrainBodeFx::kNumTypes; ++t)
        for (int c = 0; c < tw::TerrainBodeFx::kNumChars; ++c)
        {
            tw::TerrainBodeFx e; e.prepare (FS);
            auto p = base(); p.type = t; p.chr = c; p.fdbk = 1.0f; p.guard = false;
            p.shift = knobForTypeHz (t, 23.0f); p.time = 0.45f; p.blur = 0.5f;
            e.setParams (p);
            float l, r; double ph = 0.0; const double inc = 2.0 * M_PI * 90.0 / FS;
            for (int i = 0; i < (int) (4.0 * FS); ++i)
            {
                const float sIn = 0.7f * (float) std::sin (ph); ph += inc;
                e.processStereo (sIn, sIn, l, r);
                const double m = std::max (std::fabs (l), std::fabs (r));
                if (! std::isfinite (m)) bad = true;
                if (m > worst) { worst = m; wT = t; wC = c; }
            }
        }
        char d[208];
        std::snprintf (d, sizeof d, "64 cells (8 Types x 8 Chars), Guard OFF: worst %.2f (%.1f dBFS) on %s / char %d",
                       worst, db (worst), kTypeName[wT], wC);
        gate ("... bounded on EVERY Type with the output guard removed", ! bad && worst < 8.0, d);
    }

    // ═══════════════════════════════════════════════════════════════════════
    std::printf ("\n§E — THE MIX LAW, THE STEREO MIRROR, AND LOW KEEP.\n");
    {
        tw::TerrainBodeFx e; e.prepare (FS);
        auto p = base(); p.mix = 0.0f; p.shift = knobForHz (300.0f); e.setParams (p);
        float l, r; double worst = 0.0;
        for (int i = 0; i < (int) (0.5 * FS); ++i)
        {
            const float s = BUS * (float) std::sin (2.0 * M_PI * 440.0 * i / FS);
            e.processStereo (s, s, l, r);
            if (i > (int) (0.3 * FS)) worst = std::max (worst, (double) std::fabs (l - s));
        }
        char d[160]; std::snprintf (d, sizeof d, "worst |out - in| at Mix 0 = %.2e", worst);
        gate ("Mix 0 passes the dry through untouched", worst < 1e-6, d);
    }
    {
        tw::TerrainBodeFx e; e.prepare (FS);
        auto p = base(); p.shift = knobForHz (150.0f); p.dir = 1.0f; p.spread = 1.0f;
        e.setParams (p);
        auto sL = runTone (e, 1000.0f, BUS, 0.35, 0);
        tw::TerrainBodeFx e2; e2.prepare (FS); e2.setParams (p);
        auto sR = runTone (e2, 1000.0f, BUS, 0.35, 1);
        const double pl = peakHz (sL), pr = peakHz (sR);
        char d[176];
        std::snprintf (d, sizeof d, "L lands %.0f Hz, R lands %.0f Hz (mirror: one up, one down)", pl, pr);
        gate ("full Spread mirrors the shift: L and R go OPPOSITE ways",
              (pl > 1100.0 && pr < 900.0) || (pl < 900.0 && pr > 1100.0), d);
    }
    {
        tw::TerrainBodeFx e; e.prepare (FS);
        auto p = base(); p.spread = 0.5f; p.shift = knobForHz (150.0f); e.setParams (p);
        auto sL = runTone (e, 1000.0f, BUS, 0.35, 0);
        tw::TerrainBodeFx e2; e2.prepare (FS); e2.setParams (p);
        auto sR = runTone (e2, 1000.0f, BUS, 0.35, 1);
        char d[176];
        std::snprintf (d, sizeof d, "L %.0f Hz · R %.0f Hz (R should sit near the unshifted tone)",
                       peakHz (sL), peakHz (sR));
        gate ("Spread at centre leaves R unshifted (the axis really is continuous)",
              std::fabs (peakHz (sR) - 1000.0) < 60.0, d);
    }
    {
        tw::TerrainBodeFx e; e.prepare (FS);
        auto p = base(); p.shift = knobForHz (400.0f); p.lowKeep = 0.5f; e.setParams (p);
        auto s = runTone (e, 60.0f, BUS);
        char d[176];
        std::snprintf (d, sizeof d, "60 Hz tone with Low Keep at 200 Hz -> peak stays at %.0f Hz", peakHz (s));
        gate ("Low Keep really does bypass the shifter below its corner",
              std::fabs (peakHz (s) - 60.0) < 25.0, d);
    }

    // ═══════════════════════════════════════════════════════════════════════
    std::printf ("\n§F — EVERY KNOB KEEPS MOVING (no plateaus, no dead travel).\n");
    {
        // fb444 — THE METRIC, NOT THE BAR. The first version of this section
        // summed |output| and declared Blur and Damping dead. Both were fine:
        //   · Blur is a chain of ALLPASSES, which preserve energy by construction.
        //     A gross-energy metric is blind to them however much they smear.
        //   · Damping is a 700 Hz..40 kHz in-loop low-pass and the probe tone was
        //     330 Hz -- the whole knob sat above the only content present.
        // So: excite with BROADBAND noise (something for every knob to act on) and
        // compare MAGNITUDE SPECTRA between settings. That is what an ear does.
        auto fingerprint = [] (tw::TerrainBodeFx& e) -> std::vector<double>
        {
            float l, r; uint32_t rng = 2463534242u;
            for (int i = 0; i < (int) (0.45 * FS); ++i)          // seed to steady state
            {
                rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
                const float n = BUS * (((float) (rng & 0xFFFFu) / 32768.0f) - 1.0f);
                e.processStereo (n, n, l, r);
            }
            std::vector<std::complex<double>> buf ((size_t) NFFT);
            for (int i = 0; i < NFFT; ++i)                       // keep exciting: the
            {                                                    // in-loop parts only
                rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;   // shape a LIVE loop
                const float n = BUS * (((float) (rng & 0xFFFFu) / 32768.0f) - 1.0f);
                e.processStereo (n, n, l, r);
                const double w = 0.5 - 0.5 * std::cos (2.0 * M_PI * i / (NFFT - 1));
                buf[(size_t) i] = l * w;
            }
            fft (buf);
            std::vector<double> out ((size_t) (NFFT / 2));
            for (int i = 0; i < NFFT / 2; ++i) out[(size_t) i] = db (std::abs (buf[(size_t) i]));
            return out;
        };
        auto spectralDist = [] (const std::vector<double>& a, const std::vector<double>& b)
        {
            double acc = 0.0; int n = 0;
            const int lo = (int) (40.0 / ((double) FS / NFFT));
            const int hi = (int) (16000.0 / ((double) FS / NFFT));
            for (int i = lo; i < hi && i < (int) a.size(); ++i)
            { acc += std::fabs (a[(size_t) i] - b[(size_t) i]); ++n; }
            return acc / std::max (1, n);
        };

        struct K { const char* nm; float tw::TerrainBodeFx::Params::* fld; };
        const K ks[] = { { "Shift",     &tw::TerrainBodeFx::Params::shift },
                         { "Direction", &tw::TerrainBodeFx::Params::dir },
                         { "Fdbk",      &tw::TerrainBodeFx::Params::fdbk },
                         { "Time",      &tw::TerrainBodeFx::Params::time },
                         { "Blur",      &tw::TerrainBodeFx::Params::blur },
                         { "Damping",   &tw::TerrainBodeFx::Params::damping },
                         { "Drift",     &tw::TerrainBodeFx::Params::drift } };
        for (const auto& k : ks)
        {
            std::vector<double> prev; double minStep = 1e9;
            for (int i = 0; i <= 6; ++i)
            {
                tw::TerrainBodeFx e; e.prepare (FS);
                auto p = base();
                p.fdbk = 0.60f; p.shift = knobForHz (40.0f); p.time = 0.45f;
                p.*(k.fld) = (float) i / 6.0f;
                e.setParams (p);
                auto f = fingerprint (e);
                if (! prev.empty()) minStep = std::min (minStep, spectralDist (prev, f));
                prev = f;
            }
            char d[176];
            std::snprintf (d, sizeof d, "%-9s smallest spectral step across 6 = %.3f dB", k.nm, minStep);
            gate ((std::string (k.nm) + " transforms across its whole travel").c_str(),
                  minStep > 0.25, d);
        }
    }

    // ═══════════════════════════════════════════════════════════════════════
    // §T — THE TYPES ARE DIFFERENT.
    //
    // fb444 shipped eight Type NAMES over ONE topology. `Params::type` was
    // written by the processor, stored by the engine, and read by nothing. All
    // 28 gates above were green the whole time, because every one of them asks
    // "does the engine work" and none of them asks "does the CONTROL reach a
    // different engine". That is fb373 one level up, and it is what this section
    // exists to make impossible: the matrix below has a cell for every pair, and
    // a fb444 engine prints 0.000 in all 28 of them.
    // ═══════════════════════════════════════════════════════════════════════
    std::printf ("\n§T — THE TYPES ARE DIFFERENT. fb444 read `type` nowhere; every gate above stayed green.\n");

    {
        // T0 — THE MAPPING IS THE TYPE'S, read off the engine's OWN meter (fb432).
        // Not "the spectrum changed": the actual Hz the shifter was told to use.
        double got1[8], got75[8]; bool ok = true;
        std::printf ("      Type          Shift@100%%     Shift@75%%      Delay@0%%   Delay@100%%\n");
        for (int t = 0; t < 8; ++t)
        {
            const float wantHi = (t == 4) ? 5000.0f : kSpanT[t];
            const float want75 = (t == 4) ? (0.1f * std::pow (50000.0f, 0.75f))
                                          : (std::pow (kSpanT[t] + 1.0f, 0.5f) - 1.0f);
            float dLo = 0.0f, dHi = 0.0f;
            for (int pass = 0; pass < 4; ++pass)
            {
                tw::TerrainBodeFx e; e.prepare (FS);
                auto p = base(); p.type = t;
                p.shift = (pass == 0) ? 1.0f : (pass == 1 ? 0.75f : 0.5f);
                p.time  = (pass == 2) ? 0.0f : (pass == 3 ? 1.0f : 0.45f);
                e.setParams (p);
                float l, r; for (int i = 0; i < (int) (0.30 * FS); ++i) e.processStereo (0.0f, 0.0f, l, r);
                if (pass == 0) got1[t]  = e.meterShiftHz();
                if (pass == 1) got75[t] = e.meterShiftHz();
                if (pass == 2) dLo = e.meterDelayMs();
                if (pass == 3) dHi = e.meterDelayMs();
            }
            const bool cell = std::fabs (got1[t]  - wantHi) < 0.02 * wantHi
                           && std::fabs (got75[t] - want75) < 0.02 * std::max (1.0f, want75)
                           && std::fabs (dLo - kTLo[t]) < 0.06f * std::max (0.05f, kTLo[t])
                           && std::fabs (dHi - kTHi[t]) < 0.06f * kTHi[t];
            ok = ok && cell;
            std::printf ("      %-11s %9.1f Hz %9.2f Hz %11.2f ms %9.1f ms  %s\n",
                         kTypeName[t], got1[t], got75[t], dLo, dHi, cell ? "" : "  <- WRONG");
        }
        int distinct = 0;
        for (int a = 0; a < 8; ++a)
        {
            bool uniq = true;
            for (int b = 0; b < a; ++b)
                if (std::fabs (got75[a] - got75[b]) < 0.02 * std::max (1.0, std::fabs (got75[b]))) uniq = false;
            if (uniq) ++distinct;
        }
        char d[192];
        std::snprintf (d, sizeof d, "8 Types, 8 different mappings · %d distinct Hz at the SAME 75%% knob", distinct);
        gate ("each Type has its OWN shift + delay mapping (engine meters)", ok && distinct == 8, d);
    }

    {
        // T1 — THE 28-PAIR MATRIX. Same knobs, two probes, every pair.
        std::vector<double> fN[8], fT[8], fW[8];
        for (int t = 0; t < 8; ++t)
        {
            const auto p = tBase (t);
            const double sd = seedFor (t, p.time, 2.0);
            { tw::TerrainBodeFx e; e.prepare (FS); e.setParams (p); fN[t] = fpSaw   (e, 110.0f, sd); }
            { tw::TerrainBodeFx e; e.prepare (FS); e.setParams (p); fT[t] = fpTone  (e, 220.0f, sd); }
            { tw::TerrainBodeFx e; e.prepare (FS); e.setParams (p); fW[t] = fpNoise (e, sd); }
        }
        double worstN = 1e9, worstT = 1e9; int wa = 0, wb = 0;
        std::printf ("      mean |dB| per 1/6-octave band, 52 bands 40 Hz..16 kHz\n");
        std::printf ("      upper right = BROADBAND (110 Hz saw) · lower left = 220 Hz TONE\n");
        std::printf ("      %-11s", "");
        for (int b = 0; b < 8; ++b) std::printf ("%7.7s", kTypeName[b]);
        std::printf ("\n");
        for (int a = 0; a < 8; ++a)
        {
            std::printf ("      %-11s", kTypeName[a]);
            for (int b = 0; b < 8; ++b)
            {
                if (a == b) { std::printf ("      ·"); continue; }
                const double v = (b > a) ? specDistT (fN[a], fN[b]) : specDistT (fT[a], fT[b]);
                std::printf ("%7.2f", v);
            }
            std::printf ("\n");
        }
        for (int a = 0; a < 8; ++a)
            for (int b = a + 1; b < 8; ++b)
            {
                const double dn = specDistT (fN[a], fN[b]), dt = specDistT (fT[a], fT[b]);
                if (std::min (dn, dt) < std::min (worstN, worstT)) { wa = a; wb = b; }
                worstN = std::min (worstN, dn); worstT = std::min (worstT, dt);
            }
        char d[208];
        std::snprintf (d, sizeof d, "28 pairs · worst BROADBAND %.2f dB · worst TONE %.2f dB · closest pair %s/%s",
                       worstN, worstT, kTypeName[wa], kTypeName[wb]);
        gate ("all 28 Type pairs differ by >3 dB on BOTH probes", worstN > 3.0 && worstT > 3.0, d);

        // THE NEGATIVE CONTROL, in the §B idiom: the probe this harness REJECTED,
        // and the measurement that rejected it. White noise cannot tell two
        // shifting-loop topologies apart, because a shifting loop turns white
        // noise into white noise; that is a fact about the probe, and pretending
        // otherwise would have meant lowering a bar to fit a blind instrument.
        double worstW = 1e9; int wwa = 0, wwb = 0;
        for (int a = 0; a < 8; ++a)
            for (int b = a + 1; b < 8; ++b)
            { const double v = specDistT (fW[a], fW[b]); if (v < worstW) { worstW = v; wwa = a; wwb = b; } }
        char dw[208];
        std::snprintf (dw, sizeof dw,
                       "WHITE noise sees %s/%s only %.2f dB apart; with partials they are %.2f dB apart",
                       kTypeName[wwa], kTypeName[wwb], worstW, specDistT (fN[wwa], fN[wwb]));
        gate ("... and white noise is the WRONG probe here (negative control)",
              worstW < 3.0 && specDistT (fN[wwa], fN[wwb]) > 3.0, dw);
    }

    // ── the signature gates: does each Type's OWN mechanism engage? ──────────
    {
        // SHIFT — one rung and one only. Everything else here ladders; Type 0 at
        // zero feedback must not, or it is not an insert shifter.
        tw::TerrainBodeFx e; e.prepare (FS);
        auto p = base(); p.type = 0; p.fdbk = 0.0f;
        p.shift = knobForTypeHz (0, 200.0f); p.time = knobForTypeMs (0, 20.0f);
        e.setParams (p);
        auto s = runTone (e, 500.0f, BUS, 0.6);
        const double r1 = std::sqrt (energyNear (s, 700.0)), r2 = std::sqrt (energyNear (s, 900.0));
        const double hf = std::sqrt (energyNear (s, 800.0));
        char d[192];
        std::snprintf (d, sizeof d, "500+200: rung1 0.0 · rung2 %.1f dB · half-step %.1f dB (both must be far down)",
                       db (r2) - db (r1), db (hf) - db (r1));
        gate ("T0 Shift — ONE rung, no ladder, no half-steps",
              r2 < r1 * 0.056 && hf < r1 * 0.056, d);
    }
    {
        // BARBERPOLE — the ladder AND the half-step interleave voice that makes
        // it a barberpole rather than a staircase.
        auto probe = [] (int type) {
            tw::TerrainBodeFx e; e.prepare (FS);
            auto p = base(); p.type = type; p.fdbk = 0.85f; p.damping = 1.0f; p.blur = 0.0f;
            p.shift = knobForTypeHz (type, 200.0f); p.time = knobForTypeMs (type, 17.0f);
            e.setParams (p);
            return runTone (e, 500.0f, BUS, 1.2);
        };
        auto s = probe (1);
        const double ref = std::sqrt (energyNear (s, 300.0));
        const double a1 = std::sqrt (energyNear (s,  700.0)), a2 = std::sqrt (energyNear (s,  900.0));
        const double a3 = std::sqrt (energyNear (s, 1100.0)), hs = std::sqrt (energyNear (s, 800.0));
        char d[208];
        std::snprintf (d, sizeof d, "rungs 700/900/1100 = %.0f/%.0f/%.0f dB over floor · half-step 800 = %.0f dB",
                       db (a1) - db (ref), db (a2) - db (ref), db (a3) - db (ref), db (hs) - db (ref));
        gate ("T1 Barberpole — f+2d and f+3d ranks AND a half-step rung",
              a1 > ref * 31.6 && a2 > ref * 31.6 && a3 > ref * 31.6 && hs > ref * 31.6, d);

        auto s0 = probe (0);
        const double ref0 = std::sqrt (energyNear (s0, 300.0));
        const double hs0  = std::sqrt (energyNear (s0, 800.0));
        char d2[208];
        std::snprintf (d2, sizeof d2, "Type 0 at the SAME knobs puts %.0f dB at 800 Hz; Barberpole puts %.0f dB",
                       db (hs0) - db (ref0), db (hs) - db (ref));
        gate ("... and the half-step voice is Barberpole's alone",
              (db (hs) - db (ref)) - (db (hs0) - db (ref0)) > 20.0, d2);
    }
    {
        // ECHOBODE — the wet IS the tap, so nothing comes out until one delay
        // time has passed. That is what makes it a pedal and not an insert.
        const float wantMs = 250.0f;
        tw::TerrainBodeFx e; e.prepare (FS);
        auto p = base(); p.type = 2; p.fdbk = 0.55f; p.blur = 0.0f;
        p.shift = knobForTypeHz (2, 300.0f); p.time = knobForTypeMs (2, wantMs);
        e.setParams (p);
        auto env = burstEnv (e, 440.0f, BUS, 0.030, 0.9, 240);   // 5 ms hops
        const double hopSec = 240.0 / FS;
        size_t pk = 0; double pv = 0.0;
        for (size_t i = 0; i < env.size(); ++i) if (env[i] > pv) { pv = env[i]; pk = i; }
        double pre = 0.0;
        for (size_t i = 0; i < env.size() && (double) i * hopSec < 0.8 * wantMs * 0.001; ++i)
            pre = std::max (pre, env[i]);
        const double tPk = (double) pk * hopSec * 1000.0;
        char d[208];
        std::snprintf (d, sizeof d, "30 ms burst · first echo peaks at %.0f ms (want %.0f) · pre-echo %.0f dB down",
                       tPk, wantMs, db (pre) - db (pv));
        gate ("T2 Echobode — a DISCRETE echo at the Time delay, silence before it",
              tPk > 0.88 * wantMs && tPk < 1.20 * wantMs && pre < pv * 0.032, d);

        tw::TerrainBodeFx e0; e0.prepare (FS);
        auto p0 = p; p0.type = 0; p0.time = knobForTypeMs (0, wantMs); e0.setParams (p0);
        auto env0 = burstEnv (e0, 440.0f, BUS, 0.030, 0.9, 240);
        double pv0 = 0.0, pre0 = 0.0;
        for (size_t i = 0; i < env0.size(); ++i) pv0 = std::max (pv0, env0[i]);
        for (size_t i = 0; i < env0.size() && (double) i * hopSec < 0.8 * wantMs * 0.001; ++i)
            pre0 = std::max (pre0, env0[i]);
        char d2[208];
        std::snprintf (d2, sizeof d2, "Type 0 at the same delay is LOUD before the echo (%.0f dB down, vs %.0f)",
                       db (pre0) - db (pv0), db (pre) - db (pv));
        gate ("... while Shift answers immediately (the wet is not a tap)",
              pre0 > pv0 * 0.5, d2);
    }
    {
        // DETUNE — sub-Hz beating, reachable BY HAND, from carrier + shifted copy.
        tw::TerrainBodeFx e; e.prepare (FS);
        auto p = base(); p.type = 3; p.fdbk = 0.0f; p.shift = knobForTypeHz (3, 0.9f);
        e.setParams (p);
        auto env = rmsTrack (e, 440.0f, BUS, 1.0, 6.0, 240);
        double rate = 0.0, idx = 0.0; envRate (env, FS / 240.0, 0.30, 4.00, rate, idx);
        char d[208];
        std::snprintf (d, sizeof d, "knob %.3f -> %.2f Hz beat (want 0.90) · modulation index %.2f",
                       knobForTypeHz (3, 0.9f), rate, idx);
        gate ("T3 Detune — sub-2 Hz beating is reachable by hand",
              rate > 0.75 && rate < 1.10 && idx > 0.35, d);

        tw::TerrainBodeFx e0; e0.prepare (FS);
        auto p0 = p; p0.type = 0; e0.setParams (p0);
        auto env0 = rmsTrack (e0, 440.0f, BUS, 1.0, 6.0, 240);
        double r0 = 0.0, i0 = 0.0; envRate (env0, FS / 240.0, 0.30, 4.00, r0, i0);
        float l, r; for (int i = 0; i < 100; ++i) e0.processStereo (0.0f, 0.0f, l, r);
        char d2[208];
        std::snprintf (d2, sizeof d2, "the SAME knob on Type 0 is %+.1f Hz and beats not at all (index %.3f)",
                       e0.meterShiftHz(), i0);
        gate ("... and the same knob on Shift neither compresses to Hz nor beats",
              i0 < 0.08 && std::fabs (e0.meterShiftHz()) > 3.0, d2);
    }
    {
        // RING — Direction is OVERRIDDEN. At dir = 1.0, where Shift throws all of
        // its energy into ONE sideband, Ring must still show both and no carrier.
        auto probe = [] (int type) {
            tw::TerrainBodeFx e; e.prepare (FS);
            auto p = base(); p.type = type; p.dir = 1.0f; p.fdbk = 0.0f; p.blur = 0.0f;
            p.shift = knobForTypeHz (type, 100.0f);
            e.setParams (p);
            return runTone (e, 1000.0f, BUS, 0.6);
        };
        auto s = probe (4);
        const double up = std::sqrt (energyNear (s, 1100.0)), dn = std::sqrt (energyNear (s, 900.0));
        const double car = std::sqrt (energyNear (s, 1000.0));
        const double tilt = std::fabs (db (up) - db (dn));
        char d[208];
        std::snprintf (d, sizeof d, "dir=1.0: sidebands %.1f dB apart, carrier %.1f dB under the weaker",
                       tilt, db (car) - db (std::min (up, dn)));
        gate ("T4 Ring — BOTH sidebands + suppressed carrier at Direction = 1.0",
              tilt < 15.0 && car < std::min (up, dn) * 0.1, d);

        auto s0 = probe (0);
        const double u0 = std::sqrt (energyNear (s0, 1100.0)), d0 = std::sqrt (energyNear (s0, 900.0));
        char d2[208];
        std::snprintf (d2, sizeof d2, "Type 0 at dir=1.0 tilts %.0f dB (one sideband); Ring tilts %.1f dB",
                       std::fabs (db (u0) - db (d0)), tilt);
        gate ("... which is exactly what Direction does NOT do on Shift",
              std::fabs (db (u0) - db (d0)) > 30.0, d2);
    }
    {
        // SPIRAL — energy ABOVE and BELOW the input at the same time. A ladder
        // that only climbs is a Barberpole; this one climbs and falls at once.
        auto run = [] (int type, float fb) {
            tw::TerrainBodeFx e; e.prepare (FS);
            auto p = base(); p.type = type; p.fdbk = fb; p.blur = 0.0f; p.dir = 1.0f;
            p.shift = knobForTypeHz (type, 200.0f); p.time = knobForTypeMs (type, 60.0f);
            e.setParams (p);
            return runTone (e, 1000.0f, BUS, 1.5);
        };
        auto s = run (5, 0.60f);
        const double above = std::sqrt (energyBand (s, 1010.0, 4000.0));
        const double below = std::sqrt (energyBand (s,  200.0,  990.0));
        const double up1   = std::sqrt (energyNear (s, 1200.0));
        const double dn1   = std::sqrt (energyNear (s,  876.4));
        char d[208];
        std::snprintf (d, sizeof d, "below/above = %+.1f dB · +d rung %.1f dB · -0.618d rung %.1f dB (rel. above)",
                       db (below) - db (above), db (up1) - db (above), db (dn1) - db (above));
        gate ("T5 Spiral — energy on BOTH sides of the note at once",
              db (below) - db (above) > -20.0 && dn1 > below * 0.2, d);

        auto s0 = run (0, 0.60f);
        const double a0 = std::sqrt (energyBand (s0, 1010.0, 4000.0));
        const double b0 = std::sqrt (energyBand (s0,  200.0,  990.0));
        char d2[208];
        std::snprintf (d2, sizeof d2, "Type 0 at dir=up puts %+.1f dB below the note; Spiral puts %+.1f dB",
                       db (b0) - db (a0), db (below) - db (above));
        gate ("... where an up-shifting ladder puts nothing below it",
              (db (below) - db (above)) - (db (b0) - db (a0)) > 20.0, d2);
    }
    {
        // CHORALE — three voices at related offsets from ONE tone.
        tw::TerrainBodeFx e; e.prepare (FS);
        auto p = base(); p.type = 6; p.fdbk = 0.0f; p.blur = 0.0f;
        p.shift = knobForTypeHz (6, 300.0f);
        e.setParams (p);
        auto s = runTone (e, 1000.0f, BUS, 0.6, 0);
        const double ref = std::sqrt (energyNear (s, 2500.0));
        const double v0 = std::sqrt (energyNear (s, 1300.0));   // +d
        const double v1 = std::sqrt (energyNear (s,  850.0));   // -d/2
        const double v2 = std::sqrt (energyNear (s, 1485.0));   // +1.618 d
        const double lo = std::min (v0, std::min (v1, v2)), hi = std::max (v0, std::max (v1, v2));
        char d[208];
        std::snprintf (d, sizeof d, "1 kHz in -> 1300 %.0f dB · 850 %.0f dB · 1485 %.0f dB over floor (spread %.1f dB)",
                       db (v0) - db (ref), db (v1) - db (ref), db (v2) - db (ref), db (hi) - db (lo));
        gate ("T6 Chorale — THREE inharmonic partials from one tone",
              lo > ref * 31.6 && (db (hi) - db (lo)) < 20.0, d);
    }
    {
        // FREEZE — the hold, and the free-run law, printed side by side so the
        // trade is on the record. See the header: this is a GRAB, not eternity.
        auto run = [] (int type, double& noteRms, double& holdRms, double& tailPk) {
            tw::TerrainBodeFx e; e.prepare (FS);
            auto p = base(); p.type = type; p.fdbk = 1.0f; p.shift = 0.5f;
            p.time = knobForTypeMs (type, 200.0f); p.damping = 1.0f;
            e.setParams (p);
            float l, r; double ph = 0.0; const double inc = 2.0 * M_PI * 220.0 / FS;
            double acc = 0.0; int n = 0;
            for (int i = 0; i < (int) (0.5 * FS); ++i)
            {
                const float s = BUS * (float) std::sin (ph); ph += inc;
                e.processStereo (s, s, l, r);
                if (i > (int) (0.4 * FS)) { acc += (double) l * l; ++n; }
            }
            noteRms = std::sqrt (acc / std::max (1, n));
            double hacc = 0.0; int hn = 0; tailPk = 0.0;
            for (int i = 0; i < (int) (1.0 * FS); ++i)
            {
                e.processStereo (0.0f, 0.0f, l, r);
                const double t = (double) i / FS;
                if (t >= 0.28 && t <= 0.34) { hacc += (double) l * l; ++hn; }
                if (t >  0.60) tailPk = std::max (tailPk, (double) std::max (std::fabs (l), std::fabs (r)));
            }
            holdRms = std::sqrt (hacc / std::max (1, hn));
        };
        double n7, h7, t7, n0, h0, t0;
        run (7, n7, h7, t7); run (0, n0, h0, t0);
        char d[224];
        std::snprintf (d, sizeof d,
                       "Freeze holds %+.1f dB of the note at +0.30 s, and is %.0f dBFS at +0.60 s (law: -70)",
                       db (h7) - db (n7), db (t7));
        gate ("T7 Freeze — a real HOLD that still obeys the free-run law",
              (db (h7) - db (n7)) > -12.0 && t7 < 3.16e-4, d);
        char d2[224];
        std::snprintf (d2, sizeof d2, "Shift at the same settings has already fallen %+.1f dB by +0.30 s",
                       db (h0) - db (n0));
        gate ("... where every other Type has collapsed by then",
              (db (h7) - db (n7)) - (db (h0) - db (n0)) > 30.0, d2);
    }

    {
        // FREEZE CAPTURES INSTEAD OF PILING UP. Mutation testing found the input
        // attenuation uncovered, and then killed the FIRST gate written for it
        // too: "how far under the held note does fresh input land" reads -7.9 dB
        // with the mechanism and -7.8 dB without, because held and fresh scale
        // TOGETHER -- a ratio cancels exactly the thing being measured. What the
        // attenuation actually does is stop the loop running away: without it the
        // same settings sit 16 dB hotter, pinned against the in-loop clip. So the
        // gate is the LEVEL, and its shape is the giveaway: on a capture, driving
        // the loop HARDER makes it QUIETER, because less of the input gets in.
        auto lvl = [] (float fb) {
            tw::TerrainBodeFx e; e.prepare (FS);
            auto p = base(); p.type = 7; p.fdbk = fb; p.shift = 0.5f;
            p.time = knobForTypeMs (7, 200.0f); p.damping = 1.0f;
            e.setParams (p);
            float l, r; double ph = 0.0; const double inc = 2.0 * M_PI * 300.0 / FS;
            double acc = 0.0; int n = 0;
            for (int i = 0; i < (int) (2.5 * FS); ++i)
            {
                const float sg = BUS * (float) std::sin (ph); ph += inc;
                e.processStereo (sg, sg, l, r);
                if (i > (int) (2.0 * FS)) { acc += (double) l * l; ++n; }
            }
            return db (std::sqrt (acc / std::max (1, n)));
        };
        const double lo = lvl (0.40f), hi = lvl (1.00f);
        char d[208];
        std::snprintf (d, sizeof d, "steady level %.1f dB at Feedback 40%% -> %.1f dB at 100%% (%+.1f dB)",
                       lo, hi, hi - lo);
        gate ("T7b Freeze CAPTURES: closing the loop does not pile it up",
              hi - lo < -3.0, d);
    }
    {
        // FREEZE'S LOOP-GAIN FLOOR. Third find: zeroing it turned nothing red,
        // because T7 runs at Feedback = 1 where floor and ceiling agree. Freeze's
        // identity is that the loop is ALREADY nearly closed -- so the hold has to
        // survive at the BOTTOM of the Feedback knob too.
        tw::TerrainBodeFx e; e.prepare (FS);
        auto p = base(); p.type = 7; p.fdbk = 0.0f; p.shift = 0.5f;
        p.time = knobForTypeMs (7, 200.0f); p.damping = 1.0f;
        e.setParams (p);
        float l, r; double ph = 0.0; const double inc = 2.0 * M_PI * 220.0 / FS;
        double acc = 0.0; int n = 0;
        for (int i = 0; i < (int) (0.5 * FS); ++i)
        {
            const float sg = BUS * (float) std::sin (ph); ph += inc;
            e.processStereo (sg, sg, l, r);
            if (i > (int) (0.4 * FS)) { acc += (double) l * l; ++n; }
        }
        const double noteRms = std::sqrt (acc / std::max (1, n));
        double hacc = 0.0; int hn = 0;
        for (int i = 0; i < (int) (0.45 * FS); ++i)
        {
            e.processStereo (0.0f, 0.0f, l, r);
            const double t = (double) i / FS;
            if (t >= 0.28 && t <= 0.34) { hacc += (double) l * l; ++hn; }
        }
        const double holdRms = std::sqrt (hacc / std::max (1, hn));
        char d[208];
        std::snprintf (d, sizeof d, "at Feedback = 0 %% the loop is still %+.1f dB of the note at +0.30 s",
                       db (holdRms) - db (noteRms));
        gate ("T7c Freeze holds at the BOTTOM of the Feedback knob too",
              db (holdRms) - db (noteRms) > -18.0, d);
    }

    // ═══════════════════════════════════════════════════════════════════════
    std::printf ("\n§U — THE CONTROLS fb444 DECLARED AND NEVER READ.\n");
    {
        // GUARD. `Params::guard` was stored and used nowhere: a pill on the card
        // that did nothing at all. It is now the post stage's ceiling.
        auto fp = [] (bool g) {
            tw::TerrainBodeFx e; e.prepare (FS);
            auto p = base(); p.chr = 3; p.fdbk = 0.75f; p.guard = g;
            p.shift = knobForHz (120.0f); p.time = 0.40f;
            e.setParams (p);
            float l, r; uint32_t rng = 99u; double pk = 0.0;
            for (int i = 0; i < (int) (2.0 * FS); ++i)
            {
                rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
                const float n = 0.6f * (((float) (rng & 0xFFFFu) / 32768.0f) - 1.0f);
                e.processStereo (n, n, l, r);
                if (i > (int) (1.0 * FS)) pk = std::max (pk, (double) std::fabs (l));
            }
            return pk;
        };
        // fb445 — THE METRIC, NOT THE BAR. The first form of this gate compared
        // BROADBAND SPECTRA and read 0.51 dB, because a tanh ceiling only touches
        // the peaks: the average spectrum barely notices. The property Guard owns
        // is the PEAK, so that is what is measured.
        const double on = fp (true), off = fp (false);
        char d[208];
        std::snprintf (d, sizeof d, "hot peak %.2f with Guard ON vs %.2f with Guard OFF = %+.1f dB",
                       on, off, db (off) - db (on));
        gate ("Guard is a real control (fb444 read the field nowhere)",
              db (off) - db (on) > 4.0 && on < 1.02, d);
    }
    {
        // ROUTE 4 vs 5. In fb444 these two branches wrote the same value and
        // returned the same value; the only difference between them was the
        // ORDER of two independent statements. They were bit-identical, and this
        // gate prints 0.00 on that engine.
        // The probe needs PARTIALS: on white noise, "shifted once" and "shifted on
        // every pass" are both just noise, and the three routes measured 1.7 dB
        // apart. fpMix carries a 220 Hz tone whose ladder the routes move.
        auto fpR = [] (int route) {
            tw::TerrainBodeFx e; e.prepare (FS);
            auto p = base(); p.route = route; p.fdbk = 0.70f;
            p.shift = knobForHz (200.0f); p.time = knobForTypeMs (0, 20.0f);
            e.setParams (p);
            return fpMix (e, 1.0);
        };
        const auto f0 = fpR (0), f4 = fpR (4), f5 = fpR (5);
        const double d04 = specDistT (f0, f4), d05 = specDistT (f0, f5), d45 = specDistT (f4, f5);
        char d[208];
        std::snprintf (d, sizeof d, "In-loop vs First %.2f dB · vs Last %.2f dB · First vs Last %.2f dB",
                       d04, d05, d45);
        gate ("Shift First / Shift Last / In-loop are three different machines",
              d04 > 3.0 && d05 > 3.0 && d45 > 3.0, d);
    }
    {
        // ROUTE 3, "Wide". Declared in the dropdown, implemented nowhere.
        auto sideRatio = [] (int route) {
            tw::TerrainBodeFx e; e.prepare (FS);
            auto p = base(); p.route = route; p.fdbk = 0.45f; p.spread = 1.0f;
            p.shift = knobForHz (200.0f);
            e.setParams (p);
            float l, r; uint32_t rng = 7u; double sSum = 0.0, mSum = 0.0;
            for (int i = 0; i < (int) (1.5 * FS); ++i)
            {
                rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
                const float n = BUS * (((float) (rng & 0xFFFFu) / 32768.0f) - 1.0f);
                e.processStereo (n, n, l, r);
                if (i > (int) (0.5 * FS))
                { const double s = 0.5 * (l - r), m = 0.5 * (l + r); sSum += s * s; mSum += m * m; }
            }
            return db (std::sqrt (sSum)) - db (std::sqrt (mSum));
        };
        const double r0 = sideRatio (0), r3 = sideRatio (3);
        char d[208];
        std::snprintf (d, sizeof d, "side/mid = %+.1f dB on Normal, %+.1f dB on Wide (%+.1f dB wider)",
                       r0, r3, r3 - r0);
        gate ("Wide really widens (fb444 declared the option and never read it)",
              r3 - r0 > 4.0, d);
    }

    // ═══════════════════════════════════════════════════════════════════════
    std::printf ("\n§G — AND EVERY KNOB STILL MOVES ON EVERY TYPE (fb425: sweep the matrix, don't sample it).\n");
    {
        struct K2 { const char* nm; float tw::TerrainBodeFx::Params::* fld; };
        const K2 ks[] = { { "Shift", &tw::TerrainBodeFx::Params::shift },
                          { "Dir",   &tw::TerrainBodeFx::Params::dir },
                          { "Fdbk",  &tw::TerrainBodeFx::Params::fdbk },
                          { "Time",  &tw::TerrainBodeFx::Params::time },
                          { "Blur",  &tw::TerrainBodeFx::Params::blur },
                          { "Damp",  &tw::TerrainBodeFx::Params::damping },
                          { "Drift", &tw::TerrainBodeFx::Params::drift } };
        std::printf ("      smallest spectral step across 6 positions, dB\n      %-11s", "");
        for (const auto& k : ks) std::printf ("%8s", k.nm);
        std::printf ("\n");
        for (int t = 0; t < 8; ++t)
        {
            double worst = 1e9; const char* worstK = "";
            std::printf ("      %-11s", kTypeName[t]);
            for (const auto& k : ks)
            {
                std::vector<double> prev; double minStep = 1e9;
                for (int i = 0; i <= 6; ++i)
                {
                    tw::TerrainBodeFx e; e.prepare (FS);
                    auto p = base();
                    p.type = t; p.fdbk = 0.60f; p.time = 0.45f;
                    p.shift = knobForTypeHz (t, std::min (40.0f, 0.5f * kSpanT[t]));
                    p.*(k.fld) = (float) i / 6.0f;
                    e.setParams (p);
                    auto f = fpMixBins (e, seedFor (t, p.time, 0.8));
                    if (! prev.empty()) minStep = std::min (minStep, specDistBin (prev, f));
                    prev = f;
                }
                std::printf ("%8.2f", minStep);
                if (minStep < worst) { worst = minStep; worstK = k.nm; }
            }
            std::printf ("\n");
            char d[192];
            std::snprintf (d, sizeof d, "%-10s weakest knob is %s at %.2f dB", kTypeName[t], worstK, worst);
            gate ("no dead travel on this Type's seven knobs", worst > 0.25, d);
        }
    }

    std::printf ("\n══ RESULT: %d pass, %d FAIL ══\n\n", gPass, gFail);
    return gFail == 0 ? 0 : 1;
}
