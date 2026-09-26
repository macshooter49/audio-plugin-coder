// ══════════════════════════════════════════════════════════════════════════════════════════════
//  tpfx — TERRAIN FX, OFFLINE: the effect build's processBlock on a real input, no host, no editor.
//
//    Tests/fx_offline.sh            (builds against build/.../TerrainFX_artefacts/Release/libTerrainFX_SharedCode.a)
//
//  [1] the default patch (Audio In → Reverb → Out) on a piano-ish input: wet, sensible level, a tail after the input stops
//  [2] an EMPTY chain (the Reverb taken out) is the input, BIT FOR BIT
//  [3] Glitch fed by the Audio In changes the sound          (log-spectral distance + residual vs the input)
//  [4] Shaper fed by the Audio In changes the sound          (same metrics)
//  [5] CPU: Audio In → Reverb at 48 kHz / 512 while playing, and after the input goes silent (the sleep)
//  [6] the input-transient trigger drives an envelope routed to the Reverb mix (the mono pool fires with no MIDI)
//  Exit 1 on any FAIL.
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
#define private public
#define protected public
#include "PluginProcessor.h"
#undef private
#undef protected

#if ! TERRAIN_FX
 #error "fx_offline must be compiled with the TerrainFX target's flags (TERRAIN_FX=1)"
#endif

static constexpr double SR = 48000.0; static constexpr int BLK = 512;
static int npass = 0, nfail = 0;
static void chk (bool ok, const char* label, const std::string& detail)
{
    (ok ? npass : nfail)++;
    std::printf ("  %s  %s\n        %s\n", ok ? "PASS" : "FAIL", label, detail.c_str());
}
static void setP (TerrainAudioProcessor& p, const char* id, float plain)
{
    auto* prm = p.apvts.getParameter (id);
    if (prm == nullptr) { std::printf ("!! no param %s\n", id); std::exit (2); }
    prm->setValueNotifyingHost (prm->convertTo0to1 (plain));
}

// A host transport: playing, 120 BPM, 4/4 — the Glitch and the Shaper read the grid off it.
struct Head : juce::AudioPlayHead
{
    long long pos = 0;
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo pi; pi.setIsPlaying (true); pi.setBpm (120.0); pi.setTimeInSamples (pos);
        pi.setPpqPosition ((double) pos / SR * 2.0); pi.setTimeSignature (juce::AudioPlayHead::TimeSignature { 4, 4 });
        return pi;
    }
};

// Piano-ish: a note every 0.5 s (C major arpeggio), 8 harmonics with 1/k amplitudes and faster decay up the series,
// a 2 ms attack, inharmonic stretch. `secs` of notes then `tail` of silence. Stereo, slightly different per side.
static juce::AudioBuffer<float> pianoIsh (double secs, double tail)
{
    const int n = (int) ((secs + tail) * SR);
    juce::AudioBuffer<float> b (2, n); b.clear();
    const int notes[] = { 48, 52, 55, 60, 64, 67, 72, 67 };
    const int step = (int) (0.5 * SR);
    for (int k = 0; k * step < (int) (secs * SR); ++k)
    {
        const double f0 = 440.0 * std::pow (2.0, (notes[k % 8] - 69) / 12.0);
        for (int i = k * step; i < n; ++i)
        {
            const double t = (double) (i - k * step) / SR;
            double s = 0.0;
            for (int h = 1; h <= 8; ++h)
                s += std::sin (2.0 * juce::MathConstants<double>::pi * f0 * h * std::sqrt (1.0 + 0.0004 * h * h) * t)
                     * std::exp (-t * (1.2 + 0.9 * h)) / h;
            s *= 0.22 * std::min (1.0, t / 0.002);
            b.addSample (0, i, (float) s);
            b.addSample (1, i, (float) (s * 0.93));
        }
    }
    return b;
}

struct Run { juce::AudioBuffer<float> out; double secPerBlkPlay = 0, secPerBlkQuiet = 0; bool asleep = false; };

