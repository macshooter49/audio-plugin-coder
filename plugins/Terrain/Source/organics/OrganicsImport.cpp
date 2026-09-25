// OrganicsImport.cpp — tp108: the user SoundFont import (see OrganicsImport.h for the scope and the laws).
//
// Section map (each mirrors a piece of the offline compiler, named after it so a reader can diff the two):
//   1  utilities                       (numbers, note names, text, paths)
//   2  SFZ parser                      sfz.py  SfzParser
//   3  SF2 / SF3 hydra                 sf2.py  SF2 + parse()
//   4  interpretation                  torgc.py Interpreter.interpret / _vel_curve
//   5  mapping operations              torgc.py layerize / fill_vel_holes / fill_holes / repair_rr
//   6  audio: one output sample        torgc.py job_render_sample (minus the tail-loop search and the loop polish)
//   7  pitch: measure_f0 + tfix        analyse.py measure_f0 · torgc.py Compiler.compute_tfix
//   8  map assembly                    torgc.py Compiler.assemble
//   9  calibration + preview           torgc.py calibrate_and_preview · analyse.py Renderer / loudness_k
//  10  the User/ folder                ids / index / finalise / delete
//  11  the import                      orchestration
//  12  background jobs
#include "OrganicsImport.h"
#include "OrganicsLibrary.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_events/juce_events.h>

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <map>
#include <memory>
#include <set>
#include <stdexcept>
#include <tuple>
#include <vector>

namespace tw
{
namespace orgimport
{
namespace
{
    //==============================================================================================================
    //  1 · utilities
    //==============================================================================================================
    constexpr double kPi = 3.14159265358979323846;
    constexpr double kHop = 0.010;                          // analyse.HOP_S
    constexpr double kPeakTarget = 0.96605087898981;        // 10^(−0.3/20) (torgc.PEAK_TARGET)
    constexpr double kCalibLufs = -24.0, kPeakCeilDb = -1.0, kVeloDefault = 0.75;
    constexpr int    kLoopPad = 8;                          // analyse.LOOP_PAD
    constexpr int    kMaxRegions = 65000;
    constexpr int64_t kMaxFrames = (int64_t) 1 << 30;

    struct Fail : std::runtime_error
    {
        explicit Fail (const juce::String& s) : std::runtime_error (s.toStdString()) {}
    };
    juce::String why (const std::exception& e) { return juce::String::fromUTF8 (e.what()); }

    using Op = std::map<juce::String, juce::String>;
    using juce::juce_wchar;

    bool parseNum (const juce::String& s0, double& out)
    {
        const auto s = s0.trim();
        if (s.isEmpty()) return false;
        const std::string t = s.toStdString();
        char* end = nullptr;
        const double v = std::strtod (t.c_str(), &end);
        if (end == t.c_str()) return false;
        while (*end == ' ' || *end == '\t') ++end;
        if (*end != 0 || ! std::isfinite (v)) return false;
        out = v; return true;
    }
    const juce::String* get (const Op& r, const juce::String& k)
    {
        auto it = r.find (k);
        return it == r.end() ? nullptr : &it->second;
    }
    bool has (const Op& r, const juce::String& k) { return r.find (k) != r.end(); }
    double fnum (const Op& r, const juce::String& k, double def)          // torgc._f
    {
        const auto* v = get (r, k);
        if (v == nullptr || v->isEmpty()) return def;
        double x; return parseNum (*v, x) ? x : def;
    }
    double round4 (double x) { return std::round (x * 1.0e4) / 1.0e4; }
    double roundN (double x, int n) { const double m = std::pow (10.0, n); return std::round (x * m) / m; }
    double dbOf (double v) { return v > 1.0e-12 ? 20.0 * std::log10 (v) : -200.0; }
    double midiHz (double n) { return 440.0 * std::pow (2.0, (n - 69.0) / 12.0); }

