// ══════════════════════════════════════════════════════════════════════════════════════════════
//  fb640 — THE WHOLE CLICK, SIMULATED: real processor, real editor, real WKWebView, real page.
//
//    Tests/browser_freeze_sim.sh      (build Terrain first; macOS; opens a window for ~20 s)
//
//  Max, after fb639: "it still does that little freeze … about a second … it kind of freezes everything and then boom."
//  fb639 timed ONE native call in isolation. This times what the click actually does, end to end:
//    • HOST STALL — a watchdog thread pings the message thread (the host's UI thread inside a DAW) every ~1 ms and
//      records the longest it went unanswered while the browser opened. That is "the whole DAW freezes".
//    • MENU LATENCY — inside the page, click → panel in the DOM → first painted frame, plus every native call's round
//      trip and the time openTwoPaneBrowser spends building rows.
//  It runs the open the way Max does it: cold, straight again, and after an idle pause (the realistic one).
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
#include <future>
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
#include "PluginEditor.h"
#undef private
#undef protected

static double ms() { return juce::Time::getMillisecondCounterHiRes(); }

// ── the host's thread, as the host sees it ─────────────────────────────────────────────────────
static double maxPump = 0; static bool armPump = false;
static void pumpOnce()
{
    const double a = ms();
    CFRunLoopRunInMode (kCFRunLoopDefaultMode, 0.004, true);
    const double d = ms() - a - 4.0;            // anything beyond the 4 ms slice was a handler holding the thread
    if (armPump && d > maxPump) maxPump = d;
}
static bool pumpUntil (std::function<bool()> f, double timeoutMs)
{
    const double end = ms() + timeoutMs;
    while (ms() < end) { if (f()) return true; pumpOnce(); }
    return f();
}
static void pumpFor (double t) { const double end = ms() + t; while (ms() < end) pumpOnce(); }

struct Watchdog
{
    std::atomic<bool> run { true }, armed { false };
    std::atomic<double> maxLat { 0 };
    std::mutex logLock; std::vector<std::pair<double, double>> stalls;   // (start time, length) of every stall ≥ 25 ms
    std::thread th;
    juce::String takeLog (double since)
    {
        std::lock_guard<std::mutex> g (logLock); juce::StringArray s;
        for (auto& p : stalls) s.add (juce::String::formatted ("%.0f ms @+%.0f", p.second, p.first - since));
        stalls.clear(); return s.isEmpty() ? juce::String ("none") : s.joinIntoString (", ");
    }
    void start()
    {
        th = std::thread ([this]
        {
            while (run)
            {
                auto flag = std::make_shared<std::atomic<bool>> (false);
                const double t0 = ms();
                juce::MessageManager::callAsync ([flag] { flag->store (true); });
                while (! flag->load() && run) std::this_thread::sleep_for (std::chrono::microseconds (200));
                const double lat = ms() - t0;
                if (armed && lat >= 25.0) { std::lock_guard<std::mutex> g (logLock); stalls.push_back ({ t0, lat }); }
                if (armed) { double m = maxLat.load(); while (lat > m && ! maxLat.compare_exchange_weak (m, lat)) {} }
                std::this_thread::sleep_for (std::chrono::milliseconds (1));
            }
        });
    }
    void stop() { run = false; if (th.joinable()) th.join(); }
};

static juce::String evalJs (juce::WebBrowserComponent& wv, const juce::String& js, double timeout = 5000)
{
    struct St { bool done = false; juce::String out; };
    auto st = std::make_shared<St>();
    wv.evaluateJavascript (js, [st] (juce::WebBrowserComponent::EvaluationResult r)
    {
        if (auto* v = r.getResult()) st->out = v->isString() ? v->toString() : juce::JSON::toString (*v, true);
        else if (auto* e = r.getError()) st->out = "ERR " + e->message;
        st->done = true;
    });
    pumpUntil ([st] { return st->done; }, timeout);
    return st->out;
}

