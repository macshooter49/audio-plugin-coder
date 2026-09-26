// ══════════════════════════════════════════════════════════════════════════════════════════════
//  tp114 — A ONE-SHOT PICKED FROM THE SAMPLE BROWSER RINGS THROUGH MODAL, measured on the real processor.
//
//    Tests/modal_samples_cert.sh          (build Terrain first; links libTerrain_SharedCode.a)
//
//  Max: "For the Modal engine add a tree entry that says Samples, and the browser is the sample browser …
//  I want people to drag and drop samples into the Modal engine." The page half (the family menu's Samples
//  door → openSampleBrowser → loadSampleByPath(osc, path)) is Tests/modal_samples_gate.js. This is the
//  audio half: what that native does to the processor, and what comes out.
//
//  THE PATH UNDER TEST is loadSampleByPath's body (PluginEditor.cpp), replayed on a bare processor:
//      read the file → oscSourcePath(o) = its absolute path → getOscSampleLoader(o).loadFromMemory(bytes,
//      name, getOscSampleBuffer(o), …)            — exactly what loadOscSampleFromMemory does.
//  Bar [0] reads those lines OFF DISK so a change to the native cannot leave this cert testing a copy.
//
//  Every comparison is a 1/3-octave magnitude spectrum (phase-independent — the fb283 law; sample-
//  difference RMS is banned as a measure), 50 Hz..16 kHz, first 1.36 s of a held C3 on a Wire (string) body.
//    [0] the native's body is still the path replayed here
//    [1] A, no sample: Modal plays its own synth strike (audible) — the reference
//    [2] A, a factory one-shot loaded the browser's way: audible, and its spectrum moves ≥ 15 dB (mean |Δ| per band)
//        and ≥ 3× the note-to-note spread of the empty osc (its synth strike is a re-seeded noise burst: ~4.6 dB)
//    [3] IT WENT THROUGH THE RESONATOR: the same buffer played raw by the Sample engine is ≥ 3 dB from the Modal
//        render — Modal is ringing the one-shot as its strike, not passing it through
//    [4] E (bank B): the same load on osc E moves E's Modal the same way (≥ 15 dB from E without it)
//    [5] PERSISTS: state → a NEW instance → load → A and E each within 1 dB of their loaded render and
//        ≥ 15 dB from their empty one (the sample came back, not just the path)
//
//  MUTATION CONTROL: TP114_MUTATE=1 files the path but never loads the audio → [2] [3] [4] [5] red ([5]: the
//  restore FILLS the slot from the filed path, so the new instance no longer matches the unloaded render).
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

static int npass = 0, nfail = 0;
static void chk (bool ok, const std::string& what, const std::string& d = "")
{ std::printf ("  %s  %s\n", ok ? "PASS " : "FAIL ", what.c_str()); if (! d.empty()) std::printf ("        %s\n", d.c_str()); ok ? ++npass : ++nfail; std::fflush (stdout); }

static constexpr double SR = 48000.0; static constexpr int BLK = 512; static constexpr int kNote = 48;
static void setP (TerrainAudioProcessor& p, const char* id, float plain)
{
    auto* prm = p.apvts.getParameter (id);
    if (prm == nullptr) { std::printf ("!! no param %s\n", id); std::exit (2); }
    prm->setValueNotifyingHost (prm->convertTo0to1 (plain));
}
static std::string osc (const char* pat, int o) { std::string s (pat); s[8] = (char) ('A' + o); return s; }   // "SYN_OSC_?_…"

struct Inst
{
    std::unique_ptr<TerrainAudioProcessor> p; juce::AudioBuffer<float> buf { 2, BLK }; std::vector<float> M;
    Inst() { p = std::make_unique<TerrainAudioProcessor>(); p->setPlayConfigDetails (0, 2, SR, BLK); p->prepareToPlay (SR, BLK); }
    void block (int note = -1, bool on = true)
    {
        buf.clear(); juce::MidiBuffer m;
        if (note >= 0) m.addEvent (on ? juce::MidiMessage::noteOn (1, note, (juce::uint8) 100) : juce::MidiMessage::noteOff (1, note), 0);
        p->processBlock (buf, m);
        for (int i = 0; i < BLK; ++i) M.push_back (0.5f * (buf.getSample (0, i) + buf.getSample (1, i)));
    }
    void tick (int n = 1) { for (int i = 0; i < n; ++i) { CFRunLoopRunInMode (kCFRunLoopDefaultMode, 0.002, false); p->timerCallback(); } }
    void run (double sec) { const int n = (int) std::ceil (sec * SR / BLK); for (int b = 0; b < n; ++b) { block(); if (b & 1) tick(); } }
    /** one held note on a silent start: returns the mono render */
    std::vector<float> note (double sec = 1.5)
    {
        settle(); M.clear(); block (kNote, true); run (sec);
        auto out = M; block (kNote, false); settle(); return out;
    }
    /** run until the output is SILENT (a 100 ms window under −100 dBFS, 20 s cap). A one-shot strike keeps
        reading after note-off, so a fixed gap let a long sample's tail play under the NEXT render. */
    void settle()
    {
        for (int w = 0; w < 200; ++w)
        {
            const size_t a0 = M.size(); run (0.1); float pk = 0;
            for (size_t i = a0; i < M.size(); ++i) pk = std::max (pk, std::abs (M[i]));
            if (pk < 1.0e-5f) return;
        }
    }
};

