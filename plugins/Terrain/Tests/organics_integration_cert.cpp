// ══════════════════════════════════════════════════════════════════════════════════════════════
//  tp104 — THE ORGANICS ENGINE, INTEGRATED: measured on the real processor (design §8, the integration rows).
//
//    Tests/organics_integration.sh        (build Terrain first; links libTerrain_SharedCode.a; sets
//                                          TERRAIN_ORGANICS_DIR = Tests/fixtures/organics)
//
//  Every bar drives TerrainAudioProcessor::processBlock with MIDI and measures the AUDIO, the natives' JSON or the
//  processor's own diagnostics. The instrument is the frozen fixture `test.sine` (each sample a sine at its root plus
//  a −20 dB marker partial: k3 = low layer, k5 = high layer (keys 0-59), k7/k9 = the round robin (keys 60-127),
//  k11 = the release trigger). The message thread is pumped with CFRunLoopRunInMode + the processor timer.
//    [0] UNUSED: a fresh instance, a chord on the init patch → no voice allocated an Organics engine, the library
//        was never touched, and no "Organics" thread exists.
//    [1] Organics on osc A + test.sine: sound at the right pitch (note 69 → 440 Hz ± 3 cents; note 57 → 220 Hz),
//        through the filter (Filter 1 cutoff down → the output's centroid falls).
//    [2] a mod route (mod wheel → ORG_TONE on osc A, dest 5273) moves the centroid (+1 vs −1 depth).
//    [3] unison 4 → 4 players (the runtime's reader count; when the stub is linked: skipped, reported).
//    [4] osc F (bank B) plays too (osc A off).
//    [5] state round trip: save → a NEW instance → load → same id / knob / sound (RMS within 1 dB, same pitch).
//    [6] a missing id → silent, status "missing", no crash; a missing id whose saved family exists → the family
//        default plays and says so (wantedId).
//    [7] switch the engine away → the voices' engines freed and the library resident count back to 0 within 6 s.
//    [8] organicsPreview(id) plays preview audio with no note (or reports no preview.flac in the fixture).
//    [9] organicViz: while a note sounds the feed carries {osc, notes:[{n,lvl}]}; 300 ms after, it stops.
//   tp105:
//   [10] osc E (bank B) on Organics sounds AND its organicViz event says osc 4.
//   [11] THE PATCHER: an Organics osc A with its output cable cut reaches the output through a rack device (Utility 1);
//        its direct tap hears the raw engine past a closed filter (post-filter dark, tapped bright). The flow cards (a Chop
//        on an Organics osc) are gated on the AU: Tests/au_patcher_rules.cpp with ORG_ID=<id> (this harness has no rack clock).
//   [12] THE RELEASE LINK: with a long amp release (2 s) the piano-like fixture keeps sounding after note-off far longer
//        than with a short one (0.05 s) — the amp envelope lengthens the Organics release — and the voice ends after both.
//   [13] the tp105 parameters exist for A–H (VIBRATO / VIBRATE / VIBDELAY / VCURVE / TUNING), and a mod route to dest knob 3
//        (5272 + o·10 + 3) moves the VIBRATO (pitch wobble on osc A), not Attack.
//   tp106 (the final overpass):
//   [14] host sample rates 44.1 / 96 kHz: pitch ±3 ¢ and 3 s through the loop seams without a click (HP-residual metric).
//   [15] offline bounce (non-realtime → the sinc reader): same pitch, level within 0.5 dB of realtime, no click.
//   [16] Settings A4 = 432 Hz retunes Organics.   [17] MPE: a per-note bend moves only its own Organics note.
//   [18] switching the instrument under a held note, [19] switching the engine Organics → WT → Organics: faded
//        through the osc's own 4 ms gate (SynthVoice::requestEngine), click-free.
//   [20] a preset load in the middle of a 12 s Organics release: flushed (faded), no click, nothing bleeds after.
//   [21] a truncated map.json and [22] a missing library folder: status "missing", silence, no crash.
//   [23] LFO 1 → each of the ten Organics destinations moves its perceptual feature > 3× the unrouted note.
//   [24] the Ensemble on the INSTALLED violin section (skipped without it): loudness vs players 1 → 16 is a gentle law,
//        2 players do not phase, 16 players × 8 notes render in under half the block's real-time budget.
//   tp107:
//   [25] ATTACK is back: onset vs the knob (0 … 1), onset = max(amp attack, knob), LFO 1 → the Attack dest 5352 + osc (A, and E
//        through bank B's rebase).
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
#include <mach/mach.h>
#include <pthread.h>
#include <CoreFoundation/CoreFoundation.h>
#define private public
#define protected public
#include "PluginProcessor.h"
#undef private
#undef protected

// The runtime's test hooks (OrganicEngine.h). Weak: when the stub is linked they do not exist and [3] reports so.
namespace tw { namespace organics_debug { int lastRenderReaders() noexcept __attribute__((weak)); int lastLiveReaders() noexcept __attribute__((weak)); int lastNoteRegion() noexcept __attribute__((weak)); int steals() noexcept __attribute__((weak)); } }

static int npass = 0, nfail = 0, nskip = 0;
static void chk (bool ok, const char* what, const std::string& d = "")
{ std::printf ("  %s  %s\n", ok ? "PASS " : "FAIL ", what); if (! d.empty()) std::printf ("        %s\n", d.c_str()); ok ? ++npass : ++nfail; std::fflush (stdout); }
static void skip (const char* what, const std::string& d) { std::printf ("  SKIP   %s\n        %s\n", what, d.c_str()); ++nskip; std::fflush (stdout); }
static std::string fmt (const char* f, double a = 0, double b = 0, double c = 0, double d = 0)
{ char s[512]; std::snprintf (s, sizeof s, f, a, b, c, d); return s; }

static constexpr double SR = 48000.0; static constexpr int BLK = 512;
static void setP (TerrainAudioProcessor& p, const char* id, float plain)
{
    auto* prm = p.apvts.getParameter (id);
    if (prm == nullptr) { std::printf ("!! no param %s\n", id); std::exit (2); }
    prm->setValueNotifyingHost (prm->convertTo0to1 (plain));
}
static void pumpMs (double ms) { const double t0 = juce::Time::getMillisecondCounterHiRes(); while (juce::Time::getMillisecondCounterHiRes() - t0 < ms) CFRunLoopRunInMode (kCFRunLoopDefaultMode, 0.005, false); }

struct Inst
{
    std::unique_ptr<TerrainAudioProcessor> p; juce::AudioBuffer<float> buf { 2, BLK };
    std::vector<float> L, R;
    double fs = SR;
    explicit Inst (double sampleRate = SR, bool nonRealtime = false) : fs (sampleRate)
    {
        p = std::make_unique<TerrainAudioProcessor>();
        if (nonRealtime || std::getenv ("ORG_NONRT")) p->setNonRealtime (true);   // the offline-bounce path (8-tap sinc)
        p->setPlayConfigDetails (0, 2, fs, BLK);
        p->prepareToPlay (fs, BLK);
    }
    void block (std::initializer_list<std::pair<int,int>> ev = {}, const juce::MidiBuffer* extra = nullptr)
    {
        buf.clear(); juce::MidiBuffer m;
        for (auto e : ev) m.addEvent (e.second ? juce::MidiMessage::noteOn (1, e.first, (juce::uint8) 100) : juce::MidiMessage::noteOff (1, e.first), 0);
        if (extra) m.addEvents (*extra, 0, BLK, 0);
        p->processBlock (buf, m);
        L.insert (L.end(), buf.getReadPointer (0), buf.getReadPointer (0) + BLK);
        R.insert (R.end(), buf.getReadPointer (1), buf.getReadPointer (1) + BLK);
    }
    // the message thread's share: the library callbacks (callAsync) and the processor's 60 Hz timer
    void tick (int n = 1) { for (int i = 0; i < n; ++i) { CFRunLoopRunInMode (kCFRunLoopDefaultMode, 0.002, false); p->timerCallback(); } }
    // render `sec` seconds, ticking the timer every other block (~47 Hz)
    void run (double sec) { const int n = (int) std::ceil (sec * fs / BLK); for (int b = 0; b < n; ++b) { block(); if (b & 1) tick(); } }
    // wait for the osc's instrument to land (status leaves "loading"), rendering meanwhile
    juce::String waitLoaded (int osc, double maxSec = 5.0)
    {
        const double t0 = juce::Time::getMillisecondCounterHiRes();
        while (juce::Time::getMillisecondCounterHiRes() - t0 < maxSec * 1000.0)
        {
            tick(); block();
            const auto st = p->organicSlot (osc).status;
            // landed = decided AND delivered: the mailbox holds one instrument, so a fast (cached) answer can wait one
            // timer tick behind the nullptr the request posted first (organicsPublish's publishPending)
            if (st != "loading" && ! p->orgSlot_[osc].publishPending && ! (p->orgMailState_[osc].load() != 0)) { block(); return st; }
        }
        return p->organicSlot (osc).status;
    }
    void clear() { L.clear(); R.clear(); }
};

static double rmsOf (const std::vector<float>& x, size_t a, size_t b)
{ double s = 0; size_t n = 0; for (size_t i = a; i < b && i < x.size(); ++i) { s += (double) x[i] * x[i]; ++n; } return n ? std::sqrt (s / (double) n) : 0.0; }
static double dbOf (double r) { return 20.0 * std::log10 (std::max (1e-12, r)); }

// the frequency with the most energy within ±60 cents of `nominal` (a fine Goertzel scan, Hann window) → cents error
static double centsOff (const std::vector<float>& x, size_t a, size_t n, double nominal, double* peakHz = nullptr, double fs = SR)
{
    if (a + n > x.size()) n = x.size() > a ? x.size() - a : 0;
    if (n < 4096) return 1e9;
    double best = 0, bestF = nominal;
    for (double c = -60.0; c <= 60.0; c += 0.25)
    {
        const double f = nominal * std::pow (2.0, c / 1200.0), w = 2.0 * juce::MathConstants<double>::pi * f / fs;
        double re = 0, im = 0;
        for (size_t i = 0; i < n; ++i)
        {
            const double h = 0.5 - 0.5 * std::cos (2.0 * juce::MathConstants<double>::pi * (double) i / (double) (n - 1));
            re += x[a + i] * h * std::cos (w * (double) i); im -= x[a + i] * h * std::sin (w * (double) i);
        }
        const double m = re * re + im * im;
        if (m > best) { best = m; bestF = f; }
    }
    if (peakHz) *peakHz = bestF;
    return 1200.0 * std::log2 (bestF / nominal);
}

