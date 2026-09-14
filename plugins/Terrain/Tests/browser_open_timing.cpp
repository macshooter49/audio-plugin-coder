// ══════════════════════════════════════════════════════════════════════════════════════════════
//  fb639 — THE BROWSER NEVER WAITS FOR THE DISK — measured on the real processor and the real 454-table library.
//
//    Tests/browser_open_timing.sh      (build Terrain first; macOS; links libTerrain_SharedCode.a)
//
//  Max: "every time I try to open the tables, it freezes my FL Studio for about one second … the whole entire DAW."
//  Before: listWtImports ran getImportsJson on the message thread — 93 ms cold, ~39 ms per open after the 1.5 s cache
//  lapsed, on this Mac (far worse on Windows). Now the call must RETURN at once in every state; the walk happens on
//  wtIoPool_. The run pumps the real message loop, so callAsync lands exactly as it does in a host.
//  ⚠️ It adds and removes folders, and addImportPath SAVES the registry — so the user's real registry file is backed up
//  first and restored byte for byte at the end, whatever happens.
// ══════════════════════════════════════════════════════════════════════════════════════════════
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_dsp/juce_dsp.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include <CoreFoundation/CoreFoundation.h>
#include <atomic>
#include <thread>
#include <mutex>
#include <shared_mutex>
#include <condition_variable>
#include <vector>
#include <array>
#include <deque>
#include <list>
#include <set>
#include <map>
#include <unordered_map>
#include <memory>
#include <functional>
#include <string>
#include <sstream>
#include <fstream>
#include <random>
#include <optional>
#include <bit>
#include <span>
#include <chrono>
#include <cmath>
#include <cstring>
#include <algorithm>
#include <numeric>
#define private public
#define protected public
#include "PluginProcessor.h"
#undef private
#undef protected

static double ms() { return juce::Time::getMillisecondCounterHiRes(); }
static bool pumpUntil (std::function<bool()> f, double timeoutMs)
{
    const double end = ms() + timeoutMs;
    while (ms() < end) { if (f()) return true; CFRunLoopRunInMode (kCFRunLoopDefaultMode, 0.005, true); }
    return f();
}
static juce::String stripScan (const juce::String& js)
{
    auto v = juce::JSON::parse (js);
    if (auto* o = v.getDynamicObject()) o->removeProperty ("scan");
    return juce::JSON::toString (v, true);
}
static int fails = 0;
static void bar (const char* tag, bool ok, const juce::String& text)
{
    printf ("  %s [%s] %s\n", ok ? "✓" : "✗", tag, text.toRawUTF8()); if (! ok) ++fails;
}

