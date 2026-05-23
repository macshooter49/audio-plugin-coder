# Terrain Instrument — Scan Mode Design

**Status:** spec, awaiting plan
**Branch:** `feature/terrain-instrument` (worktree at `audio-plugin-coder/.worktrees/terrain-instrument/`)
**Base commit:** `2376071` (per-chop ADSR shipped — current Mark 1 close-out point)
**Target:** Mark 1.5 — last per-chop feature before Mark 2 (modular routing, A/B/C/D layers, sequencer, synth)
**Brainstorming session:** 2026-05-22 → 2026-05-23

---

## Goal

Add **per-chop ping-pong scan playback** to Terrain Instrument. When enabled on a chop, the playhead reads forward through the slice, reverses at the slice end, reads backward to the slice start, reverses again — continuing as long as the note is held. Each chop is independently scan-on/off with its own scan rate. Scan rate AND scan window are modulatable via the existing ModulationEngine.

Scan works on every chop type: `Warp:None`, `Warp:Beats`, `Warp:Tones`, `Warp:Texture`. The wild combos (Tones+Scan = scrubbing through frozen spectrum, Beats+Scan = ping-pong stutter, Texture+Scan = glitchy back-forth) are the unique-value-prop textures that reinforce the "operate on every chop independently with full studio-grade effects" positioning.

The visual indicator is a single moving line on the chop body — no letter badge, no extra UI surface. When scan is on AND the chop is sounding, you see the line ping-pong. When off, nothing.

---

## Non-goals (Mark 2 territory)

- **Full per-chop modulation matrix** — only ONE global "active chop dispatch" pattern is introduced here (`Active Chop Scan Rate`, `Active Chop Scan Window`). The broader "every per-chop knob is independently modulatable per voice" architecture is Mark 2.
- **Multi-voice independent modulation** — when two chops are held simultaneously, both pull from the same LFO value applied to their respective base rates. Polyphonic mod-per-voice (MPE-style) is Mark 2.
- **Sub-slice scan position** — no separate "scan position" knob. Position is controlled by marker placement (the existing slicer system). User wants a tighter scan range? Add markers.
- **Scan mode preset library** — defer to v1.x.
- **Texture/Tones algorithmic improvements** when combined with scan — out of scope. Scan operates on the warp engines' existing output, doesn't change their internal behavior.

---

## Behavior

### Per-chop state (new fields on `Slice`)

| Field | Type | Range | Default | Modulatable |
|---|---|---|---|---|
| `scanEnabled` | bool | true / false | false | No |
| `scanRate` | float | 0.1 – 4.0 | 1.0 | Yes — `Active Chop Scan Rate` |
| `scanWindow` | float | 0.05 – 1.0 | 1.0 | Yes — `Active Chop Scan Window` |

Defaults are chosen so legacy presets (no scan fields in JSON) load with `scanEnabled=false, scanRate=1.0, scanWindow=1.0` → identical playback to today. Zero regression risk on existing user presets.

### Playback behavior

**When `scanEnabled = true`:**
- Note-on: playhead starts at the SCAN-WINDOW start (which equals slice start when `scanWindow=1.0`). Initial direction respects the existing per-chop `reverse` toggle (`reverse=true` → start reading backward).
- Reaching the scan-window end → reverse direction, apply 8 ms equal-power turnaround crossfade (same cos/sin pattern BEATS uses at within-beat wraps in `SamplerVoice.h:377-378`).
- Continues for as long as the note is held (LOOP toggle is **independent** of scan — scan owns its own held-note behavior; LOOP only governs non-scan chops).
- Note-off: standard AR envelope release; voice stops normally.

**Scan rate (cycle time):**
- At `scanRate = 1.0` and `scanWindow = 1.0`, full ping-pong cycle (forward + backward) = 2 × slice duration in wall-clock time.
- `scanRate = 0.5` → cycle = 4 × slice duration (slow drone scan).
- `scanRate = 2.0` → cycle = 1 × slice duration (fast scrub).
- `scanRate = 4.0` → cycle = 0.5 × slice duration (aggressive scrub).