// spectral centroid (Hz) over [a, a + 16384), magnitude-weighted 20 Hz .. 20 kHz
static double centroid (const std::vector<float>& x, size_t a)
{
    constexpr int ord = 14, N = 1 << ord;
    if (a + N > x.size()) return 0;
    juce::dsp::FFT fft (ord); std::vector<float> w ((size_t) N * 2, 0.f);
    for (int i = 0; i < N; ++i) w[(size_t) i] = x[a + (size_t) i] * (0.5f - 0.5f * std::cos (2.0f * juce::MathConstants<float>::pi * (float) i / (float) (N - 1)));
    fft.performFrequencyOnlyForwardTransform (w.data());
    double num = 0, den = 0;
    for (int k = 1; k < N / 2; ++k) { const double f = (double) k * SR / N; if (f < 20 || f > 20000) continue; num += f * w[(size_t) k]; den += w[(size_t) k]; }
    return den > 0 ? num / den : 0;
}

// the level of the partial at f (Hz), in dB relative to full scale (Hann window Goertzel)
static double partialDb (const std::vector<float>& x, size_t a, size_t n, double f)
{
    if (a + n > x.size()) return -200;
    const double w = 2.0 * juce::MathConstants<double>::pi * f / SR; double re = 0, im = 0;
    for (size_t i = 0; i < n; ++i) { const double h = 0.5 - 0.5 * std::cos (2.0 * juce::MathConstants<double>::pi * (double) i / (double) (n - 1)); re += x[a + i] * h * std::cos (w * (double) i); im -= x[a + i] * h * std::sin (w * (double) i); }
    return dbOf (2.0 * std::sqrt (re * re + im * im) / (0.5 * (double) n));
}

static bool organicsThreadExists()
{
    thread_act_array_t th; mach_msg_type_number_t n = 0;
    if (task_threads (mach_task_self(), &th, &n) != KERN_SUCCESS) return false;
    bool found = false;
    for (mach_msg_type_number_t i = 0; i < n; ++i)
    {
        if (pthread_t pt = pthread_from_mach_thread_np (th[i]))
        { char nm[64] = {}; pthread_getname_np (pt, nm, sizeof nm); if (std::strstr (nm, "Organics") != nullptr) found = true; }
        mach_port_deallocate (mach_task_self(), th[i]);
    }
    vm_deallocate (mach_task_self(), (vm_address_t) th, n * sizeof (thread_act_t));
    return found;
}

static void useOrganic (Inst& a, int osc, const juce::String& id)
{
    setP (*a.p, ParameterIDs::kOsc_ENABLE[osc], 1.f);
    setP (*a.p, ParameterIDs::kOsc_ENGINE[osc], 7.f);
    a.tick (3);                                        // the arm (and bank B for E–H)
    const auto js = a.p->organicsSetInstrument (osc, id);
    juce::ignoreUnused (js);
}

static bool want (int k)
{
    const char* o = std::getenv ("ORG_ONLY");
    if (o == nullptr) return true;
    if (k >= 10) { const std::string t = "," + std::string (o) + ","; return t.find ("," + std::to_string (k) + ",") != std::string::npos; }
    return std::strchr (o, (char) ('0' + k)) != nullptr;
}
// tp105 — a parameter by its DISPLAY name (the AU harnesses' names), NORMALISED value
static void setN (TerrainAudioProcessor& p, const char* name, float norm)
{
    for (auto* prm : p.getParameters())
        if (prm->getName (256) == name) { prm->setValueNotifyingHost (norm); return; }
    std::printf ("!! no parameter named '%s'\n", name); std::exit (2);
}
static double rmsWin (const std::vector<float>& x, size_t a, size_t n) { return rmsOf (x, a, a + n); }

// ── tp106 helpers ──────────────────────────────────────────────────────────────────────────────────────────────
// THE CLICK METRIC (Tests/organics_audit.cpp): a sample whose HP(8 kHz) residual stands > 20 dB over the RMS of its
// ±10 ms neighbourhood AND > 6 dB over every other HF peak within ±25 ms (a spiky periodic waveform repeats its peak;
// a discontinuity does not) AND is audible (> −45 dB re the local signal, over −90 dBFS).
struct ClickHit { bool hit = false; double ex = -200, rel = -200; size_t at = 0; };
static ClickHit clickScan (const std::vector<float>& x, size_t a, size_t b, double fs)
{
    ClickHit out;
    const size_t N = x.size(); b = std::min (b, N);
    if (N < 4096 || b <= a) return out;
    std::vector<double> h (x.begin(), x.end());
    for (double q : { 0.5411961, 1.3065630 })
    {
        const double w0 = 2 * juce::MathConstants<double>::pi * 8000.0 / fs, al = std::sin (w0) / (2 * q), c = std::cos (w0);
        const double b0 = (1 + c) / 2, b1 = -(1 + c), b2 = (1 + c) / 2, a0 = 1 + al, a1 = -2 * c, a2 = 1 - al;
        double x1 = 0, x2 = 0, y1 = 0, y2 = 0;
        for (auto& v : h) { const double in = v, o = (b0 * in + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2) / a0; x2 = x1; x1 = in; y2 = y1; y1 = o; v = o; }
    }
    std::vector<double> ph (N + 1, 0.0), px (N + 1, 0.0);
    for (size_t i = 0; i < N; ++i) { ph[i + 1] = ph[i] + h[i] * h[i]; px[i + 1] = px[i] + (double) x[i] * x[i]; }
    const size_t W = (size_t) (0.010 * fs), C = (size_t) (0.0005 * fs), R = (size_t) (0.025 * fs);
    double best = -1e9;
    for (size_t i = std::max<size_t> (a, 256); i < b; ++i)
    {
        const double v = std::abs (h[i]); if (v < 1e-7) continue;
        const size_t a0 = i > W ? i - W : 0, a1 = std::min (N, i + W), c0 = i > C ? i - C : 0, c1 = std::min (N, i + C);
        const double rl = std::sqrt (std::max (0.0, (ph[a1] - ph[a0]) - (ph[c1] - ph[c0])) / (double) ((a1 - a0) - (c1 - c0))) + 1e-12;
        const double sl = std::sqrt ((px[a1] - px[a0]) / (double) (a1 - a0)) + 1e-12;
        if (sl < 1e-5) continue;
        const double ex = dbOf (v / rl), rel = dbOf (v / sl);
        bool hit = ex > 20.0 && rel > -45.0 && v > 3.16e-5;   // (under −90 dBFS: below the 16-bit floor)
        if (hit)
        {
            double ring = 1e-12;
            for (size_t j = i > R ? i - R : 0; j < std::min (N, i + R); ++j) if (j + C < i || j >= i + C) ring = std::max (ring, std::abs (h[j]));
            hit = dbOf (v / ring) > 6.0;
        }
        const double score = (hit ? 1000.0 : 0.0) + std::min (ex, 60.0) + 0.5 * rel;
        if (score > best) { best = score; out.hit = hit; out.ex = ex; out.rel = rel; out.at = i; }
    }
    return out;
}

static std::vector<float> highpassOf (const std::vector<float>& x, double fc)
{
    std::vector<float> y = x;
    for (double q : { 0.5411961, 1.3065630 })
    {
        const double w0 = 2 * juce::MathConstants<double>::pi * fc / SR, al = std::sin (w0) / (2 * q), c = std::cos (w0);
        const double b0 = (1 + c) / 2, b1 = -(1 + c), b2 = (1 + c) / 2, a0 = 1 + al, a1 = -2 * c, a2 = 1 - al;
        double x1 = 0, x2 = 0, y1 = 0, y2 = 0;
        for (auto& v : y) { const double in = v, o = (b0 * in + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2) / a0; x2 = x1; x1 = in; y2 = y1; y1 = o; v = (float) o; }
    }
    return y;
}

/** How much one perceptual feature MOVES over [s0, s1): the detrended standard deviation of — 0 the spectral centroid
    (% of its mean) · 1 the level (dB) · 2 the per-cycle pitch (¢, zero crossings) · 3 the side/mid ratio (dB).
    1024-frame windows, hop 512. */
static double featureWobble (const std::vector<float>& L, const std::vector<float>& Rr, size_t s0, size_t s1, int feature, int note)
{
    std::vector<double> v;
    s1 = std::min (s1, L.size());
    if (feature == 2)
    {
        const double ref = 440.0 * std::pow (2.0, (note - 69) / 12.0); double last = -1;
        for (size_t i = std::max<size_t> (1, s0); i < s1; ++i)
            if (L[i - 1] < 0.f && L[i] >= 0.f)
            {
                const double t = (double) (i - 1) + (double) (-L[i - 1]) / (double) (L[i] - L[i - 1]);
                if (last >= 0) v.push_back (1200.0 * std::log2 ((SR / (t - last)) / ref));
                last = t;
            }
    }
    else
    {
        juce::dsp::FFT fft (10);
        std::vector<float> w (2048);
        for (size_t s = s0; s + 1024 <= s1; s += 512)
        {
            if (feature == 0)
            {
                std::fill (w.begin(), w.end(), 0.f);
                for (int i = 0; i < 1024; ++i) w[(size_t) i] = 0.5f * (L[s + (size_t) i] + Rr[s + (size_t) i]) * (0.5f - 0.5f * std::cos (2.0f * juce::MathConstants<float>::pi * (float) i / 1023.f));
                fft.performFrequencyOnlyForwardTransform (w.data());
                double num = 0, den = 0; for (int k = 1; k < 512; ++k) { num += (double) k * w[(size_t) k]; den += w[(size_t) k]; }
                v.push_back (den > 0 ? num / den : 0);
            }
            else if (feature == 3)
            {
                double m = 0, d = 0; for (size_t i = s; i < s + 1024; ++i) { const double a = 0.5 * (L[i] + Rr[i]), b = 0.5 * (L[i] - Rr[i]); m += a * a; d += b * b; }
                v.push_back (10.0 * std::log10 ((d + 1e-20) / (m + 1e-20)));
            }
            else
            {
                v.push_back (dbOf (std::sqrt ((rmsOf (L, s, s + 1024) * rmsOf (L, s, s + 1024) + rmsOf (Rr, s, s + 1024) * rmsOf (Rr, s, s + 1024)) / 2.0)));
            }
        }
    }
    if (v.size() < 4) return 0.0;
    const double n = (double) v.size();
    double mx = 0, my = 0; for (size_t i = 0; i < v.size(); ++i) { mx += (double) i; my += v[i]; } mx /= n; my /= n;
    double sxy = 0, sxx = 0; for (size_t i = 0; i < v.size(); ++i) { sxy += ((double) i - mx) * (v[i] - my); sxx += ((double) i - mx) * ((double) i - mx); }
    const double sl = sxx > 0 ? sxy / sxx : 0; double q = 0;
    for (size_t i = 0; i < v.size(); ++i) { const double r = v[i] - my - sl * ((double) i - mx); q += r * r; }
    const double sd = std::sqrt (q / n);
    return feature == 0 ? 100.0 * sd / std::max (1e-9, std::abs (my)) : sd;
}

