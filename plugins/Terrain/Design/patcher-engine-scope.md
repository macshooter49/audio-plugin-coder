# The Terrain Patcher — engine scope (phase 2)

*Written 2026-09-14, the evening phase 1 shipped. Phase 1 is the Patcher page driving the engine Terrain already has
(see `Design/patcher-mockup.html` for the visual truth and the memory note `terrain-patcher-build-tp1`). This is the
scope for the part that needs new DSP. Nothing here is built. Max's laws apply: research → scope → mockup → sign-off →
engine; no preset may break; no parameter is born at runtime; click-free; CPU-friendly.*

## What phase 1 already gives (no engine change)
| Cable on the canvas | The parameter it is |
|---|---|
| Osc → Filter | `SYN_OSC_x_F1MIX` / `F2MIX` (the F1/F2 send) |
| Osc → effect card | `SYN_<fx>_SRC_x` (the card's route pill) |
| Card → card | the rack order, `_RANK` (a float, so any length) |
| MIDI In → Arp / Robin, audio → Chop / Glitch | `FLOW_CHAIN_1..4` (the chain order) |
| LFO n / Envelope → knob dot | the mod matrix (`synModJson`) |
| Dry path → Resonator → Grain → Tape → Out | the master inserts, in `processBlock`'s order, while switched on |

Everything a preset ever saved opens as its own environment. The layout rides in the fb621 `patcherJson` seat.

## What phase 2 is for
1. **Unlimited oscillators.** Today a voice renders exactly four oscillators (`SynthVoice`, `SYN_OSC_A..D_*`, 156 params each).
2. **Per-oscillator note stages** — "osc C is an Arp, osc B is a Robin" — today the Arp/Robin act on the whole note stream.
3. **Any number of Chop / Glitch / Resonator / Tape / Grain nodes, in any order, on any branch** — today one of each, global.
4. **The tape loop recorder and the three tape machines as routable modules** — today master inserts.
5. **Cable mute and level**, feedback loops (a one-block delay), Terrain FX's own patcher (audio in → audio out).

## The shape that keeps every preset intact
- **Parameter law.** A node's knobs are parameters, and parameters cannot be born at runtime. So every node kind gets a
  **fixed pool** of instances at build time, exactly as the rack does (16 kinds × 6): oscillators 4 → **8**, Arp/Robin/Chop/
  Glitch 1 → **4 each**, Resonator 1 → 2, Tape machine 1 → 2, Tape loop 1 → 2, Grain 1 → 2. Instance 1 of everything keeps
  its current IDs (the 51 presets and every automation lane stay valid); instances 2..n are appended IDs. The cost is the
  APVTS size (≈ +700 oscillator params, +300 FLOW), the host's parameter list, and `PresetCarries` — all measured before
  committing, the fb601 way.
- **Two graphs, one file.** The environment blob grows a `graph` section: nodes `{kind, inst, x, y}` and cables
  `{from, fromPort, to, toPort, level, mute}`. The audio thread never reads JSON: the message thread compiles the graph into
  a **fixed-size schedule** (arrays of `{kind, inst, srcMask, dstBus}` — the same idea as `chainOrder_` and the fb347 union
  masks) and swaps it in with an atomic pointer, click-free, exactly as the rack's chain rebuild does today. A patch with
  no `graph` section compiles to the default graph = the fixed topology of today → byte-identical audio (the golden-bank
  law, `scratchpad/abS.sh`).
- **Per-voice vs bus.** Oscillator nodes and their filters run per voice (they already do); the first node that is not
  per-voice on a branch merges the voices onto a bus (the Phase Plant / Serum model). Per-oscillator note stages: an Arp
  node in front of an oscillator gives that oscillator its own note stream, which means a voice must be able to carry a
  different note per oscillator — the one deep change. Recommended first cut: an Arp/Robin node owns a **voice group**
  (the oscillators cabled from it), and `SynthVoice` renders per group with its own MIDI stream. Chords + arps in one patch
  work because groups are independent.
- **Bus nodes** (Chop, Glitch, Resonator, Tape, Grain, every rack card) already have bus-agnostic `process(L, R, n)`
  interfaces ("the node architecture mandate", 2026-06-30). Multiple instances = the pool. Branching = per-branch buses,
  merges = sums, ordering = the compiled schedule. The tape loop as a node captures whatever bus feeds it — which is
  exactly what Max asked for ("something that captures all the audio and we manipulate it on the spot").
- **CPU.** Nodes with no path to Out are not scheduled (the canvas already dims them); pooled instances cost nothing until
  they are in the schedule (the rack's pooled-instance rule, fb346); one compile per edit, never per block.

## The order to build it
1. The compiled schedule for the **bus** graph (rack cards, Chop/Glitch, Resonator, Tape, Grain, Tape loop) with pooled
   instances and per-branch buses. Every preset still compiles to today's chain. Golden bank 0/52 different.
2. Cable level + mute (schedule entries carry a gain; mute = gain 0 with a ramp).
3. Oscillator pool 4 → 8 (IDs appended), the canvas adds oscillators from the list.
4. Voice groups: per-oscillator Arp/Robin.
5. Feedback (one-block delay on a marked cable), then Terrain FX's patcher on the same runtime.

Each step gets its own perceptual/golden test before the next, and Max hears it before it is called done.

## Two decisions still Max's
- **The hero page.** The Patcher replaces it; the Grain and Tape sections now live on the canvas. The XY terrain display,
  the randomiser and the chopper are the only hero furniture left. Today the MOD pill still shows the mod panel over the
  hero — where should the mod panel live once the hero is gone (over the synth page? over the canvas?).
- **Environment vs preset in the browser.** A patch saved from the Patcher carries `nodes` and shows as an environment.
  Should a patch that never opened the Patcher also count (every patch is a graph now), or only patched ones?
