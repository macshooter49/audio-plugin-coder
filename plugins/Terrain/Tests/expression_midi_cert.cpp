// ══════════════════════════════════════════════════════════════════════════════════════════════
//  tp103 — THE SETTINGS PAGE'S EXPRESSION + MIDI ROWS, MEASURED on the real processor.
//
//    Tests/expression_midi.sh cert        (build Terrain first; macOS; links libTerrain_SharedCode.a)
//
//  Every bar feeds MIDI into TerrainAudioProcessor::processBlock and measures the AUDIO (or the voice pool the
//  audio comes from). The tone is Osc A on the FM engine with both modulators at depth 0 — a clean sine — so two
//  notes sounding at once can be told apart by their FFT peaks.
//    [1] MPE per-note bend: 60 on ch 2 + 67 on ch 3, range 48, bend ch 2 by +12 st (wheel 10240) → A moves 12 st,
//        B does not move.            [1c] MPE OFF: a bend on ANOTHER channel still moves every note (today's law).
//    [2] MPE per-note pressure → Level A reaches only its own voice (ch 3 pressure: B up, A unchanged).
//    [3] MPE per-note Slide (CC 74) → Level A reaches only its own voice.
//    [4] Poly aftertouch (MPE off, both notes on ch 1): pressure on key 67 raises only 67's level; channel pressure
//        still raises both (unchanged global law).
//    [5] MIDI channel 5: a channel-1 note is dropped (silence), a channel-5 note plays; Omni plays channel 1;
//        MPE on sets the filter aside.
//    [6] A4 = 432: note 69 measures 432 Hz (−31.77 cents from 440); the sample key detector names a 432 Hz sine
//        A4 +0 cents on the 432 grid; the resonator's note→Hz follows.
//    [7] Voice ceiling 8, Voices knob 16, 16 held notes → never more than 8 voices sound; unison 2 → 4 notes;
//        [7b] the steal is a FADE: D = (ceiling 8) - (ceiling 96) on identical MIDI is the stolen voice alone.
//    [8] MPE master pedal: CC 64 on ch 1 holds a note released on ch 2.
//  tp109 — PRESSURE SENSITIVITY (Settings → Expression: Pressure curve · Pressure start · Pressure ceiling)
//    [9]  every pressure 0..127 through each setting, on the REAL source path (128 poly keys at once, the settled
//         smoothed value the voices read), against the law computed independently in double; MPE member and
//         channel pressure spot-checked through the same path; the defaults are the identity bit for bit.
//    [10] a HARD curve: the lowest pressure that reaches 90 % (want >= 100/127) — and what a medium press gives.
//    [11] the START threshold, in the audio: a resting press below it moves nothing; above it the route opens.
//    [12] per-voice isolation survives a hard curve + start + ceiling (MPE member and poly key, as [2] / [4]).
//    [13] zipper: a slow 1-LSB-at-a-time press, the largest per-block step of the smoothed source per setting.
//    [14] the setting persists: the MidiSettings.json text one instance writes, a new instance reads back (clamped).
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
#include <complex>
#define private public
#define protected public
#include "PluginProcessor.h"
#undef private
#undef protected
#include "SampleKeyDetect.h"
#include "ResonatorNode.h"
#include "TerrainPressure.h"   // tp109

static constexpr double SR = 48000.0; static constexpr int BLK = 512;
static int fails = 0, passes = 0;
static void bar (const char* tag, bool ok, const std::string& text)
{ printf ("  %s [%s] %s\n", ok ? "PASS" : "FAIL", tag, text.c_str()); fflush (stdout); if (ok) ++passes; else ++fails; }
static std::string fmt (const char* f, ...) { char b[4096]; va_list a; va_start (a, f); vsnprintf (b, sizeof b, f, a); va_end (a); return b; }

static void setP (TerrainAudioProcessor& p, const char* id, float plain)
{
    auto* prm = p.apvts.getParameter (id);
    if (prm == nullptr) { printf ("!! no param %s\n", id); std::exit (2); }
    prm->setValueNotifyingHost (prm->convertTo0to1 (plain));
}

// ── a fresh instance with a SINE on Osc A (FM engine, both modulators silent), unison 1 ─────────────────────
static std::unique_ptr<TerrainAudioProcessor> fresh()
{
    auto p = std::make_unique<TerrainAudioProcessor>();
    p->setPlayConfigDetails (0, 2, SR, BLK);
    p->prepareToPlay (SR, BLK);
    setP (*p, ParameterIDs::SYN_OSC_A_ENGINE, 4.f);      // FM
    setP (*p, ParameterIDs::SYN_OSC_A_FM_DEPTH1, 0.f);
    setP (*p, ParameterIDs::SYN_OSC_A_FM_DEPTH2, 0.f);
    setP (*p, ParameterIDs::SYN_OSC_A_FM_FB, 0.f);
    setP (*p, ParameterIDs::SYN_OSC_A_UNISON, 1.f);
    setP (*p, ParameterIDs::SYN_OSC_A_WAVER, 0.f);
    for (const char* id : { ParameterIDs::SYN_OSC_B_ENABLE, ParameterIDs::SYN_OSC_C_ENABLE, ParameterIDs::SYN_OSC_D_ENABLE }) setP (*p, id, 0.f);
    return p;
}

