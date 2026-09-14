// ══════════════════════════════════════════════════════════════════════════════════════════════
//  fb641 — THREE SAMPLE-ENGINE FIXES, MEASURED on the real processor.
//
//    Tests/sample_fixes_fb641.sh      (build Terrain first; macOS; links libTerrain_SharedCode.a)
//
//  Max: "whenever I have scan all the way to the left or negative scan the spray isn't working — the spray needs to start
//  spraying backwards if it's scanning backwards; copying a sample and pasting it over to the next oscillator changes the
//  pitching key, that's a big no-no; and I want the one shot to glide with the glide/portamento as well, just like the synth."
//    [A] SampleEngine: under reverse Scan the sprayed start is the exact MIRROR of the forward one (same seed), full Spray
//        reaches across the region, and Spray 0 still starts at the very end (unchanged).
//    [B] copyOscSampleSlot: a 44.1 kHz sample pasted onto an osc that held a 48 kHz one keeps ITS rate and plays at the
//        source's pitch (zero-crossing pitch of the rendered note) — with a control that rebuilds the old wrong rate and
//        must measure sharp, so the pitch reader is proven able to see the bug.
//    [C] a Sample-engine ONE-SHOT glides: mono legato 60 → 72 with portamento passes THROUGH the pitches in between;
//        the same run with portamento 0 jumps (the control).
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
#define private public
#define protected public
#include "PluginProcessor.h"
#undef private
#undef protected

static int fails = 0;
static void bar (const char* tag, bool ok, const juce::String& text)
{
    printf ("  %s [%s] %s\n", ok ? "✓" : "✗", tag, text.toRawUTF8()); if (! ok) ++fails;
}

static constexpr double kOut = 48000.0, kNative = 44100.0, kF60 = 261.6255653;   // note 60 plays the sample at its own pitch

static std::shared_ptr<juce::AudioBuffer<float>> sine (double hz, double rate, double seconds)
{
    const int n = (int) (rate * seconds);
    auto b = std::make_shared<juce::AudioBuffer<float>> (1, n);
    for (int i = 0; i < n; ++i) b->setSample (0, i, 0.5f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * hz * i / rate));
    return b;
}

// pitch per window from positive-going zero crossings (sub-sample interpolated); 0 where too few cycles
static std::vector<double> pitchTrack (const juce::AudioBuffer<float>& b, int from, int to, int win)
{
    const float* x = b.getReadPointer (0);
    std::vector<double> out;
    for (int w0 = from; w0 + win <= to; w0 += win)
    {
        double first = -1, last = -1; int cnt = 0;
        for (int i = w0 + 1; i < w0 + win; ++i)
            if (x[i - 1] < 0.f && x[i] >= 0.f)
            {
                const double t = (i - 1) + (double) (-x[i - 1]) / (double) (x[i] - x[i - 1]);
                if (first < 0) first = t;
                last = t; ++cnt;
            }
        out.push_back (cnt >= 3 && last > first ? kOut * (cnt - 1) / (last - first) : 0.0);
    }
    return out;
}
static double median (std::vector<double> v)
{
    v.erase (std::remove (v.begin(), v.end(), 0.0), v.end());
    if (v.empty()) return 0.0;
    std::sort (v.begin(), v.end()); return v[v.size() / 2];
}

static void setP (TerrainAudioProcessor& p, const char* id, float v)
{
    auto* q = p.getAPVTS().getParameter (id);
    if (q == nullptr) { printf ("  ✗ no parameter %s\n", id); ++fails; return; }
    q->setValueNotifyingHost (q->convertTo0to1 (v));
}

struct Ev { int at, note; bool on; };
static juce::AudioBuffer<float> render (TerrainAudioProcessor& p, const std::vector<Ev>& evs, int total)
{
    juce::AudioBuffer<float> out (2, total); out.clear();
    const int bs = 512;
    juce::AudioBuffer<float> blk (2, bs);
    for (int s0 = 0; s0 < total; s0 += bs)
    {
        const int n = std::min (bs, total - s0);
        blk.setSize (2, n, false, false, true); blk.clear();
        juce::MidiBuffer mb;
        for (auto& e : evs)
            if (e.at >= s0 && e.at < s0 + n)
                mb.addEvent (e.on ? juce::MidiMessage::noteOn (1, e.note, (juce::uint8) 100) : juce::MidiMessage::noteOff (1, e.note), e.at - s0);
        p.processBlock (blk, mb);
        for (int c = 0; c < 2; ++c) out.copyFrom (c, s0, blk, c, 0, n);
    }
    return out;
}

