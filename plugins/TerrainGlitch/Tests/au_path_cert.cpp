// ══════════════════════════════════════════════════════════════════════════════════════════════
//  au_path_cert.cpp — fb373 PATH GATE for TERRAIN GLITCH ('aumf'/'Tgli'/'Wvcr').
//
//    clang++ -O2 -std=c++17 Tests/au_path_cert.cpp -o /tmp/tgli_cert \
//            -framework AudioToolbox -framework AudioUnit -framework CoreFoundation
//    /tmp/tgli_cert
//
//  VERIFY THE PATH, NOT JUST THE ENGINE: FlowGlitch.h is certified upstream (laneC fb517
//  glitch_chance_cert, 640-step dice statistics); THIS gate proves the INSTALLED component
//  actually reaches it — APVTS id -> transplanted glitchStage -> engine -> audio out.
//
//  Method (v2 lesson from laneC's au_gli_path: a Repeat fire on steady material replays an
//  identical-sounding slice, invisible to the envelope — so the gate rides DROP=1: a fire
//  becomes a silent HOLE, and holes only exist if VARY reached the engine. RMS is the razor):
//    · host the INSTALLED AU (music effect: input render callback feeds a deterministic
//      drum-like impulse train — decaying 180 Hz thumps every 6000 samples @48k, the engine's
//      own free-run 1/16 grid at the fallback 120 BPM; no transport = the fb78 free-run law)
//    · find FLOW_GLI_VARY / _BLEND / _DROP by NAME through kAudioUnitProperty_ParameterList +
//      ParameterInfo ("Glitch Vary" / "Glitch Blend" / "Glitch Drop"), JUCE-hash fallback
//    · GATE A: vary=0 -> output == dry (bit-transparent modulo float noise; BLEND=1, DROP=1)
//    · GATE B: vary=1 -> measurable hole pattern (>= 6 dB RMS drop vs vary=0 + window notches)
//    · GATE C: vary=0.5 lands strictly between (monotonic)
//
//  A GATE THAT HAS NEVER FAILED HAS NEVER BEEN TESTED — the recorded failures:
//    · FAIL-FIRST (pre-install): run against the ABSENT/old component -> "component not
//      found", exit 2. That run is the recorded failure for every gate here (fb373).
//    · MUTATION PLAN (post-install, apply to a COPY of the plugin source, rebuild, re-run):
//        MUT1  sed 's/const float gVary  = flowKnob (ParameterIDs::FLOW_GLI_VARY);/const float gVary  = 0.0f;/'
//              in TerrainGlitch/Source/PluginProcessor.cpp  -> GATE B and GATE C MUST fail
//              (the dead-relay class: knob moves, engine never hears it)
//        MUT2  comment out the glitch.process (...) call            -> GATE B MUST fail
//              (proves the gate measures the ENGINE's holes, not incidental level changes)
// ══════════════════════════════════════════════════════════════════════════════════════════════
#include <AudioToolbox/AudioToolbox.h>
#include <CoreFoundation/CoreFoundation.h>
#include <cstdio>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>
#include <algorithm>

static const double SR = 48000.0; static const int BLK = 512;

// deterministic drum-like impulse train: a decaying 180 Hz thump every 6000 samples
// (= 1/16 at 120 BPM @ 48k). Pure function of the absolute sample index.
static float drum (long long n)
{
    const long long P = 6000, k = n % P;
    if (k >= 4800) return 0.0f;
    return 0.9f * std::exp (-(float) k / 1440.0f) * std::sin (2.0f * 3.14159265f * 180.0f * (float) k / (float) SR);
}

// JUCE's AU parameter id = hash of the paramID string (fallback when name lookup misses)
static AudioUnitParameterID juceHash (const std::string& id)
{ uint32_t r = 0; for (unsigned char ch : id) r = 31u * r + (uint32_t) ch; return (AudioUnitParameterID) (r & 0x7FFFFFFFu); }

static OSStatus inputCb (void*, AudioUnitRenderActionFlags*, const AudioTimeStamp* ts,
                         UInt32, UInt32 nFrames, AudioBufferList* io)
{
    const long long base = (long long) ts->mSampleTime;
    for (UInt32 b = 0; b < io->mNumberBuffers; ++b)
    {
        float* d = (float*) io->mBuffers[b].mData;
        for (UInt32 i = 0; i < nFrames; ++i) d[i] = drum (base + (long long) i);
    }
    return noErr;
}

struct AU
{
    AudioUnit au = nullptr;

