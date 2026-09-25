// ══════════════════════════════════════════════════════════════════════════════════════════════
//  tp107 — SOLO / MUTE ON ALL EIGHT OSCILLATORS, measured on the real processor.
//
//    Tests/osc_solo_mute_cert.sh          (build Terrain first; links libTerrain_SharedCode.a; sets
//                                          TERRAIN_ORGANICS_DIR = Tests/fixtures/organics for the Organics row)
//
//  Max: "solo doesn't work on EFGH oscillators on the Patcher, please make sure we can solo, and mute everything."
//  ROOT CAUSE (4075f80c): the processor gathers each voice bank separately (bank 0 = A–D, bank 1 = E–H, tp20) and
//  computed `anySolo` from ITS OWN four SOLO params. Soloing E silenced F–H only (A–D never heard of it), and soloing A
//  left all of E–H playing. The fix: one anySolo over all eight SOLO params, read once per block, used by both banks.
//
//  All eight oscillators play one note (57 = 220 Hz) at eight different pitches (semitone offsets 0 1 3 4 6 7 9 11 — all
//  inside one octave, so no oscillator's harmonics land on another's fundamental). Goertzel (Hann, 1 s) at each pitch:
//    [1] per engine (WT · FM · HARM · MODAL · ORGANIC): solo each of A–H in turn → its pitch stays (within 2 dB of the
//        all-playing level) and EVERY other pitch falls ≥ 30 dB.
//    [2] per engine: mute each of A–H in turn → its pitch falls ≥ 30 dB, every other stays within 2 dB.
//    [3] multiple solos combine: B + G soloed → only B and G; A + E + H → only those three.
//    [4] un-soloing restores all eight (within 1 dB of before).
//    [5] a solo toggled under a held note is click-free (HP 8 kHz residual ≤ −60 dB re the signal).
//    [6] mute + solo persist: state save → a NEW instance → load → the same params and the same silence.
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

static constexpr double SR = 48000.0; static constexpr int BLK = 512;
static constexpr int kSemi[8] = { 0, 1, 3, 4, 6, 7, 9, 11 };
static constexpr int kNote = 57;
static double freqOf (int o) { return 440.0 * std::pow (2.0, (kNote - 69 + kSemi[o]) / 12.0); }

static void setP (TerrainAudioProcessor& p, const char* id, float plain)
{
    auto* prm = p.apvts.getParameter (id);
    if (prm == nullptr) { std::printf ("!! no param %s\n", id); std::exit (2); }
    prm->setValueNotifyingHost (prm->convertTo0to1 (plain));
}
static float getP (TerrainAudioProcessor& p, const char* id) { return p.apvts.getRawParameterValue (id)->load(); }

struct Inst
{
    std::unique_ptr<TerrainAudioProcessor> p; juce::AudioBuffer<float> buf { 2, BLK };
    std::vector<float> L, R;
    Inst()
    {
        p = std::make_unique<TerrainAudioProcessor>();
        p->setPlayConfigDetails (0, 2, SR, BLK);
        p->prepareToPlay (SR, BLK);
    }
    void block (std::initializer_list<std::pair<int,int>> ev = {})
    {
        buf.clear(); juce::MidiBuffer m;
        for (auto e : ev) m.addEvent (e.second ? juce::MidiMessage::noteOn (1, e.first, (juce::uint8) 100) : juce::MidiMessage::noteOff (1, e.first), 0);
        p->processBlock (buf, m);
        L.insert (L.end(), buf.getReadPointer (0), buf.getReadPointer (0) + BLK);
        R.insert (R.end(), buf.getReadPointer (1), buf.getReadPointer (1) + BLK);
    }
    void tick (int n = 1) { for (int i = 0; i < n; ++i) { CFRunLoopRunInMode (kCFRunLoopDefaultMode, 0.002, false); p->timerCallback(); } }
    void run (double sec) { const int n = (int) std::ceil (sec * SR / BLK); for (int b = 0; b < n; ++b) { block(); if (b & 1) tick(); } }
    juce::String waitLoaded (int osc, double maxSec = 5.0)
    {
        const double t0 = juce::Time::getMillisecondCounterHiRes();
        while (juce::Time::getMillisecondCounterHiRes() - t0 < maxSec * 1000.0)
        {
            tick(); block();
            const auto st = p->organicSlot (osc).status;
            if (st != "loading" && ! p->orgSlot_[osc].publishPending && ! (p->orgMailState_[osc].load() != 0)) { block(); return st; }
        }
        return p->organicSlot (osc).status;
    }
    void clear() { L.clear(); R.clear(); }
};