**Scan window (active range):**
- `scanWindow = 1.0` → full slice scanned (default, no UI knob).
- `scanWindow = 0.4` → 40 % of slice centered at slice midpoint scanned. Effective range: `[sliceStart + 0.3 × sliceLen, sliceEnd - 0.3 × sliceLen]`.
- Window changes apply on next direction reversal, NOT mid-sweep — avoids clicks from instantaneous bounds-jump.

**Interactions with existing per-chop fields:**

| Field | Scan interaction |
|---|---|
| `reverse` | Defines initial scan direction. Scan flips this at every turnaround. |
| `pitchSemitones` | Independent — scan rate doesn't change pitch (except in Warp:None at rate≠1.0, see below). |
| `warpMode` | Independent — scan operates on warp output, see Engine section. |
| `stretchRatio` | Independent — scan cycle period scales with audible slice length (post-stretch), not source-sample length. |
| `attackMs`/`decayMs`/`sustainLevel`/`releaseMs` | Independent — envelope owns amplitude, scan owns position. |
| `volume` | Independent — applied to gain after scan playback. |
| **LOOP global toggle** | **Independent** — scan ignores LOOP entirely. |

### Warp × Scan combinations

| Combination | Behavior | Pitch behavior |
|---|---|---|
| `Warp:None` + `Scan:on`, rate=1.0 | Cheap source-level ping-pong (existing NONE-path direction logic, plus turnaround crossfade) | No pitch artifact |
| `Warp:None` + `Scan:on`, rate≠1.0 | Source-level ping-pong at varispeed | **Pitch shifts with rate** (classic tape-scrub character — documented behavior, not a bug) |
| `Warp:Tones/Beats/Texture` + `Scan:on` | Pre-rendered cache (see Engine), scan reads the stretched output buffer | Pitch fully decoupled from scan rate (rate is pure motion through cached output) |

---

## Data model

### `Slice.h` changes

Add to the `Slice` struct (alongside existing `warpMode`, `stretchRatio`, `reverse`, ADSR fields):

```cpp
bool  scanEnabled = false;  // default off — legacy presets unchanged
float scanRate    = 1.0f;   // 0.1..4.0, exponential curve, 1.0 = natural ping-pong
float scanWindow  = 1.0f;   // 0.05..1.0, default = full slice (mod-only, no manual UI)
```

### JSON roundtrip

Update three places (mirror the existing `warpMode`/`stretchRatio` pattern from the warp-modes spec):

1. **`slicesToJson` (Slice.h)** — write all three fields.
2. **`slicesFromJson` (Slice.h)** — read all three with the defaults above. Legacy presets without these keys → defaults apply → playback unchanged.
3. **JS `applySlicesJson` (PluginEditor.cpp inline JS)** — **MUST extract all three fields**, per the existing warp-gotcha: every new Slice field that isn't preserved by `applySlicesJson` silently drops through the JS state (drag the knob → C++ updates → JS overwrites it back to default on next roundtrip). Same pattern as the existing `warpMode`/`stretchRatio` extraction.

### Native functions (PluginEditor.cpp)

```
setSliceScanEnabled (int sliceIndex, bool enabled)
setSliceScanRate    (int sliceIndex, float rate)
// scanWindow has no native fn — mod-only, no manual UI
```

Same shape as the existing `setSliceWarpMode` / `setSliceStretchRatio`. Both write through the existing slice-update path that already triggers JSON serialization.

### Random Octave preservation

The existing `Object.assign`-based Random Octave preserves all slice fields by name (per `[[terrain-instrument-warp-gotchas]]`). New scan fields auto-preserve — no code change. **Verify in testing** that Random Octave on a scan-enabled chop keeps `scanEnabled`/`scanRate`/`scanWindow` intact.

