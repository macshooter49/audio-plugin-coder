# Terrain Modulation System Design

**Date:** 2026-02-14
**Version:** v3.0 (Modulation)
**Status:** Approved

---

## Overview

A modulation routing system for Terrain: three independent LFOs and XY pad as modulation sources, assignable to any parameter via right-click. The system is a parameter-value layer that does NOT modify existing DSP. It intercepts parameter values, applies modulation offsets at control rate, and passes modulated values to the audio engines.

---

## 1. MOD Button (Top Bar)

- **Location:** Header bar, right of brand version, left of preset area
- **Style:** Horizontal pill button, `MOD` label, 9px uppercase, letter-spacing 1.5px
- **Inactive:** Subtle purple border (`rgba(139,92,246,0.2)`), transparent background
- **Active (panel open):** Filled purple background (`#8B5CF6`), white text
- **Indicator:** Small animated dot (breathing pulse) when any modulation assignment is active
- **Behavior:** Click to toggle modulation panel overlay open/closed

---

## 2. Modulation Panel (Three-Column Grid)

### Layout
- **Dimensions:** 820px x 272px (replaces `#controls` section)
- **Structure:** Three equal columns (LFO 1 | LFO 2 | LFO 3), divided by 1px border (`var(--border)`)
- **Background:** White (`var(--bg-surface)`), matching existing control sections
- **Padding:** 10px 12px per column

### Per-LFO Column Contents

#### Header
- Color dot (6px circle) + "LFO 1/2/3" label (9px uppercase, letter-spacing 2px)

#### Waveform Display
- Canvas: ~240px x 50px
- Animated waveform line (2px stroke) in LFO color
- Moving position dot (4px) showing current phase
- Subtle grid lines at 25%/50%/75% vertical (`rgba(0,0,0,0.05)`)
- Center line for bipolar reference (`rgba(0,0,0,0.1)`)

#### Shape Selector
- 7 icon buttons (18x18px each), inline row
- Bordered glass style matching existing deck buttons
- Shapes: Sine, Triangle, Square, Saw Up, Saw Down, Sample & Hold, Smooth Random
- Active shape: filled border in LFO color

#### Controls (Mini Knobs)
- **Rate:** 32px knob + value label + Free/Sync toggle pill
  - Free: 0.01 Hz - 20 Hz
  - Sync: 8 bars, 4 bars, 2 bars, 1 bar, 1/2, 1/4, 1/8, 1/16, 1/32, plus triplet/dotted
- **Depth:** 32px knob + percentage (0-100%)
- **Phase:** 32px knob + degree label (0-360deg)
- **Polarity:** 3-way pill selector (Bi | + | -)

#### Targets List
- Compact list of assigned parameters
- Each row: param name + depth percentage + remove button (x)
- Scrollable if needed (max ~3 visible)
- "(no assignments)" placeholder when empty

---

## 3. Right-Click Context Menu

### Appearance
- Frosted glass dropdown: `backdrop-filter: blur(12px)`, white 85% opacity
- Border: `1.5px solid rgba(139,92,246,0.18)`
- Border-radius: 10px
- Shadow: `0 4px 16px rgba(139,92,246,0.15)`
- Width: ~240px
- Positioned near the right-clicked knob

### Content
```
MODULATE: [PARAM NAME]
---
[toggle] LFO 1  [---depth slider---] XX%
[toggle] LFO 2  [---depth slider---] XX%
[toggle] LFO 3  [---depth slider---] XX%
---
[toggle] XY X   [---depth slider---] XX%
[toggle] XY Y   [---depth slider---] XX%
---
[x] Clear All
```

### Behavior
- Toggle circles: filled = active, empty = inactive
- Toggle color matches source color
- Depth sliders: thin horizontal (same style as output slider), 0-100%
- Toggling on with depth at 0% auto-sets depth to 50%
- "Clear All" removes every assignment from this parameter
- Closes on outside click
- Opens on right-click (contextmenu event) on any modulatable knob

### Modulatable Parameters
- **Grain Engine:** SIZE, DENSITY, SPRAY, PITCH, FREEZE, WANDER, FILTER, GRAIN MIX
- **Tape FX:** WOW, SATURATION, HISS
- **Tape Loop:** FEEDBACK, DEGRADE, SPEED
- **Output:** OUTPUT LEVEL, MIX
- **NOT modulatable:** Transport buttons, Loop Length, Preset browser

---

## 4. Modulation Rings on Knobs

- **Ring:** 2px outer arc around the knob, outside the value arc
- **Color:** Matches source color (if multiple sources, uses dominant source color)
- **Animation:** Arc sweeps in real-time at LFO rate showing current modulation range
- **Badge:** 4px color dot at bottom-right of knob when modulated
- **Priority:** Subtle — doesn't overpower the knob's own value arc
- **Update:** Driven by UI animation frame, NOT audio thread

---

## 5. Color System

