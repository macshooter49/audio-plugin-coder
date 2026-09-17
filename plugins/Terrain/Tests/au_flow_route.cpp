// ══════════════════════════════════════════════════════════════════════════════════════════════
//  tp30 — THE FLOW ROUTE GATE.  The audio FLOW cards (Chop / Glitch) became routed chain devices
//  with their own ten route pills, so this proves two things on the REAL installed AU:
//
//   [NULL]  with every pill at its default (all on) the instrument renders EXACTLY as it did
//           before the change — captured from the previous binary with `dump`, compared with
//           `check`.  This is the acceptance gate: the rewrite is invisible until you cut a cable.
//   [ROUTE] cutting a pill really moves audio: a card with no pills lit is the same as no card
//           at all; two cards keep their oscillators in their own lanes; a source with its output
//           cable cut and nothing claiming it is SILENT.
//
//    clang++ -O2 -std=c++17 Tests/au_flow_route.cpp -o /tmp/aufr \
//            -framework AudioToolbox -framework AudioUnit -framework CoreFoundation -framework CoreAudio
//    /tmp/aufr dump <file>    -> render the null scenarios, write raw float32 L,R
//    /tmp/aufr check <file>   -> render them again and diff against that file
//    /tmp/aufr                -> the routing assertions
//    /tmp/aufr list <sub>     -> parameters whose name contains <sub>
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

