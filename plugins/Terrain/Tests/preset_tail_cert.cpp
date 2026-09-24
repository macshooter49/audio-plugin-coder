// ══════════════════════════════════════════════════════════════════════════════════════════════
//  preset_tail_cert.cpp — A PRESET LOAD EMPTIES EVERY TAIL, WITHOUT A CLICK. Measured on the real processor.
//
//  Linked against the built libTerrain_SharedCode.a (Tests/preset_tail_cert.sh), so every number is the
//  shipping processBlock. Deterministic (TERRAIN_DETERMINISTIC=1), 48 kHz, 512-sample blocks.
//
//  The bug: flushAudioTails() ran on every load but never reset the rack Delay or the rack reverbs, so the
//  previous preset's Hall and its feedback Delay went on ringing through the new one.
//
//  The "wet" patch: rack Hall (decay 0.85, mix 0.7) + rack Delay (Digital, unsynced, feedback 0.85,
//  mix 0.5), both on osc A; [L]/[S] ring it through a 700 Hz Ladder LP. A note (C4) plays 0.25 s, is released, and at 0.8 s — the tails still loud — a
//  patch is loaded through each path a user or a host can take. The audio runs on its own thread, paced in real
//  time like a host callback, and the load is called from this thread while it runs (a load RACES processBlock):
//      file   loadPatchFromFile (the preset browser, a dropped .terrain)      → Init / the wet patch again
//      init   initPatch()      (the header's Init)
//      host   setStateInformation (a DAW recalling a program / an undo / a project reopening into a live window)
//      prog   setCurrentProgram (a host program change → loadPreset)
//      dice   requestTailFlush() (what the page's whole-preset dice roll calls through the flushTails native)
//  For each:
//      L1 the old tail is below −90 dBFS within 20 ms of the load, and stays there until a new note (2.0 s)
//      L2 no click: the largest sample-to-sample step in the 20 ms from the load (the step INTO the first
//         loaded sample included) is no bigger than the signal's own largest step in the 10 ms before it
//      L3 a note played after the load sounds (the voices and the fade-in recover)
//  (PTC_ENV=1 prints the 5 ms envelope around each load.)
//  and from silence (a fresh instance, nothing ringing):
//      F1 the new patch's first note is BIT-IDENTICAL to the reference written by the unfixed build
//         (write it with `preset_tail_cert.sh write-ref` on the old code, then run the check on the new)
//
//    sh Tests/preset_tail_cert.sh            (build Terrain first)
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
#define private public
#define protected public
#include "PluginProcessor.h"
#undef private
#undef protected

static int npass = 0, nfail = 0;
static void chk (bool ok, const std::string& what, const std::string& d = "")
{ std::printf ("  %s  %s\n", ok ? "PASS " : "FAIL ", what.c_str()); if (! d.empty()) std::printf ("        %s\n", d.c_str()); ok ? ++npass : ++nfail; std::fflush (stdout); }
static std::string fmt (const char* f, double a = 0, double b = 0, double c = 0, double d = 0)
{ char s[512]; std::snprintf (s, sizeof s, f, a, b, c, d); return s; }
static double dB (double x) { return 20.0 * std::log10 (std::max (1e-12, x)); }

struct PH : juce::AudioPlayHead
{
    double sr = 48000.0, bpm = 120.0; int64_t t = 0; bool playing = false;
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo p; p.setBpm (bpm); p.setIsPlaying (playing); p.setTimeInSamples (t);
        p.setPpqPosition ((double) t / sr * bpm / 60.0); p.setTimeSignature (juce::AudioPlayHead::TimeSignature {});
        return p;
    }
};

static constexpr double SR = 48000.0; static constexpr int BLK = 512;
static void setP (TerrainAudioProcessor& p, const char* id, float plain)
{
    auto* prm = p.apvts.getParameter (id);
    if (prm == nullptr) { std::printf ("!! no param %s\n", id); std::exit (2); }
    prm->setValueNotifyingHost (prm->convertTo0to1 (plain));
}

