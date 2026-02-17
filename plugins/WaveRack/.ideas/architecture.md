# Wave Rack — Architecture Document

## Overview

Wave Rack is a Max for Live MIDI Effect (.amxd) that replicates FL Studio's Channel Rack step sequencer workflow inside Ableton Live. It sits on a MIDI track before a Drum Rack, outputting MIDI note-on/note-off messages to trigger pads. The UI is a custom `jsui` canvas rendered via MGraphics in a popup floating window. Timing uses `plugsync~` for sample-accurate transport sync. Data persists via unnamed `dict` objects with `@parameter_enable 1`.

**All files are generated programmatically** — both `.maxpat` patcher JSON and `.js` JavaScript files. No manual Max editor work required.

---

## 1. File/Component Architecture

### Directory Structure

```
plugins/WaveRack/
├── .ideas/
│   ├── creative-brief.md
│   ├── parameter-spec.md
│   ├── architecture.md            # THIS DOCUMENT
│   └── plan.md
├── Design/
│   ├── v1-ui-spec.md
│   └── v1-style-guide.md
├── Source/
│   ├── patchers/
│   │   ├── wr-main.maxpat         # Root device patcher (compact view + subpatcher host)
│   │   ├── wr-popup.maxpat        # Popup window patcher (full UI)
│   │   ├── wr-timing.maxpat       # Timing engine subpatcher
│   │   ├── wr-midi-out.maxpat     # MIDI output subpatcher
│   │   ├── wr-data.maxpat         # Data management subpatcher
│   │   └── wr-liveapi.maxpat      # LiveAPI bridge subpatcher
│   └── js/
│       ├── wr-ui-grid.js          # jsui: Main step grid canvas
│       ├── wr-ui-controls.js      # jsui: Channel strip controls
│       ├── wr-engine.js           # js: Data engine (dict read/write)
│       ├── wr-liveapi-bridge.js   # js: LiveAPI JavaScript bridge
│       └── wr-midi-logic.js       # js: MIDI note scheduling logic
├── status.json
└── README.md
```

### Naming Convention

All files use the `wr-` prefix to avoid name collisions in Max's global namespace. JavaScript files for `jsui` objects (visual rendering) use `wr-ui-`. JavaScript files for `js` objects (logic/data) use `wr-` without `ui`. Patcher files all use `wr-`.

### Component Responsibilities

**Patchers:**
- `wr-main.maxpat` — Root device patcher loaded by Live. Compact device view (pattern selector, "Open Wave Rack" button, transport status). Hosts all subpatchers. Becomes `.amxd` when frozen.
- `wr-popup.maxpat` — Floating window opened via `thispatcher`. Contains the full-size `jsui` grid, channel controls, pattern selector, and transport controls. Embedded as a subpatcher within `wr-main` so it shares the `---` namespaced send/receive channels.
- `wr-timing.maxpat` — Audio-rate timing engine. Contains `plugsync~` for transport sync, step counter derivation, swing offset computation, and `edge~`-based step trigger generation.
- `wr-midi-out.maxpat` — Receives note events, applies `makenote` for gate length, outputs through M4L MIDI outlet on channel 1.
- `wr-data.maxpat` — Houses the master `dict` object (unnamed, `@parameter_enable 1`, `@embed 0`) for Live Set persistence.
- `wr-liveapi.maxpat` — Contains `live.path` and `live.object` for querying the downstream Drum Rack. Also contains `live.drop` for audio file drag-and-drop detection.

**JavaScript:**
- `wr-ui-grid.js` — Primary `jsui` rendering script. Draws the step grid (channels as rows, steps as columns), playback cursor, step states (off/on/accent), velocity bars. Handles mouse click/drag for step toggling. Implements virtual scrolling for 32+ channels.
- `wr-ui-controls.js` — Secondary `jsui` for the channel strip area (left panel). Renders channel names, colors, mute/solo buttons, volume sliders, MIDI note labels.
- `wr-engine.js` — Non-visual `js` object. Central data coordinator. Reads/writes the master dict, manages pattern switching logic, validates data integrity, broadcasts state changes to UI components. Single source of truth.
- `wr-liveapi-bridge.js` — Non-visual `js` object using the `LiveAPI` JavaScript class. Queries the Drum Rack device downstream for pad names, occupied notes, and device structure.
- `wr-midi-logic.js` — Non-visual `js` object. Receives step trigger messages from the timing engine, looks up active channels and step states from cached data, computes velocity scaling, sends note-on/note-off messages.

