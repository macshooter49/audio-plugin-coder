# TERRAIN — WINDOWS SESSION HANDOFF

Written 2026-08-24 at the end of the first Windows session, so a session running **on the Windows
machine** can continue without re-deriving any of it. Read this file first.

---

## 1. WHERE THIS STANDS

Terrain is a JUCE audio plugin (synth) whose entire UI is one 34,000-line `index.html` in a
WebView. It was developed and tuned exclusively on macOS. Tonight was its first Windows build ever.

**It builds, loads, plays, and validates on Windows.** VST3 and Standalone. What remained at
handoff was CPU and smoothness, not correctness.

### The root cause of the whole "Windows is slow" saga

**FL Studio subdivides its audio buffer.** With FL's buffer set to 512 samples, Terrain's
`processBlock` was measured receiving **45-sample blocks** — about **980 calls per second** instead
of the 86 a 512 buffer implies. Terrain did ~2,400 lines of parameter gathering, a 96×93 route
broadcast, and a pile of per-sample `std::map` lookups, `pow()` calls and RTTI **on every one of
them**. macOS hid all of it because a Mac standalone gets a tenth of the calls.

Nothing about Windows is broken. The work was simply priced honestly for the first time.

### Measured progress (Tests/au_blk_cpu.cpp — idle, no notes, same machine, back to back)