int main()
{
    juce::ScopedJuceInitialiser_GUI init;
    setenv ("TERRAIN_DETERMINISTIC", "1", 1);
    const bool runtime = tw::organics_debug::lastRenderReaders != nullptr;
    std::printf ("organics_integration_cert — the Organics engine on the shipping processBlock (%s)\n",
                 runtime ? "REAL runtime linked" : "STUB linked: the sound bars cannot pass");
    std::printf ("  TERRAIN_ORGANICS_DIR = %s\n", std::getenv ("TERRAIN_ORGANICS_DIR") ? std::getenv ("TERRAIN_ORGANICS_DIR") : "(unset)");

    // ═══ [0] UNUSED ═══
    std::printf ("\n[0] Unused: zero allocation, no library, no thread\n");
    if (want (0))
    {
        Inst a; a.block ({ {48,1},{55,1},{60,1},{64,1} }); a.run (0.5); a.block ({ {48,0},{55,0},{60,0},{64,0} }); a.run (0.3);
        chk (a.p->organicsAllocatedVoiceCount() == 0 && ! a.p->organicsTouchedLibrary() && ! organicsThreadExists(),
             "0 an Organics-free instance allocates no engine, never touches the library, starts no thread",
             fmt ("voices with an engine %.0f · library touched %.0f · 'Organics' thread %.0f",
                  a.p->organicsAllocatedVoiceCount(), a.p->organicsTouchedLibrary() ? 1 : 0, organicsThreadExists() ? 1 : 0));
    }

    // ═══ [1] OSC A + test.sine, pitch, through the filter ═══
    std::printf ("\n[1] Osc A on Organics + test.sine: pitch and the filter\n");
    std::vector<float> refA; double refRms = 0;
    if (want (1))
    {
        Inst a; useOrganic (a, 0, "test.sine");
        const auto st = a.waitLoaded (0);
        chk (st == "ok", "1a the instrument loads (status ok)", ("status " + st + " · " + a.p->organicsStateJson (0).removeCharacters ("\n ")).toStdString());
        chk (a.p->organicsArmedVoiceCount() > 0, "1b the voices armed their engines", fmt ("armed voices %.0f", a.p->organicsArmedVoiceCount()));
        a.clear(); a.block ({ {69,1} }); a.run (0.6);
        const size_t s0 = (size_t) (0.2 * SR);
        double hz = 0; const double c69 = centsOff (a.L, s0, 16384, 440.0, &hz);
        const double r69 = rmsOf (a.L, s0, s0 + 16384);
        chk (r69 > 1e-3 && std::fabs (c69) <= 3.0, "1c note 69 sounds at 440 Hz (±3 cents)", fmt ("rms %.1f dBFS · peak %.2f Hz = %+.2f cents", dbOf (r69), hz, c69));
        refA = a.L; refRms = r69;
        a.block ({ {69,0} }); a.run (0.5);
        a.clear(); a.block ({ {57,1} }); a.run (0.6);
        const double c57 = centsOff (a.L, s0, 16384, 220.0, &hz);
        chk (rmsOf (a.L, s0, s0 + 16384) > 1e-3 && std::fabs (c57) <= 3.0, "1d note 57 (the other zone, repitched) at 220 Hz (±3 cents)", fmt ("peak %.2f Hz = %+.2f cents", hz, c57));
        const double cenOpen = centroid (a.L, s0);
        a.block ({ {57,0} }); a.run (0.5);
        // through the FILTER: a low cutoff must darken it (engine → FILTER → effects)
        setP (*a.p, ParameterIDs::SYN_FILTER1_TYPE, 0.f);      // Ladder LP 24
        setP (*a.p, ParameterIDs::SYN_FILTER1_CUT, 150.f);     // well under the fundamental and the marker
        setP (*a.p, ParameterIDs::SYN_FILTER1_MIX, 1.f);
        setP (*a.p, ParameterIDs::SYN_OSC_A_F1MIX, 1.f);       // osc A → filter 1 (fb79: default dry)
        a.clear(); a.block ({ {57,1} }); a.run (0.6);
        const double cenLow = centroid (a.L, s0), rLow = rmsOf (a.L, s0, s0 + 16384);
        chk (cenOpen > 0 && rLow > 0 && (cenLow < cenOpen * 0.8 || dbOf (rLow) < dbOf (refRms) - 3.0),
             "1e the osc goes THROUGH the filter (Filter 1 LP cutoff down → darker / quieter)",
             fmt ("centroid %.0f Hz → %.0f Hz · rms %.1f → %.1f dBFS", cenOpen, cenLow, dbOf (refRms), dbOf (rLow)));
    }

    // ═══ [2] MOD ROUTE → ORG_TONE ═══
    std::printf ("\n[2] A mod route to ORG_TONE (osc A) moves the brightness\n");
    if (want (2))
    {
        auto run = [] (float depth, double& cen)
        {
            Inst a; useOrganic (a, 0, "test.sine"); a.waitLoaded (0);
            a.p->setSynthModMatrix ("[{\"s\":230,\"d\":" + juce::String (wc::organicDest (0, 1)) + ",\"v\":" + juce::String (depth) + "}]");
            juce::MidiBuffer cc; cc.addEvent (juce::MidiMessage::controllerEvent (1, 1, 127), 0);
            a.block ({}, &cc); a.run (0.1);
            a.clear(); a.block ({ {69,1} }); a.run (0.6);
            const size_t s0 = (size_t) (0.2 * SR);
            cen = centroid (a.L, s0);
        };
        double cUp = 0, cDn = 0;
        run (+1.0f, cUp); run (-1.0f, cDn);
        chk (cUp > 0 && cDn > 0 && (cUp - cDn) > 0.05 * cDn,
             "2 mod wheel → ORG_TONE A (dest 5273): +1 is brighter than −1 (spectral centroid)",
             fmt ("centroid +1: %.0f Hz · −1: %.0f Hz · Δ %.1f %%", cUp, cDn, 100.0 * (cUp - cDn) / std::max (1.0, cDn)));
    }

    // ═══ [3] UNISON 4 → 4 PLAYERS ═══
    std::printf ("\n[3] Unison 4 → 4 players (Ensemble)\n");
    if (want (3))
    {
        Inst a; useOrganic (a, 0, "test.sine"); a.waitLoaded (0);
        setP (*a.p, ParameterIDs::SYN_OSC_A_UNISON, 4.f);
        a.clear(); a.block ({ {69,1} }); a.block(); a.run (0.1);
        if (std::getenv ("ORG_DEBUG"))
            for (int i = 0; i < TerrainAudioProcessor::kSynthVoiceCount; ++i)
                if (auto* v = a.p->synthVoices_[(size_t) i]) if (v->isVoiceActive())
                    std::printf ("   dbg voice %d: note %d eng %d ready %d active %d seen %u gen %u blk %p lvl %.3f rms %.1f region %d steals %d\n", i, v->getCurrentlyPlayingNote(), (int) v->engine_,
                                 v->orgReady_.load() ? 1 : 0, v->orgV_ && v->orgV_->eng[0].isActive() ? 1 : 0, v->orgInstSeen_[0], a.p->orgInstGen_[0],
                                 (const void*) v->orgBlkL_[0], v->orgV_ ? v->orgV_->eng[0].readLevel() : -1.f, dbOf (rmsOf (a.L, a.L.size() - 4096, a.L.size())), tw::organics_debug::lastNoteRegion ? tw::organics_debug::lastNoteRegion() : -9, tw::organics_debug::steals ? tw::organics_debug::steals() : -9);
        if (! runtime) skip ("3 unison 4 → 4 players", "the runtime's reader count is not linked (stub)");
        else
        {
            const int rd = tw::organics_debug::lastLiveReaders ? tw::organics_debug::lastLiveReaders() : tw::organics_debug::lastRenderReaders();
            chk (rd == 4, "3 unison 4 → the engine renders 4 players (one reader each on this one-layer zone)", fmt ("live readers %.0f", rd));
        }
    }

    // ═══ [4] OSC F (BANK B) ═══
    std::printf ("\n[4] Osc F (bank B) plays too\n");
    if (want (4))
    {
        Inst a;
        setP (*a.p, ParameterIDs::SYN_OSC_A_ENABLE, 0.f);
        setP (*a.p, ParameterIDs::kOsc_ENABLE[5], 1.f);
        a.tick (4);                                            // ensureBankB (message thread)
        useOrganic (a, 5, "test.sine");
        const auto st = a.waitLoaded (5);
        a.clear(); a.block ({ {69,1} }); a.run (0.6);
        const size_t s0 = (size_t) (0.2 * SR); double hz = 0;
        const double c = centsOff (a.L, s0, 16384, 440.0, &hz); const double r = rmsOf (a.L, s0, s0 + 16384);
        chk (st == "ok" && r > 1e-3 && std::fabs (c) <= 3.0, "4 osc F on Organics sounds at 440 Hz (bank B, rawParamB, the explicit E–H dests)",
             ("status " + st.toStdString() + " · ") + fmt ("rms %.1f dBFS · %+.2f cents · bank B %.0f", dbOf (r), c, a.p->bankB_.load() != nullptr ? 1 : 0));
    }

    // ═══ [5] STATE ROUND TRIP ═══
    std::printf ("\n[5] State: save → a new instance → load\n");
    if (want (5))
    {
        juce::MemoryBlock blob; double r1 = 0, c1 = 0;
        {
            Inst a; useOrganic (a, 0, "test.sine"); a.waitLoaded (0);
            setP (*a.p, ParameterIDs::SYN_OSC_A_ORG_TONE, 0.8f);
            setP (*a.p, ParameterIDs::SYN_OSC_A_ORG_HUMAN, 0.0f);   // deterministic (no fake RR); note 57 = a zone with no real RR
            a.clear(); a.block ({ {57,1} }); a.run (0.6);
            const size_t s0 = (size_t) (0.2 * SR); r1 = rmsOf (a.L, s0, s0 + 16384); c1 = centroid (a.L, s0);
            a.p->getStateInformation (blob);
        }
        const juce::String xml = [&] { auto x = juce::AudioProcessor::getXmlFromBinary (blob.getData(), (int) blob.getSize()); return x ? x->toString() : juce::String(); }();
        chk (xml.contains ("<ORGANICS>") && xml.contains ("id=\"test.sine\""), "5a the state carries <ORGANICS><OSC slot=\"0\" id=\"test.sine\" rev=\"1\"/>",
             xml.fromFirstOccurrenceOf ("<ORGANICS>", true, false).upToFirstOccurrenceOf ("</ORGANICS>", true, false).toStdString());
        Inst b; b.p->setStateInformation (blob.getData(), (int) blob.getSize());
        const auto st = b.waitLoaded (0);
        const float tone = b.p->apvts.getRawParameterValue (ParameterIDs::SYN_OSC_A_ORG_TONE)->load();
        b.clear(); b.block ({ {57,1} }); b.run (0.6);
        if (std::getenv ("ORG_DEBUG"))
            std::printf ("   dbg: armed %d · gen %u · audioInst %d · published %d · mailFull %d · pending %d · engine %d · enable %.0f · level %.2f\n",
                         b.p->organicsArmedVoiceCount(), b.p->orgInstGen_[0], b.p->orgAudioInst_[0] != nullptr ? 1 : 0,
                         b.p->orgSlot_[0].published != nullptr ? 1 : 0, b.p->orgMailState_[0].load(), b.p->orgSlot_[0].publishPending ? 1 : 0,
                         (int) *b.p->apvts.getRawParameterValue (ParameterIDs::SYN_OSC_A_ENGINE), b.p->apvts.getRawParameterValue (ParameterIDs::SYN_OSC_A_ENABLE)->load(),
                         b.p->apvts.getRawParameterValue (ParameterIDs::SYN_OSC_A_LEVEL)->load());
        const size_t s0 = (size_t) (0.2 * SR); const double r2 = rmsOf (b.L, s0, s0 + 16384), c2 = centroid (b.L, s0);
        chk (b.p->organicSlot (0).id == "test.sine" && std::fabs (tone - 0.8f) < 1e-3, "5b the new instance holds the same id and knob",
             ("id " + b.p->organicSlot (0).id + " · status " + st).toStdString() + fmt (" · tone %.3f", tone));
        chk (r1 > 1e-3 && std::fabs (dbOf (r2) - dbOf (r1)) < 1.0 && std::fabs (c2 - c1) < 0.05 * c1, "5c ...and the same sound (note 57, Human 0: RMS within 1 dB, centroid within 5 %)",
             fmt ("rms %.2f → %.2f dBFS · centroid %.0f → %.0f Hz", dbOf (r1), dbOf (r2), c1, c2));
        juce::String js = b.p->organicsStateJson (0);
        chk (js.contains ("\"test.sine\"") && js.contains ("\"status\""), "5d organicsGetState reads it back (the page's reopen)", js.removeCharacters ("\n").toStdString());
    }

    // ═══ [6] MISSING ═══
    std::printf ("\n[6] A missing instrument\n");
    if (want (6))
    {
        auto stateWith = [] (const juce::String& id, const juce::String& family) -> juce::MemoryBlock
        {
            Inst a; setP (*a.p, ParameterIDs::SYN_OSC_A_ENGINE, 7.f);
            juce::MemoryBlock mb; a.p->getStateInformation (mb);
            auto x = juce::AudioProcessor::getXmlFromBinary (mb.getData(), (int) mb.getSize());
            auto* org = x->createNewChildElement ("ORGANICS"); auto* o = org->createNewChildElement ("OSC");
            o->setAttribute ("slot", 0); o->setAttribute ("id", id); o->setAttribute ("rev", 1); o->setAttribute ("name", "Salamander Grand"); o->setAttribute ("family", family);
            juce::MemoryBlock out; juce::AudioProcessor::copyXmlToBinary (*x, out); return out;
        };
        {
            const auto mb = stateWith ("salamander.grand.v3", "nosuchfamily");
            Inst b; b.p->setStateInformation (mb.getData(), (int) mb.getSize());
            const auto st = b.waitLoaded (0);
            b.clear(); b.block ({ {69,1} }); b.run (0.5);
            const double r = rmsOf (b.L, 0, b.L.size());
            const juce::String js = b.p->organicsStateJson (0);
            chk (st == "missing" && r < 1e-6 && js.contains ("Salamander Grand"), "6a an id that is not installed, no family match → silent, status \"missing\", named, no crash",
                 ("status " + st).toStdString() + fmt (" · output rms %.1f dBFS · ", dbOf (r)) + js.removeCharacters ("\n").toStdString());
        }
        {
            const auto mb = stateWith ("salamander.grand.v3", "grand");
            Inst b; b.p->setStateInformation (mb.getData(), (int) mb.getSize());
            juce::String st = b.waitLoaded (0);
            if (st == "loading" || b.p->organicSlot (0).id != "test.sine") st = b.waitLoaded (0);
            b.clear(); b.block ({ {69,1} }); b.run (0.5);
            const size_t s0 = (size_t) (0.2 * SR);
            const double r = rmsOf (b.L, s0, s0 + 16384);
            const juce::String js = b.p->organicsStateJson (0);
            chk (b.p->organicSlot (0).id == "test.sine" && js.contains ("wantedId") && r > 1e-3,
                 "6b a missing id whose family is installed → the family default plays, and says so (wantedId)",
                 js.removeCharacters ("\n").toStdString() + fmt (" · rms %.1f dBFS", dbOf (r)));
        }
    }

    // ═══ [7] THE WAY BACK ═══
    std::printf ("\n[7] Switch the engine away → memory back within 6 s\n");
    if (want (7))
    {
        Inst a; useOrganic (a, 0, "test.sine"); a.waitLoaded (0);
        a.block ({ {69,1} }); a.run (0.3); a.block ({ {69,0} }); a.run (0.3);
        const int resBefore = tw::OrganicsLibrary::get().residentCount();
        const long long bytesBefore = (long long) tw::OrganicsLibrary::get().residentBytes();
        setP (*a.p, ParameterIDs::SYN_OSC_A_ENGINE, 0.f);
        const double t0 = juce::Time::getMillisecondCounterHiRes(); double tFreedEngines = -1, tFreedLib = -1;
        while (juce::Time::getMillisecondCounterHiRes() - t0 < 8000.0)
        {
            a.run (0.05);
            const double t = (juce::Time::getMillisecondCounterHiRes() - t0) * 0.001;
            if (tFreedEngines < 0 && a.p->organicsAllocatedVoiceCount() == 0) tFreedEngines = t;
            if (tFreedLib < 0 && tw::OrganicsLibrary::get().residentCount() == 0) tFreedLib = t;
            if (tFreedEngines >= 0 && tFreedLib >= 0) break;
        }
        chk (tFreedEngines >= 0 && tFreedEngines <= 6.0, "7a the voices' Organics engines are freed", fmt ("after %.2f s", tFreedEngines));
        chk (tFreedLib >= 0 && tFreedLib <= 6.0 && (runtime ? resBefore > 0 : true), "7b the library lets the instrument go (resident count back to 0) within 6 s",
             fmt ("resident before %.0f (%.0f bytes) · back to 0 after %.2f s", resBefore, (double) bytesBefore, tFreedLib));
    }

    // ═══ [8] PREVIEW ═══
    std::printf ("\n[8] organicsPreview\n");
    if (want (8))
    {
        Inst a; a.run (0.1);
        const auto f = tw::OrganicsLibrary::get().root().getChildFile ("test.sine").getChildFile ("preview.flac");
        a.p->organicsPreview ("test.sine"); a.clear(); a.run (0.5);
        const double r = rmsOf (a.L, 0, a.L.size());
        if (! f.existsAsFile()) chk (r < 1e-6, "8 no preview.flac in the fixture → organicsPreview is a silent no-op (no crash)", fmt ("rms %.1f dBFS", dbOf (r)));
        else chk (r > 1e-4, "8 organicsPreview plays preview.flac with no note held", fmt ("rms %.1f dBFS", dbOf (r)));
        a.p->organicsPreview ({}); a.run (0.1);
    }

    // ═══ [9] organicViz ═══
    std::printf ("\n[9] The organicViz feed\n");
    if (want (9))
    {
        Inst a; useOrganic (a, 0, "test.sine"); a.waitLoaded (0);
        a.p->uiClients_.store (1);                             // a page is watching (vizConsumersLive)
        a.block ({ {69,1} }); a.run (0.2);
        juce::Array<juce::var> ev; const bool got = a.p->takeOrganicViz (ev);
        const juce::String js = got ? juce::JSON::toString (ev[0], true) : juce::String();
        a.block ({ {69,0} }); a.run (1.2);
        juce::Array<juce::var> ev2; a.p->takeOrganicViz (ev2);
        pumpMs (400); juce::Array<juce::var> ev3; const bool after = a.p->takeOrganicViz (ev3);
        chk (got && js.contains ("\"n\": 69") && ! after, "9 the feed carries the sounding note while it sounds, and stops 300 ms after",
             (js.isEmpty() ? juce::String ("(nothing)") : js).toStdString() + fmt (" · after release+0.4 s: %.0f events", after ? (double) ev3.size() : 0.0));
    }


    // ═══ [10] OSC E + organicViz osc 4 ═══
    std::printf ("\n[10] Osc E (bank B) on Organics: sound + organicViz osc 4\n");
    if (want (10))
    {
        Inst a;
        setP (*a.p, ParameterIDs::SYN_OSC_A_ENABLE, 0.f);
        setP (*a.p, ParameterIDs::kOsc_ENABLE[4], 1.f);
        a.tick (4);
        useOrganic (a, 4, "test.sine");
        const auto st = a.waitLoaded (4);
        a.p->uiClients_.store (1);
        a.clear(); a.block ({ {69,1} }); a.run (0.4);
        juce::Array<juce::var> ev; const bool got = a.p->takeOrganicViz (ev);
        int oscSeen = -1; for (auto& e : ev) if ((int) e["osc"] >= 0) oscSeen = (int) e["osc"];
        const size_t s0 = (size_t) (0.1 * SR); double hz = 0;
        const double c = centsOff (a.L, s0, 8192, 440.0, &hz), r = rmsOf (a.L, s0, s0 + 8192);
        chk (st == "ok" && r > 1e-3 && std::fabs (c) <= 3.0 && got && oscSeen == 4, "10 osc E sounds at 440 Hz and its organicViz event is osc 4",
             ("status " + st.toStdString() + " · ") + fmt ("rms %.1f dBFS · %+.2f cents · viz events %.0f, osc %.0f", dbOf (r), c, (double) ev.size(), (double) oscSeen));
        a.p->uiClients_.store (0);
    }

    // ═══ [11] THE PATCHER ═══
    std::printf ("\n[11] The Patcher: rack routing, direct taps, a flow card\n");
    if (want (11))
    {
        struct Tp { bool rack, direct, inFilter, cutOut; };
        auto rend = [] (Tp t) {
            Inst a; useOrganic (a, 0, "test.sine"); a.waitLoaded (0);
            setP (*a.p, ParameterIDs::SYN_OSC_A_ORG_HUMAN, 0.f);
            setN (*a.p, "Synth OSC A Filter 1 Send", t.inFilter ? 1.f : 0.f);
            setP (*a.p, ParameterIDs::SYN_FILTER1_TYPE, 0.f); setP (*a.p, ParameterIDs::SYN_FILTER1_CUT, 60.f);
            if (t.cutOut) setN (*a.p, "Synth OSC A Out", 0.f);
            if (t.rack)
            {
                setN (*a.p, "Utility In Chain", 1.f); setN (*a.p, "Utility Power", 1.f); setN (*a.p, "Utility Chain Rank", 0.5f);
                setN (*a.p, "Utility SRC_A", 1.f);
                setN (*a.p, "Utility Direct Taps", t.direct ? 1.0f / 2047.0f : 0.f);
            }
            a.tick (60); a.run (0.2);
            a.clear(); a.block ({ {69,1} }); a.run (0.5);
            return a.L;
        };
        const auto viaRack = rend ({ true, false, false, true });
        const auto dark    = rend ({ true, false, true,  false });
        const auto tapped  = rend ({ true, true,  true,  false });
        const size_t s0 = (size_t) (0.15 * SR);
        const double lr = dbOf (rmsWin (viaRack, s0, 8192)), ld = dbOf (rmsWin (dark, s0, 8192)), lt = dbOf (rmsWin (tapped, s0, 8192));
        double hz = 0; const double c = centsOff (viaRack, s0, 8192, 440.0, &hz);
        chk (lr > -40.0 && std::fabs (c) <= 3.0, "11a output cable cut → the Organics osc reaches the output THROUGH Utility 1 (rack routing)",
             fmt ("%.1f dBFS at %+.2f cents", lr, c));
        chk (lt > ld + 20.0, "11b a direct tap hears the raw Organics engine past a closed filter; the post-filter tap is dark",
             fmt ("post-filter tap %.1f dBFS · direct tap %.1f dBFS", ld, lt));
        // (the flow cards — a Chop on an Organics osc — are gated on the REAL AU: Tests/au_patcher_rules.cpp with ORG_ID set)
    }

    // ═══ [12] THE RELEASE LINK ═══
    std::printf ("\n[12] The amp envelope's release lengthens the Organics release; the voice ends after both\n");
    if (want (12))
    {
        auto tail = [] (float ampRelMs, double& t60s, bool& voiceEnded) {
            Inst a; useOrganic (a, 0, "test.sine"); a.waitLoaded (0);
            setP (*a.p, ParameterIDs::SYN_OSC_A_ORG_HUMAN, 0.f);
            setP (*a.p, ParameterIDs::SYN_OSC_A_ORG_RELEASE, 0.f);          // the knob's shortest time (20 ms)
            setP (*a.p, ParameterIDs::SYN_ENV_AMP_R, ampRelMs);
            a.clear(); a.block ({ {69,1} }); a.run (0.5);
            const size_t off = a.L.size();
            a.block ({ {69,0} }); a.run (3.5);
            const double ref = rmsOf (a.L, off - 4800, off);
            t60s = -1;
            for (size_t w = off; w + 480 <= a.L.size(); w += 480) if (rmsOf (a.L, w, w + 480) < ref * 1e-3) { t60s = (double) (w - off) / SR; break; }
            voiceEnded = true;
            for (int i = 0; i < TerrainAudioProcessor::kSynthVoiceCount; ++i) if (auto* v = a.p->synthVoices_[(size_t) i]) if (v->isVoiceActive()) voiceEnded = false;
        };
        double tS = 0, tL = 0; bool eS = false, eL = false;
        tail (50.f, tS, eS); tail (2000.f, tL, eL);
        chk (tS > 0 && tL > 0 && tL > 1.5 && tS < 0.2 && eS && eL,
             "12 amp release 0.05 s → a short tail; 2 s → the Organics tail follows it (−60 dB), and both voices end",
             fmt ("−60 dB after note-off: amp 0.05 s → %.3f s · amp 2 s → %.3f s · voices ended %.0f / %.0f", tS, tL, eS ? 1 : 0, eL ? 1 : 0));
    }

    // ═══ [13] THE NEW PARAMETERS + DEST KNOB 3 = VIBRATO ═══
    std::printf ("\n[13] tp105 parameters A–H; mod dest knob 3 drives the Vibrato\n");
    if (want (13))
    {
        Inst a;
        int have = 0;
        for (int o = 0; o < ParameterIDs::kOscCount; ++o)
            for (auto* tbl : { ParameterIDs::kOsc_ORG_VIBRATO, ParameterIDs::kOsc_ORG_VIBRATE, ParameterIDs::kOsc_ORG_VIBDELAY, ParameterIDs::kOsc_ORG_VCURVE, ParameterIDs::kOsc_ORG_TUNING })
                have += a.p->apvts.getParameter (tbl[o]) != nullptr ? 1 : 0;
        const float dVr = a.p->apvts.getParameter (ParameterIDs::SYN_OSC_H_ORG_VIBRATE)->getDefaultValue();
        chk (have == 40 && std::fabs (dVr - 0.417f) < 1e-3, "13a VIBRATO/VIBRATE/VIBDELAY/VCURVE/TUNING declared for A–H (defaults per the amendment)",
             fmt ("%.0f of 40 · H VIBRATE default %.3f", have, dVr));
        auto wob = [] (float depth) {
            Inst b; useOrganic (b, 0, "test.sine"); b.waitLoaded (0);
            setP (*b.p, ParameterIDs::SYN_OSC_A_ORG_HUMAN, 0.f);
            setP (*b.p, ParameterIDs::SYN_OSC_A_ORG_VIBDELAY, 0.f);
            b.p->setSynthModMatrix ("[{\"s\":230,\"d\":" + juce::String (wc::organicDest (0, 3)) + ",\"v\":" + juce::String (depth) + "}]");
            juce::MidiBuffer cc; cc.addEvent (juce::MidiMessage::controllerEvent (1, 1, 127), 0);
            b.block ({}, &cc); b.run (0.1);
            b.clear(); b.block ({ {69,1} }); b.run (1.5);
            // per-cycle pitch spread by zero crossings over 0.6..1.5 s
            double lo = 1e9, hi = -1e9, last = -1;
            for (size_t i = (size_t) (0.6 * SR); i < b.L.size(); ++i)
                if (b.L[i - 1] < 0.f && b.L[i] >= 0.f)
                {
                    const double t = (double) (i - 1) + (double) (-b.L[i - 1]) / (double) (b.L[i] - b.L[i - 1]);
                    if (last >= 0) { const double c = 1200.0 * std::log2 ((SR / (t - last)) / 440.0); lo = std::min (lo, c); hi = std::max (hi, c); }
                    last = t;
                }
            return 0.5 * (hi - lo);
        };
        const double w0 = wob (0.f), w1 = wob (1.f);
        chk (w0 < 2.0 && w1 > 20.0, "13b mod wheel → dest 5275 (osc A knob 3) = VIBRATO: the pitch wobbles, and not at depth 0",
             fmt ("per-cycle pitch spread (test.sine's marker partial jitters the zero crossings ~1 ¢): route depth 0 ±%.2f cents · depth 1 ±%.1f cents", w0, w1));
    }
    // ═══════════════════════════════════════════════════════════════════════════════════════════════════════════
    //  tp106 — THE FINAL OVERPASS: robustness on the shipping processor
    // ═══════════════════════════════════════════════════════════════════════════════════════════════════════════

    // ═══ [14] HOST SAMPLE RATES ═══
    std::printf ("\n[14] Host sample rates 44.1 / 96 kHz (the fixture is 48 kHz): pitch exact, the held loop clean\n");
    if (want (14))
        for (double fs : { 44100.0, 96000.0 })
        {
            Inst a (fs); useOrganic (a, 0, "test.sine"); a.waitLoaded (0);
            setP (*a.p, ParameterIDs::SYN_OSC_A_ORG_HUMAN, 0.f);
            a.clear(); a.block ({ {69,1} }); a.run (3.0);
            double hz = 0; const double c = centsOff (a.L, (size_t) (0.3 * fs), 16384, 440.0, &hz, fs);
            const auto k = clickScan (a.L, (size_t) (0.06 * fs), a.L.size(), fs);
            chk (std::fabs (c) <= 3.0 && ! k.hit && rmsOf (a.L, (size_t) (0.3 * fs), (size_t) (2.9 * fs)) > 1e-3,
                 fs < 48000 ? "14a 44.1 kHz host: note 69 at 440 Hz (±3 ¢), 3 s through the loop seams without a click"
                            : "14b 96 kHz host: note 69 at 440 Hz (±3 ¢), 3 s through the loop seams without a click",
                 fmt ("%+.2f cents · worst HP excess %.1f dB (%.1f dB re local)", c, k.ex, k.rel));
        }

    // ═══ [15] OFFLINE BOUNCE ═══
    std::printf ("\n[15] Offline bounce (non-realtime → the 8-tap sinc reader): the same note, the same level, clean\n");
    if (want (15))
    {
        auto render = [] (bool offline, double& cents, double& rms, ClickHit& k) {
            Inst a (SR, offline); useOrganic (a, 0, "test.sine"); a.waitLoaded (0);
            setP (*a.p, ParameterIDs::SYN_OSC_A_ORG_HUMAN, 0.f);
            a.clear(); a.block ({ {69,1} }); a.run (2.0); a.block ({ {69,0} }); a.run (1.0);
            cents = centsOff (a.L, (size_t) (0.3 * SR), 16384, 440.0);
            rms = rmsOf (a.L, (size_t) (0.3 * SR), (size_t) (1.9 * SR));
            k = clickScan (a.L, (size_t) (0.06 * SR), a.L.size(), SR);
        };
        double cR = 0, rR = 0, cO = 0, rO = 0; ClickHit kR, kO;
        render (false, cR, rR, kR); render (true, cO, rO, kO);
        chk (std::fabs (cO) <= 3.0 && std::fabs (dbOf (rO) - dbOf (rR)) < 0.5 && ! kO.hit,
             "15 bounce: pitch ±3 ¢, level within 0.5 dB of the realtime render, no click (hold, loop, release)",
             fmt ("offline %+.2f ¢ %.2f dBFS · realtime %+.2f ¢ %.2f dBFS · offline worst HP excess %.1f dB", cO, dbOf (rO), cR, dbOf (rR)) + fmt (" (realtime %.1f)", kR.ex));
    }

    // ═══ [16] A4 = 432 ═══
    std::printf ("\n[16] Settings: A4 = 432 Hz retunes Organics like every engine\n");
    if (want (16))
    {
        Inst a; useOrganic (a, 0, "test.sine"); a.waitLoaded (0);
        setP (*a.p, ParameterIDs::SYN_OSC_A_ORG_HUMAN, 0.f);
        a.p->setTuningA4 (432.f, false);
        a.clear(); a.block ({ {69,1} }); a.run (0.8);
        double hz = 0; const double c = centsOff (a.L, (size_t) (0.25 * SR), 16384, 432.0, &hz);
        a.p->setTuningA4 (440.f, false);
        chk (std::fabs (c) <= 3.0, "16 A4 432: note 69 sounds at 432 Hz (±3 ¢)", fmt ("peak %.2f Hz = %+.2f cents re 432", hz, c));
    }

    // ═══ [17] MPE PER-NOTE BEND ═══
    std::printf ("\n[17] MPE: a per-note bend moves only its own Organics note\n");
    if (want (17))
    {
        Inst a; useOrganic (a, 0, "test.sine"); a.waitLoaded (0);
        setP (*a.p, ParameterIDs::SYN_OSC_A_ORG_HUMAN, 0.f);
        a.p->setMpeOn (true, 48.f, false);
        juce::MidiBuffer on; on.addEvent (juce::MidiMessage::noteOn (2, 60, (juce::uint8) 100), 0); on.addEvent (juce::MidiMessage::noteOn (3, 67, (juce::uint8) 100), 0);
        a.clear(); a.block ({}, &on); a.run (0.4);
        juce::MidiBuffer bend; bend.addEvent (juce::MidiMessage::pitchWheel (2, 8192 + 8192 * 2 / 48), 0);   // +2 st on ch 2
        a.block ({}, &bend); a.run (0.8);
        const size_t s0 = a.L.size() - (size_t) (0.5 * SR);
        const double f60 = 440.0 * std::pow (2.0, (60 - 69) / 12.0), f62 = 440.0 * std::pow (2.0, (62 - 69) / 12.0), f67 = 440.0 * std::pow (2.0, (67 - 69) / 12.0);
        const double d60 = partialDb (a.L, s0, 16384, f60), d62 = partialDb (a.L, s0, 16384, f62), d67 = partialDb (a.L, s0, 16384, f67);
        const double c62 = centsOff (a.L, s0, 16384, f62);
        a.p->setMpeOn (false, 48.f, false);
        chk (d62 > -40.0 && d67 > -40.0 && d60 < d62 - 30.0 && std::fabs (c62) <= 3.0,
             "17 ch-2 bend +2 st: its note moves 60 → 62 (±3 ¢), the ch-3 note (67) stays",
             fmt ("partial 62 %.1f dBFS (%+.2f ¢) · 67 %.1f dBFS · 60 %.1f dBFS", d62, c62, d67, d60));
    }

    // ═══ [18] SWITCH THE INSTRUMENT UNDER A HELD NOTE ═══
    std::printf ("\n[18] Switch the instrument while a note sounds: the old note fades (5 ms), no click\n");
    if (want (18))
    {
        Inst a; useOrganic (a, 0, "test.sine"); a.waitLoaded (0);
        a.clear(); a.block ({ {69,1} }); a.run (0.5);
        const size_t at = a.L.size();
        a.p->organicsSetInstrument (0, "test.piano");
        const auto st = a.waitLoaded (0);
        a.run (0.5);
        const auto k = clickScan (a.L, at - (size_t) (0.02 * SR), a.L.size(), SR);
        chk (st == "ok" && ! k.hit, "18 test.sine → test.piano under a held note: loads, and the swap is click-free",
             ("status " + st + " · ").toStdString() + fmt ("worst HP excess %.1f dB (%.1f dB re local) after the swap", k.ex, k.rel));
    }

    // ═══ [19] SWITCH THE ENGINE UNDER A HELD NOTE ═══
    std::printf ("\n[19] Switch osc A's engine Organics → Wavetable (and back) while a note sounds: faded, no click\n");
    if (want (19))
    {
        Inst a; useOrganic (a, 0, "test.sine"); a.waitLoaded (0);
        a.clear(); a.block ({ {69,1} }); a.run (0.5);
        const size_t at = a.L.size();
        setP (*a.p, ParameterIDs::kOsc_ENGINE[0], 0.f); a.run (0.5);
        const size_t back = a.L.size();
        setP (*a.p, ParameterIDs::kOsc_ENGINE[0], 7.f); a.run (0.5);
        // → WT: the osc fades out through its 4 ms gate, switches at silence, and the Wavetable fades in
        const auto k1 = clickScan (a.L, at - (size_t) (0.02 * SR), back, SR);
        // → Organics: the held note resumes from silence (the osc was faded out): the fade-in must be a ramp, never a
        //   step (its first millisecond ≥ 20 dB under the level 20 ms on), and nothing clicks once it is in
        size_t i0 = back, quiet = 0;                                     // the resume = the first sound after the silent gap
        for (; i0 < a.L.size(); ++i0) { if (std::fabs (a.L[i0]) < 1.0e-6f) ++quiet; else if (quiet >= (size_t) (0.005 * SR)) break; else quiet = 0; }
        double pk0 = 0, pk1 = 0;
        for (size_t i = i0; i < std::min (a.L.size(), i0 + (size_t) (0.001 * SR)); ++i) pk0 = std::max (pk0, (double) std::fabs (a.L[i]));
        for (size_t i = i0 + (size_t) (0.02 * SR); i < std::min (a.L.size(), i0 + (size_t) (0.04 * SR)); ++i) pk1 = std::max (pk1, (double) std::fabs (a.L[i]));
        const auto k2 = clickScan (a.L, std::min (a.L.size(), i0 + (size_t) (0.01 * SR)), a.L.size(), SR);
        // (a sampler engine switched in under a held note may also stay silent until the next note — that is not a click)
        const bool resumed = i0 < a.L.size();
        chk (! k1.hit && ! k2.hit && (! resumed || dbOf (pk0) < dbOf (pk1) - 20.0),
             "19 Organics → WT → Organics under a held note: faded both ways, no click",
             fmt ("→ WT worst HP excess %.1f dB (%.1f re local) · → Organics: first ms %.1f dB under the level 20 ms on, worst HP excess after %.1f dB",
                  k1.ex, k1.rel, dbOf (pk1) - dbOf (pk0), k2.ex)
               + (resumed ? fmt (" · resumes %.1f ms after the switch", 1000.0 * ((double) i0 - (double) back) / SR)
                          : std::string (" · Organics waits for the next note (the held one was the Wavetable's)")));
    }

    // ═══ [20] A PRESET LOAD DURING A LONG ORGANICS RELEASE ═══
    std::printf ("\n[20] A preset load in the middle of a 12 s Organics release: the tail is flushed (faded), no click, no bleed\n");
    if (want (20))
    {
        juce::MemoryBlock fresh; { Inst z; z.p->getStateInformation (fresh); }
        Inst a; useOrganic (a, 0, "test.piano"); a.waitLoaded (0);
        setP (*a.p, ParameterIDs::SYN_OSC_A_ORG_RELEASE, 1.f);
        a.clear(); a.block ({ {60,1} }); a.run (0.5); a.block ({ {60,0} }); a.run (0.6);
        const double tailDb = dbOf (rmsOf (a.L, a.L.size() - (size_t) (0.1 * SR), a.L.size()));
        // the host's way: the audio thread keeps calling processBlock while the MESSAGE thread restores the state (the
        // load waits for the audio thread's 10 ms fade + hold before it touches anything — ScopedLoadMute)
        const size_t at = a.L.size();
        std::atomic<bool> go { true };
        std::thread audio ([&] { while (go.load()) a.block(); });
        std::this_thread::sleep_for (std::chrono::milliseconds (40));
        a.p->setStateInformation (fresh.getData(), (int) fresh.getSize());
        std::this_thread::sleep_for (std::chrono::milliseconds (120));
        go = false; audio.join();
        a.run (0.4);
        const auto k = clickScan (a.L, at - (size_t) (0.02 * SR), a.L.size(), SR);
        const double afterDb = dbOf (rmsOf (a.L, a.L.size() - (size_t) (0.3 * SR), a.L.size()));
        chk (tailDb > -60.0 && afterDb < -90.0 && ! k.hit, "20 the release was still ringing, the load silences it (faded) and nothing bleeds after",
             fmt ("tail before the load %.1f dBFS · after it %.1f dBFS · worst HP excess %.1f dB (%.1f re local)", tailDb, afterDb, k.ex, k.rel));
    }

    // ═══ [21] / [22] A CORRUPT map.json · A MISSING LIBRARY FOLDER ═══
    std::printf ("\n[21] A corrupt map.json and [22] a missing library folder: silent, reported, no crash\n");
    if (want (21) || want (22))
    {
        const juce::String envWas = std::getenv ("TERRAIN_ORGANICS_DIR") ? std::getenv ("TERRAIN_ORGANICS_DIR") : "";
        const auto fixRoot = juce::File (envWas);
        const auto tmp = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("tp106_org_corrupt");
        tmp.deleteRecursively(); tmp.createDirectory();
        fixRoot.getChildFile ("index.json").copyFileTo (tmp.getChildFile ("index.json"));
        fixRoot.getChildFile ("ids.json").copyFileTo (tmp.getChildFile ("ids.json"));
        fixRoot.getChildFile ("test.sine").copyDirectoryTo (tmp.getChildFile ("test.broken"));
        tmp.getChildFile ("test.broken").getChildFile ("map.json").replaceWithText ("{ \"regions\": [ { \"kind\": \"attack\", \"smp\": ");
        auto probe = [] (const char* id, juce::String& status) {
            Inst a; useOrganic (a, 0, id); status = a.waitLoaded (0);
            a.clear(); a.block ({ {69,1} }); a.run (0.4); a.block ({ {69,0} }); a.run (0.2);
            return dbOf (rmsOf (a.L, 0, a.L.size()));
        };
        if (want (21))
        {
            setenv ("TERRAIN_ORGANICS_DIR", tmp.getFullPathName().toRawUTF8(), 1); tw::OrganicsLibrary::get().rescan();
            juce::String st; const double lv = probe ("test.broken", st);
            chk (st == "missing" && lv < -90.0, "21 a truncated map.json: the load fails cleanly → status \"missing\", silence, no crash",
                 ("status " + st + " · ").toStdString() + fmt ("%.1f dBFS", lv));
        }
        if (want (22))
        {
            setenv ("TERRAIN_ORGANICS_DIR", tmp.getChildFile ("no_such_folder").getFullPathName().toRawUTF8(), 1); tw::OrganicsLibrary::get().rescan();
            Inst z; const auto idx = z.p->organicsIndexJson();
            juce::String st; const double lv = probe ("test.rr", st);    // never loaded in this process (the RAM cache would answer)
            chk (st == "missing" && lv < -90.0 && idx.removeCharacters (" \n").startsWith ("[]"),
                 "22 the library folder is gone: an empty index, the instrument \"missing\", silence, no crash",
                 ("index " + idx.substring (0, 24) + " · status " + st + " · ").toStdString() + fmt ("%.1f dBFS", lv));
        }
        setenv ("TERRAIN_ORGANICS_DIR", envWas.toRawUTF8(), 1); tw::OrganicsLibrary::get().rescan();
        tmp.deleteRecursively();
    }

    // ═══ [23] LFO → EVERY ORGANICS DESTINATION ═══
    std::printf ("\n[23] The mod matrix: LFO 1 → each of the ten Organics knobs moves the sound (vs the same note unrouted)\n");
    if (want (23))
    {
        // feature: 0 centroid · 1 level · 2 pitch · 3 side/mid (held notes, detrended wobble) — or, for the knobs a note
        // reads at its start or its end, the spread over eight presses at different LFO phases: 4 level + pitch (Human, drawn
        // at note-on) · 6 the HF level of the note-on noise (Noise; the fixture's noise is 100 ms) · 7 the tail energy
        // after the note-off (Release)
        struct D { int knob; const char* name; const char* inst; int note; int feature; };
        const D ds[10] = { { 0, "Dynamics", "test.layers", 60, 0 }, { 1, "Tone", "test.sine", 69, 0 }, { 2, "Body", "test.sine", 57, 0 },
                           { 3, "Vibrato", "test.sine", 69, 2 }, { 4, "Human", "test.sine", 69, 4 }, { 5, "Release", "test.piano", 60, 7 },
                           { 6, "Noise", "test.noisy", 60, 6 }, { 7, "Sustain", "test.piano", 60, 1 }, { 8, "Velocity", "test.sine", 69, 1 },
                           { 9, "Image", "test.norr", 60, 3 } };
        int ok = 0; std::string rows;
        for (const auto& d : ds)
        {
            auto measure = [&] (bool routed) {
                Inst a; useOrganic (a, 0, d.inst); a.waitLoaded (0);
                setP (*a.p, ParameterIDs::SYN_OSC_A_ORG_HUMAN, 0.f);
                setP (*a.p, ParameterIDs::LFO1_RATE, 3.0f);
                if (routed) a.p->setSynthModMatrix ("[{\"s\":0,\"d\":" + juce::String (wc::organicDest (0, d.knob)) + ",\"v\":1}]");
                a.run (0.05); a.clear();
                if (d.feature >= 4)
                {   // a knob read at the note's start or end: eight presses at different LFO phases
                    std::vector<double> lv;
                    // tp107: the noise is a round-robin now (a take sounds on ~2 notes in 3 at the knob's 0.5, ±3 dB): the feature is
                    //   the spread of the SOUNDING presses' level over 16 presses (a miss has no level to spread)
                    const int nPress = d.feature == 6 ? 16 : 8;
                    for (int k = 0; k < nPress; ++k)
                    {
                        const size_t s = a.L.size();
                        a.block ({ { d.note, 1 } }); a.run (0.23);
                        const size_t off = a.L.size();
                        a.block ({ { d.note, 0 } }); a.run (d.feature == 7 ? 0.45 : 0.1);
                        if (d.feature == 4) lv.push_back (dbOf (rmsOf (a.L, s + (size_t) (0.05 * SR), s + (size_t) (0.2 * SR)))
                                                          + 0.25 * centsOff (a.L, s + (size_t) (0.02 * SR), 8192, 440.0));
                        else if (d.feature == 6)
                        {
                            // the on-burst's own 3 kHz (Goertzel): the HP-4k window also caught the previous press's 5 kHz off-burst,
                            // which now comes and goes too
                            const double l = partialDb (a.L, s, (size_t) (0.1 * SR), 3000.0);
                            if (l > -80.0) lv.push_back (l);
                        }
                        else lv.push_back (dbOf (rmsOf (a.L, off + (size_t) (0.05 * SR), off + (size_t) (0.4 * SR))));
                    }
                    if (lv.size() < 2) return 0.0;
                    double m = 0; for (double v : lv) m += v; m /= (double) lv.size();
                    double q = 0; for (double v : lv) q += (v - m) * (v - m); return std::sqrt (q / (double) lv.size());
                }
                a.block ({ { d.note, 1 } }); a.run (2.0);
                return featureWobble (a.L, a.R, (size_t) (0.3 * SR), a.L.size(), d.feature, d.note);
            };
            const double base = measure (false), mod = measure (true);
            const bool pass = mod > 3.0 * base + 0.05;
            ok += pass ? 1 : 0;
            rows += std::string (d.name) + fmt (" %.2f→%.2f", base, mod) + (pass ? " · " : " ✗ · ");
        }
        chk (ok == 10, "23 LFO 1 → Dynamics, Tone, Body, Vibrato, Human, Release, Noise, Sustain, Velocity, Image: each moves its feature > 3× the unrouted note",
             rows + "(feature wobble, unrouted → routed)");
    }

    // ═══ [24] THE ENSEMBLE: loudness vs players, 2-player phasiness, 16 players × 8 notes CPU ═══
    std::printf ("\n[24] Ensemble (unison = players) on the installed violin section: a gentle loudness law, no phasing at 2, CPU at 16 × 8\n");
    if (want (24))
    {
        const juce::String envWas = std::getenv ("TERRAIN_ORGANICS_DIR") ? std::getenv ("TERRAIN_ORGANICS_DIR") : "";
        juce::File lib ("~/Library/WavesCrate/TerrainInstrument/Organics");
        if (! lib.getChildFile ("vsco2.strings.violin-section").isDirectory()) lib = juce::File ("~/Library/WavesCrate/Terrain/Organics");
        if (! lib.getChildFile ("vsco2.strings.violin-section").isDirectory())
            skip ("24 the ensemble law", "the installed library has no vsco2.strings.violin-section");
        else
        {
            setenv ("TERRAIN_ORGANICS_DIR", lib.getFullPathName().toRawUTF8(), 1); tw::OrganicsLibrary::get().rescan();
            const int ns[8] = { 1, 2, 3, 4, 6, 8, 12, 16 };
            double lv[8] = {}, rip[8] = {};
            for (int i = 0; i < 8; ++i)
            {
                Inst a; useOrganic (a, 0, "vsco2.strings.violin-section"); a.waitLoaded (0, 10.0);
                setP (*a.p, ParameterIDs::SYN_OSC_A_ORG_HUMAN, 0.25f);
                setN (*a.p, "Synth OSC A Unison", (float) (ns[i] - 1) / 15.0f);
                a.run (0.05); a.clear();
                a.block ({ {67,1} }); a.run (3.0);
                const size_t s0 = (size_t) (1.0 * SR), s1 = a.L.size();
                double e = 0; for (size_t k = s0; k < s1; ++k) e += 0.5 * ((double) a.L[k] * a.L[k] + (double) a.R[k] * a.R[k]);
                lv[i] = 10.0 * std::log10 (e / (double) (s1 - s0) + 1e-20);
                rip[i] = featureWobble (a.L, a.R, s0, s1, 1, 67);
            }
            double worstStep = 0, rise = 0;
            std::string law;
            for (int i = 0; i < 8; ++i)
            {
                law += fmt ("%.0f:%+.1f ", ns[i], lv[i] - lv[0]);
                if (i > 0) { worstStep = std::max (worstStep, std::abs (lv[i] - lv[i - 1])); rise = std::max (rise, lv[i] - lv[0]); }
            }
            const double drop16 = lv[0] - lv[7];
            chk (worstStep <= 1.5 && rise <= 2.0 && drop16 <= 6.0,
                 "24a loudness vs players 1 → 16: a gentle law (no step > 1.5 dB, never louder by > 2 dB, 16 players ≤ 6 dB quieter)",
                 law + fmt ("dB · worst step %.2f dB", worstStep));
            chk (rip[1] <= rip[0] + 1.5, "24b 2 players do not phase: the 21 ms level wobble stays within 1.5 dB of 1 player's",
                 fmt ("level wobble (detrended SD, dB): 1 player %.2f · 2 players %.2f · 16 players %.2f", rip[0], rip[1], rip[7]));
            {
                Inst a; useOrganic (a, 0, "vsco2.strings.violin-section"); a.waitLoaded (0, 10.0);
                setN (*a.p, "Synth OSC A Unison", 1.0f);
                a.run (0.05);
                a.block ({ {55,1},{59,1},{62,1},{66,1},{67,1},{71,1},{74,1},{79,1} });
                a.run (0.3);
                const int nb = 200; const double t0 = juce::Time::getMillisecondCounterHiRes();
                for (int b = 0; b < nb; ++b) a.block();
                const double us = 1000.0 * (juce::Time::getMillisecondCounterHiRes() - t0) / nb;
                const int live = tw::organics_debug::lastLiveReaders != nullptr ? tw::organics_debug::lastLiveReaders() : -1;
                chk (us < 0.5 * 1.0e6 * BLK / SR, "24c 16 players × 8 notes: the block renders in under half its real-time budget",
                     fmt ("%.0f µs per %.0f-frame block (%.1f %% of %.0f µs) · live readers in the last engine %.0f", us, BLK, 100.0 * us / (1.0e6 * BLK / SR), 1.0e6 * BLK / SR) + fmt (" %.0f", live));
            }
            setenv ("TERRAIN_ORGANICS_DIR", envWas.toRawUTF8(), 1); tw::OrganicsLibrary::get().rescan();
        }
    }

    // ═══ [25] tp107 — ATTACK IS BACK: the knob, the amp envelope's attack (onset = the max), and a mod route to 5352 + osc ═══
    std::printf ("\n[25] tp107 Attack: onset vs the knob and the amp attack (the max), LFO 1 → the Attack dest (A and bank B's E)\n");
    if (want (25))
    {
        // onset = time until the 5 ms RMS first reaches −1 dB of the held level (3.6..4.0 s), on the looped test.sine, note 69
        auto onsetOf = [] (const std::vector<float>& x, size_t s0) {
            const size_t W = 240; double ref = rmsOf (x, s0 + (size_t) (3.6 * SR), s0 + (size_t) (4.0 * SR));
            size_t k = s0; while (k + W < x.size() && rmsOf (x, k, k + W) < ref * 0.891) k += 48;
            return 1000.0 * (double) (k + W / 2 - s0) / SR;
        };
        auto press = [&] (int osc, float knob, float ampAttMs) {
            Inst a;
            if (osc != 0) { setP (*a.p, ParameterIDs::SYN_OSC_A_ENABLE, 0.f); a.tick (4); }
            useOrganic (a, osc, "test.sine"); a.waitLoaded (osc);
            setP (*a.p, ParameterIDs::kOsc_ORG_HUMAN[osc], 0.f);
            setP (*a.p, ParameterIDs::kOsc_ORG_ATTACK[osc], knob);
            setP (*a.p, ParameterIDs::SYN_ENV_AMP_A, ampAttMs);
            a.run (0.05); a.clear();
            a.block ({ { 69, 1 } }); a.run (4.1);
            return onsetOf (a.L, 0);
        };
        const float ks[5] = { 0.f, 0.25f, 0.5f, 0.75f, 1.f };
        double o1[5], o1000[5]; std::string t;
        for (int i = 0; i < 5; ++i) { o1[i] = press (0, ks[i], 1.f); o1000[i] = press (0, ks[i], 1000.f); t += fmt ("%.0f%%: %.1f / %.0f ms · ", 100 * ks[i], o1[i], o1000[i]); }
        std::printf ("        onset (amp attack 1 ms / 1000 ms): %s\n", t.c_str());
        chk (o1[2] < 10.0 && o1[3] > 100.0 && o1[3] < 260.0 && o1[4] > 1800.0 && o1[4] < 3200.0,
             "25a the knob: Natural fast, 75 % ≈ 0.17 s, 100 % ≈ 3 s (log taper through the top half)", t);
        // (the amp envelope's own attack curve reaches −1 dB at ~0.23 of its time: 1 s → ≈ 230 ms; the point is the MAX)
        chk (std::abs (o1000[2] - o1000[3]) < 5.0 && o1000[2] > 20.0 * o1[2] && std::abs (o1000[0] - o1000[2]) < 5.0 && std::abs (o1000[4] - o1[4]) < 50.0,
             "25b onset = max(amp attack, knob): a 1 s amp attack lengthens 0–75 % to the envelope's own onset; 100 % (3 s) stays the knob's", t);
        // LFO 1 (0.4 Hz) → Attack of A (5352) and E (5356, bank B rebased): eight presses at different LFO phases → onset spread
        auto spread = [&] (int osc, bool routed) {
            Inst a;
            if (osc != 0) { setP (*a.p, ParameterIDs::SYN_OSC_A_ENABLE, 0.f); a.tick (4); }
            useOrganic (a, osc, "test.sine"); a.waitLoaded (osc);
            setP (*a.p, ParameterIDs::kOsc_ORG_HUMAN[osc], 0.f);
            setP (*a.p, ParameterIDs::kOsc_ORG_ATTACK[osc], 0.75f);
            setP (*a.p, ParameterIDs::LFO1_RATE, 0.4f);
            if (routed) a.p->setSynthModMatrix ("[{\"s\":0,\"d\":" + juce::String (wc::organicAttackDest (osc)) + ",\"v\":0.5}]");
            a.run (0.05); a.clear();
            std::vector<double> on;
            for (int k = 0; k < 8; ++k)
            {
                const size_t s = a.L.size();
                a.block ({ { 69, 1 } }); a.run (0.9);
                const double ref = rmsOf (a.L, s + (size_t) (0.75 * SR), s + (size_t) (0.85 * SR));
                size_t q = s; while (q + 240 < a.L.size() && rmsOf (a.L, q, q + 240) < ref * 0.5) q += 48;
                on.push_back (1000.0 * (double) (q - s) / SR);
                a.block ({ { 69, 0 } }); a.run (0.35);
            }
            double m = 0; for (double v : on) m += v; m /= 8; double sd = 0; for (double v : on) sd += (v - m) * (v - m);
            return std::sqrt (sd / 8);
        };
        const double sA0 = spread (0, false), sA1 = spread (0, true), sE0 = spread (4, false), sE1 = spread (4, true);
        chk (sA1 > 3.0 * sA0 + 5.0 && sE1 > 3.0 * sE0 + 5.0, "25c LFO 1 → the Attack dest moves the onset (osc A 5352, osc E 5356 through bank B's rebase)",
             fmt ("onset (−6 dB) SD over 8 presses: A %.1f → %.1f ms · E %.1f → %.1f ms (unrouted → routed)", sA0, sA1, sE0, sE1));
    }

    // ═══ [26] tp112c — A DIFFERENT INSTRUMENT STARTS FROM THE DEFAULTS (the page's switch; the state restore is untouched) ═══
    std::printf ("\n[26] tp112c: switching to another instrument resets the Organics knobs; re-picking the same one keeps them\n");
    if (want (26))
    {
        Inst a; useOrganic (a, 0, "test.sine"); a.waitLoaded (0);
        auto* hu = a.p->apvts.getParameter (ParameterIDs::kOsc_ORG_HUMAN[0]); auto* to = a.p->apvts.getParameter (ParameterIDs::kOsc_ORG_TONE[0]);
        hu->setValueNotifyingHost (0.05f); to->setValueNotifyingHost (0.9f);
        a.p->organicsSwitchInstrument (0, "test.sine");                          // the same one: kept
        const float keptH = hu->getValue(), keptT = to->getValue();
        a.p->organicsSwitchInstrument (0, "test.piano"); a.waitLoaded (0);         // another one: defaults
        const float resH = hu->getValue(), resT = to->getValue();
        chk (std::abs (keptH - 0.05f) < 1e-4f && std::abs (keptT - 0.9f) < 1e-4f
             && std::abs (resH - hu->getDefaultValue()) < 1e-6f && std::abs (resT - to->getDefaultValue()) < 1e-6f,
             "26 Human / Tone: kept on re-picking test.sine, back to default on test.piano",
             fmt ("same %.2f / %.2f · switched %.2f / %.2f (normalised; switched must equal the defaults)", keptH, keptT, resH, resT));
    }

    std::printf ("\norganics_integration_cert: %d PASS · %d FAIL · %d SKIP\n", npass, nfail, nskip);
    return nfail ? 1 : 0;
}