---

## Engine

### Where scan lives in `SamplerVoice.h`

Today's structure (`SamplerVoice.h:186-406`):
- `renderNextBlock` is the entry point. If `warpMode != None` → branches into `renderWarp` (line 200-207). Else falls through to the in-place per-sample loop (line 268-405).
- The in-place loop already handles both directions via `reversePlay` (line 271 `atForwardEnd`, line 272 `atReverseEnd`, line 404 `playhead += reversePlay ? -pitchInc : pitchInc`).
- Boundary hit (line 273-301) currently does one of two things: loop-wrap (line 281-290) or one-shot stop (line 295-300).

### Scan path — Warp:None

Modify the boundary handler at `SamplerVoice.h:273-301`:

```cpp
if (atForwardEnd || atReverseEnd)
{
    if (scanActive)
    {
        // Flip direction, kick off 8ms equal-power turnaround crossfade.
        reversePlay = !reversePlay;
        turnaroundFadeT = 1.0;          // counts down to 0 over 8ms
        // Clamp playhead to scan-window bounds (handles window changes
        // applied between turnarounds).
        if (reversePlay) playhead = effectiveSliceEnd;
        else             playhead = effectiveSliceStart;
        continue;                        // skip wrap/stop logic
    }
    // existing wrap/stop logic unchanged…
}
```

Effective scan bounds:
```cpp
const double windowSpan = sliceLen * scanWindow;
const double margin     = (sliceLen - windowSpan) * 0.5;
const double effectiveSliceStart = startIdx + margin;
const double effectiveSliceEnd   = endIdx   - margin;
```

Playhead advance rate: `playhead += reversePlay ? -(pitchInc * scanRateLive) : (pitchInc * scanRateLive)`. At `scanRate=1.0` reduces to current behavior. At `scanRate≠1.0` produces the varispeed character (documented).

Turnaround crossfade applied in the per-sample mix:
```cpp
if (turnaroundFadeT > 0.0)
{
    const float fadeAmount = (float) turnaroundFadeT;
    const float prevDirGain = std::cos(fadeAmount * juce::MathConstants<float>::halfPi);
    const float newDirGain  = std::sin(fadeAmount * juce::MathConstants<float>::halfPi);
    // Blend last-direction sample (read from the about-to-be-reversed position)
    // with new-direction sample (read from the freshly-reversed position).
    // 8ms total → decrement turnaroundFadeT by (1.0 / (0.008 * sampleRate)) per sample.
}
```

### Scan path — Warp:Beats/Tones/Texture (new: `WarpRenderCache`)

New file: **`plugins/TerrainInstrument/Source/Warp/WarpRenderCache.h`** (header-only, matching existing engine conventions).

```cpp
class WarpRenderCache
{
public:
    struct Key  // identifies a cached buffer
    {
        int    sliceIndex;
        int    sourceVersionId;   // monotonic counter; increments when SampleLoader::loadNewSample() succeeds or when the source buffer pointer changes
        float  stretchRatio;
        WarpMode warpMode;
        bool operator==(const Key&) const noexcept;
    };

    /** Returns true if a cache for this key is ready. False if missing OR still rendering. */
    bool isReady(const Key& k) const;

    /** Get the pre-rendered buffer for the key. Returns nullptr if not ready. */
    const juce::AudioBuffer<float>* get(const Key& k) const;

    /** Kick off a background render. No-op if already cached or in flight. */
    void prewarm(const Key& k, /* source buffer + slice bounds + WarpProcessor factory */);

    /** Invalidate all entries for a given sliceIndex (called when slice mutates). */
    void invalidateSlice(int sliceIndex);

    /** Invalidate all entries with a given sourceVersionId (called when source changes). */
    void invalidateSource(int sourceVersionId);

private:
    // Thread pool (1-2 workers max) running renders.
    // Lock-free hand-off: render writes to the buffer, then atomically publishes
    // a finished pointer that the audio thread reads via acquire-load.
    // RAM cap: 16 cache entries max (one per slice typical), each ~3 MB worst case
    // (2-sec slice × 4× stretch × stereo float at 48 kHz) → ~50 MB ceiling.
};
```

