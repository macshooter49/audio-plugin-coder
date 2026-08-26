// ══════════════════════════════════════════════════════════════════════════════════════════════
//  fb517 — DOES THE DSP OBEY THE LFO TRIGGER MODE, OR ONLY THE VIZ?
//
//    clang++ -std=c++17 -O2 -I Tests/shim -I Source Tests/lfo_trigger_cert.cpp -o /tmp/lfo_trigger_cert \
//            -framework AudioToolbox -framework AudioUnit -framework CoreFoundation -framework CoreAudio
//
//  Max (fb517): with LFO 1 on Envelope or Retrigger and routed to a knob, the modulation keeps
//  free-running — "it just seems like it's always stuck on free mode." The viz dot already
//  stops/pins (fb231 rides the most-active VOICE's phase); the audible DSP did not, because the
//  global block-rate bank (flowLfo_) was hard-forced Free at PluginProcessor.cpp:9121 and it is
//  what every GLOBAL consumer reads: synth-page dests >= Res1, LfoAmt, FLOW/card knobs, and the
//  fb453 FX-rack matrix.
//
//  METHOD (au_modviz's machinery, fb461): the REAL installed AU, a real route installed by
//  rewriting the state blob (the path a saved project takes), real MIDI, and a level/timbre
//  descriptor per 0.128 s window. Every gate is bracketed by a Free control on the SAME patch:
//  the control must show sustained motion, or the stasis/correlation assertion is vacuous.
//
//  GATES (each: Env/Trig case + Free control):
//    G1 ENV-STOPS   — LFO1(1 Hz sine, trigger=Env) -> OSC A Level (global dest, ModDest::LevelA).
//                     Held chord: tremolo for exactly one cycle, then STASIS (env pins).
//                     Free control: tremolo the whole render.
//    G2 TRIG-RESETS — trigger=Trig, 0.5 Hz, two chord onsets 3.072 s apart (= 1.536 cycles, so a
//                     free-running phase arrives ~half a cycle off). The post-onset window series
//                     must correlate under Trig and must NOT under Free.
//    G3 FREE-UNCHANGED — Free keeps free-running (motion over the whole render) + a printed
//                     checksum so the pre-fix and post-fix builds can be compared exactly.
//    G4 FX-PATH     — LFO1(trigger=Env) -> the RACK Filter's Cut (fxModDest(5,0,0), the fb453
//                     matrix): timbre moves for one cycle, then STASIS. Free control moves on.
//    V  PER-VOICE   — informational: LFO1(trigger=Env) -> WT Frame (a per-voice dest, applied
//                     from the voice's own synthLfo_ bank which already honours noteOn/Env).
//                     Records whether the per-voice path was ever broken. Not a pass/fail gate.
// ══════════════════════════════════════════════════════════════════════════════════════════════
#include <AudioToolbox/AudioToolbox.h>
#include <CoreFoundation/CoreFoundation.h>
#include "SynthModConfig.h"
#include <cstdio>
#include <cstring>
#include <cmath>
#include <string>
#include <vector>
#include <map>
#include <algorithm>

static const double SR = 48000.0;
static const int    BLK = 512;
static const int    WBLK = 12;              // 12 x 512 / 48k = 0.128 s per metric window

struct Ev { int blk; bool on; };            // chord on/off at a block boundary

struct AU
{
    AudioUnit au = nullptr;
    std::map<std::string, AudioUnitParameterID> byName;
    std::map<AudioUnitParameterID, AudioUnitParameterInfo> info;

