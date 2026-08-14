# Terrain Instrument — OTT Build Bible

*A dedicated 3-band upward/downward compressor — Max's "makes everything sound incredible" machine
and the house route to Serum-style top-end AIR.*

Research + spec, 2026-08-14. **Adversarially audited + patched 2026-08-14** (see the ⚠️ AUDIT marks
inline). Written to the DISTORTION-BUILD-BIBLE.md standard: a builder must be able to implement the
device from this file alone, without re-research. Every threshold in this file is stated **relative to
the measured −26 dBFS FX-bus program level** (house law 1); every literature number that assumes a
0 dBFS program has already been translated.

⚠️ **Not "the 4th FX device."** Three devices ship today (Reverb / Delay / Distortion). The overnight
sweep produced 16 bibles and *three* of them (Compressor, Splitter, OTT) each opened by calling
themselves "the 4th" — a sweep artifact, corrected here. `FX-RACK-RESEARCH-INDEX.md` §2 puts OTT late
in the build order (after Granular · Tape · Moog-Delay port · Filter · multi-instance, then
Flanger → Phaser → Utility → Chorus → Bode → Widen → **Compressor → OTT**). Build accordingly: the
Compressor lands first and this bible is written to sit *beside* it, not before it.

---

## 0. Scope decision — what this device IS, and Max's question answered

**YES: OTT = multiband upward + downward compression.** Three bands (low / mid / high, split at
~88 Hz and ~2.5 kHz in the canonical version), and *each band* runs **two gain computers at once**:

* **Downward** — level above an upper threshold is pushed *down* (classic compression, at brutally
  high ratios: 7:1 → ∞:1).
* **Upward** — level below a lower threshold is pushed *up* (the rare one: quiet detail, decays,
  reverb wash, inter-note air are *amplified toward* the threshold).

The two multiply. The band's dynamic range is squeezed from **both ends into a narrow window**, per
frequency band, with fast per-band ballistics. That is the entire trick — and it is why OTT sounds
like nothing else in the rack:

