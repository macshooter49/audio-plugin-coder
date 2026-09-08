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
//  fb601 — RETUNED, AND TWO BARS REWRITTEN. Two things happened to this file's world:
//    · fb599 pinned h.mainMode = 6 in the HARM gather (PluginProcessor.cpp:9546). Max: "I only
//      want to use tables for this ... I don't want the families anymore." Every stored HARM_MODE
//      index now renders the TABLE.
//    · fb601 split the roster parameter in two: HARM reads SYN_OSC_x_HARM_TABLE, the wavetable
//      engine keeps SYN_OSC_x_WT_PRESET.
//  This gate kept driving "Synth OSC A WT Preset" on the HARM path through a FUZZY NAME MATCH, so
//  after fb601 it was setting a parameter the harmonics engine no longer reads and measuring the
//  0.0 dB that follows. The fuzzy fallback is gone (see the PREFLIGHT), and bars [2] and [5] —
//  which asserted PRE-fb599 behaviour — now certify what actually ships.
//
//  THE BARS
//   0  the AU actually offers a SEVENTH harmonic family
//   1  IT SOUNDS — Table is not a silent option (case 6 renders silence on a null table, so this
//      is the bar that catches a bake lane that never published)
//   2  THE HARMONICS TABLE *IS* THE WAVETABLE'S SPECTRUM — the same table rendered through the
//      HARM engine and through the WAVETABLE engine must land on top of each other, and a SECOND
//      table must sit far away, so a pair of dead engines cannot read as agreement. (fb601: was
//      "not like a procedural family", which fb599's pin made unpassable and meaningless.)
//   3  THE MENU DRIVES IT — changing SYN_OSC_A_HARM_TABLE changes the additive sound, while
//      WT_PRESET stays parked on Sine. "The same menu" is a claim about wiring; this measures it.
//   4  HUE SCANS THE TABLE — the frame axis is live, and modulatable, because it is read on the
//      audio thread after the mod matrix
//   5  TABLES ONLY — every stored HARM_MODE index renders the SAME table, because fb599 pinned
//      the six procedural families out. (fb601: was "THE SIX FAMILIES ARE UNHARMED", i.e. the
//      exact opposite of shipped behaviour.)
//   6  NO CLICKS sweeping HUE under a held note (the frame scan crosses all 16 frames)
//
//  MUTATION CONTROL:  HARM_TBL_MUT=1 in the environment redirects every HARM-path table write
//  back onto "Synth OSC A WT Preset" — the fb601 rot, re-committed on purpose. Bars [2] [3] [4]
//  and [5] must ALL go red. Run it both ways; a bar that stays green under the mutation is not
//  measuring what its name says.
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

// ── fb601 · THE TABLE PARAMETER IS RESOLVED PER ENGINE, BY EXACT NAME ─────────────────────
//    fb601 split the 46-name roster across TWO AudioParameterChoices with the same strings:
//    the HARMONICS engine reads SYN_OSC_x_HARM_TABLE, the WAVETABLE engine still reads
//    SYN_OSC_x_WT_PRESET. index.html's __synTablePid() picks between them per engine; so does
//    this file, and for the same reason.
//    🚨 THIS FILE USED TO PICK ONE BY SUBSTRING — find("Wavetable") || find("WT Preset") — and
//    that is exactly how it rotted: after fb601 it kept driving "Synth OSC A WT Preset" on the
//    HARM path, the harmonics engine had stopped reading it, and bars [3] and [4] dutifully
//    reported 0.0 dB of change while still LOOKING like they exercised the menu. A gate that
//    measures the wrong parameter is worse than no gate. So there is NO fuzzy fallback here: a
//    missing name aborts the run by name (setTbl below), it never guesses a neighbour.
static const char* HARMTBL = "Synth OSC A Harm Table";   // fb601 — the HARMONICS path
static const char* WTPRE   = "Synth OSC A WT Preset";    //         the WAVETABLE path

// ── MUTATION CONTROL · HARM_TBL_MUT=1 in the environment ─────────────────────────────────────
//    Re-commits the exact fb601 rot: every HARM-path write is redirected to the WAVETABLE
//    parameter, i.e. what the old fuzzy match did. Bars [2] [3] [4] [5] must all go RED.
//    A green bar that cannot go red is not a gate — this is how this one proves it can.
static bool gMut = false;
static const char* route (const char* param)
{ return (gMut && std::strcmp (param, HARMTBL) == 0) ? WTPRE : param; }

