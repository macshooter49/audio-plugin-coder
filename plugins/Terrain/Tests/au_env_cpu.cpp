// ══════════════════════════════════════════════════════════════════════════════════════════════
//  fb456 — WHY DOES A PAD ENVELOPE COST CPU?  Real-plugin profile (the installed AU).
//
//    clang++ -O2 -std=c++17 Tests/au_env_cpu.cpp -o /tmp/auenv \
//            -framework AudioToolbox -framework AudioUnit -framework CoreFoundation -framework CoreAudio
//
//  Max: "the higher release one, the higher attack one, the higher sustain one, the pad...
//   spikes the CPU by an extra 7-8%... when it's at its default envelope shape it's perfectly
//   fine. Why is it that every time I want to make a pad the CPU gets spiked?"
//
//  THE TWO SHAPES HE SCREENSHOTTED
//    KEY (default) : Dly 1  Atk 5    Hld 1  Dec 200  Sus 70%   Rel 300
//    PAD           : Dly 1  Atk 278  Hld 1  Dec 200  Sus 100%  Rel 1870
//
//  METHOD.  au_filter_cpu.cpp (fb441) measured a HELD chord and never sent a note-off — so it
//  could not see a release at all, which is the whole variable here. This plays a repeating
//  performance instead: a 4-note chord every 1.0 s, keys up at 0.6 s, for 12 s. That is what
//  makes releases OVERLAP, which is the mechanism under test. Each scenario changes exactly ONE
//  knob away from KEY so the cost is attributable, and the HELD pair (no note-offs at all)
//  proves whether attack/sustain cost anything once overlap is removed.
//
//  Every scenario READS BACK each parameter it set and prints the achieved value (fb373 — verify
//  the PATH, not just the engine; a param that silently didn't take would fake a clean result).
// ══════════════════════════════════════════════════════════════════════════════════════════════
#include <AudioToolbox/AudioToolbox.h>
#include <CoreFoundation/CoreFoundation.h>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <string>
#include <vector>
#include <map>
#include <chrono>
#include <algorithm>
#include <cstdlib>

static const double SR   = 48000.0;
static const int    BLK  = 512;
static const int    WARM = 94;        // 1.0 s discarded
static const int    MEAS = 1125;      // 12.0 s measured
static const int    PERIOD = 94;      // 1.0 s  — one chord per second
static const int    GATE   = 56;      // 0.6 s  — keys up

