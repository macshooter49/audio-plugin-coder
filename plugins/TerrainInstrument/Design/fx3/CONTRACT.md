# FX3 — CHORUS · FLANGER · PHASER — the shared contract

**Read this before you write a line.** Three agents build three devices in parallel. This file is
what makes them one family instead of three strangers, and what keeps three agents from destroying
one 30,000-line file.

Author: Claude Code (integration owner). Date: 2026-08-17. Build line: fb395+.

---

## 0. THE RULINGS (Max said don't wait, so these are decided — build to them)

| # | Ruling |
|---|--------|
| R1 | **Three separate devices.** Chorus = chain kind 6, Flanger = 7, Phaser = 8. NOT combined into one. Max was explicit. |
| R2 | **Rack-module form**, the Tape precedent (`TapeFxEngine.h` + its card). One chassis family, three faceplates with their own identity. |
| R3 | **Back panel YES**: 8 knobs, 4×2, per the fb275 official spec — and the 8 are **different per device**. Front = 3 knobs + Mix + pills. 11 params/device. |
| R4 | **2 dropdowns = Type + Character.** Both must change **PHYSICS** (stage count, tap count, LFO topology, loop wiring) — never just a tone control. That is the fb345 law. |
| R5 | **Preset menu per device**, factory + user-savable. Recycle `TIC.presets` / the `.pmenu` glass — do NOT invent one. |
| R6 | **6 instances each**, routable · chainable · duplicatable. Same header, footer, route pills, power pill as every other rack device. |
| R7 | **Vintage chorus.** The synth page's existing chorus (`CHORUS_AMOUNT` / `CHORUS_WIDTH` / `CHORUS_CHARACTER`, `Source/TerrainChorus.h`, wow+flutter already in it) becomes a **Type inside the new Chorus device**, reusing its voicing. The front-page one stays where it is. |
| R8 | **"Moog chorus" does not exist.** Moog's famous modulation box is the **MF-103 12-stage phaser** — already the phaser's `Twelve` Type. Analog-ensemble variety comes from Roland Dimension D and the string machines instead. Told to Max in the report; build the real ones. |
| R9 | Engine interface is **locked** below. Implement it exactly or integration breaks. |
| R10 | **NO AGENT EDITS A SHARED FILE.** See §4. This is the hard one. Violating it costs the whole run. |

---

## 1. What you own, and what you must never touch

You write **only new files, only inside your own directory**:

```
Design/fx3/<yours>/            chorus/ | flanger/ | phaser/
  ROSTER.md                    your locked Type × Character grid + the 8 back params, with reasons
  Terrain<Yours>Fx.h           the engine — self-contained, header-only, tw:: namespace
  <yours>_cert.cpp             the offline perceptual harness (compiles + runs + prints PASS/FAIL)
  <yours>-worklet.js           an AudioWorklet port of the SAME algorithm, for the audible mockup
  FINDINGS.md                  what you measured, what you cut, what you could not prove
```

🛑 **FORBIDDEN — these are shared spine, the integration owner edits them serially:**
`Source/ParameterIDs.hpp` · `Source/PluginProcessor.{h,cpp}` · `Source/PluginEditor.{h,cpp}` ·
`Source/SynthVoice.h` · `Source/ui/public/index.html` · anything already in `Source/` ·
anything in `Tests/` · git commits · the build.

Why, concretely: `kGrnSendBase 15 → kTpeSendBase 21 → kFltSendBase 27 → kPoolSendCount 33`, plus
`SynthVoice::kPoolSends` and `kFxKinds`, are **four constants that must move together**. At fb391 one
of them was missed and `auval` returned **139 (SIGSEGV)** — six slots past the end of four pool
arrays. Three agents editing that concurrently is not a merge conflict, it's a crash nobody can
bisect. Same for `index.html`: at fb391 a `var` declared after its use hoisted `undefined` and
**killed the entire rack module** with a clean parse and a green build.

You may **read** anything in the repo. Read widely — that is how you match the house.

---

## 2. The locked engine interface

Model: `Source/TapeFxEngine.h` and `Source/FilterFxEngine.h`. Match their shape exactly.

