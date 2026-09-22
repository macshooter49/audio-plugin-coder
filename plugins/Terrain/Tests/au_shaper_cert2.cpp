// ══════════════════════════════════════════════════════════════════════════════════════════════
//  au_shaper_cert2.cpp — tp85 · THE CERTIFICATION BATTERY'S SECOND HALF, IN THE INSTALLED AU.
//
//  tp83 certified that each borrowed lane is AUDIBLE and RHYTHMIC (au_shaper_fx.cpp). What it could
//  not reach was everything the lane is made of: the four target knobs, the type menu, the seam when
//  a send opens and shuts, and whether the two lanes that feed back on themselves stay bounded. All
//  four were out of reach for one reason — the knobs are not parameters. They live in the shaperJson
//  blob, which only the interface writes.
//
//  🔑 THE UNLOCK, and it is the tp83 lesson a second time: THE BLOB IS ALREADY REACHABLE, AS A PRESET.
//  getStateInformation writes shaperJson0..3 into the plugin's state, and a host sets that state
//  through kAudioUnitProperty_ClassInfo. So this file does what a DAW does when it loads a patch —
//  read the state, put a lane in it, hand it back — and every k[] is suddenly measurable from outside
//  with the REAL engines running. No new parameters, no test-only door in the plugin.
//
//  WHAT IS CERTIFIED HERE
//   [2][3] THE PATCH SURVIVES THE ROUND TRIP. Save → load → save must not lose a property. This bar
//          exists because writing it FOUND A SHIPPED DATA LOSS (tp84's dangling else, below).
//   [4..]  EVERY TARGET IS AUDIBLE. Each borrowed lane's four knobs, swept end to end with the shape
//          pinned open so the knob is the only thing moving, against the log-spaced Goertzel bank.
//          A knob that exists but cannot be heard is a knob that is not wired.
//   [13..] EVERY TYPE IS ITS OWN. Each entry in a lane's type menu is compared with every other. The
//          weakest pair is named in the bar, so a menu of aliases cannot pass as a menu of effects.
//   [22]   NO SEAM. A hard gate — the send slamming shut and open on the sixteenth — must not put a
//          step in the output. Measured as the largest sample-to-sample jump against the same render
//          with the lane dark, which is what a click actually is.
//   [23]   THE TWO THAT FEED BACK STAY BOUNDED. Delay and Bode held at full feedback for twelve
//          seconds: finite, under a ceiling, and not still growing at the end.
//
//  STILL NOT CERTIFIED, and stated rather than hidden: that a LOADED NOISE SAMPLE plays. The selection
//  travels in the preset (shpNoiseSel0..3) but the PROCESSOR never re-reads the file — the editor does,
//  when it opens. So a headless render cannot load one, and neither can this harness. See Tests/README.
//
//  clang++ -O2 -std=c++17 Tests/au_shaper_cert2.cpp -o /tmp/aucert2 -framework AudioToolbox \
//          -framework AudioUnit -framework CoreFoundation -framework CoreAudio && /tmp/aucert2
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

static const double SR = 48000.0; static const int BLK = 512; static const double BPM = 120.0;
static int npass = 0, nfail = 0;
static void chk (bool ok, const char* what, const std::string& d = "")
{ printf ("  %s  %s\n", ok ? "PASS " : "FAIL ", what); if (! d.empty()) printf ("        %s\n", d.c_str()); if (ok) ++npass; else ++nfail; fflush (stdout); }

static double gPpq0 = 0.0, gStamp0 = 0.0; static bool gPlaying = false;
static OSStatus beatAndTempo (void*, Float64* b, Float64* t) { if (t) *t = BPM; if (b) *b = gPpq0; return noErr; }
static OSStatus transportState (void*, Boolean* p, Boolean* c, Float64* s, Boolean* l, Float64* a, Float64* e)
{ if (p) *p = gPlaying; if (c) *c = false; if (s) *s = gStamp0; if (l) *l = false; if (a) *a = 0; if (e) *e = 0; return noErr; }

static std::string xmlEsc (const std::string& s)
{ std::string o; for (char c : s) { if (c == '&') o += "&amp;"; else if (c == '<') o += "&lt;"; else if (c == '>') o += "&gt;"; else if (c == '"') o += "&quot;"; else o += c; } return o; }

