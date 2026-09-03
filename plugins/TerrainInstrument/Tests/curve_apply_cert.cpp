// ══════════════════════════════════════════════════════════════════════════════════════════════
//  curve_apply_cert.cpp — fb554's MOD-CONNECTION CURVE actually reaches the audio, on the real AU.
//
//    clang++ -std=c++17 -O2 Tests/curve_apply_cert.cpp -o /tmp/curve_apply_cert \
//        -framework AudioToolbox -framework CoreFoundation -framework CoreAudio && /tmp/curve_apply_cert
//
//  WHY IT EXISTS (Max, 2026-09-02): "I have an envelope attached to the fold, and I have a curve
//  edit. I play around with it, and it's not doing anything... If I put a noise on the volume and I
//  press Curve Edit... I should be able to hear a difference."
//
//  THE IDIOM is mod_src_cert.cpp's: routes are injected through the plugin's own state path
//  (synModJson -> setSynthModMatrix, the SAME parser the WebView's push() feeds), notes go through
//  MusicDeviceMIDIEvent, and the audio is measured. A curve rides the route as "c": 129 comma-
//  separated samples (fb554). Three curves per case:
//      straight  — no "c" at all (the fb554 law: a straight connection carries no curve)
//      inverted  — y = 1 - x
//      zero      — y = 0 everywhere
//      one       — y = 1 everywhere (where a bipolar / per-note source needs a constant)
//  If the curve is applied, inverted and zero MUST move the measurement away from straight.
//
//  TWO ORDERS, because the bug is an ORDER bug:
//      FRESH — the route and its curve arrive in ONE push after an empty matrix (a preset load, or
//              a route created and curved in the same gesture — how fb559 measured Key -> Level).
//      MAX'S — the route is pushed STRAIGHT first, a note is played, THEN the same route comes back
//              with a curve (the user right-clicks a modulated knob and draws). Only the "c" changes.
//  PluginProcessor.cpp's modCfgEq() compares source/dest/depth/aux/enabled and NOT `curve`, so in
//  MAX'S order the voices never receive the new curve index (synCfgChanged stays false and
//  setModConfig is not called): every per-voice destination (Fold / Level with a shape source /
//  Coarse / cutoff ...) keeps the stale index. The GLOBAL pass and the FX rack read the route list
//  directly each block, so the same curve works there in both orders. The WORKAROUND bar nudges
//  the depth by 0.01 with the curve present: the config now differs, the push happens, the curve
//  lands — which is exactly what a user who "plays around with it" never does.
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
static std::string fmt (const char* f, double a, double b = 0, double c = 0, double d = 0)
{ char buf[512]; std::snprintf (buf, sizeof buf, f, a, b, c, d); return buf; }

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
        if (! c) { std::printf ("  !! AU aumu/Tern/Wvcr not found\n"); return false; }
        if (AudioComponentInstanceNew (c, &au) != noErr) { std::printf ("  !! instantiate failed\n"); return false; }
        AudioStreamBasicDescription f {};
        f.mSampleRate = SR; f.mFormatID = kAudioFormatLinearPCM;
        f.mFormatFlags = kAudioFormatFlagsNativeFloatPacked | kAudioFormatFlagIsNonInterleaved;
        f.mBytesPerPacket = 4; f.mFramesPerPacket = 1; f.mBytesPerFrame = 4; f.mChannelsPerFrame = 2; f.mBitsPerChannel = 32;
        AudioUnitSetProperty (au, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Output, 0, &f, sizeof f);
        UInt32 mx = BLK;
        AudioUnitSetProperty (au, kAudioUnitProperty_MaximumFramesPerSlice, kAudioUnitScope_Global, 0, &mx, sizeof mx);
        if (AudioUnitInitialize (au) != noErr) { std::printf ("  !! AudioUnitInitialize failed\n"); return false; }
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
    static AudioUnitParameterID pid (const std::string& id) { uint32_t r = 0; for (unsigned char ch : id) r = 31u * r + (uint32_t) ch; return (AudioUnitParameterID) (r & 0x7FFFFFFFu); }
    bool hasId (const std::string& id) const { return info.count (pid (id)) > 0; }
    bool setId (const std::string& id, float norm)
    {
        auto it = info.find (pid (id)); if (it == info.end()) return false;
        const auto& pi = it->second;
        return AudioUnitSetParameter (au, it->first, kAudioUnitScope_Global, 0, pi.minValue + norm * (pi.maxValue - pi.minValue), 0) == noErr;
    }
    bool set (const std::string& n, float norm)
    {
        auto it = byName.find (n); if (it == byName.end()) { std::printf ("  !! no param '%s'\n", n.c_str()); return false; }
        const auto& pi = info.at (it->second);
        const float v = pi.minValue + norm * (pi.maxValue - pi.minValue);
        return AudioUnitSetParameter (au, it->second, kAudioUnitScope_Global, 0, v, 0) == noErr;
    }
    bool setRaw (const std::string& n, float v)
    {
        auto it = byName.find (n); if (it == byName.end()) { std::printf ("  !! no param '%s'\n", n.c_str()); return false; }
        return AudioUnitSetParameter (au, it->second, kAudioUnitScope_Global, 0, v, 0) == noErr;
    }
    // the plugin's own state path: rewrite synModJson in the saved XML and hand it back (mod_src_cert's idiom)
    bool setRoutes (const std::string& json)
    {
        CFPropertyListRef pl = nullptr; UInt32 psz = sizeof (pl);
        if (AudioUnitGetProperty (au, kAudioUnitProperty_ClassInfo, kAudioUnitScope_Global, 0, &pl, &psz) != noErr || pl == nullptr) return false;
        CFDictionaryRef dict = (CFDictionaryRef) pl;
        CFStringRef key = CFSTR ("jucePluginState");
        CFDataRef dd = (CFDataRef) CFDictionaryGetValue (dict, key);
        if (dd == nullptr) { CFRelease (pl); return false; }
        const UInt8* p = CFDataGetBytePtr (dd);
        uint32_t magic = 0, len = 0; std::memcpy (&magic, p, 4); std::memcpy (&len, p + 4, 4);
        if (magic != 0x21324356u || (CFIndex) (len + 8) > CFDataGetLength (dd)) { CFRelease (pl); return false; }
        std::string xml ((const char*) p + 8, len);
        std::string esc; for (char ch : json) esc += (ch == '"' ? "&quot;" : ch == '&' ? "&amp;" : ch == '<' ? "&lt;" : ch == '>' ? "&gt;" : std::string (1, ch));
        const std::string attr = " synModJson=\"" + esc + "\"";
        const size_t at = xml.find (" synModJson=\"");
        if (at != std::string::npos) { const size_t e = xml.find ('"', at + 13); xml = xml.substr (0, at) + attr + xml.substr (e + 1); }
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
        std::vector<float> out; out.reserve ((size_t) nblk * BLK);
        std::vector<float> bl ((size_t) BLK), br ((size_t) BLK);
        AudioBufferList* abl = (AudioBufferList*) calloc (1, sizeof (AudioBufferList) + sizeof (AudioBuffer));
        abl->mNumberBuffers = 2;
        AudioTimeStamp ts {}; ts.mFlags = kAudioTimeStampSampleTimeValid; ts.mSampleTime = clock_;
        for (int b = 0; b < nblk; ++b)
        {
            abl->mBuffers[0] = { 1, (UInt32) (BLK * 4), bl.data() };
            abl->mBuffers[1] = { 1, (UInt32) (BLK * 4), br.data() };
            AudioUnitRenderActionFlags fl = 0;
            if (AudioUnitRender (au, &fl, &ts, 0, BLK, abl) != noErr) break;
            ts.mSampleTime += BLK; clock_ += BLK;
            for (int i = 0; i < BLK; ++i) out.push_back (0.5f * (bl[(size_t) i] + br[(size_t) i]));
        }
        free (abl);
        return out;
    }
    double clock_ = 0;
    // one note: note-on, settle, measure the sustained part, note-off, let the release die.
    // settle 30 blocks = 320 ms: past the amp env's 5 ms attack + 200 ms decay, so the body sits on the SUSTAIN.
    std::vector<float> note (int nn, int sustainBlk = 24, int settleBlk = 30, int tailBlk = 40)
    {
        midi (0x90, (UInt32) nn, 100);
        render (settleBlk);
        auto body = render (sustainBlk);
        midi (0x80, (UInt32) nn, 0);
        render (tailBlk);
        return body;
    }
    // one note, two windows inside it (block ranges), for a source that MOVES during the note (an LFO)
    void noteWindows (int nn, int fromA, int toA, int fromB, int toB, std::vector<float>& wA, std::vector<float>& wB, int tailBlk = 40)
    {
        wA.clear(); wB.clear();
        midi (0x90, (UInt32) nn, 100);
        const int last = std::max (toA, toB);
        for (int b = 0; b < last; ++b)
        {
            auto x = render (1);
            if (b >= fromA && b < toA) wA.insert (wA.end(), x.begin(), x.end());
            if (b >= fromB && b < toB) wB.insert (wB.end(), x.begin(), x.end());
        }
        midi (0x80, (UInt32) nn, 0);
        render (tailBlk);
    }
};

