// OrganicEngine_test.cpp — THE ORGANICS ENGINE runtime gates (design §8, Agent B). Measured, one PASS/FAIL
// line per bar, exit 0 only when every bar passes. Phase-independent metrics only (the fb283 perceptual law):
// Goertzel marker power, DFT-phase f0, spectral centroid, band energy, RMS, onset time, HP residual peaks.
//
//   OrganicEngine_test --gen <dir>        write the extra fixtures (test.layers/.rr/.norr/.piano) into <dir>
//   OrganicEngine_test <fixturesRoot> [<compiledRoot>]   run the gates (TERRAIN_ORGANICS_DIR → <fixturesRoot>);
//                                                        with <compiledRoot>, also the real-data bars (Agent A's library)
//
// Built by Tests/organics_engine_test.sh (juce_core + audio_basics + audio_formats + events only).
#include "OrganicEngine.h"
#include "OrganicsLibrary.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_events/juce_events.h>

#include <algorithm>
#include <atomic>
#include <cstdarg>
#include <chrono>
#include <cmath>
#include <complex>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <map>
#include <new>
#include <set>
#include <string>
#include <vector>

// ── allocation counter (the Terrain pattern: count operator new while armed) ─────────────────────
static std::atomic<bool>    gCount { false };
static std::atomic<int64_t> gAllocs { 0 };
void* operator new (std::size_t n)                 { if (gCount.load()) ++gAllocs; if (void* p = std::malloc (n ? n : 1)) return p; throw std::bad_alloc(); }
void* operator new[] (std::size_t n)               { if (gCount.load()) ++gAllocs; if (void* p = std::malloc (n ? n : 1)) return p; throw std::bad_alloc(); }
void  operator delete (void* p) noexcept           { std::free (p); }
void  operator delete[] (void* p) noexcept         { std::free (p); }
void  operator delete (void* p, std::size_t) noexcept   { std::free (p); }
void  operator delete[] (void* p, std::size_t) noexcept { std::free (p); }

using namespace tw;
static void setEnv (const char* k, const char* v)
{
   #if defined (_WIN32)
    _putenv_s (k, v);
   #else
    setenv (k, v, 1);
   #endif
}
static constexpr double kSR = 48000.0;
static constexpr double kPi = 3.14159265358979323846;
static double mtof (double m) { return 440.0 * std::pow (2.0, (m - 69.0) / 12.0); }

//==================================================================================================
//  Fixture generator
//==================================================================================================
namespace gen
{
    struct Smp { std::vector<float> L, R; bool stereo = false; };

    static void writeFlac (const juce::File& f, const Smp& s)
    {
        f.deleteFile();
        juce::FlacAudioFormat flac;
        std::unique_ptr<juce::OutputStream> os = std::make_unique<juce::FileOutputStream> (f);
        const int ch = s.stereo ? 2 : 1;
        auto w = flac.createWriterFor (os, juce::AudioFormatWriterOptions{}.withSampleRate (kSR).withNumChannels (ch).withBitsPerSample (16));
        if (w == nullptr) { std::printf ("cannot write %s\n", f.getFullPathName().toRawUTF8()); std::exit (2); }
        const float* chans[2] = { s.L.data(), s.stereo ? s.R.data() : s.L.data() };
        w->writeFromFloatArrays (chans, ch, (int) s.L.size());
    }

    /** Integer number of f0 cycles close to `target` seconds (seam phase error < 0.02 cycle when possible). */
    static int64_t wholeCycles (double f0, double target)
    {
        double best = 1e9; int64_t bestL = (int64_t) (target * kSR);
        const int k0 = std::max (1, (int) (target * 0.8 * f0)), k1 = std::max (k0 + 1, (int) (target * 1.2 * f0));
        for (int k = k0; k <= k1; ++k)
        {
            const double L = k * kSR / f0; const double err = std::abs (L - std::round (L)) * f0 / kSR;  // cycles of error
            if (err < best) { best = err; bestL = (int64_t) std::llround (L); }
        }
        return bestL;
    }

    static juce::var region (const juce::String& kind, int smp, int lk, int hk, int lv, int hv, int root, int64_t end)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("a", 0); o->setProperty ("kind", kind); o->setProperty ("smp", smp);
        o->setProperty ("lk", lk); o->setProperty ("hk", hk); o->setProperty ("lv", lv); o->setProperty ("hv", hv);
        o->setProperty ("xfLo", lv); o->setProperty ("xfHi", hv); o->setProperty ("root", root);
        o->setProperty ("cents", 0.0); o->setProperty ("gainDb", 0.0); o->setProperty ("gainNorm", 1.0); o->setProperty ("pan", 0);
        o->setProperty ("start", 0); o->setProperty ("end", (juce::int64) end); o->setProperty ("onset", 48);
        o->setProperty ("loop", "no_loop"); o->setProperty ("ls", 0); o->setProperty ("le", 0); o->setProperty ("xf", 0);
        o->setProperty ("tailLs", 0); o->setProperty ("tailLe", 0);
        o->setProperty ("rr", juce::Array<juce::var> { 0, 1 }); o->setProperty ("rand", juce::Array<juce::var> { 0, 1 });
        o->setProperty ("grp", 0); o->setProperty ("offBy", 0); o->setProperty ("offMode", "normal");
        auto* e = new juce::DynamicObject(); e->setProperty ("a", 0.002); e->setProperty ("h", 0); e->setProperty ("d", 0); e->setProperty ("s", 1); e->setProperty ("r", 0.25);
        o->setProperty ("env", juce::var (e));
        o->setProperty ("rtDecay", 0);
        o->setProperty ("velCurve", juce::Array<juce::var> { juce::Array<juce::var> { 0, 0 }, juce::Array<juce::var> { 127, 1 } });
        return juce::var (o);
    }
    static void setLoop (juce::var& r, const char* mode, int64_t ls, int64_t le, int64_t xf)
    { auto* o = r.getDynamicObject(); o->setProperty ("loop", mode); o->setProperty ("ls", (juce::int64) ls); o->setProperty ("le", (juce::int64) le); o->setProperty ("xf", (juce::int64) xf); }

    static void writeInstrument (const juce::File& root, const juce::String& id, const juce::String& name, const juce::String& family,
                                 const std::vector<Smp>& smps, const juce::Array<juce::var>& regs, bool hasNoise, bool hasRelease,
                                 const juce::String& readme, juce::Array<juce::var> artics = { "Sustain" })
    {
        auto dir = root.getChildFile (id);
        dir.getChildFile ("samples").createDirectory();
        dir.getChildFile ("source").createDirectory();
        juce::Array<juce::var> names;
        for (size_t i = 0; i < smps.size(); ++i)
        {
            const auto n = juce::String (i + 1).paddedLeft ('0', 4) + ".flac";
            names.add (n);
            writeFlac (dir.getChildFile ("samples").getChildFile (n), smps[i]);
        }
        auto* m = new juce::DynamicObject();
        m->setProperty ("torg", 1); m->setProperty ("id", id); m->setProperty ("name", name); m->setProperty ("family", family);
        m->setProperty ("category", "Keys"); m->setProperty ("credit", "Terrain test fixture (generated by OrganicEngine_test --gen)");
        m->setProperty ("polyMax", 32); m->setProperty ("hasNoise", hasNoise); m->setProperty ("hasRelease", hasRelease);
        m->setProperty ("artics", artics);
        m->setProperty ("samples", names);
        m->setProperty ("regions", regs);
        dir.getChildFile ("map.json").replaceWithText (juce::JSON::toString (juce::var (m)));
        dir.getChildFile ("source").getChildFile ("README.txt").replaceWithText (readme);
    }

    static Smp tone (double f0, int64_t n, std::function<float (double t, int harm)> amp, int maxH, double ampScale = 1.0)
    {
        Smp s; s.L.assign ((size_t) n, 0.f);
        for (int h = 1; h <= maxH; ++h)
        {
            if (h * f0 > 20000.0) break;
            const double w = 2.0 * kPi * h * f0 / kSR;
            for (int64_t i = 0; i < n; ++i) s.L[(size_t) i] += (float) (ampScale * amp ((double) i / kSR, h) * std::sin (w * (double) i));
        }
        return s;
    }

    static void all (const juce::File& root)
    {
        // ── test.layers: 4 velocity layers, one zone, authored equal-power crossfade bands, marker per layer ──
        {
            const double f0 = mtof (48); const int64_t n = 72000, ls = 12000, le = ls + wholeCycles (f0, 0.8);
            const int mk[4] = { 3, 5, 7, 9 };
            std::vector<Smp> S; juce::Array<juce::var> R;
            const int lv[4] = { 1, 36, 76, 106 }, hv[4] = { 44, 84, 114, 127 }, xfLo[4] = { 1, 44, 84, 114 }, xfHi[4] = { 36, 76, 106, 127 };
            for (int i = 0; i < 4; ++i)
            {
                S.push_back (tone (f0, n, [&] (double, int h) { return h == 1 ? 0.5f : (h == mk[i] ? 0.1f : 0.f); }, 9));
                auto r = region ("attack", i, 0, 127, lv[i], hv[i], 48, n);
                r.getDynamicObject()->setProperty ("xfLo", xfLo[i]); r.getDynamicObject()->setProperty ("xfHi", xfHi[i]);
                setLoop (r, "continuous", ls, le, 480);
                R.add (r);
            }
            {   // articulation 1 ("Staccato"): one region, marker k11
                S.push_back (tone (f0, n, [&] (double, int h) { return h == 1 ? 0.5f : (h == 11 ? 0.1f : 0.f); }, 11));
                auto r = region ("attack", 4, 0, 127, 1, 127, 48, n);
                r.getDynamicObject()->setProperty ("a", 1);
                setLoop (r, "continuous", ls, le, 480);
                R.add (r);
            }
            writeInstrument (root, "test.layers", "Test Layers", "grand", S, R, false, false,
                "4 velocity layers over all keys, root 48 (C3). Sine at root + a marker partial per layer (-14 dB): L1 k3 vel 1-44 (fade-out 36-44), "
                "L2 k5 36-84 (in 36-44, out 76-84), L3 k7 76-114 (in 76-84, out 106-114), L4 k9 106-127 (in 106-114). Loop continuous, whole cycles. "
                "Articulation 1 (Staccato): one region, marker k11.", { "Sustain", "Staccato" });
        }
        // ── test.rr: seq RR (keys 0-59, seq_length 4) + random RR (keys 60-127, 4 x 0.25) ──
        {
            std::vector<Smp> S; juce::Array<juce::var> R;
            const int mk[4] = { 3, 5, 7, 9 };
            const int64_t n = 19200;
            for (int half = 0; half < 2; ++half)
                for (int i = 0; i < 4; ++i)
                {
                    const int root = half == 0 ? 48 : 72;
                    S.push_back (tone (mtof (root), n, [&] (double t, int h) {
                        const float fo = (float) std::min (1.0, (0.4 - t) / 0.01);
                        return fo * (h == 1 ? 0.5f : (h == mk[i] ? 0.1f : 0.f)); }, 9));
                    auto r = region ("attack", half * 4 + i, half == 0 ? 0 : 60, half == 0 ? 59 : 127, 1, 127, root, n);
                    if (half == 0) r.getDynamicObject()->setProperty ("rr", juce::Array<juce::var> { i, 4 });
                    else           r.getDynamicObject()->setProperty ("rand", juce::Array<juce::var> { i * 0.25, (i + 1) * 0.25 });
                    R.add (r);
                }
            writeInstrument (root, "test.rr", "Test Round Robin", "grand", S, R, false, false,
                "Keys 0-59 (root 48): seq_length 4, positions 0..3 carry markers k3,k5,k7,k9. Keys 60-127 (root 72): random RR, "
                "rand [0,.25) [.25,.5) [.5,.75) [.75,1) with markers k3,k5,k7,k9. 0.4 s one-shot tones.");
        }
        // ── test.norr: stereo harmonic zones every 6 st with a FIXED formant (1.2 kHz), no RR; + white-noise zone ──
        {
            std::vector<Smp> S; juce::Array<juce::var> R;
            juce::Random rnd (1234);
            {   // noise zone keys 0-11, root 6
                Smp s; s.stereo = true; const int64_t n = 48000;
                s.L.resize ((size_t) n); s.R.resize ((size_t) n);
                for (int64_t i = 0; i < n; ++i) { const float a = rnd.nextFloat() * 0.5f - 0.25f, b = rnd.nextFloat() * 0.1f - 0.05f; s.L[(size_t) i] = a + b; s.R[(size_t) i] = a - b; }
                S.push_back (s);
                auto r = region ("attack", 0, 0, 11, 1, 127, 6, n); setLoop (r, "continuous", 9600, 48000, 4800); R.add (r);
            }
            for (int z = 0; z < 16; ++z)
            {
                const int root = 17 + 6 * z;
                const double f0 = mtof (root);
                const int64_t ls = 4800, le = ls + wholeCycles (f0, 0.5), n = le + 16;
                std::vector<double> ph1 (400), ph2 (400);
                for (auto& p : ph2) p = rnd.nextDouble() * 2.0 * kPi;
                Smp s; s.stereo = true; s.L.assign ((size_t) n, 0.f); s.R.assign ((size_t) n, 0.f);
                double peak = 0.0;
                std::vector<double> x ((size_t) n, 0.0), y ((size_t) n, 0.0);
                for (int h = 1; h < 400 && h * f0 < 20000.0; ++h)
                {
                    const double fh = h * f0, d = (fh - 1200.0) / 500.0;
                    const double a = (1.0 / (1.0 + d * d) + 0.02) / std::sqrt ((double) h);
                    const double w = 2.0 * kPi * fh / kSR;
                    for (int64_t i = 0; i < n; ++i) { x[(size_t) i] += a * std::sin (w * i); y[(size_t) i] += a * std::sin (w * i + ph2[(size_t) h]); }
                }
                for (int64_t i = 0; i < n; ++i) peak = std::max (peak, std::abs (x[(size_t) i]) + 0.25 * std::abs (y[(size_t) i]));
                for (int64_t i = 0; i < n; ++i)
                {
                    const double fi = std::min (1.0, (double) i / 48.0);
                    s.L[(size_t) i] = (float) (0.6 * fi * (x[(size_t) i] + 0.25 * y[(size_t) i]) / peak);
                    s.R[(size_t) i] = (float) (0.6 * fi * (x[(size_t) i] - 0.25 * y[(size_t) i]) / peak);
                }
                S.push_back (s);
                auto r = region ("attack", z + 1, root - 5, z == 15 ? 127 : root, 1, 127, root, n);
                setLoop (r, "continuous", ls, le, 256);
                R.add (r);
            }
            writeInstrument (root, "test.norr", "Test No-RR Formant", "violin", S, R, false, false,
                "No round robin (fake-RR target). Stereo. Keys 0-11: white noise (root 6, loop). Zones every 6 st from root 17 to 107 "
                "(zone = root-5..root, the last to 127): harmonic series at the root with a FIXED 1.2 kHz formant, side = 0.25 x a "
                "phase-scrambled copy (Image). Body borrows a neighbour zone and repitches it -> the formant moves, the pitch does not.");
        }
        // ── test.piano: decaying tones with air + onset + tail loop, a release region (rt_decay), a noise region, a choke zone ──
        {
            std::vector<Smp> S; juce::Array<juce::var> R;
            juce::Random rnd (99);
            auto pianoSample = [&] (int root, int64_t& onset, int64_t& tls, int64_t& tle) {
                const double f0 = mtof (root); const int64_t n = 4 * 48000;
                Smp s = tone (f0, n, [] (double t, int h) {
                    if (t < 0.02) return 0.f;
                    const double u = t - 0.02;
                    return (float) ((1.0 - std::exp (-u / 0.001)) * std::pow (10.0, -12.0 * u / 20.0) / h); }, 8, 0.4);
                for (int64_t i = 0; i < 960; ++i) s.L[(size_t) i] += (rnd.nextFloat() * 2.f - 1.f) * 0.003f;   // 20 ms of air
                float pk = 0.f; for (auto v : s.L) pk = std::max (pk, std::abs (v));
                onset = 0; while (onset < n && std::abs (s.L[(size_t) onset]) < pk * 0.063f) ++onset;
                tls = (int64_t) (2.4 * 48000); tle = tls + wholeCycles (f0, 0.3);
                return s;
            };
            int64_t on, tls, tle;
            S.push_back (pianoSample (43, on, tls, tle));
            { auto r = region ("attack", 0, 0, 47, 1, 127, 43, 4 * 48000); auto* o = r.getDynamicObject();
              o->setProperty ("onset", (juce::int64) on); o->setProperty ("tailLs", (juce::int64) tls); o->setProperty ("tailLe", (juce::int64) tle); R.add (r); }
            S.push_back (pianoSample (72, on, tls, tle));
            { auto r = region ("attack", 1, 60, 127, 1, 127, 72, 4 * 48000); auto* o = r.getDynamicObject();
              o->setProperty ("onset", (juce::int64) on); o->setProperty ("tailLs", (juce::int64) tls); o->setProperty ("tailLe", (juce::int64) tle); R.add (r); }
            {   // choke zone: sustained loop, grp 1 offBy 1 (a new note in the group chokes the old one)
                const double f0 = mtof (52); const int64_t n = 48000, ls = 9600, le = ls + wholeCycles (f0, 0.6);
                S.push_back (tone (f0, n, [] (double, int h) { return h == 1 ? 0.5f : (h == 3 ? 0.1f : 0.f); }, 3));
                auto r = region ("attack", 2, 48, 59, 1, 127, 52, n); setLoop (r, "continuous", ls, le, 480);
                r.getDynamicObject()->setProperty ("grp", 1); r.getDynamicObject()->setProperty ("offBy", 1); R.add (r);
            }
            {   // release: 1 kHz damper tone, rt_decay 3 dB/s
                const int64_t n = 19200;
                S.push_back (tone (1000.0, n, [] (double t, int) { return (float) (0.3 * std::min (1.0, t / 0.002) * std::pow (10.0, -75.0 * t / 20.0)); }, 1));
                auto r = region ("release", 3, 0, 127, 1, 127, 60, n); r.getDynamicObject()->setProperty ("rtDecay", 3.0); R.add (r);
            }
            {   // noise: 60 ms key-off burst (noise regions trigger at NOTE-OFF, like releases)
                Smp s; const int64_t n = 2880; s.L.resize ((size_t) n);
                for (int64_t i = 0; i < n; ++i) s.L[(size_t) i] = (rnd.nextFloat() * 2.f - 1.f) * 0.2f * (float) std::min (1.0, i / 48.0) * (float) std::exp (-(double) i / 700.0);
                S.push_back (s);
                R.add (region ("noise", 4, 0, 99, 1, 127, 60, n));
            }
            writeInstrument (root, "test.piano", "Test Decaying Piano", "grand", S, R, true, true,
                "Keys 0-47 root 43 and 60-127 root 72: 20 ms of air (-50 dB noise) then an 8-harmonic tone decaying 12 dB/s, 4 s, "
                "onset marker at the -24 dB crossing, tail loop at 2.4 s (whole cycles, ~0.3 s). Keys 48-59 root 52: a looped tone in "
                "choke group 1 (grp 1, offBy 1). Release region (all keys): 1 kHz damper tone, rt_decay 3 dB/s. Noise region (keys 0-99): 60 ms key-off burst "
                "(noise regions trigger at note-off, level = the Noise knob).");
        }
    }
}

