// ══ fb621 — THE ARTIST'S MACHINE: an embedded asset reaches the DSP on an instance that has never
//    seen the file. Driven through kAudioUnitProperty_ClassInfo on the INSTALLED AU — the same door
//    a DAW uses — so a green bar here is a green bar in Ableton.
//
//    Max is about to commission sound designers. They will use THEIR one-shots, THEIR wavetables,
//    THEIR flow cards, and send banks back. Every one of those files will be missing from his disk.
//    This cert is the proof that it does not matter: what a preset carries is what it sounds like.
//
//    🚨 THE SEAM. The payloads are not synthesised here — Tests/preset_assets_cert --emit writes
//    them with the REAL encoder (Source/PresetAssets.h) and this cert injects those exact bytes.
//    An encoder shape the PROCESSOR does not accept would be green on both sides and broken in the
//    plugin; only a cert that crosses the seam can see it (the fb606 payload precedent).
//
//    HOW A BAR KNOWS THE DECODE REACHED THE ENGINE, through a door that only carries state: the
//    save side writes an asset ONLY when the live buffer holds audio (getStateInformation reads the
//    BUFFER, not the string it loaded). So an asset that goes in and comes back out has necessarily
//    been decoded into the engine. An asset that fails to decode leaves the slot empty and the
//    property is REMOVED. Survival is the proof.
//
//    AA_MUT=corrupt   the envelope's payload is truncated before injection and bar [1] flips to
//                     expect it to SURVIVE — a processor that correctly drops undecodable audio
//                     makes the mutated run fail, which is the point.
//    AA_MUT=inherit   bar [6] flips: the previous patch's assets must SURVIVE a patch that has none.
#include "au_state_blob.h"
#include <cstdlib>
#include <cmath>
#include <fstream>
#include <sstream>

