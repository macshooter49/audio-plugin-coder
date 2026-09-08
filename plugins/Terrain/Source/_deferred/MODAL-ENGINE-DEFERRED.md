# MODAL ENGINE — DEFERRED (2026-07-12)

**Decision (Max):** Defer the whole MODAL (physical‑modeling) engine to a future update.
It needs *weeks* of dedicated DSP‑realism work to make Piano / String / Bell sound truly
acoustic (not synthy), and that can't block the roadmap now. **Removed from the plugin UI
(engine selector) so it can't be selected or seen. C++ left dormant (see "What was removed").**
Everything below is the full state so we can resume cleanly. Nothing is lost — all code is in
git and still in the tree (just unreachable from the UI).

> When we come back: **re‑read this file first, then `physical-modeling/reference/piano-spec.md`
> and `physical-modeling/SKILL.md`.** The single most valuable thing here is the **relay‑scale
> bug** section — it cost a full investigation to find; do not re‑derive it.

---

## ⭐ THE #1 THING TO REMEMBER — the "knobs do nothing" bug was NOT the DSP

For three sessions the symptom was: *turn any MODAL knob (Hard/Pos/Decay/Material/Age…), replay
a note, hear NO change.* We kept re‑tuning the DSP. That was the wrong target.

**Root cause (proven, not guessed):** the MODAL controls are fully wired end‑to‑end in the code
(UI `data-syn` divs → `#syn-panel .knob[data-syn]` binder → `getSliderState().setNormalisedValue()`
→ `WebSliderRelay` → `withOptionsFrom` → `WebSliderParameterAttachment` (`mkAtt`) → APVTS →
`rawParam()` live read → `modalP` gather → `setModalParamsA` → `renderModalOsc` → `setParams`
every block → `computeLoopBounds`). **Every link is correct.** But at this plugin's scale
(**700+ `WebSliderRelay`s**), the MODAL relays — which are registered *last* in the giant
`withOptionsFrom` chain in `PluginEditor.cpp` — **silently fail to carry their value into the
APVTS parameter.** So:

- The knob **ring visually moves** (the JS pointer handler runs and fills the arc directly), but
- the **APVTS parameter never changes**, so the audio thread reads the **factory default forever**.
- This hit **every** modal control the same way — knobs AND the Family/Form `<select>`s
  (Grand→Bells did nothing either). That's the tell: it's the shared relay→APVTS hop, not any
  one control.

This is exactly the failure mode CLAUDE.md warns about: *"WebSliderRelay = 4‑point binding, or it
silently no‑ops … JS writes vanish, audio thread reads default forever, no error."* Here all 4
points are present in source, yet it still no‑ops at runtime — a **scale/ordering** problem with
hundreds of relays, not a missing wire.

**How it was confirmed (Max, in the DAW):**
- Drag Hard → *"ring fills/moves, sound doesn't"* → value leaves UI fine, dies before the engine.
- Change Family Grand→Bells → *"family/form dead too"* → shared relay→APVTS hop is the break.
- Engine **selector** works (same `getSliderState` mechanism) → the mechanism works for *early*
  relays; only the *late* (modal) ones fail. Downstream (APVTS→engine) provably works because the
  engine selector uses the identical `rawParam`→push path.

### THE FIX FOR THAT BUG IS ALREADY BUILT (bypass) — but unconfirmed in DAW
A **`setSynParam` native function** was added that writes **straight to the APVTS**, bypassing the
broken relay (same proven mechanism as `setDelayFreeze`/`loadPreset`). Modal knobs, Family, and
Form now also call `window.__setSynParam(id, norm)`. This was **built + installed** but Max
deferred **before confirming** it in the DAW. **Very likely this makes the knobs work** — verify
first thing when we resume. If confirmed, the "knobs are dead" problem is *solved* and the only
remaining work is DSP realism + the family reduction.

- C++: `PluginEditor.cpp` — `.withNativeFunction("setSynParam", …)` (writes
  `getAPVTS().getParameter(id)->setValueNotifyingHost(norm)`).
- JS: `index.html` — `window.__setSynParam` helper (after the `window.Juce` setup), called from
  the knob binder (`if (paramId.indexOf('_MODAL_') !== -1)`), the `.md-fam-select` and
  `.md-form-select` change handlers.
