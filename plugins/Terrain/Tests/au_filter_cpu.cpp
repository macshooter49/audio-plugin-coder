// ══════════════════════════════════════════════════════════════════════════════════════════════
//  fb441 — WHY DOES THE MAIN FILTER COST CPU?  Real-plugin profile (the installed AU).
//
//    clang++ -O2 -std=c++17 Tests/au_filter_cpu.cpp -o /tmp/aucpu \
//            -framework AudioToolbox -framework AudioUnit -framework CoreFoundation -framework CoreAudio
//
//  Max: "the main filter... whenever it's latched onto an ABCD or SN, not even just all of them,
//  but one of them... it drags up my CPU at least by like 7 or 8%... I also have some LFO
//  automation going on... it could be the visualizer... figure out why."
//
//  Method: one fresh AU per scenario, a 4-note chord, 4 s rendered in 512-sample blocks after a
//  0.5 s warm-up; CPU% = wall time inside AudioUnitRender / audio time. The scenarios isolate the
//  suspects one at a time: routing alone, modulated cutoff (Erosion = per-sample drift = the
//  worst case an LFO can be), the send-mirror filter pairs (reverb/delay routed), the pooled
//  rack devices (each spawns its own pair per voice), unison, and oversampled vs. not.
//  The editor is closed, so vizConsumersLive() is false: everything measured here is DSP.
// ══════════════════════════════════════════════════════════════════════════════════════════════
#include <AudioToolbox/AudioToolbox.h>
#include <CoreFoundation/CoreFoundation.h>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <string>
#include <vector>
#include <map>
#include <chrono>
#include <algorithm>

static const double SR = 48000.0;
static const int    BLK = 512;
static const int    WARM = 48;      // 0.5 s
static const int    MEAS = 375;     // 4.0 s

