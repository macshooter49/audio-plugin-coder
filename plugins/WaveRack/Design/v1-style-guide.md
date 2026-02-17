# Wave Rack — Style Guide v1

## Design Philosophy
Matte black hardware with colored LED step indicators. The UI should feel like a piece of high-end studio gear rendered in software — dark, modern, premium. Not cartoonish, not sterile.

## Color Palette

### Core Colors
| Token | Hex | Usage |
|-------|-----|-------|
| `--bg-primary` | `#0D0D1A` | Window background |
| `--bg-secondary` | `#141428` | Channel strip, status bar |
| `--bg-cell` | `#1E1E38` | Step cell (off state) |
| `--bg-cell-hover` | `#282850` | Step cell hover highlight |
| `--bg-toolbar` | `#111122` | Toolbar background |
| `--bg-graph` | `#0A0A18` | Graph editor background |
| `--grid-line` | `#2A2A4A` | Grid lines between cells |
| `--grid-beat` | `#3A3A5E` | Beat marker lines (every 4 steps) |
| `--text-primary` | `#E8E8F0` | Primary text (channel names, labels) |
| `--text-secondary` | `#8888AA` | Secondary text (MIDI notes, status) |
| `--text-dim` | `#555570` | Disabled/inactive text |
| `--cursor` | `rgba(255, 255, 255, 0.18)` | Playback cursor overlay |
| `--scrollbar` | `#3A3A5E` | Scrollbar track |
| `--scrollbar-thumb` | `#5A5A7E` | Scrollbar thumb |

### Channel Colors (LED palette)
| Index | Name | Hex | Default Assignment |
|-------|------|-----|--------------------|
| 0 | Red | `#FF3B3B` | Kick |
| 1 | Yellow | `#FFD93D` | Snare |
| 2 | Blue | `#3B82F6` | Closed HH |
| 3 | White | `#E0E0F0` | Open HH |
| 4 | Green | `#34D399` | Clap |
| 5 | Purple | `#A78BFA` | Rim |
| 6 | Orange | `#FB923C` | Low Tom |
| 7 | Pink | `#F472B6` | Mid Tom |
| 8+ | Cycle with 15% brightness shift | — | Additional channels |

### UI State Colors
| Token | Hex | Usage |
|-------|-----|-------|
| `--mute-active` | `#EF4444` | Mute button when active (red) |
| `--solo-active` | `#EAB308` | Solo button when active (yellow) |
| `--btn-inactive` | `#3A3A5E` | Mute/Solo button when inactive |
| `--pattern-active` | `#6366F1` | Active pattern button (indigo) |
| `--pattern-inactive` | `#2A2A4A` | Inactive pattern button |
| `--accent-glow` | `rgba(255, 255, 255, 0.35)` | Accent step inner border |

## Typography

| Element | Font | Size | Weight | Color |
|---------|------|------|--------|-------|
| Title "WAVE RACK" | System sans-serif | 14px | 700 (bold) | `--text-primary` |
| Channel name | System sans-serif | 11px | 500 (medium) | `--text-primary` |
| MIDI note label | System monospace | 9px | 400 (regular) | `--text-secondary` |
| Step numbers | System monospace | 9px | 400 | `--text-dim` |
| Status bar text | System sans-serif | 10px | 400 | `--text-secondary` |
| Button labels (M/S) | System sans-serif | 9px | 700 | `--text-primary` |
| Pattern labels (A/B/C/D) | System sans-serif | 12px | 700 | `--text-primary` |
| BPM display | System monospace | 12px | 500 | `--text-primary` |

**Note:** jsui MGraphics uses system fonts. Specify `Arial` as primary, falls back to system sans-serif. For monospace, use `Menlo` (macOS) / `Consolas` (Windows).

## Spacing and Layout

### Grid Measurements
| Property | Value | Notes |
|----------|-------|-------|
| Toolbar height | 40px | Fixed |
| Status bar height | 28px | Fixed |
| Channel strip width | 180px | Fixed |
| Scrollbar width | 12px | Fixed |
| Graph editor height | 80px | Collapsible (0px when collapsed) |
| Cell gap | 2px | Between step cells |
| Cell corner radius | 3px | Rounded corners on step cells |
| Channel row padding | 4px top/bottom | Between channel rows |
| Section padding | 8px | Between major sections |

### Minimum Cell Dimensions
- Width: 20px (at 32 steps, 760px window)
- Height: 32px (at 12 visible channels)

## Control Styles

### Step Cell
```
Off:      bg: --bg-cell, border: none
Hover:    bg: --bg-cell-hover, border: none
On:       bg: channel_color @ 85% opacity, border: 1px channel_color
Accent:   bg: channel_color @ 100%, border: 2px white @ 35%
```

### Mute/Solo Buttons
```
Dimensions: 22x22px, rounded corners (4px)
Inactive:   bg: --btn-inactive, text: --text-dim
Mute active: bg: --mute-active, text: white
Solo active: bg: --solo-active, text: #1A1A00
Hover:       brightness +10%
```

### Pattern Buttons
```
Dimensions: 32x28px, rounded corners (4px)
Inactive:   bg: --pattern-inactive, text: --text-dim
Active:     bg: --pattern-active, text: white
Hover:      brightness +10%
```

### Volume Mini-Fader
```
Width: 10px, Height: matches channel row
Track: --grid-line (2px wide, centered)
Fill:  channel_color (from bottom up)
Knob:  none (just filled bar)
```

### Color Indicator
```
Width: 6px, full channel row height
Solid fill: channel_color
No border
```

### Graph Editor Bars
```
Width: matches step cell width
Height: proportional to value (0 = 0px, 127 = full panel height)
Color: channel_color @ 60% opacity
Gap: same as cell gap (2px)
Hover: channel_color @ 80%
```

### Scrollbar
```
Track: --scrollbar (12px wide)
Thumb: --scrollbar-thumb, rounded (6px radius)
Thumb hover: brightness +15%
Min thumb height: 30px
```

### Playback Cursor
```
Full column height (toolbar to graph editor)
Width: one cell width
Color: --cursor (white @ 18%)
No border
Renders on top of step cells
```

## Animation

| Animation | Duration | Easing | Notes |
|-----------|----------|--------|-------|
| Step toggle | Instant | — | No transition, immediate visual feedback |
| Playback cursor | 33ms (30fps) | Linear | Moves in discrete steps, no interpolation |
| Mute/Solo toggle | Instant | — | Immediate color change |
| Pattern switch | Instant | — | Grid redraws with new data |
| Hover effects | 100ms | Ease-out | Subtle brightness change |
| Graph editor bars | Instant | — | Bars update on click/drag immediately |

## Responsive Behavior

### Window Resize
- Step cells scale width proportionally to available space
- Channel rows scale height proportionally
- Channel strip width stays fixed at 180px
- Toolbar and status bar stay fixed height
- Minimum window size enforced (760 x 440)

### Scaling at Different Channel Counts
| Channels Visible | Row Height | Notes |
|-----------------|------------|-------|
| 4-6 | 56-48px | Spacious, easy to click |
| 8-10 | 40-36px | Default, comfortable |
| 12-16 | 32-28px | Dense but usable |
| 16+ | Scroll | Virtual scrolling, 12 visible |

## Brand Elements

- **Logo:** "WC" monogram or "Waves Crate" text in toolbar top-left
- **Accent:** The indigo/purple (`#6366F1`) used for active pattern buttons doubles as the brand accent color
- **No watermarks or large branding** — the UI IS the brand (clean, professional, premium)