| host block | calls/s | fb491 (start) | fb495 | change |
|-----------:|--------:|--------------:|------:|--------|
| **45** (FL's real rate) | 980 | 11.00% | **3.17%** | **3.47× cheaper** |
| 88 | 501 | 6.82% | **2.80%** | 2.44× |
| 128 | 345 | 5.48% | **2.69%** | |
| 256 | 172 | 3.91% | **2.54%** | |
| 512 (the floor) | 86 | 3.19% | **1.94%** | 1.64× — helps macOS too |

At **fb497** the harness prints its own verdict for the first time:
`cost(45)/cost(512) = 1.59x — FLAT ENOUGH, the gather no longer rides the host's call rate.`
(It was **3.45×** at fb491.) Current row: 45→3.14%, 88→2.82%, 128→2.72%, 256→2.60%, 512→1.98%.

### Memory (fb497, measured by the same `auval` command before and after)
**Peak RSS 18,688 MB → 12,065 MB.** Stem rings −1,009 MB, capture ring −202 MB, wavetable bank
−107 MB — all now allocated lazily on first real use. Every feature preserved; the trade-offs are
listed in §5.

### UI (fb497)
Front page **17.6% → 15.4%**, synth page **26.9% → 18.6%** of a core. Verified by a deterministic
screenshot diff (seeded RNG, pinned clock, manual rAF stepping, animations frozen): **166 of
1,872,000 pixels differ** — against a before-vs-before control whose own noise floor was 1,230.

### UI cost (Chrome CPU profile of the real page; Chrome == WebView2's engine)

- Front page **11.1%** of a core — floor with every animation loop disabled: **0.54%**.
  So the front page is almost entirely JavaScript that can be deleted.
- Synth page **17.0%** — floor **11.35%**. Most of the synth page is **structural DOM
  layout/paint/compositing**, which loop-gating cannot touch.
- On a Windows laptop core (2–3× slower than this Mac) those map to roughly **22–33%** and
  **34–50%** — which is exactly the "20–30% just from opening the window" that was reported.
- **There is no open spike.** Profiling from before navigation: load is 170 ms with no large
  one-off cost. The jump on opening the window *is* the steady-state loop cost.

---

## 2. HOW TO BUILD

### A. Don't build — download it (fastest)
Every push to `windows-test` compiles on a Windows runner:
<https://github.com/macshooter49/audio-plugin-coder/actions> → newest **Terrain Windows** run →
artifact **TerrainInstrument-windows-vst3**. Unzip, copy the `.vst3` into
`C:\Program Files\Common Files\VST3\` (needs an admin shell), restart the DAW.

### B. Build locally
```powershell
cd C:\dev\audio-plugin-coder
git fetch origin; git reset --hard origin/windows-test
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -D "JUCE_WEBVIEW2_PACKAGE_LOCATION=C:\dev\nuget"
cmake --build build --config Release --target TerrainInstrument_VST3 -- /m
```
Two Windows-only prerequisites, both automated by `scripts/windows-quickstart.ps1`:
1. **The WebView2 SDK.** JUCE's `FindWebView2` wants a folder *containing* a
   `Microsoft.Web.WebView2*` directory. A `.nupkg` is a zip — no NuGet tooling needed.
2. **`scripts/juce-webview2-failure-cap.patch`** applied to `_tools/JUCE` (see §4).

### C. The one-command script
`scripts/windows-quickstart.ps1` installs prerequisites, clones/updates, patches JUCE, fetches the
SDK and builds. `-InstallPlugin` (from an **admin** shell) copies the VST3 into Program Files.

---

## 3. HOW TO MEASURE (never guess — every wrong turn tonight came from guessing)

### The DSP meter, on Windows, live
`%TEMP%\terrain-cpu.txt`, refreshed every 5 s **when `TERRAIN_CPU_PROBE` is set in the environment**
(it was unconditional until fb496; gating it stopped a release build writing a file forever):
```
DSP 3.2% of one core  [gather 1.1 | voices 0.4 | fx+master 1.6]  blk 88 @ 44100 Hz  voices 0 | ...
```
- `gather` = per-**call** setup work → rises when the host subdivides buffers.
- `fx+master` = per-**sample** work → roughly flat against block size.
- `blk` is the **average** block size (it reported the *last* one until fb492, which made two
  reports look contradictory — 45 vs 88 — and cost a round trip).
- `frames`/`acks` = the editor→WebView transport; if `frames` races ahead of `acks`, the channel
  has wedged.

### The block-size harness (macOS only today — **worth porting to Windows**)
`Tests/au_blk_cpu.cpp` renders the installed AU idle at 45/88/128/256/512-sample blocks and prints
CPU as a share of one core. Block size changes *only how often the host calls*, so a **sloped row
is fixed per-call cost and a flat row is none**. This is the single most useful instrument built
tonight. A Windows port needs a small VST3 host (or pluginval's harness) doing the same thing.

### The UI profile
`node Tests/eq_ui.js` / `fx4_ui.js` / `fx3_ui.js` (needs `puppeteer-core`; set `NODE_PATH`).
Must stay at **15/15**, **131/131**, **48 passed**. For CPU, drive Chrome's CDP Profiler over the
page — **pin rAF to 60 fps first**: headless Chrome free-runs at 120–290 fps and inflates every
result 2–5×.

---

## 4. WHAT WAS FIXED (fb480 → fb496)

| # | Fix |
|---|-----|
| fb480 | JUCE retried WebView2 controller creation **forever** on failure — an infinite async-update storm on the host UI thread that froze all of FL. Capped at 3 attempts (`scripts/juce-webview2-failure-cap.patch`). |
| fb481 | Portable fast FFT for non-Apple platforms (the wavetable bake was on the scalar reference). Nulls against it at −270 dBr. Plus `/arch:AVX`. |
| fb482 | **Backpressure**: never push a viz frame the WebView hasn't acknowledged. Input starvation was the freeze. |
| fb483 | One coalesced frame per tick instead of five; idle-skip when the frame is byte-identical. |
| fb484 | Standalone QWERTY→MIDI (`AWSEDFTGYHUJKOLP`, `Z`/`X` octaves) so sound can be tested without a DAW. |
| fb485 | **Windows frame transport moved to WebView2's web-message lane** — data, not `ExecuteScript`. This ended the FL freeze. |
| fb486 | Frame diet: scope stride-2 @2dp, EQ 3dp, the two whales alternate frames. ~50–110 KB → ~5 KB. |
| fb487 | Occlusion gates on the two legacy front-page loops; TOPO half-rate on Windows. |
| fb488 | **The analyzer FFT left the audio thread** (2× 4096-point FFTs, ~94/s, inside the real-time callback, only when the editor was open). |
| fb489 | Phase-split DSP meter (gather / voices / fx+master) + average block size. |
| fb490 | Flat allocation-free parameter table (was `std::unordered_map`); chorus stopped allocating heap coefficients every block. |
| fb492 | **Control-rate gather** — ~900 lines of knob-reading now run at ~172 Hz, not on every call. |
| fb493 | **LFO phase-locked loop** — fb487's extrapolation snapped backward on every update (the "jolt"). |
| fb494 | Per-sample waste: EQ slope `map`+`dynamic_cast`, ten global LFOs, two `pow()`, five choice loads, a 960-byte stack clear, an integer division. |
| fb495 | **Route broadcast gated on an exact change check** — 96×93 setters (~90,000 writes) per call, now only when something changed. |
| fb496 | Profiled UI cuts (`vno` allocations, per-knob `querySelector`, meter lookups, `textContent`); memory work (below). |

---

## 5. WHAT IS OPEN

1. **Memory — DONE and verified (fb497): 18,688 MB → 12,065 MB.** What remains is the *ratchet*:
   `releaseResources()` (PluginProcessor.cpp ~6845) never frees the stem/capture rings, and
   **`ModalEngine` allocates waveguide delay lines for EVERY voice in the constructor**
   (SynthVoice.h:5253). Those two are the next memory targets. Original note follows — Stem rings (−1,009 MB), capture ring
   (−202 MB) and the wavetable bank (−107 MB) are now lazily allocated. **Build and re-measure**
   `/usr/bin/time -l auval -v aumu Tern Wvcr` (macOS) or the Windows equivalent. Trade-offs the
   agent recorded honestly: audio played before the plugin window has *ever* been opened is not
   captured; arming stems costs an ~80 ms message-thread pause on first sample load; a wavetable
   preset change while playing lands up to 16.7 ms later.
2. **The "18 GB leak" is NOT a leak** — proven (`leaks`: 0 leaked bytes; 2 ctors / 2 dtors; the
   growth converges to +0.0 MB by the third pass). It is **sizing plus a grow-only ratchet**:
   `releaseResources()` never frees the rings, and `ModalEngine` waveguide delay lines are
   allocated eagerly for **every** voice in the constructor. Fix those two next.
3. **The synth page's structural paint cost** (11.35% floor). Levers: CSS `contain: layout paint`
   on panels/cards, `content-visibility`, fewer per-frame style writes. Measure with CDP tracing —
   adding compositor layers can make it *worse*.
4. **~111 native round trips per second at rest** — each a cross-process call on Windows.
   `getModDrag` at 30 Hz and `getNoiseViz` at 12 Hz poll for events that are not happening.
5. **A popped-out card window keeps the audio thread doing full viz work with no editor on
   screen** (it holds `uiClients_` open).
6. **~1.89× of slope remains** at FL's call rate: the fb75 LFO mod pass and the pre-gather
   MIDI/layer region still run on every call.
7. **The LFO *value* feed** (`__mvLfoVal`, consumed around index.html:27541 and 27810) still
   latches raw at push rate — same class as the fb493 jolt, not yet smoothed.

---

## 6. TRAPS THAT COST REAL TIME TONIGHT

- **PowerShell reads a BOM-less script as Windows-1252.** UTF-8 box-drawing characters decode to
  `”`, which PowerShell treats as string quotes. Every `.ps1` in this repo is now pure ASCII —
  keep it that way.
- **`git apply --reverse --check` is *supposed* to fail** when a patch isn't applied yet; PS 5.1
  turns redirected native stderr into script-killing errors. Those checks run through `cmd /c`.
- **MSVC caps a string literal at ~16 KB** and needs `/bigobj`, `_USE_MATH_DEFINES` and `/utf-8`.
- **`raw.githubusercontent.com` caches ~5 minutes** — use commit-pinned URLs, not branch URLs.
- **An acquire-load is free on x86 and a barrier on ARM.** A "fix" using them was a Windows win
  paid for with a macOS regression; it was caught only by re-measuring.
- **A `//` comment appended to a line of live code silently deletes it** (fb487 swallowed an
  `if(window.__cardOnly) return;` guard this way).
