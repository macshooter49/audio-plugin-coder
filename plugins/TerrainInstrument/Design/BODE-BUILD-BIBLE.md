# Terrain Instrument — Bode Build Bible (frequency shifter, FX device #4)

*Research dossier + build spec. fb-series: the 4th flagship FX device after Reverb / Delay / Distortion.*
*Written 2026-08-14 from primary sources (Sonic Charge Echobode user guide read cover-to-cover, Behringer/Bode
1630 manual, Doepfer A-126-2, csound source, Niemitalo's coefficient page) + in-tree recon with line numbers.
A builder must be able to implement the device from this file alone.*

> **AUDIT PASS 2026-08-14 (adversarial).** Every in-tree line cite re-read; both quadrature networks
> re-simulated numerically at 44.1/48/96 k; the sideband sign settled by a time-domain probe. **Nine
> measured corrections were applied** — the biggest being the **swapped up/down sideband formula (§3.3)**,
> the **inverted `Dome '64` image spec (§3.1/§4.8)** and the **wrong Niemitalo band edge (§3.1)**.
> Corrections are marked `[AUDIT]` inline. Claims that could not be verified are marked
> `⚠️ UNVERIFIED` — **do not quote those as fact.** Verification scripts:
> `scratchpad/bode_hilb.py`, `scratchpad/bode_hilb_edges.py`, `scratchpad/bode_sign.py` (worktree root).

---

## 0. The scope decision

**ONE device, name `Bode`, 7 Types on one engine.** *(⚠️ the 7-Type roster does NOT survive law 5 as
written — see the §4.0 audit box before locking it.)* The Serum 2 FX menu Max is competing with lists
`Bode` as a first-class device, and it is **feature-identical to Sonic Charge's Echobode**: Serum 2
reviews describe it as *"a frequency shifter with a delay built into the feedback path and even a
blurring diffusion control"* (databroth) — the Echobode topology exactly.
**[AUDIT] ⚠️ UNVERIFIED: that Serum 2's Bode is a *licensed* Echobode.** No licensing statement exists
on soniccharge.com/echobode or in the Xfer Serum 2 web manual; the only confirmed link is feature
parity. **[AUDIT] WRONG → FIXED: the Echobode credits.** The user guide (p.14, "Credits and Contacts")
reads *"Created by: **Magnus Lidström**"*; **Fredrik Lidström is credited for "Graphical design and
additional development"**, not for the algorithm. Do not repeat the earlier attribution.
Either way the reference bar is not "a frequency shifter knob" — it is **a frequency shifter living
inside a feedback delay loop with diffusion**, which happens to also be the superset topology that
produces every classic frequency-shift trick (barberpole, spiral echoes, detune widening, ring mod)
as a *configuration*, not as separate engines.

**Why this is the cheapest flagship we will ever build:** the core DSP is ALREADY IN THE TREE and
certified as a filter type. `TerrainFilters.h:1110` (`struct BodeAP`) + `TerrainFilters.h:1127`
(`struct BodeShifter`) implement the exact Niemitalo quadrature network, the recursive quadrature
oscillator with renorm, feedback with DC blocking, and the fb165 direction law. The device is that
engine, stereo-ized, wrapped in the Echobode loop topology, on the fb275 chassis.
**[AUDIT] Both cites re-read and CONFIRMED** (`BodeAP` 1110–1120, `BodeShifter` 1127–1172, coefficients
1139–1142, `iDelay` written at 1162, renorm at 1167, DC-blocked fb tap at 1169). **Three carry-over
traps the reuse must fix**, all in `setParams` (:1145–1157): the shift is derived from a *log cutoff*
param (`cut01`) instead of a bipolar one, `FMAX` is **1000** and the result is **clamped to ±2000 Hz**
(:1150–1152) — a ±5 kHz device must replace that whole mapping, not inherit it — and `fb = 0.95·res01`
is applied with **no sideband/`g` de-rate and no envelope gate** (§3.5 fixes both).

**What this device is NOT:** a pitch shifter. No granular/PSOLA/phase-vocoder anywhere in this device
(that is a future device). §2 gives the manual-ready explanation of the difference.

---

## 1. History and circuits — the lineage

### 1.1 Harald Bode and the analog SSB shifter

Harald Bode (**19 Oct 1909 – 15 Jan 1987**, confirmed) — the least-famous pioneer whose designs fed both
Moog and Buchla — built the first musician-facing frequency shifters. The **Bode Frequency Shifter dates
to 1964** (Bode Sound Co.); the **Model 735 / 735 Mark III** is the mid-1970s production unit. **Moog
licensed three Bode products — Ring Modulator, Frequency Shifter and Vocoder 7702 — as part of the Moog
Synthesizer**, which is where the **Moog Model 1630 "Bode Frequency Shifter"** comes from (confirmed).
**[AUDIT] ⚠️ UNVERIFIED: "share the same circuit boards"** — the licensing is documented, board-level
identity is not; treat the 1630 as *the Bode design under Moog's badge*, nothing stronger.
**[AUDIT] ⚠️ UNVERIFIED / likely conflated: the Columbia-Princeton / Ussachevsky commission.** No source
found. The documented Bode-instrument studio placement is the **Melochord at the WDR studio in Cologne**
(Meyer-Eppler / Eimert / Ligeti) — a different instrument and a different studio. Do not put the
Columbia-Princeton line in the manual.

**The 1630's control surface is a design brief for our device** (from the 1630 manual, carried into the
Behringer 1630 reissue):

| 1630 control | What it does | Our descendant |
|---|---|---|
| Shift amount, **−5000 … +5000 Hz** | continuous, voltage + manual, **linear or exponential scaling** | `Shift` hero knob, ±5 kHz, exp-symmetric taper |
| **Mixture** | continuous blend of the **A (up-shifted)** and **B (down-shifted)** outputs — ring mod at center | `Sideband` front knob |
| **Zero Adjust** + beat LED | calibrate/vernier the zero point; LED flashes at the residual shift rate | `Fine` (P1) + the viz's beat indicator |
| **Squelch**, 0 … −60 dBu | gates the output below an input threshold (analog shifters leak carrier) | our **envelope gate** on feedback (law 6) — same instinct, modern job |
| A and B outputs simultaneously | both sidebands always computed | we always compute both products; `Sideband` blends |

### 1.2 The dome filter — the analog Hilbert transform

The analog units generate the 90° quadrature pair with a **dome filter** (after R.B. Dome): two parallel
ladders of first-order allpass sections whose pole frequencies are staggered so the *difference* of the
two phase responses sits at 90° across the audio band. Precision is everything: **Doepfer's A-126-2**
(the best modern analog implementation) uses a **12-stage dome filter with 0.1% resistors / 1%
capacitors and holds 90° ± <0.3° from ~50 Hz to 14 kHz**, and needs minutes of warm-up to stabilize —
the phase error IS the image-sideband leakage (§3.2 gives the exact formula). **[AUDIT] all four
A-126-2 figures CONFIRMED at doepfer.de/a1262.htm**, which additionally states **~−54 dB sideband
suppression** (consistent with §3.2's R(0.3°) = −51.6 dB) and an internal quadrature VCO spanning
**~20 Hz to 5 kHz** — an independent modern data point for our ±5 kHz choice (§3.6).
Bernie Hutchins published the canonical DIY dome designs in *Electronotes* / the *Musical Engineer's
Handbook* (1975), and that exact 6-pole-per-path network survives today as csound's `hilbert` opcode —
attributed to **Sean M. Costello, 1999** *(⚠️ UNVERIFIED authorship/date — the pole values themselves
were re-simulated and behave exactly as the csound design does, §3.1)*, who went on to found Valhalla
DSP and build ValhallaFreqEcho. The lineage is one unbroken 60-year chain, and we hold every link's
coefficients (§3.1).

### 1.3 The digital descendants (the greats, with their param sets)

* **Sonic Charge Echobode → Serum 2 `Bode`** *(the direct competitor — full manual absorbed;
  **[AUDIT] every range in this bullet was re-checked against the v1.2 user guide PDF and is CORRECT**,
  including the "excellent 40 Hz–20 kHz" suppression line, Fine ±200 Hz / Wide ±20 kHz, delay
  0.02 ms–1 s free & 1/128–1/2 synced, in-loop HP 20 Hz–2 kHz, LP 100 Hz–40 kHz, LFO 0–100 Hz /
  8 measures–1/64, the pre-integrated delay-LFO shapes, and the Smear-adds-loop-delay sync
  compensation of §11.10)*.
  Signal flow (manual block diagram): `IN → DELAY → SMEAR → FILTER(HP+LP) → FREQ SHIFTER(+PHASE) → SIDEBAND MIX
  → ANTI-REFLECTION → CROSS CHANNELS → (feedback → DELAY) → MIX → OUT`. Params: **Frequency Shifter**
  big knob with **Range** = Fine ±200 Hz / Wide ±20 kHz / **Sync** (the knob becomes a *cycle rate*
  synced to tempo — the barberpole mode!) / MIDI (notes set the shift); **Sideband** (down ↔ "r.m." ↔
  up); **Anti Refl** on/off — removes partials shifted below 0 Hz or above Nyquist *"with the same
  excellent quality as the sideband suppression"*; **Phase** (static phase offset for tuning the comb
  notches when mixed/fed back); **Delay** 0.02 ms–1 s free / 1/128–1/2 synced, all-pass interpolation,
  smooth like an analog delay; **Feedback**; **Cross** (swap L/R in the loop = ping-pong); in-loop
  **High-Pass** 20 Hz–2 kHz and **Low-Pass** 100 Hz–40 kHz; **Smear** (Schroeder allpass diffusion
  *inside the feedback path*, Serum 2 renames it **Blur**); **Mix**; LFO (sine/saw/square/random,
  0–100 Hz or 8 bars–1/64, bipolar amount, **Stereo = inverted modulation on R**, targets
  Freq/Phase/LP/Delay — delay uses pre-integrated shapes so the LFO modulates the *rate of change* =
  pitch quality). Sideband suppression spec: *"excellent over 40 Hz to 20 kHz."* CPU: self-suspends to
  0% on silence. The manual's stated musical laws: tiny shift + mix = phaser beating; feedback with
  very short delay = phaser → longer = flanger comb → longest = distinct shifted echoes.
* **ValhallaFreqEcho** (free, Costello) — shifter + analog-style echo: Shift, Delay (knob or synced),
  Feedback to self-oscillation, LowCut/HighCut in loop, Mix. Costello's blog adds the numbers we reuse:
  **opposite L/R shifts of S Hz beat at 2·S Hz** (2 Hz shift → 4 Hz binaural beat), and **Schroeder's
  1962 anti-feedback result — a few-Hz shift buys up to ~6 dB more PA gain before howl** (the original
  industrial use of the device). **[AUDIT] both CONFIRMED** verbatim on the Valhalla blog
  (frequency-shifter category); note the blog cites *JAES 1962* while the primary paper usually cited
  elsewhere is Schroeder, *JASA* 1964 — ⚠️ the primary citation is unverified here, the ~6 dB figure is not.