static const char* kOpenJs = R"JS(
(function(){
  if (window.__tpbClose) { try { window.__tpbClose(); } catch (e) {} }
  if (! window.__simWrapped) { window.__simWrapped = true;
    try { var g = window.Juce.getNativeFunction;
      window.Juce.getNativeFunction = function (n) { var f = g.call (window.Juce, n); if (! f) return f;
        return function () { var a = performance.now(); var p = f.apply (null, arguments);
          try { Promise.resolve (p).then (function () { (window.__simN = window.__simN || []).push (n + ' ' + (performance.now() - a).toFixed (1) + 'ms'); }); } catch (e) {}
          return p; }; };
      window.__simWrapOK = (window.Juce.getNativeFunction !== g); } catch (e) { window.__simWrapOK = false; }
    var o = window.openTwoPaneBrowser;
    window.openTwoPaneBrowser = function (ev, cfg) { var a = performance.now(); var r = o (ev, cfg); window.__simTpb = performance.now() - a; return r; };
  }
  window.__simN = []; window.__simR = null; window.__simTpb = -1;
  var t0 = performance.now();
  /* THE USER'S OWN GESTURE: a real mousedown on the wavetable name's wrapper — the element the page's own listener
     sits on — so every line between the click and the panel is the line a click in the DAW runs. */
  var sel = document.getElementById ('osc-a-preset-select'), wrap = sel ? (sel.parentElement || sel) : null;
  var rc = wrap ? wrap.getBoundingClientRect() : { left: 300, top: 140, width: 0, height: 0 };
  var md = new MouseEvent ('mousedown', { bubbles: true, cancelable: true, button: 0, clientX: rc.left + rc.width / 2, clientY: rc.top + rc.height / 2 });
  window.__simReal = !! (wrap && sel && sel._wtGlass);   // the page hooks the listener on the wrapper and marks the <select>
  if (window.__simReal) wrap.dispatchEvent (md);
  else window.openWtSelectMenu ('a', { clientX: 300, clientY: 140, button: 0, preventDefault: function(){}, stopPropagation: function(){}, target: document.body });
  var sync = performance.now() - t0;
  /* the panel's arrival is stamped by a MutationObserver — a microtask on the insertion itself, which no timer
     throttling can delay (a page WebKit is not rendering runs setTimeout on a ~1 s grid and never runs rAF) */
  window.__simShownAt = -1;
  if (window.__simMO) window.__simMO.disconnect();
  window.__simMO = new MutationObserver (function () {
    if (window.__simShownAt < 0 && document.querySelector ('.tpb-panel')) { window.__simShownAt = performance.now(); window.__simMO.disconnect(); } });
  window.__simMO.observe (document.body, { childList: true });
  (function chk () { var p = document.querySelector ('.tpb-panel');
    if (p) { var shown = (window.__simShownAt >= 0 ? window.__simShownAt : performance.now()) - t0;
      /* the result exists the moment the panel does; the painted frame is filled in when (if) rAF fires — a throttled
         page never runs rAF, and a missing frame must read as "not painted", not as "never opened" */
      window.__simR = { sync: +sync.toFixed (2), shown: +shown.toFixed (1), painted: -1,
                        tpb: +(+window.__simTpb).toFixed (1), rows: p.querySelectorAll ('div').length,
                        natives: (window.__simN || []).slice(), wrap: !! window.__simWrapOK, real: !! window.__simReal };
      var R = window.__simR;
      requestAnimationFrame (function () { requestAnimationFrame (function () {
        R.painted = +(performance.now() - t0).toFixed (1); R.natives = (window.__simN || []).slice(); }); }); }
    else if (performance.now() - t0 > 8000) window.__simR = { timeout: true, natives: (window.__simN || []).slice() };
    else setTimeout (chk, 0); }) ();
  return true;
})()
)JS";

