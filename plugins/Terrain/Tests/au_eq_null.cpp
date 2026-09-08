// ══════════════════════════════════════════════════════════════════════════════════════════════
//  fb456 — MASTER-EQ NULL TEST.  Proves the coefficient-rebuild optimisation is INAUDIBLE.
//
//    clang++ -O2 -std=c++17 Tests/au_eq_null.cpp -o /tmp/aueqnull \
//            -framework AudioToolbox -framework AudioUnit -framework CoreFoundation -framework CoreAudio
//    ./aueqnull <out.raw>          → renders a scripted performance, writes stereo float32
//
//  ParametricEQ::updateAllCoefficients() used to rebuild all 7 bells + both cut cascades EVERY
//  SAMPLE, heap-allocating a juce Coefficients object each time. The fix rebuilds only when a
//  value actually moved. That is only safe if the audio is UNCHANGED — so capture this render
//  from the OLD binary, rebuild, capture again, and null the two files.
//
//  The script deliberately drives every branch the optimisation introduced, especially the ones
//  that could leave STALE coefficients behind:
//     · settled (nothing moving)          — the case the fast path skips
//     · a gain ramp / a freq sweep        — rebuild on every sample, as before
//     · bypass a band, then UN-BYPASS it  — while bypassed we skip the build, so the cache
//                                           would look "unchanged"; the dirty flag must fire
//     · HP/LP enable + SLOPE change       — the stage COUNT changes, not just a value
//     · solo on / off, master bypass      — the two other per-sample allocation sites
//  A test that only rendered the default patch would pass on a broken implementation (fb453 —
//  a gate that cannot fail is decoration).
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

static const double SR = 48000.0; static const int BLK = 512; static const int TOTAL = 620;

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
    bool has (const std::string& n) { return byName.count (n) > 0; }
    void setNorm (const std::string& n, float norm) { auto it = byName.find (n); if (it == byName.end()) { printf ("  !! no param '%s'\n", n.c_str()); return; }
        const auto& pi = info.at (it->second); AudioUnitSetParameter (au, it->second, kAudioUnitScope_Global, 0, pi.minValue + norm * (pi.maxValue - pi.minValue), 0); }
    void setRaw (const std::string& n, float v) { auto it = byName.find (n); if (it == byName.end()) { printf ("  !! no param '%s'\n", n.c_str()); return; }
        AudioUnitSetParameter (au, it->second, kAudioUnitScope_Global, 0, v, 0); }
};

int main (int argc, char** argv)
{
    const char* outPath = argc > 1 ? argv[1] : "eqnull.raw";
    AU a; if (! a.open()) return 2;

    // sanity: every param the script touches must exist, or the script silently tests nothing
    const char* needed[] = { "EQ Bypass", "EQ HP Freq", "EQ HP Slope", "EQ HP Bypass", "EQ LP Freq",
                             "EQ LP Slope", "EQ LP Bypass", "EQ B3 Freq", "EQ B3 Gain", "EQ B3 Q", "EQ B3 Bypass" };
    bool ok = true;
    for (auto* n : needed) if (! a.has (n)) { printf ("  !! MISSING PARAM '%s'\n", n); ok = false; }
    if (! ok) { printf ("  !! script cannot run — aborting rather than writing a meaningless file\n"); return 3; }

    const int NOTES[4] = { 48, 55, 60, 64 };
    std::vector<float> bl ((size_t) BLK), br ((size_t) BLK);
    std::vector<float> all; all.reserve ((size_t) TOTAL * BLK * 2);
    AudioBufferList* abl = (AudioBufferList*) calloc (1, sizeof (AudioBufferList) + sizeof (AudioBuffer)); abl->mNumberBuffers = 2;
    AudioTimeStamp ts {}; ts.mFlags = kAudioTimeStampSampleTimeValid;

    for (int b = 0; b < TOTAL; ++b)
    {
        // ── notes: a chord every 94 blocks, keys up at 56 ──
        const int ph = b % 94;
        if (ph == 0)  for (int n = 0; n < 4; ++n) MusicDeviceMIDIEvent (a.au, 0x90, NOTES[n], 100, 0);
        if (ph == 56) for (int n = 0; n < 4; ++n) MusicDeviceMIDIEvent (a.au, 0x80, NOTES[n], 0, 0);

        // ── the EQ automation script ──
        if (b == 90)  a.setNorm ("EQ B3 Gain", 0.75f);                       // settled → ramp
        if (b >= 130 && b < 170) a.setNorm ("EQ B3 Freq", 0.30f + 0.010f * (float) (b - 130));  // continuous sweep
        if (b == 180) a.setRaw  ("EQ B3 Bypass", 1.0f);                      // bypass (skip builds)
        if (b == 220) a.setRaw  ("EQ B3 Bypass", 0.0f);                      // UN-bypass — stale-coefficient trap
        if (b == 250) { a.setNorm ("EQ HP Freq", 0.35f); a.setRaw ("EQ HP Slope", 0.0f); a.setRaw ("EQ HP Bypass", 0.0f); }
        if (b == 280) a.setRaw  ("EQ HP Slope", 2.0f);                       // stage COUNT change
        if (b == 310) { a.setNorm ("EQ LP Freq", 0.70f); a.setRaw ("EQ LP Slope", 1.0f); a.setRaw ("EQ LP Bypass", 0.0f); }
        if (b >= 340 && b < 380) a.setNorm ("EQ LP Freq", 0.70f - 0.008f * (float) (b - 340));  // sweep down
        if (b == 380 && a.has ("EQ Solo")) a.setRaw ("EQ Solo", 4.0f);
        if (b == 420 && a.has ("EQ Solo")) a.setRaw ("EQ Solo", -1.0f);
        if (b == 450) a.setRaw  ("EQ Bypass", 1.0f);                         // master bypass
        if (b == 470) a.setRaw  ("EQ Bypass", 0.0f);

        abl->mBuffers[0] = { 1, (UInt32) (BLK * 4), bl.data() };
        abl->mBuffers[1] = { 1, (UInt32) (BLK * 4), br.data() };
        AudioUnitRenderActionFlags fl = 0;
        AudioUnitRender (a.au, &fl, &ts, 0, BLK, abl);
        ts.mSampleTime += BLK;
        all.insert (all.end(), bl.begin(), bl.end());
        all.insert (all.end(), br.begin(), br.end());
    }
    free (abl);

    FILE* f = fopen (outPath, "wb");
    if (! f) { printf ("!! cannot write %s\n", outPath); return 4; }
    fwrite (all.data(), sizeof (float), all.size(), f);
    fclose (f);

    double sum = 0.0, peak = 0.0; long nz = 0;
    for (float v : all) { sum += (double) v * v; peak = std::max (peak, (double) std::fabs (v)); if (v != 0.f) ++nz; }
    const double rms = std::sqrt (sum / (double) all.size());
    printf ("wrote %s  samples=%zu  nonzero=%ld  rms=%.9f (%.2f dBFS)  peak=%.9f\n",
            outPath, all.size(), nz, rms, 20.0 * std::log10 (std::max (rms, 1e-12)), peak);
    if (nz == 0) { printf ("!! OUTPUT IS DIGITAL SILENCE — the null would be vacuous. FAIL\n"); return 5; }
    return 0;
}