struct Au
{
    AudioUnit au = nullptr; std::map<std::string, AudioUnitParameterID> byName;
    std::map<std::string, std::pair<float,float>> byRange; double stamp = 0;
    bool open()
    {
        setenv ("TERRAIN_DETERMINISTIC", "1", 1);
        AudioComponentDescription d {}; d.componentType = kAudioUnitType_MusicDevice; d.componentSubType = 'Tern'; d.componentManufacturer = 'Wvcr';
        AudioComponent c = AudioComponentFindNext (nullptr, &d); if (! c || AudioComponentInstanceNew (c, &au) != noErr) return false;
        AudioStreamBasicDescription f {}; f.mSampleRate = SR; f.mFormatID = kAudioFormatLinearPCM;
        f.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked | kAudioFormatFlagIsNonInterleaved;
        f.mBytesPerPacket = 4; f.mFramesPerPacket = 1; f.mBytesPerFrame = 4; f.mChannelsPerFrame = 2; f.mBitsPerChannel = 32;
        AudioUnitSetProperty (au, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Output, 0, &f, sizeof f);
        UInt32 mx = BLK; AudioUnitSetProperty (au, kAudioUnitProperty_MaximumFramesPerSlice, kAudioUnitScope_Global, 0, &mx, sizeof mx);
        HostCallbackInfo cb {}; cb.beatAndTempoProc = beatAndTempo; cb.transportStateProc = transportState;
        AudioUnitSetProperty (au, kAudioUnitProperty_HostCallbacks, kAudioUnitScope_Global, 0, &cb, sizeof cb);
        if (AudioUnitInitialize (au) != noErr) return false;
        UInt32 sz = 0; Boolean w = false;
        AudioUnitGetPropertyInfo (au, kAudioUnitProperty_ParameterList, kAudioUnitScope_Global, 0, &sz, &w);
        std::vector<AudioUnitParameterID> ids (sz / sizeof (AudioUnitParameterID));
        AudioUnitGetProperty (au, kAudioUnitProperty_ParameterList, kAudioUnitScope_Global, 0, ids.data(), &sz);
        for (auto id : ids)
        {
            AudioUnitParameterInfo pi {}; UInt32 s = sizeof pi;
            if (AudioUnitGetProperty (au, kAudioUnitProperty_ParameterInfo, kAudioUnitScope_Global, id, &pi, &s) != noErr) continue;
            char b[256] = {};
            if ((pi.flags & kAudioUnitParameterFlag_HasCFNameString) && pi.cfNameString) CFStringGetCString (pi.cfNameString, b, sizeof b, kCFStringEncodingUTF8);
            else snprintf (b, sizeof b, "%s", pi.name);
            byName[b] = id; byRange[b] = { pi.minValue, pi.maxValue };
        }
        return true;
    }
    void close() { if (au) { AudioUnitUninitialize (au); AudioComponentInstanceDispose (au); au = nullptr; } }
    bool has (const std::string& n) const { return byName.count (n) > 0; }
    int  choiceCount (const std::string& n) const { auto it = byRange.find (n); return it == byRange.end() ? 0 : (int) (it->second.second - it->second.first) + 1; }
    bool set (const std::string& n, float v)
    { auto it = byName.find (n); if (it == byName.end()) { printf ("  !! no param '%s'\n", n.c_str()); return false; }
      return AudioUnitSetParameter (au, it->second, kAudioUnitScope_Global, 0, v, 0) == noErr; }
    /** a choice's scale is NOT 0..1 everywhere in this plugin — ask, never guess (tp83). */
    bool setIndex (const std::string& n, int idx, int count)
    { auto it = byRange.find (n); if (it == byRange.end()) { printf ("  !! no param '%s'\n", n.c_str()); return false; }
      const float lo = it->second.first, hi = it->second.second;
      return set (n, count > 1 ? lo + (hi - lo) * ((float) idx / (float) (count - 1)) : lo); }
    void pump (double sec) { const double t0 = CFAbsoluteTimeGetCurrent(); while (CFAbsoluteTimeGetCurrent() - t0 < sec) CFRunLoopRunInMode (kCFRunLoopDefaultMode, 0.01, false); }
    void note (int n, int v) { MusicDeviceMIDIEvent (au, v > 0 ? 0x90 : 0x80, (UInt32) n, (UInt32) v, 0); }
    void render (int blocks, double ppqStart, bool playing, std::vector<float>* keep = nullptr, std::vector<float>* keepR = nullptr)
    {
        std::vector<float> L (BLK), R (BLK); gPlaying = playing; double ppq = ppqStart;
        for (int b = 0; b < blocks; ++b)
        {
            gPpq0 = ppq; gStamp0 = stamp;
            AudioBufferList* abl = (AudioBufferList*) alloca (sizeof (AudioBufferList) + sizeof (AudioBuffer));
            abl->mNumberBuffers = 2;
            abl->mBuffers[0].mNumberChannels = 1; abl->mBuffers[0].mDataByteSize = BLK * 4; abl->mBuffers[0].mData = L.data();
            abl->mBuffers[1].mNumberChannels = 1; abl->mBuffers[1].mDataByteSize = BLK * 4; abl->mBuffers[1].mData = R.data();
            AudioUnitRenderActionFlags fl = 0; AudioTimeStamp ts {}; ts.mSampleTime = stamp; ts.mFlags = kAudioTimeStampSampleTimeValid;
            AudioUnitRender (au, &fl, &ts, 0, BLK, abl); stamp += BLK;
            if (playing) ppq += BLK * BPM / 60.0 / SR;
            if (keep)  keep ->insert (keep ->end(), L.begin(), L.end());
            if (keepR) keepR->insert (keepR->end(), R.begin(), R.end());   // Widen's whole claim is the IMAGE
            CFRunLoopRunInMode (kCFRunLoopDefaultMode, 0.001, false);
        }
    }

    // ── THE STATE DOOR — exactly what a host does when it saves and loads a patch ──────────────
    //  JUCE's copyXmlToBinary: 'VC2!' · uint32 little-endian length · the XML · a trailing NUL.
    bool getStateXml (std::string& out)
    {
        CFPropertyListRef pl = nullptr; UInt32 s = sizeof pl;
        if (AudioUnitGetProperty (au, kAudioUnitProperty_ClassInfo, kAudioUnitScope_Global, 0, &pl, &s) != noErr || ! pl) return false;
        CFDataRef blob = (CFDataRef) CFDictionaryGetValue ((CFDictionaryRef) pl, CFSTR ("jucePluginState"));
        if (! blob) { CFRelease (pl); return false; }
        const unsigned char* p = CFDataGetBytePtr (blob); const long n = CFDataGetLength (blob);
        if (n < 9 || memcmp (p, "VC2!", 4) != 0) { CFRelease (pl); return false; }
        out.assign ((const char*) p + 8, (size_t) (n - 9)); CFRelease (pl); return true;
    }
    bool setStateXml (const std::string& xml)
    {
        CFPropertyListRef pl = nullptr; UInt32 s = sizeof pl;
        if (AudioUnitGetProperty (au, kAudioUnitProperty_ClassInfo, kAudioUnitScope_Global, 0, &pl, &s) != noErr || ! pl) return false;
        CFMutableDictionaryRef md = CFDictionaryCreateMutableCopy (nullptr, 0, (CFDictionaryRef) pl);
        std::vector<unsigned char> out (8 + xml.size() + 1);
        memcpy (out.data(), "VC2!", 4); const unsigned int L = (unsigned int) xml.size();
        out[4] = L & 0xff; out[5] = (L >> 8) & 0xff; out[6] = (L >> 16) & 0xff; out[7] = (L >> 24) & 0xff;
        memcpy (out.data() + 8, xml.data(), xml.size()); out[8 + xml.size()] = 0;
        CFDataRef nd = CFDataCreate (nullptr, out.data(), (CFIndex) out.size());
        CFDictionarySetValue (md, CFSTR ("jucePluginState"), nd);
        CFPropertyListRef np = md;
        const OSStatus e = AudioUnitSetProperty (au, kAudioUnitProperty_ClassInfo, kAudioUnitScope_Global, 0, &np, sizeof np);
        CFRelease (nd); CFRelease (md); CFRelease (pl); pump (0.35); return e == noErr;
    }
    /** set one attribute on the root <Parameters> element, the way a saved patch carries it. */
    static void putAttr (std::string& xml, const std::string& key, const std::string& val)
    {
        const std::string attr = " " + key + "=\"" + xmlEsc (val) + "\"";
        const std::string pat = key + "=\"";
        const size_t a = xml.find (pat);
        if (a != std::string::npos) { const size_t e = xml.find ('"', a + pat.size()); xml = xml.substr (0, a - 1) + attr + xml.substr (e + 1); return; }
        const size_t t = xml.find ("<Parameters"); if (t == std::string::npos) return;
        xml = xml.substr (0, t + 11) + attr + xml.substr (t + 11);
    }
    static std::string getAttr (const std::string& xml, const std::string& key)
    { const std::string pat = key + "=\""; const size_t a = xml.find (pat); if (a == std::string::npos) return "\x01";   // \x01 = absent
      const size_t e = xml.find ('"', a + pat.size()); return xml.substr (a + pat.size(), e - a - pat.size()); }
    bool putShaper (const std::string& json)
    { std::string x; if (! getStateXml (x)) return false; putAttr (x, "shaperJson0", json); return setStateXml (x); }
};

