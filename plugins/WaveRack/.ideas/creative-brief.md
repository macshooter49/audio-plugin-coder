# Wave Rack — Creative Brief

## Hook
**"The Channel Rack Ableton Deserves."**
Wave Rack brings FL Studio's beloved step sequencer workflow into Ableton Live as a Max for Live MIDI effect. Faster drum programming. Smarter MIDI routing. Professional-grade step sequencing — right where it belongs.

## Product Identity
- **Name:** Wave Rack
- **Company:** Waves Crate
- **Type:** Max for Live MIDI Effect (.amxd)
- **Price Target:** $25-35
- **Tagline:** "The Channel Rack Ableton Deserves"

## The Problem
Ableton Live has no native channel rack-style step sequencer. FL-to-Ableton converts lose their fastest drum programming workflow. Existing solutions (Juicy Loops 2) are limited to 6 channels, lack per-step parameter control, have no pattern system, and don't scale for professional use.

## The Solution
A MIDI-only step sequencer device that sits before a Drum Rack in the device chain. Wave Rack handles sequencing, workflow, and creative speed — the Drum Rack handles sound playback. Smart MIDI note auto-assignment on drag-and-drop eliminates manual routing. A popup floating window delivers a full-sized, scalable UI that feels like hardware.

## Sonic Goal
MIDI sequencing — no audio processing. Wave Rack generates perfectly timed MIDI note events using `phasor~` for sample-accurate transport sync. The "sound" is defined by velocity control, per-step pitch variation, micro-timing shift, and gate length — all shaping how the downstream Drum Rack responds.

## Target Audience
- Hip-hop, trap, electronic, and pop producers using Ableton Live
- FL Studio converts who miss the Channel Rack
- Ableton-native producers who want faster drum programming than the piano roll
- Professional and semi-professional producers who value workflow speed

## Competitive Edge (vs. Juicy Loops 2)
| Dimension | Juicy Loops 2 | Wave Rack |
|-----------|--------------|-----------|
| Channels | 6 | 32+ (64+ architecture) |
| Per-step pitch/shift/length | No | Yes (graph editor) |
| Pattern system | No | 4-8+ patterns |
| Channel mute/solo | No | Yes |
| Channel colors | No | Yes |
| Swing | Basic | Global + per-channel |
| Scalable UI | No | Resizable popup window |
| Drum Rack integration | Basic | Smart MIDI auto-mapping |
| Drag-and-drop | No | Yes (live.drop + auto-assign) |

## Visual Identity
Dark, modern, premium. Matte black hardware aesthetic with colored LED indicators. Not cartoonish, not sterile. High-contrast text on dark backgrounds (#1A1A2E or similar). Channel colors as accent lighting. The UI should feel like a piece of high-end studio gear rendered in software.

## Key Innovation: Smart MIDI Routing
1. **Auto-Assignment on Drop** — Drag a sample onto a Wave Rack channel, it auto-assigns the next available Drum Rack pad MIDI note (C1/36 upward)
2. **LiveAPI Integration** — Query downstream Drum Rack for occupied pads and pad names
3. **Manual Override** — Full manual MIDI note assignment per channel
4. **Note Name Display** — Shows assigned note and Drum Rack pad name per channel

## Architecture
- **Device Type:** Max for Live MIDI Effect
- **UI Rendering:** `jsui` canvas (MGraphics) in a popup/floating window via `thispatcher`
- **Timing Engine:** `phasor~ 16n @lock 1` (sample-accurate, NEVER `metro`)
- **Data Storage:** `dict` objects with `@parameter_enable 1` for Live Set persistence
- **Pattern Management:** `pattrstorage` for preset slots
- **Drag-and-Drop:** `live.drop` for file detection with smart MIDI note assignment
- **MIDI Output:** Standard M4L MIDI outlet → Drum Rack

## Version Roadmap
- **v1.0 (Launch):** 16-32 channels, 16/32 steps, per-step velocity, mute/solo, colors, pattern system (4), global swing, transport sync, popup UI, smart MIDI assignment
- **v1.5 (Update):** Per-step pitch/shift/length, graph editor, 64 steps, per-channel swing, ghost notes, 8+ patterns, MIDI clip export, channel reordering
- **v2.0 (Major):** Auto-load samples into Drum Rack, pattern chaining, step probability, fill generation, preset library
