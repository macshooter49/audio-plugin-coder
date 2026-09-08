# Granular Oscillator Engine — v1 Design / Scope

**Status:** scoped, ready to plan → build. Brainstormed 2026-07-02.
**Engine slot:** `Engine::GRAN = 2` (already stubbed everywhere — see §8).
**One-liner:** The third live oscillator engine (Wavetable / Sample / **Granular**). It granulates the **same loaded sample buffer** the Sample engine uses. Reuses the entire sample UI shell; only the **bottom control row, the DSP, and the follower** change.

> Design principle (Max): **all-original DSP and naming.** No boilerplate granular lib, no copied labels. The math below is grounded in real granular research and reworked for Terrain. Serum 2 is *calibration only*.

---

## 1. Locked decisions (from the brainstorm)

| # | Decision | Choice |
|---|----------|--------|
| 1 | **Flagship** | **Living + In-key granular** — a cloud that evolves on its own (`Life` weather macro) and never plays a wrong note (`Key` snap). Both reuse shipped code, both are **zero new viz**. |
| 2 | **Default feel** (drop a sample, touch nothing) | **Slow drift** — `Scan` defaults to a gentle forward crawl (the drum-loop→pad gesture happens instantly). Freeze = `Scan→0`. |
| 3 | **Controls** | Bottom row = **6 reassignable slots**. Default lineup: **`Scan · Density · Size · Spray · Shape · Key`**. Every function is a right-click away. |
| 4 | **Follower / viz** | Keep it minimal: thin-white **grain-dot scatter** around a scan marker. No circular clouds, no new panels. (Terrain-original — Serum has no follower.) |
| 5 | **Transient-snap** | **ON by default** — grains snap to nearest transient so breakbeats give clean hits, not smeared double-attacks. |
| 6 | **Unison** | Per-osc, **default low (1–2)** + a hard grain cap. A cloud already fills the field via Spray/Width. |
| 7 | **Source** | **Single loaded buffer per osc** for v1 (matches Sample exactly). |
| 8 | **Keep as-is** | Region start/end handles, OCT/SEM/FIN, copy-paste, slicing/chop. |
| 9 | **A/B dual-value knobs** | **Deferred to v2** — it's a distinct global system (every knob + mod matrix). |