struct Inst
{
    std::unique_ptr<TerrainAudioProcessor> p; PH ph; juce::AudioBuffer<float> buf { 2, BLK };
    std::vector<float> L, R;
    Inst()
    {
        p = std::make_unique<TerrainAudioProcessor>();
        ph.sr = SR; p->setPlayHead (&ph);
        p->setPlayConfigDetails (0, 2, SR, BLK);
        p->prepareToPlay (SR, BLK);
        p->setSleepWhenSilent (false); p->setCutTailsOnStop (false); p->setQualityPrefs (1, 1, true);
    }
    void block (std::initializer_list<std::pair<int,int>> ev = {})
    {
        buf.clear(); juce::MidiBuffer m;
        for (auto e : ev) m.addEvent (e.second ? juce::MidiMessage::noteOn (1, e.first, (juce::uint8) 100) : juce::MidiMessage::noteOff (1, e.first), 0);
        p->processBlock (buf, m);
        ph.t += BLK;
        L.insert (L.end(), buf.getReadPointer (0), buf.getReadPointer (0) + BLK);
        R.insert (R.end(), buf.getReadPointer (1), buf.getReadPointer (1) + BLK);
    }
    void secs (double s) { for (int b = 0, n = (int) std::lround (s * SR / BLK); b < n; ++b) block(); }
    float absAt (size_t i) const { return std::max (std::abs (L[i]), std::abs (R[i])); }
    float stepAt (size_t i) const { return std::max (std::abs (L[i] - L[i-1]), std::abs (R[i] - R[i-1])); }
};

static juce::File dir;
//  [L]/[S] ring the DARK patch (the wet one through a Ladder LP 24 at 700 Hz): a dark tail has small sample steps
//  of its own, so a hard cut on the load cannot hide under them — the click metric has teeth. [F] uses the bright
//  wet patch its references were written with.
static bool gDark = false;
static juce::File wetFile() { return dir.getChildFile (gDark ? "dark.terrain" : "wet.terrain"); }
static juce::File initFile() { return dir.getChildFile ("init.terrain"); }

static void makeWet (TerrainAudioProcessor& p)
{
    setP (p, ParameterIDs::SYN_RVB_ACTIVE, 1.f); setP (p, ParameterIDs::SYN_RVB_POWER, 1.f);
    setP (p, ParameterIDs::SYN_RVB_SRC_A, 1.f);  setP (p, ParameterIDs::SYN_RVB_TYPE, 0.f);    // Hall
    setP (p, ParameterIDs::SYN_RVB_DECAY, 0.85f); setP (p, ParameterIDs::SYN_RVB_MIX, 0.7f);
    setP (p, ParameterIDs::SYN_DLY_ACTIVE, 1.f); setP (p, ParameterIDs::SYN_DLY_POWER, 1.f);
    setP (p, ParameterIDs::SYN_DLY_SRC_A, 1.f);  setP (p, ParameterIDs::SYN_DLY_TYPE, 0.f);    // Digital
    setP (p, ParameterIDs::SYN_DLY_SYNC, 0.f);   setP (p, ParameterIDs::SYN_DLY_TIME, 0.30f);
    setP (p, ParameterIDs::SYN_DLY_FEEDBACK, 0.85f); setP (p, ParameterIDs::SYN_DLY_MIX, 0.5f);
}

static void loadVia (Inst& a, const std::string& path, bool wetTarget)
{
    juce::String err;
    if (path == "file")      { if (! a.p->loadPatchFromFile (wetTarget ? wetFile() : initFile(), err)) { std::printf ("!! load: %s\n", err.toRawUTF8()); std::exit (2); } }
    else if (path == "init") a.p->initPatch();
    else if (path == "host") { auto& c = a.p->virginChunk_; a.p->setStateInformation (c.getData(), (int) c.getSize()); }
    else if (path == "prog") a.p->setCurrentProgram (0);
    else if (path == "dice")
    {
#ifdef TL_HAVE_REQ
        a.p->requestTailFlush();
#endif
    }
}

