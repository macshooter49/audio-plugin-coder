// ══════════════════════════════════════════════════════════════════════════════════════════════
//  support_mail_test.cpp — tp103 · Settings → Report a bug / Report a crash: THE mailto: BUILDER.
//
//  Source/TerrainSupportMail.h is what reportIssue (TerrainSettingsNatives.cpp) ships; this compiles it alone.
//    [1] the encoding: RFC 3986 unreserved kept, everything else %XX of its UTF-8 — a space is %20 and NEVER '+',
//        '+' itself is %2B, '&' '=' '?' '#' escaped, é / — / emoji as their UTF-8 octets, lines joined by %0D%0A
//    [2] a round trip: decoding the subject and body gives back exactly what went in (CRLF for each '\n')
//    [3] the cap: a body far past 1800 characters yields a URL <= 1800, flagged truncated, cut only at a line
//        boundary (every '%' is followed by two hex digits), ending with the "on your clipboard" note
//    [4] the real report fits: the shipped subject + template + an 8-line system block stays under the cap untruncated
//    [5] the subject reads "Terrain <ver> (build <sha>) — Bug report — <date>" and the crash variant says Crash report
//  Prints a sample URL (the one the report quotes).
//
//    clang++ -std=c++17 -O2 -I Source Tests/support_mail_test.cpp -o /tmp/support_mail_test && /tmp/support_mail_test
// ══════════════════════════════════════════════════════════════════════════════════════════════
#include "TerrainSupportMail.h"
#include <cstdio>
#include <string>
#include <cctype>

static int npass = 0, nfail = 0;
static void chk (bool ok, const char* what, const std::string& d = "")
{ std::printf ("  %s  %s\n", ok ? "PASS " : "FAIL ", what); if (! ok && ! d.empty()) std::printf ("        %s\n", d.c_str()); ok ? ++npass : ++nfail; }

static std::string pctDecode (const std::string& s)
{
    std::string o;
    for (size_t i = 0; i < s.size(); ++i)
    {
        if (s[i] == '%' && i + 2 < s.size() + 0 && std::isxdigit ((unsigned char) s[i + 1]) && std::isxdigit ((unsigned char) s[i + 2]))
        { o += (char) std::stoi (s.substr (i + 1, 2), nullptr, 16); i += 2; }
        else o += s[i];
    }
    return o;
}
static bool escapesWhole (const std::string& s)
{
    for (size_t i = 0; i < s.size(); ++i)
        if (s[i] == '%' && (i + 2 >= s.size() || ! std::isxdigit ((unsigned char) s[i + 1]) || ! std::isxdigit ((unsigned char) s[i + 2]))) return false;
    return true;
}
static std::string part (const std::string& url, const std::string& key)
{
    const auto k = (url.find ("?" + key + "=") != std::string::npos) ? url.find ("?" + key + "=") : url.find ("&" + key + "=");
    if (k == std::string::npos) return "";
    const auto st = k + key.size() + 2, en = url.find ('&', st);
    return url.substr (st, en == std::string::npos ? std::string::npos : en - st);
}
static std::string crlf (const std::string& s) { std::string o; for (char c : s) { if (c == '\n') o += "\r\n"; else o += c; } return o; }