- **Measure, then change, then re-measure.** Every wrong turn tonight was a plausible theory that
  a measurement killed in one minute.

---

## 7. fb501/fb502 — THE WINDOW COSTS ~190% OF A CORE, AND IT IS THE PAGE

Measured on the Windows machine with `Tests/win_blk_cpu.cpp --editor` (real editor, real window,
real-time-paced audio thread, `GetThreadTimes` per thread) plus per-process WebView2 sampling.
One WT osc, no notes, idle, blk 512:

| | editor CLOSED | editor OPEN |
|---|---:|---:|
| audio thread | 7.99% | 10.42% |
| message thread | 0.52% | 11.08% |
| plugin process TOTAL | 8.67% | 24.06% |
| **WebView2 processes** | **0.1%** | **173.7%** |

**~190% of a core, and 85-90% of it is inside WebView2, not the plugin.** The control is clean
(0.1% closed), so this is unambiguous. Split: gpu-process ~120-150%, renderer ~40-70%.

### What the page costs is REAL, and it is the animation loops
Killing every `requestAnimationFrame` + interval makes the page static:
`gpu 110.4% -> 4.9%`, `renderer 39.1% -> 10.8%`, `WebView2 149.8% -> 21.2%`.
**86% of the UI cost is the page animating itself.** That part is settled.

### ⚠️ WHAT DOES *NOT* WORK — measured, counterbalanced, do not retry blind
Every one of these was built, embedded (verified by searching the .vst3 binary) and A/B'd:

- **GPU flags.** `--ignore-gpu-blocklist --enable-gpu-rasterization`: 110.4 -> 112.2 (nothing).
  `--disable-gpu`: total 149.8 -> 223.0 (**worse** — the GPU is already helping).
- **backdrop-filter.** Neutralising all 38: gpu 110.4 -> 99.1. Worth ~9 points, not 110.
- **UI push rate.** 60 -> 20 Hz cut ticks 3x but UI cost only 28%, because per-tick cost ROSE
  (0.92 -> 1.97 ms/tick) as frames grew. Content-driven, not frequency-driven.
- **🚨 REDUCING CANVAS DRAW RATE MAKES IT WORSE.** Halving the hero terrain's redraw (one line,
  85% of front-page draw calls) — counterbalanced B/H/H/B/B/H/H/B, 4 runs each:
      BEFORE 174, 175, 174.7, 181.1  -> mean 176.2%
      HERO   187.2, 185.3, 182.3, 179.8 -> mean 183.7%
  Drawing LESS cost MORE, reproducibly. A fuller change-gate across knobs/icons/fx knobs was
  worse still (226% vs 177%), partly because the gate built a STRING signature per canvas per
  frame (renderer 42% -> 70%: never allocate in a per-frame path). **All of it was reverted.**
  Conclusion: canvas draw-call VOLUME is not the driver, and the compositor's response to it is
  not linear. `Tests/canvas_profile.js` ranks canvases by redraw rate / ops / visibility and shows
  97% of redrawn pixels are ON SCREEN, so visibility gating cannot be the win either.

### The next hypothesis (untested)
The loops also write DOM every frame — inline `style.left/width/top`, SVG `setAttribute('d')`,
`textContent` — across a page carrying 250 border-radius, 106 box-shadow, 38 backdrop-filter.
That is layout+paint+composite work, which is where a gpu-process burning a CPU core would come
from. Test it the same way the rAF kill was tested: stub only the DOM writes, leave canvas alone.
**Iterate in headful Chrome measuring Chrome's own gpu-process** — same engine, no 5-minute
plugin rebuild per idea.