//  Ring the wet patch on an AUDIO THREAD paced in real time (a host's callback), and load from THIS thread (the
//  message thread / the host's state thread) at 0.8 s, while it renders — the way a real load races processBlock.
//  A note is played again at 2.0 s (the voices and the fade-in must recover). Returns the load's sample index:
//  the first sample the audio thread had not yet rendered when the load was called.
static constexpr double kLoadAt = 0.8, kAgainAt = 2.0, kEnd = 2.6;
static size_t ringThenLoad (Inst& a, const std::string& path, bool wetTarget, double& loadMs)
{
    juce::String err;
    if (! a.p->loadPatchFromFile (wetFile(), err)) { std::printf ("!! wet: %s\n", err.toRawUTF8()); std::exit (2); }
    const int nBlk = (int) (kEnd * SR / BLK), onBlk = (int) (0.05 * SR / BLK), offBlk = onBlk + (int) (0.25 * SR / BLK),
              againBlk = (int) (kAgainAt * SR / BLK);
    std::atomic<int64_t> pos { 0 }; std::atomic<bool> rendering { false };
    std::thread au ([&]
    {
        const auto t0 = std::chrono::steady_clock::now();
        for (int b = 0; b < nBlk; ++b)
        {
            std::this_thread::sleep_until (t0 + std::chrono::microseconds ((int64_t) (1e6 * BLK / SR * b)));
            rendering.store (true, std::memory_order_release);
            if (b == onBlk || b == againBlk) a.block ({ {60,1} });
            else if (b == offBlk || b == againBlk + 8) a.block ({ {60,0} });
            else a.block();
            pos.store ((int64_t) (b + 1) * BLK, std::memory_order_release);
            rendering.store (false, std::memory_order_release);
        }
    });
    while ((double) pos.load (std::memory_order_acquire) < kLoadAt * SR) std::this_thread::sleep_for (std::chrono::microseconds (200));
    while (rendering.load (std::memory_order_acquire)) std::this_thread::yield();   // between two callbacks, as a UI click would be
    const size_t s0 = (size_t) pos.load (std::memory_order_acquire);
    const auto t = std::chrono::steady_clock::now();
    loadVia (a, path, wetTarget);
    loadMs = std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - t).count();
    au.join();
    return s0;
}

