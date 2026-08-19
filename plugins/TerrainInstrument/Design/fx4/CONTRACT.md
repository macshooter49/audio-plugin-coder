# FX4 — EQUALIZER · WIDEN · COMPRESS · OTT — the shared contract

**Read this before you write a line.** Three agents build four devices in parallel. This file is
what makes them one family instead of four strangers, and what keeps three agents from destroying
one 30,000-line file.

Author: Claude Code (integration owner). Date: 2026-08-18. Build line: fb420+.
Predecessor that worked: `Design/fx3/CONTRACT.md` — read it, this is its successor.

---

## 0. THE RULINGS (Max confirmed these tonight — build to them, do not relitigate)

| # | Ruling |
|---|--------|
| R1 | **FOUR devices, four chain kinds.** `Equalizer` = 9 · `Widen` = 10 · `Compress` = 11 · `OTT` = 12. |
| R2 | **OTT and Compress are SEPARATE devices, not one device with a mode.** The rack is duplicatable and chainable, so a combined device buys nothing — you would instantiate two cards anyway and half the knobs would be dead in each mode. Boundary, locked: **Compress owns everything single-band** (incl. single-band upward+downward levelling as its `Squeeze` Type); **OTT owns 3-band up+down** with per-band thresholds/ballistics and a crossover viz. Neither roster may grow a type that crosses this line. |
| R3 | **`Widen` is the device name.** `Hyper`, `Dimension` (Serum's own strings), `Wider` (Polyverse), `Doubler` (Waves), `Unison` (in-tree, `SynthVoice::setUnisonA`), `Imager` (iZotope) are all ruled out and stay ruled out. |
| R4 | **Rack-module form**, the Tape/Filter precedent. One chassis family, four faceplates with their own identity. |
| R5 | **Back panel YES on all four**, 8 knobs 4×2 per the fb275 spec, **different per device**. Front = 3 heroes + Mix + pills. |
| R6 | **2 back dropdowns = Character + a SECOND AXIS. Never `Type`.** `Type` is the **header pill** (`DEVS[].tp`). fb418 removed a back-panel Type duplicate from all three fx3 devices for exactly this reason — it duplicated the most visible label the card has and broke the no-doubles rule. Do not reintroduce it. Both dropdowns must change **PHYSICS**, never just tone (fb345). |
| R7 | **6 instances each**, routable · chainable · duplicatable. |
| R8 | **Send is positional and automatic.** ONE module; the Send pill renders only at chain index 0 and travels on drag. You build nothing for this — it is inherited. Do not add a Send param to your roster. |
| R9 | Engine interface is **locked** below (§2). Implement it exactly or integration breaks. |
| R10 | **NO AGENT EDITS A SHARED FILE.** See §1. This is the hard one. Violating it costs the whole run. |
| R11 | **NO CEILING.** Max, verbatim tonight: *"do not make it sound pretty when it gets to 100%. If it sounds usable at 100%, then that's not what we want."* 100 % is where the control stops being **useful**, not where it stops being clean. Serum 2 / Phase Plant / Vital sound the way they do because their maxima are destructive. Sanctioned floors: **EQ ±30 dB per band × Amount 200 % = ±60 dB**; **Compress ratio → ∞:1, attack → microseconds**; **OTT depth past Ableton's fixed preset, upward gain that lifts the noise floor into a wall**; **Widen width past mono-destruction**. If your 100 % is polite, you have failed the brief. |
| R12 | The EQ **recycles the shipped Pro-Q-grammar UI** (`redrawEqCanvas` / `redrawEqHandles`), namespaced `eqz*`. Do not design a new drag system. Engine is new; UI is inherited. |

---

## 1. What you own, and what you must never touch

You write **only new files, only inside your own directory**:

```
Design/fx4/<yours>/            eq/ | widen/ | dynamics/
  ROSTER.md                    your locked Type x Character grid + the 8 back params, with reasons
  Terrain<Yours>Fx.h           the engine(s) — self-contained, header-only, tw:: namespace
  <yours>_cert.cpp             the offline perceptual harness (compiles + runs + prints PASS/FAIL)
  <yours>-worklet.js           an AudioWorklet port of the SAME algorithm, for the audible mockup
  FINDINGS.md                  what you measured, what you cut, what you could NOT prove
```

`dynamics/` owns **two** engines (`TerrainCompressFx.h`, `TerrainOttFx.h`) over **one shared
`DynamicsCore.h`** — the envelope follower, both gain computers, the −26 dBFS threshold
calibration and the ballistics are the same math. Two agents building that separately is how the
two halves quietly diverge; that is why it is one agent.

🛑 **FORBIDDEN — shared spine, the integration owner edits these serially:**
`Source/ParameterIDs.hpp` · `Source/PluginProcessor.{h,cpp}` · `Source/PluginEditor.{h,cpp}` ·
`Source/SynthVoice.h` · `Source/FxChainTopology.h` · `Source/ui/public/index.html` ·
anything already in `Source/` · anything in `Tests/` · git commits · the build.

Why, concretely: at fb391 one of four pool constants that must move together was missed and
`auval` returned **139 (SIGSEGV)**. At fb391 a `var` declared after its use in `index.html` hoisted
`undefined` and **killed the entire rack module** with a clean parse and a green build. Three agents
editing those concurrently is not a merge conflict — it is a crash nobody can bisect.

You may **read** anything in the repo. Read widely — that is how you match the house.

---

## 2. The locked engine interface

```cpp
#pragma once
namespace tw {

class TerrainEqualizerFx        // TerrainWidenFx / TerrainCompressFx / TerrainOttFx
{
public:
    static constexpr int kNumTypes = /* yours */;
    static constexpr int kNumChars = 8;                 // 8 per Type, always
    static const char* const* typeNames() noexcept;     // Title-case, no trademark strings
    static const char* const* charNames (int type) noexcept;
    static_assert (kNumTypes > 0 && kNumChars == 8, "roster/table must move together");

    struct Params {
        int   type = 0, character = 0;
        int   axis = 0;                                  // BACK DROPDOWN 2 (Focus/Stereo/Route/...)
        float f1 = 0.5f, f2 = 0.5f, f3 = 0.5f;           // FRONT 3 — name them per device
        float mix = 1.0f;                                // 1.0 = FULLY WET, ZERO DRY (law)
        float b1=0.5f,b2=0.5f,b3=0.5f,b4=0.5f,b5=0.5f,b6=0.5f,b7=0.5f,b8=0.5f;  // BACK 8, 0..1
        bool  tempoSync = false; double bpm = 120.0;
    };

    void prepare (double sampleRate, int maxBlock) noexcept;  // may allocate — message thread ONLY
    void reset() noexcept;                                    // clears state, NO allocation
    void setParams (const Params& p) noexcept;                // PER BLOCK — cheap, no allocation
    void processStereo (float* L, float* R, int n) noexcept;  // IN-PLACE, wet+dry per Mix
    const Viz& viz() const noexcept;
};
} // namespace tw
```

**Per-device `Viz`** — this is the 60 Hz push that feeds your faceplate. Declare exactly these:

| Device | Viz fields |
|---|---|
| `Equalizer` | `float curve[96]` (magnitude dB, 96 log bins 20 Hz–20 kHz) · `float nodeHz[4], nodeDb[4]` · `float lvl` |
| `Widen` | `float corr` (−1..+1 stereo correlation) · `float voicePan[8], voiceCents[8]` · `float widthNow` · `float lvl` |
| `Compress` | `float grDb` (gain reduction, +ve = reduction) · `float inDb, outDb` · `float knee[32]` (transfer curve) · `float lvl` |
| `OTT` | `float grDb[3]` (per band, signed: −ve = upward LIFT) · `float xoverHz[2]` · `float bandDb[3]` · `float lvl` |

Rules on that interface:
- `processStereo` is **in-place** and does its own dry/wet from `Params::mix`.
- **No allocation, no locks, no `new`, no `std::function`** anywhere reachable from `processStereo`.
  🚨 **fb415 caught a `malloc` on the audio thread** because a prepare-in-processBlock shape was
  copied from the Filter — safe there (it allocates nothing), fatal for anything with a delay line.
  If your engine has a delay line, a crossover, or a lookahead buffer, it allocates in `prepare`
  **only**.
- 🔑 **`setParams` is called PER BLOCK, not per sample.** Hoisting it took 18 fx3 instances from
  17.8 % to 14.1 % of a core. Do every derived-value computation there, never in the sample loop.
- Every recirculating state must be denormal-flushed. Assume `ScopedNoDenormals` is NOT set.
- `prepare` must be safe to call repeatedly; `reset` must never allocate.
- Must run correctly at 44.1 / 48 / 88.2 / 96 kHz. **Prove it at 44.1 and 96.**
- **ZERO LATENCY. No lookahead, anywhere, ever.** The fb305 main-send exclusion math subtracts the
  routed dry from the mix **sample-aligned**; a device that delays its wet path misaligns the
  subtraction and the dry leaks back phase-smeared. This kills lookahead limiting — `Compress`
  controls overshoot the fb264 way (0.8 ms one-pole attack + soft-clip catch) and eats ~1 dB of
  overshoot as the documented price.

---

## 3. The house laws you are graded against — your harness must MEASURE these, not assert them

1. **NIGHT AND DAY.** Every param 0→100 must be obviously audible, **monotonic**, and dramatic at
   100. No plateaus, no dead knobs. *The movement is the magic.* See R11 — polite maxima fail.
2. **EVERY TYPE MUST SOUND DIFFERENT.** A different **mechanism**, not an EQ flavour. Produce a
   **cross-type distinctness matrix**: every pair, measured, phase-independent metric.
3. **MIX 100 % = FULLY WET, ZERO DRY.** Dry residual < −60 dB at Mix 1.0. Measure it.
4. **NO CLICKS, EVER.** Smooth every continuous param (10–30 ms), **glide delay lengths**, fade
   on/off. Sweep each param under a sustained tone and measure peak discontinuity.
5. **MONO-SAFE.** Fold to mono and the effect must not vanish. `L = dry+wet, R = dry−wet` sums to
   `2·dry` — the wet cancels **completely**. This is a live risk for `Widen`: any mono-hostile Type
   or Character must be tagged and gated, and the Dimension antiphase cross-mix must be measured
   folded, not assumed.
6. **PHASE-INDEPENDENT METRICS ONLY.** ⚠️ **Sample-difference RMS is BANNED.** fb282 measured
   "102 % divergence" on an allpass change Max could not hear; the real magnitude change was
   0.02 dB. Use magnitude-spectrum delta, spectral centroid, HF ratio, spectral flux, stereo
   correlation, modulation-spectrum lines, gain-reduction trajectory.
7. **CPU-FRIENDLY.** Serum is the bar; 6 instances × 13 kinds may run at once. Report µs/block at
   48 kHz/128, worst Type named.
8. **NO DISK WRITES** from plugin code. (Your harness is a test binary — it may write to its own dir.)
9. **Pragmatic names.** Title-case, says what it DOES, no jargon, no trademark strings ("Ninety",
   not "MXR Phase 90"). Real acronyms may be caps.
10. **Recycle, never reinvent.** Read the house first: `TapeMachines.h` (compander, SmoothRandom
    drift), `TerrainFilters.h` (SVF/allpass cores), `SynthLFO.h` (LFO shapes + sync divisions),
    `DelayEngine.h` (the glide law), `SpectrumAnalyzer.h` (4096 FFT, lock-free triple buffer).

### 3.1 The five probe-craft laws — every one of these cost a real diagnosis

- 🚨 **VERIFY THE PATH, NOT JUST THE ENGINE (fb373).** A green harness proves the ENGINE works,
  never that the plugin REACHES it. Selecting `Cassette` silently gave you `Studio` for four rounds
  of green measurement because a choice param was normalised on the **dropdown's option count**
  instead of the **param's cardinality**. Your roster must state `kNumTypes` and the harness must
  assert the mapping.
- 🪤 **A HARNESS KINDER THAN REALITY IS WORSE THAN NO HARNESS (fb393).** fb392's stub *stored*
  writes, so 39/39 went green while the plugin sat frozen. **Run every new gate against the OLD
  code and require it to FAIL.** A gate that has never failed has never been tested.
- 📐🚫👂 **GEOMETRY IS NOT HEARING (fb417).** The flanger's Bounce measured 57.8 % on a
  *trajectory* metric and was inaudible — it moved where the sweep sat without changing how it
  moved. Prove controls on the **OUTPUT SPECTRUM**, and print the number **beside a control Max
  already agrees is obvious** so the scale is legible.
- 🔬 **CHECK YOUR OWN DETECTOR BEFORE BELIEVING IT.** Two failures in two days: a one-pole HP at
  6 kHz leaks a 220 Hz sine at −29 dB (read "hash" that was the probe); and an HF metric counting
  above 2 kHz was blind to tanh harmonics of a 220 Hz tone at 660/1100/1540 Hz. **Always run the
  probe through a bypassed engine first and print that control number.**
- 🌾 **AN OUTLIER DETECTOR IS BLIND TO A CONTINUOUS FAULT (fb416).** The granular was silent 75 %
  of the time at its own defaults and the click gate never saw it — it hunts the tail of the
  distribution, and a continuous artifact lives in the bulk. Measure duty cycle / occupancy
  directly where it can apply.

### 3.2 Do not fit constants to a gate

If two Types cannot be made night-and-day different, **cut one and say so** — six real Types beat
nine that blur. Tuning a constant until a distinctness gate passes just moves the failure to a
different pair; that happened on the granular last night and the change was reverted. A red gate
you understand is worth more than a green one you bought.

---

## 4. Correspondence — how the four stay one family and stay distinct

Read the other agents' bibles §2 (Types) so you do not collide. Boundaries:

- **Equalizer** owns *static, level-independent* spectral shaping. If an EQ Type responds to input
  level it has become a compressor — that is `Dynamic`, one Type, and it is the only one allowed to.
- **Widen** owns *N copies of the input differing in pitch, time and stereo placement.* It is NOT
  the Chorus (shipped, kind 6): Chorus = one audible cyclic voice pair; Widen = a **crowd**. If a
  Widen Type has one obviously-modulating voice, it is a chorus and must be cut.
- **Compress** owns *single-band level-dependent gain.* If it grows bands, it is OTT.
- **OTT** owns *3-band up+down.* If it loses its bands, it is Compress's `Squeeze`.

**Shared vocabulary** — identical names, ranges and curves where the concept is genuinely the same:
`Mix`, `Amount`, `Attack`, `Release`, `Ratio`, `Width`, `Focus`, `Lo Cut`, `Rate` (same sync
divisions and same free/sync grammar as fx3). Where a concept is NOT the same, **give it a
different name** — never reuse a name for a different meaning.

**🚫 NO DOUBLES, and it is enforced across the WHOLE rack, not just your device.** Before you
finalise a name, grep the tree for it. Two fx4 bibles independently reached for `Air` as both a
Type and a hero knob inside one device — that is the absolute violation. Already resolved: the EQ's
Type 5 is **`Open`** (knob keeps `Air`, it is Max's mandate word); OTT's Type is **`Sheen`**.
`Squash` is unavailable — it is a shipped knob label. Check yours the same way.

---

## 5. What you deliver, and the bar it must clear

`ROSTER.md` — the locked grid. Per Type: name, one-line lineage, the **mechanism** (what is
physically different), its **measurable discriminator**, and its 8 Characters with what each
re-wires. Then the **8 back params**, each with name, range, curve, what it does, and why it earns
its slot over the alternatives. State the second dropdown's axis and why it is not `Type`.

`Terrain<Yours>Fx.h` — the engine(s), header-only, `tw::`, the §2 interface exactly.

`<yours>_cert.cpp` — the harness. **It must compile, run, and you must paste real output:**
```
clang++ -O2 -std=c++17 \
  -I <TI>/Tests/shim -I <TI>/Source -I <your dir> \
  <yours>_cert.cpp -o /tmp/<yours>_cert && /tmp/<yours>_cert
```
It must print PASS/FAIL lines **with the actual numbers** for:
- every Type × its discriminator (law 2) and the **cross-type distinctness matrix**
- every front + back param swept 0→100: monotonic, night-and-day, measured span (law 1)
- **the R11 ceiling gate**: at 100 % the device must be measurably EXTREME — state the metric and
  the threshold you chose and defend it
- every Character measurably changing physics (R6)
- Mix 1.0 dry residual < −60 dB (law 3)
- click test under a sustained tone, per param (law 4)
- mono-sum test per Type and per Character (law 5)
- stability: 60 s of full-drive white noise, no NaN/blow-up, every Type
- the **control number**: the same metric through a bypassed engine (§3.1)
- CPU: µs/block at 48 kHz/128, per Type
- correct operation at **44.1 and 96 kHz**

`<yours>-worklet.js` — the same algorithm as an `AudioWorkletProcessor` so Max can **hear** it in a
Safari mockup before integration. Same Type/Character/param names. Not sample-identical to the C++;
recognisably the same effect.

`FINDINGS.md` — what you measured, what surprised you, what you cut and why, what you could NOT
prove and what it would take. **Be honest about failures — a green claim you did not measure is
worth less than a red one you did.** Report contradictions in your bible; they have been audited
twice but they are not scripture, and two of tonight's four blockers were stale bible claims.

---

## 6. Where things are

- Your bible: `Design/EQUALIZER-BUILD-BIBLE.md` (835) · `HYPER-BUILD-BIBLE.md` (1084) ·
  `COMPRESSOR-BUILD-BIBLE.md` (785) + `OTT-BUILD-BIBLE.md` (923). Read **all** of it including the
  "Open questions for Max" section — several are answered by §0 above.
- The predecessor contract and its results: `Design/fx3/CONTRACT.md`, and the three
  `Design/fx3/*/ROSTER.md` + `FINDINGS.md` — this is what "good" looks like here.
- House laws in full: `CLAUDE.md` §5 at the worktree root.
- Precedents to read before designing: `Source/TapeFxEngine.h`, `Source/FilterFxEngine.h`,
  `Source/TerrainChorusFx.h` / `TerrainFlangerFx.h` / `TerrainPhaserFx.h` (last night's work),
  `Source/ParametricEQ.h` (⚠️ EQ agent: read the §0.1 audit first — it heap-allocates 9–15× per
  sample per channel; carry its NaN-trap comment forward, do NOT carry its coefficient strategy).
- Test shim: `Tests/shim`. Read `Tests/flt_cert.cpp` and the fx3 certs for house harness style.

## 7. How to work

Think first, measure second, claim third. Build the roster, build the engine, then **prove it** —
and when a measurement contradicts the bible or your intuition, **believe the measurement and write
it down**. Do not report done until your harness runs clean and you have pasted its output.
