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

## fb517 (2026-08-26, Mac session) — WHAT JUST LANDED, all verified on the installed binaries

- **THE LFO OBEYS THE TRIGGER.** `:9121` had the global `flowLfo_` bank hard-forced Free (fb228
  mirror contract) while fb231 taught only the viz to stop — Max's "always stuck on free mode".
  Now: Trig resets on every host note-on and freezes in silence; Env one-shots and pins; Free is
  fingerprint-identical (sumAbs 3.514103689e+04). `Tests/lfo_trigger_cert.cpp` (AU-level): FAILED
  3 gates against fb516, **PASS 9/0 on fb517** — Env stasis 195.83→0.40 dB, Trig corr −0.302→1.000,
  rack-Cut late 0.0922→0.0016. MONO still collapses to Free upstream (pool law).
- **MEMORY:** per-layer stem arming (`ensureStemLayerAllocated`; one loaded sample = **+423 MB not
  +1,009**; master ring rides the first arm; prepareToPlay re-arms only the armed set) + HARM
  lazy-arm (fb498 clone, `harmReady_`, gate on LEVEL; **−65 MB every ctor**). laneD audit:
  instance-no-UI is **490 MB**; the "2.2 GB" = editor(+220) + all-stems(+1,009) + DAW baseline;
  Windows' 925 MB is working-set accounting. auval battery peak **9,222 → 9,120 MB**.
- **GLITCH Chance: WORKS AS SPECED** (`Tests/glitch_chance_cert.cpp` 14/14, mutation-proven):
  fires = knob % exactly; "sometimes works" = Deja Vu dice-lock (38% dead travel at dv=1),
  Drop masking (−35 dB), Repeat-on-stationary-audio inaudibility. Real bug found+fixed: mix=0
  leaked ~2.5 ms wet on the FIRST fire (mixSm_ snap in setMix; FlowGlitch_test 68/68).
- **UNDERLINES:** fb453 already covered all 16 rack kinds (184/184 marks measured) — the real
  defects were the attenuator's missing HORIZONTAL clamp and full-word anchoring on clip-trimmed
  marks (26.7 px adrift). Fixed; `Tests/fxmod_underline.js` 15/15 (4 mutations prove teeth).
- Suites: eq 15/0 · fx3 48/0 · fx4 131/0 · fxmod_menu 19/0 · fxmod_underline 15/0. pluginval ×2
  SUCCESS. `au_blk_cpu`: blk-45 3.13–3.35%, slope 1.54–1.77 across 5 runs — **the slope metric
  has ±0.15 run-to-run noise** (two settled runs read 1.77 then 1.57); best settled = FLAT ENOUGH,
  indistinguishable from fb516. Consider averaging N runs inside the harness.
- **QUEUED memory cuts (laneD's ranked plan):** FX-pool arm-on-adoption (Bode 55 + Delay 48 +
  Convolution 21 + grain 20 ≈ −144 MB — also fixes `FilterSlot::prepare` allocating **72.6 MB on
  the HAL I/O thread**, PluginProcessor.cpp:9336-9340) · FilterSlot diet (~200 MB voice pool) ·
  modal arm per-SELECTED-osc · capture-ring arm on export-UI visit · WT bank LRU.

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
   **Mac re-verified at fb516 (2026-08-26), fb515 + a stamp bump only:** auval battery peak
   **11,646 MB (fb497) → 9,222 MB** — the fb498 modal arm landing on macOS. `au_blk_cpu`:
   blk-45 **3.05%**, slope **1.54×**, "FLAT ENOUGH". pluginval strictness 5 both formats,
   auval exit 0, UI suites eq 15/0 · fx4 131/0 · fx3 48/0 · fxmod 19/0. Of the two ratchet
   halves above, the **ModalEngine ctor half is DONE (fb498)**; `releaseResources()` never
   freeing the rings is the half that remains.

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

### fb512 — HANDS-OFF PLAYBACK PACES TO 30, DONE RIGHT. And the page/push levers, finally separated by measurement.