using Events = std::function<void (int blk, juce::MidiBuffer&)>;
static std::vector<float> render (TerrainAudioProcessor& p, int nBlk, const Events& ev, std::function<void (int)> perBlock = {})
{
    std::vector<float> out; out.reserve ((size_t) nBlk * BLK);
    juce::AudioBuffer<float> buf (2, BLK);
    for (int b = 0; b < nBlk; ++b)
    {
        buf.clear(); juce::MidiBuffer m; if (ev) ev (b, m);
        p.processBlock (buf, m);
        out.insert (out.end(), buf.getReadPointer (0), buf.getReadPointer (0) + BLK);
        if (perBlock) perBlock (b);
    }
    return out;
}

// ── FFT (radix-2), Hann window, parabolic peak on the log magnitude ─────────────────────────────────────────
struct Spec { std::vector<double> mag; double binHz = 0; };
static Spec spectrum (const std::vector<float>& x, size_t from, size_t N)
{
    std::vector<std::complex<double>> a (N);
    for (size_t i = 0; i < N; ++i)
    {
        const double w = 0.5 - 0.5 * std::cos (2.0 * M_PI * (double) i / (double) (N - 1));
        a[i] = (from + i < x.size()) ? w * (double) x[from + i] : 0.0;
    }
    for (size_t i = 1, j = 0; i < N; ++i) { size_t bit = N >> 1; for (; j & bit; bit >>= 1) j ^= bit; j ^= bit; if (i < j) std::swap (a[i], a[j]); }
    for (size_t len = 2; len <= N; len <<= 1)
    {
        const double ang = -2.0 * M_PI / (double) len; const std::complex<double> wl (std::cos (ang), std::sin (ang));
        for (size_t i = 0; i < N; i += len) { std::complex<double> w (1.0, 0.0);
            for (size_t k = 0; k < len / 2; ++k) { auto u = a[i + k], v = a[i + k + len / 2] * w; a[i + k] = u + v; a[i + k + len / 2] = u - v; w *= wl; } }
    }
    Spec s; s.binHz = SR / (double) N; s.mag.resize (N / 2);
    for (size_t k = 0; k < N / 2; ++k) s.mag[k] = std::abs (a[k]);
    return s;
}
static double peakHz (const Spec& s, double lo, double hi)
{
    const size_t k0 = (size_t) std::ceil (lo / s.binHz), k1 = (size_t) std::floor (hi / s.binHz);
    size_t best = k0; for (size_t k = k0; k <= k1 && k + 1 < s.mag.size(); ++k) if (s.mag[k] > s.mag[best]) best = k;
    if (best == 0 || best + 1 >= s.mag.size()) return (double) best * s.binHz;
    const double y0 = std::log (s.mag[best - 1] + 1e-30), y1 = std::log (s.mag[best] + 1e-30), y2 = std::log (s.mag[best + 1] + 1e-30);
    const double d = 0.5 * (y0 - y2) / (y0 - 2.0 * y1 + y2);
    return ((double) best + d) * s.binHz;
}
static double bandDb (const Spec& s, double lo, double hi)
{
    double e = 0; for (size_t k = (size_t) (lo / s.binHz); k <= (size_t) (hi / s.binHz) && k < s.mag.size(); ++k) e += s.mag[k] * s.mag[k];
    return 10.0 * std::log10 (e + 1e-30);
}
static double rmsDb (const std::vector<float>& x, size_t from, size_t n)
{ double e = 0; for (size_t i = from; i < from + n && i < x.size(); ++i) e += (double) x[i] * x[i]; return 10.0 * std::log10 (e / (double) n + 1e-30); }
static constexpr size_t NFFT = 32768;
static size_t at (int blk) { return (size_t) blk * BLK; }

