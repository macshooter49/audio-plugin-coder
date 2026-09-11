// ══ fb632 — THE FILE'S NUMBER, THROUGH THE INSTALLED AU ═══════════════════════════════════════
//    Max: "Michael Myers has no one shots in it but the carry says it has one shots."
//
//    The browser prices a preset from the manifest getStateInformation writes into its <preset>
//    child. This cert pushes a blob through kAudioUnitProperty_ClassInfo on the INSTALLED AU — the
//    door a DAW uses — and reads the <preset carries="…"> the plugin writes back. A green bar here is
//    the number the browser will show.
//
//    CA_MUT=flowblob  the control for bar [7]: expects the dark card blobs to count 3 — the fb635 tile rule makes it fail.
//    CA_MUT=ungated   the control: bar [1] flips to expect the Michael Myers shape to count ONE — a
//                     plugin that correctly counts 0 makes the mutated run fail, which is the point.
//    argv[1]          a payload dir with osc.b64 (Tests/preset_carries_cert --emit) — bar [3] embeds it
#include "au_state_blob.h"
#include <cstdlib>
#include <fstream>
#include <sstream>

static std::string slurp (const std::string& p)
{
    std::ifstream f (p, std::ios::binary); if (! f) return {};
    std::stringstream ss; ss << f.rdbuf(); std::string s = ss.str();
    while (! s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
    return s;
}
// the <preset …/> child's attribute, unescaped
static std::string childAttr (const std::string& xml, const std::string& name)
{
    const size_t at = xml.find ("<preset "); if (at == std::string::npos) return "";
    const size_t end = xml.find (">", at);   if (end == std::string::npos) return "";
    const std::string child = xml.substr (at, end - at);
    const std::string key = " " + name + "=\"";
    const size_t k = child.find (key); if (k == std::string::npos) return "";
    const size_t s = k + key.size(), q = child.find ('"', s); if (q == std::string::npos) return "";
    return xmlUnesc (child.substr (s, q - s));
}
static int smpOf (const std::string& xml)
{
    const std::string j = childAttr (xml, "carries");
    const size_t k = j.find ("\"smp\""); if (k == std::string::npos) return -1;
    const size_t c = j.find (':', k);   if (c == std::string::npos) return -1;
    return std::atoi (j.c_str() + c + 1);
}
static int fvOf (const std::string& xml) { const auto v = childAttr (xml, "fv"); return v.empty() ? -1 : std::atoi (v.c_str()); }
static int carryOf (const std::string& xml, const char* k)   // fb635 — any kind out of <preset carries="…">
{
    const std::string j = childAttr (xml, "carries"), key = std::string ("\"") + k + "\"";
    const size_t at = j.find (key); if (at == std::string::npos) return -1;
    const size_t c = j.find (':', at); if (c == std::string::npos) return -1;
    return std::atoi (j.c_str() + c + 1);
}

// "TRN1" · u32 manifestLen · manifest · u32 chunkLen · chunk(VC2! · u32 len · xml · NUL)
static bool unwrapTerrain (const std::string& path, std::string& xmlOut)
{
    const std::string b = slurp (path);
    if (b.size() < 16 || b.compare (0, 4, "TRN1") != 0) return false;
    auto u32 = [&] (size_t at) { return (uint32_t) (uint8_t) b[at] | ((uint32_t) (uint8_t) b[at+1] << 8) | ((uint32_t) (uint8_t) b[at+2] << 16) | ((uint32_t) (uint8_t) b[at+3] << 24); };
    const size_t ml = u32 (4), cAt = 8 + ml; if (cAt + 4 > b.size()) return false;
    const size_t cl = u32 (cAt), xAt = cAt + 4; if (xAt + cl > b.size() || cl < 8 || b.compare (xAt, 4, "VC2!") != 0) return false;
    const size_t len = u32 (xAt + 4);
    xmlOut = b.substr (xAt + 8, len);
    while (! xmlOut.empty() && xmlOut.back() == '\0') xmlOut.pop_back();
    return ! xmlOut.empty();
}

static const char* FOSSIL = "AirCans_cleaning-spray-single-l-stereoflac_453179.ogg";   // Michael Myers' hint, verbatim: a bare name
static const char* ABS    = "/Volumes/SomeoneElsesDrive/Kicks/Artist Kick.wav";        // an absolute path, missing on this disk

int main (int argc, char** argv)
{
    const char* mutC = std::getenv ("CA_MUT"); const std::string mut = mutC ? mutC : "";
    const std::string PAY = argc > 1 ? argv[1] : "";
    const std::string OSC = PAY.empty() ? "" : slurp (PAY + "/osc.b64");
    std::printf ("══ fb632 THE FILE'S NUMBER, THROUGH THE INSTALLED AU ══   mutation: %s   payload: %s\n",
                 mut.empty() ? "(none)" : mut.c_str(), OSC.empty() ? "(none — bar [3] skipped)" : "osc.b64");

    std::string why;
    AU a; if (! a.open()) return 2;
    const std::string virgin = a.readXml (why);
    if (virgin.empty()) { std::printf ("  readXml: %s\n", why.c_str()); return 2; }
    a.close();

    auto shape = [&] (int eng, bool withAudio, const char* hint = ABS, int modalSrc = -1)
    {
        std::string P = virgin;
        setRootAttr (P, "oscSamplePath0", hint);
        if (withAudio) setRootAttr (P, "oscAsset0", OSC);
        setParam (P, "SYN_OSC_A_ENGINE", (double) eng);
        if (modalSrc >= 0) setParam (P, "SYN_OSC_A_MODAL_SOURCE", (double) modalSrc);
        return P;
    };
    auto roundTrip = [&] (const std::string& P, std::string& out)
    { AU b; if (! b.open()) return false; b.writeXml (P); out = b.readXml (why); b.close(); return ! out.empty(); };

    // ── [1] the Michael Myers shape ───────────────────────────────────────────────────────────
    std::string out;
    {
        roundTrip (shape (5, false, FOSSIL), out);
        const int want = (mut == "ungated") ? 1 : 0;
        chk (smpOf (out) == want,
             "[1] A SAMPLE-NAME HINT ON A HARMONIC OSCILLATOR IS NOT A ONE-SHOT — the installed AU writes smp 0 into its <preset> child",
             "smp=" + std::to_string (smpOf (out)) + " (want " + std::to_string (want) + ")  hint back=\"" + getRootAttr (out, "oscSamplePath0") + "\"  fv=" + std::to_string (fvOf (out)));
    }
    // ── [2] the seven engines, the one-shot EMBEDDED ──────────────────────────────────────────
    if (! OSC.empty())
    {
        static const char* NAME[7] = { "WT", "SAMP", "GRAN", "SPEC", "FM", "HARM", "MODAL" };
        int got[7], hint[7]; std::string detail;
        for (int e = 0; e < 7; ++e)
        {
            roundTrip (shape (e, true), out);  got[e]  = smpOf (out);
            roundTrip (shape (e, false), out); hint[e] = smpOf (out);
            detail += std::string (NAME[e]) + "=" + std::to_string (got[e]) + "/" + std::to_string (hint[e]) + " ";
        }
        bool hintsZero = true; for (int e = 0; e < 7; ++e) hintsZero = hintsZero && hint[e] == 0;
        chk (got[1] == 1 && got[2] == 1 && got[3] == 1 && got[6] == 1 && got[0] == 0 && got[4] == 0 && got[5] == 0 && hintsZero,
             "[2] SAMPLE · GRANULAR · RESYNTH · MODAL(Auto) COUNT THE EMBEDDED ONE-SHOT — WT · FM · HARMONIC DO NOT; a hint alone is 0 on every engine",
             detail + "(embedded/hint-only; virgin modal source=" + std::to_string ((int) getParam (virgin, "SYN_OSC_A_MODAL_SOURCE")) + ")");
        int m[4]; std::string md;
        for (int src = 0; src < 4; ++src) { roundTrip (shape (6, true, ABS, src), out); m[src] = smpOf (out); md += std::to_string (src) + "→" + std::to_string (m[src]) + " "; }
        chk (m[0] == 1 && m[1] == 0 && m[2] == 0 && m[3] == 1,
             "[2b] A MODAL OSCILLATOR COUNTS ITS ONE-SHOT WHEN THE STRIKE IS Auto OR Sample — not on Noise or Click", "source Auto·Noise·Click·Sample: " + md);
    }
    else std::printf ("  SKIP  [2] no payload dir\n");
    // ── [3] embedded audio on a Harmonic oscillator: not counted, still carried ───────────────
    if (! OSC.empty())
    {
        roundTrip (shape (5, true), out);
        const bool kept = getRootAttr (out, "oscAsset0") == OSC;
        chk (smpOf (out) == 0 && kept,
             "[3] EMBEDDED AUDIO ON A HARMONIC OSCILLATOR — not a one-shot, and STILL CARRIED (state persists: switching back to Sample finds it)",
             "smp=" + std::to_string (smpOf (out)) + "  asset survived=" + (kept ? "yes" : "NO"));
        roundTrip (shape (2, true), out);
        chk (smpOf (out) == 1, "[3b] …AND THE SAME AUDIO ON GRANULAR IS ONE ONE-SHOT", "smp=" + std::to_string (smpOf (out)));
    }
    else std::printf ("  SKIP  [3] no payload dir — run Tests/preset_carries_cert --emit <dir> and pass it\n");
    // ── [4] the manifest's format version ─────────────────────────────────────────────────────
    chk (fvOf (out) == 4, "[4] THE <preset> CHILD SAYS fv 4 — a count made with the engine AND the on/off rules (fb635)", "fv=" + std::to_string (fvOf (out)));
    // ── [5] Michael Myers itself, when the file is on this machine ────────────────────────────
    {
        const char* home = std::getenv ("HOME");
        const std::string mm = std::string (home ? home : "") + "/Library/WavesCrate/TerrainInstrument/Banks/User/Michael Myers.terrain";
        std::string xml;
        if (unwrapTerrain (mm, xml))
        {
            roundTrip (xml, out);
            chk (smpOf (out) == 0 && fvOf (out) == 4 && getRootAttr (out, "oscSamplePath0").empty(),
                 "[5] MICHAEL MYERS ITSELF — the file's own chunk through the installed AU: smp 0, fv 4, and the fossil hint is gone from the state",
                 "smp=" + std::to_string (smpOf (out)) + " fv=" + std::to_string (fvOf (out)) + "  A engine=" + std::to_string ((int) getParam (out, "SYN_OSC_A_ENGINE"))
                 + "  hint back=\"" + getRootAttr (out, "oscSamplePath0") + "\"");
        }
        else std::printf ("  SKIP  [5] Michael Myers.terrain is not on this machine\n");
    }
    // ── [6] the fossil dies at the door; an absolute hint keeps its name ──────────────────────
    {
        roundTrip (shape (1, false, FOSSIL), out);
        const std::string fossilBack = getRootAttr (out, "oscSamplePath0"); const int fossilSmp = smpOf (out);
        roundTrip (shape (1, false, ABS), out);
        const std::string absBack = getRootAttr (out, "oscSamplePath0"); const int absSmp = smpOf (out);
        chk (fossilBack.empty() && fossilSmp == 0 && absBack == ABS && absSmp == 0,
             "[6] A BARE-NAME HINT DIES ON LOAD AND NEVER TRAVELS AGAIN — an absolute path that is merely missing keeps its name (and, being a name, counts nothing)",
             "fossil back=\"" + fossilBack + "\" smp=" + std::to_string (fossilSmp) + "  absolute back=\"" + absBack + "\" smp=" + std::to_string (absSmp));
    }
    // ── [7] fb635 — THE FLOW CARDS THAT ARE ON. Max: "I turn the flow cards off. Every time I save it, it says carrying
    //    3 flow cards." 4th Of July's shape: three dice-rolled card blobs (arp · chop · gli) and every tile dark → 0; light
    //    Glitch → 1. CA_MUT=flowblob expects the blob count (3) for the dark case — a counter that counts tiles makes it fail.
    {
        std::string P = virgin;
        setRootAttr (P, "cardStates", "{\"arp\":\"{\\\"cur\\\":1}\",\"chop\":\"{\\\"cur\\\":1}\",\"gli\":\"{\\\"cur\\\":1}\"}");
        bool ok = true; for (int i = 1; i <= 4; ++i) ok = setParam (P, "FLOW_CHAIN_" + std::to_string (i), 0.0) && ok;
        ok = setParam (P, "FLOW_MODE", 0.0) && ok;
        roundTrip (P, out); const int dark = carryOf (out, "flow"); const bool blobsKept = ! getRootAttr (out, "cardStates").empty();
        ok = setParam (P, "FLOW_CHAIN_1", 3.0) && setParam (P, "FLOW_MODE", 3.0) && ok;
        roundTrip (P, out); const int lit = carryOf (out, "flow");
        const int wantDark = (mut == "flowblob") ? 3 : 0;
        chk (ok && dark == wantDark && lit == 1 && blobsKept,
             "[7] A FLOW CARD IS CARRIED WHILE ITS TILE IS LIT — three card blobs with every tile dark carry 0; Glitch lit carries 1; the blobs stay in the state",
             "dark=" + std::to_string (dark) + " (want " + std::to_string (wantDark) + ")  lit=" + std::to_string (lit) + "  blobs kept=" + (blobsKept ? "yes" : "NO")
             + (ok ? "" : "  (a FLOW PARAM was missing from the virgin state)"));
    }
    return summary();
}