    bool open()
    {
        AudioComponentDescription d {}; d.componentType = kAudioUnitType_MusicDevice;
        d.componentSubType = 'Tern'; d.componentManufacturer = 'Wvcr';
        AudioComponent c = AudioComponentFindNext (nullptr, &d);
        if (! c || AudioComponentInstanceNew (c, &au) != noErr) { printf ("  !! AU not found\n"); return false; }
        AudioStreamBasicDescription f {}; f.mSampleRate = SR; f.mFormatID = kAudioFormatLinearPCM;
        f.mFormatFlags = kAudioFormatFlagsNativeFloatPacked | kAudioFormatFlagIsNonInterleaved;
        f.mBytesPerPacket = 4; f.mFramesPerPacket = 1; f.mBytesPerFrame = 4; f.mChannelsPerFrame = 2; f.mBitsPerChannel = 32;
        AudioUnitSetProperty (au, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Output, 0, &f, sizeof f);
        UInt32 mx = BLK; AudioUnitSetProperty (au, kAudioUnitProperty_MaximumFramesPerSlice, kAudioUnitScope_Global, 0, &mx, sizeof mx);
        if (AudioUnitInitialize (au) != noErr) { printf ("  !! init failed\n"); return false; }
        UInt32 sz = 0; Boolean w = false;
        AudioUnitGetPropertyInfo (au, kAudioUnitProperty_ParameterList, kAudioUnitScope_Global, 0, &sz, &w);
        std::vector<AudioUnitParameterID> ids (sz / sizeof (AudioUnitParameterID));
        AudioUnitGetProperty (au, kAudioUnitProperty_ParameterList, kAudioUnitScope_Global, 0, ids.data(), &sz);
        for (auto id : ids) { AudioUnitParameterInfo pi {}; UInt32 s = sizeof pi;
            if (AudioUnitGetProperty (au, kAudioUnitProperty_ParameterInfo, kAudioUnitScope_Global, id, &pi, &s) != noErr) continue;
            char b[256] = {0};
            if ((pi.flags & kAudioUnitParameterFlag_HasCFNameString) && pi.cfNameString) CFStringGetCString (pi.cfNameString, b, sizeof b, kCFStringEncodingUTF8);
            else snprintf (b, sizeof b, "%s", pi.name);
            byName[b] = id; info[id] = pi; }
        return true;
    }
    void close() { if (au) { AudioUnitUninitialize (au); AudioComponentInstanceDispose (au); au = nullptr; } }

    // by DISPLAY NAME, normalised onto the parameter's reported AU range (au_modviz's setNorm)
    void setNorm (const std::string& n, float v)
    { auto it = byName.find (n); if (it == byName.end()) { printf ("  !! no param '%s'\n", n.c_str()); return; }
      const auto& pi = info.at (it->second);
      AudioUnitSetParameter (au, it->second, kAudioUnitScope_Global, 0, pi.minValue + v * (pi.maxValue - pi.minValue), 0); }
    static float toNorm (float v, float lo, float hi) { const float p = (v - lo) / (hi - lo); return p <= 0.f ? 0.f : std::pow (p, 0.3f); }

    // by PARAMETER ID (JUCE's hashCode, sign bit cleared — au_fx_path's addressing for rack ids)
    static AudioUnitParameterID pid (const std::string& juceParamId)
    { uint32_t r = 0; for (unsigned char ch : juceParamId) r = 31u * r + (uint32_t) ch;
      return (AudioUnitParameterID) (r & 0x7FFFFFFFu); }
    bool setP (const std::string& id, float v)
    { return AudioUnitSetParameter (au, pid (id), kAudioUnitScope_Global, 0, v, 0) == noErr; }

    void pumpHost (double secs) { double t = 0; while (t < secs) { CFRunLoopRunInMode (kCFRunLoopDefaultMode, 0.02, false); t += 0.02; } }