---

## 2. Signal/Data Flows

### 2a. Transport → Step Trigger → MIDI Output

```
Ableton Transport (play/stop/tempo/time sig)
        |
        v
[plugsync~] (audio rate)
        |
        ├── outlet 1: transport playing (0/1)
        ├── outlet 2: current bar position (float, 0.0-1.0)
        |
        v
[*~ 16] (multiply by step count → 0.0-16.0)
        |
        v
[trunc~] (integer step index: 0, 1, 2, ... 15)
        |
        v
[change~] (only output when step changes)
        |
        v
[edge~] (signal change → control-rate bang)
        |
        ├── step_trigger (bang)
        ├── [snapshot~ 0] → step_index (int)
        |
        v
[wr-midi-logic.js]
        |
        ├── For each active, unmuted channel:
        │   ├── Read step_state (0=off, 1=on, 2=accent)
        │   ├── Read step_velocity
        │   ├── Compute: final_vel = step_vel × (ch_volume / 127)
        │   ├── If accent: final_vel = min(127, final_vel × 1.25)
        │   └── Output: [note_number, final_velocity, gate_ms]
        |
        v
[makenote] (with duration from gate length)
        |
        v
M4L MIDI outlet → Drum Rack
```

### 2b. User Click → Step Toggle → Dict Update

```
Mouse click on jsui grid
        |
        v
[wr-ui-grid.js] onclick(x, y, ...)
        |
        ├── Hit test → (channel_index, step_index)
        ├── If shift: cycle off → on → accent → off
        ├── If no modifier: toggle off ↔ on
        |
        v
outlet 0: "step_toggle ch step new_state"
        |
        v
[wr-engine.js]
        |
        ├── Writes to dict: patterns[active].channels[ch].steps[step] = state
        ├── If state changed to "on": set default velocity (100)
        ├── Broadcasts refresh
        |
        v
[send ---wr-ui-refresh] → jsui redraws
```

### 2c. Pattern Switch

```
User clicks Pattern B button
        |
        v
[wr-engine.js] pattern_switch(1)
        |
        ├── Updates global.active_pattern = 1
        ├── Reads new pattern data from dict
        ├── Broadcasts: "pattern_changed 1"
        |
        v
[send ---wr-pattern-changed]
        ├── → wr-ui-grid.js redraws with new step data
        ├── → wr-ui-controls.js redraws channel strip
        └── → wr-midi-logic.js switches to reading from new pattern
```

### 2d. Drag-and-Drop

```
User drags audio file from Live Browser
        |
        v
[live.drop] (in wr-liveapi.maxpat)
        |
        v
[wr-liveapi-bridge.js]
        |
        ├── Extracts filename for channel name
        ├── Finds next available MIDI note (from C1/36 upward)
        ├── Queries Drum Rack via LiveAPI for occupied pads
        |
        v
[wr-engine.js]
        |
        ├── Creates new channel in dict with name, note, color
        ├── Broadcasts: "channel_added"
        |
        v
UI redraws with new channel
```

---

## 3. Max Patcher Architecture

### 3a. Main Device Patcher (`wr-main.maxpat`)

Root patcher loaded by Live. Compact device view contents:
- `[live.menu]` — Pattern selection (A/B/C/D), automatable
- `[live.text]` — "Open Wave Rack" button, triggers popup
- `[live.dial]` — Swing amount (0-100%), automatable
- `[live.menu]` — Step resolution (1/8, 1/16, 1/32, 1/16T)
- Transport status indicator (synced to `plugsync~`)

