// ══ fb631 — THE SPIKE ON PRESET CHANGE, measured where Ableton measures it: per render block.
//    Max: "every time I change presets there's a big spike in CPU for a millisecond — Ableton's meter
//    jumps to 90 and drops. Serum presets don't do that. That CPU light is the engine light of a car."
//    Ableton's meter is audio-thread time per buffer, so this cert times EVERY AudioUnitRender call
//    before and after a real preset load (Max's own .terrain files) with a note held, and reports the
//    worst block after the load against the steady-state median before it.
//    ⚠️ This harness is single-threaded: the load runs on the calling thread, then the renders. It sees
//    work that lands IN the render path after a load; it cannot see a message-thread lock the audio
//    thread would have to wait for in a DAW. Both are reported on separately below.
#include "au_state_blob.h"
#include <chrono>
#include <fstream>
#include <algorithm>
#include <numeric>

static std::string slurpBin (const std::string& p) { std::ifstream f (p, std::ios::binary); std::stringstream ss; ss << f.rdbuf(); return ss.str(); }
static uint32_t u32 (const std::string& s, size_t at) { return (uint32_t) (uint8_t) s[at] | ((uint32_t) (uint8_t) s[at+1] << 8) | ((uint32_t) (uint8_t) s[at+2] << 16) | ((uint32_t) (uint8_t) s[at+3] << 24); }
// .terrain = "TRN1" · u32 manifestLen · manifest · u32 chunkLen · chunk;  chunk = "VC2!" · u32 · XML
static std::string xmlOfTerrain (const std::string& path, std::string& name)
{
    const std::string f = slurpBin (path);
    if (f.size() < 12 || f.compare (0, 4, "TRN1") != 0) return {};
    const uint32_t ml = u32 (f, 4); if (8 + ml + 4 > f.size()) return {};
    const std::string man = f.substr (8, ml);
    const size_t np = man.find ("\"name\""); if (np != std::string::npos) { const size_t a = man.find ('"', man.find (':', np) + 1); const size_t b = man.find ('"', a + 1); if (a != std::string::npos && b != std::string::npos) name = man.substr (a + 1, b - a - 1); }
    const uint32_t cl = u32 (f, 8 + ml); const size_t c0 = 12 + ml; if (c0 + cl > f.size() || cl < 8) return {};
    const std::string chunk = f.substr (c0, cl);
    if (chunk.compare (0, 4, "VC2!") != 0) return {};
    std::string xml = chunk.substr (8); while (! xml.empty() && xml.back() == '\0') xml.pop_back();
    return xml;
}
struct Timing { std::vector<double> us; };
static Timing timedRender (AU& a, int nblk)
{
    Timing t; std::vector<float> bl ((size_t) BLK), br ((size_t) BLK);
    AudioBufferList* abl = (AudioBufferList*) calloc (1, sizeof (AudioBufferList) + sizeof (AudioBuffer)); abl->mNumberBuffers = 2;
    AudioTimeStamp ts {}; ts.mFlags = kAudioTimeStampSampleTimeValid; ts.mSampleTime = a.clock_;
    for (int b = 0; b < nblk; ++b)
    {
        abl->mBuffers[0] = { 1, (UInt32) (BLK * 4), bl.data() }; abl->mBuffers[1] = { 1, (UInt32) (BLK * 4), br.data() };
        AudioUnitRenderActionFlags fl = 0;
        const auto t0 = std::chrono::steady_clock::now();
        if (AudioUnitRender (a.au, &fl, &ts, 0, BLK, abl) != noErr) break;
        t.us.push_back (std::chrono::duration<double, std::micro> (std::chrono::steady_clock::now() - t0).count());
        ts.mSampleTime += BLK; a.clock_ += BLK;
    }
    free (abl); return t;
}
static double median (std::vector<double> v) { if (v.empty()) return 0; std::sort (v.begin(), v.end()); return v[v.size() / 2]; }
static double maxOf (const std::vector<double>& v) { return v.empty() ? 0 : *std::max_element (v.begin(), v.end()); }