- These are harmless and remain in the tree while modal is deferred (dormant, since the engine
  can't be selected). Keep them — they're the resume shortcut.

**Consider for resume:** either (a) trust the `setSynParam` bypass for ALL modal controls, or
(b) investigate the actual relay‑scale limit (does JUCE/WKWebView cap the number of relays or the
init‑message size? HARM/GEODE — the other late engines — may have the same latent bug and should
be spot‑checked in the DAW).

---

## DSP STATE — what's built and good (commit `3db2d48`)

The GRAND piano voicing (increments 1–3 + clip fix) is **done and solid** in `ModalEngine.h`.
40/40 DSP tests pass, pluginval strictness 5 clean. The DSP is *not* the problem (see audibility
data below — every knob changes the rendered note 14–72%). Shipped pieces:

1. **Felt hammer** exciter (contact‑time force pulse; `Hard` = contact time → brightness).
2. **Baked inharmonicity** (allpass dispersion; piano string stiffness), **pitch‑anchored** so the
   fundamental stays locked while upper partials stretch sharp (a‑C‑is‑a‑C).
3. **Coupled‑unison two‑stage decay** (Weinreich): an always‑on detuned twin string → prompt +
   slow blooming aftersound ("alive" sustain).
4. **Register‑scaled DECAY** (bass rings longer than treble), **steel MATERIAL** (no nylon floor;
   presence tilt warm↔brilliant), **note‑off damper** (key‑up actually stops the note).
5. **De‑dull** (shorter contact → richer uppers) and **POS clip‑fix** (energy‑normalized strike
   comb → POS is a pure tone knob, no level jump / no clipping at any knob extreme; worst‑case
   peak 0.67 over a 4 s ring at max decay+velocity+bright material+POS).

### Per‑knob AUDIBILITY (rendered GRAND note, C3, knob 0→1) — proves the DSP responds
Measured spectral change of the actual rendered note (see the diagnostic harness in the session):
```
HARD 66%  POS 54%  DECAY 72%  MATERIAL 20%  BREATH 44%
STRETCH 20%  BLOOM 14%  HALO 63%  AGE 27%  BODY 24%
```
Guide: <5% dead, 5–15% subtle, >25% obvious. Every knob is audible **on a fresh note** — the DSP
was never the problem; the values just never reached it (relay bug above).

---

## WHAT REMAINS (the "weeks of work" — for the next update)

1. **Confirm the `setSynParam` bypass in the DAW** (knobs/family/form should move now).
2. **Reduce 9 families → 3: Piano / String / Bell** (Max's scope cut). Delete Pluck/Bow/Flute/
   Reed/Brass/Bars/Skin from `modal::` enum, the form tables, `MODAL_FAM`/`MODAL_FORMS` in JS, and
   the family `<select>` options. Keep GRAND (piano), one string family (BOW or PLUCK), BELLS.
3. **DSP realism per family** — GRAND is close; STRING and BELL need the same increment treatment
   (real material, real decay, real excitation, body). This is the multi‑week part.
4. **Sample‑exciter + dry↔model MIX** (was increment 7): strike/exciter from a dropped sample +
   a MIX blend of the raw sample vs the resonated model.
5. **Re‑retune the 10 knobs per family** once realism lands, using the audibility harness.

---

## WHAT WAS REMOVED / CHANGED for the deferral

- **UI:** the four `<option value="6">Modal</option>` entries were removed from the osc engine
  selectors (`#osc-{a,b,c,d}-engine-select`) in `Source/ui/public/index.html`. Modal is now
  **unselectable**, so its panels never show. The engine param stays a 7‑choice param (index 6 =
  Modal, just unreachable) — the selector maps by option `value` (`i/6`), so removing the option
  does **not** shift the other engines. `ENGINE_NAMES[6]='Modal'` intentionally left in place.
- **C++ left dormant (NOT removed from the build):** ripping the engine out of 6 tightly‑coupled
  files (`ModalEngine.h`, `SynthVoice.h`, `PluginProcessor.cpp/.h`, `PluginEditor.cpp/.h`,
  `ParameterIDs.hpp`) risks destabilizing the shipping plugin for zero user benefit — Modal is the
  **last** engine index, so as dead code it costs only a little binary size and nothing at runtime.
  If a future decision needs *zero* modal code compiled, do the full excision as its own task
  (safe because index 6 is last; nothing shifts).

---

## FILES (for restore) — all present in the tree + git `3db2d48`

- `Source/ModalEngine.h` — the engine (3 cores: dual‑pol SDL string / MSW loop / 2D modal bank).
- `Source/ModalEngine_test.cpp` — 40 tests (compiled manually, not a CMake target).
- `Source/SynthVoice.h` — `modalEngA_..D_`, `renderModalOsc`, `setModalParamsA..D`, `Engine::MODAL=6`.
- `Source/PluginProcessor.cpp` — `addModalOsc` (param layout), `MODAL_IDS` gather, modal push.
- `Source/PluginEditor.cpp/.h` — modal relays, `mkAtt` attachments, `withOptionsFrom`, `setSynParam`.
- `Source/ParameterIDs.hpp` — `SYN_OSC_{A..D}_MODAL_*` IDs (FROZEN — keep on restore).
- `Source/ui/public/index.html` — modal knobs/selectors/JS wiring (dormant), `__setSynParam`.
- `physical-modeling/SKILL.md` + `physical-modeling/reference/piano-spec.md` — the research + spec.

## TO RESTORE (next update)
1. Re‑add `<option value="6">Modal</option>` to the four engine selectors in `index.html`.
2. Rebuild **with the BinaryData cache‑bust** (index.html changed) — see CLAUDE.md §2B.
3. Verify the `setSynParam` bypass makes the knobs/family/form live (first test).
4. Then proceed with the "WHAT REMAINS" list.

## Memory cross‑refs
`[[terrain-instrument-modal-piano-revoicing-arc]]` · `[[terrain-instrument-modal-engine-shipped]]` ·
`[[reference-physical-modeling-skill]]` · `[[feedback-terrain-instrument-verify-webview-param-bind-chain]]`