static Run render (const juce::AudioBuffer<float>& in, const std::function<void (TerrainAudioProcessor&)>& setup, int quietFrom = -1)
{
    auto proc = std::make_unique<TerrainAudioProcessor>();
    auto& p = *proc;
    Head head; p.setPlayHead (&head);
    p.setPlayConfigDetails (2, 2, SR, BLK);
    p.prepareToPlay (SR, BLK);
    setup (p);
    for (int t = 0; t < 4; ++t) p.timerCallback();   // the lazy engine arms (the reverb's engine is built on the message thread)
    Run r; r.out.setSize (2, in.getNumSamples()); r.out.clear();
    juce::AudioBuffer<float> buf (2, BLK);
    double tPlay = 0, tQuiet = 0; int nPlay = 0, nQuiet = 0;
    for (int s0 = 0; s0 + BLK <= in.getNumSamples(); s0 += BLK)
    {
        buf.copyFrom (0, 0, in, 0, s0, BLK); buf.copyFrom (1, 0, in, 1, s0, BLK);
        juce::MidiBuffer m;
        const auto t0 = juce::Time::getHighResolutionTicks();
        p.processBlock (buf, m);
        const double dt = juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - t0);
        if (quietFrom >= 0 && s0 >= quietFrom) { tQuiet += dt; ++nQuiet; } else { tPlay += dt; ++nPlay; }
        head.pos += BLK;
        if ((s0 / BLK) % 2 == 1) p.timerCallback();
        r.out.copyFrom (0, s0, buf, 0, 0, BLK); r.out.copyFrom (1, s0, buf, 1, 0, BLK);
    }
    r.secPerBlkPlay = nPlay ? tPlay / nPlay : 0; r.secPerBlkQuiet = nQuiet ? tQuiet / nQuiet : 0;
    r.asleep = p.sleeping_.load();
    p.releaseResources();
    return r;
}

static double rmsDb (const juce::AudioBuffer<float>& b, int s0, int s1)
{
    double e = 0; int n = 0;
    for (int c = 0; c < 2; ++c) for (int i = s0; i < s1; ++i) { e += (double) b.getSample (c, i) * b.getSample (c, i); ++n; }
    return 10.0 * std::log10 (e / juce::jmax (1, n) + 1e-30);
}
static double residualDb (const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b, int s0, int s1)
{
    double e = 0, r = 0;
    for (int c = 0; c < 2; ++c) for (int i = s0; i < s1; ++i)
    { const double d = (double) a.getSample (c, i) - b.getSample (c, i); e += d * d; r += (double) b.getSample (c, i) * b.getSample (c, i); }
    return 10.0 * std::log10 ((e + 1e-30) / (r + 1e-30));
}
// Mean per-frame log-magnitude distance (dB), 2048-point frames, bins 2..1000 — phase-independent (the fb283 law).
static double lsdDb (const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b, int s0, int s1)
{
    constexpr int O = 11, N = 1 << O; juce::dsp::FFT fft (O);
    std::vector<float> fa (2 * N), fb (2 * N); double acc = 0; int frames = 0;
    for (int f = s0; f + N <= s1; f += N)
    {
        std::fill (fa.begin(), fa.end(), 0.0f); std::fill (fb.begin(), fb.end(), 0.0f);
        for (int i = 0; i < N; ++i)
        {
            const float w = 0.5f - 0.5f * std::cos (2.0f * juce::MathConstants<float>::pi * (float) i / (float) N);
            fa[(size_t) i] = w * 0.5f * (a.getSample (0, f + i) + a.getSample (1, f + i));
            fb[(size_t) i] = w * 0.5f * (b.getSample (0, f + i) + b.getSample (1, f + i));
        }
        fft.performFrequencyOnlyForwardTransform (fa.data()); fft.performFrequencyOnlyForwardTransform (fb.data());
        double d = 0; int nb = 0;
        for (int k = 2; k < 1000; ++k)
        {
            const double x = 20.0 * std::log10 (fa[(size_t) k] + 1e-6), y = 20.0 * std::log10 (fb[(size_t) k] + 1e-6);
            d += std::fabs (x - y); ++nb;
        }
        acc += d / nb; ++frames;
    }
    return frames ? acc / frames : 0.0;
}
static std::string fmt (const char* f, double a, double b = 0, double c = 0, double d = 0)
{ char s[512]; std::snprintf (s, sizeof s, f, a, b, c, d); return s; }

