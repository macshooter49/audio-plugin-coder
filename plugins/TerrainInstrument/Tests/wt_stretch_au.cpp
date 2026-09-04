// ══════════════════════════════════════════════════════════════════════════════════════════════
//  wt_stretch_au.cpp — fb583: THE INSTALLED PLUGIN REALLY APPLIES STRETCH.
//
//    clang++ -std=c++17 -O2 Tests/wt_stretch_au.cpp -o /tmp/wt_stretch_au \
//        -framework AudioToolbox -framework CoreFoundation -framework CoreAudio && /tmp/wt_stretch_au
//
//  Tests/wt_stretch_cert.cpp proves the TRANSFORM — offline, against the real factory bank, where
//  it can afford a hundred table bakes per bar. This file proves the WIRING: that the shipping AU
//  actually calls it. Those are different claims and the second is the one fb469 warned about —
//  "a display feed is not a control signal" was a bug that BUILT CLEAN AND LOOKED WIRED, and was
//  only caught on the installed plugin. STRETCH is baked on the message thread by the 60 Hz timer,
//  so every bar here renders through the real AU and pumps the run loop to let that timer run.
//
//  ⚠️ THE SYNTH IS NOT REPEATABLE NOTE TO NOTE BY DESIGN (per-voice drift and start phase), so
//     every bar averages several notes and is read against a MEASURED noise floor, never against
//     a single A/B. That lesson is fb582's, and it stands.
//
//  THE BARS
//   0  the AU exposes the knob under its NEW name — "Stretch", not "Spread" (the rename shipped)
//   1  AT ONE VOICE IT STILL WORKS — the fb582 failure was a knob that needed unison; this one
//      must move a single voice, because it is a property of the TABLE, not of the stack
//   2  IT MOVES THE SOUND on the installed plugin, well clear of the synth's own note-to-note wander
//   3  THE PITCH DOES NOT MOVE — the fundamental is pinned, so f0 must land in the same bin
//   4  NO CLICKS while the knob is swept under a held note
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

// a naive DFT magnitude spectrum over one window, normalised to unit energy
static std::vector<double> spectrum (const std::vector<float>& v, size_t off = 4096)
{
    const int N = 1024, BINS = 256;
    std::vector<double> mag ((size_t) BINS, 0.0);
    if (v.size() < off + (size_t) N) return mag;
    for (int k = 1; k <= BINS; ++k)
    {
        double re = 0, im = 0;
        const double w = 2.0 * M_PI * (double) k / (double) N;
        for (int n = 0; n < N; ++n)
        {
            const double x = (double) v[off + (size_t) n] * (0.5 - 0.5 * std::cos (2.0 * M_PI * n / (N - 1)));   // Hann
            re += x * std::cos (w * n); im -= x * std::sin (w * n);
        }
        mag[(size_t) (k - 1)] = std::sqrt (re * re + im * im);
    }
    double e = 0; for (double m : mag) e += m * m;
    e = std::sqrt (std::max (1e-30, e));
    for (double& m : mag) m /= e;      // unit energy: level drops out, only the SHAPE remains
    return mag;
}
static double specDist (const std::vector<double>& a, const std::vector<double>& b)
{ double s = 0; for (size_t i = 0; i < a.size() && i < b.size(); ++i) { const double d = a[i] - b[i]; s += d * d; } return std::sqrt (s); }
static double maxStep (const std::vector<float>& v)
{ double m = 0; for (size_t i = 1; i < v.size(); ++i) m = std::max (m, (double) std::abs (v[i] - v[i - 1])); return m; }
static double diffDb (const std::vector<float>& a, const std::vector<float>& b)
{
    const size_t n = std::min (a.size(), b.size()); if (! n) return 0.0;
    double num = 0, den = 0;
    for (size_t i = 0; i < n; ++i) { const double d = (double) a[i] - b[i]; num += d * d; den += (double) a[i] * a[i]; }
    return 10.0 * std::log10 (std::max (1e-30, num) / std::max (1e-30, den));
}


// the bin of the played note's fundamental, and how much energy sits there
static int f0Bin (const std::vector<double>& sp) { int b = 0; double m = 0;
    for (int i = 0; i < (int) sp.size(); ++i) if (sp[(size_t) i] > m) { m = sp[(size_t) i]; b = i; } return b; }