Subpatcher hosting:
```
[p wr-timing]          — audio-rate timing engine
[p wr-midi-out]        — MIDI note output
[p wr-data]            — dict + persistence
[p wr-liveapi]         — LiveAPI + live.drop
[js wr-engine.js]      — central data coordinator
[js wr-midi-logic.js]  — step-to-MIDI conversion
```

Popup trigger:
```
[thispatcher]
  ← "script open wr-popup"
  ← "window flags float"
  ← "window exec"
```

### 3b. Popup Window (`wr-popup.maxpat`)

Embedded as subpatcher with `@window flags float`. Layout:
1. **Toolbar** — Pattern buttons, step count, transport, BPM, swing dial
2. **Channel strip** (jsui) — Left panel: names, colors, mute/solo, volume
3. **Step grid** (jsui) — Main area: rows=channels, columns=steps
4. **Scroll controls** — Vertical scrollbar for 32+ channels

Key objects:
```
[jsui @jsfile wr-ui-grid.js @size 800 400]
[jsui @jsfile wr-ui-controls.js @size 200 400]
[qmetro 30]  — 33fps UI refresh
```

### 3c. Timing Engine (`wr-timing.maxpat`)

Audio-rate signal chain:
```
[plugsync~]
    |
    outlet 2 (bar position 0.0-1.0)
    |
    [*~ 16]  ← step_count
    |
    [trunc~] ← raw step index
    |
    [change~]
    |
    [edge~]  ← step trigger bang
    |
    [snapshot~ 0] ← step index int
```

**Swing implementation:** Two-segment linear ramp distortion applied before step derivation. Even steps (0, 2, 4...) unchanged. Odd steps (1, 3, 5...) delayed by `swing_amount`. `swing_amount` (0-100%) maps to `swing_offset` (0.0-0.5).

**Transport handling:** `plugsync~` outlet 1 detects play/stop. On stop, `[gate~]` mutes the timing chain. Tempo changes handled automatically.

### 3d. MIDI Output (`wr-midi-out.maxpat`)

```
[receive ---wr-midi-note]
    |
    [unpack i i i]  ← (note, velocity, duration_ms)
    |
    [makenote]
    |
    M4L MIDI outlet (channel 1)
```

Gate length: `step_duration_ms × 0.8` where `step_duration = 60000 / (tempo × resolution_multiplier)`

### 3e. Data Management (`wr-data.maxpat`)

```
[dict @parameter_enable 1 @embed 0]  ← unnamed, Live-managed persistence
```

**Critical dict rules:**
1. Dict MUST be unnamed (auto-generated name). With `@parameter_enable 1`, Live manages persistence.
2. `@embed 0` — data saves with Live Set, not embedded in device file.
3. Do NOT use `pattrstorage` — patterns managed as dict sub-keys instead.

### 3f. LiveAPI Bridge (`wr-liveapi.maxpat`)

```
[live.drop @parameter_enable 1]  ← drag-drop zone
    |
    [wr-liveapi-bridge.js]
    |
    Uses LiveAPI JS class:
    ├── "this_device" → parent track → find DrumGroupDevice
    ├── Read drum_pads → note, name, occupied status
    └── Output pad mappings
```

---

## 4. JavaScript Module Design

### 4a. `wr-ui-grid.js` (jsui — Main Step Grid)

**Purpose:** Renders the step sequencer grid. Handles all mouse interaction on the grid.

**Key functions:**
- `paint()` — Main render: background, grid lines, step cells, velocity bars, playback cursor
- `onclick(x, y, ...)` — Hit test → toggle step, with shift modifier for accent cycling
- `ondrag(x, y, ...)` — Paint mode: drag across steps to toggle multiple
- `step_data(json)` — Receive cached step data from engine, redraw
- `playback_position(step)` — Update cursor position
- `scroll_to(offset)` — Virtual scroll offset

**Messages in:** `step_data`, `playback_position`, `scroll_to`, `resize`, `refresh`
**Messages out:** `step_toggle <ch> <step> <state>`, `step_set <ch> <step> <state>`