* **Ableton Shifter** (Live 11.1+, successor of Frequency Shifter): Coarse+Fine shift, modes
  **Pitch / Freq / Ring**, **Wide** = fine shift inverted on the right channel, LFO with shapes +
  spin, dedicated **envelope follower**, MIDI sidechain. The Sound-on-Sound "Shift Yourself" recipes we
  ship as presets: **0–6.5 Hz shift + Wide = rotary speaker** (alternating L/R cancellation);
  Spread ≈1 Hz + slow sine LFO = phase shifter.
* **Kilohearts Frequency Shifter** — minimal proof that one knob is a product: a single shift readout.
* **Melda MFreqShifterMB** — the maximalist pole: per-band **Shift ±10 kHz, Width ±200%, Delay
  0–1000 ms, Feedback, Character = Clean/Soft/Rough/Ugly**, up to 6 bands. (Their Character-as-quality
  axis validates our §4.8 Character list.)
* **Unfiltered Audio Fault** — dual shifter, ±500 Hz with a ×5 Mult (= ±2.5 kHz), FM of the shift
  amount (linear/exponential), **six feedback paths (L, R, cross — for shifter and delay each)** with
  in-loop LP/HP and a Stabilize limiter. Proof that cross-feedback + in-loop filters is where the
  monster sounds live.
* **Eventide H3000 lineage** — no literal SSB shifter, but the "Dual Shift" (two H910s, per-channel)
  and the anti-feedback H910 use-case are the studio ancestors of our `Split` type.
* **Buchla 285 / 285t** — frequency shifter + balanced modulator in one panel: the West-coast
  confirmation that **ring mod is the same circuit with both sidebands kept** (our `Sideband` knob).
* **DAFx-15, Esqueda / Välimäki / Parker, "Barberpole Phasing and Flanging Illusions"** — the academic
  treatment of infinite risers: of the three constructions, **SSB modulation is the canonical one**
  ("set the shift to the desired sweep rate, mix 50%"), and the illusion reads best on dense spectra at
  slow rates (≲0.3 Hz for the seamless version). This is our `Barberpole` type, verbatim.

---

## 2. Frequency shift vs pitch shift — the manual paragraph

A **pitch shifter multiplies** every frequency: ×2 sends 100/200/300 Hz to 200/400/600 Hz — ratios
preserved, still harmonic, still "the same note higher." A **frequency shifter adds**: +50 Hz sends
100/200/300 Hz to **150/250/350 Hz** — the ratios are destroyed (1 : 1.67 : 2.33), the overtone series
becomes **inharmonic**, and the ear hears bell, metal, glass, radio. Tiny shifts (fractions of a Hz)
barely displace anything but make every partial **beat against the dry signal at the same fixed rate**
— which is why a frequency shifter is the only "detune" whose shimmer is uniform from bass to cymbal
(a chorus's beat rate grows with frequency; a shifter's does not — that constant-beat signature is our
`Detune` type's measurable tell). Shift **down through zero** and partials reflect off 0 Hz back
upward, mirrored and phase-conjugate — the underwater/alien collapse no pitch shifter can make.
Technically the device is **single-sideband amplitude modulation**: multiply the analytic (Hilbert)
signal by a complex sinusoid, keep one sideband. Ring modulation is the same operation with **both**
sidebands kept — on this device that is not a separate pedal, it is the center of the `Sideband` knob.

---

## 3. DSP core

### 3.1 The quadrature (Hilbert) network — exact recipes, both lineages

**Primary (ships, already in-tree): Olli Niemitalo's 4+4 IIR allpass pair** (yehar.com; verified against
`TerrainFilters.h:1139-1142`). Two parallel cascades of 2nd-order one-multiply allpass sections
`H(z, a) = (a² − z⁻²)/(1 − a²·z⁻²)`, implemented `y[n] = a²·(x[n] + y[n−2]) − x[n−2]`:

```
I path (then ONE extra sample of delay — mandatory, aligns the pair):
  a = 0.6923878, 0.9360654322959, 0.9882295226860, 0.9987488452737
Q path (comes out +90° from I):
  a = 0.4021921162426, 0.8561710882420, 0.9722909545651, 0.9952884791278
```

Design specs, in Niemitalo's own words (verified on the page): **"90 (± 0.7) degrees relative phase"**,
**passband ripple 0.0002 dB**, **"transition bandwidth is 0.002 times the width of passband"**,
**stopband (image) attenuated to −44 dB**, **8 multiplications** per channel.

**[AUDIT] WRONG NUMBER → FIXED. The old text said the honest band at 48 k is "~48 Hz … 23.95 kHz" and
that the 90° "COLLAPSES below ~48 Hz". Both are false.** Re-simulating the exact cascade
(`scratchpad/bode_hilb.py`, 200 k log-spaced points) gives the measured ±0.7° band:

| fs | measured ±0.7° band | image @20 Hz | image @48 Hz | worst 100 Hz–16 k |
|---|---|---|---|---|
| 44.1 k | **20.0 Hz … 22.03 kHz** | 0.702° / −44.3 dB *(the edge)* | 0.549° / −46.4 dB | 0.702° / −44.3 dB |
| **48 k** | **21.8 Hz … 23.98 kHz** | 1.645° / **−36.9 dB** | **0.367° / −49.9 dB** | 0.702° / **−44.3 dB** |
| 88.2 k | 40.0 Hz … 44.06 kHz | 16.5° / **−16.8 dB** | 0.465° / −47.8 dB | 0.703° / −44.2 dB |
| 96 k | **43.6 Hz … 47.96 kHz** | 19.6° / **−15.3 dB** | 0.059° / −65.8 dB | 0.703° / −44.2 dB |

The equiripple worst case in 100 Hz–16 kHz is **−44.3 dB at every sample rate** — the design's rated
number, confirmed. Only the LF corner moves.

So at 48 k, **48 Hz is one of the network's BEST points (−49.9 dB image), not its breakdown** — the
knee is at **~22 Hz** (20 Hz already costs 7 dB of rejection). The "48 Hz" figure is really the
**96 kHz** low edge; the edges scale with fs, so **the LF image problem is a HIGH-SAMPLE-RATE problem**:
at 88.2/96 k a 20 Hz partial leaks its mirror at only **−17/−15 dB**. `Low Keep` (§5 P5) is still
worth its slot — it anchors bass and keeps the sub out of the loop — but **its justification is
musical at 44.1/48 k, and a genuine repair only at fs ≥ 88.2 k.** State it that way in the manual, and
default `Low Keep` ≥ 60 Hz whenever `prepare()` sees fs ≥ 88.2 k.

Cost: 8 sections = **8 multiplies per channel**. The one-sample I-path delay is already in-tree
(`iDelay` written at `TerrainFilters.h:1162`) and is **mandatory** — see the alignment trap in §3.1b.

