// ══════════════════════════════════════════════════════════════════════════════════════════════
//  tp41 — THE PATCHER'S RULES GATE, on the REAL installed AU.
//
//   [TAP]    a device's DIRECT TAP hears the raw oscillator, never the main filter: with osc A sent
//            into a closed low-pass and Utility 1 routed to A, the rack's output is dark; with A's
//            bit set in "Utility Direct Taps" it is bright, and it NULLS against A not being in the
//            filter at all (the same dry bus, the same floats).
//   [INLINE] a flow card can sit IN the rack order: a Chop with gate 0 / blend 1 cuts holes in a
//            held note; a Reverb after it (Flow Chop In Rack + a rank below the reverb's) fills them
//            with its tail, while the same reverb BEFORE it (the old law) leaves the holes.
//   [PASS]   a device after a flow card passes the signal once: a mix-0 reverb after the Chop is the
//            Chop alone, within a small tolerance.
//
//    clang++ -O2 -std=c++17 Tests/au_patcher_rules.cpp -o /tmp/aupr \
//            -framework AudioToolbox -framework AudioUnit -framework CoreFoundation -framework CoreAudio
//    /tmp/aupr
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
static const double SR = 48000.0; static const int BLK = 512;
static int npass = 0, nfail = 0;
static void chk (bool ok, const char* what, const char* detail = "")
{ if (ok) { ++npass; printf ("  PASS  %s   %s\n", what, detail); } else { ++nfail; printf ("  FAIL  %s   %s\n", what, detail); } }