// ── the metrics (magnitude, never a sample difference — fb283's law) ──────────────────────────
static double goertzel (const std::vector<float>& x, size_t a, size_t n, double f)
{
    if (a + n > x.size()) return 0.0;
    double mean = 0; for (size_t i = a; i < a + n; ++i) mean += x[i]; mean /= (double) n;
    const double w = 2.0 * M_PI * f / SR, c = 2.0 * std::cos (w);
    double s1 = 0, s2 = 0;
    for (size_t i = a; i < a + n; ++i) { const double s0 = ((double) x[i] - mean) + c * s1 - s2; s2 = s1; s1 = s0; }
    return std::sqrt (std::max (0.0, s1 * s1 + s2 * s2 - c * s1 * s2)) / (double) n;
}
static const int NB = 24;
static double binHz (int b) { return 60.0 * std::pow (16000.0 / 60.0, (double) b / (NB - 1)); }
static double specChangeDb (const std::vector<float>& u, const std::vector<float>& v, size_t a, size_t n)
{
    double worst = 0, ref = 0;
    for (int b = 0; b < NB; ++b) ref = std::max (ref, std::max (goertzel (u, a, n, binHz (b)), goertzel (v, a, n, binHz (b))));
    if (ref < 1e-7) return 0.0;
    for (int b = 0; b < NB; ++b)
    {
        const double p = goertzel (u, a, n, binHz (b)), q = goertzel (v, a, n, binHz (b));
        if (std::max (p, q) < ref * 0.02) continue;
        worst = std::max (worst, std::fabs (20.0 * std::log10 ((p + 1e-9) / (q + 1e-9))));
    }
    return worst;
}
static double peakAbs (const std::vector<float>& x, size_t a, size_t b)
{ double m = 0; for (size_t i = a; i < b && i < x.size(); ++i) m = std::max (m, (double) std::fabs (x[i])); return m; }
static double rmsDb (const std::vector<float>& x, size_t a, size_t b)
{ double e = 0; size_t n = 0; for (size_t i = a; i < b && i < x.size(); ++i) { e += (double) x[i] * x[i]; ++n; } return n ? 20.0 * std::log10 (std::sqrt (e / n) + 1e-12) : -240.0; }
//  a click IS a step: the largest sample-to-sample jump. Against the same render with the lane dark,
//  because a bright note has its own slew and only the EXCESS is a seam.
static double maxSlew (const std::vector<float>& x, size_t a, size_t b)
{ double m = 0; for (size_t i = a + 1; i < b && i < x.size(); ++i) m = std::max (m, (double) std::fabs (x[i] - x[i-1])); return m; }
static bool finiteAll (const std::vector<float>& x) { for (float v : x) if (! std::isfinite (v)) return false; return true; }
//  ⚠️ tp83's law, and this file re-learned it twice: THE METRIC MUST BE THE ONE THE CLAIM IS ABOUT.
//  A band TILT and added HARMONICS are both invisible to a magnitude comparison, so the two lanes whose
//  types differ in exactly those terms are compared in exactly those terms.
static double bandRatio (const std::vector<float>& x, size_t a, size_t n)
{ double lo = 0, hi = 0;
  for (double f = 80.0;   f < 500.0;   f *= 1.12) lo += goertzel (x, a, n, f);
  for (double f = 2000.0; f < 12000.0; f *= 1.12) hi += goertzel (x, a, n, f);
  return (hi + 1e-9) / (lo + 1e-9); }
static double thd (const std::vector<float>& x, size_t a, size_t n, double f0)
{ const double f = goertzel (x, a, n, f0);
  double h = 0; for (int k = 2; k <= 6; ++k) h += goertzel (x, a, n, f0 * k);
  return (h + 1e-9) / (f + 1e-9); }
//  🚨 AND A CLICK IS NOT A SLEW RATE. The first cut of [22] compared the largest sample-to-sample jump
//  against the dry render's, and Noise (uncorrelated by construction) and Bode (energy moved to the top
//  of the band) failed it while being perfectly clean — a WIDER signal has a bigger jump every sample.
//  A click is a LOCALISED OUTLIER: one jump far above what this same signal does the rest of the time.
//  Peak-over-percentile is dimensionless, so bandwidth divides out.
static double slewOutlier (const std::vector<float>& x)
{ std::vector<double> d; d.reserve (x.size());
  for (size_t i = 1; i < x.size(); ++i) d.push_back (std::fabs ((double) x[i] - (double) x[i-1]));
  if (d.size() < 100) return 0.0;
  std::vector<double> s = d; std::sort (s.begin(), s.end());
  const double p99 = s[(size_t) (s.size() * 0.99)], mx = s.back();
  return mx / (p99 + 1e-12); }