/** osc o alone, on Modal, Wire body, Source = src */
static void rig (Inst& a, int o, int src)
{
    for (int k = 0; k < 8; ++k) setP (*a.p, ParameterIDs::kOsc_ENABLE[k], k == o ? 1.f : 0.f);
    setP (*a.p, ParameterIDs::kOsc_LEVEL[o], 0.8f);
    setP (*a.p, ParameterIDs::kOsc_ENGINE[o], 6.f);                                  // MODAL
    setP (*a.p, osc ("SYN_OSC_?_MODAL_FAMILY", o).c_str(), 1.f);                     // Wire (the string core)
    setP (*a.p, osc ("SYN_OSC_?_MODAL_SOURCE", o).c_str(), (float) src);             // 0 Auto · 1 Noise · 2 Click · 3 Sample
    a.tick (6); a.run (0.05);
    (void) a.note();   // a throwaway first note: every render compared below is a SETTLED note, never an instance's first
}

/** loadSampleByPath's body, replayed (see [0]) */
static bool loadByPath (Inst& a, int o, const juce::File& f)
{
    juce::MemoryBlock mb; if (! f.loadFileAsData (mb) || mb.getSize() == 0) return false;
    a.p->oscSourcePath (o) = f.getFullPathName();
    if (juce::SystemStats::getEnvironmentVariable ("TP114_MUTATE", "") == "1") return true;   // mutation: the path is filed, the audio never reaches the slot
    std::atomic<int> done { 0 };
    a.p->getOscSampleLoader (o).loadFromMemory (std::move (mb), f.getFileName(), a.p->getOscSampleBuffer (o),
        [] (float) {}, [&done] (tw::SampleLoader::Result r) { done = r.success ? 1 : -1; });
    const double t0 = juce::Time::getMillisecondCounterHiRes();
    while (done == 0 && juce::Time::getMillisecondCounterHiRes() - t0 < 8000.0) { a.tick(); a.block(); }
    a.run (0.05);
    return done == 1 && a.p->getOscSampleBuffer (o).getNumSamples() > 0;
}

static std::vector<double> bands (const std::vector<float>& x)
{
    constexpr int ord = 16, N = 1 << ord; juce::dsp::FFT fft (ord);
    std::vector<float> w ((size_t) 2 * N, 0.f);
    for (int i = 0; i < N && i < (int) x.size(); ++i) w[(size_t) i] = x[(size_t) i] * (float) (0.5 - 0.5 * std::cos (2 * juce::MathConstants<double>::pi * i / (N - 1)));
    fft.performFrequencyOnlyForwardTransform (w.data());
    std::vector<double> out;
    for (double lo = 50.0; lo < 16000.0; lo *= std::pow (2.0, 1.0 / 3.0))
    {
        const double hi = lo * std::pow (2.0, 1.0 / 3.0); double e = 0;
        for (int k = (int) (lo * N / SR); k < (int) (hi * N / SR) && k < N / 2; ++k) e += (double) w[(size_t) k] * w[(size_t) k];
        out.push_back (10.0 * std::log10 (e + 1e-12));
    }
    return out;
}
static double dist (const std::vector<float>& a, const std::vector<float>& b)
{ auto A = bands (a), B = bands (b); double s = 0; for (size_t i = 0; i < A.size(); ++i) s += std::abs (A[i] - B[i]); return s / (double) A.size(); }
static double peakDb (const std::vector<float>& x) { float m = 0; for (float v : x) m = std::max (m, std::abs (v)); return 20.0 * std::log10 (std::max (1e-10f, m)); }
static std::string f2 (double v) { char b[32]; std::snprintf (b, sizeof b, "%.2f", v); return b; }