int main (int argc, char** argv)
{
    const double budgetUs = 1e6 * (double) BLK / SR;      // one buffer's worth of wall time = 100% on the DAW meter
    std::printf ("══ fb631 THE SPIKE ON PRESET CHANGE ══   block %d @ %g Hz = %.0f µs budget\n", (int) BLK, (double) SR, budgetUs);
    std::vector<std::string> files; for (int i = 1; i < argc; ++i) files.push_back (argv[i]);
    if (files.empty()) { std::printf ("  usage: preset_load_spike_au <a.terrain> [b.terrain ...]\n"); return 2; }

    AU a; if (! a.open()) return 2;
    std::string why; const std::string virgin = a.readXml (why);
    a.pump (0.3);
    // steady state with a note held on the virgin patch — the 'before' floor
    a.midi (0x90, 60, 100); timedRender (a, 40);
    const Timing base = timedRender (a, 120);
    const double baseMed = median (base.us), baseMax = maxOf (base.us);
    std::printf ("  init patch, note held:  median %.0f µs (%.1f%% of budget)  worst %.0f µs (%.1f%%)\n", baseMed, 100 * baseMed / budgetUs, baseMax, 100 * baseMax / budgetUs);

    double worstRatio = 0, worstPct = 0; std::string worstName;
    for (const auto& f : files)
    {
        std::string name = f; const std::string xml = xmlOfTerrain (f, name);
        if (xml.empty()) { std::printf ("  %-24s could not read\n", name.c_str()); continue; }
        // the load, with the note still held — exactly Max's gesture in Ableton
        const auto L0 = std::chrono::steady_clock::now();
        a.writeXml (xml);
        const double loadMs = std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - L0).count();
        const Timing after = timedRender (a, 24);            // the first 24 blocks after the load
        a.pump (0.25);                                        // let message-thread work (bakes) run
        const Timing later = timedRender (a, 24);            // and the blocks after THAT
        const Timing steady = timedRender (a, 120);
        const double aMax = maxOf (after.us), lMax = maxOf (later.us), sMed = median (steady.us), sMax = maxOf (steady.us);
        int firstHot = -1; for (size_t i = 0; i < after.us.size(); ++i) if (after.us[i] > 3 * std::max (baseMed, sMed)) { firstHot = (int) i; break; }
        std::printf ("  %-24s load %.1f ms on the caller · first 24 blocks: worst %.0f µs (%.0f%% of budget, block %d) · next 24 after a pump: worst %.0f µs (%.0f%%) · steady: median %.0f µs (%.1f%%), worst %.0f µs (%.0f%%)\n",
                     name.c_str(), loadMs, aMax, 100 * aMax / budgetUs, firstHot, lMax, 100 * lMax / budgetUs, sMed, 100 * sMed / budgetUs, sMax, 100 * sMax / budgetUs);
        const double ratio = aMax / std::max (1.0, sMed), pct = 100 * aMax / budgetUs;
        if (pct > worstPct) { worstPct = pct; worstName = name; }
        if (ratio > worstRatio) worstRatio = ratio;
        a.midi (0x80, 60, 0); a.render (20); a.midi (0x90, 60, 100); a.render (10);
    }
    a.midi (0x80, 60, 0); a.close();
    std::printf ("\n  worst first-block-after-load: %.0f%% of the buffer budget (%s) · worst ratio to steady %.1fx\n", worstPct, worstName.c_str(), worstRatio);
    // THE BAR IS THE METER. Max: "it goes all the way up to like 90 and then shoots back down — that CPU
    // light is the engine light of a car, we never want that to come on." So the first block after a
    // load may not cost more than HALF the buffer budget, for any preset in the bank. Before fb631 it
    // measured 79-153% (the fb414 route push + the filter-type reset storm); after, 10-35%. The ratio
    // to the patch's own steady block is reported but not judged: a patch that idles at 0.5 ms will
    // always show a ratio on a 3 ms block that no meter would ever notice.
    const char* mut = std::getenv ("SP_MUT");
    const bool lenient = mut && std::string (mut) == "lenient";
    chk (lenient ? (worstPct > 50.0) : (worstPct <= 50.0),
         "[1] THE FIRST BLOCK AFTER A PRESET LOAD STAYS UNDER HALF THE BUFFER BUDGET, FOR EVERY PRESET",
         "worst " + std::to_string ((int) worstPct) + "% (" + worstName + ")" + (lenient ? "   (control: expects a spike)" : ""));
    const int rc = summary(); std::printf ("  %d pass, %d fail\n", pass, fail); return rc;
}
