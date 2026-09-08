// ══════════════════════════════════════════════════════════════════════════════════════════════
//  au_state_blob.h — fb601: THE SHARED ClassInfo / STATE-BLOB PLUMBING FOR THE MIGRATION CERTS.
//
//  Not a new abstraction — this is dly_spread_state_au.cpp's plumbing, lifted VERBATIM, because
//  Tests/harm_tbl_state_au.cpp needs the identical 200 lines and RECYCLE says reuse the existing
//  code rather than write a second version of it. Both certs #include this and then assert their
//  own migration; nothing here knows about Spread or about HARM_TABLE.
//
//  WHAT IT GIVES YOU
//    chk()                     the pass/FAIL bar printer (and the pass/fail counters)
//    struct AU                 open the installed 'Tern'/'Wvcr' AU, read/set parameters by name,
//                              and — the point of the file — read and WRITE the state blob:
//                                kAudioUnitProperty_ClassInfo -> the dict's "jucePluginState"
//                                CFData IS the getStateInformation blob (JUCE copyXmlToBinary:
//                                4-byte magic 'VC2!' + uint32 length + the XML + a NUL; NOT
//                                compressed in JUCE 7/8, verified on this build). Edit that XML,
//                                push it back, read the parameter out with AudioUnitGetParameter.
//                                That is a real project reload, through the real
//                                setStateInformation — not a transcription of its predicate.
//    midi/render/note          fb602: DRIVE it. Lifted VERBATIM from Tests/harm_table_au.cpp
//                              (:114-131) because the fb602 restore certs have to prove a slot
//                              RENDERS, and a second copy of the AudioUnitRender loop is exactly
//                              what RECYCLE forbids. rmsDb() is that file's metric, unchanged.
//    setParam/getParam         edit and read a <PARAM id=".." value=".."/> child
//    dropParam/hasParam        delete / test for a PARAM child outright (a pre-migration blob may
//                              not have the child at all)
//    hasProperty/dropProperty  the root's migration MARKERS, which is what gates every migration
//    setRootAttr/getRootAttr   fb602: ADD/EDIT/READ an attribute on the <Parameters …> root tag.
//                              hasProperty/dropProperty could only test and delete; a blob saved
//                              by a session that HAD a sample carries oscSamplePath0="…"
//                              (PluginProcessor.cpp:14687) and a blob from this idle AU does not,
//                              so the restore certs must be able to PUT one there.
//    sliceMarked               print the shipped migration's own lines, between its own markers
//
//  WHY THE INSTALLED AU AND NOT AN OFFLINE HARNESS: a migration lives inside a 600-line
//  setStateInformation. Transcribing its predicate into a cert proves the ARITHMETIC and nothing
//  about whether the branch is reached, whether the marker gates it, or whether the ids are in the
//  list. That is the fb373/fb469 failure class — green measurements on a path the plugin never
//  takes.
// ══════════════════════════════════════════════════════════════════════════════════════════════
#pragma once

#include <AudioToolbox/AudioToolbox.h>
#include <CoreFoundation/CoreFoundation.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <string>
#include <vector>
#include <map>
#include <fstream>
#include <sstream>

static const double SR = 48000.0;
static const int    BLK = 512;
static int pass = 0, fail = 0;
// fb602 — WHICH bar went red, in one line at the end. A cert whose tail is captured (a sweep, a
// CI log, a scrollback) reports "13 passed, 1 FAILED" and nothing about WHICH, and a flake you
// cannot name is a flake you cannot fix: one appeared here at roughly 1 run in 25 and could not be
// identified afterwards because only the summary line had been kept.
static std::vector<std::string> failedBars;
static void chk (bool ok, const char* label, const std::string& detail)
{ if (ok) ++pass; else { ++fail; failedBars.push_back (label); }
  std::printf ("  %s  %s\n        %s\n", ok ? "PASS" : "FAIL", label, detail.c_str()); }
[[maybe_unused]] static int summary()
{
    std::printf ("\n  %d passed, %d FAILED\n", pass, fail);
    if (! failedBars.empty())
    {
        std::printf ("  FAILED BARS:\n");
        for (const auto& b : failedBars) std::printf ("    - %s\n", b.c_str());
    }
    std::printf ("\n");
    return fail ? 1 : 0;
}

