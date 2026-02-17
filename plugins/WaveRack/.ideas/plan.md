# Wave Rack — Implementation Plan

## Complexity Score: 5/5

## Implementation Strategy: Phased (6 phases)

All files generated programmatically (`.maxpat` JSON + `.js`). No manual Max editor work. User loads device in Ableton for testing.

---

## Phase 1: Core Timing Engine + Basic MIDI Output

**Goal:** A single step sequence triggering MIDI notes in sync with Ableton's transport.

**Files created:**
- [ ] `Source/patchers/wr-main.maxpat` (skeleton — compact view + subpatcher hosting)
- [ ] `Source/patchers/wr-timing.maxpat` (plugsync~ → step derivation → trigger)
- [ ] `Source/patchers/wr-midi-out.maxpat` (makenote → MIDI outlet)
- [ ] `Source/js/wr-midi-logic.js` (hardcoded 4-on-floor kick pattern for testing)

**Deliverable:** Load device on MIDI track before Drum Rack → press play → hear kicks on beats 1-4 at correct tempo.

**Test criteria:**
- Steps fire exactly on beat, zero drift over 8+ bars
- Play/stop correctly starts/stops sequencing
- Tempo changes handled seamlessly
- No double triggers, no missed triggers

**Risk:** `plugsync~` outlet behavior varies. Test both `plugsync~` and `phasor~ 16n @lock 1`.

---

## Phase 2: Data Layer

**Goal:** Full dict schema with pattern storage, read/write, and persistence across save/load.

**Files created:**
- [ ] `Source/patchers/wr-data.maxpat` (unnamed dict with @parameter_enable 1)
- [ ] `Source/js/wr-engine.js` (dict initialization, read/write, pattern switching)

**Files modified:**
- [ ] `wr-midi-logic.js` — reads from dict instead of hardcoded data
- [ ] `wr-main.maxpat` — add data subpatcher wiring

**Deliverable:** Steps read from dict → patterns switchable → save Live set → reopen → all data persists.

**Test criteria:**
- Dict data survives Live set save/load cycle
- Pattern A/B/C/D switch correctly swaps step data
- 8 channels output correct MIDI notes per dict config
- Default GM drum mapping initializes on first load

---

## Phase 3: UI Rendering

**Goal:** Full step grid in a popup window with visual playback cursor.

**Files created:**
- [ ] `Source/patchers/wr-popup.maxpat` (floating window with jsui objects)
- [ ] `Source/js/wr-ui-grid.js` (MGraphics step grid canvas)
- [ ] `Source/js/wr-ui-controls.js` (MGraphics channel strip)

**Files modified:**
- [ ] `wr-main.maxpat` — add "Open Wave Rack" button, popup trigger

**Deliverable:** Click button → popup opens → 8 channels × 16 steps grid → playback cursor moves with transport.

**Test criteria:**
- Popup window opens and closes cleanly
- Grid renders all channels with correct colors
- Playback cursor tracks transport accurately at 33fps
- No CPU spikes during rendering
- Closing and reopening popup preserves state

---

## Phase 4: Interaction

**Goal:** Full mouse interaction — step toggling, mute/solo, pattern switching in the UI.

**Files modified:**
- [ ] `wr-ui-grid.js` — onclick/ondrag handlers for step toggle and paint mode
- [ ] `wr-ui-controls.js` — click handlers for mute/solo/volume
- [ ] `wr-engine.js` — wire UI events to dict writes and broadcasts
- [ ] `wr-popup.maxpat` — add pattern selector buttons, step count selector

**Deliverable:** Click steps to toggle → hear MIDI change. Mute/solo channels. Switch patterns. Drag-paint steps.

**Test criteria:**
- Step toggle immediately affects MIDI output
- Mute silences channel, solo isolates channel
- Pattern switch changes visible grid and MIDI output
- Drag painting works across multiple steps
- Accent steps (shift+click) play at higher velocity
- Volume slider scales channel velocity

---

## Phase 5: Smart MIDI Routing

**Goal:** LiveAPI reads Drum Rack pad names. Drag-drop auto-assigns MIDI notes.

**Files created:**
- [ ] `Source/patchers/wr-liveapi.maxpat` (live.drop + LiveAPI objects)
- [ ] `Source/js/wr-liveapi-bridge.js` (LiveAPI queries + file drop handling)

**Files modified:**
- [ ] `wr-engine.js` — handle assign_channel from LiveAPI bridge
- [ ] `wr-ui-controls.js` — display Drum Rack pad names

**Deliverable:** Place before Drum Rack → pad names appear. Drag .wav → new channel created with auto-assigned note.

**Test criteria:**
- Drum Rack pad names appear in channel strip
- Dragging a .wav creates new channel with correct MIDI note
- Auto-assignment skips occupied notes
- Graceful fallback when no Drum Rack present

---

## Phase 6: Polish

**Goal:** Production-quality swing, resizing, virtual scrolling, polyrhythmic lengths.

**Files modified:**
- [ ] `wr-timing.maxpat` — add swing (phasor ramp distortion)
- [ ] `wr-ui-grid.js` — virtual scrolling, resize handling, beat markers, LED-style cells
- [ ] `wr-ui-controls.js` — channel color picker, scrollbar sync
- [ ] `wr-popup.maxpat` — resize handling, window position persistence
- [ ] `wr-engine.js` — per-channel step counts (polyrhythmic)
- [ ] `wr-midi-logic.js` — polyrhythmic step modulo, step resolution switching

**Files added:**
- [ ] `wr-main.maxpat` — add live.dial for swing, live.menu for resolution

**Deliverable:** Swing sounds musical. Window resizes cleanly. 32 channels with virtual scroll. Polyrhythmic patterns work.

**Test criteria:**
- Swing sounds correct at 25%, 50%, 75%, 100%
- Window resizes without visual glitches
- Window position/size persists across save/load
- 32 channels at 32 steps under 5% CPU
- Per-channel step counts produce polyrhythmic patterns
- All step resolutions (1/8, 1/16, 1/32, 1/16T) work correctly

---

## Dependencies

### Max for Live Objects Used
- `plugsync~` — transport sync (audio rate)
- `dict` — JSON data storage
- `live.menu` — automatable dropdown
- `live.dial` — automatable knob
- `live.text` — button/toggle
- `live.drop` — drag-and-drop file detection
- `live.path` / `live.object` — LiveAPI access
- `jsui` — JavaScript canvas (MGraphics)
- `js` — JavaScript logic
- `makenote` — note on/off with duration
- `qmetro` — UI refresh timer
- `thispatcher` — popup window management
- `send` / `receive` — device-local messaging

### No External Dependencies
Wave Rack uses only built-in Max/MSP and M4L objects. No third-party externals or packages required.

---

## Risk Assessment

| Risk | Severity | Mitigation |
|------|----------|------------|
| Timing accuracy (plugsync~ behavior) | HIGH | Prototype first. Test both plugsync~ and phasor~. |
| Dict nesting performance | MEDIUM-HIGH | JS caching — never read dict during playback |
| jsui rendering at scale (32+ ch) | MEDIUM | Virtual scrolling, dirty rectangles, 20fps fallback |
| Popup window lifecycle | MEDIUM | Timer autosave, never rely on closebang |
| LiveAPI Drum Rack access | MEDIUM | Graceful fallback, read-only in v1.0 |
| .maxpat JSON generation accuracy | MEDIUM | Validate generated JSON, test incrementally |