static double dbOf (double r) { return 20.0 * std::log10 (std::max (1e-12, r)); }
static double tone (const std::vector<float>& x, size_t a, size_t n, double f)
{
    if (a + n > x.size()) return -200;
    const double w = 2.0 * juce::MathConstants<double>::pi * f / SR; double re = 0, im = 0;
    for (size_t i = 0; i < n; ++i) { const double h = 0.5 - 0.5 * std::cos (2.0 * juce::MathConstants<double>::pi * (double) i / (double) (n - 1)); re += x[a + i] * h * std::cos (w * (double) i); im -= x[a + i] * h * std::sin (w * (double) i); }
    return dbOf (2.0 * std::sqrt (re * re + im * im) / (0.5 * (double) n));
}

/** all eight on, engine e, eight pitches; returns the ready instance (note not yet played) */
static std::unique_ptr<Inst> rig (int engine, const char* orgId = "test.sine")
{
    auto a = std::make_unique<Inst>();
    for (int o = 0; o < 8; ++o)
    {
        setP (*a->p, ParameterIDs::kOsc_ENABLE[o], 1.f);
        setP (*a->p, ParameterIDs::kOsc_LEVEL[o], 0.5f);
        setP (*a->p, ParameterIDs::kOsc_SEMI[o], (float) kSemi[o]);
        setP (*a->p, ParameterIDs::kOsc_ENGINE[o], (float) engine);
    }
    a->tick (6);                                 // bank B + the lazy arms (message thread)
    if (engine == 7)
        for (int o = 0; o < 8; ++o) { a->p->organicsSetInstrument (o, orgId); a->waitLoaded (o); }
    a->run (0.05);
    return a;
}

/** levels (dB) at the eight pitches over [0.4 s, 1.4 s) of a held note, with the given solo/mute masks */
static std::array<double, 8> measure (Inst& a, unsigned soloMask, unsigned muteMask)
{
    for (int o = 0; o < 8; ++o)
    {
        setP (*a.p, ParameterIDs::kOsc_SOLO[o], (soloMask >> o) & 1u ? 1.f : 0.f);
        setP (*a.p, ParameterIDs::kOsc_MUTE[o], (muteMask >> o) & 1u ? 1.f : 0.f);
    }
    a.run (0.05);
    a.clear(); a.block ({ { kNote, 1 } }); a.run (1.45);
    std::array<double, 8> lv {};
    std::vector<float> m (a.L.size()); for (size_t i = 0; i < m.size(); ++i) m[i] = 0.5f * (a.L[i] + a.R[i]);
    for (int o = 0; o < 8; ++o) lv[(size_t) o] = tone (m, (size_t) (0.4 * SR), (size_t) SR, freqOf (o));
    a.block ({ { kNote, 0 } }); a.run (1.5);   // let it go (long releases: the next measure starts from silence)
    return lv;
}

static std::string row (const std::array<double, 8>& v)
{ std::string s; for (int o = 0; o < 8; ++o) { char b[32]; std::snprintf (b, sizeof b, "%c %.0f ", 'A' + o, v[(size_t) o]); s += b; } return s + "dB"; }