struct EnvCfg { float dly, atk, hld, dec, sus, rel; };
static const EnvCfg KEY { 1.f,   5.f, 1.f, 200.f, 0.70f,  300.f };
static const EnvCfg PAD { 1.f, 278.f, 1.f, 200.f, 1.00f, 1870.f };

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

    bool setNorm (const std::string& n, float norm)
    {
        auto it = byName.find (n); if (it == byName.end()) { printf ("  !! no param '%s'\n", n.c_str()); return false; }
        const auto& pi = info.at (it->second);
        const float v = pi.minValue + norm * (pi.maxValue - pi.minValue);
        return AudioUnitSetParameter (au, it->second, kAudioUnitScope_Global, 0, v, 0) == noErr;
    }
    float getNum (const std::string& n)
    {
        auto it = byName.find (n); if (it == byName.end()) return -12345.f;
        AudioUnitParameterValue v = 0; AudioUnitGetParameter (au, it->second, kAudioUnitScope_Global, 0, &v);
        return (float) v;
    }
    // Ask the PLUGIN what value it is actually holding, as the text it would show the user.
    // This is the only readback that cannot be fooled by my own reconstruction of JUCE's skew.
    std::string getText (const std::string& n)
    {
        auto it = byName.find (n); if (it == byName.end()) return "<no param>";
        AudioUnitParameterValue v = getNum (n);
        AudioUnitParameterStringFromValue sfv { it->second, &v, nullptr };
        UInt32 sz = sizeof sfv;
        if (AudioUnitGetProperty (au, kAudioUnitProperty_ParameterStringFromValue, kAudioUnitScope_Global, 0, &sfv, &sz) != noErr || sfv.outString == nullptr)
            return "<no string>";
        char buf[128] = {0};
        CFStringGetCString (sfv.outString, buf, sizeof buf, kCFStringEncodingUTF8);
        CFRelease (sfv.outString);
        return std::string (buf);
    }

    // JUCE NormalisableRange(start,end,interval,skew): convertTo0to1 = ((v-start)/(end-start))^skew
    static float toNorm (float v, float start, float end, float skew)
    {
        const float prop = (v - start) / (end - start);
        return prop <= 0.f ? 0.f : std::pow (prop, skew);
    }

    // Apply an envelope shape to the AMP env; VERIFY each write by reading the plugin's own text.
    bool applyEnv (const EnvCfg& e, std::string& achieved)
    {
        struct P { const char* n; float v; float lo; float hi; bool isTime; };
        const P ps[] = { {"Synth Amp Delay",   e.dly, 0.f, 10000.f, true},
                         {"Synth Amp Attack",  e.atk, 1.f, 10000.f, true},
                         {"Synth Amp Hold",    e.hld, 0.f, 10000.f, true},
                         {"Synth Amp Decay",   e.dec, 1.f, 10000.f, true},
                         {"Synth Amp Sustain", e.sus, 0.f,     1.f, false},
                         {"Synth Amp Release", e.rel, 1.f, 10000.f, true} };
        bool ok = true; char buf[512] = {0}; achieved.clear();
        for (const auto& p : ps)
        {
            setNorm (p.n, p.isTime ? toNorm (p.v, p.lo, p.hi, 0.3f) : p.v);
            const std::string txt = getText (p.n);
            const double got = strtod (txt.c_str(), nullptr);
            // time params report ms (or s past 1000 — handled by checking both readings)
            double gotMs = got;
            if (p.isTime && txt.find ('s') != std::string::npos && txt.find ("ms") == std::string::npos) gotMs = got * 1000.0;
            const double want = p.isTime ? p.v : p.v;
            const double cmp  = p.isTime ? gotMs : (got > 1.5 ? got / 100.0 : got);   // sustain may print as %
            const double tol  = std::max (0.02, std::fabs (want) * 0.02);
            if (std::fabs (cmp - want) > tol) { ok = false; snprintf (buf, sizeof buf, "[%s WANT %.3f GOT '%s'] ", p.n, want, txt.c_str()); achieved += buf; }
        }
        if (ok) { snprintf (buf, sizeof buf, "dly %s | atk %s | hld %s | dec %s | sus %s | rel %s",
                            getText("Synth Amp Delay").c_str(), getText("Synth Amp Attack").c_str(), getText("Synth Amp Hold").c_str(),
                            getText("Synth Amp Decay").c_str(), getText("Synth Amp Sustain").c_str(), getText("Synth Amp Release").c_str()); achieved = buf; }
        return ok;
    }

    // Repeating chord performance. gated=false → notes never released (held forever).
    // Returns mean CPU% and fills peakMs / p99Ms.
    double profile (bool gated, int chordSize, double& peakMs, double& p99Ms)
    {
        const int NOTES[4] = { 48, 55, 60, 64 };
        std::vector<float> bl ((size_t) BLK), br ((size_t) BLK);
        AudioBufferList* abl = (AudioBufferList*) calloc (1, sizeof (AudioBufferList) + sizeof (AudioBuffer));
        abl->mNumberBuffers = 2;
        AudioTimeStamp ts {}; ts.mFlags = kAudioTimeStampSampleTimeValid;
        double secs = 0.0;
        std::vector<double> blockMs; blockMs.reserve ((size_t) MEAS);
        bool down = false;
        for (int b = 0; b < WARM + MEAS; ++b)
        {
            const int ph = b % PERIOD;
            if (ph == 0)      { for (int n = 0; n < chordSize; ++n) MusicDeviceMIDIEvent (au, 0x90, NOTES[n], 100, 0); down = true; }
            if (gated && ph == GATE && down) { for (int n = 0; n < chordSize; ++n) MusicDeviceMIDIEvent (au, 0x80, NOTES[n], 0, 0); down = false; }
            abl->mBuffers[0] = { 1, (UInt32) (BLK * 4), bl.data() };
            abl->mBuffers[1] = { 1, (UInt32) (BLK * 4), br.data() };
            AudioUnitRenderActionFlags fl = 0;
            const auto t0 = std::chrono::steady_clock::now();
            AudioUnitRender (au, &fl, &ts, 0, BLK, abl);
            const auto t1 = std::chrono::steady_clock::now();
            const double ms = std::chrono::duration<double, std::milli> (t1 - t0).count();
            if (b >= WARM) { secs += ms / 1000.0; blockMs.push_back (ms); }
            ts.mSampleTime += BLK;
        }
        free (abl);
        std::sort (blockMs.begin(), blockMs.end());
        peakMs = blockMs.empty() ? 0.0 : blockMs.back();
        p99Ms  = blockMs.empty() ? 0.0 : blockMs[(size_t) (blockMs.size() * 99 / 100)];
        return 100.0 * secs / ((double) MEAS * BLK / SR);
    }
};