**Secondary (the `Dome '64` Character): the Hutchins/Electronotes dome, via csound.** Two parallel
cascades of **6 first-order allpass sections**, pole ring frequencies = `poles[j] × 15 Hz`
(csound `Opcodes/ugsc.c:111-143`, author Sean M. Costello 1999, poles verbatim from Hutchins'
*Musical Engineer's Handbook*):

```
path 1 (→ I role, "sin"): 0.3609,  2.7412,  11.1573,  44.7581, 179.6242,  798.4578    // ×15 Hz
path 2 (→ Q role, "cos"): 1.2524,  5.5671,  22.3423,  89.6271, 364.7914, 2770.1114    // ×15 Hz
per pole:  f = pole·15;  α = π·f/fs;  c = (1−α)/(1+α)
section:   y[n] = c·(x[n] + y[n−1]) − x[n−1]        // 1st-order allpass, H(z) = (c − z⁻¹)/(1 − c z⁻¹)
```

*[AUDIT] the coefficient form was rewritten to the csound form above. The old text specified
`coef = −β` with `y[n] = coef·(x[n] − y[n−1]) + x[n−1]`, which is **−1× the standard allpass per
section**; with 6 sections per path the (−1)⁶ cancels, so it was numerically equivalent but an
invitation to a sign bug the day anyone changes the section count. Use the form above.*

**[AUDIT] WRONG SPEC → FIXED. The old text claimed the dome is "rated 15 Hz–15 kHz" with an image
floor "~10–15 dB higher" than Niemitalo. Simulation says the opposite in-band.** Measured
(`scratchpad/bode_hilb.py` + `bode_hilb_edges.py`), ±0.7° band and image level:

| fs | ±0.7° band | 20 Hz | 100 Hz | 1 kHz | 8 kHz | 14 kHz | 16 kHz | 20 kHz |
|---|---|---|---|---|---|---|---|---|
| 44.1 k | 14.1 Hz … 11.93 kHz | | | | | | | |
| **48 k** | **14.1 Hz … 12.34 kHz** | −58.4 dB | −58.9 dB | **−55.9 dB** | **−68.2 dB** | −32.3 dB | −23.0 dB | −10.2 dB |
| 96 k | 14.1 Hz … 14.72 kHz | | | | | | | |

The dome is **6–19 dB CLEANER than Niemitalo from 20 Hz to ~11 kHz** and falls apart **above ~12 kHz**
(where Niemitalo holds −44 dB all the way to 23.9 k). Its poles are in **absolute Hz** (×15 Hz), so the
low edge is fs-independent at ~14 Hz while the top edge crawls (11.9 k @44.1 k → 14.7 k @96 k).

**That is still a great Character — but it is a TOP-OCTAVE character, not a dirty-everywhere one.**
Its honest voice: pristine mids, and a **top octave that grows a loud mirror image** — exactly what an
analog unit sounds like when you shift a cymbal. §4.8 was rewritten to match. 12 multiplies per channel.

### 3.1b The alignment trap (both networks) — **[AUDIT] added, this was missing**

The Niemitalo pair needs **one extra sample of delay on the I path** (both cascades are 2nd-order, the
Q cascade is effectively half a sample "ahead"). The dome pair is **6 first-order sections on each
path and must NOT have it.** Applying the Niemitalo `iDelay` to the dome adds `w` rad of error —
**7.5° at 1 kHz, i.e. image −23.7 dB instead of −55.9 dB**, a Character that sounds broken.
⇒ **the `iDelay` stage is part of the Character swap, not part of the fixed chain.** Bake it into the
network object (`applyIDelay` flag), never into the surrounding code. Same rule for the `Guard`
pair (§3.4).

Both networks agree on the **sign convention**: measured `arg(Q) − arg(I) = +90.4°` (Niemitalo,
1 kHz) and `+90.2°` (dome, 1 kHz). **Q LEADS I.** Everything in §3.3/§3.4 follows from that.

### 3.2 The image-rejection law (the number that grades everything)

With matched amplitudes, a total quadrature phase error ε makes the unwanted sideband leak at

```
R(ε) = 20·log10( tan(ε/2) )
ε = 1.0° → −41.2 dB     ε = 0.7° → −44.3 dB   (Niemitalo, matches his −44 dB spec)
ε = 0.5° → −47.2 dB     ε = 0.3° → −51.6 dB   (Doepfer A-126-2 class)
```

**[AUDIT] formula and all four rows re-derived and CONFIRMED** (amplitude-matched image ratio is
cot²(ε/2) ⇒ image level = 20·log₁₀ tan(ε/2)); Doepfer's independently published −54 dB for a <0.3°
unit corroborates the −51.6 dB row.

This is the harness metric (§10): play 1 kHz, shift +100 Hz, measure 900 Hz (image) against 1100 Hz
(main). Ship gate: **≤ −40 dB from 100 Hz to 16 kHz at 48 k** for the clean Characters. **[AUDIT]
measured margin: Pristine's worst case in that band is 0.702° ⇒ −44.3 dB, so the gate passes with
~4 dB to spare** — and a time-domain run of the exact recursions at 1 kHz / +100 Hz measured the image
at **−49.1 dB** (`scratchpad/bode_sign.py`), matching the phase-derived −49.0 dB. The vintage Characters
*deliberately* miss this gate by stated amounts — grade them against their own targets.

### 3.3 The shift itself — complex multiply + quadrature oscillator

```
analytic:  I[n] (delay-aligned path),  Q[n] (+90° path)
oscillator (coupled-form rotation, phase-continuous):
  nC = cosΔ·C − sinΔ·S ;  S = sinΔ·C + cosΔ·S ;  C = nC        // Δ = 2π·shiftHz/fs, SIGNED
  every 512 samples: r = 1.5 − 0.5·(C² + S²);  C·=r;  S·=r      // 1st-order renorm (in-tree, :1167)
                                                                 // [AUDIT] renamed m→r: `m` is the
                                                                 // Sideband mix two lines down.
sidebands:   ⚠️ [AUDIT] THESE SIGNS WERE INVERTED IN THE ORIGINAL DRAFT — corrected below.
  y_up = I·C + Q·S        y_dn = I·C − Q·S
  m    = 2b − 1  (b = Sideband knob 0..1;  −1 = down, 0 = ring, +1 = up)
  y    = g·( I·C + m·Q·S ),   g = sqrt( 2 / (1 + m²) )          // g: +3 dB at center so ring mod
                                                                 // holds equal power vs SSB
```

**[AUDIT] THE #1 CATCH. The draft had `y_up = I·C − Q·S` / `y_dn = I·C + Q·S` and
`y = g·(I·C − m·Q·S)` — the up and down sidebands were SWAPPED**, which would have shipped a `Shift`
knob that goes the wrong way, a `Sideband` knob whose ends are mislabelled, and a `Barberpole` that
falls when it should rise. Two independent proofs:
* **Analytic derivation.** Q leads I by +90° (measured, §3.1b) ⇒ Q = −H{I} ⇒ the analytic signal is
  `I − jQ`, and `Re{(I − jQ)·e^{jΩt}} = I·C + Q·S`. With `I = cos ψ, Q = −sin ψ` this is `cos(ψ + Ωt)`
  — **up**.
* **Time-domain probe** (`scratchpad/bode_sign.py`, exact recursions, 1 kHz in, Δ = +100 Hz):
  `I·C + Q·S` → **1100 Hz at −6.1 dB, 900 Hz at −55.3 dB**; `I·C − Q·S` → the exact mirror.
* **The in-tree engine already has it right**: `TerrainFilters.h:1168` is
  `y = iOut*osC + qOut*osS`. Copy the sign from the code, not from the old draft.

* **Signed Δ** carries the up/down of the `Shift` knob and is **phase-continuous through zero** —
  turning the knob NEVER clicks because only the rotation increment changes, never the phase. This
  replaces the fb165 `dirMul` hack in the filter version; the device engine uses signed Δ + `m` and
  retires `dirMul`.
* At `Shift = 0, m = +1`: `C = 1, S = 0` ⇒ `y = I·C + Q·S = I` = the allpassed input — **unity
  magnitude, rotated phase** (the Echobode manual documents the same, verbatim: *"even a frequency
  shifting of 0 Hz will affect the sound"* — verified in the user guide). This is our unity-through
  statement for §7, and it survives the §3.3 sign correction unchanged.
* Ring mod (`m = 0`): `y = g·I·C` — classic balanced modulation of the (allpassed) input. Both
  sidebands, carrier suppressed, spectral line count doubles. The Buchla-285 case for free.

### 3.4 Reflection at 0 Hz and Nyquist — and the `Guard` stage (Echobode's Anti-Refl, exact recipe)

Down-shifting a partial below 0 Hz (or up past Nyquist — the digital spectrum is circular, both land
in the negative-frequency half of the complex spectrum) then taking `Re(y)` **folds it back, mirrored
and conjugate**. Musically this mirror is half the charm (the "underwater collapse"), so it must be
**switchable, not silently removed**. The removal stage, when on, is a **positive-frequency projection
applied to the complex product BEFORE taking the real part**:

```
Z = (I − jQ)·e^{jωt}      (Zr = I·C + m·Q·S,  Zi = I·S − m·Q·C — compute both products)
positive-frequency part:  P = ½·(Z + j·Ĥ(Z)),  Ĥ(Z) = H(Zr) + j·H(Zi)
output:  y_guard = Re(P) = ½·( A_I(Zr) + A_Q(Zi) )
```

**[AUDIT] all three signs here were flipped too** (same root cause as §3.3: the draft assumed the
analytic signal is `I + jQ`; measured, Q leads, so it is `I − jQ`). Re-derived: `H{A_I(v)} = −A_Q(v)`
⇒ `Re(P) = ½·(A_I(Zr) + A_Q(Zi))`. The `P = ½(Z + j·Ĥ(Z))` projection itself is correct (checked
against `Z = e^{jωt}`: passes ω>0 unchanged, nulls ω<0).

where `A_I` = the I-cascade (+its 1-sample delay) and `A_Q` = the Q-cascade — i.e. **one more
Niemitalo pair applied to the two product signals**. *(⚠️ the Guard pair must carry the SAME network
and the SAME `iDelay` decision as the main pair — §3.1b — so a `Dome '64` Guard is a dome pair with no
`iDelay`, or the guard leaks worse than what it removes.)* Cost: +8 multiplies/channel. This matches
Echobode's claim that Anti-Refl removes reflections "with the same quality as the sideband
suppression" (it literally reuses the same network) and its note that the stage "introduces additional
phase shifting" (of course — 8 more allpasses). `Guard` is a front pill; toggling it **fade-swaps**
between the two outputs over 30 ms (house law 7) — both are always computable, the guard path simply
isn't run when the pill is off (CPU).

### 3.5 The loop — Echobode topology on our chassis

```
in →(+ fb·envGate)→ [Delay 0.02 ms…1 s | synced] → [Blur: 4 Schroeder allpasses]
   → [loop HP 20 Hz fixed + DC block] → [Damp: loop LP] → [char nonlinearity (per §4.8)]
   → [Hilbert → shift → sideband → (Guard)] → [Cross L↔R (per type)] → wet out
fb tap = wet out, through DCBlocker (in-tree pattern TerrainFilters.h:1169)
```

**LOOP-GAIN LAW (house law 6) — every stage audited:** Hilbert network |H| = 1 (allpass); shift stage
|gain| ≤ g·max(1,|m|) ≤ √2 at ring center — **the sideband compensation g is INSIDE the loop, so the
loop budget must assume √2**; Blur allpasses = unity; Damp LP ≤ 1; char saturator slope ≤ 1 at origin.
Therefore: **fbEff = fbKnob · (1/g(m)) · envGate = fbKnob · √((1+m²)/2) · envGate, hard ceiling 0.95**
(the DST P8 precedent). **[AUDIT] the draft ALSO stated a piecewise form — "1/√2 when |m| < 0.5 else 1"
— which is a step discontinuity at |m| = 0.5 and directly contradicts the very next sentence's "applied
smoothly as fb/g". The piecewise form is deleted; `1/g` is the whole law** and it is continuous
(1/√2 at ring center → 1.0 at either sideband), so the Sideband knob can never push a stable loop
unstable mid-sweep. **envGate** = input envelope follower (attack 5 ms, release 250 ms,
squared-release per the Phase G law) mapped `gate = min(1, env/env_ref)` with `env_ref` = −38 dBFS —
i.e. the gate is fully open for any real program on the −26 dBFS bus and **closes ~2.5 s after the
note dies, taking every tail with it. Nothing free-runs; sound dies with the note.** Self-oscillation
drones are therefore *sustain-while-played* only — that is the house reading of FreqEcho's
"self-oscillation" fun.

**Barberpole loop stability is special:** with Delay at minimum (0.02 ms ≈ 1 sample) + fb 0.9, the loop
is a 1-sample comb whose teeth *walk* at shiftHz. **[AUDIT] the draft justified the pad with the wrong
gain figure.** `1/(1−fb) = 10× (+20 dB)` is the **PEAK at a comb tooth**, not what a program does to
the meter. For broadband program the comb's **RMS** gain is `1/√(1−fb²)` = **2.29× (+7.2 dB)** at
fb 0.9. The specified pad `1 − 0.7·fb` = **0.37 (−8.6 dB)** at fb 0.9 tracks that RMS figure to within
~1.4 dB, which is why the pad works — quote the RMS law, not the peak, or the next person "fixes" the
pad to −20 dB and the type goes silent. Peak headroom is handled by the 0.95 ceiling + the output
DCBlocker/limit, not by the pad.

### 3.6 Param laws (range · taper · glide) — the exact table lives in §5; the derivations:

* **Shift taper:** `shiftHz = sgn(v)·(5001^|v| − 1)`, v ∈ [−1, 1]. ±5 kHz full scale, and the
  derivative at center is ln(5001) ≈ 8.5 Hz/unit — the first ±10% of travel spans ±1.3 Hz, so the
  sub-Hz magic (detune, rotary) is ON the knob, not hidden (law 5: the whole sweep reads). Dial legend
  mirrors Echobode: 0 · 3 · 10 · 30 · 80 · 200 · 1k · 5k each side. **[AUDIT] taper arithmetic
  re-checked: |v|=1 → 5000.0 Hz ✓, |v|=0.1 → 1.344 Hz ✓, d/dv at 0 = 8.517 Hz ✓.**
* **[AUDIT] Why ±5 kHz — the ARGUMENT, not an assertion** (the draft only asserted it in §5.1 and
  half-argued it in a question). Four independent reasons, in order of weight:
  1. **The hardware converges there.** The Bode/Moog 1630 is ±5 kHz; **Doepfer's A-126-2 quadrature
     VCO spans ~20 Hz–5 kHz** (verified on doepfer.de) — two unrelated analog designs, 40 years apart,
     both stopping at 5 k.
  2. **Musical resolution beats musical reach on a log taper.** Every Hz of top range costs
     resolution everywhere else: the same 5001-base taper stretched to ±20 kHz (Echobode's Wide) makes
     the center derivative ln(20001) ≈ 9.9 Hz/unit, i.e. **16% coarser at the sub-Hz end where the
     detune/rotary/barberpole material lives** — and that is 90% of this device's use.
  3. **Above ~5 kHz the output is reflection product, not shift.** On synth program (fundamentals
     50–1000 Hz) a +5 kHz shift already puts the entire fundamental region above 5 k and hard
     down-shifts fold the low partials through 0 Hz. Past that it is `Guard`-off trash-register
     territory, which we reach anyway with big shifts + Guard off.
  4. **It costs nothing to be wrong in the safe direction.** The taper is one constant; if Max wants
     Wide, `5001 → 20001` is a one-line change *before ship* (after ship it is a preset-compat break).
  ⇒ **Ship ±5 kHz. §13-Q3 stays open only as a taste question, not an unresolved engineering one.**
* **Kinetic remap (the elegant law):** a shift of f Hz IS a phaser sweeping f times per second — the
  quantity is the same. So in the two kinetic types (`Barberpole`, `Rotary`) the same `Shift` knob
  remaps to ±0.05…±8 Hz (`rate = sgn(v)·0.05·160^|v|`), and with the `Sync` pill ON it snaps to the
  house division list as a *cycle rate* — exactly Echobode's Range=Sync behavior.
* **Glides:** shiftHz → one-pole smoothed at 30 ms before Δ is computed (phase-continuity does the
  rest — zipper-free by construction); Sideband m → 20 ms; fb → 20 ms; Delay time → the DelayEngine
  glide law (comb-click law — NEVER snap a delay length; reuse its cubic-interp glide); Blur/Damp →
  per-block coefficient update + 20 ms; type/Character switches → **fade-swap-recover** exactly like
  the DST char-switch deferred fade (Phase G: fade → swap → re-seat state → recover).
* **Track (env → shift) bus calibration (law 1):** follower normalized so that **−20 dBFS peak program
  = 1.0 deflection** (the −26 dBFS nominal bus + 6 dB crest); `shiftTrack = ±1200 Hz · t² · env_n`.
  Stated relative to the bus, not to literature.
* **[AUDIT] THE −6 dB ROUTE SPLIT — the draft missed this and it breaks BOTH followers.** The bus is
  not one level. `kVoiceToFxPad = 0.5f  // -6 dB` (`PluginProcessor.cpp:6300`) is applied to the
  **per-osc routed** send (`raw * outputGain * kVoiceToFxPad`, e.g. :7320-7324) but the **main-send**
  branch feeds `leftChannel[i] − rtdL` **unpadded** (:7159-7161). So the same program arrives at the
  engine **6 dB hotter on main-send than on a routed pill** — a Track follower and an envGate
  calibrated on one will mis-fire on the other (routed sources would sit at ~−32 dBFS and, with
  `env_ref = −38 dBFS`, ride only ~6 dB above the gate knee instead of ~12). **Fix: normalize both
  followers to the ROUTED level (−32 dBFS peak = 1.0) and let main-send run 6 dB into the top of the
  curve, OR multiply the follower input by `1/kVoiceToFxPad` when `!routeActive`.** Pick one, write it
  in the engine, and cert it in `bod_cert` gate 6 **in both route modes** — the DST/DLY devices carry
  the same split, so this is a rack-wide correctness point, not a Bode quirk.

### 3.7 Oversampling verdict: **NEVER.**

The shifter is linear (allpasses + multiplication by a pure sinusoid generate no harmonics; the only
spectral images are the sideband/reflection products, which are handled by suppression and `Guard`,
not by rate). The in-loop Character saturator runs at ≤ unity loop gain on a −26 dBFS bus: measured
budget — `tanh` at those levels produces H3 < −60 dB, whose aliases land < −70 dB. Inaudible;
oversampling would be pure waste (law 8). The `Crush` Character adds deliberate aliasing — that IS the
mode. **No Quality dropdown exists on this device** — one less param than Distortion needed, and the
chassis stays at the official 2 dropdowns.

### 3.7b Latency verdict: **ZERO. Reported and actual. [AUDIT] — added, this was missing entirely.**

Rack law A: **nothing in this rack may report latency.** The fb305/fb338 main-send exclusion sums
subtract the routed dry **sample-aligned** (`leftChannel[i] − rtdL`, `PluginProcessor.cpp:7159`); a
device that reported N samples of latency would make the host delay-compensate the wet while the dry
subtraction stays at t=0, and the leaked dry comes back **phase-smeared**. Consequences for THIS device,
which is the one class of effect most likely to violate it:
* **NO FIR / windowed-sinc / linear-phase Hilbert transformer.** The textbook Hilbert design is an
  odd-length FIR needing **(N−1)/2 samples of latency** (N is typically 63–255 ⇒ 0.6–2.7 ms). It is
  banned here. The IIR allpass pair of §3.1 is the *only* admissible construction — which is
  convenient, because it is also the cheap one and the one already in the tree.
* The pair's own 1-sample `iDelay` (§3.1b) is **internal alignment between two wet paths**, not
  latency: the wet output is still time-aligned with the dry to the sample. `setLatencySamples` is
  never called; `getLatencySamples()` stays 0.
* Allpass **group delay** (both networks, plus `Blur`) is phase, not latency — it is the whole point
  of the effect and must never be "compensated".
* Corollary for §11: any future "Guard HQ" / "linear-phase Low Keep" idea is dead on arrival. `Low
  Keep` is an **LR2 (minimum-phase) crossover**, as §5.2 P5 already specifies. Keep it that way.

### 3.8 Denormal / DC / numeric traps (all mandatory)

DCBlocker after the sideband stage AND in the fb tap (down-shift parks real energy AT 0 Hz — without
the blocker the loop integrates it into a thump); flush denormals in the Blur/Delay tails (reuse the
alternating ±1e-18 injection idiom, `TerrainFilters.h:1101`, + `ScopedNoDenormals`); quadrature-osc
renorm every 512 samples (in-tree); `flush()` clears all 16(+16 guard) allpass states, delay, blur,
follower — wired to the device power fade like DST.

---

## 4. Types — each night-and-day, each with its measurable discriminator *(roster count UNRESOLVED — §4.0)*

*(One engine; a Type = a topology/remap preset over §3.5. All eight back knobs stay live in every
type — the per-type meaning table is in §5.3. Characters in §4.8 apply to all types.)*

### 4.0 ⚠️ **[AUDIT] THE ROSTER DOES NOT PASS LAW 5 AS DRAFTED — RESOLVE BEFORE LOCKING THE CHOICE COUNT**

Law 5 cuts both ways: *"Every Type night-and-day distinct with a stated measurable discriminator, or
cut — and inventing fake Types to fill a dropdown is also a defect."* Reading §4.1–§4.7 against the
§5.2 knob map, **there are 7 names sitting on 4 topologies**:

| Type | Actual topology | Verdict |
|---|---|---|
| `Shift` | SSB, loop bypassed | **real** |
| `Detune` | mirrored ±S, loop bypassed, sub-Hz remap | ⚠️ **= `Split` with a different Shift remap + Time default** |
| `Split` | mirrored ±S, loop bypassed, full-range | ⚠️ **same topology as `Detune`** — §4.2 and §4.3 both specify "L +S, R −S at full Spread" |
| `Barberpole` | shifter in a 1-sample–4 ms comb + fixed 50% internal mix | **real** (the internal mix is a genuine topology difference) |
| `Spiral` | shifter inside the delay loop | **real** |
| `Rotary` | mirrored ±S at a few Hz, loop bypassed | ⚠️ **same topology as `Detune`** — differs only in rate zone and Mix default |
| `Track` | SSB + P7 ≠ 0 | ❌ **not a type.** §4.7's own discriminator says *"static-input spectrum is IDENTICAL to `Shift`"* and §5.2 says P7 is **"alive in all types"** — so `Track` is `Shift` with one knob turned up. That is a **preset**. |

The harness cannot save these: `bod_cert` gate 3 (beat uniformity) and gate 6 (AM-burst tracking)
would give **identical numbers** for `Detune`/`Split`/`Rotary` at matched knob settings, and for
`Shift`/`Track` at matched P7. A discriminator you can only produce by also changing a knob default
is a preset discriminator, not a type discriminator.

**Three admissible resolutions — Max picks ONE before a line of C++ is written**
*(this is now the top item of §13, ahead of the viz question):*
* **(A) Cut to 4 real types** — `Shift` · `Widen` (absorbs Detune+Split+Rotary; Spread + the Shift
  taper already reach all three sounds) · `Barberpole` · `Spiral` — and **still declare the choice list
  at its FINAL size on day one** with the extra entries present-but-disabled (rack law C: cardinality
  is frozen at birth; see §5.2). Ship `Track` as **factory preset #11**, which is what it already is.
* **(B) Keep 7 but EARN them** — give each contested type a real topology hook that the harness can
  resolve: `Detune` = mirrored shift **with a fixed internal 50% dry sum** (so the beat is the output,
  mono-safe, Mix-independent); `Split` = mirrored shift **with no internal dry and hard L/R
  decorrelation** (Mix 100 behaviour); `Rotary` = mirrored shift **through a 2-tap L/R amplitude
  rotator locked to the shift rate** (the actual Leslie construction — L/R *amplitude* motion, which
  §4.6's discriminator already claims but no §4.6 DSP produces). `Track` gets cut or becomes a pill.
* **(C) Ship 5** — `Shift` · `Widen` · `Barberpole` · `Spiral` · `Rotary`-as-(B) — the smallest roster
  where every entry has a distinct block diagram.

**Until this is settled, treat the "7" in §0, §5.2 and §12 as UNRESOLVED.** Everything else in §4
(the recipes, the discriminators, the harness gates) is sound and survives any of the three choices.

### 4.1 `Shift` — the Bode 1630 *(reference type, default)*
Plain SSB. Delay defaults 0 (loop = phaser regen), full ±5 kHz on the knob, `Sideband` at up.
**Recipe:** §3.3 verbatim. **Discriminator:** every partial displaced by the SAME Hz (spectrogram:
parallel vertical translation, spacing preserved, harmonicity destroyed); image ≤ −44 dB (Pristine).
Blind test: a shifted sawtooth's partial spacing stays 100 Hz while its fundamental sits at 173 Hz.

### 4.2 `Detune` — the sub-Hz thickener
`Shift` knob remaps to ±15 Hz (`shiftHz = sgn(v)·15·|v|^1.6`), Spread defaults to full mirror
(L +S, R −S), Delay defaults 8 ms with 15% fb (micro-comb body). The "chorus without wobble":
SSB detune has **zero pitch modulation** — nothing undulates, it just *widens*.
**Discriminator:** the beat rate against the dry is **identical for every partial** (= 2S between
channels, Costello's law) — a chorus shows beat rate ∝ partial number; this shows a flat line.
Mono-compatible at low S (unlike chorus's comb): verify correlation ≥ 0.9 at S ≤ 1 Hz.

### 4.3 `Split` — stereo opposite shifts *(H3000 Dual-Shift / Ableton Wide lineage)*
L gets +S, R gets −S at full `Spread`; the knob still scales the base. Big S = two different bells in
two ears; small S = the widest "one voice" trick in the rack. **Discriminator:** L/R magnitude spectra
are mirror-translations (corr(specL(f), specR(f−2S)) ≈ 1); inter-channel beat at exactly 2S; stereo
correlation collapses toward 0 as S grows. **Mono-sum warning shipped in the manual:** summing
L(+S)+R(−S) reconstructs a ring-mod-like DSB — dramatic but different (§11.6).

### 4.4 `Barberpole` — the infinite phaser *(DAFx-15, Esqueda/Välimäki/Parker)*
Kinetic remap: `Shift` = sweep rate ±0.05…8 Hz (sign = rise/fall), synced divisions with the `Sync`
pill. Internally: 50% internal comb mix (the paper's construction), Delay forced short
(0.02–4 ms via `Time`), fb defaults 0.85, Damp dark-ish. The notches glide up (or down) FOREVER.
**Discriminator:** spectrogram shows continuous diagonal striping with period exactly 1/rate;
notch positions are monotone mod the comb spacing — no turnaround point ever (a swept phaser reverses;
this cannot). Idle → silence (envGate); the illusion reads best slow — default rate 0.4 Hz.

### 4.5 `Spiral` — the Echobode/FreqEcho echo staircase
The full loop: `Time` synced (default 1/8, list §5), fb 0.6, the shifter runs INSIDE the delay loop so
**every repeat is shifted again**: echoes at S, 2S, 3S… — a glissando staircase rising (or falling,
or collapsing through zero with `Guard` off). Blur turns the staircase into a shifting wash.
**Discriminator:** spectrogram of an impulse = a staircase — n-th echo displaced by exactly n·S;
against Delay-the-device: its echoes keep their spectrum, Spiral's walk away by S per pass.

### 4.6 `Rotary` — the SoS rotating-speaker illusion
Kinetic remap (±0.05…8 Hz), micro-shift with **anti-phase L/R application** (the Echobode
LFO-Stereo/Ableton-Wide construction): L leads +S while R gets −S, producing alternating L/R
cancellation — the head-spinning Leslie illusion, with `Blur` as cabinet smear.
**Discriminator:** L and R RMS envelopes anti-correlate at exactly the rotor rate (r < −0.7 at
rate ≤ 2 Hz); the goniometer image visibly rotates. Differs from `Detune` (static width, no rotation)
and from `Barberpole` (spectral notch motion, no L/R energy rotation).

### 4.7 `Track` — the envelope-steered dive/rise
`Shift` = base; **P7 `Track` is the star**: bipolar env-follower depth ±1200 Hz (§3.6 calibration).
Positive: hits bloom upward into clang and fall home as they decay (laser zap that plays itself);
negative: attacks collapse underwater and resurface. **Discriminator:** shift measurably follows the
program envelope — on a −26 dBFS burst decaying 20 dB, the wet spectral displacement glides from
depth·1.0 to depth·0.1 with the follower's release; static-input spectrum is IDENTICAL to `Shift`
(the Phase G probe-craft law: transient params need AM probes — a static probe cannot see this type).

### 4.8 The `Character` dropdown — 6 voicings, physics not EQ

1. **Pristine** *(default)* — Niemitalo network, no leak, no nonlinearity. Image ≤ −44 dB
   (measured worst case 100 Hz–16 k @48 k: **−44.3 dB**; −49 dB at the 1 kHz probe).
2. **Dome '64** — the Hutchins 6+6 dome (§3.1). **[AUDIT] SPEC REWRITTEN — the draft had this
   Character backwards.** It is not "image floor ~−30 dB everywhere"; measured, the dome is
   **CLEANER than Pristine below ~11 kHz** (−58.9 dB @100 Hz, **−55.9 dB @1 kHz**, −68 dB @8 kHz)
   and **collapses above ~12.3 kHz** (−32 dB @14 k, −27 dB @15 k, **−23 dB @16 k**, −10 dB @20 k).
   ⇒ its voice is **"immaculate mids, a top octave that mirrors"** — shift a cymbal or a bright saw
   and the air turns into a descending ghost. That IS the analog sound, and it is a *better* Character
   than the one the draft described, because it is frequency-selective instead of a flat dirt floor.
   Requires **no `iDelay`** (§3.1b). *(Tell: the 1 kHz probe shows NO image (< −55 dB) while an
   8 kHz probe's image sits at ~−23 dB — the pass/fail is the **slope between them**, not a single
   number. Cert it at 1 k AND 16 k or you will grade it as Pristine.)*
3. **Tube 735** — Dome network + in-loop `tanh` input stage trimmed for H3 ≈ −40 dB on bus program +
   carrier leak at −50 dB (a faint tonal ghost at the shift frequency itself — every analog unit
   leaks carrier; `Drift` raises it, see below). *(Tell: visible carrier line in the FFT.)*
4. **Leaky** — image suppression deliberately broken to **−18 dB** + carrier −35 dB: both sidebands
   audibly present = the cheap-SSB-radio / walkie-talkie voice, halfway to ring mod at all times.
   *(Tell: image within 18 dB of main.)*
   **[AUDIT] the draft gave the target but no mechanism — here it is, exactly.** Use **amplitude
   imbalance**, not phase error (phase error is fs- and frequency-dependent and would drift the target
   across the band): `y = g·( I·C + m·k·Q·S )` puts the image at `20·log₁₀((1−k)/(1+k))`, so
   **k = 0.7764 ⇒ −18.0 dB, flat across the whole band**. Carrier leak = add `0.0178·C` (−35 dB) to
   the output *before* the DCBlocker. Both are one multiply; `Drift` scales the carrier term (§5.2 P8).
5. **Tape Loop** — loop nonlinearity becomes soft-knee tape squash + Damp gains a fixed 6 kHz
   shoulder + `Drift` becomes wow (0.5 Hz walk on the DELAY, not the shift). Spiral/Barberpole turn
   into Echoplex-with-a-shifter. *(Tell: loop HF decays 2× faster per pass; echo pitch wobbles.)*
6. **Crush** — in-loop 8-bit / ÷4-rate decimation (the Melda "Ugly" pole): each pass through the loop
   re-grinds the shifted signal; aliases ARE the voicing. *(Tell: alias forest above the shifted
   partials, quantization floor −48 dB.)*

Every Character re-voices the LOOP + NETWORK physics; none is a tone knob. Char switch = fade-swap
with state re-seat (Phase G law — re-seat the allpass states from the live input, not zeros, to kill
the swap thump).

---

## 5. Chassis map — the fb275 device (2 dropdowns + 8 back knobs + 3 hero + Mix)

**Device name on the rack: `Bode`.** Grammar copies `SYN_DST_*` (**ParameterIDs.hpp:406-431** —
*[AUDIT] the draft said 406-424, which stops at `SYN_DST_SRC_C` and misses SRC_D/SUB/NOISE, POWER,
AUTO and PILL2; the full block is 406-431*) → **`SYN_BOD_*`**, + SRC pills / POWER / routing inherited
unchanged from the DST device plumbing.

### 5.1 Front card — 3 knobs + Mix (+ pills + the live viz §6)

| Knob | ID | Range / taper | Glide | Notes |
|---|---|---|---|---|
| **Shift** | `SYN_BOD_SHIFT` | bipolar ±5 kHz, `sgn(v)·(5001^|v|−1)` Hz; **kinetic types: ±0.05–8 Hz rate**, Sync pill → division list | 30 ms + phase-continuous osc | THE hero. Center detent = 0. |
| **Sideband** | `SYN_BOD_SIDEBAND` | 0..1 → m = 2b−1 (down ↔ ring ↔ up); default 1.0 (up) | 20 ms | The 1630 `Mixture`. Center = ring mod, +3 dB comp (§3.3). |
| **Feedback** | `SYN_BOD_FEEDBACK` | 0..1 → 0…0.95 loop gain (√2-de-rated near ring, §3.5), env-gated | 20 ms | Phaser regen ↔ spiral echoes ↔ barberpole scream. |
| **Mix** | `SYN_BOD_MIX` | equal-power; **100% = FULLY WET** (dry residual < −60 dB) | 20 ms | House law 4. |

**Front pills:** `Sync` (kinetic-rate / Time sync, default OFF) · `Guard` (anti-reflection §3.4,
default ON) · power pill per house. Pills fade, never cut (law 4/7).

### 5.2 Back panel — EXACTLY 2 dropdowns + 8 knobs (4×2)

**Dropdowns:** `SYN_BOD_TYPE` — choice(N) *(**⚠️ N is UNRESOLVED until §4.0 is answered.** Session
law ① / rack law C: cardinality is forever. So **declare the list at its FINAL length on day one**
and disable the entries the chosen resolution doesn't ship — do NOT declare 7 today and re-count
later. Recommended: declare **8** and light up whichever roster §4.0 resolves to, leaving the rest
disabled for a future addition)*.
`SYN_BOD_CHARACTER` — choice(6): Pristine / Dome '64 / Tube 735 / Leaky / Tape Loop / Crush.

| # | Knob | ID | Range / taper | Glide | Job (pragmatic name = what it does) |
|---|---|---|---|---|---|
| P1 | **Fine** | `SYN_BOD_P1` | bipolar ±2.00 Hz linear | 30 ms | Vernier added to Shift — the 1630 Zero-Adjust. Sub-Hz beating lives here. |
| P2 | **Spread** | `SYN_BOD_P2` | 0..1 → R = L·(1−2s) (R sweeps +S → 0 → −S) | 30 ms | Stereo shift mirror. Split defaults 1.0; every type widens with it. |
| P3 | **Time** | `SYN_BOD_P3` | free 0.02 ms…1 s (log) · Sync pill ON → the 20-entry list `Free,4 bar…1/256` (reuse SYN_DLY_SYNCDIV verbatim, PluginProcessor.cpp:3455-3459) | delay-glide law | Loop delay: micro = phaser comb → flanger → echoes. Law 3 satisfied. **[AUDIT] the 20-entry list was read and CONFIRMED** (`Free,4 bar,2 bar,1 bar,1/2,1/2D,1/2T,1/4,1/4D,1/4T,1/8,1/8D,1/8T,1/16,1/16D,1/16T,1/32,1/64,1/128,1/256`, default index 10 = 1/8). **⚠️ BUFFER TRAP the draft missed: "4 bar" at 60 BPM is 16 s, but the free range stops at 1 s.** Size the BodeEngine delay line for the SYNC ceiling like `DelayEngine::prepare` does (`DelayEngine.h:38-40`, `ceil(16.5·fs)+8`, rounded up to a power of two), or the top four divisions silently truncate. |
| P4 | **Blur** | `SYN_BOD_P4` | 0..1 → 0–4 Schroeder allpasses wet, 5–35 ms coprime | 20 ms xfade | In-loop diffusion (Serum 2 `Blur` / Echobode `Smear`): echoes → wash. |
| P5 | **Low Keep** | `SYN_BOD_P5` | 20 Hz…2 kHz log; 20 = off | 30 ms coeff | Crossover below which DRY bypasses the shifter — anchors the bass and keeps sub out of the loop. **[AUDIT] the "hides the <48 Hz image breakdown" rationale is DELETED — measured, there is no breakdown above ~22 Hz at 44.1/48 k (§3.1). It IS real at 96 k (edge ~44 Hz) ⇒ default P5 ≥ 60 Hz when fs ≥ 88.2 k.** LR2 (minimum-phase) crossover, phase-matched sum — **never linear-phase** (rack law A, §3.7b). |
| P6 | **Damp** | `SYN_BOD_P6` | loop LP 700 Hz…40 kHz log (default 18 k) | 30 ms coeff | In-loop darkening — barberpole/spiral tails die dark like tape. |
| P7 | **Track** | `SYN_BOD_P7` | bipolar → ±1200 Hz·t²·env (§3.6, −26 dBFS-calibrated) | follower does it | Envelope → shift. Center = off. The `Track` type's star; alive in all types. |
| P8 | **Drift** | `SYN_BOD_P8` | 0..1 → shift random-walk 0–6 Hz @ 0.3 Hz (SmoothRandom reuse) + per-Character analog misbehavior (carrier leak ×, wow on Tape Loop) | walk is the glide | Age/instability. 0 = digital-still. |

Param count the fb275 way: **3 hero + Mix on the front, 8 on the back**, 2 back dropdowns, pills —
identical accounting to Reverb/Delay/Distortion. **[AUDIT] arithmetic corrected: that is 12 continuous
params, not 11.** The fb275 shorthand "11 per device" counts the 3 hero + 8 back knobs and treats
`Mix` as the universal extra that every device carries (verified against DST, which declares
`DRIVE, SIG, TONE, MIX` + `P1…P8` = 12 floats at `ParameterIDs.hpp:409-421`). The **binding**
constraint — the one the chassis actually enforces — is **exactly 2 dropdowns and exactly 8 back
knobs in a 4×2 grid**, and this device meets it. *(Note DST ships 3 dropdowns — TYPE, CHARACTER,
QUALITY — so it is DST that bends the rule, not us; §3.7's no-Quality verdict is what keeps Bode
compliant.)* Every knob is live in every Type (the per-type *meaning* shifts are in the rows above;
no dead knobs — law 5).

### 5.3 Processor integration

* Engine file `Source/BodeEngine.h`, self-contained + shim-compilable (the DistortionEngine.h law:
  no TerrainFilters include; copy `BodeAP`/DCBlocker in, cite fb283). API mirrors
  DelayEngine/DistortionEngine exactly: `prepare(double)` · clamped setters ·
  `processSample(inL,inR,outL&,outR&)` returning WET ONLY · `flush()`.
* **⚠️ THE fb305 LANDMINE (4th send bus):** every send bus joins EVERY main-send exclusion sum. The
  bode send must be added to the `rtdL/rtdR` sums at **PluginProcessor.cpp:7159, 7161, 7326, 7328,
  7358, 7360** (currently `rvb + dly + dst`) AND gets its own main-send/routed branch like
  `dstSendL/R` (:6457, :7320). Miss one line = the fb305 double-audio bug returns.
  **[AUDIT] ALL EIGHT LINE NUMBERS RE-READ AND CONFIRMED EXACT** at this HEAD. The six are **three
  exclusion sums × two channels** — the reverb main-send (7159/7161), the distortion main-send
  (7326/7328) and the delay main-send (7358/7360) — which is what rack law D means by "all three
  exclusion sums". *(Law D's "index.html:6979, :7111" is **stale**: those lines are UI SVG/CSS today.
  The exclusion arithmetic lives only in `PluginProcessor.cpp`. Do not go hunting in index.html.)*
* **⚠️ `SYN_FX_ORDER` — [AUDIT] THE DRAFT'S INSTRUCTION VIOLATES RACK LAW C AND MUST NOT BE FOLLOWED.**
  The draft said the param *"grows to include the 4th device — bump the choice list ONCE, now."*
  It cannot grow. It is a **6-entry `AudioParameterChoice`** declared at `PluginProcessor.cpp:3488-3494`
  (`"Reverb > Delay > Distortion"` … `"Delay > Reverb > Distortion"`) and read as
  `juce::jlimit(0, 5, (int)rawParam(...))` at `:5860`. **Choice cardinality is fixed at birth
  (fb342 / rack law C): JUCE/VST3/AU cache the parameter list, and 3 devices = 3! = 6 while 4 devices
  = 4! = 24.** Changing 6 → 24 in place re-scales every stored normalised value: an existing session
  saved as index 5 (norm 1.0) reloads as index 23 — **every user patch silently reorders its FX chain.**
  Two admissible routes, pick before B5:
  1. **Pre-size a NEW param** `SYN_FX_ORDER2` at the FINAL roster size (24 for 4 devices, or larger if
     Chorus/Compressor are coming — see the roadmap; **size it for the final rack, once**), migrate the
     6 legacy orders into it on state load, and leave `SYN_FX_ORDER` declared-but-unused for
     backward compat. This is the fb342 "sized for the final roster, disabled entries allowed" pattern.
  2. **Or insert Bode at a FIXED position** (as DST itself did before fb341 — see the note at
     `ParameterIDs.hpp:436-438`) and defer the reorder entirely.
  ⇒ §12 row 5 and §11.9 are updated accordingly. **Never renumber a live choice param.**
* WebSliderRelay 4-point binding per param (the silent-no-op trap); BinaryData cache-bust on the UI;
  choice params read as INDEX (`(int)*rawParam`).

---

## 6. Visualizers

### 6.1 How the greats draw this effect

* **Echobode**: hardware fantasy — the giant mirrored log dial (0·3·10·30·80·200 both directions) and
  an **analog VU needle showing the live LFO value**. No spectrum. Charm over information.
* **Serum 2 Bode**: (exact viz unverified — Max, one screenshot settles it, §13-Q1). Serum 2 FX
  visualizers generally animate the *parameter mechanism* over a live-audio-reactive backdrop.
* **Ableton Shifter**: an LFO shape display + numeric shift readouts; the env-follower amount is a
  meter overlay. Informative, not dramatic.
* **Melda MFreqShifterMB**: full analyzer with band split overlays — maximal information, zero poetry.
* **Kilohearts**: a bare numeric shift display.

Nobody in the field shows **the spectrum actually sliding** — which is the one thing a frequency
shifter does. That gap is our card.

### 6.2 Our card — 3 concepts (canvas 2D, CPU-cheap, law 9: idle = dim, playing = bold)

1. **Sliding Spectrum** *(primary recommendation)* — reuse the `SpectrumAnalyzer` **class**
   (`SpectrumAnalyzer.h:20`; FFT_ORDER 12 = 4096, HOP 1024, `NUM_BUFS 3`, ~47 frames/s @48 k — all
   verified). **[AUDIT] but the FEED is NOT already there.** The only two instances in the tree are
   `analyzerPre` / `analyzerPost` (`PluginProcessor.h:924`), and they are the **EQ device's** pre/post
   taps (`PluginProcessor.cpp:6865, 6869`), pushed to the UI on a lane gated to `uiPage 1` and to
   frame-freshness (`PluginEditor.cpp:5881-5904`). The Bode card needs **its own two instances + its
   own visible×fresh 60 Hz push lane**, copying that gating pattern verbatim (fb342 — an ungated
   analyzer push is exactly the frame-drop tax that pass cost us). Budget it: +2 × 4096-pt FFT at
   ~47/s, only while the card is on screen. Content: dry spectrum as a dim white silhouette; wet spectrum as
   the bright filled ridge **visibly displaced by Shift** (the displacement IS the knob); a translucent
   ghost ridge at the mirror position whose opacity = image level (rises with Leaky/Dome — Character
   becomes visible); `Sideband` at center draws BOTH ridges half-bright (ring mod is *seen*). Idle:
   everything collapses to a dim baseline. Push lanes @60 Hz only while visible×fresh (fb342 law);
   no shadowBlur (session law ⑤).
2. **Barberpole Drum** — diagonal stripes scrolling at *exactly* shiftHz (kinetic types: the rate),
   direction = sign, stripe brightness = live RMS (idle = near-black), stripe persistence/trail
   length = Feedback, stripe softness = Blur. In `Spiral` the stripes wrap into a helix ring. The most
   dramatic possible statement of "this thing moves forever in one direction."
3. **Phasor Rose** *(the DSP-truthful one, nearly free)* — plot the actual (I, Q) analytic pair as a
   Lissajous orbit, ~64 points/frame: radius = live amplitude (dies to a dim dot at idle), rotation
   rate = shift (sub-Hz detune becomes a slow majestic sweep; ring center makes the rose fold into a
   line), Spread splits it into two counter-rotating orbits. Zero extra DSP — tap two floats.

**Recommendation:** Sliding Spectrum as the card core with the Phasor Rose as a corner satellite
(both feeds are already computed); Barberpole Drum as the kinetic types' background layer. Every
sound-changing param maps to a visible delta (walk: Shift→displacement/scroll, Sideband→dual-ridge,
Feedback→trail, Time→echo tick marks, Blur→softening, Low Keep→split line, Damp→ridge tilt,
Track→displacement breathing with the meter, Drift→wobble, Fine→slow beat shimmer of the ghost,
Spread→L/R ridge separation, Character→ghost-height/carrier-pip). Law 9 satisfied by construction.

---

## 7. Interplay — the device in a chain

* **Unity discipline:** defaults (Shift 0, Sideband up, fb 0, Mix 50) pass unity magnitude with
  rotated phase (§3.3). *Phase rotation means default-on CHANGES a parallel sum* — the device
  defaults **power OFF** like every rack device, and the manual notes the phaser-comb that appears
  when mixing wet+dry at 0 shift is Echobode-documented behavior, not a bug.
* **Ordering wisdom:** Bode → Reverb = inharmonic wash that still tails naturally (shift the source,
  not the tail). Reverb → Bode = the entire tail detunes/spirals — the Eno-esque "shimmer's evil
  twin"; both are legit, preset-encoded. Distortion → Bode: shifted clang from harmonic distortion.
  Bode → Distortion: intermod hell (the shifted inharmonic set hits the nonlinearity — dense, metallic;
  Gargle-class loudness, watch the preset trim). Bode before Delay: each echo repeats the SAME shifted
  spectrum; Delay before Bode: one shift over the echo sum. Spiral-type inside = per-echo shift
  (only this device can do that).
* **Spectrum/dynamics downstream:** the device conserves energy (allpass + rotation); downstream
  changes are spectral-position only — EXCEPT ring center (+3 dB comp keeps RMS but doubles line
  density: brighter percept) and feedback types (comb peaks up to +6 dB at fb 0.5 — the §3.5 pad
  keeps program RMS ±1.5 dB across the fb sweep; verify in harness).
* **Stacking trap:** two Bodes in series with opposite shifts do NOT cancel back to dry (each pass
  keeps only one sideband; phases rotate) — near-dry magnitude, audibly phasey. Manual-note it.
* **Mono-sum:** `Split`/`Spread` collapse to DSB ring-ish in mono (§4.3); the harness prints the
  mono-fold spectrum for every preset (§10).

---

## 8. Presets — 12 factory sketches (bus-calibrated, every one heard at −26 dBFS program)

| # | Name | Type/Char | Sketch (non-listed = default) |
|---|---|---|---|
| 1 | **Silver Widener** | Detune/Pristine | Shift +0.8 Hz-zone, Spread 1.0, Time 8 ms, fb 0.15, Mix 35 |
| 2 | **Slow Tide** | Detune/Dome '64 | ±0.12 Hz, Spread 1.0, Blur 0.3, Mix 50 — breathing pad glue |
| 3 | **Rotary Ghost** | Rotary/Tube 735 | rate 1.3 Hz, Blur 0.4, Drift 0.2, Mix 60 |
| 4 | **Infinite Riser** | Barberpole/Pristine | rate +0.4 Hz, fb 0.88, Damp 9 k, Mix 70 |
| 5 | **Falling Forever** | Barberpole/Tape Loop | rate −0.25 Hz, fb 0.9, Damp 5 k, Blur 0.5 |
| 6 | **Spiral Staircase** | Spiral/Pristine | Shift +90 Hz, Time 1/8D (Sync), fb 0.7, Mix 55 |
| 7 | **Dub Comet** | Spiral/Tape Loop | Shift −45 Hz, Time 1/4, fb 0.8, Damp 3.5 k, Drift 0.5 |
| 8 | **Bell Foundry** | Shift/Pristine | +173 Hz, Sideband 1.0, Low Keep 120 Hz, Mix 100 |
| 9 | **Underwater Mirror** | Shift/Pristine | −220 Hz, **Guard OFF** (reflections!), Mix 100 |
| 10 | **Dalek Intercom** | Shift/Leaky | +35 Hz, **Sideband 0.5 (ring)**, Damp 6 k, Mix 100 |
| 11 | **Laser Decay** | Track/Pristine | Shift +40 Hz, Track −0.8, Mix 80 — dives as each note dies |
| 12 | **Rust Radio** | Shift/Crush | +50 Hz, Sideband 0.8, fb 0.3, Time 30 ms, Drift 0.7, Mix 85 |

Level law: every preset ±1.5 dB of unity vs bypass on the reference chord (the Phase G preset-spread
lesson — no Sludge-quiet / Gargle-hot outliers). Presets live in the house `TIC.presets` menu.

---

## 9. CPU — budget and tiering

Per stereo instance @48 k (mults/sample, both channels): Hilbert 16 · osc 8 (+renorm amortized) ·
sideband/products 8 · Guard +16 when ON · Blur ≤ 16 · Damp/HP/DC ~10 · delay read (cubic) ~8 →
**~80 mult/sample worst-case ≈ 0.05–0.1% of one M-class core — an order under one reverb.**
Tiering: none needed and none built (no Quality dropdown, §3.7). Sleep law: reuse the DST awake-head
pattern (fb342 control-head sleep) — envGate closed + tails < −90 dB for 0.5 s ⇒ skip the loop
entirely (Echobode's "0% when silent" is the same trick). Never oversample anything in this device.

---

## 10. Build order + the perceptual verify harness

**B1** `BodeEngine.h` core: lift `BodeShifter` (TerrainFilters.h:1127) → stereo, signed-Δ + m
sideband form, Guard stage, unity/image cert. **B2** loop: delay (DelayEngine grammar) + Blur +
Damp + envGate + fb audit. **B3** types as config table + kinetic remap + fade-swap. **B4**
Characters (network swap + loop voicings, re-seat law). **B5** chassis: params/relays/UI 11-map,
fb305 six-line edit, FX_ORDER bump, viz. **B6** presets + certs ×2 (pluginval + auval EXIT CODE).

`bod_cert.cpp` (clang++ -O2 -I shim -I Source, the dst_cert pattern) gates:
0. **[AUDIT] DIRECTION — run this one FIRST, it is the cheapest catastrophic bug.** 1 kHz in,
   Δ = +100 Hz, Sideband = up ⇒ energy at **1100 Hz**, not 900 Hz. Reference numbers from
   `scratchpad/bode_sign.py`: main −6.1 dB, image −55.3 dB. Repeat with Δ = −100 Hz and Sideband = down.
   Any inversion here means §3.3's sign was copied wrong (§11.13).
1. **Image rejection**: 1 kHz @ −26 dBFS, shift +100 Hz → image/main ≤ −40 dB (Pristine, 100 Hz–16 k
   sweep; measured headroom: Pristine's worst case is −44.3 dB). Per-Character targets (§4.8) each
   within ±3 dB of spec — **and `Dome '64` must be graded at TWO points (1 kHz ≤ −50 dB AND
   16 kHz ≈ −23 dB ±3)**, because a single mid-band probe cannot tell it from Pristine (§4.8).
2. **Displacement exactness**: partial displacement = shiftHz ± 0.1 Hz across the sweep.
3. **Beat uniformity (Detune)**: beat rate flat across partials (σ < 5%), = 2S inter-channel.
4. **Staircase (Spiral)**: echo n displaced n·S ± 1 Hz, n ≤ 5.
5. **Monotone stripes (Barberpole)**: notch motion never reverses over 30 s; loop RMS bounded
   (fb 0.95, +20 dB cap, pad verified ±1.5 dB program RMS).
6. **Track**: AM-burst probe (Phase G law — static probes are blind here) shows displacement tracking
   the envelope within follower tolerances; static probe identical to `Shift` type.
7. **Click floors**: honest per-Character click floor first (Phase G probe-craft), then every knob
   0→100 sweep while playing ≤ floor + 3 dB; type/char/Guard switches ≤ floor.
8. **Death test (law 6)**: input stops at fb 0.95, Spiral 1 s delay ⇒ wet < −90 dB within 4 s.
9. **Mix law**: 100% wet ⇒ dry residual < −60 dB. **Unity**: defaults-on vs bypass ±0.1 dB magnitude.
10. **Silence metric** (Phase G: bias axes need one): no preset/DAW-default boots quiet (the P6
    shared-slot boot trap — verify all four devices' defaults after adding the 4th).
11. **[AUDIT] Latency = 0** (rack law A): `getLatencySamples() == 0`, and a null test — device powered
    with Mix 0 must reproduce the input **sample-aligned**, not merely spectrally identical.
12. **[AUDIT] Route-mode symmetry** (§3.6): gates 6 and 8 run **twice** — once main-send (unpadded),
    once through a routed pill (−6 dB `kVoiceToFxPad`). `Track` displacement and envGate close-time
    must match within tolerance across the two, or the calibration is single-sided.
13. **[AUDIT] Sync-division reach**: with Sync ON and division "4 bar" at 60 BPM, the measured echo
    spacing is 16.0 s ± 1 ms (proves the delay buffer is sized for the list, §5.2 P3).

---

## 11. Pitfalls — collected

1. **Zipper via the oscillator — solved by construction**: only Δ changes; NEVER reset osc phase on
   param/type change (that is the click). Re-seat allpass states on char swap, don't zero them.
2. **DC**: down-shift parks energy at 0 Hz → DCBlocker at output AND in the fb tap, or the loop
   integrates a thump (in-tree BodeShifter already blocks the fb tap — keep it).
3. **Feedback blow-up**: √2 sideband-comp inside the loop is the sneaky gain stage (§3.5) — audit
   says de-rate by g; ceiling 0.95; envGate closes tails (law 6).
4. **Denormals**: Blur/delay/follower tails — flush + ScopedNoDenormals (the 1e-18 alternation,
   TerrainFilters.h:1101).
5. **LF image breakdown**: **[AUDIT] corrected** — the 90° holds to **~22 Hz at 44.1/48 k** (not 48 Hz);
   the breakdown is real only at **fs ≥ 88.2 k**, where the edge moves to ~40–44 Hz. Default `Low Keep`
   ≥ 60 Hz when fs ≥ 88.2 k; at 44.1/48 k `Low Keep` is a musical anchor, not a repair.
6. **Mono-sum collapse**: Split/Spread → DSB in mono; harness prints mono-fold per preset; manual
   note. (Detune at ≤1 Hz survives mono — say so, producers care.)
7. **Nyquist wrap / zero fold**: musical with Guard OFF, artifact with Guard ON — it is a PILL, and
   toggling fade-swaps (a hard switch of 8 allpasses = click).
8. **fb305**: the six exclusion-sum lines (§5.3) — the single most likely integration bug.
9. **Choice cardinality** (session law ① / rack law C): Character=6; Type=**declare the FINAL length
   today** (§4.0 unresolved, recommend 8 with disabled spares); **`SYN_FX_ORDER` MUST NOT be bumped
   6 → 24 in place** — §5.3 gives the two legal routes. This is the most expensive mistake available
   in this build: it silently reorders every saved user patch.
10. **The Echobode Sync-compensation detail**: Blur adds loop delay — when Time is synced, SUBTRACT
    the blur delay from the delay line so the grid stays on-grid (Echobode manual does exactly this).
11. **WebView**: relay 4-point binding; BinaryData cache-bust; index.html choice lists must match C++
    lists 1:1 (both-lists trap, fb342 ×4).
12. **Don't self-normalise the shifter output program-dependently** (the Tape lesson, DST bible §9.1):
    fixed makeup per Character only, or Drive/feedback dynamics get erased.
13. **[AUDIT] Sideband sign.** The single easiest way to ship this device backwards is to copy
    `y_up = I·C − Q·S` from a textbook that assumes Q *lags*. Ours leads (§3.1b). **Cert it before
    anything else**: `bod_cert` must fail loudly if a +Δ shift puts energy at f−Δ. The in-tree
    `TerrainFilters.h:1168` is the reference.
14. **[AUDIT] Network/`iDelay` coupling.** `iDelay` belongs to the Niemitalo pair only; the dome must
    run without it (§3.1b). Wire it inside the network object — a Character swap that leaves a stale
    `iDelay` in the chain costs 32 dB of image rejection at 1 kHz and reads as "the vintage mode is
    broken".
15. **[AUDIT] Zero latency, forever.** No FIR/linear-phase Hilbert, no lookahead, `setLatencySamples`
    never called (rack law A, §3.7b). A reported latency phase-smears the fb305 dry subtraction.
16. **[AUDIT] The −6 dB route split.** `kVoiceToFxPad` (0.5) is on the routed path only
    (`PluginProcessor.cpp:6300`); the main-send branch is unpadded. Calibrate `Track` + `envGate`
    for both, and cert both (§3.6).

---

## 12. Hard-rule compliance checklist (laws 1–10, explicit)

1. **Bus reality (−26 dBFS)** ⚠️ **PARTIAL** — Track follower normalized to −20 dBFS peak (§3.6);
   envGate ref −38 dBFS; preset levels certed vs unity (§8); no literature thresholds copied. **But
   the −6 dB `kVoiceToFxPad` route split (§3.6) means "the bus" is two levels, and the draft
   calibrated for one. Closes when §3.6's fix is implemented and certed in both route modes.**
2. **Chassis fb275** ✅ front 3 hero + Mix, back 2 dropdowns + 8 knobs 4×2 (the binding constraint),
   12 continuous params — see the §5.2 count correction — pragmatic Title-case names, no jargon
   (Guard not "Anti-Refl", Low Keep not "HPF crossover").
3. **Time params 4 bars → 1/256** ✅ `Time` synced reuses the exact 20-entry SYN_DLY_SYNCDIV list;
   kinetic Sync rates snap to the same list.
4. **Mix 100% = fully wet; switches never cut** ✅ §10 gate 9; fade-swap everywhere (§3.6, §11.7).
5. **Params evolve 0→100, no dead knobs, types night-and-day** ❌ **FAILS AS DRAFTED** — tapers
   §3.6/§5 are fine and Ring is correctly a knob-center rather than a redundant type, **but 7 Type
   names sit on 4 topologies and `Track` is `Shift` with one knob turned up (§4.0). Resolve §4.0
   before B3.** Everything else in this row passes.
6. **Nothing free-runs + loop-gain law** ✅ §3.5: every loop stage audited (incl. the √2 comp),
   ceiling 0.95, envGate kills tails, death test §10.8.
7. **No clicks** ✅ phase-continuous osc, all glides tabled (§5), delay-glide law, fade-swap-recover,
   click-floor gates §10.7.
8. **CPU-friendly** ✅ §9: ~0.1% core, sleep law, NO oversampling ever (§3.7 verdict), Guard
   computed only when on.
9. **Audible ⇄ visible + dramatic** ✅ §6.2: idle-dim/playing-bright, every param walked to a visual
   delta, nobody-else-has-it sliding spectrum.
10. **Recycle first** ✅ §14: engine core, diffuser, delay grammar, sync list, analyzer, chassis,
    preset menu — all named with file:line, **all re-read and corrected in the 2026-08-14 audit**.
11. **[AUDIT] Rack law A — zero lookahead / zero reported latency** ✅ §3.7b (added).
12. **[AUDIT] Rack law B — no runtime param creation** ✅ nothing in this bible creates params at
    runtime; the whole `SYN_BOD_*` block is declared in `createParameterLayout`.
13. **[AUDIT] Rack law C — cardinality frozen at birth** ⚠️ **PARTIAL**: Character=6 is fine; `TYPE`
    must be declared at its final length (§5.2) and **`SYN_FX_ORDER` must not be renumbered** (§5.3).
14. **[AUDIT] Rack law D — the 4th bus joins every exclusion sum** ✅ §5.3, all six lines verified
    at this HEAD.

---

## 13. Open questions for Max

0. **⚠️ [AUDIT] BLOCKING — the Type roster (§4.0).** 7 names sit on 4 topologies and `Track` is a
   preset. Pick **(A) 4 types** / **(B) 7 types with real topology hooks** / **(C) 5 types**. Nothing
   in B3 can start until this is answered, because rack law C freezes the choice count on day one.
1. **Serum 2 Bode screenshot** — panel + its visualizer in motion (one screen grab calibrates §6).
   *(Corrected: the Echobode **lineage** is a feature-parity match, not a confirmed licence — §0.)*
2. **Type list final** — see Q0. The draft's "harness says Detune/Rotary are distinct" claim does not
   survive audit: at matched settings they are the same signal path.
3. **±5 kHz vs Echobode's ±20 kHz Wide** — **now argued, not asserted (§3.6)**: the 1630 and Doepfer's
   A-126-2 both stop at 5 k, and a ±20 k taper costs 16% of the sub-Hz resolution where this device
   actually lives. Ship ±5 kHz. Only override this on taste, and only before ship (it is a one-constant
   change now, a preset-compat break later).
4. **`Guard` default ON** (clean) — or OFF so the mirror drama is the out-of-box sound?
5. **Does Bode take the 4th device slot before Chorus/Compressor?** (This bible assumes yes since
   the research was commissioned; the fb305 edit is identical work whenever it lands.)
6. **Viz pick** — Sliding Spectrum + Rose satellite (my rec), or Barberpole Drum as the hero?

---

## 14. Recycle inventory — **[AUDIT] every row re-opened and read on 2026-08-14**

✅ = cite confirmed exact · ⚠️ = cite or claim corrected below.

| Asset | Where | Reuse |
|---|---|---|
| ✅ `BodeAP` 2nd-order allpass | TerrainFilters.h:1110-1120 | THE network section, verbatim |
| ⚠️ `BodeShifter` (coeffs, osc+renorm, fb+DC, drv) | TerrainFilters.h:1127-1172 | Engine core — stereo-ize, signed-Δ+m, retire fb165 `dirMul`. **Its `setParams` (:1145-1157) is NOT reusable**: log-cutoff-derived shift, `FMAX = 1000`, clamp ±2000 Hz, and `fb = 0.95·res01` with no `g` de-rate / no env gate. Keep `reset()`, `process()` (:1158-1171, **including the `+` sideband sign at :1168**); rewrite `setParams`. |
| ⚠️ `APDiffuser<N>` Schroeder allpass | TerrainFilters.h:1041-1056 | `Blur` stages. **Its name is `APDiffuser`, it is a template on a compile-time length `N`, and `g` is a plain member (0.55 default)** — a 5–35 ms coprime bank means four distinct template instantiations, not a runtime-sized buffer. |
| ✅ `LPBiquad` (RBJ LP, Q = 0.7071) | MoogDelay.h:9-48 | `Damp` / Low Keep sections. Note it self-clamps cutoff to `[20 Hz, 0.45·fs]` — the P6 "40 kHz" top rail is therefore fs-limited, which is correct and matches Echobode's behaviour. |
| ✅ DelayEngine buffer + glide + sync grammar | DelayEngine.h:36-52 (`prepare`, `ceil(16.5·fs)+8`, pow-2); PluginProcessor.cpp:7215-7242 | `Time` lane — **and the buffer-size law of §5.2 P3** |
| ✅ SYN_DLY_SYNCDIV 20-entry list | PluginProcessor.cpp:3455-3459 | `Time`/kinetic sync divisions, verbatim (default index 10 = 1/8) |
| ✅ DistortionEngine shell API + shim law | DistortionEngine.h:1-60 | BodeEngine.h skeleton — confirmed: no TerrainFilters include, `prepare/setters/processSample(inL,inR,outL&,outR&)/flush()`, **`processSample` returns WET ONLY, the processor owns Mix** |
| ⚠️ SYN_DST_* param grammar + send plumbing | ParameterIDs.hpp:**406-431**; PluginProcessor.cpp:6457, 7320 | SYN_BOD_* + 4th send bus (the draft's 406-424 truncated the block) |
| ✅ fb305 exclusion sums | PluginProcessor.cpp:7159, 7161, 7326, 7328, 7358, 7360 | the six-line edit (3 sums × L/R) |
| ⚠️ `SpectrumAnalyzer` **class** | SpectrumAnalyzer.h:20-45 | card viz feed — **but the two existing instances (`analyzerPre/Post`, PluginProcessor.h:924) are the EQ's taps (PluginProcessor.cpp:6865, 6869). Bode needs its own pair + its own gated push lane (PluginEditor.cpp:5881-5904 is the pattern).** |
| ✅ Duck/env follower pattern | PluginProcessor.cpp:7116, 7182 (`rvbDuckActive_`); :3479 + :7269 (`SYN_DLY_DUCK` → `delayEngine.setDucking`) | envGate + Track follower |
| ⚠️ `SmoothRandom` | **TapeMachines.h:215** (used at :456, :716-718, :1167-1196) | `Drift` walk. **Not in DelayEngine** — DelayEngine's wow is a deterministic sine pair (`wowPh`/`flutPh`, DelayEngine.h:154-163), which is the *wrong* thing for `Drift` (Phase G law: a per-sample noise smoother ≠ a walk). Take `SmoothRandom`. |
| ✅ Chassis + preset menu | Design/fx-back-panel-mockup.html, fx-rack-v7-CANONICAL.html, `TIC.presets` (present in ui/public/index.html) | UI, verbatim |
| ✅ `kVoiceToFxPad` | PluginProcessor.cpp:6300 (`0.5f // -6 dB`) | the calibration constant of §3.6 |

---

## 15. Sources

**[AUDIT] fetch status, 2026-08-14.** ✅ = fetched and the bible's claims checked against it ·
🚫 = host refused (403/404) this session, claim left standing but unconfirmed.
✅ yehar.com (Niemitalo) · ✅ doepfer.de/a1262.htm · ✅ Echobode User Guide PDF (extracted to text) ·
✅ soniccharge.com/echobode · ✅ valhalladsp.wordpress.com frequency-shifter category ·
✅ databroth Serum 2 review · ✅ xferrecords Serum 2 web manual (no Bode/Sonic Charge credit found) ·
✅ Wikipedia *Harald Bode* · 🚫 perfectcircuit.com · 🚫 reverb.com · 🚫 valhalladsp.com (main site).
**Local verification scripts written this audit** (`scratchpad/`): `bode_hilb.py` (phase/image response of
both networks), `bode_hilb_edges.py` (band edges at 44.1/48/88.2/96 k), `bode_sign.py` (time-domain sideband
direction). Re-run them before trusting any number in §3.

* Sonic Charge — Echobode product page + **Echobode User Guide PDF** (all params, signal flow, ranges;
  **credits p.14: created by Magnus Lidström, graphics + additional development Fredrik Lidström**):
  https://soniccharge.com/echobode · https://cdn.soniccharge.com/public/Echobode%20User%20Guide.pdf
* Olli Niemitalo — Hilbert transform allpass-pair design + coefficients: https://yehar.com/blog/?p=368
* csound source, `Opcodes/ugsc.c` (hilbert opcode, Hutchins poles, S. Costello 1999):
  https://github.com/csound/csound (Opcodes/ugsc.c:111-143; also tests/soak/hilbert_barberpole.csd)
* Behringer Bode Frequency Shifter 1630 manual (Squelch/Zero/Mixture/±5 kHz):
  https://www.manualslib.com/manual/3143484/Behringer-Bode-Frequency-Shifter-1630.html
* Synth-Werk SW 1630 spec sheet: https://www.synth-werk.com/sites/default/files/pdf/SW%201630%20Bode%20Frequency%20Shifter.pdf
* Doepfer A-126-2 (12-stage dome, <0.3°, 50 Hz–14 kHz): https://schneidersladen.de/en/doepfer-a-126-2-frequency-shifter-v2
* Valhalla DSP blog, frequency-shifter category (binaural 2×S beat, Schroeder 1962 anti-feedback,
  FreqEcho design): https://valhalladsp.wordpress.com/category/frequency-shifter/ ·
  https://valhalladsp.com/2009/09/02/valhallafreqecho-updated-new-controls-vstau/
* Esqueda, Välimäki, Parker — "Barberpole Phasing and Flanging Illusions" (DAFx-15):
  https://www.academia.edu/97507132/Barberpole_phasing_and_flanging_illusions
* Ableton — Shifter announcement + SoS techniques: https://www.ableton.com/en/blog/get-your-freq-on-explore-the-new-shifter-effect-in-live-111/ ·
  https://www.soundonsound.com/techniques/shift-yourself
* Melda MFreqShifterMB (per-band ranges, Character list): https://www.meldaproduction.com/MFreqShifterMB ·
  https://www.pluginboutique.com/articles/62
* Unfiltered Audio Fault manual (±500×5, six feedback paths):
  https://files.plugin-alliance.com/products/unfiltered_audio_fault/unfiltered_audio_fault_manual_en.pdf
* Serum 2 Bode — **feature-parity** sources (⚠️ none of them states a licence; see §0):
  https://sonic-weaponry.com/blogs/free-production-tutorials-and-resources/serum-2-released ·
  https://www.databroth.com/blog/serum-2-review · KVR "Spectral Smearing like Serum 2 Bode's Blur"
  https://www.kvraudio.com/forum/viewtopic.php?t=628429 · Serum 2 web manual root
  https://xferrecords.com/web-manual/serum-2/welcome
* History: **Wikipedia, *Harald Bode*** (dates 1909-1987; Frequency Shifter 1964; Ring Modulator /
  Frequency Shifter / Vocoder 7702 licensed to Moog Music; Melochord at WDR Cologne) —
  https://en.wikipedia.org/wiki/Harald_Bode ·
  Reverb "Find of the Week: A Historic Frequency Shifter" https://reverb.com/news/find-of-the-week-a-historic-frequency-shifter ·
  Bode 735/Moog 1630 lineage https://reverb.com/item/49060248-bode-735-frequency-shifter-1974 ·
  Perfect Circuit "Weird FX: Frequency Shifters" https://www.perfectcircuit.com/signal/frequency-shifters
* Kilohearts Frequency Shifter: https://kilohearts.com/products/frequency_shifter
* Eventide H3000 Dual Shift context: https://www.eventideaudio.com/forums/topic/the-102-dual-shift-algorithm-in-h3000-factory-native/
* Open-source device reference: LMMS frequency shifter PR https://github.com/LMMS/lmms/pull/8140
* In-tree: TerrainFilters.h (BodeShifter), DistortionEngine.h, DelayEngine.h, SpectrumAnalyzer.h,
  PluginProcessor.cpp (lines cited in §5.3/§14), Design/DISTORTION-BUILD-BIBLE.md (house style).