Max tested fb511 in a 6-instance FL session: **55% closed, 73% open**, and — his words — "plugin wide now,
NOT just the synth page." That uniformity is fb511 working (one clock everywhere); the residual +18 is the
**real reactive painting at 60 fps while notes sound**. He asked for the Serum move: 30 fps viz while hands-off,
instant 60 the moment you touch the window.

**THE MATRIX (counterbalanced, installed fb511 as the rate proxy, pairs2.ps1, clock-normalized).** Two levers
were separated for the first time — the PAGE paint cap (arbiter `__UI_FPS`) vs the C++ PUSH rate (`TERRAIN_UI_HZ`):

| play state | stock 60/60 | page-30 | cpp-30 | both-30 |
|---|---:|---:|---:|---:|
| **SYNTH** WV | 88 | **54 (-38%)** | 72 (-18%) | 50 (-43%) |
| SYNTH proc | 34 | 33 | 26 | 27 |
| **FRONT** WV | 230 | ~217 | ~223 | ~210 |

The verdict is two different physics, exactly as fb506c found:
- **SYNTH page is PAINT-BOUND** — halving the paint rate nearly halves WebView. The page cap is the dominant lever
  (it reaches the migrated painters AND the self-re-arming rAF loops — scope, wt-waterfall, spectral, redrawCurve —
  which a C++-rate change does NOT touch). cpp-30 adds only a little WV but cuts proc; not worth its C++-critical-path
  risk this round.
- **FRONT page is COMPOSITE-BOUND** — the hero `renderTerrain` GPU composite is a per-vsync price, RATE-IMMUNE:
  every lever landed inside the +-30% thermal band (clock swung 120-165%). **30 fps does nothing on the front page.**
  This is the same wall fb503/fb504 hit: "the only thing that moved it far was stopping every loop."

So fb512 ships the **PAGE-SIDE cap only** — big synth win, zero front regression, page-only, Mac byte-identical.

**DONE RIGHT (the fb511-session one-liner had two real bugs a recon pass caught):**
1. **The pacing accumulator advanced by a hardcoded `1000/60`**, so at a 30 fps target `lastFrame` drifted and the
   gate leaked back toward 60 — 30 was never actually 30. Fixed: accumulate by the *actual* period `1000/fps`.
2. **Pacing to 30 COMPOUNDED five call-count alternators to 15 fps** (hero renderTerrain fb510, topo fb487, filter
   analyzer fb492, reverb cells fb342, noise-viz poll) **and ran nine hard-coded per-call motion steps at HALF SPEED**
   (rr-wander, tape reels, LFO chaos `/60`, harmonic + cutoff/res followers, noise breath). That is the "looks laggy"
   Max has rejected every time. Fixed with a **single rate authority**: the arbiter publishes `window.__uiPaceFps`
   (60/30) and `window.__uiStepScale` (1.0/2.0) each paced frame; the five alternators now cap-at-30 (`skip only while
   __uiPaceFps > 45`, so they halve 60->30 but never 30->15), and the nine motion steps multiply by `__uiStepScale`
   so wall-clock speed holds at any pace. **Mac safety by construction:** both globals default to 60/1 on all platforms
   (set before the Windows-only early return), so on Mac every gate is a no-op and every step scale is x1 — the Mac
   path stays byte-identical.

New gate: `arbiter_contract.js` now asserts **hands-off playback paces to ~30 fps, not 60** (would have caught bug #1 —
it reads ~60 with the accumulator broken). All gates green: eq 15/15, fx4 130/130, fx3 48, flt staleFrames 0,
arbiter contract ALL PASSED (incl. the new 30fps check), lfo_val_smooth ALL PASSED, modal fingerprint 446f2e02c4dcb215
(bit-identical — JS-only change, DSP untouched), modal-live PASS.

