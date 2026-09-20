// ══════════════════════════════════════════════════════════════════════════════════════════════
//  au_roll_memory.cpp — tp63 · WHICH PARAMETER FAMILY GROWS THE FOOTPRINT PER ROLL?
//
//  mac_dice_memory.mm (chord released between rolls) still climbs ~50 MB per roll after the lazy-
//  engine cliffs were given back. A dice roll is a few hundred parameter writes plus sample loads;
//  the writes are reachable here. Each family below is rolled R times to random values and weighed
//  after each round: a family whose footprint keeps climbing round after round is a cache that
//  never evicts or a buffer that is never freed. Sample LOADS are not reachable from the AU's
//  parameter surface and are measured separately (mac_dice_memory.mm).
//
//  clang++ -O2 -std=c++17 Tests/au_roll_memory.cpp -o /tmp/auroll -framework AudioToolbox \
//          -framework AudioUnit -framework CoreFoundation -framework CoreAudio && /tmp/auroll
// ══════════════════════════════════════════════════════════════════════════════════════════════
#include <AudioToolbox/AudioToolbox.h>
#include <CoreFoundation/CoreFoundation.h>
#include <libproc.h>
#include <unistd.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <map>
#include <regex>
#include <random>

static const double SR = 48000.0; static const int BLK = 512;
static double footMB() { struct rusage_info_v4 ri {}; if (proc_pid_rusage (getpid(), RUSAGE_INFO_V4, (rusage_info_t*) &ri) != 0) return 0; return ri.ri_phys_footprint / 1e6; }
struct Au
{
    AudioUnit au = nullptr; std::map<std::string, AudioUnitParameterID> byName; std::map<AudioUnitParameterID, AudioUnitParameterInfo> info; double stamp = 0;
    bool open()
    {
        setenv ("TERRAIN_DETERMINISTIC", "1", 1);
        AudioComponentDescription d {}; d.componentType = kAudioUnitType_MusicDevice; d.componentSubType = 'Tern'; d.componentManufacturer = 'Wvcr';
        AudioComponent c = AudioComponentFindNext (nullptr, &d); if (! c || AudioComponentInstanceNew (c, &au) != noErr) return false;
        AudioStreamBasicDescription f {}; f.mSampleRate = SR; f.mFormatID = kAudioFormatLinearPCM;
        f.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked | kAudioFormatFlagIsNonInterleaved;
        f.mBytesPerPacket = 4; f.mFramesPerPacket = 1; f.mBytesPerFrame = 4; f.mChannelsPerFrame = 2; f.mBitsPerChannel = 32;
        AudioUnitSetProperty (au, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Output, 0, &f, sizeof f);
        UInt32 mx = BLK; AudioUnitSetProperty (au, kAudioUnitProperty_MaximumFramesPerSlice, kAudioUnitScope_Global, 0, &mx, sizeof mx);
        if (AudioUnitInitialize (au) != noErr) return false;
        UInt32 sz = 0; Boolean w = false;
        AudioUnitGetPropertyInfo (au, kAudioUnitProperty_ParameterList, kAudioUnitScope_Global, 0, &sz, &w);
        std::vector<AudioUnitParameterID> ids (sz / sizeof (AudioUnitParameterID));
        AudioUnitGetProperty (au, kAudioUnitProperty_ParameterList, kAudioUnitScope_Global, 0, ids.data(), &sz);
        for (auto id : ids)
        {
            AudioUnitParameterInfo pi {}; UInt32 s = sizeof pi;
            if (AudioUnitGetProperty (au, kAudioUnitProperty_ParameterInfo, kAudioUnitScope_Global, id, &pi, &s) != noErr) continue;
            char b[256] = {};
            if ((pi.flags & kAudioUnitParameterFlag_HasCFNameString) && pi.cfNameString) CFStringGetCString (pi.cfNameString, b, sizeof b, kCFStringEncodingUTF8);
            else snprintf (b, sizeof b, "%s", pi.name);
            byName[b] = id; info[id] = pi;
        }
        return true;
    }
    void pump (double sec) { const double t0 = CFAbsoluteTimeGetCurrent(); while (CFAbsoluteTimeGetCurrent() - t0 < sec) CFRunLoopRunInMode (kCFRunLoopDefaultMode, 0.01, false); }
    void note (int n, int v) { MusicDeviceMIDIEvent (au, v > 0 ? 0x90 : 0x80, (UInt32) n, (UInt32) v, 0); }
    void render (int blocks)
    {
        std::vector<float> L (BLK), R (BLK);
        for (int b = 0; b < blocks; ++b)
        {
            AudioBufferList* abl = (AudioBufferList*) alloca (sizeof (AudioBufferList) + sizeof (AudioBuffer));
            abl->mNumberBuffers = 2;
            abl->mBuffers[0].mNumberChannels = 1; abl->mBuffers[0].mDataByteSize = BLK * 4; abl->mBuffers[0].mData = L.data();
            abl->mBuffers[1].mNumberChannels = 1; abl->mBuffers[1].mDataByteSize = BLK * 4; abl->mBuffers[1].mData = R.data();
            AudioUnitRenderActionFlags fl = 0; AudioTimeStamp ts {}; ts.mSampleTime = stamp; ts.mFlags = kAudioTimeStampSampleTimeValid;
            AudioUnitRender (au, &fl, &ts, 0, BLK, abl); stamp += BLK;
            CFRunLoopRunInMode (kCFRunLoopDefaultMode, 0.002, false);
        }
    }
    void close() { if (au) { AudioUnitUninitialize (au); AudioComponentInstanceDispose (au); au = nullptr; } }
};
int main (int argc, char** argv)
{
    const int R = argc > 1 ? atoi (argv[1]) : 8;
    Au a; if (! a.open()) { printf ("  !! cannot open the AU\n"); return 2; }
    std::mt19937 rng (12345);
    printf ("\ntp63 — FOOTPRINT PER ROLL, BY PARAMETER FAMILY (installed AU, no editor, %d rounds each)\n\n", R);
    a.pump (0.8); a.render (20);
    auto settle = [&] { a.note (60, 100); a.note (67, 100); a.render (40); a.note (60, 0); a.note (67, 0); a.render (20); a.pump (0.6); };
    settle();
    struct Fam { const char* label; const char* re; bool onlyChoices; };
    const Fam fams[] = {
        { "wavetable presets (every osc)",     "Preset$|Wavetable|WT ",         true  },
        { "filter types (both slots)",         "^Filter( 2)? Type$",             true  },
        { "rack devices In Chain (all 78)",    " In Chain$",                     false },
        { "rack device types",                 "^(Reverb|Delay|Saturate|Distortion|Granular|Tape|Chorus|Flanger|Phaser|Equalizer|Widen|Compress|OTT|Bode|Utility|Splitter)( [2-6])? Type$", true },
        { "oscillator engines (A–D, no MODAL/HARM)", "^Synth OSC [A-D] Engine$", true  },
        { "flow cards (chain slots)",          "^Flow Chain",                    true  },
    };
    for (const auto& fm : fams)
    {
        std::regex rx (fm.re);
        std::vector<AudioUnitParameterID> ids;
        for (auto& kv : a.byName) if (std::regex_search (kv.first, rx)) ids.push_back (kv.second);
        if (ids.empty()) { printf ("  %-44s (no parameters matched %s)\n", fm.label, fm.re); continue; }
        // a fresh baseline for the family, then R random rounds
        settle(); const double m0 = footMB(); double prev = m0;
        printf ("  %-44s %zu params · start %.0f MB\n", fm.label, ids.size(), m0);
        for (int r = 0; r < R; ++r)
        {
            for (auto id : ids)
            {
                const auto& pi = a.info[id];
                float v;
                if (std::strcmp (fm.label, "oscillator engines (A–D, no MODAL/HARM)") == 0) v = (float) (rng() % 5);   // 0..4: WT SAMP GRAN SPEC FM
                else if (pi.maxValue - pi.minValue <= 1.0f) v = (float) (rng() % 2);
                else v = pi.minValue + (float) (rng() % (unsigned) (pi.maxValue - pi.minValue + 1.0f));
                AudioUnitSetParameter (a.au, id, kAudioUnitScope_Global, 0, v, 0);
            }
            settle(); a.pump (0.8);
            const double m = footMB();
            printf ("      round %d  %7.0f MB  %+6.0f\n", r + 1, m, m - prev); prev = m;
        }
        // restore this family to defaults so the next one starts clean
        for (auto id : ids) AudioUnitSetParameter (a.au, id, kAudioUnitScope_Global, 0, a.info[id].defaultValue, 0);
        settle(); a.pump (8.0); a.render (10); a.pump (0.5);
        const double net = footMB() - m0;
        printf ("      family net after defaults + 8 s: %+.0f MB\n\n", net);
        if (std::strstr (fm.label, "wavetable") != nullptr)
        {
            // tp63 — the wavetable bank used to keep every table ever visited (+234 MB after six rounds,
            // never freed). Tables no oscillator has named for a few seconds are released; what stays is
            // the patch's own tables plus Sine (~4.4 MB each) and malloc's slack.
            const bool ok = net < 60.0;
            printf ("  %s  wavetables no oscillator names any more are given back (net %+.0f MB, was +234 before tp63)\n\n", ok ? "PASS " : "FAIL ", net);
            if (! ok) { a.close(); return 1; }
        }
    }
    a.close(); return 0;
}
