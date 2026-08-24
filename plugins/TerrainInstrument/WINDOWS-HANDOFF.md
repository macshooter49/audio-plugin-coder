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

1. **Memory — landed but NOT yet verified by a build.** Stem rings (−1,009 MB), capture ring
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
