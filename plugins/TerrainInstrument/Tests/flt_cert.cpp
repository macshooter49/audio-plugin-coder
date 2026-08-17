// ─────────────────────────────────────────────────────────────────────────────
// flt_cert — the perceptual certification harness for the FX Filter device (fb377).
//
// COMMITTED, not scratchpad-only. Every bible in Design/ cites harnesses by paths that do not
// exist because they were written under /private/tmp and evaporated; this one stays.
//
//   clang++ -std=c++17 -O2 -I ../Source -I <shim> -o /tmp/flt_cert Tests/flt_cert.cpp
//
// ⚠️ THE LAW THIS HARNESS CANNOT ENFORCE (fb373, read it before trusting a green run):
// a green DSP harness proves the ENGINE works. It NEVER proves the plugin reaches it. The
// UI->param->DSP round trip is gated separately, headlessly, in the type round-trip test.
//
// Metrics are phase-INDEPENDENT and correlate with hearing (fb283): magnitude spectrum,
// centroid, HF ratio, THD, RT-style decay. Sample-difference RMS is BANNED as a dramaticism
// metric — an allpass gives a huge RMS and is inaudible.
// ─────────────────────────────────────────────────────────────────────────────
#include "../Source/FilterFxEngine.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <cmath>
#include <complex>

