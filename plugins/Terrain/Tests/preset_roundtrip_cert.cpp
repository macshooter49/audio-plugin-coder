// ══ fb617 — THE SERIALISER IS A FIXED POINT ═══════════════════════════════════════════════════
//   c++ -std=c++17 -O2 -I Tests -I Tests/shim -I Source Tests/preset_roundtrip_cert.cpp \
//       -framework Accelerate -framework AudioToolbox -framework CoreFoundation -o preset_roundtrip_cert
//   RT_MUT=stale      the control: expects the OLD behaviour (first <layers> wins) — must go RED
//
// Found by the preset-system build recon, measured against the shipping AU: every save appended a
// second <layers> child onto a copyState() that still carried the previous one (1 → 2 → 3, the blob
// grew ~3.1 KB per save), and loadV2State read the FIRST — i.e. the oldest — copy. After one
// reload, every later load of a DAW session restored STALE layer state. A root slicesJson attribute
// also appeared from the second save on, because the guard compared against {"slices":[]} while the
// emitter writes {"slices": []}.
//
// A preset is a state chunk. A state chunk that is not a fixed point cannot be a preset. So this
// gate stands in front of every .terrain that will ever be written: save → load → save must be
// byte-identical, and a session already carrying duplicates must load the NEWEST layers.
#include "au_state_blob.h"
#include <cstdlib>
static int countSub (const std::string& s, const std::string& k) { int n = 0; size_t p = 0; while ((p = s.find (k, p)) != std::string::npos) { ++n; p += k.size(); } return n; }
static std::string firstDiff (const std::string& a, const std::string& b)
{ size_t i = 0; while (i < a.size() && i < b.size() && a[i] == b[i]) ++i;
  if (i == a.size() && i == b.size()) return "identical";
  return "at byte " + std::to_string (i) + ": a[" + a.substr (i, 60) + "]  b[" + b.substr (i, 60) + "]"; }
// the <layers ...>...</layers> element, as one string
static bool layersElement (const std::string& xml, size_t& from, size_t& to)
{ from = xml.find ("<layers"); if (from == std::string::npos) return false;
  to = xml.find ("</layers>", from); if (to == std::string::npos) return false; to += 9; return true; }
// set layer-0's rootMidiNote inside a layers element string
static bool setRoot0 (std::string& el, const std::string& v)
{ const std::string key = "rootMidiNote=\""; size_t at = el.find (key); if (at == std::string::npos) return false;
  size_t vs = at + key.size(), ve = el.find ('"', vs); el.replace (vs, ve - vs, v); return true; }
static std::string root0 (const std::string& el)
{ const std::string key = "rootMidiNote=\""; size_t at = el.find (key); if (at == std::string::npos) return "?";
  size_t vs = at + key.size(), ve = el.find ('"', vs); return el.substr (vs, ve - vs); }
int main()
{
    const char* mutC = std::getenv ("RT_MUT"); const std::string mut = mutC ? mutC : "";
    std::printf ("══ fb617 THE SERIALISER IS A FIXED POINT ══   mutation: %s\n", mut.empty() ? "(none)" : mut.c_str());
    AU au; if (! au.open()) return 2;
    std::string why;
    const std::string x0 = au.readXml (why); if (x0.empty()) { std::printf ("  readXml: %s\n", why.c_str()); return 2; }

    chk (countSub (x0, "<layers") == 1, "[1] THE VIRGIN BLOB CARRIES EXACTLY ONE <layers> CHILD",
         "count " + std::to_string (countSub (x0, "<layers")) + " · " + std::to_string (x0.size()) + " bytes");

    au.writeXml (x0); const std::string x1 = au.readXml (why);
    au.writeXml (x1); const std::string x2 = au.readXml (why);
    chk (x1 == x2 && countSub (x2, "<layers") == 1, "[2] SAVE → LOAD → SAVE IS A FIXED POINT (the second cycle changes nothing)",
         std::to_string (x1.size()) + " → " + std::to_string (x2.size()) + " bytes · <layers ×" + std::to_string (countSub (x2, "<layers")) + " · " + firstDiff (x1, x2));
    chk (x0 == x1, "[3] LOADING A BLOB AND SAVING IT AGAIN REPRODUCES IT BYTE FOR BYTE",
         std::to_string (x0.size()) + " → " + std::to_string (x1.size()) + " bytes · " + firstDiff (x0, x1));
    // ROOT attribute only — every <layer> node carries its own slicesJson, which is correct
    auto rootHas = [] (const std::string& xml, const std::string& name) { size_t rs = xml.find ("<Parameters"); size_t re = xml.find ('>', rs); return xml.substr (rs, re - rs).find (" " + name + "=\"") != std::string::npos; };
    chk (! rootHas (x1, "slicesJson") && ! rootHas (x2, "slicesJson"),
         "[4] AN EMPTY SLICE LIST DOES NOT ECHO A ROOT slicesJson (the guard matches its own emitter)",
         std::string ("root attribute after 1 cycle: ") + (rootHas (x1, "slicesJson") ? "present" : "absent") + " · after 2: " + (rootHas (x2, "slicesJson") ? "present" : "absent"));

    // a session saved by the pre-fix build: two <layers> children, the STALE one first
    size_t f, t; std::string xd = x0; bool ok = layersElement (xd, f, t);
    std::string real = ok ? xd.substr (f, t - f) : ""; std::string stale = real;
    ok = ok && setRoot0 (stale, "33") && setRoot0 (real, "45");
    if (ok) xd.replace (f, t - f, stale + real);
    au.writeXml (xd); const std::string xf = au.readXml (why);
    size_t ff, ft; std::string got = layersElement (xf, ff, ft) ? xf.substr (ff, ft - ff) : "";
    const std::string want = (mut == "stale") ? "33" : "45";
    chk (ok && countSub (xf, "<layers") == 1 && root0 (got) == want,
         "[5] A PRE-FIX SESSION WITH TWO <layers> LOADS THE NEWEST AND SAVES ONE",
         "fed <layers ×" + std::to_string (countSub (xd, "<layers")) + " (stale root 33, newest 45) → saved ×" + std::to_string (countSub (xf, "<layers")) + " with root " + root0 (got) + " (want " + want + ")");
    au.close();
    const int rc = summary();
    std::printf ("  %d pass, %d fail\n", pass, fail);
    return rc;
}