* It is **not the Splitter** (Serum 2's Splitter L/M/H is a *routing* module that hosts other FX per
  band; OTT is a *fixed, opinionated dynamics instrument*). The two are related the way a mixing desk
  is related to a guitar pedal. Terrain **is** shipping a Splitter — `SPLITTER-BUILD-BIBLE.md` (same
  sweep) firmly recommends one device with a Mode dropdown — and OTT still deserves to exist: Serum 2
  itself ships BOTH (Splitter modules *and* the Compressor's MULTIBAND mode).
* ⚠️ **AUDIT — it is also not the Compressor, and that boundary is already locked elsewhere.**
  `COMPRESSOR-BUILD-BIBLE.md` §0 states the OTT boundary as a decided proposal: **the Compressor
  device owns everything single-band — including single-band upward+downward levelling, its `Squeeze`
  type** (whose in-tree precedent is the shipped SHAPER-family `Squash` P8, a 3 ms/80 ms ~20:1
  leveller at `DistortionEngine.h:2259-2270`) — while **this device owns 3-band up/down with per-band
  thresholds/ballistics**, because that needs a crossover viz and per-band UI that would blow the
  Compressor's 11-param chassis. The original draft of this bible never mentioned the Compressor at
  all. Read that §0 before building either; the two rosters must not both grow an "OTT-ish" type.
* It is **not the Distortion** — although at extreme Time settings it becomes one (§14 preset 13, `Envelope Eater`), its identity is *level-dependent gain*, not a static curve. The Family-Tell measurement from
  the distortion bible applies in reverse: OTT's added-harmonic content is near zero at normal Time,
  while its **level-dependence metric is the largest of any device in the rack**.
* It deserves to be **its own dedicated device** because (a) it is Max's most-used effect and a genre
  keystone (future bass / dubstep / hyperpop are unimaginable without it), (b) the competitor ships it
  twice (Serum 1's famed multiband Compressor mode + Serum 2's expanded version), and (c) it is the
  single cheapest way to buy the "expensive Serum top-end" — §2.4 explains the air mechanism with
  numbers.

**Lineage locked:** Ableton Live *Multiband Dynamics* device (Live 8, 2009 — *year/version
unverified, do not quote in copy*) → its factory preset **"OTT"**
(Over The Top) → Steve Duda freezes that preset into the free **Xfer OTT** plugin (2013) → a genre is
born → Vital clones it as its Compressor (open source — we mined its exact constants, §1.3) → Serum 2
carries it forward as the Compressor's MULTIBAND mode. Fitting: the effect Max wants most is the one
his direct competitor's author gave the world for free.

### The scope
ONE device. **8 Types** (calibrated voicings/topologies of the same engine — not 8 engines), a
**Stereo** selector (Linked / Dual / Mid-Side), **3 front hero knobs + Mix** + 2 pills, 8 back knobs
(the fb275 grammar exactly: 2 dropdowns + 8 back knobs 4×2; 3 + Mix on the front). Zero latency,
no oversampling, no lookahead. Feed-forward only — no feedback loop exists anywhere in the device
(loop-gain law satisfied by construction, §10).

---

## 1. History and circuits — the lineage that defined the effect

### 1.1 Upward compression before OTT
Downward compression is 1930s broadcast tech; **upward** compression is historically rare because
analog implementations are treacherous (boosting toward a threshold means boosting noise floors — the
same trap we must engineer around in §4.6). It lived in broadcast leveling (dbx "over easy" era
expanders run backwards), in hearing-aid DSP (wide-dynamic-range compression, the earliest mass-market
upward compressors), and as studio lore "parallel / New York compression" — mixing a crushed copy
under the dry, which *approximates* upward compression: quiet parts gain more crushed-copy level
relative to their dry level than loud parts. OTT's Depth control is literally this parallel blend.

### 1.2 Ableton Multiband Dynamics and the OTT preset
Live's Multiband Dynamics (the device the preset ships on): 3 bands, and per band an **Above** block
(threshold + ratio) and a **Below** block (threshold + ratio), each drag-editable; T (Time) scales all
attack/release settings together; a global **Amount** knob scales all ratios; RMS/Peak detection
switch; per-band output gain, solo, bypass. Below-block ratios are entered as reciprocals (type `.5`
for 1:2.00) — i.e. Live parameterizes the **slope**, exactly what our math does (§4.4). The **OTT
preset** sets the crossovers at **88.3 Hz and 2.50 kHz**, heavy Above ratios (high band effectively
∞:1 — brick-wall; mid/low around 66.7:1 per the common teardown), Below ≈ 4:1 upward on all bands,
and thresholds "set very low" so on any normal program *both* sides are always working.

### 1.3 Xfer OTT (Steve Duda, 2013, free) — the reference unit
The preset frozen into a plugin, with the parameter set that defines the genre workflow:

| Control | What it does | Numbers |
|---|---|---|
| **Depth** | Global parallel amount — dry/wet of the whole effect | 0–100 %, default 100 % |
| **Time** | ONE knob scaling attack AND release of all 6 gain computers | percentage scale; < 100 % = faster |
| **In Gain** | Drive the program into the fixed thresholds — *this is the real "amount of compression" knob* | ±dB |
| **Out Gain** | Post trim | ±dB |
| **Band gains** | Drag the three band slabs left/right — per-band output trim, the built-in 3-band EQ | ±dB per band |
| **Upwd / Dnwd** | Two global percentages scaling the upward vs downward amounts independently | 0–~200 %, default 100 % — *range + "added in the 1.3x line" are UNVERIFIED; the controls' existence is confirmed (KVR product page)* |
| Crossovers | **FIXED: 88.3 Hz / 2.50 kHz** (the Ableton preset's values; adjustable only in the Ableton original) | ✅ verified — EDMProd teardown: "Low-Mid crossover is set at 88.3Hz", mid→high 2.5 kHz |

✅ **Verified 2026-08-14** against the EDMProd teardown: Above ratios **∞:1 on the highs**
("brickwall limiting") and **1:66.7 on mids and lows**; **Below ratio 4:1 on all three bands**;
"every threshold is also set extremely low". Same source gives Xfer's own gain staging —
**+5.2 dB input gain per band; output +10.3 dB low, +5.7 dB mid, +10.3 dB high** — which is the
independent corroboration of our makeup table in §4.5.

Reported latency: **2 samples** — ✅ verified, but note the provenance: it is a *KVR user review*
(Vospi, Nov 2017: "latency of 2 samples (so no parallel processing without extra gimmicks
employed)"), not an Xfer spec. Enough to break naive parallel routing; ours will be **0**, §4.7. The UI is the famous "squeezing jaws" band display (§6.1). Duda himself, asked whether
Serum's multiband compressor = OTT: *"they're technically quite the same, but in practice there are
differences (crossover points, upwards control)"* — ✅ quote verified verbatim against the Xfer forum
thread during the audit — which is our licence to calibrate rather than clone.

### 1.4 Vital's Compressor — the open-source ground truth we mined
Matt Tytel's Vital (GPL) ships an OTT-style 3-band up/down compressor as its Compressor. We read the
source (`vital/src/synthesis/effects/compressor.cpp|h`, `synth_parameters.cpp`) and extracted **every
constant**. This is the only OTT-class implementation whose exact math is public, and it is the
skeleton our engine follows (translated to the Terrain bus in §4).
✅ **Every number in the block below was re-fetched from the GPL source and verified line-for-line
during the 2026-08-14 audit** — `kRmsTime 0.025f · kMaxExpandMult 32.0f · kLowAttackMs 2.8f ·
kBandAttackMs 1.4f · kHighAttackMs 0.7f · kLowReleaseMs 40.0f · kBandReleaseMs 28.0f ·
kHighReleaseMs 15.0f · kMinSampleEnvelope 5.0f · kMinGain −30 / kMaxGain 30`, filters
`low_band_filter_(120.0f)` / `band_high_filter_(2500.0f)`, and every `compressor_*` default in
`synth_parameters.cpp` (thresholds −28/−25/−30 upper, −35/−36/−35 lower, range −80…0; upper ratios
0.9/0.857/1.0; lower ratios 0.8/0.8/0.8, range −1…1; gains 16.3/11.7/16.3, range ±30; mix 1.0).
The only line below still **unverified** is the `exp(8t−4)` knob law (it is consistent with the
attack/release default of 0.5 ⇒ ×1.0, but the mapping itself lives in `futils`/the GUI layer):

```
Crossovers:  Linkwitz–Riley @ 120 Hz and 2500 Hz  (fixed)
Ballistics:  low  2.8 ms attack / 40 ms release      (base, per band)
             mid  1.4 ms attack / 28 ms release
             high 0.7 ms attack / 15 ms release
Knob law:    attack/release knobs t∈[0,1] scale the base by exp(8t−4)  → ×0.018 … ×54.6
Follower:    running mean of x², branching one-pole:
               N = max(exp-scaled time in samples, 5)          // kMinSampleEnvelope = 5
               env = (x² + env·N) / (N + 1)                    // attack N when rising, release N when falling
Downward:    env clamped ≥ T_dn²;  g_dn = (T_dn²/env)^(0.5·r_dn),   r_dn ∈ [0,1]
Upward:      env clamped ≤ T_up²;  g_up = (T_up²/env)^(0.5·r_up),   r_up ∈ [−1,1]
Total:       y = clamp(g_dn·g_up, 0, 32) · x                   // kMaxExpandMult = 32 → boost ≤ +30.1 dB
Defaults:    T_dn  = −28 / −25 / −30 dB   (low/mid/high)   range −80…0
             T_up  = −35 / −36 / −35 dB
             r_dn  = 0.9 / 0.857 / 1.0    ⇒ ratios 10:1 / 7:1 / ∞:1   ← the high band is a BRICK WALL
             r_up  = 0.8 all bands        ⇒ upward 5:1 (slope 0.2)
             band makeup = +16.3 / +11.7 / +16.3 dB (!!)      // the compression is THAT deep
             mix 1.0 · output gain ±30 dB · RMS window 25 ms (metering path)
```

Read that defaults row twice: Vital's calibration crushes so hard it needs **+16 dB of per-band
makeup** to come back to unity. That is what "OTT sound" means in numbers, and why a timidly-ported
version (thresholds never reached — the classic §11 failure) sounds like *nothing at all*.

### 1.5 Serum 2's Compressor (the competitor's version — chassis + viz target)
From the official Serum 2 User Guide (*page numbers p.162–164 are **UNVERIFIED** — the sweep could
not reliably fetch the manual, `FX-RACK-RESEARCH-INDEX.md` §6; the param roster below is
cross-confirmed by MusicRadar's Serum FX guide and the Xfer forum thread*), the COMPRESSOR module: **MODE** Single/Multiband ·
**THRESH** (0 → 0 dB … 100 % → −120 dB) · **RATIO** (to "Limit", which swaps in a true-peak limiter
DSP with 0–10 ms attack and 0–36 dB makeup, optional reported latency) · **ATTACK · RELEASE · GAIN**
(~30 dB makeup) · and in MULTIBAND: **X-LOW / X-HIGH** (crossovers), **BELOW** (the upward ratio —
one knob for the whole idea), **H / M / L** per-band gains · **MIX · LEVEL**. The manual calls
multiband mode "an extreme setting" — an upwards/downwards compressor, band params modulatable via
the mod matrix ("useful for side-chaining just the low end out of the way of a kick"). Serum 2's FX
all have **direct-manipulation graphical displays** (What's New, p.10 — *page cite unverified*): the compressor draws band
lanes you can grab — crossovers and band gains are dragged *on the display*. That is the bar for our
card viz (§6).

Our differentiation vs Serum 2's one-BELOW-knob version: 8 calibrated Types, independent per-band
up/down architecture, the **Air** macro, Mid-Side mode, phase-matched Mix (§4.3), and a hero
visualizer instead of a utility one.

### 1.6 The rest of the field (what each adds — competitive scan)
* **Slate MO-TT** — the commercial OTT clone: per-band Amounts + master Amount, adjustable
  crossovers, RMS/Peak, soft/hard knee, 3 Timing Styles (Classic / Smooth / **Smack** — transient-
  preserving: our `Punch` pill, §5), stereo link 0–200 %, sidechain + clip stage.
* **Minimal Audio Squash** — 4 bands, **"phase-coherent"** blending (they engineered away the
  crossover-phase comb this bible kills in §4.3), XY-pad macro control.
* **Sixth Sample Cramit** — OTT + 7 distortion algos around the compression; paid version adds
  **linear-phase** crossovers (same phase problem, the expensive fix we reject in §4.3).
* **Discreet Signals 8TT** — up to 8 bands, draw-your-own; **Audio Damage Evil Otto** — waveform view
  + draggable thresholds; **Polarity-MD** — linear phase + per-band clippers + LUFS metering;
  **Kilohearts Multipass** — the Splitter-style host (5 bands, snapin chains per band) that proves
  the routing-vs-instrument distinction of §0.
* **Melda MDynamicsMB** — the maximalist multiband dynamics lab (custom transfer shapes) — the
  anti-model: we ship opinion, not a lab.
* **Arturia FX Collection** (Max's requested deep reference — dynamics end): **Bus FORCE** = 3
  parallel paths (Dry/Comp/Sat) with Pultec-style EQ, SEM-lineage filter, VCA comp with **dual
  RMS+peak detection**, 4 saturation flavors + clip stage, 36+ routings — the lesson we take is
  *paths, calibration, and sweet-spot presets sell "analog confidence"*. **Comp DIODE-609** (Neve
  33609 diode-bridge): stepped controls, dual VU needles, and — steal this — **click the VU and it
  flips to a digital strip drawing the waveform with the gain-reduction trace overlaid** (§6.2).
  **Comp FET-76 / TUBE-STA** complete their range; none does upward — Arturia has no OTT, which is a
  selling point for ours.

---

## 2. The physics — six gain computers and where the AIR comes from

### 2.1 The topology (locked)

```
            ┌────────────────────────────────────────── wet path ─────────────────────────────┐
            │            ┌─ LOW  ──► AP2(f_hi) ──► [dn ▼ · up ▲] ──► ×g_low  ─┐               │
 in ─► LR4 split(f_lo) ──┤                                                    Σ ─► trim ─► ─┐ │
            │            └─ REST ─► LR4 split(f_hi) ─┬─ MID  ─► [dn·up] ×g_mid┘             │ │
            │                                        └─ HIGH ─► [dn·up] ×g_hi ┘             ▼ │
 in ─► AP2(f_lo) ─► AP2(f_hi) ──────────────────────────────── phase-matched dry ──► (1−m)·+m·─► out
```

2 stereo LR4 splits + 1 band-alignment allpass + 2 dry-alignment allpasses + **6 envelope followers**
(3 bands × {down, up}) + 6 `pow`s. That is the whole machine. **It is cheap** — §9 budgets it at well
under half the Delay device — because unlike reverb/distortion there is no memory structure and no
oversampling: just filters and per-sample gain arithmetic.

### 2.2 The star: the upward gain computer, precisely
Let `L` = the band's envelope level in dB (RMS-ish, from the x² follower) and `T_up` the lower
threshold, upward slope `s_up ∈ [0,1)` (s=0.8 ⇒ "5:1 upward"):

```
gainUp_dB = s_up · max(0, T_up − L)          // below threshold: BOOST toward it
gainUp_dB = min(gainUp_dB, +24 dB)           // hard cap (Vital caps the total mult at 32× = +30.1)
```

In Vital's mean-square domain this is exactly `g = (T²/env)^(0.5·s)` with `env` clamped ≤ T². Note
what this **is**: an automatic fader that rides *up* whenever the band gets quiet — decays, releases,
reverb tails, breaths between notes — at up to +24 dB. And note what it **threatens**: with no input,
`L → −∞` and the computer pins at the cap, amplifying the noise floor. §4.6's floor gate is therefore
a *stability requirement of the design*, not polish — it is also precisely house law 6 (sound dies
with the note).

### 2.3 The static picture — both computers at once
Per band, in dB in/out (slopes: down `1−r_dn`, up `1−s_up` toward the floor):

```
 out │            ____________ ← ceiling: T_dn + (L−T_dn)(1−r_dn)   (r_dn=1 ⇒ flat = brickwall)
     │        ___/
     │    ___/   ← unity 1:1 only in the narrow T_up..T_dn window
     │ __/
     │/    ← below T_up the curve is LIFTED: out = L + s_up(T_up−L)  → slope (1−s_up) = 0.2
     └────────────────────────── in
```

The audible identity: **the band's output level is nearly constant** regardless of input level — a
"sheet" of that band, dense, forward, loud-feeling at equal RMS. Multiply across three bands with
*different* thresholds and ballistics and you get the OTT sound: every band always present, every
transient snapped to the ceiling, every tail resurrected.

### 2.4 🔑 WHY THE HIGH BAND MAKES "AIR" — with numbers
A −26 dBFS saw program (the measured Terrain bus single-note level, `PluginProcessor.cpp:46`) puts
very little *absolute* energy above 2.5 kHz: a 110 Hz saw's partials above 2.5 kHz start at harmonic
23, each ≥ 27 dB below the fundamental; band RMS lands around **−45…−50 dBFS** while the full-band
program reads −26. On darker material (filtered pads) the high band sits at −60…−80 dBFS. Now put an
upward computer there with the §4.5 anchors — `T_dn(high) = −56 dBFS`, `T_up(high) = −66 dBFS`,
slope 0.8:

⚠️ **AUDIT — these were wrong in the first draft** (it quoted `T_up(high) = −56` / `T_dn(high) = −50`,
which contradicts the §4.5 calibration table by 10 and 6 dB and also inverts nothing else in the file).
Corrected to the §4.5 anchors. §4.5 is the single source of truth for thresholds; if you re-tune it,
re-derive this paragraph.

* bright saw, high band −48: above T_up → no lift, but the ∞:1 **downward ceiling** at `T_dn(high) =
  −56` grabs it by 8 dB → HF density pinned constant = "sheen".
* darker pad, high band −80: lift = 0.8·(−66 −(−80)) = **+11.2 dB of pure top**, applied only while
  the band is quiet, released at 15 ms — the shimmer breathes *into* every gap. (At −70 the lift is
  0.8·4 = +3.2 dB; the effect grows *fast* as the source darkens — that is the whole mechanism, and
  it is why the §8 `air` gate is written on a genuinely dark pad probe, not a filtered saw.)

So the air is not an EQ shelf: it is a **program-dependent shelf whose gain grows as the source gets
darker**, plus a brick-wall ceiling that keeps HF *always at the same level*. An EQ can only trade
one for the other; that is the measurable §3 discriminator between our `Air` knob and the `Treble`
band trim, and it is why Max hears OTT as "expensive top-end" on everything. The fast high-band
ballistics (0.7/15 ms) are what make it granular enough to read as *texture* rather than pumping.

### 2.5 The FAMILY TELL — the measurement that proves it's real
On the two-tone level-step probe (house perceptual-harness grammar): step the input −20 dB for
500 ms and back. A static device (EQ, distortion at fixed drive) shows the output stepping the full
20 dB. A correct OTT at Classic defaults shows the band outputs stepping **≤ 6 dB** (downward slope
0…0.14 above, upward slope 0.2 below) with the documented attack/release exponentials. If the
level-dependence metric reads flat, you shipped an EQ wearing an OTT costume — the exact class of
failure the distortion bible caught with its "static curve for now" trap.

---

## 3. Types — 8, each night-and-day, each with a measured discriminator

One engine, 8 calibrations/topologies for the **Type** dropdown. Per the choice-param cardinality law
(fb342/fb345): **ship all 8 enum slots day one** — presets store choice indices; adding entries later
reshuffles the world.

⚠️ **AUDIT — two naming defects fixed in this pass (the no-doubles law is PERMANENT):**
① The front hero knob was drafted as **`Squash`**, which is *already a shipped, visible knob label in
this plugin* — the SHAPER family's P8 (`DistortionEngine.h:314` and `:2259-2270`, rendered at
`ui/public/index.html:7577`). Renamed to **`Amount`** throughout (also Slate MO-TT's word for exactly
this control, and the Xfer "Depth" alternative was rejected because it collides semantically with
Mix). `SYN_OTT_SQUASH` → **`SYN_OTT_AMOUNT`**.
② Type 4 was drafted as **`Air`**, the same name as the front hero knob `Air` — a doubles violation
inside one device, and ironic since §5.2 P8 cites the no-doubles law to justify `Treble` vs `Air`.
Type 4 is now **`Sheen`**. The *knob* keeps the name `Air` (it is Max's mandate word).
Also watch: `Bass-Safe` (Type 5) vs `Bass` (knob P6) is a near-collision — acceptable as written, but
do not shorten the Type to "Bass" at UI time.

Base tables (the "Classic" column) are the Vital constants translated to the −26 dBFS bus (§4.5).
`Δ` rows say what the Type changes *relative to Classic*. All Types share the §4 engine; a Type is a
row of constants + at most one topology flag — no per-Type code forks beyond that.

| # | Type | The idea (lineage) | Key deltas from Classic | 🔬 Discriminator (harness-measurable, night-and-day) |
|---|---|---|---|---|
| 1 | **Classic** | Xfer OTT / Vital calibration. THE preset sound | Crossovers **88.3 Hz / 2.5 kHz** (Xfer's, not Vital's 120); ballistics 2.8/40 · 1.4/28 · 0.7/15 ms; slopes dn 0.9/0.857/1.0, up 0.8 | Reference row for all deltas. Level-step: band outputs move ≤ 6 dB per 20 dB input step; high-band ceiling flat to ±0.5 dB |
| 2 | **Smooth** | Ableton-with-taste / MO-TT "Smooth" style | Detection through a 25 ms RMS pre-average (Vital's kRmsTime) before the branch follower; ballistics ×4 slower; soft knee 12 dB (Reiss quadratic, §4.4); slopes ×0.7 | Gain-ripple THD on a 100 Hz sine at full Amount: Classic > 3 % (env tracks cycles), Smooth **< 0.3 %**; attack overshoot absent on the step probe |
| 3 | **Heavy** | MO-TT cranked / "OTT on OTT" stacking in one type | All dn slopes = 1.0 (∞:1 everywhere), thresholds 6 dB deeper, up slope 0.9, plus per-band cubic soft-clip at T_dn + 6 dB | Output crest factor on the chord probe collapses to **≤ 3 dB** (Classic ≈ 6–8); LRA → ~0; added THD from the band clippers measurable (H3 > −40 dBc) |
| 4 | **Sheen** | Max's mandate — the Serum-top-end type (was drafted "Air"; renamed, see the doubles note above) | X-High moved to **1.8 kHz** (more spectrum counts as "high"); high band: T_up raised 8 dB (closer to program), up slope 0.9, ballistics 0.35/8 ms, makeup +4 dB; mid dn slope +0.05 | Long-term spectrum on the dark-pad probe: **≥ +8 dB @ 8–12 kHz vs Classic**, while a matched static shelf fails the level-step test of §2.5 |
| 5 | **Bass-Safe** | Every mix engineer's OTT complaint, fixed | Low band: **upward OFF**, dn slope 0.75, ballistics 10/120 ms, detection mono-summed below f_lo; low T_dn raised 4 dB | 40–60 Hz two-tone IMD: Classic smears (env ripples at the beat rate), Bass-Safe **> 20 dB less** IMD sidebands; low-band GR trace moves < 2 dB where Classic pumps 8+ |
| 6 | **Bloom** | Pure upward — parallel-comp resurrection, no squash | All dn slopes = 0 (downward OFF); up slope 0.85; up cap raised to +24 dB; ballistics ×2 slower; makeup 0 | GR trace **never negative** (gain ≥ 0 dB everywhere); transient crest preserved within 1 dB while the tail RMS rises ≥ 10 dB on the pluck-decay probe |
| 7 | **Duo** | 2-band variant (body/sparkle) — kHs-style simplicity | ONE split at **650 Hz** (Low Cross knob re-ranges 150 Hz–2 kHz; High Cross becomes the second-band tilt, see §5 note); slopes dn 0.85/1.0, up 0.8/0.85 | Phase response shows **one** allpass rotation (vs two) — measure as **group-delay peak count = 1** on a swept-sine probe; Bass/Treble knobs act on 2 bands (Mids becomes tilt). ⚠️ AUDIT: the draft used "crossover notch count in the Mix=50 comb test = 1" as the discriminator — that is impossible, §4.3's phase-matched dry means the `comb` test shows **zero** notches at *every* Type. Group delay is the honest measurement |
| 8 | **Quad** | 8TT direction — adds a "brilliance" 4th band | Splits at 88.3 Hz / 1 kHz / **5.5 kHz**; 4th band ballistics 0.3/8 ms, dn ∞:1, up 0.85; band-gain knobs map L/M/(H=3+4) | 4 independent GR traces (viz shows 4 lanes); **group-delay peak count = 3** (not "notch count" — see the Duo row); 5.5 k+ band alone passes the §2.5 level-step while 2.5–5.5 k is held by band 3 |

**Type-switch law (no clicks, law 7/4):** all Types share follower state per band; a Type change
glides constants over ~60 ms (the DistortionEngine ~15 ms idiom stretched — threshold jumps are
audible as pumps, so slower). **Duo/Quad change the tree**: run old + new trees in parallel for
30 ms and equal-power crossfade outputs, then retire the old (the fb345 deferred-fade + re-seat law;
followers of the incoming tree are pre-seeded from the nearest band's env so the fade lands hot, not
from zero — the Xfmr pk-50.4 lesson).

**Cuts considered and made:** a "Limit" type (Serum's ratio-max limiter swap) — cut: different DSP
circuit, belongs in a future Utility/Limiter device; a lookahead type — cut: violates the zero-latency
decision (§4.7); a per-band-editable "Lab" type — cut: that's Melda's identity, not ours, and it
breaks the 8-knob chassis.

---

## 4. DSP core — algorithms, math, param laws, stability, oversampling verdict

### 4.1 Crossovers — Linkwitz–Riley 4th order on the in-tree TPT SVF
**LR4** = two cascaded 2nd-order Butterworth (Q = 1/√2) sections. Chosen because LR4's LP and HP
outputs are **in phase with each other** at all frequencies and sum to a flat-magnitude 2nd-order
allpass — no polarity flip needed (LR2 needs one), −6 dB at fc each side, 24 dB/oct. This is the
industry-standard multiband split (Vital uses exactly this at 120/2500).

Realization: **reuse `TerrainFilters.h:317 SvfMultimode`** (Simper TPT SVF — already computes `lp =
v2` and `hp = v0 − k·v1 − v2` from one state in one tick). Build a 10-line `LR4Split` on its exact
coefficient math (`g = tan(π·fc/fs)`, `k = √2`, `a1..a3` as at `TerrainFilters.h:348-359`):

```
stage1: SVF(fc, Q=0.7071) → lp1, hp1        // one tick, both taps
stage2a: SVF(fc) on lp1  → LP4 (take lp)    // low output
stage2b: SVF(fc) on hp1  → HP4 (take hp)    // high output
⇒ 3 SVF ticks per split per channel; LP4 + HP4 = AP2_butterworth(fc) exactly
```

**Band alignment (mandatory):** the LOW band bypasses split-2, so it must pass through
`AP2(f_hi)` — the 2nd-order allpass split-2 imposes on mid+high — or recombination combs around
f_hi. AP2 from the same SVF: `ap = v0 − 2k·v1` (with the struct's normalized BP tap `bpOut = k·v1`,
that is `ap = in − 2·bpOut`). One extra SVF tick per channel. **Quad** needs two alignment APs
(low gets AP2(f_mid)·AP2(f_hi), mid gets AP2(f_hi)); **Duo** needs none.

**Crossover knobs glide** (law 7): fc targets glide at ~30 ms; TPT SVF coefficient recompute per
block edge with per-sample state continuity is click-free (the shipped filter system's law — no
re-anchor needed at these Qs). Clamp `f_lo ∈ [30, 300]` Hz, `f_hi ∈ [1k, 8k]`, and **enforce
`f_hi ≥ 4·f_lo`** (below 2 octaves of separation the mid band thins to a phase sliver and Band knobs
stop meaning anything — a dramaticism clamp with a stability face).

### 4.2 Envelope followers — Vital's branching mean-square, exactly
Per band, per side (6 total), on the band signal *after* the split:

```
x2 = max(x·x, 1e-20)                          // denormal floor at the source (§10)
N  = (x2 > env) ? N_attack : N_release        // branching
env = (x2 + env·N) / (N + 1)                  // one-pole running mean of x²
N_attack  = max(baseAtk_ms(band) · Time · fs/1000, 5)     // Vital kMinSampleEnvelope = 5
N_release = max(baseRel_ms(band) · Time · fs/1000, 5)
```

The `N ≥ 5` floor is Vital's — it bounds the fastest gain slew to ~5-sample smoothing, which is the
whole anti-click strategy of this device class (there is no separate gain smoother; the follower IS
the smoother). Both computers of a band share ballistics but keep **separate env state** (down-env
clamps upward `≥ T_dn²` inside its own state, up-env clamps `≤ T_up²` — the clamps double as instant
re-bias when thresholds move, which is why threshold glides don't thump).

**Smooth type** prepends the 25 ms RMS pre-average (`computeMeanSquared` grammar, window
`0.025·fs`) and uses the Reiss soft knee in the gain computer (§4.4). **Stereo Linked** feeds both
channels' followers `max(x2_L, x2_R)`; **Dual** keeps them independent; **Mid-Side** splits
`M=(L+R)/√2, S=(L−R)/√2` *before* the band tree (two trees), with S thresholds 6 dB deeper (side
energy is that much lower on the bus).

⚠️ **AUDIT — the draft claimed "the widener effect falls out for free" from that offset. It is
exactly backwards.** Work it: with `L_S = L_M − 6` and `T(S) = T(M) − 6`, both `over = L − T_dn` and
`under = T_up − L` are *identical* for S and M ⇒ identical downward GR and identical upward lift ⇒
**the offset is a width NEUTRALISER, not a widener.** The free widening is what happens *without* the
offset: the quieter side sits further below `T_up`, collects ~6 dB more upward lift and ~5 dB less
downward GR, and the image blows open — usually too far, and mono-sum-unsafe on dense material. So
state it honestly: **the −6 dB S offset exists to make Mid-Side behave like Linked-with-two-trees
(spectral, not width, processing); the width axis is then a deliberate control, not an accident**
(§12 Q5 asks whether to expose it as a "Width-comp" knob — that question is now load-bearing, because
with the offset at −6 dB, Mid-Side's *only* remaining width behaviour is whatever the makeup and the
+24 dB up-cap contribute).

### 4.3 ⚠️ THE MIX LAW — phase-matched dry, or Mix combs
Wet = Σ bands = `AP2(f_lo)·AP2(f_hi)·x` even at zero compression (LR4 recombination is allpass, not
identity). Blend that against raw dry at Mix 50 % and you get **comb notches at both crossovers** —
the documented Xfer OTT flaw ("known phase cancellation issues"; Squash sells "phase-coherent" and
Cramit PE sells linear-phase *as the fix*). Our fix costs 4 SVF ticks: **pass the dry path through
the same AP2(f_lo)→AP2(f_hi) cascade** before the blend. Then wet-vs-dry differ only by gain — Mix
is clean at every setting, and Mix 100 % = fully wet (law 4). Linear crossfade (not equal-power):
wet and dry are now correlated, so equal-power would bump +3 dB at 50 %. Do NOT offer linear-phase
crossovers: 20+ ms latency for a synth insert is the wrong trade (and the fb305 send-math trap from
distortion bible §4.4 would return).

### 4.4 Gain computers — exact laws

```
// levels in dB via L = 10·log10(env)  (env is mean-square)   — compute in log2 domain, §9
down:  over  = max(0, L − T_dn);   gDn_dB = −slopeDn · over          // slopeDn = r_dn ∈ [0,1]; 1.0 = brickwall
up:    under = max(0, T_up − L);   gUp_dB = +slopeUp · under         // slopeUp = s_up ∈ [0,0.95]
       gUp_dB = min(gUp_dB, 24)                                       // cap: +24 dB (mult ≤ ~16)
       gUp_dB *= floorGate(L)                                         // §4.6
total: g = 10^((gDn_dB + gUp_dB + makeup_dB(band) + bandTrim_dB)/20)  // one pow per band per sample
Soft knee (Smooth only, Reiss/JAES-2012 form, W = 12 dB), applied to the downward side:
       if 2(L−T) < −W: identity;  if |2(L−T)| ≤ W: gDn_dB = −slopeDn·(L−T+W/2)²/(2W);  else full slope
```

Ratio bookkeeping for the UI: displayed downward ratio `R = 1/(1−slopeDn)` (0.9 → 10:1, 1.0 → ∞:1);
displayed upward ratio `R_up = 1/(1−slopeUp)` (0.8 → 5:1). Store slopes, display ratios.

### 4.5 🔑 THRESHOLD CALIBRATION — the −26 dBFS translation (house law 1)
Vital's defaults assume a hot synth bus (its own runs near full scale). Terrain's FX bus program is
**−26 dBFS single-note** (✅ verified in-tree: `PluginProcessor.cpp:46` records the real bounce of a
single note at **−20 dBFS at the output**, and the FX send is padded 6 dB below that by
`kVoiceToFxPad = 0.5f` at `:6300` ⇒ −26 dBFS on the bus) **/ ≈ −20 dBFS on the fb264 reference
chord** (*this chord figure is an ESTIMATE — ~+6 dB for a 4-note chord — not a measurement; measure
it before freezing the table, since every makeup value below is calibrated to it, fb264/fb249 law*). Port Vital's numbers verbatim and *nothing ever crosses a threshold* — the
classic dead-OTT port, the #1 predicted failure of this device. Translation: shift ≈ −20 dB and
re-spread the high band for §2.4's rolloff reality. **v1 anchors (Classic, Amount 50, Grip 0):**

| Band | T_dn (dBFS) | T_up (dBFS) | slopeDn | slopeUp | makeup (dB) | base A/R (ms) |
|---|---|---|---|---|---|---|
| Low (< 88.3) | **−48** | **−55** | 0.9 | 0.8 | **+16** | 2.8 / 40 |
| Mid (88.3–2.5k) | **−45** | **−56** | 0.857 | 0.8 | **+12** | 1.4 / 28 |
| High (> 2.5k) | **−56** | **−66** | 1.0 | 0.8 | **+10** | 0.7 / 15 |

⚠️ **AUDIT — the makeup row was wrong and the "~10 dB" note was wrong. Both corrected above.**
The exact deviation from a straight −20 dB shift of Vital's defaults is: low and mid thresholds are
**an exact −20 shift** (Vital −28/−35 → −48/−55; −25/−36 → −45/−56); the high band's `T_dn` is
**6 dB** deeper than the shift (−50 → −56) and its `T_up` is **11 dB** deeper (−55 → −66), because
the band itself is that much quieter on synth program (§2.4). *Not* "~10 dB" on both.
Consequently the makeups **cannot** be down-scaled: with Vital's slopes unchanged and the thresholds
moved *with* the program, the low and mid bands see **the same gain reduction Vital sees** (≈16 dB
low, ≈12 dB mid) — so a makeup of +10/+7 ships the device roughly **6 dB quiet** and instantly fails
the `unity` gate. The high band is the one that legitimately drops, to ≈+10, precisely because its
`T_dn` is 6 dB deeper than the shift. Independent corroboration: Xfer OTT's own factory output gains
are **+10.3 / +5.7 / +10.3 dB on top of +5.2 dB of per-band input gain** (§1.3, EDMProd teardown) —
i.e. ≈ +15.5 / +10.9 / +15.5 dB of total per-band lift, which brackets the corrected column.
⚠️ **Low-band caveat the draft missed:** below 88.3 Hz there is *nothing* in a 110 Hz saw — the low
band only carries content for notes below ~A2 plus the crossover skirt. Low-band calibration is
therefore register-dependent by construction; run `engage` on the **reference chord** (which contains
bass), never on a mid-register single note, or the low row will read dead and get mis-tuned.

**These anchors are a starting calibration, not gospel: the §8-harness gate is "each band shows
8–18 dB downward GR and 3–10 dB upward lift on the reference chord at defaults, and unity-through
±1 dB"** — tune the table until it gates, then freeze it per Type. (⚠️ AUDIT: the draft's gate read
"4–10 dB downward" — that window is *below* Vital's own reference calibration and would have
certified a device 6+ dB shallower than the sound this device exists to make. A too-timid gate is
the dead-port failure of §10.1 wearing a passing test.)
Internally, like the DistortionEngine, apply the device-local `+6.02 dB` in / `−6.02 dB` out trims
that cancel `kVoiceToFxPad` for the device's own math (distortion bible §2.2 pattern) so these
constants stay honest if the pad ever changes.

### 4.6 The floor gate — upward compression that dies with the note (law 6)
Below a floor level `F = −78 dBFS` (**52 dB** under the −26 dBFS program — the draft said "≈ 50",
off by 2; if you re-tune the bus figure, re-derive F, don't carry the constant), ramp the upward gain
back to unity:

```
t = clamp((L − F) / 12, 0, 1)                // NOTE the clamp — smoothstep is undefined outside [0,1]
floorGate(L) = t*t*(3 − 2*t)                 // 1 above F+12 dB, 0 at or below F, cubic-smooth between
```

12 dB of ramp + the follower's own release = no chatter (do NOT use a hard comparator — a decaying
reverb tail crossing F would gate-flutter; this is the Zero-Square −72 dBFS-gate lesson from the
distortion bible applied to gain instead of signal). Result: silence in → unity gain → **silence
out**; the noise floor is never resurrected; free-running behavior is impossible. The gate level is
fixed (not a knob) — it is a stability constant, and moving it up is the timid failure in the other
direction (kills the tail-bloom that IS the effect).

### 4.7 Latency, oversampling, denormals, NaN
* **Latency: 0 samples, reported 0.** IIR crossovers, no lookahead, follower attack floor ≥ 5
  samples. Nothing to compensate; the fb305 main-send exclusion math stays exact. (Xfer's 2-sample
  latency is an implementation artifact we simply don't have.)
* **Oversampling verdict: NEVER.** Gain modulation at these ballistics creates sidebands ±(1/τ)-ish
  around partials, not hard-sync spectra; at the 5-sample slew floor the modulator bandwidth is
  ≈ fs/10 with a smooth spectrum — audible aliasing is negligible and the entire industry (Xfer,
  Vital, Ableton, Serum 2) ships this class un-oversampled. The one alias-capable corner (Time 5 %
  low band tracking 40 Hz cycles = waveshaping) is a *deliberate destruction zone* (§14 preset 13, `Envelope Eater`) — document, don't launder. Heavy's band clippers are cubic (bounded H3) at
  −6 dB-band level — below the audibility gate.
* **Denormals:** the `1e-20` floor in §4.2 + JUCE's global FTZ/DAZ. The follower can never denormal-
  crawl because the floor is added pre-average.
* **NaN/BIBO:** `env ≥ 1e-20 > 0` (no divide-by-zero); total gain **hard-clamped** to
  `g ≤ 10^(52/20) ≈ 400` — ⚠️ AUDIT: the draft asserted `10^((24+12+12)/20) < 300`, but the makeup
  term is up to +16 dB (§4.5) and Sheen adds +4, so the true worst case is 24 (up-cap) + 16 (makeup)
  + 12 (band trim) = 52 dB ≈ 400×, not 300×. Clamp it explicitly (Vital's `kMaxExpandMult` idiom)
  rather than relying on the arithmetic staying true when the tables are re-voiced. Input clamp
  ±8 FS at the device gate (the DistortionEngine gate idiom). Feed-forward ⇒ unconditionally BIBO.
  **Max stable loop gain: N/A — there is no loop** (statement required by law 6; the only recursion
  is the one-pole follower, |pole| = N/(N+1) < 1 always).

### 4.8 Param glide table (law 7)

| Param | Glide | Why |
|---|---|---|
| Amount, Up, Down, Air, Grip | 15 ms one-pole (DistortionEngine idiom, `DistortionEngine.h:147`) | slopes/thresholds move smoothly; follower clamps re-bias instantly |
| Time | 30 ms on the *multiplier* | N jumps are inaudible (follower state carries) but LFO squares must not step GR |
| Crossovers | 30 ms on fc | §4.1; SVF coeff continuity |
| Band trims, makeup | 15 ms | plain gains |
| Mix | 10 ms linear | correlated crossfade |
| Type | 60 ms constant-glide (+ tree crossfade for Duo/Quad) | §3 switch law |
| Stereo mode | 30 ms output crossfade between both detection paths | M/S↔LR is a topology swap; run both matrices during the fade (cheap) |

---

## 5. Chassis map — the locked fb275 grammar

Param IDs follow the `SYN_DST_*` grammar exactly (`ParameterIDs.hpp:406-433`) as `SYN_OTT_*`; device
power `SYN_OTT_POWER` **default OFF** (dry init, = main send when on, gates routing — the DST
pattern); per-osc send pills `SYN_OTT_SRC_A..NOISE` default OFF.

### 5.1 Front card — 4 hero knobs + pills + the live viz

| Knob | ID | Range → law | Default | What it DOES (pragmatic name check) |
|---|---|---|---|---|
| **Amount** | `SYN_OTT_AMOUNT` | 0..1; s ≤ 0.5: all slopes ×(s/0.5)^1.2 (0 = true 1:1 bypass-shape); s > 0.5: slopes → max (dn → 1.0, up → 0.95) AND thresholds deepen −8·(2s−1) dB. **The +24 dB up-cap is FIXED and does not scale with Amount** (it is a stability constant, §4.6/§4.7). Taper keeps every degree live | 0.50 | THE knob. How hard both jaws close. 0 = open hands, 50 = the Classic sound, 100 = just past useful (total dynamic annihilation) |
| **Time** | `SYN_OTT_TIME` | 5 %..1000 %, log taper, ×base ballistics | 100 % | One knob, all 6 followers. Left = snappier until gain tracks the waveform itself (fuzz — intended, §3); right = slow breathing walls |
| **Air** | `SYN_OTT_AIR` | 0..1; high band: T_up += 10a dB, slopeUp ×(1+1.2a) (cap 0.95), makeup +4a dB, X-High ×(1−0.25a) | 0.25 | The Serum-sheen axis: how much quiet top gets resurrected. Distinct from Treble (a static trim) — §2.4 discriminator |
| **Mix** | `SYN_OTT_MIX` | 0..1 linear, phase-matched dry (§4.3) | 1.0 | 100 % = fully wet (law 4). 30–60 % = classic parallel "Depth" |

Pills: **`Auto`** (`SYN_OTT_AUTO`, default **OFF** — mirrors DST: ~70 % loudness match on a 300 ms
RMS tracker; the Type makeup tables already land unity at defaults, Auto is for extreme-knob riders) ·
**`Punch`** (`SYN_OTT_PUNCH`, default OFF — MO-TT "Smack" lineage: when the fast env exceeds 4× the
slow env, hold the *upward* computer at unity for 10 ms, 5 ms recover; attacks keep their snap
instead of being pre-inflated). Both glide their effect on/off over 20 ms.

### 5.2 Back panel — 2 dropdowns + 8 knobs (4×2), fb275-exact

**Dropdown 1 — `Type`** (`SYN_OTT_TYPE`, choice(8)): Classic · Smooth · Heavy · Sheen · Bass-Safe ·
Bloom · Duo · Quad (§3).
**Dropdown 2 — `Stereo`** (`SYN_OTT_STEREO`, choice(3)): Linked · Dual · Mid-Side (§4.2).

| Pos | Knob | ID | Range → law | Default | Does |
|---|---|---|---|---|---|
| P1 | **Low Cross** | `SYN_OTT_XLOW` | 30..300 Hz, log (Duo: 150 Hz..2 kHz) | 88.3 Hz | Low/mid split point |
| P2 | **High Cross** | `SYN_OTT_XHIGH` | 1k..8k Hz, log, clamped ≥ 4·XLOW | 2.5 kHz | Mid/high split point (Air scales it; Quad: top split, mid split derived geometric mean) |
| P3 | **Up** | `SYN_OTT_UP` | 0..150 % of Type's upward slopes (slope cap 0.95) | 100 % | Upward amount — the detail/air/tail resurrection dial |
| P4 | **Down** | `SYN_OTT_DOWN` | 0..150 % of Type's downward slopes (cap 1.0 = brickwall) | 100 % | Downward amount — the squash/ceiling dial |
| P5 | **Grip** | `SYN_OTT_GRIP` | ±18 dB, linear-in-dB, shifts ALL 6 thresholds opposite | 0 dB | How deep the jaws sit in the program = OTT's "In Gain" without clipping the input. + = grabs everything, − = only peaks |
| P6 | **Bass** | `SYN_OTT_BASS` | ±12 dB post-comp band trim | 0 | Low band level (the built-in 3-band EQ, Xfer's drag-slabs) |
| P7 | **Mids** | `SYN_OTT_MIDS` | ±12 dB (Duo: becomes tilt between the 2 bands) | 0 | Mid band level |
| P8 | **Treble** | `SYN_OTT_TREBLE` | ±12 dB (Quad: bands 3+4 ganged) | 0 | High band level — static, vs Air's dynamic lift (no-doubles law: different name, different mechanism, §2.4 proof) |

Every knob 0→100 alive (law 5): Amount's taper has no dead first third (slope scaling is
perceptually linear-ish in GR-dB); Up/Down above 100 % go *past* the Type calibration (the no-playing-
safe headroom); Grip's extremes are "everything is ceiling" / "device barely breathes" — both useful,
both reachable. Time's bottom decade is the deliberate destruction zone.

**Tempo-sync note (law 3):** the device has no tempo-relevant time knob — Time is a ratio scaler on
program-time ballistics (5 %–1000 %), like every compressor ever shipped. The 4-bars→1/256 rule is
therefore N/A here; if Max ever wants tempo-locked release pumping, that is a mod-matrix LFO→Grip
patch, not a synced knob (flagged in §12).

---

## 6. Visualizers — how the greats draw it, and our card

### 6.1 The survey (mechanisms, precisely)
* **Xfer OTT** — the icon. Three horizontal band regions; per band, a live **level bar** and colored
  threshold slabs that **squeeze inward** as the two computers engage: the upper edge (downward
  ceiling) presses down on peaks, the lower edge (upward floor) rises under quiet passages — the
  famous "jaws" animation, plus drag-the-slab per-band gain. In/GR metering per band on the columns.
  Loved because you *see the dynamic range being eaten* in real time.
* **Ableton Multiband Dynamics** — engineering view: per band a row with two rectangular blocks
  (Above / Below); block edge drag = threshold, vertical drag = ratio (the block's hatching
  stretches/squashes to show slope); blocks light green (above active) / orange (below active); per-
  band output meters. Zero glamour, total information.
* **Serum 2 Compressor (multiband)** — three-lane graphical display with **direct manipulation**:
  crossovers (X-LOW/X-HIGH) and H/M/L band gains are dragged on the display itself; live per-band
  level/GR in the lanes. (The What's-New sheet's "directly adjust controls using the graphical
  display" is the whole Serum 2 FX design language — our card must match this bar.)
* **FabFilter Pro-MB** — real-time spectrum with floating per-band blocks at threshold height; each
  band's live GR animates as a moving inner bar; the summed dynamic-EQ curve redraws continuously.
* **Slate MO-TT** — OTT columns + per-band GR meters + spectrum backdrop.
* **Arturia Comp DIODE-609** — dual VU needles; click a VU → digital strip drawing the **waveform
  with the gain-reduction trace overlaid** (the single best "what did it do to my note" display in
  the field). **Bus FORCE** — per-path metering with real-time displays.
* **Audio Damage Evil Otto** — scrolling waveform + draggable threshold lines directly on it.

### 6.2 Our card — 3 concepts (canvas, CPU-cheap, laws 9 + curves-must-move)
Feed: the engine publishes per-band `⟨L_env, gDn_dB, gUp_dB, trim⟩` ×3(4) bands at block rate into
the existing viz push lane (the HARM-VIZ atomic-frame idiom, `PluginProcessor.cpp:6132`; ~30–60 Hz,
push only visible×fresh per the fb342 law). No FFT needed for concepts A/B — the numbers are already
the story. All idle-dim / playing-bright (law 9), all reflect every sound-changing param.

* **A. THE JAWS (recommended)** — three vertical lanes (L/M/H; four in Quad, two in Duo — the lane
  count IS the Type telltale). Per lane: a bright level column rides `L_env`; above it a **ceiling
  slab** at `T_dn + trim` presses down by `gDn_dB` (glowing hotter with depth); below, a **floor
  slab** rises by `gUp_dB` (purple bloom — the air being poured in). The gap between slabs = the
  surviving dynamic range: at Amount 100 the jaws visibly *bite shut* on the note. Crossover
  positions = the lane boundaries, **draggable** (Serum-2 direct-manipulation parity); drag a lane's
  body vertically = band trim (Xfer's slab-drag). Amount/Up/Down/Grip/Air all visibly move slab
  geometry even at idle (dim); audio makes them fight. Cost: 6–8 rects + glows per frame, no
  shadowBlur (fb342 law), trivial.
* **B. THE CURVE RIDER** — three small in/out transfer curves (the §2.3 static picture, one per
  band, the distortion-card grammar reused): the knee geometry redraws from
  T_up/T_dn/slopes/knee, and a bright occupancy dot rides each curve at `L_env`, its vertical
  distance from the 1:1 diagonal = current gain change, trailing a short fade. Idle: dim curves
  (params still reshape them — curves-must-move law). Best param-truth per pixel; slightly less
  visceral than A.
* **C. SPECTRAL DELTA** — reuse `SpectrumAnalyzer.h` (two instances pre/post, triple-buffered,
  ~47 fps): dim white pre-spectrum, bright post overlay; fill the delta purple where OTT *added*
  energy (the air, literally visible as a growing purple crown above 2.5 k) and dark where it cut;
  crossover handles on the frequency axis. The most "wow", the most CPU (2×4096 FFT — already-proven
  in-tree cost), and it hides the up/down mechanics — ship as the card's alt-view toggle if at all.

Verdict: **A** as the core card visual, **B** embedded as the back-panel thumbnail. Both react
dramatically (idle = dim slabs at rest; playing = columns leaping, jaws biting, purple floor-glow
breathing at the release rate — an "obvious delta" per the fb311 hard rule).

---

## 7. Interplay — the device in the chain

* **Unity-through discipline:** at Power ON, defaults (Classic, Amount 50, Time 100, Air 25,
  Mix 100), the reference chord passes at **±1 dB RMS** (makeup tables are calibrated to the CHORD,
  not a sine — the fb264/fb249 law). The §8 harness gates this. A user toggling the device hears
  *texture change, not level change* at defaults.
* **Ordering wisdom:** after Distortion = the classic (tames drive spikes, resurrects the decay of
  driven notes); before Reverb = clean bloom feed; **after Reverb/Delay = the famous "OTT'd tail"**
  (upward comp holds the wash at constant level — and our floor gate §4.6 guarantees the wash still
  dies with the note, law 6). Every chain order must work; none is privileged in code. (How many orders are *reachable* is the §7 ② / §12 Q4 `SYN_FX_ORDER` decision, not a DSP constraint.)
* **Spectrum/dynamics downstream:** output crest 3–8 dB (program-dependent), HF density up —
  distortion placed *after* OTT therefore bites harder at equal Drive (flag in the manual);
  reverb after OTT blooms brighter (constant HF feed). Nothing here breaks another device's
  assumptions — output level stays ~bus-nominal by calibration.
* **Stacking OTT on OTT** (users WILL): stable (feed-forward, capped), but noise-floor lift
  compounds: two devices lift the inter-note floor by up to +48 dB minus two floor gates — the gates
  make it safe (floor gate engages at −78 dBFS *per device*, so device 2's gate sees device 1's
  gated silence and stays shut). Verified in the harness (§8, `stack` probe).
* **Engineering — the three exact edits a 4th device needs** (the fb305/fb338 landmine field,
  pre-located): ① `ottSendL/R` joins **every** main-send exclusion sum — `PluginProcessor.cpp:7159`,
  `:7326`, `:7358` (the fb338 law: EVERY send bus joins EVERY main-send exclusion, else the send
  material double-counts in the other devices' dry math — ✅ all three line numbers verified in-tree
  during the audit); ② ⚠️ **AUDIT — the chain permutation CANNOT "grow 6 → 24."** `SYN_FX_ORDER` is
  already a live `AudioParameterChoice` with a **six-entry** StringArray, created at
  `PluginProcessor.cpp:3488-3495` ("Reverb > Delay > Distortion" … "Delay > Reverb > Distortion"),
  read as an index and clamped `jlimit(0,5,…)` at `:5860`, and consumed by the `switch` at
  `:7383-7391`. **Rack law C — choice-param cardinality is fixed at birth (fb342/fb345) — forbids
  resizing that list**: presets already store indices against it, so a 24-entry replacement is a
  state-format break, not an edit. (The `ParameterIDs.hpp:435-437` comment the draft cited as a
  warning is now **stale** — it still describes `SYN_FX_ORDER` as a *bool*; fb341 already converted
  it. Fix that comment while you are in there.) The three legal options, per
  `FX-RACK-RESEARCH-INDEX.md` §3 which flags this as the decision that **blocks device #4**:
  (a) accept a 24-entry menu *and* accept the state break, (b) pin OTT to a fixed chain position and
  leave `SYN_FX_ORDER` alone, (c) **replace order with a pre-allocated rank/drag-list property now**
  — the chain bible's recommendation, and the only one that survives device #5 (120 permutations).
  This is a Max decision (§12 Q4), not a builder decision; ③ an `applyOtt` insert-lambda
  cloned from `applyDst` (`:7309`) — main send = the whole mix through the device, wet-only return,
  processor owns Mix.

---

## 8. Verify — the perceptual harness (sample-diff BANNED, per house rule)

`ott_cert` harness, same build recipe as the Phase-G family certs (`clang++ -O2 -I shim -I Source`,
self-contained header + 20-line JUCE shim — the fb283 zero-transcription-drift law). Probes and
gates:

| Probe | Signal | Metric | Gate (Classic defaults unless said) |
|---|---|---|---|
| `engage` | reference chord @ bus level (⚠️ chord, not a mid-register single note — §4.5 low-band caveat) | per-band gDn, gUp | dn **8–18 dB** AND up **3–10 dB** **per band** (the anti-dead-port gate — widened in audit; the draft's 4–10 dB window was below Vital's own reference depth) |
| `unity` | reference chord | out−in RMS | ±1 dB (all 8 Types at their defaults) |
| `levelstep` | −20 dB step, 500 ms | band out delta | ≤ 6 dB (§2.5); Bloom: gain never < 0 dB |
| `air` | dark-pad probe | Δ spectrum 8–12 k vs bypass | Classic ≥ +4 dB; Sheen type ≥ +8 dB; static-shelf null test fails levelstep |
| `floor` | note → 5 s silence | out RMS in silence | ≤ −90 dBFS (floor gate works; no free-run) — also `stack` ×2 devices |
| `ripple` | 100 Hz sine, Amount 100 | THD | Classic > 3 % @ Time 5 % (documented destruction), Smooth < 0.3 % @ Time 100 % |
| `click` | knob/type/stereo square-jumps @ audio | per-char click floor (honest per-transition, fb345 probe-craft) | no transition > −60 dBFS residual click |
| `comb` | white noise, Mix 50 % **and** Mix 100 % at Amount 0 | spectrum flatness + **perfect-reconstruction null** (wet-sum minus AP2(f_lo)·AP2(f_hi)·dry) | no notch > 1 dB at crossovers at Mix 50 (phase-matched dry proof) **and** null residual ≤ −60 dBFS at Amount 0 — the rack-wide crossover gate shared with the Splitter device |
| `discrim` | per §3 table | each Type's discriminator | all 8 pass or the Type is re-voiced/cut (law 5) |
| `cpu` | 512-block bench | % core @ 48 k | ≤ 0.5 % (§9) |

Dramaticism gate: A/B `Amount 0 ↔ 100` on the chord must be a *jaw-drop* (crest 8 dB → ≤ 3 dB, tails
+10 dB, air +8 dB) — if a blind listener calls it "subtle", the calibration failed no matter what the
numbers say.

---

## 9. CPU — budget and tiering

Per stereo sample (Classic, 3 bands): 7 SVF ticks/ch (2 splits ×3 + 1 align-AP) + 2 dry-AP ticks/ch
≈ 18 SVF ticks (~8 flops each) + 6 followers (~10 flops) + gain math with **log2/exp2 polynomial
approximations** (the Vital `futils` approach — never call `powf` per sample) ≈ 6×15 flops ≈
**~350 flops/frame ≈ 17 MFLOP/s @ 48 k** — well under **0.5 % of one core** (*"roughly a third of the
Delay device" is an UNVERIFIED comparison — no Delay bench was run; drop it or measure it*).
M/S = ×2 trees (~0.9 %); **Dual** stereo also doubles the follower + gain-computer count (detection is
per-channel: 12 followers, 12 pows) though not the filter count. ⚠️ AUDIT: the draft costed
**Quad** at "+1 split +1 AP" — wrong. Quad has *three* crossovers, so it needs +1 split (3 ticks/ch)
**+2 more band-alignment APs** (low needs AP2(f_mid)·AP2(f_hi), mid needs AP2(f_hi) — §4.1) **+1 more
dry-path AP** ≈ **+6 SVF ticks/ch**, plus 2 more followers and 2 more pows (~0.75 %). No Quality dropdown, no
tiers, no oversampling ever (§4.7). One optimization is pre-approved if the bench asks: compute the
6 gain dBs every 4 samples and linearly interpolate the *linear* gains (the smoothing floor already
guarantees ≤ 5-sample slew, so ×4 decimation is transparent) — takes the pow budget to ¼. Control
head (glides, calibration resolve) runs at block edges only; the awake-head **sleeps when the device
is bypassed** (fb344 control-head sleep law), and `flush()` snaps all glides + zeros env states
(silent, not a ramp — the DistortionEngine `:227` idiom).

---

## 10. Pitfalls — the collected traps

1. **The dead port** — literature thresholds on a −26 dBFS bus = a device that never engages. §4.5
   is the antidote; the `engage` gate is the proof. (The #1 way this ships broken.)
2. **Noise-floor resurrection** — upward comp without the §4.6 floor gate amplifies dither, voice
   noise floors, and the previous device's hiss by +24 dB, forever, violating law 6. Gate is smooth
   (12 dB ramp) or it flutters on tails.
3. **Mix comb** — un-matched dry (§4.3). Every OTT clone that skipped this grew a "phase" complaint
   thread. 4 SVF ticks. Just do it.
4. **Threshold zipper/thump** — thresholds inside the follower clamps re-bias instantly, but *slope*
   jumps modulate gain directly: everything glides (§4.8). Type switch is the worst case → 60 ms +
   tree crossfade + follower re-seat (fb345 law).
5. **Attack overshoot / GR spikes** — the `N ≥ 5` floor bounds slew; do not "optimize" it away, it
   is the click guard.
6. **Denormal crawl** — followers decay exponentially toward 0 → add `1e-20` pre-average (§4.2);
   states also snap on `flush()`.
7. **Bass IMD** — low-band env tracking 40–60 Hz cycles at fast Time = gritty bass (Classic at
   extreme Time: a feature, §3; at *default* Time the 2.8/40 ms base must NOT ripple > 0.5 dB on a
   50 Hz sine — verify, else raise the low base release).
8. **Stereo image wobble** — Dual mode on a wide unison patch makes L/R gains diverge (image
   breathes). That IS Dual's sound; Linked is the default precisely so the stock experience is
   solid-image (and the mono-sum-collapse trap is dodged: linked gains are identical, M/S mode is
   sum-safe by construction).
9. **Makeup lies** — calibrate makeup to the CHORD, not a sine (fb264 law); a sine-calibrated
   makeup under-reads by the crest difference and the device ships +6 dB hot.
10. **Auto chasing silence** — the Auto pill's RMS tracker must freeze below the floor gate level
    or it slowly cranks gain during silence and the next note blasts (the Diode-2 lesson).
11. **Choice-param cardinality** — 8 Types, 3 Stereo modes locked at first ship (fb342 law);
    reserve nothing, rename nothing after presets exist.
12. **The fb305 sums** — forget one of the three `ottSend` exclusion edits (§7) and the *other*
    devices' main-send math silently double-counts routed material. Grep all three lines in review.
13. **Viz early-return** — bypassed/idle must still push dim frames (curves-must-move / rAF law);
    the jaws at rest are part of the UI truth.
14. **DC** — the device neither creates nor passes judgment on DC (gain is positive scalar), but
    upward gain ×16 on an upstream DC offset becomes a thump: keep the rack's existing DC hygiene
    upstream (distortion's blockers); no blocker needed in-device (SVF splits kill DC in mid/high
    trivially; low band passes it — acceptable, matches every shipped OTT).

---

## 11. Hard-rule compliance checklist (laws 1–10 + rack laws A–D, explicit)

| # | Law | Compliance |
|---|---|---|
| 1 | Bus reality −26 dBFS | §4.5 translation table; thresholds stated in dBFS *on this bus* AND as a shift off Vital's 0-dBFS-program values; device-local ±6.02 dB pad-cancel trims; `engage` harness gate (widened in audit to 8–18 dB dn) |
| 2 | fb275 chassis | §5: Type + Stereo dropdowns, 8 back knobs 4×2, **3 front heroes + Mix** + Auto/Punch pills, `SYN_OTT_*` grammar cloned from `SYN_DST_*`. Names audited for doubles: `Squash`→`Amount`, Type `Air`→`Sheen` (§3 note) |
| 3 | Time params 4 bars→1/256 | N/A — no tempo-synced knob exists (Time is a ratio scaler, §5.2 note); no violation possible |
| 4 | Mix 100 % = fully wet; switches never cut | §4.3 phase-matched Mix; §4.8 glide table; §3 type-switch crossfade law |
| 5 | Params evolve 0→100, Types night-and-day | §5 taper laws (no dead zones; >100 % Up/Down headroom; Time's destruction decade); §3 discriminator column + `discrim` harness gate |
| 6 | Nothing free-runs; loop-gain law | §4.6 floor gate (sound dies with the note); §4.7: feed-forward, no loop exists, follower pole < 1 always — stated |
| 7 | No clicks/crackle | §4.8 full glide table; `N ≥ 5` slew floor; `click` harness probe with honest per-transition floors |
| 8 | CPU-friendly | §9: ≤ 0.5 % core, no oversampling ever, control-head sleep, pre-approved ×4 gain decimation |
| 9 | Audible ⇄ visible, dramatic | §6.2 Jaws: every param moves slab geometry; idle dim / playing bright; per-band GR + air-glow live at block rate |
| 10 | Recycle first | §13 inventory — SVF, ducker-follower grammar, DistortionEngine shell/API **+ its shipped SHAPER `Squash` leveller**, SpectrumAnalyzer, viz push lane, DST param grammar, insert-lambda chain, and the sibling Compressor/Splitter bibles — all verified by reading, file:line cited |
| **A** | **Zero lookahead, rack-wide** | ✅ §4.7: 0 samples reported, IIR crossovers, no lookahead, linear-phase crossovers explicitly **rejected** in §4.3 (20+ ms would phase-smear the fb305 sample-aligned dry subtraction). Xfer's 2-sample latency is deliberately not reproduced |
| **B** | **No runtime param creation** | ✅ Nothing in this device is dynamic — a fixed 11-param chassis + pills, all declared at `createParameterLayout` time. (The multi-instance slot-pool question belongs to `FX-CHAIN-BIBLE.md`, not here) |
| **C** | **Choice cardinality fixed at birth** | ✅ for this device: `SYN_OTT_TYPE` ships **all 8 enum slots day one** (§3), `SYN_OTT_STEREO` all 3 — no reserving, no renaming after presets exist (§10.11). ⚠️ **VIOLATED by the draft for `SYN_FX_ORDER`** ("grows 6 → 24") — corrected in §7 ②; that param is a *decision blocker*, not an edit |
| **D** | **Every send bus joins ALL exclusion sums** | ✅ §7 ① names all three sites, re-verified in-tree at `PluginProcessor.cpp:7159` / `:7326` / `:7358` (`rtdL`/`rtdR` = `rvbSend + dlySend + dstSend` × `outputGain` × `kVoiceToFxPad`). ⚠️ Note `FX-RACK-RESEARCH-INDEX.md` §2 cites these as "`index.html:6979`, `:7111`" — **that citation is stale/wrong**; the sums live in `PluginProcessor.cpp`. `ott_cert` §8 has no probe for this — grep all three lines in review instead (§10.12) |

---

## 12. Open questions for Max

1. **Device name on the rack card:** "OTT" (everyone knows it, but it's Xfer's plugin name — generic
   as a genre term, still worth a deliberate call) vs a house name. ⚠️ AUDIT: the draft's pick was
   "Squash", which is unavailable — it is already a shipped knob label (§3 naming note), and the
   hero knob has been renamed `Amount` for the same reason. Live candidates: **`Jaws`** (matches the
   §6.2 viz and nothing else in the tree uses it) · `Lift` · keep `OTT`. Bible assumes the rack label
   can be either; IDs are `SYN_OTT_*` regardless.
2. **Auto pill default** — OFF (DST precedent, §5.1) with unity guaranteed by calibration instead.
   Agree, or do you want Auto ON for the "always loud" demo feel?
3. **Air default 25 %** — the device sparkles out of the box but isn't a shelf. Hotter (40 %)?
4. **SYN_FX_ORDER — BLOCKS THE WIRING, decide before any 4th device lands.** It is a live
   *six*-entry choice param (`PluginProcessor.cpp:3488`) and rack law C says its cardinality is fixed
   at birth, so "just make it 24" is a **preset/state-format break**, not a bigger menu (§7 ②).
   (a) 24 entries + accept the break, (b) pin new devices to fixed chain positions, or (c) replace
   order with a pre-allocated rank/drag-list property now, before device #5 makes it 120?
5. **Mid-Side mode S-threshold offset (−6 dB)** — want a back-panel exposure later (a "Width-comp"
   axis), or keep it a fixed voicing?
6. **Duo/Quad**: keep both, or cut one for a 7-type list? Both pass discriminators; Quad costs the
   most viz/UI work (4 lanes). ⚠️ This is a **pre-ship-only** decision — rack law C freezes the Type
   list cardinality at birth, so a Type cut after v1 breaks every stored preset index. Decide before
   the enum is declared, and if in doubt ship the slot **disabled** rather than absent.
7. **Preset naming** — §14 sketches use working titles; your ears + names before freeze (the fb345
   "Max's ears" ritual).

---

## 13. Recycle inventory — verified by reading, with lines

*✅ Every file:line in this section was re-opened and confirmed during the 2026-08-14 audit.*

* `TerrainFilters.h:317` **SvfMultimode** — the TPT SVF whose `lp/hp` taps + coefficient math build
  `LR4Split` and the AP2s (§4.1); `:83 TPTOnePole` for the Auto tracker; `:69 DCBlocker` if ever
  needed.
* `MoogDelay.h:225-227, :473-485` — the shipped sidechain ducker: branching attack/release one-pole
  follower grammar (5 ms/200 ms) = the Punch pill's fast/slow detector pattern, proven click-free.
* `DistortionEngine.h` (header, `:99 prepare`, `:147` glide, `:227` flush-snap) — **the shell to
  clone**: self-contained header + JUCE shim harness law, `prepare/setters/processSample(wet-only)/
  flush` API mirroring DelayEngine, 15 ms glide idiom, device-local pad-cancel trims, control-head
  sleep (fb344), gate idiom at the input.
* `PluginProcessor.cpp:7309 applyDst` — the insert-lambda to clone as `applyOtt`; `:7159/:7326/
  :7358` the three exclusion sums; `:7383` the permutation switch; `:5860` fxPerm clamp; `:6300`
  kVoiceToFxPad; `:46` the bus measurement this whole calibration hangs on.
* `SpectrumAnalyzer.h` (whole file, 109 lines) — pre/post spectra for viz concept C, triple-buffered
  at ~47 fps, zero new engineering.
* `ParameterIDs.hpp:406-433` — the `SYN_DST_*` block to clone as `SYN_OTT_*` (type/character-slot
  dropdowns, front knobs, back-8, SRC pills, POWER, AUTO, pill-2 grammar).
* UI: the fx-rack v7 chassis + back-panel mockup (`Design/fx-rack-v7-CANONICAL.html`,
  `Design/fx-back-panel-mockup.html`), the distortion card's viz push/idle laws, the `.pmenu` preset
  pill gate (`ui/public/index.html:8230` — add the new device core to the gate list, the fb342 "the
  gate was THE bug" lesson; note the gate matches on the JS **core string**, and the distortion's is
  `'saturate'`, not `'dst'` — pick the OTT core string once and use it in both `DEVS` and the gate).
* ⚠️ **AUDIT — three recyclables the draft missed:**
  * `DistortionEngine.h:2259-2270` (`shaperF`, SHAPER P8 **Squash**) — **an upward-compression
    leveller already shipping in this plugin**: `shEnv_ += (a − shEnv_) * (rising ? 0.0032f :
    0.00013f)` (≈ 3 ms / 80 ms at 48 k), `g = 0.9f / max(0.045f, shEnv_)`, clamped to 20× (~+26 dB),
    byte-identical bypass at 0. It is the closest in-tree precedent for §2.2's gain computer and for
    the §4.6 floor problem (its `max(0.045f, …)` denominator floor is a crude version of our gate) —
    read it before writing a line of the upward path.
  * `COMPRESSOR-BUILD-BIBLE.md` §0 (the OTT boundary), §3.6 (upward compression + silence gate),
    §6.7 (`Squeeze`) — the sibling document this device must not contradict.
  * `SPLITTER-BUILD-BIBLE.md` — its LR4 + allpass-compensation section and its
    perfect-reconstruction null test are the same DSP as §4.1/§4.3; share the `LR4Split` helper
    between the two devices instead of writing it twice (recycle law).

---

## 14. Presets — 13 factory sketches (working titles; values = Amount/Time/Air/Mix · Type/Stereo · notable back)

1. **Over The Top** — the calling card. 65/100 %/30/100 · Classic/Linked · defaults.
2. **Half Squash** — daily driver. 45/100 %/25/70 · Classic/Linked.
3. **Air Lift** — the Serum-sheen preset. 55/100 %/70/90 · Sheen/Linked · Treble +2.
4. **Wide Sheen** — 50/100 %/60/85 · Sheen/**Mid-Side** · Treble +3 — the widening air.
5. **Pad Bloom** — tails forever. 60/**300 %**/40/100 · Bloom/Linked · Up 120 %.
6. **Quiet Lifter** — detail resurrection, zero squash. 50/150 %/30/100 · Bloom · Down 0 %.
7. **Bass Anchor** — low end glued, top OTT'd. 50/100 %/35/100 · Bass-Safe/Linked · Grip +4.
8. **Reese Guard** — mid grip for moving basses. 60/80 %/15/100 · Bass-Safe · Down 120 %, Mids −2.
9. **Pluck Snap** — Punch pill ON. 55/**40 %**/35/100 · Classic · Grip +6 — snappy, never smeared.
10. **Glue, Politely** — the mix-bus-ish one. 30/**250 %**/20/55 · Smooth/Linked.
11. **Breathing Wall** — slow pumping monolith. 80/**900 %**/25/100 · Heavy/Linked.
12. **Total Annihilation** — the no-playing-safe flag. **100/20 %**/50/100 · Heavy · Up 150, Down
    150, Grip +12 — crest ≤ 3 dB, jaws welded shut.
13. **Envelope Eater** — the destruction-decade demo. 90/**5 %**/20/100 · Classic · the followers
    track the waveform: bass turns to fuzz, transients to static — OTT as a distortion.

Level law: all 13 land within ±2 dB RMS of bypass on the reference chord (the fb345 preset-level-
spread lesson — no Gargle-+28 outliers).

---

## 15. Sources

**Primary / code-level**
* Vital source (GPL, Matt Tytel) — the exact OTT-class math + constants:
  https://raw.githubusercontent.com/mtytel/vital/main/src/synthesis/effects/compressor.cpp ·
  https://raw.githubusercontent.com/mtytel/vital/main/src/synthesis/effects/compressor.h ·
  https://raw.githubusercontent.com/mtytel/vital/main/src/common/synth_parameters.cpp
* Serum 2 User Guide (official PDF, Compressor pp.162-164, FX rack):
  https://www.xferrecords.com/manual/serum-2/docs · What's New in Serum 2:
  https://static.xferrecords.com/Serum%202%20What's%20New.pdf
* Steve Duda on Serum-MB vs OTT: https://xferrecords.com/forums/general/serum-fx-multiband-compressor-ott
* Xfer OTT product/params/latency: https://www.kvraudio.com/product/ott-by-xfer-records ·
  https://splice.com/plugins/3788-ott-vst-au-by-xfer-records

**The effect, dissected**
* EDMProd OTT teardown (crossovers 88.3/2.5 k, ratios, thresholds): https://www.edmprod.com/ott-plugin/
* Sound on Sound on Multiband Dynamics (Above/Below mechanics, reciprocal ratios, Time/Amount):
  https://www.soundonsound.com/techniques/multiband-dynamics-plug
* Ableton Live 12 manual, audio effect reference: https://www.ableton.com/en/manual/live-audio-effect-reference/
* MusicRadar "What is OTT compression": https://www.musicradar.com/music-tech/plugins/its-loud-in-your-face-and-got-more-punch-than-a-kangaroo-at-boxing-practice-what-is-ott-compression-and-how-do-you-use-it
* Xfer OTT UI walkthrough: https://virtualplaying.com/multiband-compressor-xfer-ott/ ·
  https://unison.audio/ott-plugin/ · https://songmixmaster.com/how-to-use-xfer-ott-compressor
* Pro-MB OTT recreation (+TDR Nova numbers): https://larslentzaudio.wpcomstaging.com/2019/02/24/creating-the-xfer-records-ott-in-fabfilter-pro-mb/

**Competitive field**
* Slate MO-TT docs (Amounts, Timing styles, stereo link): https://docs.slatedigital.com/MO-TT/MO-TT.html
* BPB OTT-alternatives survey (Cramit, Dynastia, 8TT, Squash, Evil Otto, Polarity-MD):
  https://bedroomproducersblog.com/free-vst-plugins/ott/
* Kilohearts Multipass: https://kilohearts.com/products/multipass
* Arturia Bus FORCE: https://www.arturia.com/products/software-effects/bus-force/overview ·
  Comp DIODE-609: https://www.arturia.com/products/software-effects/comp-diode-609/overview

**DSP theory**
* Giannoulis, Massberg, Reiss — "Digital Dynamic Range Compressor Design — A Tutorial and Analysis",
  JAES 60(6), 2012 (soft knee, branching smooth detectors, feed-forward log-domain verdict):
  https://www.aes.org/e-lib/browse.cfm?elib=16354 ·
  https://www.semanticscholar.org/paper/f1b20a5681e6ef7080e5b5fbce81911c6873543c
* Andrew Simper — "Solving the continuous SVF equations using trapezoidal integration" (the TPT SVF
  already shipped at TerrainFilters.h:317): https://cytomic.com/files/dsp/SvfLinearTrapOptimised2.pdf
* Linkwitz–Riley crossover primer (LR4 in-phase/allpass-sum properties):
  https://www.ranecommercial.com/legacy/note160.html · https://www.linkwitzlab.com/filters.htm

*(End of bible. Build order suggestion: LR4Split + harness `comb` gate → followers + gain computers +
`engage`/`levelstep` gates → calibration table freeze per Type → chassis/params → Jaws viz → presets →
the three fb305 edits last, with a full-rack regression.)*