### Traps this cost time on
- **The BinaryData cache is real on Windows too.** An edited index.html silently does NOT re-embed;
  the build "succeeds" with the OLD page. Always verify by searching the built .vst3 for a marker
  string, and bust it with: delete `build/plugins/TerrainInstrument/juce_binarydata_*_WebUI`,
  `TerrainInstrument_WebUI.dir`, `Release/TerrainInstrument_WebUI.lib`, then re-run cmake configure.
- **A/B confounded with run order.** The same binary varies 162-181% run to run (±12%). Always
  counterbalance (B,A,A,B) and take means, or drift reads as an effect.
- **`Start-Process -ArgumentList` splits a path containing a space** — those runs silently measured
  nothing and reported 0.00%. Quote the path inside the argument.
- **The beacon is WALL-CLOCK paced.** An offline harness rendering 15x faster than real time exits
  before it ever ticks; that looks exactly like "the probe is broken under VST3" and is not.

### fb503 — the WebView cost is NOT rate-related. Four levers falsified in the real plugin.

Traced the renderer (`Tests/ui_trace.js`, self-time per bucket, front page, headful):

    GPU 87.4% | Scripting 12.3% | Layout 4.2% | Style 3.7% | Composite 2.3% | Paint 0.7%

So it is not our JS and not our DOM writes — the earlier "DOM writes are next" hypothesis is dead.
It is GPU command work. `Tests/ui_layers.js` then ruled out the obvious follow-up: the page has
only **35-39 compositing layers / 7.0 Mpx** (normal), but **4,821 live elements and 211 canvases**.

**Everything below was BUILT, embedded-verified in the .vst3, and A/B'd COUNTERBALANCED against a
clean build in the real plugin. All of it was reverted.**

| change | result |
|---|---|
| hero terrain half-rate (1 line, 85% of front-page draws) | 176.2% -> 183.7% **worse** |
| full canvas change-gate (knobs/icons/fx knobs/setupCanvas) | 177% -> 226% **worse** |
| global rAF cap to 30 fps | 165.7% -> 194.3% **worse** |
| C++ push rate 60 -> 30 Hz (`TERRAIN_UI_HZ`) | 170.5% -> 171.1% **no effect** |
| rAF cap AND C++ 30 Hz together | 165.4% -> 195.5% **worse** |
| all CSS `filter` + `backdrop-filter` off | 170.3% -> 165.8% (~noise) |

⚠️ **CHROME IS NOT A VALID PROXY FOR THE PLUGIN.** The global rAF cap measured a clean win in
Chrome (GPU 87.4% -> 47.9% at 30 fps, near-linear) and a clean LOSS in the plugin. The plugin has
C++ pushing a frame every 16 ms, so frames are produced regardless of rAF; capping only batches
85 loops into one burst. Use Chrome to form a hypothesis, never to accept one.

### What the evidence actually supports
The cost is not proportional to frame rate — it behaves like a fixed per-vsync price that is paid
whenever the page is animating AT ALL. The only thing that ever moved it far was stopping every
loop: `WebView2 149.8% -> 21.2%`, `gpu 110.4% -> 4.9%`.

So the target is **ZERO frames at idle, not fewer frames**. That means every loop parks itself
when it has nothing to show AND the C++ push stops (fb483's idle-skip already exists but never
fires, because the LFO phases free-run and make every frame differ). Until the page can reach a
true idle, throttling anything is measurably counterproductive.

### fb504 — SOLVED: the frame arbiter. WebView2 177% -> 38% at idle, shipped and verified.

The fb503 conclusion held: the cost is per-vsync and near-binary. The loop inventory (7-agent
sweep, in the fb504 commit) found WHY the page could never rest: of 33 self-rearming rAF loops,
~24 re-arm even with nothing to draw, 7 intervals poll natives forever (getModDrag at 30 Hz was
the worst), and 16 infinite CSS + 6 SMIL animations tick the compositor with zero rAF.

**The exp hook made the fix findable**: if %TEMP%\terrain-ui-exp.js exists when the editor opens,
it runs in the page once (result to terrain-ui-exp-result.txt). Hypothesis cost fell from a
6-minute rebuild to a 50-second run IN THE REAL PLUGIN. Bisection, cumulative, real plugin:
    stock idle                                 171.6%
    rAF parked (freeze)                         62.6%
    + intervals stopped + CSS/SMIL paused       10.7%
Also measured on the way: thermal drift makes identical runs read 204-240 normalized — the
bisect harness now samples '\Processor Information(_Total)\% Processor Performance' during every
window and prints a clock-normalized column. Sub-15% deltas without it are fiction.

**What shipped (index.html, Windows-UA-gated — the Mac path is byte-untouched):**
1. THE ARBITER — first script in the page, replaces requestAnimationFrame. Interacting or notes
   playing = full 60 fps, nothing changes. After 1.5 s of neither: parked — queued rAF callbacks
   run as a 1 fps slideshow (queue bounded, page never looks dead, automation goes ≤1 s stale),
   html.ti-parked pauses every CSS animation, SMIL svgs get pauseAnimations(). Any input wakes it
   in <300 ms; the first note wakes it within ~1 s. window.__tiForceActive=true disables parking.
