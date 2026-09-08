# Earcandy - Style Guide v1

**Series:** Rack
**Theme:** Light minimal with purple tint

---

## Color System

### Backgrounds
```
--bg-main:       #F3EFF8    /* Main background — light lavender-white */
--bg-surface:    #FFFFFF    /* Knob glass surface */
--bg-glass:      rgba(139, 92, 246, 0.06)  /* Purple glass tint overlay */
```

### Interactive
```
--arc-track:     #E8E0F0    /* Inactive arc track */
--arc-active-start: #8B5CF6 /* Active arc gradient start (deeper purple) */
--arc-active-end:   #A78BFA /* Active arc gradient end (lighter purple) */
--arc-glow:      rgba(139, 92, 246, 0.3)  /* Soft bloom around active arc */
--arc-width:     3px        /* Arc stroke width */
--arc-width-active: 3.5px   /* Active arc slightly thicker */
```

### Typography
```
--text-title:    #6B5F80    /* Plugin name */
--text-value:    #5B4F70    /* Parameter values */
--text-label:    #8B7FA0    /* Parameter labels */
--text-badge:    #B0A4C0    /* Series badge */
```

### Glass Effect
```
--glass-highlight: rgba(255, 255, 255, 0.9)  /* Top-left highlight */
--glass-shadow:    rgba(139, 92, 246, 0.08)   /* Bottom-right shadow */
--glass-inner:     rgba(0, 0, 0, 0.03)        /* Subtle inner shadow */
```

---

## Typography

| Element | Font | Weight | Size | Tracking | Transform |
|---------|------|--------|------|----------|-----------|
| Plugin title | Inter / system-ui | 300 | 16px | 3px | uppercase |
| Parameter value | Inter / system-ui | 400 | 13px | 0 | normal |
| Parameter label | Inter / system-ui | 400 | 10px | 1.5px | uppercase |
| Series badge | Inter / system-ui | 300 | 11px | 2px | lowercase |

**Font stack:** `'Inter', system-ui, -apple-system, sans-serif`

---

## Knob Anatomy (Canvas)

```
        ╭─── Arc track (270° sweep, muted lavender)
       ╱
      ╱   ╭─── Active arc (purple gradient, with glow)
     ╱   ╱
    ╱   ╱
   ┌─────────┐
   │ ░░░░░░░ │ ← Glass fill (radial gradient: white center → transparent)
   │ ░░░░░░░ │   Inner shadow for depth
   │ ░░░░░░░ │
   └─────────┘
       │
    80.0 ms    ← Value text (--text-value)
   GRAIN SIZE  ← Label text (--text-label, uppercase, letter-spaced)
```

### Arc Geometry
- **Total sweep:** 270° (from 225° to -45°, or 7 o'clock to 5 o'clock)
- **Gap:** 90° at bottom
- **Radius:** 33px (from 72px diameter knob)
- **Track width:** 3px
- **Active width:** 3.5px with 6px glow blur

### Glass Rendering
1. **Base circle:** Fill with radial gradient `#FFFFFF` center → `rgba(255,255,255,0.3)` edge
2. **Purple tint:** Overlay `rgba(139,92,246,0.06)`
3. **Highlight:** Small elliptical gradient at top-left (simulates light refraction)
4. **Inner shadow:** `inset 0 1px 3px rgba(0,0,0,0.03)`
5. **No stroke/border** — glass effect is purely from gradients

---

## Spacing

| Measurement | Value |
|-------------|-------|
| Window padding (horizontal) | 30px |
| Window padding (top) | 20px |
| Header height | 30px |
| Header to knobs | 20px |
| Knob canvas size | 72px |
| Knob hit area | 80px |
| Knob to value text | 8px |
| Value to label text | 4px |
| Inter-knob spacing | ~80px center-to-center (auto from 6-col grid) |

---

## Interaction

### Knob Drag
- **Method:** Vertical drag (mousedown → mousemove Y → mouseup)
- **Sensitivity:** 200px full range (configurable)
- **Cursor:** `grab` on hover, `grabbing` during drag
- **Fine control:** Hold Shift for 4x precision (800px full range)
- **Reset:** Double-click to return to default value

### Visual Feedback
- **Hover:** Subtle brightening of glass surface (+5% opacity)
- **Active (dragging):** Arc glow intensifies, value text becomes --arc-active-start color
- **No tooltip** — value is always visible below knob

---

## Rack Series Identity

- **Title:** "EARCANDY" — left-aligned, light weight, letter-spaced
- **Badge:** "rack" + diamond ◆ — right-aligned, lowercase, muted
- **No logo, no decorative elements** — typography and spacing carry the brand
- **Consistent across Rack series:** Same header layout, same badge style, different accent colors per plugin
