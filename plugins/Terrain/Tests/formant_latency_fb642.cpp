// ══════════════════════════════════════════════════════════════════════════════════════════════
//  fb642 — FORMANT (AND STRETCH) ADD NO LATENCY — measured on the engine and on the real processor.
//
//    Tests/formant_latency_fb642.sh      (build Terrain first; macOS)
//
//  Max: "the formant adds latency to the sample which we do not need — we do not want the formant to add latency on the
//  sample mode or any other mode." Formant/Stretch route the Sample engine through a phase vocoder (Tones = Signalsmith,
//  Texture = Signalsmith + jitter, Beats = grain history). A vocoder fed LIVE has one STFT of latency; a sample is not
//  live, so the voice now primes the engine with the sample's own look-ahead (WarpProcessor::primeOutput).
//  The probe is a train of 20 ms bursts at IRREGULAR gaps, so an envelope cross-correlation has exactly one right answer
//  (a periodic train would alias every period onto zero).
//    [1] engine: tw::WarpProcessor per mode, fed the probe — UNPRIMED (the old path, the control) must show the latency;
//        PRIMED must land within 3 ms of the source. Two measures: ONSET (when the first burst arrives — what a player
//        feels) for every mode, and the whole-train cross-correlation for Tones and Beats. Texture is exempt from the
//        cross-correlation on purpose: it JUMPS its read head into the past every ~80 ms (its "Flux" character), so its
//        timeline is scrambled by design and only the onset has one right answer;
//    [2] processor: Formant +0.5 in each mode starts within 3 ms of the direct path (Formant 0), and Tones/Beats stay
//        aligned across the train;
//    [3] mid-note: Formant turned up WHILE a steady note holds opens no hole — no 10 ms window after the turn falls
//        below half the level before it;
//    [4] the formant still WORKS: on a vowel (harmonics of 110 Hz under a 700 Hz resonance) Formant +0.5 moves the
//        spectral centroid by more than 15 %.
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

static int fails = 0;
static void bar (const char* tag, bool ok, const juce::String& text)
{
    printf ("  %s [%s] %s\n", ok ? "✓" : "✗", tag, text.toRawUTF8()); if (! ok) ++fails;
}
static constexpr double kSr = 48000.0;
static const double kPi = juce::MathConstants<double>::pi;

static void setP (TerrainAudioProcessor& p, const char* id, float v)
{
    auto* q = p.getAPVTS().getParameter (id);
    if (q == nullptr) { printf ("  ✗ no parameter %s\n", id); ++fails; return; }
    q->setValueNotifyingHost (q->convertTo0to1 (v));
}

// 20 ms bursts of 440 Hz starting at irregular times (ms) — then repeating that 1.6 s pattern
static std::shared_ptr<juce::AudioBuffer<float>> probe (double seconds)
{
    const double starts[] = { 0, 70, 190, 330, 510, 620, 800, 1050, 1150, 1400 };
    auto b = std::make_shared<juce::AudioBuffer<float>> (1, (int) (kSr * seconds)); b->clear();
    for (int i = 0; i < b->getNumSamples(); ++i)
    {
        const double t = i / kSr, tm = std::fmod (t, 1.6) * 1000.0;
        for (double s : starts) if (tm >= s && tm < s + 20.0) { b->setSample (0, i, 0.5f * (float) std::sin (2.0 * kPi * 440.0 * t)); break; }
    }
    return b;
}
static std::vector<double> env2ms (const float* x, int n)
{
    const int w = (int) (kSr * 0.002); std::vector<double> e;
    for (int i = 0; i + w <= n; i += w) { double s = 0; for (int k = 0; k < w; ++k) s += (double) x[i + k] * x[i + k]; e.push_back (std::sqrt (s / w)); }
    return e;
}
// the first 2 ms window whose level passes 10 % of the loudest in the first second — in ms
static int onsetMs (const std::vector<double>& e)
{
    double mx = 0; for (size_t i = 0; i < e.size() && i < 500; ++i) mx = std::max (mx, e[i]);
    for (size_t i = 0; i < e.size(); ++i) if (e[i] > 0.1 * mx) return (int) i * 2;
    return -1;
}
// lag (2 ms steps) that best aligns y to x; positive = y is LATE
static int bestLag (const std::vector<double>& x, const std::vector<double>& y, int from, int span, int maxLag)
{
    int best = 0; double bestC = -1;
    for (int L = -maxLag; L <= maxLag; ++L)
    {
        double c = 0, nx = 0, ny = 0;
        for (int i = from; i < from + span; ++i)
        {
            const int j = i + L; if (j < 0 || j >= (int) y.size() || i >= (int) x.size()) continue;
            c += x[i] * y[j]; nx += x[i] * x[i]; ny += y[j] * y[j];
        }
        const double r = (nx > 0 && ny > 0) ? c / std::sqrt (nx * ny) : 0;
        if (r > bestC) { bestC = r; best = L; }
    }
    return best;
}