int main()
{
    std::printf ("\n══ wt_stretch_au — fb583 (the INSTALLED AU) ══\n\n");
    AU a; if (! a.open()) { std::printf ("  cannot open the AU — is it installed?\n"); return 1; }

    const std::string lvl = a.find ("OSC A Level");
    const std::string wtp = a.find ("OSC A WT Preset");
    const std::string uni = a.find ("OSC A Unison");
    const std::string str = a.find ("OSC A Stretch");
    chk (! lvl.empty() && ! wtp.empty() && ! str.empty(),
         "[0] THE AU EXPOSES 'Stretch' (the rename shipped)",
         "level='" + lvl + "' wt='" + wtp + "' stretch='" + str + "'");
    if (str.empty()) { std::printf ("\n  the knob is not exposed under that name — nothing else can be measured.\n"); a.close(); return 1; }
    if (a.find ("OSC A Spread") != "") chk (false, "[0b] the OLD name is gone", "still exposes 'Spread'");

    // OSC A alone, on a real table (a sine has nothing to stretch), one voice.
    a.set (lvl, 1.0f);
    a.set (wtp, 4.0f / 45.0f);          // Prophet Saw — 24 harmonics, plenty to move
    if (! uni.empty()) a.set (uni, 0.0f);   // ONE voice: stretch may not need a stack
    a.pump (0.4);

    const int NOTES[6] = { 48, 55, 60, 64, 67, 72 };

    auto renderAt = [&] (float stretch, int note)
    {
        a.set (str, stretch);
        a.render (2); a.pump (0.35);        // let the 60 Hz timer bake and publish the table
        return a.note (note);
    };

    // ── the FLOOR: the same settings, twice, is not the same audio on this synth ──
    double floorD = 0.0;
    for (int i = 0; i < 6; ++i)
    { const auto x = renderAt (0.0f, NOTES[i]); const auto y = renderAt (0.0f, NOTES[i]);
      floorD += specDist (spectrum (x), spectrum (y)) / 6.0; }

    // ── bar 1 + 2: one voice, stretch 0 against stretch 1 ──
    double moved = 0.0, lvlDelta = 0.0; int binShift = 0;
    for (int i = 0; i < 6; ++i)
    {
        const auto dry = renderAt (0.0f, NOTES[i]);
        const auto wet = renderAt (1.0f, NOTES[i]);
        const auto sd = spectrum (dry), sw = spectrum (wet);
        moved   += specDist (sd, sw) / 6.0;
        lvlDelta += std::abs (rmsDb (wet) - rmsDb (dry)) / 6.0;
        binShift = std::max (binShift, std::abs (f0Bin (sd) - f0Bin (sw)));
    }
    char b[256];
    std::snprintf (b, sizeof b, "one voice: moved %.3f against a %.3f noise floor (%.1fx)",
                   moved, floorD, floorD > 1e-6 ? moved / floorD : 0.0);
    chk (moved > floorD * 3.0, "[1] AT ONE VOICE IT STILL WORKS — no unison needed", b);

    std::snprintf (b, sizeof b, "spectral move %.3f, level moved %.2f dB while it did", moved, lvlDelta);
    chk (moved > floorD * 3.0 && lvlDelta < 6.0, "[2] IT MOVES THE SOUND, AND IT IS TIMBRE", b);

    std::snprintf (b, sizeof b, "fundamental moved %d DFT bins across the whole knob (must be 0)", binShift);
    chk (binShift == 0, "[3] THE PITCH DOES NOT MOVE — the fundamental is pinned", b);

    // ── bar 4: sweep the knob under a held note ──
    a.set (str, 0.0f); a.render (2); a.pump (0.3);
    a.midi (0x90, 60, 100); a.render (8);
    std::vector<float> swept;
    for (int k = 0; k <= 20; ++k)
    { a.set (str, (float) k / 20.0f); const auto chunk = a.render (3);
      swept.insert (swept.end(), chunk.begin(), chunk.end());
      CFRunLoopRunInMode (kCFRunLoopDefaultMode, 0.02, false); }
    a.midi (0x80, 60, 0); a.render (20);
    a.set (str, 0.0f); a.render (2); a.pump (0.3);
    a.midi (0x90, 60, 100); a.render (8);
    const auto steady = a.render (63);
    a.midi (0x80, 60, 0); a.render (20);
    const double sweptStep = maxStep (swept), steadyStep = maxStep (steady);
    std::snprintf (b, sizeof b, "biggest sample step while sweeping %.5f vs %.5f held still (%.2fx)",
                   sweptStep, steadyStep, steadyStep > 1e-9 ? sweptStep / steadyStep : 0.0);
    chk (sweptStep < steadyStep * 4.0, "[4] NO CLICKS WHILE SWEEPING THE KNOB", b);

    a.close();
    std::printf ("\n══ RESULT: %d pass, %d FAIL ══\n\n", pass, fail);
    return fail == 0 ? 0 : 1;
}
