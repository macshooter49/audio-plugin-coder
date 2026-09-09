// ══ fb618 — A PRESET LOADS CLEAN: the two-bar gate from Design/PRESET-SYSTEM-v1.md §5 ═════════════
//   c++ -std=c++17 -O2 -I Tests -I Tests/shim -I Source Tests/preset_null_cert.cpp \
//       -framework Accelerate -framework AudioToolbox -framework CoreFoundation -o preset_null_cert
//   PN_MUT=inherit    the control for bar [3]: expects the previous patch's cards to SURVIVE — must go RED
//   PN_MUT=drift      the control for bar [4]: compares against a SECOND note on the same instance — must go RED
//
// "DSP 100 % correct" is a gate, not a promise. Two bars, both required:
//   1. STATE — the chunk loads into a FRESH instance and comes back out byte-identical, and a chunk that
//      carries preset-owned blobs (cards, routes, curves) does NOT leave them behind when the next chunk
//      lacks them. The second half is the one that bites: setStateInformation treats an absent property
//      as "leave it" for most JSON blobs, which is right for a host restoring into a fresh instance and
//      wrong for preset B loading over preset A.
//   2. AUDIO — the first note after the chunk, on two fresh instances, nulls below -100 dBFS. Only the
//      FIRST note on a FRESH instance is comparable (fb544 continuous phase + fb563 alternator make a
//      second note differ in every sample) — bar [4]'s control proves the cert knows that.
// Everything is driven through kAudioUnitProperty_ClassInfo on the INSTALLED AU — the same door a DAW
// uses — so a green bar here is a green bar in Ableton.
#include "au_state_blob.h"
#include <cstdlib>
#include <cmath>
static int countSub (const std::string& s, const std::string& k) { int n = 0; size_t p = 0; while ((p = s.find (k, p)) != std::string::npos) { ++n; p += k.size(); } return n; }
static std::string firstDiff (const std::string& a, const std::string& b)
{ size_t i = 0; while (i < a.size() && i < b.size() && a[i] == b[i]) ++i;
  if (i == a.size() && i == b.size()) return "identical";
  return "at byte " + std::to_string (i) + ": a[" + a.substr (i, 50) + "]  b[" + b.substr (i, 50) + "]"; }
static double nullDb (const std::vector<float>& a, const std::vector<float>& b)
{ double d = 0, r = 0; const size_t n = std::min (a.size(), b.size());
  for (size_t i = 0; i < n; ++i) { const double e = (double) a[i] - (double) b[i]; d += e * e; r += (double) a[i] * (double) a[i]; }
  if (r <= 0) return 0.0; return 10.0 * std::log10 (d / r + 1e-30); }
static bool rootHas (const std::string& xml, const std::string& name)
{ size_t rs = xml.find ("<Parameters"); size_t re = xml.find ('>', rs); return xml.substr (rs, re - rs).find (" " + name + "=\"") != std::string::npos; }

int main()
{
    const char* mutC = std::getenv ("PN_MUT"); const std::string mut = mutC ? mutC : "";
    std::printf ("══ fb618 A PRESET LOADS CLEAN ══   mutation: %s\n", mut.empty() ? "(none)" : mut.c_str());
    std::string why;

    // ── the preset under test: the virgin blob, with a <preset> child at index 0 ─────────────────
    AU a; if (! a.open()) return 2;
    std::string virgin = a.readXml (why); if (virgin.empty()) { std::printf ("  readXml: %s\n", why.c_str()); return 2; }
    std::string P = virgin;
    { const std::string child = "<preset name=\"Probe\" bank=\"Cert\" author=\"Gate\" type=\"Pad\" styles=\"Dark,Wide\" note=\"first note only\" fv=\"1\"/>";
      size_t at = P.find ('>', P.find ("<Parameters")) + 1; P.insert (at, child); }

    // [1] the child survives a load and a save — once, first ─────────────────────────────────────
    a.writeXml (P); std::string P1 = a.readXml (why);
    { const size_t pre = P1.find ("<preset "), par = P1.find ("<PARAM ");
      chk (countSub (P1, "<preset ") == 1 && pre != std::string::npos && pre < par && P1.find ("bank=\"Cert\"") != std::string::npos,
           "[1] THE <preset> CHILD SURVIVES A LOAD AND A SAVE — ONE COPY, FIRST CHILD, ATTRIBUTES INTACT",
           "<preset ×" + std::to_string (countSub (P1, "<preset ")) + " at byte " + std::to_string (pre) + ", first <PARAM at " + std::to_string (par)
           + (P1.find ("carries=") != std::string::npos ? " · carries written" : " · NO carries attribute")); }

    // [2] a FRESH instance loads P1 and reproduces it byte for byte ──────────────────────────────
    a.close();
    AU b; if (! b.open()) return 2;
    b.writeXml (P1); std::string P2 = b.readXml (why);
    chk (P1 == P2, "[2] A FRESH INSTANCE LOADS THE CHUNK AND SAVES IT BACK BYTE FOR BYTE",
         std::to_string (P1.size()) + " → " + std::to_string (P2.size()) + " bytes · " + firstDiff (P1, P2));

    // [3] no inheritance: A carries cards + a macro name; B (absent) must leave NOTHING of A ──────
    std::string A = P1;
    setRootAttr (A, "cardStates", "{\"probe\":\"{\\\"x\\\":1}\"}");
    setRootAttr (A, "macroNames", "{\"1\":\"ProbeMacro\"}");
    b.writeXml (A); std::string Aout = b.readXml (why);
    const bool aHad = Aout.find ("probe") != std::string::npos && Aout.find ("ProbeMacro") != std::string::npos;
    b.writeXml (P1); std::string Bout = b.readXml (why);          // P1 carries neither property
    const bool cardsLeft = Bout.find ("probe") != std::string::npos, macroLeft = Bout.find ("ProbeMacro") != std::string::npos;
    const bool want = (mut == "inherit");
    chk (aHad && (cardsLeft == want) && (macroLeft == want),
         "[3] A CHUNK WITHOUT A PROPERTY LEAVES NOTHING OF THE PREVIOUS PATCH BEHIND (cards, macro names)",
         std::string ("after A: ") + (aHad ? "present" : "NOT APPLIED") + " · after B: cards " + (cardsLeft ? "INHERITED" : "cleared") + ", macro " + (macroLeft ? "INHERITED" : "cleared") + (want ? "  (control expects inherited)" : ""));

    // [4] the audio null: first note, fresh instance, twice ──────────────────────────────────────
    b.close();
    AU c; if (! c.open()) return 2; c.writeXml (P1); auto n1 = c.note (60);
    std::vector<float> n2;
    if (mut == "drift") { n2 = c.note (60); }                       // the SAME instance's second note — must differ
    else { c.close(); AU d; if (! d.open()) return 2; d.writeXml (P1); n2 = d.note (60); d.close(); }
    const double db = nullDb (n1, n2), lvl = rmsDb (n1);
    chk (lvl > -60.0 && db < -100.0, "[4] THE FIRST NOTE ON TWO FRESH INSTANCES NULLS BELOW -100 dBFS",
         "note level " + std::to_string (lvl) + " dBFS · null " + std::to_string (db) + " dB" + (mut == "drift" ? "  (control: second note on one instance)" : ""));
    if (mut != "drift") c.close();

    const int rc = summary();
    std::printf ("  %d pass, %d fail\n", pass, fail);
    return rc;
}
