// ══════════════════════════════════════════════════════════════════════════════════════════════
//  tp103 — THE MPE-OFF NULL. Renders ONE fixed MIDI sequence through the real processor and writes the
//  raw float output, so the build BEFORE the expression/MIDI settings work and the build AFTER it can be
//  compared bit for bit (memcmp, not an epsilon).
//
//    Tests/expression_midi.sh null <out.f32>        (build Terrain first; links libTerrain_SharedCode.a)
//
//  Deliberately uses ONLY APIs that existed before tp103 (it is compiled against the baseline build too).
//  The sequence is what a non-MPE player sends: notes on channel 1 AND channel 3 (a multi-channel host
//  track), the pitch wheel, CHANNEL pressure (routed to cutoff and to Osc A level), the mod wheel, CC 74,
//  the sustain pedal, and a 12-note cluster past the Voices knob (the steal path), with Osc A unison 3.
//  Poly aftertouch is left out ON PURPOSE: reaching only its own voice is the one intended change.
//
//    Tests/expression_midi.sh null <out.f32> press  — tp109: the PRESSURE sequence (pressure curve / start / ceiling at
//  their neutral defaults must be bit-identical to the build before them). MPE on (setMpeOn, tp103): per-note channel
//  pressure ramps on members 2 and 3 + master-channel pressure; then MPE off: poly aftertouch ramps on two keys and
//  channel pressure on channel 1. Every value 0..127 is visited. Uses only APIs that existed at tp103.
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

static void setP (TerrainAudioProcessor& p, const char* id, float plain)
{
    auto* prm = p.apvts.getParameter (id);
    if (prm == nullptr) { printf ("!! no param %s\n", id); std::exit (2); }
    prm->setValueNotifyingHost (prm->convertTo0to1 (plain));
}