static juce::AudioBuffer<float> render (TerrainAudioProcessor& p, int total, std::function<void (int)> atBlock = {})
{
    juce::AudioBuffer<float> out (2, total); out.clear();
    juce::AudioBuffer<float> blk (2, 512);
    for (int s0 = 0, b = 0; s0 < total; s0 += 512, ++b)
    {
        if (atBlock) atBlock (b);
        const int n = std::min (512, total - s0);
        blk.setSize (2, n, false, false, true); blk.clear();
        juce::MidiBuffer mb;
        if (s0 == 0) mb.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);
        p.processBlock (blk, mb);
        for (int c = 0; c < 2; ++c) out.copyFrom (c, s0, blk, c, 0, n);
    }
    return out;
}
static std::unique_ptr<TerrainAudioProcessor> sampleProc (std::shared_ptr<juce::AudioBuffer<float>> smp, int stretchMode, float formant)
{
    auto p = std::make_unique<TerrainAudioProcessor>();
    p->prepareToPlay (kSr, 512);
    p->oscSampleBuffers_[0].setSampleRate (kSr);
    p->oscSampleBuffers_[0].store (std::move (smp));
    setP (*p, ParameterIDs::SYN_OSC_A_ENGINE, 1.f);            // SAMP
    setP (*p, ParameterIDs::SYN_OSC_A_LEVEL, 0.7f);
    setP (*p, ParameterIDs::SYN_OSC_B_LEVEL, 0.f);
    setP (*p, ParameterIDs::SYN_OSC_A_SAMPLE_SCAN, 0.5f);
    setP (*p, ParameterIDs::SYN_OSC_A_SAMPLE_LOOP_MODE, 1.f);  // Forward loop
    setP (*p, ParameterIDs::SYN_OSC_A_SAMPLE_STRETCH_MODE, (float) stretchMode);
    setP (*p, ParameterIDs::SYN_OSC_A_SAMPLE_FORMANT, formant);
    return p;
}
static double centroid (const juce::AudioBuffer<float>& b, int from)
{
    const int order = 13, N = 1 << order; juce::dsp::FFT fft (order);
    std::vector<float> buf ((size_t) N * 2, 0.f);
    const float* x = b.getReadPointer (0);
    for (int i = 0; i < N && from + i < b.getNumSamples(); ++i)
        buf[(size_t) i] = x[from + i] * (0.5f - 0.5f * std::cos (2.0f * juce::MathConstants<float>::pi * i / (N - 1)));
    fft.performFrequencyOnlyForwardTransform (buf.data());
    double num = 0, den = 0;
    for (int k = 1; k < N / 2; ++k) { const double f = k * kSr / N; num += f * buf[(size_t) k]; den += buf[(size_t) k]; }
    return den > 0 ? num / den : 0;
}

