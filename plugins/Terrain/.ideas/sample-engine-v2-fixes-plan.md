# Sample Engine — Review Fixes Plan (Opus ↔ CC exchange)

From Max's first full audition (2026-06-26). Sampler sounds great; these are the fixes/features.
**Owner key:** **[CC]** = UI (index.html), **[OPUS]** = DSP/C++, **[BOTH]** = needs a C++ feed + UI draw.
Ordered by priority.

---

## P0 — Drag-drop separation (synth osc ↔ front multisampler) — Max's #1, adamant
**Symptom:** dropping a one-shot on a *synth* Sample oscillator ALSO loads it into the *front-panel* multisampler layer. They must be fully separate (combining is a 2.0 thing).
**Diagnosis — two load paths fire on a synth-page drop:**
1. The front sampler's **`document`-level JS `drop` handler** (PluginEditor.cpp:3276) → `loadSampleFromBase64` → editingLayer.
   → **[CC] DONE** (shipped `557cc7b`): `e.stopPropagation()` on the `.samp-disp` drag/drop handlers stops the bubble.
2. The **native OS file drop** `filesDropped` (PluginEditor.cpp:9393) → `loadSampleAsync` → editingLayer. **Not page-aware**, so it fires on the synth page too. ← this is what's still bleeding through.
   → **[OPUS]** ~3-line guard using the **already-existing** `setSynthView(bool)` signal (the UI already calls it on page switch; today it only sets `captureDragStrip.synthViewActive`). Add an editor-level flag set in that same lambda, then in `filesDropped`, **after** the `.terrain`/`.terrainpack` patch handling (those must still load), guard the audio path:
   ```cpp
   if (synthPageActive) return;   // synth-page audio drops are handled per-osc by the JS layer
   loadSampleAsync (f);
   ```
**Verify:** on synth page, drop on osc B → loads only osc B, nothing on the front sampler. Off synth page, front-sampler drops still work.

---

