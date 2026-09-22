// ══════════════════════════════════════════════════════════════════════════════════════════════
//  au_shaper_level.cpp — tp89 · tp90 · THE LOUDNESS LAW, IN THE INSTALLED AU.
//
//  Max: "nothing should be making anything quieter or louder … unless they're shaped, or the volume
//  knob or the drive knob." Each lane is lit alone, shape pinned OPEN, knobs at rest, every TYPE in
//  turn, and compared with the same render dark — on TWO sources, because one lies:
//    · a held chord from the synth (nearly pure sines), and
//    · the Noise lane's PINK hiss chained AHEAD of the lane under test (no notes).
//  A sustained sine plus its own echo partly cancels, so a Delay reads -3 dB on the chord and +2.6 on
//  noise. The verdict is the MEAN of the two, within 1 dB of the lane's target (0 dB; Multiband's push
//  +5.5), and a peak under 2x the same source dry. Exit code 1 if any type fails.
//
//  Every take writes all EIGHT chain slots: a repeated kind makes sanitise() fall back to kinds 0..7
//  and the lane (or the Noise source) silently leaves the chain.
//  The CAL lines it prints regenerate FlowShaper.h's kShaperTrimDb (see the note there).
//
//  NOT JUDGED, and why: Volume IS the volume · Time and Repeat replay their input · Filter's job is to
//  take energy away · Pan's Linear law is -3 dB at the edge by definition · Drive is the drive knob.
//
//  clang++ -O2 -std=c++17 Tests/au_shaper_level.cpp -o /tmp/aulevel -framework AudioToolbox \
//          -framework AudioUnit -framework CoreFoundation -framework CoreAudio && /tmp/aulevel
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
static const char* const kAll[17] = { "Volume","Time","Filter","Pan","Repeat","Drive","Phaser","Crush",
                                      "Reverb","Delay","Chorus","Widen","Multiband","Tape","Granular","Bode","Noise" };
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
    for (int i = 0; i < 17; ++i)
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
    for (int i = 0; i < 17; ++i)
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


