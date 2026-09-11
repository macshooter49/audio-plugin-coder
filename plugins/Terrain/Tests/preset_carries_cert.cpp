// ══ fb635 — OFF IS NOT CARRIED (drafted in scratch; the proposed Tests/preset_carries_cert.cpp) ═══
//   Max: "if one thing in the preset is off or gone, then it shouldn't say it's carrying it."
//   Uses ONLY API the fb632 header already has (of · healCatalogue · oscEngineOf · isSampleEngine ·
//   kEngModal · kEngineDefault) and literal numbers, so the SAME file compiles against the shipped
//   header (must go RED) and the new one (must go GREEN).
//   CR_MUT=ungated   bar [1] expects the Michael Myers shape to count ONE          → RED
//   CR_MUT=noheal    bar [7] expects the healed July row to still say flow 3      → RED
//   CR_MUT=flowblob  bar [11] expects the blob count (4th Of July = 3)            → RED
//   CR_MUT=offcounts bars [8] [10] [12] expect the ungated count                  → RED
#include "PresetCarries.h"
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <vector>
#include <string>
#include <initializer_list>

static int pass = 0, fail = 0; static std::vector<std::string> failedBars;
static void chk (bool ok, const char* label, const juce::String& detail)
{ if (ok) ++pass; else { ++fail; failedBars.push_back (label); }
  std::printf ("  %s  %s\n        %s\n", ok ? "PASS" : "FAIL", label, detail.toRawUTF8()); }
static juce::AudioBuffer<float> tone (int nch, int n, float amp, float inc = 0.013f)
{ juce::AudioBuffer<float> b (nch, n);
  for (int c = 0; c < nch; ++c) for (int i = 0; i < n; ++i)
      b.setSample (c, i, amp * std::sin ((float) i * inc + (float) c * 0.7f) * (0.6f + 0.4f * std::sin ((float) i * 0.00031f)));
  return b; }
static const char* const OSCL = "ABCD";
static const char* const NAME[7] = { "WT", "SAMP", "GRAN", "SPEC", "FM", "HARM", "MODAL" };
static const char* const FOSSIL = "AirCans_cleaning-spray-single-l-stereoflac_453179.ogg";
static const char* const ABS    = "/Volumes/SomeoneElsesDrive/Kicks/Artist Kick.wav";
static const char* const MEM    = "mem:Artist Kick.wav";

static void param (juce::ValueTree& s, const juce::String& id, double v)
{ juce::ValueTree p ("PARAM"); p.setProperty ("id", id, nullptr); p.setProperty ("value", v, nullptr); s.appendChild (p, nullptr); }
static juce::String oscId (int o, const char* suf) { return juce::String ("SYN_OSC_") + OSCL[o] + "_" + suf; }
static void engine   (juce::ValueTree& s, int o, int e)   { param (s, oscId (o, "ENGINE"), e); }
static void modalSrc (juce::ValueTree& s, int o, int src) { param (s, oscId (o, "MODAL_SOURCE"), src); }
static void on       (juce::ValueTree& s, int o, int v)   { param (s, oscId (o, "ENABLE"), v); }
static juce::String rvbP (int inst, const char* suf) { return (inst <= 1 ? juce::String ("SYN_RVB_") : "SYN_RVB" + juce::String (inst) + "_") + suf; }
static void rvb (juce::ValueTree& s, int inst, int type, int power, int active)
{ param (s, rvbP (inst, "TYPE"), type); param (s, rvbP (inst, "POWER"), power); param (s, rvbP (inst, "ACTIVE"), active); }
static void flowChain (juce::ValueTree& s, std::initializer_list<int> slots, int mode)
{ int i = 1; for (int m : slots) param (s, "FLOW_CHAIN_" + juce::String (i++), m); param (s, "FLOW_MODE", mode); }
static void cards (juce::ValueTree& s, std::initializer_list<const char*> keys)
{ auto* o = new juce::DynamicObject();
  for (auto k : keys) o->setProperty (k, "{\"slots\":{\"1\":\"{}\"},\"chain\":[1],\"cur\":1}");
  s.setProperty ("cardStates", juce::JSON::toString (juce::var (o), true), nullptr); }
static void shapes (juce::ValueTree& s, std::initializer_list<int> ns, int npts = 3)
{ juce::String j = "{\"shapes\":[";
  bool first = true;
  for (int n : ns) { if (! first) j << ","; first = false;
      j << "{\"n\":" << n << ",\"pts\":[" << (npts >= 1 ? "[0,0,0]" : "") << (npts >= 2 ? ",[0.5,1,0]" : "") << (npts >= 3 ? ",[1,0,0]" : "") << "],\"gh\":8,\"gv\":8,\"sn\":1,\"nm\":\"\"}"; }
  s.setProperty ("lfoShapesJson", j + "]}", nullptr); }
