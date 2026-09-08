// ══════════════════════════════════════════════════════════════════════════════════════════════
//  macro_wheel_cert.cpp — fb575: A MACRO UNDER THE MOD WHEEL IS AS SMOOTH AS A MACRO UNDER THE MOUSE, on the real AU.
//
//    clang++ -std=c++17 -O2 Tests/macro_wheel_cert.cpp -o /tmp/macro_wheel_cert \
//        -framework AudioToolbox -framework CoreFoundation -framework CoreAudio && /tmp/macro_wheel_cert
//
//  Max: "whenever I press MIDI Learn and I move my mod wheel on the Macros, it's still choppy... It needs to be
//  smooth, just like how we're clicking and dragging it."
//
//  WHY (read): a learned CC is parked on the audio thread (midiCcSeen) and lands on its PARAMETER from the
//  processor's message-thread timer — 60 Hz with an editor open, 15 Hz with it closed — and the macro base is
//  read raw once per block (no smoother; the wheel SOURCE has a 10 ms one-pole, the macro knob had none), so a
//  sweep of the wheel reached the sound as a staircase of timer-sized steps with only the level's 2.5 ms glide
//  between them. A drag writes the parameter on every pointer move and never waits for the timer.
//
//  THE RIG: midiCcMap {"1":"SYN_MACRO_1"} and one route Macro 1 → Level A (depth 1) through the state door;
//  Level A's knob at 0, so the level IS the macro. A note is held while the wheel sweeps 0 → 127, one CC per
//  rendered block with the run loop pumped ~10 ms between them (wall time ≈ audio time, so the timer fires as
//  it does in a host). The output's amplitude envelope (64-sample windows) over the sweep is normalised to the
//  sweep's range; its biggest single-window jump and its longest plateau are the two numbers of "choppy".
//
//  THE BARS
//   0  the AU exposes OSC A Level and Macro 1; the map and the route install
//   1  THE WHEEL IS AS SMOOTH AS THE MOUSE — the wheel sweep's biggest jump and longest plateau are no worse
//      than the mouse sweep's (same sweep through AudioUnitSetParameter), within 25 % + a window
//   2  NEITHER PATH STEPS — both sweeps: biggest jump <= 1.5 % of the range per 1.3 ms window, longest plateau
//      <= 24 ms (no timer-sized stair)
//   3  THE PARAMETER FOLLOWS THE WHEEL — after the sweep the host reads Macro 1 at 1.0
//   4  A MOUSE MOVE AFTER THE WHEEL WINS — Macro 1 set to 0.25 after a wheel at 127 sounds like 0.25 set alone
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
#include <algorithm>

static const double SR = 48000.0;
static const int    BLK = 512;
static int pass = 0, fail = 0;
static void chk (bool ok, const char* label, const std::string& detail = "")
{ if (ok) ++pass; else ++fail; std::printf ("  %s  %s%s%s\n", ok ? "ok  " : "FAIL", label, detail.empty() ? "" : "   ", detail.c_str()); }