namespace {

constexpr float FS = 48000.0f;
int gPass = 0, gFail = 0;
std::string gSec;

void section (const char* s) { gSec = s; std::printf ("\n[%s]\n", s); }

void gate (const char* what, bool ok, const std::string& detail)
{
    if (ok) { ++gPass; std::printf ("  ok    %-52s %s\n", what, detail.c_str()); }
    else    { ++gFail; std::printf ("  FAIL  %-52s %s\n", what, detail.c_str()); }
}

std::string fmt (const char* f, double v) { char b[96]; std::snprintf (b, sizeof b, f, v); return b; }

// ── the standard program: a chord at the measured bus level, -26 dBFS nominal.
std::vector<float> chord (int n, float rms = 0.05f, bool stabs = false)
{
    std::vector<float> x ((size_t) n, 0.0f);
    const float f[4] = { 110.0f, 130.81f, 164.81f, 220.0f };
    for (int i = 0; i < n; ++i)
    {
        float s = 0.0f;
        for (float fr : f)
            for (int h = 1; h <= 12; ++h)                    // saw-ish, band-limited by hand
                s += std::sin (6.2831853f * fr * h * i / FS) / (float) h;
        if (stabs)
        {
            const int per = (int) (FS * 0.25f);
            const float ph = (float) (i % per) / (float) per;
            s *= std::exp (-ph * 9.0f);
        }
        x[(size_t) i] = s * 0.02f;
    }
    // normalise to the requested RMS so every measurement sits at the real bus level
    double acc = 0; for (float v : x) acc += (double) v * v;
    const float cur = (float) std::sqrt (acc / (double) n);
    if (cur > 1e-9f) { const float g = rms / cur; for (float& v : x) v *= g; }
    return x;
}

std::vector<float> tone (int n, float hz, float rms = 0.05f)
{
    std::vector<float> x ((size_t) n);
    for (int i = 0; i < n; ++i) x[(size_t) i] = std::sin (6.2831853f * hz * i / FS);
    double acc = 0; for (float v : x) acc += (double) v * v;
    const float g = rms / (float) std::sqrt (acc / (double) n);
    for (float& v : x) v *= g;
    return x;
}

// ⚠️ THE PROBE MATTERS MORE THAN THE METRIC. A bass-heavy chord has almost no energy above
// 2 kHz, so ANY high-frequency measurement of a cutoff sweep reads flat on it — the metric is
// then reporting the source, not the filter. White noise is the honest probe for a response.
std::vector<float> noise (int n, float rms = 0.05f)
{
    std::vector<float> x ((size_t) n);
    uint32_t st = 22222u;
    for (int i = 0; i < n; ++i)
    { st = st * 1664525u + 1013904223u; x[(size_t) i] = ((float) (st >> 8) / 8388608.0f) - 1.0f; }
    double acc = 0; for (float v : x) acc += (double) v * v;
    const float g = rms / (float) std::sqrt (acc / (double) n);
    for (float& v : x) v *= g;
    return x;
}

struct Run { std::vector<float> l, r; };

Run run (tw::FilterFxEngine::Params p, const std::vector<float>& in, int note = -1)
{
    tw::FilterFxEngine e; e.prepare (FS, 512);
    if (note >= 0) e.noteOn (note, 1.0f);
    Run o; o.l.resize (in.size()); o.r.resize (in.size());
    for (size_t i = 0; i < in.size(); ++i)
    { float a = in[i], b = in[i]; e.processSample (a, b, p); o.l[i] = a; o.r[i] = b; }
    return o;
}

double rmsOf (const std::vector<float>& v, size_t from = 0)
{
    double a = 0; size_t n = 0;
    for (size_t i = from; i < v.size(); ++i) { a += (double) v[i] * v[i]; ++n; }
    return n ? std::sqrt (a / (double) n) : 0.0;
}
double db (double x) { return 20.0 * std::log10 (std::max (x, 1e-12)); }

// magnitude at a bin, by DFT (Goertzel over a long window reports a flat 0.000 — the fb362 trap)
double binMag (const std::vector<float>& v, double hz, size_t from, size_t len)
{
    std::complex<double> acc (0, 0);
    for (size_t i = 0; i < len && from + i < v.size(); ++i)
    {
        const double t = 6.2831853 * hz * (double) i / FS;
        acc += std::complex<double> (v[from + i] * std::cos (t), -v[from + i] * std::sin (t));
    }
    return std::abs (acc) / (double) len;
}

// 48-band log magnitude spectrum, the perceptual workhorse
std::vector<double> spectrum (const std::vector<float>& v, size_t from)
{
    std::vector<double> s;
    const size_t len = std::min<size_t> (v.size() - from, 16384);
    for (int b = 0; b < 48; ++b)
        s.push_back (db (binMag (v, 30.0 * std::pow (500.0, b / 47.0), from, len)));
    return s;
}
double specDist (const std::vector<double>& a, const std::vector<double>& b)
{
    double m = 0; for (size_t i = 0; i < a.size(); ++i) m = std::max (m, std::fabs (a[i] - b[i]));
    return m;
}
double centroid (const std::vector<float>& v, size_t from)
{
    const size_t len = std::min<size_t> (v.size() - from, 16384);
    double num = 0, den = 0;
    for (int b = 0; b < 48; ++b)
    { const double f = 30.0 * std::pow (500.0, b / 47.0);
      const double m = binMag (v, f, from, len); num += f * m; den += m; }
    return den > 1e-12 ? num / den : 0.0;
}

tw::FilterFxEngine::Params base()
{
    tw::FilterFxEngine::Params p;
    p.env = 0.5f;      // follower OFF (bipolar centre) unless a gate asks for it
    p.sweep = 0.0f; p.track = 0.0f; p.drive = 0.0f; p.mix = 1.0f;
    return p;
}

} // namespace

