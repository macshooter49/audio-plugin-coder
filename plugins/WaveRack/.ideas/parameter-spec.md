# Wave Rack — Parameter Specification

> **Note:** Wave Rack is a Max for Live MIDI Effect, not a JUCE plugin. Parameters below map to M4L `live.dial`, `live.toggle`, `live.numbox`, and `dict`-stored per-step data — not JUCE AudioParameterFloat/Bool.

## Global Parameters

| ID | Name | Type | Range | Default | Unit | M4L Object | Notes |
|:---|:-----|:-----|:------|:--------|:-----|:-----------|:------|
| `pattern_select` | Pattern | Int | 0-3 (v1.0), 0-7 (v1.5) | 0 | — | `live.menu` | A/B/C/D pattern switching |
| `step_count` | Steps | Enum | 16, 32, 64 | 16 | steps | `live.menu` | Global default step count |
| `step_resolution` | Resolution | Enum | 1/8, 1/16, 1/32, 1/16T | 1/16 | — | `live.menu` | Note subdivision |
| `swing_amount` | Swing | Float | 0-100 | 50 | % | `live.dial` | Global swing (50% = straight) |
| `transport_playing` | Playing | Bool | 0/1 | 0 | — | internal | Synced to Ableton transport |

## Per-Channel Parameters

| ID | Name | Type | Range | Default | Unit | Storage | Notes |
|:---|:-----|:-----|:------|:--------|:-----|:--------|:------|
| `ch_name` | Channel Name | String | — | MIDI note name | — | `dict` | Editable, defaults to "Kick", "Snare", etc. |
| `ch_color` | Channel Color | Hex | #000000-#FFFFFF | Per-channel preset | — | `dict` | User-selectable color indicator |
| `ch_midi_note` | MIDI Note | Int | 0-127 | 36+ (auto-increment) | MIDI | `dict` | Output note number for Drum Rack |
| `ch_mute` | Mute | Bool | 0/1 | 0 | — | `dict` | Silences channel MIDI output |
| `ch_solo` | Solo | Bool | 0/1 | 0 | — | `dict` | Solos channel (mutes all others) |
| `ch_volume` | Volume | Int | 0-127 | 100 | velocity | `dict` | Velocity scaling for entire channel |
| `ch_step_count` | Step Count | Int | 1-64 | inherit global | steps | `dict` | Per-channel polyrhythmic length |
| `ch_swing_mix` | Swing Mix | Float | 0-100 | 100 | % | `dict` | Blend of global swing (v1.5) |

## Per-Step Parameters (stored in `dict` arrays)

| ID | Name | Type | Range | Default | Unit | Notes |
|:---|:-----|:-----|:------|:--------|:-----|:------|
| `step_state` | Step State | Enum | 0=Off, 1=On, 2=Accent | 0 | — | Click toggles Off/On, modifier+click cycles accent |
| `step_velocity` | Velocity | Int | 1-127 | 100 | velocity | Per-step hit velocity, scaled by channel volume |
| `step_pitch` | Pitch | Int | -24 to +24 | 0 | semitones | Per-step pitch offset (v1.5) |
| `step_shift` | Shift | Float | -50 to +50 | 0 | % of step | Micro-timing offset for groove/humanize (v1.5) |
| `step_length` | Length/Gate | Int | 1-100 | 80 | % of step | Note duration as percentage of step length (v1.5) |

## MIDI Output Specification

| Message | Field | Range | Notes |
|:--------|:------|:------|:------|
| Note On | Note Number | 0-127 | From `ch_midi_note` |
| Note On | Velocity | 1-127 | `step_velocity * (ch_volume / 127)` |
| Note On | Channel | 1 | All on MIDI Ch 1 for Drum Rack |
| Note Off | Timing | — | After `step_length` % of step duration |

## Data Architecture

### Pattern Dict Structure
Each pattern stores the complete state of all channels and steps:
```json
{
  "pattern_A": {
    "channels": [
      {
        "id": 0,
        "name": "Kick",
        "color": "#FF3333",
        "midi_note": 36,
        "muted": false,
        "solo": false,
        "volume": 127,
        "step_count": 16,
        "steps":    [1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0],
        "velocity": [120, 0, 0, 0, 100, 0, 0, 0, 120, 0, 0, 0, 110, 0, 0, 0],
        "pitch":    [0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0],
        "shift":    [0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0],
        "length":   [80, 0, 0, 0, 80, 0, 0, 0, 80, 0, 0, 0, 80, 0, 0, 0]
      }
    ],
    "swing": 50,
    "step_resolution": "1/16"
  }
}
```

### Persistence
- `dict` with `@parameter_enable 1` — auto-saves with Live Set
- `pattrstorage` — pattern preset management (A/B/C/D switching, copy, paste)
- Full state restoration on Live Set load

## Default Channel Mapping (v1.0)

| Channel | Name | MIDI Note | Color |
|:--------|:-----|:----------|:------|
| 0 | Kick | C1 (36) | #FF3333 (Red) |
| 1 | Snare | D1 (38) | #FFDD33 (Yellow) |
| 2 | Closed HH | F#1 (42) | #3399FF (Blue) |
| 3 | Open HH | A#1 (46) | #FFFFFF (White) |
| 4 | Clap | D#1 (39) | #33FF99 (Green) |
| 5 | Rim | C#1 (37) | #CC66FF (Purple) |
| 6 | Low Tom | F1 (41) | #FF6633 (Orange) |
| 7 | Mid Tom | B1 (47) | #FF99CC (Pink) |

## v1.0 vs v1.5 Scope

### v1.0 (Launch) — Implemented
- `step_state`, `step_velocity` (per-step)
- All global params
- All per-channel params (except `ch_swing_mix`)
- Pattern system (4 patterns)
- 16/32 step grid

### v1.5 (Update) — Deferred
- `step_pitch`, `step_shift`, `step_length` (per-step, graph editor)
- `ch_swing_mix` (per-channel swing blend)
- 64 step grid
- 8+ patterns
- Ghost notes
- MIDI clip export
- Channel drag-reordering
