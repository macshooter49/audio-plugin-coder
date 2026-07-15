# WAVETABLE EXTENDER — Build Plan
*"Turn anything into a wavetable." Terrain Instrument · drafted 2026-07-14 · research-backed (Serum 2 manual, licensing + canvas-viz web research, codebase map).*

---

## 0. Vision & north star
- **Drop in any audio → it becomes a playable wavetable.** Plus **import other synths' wavetable files** (Serum/Vital). All four oscillators.
- **North star = Phase Plant / modular.** Keep every piece a self-contained, node-ready unit (import = a clean `audio → analyze → tw::Wavetable` pipeline; the viz is its own module). Power comes from combining modules, not from one giant editor. This feeds the Terrain Patcher endgame.
- **Simple by default, deep when wanted.** Drag-and-drop just works (one great auto analysis). No meticulous shaping required (Max: "keep this shit simple").
- **Deferred (explicit):** in-app wavetable **shaper/editor**, **making tables in-app**, **image→wavetable** (trivial later — see §7), and the full multi-mode drop-zone overlay (start with one auto mode).

---

## 1. Research summary (the load-bearing facts)

### Serum's import model (the gold standard to emulate)
- **Frame = 2048 samples; up to 256 frames** per table. Store the **interpolation TYPE**, not the interpolated frames (load-time morph → tiny files).
- Drag audio onto the osc waveform → an **overlay whose drop-zone picks the analysis mode**:
  - **Constant Framesize / Pitch-Average** — *"if in doubt, try this first."* Detects avg pitch → fixed frame length. Best for fixed-pitch one-shots.
  - **Dynamic Pitch (Zero-Snap / Follow)** — per-frame pitch tracking; Follow handles complex/noisy material.
  - **Frequency Estimation** — finds the fundamental, matches a musical pitch, preserves harmonics. Best for clean pitched material.
  - **FFT Resynth (256/512/1024/2048)** — spectral, not time-slicing; "blurred averaging of frequency content." **This is what makes arbitrary audio (drums, speech, texture) usable.**
- **Click the waveform toggles 2D ↔ 3D.** 3D "waterfall" = all frames; **green = real, gray = interpolated, yellow = selected**; **WT POS shown lower-left**.
- **Selector** = categories as fly-out submenus + `< >` arrows + mouse-wheel; user tables in a **User** folder; disk scan is **one level deep**.

### Wavetable file format (for importing Serum/Vital tables)
- Standard **RIFF/WAV**, mono, 16-bit PCM or 32-bit float, nominally 44.1 kHz, **2048 samples/frame** concatenated.
- Serum carries a custom **`clm ` RIFF chunk** (4-char id, trailing space): ASCII `<!>2048 <8-digit flags> <name>` where **2048 = samples-per-frame**. Parse it to slice frames.
- **No metadata? Detect:** `frame_count = total_samples / frame_size`; try common sizes **2048 → 1024 → 4096 → 512 → 256**, pick the one that divides evenly (prefer 2048). Single-cycle file = 2048 samples = 1 frame.