static double rmsDb (const std::vector<float>& v)
{ double s = 0; for (float x : v) s += (double) x * x; return 10.0 * std::log10 (s / std::max<size_t> (1, v.size()) + 1e-30); }

// harmonic spectrum by DFT at k*f0 on a Hann window: centroid in HARMONIC NUMBER + live harmonics
// (within -40 dB of the loudest) — curve_probe.mm's report(), the fb559 ruler.
struct Spec { double centroid = 0; int live = 0; double rms = 0; };
static Spec spec (const std::vector<float>& v, int note, int K = 48)
{
    const double f0 = 440.0 * std::pow (2.0, (note - 69) / 12.0);
    const int N = (int) v.size();
    std::vector<double> H ((size_t) K + 1, 0.0);
    for (int k = 1; k <= K; ++k)
    {
        if (k * f0 >= SR / 2 - 200) break;
        double re = 0, im = 0;
        for (int n = 0; n < N; ++n)
        { const double wn = 0.5 - 0.5 * std::cos (2.0 * M_PI * n / (N - 1)); const double a = 2.0 * M_PI * k * f0 * n / SR;
          re += v[(size_t) n] * wn * std::cos (a); im -= v[(size_t) n] * wn * std::sin (a); }
        H[(size_t) k] = std::sqrt (re * re + im * im);
    }
    Spec s; double num = 0, den = 0, pk = 0, tot = 0;
    for (int k = 1; k <= K; ++k) { num += k * H[(size_t) k]; den += H[(size_t) k]; tot += H[(size_t) k] * H[(size_t) k]; pk = std::max (pk, H[(size_t) k]); }
    for (int k = 1; k <= K; ++k) if (H[(size_t) k] > pk * 0.01) ++s.live;
    s.centroid = den > 0 ? num / den : 0.0; s.rms = std::sqrt (tot);
    return s;
}