static void lfoShape (juce::ValueTree& s, int n, int k) { param (s, "LFO" + juce::String (n) + "_SHAPE", k); }
static int    get     (const juce::ValueTree& s, const char* k) { return (int) tw::carries::of (s).getProperty (k, -1); }
static double bytesOf (const juce::ValueTree& s) { return (double) tw::carries::of (s).getProperty ("bytes", -1.0); }
static juce::String body (const juce::MemoryBlock& chunk)
{ auto x = tw::bank::chunkToXml (chunk); if (x == nullptr) return "(bad chunk)";
  x->removeChildElement (x->getChildByName ("preset"), true); juce::MemoryBlock mb; tw::bank::xmlToChunk (*x, mb);
  return juce::String::fromUTF8 ((const char*) mb.getData(), (int) mb.getSize()); }

// ── the two tools fb632 shipped with this cert, carried over verbatim ─────────────────────────
//   --emit <dir>                  writes osc.b64 (a real one-shot envelope) for Tests/preset_carries_au.cpp
//   --heal <factoryRoot> <userRoot>  runs healCatalogue on a real root and prints every recount
static int heal (const juce::String& factoryRoot, const juce::String& userRoot)
{
    tw::bank::ScanStats st; tw::bank::Caps caps;
    auto cat = tw::bank::scan (juce::File (factoryRoot), juce::File (userRoot), caps, st);
    std::vector<std::pair<juce::String, juce::String>> before;
    if (auto* banks = cat.getProperty ("banks", juce::var()).getArray())
        for (auto& b : *banks) if (auto* ps = b.getProperty ("presets", juce::var()).getArray())
            for (auto& p : *ps) before.push_back ({ p.getProperty ("path", "").toString(), juce::JSON::toString (p.getProperty ("carries", juce::var()), true) + " fv " + p.getProperty ("fv", "").toString() });
    const int n = tw::carries::healCatalogue (cat, juce::File (userRoot));
    size_t i = 0;
    if (auto* banks = cat.getProperty ("banks", juce::var()).getArray())
        for (auto& b : *banks) if (auto* ps = b.getProperty ("presets", juce::var()).getArray())
            for (auto& p : *ps)
            {
                const auto after = juce::JSON::toString (p.getProperty ("carries", juce::var()), true) + " fv " + p.getProperty ("fv", "").toString();
                if (i < before.size() && before[i].second != after)
                    std::printf ("  recounted  %s\n      was %s\n      now %s\n", juce::File (before[i].first).getFileName().toRawUTF8(), before[i].second.toRawUTF8(), after.toRawUTF8());
                ++i;
            }
    std::printf ("  %d preset(s) scanned, %d file(s) rewritten\n", (int) before.size(), n);
    return 0;
}

static int emit (const juce::String& dirPath)
{
    const juce::File dir (dirPath); dir.createDirectory();
    const auto b64 = tw::asset::encode (tone (2, 24000, 0.7f), 48000.0, 0, "Artist One Shot.wav");
    dir.getChildFile ("osc.b64").replaceWithText (b64);
    std::printf ("  osc.b64  %d chars\n", b64.length());
    return b64.isEmpty() ? 1 : 0;
}

