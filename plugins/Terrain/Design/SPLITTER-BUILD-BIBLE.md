# Terrain Instrument — Splitter Build Bible

*The 4th FX device. Parallel lanes: band / mid-side / sub / dual-mono splitting of the FX chain,
with the existing Reverb / Delay / Distortion devices as the lane processors.*

*Researcher's edition, 2026-08-14. Sources: Serum 2 "What's New" official PDF (splitter pages read
frame-by-frame), Kilohearts Multipass + Snap Heap docs, Bitwig dev statements, three KVR DSP threads,
Rane Note 160, Cytomic papers, FabFilter Saturn 2 / Pro-MB manuals, Arturia Dist COLDFIRE, and a full
recon of the fb341 chain code. Every file:line cited below was read, not assumed.*

---

## 0. The scope decision — ONE device, a Mode dropdown. (Answering Max's question first.)

**Serum 2 ships THREE splitter modules — `Splitter L/H`, `Splitter L/M/H`, `Splitter M/S` — because
its FX rack is a flat add-N-modules list and each splitter is a rack row.** (Official "What's New in
Serum 2" PDF, p. 11–13: the FX list shows `Splitter L/H`, `Splitter L/M/H`, `Splitter M/S` as three
separate entries alongside 13 effects.) That is a UI consequence of their architecture, not a DSP
argument. The three modules share one engine: split → lanes → merge. Only the split *law* differs.

**Recommendation — FIRM: Terrain ships ONE `Splitter` device with a `Mode` dropdown.** Max's instinct
is right and it is exactly our grammar: the Distortion put 23 modes behind one Type dropdown; the
Reverb put 9. A splitter has *fewer* natural modes than either. Three chassis for one engine would
violate the recycle law and burn two of nothing — we have one rack, not an infinite module list.

**The SUB question — also firm: a dedicated `Sub Split` MODE, *and* a wide crossover range in
Low/High.** Both, because they are different tools:
* `Low/High` with its crossover dragged to 100 Hz *can* isolate a sub — but a sub lane wants
  different defaults and different lane controls: a ~120 Hz default (the club-mono line — mastering
  practice puts stereo content ≥ +0.7 correlation below ~120 Hz), a **Sub Mono** control (the M/S
  side-HP trick every "mono-maker" uses internally), and a **Rumble Cut** guard. Those controls would
  be dead weight in plain Low/High.
* So: `Sub Split` is Mode 4 with its own back-8 (per-mode relabel — the Distortion families
  precedent, `ParameterIDs.hpp:414-421`), and `Low/High`'s crossover still runs 40 Hz → 8 kHz so it
  can do sub duty when someone wants the plain tool.

**What this device actually is:** not a new sound engine — a **topology device**. It splits the FX-bus
program into 2–3 lanes, lets each lane *target one of the three existing devices* (multiband
distortion, side-only reverb, high-band delay — the exact Serum 2 use cases), applies per-lane
trims, and merges phase-coherently. The DSP is a page of filter math; the engineering is the chain
integration (§6). That is why this bible's longest section is architecture, not physics.

---

## 1. History and circuits — the lineage

* **1933 — Alan Blumlein's stereo patent** (GB 394,325) contains the sum/difference ("M/S") matrix:
  transmit `L+R` and `L−R`, recover L/R by re-matrixing. Every M/S processor since is this matrix
  with gain in the middle.
* **1950s–70s — vinyl cutting, the elliptical filter:** out-of-phase bass makes the cutter head
  lift out of the groove, so lathes high-pass the *Side* channel (~50–300 Hz). This is the origin of
  "bass is mono below N Hz" — a *frequency-dependent M/S* move, i.e. exactly our Sub and M/S modes
  shaking hands. Maselec still sells the hardware.
* **1976 — Linkwitz & Riley** (Siegfried Linkwitz, Russ Riley, HP engineers): cascade two Butterworth
  filters → a crossover whose LP + HP sum has **flat magnitude and identical phase in both bands**
  — the LP and HP outputs of an LR4 are in phase with each other at every frequency, so they sum
  to an allpass with no notch. −6 dB per side at fc, in-phase, 24 dB/oct. Rane Note 160 is the
  canonical primer. This is the crossover *every* modern multiband plugin defaults to.
* **1990s–2000s — broadcast/PA DSP** standardizes the N-band LR tree: split low-first, re-split the
  high branch, and **insert allpass sections in the lower legs matching every crossover above them**
  so the recombined sum stays allpass (KVR "N-band Linkwitz-Riley crossovers" thread — the exact
  placement rule is quoted in §3.4).
* **2013 — Xfer OTT** (free) makes 3-band up/down compression a genre. Its viz — three vertical band
  strips with input/GR bars — taught a generation to *read* bands as stacked rows.
* **2014–2020 — the modular band-splitter hosts:** Ableton's Audio Effect Rack (parallel chains, no
  filters — users build crossovers from EQ Eight), **Bitwig Multiband FX-2/FX-3** (crossover
  container; its dev's phase statement is our §9.2 law), **Kilohearts Multipass** (up to 5 LR bands,
  per-band Gain/Pan/Mix/Post, drag the split bars on a live spectrum), **FabFilter Pro-MB / Saturn 2**
  (bands drawn over the spectrum, 6/12/24/48 dB/oct slopes, optional linear-phase for mastering),
  **Cableguys ShaperBox 3** (every effect gets an optional 3-band split with per-band LFOs).
* **2022 — Arturia Dist COLDFIRE** (released 2022-08-30): dual distortion engines with a routing selector —
  Serial / Parallel / Stereo / **Mid-Side** / **Band-Split** — i.e. Arturia reached the same
  conclusion we do: split modes are a *dropdown on one device*, not separate products.
* **2025 — Serum 2** puts splitters *inside a synth's* FX rack: `Splitter L/H`, `L/M/H`, `M/S`,
  each rendering as a rack row of **lane segments** with per-lane FX-count badges, defaults
  **210 Hz** (L/H and the low split of L/M/H) and **1000 Hz** (the upper split), lanes populated by
  indented groups (`LOWS`/`MIDS`/`HIGHS`, each with a `+`) in the rack list. This is the direct
  competitor feature Max is answering.

---

## 2. Modes — the Type dropdown (`Mode`), six candidates, five ship

Each mode = a split law. Law 5 gate: every mode must have a **measurable discriminator** proving it
is night-and-day. A splitter's modes are *topologies*, so the discriminators are routing/correlation
measurements, not spectra.