int main()
{
    using namespace tw::support;
    std::printf ("support_mail_test — the Report a bug / crash mailto builder\n");

    // [1] encoding
    chk (pctEncode ("a b") == "a%20b", "[1] a space is %20 (mail clients read '+' literally)");
    chk (pctEncode ("C++") == "C%2B%2B", "[1] '+' is escaped");
    chk (pctEncode ("a&b=c?d#e/f") == "a%26b%3Dc%3Fd%23e%2Ff", "[1] & = ? # / are escaped");
    chk (pctEncode ("Az09-_.~") == "Az09-_.~", "[1] the unreserved set passes untouched");
    chk (pctEncode ("\xc3\xa9") == "%C3%A9", "[1] e-acute is its two UTF-8 octets");
    chk (pctEncode ("\xe2\x80\x94") == "%E2%80%94", "[1] the em dash is its three UTF-8 octets");
    chk (pctEncode ("\xf0\x9f\x8e\xb9") == "%F0%9F%8E%B9", "[1] a 4-byte emoji survives");

    // [2] round trip
    {
        const std::string subj = "Terrain 1.0.0-beta (build abc1234) \xe2\x80\x94 Bug report \xe2\x80\x94 2026-09-24 14:32";
        const std::string body = "Line one & two\nC++ = 100% sure?\n\nCaf\xc3\xa9 #3";
        const auto m = buildMailto ("contact@wavescrate.com", subj, body);
        chk (m.url.rfind ("mailto:contact@wavescrate.com?subject=", 0) == 0, "[2] mailto:contact@wavescrate.com?subject=… leads");
        chk (pctDecode (part (m.url, "subject")) == subj, "[2] the subject decodes back exactly");
        chk (pctDecode (part (m.url, "body")) == crlf (body), "[2] the body decodes back exactly, each line break a CRLF");
        chk (! m.truncated, "[2] a short report is not truncated");
        chk (m.url.find (' ') == std::string::npos && m.url.find ('+') == std::string::npos && m.url.find ('\n') == std::string::npos,
             "[2] no raw space, '+' or newline anywhere in the URL");
    }

    // [3] the cap
    {
        std::string big; for (int i = 0; i < 200; ++i) big += "System line " + std::to_string (i) + ": caf\xc3\xa9 & more & more\n";
        const auto m = buildMailto ("contact@wavescrate.com", "Terrain bug", big, 1800);
        chk (m.url.size() <= 1800, "[3] a 5 kB body gives a URL <= 1800 chars", std::to_string (m.url.size()));
        chk (m.truncated, "[3] … and says it was truncated");
        chk (escapesWhole (m.url), "[3] … cut only at a line: every % has its two hex digits");
        const auto dec = pctDecode (part (m.url, "body"));
        chk (dec.find ("the full report is on your clipboard") != std::string::npos, "[3] … and ends with the clipboard note");
        const auto m2 = buildMailto ("contact@wavescrate.com", "x", big, 600);
        chk (m2.url.size() <= 600 && m2.truncated && escapesWhole (m2.url), "[3] the cap holds at 600 too", std::to_string (m2.url.size()));
    }

    // [4]/[5] the report as shipped (reportSubject + reportBody + a realistic system block from this Mac)
    const std::string sys =
        "Terrain 1.0.0-beta (build 3e5e124, Sep 24 2026)\n"
        "Format: AU - Host: Ableton Live 12.1.5\n"
        "OS: Mac OSX 26.0\n"
        "CPU: Apple M2 Pro, 12 cores - RAM: 32 GB\n"
        "Audio: 48.0 kHz - buffer 512 samples\n"
        "Preset: Glass Choir\n"
        "DSP load: 7 %\n"
        "Licence: Registered to Max";
    const auto subjB = reportSubject ("1.0.0-beta", "3e5e124", false, "2026-09-24 14:32");
    const auto subjC = reportSubject ("1.0.0-beta", "3e5e124", true,  "2026-09-24 14:32");
    chk (subjB == "Terrain 1.0.0-beta (build 3e5e124) \xe2\x80\x94 Bug report \xe2\x80\x94 2026-09-24 14:32", "[5] the bug subject reads as specified");
    chk (subjC.find ("Crash report") != std::string::npos, "[5] the crash subject says Crash report");
    const auto bug = buildMailto ("contact@wavescrate.com", subjB, reportBody (false, "", sys));
    const auto crash = buildMailto ("contact@wavescrate.com", subjC,
        reportBody (true, "Crash log: Live-2026-09-24-143012.ips - please attach the file that just opened in Finder.", sys));
    chk (bug.url.size() <= 1800 && ! bug.truncated, "[4] the full bug report fits untruncated", std::to_string (bug.url.size()) + " chars");
    chk (crash.url.size() <= 1800 && ! crash.truncated, "[4] the full crash report fits untruncated", std::to_string (crash.url.size()) + " chars");
    chk (pctDecode (part (crash.url, "body")).find ("please attach the file that just opened") != std::string::npos, "[4] the crash body asks for the log");

    std::printf ("\n  sample bug URL (%zu chars):\n  %s\n", bug.url.size(), bug.url.c_str());
    std::printf ("\n  crash URL: %zu chars\n", crash.url.size());
    std::printf ("\nsupport_mail_test: %d pass, %d fail — %s\n", npass, nfail, nfail ? "FAIL" : "PASS");
    return nfail ? 1 : 0;
}