// drive one of the two table parameters — LOUDLY. Never silently no-ops.
static void setTbl (AU& a, const char* want, int idx)
{
    const char* param = route (want);
    if (! a.has (param))
    { std::printf ("\n  !! FATAL — the installed AU has no parameter named '%s'.\n"
                   "     This gate resolves the table parameter PER ENGINE and refuses to guess:\n"
                   "     quietly measuring the other one is precisely how it went 0.0 dB after fb601.\n"
                   "     If the parameter was renamed, fix the name here — do NOT restore a fuzzy match.\n\n", param);
      std::exit (2); }
    if (! a.setAbs (param, (float) idx))
    { std::printf ("\n  !! FATAL — AudioUnitSetParameter failed on '%s' (index %d)\n\n", param, idx);
      std::exit (2); }
}

int main()
{
    std::printf ("\n══ harm_table_au — fb588, retuned fb601 ══  additive synthesis from the wavetables, on the INSTALLED AU\n\n");
    AU a; if (! a.open()) return 1;

    gMut = (std::getenv ("HARM_TBL_MUT") != nullptr);
    if (gMut) std::printf ("  \xe2\x9a\xa0\xef\xb8\x8f  MUTATION ACTIVE: HARM_TBL_MUT set — every HARM-path table write is redirected to\n"
                           "      '%s', which is exactly what the pre-fb601 fuzzy name match did.\n"
                           "      Bars [2] [3] [4] [5] are EXPECTED to go red. If they do not, they are not\n"
                           "      reading the parameter they name.\n\n", WTPRE);

    // ── PREFLIGHT · both table parameters, resolved by EXACT name and PRINTED ─────────────────
    //    A detector that can silently no-op has to say whether it fired. These two lines are that
    //    statement: they name the parameter each engine will be driven through, and its range.
    for (const char* p : { HARMTBL, WTPRE })
    {
        if (! a.has (p))
        { std::printf ("  !! FATAL — the installed AU has no parameter named '%s'.\n"
                       "     Every measurement below would silently land on the OTHER table parameter,\n"
                       "     which is the fb601 rot this preflight exists to prevent. Refusing to run.\n\n", p);
          a.close(); return 2; }
        std::printf ("  ·  %-24s -> '%s'  (choice 0..%.0f)\n",
                     std::strcmp (p, HARMTBL) == 0 ? "HARM path table param" : "WAVETABLE path table param",
                     p, a.maxOf (p));
    }
    std::printf ("\n");

    // ── 0 · a seventh family exists ───────────────────────────────────────────────────────────
    const float mx = a.maxOf (HMODE);
    { char b[160]; std::snprintf (b, sizeof b, "'%s' max index = %.0f (6 = seven choices; was 5)", HMODE, mx);
      chk (mx == 6.0f, "[0] THE AU OFFERS A SEVENTH HARMONIC FAMILY", b); }

    a.setAbs (ENG, 5);                      // HARM
    a.set ("Synth OSC A Level", 1.0f);
    a.setAbs (HMODE, 6);                    // Table (fb599 pins this anyway — see bar [5])
    setTbl (a, HARMTBL, 4);                 // Prophet Saw, on the HARM path's OWN parameter
    setTbl (a, WTPRE,   0);                 // 🚨 and PARK the wavetable parameter on Sine. Every
                                            //    HARM bar below moves only HARM_TABLE, so a distance
                                            //    that moves is proof the additive bank reads the
                                            //    fb601 id — not the shared one it used to read.
    a.set (HUE, 0.35f);
    const auto table = a.note (57);
    const double dbT = rmsDb (table, 2048);

    // ── 1 · it sounds ─────────────────────────────────────────────────────────────────────────
    { char b[160]; std::snprintf (b, sizeof b, "HARM/Table on a wavetable renders %.1f dB RMS", dbT);
      chk (dbT > -50.0, "[1] IT SOUNDS — Table is not a silent option", b); }

    // ── 2 · the harmonics table IS the wavetable's spectrum ───────────────────────────────────
    //    🔁 fb601 — REWRITTEN, AND THE GATE WAS THE THING THAT WAS WRONG. This bar used to read
    //    "IT SOUNDS LIKE THE TABLE, not like a procedural family" and passed only if HARM/Table sat
    //    far closer to the wavetable engine than to the nearest of the six families. fb599 pinned
    //    h.mainMode = 6 in the HARM gather (PluginProcessor.cpp:9546), so the six families now
    //    RENDER THE TABLE and are 0.0 dB away by design — Tests/harm_tables_cert.cpp certifies that
    //    pin offline on the prepared partial bank. The old bar therefore asserted the exact opposite
    //    of shipped, intended behaviour: it could never pass again, and "fixing" it would have meant
    //    un-shipping the pin. What is worth protecting is the claim fb588 actually made — "0.01 dB
    //    from the wavetable engine on the installed AU" — so that is now the bar, and it is a far
    //    stronger one than "not like a family" ever was.
    //
    //    🚨 CLOSENESS IS ONLY MEANINGFUL WITH A SCALE. Two dead engines are also 0.00 dB apart —
    //    the degenerate shape that hid the never-baked grid in fb588, and the shape this very file
    //    printed all over its report after fb601. So the pairing is measured on TWO tables and the
    //    cross pair must be far: same table -> near AND different table -> far, or nothing is shown.
    struct Pair { std::vector<double> harm, wt; };
    auto renderPair = [&] (int idx) -> Pair
    {
        Pair pr;
        a.setAbs (ENG, 5); setTbl (a, HARMTBL, idx); setTbl (a, WTPRE, 0);   // HARM: its own param
        pr.harm = spec (a.note (57));
        a.setAbs (ENG, 0); setTbl (a, WTPRE, idx);                            // WT: its own param
        a.set ("Synth OSC A WT Frame", 0.35f);                                // the same frame as HUE
        pr.wt = spec (a.note (57));
        a.setAbs (ENG, 5); setTbl (a, WTPRE, 0);
        return pr;
    };
    const Pair P1 = renderPair (4), P2 = renderPair (16);        // Prophet Saw · Vowel Morph
    const double dSame1 = dist (P1.harm, P1.wt), dSame2 = dist (P2.harm, P2.wt);
    const double dCross = dist (P1.harm, P2.wt), dHH = dist (P1.harm, P2.harm);
    { char b[320]; std::snprintf (b, sizeof b,
        "SAME table through both engines: Prophet Saw %.2f dB, Vowel Morph %.2f dB. "
        "SCALE — HARM(Prophet Saw) vs WT(Vowel Morph) %.1f dB, HARM vs HARM across the two %.1f dB "
        "(without these two a pair of dead engines also reads 0.00)", dSame1, dSame2, dCross, dHH);
      chk (dSame1 < 3.0 && dSame2 < 3.0 && dCross > 8.0 && dHH > 8.0,
           "[2] THE HARMONICS TABLE *IS* THE WAVETABLE'S SPECTRUM", b); }

    // ── 3 · the menu drives it ────────────────────────────────────────────────────────────────
    //    fb601 — only HARM_TABLE moves here; WT_PRESET stays parked on Sine from the setup. That
    //    is the whole point: if the additive bank ever went back to reading the wavetable id, these
    //    three renders would be one sound and the bar would go red instead of looking busy.
    setTbl (a, HARMTBL, 2);  const auto pA = a.note (57);
    setTbl (a, HARMTBL, 16); const auto pB = a.note (57);
    setTbl (a, HARMTBL, 12); const auto pC = a.note (57);
    const double d32 = dist (spec (pA), spec (pB)), d33 = dist (spec (pB), spec (pC));
    { char b[220]; std::snprintf (b, sizeof b, "Square->VowelMorph %.1f dB, VowelMorph->D50Bell %.1f dB"
                                               " (WT Preset held at Sine throughout)", d32, d33);
      chk (d32 > 6.0 && d33 > 6.0, "[3] THE MENU DRIVES IT — SYN_OSC_A_HARM_TABLE picks the bank", b); }

    // ── 4 · HUE scans the table ───────────────────────────────────────────────────────────────
    setTbl (a, HARMTBL, 16);                  // VowelMorph: its frames genuinely differ
    a.set (HUE, 0.0f);  const auto h0 = a.note (57);
    a.set (HUE, 0.5f);  const auto h1 = a.note (57);
    a.set (HUE, 1.0f);  const auto h2 = a.note (57);
    const double dh01 = dist (spec (h0), spec (h1)), dh12 = dist (spec (h1), spec (h2));
    { char b[200]; std::snprintf (b, sizeof b, "HUE 0->0.5 moves %.1f dB, 0.5->1 moves %.1f dB", dh01, dh12);
      chk (dh01 > 4.0 && dh12 > 4.0, "[4] HUE SCANS THE TABLE — the frame axis is live", b); }

    // ── 5 · TABLES ONLY ───────────────────────────────────────────────────────────────────────
    //    🔁 fb601 — INVERTED ON PURPOSE, AND THE GATE WAS THE THING THAT WAS WRONG. This bar used
    //    to be "[5] THE SIX FAMILIES ARE UNHARMED" and required the six procedural families to stay
    //    more than 3 dB apart. fb599 pinned h.mainMode = 6 in the HARM gather
    //    (PluginProcessor.cpp:9546-9547, sculptMode pinned to Keel alongside it) — Max: "I only want
    //    to use tables for this ... I don't want the families anymore" — so every stored HARM_MODE
    //    index renders the TABLE and the six sit 0.0 dB apart BY DESIGN. The old bar asserted the
    //    opposite of shipped behaviour; it is rewritten to certify the pin instead of the ghost.
    //
    //    WHY IT ASSERTS SAMENESS, for whoever reads this next: HARM_MODE is still REGISTERED — a
    //    choice param's cardinality is frozen at birth and is never renumbered — it is simply never
    //    read. So the shipped property is that all SEVEN indices are one sound, including the six
    //    that a pre-fb599 preset may still have stored. Tests/harm_tables_cert.cpp certifies the
    //    same pin OFFLINE against the prepared partial bank, with its own HM_MUT mutation seam;
    //    no code is shared, because that cert never opens an AU (it includes HarmonicEngine.h with
    //    `#define private public`) and this one never touches the engine header.
    //
    //    🚨 SAMENESS NEEDS A SCALE, or "all 0.0 dB apart" is exactly what a dead gate prints too —
    //    which is what bar [3] was doing five minutes ago. So the same metric is shown MOVING on the
    //    one axis that is still live, the table, and it is moved at HARM_MODE = 0 (Blade, a RETIRED
    //    index) so the scale doubles as proof that a retired index still resolves down the table path.
    { setTbl (a, HARMTBL, 4);
      bool ok = true; std::string silent; double worstPair = 0;
      std::vector<std::vector<double>> ss;
      for (int m = 0; m <= 6; ++m) { a.setAbs (HMODE, (float) m); const auto f = a.note (57);
        if (rmsDb (f, 2048) < -50.0) { ok = false; silent += std::to_string (m) + " "; }
        ss.push_back (spec (f)); }
      for (size_t i = 0; i < ss.size(); ++i) for (size_t j = i + 1; j < ss.size(); ++j)
        worstPair = std::max (worstPair, dist (ss[i], ss[j]));
      a.setAbs (HMODE, 0);                                   // Blade — retired, still table-only
      setTbl (a, HARMTBL, 16); const auto other = a.note (57);
      const double scale = dist (ss[0], spec (other));
      setTbl (a, HARMTBL, 4); a.setAbs (HMODE, 6);
      char b[320]; std::snprintf (b, sizeof b,
        "%s; the 7 stored HARM_MODE indices are at most %.2f dB apart on one table, while the SAME "
        "metric moves %.1f dB when only the table changes (mode still 0 = Blade)",
        ok ? "all seven sound" : ("SILENT: " + silent).c_str(), worstPair, scale);
      chk (ok && worstPair < 2.0 && scale > 8.0,
           "[5] TABLES ONLY — fb599 pinned the six families out", b); }

    // ── 6 · no clicks sweeping HUE across all 16 frames under a held note ─────────────────────
    a.setAbs (HMODE, 6); setTbl (a, HARMTBL, 16); a.set (HUE, 0.0f);
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
