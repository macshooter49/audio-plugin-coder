# CLAUDE.md — Terrain Instrument / Waves Crate (project root authority)

**Read this first, every session.** This is the single file Max owns. If anything in
`.claude/rules/*.md` conflicts with what's written here, **THIS file wins**. For deep
project history and per-topic detail, read `MEMORY.md` and its linked topic files —
this file is the operating contract + the load-bearing facts, not the full archive.

---

## 0. Who does what (the cross-AI workflow — LOCKED)

This project runs as a two-AI team:

- **Claude chat (Opus 4.8)** = the architect. Does research, DSP math, writes the actual
  C++ / HTML, and offline-validates it (g++ + a JUCE shim; brace/syntax checks). Delivers
  **complete, finished files**.
- **You, Claude Code** = the builder. Your job is **package -> place -> compile -> report**:
  1. Take the finished files Max drops on disk.
  2. Copy each into its correct path (do **not** regenerate, re-type, refactor, or "improve").
  3. Build the VST3 **and** AU.
  4. Report results — on failure, the **FIRST** error verbatim (not the whole cascade).

The dropped files are the source of truth. **Never re-author content pasted into chat** —
for large files (index.html is ~17K lines) re-typing via Write/Edit causes silent
transcription drift. Always copy the finished file from the drop folder. If something looks
wrong, **report it — don't fix it yourself.**

---

## 1. The drop workflow

Finished files arrive in the drop folder: **`~/terrain-drop/`**.
Copy each present file into the repo (under the plugin `Source/` dir), then build.

| Drop file            | Destination (under `Source/`)   |
|----------------------|---------------------------------|
| index.html           | **ui/public/index.html**        |
| PluginEditor.cpp     | PluginEditor.cpp                |
| PluginEditor.h       | PluginEditor.h                  |
| SynthVoice.h         | SynthVoice.h                    |
| PluginProcessor.cpp  | PluginProcessor.cpp             |
| ParameterIDs.hpp     | ParameterIDs.hpp                |
| TerrainFilters.h     | TerrainFilters.h                |

Only copy the files actually present in the drop — not every file changes each time.

```bash
SRC="$HOME/Developer/VST-Plugins/audio-plugin-coder/.worktrees/terrain-instrument/plugins/TerrainInstrument/Source"
DROP="$HOME/terrain-drop"
cp "$DROP/index.html"          "$SRC/ui/public/index.html"
cp "$DROP/PluginEditor.cpp"    "$SRC/PluginEditor.cpp"
# ...only the files present in the drop...
```

> **WebView path is `Source/ui/public/index.html` for TerrainInstrument** — NOT `WebUI/`.
> The generic "WebView lives in WebUI/" rule in `juce-build-protocols.md` does **not** apply
> to this plugin. Do not move index.html. The CMake target `TerrainInstrument_WebUI` embeds
> it from `Source/ui/public/index.html`.

---

## 2. BUILD — the rules that actually matter here

### A. Build BOTH formats, every time
Max runs **Ableton AND FL Studio**; either may load either format. Building only one strands
him on a stale plugin in the other (looks like "my change didn't take effect"). **Always build
`TerrainInstrument_VST3` AND `TerrainInstrument_AU`.** Verify both with `strings` after.

### B. index.html changed? You MUST bust the BinaryData cache (or you embed STALE HTML)
This is the #1 repeatedly-tripped trap. `touch` / a plain rebuild does **not** reliably
re-embed an edited index.html — CMake misses the mtime change on `juce_add_binary_data`
inputs. After copying a new index.html you must delete the WebUI BinaryData build tree AND
re-run cmake configure before building:

```bash
# from repo root (adjust 'build/' if your tree differs):
rm -rf build/plugins/TerrainInstrument/juce_binarydata_TerrainInstrument_WebUI \
       build/plugins/TerrainInstrument/CMakeFiles/TerrainInstrument_WebUI.dir \
       build/plugins/TerrainInstrument/libTerrainInstrument_WebUI.a
cmake -S . -B build -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES="arm64"
cmake --build build --target TerrainInstrument_VST3 --target TerrainInstrument_AU
```
**Verify it embedded:** `strings "$HOME/Library/Audio/Plug-Ins/VST3/TerrainInstrument.vst3/Contents/MacOS/TerrainInstrument" | grep -c "<a-string-from-the-new-html>"` -> non-zero. (Grepping BinaryData*.cpp returns 0 because it's numeric byte arrays — use `strings` on the compiled binary.)

### C. Generator / configure (current reality on Max's Mac)
- **No full Xcode** on this Mac -> use the **`"Unix Makefiles"`** generator, NOT `"Xcode"`.
  (This overrides the `-G Xcode` example in `juce-build-protocols.md`.)
- `./scripts/build-and-install.sh` has been **broken on this Mac** -> use direct cmake.
- arm64 only (Max's machine). Switching generators requires `rm build/CMakeCache.txt build/CMakeFiles`.
- **Max — confirm this is still your current build command.** If you've fixed the script
  or moved to the Xcode generator / universal binary, tell the chat side and we'll update this.

### D. Install paths (macOS)
- VST3: `~/Library/Audio/Plug-Ins/VST3/`
- AU:   `~/Library/Audio/Plug-Ins/Components/`

---

## 3. NAMING LOCK (do not violate)

- **`Terrain Instrument`** = the **synth** (this project). Dir `plugins/TerrainInstrument/`,
  bundle `com.wavescrate.terraininstrument`, AU subtype `Tern`.
- **`Terrain` / `Terrain FX`** = the separate **FX/sampler** plugin. Dir `plugins/Terrain/`,
  bundle `com.wavescrate.terrain`, AU subtype `Trrn`. Different plugin, different source.
- **Never** rename TerrainInstrument back to "Terrain" (bundle-ID collision — was deliberately
  removed). **Never** create "Terrain 2". Company: **Waves Crate**, mfr code `Wvcr`.
- All synth work lives in `plugins/TerrainInstrument/`. Don't touch FX data paths.

---

## 4. Critical engineering gotchas (carried forward)

- **WebSliderRelay = 4-point binding, or it silently no-ops.** Any new APVTS param exposed to
  the WebView needs: (1) relay member in PluginEditor.h, (2) `.withOptionsFrom(relay)` in the
  constructor, (3) a `WebSliderParameterAttachment` created against the APVTS param, (4) the JS
  side reading it. Miss one -> JS writes vanish, audio thread reads default forever, no error.
- **JS API:** use `window.Juce.getSliderState(id)` and `window.Juce.getNativeFunction('name')`
  — NOT `window.__JUCE__...` (silently fails).
- **Inline all JS into the HTML.** No ES `import`/`export`, no `juce/` subfolder — WKWebView
  custom-scheme + BinaryData lookups break with modules / duplicate basenames.
- **Don't move styled elements out of `#syn-panel`** (e.g. a menu to `document.body`) — every
  CSS rule is scoped under `#syn-panel`; outside it nothing matches and the element renders
  invisibly. Keep menus inside, clamp height to the panel.
- **Over-length C++ raw strings truncate (~245KB) silently** -> breaks the injected `<script>`.
  Split into <60KB `juce::String(R"(...)")` pieces. (Relevant if HTML is ever embedded as a raw
  string rather than BinaryData.)
- **AudioParameterChoice:** `getRawParameterValue()` returns normalized [0,1], not the index.
- **New SYN knobs:** prefer a default of 0.5 over 0/1 — dblclick-reset to an extreme can hide
  itself as a "snap-back" bug. Suppress dblclick within 400ms of a real drag.

---

## 5. Design + process rules (from Max)

- **UI spacing/alignment is Max's #1 principle.** Align the *visible glyph* (not the CSS box)
  to elements above/below; expect 1–3px nudges. References: Prophet 5, PolyBrute. Don't claim
  UI "done" until it passes this.
- **Every filter response curve must mirror the DSP and MOVE with the knobs** — no flat
  placeholder lines, ever. Applies to the upcoming wavetable work too (no flat spectrums).
- **Defer to manuals + research, never guess from training** when referencing other
  synths/tools for design. Read the actual manual or do real research first.
- **Build a standalone HTML mockup and get sign-off before wiring UI into the plugin.**

---

## 6. Current state & roadmap (as of 2026-06-05)

- **Filter system COMPLETE** — 27 types, 7 DSP cores, all offline-validated, both formats built.
  Branch `feature/terrain-instrument`.
- **TWO independent filters** just added (this session): `filterSlot_` + `filterSlot2_` in
  SynthVoice — series/parallel routing (`SYN_FILTER_ROUTING`) + per-filter wet/dry mix
  (`SYN_FILTER1_MIX`/`SYN_FILTER2_MIX`), fully independent, no linking. Front-panel 1/2 tabs
  switch the active filter; a "+" opens the back panel (MIX + routing). Relays registered.
- **Next, per Max's plan:** (1) fold the whole filter system into a `filter` sub-skill under a
  master **`VST Plugins`** skill; (2) deep **wavetable synthesis** rework (anti-aliased
  mip-mapped band-limited tables, better interpolation, spectral richness — Serum-tier).

---

## 7. Where the deep history lives
- `MEMORY.md` — index of ~113 per-topic memory files (the full project brain).
- `.claude/rules/agent.md` — APC phase dispatcher (/dream /plan /design /impl /ship), OS
  protocol, troubleshooting auto-capture.
- `.claude/rules/juce-build-protocols.md` — general JUCE 8/CMake rules (note conflicts
  resolved in sections 1–2 above for THIS plugin).
- `.claude/rules/file-naming-conventions.md` — directory layout per plugin.
