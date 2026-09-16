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
static const int    MEAS = 188;     // 2.0 s

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
    double profile (int notes)
    {
        static const int CH[8] = { 48, 55, 60, 64, 67, 72, 76, 79 };
        for (int n = 0; n < notes && n < 8; ++n) MusicDeviceMIDIEvent (au, 0x90, (UInt32) CH[n], 100, 0);
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

//  tp17 — IS IT THE OVERSAMPLING MACHINERY, OR THE CORE?
//  Every row: 8-note chord, ONE oscillator, filter 2 = None. The only variable is filter 1's TYPE.
//  XPD_LP1 (42) is the probe: a ONE-POLE core that sits inside the oversampled range 34..42, so it
//  pays the machinery and almost nothing else. SVF_LP24 (43) is its mirror: four poles, NOT
//  oversampled. If 42 costs like the Ladder and 43 costs like the SVF, the machinery is the bill.
struct Row { const char* label; float type; bool os; };

int main()
{
    printf ("\n══ tp17 — FILTER COST BY TYPE (8-note chord, 1 osc, F2 None, editor closed) ══\n\n");
    const Row R[] = {
        { "NONE            (control)        ", 27, false },
        { "SVF LP          1 pole-pair      ",  5, false },
        { "SVF LP24        4 poles          ", 43, false },
        { "MS-20 LP        nonlinear        ", 88, false },
        { "COMB +          delay line       ", 10, false },
        { "BIT CRUSH       nonlinear        ", 23, false },
        { "FORMANT MORPH   5 resonators     ", 17, false },
        { "── oversampled below ────────────", -1, false },
        { "XPD LP1         ONE POLE + 2x    ", 42, true  },
        { "RING MOD        a multiply + 2x  ", 21, true  },
        { "WAVESHAPER      a shaper + 2x    ", 24, true  },
        { "LADDER LP24     4 poles + 2x     ",  0, true  },
        { "DIODE LP        nonlinear + 2x   ",  3, true  },
        { "ACID 303        nonlinear + 2x   ",  4, true  },
        { "POLIVOKS        nonlinear + 2x   ", 89, true  },
    };
    const int N = (int) (sizeof R / sizeof R[0]);
    std::vector<double> best ((size_t) N, 1e9);
    for (int pass = 0; pass < 2; ++pass)
        for (int k = 0; k < N; ++k)
        {
            if (R[k].type < 0) continue;
            AU a; if (! a.open()) return 2;
            a.setRaw ("Synth Filter 1 Type", R[k].type);
            a.set ("Synth Filter 1 Source A", 1.0f);
            a.set ("Synth OSC A Filter 1 Send", 1.0f);
            const double pct = a.profile (8);
            a.close();
            best[(size_t) k] = std::min (best[(size_t) k], pct);
        }
    const double none = best[0];
    for (int k = 0; k < N; ++k)
    {
        if (R[k].type < 0) { printf ("  %s\n", R[k].label); continue; }
        printf ("  %6.2f%%  (%+5.2f vs None)  %s %s\n", best[(size_t) k], best[(size_t) k] - none,
                R[k].label, R[k].os ? "[2x]" : "");
    }
    printf ("\n");
    return 0;
}
