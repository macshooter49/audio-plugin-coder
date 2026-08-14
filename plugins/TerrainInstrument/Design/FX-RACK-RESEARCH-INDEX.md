# FX RACK — RESEARCH INDEX
### The wake-up document. Read this first, then open only the bible you are building from.

**Written 2026-08-14 (overnight sweep).** 16 research files, ~11,600 lines, one per remaining FX
device plus the chain architecture and the Serum 2 teardown. Every bible follows the
`DISTORTION-BUILD-BIBLE.md` format that produced a certified device: scope → history → types →
DSP math → 11-param chassis map → visualizers → interplay → presets → CPU → pitfalls →
hard-rule checklist → open questions → recycle inventory (file:line, read-verified) → sources.

**Builder-sufficient by design:** each bible is written so a build session needs no re-research.

---

## 1. THE TABLE

| Bible | The device, in one line | Types | Verify | The most surprising find |
|---|---|---|---|---|
| `GRANULAR-FX-BUILD-BIBLE.md` | Live-bus granulator — **the headline differentiator** | 8 | — | We already ship **two** granulators; `GrainEngine.h` is the *live-input* one with a 5 s circular capture buffer already written every sample. The FX device is mostly wiring. |
| `TAPE-BUILD-BIBLE.md` | The tape **machine** — heads, motor, loop, splice | 7 | — | **Three tapes already live in the tree** (DLY-panel color, TapeLoop looper, Distortion's hysteresis Tape). §0.2 draws a hard boundary law so they never tangle: Distortion owns the *magnetisation*, this device owns the *machine*. |
| `DELAY-MOOG-PORT-PLAN.md` | Port `MoogDelay.h` into the Delay device | 6 | — | The Moog engine has **five capabilities the shipped Delay lacks** (true BBD companding, clock-tracked bandwidth both sides, pitch-shifted feedback, 7-wave time LFO, freeze) — and all five port in as 2 new Types + 1 rebuild with **zero new knobs**. |
| `FILTER-BUILD-BIBLE.md` | The synth filter, hosted as an FX device | 9 | — | `TerrainFilters.h` is **2,195 lines / 94 filter types** behind one stable `FilterSlot` façade. This is not a DSP project — it is a hosting-and-motion project. The device's real new code is the env-follower / key-track motion block. |
| `FX-CHAIN-BIBLE.md` | Multi-instance chain + the anti-mud doctrine | 6 | — | 🔑 **JUCE cannot create parameters at runtime.** The `+` button can therefore never *create* a device — it can only claim a **pre-allocated slot**. Design: K=5 slots × 26 params = 130 new params. This constraint shapes the entire epic. |
| `COMPRESSOR-BUILD-BIBLE.md` | Single-band, 8 topologies, zero latency | 8 | PATCHED | **Zero lookahead is non-negotiable** — not a taste call. The fb305 main-send exclusion math subtracts dry *sample-aligned*; any latency-reporting device makes the dry leak back phase-smeared. |
| `OTT-BUILD-BIBLE.md` | 3-band up+down compression — the AIR machine | 8 | — | Your instinct was right and the bible confirms the boundary: OTT is a *fixed opinionated dynamics instrument*, the Splitter is *routing*. Serum 2 ships **both**. The upward computer on the high band is literally where the air comes from. |
| `EQUALIZER-BUILD-BIBLE.md` | The device EQ + audit of our old one | 7 | PATCHED | 🚨 **Our shipped v6 EQ heap-allocates 9–15 times per sample per channel** (`ParametricEQ.h:135-158` runs the full RBJ design math at audio rate). It survives as one instance; it is disqualified as a rack device engine. |
| `SPLITTER-BUILD-BIBLE.md` | ONE device, Mode dropdown (your call, confirmed) | 5–6 | PATCHED | Serum ships three splitters purely because their rack is a flat module list — **not a DSP argument**. Firm recommendation: one device, plus a dedicated `Sub Split` mode at 120 Hz (the club-mono line). |
| `FLANGER-BUILD-BIBLE.md` | 0.05–40 ms comb territory, incl. through-zero | 6 | PATCHED | **Serum 2 has no through-zero flanging** — headline differentiator — and the flanger is *the cheapest flagship we will ever build*: two fractional delay reads and a few one-poles. |
| `PHASER-BUILD-BIBLE.md` | 9 allpass topologies, 8 characters each | 9 | PATCHED | Serum's phaser is ~6 params, one topology, one LFO shape. Our roster lands in **Soundtoys PhaseMistress territory** — and allpass chains are nearly CPU-free, so the budget goes to voicing. |
| `CHORUS-BUILD-BIBLE.md` | 7 tap-count × LFO-topology answers | 7 | — | The boundary math is exact: **below ~1.5 ms the comb fundamental enters the audible band** and it reads as filtering, not doubling. That number is what separates Chorus from Flanger. |
| `HYPER-BUILD-BIBLE.md` | Our unison-widener — working name **`Widen`** | 7 | — | Serum ships Hyper *and* Dimension as ONE menu item — a confession they are one job (thicken then widen). The pair-string "Hyper/Dimension" is the only thing that is actually theirs; the mechanisms are public-domain with 1979 precedent. |
| `BODE-BUILD-BIBLE.md` | Frequency shifter (Hilbert SSB) | 7 | — | We already have a `BodeShifter` in `TerrainFilters.h`. Range chosen at **±5 kHz** (the 1630's), not Echobode's ±20 k — past ~5 k it is all foldover chatter on synth program. |
| `UTILITY-BUILD-BIBLE.md` | Gain / width / bass-mono / meters — the joint | Route+Flip | — | **No fake Types** — law 5 cuts both ways. A utility that "colors" is a broken utility. Its dropdowns are Route and Flip, both discrete and sound-changing. It is also the rack's first real metering surface. |
| `SERUM2-FX-REFERENCE.md` | The competitive teardown, all 16 modules | 12 | — | Their FX are **strictly post-sum** (self-admitted paraphonic) while we ship per-layer independent chains. Their rack is wide and shallow; ours is narrow and deep. $249 vs $99. |

---

## 2. THE BUILD ORDER (yours, encoded)

### Build 1 — GRANULAR device
Size the existing front-page granular UI into the rack card, same layout + header/footer.
* Bible §2 is a **knob-by-knob inventory of the current front-page UI with line anchors** — that section exists specifically to make tomorrow morning mechanical.
* Recycle: `GrainEngine.h` (live-input granulator, 5 s circular buffer already written per-sample) + `GranularEngine.h` (the optimized grain pool, windows, skew LUT).
* ⚠️ **Decide before wiring:** the Freeze law tension (below) and buffer length (16.5 s = 6.3 MB/instance vs 8 s).

### Build 2 — TAPE device
* ⚠️ **Read §0.2 (the boundary law) before writing a line.** Three tape systems already ship. This device is the *machine*; Distortion keeps the *magnetisation*; TapeLoop keeps *recording*; the DLY-panel tape stays as channel-strip color.
* ⚠️ Open: does Tape join as the 4th **send** device (fb305-family wiring) or as the first **insert-only** device of the chain epic? Affects wiring order only, not DSP.

### Build 3 — MOOG DLY PORT into the Delay device
* Port 5 capabilities as 2 new Types + 1 rebuilt Type, **zero new back-panel knobs** (§4, §6).
* ⚠️ Decide: does the front-page DLY panel retire? §11 lays out both options with the migration shim and preset-migration story.

### Build 4 — FILTER device in the rack
* Hosting project, not DSP: one `FilterSlot` + the motion block (env follower, synced LFO, **key-tracked cutoff — the feature no external FX plugin can do**).
* ⚠️ `FilterSlot::setType()` calls `reset()` internally (`TerrainFilters.h:1426`) → needs the fb345 deferred-fade + re-seat treatment or type switches click.

### Build 5 — MULTI-INSTANCE (`+` button, duplicates, reorder)
* ⚠️ **This is the one with a hard host constraint.** Params cannot be born at runtime → pre-allocated slot pool (K=5 proposed, 130 params). Read `FX-CHAIN-BIBLE.md` §3.2 before any UI work.
* ⚠️ Every new bus must join **all three** fb305/fb338 exclusion sums (`index.html:6979`, `:7111`) or sends double-count.

### Then the big family
Bode · Chorus · Flanger · Phaser · Widen · Compressor · OTT · Equalizer · Splitter · Utility.
Cheapest-first order if you want early wins: **Flanger → Phaser → Utility → Chorus → Bode → Widen → Compressor → OTT → Equalizer → Splitter** (Splitter last because it is chain architecture, not DSP).

---

## 3. OPEN DECISIONS — ordered by how much they block building

### 🚨 BLOCKS EVERYTHING — decide before device #4 wires
**`SYN_FX_ORDER` cannot grow in place.** It is a 6-way choice param (3 devices = 6 permutations).
Four devices = 24 entries, five = 120. Choice-param cardinality is fixed at birth (the fb342 law),
so this decision is **state-format-breaking if deferred**. Nearly every bible raises it independently.
The three options: (a) grow the dropdown to 24 and accept an unusable menu, (b) pin new devices to
fixed slots, (c) **replace order with a drag-list / rank-property now** — the chain bible's
recommendation, and it makes order click-free by construction (at the cost of non-automatable order).

### The four you asked me to bring you
1. **Widen name** — `Widen` ⭐ / `Swarm` / `Thicken` / `Stack` / `Multiply`. Ruled out by law: `Hyper`, `Dimension`, `Wider`, `Doubler`, `Unison` (collides with osc Unison), `Imager`.
2. **Splitter: one device or three** — firm recommendation **one**, Mode dropdown, plus a dedicated `Sub Split` mode (120 Hz default, Sub Mono + Rumble Cut controls that would be dead weight in plain Low/High).
3. **Granular Freeze vs the nothing-free-runs law** — recommendation C: env-decayed Freeze *knob* plus a **latched pill** as an explicit user gesture (the user asking for infinite ≠ the engine free-running).
4. **Does the DLY panel retire after the Moog port** — you said everything below the chopper gets replaced; §11 has both paths costed, including whether the migration shim runs silently or announces itself.

### Voicing / taste calls (cheap, but yours)
Type-roster trims (Chorus 7?, Flanger 6?, Phaser 9?, Granular 8?, Tape 7?) · Character count 8 vs 6 per Type (8 × 7 Types = 56 voicings ≈ 2 sweep sessions each) · Auto-makeup pill default ON or OFF (distortion precedent says OFF) · OTT `Air` default 25 % or hotter · EQ band count (4 fixed-role is the chassis-honest answer) · Bode `Guard` default · every visualizer pick (each bible proposes 2–4; all need your sign-off before mockups, per the mockup-first law).

---

## 4. CROSS-DEVICE LAWS THE SWEEP DISCOVERED

1. **Params cannot be born at runtime** (JUCE/VST3/AU). Dynamic device lists are always a pre-allocated pool with grow-on-demand UI. Serum 2 does exactly this (LFO 7–10 "appear" after you assign LFO 6).
2. **Zero-lookahead mandate, rack-wide.** The fb305 exclusion sums subtract dry sample-aligned — any latency-reporting device phase-smears the dry. Kills lookahead limiting; constrains linear-phase EQ and phase-vocoder options.
3. **Choice-param cardinality is fixed at birth** — every new device's Type list must be sized for its *final* roster on day one, disabled entries included.
4. **Type switches must fade-swap-recover.** Any engine whose `setType` resets state (FilterSlot, most cores) clicks otherwise. fb345 proved this the hard way on Distortion.
5. **Unity-through discipline:** every device at default must pass ≈ unity. This is the actual answer to the mud problem your friend Keon warned about — mud is cumulative gain drift plus masking plus resonance stacking, and the countermeasures are per-device (documented in each bible's §Interplay) plus the Utility joint plus per-slot metering.
6. **Mono-compatibility is a gate, not a feature** for every widener (Chorus, Widen, Utility, Splitter M/S): the bibles each carry a mono-sum test plan.
7. **Crossover phase coherence:** Linkwitz-Riley 4th order with allpass compensation on non-split lanes; perfect-reconstruction null test is the acceptance gate (Splitter, OTT).
8. **Envelope-gate everything that can self-oscillate** (feedback loops, noise, resonance) — and account for every gain stage inside a loop before claiming stability.
9. **The −26 dBFS bus reality** governs every threshold, drive and range in all 16 files. An OTT or compressor ported at literature values simply never engages.

---

## 5. WHERE WE ACTUALLY STAND VS SERUM 2

**We have that they don't:** granular FX, a tape echo machine, per-layer independent FX chains
(theirs are strictly post-sum), the four front-page performance modes, and per-device depth that
isn't close — 23 distortion modes vs their 13–18 (with no bit reduction at all), 9 reverb types vs
5 on a licensed TAL core, 4 delay types with ducking and an echo timeline vs one delay.

**They have that we don't:** free device ordering with multiple instances (the epic), parallel
busses, splitter lanes, Bode (the only module with no counterpart in our tree), and FX-rack presets.

**The frame:** $249 vs $99. We do not need 16 shallow modules — we need the chain architecture
carrying ten deep devices, each with a visualizer that moves. *Fewer, deeper, visibly alive* is a
position no screenshot war answers.

---

## 6. RESEARCH DEBT (be honest about this before building)

* **Verification pass incomplete.** Five bibles were adversarially audited and patched
  (Flanger, Phaser, Compressor, Equalizer, Splitter). The other eleven are researcher-written
  but un-audited — the sweep hit the usage-credit ceiling mid-verify. They are structurally
  complete and internally consistent; treat their boldest hardware numbers as unconfirmed until
  the verify pass is re-run.
* **No Serum 2 screenshots.** Their manual is not reliably fetchable and their visualizer
  *motion* (does it react to audio?) is unverified. Twenty minutes of you eyeballing Serum 2 live
  settles the visualizer baseline before any card mockup locks. Bode, Phaser and Serum2-Reference
  each name this as their top open question.
* **Un-spot-checked history numbers** flagged in-file: Ableton Grain Delay specs (Granular §1),
  RE-201 head timings (Tape §12 Q7 — do not quote in marketing copy unverified).

---

*Sweep: 32 agents, ~4.76 M tokens, 20 completed before the credit ceiling. The two agents whose
files exist despite a reported failure (`DELAY-MOOG-PORT-PLAN.md`, 915 lines) wrote to disk before
dying — verified complete by read.*