int main()
{
    juce::ScopedJuceInitialiser_GUI gui;
    std::printf ("modal_samples_cert — tp114 a browser-picked one-shot rings through Modal (A and E)\n\n");

    // [0] the native's body, read off disk
    {
        const auto ed = juce::File (__FILE__).getParentDirectory().getParentDirectory().getChildFile ("Source/PluginEditor.cpp").loadFileAsString();
        const int a = ed.indexOf ("withNativeFunction(\"loadSampleByPath\"");
        const auto body = a >= 0 ? ed.substring (a, a + 1600) : juce::String();
        const int b = ed.indexOf ("void TerrainUiCore::loadOscSampleFromMemory");
        const auto lom = b >= 0 ? ed.substring (b, b + 2400) : juce::String();
        const bool ok = body.contains ("loadOscSampleFromMemory (oscIdx, std::move (mb), f.getFileName(), f.getFullPathName())")
                     && lom.contains ("audioProcessor.oscSourcePath (oscIdx) = sourcePath.isNotEmpty() ? sourcePath")
                     && lom.contains ("getOscSampleLoader (oscIdx)") && lom.contains ("loader.loadFromMemory (")
                     && lom.contains ("getOscSampleBuffer (oscIdx)");
        chk (ok, "0 loadSampleByPath → loadOscSampleFromMemory is still {path → oscSourcePath, loader.loadFromMemory → the osc buffer}",
             std::string ("native ") + (a >= 0 ? "found" : "MISSING") + " · loadOscSampleFromMemory " + (b >= 0 ? "found" : "MISSING"));
    }

    // a factory one-shot, from the built bundle or the installed one
    juce::File smp (juce::SystemStats::getEnvironmentVariable ("TP114_SAMPLE", ""));
    if (! smp.existsAsFile())
        for (auto root : { juce::File (__FILE__).getParentDirectory().getParentDirectory().getParentDirectory().getParentDirectory()
                               .getChildFile ("build/plugins/Terrain/Terrain_artefacts/Release/VST3/Terrain.vst3/Contents/Resources/Samples"),
                           juce::File::getSpecialLocation (juce::File::userHomeDirectory).getChildFile ("Library/Audio/Plug-Ins/VST3/Terrain.vst3/Contents/Resources/Samples") })
            if (root.getChildFile ("Bell/Bell 01.flac").existsAsFile()) { smp = root.getChildFile ("Bell/Bell 01.flac"); break; }
    if (! smp.existsAsFile()) { std::printf ("!! no factory one-shot found (set TP114_SAMPLE=<file>)\n"); return 2; }
    std::printf ("  one-shot: %s\n\n", smp.getFullPathName().toRawUTF8());

    std::vector<float> empty[2], loaded[2];
    const int OSCS[2] = { 0, 4 };
    for (int j = 0; j < 2; ++j)
    {
        const int o = OSCS[j]; const char L = (char) ('A' + o);
        Inst a; rig (a, o, 0);
        empty[j] = a.note();
        const double spread = dist (a.note(), empty[j]);   // the synth strike is a NOISE burst, re-seeded every note: two empties differ by this much
        const bool ok = loadByPath (a, o, smp);
        loaded[j] = a.note();
        const double d = dist (loaded[j], empty[j]);
        if (j == 0)
        {
            chk (peakDb (empty[j]) > -40.0, "1 A on Modal with no sample plays its own strike", "peak " + f2 (peakDb (empty[j])) + " dBFS");
            chk (ok && peakDb (loaded[j]) > -40.0 && d >= 15.0 && d >= 3.0 * spread, "2 A: the browser-loaded one-shot sounds through Modal and moves its spectrum ≥ 15 dB (and ≥ 3× the note-to-note spread)",
                 std::string (ok ? "loaded" : "LOAD FAILED") + " · peak " + f2 (peakDb (loaded[j])) + " dBFS · mean |Δ| " + f2 (d) + " dB per 1/3-oct band · empty-to-empty spread " + f2 (spread) + " dB");
            // the SAME buffer on the Sample engine is the one-shot played back raw; Modal must not be that
            setP (*a.p, "SYN_OSC_A_ENGINE", 1.f); a.tick (4); a.run (0.05);
            const auto raw = a.note();
            setP (*a.p, "SYN_OSC_A_ENGINE", 6.f); a.tick (4); a.run (0.05);   // back on Modal before [5] saves the state
            const double dr = dist (raw, loaded[j]);
            chk (dr >= 3.0 && peakDb (raw) > -40.0, "3 it went THROUGH the resonator: the same buffer on the Sample engine (raw playback) is ≥ 3 dB from the Modal render",
                 "raw one-shot to Modal " + f2 (dr) + " dB");
        }
        else
            chk (ok && peakDb (loaded[j]) > -40.0 && d >= 15.0 && d >= 3.0 * spread, std::string ("4 ") + L + " (bank B): the same load moves E's Modal ≥ 15 dB (and ≥ 3× the spread)",
                 std::string (ok ? "loaded" : "LOAD FAILED") + " · empty peak " + f2 (peakDb (empty[j])) + " · loaded peak " + f2 (peakDb (loaded[j])) + " dBFS · mean |Δ| " + f2 (d) + " dB · spread " + f2 (spread) + " dB");

        // [5] persistence — state → a new instance
        juce::MemoryBlock blob; a.p->getStateInformation (blob);
        Inst b; b.p->setStateInformation (blob.getData(), (int) blob.getSize()); b.tick (8); b.run (0.1);
        const auto back = b.note();
        const double dl = dist (back, loaded[j]), de = dist (back, empty[j]);
        chk (dl <= 1.0 && de >= 15.0, std::string ("5 ") + L + ": the sample persists — a new instance from the saved state plays the loaded sound",
             "to loaded " + f2 (dl) + " dB · to empty " + f2 (de) + " dB · slot " + juce::String (b.p->getOscSampleBuffer (o).getNumSamples()).toStdString() + " samples");
    }
    std::printf ("\n  %d passed, %d failed\n", npass, nfail);
    return nfail ? 1 : 0;
}