**Parked for later (do NOT build in v1):**
- **Terrain-contour flow-field** ("grains ride a carved landscape") — the only idea that costs real viz. Strong v2 hero.
- **Multi-source morph** ("Terrain Morph Pad" — blend 2–4 buffers across the field) — pairs with the contour.
- **A/B scene knobs** (see #9).

---

## 2. What granular *is*, in plain DSP terms

A **grain** = a short, amplitude-windowed slice of the loaded buffer. We spawn a stream of overlapping grains; the **read-head (Scan) is decoupled from pitch** — that decoupling is the entire point (freeze a moment, or crawl a loop into a pad, at any pitch). Grains are windowed *by construction*, so grain start/stop **physically cannot click** — this is actually *simpler* than the Sample engine's loop-crossfade declick (`SampleEngine.h:490-582`), not more work.

---

## 3. DSP model (all-original, grounded)

New file **`Source/GranularEngine.h`** (`namespace tw`), mirroring `tw::SampleEngine`'s contract: borrowed `const float**` buffer view, `prepare / setSample / noteOn / noteOff / renderBlock`. Reuse `readHermite` interpolation from `SampleEngine.h:586-606` (do not reinvent interpolation).

### 3.1 Grain pool (no allocation in the audio thread)
```
struct Grain {
    double readPos;      // fractional sample index into the buffer
    double readInc;      // per-sample advance = pitchRatio * (dir)
    int    age;          // samples elapsed
    int    len;          // grain length in samples
    float  gain;         // per-grain level (level-spray applied at birth)
    float  panL, panR;   // equal-power pan (pan-spray at birth)
    const float* win;    // pointer into the precomputed window LUT for this grain
    bool   active;
};
Grain pool[kGrainPoolPerEngine];   // 64 per engine (research ceiling: GR-1 128/voice, Quanta ~100, Arbhar 88)
```
`kGrainPoolPerEngine = 64`. Expose a **Max-Grains cap** so dense clouds *skip-and-wait* (degrade gracefully) instead of glitching — Serum does exactly this ("Jump Start / Max Grains", manual p.101).

### 3.2 Master read-head vs grain-birth (the decoupling)
- A per-voice `scanPos` (0..1 across the region) advances each block by `scanRate` derived from the **Scan** param. **`Scan = 0` → frozen** (scanPos holds); `Scan < 0` → reverse. Range switch ±200/400/800% (right-click deep-option) mirrors Serum.
- Each **new grain is born at** `birth = clamp(region, scanPos + Position + spray·rand())`, then reads *independently* at its own `pitchRatio` for its whole life. Freeze = scanRate 0 while grains keep spraying from the held slice → the "living freeze."

### 3.3 Async scheduler (stochastic onset is mandatory)
- Fractional `nextGrainCountdown` (samples). Each sample: decrement; when `≤ 0`, spawn into the first free pool slot and reset countdown to `outputRate / density`.
- **Jitter the interval** by `±regularity · interval` (xorshift32, reuse the SPRAY RNG at `SampleEngine.h:145-152`). Periodic spawning sounds buzzy/pitched; jittered spawning smears into texture (core granular theory). Decorrelate per-unison-voice seeds via the existing Murmur3 avalanche seed (WAVER).

### 3.4 Window (a *primary* timbral control)
- Precompute 2–4 window LUTs at `prepare()`. `Shape` knob **morphs Tukey (flat-top, tapered edges → percussive/present) ↔ Gaussian (bell → soft/pad)**. `Skew` warps window asymmetry: `−` = pluck (fast attack/slow decay), `+` = swell.
- Per-grain lookup is one indexed read. (Serum ships 10 discrete shapes; we morph continuously + optionally expose a shape menu as a deep-option. Renamed, reworked.)

### 3.5 Per-sample mix + retire
```
for each active grain g:
    float w   = g.win[ (g.age * winLen) / g.len ];      // windowed amplitude
    float s   = readHermite(buffer, g.readPos);
    outL += w * s * g.gain * g.panL;
    outR += w * s * g.gain * g.panR;
    g.readPos += g.readInc;  g.age++;
    if (g.age >= g.len) g.active = false;                // retire, no click (window→0 at edges)
```
Sum then RMS-normalize like the sample unison path (`SynthVoice.h:3198-3217`).

### 3.6 Default ranges
- **Size** 2–500 ms (log), default ~**80 ms**. Below ~10 ms grain-rate becomes audio-rate/pitched; above ~200 ms = smear.
- **Density** 1–200 g/s (log), default ~**40** (≈3× overlap at 80 ms → continuous).
- **Overlap** target 2–4× at defaults; pool sized so `overlap × density < 64`.
- **Scan** default small **+** (slow drift); bipolar, 0 = freeze.

---

## 4. Control set

### 4.1 The 6 default slots
`Scan · Density · Size · Spray · Shape · Key`
(PAN/LEVEL are intentionally **not** here — they're already the per-osc right-click quick-controls, so grain slots aren't wasted on them.)

### 4.2 Full function menu (any slot → any function via right-click)
Every function is a **permanent APVTS param** (so all are mod-matrix targets whether shown or not). Slots are UI views that pick which param's knob to render.

| Group | Function | Param (per osc A–D) | Range / default | DSP |
|-------|----------|---------------------|-----------------|-----|
| Motion | **Position** | `GRAN_POS` | 0..1 / 0 | grain-birth anchor / mod target |
| Motion | **Scan** ✔ | `GRAN_SCAN` | −1..+1 / **+0.15** | read-head rate, 0=freeze, −=reverse |
| Cloud | **Density** ✔ | `GRAN_DENSITY` | 0..1→1–200 g/s / ~40 | scheduler spawn rate |
| Cloud | **Size** ✔ | `GRAN_SIZE` | 0..1→2–500 ms / ~80 ms | grain length |
| Cloud | **Spray** ✔ | `GRAN_SPRAY` | 0..1 / 0.10 | grain-birth position jitter |
| Pitch | **Pitch** | `GRAN_PITCH` | −24..+24 st / 0 | base grain transpose |
| Pitch | **Pitch Spray** | `GRAN_PITCHSPRAY` | 0..1 / 0 | per-grain pitch scatter |
| Pitch | **Key** ✔ ⭐ | `GRAN_KEY` | choice / Off | in-key snap: Off/Oct/5th/Chord-Follow/Maj/Min/Penta |
| Character | **Shape** ✔ | `GRAN_SHAPE` | 0..1 / 0.5 | window morph Tukey↔Gaussian |
| Character | **Skew** | `GRAN_SKEW` | −1..+1 / 0 | window asymmetry pluck↔swell |
| Character | **Direction** | `GRAN_DIR` | −1..+1 / +1 | per-grain fwd↔rev bias (−1 all-rev, 0 random) |
| Space | **Width** | `GRAN_WIDTH` | 0..1 / 0 | per-grain pan spread |
| Life | **Life** ⭐ | `GRAN_LIFE` | 0..1 / 0.15 | weather macro → bounded OU drift on density/size/spray/pitch |
| Life | **Jump** | `GRAN_JUMP` | 0..1 / 0.5 | onset: soft-build ↔ instant full density |

✔ = default-slot. ⭐ = flagship.

**Choice / deep-option params** (right-click ⚙ on the *current* function, not slot-reassign):
`GRAN_DENSITY_MODE` (Free-Hz / BPM / constant-overlap "Grains") · `GRAN_SIZE_MODE` (ms / BPM / % of period) · `GRAN_SCAN_RANGE` (±200/400/800%) · `GRAN_SCAN_KEYTRACK` (bool) · window shape menu.

**Slot assignment** = per-osc UI state, persisted alongside the reopen-state (`currentActivePanel` pattern) and pushed on `signalPageReady`. Not an APVTS param.

### 4.3 Two-layer right-click
1. **Reassign slot** → the function menu (§4.2).
2. **⚙ Options** → deep-options for the function currently in that slot.

---

## 5. Keep / drop / change vs the Sample engine

| Sample feature | v1 decision | Why |
|----------------|-------------|-----|
| Top-bar loop-mode selector (One-Shot/Fwd/Rev/PingPong/Tailed) | **Replace** | Single-playhead concept. Granular's motion IS `Scan` (bipolar) + region ends. Selector becomes a small **Freeze/Scan mode** affordance. |
| Reverse | **Adapt** → per-grain `Direction` | Grain-level reverse keeps timbre/place, adds the "breath." More useful than whole-buffer flip. |
| Ping-pong | **Adapt** → a Scan mode (read-head bounces region ends) | Distinct from per-grain reverse. |
| Chop / Snap (Off/Zero-cross/Transient) | **Keep + finish** | Transient-snap is *the* drum-loop feature. Precompute transient markers on load (the Sample engine's transient snap is a TODO at `SampleEngine.h:313-330`). |
| Region start/end handles | **Keep as-is** | Bound where grains are born + where Scan travels. Reuse `wireSample` `dragH` verbatim. |
| OCT/SEM/FIN | **Keep as-is** | Shared base pitch grains inherit. |
| Copy/paste sample | **Keep as-is** | Buffer-generic (`copyOscSample`). |
| AIR exciter | **Drop** | A sample-playback sweetener; clouds get shimmer from window/pitch/Key instead. |
| Stretch / Formant (phase-vocoder) | **Drop** | Granular *is* native time/pitch decoupling — don't carry both. |

---

## 6. Follower (viz)

**Sample today:** static waveform + N white playhead lines riding `position01()` (`updateSampleFollower`, `index.html:10199-10215`).

**Granular:** one line lies about a cloud. Change to a **grain-dot scatter + scan marker**:
- **DSP tap:** index-0 engine exposes a small ring of recent grain births — `struct GrainViz { float pos01; float age01; float pan; }`, ~12 latest/block. New `granCloudSnapshot(osc, out[])` beside `sampleFollowPos01` (`SynthVoice.h:118-127`).
- **Push:** extend the 60 Hz emitter (`PluginEditor.cpp:3144-3163`) with the per-osc grain array.
- **Render:** `updateGranularCloud(osc, grains)` — each grain a **thin-white dot** at `left:pos%`, `opacity = 1 - age01` (fade as they die), small vertical offset = pan. A bold **Scan marker** at `scanPos`. Frozen (Scan=0) → marker still, dots pulse in place = reads instantly as "frozen but alive."
- **Robustness:** wrap the rAF draw in try/catch + bail if the canvas is <8px (the filter-viz lesson — a dead loop stretches a 1×1 buffer into a solid block).

---

## 7. Flagship implementation

### 7.1 `Life` — living weather (reuse WAVER)
Bounded **Ornstein-Uhlenbeck** drift (already shipped for WAVER: σ/τ, Murmur3 seed) applied to `density`, `size`, `spray`, `pitchSpray`. `Life` scales σ (calm→stormy). Optional per-grain Bernoulli mute for breathing. Cost: negligible. This is what makes a held note never become a static loop (works whether drifting or frozen).

### 7.2 `Key` — in-key snap (reuse held-MIDI)
At **grain birth**, quantize the grain's pitch ratio to an allowed-degree table:
- `Off` → no quantize. `Oct/5th` → snap to octave/fifth of root. `Maj/Min/Penta` → scale degrees. `Chord-Follow` → snap to the currently **held MIDI notes** (the `Held[64]` array from the mono/legato work).
Applied *after* Pitch + Pitch-Spray, so wild scatter stays musical. Cost: one table lookup per birth.

---

## 8. Integration surface (file:line anchors — from the codebase map)

**Already stubbed (verified):** `Engine::GRAN=2` (`SynthVoice.h:44`); `"GRAN"` in the ENGINE StringArray (`PluginProcessor.cpp:888,1448,1591,1734`); `<option value="2">Granular` in all four osc selects (`index.html:5041,5210,5395,+D`); `case Engine::GRAN` silent stubs (`SynthVoice.h:1504,1739,1970,+D`).

**Reuse UNCHANGED (buffer-generic spine):** `tw::SampleBuffer`, `oscSampleBuffers_`, `SampleLoader` (`SampleLoader.h:51-161`), `loadOscSampleAsync` (`PluginEditor.cpp:9946-9987`), cached-payload reopen rehydrate (`PluginEditor.cpp:705-725`), drop/copy/paste/normalize (`PluginEditor.cpp:2007-2106`). **No new load path.**

**New DSP:** `GranularEngine.h`; per-voice `std::array<tw::GranularEngine, kMaxUnison> granEngA_..D_` beside sample engines (`SynthVoice.h:3041`); `GranularEngineParams` struct beside `SampleEngineParams` (`SynthVoice.h:428-439`); `renderGranularBlocks()` mirroring `renderSampleBlocks` (`SynthVoice.h:3220-3251`); wire into the swap points that currently zero `sAu` for GRAN (`SynthVoice.h:1503-1506` A, `1739` B, `1970` C, +D).

**Params + 6-link bind chain (per new param — miss a link = silent no-op, CLAUDE.md §4):**
`ParameterIDs.hpp` (mirror the sample block ~`:415-433`) → `createParameterLayout` register (`PluginProcessor.cpp:1827-1900`) → relay member in `PluginEditor.h` → `.withOptionsFrom(relay)` (`PluginEditor.cpp:292-363`) → `WebSliderParameterAttachment` (`PluginEditor.cpp:2212+`) → JS `getSliderState` (`index.html:9686-9749`) → `data-syn` element. Processor gather/push mirrors `SampleEngineParams spA..spD` (`PluginProcessor.cpp:3051-3095`) + `setSampleParamsA..D` (`:3243-3246`).

**UI shell:** `engine-granular` class toggle beside `engine-sample` (`index.html:9769`); `.granular-view` mirroring `.sample-view` (`:5117-5142`), CSS-gated (`:3822-3823`); `.gran-knobs` bottom row; `wireGranular(view)` cloning `wireSample` (`:10007-10192`); reuse region handles + drop/copy/paste.

**Follower:** see §6 (`SynthVoice.h:118-127` → new tap; `PluginProcessor.cpp:3494-3516` gather; `PluginEditor.cpp:3144-3163` push; `index.html:10199-10215` → `updateGranularCloud`).

---

## 9. Build phases

1. **DSP core** — `GranularEngine.h`: grain pool 64, async scheduler w/ regularity jitter, window LUTs Tukey↔Gaussian+skew, read-head/grain-birth decoupling, per-grain reverse. **Standalone harness**: prove no clicks, correct freeze, RMS-stable output, aliasing sane at small sizes. No UI yet.
2. **Params + 6-link bind** — register `SYN_OSC_*_GRAN_*` (all functions), wire the full bind chain, processor gather/push. **Verify each param moves audio before UI** (hard rule).
3. **UI controls** — `engine-granular` toggle, `.granular-view`, 6-slot `.gran-knobs`, `wireGranular`, reassignable-slot menu + ⚙ deep-options, replace loop-selector with Freeze/Scan affordance.
4. **Viz + follower** — `updateGranularCloud` scatter + scan marker (try/catch + <8px bail); transient-snap precompute-on-load.
5. **Flagship** — `Life` (OU weather) + `Key` (in-key snap, chord-follow from held MIDI).
6. **Polish** — grain-cap/CPU tuning across unison, window skew character, hero presets (frozen living pad, drum-loop drift-scan, in-key shimmer). Checkpoint + memory save.

---

## 10. Risks / notes

- **CPU:** granular × unison × 64 grains is the worst case. Default unison low + Max-Grains cap + skip-and-wait. Watch `kMaxUnison=16` fully-detuned.
- **Aliasing** at tiny grain sizes / high pitch-spray — Hermite read helps; check, add mip/oversample only if needed.
- **Slot-reassign persistence** must survive reopen (push on `signalPageReady`, don't poll — reopen-robustness lesson).
- **Serum ground-truth** captured in `.ideas/` research notes; we calibrate, we do not copy names or code.
