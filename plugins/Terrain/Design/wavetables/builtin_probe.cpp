// ══════════════════════════════════════════════════════════════════════════════════════════════
//  builtin_probe.cpp — dump Terrain's 46 BUILT-IN (procedural) wavetables exactly as the plugin
//  bakes them, so the generated FLAC tables can be PROVEN distinct from them.
//
//  The factory library is 46 built-ins + 454 FLAC. The FLAC half is on disk; the built-in half is
//  not — it is baked at run time from WavetableBank::specForPreset(). This probe runs that bake and
//  writes the result in the FLAC bank's own layout so the Python side can fingerprint it with
//  wtlib exactly like any other table (see builtin_tables.py).
//
//  THE PATH IS THE PLUGIN'S OWN, END TO END — nothing here re-implements a table:
//    WavetableBank::ensureBuilt(i) -> buildOne -> Wavetable::buildFromSpec(specForPreset(i))
//    -> getTableIfBuilt(i) -> renderBlend(mip 0, WT POS on frame f exactly, blur 0)
//  renderBlend's blur-0 branch is the voice's own frame read: s[f0]*(1-fFrac) + s[f1]*fFrac. At a
//  WT POS whose frame index comes out EXACT in renderBlend's own float arithmetic (found and
//  checked below, never assumed) fFrac is 0 and it returns frame f of mip level 0 — the full-band
//  level, kMipMaxHarmonics[0] = 1023 harmonics — bit for bit. lookup() is a second, independent
//  public read of the same samples and must agree on every one of them, or the probe fails.
//  No `#define private public`: Wavetable::sample() is private and stays that way.
//
//  NAMES AND CATEGORIES ARE NOT RETYPED. They are parsed at run time from PluginProcessor.cpp —
//  the SYN_OSC_A_WT_PRESET StringArray (fb601's one roster) and kWtBuiltinCat[] / kWtCats[] /
//  the CAT_* enum — so this file cannot become an eleventh site of the ten-site law. If the roster
//  and WavetableBank::kNumPresets ever disagree the probe refuses to write anything.
//
//  Output (same layout as gate.py's .wav and the Serum/Vital importer condition):
//    <out>/<idx>.wav     float32 mono 44.1 kHz, 2048 samples per frame, frames concatenated
//    <out>/index.json    [{"idx", "name", "category", "frames"}, ...]   46 entries, idx order
//
//  Build + run (from this directory; the ABSOLUTE source path lets the probe find
//  ../../Source/PluginProcessor.cpp from __FILE__ wherever the binary is run — or pass it as argv[2]):
//    c++ -std=c++17 -O2 -I ../../Tests/shim -I ../../Source "$PWD/builtin_probe.cpp" \
//        -framework Accelerate -o /tmp/builtin_probe && /tmp/builtin_probe <out-dir>
//    python3 builtin_tables.py <out-dir>        # verify + the harm60/span board at 128 frames
// ══════════════════════════════════════════════════════════════════════════════════════════════
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <cerrno>
#include <string>
#include <vector>
#include <memory>
#include <fstream>
#include <sstream>
#include <regex>
#include <sys/stat.h>
#include "WavetableBank.h"

using namespace tw;

static_assert (__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__, "the WAV writer stores host-order words");

namespace
{
    std::string slurp (const std::string& p)
    {
        std::ifstream f (p, std::ios::binary);
        if (! f) return {};
        std::stringstream ss; ss << f.rdbuf(); return ss.str();
    }

    // Remove // and /* */ comments from a fragment that starts OUTSIDE a string literal. The
    // roster's comments contain quoted words ("the analog super-stack…"), so this must run before
    // any string is collected.
    std::string stripComments (const std::string& s)
    {
        std::string out; out.reserve (s.size());
        bool inStr = false;
        for (size_t i = 0; i < s.size(); ++i)
        {
            const char c = s[i];
            if (inStr)
            {
                out += c;
                if (c == '\\' && i + 1 < s.size()) out += s[++i];
                else if (c == '"') inStr = false;
                continue;
            }
            if (c == '"') { inStr = true; out += c; continue; }
            if (c == '/' && i + 1 < s.size() && s[i + 1] == '/')
            { while (i < s.size() && s[i] != '\n') ++i; out += '\n'; continue; }
            if (c == '/' && i + 1 < s.size() && s[i + 1] == '*')
            { i += 2; while (i + 1 < s.size() && ! (s[i] == '*' && s[i + 1] == '/')) ++i; ++i; out += ' '; continue; }
            out += c;
        }
        return out;
    }