2. window.__tiParked gates on the 7 always-on intervals (getModDrag 30 Hz, FMIX x8, cmp/ott
   400 ms, mod-merge 2.5 s, VIZDBG, lfoXWinSync, crvXWin).
3. Everything the C++ pushes by direct eval — meters, scope, analyzer — stays LIVE while parked;
   only pure-JS decoration parks.

**Verified:** counterbalanced A/B on the shipped binary: BEFORE 179.1/175.1, ARBITER 42.5/33.6
(WebView2, % of one core). Final end-to-end: WebView2 33.0%, plugin process 31.4%. The 8-check
contract test is Tests/arbiter_contract.js (parks, 1.3 fps parked, <300 ms wake on input, wakes
on notes, re-parks, 64 fps active, no callback dropped). UI gates eq_ui 15/15, fx4_ui 130/130,
fx3_ui 48 — all with the arbiter ACTIVE in the test browser. DSP untouched: fingerprint
446f2e02c4dcb215, live-switch PASS, peak 540.6 MB.

Remaining, in order of measured size: the plugin process message thread (~17% — the 60 Hz push
build+ship churn; C++ idle-skip never fires because free-running LFO phases change every frame's
hash), and the active-state cost while PLAYING (unchanged ~170% — next target if playing-with-
window-open needs to come down too).

### fb505 — SERUM-2 POLICY: decoration static ALWAYS on Windows. WebView2 idle 17%, playing-with-static-data 16%.

Max tested fb504 and rejected the 1 fps ambient slideshow ("that's not gonna work"). Verdict:
NOTHING moves on its own on Windows — decoration is static even WHILE NOTES PLAY; only REACTIVE
visuals (scope, meters, wavetable, filter curve, envelope, LFO-during-notes) animate. Shipped:

1. Arbiter drain 1 s -> 5 s (parked page is visually STATIC; queue stays bounded).
2. `__tiSlow(cb)` — decorative loops re-arm with this: 2 fps while pointer/keys active (resizes
   never look stale), one frame per 10 s otherwise. On macOS it IS requestAnimationFrame.
   Applied to: topo contour map, RR wander, SWAY lane.
3. `__tiDecoDue(key)` — mixed loops draw their decoration only when this fires (same cadence).
   Applied to: animated icons, section icons, tape reels (front page), FX-suite tape viz,
   analyzer fk-em emblems. renderTerrain (hero mountain) stays 60 fps — it reacts to the audio
   scope and is the identity visual; the arbiter freezes it at idle anyway.
4. The infinite decorative CSS animations die PERMANENTLY on Windows (targeted list, one-shot
   feedback flashes untouched); SMIL emblems paused at boot and never unpaused.
5. `__notesActive` is now a property SETTER on Windows: the C++ push assigning it 0->1 wakes a
   parked page INSTANTLY (the 5 s drain would otherwise leave viz frozen up to 5 s on the first
   key press).

MEASURED (bisect harness, clock column watched): idle 17.2%, notes-forced-with-static-data
15.9% — vs 179.3% for the pre-arbiter baseline in the same forced-notes state. Cost is now
proportional to what CHANGES on screen, not to time passing. Contract test 8/8 (note-wake
included), eq_ui 15/15, fx4_ui 130/130, fx3_ui 48, lfo_val_smooth ALL PASSED, fingerprint
446f2e02c4dcb215, peak 540.8 MB. Installed to Program Files (hash-verified).