int main()
{
    juce::ScopedJuceInitialiser_GUI init;
    const auto regDir = juce::File::getSpecialLocation (juce::File::userHomeDirectory).getChildFile ("Library/WavesCrate");
    juce::Array<juce::File> regs; juce::StringArray saved;
    for (auto& f : regDir.findChildFiles (juce::File::findFiles, true, "imports-*.json"))
        if (! f.getFullPathName().contains ("/Backups/")) { regs.add (f); saved.add (f.loadFileAsString()); }
    auto restore = [&] { for (int i = 0; i < regs.size(); ++i) regs[i].replaceWithText (saved[i]); };

    const auto tmp = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("fb639_browser_timing");
    tmp.deleteRecursively();
    const auto dirA = tmp.getChildFile ("Added Folder A"), dirB = tmp.getChildFile ("Added Folder B");
    for (auto d : { dirA, dirB }) { d.createDirectory(); d.getChildFile ("one.wav").replaceWithText ("x"); d.getChildFile ("two.wav").replaceWithText ("x"); }

    {
        auto proc = std::make_unique<TerrainAudioProcessor>();
        auto& p = *proc;
        printf ("══ fb639 THE BROWSER NEVER WAITS FOR THE DISK ══\n");

        // what the OLD path cost on this thread (the direct walk, for the record)
        p.importsCacheValid_[1] = false;
        double a = ms(); const auto direct = p.getImportsJson (1); const double oldCost = ms() - a;

        // [1] cold: the call returns at once, the answer lands later via the message loop
        p.importsJson_[1] = {}; p.importsStale_[1] = true;
        juce::String got; bool done = false;
        a = ms(); p.requestImportsJson (1, [&] (const juce::String& s) { got = s; done = true; }); const double cold = ms() - a;
        const bool landed = pumpUntil ([&] { return done; }, 10000);
        bar ("1", cold < 2.0 && landed, juce::String::formatted ("a cold open returns in %.3f ms (the walk used to cost %.1f ms right here); the answer landed: %s",
                                                                  cold, oldCost, landed ? "yes" : "NO"));

        // [2] warm: instant, and the same library as a direct walk
        double worst = 0; juce::String w;
        for (int i = 0; i < 50; ++i) { a = ms(); p.requestImportsJson (1, [&] (const juce::String& s) { w = s; }); worst = juce::jmax (worst, ms() - a); }
        bar ("2", worst < 0.5 && stripScan (w) == stripScan (direct),
             juce::String::formatted ("50 warm opens: worst %.3f ms on this thread; payload identical to a direct walk (%d bytes): %s",
                                      worst, (int) w.getNumBytesAsUTF8(), stripScan (w) == stripScan (direct) ? "yes" : "NO"));

        // [3] past the TTL: still instant; the refresh happens behind the user's back
        p.importsJsonAt_[1] -= 5000;
        const auto before = p.importsJsonAt_[1];
        a = ms(); p.requestImportsJson (1, [&] (const juce::String&) {}); const double stale = ms() - a;
        const bool refreshed = pumpUntil ([&] { return p.importsJsonAt_[1] != before && ! p.importsScanning_[1]; }, 10000);
        bar ("3", stale < 0.5 && refreshed, juce::String::formatted ("an open past the 1.5 s window returns in %.3f ms and refreshes in the background: %s",
                                                                     stale, refreshed ? "yes" : "NO"));

        // [4] fb606's law survives: a folder added is in the VERY NEXT answer
        p.addImportPathAsync (1, dirA.getFullPathName());
        juce::String next; bool nd = false;
        a = ms(); p.requestImportsJson (1, [&] (const juce::String& s) { next = s; nd = true; }); const double afterAdd = ms() - a;
        pumpUntil ([&] { return nd; }, 10000);
        bar ("4", afterAdd < 2.0 && next.contains ("Added Folder A"),
             juce::String::formatted ("the open after adding a folder returns in %.3f ms and its answer holds the new folder: %s",
                                      afterAdd, next.contains ("Added Folder A") ? "yes" : "NO"));

        // [5] a folder added WHILE a walk holds the lock never waits, and still arrives
        std::atomic<bool> held { false }, release { false };
        std::thread holder ([&] { std::lock_guard<std::mutex> g (p.importsLock_); held = true; while (! release) std::this_thread::sleep_for (std::chrono::milliseconds (2)); });
        while (! held) std::this_thread::sleep_for (std::chrono::milliseconds (1));
        p.importsStale_[1] = true; p.startImportsScan (1);                       // a walk that will need the lock
        a = ms(); p.addImportPathAsync (1, dirB.getFullPathName()); const double editCost = ms() - a;
        const bool queued = p.importsPendingEdits_.size() == 1;
        std::this_thread::sleep_for (std::chrono::milliseconds (300)); release = true; holder.join();
        juce::String fin; bool fd = false;
        pumpUntil ([&] { return ! p.importsScanning_[1] && p.importsPendingEdits_.empty() && ! p.importsStale_[1]; }, 10000);
        p.requestImportsJson (1, [&] (const juce::String& s) { fin = s; fd = true; });
        pumpUntil ([&] { return fd; }, 10000);
        bar ("5", editCost < 2.0 && queued && fin.contains ("Added Folder B") && fin.contains ("Added Folder A"),
             juce::String::formatted ("adding a folder while a walk holds the lock took %.3f ms (queued: %s); both folders arrive: %s",
                                      editCost, queued ? "yes" : "NO", (fin.contains ("Added Folder B") && fin.contains ("Added Folder A")) ? "yes" : "NO"));

        // leave the registry as we found it
        p.removeImportPathAsync (1, dirA.getFullPathName()); p.removeImportPathAsync (1, dirB.getFullPathName());
        pumpUntil ([&] { return ! p.importsScanning_[1] && p.importsPendingEdits_.empty(); }, 10000);
        p.releaseResources();
    }
    restore();
    tmp.deleteRecursively();
    bool same = true; for (int i = 0; i < regs.size(); ++i) same &= regs[i].loadFileAsString() == saved[i];
    bar ("6", same, juce::String::formatted ("the user's %d registry files are exactly as they were", regs.size()));
    printf ("browser_open_timing: %s\n", fails ? "FAIL" : "PASS");
    return fails ? 1 : 0;
}
