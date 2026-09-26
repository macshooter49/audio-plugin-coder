// ══════════════════════════════════════════════════════════════════════════════════════════════
//  tp114 — THE ORGANICS SAFETY LIMITER, measured at the PLUGIN OUTPUT (the shipping processBlock, the installed library).
//
//    Tests/organics_limiter.sh [mode ...]      (build Terrain first; links libTerrain_SharedCode.a)
//
//  The limiter lives in the engine (OrganicEngine.cpp, organics::kLimiterCeilingDb); the engine-level proof is
//  Tests/organics_audit.sh peaks (LPEAK) / lim. This harness checks what that ceiling means where Max hears it:
//    gain  <id> <key> <vel>            the engine → plugin-output offset on the default path (osc Volume, pan, master):
//                                      plugin peak / ISP / LUFS vs the SAME note rendered by the engine alone
//    keys  <id> <vels> [artic]         every key × every RR take at the plugin output (sample peak + 4× ISP), limiter on
//    wav   <id> <key> <vel> <dir>      before / after WAV pair (the limiter off = the tp113 engine, on = tp114) + GR stats
//    layer <id> <key> <vel>            two Organics oscs on the same note (each limited, the SUM is not) and Organics + WT
//  Human 0 (deterministic), Noise at its default 0.5, every other control at the init patch's default.
// ══════════════════════════════════════════════════════════════════════════════════════════════
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_dsp/juce_dsp.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include <atomic>
#include <thread>
#include <mutex>
#include <shared_mutex>
#include <condition_variable>
#include <vector>
#include <array>
#include <deque>
#include <list>
#include <set>
#include <map>
#include <unordered_map>
#include <memory>
#include <functional>
#include <string>
#include <sstream>
#include <fstream>
#include <random>
#include <optional>
#include <bit>
#include <span>
#include <chrono>
#include <cmath>
#include <cstring>
#include <algorithm>
#include <numeric>
#include <tuple>
#include <CoreFoundation/CoreFoundation.h>
#define private public
#define protected public
#include "PluginProcessor.h"
#undef private
#undef protected
#include "organics/OrganicEngine.h"
#include "organics/OrganicsLibrary.h"

static constexpr double SR = 48000.0; static constexpr int BLK = 512;
using Buf = std::vector<float>;
static double dbOf (double r) { return 20.0 * std::log10 (std::max (1e-12, r)); }
static void setP (TerrainAudioProcessor& p, const char* id, float plain)
{
    auto* prm = p.apvts.getParameter (id);
    if (prm == nullptr) { std::printf ("!! no param %s\n", id); std::exit (2); }
    prm->setValueNotifyingHost (prm->convertTo0to1 (plain));
}

struct Inst
{
    std::unique_ptr<TerrainAudioProcessor> p; juce::AudioBuffer<float> buf { 2, BLK };
    Buf L, R;
    Inst() { p = std::make_unique<TerrainAudioProcessor>(); p->setPlayConfigDetails (0, 2, SR, BLK); p->prepareToPlay (SR, BLK); }
    void block (std::initializer_list<std::tuple<int,int,int>> ev = {})   // (note, on, vel)
    {
        buf.clear(); juce::MidiBuffer m;
        for (auto e : ev) m.addEvent (std::get<1> (e) ? juce::MidiMessage::noteOn (1, std::get<0> (e), (juce::uint8) std::get<2> (e))
                                                      : juce::MidiMessage::noteOff (1, std::get<0> (e)), 0);
        p->processBlock (buf, m);
        L.insert (L.end(), buf.getReadPointer (0), buf.getReadPointer (0) + BLK);
        R.insert (R.end(), buf.getReadPointer (1), buf.getReadPointer (1) + BLK);
    }
    void tick (int n = 1) { for (int i = 0; i < n; ++i) { CFRunLoopRunInMode (kCFRunLoopDefaultMode, 0.002, false); p->timerCallback(); } }
    void run (double sec) { const int n = (int) std::ceil (sec * SR / BLK); for (int b = 0; b < n; ++b) { block(); if (b & 1) tick(); } }
    bool waitLoaded (int osc, double maxSec = 20.0)
    {
        const double t0 = juce::Time::getMillisecondCounterHiRes();
        while (juce::Time::getMillisecondCounterHiRes() - t0 < maxSec * 1000.0)
        {
            tick(); block();
            const auto st = p->organicSlot (osc).status;
            if (st != "loading" && ! p->orgSlot_[osc].publishPending && ! (p->orgMailState_[osc].load() != 0)) { block(); return st == "ok"; }
        }
        return false;
    }
    void clear() { L.clear(); R.clear(); }
};