**Render trigger points:**
- Toggling scan-on for a chop with `warpMode != None` → call `prewarm(key)`. Background render runs while user is still interacting with the UI.
- Slice mutation (source change, stretchRatio change, warpMode change) → `invalidateSlice` + auto-`prewarm` if `scanEnabled` is still true.
- Note-on: check `isReady(key)`. If ready, scan reads from cached buffer. If not ready (cache miss — typically only the very first note-on after toggle), block briefly waiting on the render Future (worst case ~100 ms on long chops with M-series silicon).

**Scan playhead reads from cache identically to source:**
- Same direction-flip + turnaround crossfade math as `Warp:None`, but operating on `cache->getReadPointer(channel)` instead of `buf->getReadPointer(channel)`.
- `scanRate` controls playhead advance through the cache. Since the cache is already at the target stretch, varying `scanRate` here is pure motion — no pitch artifact.

### State additions on `SamplerVoice`

```cpp
bool   scanActive       = false;   // mirrors activeConfig.scanEnabled
float  scanRateLive     = 1.0f;    // base × mod (resolved per block)
float  scanWindowLive   = 1.0f;    // base × mod (resolved per block)
double turnaroundFadeT  = 0.0;     // 1.0 → 0.0 over 8ms after each reversal
// scanDirection is captured by existing reversePlay
```

### Files touched

| File | Change |
|---|---|
| `Slice.h` | Add 3 fields, extend JSON roundtrip |
| `SamplerVoice.h` | Boundary handler rewrite, scan state, turnaround crossfade, cache integration |
| `Warp/WarpRenderCache.h` | **NEW** — owns pre-rendered buffers + background render scheduler |
| `Warp/WarpProcessor.h` | Add one-shot full-slice render entry point (used by cache) |
| `TerrainSynth.h` | Populate `VoiceConfig` with scan fields from Slice; trigger cache prewarm on scan-toggle |
| `PluginEditor.cpp` | Inline JS: MOTION row UI, scan-line viz, mod target wiring, JS `applySlicesJson` extension |
| `PluginEditor.h` | WebSliderRelay registration for scan rate (4 places — see Gotchas below) |
| `Source/ModulationEngine.h` | Extend `paramMin`/`paramMax` arrays; add `kModTargetActiveChopScanRate`, `kModTargetActiveChopScanWindow` |

---

## Modulation integration

Two new mod targets in `ModulationEngine.h`:

| Target | Slice field | Range applied | Mod combine |
|---|---|---|---|
| `Active Chop Scan Rate` | `scanRate` | 0.1 – 4.0 (clamped) | Multiplicative: `live = clamp(base × (1 + offset), 0.1, 4.0)` |
| `Active Chop Scan Window` | `scanWindow` | 0.05 – 1.0 (clamped) | Multiplicative: `live = clamp(base × (1 + offset), 0.05, 1.0)` |

**Modulation depth direction — important for scanWindow:**
- `scanWindow` defaults at the TOP of its range (1.0 = full slice). A bipolar LFO mathematically swings the modOffset in both directions, but the upper-clamp at 1.0 means **only the negative half of the LFO is audible** — the window narrows during the LFO's negative excursions, returns to 1.0 during positive excursions. Effectively a unipolar "narrowing" mod, even with a bipolar source. This is intentional: users dial the LFO depth = how MUCH the scan narrows.
- `scanRate` has its base in the middle of its useful range (1.0 with min 0.1, max 4.0), so bipolar LFOs swing it both faster and slower symmetrically.

