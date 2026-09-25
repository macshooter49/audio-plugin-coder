// organics_audit_knobs.cpp — the Organics KNOB SWEEPS (perceptual law, fb283 + the lifeguard/exposure law).
// Part of Tests/organics_audit (--knobs). Every front knob and every back-panel control, 0 → 10 → 25 → 50 → 75 → 100 %,
// on five instruments (a decaying piano, a sustaining section, a wind, two mallets), each measured with the ONE metric
// that tracks what the knob is for (phase-independent): Dynamics = K-loudness + centroid, Tone = centroid, Body = log-
// spectrum distance, Vibrato = pitch-track depth, Human = press-to-press spread, Release = T60 after note-off, Noise =
// the noise signal's level re the note, Sustain = level 3 s in, Velocity = vel 30 → 127 range, Image = side/mid.
// Then LIVE TURNS: each knob swept 0 → 100 → 0 % across a held note, block by block — the click metric must not fire
// (zipper / steps) beyond what the static note itself shows.
#include "../Source/organics/OrganicEngine.h"
#include "../Source/organics/OrganicsLibrary.h"
#include <juce_events/juce_events.h>
#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdarg>
#include <cstdio>
#include <functional>
#include <set>
#include <string>
#include <vector>

using namespace tw;
using Buf = std::vector<float>;
namespace { constexpr double kPi = 3.14159265358979323846; constexpr double SR = 48000.0; const float kNoDet[16] = {}; }
static double dbk (double v) { return 20.0 * std::log10 (std::max (v, 1e-20)); }
static std::string fmtk (const char* f, ...) { char b[1024]; va_list ap; va_start (ap, f); std::vsnprintf (b, sizeof b, f, ap); va_end (ap); return b; }
static double mtofk (double m) { return 440.0 * std::pow (2.0, (m - 69.0) / 12.0); }

