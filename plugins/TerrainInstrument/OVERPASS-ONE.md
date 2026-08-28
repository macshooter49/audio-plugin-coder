# OVERPASS ONE — the master list
*Max, 2026-08-28. This is the canonical list. It outlives any chat compaction. Do not lose it.*

Order is Max's, not mine. **The broken things go LAST** — his words: *"those are gonna be the
final creases."* Big wins first.

---

## 0 · THE "ALL" FOLDER — do this first, it is an easy win
Every cascade menu needs an **`All`** entry at the **top of the folder list**, containing every
item from every subfolder — **including user content**. Wavetables, filters, warp modes, presets,
the lot. Max: *"every single folder, every single subfolder, at the top is the top of the folder
name 'All'."*

## 1 · THE WAVETABLES — the biggest remaining win
**25 of 30 factory generators stop at a hard-coded harmonic count** (`Wavetable.h`: `h <= 96`,
`<= 64`, `<= 48`, `< 32`). The largest `numHarmonics` in the whole bank is **28**; "Prophet Saw"
has **24**; the reference's init table has **183**. Proof it is content and not band-limiting:
Prophet Saw reads N60 = 24 at C1 *through* C5 identically, with **h24 at −29.31 dBc and h25 at
−134.22 dBc — a 104.9 dB cliff across one harmonic with Nyquist 22 kHz away.**

Everything built in the overpass is being fed thin material. Fixing this makes every shaper,
every FM setting and every raised ceiling sound better retroactively.

Plan is written and the prototype is measured: `scratchpad/WAVETABLE-PLAN.md`. TERRA-01 is
**15 numbers and a seed** — no stored data — and hits **99.5%** of available bandwidth against
Prophet Saw's 38%, through the SHIPPED bake and playback path with zero code changes.
Build order: superset mip ladder + per-level frame length (bit-identical below C6) → `kMaxHarmonics`
512→1024 → the TERRA kernel + a grep gate that fails the build on a new hard-coded ceiling → new
tables at index 30+ **across all ten sites in ONE commit** → Catmull-Rom re-measured → **do NOT
re-tune the legacy 30**, ship TERRA additively so Max can A/B.
🚨 **THE TEN-SITE INDEX TRAP**: `index.html:16172` normalises by DOM option count, not parameter
cardinality. Grow the C++ list while any one `<select>` still lists 30 and index 29 writes 1.0 →
selects table 45. Sites: `WavetableBank.h:44-82` + `:145-178`, `PluginProcessor.cpp` 2532/2728/
2888/3048, `index.html` 6239/6513/6806/7080, plus copies at `:33298`, `:33355`, clamp at `:34845`.

## 2 · UNISON GETS ITS OWN BUTTON SET — on EVERY engine
Max: *"Unison is connected to all the engines."* Every engine gets an arrow, and unison lives
behind it as its own knob page:
- engine has no second page → **unison is page 2**
- engine already has a second page (FM, Harmonics, Modal, Geode, Sample) → **unison is page 3**
- Sample's blend mode adds a page when active → unison becomes **page 3 when blend is on, page 2
  when it is off**
Pattern to copy exactly, do not invent: the **`gran-knob-wrap` / `gk-arrow`** two-page idiom
(`index.html:4812-4826`, replicated 5×, all toggling `.pg2`), **6 knobs per page**.
Params already shipped and live: `URANGE` (5..4800 cents), `USTACK` (9 intervals), `UWARP`,
`UDETUNE`, `UBLEND`, `UWIDTH` (bipolar).

## 3 · PHASING TAKES UNISON'S OLD PLACE ON THE BACK PANEL
Unison vacates the back panel; **phasing moves in** — random phase + current phase position,
modelled on the reference's measured set: `A Phase` (0–360°, 361 steps) and `A Rand Phase`
(0–100 amount, default 100 = fully random). Terrain's `SYN_OSC_x_PHASE` / `_PHASE_AMT` /
`_PHASE_MODE` already exist and are LIVE as of fb526 — this is the UI half.

## 4 · FILTER AS A WARP MODE, AND AS A MODULATION SOURCE
The reference has LPF and HPF as per-oscillator warp modes, and can cross-modulate **from a
filter's output** (`FM (Filter 1)`, `RM (Filter 2)`). We have neither. 78 of our 94 filter types
are ≤232 bytes and allocation-free, so the reuse is cheap — but **exclude the 16 delay-line types**
(CombCore is 96 B + 32 KB heap/channel ⇒ 8.2 MB).