//==================================================================================================
//  Analysis
//==================================================================================================
namespace an
{
    using Buf = std::vector<float>;

    /** Hann-windowed single-bin DFT: amplitude of a sinusoid at f (≈ its peak amplitude). */
    static std::complex<double> dft (const Buf& x, int64_t s, int64_t n, double f)
    {
        std::complex<double> acc = 0; double wsum = 0;
        const double w = 2.0 * kPi * f / kSR;
        for (int64_t i = 0; i < n && s + i < (int64_t) x.size(); ++i)
        {
            const double h = 0.5 - 0.5 * std::cos (2.0 * kPi * (double) i / (double) (n - 1));
            acc += h * (double) x[(size_t) (s + i)] * std::polar (1.0, -w * (double) (s + i));
            wsum += h;
        }
        return acc * (2.0 / wsum);
    }
    static double amp (const Buf& x, int64_t s, int64_t n, double f) { return std::abs (dft (x, s, n, f)); }

    /** f0 from the DFT phase advance at the expected frequency between two windows (sub-cent accurate). */
    static double freq (const Buf& x, int64_t s, int64_t n, int64_t hop, double fExp)
    {
        const auto a = dft (x, s, n, fExp), b = dft (x, s + hop, n, fExp);
        double d = std::arg (b / a);                       // the reference rotation is already removed by the global-phase DFT
        return fExp + d / (2.0 * kPi * (double) hop / kSR);
    }
    static double cents (double f, double ref) { return 1200.0 * std::log2 (f / ref); }

    static double rms (const Buf& x, int64_t s, int64_t n)
    {
        double a = 0; int64_t c = 0;
        for (int64_t i = s; i < s + n && i < (int64_t) x.size(); ++i) { a += (double) x[(size_t) i] * x[(size_t) i]; ++c; }
        return std::sqrt (a / (double) std::max<int64_t> (1, c));
    }
    static double db (double v) { return 20.0 * std::log10 (v + 1e-20); }

    static void fft (std::vector<std::complex<double>>& a)
    {
        const size_t n = a.size();
        for (size_t i = 1, j = 0; i < n; ++i) { size_t bit = n >> 1; for (; j & bit; bit >>= 1) j ^= bit; j ^= bit; if (i < j) std::swap (a[i], a[j]); }
        for (size_t len = 2; len <= n; len <<= 1)
        {
            const double ang = -2.0 * kPi / (double) len;
            const std::complex<double> wl (std::cos (ang), std::sin (ang));
            for (size_t i = 0; i < n; i += len)
            {
                std::complex<double> w (1);
                for (size_t j = 0; j < len / 2; ++j) { auto u = a[i + j], v = a[i + j + len / 2] * w; a[i + j] = u + v; a[i + j + len / 2] = u - v; w *= wl; }
            }
        }
    }
    static std::vector<double> mag (const Buf& x, int64_t s, int n = 16384)
    {
        std::vector<std::complex<double>> a ((size_t) n);
        for (int i = 0; i < n; ++i)
        {
            const double h = 0.5 - 0.5 * std::cos (2.0 * kPi * i / (n - 1));
            a[(size_t) i] = s + i < (int64_t) x.size() ? h * x[(size_t) (s + i)] : 0.0;
        }
        fft (a);
        std::vector<double> m ((size_t) n / 2);
        for (int i = 0; i < n / 2; ++i) m[(size_t) i] = std::abs (a[(size_t) i]);
        return m;
    }
    /** f0 by FFT peak near fExp (±60 ¢): Hann window of n, zero-padded ×4, parabolic on log magnitude. */
    static double peakFreq (const Buf& x, int64_t s, int n, double fExp)
    {
        int N = 1; while (N < n * 4) N <<= 1;
        std::vector<std::complex<double>> a ((size_t) N);
        for (int i = 0; i < n; ++i)
        {
            const double h = 0.5 - 0.5 * std::cos (2.0 * kPi * i / (n - 1));
            a[(size_t) i] = s + i < (int64_t) x.size() ? h * x[(size_t) (s + i)] : 0.0;
        }
        fft (a);
        const int b0 = std::max (2, (int) (fExp * std::pow (2.0, -60.0 / 1200.0) * N / kSR));
        const int b1 = std::min (N / 2 - 2, (int) (fExp * std::pow (2.0, 60.0 / 1200.0) * N / kSR) + 1);
        int bk = b0; for (int b = b0; b <= b1; ++b) if (std::abs (a[(size_t) b]) > std::abs (a[(size_t) bk])) bk = b;
        const double l = std::log (std::abs (a[(size_t) bk - 1]) + 1e-30), c = std::log (std::abs (a[(size_t) bk]) + 1e-30), r = std::log (std::abs (a[(size_t) bk + 1]) + 1e-30);
        const double d = 0.5 * (l - r) / (l - 2 * c + r);
        return ((double) bk + d) * kSR / N;
    }
    static double centroid (const Buf& x, int64_t s, int n = 16384)
    {
        const auto m = mag (x, s, n);
        double num = 0, den = 0;
        for (size_t i = 1; i < m.size(); ++i) { const double f = (double) i * kSR / n; if (f < 20 || f > 20000) continue; num += f * m[i]; den += m[i]; }
        return num / std::max (1e-20, den);
    }
    static double band (const Buf& x, int64_t s, double f1, double f2, int n = 16384)
    {
        const auto m = mag (x, s, n);
        double e = 0;
        for (size_t i = 1; i < m.size(); ++i) { const double f = (double) i * kSR / n; if (f >= f1 && f <= f2) e += m[i] * m[i]; }
        return e;
    }

