// ══════════════════════════════════════════════════════════════════════════════════════════════
//  chop_baseline_cert.cpp — tp101 · THE CHOPS SOUND LIKE THE PITCH-MODE SETUP, AND THE ENGINE
//  HOLDS 64 OF THEM WITHOUT ALLOCATING.
//
//  Max: "when I switch to slice mode it has to keep EVERYTHING I set in pitch mode — fine tune,
//  tuning, attack, decay, sustain, release, volume — so every chop of the same one-shot sounds
//  identical to the pitch-mode setup until I edit a chop."
//
//  Drives the SHIPPED tw::LayerState / tw::TerrainSynth / tw::SamplerVoice (header-only) through
//  juce::Synthesiser::renderNextBlock with real MIDI. Nothing here models the engine.
//
//  BARS
//   [0] the voice pool is PREALLOCATED at 64 per layer
//   [1] LAYER mode on a 64-chop grid sounds all 64 chops on ONE key
//   [2] 🚨 a one-chop grid played in CHOP mode at the root is BIT-IDENTICAL to pitch mode, with the
//       pitch-mode panel set to a fine-tuned transpose + a full ADSR + volume (note-on AND release)
//   [3] ...and in CHROMATIC mode, a key above the root, the same
//   [4] 🚨 AUDITION (clicking a chop) plays the pitch-mode tuning + fine tune, not unity
//   [5] an EDITED chop (concrete override) is NOT the baseline — the override still wins
//   [6] the pitch-mode PLAYHEAD feed: advances at the read rate, runs backwards when reversed,
//       reads -1 once the voice has finished
//   [7] 🚨 NO AUDIO-THREAD ALLOCATION: setSliceContext + renderNextBlock, block after block,
//       with 64 chops and notes starting, allocate nothing
//
//    bash Tests/chop_baseline_cert.sh      (run from plugins/Terrain)
// ══════════════════════════════════════════════════════════════════════════════════════════════
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <new>
#include <atomic>
#include <string>

// ── allocation counter (bar [7]) ────────────────────────────────────────────────────────────────
static std::atomic<long> gAllocs { 0 };
static std::atomic<bool> gCount  { false };
void* operator new (std::size_t n)                { if (gCount.load()) ++gAllocs; if (void* p = std::malloc (n ? n : 1)) return p; throw std::bad_alloc(); }
void* operator new[] (std::size_t n)              { if (gCount.load()) ++gAllocs; if (void* p = std::malloc (n ? n : 1)) return p; throw std::bad_alloc(); }
void  operator delete (void* p) noexcept          { std::free (p); }
void  operator delete[] (void* p) noexcept        { std::free (p); }
void  operator delete (void* p, std::size_t) noexcept   { std::free (p); }
void  operator delete[] (void* p, std::size_t) noexcept { std::free (p); }

#include "LayerState.h"

static int gPass = 0, gFail = 0;
static void ok (bool c, const std::string& label, const std::string& info = "")
{
    std::printf ("  %s  %s%s%s\n", c ? "PASS" : "FAIL", label.c_str(), info.empty() ? "" : "\n        ", info.c_str());
    (c ? gPass : gFail)++;
}

static constexpr double SR  = 48000.0;
static constexpr int    BLK = 512;
static constexpr int    ROOT = 60;

// A 1.5 s harmonic tone with a decaying partial — anything a pitch or envelope change would move.
static tw::SampleBuffer::BufferPtr makeSample()
{
    const int N = (int) (1.5 * SR);
    auto b = std::make_shared<juce::AudioBuffer<float>> (2, N);
    for (int i = 0; i < N; ++i)
    {
        const double t = i / SR;
        const float v = (float) (0.5 * std::sin (2 * M_PI * 220.0 * t) + 0.25 * std::sin (2 * M_PI * 660.0 * t) * std::exp (-2.0 * t));
        b->setSample (0, i, v); b->setSample (1, i, v * 0.9f);
    }
    return b;
}

