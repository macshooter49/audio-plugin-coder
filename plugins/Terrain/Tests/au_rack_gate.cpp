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
#include <CoreFoundation/CFRunLoop.h>
static bool gPump = false;

static const double SR = 48000.0;
static const int    BLK = 512;
static const int    WARM = 48;      // 0.5 s
static const int    MEAS = 94;      // 1.0 s

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
    double rms()
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
            AudioUnitRender (au, &fl, &ts, 0, BLK, abl);
            if (gPump) CFRunLoopRunInMode (kCFRunLoopDefaultMode, 0.012, false);   // ~ a DAW's message thread
            if (b >= WARM) for (int i = 0; i < BLK; ++i) secs += (double) bl[(size_t) i] * bl[(size_t) i] + (double) br[(size_t) i] * br[(size_t) i];
            ts.mSampleTime += BLK;
        }
        free (abl);
        return std::sqrt (secs / ((double) MEAS * BLK * 2));
    }
};

struct Set { const char* name; float v; bool raw; };
struct Scn { const char* label; std::vector<Set> sets; };
static double dB (double x) { return 20.0 * std::log10 (std::max (x, 1e-9)); }
//  tp19 — "IT'S ALMOST LIKE A GATE": a rack device that is ROUTED but not passing audio must not
//  swallow its sources. Since tp12b the voice drops a pulled source from the main path outright
//  (exKeep_), so the device alone puts it back. Output RMS per state, against no rack at all.
int main (int argc, char** argv)
{
    gPump = argc > 1;
    printf ("\n══ tp19 — THE RACK GATE (installed AU, 4-note chord, output RMS) ══  runloop pump: %s\n\n", gPump ? "ON (timer can build pairs)" : "OFF");
    const Scn S[] = {
      { "R0  no rack",                                        { } },
      { "R1  Tape   in chain, POWER ON,  routed A",           { {"Tape In Chain",1,false},{"Tape Power",1,false},{"Tape SRC_A",1,false} } },
      { "R2  Tape   in chain, POWER OFF, routed A",           { {"Tape In Chain",1,false},{"Tape Power",0,false},{"Tape SRC_A",1,false} } },
      { "R3  Tape   ON, routed A, type CASSETTE",             { {"Tape In Chain",1,false},{"Tape Power",1,false},{"Tape SRC_A",1,false},{"Tape Type",1,true} } },
      { "R4  Tape   in chain, ON, NOT routed",                { {"Tape In Chain",1,false},{"Tape Power",1,false} } },
      { "R5  Multiband in chain, POWER ON,  routed A",        { {"Multiband In Chain",1,false},{"Multiband Power",1,false},{"Multiband SRC_A",1,false} } },
      { "R6  Multiband in chain, POWER OFF, routed A",        { {"Multiband In Chain",1,false},{"Multiband Power",0,false},{"Multiband SRC_A",1,false} } },
      { "R7  Widen  in chain, POWER ON,  routed A",           { {"Widen In Chain",1,false},{"Widen Power",1,false},{"Widen SRC_A",1,false} } },
      { "R8  Widen  in chain, POWER OFF, routed A",           { {"Widen In Chain",1,false},{"Widen Power",0,false},{"Widen SRC_A",1,false} } },
      { "R9  Tape   NOT in chain, routed A (stale pill)",     { {"Tape In Chain",0,false},{"Tape Power",1,false},{"Tape SRC_A",1,false} } },
    };
    double base = -1;
    for (const auto& s : S)
    {
        AU a; if (! a.open()) return 2;
        for (const auto& st : s.sets) { if (st.raw) a.setRaw (st.name, st.v); else a.set (st.name, st.v); }
        const double r = a.rms(); a.close();
        if (base < 0) base = r;
        const double d = dB (r) - dB (base);
        printf ("  %7.2f dBFS  (%+6.1f vs R0)  %s%s\n", dB (r), d, s.label, d < -20.0 ? "   <<< GATED" : "");
    }
    printf ("\n");
    return 0;
}