struct AU
{
    AudioUnit au = nullptr;
    std::map<std::string, AudioUnitParameterID> byName;
    std::map<AudioUnitParameterID, AudioUnitParameterInfo> info;
    bool open()
    {
        AudioComponentDescription d {};
        d.componentType = kAudioUnitType_MusicDevice; d.componentSubType = 'Tern'; d.componentManufacturer = 'Wvcr';
        AudioComponent c = AudioComponentFindNext (nullptr, &d);
        if (! c || AudioComponentInstanceNew (c, &au) != noErr) { printf ("  !! AU not found\n"); return false; }
        AudioStreamBasicDescription f {};
        f.mSampleRate = SR; f.mFormatID = kAudioFormatLinearPCM;
        f.mFormatFlags = kAudioFormatFlagsNativeFloatPacked | kAudioFormatFlagIsNonInterleaved;
        f.mBytesPerPacket = 4; f.mFramesPerPacket = 1; f.mBytesPerFrame = 4; f.mChannelsPerFrame = 2; f.mBitsPerChannel = 32;
        AudioUnitSetProperty (au, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Output, 0, &f, sizeof f);
        UInt32 mx = BLK; AudioUnitSetProperty (au, kAudioUnitProperty_MaximumFramesPerSlice, kAudioUnitScope_Global, 0, &mx, sizeof mx);
        if (AudioUnitInitialize (au) != noErr) { printf ("  !! init failed\n"); return false; }
        UInt32 sz = 0; Boolean w = false;
        AudioUnitGetPropertyInfo (au, kAudioUnitProperty_ParameterList, kAudioUnitScope_Global, 0, &sz, &w);
        std::vector<AudioUnitParameterID> ids (sz / sizeof (AudioUnitParameterID));
        AudioUnitGetProperty (au, kAudioUnitProperty_ParameterList, kAudioUnitScope_Global, 0, ids.data(), &sz);
        for (auto id : ids) {
            AudioUnitParameterInfo pi {}; UInt32 s = sizeof pi;
            if (AudioUnitGetProperty (au, kAudioUnitProperty_ParameterInfo, kAudioUnitScope_Global, id, &pi, &s) != noErr) continue;
            char buf[256] = {0};
            if ((pi.flags & kAudioUnitParameterFlag_HasCFNameString) && pi.cfNameString) CFStringGetCString (pi.cfNameString, buf, sizeof buf, kCFStringEncodingUTF8);
            else snprintf (buf, sizeof buf, "%s", pi.name);
            byName[buf] = id; info[id] = pi;
        }
        return true;
    }
    void close() { if (au) { AudioUnitUninitialize (au); AudioComponentInstanceDispose (au); au = nullptr; } }
    bool set (const std::string& n, float norm)
    {
        auto it = byName.find (n); if (it == byName.end()) { printf ("  !! no param '%s'\n", n.c_str()); return false; }
        const auto& pi = info.at (it->second);
        return AudioUnitSetParameter (au, it->second, kAudioUnitScope_Global, 0, pi.minValue + norm * (pi.maxValue - pi.minValue), 0) == noErr;
    }
    bool setRaw (const std::string& n, float v)
    {
        auto it = byName.find (n); if (it == byName.end()) { printf ("  !! no param '%s'\n", n.c_str()); return false; }
        return AudioUnitSetParameter (au, it->second, kAudioUnitScope_Global, 0, v, 0) == noErr;
    }
    // returns CPU% (render wall time / audio time) over the measurement window
    double profile()
    {
        MusicDeviceMIDIEvent (au, 0x90, 48, 100, 0); MusicDeviceMIDIEvent (au, 0x90, 55, 100, 0);
        MusicDeviceMIDIEvent (au, 0x90, 60, 100, 0); MusicDeviceMIDIEvent (au, 0x90, 64, 100, 0);
        std::vector<float> bl ((size_t) BLK), br ((size_t) BLK);
        AudioBufferList* abl = (AudioBufferList*) calloc (1, sizeof (AudioBufferList) + sizeof (AudioBuffer));
        abl->mNumberBuffers = 2;
        AudioTimeStamp ts {}; ts.mFlags = kAudioTimeStampSampleTimeValid;
        double secs = 0.0;
        for (int b = 0; b < WARM + MEAS; ++b) {
            abl->mBuffers[0] = { 1, (UInt32) (BLK * 4), bl.data() };
            abl->mBuffers[1] = { 1, (UInt32) (BLK * 4), br.data() };
            AudioUnitRenderActionFlags fl = 0;
            const auto t0 = std::chrono::steady_clock::now();
            AudioUnitRender (au, &fl, &ts, 0, BLK, abl);
            const auto t1 = std::chrono::steady_clock::now();
            if (b >= WARM) secs += std::chrono::duration<double> (t1 - t0).count();
            ts.mSampleTime += BLK;
        }
        free (abl);
        return 100.0 * secs / ((double) MEAS * BLK / SR);
    }
};

struct Set { const char* name; float v; bool raw; };
struct Scn { const char* label; std::vector<Set> sets; };