## P1 — Scan: reverse (left half) is broken
**Symptom:** full-right = 2× forward (works). Full-left = slows then **stops** — should be **2× reverse** (plays backwards, same pitch). Center = no scan.
**[OPUS]** `scanToRate()` in SampleEngine.h (Opus flagged this as the one-liner). Map bipolar: full-left → −2×, center → 0, full-right → +2×; the engine must actually advance the read position **negatively** (reverse) in loop AND one-shot, looping backwards when in a loop mode.
**⚠️ Model to pin with Max:** Max says "center = doesn't scan at all" *and* "it still plays." For a sample, SCAN is the traversal rate, so center = 0 = frozen (no advance). Decide: is SCAN a **bipolar scrub rate added on top of normal keytracked playback** (center = normal 1× play, left = reverse, right = faster — Serum's "one notch right = normal" feel), or **center = frozen**? Max's words point at *Serum's notch model* (center frozen, the playable default sits just right of center). **Recommend:** center = 0 (frozen), and the engine's *default* SCAN value sits at a "normal forward 1×" point, not dead center — matches what Max described. Opus + Max to confirm the exact curve.
**[CC]** follower must show reverse motion (handled by the follower rework below).

## P1 — Reverse LOOP mode doesn't reverse
**[OPUS]** the `Reverse` loop-mode option in SampleEngine plays forward. Fix the reverse-loop playback (+ Ping-Pong while there). **[CC]** follower direction follows it.

## P1 — X-Fade doesn't work + clicks at the loop seam
**Symptom:** XFADE param has no audible effect; loop seam clicks.
**[OPUS]** the offline harness proved equal-power xfade (seam 4.8e-4→1.9e-4) but it's not engaging in-plugin — trace the XFADE param → `setXFade` → `recomputeRegion` path, and confirm the loop-seam crossfade actually runs at the loopEnd boundary. Research equal-power loop crossfade. Ties into the loop-catch behavior below.
**[CC]** the shift-drag fade visual is "sucky" — redo it Ableton/FL-style (drag the edge → a real slanted fade that grays/blurs the faded region; visible while dragging). I'll redo once the param path is live so the picture matches the sound.

## P1 — Spray doesn't work
**[OPUS]** per-note random start scatter is inert in-plugin (harness tested spray=0 deterministic + spray>0 random-on-note via seed). Trace why the per-note seed/offset isn't applied on note-on.

## P0/P1 — Follower attached to MIDI (the big one)
**Symptom:** the follower free-runs (fake rAF animation), only shows in loop mode, isn't tied to MIDI. Wrong.
**Want (Serum / our front sampler):** the follower = the **actual playback read-position**, **retriggers on every note-on**, **fades out** when the note/scan ends, works in **one-shot AND loop**, always retriggered, sample-accurate to the audio.
**[OPUS]** push the voice's real sample read-position for active notes to the UI — a throttled (~30–60 Hz) feed, e.g. `window.updateSampleFollower(osc, pos01, active)` (mirror however the front-sampler playhead is already fed).
**[CC]** draw the follower from that position + fade-out envelope; **delete the rAF fake**; show it in one-shot too. I'll mirror the front sampler's MIDI-follower (Max: "ask CC how it works" — I'll read that path and match it).

## P1 — Loop-catch behavior (Serum model) — design
**Want:** loop modes should NOT instantly jump to the loop. The note plays as a **one-shot from Sample Start**, rings out, and only when the playhead **reaches the loop region (LS)** does it get **"caught"** and start looping (fwd/rev/ping-pong/tailed). So: Sample-Start..LS is the intro, LS..LE is the trap. Needs the loop xfade for a clean, click-free catch.
**[OPUS]** SampleEngine playback logic: free-run from regionStart; on first crossing into [loopStart,loopEnd], engage the loop mode. **[CC]** maybe a subtle visual once "caught" (later).

---

## P2 — Right-click modes (stretch / formant), like the wavetable warp/fold right-click
**Want:** right-click the **STRETCH** knob → choose **Tones / Texture / Beats** + special settings; right-click **FORMANT** → a creative/weird formant mode. Parity with how WT WARP/FOLD expose modes on right-click.
**[OPUS]** add `SYN_OSC_x_SAMPLE_STRETCH_MODE` (choice: Tones/Texture/Beats) + `SAMPLE_FORMANT_MODE` (choice incl. a creative "weird" one) params, and wire them in SampleEngine/WarpProcessor.
**[CC]** the right-click glass menu on those knobs, wired to the new choice params (I already have the menu infra + the choice-param read/write pattern from the loop-mode/snap dropdowns).

## P2 — A creative/weird FORMANT timbre mode
**[OPUS]** DSP idea — a second formant mode beyond the current shift (something unique to our sampler). Surfaced via the right-click above.

## P2.5 — Creative per-knob features (GREENLIT, STANDBY — build AFTER the base mode menus + fix-verification)
Max approved all four. Each = a small UI menu addition (CC) + a real DSP mode (Opus), layered on top of the base STRETCH/FORMANT mode menus. **Do NOT start until the main priorities below are wrapped.**

1. **STRETCH → Freeze** — at max stretch (or a Freeze toggle), the read position stops advancing and the engine holds the current grain/spectral snapshot as **infinite sustain** (drum hit / vocal → frozen pad). [OPUS] freeze in the warp/stretch path (`SAMPLE_STRETCH_FREEZE` bool or fold into STRETCH_MODE). [CC] toggle in the STRETCH right-click.
2. **FORMANT → Vowel** — FORMANT knob morphs through **A-E-I-O-U** vowel shapes (talkbox on any sample). [OPUS] vowel formant bank — **reuse the existing Hillenbrand-1995 vowel data** already in the wavetable Vocal presets (Choir/Whisper/VowelMorph). [CC] "Vowel" added to FORMANT_MODE menu.
3. **SPRAY → Multi-target** (the beat-Serum signature) — per-note scatter of **Start + Pitch (micro-detune) + Pan + Reverse-chance (% notes play backward)**, selectable targets, + Mode **Random / Round-Robin / Drift**. [OPUS] `SAMPLE_SPRAY_TARGETS` (bitmask/bools) + `SAMPLE_SPRAY_MODE` (choice); SPRAY knob = intensity. [CC] targets multi-select + mode in the SPRAY right-click.
4. **X-FADE → Smear** — past the clean-loop point the crossfade **overlaps the whole loop region** (not just the seam) → diffuse ambient wash; tight loop → evolving pad. [OPUS] `SAMPLE_XFADE_SMEAR` bool + `SAMPLE_XFADE_CURVE` (Equal-Power/Linear/S). [CC] curve + Smear in the X-Fade right-click.

## MAIN PRIORITIES to wrap FIRST (before P2.5)
1. **Base STRETCH/FORMANT mode menus** (the originally-locked Contract 2). [OPUS] add `SAMPLE_STRETCH_MODE` [Tones,Beats,Texture] + `SAMPLE_FORMANT_MODE` [Normal,Inverted,Cross-Formant,Spectral-Tilt] params + the DSP — he has the `Warp/` source now (`~/Desktop/terrain-warp-source-for-opus-8f57973.zip`). [CC] builds the right-click menus the instant those params land.
2. **Verify the recent DSP fixes by ear** (Max): SCAN reverse (left=2× back, loads at 1×), X-Fade on a **mid-sample** loop in Forward/Reverse, Reverse loop (lead-in→snap-to-end backward), the MIDI follower, and especially **SPRAY** (max it, one-shot, no loop, repeated notes → different start each time; if nothing, Opus adds a debug readout).

## P3 — Embed samples into presets (future)
**[BOTH, deferred]** when we build the preset section: embed sample audio into the preset (base64 in state) so patches are self-contained. Today state stores paths only. Noted, not now.

---

## Split summary
- **CC tonight:** this plan + the JS drag-drop half (already shipped). 
- **CC next (once Opus's pieces land):** follower draw-from-position + fade-out (drop the rAF fake); right-click stretch/formant menus (need the mode params); redo the shift-drag fade visual (need xfade working).
- **OPUS:** filesDropped synth-page guard (P0 #2); scan reverse mapping + model decision; reverse/ping-pong loop playback; xfade engaging + click-free; spray; the follower position feed; loop-catch behavior; stretch/formant mode params + the creative formant DSP.
- **Pin with Max:** the SCAN center model (frozen vs normal-1×-just-right-of-center).