**GROUND-TRUTH A/B (built fb512 auto-30 vs archived fb511, counterbalanced, -Play):**
    SYNTH: fb511 83.9 -> fb512 56.6 WV (-33%); normalized 135 -> 90; GPU 40 -> 25, renderer 37 -> 26; proc ~33 unchanged (4-run counterbalanced, clock ~160%)
    FRONT: fb511 219.5 -> fb512 200.0 WV (-9%, a small bonus -- non-hero front painters go 30 hands-off); normalized 298 -> 270; NO regression (4-run counterbalanced)

**THE HONEST CEILING — read before chasing the front page.** Max's 73 is almost certainly the FRONT/main page open
(its ~230% WV / 16 threads ~= +14%, + proc ~= the +18 he sees; the synth page open is only ~86% WV ~= +8%). fb512 does
NOT move the front number, because the front cost is the hero terrain's per-vsync GPU composite, which is rate-immune.
The next real front-page lever is the HERO ITSELF — its canvas resolution, per-frame paint-op count, and the
backdrop-filters/layers it composites through — NOT its frame rate (proven dead four times). That is a separate
campaign and it touches Max's "loopers keep looping / the hero is the identity visual" call, so it is his decision,
not a unilateral cut. Ranked next levers: (1) hero composite cost (front); (2) cpp-30 during hands-off play as a
follow-on (needs a page->native rate signal; ~extra 5-7% synth WV + ~20% proc); (3) editor-open 5-10 s.

### fb513 — THE FRONT PAGE'S WHALE WAS THE FAKE METERS. The hero mountain is innocent.

Max greenlit a hero campaign with an escalation ladder (optimize -> manual-only animations -> kill
the hero), convinced the "big ass interactive audio mountain" (+ grain/tape engines) was the cost.
Three exp-hook ladders in the real plugin (bisect.ps1, --play, front page) said otherwise:

**LADDER 1 (structure):** control 231 WV · hero canvas hidden 223 · glow fills off 225 · all mesh
strokes off 222 · hero at half resolution 224 · glows+strokes off 217 — **the terrain's entire op
inventory prices at <=14** — but 'front' painter unregistered **54** and +'fx' **26**. The whale was
inside animate(), not in renderTerrain's draw ops.

**LADDER 2 (function-by-function decomposition of animate()):** tapeMech viz off -6 · grainBreathe
CSS off -5 · animated icons off ~0 · xy-auto off ~0 · section icons off -5 · updateModulation off
~0 · **renderTerrain fully off (JS and all): -7** · **updateMeters off: 205 -> 71 (-134% of a
core; gpu 133->45, renderer 63->21, proc 37->29).**

**THE MECHANISM (law 3 in a costume):** `.meter-bar` carries `transition: height 0.08s` (CSS
~2601) and `updateMeters` (~23234) writes a new height every paced frame — every write RESTARTS a
compositor transition, and compositor animations tick at PANEL rate (360 Hz on this machine),
FOREVER, immune to rAF pacing, push rate, draw-op counts and canvas resolution. That is why every
rate/op/area lever measured as noise on the front page (fb502/fb503/fb512 included). And the meters
are `Math.random()` FAKE — not even audio. Two 12-px bars cost more than the entire mountain,
tape machine, and icon system combined.

**THE FIX (fb513):** `.meter-bar{transition:none !important}` appended to the Windows-only
ti-static-decor kill list (the fb505 block, UA-gated — Mac byte-identical). The JS already animates
the height every paced frame, so the bars move exactly as before; the redundant 360 Hz CSS
double-animation dies. (The RMS textContent relayout measured ~0 once the transition was dead — G3 vs G2 — so it is left untouched.)

**Validation ladder (shippable form, same epoch):** control 225.0 WV -> transition:none 72.7 (gpu 145.7->43.9, rend 70.9->23.7, proc 43.9->30.8) -> +text-frozen 71.9 (text adds nothing). The one CSS line captures the entire meters-off win with the bars still animating.