struct AU
{
    AudioUnit au = nullptr;
    std::map<std::string, AudioUnitParameterID> byName;
    std::map<AudioUnitParameterID, AudioUnitParameterInfo> info;
    double clock_ = 0;
    bool open()
    {
        AudioComponentDescription d {};
        d.componentType = kAudioUnitType_MusicDevice; d.componentSubType = 'Tern'; d.componentManufacturer = 'Wvcr';
        AudioComponent c = AudioComponentFindNext (nullptr, &d);
        if (! c) { std::printf ("  !! AU not found\n"); return false; }
        if (AudioComponentInstanceNew (c, &au) != noErr) { std::printf ("  !! instantiate failed\n"); return false; }
        AudioStreamBasicDescription f {};
        f.mSampleRate = SR; f.mFormatID = kAudioFormatLinearPCM;
        f.mFormatFlags = kAudioFormatFlagsNativeFloatPacked | kAudioFormatFlagIsNonInterleaved;
        f.mBytesPerPacket = 4; f.mFramesPerPacket = 1; f.mBytesPerFrame = 4; f.mChannelsPerFrame = 2; f.mBitsPerChannel = 32;
        AudioUnitSetProperty (au, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Output, 0, &f, sizeof f);
        UInt32 mx = BLK;
        AudioUnitSetProperty (au, kAudioUnitProperty_MaximumFramesPerSlice, kAudioUnitScope_Global, 0, &mx, sizeof mx);
        if (AudioUnitInitialize (au) != noErr) { std::printf ("  !! init failed\n"); return false; }
        UInt32 sz = 0; Boolean w = false;
        AudioUnitGetPropertyInfo (au, kAudioUnitProperty_ParameterList, kAudioUnitScope_Global, 0, &sz, &w);
        std::vector<AudioUnitParameterID> ids (sz / sizeof (AudioUnitParameterID));
        AudioUnitGetProperty (au, kAudioUnitProperty_ParameterList, kAudioUnitScope_Global, 0, ids.data(), &sz);
        for (auto id : ids)
        {
            AudioUnitParameterInfo pi {}; UInt32 s = sizeof pi;
            if (AudioUnitGetProperty (au, kAudioUnitProperty_ParameterInfo, kAudioUnitScope_Global, id, &pi, &s) != noErr) continue;
            std::string nm;
            if ((pi.flags & kAudioUnitParameterFlag_HasCFNameString) && pi.cfNameString)
            { char buf[256] = {0}; CFStringGetCString (pi.cfNameString, buf, sizeof buf, kCFStringEncodingUTF8); nm = buf; }
            else nm = pi.name;
            byName[nm] = id; info[id] = pi;
        }
        return true;
    }
    void close() { if (au) { AudioUnitUninitialize (au); AudioComponentInstanceDispose (au); au = nullptr; } }
    std::string find (const std::string& needle) const
    { for (auto& kv : byName) if (kv.first.find (needle) != std::string::npos) return kv.first; return ""; }
    float norm (const std::string& n)   // the parameter's value as 0..1 of its own range
    {
        auto it = byName.find (n); if (it == byName.end()) return NAN;
        AudioUnitParameterValue v = 0; AudioUnitGetParameter (au, it->second, kAudioUnitScope_Global, 0, &v);
        const auto& pi = info.at (it->second); return (v - pi.minValue) / std::max (1e-9f, pi.maxValue - pi.minValue);
    }
    bool set (const std::string& n, float nv)
    {
        auto it = byName.find (n); if (it == byName.end()) return false;
        const auto& pi = info.at (it->second);
        return AudioUnitSetParameter (au, it->second, kAudioUnitScope_Global, 0, pi.minValue + nv * (pi.maxValue - pi.minValue), 0) == noErr;
    }
    // read the state XML out of ClassInfo
    std::string stateXml()
    {
        CFPropertyListRef pl = nullptr; UInt32 psz = sizeof (pl);
        if (AudioUnitGetProperty (au, kAudioUnitProperty_ClassInfo, kAudioUnitScope_Global, 0, &pl, &psz) != noErr || pl == nullptr) return "";
        CFDataRef dd = (CFDataRef) CFDictionaryGetValue ((CFDictionaryRef) pl, CFSTR ("jucePluginState"));
        std::string xml;
        if (dd) { const UInt8* p = CFDataGetBytePtr (dd); uint32_t len = 0; std::memcpy (&len, p + 4, 4); xml.assign ((const char*) p + 8, len); }
        CFRelease (pl); return xml;
    }
    // rewrite one attribute of the state XML and hand it back (au_fx_path.cpp's idiom, generalised)
    bool setStateAttr (const std::string& name, const std::string& value)
    {
        CFPropertyListRef pl = nullptr; UInt32 psz = sizeof (pl);
        if (AudioUnitGetProperty (au, kAudioUnitProperty_ClassInfo, kAudioUnitScope_Global, 0, &pl, &psz) != noErr || pl == nullptr) return false;
        CFDictionaryRef dict = (CFDictionaryRef) pl;
        CFStringRef key = CFSTR ("jucePluginState");
        CFDataRef dd = (CFDataRef) CFDictionaryGetValue (dict, key);
        if (dd == nullptr) { CFRelease (pl); return false; }
        const UInt8* p = CFDataGetBytePtr (dd);
        uint32_t magic = 0, len = 0; std::memcpy (&magic, p, 4); std::memcpy (&len, p + 4, 4);
        if (magic != 0x21324356u) { CFRelease (pl); return false; }
        std::string xml ((const char*) p + 8, len);
        std::string esc; for (char ch : value) esc += (ch == '"' ? "&quot;" : ch == '&' ? "&amp;" : ch == '<' ? "&lt;" : ch == '>' ? "&gt;" : std::string (1, ch));
        const std::string attr = " " + name + "=\"" + esc + "\"";
        const size_t at = xml.find (" " + name + "=\"");
        if (at != std::string::npos) { const size_t e = xml.find ('"', at + name.size() + 3); xml = xml.substr (0, at) + attr + xml.substr (e + 1); }
        else { const size_t r = xml.find ("<Parameters"); if (r == std::string::npos) { CFRelease (pl); return false; }
               xml = xml.substr (0, r + 11) + attr + xml.substr (r + 11); }
        std::vector<UInt8> blob (8 + xml.size() + 1, 0);
        const uint32_t m = 0x21324356u, l = (uint32_t) xml.size() + 1;
        std::memcpy (blob.data(), &m, 4); std::memcpy (blob.data() + 4, &l, 4); std::memcpy (blob.data() + 8, xml.data(), xml.size());
        CFMutableDictionaryRef nd = CFDictionaryCreateMutableCopy (nullptr, 0, dict);
        CFDataRef ndata = CFDataCreate (kCFAllocatorDefault, blob.data(), (CFIndex) blob.size());
        CFDictionarySetValue (nd, key, ndata);
        CFPropertyListRef npl = (CFPropertyListRef) nd;
        const OSStatus st = AudioUnitSetProperty (au, kAudioUnitProperty_ClassInfo, kAudioUnitScope_Global, 0, &npl, sizeof (npl));
        CFRelease (ndata); CFRelease (nd); CFRelease (pl);
        pump (0.3);
        return st == noErr;
    }
    void pump (double seconds) { double t = 0; while (t < seconds) { CFRunLoopRunInMode (kCFRunLoopDefaultMode, 0.02, false); t += 0.02; } }
    void midi (UInt32 status, UInt32 d1, UInt32 d2) { MusicDeviceMIDIEvent (au, status, d1, d2, 0); }
    std::vector<float> render (int nblk)
    {
        std::vector<float> out; std::vector<float> bl ((size_t) BLK), br ((size_t) BLK);
        AudioBufferList* abl = (AudioBufferList*) calloc (1, sizeof (AudioBufferList) + sizeof (AudioBuffer)); abl->mNumberBuffers = 2;
        AudioTimeStamp ts {}; ts.mFlags = kAudioTimeStampSampleTimeValid; ts.mSampleTime = clock_;
        for (int b = 0; b < nblk; ++b)
        {
            abl->mBuffers[0] = { 1, (UInt32) (BLK * 4), bl.data() }; abl->mBuffers[1] = { 1, (UInt32) (BLK * 4), br.data() };
            AudioUnitRenderActionFlags fl = 0; if (AudioUnitRender (au, &fl, &ts, 0, BLK, abl) != noErr) break;
            ts.mSampleTime += BLK; clock_ += BLK;
            for (int i = 0; i < BLK; ++i) out.push_back (0.5f * (bl[(size_t) i] + br[(size_t) i]));
        }
        free (abl); return out;
    }
    // a CC event has to pass through processBlock (audio) and then the timer (message thread)
    void cc (int num, int val) { midi (0xB0, (UInt32) num, (UInt32) val); render (4); pump (0.35); }
    std::vector<float> note (int nn) { midi (0x90, (UInt32) nn, 100); render (12); auto body = render (24); midi (0x80, (UInt32) nn, 0); render (40); return body; }
};

