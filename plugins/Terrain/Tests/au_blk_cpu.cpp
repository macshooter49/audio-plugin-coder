// ══════════════════════════════════════════════════════════════════════════════════════════════
//  fb492 — DOES IDLE DSP COST STILL SCALE WITH THE HOST'S BLOCK SIZE?
//
//    clang++ -O2 -std=c++17 Tests/au_blk_cpu.cpp -o /tmp/aublk \
//            -framework AudioToolbox -framework AudioUnit -framework CoreFoundation
//
//  THE FINDING THIS EXISTS TO TEST (fb491). Max's Windows meter reported `blk 45 @ 44100` while
//  FL Studio's buffer was set to 512: FL subdivides its device buffer and calls the plugin with
//  ~45-88 sample blocks, i.e. 500-1000 processBlock invocations per second instead of the 86 a
//  512 buffer implies. Terrain ran ~900 lines of pure parameter gathering on EVERY call, so every
//  fixed per-call cost was multiplied ~11x. That is why the same code measured 3.33% on a Mac
//  standalone (normal buffer) and 20-40% inside FL.
//
//  THE TEST. Render the SAME amount of audio, idle, at several block sizes and report CPU as a
//  share of one core. The block size changes ONLY how often the host calls us — the audio work is
//  identical — so:
//      before fb492 : cost(45) >> cost(512)   (the gather rode the call rate)
//      after  fb492 : cost(45) ~= cost(512)   (the gather runs at a fixed ~172 Hz)
//  A flat row across block sizes IS the fix. A sloped one says it is not done.
//
//  No notes are played: this is the state Max reported ("no preset, just a sine, no notes"), and
//  it isolates fixed per-call cost from anything the voices do.
#include <AudioToolbox/AudioToolbox.h>
#include <chrono>
#include <cstdio>
#include <vector>

static const double SR = 44100.0;
static const double SECONDS = 6.0;

static double measure (int blk, bool* ok)
{
    *ok = false;
    AudioComponentDescription d {};
    d.componentType = kAudioUnitType_MusicDevice; d.componentSubType = 'Tern'; d.componentManufacturer = 'Wvcr';
    AudioComponent c = AudioComponentFindNext (nullptr, &d);
    AudioUnit au = nullptr;
    if (! c || AudioComponentInstanceNew (c, &au) != noErr) { printf ("  !! AU not found\n"); return 0.0; }

    AudioStreamBasicDescription f {};
    f.mSampleRate = SR; f.mFormatID = kAudioFormatLinearPCM;
    f.mFormatFlags = kAudioFormatFlagsNativeFloatPacked | kAudioFormatFlagIsNonInterleaved;
    f.mBytesPerPacket = 4; f.mFramesPerPacket = 1; f.mBytesPerFrame = 4; f.mChannelsPerFrame = 2; f.mBitsPerChannel = 32;
    AudioUnitSetProperty (au, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Output, 0, &f, sizeof f);
    UInt32 mx = (UInt32) blk;   // must be set BEFORE Initialize
    AudioUnitSetProperty (au, kAudioUnitProperty_MaximumFramesPerSlice, kAudioUnitScope_Global, 0, &mx, sizeof mx);
    if (AudioUnitInitialize (au) != noErr) { printf ("  !! init failed\n"); AudioComponentInstanceDispose (au); return 0.0; }

    std::vector<float> bl ((size_t) blk), br ((size_t) blk);
    AudioBufferList* abl = (AudioBufferList*) calloc (1, sizeof (AudioBufferList) + sizeof (AudioBuffer));
    abl->mNumberBuffers = 2;
    AudioTimeStamp ts {}; ts.mFlags = kAudioTimeStampSampleTimeValid;

    const int total = (int) (SR * SECONDS / blk);
    const int warm  = (int) (SR * 1.0 / blk);
    double dspSec = 0.0;
    for (int b = 0; b < warm + total; ++b)
    {
        abl->mBuffers[0] = { 1, (UInt32) (blk * 4), bl.data() };
        abl->mBuffers[1] = { 1, (UInt32) (blk * 4), br.data() };
        AudioUnitRenderActionFlags fl = 0;
        const auto t0 = std::chrono::steady_clock::now();
        AudioUnitRender (au, &fl, &ts, 0, (UInt32) blk, abl);
        const auto t1 = std::chrono::steady_clock::now();
        if (b >= warm) dspSec += std::chrono::duration<double> (t1 - t0).count();
        ts.mSampleTime += blk;
    }
    free (abl);
    AudioUnitUninitialize (au); AudioComponentInstanceDispose (au);
    *ok = true;
    const double audioSec = (double) total * blk / SR;
    return 100.0 * dspSec / audioSec;
}

int main()
{
    printf ("\n  IDLE DSP cost vs HOST BLOCK SIZE (no notes, %.0f s per point, %.0f Hz)\n\n", SECONDS, SR);
    const int sizes[5] = { 45, 88, 128, 256, 512 };
    double v[5] = {0};
    for (int i = 0; i < 5; ++i)
    {
        bool ok = false;
        v[i] = measure (sizes[i], &ok);
        printf ("    blk %4d  (%6.0f calls/s)   %6.2f%% of one core%s\n",
                sizes[i], SR / sizes[i], v[i], ok ? "" : "   <- FAILED");
    }
    if (v[4] > 0.0)
    {
        const double ratio = v[0] / v[4];
        printf ("\n    cost(45) / cost(512) = %.2fx\n", ratio);
        printf ("    %s\n\n", ratio < 1.6 ? "FLAT ENOUGH — the gather no longer rides the host's call rate."
                                          : "STILL SLOPED — fixed per-call cost still dominates.");
        return ratio < 1.6 ? 0 : 1;
    }
    return 1;
}