static void loadSlot (TerrainAudioProcessor& p, int slot, std::shared_ptr<juce::AudioBuffer<float>> b, double rate, const juce::String& path)
{
    p.oscSampleBuffers_[(size_t) slot].setSampleRate (rate);
    p.oscSampleBuffers_[(size_t) slot].store (std::move (b));
    p.oscSourcePaths_[(size_t) slot] = path;
    p.oscLoadedPath_[(size_t) slot]  = path;
}

static std::unique_ptr<TerrainAudioProcessor> freshProc()
{
    auto p = std::make_unique<TerrainAudioProcessor>();
    p->prepareToPlay (kOut, 512);
    return p;
}

int main()
{
    juce::ScopedJuceInitialiser_GUI init;
    printf ("══ fb641 THREE SAMPLE-ENGINE FIXES ══\n");

    // ── [A] reverse Scan sprays backward from the end ─────────────────────────────────────────
    {
        const int n = 48000;
        std::vector<float> ramp ((size_t) n);
        for (int i = 0; i < n; ++i) ramp[(size_t) i] = (float) i / n;
        const float* ch[1] = { ramp.data() };
        double regEnd = 0;
        auto startAt = [&] (float scan, float spray, uint32_t seed)
        {
            tw::SampleEngine e; e.prepare (kOut); e.setSample (ch, 1, n, kOut);
            e.setRegionParams (0.f, 1.f, 0.f, 1.f, 0.12f, 0.f, 0.f);
            e.setLoopMode (tw::SampleEngine::LoopMode::OneShot);
            e.setScan (scan);
            e.noteOn (1.0, spray, seed);
            regEnd = e.regEnd_;
            return e.pos_;
        };
        const double end0 = startAt (-1.f, 0.f, 7u), fwd0 = startAt (1.f, 0.f, 7u);
        double worstMirror = 0, lo = 1e18, hi = -1e18;
        for (uint32_t s = 1; s <= 400; ++s)
        {
            const double f = startAt (1.f, 1.f, s), r = startAt (-1.f, 1.f, s);
            worstMirror = std::max (worstMirror, std::abs ((f + r) - (fwd0 + end0)));
            lo = std::min (lo, r); hi = std::max (hi, r);
        }
        bar ("A", end0 == regEnd - 1.0,
             juce::String::formatted ("Spray 0 under reverse Scan still starts at the very end, exactly as before (sample %.0f = region end - 1)", end0));
        bar ("A", worstMirror < 1e-6 && lo < 0.1 * n && hi > 0.9 * n,
             juce::String::formatted ("full Spray under reverse Scan spreads the start across the region (%.0f%%..%.0f%% of it, 400 notes) "
                                      "and every start MIRRORS the forward one for the same seed (worst error %.2g samples)",
                                      100.0 * lo / n, 100.0 * hi / n, worstMirror));
    }

    // ── [B] a pasted sample keeps its pitch ───────────────────────────────────────────────────
    {
        // the paste goes B -> A: osc A is the target (it is the osc [C] proves renders in this harness)
        auto p = freshProc();
        loadSlot (*p, 1, sine (kF60, kNative, 3.0), kNative, "/fb641/source-44k1.wav");
        loadSlot (*p, 0, sine (1000.0, 48000.0, 3.0), 48000.0, "/fb641/old-48k.wav");      // what the target held before
        const bool ok = p->copyOscSampleSlot (1, 0);
        const bool state = ok && p->oscSampleBuffers_[0].getSampleRate() == kNative
                           && p->oscLoadedPath_[0] == p->oscLoadedPath_[1] && p->oscSourcePaths_[0] == p->oscSourcePaths_[1]
                           && p->oscSampleBuffers_[0].load()->getNumSamples() == p->oscSampleBuffers_[1].load()->getNumSamples();
        bar ("B", state, juce::String::formatted ("the paste carries the native rate (%.0f Hz), the source path and the loaded-path record",
                                                  p->oscSampleBuffers_[0].getSampleRate()));
        auto asSampleOsc = [] (TerrainAudioProcessor& t)
        {
            setP (t, ParameterIDs::SYN_OSC_A_ENGINE, 1.f);                 // SAMP
            setP (t, ParameterIDs::SYN_OSC_A_LEVEL, 0.7f);
            setP (t, ParameterIDs::SYN_OSC_B_LEVEL, 0.f);
            setP (t, ParameterIDs::SYN_OSC_A_SAMPLE_SCAN, 0.5f);           // natural forward
            setP (t, ParameterIDs::SYN_OSC_A_SAMPLE_LOOP_MODE, 1.f);       // Forward loop: it sustains
        };
        asSampleOsc (*p);
        const int N = (int) (kOut * 0.6);
        auto out = render (*p, { { 0, 60, true } }, N);
        const double fPaste = median (pitchTrack (out, (int) (kOut * 0.08), N, 960));

        // CONTROL — rebuild the pre-fb641 state (the target keeps its OLD 48 kHz rate) on a fresh processor
        auto q = freshProc();
        loadSlot (*q, 0, sine (kF60, kNative, 3.0), 48000.0, "/fb641/source-44k1.wav");
        asSampleOsc (*q);
        auto outC = render (*q, { { 0, 60, true } }, N);
        const double fOld = median (pitchTrack (outC, (int) (kOut * 0.08), N, 960));
        const double centsPaste = 1200.0 * std::log2 (fPaste / kF60), centsOld = 1200.0 * std::log2 (fOld / kF60);
        bar ("B", std::abs (centsPaste) < 3.0 && centsOld > 100.0,
             juce::String::formatted ("pasted onto an osc that held a 48 kHz sample, note 60 plays %.2f Hz (%+.1f cents from the source); "
                                      "the old behaviour, rebuilt as a control, measures %.2f Hz (%+.0f cents)", fPaste, centsPaste, fOld, centsOld));
        p->releaseResources(); q->releaseResources();
    }

    // ── [C] a one-shot glides ─────────────────────────────────────────────────────────────────
    {
        auto run = [&] (float porta, std::vector<double>& track)
        {
            auto p = freshProc();
            loadSlot (*p, 0, sine (kF60, kNative, 8.0), kNative, "/fb641/glide.wav");
            setP (*p, ParameterIDs::SYN_OSC_A_ENGINE, 1.f);              // SAMP
            setP (*p, ParameterIDs::SYN_OSC_A_LEVEL, 0.7f);
            setP (*p, ParameterIDs::SYN_OSC_B_LEVEL, 0.f);
            setP (*p, ParameterIDs::SYN_OSC_A_SAMPLE_SCAN, 0.5f);
            setP (*p, ParameterIDs::SYN_OSC_A_SAMPLE_LOOP_MODE, 0.f);    // ONE-SHOT
            setP (*p, ParameterIDs::SYN_MONO, 1.f); setP (*p, ParameterIDs::SYN_LEGATO, 1.f);
            setP (*p, ParameterIDs::SYN_GLIDE_ALWAYS, 1.f); setP (*p, ParameterIDs::SYN_GLIDE_SCALED, 0.f);
            setP (*p, ParameterIDs::SYN_GLIDE_CURVE, 50.f);
            setP (*p, ParameterIDs::SYN_PORTA, porta);
            const int N = (int) (kOut * 1.6), at = (int) (kOut * 0.3);
            auto out = render (*p, { { 0, 60, true }, { at, 72, true } }, N);
            track = pitchTrack (out, at, N, 960);                        // 20 ms windows from the second note on
            p->releaseResources();
        };
        std::vector<double> glide, jump;
        run (40.f, glide); run (0.f, jump);
        const double f72 = kF60 * 2.0;
        auto between = [&] (const std::vector<double>& t) { int c = 0; for (double f : t) if (f > kF60 * 1.06 && f < f72 * 0.94) ++c; return c; };
        const int gB = between (glide), jB = between (jump);
        const double gEnd = glide.empty() ? 0 : glide.back();
        bool mono = true; double prev = 0;
        for (double f : glide) { if (f <= 0) continue; if (prev > 0 && f < prev * 0.985) mono = false; prev = f; }
        juce::String path; for (size_t i = 0; i < glide.size() && i < 40; i += 4) path << juce::String (glide[i], 0) << " ";
        bar ("C", gB >= 3 && mono && std::abs (1200.0 * std::log2 (gEnd / f72)) < 5.0 && jB <= 1,
             juce::String::formatted ("mono legato 60 -> 72 on a one-shot: %d windows (20 ms) pass between the two pitches, it only rises, "
                                      "and it lands on %.1f Hz; with portamento 0 (control) %d window(s) do", gB, gEnd, jB)
             + "\n        glide Hz every 80 ms: " + path);
    }

    printf ("sample_fixes_fb641: %s\n", fails ? "FAIL" : "PASS");
    return fails ? 1 : 0;
}
