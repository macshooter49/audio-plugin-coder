# Earcandy - UI Specification v1

**Plugin:** Earcandy (Rack Series)
**Framework:** WebView (HTML5 Canvas)
**Window:** 600 x 350 px

---

## Layout

```
┌──────────────────────────────────────────────────────────────┐
│  EARCANDY                                          rack ◆   │  ← Header (30px)
│                                                              │
│   ┌─────┐  ┌─────┐  ┌─────┐  ┌─────┐  ┌─────┐  ┌─────┐   │
│   │     │  │     │  │     │  │     │  │     │  │     │   │
│   │ ARC │  │ ARC │  │ ARC │  │ ARC │  │ ARC │  │ ARC │   │  ← Knobs (200px)
│   │     │  │     │  │     │  │     │  │     │  │     │   │
│   └─────┘  └─────┘  └─────┘  └─────┘  └─────┘  └─────┘   │
│    80.0     20.0     40.0      0.0     15.0     50.0       │  ← Values
│   GRAIN    DENSITY   SPRAY    PITCH   FEEDBACK   MIX       │  ← Labels
│    SIZE                                                      │
│                                                              │
└──────────────────────────────────────────────────────────────┘
```

- **Grid:** Single row, 6 equal columns, 80px spacing
- **Knob diameter:** 72px (canvas), 80px hit area
- **Margins:** 30px left/right, 20px top

---

## Controls

| Parameter | ID | Type | Position | Range | Default | Display |
|-----------|-----|------|----------|-------|---------|---------|
| Grain Size | `grain_size` | Arc Knob | Col 1 | 5-500 ms | 80.0 | `{val} ms` |
| Density | `density` | Arc Knob | Col 2 | 1-100 grains/s | 20.0 | `{val}` |
| Spray | `spray` | Arc Knob | Col 3 | 0-100% | 40.0 | `{val}%` |
| Pitch | `pitch` | Arc Knob | Col 4 | -12 to +12 st | 0.0 | `{val} st` |
| Feedback | `feedback` | Arc Knob | Col 5 | 0-95% | 15.0 | `{val}%` |
| Mix | `mix` | Arc Knob | Col 6 | 0-100% | 50.0 | `{val}%` |

---

## Color Palette

| Role | Hex | Description |
|------|-----|-------------|
| Background | `#F3EFF8` | Light lavender-white |
| Surface | `#FFFFFF` | Pure white (knob glass fill) |
| Glass tint | `rgba(139,92,246,0.06)` | Faint purple glass sheen |
| Arc track | `#E8E0F0` | Muted lavender (inactive arc) |
| Arc active | `#8B5CF6` → `#A78BFA` | Purple gradient (value arc) |
| Arc glow | `rgba(139,92,246,0.3)` | Soft purple bloom on arc |
| Label text | `#8B7FA0` | Muted purple-gray |
| Value text | `#5B4F70` | Darker purple-gray |
| Title text | `#6B5F80` | Medium purple-gray |
| Badge text | `#B0A4C0` | Light muted purple |

---

## Style Notes

- **Glass knobs:** Radial gradient from white center to transparent edge, subtle inner shadow for depth. No hard borders — the glass look comes from light/shadow interplay.
- **Arc indicator:** 270-degree sweep (from 7 o'clock to 5 o'clock). Track is muted lavender; active portion is purple gradient with a soft glow/bloom effect.
- **Value display:** Shown directly below knob in the darker purple-gray. Updates in real-time during drag.
- **Labels:** ALL CAPS, letter-spaced, light weight. Subtle, not dominant.
- **Header:** Plugin name "EARCANDY" left-aligned, "rack" series badge right-aligned with small diamond icon.
- **No borders, no boxes** — controls float on the light background. Spacing creates the structure.
- **Interaction:** Vertical drag on knobs (drag up = increase, drag down = decrease). Cursor changes to grab/grabbing.