[[maybe_unused]] static std::string cf2s (CFStringRef s)
{ if (! s) return ""; char b[1024] = {0}; CFStringGetCString (s, b, sizeof b, kCFStringEncodingUTF8); return b; }

// ── the AU, opened the way every other au_* harness in this directory opens it ─────────────────
struct AU
{
    AudioUnit au = nullptr;
    std::map<std::string, AudioUnitParameterID> byName;
    bool open()
    {
        AudioComponentDescription d {};
        d.componentType = kAudioUnitType_MusicDevice; d.componentSubType = 'Tern'; d.componentManufacturer = 'Wvcr';
        AudioComponent c = AudioComponentFindNext (nullptr, &d);
        if (! c) { std::printf ("  !! AU not found (is Terrain installed?)\n"); return false; }
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
            byName[(pi.flags & kAudioUnitParameterFlag_HasCFNameString) && pi.cfNameString ? cf2s (pi.cfNameString)
                                                                                           : std::string (pi.name)] = id;
        }
        return true;
    }
    void close() { if (au) { AudioUnitUninitialize (au); AudioComponentInstanceDispose (au); au = nullptr; } }
    void pump (double seconds) { double t = 0; while (t < seconds) { CFRunLoopRunInMode (kCFRunLoopDefaultMode, 0.02, false); t += 0.02; } }

    // ── fb602: DRIVING it. harm_table_au.cpp:114-131, VERBATIM. ───────────────────────────────
    //  A state cert that only reads parameters back proves the blob was parsed. It cannot tell a
    //  restored SAMPLE SLOT from an empty one, because the slot is a buffer and not a parameter —
    //  which is precisely how bug (c) stayed invisible for the whole life of the plugin.
    double clock_ = 0.0;
    void midi (UInt32 s, UInt32 a, UInt32 b) { MusicDeviceMIDIEvent (au, s, a, b, 0); }
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
    // the bake runs on the MESSAGE thread at 60 Hz, so a change needs a real run-loop pump
    // before the note — not just more audio blocks.
    std::vector<float> note (int nn) { pump (0.30); midi (0x90, (UInt32) nn, 100); render (10); auto b = render (26); midi (0x80, (UInt32) nn, 0); render (30); return b; }
    bool has (const std::string& n) const { return byName.count (n) != 0; }
    float get (const std::string& n)
    { auto it = byName.find (n); if (it == byName.end()) return -999.f;
      AudioUnitParameterValue v = -999.f;
      AudioUnitGetParameter (au, it->second, kAudioUnitScope_Global, 0, &v); return (float) v; }
    bool set (const std::string& n, float v)
    { auto it = byName.find (n); if (it == byName.end()) return false;
      return AudioUnitSetParameter (au, it->second, kAudioUnitScope_Global, 0, v, 0) == noErr; }

    // ── the state blob, in and out ────────────────────────────────────────────────────────────
    CFDictionaryRef classInfo()
    { CFPropertyListRef pl = nullptr; UInt32 s = sizeof pl;
      if (AudioUnitGetProperty (au, kAudioUnitProperty_ClassInfo, kAudioUnitScope_Global, 0, &pl, &s) != noErr) return nullptr;
      if (! pl || CFGetTypeID (pl) != CFDictionaryGetTypeID()) { if (pl) CFRelease (pl); return nullptr; }
      return (CFDictionaryRef) pl; }

    std::string readXml (std::string& why)
    {
        CFDictionaryRef d = classInfo();
        if (! d) { why = "no ClassInfo"; return ""; }
        CFDataRef dat = (CFDataRef) CFDictionaryGetValue (d, CFSTR ("jucePluginState"));
        if (! dat || CFGetTypeID (dat) != CFDataGetTypeID()) { CFRelease (d); why = "no jucePluginState CFData"; return ""; }
        const UInt8* p = CFDataGetBytePtr (dat); const CFIndex L = CFDataGetLength (dat);
        if (L < 12 || std::memcmp (p, "VC2!", 4) != 0)                    // 0x21324356 little-endian
        { CFRelease (d); why = "blob is not JUCE copyXmlToBinary (magic mismatch) — it may be compressed on this JUCE"; return ""; }
        std::string xml ((const char*) p + 8, (size_t) (L - 8));
        while (! xml.empty() && xml.back() == '\0') xml.pop_back();
        CFRelease (d);
        return xml;
    }

    bool writeXml (const std::string& xml)
    {
        CFDictionaryRef cur = classInfo();
        if (! cur) return false;
        CFMutableDictionaryRef m = CFDictionaryCreateMutableCopy (nullptr, 0, cur);
        CFRelease (cur);
        std::vector<UInt8> blob;
        const UInt32 magic = 0x21324356u, len = (UInt32) (xml.size() + 1);
        blob.insert (blob.end(), (const UInt8*) &magic, (const UInt8*) &magic + 4);
        blob.insert (blob.end(), (const UInt8*) &len,   (const UInt8*) &len   + 4);
        blob.insert (blob.end(), xml.begin(), xml.end());
        blob.push_back (0);
        CFDataRef dat = CFDataCreate (nullptr, blob.data(), (CFIndex) blob.size());
        CFDictionarySetValue (m, CFSTR ("jucePluginState"), dat);
        CFPropertyListRef pl = m;
        const OSStatus st = AudioUnitSetProperty (au, kAudioUnitProperty_ClassInfo, kAudioUnitScope_Global, 0, &pl, sizeof pl);
        CFRelease (dat); CFRelease (m);
        pump (0.35);                                   // the restore finishes on the message thread
        return st == noErr;
    }
};