// ── small DSP helpers (self-contained; same definitions as organics_audit.cpp) ──
static void fftk (std::vector<std::complex<double>>& a)
{
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i) { size_t bit = n >> 1; for (; j & bit; bit >>= 1) j ^= bit; j ^= bit; if (i < j) std::swap (a[i], a[j]); }
    for (size_t len = 2; len <= n; len <<= 1)
    {
        const double ang = -2.0 * kPi / (double) len; const std::complex<double> wl (std::cos (ang), std::sin (ang));
        for (size_t i = 0; i < n; i += len) { std::complex<double> w (1); for (size_t j = 0; j < len / 2; ++j) { auto u = a[i + j], v = a[i + j + len / 2] * w; a[i + j] = u + v; a[i + j + len / 2] = u - v; w *= wl; } }
    }
}
static std::vector<double> magk (const Buf& x, int64_t s, int n)
{
    std::vector<std::complex<double>> a ((size_t) n);
    for (int i = 0; i < n; ++i) { const double h = 0.5 - 0.5 * std::cos (2.0 * kPi * i / (n - 1)); a[(size_t) i] = s + i < (int64_t) x.size() && s + i >= 0 ? h * x[(size_t) (s + i)] : 0.0; }
    fftk (a);
    std::vector<double> m ((size_t) n / 2); for (int i = 0; i < n / 2; ++i) m[(size_t) i] = std::abs (a[(size_t) i]);
    return m;
}
static double centroidk (const Buf& x, int64_t s, int n = 16384)
{
    const auto m = magk (x, s, n); double num = 0, den = 0;
    for (size_t i = 1; i < m.size(); ++i) { const double f = (double) i * SR / n; if (f < 20 || f > 20000) continue; num += f * m[i] * m[i]; den += m[i] * m[i]; }
    return num / std::max (1e-30, den);
}
/** RMS distance of two log spectra in 1/3-octave bands 60 Hz..16 kHz, level-normalised (shape only). */
static double specDist (const Buf& a, const Buf& b, int64_t s, int n = 16384)
{
    const auto A = magk (a, s, n), B = magk (b, s, n);
    std::vector<double> ea, eb;
    for (double f = 60; f < 16000; f *= std::pow (2.0, 1.0 / 3.0))
    {
        double x = 0, y = 0; const int i0 = (int) (f * n / SR), i1 = std::max (i0 + 1, (int) (f * std::pow (2.0, 1.0 / 3.0) * n / SR));
        for (int i = i0; i < i1 && i < (int) A.size(); ++i) { x += A[(size_t) i] * A[(size_t) i]; y += B[(size_t) i] * B[(size_t) i]; }
        ea.push_back (10 * std::log10 (x + 1e-20)); eb.push_back (10 * std::log10 (y + 1e-20));
    }
    double ma = 0, mb = 0; for (size_t i = 0; i < ea.size(); ++i) { ma += ea[i]; mb += eb[i]; }
    ma /= (double) ea.size(); mb /= (double) eb.size();
    double d = 0; int c = 0;
    for (size_t i = 0; i < ea.size(); ++i) { if (ea[i] < ma - 40 && eb[i] < mb - 40) continue; const double q = (ea[i] - ma) - (eb[i] - mb); d += q * q; ++c; }
    return std::sqrt (d / std::max (1, c));
}
static double rmsk (const Buf& x, int64_t s, int64_t n) { double a = 0; int64_t c = 0; for (int64_t i = std::max<int64_t> (0, s); i < s + n && i < (int64_t) x.size(); ++i) { a += (double) x[(size_t) i] * x[(size_t) i]; ++c; } return std::sqrt (a / (double) std::max<int64_t> (1, c)); }
static Buf hpk (const Buf& x, double fc)
{
    Buf y = x;
    for (double q : { 0.5411961, 1.3065630 })
    {
        const double w0 = 2 * kPi * fc / SR, al = std::sin (w0) / (2 * q), c = std::cos (w0);
        const double b0 = (1 + c) / 2, b1 = -(1 + c), b2 = (1 + c) / 2, a0 = 1 + al, a1 = -2 * c, a2 = 1 - al;
        double x1 = 0, x2 = 0, y1 = 0, y2 = 0;
        for (auto& v : y) { const double in = v, o = (b0 * in + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2) / a0; x2 = x1; x1 = in; y2 = y1; y1 = o; v = (float) o; }
    }
    return y;
}
/** Click metric (organics_audit.cpp): worst HP(8k) excess over its ±10 ms neighbourhood in [s0, s1), + its level re local. */
static std::pair<double, double> clickk (const Buf& x, int64_t s0, int64_t s1)
{
    const auto h = hpk (x, 8000.0); const int64_t N = (int64_t) x.size(), W = 480, C = 24;
    std::vector<double> ph ((size_t) N + 1, 0.0), px ((size_t) N + 1, 0.0);
    for (int64_t i = 0; i < N; ++i) { ph[(size_t) i + 1] = ph[(size_t) i] + (double) h[(size_t) i] * h[(size_t) i]; px[(size_t) i + 1] = px[(size_t) i] + (double) x[(size_t) i] * x[(size_t) i]; }
    double bestEx = -200, bestRel = -200, bestScore = -1e9;
    for (int64_t i = std::max<int64_t> (s0, 256); i < std::min (s1, N); ++i)
    {
        const double a = std::abs ((double) h[(size_t) i]); if (a < 1e-7) continue;
        const int64_t a0 = std::max<int64_t> (0, i - W), a1 = std::min (N, i + W), c0 = std::max<int64_t> (0, i - C), c1 = std::min (N, i + C);
        const double rl = std::sqrt (std::max (0.0, (ph[(size_t) a1] - ph[(size_t) a0]) - (ph[(size_t) c1] - ph[(size_t) c0])) / (double) ((a1 - a0) - (c1 - c0))) + 1e-12;
        const double sl = std::sqrt ((px[(size_t) a1] - px[(size_t) a0]) / (double) (a1 - a0)) + 1e-12;
        if (sl < 1e-5) continue;
        const double ex = dbk (a / rl), rel = dbk (a / sl);
        const double sc = ((ex > 20 && rel > -45) ? 1000.0 : 0.0) + std::min (ex, 60.0) + 0.5 * rel;
        if (sc > bestScore) { bestScore = sc; bestEx = ex; bestRel = rel; }
    }
    return { bestEx, bestRel };
}
/** Fundamental track (cents re f) by an STFT peak ±150 ¢, 4096 window, 10 ms hop. */
static std::vector<double> pitchTrack (const Buf& x, int64_t s0, int64_t s1, double f)
{
    std::vector<double> out, mags; const int n = 4096, N = 16384;
    for (int64_t s = s0; s + n <= s1 && s + n <= (int64_t) x.size(); s += 480)
    {
        std::vector<std::complex<double>> a ((size_t) N);
        for (int i = 0; i < n; ++i) { const double h = 0.5 - 0.5 * std::cos (2.0 * kPi * i / (n - 1)); a[(size_t) i] = h * x[(size_t) (s + i)]; }
        fftk (a);
        const int b0 = (int) (f * std::pow (2.0, -150 / 1200.0) * N / SR), b1 = (int) (f * std::pow (2.0, 150 / 1200.0) * N / SR) + 1;
        int bk = b0; for (int b = b0; b <= b1; ++b) if (std::abs (a[(size_t) b]) > std::abs (a[(size_t) bk])) bk = b;
        const double l = std::log (std::abs (a[(size_t) bk - 1]) + 1e-30), c = std::log (std::abs (a[(size_t) bk]) + 1e-30), r = std::log (std::abs (a[(size_t) bk + 1]) + 1e-30);
        const double d = 0.5 * (l - r) / (l - 2 * c + r);
        out.push_back (1200.0 * std::log2 (((double) bk + d) * SR / N / f));
        mags.push_back (std::abs (a[(size_t) bk]));
    }
    double top = 0; for (double m : mags) top = std::max (top, m);
    std::vector<double> kept;
    for (size_t i = 0; i < out.size(); ++i) if (mags[i] >= top * 0.0316) kept.push_back (out[i]);   // within 30 dB
    return kept;
}
static double depthOf (std::vector<double> t) { if (t.size() < 8) return 0; std::sort (t.begin(), t.end()); return 0.5 * (t[(size_t) (0.95 * (t.size() - 1))] - t[(size_t) (0.05 * (t.size() - 1))]); }

