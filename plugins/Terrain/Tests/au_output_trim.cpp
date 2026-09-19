// ══════════════════════════════════════════════════════════════════════════════════════════════
//  tp41 — THE PATCHER'S RULES GATE, on the REAL installed AU.
//
//   [TAP]    a device's DIRECT TAP hears the raw oscillator, never the main filter: with osc A sent
//            into a closed low-pass and Utility 1 routed to A, the rack's output is dark; with A's
//            bit set in "Utility Direct Taps" it is bright, and it NULLS against A not being in the
//            filter at all (the same dry bus, the same floats).
//   [INLINE] a flow card can sit IN the rack order: a Chop with gate 0 / blend 1 cuts holes in a
//            held note; a Reverb after it (Flow Chop In Rack + a rank below the reverb's) fills them
//            with its tail, while the same reverb BEFORE it (the old law) leaves the holes.
//   [PASS]   a device after a flow card passes the signal once: a mix-0 reverb after the Chop is the
//            Chop alone, within a small tolerance.
//
//    clang++ -O2 -std=c++17 Tests/au_patcher_rules.cpp -o /tmp/aupr \
//            -framework AudioToolbox -framework AudioUnit -framework CoreFoundation -framework CoreAudio
//    /tmp/aupr
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
    bool has (const char* name) const { return byName.find (name) != byName.end(); }
    bool set (const char* name, float norm, bool loud = true)
    {
        auto it = byName.find (name);
        if (it == byName.end()) { if (loud) printf ("    !! no parameter named '%s'\n", name); return false; }
        const auto& pi = info.at (it->second);
        return AudioUnitSetParameter (au, it->second, kAudioUnitScope_Global, 0, pi.minValue + norm * (pi.maxValue - pi.minValue), 0) == noErr;
    }
    // a CHOICE parameter is an index, never a fraction: hand it the raw index
    bool setIdx (const char* name, int idx)
    {
        auto it = byName.find (name); if (it == byName.end()) { printf ("    !! no parameter named '%s'\n", name); return false; }
        return AudioUnitSetParameter (au, it->second, kAudioUnitScope_Global, 0, (float) idx, 0) == noErr;
    }
    void note (int n, int v) { MusicDeviceMIDIEvent (au, v ? 0x90 : 0x80, (UInt32) n, (UInt32) v, 0); }
    void pump (double sec) { const double t0 = CFAbsoluteTimeGetCurrent(); while (CFAbsoluteTimeGetCurrent() - t0 < sec) CFRunLoopRunInMode (kCFRunLoopDefaultMode, 0.01, false); }
    // Renders `blocks` blocks, appending every sample to `out` (L then R per block). The timestamp
    // keeps advancing across calls — an AU that sees sample time go backwards stops rendering.
    void render (int blocks, std::vector<float>* out, bool pumpLoop = true)
    {
        std::vector<float> bl ((size_t) BLK), br ((size_t) BLK);
        AudioBufferList* abl = (AudioBufferList*) calloc (1, sizeof (AudioBufferList) + sizeof (AudioBuffer));
        for (int b = 0; b < blocks; ++b)
        {
            abl->mNumberBuffers = 2;
            abl->mBuffers[0].mNumberChannels = 1; abl->mBuffers[0].mDataByteSize = BLK * 4; abl->mBuffers[0].mData = bl.data();
            abl->mBuffers[1].mNumberChannels = 1; abl->mBuffers[1].mDataByteSize = BLK * 4; abl->mBuffers[1].mData = br.data();
            AudioUnitRenderActionFlags fl = 0; AudioTimeStamp ts {}; ts.mSampleTime = stamp; ts.mFlags = kAudioTimeStampSampleTimeValid;
            AudioUnitRender (au, &fl, &ts, 0, BLK, abl);
            stamp += BLK;
            if (out != nullptr) { out->insert (out->end(), bl.begin(), bl.end()); out->insert (out->end(), br.begin(), br.end()); }
            if (pumpLoop) CFRunLoopRunInMode (kCFRunLoopDefaultMode, 0.004, false);
        }
        free (abl);
    }
    void close() { if (au) { AudioUnitUninitialize (au); AudioComponentInstanceDispose (au); au = nullptr; } }
};
static double rmsDb (const std::vector<float>& v, size_t from = 0)
{ double e = 0; size_t n = 0; for (size_t i = from; i < v.size(); ++i) { e += (double) v[i] * v[i]; ++n; } return n ? 20.0 * std::log10 (std::sqrt (e / (double) n) + 1e-12) : -240.0; }
static double diffDb (const std::vector<float>& a, const std::vector<float>& b)
{ if (a.size() != b.size() || a.empty()) return 0.0; double e = 0; for (size_t i = 0; i < a.size(); ++i) { const double d = (double) a[i] - b[i]; e += d * d; } return 20.0 * std::log10 (std::sqrt (e / (double) a.size()) + 1e-12); }


