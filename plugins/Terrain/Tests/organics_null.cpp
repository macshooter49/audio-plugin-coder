// ══════════════════════════════════════════════════════════════════════════════════════════════
//  tp104 — THE ORGANICS NULL: a patch that never selects the Organics engine must render
//  BIT-IDENTICALLY before and after the engine was integrated (design §8, "Bit-identical when unused").
//
//    Tests/organics_null.sh write <dir> [bankDir]   (on the OLD build)  -> one .f32 per case + hashes.txt
//    Tests/organics_null.sh check <dir> [bankDir]   (on the NEW build)  -> exit 1 on any differing sample
//
//  The frozen-bank run (the fb636 abS.sh method, rebuilt on the SharedCode link): every .terrain in
//  the bank folder (default: the frozen 52 at ~/Library/WavesCrate/TerrainInstrument/Backups/
//  cpu-pass-before-2026-09-11/Banks/User) is loaded through TerrainAudioProcessor::loadPatchFromFile
//  into a FRESH processor, the message-thread timer is ticked at a fixed block cadence (so every lazy
//  arm the patch asks for happens at the same block in both builds), and a chord + release is rendered.
//  Plus two cases that need no bank: the init patch (Tests/pool_identity.cpp's chord, 240 blocks) and
//  the init patch with oscillators A-H all on (bank B built).
//  Deliberately uses ONLY APIs that exist before the Organics work (it is compiled against the baseline).
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

static constexpr double SR = 48000.0; static constexpr int BLK = 512;

static void setP (TerrainAudioProcessor& p, const char* id, float plain)
{
    auto* prm = p.apvts.getParameter (id);
    if (prm == nullptr) { std::printf ("!! no param %s\n", id); std::exit (2); }
    prm->setValueNotifyingHost (prm->convertTo0to1 (plain));
}

static uint64_t fnv (const std::vector<float>& v)
{
    uint64_t h = 1469598103934665603ull;
    for (float x : v) { uint32_t u; std::memcpy (&u, &x, 4); h = (h ^ u) * 1099511628211ull; }
    return h;
}

// setup: runs after prepareToPlay (a preset load or parameter changes). Returns false to skip the case.
static std::vector<float> renderCase (const std::function<bool (TerrainAudioProcessor&)>& setup, int nBlk, int offBlk, bool& ok)
{
    auto proc = std::make_unique<TerrainAudioProcessor>();
    auto& p = *proc;
    p.setPlayConfigDetails (0, 2, SR, BLK);
    p.prepareToPlay (SR, BLK);
    ok = setup (p);
    for (int t = 0; t < 3; ++t) p.timerCallback();   // the lazy arms a loaded patch asks for (bank B, modal, harm, wavetables)
    std::vector<float> out; out.reserve ((size_t) nBlk * BLK * 2);
    juce::AudioBuffer<float> buf (2, BLK);
    for (int b = 0; b < nBlk; ++b)
    {
        buf.clear(); juce::MidiBuffer m;
        if (b == 0)      for (int n : { 48, 55, 60, 64 }) m.addEvent (juce::MidiMessage::noteOn (1, n, (juce::uint8) 100), 0);
        if (b == offBlk) for (int n : { 48, 55, 60, 64 }) m.addEvent (juce::MidiMessage::noteOff (1, n), 0);
        p.processBlock (buf, m);
        if (b % 2 == 1) p.timerCallback();            // a fixed ~47 Hz message-thread cadence, identical in both builds
        for (int i = 0; i < BLK; ++i) { out.push_back (buf.getSample (0, i)); out.push_back (buf.getSample (1, i)); }
    }
    p.releaseResources();
    return out;
}

int main (int argc, char** argv)
{
    setenv ("TERRAIN_DETERMINISTIC", "1", 1);
    if (argc < 3) { std::printf ("usage: organics_null write|check <dir> [bankDir]\n"); return 2; }
    const bool write = std::strcmp (argv[1], "write") == 0;
    const juce::File dir { juce::String (argv[2]) };
    const juce::File bank (argc > 3 ? juce::String (argv[3])
                                    : juce::File::getSpecialLocation (juce::File::userHomeDirectory)
                                        .getChildFile ("Library/WavesCrate/TerrainInstrument/Backups/cpu-pass-before-2026-09-11/Banks/User").getFullPathName());
    juce::ScopedJuceInitialiser_GUI init;
    dir.createDirectory();

    struct Case { juce::String name; std::function<bool (TerrainAudioProcessor&)> setup; int nBlk, offBlk; };
    std::vector<Case> cases;
    cases.push_back ({ "_init", [] (TerrainAudioProcessor&) { return true; }, 240, 1000 });
    cases.push_back ({ "_init_AtoH", [] (TerrainAudioProcessor& p) {
        for (int o = 0; o < ParameterIDs::kOscCount; ++o) setP (p, ParameterIDs::kOsc_ENABLE[o], 1.f);
        return true; }, 200, 120 });
    auto files = bank.findChildFiles (juce::File::findFiles, false, "*.terrain"); files.sort();
    for (auto f : files)
        cases.push_back ({ f.getFileNameWithoutExtension(), [f] (TerrainAudioProcessor& p) {
            juce::String err; const bool r = p.loadPatchFromFile (f, err);
            if (! r) std::printf ("   (load failed: %s)\n", err.toRawUTF8());
            return r; }, 330, 190 });

    int nDiff = 0, nMissing = 0, nOk = 0; size_t totalDiffSamples = 0;
    for (auto& c : cases)
    {
        bool ok = true;
        const auto out = renderCase (c.setup, c.nBlk, c.offBlk, ok);
        double rms = 0; for (float v : out) rms += (double) v * v; rms = std::sqrt (rms / (double) juce::jmax ((size_t) 1, out.size()));
        const auto f = dir.getChildFile (c.name + ".f32");
        if (write)
        {
            juce::FileOutputStream os (f); os.setPosition (0); os.truncate(); os.write (out.data(), out.size() * 4);
            std::printf ("  wrote %-48s rms %.5f fnv %016llx%s\n", c.name.toRawUTF8(), rms, (unsigned long long) fnv (out), ok ? "" : "  [load failed]");
            continue;
        }
        juce::MemoryBlock mb;
        if (! f.loadFileAsData (mb)) { std::printf ("  MISSING %s\n", c.name.toRawUTF8()); ++nMissing; continue; }
        const size_t n = mb.getSize() / 4; const float* ref = (const float*) mb.getData();
        size_t diff = 0; double worst = 0;
        if (n != out.size()) diff = std::max (n, out.size());
        else for (size_t i = 0; i < n; ++i) if (std::memcmp (&ref[i], &out[i], 4) != 0) { ++diff; worst = std::max (worst, (double) std::fabs (ref[i] - out[i])); }
        std::printf ("  %s %-48s rms %.5f  %zu/%zu differ  worst %.3e\n", diff ? "FAIL" : "same", c.name.toRawUTF8(), rms, diff, out.size(), worst);
        if (diff) { ++nDiff; totalDiffSamples += diff; } else ++nOk;
    }
    if (write) { std::printf ("organics_null: wrote %zu cases to %s\n", cases.size(), dir.getFullPathName().toRawUTF8()); return 0; }
    std::printf ("organics_null: %d/%zu BIT-IDENTICAL, %d differ (%zu samples), %d missing\n", nOk, cases.size(), nDiff, totalDiffSamples, nMissing);
    return (nDiff || nMissing) ? 1 : 0;
}