**"Active chop" dispatch pattern (NEW architectural foundation):**
- ModulationEngine doesn't know which chop is active. Instead, each voice's `renderNextBlock` (or `beginBlock`) asks the mod engine for the modulated value of "Active Chop Scan Rate" — and applies the result on top of *its own* slice's base rate.
- Concretely: voice computes `scanRateLive = juce::jlimit(0.1f, 4.0f, activeConfig.scanRate * (1.0f + modEngine.getModOffset(kModTargetActiveChopScanRate)))`.
- Multiple voices sounding simultaneously → each pulls the same LFO value but applies to its own per-chop base → polyrhythmic ping-pong textures emerge naturally with one LFO and per-chop base differentiation.

**ModulationEngine changes:**
- Extend `paramMin` / `paramMax` arrays. **CRITICAL:** these arrays MUST have entries for ALL `pNumParams` (per the existing gotcha — C++ zero-initializes missing entries → `getModulatedValue()` clamps to [0,0] → broken modulation).
- Add the two target identifiers to the enum.
- No new LFO/source code needed — existing LFOs/XY pad target these like any other param.

**Mod panel UI changes:**
- "Active Chop Scan Rate" and "Active Chop Scan Window" appear in the existing mod target dropdown list, alongside global targets.
- Mod ring/badge logic: scan rate display in MOTION row shows the standard mod ring when being modulated. Scan window has no manual display so no ring needed (the chop-body window highlight conveys the modulation visually).

**Why "Active Chop" pattern matters for Mark 2:**
The same dispatch pattern generalizes to `Active Chop Stretch`, `Active Chop Pitch`, `Active Chop ADSR`, etc. when Mark 2's full per-chop modulation arc lands. Establishing it now for scan rate means Mark 2 reuses the plumbing instead of rewriting it.

---

## UI

### Chop overlay panel — new MOTION row

Layout C from brainstorming: dedicated row between the 4 mode emblems and the ADSR display.

```
┌──────────────────────────────────────┐
│  CHOP 02                          ×  │
│                                       │
│  [—] [▒] [∿] [⋮⋮]   ← mode emblems  │
│                                       │
│  MOTION  [SCAN ON]      RATE 2.00×   │  ← NEW row
│                                       │
│  ╭─ ADSR display ────────────────╮   │
│  │                                │   │
│  ╰────────────────────────────────╯   │
│                                       │
│  [VOL]  [PITCH]  [STRETCH]           │
│                                       │
│  ↺ REVERSE  ↻ RESET  ✕ DELETE        │
└──────────────────────────────────────┘
```

**MOTION row spec:**
- Fixed 28 px height row, baseline-aligned (all elements share `line-height: 1` and explicit heights — no padding drift)
- Left: `MOTION` label (9 px, letter-spacing 1.5px, opacity 0.55)
- Center: `SCAN ON / SCAN OFF` pill (18 px height, 0 10 px padding, border-radius 10 px, click to toggle). On-state uses purple accent border + 28 % opacity fill; off-state lightens the border and dims to 55 % opacity.
- Right: `RATE 2.00×` display (`margin-left: auto`, 9 px label opacity 0.5, 10 px white value). Drag-to-change behavior identical to the bottom-row knobs (already established gesture). When modulated, shows the standard mod ring around the value.

**Right-click context menu:**
- Add one item: **"Scan: On/Off"** toggle (mirrors the SCAN pill state).
- Add submenu: **"Scan Rate"** → quick presets (0.5× / 1.0× / 2.0× / 4.0×) — convenience only.

### Chop body scan-line viz

Renders directly on each chop's `<canvas>` in the main slicer view.

**Visual spec:**
- 1.5 px wide solid line, color `#c8a8ff` (Terrain purple accent). No glow, no shadow, no trail — matches Terrain's restrained design language.
- Window highlight: when `scanWindow < 1.0`, render a faint purple tint (~8% opacity) plus dashed edge markers at the active scan range bounds. Hidden when window = 1.0.
- Renders ONLY when `scanEnabled == true` AND a voice is currently sounding on that slice. Disappears on note-off.