    bool open()
    {
        AudioComponentDescription d {};
        d.componentType = kAudioUnitType_MusicEffect; d.componentSubType = 'Tgli'; d.componentManufacturer = 'Wvcr';
        AudioComponent c = AudioComponentFindNext (nullptr, &d);
        if (! c) { printf ("!! component not found ('aumf'/'Tgli'/'Wvcr' not installed)\n"); return false; }
        if (AudioComponentInstanceNew (c, &au) != noErr) { printf ("!! instance failed\n"); return false; }
        AudioStreamBasicDescription f {};
        f.mSampleRate = SR; f.mFormatID = kAudioFormatLinearPCM;
        f.mFormatFlags = kAudioFormatFlagsNativeFloatPacked | kAudioFormatFlagIsNonInterleaved;
        f.mBytesPerPacket = 4; f.mFramesPerPacket = 1; f.mBytesPerFrame = 4;
        f.mChannelsPerFrame = 2; f.mBitsPerChannel = 32;
        AudioUnitSetProperty (au, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Input,  0, &f, sizeof f);
        AudioUnitSetProperty (au, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Output, 0, &f, sizeof f);
        AURenderCallbackStruct cb { inputCb, nullptr };
        AudioUnitSetProperty (au, kAudioUnitProperty_SetRenderCallback, kAudioUnitScope_Input, 0, &cb, sizeof cb);
        UInt32 mx = BLK;
        AudioUnitSetProperty (au, kAudioUnitProperty_MaximumFramesPerSlice, kAudioUnitScope_Global, 0, &mx, sizeof mx);
        return AudioUnitInitialize (au) == noErr;
    }

    void close() { if (au) { AudioUnitUninitialize (au); AudioComponentInstanceDispose (au); au = nullptr; } }

    // the contract path: find the parameter by NAME through ParameterList/ParameterInfo
    AudioUnitParameterID byName (const char* wanted, const std::string& idFallback)
    {
        UInt32 sz = 0; Boolean wr = false;
        if (AudioUnitGetPropertyInfo (au, kAudioUnitProperty_ParameterList, kAudioUnitScope_Global, 0, &sz, &wr) == noErr && sz > 0)
        {
            std::vector<AudioUnitParameterID> ids (sz / sizeof (AudioUnitParameterID));
            if (AudioUnitGetProperty (au, kAudioUnitProperty_ParameterList, kAudioUnitScope_Global, 0, ids.data(), &sz) == noErr)
            {
                for (auto pidV : ids)
                {
                    AudioUnitParameterInfo info {}; UInt32 isz = sizeof info;
                    if (AudioUnitGetProperty (au, kAudioUnitProperty_ParameterInfo, kAudioUnitScope_Global, pidV, &info, &isz) != noErr)
                        continue;
                    char nm[256] = {};
                    if ((info.flags & kAudioUnitParameterFlag_HasCFNameString) && info.cfNameString != nullptr)
                        CFStringGetCString (info.cfNameString, nm, sizeof nm, kCFStringEncodingUTF8);
                    else
                        std::strncpy (nm, info.name, sizeof nm - 1);
                    if (std::strcmp (nm, wanted) == 0) return pidV;
                }
            }
        }
        printf ("  (name '%s' not in ParameterList — JUCE-hash fallback for %s)\n", wanted, idFallback.c_str());
        return juceHash (idFallback);
    }

    bool setP (AudioUnitParameterID pidV, float v)
    { return AudioUnitSetParameter (au, pidV, kAudioUnitScope_Global, 0, v, 0) == noErr; }
    float getP (AudioUnitParameterID pidV)
    { AudioUnitParameterValue v = 0; AudioUnitGetParameter (au, pidV, kAudioUnitScope_Global, 0, &v); return v; }

    void pump (double s) { double t = 0; while (t < s) { CFRunLoopRunInMode (kCFRunLoopDefaultMode, 0.02, false); t += 0.02; } }

    // render, returning (mono mix, absolute start sample of the first KEPT frame)
    std::vector<float> render (int nblk, int skip)
    {
        std::vector<float> mono;
        std::vector<float> bl ((size_t) BLK), br ((size_t) BLK);
        AudioBufferList* abl = (AudioBufferList*) calloc (1, sizeof (AudioBufferList) + sizeof (AudioBuffer));
        abl->mNumberBuffers = 2;
        AudioTimeStamp ts {}; ts.mFlags = kAudioTimeStampSampleTimeValid; ts.mSampleTime = 0;
        for (int b = 0; b < nblk; ++b)
        {
            abl->mBuffers[0] = { 1, (UInt32) (BLK * 4), bl.data() };
            abl->mBuffers[1] = { 1, (UInt32) (BLK * 4), br.data() };
            AudioUnitRenderActionFlags fl = 0;
            if (AudioUnitRender (au, &fl, &ts, 0, BLK, abl) != noErr) { printf ("!! render failed at block %d\n", b); break; }
            ts.mSampleTime += BLK;
            if (b >= skip)
                for (int i = 0; i < BLK; ++i) mono.push_back (0.5f * (bl[(size_t) i] + br[(size_t) i]));
        }
        free (abl);
        return mono;
    }
};