// the wire's curve: 129 samples, comma-separated (index.html push(): a.curve.map(toFixed(4)).join(','))
static std::string curveCsv (const char* kind)
{
    std::string c;
    for (int k = 0; k < 129; ++k)
    {
        const double x = (double) k / 128.0;
        double y = x;
        if (! std::strcmp (kind, "inv"))  y = 1.0 - x;
        if (! std::strcmp (kind, "zero")) y = 0.0;
        if (! std::strcmp (kind, "one"))  y = 1.0;
        if (k) c += ",";
        c += fmt ("%.4f", y);
    }
    return c;
}
static std::string route (int s, int d, double v, const char* curve = nullptr)
{
    std::string r = "{\"s\":" + std::to_string (s) + ",\"d\":" + std::to_string (d) + ",\"v\":" + fmt ("%.4f", v);
    if (curve != nullptr && *curve) r += ",\"c\":\"" + curveCsv (curve) + "\"";
    return r + "}";
}
static std::string arr (const std::string& a) { return "[" + a + "]"; }
static std::string arr (const std::string& a, const std::string& b) { return "[" + a + "," + b + "]"; }

// wire codes (SynthModConfig.h): env base 100 · velocity 200 · Key 201 · followers 210+ · macros 220+ · wheel 230 · random 240+
static const int W_ENV1 = 100, W_LFO1 = 0, W_KEY = 201, W_MACRO1 = 220, W_RAND1 = 240;
static const int D_FOLD = 4, D_LEVEL_A = 64, D_RVB_MIX = 697;

