// ══════════════════════════════════════════════════════════════════════════════════════════════
//  tp18 — THE GOLDEN LAW FOR A WIDENED POOL: a patch that uses no new instance must render
//  BIT-IDENTICALLY. The scope doc mandates this and the harness it cites (scratchpad/abS.sh)
//  evaporated, so here it is, committed.
//
//    clang++ -O2 -std=c++17 Tests/pool_identity.cpp -o /tmp/poolid \
//            -framework AudioToolbox -framework AudioUnit -framework CoreFoundation -framework CoreAudio
//    /tmp/poolid write  out.raw     (on the OLD binary)
//    /tmp/poolid check  out.raw     (on the NEW binary)  -> exit 1 on any differing sample
//
//  Deterministic by construction: TERRAIN_DETERMINISTIC seeds the random streams, one fresh AU,
//  a fixed chord, a fixed number of 512-frame blocks, defaults everywhere else.
// ══════════════════════════════════════════════════════════════════════════════════════════════
#include <AudioToolbox/AudioToolbox.h>
#include <CoreFoundation/CoreFoundation.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <vector>
static const double SR = 48000.0; static const int BLK = 512, BLOCKS = 240;   // 2.56 s
int main (int argc, char** argv)
{
    const bool write = (argc > 1 && std::strcmp (argv[1], "write") == 0);
    const char* path = (argc > 2) ? argv[2] : "/tmp/pool_identity.raw";
    setenv ("TERRAIN_DETERMINISTIC", "1", 1);
    AudioComponentDescription d {}; d.componentType = kAudioUnitType_MusicDevice;
    d.componentSubType = 'Tern'; d.componentManufacturer = 'Wvcr';
    AudioComponent c = AudioComponentFindNext (nullptr, &d); AudioUnit au = nullptr;
    if (! c || AudioComponentInstanceNew (c, &au) != noErr) { printf ("no AU\n"); return 2; }
    AudioStreamBasicDescription f {}; f.mSampleRate = SR; f.mFormatID = kAudioFormatLinearPCM;
    f.mFormatFlags = kAudioFormatFlagsNativeFloatPacked | kAudioFormatFlagIsNonInterleaved;
    f.mBytesPerPacket = 4; f.mFramesPerPacket = 1; f.mBytesPerFrame = 4; f.mChannelsPerFrame = 2; f.mBitsPerChannel = 32;
    AudioUnitSetProperty (au, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Output, 0, &f, sizeof f);
    UInt32 mx = BLK; AudioUnitSetProperty (au, kAudioUnitProperty_MaximumFramesPerSlice, kAudioUnitScope_Global, 0, &mx, sizeof mx);
    if (AudioUnitInitialize (au) != noErr) { printf ("init failed\n"); return 2; }
    MusicDeviceMIDIEvent (au, 0x90, 48, 100, 0); MusicDeviceMIDIEvent (au, 0x90, 55, 100, 0);
    MusicDeviceMIDIEvent (au, 0x90, 60, 100, 0); MusicDeviceMIDIEvent (au, 0x90, 64, 100, 0);
    std::vector<float> bl ((size_t) BLK), br ((size_t) BLK), all;
    AudioBufferList* abl = (AudioBufferList*) calloc (1, sizeof (AudioBufferList) + sizeof (AudioBuffer));
    abl->mNumberBuffers = 2; AudioTimeStamp ts {}; ts.mFlags = kAudioTimeStampSampleTimeValid;
    for (int b = 0; b < BLOCKS; ++b) {
        abl->mBuffers[0] = { 1, (UInt32) (BLK * 4), bl.data() };
        abl->mBuffers[1] = { 1, (UInt32) (BLK * 4), br.data() };
        AudioUnitRenderActionFlags fl = 0;
        AudioUnitRender (au, &fl, &ts, 0, BLK, abl); ts.mSampleTime += BLK;
        all.insert (all.end(), bl.begin(), bl.end()); all.insert (all.end(), br.begin(), br.end());
    }
    if (write) { FILE* fp = fopen (path, "wb"); fwrite (all.data(), 4, all.size(), fp); fclose (fp);
                 printf ("  wrote %zu samples to %s\n", all.size(), path); return 0; }
    FILE* fp = fopen (path, "rb"); if (! fp) { printf ("  no reference at %s — run `write` on the old binary first\n", path); return 2; }
    std::vector<float> ref (all.size()); const size_t got = fread (ref.data(), 4, ref.size(), fp); fclose (fp);
    if (got != all.size()) { printf ("  FAIL length %zu vs %zu\n", got, all.size()); return 1; }
    size_t diff = 0; double worst = 0.0;
    for (size_t i = 0; i < all.size(); ++i) { if (all[i] != ref[i]) { ++diff; worst = std::fmax (worst, std::fabs ((double) all[i] - ref[i])); } }
    printf ("  %s — %zu/%zu samples differ, worst |delta| %.3e\n", diff ? "FAIL" : "BIT-IDENTICAL", diff, all.size(), worst);
    return diff ? 1 : 0;
}
