# Earcandy - Parameter Specification

**Plugin:** Earcandy (Rack Series)
**Total Parameters:** 6

---

## Parameter Table

| ID | Name | Type | Range | Default | Unit | Description |
|:---|:-----|:-----|:------|:--------|:-----|:------------|
| `grain_size` | Grain Size | Float | 5.0 - 500.0 | 80.0 | ms | Length of each grain. Short = glitchy/granular. Long = smooth/padlike. |
| `density` | Density | Float | 1.0 - 100.0 | 20.0 | grains/s | How many grains spawn per second. Low = sparse texture. High = dense cloud. |
| `spray` | Spray | Float | 0.0 - 100.0 | 40.0 | % | Randomization of grain start position within the buffer. 0% = sequential. 100% = fully scattered. |
| `pitch` | Pitch | Float | -12.0 - 12.0 | 0.0 | semitones | Pitch shift applied to each grain. 0 = no shift. +12 = octave up (shimmer). -12 = octave down (drone). |
| `feedback` | Feedback | Float | 0.0 - 95.0 | 15.0 | % | Amount of processed signal fed back into the grain buffer. High values create self-generating textures. Capped at 95% for stability. |
| `mix` | Mix | Float | 0.0 - 100.0 | 50.0 | % | Dry/wet blend. 0% = fully dry (bypass). 100% = fully wet (grains only). |

## Parameter Grouping

**All 6 parameters are top-level knobs — no submenus or secondary controls.**

### Signal Flow

```
Input Audio
    |
    v
[Circular Buffer] <-- feedback --+
    |                             |
    v                             |
[Grain Spawner]                   |
  (size, density, spray)          |
    |                             |
    v                             |
[Pitch Shifter]                   |
  (pitch)                         |
    |                             |
    v                             |
[Output Mixer] -------------------+
  (mix)
    |
    v
Output Audio
```

## Design Notes

- **Grain Size** and **Density** are the primary texture shapers
- **Spray** is the "magic" parameter — it's what turns a delay-like effect into a granular soundscape
- **Pitch** at subtle values (+/- 0.5 semitones) creates natural detuning; at extremes creates shimmer/drone effects
- **Feedback** should be smooth and musically useful up to ~80%, becoming increasingly self-generative above that
- **Mix** at 100% is the default creative position; 50% is good for parallel blending in a mix context