static std::vector<float> firstNote (const std::string& path, bool wetTarget)
{
    Inst a;
    a.secs (0.1);
    loadVia (a, path, wetTarget);
    a.secs (0.05);
    a.block ({ {60,1} }); a.secs (0.4); a.block ({ {60,0} }); a.secs (1.0);
    std::vector<float> v; v.reserve (a.L.size() * 2);
    for (size_t i = 0; i < a.L.size(); ++i) { v.push_back (a.L[i]); v.push_back (a.R[i]); }
    return v;
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    setenv ("TERRAIN_DETERMINISTIC", "1", 1);
    const std::string mode = argc > 1 ? argv[1] : "check";
    dir = juce::File (argc > 2 ? juce::String (argv[2]) : juce::File::getSpecialLocation (juce::File::tempDirectory).getFullPathName())
              .getChildFile ("terrain_preset_tail_data");
    dir.createDirectory();
#ifdef TL_HAVE_REQ
    std::printf ("preset_tail_cert — a preset load empties every tail (build: FIXED, requestTailFlush present)\n");
#else
    std::printf ("preset_tail_cert — a preset load empties every tail (build: UNFIXED, no requestTailFlush)\n");
#endif

    {   // the two patch files, written by the shipping save path
        juce::String err;
        Inst w; makeWet (*w.p); w.secs (0.02);
        if (! w.p->savePatchToFile (wetFile(), "{\"name\":\"Wet cert\"}", err)) { std::printf ("!! save wet: %s\n", err.toRawUTF8()); return 2; }
        Inst d; makeWet (*d.p); setP (*d.p, ParameterIDs::SYN_DLY_FEEDBACK, 0.6f); setP (*d.p, ParameterIDs::SYN_DLY_MIX, 0.35f); setP (*d.p, ParameterIDs::SYN_RVB_MIX, 0.5f);
        setP (*d.p, "SYN_OSC_A_F1MIX", 1.f); setP (*d.p, ParameterIDs::SYN_FILTER1_TYPE, 0.f); setP (*d.p, ParameterIDs::SYN_FILTER1_CUT, 700.f);   // the F1 pill sends osc A into Filter 1
        d.secs (0.02); gDark = true;
        if (! d.p->savePatchToFile (wetFile(), "{\"name\":\"Dark cert\"}", err)) { std::printf ("!! save dark: %s\n", err.toRawUTF8()); return 2; }
        Inst i0; i0.secs (0.02);
        if (! i0.p->savePatchToFile (initFile(), "{\"name\":\"Init cert\"}", err)) { std::printf ("!! save init: %s\n", err.toRawUTF8()); return 2; }
    }

    // ═══ [L] a load while the tail rings ═══
    std::printf ("\n[L] Load while the Hall + feedback Delay ring (load at 0.8 s, 0.55 s after the release)\n");
    struct Case { const char* path; bool wet; const char* label; };
    const Case cases[] = {
        { "file", false, "file  → Init (preset browser / drop)" },
        { "file", true,  "file  → the wet patch again (a wet preset: the old tail must not feed the new reverb)" },
        { "init", false, "init  → Init (header Init)" },
        { "host", false, "host  → setStateInformation(Init) (DAW recall / undo / reopen)" },
        { "prog", false, "prog  → setCurrentProgram(0) (host program change)" },
        { "dice", false, "dice  → requestTailFlush (whole-preset dice roll)" },
    };
    const size_t n10 = (size_t) (0.010 * SR), n20 = (size_t) (0.020 * SR);
    const float thr90 = (float) std::pow (10.0, -90.0 / 20.0);
    for (const auto& c : cases)
    {
        Inst a; double loadMs = 0; const size_t s0 = ringThenLoad (a, c.path, c.wet, loadMs);
        const size_t sEnd = (size_t) ((int) (kAgainAt * SR / BLK) * BLK);   // the old tail's window ends where the new note starts
        const std::string tag = std::string ("[") + c.path + (c.wet ? "/wet" : "") + "]";
        float pre = 0, stepSig = 0;
        for (size_t i = s0 - n10; i < s0; ++i) { pre = std::max (pre, a.absAt (i)); stepSig = std::max (stepSig, a.stepAt (i)); }
        float post = 0; for (size_t i = s0 + n20; i < sEnd; ++i) post = std::max (post, a.absAt (i));
        size_t below = s0;   // first sample from which the output stays under −90 dBFS until the new note
        for (size_t i = sEnd; i-- > s0;) if (a.absAt (i) >= thr90) { below = i + 1; break; }
        float stepFade = 0; for (size_t i = s0; i < s0 + n20; ++i) stepFade = std::max (stepFade, a.stepAt (i));   // from s0: the step INTO the first loaded sample counts
        float again = 0; for (size_t i = sEnd; i < a.L.size(); ++i) again = std::max (again, a.absAt (i));
        std::printf ("  ── %s   (the load call took %.1f ms on its thread)\n", c.label, loadMs);
        if (std::getenv ("PTC_ENV"))
        {
            std::printf ("        envelope (5 ms peaks, dBFS) from 10 ms before the load:");
            for (size_t w = s0 - 480; w + 240 < s0 + 9600; w += 240)
            { float m = 0; for (size_t i = w; i < w + 240; ++i) m = std::max (m, a.absAt (i)); std::printf (" %.0f", dB (m)); }
            std::printf ("\n");
        }
        chk (post < thr90 && below - s0 <= n20, "L1 " + tag + " the old tail is under -90 dBFS within 20 ms of the load and stays there",
             fmt ("level before the load %.1f dBFS · under -90 from %.2f ms after it · peak 20 ms .. %.2f s after: %.1f dBFS", dB (pre), (double) (below - s0) * 1000.0 / SR, (double) (sEnd - s0) / SR, dB (post)));
        chk (stepFade <= stepSig * 1.05f + 1e-6f, "L2 " + tag + " no click: the largest step in the 20 ms from the load <= the signal's own",
             fmt ("transition max step %.5f · the tail's own max step in the 10 ms before %.5f · step into the load sample %.5f", stepFade, stepSig, a.stepAt (s0)));
        chk (again > 1e-3f, "L3 " + tag + " a note played after the load sounds (voices + fade-in recovered)", fmt ("peak %.4f", again));
    }

    // ═══ [S] the load ON the audio thread, between two callbacks (a host that restores there) ═══
    //  Nothing can fade BEFORE the state goes in here (the thread that would fade is the one loading), and the load
    //  must not wait on itself. The tail must still go; the step is reported, not judged (it is the state's own).
    std::printf ("\n[S] Load on the audio thread itself, between callbacks (no wait allowed)\n");
    for (const char* path : { "file", "host" })
    {
        Inst a; juce::String err;
        a.p->loadPatchFromFile (wetFile(), err);
        a.secs (0.05); a.block ({ {60,1} }); a.secs (0.25); a.block ({ {60,0} });
        while ((double) a.L.size() < kLoadAt * SR) a.block();
        const size_t s0 = a.L.size();
        const auto t = std::chrono::steady_clock::now();
        loadVia (a, path, false);
        const double ms = std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - t).count();
        a.secs (1.0);
        float post = 0; for (size_t i = s0 + n20; i < a.L.size(); ++i) post = std::max (post, a.absAt (i));
        float stepSig = 0; for (size_t i = s0 - n10; i < s0; ++i) stepSig = std::max (stepSig, a.stepAt (i));
        float stepT = 0; for (size_t i = s0; i < s0 + n20; ++i) stepT = std::max (stepT, a.stepAt (i));
        chk (post < thr90 && ms < 45.0, std::string ("S1 [") + path + "→Init] same-thread load: no stall, the tail under -90 dBFS from 20 ms",
             fmt ("load call %.1f ms · peak 20 ms..1 s after: %.1f dBFS · (info) transition max step %.5f vs the signal's own %.5f", ms, dB (post), stepT, stepSig));
    }

    // ═══ [F] from silence the new patch sounds exactly as before ═══
    gDark = false;
    std::printf ("\n[F] Loaded from silence, the first note is unchanged (mode: %s)\n", mode.c_str());
    const Case fc[] = { { "file", false, "Init via file" }, { "init", false, "Init via initPatch" }, { "host", false, "Init via setStateInformation" },
                        { "file", true, "the wet patch via file" }, { "prog", false, "program 0 via setCurrentProgram" } };
    for (const auto& c : fc)
    {
        const auto v = firstNote (c.path, c.wet);
        const auto f = dir.getChildFile (juce::String ("ref_") + c.path + (c.wet ? "_wet" : "") + ".f32");
        float pk = 0; for (float x : v) pk = std::max (pk, std::abs (x));
        if (mode == "write-ref")
        {
            f.replaceWithData (v.data(), v.size() * sizeof (float));
            std::printf ("  wrote %s (%zu samples, peak %.4f)\n", f.getFileName().toRawUTF8(), v.size() / 2, pk);
            continue;
        }
        juce::MemoryBlock mb;
        if (! f.loadFileAsData (mb)) { chk (false, std::string ("F1 [") + c.label + "] no reference — run write-ref on the unfixed build first"); continue; }
        const float* r = (const float*) mb.getData(); const size_t nr = mb.getSize() / sizeof (float);
        size_t firstDiff = v.size(); double maxd = 0;
        for (size_t i = 0; i < std::min (nr, v.size()); ++i)
        { if (std::memcmp (&r[i], &v[i], 4) != 0 && firstDiff == v.size()) firstDiff = i; maxd = std::max (maxd, (double) std::abs (r[i] - v[i])); }
        chk (nr == v.size() && firstDiff == v.size() && pk > 1e-3f, std::string ("F1 [") + c.label + "] bit-identical to the unfixed build's first note",
             fmt ("%.0f samples, peak %.4f · first differing sample %.0f · max |diff| %.1f dBFS", (double) v.size() / 2, pk, firstDiff == v.size() ? -1.0 : (double) (firstDiff / 2), dB (maxd)));
    }

    std::printf ("\npreset_tail_cert: %d pass, %d fail — %s\n", npass, nfail, nfail ? "FAIL" : "PASS");
    return nfail ? 1 : 0;
}