| # | Mode | Lanes | Split law | Discriminator (harness-measurable) |
|---|------|-------|-----------|-------------------------------------|
| 1 | **Low/High** | 2 | LR crossover @ `Split` (40 Hz–8 kHz, log, default 210 Hz — Serum 2's default) | Lane-solo spectra: Low lane −24 dB/oct above fc, High −24 dB/oct below; sum flat ±0.05 dB |
| 2 | **Low/Mid/High** | 3 | LR tree @ `Split` (40–800 Hz, def 210) and `Split Hi` (800 Hz–8 kHz, def 1 kHz) + allpass comp (§3.4) | 3 solo spectra band-limited as above; sum flat ±0.05 dB incl. around Split Hi (the allpass-comp proof) |
| 3 | **Mid/Side** | 2 | M=(L+R)/2, S=(L−R)/2; decode L=M+S, R=M−S | Mid-solo: correlation +1.0; Side-solo: correlation −1.0; mono-sum of Side lane = digital silence |
| 4 | **Sub Split** | 2 | LR crossover @ `Split` (30–250 Hz, def 120) + `Sub Mono` (side-kill below fc) + `Rumble Cut` | Correlation below fc → +1.0 as Sub Mono → 100; sub lane energy above 2·fc < −40 dB |
| 5 | **Left/Right** | 2 | trivial channel split | Lane solos are pure L / pure R (opposite channel < −120 dB) |
| 6 | **Punch/Tail** *(v1.5 — flag for Max, §11)* | 2 | transient/sustain decomposition: fast−slow envelope ratio gates lane A, remainder lane B; A+B ≡ input by construction | Lane A solo: crest factor rises ≥ 6 dB on drum material; A+B nulls to −inf vs input |

**Cut candidates and why:** *Wide/Center* is Mid/Side by another name (cut — no-doubles law).
A *Wet/Dry* splitter is meaningless in our insert grammar (cut). 4+ frequency bands (Multipass does
5) is over-scoped for an 11-param chassis and our three targetable devices — three bands saturate the
use cases (cut; revisit in the Patcher endgame).

**Night-and-day proof obligation:** modes 1/2/4 differ *by number and law of lanes*, 3/5 by *axis*
(stereo matrix vs frequency). The harness (§8) proves each mode's discriminator; a mode that can't
prove its row dies before ship — same rule that killed Soft Sat in the Distortion.

---

## 3. DSP core — the exact math

### 3.1 The crossover — LR4 on the TPT structure, and it is ALREADY IN THE TREE

**JUCE ships the whole crossover.** `juce::dsp::LinkwitzRileyFilter` —
`_tools/JUCE/modules/juce_dsp/processors/juce_LinkwitzRileyFilter.h` (verified read):

* TPT (topology-preserving transform) structure — the docstring says it verbatim: *"designed to
  perform multi-band separation using the TPT structure … −24 dB/octave slope (LR 4th order)"*.
* `Type::{lowpass, highpass, allpass}` — **the allpass type is the §3.4 compensation filter, free.**
* **The money API** (`.h:128`): `processSample (int channel, SampleType in, SampleType& outLow,
  SampleType& outHigh)` — one call returns BOTH bands, phase-matched, from one state
  (`.cpp:121-131`: the second cascade stage runs on `yL`, and `outHigh = in − outLow` — complementary
  by construction).
* `snapToZero()` for denormals; per-sample `processSample(channel, x)` fits our per-sample insert
  lambdas exactly.

This is the recycle verdict: **zero new filter code for slope 24.** For the other slopes (§3.3) we
build from `TerrainFilters.h` primitives: `TPTOnePole` (`TerrainFilters.h:83`) for 6 dB,
`SvfMultimode` (`TerrainFilters.h:317` — Simper TPT SVF, LP and HP taps both computed every sample)
for 12/48 dB. Simper's own paper (SvfLinearTrapOptimised2) is the stability warrant: trapezoidal
SVFs tolerate per-sample cutoff modulation without blowing up — which is why the crossover knob can
glide live.

### 3.2 The transfer functions (so the harness knows what "correct" is)

LR4 lowpass = squared 2nd-order Butterworth (Q = 1/√2 each):

```
H_LP4(s) = [ ω0² / (s² + √2·ω0·s + ω0²) ]²        H_HP4(s) = [ s² / (s² + √2·ω0·s + ω0²) ]²

H_LP4 + H_HP4 = (s² − √2·ω0·s + ω0²)·(s² + √2·ω0·s + ω0²)⁻¹ · … = AP2(s)   (2nd-order allpass)
```

* At fc each band is **−6 dB**; the sum is **0 dB** (flat) — LP and HP are *in phase at every
  frequency* (total 360° rotation), so no polarity flip is needed at slope 24.
* The recombined sum is NOT the input bit-stream: it is the input through `AP2` — flat magnitude,
  rotated phase. Every IIR splitter on earth shares this (Bitwig's dev, §9.2). Group delay of AP2 at
  DC: **τ(0) = 2/(Q·ω0)** ⇒ at fc = 120 Hz, Q = 0.7071: τ ≈ **3.8 ms** of bass-only lag, sliding to
  0 at HF. Audible on solo'd clicky bass at extreme settings, invisible on program — same class of
  honesty as the Distortion's ±0.5-sample ADAA note. Linear-phase FIR would null exactly but costs
  **latency**, which the fb305 send math forbids (Distortion bible §4.4 measured even 4 samples
  fatal). **Linear phase is BANNED in this device. Zero-latency IIR only. The device reports and
  has 0 samples latency.** This is the Splitter's single biggest engineering advantage: none of the
  Distortion's §4.4 latency agony applies.

### 3.3 The `Slope` dropdown (back d2) — four laws, each a different animal

| Slope | Build | Sum property | Character |
|-------|-------|--------------|-----------|
| **6 dB** | one `TPTOnePole` LP; HP = in − LP | **PERFECT: LP+HP ≡ input, bit-exact, zero phase error** (first-order complementary) | huge band overlap; the "transparent glue" split. The KVR transparent-splitter thread: only 6 dB/oct splitters truly null. |
| **12 dB** | one 2nd-order `SvfMultimode` at **Q = 0.5** (LR2 = two cascaded 1st-order sections; `res01 = 0` in `setCoeffs` gives Q 0.5 exactly. ⚠️ NOT Butterworth Q 0.7071 — at Butterworth Q the flipped sum BUMPS **+3 dB** at fc) — **HP must be POLARITY-INVERTED to sum allpass** (LR2's classic trap: LP and HP are 180° out at fc; un-flipped they NOTCH at fc). Each band −6 dB at fc; sum = 1st-order allpass | flat ±0 dB after the flip | vintage crossover feel, moderate overlap |
| **24 dB** | `juce::dsp::LinkwitzRileyFilter` — **the default** | flat, AP2 rotation | the industry-standard multiband split (Serum 2, Multipass, OTT) |
| **48 dB** | cascade two LR4 (= LR8, two `LinkwitzRileyFilter`s per split; comp allpass = AP4 built from the two AP2s) | flat, AP4 rotation | surgical isolation (FabFilter's steepest is also 48) |

Slope is *audible in the lane overlap* — the discriminator: solo the Low lane at fc·4 (two octaves
up); level reads ≈ −12/−24/−48/−96 dB for slopes 6/12/24/48. The dropdown never clicks: slope switch = full fade-swap-recover
(§9.6), same as a reverb type swap.

### 3.4 Three bands — the allpass-compensation law (the one everyone gets wrong)

Split tree (low-first), from the KVR N-band thread — *"for every extra split, you just add an
allpass (same Q and order) into the lower leg(s)"*, highest band gets none:

```
in ─┬─ LP@f1 ── AP@f2 ──────────► LOW        f1 = Split (low crossover)
    └─ HP@f1 ─┬─ LP@f2 ─────────► MID        f2 = Split Hi (upper crossover)
              └─ HP@f2 ─────────► HIGH       AP@f2 = LinkwitzRileyFilter Type::allpass at f2
```

Omit `AP@f2` and Low recombines against Mid/High with AP2(f2)'s rotation missing ⇒ dips around f2
in the sum — the classic "my multiband sounds phasey at the mid crossover" bug. The acceptance gate
(§8) demands sum flatness **±0.1 dB from 20 Hz–20 kHz with all lanes Thru** — it catches this in one
sweep. At slope 6 the tree needs no compensation (zero phase error); at 12 the comp is the LR2's
1st-order allpass equivalent; at 48 it is AP4.

### 3.5 Mid/Side — the exact matrix and the width laws

```
encode:  M = 0.5·(L + R)      S = 0.5·(L − R)          (the ÷2 convention — decode needs no gain)
decode:  L = M + S            R = M − S                 (verified identity: lossless, bit-exact)
```

* **Side Gain** = width: +6 dB doubles width; `S=0` ⇒ mono. Range **−∞ (kill) … +12 dB** — at the
  −26 dBFS bus (§Law 1) +12 dB of side leaves ≥ 8 dB headroom on any legal program; the fb264
  master limiter guards pathological cases. No-playing-safe: max = "just past useful" — +12 dB side
  on a wide pad audibly *unwraps* the speakers and starts to hollow the center. That is the point.
* **Bass Mono** (Sub-mode logic living inside M/S too): HP the **S** signal at `Bass Mono` Hz
  (0 = off … 500 Hz) with slope = d2. This is *literally* the vinyl elliptical filter and the
  internal mechanism of every "mono-maker" (SOS Q&A confirms: mono-below-N devices are M/S with a
  side HP). Discriminator: correlation below the knob frequency → +1.0.
* M and S are the *lanes*: Side→Reverb is Serum 2's own demo patch ("reverb only on the Side,
  Mid stays dry").

### 3.6 Punch/Tail (mode 6, optional) — decomposition that reconstructs by construction

```
env_f = follower(|x|, atk 1 ms,  rel 20 ms)         env_s = follower(|x|, atk 20 ms, rel 250 ms)
t     = clamp01((env_f − env_s) / (env_f + ε)) — smoothed 5 ms
PUNCH = t·x        TAIL = (1−t)·x                    PUNCH + TAIL ≡ x  (exact, always)
```

Same-domain gain masks ⇒ perfect reconstruction with **zero** phase cost (better than the frequency
modes!). d2 relabels to `Detector` (Fast/Standard/Slow/Loose = four fixed fast:slow RATIO profiles;
back P3 `Attack`/P4 `Release` set the fast follower's times directly and the slow follower derives
as fast × the profile ratio — the dropdown is the law, the knobs the time trim; no double-control,
§5.2). The lane
targets make it a transient-only distortion or tail-only reverb — nothing in Serum 2 does this;
it is the device's originality card. Costs two one-pole followers. Recommend: ship behind a
version gate after Max hears modes 1–5 (open question §11.1).

### 3.7 Param laws (range · taper · glide — every continuous param)

| Param | Range | Taper | Glide |
|---|---|---|---|
| Frequency knobs (`Split`, `Split Hi`, `Bass Mono`, `Sub Cross`, `Rumble Cut`, `Bleed Tone`) | per-mode (§2, §5) | **log** (`f = fmin·(fmax/fmin)^t` — octaves feel linear) | 15 ms one-pole on the *log-domain* value, per-sample (`xC += (xT−xC)·smth` — the `DelayEngine.h` idiom). TPT filters take per-sample cutoff safely (Simper), the glide exists to kill knob zipper, not to protect stability. `Split Hi` clamps ≥ 1 octave above `Split` (Multipass forbids crossing bars; we forbid closer than 1 oct — below that the mid lane collapses to a resonator). |
| Lane Gains | −60 dB…+12 dB, −60 = kill | dB-linear above −30, fast-fall to kill below | 15 ms linear-gain glide |
| Widths (per-lane) | 0 (mono) … 200 % (side ×2) | linear | 15 ms |
| Pans | ±100 % equal-power | sin/cos | 15 ms |
| Bipolar trims (`Side Tilt`/`Mid Tilt` ±6 dB, `Rotate` ±45°, `Asym`) | stated in §5.2 | linear, 0.5 = neutral (dblclick-reset home) | 15 ms |
| Unipolar amounts (`Swap`, `Sub Mono`, `Sharpen`, front `Bleed`) | 0–100 %, 0 = off/neutral | linear | 15 ms |
| P/T `Attack` / `Release` | 0.1–10 ms / 20–500 ms | log (time knobs feel linear in octaves) | 15 ms |
| `Slip` | 0…20 ms | `t²` (drama lives past 8 ms) | **crossfade, not glide** — a moving delay is a pitch bend; retarget via 10 ms dual-tap crossfade (the comb-click law, delay bible) |
| `Balance`, `Spread`, `Mix` (front) | 0–100 | linear | 15 ms |

**Time-param house rule note:** the Splitter has **no tempo-synced param** — `Slip` is a Haas
micro-delay (0–20 ms), below the shortest musical division; the 4-bars→1/256 rule therefore does not
bind any knob in this device (stated so nobody "fixes" it later).

### 3.8 Oversampling verdict

**NONE. EVER.** Every path in this device is LTI (filters, gains, delays) or slow-envelope gain
(Punch/Tail masks band-limited ≪ Nyquist by the 5 ms smoother). Nothing creates harmonics ⇒ nothing
aliases ⇒ oversampling would buy zero and cost latency + CPU. The Splitter has no Quality dropdown —
its d2 slot is `Slope` (which IS its quality axis, honestly named). Nonlinearity lives in the
*targeted devices*, which already own their AA budgets (Distortion bible §3).

---

## 4. NO PLAYING SAFE — the extremity table

The Splitter's drama is *routing* drama; its knobs must still hit "just past useful" at 100:

| Param @ 100 % | What happens | Why it's "just past useful" |
|---|---|---|
| `Balance` (front) | the OTHER lane is **gone** (−60 dB) | a performable band-kill / side-kill morph — sweeping it live is a DJ filter kill on steroids |
| `Spread` (front) | lows hard-mono + highs at 200 % width + high-lane pan wander unlocked | the master-chain "club prep" exaggerated past mastering taste into sound design |
| `Side Gain` +12 dB | center hollows out, walls arrive | the "outside the speakers" break point |
| `Slip` 20 ms | Haas collapses into audible slapback; mono-sum combs hard (§9.4 states the comb) | width → wrongness is the knob's story; 100 % IS the wrongness |
| `Sub Mono` 100 + `Sub Cross` 250 Hz | everything below 250 collapses mono | kills stereo bass dead — brutal on wide pads, exactly the point |
| Crossover extremes | Low/High at 8 kHz = "everything is the Low lane" — the targeted FX eats nearly the whole program; at 40 Hz the High lane is the whole program | the knob morphs *how much program the lane FX owns* — a genuinely new performance gesture (crossover-as-send-amount) |

**Timidity guard (Law 1):** there are no drive/threshold knobs here to mis-copy, but the −26 dBFS
bus still bites twice: (1) lane **gain** ranges are stated in dB *relative to unity in the chain* —
never absolute dBFS; (2) any future lane-dynamics idea (§11) must state thresholds relative to
−26 dBFS program or it will never trigger — the Compressor bible's problem, pre-answered.

---

## 5. Chassis map — the locked fb275 grammar, 11 params + pills + lane strip

Follows the Distortion precedent byte-for-byte: front 3 knobs + Mix, back 2 dropdowns + 8 knobs
(4×2, 3 separators), 2 front pills, per-mode relabels of generic slots. New param IDs mirror
`SYN_DST_*` (`ParameterIDs.hpp:406-431`):

```
> 🔧 **[CROSS-BIBLE AUDIT 2026-08-14] CHASSIS CORRECTION — `Type` is the HEADER PILL, not back-d1.**
> Verified in the shipped tree: on Reverb, Delay **and** Distortion, `*_TYPE` renders in the header
> `.fxr-type` `<select>` on the card centerline (`index.html` `DEVS[].tp` +
> `Design/fx-back-panel-mockup.html`); the two **back** dropdowns are `Character` + a second
> selector (`Mod Mode` / `Sync` / `Quality`). Spending back-d1 on `Type` duplicates the header pill
> — the most visible label the card has — and silently throws away a back dropdown this device is
> entitled to. Move `Type` to the header, slide `Character` to back-d1, and back-d2 is free.
> Full ruling (incl. that the honest knob count is **12** = 3 heroes + Mix + 8 back, not the "11"
> four bibles reconstructed four different ways): `FX-CHAIN-BIBLE.md` §7.1.

SYN_SPL_MODE        choice(5|6): Low/High · Low/Mid/High · Mid/Side · Sub Split · Left/Right [· Punch/Tail]
SYN_SPL_SLOPE       choice(4):  6 dB · 12 dB · 24 dB · 48 dB     (d2; relabels per mode, §5.3)
SYN_SPL_SPLIT       float 0..1  — front hero, the SIGNATURE knob, relabelled per mode (like DST's SIG)
SYN_SPL_BALANCE     float 0..1  — front, lane A↔B(↔C) energy morph, 0.5 = neutral
SYN_SPL_SPREAD      float 0..1  — front, mono-lows↔wide-highs fan, 0.5 = neutral
SYN_SPL_MIX         float 0..1  — front, 100 % = fully wet (§6.5 defines wet/dry PHASE-MATCHED)
SYN_SPL_P1..P8      float 0..1  — back-8, per-MODE relabels (§5.2)
SYN_SPL_LANE1_TGT   choice(5):  Thru · Reverb · Delay · Distortion · Mute      ┐ the lane strip —
SYN_SPL_LANE2_TGT   choice(5):  〃                                             │ real dropdowns on
SYN_SPL_LANE3_TGT   choice(5):  〃  (L/M/H only; hidden elsewhere)             ┘ the card (§5.4)
SYN_SPL_SOLO        choice(4):  Off · Lane 1 · Lane 2 · Lane 3   (pill-latched audition)
SYN_SPL_POWER       bool — default OFF (byte-identical dry init, like every device)
```

*(No `SYN_SPL_SRC_*` per-osc pills in v1 — deliberate; §6.4 explains, §11.3 asks.)*

### 5.1 Front card

| Slot | Name | Notes |
|---|---|---|
| Knob 1 | **`Split`** | the signature knob — per-mode relabel: L/H & L/M/H `Split` (primary crossover) · Sub `Sub Cross` · M/S `Bass Mono` · L/R `Bleed` (cross-feed amount) · P/T `Bias` (punch↔tail detector tilt) |
| Knob 2 | **`Balance`** | 0 = lane A only … 100 = last lane only (3-lane: A→B→C sweep, equal-power segments) |
| Knob 3 | **`Spread`** | 0 = ALL lanes mono → 50 neutral → 100 = low lane mono + top lane 200 % wide (in M/S: pure width; in L/R: rotation) |
| Knob 4 | **`Mix`** | 100 % = fully wet (phase-matched blend, §6.5 — never a comb) |
| Pill 1 | **`Solo`** | latches `SYN_SPL_SOLO` to the lane selected in the strip — audition without touching gains |
| Pill 2 | **`Mono`** | output mono-fold audition (the club/car check — Gearspace practice) — **latching**, bright active state (resolved by the state-persists law, §11.5) |
| Core viz | §7 | the Band Stack |
| **Lane strip** | §5.4 | the Serum-style segments — Terrain's first in-card routing UI |

### 5.2 Back-8 per mode (generic `SYN_SPL_P1..P8`, relabelled — the DST family precedent)

**Mirror-binding law:** wherever a back slot repeats the front `Split` relabel (L/H P1 `Crossover`,
L/M/H P1 `Split`, Sub P1 `Sub Cross`, M/S P3 `Bass Mono`), that slot BINDS `SYN_SPL_SPLIT` itself —
the same-param mirror, exactly how the Delay's back `Time L` binds `SYN_DLY_TIME`
(index.html:7485). The generic `SYN_SPL_Pn` behind a mirrored slot is unused in that mode and never
exposed (no dead knob).

**Low/High** *(P1 mirrors front `Split`, the Delay Time-L precedent, index.html:7485 `Time L` mirror)*
```
P1 Crossover (40 Hz–8 kHz log, def 210)   P2 Low Gain (−60..+12)   P3 High Gain   P4 Low Width (0–200 %)
P5 High Width                             P6 Low Pan               P7 High Pan    P8 Slip (High lane 0–20 ms)
```
**Low/Mid/High**
```
P1 Split (40–800, def 210)   P2 Split Hi (800–8 k, def 1 k)   P3 Low Gain    P4 Mid Gain
P5 High Gain                 P6 Low Width                     P7 High Width  P8 Slip (High)
```
**Mid/Side**
```
P1 Mid Gain      P2 Side Gain (−∞..+12)   P3 Bass Mono (Off–500 Hz)   P4 Side Tilt (dark↔bright ±6 dB tilt on S)
P5 Rotate (±45°) P6 Asym (side L↔R skew)  P7 Slip (Side)              P8 Mid Tilt
```
> ⚠️🔧 **[CROSS-BIBLE AUDIT 2026-08-14] P5 `Rotate` is a genuine duplicate — decide before wiring.**
> `UTILITY-BUILD-BIBLE.md` §5.2 P8 ships `Rotate` at the identical ±45° on the identical M/S matrix.
> The line the sweep drew between these two devices is **PER-LANE vs GLOBAL**: this device owns the
> crossover and everything that exists *because lanes exist* (per-lane Gain/Width/Pan/Slip,
> `Bass Mono` as the M/S crossover frequency) — Utility owns the **global image** (one Width, one
> Haas `Widen`, one `Rotate`, one `Balance`, one `Center`, one `Mono Below` shelf). A whole-image
> rotation is not a lane operation, so by that line it belongs to Utility.
> **Recommendation: drop P5 `Rotate` here and spend the slot on something lane-specific** (a `Side
> Delay`-style lane offset, or promote `Asym` to a proper per-side skew pair). A user who wants
> rotation adds a Utility — which is the whole point of the chain epic. Everything else in these
> six maps was checked and is clean; per-lane `Width`/`Slip` are legitimately this device's.
**Sub Split**
```
P1 Sub Cross (30–250, def 120)   P2 Sub Gain   P3 Body Gain   P4 Sub Mono (0–100 %)
P5 Rumble Cut (10–60 Hz HP)      P6 Body Width P7 Body Pan    P8 Slip (Body)
```
**Left/Right**
```
P1 Left Gain   P2 Right Gain   P3 Swap (0–100 % L↔R crossfade)   P4 Left Pan
P5 Right Pan   P6 Rotate       P7 Slip (Right — the Haas knob)   P8 Bleed Tone (crossfeed LP 1–16 kHz)
```
**Punch/Tail** *(if shipped)*
```
P1 Punch Gain   P2 Tail Gain   P3 Attack (0.1–10 ms)   P4 Release (20–500 ms)
P5 Sharpen (mask contrast)     P6 Punch Width          P7 Tail Width          P8 Slip (Tail)
```
*(P3/P4 = the FAST follower's times; the slow follower derives via the `Detector` ratio profile —
§3.6. Not a double-control.)*

Pragmatic-names audit: every label says what the knob does; the only acronym-free exceptions are
mode names themselves (`Mid/Side` is the honest name of the thing; "Center/Sides" would be a double
against the industry term). `Slip` chosen over "Haas" (jargon).

### 5.3 The d2 relabels

`Slope` for Low/High · Low/Mid/High · Sub Split (also drives Bass-Mono side-HP order in M/S — label
`Side Slope`) · L/R relabels `Bleed Mode` (Off/Dark/Neutral/Bright — crossfeed voicing; Off = knob P8
inert is NOT allowed, so `Bleed Mode: Off` also zeroes front `Bleed` and grays it — no dead knob, the
control moves to the pill state) — *simplification candidate: drop L/R d2 to `Slope: —` is banned
(dead control); this relabel is the fix* · Punch/Tail relabels `Detector` (Fast/Standard/Slow/Loose).

### 5.4 The lane strip — the one NEW UI element (everything else is recycled chassis)

Serum 2's splitter row (What's-New PDF p. 13, read at pixel level): each lane = a **segment pill**
[`LOWS ▾  n  ⊘`] — name, a dropdown chevron, a count badge (the number of FX in that lane — verified:
the populated rack shows LOWS=2/MIDS=1/HIGHS=1 matching its sidebar, the empty modules show 0), a
per-lane bypass glyph; crossover value readouts (210 / 1000) sit *between* segments; the selected
lane is tinted. **Ours:** the same strip on the card between the header and the knobs — per lane:
* the lane name (auto per mode: `Low/High/Mid/Side/Sub/Body/Left/Right/Punch/Tail`),
* a **real dropdown** (the `engine-select` overlay idiom — the dropdowns-not-click-rotate law) for
  `Thru / Reverb / Delay / Distortion / Mute`,
* a target-colored dot when a device is owned (reverb teal / delay violet / distortion ember — the
  devices' existing accent colors, matched not invented),
* click segment = select (drives `Solo` pill target + viz highlight),
* the crossover readout between segments doubles as the §7 drag handle.
Centerline law applies to the strip exactly as to headers: equal-height boxes, `align-items:center`,
zero per-element nudges.

---

## 6. Architecture — chain integration (THE hard part; read with the code open)

### 6.1 The chain as it exists (fb341, all verified)

* Per-sample serial chain of insert lambdas: `applyRvb` (`PluginProcessor.cpp:7137`), `applyDst`
  (`:7309`), `applyDly` (`:7345`), ordered by `switch (fxPerm_)` (`:7383-7392`), `fxPerm_` read from
  `SYN_FX_ORDER` — an `AudioParameterChoice(6)` (`ParameterIDs.hpp:435`, built at
  `PluginProcessor.cpp:3488`, read as index at `:5860`).
* Each device: MAIN-SEND mode eats `leftChannel[i] − rtd` where `rtd` = **the fb305 exclusion sum**
  — routed-osc send buses × `outputGain × kVoiceToFxPad`; or PER-OSC mode eats its own send bus.
* **The landmine (fb305/fb338):** the exclusion sum appears at THREE sites —
  `PluginProcessor.cpp:7159/7161` (reverb), `:7326/7328` (distortion), `:7358/7360` (delay) — each
  already summing all three send buses (`rvbSendL + dlySendL + dstSendL`), each carrying the comment
  *"the fb305 law: EVERY send bus joins EVERY main-send exclusion"*. Send buses live at
  `PluginProcessor.h:1534/1559/1572`.
* UI: `DEVS` array (`index.html:7479`) — device objects with `core:` keys `'reverb'|'delay'|
  'saturate'`; drag order writes `SYN_FX_ORDER` normalized (`:8323`, `pi/5`); restore
  `fxrRestoreOrder` (`:7974-7988`) maps the 6-perm table.

### 6.2 🔑 THE INTEGRATION LAW — v1 adds NO new send bus, so the exclusion sums are UNTOUCHED

The Splitter v1 is **main-send only**: its input is the whole mix at its chain position, exactly like
a device with no pills lit. Its input therefore uses the *identical* `sg = leftChannel[i] − rtd`
expression with the *existing three-bus sum* — copied, not extended. **No `splitterSendBuf_` exists
in v1 ⇒ the three exclusion sites keep exactly three terms ⇒ fb305 cannot re-break.** This is the
single most important scoping decision in this bible, and it is what makes the device shippable
without touching the landmine.

*(If Max later wants per-osc pills INTO the splitter — §11.3 — THEN `splitterSendBuf_` must be born
in the same commit that adds `+ (splSendL ? splSendL[i] : 0.0f)` to ALL THREE sites (six lines: L+R
at 7159/7161, 7326/7328, 7358/7360) plus the splitter's own symmetric four-term subtraction. The
Distortion bible §4.5 wrote this law for device 3; it holds verbatim for any device N.)*

### 6.3 Lane ownership — how a lane "contains" a device without nested racks

Terrain has ONE instance of each device (CPU-friendly node-chain law) — we cannot host arbitrary
child chains like Serum/Multipass. What we *can* do — and what covers every Serum 2 splitter demo
(multiband distortion, side-only reverb, high-band delay) — is **lane targeting**: a lane claims one
of the three existing devices; the claimed device processes THAT LANE instead of the main mix.

Per-sample flow when `splPower_` and ≥ 1 lane targets a device:

```
1. applySplSplit()   — runs FIRST among active FX regardless of drag position:
                       sg = leftChannel[i] − rtd            (the SAME fb305 expression)
                       lane[0..2] = split(sg)               (per mode; §3)
                       laneAcc = Σ lanes whose target == Thru/Mute (Mute ⇒ 0), with per-lane trims
2. owned devices     — each targeted device's insert lambda runs with TWO substitutions:
                       · input: its main-send `sg` becomes `lane[k]` (already exclusion-clean —
                         it MUST NOT re-subtract rtd; one flag: `ownedBySplitter`)
                       · output: accumulates into `laneAcc` instead of `leftChannel[i] +=`
                       (their internal Mix/env/duck math is UNTOUCHED — the fb318 engine-owns-Mix
                        grammar survives; a lane-owned Distortion at Mix 50 % is half-distorted lane)
3. applySplMerge()   — leftChannel[i] = rtd + splMix·laneAcc + (1−splMix)·laneAccDryTrims
                       (§6.5 defines the two accumulators; rtd rides through untouched so per-osc
                        routed oscs still reach their own devices' send paths)
4. un-owned devices  — run EXACTLY as today, in their fxPerm_ relative order, on the merged mix.
```

**Ownership conflict rules (hard):**
* A device with ANY route pill lit cannot be lane-owned; the UI grays it in the lane dropdown
  (and vice versa: owning a device grays its pills). One signal path per device — no double-feeding.
* A device can be owned by ONE lane (the dropdowns mutually exclude, same-name-grayed).
* Owned devices leave the serial permutation; the perm applies to whatever remains un-owned. The
  drag order of an owned device is remembered (state-persists law) and re-applies on release.
* `SYN_FX_ORDER` stays a choice(6) in v1 — the Splitter does NOT join the permutation; it runs at
  the head of the FX section (post-filter, pre-serial-chain). Making it draggable would force
  4! = 24 perm states for near-zero musical gain (the split-into-lanes topology already defines
  "before the devices"); ship fixed-first, revisit only if Max asks for "serial FX *then* split".

### 6.4 Why no per-osc pills in v1 (the honest reason)

Pills would route *one oscillator* into the splitter — but the splitter's product is *lanes of the
whole program*. Osc-level selection is already served: route osc → device directly (fb338). The
combination "osc C only, split, mid lane to delay" is real but third-order; it costs the §6.2
landmine edit plus a fourth bus everywhere. Defer until a user asks for it by name (§11.3).

### 6.5 The Mix law — phase-matched or it combs (the Bitwig lesson, made a rule)

Bitwig's own developer, on why Multiband FX-3 at Mix 0 still sounds different: the split is
*"based on phase shifting"* and therefore *"the dry signal has to go through the same EQ
configuration … if it would not do that, you would get strange phase cancellation issues."*
Quantified for our case: blending un-rotated dry with AP2-rotated wet at Mix 50 % gives
`|0.5 + 0.5·e^{jφ(ω)}|` — at each crossover frequency φ = 180° ⇒ **a perfect notch at every fc.**

**THE RULE:** `Mix` blends **processed lanes vs unprocessed lanes** — both sides of the fade live
*after the same split filters*:
* `laneAcc` = lanes with trims + owned-device processing,
* `laneAccDryTrims` = the same lane signals with **no** trims and **no** owned devices (free — they
  are the raw `lane[k]` values already computed).
Both share the identical phase rotation ⇒ Mix sweeps cleanly at every position, no comb, ever.
At Mix 100 % = fully wet (house law). At Mix 0 with Power ON, output = allpass-rotated input
(= "transparent" in the industry sense; the honest not-bit-exact note lives in §3.2). Power OFF =
byte-identical bypass — no filter in circuit at all (the fb318 "no delay line in circuit when off"
precedent).

### 6.6 Unity-through discipline (interplay opening move)

Defaults: all lane gains 0 dB, widths 100 %, pans center, targets Thru, Mix 100 %, Balance/Spread
50. With Power ON, the device then passes **unity magnitude ±0.05 dB** (the §8 gate) — a chain with
the Splitter idling measures flat. Nothing in the device adds gain at defaults, so chain level
discipline (Serum-parity fb299-302 laws) is untouched.

### 6.7 Interplay wisdom (what it does downstream, what breaks when stacked)

* **Splitter → serial devices:** un-owned devices see the *merged* signal — e.g. Low/High with
  High→Delay, then serial Reverb on everything: the reverb hears delayed highs + dry lows. That is
  the classic desk topology (multiband first, glue later). Correct default.
* **Owned Delay feedback:** stays entirely inside `DelayEngine` (its loop is internal, fb306-310
  loop-gain law) — lane ownership feeds its INPUT only; **no new feedback path exists** (see Law 6
  walk, §12). Max stable loop gain is unchanged from the delay's own cert.
* **Spectrum downstream:** lane gains are a 2/3-band shelving EQ at merge; `Spread` is
  frequency-dependent width — downstream mono-ing (a DAW utility) will drop wide-high energy: the
  `Mono` pill exists precisely to audition this before it surprises anyone.
* **Stacking:** one Splitter instance exists; "splitter inside a lane" is impossible by
  construction (no self-target in the dropdown) — the Patcher endgame is where nested topologies
  belong (`terrain-instrument-node-architecture-patcher`).
* **The order pitfall to document for users:** Sub Split with Body→Distortion then serial Reverb =
  clean sub under distorted-and-reverbed body (good); dragging Reverb *before* the splitter in a
  future draggable-splitter world would reverb the sub too (why v1 pins the splitter first).

---

## 7. Visualizers — survey, then ours

### 7.1 How the greats draw a splitter (mechanisms, precisely)

| Product | Mechanism |
|---|---|
| **Serum 2** (PDF p. 13) | The card IS the viz: lane segments sized/labeled (`LOWS/MIDS/HIGHS`, `MID/SIDE`), selected lane tinted green, per-lane FX-count badge + bypass glyph, crossover Hz readouts between segments, "at-a-glance views with **direct manipulation**" — drag values on the card itself. No spectrum on the splitter row. |
| **Kilohearts Multipass** | Full-width live spectrum analyzer; crossovers = vertical bars **dragged on the spectrum** (bars cannot cross); lanes colored; per-lane Gain/Pan/Mix/Post knobs in lane footers. |
| **FabFilter Pro-MB** | Bands drawn as translucent regions over a live spectrum; hover a crossover → Split/Unsnap buttons; per-band range line dragged vertically; untouched spectrum stays visible (their manual stresses showing *what is NOT processed*). |
| **Xfer OTT** | Three horizontal band rows, each with input level line + GR bars — band activity as stacked meters, no spectrum. |
| **Cableguys ShaperBox 3** | Crossover handles top-left; per-band editing; **sample-accurate oscilloscope with gray input trace + colored processed trace superimposed** — the pre/post-in-one-drawing idea. |
| **Arturia Dist COLDFIRE** | Central real-time viz **color-coded to match the chosen algorithm** per engine — the "lane tint = target identity" idea. |

### 7.2 Ours — three concepts (canvas, CPU-cheap, dramatic, param-reflecting)

The plumbing is already built: per-device bloom atomics (`PluginProcessor.h:701-702`,
`dstBloomViz_ :1580`) ride the single 60 Hz editor push (`PluginEditor.cpp:5552-5557`,
`window.__fxBloomRvb/Dly` — fb342 killed per-frame JS polls; the Splitter adds its lane peaks to
THIS push, never a new poll). `SpectrumAnalyzer.h:20` (4096-pt FFT, triple-buffered, ~47 fps) exists
if a concept needs a spectrum.

* **Concept A — THE BAND STACK (recommended core).** The card center draws 2–3 horizontal lane
  bars (the Serum segment idea rotated into our wide-card format). Each bar: fill = that lane's
  live peak (a new `splLaneViz_[3]` atomic trio, abs-peak per block — the exact `dstBlockWetPk`
  idiom `:7340-7341`, near-zero cost), tinted the owned device's accent (COLDFIRE's trick), dim
  when idle → blazing when fed (fb311 idle-dim/playing-bright law). Crossover boundary between
  bars = a draggable vertical handle with Hz readout (Multipass's direct manipulation, Serum's
  "direct manipulation" bullet) — dragging writes `SYN_SPL_P1/P2` through the normal relay (the
  4-point bind chain, no new mechanism). `Balance`/`Spread`/gain knobs visibly resize/re-tint the
  bars — every sound-changing param has a visible twin. Cost: ≤ 3 rects + 2 lines per frame.
* **Concept B — SPLIT SPECTRUM (the flex upgrade, L/H · L/M/H · Sub modes).** Decimate
  `SpectrumAnalyzer` magnitudes to ~64 columns; draw one mini-spectrum where each column is tinted
  by its lane (boundary moves live with the crossover glide); lane solo dims the others to 15 %.
  This is Multipass/Pro-MB in 140 px. Reuse the filter-analyzer column renderer (the
  `terrain-instrument-filter-live-analyzer` code) — recycle, don't rewrite. Only drawn when the
  rack pane is visible (card-guards law, fb342); no per-frame `shadowBlur` (session law ⑤).
* **Concept C — THE BUTTERFLY (M/S and L/R modes).** Center column = Mid energy (grows tall),
  side wings = Side energy (grow WIDE, mirrored) — a glanceable vectorscope abstraction. `Bass
  Mono` shades the wing roots to visualize "below here the wings are clipped"; `Rotate` visibly
  tilts the figure; correlation < 0 flashes the wings warm (the mono-danger tell). Three
  filled paths per frame.

Ship A as the always-on card core; B/C swap into the same canvas per mode (mode-appropriate viz =
the type-unique-controls law applied to drawing). All three read ONLY pushed floats — zero JS
polling, zero layout thrash, `requestAnimationFrame` with the no-early-return law (fb312/313).

---

## 8. Verify — the perceptual/measurement harness (gates before Max hears it)

Extend the per-family cert pattern (`dst_cert_*` harnesses; compile `clang++ -O2 -I shim -I Source`):

| Gate | Method | Pass |
|---|---|---|
| **G1 Reconstruction (frequency modes)** | log-sine sweep 20–20 k through Power ON, defaults, each slope | magnitude flat: slope 6 → residual < −120 dB (true null); 12/24/48 → ±0.1 dB (allpass-comp proof at BOTH crossovers in L/M/H) |
| **G2 M/S identity** | encode→decode, no trims | bit-exact null (< −140 dB) |
| **G3 Discriminators** | per-mode table §2 | every row's number met, else the mode dies |
| **G4 Lane isolation** | solo each lane, sweep | out-of-band slope matches d2 (−24 dB/oct at slope 24 etc.); L/R solo cross-talk < −120 dB |
| **G5 Mix comb** | Mix 25/50/75 %, sweep | NO notch at any crossover (the §6.5 law proven) — magnitude within lane-trim expectation ±0.1 dB |
| **G6 Click audit** | every knob swept 0→100 while playing; mode/slope/target switched mid-note | no transient > −60 dBFS above program (the fb283 phase-independent metrics; sample-diff BANNED as a *dramaticism* metric, still fine as a *click* metric) |
| **G7 Ownership math** | Low/High, High→Distortion @ known drive, compare vs manual chain | lane-owned device output == device fed the lane signal directly (< −100 dB residual) |
| **G8 Default transparency** | Power OFF vs ON-at-defaults | OFF: byte-identical; ON: G1 numbers + zero latency (cross-correlation peak at lag 0) |
| **G9 Dramaticism** | Balance/Spread/Side Gain at 100 vs 50 | ≥ 6 dB band-energy or width-metric delta — obvious, per the extremity table |

---

## 9. Pitfalls — collected, each with its kill

1. **LR2 polarity flip forgotten** (slope 12): LP+HP notches at fc. Invert the HP leg at slope 12
   only. G1 catches it in one sweep.
2. **Un-matched dry in Mix** — the Bitwig lesson (§6.5). Never blend pre-split dry against
   post-split wet. G5.
3. **Missing allpass comp in L/M/H** (§3.4): dips around `Split Hi`. G1.
4. **Mono-sum collapse of `Slip`:** a 20 ms Haas lane combs hard when the mix is folded to mono
   (period 50 Hz — comb teeth through the whole spectrum). Not a bug — physics — but the `Mono`
   pill must make it auditionable, and the preset notes (§10) never ship Slip on center-critical
   material. Document in the manual card.
5. **Crossover zipper:** TPT is stable under per-sample retune (Simper), but the *sound* of a
   stepped knob is zipper — glide in log domain, 15 ms (§3.7). G6.
6. **Type/slope/target switches clicking:** every discrete switch = fade-down → swap → fade-up
   (~15 ms each way, the `hallSm_` idiom `:7313`); lane-target change also PARKS the released
   device (its env fades at its own insert — free, the power-fade path already exists). ADAA-class
   state hygiene applies to the filters: **reset crossover filter states on mode entry, seeded with
   the first input sample** (the `SubOsc.h:63-82` seeding law) or re-entry thumps.
7. **Denormals:** 8–16 SVF states idling on silence after note-off = textbook denormal grind.
   `ScopedNoDenormals` rides the block (already global); add the `DelayEngine.h` `flush()` idiom to
   every crossover/allpass state + `snapToZero()` (JUCE provides it) on the LR filters.
8. **DC in the Side path:** S of a mono signal is exact 0 — follower states in Punch/Tail and
   meters must not denormal-buzz on it (flush covers it); conversely a DC-offset program puts DC in
   M — the merge is DC-transparent by design, and the targeted Distortion already owns its DC
   blocker (`DISTORTION bible §4.1`); no new DC blocker in the splitter (params play their roles —
   it is a router, not a cleaner).
9. **Gain staging at −26 dBFS:** lane gains are relative; the ONLY absolute-level trap is Side
   Gain +12 on already-wide program → merge peaks +6 dB over unity — inside the fb264 limiter's
   comfort; still, G9 measures worst-case merge peak and the preset bank stays ≤ +6 dB total lift.
10. **State restore:** the lane strip is UI-heavy state — `fxrRestoreSplitter()` must read BACK
    every param including the three lane targets and `SOLO` (the fb294/fb319 read-back law,
    `index.html:7944-7972` is the copy-template); re-render surgically (fixed-positions law — a
    mode switch relabels in place, nothing moves).
11. **The perm interaction bug waiting to happen:** un-owning a device mid-note must re-insert it
    into the serial chain at its remembered drag slot WITHOUT a click — re-entry env starts at 0
    and fades in (the existing `dstEnv_` machinery does this for free if ownership routes through
    the same env).
12. **Two crossovers colliding:** clamp `Split Hi ≥ Split + 1 oct` at the param layer (not the UI
    layer only — automation can violate UI clamps).

---

## 10. Presets — 12 factory sketches (names Title-case, pragmatic)

| Name | Mode | The point | Key values |
|---|---|---|---|
| **Bass Stays Home** | Sub Split | the club master move | Cross 120 · Sub Mono 100 · Rumble 25 Hz · Body Width 120 % |
| **Dirty Up Top** | Low/High | multiband distortion 101 | Split 350 · High→Distortion (Tube, Drive 45) · Low Gain +1 dB |
| **Sub Grit Clean Air** | L/M/H | mid-only crunch | 120/2.5 k · Mid→Distortion (Stomp Box) · Low/High Thru |
| **Side Wash** | Mid/Side | the Serum demo, done better | Side→Reverb (Hall, Mix 60) · Bass Mono 150 · Mid dry |
| **Center Punch Wide Echo** | Mid/Side | dry center, echoing walls | Side→Delay (1/8D, Fdbk 35) · Side Gain +3 |
| **Mono Maker** | Mid/Side | the utility classic | Side Gain −60 (kill) — one-knob mono |
| **Vinyl Rules** | Mid/Side | the elliptical filter | Bass Mono 300 · Side Slope 12 · Side Tilt dark |
| **Haas Wide** | Left/Right | the widener, honest | Slip 12 ms · Bleed 20 · (preset note: check Mono pill) |
| **Swapped Stage** | Left/Right | ear-flip drama | Swap 100 · Rotate +15° |
| **Air Delay** | Low/High | highs-only echo sparkle | Split 2 k · High→Delay (1/16, 25 %) · High Width 160 % |
| **Big Bottom Bloom** | Sub Split | reverb that never muddies | Body→Reverb (Basin) · Sub Thru · Cross 100 |
| **Kill The Lows** | Low/High | performance filter-kill | Balance 100 (High only) · Split 250 — automate Balance |

*(+2 if Punch/Tail ships: **Snap Only** — Punch→Distortion, Tail Gain −12; **Ghost Tail** —
Tail→Reverb Shimmer, Punch dry.)*

Preset level audit (the Phase-G lesson — no quiet/hot outliers): every preset's merge output within
±3 dB of bypass on the reference chord before ship.

---

## 11. Open questions for Max

1. **Punch/Tail mode** — ship in v1, or hold for v1.5? It is the originality card (nothing in
   Serum 2 splits by transient), costs ~2 followers, but adds a 6th mode to voice and test.
2. **Lane strip on the FRONT card** — sign off the §5.4 mockup direction before any build
   (mockup-first law; it must be interactive + audible in Safari per fb296).
3. **Per-osc pills into the splitter** (v2): worth the fourth send bus + the six exclusion-sum
   edits? (§6.2 documents the exact cost.)
4. **Draggable splitter position** (join a 24-perm order) vs pinned-first (§6.3): pinned-first
   recommended; confirm.
5. ~~`Mono` pill: momentary or latching?~~ **RESOLVED — LATCHING**, by the state-persists hard
   rule ("a click stays HELD until the user turns it off" — a momentary pill is a control that
   turns itself off, which the law forbids). Bright active state so a forgotten latch cannot hide.
   Max holds a feel-veto, but the law decides the default.
6. ~~Sub Split default crossover 120 Hz or 100?~~ **RESOLVED — 120 Hz stands.** The club-mono
   correlation practice this bible already cites (§0) draws the line at ~120 Hz; 120 sits mid-range
   of the 100–150 practice band; OTT's 88.3 Hz low band is a *compressor* band split (band carving),
   not a mono line — the wrong tool to copy; and 100 Hz is one nudge away on the 30–250 log knob.
   Pure-voicing veto stays Max's.
7. **Does `Spread` at 0 hard-mono ALL lanes** (dramatic, mono-check-adjacent) or only the low lane
   (safer)? Bible says all (no playing safe); confirm the feel.

---

## 12. Hard-rule compliance checklist (Laws 1–10, walked)

1. **Bus reality (−26 dBFS):** no absolute thresholds exist in the device; all gains relative;
   future dynamics ideas pre-warned (§4). ✔
2. **Chassis fb275:** 2 back dropdowns (`Mode`, `Slope`) + 8 back knobs (per-mode relabels) + front
   3 + Mix + 2 pills; the lane strip is additive card furniture like the Distortion's curve display,
   not a chassis violation; 11 chassis params + auxiliaries mirroring `SYN_DST_*` grammar. ✔ (§5)
3. **Time params 4 bars→1/256:** no tempo-synced knob exists; `Slip` is 0–20 ms micro-time —
   rule acknowledged, not applicable (§3.7). ✔
4. **Mix 100 % = fully wet; switches never cut:** §6.5 phase-matched Mix; every discrete switch
   fade-swap-recovers (§9.6). ✔
5. **Params evolve 0→100, no dead knobs:** extremity table §4; per-mode relabels kill dead slots
   (L/R d2 relabel §5.3); crossover clamps prevent dead zones, taper laws §3.7. ✔
6. **Nothing free-runs / LOOP GAIN:** the splitter contains **zero feedback paths** — split, gain,
   crossfeed, and merge are strictly feed-forward; lane→device→merge cannot re-enter the splitter
   (single forward pass per sample, §6.3). Owned Delay/Reverb feedback stays inside those engines
   under their certified loop gains. Max stable loop gain: N/A by construction — stated, not
   assumed. All output is input-derived; silence in ⇒ silence out (no noise sources). ✔
7. **No clicks:** every continuous param glides (§3.7); Slip crossfades; switches fade;
   filter-state seeding on entry (§9.6). ✔
8. **CPU:** §13 — worst mode ≈ 6 biquad-equivalents/channel, no oversampling ever, no FFT in the
   audio path, viz rides existing pushes; Power OFF = zero DSP in circuit. ✔
9. **Audible ⇔ visible, dramatic:** Band Stack + per-lane peaks (idle dim / playing bright), every
   param has a drawn twin, mode-specific viz (§7.2). ✔
10. **Recycle:** §14 inventory — the crossover, both filter primitives, the smoothing idiom, the
    bloom/push plumbing, the analyzer, the dropdown/preset-menu/restore UI code, the insert-lambda
    grammar. New code = the split/merge lambdas, the lane strip, one filter wrapper. ✔

---

## 13. CPU budget

Per sample, stereo, worst legal patch (L/M/H @ slope 48, all trims active):
* 2 crossovers × LR8 (= 2× LR4) × 2 ch = 8 TPT 2nd-order sections + AP4 comp (2 sections) ×2 ch
  ≈ **~90 mul-adds/sample** ≈ 6 biquads/channel — on the order of ONE reverb early-reflection
  stage; at 48 kHz ≈ 4–5 MFLOPS: **< 0.3 % of one M-series core.** Typical patch (LR4, 1 crossover)
  is a third of that. M/S and L/R modes are near-free (a handful of mul-adds).
* No oversampling (§3.8), no allocation, no transcendentals in the loop (crossover retune uses one
  `tan` per glide step per filter — precompute via the existing `fastTanh`-family tables if
  profiling ever cares; it will not).
* Sleep discipline: Power OFF or env decayed ⇒ the lambdas early-out exactly like `applyDst`
  (`:7311` pattern) — zero work in the default patch (the awake-head-sleep law, fb342).
* Viz: 3 atomics/block + existing 60 Hz push; canvas ≤ 60 fps only while the rack is visible
  (card guards). No `Quality` tiers needed — the device is flat-cheap; `Slope` is the only cost
  axis and 48 is still trivial.

---

## 14. Recycle inventory (verified by reading, with addresses)

| Reuse | Where | For |
|---|---|---|
| `juce::dsp::LinkwitzRileyFilter` (TPT LR4, dual-out `processSample`, `allpass` type, `snapToZero`) | `_tools/JUCE/modules/juce_dsp/processors/juce_LinkwitzRileyFilter.h:121-131` | slope 24/48 crossovers + §3.4 comp allpass |
| `TPTOnePole` | `TerrainFilters.h:83` | slope 6 complementary split; Bass-Mono side HP @ 6 dB; Bleed Tone LP |
| `SvfMultimode` (Simper TPT SVF, LP+HP taps) | `TerrainFilters.h:317` | slope 12 (Q = 0.5 LR2 via `res01 = 0`, HP inverted — §3.3); Side/Mid Tilt shelves via existing taps |
| `DCBlocker` | `TerrainFilters.h:69` | only if Punch/Tail followers ever need it (likely not) |
| smoothing idiom `xC += (xT−xC)·smth` + `flush()` | `DelayEngine.h` | every param glide + denormal flush |
| insert-lambda grammar + env fade + `hallSm_` | `PluginProcessor.cpp:7137/7309/7345` | `applySplSplit`/`applySplMerge` + power/target fades |
| fb305 exclusion expression (copy verbatim, three-bus) | `PluginProcessor.cpp:7159/7326/7358` | the splitter's input tap (§6.2 — copied, never extended in v1) |
| bloom atomic + 60 Hz push | `PluginProcessor.h:701-702`, `PluginEditor.cpp:5552-5557` | `splLaneViz_[3]` lane peaks |
| `SpectrumAnalyzer` (4096 FFT, triple-buffered, 47 fps) | `SpectrumAnalyzer.h:20-98` | Concept B split spectrum |
| `DEVS` device-object grammar + `fxrRestore*` + perm table | `index.html:7479`, `:7944-7988`, `:8323` | card, restore, order interplay |
| `engine-select` dropdown overlay + `.pmenu` preset menu | UI idioms (CLAUDE.md recycle law) | lane-target dropdowns + device presets |
| filter live-analyzer column renderer | `terrain-instrument-filter-live-analyzer` | Concept B drawing |
| per-family cert harness skeletons | scratchpad `dst_cert_*` (Phase G) | §8 gates G1–G9 |

**Build order (distortion-bible style):** ① engine header `SplitterEngine.h` (split/merge/trims, all
modes, G1–G5 offline) → ② chain integration WITHOUT targeting (Thru/Mute lanes only) + G8 → ③ lane
targeting for the three devices + G7 → ④ card UI (mockup → sign-off → wire) + restore → ⑤ presets +
G9 + Max's ears.

---

## 15. Sources

* Serum 2 "What's New" official PDF (splitter modules p. 11–13, defaults 210/1000 Hz, lane badges) — https://static.xferrecords.com/Serum%202%20What's%20New.pdf
* Splice — Serum 2 advanced features (splitter/bus descriptions) — https://splice.com/blog/serum-2-advanced-features/
* Outerverse — 10 Serum 2 Secrets (splitter workflow) — https://outerverse.fm/blogs/tutorials/10-serum-2-secrets
* "If You're Not Using the Low-High Splitter in Serum 2…" (walkthrough) — https://www.youtube.com/watch?v=ePZ-hsnyyQQ
* Kilohearts Multipass docs (LR crossovers, lane controls, drag bars) — https://kilohearts.com/docs/multipass
* Kilohearts Snap Heap docs (lane routing, parallel links) — https://kilohearts.com/docs/snap_heap
* KVR — N-band Linkwitz-Riley crossovers (allpass placement law) — https://www.kvraudio.com/forum/viewtopic.php?t=479651
* KVR — Bitwig Multiband FX-3 mix-at-zero (dev statement on phase-matched dry) — https://www.kvraudio.com/forum/viewtopic.php?t=446003
* KVR — Most Transparent Multiband Splitter (null behavior by slope) — https://www.kvraudio.com/forum/viewtopic.php?t=570992
* Rane Note 160 — Linkwitz-Riley Crossovers: A Primer — https://www.ranecommercial.com/legacy/note160.html
* Wikipedia — Linkwitz–Riley filter (Butterworth-squared, −6 dB @ fc) — https://en.wikipedia.org/wiki/Linkwitz%E2%80%93Riley_filter
* musicdsp.org — 4th-order Linkwitz-Riley implementation — https://www.musicdsp.org/en/latest/Filters/266-4th-order-linkwitz-riley-filters.html
* Cytomic (Andrew Simper) — SvfLinearTrapOptimised2 + technical papers — https://cytomic.com/technical-papers/ · https://www.cytomic.com/files/dsp/SvfLinearTrapOptimised.pdf
* FabFilter Pro-MB manual — Display and workflow (band handles, snap/split) — https://www.fabfilter.com/help/pro-mb/using/display
* FabFilter Saturn 2 manual (6/12/24/48 slopes, linear-phase tradeoff) — https://www.fabfilter.com/downloads/pdf/help/ffsaturn2-manual.pdf
* Arturia Dist COLDFIRE (routing modes incl. Mid/Side + Band Split, color-coded viz) — https://www.arturia.com/products/software-effects/dist-coldfire/overview
* KVR — Dist COLDFIRE product page + release news (routing list "Serial, Parallel, Stereo, Mid/Side, Band Split"; released 30 Aug 2022) — https://www.kvraudio.com/product/dist-coldfire-by-arturia
* Xfer Serum 2 User Guide pp. 177–182 (Splitter L/H · L/M/H · MS chapters: per-lane racks, SPLIT FREQ crossovers, per-lane bypass, LEVEL — corroborates the What's-New reading) — https://xferrecords.com/support
* Ableton Live manual — Audio Effect Reference (racks, Multiband Dynamics) — https://www.ableton.com/en/manual/live-audio-effect-reference/
* Bitwig userguide — container devices — https://www.bitwig.com/userguide/latest/container/
* Cableguys ShaperBox 3 (3-band per effect, dual-trace scopes) — https://www.cableguys.com/shaperbox
* SOS — "How do you make only low frequencies mono?" (M/S side-HP mechanism) — https://www.soundonsound.com/sound-advice/q-how-do-you-make-only-low-frequencies-mono
* Flotown Mastering — Center That Sub (club correlation practice) — https://flotownmastering.com/blog/center-that-sub
* Gearspace — Mid/Side: a Primer (encode/decode conventions) — https://gearspace.com/board/so-much-gear-so-little-time/478912-mid-side-primer-encoding-decoding.html
* music-dsp list — 24 dB/oct splitter thread — https://music-dsp.music.columbia.narkive.com/Eg7mDldt/24db-oct-splitter
* In-tree: `juce_LinkwitzRileyFilter.h/.cpp`, `TerrainFilters.h`, `PluginProcessor.cpp` fb341 chain, `index.html` DEVS/restore, `DISTORTION-BUILD-BIBLE.md` (§4.4/4.5 latency & exclusion laws), `REVERB-BUILD-BIBLE.md`.
