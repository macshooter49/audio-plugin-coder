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
#include <cstring>
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
        // ── tp105 test.noisy: a looped pure sine (release / vibrato / tfix), an "on" and an "off" mechanical noise ──
        {
            std::vector<Smp> S; juce::Array<juce::var> R;
            {   // attack: 220 Hz sine, loop continuous (whole cycles), authored release 0.5 s, tfix +25 cents
                const double f0 = 220.0; const int64_t ls = 4800, le = ls + wholeCycles (f0, 1.0), n = le + 64;
                S.push_back (tone (f0, n, [] (double t, int) { return (float) (0.5 * std::min (1.0, t / 0.002)); }, 1));
                auto r = region ("attack", 0, 0, 127, 1, 127, 57, n); setLoop (r, "continuous", ls, le, 0);
                auto* o = r.getDynamicObject(); o->setProperty ("tfix", 25.0);
                o->getProperty ("env").getDynamicObject()->setProperty ("r", 0.5);
                R.add (r);
            }
            {   // "on" noise: 100 ms 3 kHz burst, root 60 (play key 60 → 3 kHz), starts WITH the note
                const int64_t n = 4800;
                S.push_back (tone (3000.0, n, [] (double t, int) { return (float) (0.2 * std::min (1.0, t / 0.002) * std::min (1.0, (0.1 - t) / 0.005)); }, 1));
                auto r = region ("noise", 1, 0, 127, 1, 127, 60, n); r.getDynamicObject()->setProperty ("trig", "on"); R.add (r);
            }
            {   // "off" noise (no "trig" key = off): 100 ms 5 kHz burst, root 60, at note-off
                const int64_t n = 4800;
                S.push_back (tone (5000.0, n, [] (double t, int) { return (float) (0.2 * std::min (1.0, t / 0.002) * std::min (1.0, (0.1 - t) / 0.005)); }, 1));
                R.add (region ("noise", 2, 0, 127, 1, 127, 60, n));
            }
            writeInstrument (root, "test.noisy", "Test Noisy Sine", "violin", S, R, true, false,
                "tp105. All keys, root 57: a 220 Hz sine, loop continuous (whole cycles), authored env release 0.5 s, tfix +25 cents. "
                "Noise regions (root 60): trig \"on\" = 100 ms 3 kHz burst with the note; no trig (= \"off\") = 100 ms 5 kHz burst at note-off.");
        }
        // ── tp107 test.rrnoise: the noise ROUND-ROBIN — a looped 220 Hz sine, FOUR "on" noise takes (random ranges) and THREE
        //    "off" takes (a sequential RR of 3), each a 60 ms burst at its own frequency so the take is audible in the output ──
        {
            std::vector<Smp> S; juce::Array<juce::var> R;
            {
                const double f0 = 220.0; const int64_t ls = 4800, le = ls + wholeCycles (f0, 1.0), n = le + 64;
                S.push_back (tone (f0, n, [] (double t, int) { return (float) (0.5 * std::min (1.0, t / 0.002)); }, 1));
                auto r = region ("attack", 0, 0, 127, 1, 127, 57, n); setLoop (r, "continuous", ls, le, 0); R.add (r);
            }
            auto burst = [] (double f) { return tone (f, 2880, [] (double t, int) { return (float) (0.2 * std::min (1.0, t / 0.002) * std::min (1.0, (0.06 - t) / 0.005)); }, 1); };
            const double onF[4] = { 2000.0, 2500.0, 3000.0, 3500.0 }, offF[3] = { 5000.0, 6000.0, 7000.0 };
            for (int v = 0; v < 4; ++v)
            {
                S.push_back (burst (onF[v]));
                auto r = region ("noise", (int) S.size() - 1, 0, 127, 1, 127, 60, 2880); auto* o = r.getDynamicObject();
                o->setProperty ("trig", "on"); o->setProperty ("rand", juce::Array<juce::var> { 0.25 * v, 0.25 * (v + 1) }); R.add (r);
            }
            for (int v = 0; v < 3; ++v)
            {
                S.push_back (burst (offF[v]));
                auto r = region ("noise", (int) S.size() - 1, 0, 127, 1, 127, 60, 2880);
                r.getDynamicObject()->setProperty ("rr", juce::Array<juce::var> { v, 3 }); R.add (r);
            }
            writeInstrument (root, "test.rrnoise", "Test Round-Robin Noise", "violin", S, R, true, false,
                "tp107. All keys, root 57: a 220 Hz sine, loop continuous. Noise (root 60, play key 60): four trig \"on\" takes by random "
                "range (60 ms bursts at 2 / 2.5 / 3 / 3.5 kHz) and three note-off takes by a sequential RR of 3 (5 / 6 / 7 kHz).");
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

// ── tp105 measurement helpers ──
/** Seconds from `off` until the 10 ms RMS first falls 60 dB under the RMS of the 100 ms before `off` (−1 = never). */
static double t60 (const std::vector<float>& x, int64_t off)
{
    double ref = 0; for (int64_t i = off - 4800; i < off; ++i) ref += (double) x[(size_t) i] * x[(size_t) i];
    ref = std::sqrt (ref / 4800.0);
    for (int64_t w = off; w + 480 <= (int64_t) x.size(); w += 480)
    {
        double a = 0; for (int64_t i = w; i < w + 480; ++i) a += (double) x[(size_t) i] * x[(size_t) i];
        if (std::sqrt (a / 480.0) < ref * 1.0e-3) return (double) (w - off) / kSR;
    }
    return -1.0;
}
/** Per-cycle f0 of a (near-)pure tone by interpolated rising zero crossings: {time s, cents re ref}. */
static std::vector<std::pair<double, double>> zcTrack (const std::vector<float>& x, int64_t s0, int64_t s1, double ref)
{
    std::vector<std::pair<double, double>> out; double last = -1;
    for (int64_t i = std::max<int64_t> (1, s0); i < s1 && i < (int64_t) x.size(); ++i)
        if (x[(size_t) i - 1] < 0.f && x[(size_t) i] >= 0.f)
        {
            const double t = (double) (i - 1) + (double) (-x[(size_t) i - 1]) / (double) (x[(size_t) i] - x[(size_t) i - 1]);
            if (last >= 0) out.push_back ({ 0.5 * (t + last) / kSR, 1200.0 * std::log2 ((kSR / (t - last)) / ref) });
            last = t;
        }
    return out;
}
struct VibStats { double depth = 0, rate = 0, mean = 0; };
static VibStats vibStats (const std::vector<std::pair<double, double>>& tr, double t0, double t1)
{
    std::vector<double> c; std::vector<double> ts;
    for (auto& q : tr) if (q.first >= t0 && q.first < t1) { c.push_back (q.second); ts.push_back (q.first); }
    VibStats v; if (c.size() < 8) return v;
    for (double x : c) v.mean += x; v.mean /= (double) c.size();
    auto srt = c; std::sort (srt.begin(), srt.end());
    v.depth = 0.5 * (srt[(size_t) (0.98 * (double) (srt.size() - 1))] - srt[(size_t) (0.02 * (double) (srt.size() - 1))]);
    int cross = 0; double first = -1, lastT = -1;
    for (size_t i = 1; i < c.size(); ++i)
        if (c[i - 1] < v.mean && c[i] >= v.mean) { ++cross; if (first < 0) first = ts[i]; lastT = ts[i]; }
    v.rate = cross > 1 ? (double) (cross - 1) / (lastT - first) : 0;
    return v;
}
static an::Buf mono (const Rec& r) { an::Buf m (r.L.size()); for (size_t i = 0; i < m.size(); ++i) m[i] = 0.5f * (r.L[i] + r.R[i]); return m; }

static const float kNoDet[16] = {};

//==================================================================================================
//  tp105 NO-SILENCE sweep (Max: "Xylophone, F4 — every OTHER press is silent… NO SILENCES, ever").
//  Every installed instrument × every articulation × every key of its range (± margin) × velocities
//  {20, 64, 100, 127} × Human {0, 0.41, 1} × 8 repeated presses: each press must reach −60 dBFS within 30 ms + the
//  Human timing (12 ms × Human) + the region's own authored onset at the playback ratio (a soft bowed cello
//  layer speaks 97 ms in — that is the recording, not a silence).
//==================================================================================================
static int runSweep (const juce::File& root, int margin)
{
    setEnv ("TERRAIN_ORGANICS_DIR", root.getFullPathName().toRawUTF8());
    juce::MessageManager::getInstance();
    OrganicsLibrary::get().rescan();
    const auto idx = OrganicsLibrary::get().index();
    std::printf ("══ ORGANICS NO-SILENCE SWEEP — %s (margin ±%d keys) ══\n", root.getFullPathName().toRawUTF8(), margin);
    if (! idx.isArray() || idx.size() == 0) { std::printf ("FAIL  no instruments under the root\n"); return 1; }
    int64_t totalFail = 0, totalPress = 0; int instFail = 0, insts = 0;
    const int vels[4] = { 20, 64, 100, 127 };
    constexpr int kBlk = 256;
    for (auto& ent : *idx.getArray())
    {
        const juce::String id = ent["id"].toString();
        if (const char* only = std::getenv ("ORG_SWEEP_ONLY")) if (id != juce::String (only)) continue;   // test-only: sweep one instrument
        auto I = load (id);
        ++insts;
        if (! I) { std::printf ("FAIL  %-40s does not load\n", id.toRawUTF8()); ++instFail; continue; }
        int fails = 0, presses = 0; std::string first;
        for (int a = 0; a < I->numArtics; ++a)
        {
            int lo = 128, hi = -1;
            for (auto& r : I->regions) if (r.artic == a && r.kind == org::Kind::Attack) { lo = std::min (lo, r.lk); hi = std::max (hi, r.hk); }
            if (hi < 0) continue;
            lo = std::max (0, lo - margin); hi = std::min (127, hi + margin);
            OrganicEngine e; e.prepare (kSR, kBlk); e.setInstrument (I);
            std::vector<float> L (kBlk), R (kBlk);
            for (float human : { 0.f, 0.41f, 1.f })             // Human 0 · 0.41 (Max's patch) · 1: fake RR at every strength
            for (int key = lo; key <= hi; ++key)
                for (int v : vels)
                {
                    OrganicParams p; p.artic = a; p.human = human;
                    int silent = 0;
                    for (int press = 0; press < 8; ++press)
                    {
                        e.noteOn (key, (float) v / 127.f, 1, kNoDet, 0x1234567u + (uint32_t) (key * 131 + v * 7 + press * 7919));
                        float pk = 0.f; int64_t t = 0, win = (int64_t) ((0.031 + 0.012 * human + (std::getenv ("ORG_SWEEP_EXTRA_MS") ? std::atof (std::getenv ("ORG_SWEEP_EXTRA_MS")) / 1000.0 : 0.0)) * kSR); bool winSet = false;   // + the Human timing (12 ms × h)
                        for (int guard = 0; guard < 400 && t < win; ++guard)
                        {
                            std::fill (L.begin(), L.end(), 0.f); std::fill (R.begin(), R.end(), 0.f);
                            e.render (p, 0.f, L.data(), R.data(), kBlk);
                            for (int i = 0; i < kBlk; ++i) pk = std::max (pk, std::max (std::abs (L[(size_t) i]), std::abs (R[(size_t) i])));
                            t += kBlk;
                            if (! winSet)
                            {
                                winSet = true;
                                const int ri = organics_debug::lastNoteRegion();
                                if (ri >= 0 && ri < (int) I->regions.size())
                                {
                                    const auto& rg = I->regions[(size_t) ri];
                                    const double ratio = std::pow (2.0, (key - rg.root) / 12.0) * I->samples[(size_t) rg.smp].sampleRate / kSR;
                                    win += (int64_t) ((double) std::max<int64_t> (0, rg.onset - rg.start) / std::max (1.0e-3, ratio));
                                }
                            }
                        }
                        e.noteOff (false);
                        for (int b = 0; b < 4; ++b) { std::fill (L.begin(), L.end(), 0.f); std::fill (R.begin(), R.end(), 0.f); e.render (p, 0.f, L.data(), R.data(), kBlk); }
                        ++presses;
                        if (pk < 0.001f) { ++fails; ++silent; }
                    }
                    if (silent > 0 && first.size() < 300) first += fmt (" a%d:k%dv%dh%.2f(%d/8)", a, key, v, human, silent);
                }
            e.kill(); e.setInstrument (nullptr);
        }
        totalFail += fails; totalPress += presses;
        if (fails) { ++instFail; std::printf ("FAIL  %-40s %d/%d silent presses:%s\n", id.toRawUTF8(), fails, presses, first.c_str()); }
        else std::printf ("PASS  %-40s %d presses, all sound\n", id.toRawUTF8(), presses);
        std::fflush (stdout);
        I.reset();
        org::drainDeferredReleases();
    }
    std::printf ("══ %s — %lld silent of %lld presses · %d/%d instruments clean ══\n", totalFail == 0 ? "PASS" : "FAIL",
                 (long long) totalFail, (long long) totalPress, insts - instFail, insts);
    return totalFail == 0 && instFail == 0 ? 0 : 1;
}

//==================================================================================================
//  tp107 — ATTACK comes back (0 Tight · 0.5 Natural · 1 a ~3 s swell) and NOISE becomes a round-robin, like a player
//==================================================================================================
static void tp107Bars (const std::shared_ptr<const OrganicInstrument>& noisy, const std::shared_ptr<const OrganicInstrument>& piano)
{
    std::printf ("── tp107: Attack · round-robin Noise ──\n");
    const float knobs[5] = { 0.f, 0.25f, 0.5f, 0.75f, 1.f };
    auto atkOf = [] (float knob) { return 1.f - 2.f * knob; };          // the processor's mapping (PluginProcessor gather)
    // ── ATTACK: onset time vs the knob. Sustained sine (test.noisy, looped): time until the 5 ms RMS reaches −1 dB of the
    //    steady level; piano (test.piano: 20 ms of air, then the transient): the transient's arrival (−24 dB of the peak). ──
    {
        auto sineOnset = [&] (float atk, float ampAtt, double* clickDb = nullptr) {
            OrganicEngine e; e.prepare (kSR, 512); e.setInstrument (noisy);
            auto p = P0(); p.tuning = 0; p.noise = 0.f; p.attack = atk; p.ampAttack = ampAtt;
            e.noteOn (57, 0.8f, 1, kNoDet, 5u);
            Rec r; run (e, p, r, (int64_t) (4.5 * kSR), 256);
            const auto x = mono (r);
            const double ref = an::rms (x, (int64_t) (4.0 * kSR), 24000);
            int64_t k = 0; const int64_t W = 240;
            while (k + W < (int64_t) x.size() && an::rms (x, k, W) < ref * 0.891) k += 48;
            if (clickDb) { const auto c = an::clicks (x); *clickDb = c.localRatio <= 1.5 ? -200.0 : c.relDb; }
            return 1000.0 * (double) (k + W / 2) / kSR;
        };
        auto pianoOnset = [&] (float atk) {
            OrganicEngine e; e.prepare (kSR, 512); e.setInstrument (piano);
            auto p = P0(); p.attack = atk; p.noise = 0.f;
            e.noteOn (43, 0.8f, 1, kNoDet, 3u);
            Rec r; run (e, p, r, 24000, 64);
            const auto m = mono (r); const double thr = 0.063 * an::peak (m, 0, 24000);
            int64_t k = 0; while (k < (int64_t) m.size() && std::abs (m[(size_t) k]) < thr) ++k;
            return std::make_pair (1000.0 * (double) k / kSR, an::db (an::rms (m, 0, 576)));
        };
        double so[5], po[5], pe[5], ck[5];
        std::string tbl;
        for (int i = 0; i < 5; ++i)
        {
            so[i] = sineOnset (atkOf (knobs[i]), 0.f, &ck[i]);
            const auto pp = pianoOnset (atkOf (knobs[i])); po[i] = pp.first; pe[i] = pp.second;
            tbl += fmt ("%.0f%%: sine %.1f ms · piano transient %.1f ms (first 12 ms %.1f dB) · click %s\n        ", 100 * knobs[i], so[i], po[i], pe[i],
                        ck[i] < -150 ? "none" : fmt ("%.0f dB", ck[i]).c_str());
        }
        std::printf ("  Attack onset table (knob → onset):\n        %s\n", tbl.c_str());
        bar ("Attack: 0 Tight earlier + hotter than 0.5 Natural (piano)", po[0] < po[1] && po[1] < po[2] && pe[0] > pe[2] + 1.0,
             fmt ("transient %.1f / %.1f / %.1f ms · first-12ms %.1f / %.1f / %.1f dB (0 / 25 / 50 %%)", po[0], po[1], po[2], pe[0], pe[1], pe[2]));
        bar ("Attack: 0.5 → 1 a log-tapered swell reaching ~3 s", so[2] < 5.0 && so[3] > 100.0 && so[3] < 250.0 && so[4] > 2000.0 && so[4] < 3200.0 && so[3] > so[2],
             fmt ("sine −1 dB onset %.1f ms (50 %%) · %.0f ms (75 %%) · %.0f ms (100 %%)", so[2], so[3], so[4]));
        bool noCk = true; for (int i = 0; i < 5; ++i) noCk &= ck[i] < -60.0;
        bar ("Attack: every setting starts click-free (HP 8 kHz residual)", noCk,
             fmt ("residual %s / %s / %s / %s / %s", ck[0] < -150 ? "—" : fmt ("%.0f", ck[0]).c_str(), ck[1] < -150 ? "—" : fmt ("%.0f", ck[1]).c_str(),
                  ck[2] < -150 ? "—" : fmt ("%.0f", ck[2]).c_str(), ck[3] < -150 ? "—" : fmt ("%.0f", ck[3]).c_str(), ck[4] < -150 ? "—" : fmt ("%.0f", ck[4]).c_str()));
        // the amp-envelope link: the engine fades only when its fade is the LONGER one (else the voice's VCA ramp is the onset)
        const double l75 = sineOnset (atkOf (0.75f), 1.0f), l100 = sineOnset (atkOf (1.f), 1.0f), l50 = sineOnset (0.f, 1.0f);
        bar ("Attack = max(amp-env attack, knob): the engine yields to a longer amp attack", std::abs (l75 - so[2]) < 1.0 && std::abs (l50 - so[2]) < 0.01 && std::abs (l100 - so[4]) < 1.0,
             fmt ("amp attack 1 s: engine onset %.1f ms at 75 %% (its 173 ms fade yields to the VCA) · %.1f ms at 50 %% · %.0f ms at 100 %% (3 s > 1 s: the engine's)", l75, l50, l100));
    }
    // ── NOISE ROUND-ROBIN: 300 notes at 0.25 / 0.5 / 1 on test.rrnoise (4 on-takes, 3 off-takes), fixed seeds ──
    {
        auto rr = load ("test.rrnoise");
        bar ("tp107 fixture test.rrnoise loads", rr != nullptr, fmt ("rrnoise=%d", !! rr));
        if (rr == nullptr) return;
        const double onF[4] = { 2000.0, 2500.0, 3000.0, 3500.0 }, offF[3] = { 5000.0, 6000.0, 7000.0 };
        struct Run { int onHits = 0, offHits = 0, onRep = 0, offRep = 0, decisions = 0; double dbMin = 99, dbMax = -99, dbSd = 0, audSd = 0, audMean = 0;
                     int delayMax = 0; std::vector<int> onSeq, offSeq; std::vector<float> dbs; };
        auto go = [&] (float knob, float human) {
            rr->resetPerformanceState();
            Run R;
            OrganicEngine e; e.prepare (kSR, 480); e.setInstrument (rr);
            auto p = P0(); p.tuning = 0; p.noise = knob; p.human = human; p.release = 0.f;
            std::vector<double> aud;
            int lastOn = -1, lastOff = -1;
            for (int i = 0; i < 300; ++i)
            {
                const int d0 = organics_debug::noiseDecisions(), h0 = organics_debug::noiseHits();
                e.noteOn (60, 0.8f, 1, kNoDet, 0x9E3779B9u * (uint32_t) (i + 1));
                Rec r; run (e, p, r, 4800, 480);
                const int h1 = organics_debug::noiseHits();
                const bool hitOn = h1 > h0; if (hitOn) { R.dbs.push_back (organics_debug::lastNoiseDb()); R.delayMax = std::max (R.delayMax, organics_debug::lastNoiseDelay()); }
                e.noteOff (false); run (e, p, r, 4800, 480);
                const int h2 = organics_debug::noiseHits();
                const bool hitOff = h2 > h1; if (hitOff) { R.dbs.push_back (organics_debug::lastNoiseDb()); R.delayMax = std::max (R.delayMax, organics_debug::lastNoiseDelay()); }
                R.decisions += organics_debug::noiseDecisions() - d0;
                const auto x = mono (r);
                // the take, heard: the loudest burst frequency in each 90 ms window (a miss = nothing over −70 dBFS)
                int vo = -1; double bo = 3e-4; for (int v = 0; v < 4; ++v) { const double a = an::amp (x, 0, 4320, onF[v]); if (a > bo) { bo = a; vo = v; } }
                int vf = -1; double bf = 3e-4; for (int v = 0; v < 3; ++v) { const double a = an::amp (x, 4800, 4320, offF[v]); if (a > bf) { bf = a; vf = v; } }
                if ((vo >= 0) != hitOn || (vf >= 0) != hitOff) { R.onRep += 1000; }            // audio and hook disagree: flagged
                if (vo >= 0) { R.onHits++; R.onSeq.push_back (vo); if (vo == lastOn) R.onRep++; lastOn = vo; aud.push_back (an::db (bo)); }
                if (vf >= 0) { R.offHits++; R.offSeq.push_back (vf); if (vf == lastOff) R.offRep++; lastOff = vf; }
            }
            double m = 0; for (float d : R.dbs) { m += d; R.dbMin = std::min (R.dbMin, (double) d); R.dbMax = std::max (R.dbMax, (double) d); }
            m /= std::max<size_t> (1, R.dbs.size()); for (float d : R.dbs) R.dbSd += (d - m) * (d - m); R.dbSd = std::sqrt (R.dbSd / std::max<size_t> (1, R.dbs.size()));
            double am = 0; for (double a : aud) am += a; am /= std::max<size_t> (1, aud.size()); for (double a : aud) R.audSd += (a - am) * (a - am);
            R.audSd = std::sqrt (R.audSd / std::max<size_t> (1, aud.size())); R.audMean = am;
            return R;
        };
        const Run r25 = go (0.25f, 0.f), r50 = go (0.5f, 0.f), r100 = go (1.f, 0.f), r0 = go (0.f, 0.f), r50b = go (0.5f, 0.f), r50h = go (0.5f, 1.f);
        auto line = [&] (const char* k, const Run& R) {
            return fmt ("%s: on %d/300 (%.0f %%) · off %d/300 (%.0f %%) · level offset %+.1f..%+.1f dB (SD %.2f) · heard %.1f dBFS (SD %.2f) · delay ≤ %.1f ms · back-to-back repeats %d / %d",
                        k, R.onHits, R.onHits / 3.0, R.offHits, R.offHits / 3.0, R.dbMin, R.dbMax, R.dbSd, R.audMean, R.audSd, 1000.0 * R.delayMax / kSR, R.onRep, R.offRep);
        };
        std::printf ("  Noise round-robin over 300 notes (test.rrnoise: 4 on-takes, 3 off-takes):\n        %s\n        %s\n        %s\n        %s\n        %s\n",
                     line ("  0 %", r0).c_str(), line (" 25 %", r25).c_str(), line (" 50 %", r50).c_str(), line ("100 %", r100).c_str(), line (" 50 % Human 1", r50h).c_str());
        auto near = [] (int hits, double want) { return std::abs (hits / 300.0 - want) <= 0.07; };
        bar ("Noise RR: chance 0 never · 25 % ≈ 1/3 · 50 % ≈ 2/3 · 100 % ≈ 9/10 (on AND off, independent)",
             r0.onHits == 0 && r0.offHits == 0 && r0.decisions == 0 && near (r25.onHits, 1 / 3.0) && near (r25.offHits, 1 / 3.0) && near (r50.onHits, 2 / 3.0)
             && near (r50.offHits, 2 / 3.0) && near (r100.onHits, 0.9) && near (r100.offHits, 0.9),
             fmt ("on %.0f / %.0f / %.0f %% · off %.0f / %.0f / %.0f %% (25 / 50 / 100)", r25.onHits / 3.0, r50.onHits / 3.0, r100.onHits / 3.0, r25.offHits / 3.0, r50.offHits / 3.0, r100.offHits / 3.0));
        bar ("Noise RR: the take never repeats back to back (4 on-takes, 3 off-takes), audio = hook",
             r25.onRep + r25.offRep + r50.onRep + r50.offRep + r100.onRep + r100.offRep + r50h.onRep + r50h.offRep == 0,
             fmt ("repeats %d / %d / %d / %d (a value ≥ 1000 = the heard take and the hook disagree)", r25.onRep + r25.offRep, r50.onRep + r50.offRep, r100.onRep + r100.offRep, r50h.onRep + r50h.offRep));
        int used[4] = {}; for (int v : r50.onSeq) used[v]++;
        bar ("Noise RR: every take is used", used[0] > 20 && used[1] > 20 && used[2] > 20 && used[3] > 20, fmt ("on-takes at 50 %%: %d / %d / %d / %d", used[0], used[1], used[2], used[3]));
        bar ("Noise RR: level ±3 dB, timing 0–8 ms (Human 1: ±5 dB, 0–12 ms)",
             r50.dbMin >= -3.0 && r50.dbMax <= 3.0 && r50.dbMax - r50.dbMin > 5.0 && r50.delayMax <= (int) (0.008 * kSR) + 1 && r50.delayMax > (int) (0.006 * kSR)
             && r50h.dbMax - r50h.dbMin > 8.0 && r50h.dbMax <= 5.0 && r50h.delayMax <= (int) (0.012 * kSR) + 1,
             fmt ("%.1f..%+.1f dB (heard SD %.2f dB) · ≤ %.1f ms · Human 1: %.1f..%+.1f dB, ≤ %.1f ms", r50.dbMin, r50.dbMax, r50.audSd, 1000.0 * r50.delayMax / kSR,
                  r50h.dbMin, r50h.dbMax, 1000.0 * r50h.delayMax / kSR));
        bar ("Noise RR: knob 1 is +12 dB over 0.5 (the heard mean)", std::abs ((r100.audMean - r50.audMean) - 12.0) <= 1.0,
             fmt ("heard burst mean %.1f vs %.1f dBFS → %+.2f dB", r100.audMean, r50.audMean, r100.audMean - r50.audMean));
        bar ("Noise RR: deterministic at a fixed seed (two runs, same sequence)", r50.onSeq == r50b.onSeq && r50.offSeq == r50b.offSeq && r50.dbs == r50b.dbs,
             fmt ("run 1 %zu + %zu takes, run 2 %zu + %zu: %s", r50.onSeq.size(), r50.offSeq.size(), r50b.onSeq.size(), r50b.offSeq.size(),
                  (r50.onSeq == r50b.onSeq && r50.dbs == r50b.dbs) ? "identical" : "DIFFERENT"));
    }
}

//==================================================================================================
//==================================================================================================
//  tp108 — TONE ON PURE TONES. The tilt alone moved a near-sine's centroid ×1.0–1.4 end to end; the spectrum-aware stages
//  (a Chebyshev exciter on +, the tilt's shelf morphed into a low-pass on −) must make it ×1.8+, keep 10–50 % clean, add no
//  aliasing, stay click-free while swept, stay out of spectra the tilt already moves (and out of noise), and cost nothing at 0.
//==================================================================================================
namespace tp108
{
    /** POWER-weighted centroid of [s, s+n) — the knob sweep's brightness metric (organics_audit --tone). */
    static double pcen (const an::Buf& x, int64_t s = 960, int n = 8192)
    {
        const auto m = an::mag (x, s, n);
        double num = 0, den = 0;
        for (size_t i = 1; i < m.size(); ++i) { const double f = (double) i * kSR / n; if (f < 20 || f > 20000) continue; num += f * m[i] * m[i]; den += m[i] * m[i]; }
        return num / std::max (1e-30, den);
    }
    /** Power in [f1, f2) re the whole 20 Hz–24 kHz, dB. */
    static double bandDb (const an::Buf& x, double f1, double f2, int64_t s = 960, int n = 8192)
    {
        const auto m = an::mag (x, s, n);
        double e = 0, t = 0;
        for (size_t i = 1; i < m.size(); ++i) { const double f = (double) i * kSR / n, p = m[i] * m[i]; if (f < 20) continue; t += p; if (f >= f1 && f < f2) e += p; }
        return 10 * std::log10 (std::max (1e-30, e) / std::max (1e-30, t));
    }
    /** Harmonic (k = 2..12) and inharmonic (everything else, 50 Hz up) power re the fundamental, dB — a 4-term Blackman-Harris
        window (−92 dB sidelobes: a Hann window's own skirts round harmonics 8 dB over the fundamental read as "off the grid")
        and ±6 bins round every k·f0 counted as that harmonic. */
    static std::pair<double, double> harmonics (const an::Buf& x, double f0, int64_t s = 9600, int n = 16384)
    {
        std::vector<std::complex<double>> a ((size_t) n);
        for (int i = 0; i < n; ++i)
        {
            const double t = 2.0 * kPi * i / (n - 1), w = 0.35875 - 0.48829 * std::cos (t) + 0.14128 * std::cos (2 * t) - 0.01168 * std::cos (3 * t);
            a[(size_t) i] = s + i < (int64_t) x.size() ? w * x[(size_t) (s + i)] : 0.0;
        }
        an::fft (a);
        double fund = 0, harm = 0, inh = 0;
        for (int i = 1; i < n / 2; ++i)
        {
            const double f = (double) i * kSR / n, p = std::norm (a[(size_t) i]);
            if (f < 50) continue;
            const double k = std::round (f / f0), dk = std::abs (f - k * f0) * n / kSR;
            if (k >= 1 && dk <= 6.0) { if (k == 1) fund += p; else if (k <= 12) harm += p; else inh += p; }
            else inh += p;
        }
        return { 10 * std::log10 (std::max (1e-30, harm) / std::max (1e-30, fund)), 10 * std::log10 (std::max (1e-30, inh) / std::max (1e-30, fund)) };
    }
    static an::Buf render (const std::shared_ptr<const OrganicInstrument>& I, int key, float vel, OrganicParams p, int64_t frames,
                           bool stages = true, std::function<void (OrganicParams&, int64_t)> hook = {})
    {
        organics_debug::setToneStages (stages);
        I->resetPerformanceState();
        OrganicEngine e; e.prepare (kSR, 256); e.setInstrument (I);
        e.noteOn (key, vel, 1, kNoDet, 1u);
        Rec r; run (e, p, r, frames, 256, 0.f, hook);
        organics_debug::setToneStages (true);
        return mono (r);
    }
}

static void tp108Bars (const std::shared_ptr<const OrganicInstrument>& pure, const std::shared_ptr<const OrganicInstrument>& sine,
                       const std::shared_ptr<const OrganicInstrument>& norr)
{
    using namespace tp108;
    auto pp = [] (float tone) { auto p = P0(); p.noise = 0.f; p.tuning = 0; p.tone = tone; return p; };
    // 1. DRAMATIC: a pure sine's centroid ×1.8+ from Tone −1 to +1 (it was ×1.00 with the tilt alone) at three pitches
    {
        std::string d; bool ok = true;
        for (int key : { 57, 81, 96 })
        {
            const double lo = pcen (render (pure, key, 0.63f, pp (-1.f), 12000)), hi = pcen (render (pure, key, 0.63f, pp (1.f), 12000));
            const double lo0 = pcen (render (pure, key, 0.63f, pp (-1.f), 12000, false)), hi0 = pcen (render (pure, key, 0.63f, pp (1.f), 12000, false));
            const auto st = organics_debug::lastTone();
            ok &= hi / lo >= 1.8;
            d += fmt ("%sk%d ×%.2f (tilt alone ×%.2f, sparse %.2f)", d.empty() ? "" : " · ", key, hi / lo, hi0 / lo0, st.sparse);
        }
        bar ("tp108 Tone: a pure sine ×1.8+ from −1 to +1", ok, d);
    }
    // 2. EXACT HARMONICS: at +1 the added content sits on the harmonic grid (Chebyshev: a sine → its harmonics, nothing else)
    {
        const auto x = render (pure, 57, 0.63f, pp (1.f), 36000);
        const auto h = harmonics (x, 220.0);
        bar ("tp108 Tone +1 on a sine: harmonics, not noise", h.first >= -6.0 && h.second <= -45.0,
             fmt ("220 Hz sine at +1: harmonics 2–12 %+.1f dB re the fundamental, everything off the grid %+.1f dB", h.first, h.second));
    }
    // 3. THE TAPER (the lifeguard law): the knob's 10–50 % is Tone −0.8 … 0 — the dark side, a linear filter: it can't add a
    //    harmonic. The first half of the bright side is a sheen; 100 % is the whole distance.
    {
        const float ts[6] = { -0.8f, -0.5f, 0.f, 0.25f, 0.5f, 1.f };
        double hd[6]; std::string d;
        for (int i = 0; i < 6; ++i)
        {
            hd[i] = harmonics (render (pure, 57, 0.63f, pp (ts[i]), 36000), 220.0).first;
            d += fmt ("%s%+.2f → %+.1f dB", i ? " · " : "", ts[i], hd[i]);
        }
        bar ("tp108 Tone taper: 10–50 % adds no harmonic", hd[0] <= hd[2] + 1.0 && hd[1] <= hd[2] + 1.0, fmt ("harmonics 2–12 re the fundamental (knob 10 %%, 25 %%, 50 %%, 62.5 %%, 75 %%, 100 %%): %s", d.c_str()));
        bar ("tp108 Tone taper: a sheen at 62.5 %, the lot at 100 %", hd[3] <= -20.0 && hd[4] <= -10.0 && hd[5] >= 0.0 && hd[3] < hd[4] && hd[4] < hd[5],
             fmt ("+0.25 %+.1f dB (≤ −20) · +0.5 %+.1f dB (≤ −10) · +1 %+.1f dB (≥ 0: the harmonics outweigh the fundamental)", hd[3], hd[4], hd[5]));
    }
    // 4. NO ALIASING: a C7 sine at +1 — every harmonic past 14 kHz faded out, the band keeps the rest from folding
    {
        const auto on = render (pure, 96, 0.63f, pp (1.f), 36000), off = render (pure, 96, 0.63f, pp (1.f), 36000, false);
        const double a = bandDb (on, 16000.0, 24000.0), b = bandDb (off, 16000.0, 24000.0);
        const auto h = harmonics (on, mtof (96));
        bar ("tp108 Tone +1 at C7: nothing over 16 kHz", a <= -60.0 && h.second <= -40.0,
             fmt ("energy > 16 kHz %+.1f dB re the note (the tilt alone %+.1f) · off the harmonic grid %+.1f dB", a, b, h.second));
    }
    // 5. NO CLICK: swept −1 → +1 → −1 on a held C7 sine (the exciter crossing on/off, the gate, the dark morph) — the HP(8k)
    //    residual may not exceed the static note's at either end
    {
        auto hp = [] (const an::Buf& x) { return an::peak (an::highpass (x, 8000.0), 9600, 72000); };
        const auto sLo = render (pure, 96, 0.63f, pp (-1.f), 96000), sHi = render (pure, 96, 0.63f, pp (1.f), 96000);
        const auto sw = render (pure, 96, 0.63f, pp (-1.f), 96000, true, [] (OrganicParams& q, int64_t at) {
            const double t = (double) at / kSR;
            q.tone = (float) (t < 0.2 ? -1.0 : t < 1.0 ? -1.0 + 2.0 * (t - 0.2) / 0.8 : t < 1.8 ? 1.0 - 2.0 * (t - 1.0) / 0.8 : -1.0); });
        const double ref = std::max (hp (sLo), hp (sHi)), got = hp (sw);
        bar ("tp108 Tone swept on a pure tone: no click", an::db (got / ref) <= 1.0,
             fmt ("C7 sine, Tone −1 → +1 → −1 in 1.6 s: HP(8k) peak %+.2f dB re the static note at either end (≤ +1)", an::db (got / ref)));
    }
    // 6. DARK: on a sparse note (test.sine C3: a sine + a −14 dB marker partial, SPARSE 1) −1 takes the partials down
    //    further than the tilt alone could (its −9 dB shelf has a floor; the morphed shelf does not). The marker sits under
    //    the 700 Hz pivot, where a first-order morph gains ~5 dB; the real bars show it on whole notes (the centroids).
    {
        auto mk = [&] (bool st) { return harmonics (render (sine, 48, 0.63f, pp (-1.f), 14400, st), mtof (48), 4800, 8192); };
        const auto on = mk (true), off = mk (false);
        const auto stt = organics_debug::lastTone();
        bar ("tp108 Tone −1: dark keeps going past the tilt", on.first <= off.first - 3.0,
             fmt ("test.sine C3 (sparse %.2f): its partials re the fundamental at −1 %+.1f dB (the tilt alone %+.1f)", stt.sparse, on.first, off.first));
    }
    // 7. STAYS OUT: white noise (the tilt can't move it, but it is not a sparse spectrum) and a rich zone → bit-identical
    {
        bool same = true; std::string d;
        for (int key : { 6, 48 })
            for (float t : { -1.f, 1.f })
            {
                const auto a = render (norr, key, 0.6f, pp (t), 24000), b = render (norr, key, 0.6f, pp (t), 24000, false);
                const bool eq = a.size() == b.size() && std::memcmp (a.data(), b.data(), sizeof (float) * a.size()) == 0;
                same &= eq;
                d += fmt ("%sk%d %+.0f %s", d.empty() ? "" : " · ", key, t, eq ? "=" : "≠");
            }
        bar ("tp108 Tone: noise and a rich spectrum untouched", same, fmt ("test.norr white-noise zone (k6) and a harmonic zone the tilt moves (k48), stages on vs off: %s", d.c_str()));
    }
}

/** The real library (organics_audit --tone has the full table; these are its load-bearing rows). TODAY = ceff345d. */
//==================================================================================================
//  tp114 — THE SAFETY LIMITER (OrganicEngine.cpp PeakGuard, organics::kLimiterCeilingDb). Every other bar in this file runs
//  with it BYPASSED (main: they gate the engine's DSP, and the fixtures are full-scale sines that sit over the ceiling);
//  these gate the limiter itself.
//==================================================================================================
/** A reference 4× true peak: the EBU R128 / BS.1770 meter design (a Hann-windowed sinc over ±6 samples at ¼ ½ ¾). */
static double refIsp (const an::Buf& L, const an::Buf& R)
{
    auto one = [] (const an::Buf& x) {
        double best = 0; const int n = (int) x.size();
        for (int i = 0; i < n; ++i) best = std::max (best, (double) std::abs (x[(size_t) i]));
        const double pk = best;
        for (int i = 0; i + 1 < n; ++i)
        {
            if (std::max (std::abs (x[(size_t) i]), std::abs (x[(size_t) i + 1])) < 0.5 * pk) continue;
            for (int p = 1; p < 4; ++p)
            {
                double acc = 0;
                for (int k = -5; k <= 6; ++k)
                {
                    const int j = i + k; if (j < 0 || j >= n) continue;
                    const double u = p / 4.0 - k;
                    acc += x[(size_t) j] * std::sin (kPi * u) / (kPi * u) * (0.5 + 0.5 * std::cos (kPi * u / 6.0));
                }
                best = std::max (best, std::abs (acc));
            }
        }
        return best;
    };
    return std::max (one (L), one (R));
}

static void tp114LimiterBars (const std::shared_ptr<const OrganicInstrument>& sine, const std::shared_ptr<const OrganicInstrument>& piano)
{
    const double ceil = std::pow (10.0, (double) organics::kLimiterCeilingDb / 20.0), ceilTp = ceil * std::pow (10.0, 1.0 / 20.0);
    auto note = [] (const std::shared_ptr<const OrganicInstrument>& I, bool lim, int key, float vel, int64_t frames, int blk, int maxBlk = 512,
                    int64_t offAt = -1) {
        organics_debug::setLimiter (lim);
        I->resetPerformanceState();
        OrganicEngine e; e.prepare (kSR, maxBlk); e.setInstrument (I);
        e.noteOn (key, vel, 1, kNoDet, 5u);
        Rec r;
        if (offAt < 0) run (e, P0(), r, frames, blk);
        else { run (e, P0(), r, offAt, blk); e.noteOff (false); run (e, P0(), r, frames - offAt, blk); }
        organics_debug::setLimiter (false);
        return r;
    };
    auto pk = [] (const Rec& r) { return std::max (an::peak (r.L), an::peak (r.R)); };
    auto same = [] (const Rec& a, const Rec& b) { return a.L.size() == b.L.size() && std::memcmp (a.L.data(), b.L.data(), a.L.size() * sizeof (float)) == 0
                                                         && std::memcmp (a.R.data(), b.R.data(), a.R.size() * sizeof (float)) == 0; };
    // (a) at rest: under the ceiling the output is tp113's bit for bit (sine and the decaying piano, block sizes 1 … 777)
    {
        bool allSame = true; double worstRaw = 0; int cases = 0;
        for (auto* I : { &sine, &piano })
            for (float vel : { 0.08f, 0.2f })
                for (int blk : { 1, 64, 512, 777 })
                {
                    const auto off = note (*I, false, 60, vel, 36000, blk), on = note (*I, true, 60, vel, 36000, blk);
                    if (pk (off) >= ceil || refIsp (off.L, off.R) >= 0.95 * ceilTp) continue;
                    ++cases; worstRaw = std::max (worstRaw, pk (off));
                    allSame &= same (off, on);
                }
        bar ("limiter: under the ceiling = tp113 bit for bit", cases >= 8 && allSame,
             fmt ("%d notes (raw peaks up to %+.1f dBFS, ceiling %+.1f): %s", cases, an::db (worstRaw), an::db (ceil), allSame ? "every sample identical" : "DIFFERENT"));
    }
    // (b) hot notes never pass the ceiling — any block size (the read-ahead spans only the chunk), keys 24 … 96, note-offs
    {
        double worst = 0, worstRaw = 0, worstIsp = 0; int n = 0; std::string ispAt;
        for (auto* I : { &sine, &piano })
            for (int key = 24; key <= 108; key += 12)
                for (int blk : { 1, 17, 64, 256, 512, 777 })
                {
                    const auto on = note (*I, true, key, 1.f, 24000, blk, 512, 12000);
                    const auto off = note (*I, false, key, 1.f, 24000, 512, 512, 12000);
                    worst = std::max (worst, pk (on)); worstRaw = std::max (worstRaw, pk (off)); ++n;
                    if (blk == 512 || blk == 64 || blk == 17)
                    {
                        const double is = refIsp (on.L, on.R);
                        if (is > worstIsp) { worstIsp = is; ispAt = fmt ("%s k%d blk %d", I == &sine ? "sine" : "piano", key, blk); }
                    }
                }
        bar ("limiter: hot notes never pass the ceiling", worst <= ceil * (1.0 + 1e-6) && worstRaw > 1.5 * ceil && worstIsp <= ceilTp,
             fmt ("%d notes × block sizes 1-777: raw up to %+.2f dBFS → limited %+.4f dBFS (ceiling %+.2f), 4× ISP %+.2f (%s; bar %+.2f = 0 dBTP at the plugin output)",
                  n, an::db (worstRaw), an::db (worst), an::db (ceil), an::db (worstIsp), ispAt.c_str(), an::db (ceilTp)));
    }
    // (c) a sustained LOW note held over the ceiling: the gain settles flat — no pumping at the waveform rate (the gain's own
    //     ripple = limited / raw, 20 ms windows) and nothing added (the residual after a per-window gain match)
    {
        double worstRip = 0, worstRes = -200; std::string d;
        for (int key : { 28, 40, 52 })
        {
            const auto on = note (sine, true, key, 1.f, 96000, 512), off = note (sine, false, key, 1.f, 96000, 512);
            const auto m = mono (on), mo = mono (off);
            double lo = 1e9, hi = 0, eRes = 0, eSig = 0;
            for (int64_t s = 24000; s + 960 <= 96000; s += 960)
            {
                double xy = 0, yy = 0;
                for (int64_t i = s; i < s + 960; ++i) { xy += (double) m[(size_t) i] * mo[(size_t) i]; yy += (double) mo[(size_t) i] * mo[(size_t) i]; }
                const double g = yy > 0 ? xy / yy : 1.0;
                lo = std::min (lo, g); hi = std::max (hi, g);
                for (int64_t i = s; i < s + 960; ++i) { const double e = m[(size_t) i] - g * mo[(size_t) i]; eRes += e * e; eSig += (double) m[(size_t) i] * m[(size_t) i]; }
            }
            const double rip = an::db (hi / lo), res = 10.0 * std::log10 (eRes / std::max (1e-30, eSig) + 1e-30);
            worstRip = std::max (worstRip, rip); worstRes = std::max (worstRes, res);
            d += fmt ("k%d (%.0f Hz) GR %.2f dB · gain ripple %.3f dB · residual %.0f dB · ", key, mtof (key), -an::db (lo), rip, res);
        }
        bar ("limiter: sustained low note — flat gain, nothing added", worstRip <= 0.05 && worstRes <= -50.0, d);
    }
    // (d) the gain comes back to EXACTLY 1.0f while the note still sounds: from then on the output is tp113's sample for sample
    {
        const auto on = note (piano, true, 60, 1.f, 240000, 512), off = note (piano, false, 60, 1.f, 240000, 512);
        int64_t last = -1;
        for (size_t i = 0; i < on.L.size(); ++i) if (on.L[i] != off.L[i] || on.R[i] != off.R[i]) last = (int64_t) i;
        const double after = last >= 0 ? an::peak (off.L, last + 1, 4800) : 0.0;
        const double firstOver = [&] { for (size_t i = 0; i < off.L.size(); ++i) if (std::max (std::abs (off.L[i]), std::abs (off.R[i])) > ceil) return (double) i; return -1.0; }();
        bar ("limiter: gain back to exactly 1.0 while the note sounds", last > 0 && last < 192000 && after > 1e-3,
             fmt ("piano k60 v127: first over at %.1f ms, the last limited sample at %.1f ms, then identical to tp113 (still sounding at %+.1f dBFS)",
                  1000.0 * firstOver / kSR, 1000.0 * (double) last / kSR, an::db (after)));
    }
    // (e) no click from the gain: the HP(8k) content of a limited attack is never above the same attack unlimited (a gain step
    //     would splash HF; a smooth gain ≤ 1 only lowers it) — absolute peaks, every block size
    {
        double worst = -200; std::string d;
        for (int key : { 36, 60, 84 })
            for (int blk : { 64, 512 })
            {
                const auto on = note (piano, true, key, 1.f, 24000, blk), off = note (piano, false, key, 1.f, 24000, blk);
                const double ca = an::peak (an::highpass (mono (on), 8000.0), 256), cb = an::peak (an::highpass (mono (off), 8000.0), 256);
                worst = std::max (worst, an::db (ca / cb));
                d += fmt ("k%d/%d HP(8k) %+.1f dB re unlimited (GR %.1f dB) · ", key, blk, an::db (ca / cb), an::db (pk (off) / pk (on)));
            }
        bar ("limiter: no click from the gain (HP residual)", worst <= 0.5, d + fmt ("worst %+.2f dB", worst));
    }
    // (f) CPU per engine per 512-sample block: at rest (the peak scan) and while limiting
    {
        auto timeIt = [&] (const std::shared_ptr<const OrganicInstrument>& I, float vel, bool lim) {
            organics_debug::setLimiter (lim);
            std::vector<std::unique_ptr<OrganicEngine>> es;
            for (int k = 0; k < 8; ++k) { es.emplace_back (new OrganicEngine()); es.back()->prepare (kSR, 512); es.back()->setInstrument (I); es.back()->noteOn (48 + k, vel, 1, kNoDet, 9u); }
            std::vector<float> l (512), r (512); double best = 1e9;
            for (int rep = 0; rep < 5; ++rep)
            {
                const auto t0 = std::chrono::steady_clock::now();
                for (int b = 0; b < 60; ++b) for (auto& e : es) { std::fill (l.begin(), l.end(), 0.f); std::fill (r.begin(), r.end(), 0.f); e->render (P0(), 0.f, l.data(), r.data(), 512); }
                best = std::min (best, std::chrono::duration<double, std::micro> (std::chrono::steady_clock::now() - t0).count() / (60.0 * 8.0));
            }
            organics_debug::setLimiter (false);
            return best;
        };
        const double restOn = timeIt (sine, 0.08f, true), restOff = timeIt (sine, 0.08f, false);
        const double limOn = timeIt (sine, 1.f, true), limOff = timeIt (sine, 1.f, false);
        bar ("limiter: CPU per engine per 512 block", restOn - restOff <= 0.5 && limOn - limOff <= 12.0,
             fmt ("at rest +%.2f µs (%.2f → %.2f) · limiting +%.2f µs (%.2f → %.2f)", restOn - restOff, restOff, restOn, limOn - limOff, limOff, limOn));
    }
}

static void tp108RealBars (const juce::File& realRoot, const juce::File& fixRoot)
{
    using namespace tp108;
    setEnv ("TERRAIN_ORGANICS_DIR", realRoot.getFullPathName().toRawUTF8());
    OrganicsLibrary::get().rescan();
    auto pp = [] (float tone) { auto p = P0(); p.noise = 0.f; p.tone = tone; return p; };
    auto cen = [&] (const std::shared_ptr<const OrganicInstrument>& I, int key, float t) { return pcen (render (I, key, 80 / 127.f, pp (t), 24000)); };
    // pure tones ×1.8
    {
        struct R { const char* id; int key; } rows[] = { { "vcsl.mallets.vibraphone", 65 }, { "vcsl.mallets.glockenspiel", 84 }, { "vcsl.bells.tubular", 67 }, { "vsco2.woodwinds.flute", 84 } };
        bool ok = true; std::string d;
        for (auto& r : rows)
        {
            auto I = load (r.id);
            if (! I) { d += fmt ("%s%s missing", d.empty() ? "" : " · ", r.id); continue; }
            const double q = cen (I, r.key, 1.f) / cen (I, r.key, -1.f);
            ok &= q >= 1.8;
            d += fmt ("%s%s k%d ×%.2f", d.empty() ? "" : " · ", r.id, r.key, q);
        }
        bar ("real tp108: pure tones ×1.8+ (−1 → +1)", ok, d);
    }
    // rich ones: within ±15 % of today at ±1
    {
        struct R { const char* id; int key; double lo, hi; } rows[] = {
            { "salamander.grand.v3", 60, 385.5, 678.4 }, { "vsco2.strings.violin-section", 69, 455.3, 1432.4 },
            { "vsco2.brass.trumpet", 67, 673.7, 1305.8 }, { "freepats.guitar.nylon", 52, 185.2, 292.7 } };
        bool ok = true; std::string d;
        for (auto& r : rows)
        {
            auto I = load (r.id);
            if (! I) { d += fmt ("%s%s missing", d.empty() ? "" : " · ", r.id); continue; }
            const double a = cen (I, r.key, -1.f) / r.lo - 1.0, b = cen (I, r.key, 1.f) / r.hi - 1.0;
            ok &= std::abs (a) <= 0.15 && std::abs (b) <= 0.15;
            d += fmt ("%s%s k%d %+.1f/%+.1f %%", d.empty() ? "" : " · ", r.id, r.key, 100 * a, 100 * b);
        }
        bar ("real tp108: rich ones within ±15 % of today (−1/+1)", ok, d);
    }
    // C7 at +1: nothing new over 16 kHz
    {
        struct R { const char* id; double today; } rows[] = { { "vcsl.mallets.vibraphone", -55.1 }, { "vcsl.mallets.glockenspiel", -47.4 }, { "vsco2.woodwinds.flute", -49.4 } };
        bool ok = true; std::string d;
        for (auto& r : rows)
        {
            auto I = load (r.id);
            if (! I) { d += fmt ("%s%s missing", d.empty() ? "" : " · ", r.id); continue; }
            const double a = bandDb (render (I, 96, 80 / 127.f, pp (1.f), 24000), 16000.0, 24000.0);
            ok &= a <= std::max (r.today + 6.0, -60.0);
            d += fmt ("%s%s %+.1f dB (today %+.1f)", d.empty() ? "" : " · ", r.id, a, r.today);
        }
        bar ("real tp108: C7 at +1, energy > 16 kHz ≤ today + 6 dB", ok, d);
    }
    org::drainDeferredReleases();
    setEnv ("TERRAIN_ORGANICS_DIR", fixRoot.getFullPathName().toRawUTF8());
    OrganicsLibrary::get().rescan();
}

int main (int argc, char** argv)
{
    if (argc >= 3 && std::string (argv[1]) == "--sweep")
        return runSweep (juce::File::getCurrentWorkingDirectory().getChildFile (argv[2]), argc >= 4 ? std::atoi (argv[3]) : 12);

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
    // tp114: these gates measure the engine's DSP with the safety limiter BYPASSED (the fixtures are full-scale sines over its
    // ceiling); tp114LimiterBars switches it on for its own bars.
    organics_debug::setLimiter (false);

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
    auto noisy = load ("test.noisy");   // tp105
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
        // Human: per-note spread matches the ranges (detune ±4¢, level 0 … +3 dB (tp107b), timing 0..12 ms, tone ±1.5 dB).
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
            const bool ok = zeroSame && dl >= -4.2 && dh <= 4.2 && dh - dl > 5.0 && ll >= -0.2 && lh <= 3.2 && lh - ll > 2.0
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
            bar ("Noise: key-off noise level (0 / authored / +12 dB)", eh > 1e-3 && pre == 0.0 && std::abs (an::db (ef / eh) - 12.0) <= 0.5 && s0 == s1,
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
            at (0.f, ne, nl, n3); at (-0.45f, ge, gl, g3);   // tp107: −1 is now the 3 s swell; −0.45 (knob 72.5 %) = a 0.13 s fade, a 65 ms blend
            bar ("Attack swell: the start leans on the softer layer", ge > 0.5 && ne < 0.01 && gl < 1e-3 && std::abs (an::db (g3 / n3)) < 0.1,
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
                if (an::amp (m, s, 480, mk[k] * mtof (48)) > 0.02 * organics::outputMakeupGain()) { first[k] = 1000.0 * s / kSR; break; }   // tp113: library units
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
        auto bench = [&] (const std::shared_ptr<const OrganicInstrument>& I, int players, double& usPerBlock, double& usPerRegion, int& regions,
                          float tone = 0.3f, bool stages = false) {
            organics_debug::setToneStages (stages);
            std::vector<std::unique_ptr<OrganicEngine>> v;
            const int chord[8] = { 48, 52, 55, 59, 62, 65, 69, 72 };
            float det[16]; for (int k = 0; k < 16; ++k) det[k] = (float) ((k % 2 ? 1 : -1) * (k + 1) * 1.5);
            for (int i = 0; i < 8; ++i)
            {
                v.push_back (std::make_unique<OrganicEngine>()); v.back()->prepare (kSR, 512); v.back()->setInstrument (I);
                v.back()->noteOn (chord[i], 0.8f, players, det, (uint32_t) i + 1);
            }
            std::vector<float> l (512), r (512);
            auto p = P0(); p.tone = tone; p.noise = 0.f;
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
            organics_debug::setToneStages (true);
        };
        double b1, r1, b7, r7, bm, rm; int n1, n7, nm;
        bench (norr, 1, b1, r1, n1); bench (norr, 7, b7, r7, n7); bench (sine, 1, bm, rm, nm);
        bar ("CPU: engine core ≤ 4 µs per region per 512 block", r1 <= 4.0 && r7 <= 4.0 && rm <= 4.0,
             fmt ("stereo %.2f µs/region (players 1), %.2f (players 7) · mono %.2f  (Tone 0.3, the tilt; tp108 stages off)", r1, r7, rm));
        std::printf ("      8-note chord, stereo loop + tilt: players 1 → %.1f µs/block (%d regions) · players 7 → %.1f µs/block (%d regions) · mono players 1 → %.1f µs (%d)\n",
                     b1, n1, b7, n7, bm, nm);

        // tp108 — THE TONE STAGES' OWN COST: two identical 8-note chords, one rendered with the stages on and one with them off
        // (the tp107 tilt alone), INTERLEAVED block by block so machine load hits both alike; the minimum of 15 runs of each.
        // Steady state (the per-note spectrum measurement is over after 4 blocks; it is costed separately below).
        {
            auto ab = [&] (const std::shared_ptr<const OrganicInstrument>& I, int players, float tone) {
                std::vector<std::unique_ptr<OrganicEngine>> on, off;
                const int chord[8] = { 48, 52, 55, 59, 62, 65, 69, 72 };
                float det[16]; for (int k = 0; k < 16; ++k) det[k] = (float) ((k % 2 ? 1 : -1) * (k + 1) * 1.5);
                for (auto* v : { &on, &off })
                    for (int i = 0; i < 8; ++i)
                    {
                        v->push_back (std::make_unique<OrganicEngine>()); v->back()->prepare (kSR, 512); v->back()->setInstrument (I);
                        v->back()->noteOn (chord[i], 0.8f, players, det, (uint32_t) i + 1);
                    }
                std::vector<float> l (512), r (512);
                auto p = P0(); p.tone = tone; p.noise = 0.f;
                for (int b = 0; b < 20; ++b)
                {
                    organics_debug::setToneStages (true);  for (auto& e : on)  e->render (p, 0.f, l.data(), r.data(), 512);
                    organics_debug::setToneStages (false); for (auto& e : off) e->render (p, 0.f, l.data(), r.data(), 512);
                }
                double best[2] = { 1e30, 1e30 }; int64_t regs[2] = { 1, 1 };
                for (int rep = 0; rep < 25; ++rep)
                {
                    double us[2] = { 0, 0 }; int64_t rb[2] = { 0, 0 };
                    for (int b = 0; b < 100; ++b)
                        for (int k = 0; k < 2; ++k)
                        {
                            organics_debug::setToneStages (k == 0);
                            const auto t0 = std::chrono::steady_clock::now();
                            for (auto& e : (k == 0 ? on : off)) { e->render (p, 0.f, l.data(), r.data(), 512); rb[k] += organics_debug::lastRenderReaders(); }
                            us[k] += std::chrono::duration<double, std::micro> (std::chrono::steady_clock::now() - t0).count();
                        }
                    for (int k = 0; k < 2; ++k) if (us[k] < best[k]) { best[k] = us[k]; regs[k] = std::max<int64_t> (1, rb[k]); }
                }
                organics_debug::setToneStages (true);
                return best[0] / (double) regs[0] - best[1] / (double) regs[1];   // µs per region per 512 block
            };
            std::string s; double worstUp = 0.0, worstDn = 0.0, worst0 = 0.0;
            struct Case { const char* n; const std::shared_ptr<const OrganicInstrument>* I; int players; };
            const Case cases[] = { { "sine p1", &sine, 1 }, { "norr p1", &norr, 1 }, { "sine p7", &sine, 7 } };
            for (const auto& c : cases)
            {
                const double up = ab (*c.I, c.players, 1.f), zero = ab (*c.I, c.players, 0.f), dn = ab (*c.I, c.players, -1.f);
                worstUp = std::max (worstUp, up); worst0 = std::max (worst0, std::abs (zero)); worstDn = std::max (worstDn, dn);
                s += fmt (" %s: +1 %+.2f · 0 %+.2f · −1 %+.2f;", c.n, up, zero, dn);
            }
            bar ("CPU: Tone stages ≤ 0.5 µs extra per region (+1, a sine)", worstUp <= 0.5, fmt ("µs per region per 512 block, on − off —%s", s.c_str()));
            {
                // Tone 0 never enters the stages (one branch per block): the chord renders BIT-IDENTICALLY with them on and off,
                // and no stage ran — the timing above is only the machine's noise
                const int chord[8] = { 48, 52, 55, 59, 62, 65, 69, 72 };
                auto p = P0(); p.tone = 0.f; p.noise = 0.f;
                auto chordRun = [&] (bool stages) {
                    organics_debug::setToneStages (stages);
                    sine->resetPerformanceState();
                    std::vector<std::unique_ptr<OrganicEngine>> v;
                    for (int i = 0; i < 8; ++i) { v.push_back (std::make_unique<OrganicEngine>()); v.back()->prepare (kSR, 512); v.back()->setInstrument (sine); v.back()->noteOn (chord[i], 0.8f, 1, kNoDet, (uint32_t) i + 1); }
                    std::vector<float> out, l (512), r (512);
                    for (int b = 0; b < 60; ++b)
                    {
                        std::fill (l.begin(), l.end(), 0.f); std::fill (r.begin(), r.end(), 0.f);
                        for (auto& e : v) e->render (p, 0.f, l.data(), r.data(), 512);
                        out.insert (out.end(), l.begin(), l.end()); out.insert (out.end(), r.begin(), r.end());
                    }
                    organics_debug::setToneStages (true);
                    return out;
                };
                const int before = organics_debug::lastTone().stages;
                const auto a = chordRun (true);
                const int ran = organics_debug::lastTone().stages != before ? 1 : 0;
                const auto b = chordRun (false);
                const bool same = a.size() == b.size() && std::memcmp (a.data(), b.data(), sizeof (float) * a.size()) == 0;
                bar ("CPU: Tone 0 never enters the stages", same && ran == 0,
                     fmt ("8-note chord, 60 blocks: stages on vs off %s; timing |on − off| %.3f µs per region (the machine's noise)", same ? "bit-identical" : "DIFFER", worst0));
            }
            bar ("CPU: Tone −1 (the pure-tone low-pass) ≤ 0.5 µs extra", worstDn <= 0.5, fmt ("worst %.2f µs per region per 512 block", worstDn));
        }
        // the note's spectrum measurement: 4 real 2048-point FFTs of the lead region, one per block, once per note
        {
            OrganicEngine e; e.prepare (kSR, 512); e.setInstrument (sine);
            std::vector<float> l (512), r (512);
            auto p = P0(); p.tone = 1.f;
            double best = 1e30;
            for (int rep = 0; rep < 20; ++rep)
            {
                e.kill(); for (int b = 0; b < 4; ++b) e.render (p, 0.f, l.data(), r.data(), 512);
                e.noteOn (60, 0.8f, 1, kNoDet, 1u);
                e.render (p, 0.f, l.data(), r.data(), 512);                       // the note starts, segment 1
                double us = 0;
                for (int b = 0; b < 3; ++b)
                {
                    const auto t0 = std::chrono::steady_clock::now();
                    e.render (p, 0.f, l.data(), r.data(), 512);                   // segments 2..4 (the FFT blocks)
                    us += std::chrono::duration<double, std::micro> (std::chrono::steady_clock::now() - t0).count();
                }
                organics_debug::setToneStages (false);
                double base = 0;
                e.kill(); for (int b = 0; b < 4; ++b) e.render (p, 0.f, l.data(), r.data(), 512);
                e.noteOn (60, 0.8f, 1, kNoDet, 1u); e.render (p, 0.f, l.data(), r.data(), 512);
                for (int b = 0; b < 3; ++b)
                {
                    const auto t0 = std::chrono::steady_clock::now();
                    e.render (p, 0.f, l.data(), r.data(), 512);
                    base += std::chrono::duration<double, std::micro> (std::chrono::steady_clock::now() - t0).count();
                }
                organics_debug::setToneStages (true);
                best = std::min (best, (us - base) / 3.0);
            }
            bar ("CPU: the per-note spectrum ≤ 25 µs per FFT block", best <= 25.0, fmt ("%.1f µs extra in each of the 4 blocks after a note-on (Tone ≠ 0 only), then nothing", best));
        }
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

    // ══ tp105 — VIBRATO · A REAL RELEASE · NOISE ON/OFF · TUNING (tfix) · VELOCITY CURVE (fixture test.noisy) ══════════
    bar ("tp105 fixture test.noisy loads", noisy != nullptr, fmt ("noisy=%d (ORG_REGEN=1 writes it)", !! noisy));
    if (noisy)
    {
        auto sineRun = [&] (OrganicParams p, int64_t frames, int players = 1, int blk = 512,
                            std::function<void (OrganicParams&, int64_t)> hook = {}) {
            OrganicEngine e; e.prepare (kSR, 512); e.setInstrument (noisy);
            e.noteOn (57, 0.8f, players, kNoDet, 7u);
            Rec r; run (e, p, r, frames, blk, 0.f, hook); return mono (r);
        };
        auto pv = [] { auto p = P0(); p.tuning = 0; p.noise = 0.f; return p; };   // the pure sine (no bursts)
        // vibrato depth taper (knob 0.25 / 0.5 / 1 → 5.4 / 16.5 / 50 cents peak), rate 5.5 + 0.6·depth Hz
        {
            const float knobs[3] = { 0.25f, 0.5f, 1.f };
            double dep[3], rate[3], want[3]; bool ok = true; std::string d;
            for (int i = 0; i < 3; ++i)
            {
                auto p = pv(); p.vibrato = knobs[i];
                const auto x = sineRun (p, (int64_t) (4.0 * kSR));
                const auto st = vibStats (zcTrack (x, 0, (int64_t) x.size(), 220.0), 1.5, 3.9);
                dep[i] = st.depth; rate[i] = st.rate; want[i] = 50.0 * std::pow (knobs[i], 1.6);
                const double wantR = 5.5 + 0.6 * knobs[i];
                ok &= std::abs (dep[i] - want[i]) <= 0.08 * want[i] + 0.3 && std::abs (rate[i] - wantR) <= 0.15 && std::abs (st.mean) <= 0.3 + 0.05 * want[i];
                d += fmt ("%s%.2f → ±%.1f¢ (want %.1f) @ %.2f Hz (want %.2f), mean %+.2f¢", i ? " · " : "", knobs[i], dep[i], want[i], rate[i], wantR, st.mean);
            }
            bar ("Vibrato: depth taper + rate rise, f0-tracked", ok, d);
        }
        // rate follows the back panel (3 / 9 Hz at depth 0.5)
        {
            bool ok = true; std::string d;
            for (float hz : { 3.f, 9.f })
            {
                auto p = pv(); p.vibrato = 0.5f; p.vibRate = hz;
                const auto x = sineRun (p, (int64_t) (5.0 * kSR));
                const auto st = vibStats (zcTrack (x, 0, (int64_t) x.size(), 220.0), 1.5, 4.9);
                ok &= std::abs (st.rate - (hz + 0.3)) <= 0.15;
                d += fmt ("%s%.0f Hz → %.2f Hz (want %.2f)", hz < 5 ? "" : " · ", hz, st.rate, hz + 0.3);
            }
            bar ("Vibrato: rate 3..9 Hz from the back panel", ok, d);
        }
        // onset delay 0 / 1 s then a 250 ms fade-in: the time |dev| first passes 30 % of the full depth
        {
            bool ok = true; std::string d;
            for (float dl : { 0.f, 1.f })
            {
                auto p = pv(); p.vibrato = 1.f; p.vibDelay = dl;
                const auto x = sineRun (p, (int64_t) (3.0 * kSR));
                const auto tr = zcTrack (x, 0, (int64_t) x.size(), 220.0);
                double on = -1; for (auto& q : tr) if (q.first > 0.05 && std::abs (q.second) > 15.0) { on = q.first; break; }
                const double pre = [&] { double m = 0; for (auto& q : tr) if (q.first > 0.05 && q.first < dl) m = std::max (m, std::abs (q.second)); return m; }();
                ok &= on >= dl + 0.03 && on <= dl + 0.30 && (dl == 0.f || pre < 0.05);
                d += fmt ("%sdelay %.0f s → 15¢ reached at %.3f s (before the delay: max %.3f¢)", dl == 0.f ? "" : " · ", dl, on, pre);
            }
            bar ("Vibrato: onset delay, then a 250 ms fade-in", ok, d);
        }
        // depth 0 is the untouched path: rate / delay knobs change nothing, and the pitch is exact
        {
            auto a = pv(); auto b = pv(); b.vibRate = 9.f; b.vibDelay = 0.f;
            const auto xa = sineRun (a, 96000), xb = sineRun (b, 96000);
            const auto st = vibStats (zcTrack (xa, 0, (int64_t) xa.size(), 220.0), 0.2, 1.9);
            bar ("Vibrato 0: bit-identical path, pitch exact", xa == xb && std::abs (st.mean) < 0.02 && st.depth < 0.02,
                 fmt ("rate/delay moved at depth 0: %s · mean %+.4f¢, spread ±%.4f¢", xa == xb ? "identical" : "DIFFERENT", st.mean, st.depth));
        }
        // no zipper / clicks: the depth knob swept 0 → 1 → 0 over 3 s while held (a mod wheel)
        {
            auto p = pv(); p.vibDelay = 0.f;
            const auto x = sineRun (p, (int64_t) (4.0 * kSR), 1, 256, [] (OrganicParams& q, int64_t at) {
                const double t = (double) at / kSR; q.vibrato = (float) (t < 0.5 ? 0.0 : t < 2.0 ? (t - 0.5) / 1.5 : t < 3.5 ? 1.0 - (t - 2.0) / 1.5 : 0.0); });
            const auto ck = an::clicks (x);
            bar ("Vibrato: depth sweep 0→1→0 held, no zipper", ck.relDb <= -60.0 || ck.localRatio <= 1.5,
                 fmt ("HP(8k) residual %.1f dB re peak (local ratio %.2f, worst at %.0f ms)", ck.relDb, ck.localRatio, ck.atMs));
        }
        // Ensemble: 2 players, Human 0, no detune — lockstep would be exactly 2× one player; own rates/phases decorrelate
        {
            auto dec = [&] (float vib) {
                auto p = pv(); p.vibrato = vib; p.vibDelay = 0.f;
                const auto x2 = sineRun (p, (int64_t) (4.0 * kSR), 2), x1 = sineRun (p, (int64_t) (4.0 * kSR), 1);
                an::Buf d (x2.size()); for (size_t i = 0; i < d.size(); ++i) d[i] = x2[i] - 2.f * x1[i];
                return an::db (an::rms (d, (int64_t) (1.0 * kSR), (int64_t) (2.9 * kSR)) / an::rms (x2, (int64_t) (1.0 * kSR), (int64_t) (2.9 * kSR)));
            };
            const double d0 = dec (0.f), d5 = dec (0.5f);
            bar ("Vibrato Ensemble: players differ (shimmer, not lockstep)", d0 < -120.0 && d5 > -30.0,
                 fmt ("2 players − 2× one player: vibrato 0 %.0f dB (identical = lockstep) · vibrato 0.5 %.1f dB re the pair", d0, d5));
        }
        // A REAL RELEASE: −60 dB time vs the Release knob (amp release 0) and vs the amp release (knob 0), looped tone
        {
            auto rel = [&] (float knob, float ampRel) {
                OrganicEngine e; e.prepare (kSR, 512); e.setInstrument (noisy);
                auto p = pv(); p.release = knob; p.ampRelease = ampRel; p.noise = 0.f;   // the tone's release (key-off burst off)
                e.noteOn (57, 0.8f, 1, kNoDet, 7u);
                Rec r; run (e, p, r, 24000, 512); e.noteOff (false);
                run (e, p, r, (int64_t) (16.0 * kSR), 512);
                return t60 (mono (r), 24000);
            };
            const float knobs[5] = { 0.f, 0.25f, 0.5f, 0.75f, 1.f };
            const double wantK[5] = { 0.02, 0.1, 0.5, 0.5 * std::sqrt (24.0), 12.0 };
            bool ok = true; std::string d;
            for (int i = 0; i < 5; ++i)
            {
                const double t = rel (knobs[i], 0.f);
                ok &= t > 0 && std::abs (t - wantK[i]) <= 0.12 * wantK[i] + 0.012;
                d += fmt ("%s%.2f→%.3fs (%.3f)", i ? " " : "knob ", knobs[i], t, wantK[i]);
            }
            bar ("Release knob: 20 ms · authored 0.5 s · 12 s (−60 dB)", ok, d);
            ok = true; d.clear();
            for (float a : { 0.3f, 2.f, 5.f })
            {
                const double t = rel (0.f, a);
                ok &= t > 0 && std::abs (t - a) <= 0.12 * a + 0.012;
                d += fmt ("%samp %.1fs→%.3fs", a < 1 ? "" : " · ", a, t);
            }
            const double both = rel (0.75f, 0.3f);   // max(): the knob's 2.45 s wins over a 0.3 s amp release
            ok &= std::abs (both - wantK[3]) <= 0.12 * wantK[3];
            bar ("Release = max(amp-env release, knob time)", ok, d + fmt (" · knob .75 + amp 0.3 s → %.3f s", both));
        }
        // tp105b THE TAIL RELEASE on the fixture piano (12 dB/s decay, 4 s, tail loop at 2.4 s): a 12 s release crosses into
        // the tail loop and lands −60 dB at 12 s; a release the recording covers takes the round-1 path (no tail release)
        {
            auto pr = [&] (float knob, float ampRel, int& tails) {
                const int t0 = organics_debug::tailReleases();
                OrganicEngine e; e.prepare (kSR, 512); e.setInstrument (piano);
                auto p = P0(); p.release = knob; p.ampRelease = ampRel; p.noise = 0.f;
                e.noteOn (43, 0.8f, 1, kNoDet, 7u);
                Rec r; run (e, p, r, 48000, 512); e.noteOff (false); run (e, p, r, (int64_t) (16.0 * kSR), 512);
                tails = organics_debug::tailReleases() - t0;
                return t60 (mono (r), 48000);
            };
            int n1 = 0, n0 = 0, n2 = 0;
            const double t1 = pr (1.f, 0.f, n1), t0 = pr (0.f, 0.f, n0), t2 = pr (0.f, 2.f, n2);
            bar ("Tail release: 12 s past a 4 s recording (fixture)", n1 == 1 && std::abs (t1 - 12.0) <= 0.4 && n0 == 0 && n2 == 0 && t0 < 0.1,
                 fmt ("knob 1 → %.2f s (tail release %d) · knob 0 → %.3f s, amp 2 s → %.2f s: no tail release (%d, %d), the round-1 path", t1, n1, t0, t2, n0, n2));
        }
        // NOISE on / off: the on-burst (3 kHz) with the note, the off-burst (5 kHz) at note-off; knob 0 / 0.5 / 1
        {
            auto nz = [&] (float knob, double& on, double& off, double& onLate, double& offEarly) {
                OrganicEngine e; e.prepare (kSR, 512); e.setInstrument (noisy);
                auto p = pv(); p.noise = knob;
                e.noteOn (60, 0.8f, 1, kNoDet, 7u);
                Rec r; run (e, p, r, 14400, 512); e.noteOff (false); run (e, p, r, 14400, 512);
                const auto x = mono (r);
                on = an::amp (x, 480, 3840, 3000.0); offEarly = an::amp (x, 9600, 3840, 5000.0);
                off = an::amp (x, 14400 + 480, 3840, 5000.0); onLate = an::amp (x, 14400 + 480, 3840, 3000.0);
            };
            double on0, off0, a, b, on5, off5, onL, offE, on1, off1;
            nz (0.f, on0, off0, a, b); nz (0.5f, on5, off5, onL, offE); nz (1.f, on1, off1, a, b);
            const double u = organics::outputMakeupGain();   // tp113: the floors are in library units (the engine plays +20 dB)
            const bool ok = on0 < 1e-6 * u && off0 < 1e-6 * u && on5 > 0.05 * u && off5 > 0.05 * u && onL < 0.01 * on5 && offE < 1e-6 * u
                         && std::abs (an::db (on1 / on5) - 12.0) <= 0.5 && std::abs (an::db (off1 / off5) - 12.0) <= 0.5;
            bar ("Noise: trig on with the note, off at note-off, 0/authored/+12", ok,
                 fmt ("on-burst %.1f dBFS · off-burst %.1f dBFS (knob 0: %.0f / %.0f dB) · knob 1: %+.2f / %+.2f dB re 0.5 · off-burst before note-off %.0f dB",
                      an::db (on5), an::db (off5), an::db (on0), an::db (off0), an::db (on1 / on5), an::db (off1 / off5), an::db (offE)));
        }
        // TUNING: tfix +25 cents applied at Equal (1), ignored As recorded (0)
        {
            auto f = [&] (int tuning) {
                auto p = P0(); p.tuning = tuning;
                const auto x = sineRun (p, 72000);
                return vibStats (zcTrack (x, 0, (int64_t) x.size(), 220.0), 0.3, 1.4).mean;
            };
            const double c0 = f (0), c1 = f (1);
            bar ("Tuning: tfix applied at Equal, not As recorded", std::abs (c0) < 0.05 && std::abs (c1 - 25.0) < 0.05,
                 fmt ("As recorded %+.3f¢ · Equal %+.3f¢ (tfix +25)", c0, c1));
        }
        // VELOCITY CURVE: Soft / Hard at velocity 64 = Linear at the curve's velocity (0.55 / 1.8 power), bit for bit;
        // Soft louder than Linear louder than Hard (Velocity knob 1 so the level follows the velocity)
        {
            auto rn = [&] (int curve, float vel) {
                OrganicEngine e; e.prepare (kSR, 512); e.setInstrument (noisy);
                auto p = pv(); p.velo = 1.f; p.velCurve = curve;
                e.noteOn (57, vel, 1, kNoDet, 7u);
                Rec r; run (e, p, r, 24000, 512); return mono (r);
            };
            const float v = 64.f / 127.f;
            const auto s0 = rn (0, v), s1 = rn (1, v), s2 = rn (2, v);
            const auto l0 = rn (1, std::pow (v, 0.55f)), l2 = rn (1, std::pow (v, 1.8f));
            const double d0 = an::db (an::rms (s0, 9600, 9600) / an::rms (s1, 9600, 9600)), d2 = an::db (an::rms (s2, 9600, 9600) / an::rms (s1, 9600, 9600));
            bar ("Velocity curve: Soft > Linear > Hard (vel 64)", s0 == l0 && s2 == l2 && d0 > 2.0 && d2 < -3.0,
                 fmt ("Soft %+.2f dB · Hard %+.2f dB re Linear · each = Linear at v^0.55 / v^1.8: %s / %s", d0, d2, s0 == l0 ? "identical" : "DIFFERENT", s2 == l2 ? "identical" : "DIFFERENT"));
        }
        tp107Bars (noisy, piano);
        tp108Bars (noisy, sine, norr);
        tp114LimiterBars (sine, piano);
    }
    if (argc >= 3 && juce::File (juce::File::getCurrentWorkingDirectory().getChildFile (argv[2])).isDirectory())
        tp108RealBars (juce::File::getCurrentWorkingDirectory().getChildFile (argv[2]), fixRoot);
    if (argc >= 3 && juce::File (juce::File::getCurrentWorkingDirectory().getChildFile (argv[2])).isDirectory())
    {
        const auto realRoot = juce::File::getCurrentWorkingDirectory().getChildFile (argv[2]);
        std::printf ("── real data: %s ──\n", realRoot.getFullPathName().toRawUTF8());
        setEnv ("TERRAIN_ORGANICS_DIR", realRoot.getFullPathName().toRawUTF8());
        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        auto sal = load ("salamander.grand.v3");
        const double tSal = (juce::Time::getMillisecondCounterHiRes() - t0) / 1000.0;
        auto vio = load ("vsco2.strings.violin-section"), solo = load ("vsco2.strings.solo-violin");
        auto vibes = load ("vcsl.mallets.vibraphone");   // tp105 release table (absent → reported, not failed)
        auto glocken = load ("vcsl.mallets.glockenspiel");   // tp105b
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
                Rec r; std::vector<int64_t> offs;
                for (int i = 0; i < 60; ++i)
                {
                    e.noteOn (48 + rnd.nextInt (25), 0.4f + 0.5f * rnd.nextFloat(), 1, kNoDet, (uint32_t) i);
                    run (e, P0(), r, 4800 + rnd.nextInt (19200), 64 + rnd.nextInt (448));
                    offs.push_back ((int64_t) r.L.size());
                    e.noteOff (false);
                    run (e, P0(), r, 9600 + rnd.nextInt (9600), 64 + rnd.nextInt (448));
                }
                const auto ck = an::clicks (mono (r));
                // tp105: the claim is about the NOTE-OFFS. The whole-buffer argmax lands on a piano ATTACK (a new note's hammer
                // transient, 30 ms after its note-on) and flips between two equal −46.0 dB peaks on a 1e-8 difference — so the
                // bar now reads the same metric inside the 60 ms after each note-off, where a handoff click would live.
                const auto x = mono (r); const auto h = an::highpass (x, 8000.0); const double sp = an::peak (x);
                double hp = 0; int64_t at = 0;
                for (auto o : offs) for (int64_t i = std::max<int64_t> (256, o - 240); i < std::min<int64_t> ((int64_t) h.size(), o + 2880); ++i)
                    if (std::abs (h[(size_t) i]) > hp) { hp = std::abs (h[(size_t) i]); at = i; }
                std::vector<double> dd;
                for (int64_t i = std::max<int64_t> (1, at - 2400); i < std::min<int64_t> ((int64_t) x.size(), at + 2400); ++i) dd.push_back (std::abs ((double) x[(size_t) i] - x[(size_t) i - 1]));
                std::nth_element (dd.begin(), dd.begin() + (long) dd.size() / 2, dd.end());
                const double lr = std::abs ((double) x[(size_t) at] - x[(size_t) at - 1]) / std::max (1e-20, dd[dd.size() / 2]);
                const double offDb = an::db (hp / std::max (1e-20, sp));
                bar ("real no clicks: salamander 60 notes, offs at random phases", offDb <= -60 || lr <= 1.5,
                     fmt ("after the 60 note-offs: HP residual %.1f dB re peak (local ratio %.2f, at %.0f ms) · whole render %.1f dB (worst at %.0f ms, a note's attack)",
                          offDb, lr, 1000.0 * (double) at / kSR, ck.relDb, ck.atMs));
            }

            // tp105/tp105b — THE RELEASE ON REAL INSTRUMENTS: the −60 dB point after note-off for knob 0 / 0.5 / 1 × amp release
            // 0.3 / 2 / 5 s (release = max of the two), Salamander C3 · violin section G4 · vibraphone F4 · glockenspiel G5.
            // Max: "at 100 % … at least 10 seconds" — on EVERY instrument: a release longer than the recording crosses into the
            // region's compile-time tail loop (level-matched crossfades) and keeps decaying there. Per case: no click after the
            // note-off (HP 8k residual re the note), and no loop flutter (10 ms RMS, detrended over 0.4 s windows, < 0.5 dB).
            {
                auto glock = glocken;
                struct Inst { const char* nm; std::shared_ptr<const OrganicInstrument> I; int key; };
                const Inst ins[4] = { { "salamander C3", sal, 48 }, { "violin sect G4", vio, 67 }, { "vibraphone F4", vibes, 65 }, { "glockenspiel G5", glock, 79 } };
                bool okK = true, okA = true, okC = true, okF = true, okH = true;
                double worstRip = 0, worstClk = -300, worstLr = 0; std::string ripWho, clkWho;
                std::printf ("      INFO release (−60 dB re note-off, s; key held 1 s)   rows knob 0 / 0.5 / 1 · columns amp 0.3 / 2 / 5 s\n");
                for (const auto& in : ins)
                {
                    if (! in.I) { std::printf ("      INFO release: %s not in the compiled library\n", in.nm); okK = false; continue; }
                    // returns t60; fills flutter (dB) and click metrics of the release part
                    bool tail = false;
                    auto rel = [&] (float knob, float ampRel, int64_t hold, double& rip, double& clk, double& lr) {
                        const int tr0 = organics_debug::tailReleases();
                        OrganicEngine e; e.prepare (kSR, 512); e.setInstrument (in.I);
                        auto p = P0(); p.release = knob; p.ampRelease = ampRel; p.noise = 0.f;   // the tone's release (key-off noise off)
                        e.noteOn (in.key, 0.8f, 1, kNoDet, 7u);
                        Rec r; run (e, p, r, hold, 512); e.noteOff (false);
                        run (e, p, r, (int64_t) (16.0 * kSR), 512);
                        const auto x = mono (r);
                        const double t = t60 (x, hold);
                        // flutter: the STEREO level (L² + R², what the ears get; a stereo piano's mono sum drifts ±1 dB with its own
                        // L/R correlation), 40 ms RMS (hop 10 ms: longer than a low note's period, shorter than a tail loop) from
                        // 0.3 s after the off until 45 dB down, detrended per 0.4 s (linear fit)
                        an::Buf pw (r.L.size());
                        for (size_t q = 0; q < pw.size(); ++q) pw[q] = std::sqrt (0.5f * (r.L[q] * r.L[q] + r.R[q] * r.R[q]));
                        std::vector<double> db;
                        double ref = an::rms (pw, hold - 4800, 4800);
                        for (int64_t w = hold + (int64_t) (0.3 * kSR); w + 1920 < (int64_t) pw.size(); w += 480)
                        {
                            const double v = an::rms (pw, w, 1920);
                            if (v < ref * 0.0056) break;           // −45 dB: below that the int16 floor and the fade take over
                            db.push_back (an::db (v));
                        }
                        rip = 0;
                        for (size_t w0 = 0; w0 + 40 <= db.size(); w0 += 20)
                        {
                            double sx = 0, sy = 0, sxx = 0, sxy = 0; const int N = 40;
                            for (int k = 0; k < N; ++k) { sx += k; sy += db[w0 + (size_t) k]; sxx += (double) k * k; sxy += k * db[w0 + (size_t) k]; }
                            const double sl = (N * sxy - sx * sy) / (N * sxx - sx * sx), ic = (sy - sl * sx) / N;
                            double lo = 1e9, hi = -1e9;
                            for (int k = 0; k < N; ++k) { const double rr = db[w0 + (size_t) k] - (ic + sl * k); lo = std::min (lo, rr); hi = std::max (hi, rr); }
                            rip = std::max (rip, hi - lo);
                        }
                        // click: HP(8k) peak after the off re the whole render's peak, and its local |Δ| ratio
                        const auto h = an::highpass (x, 8000.0); const double sp = an::peak (x);
                        double hp = 0; int64_t at = hold;
                        for (int64_t i = hold; i < (int64_t) h.size(); ++i) if (std::abs (h[(size_t) i]) > hp) { hp = std::abs (h[(size_t) i]); at = i; }
                        std::vector<double> dd;
                        for (int64_t i = std::max<int64_t> (1, at - 2400); i < std::min<int64_t> ((int64_t) x.size(), at + 2400); ++i) dd.push_back (std::abs ((double) x[(size_t) i] - x[(size_t) i - 1]));
                        std::nth_element (dd.begin(), dd.begin() + (long) dd.size() / 2, dd.end());
                        lr = std::abs ((double) x[(size_t) at] - x[(size_t) at - 1]) / std::max (1e-20, dd[dd.size() / 2]);
                        clk = an::db (hp / std::max (1e-20, sp));
                        tail = organics_debug::tailReleases() != tr0;
                        return t;
                    };
                    auto T = [] (double t) { return t < 0 ? 1.0e9 : t; };   // −1 = still above −60 dB after 16 s
                    double tab[3][3], flut[3][3];
                    const float knobs[3] = { 0.f, 0.5f, 1.f }, amps[3] = { 0.3f, 2.f, 5.f };
                    for (int kk = 0; kk < 3; ++kk)
                        for (int aa = 0; aa < 3; ++aa)
                        {
                            double rip, clk, lr;
                            tab[kk][aa] = rel (knobs[kk], amps[aa], 48000, rip, clk, lr);
                            // flutter + click are the TAIL release's bars: the instruments whose regions play the tail loop (decaying);
                            // a looped (bowed) region keeps its authored loop and its own bow movement (reported, not gated)
                            flut[kk][aa] = tail ? rip : -1.0;
                            if (in.I != vio && tail)
                            {
                                okF &= rip < 0.5; okC &= clk <= -60.0 || lr <= 1.5;
                                if (rip > worstRip) { worstRip = rip; ripWho = fmt ("%s knob %.1f amp %.1f", in.nm, knobs[kk], amps[aa]); }
                                if (clk > worstClk) { worstClk = clk; worstLr = lr; clkWho = fmt ("%s knob %.1f amp %.1f", in.nm, knobs[kk], amps[aa]); }
                            }
                            else std::printf ("      INFO violin (authored sustain loops, not the tail path) knob %.1f amp %.1f: ripple %.2f dB, HP %.1f dB lr %.2f\n", knobs[kk], amps[aa], rip, clk, lr);
                        }
                    double rh, ch, lh;
                    const double held = rel (1.f, 0.f, (int64_t) (3.5 * kSR), rh, ch, lh);   // key held PAST the tail loop: the jump in
                    okH &= T (held) >= 10.0 && (in.I == vio || (rh < 0.5 && (ch <= -60.0 || lh <= 1.5)));
                    std::printf ("      INFO %-15s tail-release flutter (dB; -1 = the short, non-tail path):  %5.2f %5.2f %5.2f | %5.2f %5.2f %5.2f | %5.2f %5.2f %5.2f\n", in.nm,
                                 flut[0][0], flut[0][1], flut[0][2], flut[1][0], flut[1][1], flut[1][2], flut[2][0], flut[2][1], flut[2][2]);
                    std::printf ("      INFO %-15s knob 0 %6.2f %6.2f %6.2f | 0.5 %6.2f %6.2f %6.2f | 1 %6.2f %6.2f %6.2f  · held 3.5 s, knob 1: %6.2f s (flutter %.2f dB)\n",
                                 in.nm, tab[0][0], tab[0][1], tab[0][2], tab[1][0], tab[1][1], tab[1][2], tab[2][0], tab[2][1], tab[2][2], held, rh);
                    // knob 0: the amp release sets it. Shorter than the recording, the release multiplies the natural decay (as
                    // round 1: −60 dB lands a little early on a fast-decaying mallet); longer, the tail release lands it exactly
                    for (int aa = 0; aa < 3; ++aa) okA &= T (tab[0][aa]) >= amps[aa] * 0.5;
                    okA &= std::abs (T (tab[0][2]) - 5.0) <= 0.6 || in.I == vio;
                    okA &= T (tab[0][2]) > T (tab[0][1]) && T (tab[0][1]) > T (tab[0][0]);
                    okK &= T (tab[2][0]) >= 10.0 && T (tab[2][0]) >= T (tab[1][0]) - 0.05;
                }
                bar ("real release: knob 1 ≥ 10 s on every instrument", okK, "see the INFO table above (rows knob, columns amp)");
                bar ("real release: the amp release lengthens every instrument", okA, "knob 0: amp 0.3 → 2 → 5 s, each longer; amp 5 s lands at 5 s ± 0.6 (the tail release), piano included");
                bar ("real release: no click after the note-off", okC, fmt ("worst HP(8k) %.1f dB re peak, local ratio %.2f (%s)", worstClk, worstLr, clkWho.c_str()));
                bar ("real release: no loop flutter (< 0.5 dB)", okF, fmt ("worst detrended 40 ms stereo RMS ripple (tail releases) %.2f dB (%s)", worstRip, ripWho.c_str()));
                bar ("real release: a note held past its tail loop still rings ≥ 10 s", okH, "key held 3.5 s, knob 1 (the crossfade into the loop from beyond it)");
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