int main()
{
    setenv ("TERRAIN_DETERMINISTIC", "1", 1);   // also keeps the owner's MidiSettings.json out of the measurement
    juce::ScopedJuceInitialiser_GUI init;
    printf ("tp103 expression + MIDI cert (the real processor, %g kHz, %d-sample blocks)\n", SR / 1000.0, BLK);
    const double fA = 261.6255653, fB = 391.9954360;   // notes 60 and 67 at A4 = 440

    // ── [1] MPE per-note bend ──────────────────────────────────────────────────────────────────────────────
    {
        auto pp = fresh(); auto& p = *pp; p.setMpeOn (true, 48.0f, false);
        const int bendAt = 120;
        auto x = render (p, 260, [&] (int b, juce::MidiBuffer& m) {
            if (b == 2) { m.addEvent (juce::MidiMessage::noteOn (2, 60, (juce::uint8) 100), 0); m.addEvent (juce::MidiMessage::noteOn (3, 67, (juce::uint8) 100), 0); }
            if (b == bendAt) m.addEvent (juce::MidiMessage::pitchWheel (2, 8192 + 2048), 0);   // +0.25 of 48 st = +12 st
        });
        const auto s0 = spectrum (x, at (50), NFFT), s1 = spectrum (x, at (bendAt + 20), NFFT);
        const double a0 = peakHz (s0, 240, 285), b0 = peakHz (s0, 370, 415);
        const double a1 = peakHz (s1, 490, 560), b1 = peakHz (s1, 370, 415);
        const double centsA = 1200.0 * std::log2 (a1 / a0), centsB = 1200.0 * std::log2 (b1 / b0);
        const double aGone = bandDb (s1, 240, 285) - bandDb (s0, 240, 285);
        bar ("1 MPE bend", std::abs (centsA - 1200.0) < 3.0 && std::abs (centsB) < 1.0 && aGone < -30.0 && std::abs (a0 - fA) < 0.5 && std::abs (b0 - fB) < 0.5,
             fmt ("ch2 note 60: %.2f -> %.2f Hz = %+.2f cents (want +1200) · ch3 note 67: %.2f -> %.2f Hz = %+.3f cents (want 0) · 60's old band %+.1f dB",
                  a0, a1, centsA, b0, b1, centsB, aGone));
    }
    {   // [1c] MPE off: the same bend on channel 2 is the whole instrument's (range 2 st) — both notes move
        auto pp = fresh(); auto& p = *pp;
        auto x = render (p, 260, [&] (int b, juce::MidiBuffer& m) {
            if (b == 2) { m.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0); m.addEvent (juce::MidiMessage::noteOn (1, 67, (juce::uint8) 100), 0); }
            if (b == 120) m.addEvent (juce::MidiMessage::pitchWheel (2, 16383), 0);
        });
        const auto s0 = spectrum (x, at (50), NFFT), s1 = spectrum (x, at (140), NFFT);
        const double cA = 1200.0 * std::log2 (peakHz (s1, 270, 310) / peakHz (s0, 240, 285));
        const double cB = 1200.0 * std::log2 (peakHz (s1, 410, 460) / peakHz (s0, 370, 415));
        bar ("1c MPE off", std::abs (cA - 200.0) < 3.0 && std::abs (cB - 200.0) < 3.0,
             fmt ("a channel-2 wheel at full moves BOTH channel-1 notes: %+.2f / %+.2f cents (want +200, range 2)", cA, cB));
    }

    // ── [2] / [3] MPE per-note pressure and slide → Level A (knob 0.5, route +0.5) ────────────────────────
    for (int which = 0; which < 2; ++which)
    {
        auto pp = fresh(); auto& p = *pp; p.setMpeOn (true, 48.0f, false);
        setP (p, ParameterIDs::SYN_OSC_A_LEVEL, 0.5f);
        const int src = which == 0 ? wc::kAftertouchSrc : wc::kSlideSrc;
        p.setSynthModMatrix ("[{\"s\":" + juce::String (src) + ",\"d\":" + juce::String ((int) wc::ModDest::LevelA) + ",\"v\":0.5}]");
        auto x = render (p, 260, [&] (int b, juce::MidiBuffer& m) {
            if (b == 2) { m.addEvent (juce::MidiMessage::noteOn (2, 60, (juce::uint8) 100), 0); m.addEvent (juce::MidiMessage::noteOn (3, 67, (juce::uint8) 100), 0); }
            if (b == 120) m.addEvent (which == 0 ? juce::MidiMessage::channelPressureChange (3, 127) : juce::MidiMessage::controllerEvent (3, 74, 127), 0);
        });
        const auto s0 = spectrum (x, at (50), NFFT), s1 = spectrum (x, at (150), NFFT);
        const double dA = bandDb (s1, 240, 285) - bandDb (s0, 240, 285), dB = bandDb (s1, 370, 415) - bandDb (s0, 370, 415);
        bar (which == 0 ? "2 MPE pressure" : "3 MPE slide", dB > 4.0 && std::abs (dA) < 0.3,
             fmt ("%s on ch 3 only -> Level A: ch3 note 67 %+.2f dB, ch2 note 60 %+.3f dB (want 67 up ~6 dB, 60 unchanged)",
                  which == 0 ? "pressure 127" : "CC 74 = 127", dB, dA));
    }

    // ── [4] poly aftertouch, MPE off ─────────────────────────────────────────────────────────────────────────
    {
        auto pp = fresh(); auto& p = *pp;
        setP (p, ParameterIDs::SYN_OSC_A_LEVEL, 0.5f);
        p.setSynthModMatrix ("[{\"s\":231,\"d\":" + juce::String ((int) wc::ModDest::LevelA) + ",\"v\":0.5}]");
        auto x = render (p, 380, [&] (int b, juce::MidiBuffer& m) {
            if (b == 2) { m.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0); m.addEvent (juce::MidiMessage::noteOn (1, 67, (juce::uint8) 100), 0); }
            if (b == 120) m.addEvent (juce::MidiMessage::aftertouchChange (1, 67, 127), 0);   // 0xA0: key 67 only
            if (b == 240) m.addEvent (juce::MidiMessage::aftertouchChange (1, 67, 0), 0);
            if (b == 250) m.addEvent (juce::MidiMessage::channelPressureChange (1, 127), 0);  // 0xD0: every note
        });
        const auto s0 = spectrum (x, at (50), NFFT), s1 = spectrum (x, at (160), NFFT), s2 = spectrum (x, at (300), NFFT);
        const double dA = bandDb (s1, 240, 285) - bandDb (s0, 240, 285), dB = bandDb (s1, 370, 415) - bandDb (s0, 370, 415);
        const double cA = bandDb (s2, 240, 285) - bandDb (s0, 240, 285), cB = bandDb (s2, 370, 415) - bandDb (s0, 370, 415);
        bar ("4 poly AT", dB > 4.0 && std::abs (dA) < 0.3 && cA > 4.0 && cB > 4.0,
             fmt ("poly pressure on key 67 -> 67 %+.2f dB, 60 %+.3f dB · channel pressure -> 67 %+.2f dB, 60 %+.2f dB", dB, dA, cB, cA));
    }

    // ── [5] MIDI channel filter ──────────────────────────────────────────────────────────────────────────────
    {
        auto run = [&] (int filt, int ch, bool mpe) {
            auto pp = fresh(); auto& p = *pp; p.setMidiChannelFilter (filt, false); if (mpe) p.setMpeOn (true, 48.0f, false);
            auto x = render (p, 80, [&] (int b, juce::MidiBuffer& m) { if (b == 2) m.addEvent (juce::MidiMessage::noteOn (ch, 60, (juce::uint8) 100), 0); });
            return rmsDb (x, at (30), at (40));
        };
        const double f5c1 = run (5, 1, false), f5c5 = run (5, 5, false), omni = run (0, 1, false), f5c2mpe = run (5, 2, true);
        bar ("5 MIDI channel", f5c1 < -120.0 && f5c5 > -40.0 && omni > -40.0 && f5c2mpe > -40.0,
             fmt ("channel 5: a ch-1 note %.1f dB (dropped), a ch-5 note %.1f dB · Omni ch 1 %.1f dB · channel 5 + MPE on, ch-2 note %.1f dB (MPE owns the channels)",
                  f5c1, f5c5, omni, f5c2mpe));
    }

    // ── [6] A4 ───────────────────────────────────────────────────────────────────────────────────────────────
    {
        auto measure = [&] (float a4) {
            auto pp = fresh(); auto& p = *pp; p.setTuningA4 (a4, false);
            auto x = render (p, 120, [&] (int b, juce::MidiBuffer& m) { if (b == 2) m.addEvent (juce::MidiMessage::noteOn (1, 69, (juce::uint8) 100), 0); });
            const double hz = peakHz (spectrum (x, at (40), NFFT), 400, 470);
            p.setTuningA4 (440.0f, false);
            return hz;
        };
        const double h440 = measure (440.0f), h432 = measure (432.0f);
        const double cents = 1200.0 * std::log2 (h432 / h440);
        bar ("6 A4 432", std::abs (h440 - 440.0) < 0.05 && std::abs (h432 - 432.0) < 0.05 && std::abs (cents - (-31.7667)) < 0.2,
             fmt ("note 69: %.3f Hz at A4 440, %.3f Hz at A4 432 = %+.2f cents (want -31.77)", h440, h432, cents));
        // the sample key detector and the resonator, on the same process-wide value
        const int n = 48000; std::vector<float> sine (n); for (int i = 0; i < n; ++i) sine[i] = 0.5f * (float) std::sin (2.0 * M_PI * 432.0 * i / 48000.0);
        const float* ch[1] = { sine.data() };
        wc::tuningA4Hz().store (440.0f); const auto k440 = tw::SampleKeyDetector::detect (ch, 1, n, 48000.0);
        const float r440 = wc::ResonatorNode::noteToHz (69);
        wc::tuningA4Hz().store (432.0f); const auto k432 = tw::SampleKeyDetector::detect (ch, 1, n, 48000.0);
        const float r432 = wc::ResonatorNode::noteToHz (69);
        wc::tuningA4Hz().store (440.0f);
        bar ("6b A4 detect", k432.midiNote == 69 && std::abs (k432.cents) < 1.0 && std::abs (k440.cents + 31.77) < 1.0 && std::abs (r432 - 432.0f) < 0.01f && std::abs (r440 - 440.0f) < 0.01f,
             fmt ("a 432 Hz sine is named note %d %+.2f cents at A4 440, note %d %+.2f cents at A4 432 · resonator note 69 = %.2f / %.2f Hz",
                  k440.midiNote, k440.cents, k432.midiNote, k432.cents, r440, r432));
    }

    // ── [7] voice ceiling ────────────────────────────────────────────────────────────────────────────────────
    {
        auto run = [&] (int ceiling, int unison, int& maxSounding, int& maxAlive, double& maxD2Steal, double& maxD2Steady) {
            auto pp = fresh(); auto& p = *pp; p.setVoiceCeiling (ceiling, false);
            setP (p, ParameterIDs::SYN_VOICES, 16.f); setP (p, ParameterIDs::SYN_OSC_A_UNISON, (float) unison);
            maxSounding = 0; maxAlive = 0;
            std::vector<int> stealBlocks;
            auto x = render (p, 200, [&] (int b, juce::MidiBuffer& m) {
                if (b >= 4 && b < 4 + 16 * 4 && (b - 4) % 4 == 0) m.addEvent (juce::MidiMessage::noteOn (1, 48 + (b - 4) / 4 * 2, (juce::uint8) 100), 100);
            }, [&] (int b) {
                int snd = 0, alive = 0, stealing = 0;
                for (auto* v : p.synthVoices_) if (v != nullptr && v->getCurrentlyPlayingNote() >= 0) { ++alive; if (v->isStealing()) ++stealing; else ++snd; }
                maxSounding = std::max (maxSounding, snd); maxAlive = std::max (maxAlive, alive);
                if (stealing > 0) stealBlocks.push_back (b);
            });
            p.setVoiceCeiling (96, false);
            auto d2max = [&] (size_t from, size_t to) { double mx = 0; for (size_t i = from + 2; i < to && i < x.size(); ++i) mx = std::max (mx, (double) std::abs (x[i] - 2.f * x[i - 1] + x[i - 2])); return mx; };
            maxD2Steal = 0; for (int b : stealBlocks) maxD2Steal = std::max (maxD2Steal, d2max (at (b), at (b + 1)));
            maxD2Steady = d2max (at (160), at (200));   // eight held notes, nothing changing
            return (int) stealBlocks.size();
        };
        int s8 = 0, a8 = 0, s4 = 0, a4n = 0; double d2s = 0, d2q = 0, d2s2 = 0, d2q2 = 0;
        const int nSteal = run (8, 1, s8, a8, d2s, d2q);
        run (8, 2, s4, a4n, d2s2, d2q2);
        int sOff = 0, aOff = 0; double x1 = 0, x2 = 0; run (96, 1, sOff, aOff, x1, x2);
        bar ("7 voice ceiling", s8 <= 8 && s4 <= 4 && sOff == 16 && nSteal > 0,
             fmt ("16 held notes, Voices 16: ceiling 8 -> at most %d sounding (%d incl. fading) · unison 2 -> %d · ceiling 96 -> %d · %d blocks with a steal fading",
                  s8, a8, s4, sOff, nSteal));
        juce::ignoreUnused (d2s, d2q, d2s2, d2q2, x1, x2, a4n, aOff);
    }
    {   // [7b] THE STEAL IS A FADE. Nine notes, ceiling 8: the ninth steals the first. D = (ceiling 8) − (ceiling 96) on the
        //  identical MIDI is exactly 0 until the steal and then exactly the stolen voice's missing part, so D's own
        //  sample-to-sample step IS the steal's step. A hard cut would step by up to the voice's whole amplitude.
        auto run = [&] (int ceiling) {
            auto pp = fresh(); auto& p = *pp; p.setVoiceCeiling (ceiling, false); setP (p, ParameterIDs::SYN_VOICES, 16.f);
            auto x = render (p, 160, [&] (int b, juce::MidiBuffer& m) {
                if (b >= 4 && b < 4 + 9 * 4 && (b - 4) % 4 == 0) m.addEvent (juce::MidiMessage::noteOn (1, 48 + (b - 4) / 4 * 2, (juce::uint8) 100), 100);
            });
            p.setVoiceCeiling (96, false); return x;
        };
        const auto xs = run (8), xr = run (96);
        size_t first = xs.size(); double dMax = 0, stepMax = 0;
        for (size_t i = 0; i < xs.size(); ++i) { const double d = (double) xs[i] - (double) xr[i]; if (d != 0.0 && first == xs.size()) first = i; dMax = std::max (dMax, std::abs (d)); }
        for (size_t i = first + 1; i < xs.size(); ++i) stepMax = std::max (stepMax, std::abs (((double) xs[i] - xr[i]) - ((double) xs[i - 1] - xr[i - 1])));
        const double sineStep = dMax * 2.0 * M_PI * 130.8128 / SR;   // the stolen note's (48) own largest step at that amplitude
        const size_t stealBlk = first / BLK, ninthOn = at (4 + 8 * 4) + 100;
        bar ("7b steal fade", first != xs.size() && first >= ninthOn && dMax > 1e-3 && stepMax < 1.5 * sineStep + dMax / 1000.0,
             fmt ("the ninth note (sample %zu) steals the first: D departs 0 at sample %zu (block %zu), reaches %.4f (the voice gone); "
                  "max sample-to-sample step of D %.5f = %.1f%% of a hard cut's worst (%.4f); the sine's own step %.5f",
                  ninthOn, first, stealBlk, dMax, stepMax, 100.0 * stepMax / dMax, dMax, sineStep));
    }

    // ── [8] MPE master pedal ─────────────────────────────────────────────────────────────────────────────────
    {
        auto run = [&] (bool pedal) {
            auto pp = fresh(); auto& p = *pp; p.setMpeOn (true, 48.0f, false);
            auto x = render (p, 200, [&] (int b, juce::MidiBuffer& m) {
                if (b == 2 && pedal) m.addEvent (juce::MidiMessage::controllerEvent (1, 64, 127), 0);
                if (b == 4) m.addEvent (juce::MidiMessage::noteOn (2, 60, (juce::uint8) 100), 0);
                if (b == 40) m.addEvent (juce::MidiMessage::noteOff (2, 60), 0);
            });
            return rmsDb (x, at (150), at (40));
        };
        const double held = run (true), free = run (false);
        bar ("8 MPE pedal", held > -40.0 && free < held - 30.0,
             fmt ("a note released on member ch 2: %.1f dB 2 s later with CC 64 held on master ch 1, %.1f dB without", held, free));
    }

    // ══ tp109 — PRESSURE SENSITIVITY ══════════════════════════════════════════════════════════════════════════
    struct Shape { const char* name; float c, s, e; };
    const Shape shapes[] = { { "linear (default)", 0.f, 0.f, 1.f }, { "Soft -0.45", -0.45f, 0.f, 1.f }, { "Hard +0.45", 0.45f, 0.f, 1.f },
                             { "Harder +0.8", 0.8f, 0.f, 1.f }, { "start 20 %", 0.f, 0.2f, 1.f }, { "ceiling 80 %", 0.f, 0.f, 0.8f },
                             { "Hard, start 10 %, ceiling 90 %", 0.45f, 0.1f, 0.9f } };
    auto law = [] (int v, const Shape& sh) {   // the published law, in double, written out again (not wc::PressureShape)
        double u = (double) v / 127.0;
        if (sh.s > 0.f || sh.e < 1.f) u = (u - (double) sh.s) / ((double) sh.e - (double) sh.s);
        u = std::clamp (u, 0.0, 1.0);
        return std::pow (u, std::pow (2.0, 2.6 * (double) sh.c));
    };
    auto neutral = [] (TerrainAudioProcessor& p) { p.setPressureShape (0.f, 0.f, 1.f, false); };
    // the settled source for all 128 values at once: poly key k gets pressure k (MPE off)
    auto settle128 = [&] (const Shape& sh, std::array<float, 128>& got) {
        auto pp = fresh(); auto& p = *pp; p.setPressureShape (sh.c, sh.s, sh.e, false);
        render (p, 80, [&] (int b, juce::MidiBuffer& m) { if (b == 2) for (int k = 0; k < 128; ++k) m.addEvent (juce::MidiMessage::aftertouchChange (1, k, k), 0); });
        for (int k = 0; k < 128; ++k) got[(size_t) k] = p.globalSrc_.polyAt[k].load();
        neutral (p);
    };
    {   // [9] the map
        bool allOk = true; std::string rows;
        for (const auto& sh : shapes)
        {
            std::array<float, 128> got {}; settle128 (sh, got);
            double err = 0; for (int v = 0; v < 128; ++v) err = std::max (err, std::abs ((double) got[(size_t) v] - law (v, sh)));
            const bool ok = err < 2e-5; allOk = allOk && ok;
            rows += fmt ("\n        %-32s max |source - law| %.1e · v 16/32/64/96/112/127 -> %5.1f %5.1f %5.1f %5.1f %5.1f %5.1f %%", sh.name, err,
                         100.0 * got[16], 100.0 * got[32], 100.0 * got[64], 100.0 * got[96], 100.0 * got[112], 100.0 * got[127]);
        }
        // the defaults are the IDENTITY bit for bit (the pre-tp109 value v/127, not a near miss)
        std::array<float, 128> lin {}; settle128 (shapes[0], lin);
        bool bit = true; for (int v = 0; v < 128; ++v) bit = bit && lin[(size_t) v] == (float) v / 127.0f;
        wc::PressureShape d; bool idn = true; for (int v = 0; v < 128; ++v) idn = idn && d.apply ((float) v / 127.0f) == (float) v / 127.0f;
        // the same map on the MPE member path (ch 2..16 pressure) and the channel-pressure path
        const Shape hs = shapes[6]; double errM = 0, errC = 0;
        {
            auto pp = fresh(); auto& p = *pp; p.setMpeOn (true, 48.0f, false); p.setPressureShape (hs.c, hs.s, hs.e, false);
            const int vals[15] = { 0, 5, 13, 20, 33, 47, 60, 71, 84, 95, 102, 110, 115, 121, 127 };
            render (p, 80, [&] (int b, juce::MidiBuffer& m) { if (b == 2) for (int c = 2; c <= 16; ++c) m.addEvent (juce::MidiMessage::channelPressureChange (c, vals[c - 2]), 0); });
            for (int c = 2; c <= 16; ++c) errM = std::max (errM, std::abs ((double) p.globalSrc_.chPress[c].load() - law (vals[c - 2], hs)));
            neutral (p);
        }
        for (int v : { 0, 12, 40, 64, 100, 114, 127 })
        {
            auto pp = fresh(); auto& p = *pp; p.setPressureShape (hs.c, hs.s, hs.e, false);
            render (p, 200, [&] (int b, juce::MidiBuffer& m) { if (b == 2) m.addEvent (juce::MidiMessage::channelPressureChange (1, v), 0); });
            errC = std::max (errC, std::abs ((double) p.globalSrc_.atChan.load() - law (v, hs)));
            neutral (p);
        }
        bar ("9 pressure map", allOk && bit && idn && errM < 1e-4 && errC < 1e-4,
             fmt ("0..127 through every setting on the poly-key source path (settled):%s\n        defaults: source == v/127 bit for bit %s, apply() identity %s · "
                  "MPE member path max err %.1e · channel pressure path max err %.1e", rows.c_str(), bit ? "yes" : "NO", idn ? "yes" : "NO", errM, errC));
    }
    {   // [10] hard: the press it takes to reach 90 %
        auto first90 = [&] (const Shape& sh, std::array<float, 128>& g) { settle128 (sh, g); for (int v = 0; v < 128; ++v) if (g[(size_t) v] >= 0.9f) return v; return 128; };
        std::array<float, 128> gl {}, gh {}, gh2 {}, gs {};
        const int L = first90 (shapes[0], gl), H = first90 (shapes[2], gh), H2 = first90 (shapes[3], gh2), S = first90 (shapes[1], gs);
        bar ("10 hard curve", H >= 100 && H2 >= 100 && L < H && S < L,
             fmt ("the lowest pressure that reaches 90 %%: Soft %d · linear %d · Hard %d · Harder %d (want Hard >= 100/127) · "
                  "a medium press (80/127): Soft %.0f %%, linear %.0f %%, Hard %.0f %%, Harder %.0f %%",
                  S, L, H, H2, 100.0 * gs[80], 100.0 * gl[80], 100.0 * gh[80], 100.0 * gh2[80]));
    }
    // audio helper as [4]: two notes on ch 1, route Aftertouch -> Level A, poly pressure `v` on key 67 at block 120
    auto polyLevel = [&] (const Shape& sh, int v, double& dB67, double& dB60) {
        auto pp = fresh(); auto& p = *pp; p.setPressureShape (sh.c, sh.s, sh.e, false);
        setP (p, ParameterIDs::SYN_OSC_A_LEVEL, 0.5f);
        p.setSynthModMatrix ("[{\"s\":231,\"d\":" + juce::String ((int) wc::ModDest::LevelA) + ",\"v\":0.5}]");
        auto x = render (p, 260, [&] (int b, juce::MidiBuffer& m) {
            if (b == 2) { m.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0); m.addEvent (juce::MidiMessage::noteOn (1, 67, (juce::uint8) 100), 0); }
            if (b == 120) m.addEvent (juce::MidiMessage::aftertouchChange (1, 67, v), 0);
        });
        const auto s0 = spectrum (x, at (50), NFFT), s1 = spectrum (x, at (150), NFFT);
        dB67 = bandDb (s1, 370, 415) - bandDb (s0, 370, 415); dB60 = bandDb (s1, 240, 285) - bandDb (s0, 240, 285);
        neutral (p);
    };
    {   // [11] the start threshold, heard
        const Shape st { "start 20 %", 0.f, 0.2f, 1.f }, none { "linear", 0.f, 0.f, 1.f };
        double r0 = 0, r0b = 0, rL = 0, rLb = 0, rUp = 0, rUpb = 0;
        polyLevel (st, 24, r0, r0b);     // 24/127 = 18.9 % < 20 %: a resting finger
        polyLevel (none, 24, rL, rLb);   // the same press with no start
        polyLevel (st, 90, rUp, rUpb);   // well past the start
        bar ("11 pressure start", std::abs (r0) < 0.01 && rL > 0.3 && rUp > 1.0 && std::abs (r0b) < 0.3 && std::abs (rUpb) < 0.3,
             fmt ("a resting press of 24/127 on key 67: start 20 %% -> %+.3f dB (nothing), no start -> %+.2f dB · 90/127 with start 20 %% -> %+.2f dB · key 60 %+.3f / %+.3f dB",
                  r0, rL, rUp, r0b, rUpb));
    }
    {   // [12] isolation under a non-neutral shape: MPE member (as [2]) and poly key (as [4])
        const Shape hs = shapes[6];
        double d67 = 0, d60 = 0; polyLevel (hs, 127, d67, d60);
        auto pp = fresh(); auto& p = *pp; p.setMpeOn (true, 48.0f, false); p.setPressureShape (hs.c, hs.s, hs.e, false);
        setP (p, ParameterIDs::SYN_OSC_A_LEVEL, 0.5f);
        p.setSynthModMatrix ("[{\"s\":" + juce::String (wc::kAftertouchSrc) + ",\"d\":" + juce::String ((int) wc::ModDest::LevelA) + ",\"v\":0.5}]");
        auto x = render (p, 260, [&] (int b, juce::MidiBuffer& m) {
            if (b == 2) { m.addEvent (juce::MidiMessage::noteOn (2, 60, (juce::uint8) 100), 0); m.addEvent (juce::MidiMessage::noteOn (3, 67, (juce::uint8) 100), 0); }
            if (b == 120) m.addEvent (juce::MidiMessage::channelPressureChange (3, 127), 0);
        });
        neutral (p);
        const auto s0 = spectrum (x, at (50), NFFT), s1 = spectrum (x, at (150), NFFT);
        const double dA = bandDb (s1, 240, 285) - bandDb (s0, 240, 285), dB = bandDb (s1, 370, 415) - bandDb (s0, 370, 415);
        bar ("12 isolation", d67 > 4.0 && std::abs (d60) < 0.3 && dB > 4.0 && std::abs (dA) < 0.3,
             fmt ("%s: poly key 67 at 127 -> 67 %+.2f dB, 60 %+.3f dB · MPE ch 3 at 127 -> 67 %+.2f dB, ch 2's 60 %+.3f dB", hs.name, d67, d60, dB, dA));
    }
    {   // [13] zipper: a slow press, one LSB every 4 blocks (42.7 ms), 0 -> 127; the largest per-block step of the source
        std::string rows; double stepLin = 0, stepHard = 0;
        const Shape zs[] = { shapes[0], shapes[2], shapes[3], { "Harder, start 20 %, ceiling 60 %", 0.8f, 0.2f, 0.6f } };
        for (int zi = 0; zi < 4; ++zi)
        {
            const auto& sh = zs[zi];
            auto pp = fresh(); auto& p = *pp; p.setPressureShape (sh.c, sh.s, sh.e, false);
            float prev = 0.f; double mx = 0, inStep = 0;
            render (p, 4 * 128 + 40, [&] (int b, juce::MidiBuffer& m) { if (b % 4 == 0 && b / 4 < 128) m.addEvent (juce::MidiMessage::aftertouchChange (1, 64, b / 4), 0); },
                    [&] (int) { const float v = p.globalSrc_.polyAt[64].load(); mx = std::max (mx, (double) std::abs (v - prev)); prev = v; });
            for (int v = 1; v < 128; ++v) inStep = std::max (inStep, law (v, sh) - law (v - 1, sh));
            neutral (p);
            if (zi == 0) stepLin = mx; else if (zi == 1) stepHard = mx;
            rows += fmt ("\n        %-32s largest 1-LSB step of the law %.2f %% -> largest per-block step of the source %.2f %%", sh.name, 100.0 * inStep, 100.0 * mx);
        }
        bar ("13 zipper", stepLin > 0 && stepHard <= 2.5 * stepLin && stepHard < 0.02,
             fmt ("512-sample blocks at 48 kHz through the existing 10 ms one-pole:%s", rows.c_str()));
    }
    {   // [14] persistence: the text MidiSettings.json holds, written by one instance and read by the next
        auto a = fresh(); a->setPressureShape (0.6f, 0.12f, 0.85f, false);
        const auto text = a->midiPrefsJson();
        neutral (*a);
        auto b = fresh(); const auto before = wc::pressureShape().load();
        b->applyMidiPrefsJson (text);
        const auto got = wc::pressureShape().load();
        const auto page = juce::JSON::parse (b->getMidiSettingsJson());
        b->applyMidiPrefsJson ("{\"pressCurve\":7,\"pressStart\":0.9,\"pressCeil\":0.2}");   // a hand-edited file is clamped
        const auto cl = wc::pressureShape().load();
        b->applyMidiPrefsJson ("{\"a4\":440}");   // a pre-tp109 file leaves the shape where it is
        const auto keep = wc::pressureShape().load();
        neutral (*b);
        const bool ok = before.neutral() && std::abs (got.curve - 0.6f) < 1e-6f && std::abs (got.start - 0.12f) < 1e-6f && std::abs (got.ceiling - 0.85f) < 1e-6f
                        && std::abs ((double) page["pressCurve"] - 0.6) < 1e-6 && std::abs ((double) page["pressCeil"] - 0.85) < 1e-6
                        && cl.curve == 1.0f && cl.start == 0.5f && std::abs (cl.ceiling - 0.6f) < 1e-6f && keep.curve == cl.curve;
        bar ("14 persist", ok, fmt ("written %s -> a new instance reads curve %.2f start %.2f ceiling %.2f; getMidiSettings shows %.2f / %.2f / %.2f; "
                                    "a hand-edited 7 / 0.9 / 0.2 is clamped to %.2f / %.2f / %.2f; an old file without them keeps them",
                                    text.removeCharacters ("\n ").toRawUTF8(), got.curve, got.start, got.ceiling,
                                    (double) page["pressCurve"], (double) page["pressStart"], (double) page["pressCeil"], cl.curve, cl.start, cl.ceiling));
    }

    printf ("expression_midi_cert: %d passed, %d failed -> %s\n", passes, fails, fails == 0 ? "PASS" : "FAIL");
    return fails == 0 ? 0 : 1;
}
