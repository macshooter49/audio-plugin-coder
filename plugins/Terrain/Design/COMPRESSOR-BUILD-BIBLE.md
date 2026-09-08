# Terrain Instrument — Compressor Build Bible

> The 4th flagship FX device. One device, **8 Types × 8 Characters = 64 voicings**, one back-8.
> Written 2026-08-14 from primary-source research (UA/Teletronix/Fairchild/dbx/SSL history, the
> Giannoulis–Massberg–Reiss JAES tutorial, Cytomic/Simper, u-he Presswerk, FabFilter Pro-C 2,
> Arturia FET-76 / VCA-65 / TUBE-STA / DIODE-609, Serum 2, Xfer OTT, Airwindows) plus verified
> line-level recon of the Terrain tree. A builder should be able to implement from this file alone.

---

## 0. Scope decision

**One compressor device, single-band, zero latency.** The Type dropdown carries the topology
(the thing hardware lineages actually differ by); Character carries the per-topology voicings —
exactly the distortion device's architecture (23 modes / 6 families → here 8 types / 1 family,
so **one back-8 serves all types**, no relabel maps needed).

**The OTT boundary (LOCKED proposal, confirm in §13):** Serum 2's Compressor has a `Multiband`
button that switches to 3-band combined upward+downward compression (the OTT engine — Xfer's own
forum confirms "effectively the same as stand-alone OTT"). We do **not** put multiband in this
device. The boundary:

* **THIS device** owns everything single-band: all downward topologies, *and* single-band
  upward+downward levelling (the `Squeeze` type — precedent already shipped in-tree as the
  SHAPER family's `Squash` P8, DistortionEngine.h:2259-2270, a 3 ms/80 ms 20:1 leveller).
* **A future OTT device** owns 3-band up/down with per-band Depth/Time — it needs its own
  crossover viz and per-band UI and would blow the 11-param chassis here.

**Zero/low latency mandate (VERDICT: ZERO, no lookahead, non-negotiable).** Lookahead buys
overshoot-free limiting at the cost of N samples of latency. The distortion bible §4.4 documents
why a latency-reporting FX device is a trap in this rack: the fb305 main-send exclusion math
subtracts the routed dry from the mix **sample-aligned** (PluginProcessor.cpp:7159/:7326/:7358);
a device that delays its wet path misaligns the subtraction and the dry leaks back phase-smeared.
The compressor therefore uses **no lookahead anywhere**. The `Limit` type controls overshoot the
way the in-tree fb264 master limiter already does (PluginProcessor.cpp:3753 — 0.8 ms one-pole
attack, stereo-linked) plus a soft-clip safety catch. Overshoot at 0.8 ms attack is ≤ ~1 dB on
program material; that is the documented, accepted price of zero latency.

---

## 1. History and circuits — the lineage that defined the effect

Every compressor is four blocks: **detector** (what it hears) → **gain computer** (the static
curve: threshold/ratio/knee) → **ballistics** (attack/release smoothing) → **gain element**
(what actually turns the volume down, and what colors the sound while doing it). The lineages
differ in *which block dominates*:

| Era | Circuit | Gain element | Detector topology | What it gave the world |
|---|---|---|---|---|
| 1930s–50s | **Vari-mu** (Western Electric → Fairchild 660/670, 1959) | Remote-cutoff tube: amplification factor falls as control voltage rises | Feedback, full-wave rectifier into RC networks | Ratio that **increases with drive** (the curve gets steeper the harder you hit it), push-pull H2 warmth, the 6-position time-constant switch (0.2 ms/0.3 s up to the program-adaptive position 6: 0.3 s single peaks / 10 s multiple / 25 s sustained) |
| 1962 | **Opto** (Teletronix LA-2A, T4 cell) | Electroluminescent panel + photoresistor | Feedback; the cell IS the detector | **Two-stage program-dependent release**: ~40–80 ms to 50 % recovery, then 0.5–5 s for the rest (UA manual: "depending upon the amount of previous reduction") — the slow half **remembers** how long the cell has been lit. Fixed ~10 ms attack. Frequency-dependent (more sensitive to highs) |
| 1967 | **FET** (UREI 1176, Bill Putnam) | FET as voltage-controlled resistor | Feedback, no threshold control — you drive Input into a fixed threshold | **Microsecond attack** (20–800 µs), release 50–1100 ms, ratios 4/8/12/20:1, program-dependent everything, and the accidental **All-Buttons mode** ("British mode"): ratio wanders 12–20:1, curve goes plateau-shaped, attack/release go erratic — drums explode |
| 1976 | **dbx 160 / OverEasy** (Blackmer; dbx founded 1971) | Decilinear VCA (log-domain gain cell) | **Feedforward**, true-RMS detector (the 165/160X added OverEasy) | Wide soft knee ("OverEasy"), exact dial-a-ratio including **∞:1 and beyond — negative ratios** (output falls as input rises; the 160X/160A/165A "Infinity+"), RMS = transient-blind musical levelling |
| 1976 | **SSL 4000 bus compressor** | dbx-derived VCA | Sidechain shared across L/R (one gain for both) | **"Glue"**: 2/4/10:1, attack 0.1–30 ms stepped, release 0.1–1.2 s **plus Auto** (dual-time-constant), the mix-bus sound of the 80s–now. Cytomic's The Glue (Andrew Simper) is the reference model; its documented trick: diodes make attack **level-dependent** — small overs attack slowly, big overs attack fast |
| 1990s–now | **Digital clean** (Massey, Pro-C 2 Clean, JUCE-era) | Multiplier | Feedforward, log-domain, smooth-branching detector | The curve you draw is the curve you get. Giannoulis/Massberg/Reiss (JAES 2012) is the canonical tutorial: feedforward beats feedback in predictability; log-domain detection beats linear; smooth branching beats decoupled for low ripple |
| 2013 | **OTT** (Xfer, from Ableton's Multiband Dynamics preset) | Multiplier ×3 bands | Feedforward up+down per band | Upward compression as a *sound* — quiet detail surging to the front. Depth/Time/In/Out + per-band up/down |

**Why synth-FX compressors exist at all** (the Serum 2 lesson): inside a synth, a compressor is
not a mixing utility — it is a **motion effect**. It converts level variation into timbral and
envelope animation: pumping pads, snapped plucks, breathing reverb tails, gated stutter. Serum 2
ships Threshold/Ratio/Attack/Release/Gain + Multiband, and its Ratio at maximum becomes **"Limit"
— a true peak limiter circuit**, not just a big ratio. FabFilter Pro-C 2 ships eight *styles*
(Clean, Classic, Opto, Vocal, Mastering, Bus, Punch, Pumping) — proof that topology-as-a-dropdown
is the modern standard. u-he Presswerk exposes the detector itself (FF/FB/INT, link %, up to
−60 dB of GR at 20:1). Arturia sells four separate compressors (FET-76, VCA-65, TUBE-STA,
DIODE-609) that are literally our Type dropdown as four products.

---

## 2. ⚡ THE BUS REALITY + no-playing-safe

### 2.1 The −26 dBFS law applied to thresholds (LAW 1 — the one that sinks copied designs)

Measured (fb299 comment, PluginProcessor.cpp:26/:46): a single note bounces at **−20 dBFS**
pre-makeup at the master; the FX bus receives the voice mix through `kVoiceToFxPad = 0.5f`
(PluginProcessor.cpp:6300-6301), so **a single note arrives at this device at ≈ −26 dBFS
(0.050 linear); a 3-note chord ≈ −20 dBFS.** Every threshold in every manual is stated against
0 dBFS program. Copy the LA-2A's or Pro-C's default threshold of −20…−30 dBFS and the program
*never crosses it*: the device ships dead — the compressor version of the fb286 Diffusion failure.

**THE HOUSE LAW — program-relative dB (dBp):** the device lifts its detector input by a fixed
**+26.02 dB** (`xd = x * 20.0f`) so all internal math lives in **dBp, where 0 dBp = −26 dBFS =
single-note nominal**. The chord sits ≈ +6 dBp. Every threshold, ceiling, gate and makeup number
in this bible is in dBp. (Audio path is untouched — only the detector is lifted; this is the
distortion §2.2 "internal trim" pattern.)

```
Push (front knob, 0..1):   T_dBp = +9 − 48 · push^0.9
   push 0    → +9 dBp  (3 dB above the chord — zero GR, bit-transparent)
   push 0.25 → −5 dBp  (single notes ~5 dB over — gentle, immediately audible)
   push 0.5  → −16 dBp (10–20 dB GR on program — deep pump territory)
   push 1.0  → −39 dBp (everything above the noise floor is over threshold — total crush)
```

The `^0.9` keeps the travel near-linear in dB (the ear hears dB — distortion §2.5 law) with a
slight stretch at the top so the last 20 % still visibly deepens.

### 2.2 Ratio law

```
knob t ∈ [0,1]:  (1 − 1/R) = t^0.85   ⇒   R = 1 / (1 − t^0.85)
   t=0.25 → 1.4:1 · t=0.5 → 2.2:1 · t=0.75 → 4.6:1 · t=0.9 → 11.7:1 · t≥0.995 → ∞:1
```

GR-per-dB-over is `(1 − 1/R)` — making *that* linear in the knob means every degree of travel
adds the same audible dB of squash: no dead first third, no wasted top. Readout shows the ratio
(`"4.4 : 1"`, `"∞ : 1"`). **OverEasy extends the same knob past ∞** (§6.6): its top 15 % of
travel maps slope 0 → −1, i.e. output FALLS 1 dB per input dB — dbx's documented "Infinity+"
negative ratios. No other type unlocks that region.

### 2.3 🔑 The extremity table (what 100 % actually does)

| Control at max | What happens | Why it ships anyway (NO PLAYING SAFE) |
|---|---|---|
| **Push 100** | Threshold 39 dB inside the program: every note, tail and release is over threshold; with ∞:1 the synth plays at ONE level — total dynamic annihilation | This is OTT-depth flattening, the modern sound. The movement 0→100 is all live because T falls in dB linearly |
| **Attack 0.02 ms + Release 20 ms + Push high** | The gain tracks the waveform *inside the period* of anything below ~500 Hz: the compressor becomes a **waveshaper** — measured THD > 10 % on an 80 Hz sine. Gated, torn, buzzing | This is precisely how a real 1176 distorts LF at 20 µs attack — the classic "release faster than the wave" grind. The bottom decade of Release is deliberately spent there (the ANALOG `Recovery` precedent, distortion §5.5 P5) |
| **Hold 250 ms + fast release + deep Push** | GR freezes at max for a rhythmic beat then snaps back: **gated pumping** — the tempo-feel stutter without a gate device | Hold is what separates "compressor chatter" from "intentional pump"; at 0 it is bit-bypassed |
| **Punch −100** | Attack transients get +24 dB of *extra* GR: every pluck is vaporised, notes become swells | Reverse-transient design tool; mirrors CLIP P7's negative pole (distortion §5.5) |
| **Punch +100** | The detector goes blind for the first ~3 ms of every hit: transients escape untouched over a crushed sustain — the snap-enhancer | The most requested "punch" behavior in modern comps, free from our existing follower |
| **OverEasy / `Anti` character** | Slope −1 everywhere: playing louder makes it QUIETER. A crescendo comes out as a diminuendo | dbx's 160X/160A/165A shipped this ("Infinity+" — dbx's own spec: ratio 1:1 → ∞:1 thru −1:1); nobody else in soft-synth land does. Night-and-day by construction |
| **Squeeze at max** | −40 dBp passages raised to program level: reverb tails, key-release bleed and room tone SURGE between notes — the single-band OTT breath | The gate law (§4.3) keeps true silence silent, so it dies with the note (LAW 6) |
| **Limit + `Loud War` character** | Full auto-makeup against a program-relative ceiling: crest → ~1, loudness war in a box | It is a *character*, opt-in, never the default |

**Stability clamps (BIBO, never taste):** attack floor 0.02 ms ≈ 1 sample @ 48 k (knob labels
below it — Glue's documented 0.01 ms stop — are legal, and clamp to the same one-sample attack;
the floor is a FB-topology stability clamp, and Glue is FF); release floor
20 ms; detector smoothing ≥ 0.1 ms in feedback topologies (§3.4); GR clamp [0, 60] dB; upward
boost cap +24 dB with the silence gate; final `DelayEngine::softClip` (DelayEngine.h:315-321)
after makeup as the NaN/overshoot net.

---

## 3. DSP core — detector math, curves, ballistics

Canonical reference: Giannoulis, Massberg, Reiss, *Digital Dynamic Range Compressor Design — A
Tutorial and Analysis*, JAES 60(6), 2012. Architecture (their recommended, ours): **feedforward,
log-domain detection, gain computer before ballistics, smoothing in dB.**

### 3.1 The block chain (per sample, stereo)

```
in L/R ──┬───────────────────────────────────────────────────────► × g[n] ──► Color ──► Lift ──► Mix
         │
         └► link mix ► Hear Cut HP ► lift +26 dB ► |x| or RMS ► 20·log10 ► static curve ► ballistics ► g[n]=10^(−GR/20)
            (P7 Link)   (P4, detector-only)         (Detect d2)              (Push/Ratio/Knee)  (P1/P2/P6 + type)
```

* **Link (P7):** `d = link·max(|L|,|R|) + (1−link)·|ch|` — at 100 % one detector drives both
  channels (solid image, SSL-style); at 0 % dual-mono (lopsided breathing, wide). Presswerk
  exposes exactly this as a 0–100 % control.
* **Hear Cut (P4):** 1-pole HP **in the detector only** (recycle `TPTOnePole`,
  TerrainFilters.h:83, in HP configuration): the compressor stops *hearing* lows, so bass stops
  pumping the whole patch. Off(20 Hz)…500 Hz, exp taper. Mandatory internal sidechain filter —
  every serious reference (Glue, FET-76, Presswerk, Pro-C 2) ships one.
* **Detect (back d2):** `Auto` (type-native, default) · `Peak` (instant attack rectifier) ·
  `Smooth` (RMS, 10 ms window: `e2 += (x²−e2)·aw; d=√e2`) · `Slow` (RMS 50 ms — dbx-style
  transient-blind) · `Spike` (peak + 5 ms hold — pointiest).

### 3.2 The static curve (gain computer) — exact

Soft-knee formula (Giannoulis eq. 4), input `xG` in dBp, threshold `T`, ratio `R`, knee width
`W` (P3, 0–24 dB):

```
if  2(xG − T) < −W :  yG = xG                                   (below knee)
if |2(xG − T)| ≤ W :  yG = xG + (1/R − 1)·(xG − T + W/2)² / (2W) (inside knee, quadratic)
if  2(xG − T) >  W :  yG = T + (xG − T)/R                        (above knee)
GRtarget = xG − yG        (≥ 0 dB of reduction)
```

For OverEasy's negative region replace `1/R` with slope `s ∈ (0, −1]` — same formula, `s`
continues smoothly through 0 (∞:1) into negative. For `Squeeze` add the upward branch (§3.6).

### 3.3 Ballistics — smoothing in the dB domain

**Smooth-branching one-pole on GR** (the paper's low-ripple recommendation):

```
aA = 1 − exp(−1 / (fs·τA)),  aR = 1 − exp(−1 / (fs·τR))     (τ = time to 63 %)
GR[n] += (GRtarget − GR[n−1]) · (GRtarget > GR[n−1] ? aA : aR)
```

Attack/release curve *shapes* are a Type property (this is most of what makes topologies sound
different):

| Shape | Math | Used by |
|---|---|---|
| Exponential (RC) | the one-pole above | Clean, FET, OverEasy |
| **Level-adaptive attack** (diode conductance — The Glue's documented trick) | `τA_eff = τA / (0.2 + 0.8·min(1, over/12dB))` — small overs attack up to 5× slower | Glue |
| **Dual-pool release** (auto-release) | two release one-poles τ 0.15 s and 2.5 s driven in parallel; `GR = max(fast, slow)` — short bursts recover fast, sustained GR recovers slow | Glue `Auto`, Vari-Mu positions 5/6 |
| **Two-stage + memory** (T4 cell) | §3.5 | Opto |
| **2nd-order damped** (ζ = 0.6 on the smoother) | adds a controlled release overshoot-wobble | FET `All Buttons` — the erratic pump |

### 3.4 Feedforward vs feedback — and the loop-gain law (LAW 6)

`FET`, `Opto` and `Vari-Mu` are **feedback** topologies (§6 table): the detector taps the
*output* (one sample late): `d[n] = |y[n−1]|` — for Opto that tap feeds the T4 model's two
pools (§3.5), same loop shape. Consequences, stated honestly:

* Ratio becomes program-dependent (measured ratio drifts upward with drive — the hardware
  behavior; this is the *point* of the topology, cite Presswerk's FB = "musical, loose").
* **Loop-gain accounting:** the loop is detector → GR → gain `g ≤ 1` → output → detector. Since
  `g ∈ (10^{−60/20}, 1]` the static loop is a monotone contraction — **BIBO-stable by
  construction, max static loop gain = 1.0 at zero GR**. The dynamic risk is limit-cycle chatter
  when attack < 1 sample: clamped by the 0.02 ms attack floor and a mandatory ≥ 0.1 ms one-pole
  on the feedback detector tap. There is no configuration that free-runs: zero input ⇒ detector
   0 ⇒ GR → 0 ⇒ silence. Sound dies with the note.
* Everything else is **feedforward** (Giannoulis: predictable, surgical) — including `Squeeze`,
  where feedback + upward gain would be a genuine runaway loop (upward gain > 1 inside a loop ⇒
  divergence). **Upward compression is feedforward-only, forever.**

### 3.5 Program-dependent release — the Opto two-stage math (exact)

T4 cell measured behavior (UA manual + GroupDIY): attack ≈ 10 ms; release ~40–80 ms to 50 %,
then 0.5–5 s for the rest, slower the longer/harder it has been lit (UA manual verbatim:
"approximately 0.06 seconds for 50% release, 0.5 to 5 seconds for complete release depending
upon the amount of previous reduction"). Model with two pools + a memory integrator:

```
attack:  F += (GRt − F)·a10ms ;  S += (GRt − S)·a10ms          (both charge together)
release: F −= F·(1/(fs·0.06))  ;  S −= S·(1/(fs·τS))
memory:  M += (GR − M)·aM,  τM = 10 s                           (how lit the cell has been)
         τS = 0.5 s + 4.5 s · min(1, M / 6 dB)                  (worn cell = slow tail)
GR = 0.55·F + 0.45·S
```

Discriminator (§6.3): a double-exponential fit to the release trajectory separates two time
constants ≥ 10× apart — no single-pole type can fake it — and a 10-second pump test measurably
slows the tail (memory).

### 3.6 Upward compression (`Squeeze` only) + the silence gate

```
T_up = T − 18 dB ;  R_up = 3:1
xG < T_up :  boost = min( 24·ratioKnob , (T_up − xG)·(1 − 1/R_up) )   [dB, gain > 1]
GATE (LAW 6): below −45 dBp the boost fades linearly to 0 over 12 dB
             (−45 → full boost, −57 → zero). True silence stays silent;
             tails ride up and then DIE with the note. No free-running hiss lift.
```

### 3.7 Auto-makeup — the fb249 CHORD law

fb249's lesson (master level): calibrate makeup to the **CHORD**, not a sine — a summed chord
sits ~+6 dB over a single note and clipping/over-lifting a chord is broadband IMD. Therefore:

```
reference level L_ref = +6 dBp   (the measured 3-note chord at this bus — §2.1)
makeupDb = 0.7 · staticCurveGR(L_ref)      (70 % compensation — distortion §4.2 precedent)
slewed on a 300 ms one-pole; hard cap +24 dB
```

`Auto` (front pill) ships **OFF** — full compensation converts a Push sweep into a timbre-only
change and deletes the "louder AND denser" that reads as power (distortion §2.6 item 1). The
70 %/300 ms numbers keep Sag-like pumping audible when ON.

### 3.8 Color — the gain element's nonlinearity (envelope-gated by construction)

P8 `Color` cross-fades a per-type nonlinearity applied to the OUTPUT, scaled by current GR
(no GR ⇒ bit-clean at any Color):

```
k = Color · min(1, GR / 12 dB)
FET:      y += k·0.15·tanh(3y)−…  odd harmonics, hard  |  Vari-Mu: asym tanh, H2-dominant
VCA/dbx:  decilinear crunch (tanh of log-domain error) |  Opto/Glue/Clean: gentle H3 ≤ 0.5 %
```

Because `k` rides GR, Color **cannot** sound on silence (LAW 6) and it *breathes with the
compression* — the measured signature of driven hardware. Any asymmetric Color engages a
`DCBlocker` (TerrainFilters.h:69) after the stage — engaged only when Color > 0 (bit-clean off).

### 3.9 Oversampling verdict

**None, at any Quality tier.** The gain signal is a low-rate modulator; multiplying by it creates
only ±sideband spread around program partials (that IS compression). The two aliasing-adjacent
zones are (a) sub-ms ballistics — which are the *point* (the 1176's AM-at-audio-rate grind;
hardware aliases against physics the same way) and (b) `Color` saturation — tanh-grade at ≤ 0.15
mix, measured alias floor < −70 dBFS at the bus level; not worth 4× CPU. This is the cheapest
flagship device in the rack, and that is a feature (LAW 8).

### 3.10 Parameter laws (range · taper · glide) — every knob

All continuous params glide on a 20 ms one-pole (LAW 7). Threshold/makeup glide **in dB**, gain
applied linear. Type/Character switches: **state carry-over** — the new type inherits the current
GR envelope and glides its constants over 30 ms; since the applied gain is a continuous signal,
no dual-engine crossfade is needed and switches are click-free by construction (LAW 4).

| Param | Range | Taper | Notes |
|---|---|---|---|
| Push | T = +9 → −39 dBp | `t^0.9` in dB | §2.1 |
| Ratio | 1:1 → ∞:1 (OverEasy: → −1) | `(1−1/R) = t^0.85` | §2.2 |
| Lift | 0 → +24 dB | linear-in-dB | output makeup |
| Mix | 0 → 100 % wet | linear, equal-gain | pure-gain wet ⇒ phase-safe parallel at ANY mix (§8) |
| Attack (P1) | per-type window (§6 table) | log | floor 0.02 ms |
| Release (P2) | per-type window | log | floor 20 ms; bottom decade = intentional LF grind |
| Knee (P3) | 0 → 24 dB | linear | OverEasy floors it at 6 |
| Hear Cut (P4) | Off(20) → 500 Hz | exp | detector-only HP |
| Punch (P5) | −100 → 0 → +100, detented | linear | §4.2 |
| Hold (P6) | 0 → 250 ms | linear | 0 = bit-bypassed |
| Link (P7) | 0 → 100 % | linear | default 100 |
| Color (P8) | 0 → 100 | linear | 0 = bit-clean |

---

## 4. Engineering musts

### 4.1 Unity-through discipline (the default is bit-transparent)

Defaults: Push 0 (T = +9 dBp, above program) · Ratio 2.2:1 · Lift 0 · Mix 100 % · Color 0 ·
Power OFF. With zero GR the wet path is `y = x·1.0` — **byte-identical pass-through**, same
default-silence contract as reverb/delay/distortion (distortion §5.7). Turning Power ON changes
nothing until Push moves. This is LAW 5's "no dead zone" *and* the rack's unity law at once.

### 4.2 Punch — the transient lane (recycled, verified)

The exact follower already ships: DistortionEngine.h:710-711 with `kPunchAtk = 0.0064f` (~3 ms)
and `kPunchRel = 0.00026f` (~80 ms) @48 k (DistortionEngine.h:3022-3023), block-rate corrected
at :590-591. Transientness `p = clamp01((fastEnv − slowEnv)·8)`:

* Punch **+**: detector input dB reduced by `24·p·punch` → the first ~3 ms escape clean.
* Punch **−**: `GRtarget += 24·p·|punch|` → attacks smashed into swells.

### 4.3 Denormals / NaN / DC traps

* `log10(0)` → −inf: detector floor `d = max(d, 1e-7)` (−140 dBFS ≈ −114 dBp; below the gate).
* Envelope flush: `DelayEngine.h:330` `flush()` idiom on F/S/M/GR each block + `ScopedNoDenormals`.
* Gain path generates **no DC** (pure multiply); only asymmetric Color does — blocker per §3.8.
* RMS state `e2` can go negative via float cancellation — `e2 = max(e2, 0)` before sqrt.

### 4.4 ⚠️ The 4th send bus re-breaks fb305/fb338 unless the exclusion sums are edited

The law (fb338, verified in-tree): **EVERY send bus joins EVERY main-send exclusion.** Today's
sums read `(rvbSend + dlySend + dstSend)` at PluginProcessor.cpp:**7159**, **:7326**, **:7358**
(plus the read-pointer setup at :6457-6458 and the per-voice send accumulation). Adding
`cmpSendL/R` means editing **every one of those lines** plus the buffer alloc/clear, or the
routed dry leaks back phase-inverted through the other devices' exclusions. This is the exact
:6979/:7111 landmine the distortion bible §4.5 called — it moved; chase the comment
`fb305 law: EVERY send bus joins EVERY main-send exclusion`, not the line numbers.

### 4.5 IndyFxChain

`IndyFxChain.h` owns a private copy of every global FX module for the chop path (it already
wraps MoogDelay at :29/:281). **The compressor must be added there too** or it silently vanishes
on chopped voices (distortion §5.7 carried the same warning).

### 4.6 The APVTS block (greenfield — the 4-point relay trap applies in full)

No `SYN_CMP_*` exists in ParameterIDs.hpp (verified by grep). Clone the SYN_DST grammar at
ParameterIDs.hpp:406-435 verbatim:

```
SYN_CMP_TYPE (choice 8) · SYN_CMP_CHARACTER (choice 8, per-type) · SYN_CMP_DETECT (choice 5)
SYN_CMP_PUSH · SYN_CMP_RATIO · SYN_CMP_LIFT · SYN_CMP_MIX          (floats 0..1)
SYN_CMP_P1..P8 · SYN_CMP_SRC_A/B/C/D/SUB/NOISE (bools, default OFF)
SYN_CMP_POWER (default OFF) · SYN_CMP_AUTO (pill, default OFF) · SYN_CMP_PILL2 (`Grab`)
```

Read every choice as the **INDEX** via `getRawParameterValue` (the CLAUDE.md §4 law, precedent
PluginProcessor.cpp:5860). Every param needs the full WebSliderRelay 4-point binding or it
builds clean and silently no-ops. Character list swaps per Type follow the fb342 session law ①
(choice-param cardinality: allocate the max 8 entries, relabel).

---

## 5. Chassis map — the 11 params on the locked fb275 chassis

Type lives in the **device header pill** (the shipped pattern — reverb/delay/distortion all
select type there; back d1/d2 remain Character + a second selector, per fx-back-panel-official-spec).

### 5.1 Front — 3 + Mix, 2 pills

| Slot | Name | Range | What it does (pragmatic name test) |
|---|---|---|---|
| 1 | **Push** | 0–100 | How far the compressor digs into the sound. 0 = touches nothing, 100 = everything is over threshold. (It IS the threshold, inverted and program-calibrated — §2.1) |
| 2 | **Ratio** | 1:1 → ∞:1 | How hard it holds what it catches. Readout `"4.4 : 1"`. OverEasy continues past ∞ into the swallow zone |
| 3 | **Lift** | 0 → +24 dB | Brings the level back up after squashing |
| 4 | **Mix** | 0–100 % | 100 % = fully wet (hard rule). 30–60 % = parallel/NY compression, phase-safe by construction |
| pill | **Auto** | off/on | Auto-makeup, chord-calibrated 70 %/300 ms (§3.7). Default OFF |
| pill | **Grab** | off/on | Attack AND release ×0.25 — the whole device gets grabbier, one click (the Decapitator-Punish precedent: split resolution, buy reach) |

### 5.2 Back — 2 dropdowns + 8 knobs (4×2)

* **d1 `Character`** — 8 voicings per Type (§6), 64 total. Physics changes, never EQ.
* **d2 `Detect`** — Auto · Peak · Smooth · Slow · Spike (§3.1). `Auto` = the type's native ears.

| Slot | Name | Range | Role |
|---|---|---|---|
| P1 | **Attack** | per-type (§6 table) | How fast it grabs. Log taper; readout in ms/µs |
| P2 | **Release** | per-type | How fast it lets go. Bottom decade = LF-grind zone, on purpose |
| P3 | **Knee** | 0 – 24 dB | Razor corner → OverEasy round-off |
| P4 | **Hear Cut** | Off – 500 Hz | Cuts lows out of what the compressor HEARS — kicks/bass stop pumping the patch. Detector only, audio untouched |
| P5 | **Punch** | ±100, detent 0 | + : transients escape clean · − : transients smashed into swells (§4.2) |
| P6 | **Hold** | 0 – 250 ms | Freezes the clamp before release runs — pump shaping, bass de-chatter |
| P7 | **Link** | 0 – 100 % | 100 = one clamp for L+R (solid) · 0 = each side breathes alone (wide, lopsided) |
| P8 | **Color** | 0 – 100 | How much the gain element distorts while working. GR-gated: silent patch = zero color |

Matched-pair law: Attack+Release are the pair, both always present. Punch's depth-only design is
legal because its "rate" is the fixed measured 3/80 ms follower (a constant of the transient
definition, not a musical axis — same argument that shipped CLIP P7).

---

## 6. §Types — 8 topologies, each with recipe, characters, and the measurable discriminator

Per-type ballistic windows (knob spans the window, log):

| Type | Attack window | Release window | Native Detect | Topology |
|---|---|---|---|---|
| Clean | 0.05 – 300 ms | 20 – 2500 ms | Peak | FF |
| Glue | 0.01 – 30 ms | 100 ms – 1.2 s (+Auto chars) | Peak, link 100 forced-default | FF + diode attack |
| FET 76 | **0.02 – 0.8 ms** | 50 – 1100 ms | Peak | **FB** |
| Opto | 2 – 50 ms | scales fast pool 40–200 ms; slow pool via memory | RMS 10 ms | **FB** |
| Vari-Mu | 0.2 – 50 ms | 0.2 – **25 s** | RMS 5 ms | **FB** |
| OverEasy | RMS window 1 – 80 ms (attack knob = window) | 40 ms – 2 s | **RMS true** | FF |
| Squeeze | 0.5 – 100 ms | 20 ms – 1 s | RMS 10 ms | FF only (§3.4) |
| Limit | 0.1 – 5 ms | 20 – 500 ms | Peak+hold 1 ms | FF |

### 6.1 Clean — the reference ruler *(digital VCA, feedforward)*
Recipe: §3.1-3.3 verbatim, exponential ballistics, exact knee. The curve you set is the curve
you measure. **Discriminator:** static-curve error < 0.1 dB vs the drawn curve; added THD ≈ 0 at
release > 100 ms — no other type passes both.
Characters: `Precise`(default) · `Soft Touch`(knee auto-widens +6 dB below 6 dB GR) ·
`RMS Ears`(forces RMS 10) · `Peak Ears`(forces peak) · `Deep Release`(τR scales ×(1+GR/12) —
deeper GR releases slower) · `Line Attack`(linear-in-dB attack ramp, not RC — punchier onset) ·
`Silky`(critically-damped 2nd-order smoother — zero ripple) · `Bounce`(ζ=0.5 underdamped — a
controlled release overshoot wobble).

### 6.2 Glue — the SSL bus *(The Glue lineage)*
Recipe: feedforward + **level-adaptive diode attack** + **dual-pool auto release** (§3.3 table),
link forced to 100 by default (the console's one-sidechain-for-both law), knee ~4 dB.
**Discriminator:** burst test — release measured after a 50 ms burst vs after 3 s of GR differs
≥ 3× (auto-release adaptation); crest factor of a full mix converges (the "glue" metric) while
per-note envelopes stay intact.
Characters: `Quad Bus`(default, Auto release) · `Fixed 400`(0.4 s fixed) · `Two Gentle`(2:1
cap, slow attack — mastering) · `Ten Punchy`(10:1, 30 ms attack) · `Fast City`(0.01 ms attack —
The Glue's documented ultra-fast extremity) · `Big Console`(Color floor 25, link 100 locked) ·
`Pump Bus`(release fixed 100 ms — the EDM breathe) · `No Diode`(linear attack — surgical modern).

### 6.3 FET 76 — the microsecond machine *(1176 lineage, feedback)*
Recipe: feedback detector tap (§3.4); attack knob spans **20–800 µs** (the hardware's entire
legendary range — at our bus this is the "compressor as waveshaper" zone below 500 Hz, §2.3);
threshold fixed-ish: Push maps to detector *input drive* (the 1176 has no threshold — you crank
Input; identical math, honest lineage); ratio knob picks 4/8/12/20-shaped curves continuously;
Color defaults 20 (FET odd harmonics under GR).
**Discriminator:** GR onset < 1 ms on a step; measured ratio drifts upward with drive (FB
signature); added THD > 1 % at 10 dB GR — three metrics no FF type reproduces together.
Characters: `Blackface`(default, rev D-ish, cleaner) · `Blue Stripe`(rev A: Color +15, hotter,
less stable wobble) · **`All Buttons`**(the British mode: plateau curve — knee forced 10, ratio
wanders 12–20:1 via GR-coupled slope, 2nd-order ζ=0.6 release wobble = the erratic pump,
Color +10) · `Slam 20`(20:1 locked, fastest attack) · `Loose 4`(4:1, release drifts ±20 % with
program) · `Broken Bias`(asymmetric FET: H2-heavy Color, sputters at deep GR) · `Slow Fet`
(attack window ×20 — an 1176 that can wait) · `Two Pass`(two half-depth stages in series — the
serial-1176 vocal trick in one slot).

### 6.4 Opto — the remembering cell *(LA-2A / TUBE-STA lineage, feedback)*
Recipe: §3.5 exactly; attack ~10 ms nominal (knob 2–50); Release knob scales the FAST pool
(40–200 ms) — the slow pool obeys the memory law; ratio soft-capped ≈ 6:1 with a 12 dB knee
(the T4 barely knows what a ratio is); detector tilt +3 dB/oct above 2 kHz (the cell's
frequency dependence).
**Discriminator:** double-exponential release fit with τ-separation ≥ 10×, AND the history
test: after 10 s of sustained GR the tail slows measurably (M-integrator) — unique.
Characters: `T4 Classic`(default) · `Fresh Cell`(memory ×0.3 — snappy) · `Tired Cell`(memory
×3 — old-unit molasses) · `Fast Opto`(fast pool 20 ms — LA-3A flavor) · `Even Pools`(0.7/0.3
mix — wider two-stage audibility) · `Glass`(attack 25 ms, knee 16 — invisible vocal ride) ·
`Tube Stage`(Color floor 30, H2 — the amplifier half of the LA-2A) · `Bright Ears`(detector
tilt +6 dB/oct — highs duck the patch: free de-esser behavior).

### 6.5 Vari-Mu — the deepening curve *(Fairchild 670 lineage, feedback)*
Recipe: feedback; **ratio grows with GR**: `s_eff = s·(1 + GR/18dB)` clamped at ∞ — hit it
harder and the curve steepens (the remote-cutoff tube law); H2-dominant Color default 25;
release spans to 25 s (position-6 territory: a compressor that settles over a phrase, not a
note); dual-pool release on the `Auto Peaks`/`Long Haul` characters (the 670's program-adaptive
positions 5/6 — verified: pos 5 = 2 s single peaks / 10 s multiple; pos 6 = 0.3 s single /
10 s multiple / 25 s sustained).
**Discriminator:** dRatio/dGR > 0 measured on a level staircase (unique among all 8) + H2 > H3
in the Color spectrum.
Characters: `Studio 670`(default, TC pos 2: 0.2 ms/0.8 s) · `TC One`(0.2 ms/0.3 s) ·
`TC Four`(0.8 ms/5 s) · `Auto Peaks`(pos 5 dual-pool) · `Long Haul`(pos 6: the 25 s ocean) ·
`Push Pull Hot`(Color ×2, bias asym — driven iron) · `Lateral`(M/S detector split — the 670's
actual LAT/VERT mode: mid compressed, sides ride — instant width under compression) ·
`Triode Soft`(knee 18, ratio cap 4 — the gentlest thing in the device).

### 6.6 OverEasy — the wide knee and the negative zone *(dbx 160/165 lineage, feedforward)*
Recipe: true-RMS detector (attack knob = RMS window 1–80 ms; release exponential-decilinear);
knee floored at 6 dB, default 12 (the OverEasy patent behavior: compression begins *before*
threshold); decilinear Color (tanh in the log domain — the VCA's actual error mechanism);
**the ratio knob's top 15 % unlocks slope 0 → −1** ("Infinity+", §2.2).
**Discriminator:** measured knee width ≥ 10 dB; and past ∞ the output slope goes NEGATIVE —
input +6 dB ⇒ output −6 dB at max. Nothing else in the plugin, or in most of the market, does it.
Characters: `Over Easy`(default) · `Hard 160`(knee 0 VCA — the original 160) · `Infinity`
(∞:1 reached at knob 70 %, plateau top) · `Infinity Plus`(negative zone spread across the top
40 %) · `True Slow`(RMS 35 ms locked — transients sail through untouched) · `Crush RMS`(RMS
2 ms — RMS chatter grit, a genuinely strange texture) · `Decilinear Drive`(Color floor 30) ·
`Anti`(slope −1 everywhere above threshold — the swallow: crescendo in, diminuendo out).

### 6.7 Squeeze — single-band up+down leveller *(the OTT-boundary type, feedforward only)*
Recipe: downward per §3.2 **plus** upward per §3.6 with the −45 dBp silence gate; Ratio knob
scales BOTH slopes (one "flatten" axis — OTT's Depth feel); attack/release apply to both lanes.
In-tree precedent: the SHAPER `Squash` leveller (DistortionEngine.h:2259-2270) is this type's
downward+gain-riding half, already certified.
**Discriminator:** a −40 dBp probe comes out RAISED (measured gain > 1 — unique); dynamic-range
ratio out/in < 0.3 on a 24 dB staircase.
Characters: `Level Rider`(default) · `Deep Floor`(upward reach 24 dB) · `Only Up`(downward off —
pure upward compression, the rarest sound in the set) · `Only Down`(a dense leveller) ·
`Fast Squash`(5 ms both — the OTT-artifact zone; Serum-lore fix "attack down/release up" lives
in the manual copy) · `Slow Iron`(200 ms both — invisible levelling) · `Bright Bias`(detector
tilt: highs raised more — sheen) · `Vocal Sit`(Hear Cut forced 120 Hz + mid-weighted detector).

### 6.8 Limit — the wall *(Serum 2 "Limit" + fb264 lineage, feedforward)*
Recipe: threshold = **Ceiling** (Push relabels in readout); attack 0.1–5 ms; peak+1 ms-hold
detector; ∞ slope, knee 0–6; the fb264 recycle: 0.8 ms-class one-pole attack + stereo link
(PluginProcessor.cpp:3753 grammar) and `DelayEngine::softClip` engaged at ceiling+1 dB as the
overshoot net (§0 zero-lookahead verdict).
**Discriminator:** zero samples above ceiling+0.5 dB on any program; crest factor → ~1.05.
Characters: `Clean Wall`(default) · `Soft Ceiling`(knee 6 into the clamp) · `Hard Slam`(0.1 ms) ·
`Pump Limit`(release 400 ms, deep — the master-pump) · `Loud War`(Auto-makeup forced full — the
maximizer, opt-in only) · `Clip Guard`(softClip stage always on — limiting into clipping, the
mastering-chain cliché) · `Bounce Wall`(release τ scales with GR — bouncy) · `Leaky`(slope 6:1
above ceiling instead of ∞ — an "over-limit leak" that keeps 2–3 dB of life).

**The type tell (the §1.144 analog):** run a −40→+9 dBp staircase (1 dB / 200 ms) and record the
GR trajectory. Clean draws the textbook; Glue's steps *adapt*; FET overshoots in µs; Opto's steps
have two-slope tails that slow over the run; Vari-Mu's steps deepen non-linearly; OverEasy rounds
10 dB early then INVERTS; Squeeze pushes the bottom steps UP; Limit flatlines. Eight staircase
plots, eight unmistakable shapes — that plot is the certification artifact.

---

## 7. §Visualizers

### 7.1 How the greats draw compression (mechanisms, precisely)

| Reference | Mechanism |
|---|---|
| **FabFilter Pro-C 2/3** | The gold standard. (a) Scrolling **level display**: input as dark-grey filled envelope, output as light-grey fill on top, **gain reduction as a red line descending from the top edge** (GR depth = how far it dips); (b) **knee display**: static in/out transfer curve that **lights green at the segment the current input occupies**; (c) side meters: in/GR/out with peak+loudness readouts |
| **Ableton Compressor** | "Activity view": input level as light-grey region with the **orange GR curve** overlaid (or output in dark grey — switchable GR/Out); plus a transfer-curve view with a moving input dot |
| **Ableton/Cytomic Glue** | A single **VU needle** showing average GR — the analog idiom: needle kicks left with the music |
| **dbx 160 hardware** | 12-LED GR ladder — instant, crude, readable from across the room |
| **Serum 2 / Xfer OTT (multiband)** | Three horizontal band strips; per-band **GR bars squeeze inward from the edges** while thresholds are draggable on the strips themselves; band level animates behind |
| **Arturia FET-76 / TUBE-STA** | Skeuomorphic VU needle (GR mode), plus an advanced-panel time-domain in/out view; TUBE-STA animates tube glow with drive |
| **u-he Presswerk** | Large VU + numeric GR; the panel is knobs-first, viz-minimal |

The shared grammar: **input vs output over time + how much is being taken, right now.** The
knee curve is the *secondary* surface (parameter geometry); the level river is the *primary*
(what is it doing to MY sound).

### 7.2 Our card — 2 concepts + verdict (canvas, CPU-cheap, fb311-dramatic)

Data plumbing (one native, the shipped idiom): `getCmpViz` returns one JSON string at UI rate —
`{ gr, in, out, t, k[16] }` = current GR dB, in/out peaks (dBp), threshold dBp, and a 16-point
envelope history ring. Precedent: `getReverbBloom` (PluginEditor.cpp:952/:4862) and the
`__dstViz` push (PluginEditor.cpp:5577 → index.html:8163/:28384). Processor cost: 2 peak
trackers + a ring write ≈ 4 flops/sample.

**A. The Press River (hero).** A scrolling envelope river (recycle the delay echo-timeline
canvas grammar): the INPUT envelope drawn as a dim outline mountain; the OUTPUT envelope as the
bright filled body; **the gap between them is tinted** (the crushed dB made visible); and a
**ceiling bar** sits at the threshold height, physically pressing down — bar thickness = current
GR, its edge rounded by Knee, its position riding Push in real time. Attack/Release read as how
fast the fill snaps to the bar; Hold reads as the bar freezing; Punch flashes markers where
transients escape (+) or get smashed (−); Link < 100 splits the river into L/R half-heights.
Idle = one dim flat line; playing = the whole card breathes (obvious delta — fb311). Every
sound-changing param has a visible consequence (LAW 9, walked in §11).

**B. Curve + Ball (inset).** The §7.1 knee display with the distortion §5.8 upgrade: static
in/out curve reshaping live with Push/Ratio/Knee (OverEasy's negative slope literally bends
downward — the swallow zone is *visible before you hear it*), a ball riding the curve at the
current input with a decaying trail, and segment-occupancy glow (the `__dstViz` histogram idiom,
16 bins). No shipping compressor draws occupancy on its knee; ours will.

**Verdict:** A as the hero (~70 % of card), B as a mini inset (~30 %), one numeric GR readout
(`"−7.3 dB"`). Both are stroke-and-fill canvas paths on the existing 60 Hz rAF push lanes
(fb342 laws: no per-frame shadowBlur/filters, visible×fresh pushes only).

---

## 8. §Interplay — the device in a chain

* **Unity-through:** defaults are bit-transparent (§4.1); Power ON at defaults changes nothing.
* **Crest discipline downstream:** N dB of GR ≈ N dB more effective drive into a downstream
  Distortion at the same knob (compression removes the level variation Drive rode on). Document
  in the manual copy: "Compressor → Distortion = steadier bite; Distortion → Compressor = tamed
  splatter." Both orders are legitimate; drag order decides (fb307 inherited).
* **Before reverb/delay:** evens the send level — tails bloom uniformly. **After reverb:** the
  dry note pumps its own tail (release breathing) — the classic ambient trick; Squeeze after
  reverb = tails surge between notes (the single-band OTT wash). Both are presets (§9).
* **Auto-tracker beating:** distortion's `Auto` (300 ms RMS) + our `Auto` (300 ms) in series can
  beat < 1 Hz. Rule: our Auto compensates from the STATIC curve (§3.7), not a live tracker —
  only the 300 ms slew moves, monotonically. No loop, no beat.
* **Stacked compressors:** slopes multiply (`s_tot = s1·s2`) — two gentle 2:1s = one 4:1 with
  two-stage ballistics (the serial-1176 trick, also `Two Pass` character). Safe by construction;
  the only trap is both at fast release on bass = multiplied LF grind (document, don't clamp).
* **Mix is phase-safe here** (unlike most devices): wet = dry × smooth gain, so parallel blend
  at ANY Mix never combs. NY compression (Mix 40 %) is a first-class citizen. 100 % = fully wet
  (LAW 4; pad per fb292 as the other devices do).
* **Mono-sum:** Link 100 = identical L/R gain, mono-perfect. Link 0 on wide material shifts the
  image toward the quieter side under GR — a timbre choice, mono-safe (gain-only, no phase).
  State it in the tooltip, no clamp (distortion Width precedent).
* **External sidechain (compress the pad with the kick osc):** the per-osc SRC pills route what
  the device *processes*, not what it *hears*. A separate KEY input = the **chain-epic hook** —
  flagged, out of scope here; the detector code is already factored to accept any buffer.

---

## 9. §Presets — 14 factory sketches

| # | Name | Type · Character | Push/Ratio/Lift/Mix | Back (non-default) | Intent |
|---|---|---|---|---|---|
| 1 | Just Glue | Glue · Quad Bus | 22 / 4:1 / +2 / 100 | A 30 ms, R Auto, Knee 4 | The invisible mix-bus hug |
| 2 | Drum Brick | FET · All Buttons | 65 / 12:1 / +6 / 100 | A 0.1 ms, R 200 ms, Color 40 | Exploding-room drums/plucks |
| 3 | Vocal Ride | Opto · Glass | 40 / 3:1 / +4 / 100 | Hear Cut 90 Hz | The LA-2A lead-line sit |
| 4 | Pump Machine | Glue · Pump Bus | 70 / 10:1 / +8 / 100 | A 0.5 ms, R 100 ms, Hold 60 ms | EDM breathe keyed by its own notes |
| 5 | NY Thick | OverEasy · Over Easy | 60 / 8:1 / +6 / **45** | RMS Slow, Color 25 | Parallel weight, transients intact |
| 6 | Bass Even | Opto · Fresh Cell | 45 / 4:1 / +3 / 100 | Hear Cut 150 Hz, Hold 40 ms | Even bass without LF chatter |
| 7 | The Swallow | OverEasy · Anti | 55 / **past ∞** / +6 / 100 | Knee 12 | Louder in = quieter out. The freak |
| 8 | One Level | Squeeze · Level Rider | 55 / 60 % / +4 / 100 | A 20 ms, R 150 ms | Everything at one loudness, single-band OTT feel |
| 9 | Tail Surge | Squeeze · Only Up | 35 / 70 % / +2 / 100 | R 400 ms | Reverb tails/releases bloom between notes, die with the note |
| 10 | Snap Back | Clean · Precise | 50 / 6:1 / +5 / 100 | A 25 ms, **Punch +80** | Sustain crushed, attacks explode |
| 11 | Smash Swell | FET · Blackface | 60 / 8:1 / +6 / 100 | **Punch −80**, R 300 ms | Attacks eaten — every note a swell |
| 12 | Mu Warm | Vari-Mu · Studio 670 | 30 / 2.5:1 / +3 / 100 | R 800 ms, Color 45 | Slow tube weight, H2 bloom |
| 13 | Wide Breather | Vari-Mu · Lateral | 45 / 4:1 / +4 / 100 | **Link 0**, R 2 s | Mid ducks, sides breathe — width from dynamics |
| 14 | Ceiling | Limit · Clean Wall | 45 / — / +6 / 100 | A 0.5 ms, R 80 ms | The program-relative brick wall |

Level spread gate (Phase G lesson): all 14 must land within ±3 dB LUFS of bypass at the master
(the Sludge/Gargle spread bug class) — verify in the harness before shipping.

---

## 10. §CPU — budget and tiering

Per-instance, stereo @48 k: detector (rectify+link+HP+lift) ~8 flops, log10 ~15 (use a log2
poly-approx ×0.301), curve ~10, ballistics ~6, exp ~15 (exp2 poly), Color when active ~12
⇒ **≈ 60–70 flops/sample ≈ 0.1–0.2 % of one core — the cheapest flagship device in the rack.**
Opto/Vari-Mu add 2 poles (+8). No oversampling at any tier (§3.9); Quality dropdown is **not
used** by this device (d2 is `Detect` instead — the one device where that swap is honest).
Tiering: none needed; the only guard is the **awake-head sleep** (the fb342 dst-CPU law): after
300 ms with input peak < 1e-6 AND GR < 0.01 dB, flush states and short-circuit to pass-through;
first non-silent sample re-enters through the normal attack (no click possible — gain is 1.0
either way). Control-rate work (coefficient recompute on param change) on the block head, 20 ms
glides per §3.10.

---

## 11. §Pitfalls — the traps, collected

1. **Copied thresholds land 26 dB high** — the entire §2.1 law. Any literature number entering
   this code must pass through dBp.
2. **log(0) / negative RMS state** → −inf/NaN into the curve → full-scale scream. Floors per §4.3.
3. **Zipper on Push/Lift** — glide in dB, 20 ms; applying stepped dB gains is the classic
   compressor zipper (LAW 7).
4. **Release chatter on bass** — release < the waveform period tracks cycles (intentional at the
   bottom decade, §2.3) but must never be the *default*: default release 250 ms, Hold available.
5. **Feedback-topology limit cycles** — attack floor + detector smoothing (§3.4). Never let the
   FB tap read the post-Color signal (Color adds harmonics into its own detector = fizz loop);
   tap pre-Color.
6. **Upward compression amplifying the noise floor** — the §3.6 gate is a stability requirement,
   exactly like Zero-Square's −72 dB gate (distortion §2.3). Without it, Squeeze free-runs on
   denormal hiss and violates LAW 6.
7. **Auto-makeup chasing silence** — makeup from the STATIC curve only (§3.7), never from a live
   output tracker (which lifts the floor between notes — the Diode-2 Auto trap, distortion §2.3).
8. **Type-switch thumps** — carry the GR envelope across the switch (§3.10); never reset GR to 0
   on a type change (that is an instant +N dB step = a bang).
9. **Latency** — any "just 32 samples of lookahead" commit re-breaks fb305 (§0, §4.4). The
   verdict is zero; hold the line.
10. **The 4th-send-bus exclusion sums** (§4.4) and **IndyFxChain omission** (§4.5) — both build
    clean and fail silently.
11. **Mix100 wet-only law:** at Mix 100 the routed dry must be fully excluded (< −60 dB residual)
    — the shipped verification from fb292/fb303 applies unchanged.
12. **Perceptual harness honesty (fb283/Phase G):** sample-diff RMS is BANNED; a compressor's
    metrics are **GR trajectory, crest factor, LUFS delta, envelope-shape diff, THD** measured
    on BURSTS and CHORDS — a static sine cannot reveal attack/release/punch/hold at all (the
    PK_AM lesson: transient params need AM/burst probes). The §6 staircase is the master probe.
13. **Choice-param cardinality (fb342 law ①):** Type=8, Character=8, Detect=5 — sizes locked at
    birth; adding an entry later re-scales every saved preset's index. Ship with spares? No —
    lock the lists now (this bible is the lock).

---

## 12. §Hard-rule compliance checklist (laws 1–10, walked)

| # | Law | Where satisfied |
|---|---|---|
| 1 | Bus reality −26 dBFS | §2.1 dBp system; every threshold in this file is program-relative; detector lift +26.02 dB; nothing copied verbatim from literature |
| 2 | Chassis 11-param | §5: front Push/Ratio/Lift/Mix + Auto/Grab pills; back Character+Detect dropdowns + Attack/Release/Knee/Hear Cut/Punch/Hold/Link/Color 4×2. Pragmatic names, Title-case, only FET/RMS-class real acronyms |
| 3 | Time params 4 bars→1/256 | N/A — no tempo-synced knob in v1 (Attack/Release are ballistic, not musical-grid times). If Max wants a synced Release for pump timing, it must span 4 bars→1/256 (flagged §13 Q5) |
| 4 | Mix 100 = wet; switches never cut | §3.10 state carry-over + §8 Mix law + §11.11 |
| 5 | Params evolve 0→100, types night-and-day | §2 taper laws (no dead first third); §6 per-type measurable discriminators + the staircase tell |
| 6 | Nothing free-runs; loop gain stated | §3.4 (FB loop is a contraction, max static loop gain 1.0), §3.6 gate, §3.8 GR-gated Color |
| 7 | No clicks | §3.10 glides + envelope carry-over; Hold/Detect switches glide their windows |
| 8 | CPU-friendly | §10: ~0.15 %/instance, zero oversampling, awake-head sleep |
| 9 | Audible ⇒ visible, dramatic | §7.2: every one of the 11 params has a named visual consequence; idle-dim/playing-bright |
| 10 | Recycle first | Appendix A — 9 verified in-tree reuse targets with file:line |

---

## 13. §Open questions for Max

1. ~~**Type placement**~~ — **RESOLVED by recon (2026-08-14):** the header type dropdown is the
   shipped, device-agnostic chassis — index.html:7690 renders `fxr-type` (the fb275 native
   `<select>` overlay) from each device descriptor's `tp:` field (the distortion descriptor at
   :7497 sets `tp:'SYN_DST_TYPE'`; reverb/delay use the same field), and back d1/d2 are always
   Character + a second selector. Header pill it is; no decision needed.
2. **Limit as a Type vs the Ratio endpoint** (Serum 2 turns Ratio's max into "Limit"). I kept it
   a Type (it needs its own characters + fb264 recycle); collapsing it frees a Type slot for a
   9th topology (e.g., a `Diode Bridge` 33609/DIODE-609 type). Preference?
3. **OTT boundary** — Squeeze (single-band up+down) stays here; 3-band OTT is its own future
   device. Confirm the boundary before the chain epic.
4. **Auto pill default OFF** (the distortion timidity precedent) — or ON for instant gratification?
5. **Synced Release** for pump timing (1/4, 1/8 dotted…) — worth a Character set (`Pump Bus`
   variants), a Detect-style dropdown entry, or nothing? If yes it must obey the 4-bars→1/256 law.
6. **Hero viz** — Press River + knee inset (§7.2 verdict) — sign off before the mockup, which per
   the fb296 law must be interactive + audible (Web Audio DynamicsCompressorNode prototypes the
   river convincingly in Safari).
7. **Punch polarity naming** — one bipolar `Punch` knob (assumed) vs two-position pill + depth?
8. **A 9th type `Smooth`** (Airwindows ButterComp bipolar-interleaved: four alternating
   compressors on waveform halves — near-inaudible glue)? Researched, cut by LAW 5 (its whole
   point is imperceptibility — no night-and-day discriminator). Veto if you disagree.

---

## Appendix A — Recycle inventory (verified by reading, not assumed)

| Target | Where (verified) | Use |
|---|---|---|
| Punch transient follower | DistortionEngine.h:710-711, coeffs :3022-3023 (3 ms/80 ms), block correction :590-591 | P5 Punch, verbatim |
| Squash leveller | DistortionEngine.h:2259-2270 (20:1, 0.9 target, floors) | Squeeze's downward lane + the gain-riding grammar |
| fb264 master limiter | PluginProcessor.cpp:45-47 (knee 0.90), :3753 (0.8 ms attack, stereo-linked), applied :7410 | Limit type ballistics + link pattern |
| softClip bound | DelayEngine.h:315-321 | The overshoot/NaN net after makeup |
| onePole coeff helper + flush | DelayEngine.h:324-330 | All ballistic coefficients + denormal flush |
| TPTOnePole / DCBlocker / SvfMultimode | TerrainFilters.h:83 / :69 / :317 | Hear Cut HP · post-Color DC · detector tilt shelf |
| Engine contract shape | DistortionEngine.h:99 `prepare(double)`, :356 `processSample(inL,inR,outL,outR)` | CompressorEngine.h copies this exact API or it will not drop into the rack |
| Viz plumbing | PluginEditor.cpp:952/:4862 (`getReverbBloom` native), :5577 (`__dstViz` push), index.html:8163/:27739/:28384 (consumers) | `getCmpViz` clones the JSON-push idiom |
| Param grammar | ParameterIDs.hpp:406-435 (the full SYN_DST block incl. SRC pills/POWER/AUTO/PILL2) | SYN_CMP block, field-for-field |
| Send-bus exclusion law | PluginProcessor.cpp:6457-6458, :7159, :7326, :7358 (fb338 comments) | The three sums + pointers a 4th bus must join |
| IndyFxChain wrap pattern | IndyFxChain.h:29, :223-231, :281 (MoogDelay example) | Add the compressor to the chop path |

**Confirmed greenfield:** no `SYN_CMP*`/`Compressor` symbols anywhere in ParameterIDs.hpp or
PluginProcessor.cpp (grep, 2026-08-14) — the whole param block + engine + relays are new work,
so the WebSliderRelay 4-point trap applies to all ~25 params.

---

## Sources

### Serum 2 / Xfer
* https://xferrecords.com/web-manual/serum-2/welcome — Serum 2 online manual (root)
* https://xferrecords.com/forums/general/serum-fx-multiband-compressor-ott — Xfer forum: multiband == OTT engine
* https://www.musicradar.com/how-to/a-quick-guide-to-xfer-records-serums-effects — Serum compressor params (Threshold/Ratio/Attack/Release + Multiband)
* https://thenoisedept.com/blogs/lab-notes/i-modeled-guitar-pedals-in-serum-2s-fx-rack-heres-how-lab-notes-3 — Serum 2 FX rack; **Ratio → "Limit" true peak limiter**; per-module Level slider
* https://www.edmprod.com/ott-plugin/ and https://unison.audio/ott-plugin/ — OTT Depth/Time/In/Out + up/down controls

### DSP / papers / talks
* https://www.aes.org/e-lib/browse.cfm?elib=16354 — Giannoulis, Massberg, Reiss, *Digital Dynamic Range Compressor Design — A Tutorial and Analysis*, JAES 60(6) 2012 (knee formula, branching detectors, FF vs FB, log-domain)
* https://cytomic.com/technical-papers/ — Andrew Simper's technical papers (ZDF/state-space, dynamic smoothing)
* https://www.youtube.com/watch?v=eGcqomH6aAc — Simper, *From Circuit to Code*, ADC20
* https://www.ableton.com/en/blog/andrew-simper-glue-eq-eight/ — Simper on The Glue
* https://gearspace.com/threads/cytomic-quot-the-glue-quot-bus-compressor-effect-plugin.363849/ — Glue specs: attack 0.01–30 ms, release 0.1–1.2 s + Auto, ratios 2/4/10, the diode level-dependent attack description
* https://www.airwindows.com/pressure/ · https://www.airwindows.com/buttercomp2/ — bipolar interleaved compressor design (evaluated, cut per LAW 5)

### Hardware lineages
* https://help.uaudio.com/hc/en-us/articles/34530260482324-1176-Classic-FET-Compressor-Manual — 1176: 20–800 µs attack, 50–1100 ms release, 4/8/12/20:1
* https://pulsar.audio/blog/the-history-of-all-buttons-in-mode/ — all-buttons: ratio 12–20:1 wander, plateau curve, erratic ballistics
* https://media.uaudio.com/assetlibrary/l/a/la-2a_manual.pdf — LA-2A manual; T4: ~10 ms attack, 40–80 ms to 50 %, 0.5–5 s tail ("depending upon the amount of previous reduction"), program/frequency dependent
* https://groupdiy.com/threads/t4-optical-attenuator-for-teletronix-la-2a-and-how-the-compressors-works.80210/ — T4 cell memory behavior
* https://blog.native-instruments.com/fairchild-compressor/ and https://www.soundonsound.com/reviews/fairchild-660-670 — Fairchild 670; six time constants (0.2 ms/0.3 s … 25 s program-adaptive), vari-mu principle, LAT/VERT
* https://www.mixonline.com/technology/birth-of-a-classic-the-dbx-160-compressor — dbx 160/OverEasy, decilinear VCA, true-RMS, feedforward
* https://assets.wavescdn.com/pdf/plugins/dbx-160.pdf — Waves dbx 160 manual ("In 1976, dbx introduced the dbx 160 compressor"; decilinear VCA, RMS)
* https://dbxpro.com/en/products/160a — dbx's own 160A spec: ratio "1:1 to ∞:1 thru to −1:1", "'INFINITY +' inverse-compression mode actually decreases the audio output level below unity gain when the input exceeds threshold" (the §6.6 negative-zone primary source)

### Reference plugins (the bar)
* https://www.fabfilter.com/help/pro-c/using/displays — Pro-C display: input dark/output light/GR red line; knee display lights at current level (the §7 survey primary)
* https://prod.fabfilter.com/forum/topic/3006/pro-c2-compression-types and https://x.com/natemixing/status/1832108764125696230 — Pro-C 2's 8 styles decoded (Clean FF / Classic FB / Opto / Vocal 100:1 / Mastering / Bus / Punch / Pumping)
* https://manualzz.com/doc/2982445/u-he-presswerk-dynamics-processor-user-guide — Presswerk: FF/FB/INT detector modes, link 0–100 %, −60 dB @ 20:1, DPR
* https://dl.arturia.net/products/comp-fet76/manual/comp-fet76_Manual_1_0_1_EN.pdf — FET-76: input-drives-threshold, Link in/out, compression Range, sidechain HPF
* https://dl.arturia.net/products/comp-vca65/manual/comp-vca65_Manual_1_0_1_EN.pdf — VCA-65 (dbx 165A model)
* https://dl.arturia.net/products/comp-tubesta/manual/comp-tubesta_Manual_1_0_1_EN.pdf — TUBE-STA: Input/Output link, Recovery, Mix, SC filter, mono/stereo trigger
* https://dl.arturia.net/products/comp-diode-609/manual/comp-diode-609_Manual_1_0_EN.pdf — DIODE-609 (Neve 33609 diode-bridge; the candidate 9th type)
* https://www.ableton.com/en/manual/live-audio-effect-reference/ — Ableton Compressor activity view + Glue needle

### Repo (read directly, line references verified 2026-08-14)
* PluginProcessor.cpp:26/:45-47/:3753/:6300-6301/:6457-6458/:7159/:7326/:7358/:7410/:5860/:3488
* DistortionEngine.h:99/:356/:590-591/:710-711/:2259-2270/:3022-3023 · DelayEngine.h:315-330
* TerrainFilters.h:69/:83/:317 · ParameterIDs.hpp:406-435 · IndyFxChain.h:29/:223-231/:281
* PluginEditor.cpp:952/:4862/:5577 · Source/ui/public/index.html:8163/:27739/:28384
* Design/DISTORTION-BUILD-BIBLE.md — §2.2 drive law, §4.2 Auto, §4.4 latency trap, §5.5 back-8
  grammar, §5.8 occupancy viz (the house patterns this bible extends)
