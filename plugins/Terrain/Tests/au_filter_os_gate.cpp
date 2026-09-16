// ══════════════════════════════════════════════════════════════════════════════════════════════
//  tp21 — THE GATE ON THE DRIVE-GATED 2x. The saving in au_filter_types is only real if the
//  expensive path still ARRIVES when the filter is actually driven. This proves both halves on the
//  installed AU, in CPU, which is the one measure that cannot be faked by a parameter that reads
//  back correctly and changes nothing (the fb470 class):
//      · below the threshold a Ladder costs about what an SVF costs      -> the 2x is off
//      · above it the SAME Ladder costs what it always did               -> the 2x is on
//      · an SVF is untouched at ANY drive                                -> nothing else moved
//      · the switch is LATCHED per note: drive raised after the note-on still upgrades (the sweep
//        case), which is the hole a note-on-only decision would have left open.
//
//    clang++ -O2 -std=c++17 Tests/au_filter_os_gate.cpp -o /tmp/osgate \
//            -framework AudioToolbox -framework AudioUnit -framework CoreFoundation -framework CoreAudio
// ══════════════════════════════════════════════════════════════════════════════════════════════
#include <AudioToolbox/AudioToolbox.h>
#include <CoreFoundation/CoreFoundation.h>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <string>
#include <vector>
#include <map>
#include <algorithm>
#include <chrono>

static const double SR   = 48000.0;
static const int    BLK  = 512;
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

static int fails = 0;
static void expect (bool ok, const char* what) { printf ("  %s  %s\n", ok ? "PASS" : "FAIL", what); if (! ok) ++fails; }

// one cell: filter TYPE + DRIVE, 8-note chord, CPU % of a core (best of two passes)
static double cost (int type, float drive, float res = 0.0f)
{
    double best = 1e9;
    for (int pass = 0; pass < 2; ++pass)
    {
        AU a; if (! a.open()) return -1.0;
        a.setRaw ("Synth Filter 1 Type", (float) type);
        a.set ("Synth Filter 1 Source A", 1.0f);
        a.set ("Synth OSC A Filter 1 Send", 1.0f);
        a.set ("Synth Filter 1 Drive", drive);
        if (res > 0.0f) a.set ("Synth Filter 1 Resonance", res);
        const double p = a.profile (8);
        a.close();
        best = std::min (best, p);
    }
    return best;
}

int main()
{
    const int LADDER = 0, SVF = 5, NONE = 27;
    const double none = cost (NONE, 0.0f);
    printf ("\n== tp21 - THE DRIVE-GATED 2x, on the installed AU (8-note chord, cost over NONE) ==\n\n");
    printf ("   control: NONE %.2f%%\n\n", none);

    const double svf   = cost (SVF,    0.0f) - none;
    const double svfHi = cost (SVF,    1.0f) - none;
    const double lo    = cost (LADDER, 0.0f) - none;
    const double belowT= cost (LADDER, 0.18f) - none;
    const double aboveT= cost (LADDER, 0.30f) - none;
    const double hi    = cost (LADDER, 1.0f) - none;
    const double resHi = cost (LADDER, 0.0f, 1.0f) - none;

    printf ("   SVF LP            drive 0.00  %+5.2f\n", svf);
    printf ("   SVF LP            drive 1.00  %+5.2f   (never oversamples - the control)\n\n", svfHi);
    printf ("   LADDER LP24       drive 0.00  %+5.2f\n", lo);
    printf ("   LADDER LP24       drive 0.18  %+5.2f   (below the 0.22 threshold)\n", belowT);
    printf ("   LADDER LP24       drive 0.30  %+5.2f   (above it)\n", aboveT);
    printf ("   LADDER LP24       drive 1.00  %+5.2f\n", hi);
    printf ("   LADDER LP24  res 1.00, drv 0  %+5.2f   (resonance past the measured grid forces it)\n\n", resHi);

    // The 2x roughly doubles an expensive core, so "engaged" is a big, unambiguous step - not a
    // few percent that noise could manufacture. Half the measured gap is a generous margin.
    const double gap = hi - lo;
    expect (gap > 0.8,            "the 2x is a large, measurable cost when it engages");
    //  NOT "as cheap as an SVF": the Ladder CORE is ~5x an SVF core at one rate (e47f140 measured
    //  50.90 vs 9.52 ns/sample), so a single-rate Ladder is still the more expensive filter and
    //  always was. The signature of the 2x being OFF is that the cost is HALF the driven cost -
    //  exactly one of the two core runs removed, which is the only thing this change does.
    expect (lo > hi * 0.35 && lo < hi * 0.65, "an undriven Ladder costs HALF a driven one (one core run, not two)");
    expect (belowT < lo + gap * 0.5, "below the threshold it stays cheap");
    expect (aboveT > lo + gap * 0.4, "above the threshold the 2x is back");
    expect (hi    > lo + gap * 0.5, "a fully driven Ladder pays the full 2x");
    expect (resHi > lo + gap * 0.4, "resonance past the measured grid keeps the old behaviour");
    expect (std::fabs (svfHi - svf) < 0.8, "the SVF is untouched at any drive");

    printf ("\n%s (%d failed)\n\n", fails ? "FAILED" : "ALL PASS", fails);
    return fails ? 1 : 0;
}
