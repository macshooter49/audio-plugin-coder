// ══════════════════════════════════════════════════════════════════════════════════════════════
//  fb441 — WHY DOES THE MAIN FILTER COST CPU?  Real-plugin profile (the installed AU).
//
//    clang++ -O2 -std=c++17 Tests/au_filter_cpu.cpp -o /tmp/aucpu \
//            -framework AudioToolbox -framework AudioUnit -framework CoreFoundation -framework CoreAudio
//
//  Max: "the main filter... whenever it's latched onto an ABCD or SN, not even just all of them,
//  but one of them... it drags up my CPU at least by like 7 or 8%... I also have some LFO
//  automation going on... it could be the visualizer... figure out why."
//
//  Method: one fresh AU per scenario, a 4-note chord, 4 s rendered in 512-sample blocks after a
//  0.5 s warm-up; CPU% = wall time inside AudioUnitRender / audio time. The scenarios isolate the
//  suspects one at a time: routing alone, modulated cutoff (Erosion = per-sample drift = the
//  worst case an LFO can be), the send-mirror filter pairs (reverb/delay routed), the pooled
//  rack devices (each spawns its own pair per voice), unison, and oversampled vs. not.
//  The editor is closed, so vizConsumersLive() is false: everything measured here is DSP.
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

static const double SR = 48000.0;
static const int    BLK = 512;
static const int    WARM = 48;      // 0.5 s
static const int    MEAS = 281;     // 3.0 s

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
    bool set (const std::string& n, float norm)
    {
        auto it = byName.find (n); if (it == byName.end()) { printf ("  !! no param '%s'\n", n.c_str()); return false; }
        const auto& pi = info.at (it->second);
        return AudioUnitSetParameter (au, it->second, kAudioUnitScope_Global, 0, pi.minValue + norm * (pi.maxValue - pi.minValue), 0) == noErr;
    }
    bool setRaw (const std::string& n, float v)
    {
        auto it = byName.find (n); if (it == byName.end()) { printf ("  !! no param '%s'\n", n.c_str()); return false; }
        return AudioUnitSetParameter (au, it->second, kAudioUnitScope_Global, 0, v, 0) == noErr;
    }
    // returns CPU% (render wall time / audio time) over the measurement window
    double profile (int notes)
    {
        static const int CH[8] = { 48, 55, 60, 64, 67, 72, 76, 79 };
        for (int n = 0; n < notes && n < 8; ++n) MusicDeviceMIDIEvent (au, 0x90, (UInt32) CH[n], 100, 0);
        std::vector<float> bl ((size_t) BLK), br ((size_t) BLK);
        AudioBufferList* abl = (AudioBufferList*) calloc (1, sizeof (AudioBufferList) + sizeof (AudioBuffer));
        abl->mNumberBuffers = 2;
        AudioTimeStamp ts {}; ts.mFlags = kAudioTimeStampSampleTimeValid;
        double secs = 0.0;
        for (int b = 0; b < WARM + MEAS; ++b) {
            abl->mBuffers[0] = { 1, (UInt32) (BLK * 4), bl.data() };
            abl->mBuffers[1] = { 1, (UInt32) (BLK * 4), br.data() };
            AudioUnitRenderActionFlags fl = 0;
            const auto t0 = std::chrono::steady_clock::now();
            AudioUnitRender (au, &fl, &ts, 0, BLK, abl);
            const auto t1 = std::chrono::steady_clock::now();
            if (b >= WARM) secs += std::chrono::duration<double> (t1 - t0).count();
            ts.mSampleTime += BLK;
        }
        free (abl);
        return 100.0 * secs / ((double) MEAS * BLK / SR);
    }
};

struct Set { const char* name; float v; bool raw; };
struct Scn { const char* label; int notes; std::vector<Set> sets; };

//  Type indices (Synth Filter 1/2 Type, 0..117):  0 = Ladder LP24 (OVERSAMPLED), 5 = SVF LP (not),
//  17 = Formant Morph (heavy coefficients), 27 = None.
static const float T_LAD = 0, T_SVF = 5, T_FMT = 17, T_NONE = 27;

// route one oscillator into filter 1 (both the pill and the send amount, as the UI writes them)
static void osc1 (std::vector<Set>& v, const char* en, const char* src, const char* snd)
{ if (en) v.push_back ({ en, 1.0f, false }); v.push_back ({ src, 1.0f, false }); v.push_back ({ snd, 1.0f, false }); }