// ── tp89 — THE LOUDNESS LAW, AND WHAT BREAKS IT ───────────────────────────────────────────────
//  Max: "nothing should be making anything quieter or louder by the way in terms of these effects.
//  Nothing should be making them quieter or louder unless they're shaped, or the volume knob or the
//  drive knob." So: light each lane alone, shape pinned OPEN, everything at rest, and compare with the
//  same render dark. A lane that moves the level is a lane that breaks the law; a lane that lifts the
//  PEAK far more than the RMS is a lane that is clipping.
static std::string oneLane (int lane)
{
    std::string j = "{\"tv\":2,\"lanes\":[";
    for (int i = 0; i < 17; ++i)
    { if (i) j += ",";
      if (i == lane) j += "{\"pts\":[[0,1,0],[1,1,0]],\"smooth\":0.2,\"phase\":0,\"tension\":0.5,\"floor\":0,\"blend\":1,\"swing\":0,\"grid\":16}";
      else j += "{}"; }
    return j + "]}";
}
int main()
{
    Au a; if (! a.open()) { printf ("  !! no Terrain AU\n"); return 2; }
    printf ("\ntp89/tp90 — DOES ANY LANE CHANGE THE LEVEL? (each lane alone, shape pinned open, knobs at rest)\n");
    printf ("  first: the chord alone, each lane on its boot type — REFERENCE; the verdict is the two-source table\n\n");
    a.pump (1.2); a.render (30, 0.0, false);
    a.set ("Flow Chain 1", 2.0f); a.pump (0.8); a.render (20, 0.0, false);

    auto allDark = [&] { for (int q = 0; q < 17; ++q) a.set (std::string ("Shaper ") + kAll[q] + " On", 0.0f); };
    /* 🚨 tp90 — THE CHAIN IS EIGHT DISTINCT KINDS OR IT IS NOT THE CHAIN. ShaperState::sanitise() falls back to
       kinds 0..7 when any two positions repeat, so setting only slot 1 (or 1 and 2) leaves a duplicate further
       down and silently swaps the whole chain: the first cut of this audit read every lane past Crush at
       exactly +0.00 on the chord (the lane had fallen out) and the Noise source at -229 dB behind Phaser and
       Crush (Noise had). Every take now writes all eight. */
    auto setChain = [&] (int first, int second)
    {
        std::vector<int> ch; ch.push_back (first); if (second >= 0 && second != first) ch.push_back (second);
        for (int k = 0; k < 17 && ch.size() < 8; ++k) if (std::find (ch.begin(), ch.end(), k) == ch.end()) ch.push_back (k);
        for (int q = 0; q < 8; ++q) a.setIndex ("Shaper Slot " + std::to_string (q + 1), ch[(size_t) q], 17);
    };
    //  a held chord, measured after it settles. Time and Repeat read the PAST, so a steady source is the
    //  only fair one — a shifted read of a steady chord is still the same chord at the same level.
    auto take = [&] (int lane, int mode, std::vector<float>& out)
    {
        a.putShaper (oneLane (lane < 0 ? 0 : lane));
        allDark (); setChain (lane < 0 ? 0 : lane, -1);
        if (lane >= 0)
        { a.set (std::string ("Shaper ") + kAll[lane] + " On", 1.0f);
          a.set (std::string ("Shaper ") + kAll[lane] + " Depth", 1.0f);
          if (mode >= 0) { const std::string m = std::string ("Shaper ") + kAll[lane] + " Mode";
                           if (a.has (m)) a.setIndex (m, mode, a.choiceCount (m)); } }
        a.pump (0.9); a.render ((int) (SR * 0.6 / BLK), 0.0, false);
        a.note (48, 100); a.note (55, 100); a.note (60, 100);
        std::vector<float> rr2; a.render ((int) (SR * 5.0 / BLK), 0.0, true, &out, &rr2);
        /* tp90 — STEREO POWER, not the mid. The mid read a constant-power pan as -3.01 dB and a wide delay
           as -3.05 dB: both were energy moved into the SIDE, which the ear hears as exactly as loud. Each
           sample becomes sqrt((L^2 + R^2) / 2), so a mono source reads its own level and a hard pan reads 0. */
        for (size_t q = 0; q < out.size() && q < rr2.size(); ++q)
        { const float L = out[q], R = rr2[q]; out[q] = std::sqrt (0.5f * (L * L + R * R)) * ((L + R) < 0.0f ? -1.0f : 1.0f); }
        a.note (48, 0); a.note (55, 0); a.note (60, 0); a.render (40, 5.0, true);
    };
    /* tp90 — THE SECOND SOURCE. A held chord that is nearly pure sines is a LUCKY source for anything with
       a delay in it: the offline check put the same Delay at -3.07 dB on a sine chord, -0.13 on a saw chord
       and +2.88 on noise — a sustained sine plus its own echo partly cancels, by phase coincidence. So every
       trim is calibrated on TWO sources, the chord and the Noise lane's broadband hiss chained AHEAD of the
       lane under test, and set to their mean. No notes are played: the hiss is the whole input. */
    auto takeNoise = [&] (int lane, int mode, std::vector<float>& out)
    {
        std::string j = "{\"tv\":2,\"lanes\":[";
        for (int i = 0; i < 17; ++i)
        { if (i) j += ",";
          if (i == 16 || i == lane) j += "{\"pts\":[[0,1,0],[1,1,0]],\"smooth\":0.2,\"phase\":0,\"tension\":0.5,\"floor\":0,\"blend\":1,\"swing\":0,\"grid\":16}";
          else j += "{}"; }
        a.putShaper (j + "]}");
        allDark (); setChain (16, lane < 0 ? 0 : lane);
        //  PINK, not white: white puts most of its energy above 5 kHz, so every lane with a low-pass in it (all of
        //  Tape, most of Chorus) read 10-24 dB down on it. Pink falls at -3 dB/octave, the long-run average of music.
        a.set ("Shaper Noise On", 1.0f); a.set ("Shaper Noise Depth", 1.0f); a.setIndex ("Shaper Noise Mode", 1, 13);
        if (lane >= 0)
        { a.set (std::string ("Shaper ") + kAll[lane] + " On", 1.0f);
          a.set (std::string ("Shaper ") + kAll[lane] + " Depth", 1.0f);
          const std::string m = std::string ("Shaper ") + kAll[lane] + " Mode";
          if (mode >= 0 && a.has (m)) a.setIndex (m, mode, a.choiceCount (m)); }
        a.pump (0.9); a.render ((int) (SR * 0.6 / BLK), 0.0, false);
        std::vector<float> rr2; a.render ((int) (SR * 5.0 / BLK), 0.0, true, &out, &rr2);
        for (size_t q = 0; q < out.size() && q < rr2.size(); ++q)
        { const float L = out[q], R = rr2[q]; out[q] = std::sqrt (0.5f * (L * L + R * R)) * ((L + R) < 0.0f ? -1.0f : 1.0f); }
        a.set ("Shaper Noise On", 0.0f); a.render (40, 5.0, true);
    };
    auto peakOf = [] (const std::vector<float>& x, size_t A, size_t B2)
    { double m = 0; for (size_t i = A; i < B2 && i < x.size(); ++i) m = std::max (m, (double) std::fabs (x[i])); return m; };

    std::vector<float> dry; take (-1, -1, dry);
    const size_t A = (size_t) (SR * 2.0), B2 = (size_t) (SR * 4.5);
    const double dRms = rmsDb (dry, A, B2), dPk = peakOf (dry, A, B2);
    printf ("  dry reference: %.2f dBFS rms, peak %.3f\n\n", dRms, dPk);
    printf ("  %-11s %10s %10s %s\n", "lane", "rms", "peak", "");
    int broken = 0;
    for (int ln = 0; ln < 17; ++ln)
    {
        std::vector<float> w; take (ln, -1, w);
        const double r = rmsDb (w, A, B2) - dRms, pk = peakOf (w, A, B2) / (dPk + 1e-12);
        /* tp90 — this first table is the CHORD ALONE, each lane on its boot type, and it is REFERENCE ONLY. One
           sustained sine chord is a lucky source for anything with a delay in it (Delay reads -2.85 dB here and
           +2.6 on noise), so the verdict is the two-source table below. */
        const char* flag = ln == 16 ? "(Noise ADDS signal — exempt)" : "";
        (void) broken;
        printf ("  %-11s %+9.2f dB %9.2fx  %s\n", kAll[ln], r, pk, flag);
    }
    // ── tp90 — EVERY TYPE OF EVERY TRIMMED LANE, ON BOTH SOURCES. A lane's level is set per type (each type
    //    is its own engine voicing), so a lane that passes on type 0 has proved nothing about type 6.
    //    NOT TRIMMED, and why: Volume IS the volume · Time and Repeat replay the input · Filter's job is to
    //    remove energy (a high-pass at rest SHOULD take out a C3 chord) · Pan's Linear law is -3 dB at the
    //    edge by definition · Drive is the drive knob, which Max exempted by name. ──
    std::vector<float> nDry; takeNoise (-1, -1, nDry);
    const double nRms = rmsDb (nDry, A, B2), nPk = peakOf (nDry, A, B2);
    printf ("\n  noise reference: %.2f dBFS rms\n", nRms);
    printf ("  EVERY TYPE, BOTH SOURCES (pass: the MEAN within 1 dB of its target, and the peak under 2x its own source dry)\n");
    printf ("  %-10s %4s %9s %9s %9s %8s\n", "lane", "type", "chord", "noise", "mean", "pk x");
    int badTypes = 0;
    const int kTrimmed[10] = { 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 };
    for (int ln : kTrimmed)
    {
        const std::string m = std::string ("Shaper ") + kAll[ln] + " Mode";
        const int N = a.has (m) ? a.choiceCount (m) : 1;
        for (int t = 0; t < N; ++t)
        {
            std::vector<float> w, wn; take (ln, t, w); takeNoise (ln, t, wn);
            const double rc = rmsDb (w, A, B2) - dRms, rn = rmsDb (wn, A, B2) - nRms, mean = 0.5 * (rc + rn);
            //  a peak is judged against the SAME source dry: pink noise at this level already peaks near full
            //  scale by itself, so an absolute ceiling flagged every noise take. Doubling the crest is the fault.
            const double pk = std::max (peakOf (w, A, B2) / (dPk + 1e-12), peakOf (wn, A, B2) / (nPk + 1e-12));
            //  Multiband's push is the product (tp89) — its target is +5.5 dB, the same for every type
            const double target = (ln == 12) ? 5.5 : 0.0;
            //  ...and a lane whose TARGET is a push is judged on its crest, not on the push itself: +5.5 dB of rms
            //  alone lifts a peak 1.88x, so Multiband's peak is measured against its own target gain.
            const double pkN = pk / std::pow (10.0, target / 20.0);
            const bool bad = std::fabs (mean - target) > 1.0 || pkN >= 2.0;
            if (bad) ++badTypes;
            printf ("  %-10s %4d %+8.2f %+8.2f %+8.2f %7.3f  %s\n", kAll[ln], t, rc, rn, mean, pkN,
                    bad ? (pkN >= 2.0 ? "<<< PEAK DOUBLED" : (mean < target ? "<<< QUIET" : "<<< LOUD")) : "");
            printf ("CAL %d %d %.3f %.3f\n", ln, t, rc, rn);
        }
    }
    printf ("\n  %d lane types break the level law.\n", badTypes);
    //  (the Phaser-only table that followed here through tp89 is the Phaser rows above, now on both sources)
    a.close(); return badTypes ? 1 : 0;
}
