// ══════════════════════════════════════════════════════════════════════════════════════════════
//  harm_table_au.cpp — fb588: ADDITIVE SYNTHESIS FROM THE WAVETABLES, ON THE INSTALLED PLUGIN.
//
//    clang++ -std=c++17 -O2 Tests/harm_table_au.cpp -o /tmp/harm_table_au \
//        -framework AudioToolbox -framework CoreFoundation -framework CoreAudio && /tmp/harm_table_au
//
//  Max: "implement our wave table presets and the same presets that we already have, the same menu
//  for harmonics mode. I want to be able to actually have additive synthesis with the wave tables...
//  get REAL RESULTS."
//
//  The offline cert (Tests/harm_table_cert.cpp) proves the ENGINE. This proves the SHIPPING AU —
//  parameters, menu wiring, threading and all — because a bank that only works in a test harness
//  is not a feature.
//
//  THE BARS
//   0  the AU actually offers a SEVENTH harmonic family
//   1  IT SOUNDS — Table is not a silent option (case 6 renders silence on a null table, so this
//      is the bar that catches a bake lane that never published)
//   2  IT SOUNDS LIKE THE TABLE — HARM/Table on a given wavetable lands far closer to the WAVETABLE
//      ENGINE on that same table than to any of the six procedural families. This is the feature.
//   3  THE MENU DRIVES IT — changing the wavetable preset changes the additive sound. "The same
//      menu" is a claim about wiring, and this is the measurement of it.
//   4  HUE SCANS THE TABLE — the frame axis is live, and modulatable, because it is read on the
//      audio thread after the mod matrix
//   5  THE SIX FAMILIES ARE UNHARMED — each still sounds, and each is still ITSELF
//   6  NO CLICKS sweeping HUE under a held note (the frame scan crosses all 16 frames)
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
{ if (ok) ++pass; else ++fail; std::printf ("  %s  %-58s %s\n", ok ? "ok  " : "FAIL", label, detail.c_str()); }

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
    bool has (const std::string& n) const { return byName.count (n) != 0; }
    float maxOf (const std::string& n) { auto it = byName.find (n); return it == byName.end() ? -1.f : info.at (it->second).maxValue; }
    // absolute (not normalised) set — choice params are indices
    bool setAbs (const std::string& n, float v)
    { auto it = byName.find (n); if (it == byName.end()) return false;
      return AudioUnitSetParameter (au, it->second, kAudioUnitScope_Global, 0, v, 0) == noErr; }
    bool set (const std::string& n, float nv)
    { auto it = byName.find (n); if (it == byName.end()) return false;
      const auto& pi = info.at (it->second);
      return AudioUnitSetParameter (au, it->second, kAudioUnitScope_Global, 0, pi.minValue + nv * (pi.maxValue - pi.minValue), 0) == noErr; }
    void pump (double seconds) { double t = 0; while (t < seconds) { CFRunLoopRunInMode (kCFRunLoopDefaultMode, 0.02, false); t += 0.02; } }
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
    // ⚠️ the bake runs on the MESSAGE thread at 60 Hz, so a parameter change needs a real
    //    run-loop pump before the note — not just more audio blocks.
    std::vector<float> note (int nn) { pump (0.30); midi (0x90, (UInt32) nn, 100); render (10); auto b = render (26); midi (0x80, (UInt32) nn, 0); render (30); return b; }
};

static double rmsDb (const std::vector<float>& v, size_t from = 0)
{ double s = 0; size_t n = 0; for (size_t i = from; i < v.size(); ++i) { s += (double) v[i]*v[i]; ++n; }
  return 10.0 * std::log10 (std::max (1e-20, s / std::max<size_t> (1, n))); }

// amplitude-weighted magnitude-spectrum distance in dB — the project's "hearing" metric
static std::vector<double> spec (const std::vector<float>& v)
{
    const int N = 4096, BINS = 400;
    std::vector<double> m ((size_t) BINS + 1, 0.0);
    if (v.size() < (size_t) N + 2048) return m;
    const size_t off = 2048;
    for (int k = 1; k <= BINS; ++k)
    { double re = 0, im = 0; const double w = 2.0 * M_PI * k / N;
      for (int i = 0; i < N; ++i)
      { const double win = 0.35875 - 0.48829*std::cos(2*M_PI*i/(N-1)) + 0.14128*std::cos(4*M_PI*i/(N-1)) - 0.01168*std::cos(6*M_PI*i/(N-1));
        const double x = v[off + (size_t) i] * win; re += x*std::cos(w*i); im += x*std::sin(w*i); }
      m[(size_t) k] = std::sqrt (re*re + im*im); }
    return m;
}
static double dist (const std::vector<double>& a, const std::vector<double>& b)
{
    double pa=0,pb=0; for (size_t k=1;k<a.size();++k){pa=std::max(pa,a[k]);pb=std::max(pb,b[k]);}
    if (pa<=0||pb<=0) return 999.0; double num=0,den=0;
    for (size_t k=1;k<a.size();++k)
    { const double na=a[k]/pa, nb=b[k]/pb, w=std::max(na,nb);
      if (w<1e-3) continue;
      const double d=20*std::log10(std::max(na,1e-3))-20*std::log10(std::max(nb,1e-3));
      num+=w*d*d; den+=w; }
    return den>0? std::sqrt(num/den):0.0;
}