struct Au
{
    AudioUnit au = nullptr; std::map<std::string, AudioUnitParameterID> byName; std::map<AudioUnitParameterID, AudioUnitParameterInfo> info;
    double stamp = 0.0;
    bool open()
    {
        setenv ("TERRAIN_DETERMINISTIC", "1", 1);
        AudioComponentDescription d {}; d.componentType = kAudioUnitType_MusicDevice; d.componentSubType = 'Tern'; d.componentManufacturer = 'Wvcr';
        AudioComponent c = AudioComponentFindNext (nullptr, &d); if (! c || AudioComponentInstanceNew (c, &au) != noErr) return false;
        AudioStreamBasicDescription f {}; f.mSampleRate = SR; f.mFormatID = kAudioFormatLinearPCM;
        f.mFormatFlags = kAudioFormatFlagsNativeFloatPacked | kAudioFormatFlagIsNonInterleaved;
        f.mBytesPerPacket = 4; f.mFramesPerPacket = 1; f.mBytesPerFrame = 4; f.mChannelsPerFrame = 2; f.mBitsPerChannel = 32;
        AudioUnitSetProperty (au, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Output, 0, &f, sizeof f);
        UInt32 mx = BLK; AudioUnitSetProperty (au, kAudioUnitProperty_MaximumFramesPerSlice, kAudioUnitScope_Global, 0, &mx, sizeof mx);
        if (AudioUnitInitialize (au) != noErr) return false;
        UInt32 sz = 0; Boolean w = false;
        AudioUnitGetPropertyInfo (au, kAudioUnitProperty_ParameterList, kAudioUnitScope_Global, 0, &sz, &w);
        std::vector<AudioUnitParameterID> ids (sz / sizeof (AudioUnitParameterID));
        AudioUnitGetProperty (au, kAudioUnitProperty_ParameterList, kAudioUnitScope_Global, 0, ids.data(), &sz);
        for (auto id : ids)
        {
            AudioUnitParameterInfo pi {}; UInt32 s = sizeof pi;
            if (AudioUnitGetProperty (au, kAudioUnitProperty_ParameterInfo, kAudioUnitScope_Global, id, &pi, &s) != noErr) continue;
            char nm[256] = {}; if (pi.cfNameString) CFStringGetCString (pi.cfNameString, nm, sizeof nm, kCFStringEncodingUTF8); else std::strncpy (nm, pi.name, 255);
            byName[nm] = id; info[id] = pi;
        }
        return true;
    }
    bool has (const char* name) const { return byName.find (name) != byName.end(); }
    bool set (const char* name, float norm, bool loud = true)
    {
        auto it = byName.find (name);
        if (it == byName.end()) { if (loud) printf ("    !! no parameter named '%s'\n", name); return false; }
        const auto& pi = info.at (it->second);
        return AudioUnitSetParameter (au, it->second, kAudioUnitScope_Global, 0, pi.minValue + norm * (pi.maxValue - pi.minValue), 0) == noErr;
    }
    // a CHOICE parameter is an index, never a fraction: hand it the raw index
    bool setIdx (const char* name, int idx)
    {
        auto it = byName.find (name); if (it == byName.end()) { printf ("    !! no parameter named '%s'\n", name); return false; }
        return AudioUnitSetParameter (au, it->second, kAudioUnitScope_Global, 0, (float) idx, 0) == noErr;
    }
    void note (int n, int v) { MusicDeviceMIDIEvent (au, v ? 0x90 : 0x80, (UInt32) n, (UInt32) v, 0); }
    void pump (double sec) { const double t0 = CFAbsoluteTimeGetCurrent(); while (CFAbsoluteTimeGetCurrent() - t0 < sec) CFRunLoopRunInMode (kCFRunLoopDefaultMode, 0.01, false); }
    // Renders `blocks` blocks, appending every sample to `out` (L then R per block). The timestamp
    // keeps advancing across calls — an AU that sees sample time go backwards stops rendering.
    void render (int blocks, std::vector<float>* out, bool pumpLoop = true)
    {
        std::vector<float> bl ((size_t) BLK), br ((size_t) BLK);
        AudioBufferList* abl = (AudioBufferList*) calloc (1, sizeof (AudioBufferList) + sizeof (AudioBuffer));
        for (int b = 0; b < blocks; ++b)
        {
            abl->mNumberBuffers = 2;
            abl->mBuffers[0].mNumberChannels = 1; abl->mBuffers[0].mDataByteSize = BLK * 4; abl->mBuffers[0].mData = bl.data();
            abl->mBuffers[1].mNumberChannels = 1; abl->mBuffers[1].mDataByteSize = BLK * 4; abl->mBuffers[1].mData = br.data();
            AudioUnitRenderActionFlags fl = 0; AudioTimeStamp ts {}; ts.mSampleTime = stamp; ts.mFlags = kAudioTimeStampSampleTimeValid;
            AudioUnitRender (au, &fl, &ts, 0, BLK, abl);
            stamp += BLK;
            if (out != nullptr) { out->insert (out->end(), bl.begin(), bl.end()); out->insert (out->end(), br.begin(), br.end()); }
            if (pumpLoop) CFRunLoopRunInMode (kCFRunLoopDefaultMode, 0.004, false);
        }
        free (abl);
    }
    void close() { if (au) { AudioUnitUninitialize (au); AudioComponentInstanceDispose (au); au = nullptr; } }
};
static double rmsDb (const std::vector<float>& v, size_t from = 0)
{ double e = 0; size_t n = 0; for (size_t i = from; i < v.size(); ++i) { e += (double) v[i] * v[i]; ++n; } return n ? 20.0 * std::log10 (std::sqrt (e / (double) n) + 1e-12) : -240.0; }
static double diffDb (const std::vector<float>& a, const std::vector<float>& b)
{ if (a.size() != b.size() || a.empty()) return 0.0; double e = 0; for (size_t i = 0; i < a.size(); ++i) { const double d = (double) a[i] - b[i]; e += d * d; } return 20.0 * std::log10 (std::sqrt (e / (double) a.size()) + 1e-12); }