static double hfRatio (const std::vector<float>& v, size_t from)
{   // energy of the first difference over the energy of the signal — a cheap brightness number
    double e = 0, d = 0; for (size_t i = from + 1; i < v.size(); ++i) { const double x = v[i], p = v[i - 1]; e += x * x; d += (x - p) * (x - p); }
    return e > 0 ? std::sqrt (d / e) : 0.0;
}
static std::vector<float> leftOnly (const std::vector<float>& lr)
{   // render() appends L block then R block; keep L
    std::vector<float> L; for (size_t i = 0; i + BLK <= lr.size(); i += 2 * BLK) L.insert (L.end(), lr.begin() + (long) i, lr.begin() + (long) (i + BLK)); return L;
}
static double gapDepthDb (const std::vector<float>& L, size_t from, size_t win, double* maxDb = nullptr, double* minDb = nullptr)
{
    double mx = -240, mn = 0; bool any = false;
    for (size_t i = from; i + win <= L.size(); i += win)
    {
        double e = 0; for (size_t k = 0; k < win; ++k) e += (double) L[i + k] * L[i + k];
        const double db = 20.0 * std::log10 (std::sqrt (e / (double) win) + 1e-12);
        if (! any) { mx = mn = db; any = true; } else { mx = std::max (mx, db); mn = std::min (mn, db); }
    }
    if (maxDb) *maxDb = mx; if (minDb) *minDb = mn; return mx - mn;
}
struct Tap { bool direct; bool inFilter; bool cutOut; };
// osc A (bank 0) or E (bank 1) alone, into Utility 1 by its pill, through / around the main filter, tapped or not

// ── tp51 — THE OUTPUT KNOB IS A REAL MASTER TRIM ─────────────────────────────────────────────
//    clang++ -O2 -std=c++17 Tests/au_output_trim.cpp -o /tmp/auout -framework AudioToolbox -framework AudioUnit -framework CoreFoundation -framework CoreAudio && /tmp/auout
//   Max: "the footer Output gain isn't a real MASTER OUTPUT gain, it only outputs a certain part."
//   Every configuration is rendered twice — Output at 0 dB and at -12 dB (the knob's floor) — and the
//   drop is measured. A true master gain drops EVERY path by exactly 12.0 dB.
static double levelOf (Au& a, double outDb, void (*setup) (Au&))
{
    a.set ("Output Gain", (float) ((outDb + 12.0) / 24.0));
    if (setup) setup (a);
    a.pump (0.45);
    a.render (200, nullptr);                      // FLUSH: ~2.1 s of silence, so the previous reading's reverb/delay tail is gone
    a.note (60, 100);
    std::vector<float> v; a.render (60, &v);      // ~0.64 s
    a.note (60, 0); a.render (6, nullptr);
    return rmsDb (v, v.size() / 2);               // the last half: steady state
}
static void setupBare (Au& a) { (void) a; }
static void setupRack (Au& a)
{
    a.set ("Reverb In Chain", 1.0f); a.set ("Reverb Power", 1.0f);
    a.set ("SYN_RVB_SRC_A", 1.0f);  a.set ("Reverb Mix", 1.0f);
}
static void setupSplit (Au& a)
{
    a.set ("Delay In Chain", 1.0f); a.set ("Delay Power", 1.0f);
    a.set ("SYN_DLY_SRC_A", 1.0f);
}
int main()
{
    printf ("\n== tp51 -- THE OUTPUT KNOB ==\n\n");
    struct Case { const char* what; void (*fn) (Au&); } cases[] = {
        { "the bare oscillator",            setupBare  },
        { "a Reverb in the rack",           setupRack  },
        { "a Delay in the rack", setupSplit },
    };
    for (auto& c : cases)
    {
        Au a; if (! a.open()) { printf ("  !! cannot open the AU\n"); return 1; }
        a.set ("Osc A Enable", 1.0f, false);
        const double l0 = levelOf (a, 0.0,   c.fn);
        const double l6 = levelOf (a, -6.0,  c.fn);
        const double l12= levelOf (a, -12.0, c.fn);
        a.close();
        char d[300]; snprintf (d, sizeof d, "%s: 0 dB -> %.1f, -6 dB -> %.1f, -12 dB -> %.1f dBFS  |  0->-6 moved %.2f dB, -6->-12 moved %.2f dB (want 6.00 each)", c.what, l0, l6, l12, l0 - l6, l6 - l12);
        chk (l0 > -70.0 && std::fabs ((l6 - l12) - 6.0) < 0.3, "[OUT] the knob is linear in its QUIET half (-6 to -12: the limiter is out of the way)", d);
    }
    printf ("\n  PASS %d   FAIL %d\n\n", npass, nfail);
    return nfail == 0 ? 0 : 1;
}