    /** 4th-order Butterworth high-pass at fc (two cascaded biquads), then peak |y|. */
    static Buf highpass (const Buf& x, double fc)
    {
        Buf y = x;
        for (double q : { 0.5411961, 1.3065630 })
        {
            const double w0 = 2 * kPi * fc / kSR, al = std::sin (w0) / (2 * q), c = std::cos (w0);
            const double b0 = (1 + c) / 2, b1 = -(1 + c), b2 = (1 + c) / 2, a0 = 1 + al, a1 = -2 * c, a2 = 1 - al;
            double x1 = 0, x2 = 0, y1 = 0, y2 = 0;
            for (auto& v : y)
            {
                const double in = v, o = (b0 * in + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2) / a0;
                x2 = x1; x1 = in; y2 = y1; y1 = o; v = (float) o;
            }
        }
        return y;
    }
    static double peak (const Buf& x, int64_t s = 0, int64_t n = -1)
    {
        double p = 0; const int64_t e = n < 0 ? (int64_t) x.size() : std::min ((int64_t) x.size(), s + n);
        for (int64_t i = s; i < e; ++i) p = std::max (p, (double) std::abs (x[(size_t) i]));
        return p;
    }
    /** HP-residual click metric: peak HP(8k) relative to the signal peak, skipping the filter's own warm-up. */
    struct Click { double relDb; double localRatio; double atMs; };
    static Click clicks (const Buf& x)
    {
        const auto h = highpass (x, 8000.0);
        const double sp = peak (x);
        double hp = 0; int64_t at = 0;
        for (int64_t i = 256; i < (int64_t) h.size(); ++i) if (std::abs (h[(size_t) i]) > hp) { hp = std::abs (h[(size_t) i]); at = i; }
        // local median |Δ| around the worst point
        std::vector<double> d;
        for (int64_t i = std::max<int64_t> (1, at - 2400); i < std::min<int64_t> ((int64_t) x.size(), at + 2400); ++i) d.push_back (std::abs ((double) x[(size_t) i] - x[(size_t) i - 1]));
        std::nth_element (d.begin(), d.begin() + (long) d.size() / 2, d.end());
        const double med = d.empty() ? 1e-20 : d[d.size() / 2];
        const double dAt = at > 0 ? std::abs ((double) x[(size_t) at] - x[(size_t) at - 1]) : 0.0;
        return { db (hp / std::max (1e-20, sp)), dAt / std::max (1e-20, med), 1000.0 * (double) at / kSR };
    }
}

//==================================================================================================
//  Harness
//==================================================================================================
static int gFails = 0, gBars = 0;
static void bar (const char* name, bool ok, const std::string& detail)
{
    ++gBars; if (! ok) ++gFails;
    std::printf ("%s  %-46s %s\n", ok ? "PASS" : "FAIL", name, detail.c_str());
    std::fflush (stdout);
}
static std::string fmt (const char* f, ...)
{
    char b[1024]; va_list ap; va_start (ap, f); std::vsnprintf (b, sizeof b, f, ap); va_end (ap); return b;
}

static std::shared_ptr<const OrganicInstrument> load (const juce::String& id)
{
    std::shared_ptr<const OrganicInstrument> out; bool done = false;
    OrganicsLibrary::get().request (id, [&] (std::shared_ptr<const OrganicInstrument> p) { out = p; done = true; });
    for (int i = 0; i < 1000 && ! done; ++i) juce::MessageManager::getInstance()->runDispatchLoopUntil (5);
    return out;
}

struct Rec { an::Buf L, R; };
/** Render `frames` into rec (appending), in blocks of `blk`, with an optional per-block param hook. */
static void run (OrganicEngine& e, OrganicParams p, Rec& rec, int64_t frames, int blk = 256, float pitch = 0.f,
                 std::function<void (OrganicParams&, int64_t)> hook = {})
{
    std::vector<float> l ((size_t) blk), r ((size_t) blk);
    for (int64_t done = 0; done < frames;)
    {
        const int n = (int) std::min<int64_t> (blk, frames - done);
        std::fill (l.begin(), l.end(), 0.f); std::fill (r.begin(), r.end(), 0.f);
        if (hook) hook (p, (int64_t) rec.L.size());
        e.render (p, pitch, l.data(), r.data(), n);
        rec.L.insert (rec.L.end(), l.begin(), l.begin() + n);
        rec.R.insert (rec.R.end(), r.begin(), r.begin() + n);
        done += n;
    }
}
static OrganicParams P0() { OrganicParams p; p.human = 0.f; return p; }
static an::Buf mono (const Rec& r) { an::Buf m (r.L.size()); for (size_t i = 0; i < m.size(); ++i) m[i] = 0.5f * (r.L[i] + r.R[i]); return m; }

static const float kNoDet[16] = {};

