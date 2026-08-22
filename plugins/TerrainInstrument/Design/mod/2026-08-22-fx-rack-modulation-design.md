# FX RACK MODULATION — LFO & ENV on every rack knob (design, 2026-08-22)

Max: *"next, we need to add modulation for these, LFO & ENV."*

**Decided with Max before writing this:** every continuous knob (front 4 + back 8) on every device,
every instance · ENV means the existing envelopes 1..32 (not a new per-device follower) · and the
modulated knob wears **the existing moving underline**, unchanged — *"you can keep it the same as
how the modulation is already."*

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

**CORRECTED after reading all 12 ref structs (the first draft of this section was wrong).** The read
sites are NOT uniform. Only the newest kinds carry `f1/f2/f3/mix/b[8]`; the older ones carry named
members in their own order — `RvbRefs{size decay tone mix predelay diffuse moddepth modrate hidamp
lowdecay lowcut width}`, `TpeRefs{p1 p2 p3 mix time repeats drive age …}`, `FltRefs{cut res drive mix
env track poles …}`. So there is no single mechanical substitution, and the obvious repair — a
hand-written `knob index → struct member` map, 184 entries — is a map where one slip silently
modulates the WRONG knob and every gate still passes.

**So the match is made by POINTER IDENTITY, never by index arithmetic:**

1. `kFxModIds[16][12]` names the parameter behind each dial (the Filter's 8 back slots are `nullptr`).
   It is **GENERATED from the UI's own card definitions** and committed beside them, exactly like
   `Design/fx4/eq/shipped_labels.inc` — with a gate that regenerating produces no diff. That is what
   keeps the dial and the destination authored in ONE place.
2. That table caches `fxModRef_[kind][inst][knob]` — the same `std::atomic<float>*` the read site
   already holds.
3. Per block, the ≤128 assignments are walked ONCE into a sorted `(pointer → modulated value)` array.
4. Every read site's `V.size->load()` becomes `M (V.size)` — a pure textual substitution that carries
   no index and therefore **cannot mis-map**. `M` binary-searches the sorted array (≤7 compares) and
   falls back to `->load()`; with no routes it is one branch.

The equivalence this buys is directly testable, and it is what §7 gates: **modulating knob k by x must
produce bit-identical audio to moving knob k's own parameter by x.**

- **CPU is O(routes), not O(destinations).** The 1,152 dests are never iterated.
- **The viz moves for free.** Every core draws from the engine's OWN meters (fb432) and the engine now
  receives the modulated value — an LFO on Bode's Shift slides the streams, on the Splitter's crossover
  walks the band edge. "Everything audible interacts visually", satisfied by construction.
- ⚠️ **`kDestInfo` is a 694-row literal sized by `NumDests`.** Growing the enum without growing the
  table zero-fills the new rows — `fullScale = 0.0f` — and every FX route would silently do NOTHING.
  The table becomes `kDestInfoBase[694]` plus a constexpr builder that fills the FX range with
  `{Linear01, 1.0f}`, and §7 gates that every FX dest reports 1.0.
- Devices that are powered off / not in the chain apply nothing.

## 4. What a modulated rack knob LOOKS like — the existing underline, verbatim

**Max, deciding this: *"you can keep it the same as how the modulation is already, we have that
moving underline."*** So: no second arc, no Serum-style ring, no new pattern. A modulated rack knob
wears the same `.sm-ul` every modulated control in the plugin wears, and it already moves —

- **THE COMET (fb189)** rides the underline: `territory = [(1−d)·knob, (1−d)·knob + d]`, and the
  live value travels inside it. Fast attack streaks, a slow envelope pours, an LFO breathes.
- purple = the SELECTED modulator's route lives here, dim = someone else's (fb182/fb257), so
  switching modulator flips the whole map instantly.
- hover = the route list · click = pin it · vertical drag = depth (fb188/fb190).

🔑 **This also deletes the problem the other option created.** A second live arc would have needed
the engine's effective value pushed at 60 Hz — because a dial that recomputes in JS what the DSP
did is the "display disagrees with the audio" failure fb452 just spent an evening killing. The
comet is fed by the modulator's own value, which the UI already receives, so there is nothing new
on the wire and nothing new that can disagree.

## 5. UI

- The card renderer hangs `data-mod-dest="<int>"` on the 4 front dials and the 8 back dials, recomputed
  whenever a card's kind or instance changes (the renderer knows both).
- Everything else is inherited: the existing module handles the drop, the underline, the comet, the
  hover route list, the pinned list, and the vertical depth drag (fb188 — the letter badges are
  retired, the underline IS the grammar). **Nothing new is invented and no new interaction is taught.**
- **The one rack-specific fix:** the underline measures the word's INK with a Range over
  `.knob-label` so it covers the whole word exactly (fb188, Max's requirement). Rack labels are not
  `.knob-label`, so today the lookup would fall back to the element box and underline too much —
  the label lookup learns the rack's label class. One line, and it is gated.
- **Routes survive a re-render for free:** `curEl()` already re-resolves an assignment whose element
  went stale (fb145, for rebuilt card grids), which is exactly what a rack card does on every add,
  delete, reorder and flip.
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
2. **`au_fx_path` (extended, the REAL AU)** — the fb373 law, as an EQUIVALENCE over the full matrix
   (fb425: sweep the matrix, not a sample). For all 184 live (kind, knob) cells: install a route
   through the same `setSynthMod` bridge the UI uses, render, and assert the audio equals the audio
   from moving THAT knob's own parameter by the same amount. A mis-mapped knob fails immediately,
   which is the whole reason the mapping is generated rather than typed.
3. **Mutation (mandatory, fb421)** — delete the mod add at the injection site: `au_fx_path`'s mod gates
   must go RED while every existing gate stays green.
4. **`fx4_ui.js` (extended)** — every rack dial (front and back, every kind) carries a
   `data-mod-dest`; all are unique; a synthetic drop writes the route and raises an `.sm-ul` on that
   dial; the underline's width equals the LABEL'S INK, not the element box (the fb188 requirement,
   which is the one thing the rack does not get for free); and the route survives a card reorder,
   which is the fb145 path this depends on.
5. **Mockup first (house rule)** — the live-dial arc is a UI change, so it goes to Safari as real code
   for Max's approval before any of it is built.

## 8. Risks

- **The read-site edit is wide (184 loads across 16 blocks).** It is a pure `X->load()` → `M(X)`
  substitution carrying no index, scripted rather than hand-typed, and the equivalence matrix in §7
  is what proves each one landed on the right knob.
- **The underline is positioned in VIEWPORT space** (`position:fixed`, measured per frame from the
  target's rect). The rack scrolls, and cards move under it — the repaint is already per-frame so it
  should track, but a scrolling container is a case the synth panel never exercised. Gated: scroll
  the rack and prove the underline stays on its word.