static bool useOrganic (Inst& a, int osc, const juce::String& id)
{
    setP (*a.p, ParameterIDs::kOsc_ENABLE[osc], 1.f);
    setP (*a.p, ParameterIDs::kOsc_ENGINE[osc], 7.f);
    a.tick (3);
    (void) a.p->organicsSetInstrument (osc, id);
    const bool ok = a.waitLoaded (osc);
    setP (*a.p, ParameterIDs::kOsc_ORG_HUMAN[osc], 0.f);
    if (const char* nz = std::getenv ("ORG_LIM_NOISE")) setP (*a.p, ParameterIDs::kOsc_ORG_NOISE[osc], (float) std::atof (nz));
    a.run (0.05); a.clear();
    return ok;
}

// ── measures ──
static double peakOf (const Buf& L, const Buf& R) { double p = 0; for (size_t i = 0; i < L.size(); ++i) p = std::max ({ p, (double) std::abs (L[i]), (double) std::abs (R[i]) }); return p; }
/** 4× oversampled true peak (the EBU R128 / BS.1770 meter design: Hann-windowed sinc over ±6 samples), between every pair where
    either sample is within 6 dB of the loudest one (an inter-sample peak can only exceed its neighbours by a few dB). */
static double ispOnlyLoud (const Buf& L, const Buf& R, double /*unused*/)
{
    constexpr int K = 4, T = 6;    // the EBU R128 / BS.1770 true-peak meter design: 4×, a Hann-windowed sinc over ±6 samples (49 taps at 4×)
    static double h[K][2 * T]; static bool init = false;
    if (! init)
    {
        for (int ph = 0; ph < K; ++ph)
            for (int t = -T; t < T; ++t)
            {
                // x(i + ph/K) = Σ_t x[i − t] · sinc(t + ph/K)
                const double u = (double) t + (double) ph / K;
                const double s = u == 0.0 ? 1.0 : std::sin (M_PI * u) / (M_PI * u);
                const double w = 0.5 + 0.5 * std::cos (M_PI * u / T);
                h[ph][t + T] = s * w;
            }
        init = true;
    }
    auto one = [] (const Buf& x) {
        double pk = 0; const int n = (int) x.size();
        for (int i = 0; i < n; ++i) pk = std::max (pk, (double) std::abs (x[(size_t) i]));
        double best = pk;
        for (int i = 0; i + 1 < n; ++i)
        {
            if (std::max (std::abs (x[(size_t) i]), std::abs (x[(size_t) i + 1])) < 0.5 * pk) continue;
            for (int ph = 1; ph < K; ++ph)
            {
                double acc = 0;
                for (int t = -T; t < T; ++t) { const int j = i - t; if (j >= 0 && j < n) acc += x[(size_t) j] * h[ph][t + T]; }
                best = std::max (best, std::abs (acc));
            }
        }
        return best;
    };
    return std::max (one (L), one (R));
}
static double lufs (const Buf& L, const Buf& R, int64_t s, int64_t n)
{
    auto kw = [] (const Buf& x, int64_t s0, int64_t n0) {
        double f0 = 1681.974450955533, G = 3.999843853973347, Q = 0.7071752369554196;
        double K = std::tan (M_PI * f0 / SR), Vh = std::pow (10.0, G / 20.0), Vb = std::pow (Vh, 0.4996667741545416);
        double a0 = 1.0 + K / Q + K * K;
        const double b1[3] = { (Vh + Vb * K / Q + K * K) / a0, 2.0 * (K * K - Vh) / a0, (Vh - Vb * K / Q + K * K) / a0 };
        const double a1[3] = { 1.0, 2.0 * (K * K - 1.0) / a0, (1.0 - K / Q + K * K) / a0 };
        f0 = 38.13547087602444; Q = 0.5003270373238773; K = std::tan (M_PI * f0 / SR); a0 = 1.0 + K / Q + K * K;
        const double a2[3] = { 1.0, 2.0 * (K * K - 1.0) / a0, (1.0 - K / Q + K * K) / a0 };
        double x1 = 0, x2 = 0, y1 = 0, y2 = 0, u1 = 0, u2 = 0, z1 = 0, z2 = 0, acc = 0; int64_t c = 0;
        for (int64_t i = 0; i < s0 + n0 && i < (int64_t) x.size(); ++i)
        {
            const double in = x[(size_t) i];
            const double y = b1[0] * in + b1[1] * x1 + b1[2] * x2 - a1[1] * y1 - a1[2] * y2;
            x2 = x1; x1 = in; y2 = y1; y1 = y;
            const double z = y - 2.0 * u1 + u2 - a2[1] * z1 - a2[2] * z2;
            u2 = u1; u1 = y; z2 = z1; z1 = z;
            if (i >= s0) { acc += z * z; ++c; }
        }
        return c > 0 ? acc / (double) c : 0.0;
    };
    const double p = kw (L, s, n) + kw (R, s, n);
    return p > 1e-20 ? -0.691 + 10.0 * std::log10 (p) : -200.0;
}
static int64_t onset (const Buf& L, const Buf& R)
{
    double pk = 0; for (size_t i = 0; i < L.size(); ++i) pk = std::max (pk, std::abs (0.5 * ((double) L[i] + R[i])));
    const double lim = pk * std::pow (10.0, -24.0 / 20.0);
    for (size_t i = 0; i < L.size(); ++i) if (std::abs (0.5 * ((double) L[i] + R[i])) >= lim) return (int64_t) i;
    return 0;
}