struct Scn { const char* label; EnvCfg env; bool gated; int chord; };

int main()
{
    printf ("\n══ fb456 — PAD-ENVELOPE CPU PROFILE (installed AU, editor closed, 4-note chord/sec, keys up at 0.6s) ══\n\n");

    EnvCfg relOnly = KEY; relOnly.rel = PAD.rel;     // KEY + long release only
    EnvCfg atkOnly = KEY; atkOnly.atk = PAD.atk;     // KEY + long attack only
    EnvCfg susOnly = KEY; susOnly.sus = PAD.sus;     // KEY + full sustain only

    const Scn S[] = {
        { "E0  KEY  (default shape)                       gated 4-note",  KEY,     true,  4 },
        { "E1  PAD  (atk278 sus100 rel1870)               gated 4-note",  PAD,     true,  4 },
        { "E2  KEY + RELEASE 1870 only                    gated 4-note",  relOnly, true,  4 },
        { "E3  KEY + ATTACK 278 only                      gated 4-note",  atkOnly, true,  4 },
        { "E4  KEY + SUSTAIN 100% only                    gated 4-note",  susOnly, true,  4 },
        { "E5  KEY  HELD (no note-offs — zero overlap)    held  4-note",  KEY,     false, 4 },
        { "E6  PAD  HELD (no note-offs — zero overlap)    held  4-note",  PAD,     false, 4 },
        { "E7  KEY  gated, 1 note (per-voice scaling)     gated 1-note",  KEY,     true,  1 },
        { "E8  PAD  gated, 1 note (per-voice scaling)     gated 1-note",  PAD,     true,  1 },
    };
    const int N = (int) (sizeof (S) / sizeof (S[0]));

    std::vector<double> best ((size_t) N, 1e9), pk ((size_t) N, 0.0), p99 ((size_t) N, 0.0);
    std::vector<std::string> ach ((size_t) N);
    bool allOk = true;

    for (int pass = 0; pass < 2; ++pass)
        for (int k = 0; k < N; ++k)
        {
            AU a; if (! a.open()) return 2;
            std::string got;
            if (! a.applyEnv (S[k].env, got)) { printf ("  !! PARAM PATH FAILED  %s  %s\n", S[k].label, got.c_str()); allOk = false; }
            ach[(size_t) k] = got;
            double pkMs = 0, p9 = 0;
            const double pct = a.profile (S[k].gated, S[k].chord, pkMs, p9);
            a.close();
            if (pct < best[(size_t) k]) { best[(size_t) k] = pct; pk[(size_t) k] = pkMs; p99[(size_t) k] = p9; }
        }

    const double base = best[0];
    const double blockBudgetMs = 1000.0 * BLK / SR;
    printf ("  block budget = %.2f ms (512 @ 48k)\n\n", blockBudgetMs);
    printf ("   CPU%%    vs E0    peak ms   p99 ms   scenario\n");
    printf ("  ─────────────────────────────────────────────────────────────────────────────\n");
    for (int k = 0; k < N; ++k)
        printf ("  %6.2f%%  %+6.2f   %7.3f  %7.3f   %s\n", best[(size_t) k], best[(size_t) k] - base, pk[(size_t) k], p99[(size_t) k], S[k].label);
    printf ("\n  achieved envelope values (the PLUGIN'S OWN text readback — fb373):\n");
    for (int k = 0; k < N; ++k) printf ("    %-6.6s %s\n", S[k].label, ach[(size_t) k].c_str());
    printf ("\n  %s\n\n", allOk ? "param path OK on every scenario" : "!! PARAM PATH FAILED — results above are NOT trustworthy");
    return allOk ? 0 : 1;
}