int main()
{
    juce::ScopedJuceInitialiser_GUI gui;
    std::printf ("osc_solo_mute_cert — tp107 solo / mute on A–H\n");
    const struct { int e; const char* name; } engines[] = { { 0, "WT" }, { 4, "FM" }, { 5, "HARM" }, { 6, "MODAL" }, { 7, "ORGANIC" } };
    for (const auto& E : engines)
    {
        std::printf ("\n[%s]\n", E.name);
        auto a = rig (E.e);
        const auto all = measure (*a, 0, 0);
        std::printf ("        all playing: %s\n", row (all).c_str());
        // FM's operators start at a random phase every note, so its fundamental swings ±8 dB press to press: the "kept" / "untouched"
        // tolerance is 10 dB there (2 dB elsewhere). MODAL's modes thin out up the octave (H at −88 dBFS) — audible = over −100.
        const double tol = E.e == 4 ? 10.0 : 2.0;
        bool audible = true; for (double v : all) audible &= v > -100.0;
        chk (audible, std::string ("0 ") + E.name + ": all eight pitches sound with no solo / mute", row (all));
        // [1] solo each
        std::string bad1, rows1; double worstKeep = 0, worstLeak = -300;
        for (int x = 0; x < 8; ++x)
        {
            const auto s = measure (*a, 1u << x, 0);
            for (int o = 0; o < 8; ++o)
            {
                const double d = s[(size_t) o] - all[(size_t) o];
                if (o == x) { worstKeep = std::max (worstKeep, std::abs (d)); if (std::abs (d) > tol) bad1 += std::string (1, (char) ('A' + x)) + " "; }
                else        { worstLeak = std::max (worstLeak, d);          if (d > -30.0) bad1 += std::string (1, (char) ('A' + x)) + "→" + (char) ('A' + o) + " "; }
            }
            rows1 += std::string (1, (char) ('A' + x)) + ": " + row (s) + "\n        ";
        }
        chk (bad1.empty(), std::string ("1 ") + E.name + ": solo each of A–H → only its pitch (kept within 2 dB — FM 10 —, every other ≥ 30 dB down)",
             std::string ("worst kept Δ ") + std::to_string (worstKeep).substr (0, 5) + " dB · worst other Δ " + std::to_string (worstLeak).substr (0, 6) + " dB"
             + (bad1.empty() ? "" : " · FAILS " + bad1) + "\n        " + rows1);
        // [2] mute each
        std::string bad2; double worstMute = -200, worstOther = 0;
        for (int x = 0; x < 8; ++x)
        {
            const auto s = measure (*a, 0, 1u << x);
            for (int o = 0; o < 8; ++o)
            {
                const double d = s[(size_t) o] - all[(size_t) o];
                if (o == x) { worstMute = std::max (worstMute, d); if (d > -30.0) bad2 += std::string (1, (char) ('A' + x)) + " "; }
                else        { worstOther = std::max (worstOther, std::abs (d)); if (std::abs (d) > tol) bad2 += std::string (1, (char) ('A' + x)) + "→" + (char) ('A' + o) + " "; }
            }
        }
        chk (bad2.empty(), std::string ("2 ") + E.name + ": mute each of A–H → its pitch ≥ 30 dB down, the other seven untouched (±2 dB — FM ±10)",
             "worst muted Δ " + std::to_string (worstMute).substr (0, 6) + " dB · worst other |Δ| " + std::to_string (worstOther).substr (0, 5) + " dB" + (bad2.empty() ? "" : " · FAILS " + bad2));
        if (E.e != 0) continue;
        // [3] multiple solos combine
        {
            auto chkSet = [&] (unsigned mask, const char* label) {
                const auto s = measure (*a, mask, 0); bool ok = true;
                for (int o = 0; o < 8; ++o) { const double d = s[(size_t) o] - all[(size_t) o]; ok &= ((mask >> o) & 1u) ? std::abs (d) <= 2.0 : d <= -30.0; }
                chk (ok, std::string ("3 multiple solos combine: ") + label + " soloed → only those sound", row (s));
            };
            chkSet ((1u << 1) | (1u << 6), "B + G");
            chkSet ((1u << 0) | (1u << 4) | (1u << 7), "A + E + H");
            chkSet ((1u << 5) | (1u << 6), "F + G (bank B only)");
        }
        // [4] un-solo restores
        {
            measure (*a, 1u << 5, 0);
            const auto s = measure (*a, 0, 0); double w = 0;
            for (int o = 0; o < 8; ++o) w = std::max (w, std::abs (s[(size_t) o] - all[(size_t) o]));
            chk (w <= 1.0, "4 un-soloing restores all eight", row (s) + " · worst |Δ| " + std::to_string (w).substr (0, 5) + " dB");
        }
        // [5] click-free solo toggle under a held note (solo E, then solo A, then none) — HP 8 kHz residual
        {
            for (int o = 0; o < 8; ++o) { setP (*a->p, ParameterIDs::kOsc_SOLO[o], 0.f); setP (*a->p, ParameterIDs::kOsc_MUTE[o], 0.f); }
            a->run (0.05); a->clear(); a->block ({ { kNote, 1 } }); a->run (0.5);
            setP (*a->p, ParameterIDs::kOsc_SOLO[4], 1.f); a->run (0.3);
            setP (*a->p, ParameterIDs::kOsc_SOLO[0], 1.f); setP (*a->p, ParameterIDs::kOsc_SOLO[4], 0.f); a->run (0.3);
            setP (*a->p, ParameterIDs::kOsc_MUTE[6], 1.f); setP (*a->p, ParameterIDs::kOsc_SOLO[0], 0.f); a->run (0.3);
            std::vector<double> h (a->L.begin(), a->L.end());
            for (double q : { 0.5411961, 1.3065630 })
            {
                const double w0 = 2 * juce::MathConstants<double>::pi * 8000.0 / SR, al = std::sin (w0) / (2 * q), c = std::cos (w0);
                const double b0 = (1 + c) / 2, b1 = -(1 + c), b2 = (1 + c) / 2, a0 = 1 + al, a1 = -2 * c, a2 = 1 - al;
                double x1 = 0, x2 = 0, y1 = 0, y2 = 0;
                for (auto& v : h) { const double in = v, o = (b0 * in + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2) / a0; x2 = x1; x1 = in; y2 = y1; y1 = o; v = o; }
            }
            double hp = 0, sig = 0; const size_t s0 = (size_t) (0.4 * SR);
            for (size_t i = s0; i < h.size(); ++i) { hp = std::max (hp, std::abs (h[i])); sig = std::max (sig, (double) std::abs (a->L[i])); }
            chk (dbOf (hp / std::max (1e-12, sig)) <= -60.0, "5 solo / mute toggled under a held note (E, A+¬E, ¬A+mute G): click-free",
                 "HP(8 kHz) residual peak " + std::to_string (dbOf (hp / std::max (1e-12, sig))).substr (0, 6) + " dB re the signal peak");
            a->block ({ { kNote, 0 } }); a->run (1.0);
        }
        // [6] persistence: mute E + G, solo F → state → a new instance
        {
            for (int o = 0; o < 8; ++o) { setP (*a->p, ParameterIDs::kOsc_SOLO[o], 0.f); setP (*a->p, ParameterIDs::kOsc_MUTE[o], 0.f); }
            setP (*a->p, ParameterIDs::kOsc_MUTE[4], 1.f); setP (*a->p, ParameterIDs::kOsc_MUTE[6], 1.f); setP (*a->p, ParameterIDs::kOsc_MUTE[1], 1.f);
            a->run (0.05);
            juce::MemoryBlock blob; a->p->getStateInformation (blob);
            Inst b; b.p->setStateInformation (blob.getData(), (int) blob.getSize()); b.tick (6); b.run (0.05);
            const bool params = getP (*b.p, ParameterIDs::kOsc_MUTE[4]) > 0.5f && getP (*b.p, ParameterIDs::kOsc_MUTE[6]) > 0.5f
                             && getP (*b.p, ParameterIDs::kOsc_MUTE[1]) > 0.5f && getP (*b.p, ParameterIDs::kOsc_MUTE[0]) < 0.5f;
            b.clear(); b.block ({ { kNote, 1 } }); b.run (1.45);
            std::vector<float> m (b.L.size()); for (size_t i = 0; i < m.size(); ++i) m[i] = 0.5f * (b.L[i] + b.R[i]);
            std::array<double, 8> s {}; for (int o = 0; o < 8; ++o) s[(size_t) o] = tone (m, (size_t) (0.4 * SR), (size_t) SR, freqOf (o));
            bool ok = params;
            for (int o = 0; o < 8; ++o) { const double d = s[(size_t) o] - all[(size_t) o]; ok &= (o == 1 || o == 4 || o == 6) ? d <= -30.0 : std::abs (d) <= 2.0; }
            chk (ok, "6 mute B + E + G persists: state → a new instance → the same params and the same silence", std::string (params ? "params restored · " : "PARAMS LOST · ") + row (s));
            // and a solo on E–H persists too
            setP (*a->p, ParameterIDs::kOsc_MUTE[4], 0.f); setP (*a->p, ParameterIDs::kOsc_MUTE[6], 0.f); setP (*a->p, ParameterIDs::kOsc_MUTE[1], 0.f);
            setP (*a->p, ParameterIDs::kOsc_SOLO[7], 1.f); a->run (0.05);
            juce::MemoryBlock blob2; a->p->getStateInformation (blob2);
            Inst c; c.p->setStateInformation (blob2.getData(), (int) blob2.getSize()); c.tick (6); c.run (0.05);
            c.clear(); c.block ({ { kNote, 1 } }); c.run (1.45);
            std::vector<float> m2 (c.L.size()); for (size_t i = 0; i < m2.size(); ++i) m2[i] = 0.5f * (c.L[i] + c.R[i]);
            bool ok2 = getP (*c.p, ParameterIDs::kOsc_SOLO[7]) > 0.5f;
            std::array<double, 8> s2 {}; for (int o = 0; o < 8; ++o) { s2[(size_t) o] = tone (m2, (size_t) (0.4 * SR), (size_t) SR, freqOf (o)); const double d = s2[(size_t) o] - all[(size_t) o]; ok2 &= o == 7 ? std::abs (d) <= 2.0 : d <= -30.0; }
            chk (ok2, "6 solo H persists: state → a new instance → only H sounds", row (s2));
        }
    }
    std::printf ("\nosc_solo_mute_cert: %d PASS · %d FAIL\n", npass, nfail);
    return nfail ? 1 : 0;
}
