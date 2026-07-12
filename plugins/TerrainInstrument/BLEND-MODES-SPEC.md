# BLEND MODES — Implementation Spec (Serum-2-style cross-osc warp)

**Status:** researched, not built (2026-07-12). No code yet — this is the plan to build from.
**Research:** 6-agent deep dive (Serum 2 manual + screenshots + FM/PD/AM/RM DSP + comparative synths
Vital/Pigments/Massive X/Phase Plant/Bazille + our codebase, file:line-grounded). Sources at bottom.

> **The one-sentence version:** each oscillator gets **4 warp slots (B1–B4)** that let it be modulated by
> **another oscillator / its Sub** using **FM, PD, AM, RM** (plus self shapers), where the modulator is
> **tapped before its output volume** — so you turn osc C's volume to 0 and still hear it warp osc A.
> That "silent modulator" behavior is the whole magic, and our engine already has the tap point.

> **ALL FOUR OSCILLATORS, ANY-TO-ANY (Max, 2026-07-12).** This is NOT osc-A-only. Every osc A/B/C/D gets
> the identical back panel + right-click menus + 4 slots = **16 slots total (`blend_[4][4]`)**, and any osc
> can modulate any other (A→C, D→B, C→A, …). Sources for a slot = the OTHER three oscs + that osc's Sub
> (+ Self for PD). Serum has ~6 ports (3 oscs × 2 warp slots); we have 16 (4 oscs × 4 slots). Same code
> stamped ×4 carriers.

---

## 1. THE MODEL (what we're building)

Right-click a blend slot → glass menu → pick **FAMILY** → pick **SOURCE** → left-drag = **DEPTH 0–100%**.
This is Serum's warp model 1:1, mapped onto our 4 slots (Serum has 2). Each osc has:

- **B1 · B2 · B3 · B4** — four warp slots, processed as a **series chain** (B1→B2→B3→B4). Each defaults **Off**.
- **Mix** — master dry/wet of the whole blend section (see §7).
- **Mod** — a global modulator control (see §7).

Each slot = `{ family, source, depth }`.

### Families (menu root, in order)
| Family | Kind | Source? | Ships in |
|---|---|---|---|
| **Off** | — | — | P1 |
| **FM** | cross-osc | yes + Mode(Thru-Zero/Exp/Linear) | **P1** |
| **PD** (phase mod) | cross-osc | yes (+ Self) | **P1** |
| **AM** | cross-osc | yes | P2 |
| **RM** | cross-osc | yes | P2 |
| **Alt Warp** (Bend/Asym/PWM/Mirror/Flip/Quantize…) | self | no | P3 (reuse existing) |
| **Distortion** (Soft/Hard Clip, Folds, Rectify, Sat…) | self | no | P3 (reuse existing) |
| **Sync** (hard↔soft) | self | no | P4 (needs minBLEP — hardest) |
| **Filter** (LPF/HPF on the osc) | self | no | **P3 — INCLUDED** (per-osc one-pole; depth=cutoff) |
| _Filter 1/2 as a **source**_ | cross | — | Deferred to routing pass (architectural, §5/§7) |

### Sources (submenu for FM/PD/AM/RM)
`B · C · D · Sub` today. **Self** only for PD. **Noise** and **Filter 1/2** shown greyed `soon` (§5).
(We uniquely have 4 oscs A–D + a per-osc Sub — natural sources Serum lacks.)

---

## 2. THE DSP — one primitive builds most of it