    // The body between the first '{' at/after `anchor` (searched from `from`) and its MATCHING '}',
    // comments removed. Braces inside string literals are skipped. Empty string on any failure.
    std::string braceBody (const std::string& src, const std::string& anchor, size_t from = 0)
    {
        const size_t a = src.find (anchor, from);
        if (a == std::string::npos) return {};
        const size_t b = src.find ('{', a);
        if (b == std::string::npos) return {};
        const std::string s = stripComments (src.substr (b, 32768));
        int depth = 0; bool inStr = false;
        for (size_t i = 0; i < s.size(); ++i)
        {
            const char c = s[i];
            if (inStr) { if (c == '\\') ++i; else if (c == '"') inStr = false; continue; }
            if (c == '"') { inStr = true; continue; }
            if (c == '{') ++depth;
            else if (c == '}' && --depth == 0) return s.substr (1, i - 1);
        }
        return {};
    }

    std::vector<std::string> quoted (const std::string& body)
    {
        std::vector<std::string> v;
        static const std::regex re ("\"((?:[^\"\\\\]|\\\\.)*)\"");
        for (auto it = std::sregex_iterator (body.begin(), body.end(), re); it != std::sregex_iterator(); ++it)
            v.push_back ((*it)[1].str());
        return v;
    }

    std::vector<std::string> catIdents (const std::string& body)
    {
        std::vector<std::string> v;
        static const std::regex re ("\\b(CAT_[A-Z0-9_]+)\\b");
        for (auto it = std::sregex_iterator (body.begin(), body.end(), re); it != std::sregex_iterator(); ++it)
            v.push_back ((*it)[1].str());
        return v;
    }

    std::string jsonStr (const std::string& s)
    {
        std::string o = "\"";
        for (unsigned char c : s)
        {
            if (c == '"' || c == '\\') { o += '\\'; o += (char) c; }
            else if (c < 0x20) { char b[8]; std::snprintf (b, sizeof b, "\\u%04x", c); o += b; }
            else o += (char) c;
        }
        return o + "\"";
    }

    bool mkdirs (const std::string& d)
    {
        std::string cur;
        for (size_t i = 0; i <= d.size(); ++i)
        {
            if ((i == d.size() || d[i] == '/') && ! cur.empty() && cur != "/")
                if (::mkdir (cur.c_str(), 0755) != 0 && errno != EEXIST) return false;
            if (i < d.size()) cur += d[i];
        }
        return true;
    }

    // Byte-for-byte the layout wtlib.write_wav writes: WAVE_FORMAT_IEEE_FLOAT, mono, 44100, 32-bit,
    // with a 'fact' chunk.
    bool writeWav (const std::string& path, const std::vector<float>& x)
    {
        std::FILE* fp = std::fopen (path.c_str(), "wb");
        if (fp == nullptr) return false;
        auto u32 = [fp] (std::uint32_t v) { std::fwrite (&v, 4, 1, fp); };
        auto u16 = [fp] (std::uint16_t v) { std::fwrite (&v, 2, 1, fp); };
        const std::uint32_t dataBytes = (std::uint32_t) (x.size() * 4);
        std::fwrite ("RIFF", 1, 4, fp); u32 (4 + (8 + 16) + (8 + 4) + (8 + dataBytes)); std::fwrite ("WAVE", 1, 4, fp);
        std::fwrite ("fmt ", 1, 4, fp); u32 (16); u16 (3); u16 (1); u32 (44100); u32 (44100 * 4); u16 (4); u16 (32);
        std::fwrite ("fact", 1, 4, fp); u32 (4); u32 ((std::uint32_t) x.size());
        std::fwrite ("data", 1, 4, fp); u32 (dataBytes);
        const size_t w = std::fwrite (x.data(), 4, x.size(), fp);
        return std::fclose (fp) == 0 && w == x.size();
    }