## 5 · REAL HARD SYNC (PolyBLEP)
Unblocks Sync, Formant **and** Fractalize — all three raises were REVERTED after cert measured
them *losing* harmonics (nharm 183→25). Reading a band-limited mip faster transposes partials out
through Nyquist; it cannot create them. Our peak count stays 8–10 against the reference's **423**.

## 6 · MORE WAYS TO SHAPE A WAVETABLE — Max's own ask, and our edge
*"I wonder what else type of warp modes we can have… something creative, something that Serum 2
doesn't have. Something we can get our edge on."* He loves P-Quantize and the folds. Research
brief: find shaping mechanisms **no major synth ships**. Plus **custom/drawable warp shapes** —
he expects that to be the best one.

## 7 · FME / FML — missing capabilities, not depth
Exponential FM and carrier-splitting FM. Measured: FME at d=0.4 puts everything at (k±0.111)·f0
with a real 14.65 Hz component; FML at d=0.2 splits the carrier into 133.30 + 128.17 Hz.

## 8 · BLEND SLOTS 7 `Dist` / 8 `Filter`
Already allocated in the parameter and **still inert** (verified). Lower priority now that warp
carries 26 shapers — but this is the *modulatable* version.

---

## THE BROKEN THINGS — LAST, by Max's instruction ("the final creases")
| | state, verified 2026-08-28 |
|---|---|
| **Sine Shaper** | still `1.0f + amount * 4.0f`. Centroid climbs to 11,440 at 0.8 then **falls to 8,855** at 1.0 — the last fifth goes backwards. Also **not transparent at amount 0**. |
| **P-Quantize** | still `2^(5.0 − 4.0a)`. Moves **4.6% across its entire sweep**, and is **+63% brighter than the bare carrier at a = 0** when it should be inert. Cause never found. |
| **AM's top quarter** | eaten by the master limiter (`kLimiterThresh = 0.90`). Law is exact to depth 0.75, then 0.946 → 0.348 by 1.00: only **34.8%** of algorithmic peak survives. A LIFEGUARD-LAW violation. |
| **FM on rich carriers** | 2% out-of-harmonic at knob **0.372**, not the predicted 0.588. `fmRateMul` covers only `Engine::FM`, so blend-FM's excursion never reaches the mip pick. Top should break; the shallow end must stay clean. |
| **Diode 2** | drive knob runs **backwards** (THD 9.5 → 8.2). Fix compiled in but **never certified through the plugin**. |

## HOUSEKEEPING
- **Sweep every `Tests/*` cert for the preset-0 trap.** Terrain's WT Preset renders as preset 0
  headless without a message-thread pump — every cert that selects a table has been measuring the
  default. A pile of past green checkmarks are worth less than they look.
- **The parameter audit.** Every knob: does it move, and what is its EXPOSURE RATIO (what it
  delivers at max ÷ what the engine can still deliver). Today caught four dead controls by
  accident; that is unlikely to be the last.
- Re-tune the 20 factory presets after the unison `d^2.5` taper change (no migration row, by design).
- FX-card CPU baseline has **never** been measured; every oversampling cost estimate rests on it.

## UNTOUCHED FROM THE ORIGINAL MANDATE
Multi-sample engine (soundfonts → pianos, bells) · more filter types (they ship 108) · preset menu
→ terrain patcher · the voice-panel randomisers (Osc Detune Rnd, Osc Pan Rand, Env Rand, Cutoff
Rand) · **more/better wavetables beyond TERRA** (Max will not build presets on another maker's
tables — in-house content is required, not preferred).

---
## LAWS THAT BIND ALL OF IT
🏊 **THE LIFEGUARD LAW** (CLAUDE.md §5) — the knob's 100% must be the ALGORITHM's 100%; unused
headroom is a defect; fold-back at the top is the product; the shallow end stays clean; never a
limiter, never a clamp, never a "safe" ceiling.
⚖️ **CLEAN ROOM** — measure the reference's OUTPUT as a target number, implement with our own DSP.
Never decompile, never extract a table or preset, never transcribe a curve.
🧩 **NAME THE SEAM** — a call site in one owner's file and the callee in another's means NOBODY
writes the call. 28 params shipped inert that way and passed pluginval, auval AND bit-identity.
Every new parameter needs a MOVEMENT gate, not just a floor gate.
🔗 **A CONSTANT THAT FEEDS A SELECTION PATH LIVES IN TWO PLACES** (`warpRateMul`, the JS default
mirrors, `kFoldPre`). Change both or it compiles clean and silently misbehaves.

