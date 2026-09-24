#pragma once
// ══ tp103 — REPORT A BUG / REPORT A CRASH: THE mailto: BUILDER ══════════════════════════════════════════════════
//  Settings → Updates & About → Support opens the user's own mail app (whatever handles mailto: — Apple Mail,
//  Outlook, Windows Mail, a browser's Gmail handler) with a message to contact@wavescrate.com already written:
//  a subject naming the version, the build and the minute, a short template for what happened, and a system
//  block Terrain fills in itself.
//
//  PURE C++ (no JUCE) so Tests/support_mail_test.cpp can prove the encoding and the length cap offline.
//
//  THE THREE THINGS THAT GO WRONG WITH mailto:, each handled here:
//   1. ENCODING. RFC 6068: every octet outside RFC 3986's unreserved set is %-escaped, UTF-8 first. A SPACE
//      IS %20, NEVER '+' — mail clients take '+' literally (juce::URL's own parameter path is avoided for this
//      reason: it re-parses and re-escapes the query, and the '+' question is not one to leave to a re-parse).
//      Line breaks are CRLF (%0D%0A); a bare LF shows as one long line in Outlook.
//   2. LENGTH. Windows hands a mailto: to ShellExecute, and past ~2 kB the mail app opens with the body cut
//      mid-escape or not at all. The whole URL is capped (default 1800). The template and the most useful
//      system lines come first; lines are dropped WHOLE from the end, never cut inside an escape, and a note
//      says the full report is on the clipboard (the caller puts it there whenever `truncated` is set).
//   3. NO MAIL APP. Opening the URL can fail; that is the caller's to detect (Process::openDocument's bool)
//      and answer with the clipboard fallback.
#include <string>
#include <vector>
#include <cstddef>

namespace tw { namespace support {

inline std::string pctEncode (const std::string& utf8)
{
    static const char* hex = "0123456789ABCDEF";
    std::string out; out.reserve (utf8.size() * 3);
    for (const char ch : utf8)
    {
        const unsigned c = (unsigned) (unsigned char) ch;
        const bool unreserved = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')
                             || c == '-' || c == '_' || c == '.' || c == '~';
        if (unreserved) out += (char) c;
        else { out += '%'; out += hex[c >> 4]; out += hex[c & 15]; }
    }
    return out;
}

/** Splits on '\n' (a '\r' before it is dropped), keeping empty lines. */
inline std::vector<std::string> splitLines (const std::string& s)
{
    std::vector<std::string> v; std::string cur;
    for (char c : s)
    {
        if (c == '\n') { if (! cur.empty() && cur.back() == '\r') cur.pop_back(); v.push_back (cur); cur.clear(); }
        else cur += c;
    }
    v.push_back (cur);
    return v;
}

struct Mailto
{
    std::string url;          // mailto:… ready for the OS
    bool truncated = false;   // some body lines did not fit — the caller should copy the full report
};

/** Builds mailto:<to>?subject=…&body=… no longer than `cap` characters. */
inline Mailto buildMailto (const std::string& to, const std::string& subject, const std::string& body,
                           std::size_t cap = 1800)
{
    Mailto m;
    const std::string head = "mailto:" + to + "?subject=" + pctEncode (subject) + "&body=";
    const std::string crlf = "%0D%0A";
    const std::string note = pctEncode ("[Shortened to fit - the full report is on your clipboard. Paste it here.]");
    const auto lines = splitLines (body);
    std::string enc;
    for (std::size_t i = 0; i < lines.size(); ++i)
    {
        const std::string piece = (i ? crlf : std::string()) + pctEncode (lines[i]);
        // keep room for the note (and its line break) in case a LATER line is the one that does not fit
        const std::size_t reserve = (i + 1 < lines.size()) ? crlf.size() + note.size() : 0;
        if (head.size() + enc.size() + piece.size() + reserve > cap)
        {
            m.truncated = true;
            if (head.size() + enc.size() + crlf.size() + note.size() <= cap) enc += crlf + note;
            break;
        }
        enc += piece;
    }
    m.url = head + enc;
    if (m.url.size() > cap) { m.url = head; m.truncated = true; }   // a subject so long nothing fits (never in practice)
    return m;
}

/** "Terrain 1.0.0-beta (build 3e5e124) — Bug report — 2026-09-24 14:32" */
inline std::string reportSubject (const std::string& version, const std::string& build, bool crash, const std::string& when)
{
    return "Terrain " + version + " (build " + build + ") \xe2\x80\x94 " + (crash ? "Crash report" : "Bug report") + " \xe2\x80\x94 " + when;
}

/** The message: a short template the user fills in, then (for a crash) the log line, then the system block Terrain
    wrote. The template and the first system lines come first so a length cap only ever drops the least useful. */
inline std::string reportBody (bool crash, const std::string& crashLine, const std::string& systemBlock)
{
    std::string b;
    b += "Hi Waves Crate,\n\n";
    b += crash ? "What I was doing when Terrain crashed:\n\n\n" : "What happened:\n\n\n";
    b += "Steps to make it happen again:\n1. \n2. \n3. \n\n";
    b += "What I expected:\n\n\n";
    if (crash && ! crashLine.empty()) b += crashLine + "\n\n";
    b += "--- System (filled in by Terrain) ---\n";
    b += systemBlock;
    return b;
}

}} // namespace tw::support