static double hfRatio (const std::vector<float>& v, size_t from)
{   // energy of the first difference over the energy of the signal — a cheap brightness number
    double e = 0, d = 0; for (size_t i = from + 1; i < v.size(); ++i) { const double x = v[i], p = v[i - 1]; e += x * x; d += (x - p) * (x - p); }
    return e > 0 ? std::sqrt (d / e) : 0.0;
}
static std::vector<float> leftOnly (const std::vector<float>& lr)
{   // render() appends L block then R block; keep L
    std::vector<float> L; for (size_t i = 0; i + BLK <= lr.size(); i += 2 * BLK) L.insert (L.end(), lr.begin() + (long) i, lr.begin() + (long) (i + BLK)); return L;
}
static double gapDepthDb (const std::vector<float>& L, size_t from, size_t win, double* maxDb = nullptr, double* minDb = nullptr)
{
    double mx = -240, mn = 0; bool any = false;
    for (size_t i = from; i + win <= L.size(); i += win)
    {
        double e = 0; for (size_t k = 0; k < win; ++k) e += (double) L[i + k] * L[i + k];
        const double db = 20.0 * std::log10 (std::sqrt (e / (double) win) + 1e-12);
        if (! any) { mx = mn = db; any = true; } else { mx = std::max (mx, db); mn = std::min (mn, db); }
    }
    if (maxDb) *maxDb = mx; if (minDb) *minDb = mn; return mx - mn;
}
struct Tap { bool direct; bool inFilter; bool cutOut; };
// osc A (bank 0) or E (bank 1) alone, into Utility 1 by its pill, through / around the main filter, tapped or not
static std::vector<float> renderTap (bool bankB, Tap t)
{
    Au a; if (! a.open()) { printf ("no AU\n"); exit (2); }
    const char* send = bankB ? "Synth OSC E Filter 1 Send" : "Synth OSC A Filter 1 Send";
    const char* src  = bankB ? "Utility SRC_E" : "Utility SRC_A";
    if (bankB) { a.set ("Osc E Enable", 1.0f); a.set ("Synth OSC E Level", 0.5f); a.set ("Osc A Enable", 0.0f); a.pump (0.6); }   // bank B is built lazily on the message thread
    a.set (send, t.inFilter ? 1.0f : 0.0f);
    a.setIdx ("Synth Filter 1 Type", 0);   // the shipped default is NONE (27); type 0 is a low-pass
    a.set ("Synth Filter 1 Cutoff", 0.06f);
    if (t.cutOut) a.set (bankB ? "Synth OSC E Out" : "Synth OSC A Out", 0.0f);
    a.set ("Utility In Chain", 1.0f); a.set ("Utility Power", 1.0f); a.set ("Utility Chain Rank", 0.5f);
    a.set (src, 1.0f);
    const char* taps = a.has ("Utility Direct Taps") ? "Utility Direct Taps" : "Utility 1 Direct Taps";
    a.set (taps, t.direct ? (float) (1u << (bankB ? 6 : 0)) / 2047.0f : 0.0f);
    a.pump (0.8); a.render (4, nullptr);   // the pooled send pair is built by the timer; the tp19 law bypasses until it is
    std::vector<float> out; a.note (48, 100); a.render (48, &out); a.close();
    return leftOnly (out);
}
static void tapChecks (bool bankB)
{
    const char* who = bankB ? "E (bank B, bit 6)" : "A (bank 0, bit 0)";
    auto dark   = renderTap (bankB, { false, true,  false });
    auto direct = renderTap (bankB, { true,  true,  false });
    auto raw    = renderTap (bankB, { false, false, false });
    const size_t from = 4800;
    const double ld = rmsDb (dark, from), lr = rmsDb (direct, from), lw = rmsDb (raw, from), nul = diffDb (direct, raw);
    char d[300];
    snprintf (d, sizeof d, "%s: post-filter tap %.1f dBFS, direct tap %.1f dBFS, not in the filter %.1f dBFS, direct-vs-raw residual %.1f dB", who, ld, lr, lw, nul);
    chk (lw > -30.0 && lr > -30.0, "[TAP] the oscillator reaches the output through the rack", d);
    chk (ld < lr - 30.0, "[TAP] the post-filter tap hears the CLOSED filter; the direct tap does not", d);
    // the first 100 ms carry a -100 dB onset residual (the per-source filter-send glide, fb79); after it the two are the same floats
    std::vector<float> ds (direct.begin() + 9600, direct.end()), rs (raw.begin() + 9600, raw.end());
    const double steady = diffDb (ds, rs);
    snprintf (d, sizeof d, "%s: whole render %.1f dB, after 0.2 s %.1f dB", who, nul, steady);
    chk (steady < -150.0, "[TAP] the direct tap IS the raw oscillator (nulls against not being in the filter, after the onset)", d);
    if (bankB)
    {
        auto viaRack = renderTap (true, { false, false, true });   // E's output cable CUT: only the rack can carry it
        snprintf (d, sizeof d, "E with its output cable cut, routed into Utility 1: %.1f dBFS (was -240 before the bank-B pairs were built)", rmsDb (viaRack, from));
        chk (rmsDb (viaRack, from) > -30.0, "[BANK B] E really goes THROUGH a rack device (its send pair exists)", d);
    }
}
static void probe()
{
    auto run = [] (const char* label, bool bankB, bool inFilter, bool rack, bool direct, bool pumpLong)
    {
        Au a; if (! a.open()) { printf ("no AU\n"); exit (2); }
        const char* send = bankB ? "Synth OSC E Filter 1 Send" : "Synth OSC A Filter 1 Send";
        const char* src  = bankB ? "Utility SRC_E" : "Utility SRC_A";
        if (bankB) { a.set ("Osc E Enable", 1.0f); a.set ("Synth OSC E Level", 0.5f); a.set ("Osc A Enable", 0.0f); a.pump (0.6); }
        a.set (send, inFilter ? 1.0f : 0.0f);
        a.setIdx ("Synth Filter 1 Type", 0); a.set ("Synth Filter 1 Cutoff", 0.06f);
        if (rack)
        {
            a.set ("Utility In Chain", 1.0f); a.set ("Utility Power", 1.0f); a.set ("Utility Chain Rank", 0.5f); a.set (src, 1.0f);
            const char* taps = a.has ("Utility Direct Taps") ? "Utility Direct Taps" : "Utility 1 Direct Taps";
            a.set (taps, direct ? (float) (1u << (bankB ? 6 : 0)) / 2047.0f : 0.0f);
        }
        a.pump (pumpLong ? 1.0 : 0.35); a.render (4, nullptr);
        std::vector<float> out; a.note (48, 100); a.render (48, &out); a.close();
        auto L = leftOnly (out);
        printf ("  %-44s  rms %.1f dBFS  (first 0.1 s %.1f, last 0.1 s %.1f)  bright %.4f\n", label, rmsDb (L, 4800), rmsDb (L, 0) , rmsDb (std::vector<float> (L.end() - 4800, L.end()), 0), hfRatio (L, 4800));
    };
    if (getenv ("PROBE2"))
    {
        auto rend = [] (bool bankB, bool inFilter, bool rack, bool direct, bool cutOut) -> std::vector<float>
        {
            Au a; if (! a.open()) { printf ("no AU\n"); exit (2); }
            const char* send = bankB ? "Synth OSC E Filter 1 Send" : "Synth OSC A Filter 1 Send";
            const char* src  = bankB ? "Utility SRC_E" : "Utility SRC_A";
            if (bankB) { a.set ("Osc E Enable", 1.0f); a.set ("Synth OSC E Level", 0.5f); a.set ("Osc A Enable", 0.0f); a.pump (0.6); }
            a.set (send, inFilter ? 1.0f : 0.0f);
            a.setIdx ("Synth Filter 1 Type", 0); a.set ("Synth Filter 1 Cutoff", 0.06f);
            if (cutOut) a.set (bankB ? "Synth OSC E Out" : "Synth OSC A Out", 0.0f);
            if (rack)
            {
                a.set ("Utility In Chain", 1.0f); a.set ("Utility Power", 1.0f); a.set ("Utility Chain Rank", 0.5f); a.set (src, 1.0f);
                const char* taps = a.has ("Utility Direct Taps") ? "Utility Direct Taps" : "Utility 1 Direct Taps";
                a.set (taps, direct ? (float) (1u << (bankB ? 6 : 0)) / 2047.0f : 0.0f);
            }
            a.pump (getenv ("PUMP") ? atof (getenv ("PUMP")) : 0.6); a.render (4, nullptr);
            std::vector<float> out; a.note (48, 100); a.render (48, &out); a.close();
            return leftOnly (out);
        };
        if (getenv ("EONLY"))
        {
            auto eCutRack = rend (true, false, true, false, true), eTap = rend (true, true, true, true, true);
            printf ("pump %s: E out cut + rack %.1f dBFS   E in filter + rack + tap (out cut) %.1f dBFS\n", getenv ("PUMP") ? getenv ("PUMP") : "0.6", rmsDb (eCutRack, 4800), rmsDb (eTap, 4800));
            return;
        }
        auto direct = rend (false, true, true, true, false), raw = rend (false, false, true, false, false);
        printf ("A: direct vs raw residual per 0.1 s:");
        for (size_t w = 0; w + 4800 <= direct.size(); w += 4800)
        { std::vector<float> x (direct.begin() + (long) w, direct.begin() + (long) (w + 4800)), y (raw.begin() + (long) w, raw.begin() + (long) (w + 4800)); printf (" %.0f", diffDb (x, y)); }
        printf ("\n");
        auto eCutRack   = rend (true, false, true, false, true);
        auto eCutNoRack = rend (true, false, false, false, true);
        auto eCutTap    = rend (true, true,  true, true,  true);
        printf ("E out cut: no rack %.1f dBFS   rack (no filter) %.1f dBFS   in filter + rack + tap %.1f dBFS\n", rmsDb (eCutNoRack, 4800), rmsDb (eCutRack, 4800), rmsDb (eCutTap, 4800));
        auto aCutRack   = rend (false, false, true, false, true);
        auto aCutNoRack = rend (false, false, false, false, true);
        printf ("A out cut: no rack %.1f dBFS   rack (no filter) %.1f dBFS\n", rmsDb (aCutNoRack, 4800), rmsDb (aCutRack, 4800));
        return;
    }
    printf ("bank 0 (A)\n");
    run ("A, no filter, no rack",           false, false, false, false, false);
    run ("A in filter, no rack",            false, true,  false, false, false);
    run ("A, no filter, rack",              false, false, true,  false, false);
    run ("A in filter, rack, taps 0",       false, true,  true,  false, false);
    run ("A in filter, rack, taps A",       false, true,  true,  true,  false);
    run ("A, no filter, rack (pump 1 s)",   false, false, true,  false, true);
    run ("A in filter, rack, taps A (1 s)", false, true,  true,  true,  true);
    printf ("bank 1 (E)\n");
    run ("E, no filter, no rack",           true,  false, false, false, false);
    run ("E in filter, no rack",            true,  true,  false, false, false);
    run ("E, no filter, rack",              true,  false, true,  false, false);
    run ("E in filter, rack, taps 0",       true,  true,  true,  false, false);
    run ("E in filter, rack, taps E",       true,  true,  true,  true,  false);
}
int main (int argc, char** argv)
{
    if (argc > 1 && ! std::strcmp (argv[1], "probe")) { probe(); return 0; }
    printf ("tp41 — the Patcher's rules, on the installed AU\n");
    { Au a; if (! a.open()) { printf ("no AU\n"); return 2; }
      chk ((a.has ("Utility Direct Taps") || a.has ("Utility 1 Direct Taps")) && a.has ("Flow Chop In Rack") && a.has ("Flow Chop Chain Rank"), "the tp41 parameters reached the AU");
      a.close(); }
    tapChecks (false);
    tapChecks (true);
    // ── [INLINE] the Chop's holes, with the reverb before (old law) and after (the Patcher's) ──
    auto renderChop = [] (bool inline_, bool reverb, float mix) -> std::vector<float>
    {
        Au a; if (! a.open()) { printf ("no AU\n"); exit (2); }
        a.setIdx ("Flow Chain 1", 2);
        for (const char* p : { "Flow Chop Src B", "Flow Chop Src C", "Flow Chop Src D", "Flow Chop Src Sub", "Flow Chop Src Noise",
                               "Flow Chop Src E", "Flow Chop Src F", "Flow Chop Src G", "Flow Chop Src H" }) a.set (p, 0.0f);
        a.set ("Chop Blend", 1.0f); a.set ("Chop Gate", 0.0f); a.set ("Chop Rate", 0.55f);
        if (reverb)
        {
            a.set ("Reverb In Chain", 1.0f); a.set ("Reverb Power", 1.0f); a.set ("Reverb Chain Rank", 0.5f);
            a.set ("SYN_RVB_SRC_A", 1.0f); a.set ("Reverb Mix", mix); a.set ("Reverb Decay", 0.92f); a.set ("Reverb Size", 0.8f);
        }
        a.set ("Flow Chop In Rack", inline_ ? 1.0f : 0.0f); a.set ("Flow Chop Chain Rank", 0.05f);
        a.pump (0.35); a.render (4, nullptr);
        std::vector<float> out; a.note (48, 100); a.render (280, &out); a.close();
        return leftOnly (out);
    };
    {
        auto before = renderChop (false, true, 1.0f);
        auto after  = renderChop (true,  true, 1.0f);
        const size_t from = 48000, win = 960;
        double bMax, bMin, aMax, aMin;
        const double gb = gapDepthDb (before, from, win, &bMax, &bMin), ga = gapDepthDb (after, from, win, &aMax, &aMin);
        char d[256];
        snprintf (d, sizeof d, "reverb BEFORE the chop: windows %.1f..%.1f dB (depth %.1f)   reverb AFTER: %.1f..%.1f dB (depth %.1f)", bMax, bMin, gb, aMax, aMin, ga);
        chk (gb > 25.0, "[INLINE] the Chop really cuts holes (reverb before it, the old law)", d);
        chk (rmsDb (after, from) > -40.0, "[INLINE] the reverb after the Chop is audible", d);
        chk (ga < gb - 12.0, "[INLINE] the reverb AFTER the Chop fills its holes with tail", d);
    }
    {
        auto alone = renderChop (true, false, 1.0f);
        auto pass  = renderChop (true, true,  0.0f);
        const size_t from = 48000;
        char d[256];
        snprintf (d, sizeof d, "chop alone %.1f dBFS, chop -> mix-0 reverb %.1f dBFS, residual %.1f dB", rmsDb (alone, from), rmsDb (pass, from), diffDb (alone, pass));
        chk (std::abs (rmsDb (alone, from) - rmsDb (pass, from)) < 1.0, "[PASS] a device after the flow card passes the signal ONCE (level within 1 dB)", d);
    }
    // ── [NOISE 2] the second noise: its own switch, its own pill (bit 10), its own cable, its own knobs ──
    {
        auto rend = [] (bool n2on, bool routeN2, bool cutOut, float level, bool n1on) -> std::vector<float>
        {
            Au a; if (! a.open()) { printf ("no AU\n"); exit (2); }
            a.set ("Osc A Enable", 0.0f);
            a.set ("Noise On", n1on ? 1.0f : 0.0f);
            a.set ("Noise 2 On", n2on ? 1.0f : 0.0f); a.set ("Noise 2 Level", level); a.pump (0.6);   // the bank builds on the message thread
            if (routeN2) { a.set ("Utility In Chain", 1.0f); a.set ("Utility Power", 1.0f); a.set ("Utility Chain Rank", 0.5f); a.set ("Utility SRC_N2", 1.0f); }
            if (cutOut) a.set ("Synth Noise 2 Out", 0.0f);
            a.pump (0.8); a.render (4, nullptr);
            std::vector<float> out; a.note (48, 100); a.render (48, &out); a.close();
            return leftOnly (out);
        };
        const size_t from = 4800;
        auto off   = rend (false, false, false, 0.5f, false);
        auto on    = rend (true,  false, false, 0.5f, false);
        auto quiet = rend (true,  false, false, 0.0f, false);
        auto cut   = rend (true,  false, true,  0.5f, false);
        auto via   = rend (true,  true,  true,  0.5f, false);
        char d[300];
        snprintf (d, sizeof d, "off %.1f  on %.1f  level 0 %.1f  cable cut %.1f  cut but routed into Utility %.1f dBFS", rmsDb (off, from), rmsDb (on, from), rmsDb (quiet, from), rmsDb (cut, from), rmsDb (via, from));
        chk (rmsDb (off, from) < -100.0 && rmsDb (on, from) > -40.0, "[NOISE 2] Noise 2 On makes sound on its own (no oscillator, Noise 1 off)", d);
        chk (rmsDb (quiet, from) < rmsDb (on, from) - 40.0, "[NOISE 2] its own Level knob is its own", d);
        chk (rmsDb (cut, from) < -100.0, "[NOISE 2] its own Out cable: cut = silent", d);
        chk (rmsDb (via, from) > -40.0, "[NOISE 2] cut, but routed into a Utility by the N2 pill: audible THROUGH the rack (bit 10)", d);
        auto n1 = rend (false, false, false, 0.5f, true);
        auto both = rend (true, false, false, 0.5f, true);
        snprintf (d, sizeof d, "Noise 1 alone %.1f  both %.1f dBFS", rmsDb (n1, from), rmsDb (both, from));
        chk (rmsDb (n1, from) > -40.0 && rmsDb (both, from) > rmsDb (n1, from) + 1.0, "[NOISE 2] the two noises add (Noise 1 untouched, Noise 2 on top)", d);
    }
    printf ("\n  PASS %d   FAIL %d\n", npass, nfail);
    return nfail ? 1 : 0;
}
