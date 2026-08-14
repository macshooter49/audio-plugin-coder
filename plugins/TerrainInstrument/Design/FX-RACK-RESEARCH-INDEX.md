# FX RACK — RESEARCH INDEX
### The wake-up document. Read this first, then open only the bible you are building from.

**Written 2026-08-14 (overnight sweep). Audit closed 2026-08-14 (cross-bible pass).** 16 research
files, ~11,900 lines, one per remaining FX device plus the chain architecture and the Serum 2
teardown. Every bible follows the `DISTORTION-BUILD-BIBLE.md` format that produced a certified
device: scope → history → types → DSP math → chassis map → visualizers → interplay → presets → CPU →
pitfalls → hard-rule checklist → open questions → recycle inventory (file:line, read-verified) →
sources.

**Builder-sufficient by design:** each bible is written so a build session needs no re-research.

**All 16 are now adversarially audited.** ~141 factual/repo errors were corrected in place during the
per-file pass; a final **cross-bible pass** then hunted contradictions *between* files and found 11
more. Every fix is marked inline with `[AUDIT]` / `🔧` at the point of the error, so the wrong
version is visible next to the right one.

---

## 1. THE TABLE

| Bible | The device, in one line | Types | Verify | The most surprising find |
|---|---|---|---|---|
| `GRANULAR-FX-BUILD-BIBLE.md` | Live-bus granulator — **the headline differentiator** | 8 | ✅ AUDITED ×2 | We already ship **two** granulators; `GrainEngine.h` is the *live-input* one with a 5 s circular capture buffer already written every sample. The FX device is mostly wiring. 🔧 **The cross-bible pass then solved the file's own worst open question for free:** it had `Type` occupying back-dropdown-1 and no home for `Key`. `Type` is the **header pill** on every shipped device — so back-d2 was never spent, and `Key` lives there. Three Type names also had to change (below). |
| `TAPE-BUILD-BIBLE.md` | The tape **machine** — heads, motor, loop, splice | 7 | ✅ AUDITED ×2 | **Four tape surfaces already live in the tree** (DLY-panel colour, `TapeMachines.h`, TapeLoop looper, Distortion's hysteresis Tape). §0.2 draws a hard boundary law so they never tangle: Distortion owns the *magnetisation*, this device owns the *machine*. 🔧 The audit found that law stated in only ONE direction — `DISTORTION-BUILD-BIBLE.md` had never heard of it. It now carries the same block verbatim at its Tape mode. |
| `DELAY-MOOG-PORT-PLAN.md` | Port `MoogDelay.h` into the Delay device | 6 | ✅ AUDITED ×2 | The Moog engine has **five capabilities the shipped Delay lacks** (true BBD companding, clock-tracked bandwidth both sides, pitch-shifted feedback, 7-wave time LFO, freeze) — and all five port in as 2 new Types + 1 rebuild with **zero new knobs**. 🔧 New §0.9: the Delay/Granular boundary (recirculation vs re-read) — and the warning that capability 3 must stay *per-repeat*, never per-grain. |
| `FILTER-BUILD-BIBLE.md` | The synth filter, hosted as an FX device | 9 | ✅ AUDITED ×2 | `TerrainFilters.h` is **2,195 lines / 94 filter types** behind one stable `FilterSlot` façade. This is not a DSP project — it is a hosting-and-motion project. 🔧 The cross-bible pass added the boundary this file was missing (the EQ bible had its twin, this one didn't): **per-voice filtering is the synth panel's job; whole-bus filtering that MOVES is the device's.** A chord's notes sweeping individually vs a chord ducking together — the synth filters structurally cannot make the second sound. |
| `FX-CHAIN-BIBLE.md` | Multi-instance chain + the anti-mud doctrine | 6 | ✅ AUDITED ×2 | 🔑 **The `+` button ships and behaves exactly as designed** — add any device, duplicate freely, drag to reorder. Hosts cache the param list at load, so the params are **pre-allocated at startup and the `+` button fills them**. 🚨 **Max ruled `K = 24`, not 5** — *"we should have an unlimited amount of effects… their CPU is on them."* So the pool is not a taste judgement and **there is no CPU cap**: the price is that **an Empty or bypassed slot must cost EXACTLY zero**, verified by a 24-empty-slot CPU null. 600 new params ⇒ 1 564 total, unremarkable. |
| `COMPRESSOR-BUILD-BIBLE.md` | Single-band, 8 topologies, zero latency | 8 | ✅ AUDITED ×2 | **Zero lookahead is non-negotiable** — not a taste call. The fb305 main-send exclusion math subtracts dry *sample-aligned*; any latency-reporting device makes the dry leak back phase-smeared. *(Also: the only bible that got the chassis right first time — it put `Type` in the header pill, where the shipped tree puts it.)* |
| `OTT-BUILD-BIBLE.md` | 3-band up+down compression — the AIR machine | 8 | ✅ AUDITED ×2 | Your instinct was right and the bible confirms the boundary: OTT is a *fixed opinionated dynamics instrument*, the Splitter is *routing*. Serum 2 ships **both**. The upward computer on the high band is literally where the air comes from. Its two name collisions (`Squash`→`Amount`, Type `Air`→`Sheen`) turned out to be a **pattern, not an accident** — the EQ bible had independently reached for the identical `Air`-on-`Air` clash. |
| `EQUALIZER-BUILD-BIBLE.md` | The device EQ + audit of our old one | 7 | ✅ AUDITED ×2 | 🚨 **Our shipped v6 EQ heap-allocates 9–15 times per sample per channel** (`ParametricEQ.h:135-158` runs the full RBJ design math at audio rate). It survives as one instance; it is disqualified as a rack device engine. 🔧 Its own "no doubles anywhere, `Air` appears once" claim was **false when written** — Type 5 was also `Air`. Now `Open`. |
| `SPLITTER-BUILD-BIBLE.md` | ONE device, Mode dropdown (your call, confirmed) | 5–6 | ✅ AUDITED ×2 | Serum ships three splitters purely because their rack is a flat module list — **not a DSP argument**. Firm recommendation: one device, plus a dedicated `Sub Split` mode at 120 Hz (the club-mono line). 🔧 Splitter-vs-Utility is now settled as **per-lane vs global**: per-lane Width/Slip/Gain are the Splitter's (they exist only because lanes do); the global image is Utility's — which leaves exactly one real duplicate, M/S `Rotate`, flagged for a cut. |
| `FLANGER-BUILD-BIBLE.md` | 0.05–40 ms comb territory, incl. through-zero | 6 | ✅ AUDITED ×2 | **Serum 2 has no through-zero flanging** — headline differentiator — and the flanger is *the cheapest flagship we will ever build*: two fractional delay reads and a few one-poles. 🔧 Two cross-bible fixes: it had drawn the Chorus line at ">20 ms", which the Chorus bible disproves (see below), and its Type `Barberpole` collided head-on with Bode's → renamed **`Endless`**. |
| `PHASER-BUILD-BIBLE.md` | 9 allpass topologies, 8 characters each | 9 | ✅ AUDITED ×2 | Serum's phaser is ~6 params, one topology, one LFO shape. Our roster lands in **Soundtoys PhaseMistress territory** — and allpass chains are nearly CPU-free, so the budget goes to voicing. 🔧 It still treated Bode as hypothetical ("*if* Max later wants a true Bode device"); Bode is specced, so its Q6 ("cut the SSB Type?") is **closed — cut it**. |
| `CHORUS-BUILD-BIBLE.md` | 7 tap-count × LFO-topology answers | 7 | ✅ AUDITED ×2 | **There is no ms number that separates Chorus from Flanger** — the first draft's "below ~1.5 ms the comb fundamental enters the audible band" was backwards (shorter delay pushes the comb fundamental *up* and makes notches *sparser*). The real split is **notch density** (1/d spacing: 2 kHz at 0.5 ms = a resolvable swept filter, 50 Hz at 20 ms = two voices) plus **regeneration** and **through-zero**, neither of which this device has. The Time ranges deliberately overlap. 🔧 **The Flanger bible disagreed until this pass; it now carries the same table.** |
| `HYPER-BUILD-BIBLE.md` | Our unison-widener — working name **`Widen`** | 7 | ✅ AUDITED ×2 | Serum ships Hyper *and* Dimension as ONE menu item — a confession they are one job (thicken then widen). The pair-string "Hyper/Dimension" is the only thing that is actually theirs; the mechanisms are public-domain with 1979 precedent. 🔧 Two names moved: a back knob called `Delay` (a device's name) → `Offset`, and the `Shift` Type's hero relabel `Shift` → `Cents`. |
| `BODE-BUILD-BIBLE.md` | Frequency shifter (Hilbert SSB) | 7 (unresolved) | ✅ AUDITED ×2 | We already have a `BodeShifter` in `TerrainFilters.h`. Range chosen at **±5 kHz** (the 1630's), not Echobode's ±20 k — past ~5 k it is all foldover chatter on synth program. 🔧 Its own §4.0 already admits **7 Type names sit on 4 topologies** (unresolved — Max picks). The cross-bible pass added two renames (`Shift`→`Fixed`, knob `Track`→`Touch`) that hold whichever roster wins, and confirmed **`Barberpole` stays here** — it is the only true SSB one. |
| `UTILITY-BUILD-BIBLE.md` | Gain / width / bass-mono / meters — the joint | Route+Flip | ✅ AUDITED ×2 | **No fake Types** — law 5 cuts both ways. A utility that "colors" is a broken utility. Its dropdowns are Route and Flip, both discrete and sound-changing. It is also the rack's first real metering surface. 🚨 **The biggest cross-bible find lives here:** `FX-CHAIN-BIBLE.md` §5–§8 specs *this same device, under the same `SYN_UTL_*` prefix, with a completely different chassis* (6 Types, `Level·Width·Shape`, a `Meter` dropdown). Three documents, three positions. **This file wins; the ruling is applied in both.** |
| `SERUM2-FX-REFERENCE.md` | The competitive teardown, all 16 modules | 12 | ✅ AUDITED | Their FX are **strictly post-sum** (self-admitted paraphonic) while we ship per-layer independent chains. Their rack is wide and shallow; ours is narrow and deep. $249 vs $99. ⚠️ Note for Max: its `:376-379` claim that *"Terrain gets no Utility device"* is the third leg of the Utility contradiction above — closed by §3 decision 0. |

---

## 2. THE BUILD ORDER (yours, encoded)

### Build 1 — GRANULAR device
Size the existing front-page granular UI into the rack card, same layout + header/footer.
* Bible §2 is a **knob-by-knob inventory of the current front-page UI with line anchors** — that
  section exists specifically to make tomorrow morning mechanical.
* Recycle: `GrainEngine.h` (live-input granulator, 5 s circular buffer already written per-sample) +
  `GranularEngine.h` (the optimized grain pool, windows, skew LUT).
* 🔑 **Chassis, corrected — build it this way:** header pill = `Type` · back-d1 = `Character` ·
  **back-d2 = `Key`** (Off/Oct/5th/Chord/Maj/Min/Penta). Open Question #4 is closed for free.
* 🏷️ **Three Type names changed and Build 1 must use the new ones:** `Freeze`→**`Suspend`** (it was
  about to be the Type *and* knob P8 *and* the front pill — a triple), `Shimmer`→**`Rise`** (shipped
  Reverb Type *and* a shipped Reverb knob label), `Reverse`→**`Rewind`** (shipped Convolution pill).
* ⚠️ **Still decide before wiring:** the Freeze law tension (below) and buffer length (16.5 s =
  8.4 MB/instance at 48 k — audit-corrected up from 6.3 — vs 8 s).

### Build 2 — TAPE device
* ⚠️ **Read §0.2 (the boundary law) before writing a line.** Four tape surfaces already ship. This
  device is the *machine*; Distortion keeps the *magnetisation* (and now says so in its own file);
  TapeLoop keeps *recording*; the DLY-panel tape stays as channel-strip color.
* ⚠️ Open: does Tape join as the 4th **send** device (fb305-family wiring) or as the first
  **insert-only** device of the chain epic? Affects wiring order only, not DSP.

### Build 3 — MOOG DLY PORT into the Delay device
* Port 5 capabilities as 2 new Types + 1 rebuilt Type, **zero new back-panel knobs** (§4, §6).
* ⚠️ Decide: does the front-page DLY panel retire? §11 lays out both options with the migration shim
  and preset-migration story.

### Build 4 — FILTER device in the rack
* Hosting project, not DSP: one `FilterSlot` + the motion block (env follower, synced LFO,
  key-tracked cutoff). Read the new §0.0 boundary first — this device does **not** re-implement the
  synth panel's two-filter/series-parallel grammar.
* ⚠️ `FilterSlot::setType()` calls `reset()` internally (`TerrainFilters.h:1426`) → needs the fb345
  deferred-fade + re-seat treatment or type switches click.

### Build 5 — MULTI-INSTANCE (`+` button, duplicates, reorder)
**The feature is exactly what Max asked for: add any device, several of the same kind, drag to
reorder.** Nothing about the host constraint changes the UX.
* The build detail: hosts cache the parameter list at load time, so we **pre-allocate K device slots
  at startup and the `+` button claims the next empty one**. The user only ever sees an empty rack
  that grows. Same pattern as our own mod matrix, and as Serum's LFO 7–10 reveal.
* ✅ **K is decided: `K = 24`** (3 flagships + 24 slots = 27 devices), 25 params/slot ⇒ 600 new ⇒
  **1 564 total**. *(The "K=5 / 26 params / 130 on top of 972" figures previously carried here were
  all wrong; the arithmetic and the 964 baseline are corrected in the chain bible §3.2.)*
* 🚨 **The price of the generous pool, and it is non-negotiable: an Empty or bypassed slot costs
  EXACTLY zero.** Not "nearly". Acceptance gate = a 24-empty-slot CPU null against the 0-slot build,
  and the same with 24 claimed-but-powered-off. **We never cap for CPU — we SHOW CPU.**
* ⚠️ Every new bus must join every fb305/fb338 exclusion sum or sends double-count. 🔑
  **Audit-corrected addresses:** `PluginProcessor.cpp` **three blocks / six lines — 7159+7161
  (reverb), 7326+7328 (distortion), 7358+7360 (delay)**. The `:6979/:7111` numbers previously carried
  here and in memory were stale. Only the **L** lines carry the `fb305 law` comment, so grepping it
  finds 3 — never 6. **At K = 24 the hand-edited version of this is 27×27 = 729 terms**, so the §3.6
  exclusion-sum refactor is now a *precondition* of the epic, not a nicety.

### Then the big family
Bode · Chorus · Flanger · Phaser · Widen · Compressor · OTT · Equalizer · Splitter · Utility.
Cheapest-first order if you want early wins: **Flanger → Phaser → Utility → Chorus → Bode → Widen →
Compressor → OTT → Equalizer → Splitter** (Splitter last because it is chain architecture, not DSP).

---

## 3. OPEN DECISIONS — ordered by how much they block building

### 🚨 BLOCKS EVERYTHING — decide before device #4 wires
**`SYN_FX_ORDER` cannot grow in place.** It is a 6-way choice param (3 devices = 6 permutations).
Four devices = 24 entries, five = 120. Choice-param cardinality is fixed at birth **in both
directions** (the fb342 law) — index-preserving "append" does *not* make it safe, because hosts
normalize automation against `N` and our read path is `round(v·(N−1))`. Nearly every bible raises
this independently, and three were still proposing the illegal version until this pass (Phaser,
Equalizer, Flanger — all now corrected and all now defer here).
**`FX-CHAIN-BIBLE.md` §3.4 is the authority.** The options: (a) ~~grow the dropdown to 24~~ —
**illegal, not on the table**, (b) pin new devices to fixed slots, (c) a **brand-new** param born at
choice(24), (d) **replace order with a drag-list / rank ValueTree property now** — the chain bible's
recommendation, click-free by construction and the only shape that survives device #5's 120
permutations (at the cost of non-automatable order).

### 🚨 NEW — BLOCKS THE UTILITY BUILD
**Which Utility ships?** Three documents disagree: `UTILITY-BUILD-BIBLE.md` (no Types, Route+Flip,
`Gain·Width·Tilt·Mix` — **the recommended authority**, it is the dedicated device bible) ·
`FX-CHAIN-BIBLE.md` §5–§8 (6 Types, `Level·Width·Shape·Mix`, a `Meter` dropdown) ·
`SERUM2-FX-REFERENCE.md:376-379` (no Utility device at all). Both live specs use the same
`SYN_UTL_*` prefix, so they collide in code as well as on paper. **Recommendation: ship the device
bible's chassis, and fold in `Match` / `Pump` / `Fence` later as a behaviour selector** — those
three are the only ideas the chain bible has that the device bible lacks, and the anti-mud doctrine
genuinely wants them. One word from you closes all three documents.

### The four you asked me to bring you
1. **Widen name** — `Widen` ⭐ / `Swarm` / `Thicken` / `Stack` / `Multiply`. Ruled out by law:
   `Hyper`, `Dimension`, `Wider`, `Doubler`, `Unison` (collides with osc Unison), `Imager`.
2. **Splitter: one device or three** — firm recommendation **one**, Mode dropdown, plus a dedicated
   `Sub Split` mode (120 Hz default, Sub Mono + Rumble Cut controls that would be dead weight in
   plain Low/High).
3. **Granular Freeze vs the nothing-free-runs law** — recommendation C: env-decayed Freeze *knob*
   plus a **latched pill** as an explicit user gesture (the user asking for infinite ≠ the engine
   free-running).
4. **Does the DLY panel retire after the Moog port** — you said everything below the chopper gets
   replaced; §11 has both paths costed, including whether the migration shim runs silently or
   announces itself.

### Voicing / taste calls (cheap, but yours)
Type-roster trims (Chorus 7?, Flanger 6?, Phaser 9?, Granular 8?, Tape 7?, **Bode 4/5/7 — its §4.0
says the drafted 7 names sit on 4 topologies; this one is a law-5 blocker, not a taste call**) ·
Character count 8 vs 6 per Type (8 × 7 Types = 56 voicings ≈ 2 sweep sessions each) · Auto-makeup
pill default ON or OFF (distortion precedent says OFF) · OTT `Air` default 25 % or hotter · EQ band
count (4 fixed-role is the chassis-honest answer) · Bode `Guard` default · **cut the Flanger's
`Shift` (SSB) character? — it duplicates Bode's whole reason to exist** · **rename the Filter's Type
`Phaser` to `Notches`? — it collides with the Phaser device** · **cut the Splitter's M/S `Rotate`? —
it duplicates Utility's** · every visualizer pick (each bible proposes 2–4; all need your sign-off
before mockups, per the mockup-first law).

---

## 4. CROSS-DEVICE LAWS THE SWEEP DISCOVERED

1. **Params cannot be born at runtime** (JUCE/VST3/AU). Dynamic device lists are always a
   pre-allocated pool with grow-on-demand UI. Serum 2 does exactly this (LFO 7–10 "appear" after you
   assign LFO 6).
2. **Zero-lookahead mandate, rack-wide.** The fb305 exclusion sums subtract dry sample-aligned — any
   latency-reporting device phase-smears the dry. Kills lookahead limiting; constrains linear-phase
   EQ and phase-vocoder options.
3. **Choice-param cardinality is fixed at birth** — every new device's Type list must be sized for
   its *final* roster on day one, disabled entries included. It can never grow **or shrink**, and
   preserving the leading indices does not rescue it.
4. **Type switches must fade-swap-recover.** Any engine whose `setType` resets state (FilterSlot,
   most cores) clicks otherwise. fb345 proved this the hard way on Distortion.
5. **Unity-through discipline:** every device at default must pass ≈ unity. This is the actual answer
   to the mud problem your friend Keon warned about — mud is cumulative gain drift plus masking plus
   resonance stacking, and the countermeasures are per-device (documented in each bible's §Interplay)
   plus the Utility joint plus per-slot metering.
6. **Mono-compatibility is a gate, not a feature** for every widener (Chorus, Widen, Utility,
   Splitter M/S): the bibles each carry a mono-sum test plan.
7. **Crossover phase coherence:** Linkwitz-Riley 4th order with allpass compensation on non-split
   lanes; perfect-reconstruction null test is the acceptance gate (Splitter, OTT).
8. **Envelope-gate everything that can self-oscillate** (feedback loops, noise, resonance) — and
   account for every gain stage inside a loop before claiming stability.
9. **The −26 dBFS bus reality** governs every threshold, drive and range in all 16 files. An OTT or
   compressor ported at literature values simply never engages. *(⚠️ the Utility bible flags a real
   soft spot in this derivation — the first build must re-measure the bus at the insert point; if it
   lands at −20, every dB-referenced ceiling in every bible moves down 6 dB.)*

### 🆕 Added by the cross-bible pass

10. 🚨 **NO CAP FOR CPU — SHOW CPU (Max's K = 24 ruling).** The slot count exists only because hosts
    cache the parameter list, never as a performance judgement. The gate that replaces a cap:
    **an Empty or bypassed slot costs EXACTLY zero**, measured as a null. Corollary that changes
    design priorities: **the cheap devices must be aggressively cheap**, because chorus / EQ /
    compressor / filter / utility are what people actually stack — nobody stacks five reverbs.
11. 🔒 **THE CHASSIS IS `header Type pill + [3 heroes + Mix] + [2 back dropdowns + 8 knobs 4×2]`.**
    Verified against the shipped `DEVS` table. **Ten bibles had spent back-dropdown-1 on `Type`** —
    duplicating the header pill (the most visible label a card has) and silently throwing away a
    back dropdown they were entitled to. All ten now carry the correction. And the honest knob count
    is **12** (3 + Mix + 8), not the legacy "11" — which four bibles had each reconstructed
    differently. *Stop quoting "11"; check the shape.*
12. 🏷️ **NO-DOUBLES HAS THREE TIERS, PLUS ONE ABSOLUTE.** The absolute: **never the same word twice
    inside one device**, across Type / Character / knob / pill / dropdown header. Tier 1 (globally
    unique): device names and any *coined* word. Tier 2 (unique across the rack): **Type names and
    hero-knob names**. Tier 3 (deliberately shared, meaning must match): `Mix` `Tone` `Width`
    `Low Cut` `Hi Cut` `Spread` `Rate` `Depth` `Feedback` `Drive` `Attack` `Release` `Time` and the
    pills `Power` `Sync` `Auto` `Freeze` `Mono` `Solo` `Duck`. Reverb and Delay both shipping `Mix`
    is the chassis being consistent; two devices coining the same word for different mechanisms is
    the violation. **The absolute caught the most bugs — five devices had a word twice on their own
    card.**
13. 🧭 **EVERY DEVICE PAIR THAT SHARES TERRITORY NEEDS ITS BOUNDARY WRITTEN IN *BOTH* FILES.** Every
    boundary the sweep checked was stated in exactly one direction or not at all: Tape↔Distortion
    (one way), Chorus↔Flanger (contradictory), Filter↔synth-panel and Granular↔Delay (missing
    entirely), Utility↔Splitter (flagged but unresolved), Bode↔Phaser↔Flanger (three barberpoles,
    two with the same name). A one-way boundary is not a boundary — the other builder never reads
    your file. All are now reciprocal.

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

* ✅ ~~**Verification pass incomplete.** Five bibles audited, eleven un-audited.~~ **CLOSED — all 16
  are audited**, plus a cross-bible contradiction pass. ~141 factual/repo errors were corrected in
  the per-file passes and 11 cross-file contradictions in the final one. Remaining unverifiable
  claims are marked `⚠️ UNVERIFIED` **inline rather than deleted** — do not quote those as fact.
* ~~**Does Serum 2's visualizer move with audio?**~~ **RESOLVED — Max, 2026-08-14: YES, they move.**
  Three bibles named this as their top open question; it is closed. **The consequence: stop treating
  "does it move" as the differentiator — the differentiator is that ours move DRAMATICALLY and
  reflect every param (law 9, fb311).** Serum draws two FFT overlays total; our bar is a live,
  param-reflecting card per device. Build the visualizers as specced; no Serum screenshot is
  required to proceed.
* **Serum 2 param inventories** rest on the official manual + What's New PDF (their manual is not
  reliably fetchable). Good enough for competitive framing; do not quote their exact ranges as fact.
* **Un-spot-checked history numbers** flagged in-file: Ableton Grain Delay specs (Granular §1),
  RE-201 head timings (Tape §12 Q7 — do not quote in marketing copy unverified).
* **The one number that would invalidate arithmetic everywhere: the −26 dBFS bus.** Utility §0 shows
  the derivation may double-count the −6 dB voice pad. Measure it at the insert point on the first
  build, before any threshold is calibrated.

---

*Sweep: 32 agents, ~4.76 M tokens, 20 completed before the credit ceiling. The two agents whose
files exist despite a reported failure (`DELAY-MOOG-PORT-PLAN.md`, 915 lines) wrote to disk before
dying — verified complete by read. Audit + cross-bible pass: 2026-08-14, solo.*