static const char* ENG   = "Synth OSC A Engine";
static const char* HMODE = "Synth OSC A Harmonic Mode";
static const char* HUE   = "Synth OSC A Harmonic Hue";
static const char* WTPRE = "Synth OSC A Wavetable Preset";

int main()
{
    std::printf ("\n══ harm_table_au — fb588 ══  additive synthesis from the wavetables, on the INSTALLED AU\n\n");
    AU a; if (! a.open()) return 1;

    // find the wavetable-preset parameter by fuzzy name (it has never needed one before)
    std::string wtName;
    for (auto& kv : a.byName)
        if (kv.first.find ("OSC A") != std::string::npos
            && (kv.first.find ("Wavetable") != std::string::npos || kv.first.find ("WT Preset") != std::string::npos)
            && kv.first.find ("Preset") != std::string::npos) { wtName = kv.first; break; }
    if (wtName.empty()) wtName = WTPRE;

    // ── 0 · a seventh family exists ───────────────────────────────────────────────────────────
    const float mx = a.maxOf (HMODE);
    { char b[160]; std::snprintf (b, sizeof b, "'%s' max index = %.0f (6 = seven choices; was 5)", HMODE, mx);
      chk (mx == 6.0f, "[0] THE AU OFFERS A SEVENTH HARMONIC FAMILY", b); }

    a.setAbs (ENG, 5);                      // HARM
    a.set ("Synth OSC A Level", 1.0f);
    a.setAbs (HMODE, 6);                    // Table
    a.setAbs (wtName.c_str(), 4);           // ProphetSaw
    a.set (HUE, 0.35f);
    const auto table = a.note (57);
    const double dbT = rmsDb (table, 2048);

    // ── 1 · it sounds ─────────────────────────────────────────────────────────────────────────
    { char b[160]; std::snprintf (b, sizeof b, "HARM/Table on a wavetable renders %.1f dB RMS", dbT);
      chk (dbT > -50.0, "[1] IT SOUNDS — Table is not a silent option", b); }

    // ── 2 · it sounds like the table ──────────────────────────────────────────────────────────
    a.setAbs (ENG, 0);                      // the WAVETABLE engine, same table
    a.set ("Synth OSC A WT Frame", 0.35f);   // the wavetable engine's frame axis
    const auto wt = a.note (57);
    a.setAbs (ENG, 5);
    const auto sT = spec (table), sW = spec (wt);
    const double dTW = dist (sT, sW);
    double nearestFam = 1e9; int nearIdx = -1; double wtVsBlade = 0;
    for (int m = 0; m < 6; ++m)
    { a.setAbs (HMODE, (float) m); const auto f = a.note (57); const auto sf = spec (f);
      if (m == 0) wtVsBlade = dist (sW, sf);
      const double d = dist (sT, sf); if (d < nearestFam) { nearestFam = d; nearIdx = m; } }
    static const char* FAM[6] = { "Blade","Neon","Console","Chant","Bronze","Hornet" };
    a.setAbs (HMODE, 6);
    // 🚨 A ZERO HERE IS ONLY MEANINGFUL IF THE TWO RENDERS CAME FROM DIFFERENT ENGINES. If the
    //    engine switch had silently failed, both would be the SAME render and the distance would
    //    also be 0.0 — the exact degenerate shape that hid the never-baked grid. So the wavetable
    //    render must first be shown to differ from a procedural family; only then does its
    //    closeness to HARM/Table mean the bank really did rebuild that table.
    { char b[240]; std::snprintf (b, sizeof b,
        "%.2f dB from the WAVETABLE engine on the same table; nearest procedural family (%s) is %.1f dB away"
        " [switch proof: that same wavetable render is %.1f dB from Blade]",
        dTW, nearIdx >= 0 ? FAM[nearIdx] : "?", nearestFam, wtVsBlade);
      chk (wtVsBlade > 3.0 && dTW < nearestFam * 0.5,
           "[2] IT SOUNDS LIKE THE TABLE, not like a procedural family", b); }

    // ── 3 · the menu drives it ────────────────────────────────────────────────────────────────
    a.setAbs (wtName.c_str(), 2);  const auto pA = a.note (57);
    a.setAbs (wtName.c_str(), 16); const auto pB = a.note (57);
    a.setAbs (wtName.c_str(), 12); const auto pC = a.note (57);
    const double d32 = dist (spec (pA), spec (pB)), d33 = dist (spec (pB), spec (pC));
    { char b[200]; std::snprintf (b, sizeof b, "Square->VowelMorph %.1f dB, VowelMorph->D50Bell %.1f dB", d32, d33);
      chk (d32 > 6.0 && d33 > 6.0, "[3] THE MENU DRIVES IT — the wavetable preset picks the bank", b); }

    // ── 4 · HUE scans the table ───────────────────────────────────────────────────────────────
    a.setAbs (wtName.c_str(), 16);            // VowelMorph: its frames genuinely differ
    a.set (HUE, 0.0f);  const auto h0 = a.note (57);
    a.set (HUE, 0.5f);  const auto h1 = a.note (57);
    a.set (HUE, 1.0f);  const auto h2 = a.note (57);
    const double dh01 = dist (spec (h0), spec (h1)), dh12 = dist (spec (h1), spec (h2));
    { char b[200]; std::snprintf (b, sizeof b, "HUE 0->0.5 moves %.1f dB, 0.5->1 moves %.1f dB", dh01, dh12);
      chk (dh01 > 4.0 && dh12 > 4.0, "[4] HUE SCANS THE TABLE — the frame axis is live", b); }

    // ── 5 · the six families are unharmed ─────────────────────────────────────────────────────
    { bool ok = true; std::string worst; double minPair = 1e9;
      std::vector<std::vector<double>> ss;
      for (int m = 0; m < 6; ++m) { a.setAbs (HMODE, (float) m); const auto f = a.note (57);
        if (rmsDb (f, 2048) < -50.0) { ok = false; worst += std::to_string (m) + " "; }
        ss.push_back (spec (f)); }
      for (size_t i = 0; i < ss.size(); ++i) for (size_t j = i + 1; j < ss.size(); ++j)
        minPair = std::min (minPair, dist (ss[i], ss[j]));
      char b[200]; std::snprintf (b, sizeof b, "%s; closest pair of the six is still %.1f dB apart",
                                  ok ? "all six sound" : ("SILENT: " + worst).c_str(), minPair);
      chk (ok && minPair > 3.0, "[5] THE SIX FAMILIES ARE UNHARMED", b); }

    // ── 6 · no clicks sweeping HUE across all 16 frames under a held note ─────────────────────
    a.setAbs (HMODE, 6); a.setAbs (wtName.c_str(), 16); a.set (HUE, 0.0f);
    a.pump (0.3); a.midi (0x90, 57, 100); a.render (8);
    std::vector<float> swept;
    for (int s = 0; s <= 40; ++s) { a.set (HUE, (float) s / 40.0f); const auto c = a.render (2);
                                    swept.insert (swept.end(), c.begin(), c.end()); }
    a.midi (0x80, 57, 0); a.render (20);
    double worstJump = 0; for (size_t i = 1; i < swept.size(); ++i)
        worstJump = std::max (worstJump, (double) std::fabs (swept[i] - swept[i-1]));
    // 🚨 THE REFERENCE IS THE SOUNDS THE SWEEP PASSES THROUGH, not one arbitrary standstill.
    //    A brighter frame has bigger sample-to-sample steps for entirely legitimate reasons, so
    //    comparing a sweep that ENDS somewhere bright against a standstill somewhere dull measures
    //    the table's brightness, not a click. (Exactly the trap fb585's click bar fell into: it
    //    read 7.54x against the wrong reference and the "fix" changed nothing.) The honest
    //    reference is the worst step across every static position the sweep visits.
    double refJump = 0;
    for (int s = 0; s <= 4; ++s)
    { a.set (HUE, (float) s / 4.0f); const auto still = a.note (57);
      for (size_t i = 1; i < still.size(); ++i)
        refJump = std::max (refJump, (double) std::fabs (still[i] - still[i-1])); }
    { char b[200]; std::snprintf (b, sizeof b, "worst step sweeping %.4f vs %.4f at the positions it passes through (%.2fx)",
                                  worstJump, refJump, refJump > 1e-9 ? worstJump / refJump : 0.0);
      chk (worstJump <= refJump * 1.6, "[6] NO CLICKS sweeping HUE across all 16 frames", b); }

    a.close();
    std::printf ("\n══ RESULT: %d pass, %d FAIL ══\n\n", pass, fail);
    return fail == 0 ? 0 : 1;
}