**Key insight (agent 1):** FM and "PD-from-source" are the **same operation** — phase modulation (inject
the modulator sample into the carrier's read-phase). That's what DX7 and Serum actually do. Build **one**
phase-inject path, expose it as both FM and PD. AM/RM are **one multiply** path. Self-warps **reuse existing
code**. So the real new DSP surface is tiny.

### 2a. PHASE MODULATION — powers FM(Thru-Zero) + PD(source). THE core primitive.
```
// per carrier sample (WT or FM engine only — they have a live phase accumulator)
phase  += inc;  phase -= floor(phase);              // normal carrier advance
readPh  = phase + betaCycles * modSample;           // inject modulator (in [-1,1])
readPh -= floor(readPh);                             // floor() handles NEGATIVE -> thru-zero for FREE
out = readCycle(table, readPh);
// betaCycles = depth curve, ~0..2 cycles. Thru-zero is automatic (backward read when mod dips hard).
```
- **Thru-Zero = the default FM mode** — stays perfectly in tune at any depth (critical for basses; matches
  our piano's "a-C-is-a-C" pitch-anchoring ethos). Exp/Linear are alternate "gnarly" modes in the Mode row.
- **Exp FM** `inc *= pow(2, D*mod)` — detunes sharp by design (label it; don't "fix" it).
- **Linear FM** `f = f0 + I*mod`, clamp ≥0 — classic in-tune-until-extreme.
- **PD(Self)** = DX7 feedback: `readPh = phase + beta*fbState; fbState = 0.5*(fbState+out)` (averaged =
  anti-blowup). We already have this exact `fmFbA_` pattern.

### 2b. AM / RM — one level-safe multiply (agent 2)
```
m  = dcBlock(modSample);                    // zero-mean -> clean carrier suppression
mm = isRing ? m : 0.5*(m + 1.0);            // RM bipolar / AM unipolar  (the ONLY difference)
g  = 1.0 - depth + depth*mm;                // LINEAR dry->wet crossfade
out = carrier * g;                          // |out| <= |carrier| ALWAYS -> no clipping, no makeup
```
- **Do NOT** use textbook `C*(1+m*M)` (peaks +6 dB, clips). The crossfade form is peak-safe by construction.
- **DC-block the modulator** or RM won't fully null the carrier. Linear crossfade (not equal-power) — the
  dry/wet share the carrier factor and are correlated (same lesson as our sample-loop crossfade).
- **No Self for AM/RM** (a signal × itself = squaring, not musical — Serum omits it too).

### 2c. Self-warps (Alt Warp / Distortion / Sync) — REUSE what we ship
Agent 5: we already have `applyPhaseWarp()` (Bend/Sync/Formant/PWM/Skew/Mirror/Fractalize/Quantize),
`applyAmpWarp()` (Rectify/Sine-Shaper), `applyFoldADAA()` — used today as our 2 wavetable warp slots.
A self blend slot just routes into an extra chained call. **Near-zero new DSP** for the whole Alt-Warp +
Distortion families. Sync (hard/soft) is the one genuinely hard piece (needs minBLEP at the reset) → P4.

### 2d. Depth curve + smoothing (all agents)
`depth = (exp(P*knob)-1)/(exp(P)-1)`, P≈2 (our house exp-bias law, **not** a power curve). Smooth per-sample
toward target with the existing `fmD1Sm_`-style one-pole (0.35 coef) — **raw block-stepped depth = crackle.**
`depth≈0` must be a **bit-identical bypass** (early-out) so an empty slot costs nothing.

---

## 3. THE TAP — "volume down but still modulates" (the #1 requirement)

**Great news (agent 5): the tap point already exists.** In `SynthVoice.h` the per-sample loop (`:1905`)
renders all 4 oscs into locals `sA_L … sD_L`, and per-osc **level/pan/gate/VCA is applied only at the sum**
(`:3036-3037`). So `sA_L … sD_L` **are** the pre-gain taps. And `LEVEL=0` still renders (culling is driven by
`oscDead_` = mute/solo/robin, **not** level, `:1888`). So "turn C's volume down, still warps A" is **free**.

Implementation:
- Add `float modPrev_[5]` per voice. After the sum (`:3037`) write each osc's pre-gain sample (+ `subTap_`).
  FM/PD read `modPrev_` (a **1-sample delay** — mandatory since oscs render A→B→C→D in one iteration; ~0.02 ms,
  inaudible, and it's what breaks the A↔B feedback loop safely).
- AM/RM read the **current-sample snapshot** (all `sX_L` exist at the sum) — snapshot all four first so
  mutual mod is order-independent.
- **Healer/watchdog (per our "healers everywhere" rule):** OR a `tappedAsModulator` flag into the
  render-needed condition so an osc referenced as a source is **never culled** even if muted. Prevents the
  classic "warp dies when I mute the source" support trap. (Test: source LEVEL=0 → modulation still audible.)

---

## 4. ENGINE GATING (menu adapts to the carrier)
- **FM / PD** need a live per-sample phase accumulator → only on **WT** and **FM** carrier engines.
- **AM / RM / Distortion** are post-read multiplies/shapers → work on **all** carriers (incl. the block
  engines Sample/Granular/Resynth/Harmonic/Modal, which read out per-sample at `:2057`).
- **Alt Warp / Sync** are phase-domain → WT (and FM) carriers.
- The glass menu **greys out** families the current carrier can't do (no silent no-op selections).

---

## 5. WHAT'S DEFERRED (and why)
- **Filter 1/2 as a source:** our 2 filters run **post-sum, per-block, after** the osc loop (`:3204`) — their
  output doesn't exist inside the per-sample osc loop. Would need a 1-block-delayed tap or a full restructure.
  They're Serum's most exotic sources → **omit in v1**, grey `soon`.
- **Noise as a source:** no Noise engine yet → grey `soon`, wire as one appended source when it ships.
- **Sync:** ships P4 (minBLEP is the one hard DSP piece).

---

## 6. UI — ZERO new furniture (agent 6)
Everything exists; scope carefully:
- **Slots:** the 6 `.blend-pill` divs (`index.html:5524-5531`, B1-B4/Mix/Mod) are **bare placeholders, no JS**.
  ⚠️ `.blend-pill` is ALSO used by the filter routing row — scope to `#syn-panel .back-only .blend-pills .blend-pill`.
- **Menu:** `window.__synShowMenu(header, items, x,y)` (`:13568`, element `#syn-ctx-menu :6822`). It's a FLAT
  list but **drills in via `keepOpen:true`** (rebuild same menu) — exactly how the **envelope hub `showHub()`
  (`:13491`)** does family→source→depth. **Copy `showHub` almost verbatim** for `showBlendMenu()`.
- **In-menu depth slider:** clone `buildDepthNode()` (`:13442`) → unipolar `buildBlendDepthNode` (drop the
  center tick, anchor fill left, `setDepth` via `__setSynParam`). Keep its `stopPropagation` or dragging closes the menu.
- **On-pill %/drag (Serum shows "Sync 45%"):** the **WARP2 pill `initWarp2Pills()` (`:12862`)** is the exact
  structural twin — right-click = mode menu, left-vertical-drag = amount%, engine-conditional. Mirror it.
- **Menu vocab reused:** `isHeader` sections ("Blend N"/"Source"/"Mode"), `isChecked` purple tick, `badge`
  for the active source, `isDisabled+badge:'soon'` for Noise/Filter/Self-where-N/A. No new CSS.
- Keep the menu **inside `#syn-panel`** (all `.syn-ctx-*` CSS is scoped there — CLAUDE.md rule).

---

## 7. OPEN QUESTIONS — decisions (for Max sign-off)
- **Why 4 slots vs Serum's 2?** Keep **4** as a series chain; justified because we have 4 sources (B/C/D/Sub).
  Common case is 1–2. Dim B3/B4 until B1/B2 are used, to fight overwhelm.
- **Mix box** → **master dry/wet of the whole blend section** (0% = clean carrier bypass, 100% = fully warped).
  One global "how much character" knob + a clean bypass. (Best label: "Amount" or keep "Mix".)
- **Mod box** → **DECIDED (Max): self-feedback / grit** — a global phase-feedback amount (DX7/Massive-X/
  Bazille "self" character), unique + cheap + always-wanted. LFO/env motion on depths is already covered by
  our mod matrix, so this doesn't duplicate it.
- **Swap Warps?** **Drop it.** Serum needs swap because its 2 slots are fixed; our B1–B4 are freely
  right-click-reassignable, so swap is redundant. (Order matters; a drag-reorder can come later if wanted.)
- **Filter (Max highly wants it) — split into two things:**
  - **Filter as a warp FAMILY (LPF/HPF applied to the osc in the slot)** — Serum's `Filter ▸ LPF/HPF`. A small
    per-osc one-pole in the slot, depth = cutoff. Easy, no rebuild. **INCLUDED (P3).** "Filter" appears in the menu.
  - **Filter 1/2 as a modulator SOURCE (`FM (Filter 1)`)** — **deferred.** Our 2 filters process the SUMMED mix
    of all oscs in a loop that runs AFTER the osc render loop, so their per-sample output doesn't exist when a
    carrier needs it (only last block's). A clean version needs MERGING the osc-render loop and the filter loop
    into one per-sample pass — rebuilding the working 27-type/7-core filter path into the osc loop + re-tuning
    CPU. Days of work + regression risk, for Serum's least-used source. A 1-block-delayed tap would be laggy AND
    a feedback loop (the filtered mix contains the carrier being modulated). Do it PROPERLY during the routing/
    mod-matrix pass (already on the roadmap — that's where per-sample tapping gets rebuilt anyway).

---

## 8. PARAMS
Fresh prefix (⚠️ `SYN_OSC_x_BLEND_*` is TAKEN by the offline sample-baker — do not reuse):
`SYN_OSC_{A..D}_WSLOT{1..4}_MODE` (choice), `_SRC` (choice), `_DEPTH` (float 0..1) → 48 params,
plus `SYN_OSC_{A..D}_WMIX`, `SYN_OSC_{A..D}_WMOD`. **Write via `window.__setSynParam`** (the proven
relay-bypass — the MODAL engine died on the 700+-relay scale bug; don't repeat it). Register relays for
**reads** (live refresh via `valueChangedEvent`). Setters beside `setWarp2CD` (`SynthVoice.h:1162`), pushed
from `PluginProcessor.cpp:~3913`. (AudioParameterChoice: read via `getScaledValue()`, not raw.)

---

## 9. ANTI-ALIASING (respect the CPU rule — Serum is the bar, thin gracefully)
- Extend the **existing FM-aware mip pick** (`mipLevelForPhaseIncrement` + `fmRateMul`) to blend depth so
  heavy warp auto-dulls the carrier mip (no high harmonics to alias).
- **Optional 2× oversample** the modulated carrier/multiply for aggressive slots — reuse the existing 2× path
  (`SynthVoice.h:~3234`). Gate it: only when a slot is active AND (carrier bright) AND depth>0. Skip for
  Sub/low modulators.
- DC-block AM/RM; clamp `modPrev_` magnitude; lean on the existing NaN/|x|>30 guard (`:3259`).

---

## 10. BUILD PLAN (increments — the dopamine-first order)
- **P1 — the magic, minimum:** params + `modPrev_` tap + the PM primitive → **FM + PD from B/C/D** on WT
  carriers, with the glass menu (family→source→depth) wired on B1–B4. Verify the "silent modulator" in the
  DAW (source LEVEL=0 still warps). *This is the night-and-day moment.*
- **P2 — AM/RM:** the multiply path + DC-block, on all carriers. Add **Sub** as a source.
- **P3 — self families + panel:** Alt Warp + Distortion (reuse `applyPhaseWarp`/`applyFoldADAA`), engine
  gating, **Mix** + **Mod**, on-pill % readout, anti-alias polish.
- **P4 — Sync** (minBLEP) + revisit Filter/Noise sources when those land.

---

## Sources
Serum 2 manual pp.49–56 (warp taxonomy, FM Thru-Zero/Exp/Linear, "modulator enabled but volume down",
PD/AM/RM = phase/amp/ring "instead of frequency", Swap Warps) + 9 screenshots (menu structure).
DSP: learningmodular (Exp/Lin/TZ FM), righto.com + ajxs.me (DX7 = phase modulation), Casio CZ phase-distortion
(wikipedia/electricdruid), ring/AM product-to-sum identity. Comparative: Vital, Phase Plant, Massive X,
Pigments, Bazille, Ableton Wavetable/Operator, Hive. Codebase: `SynthVoice.h` (:1905 loop, :3036 sum/tap,
:1957 FM engine, :712/:777 warp helpers, :3234 oversample), `index.html` (:5524 pills, :13568 menu, :13491
hub, :13442 depth node, :12862 warp2 twin, :7212 `__setSynParam`), `PluginEditor.cpp` (:515 setSynParam),
`ParameterIDs.hpp` (:522 BLEND_ = taken). Full findings: workflow `wf_8c25fd85-07a`.
