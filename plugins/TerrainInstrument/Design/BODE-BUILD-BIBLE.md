# Terrain Instrument — Bode Build Bible (frequency shifter, FX device #4)

*Research dossier + build spec. fb-series: the 4th flagship FX device after Reverb / Delay / Distortion.*
*Written 2026-08-14 from primary sources (Sonic Charge Echobode user guide read cover-to-cover, Behringer/Bode
1630 manual, Doepfer A-126-2, csound source, Niemitalo's coefficient page) + in-tree recon with line numbers.
A builder must be able to implement the device from this file alone.*

---

## 0. The scope decision

**ONE device, name `Bode`, 7 Types on one engine.** The Serum 2 FX menu Max is competing with lists
`Bode` as a first-class device — and Serum 2's Bode is the licensed **Echobode** algorithm (Sonic Charge;
the Echobode faceplate credits Fredrik Lidström, and Serum 2 reviews describe it verbatim: *"a frequency
shifter with a delay built into the feedback path and a blurring diffusion control"*). So the reference
bar is not "a frequency shifter knob" — it is **a frequency shifter living inside a feedback delay loop
with diffusion**, which happens to also be the superset topology that produces every classic
frequency-shift trick (barberpole, spiral echoes, detune widening, ring mod) as a *configuration*, not
as separate engines.

**Why this is the cheapest flagship we will ever build:** the core DSP is ALREADY IN THE TREE and
certified as a filter type. `TerrainFilters.h:1110` (`struct BodeAP`) + `TerrainFilters.h:1127`
(`struct BodeShifter`) implement the exact Niemitalo quadrature network, the recursive quadrature
oscillator with renorm, feedback with DC blocking, and the fb165 direction law. The device is that
engine, stereo-ized, wrapped in the Echobode loop topology, on the fb275 chassis.

**What this device is NOT:** a pitch shifter. No granular/PSOLA/phase-vocoder anywhere in this device
(that is a future device). §2 gives the manual-ready explanation of the difference.

---

## 1. History and circuits — the lineage

### 1.1 Harald Bode and the analog SSB shifter

Harald Bode (1909–1987) — the least-famous pioneer whose designs fed both Moog and Buchla — built the
first musician-facing frequency shifters. The **Bode Model 735** (mid-1970s) and the **Moog Model 1630
"Bode Frequency Shifter"** (licensed by Moog from Bode for the Moog Modular) share the same circuit
boards. Bode's earlier shifter work was commissioned for the Columbia-Princeton Electronic Music Center
(Ussachevsky's studio), which is where the sound entered the classic electronic repertoire.

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
the phase error IS the image-sideband leakage (§3.2 gives the exact formula). Bernie Hutchins published
the canonical DIY dome designs in *Electronotes* / the *Musical Engineer's Handbook* (1975), and that
exact 6-pole-per-path network survives today as csound's `hilbert` opcode — **written in 1999 by Sean
M. Costello**, who went on to found Valhalla DSP and build ValhallaFreqEcho. The lineage is one
unbroken 60-year chain, and we hold every link's coefficients (§3.1).

### 1.3 The digital descendants (the greats, with their param sets)

* **Sonic Charge Echobode → Serum 2 `Bode`** *(the direct competitor — full manual absorbed)*.
  Signal flow (manual p.12): `IN → DELAY → SMEAR → FILTER(HP+LP) → FREQ SHIFTER(+PHASE) → SIDEBAND MIX
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
  industrial use of the device).
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

Measured design specs (Niemitalo): phase difference **90° ± 0.7°** over a band of width 0.998·Nyquist
centered on Nyquist/2, passband ripple 0.0002 dB, equivalent stopband (image) attenuation **−44 dB**.
At fs = 48 kHz the honest band is **~48 Hz … 23.95 kHz** — the 90° relationship COLLAPSES below
~48 Hz, which is why deep bass through any allpass-pair shifter grows a loud image. That is not a bug
to fix in the network; it is what the `Low Keep` knob is for (§5). Cost: 8 sections = **8 multiplies
per channel**. The one-sample I-path delay is already in-tree (`iDelay`, `TerrainFilters.h:1162`).

**Secondary (the `Dome '64` Character): the Hutchins/Electronotes dome, via csound.** Two parallel
cascades of **6 first-order allpass sections**, pole ring frequencies = `poles[j] × 15 Hz`
(csound `Opcodes/ugsc.c:111-143`, author Sean M. Costello 1999, poles verbatim from Hutchins'
*Musical Engineer's Handbook*):

```
path 1 (sin): 0.3609,  2.7412,  11.1573,  44.7581, 179.6242,  798.4578    // ×15 Hz
path 2 (cos): 1.2524,  5.5671,  22.3423,  89.6271, 364.7914, 2770.1114    // ×15 Hz
per pole:  f = pole·15;  α = π·f/fs;  β = (1−α)/(1+α);  coef = −β
section:   y[n] = coef·(x[n] − y[n−1]) + x[n−1]                            // 1st-order allpass
```

This is the *analog* voice: rated for 15 Hz–15 kHz, phase ripple several times Niemitalo's, so its
image floor sits ~10–15 dB higher and **rises further at the band edges** — measurably and audibly
"vintage" (§4.8). 12 multiplies per channel.

### 3.2 The image-rejection law (the number that grades everything)

With matched amplitudes, a total quadrature phase error ε makes the unwanted sideband leak at

```
R(ε) = 20·log10( tan(ε/2) )
ε = 1.0° → −41.2 dB     ε = 0.7° → −44.3 dB   (Niemitalo, matches his −44 dB spec)
ε = 0.5° → −47.2 dB     ε = 0.3° → −51.6 dB   (Doepfer A-126-2 class)
```

This is the harness metric (§10): play 1 kHz, shift +100 Hz, measure 900 Hz (image) against 1100 Hz
(main). Ship gate: **≤ −40 dB from 100 Hz to 16 kHz at 48 k** for the clean Characters. The vintage
Characters *deliberately* miss this gate by stated amounts — grade them against their own targets.

### 3.3 The shift itself — complex multiply + quadrature oscillator

```
analytic:  I[n] (delay-aligned path),  Q[n] (+90° path)
oscillator (coupled-form rotation, phase-continuous):
  nC = cosΔ·C − sinΔ·S ;  S = sinΔ·C + cosΔ·S ;  C = nC        // Δ = 2π·shiftHz/fs, SIGNED
  every 512 samples: m = 1.5 − 0.5·(C² + S²);  C·=m;  S·=m      // 1st-order renorm (in-tree, :1167)
sidebands:
  y_up = I·C − Q·S        y_dn = I·C + Q·S
  m    = 2b − 1  (b = Sideband knob 0..1;  −1 = down, 0 = ring, +1 = up)
  y    = g·( I·C − m·Q·S ),   g = sqrt( 2 / (1 + m²) )          // g: +3 dB at center so ring mod
                                                                 // holds equal power vs SSB
```

* **Signed Δ** carries the up/down of the `Shift` knob and is **phase-continuous through zero** —
  turning the knob NEVER clicks because only the rotation increment changes, never the phase. This
  replaces the fb165 `dirMul` hack in the filter version; the device engine uses signed Δ + `m` and
  retires `dirMul`.
* At `Shift = 0, m = +1`: `y = I` = the allpassed input — **unity magnitude, rotated phase** (the
  Echobode manual documents the same: "even with zero frequency shifting, the phases will be
  distorted"). This is our unity-through statement for §7.
* Ring mod (`m = 0`): `y = g·I·C` — classic balanced modulation of the (allpassed) input. Both
  sidebands, carrier suppressed, spectral line count doubles. The Buchla-285 case for free.

### 3.4 Reflection at 0 Hz and Nyquist — and the `Guard` stage (Echobode's Anti-Refl, exact recipe)

Down-shifting a partial below 0 Hz (or up past Nyquist — the digital spectrum is circular, both land
in the negative-frequency half of the complex spectrum) then taking `Re(y)` **folds it back, mirrored
and conjugate**. Musically this mirror is half the charm (the "underwater collapse"), so it must be
**switchable, not silently removed**. The removal stage, when on, is a **positive-frequency projection
applied to the complex product BEFORE taking the real part**:

```
Z = (I + jQ)·e^{jωt}      (Zr = I·C − m·Q·S,  Zi = I·S + m·Q·C — compute both products)
positive-frequency part:  P = ½·(Z + j·Ĥ(Z)),  Ĥ(Z) = H(Zr) + j·H(Zi)
output:  y_guard = Re(P) = ½·( A_I(Zr) − A_Q(Zi) )
```

where `A_I` = the I-cascade (+its 1-sample delay) and `A_Q` = the Q-cascade — i.e. **one more
Niemitalo pair applied to the two product signals**. Cost: +8 multiplies/channel. This matches
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
Therefore: **fbEff = fbKnob · (1/√2 when |m| < 0.5 else 1) · envGate, hard ceiling 0.95** (the DST P8
precedent). The √2 de-rate is applied smoothly as `fb / g` so the Sideband knob can never push a
stable loop unstable mid-sweep. **envGate** = input envelope follower (attack 5 ms, release 250 ms,
squared-release per the Phase G law) mapped `gate = min(1, env/env_ref)` with `env_ref` = −38 dBFS —
i.e. the gate is fully open for any real program on the −26 dBFS bus and **closes ~2.5 s after the
note dies, taking every tail with it. Nothing free-runs; sound dies with the note.** Self-oscillation
drones are therefore *sustain-while-played* only — that is the house reading of FreqEcho's
"self-oscillation" fun.

**Barberpole loop stability is special:** with Delay at minimum (0.02 ms ≈ 1 sample) + fb 0.9, the loop
is a 1-sample comb whose teeth *walk* at shiftHz. Energy at any instant is bounded by
1/(1−fb) ≈ 10× (+20 dB) — so the loop feed is padded by `1−0.7·fb` before the summer (measured-equal
loudness trick: the pad restores ≈unity RMS at fb 0.9 while leaving the resonant character intact).

### 3.6 Param laws (range · taper · glide) — the exact table lives in §5; the derivations:

* **Shift taper:** `shiftHz = sgn(v)·(5001^|v| − 1)`, v ∈ [−1, 1]. ±5 kHz full scale (the 1630's
  range), and the derivative at center is ln(5001) ≈ 8.5 Hz/unit — the first ±10% of travel spans
  ±1.3 Hz, so the sub-Hz magic (detune, rotary) is ON the knob, not hidden (law 5: the whole sweep
  reads). Dial legend mirrors Echobode: 0 · 3 · 10 · 30 · 80 · 200 · 1k · 5k each side.
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

### 3.7 Oversampling verdict: **NEVER.**

The shifter is linear (allpasses + multiplication by a pure sinusoid generate no harmonics; the only
spectral images are the sideband/reflection products, which are handled by suppression and `Guard`,
not by rate). The in-loop Character saturator runs at ≤ unity loop gain on a −26 dBFS bus: measured
budget — `tanh` at those levels produces H3 < −60 dB, whose aliases land < −70 dB. Inaudible;
oversampling would be pure waste (law 8). The `Crush` Character adds deliberate aliasing — that IS the
mode. **No Quality dropdown exists on this device** — one less param than Distortion needed, and the
chassis stays at the official 2 dropdowns.

### 3.8 Denormal / DC / numeric traps (all mandatory)

DCBlocker after the sideband stage AND in the fb tap (down-shift parks real energy AT 0 Hz — without
the blocker the loop integrates it into a thump); flush denormals in the Blur/Delay tails (reuse the
alternating ±1e-18 injection idiom, `TerrainFilters.h:1101`, + `ScopedNoDenormals`); quadrature-osc
renorm every 512 samples (in-tree); `flush()` clears all 16(+16 guard) allpass states, delay, blur,
follower — wired to the device power fade like DST.

---

## 4. Types — 7, each night-and-day, each with its measurable discriminator

*(One engine; a Type = a topology/remap preset over §3.5. All eight back knobs stay live in every
type — the per-type meaning table is in §5.3. Characters in §4.8 apply to all types.)*

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

1. **Pristine** *(default)* — Niemitalo network, no leak, no nonlinearity. Image ≤ −44 dB.
2. **Dome '64** — the Hutchins 6+6 dome (§3.1), band 15 Hz–15 kHz: image floor ~−30 dB rising at the
   edges, slight top droop. The analog phase-ripple sound. *(Tell: image tone at −30 dB on the 1 kHz
   probe, −20 dB at 8 kHz.)*
3. **Tube 735** — Dome network + in-loop `tanh` input stage trimmed for H3 ≈ −40 dB on bus program +
   carrier leak at −50 dB (a faint tonal ghost at the shift frequency itself — every analog unit
   leaks carrier; `Drift` raises it, see below). *(Tell: visible carrier line in the FFT.)*
4. **Leaky** — image suppression deliberately broken to **−18 dB** + carrier −35 dB: both sidebands
   audibly present = the cheap-SSB-radio / walkie-talkie voice, halfway to ring mod at all times.
   *(Tell: image within 18 dB of main.)*
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

## 5. Chassis map — the fb275 device, 11 params

**Device name on the rack: `Bode`.** Grammar copies `SYN_DST_*` (ParameterIDs.hpp:406-424) →
**`SYN_BOD_*`**, + SRC pills / POWER / routing inherited unchanged from the DST device plumbing.

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

**Dropdowns:** `SYN_BOD_TYPE` — choice(7): Shift / Detune / Split / Barberpole / Spiral / Rotary /
Track *(lock the count NOW — session law ①: choice cardinality is forever)*.
`SYN_BOD_CHARACTER` — choice(6): Pristine / Dome '64 / Tube 735 / Leaky / Tape Loop / Crush.

| # | Knob | ID | Range / taper | Glide | Job (pragmatic name = what it does) |
|---|---|---|---|---|---|
| P1 | **Fine** | `SYN_BOD_P1` | bipolar ±2.00 Hz linear | 30 ms | Vernier added to Shift — the 1630 Zero-Adjust. Sub-Hz beating lives here. |
| P2 | **Spread** | `SYN_BOD_P2` | 0..1 → R = L·(1−2s) (R sweeps +S → 0 → −S) | 30 ms | Stereo shift mirror. Split defaults 1.0; every type widens with it. |
| P3 | **Time** | `SYN_BOD_P3` | free 0.02 ms…1 s (log) · Sync pill ON → the 20-entry list `Free,4 bar…1/256` (reuse SYN_DLY_SYNCDIV verbatim, PluginProcessor.cpp:3458) | delay-glide law | Loop delay: micro = phaser comb → flanger → echoes. Law 3 satisfied. |
| P4 | **Blur** | `SYN_BOD_P4` | 0..1 → 0–4 Schroeder allpasses wet, 5–35 ms coprime | 20 ms xfade | In-loop diffusion (Serum 2 `Blur` / Echobode `Smear`): echoes → wash. |
| P5 | **Low Keep** | `SYN_BOD_P5` | 20 Hz…2 kHz log; 20 = off | 30 ms coeff | Crossover below which DRY bypasses the shifter — anchors the bass AND hides the network's <48 Hz image breakdown (§3.1). LR2 crossover, phase-matched sum. |
| P6 | **Damp** | `SYN_BOD_P6` | loop LP 700 Hz…40 kHz log (default 18 k) | 30 ms coeff | In-loop darkening — barberpole/spiral tails die dark like tape. |
| P7 | **Track** | `SYN_BOD_P7` | bipolar → ±1200 Hz·t²·env (§3.6, −26 dBFS-calibrated) | follower does it | Envelope → shift. Center = off. The `Track` type's star; alive in all types. |
| P8 | **Drift** | `SYN_BOD_P8` | 0..1 → shift random-walk 0–6 Hz @ 0.3 Hz (SmoothRandom reuse) + per-Character analog misbehavior (carrier leak ×, wow on Tape Loop) | walk is the glide | Age/instability. 0 = digital-still. |

Param count the fb275 way: **3 + Mix on the front, 8 on the back = 11 knobs**, 2 back dropdowns,
pills — identical accounting to Reverb/Delay/Distortion. Every knob is live in every Type (the
per-type *meaning* shifts are in the rows above; no dead knobs — law 5).

### 5.3 Processor integration

* Engine file `Source/BodeEngine.h`, self-contained + shim-compilable (the DistortionEngine.h law:
  no TerrainFilters include; copy `BodeAP`/DCBlocker in, cite fb283). API mirrors
  DelayEngine/DistortionEngine exactly: `prepare(double)` · clamped setters ·
  `processSample(inL,inR,outL&,outR&)` returning WET ONLY · `flush()`.
* **⚠️ THE fb305 LANDMINE (4th send bus):** every send bus joins EVERY main-send exclusion sum. The
  bode send must be added to the `rtdL/rtdR` sums at **PluginProcessor.cpp:7159, 7161, 7326, 7328,
  7358, 7360** (currently `rvb + dly + dst`) AND gets its own main-send/routed branch like
  `dstSendL/R` (:6457, :7320). Miss one line = the fb305 double-audio bug returns.
* `SYN_FX_ORDER` (:3488, `fxPerm_` :5860) grows to include the 4th device — permutation choice-count
  changes: bump the choice list ONCE, now (session law ①).
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

1. **Sliding Spectrum** *(primary recommendation)* — reuse `SpectrumAnalyzer.h` (4096-pt, triple-
   buffered, ~47 fps feed already in-tree): dry spectrum as a dim white silhouette; wet spectrum as
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
1. **Image rejection**: 1 kHz @ −26 dBFS, shift +100 Hz → image/main ≤ −40 dB (Pristine, 100 Hz–16 k
   sweep); per-Character targets (§4.8) each within ±3 dB of spec.
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
5. **LF image breakdown**: <48 Hz the 90° collapses (§3.1) — `Low Keep` exists for this; default it
   80 Hz in bass-heavy presets, document in manual.
6. **Mono-sum collapse**: Split/Spread → DSB in mono; harness prints mono-fold per preset; manual
   note. (Detune at ≤1 Hz survives mono — say so, producers care.)
7. **Nyquist wrap / zero fold**: musical with Guard OFF, artifact with Guard ON — it is a PILL, and
   toggling fade-swaps (a hard switch of 8 allpasses = click).
8. **fb305**: the six exclusion-sum lines (§5.3) — the single most likely integration bug.
9. **Choice cardinality** (session law ①): Type=7, Character=6, FX_ORDER bump — locked at birth.
10. **The Echobode Sync-compensation detail**: Blur adds loop delay — when Time is synced, SUBTRACT
    the blur delay from the delay line so the grid stays on-grid (Echobode manual does exactly this).
11. **WebView**: relay 4-point binding; BinaryData cache-bust; index.html choice lists must match C++
    lists 1:1 (both-lists trap, fb342 ×4).
12. **Don't self-normalise the shifter output program-dependently** (the Tape lesson, DST bible §9.1):
    fixed makeup per Character only, or Drive/feedback dynamics get erased.

---

## 12. Hard-rule compliance checklist (laws 1–10, explicit)

1. **Bus reality (−26 dBFS)** ✅ Track follower normalized to −20 dBFS peak (§3.6); envGate ref
   −38 dBFS; preset levels certed vs unity (§8); no literature thresholds copied.
2. **Chassis fb275** ✅ front 3 + Mix, back 2 dropdowns + 8 knobs 4×2, 11-param map §5, pragmatic
   Title-case names, no jargon (Guard not "Anti-Refl", Low Keep not "HPF crossover").
3. **Time params 4 bars → 1/256** ✅ `Time` synced reuses the exact 20-entry SYN_DLY_SYNCDIV list;
   kinetic Sync rates snap to the same list.
4. **Mix 100% = fully wet; switches never cut** ✅ §10 gate 9; fade-swap everywhere (§3.6, §11.7).
5. **Params evolve 0→100, no dead knobs, types night-and-day** ✅ tapers §3.6/§5; per-type
   discriminators §4 with harness gates §10; Ring shipped as knob-center, not a redundant type.
6. **Nothing free-runs + loop-gain law** ✅ §3.5: every loop stage audited (incl. the √2 comp),
   ceiling 0.95, envGate kills tails, death test §10.8.
7. **No clicks** ✅ phase-continuous osc, all glides tabled (§5), delay-glide law, fade-swap-recover,
   click-floor gates §10.7.
8. **CPU-friendly** ✅ §9: ~0.1% core, sleep law, NO oversampling ever (§3.7 verdict), Guard
   computed only when on.
9. **Audible ⇄ visible + dramatic** ✅ §6.2: idle-dim/playing-bright, every param walked to a visual
   delta, nobody-else-has-it sliding spectrum.
10. **Recycle first** ✅ §14: engine core, diffuser, delay grammar, sync list, analyzer, chassis,
    preset menu — all named with file:line, all read this session.

---

## 13. Open questions for Max

1. **Serum 2 Bode screenshot** — panel + its visualizer in motion (one screen grab calibrates §6;
   everything else here stands without it — the algorithm lineage is confirmed Echobode).
2. **Type list final** — 7 proposed (§4). Cut `Rotary` to 6 if it reads too close to `Detune` in
   your ears? (Harness says they're distinct — rotation vs static width — but ears rule.)
3. **±5 kHz vs Echobode's ±20 kHz Wide** — I chose the 1630's ±5 kHz (past ~5 k it's all foldover
   chatter on synth program; `Guard OFF + big shift` already covers the trash-register). Want Wide?
4. **`Guard` default ON** (clean) — or OFF so the mirror drama is the out-of-box sound?
5. **Does Bode take the 4th device slot before Chorus/Compressor?** (This bible assumes yes since
   the research was commissioned; the fb305 edit is identical work whenever it lands.)
6. **Viz pick** — Sliding Spectrum + Rose satellite (my rec), or Barberpole Drum as the hero?

---

## 14. Recycle inventory (verified by reading, this session)

| Asset | Where | Reuse |
|---|---|---|
| `BodeAP` 2nd-order allpass | TerrainFilters.h:1110-1120 | THE network section, verbatim |
| `BodeShifter` (coeffs, osc+renorm, fb+DC, drv) | TerrainFilters.h:1127-1172 | Engine core — stereo-ize, signed-Δ+m, retire fb165 `dirMul` |
| Schroeder allpass diffuser | TerrainFilters.h:1040-1057 | `Blur` stages |
| `LPBiquad` | MoogDelay.h:9-45 | `Damp` / Low Keep sections |
| DelayEngine buffer + glide + sync grammar | DelayEngine.h:40; PluginProcessor.cpp:7215-7242 | `Time` lane |
| SYN_DLY_SYNCDIV 20-entry list | PluginProcessor.cpp:3458-3459 | `Time`/kinetic sync divisions, verbatim |
| DistortionEngine shell API + shim law | DistortionEngine.h:1-60 | BodeEngine.h skeleton |
| SYN_DST_* param grammar + send plumbing | ParameterIDs.hpp:406-424; PluginProcessor.cpp:6457,7320 | SYN_BOD_* + 4th send bus |
| fb305 exclusion sums | PluginProcessor.cpp:7159,7161,7326,7328,7358,7360 | the six-line edit |
| `SpectrumAnalyzer` | SpectrumAnalyzer.h:20-40 | card viz feed |
| Duck/env follower pattern | PluginProcessor.cpp (rvbDuck, DLY_DUCK) | envGate + Track follower |
| SmoothRandom wow stack | DelayEngine / TapeMachines.h | `Drift` walk |
| Chassis + preset menu | Design/fx-back-panel-mockup.html, fx-rack-v7-CANONICAL.html, TIC.presets | UI, verbatim |

---

## 15. Sources

* Sonic Charge — Echobode product page + **Echobode User Guide PDF** (all params, signal flow, ranges):
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
* Serum 2 Bode = Echobode confirmations: https://sonic-weaponry.com/blogs/free-production-tutorials-and-resources/serum-2-released ·
  https://www.databroth.com/blog/serum-2-review · KVR "Spectral Smearing like Serum 2 Bode's Blur"
  https://www.kvraudio.com/forum/viewtopic.php?t=628429 · Serum 2 web manual root
  https://xferrecords.com/web-manual/serum-2/welcome
* History: Reverb "Find of the Week: A Historic Frequency Shifter" https://reverb.com/news/find-of-the-week-a-historic-frequency-shifter ·
  Bode 735/Moog 1630 lineage https://reverb.com/item/49060248-bode-735-frequency-shifter-1974 ·
  Perfect Circuit "Weird FX: Frequency Shifters" https://www.perfectcircuit.com/signal/frequency-shifters
* Kilohearts Frequency Shifter: https://kilohearts.com/products/frequency_shifter
* Eventide H3000 Dual Shift context: https://www.eventideaudio.com/forums/topic/the-102-dual-shift-algorithm-in-h3000-factory-native/
* Open-source device reference: LMMS frequency shifter PR https://github.com/LMMS/lmms/pull/8140
* In-tree: TerrainFilters.h (BodeShifter), DistortionEngine.h, DelayEngine.h, SpectrumAnalyzer.h,
  PluginProcessor.cpp (lines cited in §5.3/§14), Design/DISTORTION-BUILD-BIBLE.md (house style).
