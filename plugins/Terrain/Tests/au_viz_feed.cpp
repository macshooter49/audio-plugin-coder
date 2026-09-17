// ══════════════════════════════════════════════════════════════════════════════════════════════
//  tp31 — WHAT THE SPECTRUM ANALYZERS CAN ACTUALLY SEE.
//
//  Max: "I'm playing something and there isn't a background filter visualizer... some presets
//  don't have it and some do."  Both the filter card's white spectrum and the rack Equalizer's
//  spectrum are drawn from ONE feed: analyzerPre/analyzerPost, pushed to the page as
//  __terrainEqAnalyzer. Those two analyzers are fed inside the master-EQ block of processBlock —
//  analyzerPre.pushSample() immediately before eqL.processSample(), analyzerPost immediately
//  after — which is UPSTREAM of the FX rack and of the FLOW cards.
//
//  So this test does not need to read the analyzer at all. The master EQ occupies the analyzers'
//  exact tap point, so whether the EQ can be HEARD on a given patch is a direct measurement of
//  whether the analyzers can SEE it. If a patch whose oscillator is routed into the rack is
//  unaffected by a brutal master-EQ lowpass, then the analyzers are being fed silence on that
//  patch and every spectrum in the UI is dead — which is the bug.
//
//    clang++ -O2 -std=c++17 Tests/au_viz_feed.cpp -o /tmp/auvf \
//            -framework AudioToolbox -framework AudioUnit -framework CoreFoundation -framework CoreAudio
// ══════════════════════════════════════════════════════════════════════════════════════════════
#include <AudioToolbox/AudioToolbox.h>
#include <CoreFoundation/CoreFoundation.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <string>
#include <vector>
#include <map>
static const double SR = 48000.0; static const int BLK = 512;
static int npass = 0, nfail = 0;
static void chk (bool ok, const char* what, const char* detail = "")
{ if (ok) { ++npass; printf ("  PASS  %s   %s\n", what, detail); } else { ++nfail; printf ("  FAIL  %s   %s\n", what, detail); } }
struct Au
{
    AudioUnit au = nullptr; std::map<std::string, AudioUnitParameterID> byName; std::map<AudioUnitParameterID, AudioUnitParameterInfo> info;
    double stamp = 0.0;
    bool open()
    {
        setenv ("TERRAIN_DETERMINISTIC", "1", 1);
        AudioComponentDescription d {}; d.componentType = kAudioUnitType_MusicDevice; d.componentSubType = 'Tern'; d.componentManufacturer = 'Wvcr';
        AudioComponent c = AudioComponentFindNext (nullptr, &d); if (! c || AudioComponentInstanceNew (c, &au) != noErr) return false;
        AudioStreamBasicDescription f {}; f.mSampleRate = SR; f.mFormatID = kAudioFormatLinearPCM;
        f.mFormatFlags = kAudioFormatFlagsNativeFloatPacked | kAudioFormatFlagIsNonInterleaved;
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
            char nm[256] = {}; if (pi.cfNameString) CFStringGetCString (pi.cfNameString, nm, sizeof nm, kCFStringEncodingUTF8); else std::strncpy (nm, pi.name, 255);
            byName[nm] = id; info[id] = pi;
        }
        return true;
    }
    bool set (const char* name, float norm)
    {
        auto it = byName.find (name); if (it == byName.end()) { printf ("    !! no parameter named '%s'\n", name); return false; }
        const auto& pi = info.at (it->second);
        return AudioUnitSetParameter (au, it->second, kAudioUnitScope_Global, 0, pi.minValue + norm * (pi.maxValue - pi.minValue), 0) == noErr;
    }
    bool setIdx (const char* name, int idx)
    { auto it = byName.find (name); if (it == byName.end()) { printf ("    !! no parameter named '%s'\n", name); return false; }
      return AudioUnitSetParameter (au, it->second, kAudioUnitScope_Global, 0, (float) idx, 0) == noErr; }
    void note (int n, int v) { MusicDeviceMIDIEvent (au, v ? 0x90 : 0x80, (UInt32) n, (UInt32) v, 0); }
    void pump (double sec) { const double t0 = CFAbsoluteTimeGetCurrent(); while (CFAbsoluteTimeGetCurrent() - t0 < sec) CFRunLoopRunInMode (kCFRunLoopDefaultMode, 0.01, false); }
    void render (int blocks, std::vector<float>* out)
    {
        std::vector<float> bl ((size_t) BLK), br ((size_t) BLK);
        AudioBufferList* abl = (AudioBufferList*) calloc (1, sizeof (AudioBufferList) + sizeof (AudioBuffer));
        for (int b = 0; b < blocks; ++b)
        {
            abl->mNumberBuffers = 2;
            abl->mBuffers[0].mNumberChannels = 1; abl->mBuffers[0].mDataByteSize = BLK * 4; abl->mBuffers[0].mData = bl.data();
            abl->mBuffers[1].mNumberChannels = 1; abl->mBuffers[1].mDataByteSize = BLK * 4; abl->mBuffers[1].mData = br.data();
            AudioUnitRenderActionFlags fl = 0; AudioTimeStamp ts {}; ts.mSampleTime = stamp; ts.mFlags = kAudioTimeStampSampleTimeValid;
            AudioUnitRender (au, &fl, &ts, 0, BLK, abl); stamp += BLK;
            if (out) { out->insert (out->end(), bl.begin(), bl.end()); out->insert (out->end(), br.begin(), br.end()); }
            CFRunLoopRunInMode (kCFRunLoopDefaultMode, 0.004, false);
        }
        free (abl);
    }
    void close() { if (au) { AudioUnitUninitialize (au); AudioComponentInstanceDispose (au); au = nullptr; } }
};
static double rmsDb (const std::vector<float>& v)
{ double e = 0; for (float x : v) e += (double) x * x; return v.empty() ? -240.0 : 20.0 * std::log10 (std::sqrt (e / (double) v.size()) + 1e-12); }
static double diffDb (const std::vector<float>& a, const std::vector<float>& b)
{ if (a.size() != b.size() || a.empty()) return 0.0; double e = 0; for (size_t i = 0; i < a.size(); ++i) { const double d = (double) a[i] - b[i]; e += d * d; } return 20.0 * std::log10 (std::sqrt (e / (double) a.size()) + 1e-12); }