int main()
{
    setenv ("TERRAIN_DETERMINISTIC", "1", 1);
    juce::ScopedJuceInitialiser_GUI init;
    std::printf ("\n══ tpfx — TERRAIN FX OFFLINE (48 kHz, %d-sample blocks) ══\n", BLK);
    const double playS = 4.0, tailS = 12.0;
    const auto in = pianoIsh (playS, tailS);
    const int nPlay = (int) (playS * SR), nAll = in.getNumSamples() / BLK * BLK;

    {   // [0] the build really is the effect
        TerrainAudioProcessor p;
        chk (! p.acceptsMidi() && p.getBusCount (true) == 1 && p.getBusCount (false) == 1,
             "[0] the processor is an effect: one input bus, one output bus, no MIDI in",
             fmt ("inputs %.0f  outputs %.0f  acceptsMidi %.0f", p.getBusCount (true), p.getBusCount (false), p.acceptsMidi() ? 1 : 0));
    }

    // [1] the default patch
    const auto dflt = render (in, [] (TerrainAudioProcessor&) {}, nPlay + (int) (9.0 * SR));   // the last 3 s: the reverb tail has died and the plugin sleeps
    {
        const double inDb = rmsDb (in, 0, nPlay), outDb = rmsDb (dflt.out, 0, nPlay), tailDb = rmsDb (dflt.out, nPlay, nPlay + (int) SR);
        const double wet = residualDb (dflt.out, in, 0, nPlay);
        bool finite = true; for (int c = 0; c < 2; ++c) for (int i = 0; i < nAll; ++i) finite = finite && std::isfinite (dflt.out.getSample (c, i));
        chk (finite && outDb > inDb - 6.0 && outDb < inDb + 6.0 && wet > -30.0 && tailDb > -60.0,
             "[1] Audio In -> Reverb -> Out: a sensible level, audibly wet, and a tail after the input stops",
             fmt ("input %.1f dB  output %.1f dB  wet residual %.1f dB  tail (first 1 s after) %.1f dB", inDb, outDb, wet, tailDb));
    }
    // [2] empty chain = passthrough, bit for bit
    {
        const auto emp = render (in, [] (TerrainAudioProcessor& p) { setP (p, ParameterIDs::SYN_RVB_ACTIVE, 0.0f); });
        size_t diff = 0; double worst = 0;
        for (int c = 0; c < 2; ++c) for (int i = 0; i < nAll; ++i)
        {
            const float a = emp.out.getSample (c, i), b = in.getSample (c, i);
            if (std::memcmp (&a, &b, 4) != 0 && ! (a == 0.0f && std::fabs (b) < 1.0e-5f)) { ++diff; worst = std::max (worst, (double) std::fabs (a - b)); }
        }
        if (diff && std::getenv ("FXDBG"))
        {
            for (int lag = -4; lag <= 600; ++lag)
            {
                double xy = 0, xx = 0, yy = 0;
                for (int i = 2000; i < 40000; ++i) { const double x = in.getSample (0, i), y = emp.out.getSample (0, i + lag); xy += x * y; xx += x * x; yy += y * y; }
                const double r = xy / std::sqrt (xx * yy + 1e-30);
                if (r > 0.99 || lag == 0) std::printf ("   lag %d  corr %.5f  gain %.5f\n", lag, r, xy / (xx + 1e-30));
            }
            for (int i = 1000; i < 1006; ++i) std::printf ("   i %d in %.6f out %.6f\n", i, in.getSample (0, i), emp.out.getSample (0, i));
            for (int s = 0; s + 24000 <= nAll; s += 24000)
            {
                double mx = 0, mi = 0; for (int i = s; i < s + 24000; ++i) { mx = std::max (mx, (double) std::fabs (emp.out.getSample (0, i) - in.getSample (0, i))); mi = std::max (mi, (double) std::fabs (in.getSample (0, i))); }
                std::printf ("   t %.1fs  max|diff| %.3e  max|in| %.3e\n", s / SR, mx, mi);
            }
        }
        chk (diff == 0, "[2] an empty chain passes the input through BIT-IDENTICALLY",
             fmt ("%.0f differing samples of %.0f (worst %.3e; a sleeping block below -100 dBFS may read 0)", (double) diff, 2.0 * nAll, worst));
    }
    // [3] Glitch, [4] Shaper — each fed by the Audio In, nothing else in the chain
    const char* names[2] = { "[3] Glitch (FLOW_GLI_CHOPS = Audio In) audibly processes the input",
                             "[4] Shaper (FLOW_CHOP_CHOPS = Audio In, Volume + Filter lanes lit) audibly processes the input" };
    for (int k = 0; k < 2; ++k)
    {
        const bool gli = k == 0;
        const auto r = render (in, [gli] (TerrainAudioProcessor& p) {
            setP (p, ParameterIDs::SYN_RVB_ACTIVE, 0.0f);
            setP (p, "FLOW_CHAIN_1", gli ? 3.0f : 2.0f);     // Off, Arp, Chop (= the Shaper), Glitch, Robin
            setP (p, gli ? "FLOW_GLI_CHOPS" : "FLOW_CHOP_CHOPS", 1.0f);
            if (! gli) { setP (p, "FLOW_CHOP_VOL_ON", 1.0f); setP (p, "FLOW_CHOP_FILT_ON", 1.0f); } });   // a fresh Shaper is silent until a lane is lit (tp79)
        const double lsd = lsdDb (r.out, in, 0, nPlay), res = residualDb (r.out, in, 0, nPlay), lvl = rmsDb (r.out, 0, nPlay);
        chk (lsd > 3.0 && res > -20.0 && lvl > rmsDb (in, 0, nPlay) - 30.0, names[k],
             fmt ("log-spectral distance %.2f dB · residual %.1f dB · level %.1f dB (input %.1f dB)", lsd, res, lvl, rmsDb (in, 0, nPlay)));
    }
    // [5] CPU
    {
        const double blkSec = BLK / SR;
        const double play = dflt.secPerBlkPlay / blkSec * 100.0, quiet = dflt.secPerBlkQuiet / blkSec * 100.0;
        chk (play < 25.0 && quiet < 2.0 && dflt.asleep, "[5] CPU, Audio In -> Reverb (one core, % of real time)",
             fmt ("playing %.2f %% (%.1f us/block) · silent input after the tail %.3f %% (%.2f us/block) · asleep at the end: yes",
                  play, dflt.secPerBlkPlay * 1e6, quiet, dflt.secPerBlkQuiet * 1e6));
    }
    // [6] the transient trigger: an envelope on the Reverb mix, no MIDI anywhere
    {
        auto proc = std::make_unique<TerrainAudioProcessor>(); auto& p = *proc;
        p.setPlayConfigDetails (2, 2, SR, BLK); p.prepareToPlay (SR, BLK);
        p.setSynthModMatrix ("[{\"s\":106,\"d\":" + juce::String ((int) wc::ModDest::Res1) + ",\"v\":1.0}]");   // any global dest arms the pool
        p.monoEnvGlobalMask_.store (0xFFFFFFFFu);
        juce::AudioBuffer<float> buf (2, BLK); int fired = 0; double peak = 0;
        for (int s0 = 0; s0 + BLK <= nPlay; s0 += BLK)
        {
            buf.copyFrom (0, 0, in, 0, s0, BLK); buf.copyFrom (1, 0, in, 1, s0, BLK); juce::MidiBuffer m;
            p.processBlock (buf, m);
            fired += p.fxTrigOn_ ? 1 : 0;
            peak = std::max (peak, (double) p.monoLegEnv_[3].level());
        }
        chk (fired >= 6 && fired <= 10 && peak > 0.5, "[6] input transients trigger the envelopes (8 notes in, no MIDI)",
             fmt ("%.0f triggers for 8 note onsets · Mod env 1 peak %.2f", fired, peak));
    }
    std::printf ("\nfx_offline: %d PASS, %d FAIL\n", npass, nfail);
    return nfail ? 1 : 0;
}