**Polling:**
- JS `requestAnimationFrame` loop polls C++ at 30 Hz (one pull per ~33 ms), identical cadence to existing slice-glow viz. C++ pushes nothing — JS pulls.
- C++ exposes `getScanPosition(int sliceIndex)` returning normalized [0, 1] position within the slice (NOT within the scan window — full-slice coordinates, so the visualization knows where the line sits relative to slice boundaries) and `getScanWindowBounds(int sliceIndex)` returning `{startNorm, endNorm}` for the current live window range.
- JS animation loop reads both values per frame and redraws the line + window-rect canvas overlay.
- **Update granularity:** because `scanWindow` only re-samples at direction reversals (per Behavior section), the window bounds JS reads at frame N are guaranteed stable until the next turnaround — no per-frame jitter on the dashed window edges, even with a fast LFO on window.

**Edge cases:**
- Multiple voices on same chop → display position from the most-recently-started voice. (Positions converge anyway since same slice = same scan motion.)
- Chop too narrow to display line (visualWidth < 50 px) → hide viz, same threshold as existing chop-body overlays (per Phase 2 polish — `auto-hide when chop visualWidth < 50px`).

### WebSliderRelay registration (the perennial gotcha)

The scan rate "knob" (drag-to-change RATE display in the MOTION row) is a slider exposed to the WebView. Per `[[terrain-websliderrelay-gotcha]]`, MUST register in **4 places**:

1. `PluginEditor.h` — add `juce::WebSliderRelay scanRateRelay` field.
2. `PluginEditor.cpp` constructor — initialize the relay with the param ID.
3. `setupRelays()` — attach the relay to the WebView options.
4. JS bridge — confirm the slider state property is wired in the JS state object.

Skipping any of these = silent no-op (JS writes go to a phantom `SliderState` with no APVTS binding, audio thread reads default forever, no error fires). Burn-rate from past sessions: hours per skipped step. **Don't skip.**

Note: `scanWindow` is mod-only with no manual UI, so it does NOT need a WebSliderRelay — modulation writes go straight to the engine via the existing mod path.

---

## Testing

### Unit tests (C++)

Extend `plugins/TerrainInstrument/Source/Warp/WarpProcessor_test.cpp` and/or add `SamplerVoice_test.cpp`:

- **Click detector at turnarounds.** Render a 440 Hz sine through a scan-on chop, sweep the (rate × window × pitch) parameter space, verify no discontinuities exceed -60 dB above the noise floor at every direction reversal.
- **Cache hit/miss.** Render a warped scan-on chop, verify second-trigger reads from cache (no warp engine invocation). Mutate stretchRatio → verify cache invalidation fires. Mutate source sample → verify global cache reset.
- **Scan rate math.** At `scanRate=1.0` with N-sample slice, full ping-pong cycle = 2N samples. At `scanRate=2.0` = N samples. At `scanRate=0.5` = 4N samples. Off-by-one boundary tests at turnaround.
- **Scan window math.** At `scanWindow=1.0`, effective range = `[sliceStart, sliceEnd]`. At `scanWindow=0.4` centered, range = `[sliceStart + 0.3·sliceLen, sliceEnd - 0.3·sliceLen]`. Window-change applied between turnarounds, never mid-sweep.
- **Modulation correctness.** Mod engine sets target = X, voice reads X, effective rate = `base × (1 + X)`. Two voices on different slices share LFO value, each applies to own base.

### Behavioral verification (manual, in DAW)

