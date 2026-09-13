// ══════════════════════════════════════════════════════════════════════════════════════════════
//  fb636e — FUNCTIONAL proof that the Terra capture holds what the host hears, FLOW stages included.
//
//    plugins/Terrain/Tests/capture_flow_render.sh          (build Terrain_VST3/AU first — it links the SharedCode)
//    CAPFLOW_OFF=1 → the FLOW-off reference run
//
//  Links the real processor (libTerrain_SharedCode.a), arms the Export ring exactly as an editor does,
//  plays a held chord through the FLOW chain Arp -> Chop -> Glitch -> Robin (Glitch chance 100 %), and
//  requires the ring's last N samples to equal the N samples the host received — BIT FOR BIT — while
//  Glitch has really fired and Chop has really been active (so a pre-FLOW tap could NOT pass).
//  capture_last_gate.py pins the ORDER in the source (with a mutation control); this proves the RESULT.
//  First run (2026-09-12, 38f5fb4): Glitch fired 63x, active 739/750 blocks, Chop 750/750, 0 of 384,000 differ.
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

struct PH : juce::AudioPlayHead
{
    double sr = 48000.0, bpm = 120.0; int64_t t = 0;
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo p; p.setBpm (bpm); p.setIsPlaying (true); p.setTimeInSamples (t);
        p.setPpqPosition ((double) t / sr * bpm / 60.0); p.setTimeSignature (juce::AudioPlayHead::TimeSignature {});
        return p;
    }
};

static void setP (TerrainAudioProcessor& p, const char* id, float plain)
{
    auto* prm = p.apvts.getParameter (id);
    if (prm == nullptr) { printf ("!! no param %s\n", id); std::exit (2); }
    prm->setValueNotifyingHost (prm->convertTo0to1 (plain));
}

int main()
{
    juce::ScopedJuceInitialiser_GUI init;
    const double SR = 48000.0; const int BLK = 512; const int NBLK = (int) (8.0 * SR / BLK);
    auto proc = std::make_unique<TerrainAudioProcessor>();
    auto& p = *proc;
    PH ph; ph.sr = SR; p.setPlayHead (&ph);
    p.setPlayConfigDetails (0, 2, SR, BLK);
    p.prepareToPlay (SR, BLK);
    { const std::lock_guard<std::mutex> g (p.prepLock_); p.ensureCaptureBufferAllocated(); }
    if (! p.captureBuffer.isAllocated()) { printf ("!! capture ring not armed\n"); return 2; }

    const bool flowOff = std::getenv ("CAPFLOW_OFF") != nullptr;   // reference run: FLOW off
    if (! flowOff)
    {
        setP (p, ParameterIDs::FLOW_CHAIN_1, 1.f);   // Arp
        setP (p, ParameterIDs::FLOW_CHAIN_2, 2.f);   // Chop
        setP (p, ParameterIDs::FLOW_CHAIN_3, 3.f);   // Glitch
        setP (p, ParameterIDs::FLOW_CHAIN_4, 4.f);   // Robin
        setP (p, ParameterIDs::FLOW_GLI_VARY, 1.f);  // fire chance 100 %
        setP (p, ParameterIDs::FLOW_GLI_BLEND, 1.f);
        setP (p, ParameterIDs::FLOW_CHOP_BLEND, 1.f);
    }

    std::vector<float> outL, outR; outL.reserve ((size_t) (NBLK * BLK)); outR.reserve ((size_t) (NBLK * BLK));
    juce::AudioBuffer<float> buf (2, BLK);
    int gliFiredMax = 0, gliActiveBlocks = 0, chopActiveBlocks = 0;
    for (int b = 0; b < NBLK; ++b)
    {
        buf.clear();
        juce::MidiBuffer midi;
        if (b == 0) for (int n : { 48, 55, 60, 64, 67 }) midi.addEvent (juce::MidiMessage::noteOn (1, n, (juce::uint8) 110), 0);
        if (b == NBLK - 60) for (int n : { 48, 55, 60, 64, 67 }) midi.addEvent (juce::MidiMessage::noteOff (1, n), 0);
        p.processBlock (buf, midi);
        ph.t += BLK;
        outL.insert (outL.end(), buf.getReadPointer (0), buf.getReadPointer (0) + BLK);
        outR.insert (outR.end(), buf.getReadPointer (1), buf.getReadPointer (1) + BLK);
        gliFiredMax = std::max (gliFiredMax, (int) p.glitch.vizFireCount());
        if (p.glitch.isActive()) ++gliActiveBlocks;
        if (p.chop.isActive())   ++chopActiveBlocks;
    }
    const int N = (int) outL.size();
    std::vector<float> cL ((size_t) N), cR ((size_t) N);
    const int got = p.captureBuffer.copyForExport (cL.data(), cR.data(), ((double) N + 0.5) / SR);

    double rms = 0; for (int i = 0; i < N; ++i) rms += (double) outL[(size_t) i] * outL[(size_t) i]; rms = std::sqrt (rms / N);
    int diff = 0; float maxd = 0;
    for (int i = 0; i < got; ++i)
    {
        const size_t o = (size_t) (N - got + i);
        const float d = std::max (std::abs (cL[(size_t) i] - outL[o]), std::abs (cR[(size_t) i] - outR[o]));
        if (std::memcmp (&cL[(size_t) i], &outL[o], 4) != 0 || std::memcmp (&cR[(size_t) i], &outR[o], 4) != 0) ++diff;
        maxd = std::max (maxd, d);
    }
    printf ("  rendered %d samples (%.2f s), output RMS %.4f, ring returned %d\n", N, N / SR, rms, got);
    printf ("  Glitch fires %d, Glitch active %d/%d blocks, Chop active %d/%d blocks\n", gliFiredMax, gliActiveBlocks, NBLK, chopActiveBlocks, NBLK);
    printf ("  capture vs host output: %d of %d samples differ (max |d| %.6g)\n", diff, got, maxd);

    bool ok = got == N && rms > 1e-3 && diff == 0;
    if (! flowOff) ok = ok && gliFiredMax > 0 && gliActiveBlocks > 0 && chopActiveBlocks > 0;
    p.releaseResources();
    printf ("capture_flow_render%s: %s\n", flowOff ? " (FLOW off)" : "", ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}