static double rmsDb (const std::vector<float>& v)
{ double s = 0; for (float x : v) s += (double) x * x; return 10.0 * std::log10 (std::max (1e-20, s / std::max<size_t> (1, v.size()))); }

struct Sweep { double maxJump = 0, plateauMs = 0; int windows = 0; double e0 = 0, e1 = 0; };
static Sweep analyse (const std::vector<float>& v)
{
    const int W = 64; std::vector<double> env;
    for (size_t i = 0; i + W <= v.size(); i += W) { double s = 0; for (int k = 0; k < W; ++k) s += (double) v[i + k] * v[i + k]; env.push_back (std::sqrt (s / W)); }
    Sweep r; r.windows = (int) env.size(); if (env.size() < 4) return r;
    // the range: the quietest and loudest windows (the sweep runs 0 → 1, the tone is steady)
    double lo = 1e9, hi = -1e9; for (double e : env) { lo = std::min (lo, e); hi = std::max (hi, e); }
    r.e0 = lo; r.e1 = hi; const double range = std::max (1e-9, hi - lo);
    int run = 0, maxRun = 0;
    for (size_t i = 1; i < env.size(); ++i)
    {
        const double d = (env[i] - env[i - 1]) / range;
        r.maxJump = std::max (r.maxJump, std::abs (d));
        if (std::abs (d) < 0.0005) { ++run; maxRun = std::max (maxRun, run); } else run = 0;
    }
    r.plateauMs = maxRun * W * 1000.0 / SR;
    return r;
}
static std::string fmt (const Sweep& s) { char b[160]; std::snprintf (b, sizeof b, "jump %.2f%%/window · plateau %.1f ms · %d windows · range %.4f..%.4f", s.maxJump * 100, s.plateauMs, s.windows, s.e0, s.e1); return b; }

