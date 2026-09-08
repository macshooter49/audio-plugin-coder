// ══════════════════════════════════════════════════════════════════════════════════════════════
//  fm_null_dump.cpp — fb587's NULL TEST, half one: render the FM engine and dump the samples.
//
//    clang++ -std=c++17 -O2 Tests/fm_null_dump.cpp -o /tmp/fm_null_dump \
//        -framework AudioToolbox -framework CoreFoundation -framework CoreAudio
//    /tmp/fm_null_dump <out.raw>
//
//  Moving the FM operator stage out of the render loop and into a shared header is a refactor of
//  the HOT AUDIO PATH, four times over. The only honest way to ship that is to prove the render is
//  UNCHANGED — so this dumps the exact samples before the move and again after, and they must
//  match bit for bit.
//
//  ⚠️ THAT ONLY WORKS IF THE SYNTH IS DETERMINISTIC, AND BY DESIGN IT IS NOT: per-voice drift and
//     RANDOM start phase mean two renders of one note differ by ~5 dB (fb582 measured it, and
//     every cert since averages six notes against a floor because of it). So this pins the settings
//     that cause it — ONE unison voice, phase mode RETRIG — and its FIRST job is to report whether
//     two back-to-back renders are identical. If they are not, a bit-exact null test is impossible
//     and the refactor needs a different proof.
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



int main (int argc, char** argv)
{
    const char* outPath = (argc > 1) ? argv[1] : "/tmp/fm_null.raw";
    std::printf ("\n══ fm_null_dump — fb587 ══\n\n");
    AU a; if (! a.open()) { std::printf ("  cannot open the AU\n"); return 1; }

    auto need = [&] (const char* n) { const std::string s = a.find (n);
        if (s.empty()) std::printf ("  !! missing param: %s\n", n); return s; };

    const std::string eng = need ("OSC A Engine");
    const std::string lvl = need ("OSC A Level");
    const std::string uni = need ("OSC A Unison");
    const std::string pha = a.find ("OSC A Phase Mode");
    const std::string wtp = need ("OSC A WT Preset");

    // pin everything that makes this synth deliberately non-repeatable
    a.set (lvl, 1.0f);
    a.set (wtp, 4.0f / 45.0f);            // Prophet Saw
    if (! uni.empty()) a.set (uni, 0.0f); // ONE voice — no detune fan, no per-voice drift
    if (! pha.empty()) a.set (pha, 0.0f); // RETRIG — not RANDOM
    // FM engine: the choice is 0..6, FM is index 4
    if (! eng.empty()) a.set (eng, 4.0f / 6.0f);
    a.pump (0.5);

    // a spread of FM settings so the dump exercises every branch: algorithms, feedback,
    // and the weathering suite (STORM / RUST / SCORCH / QUAKE)
    struct KV { const char* name; float v; };
    const KV rig[] = { {"OSC A FM Ratio 1", 0.30f}, {"OSC A FM Depth 1", 0.55f},
                       {"OSC A FM Ratio 2", 0.45f}, {"OSC A FM Depth 2", 0.40f},
                       {"OSC A FM Feedback", 0.35f}, {"OSC A FM Strike", 0.30f},
                       {"OSC A FM Age", 0.25f}, {"OSC A FM Rust", 0.35f},
                       {"OSC A FM Quake", 0.45f}, {"OSC A FM Scorch", 0.55f},
                       {"OSC A FM Storm", 0.40f} };
    for (auto& k : rig) { const std::string s = a.find (k.name); if (! s.empty()) a.set (s, k.v); }
    a.pump (0.4);

    const std::string alg = a.find ("OSC A FM Algo");

    // render every algorithm, so the dump covers STACK, SPLIT and RING
    std::vector<float> all;
    for (int algo = 0; algo < 3; ++algo)
    {
        if (! alg.empty()) a.set (alg, algo / 2.0f);
        a.render (2); a.pump (0.35);
        a.midi (0x90, 48, 100); a.render (6);
        const auto body = a.render (40);
        a.midi (0x80, 48, 0); a.render (12);
        all.insert (all.end(), body.begin(), body.end());
    }

    // determinism check: do it all again and compare
    std::vector<float> again;
    for (int algo = 0; algo < 3; ++algo)
    {
        if (! alg.empty()) a.set (alg, algo / 2.0f);
        a.render (2); a.pump (0.35);
        a.midi (0x90, 48, 100); a.render (6);
        const auto body = a.render (40);
        a.midi (0x80, 48, 0); a.render (12);
        again.insert (again.end(), body.begin(), body.end());
    }

    size_t diff = 0; double worst = 0.0;
    for (size_t i = 0; i < all.size() && i < again.size(); ++i)
        if (all[i] != again[i]) { ++diff; worst = std::max (worst, (double) std::abs (all[i] - again[i])); }

    double rms = 0; for (float v : all) rms += (double) v * v;
    rms = std::sqrt (rms / std::max<size_t> (1, all.size()));

    std::printf ("  rendered %zu samples across 3 algorithms, rms %.6f\n", all.size(), rms);
    std::printf ("  %s  back-to-back renders differ in %zu samples (worst |d| %.3e)\n",
                 diff == 0 ? "DETERMINISTIC —" : "NOT DETERMINISTIC —", diff, worst);
    if (rms < 1e-6) std::printf ("  !! the render is SILENT — the rig did not take; the dump is worthless\n");

    FILE* f = std::fopen (outPath, "wb");
    if (f) { std::fwrite (all.data(), sizeof (float), all.size(), f); std::fclose (f);
             std::printf ("  wrote %s\n", outPath); }
    a.close();
    return (diff == 0 && rms > 1e-6) ? 0 : 1;
}