**GROUND-TRUTH A/B (built fb513 vs archived fb512, counterbalanced, --play):**
    FRONT: fb512 200.4 -> fb513 72.5 WV (-64%); gpu 132->44, renderer 61->24, proc 37->29; normalized 273 -> 116 (4-run counterbalanced)
    SYNTH: fb512 57.8 vs fb513 57.9 WV -- dead flat, exactly as predicted (updateMeters never runs with a panel open)

The hero stays. The loopers keep looping. Nothing visible changed.

REMAINING, ranked: (1) the residual front-play floor (~70 WV: renderTerrain+friends at the paced
rate — real but modest; the E/F ladders price each piece if more is wanted); (2) cpp-30 hands-off
follow-on (~extra 5-7% synth WV + ~20% proc; needs a page->native rate signal); (3) editor-open
5-10 s; (4) audit OTHER hosts/pages for the same restart-a-transition-per-frame anti-pattern
(grep `transition:` for properties JS writes per frame — only .meter-bar and a max-height (654,
not per-frame) exist today).

### fb514 — THE WINDOWS POLISH CAMPAIGN: six audited bugs, one commit. Keyboard locked out, reopen lands on your page, junk sizes healed forever, white menus, closed instances go quiet.

Max's list after living with fb513 (multi-agent audit, six specialists, every mechanism verified in
source before any edit):

1. **NEW INSTANCES OPENED SMALL.** FL replays a remembered junk size per plugin TYPE — typically
   533, EXACTLY the constrainer minimum — at attach and on re-shows. The fb176 sticky latch
   RATIFIED it: isDragging() is process-global across all Terrain instances in the DLL, so junk
   landing during any drag became "user intent"; after the 4 s heal window any re-delivery was
   adopted wholesale; fb96 had removed state persistence so the user's manual fix never outlived
   the instance. FIX: (a) the latch only accepts a drag OVER THIS EDITOR, and adopts host GROWS
   never shrinks; (b) Windows-only eternal clamp in resized(): any UNATTENDED shrink is re-asserted
   immediately, forever; (c) editorWidth now genuinely saved/restored in plugin state (reverses
   fb96, deliberately — the latch now guards what enters the atomic); (d) the stale "saved in
   state" comments made true.
2. **SPACEBAR TOGGLED THE LAST-CLICKED PILL** (FL transport stolen). Zero blur() existed in all
   34,896 lines — every clicked <button> kept DOM focus and Space/Enter re-activated it; five
   arrow divs even had explicit Enter/Space handlers. FIX: capture-phase key guard (Space, Enter,
   Tab, arrows preventDefault+stopPropagation on non-text targets — text inputs exempt so preset
   naming still types; Escape/Delete stay for the shaper + menus), pointerup focus-drop (nothing
   keeps focus after a click), select-blur-on-change, five keydown handlers deleted. NO key ever
   activates a Terrain control now, on any platform.
3. **REOPEN ALWAYS LANDED ON THE HERO + multi-second spike.** The page WAS remembered
   (processor uiPage atomic) but the only restore path — the pre-ready RESTORE push — ships on
   Windows over the terrainFrame web-message lane whose page-side listener registers ~6,500 lines
   AFTER the script that fires signalPageReady: pushes into a listenerless lane are silently
   dropped, every time. Mac was immune (evaluateJavascript needs no listener). FIX, three layers:
   (a) the boot URL carries ?page=N (uiPage>0 only — fresh instances byte-identical); (b) the page
   sniffs it next to __cardOnly and applies the panel AT PARSE TIME — the front page never paints,
   the hero painters never start, no flash, no reopen spike; (c) belt: signalPageReady pushes
   restoreUiPage via evaluateJavascript (listenerless-lane-proof; idempotent via _uiPageRestored).
   Also applied to the wd9 recovery reload.