    // A WT POS at which renderBlend's own arithmetic — clamp to [0,1], then pos * (float)(F-1) —
    // lands EXACTLY on frame f, so its crossfade weight is 0. NaN if no such float exists nearby.
    float exactPos (int f, int F)
    {
        if (F < 2) return 0.0f;
        float p = (float) f / (float) (F - 1);
        for (int k = 0; k < 64; ++k)
        {
            const float pc   = juce::jlimit (0.0f, 1.0f, p);
            const float fIdx = pc * (float) (F - 1);
            if (fIdx == (float) f && (int) fIdx == f) return pc;
            p = std::nextafter (p, fIdx < (float) f ? 2.0f : -1.0f);
        }
        return std::nanf ("");
    }
}

int main (int argc, char** argv)
{
    const std::string out = (argc > 1) ? argv[1] : "builtins";
    std::string here = __FILE__;
    here = (here.find_last_of ('/') == std::string::npos) ? std::string (".") : here.substr (0, here.find_last_of ('/'));
    const std::string procPath = (argc > 2) ? argv[2] : here + "/../../Source/PluginProcessor.cpp";

    std::printf ("\n══ builtin_probe ══ the %d built-in tables, baked by the plugin's own WavetableBank\n",
                 (int) WavetableBank::kNumPresets);
    std::printf ("   roster source: %s\n   out: %s\n\n", procPath.c_str(), out.c_str());

    // ── the roster, parsed (never retyped) ────────────────────────────────────────────────────
    const std::string proc = slurp (procPath);
    if (proc.empty()) { std::fprintf (stderr, "!! cannot read %s\n", procPath.c_str()); return 2; }
    const size_t at = proc.find ("ParameterIDs::SYN_OSC_A_WT_PRESET, 1");
    if (at == std::string::npos) { std::fprintf (stderr, "!! SYN_OSC_A_WT_PRESET ParameterID not found\n"); return 2; }
    const std::vector<std::string> names    = quoted (braceBody (proc, "juce::StringArray", at));
    const std::vector<std::string> catNames = quoted (braceBody (proc, "kWtCats[] ="));
    const std::string              enumBody = braceBody (proc, "enum { CAT_BASIC");
    const std::vector<std::string> enumIds  = catIdents (enumBody);
    const std::vector<std::string> catTok   = catIdents (braceBody (proc, "kWtBuiltinCat[] ="));

    const int P = (int) WavetableBank::kNumPresets;
    bool rosterOk = true;
    auto need = [&] (bool c, const char* what) { if (! c) { std::fprintf (stderr, "!! roster: %s\n", what); rosterOk = false; } };
    need ((int) names.size() == P,       "SYN_OSC_A_WT_PRESET StringArray size != WavetableBank::kNumPresets");
    need ((int) catTok.size() == P,      "kWtBuiltinCat[] size != WavetableBank::kNumPresets");
    need (! catNames.empty() && enumIds.size() == catNames.size(), "CAT_* enum and kWtCats[] disagree in length");
    // The enum is implicit-valued after CAT_BASIC = 0, so declaration order IS the value. Refuse a
    // second '=' rather than silently mis-map a category.
    need (enumBody.find ("CAT_BASIC = 0") != std::string::npos
          && std::count (enumBody.begin(), enumBody.end(), '=') == 1, "CAT_* enum is not the implicit 0..n-1 form");
    std::vector<std::string> category ((size_t) std::max (0, P));
    for (int i = 0; rosterOk && i < P; ++i)
    {
        const auto it = std::find (enumIds.begin(), enumIds.end(), catTok[(size_t) i]);
        if (it == enumIds.end()) { need (false, "kWtBuiltinCat[] names a CAT_* that is not in the enum"); break; }
        category[(size_t) i] = catNames[(size_t) (it - enumIds.begin())];
    }
    if (! rosterOk) return 3;
    std::printf ("   roster: %zu names · %zu category tokens · %zu categories  (kNumPresets %d)  OK\n\n",
                 names.size(), catTok.size(), catNames.size(), P);

    if (! mkdirs (out)) { std::fprintf (stderr, "!! cannot create %s\n", out.c_str()); return 2; }

    // ── bake + dump ──────────────────────────────────────────────────────────────────────────
    auto bank = std::make_unique<WavetableBank>();   // ~350 MB once all 46 are built; heap, not stack
    std::vector<float> cyc ((size_t) Wavetable::kFrameSize), all;
    std::string json = "[\n";
    int fails = 0;

    std::printf ("  %3s  %-16s %-13s %6s %5s %9s %9s %5s %9s  %s\n",
                 "idx", "name", "category", "frames", "mips", "pk min", "pk max", "dead", "lk-diff", "");
    std::printf ("  %s\n", std::string (96, '-').c_str());
    for (int i = 0; i < P; ++i)
    {
        bank->ensureBuilt (i);
        const Wavetable* w = bank->getTableIfBuilt (i);
        if (w == nullptr) { std::printf ("  %3d  *** NOT BUILT ***\n", i); ++fails; continue; }

        const int F = w->getNumFrames(), N = w->getFrameSize();
        all.assign ((size_t) F * (size_t) N, 0.0f);
        cyc.assign ((size_t) N, 0.0f);
        int dead = 0, lkDiff = 0, badPos = 0; bool finite = true;
        double pkMin = 1e30, pkMax = 0.0;
        for (int f = 0; f < F; ++f)
        {
            const float pos = exactPos (f, F);
            if (std::isnan (pos)) { ++badPos; continue; }
            w->renderBlend (0, pos, 0.0f, cyc.data());
            double pk = 0.0;
            for (int n = 0; n < N; ++n)
            {
                const float v = cyc[(size_t) n];
                if (! std::isfinite (v)) finite = false;
                // second, independent public read of the same sample (phase n/N is exact: N = 2^11)
                if (w->lookup (0, pos, (float) n / (float) N) != v) ++lkDiff;
                pk = std::max (pk, (double) std::fabs (v));
                all[(size_t) f * (size_t) N + (size_t) n] = v;
            }
            if (pk <= 1e-9) ++dead;
            pkMin = std::min (pkMin, pk); pkMax = std::max (pkMax, pk);
        }

        const std::string path = out + "/" + std::to_string (i) + ".wav";
        const bool wrote = writeWav (path, all);
        const bool ok = wrote && finite && dead == 0 && lkDiff == 0 && badPos == 0
                        && N == Wavetable::kFrameSize && F >= 2 && F <= Wavetable::kMaxFrames;
        if (! ok) ++fails;
        std::printf ("  %3d  %-16s %-13s %6d %5d %9.5f %9.5f %5d %9d  %s\n", i, names[(size_t) i].c_str(),
                     category[(size_t) i].c_str(), F, w->getNumMipLevels(), pkMin, pkMax, dead, lkDiff,
                     ok ? "ok" : (! wrote ? "*** WRITE FAILED ***" : ! finite ? "*** NON-FINITE ***"
                                  : dead ? "*** SILENT FRAME ***" : badPos ? "*** NO EXACT WT POS ***"
                                  : "*** READ MISMATCH ***"));

        json += "  {\"idx\": " + std::to_string (i) + ", \"name\": " + jsonStr (names[(size_t) i])
              + ", \"category\": " + jsonStr (category[(size_t) i]) + ", \"frames\": " + std::to_string (F) + "}"
              + (i + 1 < P ? ",\n" : "\n");
    }
    json += "]\n";

    const std::string ipath = out + "/index.json";
    std::FILE* jf = std::fopen (ipath.c_str(), "wb");
    if (jf == nullptr || std::fwrite (json.data(), 1, json.size(), jf) != json.size()) ++fails;
    if (jf != nullptr) std::fclose (jf);

    std::printf ("\n   %d tables -> %s/<idx>.wav + index.json   %s\n\n", P, out.c_str(),
                 fails == 0 ? "ALL OK" : (std::to_string (fails) + " FAILED").c_str());
    return fails == 0 ? 0 : 1;
}