### 3D waterfall in HTML5 **canvas-2D** (no WebGL in WKWebView) — concrete recipe
- **Oblique "lerp" projection** (visually identical to Serum's perspective, far cheaper): define a **front anchor line** and **back anchor line** on screen; for a point `(u∈[0,1], a∈[-1,1], depth d∈[0,1])` lerp left/right X, baseline Y, and amplitude between front/back by `d`. Back line sits **higher + narrower + shorter-amplitude** → recedes up-and-back with a slight rightward skew (the Serum tilt). Zero divides.
- **Hidden-line = opaque-curtain fill.** Draw frames **back→front** (painter's); for each, fill a polygon from its top edge down to the canvas bottom in the **bg color**, then stroke only the top edge. Nearer curtains overwrite farther lines. (Translucent fill `rgba(bg,0.82)` = Vital's airier look.)
- **Perf (decisive): offscreen-cache the static stack.** The whole stack only changes on table-data / resize / view-angle change. Render it once to an offscreen canvas; every animation frame = **1 `drawImage` blit + 1 highlight polyline**. 256-frame redraw → trivial 60fps.
- **Decimate:** draw ~**48–64** representative frames (not all 256); X-points per frame scale with depth (`~64` far → `~360` near).
- **Current position (WTPOS):** its own polyline at `d = pos/(N-1)`, drawn **on top** using the **morph-interpolated** waveform between adjacent frames so it slides smoothly. **Purple `#b07cff` (purple-400), ~2.2px, `shadowBlur ~10` glow.** Everything else white, depth-faded alpha (near bright → far dim). Dark bg. → lands exactly on our thin-white / one-accent house style.

### Licensing (Max's question, answered) — see §6.

---

## 2. Codebase map (where it plugs in)
- **`Source/Wavetable.h` → `tw::Wavetable`:**
  - `kNumMipLevels = 8` (256/128/64/32/16/8/4/2 harmonics). Flat `mipData_` = `[lvl*NF*FS + frame*FS + samp]`. Factory tables built **frequency-domain** via **`buildFromSpec()`** (FFT per frame per mip → harmonic-limited → `normalizeMipLevels()`). Raw ctor `Wavetable(numFrames, frameSize)` (mip=1) exists as a container.
  - `lookup(mipLevel, framePos, phase)` is the render path — **`framePos` (0..1) = WTPOS** scanning frames with morph.
  - **⟹ NEW: `buildFromPcm(const float* pcm, int totalSamples, int frameSize, int maxFrames)`** — slice → per-frame FFT → build the 8 band-limited mips (reuse `buildFromSpec`'s per-level harmonic-limiting + `normalizeMipLevels`). Supports up to 256 frames. Match/resample to the engine's native `kFrameSize` (2048 for Serum interop).
- **`Source/ParameterIDs.hpp`:** `SYN_OSC_{A..D}_WT_FRAME` (float 0..1) = **WTPOS (scan)** — reuse as-is. `SYN_OSC_{A..D}_WT_PRESET` (choice, fixed enum of built-ins) = **the selector** — must become a **dynamic table reference** (see §5 risk).
- **`Source/ui/public/index.html`:** engine `<select>` (Wavetable/Sample/Granular/FM/Resynth/Harmonic) ~L5344; sample drag-drop → `loadSampleForOsc` (decode path to reuse); the **canvas waveform visualizer** ~L3858 (add the left-click 2D↔3D toggle here).
- **`Source/PluginProcessor.cpp` / `PluginEditor.cpp`:** native fns `loadSampleForOsc` / `loadSampleFromBase64` (audio decode we reuse); how built-in tables reach voices (`sv->setWavetable(const tw::Wavetable*)`).

---

## 3. Analysis modes — v1 scope (YAGNI)
Ship **two auto modes** first (covers "anything"), skip the full drop-zone overlay initially:
1. **Resample (constant-frame / pitch-average)** — default; the "just works" path for most audio.
2. **FFT Resynth** — for arbitrary/percussive/noisy/vocal material (drums, speech, textures).
Single-cycle detection + Dynamic-Pitch + the multi-zone overlay = **fast-follow**, not v1.

---

## 4. Phased delivery

**Phase 1 — Engine: `buildFromPcm` + dynamic per-osc tables.**
- Add `Wavetable::buildFromPcm(...)` (slice → FFT → 8 mips → normalize). Add a per-osc **owned dynamic `tw::Wavetable`** (so an osc can hold an imported table, not just a built-in enum). Wire `setWavetable` to it. All 4 oscs.
- *Verify:* generate a table from a test buffer, confirm it plays + scans with WT_FRAME, no aliasing.

**Phase 2 — Audio → wavetable (the drop).**
- Extend the audio-drop flow: add **"Wavetable"** as the 4th target next to Sample / Granular / Resynth. Picking it → new native `importAudioAsWavetable(osc, b64, mode)` → decode (reuse) → `buildFromPcm` → assign + switch osc to Wavetable engine. Default mode = Resample; (mode arg ready for FFT/overlay later).
- *Verify (eye+ear):* drop a vocal → scan WTPOS morphs the timbre; drop a drum loop → scan its guts.

**Phase 3 — 3D waterfall visualizer.**
- **Left-click the oscilloscope** → toggle to the canvas-2D waterfall (§1 recipe): white depth-faded lines, **purple WTPOS line that moves as position modulates**, offscreen-cache + decimation. Basic shapes get waterfalls too. All 4 oscs. Left-click back to 2D.
- *Verify:* 60fps, purple streak tracks WTPOS live, matches the reference screenshots recolored white-on-purple.

**Phase 4 — Wavetable FILE import + Imports folder + glass-menu selector.**
- **Imports folder:** created on first run at the OS-correct spot (macOS `~/Library/Application Support/Waves Crate/Terrain/Wavetables/` → subfolders `Imports/`, `Factory/`; Win `%APPDATA%\...`). Scanned on launch + on "refresh".
- **File import:** parse `.wav` wavetables (`clm ` chunk or frame-size detection §1) → `buildFromPcm`.
- **Glass-menu selector:** left-click the header selector opens the **transparent glass menu** (retire the Apple dropdown here) → **Open Imports Folder** (top) · **Basic** · **Factory** · **Imports**. Dynamic table list backs it (§5).
- *Verify:* drop a Serum `.wav` into the folder → appears under Imports → loads + scans + shows its waterfall.

**Phase 5 — Factory library (legal, big).** §6 — CC0 ingest + generator. Parallel track; can start anytime.

**Deferred:** shaper/editor · image→wavetable (§7) · in-app table creation · multi-zone analysis-mode overlay · morph-type choice (crossfade vs spectral).

---

## 5. Key decisions & risks
- **Dynamic selection is the main architectural lift.** Today `WT_PRESET` is a fixed choice enum; imports/factory need a **dynamic table list** (id/path → loaded `tw::Wavetable`) per osc, with preset save/recall. Plan: keep `WT_PRESET` for built-ins, add a parallel "table reference" (source: builtin | factory-file | import-file + identifier) that the glass menu drives and the preset serializes.
- **Preset persistence:** an imported table must survive save/reload. Options: **embed** the table bytes in the preset (Serum's "Embed in Preset"; safe, bigger presets) or **reference** the imports-folder path (small, breaks if moved). Recommend **embed by default** for imported audio, **reference** for factory.
- **Frame size:** engine native `kFrameSize`; resample imported frames to it. Match **2048** for Serum interop; read/write a `clm ` chunk if/when we export.
- **CPU:** all analysis is **offline on import** → zero runtime cost. Viz uses offscreen-cache → ~1 blit/frame. Fully in line with the CPU-friendly rule.
- **Anti-aliasing:** imported tables MUST build all 8 mips (band-limited) like factory tables, or high frames alias — reuse `buildFromSpec`'s harmonic limiting.

---

## 6. Factory tables — the legal answer (do NOT bundle downloaded packs)
Bundling downloaded/commercial packs = **copyright + EULA infringement** (packs are licensed; EULAs forbid redistribution as factory content). Legal 3-tier path to a *big* library fast:
1. **CC0 / public-domain (thousands, day one):** **AKWF** (~4,300 single-cycle waveforms, CC0), **WaveEdit Online** CC0 banks, **open-vital-resources**. Convert via an offline import script. Keep a **license manifest** per table.
2. **Generate our own (distinctive):** batch-generate with Terrain's DSP (additive/harmonic, FM, phase-distortion, warp/fold, spectral-morph) → categorized banks (analog, harmonic sweeps, FM, formant/vowel, metallic, folded, spectral). Unambiguously ours, zero licensing — and it's how Serum/Pigments actually do it.
3. **Commission a "Terrain Signature" bank (optional, paid):** work-for-hire with full-redistribution assignment. The flagship, reviewer-noticed content.
**Guardrail:** anything shipped must be CC0 / self-generated / work-for-hire / explicitly OEM-licensed — never ripped from another synth, never a pack whose EULA you didn't read for a redistribution grant.

---

## 7. Image → wavetable (deferred, but easy — noted for Max)
Serum's model: **image width = frame count (≤256), pixel luminance = amplitude** (black=silent, white=max). Trivial once the drop pipeline exists — an image decode → per-column luminance → frames → `buildFromPcm`. Park it; it's a fun fast-follow.

---

## 8. Open decisions for Max
1. **Build order** — Engine→Drop→**Waterfall viz**→File-import/folder/selector→Factory, OR pull the **waterfall viz first** (sexiest, visible)?
2. **v1 analysis modes** — start with the two auto modes (Resample + FFT), overlay/mode-picker later? (recommended)
3. **Factory** — green-light the CC0-ingest + generator plan?
4. **Preset persistence** — embed imported tables by default? (recommended)