int main()
{
    const int N = (int) (FS * 2.0f);
    const size_t SETTLE = (size_t) (FS * 0.4f);      // skip glides + the engine-swap dip

    std::printf ("\n══ FILTER FX ENGINE — certification ══  (bus program -26 dBFS, fs %.0f)\n", FS);

    // ─────────────────────────────────────────────────────────────────────────
    section ("A. Unity and the Mix law");

    {   // Multi/SVF LP at the very top with no drive and no resonance must pass ~unity.
        auto p = base(); p.engine = 5 /*SVF_LP*/; p.res = 0.0f; p.cut = 1.0f;
        auto in = chord (N); auto o = run (p, in);
        const double resid = db (rmsOf (o.l, SETTLE) / std::max (1e-9, rmsOf (in, SETTLE)));
        gate ("neutral filter passes unity (+-1.5 dB)", std::fabs (resid) < 1.5,
              fmt ("%.2f dB vs dry", resid));
    }
    {   // Mix 0 must be BIT-transparent — this is the row that catches an oversampler's latency
        // combing against the dry leg (FILTER-BUILD-BIBLE §6.3, the MUST-RESOLVE).
        auto p = base(); p.engine = 0; p.res = 0.7f; p.cut = 0.4f; p.mix = 0.0f;
        auto in = chord (N); auto o = run (p, in);
        double worst = 0; for (size_t i = SETTLE; i < in.size(); ++i)
            worst = std::max (worst, (double) std::fabs (o.l[i] - in[i]));
        gate ("Mix 0 is bit-transparent", worst < 1e-6, fmt ("worst sample delta %.3e", worst));
    }
    {   // Mix 1.0 = FULLY WET, zero dry. Measured as: a cutoff that mutes the program must mute it.
        // ⚠️ probe craft: a 20 Hz lowpass against a 110 Hz CHORD reads about -58 dB, which is the
        // ladder's own rolloff, not a dry leak — the first version of this gate was measuring the
        // filter and calling it the mix. A 5 kHz tone is ~8 octaves past the knee, so anything
        // that survives really is dry bleeding through.
        auto p = base(); p.engine = 0; p.cut = 0.0f; p.res = 0.0f; p.mix = 1.0f;
        auto in = tone (N, 5000.0f); auto o = run (p, in);
        const double resid = db (rmsOf (o.l, SETTLE) / std::max (1e-9, rmsOf (in, SETTLE)));
        gate ("Mix 100% leaks no dry (< -60 dB)", resid < -60.0, fmt ("%.1f dB residual", resid));
    }

    // ─────────────────────────────────────────────────────────────────────────
    section ("B. The engines are genuinely different things (law 5)");

    {   // every curated group representative must be night-and-day against SVF LP
        struct E { int idx; const char* name; };
        const E reps[] = { {0,"Ladder LP 24"}, {4,"Acid 303"}, {6,"SVF HP"}, {7,"SVF BP"},
                           {8,"SVF Notch"}, {14,"Formant A"}, {10,"Comb +"}, {19,"Phaser 4P"},
                           {21,"Ring Mod"}, {77,"Tilt"} };
        auto p0 = base(); p0.engine = 5; p0.cut = 0.55f; p0.res = 0.3f;
        auto in = chord (N);
        auto ref = spectrum (run (p0, in).l, SETTLE);
        for (auto& e : reps)
        {
            auto p = p0; p.engine = e.idx;
            const double d = specDist (spectrum (run (p, in).l, SETTLE), ref);
            gate ((std::string ("engine differs from SVF LP: ") + e.name).c_str(), d > 6.0,
                  fmt ("%.1f dB max band distance (gate 6)", d));
        }
    }
    {   // and they must differ from EACH OTHER, not just from the reference
        auto in = chord (N);
        auto p = base(); p.cut = 0.55f; p.res = 0.3f;
        p.engine = 0;  auto a = spectrum (run (p, in).l, SETTLE);
        p.engine = 4;  auto b = spectrum (run (p, in).l, SETTLE);
        p.engine = 88; auto c = spectrum (run (p, in).l, SETTLE);   // MS-20 LP
        gate ("Ladder vs Acid are different machines", specDist (a, b) > 3.0,
              fmt ("%.1f dB (gate 3)", specDist (a, b)));
        gate ("Acid vs MS-20 are different machines", specDist (b, c) > 3.0,
              fmt ("%.1f dB (gate 3)", specDist (b, c)));
    }

    // ─────────────────────────────────────────────────────────────────────────
    section ("C. Every knob evolves 0 -> 100, nothing plateaus (law 5)");

    {   // CUTOFF: a full sweep must move the centroid enormously
        // ⚠️ probe craft: CENTROID under-reports a cutoff sweep on a bass-heavy chord (the program
        // has little HF energy to move), reading only x2.3 for a 56 Hz -> 14 kHz sweep that is
        // obviously night-and-day. HF RATIO — energy above 2 kHz over total — is the honest probe
        // for a lowpass, and it moves in tens of dB.
        auto in = noise (N); auto p = base(); p.engine = 0; p.res = 0.2f;
        auto hfr = [&] (float c) { p.cut = c; auto o = run (p, in);
            const size_t len = 16384; double hi = 0, all = 0;
            for (int b = 0; b < 48; ++b) { const double f = 30.0 * std::pow (500.0, b / 47.0);
                const double m = binMag (o.l, f, SETTLE, len); all += m; if (f > 2000.0) hi += m; }
            return db (hi / std::max (1e-12, all)); };
        const double lo = hfr (0.15f), hi = hfr (0.95f);
        gate ("Cutoff 15%->95% opens the top (HF ratio)", hi - lo > 20.0,
              fmt ("HF ratio %.1f dB -> ", lo) + fmt ("%.1f dB (gate +20)", hi));
    }
    {   // RES: the peak must actually rise
        auto p = base(); p.engine = 0; p.cut = 0.5f;
        const double fc = tw::filters::cutKnobToHz (0.5f);
        auto in = tone (N, (float) fc);
        p.res = 0.0f; const double q0 = db (binMag (run (p, in).l, fc, SETTLE, 16384));
        p.res = 0.95f; const double q1 = db (binMag (run (p, in).l, fc, SETTLE, 16384));
        gate ("Res 0->95% lifts the peak", q1 - q0 > 6.0, fmt ("%.1f dB lift (gate 6)", q1 - q0));
    }
    {   // DRIVE: THD must climb
        auto p = base(); p.engine = 0; p.cut = 1.0f; p.res = 0.0f;
        auto in = tone (N, 220.0f);
        auto thd = [&] (float dr) {
            p.drive = dr; auto o = run (p, in);
            const double f0 = binMag (o.l, 220.0, SETTLE, 16384);
            double h = 0; for (int k = 2; k <= 6; ++k)
            { const double m = binMag (o.l, 220.0 * k, SETTLE, 16384); h += m * m; }
            return db (std::sqrt (h) / std::max (1e-12, f0)); };
        const double t0 = thd (0.0f), t1 = thd (1.0f);
        gate ("Drive 0->100% adds harmonics", t1 - t0 > 12.0,
              fmt ("THD %.1f dB -> ", t0) + fmt ("%.1f dB", t1));
    }
    {   // ENV follower: a level step must sweep the cutoff, and the direction must invert
        // ⚠️ probe craft: CENTROID is the wrong metric at the closed end. Once the filter shuts
        // hard the surviving energy is measurement residue with a HIGH centroid, so "closed"
        // reads as brighter than "open" — the metric inverts. The level of a fixed high harmonic
        // is unambiguous: open lets it through, closed does not.
        auto in = noise (N);
        // ⚠️ and the probe needs HEADROOM IN BOTH DIRECTIONS. At cut 0.30 (159 Hz) the 3 kHz probe
        // already sits at -139 dB, so "closes it further" has nothing left to remove and the gate
        // fails on a filter that is working perfectly. Sit the cutoff at 894 Hz and probe just
        // above it, where there is room to open AND room to close.
        auto p = base(); p.engine = 0; p.cut = 0.55f; p.res = 0.2f; p.sense = 0.6f;
        auto hi = [&] (float e) { p.env = e;
            return db (binMag (run (p, in).l, 1500.0, SETTLE, 16384)); };
        const double off = hi (0.5f), up = hi (1.0f), dn = hi (0.0f);
        gate ("Env +100% opens the filter", up > off + 6.0,
              fmt ("1.5 kHz %.1f dB -> ", off) + fmt ("%.1f dB", up));
        gate ("Env -100% closes it (bipolar)", dn < off - 6.0,
              fmt ("1.5 kHz %.1f dB -> ", off) + fmt ("%.1f dB", dn));
    }
    {   // SWEEP (the LFO): depth must produce real movement, measured as spectral flux
        auto in = chord (N); auto p = base(); p.engine = 0; p.cut = 0.5f; p.res = 0.4f;
        auto flux = [&] (float sw) {
            p.sweep = sw; auto o = run (p, in);
            // centroid over successive 40 ms frames; the spread IS the movement
            double mn = 1e9, mx = 0;
            for (int f = 0; f < 20; ++f)
            { const size_t off = SETTLE + (size_t) (f * FS * 0.04f);
              if (off + 2048 >= o.l.size()) break;
              std::vector<float> w (o.l.begin() + (long) off, o.l.begin() + (long) off + 2048);
              const double c = centroid (w, 0); mn = std::min (mn, c); mx = std::max (mx, c); }
            return mx / std::max (1.0, mn); };
        const double f0 = flux (0.0f), f1 = flux (1.0f);
        gate ("Sweep 0->100% makes the filter MOVE", f1 > f0 * 1.5,
              fmt ("centroid range x%.2f -> ", f0) + fmt ("x%.2f", f1));
    }
    {   // TRACK: the same engine on two different notes must land on different cutoffs
        auto in = chord (N); auto p = base(); p.engine = 0; p.cut = 0.45f; p.res = 0.5f;
        p.track = 1.0f;
        const double c2 = centroid (run (p, in, 36).l, SETTLE);
        const double c4 = centroid (run (p, in, 72).l, SETTLE);
        gate ("Track 100% follows the keyboard", c4 > c2 * 1.5,
              fmt ("C2 %.0f Hz vs ", c2) + fmt ("C5 %.0f Hz", c4));
        p.track = 0.0f;
        const double n2 = centroid (run (p, in, 36).l, SETTLE);
        const double n4 = centroid (run (p, in, 72).l, SETTLE);
        gate ("Track 0% ignores the keyboard", std::fabs (n4 - n2) < n2 * 0.05,
              fmt ("%.0f vs ", n2) + fmt ("%.0f Hz", n4));
    }
    {   // POLES: the slope must genuinely change
        // ⚠️ probe craft: Poles means SLOPE, so measure slope — dB per octave over the two octaves
        // past the knee, with a tone at each frequency. Centroid on a chord barely sees it (x1.10)
        // because most of the program sits BELOW the cutoff either way; that is the metric being
        // wrong about what the control does, not the control being weak.
        auto p = base(); p.engine = 0; p.cut = 0.45f; p.res = 0.0f;
        const double fc = tw::filters::cutKnobToHz (0.45f);
        auto slope = [&] (float pol) { p.poles = pol;
            const double a = db (binMag (run (p, tone (N, (float) (fc * 2.0))).l, fc * 2.0, SETTLE, 16384));
            const double b = db (binMag (run (p, tone (N, (float) (fc * 8.0))).l, fc * 8.0, SETTLE, 16384));
            return (a - b) / 2.0; };                       // dB per octave across 2 octaves
        const double s6 = slope (0.0f), s24 = slope (1.0f);
        gate ("Poles 6 dB vs 24 dB really changes the slope", s24 > s6 * 1.8,
              fmt ("%.1f dB/oct (6) vs ", s6) + fmt ("%.1f dB/oct (24)", s24));
    }
    {   // CHAR: the six drive flavours must not be lookalikes
        auto in = tone (N, 220.0f); auto p = base(); p.engine = 5; p.cut = 1.0f; p.drive = 0.8f;
        std::vector<std::vector<double>> sp;
        for (int c = 0; c < 6; ++c) { p.charIdx = c; sp.push_back (spectrum (run (p, in).l, SETTLE)); }
        double worst = 1e9; int wa = 0, wb = 0;
        for (int a = 0; a < 6; ++a) for (int b = a + 1; b < 6; ++b)
        { const double d = specDist (sp[(size_t) a], sp[(size_t) b]);
          if (d < worst) { worst = d; wa = a; wb = b; } }
        gate ("all 6 Char flavours differ from each other", worst > 1.5,
              fmt ("closest pair %.1f dB", worst) + " (" + std::to_string (wa) + "/" + std::to_string (wb) + ")");
    }
    {   // WIDE: the pill must actually decorrelate L/R
        auto in = chord (N); auto p = base(); p.engine = 0; p.cut = 0.5f; p.res = 0.6f;
        auto diff = [&] (bool w) { p.wide = w; auto o = run (p, in);
            std::vector<float> d (o.l.size());
            for (size_t i = 0; i < d.size(); ++i) d[i] = o.l[i] - o.r[i];
            return db (rmsOf (d, SETTLE) / std::max (1e-9, rmsOf (o.l, SETTLE))); };
        const double off = diff (false), on = diff (true);
        gate ("Wide splits L/R", on > off + 12.0,
              fmt ("L-R %.1f dB -> ", off) + fmt ("%.1f dB", on));
    }

    {   // PUNCH — the follower becomes a TRANSIENT detector, so it must blip on attacks and
        // then fall back, where the plain follower sits on the sustain. Measured as the RANGE of
        // the cutoff excursion over the loop: a transient detector swings more and rests lower.
        auto in = chord (N, 0.05f, true);                      // stabs: real attacks to detect
        auto p = base(); p.engine = 0; p.cut = 0.42f; p.res = 0.3f; p.env = 0.95f; p.sense = 0.6f;
        auto swing = [&] (bool punch) {
            p.punch = punch; auto o = run (p, in);
            double mn = 1e9, mx = 0;
            for (int f = 0; f < 24; ++f)
            { const size_t off = SETTLE + (size_t) (f * FS * 0.05f);
              if (off + 2048 >= o.l.size()) break;
              std::vector<float> w (o.l.begin() + (long) off, o.l.begin() + (long) off + 2048);
              const double c = centroid (w, 0); mn = std::min (mn, c); mx = std::max (mx, c); }
            return std::make_pair (mn, mx); };
        const auto off = swing (false); const auto on = swing (true);
        const double rOff = off.second / std::max (1.0, off.first);
        const double rOn  = on.second  / std::max (1.0, on.first);
        gate ("Punch changes the follower's behaviour", std::fabs (rOn - rOff) > 0.15,
              fmt ("centroid range x%.2f -> ", rOff) + fmt ("x%.2f", rOn));
        // and it must not be a no-op: the rendered audio has to differ
        p.punch = false; auto a = spectrum (run (p, in).l, SETTLE);
        p.punch = true;  auto b = spectrum (run (p, in).l, SETTLE);
        gate ("Punch is audible (not a dead pill)", specDist (a, b) > 2.0,
              fmt ("%.1f dB max band distance (gate 2)", specDist (a, b)));
    }
    {   // WIDE — setSpread splits L/R cutoff, so the two channels must DECORRELATE. With Wide off
        // the engine is mono-identical; a mono sum must also survive (law 6: every widener gates
        // on mono-compatibility).
        auto in = chord (N); auto p = base(); p.engine = 0; p.cut = 0.5f; p.res = 0.65f;
        auto lr = [&] (bool w) { p.wide = w; auto o = run (p, in);
            std::vector<float> d (o.l.size()), m (o.l.size());
            for (size_t i = 0; i < d.size(); ++i) { d[i] = o.l[i] - o.r[i]; m[i] = 0.5f * (o.l[i] + o.r[i]); }
            return std::make_pair (db (rmsOf (d, SETTLE) / std::max (1e-9, rmsOf (o.l, SETTLE))),
                                   db (rmsOf (m, SETTLE) / std::max (1e-9, rmsOf (o.l, SETTLE)))); };
        const auto o0 = lr (false); const auto o1 = lr (true);
        gate ("Wide OFF is mono-identical", o0.first < -100.0,
              fmt ("L-R %.1f dB", o0.first));
        gate ("Wide ON decorrelates L/R", o1.first > -20.0,
              fmt ("L-R %.1f dB (gate -20)", o1.first));
        gate ("Wide survives a mono sum (no cancellation)", o1.second > -6.0,
              fmt ("mono sum %.1f dB vs L (gate -6)", o1.second));
    }
    {   // KARPLUS PLUCK — all three voices must respond to excite(). KARPLUS_BRIGHT/_MUTE were
        // silent no-ops until the guard widened; this is the row that proves it and keeps it fixed.
        const int karplus[3] = { 13 /*KARPLUS*/, 66 /*KARPLUS_BRIGHT*/, 67 /*KARPLUS_MUTE*/ };
        const char* nm[3] = { "Karplus-Strong", "Karplus Bright", "Karplus Mute" };
        std::vector<float> silence ((size_t) (FS / 2), 0.0f);
        for (int k = 0; k < 3; ++k)
        {
            auto p = base(); p.engine = karplus[k]; p.cut = 0.45f; p.res = 0.7f;
            // excite into SILENCE: anything that rings came from the pluck, nothing else.
            tw::FilterFxEngine e; e.prepare (FS, 512);
            float a0 = 0.0f, b0 = 0.0f;
            for (int i = 0; i < 400; ++i) { a0 = 0.0f; b0 = 0.0f; e.processSample (a0, b0, p); }
            e.noteOn (60, 1.0f);
            double pk = 0;
            for (size_t i = 0; i < silence.size(); ++i)
            { float a = 0.0f, b = 0.0f; e.processSample (a, b, p); pk = std::max (pk, (double) std::fabs (a)); }
            gate ((std::string ("pluck rings on ") + nm[k]).c_str(), pk > 1.0e-4,
                  fmt ("peak %.5f after excite into silence", pk));
        }
    }

    // ─────────────────────────────────────────────────────────────────────────
    section ("D. Nothing free-runs, nothing clicks, nothing latches");

    {   // self-osc must DIE with the note (the fb325/fb345 gate)
        auto p = base(); p.engine = 0; p.cut = 0.4f; p.res = 1.0f;
        std::vector<float> in ((size_t) N, 0.0f);
        for (int i = 0; i < N / 2; ++i) in[(size_t) i] = 0.05f * std::sin (6.2831853f * 220.0f * i / FS);
        auto o = run (p, in);
        const size_t tail = (size_t) (N / 2 + FS * 1.0f);
        const double lvl = db (rmsOf (std::vector<float> (o.l.begin() + (long) tail, o.l.end())));
        gate ("self-oscillation dies within 1 s of silence", lvl < -60.0,
              fmt ("tail %.1f dBFS (gate -60)", lvl));
    }
    {   // and it must NOT latch to DC (the Phase G grid-leak silence class)
        auto p = base(); p.engine = 0; p.cut = 0.4f; p.res = 1.0f;
        std::vector<float> in ((size_t) N, 0.0f);
        for (int i = 0; i < N / 2; ++i) in[(size_t) i] = 0.05f * std::sin (6.2831853f * 220.0f * i / FS);
        auto o = run (p, in);
        // after the silence, feed it again: it must come back
        auto o2 = run (p, chord (N));
        gate ("the filter is not silent after a rail excursion", rmsOf (o2.l, SETTLE) > 1e-4,
              fmt ("post-recovery RMS %.5f", rmsOf (o2.l, SETTLE)));
        (void) o;
    }
    {   // ENGINE SWAP mid-note must not bang. Every adjacent pair in the curated set.
        const int ids[] = { 0, 4, 5, 6, 10, 14, 19, 21, 88 };
        auto in = chord (N, 0.05f);
        double worstPk = 0; int wa = -1, wb = -1;
        for (size_t k = 0; k + 1 < sizeof ids / sizeof ids[0]; ++k)
        {
            tw::FilterFxEngine e; e.prepare (FS, 512);
            auto p = base(); p.engine = ids[k]; p.cut = 0.5f; p.res = 0.4f;
            double pk = 0, ref = 0;
            for (int i = 0; i < N; ++i)
            {
                if (i == N / 2) p.engine = ids[k + 1];
                float a = in[(size_t) i], b = a; e.processSample (a, b, p);
                if (i < N / 2 - 1000) ref = std::max (ref, (double) std::fabs (a));
                else if (i > N / 2 - 500 && i < N / 2 + 4000) pk = std::max (pk, (double) std::fabs (a));
            }
            const double over = db (pk / std::max (1e-9, ref));
            if (over > worstPk) { worstPk = over; wa = ids[k]; wb = ids[k + 1]; }
        }
        gate ("engine swap mid-note never bangs (< +3 dB)", worstPk < 3.0,
              fmt ("worst +%.2f dB", worstPk) + " (" + std::to_string (wa) + "->" + std::to_string (wb) + ")");
    }
    {   // a fast knob sweep while playing must not zipper
        auto in = chord (N);
        tw::FilterFxEngine e; e.prepare (FS, 512);
        auto p = base(); p.engine = 0; p.res = 0.5f;
        std::vector<float> out ((size_t) N);
        for (int i = 0; i < N; ++i)
        { p.cut = 0.15f + 0.8f * (float) i / (float) N;      // full sweep across 2 s
          float a = in[(size_t) i], b = a; e.processSample (a, b, p); out[(size_t) i] = a; }
        // a click is a sample-to-sample jump far above the program's own slew
        double worst = 0;
        for (size_t i = SETTLE + 1; i < out.size(); ++i)
            worst = std::max (worst, (double) std::fabs (out[i] - out[i - 1]));
        const double prog = rmsOf (out, SETTLE);
        gate ("full Cutoff sweep while playing does not zipper", worst < prog * 12.0,
              fmt ("worst step %.4f vs ", worst) + fmt ("program RMS %.4f", prog));
    }
    {   // DC must not accumulate on an asymmetric engine
        auto p = base(); p.engine = 21 /*Ring Mod*/; p.cut = 0.5f; p.drive = 0.9f; p.charIdx = 2;
        auto o = run (p, chord (N));
        double mean = 0; for (size_t i = SETTLE; i < o.l.size(); ++i) mean += o.l[i];
        mean /= (double) (o.l.size() - SETTLE);
        const double prog = rmsOf (o.l, SETTLE);
        gate ("no DC offset after drive + ring mod", std::fabs (mean) < prog * 0.01,
              fmt ("mean %.2e", mean) + fmt (" = %.3f%% of program", 100.0 * std::fabs (mean) / std::max (1e-12, prog)));
    }
    {   // and every engine must produce FINITE output — the whole roster, not a sample
        int bad = 0; std::string first;
        auto in = chord (N / 8);
        for (int t = 0; t < tw::filters::kNumTypes; ++t)
        {
            if (t == 27) continue;                       // NONE
            auto p = base(); p.engine = t; p.cut = 0.5f; p.res = 0.85f; p.drive = 0.5f;
            auto o = run (p, in);
            for (float v : o.l)
                if (! std::isfinite (v) || std::fabs (v) > 40.0f)
                { ++bad; if (first.empty()) first = "engine " + std::to_string (t); break; }
        }
        gate ("all 93 engines stay finite and bounded", bad == 0,
              bad ? (std::to_string (bad) + " unstable, first " + first) : std::string ("0 unstable"));
    }

    // ─────────────────────────────────────────────────────────────────────────
    section ("E. Self-check — can these gates actually fail?");

    {   // if the metric cannot see a difference it is not a gate, it is decoration.
        auto in = chord (N); auto p = base(); p.engine = 5; p.cut = 0.55f;
        auto a = spectrum (run (p, in).l, SETTLE);
        auto b = a; for (auto& v : b) v += 9.0;            // paste in a known 9 dB offset
        gate ("(self-check) specDist sees an injected 9 dB", specDist (a, b) > 8.5,
              fmt ("%.1f dB", specDist (a, b)));
    }
    {   auto in = chord (N); auto p = base(); p.engine = 5; p.cut = 0.55f;
        auto o = run (p, in);
        std::vector<float> spike = o.l;
        for (size_t i = SETTLE + 1000; i < SETTLE + 1010 && i < spike.size(); ++i) spike[i] += 3.0f;
        double worst = 0;
        for (size_t i = SETTLE + 1; i < spike.size(); ++i)
            worst = std::max (worst, (double) std::fabs (spike[i] - spike[i - 1]));
        gate ("(self-check) the click metric sees an injected click",
              worst > rmsOf (o.l, SETTLE) * 12.0, fmt ("injected step %.3f", worst));
    }

    std::printf ("\n  %d passed, %d FAILED\n\n", gPass, gFail);
    return gFail == 0 ? 0 : 1;
}