int main (int argc, char** argv)
{
    setenv ("TERRAIN_DETERMINISTIC", "1", 1);
    if (argc < 2) { printf ("usage: expression_null_render <out.f32>\n"); return 2; }
    juce::ScopedJuceInitialiser_GUI init;
    const double SR = 48000.0; const int BLK = 512; const int NBLK = 520;   // ~5.5 s
    auto proc = std::make_unique<TerrainAudioProcessor>();
    auto& p = *proc;
    p.setPlayConfigDetails (0, 2, SR, BLK);
    p.prepareToPlay (SR, BLK);
    setP (p, ParameterIDs::SYN_OSC_A_UNISON, 3.f);
    // Aftertouch (231) -> Cut 1 and -> Osc A level; Pitch Bend (232) -> Frame; Mod wheel (230) -> Warp
    p.setSynthModMatrix ("[{\"s\":231,\"d\":" + juce::String ((int) wc::ModDest::Cut1)   + ",\"v\":0.6},"
                          "{\"s\":231,\"d\":" + juce::String ((int) wc::ModDest::LevelA) + ",\"v\":-0.5},"
                          "{\"s\":232,\"d\":" + juce::String ((int) wc::ModDest::Frame)  + ",\"v\":0.3},"
                          "{\"s\":230,\"d\":" + juce::String ((int) wc::ModDest::Warp)   + ",\"v\":0.4}]");

    const bool press = argc >= 3 && std::strcmp (argv[2], "press") == 0;
    if (press) p.setMpeOn (true, 48.0f, false);

    std::vector<float> out; out.reserve ((size_t) (NBLK * BLK * 2));
    juce::AudioBuffer<float> buf (2, BLK);
    for (int b = 0; b < NBLK; ++b)
    {
        buf.clear();
        juce::MidiBuffer m;
        auto on  = [&] (int ch, int n, int v, int pos = 0) { m.addEvent (juce::MidiMessage::noteOn  (ch, n, (juce::uint8) v), pos); };
        auto off = [&] (int ch, int n, int pos = 0)        { m.addEvent (juce::MidiMessage::noteOff (ch, n), pos); };
        if (press)
        {   // blocks 0..259: MPE members · 260..519: MPE off, poly AT + channel pressure
            if (b == 2)  { on (2, 60, 100); on (3, 67, 90, 33); }
            if (b >= 10 && b < 138)  m.addEvent (juce::MidiMessage::channelPressureChange (2, b - 10), 64);          // 0..127 up
            if (b >= 30 && b < 158)  m.addEvent (juce::MidiMessage::channelPressureChange (3, 127 - (b - 30)), 300); // 127..0 down
            if (b >= 170 && b < 200) m.addEvent (juce::MidiMessage::channelPressureChange (1, (b - 170) * 4), 10);   // the master
            if (b == 230) { off (2, 60); off (3, 67, 9); }
            if (b == 250) p.setMpeOn (false, 48.0f, false);
            if (b == 262) { on (1, 60, 100); on (1, 64, 90, 17); }
            if (b >= 270 && b < 398) m.addEvent (juce::MidiMessage::aftertouchChange (1, 64, b - 270), 128);
            if (b >= 280 && b < 408) m.addEvent (juce::MidiMessage::aftertouchChange (1, 60, 127 - (b - 280)), 7);
            if (b >= 420 && b < 480) m.addEvent (juce::MidiMessage::channelPressureChange (1, ((b - 420) * 9) % 128), 200);
            if (b == 500) { off (1, 60); off (1, 64); }
            p.processBlock (buf, m);
            for (int i = 0; i < BLK; ++i) { out.push_back (buf.getSample (0, i)); out.push_back (buf.getSample (1, i)); }
            continue;
        }
        if (b == 2)  { on (1, 60, 100); on (1, 64, 90, 17); on (1, 67, 80, 301); }
        if (b == 10) on (3, 72, 110, 40);
        if (b >= 20 && b < 60)  m.addEvent (juce::MidiMessage::pitchWheel (1, 8192 + (b - 20) * 180), 100);
        if (b >= 60 && b < 90)  m.addEvent (juce::MidiMessage::pitchWheel (3, 8192 + 7200 - (b - 60) * 240), 5);
        if (b >= 30 && b < 80)  m.addEvent (juce::MidiMessage::channelPressureChange (1, (b - 30) * 2), 200);
        if (b >= 80 && b < 110) m.addEvent (juce::MidiMessage::channelPressureChange (3, 127 - (b - 80) * 4), 60);
        if (b >= 40 && b < 70)  m.addEvent (juce::MidiMessage::controllerEvent (1, 1, (b - 40) * 4), 0);
        if (b >= 50 && b < 75)  m.addEvent (juce::MidiMessage::controllerEvent (1, 74, (b - 50) * 5), 0);
        if (b == 120) { off (1, 60); off (1, 64, 3); off (1, 67, 7); off (3, 72, 90); }
        if (b == 140) m.addEvent (juce::MidiMessage::controllerEvent (1, 64, 127), 0);
        if (b == 150) for (int k = 0; k < 12; ++k) on (1, 48 + k * 2, 70 + k * 4, k * 11);
        if (b == 200) for (int k = 0; k < 12; ++k) off (1, 48 + k * 2, k * 5);
        if (b == 260) m.addEvent (juce::MidiMessage::controllerEvent (1, 64, 0), 0);
        if (b == 300) { on (2, 55, 100); on (5, 62, 100); }
        if (b == 330) m.addEvent (juce::MidiMessage::pitchWheel (2, 12000), 0);
        if (b == 380) { off (2, 55); off (5, 62); }
        if (b == 400) m.addEvent (juce::MidiMessage::pitchWheel (1, 8192), 0);
        p.processBlock (buf, m);
        for (int i = 0; i < BLK; ++i) { out.push_back (buf.getSample (0, i)); out.push_back (buf.getSample (1, i)); }
    }
    double rms = 0; for (float v : out) rms += (double) v * v; rms = std::sqrt (rms / (double) out.size());
    uint64_t h = 1469598103934665603ull;
    for (float v : out) { uint32_t u; std::memcpy (&u, &v, 4); h = (h ^ u) * 1099511628211ull; }
    FILE* f = fopen (argv[1], "wb"); if (! f) { printf ("!! cannot write %s\n", argv[1]); return 2; }
    fwrite (out.data(), 4, out.size(), f); fclose (f);
    printf ("expression_null_render: %zu samples, RMS %.6f, fnv64 %016llx -> %s\n", out.size(), rms, (unsigned long long) h, argv[1]);
    p.releaseResources();
    return rms > 1e-4 ? 0 : 1;
}