// ── XML surgery on the blob: exactly what a PRE-fb600 project looks like ───────────────────────
[[maybe_unused]] static bool setParam (std::string& xml, const std::string& id, double v)
{
    const std::string key = "<PARAM id=\"" + id + "\" value=\"";
    const size_t at = xml.find (key);
    if (at == std::string::npos) return false;
    const size_t vs = at + key.size();
    const size_t ve = xml.find ('"', vs);
    if (ve == std::string::npos) return false;
    char num[64]; std::snprintf (num, sizeof num, "%.17g", v);
    xml.replace (vs, ve - vs, num);
    return true;
}
[[maybe_unused]] static double getParam (const std::string& xml, const std::string& id)
{
    const std::string key = "<PARAM id=\"" + id + "\" value=\"";
    const size_t at = xml.find (key); if (at == std::string::npos) return -999.0;
    return std::atof (xml.c_str() + at + key.size());
}
[[maybe_unused]] static bool hasProperty (const std::string& xml, const std::string& name)
{ return xml.find (" " + name + "=\"") != std::string::npos; }
[[maybe_unused]] static bool dropProperty (std::string& xml, const std::string& name)
{
    const std::string key = " " + name + "=\"";
    const size_t at = xml.find (key); if (at == std::string::npos) return false;
    const size_t q = xml.find ('"', at + key.size());
    if (q == std::string::npos) return false;
    xml.erase (at, q + 1 - at);
    return true;
}