    // Rewrite attributes of the AU's own state XML (the saved-project path — au_modviz's trick,
    // widened to N attributes so synModJson and lfoShapesJson land in ONE ClassInfo write).
    bool setStateAttrs (const std::vector<std::pair<std::string, std::string>>& attrs)
    {
        CFPropertyListRef pl = nullptr; UInt32 psz = sizeof (pl);
        if (AudioUnitGetProperty (au, kAudioUnitProperty_ClassInfo, kAudioUnitScope_Global, 0, &pl, &psz) != noErr || pl == nullptr) return false;
        CFDictionaryRef dict = (CFDictionaryRef) pl;
        CFStringRef key = CFSTR ("jucePluginState");
        CFDataRef dd = (CFDataRef) CFDictionaryGetValue (dict, key);
        if (dd == nullptr) { CFRelease (pl); return false; }
        const UInt8* p = CFDataGetBytePtr (dd);
        uint32_t magic = 0, len = 0; memcpy (&magic, p, 4); memcpy (&len, p + 4, 4);
        if (magic != 0x21324356u || (CFIndex) (len + 8) > CFDataGetLength (dd)) { CFRelease (pl); return false; }
        std::string xml ((const char*) p + 8, len);
        for (const auto& kv : attrs)
        {
            std::string esc; for (char ch : kv.second) esc += (ch == '"' ? "&quot;" : ch == '&' ? "&amp;" : ch == '<' ? "&lt;" : ch == '>' ? "&gt;" : std::string (1, ch));
            const std::string tag = " " + kv.first + "=\"";
            const std::string attr = tag + esc + "\"";
            const size_t at = xml.find (tag);
            if (at != std::string::npos) { const size_t e = xml.find ('"', at + tag.size()); xml = xml.substr (0, at) + attr + xml.substr (e + 1); }
            else { const size_t r = xml.find ("<Parameters"); if (r == std::string::npos) { CFRelease (pl); return false; }
                   xml = xml.substr (0, r + 11) + attr + xml.substr (r + 11); }
        }
        std::vector<UInt8> blob (8 + xml.size() + 1, 0);
        const uint32_t m = 0x21324356u, l = (uint32_t) xml.size() + 1;
        memcpy (blob.data(), &m, 4); memcpy (blob.data() + 4, &l, 4); memcpy (blob.data() + 8, xml.data(), xml.size());
        CFMutableDictionaryRef nd = CFDictionaryCreateMutableCopy (nullptr, 0, dict);
        CFDataRef ndata = CFDataCreate (kCFAllocatorDefault, blob.data(), (CFIndex) blob.size());
        CFDictionarySetValue (nd, key, ndata);
        CFPropertyListRef npl = (CFPropertyListRef) nd;
        const OSStatus st = AudioUnitSetProperty (au, kAudioUnitProperty_ClassInfo, kAudioUnitScope_Global, 0, &npl, sizeof (npl));
        CFRelease (ndata); CFRelease (nd); CFRelease (pl);
        return st == noErr;
    }

    // Render nblk blocks, firing the chord per the event list; per-WBLK-window level + timbre.
    void renderTimeline (int nblk, const std::vector<Ev>& evs,
                         std::vector<double>& rmsDb, std::vector<double>& hiRatio,
                         double* sumAbs = nullptr, double* sumSq = nullptr)
    {
        static const int NOTES[3] = { 48, 55, 60 };
        std::vector<float> bl ((size_t) BLK), br ((size_t) BLK);
        AudioBufferList* abl = (AudioBufferList*) calloc (1, sizeof (AudioBufferList) + sizeof (AudioBuffer));
        abl->mNumberBuffers = 2;
        AudioTimeStamp ts {}; ts.mFlags = kAudioTimeStampSampleTimeValid;
        double hi = 0.0, lo = 0.0; int wblk = 0; float prev = 0.0f;
        double sa = 0.0, sq = 0.0;
        size_t evi = 0;
        std::vector<Ev> ev = evs;
        std::sort (ev.begin(), ev.end(), [] (const Ev& a, const Ev& b) { return a.blk < b.blk; });
        for (int b = 0; b < nblk; ++b)
        {
            while (evi < ev.size() && ev[evi].blk == b)
            {
                for (int n = 0; n < 3; ++n)
                    MusicDeviceMIDIEvent (au, ev[evi].on ? 0x90 : 0x80, NOTES[n], ev[evi].on ? 100 : 0, 0);
                ++evi;
            }
            abl->mBuffers[0] = { 1, (UInt32) (BLK * 4), bl.data() };
            abl->mBuffers[1] = { 1, (UInt32) (BLK * 4), br.data() };
            AudioUnitRenderActionFlags fl = 0;
            AudioUnitRender (au, &fl, &ts, 0, BLK, abl);
            ts.mSampleTime += BLK;
            if (b % 6 == 0) CFRunLoopRunInMode (kCFRunLoopDefaultMode, 0.0, false);   // message-thread engines/timers
            for (int i = 0; i < BLK; ++i)
            {
                const float x = bl[(size_t) i], d = x - prev; prev = x;
                hi += (double) d * d; lo += (double) x * x;
                sa += std::fabs ((double) x); sq += (double) x * x;
            }
            if (++wblk == WBLK)
            {
                rmsDb.push_back (20.0 * std::log10 (std::sqrt (lo / (double) (WBLK * BLK)) + 1e-12));
                hiRatio.push_back (lo > 1e-20 ? std::sqrt (hi / lo) : 0.0);
                hi = lo = 0.0; wblk = 0;
            }
        }
        free (abl);
        if (sumAbs) *sumAbs = sa;
        if (sumSq)  *sumSq  = sq;
    }
};

