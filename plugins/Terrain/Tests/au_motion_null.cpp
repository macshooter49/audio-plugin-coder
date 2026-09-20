// ══════════════════════════════════════════════════════════════════════════════════════════════
//  au_motion_null.cpp — tp62 · THE MOTION SETTING NEVER TOUCHES THE AUDIO (the installed AU).
//
//  Max: "make sure the plugin still works of course, DSP-wise, sound-wise and everything."
//  The setting lives in the processor (a marker file read in the constructor) because the viz JSON
//  builders and the editor's push lane read it — so the one thing that must be PROVED is that
//  processBlock does not. Two fresh instances, one with ~/Library/Caches/Terrain/motion-off on disk
//  and one without, the same chord, the same blocks: the renders must be BIT-IDENTICAL.
//  The user's real preference file is restored on every exit path.
//
//  clang++ -O2 -std=c++17 Tests/au_motion_null.cpp -o /tmp/aumot -framework AudioToolbox \
//          -framework AudioUnit -framework CoreFoundation -framework CoreAudio && /tmp/aumot
// ══════════════════════════════════════════════════════════════════════════════════════════════
#include <AudioToolbox/AudioToolbox.h>
#include <CoreFoundation/CoreFoundation.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <string>
#include <vector>

static const double SR = 48000.0; static const int BLK = 512;
static int npass = 0, nfail = 0;
static void chk (bool ok, const char* what, const std::string& d = "")
{ if (ok) { ++npass; printf ("  PASS  %s\n        %s\n", what, d.c_str()); } else { ++nfail; printf ("  FAIL  %s\n        %s\n", what, d.c_str()); } }
static std::string home() { const char* h = getenv ("HOME"); return h ? h : ""; }
static std::string marker() { return home() + "/Library/Caches/Terrain/motion-off"; }
static bool exists (const std::string& p) { struct stat st {}; return stat (p.c_str(), &st) == 0; }
struct MarkerGuard
{
    bool had; MarkerGuard() : had (exists (marker())) {}
    ~MarkerGuard() { set (had); }
    static void set (bool on)
    { if (on) { const std::string d = home() + "/Library/Caches/Terrain"; mkdir (d.c_str(), 0755); FILE* f = fopen (marker().c_str(), "w"); if (f) { fputs ("1", f); fclose (f); } }
      else unlink (marker().c_str()); }
};
struct Au
{
    AudioUnit au = nullptr; double stamp = 0.0;
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
        return AudioUnitInitialize (au) == noErr;
    }
    void pump (double sec) { const double t0 = CFAbsoluteTimeGetCurrent(); while (CFAbsoluteTimeGetCurrent() - t0 < sec) CFRunLoopRunInMode (kCFRunLoopDefaultMode, 0.01, false); }
    void note (int n, int v) { MusicDeviceMIDIEvent (au, v > 0 ? 0x90 : 0x80, (UInt32) n, (UInt32) v, 0); }
    std::vector<float> render (int blocks)
    {
        std::vector<float> out; out.reserve ((size_t) blocks * BLK * 2);
        std::vector<float> L (BLK), R (BLK);
        for (int b = 0; b < blocks; ++b)
        {
            AudioBufferList* abl = (AudioBufferList*) alloca (sizeof (AudioBufferList) + sizeof (AudioBuffer));
            abl->mNumberBuffers = 2;
            abl->mBuffers[0].mNumberChannels = 1; abl->mBuffers[0].mDataByteSize = BLK * 4; abl->mBuffers[0].mData = L.data();
            abl->mBuffers[1].mNumberChannels = 1; abl->mBuffers[1].mDataByteSize = BLK * 4; abl->mBuffers[1].mData = R.data();
            AudioUnitRenderActionFlags fl = 0; AudioTimeStamp ts {}; ts.mSampleTime = stamp; ts.mFlags = kAudioTimeStampSampleTimeValid;
            AudioUnitRender (au, &fl, &ts, 0, BLK, abl); stamp += BLK;
            out.insert (out.end(), L.begin(), L.end()); out.insert (out.end(), R.begin(), R.end());
        }
        return out;
    }
    void close() { if (au) { AudioUnitUninitialize (au); AudioComponentInstanceDispose (au); au = nullptr; } }
};
static std::vector<float> take (bool motionOff, double& rmsOut)
{
    MarkerGuard::set (motionOff);
    Au a; if (! a.open()) return {};
    a.pump (0.5); a.render (10);
    a.note (60, 100); a.note (64, 100); a.note (67, 100);
    auto v = a.render (120);
    a.note (60, 0); a.note (64, 0); a.note (67, 0);
    auto t = a.render (60); v.insert (v.end(), t.begin(), t.end());
    double e = 0; for (float x : v) e += (double) x * x; rmsOut = 20.0 * std::log10 (std::sqrt (e / (double) v.size()) + 1e-12);
    a.close(); return v;
}
int main()
{
    MarkerGuard guard;
    printf ("\ntp62 — THE MOTION SETTING AND THE AUDIO (installed AU, deterministic)\n\n");
    double r0 = 0, r1 = 0;
    const auto on  = take (false, r0);
    const auto off = take (true,  r1);
    if (on.empty() || off.empty()) { printf ("  !! cannot open the AU\n"); return 2; }
    chk (r0 > -60.0, "[0] the reference chord sounds", "rms " + std::to_string (r0) + " dBFS");
    size_t diff = 0; float maxd = 0;
    for (size_t i = 0; i < on.size() && i < off.size(); ++i) { const float d = std::fabs (on[i] - off[i]); if (d != 0.0f) ++diff; if (d > maxd) maxd = d; }
    chk (on.size() == off.size() && diff == 0, "🚨 [1] motion OFF renders BIT-IDENTICAL audio to motion ON — the setting never reaches processBlock",
         std::to_string (diff) + " differing samples of " + std::to_string (on.size()) + ", max |d| " + std::to_string (maxd));
    printf ("\n  %d passed, %d failed\n\n", npass, nfail);
    return nfail ? 1 : 0;
}
