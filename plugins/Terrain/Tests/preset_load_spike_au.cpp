// ══ fb631 — THE SPIKE ON PRESET CHANGE, measured where Ableton measures it: per render block.
//    Max: "every time I change presets there's a big spike in CPU for a millisecond — Ableton's meter
//    jumps to 90 and drops. Serum presets don't do that. That CPU light is the engine light of a car."
//    Ableton's meter is audio-thread time per buffer, so this cert times EVERY AudioUnitRender call
//    before and after a real preset load (Max's own .terrain files) with a note held, and reports the
//    worst block after the load against the steady-state median before it.
//    ⚠️ This harness is single-threaded: the load runs on the calling thread, then the renders. It sees
//    work that lands IN the render path after a load; it cannot see a message-thread lock the audio
//    thread would have to wait for in a DAW. Both are reported on separately below.
// ══ fb636 bugA — RE-SCOPED: THE WHOLE LOAD WINDOW, NOT ITS FIRST 24 BLOCKS. The bar used to judge only the
//    24 blocks right after writeXml. The blocks after the pump (the timer's ticks: pooled-send pairs, bakes,
//    engines) were printed and never judged — and that is exactly where the spike went on a build whose pooled
//    pairs were timer-built: the first render after the tick paid for the freshly built pair (bugA's first
//    attempt measured the build before it at 52-72% of the budget in the post-pump blocks while its first 24
//    read green). bugA builds the pair during the load instead, so the same cost lands in block 0 and the
//    post-pump blocks drop: the SAME spike, relocated from where the gate did not look to where it did. Max's
//    intent is "loading a preset must not spike the CPU at any point while it settles", so the judged number is
//    now the MAX over block 0 through the last post-pump block, with the same 50% bar. Both halves are printed.
//    MEASURED at the re-scope (5 interleaved runs x Max's 52, bugA vs the build before it, side-loaded): the
//    window did NOT get worse — per preset the median window max is lower on 31, higher on 21 (sign test
//    p 0.21), bank mean 36.9% vs 38.9%, the worst block per run 74-118% vs 67-114% (median 90 vs 107). ⚠️ And
//    NEITHER build meets the bar over the whole bank: the re-scope exposes spikes the old window never judged
//    (Max Voltage 66.5% / 107.2% at median, in the first 24 blocks; 7 / 4 presets above 50% at median). Where
//    bugA reads higher (Brahman 55 vs 43, Bloodlust, Corinthians, Snare Jordan, Mario Kingdom) it is the shape
//    the relocation implies: the route push and the pair's first render now share block 0. fb631_gates.sh's
//    set (the first 8 files), 3 runs: worst 60/56/48% vs 41/53/69% — one green run in three on either build.
//    Controls: SP_MUT=lenient inverts the bar (a clean plugin fails it). SP_MUT=latespike busy-waits 60% of the
//    budget INSIDE the timed first post-pump block of every preset — the relocated shape, and nothing else —
//    so the re-scoped bar must go red while the old first-24 bar, still printed, stays where it was.
// ══ fb636 review — [0] EVERY ARGUMENT IS A PRESET THE HARNESS ACTUALLY LOADED. fb631_gates.sh expanded its preset
//    list unquoted, so names with spaces split into fragments: 17 "could not read" lines, and [1] judged the 2 files
//    that survived — a bar over a short list that passed as if it were the set. An unreadable argument is now a
//    FAIL of its own, so a list that did not load can never read green (and [1] cannot pass vacuously on none).
//    RE-MEASURED with all 8 actually loaded (the first 8 files, 3 interleaved runs each, side-loaded, while the machine
//    was also running Max's DAW session): [1] is RED ON BOTH BUILDS in every run, pre-existing — the installed build
//    (cand12) worst 61/65/69%, this build 61/71/50.x% (the bar is <= 50.0). Per preset, median window max of 3,
//    cand12 vs this build: 4th Of July 18/15, Alice In Wonderland 15/40, All You Need 33/44, Bloodlust 31/36,
//    Brahman 43/48, Broken Bitcrush 54/50, Can You Hear Me? 32/26, Clic Up 9/14 — single runs swing 8-65% on one
//    preset, so no per-preset difference here is outside the run-to-run noise. The 60/56/48 vs 41/53/69 above
//    predates [0]; whether its list loaded all 8 cannot be told from its output.
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
static Timing timedRender (AU& a, int nblk, double spinFirstUs = 0)   // spinFirstUs: SP_MUT=latespike only
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
        if (b == 0 && spinFirstUs > 0) while (std::chrono::duration<double, std::micro> (std::chrono::steady_clock::now() - t0).count() < spinFirstUs) {}
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

    const char* mut = std::getenv ("SP_MUT");
    const bool lenient = mut && std::string (mut) == "lenient";
    const double lateSpinUs = (mut && std::string (mut) == "latespike") ? 0.60 * budgetUs : 0.0;   // fb636 bugA control

    AU a; if (! a.open()) return 2;
    std::string why; const std::string virgin = a.readXml (why);
    a.pump (0.3);
    // steady state with a note held on the virgin patch — the 'before' floor
    a.midi (0x90, 60, 100); timedRender (a, 40);
    const Timing base = timedRender (a, 120);
    const double baseMed = median (base.us), baseMax = maxOf (base.us);
    std::printf ("  init patch, note held:  median %.0f µs (%.1f%% of budget)  worst %.0f µs (%.1f%%)\n", baseMed, 100 * baseMed / budgetUs, baseMax, 100 * baseMax / budgetUs);

    double worstRatio = 0, worstPct = 0, worstFirstPct = 0; std::string worstName, worstWhere, worstFirstName;
    std::vector<std::string> unread;   // fb636 review — judged by [0]
    for (const auto& f : files)
    {
        std::string name = f; const std::string xml = xmlOfTerrain (f, name);
        if (xml.empty()) { std::printf ("  %-24s could not read\n", name.c_str()); unread.push_back (f); continue; }
        // the load, with the note still held — exactly Max's gesture in Ableton
        const auto L0 = std::chrono::steady_clock::now();
        a.writeXml (xml);
        const double loadMs = std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - L0).count();
        const Timing after = timedRender (a, 24);            // the first 24 blocks after the load
        a.pump (0.25);                                        // let message-thread work (bakes) run
        const Timing later = timedRender (a, 24, lateSpinUs); // and the blocks after THAT (still the load window)
        const Timing steady = timedRender (a, 120);
        const double aMax = maxOf (after.us), lMax = maxOf (later.us), sMed = median (steady.us), sMax = maxOf (steady.us);
        int firstHot = -1; for (size_t i = 0; i < after.us.size(); ++i) if (after.us[i] > 3 * std::max (baseMed, sMed)) { firstHot = (int) i; break; }
        std::printf ("  %-24s load %.1f ms on the caller · first 24 blocks: worst %.0f µs (%.0f%% of budget, block %d) · next 24 after a pump: worst %.0f µs (%.0f%%) · steady: median %.0f µs (%.1f%%), worst %.0f µs (%.0f%%)\n",
                     name.c_str(), loadMs, aMax, 100 * aMax / budgetUs, firstHot, lMax, 100 * lMax / budgetUs, sMed, 100 * sMed / budgetUs, sMax, 100 * sMax / budgetUs);
        // fb636 bugA — the judged number is the whole load window: block 0 through the last post-pump block
        const double wMax = std::max (aMax, lMax), ratio = wMax / std::max (1.0, sMed), pct = 100 * wMax / budgetUs;
        std::printf ("  %-24s LOAD WINDOW max %.0f µs (%.0f%% of budget, in the %s)\n", "", wMax, pct, aMax >= lMax ? "first 24 blocks" : "blocks after the pump");
        if (pct > worstPct) { worstPct = pct; worstName = name; worstWhere = aMax >= lMax ? "first 24 blocks" : "after the pump"; }
        if (100 * aMax / budgetUs > worstFirstPct) { worstFirstPct = 100 * aMax / budgetUs; worstFirstName = name; }
        if (ratio > worstRatio) worstRatio = ratio;
        a.midi (0x80, 60, 0); a.render (20); a.midi (0x90, 60, 100); a.render (10);
    }
    a.midi (0x80, 60, 0); a.close();
    std::printf ("\n  worst block in the load window: %.0f%% of the buffer budget (%s, %s) · worst ratio to steady %.1fx\n", worstPct, worstName.c_str(), worstWhere.c_str(), worstRatio);
    std::printf ("  (the pre-fb636 scope, first 24 blocks only, reported not judged: %.0f%% (%s))\n", worstFirstPct, worstFirstName.c_str());
    // THE BAR IS THE METER. Max: "it goes all the way up to like 90 and then shoots back down — that CPU
    // light is the engine light of a car, we never want that to come on." So the first block after a
    // load may not cost more than HALF the buffer budget, for any preset in the bank. Before fb631 it
    // measured 79-153% (the fb414 route push + the filter-type reset storm); after, 10-35%. The ratio
    // to the patch's own steady block is reported but not judged: a patch that idles at 0.5 ms will
    // always show a ratio on a 3 ms block that no meter would ever notice.
    // fb636 bugA — "the first block" became "every block of the load window" (see the header): a spike the
    // timer defers by one tick is still the engine light, only later.
    // fb636 review — not inverted by SP_MUT: a control that "fails" because its list did not load proves nothing.
    std::string unreadList; for (const auto& u : unread) unreadList += (unreadList.empty() ? "" : " | ") + u;
    chk (unread.empty(),
         "[0] EVERY PRESET ARGUMENT WAS READ AND LOADED — a short list cannot stand in for the set",
         std::to_string (files.size() - unread.size()) + " of " + std::to_string (files.size()) + " loaded" + (unread.empty() ? "" : "; could not read: " + unreadList));
    chk (lenient ? (worstPct > 50.0) : (worstPct <= 50.0),
         "[1] NO BLOCK OF THE LOAD WINDOW (block 0 through the blocks after the pump) PASSES HALF THE BUFFER BUDGET, FOR EVERY PRESET",
         "worst " + std::to_string ((int) worstPct) + "% (" + worstName + ", " + worstWhere + ")" + (lenient ? "   (control: expects a spike)" : lateSpinUs > 0 ? "   (control: a synthetic 60% block after the pump)" : ""));
    const int rc = summary(); std::printf ("  %d pass, %d fail\n", pass, fail); return rc;
}