int main()
{
    printf ("\n══ tp17 — FILTER CPU vs SCALE (installed AU, editor closed, %d s/scenario) ══\n", MEAS * BLK / (int) SR);
    printf ("   every filter row has a type-NONE control at the SAME size, so the filter's own\n"
            "   cost is the gap between them — not a delta against an empty synth.\n\n");

    std::vector<Scn> S;
    auto addOscRow = [&] (const char* label, float type, int nOsc, int notes)
    {
        std::vector<Set> v { { "Synth Filter 1 Type", type, true } };
        osc1 (v, nullptr,        "Synth Filter 1 Source A", "Synth OSC A Filter 1 Send");
        if (nOsc > 1) osc1 (v, "Osc B Enable", "Synth Filter 1 Source B", "Synth OSC B Filter 1 Send");
        if (nOsc > 2) osc1 (v, "Osc C Enable", "Synth Filter 1 Source C", "Synth OSC C Filter 1 Send");
        if (nOsc > 3) osc1 (v, "Osc D Enable", "Synth Filter 1 Source D", "Synth OSC D Filter 1 Send");
        S.push_back ({ label, notes, v });
    };
    // ── A. does OSCILLATOR COUNT multiply the filter? (4-note chord) ──
    addOscRow ("A1  1 osc · filter None      ", T_NONE, 1, 4);
    addOscRow ("A2  2 osc · filter None      ", T_NONE, 2, 4);
    addOscRow ("A3  3 osc · filter None      ", T_NONE, 3, 4);
    addOscRow ("A4  4 osc · filter None      ", T_NONE, 4, 4);
    addOscRow ("A5  1 osc · Ladder LP24      ", T_LAD,  1, 4);
    addOscRow ("A6  2 osc · Ladder LP24      ", T_LAD,  2, 4);
    addOscRow ("A7  3 osc · Ladder LP24      ", T_LAD,  3, 4);
    addOscRow ("A8  4 osc · Ladder LP24      ", T_LAD,  4, 4);
    // ── B. does HELD-NOTE COUNT multiply it? (1 osc) ──
    addOscRow ("B1  1 note · filter None     ", T_NONE, 1, 1);
    addOscRow ("B2  1 note · Ladder LP24     ", T_LAD,  1, 1);
    addOscRow ("B3  4 notes · filter None    ", T_NONE, 1, 4);
    addOscRow ("B4  4 notes · Ladder LP24    ", T_LAD,  1, 4);
    addOscRow ("B5  8 notes · filter None    ", T_NONE, 1, 8);
    addOscRow ("B6  8 notes · Ladder LP24    ", T_LAD,  1, 8);
    // ── C. BOTH SLOTS, and the oversample OR (8 notes, 1 osc) ──
    auto two = [&] (const char* label, float t1, float t2, bool erosion = false)
    {
        std::vector<Set> v { { "Synth Filter 1 Type", t1, true }, { "Synth Filter 2 Type", t2, true } };
        osc1 (v, nullptr, "Synth Filter 1 Source A", "Synth OSC A Filter 1 Send");
        v.push_back ({ "Synth Filter 2 Source A", 1.0f, false });
        v.push_back ({ "Synth OSC A Filter 2 Send", 1.0f, false });
        if (erosion) v.push_back ({ "Synth Erosion", 1.0f, false });
        S.push_back ({ label, 8, v });
    };
    two ("C1  8n · F1 None  + F2 None  ", T_NONE, T_NONE);
    two ("C2  8n · F1 SVF   + F2 None  ", T_SVF,  T_NONE);
    two ("C3  8n · F1 SVF   + F2 SVF   ", T_SVF,  T_SVF);
    two ("C4  8n · F1 Ladder+ F2 None  ", T_LAD,  T_NONE);
    two ("C5  8n · F1 Ladder+ F2 Ladder", T_LAD,  T_LAD);
    two ("C6  8n · F1 SVF   + F2 Ladder", T_SVF,  T_LAD);    // the OR: one oversampled type taxes BOTH
    two ("C7  8n · F1 Ladder+ F2 SVF   ", T_LAD,  T_SVF);
    // ── D. modulated cutoff at size, and a heavy coefficient engine ──
    two ("D1  8n · 2x Ladder + Erosion ", T_LAD,  T_LAD,  true);
    two ("D2  8n · 2x SVF    + Erosion ", T_SVF,  T_SVF,  true);
    two ("D3  8n · 2x Formant+ Erosion ", T_FMT,  T_FMT,  true);

    std::vector<double> best (S.size(), 1e9);
    for (int pass = 0; pass < 2; ++pass)
        for (size_t k = 0; k < S.size(); ++k)
        {
            AU a; if (! a.open()) return 2;
            for (const auto& st : S[k].sets) { if (st.raw) a.setRaw (st.name, st.v); else a.set (st.name, st.v); }
            const double pct = a.profile (S[k].notes);
            a.close();
            best[k] = std::min (best[k], pct);
        }
    for (size_t k = 0; k < S.size(); ++k) printf ("  %6.2f%%   %s\n", best[k], S[k].label);
    printf ("\n  filter's own cost = each row minus its None control at the same size\n\n");
    return 0;
}