Still open if more is wanted: the plugin process itself (~23-31% — message-thread push churn;
C++ idle-skip never fires because free-running LFO phases change every frame's hash), and
whatever REAL audio playback costs (scope/meter data changes every frame during sound — not
measurable in this harness; Max tests in FL).

### fb506 — THE ROOT CAUSE HAD A NUMBER: THE PANEL IS 360 Hz. Plus the retrigger PLL fix.

**This machine's display is 360 Hz** (Win32_VideoController CurrentRefreshRate=360). On Windows
rAF fires at PANEL rate, so every "60 fps" loop ran at up to 360 fps — six times the Mac's work.
It is why per-frame optimisation never moved the number, why the LFO/envelope looked sloppy
(phase math tuned for 16.7 ms frames on a 2.8 ms budget), and why FL dropped frames.

New instrument: --play in Tests/win_blk_cpu.cpp — the editor harness now PLAYS an arpeggiated
chord, so scope/LFO/analyzer carry CHANGING data. First honest measurement of Max's real state:
    PLAY + synth page:  WebView2 212%, plugin process 99%   <- the complaint, quantified
    idle + synth page:  WebView2 48%,  plugin process 80%
Push-rate sweep on that state: FLAT (60/30/20 Hz all ~206-238) — the rAF consumer loops are the
cost during play, not the push.

Shipped (fb506c):
1. **Pacing, page-aware.** The arbiter's active path runs callbacks at most 60/s ON THE PANEL
   PAGES (synth/EQ/delay/mod — currentActivePanel truthy); skips wait on a TIMER, never a rAF
   re-arm (a pending rAF keeps BeginFrame at 360 Hz: measured +24). The front page stays native —
   pacing measured -114 on the JS-bound synth page but +25 on the compositor-bound front (the
   hero misaligns with vsync and double-rasters). __UI_FPS overrides live.
2. **Trigger-aware PLL** (the retrigger glitch): Retrig/Env push the most-active VOICE's phase,
   which resets on every note-on; the PLL wrapped that backward jump into a fake ~24 Hz forward
   rate — dot raced, rubber-banded, pinned at start phase. Now: backward jump in a trigger mode =
   RESET -> snap to DSP truth, never feed the wrap into the rate; byte-equal pinned phase -> ease
   rate to 0 (park on the pin, no buzzing ahead). Free/mono bit-identical.

Measured (fb506c, same session): PLAY+synth 212 -> 93. idle+synth 48 -> 9.5. Front unchanged by
design. All gates green (contract 8/8, eq 15/15, fx4 130/130, fx3 48, lfo smooth, fingerprint
bit-identical). Installed.

STILL OPEN, ranked: (1) plugin process ~93% during synth-page play — the C++ message thread
composing 60 Hz synth-page frames; segment-level idle-skip / 30 Hz Windows push for the whale
segments is the lever (push-rate flat result was WEBVIEW-side; the C++ compose cost still scales
with rate). (2) front-page play ~200% — renderTerrain compositor work. (3) envelope feed has no
fb498-style interpolation (level/stage step at push rate) and voice-hopping teleports the dot —
needs C++ voice-selection hysteresis. (4) editor open takes 5-10 s (WebView2 boot + 34k-line
parse + boot pushes) — unmeasured.

### fb507 — the C++ whale (the EQ spectrum's silence problem) + smooth-while-active decorations

The message-thread meter found it in one line: idle SYNTH page = `UI 57.8% [build 54.2 | ship
3.6], 15.23 ms/tick, 38 Hz` — the tick could not even reach its own 60 Hz. The whale: the fb342
EQ spectrum push gates on "a FRESH analyzer frame", but an FFT OF SILENCE publishes fresh frames
forever — so with the synth page open and nothing sounding, the editor ran two 4096-point FFTs
AND built a ~29 KB string every tick, for a spectrum whose pixels were the noise floor.

**fb507 SILENCE GATE** (PluginEditor.cpp, fb342 block): inaudible output (voices==0 and
oscScopeORms < 1e-4) for ~1.5 s (tail draws to the floor first) -> skip the FFTs, the build and
the push. First audible block re-enters next tick. A page-flip edge allows ~30 pushes so a fresh
panel never opens blank. Measured: UI build 54.2 -> 6.9, tick 15.2 -> 1.85 ms (58 Hz again),
message thread 72 -> 18.7, plugin process 72 -> 33.

**fb507 DECORATION CADENCE** (Max's stop/go complaint): __tiSlow/__tiDecoDue are now binary —
ACTIVE (pointer/keys recent OR notes sounding) = full paced rate, completely smooth; TRUE IDLE =
frozen (10 s heal). No more 2 fps stop-go while using the plugin.

Four states (fb507): synth-idle WV 16.1 / proc 34 · synth-play WV 116 / proc 96 · front-idle
18.6 / 29 · front-play 171 / 50. All gates green; fingerprint bit-identical.

NEXT (ranked): (1) play-state C++ ~96% — the spectrum (~47/s x 40 KB) and scope (30/s x 11 KB)
composes during REAL audio; Windows lever: 2 dp bins + 30 Hz spectrum push. (2) front-play 171%
— renderTerrain; push-clocking the hero is the planned fix. (3) LFO/env push-paint migration
(the campaign's smoothness half; PLL deletion). (4) editor open 5-10 s.

UNIVERSALITY (Max's requirement — no machine-specific tuning): every fix keys on BEHAVIOUR, not
this machine: pacing compares timestamps against 60 fps (no-op on 60 Hz panels, caps 144/240/360
alike); the arbiter keys on input/notes; the silence gate keys on audibility; __WIN_LITE and the
arbiter key on the Windows UA, never on hardware. Nothing reads 360 anywhere. Mac path untouched
and byte-identical (UA-gated) — a Mac-to-Mac transfer behaves as before; ProMotion (120 Hz)
MacBooks would eventually benefit from the same push-clock migration, but that is a Mac-session
decision.

### fb508 — THE NO-FREEZE DESIGN: rest poses, not parks. And the FL frame-drop mechanism named.

Max rejected the fb504-507 inactivity freeze ("the plugin freezes mid-decay... looks like it's
lagging... I absolutely hate it") and specified the Mac semantics: reactive viz DECAYS TO A REST
STATE when audio stops (never freezes mid-motion), loopers keep looping, and only invisible work
may pause. Also confirmed working: the fb506 LFO retrigger fix ("perfect now") and the envelope.

**THE FL FRAME-DROP MECHANISM, finally named.** Max observed: front page = HIGHER CPU but FL
stays smooth; synth page = lower CPU but FL's master meter drops frames and window-drags lag.
Because the editor's 60 Hz timerCallback runs ON FL'S UI THREAD: the synth page's spectrum
string build stretched ticks to 14-15 ms — that stall IS the frame drop. WebView2's CPU lives in
separate processes and never blocks FL. **Total CPU was never the FL symptom; tick length is.**
The probe's `ms/tick` is the number to watch for FL smoothness.

Shipped (fb508d):
1. isActive() is always true — VISIBLE animation never parks. `__tiQuiet` (no input+no notes
   1.5 s) replaces `__tiParked` on the 7 interval gates — it gates only INVISIBLE work.
2. Pacing now covers every page (60 fps cap vs the 360 Hz panel).
3. SMIL scrubber: FLOW emblems move again — paused from the panel clock, advanced by hand via
   svg.setCurrentTime() on the paced clock. ⚠️ scrub ONLY '.flow-mode svg': scrubbing every
   svg with an <animate> DOUBLED page cost (116 -> 225, measured).
4. Infinite decorative CSS animations STAY dead — they tick at compositor/panel rate and cannot
   be paced; re-enabling them cost ~+100 (measured). Rebuild wanted ones as paced JS tweens.
5. THE REST POSE (front page): while notes/input, full animation; after the last note the hero,
   reels and icons EASE to stillness over 1.5 s, draw one rest frame, and stop burning; fake
   meters FALL to zero instead of jittering on Math.random forever. First note resumes instantly.
6. Spectrum build 30 -> 15 Hz + 2 dp bins (never shares a tick with the scope build).

Four states (WebView2 / plugin proc): front-idle 94.6/29 · front-play 198/57 · synth-idle 92/30
· synth-play 112/81. Versus the saga's start (editor open idle: 171.6 + freeze-free semantics
impossible), everything now animates continuously AND costs half. Contract v2 7/7, eq 15/15,
fx4 130/130, fx3 48, lfo smooth, fingerprint bit-identical. Installed.

NEXT, ranked: (1) 🚨 the play-state build is STILL ~55% [UI build] at 42 Hz ticks — the 15 Hz
spectrum did NOT explain it; something else in timerCallback scales with play. Extend the fb501
meter to PER-SEGMENT timings, then likely move whale builds to a background composer thread
(timerCallback must never exceed ~2 ms on FL's UI thread). (2) front-play ~198 — hero at paced
60 during notes; options: 30 fps under notes, or the push-clock migration. (3) rest poses for
the remaining front loopers (fxAnimate tape viz, delay comet). (4) LFO/env push-paint migration.
(5) editor open 5-10 s.

### fb509 — the per-segment meter caught the real whale: juce::String FORMATTING

The play-state tick was guessed at twice (spectrum cadence, decimal places) and both guesses
missed. A 7-bucket per-segment meter (SEGM marks in timerCallback; probe line "SEG pre|mv|fol|
scope|save|eq|geo") named it in one run:
    eq 32.1% + scope 17.8% of one core — AT 15 Hz / 30 Hz. ~21 ms per spectrum build.
Not the FFTs. Not the cadence. **juce::String: one heap-allocated String object per float plus a
+= that re-walks the buffer, thousands per frame.** Rewritten as snprintf into one reserved
std::vector<char>, converted once at the end:
    scope 17.8 -> 0.9   eq 32.1 -> 2.3   UI build 53.7 -> 10.8   tick 13.5 -> 3.4 ms @ 58 Hz
    plugin process during synth-page play: ~90 -> ~46
LAW: never build a push segment with juce::String appends — snprintf into a char buffer, wrap once.

Also this round:
- ARP tile: it was the one FLOW tile animated by CSS (others are SMIL, hence the scrubber moved
  3 of 4). Re-enabling its four CSS tweens cost ~108% of a core, A/B-measured (88 -> 196) — CSS
  animations tick at the 360 Hz compositor and cannot be paced. Rebuilt as a byte-faithful JS
  tween (arpTick) on the paced clock, riding the SMIL scrubber loop. All four modes now move.
- LFO "Free acts like retrigger at the 8-bar loop": NOT a bug and NOT Windows. A tempo-SYNCED
  LFO locks phase to host transport (PluginProcessor.cpp:9115 setPhaseFromTransport, "locks to
  bar + arp clock"), so a loop wrap audibly resets it — identical code on Mac. If Free+sync
  should free-run through loop wraps, that is a cross-platform DSP design decision for the Mac
  session; do not change unilaterally.

Four states (WV / proc): synth-idle 96.5/33 · synth-play 124/46 · front-idle 103/30 ·
front-play 198/37. Everything animates continuously. All gates green; fingerprint bit-identical;
installed.

NEXT: (1) front-play ~198 — the hero at paced 60 during notes (push-clock migration or 30 fps
under notes). (2) the push-paint migration proper (LFO playhead first — deletes the PLL). (3)
ship lane 9-10% [ship] — emitEvent hop; possibly batch. (4) editor open 5-10 s.

### fb510 — hero 30 fps under notes · front loopers rest-posed · the 94-type model CACHED

Max confirms: **FL frame drops are SOLVED** (the fb509 tick fix), animations behave, rest poses
feel right. Remaining mission: the window's +10-15 machine-% while 5 instances play.

Shipped:
1. HERO at 30 fps while animating (alternator inside the rest-pose block; the final rest frame
   always draws). Front-play measured 198 -> 150 in the same epoch.
2. Front loopers rest-posed like the hero: fxAnimate's tape mech + space viz ease to a stop over
   1.5 s of quiet (a stopped tape LOOKS stopped), one final frame, sleep; delay-grid comet rides
   the hero's rest factor. Mac path untouched (all gated on __tiQuiet existing).
3. THE MODEL-CURVE CACHE (from the recon agent's exact-line plan): drawInto caches fAtX
   frequencies + raw mag() power per canvas, keyed NUMERICALLY on (ti, fc, res, drv, slot, N, W)
   — fc/res are post-liveFc/liveRes, so modulated cutoff rebuilds every frame BY CONSTRUCTION
   (curve-mirrors-DSP law); MAG_ANIM whitelists the 22 t-reading types which bypass the cache so
   their idle motion never freezes; breath + audio ripple applied per frame to cached power.
   PROOF: Tests/flt_smooth.js staleFrames=0 (curve updates every frame through a sweep).
   ⚠️ any future CAT[] type whose mag() branch reads t MUST be added to MAG_ANIM.

MEASUREMENT REALITY: this machine's thermal epochs now swing single-shot runs by ±30% (front-play
read 150 and 218 for the same front-page code an hour apart). Deltas under ~40 points need
counterbalanced pairs; the four-state snapshot below is one epoch, order T-synth-idle first:
    synth-idle 98.6/32 · synth-play 134/48 · front-idle 78/29 · front-play 218/44 (outlier vs
    the 150 measured pre-cache in a cooler epoch — front-page code identical, treat 150-218 as
    the honest range).

THE REMAINING TAIL (why idle pages still read ~80-100, not ~20): a long tail of always-running
loops each doing full redraws at paced 60 — the rack card drivers (granular/tape card
clearRect+repaint per card per frame, inventory #3/#4), fxKnob draws (ungated since fb502's
revert), noise-viz, hvRaf, RR/SWAY when visible. No single item is big; together they are the
floor. The architectural finisher remains THE PUSH-PAINT MIGRATION (approved by Max): widgets
paint from the C++ push, rAF loops get deleted one by one — LFO playhead first (deletes the
fb493 PLL), then env, then the per-card feeds. That is the path to "the window costs ~nothing
unless the DSP is doing something".

### fb511 — THE PUSH-PAINT MIGRATION SHIPPED. 22 loops re-clocked onto the C++ push; the page reaches TRUE idle.

The approved architecture, delivered end-to-end via a 5-agent recon fleet whose exact-string
patches (48 edits) were machine-applied (scratchpad apply-plan.ps1 idiom — every anchor verified
unique before touching the file):
1. **The dispatcher**: every shipped C++ frame ends with `;window.__tiFrame&&window.__tiFrame();`
   (appended BEFORE the FNV hash, so idle-skip semantics hold). Page side: __tiFrameReg(name,fn)
   registry; a push marks pending and arms ONE paced rAF — **push arms, vsync paints** (running
   painters synchronously inside the push eval measured +32 during play: the message-event
   timeline misalignment tax again). When the push lane is DARK >700 ms (browser preview, popped
   cards, editor closed) a rAF fallback chain becomes the clock at full rate; the C++ keepalive
   stamps `__tiAlive` so an idle-skipping (alive but silent) lane never triggers it.
2. **Migrated onto the push clock, rAF self-loops deleted**: LFO card (the fb493/fb506 PLL is
   DELETED — the dot paints the DSP's pushed phase, retrigger correct by construction), env
   editor, all nine FX-rack card drivers, filter analyzer, harmonic viz, noise viz, topo, RR
   wander, SWAY, underline/comet, animate() (front master, rest poses intact), fxAnimate().
3. **True idle**: at ~1.5 s of inaudible output the C++ stops appending the modViz + synthLfo1
   segments -> the frame goes BYTE-IDENTICAL -> idle-skip ships nothing -> painters never run.
   The LFO dot parks at quiet (fb505 rest-pose semantics). First audible block resumes all.
4. Emblem scrub + arp tween at 30 fps (they were most of the idle residual).

Counterbalanced (pairs.ps1), pre-migration vs migrated: synth+play 130.6 -> 119.7 WV;
synth idle 90.7 -> 75.5 WV and **plugin proc 31.3 -> 22.3** (the composer truly sleeps).
Gates: eq 15/15, fx4 130/130, fx3 48, staleFrames 0, contract 7/7, lfo smooth, fingerprint
bit-identical. Installed.

TWO BUGS THE GATES CAUGHT during application (kept for the record): one agent registered via
`__tiFrame.reg(...)` instead of `__tiFrameReg(...)` (TypeError killed the underline module —
15 assertions failed); and the first fallback lane ran at 4 Hz, starving the pushless test
browser (staleFrames 36). Both fixed; the gate suite is the reason this migration is safe.

REMAINING (the honest tail): idle WV ~70 = the 30 fps emblem motion + smilLoop chain + WebView2
baseline + unmigrated one-shots; play WV ~120 = real reactive painting (scope/analyzer/LFO at
60). Next candidates if more is wanted: emblem motion rest-posed at quiet (Max said loopers
loop — his call), remaining setIntervals, the ship lane, editor-open time (5-10 s).
