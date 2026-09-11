// ══ fb632 — CARRIES ARE COUNTED WHERE THEY ARE PLAYED ═══════════════════════════════════════════
//   Max: "Michael Myers has no one shots in it but the carry says it has one shots. The only time a
//   carry should even be activated is if there's a Sampler, Resynth, or a Granular engine going on."
//
//   Source/PresetCarries.h::of() on synthetic trees, and the heal on a throwaway root — the same
//   function and the same file layer (Source/PresetBank.h) the plugin runs, with real juce_core +
//   juce_data_structures + libFLAC behind the asset envelope (compiled by Tests/fb632_gates.sh).
//
//   CR_MUT=ungated   the control: bar [1] expects the Michael Myers shape to count ONE — must go RED
//   CR_MUT=noheal    the control: bar [7] expects the healed catalogue row to still say ONE — must go RED
//   --emit <dir>     writes osc.b64 (a real one-shot envelope) for Tests/preset_carries_au.cpp
//   --heal <factoryRoot> <userRoot>   runs the plugin's own heal (scan + healCatalogue) on real roots
//                    and prints every row it recounted — what the browser does on its next open
#include "PresetCarries.h"
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <vector>
#include <string>

static int pass = 0, fail = 0; static std::vector<std::string> failedBars;
static void chk (bool ok, const char* label, const juce::String& detail)
{ if (ok) ++pass; else { ++fail; failedBars.push_back (label); }
  std::printf ("  %s  %s\n        %s\n", ok ? "PASS" : "FAIL", label, detail.toRawUTF8()); }

static juce::AudioBuffer<float> tone (int nch, int n, float amp, float inc = 0.013f)
{
    juce::AudioBuffer<float> b (nch, n);
    for (int c = 0; c < nch; ++c)
        for (int i = 0; i < n; ++i)
            b.setSample (c, i, amp * std::sin ((float) i * inc + (float) c * 0.7f)
                                * (0.6f + 0.4f * std::sin ((float) i * 0.00031f)));
    return b;
}

static const char* const ENG[4] = { ParameterIDs::SYN_OSC_A_ENGINE, ParameterIDs::SYN_OSC_B_ENGINE,
                                    ParameterIDs::SYN_OSC_C_ENGINE, ParameterIDs::SYN_OSC_D_ENGINE };
static const char* const MSRC[4] = { ParameterIDs::SYN_OSC_A_MODAL_SOURCE, ParameterIDs::SYN_OSC_B_MODAL_SOURCE,
                                     ParameterIDs::SYN_OSC_C_MODAL_SOURCE, ParameterIDs::SYN_OSC_D_MODAL_SOURCE };
static const char* const NAME[7] = { "WT", "SAMP", "GRAN", "SPEC", "FM", "HARM", "MODAL" };
static const char* const FOSSIL = "AirCans_cleaning-spray-single-l-stereoflac_453179.ogg";   // Michael Myers' own hint, verbatim: a bare name
static const char* const ABS    = "/Volumes/SomeoneElsesDrive/Kicks/Artist Kick.wav";        // an absolute path, missing on this disk
static const char* const MEM    = "mem:Artist Kick.wav";                                       // a dropped file's ref

// the tree the way getStateInformation lays it out: root properties + <PARAM id value/> children
static void param (juce::ValueTree& s, const char* id, int v)
{ juce::ValueTree p ("PARAM"); p.setProperty ("id", id, nullptr); p.setProperty ("value", (double) v, nullptr); s.appendChild (p, nullptr); }
static void engine   (juce::ValueTree& s, int o, int e)   { param (s, ENG[o],  e); }
static void modalSrc (juce::ValueTree& s, int o, int src) { param (s, MSRC[o], src); }
static int    smpOf   (const juce::ValueTree& s) { return (int)    tw::carries::of (s).getProperty ("smp",   -1); }
static double bytesOf (const juce::ValueTree& s) { return (double) tw::carries::of (s).getProperty ("bytes", -1.0); }