| Source   | Color   | Hex       | Usage                              |
|----------|---------|-----------|-------------------------------------|
| LFO 1   | Purple  | `#8B5CF6` | Waveform, ring, toggle, badge       |
| LFO 2   | Teal    | `#06B6D4` | Waveform, ring, toggle, badge       |
| LFO 3   | Amber   | `#F59E0B` | Waveform, ring, toggle, badge       |
| XY Pad X | Lt Purple | `#A78BFA` | Toggle, ring                      |
| XY Pad Y | Dk Purple | `#7C3AED` | Toggle, ring                      |

---

## 6. LFO Engines (DSP)

### Parameters Per LFO
- Shape: enum (7 values)
- Rate: float (Hz in free mode, division index in sync mode)
- Rate Mode: Free / Sync
- Depth: float (0.0 - 1.0)
- Phase: float (0.0 - 1.0, maps to 0-360deg)
- Polarity: Bipolar / Unipolar+ / Unipolar-

### Waveform Functions
```
SINE:          sin(phase * 2pi)                    -> [-1, 1]
TRIANGLE:      1.0 - 4.0 * abs(phase - 0.5)       -> [-1, 1]
SQUARE:        phase < 0.5 ? 1.0 : -1.0            -> [-1, 1]
SAW_UP:        2.0 * phase - 1.0                    -> [-1, 1]
SAW_DOWN:      1.0 - 2.0 * phase                    -> [-1, 1]
SAMPLE_HOLD:   random value, new each cycle wrap     -> [-1, 1]
SMOOTH_RANDOM: interpolate between random targets    -> [-1, 1]
```

### Polarity Transform
- Bipolar: output unchanged (-1 to +1)
- Unipolar+: `output * 0.5 + 0.5` (0 to +1, sweeps above)
- Unipolar-: `output * -0.5 + 0.5` (+1 to 0, sweeps below)

### Processing
- Control rate: update every 32 samples
- Phase accumulation: `phase += rate * (32.0 / sampleRate)`
- Sync mode: rate derived from BPM and division

---

## 7. Routing Architecture

### Data Structure
```cpp
struct ModAssignment {
    int source;          // LFO1=0, LFO2=1, LFO3=2, XY_X=3, XY_Y=4
    int targetParamIdx;  // index into modulatable params array
    float depth;         // 0.0 - 1.0
    bool enabled;
};
```

### Value Calculation
```
finalValue = clamp(
    baseValue
    + lfo1Output * lfo1Depth * assignment1Depth
    + lfo2Output * lfo2Depth * assignment2Depth
    + lfo3Output * lfo3Depth * assignment3Depth
    + xyX * assignmentXDepth
    + xyY * assignmentYDepth,
    paramMin, paramMax
)
```

- All offsets are additive
- Final value clamped to parameter's valid range
- DSP engines receive final value, never know modulation exists
- No per-sample processing for modulation

### Limits
- No limit on number of assignments
- Any parameter can have multiple sources
- Any source can target multiple parameters
- Assignments stack additively

---

## 8. Preset Integration

- All LFO settings save with presets (shape, rate, sync, depth, phase, polarity)
- All modulation assignments save with presets (source, target, depth, enabled)
- XY pad position saves with presets
- Older presets without modulation data load with no assignments (backward compatible)
- Modulation state stored in a separate XML element within the preset data

---

## 9. Preset Ideas

| Name | Routing | Description |
|------|---------|-------------|
| Breathing Machine | LFO 1 (sine, 0.3Hz) -> FREEZE 40% | Grains slowly freeze and unfreeze |
| Wobble Tape | LFO 2 (tri, sync 1/2) -> WOW 60% | Tape wow surges rhythmically |
| Chaos Drift | LFO 1 (smooth random, 0.5Hz) -> WANDER 50%, LFO 2 (sine, 0.1Hz) -> SPRAY 30% | Unpredictable grain behavior |
| Sidechain Grain | LFO 3 (saw down, sync 1/4) -> GRAIN MIX 80% | Granular pumps rhythmically |
| XY Performance | XY X -> FREEZE + DEGRADE, XY Y -> WANDER + SAT | One gesture, four parameters |
| Tape Drift | LFO 1 (smooth random, 0.08Hz, uni+) -> SPEED 15% | Loop speed wanders |
| Pulse Engine | LFO 2 (square, sync 1/8) -> DENSITY 70% | Grain density switches rhythmically |

---

## 10. Implementation Order

1. **UI Shell (build first, show for approval)**
   - MOD button in header
   - Modulation panel overlay (3-column grid)
   - Animated waveform displays (placeholder animation)
   - Shape/Rate/Depth/Phase/Polarity controls
   - Right-click context menu on knobs
   - Modulation ring visuals on knobs
   - All interactive, no DSP wired

2. **DSP Layer (after UI approval)**
   - LFO engine class (3 instances)
   - Modulation routing manager
   - Control-rate processing in processBlock
   - Wire LFO outputs to parameter offsets
   - XY pad as modulation source

3. **Integration**
   - Native functions for JS <-> C++ mod state
   - Preset save/load for modulation data
   - Real-time waveform data push to UI (30Hz timer)
   - Modulation ring updates from LFO output

4. **Polish**
   - Modulation presets (7 from table above)
   - Edge cases (parameter limits, tempo changes, LFO reset)
   - CPU profiling