static std::string routeJson (int src, int dest, float depth)
{ char b[128]; snprintf (b, sizeof b, "[{\"s\":%d,\"d\":%d,\"v\":%.6f}]", src, dest, depth); return b; }

// The LFO-1 trigger travels in the fb228 motion blob beside the drawn-shape points (the ONLY
// path a trigger takes to the DSP — there is no APVTS trigger parameter). pts >= 2 is required
// by the parser or the whole entry (motion included) is skipped; the shape param stays SINE so
// the baked table is never read.
static std::string motionJson (int tg)
{ char b[192]; snprintf (b, sizeof b,
    "{\"shapes\":[{\"n\":1,\"pts\":[[0.0,0.5],[0.5,1.0],[1.0,0.5]],\"mo\":{\"tg\":%d,\"ho\":1}}]}", tg); return b; }

static float rateNorm (float hz) { return AU::toNorm (hz, 0.01f, 40.0f); }   // LFO Rate range + 0.3 skew

static double spreadOf (const std::vector<double>& v, int a, int b)
{
    double mn = 1e30, mx = -1e30;
    for (int i = a; i <= b && i < (int) v.size(); ++i) { mn = std::min (mn, v[(size_t) i]); mx = std::max (mx, v[(size_t) i]); }
    return (mx < mn) ? 0.0 : mx - mn;
}
static double pearson (const std::vector<double>& v, int a1, int a2, int n)
{
    double m1 = 0, m2 = 0;
    for (int i = 0; i < n; ++i) { m1 += v[(size_t) (a1 + i)]; m2 += v[(size_t) (a2 + i)]; }
    m1 /= n; m2 /= n;
    double num = 0, d1 = 0, d2 = 0;
    for (int i = 0; i < n; ++i)
    { const double x = v[(size_t) (a1 + i)] - m1, y = v[(size_t) (a2 + i)] - m2;
      num += x * y; d1 += x * x; d2 += y * y; }
    return num / std::sqrt (std::max (1e-20, d1 * d2));
}

// ── shared patch: fast attack, full sustain, LFO1 = sine, free-rate, full master depth ──
static void basePatch (AU& a, float lfoHz)
{
    a.setNorm ("Synth Amp Attack",  AU::toNorm (5.f,    1.f, 10000.f));
    a.setNorm ("Synth Amp Decay",   AU::toNorm (2000.f, 1.f, 10000.f));
    a.setNorm ("Synth Amp Sustain", 1.0f);
    a.setNorm ("Synth Amp Release", AU::toNorm (300.f,  1.f, 10000.f));
    a.setNorm ("LFO 1 Shape", 0.0f);       // SINE
    a.setNorm ("LFO 1 Sync",  0.0f);
    a.setNorm ("LFO 1 Depth", 1.0f);       // master ring, range -1..1: norm 1 = +1 (full)
    a.setNorm ("LFO 1 Phase", 0.0f);
    a.setNorm ("LFO 1 Rate",  rateNorm (lfoHz));
}