### 4b. `wr-ui-controls.js` (jsui — Channel Strip)

**Purpose:** Renders left-side channel strip: names, colors, mute/solo, volume, MIDI note labels.

**Key functions:**
- `paint()` — Render all visible channels with controls
- `onclick(x, y, ...)` — Hit test regions (mute, solo, volume, name, color)
- `channel_data(json)` — Receive channel metadata from engine

**Messages in:** `channel_data`, `scroll_to`
**Messages out:** `mute_toggle <ch>`, `solo_toggle <ch>`, `volume_set <ch> <val>`

### 4c. `wr-engine.js` (js — Data Engine)

**Purpose:** Central coordinator. Single source of truth. All dict read/write goes through here.

**Key functions:**
- `loadbang()` — Initialize default data if dict is empty (8 channels, 4 patterns, GM drum mapping)
- `step_toggle(ch, step, state)` — Write step state to dict, broadcast refresh
- `pattern_switch(idx)` — Update active_pattern in dict, broadcast
- `mute_toggle(ch)` — Toggle mute, handle solo exclusivity
- `get_step_data(offset, count)` — Read visible channels from dict, output to jsui
- `assign_channel(ch, note, name, color)` — Create/update channel from drag-drop
- `copy_pattern(from, to)` — Deep copy pattern data in dict

**Messages in:** All UI events (step_toggle, mute_toggle, solo_toggle, volume_set, pattern_switch, etc.)
**Messages out:** Refresh broadcasts via `send ---wr-*` channels

### 4d. `wr-liveapi-bridge.js` (js — LiveAPI Bridge)

**Purpose:** Queries Drum Rack for pad names/mappings. Handles file drop auto-assignment.

**Key functions:**
- `loadbang()` — Delayed Drum Rack query (1s after load)
- `queryDrumRack()` — Navigate LiveAPI: this_device → track → DrumGroupDevice → drum_pads
- `file_dropped(path, type)` — Extract filename, request next available MIDI note

**Messages in:** `file_dropped`, `refresh_pads`
**Messages out:** `pad_mappings`, `assign_channel`

### 4e. `wr-midi-logic.js` (js — MIDI Scheduling)

**Purpose:** Converts step triggers into MIDI note messages.

**Key functions:**
- `step_trigger(stepIndex)` — For each channel: check mute/solo, read step state, compute velocity, output note
- `cache_channels(json)` — Update cached channel data (called on any data change)
- `set_tempo(bpm)` — Update tempo for gate length calculation
- `set_resolution(res)` — Update resolution multiplier

**Messages in:** `step_trigger`, `cache_channels`, `set_tempo`, `set_resolution`
**Messages out:** `<note> <velocity> <duration>` to makenote

---

## 5. Dict Data Schema

### Master Structure

```json
{
    "version": 1,
    "global": {
        "swing": 50,
        "step_resolution": "1/16",
        "default_step_count": 16,
        "channel_count": 8,
        "active_pattern": 0
    },
    "patterns": [
        {
            "id": 0,
            "name": "A",
            "channels": [
                {
                    "id": 0,
                    "name": "Kick",
                    "color": "#FF3333",
                    "midi_note": 36,
                    "muted": 0,
                    "solo": 0,
                    "volume": 100,
                    "step_count": 16,
                    "steps":    [1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0],
                    "velocity": [120, 100, 100, 100, 100, 100, 100, 100, 120, 100, 100, 100, 110, 100, 100, 100]
                }
            ]
        },
        { "id": 1, "name": "B", "channels": [] },
        { "id": 2, "name": "C", "channels": [] },
        { "id": 3, "name": "D", "channels": [] }
    ],
    "ui_state": {
        "scroll_y": 0,
        "selected_channel": -1,
        "popup_window_rect": [100, 100, 900, 500]
    }
}
```

### Dict Key Paths