static std::shared_ptr<const OrganicInstrument> loadk (const juce::String& id)
{
    std::shared_ptr<const OrganicInstrument> out; bool done = false;
    OrganicsLibrary::get().request (id, [&] (std::shared_ptr<const OrganicInstrument> p) { out = p; done = true; });
    for (int i = 0; i < 4000 && ! done; ++i) juce::MessageManager::getInstance()->runDispatchLoopUntil (5);
    return out;
}
struct Rn { Buf L, R, M; int64_t off; };
/** Render one note; hook(p, t) runs before every 256 block (live turns). */
static Rn rendk (const std::shared_ptr<const OrganicInstrument>& I, OrganicParams p, int key, int vel, double hold, double tail,
                 uint32_t seed = 1u, std::function<void (OrganicParams&, int64_t)> hook = {})
{
    I->resetPerformanceState();
    OrganicEngine e; e.prepare (SR, 256); e.setInstrument (I);
    e.noteOn (key, (float) vel / 127.f, 1, kNoDet, seed);
    Rn o; o.off = (int64_t) (hold * SR) / 256 * 256; Buf l (256), r (256);
    for (int64_t t = 0; t < (int64_t) ((hold + tail) * SR); t += 256)
    {
        if (t == o.off) e.noteOff (false);
        if (hook) hook (p, t);
        std::fill (l.begin(), l.end(), 0.f); std::fill (r.begin(), r.end(), 0.f);
        e.render (p, 0.f, l.data(), r.data(), 256);
        o.L.insert (o.L.end(), l.begin(), l.end()); o.R.insert (o.R.end(), r.begin(), r.end());
    }
    o.M.resize (o.L.size()); for (size_t i = 0; i < o.M.size(); ++i) o.M[i] = 0.5f * (o.L[i] + o.R[i]);
    return o;
}
static double t60k (const Buf& x, int64_t off)
{
    const double ref = rmsk (x, off - 4800, 4800);
    for (int64_t w = off; w + 480 <= (int64_t) x.size(); w += 480) if (rmsk (x, w, 480) < ref * 1.0e-3) return (double) (w - off) / SR;
    return -1.0;
}