// One measured case: fresh AU, patch, route+trigger via state, timeline, windows out.
struct CaseOut { std::vector<double> rms, hr; double sumAbs = 0, sumSq = 0; bool ok = false; };
static CaseOut runCase (int tg, int dest, float depth, float lfoHz, bool rackFlt, bool wtSaw,
                        int nblk, const std::vector<Ev>& evs)
{
    CaseOut o;
    AU a; if (! a.open()) return o;
    basePatch (a, lfoHz);
    if (wtSaw)
    {
        a.setNorm ("Synth OSC A WT Preset", 0.20f);   // Prophet Saw — harmonics for the timbre metric
        a.setNorm ("Synth OSC A WT Frame",  0.55f);
    }
    if (rackFlt)
    {
        // the fb453 rack: Filter device, instance 1, powered + routed (devices arrive unrouted)
        a.setP ("SYN_FLT_ACTIVE", 1.0f); a.setP ("SYN_FLT_POWER", 1.0f);
        for (const char* s : { "SRC_A", "SRC_B", "SRC_C", "SRC_D", "SRC_SUB", "SRC_NOISE" })
            a.setP (std::string ("SYN_FLT_") + s, 1.0f);
        a.setP ("SYN_FLT_CUT", 0.35f); a.setP ("SYN_FLT_RES", 0.30f);
        a.setP ("SYN_FLT_DRIVE", 0.0f); a.setP ("SYN_FLT_MIX", 1.0f);
    }
    if (! a.setStateAttrs ({ { "synModJson",    routeJson (0, dest, depth) },
                             { "lfoShapesJson", motionJson (tg) } }))
    { printf ("  !! state install failed\n"); a.close(); return o; }
    a.pumpHost (0.40);   // let the state land + message-thread builds (WT preset, rack pool)
    if (rackFlt)
    {   // fb453 lesson: pooled/lazy engines are BUILT on the message thread after the audio
        // thread asks — a silent warm render + a pump, or the device measures as pass-through.
        std::vector<double> t1, t2;
        a.renderTimeline (24, {}, t1, t2);
        a.pumpHost (0.15);
    }
    a.renderTimeline (nblk, evs, o.rms, o.hr, &o.sumAbs, &o.sumSq);
    a.close();
    o.ok = true;
    return o;
}

static int npass = 0, nfail = 0;
static void gate (bool ok, const char* label, const char* detail)
{ (ok ? npass : nfail)++; printf ("  %s  %-46s %s\n", ok ? "ok  " : "FAIL", label, detail); }

