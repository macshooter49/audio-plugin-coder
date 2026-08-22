# FX RACK MODULATION — LFO & ENV on every rack knob (design, 2026-08-22)

Max: *"next, we need to add modulation for these, LFO & ENV."*

**Decided with Max before writing this:** every continuous knob (front 4 + back 8) on every device,
every instance · ENV means the existing envelopes 1..32 (not a new per-device follower) · a modulated
dial shows the modulation **riding the dial**, Serum-style.

---

## 1. What already exists (this is wiring, not architecture)

| piece | where | what it gives us |
|---|---|---|
| block-rate mod applied at the **processor param-push site**, not in the voice (fb75) | `PluginProcessor.cpp` `flowKnob`/`flowMod` | the exact stage the FX rack reads its params in |
| `flowLfo_[10]` — a global, block-rate copy of the LFO bank | `PluginProcessor.h:1310` | an LFO value that exists with no voice playing |
| `monoEnvLevelOf()` + the ownership law (fb184) | `PluginProcessor.cpp:8858` | how a **per-voice** envelope drives a **global** knob |
| `ModDest` — a plain append-only int enum, carried in `synModJson` | `SynthModConfig.h` | destinations can grow without a state break (it is NOT an APVTS choice) |
| `MAX_ASSIGNMENTS = 128` | `SynthModConfig.h:949` | the route ceiling, already generous |
| the drop-target scan `#syn-panel [data-mod-dest]` | `index.html:28469` | the rack is inside `#syn-panel`, so a dial that declares a dest is **already** a drop target |

**THE LAW WE INHERIT UNCHANGED (fb184):** an **LFO ADDS** to a knob; an **ENV OWNS** it. Effective =
`(base + Σ lfo·depth) · (1 − w) + Σ|depth|·(env+1)`, with `w = min(1, Σ|depth|)` over env routes.
An env-modulated FX knob therefore behaves exactly like an env-modulated FLOW knob. No new grammar.

## 2. The destinations

Appended at the end of `ModDest`:

```
FxModBase = 694            // == the current ModDest::NumDests, measured, not assumed
dest = FxModBase + (kind * kFxInstances + instance) * 12 + knob
   kind     0..15   (kFxKinds)
   instance 0..5    (kFxInstances)
   knob     0..3    the four front dials (three + Mix)
            4..11   the eight back knobs (b1..b8)
NumDests  = 694 + 16*6*12 = 1846
```

Every FX knob is a normalised 0..1 parameter, so all 1,152 share ONE `kDestInfo` shape
(`Linear01`, fullScale 1.0) — the table is filled by a loop, not by 1,152 hand-written rows.

**The 12-wide block is FIXED even where a device has fewer knobs.** Audited: 15 kinds are 4 front +
8 back; the **Filter is 4 knobs total** — fb384, Max: *"I don't even want a back panel anymore"* — so
its block carries 8 permanent holes. A hole costs one table row and can never be assigned (the UI
only ever hands out dests it renders), and paying for it buys arithmetic that stays true forever:
no per-kind offset table to keep in sync, and a device that grows a back panel later slots straight
in without moving anybody else's dest ints.

Append-only ⇒ every saved project keeps its existing dest ints. Nothing reshuffles.

## 3. Where it applies

Each device already reads its params in one uniform shape (`V.f1->load()`, `V.b[k]->load()`), so one
helper replaces the load at all 16 read sites:

```cpp
// fxMod(V.f1, kind, i, 0) == jlimit(0,1, base + lfoSum) * (1-w) + envV   — fb184's math, verbatim
q.gain = fxMod (V.f1, kFxKindUtility, i, 0);
```

- **CPU is O(routes), not O(destinations).** Once per block, walk the ≤128 assignments and accumulate
  into a small sparse map keyed by dest; the 1,152 dests are never iterated. A rack with no routes
  costs one branch per knob.
- **The viz moves for free.** Every core draws from the engine's OWN meters (fb432) and the engine now
  receives the modulated value — an LFO on Bode's Shift slides the streams, on the Splitter's crossover
  walks the band edge. This satisfies "everything audible interacts visually" by construction, with no
  new viz code.
- Devices that are powered off / not in the chain apply nothing and push nothing.

## 4. The live dial

The dial draws its base arc plus a second arc riding the **modulated** position at 60 Hz.

🔑 **The live value is the ENGINE's, pushed — never recomputed in JS.** The UI already holds every LFO
and env value (`mvL[]`, `mvE[]`) and already does ownership math client-side for the envelope breathing
curve, so recomputing here would be the cheap path — and it is exactly the "display recomputes what the
DSP did" failure fb452 spent an evening killing. The processor publishes the effective value it actually
handed the engine, for modulated knobs only (≤128 floats ≈ 700 B/frame, the EQ curve's order), on the
existing 60 Hz fx4 push.

## 5. UI

- The card renderer hangs `data-mod-dest="<int>"` on the 4 front dials and the 8 back dials, recomputed
  whenever a card's kind or instance changes (the renderer knows both).
- Everything else is inherited: the existing module handles the drop, the `.sm-modded` underline, the
  hover route list, the pinned list, and the vertical depth drag (fb188 — the letter badges are retired,
  the underline IS the grammar). **Nothing new is invented and no new interaction is taught.**
- Back-panel knobs behave identically, behind the flip.

## 6. State and edge cases

- **Chain reorder** does not touch instance identity (routes are keyed to the pool slot), so a route
  survives a drag-reorder — which is what a user expects: the modulation belongs to the device.
- **Deleting a device** drops its routes (the UI prunes any assignment whose dest falls in that
  device's 12-wide block), so a later card in the same slot never inherits a ghost.
- Pills, Types and dropdowns are **out of scope** (Max's call): a boolean needs threshold semantics,
  which is its own design.
- `synModJson` is unchanged in shape — `{s,d,v}` — so persistence, undo and preset behaviour are
  untouched.

## 7. How it gets proven

1. **`fxmod_cert` (new, C++)** — the dest arithmetic exhaustively: 1,152 dests unique, contiguous, none
   colliding with an existing dest (`FxModBase == 694 == the old NumDests` is itself a gate, so the
   day another dest is appended without moving the base, it FAILS), every one round-tripping
   (kind,instance,knob) → dest → back. And the
   apply helper against fb184's math on a table of (base, lfo, env, depth) cases.
2. **`au_fx_path` (extended, the REAL AU)** — the fb373 law: for each of the 16 kinds, install a route
   through the same `setSynthMod` bridge the UI uses, render, and assert **the audio moved** — not that
   a parameter changed. One knob per kind, chosen as the one with the most obvious spectral signature.
3. **Mutation (mandatory, fb421)** — delete the mod add at the injection site: `au_fx_path`'s mod gates
   must go RED while every existing gate stays green.
4. **`fx4_ui.js` (extended)** — every rack dial (front and back, every kind) carries a `data-mod-dest`;
   all are unique; a synthetic drop marks the dial `.sm-modded` and writes the route; the live arc reads
   the pushed value and not a JS recomputation (push a value that disagrees with the LFO feed and prove
   the arc follows the PUSH).
5. **Mockup first (house rule)** — the live-dial arc is a UI change, so it goes to Safari as real code
   for Max's approval before any of it is built.

## 8. Risks

- **The 16-site edit is mechanical and wide.** It will be scripted and every site gated, not hand-typed.
- **The live arc is a new pattern** — nothing else in the plugin draws a modulated position. If it reads
  busy on a full rack, the fallback already discussed is to show it only for the selected modulator.