int main()
{
    printf ("\n══ fb441 — MAIN FILTER CPU PROFILE (installed AU, 4-note chord, editor closed) ══\n\n");
    // Type index: 0 = Ladder LP 24 (oversampled), 5 = SVF LP (not), 17 = Formant Morph, 27 = None.
    const Scn S[] = {
        { "S0  baseline: filter type None, nothing routed",                  { } },
        { "S1  Ladder LP24 + Source A, static cutoff",                       { {"Synth Filter 1 Type",0,true}, {"Synth Filter 1 Source A",1,false} } },
        { "S2  S1 + Erosion 100% (cutoff drifts EVERY sample = LFO worst case)", { {"Synth Filter 1 Type",0,true}, {"Synth Filter 1 Source A",1,false}, {"Synth Erosion",1,false} } },
        { "S3  S1 + Reverb routed to A (one send-mirror pair runs)",          { {"Synth Filter 1 Type",0,true}, {"Synth Filter 1 Source A",1,false}, {"Reverb In Chain",1,false}, {"Reverb Power",1,false}, {"SYN_RVB_SRC_A",1,false} } },
        { "S4  S2 + Reverb routed (mirror pair + per-sample recompute)",      { {"Synth Filter 1 Type",0,true}, {"Synth Filter 1 Source A",1,false}, {"Synth Erosion",1,false}, {"Reverb In Chain",1,false}, {"Reverb Power",1,false}, {"SYN_RVB_SRC_A",1,false} } },
        { "S5  S4 + Delay + Equalizer + Compress + Multiband routed to A",    { {"Synth Filter 1 Type",0,true}, {"Synth Filter 1 Source A",1,false}, {"Synth Erosion",1,false},
                                                                               {"Reverb In Chain",1,false}, {"Reverb Power",1,false}, {"SYN_RVB_SRC_A",1,false},
                                                                               {"Delay In Chain",1,false}, {"Delay Power",1,false}, {"SYN_DLY_SRC_A",1,false},
                                                                               {"Equalizer In Chain",1,false}, {"Equalizer Power",1,false}, {"Equalizer SRC_A",1,false},
                                                                               {"Compress In Chain",1,false}, {"Compress Power",1,false}, {"Compress SRC_A",1,false},
                                                                               {"Multiband In Chain",1,false}, {"Multiband Power",1,false}, {"Multiband SRC_A",1,false} } },
        { "S5b same rack devices routed, filter UNROUTED (what the rack alone costs)", { {"Synth Filter 1 Type",0,true},
                                                                               {"Reverb In Chain",1,false}, {"Reverb Power",1,false}, {"SYN_RVB_SRC_A",1,false},
                                                                               {"Delay In Chain",1,false}, {"Delay Power",1,false}, {"SYN_DLY_SRC_A",1,false},
                                                                               {"Equalizer In Chain",1,false}, {"Equalizer Power",1,false}, {"Equalizer SRC_A",1,false},
                                                                               {"Compress In Chain",1,false}, {"Compress Power",1,false}, {"Compress SRC_A",1,false},
                                                                               {"Multiband In Chain",1,false}, {"Multiband Power",1,false}, {"Multiband SRC_A",1,false} } },
        { "S6  S1 with OSC A unison 50%",                                     { {"Synth Filter 1 Type",0,true}, {"Synth Filter 1 Source A",1,false}, {"Synth OSC A Unison",0.5f,false} } },
        { "S7  SVF LP (NOT oversampled) + Source A, static",                 { {"Synth Filter 1 Type",5,true}, {"Synth Filter 1 Source A",1,false} } },
        { "S8  SVF LP + Erosion 100%",                                       { {"Synth Filter 1 Type",5,true}, {"Synth Filter 1 Source A",1,false}, {"Synth Erosion",1,false} } },
        { "S9  Formant Morph + Erosion 100% (heavy coefficient engine)",     { {"Synth Filter 1 Type",17,true}, {"Synth Filter 1 Source A",1,false}, {"Synth Erosion",1,false} } },
    };
    std::vector<double> best;
    for (int pass = 0; pass < 2; ++pass)
    {
        int k = 0;
        for (const auto& s : S)
        {
            AU a; if (! a.open()) return 2;
            for (const auto& st : s.sets) { if (st.raw) a.setRaw (st.name, st.v); else a.set (st.name, st.v); }
            const double pct = a.profile();
            a.close();
            if (pass == 0) best.push_back (pct); else best[(size_t) k] = std::min (best[(size_t) k], pct);
            ++k;
        }
    }
    const double base = best[0];
    int k = 0;
    for (const auto& s : S) { printf ("  %6.2f%%   (%+6.2f vs S0)   %s\n", best[(size_t) k], best[(size_t) k] - base, s.label); ++k; }
    printf ("\n");
    return 0;
}