- Scan-on chop at `Warp:None`, rate 1.0× — smooth ping-pong, zero clicks.
- Scan-on chop at `Warp:None`, rate ≠ 1.0× — varispeed pitch character audible (expected; documented in tooltip).
- Scan-on chop at `Warp:Tones/Beats/Texture` — first note-on after enabling scan may have ≤100 ms latency (the cache prewarm should usually hide this). Subsequent triggers instant.
- Scan-on chop modulated by LFO — rate display in MOTION row shows mod ring; scan audibly accelerates/decelerates.
- Window modulated by LFO — chop body window highlight visibly narrows/widens; ping-pong stays inside.
- Two scan-on chops at different base rates held simultaneously — each scrubs independently, both wobble in unison with the shared LFO.
- Three entry points (right-click menu, MOTION-row pill, scan rate drag) all produce identical state changes.
- pluginval level 5: SUCCESS (per existing Terrain Instrument standard).

### Regression tests

- Legacy presets (no scan fields in JSON) load with all defaults; playback identical to base commit `2376071`.
- Existing warp suite (Beats/Tones/Texture) with `scanEnabled=false` produces bit-identical output to base commit.
- Random Octave on a scan-enabled chop preserves all scan fields.
- DAW state persistence: close project with scan-on chops, reopen, full state restored.

### Pre-warm verification

- Toggle scan-on for a `Warp:Tones` chop → background render kicks off immediately (visible in instrumentation log).
- By the time user plays a note (≥500 ms later, typical user flow), cache is ready → zero first-trigger latency.
- Queue 4 simultaneous scan-on toggles on 4 different stretched chops → all 4 renders process sequentially without blocking UI thread.
- Cancel-mid-render: change stretchRatio on a chop while its cache is rendering → in-flight render cancels cleanly, no orphaned threads.

---

## Mark 2 candidates surfaced by this work

Capture for future arcs (NOT in scope):

- **Full per-chop modulation matrix** — generalize the "Active Chop X" dispatch pattern established here to every per-chop param (stretch, pitch, ADSR, volume, etc.). Establishes the modular-routing foundation of Mark 2.
- **Multi-voice per-voice modulation** — MPE-style independent mod per voice (different LFO phases on different held notes).
- **Sub-slice scan position knob** — only if marker placement turns out to be insufficient in practice.
- **Scan curve shaping** — instead of linear ping-pong, expose a curve (ease-in/ease-out, exponential, S-curve) so the scan motion accelerates/decelerates within each sweep.
- **DECAY warp mode + Scan** — the deferred 4th warp mode (per `[[terrain-instrument-warp-open-work]]`) might pair particularly well with scan; revisit when DECAY lands.

---

## Open questions

None as of brainstorming end (2026-05-23). All architectural questions answered:
- Hybrid scan model (boundary ping-pong with rate knob 0.1–4.0×) — locked.
- Orthogonal to Warp (not a 4th warp mode) — locked.
- Per-chop base rate + "Active Chop" mod dispatch — locked.
- Path X (pre-render with cache) for Warp+Scan combinations — locked.
- Varispeed character at `Warp:None + scanRate≠1.0` — locked as documented behavior.
- Layout C (MOTION row in overlay panel) — locked.
- Clean 1.5 px purple line viz, no glow/trail — locked.
- Scan position via markers, not new knob — locked.
- Scan window as second mod target — locked.

---

## Related artifacts

- Prior spec: `.planning/specs/2026-05-21-terrain-instrument-warp-modes-design.md`
- Visual mockups (this brainstorm): `.superpowers/brainstorm/50750-1779509075/content/`
  - `chop-overlay-c-refined.html` — locked MOTION row layout
  - `scan-line-viz-v2.html` — locked chop-body viz
- Memory referenced during design:
  - `[[terrain-instrument-warp-shipped]]` — base architecture
  - `[[terrain-instrument-warp-gotchas]]` — lessons that informed this design
  - `[[terrain-instrument-warp-open-work]]` — items deferred to Mark 2
  - `[[terrain-instrument-product-vision-position]]` — unique-value-prop framing
  - `[[terrain-websliderrelay-gotcha]]` — 4-place registration discipline