struct Rig
{
    tw::LayerState L;
    tw::SampleBuffer::BufferPtr buf;
    Rig()
    {
        buf = makeSample();
        L.sampleBuffer.store (buf);
        L.sampleBuffer.setSampleRate (SR);
        L.synth.setCurrentPlaybackSampleRate (SR);
        L.attackMsAtomic.store (5.0f); L.releaseMsAtomic.store (800.0f);
        L.sampleLoopMode.store (0);
        L.pitchModeSlice.startSample = 0; L.pitchModeSlice.endSample = buf->getNumSamples();
    }
    tw::SliceContext ctx (tw::SliceContext::Mode m)
    {
        tw::SliceContext c;
        c.mode = m; c.rootMidiNote = ROOT; c.activeSliceIndex = 0;
        c.slices = std::atomic_load (&L.currentSlices);
        c.pitchModeSlice = L.pitchModeSlice;
        return c;
    }
    // note on at block 0, note off at block `offBlk`, `nBlk` blocks total; returns the left channel
    std::vector<float> play (tw::SliceContext::Mode m, int note, int offBlk, int nBlk)
    {
        std::vector<float> out;
        juce::AudioBuffer<float> o (2, BLK);
        for (int k = 0; k < nBlk; ++k)
        {
            juce::MidiBuffer mb;
            if (k == 0)      mb.addEvent (juce::MidiMessage::noteOn  (1, note, (juce::uint8) 127), 0);
            if (k == offBlk) mb.addEvent (juce::MidiMessage::noteOff (1, note), 0);
            L.synth.setSliceContext (ctx (m));
            o.clear();
            L.synth.renderNextBlock (o, mb, 0, BLK);
            for (int i = 0; i < BLK; ++i) out.push_back (o.getSample (0, i));
        }
        L.synth.allNotesOff (0, false);
        return out;
    }
};

static double maxDiff (const std::vector<float>& a, const std::vector<float>& b)
{
    double m = 0; for (size_t i = 0; i < a.size() && i < b.size(); ++i) m = std::max (m, (double) std::abs (a[i] - b[i]));
    return m;
}
static double rms (const std::vector<float>& a) { double s = 0; for (float v : a) s += (double) v * v; return std::sqrt (s / (double) juce::jmax ((size_t) 1, a.size())); }