    /** "60" / "c4" / "C#4" / "db4" → MIDI (C4 = 60). false when not a note. sfz.note_to_midi */
    bool noteToMidi (const juce::String& v, int noteOff, int octOff, int& out)
    {
        const auto s = v.trim();
        if (s.isEmpty()) return false;
        double d;
        if (parseNum (s, d)) { out = (int) d + noteOff + 12 * octOff; return true; }   // int(float(s)): truncation
        auto p = s.getCharPointer();
        const juce_wchar c0 = juce::CharacterFunctions::toLowerCase (p.getAndAdvance());
        static const int base[] = { 9, 11, 0, 2, 4, 5, 7 };                             // a b c d e f g
        if (c0 < 'a' || c0 > 'g') return false;
        int n = base[c0 - 'a'];
        juce::String rest (p);
        if (rest.startsWithChar ('#') || rest.startsWithChar (0x266F)) { n += 1; rest = rest.substring (1); }
        else if ((rest.startsWithChar ('b') || rest.startsWithChar (0x266D)) && rest.length() > 1) { n -= 1; rest = rest.substring (1); }
        if (rest.isEmpty()) return false;
        const bool neg = rest.startsWithChar ('-');
        const auto digits = neg ? rest.substring (1) : rest;
        if (digits.isEmpty() || ! digits.containsOnly ("0123456789")) return false;
        const int oct = digits.getIntValue() * (neg ? -1 : 1);
        out = (oct + 1) * 12 + n + noteOff + 12 * octOff;
        return true;
    }
    juce::String noteName (int n)
    {
        static const char* nm[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        return juce::String (nm[((n % 12) + 12) % 12]) + juce::String (n / 12 - 1);
    }

    /** A text file as UTF-8 (BOM stripped), else Latin-1 — sfz.SfzParser._read. */
    juce::String readText (const juce::File& f)
    {
        juce::MemoryBlock mb;
        if (! f.loadFileAsData (mb)) throw Fail ("Cannot read " + f.getFullPathName());
        auto* d = static_cast<const char*> (mb.getData());
        size_t n = mb.getSize();
        if (n >= 3 && (uint8_t) d[0] == 0xEF && (uint8_t) d[1] == 0xBB && (uint8_t) d[2] == 0xBF) { d += 3; n -= 3; }
        if (juce::CharPointer_UTF8::isValidString (d, (int) n)) return juce::String::fromUTF8 (d, (int) n);
        std::vector<juce_wchar> w (n + 1, 0);
        for (size_t i = 0; i < n; ++i) w[i] = (juce_wchar) (uint8_t) d[i];
        return juce::String (juce::CharPointer_UTF32 (w.data()));
    }
    juce::String fromW (const std::vector<juce_wchar>& v, size_t a, size_t b)      // [a, b) of a wide buffer
    {
        std::vector<juce_wchar> t (v.begin() + (std::ptrdiff_t) a, v.begin() + (std::ptrdiff_t) b);
        t.push_back (0);
        return juce::String (juce::CharPointer_UTF32 (t.data()));
    }
    std::vector<juce_wchar> toW (const juce::String& s)
    {
        std::vector<juce_wchar> v;
        for (auto p = s.getCharPointer(); ! p.isEmpty(); ++p) v.push_back (*p);
        return v;
    }

    bool isIdentStart (juce_wchar c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'; }
    bool isAsciiAlnum (juce_wchar c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'); }

    /** base + "/"-separated relative path with ".." / "." walked, then a case-insensitive fallback (sfz._ci_path). */
    juce::File resolvePath (const juce::File& base, juce::String rel)
    {
        rel = rel.trim().replaceCharacter ('\\', '/');
        if (rel.isEmpty()) return {};
        if (juce::File::isAbsolutePath (rel)) return juce::File (rel);
        auto parts = juce::StringArray::fromTokens (rel, "/", "");
        juce::File cur = base;
        for (auto& p : parts)
        {
            if (p.isEmpty() || p == ".") continue;
            if (p == "..") { cur = cur.getParentDirectory(); continue; }
            cur = cur.getChildFile (p);
        }
        if (cur.exists()) return cur;
        juce::File ci = base;
        for (auto& p : parts)
        {
            if (p.isEmpty() || p == ".") continue;
            if (p == "..") { ci = ci.getParentDirectory(); continue; }
            auto direct = ci.getChildFile (p);
            if (direct.exists()) { ci = direct; continue; }
            bool found = false;
            for (const auto& e : juce::RangedDirectoryIterator (ci, false, "*", juce::File::findFilesAndDirectories))
                if (e.getFile().getFileName().equalsIgnoreCase (p)) { ci = e.getFile(); found = true; break; }
            if (! found) return cur;
        }
        return ci;
    }

    //==============================================================================================================
    //  2 · SFZ parser (sfz.py)
    //==============================================================================================================
    struct SfzFile
    {
        juce::File path;
        std::vector<Op> regions;
        Op control;
        std::map<int, std::vector<double>> curves;
        std::map<int, double> cc;
        juce::Array<juce::File> includes;
        int missingIncludes = 0;

        double ccValue (int n) const
        {
            auto it = cc.find (n);
            if (it != cc.end()) return it->second;
            if (n == 7) return 100.0;
            if (n == 10) return 64.0;
            if (n == 11) return 127.0;
            return 0.0;
        }
    };

    std::vector<double> interpPoints (std::map<int, double> pts)          // sfz._interp_points
    {
        if (pts.find (0) == pts.end()) pts[0] = 0.0;
        if (pts.find (127) == pts.end()) pts[127] = 1.0;
        std::vector<int> ks; for (auto& p : pts) ks.push_back (p.first);
        std::vector<double> out; out.reserve (128);
        size_t j = 0;
        for (int v = 0; v < 128; ++v)
        {
            while (j + 1 < ks.size() && ks[j + 1] < v) ++j;
            const int a = ks[j], b = ks[std::min (j + 1, ks.size() - 1)];
            if (v <= a || b == a) out.push_back (pts[a]);
            else { const double t = (double) (v - a) / (double) (b - a); out.push_back (pts[a] + t * (pts[b] - pts[a])); }
        }
        return out;
    }

    bool ccNum (const juce::String& s, int& n)
    {
        if (s.isEmpty() || ! s.containsOnly ("0123456789")) return false;
        n = s.getIntValue(); return true;
    }

    class SfzParser
    {
    public:
        explicit SfzParser (const juce::File& f) : rootPath (f), rootDir (f.getParentDirectory()) { out.path = f; }

        SfzFile parse()
        {
            parseFile (rootPath);
            closeRegion(); closeCurve();
            out.control = control;
            return std::move (out);
        }

    private:
        juce::File rootPath, rootDir, curFile;
        SfzFile out;
        std::map<juce::String, juce::String> defines;
        juce::String level;
        Op control, glob, master, group;
        std::unique_ptr<Op> region, curve;
        juce::String defaultPath;
        int noteOffset = 0, octaveOffset = 0, depth = 0;
        int64_t textBytes = 0;

        /** The file's lines with comments removed: a block comment becomes ONE space and swallows its newlines (sfz.py
            strips block comments over the whole text first), then "//" cuts to the end of its line. */
        static juce::StringArray commentFreeLines (const juce::String& text)
        {
            const auto w = toW (text);
            juce::StringArray out;
            std::vector<juce_wchar> cur;
            auto flush = [&] { cur.push_back (0); out.add (juce::String (juce::CharPointer_UTF32 (cur.data()))); cur.clear(); };
            bool lineCmt = false;
            for (size_t i = 0; i < w.size(); ++i)
            {
                const juce_wchar c = w[i];
                if (! lineCmt && c == '/' && i + 1 < w.size() && w[i + 1] == '*')
                {
                    size_t e = i + 2;
                    while (e + 1 < w.size() && ! (w[e] == '*' && w[e + 1] == '/')) ++e;
                    if (e + 1 >= w.size()) { i = w.size(); break; }
                    cur.push_back (' ');
                    i = e + 1;
                    continue;
                }
                if (c == '\r' || c == '\n')
                {
                    if (c == '\r' && i + 1 < w.size() && w[i + 1] == '\n') ++i;
                    lineCmt = false; flush(); continue;
                }
                if (lineCmt) continue;
                if (c == '/' && i + 1 < w.size() && w[i + 1] == '/') { lineCmt = true; continue; }
                cur.push_back (c);
            }
            flush();
            return out;
        }

        juce::String subst (const juce::String& s) const
        {
            if (! s.containsChar ('$') || defines.empty()) return s;
            juce::String r;
            auto p = s.getCharPointer();
            while (! p.isEmpty())
            {
                const juce_wchar c = *p;
                if (c != '$') { r += juce::String::charToString (c); ++p; continue; }
                // $NAME
                juce::String name ("$"); ++p;
                while (! p.isEmpty() && (isAsciiAlnum (*p) || *p == '_')) { name += juce::String::charToString (*p); ++p; }
                auto it = defines.find (name);
                if (it != defines.end()) { r += it->second; continue; }
                // the longest defined prefix (sfizz's "$VAR_suffix" glue)
                juce::String best; juce::String bestVal;
                for (auto& d : defines)
                    if (name.startsWith (d.first) && d.first.length() > best.length()) { best = d.first; bestVal = d.second; }
                if (best.isNotEmpty()) r += bestVal + name.substring (best.length());
                else r += name;
            }
            return r;
        }

        void parseFile (const juce::File& f)
        {
            if (++depth > 32) throw Fail ("#include nesting is too deep at " + f.getFullPathName());
            if (f.getSize() > 64 * 1024 * 1024) throw Fail (f.getFileName() + " is too large to be an SFZ file");
            textBytes += f.getSize();
            if (textBytes > 256 * 1024 * 1024) throw Fail ("the SFZ includes more than 256 MB of text");
            const auto prev = curFile;
            curFile = f;
            out.includes.add (f);
            const auto lines = commentFreeLines (readText (f));
            for (auto& l : lines) parseLine (l);
            curFile = prev;
            --depth;
        }

        void parseLine (const juce::String& line)
        {
            if (line.trim().isEmpty()) return;
            {
                const auto t = line.trimStart();
                if (t.startsWith ("#define"))
                {
                    auto rest = t.substring (7);
                    if (rest.isNotEmpty() && juce::CharacterFunctions::isWhitespace (rest[0]))
                    {
                        rest = rest.trimStart();
                        if (rest.startsWithChar ('$'))
                        {
                            int k = 1;
                            while (k < rest.length() && (isAsciiAlnum (rest[k]) || rest[k] == '_')) ++k;
                            if (k > 1 && (k == rest.length() || juce::CharacterFunctions::isWhitespace (rest[k])))
                            {
                                const auto val = rest.substring (k).trim();
                                if (val.isNotEmpty()) { defines[rest.substring (0, k)] = subst (val); return; }
                            }
                        }
                    }
                }
            }
            int pos = 0;
            for (;;)
            {
                const int inc = line.indexOf (pos, "#include");
                if (inc < 0) break;
                int q = inc + 8;
                while (q < line.length() && juce::CharacterFunctions::isWhitespace (line[q])) ++q;
                if (q <= inc + 8 || q >= line.length() || line[q] != '"') { parseSegment (line.substring (pos, inc + 8)); pos = inc + 8; continue; }
                const int q2 = line.indexOfChar (q + 1, '"');
                if (q2 < 0) break;
                parseSegment (line.substring (pos, inc));
                const auto rel = subst (line.substring (q + 1, q2)).replaceCharacter ('\\', '/');
                auto p = resolvePath (rootDir, rel);
                if (! p.existsAsFile())
                {
                    const auto alt = resolvePath (curFile.getParentDirectory(), rel);
                    if (alt.existsAsFile()) p = alt;
                }
                if (p.existsAsFile()) parseFile (p);
                else ++out.missingIncludes;
                pos = q2 + 1;
            }
            parseSegment (line.substring (pos));
        }

        void parseSegment (const juce::String& seg0)
        {
            const auto segS = subst (seg0);
            if (segS.trim().isEmpty()) return;
            const auto seg = toW (segS);
            struct Tok { size_t s, e; bool header; juce::String name; };
            std::vector<Tok> toks;
            const size_t n = seg.size();
            // headers <word>
            for (size_t i = 0; i < n; ++i)
                if (seg[i] == '<')
                {
                    size_t j = i + 1;
                    while (j < n && (isAsciiAlnum (seg[j]) || seg[j] == '_')) ++j;
                    if (j > i + 1 && j < n && seg[j] == '>') { toks.push_back ({ i, j + 1, true, fromW (seg, i + 1, j).toLowerCase() }); i = j; }
                }
            // opcodes name= (the leftmost identifier start in the run before '=', like the regex scan)
            for (size_t i = 0; i < n; ++i)
            {
                if (seg[i] != '=') continue;
                size_t k = i;
                while (k > 0 && (isAsciiAlnum (seg[k - 1]) || seg[k - 1] == '_' || seg[k - 1] == '$')) --k;
                while (k < i && ! isIdentStart (seg[k])) ++k;
                if (k >= i) continue;
                bool inHeader = false;
                for (auto& t : toks) if (t.header && t.s <= k && k < t.e) { inHeader = true; break; }
                if (inHeader) continue;
                toks.push_back ({ k, i + 1, false, fromW (seg, k, i) });
            }
            std::sort (toks.begin(), toks.end(), [] (const Tok& a, const Tok& b) { return a.s < b.s; });
            for (size_t t = 0; t < toks.size(); ++t)
            {
                if (toks[t].header) { header (toks[t].name); continue; }
                const size_t nxt = t + 1 < toks.size() ? toks[t + 1].s : n;
                opcode (toks[t].name, fromW (seg, toks[t].e, std::max (toks[t].e, nxt)).trim());
            }
        }

        juce::String resolveSample (const juce::String& smp, const juce::String& dp) const
        {
            const auto s = smp.trim().replaceCharacter ('\\', '/');
            if (s.startsWithChar ('*')) return s;
            const auto base = dp.isEmpty() ? rootDir : resolvePath (rootDir, dp);
            return resolvePath (base.exists() ? base : rootDir.getChildFile (dp), s).getFullPathName();
        }

        void closeRegion()
        {
            if (region != nullptr)
            {
                Op r;
                for (auto& kv : glob)    r[kv.first] = kv.second;
                for (auto& kv : master)  r[kv.first] = kv.second;
                for (auto& kv : group)   r[kv.first] = kv.second;
                for (auto& kv : *region) r[kv.first] = kv.second;
                r["_note_offset"] = juce::String (noteOffset);
                r["_octave_offset"] = juce::String (octaveOffset);
                r["_index"] = juce::String ((int) out.regions.size());
                const auto* smp = get (r, "sample");
                if (smp != nullptr && smp->isNotEmpty()) r["_sample"] = resolveSample (*smp, defaultPath);
                out.regions.push_back (std::move (r));
                if ((int) out.regions.size() > 4 * kMaxRegions) throw Fail ("the SFZ has an absurd number of regions (over 260000)");
            }
            region.reset();
        }

        void closeCurve()
        {
            if (curve != nullptr)
            {
                const int idx = (int) fnum (*curve, "curve_index", 0);
                std::map<int, double> pts;
                for (auto& kv : *curve)
                    if (kv.first.length() == 4 && kv.first[0] == 'v' && kv.first.substring (1).containsOnly ("0123456789"))
                    {
                        double v; if (parseNum (kv.second, v)) pts[kv.first.substring (1).getIntValue()] = v;
                    }
                if (! pts.empty()) out.curves[idx] = interpPoints (pts);
            }
            curve.reset();
        }

        void header (const juce::String& h)
        {
            closeRegion(); closeCurve();
            level = h;
            if (h == "global")      { glob.clear(); master.clear(); group.clear(); }
            else if (h == "master") { master.clear(); group.clear(); }
            else if (h == "group")  { group.clear(); }
            else if (h == "region") { region = std::make_unique<Op>(); }
            else if (h == "curve")  { curve = std::make_unique<Op>(); }
        }

        void opcode (const juce::String& name, const juce::String& value)
        {
            if (level == "control")
            {
                control[name] = value;
                int n;
                if (name == "default_path") defaultPath = value.replaceCharacter ('\\', '/');
                else if (name == "note_offset")   { double d; if (parseNum (value, d)) noteOffset = (int) d; }
                else if (name == "octave_offset") { double d; if (parseNum (value, d)) octaveOffset = (int) d; }
                else if (name.startsWith ("set_hdcc") && ccNum (name.substring (8), n)) { double d; if (parseNum (value, d)) out.cc[n] = d * 127.0; }
                else if (name.startsWith ("set_cc") && ccNum (name.substring (6), n))   { double d; if (parseNum (value, d)) out.cc[n] = d; }
                return;
            }
            if (level == "curve")  { if (curve) (*curve)[name] = value; return; }
            if (level == "global") glob[name] = value;
            else if (level == "master") master[name] = value;
            else if (level == "group") group[name] = value;
            else if (level == "region" && region != nullptr) (*region)[name] = value;
            else if (level.isEmpty()) glob[name] = value;              // opcodes before any header: global (sfizz)
        }
    };

    //==============================================================================================================
    //  Audio sources: a file on disk, or one/two samples of an SF2/SF3
    //==============================================================================================================
    struct Audio
    {
        std::vector<std::vector<float>> ch;     // 1 or 2 channels
        double sr = 48000.0;
        int64_t frames() const { return ch.empty() ? 0 : (int64_t) ch[0].size(); }
        int channels() const { return (int) ch.size(); }
    };

    juce::AudioFormatManager& formats()
    {
        thread_local std::unique_ptr<juce::AudioFormatManager> fm;
        if (fm == nullptr) { fm = std::make_unique<juce::AudioFormatManager>(); fm->registerBasicFormats(); }
        return *fm;
    }

    void readAll (juce::AudioFormatReader& r, Audio& a, const juce::String& what)
    {
        const int ch = (int) juce::jlimit<unsigned> (1, 2, r.numChannels);
        const int64_t n = r.lengthInSamples;
        if (n < 2) throw Fail (what + " is empty");
        if (n > kMaxFrames) throw Fail (what + " is absurdly long (" + juce::String (n) + " frames)");
        a.sr = r.sampleRate > 1000.0 ? r.sampleRate : 44100.0;
        a.ch.assign ((size_t) ch, std::vector<float> ((size_t) n, 0.0f));
        constexpr int kChunk = 65536;
        juce::AudioBuffer<float> buf (ch, kChunk);
        for (int64_t pos = 0; pos < n; pos += kChunk)
        {
            const int m = (int) std::min<int64_t> (kChunk, n - pos);
            buf.clear();
            if (! r.read (&buf, 0, m, pos, true, ch > 1)) throw Fail ("Cannot decode " + what + " (read error)");
            for (int c = 0; c < ch; ++c) std::copy (buf.getReadPointer (c), buf.getReadPointer (c) + m, a.ch[(size_t) c].begin() + pos);
        }
    }

    /** analyse.read_smpl_loops: (unity note, [(start, end_exclusive)]) from a WAV 'smpl' chunk. */
    void readSmplLoops (const juce::File& f, int& unity, std::vector<std::pair<int64_t, int64_t>>& loops)
    {
        unity = -1; loops.clear();
        if (! f.getFileExtension().equalsIgnoreCase (".wav")) return;
        juce::FileInputStream in (f);
        if (! in.openedOk()) return;
        char hdr[12];
        if (in.read (hdr, 12) != 12) return;
        if (! ((std::memcmp (hdr, "RIFF", 4) == 0 || std::memcmp (hdr, "RF64", 4) == 0) && std::memcmp (hdr + 8, "WAVE", 4) == 0)) return;
        while (! in.isExhausted())
        {
            char id[4];
            if (in.read (id, 4) != 4) return;
            const uint32_t size = (uint32_t) in.readInt();
            if (std::memcmp (id, "smpl", 4) == 0)
            {
                if (size < 36 || size > (1u << 20)) return;
                juce::MemoryBlock mb; if (in.readIntoMemoryBlock (mb, size) != (size_t) size) return;
                auto* d = static_cast<const uint8_t*> (mb.getData());
                auto u32 = [d] (size_t o) { return (uint32_t) d[o] | ((uint32_t) d[o + 1] << 8) | ((uint32_t) d[o + 2] << 16) | ((uint32_t) d[o + 3] << 24); };
                const uint32_t un = u32 (12), nl = u32 (28);
                for (uint32_t i = 0; i < nl; ++i)
                {
                    const size_t o = 36 + 24 * (size_t) i;
                    if (o + 24 > size) break;
                    const uint32_t s = u32 (o + 8), e = u32 (o + 12);
                    if (e > s) loops.push_back ({ (int64_t) s, (int64_t) e + 1 });
                }
                if (un > 0 && un < 128) unity = (int) un;
                return;
            }
            if (! in.setPosition (in.getPosition() + size + (size & 1))) return;
        }
    }

    //==============================================================================================================
    //  3 · SF2 / SF3 (sf2.py)
    //==============================================================================================================
    struct Sf2
    {
        struct Phdr { juce::String name; int preset = 0, bank = 0, bag = 0; };
        struct Inst { juce::String name; int bag = 0; };
        struct Shdr { juce::String name; uint32_t start = 0, end = 0, sl = 0, el = 0, rate = 0; uint8_t pitch = 60; int8_t corr = 0; uint16_t link = 0, type = 0; };
        struct Gen  { uint16_t op = 0; uint16_t amt = 0; };

        juce::File file;
        int64_t smplOff = -1, smplLen = 0, sm24Off = -1, sm24Len = 0, fileLen = 0;
        std::vector<Phdr> phdr; std::vector<Inst> inst; std::vector<Shdr> shdr;
        std::vector<uint16_t> pbag, ibag; std::vector<Gen> pgen, igen;
        bool anyCompressed = false;

        static juce::String name20 (const uint8_t* p)
        {
            int n = 0; while (n < 20 && p[n] != 0) ++n;
            juce::String s; for (int i = 0; i < n; ++i) s += juce::String::charToString ((juce_wchar) p[i]);   // latin-1
            return s.trim();
        }

        explicit Sf2 (const juce::File& f) : file (f)
        {
            juce::FileInputStream in (f);
            if (! in.openedOk()) throw Fail ("Cannot open " + f.getFullPathName());
            fileLen = in.getTotalLength();
            char hdr[12];
            if (fileLen < 12 || in.read (hdr, 12) != 12 || std::memcmp (hdr, "RIFF", 4) != 0 || std::memcmp (hdr + 8, "sfbk", 4) != 0)
                throw Fail (f.getFileName() + " is not a SoundFont (no RIFF sfbk header)");
            std::map<juce::String, juce::MemoryBlock> pdta;
            int64_t off = 12;
            while (off + 8 <= fileLen)
            {
                in.setPosition (off);
                char id[4]; in.read (id, 4);
                const int64_t size = (int64_t) (uint32_t) in.readInt();
                const int64_t body = off + 8;
                if (body + size > fileLen) throw Fail (f.getFileName() + " is truncated (a chunk runs past the end of the file)");
                if (std::memcmp (id, "LIST", 4) == 0 && size >= 4)
                {
                    char kind[4]; in.read (kind, 4);
                    int64_t so = body + 4;
                    while (so + 8 <= body + size)
                    {
                        in.setPosition (so);
                        char sid[4]; in.read (sid, 4);
                        const int64_t sn = (int64_t) (uint32_t) in.readInt();
                        if (so + 8 + sn > body + size) throw Fail (f.getFileName() + " is truncated or corrupt (sub-chunk overruns its LIST)");
                        if (std::memcmp (kind, "sdta", 4) == 0)
                        {
                            if (std::memcmp (sid, "smpl", 4) == 0) { smplOff = so + 8; smplLen = sn; }
                            else if (std::memcmp (sid, "sm24", 4) == 0) { sm24Off = so + 8; sm24Len = sn; }
                        }
                        else if (std::memcmp (kind, "pdta", 4) == 0)
                        {
                            if (sn > 256 * 1024 * 1024) throw Fail (f.getFileName() + " has an absurd preset table");
                            juce::MemoryBlock mb;
                            if (in.readIntoMemoryBlock (mb, (juce::pointer_sized_int) sn) != (size_t) sn) throw Fail (f.getFileName() + " is truncated");
                            pdta[juce::String (sid, 4)] = std::move (mb);
                        }
                        so += 8 + sn + (sn & 1);
                    }
                }
                off = body + size + (size & 1);
            }
            for (auto* k : { "phdr", "pbag", "pgen", "inst", "ibag", "igen", "shdr" })
                if (pdta.find (k) == pdta.end()) throw Fail (f.getFileName() + " is corrupt (no " + juce::String (k) + " table)");
            if (smplOff < 0) throw Fail (f.getFileName() + " has no sample data (smpl chunk)");
            auto recs = [&] (const char* k, size_t sz) -> std::pair<const uint8_t*, size_t>
            {
                const auto& mb = pdta[k];
                if (mb.getSize() % sz != 0 || mb.getSize() < 2 * sz) throw Fail (f.getFileName() + " is corrupt (" + juce::String (k) + " size)");
                return { static_cast<const uint8_t*> (mb.getData()), mb.getSize() / sz };
            };
            auto u16 = [] (const uint8_t* p) { return (uint16_t) (p[0] | (p[1] << 8)); };
            auto u32 = [] (const uint8_t* p) { return (uint32_t) p[0] | ((uint32_t) p[1] << 8) | ((uint32_t) p[2] << 16) | ((uint32_t) p[3] << 24); };
            { auto [p, n] = recs ("phdr", 38); for (size_t i = 0; i < n; ++i) { const uint8_t* q = p + 38 * i; phdr.push_back ({ name20 (q), u16 (q + 20), u16 (q + 22), u16 (q + 24) }); } }
            { auto [p, n] = recs ("pbag", 4);  for (size_t i = 0; i < n; ++i) pbag.push_back (u16 (p + 4 * i)); }
            { auto [p, n] = recs ("pgen", 4);  for (size_t i = 0; i < n; ++i) pgen.push_back ({ u16 (p + 4 * i), u16 (p + 4 * i + 2) }); }
            { auto [p, n] = recs ("inst", 22); for (size_t i = 0; i < n; ++i) inst.push_back ({ name20 (p + 22 * i), u16 (p + 22 * i + 20) }); }
            { auto [p, n] = recs ("ibag", 4);  for (size_t i = 0; i < n; ++i) ibag.push_back (u16 (p + 4 * i)); }
            { auto [p, n] = recs ("igen", 4);  for (size_t i = 0; i < n; ++i) igen.push_back ({ u16 (p + 4 * i), u16 (p + 4 * i + 2) }); }
            { auto [p, n] = recs ("shdr", 46);
              for (size_t i = 0; i < n; ++i)
              {
                  const uint8_t* q = p + 46 * i;
                  Shdr s; s.name = name20 (q); s.start = u32 (q + 20); s.end = u32 (q + 24); s.sl = u32 (q + 28); s.el = u32 (q + 32);
                  s.rate = u32 (q + 36); s.pitch = q[40]; s.corr = (int8_t) q[41]; s.link = u16 (q + 42); s.type = u16 (q + 44);
                  if (s.type & 0x10) anyCompressed = true;
                  shdr.push_back (s);
              } }
        }

        juce::StringArray presetNames() const
        {
            juce::StringArray a;
            for (size_t i = 0; i + 1 < phdr.size(); ++i) a.add (phdr[i].name.isNotEmpty() ? phdr[i].name : "Preset " + juce::String ((int) i + 1));
            return a;
        }

        using Zone = std::map<int, int>;         // op → amount (ranges packed lo | hi << 8)
        static bool isRange (int op) { return op == 43 || op == 44; }

        Zone zoneGens (const std::vector<Gen>& gens, size_t a, size_t b) const
        {
            Zone z;
            for (size_t i = a; i < b && i < gens.size(); ++i)
            {
                const int op = gens[i].op;
                z[op] = (isRange (op) || op == 41 || op == 53) ? (int) gens[i].amt : (int) (int16_t) gens[i].amt;
            }
            return z;
        }
        void zones (const std::vector<uint16_t>& bags, const std::vector<Gen>& gens, int first, int last, int term, Zone& glob, std::vector<Zone>& out) const
        {
            std::vector<Zone> zs;
            for (int bi = first; bi < last; ++bi)
            {
                if (bi < 0 || (size_t) bi + 1 >= bags.size()) break;
                const size_t g0 = bags[(size_t) bi], g1 = bags[(size_t) bi + 1];
                if (g1 < g0) continue;
                zs.push_back (zoneGens (gens, g0, g1));
            }
            glob.clear(); out.clear();
            if (! zs.empty() && zs[0].find (term) == zs[0].end()) { glob = zs[0]; zs.erase (zs.begin()); }
            for (auto& z : zs) if (z.find (term) != z.end()) out.push_back (z);
        }

        /** One sample, float, relative loop points (sf2.SF2.sample_data). */
        void sampleData (int sid, juce::FileInputStream& in, std::vector<float>& x, double& rate, int& sl, int& el) const
        {
            const auto& s = shdr[(size_t) sid];
            rate = s.rate > 1000 ? (double) s.rate : 44100.0;
            if (s.type & 0x10)
            {
               #if JUCE_USE_OGGVORBIS
                if (s.end <= s.start || (int64_t) s.end > smplLen) throw Fail (file.getFileName() + " is corrupt (compressed sample " + juce::String (sid) + " out of range)");
                juce::MemoryBlock mb;
                in.setPosition (smplOff + s.start);
                if (in.readIntoMemoryBlock (mb, (juce::pointer_sized_int) (s.end - s.start)) != (size_t) (s.end - s.start)) throw Fail (file.getFileName() + " is truncated");
                juce::OggVorbisAudioFormat ogg;
                std::unique_ptr<juce::AudioFormatReader> r (ogg.createReaderFor (new juce::MemoryInputStream (mb, false), true));
                if (r == nullptr) throw Fail (file.getFileName() + ": cannot decode the Ogg Vorbis sample \"" + s.name + "\" (SF3)");
                Audio a; readAll (*r, a, "SF3 sample \"" + s.name + "\"");
                rate = a.sr;
                x = std::move (a.ch[0]);
                sl = (int) s.sl; el = (int) s.el;
                return;
               #else
                throw Fail ("SF3 not supported (this build has no Ogg Vorbis decoder)");
               #endif
            }
            if (s.end <= s.start) throw Fail (file.getFileName() + " is corrupt (sample \"" + s.name + "\" has no frames)");
            const int64_t n = (int64_t) s.end - (int64_t) s.start;
            if (n > kMaxFrames) throw Fail (file.getFileName() + ": sample \"" + s.name + "\" is absurdly long");
            if (2 * (int64_t) s.end > smplLen) throw Fail (file.getFileName() + " is truncated (sample \"" + s.name + "\" runs past the sample data)");
            juce::MemoryBlock mb;
            in.setPosition (smplOff + 2 * (int64_t) s.start);
            if (in.readIntoMemoryBlock (mb, (juce::pointer_sized_int) (2 * n)) != (size_t) (2 * n)) throw Fail (file.getFileName() + " is truncated");
            const auto* p = static_cast<const uint8_t*> (mb.getData());
            x.resize ((size_t) n);
            const bool s24 = sm24Off >= 0 && (int64_t) s.end <= sm24Len;
            juce::MemoryBlock lo;
            if (s24) { in.setPosition (sm24Off + (int64_t) s.start); in.readIntoMemoryBlock (lo, (juce::pointer_sized_int) n); }
            const auto* pl = static_cast<const uint8_t*> (lo.getData());
            for (int64_t i = 0; i < n; ++i)
            {
                const int v = (int) (int16_t) (uint16_t) (p[2 * i] | (p[2 * i + 1] << 8));
                x[(size_t) i] = s24 && (int64_t) lo.getSize() == n ? (float) (((v << 8) | pl[i]) / 8388608.0) : (float) (v / 32768.0);
            }
            sl = (int) ((int64_t) s.sl - (int64_t) s.start); el = (int) ((int64_t) s.el - (int64_t) s.start);
        }

        int64_t sampleFrames (int sid) const
        {
            const auto& s = shdr[(size_t) sid];
            if (! (s.type & 0x10)) return (int64_t) s.end - (int64_t) s.start;
            return ((int64_t) s.end - (int64_t) s.start) * 10;     // an estimate for the size guard (Vorbis ≈ 10:1)
        }
    };

    double tc (int v) { return v <= -12000 ? 0.0 : std::pow (2.0, v / 1200.0); }

    struct Sf2Source { std::vector<int> sids; };             // one (mono) or two (left, right) sample ids

    /** sf2.parse for one preset → flat region dicts (the SFZ opcode vocabulary) + the sample sources they name. */
    SfzFile sf2Regions (const Sf2& s, int idx, std::map<juce::String, Sf2Source>& srcs)
    {
        static const std::map<int, int> kDefaults { { 33, -12000 }, { 34, -12000 }, { 35, -12000 }, { 36, -12000 }, { 37, 0 }, { 38, -12000 }, { 56, 100 }, { 58, -1 } };
        SfzFile out; out.path = s.file; out.includes.add (s.file);
        if (idx < 0 || (size_t) idx + 1 >= s.phdr.size()) throw Fail ("preset " + juce::String (idx + 1) + " is not in " + s.file.getFileName());
        Sf2::Zone pglob; std::vector<Sf2::Zone> pzones;
        s.zones (s.pbag, s.pgen, s.phdr[(size_t) idx].bag, s.phdr[(size_t) idx + 1].bag, 41, pglob, pzones);
        std::vector<Sf2::Zone> raw;
        for (auto& pz : pzones)
        {
            Sf2::Zone pall = pglob; for (auto& kv : pz) pall[kv.first] = kv.second;
            const int ii = pall[41];
            if (ii < 0 || (size_t) ii + 1 >= s.inst.size()) continue;
            Sf2::Zone iglob; std::vector<Sf2::Zone> izones;
            s.zones (s.ibag, s.igen, s.inst[(size_t) ii].bag, s.inst[(size_t) ii + 1].bag, 53, iglob, izones);
            for (auto& iz : izones)
            {
                Sf2::Zone g (kDefaults.begin(), kDefaults.end());
                for (auto& kv : iglob) g[kv.first] = kv.second;
                for (auto& kv : iz) g[kv.first] = kv.second;
                for (auto& kv : pall)
                {
                    const int op = kv.first, v = kv.second;
                    if (op == 41) continue;
                    if (Sf2::isRange (op))
                    {
                        const int cur = g.count (op) ? g[op] : (127 << 8);
                        const int lo = std::max (cur & 0xFF, v & 0xFF), hi = std::min ((cur >> 8) & 0xFF, (v >> 8) & 0xFF);
                        g[op] = (lo & 0xFF) | ((hi & 0xFF) << 8);
                    }
                    else if (op == 53 || op == 54 || op == 57 || op == 58 || op == 0 || op == 1 || op == 2 || op == 3 || op == 4 || op == 12 || op == 45 || op == 50) continue;
                    else g[op] = (g.count (op) ? g[op] : (kDefaults.count (op) ? kDefaults.at (op) : 0)) + v;
                }
                const int kr = g.count (43) ? g[43] : (127 << 8), vr = g.count (44) ? g[44] : (127 << 8);
                if ((kr & 0xFF) > ((kr >> 8) & 0xFF) || (vr & 0xFF) > ((vr >> 8) & 0xFF)) continue;
                const int sid = g[53];
                if (sid < 0 || (size_t) sid + 1 >= s.shdr.size()) continue;              // a bad sample id (or the EOS terminator)
                if (s.shdr[(size_t) sid].type & 0x8000) continue;                           // ROM sample: no data in the file
                raw.push_back (g);
            }
        }
        // linked stereo pairs → one stereo source
        std::vector<bool> used (raw.size(), false);
        for (size_t i = 0; i < raw.size(); ++i)
        {
            if (used[i]) continue;
            auto& g = raw[i];
            const int sid = g[53];
            const auto& sh = s.shdr[(size_t) sid];
            int pair = -1;
            if (sh.type & 0x6)
                for (size_t j = i + 1; j < raw.size(); ++j)
                    if (! used[j] && raw[j][53] == (int) sh.link && raw[j][43] == g[43] && raw[j][44] == g[44]) { pair = (int) j; break; }
            std::vector<int> sids;
            if (pair >= 0)
            {
                used[(size_t) pair] = true;
                const int other = raw[(size_t) pair][53];
                sids = (sh.type & 0x4) ? std::vector<int> { sid, other } : std::vector<int> { other, sid };
            }
            else sids = { sid };
            juce::String key ("sf2:");
            for (size_t k = 0; k < sids.size(); ++k) key << (k ? "_" : "") << sids[k];
            srcs[key] = Sf2Source { sids };
            const auto& h0 = s.shdr[(size_t) sids[0]];
            const int root = g[58] >= 0 ? g[58] : (h0.pitch <= 127 ? (int) h0.pitch : 60);
            Op r;
            r["sample"] = key; r["_sample"] = key;
            r["_note_offset"] = "0"; r["_octave_offset"] = "0"; r["_index"] = juce::String ((int) out.regions.size());
            const int kr = g.count (43) ? g[43] : (127 << 8), vr = g.count (44) ? g[44] : (127 << 8);
            r["lokey"] = juce::String (kr & 0xFF); r["hikey"] = juce::String ((kr >> 8) & 0xFF);
            r["lovel"] = juce::String (vr & 0xFF); r["hivel"] = juce::String ((vr >> 8) & 0xFF);
            r["pitch_keycenter"] = juce::String (root);
            r["transpose"] = juce::String (g.count (51) ? g[51] : 0);
            r["tune"] = juce::String ((g.count (52) ? g[52] : 0) + (int) h0.corr);
            r["volume"] = juce::String (roundN (-(g.count (48) ? g[48] : 0) / 10.0, 2));
            r["pan"] = juce::String (roundN (juce::jlimit (-500, 500, g.count (17) ? g[17] : 0) / 5.0, 1));
            if ((g.count (56) ? g[56] : 100) != 100) r["pitch_keytrack"] = juce::String (g[56]);
            const int offs = (g.count (0) ? g[0] : 0) + 32768 * (g.count (4) ? g[4] : 0);
            if (offs > 0) r["offset"] = juce::String (offs);
            const int eoff = (g.count (1) ? g[1] : 0) + 32768 * (g.count (12) ? g[12] : 0);
            if (eoff < 0)
            {
                int64_t fr = s.sampleFrames (sids[0]);
                for (auto q : sids) fr = std::min (fr, s.sampleFrames (q));
                r["end"] = juce::String (std::max<int64_t> (1, fr - 1 + eoff));
            }
            const auto& hl = s.shdr[(size_t) sids.back()];
            const int64_t sl = (hl.type & 0x10) ? (int64_t) hl.sl : (int64_t) hl.sl - (int64_t) hl.start;
            const int64_t el = (hl.type & 0x10) ? (int64_t) hl.el : (int64_t) hl.el - (int64_t) hl.start;
            const int mode = (g.count (54) ? g[54] : 0) & 3;
            if ((mode == 1 || mode == 3) && el > sl)
            {
                r["loop_mode"] = mode == 1 ? "loop_continuous" : "loop_sustain";
                r["loop_start"] = juce::String (sl + (g.count (2) ? g[2] : 0) + 32768 * (g.count (45) ? g[45] : 0));
                r["loop_end"] = juce::String (el - 1 + (g.count (3) ? g[3] : 0) + 32768 * (g.count (50) ? g[50] : 0));
            }
            else r["loop_mode"] = "no_loop";
            r["ampeg_delay"] = juce::String (round4 (tc (g[33])));
            r["ampeg_attack"] = juce::String (round4 (tc (g[34])));
            r["ampeg_hold"] = juce::String (round4 (tc (g[35])));
            r["ampeg_decay"] = juce::String (round4 (tc (g[36])));
            r["ampeg_sustain"] = juce::String (roundN (100.0 * std::pow (10.0, -std::max (0, g[37]) / 200.0), 2));
            r["ampeg_release"] = juce::String (round4 (std::max (0.001, tc (g[38]))));
            if (g.count (57) && g[57] != 0) { r["group"] = juce::String (1000 + g[57]); r["off_by"] = juce::String (1000 + g[57]); }
            out.regions.push_back (std::move (r));
        }
        return out;
    }

    //==============================================================================================================
    //  4 · interpretation (torgc.Interpreter)
    //==============================================================================================================
    enum Kind { kAttack = 0, kRelease = 1, kNoise = 2 };
    const char* kindName (int k) { return k == kRelease ? "release" : (k == kNoise ? "noise" : "attack"); }

    struct Env { double a = 0, h = 0, d = 0, s = 1, r = 0.001; };

    struct Reg
    {
        int a = 0, kind = kAttack;
        juce::String src;
        int lk = 0, hk = 127, lv = 1, hv = 127, root = 60;
        double cents = 0, gainDb = 0, pan = 0;
        int64_t offset = 0, end = -1;           // end: exclusive frame, −1 = none
        juce::String loopMode = "no_loop";
        int64_t ls = -1, le = -1;
        double xfS = 0;
        int rrPos = 0, rrLen = 1;
        double randLo = 0, randHi = 1;
        int grp = 0, offBy = 0;
        juce::String offMode = "normal";
        Env env;
        double rtDecay = 0;
        std::vector<double> curve;             // 128
        double fa = 0, fb = 127;
        int olk = -1, ohk = -1;                // fill_holes: the authored edges (−1 = untouched)
    };

    struct SrcInfo { bool exists = false; int64_t frames = 0; int channels = 1; double sr = 44100; int unity = -1; std::vector<std::pair<int64_t, int64_t>> loops; };

    class Interpreter
    {
    public:
        Interpreter (const SfzFile& f, std::map<juce::String, SrcInfo>& info) : sf (f), srcInfo (info) {}
        std::map<juce::String, int> drops;

        std::vector<Reg> interpret (const Op& r, int aIdx)
        {
            std::vector<Reg> none;
            const auto* smpP = get (r, "_sample");
            if (smpP == nullptr || smpP->isEmpty() || smpP->startsWithChar ('*')) { ++drops["no_sample_or_generator"]; return none; }
            const juce::String smp = *smpP;
            const auto& si = info (smp);
            if (! si.exists)
            {
                if (undecodable.count (smp)) ++drops["undecodable"]; else { ++drops["missing_file"]; missing.insert (smp); }
                return none;
            }
            for (auto& kv : r)
                if (kv.first.startsWith ("on_locc") || kv.first.startsWith ("on_hicc")) { ++drops["cc_triggered"]; return none; }
            for (auto& kv : r)
            {
                const auto& k = kv.first;
                if ((k.startsWith ("locc") || k.startsWith ("hicc")) && k.length() > 4 && k.substring (4).containsOnly ("0123456789"))
                {
                    const int n = k.substring (4).getIntValue();
                    const double val = cc (n);
                    const bool lo = k.startsWith ("lo");
                    const double lim = fnum (r, k, lo ? 0 : 127);
                    if ((lo && val < lim) || (! lo && val > lim)) { ++drops["cc_condition"]; return none; }
                }
            }
            if (has (r, "lochan") && fnum (r, "lochan", 1) > 1) { ++drops["channel"]; return none; }
            if (has (r, "sw_previous") || has (r, "sw_down") || has (r, "sw_up")) { ++drops["sw_state"]; return none; }
            if (fnum (r, "lobend", -8192) > 0 || fnum (r, "hibend", 8192) < 0) { ++drops["bend_condition"]; return none; }
            juce::String trig = get (r, "trigger") ? get (r, "trigger")->trim().toLowerCase() : juce::String ("attack");
            if (trig.isEmpty()) trig = "attack";
            if (trig == "legato") { ++drops["legato"]; return none; }
            if (trig == "first") trig = "attack";
            if (trig != "attack" && trig != "release" && trig != "release_key") { ++drops["trigger_" + trig]; return none; }
            const int kind = trig == "attack" ? kAttack : kRelease;

            const int no = (int) fnum (r, "_note_offset", 0), oo = (int) fnum (r, "_octave_offset", 0);
            auto N = [&] (const char* k, int& out) -> bool { const auto* v = get (r, k); return v != nullptr && v->isNotEmpty() && noteToMidi (*v, no, oo, out); };
            int key = 0, lk = 0, hk = 0;
            const bool hasKey = N ("key", key);
            bool hasLk = N ("lokey", lk), hasHk = N ("hikey", hk);
            if (hasKey) { if (! hasLk) { lk = key; hasLk = true; } if (! hasHk) { hk = key; hasHk = true; } }
            if (! hasLk) lk = 0;
            if (! hasHk) hk = 127;
            if (lk < 0 || hk < 0 || hk < lk) { ++drops["no_keys"]; return none; }
            lk = std::max (0, lk); hk = std::min (127, hk);
            int root = 60;
            const auto* pkc = get (r, "pitch_keycenter");
            if (pkc != nullptr && pkc->trim().equalsIgnoreCase ("sample")) root = si.unity >= 0 ? si.unity : (hasKey ? key : 60);
            else if (pkc != nullptr) { if (! noteToMidi (*pkc, no, oo, root)) root = 60; }
            else root = hasKey ? key : 60;
            ctxRoot = root;
            int lv = (int) fnum (r, "lovel", 0), hv = (int) fnum (r, "hivel", 127);
            double fa = lv, fb = hv;
            if (has (r, "xfin_lovel") || has (r, "xfin_hivel")) { fa = fnum (r, "xfin_hivel", fa); lv = (int) fnum (r, "xfin_lovel", lv); }
            if (has (r, "xfout_lovel") || has (r, "xfout_hivel")) { fb = fnum (r, "xfout_lovel", fb); hv = (int) fnum (r, "xfout_hivel", hv); }
            const double tune = fnum (r, "tune", 0) + fnum (r, "pitch", 0) + onccSum (r, "tune") + onccSum (r, "pitch");
            const double cents = tune + 100.0 * fnum (r, "transpose", 0);
            double gain = fnum (r, "volume", 0) + fnum (r, "group_volume", 0) + fnum (r, "master_volume", 0) + fnum (r, "global_volume", 0)
                        + onccSum (r, "gain") + onccSum (r, "volume");
            double amp = fnum (r, "amplitude", 100.0) / 100.0;
            for (auto& kv : r)
            {
                int n;
                if (ccSuffix (kv.first, "amplitude_", n) || ccSuffix (kv.first, "amplitude_on", n))
                    amp *= fnum (r, kv.first, 100.0) / 100.0 * std::max (0.0, curveOf (r, "amplitude", n));
            }
            amp *= ccXfadeGain (r);
            if (amp <= 1.0e-6) { ++drops["silent_at_default_cc"]; return none; }
            gain += 20.0 * std::log10 (amp);
            const double pan = juce::jlimit (-100.0, 100.0, fnum (r, "pan", 0) + onccSum (r, "pan"));
            const int64_t offset = (int64_t) (fnum (r, "offset", 0) + onccSum (r, "offset"));
            int64_t endI = -1;
            if (const auto* e = get (r, "end"); e != nullptr && e->isNotEmpty())
            {
                const int64_t ev = (int64_t) fnum (r, "end", 0);
                if (ev <= 0) { ++drops["end_zero"]; return none; }
                endI = ev + 1;
            }
            juce::String lm = (get (r, "loop_mode") ? *get (r, "loop_mode") : (get (r, "loopmode") ? *get (r, "loopmode") : juce::String())).trim().toLowerCase();
            const auto* lsS = get (r, "loop_start") ? get (r, "loop_start") : get (r, "loopstart");
            const auto* leS = get (r, "loop_end") ? get (r, "loop_end") : get (r, "loopend");
            int64_t lsI = -1, leI = -1;
            { double d; if (lsS && parseNum (*lsS, d)) lsI = (int64_t) d; if (leS && parseNum (*leS, d)) leI = (int64_t) d + 1; }
            if ((lsI < 0 || leI < 0) && ! si.loops.empty()) { lsI = si.loops[0].first; leI = si.loops[0].second; }
            if (lm.isEmpty()) lm = (lsI >= 0 && leI >= 0 && leI > lsI) ? "loop_continuous" : "no_loop";
            if (lm == "loop_continuous") lm = "continuous";
            else if (lm == "loop_sustain") lm = "sustain";
            else if (lm != "one_shot") lm = "no_loop";
            if ((lm == "continuous" || lm == "sustain") && ! (lsI >= 0 && leI >= 0 && leI > lsI)) { lm = "no_loop"; lsI = leI = -1; }
            if (lm == "no_loop" || lm == "one_shot") lsI = leI = -1;
            const double xfS = fnum (r, "loop_crossfade", 0.0);
            const int seqLen = (int) fnum (r, "seq_length", 1), seqPos = (int) fnum (r, "seq_position", 1);
            const double rlo = std::max (0.0, fnum (r, "lorand", 0.0)), rhi = std::min (1.0, fnum (r, "hirand", 1.0));
            if (rhi <= rlo) { ++drops["empty_rand"]; return none; }
            Reg b;
            b.env.a = round4 (std::max (0.0, fnum (r, "ampeg_attack", 0.0) + onccSum (r, "ampeg_attack")));
            b.env.h = round4 (std::max (0.0, fnum (r, "ampeg_hold", 0.0) + onccSum (r, "ampeg_hold")));
            b.env.d = round4 (std::max (0.0, fnum (r, "ampeg_decay", 0.0) + onccSum (r, "ampeg_decay")));
            b.env.s = round4 (std::max (0.0, std::min (1.0, (fnum (r, "ampeg_sustain", 100.0) + onccSum (r, "ampeg_sustain")) / 100.0)));
            b.env.r = round4 (std::max (0.001, fnum (r, "ampeg_release", 0.001) + onccSum (r, "ampeg_release")));
            b.curve = velCurve (r);
            const auto om = get (r, "off_mode") ? get (r, "off_mode")->trim().toLowerCase() : juce::String ("normal");
            b.a = aIdx; b.kind = kind; b.src = smp; b.lk = lk; b.hk = hk; b.lv = std::max (1, lv); b.hv = std::min (127, hv); b.root = root;
            b.cents = roundN (cents, 2); b.gainDb = gain; b.pan = roundN (pan, 1); b.offset = std::max<int64_t> (0, offset); b.end = endI;
            b.loopMode = lm; b.ls = lsI; b.le = leI; b.xfS = xfS;
            if (seqLen > 1) { b.rrPos = std::max (0, seqPos - 1); b.rrLen = std::max (1, seqLen); } else { b.rrPos = 0; b.rrLen = 1; }
            b.randLo = round4 (rlo); b.randHi = round4 (rhi);
            b.grp = (int) fnum (r, "group", 0); b.offBy = (int) fnum (r, "off_by", 0); b.offMode = om == "fast" ? "fast" : "normal";
            b.rtDecay = fnum (r, "rt_decay", 0.0);
            b.fa = fa; b.fb = fb;
            const double kt = fnum (r, "pitch_keytrack", 100.0);
            if (std::abs (kt - 100.0) > 1.0e-6)
            {
                if (hk - lk > 48) { ++drops["keytrack_wide"]; return none; }
                std::vector<Reg> out;
                for (int k = lk; k <= hk; ++k)
                {
                    Reg x = b; x.lk = x.hk = x.root = k; x.cents = roundN (cents + (k - root) * (kt - 100.0), 2);
                    out.push_back (x);
                }
                return out;
            }
            return { b };
        }

        std::set<juce::String> missing;

    private:
        const SfzFile& sf;
        std::map<juce::String, SrcInfo>& srcInfo;
        int ctxRoot = 60;

        const SrcInfo& info (const juce::String& smp)
        {
            auto it = srcInfo.find (smp);
            if (it != srcInfo.end()) return it->second;
            SrcInfo si;
            const juce::File f (smp);
            if (f.existsAsFile())
            {
                std::unique_ptr<juce::AudioFormatReader> rd (formats().createReaderFor (f));
                if (rd != nullptr)
                {
                    si.exists = true; si.frames = rd->lengthInSamples; si.channels = (int) juce::jlimit<unsigned> (1, 2, rd->numChannels);
                    si.sr = rd->sampleRate;
                    readSmplLoops (f, si.unity, si.loops);
                }
                else undecodable.insert (smp);
            }
            return srcInfo[smp] = si;
        }

    public:
        std::set<juce::String> undecodable;

    private:
        double cc (int n) const
        {
            if (n == 133) return ctxRoot;
            if (n == 131) return 100.0;
            return sf.ccValue (n);
        }
        double curveOf (const Op& r, const juce::String& base, int n) const
        {
            const double x = juce::jlimit (0.0, 1.0, cc (n) / 127.0);
            const auto* c = get (r, base + "_curvecc" + juce::String (n));
            if (c == nullptr) return x;
            double cd; if (! parseNum (*c, cd)) return x;
            const int ci = (int) cd;
            auto it = sf.curves.find (ci);
            if (it != sf.curves.end()) return it->second[(size_t) juce::jlimit (0, 127, (int) std::round (x * 127))];
            switch (ci)
            {
                case 1: return 2 * x - 1;  case 2: return 1 - x;  case 3: return 1 - 2 * x;
                case 4: return x * x;      case 5: return std::sqrt (x);  case 6: return std::sqrt (1 - x);
                default: return x;
            }
        }
        static bool ccSuffix (const juce::String& k, const juce::String& prefix, int& n)
        {
            if (! k.startsWith (prefix)) return false;
            const auto rest = k.substring (prefix.length());
            return rest.startsWith ("cc") && ccNum (rest.substring (2), n);
        }
        double onccSum (const Op& r, const juce::String& base) const       // base(_on)?ccN at the default CC state
        {
            double tot = 0;
            for (auto& kv : r)
            {
                int n;
                if (ccSuffix (kv.first, base, n) || ccSuffix (kv.first, base + "_on", n))
                {
                    double v; if (parseNum (kv.second, v)) tot += v * curveOf (r, base, n);
                }
            }
            return tot;
        }
        double ccXfadeGain (const Op& r) const
        {
            double g = 1.0;
            std::set<int> ccs;
            for (auto& kv : r)
            {
                int n;
                for (auto* p : { "xfin_locc", "xfin_hicc", "xfout_locc", "xfout_hicc" })
                    if (kv.first.startsWith (p) && ccNum (kv.first.substring ((int) std::strlen (p)), n)) ccs.insert (n);
            }
            for (int n : ccs)
            {
                const double v = cc (n);
                const auto sn = juce::String (n);
                if (has (r, "xfin_locc" + sn) || has (r, "xfin_hicc" + sn))
                {
                    const double lo = fnum (r, "xfin_locc" + sn, 0), hi = fnum (r, "xfin_hicc" + sn, 0);
                    if (v <= lo) g *= 0.0; else if (v < hi) g *= std::sqrt ((v - lo) / (hi - lo));
                }
                if (has (r, "xfout_locc" + sn) || has (r, "xfout_hicc" + sn))
                {
                    const double lo = fnum (r, "xfout_locc" + sn, 127), hi = fnum (r, "xfout_hicc" + sn, 127);
                    if (v >= hi) g *= 0.0; else if (v > lo) g *= std::sqrt ((hi - v) / (hi - lo));
                }
            }
            return g;
        }
        std::vector<double> velCurve (const Op& r) const
        {
            std::map<int, double> pts;
            for (auto& kv : r)
                if (kv.first.startsWith ("amp_velcurve_") && kv.first.substring (13).containsOnly ("0123456789") && kv.first.length() > 13)
                {
                    double v; if (parseNum (kv.second, v)) pts[kv.first.substring (13).getIntValue()] = v;
                }
            std::vector<double> curve;
            if (! pts.empty()) curve = interpPoints (pts);
            else for (int v = 0; v < 128; ++v) curve.push_back ((v / 127.0) * (v / 127.0));
            const double vt = juce::jlimit (-1.0, 1.0, (fnum (r, "amp_veltrack", 100.0) + onccSum (r, "amp_veltrack")) / 100.0);
            for (auto& c : curve) c = vt >= 0 ? 1.0 - vt + vt * c : 1.0 + vt * c;
            return curve;
        }
    };

    //==============================================================================================================
    //  5 · mapping operations
    //==============================================================================================================
    struct GKey
    {
        int a, lk, hk, rrPos, rrLen; double rlo, rhi;
        bool operator< (const GKey& o) const
        {
            return std::tie (a, lk, hk, rrPos, rrLen, rlo, rhi) < std::tie (o.a, o.lk, o.hk, o.rrPos, o.rrLen, o.rlo, o.rhi);
        }
    };
    GKey gkey (const Reg& r) { return { r.a, r.lk, r.hk, r.rrPos, r.rrLen, r.randLo, r.randHi }; }

    /** Insertion-ordered grouping (Python dicts keep insertion order; the output order follows it). */
    template <typename K>
    std::vector<std::vector<size_t>> groupBy (const std::vector<Reg>& regs, std::function<bool (const Reg&)> want, std::function<K (const Reg&)> key)
    {
        std::map<K, size_t> at; std::vector<std::vector<size_t>> groups;
        for (size_t i = 0; i < regs.size(); ++i)
        {
            if (! want (regs[i])) continue;
            const K k = key (regs[i]);
            auto it = at.find (k);
            if (it == at.end()) { at[k] = groups.size(); groups.push_back ({ i }); }
            else groups[it->second].push_back (i);
        }
        return groups;
    }

    std::vector<Reg> layerize (const std::vector<Reg>& regs)
    {
        std::vector<Reg> out;
        for (auto& r : regs) if (r.kind != kAttack) out.push_back (r);
        auto groups = groupBy<GKey> (regs, [] (const Reg& r) { return r.kind == kAttack; }, gkey);
        for (auto& g : groups)
        {
            std::vector<std::pair<double, double>> keys; std::map<std::pair<double, double>, std::vector<size_t>> layers;
            for (auto i : g)
            {
                const auto k = std::make_pair (regs[i].fa, regs[i].fb);
                if (layers.find (k) == layers.end()) keys.push_back (k);
                layers[k].push_back (i);
            }
            std::stable_sort (keys.begin(), keys.end(), [] (const auto& x, const auto& y) { return (x.first + x.second) / 2 < (y.first + y.second) / 2; });
            if (keys.size() == 1) { for (auto i : layers[keys[0]]) out.push_back (regs[i]); continue; }
            std::vector<int> bounds;
            for (size_t i = 0; i + 1 < keys.size(); ++i)
            {
                int b = (int) std::floor ((keys[i].second + keys[i + 1].first) / 2.0);
                b = std::max (b, bounds.empty() ? 1 : bounds.back() + 1);
                bounds.push_back (std::min (b, 126));
            }
            for (size_t i = 0; i < keys.size(); ++i)
            {
                const int lv = i == 0 ? 1 : bounds[i - 1] + 1;
                const int hv = i == keys.size() - 1 ? 127 : bounds[i];
                if (hv < lv) continue;
                for (auto j : layers[keys[i]]) { Reg x = regs[j]; x.lv = lv; x.hv = hv; out.push_back (x); }
            }
        }
        return out;
    }

    void fillVelHoles (std::vector<Reg>& regs)
    {
        auto groups = groupBy<GKey> (regs, [] (const Reg& r) { return r.kind == kAttack; }, gkey);
        for (auto& g : groups)
        {
            std::set<std::pair<int, int>> bs; for (auto i : g) bs.insert ({ regs[i].lv, regs[i].hv });
            std::vector<std::pair<int, int>> bands (bs.begin(), bs.end());
            std::map<std::pair<int, int>, std::pair<int, int>> fix;
            for (size_t i = 0; i < bands.size(); ++i)
            {
                const int nlv = i == 0 ? 1 : bands[i].first;
                const int nhv = i == bands.size() - 1 ? 127 : std::max (bands[i].second, bands[i + 1].first - 1);
                fix[bands[i]] = { nlv, nhv };
            }
            for (auto i : g) { const auto f = fix[{ regs[i].lv, regs[i].hv }]; regs[i].lv = f.first; regs[i].hv = f.second; }
        }
    }

    std::vector<Reg> fillHoles (std::vector<Reg> regs, int& filled)
    {
        filled = 0;
        std::vector<Reg> out;
        for (auto& r : regs) if (r.kind != kAttack) out.push_back (r);
        std::vector<int> aOrder; std::map<int, std::vector<size_t>> byA;
        for (size_t i = 0; i < regs.size(); ++i)
            if (regs[i].kind == kAttack) { if (byA.find (regs[i].a) == byA.end()) aOrder.push_back (regs[i].a); byA[regs[i].a].push_back (i); }
        for (int a : aOrder)
        {
            auto& rs = byA[a];
            int kmin = 127, kmax = 0;
            for (auto i : rs) { kmin = std::min (kmin, regs[i].lk); kmax = std::max (kmax, regs[i].hk); }
            std::map<std::tuple<int, int, double, double>, std::vector<size_t>> slots;
            std::vector<std::tuple<int, int, double, double>> slotKeys;
            for (auto i : rs)
            {
                const auto k = std::make_tuple (regs[i].rrPos, regs[i].rrLen, regs[i].randLo, regs[i].randHi);
                if (slots.find (k) == slots.end()) slotKeys.push_back (k);
                slots[k].push_back (i);
            }
            for (auto& sk : slotKeys)
            {
                auto& sr = slots[sk];
                for (int v = 1; v < 128; ++v)
                {
                    std::vector<size_t> cov;
                    for (auto i : sr) if (regs[i].lv <= v && v <= regs[i].hv) cov.push_back (i);
                    if (cov.empty()) continue;
                    bool covered[128] = {};
                    for (auto i : cov) for (int k = regs[i].lk; k <= regs[i].hk; ++k) covered[k] = true;
                    for (int k = kmin; k <= kmax; ++k)
                    {
                        if (covered[k]) continue;
                        int cand = -1;
                        int bestAbove = -1, bestBelow = -1;
                        for (auto i : cov) if (regs[i].lk > k && (bestAbove < 0 || regs[i].lk < regs[(size_t) bestAbove].lk)) bestAbove = (int) i;
                        for (auto i : cov) if (regs[i].hk < k && (bestBelow < 0 || regs[i].hk > regs[(size_t) bestBelow].hk)) bestBelow = (int) i;
                        if (bestAbove >= 0) cand = bestAbove;
                        if (bestBelow >= 0 && (cand < 0 || (k - regs[(size_t) bestBelow].hk) < (regs[(size_t) cand].lk - k))) cand = bestBelow;
                        if (cand < 0) continue;
                        const Reg& c = regs[(size_t) cand];
                        const int olk = c.olk >= 0 ? c.olk : c.lk, ohk = c.ohk >= 0 ? c.ohk : c.hk;
                        if ((k < olk && olk - k > 7) || (k > ohk && k - ohk > 7)) continue;
                        const Reg cc = c;
                        for (auto i : sr)
                        {
                            auto& r = regs[i];
                            if ((int) i == cand || (r.lk == cc.lk && r.hk == cc.hk && r.lv == cc.lv && r.hv == cc.hv && r.src == cc.src))
                            {
                                if (r.olk < 0) { r.olk = r.lk; r.ohk = r.hk; }
                                if (k < r.lk) r.lk = k; else if (k > r.hk) r.hk = k;
                            }
                        }
                        for (auto i : cov) for (int q = regs[i].lk; q <= regs[i].hk; ++q) covered[q] = true;
                        ++filled;
                    }
                }
            }
            for (auto i : rs) out.push_back (regs[i]);
        }
        return out;
    }

    //==============================================================================================================
    //  6 · audio: one output sample (torgc.job_render_sample)
    //==============================================================================================================
    std::vector<double> monoOf (const Audio& x)
    {
        const size_t n = (size_t) x.frames();
        std::vector<double> m (n, 0.0);
        for (auto& c : x.ch) for (size_t i = 0; i < n; ++i) m[i] += c[i];
        if (x.channels() > 1) for (auto& v : m) v /= x.channels();
        return m;
    }
    std::vector<double> envelopeDb (const double* m, int64_t len, double sr, double hopS = kHop)
    {
        const int64_t hop = std::max<int64_t> (1, (int64_t) (sr * hopS));
        const int64_t n = len / hop;
        std::vector<double> e;
        if (n == 0)
        {
            double s = 0; for (int64_t i = 0; i < len; ++i) s += m[i] * m[i];
            e.push_back (len ? dbOf (std::sqrt (s / (double) len)) : -200.0);
            return e;
        }
        e.resize ((size_t) n);
        for (int64_t w = 0; w < n; ++w)
        {
            double s = 0; for (int64_t i = 0; i < hop; ++i) { const double v = m[w * hop + i]; s += v * v; }
            e[(size_t) w] = 20.0 * std::log10 (std::max (std::sqrt (s / (double) hop), 1.0e-10));
        }
        return e;
    }
    int64_t findOnset (const std::vector<double>& m, int64_t start, int64_t end, double threshDb = -24.0)
    {
        end = std::min<int64_t> (end, (int64_t) m.size());
        if (end <= start) return start;
        double pk = 0; for (int64_t i = start; i < end; ++i) pk = std::max (pk, std::abs (m[(size_t) i]));
        if (pk <= 0) return start;
        const double lim = pk * std::pow (10.0, threshDb / 20.0);
        for (int64_t i = start; i < end; ++i) if (std::abs (m[(size_t) i]) >= lim) return i;
        return start;
    }
    int64_t trimEnd (const std::vector<double>& m, double sr, int64_t start, double relDb = -60.0, double floorAbs = -96.0)
    {
        const int64_t len = (int64_t) m.size() - start;
        if (len <= 0) return (int64_t) m.size();
        const auto env = envelopeDb (m.data() + start, len, sr);
        if (env.empty()) return (int64_t) m.size();
        const double pk = *std::max_element (env.begin(), env.end());
        const double lim = std::max (pk + relDb, floorAbs);
        int64_t last = -1;
        for (int64_t i = (int64_t) env.size() - 1; i >= 0; --i) if (env[(size_t) i] >= lim) { last = i; break; }
        if (last < 0) return (int64_t) m.size();
        const int64_t hop = (int64_t) (sr * kHop);
        return std::min<int64_t> ((int64_t) m.size(), start + (last + 1) * hop);
    }

    struct F0 { double hz = 0, cents = 0, spread = 0, conf = 0, agree = 0; int frames = 0; };

    struct PerStart { int64_t onset = 0; double rms = 0, pkDb = 0; };
    struct SmpResult
    {
        double sr = 44100; int ch = 1; int64_t frames = 0; double compDb = 0;
        bool hasLoop = false; int64_t loopLs = 0, loopLe = 0, loopXf = 0;
        std::map<int64_t, PerStart> perStart;
        F0 f0; bool hasF0 = false;
    };

    /** A 2nd-order Butterworth high-pass (RBJ, Q = 1/√2 — scipy's bilinear butter(2)) run causally. */
    void highpass8Hz (std::vector<float>& x, double sr)
    {
        const double w0 = 2.0 * kPi * 8.0 / sr, cs = std::cos (w0), al = std::sin (w0) / (2.0 * 0.7071067811865476);
        const double a0 = 1.0 + al;
        const double b0 = (1.0 + cs) / 2.0 / a0, b1 = -(1.0 + cs) / a0, b2 = b0, a1 = -2.0 * cs / a0, a2 = (1.0 - al) / a0;
        double x1 = 0, x2 = 0, y1 = 0, y2 = 0;
        const double first = x.empty() ? 0.0 : x[0];
        for (auto& s : x)
        {
            const double xi = s - first;
            const double y = b0 * xi + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
            x2 = x1; x1 = xi; y2 = y1; y1 = y;
            s = (float) y;
        }
    }

    F0 measureF0 (const std::vector<double>& mono, double sr, int64_t onset, int64_t end, double fExpect);

    struct RenderJob
    {
        juce::String src; juce::File out;
        int64_t start0 = 0; std::vector<int64_t> starts;
        bool srcLoop = false; int64_t ls = 0, le = 0; double xfS = 0;
        int64_t endOpcode = -1;
        double fExpect = 0;
    };

    void writeFlac16 (const juce::File& f, const std::vector<std::vector<float>>& y, double sr)
    {
        f.deleteFile();
        juce::FlacAudioFormat flac;
        std::unique_ptr<juce::OutputStream> os = std::make_unique<juce::FileOutputStream> (f);
        if (static_cast<juce::FileOutputStream*> (os.get())->failedToOpen()) throw Fail ("Cannot write " + f.getFullPathName());
        const int ch = (int) y.size();
        auto w = flac.createWriterFor (os, juce::AudioFormatWriterOptions{}.withSampleRate (sr).withNumChannels (ch).withBitsPerSample (16));
        if (w == nullptr) throw Fail ("Cannot create a FLAC writer for " + f.getFullPathName());
        const int64_t n = y.empty() ? 0 : (int64_t) y[0].size();
        constexpr int kChunk = 16384;
        std::vector<std::vector<int>> buf ((size_t) ch, std::vector<int> (kChunk));
        std::vector<const int*> ptrs ((size_t) ch + 1, nullptr);
        for (int64_t pos = 0; pos < n; pos += kChunk)
        {
            const int m = (int) std::min<int64_t> (kChunk, n - pos);
            for (int c = 0; c < ch; ++c)
            {
                for (int i = 0; i < m; ++i)
                {
                    const double v = std::round ((double) y[(size_t) c][(size_t) (pos + i)] * 32768.0);
                    buf[(size_t) c][(size_t) i] = (int) juce::jlimit (-32768.0, 32767.0, v) * 65536;   // left-justified 32-bit
                }
                ptrs[(size_t) c] = buf[(size_t) c].data();
            }
            ptrs[(size_t) ch] = nullptr;
            if (! w->write (ptrs.data(), m)) throw Fail ("Cannot write " + f.getFullPathName() + " (disk full?)");
        }
        w.reset();
    }

    SmpResult renderSample (const RenderJob& job, Audio x)
    {
        SmpResult res;
        const double sr = x.sr;
        int64_t n = x.frames();
        res.sr = sr; res.ch = x.channels();
        // tp106 DC guard: a causal 8 Hz high-pass only where the file's mean is over 2 % of its RMS
        if (n > (int64_t) sr / 4)
        {
            double worst = 0;
            for (auto& c : x.ch)
            {
                double s = 0, q = 0; for (auto v : c) { s += v; q += (double) v * v; }
                const double mean = std::abs (s / (double) n), rms = std::sqrt (q / (double) n);
                worst = std::max (worst, mean / std::max (rms, 1.0e-12));
            }
            if (worst > 0.02) for (auto& c : x.ch) highpass8Hz (c, sr);
        }
        auto mono = monoOf (x);
        const int64_t start0 = std::min<int64_t> (job.start0, std::max<int64_t> (0, n - 2));
        int64_t end = trimEnd (mono, sr, start0, -60.0);
        if (job.endOpcode > 0) end = std::min (end, job.endOpcode);
        end = std::max (end, std::min<int64_t> (n, start0 + (int64_t) (0.05 * sr)));
        const int64_t firstStart = job.starts.empty() ? start0 : *std::min_element (job.starts.begin(), job.starts.end());
        const int64_t onset0 = findOnset (mono, firstStart, end);
        if (job.srcLoop)
        {
            int64_t ls = job.ls, le = std::min (job.le, n);
            int64_t xf = (int64_t) (job.xfS * sr);
            xf = std::max<int64_t> (0, std::min ({ xf, ls, le - ls }));
            if (le > ls + 1 && ls >= 0)
            {
                res.hasLoop = true; res.loopLs = ls; res.loopLe = le; res.loopXf = xf;
                end = std::max (le, std::min<int64_t> (n, le + (int64_t) (0.05 * sr)));
            }
        }
        end = std::min (end, n);
        const int64_t protect = res.hasLoop ? res.loopLe + kLoopPad : firstStart;
        int64_t fade = res.hasLoop ? (int64_t) (0.03 * sr) : (int64_t) (0.2 * sr);
        fade = std::min (fade, end - protect);
        const int64_t len = std::max<int64_t> (2, end - start0);
        std::vector<std::vector<float>> y ((size_t) x.channels(), std::vector<float> ((size_t) len, 0.0f));
        double pk = 0;
        for (int c = 0; c < x.channels(); ++c)
        {
            auto& src = x.ch[(size_t) c]; auto& d = y[(size_t) c];
            for (int64_t i = 0; i < len && start0 + i < n; ++i) d[(size_t) i] = src[(size_t) (start0 + i)];
            if (fade > 0 && len > fade)
                for (int64_t i = 0; i < fade; ++i)
                    d[(size_t) (len - fade + i)] = (float) (d[(size_t) (len - fade + i)] * std::cos ((double) i / (double) (fade - 1 > 0 ? fade - 1 : 1) * kPi / 2.0));
            for (auto v : d) pk = std::max (pk, (double) std::abs (v));
        }
        res.compDb = pk > 1.0e-9 ? 20.0 * std::log10 (pk / kPeakTarget) : 0.0;
        const double scale = pk > 1.0e-9 ? kPeakTarget / pk : 1.0;
        for (auto& d : y)
            for (auto& v : d)
            {
                double q = juce::jlimit (-1.0, 1.0 - 1.0 / 32768.0, (double) v * scale);
                v = (float) (std::round (q * 32768.0) / 32768.0);
            }
        writeFlac16 (job.out, y, sr);
        if (job.fExpect > 0) { res.f0 = measureF0 (mono, sr, onset0, std::min (end, n), job.fExpect); res.hasF0 = true; }
        res.frames = len;
        if (res.hasLoop) { res.loopLs -= start0; res.loopLe -= start0; }
        for (auto s : job.starts)
        {
            const int64_t ons = findOnset (mono, s, end);
            const int64_t e2 = std::min<int64_t> ((int64_t) mono.size(), ons + (int64_t) (0.5 * sr));
            double q = 0; for (int64_t i = ons; i < e2; ++i) q += mono[(size_t) i] * mono[(size_t) i];
            const double rms = e2 > ons ? std::sqrt (q / (double) (e2 - ons)) : 0.0;
            double pks = 0; for (int64_t i = s; i < end; ++i) pks = std::max (pks, std::abs (mono[(size_t) i]));
            res.perStart[s] = { ons - start0, rms, roundN (dbOf (pks * scale), 2) };
        }
        return res;
    }

    //==============================================================================================================
    //  7 · pitch (analyse.measure_f0, without numpy: direct YIN and a direct DTFT for the partial)
    //==============================================================================================================
    std::vector<double> yinCmnd (const double* x, int n, int tauMax)
    {
        const int w = n - tauMax;
        std::vector<double> d ((size_t) tauMax + 1, 0.0), out ((size_t) tauMax + 1, 1.0);
        for (int tau = 1; tau <= tauMax; ++tau)
        {
            double s = 0;
            for (int j = 0; j < w; ++j) { const double t = x[j] - x[j + tau]; s += t * t; }
            d[(size_t) tau] = s;
        }
        double cum = 0;
        for (int tau = 1; tau <= tauMax; ++tau)
        {
            cum += d[(size_t) tau];
            const double cm = cum / tau;
            out[(size_t) tau] = d[(size_t) tau] / std::max (cm, 1.0e-20);
        }
        return out;
    }

    double median (std::vector<double> v)
    {
        if (v.empty()) return 0.0;
        std::sort (v.begin(), v.end());
        const size_t n = v.size();
        return n % 2 ? v[n / 2] : 0.5 * (v[n / 2 - 1] + v[n / 2]);
    }
    double percentile (std::vector<double> v, double p)       // numpy linear interpolation
    {
        if (v.empty()) return 0.0;
        std::sort (v.begin(), v.end());
        const double pos = p / 100.0 * (double) (v.size() - 1);
        const size_t i = (size_t) std::floor (pos);
        const double f = pos - (double) i;
        return i + 1 < v.size() ? v[i] + f * (v[i + 1] - v[i]) : v[i];
    }

    double dtftMagDb (const std::vector<double>& fr, double hz, double sr)
    {
        const double w = 2.0 * kPi * hz / sr;
        double re = 0, im = 0;
        const size_t n = fr.size();
        // Goertzel-free direct sum with a recurrence (exact enough over ≤ a few 10k samples)
        const double cw = std::cos (w), sw = std::sin (w);
        double c = 1.0, s = 0.0;
        for (size_t i = 0; i < n; ++i)
        {
            re += fr[i] * c; im -= fr[i] * s;
            const double c2 = c * cw - s * sw; s = s * cw + c * sw; c = c2;
        }
        return 20.0 * std::log10 (std::max (std::sqrt (re * re + im * im), 1.0e-12));
    }

    F0 measureF0 (const std::vector<double>& mono, double sr, int64_t onset, int64_t end, double fExpect)
    {
        F0 res;
        const double searchCents = 150.0;
        if (fExpect <= 0 || end - onset < (int64_t) (0.03 * sr)) return res;
        const double T0 = sr / fExpect;
        const int k = std::max (1, (int) std::ceil (256.0 / T0));
        const int lo1 = std::max (2, (int) std::floor (T0 * std::pow (2.0, -searchCents / 1200.0)) - 1);
        const int hi1 = (int) std::ceil (T0 * std::pow (2.0, searchCents / 1200.0)) + 1;
        const int hi = (int) std::ceil (k * hi1 + T0) + 2;
        const int win = (int) std::max (3.0 * hi, 0.04 * sr);
        if (hi > 20000) return res;                                   // below ~5 Hz: nothing to tune
        const double* seg = mono.data() + onset;
        const int64_t segLen = std::min<int64_t> (end, (int64_t) mono.size()) - onset;
        const auto env = envelopeDb (seg, segLen, sr);
        if (env.empty()) return res;
        const double pk = *std::max_element (env.begin(), env.end());
        int64_t skip = (int64_t) std::max (0.06 * sr, 6.0 * T0);
        if (segLen - skip < win + hi) skip = (int64_t) std::max (0.015 * sr, 2.0 * T0);
        const int64_t hopEnv = (int64_t) (sr * kHop);
        std::vector<int64_t> starts;
        const int64_t step = std::max<int64_t> ((int64_t) (0.05 * sr), win / 2);
        for (int64_t s = skip; s + win + hi <= segLen && starts.size() < 8; s += step)
        {
            const size_t e0 = (size_t) std::min<int64_t> ((int64_t) env.size() - 1, s / hopEnv);
            const size_t e1 = (size_t) std::min<int64_t> ((int64_t) env.size() - 1, (s + win) / hopEnv);
            double mn = 1e9; for (size_t q = e0; q <= e1; ++q) mn = std::min (mn, env[q]);
            if (mn < pk - 35.0) break;
            starts.push_back (s);
        }
        auto parab = [] (const std::vector<double>& d, int i, double& b) {
            const double a = d[(size_t) i - 1], bb = d[(size_t) i], c = d[(size_t) i + 1];
            const double den = a - 2 * bb + c;
            const double off = std::abs (den) > 1.0e-12 ? 0.5 * (a - c) / den : 0.0;
            b = bb;
            return i + std::max (-0.5, std::min (0.5, off));
        };
        std::vector<double> ests, confs;
        for (auto s : starts)
        {
            const auto d = yinCmnd (seg + s, win + hi, hi);
            int i1 = lo1; for (int t = lo1; t <= hi1 && t <= hi; ++t) if (d[(size_t) t] < d[(size_t) i1]) i1 = t;
            if (i1 <= lo1 || i1 >= hi1) continue;
            double b1; double lag1 = parab (d, i1, b1);
            if (k > 1)
            {
                const double c2 = k * lag1;
                const int lo2 = (int) std::floor (c2 - lag1 / 3), hi2 = (int) std::ceil (c2 + lag1 / 3);
                if (hi2 + 1 <= hi)
                {
                    int i2 = lo2; for (int t = lo2; t <= hi2; ++t) if (d[(size_t) t] < d[(size_t) i2]) i2 = t;
                    if (lo2 < i2 && i2 < hi2) { double bb; lag1 = parab (d, i2, bb) / k; }
                }
            }
            ests.push_back (sr / lag1);
            confs.push_back (1.0 - b1);
        }
        if (ests.empty()) return res;
        std::vector<double> cents, cf;
        for (size_t i = 0; i < ests.size(); ++i) if (confs[i] >= 0.6) { cents.push_back (1200.0 * std::log2 (ests[i] / fExpect)); cf.push_back (confs[i]); }
        if (cents.empty()) { for (size_t i = 0; i < ests.size(); ++i) cents.push_back (1200.0 * std::log2 (ests[i] / fExpect)); cf = confs; }
        const double med = median (cents);
        const double q1 = cents.size() > 1 ? percentile (cents, 25) : med, q3 = cents.size() > 1 ? percentile (cents, 75) : med;
        res.hz = fExpect * std::pow (2.0, med / 1200.0); res.cents = med; res.spread = q3 - q1; res.conf = median (cf); res.frames = (int) cents.size();
        // refinement: the fundamental PARTIAL from a long Hann-windowed spectrum around the YIN estimate
        const int64_t a0 = starts[0];
        const int64_t b0 = std::min<int64_t> (segLen, std::max<int64_t> (starts.back() + win, a0 + (int64_t) (0.25 * sr)));
        int64_t eOk = a0;
        while (eOk + hopEnv <= b0 && env[(size_t) std::min<int64_t> ((int64_t) env.size() - 1, eOk / hopEnv)] >= pk - 35.0) eOk += hopEnv;
        const int64_t L = eOk - a0;
        if (L >= (int64_t) (8 * T0) && L >= (int64_t) (0.08 * sr) && L < (int64_t) (4.0 * sr))
        {
            std::vector<double> fr ((size_t) L);
            for (int64_t i = 0; i < L; ++i) fr[(size_t) i] = seg[a0 + i] * (L > 1 ? 0.5 - 0.5 * std::cos (2.0 * kPi * (double) i / (double) (L - 1)) : 1.0);
            int nfft = 1; while (nfft < L * 4) nfft <<= 1;
            const double hz0 = res.hz;
            const int iLo = (int) (hz0 * std::pow (2.0, -40.0 / 1200.0) * nfft / sr);
            const int iHi = (int) std::ceil (hz0 * std::pow (2.0, 40.0 / 1200.0) * nfft / sr);
            // the strongest partial: the harmonics of hz0 (a full spectrum would also see noise peaks, which never win here)
            double top = -1e9;
            for (int h = 1; h <= 24 && h * hz0 < sr * 0.5; ++h)
            {
                const int c = (int) std::round (h * hz0 * nfft / sr);
                for (int q = c - 2; q <= c + 2; ++q) if (q * sr / nfft >= 20.0) top = std::max (top, dtftMagDb (fr, q * sr / nfft, sr));
            }
            if (1 <= iLo && iLo < iHi && iHi < nfft / 2 - 1 && iHi - iLo < 400)
            {
                std::vector<double> lx ((size_t) (iHi - iLo + 3));
                for (int q = iLo - 1; q <= iHi + 1; ++q) lx[(size_t) (q - iLo + 1)] = dtftMagDb (fr, q * sr / nfft, sr);
                int best = iLo; for (int q = iLo; q <= iHi; ++q) if (lx[(size_t) (q - iLo + 1)] > lx[(size_t) (best - iLo + 1)]) best = q;
                if (iLo < best && best < iHi && lx[(size_t) (best - iLo + 1)] >= top - 24.0)
                {
                    const double a = lx[(size_t) (best - iLo)], b = lx[(size_t) (best - iLo + 1)], c = lx[(size_t) (best - iLo + 2)];
                    const double den = a - 2 * b + c;
                    const double off = std::abs (den) > 1.0e-12 ? 0.5 * (a - c) / den : 0.0;
                    const double hzS = (best + std::max (-0.5, std::min (0.5, off))) * sr / nfft;
                    const double cS = 1200.0 * std::log2 (hzS / fExpect);
                    res.hz = hzS; res.cents = cS; res.agree = std::abs (cS - med);
                }
            }
        }
        return res;
    }

    //==============================================================================================================
    //  8 · map assembly (torgc.Compiler.assemble)
    //==============================================================================================================
    struct Rec
    {
        int a = 0, kind = kAttack, smp = 0, lk = 0, hk = 127, lv = 1, hv = 127, root = 60;
        double cents = 0, gainDb = 0, gainNorm = 1; int pan = 0;
        int64_t start = 0, end = 0, onset = 0;
        juce::String loop = "no_loop";
        int64_t ls = 0, le = 0, xf = 0, tailLs = 0, tailLe = 0;
        int rrPos = 0, rrLen = 1; double randLo = 0, randHi = 1;
        int grp = 0, offBy = 0; juce::String offMode = "normal";
        Env env; double rtDecay = 0;
        std::vector<std::pair<int, double>> velCurve;
        double tfix = 0;
        // compile-time only
        double gDb = 0, rms = 0, pk = 0; std::vector<double> curve; F0 f0; bool hasF0 = false;
    };
    bool recLess (const Rec& x, const Rec& y)
    {
        return std::tie (x.a, x.kind, x.lk, x.lv, x.rrPos, x.rrLen, x.randLo, x.randHi) < std::tie (y.a, y.kind, y.lk, y.lv, y.rrPos, y.rrLen, y.randLo, y.randHi);
    }
    bool regLess (const Reg& x, const Reg& y)
    {
        return std::tie (x.a, x.kind, x.lk, x.lv, x.rrPos, x.rrLen, x.randLo, x.randHi) < std::tie (y.a, y.kind, y.lk, y.lv, y.rrPos, y.rrLen, y.randLo, y.randHi);
    }

    /** repair_rr: complete sequential sets, random slots that cover [0, 1) without gaps. */
    void repairRR (std::vector<Rec>& recs)
    {
        std::map<std::tuple<int, int, int, int, int, int>, std::vector<size_t>> groups;
        std::vector<std::tuple<int, int, int, int, int, int>> order;
        for (size_t i = 0; i < recs.size(); ++i)
        {
            const auto k = std::make_tuple (recs[i].a, recs[i].kind, recs[i].lk, recs[i].hk, recs[i].lv, recs[i].hv);
            if (groups.find (k) == groups.end()) order.push_back (k);
            groups[k].push_back (i);
        }
        std::vector<Rec> add;
        for (auto& k : order)
        {
            auto& g = groups[k];
            std::map<int, std::vector<size_t>> byLen;
            for (auto i : g) byLen[recs[i].rrLen].push_back (i);
            for (auto& [L, rs] : byLen)
            {
                if (L <= 1) continue;
                std::map<int, size_t> have; for (auto i : rs) have[recs[i].rrPos] = i;
                std::vector<int> hk; for (auto& h : have) hk.push_back (h.first);
                for (int pos = 0; pos < L; ++pos)
                    if (have.find (pos) == have.end()) { Rec c = recs[have[hk[(size_t) pos % hk.size()]]]; c.rrPos = pos; c.rrLen = L; add.push_back (c); }
            }
            std::set<std::pair<double, double>> sl; for (auto i : g) sl.insert ({ recs[i].randLo, recs[i].randHi });
            std::vector<std::pair<double, double>> slots (sl.begin(), sl.end());
            if (slots.size() > 1 || (! slots.empty() && slots[0] != std::make_pair (0.0, 1.0)))
            {
                std::map<std::pair<double, double>, std::pair<double, double>> remap;
                double prevHi = 0.0;
                for (size_t i = 0; i < slots.size(); ++i)
                {
                    const double nlo = i == 0 ? 0.0 : std::min (slots[i].first, prevHi);
                    const double nhi = i == slots.size() - 1 ? 1.0 : std::max (slots[i].second, slots[i + 1].first);
                    remap[slots[i]] = { round4 (nlo), round4 (nhi) };
                    prevHi = nhi;
                }
                for (auto i : g) { const auto t = remap[{ recs[i].randLo, recs[i].randHi }]; recs[i].randLo = t.first; recs[i].randHi = t.second; }
            }
        }
        for (auto& c : add) recs.push_back (c);
    }

    /** compute_tfix for a struck/plucked-style instrument (a note-median per (root, rr, rand); root fixes allowed). */
    void computeTfix (std::vector<Rec>& recs, int numArtics)
    {
        std::map<size_t, std::pair<double, bool>> meas;
        for (size_t i = 0; i < recs.size(); ++i)
        {
            auto& x = recs[i];
            if (x.kind != kAttack || ! x.hasF0 || x.f0.hz <= 0) continue;
            const double dev = 1200.0 * std::log2 (x.f0.hz / midiHz (x.root));
            const double cf = x.cents - 100.0 * std::trunc (x.cents / 100.0);
            double tot = dev + cf;
            if (std::abs (x.cents - cf) >= 100.0 && std::abs (dev + x.cents) <= 25.0) tot = dev + x.cents;
            const int nn = (int) std::round (tot / 100.0);
            if (nn != 0 && std::abs (nn) == 1 && x.f0.conf >= 0.65 && x.f0.spread <= 10.0 && std::abs (tot - 100.0 * nn) <= 25.0)
            {
                x.root += nn; tot -= 100.0 * nn;
            }
            bool ok = x.f0.conf >= 0.6 && std::abs (tot) <= 60.0 && x.f0.spread <= 12.0 && x.f0.agree <= 10.0 && x.f0.frames >= 3;
            if (ok && std::abs (tot) > 30.0 && (x.f0.frames < 5 || x.f0.spread > 8.0)) ok = false;
            meas[i] = { tot, ok };
        }
        for (int a = 0; a < numArtics; ++a)
        {
            using NK = std::tuple<int, int, int, double, double>;
            auto nk = [&] (const Rec& x) { return NK (x.root, x.rrPos, x.rrLen, x.randLo, x.randHi); };
            std::map<NK, std::vector<double>> perNote;
            std::map<size_t, double> own;
            for (auto& [i, m] : meas)
                if (recs[i].a == a && m.second) { own[i] = -m.first; perNote[nk (recs[i])].push_back (own[i]); }
            for (size_t i = 0; i < recs.size(); ++i)
            {
                auto& x = recs[i];
                if (x.a != a || x.kind != kAttack) continue;
                std::vector<double> v;
                auto it = perNote.find (nk (x));
                if (it != perNote.end()) v = it->second;
                if (v.empty())
                {
                    std::vector<double> nb;
                    for (auto& [j, o] : own) if (std::abs (recs[j].root - x.root) <= 4) nb.push_back (o);
                    if (nb.size() >= 2) v = nb;
                }
                x.tfix = v.empty() ? 0.0 : roundN (juce::jlimit (-60.0, 60.0, median (v)), 1);
            }
        }
        std::map<std::pair<int, int>, std::vector<double>> byRoot;
        for (auto& x : recs) if (x.kind == kAttack && x.tfix != 0.0) byRoot[{ x.a, x.root }].push_back (x.tfix);
        for (auto& x : recs)
            if (x.kind == kRelease)
            {
                auto it = byRoot.find ({ x.a, x.root });
                x.tfix = it != byRoot.end() ? roundN (median (it->second), 1) : 0.0;
            }
    }

    //==============================================================================================================
    //  9 · calibration + preview (analyse.Renderer, loudness_k)
    //==============================================================================================================
    struct Renderer
    {
        const std::vector<Rec>& regs;
        juce::File dir; const juce::StringArray& samples;
        double outSr = 48000.0, velo = 1.0;
        std::map<int, Audio> cache;

        Renderer (const std::vector<Rec>& r, const juce::File& d, const juce::StringArray& s, double v) : regs (r), dir (d), samples (s), velo (v) {}

        const Audio& sample (int smp)
        {
            auto it = cache.find (smp);
            if (it != cache.end()) return it->second;
            Audio a;
            std::unique_ptr<juce::AudioFormatReader> rd (formats().createReaderFor (dir.getChildFile ("samples").getChildFile (samples[smp])));
            if (rd != nullptr) readAll (*rd, a, samples[smp]);
            return cache[smp] = std::move (a);
        }
        static double velGain (const std::vector<std::pair<int, double>>& c, double v)
        {
            if (c.empty()) return 1.0;
            if (v <= c.front().first) return c.front().second;
            for (size_t i = 1; i < c.size(); ++i)
                if (v <= c[i].first) { const double t = (v - c[i - 1].first) / std::max (1e-9, (double) (c[i].first - c[i - 1].first)); return c[i - 1].second + t * (c[i].second - c[i - 1].second); }
            return c.back().second;
        }
        std::vector<std::pair<const Rec*, double>> pick (int note, int vel, int artic, int kind, bool xfade = true) const
        {
            std::vector<const Rec*> rs;
            for (auto& r : regs)
                if (r.a == artic && r.kind == kind && r.lk <= note && note <= r.hk && r.rrPos == 0 && r.randLo <= 1.0e-9) rs.push_back (&r);
            std::vector<std::pair<const Rec*, double>> out;
            if (kind != kAttack || ! xfade) { for (auto* r : rs) if (r->lv <= vel && vel <= r->hv) out.push_back ({ r, 1.0 }); return out; }
            std::set<std::pair<int, int>> bs; for (auto* r : rs) bs.insert ({ r->lv, r->hv });
            std::vector<std::pair<int, int>> bands (bs.begin(), bs.end());
            int i = -1; for (size_t q = 0; q < bands.size(); ++q) if (bands[q].first <= vel && vel <= bands[q].second) { i = (int) q; break; }
            if (i < 0) return out;
            const double ci = (bands[(size_t) i].first + bands[(size_t) i].second) / 2.0;
            int j = -1;
            if (vel >= ci && (size_t) i + 1 < bands.size()) j = i + 1; else if (vel < ci && i > 0) j = i - 1;
            double wi = 1.0, wj = 0.0;
            if (j >= 0)
            {
                const double cj = (bands[(size_t) j].first + bands[(size_t) j].second) / 2.0;
                const double t = juce::jlimit (0.0, 1.0, std::abs (vel - ci) / std::max (1e-9, std::abs (cj - ci)));
                wi = std::cos (t * kPi / 2); wj = std::sin (t * kPi / 2);
            }
            for (auto* r : rs)
            {
                const auto b = std::make_pair (r->lv, r->hv);
                if (b == bands[(size_t) i]) out.push_back ({ r, wi });
                else if (j >= 0 && b == bands[(size_t) j] && wj > 1e-6) out.push_back ({ r, wj });
            }
            return out;
        }
        void renderRegion (const Rec& r, int note, double dur, int vel, double hold, bool noLoop, std::vector<double>& L, std::vector<double>& R, int64_t at, double w)
        {
            const Audio& x = sample (r.smp);
            if (x.frames() < 4) return;
            const double ratio = std::pow (2.0, (note - r.root) / 12.0 + r.cents / 1200.0) * x.sr / outSr;
            const int64_t nOut = (int64_t) (dur * outSr);
            const bool loops = ! noLoop && (r.loop == "sustain" || r.loop == "continuous") && r.le > r.ls;
            const double g = w * std::pow (10.0, r.gainDb / 20.0) * r.gainNorm * (1.0 - velo * (1.0 - velGain (r.velCurve, vel)));
            const double p = juce::jlimit (-1.0, 1.0, r.pan / 100.0);
            const double gl = g * std::cos ((p + 1) * kPi / 4) * std::sqrt (2.0), gr = g * std::sin ((p + 1) * kPi / 4) * std::sqrt (2.0);
            const int64_t na = std::min<int64_t> (nOut, (int64_t) (std::max (r.env.a, 0.001) * outSr));
            const int64_t h = (int64_t) (hold * outSr);
            const double rel = std::max (r.env.r, 0.05);
            const int64_t fr = x.frames();
            auto at_ = [&] (int c, double pos) -> double
            {
                // pos in region-relative frames from r.start, loop-wrapped
                double q = (double) r.start + pos;
                if (loops && q >= (double) r.le) q = (double) r.ls + std::fmod (q - (double) r.ls, (double) (r.le - r.ls));
                const int64_t i0 = (int64_t) q;
                if (i0 < 0 || i0 + 1 >= std::min<int64_t> (fr, r.end)) return 0.0;
                const double f = q - (double) i0;
                const auto& ch = x.ch[(size_t) std::min (c, x.channels() - 1)];
                return ch[(size_t) i0] + f * (ch[(size_t) i0 + 1] - ch[(size_t) i0]);
            };
            for (int64_t i = 0; i < nOut && at + i < (int64_t) L.size(); ++i)
            {
                double e = 1.0;
                if (i < na) e *= (double) i / (double) std::max<int64_t> (1, na - 1);
                if (i >= h && hold < dur) e *= std::exp (-(double) (i - h) / outSr * 6.9 / rel);
                const double pos = (double) i * ratio;
                L[(size_t) (at + i)] += at_ (0, pos) * gl * e;
                R[(size_t) (at + i)] += at_ (1, pos) * gr * e;
            }
        }
        void render (int note, int vel, double dur, double hold, bool withRelease, std::vector<double>& L, std::vector<double>& R)
        {
            const int64_t n = (int64_t) (dur * outSr);
            L.assign ((size_t) n, 0.0); R.assign ((size_t) n, 0.0);
            for (auto& [r, w] : pick (note, vel, 0, kAttack)) renderRegion (*r, note, dur, vel, hold, false, L, R, 0, w);
            if (withRelease && hold < dur)
            {
                const int64_t h = (int64_t) (hold * outSr);
                for (auto& [r, w] : pick (note, vel, 0, kRelease))
                    renderRegion (*r, note, dur - hold, vel, dur, true, L, R, h, w * std::pow (10.0, -r->rtDecay * hold / 20.0));
            }
            const int64_t f = std::min<int64_t> (n, (int64_t) (0.03 * outSr));
            for (int64_t i = 0; i < f; ++i) { const double g = 1.0 - (double) i / (double) std::max<int64_t> (1, f - 1); L[(size_t) (n - f + i)] *= g; R[(size_t) (n - f + i)] *= g; }
        }
    };

    double loudnessK (const std::vector<double>& L, const std::vector<double>& R, size_t a, size_t b, double sr)
    {
        // BS.1770 K-weighting: the analog prototypes bilinear-transformed (analyse._k_filter_coeffs)
        double f0 = 1681.974450955533, G = 3.999843853973347, Q = 0.7071752369554196;
        double K = std::tan (kPi * f0 / sr);
        const double Vh = std::pow (10.0, G / 20.0), Vb = std::pow (Vh, 0.4996667741545416);
        double a0 = 1.0 + K / Q + K * K;
        const double b1[3] = { (Vh + Vb * K / Q + K * K) / a0, 2.0 * (K * K - Vh) / a0, (Vh - Vb * K / Q + K * K) / a0 };
        const double a1[3] = { 1.0, 2.0 * (K * K - 1.0) / a0, (1.0 - K / Q + K * K) / a0 };
        f0 = 38.13547087602444; Q = 0.5003270373238773; K = std::tan (kPi * f0 / sr); a0 = 1.0 + K / Q + K * K;
        const double b2[3] = { 1.0, -2.0, 1.0 };
        const double a2[3] = { 1.0, 2.0 * (K * K - 1.0) / a0, (1.0 - K / Q + K * K) / a0 };
        double p = 0;
        b = std::min (b, L.size());
        if (b <= a) return -200.0;
        for (const auto* ch : { &L, &R })
        {
            double x1 = 0, x2 = 0, y1 = 0, y2 = 0, u1 = 0, u2 = 0, z1 = 0, z2 = 0, s = 0;
            for (size_t i = a; i < b; ++i)
            {
                const double x = (*ch)[i];
                const double y = b1[0] * x + b1[1] * x1 + b1[2] * x2 - a1[1] * y1 - a1[2] * y2;
                x2 = x1; x1 = x; y2 = y1; y1 = y;
                const double z = b2[0] * y + b2[1] * u1 + b2[2] * u2 - a2[1] * z1 - a2[2] * z2;
                u2 = u1; u1 = y; z2 = z1; z1 = z;
                s += z * z;
            }
            p += s / (double) (b - a);
        }
        return p > 1.0e-20 ? -0.691 + 10.0 * std::log10 (p) : -200.0;
    }

    //==============================================================================================================
    //  10 · the User/ folder
    //==============================================================================================================
    juce::CriticalSection& userLock() { static juce::CriticalSection c; return c; }

    struct ScopedUserLock
    {
        juce::ScopedLock sl { userLock() };
        juce::InterProcessLock ipl { "TerrainOrganicsUser" };
        bool ok = false;
        ScopedUserLock() { for (int i = 0; i < 50 && ! (ok = ipl.enter (100)); ++i) {} }
        ~ScopedUserLock() { if (ok) ipl.exit(); }
    };

    juce::var readJson (const juce::File& f)
    {
        if (! f.existsAsFile()) return {};
        juce::var v;
        if (juce::JSON::parse (f.loadFileAsString(), v).failed()) return {};
        return v;
    }
    bool writeJsonAtomic (const juce::File& f, const juce::var& v)
    {
        juce::TemporaryFile tmp (f);
        if (! tmp.getFile().replaceWithText (juce::JSON::toString (v, false))) return false;
        return tmp.overwriteTargetFileWithTemporary();
    }

    juce::String slugOf (const juce::String& name)
    {
        juce::String s; bool dash = false;
        const auto lower = name.toLowerCase();
        for (auto p = lower.getCharPointer(); ! p.isEmpty(); ++p)
        {
            const juce_wchar c = *p;
            if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) { s += juce::String::charToString (c); dash = false; }
            else if (! dash && s.isNotEmpty()) { s += "-"; dash = true; }
        }
        while (s.endsWithChar ('-')) s = s.dropLastCharacters (1);
        if (s.length() > 48) s = s.substring (0, 48).trimCharactersAtEnd ("-");
        return s.isEmpty() ? juce::String ("instrument") : s;
    }

    juce::String guessFamily (const juce::String& nameIn, int gmProgram)
    {
        const auto n = nameIn.toLowerCase();
        struct KW { const char* k; const char* f; };
        static const KW kws[] = {
            { "rhodes", "rhodes" }, { "wurli", "rhodes" }, { "e-piano", "rhodes" }, { "epiano", "rhodes" }, { "electric piano", "rhodes" }, { "ep ", "rhodes" },
            { "upright", "upright" }, { "honky", "upright" }, { "harpsi", "harpsi" }, { "clavinet", "clav" }, { "clav", "clav" },
            { "piano", "grand" }, { "tonewheel", "tonewheel" }, { "drawbar", "tonewheel" }, { "b3", "tonewheel" }, { "hammond", "tonewheel" },
            { "organ", "organ" }, { "accordion", "organ" }, { "harmonium", "organ" },
            { "violin", "violin" }, { "viola", "violin" }, { "fiddle", "violin" }, { "string", "violin" }, { "cello", "cello" },
            { "contrabass", "bass" }, { "double bass", "bass" }, { "upright bass", "bass" }, { "bass", "bass" },
            { "guitar", "guitar" }, { "banjo", "guitar" }, { "mandolin", "guitar" }, { "ukulele", "guitar" }, { "harp", "harp" },
            { "koto", "koto" }, { "zither", "koto" }, { "dulcimer", "koto" }, { "flute", "flute" }, { "piccolo", "flute" }, { "recorder", "flute" },
            { "whistle", "flute" }, { "pan pipe", "flute" }, { "clarinet", "clarinet" }, { "oboe", "clarinet" }, { "bassoon", "clarinet" },
            { "sax", "sax" }, { "trumpet", "trumpet" }, { "cornet", "trumpet" }, { "flugel", "trumpet" }, { "horn", "horn" }, { "trombone", "horn" },
            { "tuba", "horn" }, { "brass", "trumpet" }, { "choir", "choir" }, { "voice", "choir" }, { "vocal", "choir" }, { "aah", "choir" }, { "ooh", "choir" },
            { "glock", "glock" }, { "celesta", "glock" }, { "bell", "glock" }, { "chime", "glock" }, { "marimba", "marimba" }, { "xylo", "marimba" },
            { "vibra", "marimba" }, { "kalimba", "kalimba" }, { "mbira", "kalimba" }, { "music box", "musicbox" }, { "musicbox", "musicbox" } };
        for (auto& k : kws) if (n.contains (k.k)) return k.f;
        if (gmProgram >= 0 && gmProgram < 128)
        {
            const int p = gmProgram;
            if (p == 4 || p == 5) return "rhodes";
            if (p == 6) return "harpsi";
            if (p == 7) return "clav";
            if (p < 8) return "grand";
            if (p == 8 || p == 9 || p == 14) return "glock";
            if (p == 10) return "musicbox";
            if (p >= 11 && p <= 13) return "marimba";
            if (p == 15) return "koto";
            if (p >= 16 && p <= 18) return "tonewheel";
            if (p < 24) return "organ";
            if (p < 32) return "guitar";
            if (p < 40) return "bass";
            if (p == 40 || p == 41 || p == 44 || p == 45) return "violin";
            if (p == 42) return "cello";
            if (p == 43) return "bass";
            if (p == 46) return "harp";
            if (p == 47) return "marimba";
            if (p >= 52 && p <= 54) return "choir";
            if (p < 56) return "violin";
            if (p == 56 || p == 59 || (p >= 61 && p <= 63)) return "trumpet";
            if (p < 64) return "horn";
            if (p < 68) return "sax";
            if (p < 72) return "clarinet";
            if (p < 80) return "flute";
            if (p == 91 || p == 85) return "choir";
            if (p == 108) return "kalimba";
            if (p == 105 || p == 106 || p == 107) return "guitar";
            if (p == 109) return "organ";
            if (p == 110) return "violin";
            if (p == 112) return "glock";
        }
        return "grand";
    }

    //==============================================================================================================
    //  11 · the import
    //==============================================================================================================
    struct Built                                              // one instrument, built in its temp folder
    {
        juce::File tmp;
        juce::String name, family, credit, sourceName;
        juce::var map;                                        // map.json (the "id" is set at finalise)
        double sizeMB = 0;
    };

    struct Artic { juce::String name; int sw = -1; };

    /** The keyswitches of a file → articulations (torgc recipe "artics" with "sw", built automatically). */
    std::vector<Artic> articsOf (const SfzFile& f, juce::String& warn)
    {
        std::map<int, juce::String> sws;
        for (auto& r : f.regions)
            if (const auto* s = get (r, "sw_last"))
            {
                int n; if (! noteToMidi (*s, 0, 0, n)) continue;
                auto& lab = sws[n];
                if (lab.isEmpty()) if (const auto* l = get (r, "sw_label")) lab = l->trim();
            }
        std::vector<Artic> out;
        if (sws.empty()) { out.push_back ({ "Normal", -1 }); return out; }
        juce::StringArray used;
        for (auto& [n, lab] : sws)
        {
            if (out.size() >= 8) { warn << (warn.isEmpty() ? "" : " ") << "Only the first 8 keyswitches were imported."; break; }
            juce::String nm = lab.isNotEmpty() ? lab : noteName (n);
            juce::String u = nm; int k = 2; while (used.contains (u)) u = nm + " " + juce::String (k++);
            used.add (u);
            out.push_back ({ u, n });
        }
        return out;
    }

    Built buildOne (const juce::File& libUser, const SfzFile& f, const std::map<juce::String, Sf2Source>* sf2srcs, const Sf2* sf2,
                    const juce::String& name, const juce::String& family, const juce::String& credit, bool allowLarge,
                    const std::function<void (double, const juce::String&)>& prog, const std::atomic<bool>* cancel,
                    juce::String& warn, double& mbOut, bool& needConfirm)
    {
        auto checkCancel = [cancel] { if (cancel != nullptr && cancel->load()) throw Fail ("Cancelled"); };
        std::map<juce::String, SrcInfo> info;
        if (sf2srcs != nullptr)
            for (auto& [k, s] : *sf2srcs)
            {
                SrcInfo si; si.exists = true;
                int64_t fr = std::numeric_limits<int64_t>::max(); for (auto q : s.sids) fr = std::min (fr, sf2->sampleFrames (q));
                si.frames = fr; si.channels = (int) s.sids.size(); si.sr = sf2->shdr[(size_t) s.sids[0]].rate;
                info[k] = si;
            }
        // ── parse → regions per articulation (torgc.Compiler._collect_one)
        auto artics = articsOf (f, warn);
        Interpreter ip (f, info);
        std::vector<Reg> regs;
        for (int ai = 0; ai < (int) artics.size(); ++ai)
        {
            const int sw = artics[(size_t) ai].sw;
            for (auto& r : f.regions)
            {
                const auto* rsw = get (r, "sw_last");
                if (sw >= 0)
                {
                    if (rsw != nullptr) { int n; if (! noteToMidi (*rsw, 0, 0, n) || n != sw) continue; }
                }
                else if (rsw != nullptr)
                {
                    const auto* d = get (r, "sw_default"); int a1, a2;
                    if (d != nullptr && noteToMidi (*rsw, 0, 0, a1) && noteToMidi (*d, 0, 0, a2) && a1 != a2) continue;
                }
                auto v = ip.interpret (r, ai);
                regs.insert (regs.end(), v.begin(), v.end());
            }
            if ((int) regs.size() > kMaxRegions) throw Fail ("The file maps more than 65000 regions - too many for one instrument.");
        }
        checkCancel();
        // missing / undecodable samples: every one missing → an error; a few → imported without them, with a warning
        {
            std::set<juce::String> refd;
            for (auto& r : f.regions) if (const auto* s = get (r, "_sample"); s != nullptr && s->isNotEmpty() && ! s->startsWithChar ('*')) refd.insert (*s);
            const auto nMiss = ip.missing.size(), nBad = ip.undecodable.size();
            if (nBad > 0 && (regs.empty() || nBad * 10 > refd.size()))
                throw Fail ("Cannot decode " + juce::String ((int) nBad) + " of the samples, e.g. " + juce::File (*ip.undecodable.begin()).getFileName()
                            + " (WAV, AIFF, FLAC and OGG are supported)");
            if (nBad > 0) warn << (warn.isEmpty() ? "" : " ") << juce::String ((int) nBad) << " undecodable sample" << (nBad > 1 ? "s were" : " was") << " skipped.";
            if (nMiss > 0 && (regs.empty() || nMiss * 10 > refd.size()))
                throw Fail (juce::String ((int) nMiss) + " of " + juce::String ((int) refd.size()) + " samples are missing, e.g. " + *ip.missing.begin()
                            + " - keep the .sfz next to its samples folder");
            if (nMiss > 0) warn << (warn.isEmpty() ? "" : " ") << juce::String ((int) nMiss) << " missing sample" << (nMiss > 1 ? "s were" : " was") << " skipped.";
        }
        if (regs.empty())
        {
            juce::String d; for (auto& [k, v] : ip.drops) d << (d.isEmpty() ? "" : ", ") << k << " " << v;
            throw Fail ("No playable regions in " + f.path.getFileName() + (d.isNotEmpty() ? " (dropped: " + d + ")" : juce::String()));
        }
        regs = layerize (regs);
        fillVelHoles (regs);
        int holes = 0;
        regs = fillHoles (regs, holes);
        // artics that ended with no attack region → dropped (their regions renumbered)
        {
            std::vector<int> map_ ((size_t) artics.size(), -1); std::vector<Artic> keep;
            for (int a = 0; a < (int) artics.size(); ++a)
            {
                bool any = false; for (auto& r : regs) if (r.a == a && r.kind == kAttack) { any = true; break; }
                if (any) { map_[(size_t) a] = (int) keep.size(); keep.push_back (artics[(size_t) a]); }
            }
            if (keep.empty()) throw Fail ("No playable (note-on) regions in " + f.path.getFileName());
            std::vector<Reg> kept; for (auto& r : regs) if (map_[(size_t) r.a] >= 0) { r.a = map_[(size_t) r.a]; kept.push_back (r); }
            regs.swap (kept); artics.swap (keep);
        }
        // ── the size guard (decoded int16 audio, all channels, of every source the map plays)
        std::vector<Reg> order = regs;
        std::stable_sort (order.begin(), order.end(), regLess);
        std::vector<juce::String> srcOrder; std::map<juce::String, int> srcIndex;
        for (auto& r : order) if (srcIndex.find (r.src) == srcIndex.end()) { srcIndex[r.src] = (int) srcOrder.size(); srcOrder.push_back (r.src); }
        double mb = 0;
        for (auto& s : srcOrder) { const auto& si = info[s]; mb += (double) si.frames * si.channels * 2.0 / 1048576.0; }
        mbOut = mb;
        if (mb > 16384.0) throw Fail ("Too large to import: " + juce::String (mb / 1024.0, 1) + " GB of audio.");
        if (mb > kLargeMB && ! allowLarge) { needConfirm = true; return {}; }
        // ── the temp folder
        Built B;
        B.tmp = libUser.getChildFile (".importing-" + juce::Uuid().toString());
        if (! B.tmp.getChildFile ("samples").createDirectory() || ! B.tmp.getChildFile ("source").createDirectory())
            throw Fail ("Cannot write to the library folder " + libUser.getFullPathName());
        struct TmpGuard { juce::File d; bool keep = false; ~TmpGuard() { if (! keep) d.deleteRecursively(); } } guard { B.tmp };
        // ── samples (one output file per source)
        std::map<juce::String, SmpResult> results;
        std::map<juce::String, int64_t> start0s;
        for (auto& r : regs) { auto it = start0s.find (r.src); start0s[r.src] = it == start0s.end() ? r.offset : std::min (it->second, r.offset); }
        std::unique_ptr<juce::FileInputStream> sf2in;
        if (sf2 != nullptr) { sf2in = std::make_unique<juce::FileInputStream> (sf2->file); if (! sf2in->openedOk()) throw Fail ("Cannot open " + sf2->file.getFullPathName()); }
        for (size_t si = 0; si < srcOrder.size(); ++si)
        {
            checkCancel();
            const auto& s = srcOrder[si];
            prog (10.0 + 78.0 * (double) si / (double) srcOrder.size(), "Samples");
            RenderJob job;
            job.src = s; job.out = B.tmp.getChildFile ("samples").getChildFile (juce::String (si + 1).paddedLeft ('0', 4) + ".flac");
            job.start0 = start0s[s];
            std::set<int64_t> st; const Reg* firstLoop = nullptr; const Reg* firstPitched = nullptr; const Reg* firstAtt = nullptr;
            for (auto& r : regs)
                if (r.src == s)
                {
                    st.insert (r.offset);
                    if (firstLoop == nullptr && (r.loopMode == "continuous" || r.loopMode == "sustain") && r.le > 0) firstLoop = &r;
                    if (firstAtt == nullptr && r.kind == kAttack) firstAtt = &r;
                    if (firstPitched == nullptr) firstPitched = &r;
                    if (r.end > 0) job.endOpcode = std::max (job.endOpcode, r.end);
                }
            job.starts.assign (st.begin(), st.end());
            if (firstLoop != nullptr) { job.srcLoop = true; job.ls = firstLoop->ls; job.le = firstLoop->le; job.xfS = firstLoop->xfS; }
            job.fExpect = midiHz ((firstAtt != nullptr ? firstAtt : firstPitched)->root);
            Audio x;
            if (s.startsWith ("sf2:"))
            {
                const auto& src = sf2srcs->at (s);
                double rate = 44100; int sl = 0, el = 0;
                for (auto q : src.sids) { std::vector<float> c; sf2->sampleData (q, *sf2in, c, rate, sl, el); x.ch.push_back (std::move (c)); }
                size_t n = std::numeric_limits<size_t>::max(); for (auto& c : x.ch) n = std::min (n, c.size());
                for (auto& c : x.ch) c.resize (n);
                x.sr = rate;
            }
            else
            {
                std::unique_ptr<juce::AudioFormatReader> rd (formats().createReaderFor (juce::File (s)));
                if (rd == nullptr) throw Fail ("Cannot decode the sample " + s);
                readAll (*rd, x, juce::File (s).getFileName());
            }
            if (x.frames() < 2) throw Fail ("The sample " + s + " is empty");
            results[s] = renderSample (job, std::move (x));
        }
        checkCancel();
        prog (88.0, "Mapping");
        // ── assemble (torgc.assemble)
        std::vector<Rec> out;
        std::vector<Reg> sorted = regs; std::stable_sort (sorted.begin(), sorted.end(), regLess);
        std::map<std::tuple<int, int, int>, std::vector<size_t>> perNote; std::vector<std::tuple<int, int, int>> noteOrder;
        for (auto& r : sorted)
        {
            const auto& res = results[r.src];
            const int64_t start0 = start0s[r.src], start = r.offset - start0;
            const auto& ps = res.perStart.at (r.offset);
            Rec x;
            x.a = r.a; x.kind = r.kind; x.smp = srcIndex[r.src]; x.lk = r.lk; x.hk = r.hk; x.lv = r.lv; x.hv = r.hv; x.root = r.root;
            x.cents = r.cents; x.pan = (int) std::round (r.pan); x.start = start; x.end = res.frames;
            x.onset = std::max<int64_t> (start, std::min<int64_t> (res.frames - 1, ps.onset));
            if (res.hasLoop && r.kind == kAttack)
            {
                if (res.loopLs < start + 1) { x.loop = "no_loop"; }
                else { x.loop = (r.loopMode == "continuous" || r.loopMode == "sustain") ? r.loopMode : juce::String ("sustain");
                       x.ls = res.loopLs; x.le = res.loopLe; x.xf = res.loopXf; x.tailLs = x.ls; x.tailLe = x.le; }
            }
            else x.loop = r.loopMode == "one_shot" ? "one_shot" : "no_loop";
            x.rrPos = r.rrPos; x.rrLen = r.rrLen; x.randLo = r.randLo; x.randHi = r.randHi;
            x.grp = r.grp; x.offBy = r.offBy; x.offMode = r.offMode; x.env = r.env; x.rtDecay = r.rtDecay;
            x.gDb = r.gainDb + res.compDb;
            x.rms = ps.rms * std::pow (10.0, r.gainDb / 20.0);
            x.curve = r.curve; x.f0 = res.f0; x.hasF0 = res.hasF0; x.pk = ps.pkDb;
            out.push_back (x);
            if (r.kind == kAttack)
            {
                const auto k = std::make_tuple (r.a, r.root, r.lk);
                if (perNote.find (k) == perNote.end()) noteOrder.push_back (k);
                perNote[k].push_back (out.size() - 1);
            }
        }
        // gainNorm (50 % restore) + the velocity power fit
        std::map<int, std::pair<std::vector<double>, std::vector<double>>> pts;
        for (auto& k : noteOrder)
        {
            auto& recs = perNote[k];
            double top = 0; for (auto i : recs) top = std::max (top, out[i].rms);
            if (top <= 0) continue;
            std::map<std::pair<int, int>, std::vector<double>> bands;
            for (auto i : recs) bands[{ out[i].lv, out[i].hv }].push_back (out[i].rms);
            for (auto i : recs)
            {
                const double g = out[i].rms > 0 ? std::sqrt (top / out[i].rms) : 1.0;
                out[i].gainNorm = round4 (juce::jlimit (0.25, 4.0, g));
            }
            if (bands.size() > 1)
                for (auto& [b, rl] : bands)
                {
                    double m = 0; for (auto v : rl) m += v; m /= (double) rl.size();
                    if (m > 0) { pts[std::get<0> (k)].first.push_back ((b.first + b.second) / 2.0); pts[std::get<0> (k)].second.push_back (std::sqrt (m / top)); }
                }
        }
        std::map<int, double> powers;
        for (auto& [a, pv] : pts)
        {
            double p = 0;
            if (pv.first.size() >= 2)
            {
                double num = 0, den = 0;
                for (size_t i = 0; i < pv.first.size(); ++i)
                {
                    const double lv = std::log (pv.first[i] / 127.0), ls = std::log (std::max (pv.second[i], 1.0e-4));
                    num += lv * ls; den += lv * lv;
                }
                p = den > 0 ? juce::jlimit (0.0, 2.0, num / den) : 0.0;
            }
            powers[a] = p;
        }
        static const int vs[] = { 0, 1, 8, 16, 24, 32, 40, 48, 56, 64, 72, 80, 88, 96, 104, 112, 120, 127 };
        for (auto& x : out)
        {
            x.velCurve.clear();
            const double p = (x.kind == kAttack && powers.count (x.a)) ? powers[x.a] : 0.0;
            for (int v : vs)
            {
                const double c = x.curve[(size_t) v] * (x.kind == kAttack ? std::pow (std::max (v, 1) / 127.0, p) : 1.0);
                x.velCurve.push_back ({ v, roundN (c, 5) });
            }
            if (x.velCurve[1].second > 0) x.velCurve[0].second = 0.0;
            x.gainDb = roundN (x.gDb, 3);
        }
        // a silent round-robin step (never above −50 dBFS) is dropped
        {
            std::vector<Rec> keep;
            for (auto& x : out)
                if (! (x.kind != kNoise && x.pk < -50.0 && (x.rrLen > 1 || x.randLo != 0.0 || x.randHi != 1.0))) keep.push_back (x);
            if (keep.empty()) throw Fail ("Every region of " + f.path.getFileName() + " is silent.");
            out.swap (keep);
        }
        repairRR (out);
        computeTfix (out, (int) artics.size());
        std::stable_sort (out.begin(), out.end(), recLess);
        juce::StringArray samples;
        for (size_t i = 0; i < srcOrder.size(); ++i) samples.add (juce::String (i + 1).paddedLeft ('0', 4) + ".flac");
        checkCancel();
        // ── calibration (torgc.calibrate_and_preview: −24 LUFS at the centre key, v127 peak ≤ −1 dBFS)
        prog (92.0, "Calibrating");
        {
            int klo = 127, khi = 0;
            for (auto& x : out) if (x.kind == kAttack && x.a == 0) { klo = std::min (klo, x.lk); khi = std::max (khi, x.hk); }
            const int note = (klo <= 60 && 60 <= khi) ? 60 : (int) std::round ((klo + khi) / 2.0);
            Renderer rd (out, B.tmp, samples, kVeloDefault);
            std::vector<double> L, R;
            auto loudAt = [&] (int vel) {
                rd.render (note, vel, 2.0, 1.6, false, L, R);
                std::vector<double> m (L.size()); for (size_t i = 0; i < L.size(); ++i) m[i] = 0.5 * (L[i] + R[i]);
                const int64_t ons = findOnset (m, 0, (int64_t) m.size());
                return loudnessK (L, R, (size_t) ons, (size_t) ons + (size_t) rd.outSr, rd.outSr);
            };
            const double l100 = loudAt (100);
            double off = l100 > -150.0 ? kCalibLufs - l100 : 0.0;
            rd.render (note, 127, 2.0, 1.6, false, L, R);
            double pk = 0; for (size_t i = 0; i < L.size(); ++i) pk = std::max ({ pk, std::abs (L[i]), std::abs (R[i]) });
            const double pk127 = dbOf (pk);
            if (pk127 + off > kPeakCeilDb) off -= pk127 + off - kPeakCeilDb;
            for (auto& x : out) x.gainDb = roundN (x.gainDb + off, 3);
            // preview.flac: the centre key, velocity 90, 3 s, note-off at 2.2 s
            Renderer pv (out, B.tmp, samples, 1.0);
            pv.render (note, 90, 3.0, 2.2, true, L, R);
            double pp = 0; for (size_t i = 0; i < L.size(); ++i) pp = std::max ({ pp, std::abs (L[i]), std::abs (R[i]) });
            const double g = pp > 0.89 ? 0.89 / pp : 1.0;
            std::vector<std::vector<float>> y (2, std::vector<float> (L.size()));
            for (size_t i = 0; i < L.size(); ++i) { y[0][i] = (float) (L[i] * g); y[1][i] = (float) (R[i] * g); }
            writeFlac16 (B.tmp.getChildFile ("preview.flac"), y, pv.outSr);
        }
        // ── map.json (field order = the compiler's; "artics" before "regions" — the processor reads the header only)
        bool hasNoise = false, hasRel = false;
        juce::Array<juce::var> regsV;
        for (auto& x : out)
        {
            hasNoise = hasNoise || x.kind == kNoise; hasRel = hasRel || x.kind == kRelease;
            auto* o = new juce::DynamicObject();
            o->setProperty ("a", x.a); o->setProperty ("kind", kindName (x.kind)); o->setProperty ("smp", x.smp);
            o->setProperty ("lk", x.lk); o->setProperty ("hk", x.hk); o->setProperty ("lv", x.lv); o->setProperty ("hv", x.hv);
            o->setProperty ("xfLo", x.lv); o->setProperty ("xfHi", x.hv); o->setProperty ("root", x.root);
            o->setProperty ("cents", x.cents); o->setProperty ("gainDb", x.gainDb); o->setProperty ("gainNorm", x.gainNorm);
            o->setProperty ("pan", x.pan); o->setProperty ("start", (juce::int64) x.start); o->setProperty ("end", (juce::int64) x.end);
            o->setProperty ("onset", (juce::int64) x.onset); o->setProperty ("loop", x.loop);
            o->setProperty ("ls", (juce::int64) x.ls); o->setProperty ("le", (juce::int64) x.le); o->setProperty ("xf", (juce::int64) x.xf);
            o->setProperty ("tailLs", (juce::int64) x.tailLs); o->setProperty ("tailLe", (juce::int64) x.tailLe);
            o->setProperty ("rr", juce::Array<juce::var> { x.rrPos, x.rrLen });
            o->setProperty ("rand", juce::Array<juce::var> { x.randLo, x.randHi });
            o->setProperty ("grp", x.grp); o->setProperty ("offBy", x.offBy); o->setProperty ("offMode", x.offMode);
            auto* e = new juce::DynamicObject();
            e->setProperty ("a", x.env.a); e->setProperty ("h", x.env.h); e->setProperty ("d", x.env.d); e->setProperty ("s", x.env.s); e->setProperty ("r", x.env.r);
            o->setProperty ("env", juce::var (e));
            o->setProperty ("rtDecay", x.rtDecay);
            juce::Array<juce::var> vc; for (auto& p : x.velCurve) vc.add (juce::Array<juce::var> { p.first, p.second });
            o->setProperty ("velCurve", vc);
            o->setProperty ("tfix", x.tfix);
            regsV.add (juce::var (o));
        }
        auto* m = new juce::DynamicObject();
        m->setProperty ("torg", 1); m->setProperty ("id", ""); m->setProperty ("name", name); m->setProperty ("family", family);
        m->setProperty ("category", "User"); m->setProperty ("credit", credit); m->setProperty ("polyMax", 32);
        m->setProperty ("hasNoise", hasNoise); m->setProperty ("hasRelease", hasRel);
        juce::Array<juce::var> an; for (auto& a : artics) an.add (a.name);
        m->setProperty ("artics", an);
        juce::Array<juce::var> sv; for (auto& s : samples) sv.add (s);
        m->setProperty ("samples", sv);
        m->setProperty ("markers", juce::var (new juce::DynamicObject()));
        m->setProperty ("regions", regsV);
        B.map = juce::var (m);
        int64_t ram = 0;
        for (auto& [s, res] : results) ram += res.frames * res.ch * 2;
        B.sizeMB = roundN ((double) ram / 1048576.0, 1);
        B.name = name; B.family = family; B.credit = credit;
        // ── source/: the mapping file(s) (a big SF2 is referenced, not copied — torgc's 20 MB rule)
        {
            auto src = B.tmp.getChildFile ("source");
            juce::StringArray seen;
            for (auto& inc : f.includes)
            {
                if (seen.contains (inc.getFullPathName())) continue;
                seen.add (inc.getFullPathName());
                auto rel = inc.getRelativePathFrom (f.path.getParentDirectory());
                if (rel.startsWith ("..") || juce::File::isAbsolutePath (rel)) rel = inc.getFileName();
                auto dst = src.getChildFile (rel.replaceCharacter ('\\', '/'));
                dst.getParentDirectory().createDirectory();
                if (inc.getSize() > 20 * 1048576)
                    dst.withFileExtension (dst.getFileExtension() + ".txt").replaceWithText (inc.getFullPathName() + "\n" + juce::String (inc.getSize()) + " bytes\n");
                else if (! inc.copyFileTo (dst)) throw Fail ("Cannot copy " + inc.getFileName() + " into the library");
            }
            src.getChildFile ("IMPORT.txt").replaceWithText ("Imported by Terrain (Organics - User import)\nSource: " + f.path.getFullPathName()
                + "\nDate: " + juce::Time::getCurrentTime().toISO8601 (true) + "\nRegions: " + juce::String ((int) out.size())
                + "  Samples: " + juce::String (samples.size()) + (warn.isNotEmpty() ? "\nNote: " + warn : juce::String()) + "\n");
        }
        guard.keep = true;
        return B;
    }

    /** Under the user lock: pick the id + number, write map.json, rename the folder into place, append the indexes. */
    void finalise (const juce::File& root, std::vector<Built>& built, juce::StringArray& ids, juce::StringArray& names)
    {
        ScopedUserLock lock;
        if (! lock.ok) throw Fail ("The library is busy (another import holds it) - try again.");
        const auto user = root.getChildFile ("User");
        const auto idxF = user.getChildFile ("user-index.json"), idsF = user.getChildFile ("user-ids.json");
        juce::var idx = readJson (idxF), nums = readJson (idsF);
        if (! idx.isArray()) idx = juce::var (juce::Array<juce::var>());
        auto* numObj = nums.getDynamicObject();
        if (numObj == nullptr) { nums = juce::var (new juce::DynamicObject()); numObj = nums.getDynamicObject(); }
        const juce::var factIds = readJson (root.getChildFile ("ids.json"));
        std::set<int> usedNums;
        if (auto* fo = factIds.getDynamicObject()) for (auto& p : fo->getProperties()) usedNums.insert ((int) p.value);
        int next = kFirstUserNum - 1;
        for (auto& p : numObj->getProperties()) { next = std::max (next, (int) p.value); usedNums.insert ((int) p.value); }
        std::vector<juce::File> placed;
        const auto idxBefore = idx.clone(); const auto numsBefore = nums.clone();
        try
        {
            for (auto& b : built)
            {
                const auto slug = "user." + slugOf (b.name);
                juce::String id = slug;
                for (int k = 2;; ++k)
                {
                    bool clash = user.getChildFile (id).exists() || factIds.hasProperty (id);
                    if (auto* arr = idx.getArray()) for (auto& e : *arr) if (e.getProperty ("id", {}).toString() == id) clash = true;
                    if (! clash) break;
                    id = slug + "-" + juce::String (k);
                }
                if (! numObj->hasProperty (id))
                {
                    do ++next; while (usedNums.count (next));
                    if (next > 4095) throw Fail ("The user library is full (4095 instruments).");
                    numObj->setProperty (id, next); usedNums.insert (next);
                }
                b.map.getDynamicObject()->setProperty ("id", id);
                if (! b.tmp.getChildFile ("map.json").replaceWithText (juce::JSON::toString (b.map, false))) throw Fail ("Cannot write map.json");
                const auto dst = user.getChildFile (id);
                if (! b.tmp.moveFileTo (dst)) throw Fail ("Cannot move the instrument into " + dst.getFullPathName());
                placed.push_back (dst);
                auto* e = new juce::DynamicObject();
                e->setProperty ("id", id); e->setProperty ("name", b.name); e->setProperty ("family", b.family); e->setProperty ("category", "User");
                e->setProperty ("tags", juce::Array<juce::var> { "user" }); e->setProperty ("sizeMB", b.sizeMB);
                e->setProperty ("licence", "User import"); e->setProperty ("credit", b.credit);
                idx.getArray()->add (juce::var (e));
                ids.add (id); names.add (b.name);
            }
            if (! writeJsonAtomic (idsF, nums) || ! writeJsonAtomic (idxF, idx)) throw Fail ("Cannot write the user index (" + user.getFullPathName() + ")");
        }
        catch (...)
        {
            for (auto& d : placed) d.deleteRecursively();
            writeJsonAtomic (idxF, idxBefore);
            if (numsBefore.isObject()) writeJsonAtomic (idsF, numsBefore);
            ids.clear(); names.clear();
            throw;
        }
    }

    Result runImport (const Request& req, ProgressFn progress, const std::atomic<bool>* cancel)
    {
        Result res;
        std::vector<Built> built;
        auto prog = [&] (double p, const juce::String& st) { if (progress) progress ((float) p, st); };
        try
        {
            const auto src = req.source;
            if (! src.existsAsFile()) throw Fail ("File not found: " + src.getFullPathName());
            if (! isSoundFontFile (src)) throw Fail (src.getFileName() + " is not an .sfz, .sf2 or .sf3 file");
            if (src.getSize() > (int64_t) 8 * 1024 * 1024 * 1024) throw Fail (src.getFileName() + " is too large to import");
            const auto root = req.root != juce::File() ? req.root : OrganicsLibrary::get().root();
            const auto user = root.getChildFile ("User");
            if (! user.createDirectory()) throw Fail ("Cannot create the user library folder " + user.getFullPathName());
            prog (1.0, "Reading");
            const auto ext = src.getFileExtension().toLowerCase();
            juce::String warn;
            double mbTotal = 0; bool needConfirm = false;
            auto oneProg = [&] (int i, int n) { return [&, i, n] (double p, const juce::String& st) { prog ((i * 100.0 + p) / n, st); }; };
            if (ext == ".sfz")
            {
                SfzFile f = SfzParser (src).parse();
                if (f.regions.empty()) throw Fail ("No <region> in " + src.getFileName() + " - is it an SFZ file?");
                if (f.missingIncludes > 0) warn << f.missingIncludes << " #include file" << (f.missingIncludes > 1 ? "s were" : " was") << " not found.";
                prog (8.0, "Reading");
                const auto nm = req.name.isNotEmpty() ? req.name : src.getFileNameWithoutExtension().replaceCharacters ("_", " ").trim();
                double mb = 0;
                auto b = buildOne (user, f, nullptr, nullptr, nm, guessFamily (nm, -1), src.getFileName(), req.allowLarge, oneProg (0, 1), cancel, warn, mb, needConfirm);
                mbTotal = mb;
                if (! needConfirm) built.push_back (std::move (b));
            }
            else
            {
                Sf2 s (src);
                const auto names = s.presetNames();
                if (names.isEmpty()) throw Fail (src.getFileName() + " has no presets");
                std::vector<int> which;
                if (req.preset == kAllPresets) for (int i = 0; i < names.size(); ++i) which.push_back (i);
                else if (req.preset >= 0) { if (req.preset >= names.size()) throw Fail ("Preset " + juce::String (req.preset + 1) + " is not in " + src.getFileName()); which.push_back (req.preset); }
                else which.push_back (0);
                // the size guard over everything this import will hold (every chosen preset keeps its own samples),
                // from the sample headers, BEFORE anything is decoded
                if (! req.allowLarge)
                {
                    for (auto pi : which)
                    {
                        std::map<juce::String, Sf2Source> s2;
                        try { sf2Regions (s, pi, s2); } catch (...) {}
                        for (auto& [k, v] : s2)
                        {
                            int64_t fr = std::numeric_limits<int64_t>::max(); for (auto q : v.sids) fr = std::min (fr, s.sampleFrames (q));
                            mbTotal += (double) fr * (double) v.sids.size() * 2.0 / 1048576.0;
                        }
                    }
                    if (mbTotal > kLargeMB) needConfirm = true;
                    mbTotal = needConfirm ? mbTotal : 0.0;
                }
                int done = 0; juce::StringArray usedNames; juce::String skipped;
                for (size_t w = 0; w < which.size() && ! needConfirm; ++w)
                {
                    const int pi = which[w];
                    std::map<juce::String, Sf2Source> srcs;
                    juce::String nm = req.name.isNotEmpty() && which.size() == 1 ? req.name
                                    : (names.size() == 1 ? src.getFileNameWithoutExtension().replaceCharacters ("_", " ").trim() : names[pi]);
                    { juce::String u = nm; int k = 2; while (usedNames.contains (u)) u = nm + " " + juce::String (k++); nm = u; usedNames.add (nm); }
                    const int gm = s.phdr[(size_t) pi].bank == 0 ? s.phdr[(size_t) pi].preset : -1;
                    try
                    {
                        SfzFile f = sf2Regions (s, pi, srcs);
                        double mb = 0; bool nc = false;
                        juce::String w1;
                        auto b = buildOne (user, f, &srcs, &s, nm, guessFamily (nm, gm), src.getFileName() + (names.size() > 1 ? " - " + names[pi] : juce::String()),
                                           true, oneProg ((int) w, (int) which.size()), cancel, w1, mb, nc);
                        mbTotal += mb;
                        if (w1.isNotEmpty()) warn << (warn.isEmpty() ? "" : " ") << (which.size() > 1 ? names[pi] + ": " : juce::String()) << w1;
                        built.push_back (std::move (b)); ++done;
                    }
                    catch (const Fail& e)
                    {
                        const auto msg = why (e);
                        if (msg == "Cancelled" || which.size() == 1) throw;
                        skipped << (skipped.isEmpty() ? "" : ", ") << names[pi];                  // "Import all": skip an empty/bad preset
                    }
                }
                if (! needConfirm && done == 0) throw Fail ("None of the presets of " + src.getFileName() + " could be imported");
                if (skipped.isNotEmpty()) warn << (warn.isEmpty() ? "" : " ") << "Skipped (no playable regions): " << skipped << ".";
            }
            res.mb = roundN (mbTotal, 1);
            if (needConfirm)
            {
                res.needConfirm = true;
                res.error = src.getFileName() + " holds " + juce::String (mbTotal, 0) + " MB of audio (over " + juce::String ((int) kLargeMB) + " MB). Import anyway?";
                return res;
            }
            if (cancel != nullptr && cancel->load()) throw Fail ("Cancelled");
            prog (97.0, "Writing");
            finalise (root, built, res.ids, res.names);
            built.clear();
            res.ok = true; res.warning = warn;
            prog (100.0, "Done");
        }
        catch (const std::exception& e) { res.ok = false; res.error = why (e); }
        catch (...) { res.ok = false; res.error = "Unexpected error while importing"; }
        for (auto& b : built) if (b.tmp != juce::File()) b.tmp.deleteRecursively();
        return res;
    }
} // namespace

//==================================================================================================================
//  public API
//==================================================================================================================
bool isSoundFontFile (const juce::File& f)
{
    const auto e = f.getFileExtension().toLowerCase();
    return e == ".sfz" || e == ".sf2" || e == ".sf3";
}

Result importSoundFont (const Request& req, ProgressFn progress, const std::atomic<bool>* cancel)
{
    return runImport (req, std::move (progress), cancel);
}

juce::StringArray listSf2Presets (const juce::File& f, juce::String* error)
{
    try
    {
        if (! f.existsAsFile()) throw Fail ("File not found: " + f.getFullPathName());
        Sf2 s (f);
       #if ! JUCE_USE_OGGVORBIS
        if (s.anyCompressed) throw Fail ("SF3 not supported (this build has no Ogg Vorbis decoder)");
       #endif
        return s.presetNames();
    }
    catch (const std::exception& e) { if (error) *error = why (e); }
    return {};
}

bool deleteUserInstrument (const juce::File& root, const juce::String& id, juce::String* error)
{
    auto fail = [error] (const juce::String& w) { if (error) *error = w; return false; };
    if (! id.startsWith ("user.") || id.length() > 200 || id.containsAnyOf ("/\\:") || id.contains ("..")) return fail ("Not a user instrument: " + id);
    ScopedUserLock lock;
    if (! lock.ok) return fail ("The library is busy - try again.");
    const auto user = root.getChildFile ("User");
    const auto idxF = user.getChildFile ("user-index.json");
    juce::var idx = readJson (idxF);
    bool found = false;
    if (auto* arr = idx.getArray())
        for (int i = arr->size(); --i >= 0;)
            if ((*arr)[i].getProperty ("id", {}).toString() == id) { arr->remove (i); found = true; }
    const auto dir = user.getChildFile (id);
    if (! found && ! dir.exists()) return fail ("No user instrument " + id);
    if (found && ! writeJsonAtomic (idxF, idx)) return fail ("Cannot write " + idxF.getFullPathName());
    if (dir.exists())
    {
        const auto trash = user.getChildFile (".deleting-" + juce::Uuid().toString());
        if (! dir.moveFileTo (trash)) return fail ("Cannot remove " + dir.getFullPathName());
        trash.deleteRecursively();
    }
    return true;
}

//==================================================================================================================
//  12 · background jobs
//==================================================================================================================
namespace
{
    struct Jobs : private juce::Thread
    {
        struct Job { int id; Request req; std::function<void (const JobEvent&)> cb; };
        juce::CriticalSection lock;
        std::vector<Job> queue;
        std::atomic<bool> cancel { false };
        std::atomic<int> running { 0 };
        int nextId = 0;

        Jobs() : juce::Thread ("Organics import") {}
        ~Jobs() override { cancel = true; stopThread (10000); }

        int add (const Request& r, std::function<void (const JobEvent&)> cb)
        {
            const juce::ScopedLock sl (lock);
            const int id = ++nextId;
            queue.push_back ({ id, r, std::move (cb) });
            cancel = false;
            if (! isThreadRunning()) startThread (juce::Thread::Priority::low);
            notify();
            return id;
        }
        bool busy() { const juce::ScopedLock sl (lock); return ! queue.empty() || running.load() > 0; }

        static void post (std::function<void (const JobEvent&)> cb, JobEvent ev)
        {
            if (! cb) return;
            if (auto* mm = juce::MessageManager::getInstanceWithoutCreating(); mm != nullptr)
                juce::MessageManager::callAsync ([cb, ev] { cb (ev); });
        }

        void run() override
        {
            while (! threadShouldExit())
            {
                Job job; bool have = false;
                {
                    const juce::ScopedLock sl (lock);
                    if (! queue.empty()) { job = queue.front(); queue.erase (queue.begin()); have = true; running = 1; }
                }
                if (! have) { wait (500); continue; }
                double lastT = 0;
                auto res = importSoundFont (job.req, [&] (float pct, const juce::String& stage)
                {
                    const double t = juce::Time::getMillisecondCounterHiRes();
                    if (t - lastT < 100.0 && pct < 100.0f) return;
                    lastT = t;
                    JobEvent ev; ev.job = job.id; ev.pct = pct; ev.stage = stage;
                    post (job.cb, ev);
                }, &cancel);
                JobEvent ev; ev.job = job.id; ev.pct = 100.0f; ev.done = true; ev.result = res; ev.stage = res.ok ? "Done" : "Failed";
                const auto cb = job.cb;
                const bool wrote = res.ok;
                if (auto* mm = juce::MessageManager::getInstanceWithoutCreating(); mm != nullptr)
                    juce::MessageManager::callAsync ([cb, ev, wrote]
                    {
                        if (wrote) OrganicsLibrary::get().rescan();       // the new instrument is in index() before the page hears
                        if (cb) cb (ev);
                    });
                running = 0;
            }
        }
    };
    Jobs& jobs() { static Jobs j; return j; }
}

int  startImport (const Request& req, std::function<void (const JobEvent&)> onEvent) { return jobs().add (req, std::move (onEvent)); }
void cancelAll() { jobs().cancel = true; }
bool busy() { return jobs().busy(); }

} // namespace orgimport
} // namespace tw