// ── fb602: PUT an attribute on the root that this blob does not have ───────────────────────────
//    getStateInformation writes oscSamplePath0.. / cardStates / convIRRaw1.. as ROOT attributes
//    (PluginProcessor.cpp:14544, :14561, :14687). An AU that never had a sample or a card chain
//    emits none of them, so a restore cert has to synthesise the blob a real session would have
//    saved. Insert just before the '>' of the opening <Parameters …> tag — the same place JUCE's
//    XmlElement writer would have put it.
[[maybe_unused]] static std::string xmlEsc (const std::string& v)
{
    std::string o;
    for (char c : v) { if (c=='&') o+="&amp;"; else if (c=='<') o+="&lt;"; else if (c=='>') o+="&gt;";
                       else if (c=='"') o+="&quot;"; else o+=c; }
    return o;
}
[[maybe_unused]] static std::string xmlUnesc (const std::string& v)
{
    std::string o; size_t i = 0;
    while (i < v.size())
    {
        if (v[i]=='&') { if (!v.compare(i,5,"&amp;")) { o+='&'; i+=5; continue; }
                         if (!v.compare(i,4,"&lt;"))  { o+='<'; i+=4; continue; }
                         if (!v.compare(i,4,"&gt;"))  { o+='>'; i+=4; continue; }
                         if (!v.compare(i,6,"&quot;")){ o+='"'; i+=6; continue; } }
        o += v[i++];
    }
    return o;
}
[[maybe_unused]] static std::string getRootAttr (const std::string& xml, const std::string& name)
{
    const std::string key = " " + name + "=\"";
    const size_t at = xml.find (key); if (at == std::string::npos) return "";
    const size_t s = at + key.size(); const size_t q = xml.find ('"', s);
    if (q == std::string::npos) return "";
    return xmlUnesc (xml.substr (s, q - s));
}
[[maybe_unused]] static bool setRootAttr (std::string& xml, const std::string& name, const std::string& value)
{
    dropProperty (xml, name);                                  // idempotent: replace, never duplicate
    const size_t tag = xml.find ("<Parameters");
    if (tag == std::string::npos) return false;
    const size_t gt = xml.find ('>', tag);
    if (gt == std::string::npos) return false;
    size_t ins = gt;
    while (ins > tag && (xml[ins-1] == '/' || xml[ins-1] == ' ' || xml[ins-1] == '\n')) --ins;
    xml.insert (ins, " " + name + "=\"" + xmlEsc (value) + "\"");
    return true;
}

// ── two more of the same, for a migration that ADDS a PARAM child rather than editing one ──────
//    (a pre-fb601 blob has no <PARAM id="SYN_OSC_x_HARM_TABLE"> at all, so a cert for it has to be
//    able to DELETE one and to ASK whether one is there.)
[[maybe_unused]] static bool dropParam (std::string& xml, const std::string& id)
{
    const std::string key = "<PARAM id=\"" + id + "\"";
    const size_t at = xml.find (key); if (at == std::string::npos) return false;
    const size_t end = xml.find ("/>", at); if (end == std::string::npos) return false;
    xml.erase (at, end + 2 - at);
    return true;
}
[[maybe_unused]] static bool hasParam (const std::string& xml, const std::string& id)
{ return xml.find ("<PARAM id=\"" + id + "\"") != std::string::npos; }

// ── slice a marked block out of a source file, VERBATIM ────────────────────────────────────────
//    A migration cert that transcribes the branch it is testing proves arithmetic and nothing
//    about the shipped code. Printing the real lines — between markers the source promises to
//    keep — is the cheap half of the fix; driving the real setStateInformation is the other half.
//    Returns "" if either marker is gone, which the caller must treat as a RED bar: a promissory
//    comment with no cert behind it is exactly the hole this header exists to close.
[[maybe_unused]] static std::string sliceMarked (const char* path, const char* beginMark, const char* endMark)
{
    std::ifstream f (path); if (! f) return "";
    std::stringstream ss; ss << f.rdbuf(); const std::string src = ss.str();
    const size_t b = src.find (beginMark); if (b == std::string::npos) return "";
    const size_t bl = src.find ('\n', b);  if (bl == std::string::npos) return "";
    const size_t e = src.find (endMark, bl); if (e == std::string::npos) return "";
    const size_t el = src.rfind ('\n', e);  if (el == std::string::npos || el <= bl) return "";
    return src.substr (bl + 1, el - bl - 1);
}

// ── fb602: the project's loudness metric, harm_table_au.cpp:134-136 VERBATIM ───────────────────
//    Report the NUMBER, never a boolean: "restored" and "restored to silence" are the same
//    boolean and completely different bugs.
[[maybe_unused]] static double rmsDb (const std::vector<float>& v, size_t from = 0)
{ double s = 0; size_t n = 0; for (size_t i = from; i < v.size(); ++i) { s += (double) v[i]*v[i]; ++n; }
  return 10.0 * std::log10 (std::max (1e-20, s / std::max<size_t> (1, n))); }