---

## 🆕 FOUND 2026-08-28 BY THE NOVEL-WARP RESEARCH — verified independently

### 🟠 FIVE SHIPPED WARP MODES EMIT DC WITH NO BLOCKER — needs Max's call, it is a SOUND CHANGE
`warpAmpNeedsDc()` (`SynthVoice.h:1325`) opens `if (mode < 9 || amount <= 0.001f) return false;`,
so the phase-domain modes never arm `wtRectDc*`. **A non-bijective phase remap changes the cycle
mean**, and measured worst-case |DC| as a fraction of full scale:
**Mirror 0.249 · P-Quantize 0.212 (saw) · Sync/Formant 0.172 · Fractalize 0.123.**
I confirmed the gate and grepped the chain: **there is no other DC removal anywhere downstream of
the oscillator sum.** It also survives the unison sum — every sine gets the same warp, so the
offset adds coherently. Consequences: eaten headroom, an asymmetric waveform, and early limiter
engagement (`kLimiterThresh = 0.90`).
⚠️ **Why it is not an overnight fix:** arming the blocker is a 38 Hz high-pass on five modes that
have shipped for months — it changes existing sounds and breaks bit-identity. Options are (a) arm
it and accept the change, (b) arm it only above an amount threshold, (c) leave it and document.
**Max decides.** Also note the research doc's own build-checklist got this exactly backwards — it
claimed phase modes "cannot possibly produce DC" and must be added to the `return false` list.
The measurement says the `default: return true` is correct.

### The novel-shape research: measured, and the headline candidate DIED
`scratchpad/novel-warp-modes.md` (708 lines) + `scratchpad/warpbench/` (the rig: exactly-periodic
f0 so every FFT bin is exact, tables band-limited to the note's mip limit so the baseline aliases
at **0.000%**, and an 8x-oversampled truth reference; shipped modes copied verbatim from
`SynthVoice.h`/`Shapers.h` so every comparison is against what actually ships).
- **C1 SHATTER as specced is not a scramble, it is a x3 PITCH SHIFT.** `j = (i*3) & (N-1)` is
  multiplication mod 2^m — a linear map — so the read head advances three slices per slice. On a
  sine at full amount the fundamental measures **-340.3 dB**: the note is gone. It accidentally
  reimplemented Fractalize. **The bit-reversal variant (C1b) is the real one** and it is strong:
  nh 163 at amount 0.20, zero DC, f0 intact until the very top.
- Survivors worth building, with numbers in the doc: **C1b SHATTER (bit-reversal)**, **C3 ZENO**
  (projective/Mobius quantise with a PULL — fixes P-Quantize's three documented defects at once),
  **C2 CRUMPLE** (fBm domain warp; the only candidate that generates EVEN harmonics, and 0.000%
  alias at C3), **C5 FRACTAL** (Takagi folder, genuinely odd at +160..+195 dB odd/even, zero DC,
  and the first shaper with a dB/oct SLOPE knob).
- Killed by measurement: **C4 GRAY is dead travel** (nh = 1 at amount 0.2 AND 0.5 — nothing happens
  until 1.0), **C7 PERSPECTIVE is not transparent at 0** (nh 65, f0 -13.1 dB with the knob at zero).
- ⚠️ Two build traps for whoever ships these: `warpAmpNeedsDc` defaults `true` for index >= 9, which
  is CORRECT for these (see above) — do not "fix" it; and **`warpRateMul()` takes `(mode, amt)` and
  NOT `var`**, so any mode whose read rate depends on VAR would be band-limited for the wrong rate
  — exactly the fb523 "the raise LOST harmonics" failure. Widen the lambda.
- Independent confirmation of a known defect: **P-Quantize at amount 0.00 measures nh 11 and
  centroid 1262 Hz against dry's nh 1 / 131 Hz.** Its transparency defect is real and reproducible
  outside the plugin.
- 🔑 Correction to a claim I had repeated: "a 1-periodic phase remap folds back onto the harmonic
  grid so OOHR is blind to it" only holds when `sr/f0` is an integer, which it generally is not.
  Shipped Bend at C7 amount 1.0 measures **18.7% inharmonic**. OOHR sees these modes fine.
- Highest-value idea that does NOT fit today's signature: `p -> p + a*table'(p)` — steering the read
  by the table's own SLOPE (an unsharp mask, not a displacement map). The reference does not have
  it; it needs a table pointer in `applyPhaseWarp`.