//==================================================================================================
int main (int argc, char** argv)
{
    if (argc >= 3 && std::string (argv[1]) == "--gen")
    {
        juce::File root (juce::File::getCurrentWorkingDirectory().getChildFile (argv[2]));
        root.createDirectory();
        gen::all (root);
        std::printf ("fixtures written to %s\n", root.getFullPathName().toRawUTF8());
        return 0;
    }
    if (argc < 2) { std::printf ("usage: %s <fixturesRoot> [<compiledLibraryRoot>] | --gen <dir>\n", argv[0]); return 2; }
    const auto fixRoot = juce::File::getCurrentWorkingDirectory().getChildFile (argv[1]);
    setEnv ("TERRAIN_ORGANICS_DIR", fixRoot.getFullPathName().toRawUTF8());
    juce::MessageManager::getInstance();

    std::printf ("══ ORGANICS ENGINE GATES (Agent B) — fixtures %s ══\n", fixRoot.getFullPathName().toRawUTF8());

    // ── library basics ────────────────────────────────────────────────────────────────────────────
    {
        auto& L = OrganicsLibrary::get();
        const bool rootOk = L.root() == fixRoot;
        const int  idx = L.idToIndex ("test.sine");
        const auto back = L.indexToId (1);
        const auto ix = L.index();
        bar ("library: root / ids / index", rootOk && idx == 1 && back == "test.sine" && L.idToIndex ("nope") == -1
                 && L.indexToId (999).isEmpty() && ix.isArray() && ix.size() >= 1,
             fmt ("root %s, idToIndex(test.sine)=%d, indexToId(1)=%s, index entries=%d", rootOk ? "=env" : "WRONG", idx, back.toRawUTF8(), ix.size()));
    }
    auto sine = load ("test.sine"), layers = load ("test.layers"), rr = load ("test.rr"), norr = load ("test.norr"), piano = load ("test.piano");
    bar ("library: fixtures load (background thread → message cb)", sine && layers && rr && norr && piano,
         fmt ("sine=%d layers=%d rr=%d norr=%d piano=%d  resident=%d (%.2f MB)", !! sine, !! layers, !! rr, !! norr, !! piano,
              OrganicsLibrary::get().residentCount(), OrganicsLibrary::get().residentBytes() / 1048576.0));
    if (! (sine && layers && rr && norr && piano)) { std::printf ("cannot continue\n"); return 1; }
    {
        auto again = load ("test.sine");
        bar ("library: cache shares one object per id", again.get() == sine.get(), fmt ("same pointer: %s", again.get() == sine.get() ? "yes" : "no"));
    }

    // ── 1. Pitch per key 21–108, Human 0, Body 0 / ±1, Hermite and Sinc8 ──────────────────────────
    {
        for (float body : { 0.f, -1.f, 1.f })
        {
            double worst = 0; int worstKey = 0;
            for (int key = 21; key <= 108; ++key)
            {
                OrganicEngine e; e.prepare (kSR, 512); e.setInstrument (sine);
                auto p = P0(); p.body = body;
                e.noteOn (key, 0.8f, 1, kNoDet, 1u);
                Rec r; run (e, p, r, 48000, 512);
                const auto m = mono (r);
                const double f = an::freq (m, 9600, 16384, 4096, mtof (key));
                const double c = std::abs (an::cents (f, mtof (key)));
                if (c > worst) { worst = c; worstKey = key; }
            }
            bar (fmt ("pitch keys 21-108, Body %+.0f (Hermite)", body).c_str(), worst <= 3.0, fmt ("max |error| %.4f cents (key %d)", worst, worstKey));
        }
        double worst = 0; int worstKey = 0;
        for (int key = 21; key <= 108; key += 3)
        {
            OrganicEngine e; e.prepare (kSR, 512); e.setInstrument (sine); e.setNonRealtime (true);
            e.noteOn (key, 0.8f, 1, kNoDet, 1u);
            Rec r; run (e, P0(), r, 48000, 512);
            const double c = std::abs (an::cents (an::freq (mono (r), 9600, 16384, 4096, mtof (key)), mtof (key)));
            if (c > worst) { worst = c; worstKey = key; }
        }
        bar ("pitch keys 21-108 step 3, Sinc8 (non-realtime)", worst <= 3.0, fmt ("max |error| %.4f cents (key %d)", worst, worstKey));
        // host at 44.1 kHz (the fixtures are 48 kHz): read at 48 k the tone appears ×48/44.1 higher
        worst = 0; worstKey = 0;
        for (int key = 21; key <= 108; key += 5)
        {
            OrganicEngine e; e.prepare (44100.0, 512); e.setInstrument (sine);
            e.noteOn (key, 0.8f, 1, kNoDet, 1u);
            Rec r; run (e, P0(), r, 44100, 512);
            const double fx = mtof (key) * 48000.0 / 44100.0;
            const double c = std::abs (an::cents (an::freq (mono (r), 8820, 16384, 4096, fx), fx));
            if (c > worst) { worst = c; worstKey = key; }
        }
        bar ("pitch at a 44.1 kHz host (48 kHz samples)", worst <= 3.0, fmt ("max |error| %.4f cents (key %d)", worst, worstKey));
    }

    // ── 2. Velocity layers ─────────────────────────────────────────────────────────────────────────
    {
        const double f0 = mtof (48); const int mk[4] = { 3, 5, 7, 9 };
        auto markers = [&] (const an::Buf& m, int64_t s, int64_t n, double out[4]) { for (int i = 0; i < 4; ++i) out[i] = an::amp (m, s, n, mk[i] * f0); };
        auto renderVel = [&] (float vel01, float dyn, double out[4]) {
            OrganicEngine e; e.prepare (kSR, 512); e.setInstrument (layers);
            auto p = P0(); p.velo = 0.f; p.dyn = dyn;
            e.noteOn (48, vel01, 1, kNoDet, 7u);
            Rec r; run (e, p, r, 19200, 512);
            markers (mono (r), 4800, 12000, out);
        };
        double ref[4];
        { double o[4]; renderVel (20 / 127.f, 0, o); ref[0] = o[0]; renderVel (60 / 127.f, 0, o); ref[1] = o[1];
          renderVel (95 / 127.f, 0, o); ref[2] = o[2]; renderVel (120 / 127.f, 0, o); ref[3] = o[3]; }
        double worstPow = 0; int badMarker = 0, nonAdj = 0; std::string firstBad;
        for (int v = 1; v <= 127; ++v)
        {
            double o[4]; renderVel ((float) v / 127.f, 0, o);
            double pw[4], sum = 0; for (int i = 0; i < 4; ++i) { pw[i] = (o[i] / ref[i]) * (o[i] / ref[i]); sum += pw[i]; }
            worstPow = std::max (worstPow, std::abs (10 * std::log10 (sum)));
            // expected layer set from the authored bands
            const int lv[4] = { 1, 36, 76, 106 }, hv[4] = { 44, 84, 114, 127 };
            std::vector<int> on;
            for (int i = 0; i < 4; ++i) if (pw[i] > 1e-3) on.push_back (i);
            for (int i = 0; i < 4; ++i)
            {
                const bool inRange = v >= lv[i] && v <= hv[i];
                if (pw[i] > 1e-3 && ! inRange) { ++badMarker; if (firstBad.empty()) firstBad = fmt ("v%d marker L%d", v, i + 1); }
            }
            if (on.size() > 2 || (on.size() == 2 && on[1] - on[0] != 1)) ++nonAdj;
        }
        bar ("velocity: right marker per band", badMarker == 0, fmt ("markers outside their band: %d %s", badMarker, firstBad.c_str()));
        bar ("velocity: summed power across crossfades", worstPow <= 0.5, fmt ("worst |Σp − 1| = %.3f dB over v 1..127", worstPow));
        bar ("velocity: no non-adjacent marker", nonAdj == 0, fmt ("velocities with >2 or non-adjacent layers: %d", nonAdj));

        // contiguous layers (the compiler's format: xfLo/xfHi == lv/hv) get the runtime seam band: test.sine lo 1-63 / hi 64-127
        {
            auto sv = [&] (int v, double& lo, double& hi) {
                OrganicEngine e; e.prepare (kSR, 512); e.setInstrument (sine);
                auto p = P0(); p.velo = 0.f;
                e.noteOn (48, (float) v / 127.f, 1, kNoDet, 7u);
                Rec r; run (e, p, r, 19200, 512);
                lo = an::amp (mono (r), 4800, 12000, 3 * mtof (48)); hi = an::amp (mono (r), 4800, 12000, 5 * mtof (48));
            };
            double lr, hr, l0, h0; sv (20, lr, h0); sv (110, l0, hr);
            double worst = 0; int blended = 0;
            for (int v = 40; v <= 90; ++v)
            {
                double lo, hi; sv (v, lo, hi);
                const double pw = (lo / lr) * (lo / lr) + (hi / hr) * (hi / hr);
                worst = std::max (worst, std::abs (10 * std::log10 (pw)));
                blended += (lo / lr > 0.05 && hi / hr > 0.05) ? 1 : 0;
            }
            bar ("velocity: contiguous layers get an equal-power seam", worst <= 0.5 && blended >= 8,
                 fmt ("test.sine lo|hi at 63|64: %d velocities blend both, worst |Σp − 1| %.3f dB", blended, worst));
        }

        // Dynamics: vel 64 (L2 plateau) → −1 = L1 only, +1 = L4 only
        double lo[4], hi[4], mid[4];
        renderVel (64 / 127.f, -1.f, lo); renderVel (64 / 127.f, 1.f, hi); renderVel (64 / 127.f, 0.f, mid);
        const bool dOk = lo[0] / ref[0] > 0.9 && lo[1] / ref[1] < 0.03 && hi[3] / ref[3] > 0.9 && hi[2] / ref[2] < 0.03 && mid[1] / ref[1] > 0.9;
        bar ("dynamics: −1 → layer 1, 0 → layer 2, +1 → layer 4 (vel 64)", dOk,
             fmt ("dyn−1 L1 %.2f L2 %.3f · dyn0 L2 %.2f · dyn+1 L4 %.2f L3 %.3f", lo[0] / ref[0], lo[1] / ref[1], mid[1] / ref[1], hi[3] / ref[3], hi[2] / ref[2]));

        // live sweep while held: monotonic layer progression, ≤ 2 readers per note, no click
        OrganicEngine e; e.prepare (kSR, 512); e.setInstrument (layers);
        e.noteOn (48, 64 / 127.f, 1, kNoDet, 7u);
        int maxReaders = 0;
        Rec r; auto p = P0(); p.velo = 0.f; p.dyn = -1.f;
        const int64_t total = 4 * 48000;
        std::vector<float> l (256), rr2 (256);
        for (int64_t t = 0; t < total; t += 256)
        {
            p.dyn = (float) std::clamp (-1.0 + 2.0 * (double) (t - 24000) / (double) (total - 48000), -1.0, 1.0);
            std::fill (l.begin(), l.end(), 0.f); std::fill (rr2.begin(), rr2.end(), 0.f);
            e.render (p, 0.f, l.data(), rr2.data(), 256);
            maxReaders = std::max (maxReaders, organics_debug::lastRenderReaders());
            r.L.insert (r.L.end(), l.begin(), l.end()); r.R.insert (r.R.end(), rr2.begin(), rr2.end());
        }
        const auto m = mono (r);
        int prevDom = 0; bool mono_ = true; std::string seq;
        for (int64_t s = 0; s + 4800 < (int64_t) m.size(); s += 4800)
        {
            double o[4]; markers (m, s, 4800, o);
            int dom = 0; for (int i = 1; i < 4; ++i) if (o[i] / ref[i] > o[dom] / ref[dom]) dom = i;
            if (dom < prevDom) mono_ = false;
            if (seq.empty() || seq.back() != (char) ('1' + dom)) seq += (char) ('1' + dom);
            prevDom = dom;
        }
        const auto ck = an::clicks (m);
        bar ("dynamics live sweep: layers progress monotonically", mono_ && seq == "1234", fmt ("dominant-layer sequence %s", seq.c_str()));
        bar ("dynamics live sweep: ≤ 2 readers per note", maxReaders <= 2, fmt ("max readers %d", maxReaders));
        bar ("dynamics live sweep: no click (HP 8k residual)", ck.relDb <= -60.0 || ck.localRatio <= 1.5, fmt ("HP residual %.1f dB re note peak (local |Δ| ratio %.2f)", ck.relDb, ck.localRatio));
    }

    // ── 3. Round robin ─────────────────────────────────────────────────────────────────────────────
    {
        const int mk[4] = { 3, 5, 7, 9 };
        auto which = [&] (const an::Buf& m, int64_t s, double f0) {
            int best = 0; double bv = -1;
            for (int i = 0; i < 4; ++i) { const double a = an::amp (m, s, 4096, mk[i] * f0); if (a > bv) { bv = a; best = i; } }
            return best;
        };
        rr->resetPerformanceState();
        OrganicEngine e1, e2; e1.prepare (kSR, 512); e2.prepare (kSR, 512); e1.setInstrument (rr); e2.setInstrument (rr);
        std::string seq;
        for (int i = 0; i < 16; ++i)
        {
            auto& e = (i % 2) ? e2 : e1;                       // alternate voices: RR is per key on the instrument
            e.noteOn (48, 0.8f, 1, kNoDet, 100u + (uint32_t) i);
            Rec r; run (e, P0(), r, 9600, 512);
            e.noteOff (false); Rec t; run (e, P0(), t, 9600, 512);
            seq += (char) ('1' + which (mono (r), 2400, mtof (48)));
        }
        bar ("RR sequential: exactly 1234·1234 (two voices)", seq == "1234123412341234", fmt ("sequence %s", seq.c_str()));

        OrganicEngine e; e.prepare (kSR, 512); e.setInstrument (rr);
        int cnt[4] = {}, repeats = 0, last = -1;
        for (int i = 0; i < 400; ++i)
        {
            e.noteOn (72, 0.8f, 1, kNoDet, 7919u * (uint32_t) i + 13u);
            Rec r; run (e, P0(), r, 6144, 512);
            e.kill(); Rec t; run (e, P0(), t, 1024, 512);
            const int w = which (mono (r), 1024, mtof (72));
            cnt[w]++; if (w == last) ++repeats; last = w;
        }
        bool within = true; for (int c : cnt) within &= c >= 80 && c <= 120;
        bar ("RR random: 400 notes, no immediate repeat", repeats == 0, fmt ("immediate repeats %d", repeats));
        bar ("RR random: each 25% ± 5%", within, fmt ("counts %d / %d / %d / %d", cnt[0], cnt[1], cnt[2], cnt[3]));

        // fake RR on a no-RR set: Human > 0 → consecutive notes borrow different zones; Human 0 → never
        auto fake = [&] (float human, int& changes, double& centroidSpread) {
            OrganicEngine f; f.prepare (kSR, 512); f.setInstrument (norr);
            auto p = P0(); p.human = human;
            int prev = -1; changes = 0; double cmin = 1e9, cmax = -1e9;
            for (int i = 0; i < 30; ++i)
            {
                f.noteOn (60, 0.8f, 1, kNoDet, 31u * (uint32_t) i + 5u);
                Rec r; run (f, p, r, 12288, 512);
                const int reg = organics_debug::lastNoteRegion();
                if (prev >= 0 && reg != prev) ++changes;
                prev = reg;
                const double c = an::centroid (mono (r), 4096, 8192);
                cmin = std::min (cmin, c); cmax = std::max (cmax, c);
                f.kill(); Rec t; run (f, p, t, 1024, 512);
            }
            centroidSpread = cmax - cmin;
        };
        int ch0, ch5; double cs0, cs5;
        norr->resetPerformanceState(); fake (0.f, ch0, cs0);
        norr->resetPerformanceState(); fake (0.5f, ch5, cs5);
        bar ("fake RR: Human 0.5 on a no-RR set varies every note", ch5 == 29 && ch0 == 0,
             fmt ("zone changes over 29 repeats: Human0.5 %d, Human0 %d · centroid spread %.0f Hz vs %.1f Hz", ch5, ch0, cs5, cs0));

        // determinism: Human 0 + fixed seed → bit identical run to run (also Human 0.5: the seed decides)
        auto seqRender = [&] (float human) {
            rr->resetPerformanceState(); sine->resetPerformanceState();
            OrganicEngine d; d.prepare (kSR, 512); d.setInstrument (rr);
            auto p = P0(); p.human = human;
            Rec r;
            for (int i = 0; i < 8; ++i) { d.noteOn (i % 2 ? 48 : 72, 0.7f, 3, kNoDet, 4242u); run (d, p, r, 4000, 512); d.noteOff (false); run (d, p, r, 2000, 512); }
            return r.L;
        };
        const auto a0 = seqRender (0.f), b0 = seqRender (0.f), a5 = seqRender (0.5f), b5 = seqRender (0.5f);
        size_t diff0 = 0, diff5 = 0; for (size_t i = 0; i < a0.size(); ++i) { diff0 += a0[i] != b0[i]; diff5 += a5[i] != b5[i]; }
        bar ("determinism: fixed seed is bit-identical run to run", diff0 == 0 && diff5 == 0 && a0 != a5,
             fmt ("differing samples: Human0 %zu, Human0.5 %zu (Human changes the output: %s)", diff0, diff5, a0 != a5 ? "yes" : "no"));
    }

    // ── 4. No clicks ───────────────────────────────────────────────────────────────────────────────
    {
        juce::Random rnd (777);
        {   // 10 s looped sustain
            OrganicEngine e; e.prepare (kSR, 512); e.setInstrument (sine);
            e.noteOn (48, 0.8f, 1, kNoDet, 1u);
            Rec r; run (e, P0(), r, 10 * 48000, 512);
            const auto c = an::clicks (mono (r));
            bar ("no clicks: 10 s looped sustain (loop seams)", c.relDb <= -60.0 || c.localRatio <= 1.5, fmt ("HP residual %.1f dB (local ratio %.2f, worst at %.0f ms)", c.relDb, c.localRatio, c.atMs));
        }
        {   // 200 note-offs at random phases (loop + release trigger), then the decaying handoff on the piano
            for (int which = 0; which < 2; ++which)
            {
                OrganicEngine e; e.prepare (kSR, 512); e.setInstrument (which == 0 ? sine : piano);
                Rec r; auto p = P0(); p.noise = 0.f;
                for (int i = 0; i < 200; ++i)
                {
                    e.noteOn (which == 0 ? 48 : 43, 0.8f, 1, kNoDet, (uint32_t) i);
                    run (e, p, r, 2400 + rnd.nextInt (12000), 64 + rnd.nextInt (448));
                    e.noteOff (false);
                    run (e, p, r, 4800 + rnd.nextInt (4800), 64 + rnd.nextInt (448));
                }
                const auto c = an::clicks (mono (r));
                bar (which == 0 ? "no clicks: 200 note-offs at random phases (sine)" : "no clicks: 200 note-offs, decaying handoff (piano)",
                     c.relDb <= -60.0 || c.localRatio <= 1.5, fmt ("HP residual %.1f dB (local ratio %.2f, worst at %.0f ms)", c.relDb, c.localRatio, c.atMs));
            }
        }
        {   // 100 chokes: two voices alternate in choke group 1
            OrganicEngine a, b; a.prepare (kSR, 512); b.prepare (kSR, 512); a.setInstrument (piano); b.setInstrument (piano);
            Rec r;
            std::vector<float> la (256), ra (256);
            int choked = 0;
            for (int i = 0; i < 100; ++i)
            {
                auto& on = (i % 2) ? b : a; auto& other = (i % 2) ? a : b;
                on.noteOn (50, 0.8f, 1, kNoDet, (uint32_t) i);
                const int frames = 4800 + rnd.nextInt (9600);
                for (int t = 0; t < frames; t += 256)
                {
                    std::fill (la.begin(), la.end(), 0.f); std::fill (ra.begin(), ra.end(), 0.f);
                    auto pc = P0(); pc.noise = 0.f;              // the hammer-noise region is broadband by design
                    a.render (pc, 0.f, la.data(), ra.data(), 256); b.render (pc, 0.f, la.data(), ra.data(), 256);
                    r.L.insert (r.L.end(), la.begin(), la.end()); r.R.insert (r.R.end(), ra.begin(), ra.end());
                }
                if (i > 0 && ! other.isActive()) ++choked;
            }
            const auto c = an::clicks (mono (r));
            bar ("no clicks: 100 chokes (5 ms fade)", (c.relDb <= -60.0 || c.localRatio <= 1.5) && choked == 99,
                 fmt ("HP residual %.1f dB (local ratio %.2f, worst at %.0f ms) · choked voices %d/99", c.relDb, c.localRatio, c.atMs, choked));
        }
        {   // 20 instrument swaps mid-note — test.layers ↔ test.rr (test.sine's samples carry a baked 1 ms LINEAR onset
            // ramp whose corner at frame 48 alone measures −55.6 dB HP at every note-on; that is the fixture, not a swap)
            OrganicEngine e; e.prepare (kSR, 512); e.setInstrument (rr);
            Rec r;
            for (int i = 0; i < 20; ++i)
            {
                e.noteOn (48, 0.8f, 1, kNoDet, (uint32_t) i);
                run (e, P0(), r, 4000 + rnd.nextInt (8000), 64 + rnd.nextInt (448));
                e.setInstrument ((i % 2) ? rr : layers);
                run (e, P0(), r, 1000 + rnd.nextInt (2000), 64 + rnd.nextInt (448));
            }
            const auto c = an::clicks (mono (r));
            bar ("no clicks: 20 instrument swaps mid-note (5 ms fade)", c.relDb <= -60.0 || c.localRatio <= 1.5, fmt ("HP residual %.1f dB (local ratio %.2f, worst at %.0f ms)", c.relDb, c.localRatio, c.atMs));
        }
    }

    // ── 5. Each knob does its job ──────────────────────────────────────────────────────────────────
    {
        // Tone: white-noise zone (ratio 1), vel 0.6 → the velocity term is 0
        {
            double cen[5], hf[5]; const float tones[5] = { -1, -0.5f, 0, 0.5f, 1 };
            for (int i = 0; i < 5; ++i)
            {
                OrganicEngine e; e.prepare (kSR, 512); e.setInstrument (norr);
                auto p = P0(); p.tone = tones[i];
                e.noteOn (6, 0.6f, 1, kNoDet, 3u);
                Rec r; run (e, p, r, 48000, 512);
                const auto m = mono (r);
                cen[i] = an::centroid (m, 24000); hf[i] = an::band (m, 24000, 19000, 20000);
            }
            bool monoC = true; for (int i = 1; i < 5; ++i) monoC &= cen[i] > cen[i - 1];
            const double up = 10 * std::log10 (hf[4] / hf[2]), dn = 10 * std::log10 (hf[0] / hf[2]);
            bar ("Tone: centroid monotonic, ±9 dB at 20 kHz", monoC && std::abs (up - 9) <= 1 && std::abs (dn + 9) <= 1,
                 fmt ("centroid %.0f/%.0f/%.0f/%.0f/%.0f Hz · 19-20k: %+.2f dB (+1) %+.2f dB (−1)", cen[0], cen[1], cen[2], cen[3], cen[4], up, dn));
            // velocity-aware: 4·(vel − 0.6)·Velocity dB at 20 kHz → vel 0.2 vs 1.0 at Velocity 1 = 3.2 dB
            auto hfAt = [&] (float vel) {
                OrganicEngine e; e.prepare (kSR, 512); e.setInstrument (norr);
                auto p = P0(); p.velo = 1.f;
                e.noteOn (6, vel, 1, kNoDet, 3u);
                Rec r; run (e, p, r, 48000, 512);
                const auto m = mono (r);
                return 10 * std::log10 (an::band (m, 24000, 19000, 20000) / an::band (m, 24000, 650, 750));   // re the pivot band
            };
            const double dv = hfAt (1.f) - hfAt (0.2f);
            bar ("Tone: velocity → brightness (+3.2 dB at 20 kHz)", std::abs (dv - 3.2) <= 0.4, fmt ("HF re pivot, vel 1.0 vs 0.2 at Velocity 1: %+.2f dB", dv));
        }
        // Body: formant moves, pitch does not
        {
            double cen[5], perr = 0; const float bodies[5] = { -1, -0.5f, 0, 0.5f, 1 };
            for (int i = 0; i < 5; ++i)
            {
                OrganicEngine e; e.prepare (kSR, 512); e.setInstrument (norr);
                auto p = P0(); p.body = bodies[i];
                e.noteOn (60, 0.8f, 1, kNoDet, 3u);
                Rec r; run (e, p, r, 48000, 512);
                const auto m = mono (r);
                cen[i] = an::centroid (m, 16000);
                perr = std::max (perr, std::abs (an::cents (an::freq (m, 9600, 16384, 4096, mtof (60)), mtof (60))));
            }
            bool monoC = true; int distinct = 1; for (int i = 1; i < 5; ++i) { monoC &= cen[i] <= cen[i - 1] * 1.005; distinct += cen[i] < cen[i - 1] * 0.97; }
            bar ("Body: centroid falls −1→+1, pitch unchanged", monoC && distinct >= 3 && perr <= 3.0 && cen[0] / cen[4] > 1.3,
                 fmt ("centroid %.0f/%.0f/%.0f/%.0f/%.0f Hz · pitch |err| max %.3f cents", cen[0], cen[1], cen[2], cen[3], cen[4], perr));
        }
        // Attack: onset time + first-12 ms energy (piano: 20 ms of air before the transient)
        {
            double onset[3], e12[3]; const float atk[3] = { -1, 0, 1 }; double thr = 0;
            for (int pass = 0; pass < 2; ++pass)
                for (int i = 0; i < 3; ++i)
                {
                    OrganicEngine e; e.prepare (kSR, 512); e.setInstrument (piano);
                    auto p = P0(); p.attack = atk[i]; p.noise = 0.f;
                    e.noteOn (43, 0.8f, 1, kNoDet, 3u);
                    Rec r; run (e, p, r, 24000, 64);
                    const auto m = mono (r);
                    if (pass == 0 && i == 1) thr = 0.063 * an::peak (m, 0, 9600);
                    if (pass == 1)
                    {
                        int64_t k = 0; while (k < (int64_t) m.size() && std::abs (m[(size_t) k]) < thr) ++k;
                        onset[i] = 1000.0 * (double) k / kSR;
                        e12[i] = an::rms (m, 0, 576);
                    }
                }
            bar ("Attack: Tight earlier+hotter, Gentle later+softer", onset[2] < onset[1] && onset[1] < onset[0] && e12[2] > e12[1] && e12[1] > e12[0],
                 fmt ("onset %.1f / %.1f / %.1f ms · first-12ms RMS %.1f / %.1f / %.1f dB (Gentle/Natural/Tight)",
                      onset[0], onset[1], onset[2], an::db (e12[0]), an::db (e12[1]), an::db (e12[2])));
        }
        // Human: per-note spread matches the ranges (detune ±4¢, level ±1.5 dB, timing 0..12 ms, tone ±1.5 dB).
        // test.layers vel 60 = one zone, one layer, no RR (fake RR finds no neighbour): fundamental 130.8 Hz + marker
        // k5 at 654 Hz ≈ the 700 Hz tilt pivot → level = the marker, tone = marker/fundamental (the tilt's LF side).
        {
            std::vector<double> det, lvl, tim, ton;
            auto one = [&] (float human, uint32_t seed, double& c, double& l, double& t, double& tn) {
                OrganicEngine e; e.prepare (kSR, 512); e.setInstrument (layers);
                auto p = P0(); p.human = human; p.velo = 0.f;
                e.noteOn (48, 60 / 127.f, 1, kNoDet, seed);
                Rec r; run (e, p, r, 36000, 64);
                const auto m = mono (r);
                const double f = an::freq (m, 9600, 16384, 4096, mtof (48));
                c = an::cents (f, mtof (48));
                const double a5 = an::amp (m, 9600, 16384, 5 * f), a1 = an::amp (m, 9600, 16384, f);
                l = an::db (a5); tn = an::db (a5 / a1);
                int64_t k = 0; while (k < (int64_t) m.size() && std::abs (m[(size_t) k]) < 0.05) ++k;
                t = 1000.0 * (double) k / kSR;
            };
            double c0, l0, t0, n0; one (0.f, 1u, c0, l0, t0, n0);
            bool zeroSame = true;
            for (uint32_t s2 = 2; s2 < 6; ++s2) { double cc, ll, tt, nn; one (0.f, s2, cc, ll, tt, nn); zeroSame &= cc == c0 && ll == l0 && tt == t0 && nn == n0; }
            for (uint32_t s2 = 1; s2 <= 60; ++s2)
            {
                double c, l, t, tn; one (1.f, s2 * 977u, c, l, t, tn);
                det.push_back (c); lvl.push_back (l - l0); tim.push_back (t - t0); ton.push_back (tn - n0);
            }
            auto mm = [] (const std::vector<double>& v, double& lo, double& hi) { lo = *std::min_element (v.begin(), v.end()); hi = *std::max_element (v.begin(), v.end()); };
            double dl, dh, ll, lh, tl, th, ol, oh; mm (det, dl, dh); mm (lvl, ll, lh); mm (tim, tl, th); mm (ton, ol, oh);
            const bool ok = zeroSame && dl >= -4.2 && dh <= 4.2 && dh - dl > 5.0 && ll >= -1.7 && lh <= 1.7 && lh - ll > 2.0
                            && tl >= -0.5 && th <= 12.5 && th > 8.0 && ol >= -1.7 && oh <= 1.7 && oh - ol > 1.5;
            bar ("Human 1: per-note spread within ranges (60 notes)", ok,
                 fmt ("detune [%+.2f,%+.2f]¢ · level [%+.2f,%+.2f] dB · timing [%.1f,%.1f] ms · tone [%+.2f,%+.2f] dB · Human 0 identical %s",
                      dl, dh, ll, lh, tl, th, ol, oh, zeroSame ? "yes" : "NO"));
        }
        // Release: release-region level (k11 marker after note-off) + rt_decay
        {
            auto relLevel = [&] (float rel) {
                OrganicEngine e; e.prepare (kSR, 512); e.setInstrument (sine);
                auto p = P0(); p.release = rel;
                e.noteOn (48, 0.8f, 1, kNoDet, 1u);
                Rec r; run (e, p, r, 24000, 512); e.noteOff (false); run (e, p, r, 12000, 512);
                return an::amp (mono (r), 24000 + 960, 8192, 11 * mtof (48));
            };
            const double r0 = relLevel (0.f), r5 = relLevel (0.5f), r1 = relLevel (1.f);
            auto rtd = [&] (int64_t hold) {
                OrganicEngine e; e.prepare (kSR, 512); e.setInstrument (piano);
                auto p = P0(); p.noise = 0.f;
                e.noteOn (66, 0.8f, 1, kNoDet, 1u);             // release root 60 → keytracked to 1414 Hz
                Rec r; run (e, p, r, hold, 512); e.noteOff (false); run (e, p, r, 9600, 512);
                return an::amp (mono (r), hold + 240, 4096, 1000.0 * std::pow (2.0, 6.0 / 12.0));
            };
            const double h1 = rtd (9600), h2 = rtd (9600 + 96000);
            bar ("Release: region level 0 / authored / +6 dB, rt_decay", an::db (r0 / r5) < -60 && std::abs (an::db (r1 / r5) - 6.0) <= 0.5 && std::abs (an::db (h2 / h1) + 6.0) <= 0.6,
                 fmt ("0 → %.1f dB, 1 → %+.2f dB re 0.5 · rt_decay 3 dB/s over 2 s: %+.2f dB", an::db (r0 / r5), an::db (r1 / r5), an::db (h2 / h1)));
        }
        // Noise: noise-region level by the difference method (Human 0 → the rest is identical)
        {
            auto rnd = [&] (float nz, const std::shared_ptr<const OrganicInstrument>& I) {
                OrganicEngine e; e.prepare (kSR, 512); e.setInstrument (I);
                auto p = P0(); p.noise = nz;
                e.noteOn (43, 0.8f, 1, kNoDet, 1u);
                Rec r; run (e, p, r, 9600, 512); e.noteOff (false); run (e, p, r, 9600, 512); return mono (r);
            };
            const auto b = rnd (0.f, piano), h = rnd (0.5f, piano), f = rnd (1.f, piano);
            an::Buf dh (b.size()), df (b.size()); for (size_t i = 0; i < b.size(); ++i) { dh[i] = h[i] - b[i]; df[i] = f[i] - b[i]; }
            const double eh = an::rms (dh, 9600, 4800), ef = an::rms (df, 9600, 4800);
            const double pre = an::rms (dh, 0, 9600);                  // nothing before the note-off
            const auto s0 = rnd (0.f, sine), s1 = rnd (1.f, sine);
            bar ("Noise: key-off noise level (0 / authored / +6 dB)", eh > 1e-3 && pre == 0.0 && std::abs (an::db (ef / eh) - 6.0) <= 0.5 && s0 == s1,
                 fmt ("authored key-off noise %.1f dBFS (before note-off: %s), knob 1 %+.2f dB re 0.5 · no-noise instrument unaffected: %s",
                      an::db (eh), pre == 0.0 ? "silent" : "LEAK", an::db (ef / eh), s0 == s1 ? "yes" : "NO"));
        }
        // Sustain: at 1 the level after 10 s ≥ −6 dB of the level at 1 s; 0.5 halves the decay rate; 0 decays + retires
        {
            auto lvl = [&] (float s2, double& at1, double& at4, double& at10, bool& active) {
                OrganicEngine e; e.prepare (kSR, 512); e.setInstrument (piano);
                auto p = P0(); p.sustain = s2; p.noise = 0.f;
                e.noteOn (43, 0.8f, 1, kNoDet, 1u);
                Rec r; run (e, p, r, 10 * 48000 + 4800, 512);
                const auto m = mono (r);
                at1 = an::db (an::rms (m, 48000, 4800)); at4 = an::db (an::rms (m, 3 * 48000, 4800));
                at10 = an::db (an::rms (m, 10 * 48000, 4800)); active = e.isActive();
            };
            double a1, a4, a10, b1, b4, b10, c1, c4, c10; bool act1, act0, acth;
            lvl (1.f, a1, a4, a10, act1); lvl (0.5f, c1, c4, c10, acth); lvl (0.f, b1, b4, b10, act0);
            const double r0 = (b1 - b4) / 2.0, r5 = (c1 - c4) / 2.0;
            bar ("Sustain: 1 holds, 0.5 halves the decay, 0 decays", a10 >= a1 - 6.0 && r5 / r0 > 0.35 && r5 / r0 < 0.65 && ! act0 && act1,
                 fmt ("s=1: %.1f dB @1s → %.1f @10s · decay 1→3 s: s=0 %.1f dB/s, s=0.5 %.1f dB/s · s=0 retired %s",
                      a1, a10, r0, r5, act0 ? "no" : "yes"));
        }
        // Velocity: level spread (test.layers played at key 76 → the fundamental sits at 659 Hz ≈ the tilt pivot,
        // so velocity→brightness does not leak into the level reading; vel 0.2 and 1.0 are both on a layer plateau)
        {
            auto spread = [&] (float velo) {
                double lv[2]; const float v[2] = { 0.2f, 1.0f };
                for (int i = 0; i < 2; ++i)
                {
                    OrganicEngine e; e.prepare (kSR, 512); e.setInstrument (layers);
                    auto p = P0(); p.velo = velo;
                    e.noteOn (76, v[i], 1, kNoDet, 1u);
                    Rec r; run (e, p, r, 24000, 512);
                    lv[i] = an::db (an::amp (mono (r), 9600, 8192, mtof (76)));
                }
                return lv[1] - lv[0];
            };
            const double s0 = spread (0.f), s5 = spread (0.5f), s1 = spread (1.f);
            const double want = 20 * std::log10 (127.0 / 25.0);
            bar ("Velocity: 0 = flat, 1 = authored curve", std::abs (s0) < 0.1 && s1 > s5 && s5 > s0 && std::abs (s1 - want) < 0.3,
                 fmt ("vel 0.2→1.0 spread: %.2f / %.2f / %.2f dB (knob 0 / 0.5 / 1; curve says %.2f)", s0, s5, s1, want));
        }
        // Image: side/mid ratio
        {
            auto sm = [&] (float img) {
                OrganicEngine e; e.prepare (kSR, 512); e.setInstrument (norr);
                auto p = P0(); p.image = img;
                e.noteOn (60, 0.8f, 1, kNoDet, 3u);
                Rec r; run (e, p, r, 24000, 512);
                double ms = 0, ss = 0;
                for (size_t i = 9600; i < r.L.size(); ++i) { const double m = r.L[i] + r.R[i], s = r.L[i] - r.R[i]; ms += m * m; ss += s * s; }
                return 10 * std::log10 (ss / ms + 1e-30);
            };
            const double i0 = sm (0.f), i1 = sm (1.f), i15 = sm (1.5f);
            bar ("Image: side/mid 0 = mono, 1.5 = +3.5 dB side", i0 < -100 && std::abs ((i15 - i1) - 20 * std::log10 (1.5)) < 0.1,
                 fmt ("side/mid %.1f / %.2f / %.2f dB (image 0 / 1 / 1.5)", i0, i1, i15));
        }
    }

    // ── 5b. Pedal, articulations, Gentle's softer layer, key tracking, readLevel ──────────────────
    {
        // pedal: noteOff(pedalDown) holds (no release trigger), pedal up releases (release k11 appears)
        {
            OrganicEngine e; e.prepare (kSR, 512); e.setInstrument (sine);
            e.noteOn (48, 0.8f, 1, kNoDet, 1u);
            Rec r; run (e, P0(), r, 9600, 512);
            e.pedal (true); e.noteOff (true);  run (e, P0(), r, 9600, 512);
            e.pedal (false);                    run (e, P0(), r, 9600, 512);
            const auto m = mono (r);
            const double held = an::amp (m, 9600 + 960, 4096, 11 * mtof (48)), up = an::amp (m, 19200 + 960, 4096, 11 * mtof (48));
            const double body = an::amp (m, 9600 + 960, 4096, mtof (48));
            bar ("Pedal: held note sustains, pedal-up releases", an::db (held / up) < -40 && an::db (body) > -20,
                 fmt ("release marker while pedal held %.1f dB re after pedal-up · note level while held %.1f dBFS", an::db (held / up), an::db (body)));
        }
        // articulations: artic 1 = the Staccato region (marker k11), out-of-range clamps
        {
            auto mk = [&] (int artic, double& k5, double& k11) {
                OrganicEngine e; e.prepare (kSR, 512); e.setInstrument (layers);
                auto p = P0(); p.artic = artic; p.velo = 0.f;
                e.noteOn (48, 60 / 127.f, 1, kNoDet, 1u);
                Rec r; run (e, p, r, 14400, 512);
                k5 = an::amp (mono (r), 4800, 8192, 5 * mtof (48)); k11 = an::amp (mono (r), 4800, 8192, 11 * mtof (48));
            };
            double a5, a11, b5, b11, c5, c11;
            mk (0, a5, a11); mk (1, b5, b11); mk (7, c5, c11);
            bar ("Articulations: artic selects its region set", a5 > 0.02 && a11 < 1e-4 && b11 > 0.02 && b5 < 1e-4 && c11 > 0.02,
                 fmt ("artic 0: k5 %.3f k11 %.5f · artic 1: k5 %.5f k11 %.3f · artic 7 (clamped to 1): k11 %.3f", a5, a11, b5, b11, c11));
        }
        // Gentle: the first 60 ms lean on the next-softer layer (vel 100 = L3 plateau → L2 marker early, gone later)
        {
            // the L2/L3 marker RATIO in the first 40 ms cancels Gentle's own 150 ms fade-in
            auto at = [&] (float attack, double& earlyRatio, double& late2, double& late3) {
                OrganicEngine e; e.prepare (kSR, 512); e.setInstrument (layers);
                auto p = P0(); p.attack = attack; p.velo = 0.f;
                e.noteOn (48, 100 / 127.f, 1, kNoDet, 1u);
                Rec r; run (e, p, r, 19200, 64);
                const auto m = mono (r);
                earlyRatio = an::amp (m, 240, 1680, 5 * mtof (48)) / std::max (1e-12, an::amp (m, 240, 1680, 7 * mtof (48)));
                late2 = an::amp (m, 9600, 4096, 5 * mtof (48)); late3 = an::amp (m, 9600, 4096, 7 * mtof (48));
            };
            double ne, nl, n3, ge, gl, g3;
            at (0.f, ne, nl, n3); at (-1.f, ge, gl, g3);
            bar ("Attack Gentle: first 60 ms from the softer layer", ge > 0.5 && ne < 0.01 && gl < 1e-3 && std::abs (an::db (g3 / n3)) < 0.1,
                 fmt ("L2/L3 marker ratio 5-40 ms: Natural %.3f, Gentle %.2f · after 200 ms: L2 %.5f, L3 %+.2f dB re Natural", ne, ge, gl, an::db (g3 / n3)));
        }
        // Tone key tracking above C6: the same region at the same pitch, key 96 vs key 84 + 1200 cents
        {
            auto ratio = [&] (int key, float pitch) {
                OrganicEngine e; e.prepare (kSR, 512); e.setInstrument (layers);
                auto p = P0(); p.velo = 0.f;
                e.noteOn (key, 60 / 127.f, 1, kNoDet, 1u);
                Rec r; run (e, p, r, 19200, 512, pitch);
                const auto m = mono (r);
                return an::db (an::amp (m, 4800, 8192, 5 * mtof (96)));          // same region, same pitch: absolute level
            };
            const double d = ratio (96, 0.f) - ratio (84, 1200.f);
            bar ("Tone key tracking: −1.5 dB/oct tilt above C6", d < -0.5 && d > -1.6, fmt ("10.4 kHz marker, key C7 vs the same pitch played from C6: %+.2f dB", d));
        }
        // every knob turned while a note holds: no zipper / click (the CLAUDE.md "while turning it" law).
        // Each knob is swept end to end and back in 1 s (per-block params) on a held note; the HP(8 kHz) peak from
        // 0.4 s on (the note start excluded) must stay ≤ −60 dB re the note OR within +1 dB of the same note held
        // STATIC at either end of the knob (the recording's own HF, e.g. the piano fixture's air, is not a zipper).
        {
            const char* names[8] = { "Dynamics", "Tone", "Body", "Human", "Release", "Velocity", "Image", "Sustain" };
            double worstDb = -300; int wi = 0; bool ok = true;
            for (int k = 0; k < 8; ++k)
            {
                auto set = [k] (OrganicParams& q, float u) {
                    switch (k)
                    {
                        case 0: q.dyn = 2.f * u - 1.f; break;      case 1: q.tone = 2.f * u - 1.f; break;
                        case 2: q.body = 2.f * u - 1.f; break;     case 3: q.human = u; break;
                        case 4: q.release = u; break;              case 5: q.velo = u; break;
                        case 6: q.image = 1.5f * u; break;         default: q.sustain = u; break;
                    }
                };
                auto render = [&] (int mode) {                                     // 0/1 static at u, 2 sweep
                    OrganicEngine e; e.prepare (kSR, 512); e.setInstrument (k == 7 ? piano : layers);
                    e.noteOn (k == 7 ? 43 : 31, 0.7f, 1, kNoDet, 1u);
                    Rec r; auto p = P0(); p.noise = 0.f;
                    run (e, p, r, 2 * 48000, 256, 0.f, [&] (OrganicParams& q, int64_t at) {
                        const double t = (double) at / kSR;
                        const float u = mode < 2 ? (float) mode
                                                 : (float) (t < 0.5 ? 0.0 : t < 1.0 ? (t - 0.5) * 2.0 : t < 1.5 ? 1.0 - (t - 1.0) * 2.0 : 0.0);
                        set (q, u);
                    });
                    const auto m = mono (r);
                    return std::pair<double, double> (an::peak (an::highpass (m, 8000.0), 19200, 72000), an::peak (m));
                };
                const auto s0 = render (0), s1 = render (1), sw = render (2);
                const double rel = an::db (sw.first / std::max (s0.second, s1.second));
                const double vsStatic = an::db (sw.first / std::max (s0.first, s1.first));
                const bool pass = rel <= -60.0 || vsStatic <= 1.0;
                ok &= pass;
                const double score = pass ? std::min (rel, -60.0) : rel;
                if (! pass || score > worstDb) { if (! pass || ok) { worstDb = rel; wi = k; } }
                std::printf ("      sweep %-8s HP %.1f dB re note, %+.2f dB re static ends\n", names[k], rel, vsStatic);
            }
            bar ("knob sweeps while held: no zipper (8 knobs)", ok, fmt ("worst %s: HP residual %.1f dB re note", names[wi], worstDb));
        }
        // isActive / readLevel follow the sound
        {
            OrganicEngine e; e.prepare (kSR, 512); e.setInstrument (piano);
            const bool idle0 = ! e.isActive() && e.readLevel() == 0.f;
            e.noteOn (43, 0.8f, 1, kNoDet, 1u);
            Rec r; auto p = P0(); run (e, p, r, 4800, 512);
            const float lv = e.readLevel(); const bool act = e.isActive();
            e.noteOff (false); run (e, p, r, 2 * 48000, 512);
            bar ("isActive / readLevel follow the sound", idle0 && act && lv > 0.05f && ! e.isActive() && e.readLevel() == 0.f,
                 fmt ("idle %s · playing active %s level %.3f · after release+tail active %s level %.3f",
                      idle0 ? "yes" : "NO", act ? "yes" : "no", lv, e.isActive() ? "yes" : "no", e.readLevel()));
        }
    }

    // ── 6. Players (Ensemble) + the 48-region cap ──────────────────────────────────────────────────
    {
        rr->resetPerformanceState();
        OrganicEngine e; e.prepare (kSR, 512); e.setInstrument (rr);
        const float det[4] = { 0, -10, 10, -20 };
        e.noteOn (48, 0.8f, 4, det, 5u);
        Rec r; run (e, P0(), r, 9600, 64);
        const auto m = mono (r);
        // player k starts k·7 ms·D later (D = 20/25): measure where each marker first appears
        const int mk[4] = { 3, 5, 7, 9 };
        double first[4];
        for (int k = 0; k < 4; ++k)
        {
            first[k] = -1;
            for (int64_t s = 0; s + 480 < 4800; s += 24)
                if (an::amp (m, s, 480, mk[k] * mtof (48)) > 0.02) { first[k] = 1000.0 * s / kSR; break; }
        }
        bar ("Ensemble: 4 players = 4 RR picks, k·7 ms·D timing", first[0] >= 0 && first[1] > first[0] && first[2] > first[1] && first[3] > first[2],
             fmt ("markers k3/k5/k7/k9 first seen at %.1f / %.1f / %.1f / %.1f ms (expected 0 / 5.6 / 11.2 / 16.8 + window)", first[0], first[1], first[2], first[3]));

        // cap: 16 players × (attack + noise) per note, plus 16 release regions per note-off, retriggered fast
        OrganicEngine c; c.prepare (kSR, 512); c.setInstrument (piano);
        float det16[16] = {};                                  // D = 0: all 16 players start together
        int maxLive = 0, maxR = 0; const int st0 = organics_debug::steals();
        std::vector<float> l (256), rr2 (256);
        Rec cr;
        for (int i = 0; i < 12; ++i)
        {
            c.noteOn (43 + (i % 3), 0.8f, 16, det16, (uint32_t) i);
            for (int b2 = 0; b2 < 6; ++b2)
            {
                std::fill (l.begin(), l.end(), 0.f); std::fill (rr2.begin(), rr2.end(), 0.f);
                c.render (P0(), 0.f, l.data(), rr2.data(), 256);
                maxLive = std::max (maxLive, organics_debug::lastLiveReaders()); maxR = std::max (maxR, organics_debug::lastRenderReaders());
                cr.L.insert (cr.L.end(), l.begin(), l.end()); cr.R.insert (cr.R.end(), rr2.begin(), rr2.end());
                if (b2 == 3) c.noteOff (false);
            }
        }
        const int st = organics_debug::steals() - st0;
        // the same on tonal content (test.sine: looped attacks + 0.4 s release tails piling up) → the steal-fade is click-free
        OrganicEngine sc; sc.prepare (kSR, 512); sc.setInstrument (sine);
        float spread16[16]; for (int k = 0; k < 16; ++k) spread16[k] = ((float) k - 7.5f) * 1.5f;   // a real ensemble spread
        Rec sr2; int stLive = 0; const int sst0 = organics_debug::steals();
        for (int i = 0; i < 40; ++i)
        {
            sc.noteOn (48, 40 / 127.f, 16, spread16, (uint32_t) i);
            for (int b2 = 0; b2 < 6; ++b2)
            {
                std::fill (l.begin(), l.end(), 0.f); std::fill (rr2.begin(), rr2.end(), 0.f);
                sc.render (P0(), 0.f, l.data(), rr2.data(), 256);
                stLive = std::max (stLive, organics_debug::lastLiveReaders());
                sr2.L.insert (sr2.L.end(), l.begin(), l.end()); sr2.R.insert (sr2.R.end(), rr2.begin(), rr2.end());
                if (b2 == 3) sc.noteOff (false);
            }
        }
        const int sst = organics_debug::steals() - sst0;
        const auto ck = an::clicks (mono (sr2));
        bar ("48-region cap (piano stress: 16 players, 32 ms retriggers)", maxLive <= 48 && st > 0 && maxR > 48,
             fmt ("max live %d (cap 48) · max rendered incl. fading %d · steal-fades %d", maxLive, maxR, st));
        bar ("48-region cap: steal-fade is click-free (sine tails)", stLive <= 48 && sst > 0 && (ck.relDb <= -60.0 || ck.localRatio <= 1.5),
             fmt ("max live %d · steal-fades %d · HP residual %.1f dB (local ratio %.2f, worst at %.0f ms)", stLive, sst, ck.relDb, ck.localRatio, ck.atMs));
    }

    // ── 7. CPU (µs per region per 512-frame block) ────────────────────────────────────────────────
    {
        auto bench = [&] (const std::shared_ptr<const OrganicInstrument>& I, int players, double& usPerBlock, double& usPerRegion, int& regions) {
            std::vector<std::unique_ptr<OrganicEngine>> v;
            const int chord[8] = { 48, 52, 55, 59, 62, 65, 69, 72 };
            float det[16]; for (int k = 0; k < 16; ++k) det[k] = (float) ((k % 2 ? 1 : -1) * (k + 1) * 1.5);
            for (int i = 0; i < 8; ++i)
            {
                v.push_back (std::make_unique<OrganicEngine>()); v.back()->prepare (kSR, 512); v.back()->setInstrument (I);
                v.back()->noteOn (chord[i], 0.8f, players, det, (uint32_t) i + 1);
            }
            std::vector<float> l (512), r (512);
            auto p = P0(); p.tone = 0.3f; p.noise = 0.f;
            for (int b = 0; b < 20; ++b) for (auto& e : v) e->render (p, 0.f, l.data(), r.data(), 512);   // warm-up, all players started
            // best of 9 runs (other processes share this machine; the minimum is the engine's own cost)
            const int blocks = 300;
            usPerBlock = 1e30;
            for (int rep = 0; rep < 9; ++rep)
            {
                int64_t regionBlocks = 0;
                const auto t0 = std::chrono::steady_clock::now();
                for (int b = 0; b < blocks; ++b)
                    for (auto& e : v) { e->render (p, 0.f, l.data(), r.data(), 512); regionBlocks += organics_debug::lastRenderReaders(); }
                const double us = std::chrono::duration<double, std::micro> (std::chrono::steady_clock::now() - t0).count();
                if (us / blocks < usPerBlock) { usPerBlock = us / blocks; usPerRegion = us / (double) std::max<int64_t> (1, regionBlocks); regions = (int) (regionBlocks / blocks); }
            }
        };
        double b1, r1, b7, r7, bm, rm; int n1, n7, nm;
        bench (norr, 1, b1, r1, n1); bench (norr, 7, b7, r7, n7); bench (sine, 1, bm, rm, nm);
        bar ("CPU: engine core ≤ 4 µs per region per 512 block", r1 <= 4.0 && r7 <= 4.0 && rm <= 4.0,
             fmt ("stereo %.2f µs/region (players 1), %.2f (players 7) · mono %.2f", r1, r7, rm));
        std::printf ("      8-note chord, stereo loop + tilt: players 1 → %.1f µs/block (%d regions) · players 7 → %.1f µs/block (%d regions) · mono players 1 → %.1f µs (%d)\n",
                     b1, n1, b7, n7, bm, nm);
    }

    // ── 8. Memory / threads ────────────────────────────────────────────────────────────────────────
    {
        // zero allocation in render across on/off/pedal/choke/swap/steal/dyn sweeps
        OrganicEngine a, b; a.prepare (kSR, 512); b.prepare (kSR, 512); a.setInstrument (piano); b.setInstrument (layers);
        std::vector<float> l (512), r (512);
        float det[16]; for (int k = 0; k < 16; ++k) det[k] = (float) k;
        auto swapA = sine, swapB = layers;
        gAllocs = 0; gCount = true;
        auto p = P0(); p.human = 0.7f; p.sustain = 0.5f;
        for (int i = 0; i < 400; ++i)
        {
            p.dyn = (float) std::sin (i * 0.1); p.body = (float) std::cos (i * 0.07); p.tone = (float) std::sin (i * 0.05); p.image = 1.2f;
            if (i % 7 == 0) a.noteOn (40 + i % 30, 0.7f, 1 + i % 16, det, (uint32_t) i);
            if (i % 5 == 0) b.noteOn (48, (float) (i % 127) / 127.f, 1 + i % 8, det, (uint32_t) i);
            if (i % 11 == 3) a.noteOff (i % 2 == 0);
            if (i % 13 == 4) a.pedal (i % 3 == 0);
            if (i % 17 == 5) b.kill();
            if (i % 29 == 6) b.setInstrument ((i / 29) % 2 ? swapA : swapB);
            if (i % 9 == 1) a.noteOn (50 + i % 4, 0.9f, 2, det, (uint32_t) i);   // choke zone
            a.setNonRealtime (i % 50 > 40);
            a.render (p, 0.f, l.data(), r.data(), 512); b.render (p, 0.f, l.data(), r.data(), 512);
        }
        gCount = false;
        bar ("memory: zero allocations in render", gAllocs.load() == 0, fmt ("allocations=%lld over 800 renders (notes, pedal, chokes, swaps, steals, sinc)", (long long) gAllocs.load()));
    }
    {
        // the cache frees ~5 s after the last user lets go (and not before)
        auto& L = OrganicsLibrary::get();
        auto inst = load ("test.rr");
        const int before = L.residentCount();
        {
            OrganicEngine e; e.prepare (kSR, 512); e.setInstrument (inst);
            std::vector<float> l (512), r (512);
            e.noteOn (48, 0.8f, 1, kNoDet, 1u);
            for (int i = 0; i < 10; ++i) e.render (P0(), 0.f, l.data(), r.data(), 512);
            e.setInstrument (nullptr);                   // audio thread lets go → deferred-release queue
            for (int i = 0; i < 10; ++i) e.render (P0(), 0.f, l.data(), r.data(), 512);
        }
        inst.reset(); rr.reset();
        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        double freedAt = -1;
        while (juce::Time::getMillisecondCounterHiRes() - t0 < 9000.0)
        {
            juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
            if (L.residentCount() < before) { freedAt = (juce::Time::getMillisecondCounterHiRes() - t0) / 1000.0; break; }
        }
        bar ("memory: cache frees 5 s after the last user", freedAt >= 4.9 && freedAt <= 6.0,
             fmt ("resident %d → %d, freed after %.2f s", before, L.residentCount(), freedAt));
    }
    {
        // missing / corrupt instruments are safe
        auto missing = load ("does.not.exist");
        auto bad = load ("../etc");
        auto tmp = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("organics_corrupt_" + juce::String (juce::Random::getSystemRandom().nextInt (1 << 30)));
        tmp.getChildFile ("broken.json").createDirectory();
        tmp.getChildFile ("broken.json").getChildFile ("map.json").replaceWithText ("{ this is not json");
        tmp.getChildFile ("broken.flac").getChildFile ("samples").createDirectory();
        tmp.getChildFile ("broken.flac").getChildFile ("samples").getChildFile ("0001.flac").replaceWithText ("garbage");
        tmp.getChildFile ("broken.flac").getChildFile ("map.json").replaceWithText (
            R"({"torg":1,"id":"broken.flac","samples":["0001.flac"],"regions":[{"smp":0,"kind":"attack","lk":0,"hk":127,"lv":1,"hv":127,"root":60,"end":100}]})");
        tmp.getChildFile ("broken.range").getChildFile ("samples").createDirectory();
        juce::File (fixRoot.getChildFile ("test.sine/samples/0001.flac")).copyFileTo (tmp.getChildFile ("broken.range/samples/0001.flac"));
        tmp.getChildFile ("broken.range").getChildFile ("map.json").replaceWithText (
            R"({"torg":1,"id":"broken.range","samples":["0001.flac"],"regions":[{"smp":5,"kind":"attack"}]})");
        setEnv ("TERRAIN_ORGANICS_DIR", tmp.getFullPathName().toRawUTF8());
        auto c1 = load ("broken.json"), c2 = load ("broken.flac"), c3 = load ("broken.range");
        setEnv ("TERRAIN_ORGANICS_DIR", fixRoot.getFullPathName().toRawUTF8());
        tmp.deleteRecursively();
        OrganicEngine e; e.prepare (kSR, 512); e.setInstrument (nullptr);
        e.noteOn (60, 0.8f, 4, kNoDet, 1u);
        Rec r; run (e, P0(), r, 4800, 512);
        e.noteOff (false); e.pedal (true); e.pedal (false); e.kill();
        run (e, P0(), r, 4800, 512);
        const bool silent = an::peak (r.L) == 0.0 && ! e.isActive();
        bar ("missing / corrupt instrument is safe", ! missing && ! bad && ! c1 && ! c2 && ! c3 && silent,
             fmt ("missing→%s, ../→%s, bad json→%s, bad flac→%s, bad smp→%s, null engine silent %s",
                  missing ? "ptr" : "null", bad ? "ptr" : "null", c1 ? "ptr" : "null", c2 ? "ptr" : "null", c3 ? "ptr" : "null", silent ? "yes" : "NO"));
    }

    // ── 9. Real data: Agent A's compiled library (runs when <compiledRoot> is given and exists) ──────
    if (argc >= 3 && juce::File (juce::File::getCurrentWorkingDirectory().getChildFile (argv[2])).isDirectory())
    {
        const auto realRoot = juce::File::getCurrentWorkingDirectory().getChildFile (argv[2]);
        std::printf ("── real data: %s ──\n", realRoot.getFullPathName().toRawUTF8());
        setEnv ("TERRAIN_ORGANICS_DIR", realRoot.getFullPathName().toRawUTF8());
        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        auto sal = load ("salamander.grand.v3");
        const double tSal = (juce::Time::getMillisecondCounterHiRes() - t0) / 1000.0;
        auto vio = load ("vsco2.strings.violin-section"), solo = load ("vsco2.strings.solo-violin");
        OrganicsLibrary::get().rescan();
        double idxMB = 0;
        if (auto* a = OrganicsLibrary::get().index().getArray())
            for (auto& e : *a) if (e["id"].toString() == "salamander.grand.v3") idxMB = (double) e["sizeMB"];
        setEnv ("TERRAIN_ORGANICS_DIR", fixRoot.getFullPathName().toRawUTF8());
        OrganicsLibrary::get().rescan();
        bar ("real: salamander / violin-section / solo-violin load", sal && vio && solo,
             fmt ("salamander %zu regions, %d ch, %.1f MB resident (index sizeMB %.1f), loaded in %.2f s", sal ? sal->regions.size() : 0,
                  sal ? sal->samples[0].channels : 0, sal ? sal->bytes / 1048576.0 : 0.0, idxMB, tSal));
        if (sal && vio && solo)
        {
            // pitch: FFT peak (zero-padded, parabolic) near the expected f0 — robust to vibrato and weak fundamentals
            auto pitchScan = [&] (const std::shared_ptr<const OrganicInstrument>& I, int k0, int k1, int step, double& worst, int& wk, double& mean) {
                worst = 0; wk = 0; mean = 0; int cnt = 0;
                for (int key = k0; key <= k1; key += step)
                {
                    OrganicEngine e; e.prepare (kSR, 512); e.setInstrument (I);
                    auto p = P0(); p.velo = 0.f;
                    e.noteOn (key, 0.6f, 1, kNoDet, 1u);
                    Rec r; run (e, p, r, 48000 + 32768, 512);
                    if (an::rms (mono (r), 24000, 32768) < 1e-5) continue;          // outside the instrument's range
                    const double c = an::cents (an::peakFreq (mono (r), 24000, 32768, mtof (key)), mtof (key));
                    mean += c; ++cnt;
                    if (std::abs (c) > std::abs (worst)) { worst = c; wk = key; }
                }
                mean /= std::max (1, cnt);
            };
            // (a) the ENGINE's repitch: each zone's sample at its root vs at the other keys of its zone (same recording,
            //     so vibrato and the recording's own tuning cancel) → must be exactly 100 ¢ per semitone
            auto repitch = [&] (const std::shared_ptr<const OrganicInstrument>& I, int artic, double& worst, int& wk, int& zones) {
                worst = 0; wk = 0; zones = 0;
                // the window is TIME-ALIGNED to the same stretch of the recording: a note repitched by d semitones
                // reads the sample 2^(d/12) faster, so its window starts and lasts 2^(-d/12) as long in output time
                auto f0At = [&] (int key, int root, int ref) {
                    OrganicEngine e; e.prepare (kSR, 512); e.setInstrument (I);
                    auto p = P0(); p.velo = 0.f; p.artic = artic;
                    e.noteOn (key, 0.6f, 1, kNoDet, 1u);
                    const double k = std::pow (2.0, (ref - key) / 12.0);
                    const int64_t st = (int64_t) std::llround (24000.0 * k); const int n = (int) std::lround (32768.0 * k);
                    Rec r; run (e, p, r, st + n + 512, 512);
                    return an::peakFreq (mono (r), st, n, mtof (root) * std::pow (2.0, (key - root) / 12.0));
                };
                std::set<int> done;
                for (const auto& rg : I->regions)
                {
                    if (rg.artic != artic || rg.kind != org::Kind::Attack || rg.lk == rg.hk || done.count (rg.root) || rg.root < 28 || rg.root > 100) continue;
                    done.insert (rg.root);
                    const int other = rg.hk != rg.root ? rg.hk : rg.lk;
                    const double c = an::cents (f0At (other, rg.root, other), f0At (rg.root, rg.root, other)) - 100.0 * (other - rg.root);
                    ++zones;
                    if (std::abs (c) > std::abs (worst)) { worst = c; wk = other; }
                    if (zones >= 12) break;
                }
            };
            double rw1, rw2; int rk1, rk2, z1, z2;
            repitch (sal, 0, rw1, rk1, z1); repitch (vio, 0, rw2, rk2, z2);
            bar ("real pitch: engine repitch (root vs zone neighbours)", std::abs (rw1) <= 3.0 && std::abs (rw2) <= 3.0 && z1 > 0,
                 fmt ("salamander %d zones worst %+.3f ¢ (key %d) · violin section %d zones worst %+.3f ¢ (key %d)", z1, rw1, rk1, z2, rw2, rk2));
            // (b) absolute tuning vs 12-TET (information: a recording's own tuning is Agent A's cents field)
            double w1, m1, w2, m2, w3, m3; int k1, k2, k3;
            pitchScan (sal, 33, 96, 3, w1, k1, m1);
            pitchScan (vio, 55, 94, 3, w2, k2, m2);
            // Salamander artic 1 ("Retuned")
            {
                w3 = 0; k3 = 0; m3 = 0; int cnt = 0;
                for (int key = 33; key <= 96; key += 3)
                {
                    OrganicEngine e; e.prepare (kSR, 512); e.setInstrument (sal);
                    auto p = P0(); p.velo = 0.f; p.artic = 1;
                    e.noteOn (key, 0.6f, 1, kNoDet, 1u);
                    Rec r; run (e, p, r, 48000 + 32768, 512);
                    const double c = an::cents (an::peakFreq (mono (r), 24000, 32768, mtof (key)), mtof (key));
                    m3 += c; ++cnt; if (std::abs (c) > std::abs (w3)) { w3 = c; k3 = key; }
                }
                m3 /= cnt;
            }
            std::printf ("      INFO absolute tuning vs 12-TET: salamander Natural worst %+.1f ¢ (key %d) mean %+.1f · Retuned worst %+.1f ¢ (key %d) mean %+.1f · violin section worst %+.1f ¢ (key %d) mean %+.1f\n",
                         w1, k1, m1, w3, k3, m3, w2, k2, m2);

            // velocity continuity across contiguous layers (Velocity 0 → the level is the recordings')
            auto velScan = [&] (const std::shared_ptr<const OrganicInstrument>& I, int key, int v0, int v1, int64_t at, double& worstStep, int& wv) {
                worstStep = 0; wv = 0; double prev = 0;
                for (int v = v0; v <= v1; ++v)
                {
                    OrganicEngine e; e.prepare (kSR, 512); e.setInstrument (I);
                    auto p = P0(); p.velo = 0.f;
                    e.noteOn (key, (float) v / 127.f, 1, kNoDet, 1u);
                    Rec r; run (e, p, r, at + 9600, 512);
                    const double l = an::db (an::rms (mono (r), at, 9600));
                    if (v > v0 && std::abs (l - prev) > worstStep) { worstStep = std::abs (l - prev); wv = v; }
                    prev = l;
                }
            };
            double ws; int wv;
            velScan (sal, 60, 1, 127, 2400, ws, wv);
            bar ("real layers: salamander C4 vel 1-127, adjacent step", ws <= 1.5, fmt ("largest level step between adjacent velocities %.2f dB (at vel %d)", ws, wv));
            double worstSolo = 0; int soloKey = 0, soloVel = 0;
            for (int key = 55; key <= 100; ++key)
            {
                double w; int v; velScan (solo, key, 48, 78, 24000, w, v);
                if (w > worstSolo) { worstSolo = w; soloKey = key; soloVel = v; }
            }
            bar ("real layers: solo violin seam (the 8 dB step) hidden", worstSolo <= 1.5,
                 fmt ("largest adjacent-velocity step over keys 55-100, vel 48-78: %.2f dB (key %d vel %d)", worstSolo, soloKey, soloVel));

            // live Dynamics sweep on the violin section: ≤ 2 readers, continuous level, no click
            {
                OrganicEngine e; e.prepare (kSR, 512); e.setInstrument (vio);
                e.noteOn (67, 64 / 127.f, 1, kNoDet, 1u);
                Rec r; auto p = P0(); int maxR = 0;
                std::vector<float> l (256), rr2 (256);
                for (int64_t t = 0; t < 5 * 48000; t += 256)
                {
                    p.dyn = (float) std::clamp (-1.0 + 2.0 * (double) (t - 48000) / (3.0 * 48000), -1.0, 1.0);
                    std::fill (l.begin(), l.end(), 0.f); std::fill (rr2.begin(), rr2.end(), 0.f);
                    e.render (p, 0.f, l.data(), rr2.data(), 256);
                    maxR = std::max (maxR, organics_debug::lastRenderReaders());
                    r.L.insert (r.L.end(), l.begin(), l.end()); r.R.insert (r.R.end(), rr2.begin(), rr2.end());
                }
                // a bowed section has its own HF (bow noise) and level motion, so the click reference is the SAME note
                // held at each end of the sweep: the sweep may not add HF above what the recording already has.
                auto held = [&] (float dyn) {
                    OrganicEngine h; h.prepare (kSR, 512); h.setInstrument (vio);
                    h.noteOn (67, 64 / 127.f, 1, kNoDet, 1u);
                    Rec q; auto pp = P0(); pp.dyn = dyn; run (h, pp, q, 5 * 48000, 256);
                    const auto hp = an::highpass (mono (q), 8000.0); return an::peak (hp, 48000, 3 * 48000);
                };
                const double ref = std::max (held (-1.f), held (1.f));
                const double sw = an::peak (an::highpass (mono (r), 8000.0), 48000, 3 * 48000);
                bar ("real Dynamics sweep: violin section G4, held", maxR <= 2 && an::db (sw / ref) <= 1.0,
                     fmt ("readers ≤ %d · HP(8k) peak during the sweep %+.2f dB re the static holds' own peak", maxR, an::db (sw / ref)));
            }
            // note-offs at random phases on the real piano (handoff + damper release + key-off noise)
            {
                juce::Random rnd (4242);
                OrganicEngine e; e.prepare (kSR, 512); e.setInstrument (sal);
                Rec r;
                for (int i = 0; i < 60; ++i)
                {
                    e.noteOn (48 + rnd.nextInt (25), 0.4f + 0.5f * rnd.nextFloat(), 1, kNoDet, (uint32_t) i);
                    run (e, P0(), r, 4800 + rnd.nextInt (19200), 64 + rnd.nextInt (448));
                    e.noteOff (false);
                    run (e, P0(), r, 9600 + rnd.nextInt (9600), 64 + rnd.nextInt (448));
                }
                const auto ck = an::clicks (mono (r));
                bar ("real no clicks: salamander 60 notes, offs at random phases", ck.relDb <= -60 || ck.localRatio <= 1.5,
                     fmt ("HP residual %.1f dB re peak (local ratio %.2f, worst at %.0f ms)", ck.relDb, ck.localRatio, ck.atMs));
            }
            // CPU on the real piano (stereo, decaying, 8-note chord)
            {
                std::vector<std::unique_ptr<OrganicEngine>> v;
                const int chord[8] = { 48, 52, 55, 59, 62, 65, 69, 72 };
                for (int i = 0; i < 8; ++i) { v.push_back (std::make_unique<OrganicEngine>()); v.back()->prepare (kSR, 512); v.back()->setInstrument (sal); }
                std::vector<float> l (512), r (512);
                auto p = P0(); p.sustain = 0.3f;
                double best = 1e30, perReg = 0; int regs = 0;
                for (int rep = 0; rep < 7; ++rep)
                {
                    for (int i = 0; i < 8; ++i) v[(size_t) i]->noteOn (chord[i], 0.7f, 1, kNoDet, (uint32_t) i);
                    for (int b = 0; b < 10; ++b) for (auto& e : v) e->render (p, 0.f, l.data(), r.data(), 512);
                    int64_t rb = 0; const auto t1 = std::chrono::steady_clock::now();
                    for (int b = 0; b < 200; ++b) for (auto& e : v) { e->render (p, 0.f, l.data(), r.data(), 512); rb += organics_debug::lastRenderReaders(); }
                    const double us = std::chrono::duration<double, std::micro> (std::chrono::steady_clock::now() - t1).count();
                    if (us / 200 < best) { best = us / 200; perReg = us / (double) std::max<int64_t> (1, rb); regs = (int) (rb / 200); }
                }
                bar ("real CPU: salamander 8-note chord", perReg <= 4.0, fmt ("%.1f µs/block, %d regions, %.2f µs per region per 512 block", best, regs, perReg));
            }
        }
    }

    std::printf ("══ %d/%d bars pass ══\n", gBars - gFails, gBars);
    return gFails == 0 ? 0 : 1;
}