int main()
{
    printf ("\n══ fb517 — LFO TRIGGER MODES, measured on the installed AU ══\n\n");

    const int dLevelA = (int) wc::ModDest::LevelA;      // synth-page GLOBAL dest (>= Res1 batch)
    const int dFrame  = (int) wc::ModDest::Frame;       // per-voice dest (voice bank applies it)
    const int dRackCut = wc::fxModDest (5, 0, 0);       // rack Filter, instance 1, knob 0 = CUT

    // ── G1 · ENV-STOPS (global dest) ──────────────────────────────────────────────────────────
    // 1 Hz one-shot: tremolo through windows ~1..7, pinned from window 8 on. Hold 24 windows.
    {
        std::vector<Ev> evs = { { 0, true }, { 288, false } };
        CaseOut env  = runCase (2, dLevelA, 0.9f, 1.0f, false, false, 300, evs);
        CaseOut free_ = runCase (0, dLevelA, 0.9f, 1.0f, false, false, 300, evs);
        const double envEarly = spreadOf (env.rms, 2, 7),  envLate = spreadOf (env.rms, 12, 23);
        const double frEarly  = spreadOf (free_.rms, 2, 7), frLate  = spreadOf (free_.rms, 12, 23);
        char d[256];
        snprintf (d, sizeof d, "env early %.2f dB late %.2f dB · free early %.2f dB late %.2f dB", envEarly, envLate, frEarly, frLate);
        gate (env.ok && free_.ok && frLate > 3.0, "G1 control: Free keeps moving after 1.5 s", d);
        gate (env.ok && envEarly > 3.0,           "G1 Env: motion present during the one-shot", d);
        gate (env.ok && free_.ok && envLate < 1.0 && envLate < 0.25 * frLate,
                                                  "G1 Env: STASIS once the one-shot pinned", d);
    }

    // ── G2 · TRIG-RESETS (global dest) ────────────────────────────────────────────────────────
    // 0.5 Hz; onsets 3.072 s apart = 1.536 cycles, so Free arrives ~half a cycle off.
    {
        std::vector<Ev> evs = { { 0, true }, { 180, false }, { 288, true }, { 468, false } };
        CaseOut trig = runCase (1, dLevelA, 0.9f, 0.5f, false, false, 480, evs);
        CaseOut free_ = runCase (0, dLevelA, 0.9f, 0.5f, false, false, 480, evs);
        const double cT = trig.ok ? pearson (trig.rms, 1, 25, 13) : 0.0;
        const double cF = free_.ok ? pearson (free_.rms, 1, 25, 13) : 0.0;
        char d[128]; snprintf (d, sizeof d, "corr trig %.3f · corr free %.3f", cT, cF);
        gate (free_.ok && cF < 0.5, "G2 control: Free does NOT repeat across onsets", d);
        gate (trig.ok && cT > 0.75, "G2 Trig: the pattern REPEATS from every note-on", d);
    }

    // ── G3 · FREE-UNCHANGED ───────────────────────────────────────────────────────────────────
    {
        std::vector<Ev> evs = { { 0, true }, { 288, false } };
        CaseOut fr = runCase (0, dLevelA, 0.9f, 2.0f, false, false, 300, evs);
        const double sp = spreadOf (fr.rms, 1, 22);
        char d[160]; snprintf (d, sizeof d, "spread %.2f dB · checksum sumAbs %.9e sumSq %.9e", sp, fr.sumAbs, fr.sumSq);
        gate (fr.ok && sp > 3.0, "G3 Free: full-render motion (+ printed fingerprint)", d);
    }

    // ── G4 · FX-PATH (fb453 rack dest under Env) ──────────────────────────────────────────────
    {
        std::vector<Ev> evs = { { 0, true }, { 288, false } };
        CaseOut env  = runCase (2, dRackCut, 0.6f, 1.0f, true, true, 300, evs);
        CaseOut free_ = runCase (0, dRackCut, 0.6f, 1.0f, true, true, 300, evs);
        const double envEarly = spreadOf (env.hr, 2, 7),  envLate = spreadOf (env.hr, 12, 23);
        const double frEarly  = spreadOf (free_.hr, 2, 7), frLate  = spreadOf (free_.hr, 12, 23);
        char d[256];
        snprintf (d, sizeof d, "env early %.4f late %.4f · free early %.4f late %.4f", envEarly, envLate, frEarly, frLate);
        gate (env.ok && free_.ok && frLate > 4.0 * std::max (1e-4, envLate) + (frLate > 1e-3 ? 0.0 : 1.0),
              "G4 rack Cut under Env: STASIS after the one-shot", d);
        gate (free_.ok && frLate > 1e-3, "G4 control: Free keeps sweeping the rack Cut", d);
        gate (env.ok && envEarly > 1e-3, "G4 Env: the rack route did move during the one-shot", d);
    }

    // ── V · PER-VOICE dest (informational — which path was broken?) ───────────────────────────
    {
        std::vector<Ev> evs = { { 0, true }, { 288, false } };
        CaseOut env  = runCase (2, dFrame, 0.9f, 1.0f, false, true, 300, evs);
        CaseOut free_ = runCase (0, dFrame, 0.9f, 1.0f, false, true, 300, evs);
        // depth-0 route = the metric's own floor on this patch (unison beating, detune shimmer —
        // timbral motion that has nothing to do with the LFO). The verdict is read against it.
        CaseOut none = runCase (2, dFrame, 0.0f, 1.0f, false, true, 300, evs);
        const double envLate = spreadOf (env.hr, 12, 23), frLate = spreadOf (free_.hr, 12, 23);
        const double envEarly = spreadOf (env.hr, 2, 7);
        const double floorLate = spreadOf (none.hr, 12, 23);
        printf ("  info  V per-voice Frame under Env: early %.4f late %.4f (free late %.4f · depth-0 floor %.4f) -> %s\n",
                envEarly, envLate, frLate, floorLate,
                (envLate < std::max (2.0 * floorLate, 0.25 * frLate)) ? "voice bank HONOURS Env (pins)"
                                                                      : "voice bank ALSO free-runs");
    }

    printf ("\n  PASS %d   FAIL %d\n\n", npass, nfail);
    return nfail ? 1 : 0;
}
