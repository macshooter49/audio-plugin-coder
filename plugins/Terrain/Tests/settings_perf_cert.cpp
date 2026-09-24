// ══════════════════════════════════════════════════════════════════════════════════════════════
//  settings_perf_cert.cpp — tp103 · SETTINGS → PERFORMANCE, MEASURED ON THE REAL PROCESSOR.
//
//  Linked against the built libTerrain_SharedCode.a (Tests/settings_perf_cert.sh), so every number is the
//  shipping processBlock. Deterministic (TERRAIN_DETERMINISTIC=1), 48 kHz, 512-sample blocks.
//
//  [S] SLEEP WHEN SILENT — two identical instances, sleep OFF (the reference) and ON; a chord into a long Hall
//      reverb (Hall, decay 0.60, mix 0.80), released at 0.3 s, 24 s rendered.
//        S1 the reverb tail really is long (reference falls 60 dB below its peak no sooner than 3.5 s after release)
//        S2 until the instance falls asleep its output is BIT-IDENTICAL to the reference (nothing cut early)
//        S3 it falls asleep, and only once the reference is below −100 dBFS and stays there to the end
//        S4 asleep, processBlock costs a fraction of the awake-but-silent tail (CPU per block, before/after)
//        S5 a note wakes it on the very block it arrives (non-silent output in that block)
//  [T] CUT TAILS WHEN THE DAW STOPS — transport playing, a chord into the reverb, released at 0.4 s, then STOP at 0.9 s (the tail still loud)
//        T1 within 20 ms of the stop the output is silent (|x| < 1e-6) and stays silent for 1 s
//        T2 the fade adds no click: the largest sample-to-sample step across the fade < 0.05
//        T3 control: with the setting OFF the same stop leaves the tail ringing
//        T4 a new note after the cut sounds (the voices and the fade-in recover)
//  [Q] QUALITY — which path the voice filter's 2× actually runs (SynthVoice::osOsLatch_), Ladder LP 24 on osc A
//        Q1 drive 0:   Eco off · Standard off · High ON       Q2 drive 0.6: Eco off · Standard ON · High ON
//        Q3 offline render with Bounce = High turns it ON at Eco; Bounce = Same keeps Eco off
//        Q4 the Distortion tier: presets may → the knob wins; may not → Eco 0 / Standard 1 / High 2; Best bounce → 3
//        Q5 CPU per block for a held 6-note chord at drive 0, Eco vs High (the price of the setting)
//
//    sh Tests/settings_perf_cert.sh     (build Terrain first)
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
static void chk (bool ok, const char* what, const std::string& d = "")
{ std::printf ("  %s  %s\n", ok ? "PASS " : "FAIL ", what); if (! d.empty()) std::printf ("        %s\n", d.c_str()); ok ? ++npass : ++nfail; std::fflush (stdout); }
static std::string fmt (const char* f, double a = 0, double b = 0, double c = 0, double d = 0)
{ char s[512]; std::snprintf (s, sizeof s, f, a, b, c, d); return s; }

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
    std::vector<float> L, R; std::vector<double> blockUs; std::vector<char> asleep;
    explicit Inst (bool reverb)
    {
        p = std::make_unique<TerrainAudioProcessor>();
        ph.sr = SR; p->setPlayHead (&ph);
        p->setPlayConfigDetails (0, 2, SR, BLK);
        p->prepareToPlay (SR, BLK);
        p->setSleepWhenSilent (false); p->setCutTailsOnStop (false); p->setQualityPrefs (1, 1, true);
        if (reverb)
        {
            setP (*p, ParameterIDs::SYN_RVB_ACTIVE, 1.f); setP (*p, ParameterIDs::SYN_RVB_POWER, 1.f);
            setP (*p, ParameterIDs::SYN_RVB_SRC_A, 1.f);  setP (*p, ParameterIDs::SYN_RVB_MIX, 0.8f);
            setP (*p, ParameterIDs::SYN_RVB_TYPE, 0.f);   setP (*p, ParameterIDs::SYN_RVB_DECAY, 0.60f);
        }
    }
    // one block; notes = {note, on(1)/off(0)} at sample 0
    void block (std::initializer_list<std::pair<int,int>> ev = {})
    {
        buf.clear(); juce::MidiBuffer m;
        for (auto e : ev) m.addEvent (e.second ? juce::MidiMessage::noteOn (1, e.first, (juce::uint8) 100) : juce::MidiMessage::noteOff (1, e.first), 0);
        const auto t0 = std::chrono::steady_clock::now();
        p->processBlock (buf, m);
        blockUs.push_back (std::chrono::duration<double, std::micro> (std::chrono::steady_clock::now() - t0).count());
        asleep.push_back (p->isSleeping() ? 1 : 0);
        ph.t += BLK;
        L.insert (L.end(), buf.getReadPointer (0), buf.getReadPointer (0) + BLK);
        R.insert (R.end(), buf.getReadPointer (1), buf.getReadPointer (1) + BLK);
    }
    float absAt (size_t i) const { return std::max (std::abs (L[i]), std::abs (R[i])); }
};