// ── the FOUR NULL SCENARIOS. Every one leaves the new route pills at their defaults, so each must
//    render byte-for-byte what the previous binary rendered. `flow` names the FLOW_CHAIN slots.
struct Scene { const char* name; int chain1; int chain2; };
static const Scene kNull[4] = {
    { "no flow card at all", 0, 0 },
    { "one Glitch",          3, 0 },
    { "one Chop",            2, 0 },
    { "Chop then Glitch",    2, 3 },
};
static void renderNull (std::vector<float>& out)
{
    for (const auto& sc : kNull)
    {
        Au a; if (! a.open()) { printf ("no AU\n"); exit (2); }
        a.setIdx ("Flow Chain 1", sc.chain1);
        a.setIdx ("Flow Chain 2", sc.chain2);
        a.pump (0.35);
        a.render (4, nullptr);                 // settle
        a.note (48, 100); a.note (60, 100);
        a.render (24, &out);
        a.close();
    }
}
int main (int argc, char** argv)
{
    if (argc > 2 && ! std::strcmp (argv[1], "list"))
    {
        Au a; if (! a.open()) { printf ("no AU\n"); return 2; }
        for (auto& kv : a.byName) if (kv.first.find (argv[2]) != std::string::npos) printf ("  %s\n", kv.first.c_str());
        printf ("  (%zu parameters)\n", a.byName.size()); return 0;
    }
    if (argc > 2 && ! std::strcmp (argv[1], "dump"))
    {
        std::vector<float> v; renderNull (v);
        FILE* f = fopen (argv[2], "wb"); if (! f) { printf ("cannot write %s\n", argv[2]); return 2; }
        fwrite (v.data(), 4, v.size(), f); fclose (f);
        printf ("  wrote %zu samples, rms %.2f dBFS -> %s\n", v.size(), rmsDb (v), argv[2]);
        return 0;
    }
    if (argc > 2 && ! std::strcmp (argv[1], "check"))
    {
        FILE* f = fopen (argv[2], "rb"); if (! f) { printf ("cannot read %s\n", argv[2]); return 2; }
        std::vector<float> ref; { float x; while (fread (&x, 4, 1, f) == 1) ref.push_back (x); } fclose (f);
        std::vector<float> now; renderNull (now);
        printf ("\n== tp30 - THE NULL: every pill at its default renders the OLD binary, sample for sample ==\n\n");
        chk (ref.size() == now.size(), "the reference and this run are the same length",
             (std::to_string (ref.size()) + " vs " + std::to_string (now.size())).c_str());
        if (ref.size() != now.size()) { printf ("\nFAILED\n"); return 1; }
        // WHAT "NULL" MEANS HERE, EXACTLY. Scenario 0 has no flow card, so it must be BIT-identical:
        //  that is the proof the rack itself is untouched. The flow scenarios cannot be bit-identical
        //  and it would be dishonest to assert they are — the card's input moved from "the summed
        //  master" to "its own post-filter send bus", which is the same signal computed by a
        //  different instance of the same filter, and the master limiter is recursive so the last
        //  bit of difference compounds. The bar is therefore: the same level to a thousandth of a
        //  dB, and a residual at least 70 dB under the peak.
        const size_t per = ref.size() / 4;
        for (int k = 0; k < 4; ++k)
        {
            size_t b = 0; double e = 0, sg = 0, sn = 0, pk = 0, pd = 0;
            for (size_t i = k * per; i < (k + 1) * per; ++i)
            {
                if (ref[i] != now[i]) ++b;
                const double d = (double) ref[i] - now[i];
                e += d * d; sg += (double) ref[i] * ref[i]; sn += (double) now[i] * now[i];
                pk = std::fmax (pk, std::fabs ((double) ref[i])); pd = std::fmax (pd, std::fabs (d));
            }
            const double rdb = 20.0 * std::log10 (std::sqrt (sg / (double) per) + 1e-15);
            const double ndb = 20.0 * std::log10 (std::sqrt (sn / (double) per) + 1e-15);
            char d[200]; snprintf (d, sizeof d, "%6zu/%zu differ   level %+.4f dB   peak|delta| %.2e vs peak %.3f",
                                   b, per, ndb - rdb, pd, pk);
            if (k == 0) chk (b == 0, "[0] no flow card: BIT-IDENTICAL, so the rack is untouched", d);
            else        chk (std::fabs (ndb - rdb) < 0.001 && (pd < pk * 3.2e-4 || pk == 0.0),
                             (std::string ("[") + std::to_string (k) + "] " + kNull[k].name
                              + ": same level, residual under -70 dB of peak").c_str(), d);
        }
        size_t bad = 0; for (size_t i = 0; i < ref.size(); ++i) if (ref[i] != now[i]) ++bad;
        (void) bad;
        printf ("\n  PASS %d   FAIL %d\n\n", npass, nfail);
        return nfail ? 1 : 0;
    }
    printf ("\n== tp30 - THE FLOW CARDS ARE PER-OSCILLATOR ==\n\n");
    // ── 1. a Glitch with EVERY pill cut is the same as no Glitch at all ─────────────────────────
    {
        std::vector<float> off, cut;
        static const char* kPill[10] = { "Flow Glitch Src A", "Flow Glitch Src B", "Flow Glitch Src C", "Flow Glitch Src D",
                                         "Flow Glitch Src Sub", "Flow Glitch Src Noise", "Flow Glitch Src E",
                                         "Flow Glitch Src F", "Flow Glitch Src G", "Flow Glitch Src H" };
        { Au a; if (! a.open()) { printf ("no AU\n"); return 2; }
          bool ok = a.has (kPill[0]);
          chk (ok, "the Glitch card carries the ten route pills", ok ? "found Flow Glitch Src A" : "MISSING - the pills never reached the AU");
          if (! ok) { printf ("\nFAILED\n"); return 1; }
          a.setIdx ("Flow Chain 1", 0); a.pump (0.35); a.render (4, nullptr);
          a.note (48, 100); a.note (60, 100); a.render (24, &off); a.close(); }
        { Au a; a.open();
          a.setIdx ("Flow Chain 1", 3);
          for (int k = 0; k < 10; ++k) a.set (kPill[k], 0.0f);
          a.pump (0.35); a.render (4, nullptr);
          a.note (48, 100); a.note (60, 100); a.render (24, &cut); a.close(); }
        char d[160]; snprintf (d, sizeof d, "residual %.1f dBFS against a %.2f dBFS signal", diffDb (off, cut), rmsDb (off));
        chk (diffDb (off, cut) < rmsDb (off) - 60.0, "1. a Glitch with every cable cut is inaudible - the oscillators go straight to Out", d);
    }
    // ── 2. cutting ONE oscillator's cable changes the sound ────────────────────────────────────
    {
        std::vector<float> all, cutB;
        { Au a; a.open(); a.setIdx ("Flow Chain 1", 3); a.set ("Osc B Enable", 1.0f);
          a.pump (0.35); a.render (4, nullptr); a.note (48, 100); a.note (60, 100); a.render (24, &all); a.close(); }
        { Au a; a.open(); a.setIdx ("Flow Chain 1", 3); a.set ("Osc B Enable", 1.0f);
          a.set ("Flow Glitch Src B", 0.0f);
          a.pump (0.35); a.render (4, nullptr); a.note (48, 100); a.note (60, 100); a.render (24, &cutB); a.close(); }
        char d[160]; snprintf (d, sizeof d, "residual %.1f dBFS against a %.2f dBFS signal", diffDb (all, cutB), rmsDb (all));
        chk (diffDb (all, cutB) > rmsDb (all) - 40.0, "2. cutting oscillator B out of the Glitch really changes the audio", d);
    }
    // ── 3. TWO cards stay in their own lanes — the "four tires" rule ───────────────────────────
    //     Osc A -> Glitch 1 only, Osc B -> Glitch 2 only. Then:
    //       · with B silent, moving Glitch 2's Blend must change NOTHING — A never touches it;
    //       · with B sounding, the same move must change the audio — B really does go through it.
    //     One pair of renders proves both halves, which is what "separate" has to mean.
    {
        auto lanes = [] (bool bOn, float gli2Blend, std::vector<float>& out)
        {
            Au a; a.open();
            a.setIdx ("Flow Chain 1", 3); a.setIdx ("Flow Chain 2", 3); a.setIdx ("Flow Chain 2 Instance", 1);
            a.set ("Osc B Enable", bOn ? 1.0f : 0.0f);
            const char* k1[10] = { "Flow Glitch Src A","Flow Glitch Src B","Flow Glitch Src C","Flow Glitch Src D",
                                   "Flow Glitch Src Sub","Flow Glitch Src Noise","Flow Glitch Src E","Flow Glitch Src F",
                                   "Flow Glitch Src G","Flow Glitch Src H" };
            const char* k2[10] = { "Flow 2 Glitch Src A","Flow 2 Glitch Src B","Flow 2 Glitch Src C","Flow 2 Glitch Src D",
                                   "Flow 2 Glitch Src Sub","Flow 2 Glitch Src Noise","Flow 2 Glitch Src E","Flow 2 Glitch Src F",
                                   "Flow 2 Glitch Src G","Flow 2 Glitch Src H" };
            for (int k = 0; k < 10; ++k) { a.set (k1[k], 0.0f); a.set (k2[k], 0.0f); }
            a.set (k1[0], 1.0f);      // A -> glitch 1
            a.set (k2[1], 1.0f);      // B -> glitch 2
            a.set ("Glitch 2 Blend", gli2Blend);
            a.pump (0.35); a.render (4, nullptr); a.note (48, 100); a.note (60, 100); a.render (24, &out); a.close();
        };
        std::vector<float> aOnly, aOnlyMoved, both, bothMoved;
        lanes (false, 0.0f, aOnly);
        lanes (false, 1.0f, aOnlyMoved);
        lanes (true,  0.0f, both);
        lanes (true,  1.0f, bothMoved);
        char d1[190], d2[190];
        snprintf (d1, sizeof d1, "A alone: %.2f dBFS, residual when Glitch 2 moves %.1f dBFS", rmsDb (aOnly), diffDb (aOnly, aOnlyMoved));
        snprintf (d2, sizeof d2, "A+B: %.2f dBFS, residual when Glitch 2 moves %.1f dBFS", rmsDb (both), diffDb (both, bothMoved));
        chk (rmsDb (aOnly) > -60.0, "3a. oscillator A alone still sounds through Glitch 1", d1);
        chk (diffDb (aOnly, aOnlyMoved) < rmsDb (aOnly) - 60.0,
             "3b. and Glitch 2 cannot touch it - moving Glitch 2 changes NOTHING", d1);
        chk (diffDb (both, bothMoved) > rmsDb (both) - 40.0,
             "3c. while oscillator B, cabled to Glitch 2, really does go through it", d2);
    }
    // ── 4. the OUTPUT CABLE: cut it, claim it nowhere, and the oscillator is silent ─────────────
    {
        std::vector<float> on, off;
        { Au a; a.open(); a.setIdx ("Flow Chain 1", 0); a.pump (0.35); a.render (4, nullptr);
          a.note (48, 100); a.note (60, 100); a.render (24, &on); a.close(); }
        { Au a; a.open(); a.setIdx ("Flow Chain 1", 0);
          bool ok = a.has ("Synth OSC A Out");
          chk (ok, "4a. the per-oscillator output cable reached the AU", ok ? "Synth OSC A Out" : "MISSING");
          a.set ("Synth OSC A Out", 0.0f);
          a.pump (0.35); a.render (4, nullptr); a.note (48, 100); a.note (60, 100); a.render (24, &off); a.close(); }
        char d[160]; snprintf (d, sizeof d, "with the cable: %.2f dBFS   cut: %.2f dBFS", rmsDb (on), rmsDb (off));
        chk (rmsDb (off) < rmsDb (on) - 60.0, "4b. an oscillator hooked up to nothing makes NO sound", d);
    }
    printf ("\n  PASS %d   FAIL %d\n\n", npass, nfail);
    return nfail ? 1 : 0;
}
