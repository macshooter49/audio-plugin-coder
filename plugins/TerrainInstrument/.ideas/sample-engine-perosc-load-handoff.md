# Per-OSC Sample Load — C++ Contract (for Opus)

**Goal:** each of the 4 Sample oscillators (A/B/C/D) holds **its own** loaded sample — drop a different one-shot on each. Replaces the v1 shared `getSampleBuffer()` read (where all SAMP oscillators read the editing-layer buffer). The Serum-style payoff.

**Status:** the **UI side is built + installed** (commit pending). It mirrors the proven front-sampler load pattern (`readAsBase64` → `loadSampleFromBase64` → `onSampleLoaded(peaks)`). It calls 3 native hooks (below) — all guarded, so nothing breaks until you add them; they just no-op today. The waveform stays a synthetic placeholder until a real load fires `onOscSampleLoaded`.

---

## What the UI already does (so you wire to it)

1. **Drop** a file on a Sample oscillator's waveform (`.samp-disp`):
   - JS reads it via `FileReader.readAsDataURL` → base64, then calls:
     ```
     window.Juce.getNativeFunction('loadSampleForOsc')(osc, filename, base64)
     ```
   - `osc` = the **lowercase letter** `'a' | 'b' | 'c' | 'd'`. `filename` = e.g. `"Morning Bass F2.wav"`. `base64` = raw file bytes, base64 (no data-URL prefix).
2. **On engine→Sample** (and editor reopen), JS requests any cached sample for that osc:
     ```
     window.Juce.getNativeFunction('getOscSamplePayload')(osc)  // returns a Promise<json string|"">
     ```
   and feeds the result to `window.onOscSampleLoaded(osc, json)`.
3. **Draw hook** the UI exposes for you to call after a load completes:
     ```
     window.onOscSampleLoaded(osc, payload)
     ```
   `payload` = the **same shape as the front sampler's `onSampleLoaded`** (string or object):
     ```json
     { "filename":"Morning Bass F2.wav", "sampleRate":48000, "lengthSamples":96000,
       "numChannels":2, "peaksMin":[-0.4,...], "peaksMax":[0.6,...] }
     ```
   The UI draws `peaksMin/peaksMax` into the oscillator waveform and marks it loaded. (Only peaks + filename are required for drawing; the rest is metadata.)

---

## C++ to add

### 1. Per-OSC buffers
Add **4 independent `tw::SampleBuffer`** for the Sample oscillators (recommend keeping them **separate from the 4 sampler `layers[]`** — the synth oscillators are a distinct context from the big-brother SAMP pads; Max: *"keep it on the synth side"*). E.g. `std::array<tw::SampleBuffer,4> oscSampleBuffers;` in the processor, with `getOscSampleBuffer(int oscIdx)`.

*(Alternative if you prefer minimal new infra: map osc A/B/C/D → `layers[0..3].sampleBuffer` and reuse `loadSampleIntoLayer`. Cleaner separation argues for dedicated buffers; your call.)*

### 2. Wire the engine to read the per-OSC buffer
In `PluginProcessor.cpp` (~2920) the SAMP voice render currently does
`tw::SampleBuffer* sampleSrcPtr = &getSampleBuffer();` (shared). Make each OSC's `SampleEngine` read **its own** `getOscSampleBuffer(oscIdx)` instead, so A/B/C/D are independent.

### 3. Native functions (register in PluginEditor.cpp, mirror `loadSampleFromBase64` @1909)
- **`loadSampleForOsc(osc, filename, base64)`** — map `osc` letter→idx, base64-decode → temp file (reuse the `Terrain-Instrument-Drops` temp dir) → load into `oscSampleBuffers[idx]` (async loader, like `loadSampleIntoLayer`). On completion build the peaks JSON and push:
  `webView->evaluateJavascript("if(window.onOscSampleLoaded) window.onOscSampleLoaded('" + oscLetter + "'," + json + ");")`.
  Cache the JSON per-osc for reopen. Auto-map root note from the filename suffix (C3 default, MIDI 69 = A3) — reuse the front sampler's logic if present.
- **`getOscSamplePayload(osc)`** — return the cached peaks JSON for that osc (or `""`). Parallels `getCachedSamplePayload` (@840).

### 4. Native file-drop coordination (gotcha)
The editor is a `FileDragAndDropTarget`; `filesDropped` (@9332) loads to `editingLayer` (a sampler pad). On the **synth page**, a waveform drop must go to the **oscillator buffer**, not a sampler layer. The UI handles synth-page drops in JS (`drop` → `loadSampleForOsc`). Ensure the native `filesDropped` doesn't *also* fire for those (page-aware guard, or rely on the WebView consuming the JS `drop`). Flagging so we don't double-load.

### 5. Serialization
Persist each osc's sample path like `layers[].sourcePath` (in `getStateInformation` ValueTree); on `setStateInformation` + editor-construct, async-reload each osc buffer from its saved path (mirror the `loadSampleIntoLayer` reload loop @2772). Then `getOscSamplePayload` returns the restored peaks so the UI redraws on reopen.

---

## Summary — the 3 hooks the UI calls (names are the contract)
| Direction | Name | Args | Returns |
|---|---|---|---|
| JS→C++ | `loadSampleForOsc` | `(osc, filename, base64)` | — |
| JS→C++ | `getOscSamplePayload` | `(osc)` | Promise<json string \| ""> |
| C++→JS | `window.onOscSampleLoaded` | `(osc, payloadJson)` | — |

`osc` ∈ `{'a','b','c','d'}`. `payload` shape = front sampler's `onSampleLoaded` (peaksMin/peaksMax/filename/sampleRate/lengthSamples/numChannels). Rename any of these and tell me — it's a one-line UI change each.
