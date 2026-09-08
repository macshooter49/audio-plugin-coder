// ══════════════════════════════════════════════════════════════════════════════════════════════
//  midi_learn_cert.cpp — fb563 (4): a LEARNED CC MOVES ITS PARAMETER, on the real AU.
//
//    clang++ -std=c++17 -O2 Tests/midi_learn_cert.cpp -o /tmp/midi_learn_cert \
//        -framework AudioToolbox -framework CoreFoundation -framework CoreAudio && /tmp/midi_learn_cert
//
//  The CC map is installed the way a saved project installs it — the `midiCcMap` attribute of the
//  state XML (the same door setStateInformation uses), because a host cannot call a WebView native.
//  The audio thread stores the CC, the processor's message-thread timer applies it, so the harness
//  PUMPS THE RUN LOOP between the event and the read (the fb453 host law).
//
//  THE BARS
//   1  a fresh instance has no bindings (midiCcMap absent from its state)
//   2  {"74":"SYN_OSC_A_WARP_AMOUNT"} installed → CC 74 = 127 puts Warp A at its maximum, CC 74 = 0 at its minimum
//   3  an UNMAPPED CC (75) leaves the parameter alone
//   4  the map ROUND-TRIPS through the state (read back from ClassInfo after install)
//   5  CC 1 mapped to a parameter STILL feeds the mod wheel (both are honest): a Wheel → Level A route
//      still sounds with CC 1 mapped elsewhere
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
static int PASS = 0, FAIL = 0;
static void chk (bool ok, const char* label, const std::string& detail = "")
{
    if (ok) { ++PASS; std::printf ("  ok    %s%s%s\n", label, detail.empty() ? "" : "   ", detail.c_str()); }
    else    { ++FAIL; std::printf ("  FAIL  %s%s%s\n", label, detail.empty() ? "" : "   ", detail.c_str()); }
}
static std::string fmt (const char* f, double a, double b = 0, double c = 0)
{ char buf[256]; std::snprintf (buf, sizeof buf, f, a, b, c); return buf; }

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
{ double s = 0; for (float x : v) s += (double) x * x; return 10.0 * std::log10 (s / std::max<size_t> (1, v.size()) + 1e-30); }

int main()
{
    setvbuf (stdout, nullptr, _IOLBF, 0);
    std::printf ("\n══ fb563 (4) — MIDI LEARN: A LEARNED CC MOVES ITS PARAMETER (installed AU) ══\n");
    AU au; if (! au.open()) return 2;
    const std::string WARP = au.find ("OSC A Warp Amount"), LVL = au.find ("OSC A Level");
    std::printf ("   params: warp='%s' level='%s'\n", WARP.c_str(), LVL.c_str());
    chk (! WARP.empty() && ! LVL.empty(), "0  the AU exposes OSC A Warp Amount and OSC A Level");
    if (WARP.empty() || LVL.empty()) { au.close(); return 1; }

    chk (au.stateXml().find ("midiCcMap=") == std::string::npos, "1  a fresh instance saves no midiCcMap");

    au.set (WARP, 0.25f); au.pump (0.2);
    chk (au.setStateAttr ("midiCcMap", "{\"74\":\"SYN_OSC_A_WARP_AMOUNT\"}"), "2  the map {74 → Warp A} installs through the state");
    au.cc (74, 127); const float w1 = au.norm (WARP);
    au.cc (74, 0);   const float w0 = au.norm (WARP);
    au.cc (74, 64);  const float wm = au.norm (WARP);
    chk (std::abs (w1 - 1.0f) < 0.01f && std::abs (w0) < 0.01f && std::abs (wm - 64.0f / 127.0f) < 0.02f,
         "2  CC 74 = 127 / 0 / 64 → Warp A at 1.00 / 0.00 / 0.50", fmt ("%.3f / %.3f / %.3f", w1, w0, wm));

    au.set (WARP, 0.25f); au.pump (0.2);
    au.cc (75, 127); const float wu = au.norm (WARP);
    chk (std::abs (wu - 0.25f) < 0.01f, "3  an unmapped CC (75) leaves Warp A where it was", fmt ("%.3f", wu));

    const std::string xml = au.stateXml();
    chk (xml.find ("midiCcMap=\"{&quot;74&quot;:&quot;SYN_OSC_A_WARP_AMOUNT&quot;}\"") != std::string::npos, "4  the map round-trips through the saved state",
         xml.find ("midiCcMap=") != std::string::npos ? xml.substr (xml.find ("midiCcMap="), 70) : "(absent)");

    // 5 — CC 1 mapped to Warp A must still drive the mod wheel source
    au.setStateAttr ("midiCcMap", "{\"1\":\"SYN_OSC_A_WARP_AMOUNT\"}");
    au.setStateAttr ("synModJson", "[{\"s\":230,\"d\":64,\"v\":1.0}]");
    const std::string LB = au.find ("OSC B Level"), LC = au.find ("OSC C Level"), LD = au.find ("OSC D Level");
    au.set (LVL, 0.0f); if (! LB.empty()) au.set (LB, 0.0f); if (! LC.empty()) au.set (LC, 0.0f); if (! LD.empty()) au.set (LD, 0.0f); au.pump (0.2);
    au.cc (1, 0);   au.render (30); const double q0 = rmsDb (au.note (60));
    au.cc (1, 127); au.render (30); const double q1 = rmsDb (au.note (60)); const float wcc = au.norm (WARP);
    chk (q0 < -70 && q1 > -30 && std::abs (wcc - 1.0f) < 0.01f, "5  CC 1 bound to Warp A: the wheel source still works AND Warp A follows the CC", fmt ("%.1f dB → %.1f dB · warp %.2f", q0, q1, wcc));

    au.close();
    std::printf ("\n  %d pass · %d fail\n\n", PASS, FAIL);
    return FAIL ? 1 : 0;
}