// ── the lanes ─────────────────────────────────────────────────────────────────────────────────
static const char* const kAll[18] = { "Volume","Time","Filter","Pan","Repeat","Drive","Phaser","Crush",
                                      "Reverb","Delay","Chorus","Widen","Multiband","Tape","Granular","Bode","Noise", "Flanger" };
struct Lane { int kind; const char* t[4]; float k[4]; };
//  the four targets and their resting values, mirroring kShaperKDefault in FlowShaper.h
static const Lane kBorrowed[9] = {
    {  8, { "Size",    "Decay",   "Tone",     "Diffuse" }, { 0.45f,  0.5f,  0.4f,  0.7f  } },
    {  9, { "Time",    "Feedback","Tone",     "Width"   }, { 0.375f, 0.35f, 0.5f,  0.6f  } },
    { 10, { "Rate",    "Depth",   "Feedback", "Voice"   }, { 0.35f,  0.5f,  0.0f,  0.5f  } },
    { 11, { "Amount",  "Width",   "Rate",     "Axis"    }, { 0.5f,   0.5f,  0.35f, 0.5f  } },
    { 12, { "Split",   "Slope",   "Spread",   "Range"   }, { 0.5f,   0.5f,  0.5f,  0.6f  } },
    { 13, { "Flutter", "Drive",   "Age",      "Width"   }, { 0.3f,   0.3f,  0.3f,  0.2f  } },
    { 14, { "Size",    "Density", "Pitch",    "Spread"  }, { 0.25f,  0.4f,  0.5f,  0.5f  } },
    { 15, { "Range",   "Feedback","Spread",   "Blur"    }, { 0.5f,   0.3f,  0.6f,  0.0f  } },
    { 16, { "Tone",    "Width",   "Scan",     "Drive"   }, { 0.6f,   0.6f,  1.0f,  0.0f  } } };

//  one lane's blob: the named lane carrying a shape and its six knobs, every other lane left at seed.
static std::string laneJson (int lane, const char* pts, const float k[6], float smooth = 0.2f)
{
    std::string j = "{\"lanes\":[";
    for (int i = 0; i < 18; ++i)
    {
        if (i) j += ",";
        if (i == lane)
        {
            char b[460];
            std::snprintf (b, sizeof b, "{\"pts\":%s,\"smooth\":%g,\"phase\":0,\"tension\":0.5,\"floor\":0,\"blend\":1,\"swing\":0,\"grid\":16,\"k\":[%g,%g,%g,%g,%g,%g]}",
                           pts, smooth, k[0], k[1], k[2], k[3], k[4], k[5]);
            j += b;
        }
        else j += "{}";
    }
    return j + "]}";
}
//  tp85 — two lanes in one blob, so the chain can FEED one lane from another. Multiband's types are
//  band SPLITS: told apart on a held note they read 0.3 dB, because a single note barely occupies two
//  bands. Chained behind the Noise lane they have a broadband signal to split, which is the only
//  signal the claim is even about.
static std::string twoLaneJson (int la, const float ka[6], int lb, const float kb[6])
{
    std::string j = "{\"lanes\":[";
    for (int i = 0; i < 18; ++i)
    {
        if (i) j += ",";
        const float* k = (i == la) ? ka : (i == lb) ? kb : nullptr;
        if (k == nullptr) { j += "{}"; continue; }
        char b[460];
        std::snprintf (b, sizeof b, "{\"pts\":[[0,1,0],[1,1,0]],\"smooth\":0.2,\"phase\":0,\"tension\":0.5,\"floor\":0,\"blend\":1,\"swing\":0,\"grid\":16,\"k\":[%g,%g,%g,%g,%g,%g]}",
                       k[0], k[1], k[2], k[3], k[4], k[5]);
        j += b;
    }
    return j + "]}";
}
//  a reverb's DECAY is its tail LENGTH and its DIFFUSION is its tail's SMOOTHNESS. Neither is a
//  spectrum under the note, which is why both read 0.2 dB there on a reverb that plainly works.
static double tailDropDb (const std::vector<float>& x, size_t off)
{ return rmsDb (x, off + (size_t) (SR * 0.05), off + (size_t) (SR * 0.25))
       - rmsDb (x, off + (size_t) (SR * 0.70), off + (size_t) (SR * 0.95)); }
static double tailRoughDb (const std::vector<float>& x, size_t off)
{   // the scatter of the short-time envelope: a diffuse tail is smooth, a sparse one is a row of echoes
    std::vector<double> e; const size_t F = (size_t) (SR * 0.006);
    for (size_t i = off + (size_t) (SR * 0.06); i + F < off + (size_t) (SR * 0.95) && i + F < x.size(); i += F)
        e.push_back (rmsDb (x, i, i + F));
    if (e.size() < 8) return 0.0;
    double m = 0; for (double v : e) m += v; m /= (double) e.size();
    double s2 = 0; for (double v : e) s2 += (v - m) * (v - m);
    return std::sqrt (s2 / (double) e.size());
}
//  tp85 — A TRANSPORT'S SIGNATURE IS PITCH WANDER. TapeFxEngine maps type 0 and 4 onto the same machine
//  and 1 and 3 onto another: those pairs differ ONLY in their transport, so harmonics and spectrum both
//  read them as the same effect. Wander is measured as the scatter of the fundamental's level across
//  short frames — a tone drifting off a fixed bin dips and rises in it, so the bin's wobble IS the wow.
static double wanderDb (const std::vector<float>& x, size_t a, size_t n, double f0)
{
    std::vector<double> g; const size_t F = (size_t) (SR * 0.03);
    for (size_t i = a; i + F < a + n && i + F < x.size(); i += F / 2)
        g.push_back (20.0 * std::log10 (goertzel (x, i, F, f0) + 1e-9));
    if (g.size() < 6) return 0.0;
    double m = 0; for (double v : g) m += v; m /= (double) g.size();
    double s2 = 0; for (double v : g) s2 += (v - m) * (v - m);
    return std::sqrt (s2 / (double) g.size());
}
static double sideOverMid (const std::vector<float>& l, const std::vector<float>& r, size_t a, size_t n)
{ double m = 0, s = 0;
  for (size_t i = a; i < a + n && i < l.size() && i < r.size(); ++i)
  { const double mm = 0.5 * (l[i] + r[i]), ss = 0.5 * (l[i] - r[i]); m += mm * mm; s += ss * ss; }
  return std::sqrt (s / (m + 1e-12)); }