int main()
{
    AU au; if (! au.open()) return 1;
    std::printf ("\n══ fb575 — A MACRO UNDER THE WHEEL IS AS SMOOTH AS UNDER THE MOUSE (installed AU) ══\n\n");
    const std::string LVL = au.find ("OSC A Level"), MAC1 = au.find ("Macro 1"), LB = au.find ("OSC B Level"), LC = au.find ("OSC C Level"), LD = au.find ("OSC D Level"), WT = au.find ("OSC A WT Preset");
    chk (! LVL.empty() && ! MAC1.empty(), "0  the AU exposes OSC A Level and Macro 1", "level='" + LVL + "' macro='" + MAC1 + "'");
    if (LVL.empty() || MAC1.empty()) { au.close(); return 1; }
    const bool mapOk = au.setStateAttr ("midiCcMap", "{\"1\":\"SYN_MACRO_1\"}");
    const bool rtOk  = au.setStateAttr ("synModJson", "[{\"s\":220,\"d\":64,\"v\":1.0}]");   // Macro 1 (220) → Level A (64), depth 1
    chk (mapOk && rtOk, "0b the CC map (CC 1 → Macro 1) and the route (Macro 1 → Level A, depth 1) install through the state door");
    if (! WT.empty()) au.set (WT, 4.0f / 100.0f);   // a real table
    au.set (LVL, 0.0f); if (! LB.empty()) au.set (LB, 0.0f); if (! LC.empty()) au.set (LC, 0.0f); if (! LD.empty()) au.set (LD, 0.0f);
    au.set (MAC1, 0.0f); au.midi (0xB0, 1, 0); au.render (4); au.pump (0.4);

    auto sweep = [&] (bool viaWheel) -> std::vector<float>
    {
        au.set (MAC1, 0.0f); au.midi (0xB0, 1, 0); au.render (4); au.pump (0.4);
        au.midi (0x90, 60, 100); au.render (30);            // the note settles (attack, then the macro at 0 = silence)
        std::vector<float> out;
        for (int i = 0; i < 128; ++i)
        {
            if (viaWheel) au.midi (0xB0, 1, (UInt32) i); else au.set (MAC1, (float) i / 127.0f);
            auto blk = au.render (1); out.insert (out.end(), blk.begin(), blk.end());
            CFRunLoopRunInMode (kCFRunLoopDefaultMode, 0.010, false);   // wall time ≈ audio time: the timer fires as in a host
        }
        auto tail = au.render (6); out.insert (out.end(), tail.begin(), tail.end());
        au.midi (0x80, 60, 0); au.render (60); au.pump (0.2);
        return out;
    };
    const Sweep w = analyse (sweep (true));
    au.pump (0.3); const float mAfter = au.norm (MAC1);
    const Sweep m = analyse (sweep (false));
    std::printf ("   wheel : %s\n   mouse : %s\n", fmt (w).c_str(), fmt (m).c_str());
    chk (w.maxJump <= m.maxJump * 1.25 + 0.002 && w.plateauMs <= m.plateauMs + 1.4,
         "1  THE WHEEL IS AS SMOOTH AS THE MOUSE: its biggest jump and longest plateau are no worse than the mouse sweep's (+25 %, +1 window)", fmt (w));
    chk (w.maxJump <= 0.015 && m.maxJump <= 0.015 && w.plateauMs <= 24.0 && m.plateauMs <= 24.0,
         "2  NEITHER PATH STEPS: biggest jump <= 1.5 % per 1.3 ms window, longest plateau <= 24 ms, on both", "wheel " + fmt (w) + " | mouse " + fmt (m));
    chk (std::abs (mAfter - 1.0f) < 0.02f, "3  THE PARAMETER FOLLOWS THE WHEEL: after the sweep the host reads Macro 1 at 1.0", "read " + std::to_string (mAfter));
    // 4 — a mouse move after the wheel wins
    au.set (MAC1, 0.25f); au.pump (0.3); const double L25 = rmsDb (au.note (60));
    au.midi (0xB0, 1, 127); au.render (4); au.pump (0.4); au.set (MAC1, 0.25f); au.pump (0.3); const double L25b = rmsDb (au.note (60));
    chk (std::abs (L25 - L25b) < 1.0, "4  A MOUSE MOVE AFTER THE WHEEL WINS: Macro 1 at 0.25 after a wheel at 127 sounds like 0.25 alone (within 1 dB)",
         "alone " + std::to_string (L25) + " dB · after the wheel " + std::to_string (L25b) + " dB");
    std::printf ("\n  %d pass · %d fail\n", pass, fail);
    au.close(); return fail ? 1 : 0;
}