static void chordOn  (Inst& a) { a.block ({ {48,1},{55,1},{60,1},{64,1} }); }
static void chordOff (Inst& a) { a.block ({ {48,0},{55,0},{60,0},{64,0} }); }

static double meanUs (const std::vector<double>& v, size_t a, size_t b)
{ double s = 0; size_t n = 0; for (size_t i = a; i < b && i < v.size(); ++i) { s += v[i]; ++n; } return n ? s / n : 0; }

int main()
{
    juce::ScopedJuceInitialiser_GUI init;
    setenv ("TERRAIN_DETERMINISTIC", "1", 1);
    std::printf ("settings_perf_cert — Settings → Performance on the shipping processBlock\n");

    // ═══ [S] SLEEP ═══
    std::printf ("\n[S] Sleep when silent\n");
    {
        Inst ref (true), zz (true);
        zz.p->setSleepWhenSilent (true);
        const int nBlk = (int) (24.0 * SR / BLK), offBlk = (int) (0.3 * SR / BLK);
        for (int b = 0; b < nBlk; ++b)
        {
            if (b == 0) { chordOn (ref); chordOn (zz); continue; }
            if (b == offBlk) { chordOff (ref); chordOff (zz); continue; }
            ref.block(); zz.block();
        }
        const size_t N = ref.L.size(), rel = (size_t) offBlk * BLK;
        float pk = 0; for (size_t i = 0; i < N; ++i) pk = std::max (pk, ref.absAt (i));
        // S1 — the -60 dB point of the reference after release (a running 10 ms peak window)
        size_t t60 = N;
        for (size_t i = N; i-- > rel;) if (ref.absAt (i) > pk * 1e-3f) { t60 = i; break; }
        const double tail60 = (double) (t60 - rel) / SR;
        chk (tail60 >= 3.5, "S1 the reverb tail is long: the reference stays within 60 dB of its peak until",
             fmt ("%.2f s after the release (peak %.3f)", tail60, pk));
        // S2 / S3
        int sleepBlk = -1; for (size_t b = 0; b < zz.asleep.size(); ++b) if (zz.asleep[b]) { sleepBlk = (int) b; break; }
        chk (sleepBlk > 0, "S3a it falls asleep", sleepBlk > 0 ? fmt ("at %.2f s (%.2f s after the release)", sleepBlk * BLK / SR, sleepBlk * BLK / SR - 0.3) : "never");
        size_t firstDiff = N; for (size_t i = 0; i < N; ++i) if (std::memcmp (&ref.L[i], &zz.L[i], 4) != 0 || std::memcmp (&ref.R[i], &zz.R[i], 4) != 0) { firstDiff = i; break; }
        const size_t sleepSample = sleepBlk > 0 ? (size_t) (sleepBlk + 1) * BLK : N;   // the block AFTER the one that decided
        chk (firstDiff >= sleepSample, "S2 bit-identical to the reference until asleep (no tail cut early)",
             fmt ("first differing sample %.0f (%.3f s); asleep from sample %.0f", (double) firstDiff, firstDiff / SR, (double) sleepSample));
        float after = 0; for (size_t i = sleepSample; i < N; ++i) after = std::max (after, ref.absAt (i));
        chk (after < 1e-5f, "S3b what sleep dropped was below -100 dBFS in the reference, to the end",
             fmt ("reference peak after the sleep point: %.1f dBFS", 20.0 * std::log10 (std::max (1e-12f, after))));
        // S4 — CPU: the awake silent tail (the second before sleep) vs asleep (the last 3 s)
        const size_t b1 = (size_t) sleepBlk;
        const double awakeUs = meanUs (zz.blockUs, b1 > 94 ? b1 - 94 : 0, b1);
        const double sleepUs = meanUs (zz.blockUs, zz.blockUs.size() - 280, zz.blockUs.size());
        const double refUs   = meanUs (ref.blockUs, ref.blockUs.size() - 280, ref.blockUs.size());
        const double budget = 1e6 * BLK / SR;
        chk (sleepUs < awakeUs * 0.25, "S4 asleep, a block costs under a quarter of the awake silent tail",
             fmt ("awake-silent %.1f us/block (%.2f %% of real time) -> asleep %.2f us/block (%.3f %%)", awakeUs, 100 * awakeUs / budget, sleepUs, 100 * sleepUs / budget)
             + fmt ("   [reference, never asleep, same seconds: %.1f us/block]", refUs));
        // S5 — wake on a note
        zz.block ({ {60,1} });
        float wk = 0; for (size_t i = zz.L.size() - BLK; i < zz.L.size(); ++i) wk = std::max (wk, zz.absAt (i));
        chk (! zz.p->isSleeping() && wk > 1e-4f, "S5 a note wakes it on the block it arrives", fmt ("block peak %.4f", wk));
    }

    {   // S6 — a note GENERATOR is never slept through: an Arp on the chain (its next step has no MIDI to wake us)
        Inst a (false); a.p->setSleepWhenSilent (true);
        setP (*a.p, ParameterIDs::FLOW_CHAIN_1, 1.f);   // Arp
        for (int b = 0; b < (int) (4.0 * SR / BLK); ++b) a.block();
        int slept = 0; for (char c : a.asleep) slept += c;
        Inst c (false); c.p->setSleepWhenSilent (true);
        for (int b = 0; b < (int) (4.0 * SR / BLK); ++b) c.block();
        int sleptC = 0; for (char x : c.asleep) sleptC += x;
        chk (slept == 0 && sleptC > 0, "S6 an Arp on the chain keeps it awake through 4 s of silence (control: without it, it sleeps)",
             fmt ("blocks asleep: with Arp %.0f · without %.0f", slept, sleptC));
    }

    // ═══ [T] CUT TAILS ═══
    std::printf ("\n[T] Cut tails when the DAW stops\n");
    auto runStop = [] (bool cut, Inst& a)
    {
        a.p->setCutTailsOnStop (cut);
        a.ph.playing = true;
        const int nBlk = (int) (3.0 * SR / BLK), offBlk = (int) (0.4 * SR / BLK), stopBlk = (int) (0.9 * SR / BLK);
        for (int b = 0; b < nBlk; ++b)
        {
            if (b == stopBlk) a.ph.playing = false;
            if (b == 0) chordOn (a); else if (b == offBlk) chordOff (a); else a.block();
        }
        return (size_t) stopBlk * BLK;
    };
    {
        Inst on (true), off (true);
        const size_t s0 = runStop (true, on); runStop (false, off);
        const size_t n20 = (size_t) (0.020 * SR), n1s = (size_t) SR;
        float pre = 0; for (size_t i = s0 - 480; i < s0; ++i) pre = std::max (pre, on.absAt (i));
        float post = 0; for (size_t i = s0 + n20; i < std::min (on.L.size(), s0 + n1s); ++i) post = std::max (post, on.absAt (i));
        size_t zeroAt = on.L.size(); for (size_t i = s0; i < on.L.size(); ++i) if (on.absAt (i) < 1e-6f) { bool rest = true; for (size_t k = i; k < std::min (on.L.size(), i + 4800); ++k) if (on.absAt (k) >= 1e-6f) { rest = false; break; } if (rest) { zeroAt = i; break; } }
        chk (post < 1e-6f && zeroAt - s0 <= n20, "T1 silent within 20 ms of the stop, and silent for the next second",
             fmt ("level before the stop %.4f; silent from %.2f ms after it; max after 20 ms %.2e", pre, (zeroAt - s0) * 1000.0 / SR, post));
        float stepFade = 0, stepSig = 0;
        for (size_t i = s0 + 1; i < s0 + n20; ++i) stepFade = std::max (stepFade, std::max (std::abs (on.L[i] - on.L[i-1]), std::abs (on.R[i] - on.R[i-1])));
        for (size_t i = s0 - 480; i < s0; ++i)      stepSig  = std::max (stepSig,  std::max (std::abs (on.L[i] - on.L[i-1]), std::abs (on.R[i] - on.R[i-1])));
        chk (stepFade < 0.05f && stepFade <= stepSig * 1.05f + 1e-6f, "T2 no click: the largest step across the fade < 0.05 and no bigger than the signal's own",
             fmt ("fade max step %.5f · the tail's own max step in the 10 ms before %.5f", stepFade, stepSig));
        float offPost = 0; for (size_t i = s0 + n20; i < s0 + n1s; ++i) offPost = std::max (offPost, off.absAt (i));
        chk (offPost > 1e-3f, "T3 control: with it OFF the tail keeps ringing after the stop", fmt ("peak 20 ms..1 s after the stop: %.4f", offPost));
        on.ph.playing = true; on.block ({ {60,1} }); for (int k = 0; k < 8; ++k) on.block();
        float again = 0; for (size_t i = on.L.size() - 9 * BLK; i < on.L.size(); ++i) again = std::max (again, on.absAt (i));
        chk (again > 1e-3f, "T4 a note after the cut sounds (voices + fade-in recovered)", fmt ("peak %.4f", again));
    }

    // ═══ [Q] QUALITY ═══
    std::printf ("\n[Q] Playing / Bounce quality — the voice filter's 2x actually used\n");
    auto latched = [] (int rt, int off, bool offline, float drive, double* usOut)
    {
        Inst a (false);
        setP (*a.p, ParameterIDs::SYN_FILTER1_TYPE, 0.f);     // Ladder LP 24 (a drive-able type)
        setP (*a.p, ParameterIDs::SYN_FILTER1_SRC_A, 1.f);
        setP (*a.p, ParameterIDs::SYN_FILTER1_DRV, drive);
        a.p->setQualityPrefs (rt, off, true);
        a.p->setNonRealtime (offline);
        a.block(); a.block();
        a.block ({ {48,1},{52,1},{55,1},{60,1},{64,1},{67,1} });
        for (int k = 0; k < 40; ++k) a.block();
        int active = 0, os = 0;
        for (auto* v : a.p->synthVoices_) if (v != nullptr && v->isVoiceActive()) { ++active; if (v->osOsLatch_) ++os; }
        if (usOut) *usOut = meanUs (a.blockUs, 10, a.blockUs.size());
        return std::make_pair (os, active);
    };
    double usEco = 0, usHigh = 0, usStd = 0;
    const auto e0 = latched (0, 0, false, 0.f, &usEco), s0 = latched (1, 0, false, 0.f, &usStd), h0 = latched (2, 0, false, 0.f, &usHigh);
    chk (e0.first == 0 && s0.first == 0 && h0.first == h0.second && h0.second > 0, "Q1 drive 0: Eco off · Standard off · High on every voice",
         fmt ("oversampled voices: Eco %.0f/%.0f · Standard %.0f/%.0f", e0.first, e0.second, s0.first, s0.second) + fmt (" · High %.0f/%.0f", h0.first, h0.second));
    const auto e6 = latched (0, 0, false, 0.6f, nullptr), s6 = latched (1, 0, false, 0.6f, nullptr), h6 = latched (2, 0, false, 0.6f, nullptr);
    chk (e6.first == 0 && s6.first == s6.second && h6.first == h6.second && s6.second > 0, "Q2 drive 0.6: Eco off · Standard on · High on",
         fmt ("Eco %.0f/%.0f · Standard %.0f/%.0f", e6.first, e6.second, s6.first, s6.second) + fmt (" · High %.0f/%.0f", h6.first, h6.second));
    const auto oh = latched (0, 1, true, 0.f, nullptr), os_ = latched (0, 0, true, 0.f, nullptr);
    chk (oh.first == oh.second && oh.second > 0 && os_.first == 0, "Q3 offline: Bounce High turns it on under Eco; Bounce Same keeps Eco off",
         fmt ("Bounce High %.0f/%.0f · Bounce Same %.0f/%.0f", oh.first, oh.second, os_.first, os_.second));
    {
        Inst a (false);
        auto dq = [&a] (int rt, int off, bool may, bool offline, int knob) { a.p->setQualityPrefs (rt, off, may); a.p->setNonRealtime (offline); return a.p->effectiveDistQuality (knob); };
        const bool ok = dq (0, 0, true, false, 3) == 3 && dq (2, 0, true, false, 0) == 0
                     && dq (0, 0, false, false, 3) == 0 && dq (1, 0, false, false, 3) == 1 && dq (2, 0, false, false, 0) == 2
                     && dq (0, 1, true, true, 0) == 2 && dq (0, 2, false, true, 0) == 3 && dq (2, 0, false, true, 1) == 2;
        chk (ok, "Q4 the Distortion tier follows the lock, the playing quality and the bounce floor");
        a.p->setNonRealtime (false);
    }
    chk (usHigh > usEco, "Q5 the price: a held 6-note chord through a Ladder at drive 0",
         fmt ("Eco %.1f us/block · Standard %.1f · High %.1f  (High costs %.0f %% more than Eco)", usEco, usStd, usHigh, 100.0 * (usHigh / std::max (1e-9, usEco) - 1.0)));

    // ═══ [B] THE STANDALONE'S TEMPO ═══
    //  The Terrain app has no DAW: prepareToPlay installs standaloneHead_ (its BPM = Settings → Tempo). Every tempo
    //  reader goes through the playhead: currentBPM (→ synModBpm: LFO sync, the Arp, grain sync) and flowBpm (Chop /
    //  Shaper / Glitch). The rack Delay's synced time is the audible proof: 1/4 note = 60000 / BPM ms.
    std::printf ("\n[B] Tempo in the standalone (Settings → Audio & MIDI → Tempo)\n");
    {
        juce::AudioProcessor::setTypeOfNextNewPlugin (juce::AudioProcessor::wrapperType_Standalone);
        auto sp = std::make_unique<TerrainAudioProcessor>();
        juce::AudioProcessor::setTypeOfNextNewPlugin (juce::AudioProcessor::wrapperType_Undefined);
        sp->setPlayConfigDetails (0, 2, SR, BLK); sp->prepareToPlay (SR, BLK);
        setP (*sp, ParameterIDs::SYN_DLY_ACTIVE, 1.f); setP (*sp, ParameterIDs::SYN_DLY_POWER, 1.f); setP (*sp, ParameterIDs::SYN_DLY_SRC_A, 1.f);
        setP (*sp, ParameterIDs::SYN_DLY_SYNC, 1.f);   setP (*sp, ParameterIDs::SYN_DLY_SYNCDIV, 7.f);   // 1/4
        juce::AudioBuffer<float> b (2, BLK);
        auto run = [&] (double bpm)
        {
            sp->setStandaloneBpm (bpm);
            for (int k = 0; k < 6; ++k) { b.clear(); juce::MidiBuffer m; if (k == 0) m.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0); sp->processBlock (b, m); }
            return std::make_pair ((double) sp->currentBPM.load(), (double) sp->delayEngine.timeMs_);
        };
        const bool head = sp->getPlayHead() == &sp->standaloneHead_;
        const auto a = run (90.0), c = run (140.0);
        chk (head && std::abs (a.first - 90.0) < 1e-3 && std::abs (c.first - 140.0) < 1e-3,
             "B1 the app's playhead carries the Tempo setting to every BPM reader (currentBPM)", fmt ("90 -> %.2f · 140 -> %.2f", a.first, c.first));
        chk (std::abs (a.second - 60000.0 / 90.0) < 0.5 && std::abs (c.second - 60000.0 / 140.0) < 0.5,
             "B2 a synced 1/4 Delay follows it", fmt ("at 90 BPM %.1f ms (want %.1f) · at 140 BPM %.1f ms (want %.1f)", a.second, 60000.0 / 90.0, c.second, 60000.0 / 140.0));
    }

    std::printf ("\nsettings_perf_cert: %d pass, %d fail — %s\n", npass, nfail, nfail ? "FAIL" : "PASS");
    return nfail ? 1 : 0;
}