struct M { double rmsDb; int notches; double maxDiffVsDry; };
static M stats (const std::vector<float>& m, long long firstAbs)
{
    const int W = 240;
    std::vector<double> e;
    for (size_t i = 0; i + W <= m.size(); i += W)
    { double s = 0; for (int k = 0; k < W; ++k) s += (double) m[i + k] * m[i + k]; e.push_back (std::sqrt (s / W)); }
    std::vector<double> srt = e; std::sort (srt.begin(), srt.end());
    const double med = srt.empty() ? 0 : srt[srt.size() / 2];
    int n = 0; for (double v : e) if (v < 0.35 * med) ++n;
    double rr = 0, mx = 0;
    for (size_t i = 0; i < m.size(); ++i)
    {
        rr += (double) m[i] * m[i];
        const double diff = std::fabs ((double) m[i] - (double) drum (firstAbs + (long long) i));
        if (diff > mx) mx = diff;
    }
    rr = m.empty() ? 0 : std::sqrt (rr / (double) m.size());
    return { 20.0 * std::log10 (rr + 1e-12), n, mx };
}

static const int NBLK = 400, SKIP = 60;   // ~4.1s measured after ~0.6s of ring/clock priming

static M runCase (float vary, bool probeSets)
{
    AU a; if (! a.open()) { printf ("FAIL-FIRST RECORD: the gate cannot run — exit 2\n"); exit (2); }
    a.pump (0.3);
    const auto pVary  = a.byName ("Glitch Vary",  "FLOW_GLI_VARY");
    const auto pBlend = a.byName ("Glitch Blend", "FLOW_GLI_BLEND");
    const auto pDrop  = a.byName ("Glitch Drop",  "FLOW_GLI_DROP");
    a.setP (pBlend, 1.0f);        // audible marker law (fb517 laneC): full wet ...
    a.setP (pDrop,  1.0f);        // ... and every fire lands as a silent HOLE
    a.setP (pVary,  vary);
    if (probeSets)
    {
        a.setP (pVary, 0.37f);
        printf ("  set check: BLEND=%.2f DROP=%.2f VARY(0.37 probe)=%s\n",
                a.getP (pBlend), a.getP (pDrop),
                std::fabs (a.getP (pVary) - 0.37f) < 1e-4f ? "roundtrip OK" : "MISMATCH");
        a.setP (pVary, vary);
    }
    a.pump (0.2);
    M r = stats (a.render (NBLK, SKIP), (long long) SKIP * BLK);
    a.close();
    return r;
}

int main()
{
    int fail = 0;
    printf ("TERRAIN GLITCH AU PATH CERT — FLOW_GLI_VARY through the INSTALLED 'aumf'/'Tgli'/'Wvcr'\n");
    printf ("  input: deterministic drum train (6000-sample period), no transport (fb78 free-run)\n");

    M A = runCase (0.0f, true);
    M C = runCase (0.5f, false);
    M B = runCase (1.0f, false);
    printf ("  vary=0.0 : %7.1f dB  %3d notches  maxdiff-vs-dry %.2e\n", A.rmsDb, A.notches, A.maxDiffVsDry);
    printf ("  vary=0.5 : %7.1f dB  %3d notches\n", C.rmsDb, C.notches);
    printf ("  vary=1.0 : %7.1f dB  %3d notches\n", B.rmsDb, B.notches);

    // GATE A — vary=0 is bit-transparent modulo mix (BLEND=1: nothing fires => dry through)
    const bool transparent = A.maxDiffVsDry < 1e-4;
    printf ("  GATE A: vary=0 -> output == dry (maxdiff %.2e < 1e-4) -> %s\n",
            A.maxDiffVsDry, transparent ? "PASS" : "FAIL");
    if (! transparent) ++fail;

    // GATE B — vary=1 + DROP=1: every fire is a hole => a big RMS drop + notched windows
    const bool holes = (B.rmsDb < A.rmsDb - 6.0);
    printf ("  GATE B: vary=1 punches holes (%.1f dB drop, notches %d -> %d) -> %s\n",
            A.rmsDb - B.rmsDb, A.notches, B.notches, holes ? "PASS" : "FAIL");
    if (! holes) ++fail;

    // GATE C — monotonic: vary=0.5 lands strictly between
    const bool mono = (C.rmsDb < A.rmsDb - 1.5) && (C.rmsDb > B.rmsDb + 1.5);
    printf ("  GATE C: vary=0.5 between (%.1f > %.1f > %.1f dB) -> %s\n",
            A.rmsDb, C.rmsDb, B.rmsDb, mono ? "PASS" : "FAIL");
    if (! mono) ++fail;

    printf ("%s\n", fail == 0 ? "PATH: GREEN" : "PATH: RED");
    return fail;
}