int main()
{
    juce::ScopedJuceInitialiser_GUI init;
    printf ("══ fb642 FORMANT ADDS NO LATENCY ══\n");
    const char* names[3] = { "Tones", "Beats", "Texture" };
    const tw::WarpMode modes[3] = { tw::WarpMode::Tones, tw::WarpMode::Beats, tw::WarpMode::Texture };
    auto src = probe (4.0);
    const float* s0 = src->getReadPointer (0);
    const auto eSrc = env2ms (s0, src->getNumSamples());

    // ── [1] the engine, unprimed (the old path) vs primed ─────────────────────────────────────
    for (int m = 0; m < 3; ++m)
    {
        auto lagOf = [&] (bool prime)
        {
            tw::WarpProcessor w; w.prepare (kSr, 2, 1024);
            w.setMode (modes[m]); w.setStretchRatio (1.0f); w.setPitchSemitones (0.0f); w.setFormantFactor (2.0f);
            w.noteOnReset();
            const int total = (int) (kSr * 2.0), bs = 512;
            std::vector<float> outL ((size_t) total), outR ((size_t) total), inL (4096), inR (4096);
            int readPos = 0;
            if (prime)
            {
                const int pn = w.primeLength();
                std::vector<float> pl ((size_t) pn), pr ((size_t) pn);
                for (int k = 0; k < pn; ++k) pl[(size_t) k] = pr[(size_t) k] = s0[readPos++];
                w.primeOutput (pl.data(), pr.data(), pn);
            }
            for (int o = 0; o < total; o += bs)
            {
                const int n = std::min (bs, total - o), srcN = w.sourceSamplesPerBlock (n);
                for (int k = 0; k < srcN; ++k) { inL[(size_t) k] = inR[(size_t) k] = s0[readPos++]; }
                w.process (inL.data(), inR.data(), outL.data() + o, outR.data() + o, n);
            }
            const auto e = env2ms (outL.data(), total);
            return std::make_pair (onsetMs (e) - onsetMs (eSrc), bestLag (eSrc, e, 60, 600, 150) * 2);   // ±300 ms
        };
        const auto un = lagOf (false), pr = lagOf (true);
        const bool xc = (m != 2);   // Texture: onset only (see the header)
        bar ("1", std::abs (pr.first) <= 3 && un.first >= 20 && (! xc || std::abs (pr.second) <= 3),
             juce::String::formatted ("%-7s engine: unprimed (the old path) starts %+d ms late, primed %+d ms", names[m], un.first, pr.first)
             + (xc ? juce::String::formatted ("; whole train %+d -> %+d ms", un.second, pr.second) : juce::String ("; (train scrambled by design)")));
    }

    // ── [2] the processor: Formant +0.5 vs the direct path, every mode ───────────────────────
    {
        const int N = (int) (kSr * 1.8);
        auto direct = sampleProc (src, 0, 0.0f);
        const auto od = render (*direct, N); const auto eD = env2ms (od.getReadPointer (0), N);
        for (int m = 0; m < 3; ++m)
        {
            auto w = sampleProc (src, m, 0.5f);
            const auto ow = render (*w, N);
            const auto eW = env2ms (ow.getReadPointer (0), N);
            const int on = onsetMs (eW) - onsetMs (eD), lag = bestLag (eD, eW, 60, 600, 150) * 2;
            const bool xc = (m != 2);
            bar ("2", std::abs (on) <= 3 && (! xc || std::abs (lag) <= 3),
                 juce::String::formatted ("%-7s Formant +0.5 in the plugin starts %+d ms from the direct path", names[m], on)
                 + (xc ? juce::String::formatted ("; whole train %+d ms", lag) : juce::String ("; (train scrambled by design)")));
            w->releaseResources();
        }
        direct->releaseResources();
    }

    // ── [3] Formant turned up mid-note ────────────────────────────────────────────────────────
    {
        auto tone = std::make_shared<juce::AudioBuffer<float>> (1, (int) (kSr * 4));
        for (int i = 0; i < tone->getNumSamples(); ++i) tone->setSample (0, i, 0.5f * (float) std::sin (2.0 * kPi * 220.0 * i / kSr));
        auto p = sampleProc (tone, 0, 0.0f);
        const int N = (int) (kSr * 1.2), turnBlock = (int) (kSr * 0.5) / 512;
        auto out = render (*p, N, [&] (int b) { if (b == turnBlock) setP (*p, ParameterIDs::SYN_OSC_A_SAMPLE_FORMANT, 0.5f); });
        const int turn = turnBlock * 512, w = (int) (kSr * 0.01);
        auto rms = [&] (int a) { double s = 0; for (int k = 0; k < w; ++k) { const float v = out.getSample (0, a + k); s += (double) v * v; } return std::sqrt (s / w); };
        const double before = rms (turn - 4 * w);
        double worst = 1e9;
        for (int a = turn; a + w < turn + (int) (kSr * 0.3); a += w) worst = std::min (worst, rms (a));
        bar ("3", worst > 0.5 * before,
             juce::String::formatted ("Formant turned up mid-note: the quietest 10 ms window in the next 300 ms is %.0f%% of the level before (no hole)",
                                      100.0 * worst / std::max (1e-12, before)));
        p->releaseResources();
    }

    // ── [4] the formant still works ───────────────────────────────────────────────────────────
    {
        auto vowel = std::make_shared<juce::AudioBuffer<float>> (1, (int) (kSr * 4)); vowel->clear();
        for (int h = 1; h * 110.0 < 12000.0; ++h)
        {
            const double f = h * 110.0, oct = std::log2 (f / 700.0), a = 0.2 * std::exp (-oct * oct / (2 * 0.35 * 0.35));
            for (int i = 0; i < vowel->getNumSamples(); ++i) vowel->addSample (0, i, (float) (a * std::sin (2.0 * kPi * f * i / kSr)));
        }
        const int N = (int) (kSr * 1.0);
        auto a = sampleProc (vowel, 0, 0.0f), b = sampleProc (vowel, 0, 0.5f);
        const auto oa = render (*a, N), ob = render (*b, N);
        const double ca = centroid (oa, (int) (kSr * 0.4)), cb = centroid (ob, (int) (kSr * 0.4));
        bar ("4", (cb - ca) / ca > 0.15,
             juce::String::formatted ("Formant +0.5 still moves the vowel: spectral centroid %.0f Hz -> %.0f Hz (%+.0f%%)", ca, cb, 100.0 * (cb - ca) / ca));
        a->releaseResources(); b->releaseResources();
    }
    printf ("formant_latency_fb642: %s\n", fails ? "FAIL" : "PASS");
    return fails ? 1 : 0;
}
