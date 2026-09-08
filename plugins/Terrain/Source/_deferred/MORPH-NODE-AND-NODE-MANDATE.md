# MORPH NODE + NODE MANDATE + NORTH STAR (2026-07-14)

**What this is:** design/reference notes for the Terrain Patcher endgame — the modular,
node-based future of Terrain Instrument. Captures the **Morph node** concept, the **hard rule**
that every module from now on is built node-ready, and the **north star** (expressionism +
gestures) that all of it serves. **Documentation only — no code here, nothing to compile.**

> This lives in `_deferred/` because the patcher itself is a future phase. The ideas below are
> decided and locked; they just don't get built until the patcher work begins. Re-read this
> before starting any patcher/node work, and keep it in mind while building filter DRIVE TYPES
> and STEREO SPREAD now (see Node Mandate).

---

## 🌀 Morph Node — DEFERRED (build during the Terrain Patcher phase, NOT now)

A patcher node that **crossfades / sweeps between two signals** — the input it receives and the
downstream-processed version of that same input.

- **Placed INLINE between any two modules** in the patch graph.
- **Canonical example:** `Filter 1 → Morph node → Filter 2`.
  - The Morph node takes **Filter 1's output** as its input.
  - It outputs a **blend that sweeps between the input (Filter 1) and the downstream-processed
    version (Filter 2)**.
  - So the user can **morph from one filter INTO another in real time** — a continuous sweep,
    not an A/B switch.
- **Not filter-specific.** It can be dropped between **any two things** in the graph (osc→osc,
  fx→fx, engine→fx, etc.). Filters are just the headline demo.
- **Only makes sense as a patcher node.** It is **NOT** a filter back-panel control.
  - We considered a back-panel "Morph" pill and **rejected it** — this node concept **replaces**
    that idea. Do not resurrect the pill.
- **Purpose:** expressive, sweeping **GESTURES**. This is a headline example of the plugin's
  north star (below) — reach for a knob/macro/LFO and hear the sound continuously transform.

---

## 🔒 Node Mandate — HARD RULE

**Every module we build from now on must be designed node-READY for the Terrain Patcher.**

- Build each module as a **self-contained DSP unit** with a clean **input → process → output**
  boundary — no reaching into global state, no assumptions about who feeds it or who it feeds.
- Goal: any such module can be **lifted into a patcher node later with minimal rework**.

### Applies right now
- **Filter DRIVE TYPES** and **filter STEREO SPREAD** (current in-progress work) must be built as
  **clean, self-contained DSP** so they **port cleanly to nodes** when the patcher lands.

### Retrofit task (later)
- Once the current filter work settles, **convert the existing oscillators and filters into
  nodes**. Do this after DRIVE TYPES / STEREO SPREAD are stable — don't churn the filters mid-flight.

---

## ⭐ North Star — HARD RULE

**The whole design goal is EXPRESSIONISM and GESTURES** — making the instrument feel **alive**
through expressive, sweeping, gestural control.

- **Morph-style crossfades are a primary vehicle** for this (the Morph node is the flagship).
- **Favor features that add expressive movement.** When choosing what to build or how to build it,
  bias toward things the player can *sweep, morph, and gesture with* in real time.

---

## 🏁 Competitive Context — why the patcher matters

- **OK Synthesizer** (the company) just released a **modular EFFECTS patcher** — ~100 effects,
  but **effects-ONLY**.
- **No plugin currently ships a full modular SYNTH + FX node patcher.** Oscillators, engines,
  filters, mod, AND effects all as patchable nodes in one graph.
- **That gap is exactly what the Terrain Patcher fills.** It is the differentiator. Keep pushing
  toward it, and keep building every new module node-ready (see Node Mandate) so we can get there.

---

## See also
- `MEMORY.md` → `terrain-instrument-node-architecture-patcher.md` — the broader NODE architecture
  + Terrain Patcher vision ("every module is a NODE").
- Blend Modes (already shipped, cross-osc FM/PD/AM/RM) — related "signals interacting" energy,
  but distinct: Blend *modulates*, Morph *crossfades*.