int main()
{
    std::printf ("chop_baseline_cert — tp101\n");

    // ── [0] ──
    {
        Rig r;
        ok (r.L.synth.getNumVoices() == 64 && tw::kSamplerVoicesPerLayer == 64 && tw::kMaxChops == 64,
            "[0] the voice pool is PREALLOCATED at 64 per layer", "voices=" + std::to_string (r.L.synth.getNumVoices()));
    }

    // ── [1] LAYER mode, 64 chops, one key ──
    {
        Rig r;
        auto grid = tw::makeGridSlices (*r.buf, 64, true);
        std::atomic_store (&r.L.currentSlices, std::make_shared<const tw::SliceList> (grid));
        juce::AudioBuffer<float> o (2, BLK); juce::MidiBuffer mb;
        mb.addEvent (juce::MidiMessage::noteOn (1, ROOT, (juce::uint8) 127), 0);
        r.L.synth.setSliceContext (r.ctx (tw::SliceContext::Mode::Layer));
        r.L.synth.renderNextBlock (o, mb, 0, BLK);
        int sounding = 0; bool distinct[64] = {};
        for (int v = 0; v < r.L.synth.getNumVoices(); ++v)
            if (auto* sv = dynamic_cast<tw::SamplerVoice*> (r.L.synth.getVoice (v)))
                if (sv->isPlaying()) { ++sounding; int si = sv->getSliceIndex(); if (si >= 0 && si < 64) distinct[si] = true; }
        int nd = 0; for (bool d : distinct) nd += d ? 1 : 0;
        ok ((int) grid.size() == 64 && sounding == 64 && nd == 64,
            "[1] LAYER mode on a 64-chop grid sounds ALL 64 chops on one key (32 voices stole half of them)",
            "chops=" + std::to_string (grid.size()) + " sounding=" + std::to_string (sounding) + " distinct=" + std::to_string (nd));
    }

    // The pitch-mode setup Max makes: a fine-tuned transpose + a full envelope + volume.
    auto setBaseline = [] (Rig& r)
    {
        auto& ps = r.L.pitchModeSlice;
        ps.pitchOffsetSemis = -2.37f;   // -2 st, -37 cents
        ps.attackMs = 12.0f; ps.decayMs = 150.0f; ps.sustainLevel = 0.4f; ps.releaseMs = 60.0f; ps.volume = 0.6f;
    };

    // ── [2] CHOP mode at the root == pitch mode ──
    {
        Rig r; setBaseline (r);
        auto whole = r.play (tw::SliceContext::Mode::Whole, ROOT, 30, 50);
        auto grid = tw::makeGridSlices (*r.buf, 1, true);   // one chop = the whole one-shot, born inheriting
        std::atomic_store (&r.L.currentSlices, std::make_shared<const tw::SliceList> (grid));
        auto chop = r.play (tw::SliceContext::Mode::ChopChromaticLayout, ROOT, 30, 50);
        const double d = maxDiff (whole, chop);
        ok (rms (whole) > 1e-3 && d == 0.0,
            "🚨 [2] a fresh chop in CHOP mode at the root is BIT-IDENTICAL to pitch mode (fine-tuned transpose, A/D/S/R, volume, note-on + release)",
            "rms=" + std::to_string (rms (whole)) + " maxDiff=" + std::to_string (d));
    }

    // ── [3] CHROMATIC mode, a fifth above the root ──
    {
        Rig r; setBaseline (r);
        auto whole = r.play (tw::SliceContext::Mode::Whole, ROOT + 7, 30, 50);
        auto grid = tw::makeGridSlices (*r.buf, 1, true);
        std::atomic_store (&r.L.currentSlices, std::make_shared<const tw::SliceList> (grid));
        auto chop = r.play (tw::SliceContext::Mode::ChromaticOneSlice, ROOT + 7, 30, 50);
        const double d = maxDiff (whole, chop);
        ok (rms (whole) > 1e-3 && d == 0.0, "[3] ...and CHROMATIC mode a fifth up is bit-identical too", "maxDiff=" + std::to_string (d));
    }

    // ── [4] AUDITION carries the pitch-mode tuning ──
    {
        Rig r; setBaseline (r);
        auto grid = tw::makeGridSlices (*r.buf, 1, true);
        std::atomic_store (&r.L.currentSlices, std::make_shared<const tw::SliceList> (grid));
        auto keyed = r.play (tw::SliceContext::Mode::ChopChromaticLayout, ROOT, 1000, 20);   // no note-off in range
        std::vector<float> aud; juce::AudioBuffer<float> o (2, BLK);
        for (int k = 0; k < 20; ++k)
        {
            juce::MidiBuffer mb;
            r.L.synth.setSliceContext (r.ctx (tw::SliceContext::Mode::ChopChromaticLayout));
            if (k == 0) r.L.synth.auditionSlice (grid[0], 0, 0);
            o.clear(); r.L.synth.renderNextBlock (o, mb, 0, BLK);
            for (int i = 0; i < BLK; ++i) aud.push_back (o.getSample (0, i));
        }
        const double d = maxDiff (keyed, aud);
        ok (rms (aud) > 1e-3 && d == 0.0,
            "🚨 [4] AUDITION (a click on the chop) plays the pitch-mode transpose + fine tune — it used to play unity",
            "maxDiff(keyed, audition)=" + std::to_string (d));
    }

    // ── [5] an edited chop keeps its override ──
    {
        Rig r; setBaseline (r);
        auto whole = r.play (tw::SliceContext::Mode::Whole, ROOT, 30, 50);
        auto grid = tw::makeGridSlices (*r.buf, 1, true);
        grid[0].releaseMs = 900.0f; grid[0].volume = 1.2f;   // the user moved two sliders
        std::atomic_store (&r.L.currentSlices, std::make_shared<const tw::SliceList> (grid));
        auto chop = r.play (tw::SliceContext::Mode::ChopChromaticLayout, ROOT, 30, 50);
        ok (maxDiff (whole, chop) > 1e-3, "[5] an EDITED chop (release + volume overridden) is not the baseline — the override wins",
            "maxDiff=" + std::to_string (maxDiff (whole, chop)));
    }

    // ── [6] the playhead feed ──
    {
        Rig r;
        const double len = (double) r.buf->getNumSamples();
        juce::AudioBuffer<float> o (2, BLK);
        auto findVoice = [&r]() -> tw::SamplerVoice* {
            for (int v = 0; v < r.L.synth.getNumVoices(); ++v)
                if (auto* sv = dynamic_cast<tw::SamplerVoice*> (r.L.synth.getVoice (v))) if (sv->isPlaying()) return sv;
            return nullptr; };
        auto run = [&] (bool reverse, int note, float& p0, float& p1, float& vel) {
            r.L.pitchModeSlice.reverse = reverse;
            for (int k = 0; k < 11; ++k)
            {
                juce::MidiBuffer mb; if (k == 0) mb.addEvent (juce::MidiMessage::noteOn (1, note, (juce::uint8) 100), 0);
                r.L.synth.setSliceContext (r.ctx (tw::SliceContext::Mode::Whole));
                o.clear(); r.L.synth.renderNextBlock (o, mb, 0, BLK);
                auto* sv = findVoice();
                if (k == 0)  p0 = sv ? sv->getPlayheadNorm() : -9.0f;
                if (k == 10) { p1 = sv ? sv->getPlayheadNorm() : -9.0f; vel = sv ? sv->getPlayheadVelocity() : 0.0f; }
            }
            r.L.synth.allNotesOff (0, false);
        };
        float a0, a1, av, b0, b1, bv, c0, c1, cv;
        run (false, ROOT,      a0, a1, av);
        run (false, ROOT + 12, c0, c1, cv);
        run (true,  ROOT,      b0, b1, bv);
        const double expect = 10.0 * BLK / len;                // ten blocks at unity
        const double expOct = 2.0 * expect;                    // an octave up reads twice as fast
        // the voice ends: render past the end of a short sample with nothing held
        r.L.pitchModeSlice.reverse = false;
        juce::MidiBuffer mb; mb.addEvent (juce::MidiMessage::noteOn (1, ROOT + 24, (juce::uint8) 100), 0);
        tw::SamplerVoice* last = nullptr;
        for (int k = 0; k < 60; ++k)
        {
            r.L.synth.setSliceContext (r.ctx (tw::SliceContext::Mode::Whole));
            o.clear(); r.L.synth.renderNextBlock (o, k == 0 ? mb : juce::MidiBuffer(), 0, BLK);
            if (k == 0) last = findVoice();
        }
        const float after = last ? last->getPlayheadNorm() : 0.0f;
        const bool fwd  = std::abs ((a1 - a0) - expect) < 0.002 && av > 0.0f && std::abs (av - (float) (SR / len)) < 0.02f;
        const bool oct  = std::abs ((c1 - c0) - expOct) < 0.003 && cv > 1.9f * av;
        const bool back = (b1 < b0) && bv < 0.0f && std::abs ((b0 - b1) - expect) < 0.002;
        char info[256]; std::snprintf (info, sizeof info, "fwd %.4f→%.4f (Δ%.4f want %.4f) v=%.3f · oct Δ%.4f v=%.3f · rev %.4f→%.4f v=%.3f · after end %.2f",
                                       a0, a1, a1 - a0, expect, av, c1 - c0, cv, b0, b1, bv, after);
        ok (fwd && oct && back && after < 0.0f, "[6] the PLAYHEAD feed: advances at the read rate, twice as fast an octave up, backwards reversed, -1 when done", info);
    }

    // ── [7] no allocation on the audio thread ──
    {
        Rig r;
        auto grid = tw::makeGridSlices (*r.buf, 64, true);
        std::atomic_store (&r.L.currentSlices, std::make_shared<const tw::SliceList> (grid));
        juce::AudioBuffer<float> o (2, BLK);
        juce::MidiBuffer on[8], off;
        for (int k = 0; k < 8; ++k) on[k].addEvent (juce::MidiMessage::noteOn (1, ROOT + k, (juce::uint8) 100), 0);
        // warm-up (first touches)
        for (int k = 0; k < 4; ++k) { r.L.synth.setSliceContext (r.ctx (tw::SliceContext::Mode::ChopChromaticLayout)); r.L.synth.renderNextBlock (o, on[k], 0, BLK); }
        const tw::SliceContext cChop  = r.ctx (tw::SliceContext::Mode::ChopChromaticLayout);
        const tw::SliceContext cLayer = r.ctx (tw::SliceContext::Mode::Layer);
        const tw::SliceContext cWhole = r.ctx (tw::SliceContext::Mode::Whole);
        gAllocs = 0; gCount = true;
        for (int k = 0; k < 64; ++k)
        {
            r.L.synth.setSliceContext ((k % 3 == 0) ? cLayer : (k % 3 == 1) ? cChop : cWhole);
            r.L.synth.renderNextBlock (o, (k % 4 == 0) ? on[(k / 4) % 8] : off, 0, BLK);
        }
        gCount = false;
        ok (gAllocs.load() == 0, "🚨 [7] NO AUDIO-THREAD ALLOCATION: 64 blocks of setSliceContext + render with 64 chops (LAYER / CHOP / pitch) allocate nothing",
            "allocations=" + std::to_string (gAllocs.load()));
    }

    std::printf ("\n%d passed, %d failed\n", gPass, gFail);
    return gFail == 0 ? 0 : 1;
}