4. **~5 s EDITOR OPEN.** Measured from inside the real plugin: the 34,896-line page parses in
   435 ms (domInteractive 399, first paint 428) — the page was never the 5 s. The real serial
   costs: WebView2 controller creation (serialized process-wide across instances), ~480 SEPARATE
   AddScriptToExecuteOnDocumentCreated COM calls (one per JUCE relay script), the 2.3 MB HTML
   string rebuilt on every navigation, and the pre-ready push storm. SHIPPED: getResource now
   caches the assembled HTML bytes keyed on the injected theme (wd9 reloads + same-process
   reopens skip the rebuild), and the ?page= boot (item 3) removes the hero boot cost entirely
   on reopen-to-panel.
   🚨 **FALSIFIED AND REVERTED — do not retry blind: joining the ~480 user scripts into ONE
   AddScriptToExecuteOnDocumentCreated call made open 4x SLOWER** — open-to-pageReady, 6 runs
   each, counterbalanced: stock 2,527-3,401 ms (mean 2,787) vs concatenated 11,648-12,373 ms
   (mean 11,938). Deterministic, zero overlap. Mechanism unproven (suspects: WebView2 slow path
   on a ~MB single AddScript payload; a relay script's "use strict" directive strictifying the
   whole combined block and forcing a slow recovery path) — if anyone re-attempts, bisect with
   chunked joins and measure open_ab.ps1 before believing anything. The patch file is back to
   the fb480 cap only.
   DEFERRED (documented options): WebView2 environment cache across opens (keeps browser
   processes warm; memory + DLL-unload risk), full editor keep-alive via controller re-parent
   (the Serum model; biggest win, biggest surgery), lazy page-boot module deferral.
