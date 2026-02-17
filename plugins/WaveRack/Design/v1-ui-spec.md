# Wave Rack — UI Specification v1

## Target Platform
- **Rendering:** Max for Live `jsui` with MGraphics (Canvas-like 2D API)
- **Window:** Popup/floating window via `thispatcher` (NOT the compact device view)
- **Preview:** This spec is previewed via `v1-test.html` in a browser

## Window Dimensions
- **Default:** 960 x 560 px
- **Minimum:** 760 x 440 px
- **Resizable:** Yes (all elements scale proportionally)

## Layout Overview

```
┌──────────────────────────────────────────────────────────────────────┐
│  TOOLBAR (40px height)                                               │
│  [WC logo] WAVE RACK   [A][B][C][D]  Steps:[16▾]  Res:[1/16▾]  [≡] │
├────────────┬─────────────────────────────────────────────────────────┤
│  CHANNEL   │  STEP GRID                                              │
│  STRIP     │                                                         │
│  (180px)   │  (remaining width)                                      │
│            │                                                         │
│  8-12 rows │  16-32 columns of step cells                           │
│  visible   │                                                         │
│            │                                                         │
│            │                                                         │
│            │                                                         │
│            │                                                         │
├────────────┼─────────────────────────────────────────────────────────┤
│            │  GRAPH EDITOR (80px height, collapsible)                │
│            │  [Vel ▾] ▁▃█▅▁▁▃█▅▁▁▃█▅▁▁▃                            │
├────────────┴─────────────────────────────────────────────────────────┤
│  STATUS BAR (28px height)                                            │
│  Swing: [50%━━━○━━━100%]   BPM: 140 (synced)   Ch: 8/32   ▶ Playing│
└──────────────────────────────────────────────────────────────────────┘
```

## Section 1: Toolbar (40px)

| Element | Type | Position | Size | Notes |
|---------|------|----------|------|-------|
| WC Logo | Text/Image | Left 12px | 24x24 | Waves Crate wordmark, links to wavescrate.com |
| Title | Text | Left 44px | — | "WAVE RACK" in brand font |
| Pattern A | Button | Right-aligned group | 32x28 | Lit when active, dim when inactive |
| Pattern B | Button | After A | 32x28 | Same style |
| Pattern C | Button | After B | 32x28 | Same style |
| Pattern D | Button | After C | 32x28 | Same style |
| Steps | Dropdown | After patterns +16px gap | 60x28 | Options: 16, 32 |
| Resolution | Dropdown | After steps +8px | 64x28 | Options: 1/8, 1/16, 1/32, 1/16T |
| Menu | Button | Right 12px | 28x28 | Hamburger icon, settings/about |

## Section 2: Channel Strip (180px wide)

Each channel row height = `(grid_height) / visible_channels` (typically 38-48px).

| Element | Position (x from left) | Width | Notes |
|---------|----------------------|-------|-------|
| Color indicator | 0-6px | 6px full height | Solid bar in channel color |
| Channel name | 12px | 72px | Truncated with ellipsis, editable on double-click |
| MIDI note | 86px | 28px | Small text: "C1", "D1", etc. |
| Mute (M) | 118px | 22x22 | Toggle button, red when active |
| Solo (S) | 142px | 22x22 | Toggle button, yellow when active |
| Volume | 166px | 10px | Vertical mini-fader (0-127) |

## Section 3: Step Grid (main area)

Cell dimensions calculated dynamically:
- `cellWidth = (window_width - channel_strip_width - scrollbar_width) / step_count`
- `cellHeight = channel_row_height` (matches channel strip rows)

### Step Cell States

| State | Visual | Notes |
|-------|--------|-------|
| Off | Dark background (#252540) | Subtle border visible |
| On | Channel color at 85% opacity | Rounded corner (2px radius) |
| Accent | Channel color at 100% + bright border | White inner glow/border |

### Beat Markers
- Every 4 steps: slightly brighter vertical grid line (#3A3A5E)
- Step numbers at top: "1", "2", "3", "4" above every 4th step

### Playback Cursor
- Full-height vertical bar at current step
- Color: white at 25% opacity
- Width: same as one cell
- Smooth movement (interpolated between steps at 33fps)

## Section 4: Graph Editor (80px, collapsible)

Bottom panel showing per-step parameter values as vertical bars.

| Element | Notes |
|---------|-------|
| Parameter selector | Tabs: [Velocity] [Pitch] [Shift] [Length] — only Velocity active in v1.0 |
| Bar graph | One bar per step, height proportional to value (0-127 for velocity) |
| Bar color | Channel color at 60% opacity |
| Background | Slightly darker than grid (#1A1A2E) |
| Interaction | Click/drag to set values |

## Section 5: Status Bar (28px)

| Element | Position | Notes |
|---------|----------|-------|
| Swing label + slider | Left 12px | "Swing:" + horizontal slider (50-100%) |
| BPM display | Center | "BPM: 140 (synced)" — read-only, from Ableton |
| Channel count | Center-right | "Ch: 8/32" — active/max |
| Transport state | Right 12px | Play/stop icon + "Playing"/"Stopped" |

## Section 6: Compact Device View (in Ableton)

Separate from popup. Shown in Live's device chain (~170px height).

```
┌─────────────────────────────────────────────┐
│ WAVE RACK          [A][B][C][D]    [OPEN]   │
│ Swing: [●━━━━]  BPM: 140  ▶  Steps: [16▾]  │
└─────────────────────────────────────────────┘
```

| Element | M4L Object | Notes |
|---------|-----------|-------|
| Title | `[comment]` | "WAVE RACK" |
| Pattern buttons | `[live.text]` x4 | Automatable toggles |
| Open button | `[live.text]` | Opens popup window |
| Swing | `[live.dial]` | 0-100%, automatable |
| BPM | `[live.numbox]` | Read-only, from transport |
| Transport | `[live.text]` | Play indicator |
| Steps | `[live.menu]` | 16/32 dropdown |

## Scrolling

### Vertical (Channels)
- Scrollbar on right edge (12px wide)
- Thumb size proportional to visible/total ratio
- Scroll wheel supported
- 8-12 channels visible at once (depending on window height)

### Horizontal (Steps > 16)
- When step count = 32+, horizontal scroll or zoom
- Default: fit all steps in view (cells get narrower)
- Optional: scroll mode for 64 steps

## Interaction Patterns

| Action | Trigger | Result |
|--------|---------|--------|
| Toggle step | Left click on cell | Off → On, On → Off |
| Accent step | Shift + click on cell | Off → Accent, On → Accent, Accent → Off |
| Paint steps | Click + drag across cells | All dragged cells set to same state as first toggle |
| Mute channel | Click M button | Red highlight, channel silenced |
| Solo channel | Click S button | Yellow highlight, only this channel plays |
| Set velocity | Click/drag in graph editor | Bar height = velocity value |
| Switch pattern | Click A/B/C/D button | Grid updates to show new pattern data |
| Scroll channels | Mouse wheel / scrollbar drag | Channel strip + grid scroll together |
| Edit channel name | Double-click name | Text input prompt |
| Change channel color | Right-click color indicator | Color picker popup |