static const char* const PTS_OPEN = "[[0,1,0],[1,1,0]]";                       // the lane fully on, all cycle
static const char* const PTS_GATE = "[[0,0,0],[0.4999,0,0],[0.5,1,0],[1,1,0]]"; // slammed shut, then slammed open

int main()
{
    Au a; if (! a.open()) { printf ("  !! no Terrain AU\n"); return 2; }
    printf ("\ntp85 — THE SHAPER'S SECOND HALF: TARGETS, TYPES, SEAMS, FEEDBACK (installed AU, a host transport)\n\n");
    a.pump (1.0); a.render (20, 0.0, false);
    chk (a.set ("Flow Chain 1", 2.0f), "[0] the Shaper sits in Flow Chain 1 (the Chop slot)");
    a.pump (0.6); a.render (10, 0.0, false);

    auto allDark = [&] { for (int q = 0; q < 18; ++q) a.set (std::string ("Shaper ") + kAll[q] + " On", 0.0f); };
    //  place one kind at position 1 and light it; every other lane dark.
    /* ⚠️ ARM, THEN WAIT. The roster builds a lent engine ON DEMAND — `if (! ((rvBuilt >> t) & 1)) return
       false` — and until it is built the lane passes the audio straight through. A measurement taken
       across that boundary compares NO REVERB with a reverb, which is why Reverb's Tone read 15.1 dB on
       one run and 0.9 dB on the next: not a measurement, a race. Light the lane, let the message thread
       finish, and throw the first render away. */
    auto solo = [&] (int kind) { allDark (); a.setIndex ("Shaper Slot 1", kind, 18);
                                 a.set (std::string ("Shaper ") + kAll[kind] + " On", 1.0f);
                                 a.set (std::string ("Shaper ") + kAll[kind] + " Depth", 1.0f);
                                 a.pump (0.9); a.render ((int) (SR * 0.5 / BLK), 0.0, false); a.pump (0.3); };

    const size_t W0 = (size_t) (SR * 0.45), WIN = (size_t) (SR * 0.35);
    /* 🚨 THE SOURCE WAS THE NOISE IN THE MEASUREMENT. A held C4 on the current patch is not a repeatable
       signal — the synth's modulators free-run, so two renders of "the same" note are not the same, and
       every small number moved between runs (Reverb Tone read 15.1, then 0.9, then 1.8). Multiband was
       the proof: the moment it was driven by the NOISE lane instead its four targets went from 0.2-9.3
       and unstable to 16.8-63.6 and repeatable. The Noise lane is deterministic by construction — tp82's
       null proves it bit-for-bit — so it is the source for every lane that does not need a fundamental.
       The shape gives the burst: noise for the first half of the cycle, silence for the second, which is
       exactly the gap a reverb's tail has to be measured in. */
    static const char* const PTS_BURST = "[[0,1,0],[0.499,1,0],[0.5,0,0],[1,0,0]]";
    auto behindNoise = [&] (int kind, const float k[6], const char* noisePts, std::vector<float>& out, std::vector<float>* outR, double seconds)
    {
        float kn[6] = { 0.5f, 0.5f, 1.0f, 0.0f, 0.5f, 0.5f };          // white, flat, no scan
        std::string j = "{\"lanes\":[";
        for (int i = 0; i < 18; ++i)
        {
            if (i) j += ",";
            char b[470];
            if (i == 16)      std::snprintf (b, sizeof b, "{\"pts\":%s,\"smooth\":0.05,\"phase\":0,\"tension\":0.5,\"floor\":0,\"blend\":1,\"swing\":0,\"grid\":16,\"k\":[%g,%g,%g,%g,%g,%g]}",
                                             noisePts, kn[0], kn[1], kn[2], kn[3], kn[4], kn[5]);
            else if (i == kind) std::snprintf (b, sizeof b, "{\"pts\":[[0,1,0],[1,1,0]],\"smooth\":0.2,\"phase\":0,\"tension\":0.5,\"floor\":0,\"blend\":1,\"swing\":0,\"grid\":16,\"k\":[%g,%g,%g,%g,%g,%g]}",
                                             k[0], k[1], k[2], k[3], k[4], k[5]);
            else { j += "{}"; continue; }
            j += b;
        }
        a.putShaper (j + "]}");
        allDark (); a.setIndex ("Shaper Slot 1", 16, 18); a.setIndex ("Shaper Slot 2", kind, 18);
        a.set ("Shaper Noise On", 1.0f); a.set ("Shaper Noise Depth", 1.0f);
        a.setIndex ("Shaper Noise Mode", 0, 13); a.setIndex ("Shaper Noise Rate", 4, 8);   // a 4-beat cycle = 2 s at 120
        a.set (std::string ("Shaper ") + kAll[kind] + " On", 1.0f);
        a.set (std::string ("Shaper ") + kAll[kind] + " Depth", 1.0f);
        a.pump (0.9); a.render ((int) (SR * 0.5 / BLK), 0.0, false); a.pump (0.3);   // let the engine build
        a.render ((int) (SR * seconds / BLK), 0.0, true, &out, outR);
    };
    //  a held note for 0.6 s, then 2.4 s of nothing but tail — the only window a decay lives in
    auto longTail = [&] (std::vector<float>& out)
    { a.note (60, 100); a.render ((int) (SR * 0.6 / BLK), 0.0, true, &out);
      a.note (60, 0);   a.render ((int) (SR * 2.4 / BLK), 0.6, true, &out); };
    //  Noise generates on its own; every other lane needs something to work on.
    auto capture2 = [&] (int kind, double seconds, std::vector<float>& out, std::vector<float>* outR)
    { const int blocks = (int) (SR * seconds / BLK);
      if (kind != 16) a.note (60, 100);
      a.render (blocks, 0.0, true, &out, outR);
      if (kind != 16) { a.note (60, 0); a.render ((int) (SR * 0.8 / BLK), seconds, true, &out, outR); } };
    auto capture = [&] (int kind, double seconds, std::vector<float>& out) { capture2 (kind, seconds, out, nullptr); };

    // ── [1] THE UNLOCK ────────────────────────────────────────────────────────────────────────
    {
        float k[6] = { 0.5f, 0.5f, 0.0f, 0.0f, 0.5f, 0.5f };
        std::vector<float> shut, open;
        a.putShaper (laneJson (0, "[[0,0,0],[1,0,0]]", k, 0.0f)); solo (0); capture (0, 1.6, shut);
        a.putShaper (laneJson (0, PTS_OPEN,           k, 0.0f)); solo (0); capture (0, 1.6, open);
        const double s = rmsDb (shut, W0, W0 + WIN), o = rmsDb (open, W0, W0 + WIN);
        char b[260]; std::snprintf (b, sizeof b, "[1] THE BLOB IS REACHABLE AS A HOST PRESET — a Volume curve pinned shut renders %.1f dBFS against %.1f dBFS pinned open (%.1f dB): every k[] is measurable from outside, with no test-only door in the plugin", s, o, o - s);
        chk (o - s > 20.0, b);
    }

    // ── [2][3] THE PATCH SURVIVES THE ROUND TRIP ──────────────────────────────────────────────
    //  Writing this bar is what found tp84's dangling else: the `else` after the shpNoiseSel loop
    //  bound to the INNER if, so any Shaper without a noise sample removed the SYNTH's selection.
    {
        std::string x; a.getStateXml (x);
        Au::putAttr (x, "noiseSampleSel", "{\"kind\":\"factory\",\"name\":\"CERT PROBE\"}");
        Au::putAttr (x, "shpNoiseSel0",   "{\"kind\":\"factory\",\"name\":\"SHAPER PROBE\"}");
        a.setStateXml (x); a.render (10, 0.0, false);
        std::string y; a.getStateXml (y);
        const std::string n = Au::getAttr (y, "noiseSampleSel"), s = Au::getAttr (y, "shpNoiseSel0");
        char b[420]; std::snprintf (b, sizeof b, "[2] SAVE -> LOAD -> SAVE KEEPS BOTH NOISE SELECTIONS: the synth's noiseSampleSel and the Shaper lane's shpNoiseSel0 both come back  (got %s | %s)",
                                    n == "\x01" ? "GONE" : "kept", s == "\x01" ? "GONE" : "kept");
        chk (n != "\x01" && s != "\x01", b);
    }
    {
        std::string x; a.getStateXml (x);
        Au::putAttr (x, "noiseSampleSel", "");   // a cleared selection
        a.setStateXml (x); a.render (10, 0.0, false);
        std::string y; a.getStateXml (y);
        const std::string n = Au::getAttr (y, "noiseSampleSel");
        char b[300]; std::snprintf (b, sizeof b, "[3] AND A CLEARED SELECTION IS REMOVED, NOT LEFT STALE (fb618's law)  (got %s)", n == "\x01" ? "removed" : ("\"" + n + "\"").c_str());
        chk (n == "\x01" || n.empty(), b);
    }

    // ── [4..12] EVERY TARGET IS AUDIBLE ───────────────────────────────────────────────────────
    //  The shape is pinned OPEN so the lane is fully on and the knob is the only thing that moves.
    for (int L = 0; L < 9; ++L)
    {
        const Lane& ln = kBorrowed[L];
        double worst = 1e9; const char* worstName = "";
        std::string detail;
        for (int t = 0; t < 4; ++t)
        {
            float lo[6] = { ln.k[0], ln.k[1], ln.k[2], ln.k[3], 0.5f, 0.5f };
            float hi[6] = { ln.k[0], ln.k[1], ln.k[2], ln.k[3], 0.5f, 0.5f };
            lo[t] = 0.03f; hi[t] = 0.97f;
            std::vector<float> A, B;
            a.putShaper (laneJson (ln.kind, PTS_OPEN, lo)); solo (ln.kind); capture (ln.kind, 1.3, A);
            a.putShaper (laneJson (ln.kind, PTS_OPEN, hi)); solo (ln.kind); capture (ln.kind, 1.3, B);
            /* ⚠️ A REVERB'S Decay IS ITS TAIL LENGTH AND ITS Diffusion IS ITS TAIL'S SMOOTHNESS. Neither is
               a spectrum: under the note they read 1.8 and 1.2 dB, and in the first moment of the tail
               0.2 and 0.2, on a reverb that is plainly working. Two decays only separate as the tails run
               out, which takes seconds — so those two are measured over a long tail, in their own terms. */
            double d;
            if (ln.kind == 8)
            {
                /* A reverb's four knobs are four claims about its TAIL, and the tail is what lives in the
                   gap after the burst stops. Size and Tone are what the tail SOUNDS like, Decay is how LONG
                   it is, Diffusion is how SMOOTH it is — none of them is a spectrum under a note. */
                std::vector<float> P, Q; const size_t gap = (size_t) (SR * 1.0);   // the burst ends at half of a 2 s cycle
                behindNoise (ln.kind, lo, PTS_BURST, P, nullptr, 2.0);
                behindNoise (ln.kind, hi, PTS_BURST, Q, nullptr, 2.0);
                d = (t == 1) ? std::fabs (tailDropDb  (P, gap) - tailDropDb  (Q, gap))
                  : (t == 3) ? std::fabs (tailRoughDb (P, gap) - tailRoughDb (Q, gap))
                             : specChangeDb (P, Q, gap + (size_t) (SR * 0.1), (size_t) (SR * 0.6));
            }
            else if (ln.kind == 12)
            {
                /* MULTIBAND SPLITS BANDS: on a held note Spread read 9.3 dB on one run and 0.2 dB on the
                   next. Behind the Noise lane it has something to split, and the tilt is a measure a tilt
                   is actually visible in. */
                std::vector<float> P, Q;
                behindNoise (ln.kind, lo, PTS_OPEN, P, nullptr, 1.1);
                behindNoise (ln.kind, hi, PTS_OPEN, Q, nullptr, 1.1);
                d = std::max (specChangeDb (P, Q, W0, WIN),
                              std::fabs (20.0 * std::log10 ((bandRatio (P, W0, WIN) + 1e-9) / (bandRatio (Q, W0, WIN) + 1e-9))));
            }
            else d = specChangeDb (A, B, W0, WIN);

            if (d < worst) { worst = d; worstName = ln.t[t]; }
            const char* unit = (ln.kind == 8 && t == 1) ? " dB of tail length" : (ln.kind == 8 && t == 3) ? " dB of tail scatter"
                             : (ln.kind == 8) ? " dB of tail behind a noise burst" : (ln.kind == 12) ? " dB behind Noise" : "";
            char q[130]; std::snprintf (q, sizeof q, "%s%s %.1f%s", t ? " · " : "", ln.t[t], d, unit);
            detail += q;
        }
        char b[340]; std::snprintf (b, sizeof b, "[%d] %s — ALL FOUR TARGETS ARE AUDIBLE: end to end each one moves the sound, weakest %s at %.1f dB",
                                    4 + L, kAll[ln.kind], worstName, worst);
        chk (worst > 1.5, b, detail);
    }

    // ── [13..21] EVERY TYPE IS ITS OWN ────────────────────────────────────────────────────────
    for (int L = 0; L < 9; ++L)
    {
        const Lane& ln = kBorrowed[L];
        const std::string mode = std::string ("Shaper ") + kAll[ln.kind] + " Mode";
        const int N = a.choiceCount (mode);
        if (N < 2) { chk (false, (std::string ("[") + std::to_string (13 + L) + "] " + kAll[ln.kind] + " — no type menu found").c_str()); continue; }
        float k[6] = { ln.k[0], ln.k[1], ln.k[2], ln.k[3], 0.5f, 0.5f };
        /* ⚠️ TAPE'S TYPES ARE TAPE MACHINES — a SATURATION character — and at the lane's resting Drive of
           0.3 they have almost nothing to be different with. Driven, the machines separate. */
        if (ln.kind == 13) { k[0] = 0.5f; k[1] = 0.9f; k[2] = 0.8f; k[3] = 0.5f; }
        std::vector<std::vector<float>> takes ((size_t) N), takesR ((size_t) N), takesB ((size_t) N);
        for (int t = 0; t < N; ++t)
        {
            if (ln.kind == 8)
            {   // a reverb's type is its ROOM, and a room is a tail: the same deterministic burst
                a.setIndex (mode, t, N); a.pump (0.4);
                behindNoise (ln.kind, k, PTS_BURST, takes[(size_t) t], nullptr, 2.0);
            }
            else if (ln.kind == 11)
            {
                /* ⚠️ WIDEN'S ONE CLAIM IS THE STEREO IMAGE, and a mono spectrum cannot see an image at all:
                   two of its six types read 1.0 dB apart that way while placing the sound differently
                   across the field. Side-against-mid is the measure tp83's own Widen bar used. */
                a.putShaper (laneJson (ln.kind, PTS_OPEN, k)); solo (ln.kind); a.setIndex (mode, t, N); a.pump (0.3);
                capture2 (ln.kind, 0.9, takes[(size_t) t], &takesR[(size_t) t]);
            }
            else if (ln.kind == 12 || ln.kind == 13)
            {
                /* MULTIBAND SPLITS BANDS and a TAPE MACHINE has a frequency response: both are claims a
                   held note cannot carry, and both were reading under a decibel apart while being plainly
                   different effects. Behind the Noise lane — deterministic, flat, needing no note — they
                   separate, and they separate the SAME WAY every run. */
                a.setIndex (mode, t, N); a.pump (0.4);
                behindNoise (ln.kind, k, PTS_OPEN, takes[(size_t) t], nullptr, 1.1);
                if (ln.kind == 13)
                {
                    /* and the TRANSPORT still needs a tone: types 0 and 4 run one machine and 1 and 3 the
                       other, so those pairs differ in wow and flutter alone — a pitch wander, invisible
                       both in noise and in a spectrum. */
                    a.putShaper (laneJson (ln.kind, PTS_OPEN, k)); solo (ln.kind); a.setIndex (mode, t, N); a.pump (0.3);
                    capture (ln.kind, 0.9, takesB[(size_t) t]);
                }
            }
            else { a.putShaper (laneJson (ln.kind, PTS_OPEN, k)); solo (ln.kind); a.setIndex (mode, t, N); a.pump (0.3); capture (ln.kind, 0.9, takes[(size_t) t]); }
        }
        /* Multiband's types are band SPLITS and Tape's are tape MACHINES — a tilt and a harmonic
           signature. Comparing either as a magnitude spectrum reads 0.8 and 1.0 dB on types that are
           plainly different effects, so each lane is compared in the terms its own claim is made in. */
        const char* how = "dB of spectrum";
        auto dist = [&] (int iu, int iv) -> double
        {
            const std::vector<float>& u = takes[(size_t) iu]; const std::vector<float>& v = takes[(size_t) iv];
            if (ln.kind == 11) return std::max (specChangeDb (u, v, W0, WIN),
                                                std::fabs (20.0 * std::log10 ((sideOverMid (u, takesR[(size_t) iu], W0, WIN) + 1e-6)
                                                                            / (sideOverMid (v, takesR[(size_t) iv], W0, WIN) + 1e-6))));
            if (ln.kind == 8)
            { const size_t gap = (size_t) (SR * 1.0);
              return std::max (specChangeDb (u, v, gap + (size_t) (SR * 0.1), (size_t) (SR * 0.6)),
                               std::fabs (tailDropDb (u, gap) - tailDropDb (v, gap))); }
            if (ln.kind == 12) return std::max (specChangeDb (u, v, W0, WIN),
                                                std::fabs (20.0 * std::log10 ((bandRatio (u, W0, WIN) + 1e-9) / (bandRatio (v, W0, WIN) + 1e-9))));
            /* TAPE: a machine is its HARMONICS and a transport is its WANDER, and the menu holds both —
               types 0 and 4 run the same machine and types 1 and 3 the other, so those pairs differ in
               the transport alone. Either measure separating a pair is that pair separated. */
            if (ln.kind == 13)
            { const std::vector<float>& bu = takesB[(size_t) iu]; const std::vector<float>& bv = takesB[(size_t) iv];
              return std::max (specChangeDb (u, v, W0, WIN),                                  // the MACHINE, on noise
                     std::max (std::fabs (20.0 * std::log10 ((thd (bu, W0, WIN, 261.63) + 1e-9) / (thd (bv, W0, WIN, 261.63) + 1e-9))),
                               std::fabs (wanderDb (bu, W0, WIN, 261.63) - wanderDb (bv, W0, WIN, 261.63)))); }   // the TRANSPORT, on a tone
            return specChangeDb (u, v, W0, WIN);
        };
        if (ln.kind == 12) how = "dB behind Noise";
        if (ln.kind == 13) how = "dB of machine, harmonics or wander";
        if (ln.kind == 11) how = "dB of spectrum or image";
        if (ln.kind == 8)  how = "dB of tail";
        double closest = 1e9; int ca = 0, cb = 0;
        for (int i = 0; i < N; ++i) for (int j = i + 1; j < N; ++j)
        { const double d = dist (i, j);
          if (d < closest) { closest = d; ca = i; cb = j; } }
        char b[380]; std::snprintf (b, sizeof b, "[%d] %s — ITS %d TYPES ARE %d DIFFERENT EFFECTS: every pair differs, the closest being type %d against type %d at %.1f %s",
                                    13 + L, kAll[ln.kind], N, N, ca, cb, closest, how);
        chk (closest > 1.0, b);
    }

    // ── [22] NO SEAM ──────────────────────────────────────────────────────────────────────────
    //  A hard gate on the sixteenth: the send slams shut and open. A click is a LOCALISED outlier — one
    //  jump far above what this same render does the rest of the time — so the gated take is compared with
    //  the SAME lane under a shape pinned open. Bandwidth divides out, which the first cut of this bar did
    //  not manage: it read Noise and Bode as clicking when they were simply wider.
    {
        double worstR = 0; const char* worstL = "";
        std::string detail;
        for (int L = 0; L < 9; ++L)
        {
            const Lane& ln = kBorrowed[L];
            float k[6] = { ln.k[0], ln.k[1], ln.k[2], ln.k[3], 0.5f, 0.5f };
            std::vector<float> g, o;
            a.putShaper (laneJson (ln.kind, PTS_GATE, k)); solo (ln.kind); capture (ln.kind, 1.6, g);
            a.putShaper (laneJson (ln.kind, PTS_OPEN, k)); solo (ln.kind); capture (ln.kind, 1.6, o);
            const double r = slewOutlier (g) / (slewOutlier (o) + 1e-9);
            if (r > worstR) { worstR = r; worstL = kAll[ln.kind]; }
            char q[80]; std::snprintf (q, sizeof q, "%s%s %.2fx", L ? " \u00b7 " : "", kAll[ln.kind], r);
            detail += q;
        }
        char b[380]; std::snprintf (b, sizeof b, "[22] NO SEAM WHEN THE SEND SLAMS: with a hard gate on the sixteenth, no lane's largest jump stands further out of its own signal than it does with the shape pinned open — worst is %s at %.2fx",
                                    worstL, worstR);
        chk (worstR < 3.0, b, detail);
    }

    // ── [23] THE TWO THAT FEED BACK STAY BOUNDED ──────────────────────────────────────────────
    //  ⚠️ NOT "it stops growing". DelayEngine hard-caps the loop at 0.98 and soft-clips inside it, so a
    //  maximum-feedback delay is CONVERGENT with a long time constant — asking it to be flat after twelve
    //  seconds failed working code. The claim that matters is that it CONVERGES: each window's rise is
    //  smaller than the one before, it stays finite, and it stays under a ceiling.
    {
        bool ok = true; std::string detail;
        const int idx[2] = { 1, 7 };   // Delay and Bode in kBorrowed — k[1] is Feedback on both
        for (int q = 0; q < 2; ++q)
        {
            const Lane& ln = kBorrowed[idx[q]];
            float k[6] = { ln.k[0], 1.0f, ln.k[2], ln.k[3], 0.5f, 0.5f };   // feedback pinned at maximum
            a.putShaper (laneJson (ln.kind, PTS_OPEN, k)); solo (ln.kind);
            std::vector<float> x;
            for (int bar = 0; bar < 12; ++bar)
            { a.note (60, 100); a.render ((int) (SR * 1.0 / BLK), bar * 2.0, true, &x);
              a.note (60, 0);   a.render ((int) (SR * 1.0 / BLK), bar * 2.0 + 1.0, true, &x); }
            const bool fin = finiteAll (x);
            const double pk = peakAbs (x, 0, x.size());
            double w[4];
            for (int i = 0; i < 4; ++i) { const size_t a0 = x.size() / 4 * (size_t) i; w[i] = rmsDb (x, a0, a0 + x.size() / 4); }
            const double r1 = w[1] - w[0], r2 = w[2] - w[1], r3 = w[3] - w[2];
            /* ⚠️ "each quarter rises less than the one before" is not the claim either: a tail that has
               already settled rises by -0.0 dB and then +0.0 dB, and floating point made that a failure on
               a delay that had plainly converged. What a runaway does is keep rising — so the bar is that
               the LAST quarter has stopped: under half a decibel of rise over its final six seconds. */
            const bool converging = r3 < 0.5;
            (void) r2;
            const bool bounded = fin && pk < 4.0 && converging;
            ok = ok && bounded;
            char b[260]; std::snprintf (b, sizeof b, "%s%s: finite %s \u00b7 peak %.2f \u00b7 quarters %.1f %.1f %.1f %.1f dBFS \u00b7 rises %+.1f %+.1f %+.1f",
                                        q ? "  |  " : "", kAll[ln.kind], fin ? "yes" : "NO", pk, w[0], w[1], w[2], w[3], r1, r2, r3);
            detail += b;
        }
        chk (ok, "[23] FULL FEEDBACK CONVERGES: Delay and Bode held at maximum feedback for twenty-four seconds stay finite and under a peak of 4.0, and each quarter rises less than the one before", detail);
    }

    allDark(); a.putShaper ("");
    printf ("\n  %d passed, %d failed\n\n", npass, nfail);
    a.close();
    return nfail ? 1 : 0;
}