```cpp
#pragma once
namespace tw {

class TerrainChorusFx            // TerrainFlangerFx / TerrainPhaserFx
{
public:
    // ── identity, read by the UI and by ParameterIDs; the static_assert is MANDATORY
    static constexpr int kNumTypes = /* yours */;
    static constexpr int kNumChars = 8;                 // 8 per Type, always
    static const char* const* typeNames() noexcept;     // kNumTypes entries, Title-case, no trademarks
    static const char* const* charNames (int type) noexcept;   // kNumChars entries FOR THAT TYPE
    static_assert (kNumTypes > 0 && kNumChars == 8, "roster/table must move together");

    struct Params {
        int   type = 0, character = 0;
        float rate = 0.35f, depth = 0.5f, feedback = 0.0f;   // FRONT 3 — name them per device
        float mix  = 0.5f;                                   // 1.0 = FULLY WET, ZERO DRY (law)
        float b1=0.5f,b2=0.5f,b3=0.5f,b4=0.5f,b5=0.5f,b6=0.5f,b7=0.5f,b8=0.5f;  // BACK 8, 0..1
        bool  tempoSync = false; double bpm = 120.0;
    };

    struct Viz {                 // 60 Hz push to the UI — what the faceplate animates
        float lfo = 0.0f;        // −1..+1 instantaneous sweep, THE needle
        float lvl = 0.0f;        // wet level 0..1
        float notch[8] {};       // notch/comb centres in Hz, 0 = unused  (phaser/flanger)
        float depthNow = 0.0f;   // effective excursion, ms (chorus/flanger) or octaves (phaser)
    };

    void prepare (double sampleRate, int maxBlock) noexcept;   // may allocate — message thread only
    void reset() noexcept;                                     // clears state, no allocation
    void setParams (const Params& p) noexcept;                 // per block, cheap, no allocation
    void processStereo (float* L, float* R, int numSamples) noexcept;   // IN-PLACE, wet+dry per Mix
    const Viz& viz() const noexcept;
};
} // namespace tw
```

Rules on that interface:
- `processStereo` is **in-place** and does its own equal-power dry/wet from `Params::mix`.
- **No allocation, no locks, no `new`, no `std::function`** anywhere reachable from `processStereo`.
- Every recirculating state must be denormal-flushed. Assume `ScopedNoDenormals` is NOT set for you.
- `prepare` must be safe to call repeatedly; `reset` must never allocate.
- The engine must run correctly at 44.1 / 48 / 88.2 / 96 kHz. Prove it at 44.1 and 96.

---

## 3. The house laws you are being graded against

These are Max's permanent rules. Your harness must **measure** compliance, not assert it.

1. **NIGHT AND DAY.** Every parameter, swept 0→100, must be obviously audible, must **scale
   monotonically**, and must be dramatic at 100. No plateaus, no dead knobs, no "useful only at 2–10%".
   *The movement is the magic.*
2. **EVERY TYPE MUST SOUND DIFFERENT.** Not an EQ flavour — a different mechanism. You must produce a
   **cross-type distinctness matrix**: every pair, measured, on a phase-independent metric.
3. **MIX 100 % = FULLY WET, ZERO DRY.** Dry residual < −60 dB at Mix 1.0. Measure it.
4. **NO CLICKS, EVER.** Smooth every continuous param (10–30 ms), **glide delay lengths** (never snap
   a delay time — the comb-click law), fade on/off. Prove by offline-sweeping each param while a tone
   plays and measuring peak sample-to-sample discontinuity.
5. **MONO-SAFE.** Fold to mono and the effect must not vanish. This has bitten before: a
   `L = dry+wet, R = dry−wet` recipe sums to `2·dry` — the wet cancels **completely**. Any Type or
   Character that is mono-hostile must be tagged as such and gated.
6. **PHASE-INDEPENDENT METRICS ONLY.** ⚠️ **Sample-difference RMS is BANNED as a dramaticism metric.**
   fb282 measured "102 % divergence" on an allpass change and Max heard **nothing**; the real
   magnitude-spectrum change was 0.02 dB. Use: magnitude-spectrum delta, spectral centroid, HF ratio,
   spectral flux, notch depth/count/trajectory, stereo correlation, modulation-spectrum lines.
   *A phaser is all-pass by construction — so for phaser Types the discriminator lives in the notch
   geometry and the mono-sum comb, never in the wet magnitude spectrum alone.*
7. **CPU-FRIENDLY.** Serum is the bar, and 6 instances × 3 devices may run at once inside a
   dynamic node chain. Report µs/block at 48 kHz/128 and note the worst Type.
8. **NO DISK WRITES** from plugin code. (Your harness is a test binary — it may write to its own dir.)
9. **Pragmatic names.** Title-case, what it DOES, no jargon, no trademark strings ("Ninety", not
   "MXR Phase 90"). Real acronyms may be caps.
10. **Recycle, never reinvent.** If the house already has a thing (LFO shapes, smoothers, allpass,
    compander, `TapeMachines.h` SmoothRandom drift), read it and reuse it.