int main (int argc, char** argv)
{
    if (argc >= 3 && juce::String (argv[1]) == "--emit") return emit (argv[2]);
    if (argc >= 4 && juce::String (argv[1]) == "--heal") return heal (argv[2], argv[3]);

    const char* mutC = std::getenv ("CR_MUT"); const juce::String mut = mutC ? mutC : "";
    std::printf ("══ fb635 OFF IS NOT CARRIED ══   mutation: %s\n", mut.isEmpty() ? "(none)" : mut.toRawUTF8());
    const bool offMut = mut == "offcounts";
    const auto OSC = tw::asset::encode (tone (2, 24000, 0.7f), 48000.0, 0, "Artist One Shot.wav");
    const auto WT  = tw::asset::encode (tone (1, 2 * 2048, 0.9f, 0.29f), 0.0, 2, "Rectified Sine");
    const auto LAY = tw::asset::encode (tone (1, 16000, 0.5f, 0.021f), 44100.0, 0, "Layer.wav");
    const auto IR  = tw::asset::encode (tone (2, 4096, 0.4f, 0.05f), 48000.0, 0, "Hall IR.wav");
    const double B_OSC = (double) tw::asset::sizeOf (OSC), B_WT = (double) tw::asset::sizeOf (WT),
                 B_LAY = (double) tw::asset::sizeOf (LAY), B_IR = (double) tw::asset::sizeOf (IR);

    // ── [1] THE MICHAEL MYERS SHAPE (fb632, unchanged — osc A needs no ENABLE: the layout has it on) ──
    {
        auto s = juce::ValueTree ("Parameters");
        s.setProperty ("oscSamplePath0", FOSSIL, nullptr);
        s.setProperty ("wtAsset0", WT, nullptr); s.setProperty ("wtImportFrames0", 2, nullptr); s.setProperty ("wtImportFile0", 1, nullptr);
        engine (s, 0, 5); engine (s, 1, 0); engine (s, 2, 0); engine (s, 3, 0);
        const int want = (mut == "ungated") ? 1 : 0; const auto c = tw::carries::of (s);
        chk ((int) c["smp"] == want && (int) c["wt"] == 1 && (double) c["bytes"] == B_WT,
             "[1] THE MICHAEL MYERS SHAPE — a sample-name hint on a Harmonic oscillator: not a one-shot; its imported table on HARM is one wavetable",
             "smp=" + juce::String ((int) c["smp"]) + " (want " + juce::String (want) + ") wt=" + juce::String ((int) c["wt"]) + " bytes=" + juce::String ((double) c["bytes"]));
    }
    // ── [2] THE SEVEN ENGINES on osc B — now switched ON (a fresh instance has B off) ─────────
    {
        juce::String got; bool ok = true;
        for (int e = 0; e < 7; ++e)
        {
            auto h = juce::ValueTree ("Parameters"); h.setProperty ("oscSamplePath1", ABS, nullptr); engine (h, 1, e); on (h, 1, 1);
            auto a = juce::ValueTree ("Parameters"); a.setProperty ("oscAsset1", OSC, nullptr);      engine (a, 1, e); on (a, 1, 1);
            const int want = (tw::carries::isSampleEngine (e) || e == tw::carries::kEngModal) ? 1 : 0;
            ok = ok && get (h, "smp") == 0 && get (a, "smp") == want && bytesOf (h) == 0.0 && bytesOf (a) == B_OSC;
            got += juce::String (NAME[e]) + "=" + juce::String (get (h, "smp")) + "/" + juce::String (get (a, "smp")) + " ";
        }
        chk (ok, "[2] SAMPLE · GRANULAR · RESYNTH · MODAL(Auto) COUNT AN ON OSCILLATOR'S EMBEDDED AUDIO — WT · FM · HARMONIC DO NOT; a hint alone is 0", got);
    }
    {
        juce::String got; bool ok = true; const int want[4] = { 1, 0, 0, 1 };
        for (int src = 0; src < 4; ++src)
        { auto s = juce::ValueTree ("Parameters"); s.setProperty ("oscAsset1", OSC, nullptr); engine (s, 1, 6); modalSrc (s, 1, src); on (s, 1, 1);
          ok = ok && get (s, "smp") == want[src]; got += juce::String (src) + "→" + juce::String (get (s, "smp")) + " "; }
        chk (ok, "[2b] A MODAL OSCILLATOR COUNTS ITS ONE-SHOT WHEN THE STRIKE IS Auto OR Sample", "source Auto·Noise·Click·Sample: " + got);
    }
    {
        auto s = juce::ValueTree ("Parameters"); s.setProperty ("oscAsset2", OSC, nullptr); on (s, 2, 1);
        chk (get (s, "smp") == 0 && tw::carries::oscEngineOf (s, 2) == tw::carries::kEngineDefault && bytesOf (s) == B_OSC,
             "[3] AN ENGINE PARAM THE TREE DOES NOT CARRY IS THE LAYOUT'S DEFAULT (WT) — priced, not counted", "smp=" + juce::String (get (s, "smp")));
        auto f = juce::ValueTree ("Parameters"); f.setProperty ("oscSamplePath0", FOSSIL, nullptr); engine (f, 0, 1);
        auto a = juce::ValueTree ("Parameters"); a.setProperty ("oscSamplePath0", ABS,    nullptr); engine (a, 0, 1);
        auto m = juce::ValueTree ("Parameters"); m.setProperty ("oscSamplePath0", MEM,    nullptr); engine (m, 0, 1);
        chk (get (f, "smp") == 0 && get (a, "smp") == 0 && get (m, "smp") == 0, "[3b] A NAME ALONE IS NOT A ONE-SHOT, EVEN ON A SAMPLE OSCILLATOR", "");
    }
    {
        auto s = juce::ValueTree ("Parameters");
        s.setProperty ("oscAsset0", OSC, nullptr); s.setProperty ("oscSamplePath0", "/Users/max/Library/WavesCrate/Samples/hit.wav", nullptr);
        engine (s, 0, 3); engine (s, 1, 0); engine (s, 2, 0); engine (s, 3, 0); on (s, 0, 1);
        chk (get (s, "smp") == 1 && bytesOf (s) == B_OSC, "[4] THE CODE VERONICA SHAPE — Resynth holding an embedded one-shot: ONE", "smp=" + juce::String (get (s, "smp")));
    }
    {
        auto s = juce::ValueTree ("Parameters");
        s.setProperty ("oscAsset0", OSC, nullptr); engine (s, 0, 1); on (s, 0, 1);
        s.setProperty ("oscAsset1", OSC, nullptr); engine (s, 1, 0); on (s, 1, 1);
        s.setProperty ("oscAsset2", OSC, nullptr); engine (s, 2, 2); on (s, 2, 1);
        s.setProperty ("oscAsset3", OSC, nullptr); engine (s, 3, 5); on (s, 3, 1);
        chk (get (s, "smp") == 2 && bytesOf (s) == 4 * B_OSC, "[5] FOUR ON SLOTS, EACH JUDGED BY ITS OWN ENGINE — Sample and Granular count; all four priced", "smp=" + juce::String (get (s, "smp")));
    }
    // ── [6] LAYERS — embedded layer audio counts; a layer that is only a NAME does not (Max Voltage) ──
    {
        auto s = juce::ValueTree ("Parameters");
        s.setProperty ("layerAsset1", LAY, nullptr);
        juce::ValueTree layers ("layers");
        for (int i = 0; i < 4; ++i) { juce::ValueTree l ("layer"); l.setProperty ("index", i, nullptr); l.setProperty ("sourcePath", i == 0 ? "\xc2\xa9 open-hat \xe2\x88\xab terrafaux - unknownfamily +.wav" : "", nullptr); layers.appendChild (l, nullptr); }
        s.appendChild (layers, nullptr);
        chk (get (s, "smp") == 1 && bytesOf (s) == B_LAY,
             "[6] A LAYER COUNTS ITS EMBEDDED AUDIO — a layer that carries only a sourcePath held nothing when it was saved (the fb621 writer embeds every loaded layer)",
             "smp=" + juce::String (get (s, "smp")) + " (want 1) bytes=" + juce::String (bytesOf (s)));
    }
    // ── [8] THE OSCILLATOR'S POWER — SYN_OSC_x_ENABLE ─────────────────────────────────────────
    {
        auto offS = juce::ValueTree ("Parameters"); offS.setProperty ("oscAsset1", OSC, nullptr); engine (offS, 1, 1); on (offS, 1, 0);
        auto onS  = juce::ValueTree ("Parameters"); onS .setProperty ("oscAsset1", OSC, nullptr); engine (onS,  1, 1); on (onS,  1, 1);
        auto defA = juce::ValueTree ("Parameters"); defA.setProperty ("oscAsset0", OSC, nullptr); engine (defA, 0, 1);            // no ENABLE: A is on
        auto defB = juce::ValueTree ("Parameters"); defB.setProperty ("oscAsset1", OSC, nullptr); engine (defB, 1, 1);            // no ENABLE: B is off
        auto wOff = juce::ValueTree ("Parameters"); wOff.setProperty ("wtAsset3", WT, nullptr);  engine (wOff, 3, 0); on (wOff, 3, 0);   // Little Nightmares, osc D
        auto wOn  = juce::ValueTree ("Parameters"); wOn .setProperty ("wtAsset3", WT, nullptr);  engine (wOn,  3, 0); on (wOn,  3, 1);
        const int w0 = offMut ? 1 : 0;
        const bool ok = get (offS, "smp") == w0 && bytesOf (offS) == B_OSC && get (onS, "smp") == 1 && get (defA, "smp") == 1
                     && get (defB, "smp") == w0 && get (wOff, "wt") == w0 && bytesOf (wOff) == B_WT && get (wOn, "wt") == 1;
        chk (ok, "[8] AN OSCILLATOR THAT IS OFF CARRIES NOTHING — its one-shot and its wavetable are priced, not counted; absent ENABLE is the layout's (A on, B-D off)",
             "smp off/on/A-default/B-default = " + juce::String (get (offS, "smp")) + "/" + juce::String (get (onS, "smp")) + "/" + juce::String (get (defA, "smp")) + "/" + juce::String (get (defB, "smp"))
             + " · wt D off/on = " + juce::String (get (wOff, "wt")) + "/" + juce::String (get (wOn, "wt")) + " (want off = " + juce::String (w0) + ")");
    }
    // ── [9] WHO READS AN IMPORTED WAVETABLE — WT · FM · HARMONIC ─────────────────────────────
    {
        juce::String got; bool ok = true;
        for (int e = 0; e < 7; ++e)
        { auto s = juce::ValueTree ("Parameters"); s.setProperty ("wtAsset1", WT, nullptr); engine (s, 1, e); on (s, 1, 1);
          const int want = (e == 0 || e == 4 || e == 5) ? 1 : 0;
          ok = ok && get (s, "wt") == want && bytesOf (s) == B_WT; got += juce::String (NAME[e]) + "=" + juce::String (get (s, "wt")) + " "; }
        chk (ok, "[9] AN IMPORTED WAVETABLE IS CARRIED BY WT · FM · HARMONIC — Sample · Granular · Resynth · Modal never read it; priced on every engine", got);
    }
    // ── [10] THE IR — Convolution, powered, in the rack, on ITS OWN instance ─────────────────
    {
        auto mk = [&] (int slot, int inst, int type, int power, int active)
        { auto s = juce::ValueTree ("Parameters"); s.setProperty ("irAsset" + juce::String (slot), IR, nullptr); rvb (s, inst, type, power, active); return s; };
        const int w0 = offMut ? 1 : 0;
        const auto live = mk (1, 1, 8, 1, 1), hall = mk (1, 1, 0, 1, 1), off = mk (1, 1, 8, 0, 1), out = mk (1, 1, 8, 1, 0),
                   three = mk (3, 3, 8, 1, 1), wrong = mk (3, 1, 8, 1, 1);
        const bool ok = get (live, "ir") == 1 && get (hall, "ir") == w0 && get (off, "ir") == w0 && get (out, "ir") == w0
                     && get (three, "ir") == 1 && get (wrong, "ir") == w0
                     && bytesOf (live) == B_IR && bytesOf (hall) == B_IR && bytesOf (off) == B_IR && bytesOf (out) == B_IR;
        chk (ok, "[10] AN IR IS CARRIED ONLY BY A CONVOLUTION REVERB THAT IS POWERED AND IN THE RACK — irAsset3 is reverb 3's, not reverb 1's; every IR is priced",
             "conv/hall/power-off/out-of-rack/inst3/inst3-with-inst1-params = " + juce::String (get (live, "ir")) + "/" + juce::String (get (hall, "ir")) + "/"
             + juce::String (get (off, "ir")) + "/" + juce::String (get (out, "ir")) + "/" + juce::String (get (three, "ir")) + "/" + juce::String (get (wrong, "ir")));
    }
    // ── [10b] fb635 — A PRE-RACK SAVE: no SYN_RVB_ACTIVE PARAM at all → the loader puts reverb 1 IN the rack (fb346) ──
    {
        auto pre = juce::ValueTree ("Parameters"); pre.setProperty ("irAsset1", IR, nullptr);
        param (pre, "SYN_RVB_TYPE", 8); param (pre, "SYN_RVB_POWER", 1);                 // no ACTIVE child: a pre-rack tree
        auto out = juce::ValueTree ("Parameters"); out.setProperty ("irAsset1", IR, nullptr);
        param (out, "SYN_RVB_TYPE", 8); param (out, "SYN_RVB_POWER", 1); param (out, "SYN_RVB_ACTIVE", 0);   // explicitly out of the rack
        chk (get (pre, "ir") == 1 && get (out, "ir") == (offMut ? 1 : 0),
             "[10b] A PRE-RACK SAVE CARRIES ITS CONVOLUTION IR — no ACTIVE PARAM means the loader puts reverb 1 in the rack (fb346); an explicit ACTIVE 0 does not",
             "pre-rack=" + juce::String (get (pre, "ir")) + " (want 1)  out-of-rack=" + juce::String (get (out, "ir")));
    }
    // ── [11] THE FLOW CARDS THAT ARE ON ───────────────────────────────────────────────────────
    {
        auto t = [] { return juce::ValueTree ("Parameters"); };
        auto july = t();   cards (july,  { "arp", "chop", "gli" }); flowChain (july,  { 0, 0, 0, 0 }, 0);   // 4th Of July: three dice-rolled blobs, every tile dark
        auto paulo = t();  cards (paulo, { "arp", "chop", "gli" }); flowChain (paulo, { 3, 0, 0, 0 }, 3);   // Paulo: only Glitch lit
        auto blood = t();  cards (blood, { "chop", "gli" });        flowChain (blood, { 0, 0, 0, 0 }, 0);   // Bloodlust / Razor Blade: the dice's default pick
        auto phant = t();  cards (phant, { "crv", "lfo", "gli" });  flowChain (phant, { 3, 0, 0, 0 }, 3);   // the curve pop-out + the LFO card are not FLOW cards
        auto robin = t();                                            flowChain (robin, { 4, 0, 0, 0 }, 4);   // All You Need: Robin lit, no blob
        auto legacy = t();                                           flowChain (legacy, { 0, 0, 0, 0 }, 2);  // a pre-fb131 save: FLOW_MODE alone
        auto dup = t();                                              flowChain (dup,   { 3, 3, 0, 0 }, 3);   // automation wrote Glitch twice
        auto all = t();    cards (all, { "arp", "chop", "gli", "rbn" }); flowChain (all, { 2, 3, 1, 4 }, 2);
        auto none = t();
        const bool blob = mut == "flowblob";
        const int wJ = blob ? 3 : 0;
        const bool ok = get (july, "flow") == wJ && get (paulo, "flow") == 1 && get (blood, "flow") == 0 && get (phant, "flow") == 1
                     && get (robin, "flow") == 1 && get (legacy, "flow") == 1 && get (dup, "flow") == 1 && get (all, "flow") == 4 && get (none, "flow") == 0;
        chk (ok, "[11] A FLOW CARD IS CARRIED WHILE ITS TILE IS LIT — the dice's blobs for dark cards, the LFO card and the curve pop-out are not flow cards; a lit card with no blob is",
             "July/Paulo/Bloodlust/phantom/Robin/legacy/dup/all/none = " + juce::String (get (july, "flow")) + "/" + juce::String (get (paulo, "flow")) + "/"
             + juce::String (get (blood, "flow")) + "/" + juce::String (get (phant, "flow")) + "/" + juce::String (get (robin, "flow")) + "/"
             + juce::String (get (legacy, "flow")) + "/" + juce::String (get (dup, "flow")) + "/" + juce::String (get (all, "flow")) + "/" + juce::String (get (none, "flow"))
             + "  (want " + juce::String (wJ) + "/1/0/1/1/1/1/4/0)");
    }
    // ── [12] THE LFO SHAPES THAT PLAY ─────────────────────────────────────────────────────────
    {
        auto t = [] { return juce::ValueTree ("Parameters"); };
        auto dont = t();  shapes (dont, { 1, 2, 3 }); lfoShape (dont, 1, 7); lfoShape (dont, 2, 0); lfoShape (dont, 3, 0);   // Don't Go
        auto path = t();  shapes (path, { 2 });       lfoShape (path, 2, 8);
        auto sh   = t();  shapes (sh,   { 1 });       lfoShape (sh,   1, 5);                                                 // Watership Down: an S&H seed
        auto lor  = t();  shapes (lor,  { 1 });       lfoShape (lor,  1, 9);
        auto bad  = t();  shapes (bad,  { 0, 11 });   lfoShape (bad,  1, 7);
        auto thin = t();  shapes (thin, { 1 }, 1);    lfoShape (thin, 1, 7);
        auto dupS = t();  shapes (dupS, { 1, 1 });    lfoShape (dupS, 1, 7);
        const int wD = offMut ? 3 : 1, w0 = offMut ? 1 : 0;
        const bool ok = get (dont, "lfo") == wD && get (path, "lfo") == 1 && get (sh, "lfo") == w0 && get (lor, "lfo") == w0
                     && get (bad, "lfo") == 0 && get (thin, "lfo") == 0 && get (dupS, "lfo") == 1;
        chk (ok, "[12] A DRAWN LFO SHAPE IS CARRIED ONLY BY AN LFO ON Custom OR Path — a shape seeded for a tab that plays Sine, S&H or Lorenz is not",
             "DontGo/Path/S&H/Lorenz/bad-n/one-point/dup = " + juce::String (get (dont, "lfo")) + "/" + juce::String (get (path, "lfo")) + "/" + juce::String (get (sh, "lfo")) + "/"
             + juce::String (get (lor, "lfo")) + "/" + juce::String (get (bad, "lfo")) + "/" + juce::String (get (thin, "lfo")) + "/" + juce::String (get (dupS, "lfo"))
             + "  (want " + juce::String (wD) + "/1/" + juce::String (w0) + "/" + juce::String (w0) + "/0/0/1)");
    }
    // ── [13] STATE PERSISTS — of() reads; everything OFF is still in the tree and still priced ──
    {
        auto s = juce::ValueTree ("Parameters");
        s.setProperty ("oscAsset1", OSC, nullptr); engine (s, 1, 1); on (s, 1, 0);
        s.setProperty ("wtAsset2", WT, nullptr); engine (s, 2, 0); on (s, 2, 0);
        s.setProperty ("irAsset2", IR, nullptr); rvb (s, 2, 8, 0, 1);
        cards (s, { "arp", "chop", "gli" }); flowChain (s, { 0, 0, 0, 0 }, 0);
        shapes (s, { 1 }); lfoShape (s, 1, 0);
        const auto before = s.toXmlString(); const auto c = tw::carries::of (s); const auto after = s.toXmlString();
        const int total = (int) c["wt"] + (int) c["smp"] + (int) c["ir"] + (int) c["flow"] + (int) c["lfo"];
        chk (before == after && total == 0 && (double) c["bytes"] == B_OSC + B_WT + B_IR,
             "[13] STATE PERSISTS — a preset where everything is OFF carries nothing, keeps every byte in the tree, and of() leaves the tree untouched",
             "counts=" + juce::String (total) + " bytes=" + juce::String ((double) c["bytes"]) + " (want " + juce::String (B_OSC + B_WT + B_IR) + ") tree untouched=" + juce::String (before == after ? "yes" : "NO"));
    }
    // ── [7] THE HEAL — fv 3 → 4, every row below 4 recounted ──────────────────────────────────
    {
        const auto root = juce::File::createTempFile ("carries"); root.deleteFile(); root.createDirectory();
        const auto user = root.getChildFile ("Banks"), factory = root.getChildFile ("FactoryBanks");
        user.getChildFile ("Max").createDirectory(); factory.getChildFile ("Ship").createDirectory();
        const juce::Time past (2025, 0, 15, 12, 0, 0);
        auto mkFile = [&] (const juce::File& dst, int shape, const char* oldCarries, int fv)
        {
            auto s = juce::ValueTree ("Parameters");
            if (shape == 0) { cards (s, { "arp", "chop", "gli" }); flowChain (s, { 0, 0, 0, 0 }, 0); }   // July
            else            { flowChain (s, { 4, 0, 0, 0 }, 4); }                                           // Robin lit, no blob
            juce::ValueTree pe ("preset");
            pe.setProperty ("name", dst.getFileNameWithoutExtension(), nullptr); pe.setProperty ("author", "Gate", nullptr);
            pe.setProperty ("carries", oldCarries, nullptr); pe.setProperty ("fv", fv, nullptr);
            s.addChild (pe, 0, nullptr);
            std::unique_ptr<juce::XmlElement> x (s.createXml());
            juce::MemoryBlock chunk; tw::bank::xmlToChunk (*x, chunk);
            juce::MemoryOutputStream o; tw::bank::wrap (o, tw::bank::manifestFromChild (*x->getChildByName ("preset")), chunk);
            dst.replaceWithData (o.getData(), o.getDataSize()); dst.setLastModificationTime (past);
            return chunk;
        };
        const char* three = "{\"wt\": 0, \"wtf\": 0, \"smp\": 0, \"ir\": 0, \"flow\": 3, \"lfo\": 0, \"nodes\": 0, \"bytes\": 0.0}";
        const char* zero  = "{\"wt\": 0, \"wtf\": 0, \"smp\": 0, \"ir\": 0, \"flow\": 0, \"lfo\": 0, \"nodes\": 0, \"bytes\": 0.0}";
        const auto fi = user.getChildFile ("Max").getChildFile ("July.terrain"), fii = user.getChildFile ("Max").getChildFile ("Robin.terrain"),
                   fiii = factory.getChildFile ("Ship").getChildFile ("July.terrain"), fiv = user.getChildFile ("Max").getChildFile ("Done.terrain");
        const auto ci = mkFile (fi, 0, three, 3), cii = mkFile (fii, 1, zero, 3); mkFile (fiii, 0, three, 3); mkFile (fiv, 0, three, 4);
        const auto sizeIII = fiii.getSize(), sizeIV = fiv.getSize();
        tw::bank::ScanStats st; tw::bank::Caps caps;
        auto cat = tw::bank::scan (factory, user, caps, st);
        const int rewritten = tw::carries::healCatalogue (cat, user);
        auto row = [&] (const juce::String& bank, const juce::String& name) -> juce::var
        { if (auto* banks = cat.getProperty ("banks", juce::var()).getArray())
            for (auto& b : *banks) if (b.getProperty ("name", "").toString() == bank)
              if (auto* ps = b.getProperty ("presets", juce::var()).getArray())
                for (auto& p : *ps) if (p.getProperty ("name", "").toString() == name) return p;
          return {}; };
        auto flowRow = [&] (const juce::var& r) { return (int) r.getProperty ("carries", juce::var()).getProperty ("flow", -1); };
        auto fvRow   = [&] (const juce::var& r) { return (int) r.getProperty ("fv", -1); };
        auto disk = [&] (const juce::File& f, juce::String& m, juce::MemoryBlock& c) { juce::MemoryBlock file; f.loadFileAsData (file); juce::String e; return tw::bank::unwrap (file, m, c, e); };
        auto flowDisk = [&] (const juce::File& f) { juce::String m; juce::MemoryBlock c; disk (f, m, c); return (int) juce::JSON::parse (m).getProperty ("carries", juce::var()).getProperty ("flow", -1); };
        auto fvDisk   = [&] (const juce::File& f) { juce::String m; juce::MemoryBlock c; disk (f, m, c); return (int) juce::JSON::parse (m).getProperty ("fv", -1); };
        auto bodyDisk = [&] (const juce::File& f) { juce::String m; juce::MemoryBlock c; disk (f, m, c); return body (c); };
        auto mtimeKept = [&] (const juce::File& f) { return std::abs (f.getLastModificationTime().toMilliseconds() - past.toMilliseconds()) < 2000; };
        const int wantI = (mut == "noheal") ? 3 : 0;
        const auto rI = row ("Max", "July"), rII = row ("Max", "Robin"), rIII = row ("Ship", "July"), rIV = row ("Max", "Done");
        chk (flowRow (rI) == wantI && fvRow (rI) == 4 && flowDisk (fi) == 0 && fvDisk (fi) == 4 && bodyDisk (fi) == body (ci) && mtimeKept (fi),
             "[7] THE HEAL — an fv 3 file saying \"3 flow cards\" with every tile dark is recounted off its own chunk: row 0, manifest 0, fv 4, chunk byte-identical outside <preset>, mtime kept",
             "row flow=" + juce::String (flowRow (rI)) + " (want " + juce::String (wantI) + ") fv=" + juce::String (fvRow (rI)) + " · disk flow=" + juce::String (flowDisk (fi)) + " fv=" + juce::String (fvDisk (fi))
             + " · body identical=" + juce::String (bodyDisk (fi) == body (ci) ? "yes" : "NO") + " · mtime kept=" + juce::String (mtimeKept (fi) ? "yes" : "NO"));
        chk (flowRow (rII) == 1 && flowDisk (fii) == 1 && fvDisk (fii) == 4 && bodyDisk (fii) == body (cii) && mtimeKept (fii),
             "[7b] A ROW THAT SAID 0 IS OPENED TOO — a lit Robin with no card blob is one flow card (the fv 3 \"a 0 is exact\" shortcut is gone)",
             "row flow=" + juce::String (flowRow (rII)) + " · disk flow=" + juce::String (flowDisk (fii)) + " fv=" + juce::String (fvDisk (fii)));
        chk (flowRow (rIII) == 0 && fvRow (rIII) == 3 && fiii.getSize() == sizeIII && fvDisk (fiii) == 3 && flowDisk (fiii) == 3,
             "[7c] A FACTORY FILE IS CORRECTED IN THE CATALOGUE ONLY — the row says 0, the shipped bytes are never written",
             "row flow=" + juce::String (flowRow (rIII)) + " · disk flow=" + juce::String (flowDisk (fiii)) + " fv=" + juce::String (fvDisk (fiii)));
        chk (flowRow (rIV) == 3 && fvRow (rIV) == 4 && fiv.getSize() == sizeIV && rewritten == 2,
             "[7d] AN fv 4 FILE IS NEVER OPENED — exactly the two fv 3 user files were rewritten",
             "row flow=" + juce::String (flowRow (rIV)) + " rewritten=" + juce::String (rewritten));
        tw::bank::ScanStats st2; auto cat2 = tw::bank::scan (factory, user, caps, st2);
        const int again = tw::carries::healCatalogue (cat2, user);
        chk (again == 0 && flowDisk (fi) == 0 && fvDisk (fi) == 4 && mtimeKept (fi), "[7e] THE HEAL IS ONCE — a second scan finds fv 4 and writes nothing", "rewritten=" + juce::String (again));
        root.deleteRecursively();
    }
    std::printf ("\n  %d passed, %d FAILED\n", pass, fail);
    if (! failedBars.empty()) { std::printf ("  FAILED BARS:\n"); for (auto& b : failedBars) std::printf ("    - %s\n", b.c_str()); }
    return fail ? 1 : 0;
}
