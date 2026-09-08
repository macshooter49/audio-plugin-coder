// fb456 — single-scenario continuous renderer, so a sampling profiler has a steady target.
//   clang++ -O2 -std=c++17 Tests/au_env_prof.cpp -o /tmp/auprof -framework AudioToolbox -framework AudioUnit -framework CoreFoundation -framework CoreAudio
//   ./auprof <idle|key|pad|key1|pad1> <seconds>
#include <AudioToolbox/AudioToolbox.h>
#include <CoreFoundation/CoreFoundation.h>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <cstdlib>
#include <string>
#include <vector>
#include <map>
#include <chrono>
#include <algorithm>
static const double SR = 48000.0; static const int BLK = 512;
static const int PERIOD = 94, GATE = 56;
struct EnvCfg { float dly, atk, hld, dec, sus, rel; };
static const EnvCfg KEY { 1.f, 5.f, 1.f, 200.f, 0.70f, 300.f };
static const EnvCfg PAD { 1.f, 278.f, 1.f, 200.f, 1.00f, 1870.f };
struct AU {
    AudioUnit au = nullptr;
    std::map<std::string, AudioUnitParameterID> byName; std::map<AudioUnitParameterID, AudioUnitParameterInfo> info;
    bool open() {
        AudioComponentDescription d {}; d.componentType = kAudioUnitType_MusicDevice; d.componentSubType = 'Tern'; d.componentManufacturer = 'Wvcr';
        AudioComponent c = AudioComponentFindNext (nullptr, &d);
        if (! c || AudioComponentInstanceNew (c, &au) != noErr) { printf ("!! AU not found\n"); return false; }
        AudioStreamBasicDescription f {}; f.mSampleRate = SR; f.mFormatID = kAudioFormatLinearPCM;
        f.mFormatFlags = kAudioFormatFlagsNativeFloatPacked | kAudioFormatFlagIsNonInterleaved;
        f.mBytesPerPacket = 4; f.mFramesPerPacket = 1; f.mBytesPerFrame = 4; f.mChannelsPerFrame = 2; f.mBitsPerChannel = 32;
        AudioUnitSetProperty (au, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Output, 0, &f, sizeof f);
        UInt32 mx = BLK; AudioUnitSetProperty (au, kAudioUnitProperty_MaximumFramesPerSlice, kAudioUnitScope_Global, 0, &mx, sizeof mx);
        if (AudioUnitInitialize (au) != noErr) { printf ("!! init failed\n"); return false; }
        UInt32 sz = 0; Boolean w = false;
        AudioUnitGetPropertyInfo (au, kAudioUnitProperty_ParameterList, kAudioUnitScope_Global, 0, &sz, &w);
        std::vector<AudioUnitParameterID> ids (sz / sizeof (AudioUnitParameterID));
        AudioUnitGetProperty (au, kAudioUnitProperty_ParameterList, kAudioUnitScope_Global, 0, ids.data(), &sz);
        for (auto id : ids) { AudioUnitParameterInfo pi {}; UInt32 s = sizeof pi;
            if (AudioUnitGetProperty (au, kAudioUnitProperty_ParameterInfo, kAudioUnitScope_Global, id, &pi, &s) != noErr) continue;
            char buf[256] = {0};
            if ((pi.flags & kAudioUnitParameterFlag_HasCFNameString) && pi.cfNameString) CFStringGetCString (pi.cfNameString, buf, sizeof buf, kCFStringEncodingUTF8);
            else snprintf (buf, sizeof buf, "%s", pi.name);
            byName[buf] = id; info[id] = pi; }
        return true; }
    void setNorm (const std::string& n, float norm) { auto it = byName.find (n); if (it == byName.end()) return;
        const auto& pi = info.at (it->second); AudioUnitSetParameter (au, it->second, kAudioUnitScope_Global, 0, pi.minValue + norm * (pi.maxValue - pi.minValue), 0); }
    static float toNorm (float v, float lo, float hi) { const float p = (v - lo) / (hi - lo); return p <= 0.f ? 0.f : std::pow (p, 0.3f); }
    void applyEnv (const EnvCfg& e) {
        setNorm ("Synth Amp Delay",   toNorm (e.dly, 0.f, 10000.f)); setNorm ("Synth Amp Attack", toNorm (e.atk, 1.f, 10000.f));
        setNorm ("Synth Amp Hold",    toNorm (e.hld, 0.f, 10000.f)); setNorm ("Synth Amp Decay",  toNorm (e.dec, 1.f, 10000.f));
        setNorm ("Synth Amp Sustain", e.sus);                        setNorm ("Synth Amp Release",toNorm (e.rel, 1.f, 10000.f)); }
};
int main (int argc, char** argv) {
    const std::string mode = argc > 1 ? argv[1] : "pad";
    const double secs = argc > 2 ? atof (argv[2]) : 20.0;
    const int chord = (mode == "key1" || mode == "pad1") ? 1 : 4;
    const bool notes = (mode != "idle");
    AU a; if (! a.open()) return 2;
    a.applyEnv ((mode == "pad" || mode == "pad1") ? PAD : KEY);
    const int NOTES[4] = { 48, 55, 60, 64 };
    std::vector<float> bl ((size_t) BLK), br ((size_t) BLK);
    AudioBufferList* abl = (AudioBufferList*) calloc (1, sizeof (AudioBufferList) + sizeof (AudioBuffer)); abl->mNumberBuffers = 2;
    AudioTimeStamp ts {}; ts.mFlags = kAudioTimeStampSampleTimeValid;
    const int total = (int) (secs * SR / BLK); double acc = 0.0; bool down = false;
    for (int b = 0; b < total; ++b) {
        if (notes) { const int ph = b % PERIOD;
            if (ph == 0) { for (int n = 0; n < chord; ++n) MusicDeviceMIDIEvent (a.au, 0x90, NOTES[n], 100, 0); down = true; }
            if (ph == GATE && down) { for (int n = 0; n < chord; ++n) MusicDeviceMIDIEvent (a.au, 0x80, NOTES[n], 0, 0); down = false; } }
        abl->mBuffers[0] = { 1, (UInt32) (BLK * 4), bl.data() }; abl->mBuffers[1] = { 1, (UInt32) (BLK * 4), br.data() };
        AudioUnitRenderActionFlags fl = 0;
        const auto t0 = std::chrono::steady_clock::now();
        AudioUnitRender (a.au, &fl, &ts, 0, BLK, abl);
        acc += std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
        ts.mSampleTime += BLK; }
    printf ("%s  CPU %.2f%%\n", mode.c_str(), 100.0 * acc / secs);
    return 0; }