// one note through the processor (the osc chosen by useOrganic), hold then release
static void playNote (Inst& a, int key, int vel, double hold = 1.0, double tail = 1.5)
{
    a.clear(); a.block ({ { key, 1, vel } }); a.run (hold); a.block ({ { key, 0, 0 } }); a.run (tail);
}
// the same note through the engine alone (the processor's instrument, Human 0, Noise 0.5, 1 player)
static void engineNote (const std::shared_ptr<const tw::OrganicInstrument>& I, int key, int vel, Buf& L, Buf& R, double hold = 1.0, double tail = 1.5)
{
    I->resetPerformanceState();
    tw::OrganicEngine e; e.prepare (SR, BLK); e.setInstrument (I);
    tw::OrganicParams q; q.human = 0.f; if (const char* nz = std::getenv ("ORG_LIM_NOISE")) q.noise = (float) std::atof (nz);
    const float det[16] = {};
    e.noteOn (key, (float) vel / 127.f, 1, det, 0x9e3779b9u);
    L.clear(); R.clear(); Buf l (BLK), r (BLK);
    const int nh = (int) std::ceil (hold * SR / BLK) + 1, nt = (int) std::ceil (tail * SR / BLK) + 1;
    for (int b = 0; b < nh + nt; ++b)
    {
        if (b == nh) e.noteOff (false);
        std::fill (l.begin(), l.end(), 0.f); std::fill (r.begin(), r.end(), 0.f);
        e.render (q, 0.f, l.data(), r.data(), BLK);
        L.insert (L.end(), l.begin(), l.end()); R.insert (R.end(), r.begin(), r.end());
    }
    e.kill(); e.setInstrument (nullptr);
}