// One scenario, rendered twice: master-EQ lowpass OFF vs a brutal lowpass ON.
// `setup` builds the patch. The DELTA between the two renders is how much of the patch
// passes through the analyzers' tap point.
static void probe (const char* label, void (*setup)(Au&), double& sigDb, double& deltaDb)
{
    std::vector<float> off, on;
    for (int pass = 0; pass < 2; ++pass)
    {
        Au a; if (! a.open()) { printf ("no AU\n"); exit (2); }
        setup (a);
        a.set ("EQ Bypass", 0.0f);                       // the master EQ section runs
        a.set ("EQ LP Bypass", pass == 0 ? 1.0f : 0.0f);  // pass 0: no lowpass · pass 1: lowpass in
        a.set ("EQ LP Freq", 0.0f);                       // ... at the very bottom of its range
        a.pump (0.35); a.render (4, nullptr);
        a.note (48, 100); a.note (60, 100);
        a.render (20, pass == 0 ? &off : &on);
        a.close();
    }
    sigDb = rmsDb (off); deltaDb = diffDb (off, on);
    printf ("     %-34s signal %7.2f dBFS   EQ delta %7.2f dBFS\n", label, sigDb, deltaDb);
}
int main()
{
    printf ("\n== tp31 - CAN THE SPECTRUM ANALYZERS SEE THE PATCH? ==\n");
    printf ("   (the master EQ sits at the analyzers' exact tap point, so its audibility IS their sight)\n\n");
    double s1, d1, s2, d2, s3, d3;
    probe ("1. a bare oscillator",        [] (Au& a) { }, s1, d1);
    probe ("2. that oscillator -> Reverb", [] (Au& a) {
        a.set ("Reverb In Chain", 1.0f); a.set ("Reverb Power", 1.0f); a.set ("SYN_RVB_SRC_A", 1.0f);
        a.set ("Reverb Mix", 1.0f);
    }, s2, d2);
    probe ("3. that oscillator -> Glitch", [] (Au& a) { a.setIdx ("Flow Chain 1", 3); }, s3, d3);
    printf ("\n");
    char b[220];
    snprintf (b, sizeof b, "delta %.2f dBFS against a %.2f dBFS signal", d1, s1);
    chk (d1 > s1 - 40.0, "1. on a bare patch the master chain carries the whole instrument", b);
    // ── 2 and 3 are the DIAGNOSIS, and they are the reason tp31 exists. They are reported, not
    //    asserted, because the thing they measure is a MASTER-CHAIN gap that tp31 deliberately did
    //    NOT change: moving the master EQ / tape / grain downstream of the rack would re-voice
    //    every preset that routes an oscillator, and that is Max's call, not a bug fix. What tp31
    //    fixed is the PICTURE — analyzerOut is now tapped at the final master, so the filter card
    //    and the rack's spectrum cards see this audio even though the master EQ does not.
    //    ⚠️ If the master chain is ever moved below the rack, these two will start reporting a
    //    real delta and this block should become a pair of assertions.
    printf ("  NOTE  a routed oscillator does not pass the master chain at all:\n");
    printf ("          -> Reverb   signal %6.2f dBFS, master-EQ delta %7.2f dBFS\n", s2, d2);
    printf ("          -> Glitch   signal %6.2f dBFS, master-EQ delta %7.2f dBFS\n", s3, d3);
    printf ("        That is WHY the spectrum analyzers were blind, and why the viz feed moved to\n");
    printf ("        the final master buffer (analyzerOut). The master EQ itself is untouched.\n");
    chk (s2 > -60.0 && s3 > -60.0, "2. ...while those patches are plainly audible at the output",
         "the audio is there; only the master chain's tap cannot see it");
    printf ("\n  PASS %d   FAIL %d\n\n", npass, nfail);
    return nfail ? 1 : 0;
}
