// ══════════════════════════════════════════════════════════════════════════════════════════════
//  au_lazy_memory.cpp — tp63 · WHAT DOES EACH LAZY ENGINE COST, AND DOES IT EVER COME BACK?
//
//  Max: "every time I randomize my memory goes up ... it just won't go down." mac_dice_memory.mm
//  measured +1.26 GB on the second roll and +1.8 GB on the third, then ~16 MB per roll. A dice roll
//  touches every engine the plugin builds lazily, so this weighs each arm on its own (the installed
//  AU, phys_footprint, no editor): every oscillator engine on OSC A, then the second bank, then
//  the voice count — and then switches everything back to the default and weighs AGAIN, which is the
//  question he is actually asking ("cut the shit off that's being saved").
//
//  clang++ -O2 -std=c++17 Tests/au_lazy_memory.cpp -o /tmp/aulazy -framework AudioToolbox \
//          -framework AudioUnit -framework CoreFoundation -framework CoreAudio && /tmp/aulazy
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
#include <cmath>

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
    bool setRaw (const std::string& n, float v) { auto it = byName.find (n); if (it == byName.end()) return false; return AudioUnitSetParameter (au, it->second, kAudioUnitScope_Global, 0, v, 0) == noErr; }
    float maxOf (const std::string& n) { auto it = byName.find (n); return it == byName.end() ? -1 : info[it->second].maxValue; }
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
int main()
{
    Au a; if (! a.open()) { printf ("  !! cannot open the AU\n"); return 2; }
    printf ("\ntp63 — LAZY ENGINES, WEIGHED (installed AU, phys_footprint, no editor)\n\n");
    a.pump (0.8); a.render (20);
    auto settle = [&] { a.note (60, 100); a.note (64, 100); a.render (60); a.note (60, 0); a.note (64, 0); a.render (30); a.pump (1.2); a.render (20); a.pump (0.4); };
    settle();
    double base = footMB(); printf ("  %-44s %7.0f MB\n", "baseline (default patch, a chord played)", base);
    double last = base;
    auto step = [&] (const std::string& what) { const double m = footMB(); printf ("  %-44s %7.0f MB   %+6.0f\n", what.c_str(), m, m - last); last = m; };
    const float nEng = a.maxOf ("Synth OSC A Engine");
    printf ("  (OSC A Engine has %d choices)\n", (int) nEng + 1);
    for (int e = 0; e <= (int) nEng; ++e) { a.setRaw ("Synth OSC A Engine", (float) e); settle(); step ("OSC A engine = " + std::to_string (e)); }
    a.setRaw ("Synth OSC A Engine", 0); settle(); step ("OSC A engine back to 0");
    for (const char* o : { "Osc B Enable", "Osc C Enable", "Osc D Enable" }) { a.setRaw (o, 1); settle(); step (std::string (o) + " = 1"); }
    a.setRaw ("Osc E Enable", 1); settle(); step ("Osc E Enable = 1 (bank B is built)");
    for (const char* o : { "Osc F Enable", "Osc G Enable", "Osc H Enable" }) { a.setRaw (o, 1); settle(); step (std::string (o) + " = 1"); }
    for (int e = 0; e <= (int) nEng; ++e) { a.setRaw ("Synth OSC E Engine", (float) e); settle(); step ("OSC E engine = " + std::to_string (e)); }
    const float vmax = a.maxOf ("Synth Voices"); a.setRaw ("Synth Voices", vmax); settle(); step ("Synth Voices = max (" + std::to_string ((int) vmax) + ")");
    // and back to the default: what comes back?
    for (const char* o : { "Osc B Enable", "Osc C Enable", "Osc D Enable", "Osc E Enable", "Osc F Enable", "Osc G Enable", "Osc H Enable" }) a.setRaw (o, 0);
    a.setRaw ("Synth OSC A Engine", 0); a.setRaw ("Synth OSC E Engine", 0); a.setRaw ("Synth Voices", 8);
    settle(); a.pump (2.0); step ("EVERYTHING BACK TO DEFAULT, 2 s later");
    const double held2s = last;
    a.pump (6.0); a.render (10); a.pump (0.5); step ("... 8 s later (the idle timer's release)");
    printf ("\n  net: %+.0f MB still held over the baseline (bank B stays built: its %s)\n", last - base, "release is not part of tp63");
    const bool gaveBack = (held2s - last) > 1000.0;
    printf ("  %s  the MODAL + HARM engines come back when no oscillator wants them (%+.0f MB)\n", gaveBack ? "PASS " : "FAIL ", last - held2s);
    // and re-arm: ask for MODAL again, give the timer time, weigh, and make sure it still SOUNDS
    a.setRaw ("Synth OSC A Engine", 6); settle(); a.pump (3.0); a.render (10); step ("OSC A engine = 6 again (re-armed, +3 s)");
    double e = 0; size_t n = 0;
    {
        std::vector<float> L (BLK), R (BLK);
        a.note (60, 100); a.note (64, 100);
        for (int b = 0; b < 80; ++b)
        {
            AudioBufferList* abl = (AudioBufferList*) alloca (sizeof (AudioBufferList) + sizeof (AudioBuffer));
            abl->mNumberBuffers = 2;
            abl->mBuffers[0].mNumberChannels = 1; abl->mBuffers[0].mDataByteSize = BLK * 4; abl->mBuffers[0].mData = L.data();
            abl->mBuffers[1].mNumberChannels = 1; abl->mBuffers[1].mDataByteSize = BLK * 4; abl->mBuffers[1].mData = R.data();
            AudioUnitRenderActionFlags fl = 0; AudioTimeStamp ts {}; ts.mSampleTime = a.stamp; ts.mFlags = kAudioTimeStampSampleTimeValid;
            AudioUnitRender (a.au, &fl, &ts, 0, BLK, abl); a.stamp += BLK;
            if (b >= 10) for (int i = 0; i < BLK; ++i) { e += (double) L[i] * L[i]; ++n; }
            CFRunLoopRunInMode (kCFRunLoopDefaultMode, 0.002, false);
        }
        a.note (60, 0); a.note (64, 0);
    }
    const double rms = 20.0 * std::log10 (std::sqrt (e / (double) (n ? n : 1)) + 1e-12);
    const bool sounds = rms > -60.0;
    printf ("  %s  MODAL sounds again after a release and a re-arm (%.1f dBFS)\n", sounds ? "PASS " : "FAIL ", rms);
    // ⚠️ WHY THE RE-ARM READS +0 AND A SECOND RELEASE READS -0: phys_footprint does NOT count pages
    //    malloc hands back out of its MADV_FREE'd pool and zero-fills again (measured with a bare
    //    std::vector probe: 406 MB assigned, 5 MB after swap-free, 5 MB after re-assign). The first
    //    release is the real return; the re-arm is proved by SOUND, not by the footprint.
    printf ("\n");
    a.close(); return (gaveBack && sounds) ? 0 : 1;
}