static void writeWav (const juce::File& f, const Buf& L, const Buf& R)
{
    // 32-bit float: the "before" file keeps its overs exactly as a DAW's float path carries them
    f.deleteFile();
    juce::WavAudioFormat fmtW;
    std::unique_ptr<juce::OutputStream> os (f.createOutputStream().release());
    const auto opts = juce::AudioFormatWriterOptions{}.withSampleRate (SR).withNumChannels (2).withBitsPerSample (32)
                                                       .withSampleFormat (juce::AudioFormatWriterOptions::SampleFormat::floatingPoint);
    if (os != nullptr)
        if (auto w = fmtW.createWriterFor (os, opts)) { const float* ch[2] = { L.data(), R.data() }; w->writeFromFloatArrays (ch, 2, (int) L.size()); }
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    setenv ("TERRAIN_DETERMINISTIC", "1", 1);
    if (std::getenv ("TERRAIN_ORGANICS_DIR") == nullptr)
    {
        juce::File lib ("~/Library/WavesCrate/TerrainInstrument/Organics");
        if (! lib.isDirectory()) lib = juce::File ("~/Library/WavesCrate/Terrain/Organics");
        setenv ("TERRAIN_ORGANICS_DIR", lib.getFullPathName().toRawUTF8(), 1);
    }
    tw::OrganicsLibrary::get().rescan();
    const std::string mode = argc > 1 ? argv[1] : "gain";
    const juce::String id = argc > 2 ? argv[2] : "vcsl.mallets.mbira";
    const double ceilDb = (double) tw::organics::kLimiterCeilingDb - (double) tw::organics::kDefaultPathHeadroomDb;   // at the plugin output

    if (mode == "gain")
    {
        const int key = argc > 3 ? std::atoi (argv[3]) : 60, vel = argc > 4 ? std::atoi (argv[4]) : 100;
        for (int lim : { 0, 1 })
        {
            tw::organics_debug::setLimiter (lim != 0);
            Inst a; if (! useOrganic (a, 0, id)) { std::printf ("LOADFAIL %s\n", id.toRawUTF8()); return 1; }
            playNote (a, key, vel);
            Buf eL, eR; engineNote (a.p->orgSlot_[0].inst, key, vel, eL, eR);
            const int64_t po = onset (a.L, a.R), eo = onset (eL, eR);
            const double pl = lufs (a.L, a.R, po, (int64_t) SR), el = lufs (eL, eR, eo, (int64_t) SR);
            const double pp = dbOf (peakOf (a.L, a.R)), ep = dbOf (peakOf (eL, eR));
            std::printf ("GAIN %s k%d v%d limiter %s · plugin peak %+.2f dBFS (ISP %+.2f) LUFS %.2f · engine peak %+.2f LUFS %.2f · offset peak %+.2f LUFS %+.2f\n",
                         id.toRawUTF8(), key, vel, lim ? "on " : "off", pp, dbOf (ispOnlyLoud (a.L, a.R, std::pow (10.0, ceilDb / 20.0))), pl, ep, el, pp - ep, pl - el);
            if (std::getenv ("ORG_LIM_DIAG"))
            {
                // the path as a filter: per-channel least-squares gain + residual at the best lag (−64 … 64), and the band ratios
                for (int ch = 0; ch < 2; ++ch)
                {
                    const Buf& y = ch ? a.R : a.L; const Buf& x = ch ? eR : eL;
                    double bestRes = 1e9, bestG = 0; int bestLag = 0;
                    const size_t N = std::min ({ y.size(), x.size(), (size_t) SR });
                    for (int lag = -64; lag <= 64; ++lag)
                    {
                        double xy = 0, xx = 0, yy = 0;
                        for (size_t i = 64; i + 64 < N; ++i) { const double xv = x[(size_t) ((int) i - lag)], yv = y[i]; xy += xv * yv; xx += xv * xv; yy += yv * yv; }
                        const double g = xx > 0 ? xy / xx : 0, res = (yy - g * xy) / std::max (1e-30, yy);
                        if (res < bestRes) { bestRes = res; bestG = g; bestLag = lag; }
                    }
                    std::printf ("   ch%d: plugin ≈ %+.2f dB × engine (lag %d samples), residual %.1f dB\n", ch, dbOf (bestG), bestLag, 10.0 * std::log10 (std::max (1e-30, bestRes)));
                }
                // magnitude ratio per octave band (1 s, FFT 2^16)
                constexpr int ord = 16, N = 1 << ord;
                juce::dsp::FFT fft (ord);
                std::vector<float> fy ((size_t) N * 2, 0.f), fx ((size_t) N * 2, 0.f);
                for (int i = 0; i < N && i < (int) a.L.size() && i < (int) eL.size(); ++i) { fy[(size_t) i] = a.L[(size_t) i]; fx[(size_t) i] = eL[(size_t) i]; }
                fft.performFrequencyOnlyForwardTransform (fy.data()); fft.performFrequencyOnlyForwardTransform (fx.data());
                std::string bands;
                for (double f0 = 31.25; f0 < 20000; f0 *= 2)
                {
                    double ey = 0, ex = 0;
                    for (int k = (int) (f0 * N / SR); k < (int) (2 * f0 * N / SR) && k < N / 2; ++k) { ey += (double) fy[(size_t) k] * fy[(size_t) k]; ex += (double) fx[(size_t) k] * fx[(size_t) k]; }
                    char b[64]; std::snprintf (b, sizeof b, " %.0f:%+.1f", f0, 10.0 * std::log10 ((ey + 1e-30) / (ex + 1e-30))); bands += b;
                }
                std::printf ("   band ratio plugin/engine (dB):%s\n", bands.c_str());
            }
        }
        tw::organics_debug::setLimiter (true);
        return 0;
    }
    if (mode == "keys")
    {
        juce::StringArray vs; vs.addTokens (argc > 3 ? argv[3] : "127,100", ",", "");
        const int art = argc > 4 ? std::atoi (argv[4]) : 0;
        Inst a; if (! useOrganic (a, 0, id)) { std::printf ("LOADFAIL %s\n", id.toRawUTF8()); return 1; }
        setP (*a.p, ParameterIDs::kOsc_ORG_ARTIC[0], (float) art);
        a.run (0.05);
        auto I = a.p->orgSlot_[0].inst;
        int lo = 128, hi = -1;
        for (auto& r : I->regions) if (r.artic == art && r.kind == tw::org::Kind::Attack) { lo = std::min (lo, r.lk); hi = std::max (hi, r.hk); }
        int overs = 0, n = 0; double worst = -200, worstIsp = -200; int wk = -1, wv = 0;
        for (auto& vstr : vs)
        {
            const int vel = vstr.getIntValue();
            for (int key = lo; key <= hi; ++key)
            {
                const auto& sp = I->span (art, tw::org::Kind::Attack, I->mappedKey (art, key), vel);
                const int presses = std::clamp ((int) sp.count, 1, 6);
                double pk = -200, ip = -200;
                for (int k = 0; k < presses; ++k)
                {
                    a.clear(); a.block ({ { key, 1, vel } }); a.run (0.5); a.block ({ { key, 0, 0 } }); a.run (0.4);
                    // one note at a time: let the release ring out (a second voice's tail would sum — that is the chord case)
                    for (int w = 0; w < 600; ++w)
                    {
                        double bp = 0; const size_t b0 = a.L.size() - BLK;
                        for (size_t i = b0; i < a.L.size(); ++i) bp = std::max ({ bp, (double) std::abs (a.L[i]), (double) std::abs (a.R[i]) });
                        if (bp < 1.0e-5) break;
                        a.block();
                    }
                    pk = std::max (pk, dbOf (peakOf (a.L, a.R)));
                    ip = std::max (ip, dbOf (ispOnlyLoud (a.L, a.R, std::pow (10.0, ceilDb / 20.0))));
                }
                ++n; if (pk > ceilDb + 0.01 || ip > 0.0) ++overs;
                if (pk > worst) { worst = pk; wk = key; wv = vel; }
                worstIsp = std::max (worstIsp, ip);
                std::printf ("OUT %s a%d k%d v%d peak %+.2f isp %+.2f presses %d\n", id.toRawUTF8(), art, key, vel, pk, ip, presses);
            }
        }
        std::printf ("KEYSUMMARY %s a%d %d keys×vels · %d over (peak > %+.1f dBFS or ISP > 0) · worst peak %+.2f dBFS (k%d v%d) · worst ISP %+.2f dBFS\n",
                     id.toRawUTF8(), art, n, overs, ceilDb, worst, wk, wv, worstIsp);
        return overs ? 1 : 0;
    }
    if (mode == "wav")
    {
        const int key = argc > 3 ? std::atoi (argv[3]) : 60, vel = argc > 4 ? std::atoi (argv[4]) : 127;
        const juce::File dir (argc > 5 ? argv[5] : "/tmp");
        const int art = argc > 6 ? std::atoi (argv[6]) : 0;
        dir.createDirectory();
        Buf L[2], R[2];
        for (int lim : { 0, 1 })
        {
            tw::organics_debug::setLimiter (lim != 0);
            Inst a; if (! useOrganic (a, 0, id)) { std::printf ("LOADFAIL %s\n", id.toRawUTF8()); return 1; }
            setP (*a.p, ParameterIDs::kOsc_ORG_ARTIC[0], (float) art); a.run (0.05);
            tw::organics_debug::resetLimiterStats();
            playNote (a, key, vel, 1.5, 2.0);
            const auto st = tw::organics_debug::limiterStats();
            L[lim] = a.L; R[lim] = a.R;
            const auto name = id + "_k" + juce::String (key) + "_v" + juce::String (vel) + (lim ? "_after_limiter.wav" : "_before.wav");
            writeWav (dir.getChildFile (name), a.L, a.R);
            std::printf ("WAV %s peak %+.2f dBFS · ISP %+.2f dBFS · LUFS(1 s) %.2f · GR max %.2f dB over %.1f ms (last at %.1f ms)\n",
                         dir.getChildFile (name).getFullPathName().toRawUTF8(), dbOf (peakOf (a.L, a.R)),
                         dbOf (ispOnlyLoud (a.L, a.R, 0.5)), lufs (a.L, a.R, onset (a.L, a.R), (int64_t) SR),
                         st.maxGrDb, 1000.0 * (double) st.limitedSamples / SR, 1000.0 * (double) st.lastLimitedAt / SR);
        }
        tw::organics_debug::setLimiter (true);
        return 0;
    }
    if (mode == "layer")
    {
        const int key = argc > 3 ? std::atoi (argv[3]) : 60, vel = argc > 4 ? std::atoi (argv[4]) : 127;
        {
            Inst a; useOrganic (a, 0, id);
            playNote (a, key, vel); const double one = dbOf (peakOf (a.L, a.R));
            Inst b; useOrganic (b, 0, id); useOrganic (b, 1, id);
            playNote (b, key, vel); const double two = dbOf (peakOf (b.L, b.R));
            std::printf ("LAYER %s k%d v%d · one Organics osc %+.2f dBFS · two Organics oscs (A+B, both limited) %+.2f dBFS (%s)\n",
                         id.toRawUTF8(), key, vel, one, two, two > 0.0 ? "OVER 0 dBFS — the sum is not limited" : "under 0 dBFS");
        }
        {
            Inst c; useOrganic (c, 1, id);                     // osc B Organics + osc A the init patch's Wavetable
            setP (*c.p, ParameterIDs::kOsc_ENABLE[0], 1.f); setP (*c.p, ParameterIDs::kOsc_ENGINE[0], 0.f); c.run (0.05);
            playNote (c, key, vel);
            std::printf ("LAYER %s k%d v%d · Organics (osc B) + Wavetable (osc A) %+.2f dBFS\n", id.toRawUTF8(), key, vel, dbOf (peakOf (c.L, c.R)));
        }
        return 0;
    }
    if (mode == "probe")
    {
        // per block: the engine's own output peak inside the voice (orgV_->blk[0]) × the voice's bound (orgOutGain) vs the
        // plugin's output peak — where the path adds more than the bound says. Presses one at a time (each rings out first).
        const int key = argc > 3 ? std::atoi (argv[3]) : 60, vel = argc > 4 ? std::atoi (argv[4]) : 127;
        const int presses = argc > 5 ? std::atoi (argv[5]) : 1;
        Inst a; if (! useOrganic (a, 0, id)) { std::printf ("LOADFAIL %s\n", id.toRawUTF8()); return 1; }
        double worst = -99;
        for (int k = 0; k < presses; ++k)
        {
            for (int b = 0; b < 2000; ++b)
            {
                double ep = 0, og = 0; int nv = 0;
                a.block (b == 0 ? std::initializer_list<std::tuple<int,int,int>> { { key, 1, vel } }
                                : (b == 25 ? std::initializer_list<std::tuple<int,int,int>> { { key, 0, 0 } } : std::initializer_list<std::tuple<int,int,int>> {}));
                a.p->forEachVoiceAllBanks ([&] (tw::SynthVoice* v, int) {
                    if (v->orgV_ == nullptr || ! v->orgV_->eng[0].isActive()) return;
                    ++nv;
                    const auto& bl = v->orgV_->blk[0];
                    for (int i = 0; i < BLK; ++i) ep = std::max ({ ep, (double) std::abs (bl.getReadPointer (0)[i]), (double) std::abs (bl.getReadPointer (1)[i]) });
                    og = std::max (og, (double) v->orgOutGain (0, BLK, 1));
                });
                double pp = 0; for (size_t i = a.L.size() - BLK; i < a.L.size(); ++i) pp = std::max ({ pp, (double) std::abs (a.L[i]), (double) std::abs (a.R[i]) });
                if (ep > 0 && og > 0)
                {
                    const double ex = dbOf (pp) - dbOf (ep * og);
                    worst = std::max (worst, ex);
                    if (dbOf (pp) > -1.2 || ex > 0.01) std::printf ("  press %d blk %3d voices %d · plugin %+.2f dBFS · engine %+.2f × bound %+.2f dB = %+.2f · excess %+.2f dB\n", k, b, nv, dbOf (pp), dbOf (ep), dbOf (og), dbOf (ep * og), ex);
                }
                if (b > 30 && pp < 1.0e-5 && nv == 0) break;
            }
        }
        std::printf ("PROBE worst excess of the plugin output over engine × bound: %+.2f dB\n", worst);
        return 0;
    }
    if (mode == "median")
    {
        // the tp113 measure at the plugin output: every FACTORY instrument's centre key (artic 0; middle C when playable), v100,
        // Human 0, K-weighted first 1 s from the onset, default osc Volume / master, limiter as it plays — vs the init patch's
        // Wavetable on the same note. MEDIAN line = the Organics median.
        std::vector<double> lv;
        {
            Inst w; w.clear(); w.block ({ { 60, 1, 100 } }); w.run (1.2); w.block ({ { 60, 0, 0 } }); w.run (0.2);
            std::printf ("WT init patch C4 v100: %.2f LUFS\n", lufs (w.L, w.R, onset (w.L, w.R), (int64_t) SR));
        }
        const auto idx = tw::OrganicsLibrary::get().index();
        for (auto& ent : *idx.getArray())
        {
            const juce::String iid = ent["id"].toString();
            if (iid.startsWith ("user.")) continue;
            Inst a; if (! useOrganic (a, 0, iid)) { std::printf ("LOADFAIL %s\n", iid.toRawUTF8()); continue; }
            auto I = a.p->orgSlot_[0].inst;
            int lo = 128, hi = -1;
            for (auto& r : I->regions) if (r.artic == 0 && r.kind == tw::org::Kind::Attack) { lo = std::min (lo, r.lk); hi = std::max (hi, r.hk); }
            const int centre = (lo <= 60 && 60 <= hi) ? 60 : (lo + hi + 1) / 2;
            setP (*a.p, ParameterIDs::kOsc_ORG_NOISE[0], 0.f); a.run (0.05);
            I->resetPerformanceState();
            a.clear(); a.block ({ { centre, 1, 100 } }); a.run (1.2); a.block ({ { centre, 0, 0 } }); a.run (0.2);
            const double l = lufs (a.L, a.R, onset (a.L, a.R), (int64_t) SR);
            lv.push_back (l);
            std::printf ("OUTLUFS %s k%d %.2f\n", iid.toRawUTF8(), centre, l);
            std::fflush (stdout);
        }
        std::sort (lv.begin(), lv.end());
        if (! lv.empty()) std::printf ("MEDIAN %.2f LUFS over %d factory instruments (min %.2f max %.2f)\n", lv[lv.size() / 2], (int) lv.size(), lv.front(), lv.back());
        return 0;
    }
    std::printf ("usage: gain|keys|wav|layer|probe|median <id> ...\n");
    return 2;
}