int runKnobs (const juce::File&)
{
    struct Inst { const char* id; int key; bool sustaining; bool trackable; };   // trackable: an STFT pitch track can follow it
    const Inst insts[] = { { "salamander.grand.v3", 60, false, true }, { "vsco2.strings.violin-section", 69, true, true },
                           { "vsco2.woodwinds.flute", 72, true, true }, { "vcsl.mallets.vibraphone", 65, false, true },
                           { "vcsl.mallets.glockenspiel", 84, false, false } };
    const double steps[] = { 0.0, 0.10, 0.25, 0.50, 0.75, 1.00 };
    int fails = 0;
    std::printf ("══ ORGANICS KNOB SWEEPS — 0 / 10 / 25 / 50 / 75 / 100 %% (APVTS value) ══\n");
    for (const auto& in : insts)
    {
        auto I = loadk (in.id);
        if (! I) { std::printf ("FAIL  %s missing\n", in.id); ++fails; continue; }
        const int K = in.key;
        OrganicParams base; base.human = 0.f;
        std::printf ("── %s (key %d) ──\n", in.id, K);
        // kind: 0 = SPAN (|v100 − v0| ≥ bar) · 1 = DISTANCE from the 50 % reference (max(v0, v100) ≥ bar) · 2 = RATIO
        // (v100 / v0 ≥ bar, or its inverse) · 3 = INFO. `none` names why the knob has nothing to act on (reported, not failed).
        auto row = [&] (const char* name, std::function<double (double)> f, const char* unit, double bar, int kind = 0, const char* none = nullptr) {
            std::string s; double v0 = 0, v50 = 0, v100 = 0;
            for (double st : steps) { const double v = f (st); s += fmtk (" %9.2f", v); if (st == 0.0) v0 = v; if (st == 0.5) v50 = v; if (st == 1.0) v100 = v; }
            double got = std::abs (v100 - v0);
            if (kind == 1) got = std::max (v0, v100);
            if (kind == 2) got = (v0 > 0 && v100 > 0) ? std::max (v100 / v0, v0 / v100) : 0.0;
            const bool info = kind == 3 || none != nullptr;
            const bool ok = info || got >= bar;
            if (! ok) ++fails;
            std::printf ("%s  %-10s%s  %s   (%s %.2f; bar %.2f; 50→100 %% %.2f)%s%s\n", info ? "INFO" : (ok ? "PASS" : "FAIL"), name, s.c_str(), unit,
                         kind == 1 ? "max distance" : kind == 2 ? "ratio" : "0→100 %", got, bar, std::abs (v100 - v50), none ? " — " : "", none ? none : "");
        };
        std::set<std::pair<int, int>> layersAtK;
        for (auto& r : I->regions) if (r.artic == 0 && r.kind == org::Kind::Attack && r.lk <= K && K <= r.hk) layersAtK.insert ({ r.lv, r.hv });
        const char* oneLayer = layersAtK.size() <= 1 ? "one velocity layer at this key: nothing to cross into" : nullptr;
        const int64_t S = (int64_t) (0.25 * SR);
        // Dynamics: bipolar (2v−1): the layer shift — loudness AND brightness move
        const int64_t S0 = (int64_t) (0.02 * SR);   // brightness right after the attack (a struck bar's overtones are gone by 250 ms)
        row ("Dynamics", [&] (double v) { auto p = base; p.dyn = (float) (2 * v - 1); auto r = rendk (I, p, K, 80, 1.0, 0.0); return centroidk (r.M, S0, 8192); }, "Hz centroid", 1.15, 2, oneLayer);
        row ("Dyn level", [&] (double v) { auto p = base; p.dyn = (float) (2 * v - 1); auto r = rendk (I, p, K, 80, 1.0, 0.0); return dbk (rmsk (r.M, S, (int64_t) (0.5 * SR))); }, "dB RMS (a layer shift reads as timbre by design)", 0.0, 3);
        row ("Tone", [&] (double v) { auto p = base; p.tone = (float) (2 * v - 1); auto r = rendk (I, p, K, 80, 1.0, 0.0); return centroidk (r.M, S0, 8192); }, "Hz centroid", 1.3, 2);
        {
            auto ref = rendk (I, base, K, 80, 1.0, 0.0);
            row ("Body", [&] (double v) { auto p = base; p.body = (float) (2 * v - 1); auto r = rendk (I, p, K, 80, 1.0, 0.0); return specDist (r.M, ref.M, S); }, "dB spec-dist re 50 %", 3.0, 1);
        }
        row ("Vibrato", [&] (double v) { auto p = base; p.vibrato = (float) v; p.vibDelay = 0.f; auto r = rendk (I, p, K, 80, 2.0, 0.0);
                                          return depthOf (pitchTrack (r.M, (int64_t) (0.5 * SR), (int64_t) (1.9 * SR), mtofk (K))); }, "cents depth", 20.0, 0,
            in.trackable ? nullptr : "a C6 glockenspiel's bar partials defeat the STFT pitch track (the vibraphone row measures the knob)");
        row ("Human", [&] (double v) {
            auto p = base; p.human = (float) v; std::vector<double> lv, pc;
            for (uint32_t sd = 1; sd <= 8; ++sd) { auto r = rendk (I, p, K, 80, 0.6, 0.0, sd * 7919u); lv.push_back (dbk (rmsk (r.M, 0, (int64_t) (0.5 * SR))));
                                                    auto tr = pitchTrack (r.M, (int64_t) (0.1 * SR), (int64_t) (0.55 * SR), mtofk (K)); double m = 0; for (double c : tr) m += c; pc.push_back (tr.empty() ? 0 : m / (double) tr.size()); }
            auto spread = [] (std::vector<double> q) { std::sort (q.begin(), q.end()); return q.back() - q.front(); };
            return spread (lv) + spread (pc) * 0.25;          // dB + cents/4 — one "how different is each press" number
        }, "dB+c/4 spread", 1.5);
        row ("Release", [&] (double v) { auto p = base; p.release = (float) v; auto r = rendk (I, p, K, 80, 1.0, 34.0); const double t = t60k (r.M, r.off); return t < 0 ? 34.0 : t; }, "s T60 (34 = longer than the render)", 8.0);
        row ("Noise", [&] (double v) {
            auto p0 = base; p0.noise = 0.f; auto p = base; p.noise = (float) v;
            auto a = rendk (I, p, K, 80, 1.0, 1.0), b = rendk (I, p0, K, 80, 1.0, 1.0);
            Buf d (a.M.size()); for (size_t i = 0; i < d.size(); ++i) d[i] = a.M[i] - b.M[i];
            double pk = 0; for (float x : d) pk = std::max (pk, (double) std::abs (x));
            double pn = 0; for (float x : b.M) pn = std::max (pn, (double) std::abs (x));
            return std::max (-120.0, dbk (pk) - dbk (pn));
        }, "dB noise pk re note pk", 12.0, 0, I->hasNoise ? nullptr : "the instrument has no mechanical-noise regions");
        row ("Sustain", [&] (double v) { auto p = base; p.sustain = (float) v; auto r = rendk (I, p, K, 80, 4.0, 0.0);
                                          return dbk (rmsk (r.M, (int64_t) (3.5 * SR), (int64_t) (0.3 * SR))) - dbk (rmsk (r.M, (int64_t) (0.1 * SR), (int64_t) (0.3 * SR))); }, "dB at 3.5 s re onset", 6.0, 0,
            in.sustaining ? "a looping instrument already sustains (the knob acts on decaying regions)" : nullptr);
        row ("Velocity", [&] (double v) { auto p = base; p.velo = (float) v; auto a = rendk (I, p, K, 30, 0.6, 0.0), b = rendk (I, p, K, 127, 0.6, 0.0);
                                           return dbk (rmsk (b.M, 0, (int64_t) (0.5 * SR))) - dbk (rmsk (a.M, 0, (int64_t) (0.5 * SR))); }, "dB vel30→127", 10.0);
        row ("Image", [&] (double v) { auto p = base; p.image = (float) (1.5 * v); auto r = rendk (I, p, K, 80, 1.0, 0.0);
            Buf sd (r.L.size()), md (r.L.size()); for (size_t i = 0; i < sd.size(); ++i) { sd[i] = 0.5f * (r.L[i] - r.R[i]); md[i] = 0.5f * (r.L[i] + r.R[i]); }
            return dbk (rmsk (sd, S, (int64_t) (0.5 * SR))) - dbk (rmsk (md, S, (int64_t) (0.5 * SR))); }, "dB side/mid", 12.0);
        // back panel
        {
            std::string s;
            for (double v : { 0.0, 0.417, 1.0 })
            {
                auto p = base; p.vibrato = 0.6f; p.vibDelay = 0.f; p.vibRate = (float) (3.0 + 6.0 * v);
                auto r = rendk (I, p, K, 80, 2.5, 0.0);
                auto tr = pitchTrack (r.M, (int64_t) (0.5 * SR), (int64_t) (2.4 * SR), mtofk (K));
                double m = 0; for (double c : tr) m += c; m /= std::max<size_t> (1, tr.size());
                int cross = 0; for (size_t i = 1; i < tr.size(); ++i) if (tr[i - 1] < m && tr[i] >= m) ++cross;
                s += fmtk (" %.1fHz→%.1f", p.vibRate, (double) cross / (0.01 * (double) tr.size()));
            }
            std::printf ("INFO  VibRate   set→measured:%s\n", s.c_str());
            s.clear();
            for (double d : { 0.0, 0.35, 2.0 })
            {
                auto p = base; p.vibrato = 0.8f; p.vibDelay = (float) d;
                auto r = rendk (I, p, K, 80, 3.0, 0.0);
                auto tr = pitchTrack (r.M, 0, (int64_t) (2.9 * SR), mtofk (K));
                double first = -1; for (size_t i = 0; i + 20 < tr.size(); ++i) { std::vector<double> w (tr.begin() + (long) i, tr.begin() + (long) i + 20); if (depthOf (w) > 8) { first = (double) i * 0.01; break; } }
                s += fmtk (" %.2fs→%.2fs", d, first);
            }
            std::printf ("INFO  VibDelay  set→vibrato audible from:%s\n", s.c_str());
            s.clear();
            for (int c = 0; c < 3; ++c) { auto p = base; p.velCurve = c; auto r = rendk (I, p, K, 64, 0.6, 0.0); s += fmtk (" %s %.1f dB", c == 0 ? "Soft" : c == 1 ? "Linear" : "Hard", dbk (rmsk (r.M, 0, (int64_t) (0.5 * SR)))); }
            std::printf ("INFO  VelCurve  vel 64:%s\n", s.c_str());
            if (I->numArtics > 1)
            {
                auto r0 = rendk (I, base, K, 80, 1.0, 0.0); s.clear();
                for (int a = 1; a < I->numArtics; ++a) { auto p = base; p.artic = a; auto r = rendk (I, p, K, 80, 1.0, 0.0); s += fmtk (" %s %.1f dB", I->artics[a].toRawUTF8(), specDist (r.M, r0.M, S)); }
                std::printf ("INFO  Artic     spectral distance re %s:%s\n", I->artics[0].toRawUTF8(), s.c_str());
            }
        }
        // LIVE TURNS: 0 → 100 → 0 % across a held note (block by block, like automation) vs the static note
        {
            auto stat = rendk (I, base, K, 80, 4.0, 0.0);
            const auto cs = clickk (stat.M, (int64_t) (0.1 * SR), (int64_t) (4.0 * SR));
            std::string s;
            struct KT { const char* n; std::function<void (OrganicParams&, float)> set; };
            const KT kts[] = {
                { "Dyn",   [] (OrganicParams& p, float v) { p.dyn = 2 * v - 1; } },   { "Tone", [] (OrganicParams& p, float v) { p.tone = 2 * v - 1; } },
                { "Body",  [] (OrganicParams& p, float v) { p.body = 2 * v - 1; } },  { "Vib",  [] (OrganicParams& p, float v) { p.vibrato = v; p.vibDelay = 0; } },
                { "Human", [] (OrganicParams& p, float v) { p.human = v; } },         { "Rel",  [] (OrganicParams& p, float v) { p.release = v; } },
                { "Noise", [] (OrganicParams& p, float v) { p.noise = v; } },         { "Sus",  [] (OrganicParams& p, float v) { p.sustain = v; } },
                { "Velo",  [] (OrganicParams& p, float v) { p.velo = v; } },          { "Image",[] (OrganicParams& p, float v) { p.image = 1.5f * v; } },
                { "Rate",  [] (OrganicParams& p, float v) { p.vibrato = 0.5f; p.vibDelay = 0; p.vibRate = 3 + 6 * v; } },
            };
            bool bad = false;
            for (auto& kt : kts)
            {
                auto r = rendk (I, base, K, 80, 4.0, 0.0, 1u, [&] (OrganicParams& p, int64_t t) {
                    const double x = (double) t / SR; float v = 0.5f;
                    if (x >= 0.5 && x < 2.0) v = (float) ((x - 0.5) / 1.5); else if (x >= 2.0 && x < 3.5) v = (float) (1.0 - (x - 2.0) / 1.5); else v = x < 0.5 ? 0.f : 0.f;
                    kt.set (p, v); });
                const auto c = clickk (r.M, (int64_t) (0.45 * SR), (int64_t) (3.6 * SR));
                const bool hit = c.first > 20 && c.second > -45 && c.first > cs.first + 3;
                bad |= hit;
                s += fmtk (" %s %.0f/%.0f%s", kt.n, c.first, c.second, hit ? "✗" : "");
            }
            if (bad) ++fails;
            std::printf ("%s  live turns (HP excess / re local, dB; static note %.0f/%.0f):%s\n", bad ? "FAIL" : "PASS", cs.first, cs.second, s.c_str());
        }
        I.reset(); org::drainDeferredReleases();
        std::fflush (stdout);
    }
    std::printf ("══ %s — knob sweeps: %d failing rows ══\n", fails ? "FAIL" : "PASS", fails);
    return fails ? 1 : 0;
}