int main()
{
    setvbuf (stdout, nullptr, _IOLBF, 0);
    std::printf ("\n══ fb554 THE MOD-CONNECTION CURVE REACHES THE AUDIO? (installed AU) ══\n");
    AU au; if (! au.open()) return 2;
    const std::string LVL = au.find ("OSC A Level"), FOLD = au.find ("OSC A Fold Amount"), WT = au.find ("OSC A WT Preset"),
                      SUS = au.find ("Synth Amp Sustain"), MAC1 = au.find ("Macro 1"),
                      LR = au.find ("LFO 1 Rate"), LS = au.find ("LFO 1 Shape"), LD = au.find ("LFO 1 Depth"), LSY = au.find ("LFO 1 Sync");
    std::printf ("   params: level='%s' fold='%s' wt='%s' sustain='%s' macro='%s' lfo='%s'/'%s'/'%s'\n",
                 LVL.c_str(), FOLD.c_str(), WT.c_str(), SUS.c_str(), MAC1.c_str(), LR.c_str(), LS.c_str(), LD.c_str());
    chk (! LVL.empty() && ! FOLD.empty() && ! WT.empty() && ! SUS.empty() && ! MAC1.empty() && ! LR.empty() && ! LS.empty() && ! LD.empty(),
         "0  the AU exposes Level A, Fold Amount, WT Preset, Amp Sustain, Macro 1 and LFO 1");
    if (LVL.empty() || FOLD.empty() || WT.empty() || SUS.empty() || MAC1.empty() || LR.empty() || LS.empty() || LD.empty()) { au.close(); return 1; }

    // Osc A only. A REAL table (preset 4 = Prophet Saw — "a sine has one harmonic and cannot fail a shaping test", the fb553 law).
    const std::string LB = au.find ("OSC B Level"), LC = au.find ("OSC C Level"), LDl = au.find ("OSC D Level");
    if (! LB.empty()) au.set (LB, 0.0f); if (! LC.empty()) au.set (LC, 0.0f); if (! LDl.empty()) au.set (LDl, 0.0f);
    au.setRaw (WT, 4.0f);
    // the amp env's sustain at 0.8 (default 0.7): straight / inverted / zero then land on 0.8 / 0.2 / 0.0 of the
    // owned knob — three distinct values, none equal to the dry fold base of 0.3.
    au.set (SUS, 0.8f);
    if (! LSY.empty()) au.set (LSY, 0.0f);
    au.pump (0.5);
    const int NOTE = 60;

    auto foldSpec = [&] () { return spec (au.note (NOTE), NOTE); };
    auto pr = [] (const char* tag, const Spec& s) { std::printf ("      %-44s centroid %6.2f h   live %2d   rms %.4f\n", tag, s.centroid, s.live, s.rms); };
    auto moved = [] (const Spec& a, const Spec& b) { return std::abs (a.centroid - b.centroid) > 0.25 || std::abs (a.live - b.live) >= 2; };

    // ═════ 1 · ENV 1 -> FOLD (dest 4), fold base 0.3, level 1 ═════
    std::printf ("\n── 1 · Env 1 -> Osc A Fold (dest 4) · fold knob 0.3 · depth 1 · sustain 0.8 ──\n");
    au.set (LVL, 1.0f); au.set (FOLD, 0.3f); au.pump (0.3);
    au.setRoutes ("[]");
    const Spec f_dry = foldSpec(); pr ("dry (no route, fold 0.3)", f_dry);
    au.set (FOLD, 0.8f); au.pump (0.3); const Spec f_ref80 = foldSpec(); pr ("REFERENCE: knob at 0.8, no route", f_ref80);
    au.set (FOLD, 0.2f); au.pump (0.3); const Spec f_ref20 = foldSpec(); pr ("REFERENCE: knob at 0.2, no route", f_ref20);
    au.set (FOLD, 0.0f); au.pump (0.3); const Spec f_ref00 = foldSpec(); pr ("REFERENCE: knob at 0.0, no route", f_ref00);
    au.set (FOLD, 0.3f); au.pump (0.3);
    // FRESH order: empty matrix, a note, then route+curve in one push
    au.setRoutes ("[]"); au.note (NOTE);
    au.setRoutes (arr (route (W_ENV1, D_FOLD, 1.0)));          const Spec f_s = foldSpec(); pr ("FRESH  straight (expect ~knob 0.8)", f_s);
    au.setRoutes ("[]"); au.note (NOTE);
    au.setRoutes (arr (route (W_ENV1, D_FOLD, 1.0, "inv")));   const Spec f_i = foldSpec(); pr ("FRESH  inverted (expect ~knob 0.2)", f_i);
    au.setRoutes ("[]"); au.note (NOTE);
    au.setRoutes (arr (route (W_ENV1, D_FOLD, 1.0, "zero")));  const Spec f_z = foldSpec(); pr ("FRESH  zero     (expect ~knob 0.0)", f_z);
    chk (moved (f_s, f_dry), "1a Env 1 -> Fold, straight: the route itself is audible (moved from dry)");
    chk (moved (f_i, f_s) && moved (f_z, f_s), "1b FRESH order: inverted and zero curves MOVE the fold (route + curve in one push)");
    // MAX'S order: the route straight first, a note, then the SAME route with a curve
    au.setRoutes ("[]"); au.note (NOTE);
    au.setRoutes (arr (route (W_ENV1, D_FOLD, 1.0)));          const Spec m_s = foldSpec(); pr ("MAX'S  straight first", m_s);
    au.setRoutes (arr (route (W_ENV1, D_FOLD, 1.0, "inv")));   const Spec m_i = foldSpec(); pr ("MAX'S  ...then inverted on the same route", m_i);
    au.setRoutes (arr (route (W_ENV1, D_FOLD, 1.0, "zero")));  const Spec m_z = foldSpec(); pr ("MAX'S  ...then zero on the same route", m_z);
    chk (moved (m_i, m_s) && moved (m_z, m_s), "1c MAX'S order: a curve drawn on an EXISTING straight route moves the fold");
    au.setRoutes (arr (route (W_ENV1, D_FOLD, 0.99, "zero"))); const Spec m_w = foldSpec(); pr ("WORKAROUND: zero curve + depth 1.00 -> 0.99", m_w);
    chk (moved (m_w, m_s), "1d ...and the same zero curve lands the moment the DEPTH changes by 0.01 (the config re-push)");
    au.setRoutes ("[]"); au.note (NOTE);

    // ═════ 2 · LFO 1 -> FOLD (dest 4) ═════
    //  SAW UP (shape 2) at 0.2 Hz (a 5 s cycle), depth 0.5, fold base 0.5. The LFO is bipolar (-1..+1), so a curve
    //  maps through 0..1 and back. The LFO is FREE (the product default) and keeps running through every note and
    //  tail, so each note starts at a different phase: a slope or a level of the straight / inverted cases is NOT a
    //  ruler (run 1 of this cert read a false pass from exactly that). The rulers are the two CONSTANT curves:
    //  zero = a constant -1 -> fold parked at 0.0 (must read as the fold-0 reference, flat across the note), and
    //  one = a constant +1 -> fold parked at 1.0 (flat, and not the fold-0 reference). Straight / inverted print.
    std::printf ("\n── 2 · LFO 1 (saw up, 0.2 Hz) -> Osc A Fold (dest 4) · fold knob 0.5 · depth 0.5 ──\n");
    {
        const float shapeMax = au.info.at (au.byName.at (LS)).maxValue;
        au.set (LS, 2.0f / shapeMax); au.set (LD, 1.0f); au.set (LR, 0.215f); au.set (FOLD, 0.5f); au.pump (0.3);   // 0.2 Hz on the 0.01..40 Hz skew-0.3 range
        auto lfoWin = [&] (const char* tag, Spec& e, Spec& l)
        { std::vector<float> a, b; au.noteWindows (NOTE, 12, 24, 84, 96, a, b); e = spec (a, NOTE); l = spec (b, NOTE);
          std::printf ("      %-44s early %6.2f h / %2d   late %6.2f h / %2d   slope %+.2f\n", tag, e.centroid, e.live, l.centroid, l.live, l.centroid - e.centroid); };
        Spec e, l, es, ls, ei, li, ez, lz, eo, lo;
        au.setRoutes ("[]"); lfoWin ("dry (no route, fold 0.5)", e, l);
        au.setRoutes ("[]"); au.note (NOTE); au.setRoutes (arr (route (W_LFO1, D_FOLD, 0.5)));          lfoWin ("FRESH  straight (rises)", es, ls);
        au.setRoutes ("[]"); au.note (NOTE); au.setRoutes (arr (route (W_LFO1, D_FOLD, 0.5, "inv")));   lfoWin ("FRESH  inverted (falls)", ei, li);
        au.setRoutes ("[]"); au.note (NOTE); au.setRoutes (arr (route (W_LFO1, D_FOLD, 0.5, "zero")));  lfoWin ("FRESH  zero (flat, fold 0)", ez, lz);
        au.setRoutes ("[]"); au.note (NOTE); au.setRoutes (arr (route (W_LFO1, D_FOLD, 0.5, "one")));   lfoWin ("FRESH  one  (flat, fold 1)", eo, lo);
        auto flatAt = [&] (const Spec& a, const Spec& b, const Spec& ref) { return std::abs (a.centroid - b.centroid) < 0.3 && std::abs (a.live - b.live) < 2
                                                                                 && std::abs (a.centroid - ref.centroid) < 0.3 && std::abs (a.live - ref.live) < 2; };
        chk (flatAt (ez, lz, f_ref00), "2a FRESH order: the zero curve parks LFO 1 -> Fold at the fold-0 reference, flat across the note");
        chk (std::abs (eo.centroid - lo.centroid) < 0.3 && ! flatAt (eo, lo, f_ref00), "2b FRESH order: the one curve parks it flat somewhere else (fold 1.0)");
        Spec ms_e, ms_l, mi_e, mi_l, mz_e, mz_l;
        au.setRoutes ("[]"); au.note (NOTE);
        au.setRoutes (arr (route (W_LFO1, D_FOLD, 0.5)));         lfoWin ("MAX'S  straight first", ms_e, ms_l);
        au.setRoutes (arr (route (W_LFO1, D_FOLD, 0.5, "inv")));  lfoWin ("MAX'S  ...then inverted on the same route", mi_e, mi_l);
        au.setRoutes (arr (route (W_LFO1, D_FOLD, 0.5, "zero"))); lfoWin ("MAX'S  ...then zero on the same route", mz_e, mz_l);
        Spec mo_e, mo_l, mw_e, mw_l;
        au.setRoutes (arr (route (W_LFO1, D_FOLD, 0.5, "one")));  lfoWin ("MAX'S  ...then one on the same route", mo_e, mo_l);
        chk (flatAt (mz_e, mz_l, f_ref00), "2c MAX'S order: the zero curve on an existing LFO route parks the fold at 0 (flat)");
        chk (std::abs (mo_e.centroid - mo_l.centroid) < 0.3 && ! flatAt (mo_e, mo_l, f_ref00), "2d MAX'S order: the one curve on an existing LFO route parks it flat at fold 1");
        au.setRoutes (arr (route (W_LFO1, D_FOLD, 0.49, "zero"))); lfoWin ("WORKAROUND: zero curve + depth 0.50 -> 0.49", mw_e, mw_l);
        chk (flatAt (mw_e, mw_l, f_ref00), "2e ...and the zero curve lands once the depth moves by 0.01");
        au.setRoutes ("[]"); au.note (NOTE); au.set (LS, 0.0f); au.set (LR, 0.5f); au.set (FOLD, 0.0f); au.pump (0.3);
    }

    // ═════ 3 · ENV 1 -> LEVEL A (dest 64), level knob 0 — fb563's trick: the source is the only way to a sound ═════
    std::printf ("\n── 3 · Env 1 -> Osc A Level (dest 64) · level knob 0 · depth 1 · sustain 0.8 ──\n");
    au.set (LVL, 0.0f); au.set (FOLD, 0.0f); au.pump (0.3);
    auto lvlDb = [&] () { return rmsDb (au.note (NOTE)); };
    auto prd = [] (const char* tag, double d) { std::printf ("      %-44s %8.1f dB\n", tag, d); };
    au.setRoutes ("[]"); const double l_dry = lvlDb(); prd ("dry (no route, level 0)", l_dry);
    au.setRoutes ("[]"); au.note (NOTE); au.setRoutes (arr (route (W_ENV1, D_LEVEL_A, 1.0)));          const double l_s = lvlDb(); prd ("FRESH  straight (expect level 0.8)", l_s);
    au.setRoutes ("[]"); au.note (NOTE); au.setRoutes (arr (route (W_ENV1, D_LEVEL_A, 1.0, "inv")));   const double l_i = lvlDb(); prd ("FRESH  inverted (expect level 0.2 = -12 dB)", l_i);
    au.setRoutes ("[]"); au.note (NOTE); au.setRoutes (arr (route (W_ENV1, D_LEVEL_A, 1.0, "zero")));  const double l_z = lvlDb(); prd ("FRESH  zero     (expect silence)", l_z);
    chk (l_dry < -70 && l_s > -30, "3a Env 1 -> Level A, straight: the route is audible over a silent knob");
    chk ((l_s - l_i) > 6 && l_z < -60, "3b FRESH order: inverted is ~12 dB down, zero is silent");
    au.setRoutes ("[]"); au.note (NOTE);
    au.setRoutes (arr (route (W_ENV1, D_LEVEL_A, 1.0)));          const double ml_s = lvlDb(); prd ("MAX'S  straight first", ml_s);
    au.setRoutes (arr (route (W_ENV1, D_LEVEL_A, 1.0, "inv")));   const double ml_i = lvlDb(); prd ("MAX'S  ...then inverted on the same route", ml_i);
    au.setRoutes (arr (route (W_ENV1, D_LEVEL_A, 1.0, "zero")));  const double ml_z = lvlDb(); prd ("MAX'S  ...then zero on the same route", ml_z);
    chk ((ml_s - ml_i) > 6 && ml_z < -60, "3c MAX'S order: a curve drawn on an existing Env -> Level route is heard");
    au.setRoutes (arr (route (W_ENV1, D_LEVEL_A, 0.99, "zero"))); const double ml_w = lvlDb(); prd ("WORKAROUND: zero curve + depth 0.99", ml_w);
    chk (ml_w < -60, "3d ...the same zero curve lands once the depth moves by 0.01");
    au.setRoutes ("[]"); au.note (NOTE);

    // ═════ 4 · RANDOM 1 -> LEVEL A (dest 64), level 0 — a per-note draw; the GLOBAL pass carries Random -> Level ═════
    std::printf ("\n── 4 · Random 1 -> Osc A Level (dest 64) · level knob 0 · depth 1 · six notes each ──\n");
    auto six = [&] (const char* tag) { std::string s; std::vector<double> v; for (int i = 0; i < 6; ++i) { v.push_back (lvlDb()); s += fmt ("%6.1f ", v.back()); }
                                       std::printf ("      %-44s %s dB\n", tag, s.c_str()); return v; };
    auto allBelow = [] (const std::vector<double>& v, double t) { for (double x : v) if (x >= t) return false; return true; };
    auto allAbove = [] (const std::vector<double>& v, double t) { for (double x : v) if (x <= t) return false; return true; };
    au.setRoutes ("[]"); au.note (NOTE); au.setRoutes (arr (route (W_RAND1, D_LEVEL_A, 1.0)));          auto r_s = six ("FRESH  straight (scatter)");
    au.setRoutes ("[]"); au.note (NOTE); au.setRoutes (arr (route (W_RAND1, D_LEVEL_A, 1.0, "inv")));   auto r_i = six ("FRESH  inverted (scatter, 1-u)");
    au.setRoutes ("[]"); au.note (NOTE); au.setRoutes (arr (route (W_RAND1, D_LEVEL_A, 1.0, "zero")));  auto r_z = six ("FRESH  zero (all silent)");
    au.setRoutes ("[]"); au.note (NOTE); au.setRoutes (arr (route (W_RAND1, D_LEVEL_A, 1.0, "one")));   auto r_o = six ("FRESH  one  (all full)");
    chk (allBelow (r_z, -60) && allAbove (r_o, -30), "4a FRESH order: zero curve silences every note, one curve opens every note");
    au.setRoutes ("[]"); au.note (NOTE);
    au.setRoutes (arr (route (W_RAND1, D_LEVEL_A, 1.0)));          auto mr_s = six ("MAX'S  straight first");
    au.setRoutes (arr (route (W_RAND1, D_LEVEL_A, 1.0, "zero")));  auto mr_z = six ("MAX'S  ...then zero on the same route");
    au.setRoutes (arr (route (W_RAND1, D_LEVEL_A, 1.0, "one")));   auto mr_o = six ("MAX'S  ...then one on the same route");
    chk (allBelow (mr_z, -60) && allAbove (mr_o, -30), "4b MAX'S order: Random -> Level (global pass) takes the curve on an existing route");
    au.setRoutes ("[]"); au.note (NOTE);

    // ═════ 5 · MACRO 1 -> LEVEL A (dest 64), level 0, macro at 25 % — the GLOBAL pass ═════
    std::printf ("\n── 5 · Macro 1 -> Osc A Level (dest 64) · level knob 0 · depth 1 · macro 25 %% ──\n");
    au.set (MAC1, 0.25f); au.pump (0.3);
    au.setRoutes ("[]"); au.note (NOTE); au.setRoutes (arr (route (W_MACRO1, D_LEVEL_A, 1.0)));          const double g_s = lvlDb(); prd ("FRESH  straight (level 0.25 = -12 dB rel.)", g_s);
    au.setRoutes ("[]"); au.note (NOTE); au.setRoutes (arr (route (W_MACRO1, D_LEVEL_A, 1.0, "inv")));   const double g_i = lvlDb(); prd ("FRESH  inverted (level 0.75 = -2.5 dB rel.)", g_i);
    au.setRoutes ("[]"); au.note (NOTE); au.setRoutes (arr (route (W_MACRO1, D_LEVEL_A, 1.0, "zero")));  const double g_z = lvlDb(); prd ("FRESH  zero (silent)", g_z);
    chk ((g_i - g_s) > 6 && g_z < -60, "5a FRESH order: inverted is ~9.5 dB louder than straight, zero is silent");
    au.setRoutes ("[]"); au.note (NOTE);
    au.setRoutes (arr (route (W_MACRO1, D_LEVEL_A, 1.0)));          const double mg_s = lvlDb(); prd ("MAX'S  straight first", mg_s);
    au.setRoutes (arr (route (W_MACRO1, D_LEVEL_A, 1.0, "inv")));   const double mg_i = lvlDb(); prd ("MAX'S  ...then inverted on the same route", mg_i);
    au.setRoutes (arr (route (W_MACRO1, D_LEVEL_A, 1.0, "zero")));  const double mg_z = lvlDb(); prd ("MAX'S  ...then zero on the same route", mg_z);
    chk ((mg_i - mg_s) > 6 && mg_z < -60, "5b MAX'S order: Macro -> Level (global pass) takes the curve on an existing route");
    au.set (MAC1, 0.0f); au.pump (0.3);
    au.setRoutes ("[]"); au.note (NOTE);

    // ═════ 6 · KEY -> LEVEL A (dest 64), level 0, note 84 — fb559's baseline (ramp 0.8 at 84: C1=36 .. C6=96) ═════
    std::printf ("\n── 6 · Key -> Osc A Level (dest 64) · level knob 0 · depth 1 · note 84 (key ramp 0.8) ──\n");
    auto lvl84 = [&] () { return rmsDb (au.note (84)); };
    au.setRoutes ("[]"); const double k_dry = lvl84(); prd ("dry (no route, level 0)", k_dry);
    au.setRoutes ("[]"); au.note (84); au.setRoutes (arr (route (W_KEY, D_LEVEL_A, 1.0)));          const double k_s = lvl84(); prd ("FRESH  straight (level 0.8)", k_s);
    au.setRoutes ("[]"); au.note (84); au.setRoutes (arr (route (W_KEY, D_LEVEL_A, 1.0, "inv")));   const double k_i = lvl84(); prd ("FRESH  inverted (level 0.2 = -12 dB)", k_i);
    au.setRoutes ("[]"); au.note (84); au.setRoutes (arr (route (W_KEY, D_LEVEL_A, 1.0, "zero")));  const double k_z = lvl84(); prd ("FRESH  zero (silent)", k_z);
    chk (k_dry < -70 && k_s > -30 && (k_s - k_i) > 6 && k_z < -60, "6a FRESH order (fb559's measurement): inverted ~12 dB down, zero silent");
    au.setRoutes ("[]"); au.note (84);
    au.setRoutes (arr (route (W_KEY, D_LEVEL_A, 1.0)));          const double mk_s = lvl84(); prd ("MAX'S  straight first", mk_s);
    au.setRoutes (arr (route (W_KEY, D_LEVEL_A, 1.0, "inv")));   const double mk_i = lvl84(); prd ("MAX'S  ...then inverted on the same route", mk_i);
    au.setRoutes (arr (route (W_KEY, D_LEVEL_A, 1.0, "zero")));  const double mk_z = lvl84(); prd ("MAX'S  ...then zero on the same route", mk_z);
    chk ((mk_s - mk_i) > 6 && mk_z < -60, "6b MAX'S order: a curve drawn on an existing Key -> Level route is heard");
    au.setRoutes ("[]"); au.note (84);

    // ═════ 7 · THE WRONG CURVE — two routes, the index shifts under the voice ═════
    //  [A: Env1 -> Fold straight, B: Env1 -> Level A inverted]: B's curve is index 0. Then A GAINS a curve ("one"),
    //  so on the wire A = index 0 and B = index 1. The voices, never re-pushed (only `curve` changed), keep B at
    //  index 0 — which is now A's "one" curve: Level A jumps from 0.2 (inverted) to 1.0 (one). Expected: unchanged.
    std::printf ("\n── 7 · Two routes: adding a curve to route A re-indexes route B's curve under the voice ──\n");
    au.set (LVL, 0.0f); au.set (FOLD, 0.3f); au.pump (0.3);
    au.setRoutes ("[]"); au.note (NOTE);
    au.setRoutes (arr (route (W_ENV1, D_FOLD, 1.0), route (W_ENV1, D_LEVEL_A, 1.0, "inv")));         const double x_b = lvlDb(); prd ("[A straight, B inverted] -> B: level 0.2 (-12 dB rel.)", x_b);
    au.setRoutes (arr (route (W_ENV1, D_FOLD, 1.0, "one"), route (W_ENV1, D_LEVEL_A, 1.0, "inv")));  const double x_a = lvlDb(); prd ("[A one, B inverted]      -> B should be unchanged", x_a);
    chk (std::abs (x_a - x_b) < 2.0, "7  route B's level is unchanged when route A gains a curve (else B reads A's curve: the index shift)", fmt ("%.1f -> %.1f dB", x_b, x_a));
    au.setRoutes ("[]"); au.note (NOTE); au.set (FOLD, 0.0f); au.pump (0.3);

    // ═════ 8 · THE RACK — Macro 1 -> Reverb Mix (697), the FX rack's own walk (FxModValue.h), MAX'S order ═════
    {
        const bool haveRvb = au.hasId ("SYN_RVB_MIX") && au.hasId ("SYN_RVB_POWER") && au.hasId ("SYN_RVB_ACTIVE") && au.hasId ("SYN_RVB_SRC_A");
        std::printf ("\n── 8 · Macro 1 -> Reverb Mix (dest 697) · mix knob 0 · macro 100 · the rack's walk · MAX'S order ──\n");
        if (haveRvb)
        {
            au.set (LVL, 1.0f); au.setId ("SYN_RVB_ACTIVE", 1.0f); au.setId ("SYN_RVB_POWER", 1.0f); au.setId ("SYN_RVB_SRC_A", 1.0f); au.setId ("SYN_RVB_MIX", 0.0f);
            au.pump (0.6); au.midi (0x90, 60, 100); au.render (20); au.midi (0x80, 60, 0); au.render (40); au.pump (0.6);
            auto tailDb = [&] () { au.midi (0x90, 60, 100); au.render (30); au.midi (0x80, 60, 0); au.render (40); return rmsDb (au.render (30)); };
            au.set (MAC1, 1.0f); au.pump (0.2);
            au.setRoutes (arr (route (W_MACRO1, D_RVB_MIX, 1.0)));          const double t_s = tailDb(); prd ("MAX'S  straight first (tail)", t_s);
            au.setRoutes (arr (route (W_MACRO1, D_RVB_MIX, 1.0, "zero")));  const double t_z = tailDb(); prd ("MAX'S  ...then zero on the same route (no tail)", t_z);
            au.setRoutes (arr (route (W_MACRO1, D_RVB_MIX, 1.0, "inv")));   const double t_i = tailDb(); prd ("MAX'S  ...then inverted (macro 100 -> mix 0, no tail)", t_i);
            chk (t_s > -45 && t_z < -60 && t_i < -60, "8  the rack takes a curve on an existing route (FxModValue.h reads the route list each block)");
            au.set (MAC1, 0.0f); au.pump (0.2);
            au.setRoutes ("[]"); au.setId ("SYN_RVB_ACTIVE", 0.0f); au.setId ("SYN_RVB_POWER", 0.0f); au.setId ("SYN_RVB_SRC_A", 0.0f); au.setId ("SYN_RVB_MIX", 0.35f); au.set (LVL, 0.0f); au.pump (0.4);
        }
        else std::printf ("      (reverb ids not exposed — skipped)\n");
    }

    au.close();
    std::printf ("\n  %d pass · %d fail\n\n", PASS, FAIL);
    return FAIL ? 1 : 0;
}