int main()
{
    juce::ScopedJuceInitialiser_GUI init;
    juce::Process::setDockIconVisible (true);   // a REAL foreground app, like a DAW: WebKit throttles (and never paints) a background one
    juce::Process::makeForegroundProcess();
    const auto regDir = juce::File::getSpecialLocation (juce::File::userHomeDirectory).getChildFile ("Library/WavesCrate");
    juce::Array<juce::File> regs; juce::StringArray saved;
    for (auto& f : regDir.findChildFiles (juce::File::findFiles, true, "imports-*.json"))
        if (! f.getFullPathName().contains ("/Backups/")) { regs.add (f); saved.add (f.loadFileAsString()); }

    int fails = 0;
    {
        auto proc = std::make_unique<TerrainAudioProcessor>();
        proc->prepareToPlay (48000.0, 512);

        // the legacy "Imported" drawer's walk, on this thread — what listImports costs per open
        double worstManaged = 0; int managedBytes = 0;
        for (int i = 0; i < 5; ++i) { const double a = ms(); managedBytes = (int) proc->getManagedWavetablesJson().getNumBytesAsUTF8(); worstManaged = juce::jmax (worstManaged, ms() - a); }

        std::unique_ptr<juce::AudioProcessorEditor> ed (proc->createEditor());
        juce::DocumentWindow win ("Terrain — fb640 sim", juce::Colours::black, 0);
        win.setUsingNativeTitleBar (true);
        win.setContentNonOwned (ed.get(), true);
        win.setTopLeftPosition (80, 80);
        win.setVisible (true); win.toFront (true);

        auto* core = dynamic_cast<TerrainUiCore*> (proc->uiCore_.get());
        if (core == nullptr || core->webView == nullptr) { printf ("✗ no core / webView\n"); return 2; }
        auto& wv = *core->webView;

        const double tLoad = ms();
        const bool ready = pumpUntil ([&] { return evalJs (wv, "(typeof window.openWtSelectMenu==='function' && !!document.getElementById('osc-a-preset-select') && !!(window.Juce&&window.Juce.getNativeFunction)) ? 'yes' : 'no'", 500) == "yes"; }, 30000);
        printf ("══ fb640 THE WHOLE CLICK ══  page ready: %s (%.0f ms)   listImports walk on the host thread: worst %.1f ms (%d bytes)\n",
                ready ? "yes" : "NO", ms() - tLoad, worstManaged, managedBytes);
        if (! ready) return 2;
        pumpFor (3000);   // startup timers (200/800/2000 ms) and the editor's prefetch settle, as they would before a user clicks

        Watchdog wd; wd.start();
        {   // BASELINE — the same window of time with NO click: a stall here is not the browser's
            wd.maxLat = 0; wd.armed = true; const double b0 = ms(); pumpFor (8000); wd.armed = false;
            printf ("  idle baseline   host stall %6.1f ms over 8 s with no click │ stalls ≥25 ms: %s\n", wd.maxLat.load(), wd.takeLog (b0).toRawUTF8());
        }
        struct Trial { const char* name; double idleBefore; bool coldCache; };
        const Trial trials[] = { { "first open",        0,    false }, { "again at once",    0,    false },
                                 { "after 2 s idle",    2000, false }, { "after 2 s idle",   2000, false },
                                 { "after 4 s idle",    4000, false }, { "cache wiped",      500,  true  },
                                 { "after 2 s idle",    2000, false } };
        double worstStall = 0, worstShown = -1; int shownCount = 0, paintedCount = 0;
        for (auto& t : trials)
        {
            if (t.idleBefore > 0) pumpFor (t.idleBefore);
            if (t.coldCache) { std::lock_guard<std::mutex> g (proc->importsLock_); for (int k = 0; k < TerrainAudioProcessor::kImportKinds; ++k) { proc->importsJson_[k] = {}; proc->importsStale_[k] = true; } }
            wd.maxLat = 0; wd.armed = true; maxPump = 0; armPump = true;
            const double tClick = ms();
            const auto openRes = evalJs (wv, kOpenJs);
            if (openRes != "true" && openRes != "1") printf ("    (the open script returned: %s)\n", openRes.toRawUTF8());
            juce::String r;
            pumpUntil ([&] { r = evalJs (wv, "window.__simR ? JSON.stringify(window.__simR) : ''", 500); return r.isNotEmpty(); }, 10000);
            pumpFor (400);   // anything that lands just after the panel (a background refresh publishing) counts too
            wd.armed = false; armPump = false;
            const double stall = juce::jmax (wd.maxLat.load(), maxPump);
            auto v = juce::JSON::parse (r);
            const double shown = (double) v.getProperty ("shown", -1.0), painted = (double) v.getProperty ("painted", -1.0);
            worstStall = juce::jmax (worstStall, stall);
            if (shown >= 0) { ++shownCount; worstShown = juce::jmax (worstShown, shown); }   // a trial with no panel counts as a miss below
            if (painted >= 0) ++paintedCount;
            juce::StringArray nat; if (auto* a = v.getProperty ("natives", {}).getArray()) for (auto& x : *a) nat.add (x.toString());
            printf ("  %-15s host stall %6.1f ms (watchdog %6.1f · handler %6.1f) │ page: click→panel %6.1f ms, painted %6.1f ms, rows built %5.1f ms, %d nodes │ %s\n",
                    t.name, stall, wd.maxLat.load(), maxPump, (double) v.getProperty ("shown", -1.0), painted,
                    (double) v.getProperty ("tpb", -1.0), (int) v.getProperty ("rows", 0), nat.joinIntoString (", ").toRawUTF8());
            printf ("    stalls ≥25 ms after the click: %s\n", wd.takeLog (tClick).toRawUTF8());
            if (r.isEmpty() || v.getProperty ("timeout", false)) { printf ("    ✗ the panel never appeared: %s\n", r.toRawUTF8()); ++fails; }
            evalJs (wv, "window.__tpbClose && window.__tpbClose(); true");
            pumpFor (100);
        }
        wd.stop();
        const int nTrials = (int) (sizeof (trials) / sizeof (trials[0]));
        printf ("  worst host stall %.1f ms · worst click→panel %.1f ms · painted frames observed in %d of %d trials%s\n", worstStall, worstShown,
                paintedCount, nTrials, paintedCount ? "" : " (the window was not being rendered — rAF never ran; the panel time is stamped by a MutationObserver instead)");
        const double kStall = 25.0, kShow = 120.0;   // a DAW redraws at 60 Hz: 25 ms is under two frames; 120 ms reads as instant
        printf ("  %s host never stalls ≥ %.0f ms\n", worstStall < kStall ? "✓" : "✗", kStall); fails += worstStall >= kStall;
        const bool showOk = shownCount == nTrials && worstShown >= 0 && worstShown < kShow;
        printf ("  %s the panel is in the page within %.0f ms of the click, in all %d trials (%d reported)\n", showOk ? "✓" : "✗", kShow, nTrials, shownCount); fails += ! showOk;

        win.clearContentComponent(); win.setVisible (false);
        ed.reset();
        pumpFor (200);
        proc->releaseResources();
    }
    for (int i = 0; i < regs.size(); ++i) if (regs[i].loadFileAsString() != saved[i]) regs[i].replaceWithText (saved[i]);
    printf ("browser_freeze_sim: %s\n", fails ? "FAIL" : "PASS");
    return fails ? 1 : 0;
}
