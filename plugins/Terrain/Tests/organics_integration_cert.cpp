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
    Inst()
    {
        p = std::make_unique<TerrainAudioProcessor>();
        p->setPlayConfigDetails (0, 2, SR, BLK);
        p->prepareToPlay (SR, BLK);
        if (std::getenv ("ORG_NONRT")) p->setNonRealtime (true);   // the offline-bounce path (8-tap sinc)
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
    void run (double sec) { const int n = (int) std::ceil (sec * SR / BLK); for (int b = 0; b < n; ++b) { block(); if (b & 1) tick(); } }
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
static double centsOff (const std::vector<float>& x, size_t a, size_t n, double nominal, double* peakHz = nullptr)
{
    if (a + n > x.size()) n = x.size() > a ? x.size() - a : 0;
    if (n < 4096) return 1e9;
    double best = 0, bestF = nominal;
    for (double c = -60.0; c <= 60.0; c += 0.25)
    {
        const double f = nominal * std::pow (2.0, c / 1200.0), w = 2.0 * juce::MathConstants<double>::pi * f / SR;
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
    std::printf ("\norganics_integration_cert: %d PASS · %d FAIL · %d SKIP\n", npass, nfail, nskip);
    return nfail ? 1 : 0;
}