| Operation | Path |
|-----------|------|
| Get swing | `global::swing` |
| Get step state | `patterns[0]::channels[2]::steps[7]` |
| Get velocity | `patterns[0]::channels[2]::velocity[7]` |
| Set mute | `patterns[0]::channels[2]::muted` |
| Get channel name | `patterns[0]::channels[2]::name` |
| Active pattern | `global::active_pattern` |

### Constraints

- Array indexing is 0-based
- Booleans stored as `0`/`1` integers (no native bool in Max dict)
- Colors stored as hex strings (`"#FF3333"`)
- `steps` and `velocity` arrays MUST match `step_count` in length
- Engine validates on every write

### Default Channel Mapping

| Channel | Name | MIDI Note | Color |
|---------|------|-----------|-------|
| 0 | Kick | C1 (36) | #FF3333 (Red) |
| 1 | Snare | D1 (38) | #FFDD33 (Yellow) |
| 2 | Closed HH | F#1 (42) | #3399FF (Blue) |
| 3 | Open HH | A#1 (46) | #FFFFFF (White) |
| 4 | Clap | D#1 (39) | #33FF99 (Green) |
| 5 | Rim | C#1 (37) | #CC66FF (Purple) |
| 6 | Low Tom | F1 (41) | #FF6633 (Orange) |
| 7 | Mid Tom | B1 (47) | #FF99CC (Pink) |

---

## 6. Communication Bus

All `send`/`receive` pairs use `---` prefix for device-local isolation.

| Channel | Direction | Payload | Purpose |
|---------|-----------|---------|---------|
| `---wr-step-trigger` | timing → popup | `<step_index>` | Playback cursor position |
| `---wr-ui-refresh` | engine → popup | `refresh` | Trigger grid/controls redraw |
| `---wr-pattern-changed` | engine → all | `<pattern_index>` | Pattern switch notification |
| `---wr-channel-update` | engine → popup | `<channel_index>` | Single channel data changed |
| `---wr-tempo` | timing → engine | `<bpm>` | Current tempo for gate calc |
| `---wr-playing` | timing → all | `<0 or 1>` | Transport play/stop state |
| `---wr-open-popup` | main → popup | `bang` | Open the popup window |

---

## 7. Risk Assessment

### Highest Risk

1. **Timing Accuracy (HIGH)** — `plugsync~` outlet behavior may vary across Max versions. Step boundary double-triggers possible with `trunc~` + `change~` + `edge~` chain.
   - **Mitigation:** Prototype timing engine first (Phase 1). Test both `plugsync~` and `phasor~ 16n @lock 1`. Log timing over 100 bars.
   - **Fallback:** `metro @interval 16n @quantize 16n` as last resort.

2. **Dict Performance (MEDIUM-HIGH)** — 4-level nesting, 32 channels × 32 steps read per trigger.
   - **Mitigation:** `wr-midi-logic.js` caches channel data in JS arrays. Dict reads only on data change, never during playback.

3. **jsui Rendering (MEDIUM)** — 1024+ cells at 33fps.
   - **Mitigation:** Virtual scrolling (8-12 visible channels). Dirty-rectangle optimization. Reduce to 20fps if needed.

4. **Popup Window Lifecycle (MEDIUM)** — `closebang` unreliable in M4L.
   - **Mitigation:** Timer-based autosave (every 5s). Never rely on close events.

5. **LiveAPI Drum Rack Access (MEDIUM)** — Path navigation fragile, write access uncertain.
   - **Mitigation:** Graceful fallback if no Drum Rack found. Read-only in v1.0.

### What to Prototype First

1. Timing chain (`plugsync~` → step derivation → MIDI output)
2. Dict persistence (unnamed dict + `@parameter_enable 1` save/load cycle)
3. jsui in popup window (open/close, resize, MGraphics, mouse events)

---

## 8. Complexity Score

**Score: 5/5 (Master)**

Custom canvas UI + audio-rate timing + LiveAPI integration + drag-drop + pattern management + popup window lifecycle + fully programmatic .maxpat generation. Most complex project in the APC catalog. Wave Rack has more moving parts and subsystem interactions than any JUCE plugin in the repo, and operates in the more constrained M4L environment.