---

## 4. Correspondence — how the three stay one family and stay distinct

You are three parts of one release. Read the other two bibles' §2 (Types) so you do not collide:

- **Chorus** owns *multi-tap, delay-modulated, LOW feedback.* Its identity is pitch shimmer and
  stereo width. If a chorus Type starts sounding like a jet, it has too much feedback — that's the
  flanger's territory.
- **Flanger** owns *short delay + HIGH feedback + through-zero.* Its identity is the moving harmonic
  comb and the polarity null. `Tape Zero` is the flagship and the one Serum 2 cannot do.
- **Phaser** owns *cascaded all-pass, NO delay line.* Its identity is a finite number of notches
  whose spacing is NOT harmonic. If a phaser Type has a delay line in it, it is a flanger.

**The shared vocabulary** — same names, same ranges, same curves across all three where the concept
is genuinely the same: `Rate` (with the same sync divisions and the same free/sync grammar), `Depth`,
`Feedback`, `Width`, `Mix`, `Manual`/`Center`, `Stereo Spread`, `Lo Cut`. Where a concept is NOT the
same, give it a different name — do not reuse a name for a different meaning.

**Tempo sync** must use identical divisions and identical labels in all three.

---

## 5. What you deliver, and the bar it must clear

`ROSTER.md` — the locked grid. For each Type: name, one-line lineage, the mechanism (what is
physically different), its **measurable discriminator**, and its 8 Characters with what each
re-wires. Then the **8 back-panel params** for the device, each with: name, range, curve, what it
does, and why it earns a slot over the alternatives. Cut anything that fails law 1 or 2 and say so.

`Terrain<Yours>Fx.h` — the engine. Header-only, `tw::` namespace, the §2 interface.

`<yours>_cert.cpp` — the harness. **It must compile and run and you must paste real output.**
```
clang++ -O2 -std=c++17 -I <repo>/plugins/TerrainInstrument/Tests/shim \
        -I <repo>/plugins/TerrainInstrument/Source \
        -I <your dir> <yours>_cert.cpp -o /tmp/<yours>_cert && /tmp/<yours>_cert
```
It must print, as PASS/FAIL lines with the actual numbers:
- every Type × its discriminator (law 2)
- the **cross-type distinctness matrix** — every pair distinguishable, with the metric and the margin
- every front + back param swept 0→100: monotonic, night-and-day, with the measured span (law 1)
- every Character measurably changing physics (law 4/R4)
- Mix 1.0 dry residual < −60 dB (law 3)
- click test: peak discontinuity while sweeping each param under a sustained tone (law 4)
- mono-sum test per Type and per Character (law 5)
- stability: 60 s of full-feedback white noise must not blow up or go NaN at every Type
- CPU: µs/block at 48 kHz/128, per Type

`<yours>-worklet.js` — the same algorithm as an AudioWorkletProcessor so Max can **hear** it in a
Safari mockup before any integration. Same Type/Character/param names. It does not have to be
sample-identical to the C++; it has to be recognisably the same effect.

`FINDINGS.md` — what you measured, what surprised you, what you cut and why, what you could NOT
prove and what it would take. **Be honest about failures — a green claim you did not measure is
worth less than a red one you did.** Report contradictions you find in your bible; it has been
audited twice but it is not scripture.

---

## 6. Where things are

- Your bible: `Design/CHORUS-BUILD-BIBLE.md` / `FLANGER-` / `PHASER-` (1218 / 827 / 682 lines,
  two audit passes). Read **all** of it, including §11/§12 "Open questions" and the verification log.
- House laws in full: `CLAUDE.md` §5 at the repo root of the worktree.
- Precedents to read before designing: `Source/TapeFxEngine.h` (newest rack module),
  `Source/FilterFxEngine.h` (newest device), `Source/TerrainChorus.h` (the legacy chorus — R7),
  `Source/TapeMachines.h` (SmoothRandom drift, compander), `Source/DelayEngine.h` (glide law),
  `Source/SynthLFO.h` (LFO shapes + sync divisions), `Source/TerrainFilters.h` (allpass/SVF cores).
- Test shim: `Tests/shim` — a JUCE stand-in so engines compile headless. Read
  `Tests/flt_cert.cpp` for the harness house style before writing yours.

## 7. How to work

Think first, measure second, claim third. Build the roster, build the engine, then **prove it** —
and when a measurement contradicts the bible or your intuition, believe the measurement and write
it down. If a Type cannot be made night-and-day different from its siblings, **cut it and say so**;
six real Types beat nine that blur. Do not report done until your harness runs clean and you have
pasted its output.