// the chunk with its <preset> child cut out — what must survive a heal byte for byte
static juce::String body (const juce::MemoryBlock& chunk)
{ auto x = tw::bank::chunkToXml (chunk); if (x == nullptr) return "(bad chunk)";
  x->removeChildElement (x->getChildByName ("preset"), true); juce::MemoryBlock mb; tw::bank::xmlToChunk (*x, mb);
  return juce::String::fromUTF8 ((const char*) mb.getData(), (int) mb.getSize()); }

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
    std::printf ("══ fb632 CARRIES ARE COUNTED WHERE THEY ARE PLAYED ══   mutation: %s\n", mut.isEmpty() ? "(none)" : mut.toRawUTF8());

    const auto OSC = tw::asset::encode (tone (2, 24000, 0.7f), 48000.0, 0, "Artist One Shot.wav");
    const auto WT  = tw::asset::encode (tone (1, 2 * 2048, 0.9f, 0.29f), 0.0, 2, "Rectified Sine");
    const auto LAY = tw::asset::encode (tone (1, 16000, 0.5f, 0.021f), 44100.0, 0, "Layer.wav");
    const double B_OSC = (double) tw::asset::sizeOf (OSC), B_WT = (double) tw::asset::sizeOf (WT), B_LAY = (double) tw::asset::sizeOf (LAY);

    // ── [1] THE MICHAEL MYERS SHAPE ───────────────────────────────────────────────────────────
    //  osc A on Harmonic with a sample-NAME hint and no audio; a user wavetable on the same slot;
    //  B/C/D on Wavetable. The file said smp 1 and bytes 4320 — the 4320 were the wavetable's.
    {
        auto s = juce::ValueTree ("Parameters");
        s.setProperty ("oscSamplePath0", FOSSIL, nullptr);
        s.setProperty ("wtAsset0", WT, nullptr); s.setProperty ("wtImportFrames0", 2, nullptr); s.setProperty ("wtImportFile0", 1, nullptr);
        engine (s, 0, 5); engine (s, 1, 0); engine (s, 2, 0); engine (s, 3, 0);
        const int want = (mut == "ungated") ? 1 : 0;
        const auto c = tw::carries::of (s);
        chk ((int) c["smp"] == want && (int) c["wt"] == 1 && (double) c["bytes"] == B_WT,
             "[1] THE MICHAEL MYERS SHAPE — a sample-name hint on a Harmonic oscillator, no audio: NOT a one-shot; the bytes are the wavetable's alone",
             "smp=" + juce::String ((int) c["smp"]) + " (want " + juce::String (want) + ") wt=" + juce::String ((int) c["wt"])
             + " bytes=" + juce::String ((double) c["bytes"]) + " (wavetable " + juce::String (B_WT) + ")");
    }

    // ── [2] THE SEVEN ENGINES, an absolute hint / embedded audio, on osc B ───────────────────
    {
        juce::String got; bool ok = true;
        for (int e = 0; e < 7; ++e)
        {
            auto h = juce::ValueTree ("Parameters"); h.setProperty ("oscSamplePath1", ABS, nullptr); engine (h, 1, e);
            auto a = juce::ValueTree ("Parameters"); a.setProperty ("oscAsset1", OSC, nullptr);      engine (a, 1, e);
            const int want = (tw::carries::isSampleEngine (e) || e == tw::carries::kEngModal) ? 1 : 0;   // Modal: exciter Auto (the default) rings it
            ok = ok && smpOf (h) == 0 && smpOf (a) == want && bytesOf (h) == 0.0 && bytesOf (a) == B_OSC;   // a hint alone is never audio
            got += juce::String (NAME[e]) + "=" + juce::String (smpOf (h)) + "/" + juce::String (smpOf (a)) + " ";
        }
        chk (ok, "[2] SAMPLE · GRANULAR · RESYNTH · MODAL(Auto) COUNT THE SLOT'S EMBEDDED AUDIO — WT · FM · HARMONIC DO NOT; a hint alone is 0 on every engine; embedded bytes are priced on every engine", got);
    }
    // ── [2b] MODAL follows its exciter source ─────────────────────────────────────────────────
    {
        juce::String got; bool ok = true; const int want[4] = { 1, 0, 0, 1 };   // Auto · Noise · Click · Sample
        for (int src = 0; src < 4; ++src)
        {
            auto s = juce::ValueTree ("Parameters"); s.setProperty ("oscAsset1", OSC, nullptr); engine (s, 1, 6); modalSrc (s, 1, src);
            ok = ok && smpOf (s) == want[src]; got += juce::String (src) + "→" + juce::String (smpOf (s)) + " ";
        }
        chk (ok, "[2b] A MODAL OSCILLATOR COUNTS ITS ONE-SHOT WHEN THE STRIKE IS Auto OR Sample — not on Noise or Click", "source Auto·Noise·Click·Sample: " + got);
    }

    // ── [3] a tree with no ENGINE PARAM at all: the layout's default (WT) — not counted ────────
    {
        auto s = juce::ValueTree ("Parameters"); s.setProperty ("oscAsset2", OSC, nullptr);
        chk (smpOf (s) == 0 && tw::carries::oscEngineOf (s, 2) == tw::carries::kEngineDefault && bytesOf (s) == B_OSC,
             "[3] AN ENGINE PARAM THE TREE DOES NOT CARRY IS THE LAYOUT'S DEFAULT (WT) — the slot's audio is priced, not counted",
             "smp=" + juce::String (smpOf (s)) + " engine=" + juce::String (tw::carries::oscEngineOf (s, 2)) + " bytes=" + juce::String (bytesOf (s)));
    }
    // ── [3b] the HINT's shape, on a Sample oscillator ─────────────────────────────────────────
    {
        auto f = juce::ValueTree ("Parameters"); f.setProperty ("oscSamplePath0", FOSSIL, nullptr); engine (f, 0, 1);
        auto a = juce::ValueTree ("Parameters"); a.setProperty ("oscSamplePath0", ABS,    nullptr); engine (a, 0, 1);
        auto m = juce::ValueTree ("Parameters"); m.setProperty ("oscSamplePath0", MEM,    nullptr); engine (m, 0, 1);
        chk (smpOf (f) == 0 && smpOf (a) == 0 && smpOf (m) == 0,
             "[3b] A NAME ALONE IS NOT A ONE-SHOT, EVEN ON A SAMPLE OSCILLATOR — since fb621 the audio travels embedded; a slot with only a hint held nothing when it was saved",
             "bare=" + juce::String (smpOf (f)) + " absolute=" + juce::String (smpOf (a)) + " mem:=" + juce::String (smpOf (m)));
    }

    // ── [4] THE CODE VERONICA SHAPE — Resynth on A with the one-shot embedded and an absolute path ──
    {
        auto s = juce::ValueTree ("Parameters");
        s.setProperty ("oscAsset0", OSC, nullptr); s.setProperty ("oscSamplePath0", "/Users/max/Library/WavesCrate/Samples/hit.wav", nullptr);
        engine (s, 0, 3); engine (s, 1, 0); engine (s, 2, 0); engine (s, 3, 0);
        chk (smpOf (s) == 1 && bytesOf (s) == B_OSC,
             "[4] THE CODE VERONICA SHAPE — Resynth holding an embedded one-shot: ONE, priced at its bytes",
             "smp=" + juce::String (smpOf (s)) + " bytes=" + juce::String (bytesOf (s)));
    }

    // ── [5] four oscillators, each on its own engine ──────────────────────────────────────────
    {
        auto s = juce::ValueTree ("Parameters");
        s.setProperty ("oscAsset0", OSC, nullptr);       engine (s, 0, 1);   // Sample, audio      → counts, priced
        s.setProperty ("oscAsset1", OSC, nullptr);       engine (s, 1, 0);   // WT, audio          → not played, still priced
        s.setProperty ("oscAsset2", OSC, nullptr); s.setProperty ("oscSamplePath2", ABS, nullptr); engine (s, 2, 2);   // Granular, audio → counts
        s.setProperty ("oscAsset3", OSC, nullptr); s.setProperty ("oscSamplePath3", ABS, nullptr); engine (s, 3, 5);   // Harmonic, audio → not played
        chk (smpOf (s) == 2 && bytesOf (s) == 4 * B_OSC,
             "[5] FOUR SLOTS, EACH JUDGED BY ITS OWN OSCILLATOR — Sample and Granular count their audio; WT and Harmonic do not; all four embedded assets are priced",
             "smp=" + juce::String (smpOf (s)) + " bytes=" + juce::String (bytesOf (s)) + " (one asset = " + juce::String (B_OSC) + ")");
    }

    // ── [6] the layers (the pad sampler's own audio) are untouched by the gate ───────────────
    {
        auto s = juce::ValueTree ("Parameters");
        s.setProperty ("layerAsset1", LAY, nullptr);
        juce::ValueTree layers ("layers");
        for (int i = 0; i < 4; ++i) { juce::ValueTree l ("layer"); l.setProperty ("index", i, nullptr); l.setProperty ("sourcePath", i == 2 ? "/Loops/break.wav" : "", nullptr); layers.appendChild (l, nullptr); }
        s.appendChild (layers, nullptr);
        chk (smpOf (s) == 2 && bytesOf (s) == B_LAY,
             "[6] THE LAYERS RULE IS UNCHANGED — an embedded layer and a layer with a source path are one one-shot each",
             "smp=" + juce::String (smpOf (s)) + " bytes=" + juce::String (bytesOf (s)));
    }

    // ── [7] THE HEAL — a throwaway root with pre-fv3 files ────────────────────────────────────
    //  (i)   user/Max/Michael.terrain     the Michael Myers shape, manifest says smp 1 / fv 2  → recounted to 0, rewritten
    //  (ii)  user/Max/Veronica.terrain    the Code Veronica shape, manifest says smp 1 / fv 2  → stays 1, stamped fv 3 once
    //  (iii) factory/Ship/Michael.terrain the Michael Myers shape, manifest says smp 1 / fv 2  → row corrected, file untouched
    //  (iv)  user/Max/Clean.terrain       smp 0 / fv 2                                         → never opened, never written
    {
        const auto root = juce::File::createTempFile ("carries"); root.deleteFile(); root.createDirectory();
        const auto user = root.getChildFile ("Banks"), factory = root.getChildFile ("FactoryBanks");
        user.getChildFile ("Max").createDirectory(); factory.getChildFile ("Ship").createDirectory();
        const juce::Time past (2025, 0, 15, 12, 0, 0);
        auto mkFile = [&] (const juce::File& dst, bool michael, const char* oldCarries)
        {
            auto s = juce::ValueTree ("Parameters");
            if (michael) { s.setProperty ("oscSamplePath0", FOSSIL, nullptr); s.setProperty ("wtAsset0", WT, nullptr); engine (s, 0, 5); }
            else         { s.setProperty ("oscAsset0", OSC, nullptr); engine (s, 0, 3); }
            engine (s, 1, 0); engine (s, 2, 0); engine (s, 3, 0);
            juce::ValueTree pe ("preset");
            pe.setProperty ("name", dst.getFileNameWithoutExtension(), nullptr); pe.setProperty ("author", "Gate", nullptr);
            pe.setProperty ("carries", oldCarries, nullptr); pe.setProperty ("fv", 2, nullptr);
            s.addChild (pe, 0, nullptr);
            std::unique_ptr<juce::XmlElement> x (s.createXml());
            juce::MemoryBlock chunk; tw::bank::xmlToChunk (*x, chunk);
            juce::MemoryOutputStream out; tw::bank::wrap (out, tw::bank::manifestFromChild (*x->getChildByName ("preset")), chunk);
            dst.replaceWithData (out.getData(), out.getDataSize());
            dst.setLastModificationTime (past);
            return chunk;
        };
        const char* wrong = "{\"wt\": 1, \"wtf\": 0, \"smp\": 1, \"ir\": 0, \"flow\": 0, \"lfo\": 0, \"nodes\": 0, \"bytes\": 4320.0}";
        const char* right = "{\"wt\": 0, \"wtf\": 0, \"smp\": 1, \"ir\": 0, \"flow\": 0, \"lfo\": 0, \"nodes\": 0, \"bytes\": 1000.0}";
        const char* clean = "{\"wt\": 0, \"wtf\": 0, \"smp\": 0, \"ir\": 0, \"flow\": 0, \"lfo\": 0, \"nodes\": 0, \"bytes\": 0.0}";
        const auto fi = user.getChildFile ("Max").getChildFile ("Michael.terrain");
        const auto fii = user.getChildFile ("Max").getChildFile ("Veronica.terrain");
        const auto fiii = factory.getChildFile ("Ship").getChildFile ("Michael.terrain");
        const auto fiv = user.getChildFile ("Max").getChildFile ("Clean.terrain");
        const auto ci = mkFile (fi, true, wrong), cii = mkFile (fii, false, right); mkFile (fiii, true, wrong); mkFile (fiv, true, clean);
        const auto sizeIII = fiii.getSize(), sizeIV = fiv.getSize();

        tw::bank::ScanStats st; tw::bank::Caps caps;
        auto cat = tw::bank::scan (factory, user, caps, st);
        const int rewritten = tw::carries::healCatalogue (cat, user);

        auto row = [&] (const juce::String& bank, const juce::String& name) -> juce::var
        {
            if (auto* banks = cat.getProperty ("banks", juce::var()).getArray())
                for (auto& b : *banks) if (b.getProperty ("name", "").toString() == bank)
                    if (auto* ps = b.getProperty ("presets", juce::var()).getArray())
                        for (auto& p : *ps) if (p.getProperty ("name", "").toString() == name) return p;
            return {};
        };
        auto smpRow = [&] (const juce::var& r) { return (int) r.getProperty ("carries", juce::var()).getProperty ("smp", -1); };
        auto fvRow  = [&] (const juce::var& r) { return (int) r.getProperty ("fv", -1); };
        auto disk = [&] (const juce::File& f, juce::String& manifest, juce::MemoryBlock& chunk)
        { juce::MemoryBlock file; f.loadFileAsData (file); juce::String e; return tw::bank::unwrap (file, manifest, chunk, e); };
        auto smpDisk = [&] (const juce::File& f) { juce::String m; juce::MemoryBlock c; disk (f, m, c); return (int) juce::JSON::parse (m).getProperty ("carries", juce::var()).getProperty ("smp", -1); };
        auto fvDisk  = [&] (const juce::File& f) { juce::String m; juce::MemoryBlock c; disk (f, m, c); return (int) juce::JSON::parse (m).getProperty ("fv", -1); };
        auto bodyDisk = [&] (const juce::File& f) { juce::String m; juce::MemoryBlock c; disk (f, m, c); return body (c); };
        auto mtimeKept = [&] (const juce::File& f) { return std::abs (f.getLastModificationTime().toMilliseconds() - past.toMilliseconds()) < 2000; };

        const int wantI = (mut == "noheal") ? 1 : 0;
        const auto rI = row ("Max", "Michael"), rII = row ("Max", "Veronica"), rIII = row ("Ship", "Michael"), rIV = row ("Max", "Clean");
        chk (smpRow (rI) == wantI && fvRow (rI) == 3 && smpDisk (fi) == 0 && fvDisk (fi) == 3 && bodyDisk (fi) == body (ci) && mtimeKept (fi),
             "[7] THE HEAL — a pre-fv3 file claiming a phantom one-shot is recounted off its own chunk: row 0, manifest 0, fv 3, chunk byte-identical outside the <preset> child, mtime kept",
             "row smp=" + juce::String (smpRow (rI)) + " (want " + juce::String (wantI) + ") fv=" + juce::String (fvRow (rI))
             + " · disk smp=" + juce::String (smpDisk (fi)) + " fv=" + juce::String (fvDisk (fi))
             + " · body identical=" + juce::String (bodyDisk (fi) == body (ci) ? "yes" : "NO") + " · mtime kept=" + juce::String (mtimeKept (fi) ? "yes" : "NO"));
        chk (smpRow (rII) == 1 && smpDisk (fii) == 1 && fvDisk (fii) == 3 && bodyDisk (fii) == body (cii) && mtimeKept (fii),
             "[7b] A REAL ONE-SHOT KEEPS ITS COUNT — Resynth + embedded audio stays 1, stamped fv 3 once, chunk and mtime untouched",
             "row smp=" + juce::String (smpRow (rII)) + " · disk smp=" + juce::String (smpDisk (fii)) + " fv=" + juce::String (fvDisk (fii)));
        chk (smpRow (rIII) == 0 && fvRow (rIII) == 2 && fiii.getSize() == sizeIII && fvDisk (fiii) == 2 && smpDisk (fiii) == 1 && mtimeKept (fiii),
             "[7c] A FACTORY FILE IS CORRECTED IN THE CATALOGUE ONLY — the row says 0, the shipped bytes are never written",
             "row smp=" + juce::String (smpRow (rIII)) + " · disk smp=" + juce::String (smpDisk (fiii)) + " fv=" + juce::String (fvDisk (fiii)) + " bytes " + juce::String (fiii.getSize()) + "/" + juce::String (sizeIII));
        chk (smpRow (rIV) == 0 && fvRow (rIV) == 2 && fiv.getSize() == sizeIV && fvDisk (fiv) == 2 && rewritten == 2,
             "[7d] A FILE THAT CLAIMS NOTHING IS NEVER OPENED — smp 0 rows are exact already; exactly the two user files were rewritten",
             "row fv=" + juce::String (fvRow (rIV)) + " rewritten=" + juce::String (rewritten));
        tw::bank::ScanStats st2; auto cat2 = tw::bank::scan (factory, user, caps, st2);
        const int again = tw::carries::healCatalogue (cat2, user);
        chk (again == 0 && smpDisk (fi) == 0 && fvDisk (fi) == 3 && mtimeKept (fi),
             "[7e] THE HEAL IS ONCE — a second scan finds fv 3 and writes nothing",
             "rewritten=" + juce::String (again));
        root.deleteRecursively();
    }

    std::printf ("\n  %d passed, %d FAILED\n", pass, fail);
    if (! failedBars.empty()) { std::printf ("  FAILED BARS:\n"); for (auto& b : failedBars) std::printf ("    - %s\n", b.c_str()); }
    return fail ? 1 : 0;
}