5. **BLACK ENGINE MENUS.** Not Windows theming — OUR fb503 fix deliberately chose dark
   (select{color-scheme:dark} + #EDE7F5-on-#1B1526 options). Flipped: color-scheme:light,
   black-on-white options + optgroups. One CSS edit covers all ~35 native selects (engine pickers,
   loop modes, harmonic/modal families, WT presets, FM algo, FX rack type/character/quality, TIC
   steps). Mac unaffected (WKWebView renders native popups and ignores option CSS — fb503's own
   verified finding).
6. **PLAYLIST TEARING WITH 7 CLOSED INSTANCES.** The fb148 "no UI, no viz" law was ~60% enforced.
   The closed-instance leftovers, ranked: the processor's 60 Hz juce::Timer dispatching ON FL'S
   UI THREAD forever (7 instances = 420 dispatches/s on the exact thread FL paints the playlist
   with); ~85+ ungated audio-thread viz publishes per block (follower/modViz 96-voice walk, FX
   blooms, master scope ring per-sample stores, slice-glow dynamic_cast walk, stem capture meter).
   FIX: timer governor — 15 Hz when no UI client, snapped back to 60 in createEditor/adoptCard
   (arms/rebuilds happen INSIDE the tick that detects them; the --modal-live gate pumps 150 ms ≥ 2
   ticks and stays green); all five viz blocks gated on vizLive (pure UI feeds — DSP fingerprint
   bit-identical by construction). THE MOUNTAIN RE-CONFIRMED CLEAN: renderTerrain exists only
   inside an open editor's page; no processor-side hero work exists.
   Closed-instance measured: idle proc-total 7.36 -> 7.08 per instance in the bench (the structural win -- 60->15 Hz message dispatch, 420->105/s across 7 instances on FL UI thread -- is only measurable in a real FL session: Max tests the playlist scroll).
   DEFERRED (product decisions, Max's call): disarm the capture ring (~202 MB) and stem rings
   (~1 GB) when the last editor closes — both change "capture audio played while closed"; the
   per-instance stall-beacon thread → process-wide singleton.

Gates: eq 15/15, fx4 130/130, fx3 48, flt staleFrames 0, arbiter contract ALL (incl. the fb512 30fps check), lfo smooth PASS, modal fingerprint 446f2e02c4dcb215 BIT-IDENTICAL (the viz gates + governor changed zero audio; the recon agent caught that velVis_ feeds velocity->global mod and left it ungated), modal-live PASS at the 15 Hz governor. Front/synth play A/B sanity: front 73.1 vs 72.9, synth 58.9 vs 56.7 -- dead flat, fb512/fb513 wins fully preserved.

### fb515 — THE KEYBOARD GOES BACK TO THE DAW, and the page appears ONCE, fully dressed.

Max's fb514 test: keys no longer activate Terrain controls (good) but Space stopped reaching FL
entirely — "terrain latches my controls... I have to click somewhere else". Root cause, one level
deeper than fb514: on Windows the WebView2 child HWND takes REAL Win32 keyboard focus on any click
inside the page. fb514's capture-phase guard consumed the keystroke inside the page instead of
returning it. Mac never shows this because FL/mac never hands the WKWebView the keys at all
(fb134/fb135 — the whole editArm bridge exists because of it).

**THE FIX — do what clicking outside does, automatically:** a new `releaseKeysToHost` native
(PluginEditor.cpp, next to grabKeys): `SetFocus(GetAncestor(peerHWND, GA_ROOT))` — hands focus to
the host's top-level window. The page calls it (fb514 guard block) after every pointerup that is
not text editing and not an open `<select>` popup, after a select commits, and on focusout of a
text field. Standalone excluded on BOTH sides (its QWERTY→MIDI needs the focus); Mac never calls
it (UA gate). Net behavior: click anything in Terrain, press Space → FL plays. Type a preset name →
typing works; leave the field → keys return to FL.
⚠️ If FL still swallows Space with the plugin frame focused on some setup, the next escalation is
SetFocus to FL's MAIN window (walk GA_ROOTOWNER) — one-line change at the marked site.

**THE TRANSFORMER FIX (fb147 extended, Windows-only):** the reveal fired at load+zoom-settle
(~250 ms) while the C++ RESTORE/SAMPLE-RESYNC pushes kept dressing the page for ~another second —
the visible "transforming into itself". The reveal now also waits for fonts + ~400 ms past the
page-ready signal (`__tiPageReadyT` stamp), hard-capped at ~2.6 s (fb147's a-broken-boot-never-
stays-black law). One fade, fully dressed. Mac timing byte-identical.

Measured: open-to-pageReady 2,853 -> 2,704 ms (parity; the dressed reveal delays only the VISUAL fade, not readiness); front play 72.4 vs 72.3 WV (dead flat). Gates: eq 15/15, fx4 130/130, fx3 48, flt staleFrames 0, arbiter contract ALL, lfo smooth PASS, fingerprint 446f2e02c4dcb215 BIT-IDENTICAL, modal-live PASS.

**THE SERUM TRUE-INSTANT-REOPEN VERDICT (recorded for the next session):** feasible, not a
same-day change. Requires the audit's three-phase surgery: (A) extract the 474 relays + 247
native lambdas out of the editor into a processor-owned backend (they currently capture the
editor `this` — caching the WebView with dangling editor pointers = crash); (B) on editor close,
re-parent the WebView2 controller into a hidden processor-owned HWND (JUCE patch:
put_ParentWindow + association re-run) instead of destroying it; (C) on reopen, re-attach +
reset lastFrameHash_/wd9/zoom. Payoff: reopen goes from ~2.6 s to near-zero and the reopen CPU
spike disappears entirely (no re-parse, no push storm, no controller creation). Cost: ~150-250 MB
resident per cached page (LRU cap 1-2 mandatory with 7 instances), plus the biggest JUCE-patch
surface so far. The environment-cache half-step (keep the browser process warm, still re-create
the controller) is smaller but was NOT measured this round; the script-concat lesson (fb514: a
"sure win" measured 4x WORSE) says measure it before believing it.

### THE MAC -> WINDOWS FLOW (for the preset-menu / terrain-patcher era)

Max builds on the Mac; Windows stays ready like this:
1. **Code flows through git.** Mac work lands on its branch; merge into `windows-test` and push —
   CI (terrain-windows.yml, windows-2022) builds every push and produces the VST3 artifact. On
   the Windows box: `git fetch && git reset --hard origin/windows-test`, then
   `scripts\windows-quickstart.ps1` (applies the JUCE patch, fetches the WebView2 SDK, builds)
   or grab the CI artifact straight into `C:\Program Files\Common Files\VST3\`.
2. **Presets are data, not code.** They live in the user preset dir the save/load natives use —
   copy the preset folder from the Mac to the same location on Windows (or ship factory presets
   inside the repo so the build carries them). The preset MENU is index.html code — it flows with
   git like everything else.
3. **Before trusting a merged build on Windows, run the gate suite** (scratchpad gates.sh idiom:
   ui_syntax, eq_ui 15, fx4 130, fx3 48, flt_smooth staleFrames 0, arbiter_contract — including
   the fb512 hands-off-30fps check — lfo_val_smooth, `terrain_winbench --modal` fingerprint +
   `--modal-live`). They run on any Windows box with Chrome + node; NODE_PATH needs a
   puppeteer-core node_modules.
4. **New UI work lands paced by construction** if it follows the laws in this file: painters ride
   `__tiFrameReg` (never a raF self-loop), per-frame motion multiplies `window.__uiStepScale`,
   half-rate hacks gate on `__uiPaceFps>45`, no infinite CSS/SMIL animation, no per-frame writes
   into a CSS `transition`, push segments built with snprintf not juce::String. A Mac-built
   preset menu that follows those is Windows-clean on arrival; anything that doesn't will show up
   in pairs2.ps1 within an hour of measurement.
5. **CPU regressions are caught by the counterbalanced harnesses** in the session scratchpads
   (pairs2.ps1 A/B, open_ab.ps1 open latency, closed_ab.ps1 background diet, bisect.ps1 exp-hook
   single runs) — re-create them from this file if the scratchpads are gone.

### fb518 — SERUM-CLASS INSTANT REOPEN, measured: 1,789 ms → 83 ms (21×). The UI core survives editor close.
(Authored in parallel with the Mac session's fb516/fb517 — the code and JUCE-patch comments carry the fb516 tag; the ledger number is fb518.)

Max's final Windows ask: "true loading instant serum 2 style." The acceptance harness landed
BEFORE the feature (fb516a, `terrain_winbench --reopen`): baseline OPEN1 3,545 ms / REOPEN
1,789 ms — every open a full WebView2 controller creation + 2.3 MB parse + restore storm.

**THE ARCHITECTURE (rename-not-extract, ~150 lines moved + ~300 added over four files + a
3-file JUCE patch):**
- `TerrainUiCore` — the ENTIRE old editor class renamed (WebView2 + ~805 relays + ~771
  attachments + 248 natives + all push/restore/watchdog state, member-order contract preserved).
  Owned by the PROCESSOR (the fb83 card-window idiom), it outlives every editor.
- A thin `TerrainInstrumentAudioProcessorEditor` shell adopts the core on open and keeps only
  what is genuinely the window's: resize limits, the fb103/fb176/fb514 junk-size war (driven by
  the core's timer via tickSizeHeal), editorWidth memory.
- Parked page physics: renderer suspended (put_IsVisible(false) → rAF stopped, ~0 CPU measured)
  while Component::isVisible() stays TRUE → every relay push keeps flowing → THE PAGE ABSORBS
  AUTOMATION THE WHOLE TIME IT IS PARKED. Reopen = re-parent + timer restart + resyncAfterReattach
  (sample waveforms/blends). No reparse, no restore storm, no hero, no transformer.
- Census law intact (uiClients_ moves in attach/detach → fb148/fb514 audio-thread + 15 Hz
  governor hold). LRU cap: process-wide, default 1 parked core, TERRAIN_UI_CACHE=0..4; 0 = off =
  cold path (the escape hatch, harness-verified). Mac: parking disabled (destroy-on-close, a
  Mac-session decision to enable).

**THE FALSIFICATION TRAIL (four designs died by measurement — kept so nobody retries them):**
1. Park into a JUCE TopLevelWindow holder → put_ParentWindow = 0x8007139F. Instrumented logging
   revealed `IsWindow(old)=0`: THE CONTROLLER WAS ALREADY DEAD — its parent HWND had been
   destroyed before ANY of our code ran.
2. Park in the shell dtor → too late. Park on Component::visibilityChanged → wrong hook (own
   flag only). Park via ComponentMovementWatcher showing-change → STILL too late in the bench.
   Root laws, both measured: **JUCE TopLevelWindows DESTROY their HWND on setVisible(false)**,
   and **every JUCE notification about a peer's death fires AFTER the HWND is destroyed.** A
   WebView2 controller dies with its parent HWND, unrecoverably.
3. Raw ::SetParent rescue of the chrome child after death → impossible (children die with the
   parent).
4. THE DESIGN THAT WON: a **raw Win32 hidden window** (TerrainUiPark.cpp — the only TU that
   includes windows.h; no JUCE visibility semantics can kill it) + an **explicit
   `webView->parkNativeWindow(rawHwnd)`** JUCE API called (a) from the graceful hide path
   (ComponentMovementWatcher on the shell — fires synchronously INSIDE the hide call, HWND
   alive) and (b) from a **WM_DESTROY subclass on the editor's peer** (a parent's WM_DESTROY
   runs before its children are destroyed — the classic child rescue; host-independent
   backstop). Adopt-side re-parent rides the fb516 JUCE peer-follow patch (raw → new editor
   peer, both alive, hr=0).

**MEASURED (the fb516a harness):** OPEN1 ~3.1 s (unchanged, cold) · **REOPEN 77 ms** · parked
page CPU ≈ 0 (renderer suspended) · TERRAIN_UI_CACHE=0 → cold reopen 1,355 ms (escape hatch
works) · play states flat vs fb515 (front 86.7/84.9, synth 70.9/60.8 same-epoch). Suite:
keep-alive ON: OPEN1 3,219 ms / REOPEN 83 ms / hidden-phase settles from ~6% (first seconds after close, page decaying to rest) toward the true-idle floor; TERRAIN_UI_CACHE=0: cold reopen 1,660 ms, ws drops on close (core destroyed) -- the escape hatch is real; play sanity vs fb515: front 79.4 vs 80.3, synth 66.2 vs 61.8 -- flat. Gates: eq 15/15, fx4 130/130, fx3 48, flt staleFrames 0, arbiter contract ALL (incl. the fb512 30fps check), lfo smooth PASS, modal fingerprint 446f2e02c4dcb215 BIT-IDENTICAL, modal-live PASS -- all on the production (diagnostics-stripped) build.

REMAINING TAIL: first-open cost unchanged (~2.6-3.5 s — controller creation + parse; env-cache
and lazy-boot options documented in fb514/fb515 notes); LRU eviction across 3+ instances is
manual-tested in FL; Mac parking off pending a Mac session; the unused uiCoreHolder_ member and
the fb516b/c/d intermediate shell hooks (onShowingChanged + ShowingWatch) are LOAD-BEARING for
the graceful path — do not remove.

### fb519 — EVERY instance keeps its parked UI. All reopens are the 84 ms one.

Max's verdict after living with fb518's cap of 1: only the LAST-closed instance was instant; the
others fell back to the cold "transformer" open. Fix: the parked-core LRU default goes 1 → 8
(clamp 0..16 via TERRAIN_UI_CACHE; 0 still = feature off / cold path). Honest cost, accepted
deliberately: ~150-250 MB of visibility-suspended renderer per parked instance (~1-1.5 GB across
a 7-instance session); parked CPU stays ~0.

New acceptance mode `terrain_winbench --reopen2` (two instances, interleaved): default cap →
OPEN A1 3,078 / B1 1,757 ms (cold; note the second instance's FIRST open is already ~half — warm
browser process), REOPEN A2 88 / B2 83 ms — **BOTH INSTANT**. TERRAIN_UI_CACHE=1 → A2 1,543 ms
(oldest evicted, cold) / B2 91 ms — the eviction machinery verified alive. Full gates green,
fingerprint 446f2e02c4dcb215 bit-identical.