static std::string slurp (const std::string& p)
{
    std::ifstream f (p); if (! f) return {};
    std::stringstream ss; ss << f.rdbuf();
    std::string s = ss.str();
    while (! s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
    return s;
}
static double nullDb (const std::vector<float>& a, const std::vector<float>& b)
{
    double d = 0, r = 0; const size_t n = std::min (a.size(), b.size());
    for (size_t i = 0; i < n; ++i) { const double e = (double) a[i] - (double) b[i]; d += e * e; r += (double) a[i] * (double) a[i]; }
    if (r <= 0) return 0.0;
    return 10.0 * std::log10 (d / r + 1e-30);
}

int main (int argc, char** argv)
{
    const std::string dir = argc > 1 ? argv[1] : "/tmp/tp_assets";
    const char* mutC = std::getenv ("AA_MUT"); const std::string mut = mutC ? mutC : "";
    std::printf ("══ fb621 THE ARTIST'S MACHINE ══   payloads: %s   mutation: %s\n",
                 dir.c_str(), mut.empty() ? "(none)" : mut.c_str());

    std::string OSC = slurp (dir + "/osc.b64"), LAY = slurp (dir + "/layer.b64"),
                IR  = slurp (dir + "/ir.b64"),  WT  = slurp (dir + "/wt.b64"),
                REF = slurp (dir + "/wtref.txt"), REFNAME = slurp (dir + "/wtname.txt");
    if (OSC.empty() || LAY.empty() || IR.empty() || WT.empty())
    { std::printf ("  the emitted payloads are missing — run preset_assets_cert --emit %s first\n", dir.c_str()); return 2; }
    if (mut == "corrupt") OSC = OSC.substr (0, OSC.size() / 2);   // a half-written envelope

    std::string why;
    AU a; if (! a.open()) return 2;
    const std::string virgin = a.readXml (why);
    if (virgin.empty()) { std::printf ("  readXml: %s\n", why.c_str()); return 2; }
    a.close();

    // the artist's chunk: every asset embedded, and a source path that does not exist on THIS disk
    auto withAssets = [&] (bool withRef)
    {
        std::string P = virgin;
        setRootAttr (P, "oscAsset0",     OSC);
        setRootAttr (P, "oscSamplePath0", "/Volumes/SomeoneElsesDrive/Kicks/Artist Kick.wav");
        setRootAttr (P, "layerAsset0",   LAY);
        setRootAttr (P, "irAsset1",      IR);
        setRootAttr (P, "wtAsset0",      withRef && ! REF.empty() ? REF : WT);
        setRootAttr (P, "wtImportFrames0", "64");
        setRootAttr (P, "wtImportFile0",   "1");
        setRootAttr (P, "wtImportName0",   withRef && ! REFNAME.empty() ? REFNAME : std::string ("Artist Table"));
        return P;
    };

    // ── [1] the one-shot survives a fresh instance, byte for byte ─────────────────────────────
    const std::string P = withAssets (false);
    AU b; if (! b.open()) return 2;
    b.writeXml (P);
    const std::string out = b.readXml (why);
    const std::string gotOsc = getRootAttr (out, "oscAsset0");
    {
        const bool want = (mut != "corrupt");
        const bool survived = (gotOsc == OSC) && ! gotOsc.empty();
        chk (survived == want,
             "[1] AN EMBEDDED ONE-SHOT SURVIVES A FRESH INSTANCE — decoded into the engine, then written back UNCHANGED",
             (gotOsc.empty() ? std::string ("the property is GONE after the round trip")
                             : "in " + std::to_string (OSC.size()) + " chars, out " + std::to_string (gotOsc.size())
                               + (gotOsc == OSC ? " — identical" : " — DIFFERENT (a re-encode, not the cached bytes)"))
             + (mut == "corrupt" ? "   (control: the envelope was truncated; it must NOT survive)" : ""));
    }

    // ── [2] the layer, the IR and the wavetable, same law ─────────────────────────────────────
    {
        const std::string gl = getRootAttr (out, "layerAsset0"), gi = getRootAttr (out, "irAsset1"), gw = getRootAttr (out, "wtAsset0");
        chk (gl == LAY && gi == IR && gw == WT,
             "[2] A LAYER SAMPLE, AN IMPULSE RESPONSE AND A WAVETABLE TRAVEL THE SAME WAY",
             "layer " + std::string (gl == LAY ? "ok" : gl.empty() ? "GONE" : "differs")
             + " · IR " + std::string (gi == IR ? "ok" : gi.empty() ? "GONE" : "differs")
             + " · wavetable " + std::string (gw == WT ? "ok" : gw.empty() ? "GONE" : "differs")
             + " · sizes " + std::to_string (gl.size()) + "/" + std::to_string (gi.size()) + "/" + std::to_string (gw.size()));
    }

    // ── [3] 🚨 the artist's machine — the named source file does not exist and never did ───────
    {
        const std::string keptPath = getRootAttr (out, "oscSamplePath0");
        chk (! gotOsc.empty() || mut == "corrupt",
             "[3] 🚨 THE SOURCE FILE IS ABSENT FROM THIS MACHINE AND THE AUDIO ARRIVES ANYWAY",
             "path in the chunk: \"" + keptPath + "\" (no such volume) · audio "
             + (gotOsc.empty() ? "LOST" : "present, " + std::to_string (gotOsc.size()) + " chars"));
    }
    b.close();

    // ── [4] a factory wavetable is a REFERENCE, resolved out of the bundle on this machine ────
    {
        if (REF.empty())
            chk (false, "[4] A FACTORY WAVETABLE REFERENCE RESOLVES FROM THE INSTALLED BUNDLE",
                 "no reference was emitted — the shipped wavetable library was not found next to the AU");
        else
        {
            AU c; if (! c.open()) return 2;
            c.writeXml (withAssets (true));
            const std::string o2 = c.readXml (why);
            const std::string gotRef = getRootAttr (o2, "wtAsset0");
            c.close();
            // and a reference to something that is NOT there must NOT survive
            std::string bogus = P;
            setRootAttr (bogus, "wtAsset0", "ref:1|NoSuchCategory/No Such Table.flac|0000000000000000");
            AU d; if (! d.open()) return 2;
            d.writeXml (bogus);
            const std::string gotBogus = getRootAttr (d.readXml (why), "wtAsset0");
            d.close();
            chk (gotRef == REF && gotBogus.empty(),
                 "[4] A FACTORY WAVETABLE IS A REFERENCE, RESOLVED OUT OF THE BUNDLE — and a missing one is refused",
                 "ref \"" + REF.substr (0, 58) + "…\" " + (gotRef == REF ? "resolved and re-saved" : gotRef.empty() ? "GONE" : "changed")
                 + " · a reference to a table that does not exist: " + (gotBogus.empty() ? "correctly dropped" : "SURVIVED"));
        }
    }

    // ── [5] it reaches the DSP — the sound actually changes, and it is REPRODUCIBLE ────────────
    {
        AU e; if (! e.open()) return 2; e.writeXml (virgin);            const auto dry  = e.note (60); e.close();
        AU f; if (! f.open()) return 2; f.writeXml (withAssets (false)); const auto wet1 = f.note (60); f.close();
        AU g; if (! g.open()) return 2; g.writeXml (withAssets (false)); const auto wet2 = g.note (60); g.close();
        const double diff = nullDb (dry, wet1), same = nullDb (wet1, wet2);
        const double lvl = rmsDb (wet1);
        chk (lvl > -60.0 && diff > -40.0 && same < -100.0,
             "[5] THE EMBEDDED WAVETABLE REACHES THE DSP — the note CHANGES, and two fresh instances agree exactly",
             "note level " + std::to_string (lvl) + " dBFS · against a patch with no assets: " + std::to_string (diff)
             + " dB (must be louder than -40) · two instances carrying the same assets null at " + std::to_string (same) + " dB");
    }

    // ── [6] absent means clear — the next patch inherits nothing ──────────────────────────────
    {
        AU h; if (! h.open()) return 2;
        h.writeXml (withAssets (false));
        const std::string afterA = h.readXml (why);
        h.writeXml (virgin);                                            // a patch that carries NOTHING
        const std::string afterB = h.readXml (why);
        h.close();
        const bool hadA = ! getRootAttr (afterA, "oscAsset0").empty();
        const int left = (int) (! getRootAttr (afterB, "oscAsset0").empty())
                       + (int) (! getRootAttr (afterB, "layerAsset0").empty())
                       + (int) (! getRootAttr (afterB, "irAsset1").empty())
                       + (int) (! getRootAttr (afterB, "wtAsset0").empty());
        const bool want = (mut == "inherit");
        chk (hadA && ((left > 0) == want),
             "[6] A PATCH WITH NO ASSETS LEAVES NOTHING OF THE ONE BEFORE IT (one-shot, layer, IR, wavetable)",
             std::string ("after the loaded patch: ") + (hadA ? "assets present" : "NOT APPLIED")
             + " · after the empty one: " + std::to_string (left) + " of 4 still there"
             + (want ? "   (control expects them inherited)" : ""));
    }

    const int rc = summary();
    std::printf ("  %d pass, %d fail\n", pass, fail);
    return rc;
}
